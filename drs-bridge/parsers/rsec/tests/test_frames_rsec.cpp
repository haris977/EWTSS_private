// drs-bridge/parsers/rsec/tests/test_frames_rsec.cpp
//
// Unit tests for the HIMSHAKTI RSEC parser DLL.
// No external test framework — plain assert() + stderr reporting.
// Build and run via CMake: cmake --build build && ctest --build build
//
// NOTE: calls the real 4-symbol ABI (sdfc_abi.h) — extract_frame/parse_message/
// format_response all take out-pointer pairs and return 0/-1; frame_type is
// inferred from magic bytes, never passed in. Every out-pointer returned here
// is DLL-heap-allocated and must be released with free_result().

#include "sdfc_abi.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static inline void store_be16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[1] = static_cast<uint8_t>( v       & 0xFF);
}

static inline void store_be32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>((v >> 24) & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[2] = static_cast<uint8_t>((v >>  8) & 0xFF);
    p[3] = static_cast<uint8_t>( v        & 0xFF);
}

static inline uint16_t load_be16(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

static int pass_count = 0;
static int fail_count = 0;

#define TEST_PASS(name) do { std::printf("[PASS] %s\n", name); ++pass_count; } while(0)
#define TEST_FAIL(name, msg) do { std::fprintf(stderr, "[FAIL] %s: %s\n", name, msg); ++fail_count; } while(0)

// Build a Variant A frame in-place
static const uint8_t* make_variant_a(uint16_t cmd, uint16_t seq,
                                      const uint8_t* body, int body_len, int* total) {
    static uint8_t buf[4096];
    *total = 12 + body_len;
    store_be16(buf + 0, 0xAAAAu);  // SOM
    store_be16(buf + 2, cmd);
    store_be16(buf + 4, seq);
    store_be32(buf + 6, static_cast<uint32_t>(body_len));
    if (body_len > 0) std::memcpy(buf + 10, body, static_cast<size_t>(body_len));
    store_be16(buf + 10 + body_len, 0xEEEEu); // EOM
    return buf;
}

// Build a Variant B frame in-place
static const uint8_t* make_variant_b(uint16_t cmd_uid,
                                      const uint8_t* body, int body_len, int* total) {
    static uint8_t buf[4096];
    *total = 16 + body_len;
    buf[0] = 0xAA; buf[1] = 0xAB; buf[2] = 0xBA; buf[3] = 0xBB;
    store_be32(buf + 4, static_cast<uint32_t>(body_len));
    store_be16(buf + 8,  0x0064u);   // CmdGroup always 100
    store_be16(buf + 10, cmd_uid);
    if (body_len > 0) std::memcpy(buf + 12, body, static_cast<size_t>(body_len));
    buf[12 + body_len + 0] = 0xCC;
    buf[12 + body_len + 1] = 0xCD;
    buf[12 + body_len + 2] = 0xDC;
    buf[12 + body_len + 3] = 0xDD;
    return buf;
}

// Build a Variant C (SCU) frame in-place
static const uint8_t* make_variant_c(uint8_t cmd_code,
                                      const uint8_t* data, int data_len, int* total) {
    static uint8_t buf[256];
    uint8_t data_len_field = static_cast<uint8_t>(1 + data_len); // CmdCode + data bytes
    *total = 1 + 1 + data_len_field + 1 + 1;                     // SOF+DataLen+body+Checksum+EOF
    buf[0] = 0x24; // SOF
    buf[1] = data_len_field;
    buf[2] = cmd_code;
    if (data_len > 0) std::memcpy(buf + 3, data, static_cast<size_t>(data_len));
    // XOR checksum: DataLen through last data byte
    uint8_t xr = buf[1];
    for (int i = 0; i < data_len_field; ++i) xr ^= buf[2 + i];
    buf[2 + data_len_field] = xr;
    buf[3 + data_len_field] = 0x0D; // EOF
    return buf;
}

// Runs extract_frame + parse_message on a raw wire buffer, returns the JSON
// text (caller must NOT free — freed internally before returning a copy).
static std::string extract_and_parse(const uint8_t* wire, int wire_len) {
    uint8_t* frame = nullptr;
    size_t   frame_len = 0;
    int r = extract_frame(wire, static_cast<size_t>(wire_len), &frame, &frame_len);
    if (r != 0) {
        throw std::runtime_error("extract_frame did not return 0 (success)");
    }

    char*  json = nullptr;
    size_t json_len = 0;
    int pr = parse_message(frame, frame_len, &json, &json_len);
    free_result(frame);
    if (pr != 0 || !json) {
        free_result(json);
        throw std::runtime_error("parse_message failed");
    }

    std::string s(json, json_len);
    free_result(json);
    return s;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void test_extract_variant_a_active_track() {
    const char* name = "extract_variant_a_active_track";
    try {
        uint8_t body[26] = {};
        body[0] = 1;     // SystemMode = operational
        body[1] = 1;     // NumTracks = 1
        store_be16(body + 2, 42);              // TrackID = 42
        store_be16(body + 4, 900);              // DOA = 90.0 deg
        store_be32(body + 6, 10000);             // Freq = 10000 KHz = 10 MHz
        store_be32(body + 10, 1000);             // PRI = 1000 us
        store_be32(body + 14, 5000);             // PW = 5000 ns
        store_be32(body + 18, 500);              // ScanPeriod = 500 ms
        store_be16(body + 22, static_cast<uint16_t>(-40)); // Amplitude = -40 dBm
        body[24] = 0x01; // Status = active
        body[25] = 3;    // EmitterCategory = 3

        int total;
        const uint8_t* frame = make_variant_a(0x1502, 1, body, 26, &total);
        std::string s = extract_and_parse(frame, total);

        bool ok = s.find("\"msg_type\":\"active_track_data\"") != std::string::npos
               && s.find("\"num_tracks\":1") != std::string::npos
               && s.find("\"track_id\":42") != std::string::npos
               && s.find("\"doa_deg\":90") != std::string::npos
               && s.find("\"freq_mhz\":10") != std::string::npos;
        if (ok) TEST_PASS(name); else TEST_FAIL(name, "JSON content unexpected");
    } catch (const std::exception& e) { TEST_FAIL(name, e.what()); }
}

static void test_extract_variant_a_operational_data() {
    const char* name = "extract_variant_a_operational_data";
    try {
        uint8_t body[8] = {};
        body[0] = 1; // ESMP operational
        body[1] = 3; // 3 active tracks
        body[2] = 0; // 0 manual tracks
        body[3] = 1; // scanning
        store_be16(body + 4, 2); // band index 2
        body[6] = 0; // hw_status ok

        int total;
        const uint8_t* frame = make_variant_a(0x1504, 5, body, 8, &total);
        std::string s = extract_and_parse(frame, total);

        bool ok = s.find("\"msg_type\":\"operational_data\"") != std::string::npos
               && s.find("\"esmp_status\":\"operational\"") != std::string::npos
               && s.find("\"num_active_tracks\":3") != std::string::npos;
        if (ok) TEST_PASS(name); else TEST_FAIL(name, "JSON content unexpected");
    } catch (const std::exception& e) { TEST_FAIL(name, e.what()); }
}

static void test_extract_variant_a_ack_nack() {
    const char* name = "extract_variant_a_ack_nack";
    try {
        // Body (IRS 5-byte layout): MessageCode(2B) + SequenceNumber(2B) + Status(1B)
        uint8_t body[5] = {};
        store_be16(body + 0, 0x1003); // acking a Load Warner Library command
        store_be16(body + 2, 7);      // sequence number 7
        body[4] = 2;                  // ACK received & executed successfully

        int total;
        const uint8_t* frame = make_variant_a(0x1509, 7, body, 5, &total);
        std::string s = extract_and_parse(frame, total);

        bool ok = s.find("\"msg_type\":\"ack_nack_esmp_to_rsec\"") != std::string::npos
               && s.find("\"acked_cmd_code_hex\":\"0x1003\"") != std::string::npos
               && s.find("\"acked_seq_no\":7") != std::string::npos
               && s.find("\"ack_status\":2") != std::string::npos
               && s.find("\"ack_status_text\":\"ack_received_and_executed\"") != std::string::npos;
        if (ok) TEST_PASS(name); else TEST_FAIL(name, "JSON content unexpected");
    } catch (const std::exception& e) { TEST_FAIL(name, e.what()); }
}

static void test_extract_variant_a_track_and_jam() {
    const char* name = "extract_variant_a_track_and_jam";
    try {
        // Body per IRS §5.5.3, 69 bytes total.
        uint8_t body[69] = {};
        store_be16(body + 0, 77);      // TrackNo = 77
        store_be16(body + 2, 900);     // DOA = 90.0 deg
        store_be32(body + 4, 1200000); // Frequency = 1200000 KHz = 1200 MHz
        store_be32(body + 8, 0);       // FrequencyAttributes
        store_be32(body + 12, 1000);   // PW ns
        store_be32(body + 16, 2000);   // PRF Hz
        store_be32(body + 20, 0);      // PRIAttributes
        body[24] = static_cast<uint8_t>(-30); // Amplitude dBm
        body[25] = 1;                  // ScanType
        store_be16(body + 26, 100);    // ASP ms
        store_be32(body + 28, 500);    // PWMin ns
        store_be32(body + 32, 1500);   // PWMax ns
        store_be32(body + 36, 1200000);// SpotFrequencies KHz
        body[40] = 5;                  // ThreatLevel
        store_be16(body + 41, 150);    // Elevation = 15.0 deg
        // JPRO at offset 43, 24 bytes: DRFM mode=2, num_responses=1
        store_be16(body + 43, 2);
        body[45] = 1;                  // num_responses = 1 (resp_byte low 5 bits)
        body[46] = 3;                  // technique = 3
        // remaining technique bytes default 0
        store_be16(body + 67, 30);     // JamDuration = 30 sec

        int total;
        const uint8_t* frame = make_variant_a(0x1103, 2, body, 69, &total);
        std::string s = extract_and_parse(frame, total);

        bool ok = s.find("\"msg_type\":\"semi_auto_track_jam\"") != std::string::npos
               && s.find("\"track_no\":77") != std::string::npos
               && s.find("\"drfm_mode\":2") != std::string::npos
               && s.find("\"jam_duration_sec\":30") != std::string::npos;
        if (ok) TEST_PASS(name); else TEST_FAIL(name, "JSON content unexpected");
    } catch (const std::exception& e) { TEST_FAIL(name, e.what()); }
}

static void test_extract_variant_a_jam_command() {
    const char* name = "extract_variant_a_jam_command";
    try {
        // Body per IRS §5.5.2: NumberOfEmitters(1B) + N*[EmitterNumber(2B)+JamDuration(2B)]
        uint8_t body[5] = {};
        body[0] = 1;                // 1 emitter
        store_be16(body + 1, 15);   // EmitterNumber = 15
        store_be16(body + 3, 20);   // JamDuration = 20 sec

        int total;
        const uint8_t* frame = make_variant_a(0x1102, 3, body, 5, &total);
        std::string s = extract_and_parse(frame, total);

        bool ok = s.find("\"msg_type\":\"semi_auto_jam\"") != std::string::npos
               && s.find("\"num_emitters\":1") != std::string::npos
               && s.find("\"emitter_number\":15") != std::string::npos
               && s.find("\"jam_duration_sec\":20") != std::string::npos
               && s.find("\"jpro\"") == std::string::npos; // Jam Command carries no JPRO
        if (ok) TEST_PASS(name); else TEST_FAIL(name, "JSON content unexpected");
    } catch (const std::exception& e) { TEST_FAIL(name, e.what()); }
}

static void test_extract_variant_a_load_warner() {
    const char* name = "extract_variant_a_load_warner";
    try {
        // Body: NewSetFlag(1B)+NumRecords(2B)+1 record (226B)
        static uint8_t body[3 + 226] = {};
        body[0] = 1;                 // new set
        store_be16(body + 1, 1);     // 1 record
        uint8_t* rec = body + 3;
        store_be16(rec + 0, 12);     // RecordNumber = 12
        store_be32(rec + 2, 99);     // CentralRadarDBNumber
        store_be32(rec + 6, 3000000);// Frequency KHz
        // RadarName at offset 67, 8 bytes
        std::memcpy(rec + 67, "RDR-ABC1", 8);

        int total;
        const uint8_t* frame = make_variant_a(0x1003, 1, body, sizeof(body), &total);
        std::string s = extract_and_parse(frame, total);

        bool ok = s.find("\"msg_type\":\"load_warner_library\"") != std::string::npos
               && s.find("\"new_set\":true") != std::string::npos
               && s.find("\"num_records\":1") != std::string::npos
               && s.find("\"record_no\":12") != std::string::npos
               && s.find("\"radar_name\":\"RDR-ABC1\"") != std::string::npos;
        if (ok) TEST_PASS(name); else TEST_FAIL(name, "JSON content unexpected");
    } catch (const std::exception& e) { TEST_FAIL(name, e.what()); }
}

static void test_extract_variant_b_sfb_selection() {
    const char* name = "extract_variant_b_sfb_selection";
    try {
        uint8_t body[17] = {};
        body[0] = 2; // NumBands
        store_be32(body + 1, 2000);
        store_be32(body + 5, 6000);
        store_be32(body + 9, 6000);
        store_be32(body + 13, 18000);

        int total;
        const uint8_t* frame = make_variant_b(0x1126, body, 17, &total);
        std::string s = extract_and_parse(frame, total);

        bool ok = s.find("\"frame_variant\":2") != std::string::npos
               && s.find("\"msg_type\":\"bb_sfb_selection\"") != std::string::npos
               && s.find("\"num_bands\":2") != std::string::npos;
        if (ok) TEST_PASS(name); else TEST_FAIL(name, "JSON content unexpected");
    } catch (const std::exception& e) { TEST_FAIL(name, e.what()); }
}

static void test_extract_variant_b_iq_data_logging() {
    const char* name = "extract_variant_b_iq_data_logging";
    try {
        // Body per IRS §5.8.1, 9 bytes.
        uint8_t body[9] = {};
        store_be16(body + 0, 8000); // TrackFrequency MHz
        body[2] = 10;               // RFAttenuation dB
        body[3] = 5;                // IFAttenuation dB
        body[4] = 60;               // Threshold (raw 60 -> -60 dB)
        body[5] = 1;                // IFBWSelection
        body[6] = 2;                // Sector
        store_be16(body + 7, 500);  // NumOfPulses

        int total;
        const uint8_t* frame = make_variant_b(0x1009, body, 9, &total);
        std::string s = extract_and_parse(frame, total);

        bool ok = s.find("\"msg_type\":\"rfps_iq_data_logging\"") != std::string::npos
               && s.find("\"track_frequency_mhz\":8000") != std::string::npos
               && s.find("\"num_of_pulses\":500") != std::string::npos;
        if (ok) TEST_PASS(name); else TEST_FAIL(name, "JSON content unexpected");
    } catch (const std::exception& e) { TEST_FAIL(name, e.what()); }
}

static void test_extract_variant_c_position_slew() {
    const char* name = "extract_variant_c_position_slew";
    try {
        // Position Slew now carries a single BAM-encoded PositionAngle field.
        // raw=16384 -> 16384 * 360/65536 = 90.0 deg
        uint8_t data[2] = {};
        store_be16(data + 0, 16384);

        int total;
        const uint8_t* frame = make_variant_c(0x01, data, 2, &total);
        std::string s = extract_and_parse(frame, total);

        bool ok = s.find("\"frame_variant\":3") != std::string::npos
               && s.find("\"msg_type\":\"scu_position_slew\"") != std::string::npos
               && s.find("\"position_angle_deg\":90") != std::string::npos
               && s.find("elevation") == std::string::npos; // no elevation field
        if (ok) TEST_PASS(name); else TEST_FAIL(name, "JSON content unexpected");
    } catch (const std::exception& e) { TEST_FAIL(name, e.what()); }
}

static void test_extract_variant_c_spin() {
    const char* name = "extract_variant_c_spin";
    try {
        uint8_t data[2] = { 45, 0x00 }; // Speed=45rpm, Direction=CW
        int total;
        const uint8_t* frame = make_variant_c(0x02, data, 2, &total);
        std::string s = extract_and_parse(frame, total);

        bool ok = s.find("\"msg_type\":\"scu_spin\"") != std::string::npos
               && s.find("\"speed_rpm\":45") != std::string::npos
               && s.find("\"direction\":\"cw\"") != std::string::npos;
        if (ok) TEST_PASS(name); else TEST_FAIL(name, "JSON content unexpected");
    } catch (const std::exception& e) { TEST_FAIL(name, e.what()); }
}

static void test_extract_variant_c_continuous_feedback() {
    const char* name = "extract_variant_c_continuous_feedback";
    try {
        uint8_t data[4] = {};
        data[0] = 0xC7; // bits: checksum(1)+received(1)+ready(1) + status(11=auto) in top 2 bits
        data[1] = 0x03; // encoder ok + amplifier ok
        store_be16(data + 2, 16384); // azimuth = 90.0 deg (BAM)

        int total;
        const uint8_t* frame = make_variant_c(0xA0, data, 4, &total);
        std::string s = extract_and_parse(frame, total);

        bool ok = s.find("\"msg_type\":\"scu_continuous_feedback\"") != std::string::npos
               && s.find("\"current_scu_status\":\"auto_mode\"") != std::string::npos
               && s.find("\"servo_encoder_ok\":true") != std::string::npos
               && s.find("\"azimuth_deg\":90") != std::string::npos
               && s.find("elevation") == std::string::npos;
        if (ok) TEST_PASS(name); else TEST_FAIL(name, "JSON content unexpected");
    } catch (const std::exception& e) { TEST_FAIL(name, e.what()); }
}

static void test_extract_variant_c_bad_checksum() {
    const char* name = "extract_variant_c_bad_checksum";
    uint8_t data[2] = {0x01, 0x02};
    int total;
    const uint8_t* frame = make_variant_c(0x01, data, 2, &total);

    static uint8_t corrupt[256];
    std::memcpy(corrupt, frame, static_cast<size_t>(total));
    corrupt[total - 2] ^= 0xFF; // corrupt checksum byte

    uint8_t* out_frame = nullptr;
    size_t   out_len = 0;
    int r = extract_frame(corrupt, static_cast<size_t>(total), &out_frame, &out_len);
    free_result(out_frame);
    if (r != -1) { TEST_FAIL(name, "expected -1 for bad checksum"); return; }
    TEST_PASS(name);
}

static void test_extract_variant_d_gprmc() {
    const char* name = "extract_variant_d_gprmc";
    try {
        const char* nmea = "$GPRMC,083559.00,A,2000.00,N,07830.00,E,0.5,089.9,010124,,,A*61\r\n";
        int len = static_cast<int>(std::strlen(nmea));
        std::string s = extract_and_parse(reinterpret_cast<const uint8_t*>(nmea), len);

        bool ok = s.find("\"frame_variant\":4") != std::string::npos
               && s.find("\"msg_type\":\"gnss_rmc\"") != std::string::npos
               && s.find("\"sentence_type\":\"GPRMC\"") != std::string::npos;
        if (ok) TEST_PASS(name); else TEST_FAIL(name, "JSON content unexpected");
    } catch (const std::exception& e) { TEST_FAIL(name, e.what()); }
}

static void test_extract_variant_d_gphdt() {
    const char* name = "extract_variant_d_gphdt";
    try {
        const char* nmea = "$GPHDT,274.5,T*CB\r\n";
        int len = static_cast<int>(std::strlen(nmea));
        std::string s = extract_and_parse(reinterpret_cast<const uint8_t*>(nmea), len);

        bool ok = s.find("\"msg_type\":\"gnss_hdt\"") != std::string::npos
               && s.find("\"heading_deg_true\":274") != std::string::npos;
        if (ok) TEST_PASS(name); else TEST_FAIL(name, "JSON content unexpected");
    } catch (const std::exception& e) { TEST_FAIL(name, e.what()); }
}

static void test_format_response_ack() {
    const char* name = "format_response_ack";
    const char* json = "{\"cmd_code\":0x150A,\"seq_no\":3}";
    uint8_t* out_buf = nullptr;
    size_t   out_len = 0;
    int r = format_response(nullptr, json, &out_buf, &out_len);
    if (r != 0 || !out_buf) { TEST_FAIL(name, "format_response failed"); free_result(out_buf); return; }
    if (out_len != 12) { TEST_FAIL(name, "wrong frame length for empty-body ACK"); free_result(out_buf); return; }

    uint16_t som = load_be16(out_buf);
    uint16_t cmd = load_be16(out_buf + 2);
    uint16_t eom = load_be16(out_buf + 10);
    free_result(out_buf);

    bool ok = (som == 0xAAAAu) && (cmd == 0x150Au) && (eom == 0xEEEEu);
    if (ok) TEST_PASS(name); else TEST_FAIL(name, "frame content mismatch");
}

static void test_format_response_with_payload() {
    const char* name = "format_response_with_payload";
    // Restart (0x100F) with no body
    const char* json = "{\"cmd_code\":4111,\"seq_no\":0}"; // 4111 = 0x100F
    uint8_t* out_buf = nullptr;
    size_t   out_len = 0;
    int r = format_response(nullptr, json, &out_buf, &out_len);
    if (r != 0 || out_len != 12) { TEST_FAIL(name, "wrong frame length"); free_result(out_buf); return; }
    uint16_t cmd = load_be16(out_buf + 2);
    free_result(out_buf);
    if (cmd == 0x100Fu) TEST_PASS(name); else TEST_FAIL(name, "cmd_code mismatch");
}

static void test_incomplete_variant_a() {
    const char* name = "incomplete_variant_a";
    uint8_t body[4] = {0xAB, 0xCD, 0xEF, 0x01};
    int total;
    const uint8_t* frame = make_variant_a(0x1014, 0, body, 4, &total);

    uint8_t* out_frame = nullptr;
    size_t   out_len = 0;
    // Feed only partial frame (first 8 bytes)
    int r = extract_frame(frame, 8, &out_frame, &out_len);
    free_result(out_frame);
    if (r == -1) TEST_PASS(name); else TEST_FAIL(name, "expected -1 for incomplete frame");
}

static void test_corrupt_variant_a_eom() {
    const char* name = "corrupt_variant_a_eom";
    uint8_t body[2] = {0x00, 0x05};
    int total;
    const uint8_t* frame = make_variant_a(0x1010, 0, body, 2, &total);

    static uint8_t corrupt[64];
    std::memcpy(corrupt, frame, static_cast<size_t>(total));
    corrupt[total - 1] ^= 0xFF; // corrupt EOM

    uint8_t* out_frame = nullptr;
    size_t   out_len = 0;
    int r = extract_frame(corrupt, static_cast<size_t>(total), &out_frame, &out_len);
    free_result(out_frame);
    if (r == -1) TEST_PASS(name); else TEST_FAIL(name, "expected -1 for corrupt EOM");
}

static void test_purge_passive_purge_all() {
    const char* name = "purge_passive_purge_all";
    try {
        uint8_t body[2] = {}; // NumberOfTracks = 0 -> purge all
        int total;
        const uint8_t* frame = make_variant_a(0x1010, 0, body, 2, &total);
        std::string s = extract_and_parse(frame, total);

        bool ok = s.find("\"msg_type\":\"purge_passive_track\"") != std::string::npos
               && s.find("\"action\":\"purge_all\"") != std::string::npos;
        if (ok) TEST_PASS(name); else TEST_FAIL(name, "JSON content unexpected");
    } catch (const std::exception& e) { TEST_FAIL(name, e.what()); }
}

static void test_free_null() {
    const char* name = "free_null";
    free_result(nullptr); // must not crash
    TEST_PASS(name);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    std::printf("=== RSEC Parser Unit Tests ===\n");

    test_extract_variant_a_active_track();
    test_extract_variant_a_operational_data();
    test_extract_variant_a_ack_nack();
    test_extract_variant_a_track_and_jam();
    test_extract_variant_a_jam_command();
    test_extract_variant_a_load_warner();
    test_extract_variant_b_sfb_selection();
    test_extract_variant_b_iq_data_logging();
    test_extract_variant_c_position_slew();
    test_extract_variant_c_spin();
    test_extract_variant_c_continuous_feedback();
    test_extract_variant_c_bad_checksum();
    test_extract_variant_d_gprmc();
    test_extract_variant_d_gphdt();
    test_format_response_ack();
    test_format_response_with_payload();
    test_incomplete_variant_a();
    test_corrupt_variant_a_eom();
    test_purge_passive_purge_all();
    test_free_null();

    std::printf("\nResults: %d passed, %d failed\n", pass_count, fail_count);
    return fail_count == 0 ? 0 : 1;
}
