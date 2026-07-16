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
    std::string json = parse_xml_to_json(xml.c_str(), xml.size(), "request", "ca120");
    std::string expected =
        "{\"hw\":\"ca120\",\"channel\":\"xml\",\"msg_kind\":\"request\","
        "\"body\":{\"request\":{\"type\":\"get\",\"id\":\"8\",\"time\":\"6000000\","
        "\"resource_manager\":{\"fault_reference_list\":{}}}}}";
    check(json == expected, "request_get_fault_reference_list: got " + json);
}

void test_malformed_envelope() {
    std::string xml = "<Request type=\"set\" id=\"1\"><Tuner></Request>";
    std::string json = parse_xml_to_json(xml.c_str(), xml.size(), "request", "ca120");
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

void test_hw_is_caller_supplied_not_hardcoded() {
    // Same DDF-550 sample as test_ddf550_dfselect_not_ca120_specific, but
    // this time through the full public envelope entry point with a
    // non-ca120 hw value, proving the envelope itself is device-agnostic
    // now (not just the underlying parser+mirror).
    //
    // Note: to_snake_case("DFSelect") -> "df_select" (verified against the
    // real algorithm in json_mirror.cpp), not "d_f_select" as an earlier
    // draft of this test assumed.
    std::string xml = "<DFSelect><EmitterClass>Hopper</EmitterClass><EmitterClass>Burst</EmitterClass></DFSelect>";
    std::string json = parse_xml_to_json(xml.c_str(), xml.size(), "request", "ddf550");
    std::string expected =
        "{\"hw\":\"ddf550\",\"channel\":\"xml\",\"msg_kind\":\"request\","
        "\"body\":{\"df_select\":{\"emitter_class\":[\"Hopper\",\"Burst\"]}}}";
    check(json == expected, "hw_is_caller_supplied_not_hardcoded: got " + json);
}

}  // namespace

int main() {
    test_request_get_fault_reference_list();
    test_malformed_envelope();
    test_ddf550_trace_enable_not_ca120_specific();
    test_ddf550_dfselect_not_ca120_specific();
    test_hw_is_caller_supplied_not_hardcoded();

    if (failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
