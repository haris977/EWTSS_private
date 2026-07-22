#pragma once

// Shared JSON->XML encoder for the RDFS-family control-channel idiom
// (Request/Reply/DDFCLRequest/DDFCLReply wrapping one <Command name="X">
// with zero or more <Param name="K">V</Param>/<Struct name="K">.../<Array
// name="K">... children, nested to any depth) -- the encode-side
// counterpart of pugixml_generic_mirror.h's node_to_json for that same
// idiom. Used by format_response() so callers can hand back exactly the
// "command" JSON shape parse_message() itself produces (see
// docs/ewtss/specs/rdfs-generic-mirror-json-contract.md), instead of a
// pre-built XML string.
//
// Scope: covers the Command+Param(s)/Struct/Array shape used by all of
// Remote_commands.html's 103 commands, including nested-reply commands
// (HwInfo, DeviceInfo, TestPoints, Temperature, TraceInfo, ...) whose
// Command has <Struct>/<Array> children -- these can nest arbitrarily deep
// (e.g. HwInfo's struct-in-struct, TraceInfo's array-of-arrays-under-
// struct). Reversing "param"/"struct"/"array" JSON keys back to
// Param/Struct/Array tags is exact, not a guess: those are the only three
// child-container keys node_to_json ever produces here, and all three are
// single-word tag names with no internal case transition, so
// to_snake_case's transform on them is a deterministic no-op to reverse
// (capitalize the first letter). This is NOT a generic snake_case inverse
// -- that stays lossy for names with internal word boundaries or acronym
// runs (e.g. "GUID" -> "guid" cannot be recovered) -- it is deliberately
// restricted to this closed, known vocabulary. build_command_xml() throws
// std::runtime_error if a Param/Struct/Array entry isn't a
// {"name":..., ...} object (or array of those) -- any other shape is out
// of scope for this encoder.

#include <string>

#include "json.hpp"

// Builds "<Command name=\"...\">...</Command>" XML text from a JSON object
// shaped exactly like parse_message()'s own mirrored "command" value.
// Shapes supported, matching the channels that use this idiom:
//   Request/Reply (control channel):
//     {"name": "<CommandName>",
//      "param":  <absent | one {"name":K,"#text":V,...} object | array of those>,
//      "struct": <absent | one {"name":K, [param|struct|array]...} object | array>,
//      "array":  <absent | one {"name":K, [param|struct|array]...} object | array>}
//     (param/struct/array may all be present at once, e.g. Temperature's
//     reply has both "param" and "array"; struct/array entries may nest
//     further param/struct/array children to any depth, e.g. HwInfo's
//     struct-in-struct.)
//   DDFCLRequest/DDFCLReply (preclassifier channel) -- the value sits
//   directly as the Command's own text, there is no nested <Param>:
//     {"name": "<CommandName>", "#text": "<value>"}
// Throws std::runtime_error (via nlohmann::json's own exceptions, or
// explicitly) on a malformed/out-of-scope shape -- callers must catch, same
// convention as parse_xml_ddf550's try/catch around impl_parse_xml_ddf550.
std::string build_command_xml(const nlohmann::json& command);
