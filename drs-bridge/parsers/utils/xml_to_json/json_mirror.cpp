#include "json_mirror.h"

#include <cctype>
#include <cstdio>
#include <map>
#include <vector>

namespace {

constexpr int MAX_DEPTH = 32;
constexpr int MAX_NODES = 2000;

// Build one node's JSON object body: attribute keys + child-element keys,
// merged into ONE flat object (children grouped into arrays only when a
// tag repeats). Last write wins on a key collision (a child overwrites an
// attribute of the same snake_case name) -- see
// docs/ewtss/specs/xml-to-json-parser-design.md §5.
std::string node_object_body(const XmlNode& node, int depth, MirrorBudget& budget) {
    std::vector<std::pair<std::string, std::string>> fields;
    std::map<std::string, size_t> index;

    auto set_field = [&](const std::string& key, const std::string& raw_value) {
        auto it = index.find(key);
        if (it != index.end()) {
            fields[it->second].second = raw_value;
        } else {
            index[key] = fields.size();
            fields.emplace_back(key, raw_value);
        }
    };

    for (const auto& attr : node.attributes)
        set_field(to_snake_case(attr.first), json_quote(attr.second));

    std::vector<std::string> child_order;
    std::map<std::string, std::vector<const XmlNode*>> groups;
    for (const auto& child : node.children) {
        std::string key = to_snake_case(child.name);
        if (groups.find(key) == groups.end()) child_order.push_back(key);
        groups[key].push_back(&child);
    }

    for (const auto& key : child_order) {
        auto& siblings = groups[key];
        std::string value;
        if (siblings.size() == 1) {
            value = node_to_json(*siblings[0], depth + 1, budget);
        } else {
            value = "[";
            for (size_t i = 0; i < siblings.size(); ++i) {
                if (i) value += ',';
                value += node_to_json(*siblings[i], depth + 1, budget);
            }
            value += ']';
        }
        set_field(key, value);
    }

    if (child_order.empty() && node.text.find_first_not_of(" \t\r\n") != std::string::npos)
        set_field("#text", json_quote(node.text));

    std::string out;
    bool first = true;
    for (auto& f : fields) {
        if (!first) out += ',';
        first = false;
        out += json_quote(f.first);
        out += ':';
        out += f.second;
    }
    return out;
}

}  // namespace

std::string to_snake_case(const std::string& s) {
    std::string out;
    size_t n = s.size();
    for (size_t i = 0; i < n; ++i) {
        char c = s[i];
        bool upper = std::isupper((unsigned char)c) != 0;
        if (upper) {
            bool prev_lower_or_digit = i > 0 && (std::islower((unsigned char)s[i - 1]) ||
                                                   std::isdigit((unsigned char)s[i - 1]));
            bool prev_upper = i > 0 && std::isupper((unsigned char)s[i - 1]) != 0;
            bool next_lower = (i + 1 < n) && std::islower((unsigned char)s[i + 1]) != 0;
            if (i > 0 && (prev_lower_or_digit || (prev_upper && next_lower))) out += '_';
            out += (char)std::tolower((unsigned char)c);
        } else {
            out += c;
        }
    }
    return out;
}

std::string json_quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if ((unsigned char)c < 0x20) {
                    char u[8];
                    std::snprintf(u, sizeof(u), "\\u%04x", c);
                    out += u;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
    return out;
}

std::string node_to_json(const XmlNode& node, int depth, MirrorBudget& budget) {
    ++budget.nodes_visited;
    if (depth > MAX_DEPTH || budget.nodes_visited > MAX_NODES) {
        return "{\"truncated\":\"true\",\"node_count\":" + std::to_string(budget.nodes_visited) + "}";
    }

    bool has_attrs = !node.attributes.empty();
    bool has_element_children = !node.children.empty();

    if (!has_attrs && !has_element_children) {
        if (node.text.find_first_not_of(" \t\r\n") == std::string::npos) return "{}";
        return json_quote(node.text);
    }

    return "{" + node_object_body(node, depth, budget) + "}";
}
