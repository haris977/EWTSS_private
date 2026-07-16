#pragma once

#include <cstddef>
#include <string>

// Parses one XML control message into the validated JSON mirror shape
// (envelope + body). See docs/ewtss/specs/xml-to-json-parser-design.md.
//
// hw: the value to stamp into the envelope's "hw" field (e.g. "ca120",
// "ddf550", "ddf1gtx") -- each hardware variant's own parser passes its
// own name; this shared utility does not hardcode one.
//
// frame_kind_hint: "request" or "reply" -- selects msg_kind when the root
// tag isn't <Event> (an <Event> root always yields msg_kind "event"
// regardless of the hint).
//
// Never throws: any internal exception is caught and reported as a
// msg_kind "internal_error" JSON record instead of propagating. A null
// xml, frame_kind_hint, or hw pointer is also handled without crashing.
std::string parse_xml_to_json(const char* xml, size_t len, const char* frame_kind_hint, const char* hw);
