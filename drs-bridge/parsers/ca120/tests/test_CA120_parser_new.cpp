// drs-bridge/parsers/ca120/tests/test_CA120_parser_new.cpp
//
// Standalone C++ main()-harness (no framework), matching the convention
// already used by drs-bridge/parsers/utils/xml_to_json/tests/.
//
// Testing strategy for parse_message: rather than hand-deriving expected
// JSON literals (error-prone for deeply nested structures), most content
// tests compare parse_message()'s output against a direct call to
// xml_to_json's own parse_xml_to_json() on the same bytes -- since
// parse_message is just a thin wrapper (detect root tag -> pick hint ->
// delegate), this validates the wiring specifically, while trusting
// xml_to_json's own already-proven correctness (see its own test suite).
// extract_frame has no such oracle (its framing logic is new/unique to this
// file), so those tests use direct length/content assertions instead.

#include "../../dp_ecm/include/sdfc_abi.h"
#include "../../utils/xml_to_json/xml_to_json.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int g_failures = 0;

void check(bool cond, const std::string& message) {
    if (!cond) {
        std::printf("FAIL: %s\n", message.c_str());
        ++g_failures;
    }
}

const uint8_t* u8(const std::string& s) {
    return reinterpret_cast<const uint8_t*>(s.data());
}

// ---------------------------------------------------------------------------
// extract_frame
// ---------------------------------------------------------------------------

void test_extract_frame_request_frame_boundary() {
    std::string frame =
        "<Request type=\"get\" id=\"8\" time=\"6000000\">"
          "<ResourceManager><FaultReferenceList></FaultReferenceList></ResourceManager>"
        "</Request>";
    std::string buf = frame + "<Reply type=\"set\" id=\"9\"/>";  // trailing garbage after the frame

    uint8_t* out_frame = nullptr;
    size_t out_len = 0;
    int rc = extract_frame(u8(buf), buf.size(), &out_frame, &out_len);

    check(rc == 0, "extract_frame_request_frame_boundary: expected success");
    check(out_len == frame.size(), "extract_frame_request_frame_boundary: expected out_len to match only the first frame, not the trailing bytes");
    if (out_frame && out_len == frame.size()) {
        check(memcmp(out_frame, frame.data(), frame.size()) == 0,
              "extract_frame_request_frame_boundary: extracted bytes should match the Request frame exactly");
    }
    free_result(out_frame);
}

void test_extract_frame_incomplete_request() {
    std::string buf = "<Request type=\"get\" id=\"8\"><ResourceManager>";  // no closing </Request> yet
    uint8_t* out_frame = nullptr;
    size_t out_len = 0;
    int rc = extract_frame(u8(buf), buf.size(), &out_frame, &out_len);
    check(rc == -1, "extract_frame_incomplete_request: expected -1 (closing tag not yet received)");
}

void test_extract_frame_non_xml() {
    std::string buf = "garbage-not-xml";
    uint8_t* out_frame = nullptr;
    size_t out_len = 0;
    int rc = extract_frame(u8(buf), buf.size(), &out_frame, &out_len);
    check(rc == -1, "extract_frame_non_xml: expected -1 for non-XML input");
}

void test_extract_frame_unrecognized_root() {
    std::string buf = "<Foo></Foo>";
    uint8_t* out_frame = nullptr;
    size_t out_len = 0;
    int rc = extract_frame(u8(buf), buf.size(), &out_frame, &out_len);
    check(rc == -1, "extract_frame_unrecognized_root: expected -1 for a root tag that isn't Request/Reply/Event");
}

void test_extract_frame_skips_leading_whitespace() {
    std::string frame = "<Reply type=\"set\" id=\"3\"><ResourceManager><Register type=\"processingUnit\"/></ResourceManager></Reply>";
    std::string buf = "\r\n  " + frame;

    uint8_t* out_frame = nullptr;
    size_t out_len = 0;
    int rc = extract_frame(u8(buf), buf.size(), &out_frame, &out_len);

    check(rc == 0, "extract_frame_skips_leading_whitespace: expected success");
    check(out_len == frame.size(), "extract_frame_skips_leading_whitespace: out_len should exclude the leading whitespace");
    if (out_frame && out_len == frame.size()) {
        check(memcmp(out_frame, frame.data(), frame.size()) == 0,
              "extract_frame_skips_leading_whitespace: extracted bytes should start at '<', not include the whitespace");
    }
    free_result(out_frame);
}

// ---------------------------------------------------------------------------
// parse_message -- oracle-based content checks (see file header)
// ---------------------------------------------------------------------------

void test_parse_message_request_matches_xml_to_json() {
    std::string frame =
        "<Request type=\"get\" id=\"8\" time=\"6000000\">"
          "<ResourceManager><FaultReferenceList></FaultReferenceList></ResourceManager>"
        "</Request>";

    char* out_json = nullptr;
    size_t out_len = 0;
    int rc = parse_message(u8(frame), frame.size(), &out_json, &out_len);
    check(rc == 0, "parse_message_request_matches_xml_to_json: expected success");

    std::string expected = parse_xml_to_json(frame.c_str(), frame.size(), "request", "ca120");
    if (out_json) {
        check(std::string(out_json) == expected,
              "parse_message_request_matches_xml_to_json: parse_message output should match a direct parse_xml_to_json call with hint=\"request\", hw=\"ca120\"");
    }
    free_result(out_json);
}

void test_parse_message_reply_matches_xml_to_json() {
    std::string frame =
        "<Reply type=\"set\" id=\"3\">"
          "<ResourceManager><Register type=\"processingUnit\"/></ResourceManager>"
        "</Reply>";

    char* out_json = nullptr;
    size_t out_len = 0;
    int rc = parse_message(u8(frame), frame.size(), &out_json, &out_len);
    check(rc == 0, "parse_message_reply_matches_xml_to_json: expected success");

    std::string expected = parse_xml_to_json(frame.c_str(), frame.size(), "reply", "ca120");
    if (out_json) {
        check(std::string(out_json) == expected,
              "parse_message_reply_matches_xml_to_json: parse_message output should match a direct parse_xml_to_json call with hint=\"reply\", hw=\"ca120\"");
    }
    free_result(out_json);
}

void test_parse_message_event_kind_independent_of_hint() {
    std::string frame =
        "<Event origin=\"{afc6ed3c-bf80-4a37-8ec7-22fa754b3c38}\">"
          "<Control><Input sourceType=\"tuner\">{9bc1a79a-e07a-41d6-8d44-9e7b9ce19e5d}</Input></Control>"
        "</Event>";

    char* out_json = nullptr;
    size_t out_len = 0;
    int rc = parse_message(u8(frame), frame.size(), &out_json, &out_len);
    check(rc == 0, "parse_message_event_kind_independent_of_hint: expected success");

    std::string with_request_hint = parse_xml_to_json(frame.c_str(), frame.size(), "request", "ca120");
    std::string with_reply_hint   = parse_xml_to_json(frame.c_str(), frame.size(), "reply",   "ca120");

    check(with_request_hint == with_reply_hint,
          "parse_message_event_kind_independent_of_hint: an <Event> root must produce identical output regardless of frame_kind_hint");
    check(with_request_hint.find("\"msg_kind\":\"event\"") != std::string::npos,
          "parse_message_event_kind_independent_of_hint: expected msg_kind \"event\"");
    if (out_json) {
        check(std::string(out_json) == with_request_hint,
              "parse_message_event_kind_independent_of_hint: parse_message output should match the direct xml_to_json call");
    }
    free_result(out_json);
}

void test_parse_message_unrecognized_root_returns_error() {
    std::string frame = "<Foo></Foo>";
    char* out_json = nullptr;
    size_t out_len = 0;
    int rc = parse_message(u8(frame), frame.size(), &out_json, &out_len);
    check(rc == -1, "parse_message_unrecognized_root_returns_error: expected -1 -- this isn't CA120 XML at all, not just malformed content");
}

void test_parse_message_malformed_inside_known_root_still_succeeds() {
    // Extract_frame's framing is a plain string search for the outer
    // "</Request>" -- it will find one here even though the inner <Tuner>
    // is never closed, matching the real ca120_parser.cpp's framing
    // behavior (framing != well-formedness validation).
    std::string frame = "<Request type=\"set\" id=\"1\"><Tuner></Request>";

    char* out_json = nullptr;
    size_t out_len = 0;
    int rc = parse_message(u8(frame), frame.size(), &out_json, &out_len);

    check(rc == 0, "parse_message_malformed_inside_known_root_still_succeeds: expected success (0), matching the design principle \"malformed XML -> emit an error JSON instead of failing the call\"");
    if (out_json) {
        std::string json(out_json);
        check(json.find("\"msg_kind\":\"malformed\"") != std::string::npos,
              "parse_message_malformed_inside_known_root_still_succeeds: expected msg_kind \"malformed\"");
        check(json.find("\"parse_error\":") != std::string::npos,
              "parse_message_malformed_inside_known_root_still_succeeds: expected a parse_error field");
        check(json.find("\"parse_offset\":") != std::string::npos,
              "parse_message_malformed_inside_known_root_still_succeeds: expected a parse_offset field");
    }
    free_result(out_json);
}

// ---------------------------------------------------------------------------
// format_response (stub) / free_result
// ---------------------------------------------------------------------------

void test_format_response_stub_not_implemented() {
    uint8_t* out_buf = nullptr;
    size_t out_len = 0;
    int rc = format_response("set", "{}", &out_buf, &out_len);
    check(rc == -1, "format_response_stub_not_implemented: expected -1 (encode direction not yet designed)");
}

void test_free_result_handles_null() {
    free_result(nullptr);  // must not crash -- if this test reaches the next line, it passed
    check(true, "free_result_handles_null: free_result(nullptr) did not crash");
}

}  // namespace

int main() {
    test_extract_frame_request_frame_boundary();
    test_extract_frame_incomplete_request();
    test_extract_frame_non_xml();
    test_extract_frame_unrecognized_root();
    test_extract_frame_skips_leading_whitespace();

    test_parse_message_request_matches_xml_to_json();
    test_parse_message_reply_matches_xml_to_json();
    test_parse_message_event_kind_independent_of_hint();
    test_parse_message_unrecognized_root_returns_error();
    test_parse_message_malformed_inside_known_root_still_succeeds();

    test_format_response_stub_not_implemented();
    test_free_result_handles_null();

    if (g_failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("%d FAILURE(S)\n", g_failures);
    return 1;
}
