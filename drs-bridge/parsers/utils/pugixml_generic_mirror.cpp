#include "pugixml_generic_mirror.h"

#include <cctype>
#include <cstdio>
#include <map>
#include <vector>

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

std::string mirror_json_quote(const std::string& s) {
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
                } else out += c;
        }
    }
    out += '"';
    return out;
}

static constexpr int MIRROR_MAX_DEPTH = 32;
static constexpr int MIRROR_MAX_NODES = 2000;

// One node's JSON object body: attribute keys + child-element keys, merged
// into one flat object. Attributes are inserted first, children after, so a
// child element sharing an attribute's snake_case name overwrites it
// (last-write-wins; not observed in any real ICD sample across the family).
static std::string node_object_body(const pugi::xml_node& node, int depth, MirrorBudget& budget) {
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

    for (pugi::xml_attribute attr : node.attributes())
        set_field(to_snake_case(attr.name()), mirror_json_quote(attr.value()));

    std::vector<std::string> child_order;
    std::map<std::string, std::vector<pugi::xml_node>> groups;
    for (pugi::xml_node child : node.children()) {
        if (child.type() != pugi::node_element) continue;
        std::string key = to_snake_case(child.name());
        if (groups.find(key) == groups.end()) child_order.push_back(key);
        groups[key].push_back(child);
    }

    for (const auto& key : child_order) {
        auto& siblings = groups[key];
        std::string value;
        if (siblings.size() == 1) {
            value = node_to_json(siblings[0], depth + 1, budget);
        } else {
            value = "[";
            for (size_t i = 0; i < siblings.size(); ++i) {
                if (i) value += ',';
                value += node_to_json(siblings[i], depth + 1, budget);
            }
            value += ']';
        }
        set_field(key, value);
    }

    // Leaf text only meaningful when there were no element children.
    if (child_order.empty()) {
        std::string text = node.child_value();
        size_t a = text.find_first_not_of(" \t\r\n");
        if (a != std::string::npos) set_field("#text", mirror_json_quote(text));
    }

    std::string out;
    bool first = true;
    for (auto& f : fields) {
        if (!first) out += ',';
        first = false;
        out += mirror_json_quote(f.first);
        out += ':';
        out += f.second;
    }
    return out;
}

std::string node_to_json(const pugi::xml_node& node, int depth, MirrorBudget& budget) {
    ++budget.nodes_visited;
    if (depth > MIRROR_MAX_DEPTH || budget.nodes_visited > MIRROR_MAX_NODES) {
        return "{\"truncated\":\"true\",\"node_count\":" + std::to_string(budget.nodes_visited) + "}";
    }

    bool has_attrs = node.first_attribute();
    bool has_element_children = false;
    for (pugi::xml_node child : node.children())
        if (child.type() == pugi::node_element) { has_element_children = true; break; }

    if (!has_attrs && !has_element_children) {
        std::string text = node.child_value();
        size_t a = text.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return "{}";
        return mirror_json_quote(text);
    }

    return "{" + node_object_body(node, depth, budget) + "}";
}

std::string build_malformed_envelope(const char* hw, const char* channel,
                                      const pugi::xml_parse_result& presult) {
    std::string out = "{\"hw\":" + mirror_json_quote(hw) + ",\"channel\":" + mirror_json_quote(channel) + ",";
    out += "\"msg_kind\":\"malformed\",";
    out += "\"parse_error\":" + mirror_json_quote(presult.description()) + ",";
    out += "\"parse_offset\":" + std::to_string(presult.offset);
    out += "}";
    return out;
}

std::string build_mirror_envelope(const char* hw, const char* channel,
                                   const char* msg_kind, const pugi::xml_node& root) {
    MirrorBudget budget;
    std::string body_value = node_to_json(root, 0, budget);

    std::string out = "{\"hw\":" + mirror_json_quote(hw) + ",\"channel\":" + mirror_json_quote(channel) + ",";
    out += "\"msg_kind\":" + mirror_json_quote(msg_kind) + ",";
    out += "\"body\":{" + mirror_json_quote(to_snake_case(root.name())) + ":" + body_value + "}}";
    return out;
}
