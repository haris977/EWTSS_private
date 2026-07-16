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

void test_text_preserved_verbatim() {
    std::string xml = "<Tag>  hello  </Tag>";
    XmlParseResult r = parse_xml(xml.c_str(), xml.size());
    check(r.ok, "text_preserved_verbatim: expected ok");
    check(r.root.text == "  hello  ", "text_preserved_verbatim: expected verbatim text with both leading and trailing spaces preserved, got \"" + r.root.text + "\"");
}

void test_depth_guard_no_crash() {
    std::string xml;
    int depth = 100;
    for (int i = 0; i < depth; ++i) xml += "<a>";
    xml += "leaf";
    for (int i = 0; i < depth; ++i) xml += "</a>";

    XmlParseResult r = parse_xml(xml.c_str(), xml.size());
    check(!r.ok, "depth_guard_no_crash: expected failure on pathologically deep nesting (not a crash)");
    check(!r.error.empty(), "depth_guard_no_crash: expected a non-empty error message");
}

void test_null_xml_does_not_crash() {
    XmlParseResult r = parse_xml(nullptr, 5);
    check(!r.ok, "null_xml_does_not_crash: expected failure, not a crash");
}

void test_interstitial_whitespace_discarded() {
    std::string xml = "<Root>\n  <Child>1</Child>\n  <Child>2</Child>\n</Root>";
    XmlParseResult r = parse_xml(xml.c_str(), xml.size());
    check(r.ok, "interstitial_whitespace_discarded: expected ok");
    check(r.root.text.empty(), "interstitial_whitespace_discarded: root text should stay empty (it has children)");
    check(r.root.children.size() == 2, "interstitial_whitespace_discarded: expected 2 children");
    if (r.root.children.size() == 2) {
        check(r.root.children[0].text == "1", "interstitial_whitespace_discarded: first child text");
        check(r.root.children[1].text == "2", "interstitial_whitespace_discarded: second child text");
    }
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
    test_text_preserved_verbatim();
    test_depth_guard_no_crash();
    test_null_xml_does_not_crash();
    test_interstitial_whitespace_discarded();

    if (failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
