#pragma once

// Shared XML->JSON generic tree mirror used by every RDFS-family parser
// (CA120, DDF-550, DDF-1GTX) so their parse_message output follows one
// common structure: {hw, channel, msg_kind, body: {<root_tag_snake>: {...}}}
// on success, {hw, channel, msg_kind:"malformed", parse_error, parse_offset}
// on malformed XML. Each variant supplies its own hw/channel/msg_kind
// (device-specific: root-tag vocabulary and channel taxonomy differ per
// ICD) and calls into this shared code for the parts that must be
// byte-for-byte identical across the family: snake_case conversion, JSON
// escaping, and the tree-mirroring algorithm itself.
//
// Ported from the approved design-validation prototype,
// drs-bridge/parsers/utils/proto_ca120_generic_mirror.cpp.

#include <string>

#include "pugixml.hpp"

// Converts an XML tag/attribute name to snake_case (e.g. "DataStream" ->
// "data_stream", "GUID" -> "guid"). Underscore only inserted at a genuine
// word-boundary transition -- lowercase/digit -> uppercase, or the last
// uppercase letter of a run before it drops into lowercase -- not before
// every capital (so "GUID" doesn't become "g_u_i_d").
std::string to_snake_case(const std::string& s);

// Plain JSON string escaping + quoting: ", \, \n, \r, \t, and control
// characters below 0x20 as \u00XX. Nothing else is special.
std::string mirror_json_quote(const std::string& s);

// Recursion/node-count guard shared across one node_to_json() call tree.
struct MirrorBudget {
    int nodes_visited = 0;
};

// Mirrors one pugixml node (and its whole subtree) into a JSON value:
//   - {} for an empty element (no attributes, no children, no text)
//   - a plain quoted string for a pure leaf with text and no attributes
//   - otherwise an object: attributes + child elements merged into one flat
//     object (attributes first, then children -- a same-named child element
//     overwrites an attribute, last-write-wins); a repeated child tag
//     becomes a JSON array, a single occurrence stays a scalar/object; a
//     node with element children AND non-whitespace direct text gets a
//     synthetic "#text" key for that text.
// depth/budget guard against pathological input: beyond MAX_DEPTH (32) or
// MAX_NODES (2000) visited, returns a {"truncated":...} marker instead of
// recursing further.
std::string node_to_json(const pugi::xml_node& node, int depth, MirrorBudget& budget);

// Builds the malformed-XML envelope. No "body" key -- parsing never
// completed far enough to have a tree to mirror.
std::string build_malformed_envelope(const char* hw, const char* channel,
                                      const pugi::xml_parse_result& presult);

// Builds the success envelope by mirroring `root`'s whole subtree under
// body.<snake_case root tag name>.
std::string build_mirror_envelope(const char* hw, const char* channel,
                                   const char* msg_kind, const pugi::xml_node& root);
