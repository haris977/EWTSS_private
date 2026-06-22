// parsers/cpp/tests/comm_df/test_comm_df_parser.cpp
//
// Golden-frame unit tests for the COMM_DF parser.
// Uses GoogleTest. Build with: cmake --build build --target run_tests
//
// Golden frame strategy:
//   Each test fixture is a real binary frame (captured from hardware or
//   hand-crafted from the ICD). The CRC bytes are computed offline and
//   hardcoded. Replace TODO_CRC values with real CRC when hardware is available.
//
// Running tests validates:
//   1. extract_frame correctly identifies and malloc-allocates frames
//   2. parse_message produces correct SI-unit JSON fields
//   3. format_response produces a valid malloc-allocated binary response frame
//   4. free_result is safe to call (no crash, no double-free)
#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <string>

// Include parser source directly — avoids needing to load the .dll in tests.
// The test binary links the same .cpp file that the DLL compiles.
#include "../../comm_df/comm_df_parser.cpp"

// ─────────────────────────────────────────────────────────────────────────────
// CRC HELPER — compute CRC for golden frame construction
// ─────────────────────────────────────────────────────────────────────────────

// Builds a complete CMD frame with CRC computed at runtime.
// Use this in fixture construction so test frames always have valid CRCs.
static std::vector<uint8_t> make_cmd_frame(uint16_t group_id, uint16_t unit_id,
                                            const uint8_t* payload, int payload_len) {
    using namespace comm_df;
    int total = CMD_OVERHEAD + payload_len;
    std::vector<uint8_t> f(static_cast<std::size_t>(total), 0);

    std::memcpy(f.data(),      CMD_HEADER, 4);
    write_u32(f.data() + 4,    static_cast<uint32_t>(payload_len));
    write_u16(f.data() + 8,    group_id);
    write_u16(f.data() + 10,   unit_id);
    if (payload && payload_len > 0)
        std::memcpy(f.data() + 12, payload, static_cast<std::size_t>(payload_len));

    int crc_offset = total - 6;
    uint16_t crc   = crc16_ccitt(f.data() + 4, crc_offset - 4);
    write_u16(f.data() + crc_offset, crc);
    std::memcpy(f.data() + total - 4, CMD_FOOTER, 4);
    return f;
}

static std::vector<uint8_t> make_resp_frame(uint16_t group_id, uint16_t unit_id,
                                             uint16_t status,
                                             const uint8_t* payload, int payload_len) {
    using namespace comm_df;
    int total = RESP_OVERHEAD + payload_len;
    std::vector<uint8_t> f(static_cast<std::size_t>(total), 0);

    std::memcpy(f.data(),      RESP_HEADER, 4);
    write_u32(f.data() + 4,    static_cast<uint32_t>(payload_len));
    write_u16(f.data() + 8,    group_id);
    write_u16(f.data() + 10,   unit_id);
    write_u16(f.data() + 12,   status);
    if (payload && payload_len > 0)
        std::memcpy(f.data() + 14, payload, static_cast<std::size_t>(payload_len));

    int crc_offset = total - 6;
    uint16_t crc   = crc16_ccitt(f.data() + 4, crc_offset - 4);
    write_u16(f.data() + crc_offset, crc);
    std::memcpy(f.data() + total - 4, RESP_FOOTER, 4);
    return f;
}

// ─────────────────────────────────────────────────────────────────────────────
// extract_frame TESTS
// ─────────────────────────────────────────────────────────────────────────────

TEST(CommDfExtractFrame, CompleteCommandFrame_Returns0) {
    auto frame = make_cmd_frame(comm_df::GROUP_SYSTEM_MGMT,
                                comm_df::CMD_GET_SYSTEM_VERSION,
                                nullptr, 0);
    uint8_t* out    = nullptr;
    size_t out_len  = 0;

    int result = extract_frame(frame.data(), frame.size(), &out, &out_len);

    ASSERT_EQ(result, 0);
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(out_len, frame.size());
    EXPECT_EQ(std::memcmp(out, frame.data(), frame.size()), 0);
    free_result(out);
}

TEST(CommDfExtractFrame, CompleteResponseFrame_Returns0) {
    // RESP_SYSTEM_VERSION payload: fw_major=1 fw_minor=4 fw_patch=12 hw_rev=2 serial=12345
    uint8_t payload[11];
    write_u16(payload + 0, 1);      // fw_major
    write_u16(payload + 2, 4);      // fw_minor
    write_u16(payload + 4, 12);     // fw_patch
    write_u8 (payload + 6, 2);      // hw_revision
    write_u32(payload + 7, 12345);  // serial_number

    auto frame = make_resp_frame(comm_df::GROUP_SYSTEM_MGMT,
                                 comm_df::RESP_SYSTEM_VERSION,
                                 comm_df::STATUS_OK,
                                 payload, sizeof(payload));
    uint8_t* out   = nullptr;
    size_t out_len = 0;

    int result = extract_frame(frame.data(), frame.size(), &out, &out_len);

    ASSERT_EQ(result, 0);
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(out_len, frame.size());
    free_result(out);
}

TEST(CommDfExtractFrame, IncompleteFrame_ReturnsMinus1) {
    auto frame = make_cmd_frame(comm_df::GROUP_SYSTEM_MGMT,
                                comm_df::CMD_GET_SYSTEM_VERSION,
                                nullptr, 0);
    uint8_t* out   = nullptr;
    size_t out_len = 0;

    // Only provide 6 bytes — too few to determine payload length
    int result = extract_frame(frame.data(), 6, &out, &out_len);

    EXPECT_EQ(result, -1);
    EXPECT_EQ(out, nullptr);
}

TEST(CommDfExtractFrame, CorruptHeader_ReturnsMinus1) {
    auto frame = make_cmd_frame(comm_df::GROUP_SYSTEM_MGMT,
                                comm_df::CMD_GET_SYSTEM_VERSION,
                                nullptr, 0);
    frame[0] = 0xFF; // corrupt the first header byte

    uint8_t* out   = nullptr;
    size_t out_len = 0;

    int result = extract_frame(frame.data(), frame.size(), &out, &out_len);

    EXPECT_EQ(result, -1);
    EXPECT_EQ(out, nullptr);
}

TEST(CommDfExtractFrame, CorruptCRC_ReturnsMinus1) {
    auto frame = make_cmd_frame(comm_df::GROUP_SYSTEM_MGMT,
                                comm_df::CMD_GET_SYSTEM_VERSION,
                                nullptr, 0);
    // Corrupt the CRC bytes (4 bytes before the 4-byte footer)
    int crc_pos = (int)frame.size() - 6;
    frame[crc_pos]     ^= 0xFF;
    frame[crc_pos + 1] ^= 0xFF;

    uint8_t* out   = nullptr;
    size_t out_len = 0;

    int result = extract_frame(frame.data(), frame.size(), &out, &out_len);

    EXPECT_EQ(result, -1);
}

TEST(CommDfExtractFrame, NullBuffer_ReturnsMinus1) {
    uint8_t  buf[16] = {};
    uint8_t* out     = nullptr;
    size_t   out_len = 0;

    EXPECT_EQ(extract_frame(nullptr, 10, &out, &out_len), -1);
    EXPECT_EQ(extract_frame(buf, 10, nullptr, &out_len),  -1);
    EXPECT_EQ(extract_frame(buf, 10, &out, nullptr),      -1);
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON field helpers for parse_message tests
// ─────────────────────────────────────────────────────────────────────────────

static std::string extract_json_string(const char* json, const char* key) {
    std::string s(json);
    std::string k = std::string("\"") + key + "\":\"";
    auto pos = s.find(k);
    if (pos == std::string::npos) return "";
    pos += k.size();
    auto end = s.find('"', pos);
    return s.substr(pos, end - pos);
}

static double extract_json_double(const char* json, const char* key) {
    std::string s(json);
    std::string k = std::string("\"") + key + "\":";
    auto pos = s.find(k);
    if (pos == std::string::npos) return -999999.0;
    return std::stod(s.substr(pos + k.size()));
}

static int extract_json_int(const char* json, const char* key) {
    return static_cast<int>(extract_json_double(json, key));
}

// ─────────────────────────────────────────────────────────────────────────────
// parse_message TESTS
// ─────────────────────────────────────────────────────────────────────────────

TEST(CommDfParseMessage, SystemVersionResponse_CorrectFields) {
    uint8_t payload[11];
    write_u16(payload + 0, 2);       // fw_major = 2
    write_u16(payload + 2, 7);       // fw_minor = 7
    write_u16(payload + 4, 3);       // fw_patch = 3
    write_u8 (payload + 6, 1);       // hw_revision = 1
    write_u32(payload + 7, 98765);   // serial_number = 98765

    auto frame = make_resp_frame(comm_df::GROUP_SYSTEM_MGMT,
                                 comm_df::RESP_SYSTEM_VERSION,
                                 comm_df::STATUS_OK,
                                 payload, sizeof(payload));

    char*  json     = nullptr;
    size_t json_len = 0;
    ASSERT_EQ(parse_message(frame.data(), frame.size(), &json, &json_len), 0);
    ASSERT_NE(json, nullptr);

    EXPECT_EQ(extract_json_string(json, "frame_type"), "response");
    EXPECT_EQ(extract_json_int(json, "group_id"),    100);
    EXPECT_EQ(extract_json_int(json, "unit_id"),     2);
    EXPECT_EQ(extract_json_int(json, "status"),      0);
    EXPECT_EQ(extract_json_int(json, "fw_major"),    2);
    EXPECT_EQ(extract_json_int(json, "fw_minor"),    7);
    EXPECT_EQ(extract_json_int(json, "fw_patch"),    3);
    EXPECT_EQ(extract_json_int(json, "hw_revision"), 1);
    EXPECT_EQ(extract_json_int(json, "serial_number"), 98765);

    free_result(json);
}

TEST(CommDfParseMessage, ScanResult_FrequencyConvertedToHz) {
    // frequency = 102400 kHz = 102400000 Hz
    uint8_t payload[11];
    write_u32(payload + 0, 102400);  // frequency_khz
    write_u16(payload + 4, static_cast<uint16_t>(static_cast<int16_t>(-800)));
    write_u8 (payload + 6, 95);     // confidence
    write_u32(payload + 7, 123456); // timestamp_ms

    auto frame = make_resp_frame(comm_df::GROUP_MEASUREMENT,
                                 comm_df::RESP_SCAN_RESULT,
                                 comm_df::STATUS_OK,
                                 payload, sizeof(payload));

    char*  json     = nullptr;
    size_t json_len = 0;
    ASSERT_EQ(parse_message(frame.data(), frame.size(), &json, &json_len), 0);
    ASSERT_NE(json, nullptr);

    EXPECT_DOUBLE_EQ(extract_json_double(json, "frequency_hz"), 102400000.0);
    EXPECT_EQ(extract_json_int(json, "confidence_pct"), 95);
    EXPECT_EQ(extract_json_int(json, "timestamp_ms"), 123456);

    free_result(json);
}

TEST(CommDfParseMessage, AoaResult_DegreesConvertedFromTenths) {
    // azimuth = 2143 tenths = 214.3 degrees
    uint8_t payload[5];
    write_u16(payload + 0, 2143);
    write_u16(payload + 2, static_cast<uint16_t>(static_cast<int16_t>(31)));
    write_u8 (payload + 4, 88);

    auto frame = make_resp_frame(comm_df::GROUP_MEASUREMENT,
                                 comm_df::RESP_AOA_RESULT,
                                 comm_df::STATUS_OK,
                                 payload, sizeof(payload));

    char*  json     = nullptr;
    size_t json_len = 0;
    ASSERT_EQ(parse_message(frame.data(), frame.size(), &json, &json_len), 0);
    ASSERT_NE(json, nullptr);

    EXPECT_NEAR(extract_json_double(json, "azimuth_deg"),   214.3, 0.01);
    EXPECT_NEAR(extract_json_double(json, "elevation_deg"),   3.1, 0.01);
    EXPECT_EQ  (extract_json_int(json, "aoa_quality"), 88);

    free_result(json);
}

TEST(CommDfParseMessage, NullFrame_ReturnsMinus1) {
    char*  json     = nullptr;
    size_t json_len = 0;
    EXPECT_EQ(parse_message(nullptr, 0, &json, &json_len), -1);
    EXPECT_EQ(json, nullptr);
}

TEST(CommDfParseMessage, UnrecognisedMagic_ReturnsMinus1) {
    // Corrupt magic bytes so frame type cannot be inferred
    auto frame = make_cmd_frame(comm_df::GROUP_SYSTEM_MGMT,
                                comm_df::CMD_GET_SYSTEM_VERSION,
                                nullptr, 0);
    frame[0] = 0x00; frame[1] = 0x00; // wipe magic

    char*  json     = nullptr;
    size_t json_len = 0;
    EXPECT_EQ(parse_message(frame.data(), frame.size(), &json, &json_len), -1);
    EXPECT_EQ(json, nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// format_response TESTS
// ─────────────────────────────────────────────────────────────────────────────

TEST(CommDfFormatResponse, MinimalResponse_ProducesValidFrame) {
    const char* kwargs = R"({"group_id":101,"unit_id":2,"status":0})";
    uint8_t* out    = nullptr;
    size_t out_len  = 0;

    ASSERT_EQ(format_response(nullptr, kwargs, &out, &out_len), 0);
    ASSERT_NE(out, nullptr);
    ASSERT_GT(out_len, 0u);

    // Header must be DRS->SDFC response header
    EXPECT_EQ(out[0], 0xEE);
    EXPECT_EQ(out[1], 0xEF);
    EXPECT_EQ(out[2], 0xFE);
    EXPECT_EQ(out[3], 0xFF);

    // group_id at offset 8
    EXPECT_EQ(read_u16(out + 8), 101);
    // unit_id at offset 10
    EXPECT_EQ(read_u16(out + 10), 2);
    // status at offset 12
    EXPECT_EQ(read_u16(out + 12), 0);

    // Footer at end
    EXPECT_EQ(out[out_len - 4], 0xFF);
    EXPECT_EQ(out[out_len - 3], 0xFE);
    EXPECT_EQ(out[out_len - 2], 0xEF);
    EXPECT_EQ(out[out_len - 1], 0xEE);

    free_result(out);
}

TEST(CommDfFormatResponse, MissingField_ReturnsMinus1) {
    // Missing status field
    const char* kwargs = R"({"group_id":101,"unit_id":2})";
    uint8_t* out    = nullptr;
    size_t out_len  = 0;

    EXPECT_EQ(format_response(nullptr, kwargs, &out, &out_len), -1);
}

TEST(CommDfFormatResponse, NullInputs_ReturnsMinus1) {
    uint8_t* out   = nullptr;
    size_t out_len = 0;
    EXPECT_EQ(format_response(nullptr, nullptr, &out, &out_len), -1);
    EXPECT_EQ(format_response(nullptr, "{}", nullptr, nullptr),  -1);
}

// ─────────────────────────────────────────────────────────────────────────────
// free_result TESTS
// ─────────────────────────────────────────────────────────────────────────────

TEST(CommDfFreeResult, NullPointer_DoesNotCrash) {
    EXPECT_NO_THROW(free_result(nullptr));
}

TEST(CommDfFreeResult, ValidPointer_ReleasesMemory) {
    auto frame = make_resp_frame(comm_df::GROUP_SYSTEM_MGMT,
                                 comm_df::RESP_RESET,
                                 comm_df::STATUS_OK,
                                 nullptr, 0);

    char*  json     = nullptr;
    size_t json_len = 0;
    ASSERT_EQ(parse_message(frame.data(), frame.size(), &json, &json_len), 0);
    ASSERT_NE(json, nullptr);
    EXPECT_NO_THROW(free_result(json));
}
