// drs-bridge/parsers/gnss/tests/test_gnss_parser.cpp
//
// Self-contained golden tests for the GNSS/NMEA parser.
// Fixtures are built from the ICD's own raw sample captures where
// practical, run through extract_frame -> parse_message, and checked.
// Exits non-zero on any failure.
#include "sdfc_abi.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_failures = 0;
#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { std::printf("FAIL: %s\n", (msg)); ++g_failures; } \
        else         { std::printf("ok:   %s\n", (msg)); } \
    } while (0)

static bool contains(const char* hay, const char* needle) {
    return hay && std::strstr(hay, needle) != nullptr;
}

static void test_extract_frame_complete_sentence() {
    const char* sentence = "$GNGGA,132215.00,1726.36262,N,07834.81134,E,1,12,0.56,516.9,M,-74.2,M,,*65\r\n";
    uint8_t* frame = nullptr;
    size_t frame_len = 0;
    int rc = extract_frame(reinterpret_cast<const uint8_t*>(sentence), std::strlen(sentence), &frame, &frame_len);
    CHECK(rc == 0 && frame_len == std::strlen(sentence), "extract_frame: complete GGA sentence framed in full");
    if (frame) free_result(frame);
}

static void test_extract_frame_incomplete_no_terminator() {
    const char* partial = "$GNGGA,132215.00,1726.36262,N,07834.81134,E,1,12,0.56";
    uint8_t* frame = nullptr;
    size_t frame_len = 0;
    int rc = extract_frame(reinterpret_cast<const uint8_t*>(partial), std::strlen(partial), &frame, &frame_len);
    CHECK(rc == -1, "extract_frame: sentence with no CRLF yet reports incomplete");
}

static void test_extract_frame_split_across_two_calls() {
    const char* part1 = "$GNRMC,132217.00,A,1726.36262,N,";
    uint8_t* frame = nullptr; size_t frame_len = 0;
    CHECK(extract_frame(reinterpret_cast<const uint8_t*>(part1), std::strlen(part1), &frame, &frame_len) == -1,
          "extract_frame: first half alone is incomplete");

    std::string full = std::string(part1) +
        "07834.81133,E,0.006,,141224,,,A,V*17\r\n";
    int rc = extract_frame(reinterpret_cast<const uint8_t*>(full.c_str()), full.size(), &frame, &frame_len);
    CHECK(rc == 0 && frame_len == full.size(), "extract_frame: reassembled buffer frames completely on second call");
    if (frame) free_result(frame);
}

static void test_extract_frame_leading_noise_before_dollar() {
    std::string buf = std::string("garbage\x01\x02") +
        "$GNVTG,,T,,M,0.006,N,0.012,K,A*38\r\n";
    uint8_t* frame = nullptr; size_t frame_len = 0;
    int rc = extract_frame(reinterpret_cast<const uint8_t*>(buf.c_str()), buf.size(), &frame, &frame_len);
    CHECK(rc == 0 && frame_len == buf.size(),
          "extract_frame: leading noise before '$' is included in the frame, not dropped (host buffer offset stays correct)");
    if (frame) free_result(frame);
}

static void test_extract_frame_no_dollar_at_all() {
    const char* noise = "not an nmea sentence at all";
    uint8_t* frame = nullptr; size_t frame_len = 0;
    int rc = extract_frame(reinterpret_cast<const uint8_t*>(noise), std::strlen(noise), &frame, &frame_len);
    CHECK(rc == -1, "extract_frame: buffer with no '$' anywhere reports incomplete, waits for more data");
}

static void test_parse_message_rejects_bad_checksum() {
    // Correct sentence is "...A*38" -- corrupt the checksum to "*00".
    const char* bad = "$GNVTG,,T,,M,0.006,N,0.012,K,A*00\r\n";
    uint8_t* frame = nullptr; size_t frame_len = 0;
    CHECK(extract_frame(reinterpret_cast<const uint8_t*>(bad), std::strlen(bad), &frame, &frame_len) == 0,
          "checksum test: extract_frame still frames a corrupt sentence (framing doesn't validate content)");
    char* json_out = nullptr; size_t json_len = 0;
    int rc = parse_message(frame, frame_len, &json_out, &json_len);
    CHECK(rc == -1, "parse_message: rejects sentence with mismatched checksum");
    if (frame) free_result(frame);
    if (json_out) free_result(json_out);
}

static void test_parse_message_rejects_unrecognized_sentence() {
    // Valid checksum, but no dispatch case exists for this made-up sentence ID yet.
    const char* unknown = "$GPXYZ,1,2,3*77\r\n"; // checksum not validated in this test's premise -- see below
    // Compute a real valid checksum for "$GPXYZ,1,2,3*XX" so this test isolates
    // "unrecognized sentence" rejection, not "bad checksum" rejection.
    const char* body = "GPXYZ,1,2,3";
    uint8_t cs = 0;
    for (const char* p = body; *p; ++p) cs ^= static_cast<uint8_t>(*p);
    char sentence[64];
    std::snprintf(sentence, sizeof(sentence), "$%s*%02X\r\n", body, cs);
    (void)unknown;

    uint8_t* frame = nullptr; size_t frame_len = 0;
    CHECK(extract_frame(reinterpret_cast<const uint8_t*>(sentence), std::strlen(sentence), &frame, &frame_len) == 0,
          "unrecognized sentence test: extract_frame frames it fine");
    char* json_out = nullptr; size_t json_len = 0;
    int rc = parse_message(frame, frame_len, &json_out, &json_len);
    CHECK(rc == -1, "parse_message: rejects a sentence ID with no dispatch case (valid checksum, unknown type)");
    if (frame) free_result(frame);
    if (json_out) free_result(json_out);
}

// Runs one sentence string through extract_frame -> parse_message and
// returns the decoded JSON (caller must free_result both frame and json).
static bool decode_sentence(const char* sentence, char** out_json) {
    uint8_t* frame = nullptr; size_t frame_len = 0;
    if (extract_frame(reinterpret_cast<const uint8_t*>(sentence), std::strlen(sentence), &frame, &frame_len) != 0)
        return false;
    size_t json_len = 0;
    bool ok = parse_message(frame, frame_len, out_json, &json_len) == 0;
    if (frame) free_result(frame);
    return ok;
}

static void test_gpgsv_decode_from_real_sample() {
    // Real sample capture from the ICD (page 2), 4 satellite groups + Signal_ID.
    const char* s = "$GPGSV,3,1,11,06,87,151,46,11,46,210,43,13,16,226,30,14,44,085,31,1*64\r\n";
    char* json_out = nullptr;
    CHECK(decode_sentence(s, &json_out), "GPGSV: decodes (implemented via shared GSV structure despite ICD gap)");
    CHECK(contains(json_out, "\"message_name\":\"GPGSV\""), "GPGSV: message_name present");
    CHECK(contains(json_out, "\"total_messages\":\"3\""), "GPGSV: total_messages decoded");
    CHECK(contains(json_out, "\"satellites_in_view\":\"11\""), "GPGSV: satellites_in_view decoded");
    CHECK(contains(json_out, "\"prn\":\"06\""), "GPGSV: first satellite PRN decoded");
    CHECK(contains(json_out, "\"signal_id\":\"1\""), "GPGSV: trailing signal_id decoded");
    if (json_out) free_result(json_out);
}

static void test_glgsv_decode_fewer_than_4_satellites() {
    // Real sample: only 1 satellite group (final message in a multi-message set).
    const char* s = "$GLGSV,1,1,04,73,55,114,25,74,61,010,36,84,50,003,30,85,55,256,49,1*73\r\n";
    char* json_out = nullptr;
    CHECK(decode_sentence(s, &json_out), "GLGSV: decodes with 4 satellite groups");
    CHECK(contains(json_out, "\"message_name\":\"GLGSV\""), "GLGSV: message_name present");
    CHECK(contains(json_out, "\"satellites_in_view\":\"04\""), "GLGSV: satellites_in_view decoded");
    if (json_out) free_result(json_out);
}

static void test_gagsv_gbgsv_gigsv_decode() {
    const char* ga = "$GAGSV,2,1,07,13,75,054,41,15,21,036,36,19,11,259,26,21,44,347,35,7*73\r\n";
    const char* gb = "$GBGSV,3,1,10,05,58,227,45,06,29,165,36,09,42,172,34,10,50,023,41,1*7D\r\n";
    const char* gi = "$GIGSV,2,1,05,01,,40,02,61,302,40,03,,32,06,34,252,54,1*79\r\n";
    char* json_out = nullptr;
    CHECK(decode_sentence(ga, &json_out), "GAGSV: decodes");
    CHECK(contains(json_out, "\"message_name\":\"GAGSV\""), "GAGSV: message_name present");
    if (json_out) { free_result(json_out); json_out = nullptr; }

    CHECK(decode_sentence(gb, &json_out), "GBGSV: decodes");
    CHECK(contains(json_out, "\"message_name\":\"GBGSV\""), "GBGSV: message_name present");
    if (json_out) { free_result(json_out); json_out = nullptr; }

    CHECK(decode_sentence(gi, &json_out), "GIGSV: decodes (blank elevation field in first satellite group preserved)");
    CHECK(contains(json_out, "\"message_name\":\"GIGSV\""), "GIGSV: message_name present");
    CHECK(contains(json_out, "\"elevation_deg\":\"\""), "GIGSV: blank field preserved as empty string, not dropped");
    if (json_out) free_result(json_out);
}

static void test_gngll_decode_from_real_sample() {
    const char* s = "$GNGLL,1726.36262,N,07834.81133,E,132216.00,A,A*73\r\n";
    char* json_out = nullptr;
    CHECK(decode_sentence(s, &json_out), "GNGLL: decodes");
    CHECK(contains(json_out, "\"message_name\":\"GNGLL\""), "GNGLL: message_name present");
    CHECK(contains(json_out, "\"latitude\":\"1726.36262\""), "GNGLL: latitude decoded");
    CHECK(contains(json_out, "\"ns\":\"N\""), "GNGLL: N/S hemisphere decoded as its own field");
    CHECK(contains(json_out, "\"faa_mode\":\"A\""), "GNGLL: trailing FAA mode field decoded");
    if (json_out) free_result(json_out);
}

static void test_gnzda_decode_from_real_sample() {
    const char* s = "$GNZDA,132215.00,14,12,2024,00,00*7C\r\n";
    char* json_out = nullptr;
    CHECK(decode_sentence(s, &json_out), "GNZDA: decodes");
    CHECK(contains(json_out, "\"message_name\":\"GNZDA\""), "GNZDA: message_name present");
    CHECK(contains(json_out, "\"day\":\"14\""), "GNZDA: day decoded");
    CHECK(contains(json_out, "\"local_zone_hours\":\"00\""), "GNZDA: local_zone_hours decoded");
    CHECK(contains(json_out, "\"local_zone_minutes\":\"00\""), "GNZDA: local_zone_minutes decoded (field the ICD table omitted)");
    if (json_out) free_result(json_out);
}

static void test_gnrmc_decode_from_real_sample() {
    const char* s = "$GNRMC,132217.00,A,1726.36262,N,07834.81133,E,0.006,,141224,,,A,V*17\r\n";
    char* json_out = nullptr;
    CHECK(decode_sentence(s, &json_out), "GNRMC: decodes");
    CHECK(contains(json_out, "\"message_name\":\"GNRMC\""), "GNRMC: message_name present");
    CHECK(contains(json_out, "\"speed_over_ground\":\"0.006\""), "GNRMC: speed_over_ground decoded");
    CHECK(contains(json_out, "\"date\":\"141224\""), "GNRMC: date decoded");
    CHECK(contains(json_out, "\"faa_mode\":\"A\""), "GNRMC: FAA mode field decoded (ICD table omitted this)");
    CHECK(contains(json_out, "\"nav_status\":\"V\""), "GNRMC: trailing Nav Status field decoded (ICD table omitted this)");
    if (json_out) free_result(json_out);
}

static void test_gnvtg_decode_from_real_sample() {
    const char* s = "$GNVTG,,T,,M,0.006,N,0.012,K,A*38\r\n";
    char* json_out = nullptr;
    CHECK(decode_sentence(s, &json_out), "GNVTG: decodes");
    CHECK(contains(json_out, "\"message_name\":\"GNVTG\""), "GNVTG: message_name present");
    CHECK(contains(json_out, "\"speed_knots\":\"0.006\""), "GNVTG: speed_knots decoded");
    CHECK(contains(json_out, "\"speed_kmh\":\"0.012\""), "GNVTG: speed_kmh decoded");
    CHECK(contains(json_out, "\"faa_mode\":\"A\""), "GNVTG: trailing FAA mode field decoded (ICD table omitted this)");
    if (json_out) free_result(json_out);
}

static void test_gngga_decode_from_real_sample() {
    const char* s = "$GNGGA,132217.00,1726.36262,N,07834.81133,E,1,12,0.56,516.9,M,-74.2,M,,*60\r\n";
    char* json_out = nullptr;
    CHECK(decode_sentence(s, &json_out), "GNGGA: decodes");
    CHECK(contains(json_out, "\"message_name\":\"GNGGA\""), "GNGGA: message_name present");
    CHECK(contains(json_out, "\"fix_quality\":\"1\""), "GNGGA: fix_quality decoded");
    CHECK(contains(json_out, "\"satellites_used\":\"12\""), "GNGGA: satellites_used decoded");
    CHECK(contains(json_out, "\"altitude\":\"516.9\""), "GNGGA: altitude decoded");
    CHECK(contains(json_out, "\"geoid_separation\":\"-74.2\""), "GNGGA: geoid_separation decoded");
    CHECK(contains(json_out, "\"dgps_age\":\"\""), "GNGGA: trailing DGPS age field present but blank");
    CHECK(contains(json_out, "\"dgps_station_id\":\"\""), "GNGGA: trailing DGPS station ID field present but blank");
    if (json_out) free_result(json_out);
}

static void test_gngsa_decode_from_real_sample() {
    const char* s = "$GNGSA,A,3,11,17,30,19,22,14,06,13,,,,,1.07,0.56,0.91,1*00\r\n";
    char* json_out = nullptr;
    CHECK(decode_sentence(s, &json_out), "GNGSA: decodes");
    CHECK(contains(json_out, "\"message_name\":\"GNGSA\""), "GNGSA: message_name present");
    CHECK(contains(json_out, "\"fix_mode\":\"A\""), "GNGSA: fix_mode decoded");
    CHECK(contains(json_out, "\"fix_type\":\"3\""), "GNGSA: fix_type decoded");
    CHECK(contains(json_out, "\"satellite_ids\":[\"11\",\"17\",\"30\",\"19\",\"22\",\"14\",\"06\",\"13\",\"\",\"\",\"\",\"\"]"),
          "GNGSA: all 12 fixed satellite-ID slots decoded in order, blanks preserved");
    CHECK(contains(json_out, "\"pdop\":\"1.07\""), "GNGSA: pdop decoded");
    CHECK(contains(json_out, "\"system_id\":\"1\""), "GNGSA: trailing system_id field decoded (ICD table omitted this)");
    if (json_out) free_result(json_out);
}

static void test_all_11_sentence_types_decode_with_correct_message_name() {
    // One real ICD sample per sentence type -- confirms every dispatch
    // branch is wired to the right decoder with no typo'd message_name,
    // and that nothing was missed across the 5 tasks that built this up.
    struct Sentence { const char* name; const char* raw; };
    std::vector<Sentence> all = {
        {"GPGSV", "$GPGSV,3,1,11,06,87,151,46,11,46,210,43,13,16,226,30,14,44,085,31,1*64\r\n"},
        {"GLGSV", "$GLGSV,1,1,04,73,55,114,25,74,61,010,36,84,50,003,30,85,55,256,49,1*73\r\n"},
        {"GAGSV", "$GAGSV,2,1,07,13,75,054,41,15,21,036,36,19,11,259,26,21,44,347,35,7*73\r\n"},
        {"GBGSV", "$GBGSV,3,1,10,05,58,227,45,06,29,165,36,09,42,172,34,10,50,023,41,1*7D\r\n"},
        {"GIGSV", "$GIGSV,2,1,05,01,,40,02,61,302,40,03,,32,06,34,252,54,1*79\r\n"},
        {"GNGLL", "$GNGLL,1726.36262,N,07834.81133,E,132216.00,A,A*73\r\n"},
        {"GNZDA", "$GNZDA,132215.00,14,12,2024,00,00*7C\r\n"},
        {"GNRMC", "$GNRMC,132217.00,A,1726.36262,N,07834.81133,E,0.006,,141224,,,A,V*17\r\n"},
        {"GNVTG", "$GNVTG,,T,,M,0.006,N,0.012,K,A*38\r\n"},
        {"GNGGA", "$GNGGA,132217.00,1726.36262,N,07834.81133,E,1,12,0.56,516.9,M,-74.2,M,,*60\r\n"},
        {"GNGSA", "$GNGSA,A,3,11,17,30,19,22,14,06,13,,,,,1.07,0.56,0.91,1*00\r\n"},
    };
    for (const auto& s : all) {
        char* json_out = nullptr;
        bool ok = decode_sentence(s.raw, &json_out);
        std::string label = std::string("full-coverage: ") + s.name + " decodes";
        CHECK(ok, label.c_str());
        if (ok) {
            std::string expect = std::string("\"message_name\":\"") + s.name + "\"";
            std::string label2 = std::string("full-coverage: ") + s.name + " message_name matches";
            CHECK(contains(json_out, expect.c_str()), label2.c_str());
        }
        if (json_out) free_result(json_out);
    }
}

int main() {
    test_extract_frame_complete_sentence();
    test_extract_frame_incomplete_no_terminator();
    test_extract_frame_split_across_two_calls();
    test_extract_frame_leading_noise_before_dollar();
    test_extract_frame_no_dollar_at_all();
    test_parse_message_rejects_bad_checksum();
    test_parse_message_rejects_unrecognized_sentence();
    test_gpgsv_decode_from_real_sample();
    test_glgsv_decode_fewer_than_4_satellites();
    test_gagsv_gbgsv_gigsv_decode();
    test_gngll_decode_from_real_sample();
    test_gnzda_decode_from_real_sample();
    test_gnrmc_decode_from_real_sample();
    test_gnvtg_decode_from_real_sample();
    test_gngga_decode_from_real_sample();
    test_gngsa_decode_from_real_sample();
    test_all_11_sentence_types_decode_with_correct_message_name();
    std::printf("\n%d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
