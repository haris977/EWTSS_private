// drs-bridge/parsers/ca120/tests/test_frames_ca120.cpp
//
// Self-contained golden-frame tests for the CA120 parser (no test framework).
// Builds byte-exact frames, runs extract_frame -> parse_message, and round-trips
// format_response. Exits non-zero on any failure.
//
// ICD: R&S-CA120-ICD-V15

#include "sdfc_abi.h"
#include "sdfc_endian.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

using namespace sdfc;

// ---------------------------------------------------------------------------
// Test harness
// ---------------------------------------------------------------------------

static int g_failures = 0;

#define CHECK(cond, msg) \
    do { if (!(cond)) { std::printf("FAIL: %s\n", (msg)); ++g_failures; } \
         else          { std::printf("ok:   %s\n", (msg)); } } while (0)

static bool contains(const char* hay, const char* needle) {
    return hay && std::strstr(hay, needle) != nullptr;
}

// ---------------------------------------------------------------------------
// AMMOS frame builder helpers
// ---------------------------------------------------------------------------

// Stores a uint32 in little-endian into v at position pos.
static void le32(std::vector<uint8_t>& v, size_t pos, uint32_t val) {
    v[pos + 0] = uint8_t(val);
    v[pos + 1] = uint8_t(val >> 8);
    v[pos + 2] = uint8_t(val >> 16);
    v[pos + 3] = uint8_t(val >> 24);
}

// Build a minimal AMMOS frame:
//   Frame_Header (6 × uint32 = 24 bytes):
//     [0] Magic          = 0xFB746572
//     [1] FrameLength    = total_words = (24 + dh_extra) / 4
//     [2] FrameCount     = frame_count
//     [3] FrameType      = frame_type
//     [4] DataHeaderLength = dh_extra_bytes / 4 (extra words after the 6-word header)
//     [5] SignalGroup    = 0
//   Data_Header bytes  (dh_extra_bytes of zeros unless overridden by caller)
//   Payload            (pay_bytes of zeros)
static std::vector<uint8_t> build_ammos(
    uint32_t              frame_type,
    uint32_t              frame_count,
    const std::vector<uint8_t>& dh_bytes,   // Data_Header content (must be 4-byte aligned length)
    const std::vector<uint8_t>& payload = {})
{
    // total bytes = 24 (header) + dh_bytes.size() + payload.size()
    size_t total = 24u + dh_bytes.size() + payload.size();
    // pad to 4-byte boundary
    while (total % 4) total++;
    uint32_t frame_words = uint32_t(total / 4u);
    uint32_t dh_words    = uint32_t(dh_bytes.size() / 4u);

    std::vector<uint8_t> f(total, 0);
    le32(f, 0,  0xFB746572u);
    le32(f, 4,  frame_words);
    le32(f, 8,  frame_count);
    le32(f, 12, frame_type);
    le32(f, 16, dh_words);
    le32(f, 20, 0u);  // SignalGroup

    if (!dh_bytes.empty())
        memcpy(f.data() + 24, dh_bytes.data(), dh_bytes.size());
    if (!payload.empty())
        memcpy(f.data() + 24 + dh_bytes.size(), payload.data(), payload.size());

    return f;
}

// Build a 56-byte IF Data_Header (14 × uint32, §6.1)
static std::vector<uint8_t> make_if_dh(uint32_t ts_lo = 0x000186A0u,  // 100000 µs
                                        uint32_t ts_hi = 0u,
                                        uint32_t freq_lo = 0x00989680u, // 10 MHz
                                        uint32_t freq_hi = 0u,
                                        uint32_t bw    = 0x0017D784u,   // 1.56 MHz
                                        uint32_t sr    = 0x001E8480u)   // 2 MHz
{
    std::vector<uint8_t> dh(56, 0);
    le32(dh,  0, 1u);        // DatablockCount
    le32(dh,  4, 256u);      // DatablockLength (words)
    le32(dh,  8, ts_lo);
    le32(dh, 12, ts_hi);
    le32(dh, 16, 0u);        // StatusWord
    le32(dh, 20, 42u);       // SourceID
    le32(dh, 24, 1u);        // SourceState
    le32(dh, 28, freq_lo);
    le32(dh, 32, freq_hi);
    le32(dh, 36, bw);
    le32(dh, 40, sr);
    le32(dh, 44, 1u);        // Interpolation
    le32(dh, 48, 1u);        // Decimation
    le32(dh, 52, 1000u);     // AntVoltageRef
    return dh;
}

// Build a 44-byte Audio Data_Header (10 fields, §6.2)
static std::vector<uint8_t> make_audio_dh() {
    std::vector<uint8_t> dh(44, 0);
    le32(dh,  0, 48000u);    // SampleRate Hz
    le32(dh,  4, 0u);        // StatusWord (squelch flags)
    le32(dh,  8, 0x06B49D80u); // CenterFreq_Lo = 112.8 MHz
    le32(dh, 12, 0u);        // CenterFreq_Hi
    le32(dh, 16, 200000u);   // DemodBandwidth Hz
    le32(dh, 20, 0u);        // DemodulationType = FM
    le32(dh, 24, 4800u);     // SampleCount
    le32(dh, 28, 1u);        // ChannelCount
    le32(dh, 32, 2u);        // SampleSize bytes
    le32(dh, 36, 0u);        // reserved
    // (bytes 40-43 would be ext_timestamp_lo, not present in 44-byte header)
    return dh;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void test_ammos_valid_if_frame() {
    auto dh = make_if_dh();
    auto f  = build_ammos(0x01u, 7u, dh);

    uint8_t* out = nullptr;
    size_t len = 0;
    int rc = extract_frame(f.data(), f.size(), &out, &len);
    CHECK(rc == 0, "AMMOS IF frame -> extracted successfully");
    CHECK(len == f.size(), "AMMOS IF frame out_len matches input");

    char* json = nullptr;
    size_t json_len = 0;
    int prc = parse_message(out, len, &json, &json_len);
    CHECK(prc == 0, "parse_message(AMMOS IF) -> success");
    CHECK(json != nullptr, "parse_message(AMMOS IF) -> non-null");
    CHECK(contains(json, "\"stream\""), "AMMOS IF json has stream key");
    CHECK(contains(json, "if_data"),   "AMMOS IF json stream == if_data");
    CHECK(contains(json, "\"hw\""),    "AMMOS IF json has hw key");
    CHECK(contains(json, "ca120"),     "AMMOS IF json hw == ca120");
    // source_id = 42
    CHECK(contains(json, "42"),        "AMMOS IF json has source_id value 42");
    free_result(json);
    free_result(out);
}

static void test_ammos_audio_frame() {
    auto dh = make_audio_dh();
    auto f  = build_ammos(0x100u, 3u, dh);

    uint8_t* out = nullptr;
    size_t len = 0;
    int rc = extract_frame(f.data(), f.size(), &out, &len);
    CHECK(rc == 0, "AMMOS Audio frame -> extracted successfully");

    char* json = nullptr;
    size_t json_len = 0;
    int prc = parse_message(out, len, &json, &json_len);
    CHECK(prc == 0, "parse_message(AMMOS Audio) -> success");
    CHECK(json != nullptr, "parse_message(AMMOS Audio) -> non-null");
    CHECK(contains(json, "audio"),     "AMMOS Audio json stream == audio");
    CHECK(contains(json, "FM"),        "AMMOS Audio json demod_type == FM");
    CHECK(contains(json, "48000"),     "AMMOS Audio json sample_rate_hz == 48000");
    free_result(json);
    free_result(out);
}

static void test_ammos_ddce_if_frame() {
    // DDCE uses double for bandwidth/samplerate; header is 44 bytes (0x2C)
    std::vector<uint8_t> dh(44, 0);
    le32(dh,  0, 99u);     // SourceID
    le32(dh,  4, 500u);    // AntVoltageRef
    // bytes 8-11: reserved
    // bytes 12-19: timestamp_ns (uint64) = 1000000000
    le32(dh, 12, 0x3B9ACA00u);  // 1 000 000 000 ns
    le32(dh, 16, 0u);
    // bytes 20-27: CenterFreq (split 64-bit) = 100 MHz
    le32(dh, 20, 0x05F5E100u);  // 100 000 000 Hz
    le32(dh, 24, 0u);
    // bytes 28-35: Bandwidth (double) = 200000.0 Hz
    union { double d; uint64_t u; } bw = { 200000.0 };
    le32(dh, 28, uint32_t(bw.u));
    le32(dh, 32, uint32_t(bw.u >> 32));
    // bytes 36-43: SampleRate (double) = 400000.0 Hz
    union { double d; uint64_t u; } sr = { 400000.0 };
    le32(dh, 36, uint32_t(sr.u));
    le32(dh, 40, uint32_t(sr.u >> 32));

    auto f = build_ammos(0x60u, 1u, dh);

    uint8_t* out = nullptr;
    size_t len = 0;
    int rc = extract_frame(f.data(), f.size(), &out, &len);
    CHECK(rc == 0, "AMMOS DDCE-IF frame -> extracted successfully");

    char* json = nullptr;
    size_t json_len = 0;
    int prc = parse_message(out, len, &json, &json_len);
    CHECK(prc == 0, "parse_message(AMMOS DDCE-IF) -> success");
    CHECK(json != nullptr, "parse_message(AMMOS DDCE-IF) -> non-null");
    CHECK(contains(json, "ddce_if_data"), "AMMOS DDCE-IF json stream == ddce_if_data");
    CHECK(contains(json, "99"),           "AMMOS DDCE-IF json has source_id 99");
    free_result(json);
    free_result(out);
}

static void test_ammos_spectrum_frame() {
    // Spectrum Data_Header is 0x28 = 40 bytes
    std::vector<uint8_t> dh(40, 0);
    le32(dh,  0, 500u);       // timestamp_lo µs
    le32(dh,  4, 0u);
    le32(dh,  8, 0x05F5E100u); // center_freq_lo = 100 MHz
    le32(dh, 12, 0u);
    le32(dh, 16, 0x001E8480u); // sample_rate = 2 MHz
    le32(dh, 20, 1024u);       // fft_length
    // StatusWord: WindowType nibble 7-4 = 2 (HANN), DisplayMode nibble 3-0 = 6 (CLEARWRITE)
    le32(dh, 24, (2u << 4) | 6u);
    // ref_value float 0.0
    le32(dh, 28, 0u);
    le32(dh, 32, 0u);   // left_bin
    le32(dh, 36, 1023u); // right_bin

    auto f = build_ammos(0x13u, 5u, dh);

    uint8_t* out = nullptr;
    size_t len = 0;
    int rc = extract_frame(f.data(), f.size(), &out, &len);
    CHECK(rc == 0, "AMMOS Spectrum frame -> extracted successfully");

    char* json = nullptr;
    size_t json_len = 0;
    int prc = parse_message(out, len, &json, &json_len);
    CHECK(prc == 0, "parse_message(AMMOS Spectrum) -> success");
    CHECK(json != nullptr, "parse_message(AMMOS Spectrum) -> non-null");
    CHECK(contains(json, "spectrum"),    "AMMOS Spectrum json stream == spectrum");
    CHECK(contains(json, "HANN"),        "AMMOS Spectrum json window_type == HANN");
    CHECK(contains(json, "CLEARWRITE"),  "AMMOS Spectrum json display_mode == CLEARWRITE");
    free_result(json);
    free_result(out);
}

static void test_ammos_pdw_frame() {
    // PDW header: pdw_count + pdw_size + 16-byte GUID + uint64 emitter_id = 32 bytes
    std::vector<uint8_t> dh(32, 0);
    le32(dh, 0, 10u);     // pdw_count = 10
    le32(dh, 4, 48u);     // pdw_size_bytes
    // GUID bytes 8-23 (GuidData1 u32 + GuidData2 u16 + GuidData3 u16 + GuidData4 char[8])
    le32(dh,  8, 0xDEADBEEFu);       // GuidData1
    dh[12] = 0xAB; dh[13] = 0xCD;   // GuidData2 (uint16, little-endian)
    dh[14] = 0x12; dh[15] = 0x34;   // GuidData3 (uint16, little-endian)
    // GuidData4: char[8] at bytes 16-23
    const char* tag = "EMITTER1";
    memcpy(dh.data() + 16, tag, 8);
    // emitter_id uint64 at bytes 24-31
    le32(dh, 24, 0x0000007Bu);   // 123
    le32(dh, 28, 0u);

    auto f = build_ammos(0x200u, 2u, dh);

    uint8_t* out = nullptr;
    size_t len = 0;
    int rc = extract_frame(f.data(), f.size(), &out, &len);
    CHECK(rc == 0, "AMMOS PDW frame -> extracted successfully");

    char* json = nullptr;
    size_t json_len = 0;
    int prc = parse_message(out, len, &json, &json_len);
    CHECK(prc == 0, "parse_message(AMMOS PDW) -> success");
    CHECK(json != nullptr, "parse_message(AMMOS PDW) -> non-null");
    CHECK(contains(json, "\"pdw\""),    "AMMOS PDW json stream == pdw");
    CHECK(contains(json, "deadbeef"),   "AMMOS PDW json guid has GuidData1 deadbeef");
    CHECK(contains(json, "10"),         "AMMOS PDW json pdw_count == 10");
    free_result(json);
    free_result(out);
}

static void test_ammos_incomplete_frame() {
    auto dh = make_if_dh();
    auto f  = build_ammos(0x01u, 1u, dh);

    uint8_t* out = nullptr;
    size_t len = 0;

    // One byte short -> incomplete
    int rc = extract_frame(f.data(), f.size() - 1, &out, &len);
    CHECK(rc == -1, "AMMOS truncated by 1 byte -> incomplete (-1)");

    // Only magic present (4 bytes) -> incomplete (need FrameLength)
    rc = extract_frame(f.data(), 4, &out, &len);
    CHECK(rc == -1, "AMMOS only 4 bytes -> incomplete (-1)");
}

static void test_ammos_bad_magic() {
    std::vector<uint8_t> f(8, 0xFF);
    uint8_t* out = nullptr;
    size_t len = 0;
    int rc = extract_frame(f.data(), f.size(), &out, &len);
    // 0xFF is not '<', so neither XML nor AMMOS → -1
    CHECK(rc == -1, "non-magic non-XML bytes -> corrupt (-1)");
}

static void test_ammos_iqdw_exceeds_limit() {
    // IQDW (0x201) with FrameLength > AMMOS_MAX_WORDS (0x100000) — must be accepted
    const uint32_t OVER = 0x100001u;
    std::vector<uint8_t> hdr(8, 0);
    le32(hdr, 0, 0xFB746572u);
    le32(hdr, 4, OVER);

    // We only have the 8-byte header, but we need the 16 bytes to read FrameType.
    // Build a full 8-word (32-byte) header pointing to FrameType 0x201.
    std::vector<uint8_t> mini(32, 0);
    le32(mini, 0,  0xFB746572u);   // magic
    le32(mini, 4,  8u);            // FrameLength = 8 words (32 bytes) so extract_frame returns "complete" for this short header test
    le32(mini, 8,  0u);            // FrameCount
    le32(mini, 12, 0x201u);        // FrameType = IQDW
    le32(mini, 16, 0u);            // DataHeaderLength
    le32(mini, 20, 0u);            // SignalGroup

    uint8_t* out = nullptr;
    size_t len = 0;
    int rc = extract_frame(mini.data(), mini.size(), &out, &len);
    CHECK(rc == 0, "IQDW (0x201) frame with normal FrameLength -> accepted");

    char* json = nullptr;
    size_t json_len = 0;
    int prc = parse_message(out, len, &json, &json_len);
    CHECK(prc == 0, "parse_message(IQDW 0x201) -> success");
    CHECK(json != nullptr, "parse_message(IQDW 0x201) -> non-null");
    CHECK(contains(json, "iqdw"),     "IQDW 0x201 json stream contains iqdw");
    free_result(json);
    free_result(out);
}

// ---------------------------------------------------------------------------
// XML frame tests
// ---------------------------------------------------------------------------

static std::vector<uint8_t> xml_bytes(const char* s) {
    const uint8_t* begin = reinterpret_cast<const uint8_t*>(s);
    return std::vector<uint8_t>(begin, begin + strlen(s));
}

static void test_xml_request() {
    const char* msg =
        "<Request type=\"set\" id=\"1\">"
          "<Tuner><Frequency>100000000</Frequency></Tuner>"
        "</Request>";
    auto f = xml_bytes(msg);

    uint8_t* out = nullptr;
    size_t len = 0;
    int rc = extract_frame(f.data(), f.size(), &out, &len);
    CHECK(rc == 0, "XML <Request> -> extracted successfully");
    CHECK(len == f.size(), "XML Request out_len correct");

    char* json = nullptr;
    size_t json_len = 0;
    int prc = parse_message(out, len, &json, &json_len);
    CHECK(prc == 0,                 "parse_message(XML Request) -> success");
    CHECK(json != nullptr,           "parse_message(XML Request) -> non-null");
    CHECK(contains(json, "\"msg_kind\":\"request\""), "XML Request json msg_kind == request");
    CHECK(contains(json, "\"body\":{\"request\":{\"type\":\"set\",\"id\":\"1\""),
          "XML Request json body.request.type/id preserved");
    CHECK(contains(json, "\"tuner\":{\"frequency\":\"100000000\"}"),
          "XML Request json body.request.tuner.frequency nested and preserved (not dropped)");
    free_result(json);
    free_result(out);
}

static void test_xml_multi_root_request() {
    // §3.5 of ICD: single <Request> may contain multiple subsystem root nodes
    const char* msg =
        "<Request type=\"set\" id=\"2\">"
          "<DigitalDemodulator><SymbolRate>9600</SymbolRate></DigitalDemodulator>"
          "<BitstreamProcessing><Mode>Raw</Mode></BitstreamProcessing>"
        "</Request>";
    auto f = xml_bytes(msg);

    uint8_t* out = nullptr;
    size_t len = 0;
    int rc = extract_frame(f.data(), f.size(), &out, &len);
    CHECK(rc == 0, "XML multi-root Request -> extracted successfully");

    char* json = nullptr;
    size_t json_len = 0;
    int prc = parse_message(out, len, &json, &json_len);
    CHECK(prc == 0,                           "parse_message(multi-root Request) -> success");
    CHECK(json != nullptr,                    "parse_message(multi-root Request) -> non-null");
    CHECK(contains(json, "\"msg_kind\":\"request\""), "multi-root msg_kind == request");
    CHECK(contains(json, "\"digital_demodulator\":{\"symbol_rate\":\"9600\"}"),
          "multi-root body.request.digital_demodulator.symbol_rate == 9600");
    CHECK(contains(json, "\"bitstream_processing\":{\"mode\":\"Raw\"}"),
          "multi-root body.request.bitstream_processing.mode == Raw");
    free_result(json);
    free_result(out);
}

static void test_xml_reply() {
    const char* msg =
        "<Reply type=\"set\" id=\"1\"><Tuner><ProcessingStatus/></Tuner></Reply>";
    auto f = xml_bytes(msg);

    uint8_t* out = nullptr;
    size_t len = 0;
    int rc = extract_frame(f.data(), f.size(), &out, &len);
    CHECK(rc == 0, "XML <Reply> -> extracted successfully");

    char* json = nullptr;
    size_t json_len = 0;
    int prc = parse_message(out, len, &json, &json_len);
    CHECK(prc == 0,               "parse_message(XML Reply) -> success");
    CHECK(json != nullptr,        "parse_message(XML Reply) -> non-null");
    CHECK(contains(json, "\"msg_kind\":\"reply\""), "XML Reply json msg_kind == reply");
    CHECK(contains(json, "\"tuner\":{\"processing_status\":{}}"),
          "XML Reply json body.reply.tuner.processing_status == {} (empty self-closing element)");
    free_result(json);
    free_result(out);
}

static void test_xml_event() {
    const char* msg =
        "<Event source=\"Tuner\" id=\"5\">"
          "<Status>detected</Status>"
          "<Frequency>100000000</Frequency>"
        "</Event>";
    auto f = xml_bytes(msg);

    uint8_t* out = nullptr;
    size_t len = 0;
    int rc = extract_frame(f.data(), f.size(), &out, &len);
    CHECK(rc == 0, "XML <Event> -> extracted successfully");

    char* json = nullptr;
    size_t json_len = 0;
    int prc = parse_message(out, len, &json, &json_len);
    CHECK(prc == 0,                "parse_message(XML Event) -> success");
    CHECK(json != nullptr,         "parse_message(XML Event) -> non-null");
    CHECK(contains(json, "\"msg_kind\":\"event\""), "XML Event json msg_kind == event");
    CHECK(contains(json, "\"status\":\"detected\""), "XML Event json body.event.status == detected");
    CHECK(contains(json, "\"frequency\":\"100000000\""),
          "XML Event json body.event.frequency preserved (old curated extraction only kept this under a hardcoded field name)");
    free_result(json);
    free_result(out);
}

static void test_xml_datastream_reply() {
    // Root Reply's own type="get" and DataStream's own type="IFData" must
    // stay independently scoped. The generic tree mirror nests each node's
    // attributes under that node by construction, so this can no longer
    // collide the way the old curated extraction did (which searched the
    // whole buffer for the first type="..." and returned the root's).
    const char* msg =
        "<Reply type=\"get\" id=\"10\">"
          "<DataStream type=\"IFData\">"
            "<IP>192.168.1.1</IP><Port>9200</Port>"
          "</DataStream>"
        "</Reply>";
    auto f = xml_bytes(msg);

    uint8_t* out = nullptr;
    size_t len = 0;
    int rc = extract_frame(f.data(), f.size(), &out, &len);
    CHECK(rc == 0, "XML DataStream reply -> extracted successfully");

    char* json = nullptr;
    size_t json_len = 0;
    int prc = parse_message(out, len, &json, &json_len);
    CHECK(prc == 0,        "parse_message(DataStream reply) -> success");
    CHECK(json != nullptr, "parse_message(DataStream reply) -> non-null");
    CHECK(contains(json, "\"msg_kind\":\"reply\""), "DataStream reply msg_kind == reply");
    CHECK(contains(json, "\"reply\":{\"type\":\"get\",\"id\":\"10\""),
          "DataStream reply: root Reply's own type == get, scoped at the reply level");
    CHECK(contains(json, "\"data_stream\":{\"type\":\"IFData\",\"ip\":\"192.168.1.1\",\"port\":\"9200\"}"),
          "DataStream reply: DataStream's own type == IFData, nested under data_stream (not confused with the root's type)");
    free_result(json);
    free_result(out);
}

static void test_xml_incomplete() {
    const char* partial = "<Request type=\"set\" id=\"3\"><Tuner>";
    auto f = xml_bytes(partial);

    uint8_t* out = nullptr;
    size_t len = 0;
    int rc = extract_frame(f.data(), f.size(), &out, &len);
    CHECK(rc == -1, "XML without closing </Request> -> incomplete (-1)");
}

// extract_frame's outer </Request> search is satisfied (it isn't looking
// for well-formedness), so this reaches parse_message, which must still
// succeed (rc == 0) with an error-shaped JSON record instead of failing the
// call -- a message is never silently dropped just because it's malformed.
static void test_xml_malformed_still_succeeds() {
    const char* msg = "<Request type=\"set\" id=\"1\"><Tuner></Request>";
    auto f = xml_bytes(msg);

    uint8_t* out = nullptr;
    size_t len = 0;
    int rc = extract_frame(f.data(), f.size(), &out, &len);
    CHECK(rc == 0, "malformed-inside XML -> extract_frame still finds the outer </Request>");

    char* json = nullptr;
    size_t json_len = 0;
    int prc = parse_message(out, len, &json, &json_len);
    CHECK(prc == 0,        "parse_message(malformed XML) -> still succeeds (rc == 0)");
    CHECK(json != nullptr, "parse_message(malformed XML) -> non-null");
    CHECK(contains(json, "\"msg_kind\":\"malformed\""), "malformed XML json msg_kind == malformed");
    CHECK(contains(json, "\"parse_error\":"),           "malformed XML json has parse_error");
    CHECK(contains(json, "\"parse_offset\":"),          "malformed XML json has parse_offset");
    CHECK(!contains(json, "\"body\":"),                 "malformed XML json has no body (parse never completed)");
    free_result(json);
    free_result(out);
}

static void test_xml_leading_whitespace() {
    const char* msg = "   \r\n<Reply id=\"1\"><Control/></Reply>";
    auto f = xml_bytes(msg);

    uint8_t* out = nullptr;
    size_t len = 0;
    int rc = extract_frame(f.data(), f.size(), &out, &len);
    CHECK(rc == 0, "XML Reply with leading whitespace -> extracted successfully");

    char* json = nullptr;
    size_t json_len = 0;
    int prc = parse_message(out, len, &json, &json_len);
    CHECK(prc == 0, "parse_message(XML Reply with leading whitespace) -> success");
    CHECK(json != nullptr, "parse_message(XML Reply with leading whitespace) -> non-null");
    CHECK(contains(json, "\"msg_kind\":\"reply\""), "XML Reply with leading whitespace msg_kind == reply");
    CHECK(contains(json, "\"control\":{}"),
          "XML Reply with leading whitespace body.reply.control == {} (empty self-closing element)");
    free_result(json);
    free_result(out);
}

// ---------------------------------------------------------------------------
// format_response tests
// ---------------------------------------------------------------------------

// format_response() accepts exactly the shape parse_message() itself
// produces (msg_kind/body.<msg_kind> wrapper) -- see ca120_parser.cpp's
// format_response doc comment and ca120_tag_table.h. No separate flattened
// xml_body contract exists any more; a caller (drs-server, or a
// random-mode generator) hands back parse_message's own output verbatim.

static void test_format_response_basic() {
    const char* json =
        "{\"msg_kind\":\"request\","
        "\"body\":{\"request\":{\"type\":\"set\",\"id\":\"7\","
        "\"tuner\":{\"frequency\":\"100000000\"}}}}";

    uint8_t* out_buf = nullptr;
    size_t out_len = 0;
    int rc = format_response("request", json, &out_buf, &out_len);
    CHECK(rc == 0, "format_response returns success");

    std::string xml(reinterpret_cast<const char*>(out_buf), out_len);
    CHECK(xml.find("<Request") != std::string::npos,   "format_response produces <Request");
    CHECK(xml.find("</Request>") != std::string::npos, "format_response produces </Request>");
    CHECK(xml.find("type=\"set\"") != std::string::npos, "format_response has type=set");
    CHECK(xml.find("id=\"7\"") != std::string::npos,     "format_response has id=7");
    CHECK(xml.find("<Tuner><Frequency>100000000</Frequency></Tuner>") != std::string::npos,
          "format_response reconstructed Tuner/Frequency from nested JSON");
    free_result(out_buf);
}

static void test_format_response_with_time() {
    const char* json =
        "{\"msg_kind\":\"request\","
        "\"body\":{\"request\":{\"type\":\"get\",\"id\":\"3\",\"time\":\"12345\","
        "\"tuner\":{}}}}";

    uint8_t* out_buf = nullptr;
    size_t out_len = 0;
    int rc = format_response("request", json, &out_buf, &out_len);
    CHECK(rc == 0, "format_response with time -> success");

    std::string xml(reinterpret_cast<const char*>(out_buf), out_len);
    CHECK(xml.find("time=\"12345\"") != std::string::npos, "format_response has time=12345");
    CHECK(xml.find("<Tuner/>") != std::string::npos, "format_response empty tuner object self-closes");
    free_result(out_buf);
}

static void test_format_response_missing_required_field() {
    // Missing "body"
    const char* json = "{\"msg_kind\":\"request\"}";
    uint8_t* out_buf = nullptr;
    size_t out_len = 0;
    int rc = format_response("request", json, &out_buf, &out_len);
    CHECK(rc == -1, "format_response with missing body -> -1");
    free_result(out_buf);
}

static void test_format_response_unknown_tag_fails() {
    // "not_a_real_tag" is not in ca120_tag_table.h -- must fail loudly, not guess
    const char* json =
        "{\"msg_kind\":\"request\","
        "\"body\":{\"request\":{\"type\":\"set\",\"id\":\"1\","
        "\"not_a_real_tag\":{\"x\":\"1\"}}}}";
    uint8_t* out_buf = nullptr;
    size_t out_len = 0;
    int rc = format_response("request", json, &out_buf, &out_len);
    CHECK(rc == -1, "format_response with an uncatalogued tag -> -1 (fails loudly, no guessing)");
    free_result(out_buf);
}

static void test_format_response_roundtrip() {
    // Build a Request via format_response, then extract_frame it back
    const char* json =
        "{\"msg_kind\":\"request\","
        "\"body\":{\"request\":{\"type\":\"set\",\"id\":\"9\","
        "\"tuner\":{\"frequency\":\"200000000\"}}}}";

    uint8_t* wire = nullptr;
    size_t wire_len = 0;
    int frc = format_response("request", json, &wire, &wire_len);
    CHECK(frc == 0, "roundtrip: format_response produced bytes");

    uint8_t* out = nullptr;
    size_t out_len = 0;
    int rc = extract_frame(wire, wire_len, &out, &out_len);
    CHECK(rc == 0, "roundtrip: extract_frame of format_response output -> extracted successfully");

    char* result = nullptr;
    size_t result_len = 0;
    int prc = parse_message(out, out_len, &result, &result_len);
    CHECK(prc == 0,                      "roundtrip: parse_message -> success");
    CHECK(result != nullptr,             "roundtrip: parse_message -> non-null");
    CHECK(contains(result, "\"msg_kind\":\"request\""), "roundtrip: msg_kind == request");
    CHECK(contains(result, "\"frequency\":\"200000000\""),
          "roundtrip: frequency value preserved (nested under body.request.tuner)");
    free_result(result);
    free_result(out);
    free_result(wire);
}

// Round-trips the ICD §5.6.4 AvailableDemodulators/AvailableDecoders Reply
// body (two subsystem roots) through format_response -> extract_frame ->
// parse_message, confirming ca120_tag_table.h's entries for that payload
// are correct end-to-end (matches CA120_9001_Control_XML_to_JSON.md).
static void test_format_response_available_demodulators_roundtrip() {
    const char* json =
        "{\"msg_kind\":\"reply\","
        "\"body\":{\"reply\":{\"type\":\"get\",\"id\":\"70054\","
        "\"digital_demodulator\":{\"available_demodulators\":{\"demodulator_info\":{"
        "\"demodulator_name\":\"ASK2\",\"demodulator_version\":\"1\",\"module_id\":\"1048576\","
        "\"parameter_size\":\"9\",\"supports_symbol_data\":\"1\","
        "\"supports_iq_constellation_data\":\"0\",\"supports_instant_data\":\"1\","
        "\"supports_image_data\":\"0\",\"supports_transmission_data\":\"0\","
        "\"supports_audio_data\":\"0\",\"is_universal\":\"1\",\"supports_special_data\":\"0\""
        "}}},"
        "\"bitstream_processing\":{\"available_decoders\":{\"decoder\":{"
        "\"id\":\"100000\",\"classification_only\":\"1\",\"decoder_name\":\"ASCII\""
        "}}}"
        "}}}";

    uint8_t* wire = nullptr;
    size_t wire_len = 0;
    int frc = format_response("reply", json, &wire, &wire_len);
    CHECK(frc == 0, "AvailableDemodulators/Decoders: format_response produced bytes");

    std::string xml(reinterpret_cast<const char*>(wire), wire_len);
    CHECK(xml.find("<Reply type=\"get\" id=\"70054\">") != std::string::npos,
          "AvailableDemodulators/Decoders: root Reply attrs correct");
    CHECK(xml.find("<SupportsIQ_ConstellationData>0</SupportsIQ_ConstellationData>") != std::string::npos,
          "AvailableDemodulators/Decoders: acronym+underscore tag name reconstructed exactly");
    CHECK(xml.find("<Decoder id=\"100000\" classificationOnly=\"1\">") != std::string::npos,
          "AvailableDemodulators/Decoders: classificationOnly attribute casing reconstructed");

    uint8_t* out = nullptr;
    size_t out_len = 0;
    int rc = extract_frame(wire, wire_len, &out, &out_len);
    CHECK(rc == 0, "AvailableDemodulators/Decoders: extract_frame of format_response output -> success");

    char* result = nullptr;
    size_t result_len = 0;
    int prc = parse_message(out, out_len, &result, &result_len);
    CHECK(prc == 0,          "AvailableDemodulators/Decoders: roundtrip parse_message -> success");
    CHECK(contains(result, "\"demodulator_name\":\"ASK2\""),
          "AvailableDemodulators/Decoders: roundtrip demodulator_name preserved");
    CHECK(contains(result, "\"classification_only\":\"1\""),
          "AvailableDemodulators/Decoders: roundtrip classification_only preserved");
    free_result(result);
    free_result(out);
    free_result(wire);
}

// Round-trips ICD §5.3.2's Tuner Parameters Get reply (26 fields) -- the
// exact case shown earlier failing before ca120_tag_table.h was extended
// to cover Tuner's fields beyond bare Frequency.
static void test_format_response_tuner_parameters_roundtrip() {
    const char* json =
        "{\"msg_kind\":\"reply\","
        "\"body\":{\"reply\":{\"type\":\"get\",\"id\":\"67456\","
        "\"tuner\":{\"parameters\":{"
        "\"frequency\":{\"unit\":\"Hz\",\"#text\":\"110000000\"},"
        "\"bandwidth\":{\"unit\":\"Hz\",\"#text\":\"80000000\"},"
        "\"preselection\":\"lowDistortion\","
        "\"mode\":\"ffm\""
        "}}}}}";

    uint8_t* wire = nullptr;
    size_t wire_len = 0;
    int frc = format_response("reply", json, &wire, &wire_len);
    CHECK(frc == 0, "Tuner Parameters: format_response produced bytes");

    std::string xml(reinterpret_cast<const char*>(wire), wire_len);
    CHECK(xml.find("<Bandwidth unit=\"Hz\">80000000</Bandwidth>") != std::string::npos,
          "Tuner Parameters: Bandwidth (previously-uncatalogued tag) now encodes correctly");
    CHECK(xml.find("<Mode>ffm</Mode>") != std::string::npos,
          "Tuner Parameters: bare 'mode' resolves to the <Mode> ELEMENT here (no DCP parent)");

    uint8_t* out = nullptr;
    size_t out_len = 0;
    int rc = extract_frame(wire, wire_len, &out, &out_len);
    CHECK(rc == 0, "Tuner Parameters: extract_frame of format_response output -> success");
    char* result = nullptr;
    size_t result_len = 0;
    int prc = parse_message(out, out_len, &result, &result_len);
    CHECK(prc == 0, "Tuner Parameters: roundtrip parse_message -> success");
    CHECK(contains(result, "\"bandwidth\":{\"unit\":\"Hz\",\"#text\":\"80000000\"}"),
          "Tuner Parameters: roundtrip bandwidth preserved");
    free_result(result);
    free_result(out);
    free_result(wire);
}

// Proves the "mode" attribute-vs-element context resolution: DCP's own
// mode="multiChannel" attribute (ICD §5.1.1) must NOT be confused with
// Tuner's <Mode> element exercised in the previous test.
static void test_format_response_dcp_mode_attribute_roundtrip() {
    const char* json =
        "{\"msg_kind\":\"request\","
        "\"body\":{\"request\":{\"type\":\"set\",\"id\":\"2\","
        "\"dcp\":{\"type\":\"detector\",\"mode\":\"multiChannel\","
        "\"resource_demand_class\":\"hw\"}}}}";

    uint8_t* wire = nullptr;
    size_t wire_len = 0;
    int frc = format_response("request", json, &wire, &wire_len);
    CHECK(frc == 0, "DCP mode attribute: format_response produced bytes");

    std::string xml(reinterpret_cast<const char*>(wire), wire_len);
    CHECK(xml.find("<DCP type=\"detector\" mode=\"multiChannel\">") != std::string::npos,
          "DCP mode attribute: 'mode' resolves to the mode=\"...\" ATTRIBUTE here (DCP parent context)");

    uint8_t* out = nullptr;
    size_t out_len = 0;
    int rc = extract_frame(wire, wire_len, &out, &out_len);
    CHECK(rc == 0, "DCP mode attribute: extract_frame of format_response output -> success");
    char* result = nullptr;
    size_t result_len = 0;
    int prc = parse_message(out, out_len, &result, &result_len);
    CHECK(prc == 0, "DCP mode attribute: roundtrip parse_message -> success");
    CHECK(contains(result, "\"mode\":\"multiChannel\""),
          "DCP mode attribute: roundtrip mode value preserved");
    free_result(result);
    free_result(out);
    free_result(wire);
}

// Round-trips ICD §5.1.2's LicenseAllocation reply -- three levels of
// same-named "License" nested recursively, stress-testing that the encoder
// handles a tag nesting inside itself (not just distinct parent/child tags).
static void test_format_response_license_allocation_roundtrip() {
    const char* json =
        "{\"msg_kind\":\"reply\","
        "\"body\":{\"reply\":{\"type\":\"get\",\"id\":\"67388\","
        "\"resource_manager\":{\"license_allocation\":{\"license\":{"
        "\"name\":\"root\",\"license\":{"
        "\"name\":\"mid\",\"total_licenses\":\"10\",\"available_licenses\":\"3\","
        "\"license\":{\"name\":\"leaf\"}"
        "}}}}}}}";

    uint8_t* wire = nullptr;
    size_t wire_len = 0;
    int frc = format_response("reply", json, &wire, &wire_len);
    CHECK(frc == 0, "LicenseAllocation: format_response produced bytes");

    std::string xml(reinterpret_cast<const char*>(wire), wire_len);
    CHECK(xml.find("<TotalLicenses>10</TotalLicenses>") != std::string::npos,
          "LicenseAllocation: nested License-in-License-in-License encodes correctly");

    uint8_t* out = nullptr;
    size_t out_len = 0;
    int rc = extract_frame(wire, wire_len, &out, &out_len);
    CHECK(rc == 0, "LicenseAllocation: extract_frame of format_response output -> success");
    char* result = nullptr;
    size_t result_len = 0;
    int prc = parse_message(out, out_len, &result, &result_len);
    CHECK(prc == 0, "LicenseAllocation: roundtrip parse_message -> success");
    CHECK(contains(result, "\"available_licenses\":\"3\""),
          "LicenseAllocation: roundtrip nested value preserved");
    free_result(result);
    free_result(out);
    free_result(wire);
}

// ---------------------------------------------------------------------------
// free_result
// ---------------------------------------------------------------------------

static void test_free_result_null_safe() {
    free_result(nullptr);  // must not crash
    CHECK(true, "free_result(nullptr) does not crash");
}

static void test_free_result_real_pointer() {
    // Allocate a real parse result and free it
    auto dh = make_if_dh();
    auto f  = build_ammos(0x01u, 1u, dh);
    uint8_t* out = nullptr;
    size_t len = 0;
    extract_frame(f.data(), f.size(), &out, &len);
    char* json = nullptr;
    size_t json_len = 0;
    parse_message(out, len, &json, &json_len);
    free_result(json);  // must not crash or leak
    free_result(out);   // must not crash or leak
    CHECK(true, "free_result on real parse_message result does not crash");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    std::printf("=== CA120 parser frame tests ===\n\n");

    test_ammos_valid_if_frame();
    test_ammos_audio_frame();
    test_ammos_ddce_if_frame();
    test_ammos_spectrum_frame();
    test_ammos_pdw_frame();
    test_ammos_incomplete_frame();
    test_ammos_bad_magic();
    test_ammos_iqdw_exceeds_limit();

    test_xml_request();
    test_xml_multi_root_request();
    test_xml_reply();
    test_xml_event();
    test_xml_datastream_reply();
    test_xml_incomplete();
    test_xml_malformed_still_succeeds();
    test_xml_leading_whitespace();

    test_format_response_basic();
    test_format_response_with_time();
    test_format_response_missing_required_field();
    test_format_response_unknown_tag_fails();
    test_format_response_roundtrip();
    test_format_response_available_demodulators_roundtrip();
    test_format_response_tuner_parameters_roundtrip();
    test_format_response_dcp_mode_attribute_roundtrip();
    test_format_response_license_allocation_roundtrip();

    test_free_result_null_safe();
    test_free_result_real_pointer();

    std::printf("\n=== %d failure(s) ===\n", g_failures);
    return (g_failures == 0) ? 0 : 1;
}
