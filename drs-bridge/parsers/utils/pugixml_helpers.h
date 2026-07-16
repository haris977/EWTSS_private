#pragma once

#include <string>

#include "pugixml.hpp"

// Returns the first descendant-or-self element named `tag`, in document
// order, or a null node if none exists. A null node's .text().get() and
// .attribute(...).value() both safely return "" (pugixml guarantee), so
// callers don't need to check for null before reading. Deliberately
// unscoped (searches the whole subtree, not just direct children) --
// several ICDs nest the tag of interest two levels deep (e.g. CA120's
// DataStream under Control under Request).
inline pugi::xml_node find_first(pugi::xml_node root, const char* tag) {
    std::string xpath = std::string(".//") + tag;
    return root.select_node(xpath.c_str()).node();
}
