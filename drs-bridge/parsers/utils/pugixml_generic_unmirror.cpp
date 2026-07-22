#include "pugixml_generic_unmirror.h"

#include <sstream>
#include <stdexcept>

#include "pugixml.hpp"

namespace {

// The only child-container keys the mirror (pugixml_generic_mirror.cpp)
// ever produces under a Command/Param/Struct/Array node, and the exact XML
// tag each reverses to. Order matches every real ICD example: Param(s)
// first, then Struct, then Array.
//
// This fixed list is NOT a generic snake_case inverse (that would be lossy
// for names with internal word-boundaries or acronym runs, e.g.
// "GUID" -> "guid" cannot be recovered) -- to_snake_case only ever produces
// these four tag names as-is (Command/Param/Struct/Array are single-word,
// no internal case transition, so snake_case is just a lowercase-first-
// letter no-op), so capitalizing the first letter back is exact, not a
// guess. Restricting to this closed vocabulary is what keeps the reversal
// safe; do not generalize this loop to arbitrary JSON keys.
constexpr const char* kChildKeys[] = {"param", "struct", "array"};
constexpr const char* kChildTags[] = {"Param", "Struct", "Array"};
constexpr size_t kNumChildKinds = 3;

void append_member(pugi::xml_node& parent, const std::string& tag,
                    const nlohmann::json& value);

// Appends one or more <tag name="...">...</tag> children under `parent`
// from `value`. `value` is either:
//   - a flat {"name":K,"#text":V} leaf (the common Param case), or
//   - a container with its own nested param/struct/array children
//     (Struct/Array can nest arbitrarily deep -- e.g. HwInfo's
//     struct-in-struct, TraceInfo's array-of-arrays-under-struct), or
//   - a JSON array of either of the above (repeated sibling tag, per the
//     mirror's one-or-many rule -- node_to_json only ever arrays a
//     *repeated* tag, so this recurses with the same `tag` for each entry).
void append_member(pugi::xml_node& parent, const std::string& tag,
                    const nlohmann::json& value) {
    if (value.is_array()) {
        for (const auto& entry : value) append_member(parent, tag, entry);
        return;
    }
    if (!value.is_object() || !value.contains("name")) {
        throw std::runtime_error(
            "build_command_xml: <" + tag + "> entry is not a {\"name\":..} "
            "object (or array of those) -- shape out of scope for this encoder");
    }

    pugi::xml_node node = parent.append_child(tag.c_str());
    node.append_attribute("name") = value.at("name").get<std::string>().c_str();

    bool wrote_child = false;
    for (size_t i = 0; i < kNumChildKinds; ++i) {
        if (!value.contains(kChildKeys[i])) continue;
        append_member(node, kChildTags[i], value.at(kChildKeys[i]));
        wrote_child = true;
    }
    if (!wrote_child && value.contains("#text")) {
        node.text().set(value.at("#text").get<std::string>().c_str());
    }
}

} // namespace

std::string build_command_xml(const nlohmann::json& command) {
    if (!command.is_object() || !command.contains("name")) {
        throw std::runtime_error("build_command_xml: \"command\" object missing \"name\"");
    }

    pugi::xml_document doc;
    pugi::xml_node cmd = doc.append_child("Command");
    cmd.append_attribute("name") = command.at("name").get<std::string>().c_str();

    // DDFCL error-reply idiom (DDFSystemControlInterfacePreClassifier.pdf
    // Table 6-5, "Invalid command name"): <Command name="X" returnCode="N"
    // returnMessage="..."> -- the only two extra Command-level attributes
    // this ICD family ever uses, so (like param/struct/array) reversing
    // their snake_case keys back to exact camelCase is a closed, safe
    // lookup, not a guess.
    if (command.contains("return_code"))
        cmd.append_attribute("returnCode") = command.at("return_code").get<std::string>().c_str();
    if (command.contains("return_message"))
        cmd.append_attribute("returnMessage") = command.at("return_message").get<std::string>().c_str();

    // Request/Reply idiom: <Command name="X"><Param.../><Struct.../><Array.../></Command>
    bool wrote_child = false;
    for (size_t i = 0; i < kNumChildKinds; ++i) {
        if (!command.contains(kChildKeys[i])) continue;
        append_member(cmd, kChildTags[i], command.at(kChildKeys[i]));
        wrote_child = true;
    }
    if (!wrote_child && command.contains("#text")) {
        // DDFCLRequest/DDFCLReply idiom: <Command name="X">value</Command> --
        // the value sits directly as the Command's own text, no nested Param.
        cmd.text().set(command.at("#text").get<std::string>().c_str());
    }

    std::ostringstream oss;
    cmd.print(oss, "", pugi::format_raw);
    return oss.str();
}
