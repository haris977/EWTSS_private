// drs-bridge/parsers/aus/tests/test_aus_parser.cpp
//
// Self-contained golden tests for the AUS Protocol Analyser parser.
// Builds minimal AUS-C2 /system JSON strings, runs them through
// extract_frame -> parse_message, and checks the output.
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

// ============================================================
// Minimal AUS-C2 /system JSON fixtures
// ============================================================

// Empty system response — no devices, no danger
static const char* FIXTURE_EMPTY = R"({
  "has_danger": false,
  "devices_info": {},
  "remote_id_devices_info": {},
  "sensors_info": {}
})";

// One drone detected via DF bearing, with paired RC
static const char* FIXTURE_ONE_DRONE = R"({
  "has_danger": true,
  "devices_info": {
    "0000ac": {
      "id": "0000ac",
      "type": "drone",
      "model": "DJI Mavic 3",
      "detection_engine": "AI",
      "status": "detecting",
      "frequency": "2.4GHz",
      "bw_khz": 1920,
      "modulation": "FHSS",
      "frequency_hop": true,
      "signals": [-51, -53, -55],
      "detect_counter": 28,
      "finder": "df",
      "gps": {"lat": 40.513, "lng": -113.495},
      "drone_height": 15.0,
      "lf_error_radius": null,
      "ld_result": {
        "SF1311011234": {"azimuth": 15.3, "distance": null}
      },
      "home_gps": null,
      "threat": {"level": 80},
      "defense_frequencies": ["2.4GHz", "5.8GHz"],
      "is_near_probability": 0.9,
      "paired_rc": {
        "id": "0000ac_rc",
        "finder": "df",
        "gps": {"lat": 40.510, "lng": -113.490},
        "ld_result": {
          "SF1311011234": {"azimuth": 18.0, "distance": null}
        }
      }
    }
  },
  "remote_id_devices_info": {},
  "sensors_info": {
    "SF1311011234": {
      "status": "detecting",
      "gps": {"lat": 40.500, "lng": -113.480},
      "compass": 199.0,
      "is_df_enabled": true
    }
  }
})";

// Remote-ID detection (no LD, GPS-based position from drone itself)
static const char* FIXTURE_REMOTE_ID = R"({
  "has_danger": true,
  "devices_info": {},
  "remote_id_devices_info": {
    "rid001": {
      "id": "rid001",
      "type": "drone",
      "model": "Autel EVO2",
      "detection_engine": "Library",
      "status": "detecting",
      "frequency": "2.4GHz",
      "bw_khz": 1920,
      "finder": "decoded",
      "gps": {"lat": 40.520, "lng": -113.500},
      "drone_height": 25.0,
      "signals": [-60],
      "detect_counter": 5,
      "threat": {"level": 100},
      "defense_frequencies": []
    }
  },
  "sensors_info": {}
})";

// Corrupt / non-JSON input
static const char* FIXTURE_NOT_JSON   = "this is not json";

// JSON object but missing devices_info
static const char* FIXTURE_NO_DEVICES = R"({"has_danger": false, "sensors_info": {}})";

// Buffer that looks like the start of a valid response but is truncated
static const char* FIXTURE_TRUNCATED  = R"({"has_danger": true, "devices_info": {"0000ac":)";

// ============================================================
// Helpers — maintain a single global malloc'd frame for reuse
// ============================================================

static uint8_t* g_frame     = nullptr;
static size_t   g_frame_len = 0;

// Run extract_frame on a null-terminated string (without the null byte).
// Frees any previous frame, captures the new malloc'd output globally.
static int ef(const char* s) {
    free_result(g_frame);
    g_frame     = nullptr;
    g_frame_len = 0;
    return extract_frame(
        reinterpret_cast<const uint8_t*>(s),
        std::strlen(s),
        &g_frame, &g_frame_len);
}

// ============================================================
// Tests
// ============================================================

static void test_extract_frame() {
    // 1. Empty but valid /system response
    {
        int t = ef(FIXTURE_EMPTY);
        CHECK(t == 0,              "extract: empty system response -> 0 (success)");
        CHECK(g_frame_len > 0,     "extract: empty system response -> out_len > 0");
        CHECK(g_frame != nullptr,  "extract: empty system response -> out_frame non-null");
    }

    // 2. Full response with one drone
    {
        int t = ef(FIXTURE_ONE_DRONE);
        CHECK(t == 0,              "extract: one-drone response -> 0 (success)");
        CHECK(g_frame_len > 50,    "extract: one-drone response -> non-trivial length");
    }

    // 3. Non-JSON input -> -1 (invalid)
    {
        int t = ef(FIXTURE_NOT_JSON);
        CHECK(t == -1, "extract: non-JSON input -> -1");
    }

    // 4. JSON object missing devices_info -> -1 (unrecognised, not a /system response)
    {
        int t = ef(FIXTURE_NO_DEVICES);
        CHECK(t == -1, "extract: missing devices_info -> -1 (unrecognised)");
    }

    // 5. Truncated buffer (valid start, no closing '}') -> -1
    {
        int t = ef(FIXTURE_TRUNCATED);
        CHECK(t == -1, "extract: truncated buffer -> -1 (failure)");
    }

    // 6. Leading whitespace is stripped
    {
        std::string padded = "\n\r\n  " + std::string(FIXTURE_EMPTY);
        free_result(g_frame); g_frame = nullptr; g_frame_len = 0;
        int t = extract_frame(
            reinterpret_cast<const uint8_t*>(padded.data()),
            padded.size(),
            &g_frame, &g_frame_len);
        CHECK(t == 0, "extract: leading whitespace stripped -> 0 (success)");
    }

    // 7. Null / zero-length input -> -1
    {
        uint8_t* out = nullptr; size_t out_len = 0;
        int t = extract_frame(nullptr, 0, &out, &out_len);
        CHECK(t == -1, "extract: null input -> -1");
        t = extract_frame(reinterpret_cast<const uint8_t*>(""), 0, &out, &out_len);
        CHECK(t == -1, "extract: empty input -> -1");
    }
}

static void test_parse_message_empty() {
    int t = ef(FIXTURE_EMPTY);
    assert(t == 0);

    char* result  = nullptr;
    size_t res_len = 0;
    int r = parse_message(g_frame, g_frame_len, &result, &res_len);
    CHECK(r == 0,                                   "parse: empty system -> 0 (success)");
    CHECK(result != nullptr,                        "parse: empty system -> not null");
    CHECK(contains(result, "\"frame_type\":3"),     "parse: empty system -> frame_type 3");
    CHECK(contains(result, "\"has_danger\":false"), "parse: empty system -> has_danger false");
    CHECK(contains(result, "\"detection_count\":0"), "parse: empty system -> detection_count 0");
    CHECK(contains(result, "\"sensor_count\":0"),    "parse: empty system -> sensor_count 0");
    CHECK(contains(result, "\"detections\":[]"),     "parse: empty system -> empty detections");
    CHECK(contains(result, "\"sensors\":[]"),        "parse: empty system -> empty sensors");
    free_result(result);
}

static void test_parse_message_one_drone() {
    int t = ef(FIXTURE_ONE_DRONE);
    assert(t == 0);

    char* result  = nullptr;
    size_t res_len = 0;
    int r = parse_message(g_frame, g_frame_len, &result, &res_len);
    CHECK(r == 0,                                        "parse: one-drone -> 0 (success)");
    CHECK(result != nullptr,                             "parse: one-drone -> not null");
    CHECK(contains(result, "\"has_danger\":true"),       "parse: one-drone -> has_danger true");
    CHECK(contains(result, "\"detection_count\":1"),     "parse: one-drone -> detection_count 1");
    CHECK(contains(result, "\"sensor_count\":1"),        "parse: one-drone -> sensor_count 1");

    // Detection fields
    CHECK(contains(result, "\"id\":\"0000ac\""),         "parse: one-drone -> id");
    CHECK(contains(result, "\"type\":\"drone\""),        "parse: one-drone -> type");
    CHECK(contains(result, "\"model\":\"DJI Mavic 3\""), "parse: one-drone -> model");
    CHECK(contains(result, "\"source\":\"devices\""),    "parse: one-drone -> source");
    CHECK(contains(result, "\"finder\":\"df\""),         "parse: one-drone -> finder");
    CHECK(contains(result, "\"rssi_latest_dbm\":-51"),   "parse: one-drone -> rssi");
    CHECK(contains(result, "\"frequency_hop\":true"),    "parse: one-drone -> frequency_hop");
    CHECK(contains(result, "\"threat_level\":80"),       "parse: one-drone -> threat_level");
    CHECK(contains(result, "\"defense_freq_count\":2"),  "parse: one-drone -> defense_freq_count");
    CHECK(contains(result, "\"defense_freq_0\":\"2.4GHz\""), "parse: one-drone -> defense_freq_0");
    CHECK(contains(result, "\"defense_freq_1\":\"5.8GHz\""), "parse: one-drone -> defense_freq_1");

    // Azimuth from ld_result
    CHECK(contains(result, "\"bearing_sensor_id\":\"SF1311011234\""), "parse: one-drone -> bearing_sensor_id");

    // Paired RC
    CHECK(contains(result, "\"rc_id\":\"0000ac_rc\""), "parse: one-drone -> rc_id");
    CHECK(contains(result, "\"rc_finder\":\"df\""),    "parse: one-drone -> rc_finder");

    // Sensor
    CHECK(contains(result, "\"sensor_id\":\"SF1311011234\""), "parse: one-drone -> sensor_id");
    CHECK(contains(result, "\"is_df_enabled\":true"),         "parse: one-drone -> is_df_enabled");

    free_result(result);
}

static void test_parse_message_remote_id() {
    int t = ef(FIXTURE_REMOTE_ID);
    assert(t == 0);

    char* result  = nullptr;
    size_t res_len = 0;
    int r = parse_message(g_frame, g_frame_len, &result, &res_len);
    CHECK(r == 0,                                          "parse: remote-id -> 0 (success)");
    CHECK(result != nullptr,                               "parse: remote-id -> not null");
    CHECK(contains(result, "\"detection_count\":1"),       "parse: remote-id -> detection_count 1");
    CHECK(contains(result, "\"source\":\"remote_id\""),    "parse: remote-id -> source");
    CHECK(contains(result, "\"finder\":\"decoded\""),      "parse: remote-id -> finder decoded");
    CHECK(contains(result, "\"id\":\"rid001\""),           "parse: remote-id -> id");
    CHECK(contains(result, "\"threat_level\":100"),        "parse: remote-id -> threat_level 100");
    CHECK(contains(result, "\"defense_freq_count\":0"),    "parse: remote-id -> empty defense_freqs");
    free_result(result);
}

static void test_format_response_is_noop() {
    // AUS-C2 is read-only from the bridge side — format_response always returns -1.
    uint8_t* out = nullptr;
    size_t out_len = 0;
    int n = format_response(nullptr, R"({"group_id":100,"unit_id":2,"status":0})", &out, &out_len);
    CHECK(n == -1,       "format_response returns -1 (read-only API)");
    CHECK(out == nullptr, "format_response does not allocate (read-only)");
}

static void test_free_result_null_safe() {
    free_result(nullptr);
    CHECK(true, "free_result(nullptr) does not crash");
}

// ============================================================
// main
// ============================================================

int main() {
    test_extract_frame();
    test_parse_message_empty();
    test_parse_message_one_drone();
    test_parse_message_remote_id();
    test_format_response_is_noop();
    test_free_result_null_safe();

    // Clean up the global frame if any test left it allocated
    free_result(g_frame);

    if (g_failures) {
        std::printf("\n%d test(s) FAILED.\n", g_failures);
        return 1;
    }
    std::printf("\nAll tests passed.\n");
    return 0;
}
