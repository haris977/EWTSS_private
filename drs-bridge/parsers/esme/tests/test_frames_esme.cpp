// drs-bridge/parsers/esme/tests/test_frames_esme.cpp
//
// Unit tests for esme_parser_monvuhf.cpp — no external test framework.
// Exercises extract_frame, parse_message, format_response, and free_result
// across all four frame channels (EB200, AMMOS, SCPI command, SCPI response).

#include "sdfc_abi.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Frame builder helpers
// ---------------------------------------------------------------------------

static void be8 (std::vector<uint8_t>& v, uint8_t  x) { v.push_back(x); }
static void be16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
    v.push_back(static_cast<uint8_t>( x        & 0xFF));
}
static void be32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >>  8) & 0xFF));
    v.push_back(static_cast<uint8_t>( x         & 0xFF));
}
static void be64(std::vector<uint8_t>& v, uint64_t x) {
    be32(v, static_cast<uint32_t>(x >> 32));
    be32(v, static_cast<uint32_t>(x & 0xFFFFFFFFu));
}

static void le32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>( x        & 0xFF));
    v.push_back(static_cast<uint8_t>((x >>  8) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
}
static void le64(std::vector<uint8_t>& v, uint64_t x) {
    le32(v, static_cast<uint32_t>(x & 0xFFFFFFFFu));
    le32(v, static_cast<uint32_t>(x >> 32));
}

static void pad(std::vector<uint8_t>& v, int n) {
    for (int i = 0; i < n; ++i) v.push_back(0);
}

// ---------------------------------------------------------------------------
// Build a minimal conventional TraceAttribute block
// ---------------------------------------------------------------------------

static std::vector<uint8_t> make_conv_ta(int16_t  n_items,
                                          uint32_t sel_flags,
                                          const std::vector<uint8_t>& opt_hdr,
                                          const std::vector<uint8_t>& periodic)
{
    std::vector<uint8_t> ta;
    be16(ta, static_cast<uint16_t>(n_items));
    be8 (ta, 0);                                    // ChannelNumber
    be8 (ta, static_cast<uint8_t>(opt_hdr.size())); // OptionalHeaderLength
    be32(ta, sel_flags);
    ta.insert(ta.end(), opt_hdr.begin(), opt_hdr.end());
    ta.insert(ta.end(), periodic.begin(), periodic.end());
    return ta;
}

// ---------------------------------------------------------------------------
// Build a complete EB200 datagram wrapping a conventional GenericAttribute
// ---------------------------------------------------------------------------

static std::vector<uint8_t> make_eb200_frame(uint16_t trace_tag,
                                              const std::vector<uint8_t>& ta_bytes)
{
    std::vector<uint8_t> pkt;
    uint32_t data_size = 16u + 4u + static_cast<uint32_t>(ta_bytes.size());
    be32(pkt, 0x000EB200u);         // MagicWord
    be16(pkt, 0x70);                // VersionMinor
    be16(pkt, 0x02);                // VersionMajor
    be16(pkt, 0x0001);              // SequenceNumber_low
    be16(pkt, 0x0000);              // SequenceNumber_high
    be32(pkt, data_size);           // DataSize
    // GenericAttribute conventional header: Tag + Length
    be16(pkt, trace_tag);
    be16(pkt, static_cast<uint16_t>(ta_bytes.size()));
    pkt.insert(pkt.end(), ta_bytes.begin(), ta_bytes.end());
    return pkt;
}

// ---------------------------------------------------------------------------
// Build a minimal AMMOS AIF frame header (no actual IQ data)
// ---------------------------------------------------------------------------

static std::vector<uint8_t> make_ammos_frame(uint32_t freq_hz) {
    std::vector<uint8_t> pkt;

    // Frame header: 6 × 32-bit words little-endian (Table 7-40)
    uint32_t data_hdr_words = 19u;
    uint32_t db_hdr_words   = 1u;
    uint32_t db_data_words  = 0u;  // no IQ payload in test
    uint32_t total_words    = 6u + data_hdr_words + db_hdr_words + db_data_words;

    le32(pkt, 0xFB746572u);         // MagicWord LE
    le32(pkt, total_words);         // FrameLength [32-bit units]
    le32(pkt, 1u);                  // uintFrameCount
    le32(pkt, 0x01u);               // uintFrameType (0x01 = 2×32-bit I/Q)
    le32(pkt, data_hdr_words);      // uintDataHeaderLength = 19
    le32(pkt, 0u);                  // uintReserved

    // Data header: 19 × 32-bit words little-endian (Table 7-41)
    le32(pkt, 1u);                  // uintDatablockCount
    le32(pkt, db_data_words);       // uintDatablockLength
    le64(pkt, 1700000000000000ull); // uintTimeStamp [µs] (test value)
    le32(pkt, 0u);                  // uintStatusword
    le32(pkt, 0u);                  // uintSignalSourceID
    le32(pkt, 0u);                  // uintSignalSourceState
    le32(pkt, freq_hz);             // uintFrequencyLow
    le32(pkt, 0u);                  // uintFrequencyHigh
    le32(pkt, 25000000u);           // uintBandwidth = 25 MHz
    le32(pkt, 50000000u);           // uintSamplerate = 50 MHz
    le32(pkt, 1u);                  // uintInterpolation
    le32(pkt, 1u);                  // uintDecimation
    le32(pkt, static_cast<uint32_t>(-200));  // intAntennaVoltageRef (-200 = -20.0 dBµV)
    le64(pkt, 1700000000000000ull); // uintStartTimeStamp [µs]
    // 3 remaining data-header words to reach 19 total
    le32(pkt, 0u); le32(pkt, 0u); le32(pkt, 0u);

    // Data block header: 1 × 32-bit word
    le32(pkt, 0u);                  // DatablockStatus

    return pkt;
}

// ---------------------------------------------------------------------------
// Test 1: EB200 DFPan frame — direction-finding trace (critical RDFS path)
// Encodes a DFPan trace with AZIMUTH (bearing = 123.4°) and DF_LEVEL.
// ---------------------------------------------------------------------------

static void test_eb200_dfpan() {
    // SEL_DF_LEVEL (0x800) + SEL_AZIMUTH (0x1000) + SEL_OPTIONAL_HEADER (0x80000000)
    static constexpr uint32_t SEL = 0x80001800u;

    // Minimal DFPan optional header: 96-byte baseline (Freq+FreqSpan+DF config+GPS+StepFreq)
    std::vector<uint8_t> opt;
    be32(opt, 400000000u);   // Freq_low  = 400 MHz
    be32(opt, 0u);           // Freq_high = 0   → center = 400 MHz
    be32(opt, 25000000u);    // FreqSpan  = 25 MHz
    be32(opt, 0u);           // DFThresholdMode = OFF
    be32(opt, static_cast<uint32_t>(-50));  // DFThresholdValue = -50 dBµV (NINF-like)
    be32(opt, 25000u);       // DFBandwidth = 25 kHz
    be32(opt, 25000u);       // StepWidth   = 25 kHz
    be32(opt, 2000u);        // DFMeasureTime = 2000 µs
    be32(opt, 1u);           // DFOption: bit0=1 → DF possible
    be16(opt, 450u);         // CompassHeading = 45.0°
    be16(opt, 3u);           // CompassHeadingType = TRUE_NORTH
    be32(opt, 0u);           // AntennaFactor
    be32(opt, 0u);           // DemodFreqChannel
    be32(opt, 0u);           // DemodFreq_low
    be32(opt, 0u);           // DemodFreq_high
    be64(opt, 0u);           // OutputTimestamp
    pad(opt, 24);            // GPSHeader (zeroed)
    be32(opt, 1u);           // StepFreqNumerator = 1
    be32(opt, 1u);           // StepFreqDenominator = 1
    assert(opt.size() == 96u);

    // PeriodicTraceData: 1 item, DF_LEVEL then AZIMUTH (ascending flag order)
    std::vector<uint8_t> pd;
    be16(pd, static_cast<uint16_t>(-200));  // DF_LEVEL item[0] = -20.0 dBµV (raw 1/10)
    be16(pd, static_cast<uint16_t>(1234));  // AZIMUTH item[0]  = 123.4° (raw 1/10)

    auto ta  = make_conv_ta(1, SEL, opt, pd);
    auto pkt = make_eb200_frame(1401, ta);

    uint8_t* frame    = nullptr;
    size_t   frame_len = 0;
    int ftype = extract_frame(pkt.data(), pkt.size(), &frame, &frame_len);
    assert(ftype == 0);
    assert(frame != nullptr);
    assert(frame_len == pkt.size());

    char*  json     = nullptr;
    size_t json_len = 0;
    int pm = parse_message(frame, frame_len, &json, &json_len);
    assert(pm == 0);
    assert(json != nullptr);
    std::string s(json);
    assert(s.find("\"tag_name\":\"dfpan\"") != std::string::npos);
    assert(s.find("\"df_center_freq_hz\":400000000") != std::string::npos);
    assert(s.find("\"df_option\":1") != std::string::npos);
    assert(s.find("\"azimuth_01deg\":1234") != std::string::npos);  // 123.4°
    assert(s.find("\"is_dfpan\":true") != std::string::npos);
    free_result(json);
    free_result(frame);
    std::printf("PASS  test_eb200_dfpan\n");
}

// ---------------------------------------------------------------------------
// Test 2: EB200 FScan frame — frequency scan trace
// ---------------------------------------------------------------------------

static void test_eb200_fscan() {
    static constexpr uint32_t SEL = 0x80000001u;  // LEVEL + OPTIONAL_HEADER

    // Minimal FScan optional header: 40-byte subset (just up to OutputTimestamp)
    std::vector<uint8_t> opt;
    be16(opt,  1u);               // CycleCount = 1
    be16(opt,  0u);               // HoldTime   = 0
    be16(opt,  0u);               // DwellTime  = 0
    be16(opt,  1u);               // DirectionUp = increasing
    be16(opt,  0u);               // StopSignal = off
    be32(opt,  88000000u);        // StartFreq_low = 88 MHz
    be32(opt, 108000000u);        // StopFreq_low  = 108 MHz
    be32(opt,    100000u);        // StepFreq = 100 kHz
    be32(opt,  0u);               // StartFreq_high = 0
    be32(opt,  0u);               // StopFreq_high  = 0
    pad(opt, 2);                  // reserved[2]
    be64(opt,  0u);               // OutputTimestamp
    assert(opt.size() == 40u);

    // PeriodicTraceData: 2 items, LEVEL only
    std::vector<uint8_t> pd;
    be16(pd, static_cast<uint16_t>(200));  // level[0] = 20.0 dBµV
    be16(pd, static_cast<uint16_t>(210));  // level[1] = 21.0 dBµV

    auto ta  = make_conv_ta(2, SEL, opt, pd);
    auto pkt = make_eb200_frame(101, ta);  // TAG_FSCAN = 101

    uint8_t* frame    = nullptr;
    size_t   frame_len = 0;
    int ftype = extract_frame(pkt.data(), pkt.size(), &frame, &frame_len);
    assert(ftype == 0);

    char*  json     = nullptr;
    size_t json_len = 0;
    assert(parse_message(frame, frame_len, &json, &json_len) == 0);
    assert(json != nullptr);
    std::string s(json);
    assert(s.find("\"tag_name\":\"fscan\"") != std::string::npos);
    assert(s.find("\"start_freq_hz\":88000000") != std::string::npos);
    assert(s.find("\"level_01dbuv\":200") != std::string::npos);
    free_result(json);
    free_result(frame);
    std::printf("PASS  test_eb200_fscan\n");
}

// ---------------------------------------------------------------------------
// Test 3: AMMOS AIF frame — I/Q baseband
// ---------------------------------------------------------------------------

static void test_ammos_aif() {
    auto pkt = make_ammos_frame(450000000u);  // center 450 MHz

    uint8_t* frame    = nullptr;
    size_t   frame_len = 0;
    int ftype = extract_frame(pkt.data(), pkt.size(), &frame, &frame_len);
    assert(ftype == 0);
    assert(frame_len == pkt.size());

    char*  json     = nullptr;
    size_t json_len = 0;
    assert(parse_message(frame, frame_len, &json, &json_len) == 0);
    assert(json != nullptr);
    std::string s(json);
    assert(s.find("\"stream\":\"ammos_aif\"") != std::string::npos);
    assert(s.find("\"freq_hz\":450000000") != std::string::npos);
    assert(s.find("\"samplerate_hz\":50000000") != std::string::npos);
    free_result(json);
    free_result(frame);
    std::printf("PASS  test_ammos_aif\n");
}

// ---------------------------------------------------------------------------
// Test 4: SCPI command line (bridge → ESME, TCP 5555)
// ---------------------------------------------------------------------------

static void test_scpi_command() {
    const char* line = "TRAC:UDP:TAG \"192.168.1.10\",17222,\"DFPan\"\n";

    uint8_t* frame    = nullptr;
    size_t   frame_len = 0;
    int ftype = extract_frame(reinterpret_cast<const uint8_t*>(line),
                              std::strlen(line), &frame, &frame_len);
    assert(ftype == 0);

    char*  json     = nullptr;
    size_t json_len = 0;
    assert(parse_message(frame, frame_len, &json, &json_len) == 0);
    assert(json != nullptr);
    std::string s(json);
    assert(s.find("\"msg_kind\":\"command\"") != std::string::npos);
    assert(s.find("TRAC:UDP:TAG") != std::string::npos);
    free_result(json);
    free_result(frame);
    std::printf("PASS  test_scpi_command\n");
}

// ---------------------------------------------------------------------------
// Test 5: SCPI numeric response (ESME → bridge)
// ---------------------------------------------------------------------------

static void test_scpi_response() {
    const char* line = "1\n";

    uint8_t* frame    = nullptr;
    size_t   frame_len = 0;
    int ftype = extract_frame(reinterpret_cast<const uint8_t*>(line),
                              std::strlen(line), &frame, &frame_len);
    assert(ftype == 0);

    char*  json     = nullptr;
    size_t json_len = 0;
    assert(parse_message(frame, frame_len, &json, &json_len) == 0);
    assert(json != nullptr);
    std::string s(json);
    assert(s.find("\"msg_kind\":\"response\"") != std::string::npos);
    free_result(json);
    free_result(frame);
    std::printf("PASS  test_scpi_response\n");
}

// ---------------------------------------------------------------------------
// Test 6: format_response — build SCPI command wire bytes
// ---------------------------------------------------------------------------

static void test_format_response() {
    const char* kwargs =
        "{\"scpi_cmd\":\"TRAC:UDP:FLAG \\\"192.168.1.10\\\",17222,\\\"OPT\\\",\\\"AZI\\\"\"}";

    uint8_t* out    = nullptr;
    size_t out_len  = 0;
    int r = format_response(nullptr, kwargs, &out, &out_len);
    assert(r == 0);
    assert(out != nullptr);
    assert(out_len > 0);
    assert(out[out_len - 1] == '\n');

    std::string s(reinterpret_cast<const char*>(out), out_len);
    assert(s.find("TRAC:UDP:FLAG") != std::string::npos);
    assert(s.find("AZI") != std::string::npos);
    free_result(out);
    std::printf("PASS  test_format_response\n");
}

// ---------------------------------------------------------------------------
// Test 7: incomplete EB200 datagram returns -1 (need more bytes)
// ---------------------------------------------------------------------------

static void test_incomplete_eb200() {
    // Announce DataSize=100 but only provide 16-byte header
    std::vector<uint8_t> buf;
    be32(buf, 0x000EB200u);
    be16(buf, 0x70); be16(buf, 0x02);
    be16(buf, 1); be16(buf, 0);
    be32(buf, 100u);  // DataSize=100

    uint8_t* out    = nullptr;
    size_t out_len  = 0;
    int ftype = extract_frame(buf.data(), buf.size(), &out, &out_len);
    assert(ftype == -1);  // incomplete → -1 in new ABI
    std::printf("PASS  test_incomplete_eb200\n");
}

// ---------------------------------------------------------------------------
// Test 8: corrupt EB200 — DataSize < header size → returns -1
// ---------------------------------------------------------------------------

static void test_corrupt_eb200() {
    std::vector<uint8_t> buf;
    be32(buf, 0x000EB200u);
    be16(buf, 0x70); be16(buf, 0x02);
    be16(buf, 1); be16(buf, 0);
    be32(buf, 4u);  // DataSize=4 < 16 (header) → corrupt

    uint8_t* out    = nullptr;
    size_t out_len  = 0;
    int ftype = extract_frame(buf.data(), buf.size(), &out, &out_len);
    assert(ftype == -1);
    std::printf("PASS  test_corrupt_eb200\n");
}

// ---------------------------------------------------------------------------
// Test 9: free_result(nullptr) must not crash
// ---------------------------------------------------------------------------

static void test_free_null() {
    free_result(nullptr);
    std::printf("PASS  test_free_null\n");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    std::printf("=== esme_monvuhf parser tests ===\n");
    test_eb200_dfpan();
    test_eb200_fscan();
    test_ammos_aif();
    test_scpi_command();
    test_scpi_response();
    test_format_response();
    test_incomplete_eb200();
    test_corrupt_eb200();
    test_free_null();
    std::printf("All tests passed.\n");
    return 0;
}
