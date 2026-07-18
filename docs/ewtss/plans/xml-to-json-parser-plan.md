# From-scratch XML→JSON Parser Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a small, dependency-free, hand-written XML→JSON module at `drs-bridge/parsers/utils/xml_to_json/` that reproduces the JSON mirror design already validated in `proto_ca120_generic_mirror.cpp`, without depending on pugixml.

**Architecture:** Two independently testable stages — `xml_parser` (XML text → a tiny in-memory `XmlNode` tree via hand-written recursive descent) and `json_mirror` (`XmlNode` tree → JSON string, a near-verbatim port of the prototype's `node_to_json`/`node_object_body`). A thin `xml_to_json` entry point wires the two together into the envelope shape (`{hw, channel, msg_kind, body}`) and guarantees no exception escapes.

**Tech Stack:** C++17, no external libraries, no test framework (standalone `main()`-based harness matching the prototype's own convention), built with `g++` (MSYS2/MinGW, confirmed available: `g++ 15.2.0`).

## Global Constraints

- C++17, standard library only (`<string>`, `<vector>`, `<map>`, `<utility>`, `<cctype>`, `<cstdio>`, `<cstddef>`) — no third-party dependency.
- No exceptions escape the module's public entry point (`parse_xml_to_json`) — matches the ABI rule already documented in the prototype ("no C++ types cross the boundary, no exceptions escape").
- Supported XML subset only: attributes (single- or double-quoted), nested elements, self-closing tags, repeated sibling tags, plain text leaves. CDATA, comments, namespaces, entity references, and `<?xml?>` declarations are **not** supported — encountering one is a parse error, never silently ignored or mis-parsed.
- JSON mirror shape is unchanged from the validated design in `proto_ca120_generic_mirror.cpp`: envelope `{hw, channel, msg_kind, body: {<root_tag_snake>: {...}}}`; XML tag/attribute names → snake_case; attributes and children merge into one flat object (children win on key collision); repeated siblings → array only when count > 1; values always JSON strings, never coerced; empty element → `{}`; malformed XML → `{hw, channel, msg_kind:"malformed", parse_error, parse_offset}`, no `raw_xml` fallback.
- `MAX_DEPTH = 32`, `MAX_NODES = 2000` guards on the JSON-mirroring pass (same constants as the prototype).
- Full design rationale: [`docs/ewtss/specs/xml-to-json-parser-design.md`](../specs/xml-to-json-parser-design.md) — read it first if anything below is unclear.
- This module is built and tested standalone. It is **not** wired into `ca120_parser.cpp`, `ddf550_parser.cpp`, or `ddf1gtx_parser.cpp` by this plan — that integration is a separate, later decision.

---

### Task 1: `XmlNode` data model + recursive-descent XML parser

**Files:**
- Create: `drs-bridge/parsers/utils/xml_to_json/xml_node.h`
- Create: `drs-bridge/parsers/utils/xml_to_json/xml_parser.h`
- Create: `drs-bridge/parsers/utils/xml_to_json/xml_parser.cpp`
- Create: `drs-bridge/parsers/utils/xml_to_json/.gitignore`
- Test: `drs-bridge/parsers/utils/xml_to_json/tests/test_xml_parser.cpp`

**Interfaces:**
- Consumes: nothing (foundational task).
- Produces:
  - `struct XmlNode { std::string name; std::vector<std::pair<std::string,std::string>> attributes; std::vector<XmlNode> children; std::string text; };`
  - `struct XmlParseResult { bool ok; XmlNode root; std::string error; size_t error_offset; };`
  - `XmlParseResult parse_xml(const char* xml, size_t len);`

- [ ] **Step 1: Create the data model header**

`drs-bridge/parsers/utils/xml_to_json/xml_node.h`:

```cpp
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
```

- [ ] **Step 2: Create the parser header**

`drs-bridge/parsers/utils/xml_to_json/xml_parser.h`:

```cpp
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
```

- [ ] **Step 3: Create a stub implementation (compiles, always fails)**

`drs-bridge/parsers/utils/xml_to_json/xml_parser.cpp`:

```cpp
#include "xml_parser.h"

XmlParseResult parse_xml(const char*, size_t) {
    return XmlParseResult{};  // ok == false, everything else default-empty
}
```

- [ ] **Step 4: Write the failing test**

`drs-bridge/parsers/utils/xml_to_json/tests/test_xml_parser.cpp`:

```cpp
#include <cstdio>
#include <string>

#include "../xml_node.h"
#include "../xml_parser.h"

namespace {

int failures = 0;

void check(bool cond, const std::string& message) {
    if (!cond) {
        std::printf("FAIL: %s\n", message.c_str());
        ++failures;
    }
}

void test_attribute_only_self_closing() {
    std::string xml = "<Register type=\"processingUnit\"/>";
    XmlParseResult r = parse_xml(xml.c_str(), xml.size());
    check(r.ok, "attribute_only_self_closing: expected ok");
    check(r.root.name == "Register", "attribute_only_self_closing: name");
    check(r.root.attributes.size() == 1, "attribute_only_self_closing: attribute count");
    check(r.root.attributes.size() == 1 && r.root.attributes[0].first == "type" &&
              r.root.attributes[0].second == "processingUnit",
          "attribute_only_self_closing: attribute value");
    check(r.root.children.empty(), "attribute_only_self_closing: no children");
    check(r.root.text.empty(), "attribute_only_self_closing: no text");
}

void test_text_only_leaf() {
    std::string xml = "<Hostname>sukSEs-JTO</Hostname>";
    XmlParseResult r = parse_xml(xml.c_str(), xml.size());
    check(r.ok, "text_only_leaf: expected ok");
    check(r.root.name == "Hostname", "text_only_leaf: name");
    check(r.root.attributes.empty(), "text_only_leaf: no attributes");
    check(r.root.text == "sukSEs-JTO", "text_only_leaf: text");
}

void test_attribute_and_text() {
    std::string xml = "<Input sourceType=\"tuner\">{9bc1a79a-e07a-41d6-8d44-9e7b9ce19e5d}</Input>";
    XmlParseResult r = parse_xml(xml.c_str(), xml.size());
    check(r.ok, "attribute_and_text: expected ok");
    check(r.root.attributes.size() == 1 && r.root.attributes[0].second == "tuner",
          "attribute_and_text: attribute value");
    check(r.root.text == "{9bc1a79a-e07a-41d6-8d44-9e7b9ce19e5d}", "attribute_and_text: text");
}

void test_nested_single_child() {
    std::string xml = "<Register><GUID>{fde8a6ca-f8c8-4497-b0ee-57c807c38655}</GUID></Register>";
    XmlParseResult r = parse_xml(xml.c_str(), xml.size());
    check(r.ok, "nested_single_child: expected ok");
    check(r.root.children.size() == 1, "nested_single_child: child count");
    check(r.root.children.size() == 1 && r.root.children[0].name == "GUID",
          "nested_single_child: child name");
    check(r.root.children.size() == 1 &&
              r.root.children[0].text == "{fde8a6ca-f8c8-4497-b0ee-57c807c38655}",
          "nested_single_child: child text");
}

void test_repeated_siblings() {
    std::string xml =
        "<ProcessingUnitStatusChange>"
          "<IP>172.26.251.11</IP>"
          "<IP>192.168.100.241</IP>"
          "<IP>192.168.56.1</IP>"
        "</ProcessingUnitStatusChange>";
    XmlParseResult r = parse_xml(xml.c_str(), xml.size());
    check(r.ok, "repeated_siblings: expected ok");
    check(r.root.children.size() == 3, "repeated_siblings: child count");
    if (r.root.children.size() == 3) {
        check(r.root.children[0].name == "IP" && r.root.children[1].name == "IP" &&
                  r.root.children[2].name == "IP",
              "repeated_siblings: all children named IP");
        check(r.root.children[0].text == "172.26.251.11", "repeated_siblings: first IP text");
        check(r.root.children[2].text == "192.168.56.1", "repeated_siblings: last IP text");
    }
}

void test_empty_element_equivalence() {
    std::string self_closing_xml = "<Lock/>";
    std::string open_close_xml = "<Lock></Lock>";
    XmlParseResult self_closing = parse_xml(self_closing_xml.c_str(), self_closing_xml.size());
    XmlParseResult open_close = parse_xml(open_close_xml.c_str(), open_close_xml.size());
    check(self_closing.ok, "empty_element_equivalence: self-closing expected ok");
    check(open_close.ok, "empty_element_equivalence: open/close expected ok");
    check(self_closing.root.attributes.empty() && self_closing.root.children.empty() &&
              self_closing.root.text.empty(),
          "empty_element_equivalence: self-closing is fully empty");
    check(open_close.root.attributes.empty() && open_close.root.children.empty() &&
              open_close.root.text.empty(),
          "empty_element_equivalence: open/close is fully empty");
}

void test_malformed_mismatched_tag() {
    std::string xml = "<Request type=\"set\" id=\"1\"><Tuner></Request>";
    XmlParseResult r = parse_xml(xml.c_str(), xml.size());
    check(!r.ok, "malformed_mismatched_tag: expected failure");
    check(!r.error.empty(), "malformed_mismatched_tag: expected non-empty error message");
    check(r.error_offset > 0, "malformed_mismatched_tag: expected a nonzero error offset");
}

}  // namespace

int main() {
    test_attribute_only_self_closing();
    test_text_only_leaf();
    test_attribute_and_text();
    test_nested_single_child();
    test_repeated_siblings();
    test_empty_element_equivalence();
    test_malformed_mismatched_tag();

    if (failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
```

- [ ] **Step 5: Create a build-artifact ignore file**

`drs-bridge/parsers/utils/xml_to_json/.gitignore`:

```
*.exe
*.o
*.obj
```

- [ ] **Step 6: Build and run, verify it fails**

Run (from repo root):
```bash
g++ -std=c++17 -Wall -Wextra -I drs-bridge/parsers/utils/xml_to_json -o drs-bridge/parsers/utils/xml_to_json/test_xml_parser.exe drs-bridge/parsers/utils/xml_to_json/xml_parser.cpp drs-bridge/parsers/utils/xml_to_json/tests/test_xml_parser.cpp
./drs-bridge/parsers/utils/xml_to_json/test_xml_parser.exe
```
Expected: multiple `FAIL: ...` lines (the stub returns `ok == false` for every input) followed by `N FAILURE(S)` and a nonzero exit code.

- [ ] **Step 7: Write the real recursive-descent parser**

Replace the stub at `drs-bridge/parsers/utils/xml_to_json/xml_parser.cpp`:

```cpp
#include "xml_parser.h"

#include <cctype>

namespace {

struct Cursor {
    const char* xml;
    size_t len;
    size_t pos = 0;

    bool eof() const { return pos >= len; }
    char peek() const { return xml[pos]; }
};

void skip_whitespace(Cursor& c) {
    while (!c.eof() && std::isspace((unsigned char)c.peek())) ++c.pos;
}

bool is_name_char(char ch) {
    return std::isalnum((unsigned char)ch) || ch == '_' || ch == '-' || ch == '.' || ch == ':';
}

// Reads a tag/attribute name starting at c.pos. Returns false (no name
// characters present) without advancing c.pos.
bool read_name(Cursor& c, std::string& out_name) {
    size_t start = c.pos;
    while (!c.eof() && is_name_char(c.peek())) ++c.pos;
    if (c.pos == start) return false;
    out_name.assign(c.xml + start, c.pos - start);
    return true;
}

bool fail(XmlParseResult& result, const Cursor& c, const std::string& message) {
    result.ok = false;
    result.error = message;
    result.error_offset = c.pos;
    return false;
}

bool parse_element(Cursor& c, XmlNode& out_node, XmlParseResult& result);

// Parses the attribute list following a tag name, up to (not including)
// the terminating '>' or "/>".
bool parse_attributes(Cursor& c, XmlNode& node, XmlParseResult& result) {
    for (;;) {
        skip_whitespace(c);
        if (c.eof()) return fail(result, c, "unexpected end of input in start tag");
        char ch = c.peek();
        if (ch == '>' || ch == '/') return true;

        std::string attr_name;
        if (!read_name(c, attr_name)) return fail(result, c, "expected attribute name");

        skip_whitespace(c);
        if (c.eof() || c.peek() != '=') return fail(result, c, "expected '=' after attribute name");
        ++c.pos;
        skip_whitespace(c);
        if (c.eof() || (c.peek() != '"' && c.peek() != '\''))
            return fail(result, c, "expected quote to start attribute value");
        char quote = c.peek();
        ++c.pos;
        size_t start = c.pos;
        while (!c.eof() && c.peek() != quote) ++c.pos;
        if (c.eof()) return fail(result, c, "unterminated attribute value");
        std::string attr_value(c.xml + start, c.pos - start);
        ++c.pos;  // closing quote

        node.attributes.emplace_back(std::move(attr_name), std::move(attr_value));
    }
}

// Parses the content between a start tag's '>' and its matching end tag:
// either a run of child elements, or plain text -- never both. See
// docs/ewtss/specs/xml-to-json-parser-design.md §4.
bool parse_content(Cursor& c, XmlNode& node, XmlParseResult& result) {
    skip_whitespace(c);
    if (c.eof()) return fail(result, c, "unexpected end of input, expected content or end tag");

    if (c.peek() == '<') {
        if (c.pos + 1 < c.len && c.xml[c.pos + 1] == '/') return true;  // empty element

        for (;;) {
            XmlNode child;
            if (!parse_element(c, child, result)) return false;
            node.children.push_back(std::move(child));

            skip_whitespace(c);
            if (c.eof()) return fail(result, c, "unexpected end of input, expected sibling or end tag");
            if (c.peek() == '<' && c.pos + 1 < c.len && c.xml[c.pos + 1] == '/') return true;
            if (c.peek() != '<') return fail(result, c, "unexpected text after child element");
        }
    }

    size_t start = c.pos;
    while (!c.eof() && c.peek() != '<') ++c.pos;
    if (c.eof()) return fail(result, c, "unexpected end of input in text content");
    node.text.assign(c.xml + start, c.pos - start);

    if (!(c.pos + 1 < c.len && c.xml[c.pos + 1] == '/'))
        return fail(result, c, "unexpected child element after text content");
    return true;
}

bool parse_end_tag(Cursor& c, const std::string& expected_name, XmlParseResult& result) {
    if (c.eof() || c.peek() != '<') return fail(result, c, "expected end tag");
    ++c.pos;
    if (c.eof() || c.peek() != '/') return fail(result, c, "expected '/' in end tag");
    ++c.pos;
    std::string name;
    if (!read_name(c, name)) return fail(result, c, "expected end tag name");
    if (name != expected_name)
        return fail(result, c, "mismatched end tag: expected </" + expected_name + ">, found </" + name + ">");
    skip_whitespace(c);
    if (c.eof() || c.peek() != '>') return fail(result, c, "expected '>' to close end tag");
    ++c.pos;
    return true;
}

bool parse_element(Cursor& c, XmlNode& out_node, XmlParseResult& result) {
    if (c.eof() || c.peek() != '<') return fail(result, c, "expected '<' to start element");
    ++c.pos;

    if (!c.eof() && c.peek() == '!') return fail(result, c, "comments/CDATA are not supported");
    if (!c.eof() && c.peek() == '?')
        return fail(result, c, "XML declarations/processing instructions are not supported");

    if (!read_name(c, out_node.name)) return fail(result, c, "expected element name");

    if (!parse_attributes(c, out_node, result)) return false;

    if (c.eof()) return fail(result, c, "unexpected end of input in start tag");
    if (c.peek() == '/') {
        ++c.pos;
        if (c.eof() || c.peek() != '>') return fail(result, c, "expected '>' after '/' in self-closing tag");
        ++c.pos;
        return true;
    }

    ++c.pos;  // consume '>'
    if (!parse_content(c, out_node, result)) return false;
    return parse_end_tag(c, out_node.name, result);
}

}  // namespace

XmlParseResult parse_xml(const char* xml, size_t len) {
    XmlParseResult result;
    result.ok = true;

    Cursor c{xml, len, 0};
    skip_whitespace(c);
    if (c.eof()) {
        fail(result, c, "empty document, expected a root element");
        return result;
    }

    if (!parse_element(c, result.root, result)) return result;

    skip_whitespace(c);
    if (!c.eof()) {
        fail(result, c, "unexpected trailing content after root element");
        return result;
    }

    return result;
}
```

- [ ] **Step 8: Build and run, verify it passes**

Run (from repo root):
```bash
g++ -std=c++17 -Wall -Wextra -I drs-bridge/parsers/utils/xml_to_json -o drs-bridge/parsers/utils/xml_to_json/test_xml_parser.exe drs-bridge/parsers/utils/xml_to_json/xml_parser.cpp drs-bridge/parsers/utils/xml_to_json/tests/test_xml_parser.cpp
./drs-bridge/parsers/utils/xml_to_json/test_xml_parser.exe
```
Expected: `ALL TESTS PASSED` and exit code 0.

- [ ] **Step 9: Commit**

```bash
git add drs-bridge/parsers/utils/xml_to_json/xml_node.h drs-bridge/parsers/utils/xml_to_json/xml_parser.h drs-bridge/parsers/utils/xml_to_json/xml_parser.cpp drs-bridge/parsers/utils/xml_to_json/.gitignore drs-bridge/parsers/utils/xml_to_json/tests/test_xml_parser.cpp
git commit -m "feat(xml_to_json): add hand-written recursive-descent XML parser"
```

---

### Task 2: JSON mirror (port of the validated design)

**Files:**
- Create: `drs-bridge/parsers/utils/xml_to_json/json_mirror.h`
- Create: `drs-bridge/parsers/utils/xml_to_json/json_mirror.cpp`
- Test: `drs-bridge/parsers/utils/xml_to_json/tests/test_json_mirror.cpp`

**Interfaces:**
- Consumes: `XmlNode` (Task 1, `xml_node.h`).
- Produces:
  - `std::string to_snake_case(const std::string& name);`
  - `std::string json_quote(const std::string& value);`
  - `struct MirrorBudget { int nodes_visited = 0; };`
  - `std::string node_to_json(const XmlNode& node, int depth, MirrorBudget& budget);`

- [ ] **Step 1: Create the header**

`drs-bridge/parsers/utils/xml_to_json/json_mirror.h`:

```cpp
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
```

- [ ] **Step 2: Create a stub implementation (compiles, produces wrong values)**

`drs-bridge/parsers/utils/xml_to_json/json_mirror.cpp`:

```cpp
#include "json_mirror.h"

std::string to_snake_case(const std::string& name) { return name; }
std::string json_quote(const std::string& value) { return value; }
std::string node_to_json(const XmlNode&, int, MirrorBudget&) { return "\"stub\""; }
```

- [ ] **Step 3: Write the failing test**

`drs-bridge/parsers/utils/xml_to_json/tests/test_json_mirror.cpp`:

```cpp
#include <cstdio>
#include <string>

#include "../json_mirror.h"
#include "../xml_node.h"

namespace {

int failures = 0;

void check(bool cond, const std::string& message) {
    if (!cond) {
        std::printf("FAIL: %s\n", message.c_str());
        ++failures;
    }
}

void test_attribute_only() {
    XmlNode node;
    node.name = "Register";
    node.attributes.emplace_back("type", "processingUnit");

    MirrorBudget budget;
    std::string json = node_to_json(node, 0, budget);
    check(json == "{\"type\":\"processingUnit\"}", "attribute_only: got " + json);
}

void test_text_only_leaf() {
    XmlNode node;
    node.name = "Hostname";
    node.text = "sukSEs-JTO";

    MirrorBudget budget;
    std::string json = node_to_json(node, 0, budget);
    check(json == "\"sukSEs-JTO\"", "text_only_leaf: got " + json);
}

void test_attribute_and_text() {
    XmlNode node;
    node.name = "Input";
    node.attributes.emplace_back("sourceType", "tuner");
    node.text = "{9bc1a79a-e07a-41d6-8d44-9e7b9ce19e5d}";

    MirrorBudget budget;
    std::string json = node_to_json(node, 0, budget);
    check(json == "{\"source_type\":\"tuner\",\"#text\":\"{9bc1a79a-e07a-41d6-8d44-9e7b9ce19e5d}\"}",
          "attribute_and_text: got " + json);
}

void test_nested_single_child() {
    XmlNode guid;
    guid.name = "GUID";
    guid.text = "{fde8a6ca-f8c8-4497-b0ee-57c807c38655}";

    XmlNode root;
    root.name = "Register";
    root.children.push_back(guid);

    MirrorBudget budget;
    std::string json = node_to_json(root, 0, budget);
    check(json == "{\"guid\":\"{fde8a6ca-f8c8-4497-b0ee-57c807c38655}\"}", "nested_single_child: got " + json);
}

void test_repeated_siblings() {
    XmlNode root;
    root.name = "ProcessingUnitStatusChange";
    const char* ips[] = {"172.26.251.11", "192.168.100.241", "192.168.56.1"};
    for (const char* ip : ips) {
        XmlNode child;
        child.name = "IP";
        child.text = ip;
        root.children.push_back(child);
    }

    MirrorBudget budget;
    std::string json = node_to_json(root, 0, budget);
    check(json == "{\"ip\":[\"172.26.251.11\",\"192.168.100.241\",\"192.168.56.1\"]}",
          "repeated_siblings: got " + json);
}

void test_empty_element() {
    XmlNode node;
    node.name = "Lock";

    MirrorBudget budget;
    std::string json = node_to_json(node, 0, budget);
    check(json == "{}", "empty_element: got " + json);
}

XmlNode make_chain(int depth) {
    XmlNode node;
    node.name = "N";
    if (depth <= 0) {
        node.text = "leaf";
        return node;
    }
    node.children.push_back(make_chain(depth - 1));
    return node;
}

void test_depth_guard() {
    XmlNode root = make_chain(40);
    MirrorBudget budget;
    std::string json = node_to_json(root, 0, budget);
    check(json.find("\"truncated\":\"true\"") != std::string::npos,
          "depth_guard: expected a truncated marker in output, got " + json);
}

void test_node_count_guard() {
    XmlNode root;
    root.name = "Root";
    for (int i = 0; i < 2100; ++i) {
        XmlNode child;
        child.name = "Item";
        child.text = "x";
        root.children.push_back(child);
    }

    MirrorBudget budget;
    std::string json = node_to_json(root, 0, budget);
    check(json.find("\"truncated\":\"true\"") != std::string::npos,
          "node_count_guard: expected a truncated marker in output");
}

}  // namespace

int main() {
    test_attribute_only();
    test_text_only_leaf();
    test_attribute_and_text();
    test_nested_single_child();
    test_repeated_siblings();
    test_empty_element();
    test_depth_guard();
    test_node_count_guard();

    if (failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
```

- [ ] **Step 4: Build and run, verify it fails**

Run (from repo root):
```bash
g++ -std=c++17 -Wall -Wextra -I drs-bridge/parsers/utils/xml_to_json -o drs-bridge/parsers/utils/xml_to_json/test_json_mirror.exe drs-bridge/parsers/utils/xml_to_json/json_mirror.cpp drs-bridge/parsers/utils/xml_to_json/tests/test_json_mirror.cpp
./drs-bridge/parsers/utils/xml_to_json/test_json_mirror.exe
```
Expected: multiple `FAIL: ...` lines (the stub returns `"stub"` for every node) followed by `N FAILURE(S)` and a nonzero exit code.

- [ ] **Step 5: Write the real implementation**

Replace the stub at `drs-bridge/parsers/utils/xml_to_json/json_mirror.cpp`:

```cpp
#include "json_mirror.h"

#include <cctype>
#include <cstdio>
#include <map>
#include <vector>

namespace {

constexpr int MAX_DEPTH = 32;
constexpr int MAX_NODES = 2000;

// Build one node's JSON object body: attribute keys + child-element keys,
// merged into ONE flat object (children grouped into arrays only when a
// tag repeats). Last write wins on a key collision (a child overwrites an
// attribute of the same snake_case name) -- see
// docs/ewtss/specs/xml-to-json-parser-design.md §5.
std::string node_object_body(const XmlNode& node, int depth, MirrorBudget& budget) {
    std::vector<std::pair<std::string, std::string>> fields;
    std::map<std::string, size_t> index;

    auto set_field = [&](const std::string& key, const std::string& raw_value) {
        auto it = index.find(key);
        if (it != index.end()) {
            fields[it->second].second = raw_value;
        } else {
            index[key] = fields.size();
            fields.emplace_back(key, raw_value);
        }
    };

    for (const auto& attr : node.attributes)
        set_field(to_snake_case(attr.first), json_quote(attr.second));

    std::vector<std::string> child_order;
    std::map<std::string, std::vector<const XmlNode*>> groups;
    for (const auto& child : node.children) {
        std::string key = to_snake_case(child.name);
        if (groups.find(key) == groups.end()) child_order.push_back(key);
        groups[key].push_back(&child);
    }

    for (const auto& key : child_order) {
        auto& siblings = groups[key];
        std::string value;
        if (siblings.size() == 1) {
            value = node_to_json(*siblings[0], depth + 1, budget);
        } else {
            value = "[";
            for (size_t i = 0; i < siblings.size(); ++i) {
                if (i) value += ',';
                value += node_to_json(*siblings[i], depth + 1, budget);
            }
            value += ']';
        }
        set_field(key, value);
    }

    if (child_order.empty() && node.text.find_first_not_of(" \t\r\n") != std::string::npos)
        set_field("#text", json_quote(node.text));

    std::string out;
    bool first = true;
    for (auto& f : fields) {
        if (!first) out += ',';
        first = false;
        out += json_quote(f.first);
        out += ':';
        out += f.second;
    }
    return out;
}

}  // namespace

std::string to_snake_case(const std::string& s) {
    std::string out;
    size_t n = s.size();
    for (size_t i = 0; i < n; ++i) {
        char c = s[i];
        bool upper = std::isupper((unsigned char)c) != 0;
        if (upper) {
            bool prev_lower_or_digit = i > 0 && (std::islower((unsigned char)s[i - 1]) ||
                                                   std::isdigit((unsigned char)s[i - 1]));
            bool prev_upper = i > 0 && std::isupper((unsigned char)s[i - 1]) != 0;
            bool next_lower = (i + 1 < n) && std::islower((unsigned char)s[i + 1]) != 0;
            if (i > 0 && (prev_lower_or_digit || (prev_upper && next_lower))) out += '_';
            out += (char)std::tolower((unsigned char)c);
        } else {
            out += c;
        }
    }
    return out;
}

std::string json_quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if ((unsigned char)c < 0x20) {
                    char u[8];
                    std::snprintf(u, sizeof(u), "\\u%04x", c);
                    out += u;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
    return out;
}

std::string node_to_json(const XmlNode& node, int depth, MirrorBudget& budget) {
    ++budget.nodes_visited;
    if (depth > MAX_DEPTH || budget.nodes_visited > MAX_NODES) {
        return "{\"truncated\":\"true\",\"node_count\":" + std::to_string(budget.nodes_visited) + "}";
    }

    bool has_attrs = !node.attributes.empty();
    bool has_element_children = !node.children.empty();

    if (!has_attrs && !has_element_children) {
        if (node.text.find_first_not_of(" \t\r\n") == std::string::npos) return "{}";
        return json_quote(node.text);
    }

    return "{" + node_object_body(node, depth, budget) + "}";
}
```

- [ ] **Step 6: Build and run, verify it passes**

Run (from repo root):
```bash
g++ -std=c++17 -Wall -Wextra -I drs-bridge/parsers/utils/xml_to_json -o drs-bridge/parsers/utils/xml_to_json/test_json_mirror.exe drs-bridge/parsers/utils/xml_to_json/json_mirror.cpp drs-bridge/parsers/utils/xml_to_json/tests/test_json_mirror.cpp
./drs-bridge/parsers/utils/xml_to_json/test_json_mirror.exe
```
Expected: `ALL TESTS PASSED` and exit code 0.

- [ ] **Step 7: Commit**

```bash
git add drs-bridge/parsers/utils/xml_to_json/json_mirror.h drs-bridge/parsers/utils/xml_to_json/json_mirror.cpp drs-bridge/parsers/utils/xml_to_json/tests/test_json_mirror.cpp
git commit -m "feat(xml_to_json): add JSON mirror ported from the validated CA120 design"
```

---

### Task 3: Entry point + cross-device integration tests

**Files:**
- Create: `drs-bridge/parsers/utils/xml_to_json/xml_to_json.h`
- Create: `drs-bridge/parsers/utils/xml_to_json/xml_to_json.cpp`
- Test: `drs-bridge/parsers/utils/xml_to_json/tests/test_xml_to_json.cpp`

**Interfaces:**
- Consumes: `parse_xml` (Task 1, `xml_parser.h`), `node_to_json`/`to_snake_case`/`json_quote`/`MirrorBudget` (Task 2, `json_mirror.h`).
- Produces: `std::string parse_xml_to_json(const char* xml, size_t len, const char* frame_kind_hint);`

**Note on test scope:** all 7 structural shapes from the design spec's §2 are already exercised in Task 1 (parser tree shape) and Task 2 (mirror JSON shape) using the same real XML text as `proto_ca120_generic_mirror.cpp`'s cases. This task's tests focus on what's new here: the envelope wrapper (including the malformed-XML error path) and proof that the module isn't CA120-specific, using real `<Request>`/`<DFSelect>` samples transcribed from `icd-ddf550.md` (DDF-1GTX's ICD states its XML control channel is identical to DDF-550's, so this same shape covers both).

- [ ] **Step 1: Create the header**

`drs-bridge/parsers/utils/xml_to_json/xml_to_json.h`:

```cpp
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
```

- [ ] **Step 2: Create a stub implementation (compiles, produces wrong output)**

`drs-bridge/parsers/utils/xml_to_json/xml_to_json.cpp`:

```cpp
#include "xml_to_json.h"

std::string parse_xml_to_json(const char*, size_t, const char*) {
    return "{\"stub\":true}";
}
```

- [ ] **Step 3: Write the failing test**

`drs-bridge/parsers/utils/xml_to_json/tests/test_xml_to_json.cpp`:

```cpp
#include <cstdio>
#include <string>

#include "../json_mirror.h"
#include "../xml_node.h"
#include "../xml_parser.h"
#include "../xml_to_json.h"

namespace {

int failures = 0;

void check(bool cond, const std::string& message) {
    if (!cond) {
        std::printf("FAIL: %s\n", message.c_str());
        ++failures;
    }
}

void test_request_get_fault_reference_list() {
    std::string xml =
        "<Request type=\"get\" id=\"8\" time=\"6000000\">"
          "<ResourceManager><FaultReferenceList></FaultReferenceList></ResourceManager>"
        "</Request>";
    std::string json = parse_xml_to_json(xml.c_str(), xml.size(), "request");
    std::string expected =
        "{\"hw\":\"ca120\",\"channel\":\"xml\",\"msg_kind\":\"request\","
        "\"body\":{\"request\":{\"type\":\"get\",\"id\":\"8\",\"time\":\"6000000\","
        "\"resource_manager\":{\"fault_reference_list\":{}}}}}";
    check(json == expected, "request_get_fault_reference_list: got " + json);
}

void test_malformed_envelope() {
    std::string xml = "<Request type=\"set\" id=\"1\"><Tuner></Request>";
    std::string json = parse_xml_to_json(xml.c_str(), xml.size(), "request");
    check(json.find("\"msg_kind\":\"malformed\"") != std::string::npos,
          "malformed_envelope: expected malformed msg_kind, got " + json);
    check(json.find("\"parse_error\":") != std::string::npos,
          "malformed_envelope: expected parse_error field, got " + json);
    check(json.find("\"parse_offset\":") != std::string::npos,
          "malformed_envelope: expected parse_offset field, got " + json);
}

void test_ddf550_trace_enable_not_ca120_specific() {
    // Real sample from icd-ddf550.md §2.4 "Example -- Enable Audio Trace".
    std::string xml =
        "<Request type=\"set\" id=\"123\">"
          "<Command name=\"TraceEnable\">"
            "<Param name=\"eTraceTag\">TRACETAG_AUDIO</Param>"
            "<Param name=\"zIP\">192.168.1.100</Param>"
            "<Param name=\"iPort\">9152</Param>"
          "</Command>"
        "</Request>";
    XmlParseResult presult = parse_xml(xml.c_str(), xml.size());
    check(presult.ok, "ddf550_trace_enable: expected ok");

    MirrorBudget budget;
    std::string body = node_to_json(presult.root, 0, budget);
    std::string expected =
        "{\"type\":\"set\",\"id\":\"123\",\"command\":{\"name\":\"TraceEnable\","
        "\"param\":[{\"name\":\"eTraceTag\",\"#text\":\"TRACETAG_AUDIO\"},"
        "{\"name\":\"zIP\",\"#text\":\"192.168.1.100\"},"
        "{\"name\":\"iPort\",\"#text\":\"9152\"}]}}";
    check(body == expected, "ddf550_trace_enable: got " + body);
}

void test_ddf550_dfselect_not_ca120_specific() {
    // Real sample from icd-ddf550.md §4 "Preclassifier Filter Command".
    // icd-ddf1gtx.md §3 states its XML control channel is identical to
    // DDF-550's, so this same shape covers both variants.
    std::string xml = "<DFSelect><EmitterClass>Hopper</EmitterClass><EmitterClass>Burst</EmitterClass></DFSelect>";
    XmlParseResult presult = parse_xml(xml.c_str(), xml.size());
    check(presult.ok, "ddf550_dfselect: expected ok");

    MirrorBudget budget;
    std::string body = node_to_json(presult.root, 0, budget);
    check(body == "{\"emitter_class\":[\"Hopper\",\"Burst\"]}", "ddf550_dfselect: got " + body);
}

}  // namespace

int main() {
    test_request_get_fault_reference_list();
    test_malformed_envelope();
    test_ddf550_trace_enable_not_ca120_specific();
    test_ddf550_dfselect_not_ca120_specific();

    if (failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
```

- [ ] **Step 4: Build and run, verify it fails**

Run (from repo root):
```bash
g++ -std=c++17 -Wall -Wextra -I drs-bridge/parsers/utils/xml_to_json -o drs-bridge/parsers/utils/xml_to_json/test_xml_to_json.exe drs-bridge/parsers/utils/xml_to_json/xml_parser.cpp drs-bridge/parsers/utils/xml_to_json/json_mirror.cpp drs-bridge/parsers/utils/xml_to_json/xml_to_json.cpp drs-bridge/parsers/utils/xml_to_json/tests/test_xml_to_json.cpp
./drs-bridge/parsers/utils/xml_to_json/test_xml_to_json.exe
```
Expected: `FAIL: ...` lines from every test (the stub returns `{"stub":true}` regardless of input) followed by `N FAILURE(S)` and a nonzero exit code.

- [ ] **Step 5: Write the real implementation**

Replace the stub at `drs-bridge/parsers/utils/xml_to_json/xml_to_json.cpp`:

```cpp
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

std::string impl_parse_xml_to_json(const char* xml, size_t len, const char* frame_kind_hint) {
    XmlParseResult presult = parse_xml(xml, len);

    if (!presult.ok) {
        std::string out = "{";
        out += "\"hw\":\"ca120\",\"channel\":\"xml\",\"msg_kind\":\"malformed\",";
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
    out += "\"hw\":\"ca120\",\"channel\":\"xml\",\"msg_kind\":" + json_quote(msg_kind) + ",";
    out += "\"body\":{" + json_quote(to_snake_case(root.name)) + ":" + body_value + "}";
    out += "}";
    return out;
}

}  // namespace

std::string parse_xml_to_json(const char* xml, size_t len, const char* frame_kind_hint) {
    try {
        return impl_parse_xml_to_json(xml, len, frame_kind_hint);
    } catch (...) {
        return "{\"hw\":\"ca120\",\"channel\":\"xml\",\"msg_kind\":\"internal_error\","
               "\"note\":\"exception caught at xml_to_json boundary\"}";
    }
}
```

- [ ] **Step 6: Build and run, verify it passes**

Run (from repo root):
```bash
g++ -std=c++17 -Wall -Wextra -I drs-bridge/parsers/utils/xml_to_json -o drs-bridge/parsers/utils/xml_to_json/test_xml_to_json.exe drs-bridge/parsers/utils/xml_to_json/xml_parser.cpp drs-bridge/parsers/utils/xml_to_json/json_mirror.cpp drs-bridge/parsers/utils/xml_to_json/xml_to_json.cpp drs-bridge/parsers/utils/xml_to_json/tests/test_xml_to_json.cpp
./drs-bridge/parsers/utils/xml_to_json/test_xml_to_json.exe
```
Expected: `ALL TESTS PASSED` and exit code 0.

- [ ] **Step 7: Commit**

```bash
git add drs-bridge/parsers/utils/xml_to_json/xml_to_json.h drs-bridge/parsers/utils/xml_to_json/xml_to_json.cpp drs-bridge/parsers/utils/xml_to_json/tests/test_xml_to_json.cpp
git commit -m "feat(xml_to_json): add entry point wiring parser+mirror into the envelope shape"
```
