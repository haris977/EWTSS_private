#pragma once

#include <string>
#include <utility>
#include <vector>

// A single parsed XML element: its tag name, its attributes in document
// order, its child elements in document order, and (only meaningful when
// children is empty) its text content.
// See docs/ewtss/specs/xml-to-json-parser-design.md.
struct XmlNode {
    std::string name;
    std::vector<std::pair<std::string, std::string>> attributes;
    std::vector<XmlNode> children;
    std::string text;
};
