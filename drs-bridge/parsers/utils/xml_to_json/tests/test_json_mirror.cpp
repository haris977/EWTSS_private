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

void test_whitespace_only_text_is_empty_object() {
    XmlNode node;
    node.name = "Lock";
    node.text = "   \n  ";

    MirrorBudget budget;
    std::string json = node_to_json(node, 0, budget);
    check(json == "{}", "whitespace_only_text_is_empty_object: got " + json);
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
    test_whitespace_only_text_is_empty_object();

    if (failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
