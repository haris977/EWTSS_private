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

constexpr int MAX_DEPTH = 32;

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

bool parse_element(Cursor& c, XmlNode& out_node, XmlParseResult& result, int depth);

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
bool parse_content(Cursor& c, XmlNode& node, XmlParseResult& result, int depth) {
    size_t content_start = c.pos;

    size_t peek_pos = c.pos;
    while (peek_pos < c.len && std::isspace((unsigned char)c.xml[peek_pos])) ++peek_pos;
    if (peek_pos >= c.len) return fail(result, c, "unexpected end of input, expected content or end tag");

    if (c.xml[peek_pos] == '<') {
        c.pos = peek_pos;  // safe to discard: pure formatting whitespace before a tag

        if (c.pos + 1 < c.len && c.xml[c.pos + 1] == '/') return true;  // empty element

        for (;;) {
            XmlNode child;
            if (!parse_element(c, child, result, depth + 1)) return false;
            node.children.push_back(std::move(child));

            skip_whitespace(c);
            if (c.eof()) return fail(result, c, "unexpected end of input, expected sibling or end tag");
            if (c.peek() == '<' && c.pos + 1 < c.len && c.xml[c.pos + 1] == '/') return true;
            if (c.peek() != '<') return fail(result, c, "unexpected text after child element");
        }
    }

    // Real text content -- capture verbatim from the original position,
    // including any leading whitespace (it's part of the text, not formatting).
    c.pos = content_start;
    while (!c.eof() && c.peek() != '<') ++c.pos;
    if (c.eof()) return fail(result, c, "unexpected end of input in text content");
    node.text.assign(c.xml + content_start, c.pos - content_start);

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

bool parse_element(Cursor& c, XmlNode& out_node, XmlParseResult& result, int depth) {
    if (depth > MAX_DEPTH) return fail(result, c, "maximum nesting depth exceeded");

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
    if (!parse_content(c, out_node, result, depth)) return false;
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

    if (!parse_element(c, result.root, result, 0)) return result;

    skip_whitespace(c);
    if (!c.eof()) {
        fail(result, c, "unexpected trailing content after root element");
        return result;
    }

    return result;
}
