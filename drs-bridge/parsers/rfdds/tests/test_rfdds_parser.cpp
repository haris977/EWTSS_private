// drs-bridge/parsers/rfdds/tests/test_rfdds_parser.cpp
//
// Self-contained golden tests for the RFDDS/GJSS/RFCMS parser.
// Builds minimal request frames, runs them through
// extract_frame -> parse_message, and checks the output JSON.
// Also exercises format_response for the reply side.
// Exits non-zero on any failure.
#include "sdfc_abi.h"
#include "sdfc_endian.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace sdfc;

static int g_failures = 0;
#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { std::printf("FAIL: %s\n", (msg)); ++g_failures; } \
        else         { std::printf("ok:   %s\n", (msg)); } \
    } while (0)

static bool contains(const char* hay, const char* needle) {
    return hay && std::strstr(hay, needle) != nullptr;
}

// Local copy of rfdds_parser.cpp's write_fixed_str: that one has static
// (internal) linkage inside its own translation unit, so this test binary
// -- which links src/rfdds_parser.cpp as a separate object -- cannot call
// it directly. Used by tests to build fixed-width ASCII fields on the wire.
static void write_fixed_str(uint8_t* buf, int n, const std::string& s) {
    int copy = static_cast<int>(s.size());
    if (copy > n) copy = n;
    std::memcpy(buf, s.data(), static_cast<size_t>(copy));
    if (copy < n) std::memset(buf + copy, 0, static_cast<size_t>(n - copy));
}

static void test_extract_frame_fixed_length() {
    // Stop_Jam (105) — 1 byte, fixed, no other fields.
    uint8_t buf[1] = {105};
    uint8_t* frame = nullptr;
    size_t frame_len = 0;
    int rc = extract_frame(buf, sizeof(buf), &frame, &frame_len);
    CHECK(rc == 0 && frame_len == 1, "extract_frame: Stop_Jam is 1 byte");
    if (frame) free_result(frame);
}

static void test_extract_frame_incomplete() {
    // Connect (101) needs 9 bytes total; give it only 3.
    uint8_t buf[3] = {101, 0, 0};
    uint8_t* frame = nullptr;
    size_t frame_len = 0;
    int rc = extract_frame(buf, sizeof(buf), &frame, &frame_len);
    CHECK(rc == -1, "extract_frame: Connect with only 3/9 bytes reports incomplete");
}

static void test_extract_frame_variable_start_jam() {
    // Start_Jam (102), header 15 bytes, No_of_Band_Selected(offset 14) = 2,
    // so total = 15 + 2*24 = 63 bytes.
    std::vector<uint8_t> buf(63, 0);
    buf[0] = 102;
    buf[14] = 2; // band count
    uint8_t* frame = nullptr;
    size_t frame_len = 0;
    int rc = extract_frame(buf.data(), buf.size(), &frame, &frame_len);
    CHECK(rc == 0 && frame_len == 63, "extract_frame: Start_Jam with band_count=2 is 63 bytes");
    if (frame) free_result(frame);

    // Same buffer truncated to 62 bytes must report incomplete.
    rc = extract_frame(buf.data(), 62, &frame, &frame_len);
    CHECK(rc == -1, "extract_frame: Start_Jam truncated by 1 byte reports incomplete");
}

static void test_extract_frame_variable_gnss_spoof_zero_waypoints() {
    // GNSS_Spoof (104), fixed 57 bytes, No_of_WayPoints at offset 55 (u16be) = 0.
    std::vector<uint8_t> buf(57, 0);
    buf[0] = 104;
    buf[55] = 0; buf[56] = 0; // No_of_WayPoints = 0 (big-endian u16)
    uint8_t* frame = nullptr;
    size_t frame_len = 0;
    int rc = extract_frame(buf.data(), buf.size(), &frame, &frame_len);
    CHECK(rc == 0 && frame_len == 57, "extract_frame: GNSS_Spoof with 0 waypoints is 57 bytes");
    if (frame) free_result(frame);
}

static void test_extract_frame_variable_threat_import() {
    // Threat_Library_import_request (114), fixed 5 bytes, count at offset 1 (u32be) = 1,
    // total = 5 + 1*150 = 155 bytes.
    std::vector<uint8_t> buf(155, 0);
    buf[0] = 114;
    buf[1] = 0; buf[2] = 0; buf[3] = 0; buf[4] = 1; // count = 1 (big-endian u32)
    uint8_t* frame = nullptr;
    size_t frame_len = 0;
    int rc = extract_frame(buf.data(), buf.size(), &frame, &frame_len);
    CHECK(rc == 0 && frame_len == 155, "extract_frame: Threat_Library_import with 1 threat is 155 bytes");
    if (frame) free_result(frame);
}

static void test_extract_frame_unknown_code() {
    uint8_t buf[4] = {250, 0, 0, 0}; // 250 is not a defined request code
    uint8_t* frame = nullptr;
    size_t frame_len = 0;
    int rc = extract_frame(buf, sizeof(buf), &frame, &frame_len);
    CHECK(rc == -1, "extract_frame: unknown command code is rejected");
}

static void test_connect_decode() {
    // Connect (101): Command_Code(1) + True North double(8) = 9 bytes.
    std::vector<uint8_t> buf(9, 0);
    buf[0] = 101;
    uint8_t bits[8]; store_f64be(bits, 45.5);
    std::memcpy(buf.data() + 1, bits, 8);

    uint8_t* frame = nullptr; size_t frame_len = 0;
    CHECK(extract_frame(buf.data(), buf.size(), &frame, &frame_len) == 0, "Connect: extract_frame ok");
    char* json_out = nullptr; size_t json_len = 0;
    int rc = parse_message(frame, frame_len, &json_out, &json_len);
    CHECK(rc == 0, "Connect: parse_message ok");
    CHECK(contains(json_out, "\"command_code\":101"), "Connect: command_code present");
    CHECK(contains(json_out, "\"message_name\":\"Connect\""), "Connect: message_name present");
    CHECK(contains(json_out, "\"true_north\":45.5"), "Connect: true_north decoded");
    if (frame) free_result(frame);
    if (json_out) free_result(json_out);
}

static void test_health_status_encode() {
    const char* kwargs = R"({
        "sdr1":1,"sdr2":1,"sdr3":0,"sdr4":1,"sdr5":1,"sdr6":0,
        "pa1":0,"pa2":0,"pa3":0,"pa4":0
    })";
    uint8_t* buf = nullptr; size_t len = 0;
    int rc = format_response("Health_Status", kwargs, &buf, &len);
    CHECK(rc == 0 && len == 11, "Health_Status: encodes to 11 bytes");
    CHECK(buf[0] == 201, "Health_Status: command_code byte is 201");
    CHECK(buf[1] == 1 && buf[2] == 1 && buf[3] == 0, "Health_Status: sdr1-3 encoded");
    if (buf) free_result(buf);
}

static void test_manual_health_check_request_decode() {
    uint8_t buf[1] = {107};
    uint8_t* frame = nullptr; size_t frame_len = 0;
    CHECK(extract_frame(buf, sizeof(buf), &frame, &frame_len) == 0, "Manual_health_check_request: extract_frame ok");
    char* json_out = nullptr; size_t json_len = 0;
    CHECK(parse_message(frame, frame_len, &json_out, &json_len) == 0, "Manual_health_check_request: parse_message ok");
    CHECK(contains(json_out, "\"message_name\":\"Manual_health_check_request\""), "Manual_health_check_request: message_name present");
    if (frame) free_result(frame);
    if (json_out) free_result(json_out);
}

static void test_manual_health_status_reply_encode() {
    const char* kwargs = R"({"tsu_status":1,"gnss_receiver_status":0})";
    uint8_t* buf = nullptr; size_t len = 0;
    int rc = format_response("Manual_health_status_reply", kwargs, &buf, &len);
    CHECK(rc == 0 && len == 3, "Manual_health_status_reply: encodes to 3 bytes");
    CHECK(buf[0] == 207 && buf[1] == 1 && buf[2] == 0, "Manual_health_status_reply: fields encoded in order");
    if (buf) free_result(buf);
}

static void test_start_jam_decode() {
    // header 15B + 1 band (24B) = 39 bytes.
    std::vector<uint8_t> buf(39, 0);
    buf[0] = 102;
    store_u16be(buf.data() + 1, 4200); // jam_duration
    buf[3] = 2;  // jam_power = Medium
    buf[4] = 0;  // jam_mode = Default
    buf[5] = 0;  // jam_type = 0 in default mode
    buf[6] = 1;  // ism_434mhz selected
    buf[11] = 1; // modulation_scheme_1
    buf[14] = 1; // band_count = 1
    write_fixed_str(buf.data() + 15, 8, "0400.00");
    write_fixed_str(buf.data() + 23, 4, "20.0");
    store_i32be(buf.data() + 27, 4);      // dwell_time_us
    store_f64be(buf.data() + 31, 1.0);    // step_size_mhz

    uint8_t* frame = nullptr; size_t frame_len = 0;
    CHECK(extract_frame(buf.data(), buf.size(), &frame, &frame_len) == 0 && frame_len == 39,
          "Start_Jam: extract_frame gets 39 bytes for band_count=1");
    char* json_out = nullptr; size_t json_len = 0;
    CHECK(parse_message(frame, frame_len, &json_out, &json_len) == 0, "Start_Jam: parse_message ok");
    CHECK(contains(json_out, "\"message_name\":\"Start_Jam\""), "Start_Jam: message_name present");
    CHECK(contains(json_out, "\"jam_duration\":4200"), "Start_Jam: jam_duration decoded");
    CHECK(contains(json_out, "\"band_count\":1"), "Start_Jam: band_count decoded");
    CHECK(contains(json_out, "\"jam_frequency\":\"0400.00\""), "Start_Jam: band jam_frequency decoded");
    if (frame) free_result(frame);
    if (json_out) free_result(json_out);
}

static void test_start_jam_ack_encode() {
    const char* kwargs = R"({
        "pa1":0,"pa2":0,"pa3":0,
        "frequencies":[400.0, 865.0]
    })";
    uint8_t* buf = nullptr; size_t len = 0;
    int rc = format_response("Start_Jam_Ack", kwargs, &buf, &len);
    CHECK(rc == 0 && len == 5 + 2 * 8, "Start_Jam_Ack: encodes header + 2 frequencies");
    CHECK(buf[0] == 203, "Start_Jam_Ack: command_code byte is 203");
    CHECK(buf[4] == 2, "Start_Jam_Ack: frequency_count byte is 2");
    if (buf) free_result(buf);
}

static void test_stop_jam_decode_and_ack_encode() {
    uint8_t buf[1] = {105};
    uint8_t* frame = nullptr; size_t frame_len = 0;
    CHECK(extract_frame(buf, sizeof(buf), &frame, &frame_len) == 0, "Stop_Jam: extract_frame ok");
    char* json_out = nullptr; size_t json_len = 0;
    CHECK(parse_message(frame, frame_len, &json_out, &json_len) == 0, "Stop_Jam: parse_message ok");
    CHECK(contains(json_out, "\"message_name\":\"Stop_Jam\""), "Stop_Jam: message_name present");
    if (frame) free_result(frame);
    if (json_out) free_result(json_out);

    uint8_t* rbuf = nullptr; size_t rlen = 0;
    int rc = format_response("Stop_Jam_Ack", "{}", &rbuf, &rlen);
    CHECK(rc == 0 && rlen == 1 && rbuf[0] == 204, "Stop_Jam_Ack: encodes to single command_code byte 204");
    if (rbuf) free_result(rbuf);
}

static void test_gnss_start_jam_decode() {
    uint8_t buf[7] = {103, 2, 3, 0, 0, 8, 0}; // antenna=2, jam_power=3, satellite_selection=2048
    uint8_t* frame = nullptr; size_t frame_len = 0;
    CHECK(extract_frame(buf, sizeof(buf), &frame, &frame_len) == 0, "GNSS_Start_Jam: extract_frame ok");
    char* json_out = nullptr; size_t json_len = 0;
    CHECK(parse_message(frame, frame_len, &json_out, &json_len) == 0, "GNSS_Start_Jam: parse_message ok");
    CHECK(contains(json_out, "\"satellite_selection\":2048"), "GNSS_Start_Jam: satellite_selection decoded");
    if (frame) free_result(frame);
    if (json_out) free_result(json_out);
}

static void test_gnss_spoof_decode_zero_waypoints() {
    std::vector<uint8_t> buf(57, 0);
    buf[0] = 104;
    buf[1] = 2;  // antenna
    buf[2] = 1;  // jam_power = Low
    buf[10] = 0; // spoof_mode = Fixed
    store_f64be(buf.data() + 11, 17.5); // latitude
    // waypoint_count already zero from the fill
    uint8_t* frame = nullptr; size_t frame_len = 0;
    CHECK(extract_frame(buf.data(), buf.size(), &frame, &frame_len) == 0 && frame_len == 57,
          "GNSS_Spoof: extract_frame gets 57 bytes for 0 waypoints");
    char* json_out = nullptr; size_t json_len = 0;
    CHECK(parse_message(frame, frame_len, &json_out, &json_len) == 0, "GNSS_Spoof: parse_message ok");
    CHECK(contains(json_out, "\"spoof_mode\":0"), "GNSS_Spoof: spoof_mode decoded");
    CHECK(contains(json_out, "\"latitude\":17.5"), "GNSS_Spoof: latitude decoded");
    CHECK(contains(json_out, "\"waypoint_count\":0"), "GNSS_Spoof: waypoint_count decoded as 0");
    if (frame) free_result(frame);
    if (json_out) free_result(json_out);
}

static void test_jam_spoof_ack_and_stop_jam_gnss_ack_encode() {
    uint8_t* buf = nullptr; size_t len = 0;
    CHECK(format_response("Jam_Spoof_Ack", R"({"pa4":0})", &buf, &len) == 0 && len == 2 && buf[0] == 205,
          "Jam_Spoof_Ack: encodes to 2 bytes, code 205");
    if (buf) free_result(buf);

    CHECK(format_response("Stop_Jam_GNSS_Ack", "{}", &buf, &len) == 0 && len == 1 && buf[0] == 206,
          "Stop_Jam_GNSS_Ack: encodes to 1 byte, code 206");
    if (buf) free_result(buf);
}

static void test_gnss_start_jam_ack_dual_code() {
    uint8_t* buf = nullptr; size_t len = 0;
    CHECK(format_response("GNSS_Start_Jam_Ack", R"({"pa4":0})", &buf, &len) == 0 && buf[0] == 208,
          "GNSS_Start_Jam_Ack: defaults to code 208");
    if (buf) free_result(buf);

    CHECK(format_response("GNSS_Start_Jam_Ack", R"({"pa4":0,"command_code":108})", &buf, &len) == 0 && buf[0] == 108,
          "GNSS_Start_Jam_Ack: explicit command_code=108 honored");
    if (buf) free_result(buf);
}

static void test_stop_jam_gnss_decode() {
    uint8_t sbuf[1] = {106};
    uint8_t* frame = nullptr; size_t frame_len = 0;
    CHECK(extract_frame(sbuf, sizeof(sbuf), &frame, &frame_len) == 0, "Stop_Jam_GNSS: extract_frame ok");
    char* json_out = nullptr; size_t json_len = 0;
    CHECK(parse_message(frame, frame_len, &json_out, &json_len) == 0, "Stop_Jam_GNSS: parse_message ok");
    CHECK(contains(json_out, "\"message_name\":\"Stop_Jam_GNSS\""), "Stop_Jam_GNSS: message_name present");
    if (frame) free_result(frame);
    if (json_out) free_result(json_out);
}

static void test_nav_data_decode_and_ack_encode() {
    uint8_t buf[2] = {113, 1}; // enable
    uint8_t* frame = nullptr; size_t frame_len = 0;
    CHECK(extract_frame(buf, sizeof(buf), &frame, &frame_len) == 0, "NAV_data: extract_frame ok");
    char* json_out = nullptr; size_t json_len = 0;
    CHECK(parse_message(frame, frame_len, &json_out, &json_len) == 0, "NAV_data: parse_message ok");
    CHECK(contains(json_out, "\"enable\":true"), "NAV_data: enable decoded true");
    if (frame) free_result(frame);
    if (json_out) free_result(json_out);

    uint8_t* rbuf = nullptr; size_t rlen = 0;
    CHECK(format_response("NAV_data_enable/disable_ack", "{}", &rbuf, &rlen) == 0 && rlen == 1 && rbuf[0] == 213,
          "NAV_data_enable/disable_ack: encodes to 1 byte, code 213");
    if (rbuf) free_result(rbuf);
}

static void test_bite_mode_request_decode_and_reply_encode() {
    uint8_t buf[1] = {109};
    uint8_t* frame = nullptr; size_t frame_len = 0;
    CHECK(extract_frame(buf, sizeof(buf), &frame, &frame_len) == 0, "Bite_mode_request: extract_frame ok");
    char* json_out = nullptr; size_t json_len = 0;
    CHECK(parse_message(frame, frame_len, &json_out, &json_len) == 0, "Bite_mode_request: parse_message ok");
    CHECK(contains(json_out, "\"message_name\":\"Bite_mode_request\""), "Bite_mode_request: message_name present");
    if (frame) free_result(frame);
    if (json_out) free_result(json_out);

    const char* kwargs = R"({"receiver_antenna_status":"OK_ALL_CHANS"})";
    uint8_t* rbuf = nullptr; size_t rlen = 0;
    CHECK(format_response("Bite_mode_reply", kwargs, &rbuf, &rlen) == 0 && rlen == 17 && rbuf[0] == 209,
          "Bite_mode_reply: encodes to 17 bytes, code 209");
    if (rbuf) free_result(rbuf);
}

static void test_targets_detected_encode() {
    const char* kwargs = R"({
      "targets":[
        {"skycope_or_rfdd_flag":0,"target_id":7,"band_id":2,
         "center_frequency_mhz":2450.5,"bandwidth_khz":500.0,"signal_strength_dbm":-45.0,
         "time_of_first_arrival":"120000","time_of_last_arrival":"120005",
         "doa_deg_x1000":180000,"elevation_deg_x1000":15000,"threat_confidence_pct":80,
         "target_model_label":"DJI Mavic 3",
         "drone_lat_x1e6":17123456,"drone_lng_x1e6":78123456,
         "remote_lat_x1e6":17123000,"remote_lng_x1e6":78123000,
         "emitter_type":"D",
         "home_lat_x1e6":17120000,"home_lng_x1e6":78120000,
         "distance_m":1200,"height_m":150}
      ]
    })";
    uint8_t* buf = nullptr; size_t len = 0;
    int rc = format_response("Targets_Detected", kwargs, &buf, &len);
    CHECK(rc == 0 && len == 1 + 4 + 101, "Targets_Detected: encodes header + 1 target block");
    CHECK(buf[0] == 202, "Targets_Detected: command_code byte is 202");
    if (buf) free_result(buf);
}

static void test_sbc_shutdown_req_decode() {
    uint8_t buf[1] = {121};
    uint8_t* frame = nullptr; size_t frame_len = 0;
    CHECK(extract_frame(buf, sizeof(buf), &frame, &frame_len) == 0, "SBC_shutdown_req: extract_frame ok");
    char* json_out = nullptr; size_t json_len = 0;
    CHECK(parse_message(frame, frame_len, &json_out, &json_len) == 0, "SBC_shutdown_req: parse_message ok");
    CHECK(contains(json_out, "\"message_name\":\"SBC_shutdown_req\""), "SBC_shutdown_req: message_name present");
    if (frame) free_result(frame);
    if (json_out) free_result(json_out);

    // No reply exists for this message — confirm format_response correctly rejects it.
    uint8_t* rbuf = nullptr; size_t rlen = 0;
    CHECK(format_response("SBC_shutdown_ack", "{}", &rbuf, &rlen) == -1,
          "SBC_shutdown: no reply message exists, format_response rejects any attempt");
}

static void test_extract_param_req_decode_and_reply_encode() {
    std::vector<uint8_t> buf(21, 0);
    buf[0] = 117;
    store_f64be(buf.data() + 1, 2450.5);
    store_f64be(buf.data() + 9, 500.0);
    store_u32be(buf.data() + 17, 90);
    uint8_t* frame = nullptr; size_t frame_len = 0;
    CHECK(extract_frame(buf.data(), buf.size(), &frame, &frame_len) == 0, "Extract_param_req: extract_frame ok");
    char* json_out = nullptr; size_t json_len = 0;
    CHECK(parse_message(frame, frame_len, &json_out, &json_len) == 0, "Extract_param_req: parse_message ok");
    CHECK(contains(json_out, "\"doa_deg\":90"), "Extract_param_req: doa_deg decoded");
    if (frame) free_result(frame);
    if (json_out) free_result(json_out);

    const char* kwargs = R"({"center_frequency_mhz":2450.5,"instantaneous_bandwidth_khz":500.0,
                             "burst_period":"CW","burst_width":"CW"})";
    uint8_t* rbuf = nullptr; size_t rlen = 0;
    CHECK(format_response("Extract_param_reply", kwargs, &rbuf, &rlen) == 0 && rlen == 113 && rbuf[0] == 217,
          "Extract_param_reply: encodes to 113 bytes, code 217");
    if (rbuf) free_result(rbuf);
}

static void test_threat_library_addition_decode_and_ack_encode() {
    std::vector<uint8_t> buf(151, 0);
    buf[0] = 111;
    buf[1] = 1; // frequency_parameter_type = fixed center freq
    store_f64be(buf.data() + 2, 2450.5);   // frequency_lower_or_center_mhz
    store_f64be(buf.data() + 10, 0.0);     // frequency_higher_mhz (unused, type=1)
    store_f64be(buf.data() + 18, 500.0);   // instantaneous_bandwidth_khz
    write_fixed_str(buf.data() + 26, 50, "CW");
    write_fixed_str(buf.data() + 76, 50, "CW");
    write_fixed_str(buf.data() + 126, 25, "Test Threat");
    uint8_t* frame = nullptr; size_t frame_len = 0;
    CHECK(extract_frame(buf.data(), buf.size(), &frame, &frame_len) == 0, "Threat_Library_addition: extract_frame ok");
    char* json_out = nullptr; size_t json_len = 0;
    CHECK(parse_message(frame, frame_len, &json_out, &json_len) == 0, "Threat_Library_addition: parse_message ok");
    CHECK(contains(json_out, "\"label\":\"Test Threat\""), "Threat_Library_addition: label decoded");
    if (frame) free_result(frame);
    if (json_out) free_result(json_out);

    uint8_t* rbuf = nullptr; size_t rlen = 0;
    CHECK(format_response("Threat_Library_addition_ack", R"({"ack":1,"error_code":" "})", &rbuf, &rlen) == 0
          && rlen == 3 && rbuf[0] == 211 && rbuf[1] == 1 && rbuf[2] == ' ',
          "Threat_Library_addition_ack: encodes to 3 bytes, code 211");
    if (rbuf) free_result(rbuf);
}

static std::vector<uint8_t> make_threat_block(double center, const std::string& label) {
    std::vector<uint8_t> b(150, 0);
    b[0] = 1; // frequency_parameter_type = fixed center
    store_f64be(b.data() + 1, center);
    store_f64be(b.data() + 9, 0.0);
    store_f64be(b.data() + 17, 500.0);
    write_fixed_str(b.data() + 25, 50, "CW");
    write_fixed_str(b.data() + 75, 50, "CW");
    write_fixed_str(b.data() + 125, 25, label);
    return b;
}

static void test_threat_library_import_decode_and_ack_encode() {
    std::vector<uint8_t> buf;
    buf.push_back(114);
    uint8_t count_bytes[4]; store_u32be(count_bytes, 1);
    buf.insert(buf.end(), count_bytes, count_bytes + 4);
    auto block = make_threat_block(2450.5, "Imported Threat");
    buf.insert(buf.end(), block.begin(), block.end());

    uint8_t* frame = nullptr; size_t frame_len = 0;
    CHECK(extract_frame(buf.data(), buf.size(), &frame, &frame_len) == 0 && frame_len == 155,
          "Threat_Library_import_request: extract_frame gets 155 bytes for 1 threat");
    char* json_out = nullptr; size_t json_len = 0;
    CHECK(parse_message(frame, frame_len, &json_out, &json_len) == 0, "Threat_Library_import_request: parse_message ok");
    CHECK(contains(json_out, "\"threat_count\":1"), "Threat_Library_import_request: threat_count decoded");
    CHECK(contains(json_out, "\"Imported Threat\""), "Threat_Library_import_request: label decoded inside threats array");
    if (frame) free_result(frame);
    if (json_out) free_result(json_out);

    uint8_t* rbuf = nullptr; size_t rlen = 0;
    CHECK(format_response("Threat_Library_import_ack", R"({"ack":1,"error_code":" "})", &rbuf, &rlen) == 0
          && rlen == 3 && rbuf[0] == 214,
          "Threat_Library_import_ack: encodes to 3 bytes, code 214");
    if (rbuf) free_result(rbuf);
}

static void test_threat_library_export_decode_and_ack_encode() {
    uint8_t buf[1] = {115};
    uint8_t* frame = nullptr; size_t frame_len = 0;
    CHECK(extract_frame(buf, sizeof(buf), &frame, &frame_len) == 0, "Threat_Library_export_request: extract_frame ok");
    char* json_out = nullptr; size_t json_len = 0;
    CHECK(parse_message(frame, frame_len, &json_out, &json_len) == 0, "Threat_Library_export_request: parse_message ok");
    CHECK(contains(json_out, "\"message_name\":\"Threat_Library_export_request\""), "Threat_Library_export_request: message_name present");
    if (frame) free_result(frame);
    if (json_out) free_result(json_out);

    const char* kwargs = R"({"threats":[{"frequency_parameter_type":1,"frequency_lower_or_center_mhz":2450.5,
        "frequency_higher_mhz":0.0,"instantaneous_bandwidth_khz":500.0,"burst_period":"CW","burst_width":"CW",
        "label":"Exported Threat"}]})";
    uint8_t* rbuf = nullptr; size_t rlen = 0;
    CHECK(format_response("Threat_Library_export_ack", kwargs, &rbuf, &rlen) == 0
          && rlen == 1 + 4 + 150 && rbuf[0] == 215,
          "Threat_Library_export_ack: encodes header + 1 threat block, code 215");
    if (rbuf) free_result(rbuf);
}

static void test_threat_library_delete_decode_and_ack_encode() {
    std::vector<uint8_t> buf(27, 0);
    buf[0] = 116;
    buf[1] = 'S';
    write_fixed_str(buf.data() + 2, 25, "Old Threat");
    uint8_t* frame = nullptr; size_t frame_len = 0;
    CHECK(extract_frame(buf.data(), buf.size(), &frame, &frame_len) == 0, "Threat_Library_delete_request: extract_frame ok");
    char* json_out = nullptr; size_t json_len = 0;
    CHECK(parse_message(frame, frame_len, &json_out, &json_len) == 0, "Threat_Library_delete_request: parse_message ok");
    CHECK(contains(json_out, "\"label\":\"Old Threat\""), "Threat_Library_delete_request: label decoded");
    if (frame) free_result(frame);
    if (json_out) free_result(json_out);

    uint8_t* rbuf = nullptr; size_t rlen = 0;
    CHECK(format_response("Threat_Library_delete_ack", R"({"ack":1,"error_code":" "})", &rbuf, &rlen) == 0
          && rlen == 3 && rbuf[0] == 216,
          "Threat_Library_delete_ack: encodes to 3 bytes, code 216");
    if (rbuf) free_result(rbuf);
}

static void test_skycope_health_status_req_decode_and_reply_encode() {
    uint8_t buf[1] = {118};
    uint8_t* frame = nullptr; size_t frame_len = 0;
    CHECK(extract_frame(buf, sizeof(buf), &frame, &frame_len) == 0, "SKYCOPE_health_status_req: extract_frame ok");
    char* json_out = nullptr; size_t json_len = 0;
    CHECK(parse_message(frame, frame_len, &json_out, &json_len) == 0, "SKYCOPE_health_status_req: parse_message ok");
    CHECK(contains(json_out, "\"message_name\":\"SKYCOPE_health_status_req\""), "SKYCOPE_health_status_req: message_name present");
    if (frame) free_result(frame);
    if (json_out) free_result(json_out);

    const char* kwargs = R"({
        "sensor_id":"SF7000013276","detection_range_m":7000,
        "gps_latitude_x1e6":17454976,"gps_longitude_x1e6":78364243,
        "cpu_temperature_x10":700,"cpu_usage_pct_x10":401,
        "disk_usage":"2.2G/110.9G",
        "gpu_temperature_x10":600,"gpu_usage_pct_x10":299,"memory_usage_pct_x10":248,
        "power_consumption":3,
        "system_time":"03/04/2025 17:13:27 +04",
        "uptime_x100":109413,"xpu_frequency":11381,
        "nfz_height_m":100,"nfz_latitude_x1e6":17454976,"nfz_longitude_x1e6":78364243
    })";
    uint8_t* rbuf = nullptr; size_t rlen = 0;
    int rc = format_response("SKYCOPE_health_status_reply", kwargs, &rbuf, &rlen);
    // fixed 41B (incl. cmd code + 3 length bytes) + 12 ("SF7000013276") + 11 ("2.2G/110.9G") + 23 (system_time)
    CHECK(rc == 0 && rbuf[0] == 218, "SKYCOPE_health_status_reply: encodes, code 218");
    CHECK(rlen == 41 + 12 + 11 + 23, "SKYCOPE_health_status_reply: total length matches fixed + 3 var strings");
    if (rbuf) free_result(rbuf);
}

static void test_skycope_shutdown_req_decode_and_reply_encode() {
    uint8_t buf[1] = {120};
    uint8_t* frame = nullptr; size_t frame_len = 0;
    CHECK(extract_frame(buf, sizeof(buf), &frame, &frame_len) == 0, "SKYCOPE_shutdown_req: extract_frame ok");
    char* json_out = nullptr; size_t json_len = 0;
    CHECK(parse_message(frame, frame_len, &json_out, &json_len) == 0, "SKYCOPE_shutdown_req: parse_message ok");
    CHECK(contains(json_out, "\"message_name\":\"SKYCOPE_shutdown_req\""), "SKYCOPE_shutdown_req: message_name present");
    if (frame) free_result(frame);
    if (json_out) free_result(json_out);

    uint8_t* rbuf = nullptr; size_t rlen = 0;
    CHECK(format_response("SKYCOPE_shutdown_reply", "{}", &rbuf, &rlen) == 0 && rlen == 1 && rbuf[0] == 220,
          "SKYCOPE_shutdown_reply: encodes to 1 byte, code 220");
    if (rbuf) free_result(rbuf);
}

static void test_detection_configuration_decode_and_ack_encode() {
    uint8_t buf[2] = {119, 1};
    uint8_t* frame = nullptr; size_t frame_len = 0;
    CHECK(extract_frame(buf, sizeof(buf), &frame, &frame_len) == 0, "Detection_configuration: extract_frame ok");
    char* json_out = nullptr; size_t json_len = 0;
    CHECK(parse_message(frame, frame_len, &json_out, &json_len) == 0, "Detection_configuration: parse_message ok");
    CHECK(contains(json_out, "\"skycope_flag\":1"), "Detection_configuration: skycope_flag decoded");
    if (frame) free_result(frame);
    if (json_out) free_result(json_out);

    uint8_t* rbuf = nullptr; size_t rlen = 0;
    CHECK(format_response("Detection_configuration_ack", "{}", &rbuf, &rlen) == 0 && rlen == 1 && rbuf[0] == 219,
          "Detection_configuration_ack: encodes to 1 byte, code 219");
    if (rbuf) free_result(rbuf);
}

static void test_detection_fusion_decode_and_reply_encode() {
    uint8_t buf[2] = {122, 1};
    uint8_t* frame = nullptr; size_t frame_len = 0;
    CHECK(extract_frame(buf, sizeof(buf), &frame, &frame_len) == 0, "Detection_Fusion_Request: extract_frame ok");
    char* json_out = nullptr; size_t json_len = 0;
    CHECK(parse_message(frame, frame_len, &json_out, &json_len) == 0, "Detection_Fusion_Request: parse_message ok");
    CHECK(contains(json_out, "\"fusion_mode\":1"), "Detection_Fusion_Request: fusion_mode decoded");
    if (frame) free_result(frame);
    if (json_out) free_result(json_out);

    uint8_t* rbuf = nullptr; size_t rlen = 0;
    CHECK(format_response("Detection_Fusion_Reply", "{}", &rbuf, &rlen) == 0 && rlen == 1 && rbuf[0] == 222,
          "Detection_Fusion_Reply: encodes to 1 byte, code 222");
    if (rbuf) free_result(rbuf);
}

static void test_radar_blanking_decode_and_reply_encode() {
    std::vector<uint8_t> buf(10, 0);
    buf[0] = 123;
    store_i32be(buf.data() + 1, 17123456);
    store_i32be(buf.data() + 5, 78123456);
    buf[9] = 1; // blanking_enable
    uint8_t* frame = nullptr; size_t frame_len = 0;
    CHECK(extract_frame(buf.data(), buf.size(), &frame, &frame_len) == 0, "Radar_Blanking_Req: extract_frame ok");
    char* json_out = nullptr; size_t json_len = 0;
    CHECK(parse_message(frame, frame_len, &json_out, &json_len) == 0, "Radar_Blanking_Req: parse_message ok");
    CHECK(contains(json_out, "\"blanking_enable\":true"), "Radar_Blanking_Req: blanking_enable decoded");
    if (frame) free_result(frame);
    if (json_out) free_result(json_out);

    uint8_t* rbuf = nullptr; size_t rlen = 0;
    CHECK(format_response("Radar_Blanking_Reply", "{}", &rbuf, &rlen) == 0 && rlen == 1 && rbuf[0] == 223,
          "Radar_Blanking_Reply: encodes to 1 byte, code 223");
    if (rbuf) free_result(rbuf);
}

static void test_all_20_requests_have_distinct_message_names() {
    // One minimal, individually-correct frame per request code -- checks
    // that command_code and message_name both come back correctly and
    // distinctly for every one of the 20 request codes.
    struct Req { uint8_t code; const char* name; std::vector<uint8_t> frame; };
    std::vector<Req> reqs = {
        {101, "Connect",                          std::vector<uint8_t>(9, 0)},
        {107, "Manual_health_check_request",      std::vector<uint8_t>(1, 0)},
        {102, "Start_Jam",                        std::vector<uint8_t>(15, 0)}, // band_count left 0 -> 15B total
        {105, "Stop_Jam",                         std::vector<uint8_t>(1, 0)},
        {103, "GNSS_Start_Jam",                   std::vector<uint8_t>(7, 0)},
        {104, "GNSS_Spoof",                        std::vector<uint8_t>(57, 0)}, // waypoint_count 0 -> 57B
        {106, "Stop_Jam_GNSS",                    std::vector<uint8_t>(1, 0)},
        {113, "NAV_data_enable/disable",          std::vector<uint8_t>(2, 0)},
        {109, "Bite_mode_request",                std::vector<uint8_t>(1, 0)},
        {117, "Extract_param_req",                std::vector<uint8_t>(21, 0)},
        {111, "Threat_Library_addition",          std::vector<uint8_t>(151, 0)},
        {114, "Threat_Library_import_request",    std::vector<uint8_t>(5, 0)}, // count 0 -> 5B
        {115, "Threat_Library_export_request",    std::vector<uint8_t>(1, 0)},
        {116, "Threat_Library_delete_request",    std::vector<uint8_t>(27, 0)},
        {121, "SBC_shutdown_req",                 std::vector<uint8_t>(1, 0)},
        {118, "SKYCOPE_health_status_req",        std::vector<uint8_t>(1, 0)},
        {120, "SKYCOPE_shutdown_req",             std::vector<uint8_t>(1, 0)},
        {119, "Detection_configuration",          std::vector<uint8_t>(2, 0)},
        {122, "Detection_Fusion_Request",         std::vector<uint8_t>(2, 0)},
        {123, "Radar_Blanking_Req",               std::vector<uint8_t>(10, 0)},
    };
    for (auto& r : reqs) {
        r.frame[0] = r.code;
        uint8_t* frame = nullptr; size_t frame_len = 0;
        int erc = extract_frame(r.frame.data(), r.frame.size(), &frame, &frame_len);
        std::string label = std::string("full-coverage: ") + r.name + " extract_frame ok";
        CHECK(erc == 0, label.c_str());
        if (erc != 0) continue;

        char* json_out = nullptr; size_t json_len = 0;
        int prc = parse_message(frame, frame_len, &json_out, &json_len);
        std::string label2 = std::string("full-coverage: ") + r.name + " parse_message ok";
        CHECK(prc == 0, label2.c_str());
        if (prc == 0) {
            std::string expect_code = "\"command_code\":" + std::to_string(r.code);
            std::string expect_name = std::string("\"message_name\":\"") + r.name + "\"";
            std::string label3 = std::string("full-coverage: ") + r.name + " command_code matches";
            CHECK(contains(json_out, expect_code.c_str()), label3.c_str());
            std::string label4 = std::string("full-coverage: ") + r.name + " message_name matches";
            CHECK(contains(json_out, expect_name.c_str()), label4.c_str());
        }
        if (frame) free_result(frame);
        if (json_out) free_result(json_out);
    }
}

static void test_all_20_replies_encode_with_correct_command_code() {
    struct Rep { const char* name; int expected_code; const char* kwargs; };
    std::vector<Rep> reps = {
        {"Health_Status", 201, "{}"},
        {"Targets_Detected", 202, R"({"targets":[]})"},
        {"Manual_health_status_reply", 207, "{}"},
        {"Start_Jam_Ack", 203, R"({"frequencies":[]})"},
        {"Stop_Jam_Ack", 204, "{}"},
        {"GNSS_Start_Jam_Ack", 208, "{}"}, // default code path
        {"Jam_Spoof_Ack", 205, "{}"},
        {"Stop_Jam_GNSS_Ack", 206, "{}"},
        {"NAV_data_enable/disable_ack", 213, "{}"},
        {"Bite_mode_reply", 209, "{}"},
        {"Extract_param_reply", 217, "{}"},
        {"Threat_Library_addition_ack", 211, "{}"},
        {"Threat_Library_import_ack", 214, "{}"},
        {"Threat_Library_export_ack", 215, R"({"threats":[]})"},
        {"Threat_Library_delete_ack", 216, "{}"},
        {"SKYCOPE_health_status_reply", 218, "{}"},
        {"SKYCOPE_shutdown_reply", 220, "{}"},
        {"Detection_configuration_ack", 219, "{}"},
        {"Detection_Fusion_Reply", 222, "{}"},
        {"Radar_Blanking_Reply", 223, "{}"},
    };
    for (const auto& r : reps) {
        uint8_t* buf = nullptr; size_t len = 0;
        int rc = format_response(r.name, r.kwargs, &buf, &len);
        std::string label = std::string("full-coverage: ") + r.name + " format_response ok";
        CHECK(rc == 0, label.c_str());
        if (rc == 0) {
            std::string label2 = std::string("full-coverage: ") + r.name + " command_code byte matches";
            CHECK(buf[0] == static_cast<uint8_t>(r.expected_code), label2.c_str());
        }
        if (buf) free_result(buf);
    }
}

static void test_sbc_shutdown_has_no_reply_and_gnss_ack_108_still_works() {
    uint8_t* buf = nullptr; size_t len = 0;
    CHECK(format_response("SBC_shutdown_ack", "{}", &buf, &len) == -1,
          "full-coverage: SBC_shutdown has genuinely no reply message");
    CHECK(format_response("GNSS_Start_Jam_Ack", R"({"command_code":108})", &buf, &len) == 0 && buf[0] == 108,
          "full-coverage: GNSS_Start_Jam_Ack explicit code 108 still reachable");
    if (buf) free_result(buf);
}

int main() {
    test_extract_frame_fixed_length();
    test_extract_frame_incomplete();
    test_extract_frame_variable_start_jam();
    test_extract_frame_variable_gnss_spoof_zero_waypoints();
    test_extract_frame_variable_threat_import();
    test_extract_frame_unknown_code();
    test_connect_decode();
    test_health_status_encode();
    test_manual_health_check_request_decode();
    test_manual_health_status_reply_encode();
    test_start_jam_decode();
    test_start_jam_ack_encode();
    test_stop_jam_decode_and_ack_encode();
    test_gnss_start_jam_decode();
    test_gnss_spoof_decode_zero_waypoints();
    test_jam_spoof_ack_and_stop_jam_gnss_ack_encode();
    test_gnss_start_jam_ack_dual_code();
    test_stop_jam_gnss_decode();
    test_nav_data_decode_and_ack_encode();
    test_bite_mode_request_decode_and_reply_encode();
    test_targets_detected_encode();
    test_sbc_shutdown_req_decode();
    test_extract_param_req_decode_and_reply_encode();
    test_threat_library_addition_decode_and_ack_encode();
    test_threat_library_import_decode_and_ack_encode();
    test_threat_library_export_decode_and_ack_encode();
    test_threat_library_delete_decode_and_ack_encode();
    test_skycope_health_status_req_decode_and_reply_encode();
    test_skycope_shutdown_req_decode_and_reply_encode();
    test_detection_configuration_decode_and_ack_encode();
    test_detection_fusion_decode_and_reply_encode();
    test_radar_blanking_decode_and_reply_encode();
    test_all_20_requests_have_distinct_message_names();
    test_all_20_replies_encode_with_correct_command_code();
    test_sbc_shutdown_has_no_reply_and_gnss_ack_108_still_works();
    std::printf("\n%d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
