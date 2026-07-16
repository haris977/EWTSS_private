// DESIGN-VALIDATION PROTOTYPE ONLY -- not the production implementation.
// Demonstrates the agreed CA120 XML->JSON design against real command
// examples before we write the actual ca120_parser.cpp change.
//
// Design decisions encoded here (from the brainstorming session):
//   - envelope: {hw, channel, msg_kind, body: {<root_tag_snake>: {...mirror...}}}
//   - keys: XML tag/attribute names converted to snake_case
//   - repeated siblings: array only when count > 1, else a scalar/object
//   - attributes: merged as plain snake_case keys alongside children -- no "@"
//     prefix or other marker. Accepted trade-off: if an attribute and a child
//     element ever share the same snake_case name on the same node, the child
//     silently overwrites the attribute in this object (last-write-wins on
//     the shared key). Not observed in any real CA120 example checked so far.
//   - values: always JSON strings, never coerced to numbers
//   - empty element: {} (plus any attribute keys it has)
//   - size guard: max depth / max node count, truncated marker beyond that
//   - malformed XML: emit an error JSON instead of failing the call
//   - no raw_xml fallback field: removed per explicit direction -- drs-server
//     has no XML-reading logic, so carrying the original wire bytes alongside
//     the mirror was pure payload bloat with no consumer. body's mirror is
//     now the only representation. Trade-off: a malformed-XML record now
//     carries no way to see the actual bytes that failed to parse (only
//     msg_kind/parse_error/parse_offset) -- flagged, not applied unless asked.
//
// Build (from this directory):
//   g++ -std=c++17 -I ../../../../../../ewtss-v2-design-main/drs-bridge/parsers/utils/pugixml-1.16 \
//       -o proto.exe proto_ca120_generic_mirror.cpp \
//       ../../../../../../ewtss-v2-design-main/drs-bridge/parsers/utils/pugixml-1.16/pugixml.cpp
// (see the actual invocation used below for the real relative path)

#include "pugixml.hpp"

#include <cctype>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// snake_case conversion (matches the existing kSubsystems naming convention
// already used in ca120_parser.cpp, e.g. HopperFilterSeparation -> hopper_filter_separation)
// ---------------------------------------------------------------------------
static std::string to_snake_case(const std::string& s) {
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

// ---------------------------------------------------------------------------
// Minimal JSON string escaping (mirrors json_writer.h's quote())
// ---------------------------------------------------------------------------
static std::string json_quote(const std::string& s) {
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

// ---------------------------------------------------------------------------
// Pretty-printer: reformats a compact JSON string with indentation/line breaks.
// Print-only helper (not part of the design) -- the real DLL still emits
// compact JSON on the wire; this is purely to make console output readable.
// ---------------------------------------------------------------------------
static std::string pretty_json(const std::string& compact) {
    std::string out;
    int indent = 0;
    bool in_string = false;

    auto newline_indent = [&]() {
        out += '\n';
        out += std::string((size_t)indent * 2, ' ');
    };

    for (size_t i = 0; i < compact.size(); ++i) {
        char c = compact[i];

        if (in_string) {
            out += c;
            if (c == '\\' && i + 1 < compact.size()) { out += compact[++i]; continue; }
            if (c == '"') in_string = false;
            continue;
        }

        switch (c) {
            case '"':
                in_string = true;
                out += c;
                break;
            case '{':
            case '[': {
                bool empty_next = (i + 1 < compact.size()) &&
                                   (compact[i + 1] == '}' || compact[i + 1] == ']');
                out += c;
                if (empty_next) { out += compact[++i]; break; }
                ++indent;
                newline_indent();
                break;
            }
            case '}':
            case ']':
                --indent;
                newline_indent();
                out += c;
                break;
            case ',':
                out += c;
                newline_indent();
                break;
            case ':':
                out += ": ";
                break;
            default:
                out += c;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Size guard
// ---------------------------------------------------------------------------
static constexpr int MAX_DEPTH = 32;
static constexpr int MAX_NODES = 2000;

struct MirrorBudget {
    int nodes_visited = 0;
};

// Forward decl
static std::string node_to_json(const pugi::xml_node& node, int depth, MirrorBudget& budget);

// Build one node's JSON object body: attribute keys + child-element keys,
// merged into ONE flat object (children grouped into arrays only when a tag
// repeats). Uses an ordered key->value list with overwrite-on-collision so
// the output JSON never contains a duplicate key even if an attribute and a
// child element happen to share the same snake_case name -- the child wins
// in that case (attributes are inserted first, children are applied after).
static std::string node_object_body(const pugi::xml_node& node, int depth, MirrorBudget& budget) {
    std::vector<std::pair<std::string, std::string>> fields;  // key -> raw JSON value, insertion order
    std::map<std::string, size_t> index;                      // key -> position in fields

    auto set_field = [&](const std::string& key, const std::string& raw_value) {
        auto it = index.find(key);
        if (it != index.end()) {
            fields[it->second].second = raw_value;  // overwrite: last write wins
        } else {
            index[key] = fields.size();
            fields.emplace_back(key, raw_value);
        }
    };

    // Attributes -> plain snake_case keys
    for (pugi::xml_attribute attr : node.attributes())
        set_field(to_snake_case(attr.name()), json_quote(attr.value()));

    // Group child elements by tag name, preserving first-seen order.
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
        set_field(key, value);  // overwrites a same-named attribute, if any
    }

    // Leaf text (only meaningful when there were no element children)
    if (child_order.empty()) {
        std::string text = node.child_value();
        size_t a = text.find_first_not_of(" \t\r\n");
        if (a != std::string::npos) set_field("#text", json_quote(text));
    }

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

// Returns the JSON *value* for a node: an object "{...}", unless it's a pure
// leaf with no attributes (then it's just the text as a plain JSON string,
// or {} if fully empty).
static std::string node_to_json(const pugi::xml_node& node, int depth, MirrorBudget& budget) {
    ++budget.nodes_visited;
    if (depth > MAX_DEPTH || budget.nodes_visited > MAX_NODES) {
        return "{\"truncated\":\"true\",\"node_count\":" + std::to_string(budget.nodes_visited) + "}";
    }

    bool has_attrs = node.first_attribute();
    bool has_element_children = false;
    for (pugi::xml_node child : node.children())
        if (child.type() == pugi::node_element) { has_element_children = true; break; }

    if (!has_attrs && !has_element_children) {
        std::string text = node.child_value();
        size_t a = text.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return "{}";               // empty element
        return json_quote(text);                                 // plain leaf text
    }

    return "{" + node_object_body(node, depth, budget) + "}";
}

// ---------------------------------------------------------------------------
// Top-level: envelope + body, or a malformed-XML error record.
//
// ABI note (sdfc_abi.h rule 2 -- "no C++ types cross the boundary, no
// exceptions escape"): pugixml, unlike the current malloc-only parser code,
// can throw std::bad_alloc internally under memory pressure. The real
// parse_message() in ca120_parser.cpp MUST wrap its call into this function
// (or the equivalent) in try/catch(...) and return -1 on any escape, so an
// allocation failure inside pugixml can never propagate a C++ exception out
// of an extern "C" function. Demonstrated here as impl_parse_xml() +
// parse_xml_prototype() wrapper, mirroring the split the real ABI functions
// would use.
// ---------------------------------------------------------------------------
static std::string impl_parse_xml(const char* xml, size_t len, const char* frame_kind_hint) {
    pugi::xml_document doc;
    pugi::xml_parse_result presult = doc.load_buffer(xml, len);

    if (!presult) {
        std::string out = "{";
        out += "\"hw\":\"ca120\",\"channel\":\"xml\",\"msg_kind\":\"malformed\",";
        out += "\"parse_error\":" + json_quote(presult.description()) + ",";
        out += "\"parse_offset\":" + std::to_string(presult.offset);
        out += "}";
        return out;
    }

    pugi::xml_node root = doc.first_child();
    std::string root_name = root.name();
    bool is_event = (root_name == "Event");

    std::string msg_kind = is_event ? "event"
                          : (frame_kind_hint && std::string(frame_kind_hint) == "request") ? "request"
                          : "reply";

    MirrorBudget budget;
    std::string body_value = node_to_json(root, 0, budget);

    std::string out = "{";
    out += "\"hw\":\"ca120\",\"channel\":\"xml\",\"msg_kind\":" + json_quote(msg_kind) + ",";
    out += "\"body\":{" + json_quote(to_snake_case(root_name)) + ":" + body_value + "}";
    out += "}";
    return out;
}

// This is the shape the real parse_message() ABI function takes: catch
// everything at the boundary, never let a C++ exception escape extern "C".
static std::string parse_xml_prototype(const char* xml, size_t len, const char* frame_kind_hint) {
    try {
        return impl_parse_xml(xml, len, frame_kind_hint);
    } catch (...) {
        // Real ABI function would `return -1;` here instead of a string.
        // The prototype has no -1 path (main() always expects a JSON string
        // back), so it reports the caught exception as its own error JSON
        // purely for this demo's visibility -- the real parse_message()
        // must NOT do this; it returns -1 per the existing ABI contract for
        // malformed/failed input, matching every other error path already
        // in extract_frame/parse_message today.
        return "{\"hw\":\"ca120\",\"channel\":\"xml\",\"msg_kind\":\"internal_error\","
               "\"note\":\"exception caught at ABI boundary, real code returns -1 here\"}";
    }
}

// ---------------------------------------------------------------------------
// Test cases (real command text from CA120 API Training v2.pdf)
// ---------------------------------------------------------------------------
struct Case { const char* label; const char* source; const char* kind; std::string xml; };

int main() {
    std::vector<Case> cases = {
    {"Request-Get FaultReferenceList (empty leaf)", "p.67", "request",
     "<Request type=\"get\" id=\"8\" time=\"6000000\">"
       "<ResourceManager><FaultReferenceList></FaultReferenceList></ResourceManager>"
     "</Request>"},

    {"Request-Set: register a Processing Unit", "p.67", "request",
     "<Request type=\"set\" id=\"3\" time=\"6000000\">"
       "<ResourceManager><Register type=\"processingUnit\">"
         "<GUID>{fde8a6ca-f8c8-4497-b0ee-57c807c38655}</GUID>"
       "</Register></ResourceManager>"
     "</Request>"},

    {"Reply-Set: positive ack (self-closing w/ attribute only)", "p.69", "reply",
     "<Reply type=\"set\" id=\"3\">"
       "<ResourceManager><Register type=\"processingUnit\"/></ResourceManager>"
     "</Reply>"},

    {"Event: ProcessingUnitStatusChange (3x <IP> -> array)", "p.72", "event",
     "<Event origin=\"{origin-guid}\">"
       "<ResourceManager><ProcessingUnitStatusChange>"
         "<GUID>{fde8a6ca-f8c8-4497-b0ee-57c807c38655}</GUID>"
         "<OldRegisterStatus>notRegistered</OldRegisterStatus>"
         "<NewRegisterStatus>registeredActive</NewRegisterStatus>"
         "<Hostname>sukSEs-JTO</Hostname>"
         "<IP>172.26.251.11</IP>"
         "<IP>192.168.100.241</IP>"
         "<IP>192.168.56.1</IP>"
         "<Port>55848</Port>"
       "</ProcessingUnitStatusChange></ResourceManager>"
     "</Event>"},

    {"Request-Set: multi-Control DataStream start (the bug case)", "p.161", "request",
     "<Request type=\"set\" id=\"1419\" time=\"6000000\">"
       "<Control><DataStream action=\"start\" type=\"processorSpectrum\">"
         "<Protocol>tcp</Protocol><IP>192.168.100.123</IP><Port>49526</Port>"
       "</DataStream></Control>"
       "<Control><DataStream action=\"start\" type=\"timeDomain\">"
         "<Protocol>tcp</Protocol><IP>192.168.100.123</IP><Port>49527</Port>"
       "</DataStream></Control>"
     "</Request>"},

    {"Malformed: mismatched closing tag", "synthetic", "request",
     "<Request type=\"set\" id=\"1\"><Tuner></Request>"},

    {"Event: StartApplication (dcp) -- 3x IP, plain text children", "p.136", "event",
     "<Event origin=\"{fde8a6ca-f8c8-4497-b0ee-57c807c38655}\">"
       "<ResourceManager><StartApplication type=\"dcp\">"
         "<Port>57150</Port>"
         "<GUID>{afc6ed3c-bf80-4a37-8ec7-22fa754b3c38}</GUID>"
         "<IP>172.26.251.11</IP>"
         "<IP>192.168.100.241</IP>"
         "<IP>192.168.56.1</IP>"
         "<ProcessingUnit>{fde8a6ca-f8c8-4497-b0ee-57c807c38655}</ProcessingUnit>"
         "<ResourceDemandClass>scp</ResourceDemandClass>"
       "</StartApplication></ResourceManager>"
     "</Event>"},

    {"Event: Input (attribute + text -- the #text case)", "p.136", "event",
     "<Event origin=\"{afc6ed3c-bf80-4a37-8ec7-22fa754b3c38}\">"
       "<Control><Input sourceType=\"tuner\">{9bc1a79a-e07a-41d6-8d44-9e7b9ce19e5d}</Input></Control>"
     "</Event>"},

    {"Event: FreeCapacity (attribute + large numeric text -- #text, big number as string)", "p.136", "event",
     "<Event origin=\"{1736b69c-f51f-4404-bac8-0eb90058603c}\">"
       "<Control><StorageUnit>"
         "<GUID>{6f402679-cc03-4f2b-a1d0-304badb0beb2}</GUID>"
         "<FreeCapacity unit=\"bytes\">605777170432</FreeCapacity>"
       "</StorageUnit></Control>"
     "</Event>"},

    {"Event: Status (Lock is empty, no attributes -- {} case)", "p.136", "event",
     "<Event origin=\"{afc6ed3c-bf80-4a37-8ec7-22fa754b3c38}\">"
       "<Control><Status>"
         "<Idle>0</Idle>"
         "<Mode>singleChannel</Mode>"
         "<Processing>sw</Processing>"
         "<Lock/>"
       "</Status></Control>"
     "</Event>"},
    };

    for (auto& c : cases) {
        std::printf("\n--- %s (%s) ---\n", c.label, c.source);
        std::string json = parse_xml_prototype(c.xml.c_str(), c.xml.size(), c.kind);
        std::printf("%s\n", pretty_json(json).c_str());
    }
    return 0;
}
