// ABI note (matches proto_ca120_generic_mirror.cpp and the existing
// parse_message() contract): no C++ types cross the boundary, no
// exceptions escape. This function never throws; a caught exception
// becomes an "internal_error" JSON record here purely for standalone
// visibility. The real ca120_parser.cpp parse_message() integration must
// instead return -1 on that path, matching every other error path already
// in extract_frame/parse_message today.

#include "xml_to_json.h"

#include "json_mirror.h"
#include "xml_node.h"
#include "xml_parser.h"

namespace {

std::string impl_parse_xml_to_json(const char* xml, size_t len, const char* frame_kind_hint, const char* hw) {
    std::string hw_str = hw ? hw : "unknown";

    XmlParseResult presult = parse_xml(xml, len);

    if (!presult.ok) {
        std::string out = "{";
        out += "\"hw\":" + json_quote(hw_str) + ",\"channel\":\"xml\",\"msg_kind\":\"malformed\",";
        out += "\"parse_error\":" + json_quote(presult.error) + ",";
        out += "\"parse_offset\":" + std::to_string(presult.error_offset);
        out += "}";
        return out;
    }

    const XmlNode& root = presult.root;
    bool is_event = (root.name == "Event");
    std::string msg_kind = is_event ? "event"
                          : (frame_kind_hint && std::string(frame_kind_hint) == "request") ? "request"
                          : "reply";

    MirrorBudget budget;
    std::string body_value = node_to_json(root, 0, budget);

    std::string out = "{";
    out += "\"hw\":" + json_quote(hw_str) + ",\"channel\":\"xml\",\"msg_kind\":" + json_quote(msg_kind) + ",";
    out += "\"body\":{" + json_quote(to_snake_case(root.name)) + ":" + body_value + "}";
    out += "}";
    return out;
}

}  // namespace

std::string parse_xml_to_json(const char* xml, size_t len, const char* frame_kind_hint, const char* hw) {
    try {
        return impl_parse_xml_to_json(xml, len, frame_kind_hint, hw);
    } catch (...) {
        std::string hw_str = hw ? hw : "unknown";
        return "{\"hw\":" + json_quote(hw_str) + ",\"channel\":\"xml\",\"msg_kind\":\"internal_error\","
               "\"note\":\"exception caught at xml_to_json boundary\"}";
    }
}
