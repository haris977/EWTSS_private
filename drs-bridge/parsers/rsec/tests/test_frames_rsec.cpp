// drs-bridge/parsers/rsec/tests/test_frames_rsec.cpp
//
// Unit tests for the HIMSHAKTI RSEC parser DLL.
// No external test framework — plain assert() + stderr reporting.
// Build and run via CMake: cmake --build build && ctest --build build

#include "sdfc_abi.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static uint8_t g_out_frame[MAX_FRAME_BUFFER_BYTES];
static int     g_out_len = 0;

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
// Returns pointer to static buffer and sets *len
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

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void test_extract_variant_a_active_track() {
    const char* name = "extract_variant_a_active_track";

    // Build body for 0x1502 with 1 track
    uint8_t body[26] = {};
    body[0] = 1;     // SystemMode = operational
    body[1] = 1;     // NumTracks = 1
    // Track entry at body+2 (24 bytes)
    // TrackID = 42
    store_be16(body + 2, 42);
    // DOA = 0x0384 = 900 tenths → 90.0°
    store_be16(body + 4, 900);
    // Freq = 10000 KHz = 10 MHz
    store_be32(body + 6, 10000);
    // PRI = 1000 µs
    store_be32(body + 10, 1000);
    // PW = 5000 ns
    store_be32(body + 14, 5000);
    // ScanPeriod = 500 ms
    store_be32(body + 18, 500);
    // Amplitude = -40 dBm (stored as INT16 BE = 0xFFD8)
    store_be16(body + 22, static_cast<uint16_t>(-40));
    // Status = 0x01 (active)
    body[24] = 0x01;
    // EmitterCategory = 3
    body[25] = 3;

    int total;
    const uint8_t* frame = make_variant_a(0x1502, 1, body, 26, &total);

    int r = extract_frame(frame, total, g_out_frame, &g_out_len);
    if (r != 1) { TEST_FAIL(name, "extract_frame did not return 1"); return; }
    if (g_out_len != total) { TEST_FAIL(name, "frame length mismatch"); return; }

    const char* json = parse_message(g_out_frame, g_out_len, 1);
    if (!json) { TEST_FAIL(name, "parse_message returned null"); return; }

    std::string s(json);
    bool ok = s.find("\"msg_type\":\"active_track_data\"") != std::string::npos
           && s.find("\"num_tracks\":1") != std::string::npos
           && s.find("\"track_id\":42") != std::string::npos
           && s.find("\"doa_deg\":90") != std::string::npos
           && s.find("\"freq_mhz\":10") != std::string::npos;
    free_result(json);

    if (ok) TEST_PASS(name);
    else    TEST_FAIL(name, "JSON content unexpected");
}

static void test_extract_variant_a_operational_data() {
    const char* name = "extract_variant_a_operational_data";

    uint8_t body[8] = {};
    body[0] = 1; // ESMP operational
    body[1] = 3; // 3 active tracks
    body[2] = 0; // 0 manual tracks
    body[3] = 1; // scanning
    store_be16(body + 4, 2); // band index 2
    body[6] = 0; // hw_status ok

    int total;
    const uint8_t* frame = make_variant_a(0x1504, 5, body, 8, &total);

    int r = extract_frame(frame, total, g_out_frame, &g_out_len);
    if (r != 1) { TEST_FAIL(name, "extract_frame failed"); return; }

    const char* json = parse_message(g_out_frame, g_out_len, 1);
    if (!json) { TEST_FAIL(name, "parse_message null"); return; }

    std::string s(json);
    bool ok = s.find("\"msg_type\":\"operational_data\"") != std::string::npos
           && s.find("\"esmp_status\":\"operational\"") != std::string::npos
           && s.find("\"num_active_tracks\":3") != std::string::npos;
    free_result(json);

    if (ok) TEST_PASS(name);
    else    TEST_FAIL(name, "JSON content unexpected");
}

static void test_extract_variant_a_ack() {
    const char* name = "extract_variant_a_ack";

    uint8_t body[1] = {0};
    int total;
    const uint8_t* frame = make_variant_a(0x150A, 7, body, 1, &total);

    int r = extract_frame(frame, total, g_out_frame, &g_out_len);
    if (r != 1) { TEST_FAIL(name, "extract_frame failed"); return; }

    const char* json = parse_message(g_out_frame, g_out_len, 1);
    if (!json) { TEST_FAIL(name, "parse_message null"); return; }

    std::string s(json);
    bool ok = s.find("\"msg_type\":\"ack\"") != std::string::npos
           && s.find("\"seq_no\":7") != std::string::npos;
    free_result(json);

    if (ok) TEST_PASS(name);
    else    TEST_FAIL(name, "JSON content unexpected");
}

static void test_extract_variant_a_track_and_jam() {
    const char* name = "extract_variant_a_track_and_jam";

    // Body: TrackID(2B) + JPRO(24B)
    uint8_t body[26] = {};
    store_be16(body + 0, 77); // TrackID = 77
    // JPRO: drfm_mode=2, num_responses=1
    store_be16(body + 2, 2);   // DRFM mode
    body[4] = 1;               // num_responses
    // Technique 1: JammingType=3, Freq=94000 (9400.0 MHz), Params=0
    body[5] = 3;               // jamming type
    store_be32(body + 6, 94000); // 94000 raw → 9400.0 MHz
    store_be16(body + 10, 0);  // params

    int total;
    const uint8_t* frame = make_variant_a(0x1103, 2, body, 26, &total);

    int r = extract_frame(frame, total, g_out_frame, &g_out_len);
    if (r != 1) { TEST_FAIL(name, "extract_frame failed"); return; }

    const char* json = parse_message(g_out_frame, g_out_len, 1);
    if (!json) { TEST_FAIL(name, "parse_message null"); return; }

    std::string s(json);
    bool ok = s.find("\"msg_type\":\"semi_auto_track_jam\"") != std::string::npos
           && s.find("\"track_id\":77") != std::string::npos
           && s.find("\"drfm_mode\":2") != std::string::npos;
    free_result(json);

    if (ok) TEST_PASS(name);
    else    TEST_FAIL(name, "JSON content unexpected");
}

static void test_extract_variant_b_sfb_selection() {
    const char* name = "extract_variant_b_sfb_selection";

    // Body: NumBands=2, band1=[2000,6000 MHz], band2=[6000,18000 MHz]
    uint8_t body[17] = {};
    body[0] = 2; // NumBands
    store_be32(body + 1, 2000);
    store_be32(body + 5, 6000);
    store_be32(body + 9, 6000);
    store_be32(body + 13, 18000);

    int total;
    const uint8_t* frame = make_variant_b(0x1126, body, 17, &total);

    int r = extract_frame(frame, total, g_out_frame, &g_out_len);
    if (r != 2) { TEST_FAIL(name, "extract_frame did not return 2"); return; }

    const char* json = parse_message(g_out_frame, g_out_len, 2);
    if (!json) { TEST_FAIL(name, "parse_message null"); return; }

    std::string s(json);
    bool ok = s.find("\"frame_variant\":2") != std::string::npos
           && s.find("\"msg_type\":\"bb_sfb_selection\"") != std::string::npos
           && s.find("\"num_bands\":2") != std::string::npos;
    free_result(json);

    if (ok) TEST_PASS(name);
    else    TEST_FAIL(name, "JSON content unexpected");
}

static void test_extract_variant_c_position_slew() {
    const char* name = "extract_variant_c_position_slew";

    // Position Slew: Azimuth=0x0384=900 (90.0°), Elevation=0x012C=300 (30.0°)
    uint8_t data[4] = {};
    store_be16(data + 0, 900);  // 90.0°
    store_be16(data + 2, 300);  // 30.0°

    int total;
    const uint8_t* frame = make_variant_c(0x01, data, 4, &total);

    int r = extract_frame(frame, total, g_out_frame, &g_out_len);
    if (r != 3) { TEST_FAIL(name, "extract_frame did not return 3"); return; }

    const char* json = parse_message(g_out_frame, g_out_len, 3);
    if (!json) { TEST_FAIL(name, "parse_message null"); return; }

    std::string s(json);
    bool ok = s.find("\"frame_variant\":3") != std::string::npos
           && s.find("\"msg_type\":\"scu_position_slew\"") != std::string::npos
           && s.find("\"azimuth_deg\":90") != std::string::npos;
    free_result(json);

    if (ok) TEST_PASS(name);
    else    TEST_FAIL(name, "JSON content unexpected");
}

static void test_extract_variant_c_bad_checksum() {
    const char* name = "extract_variant_c_bad_checksum";

    uint8_t data[2] = {0x01, 0x02};
    int total;
    const uint8_t* frame = make_variant_c(0x01, data, 2, &total);

    // Corrupt the checksum byte (second-to-last byte)
    static uint8_t corrupt[256];
    std::memcpy(corrupt, frame, static_cast<size_t>(total));
    corrupt[total - 2] ^= 0xFF;

    int r = extract_frame(corrupt, total, g_out_frame, &g_out_len);
    if (r != -1) { TEST_FAIL(name, "expected -1 for bad checksum"); return; }

    TEST_PASS(name);
}

static void test_extract_variant_d_gprmc() {
    const char* name = "extract_variant_d_gprmc";

    const char* nmea = "$GPRMC,083559.00,A,2000.00,N,07830.00,E,0.5,089.9,010124,,,A*61\r\n";
    int len = static_cast<int>(std::strlen(nmea));

    int r = extract_frame(reinterpret_cast<const uint8_t*>(nmea), len,
                          g_out_frame, &g_out_len);
    if (r != 4) { TEST_FAIL(name, "extract_frame did not return 4"); return; }

    const char* json = parse_message(g_out_frame, g_out_len, 4);
    if (!json) { TEST_FAIL(name, "parse_message null"); return; }

    std::string s(json);
    bool ok = s.find("\"frame_variant\":4") != std::string::npos
           && s.find("\"msg_type\":\"gnss_rmc\"") != std::string::npos
           && s.find("\"sentence_type\":\"GPRMC\"") != std::string::npos;
    free_result(json);

    if (ok) TEST_PASS(name);
    else    TEST_FAIL(name, "JSON content unexpected");
}

static void test_extract_variant_d_gphdt() {
    const char* name = "extract_variant_d_gphdt";

    const char* nmea = "$GPHDT,274.5,T*CB\r\n";
    int len = static_cast<int>(std::strlen(nmea));

    int r = extract_frame(reinterpret_cast<const uint8_t*>(nmea), len,
                          g_out_frame, &g_out_len);
    if (r != 4) { TEST_FAIL(name, "extract_frame did not return 4"); return; }

    const char* json = parse_message(g_out_frame, g_out_len, 4);
    if (!json) { TEST_FAIL(name, "parse_message null"); return; }

    std::string s(json);
    bool ok = s.find("\"msg_type\":\"gnss_hdt\"") != std::string::npos
           && s.find("\"heading_deg_true\":274") != std::string::npos;
    free_result(json);

    if (ok) TEST_PASS(name);
    else    TEST_FAIL(name, "JSON content unexpected");
}

static void test_format_response_ack() {
    const char* name = "format_response_ack";

    const char* json = "{\"cmd_code\":0x150A,\"seq_no\":3}";
    int r = format_response(json, g_out_frame);
    if (r < 0) { TEST_FAIL(name, "format_response failed"); return; }
    if (r != 12) { TEST_FAIL(name, "wrong frame length for empty-body ACK"); return; }

    // Check SOM
    uint16_t som = load_be16(g_out_frame);
    if (som != 0xAAAAu) { TEST_FAIL(name, "SOM mismatch"); return; }

    // Check cmd_code
    uint16_t cmd = load_be16(g_out_frame + 2);
    if (cmd != 0x150Au) { TEST_FAIL(name, "cmd_code mismatch"); return; }

    // Check EOM
    uint16_t eom = load_be16(g_out_frame + 10);
    if (eom != 0xEEEEu) { TEST_FAIL(name, "EOM mismatch"); return; }

    TEST_PASS(name);
}

static void test_format_response_with_payload() {
    const char* name = "format_response_with_payload";

    // Restart (0x100F) with no body
    const char* json = "{\"cmd_code\":4111,\"seq_no\":0}"; // 4111 = 0x100F
    int r = format_response(json, g_out_frame);
    if (r != 12) { TEST_FAIL(name, "wrong frame length"); return; }
    uint16_t cmd = load_be16(g_out_frame + 2);
    if (cmd != 0x100Fu) { TEST_FAIL(name, "cmd_code mismatch"); return; }

    TEST_PASS(name);
}

static void test_incomplete_variant_a() {
    const char* name = "incomplete_variant_a";

    uint8_t body[4] = {0xAB, 0xCD, 0xEF, 0x01};
    int total;
    const uint8_t* frame = make_variant_a(0x1014, 0, body, 4, &total);

    // Feed only partial frame (first 8 bytes)
    int r = extract_frame(frame, 8, g_out_frame, &g_out_len);
    if (r != 0) { TEST_FAIL(name, "expected 0 for incomplete frame"); return; }

    TEST_PASS(name);
}

static void test_corrupt_variant_a_eom() {
    const char* name = "corrupt_variant_a_eom";

    uint8_t body[2] = {0x00, 0x05};
    int total;
    const uint8_t* frame = make_variant_a(0x1010, 0, body, 2, &total);

    static uint8_t corrupt[64];
    std::memcpy(corrupt, frame, static_cast<size_t>(total));
    // Corrupt EOM
    corrupt[total - 1] ^= 0xFF;

    int r = extract_frame(corrupt, total, g_out_frame, &g_out_len);
    if (r != -1) { TEST_FAIL(name, "expected -1 for corrupt EOM"); return; }

    TEST_PASS(name);
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
    test_extract_variant_a_ack();
    test_extract_variant_a_track_and_jam();
    test_extract_variant_b_sfb_selection();
    test_extract_variant_c_position_slew();
    test_extract_variant_c_bad_checksum();
    test_extract_variant_d_gprmc();
    test_extract_variant_d_gphdt();
    test_format_response_ack();
    test_format_response_with_payload();
    test_incomplete_variant_a();
    test_corrupt_variant_a_eom();
    test_free_null();

    std::printf("\nResults: %d passed, %d failed\n", pass_count, fail_count);
    return fail_count == 0 ? 0 : 1;
}
