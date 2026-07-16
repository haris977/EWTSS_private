#pragma once

#include <cstddef>
#include <string>

// Parses one XML control message into the validated JSON mirror shape
// (envelope + body). See docs/ewtss/specs/xml-to-json-parser-design.md.
//
// frame_kind_hint: "request" or "reply" -- selects msg_kind when the root
// tag isn't <Event> (an <Event> root always yields msg_kind "event"
// regardless of the hint).
//
// Never throws: any internal exception is caught and reported as a
// msg_kind "internal_error" JSON record instead of propagating. The
// eventual production parse_message() ABI function must instead return -1
// on that path, per the existing ABI contract -- see the top comment in
// xml_to_json.cpp.
std::string parse_xml_to_json(const char* xml, size_t len, const char* frame_kind_hint);
