#pragma once

#include <cstddef>
#include <string>

#include "xml_node.h"

// Result of parse_xml(): either a parsed tree (ok == true, root populated)
// or an error with the byte offset it was detected at (ok == false).
struct XmlParseResult {
    bool ok = false;
    XmlNode root;
    std::string error;
    size_t error_offset = 0;
};

// Parses a single well-formed XML document (exactly one root element) out
// of xml[0..len). Supports: attributes (single- or double-quoted), nested
// elements, self-closing tags, repeated sibling tags, and plain text
// leaves. Does not support CDATA, comments, namespaces, entity references,
// or <?xml?> declarations -- encountering any of those is a parse error,
// not silently ignored. See docs/ewtss/specs/xml-to-json-parser-design.md §4.
XmlParseResult parse_xml(const char* xml, size_t len);
