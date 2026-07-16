#pragma once

#include <string>

#include "xml_node.h"

// snake_case conversion for XML tag/attribute names (matches the naming
// convention already used in ca120_parser.cpp, e.g.
// HopperFilterSeparation -> hopper_filter_separation).
std::string to_snake_case(const std::string& name);

// JSON string escaping (mirrors json_writer.h's quote()).
std::string json_quote(const std::string& value);

// Bounds how much of a document node_to_json will mirror -- see
// docs/ewtss/specs/xml-to-json-parser-design.md §4 (MAX_DEPTH/MAX_NODES).
struct MirrorBudget {
    int nodes_visited = 0;
};

// Mirrors a single XmlNode subtree into a JSON *value* fragment: an object
// "{...}", a plain quoted string for a pure text leaf, or "{}" for an
// empty leaf. See docs/ewtss/specs/xml-to-json-parser-design.md §5.
std::string node_to_json(const XmlNode& node, int depth, MirrorBudget& budget);
