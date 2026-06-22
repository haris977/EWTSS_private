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

    std::vector<uint8_t> out(MAX_FRAME_BUFFER_BYTES);
    int len = 0;
    int t = extract_frame(f.data(), (int)f.size(), out.data(), &len);
    CHECK(t == 3, "AMMOS IF frame -> type 3 (streaming)");
    CHECK(len == (int)f.size(), "AMMOS IF frame out_len matches input");

    const char* json = parse_message(out.data(), len, 3);
    CHECK(json != nullptr, "parse_message(AMMOS IF) -> non-null");
    CHECK(contains(json, "\"stream\""), "AMMOS IF json has stream key");
    CHECK(contains(json, "if_data"),   "AMMOS IF json stream == if_data");
    CHECK(contains(json, "\"hw\""),    "AMMOS IF json has hw key");
    CHECK(contains(json, "ca120"),     "AMMOS IF json hw == ca120");
    // source_id = 42
    CHECK(contains(json, "42"),        "AMMOS IF json has source_id value 42");
    free_result(json);
}

static void test_ammos_audio_frame() {
    auto dh = make_audio_dh();
    auto f  = build_ammos(0x100u, 3u, dh);

    std::vector<uint8_t> out(MAX_FRAME_BUFFER_BYTES);
    int len = 0;
    int t = extract_frame(f.data(), (int)f.size(), out.data(), &len);
    CHECK(t == 3, "AMMOS Audio frame -> type 3");

    const char* json = parse_message(out.data(), len, 3);
    CHECK(json != nullptr, "parse_message(AMMOS Audio) -> non-null");
    CHECK(contains(json, "audio"),     "AMMOS Audio json stream == audio");
    CHECK(contains(json, "FM"),        "AMMOS Audio json demod_type == FM");
    CHECK(contains(json, "48000"),     "AMMOS Audio json sample_rate_hz == 48000");
    free_result(json);
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

    std::vector<uint8_t> out(MAX_FRAME_BUFFER_BYTES);
    int len = 0;
    int t = extract_frame(f.data(), (int)f.size(), out.data(), &len);
    CHECK(t == 3, "AMMOS DDCE-IF frame -> type 3");

    const char* json = parse_message(out.data(), len, 3);
    CHECK(json != nullptr, "parse_message(AMMOS DDCE-IF) -> non-null");
    CHECK(contains(json, "ddce_if_data"), "AMMOS DDCE-IF json stream == ddce_if_data");
    CHECK(contains(json, "99"),           "AMMOS DDCE-IF json has source_id 99");
    free_result(json);
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

    std::vector<uint8_t> out(MAX_FRAME_BUFFER_BYTES);
    int len = 0;
    int t = extract_frame(f.data(), (int)f.size(), out.data(), &len);
    CHECK(t == 3, "AMMOS Spectrum frame -> type 3");

    const char* json = parse_message(out.data(), len, 3);
    CHECK(json != nullptr, "parse_message(AMMOS Spectrum) -> non-null");
    CHECK(contains(json, "spectrum"),    "AMMOS Spectrum json stream == spectrum");
    CHECK(contains(json, "HANN"),        "AMMOS Spectrum json window_type == HANN");
    CHECK(contains(json, "CLEARWRITE"),  "AMMOS Spectrum json display_mode == CLEARWRITE");
    free_result(json);
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

    std::vector<uint8_t> out(MAX_FRAME_BUFFER_BYTES);
    int len = 0;
    int t = extract_frame(f.data(), (int)f.size(), out.data(), &len);
    CHECK(t == 3, "AMMOS PDW frame -> type 3");

    const char* json = parse_message(out.data(), len, 3);
    CHECK(json != nullptr, "parse_message(AMMOS PDW) -> non-null");
    CHECK(contains(json, "\"pdw\""),    "AMMOS PDW json stream == pdw");
    CHECK(contains(json, "deadbeef"),   "AMMOS PDW json guid has GuidData1 deadbeef");
    CHECK(contains(json, "10"),         "AMMOS PDW json pdw_count == 10");
    free_result(json);
}

static void test_ammos_incomplete_frame() {
    auto dh = make_if_dh();
    auto f  = build_ammos(0x01u, 1u, dh);

    std::vector<uint8_t> out(MAX_FRAME_BUFFER_BYTES);
    int len = 0;

    // One byte short -> incomplete
    int t = extract_frame(f.data(), (int)f.size() - 1, out.data(), &len);
    CHECK(t == 0, "AMMOS truncated by 1 byte -> incomplete (0)");

    // Only magic present (4 bytes) -> incomplete (need FrameLength)
    t = extract_frame(f.data(), 4, out.data(), &len);
    CHECK(t == 0, "AMMOS only 4 bytes -> incomplete (0)");
}

static void test_ammos_bad_magic() {
    std::vector<uint8_t> f(8, 0xFF);
    std::vector<uint8_t> out(MAX_FRAME_BUFFER_BYTES);
    int len = 0;
    int t = extract_frame(f.data(), (int)f.size(), out.data(), &len);
    // 0xFF is not '<', so neither XML nor AMMOS → -1
    CHECK(t == -1, "non-magic non-XML bytes -> corrupt (-1)");
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

    std::vector<uint8_t> out(MAX_FRAME_BUFFER_BYTES);
    int len = 0;
    int t = extract_frame(mini.data(), (int)mini.size(), out.data(), &len);
    CHECK(t == 3, "IQDW (0x201) frame with normal FrameLength -> accepted (type 3)");

    const char* json = parse_message(out.data(), len, 3);
    CHECK(json != nullptr, "parse_message(IQDW 0x201) -> non-null");
    CHECK(contains(json, "iqdw"),     "IQDW 0x201 json stream contains iqdw");
    free_result(json);
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

    std::vector<uint8_t> out(MAX_FRAME_BUFFER_BYTES);
    int len = 0;
    int t = extract_frame(f.data(), (int)f.size(), out.data(), &len);
    CHECK(t == 1, "XML <Request> -> type 1 (cmd)");
    CHECK(len == (int)f.size(), "XML Request out_len correct");

    const char* json = parse_message(out.data(), len, 1);
    CHECK(json != nullptr,           "parse_message(XML Request) -> non-null");
    CHECK(contains(json, "request"), "XML Request json msg_kind == request");
    CHECK(contains(json, "tuner"),   "XML Request json subsystems includes tuner");
    CHECK(contains(json, "\"set\""), "XML Request json msg_type == set");
    free_result(json);
}

static void test_xml_multi_root_request() {
    // §3.5 of ICD: single <Request> may contain multiple subsystem root nodes
    const char* msg =
        "<Request type=\"set\" id=\"2\">"
          "<DigitalDemodulator><SymbolRate>9600</SymbolRate></DigitalDemodulator>"
          "<BitstreamProcessing><Mode>Raw</Mode></BitstreamProcessing>"
        "</Request>";
    auto f = xml_bytes(msg);

    std::vector<uint8_t> out(MAX_FRAME_BUFFER_BYTES);
    int len = 0;
    int t = extract_frame(f.data(), (int)f.size(), out.data(), &len);
    CHECK(t == 1, "XML multi-root Request -> type 1");

    const char* json = parse_message(out.data(), len, 1);
    CHECK(json != nullptr,                    "parse_message(multi-root Request) -> non-null");
    CHECK(contains(json, "digital_demodulator"),  "multi-root subsystems has digital_demodulator");
    CHECK(contains(json, "bitstream_processing"), "multi-root subsystems has bitstream_processing");
    free_result(json);
}

static void test_xml_reply() {
    const char* msg =
        "<Reply type=\"set\" id=\"1\"><Tuner><ProcessingStatus/></Tuner></Reply>";
    auto f = xml_bytes(msg);

    std::vector<uint8_t> out(MAX_FRAME_BUFFER_BYTES);
    int len = 0;
    int t = extract_frame(f.data(), (int)f.size(), out.data(), &len);
    CHECK(t == 2, "XML <Reply> -> type 2 (response)");

    const char* json = parse_message(out.data(), len, 2);
    CHECK(json != nullptr,        "parse_message(XML Reply) -> non-null");
    CHECK(contains(json, "reply"),"XML Reply json msg_kind == reply");
    free_result(json);
}

static void test_xml_event() {
    const char* msg =
        "<Event source=\"Tuner\" id=\"5\">"
          "<Status>detected</Status>"
          "<Frequency>100000000</Frequency>"
        "</Event>";
    auto f = xml_bytes(msg);

    std::vector<uint8_t> out(MAX_FRAME_BUFFER_BYTES);
    int len = 0;
    int t = extract_frame(f.data(), (int)f.size(), out.data(), &len);
    CHECK(t == 2, "XML <Event> -> type 2");

    const char* json = parse_message(out.data(), len, 2);
    CHECK(json != nullptr,         "parse_message(XML Event) -> non-null");
    CHECK(contains(json, "event"), "XML Event json msg_kind == event");
    CHECK(contains(json, "detected"), "XML Event json status == detected");
    free_result(json);
}

static void test_xml_datastream_reply() {
    const char* msg =
        "<Reply type=\"get\" id=\"10\">"
          "<DataStream type=\"IFData\">"
            "<IP>192.168.1.1</IP><Port>9200</Port>"
          "</DataStream>"
        "</Reply>";
    auto f = xml_bytes(msg);

    std::vector<uint8_t> out(MAX_FRAME_BUFFER_BYTES);
    int len = 0;
    int t = extract_frame(f.data(), (int)f.size(), out.data(), &len);
    CHECK(t == 2, "XML DataStream reply -> type 2");

    const char* json = parse_message(out.data(), len, 2);
    CHECK(json != nullptr,              "parse_message(DataStream reply) -> non-null");
    CHECK(contains(json, "9200"),       "DataStream reply has port 9200");
    CHECK(contains(json, "192.168.1.1"),"DataStream reply has IP");
    free_result(json);
}

static void test_xml_incomplete() {
    const char* partial = "<Request type=\"set\" id=\"3\"><Tuner>";
    auto f = xml_bytes(partial);

    std::vector<uint8_t> out(MAX_FRAME_BUFFER_BYTES);
    int len = 0;
    int t = extract_frame(f.data(), (int)f.size(), out.data(), &len);
    CHECK(t == 0, "XML without closing </Request> -> incomplete (0)");
}

static void test_xml_leading_whitespace() {
    const char* msg = "   \r\n<Reply id=\"1\"><Control/></Reply>";
    auto f = xml_bytes(msg);

    std::vector<uint8_t> out(MAX_FRAME_BUFFER_BYTES);
    int len = 0;
    int t = extract_frame(f.data(), (int)f.size(), out.data(), &len);
    CHECK(t == 2, "XML Reply with leading whitespace -> type 2");
}

// ---------------------------------------------------------------------------
// format_response tests
// ---------------------------------------------------------------------------

static void test_format_response_basic() {
    const char* json =
        "{\"msg_type\":\"set\",\"id\":7,"
        "\"xml_body\":\"<Tuner><Frequency>100000000</Frequency></Tuner>\"}";

    std::vector<uint8_t> out(MAX_FRAME_BUFFER_BYTES);
    int n = format_response(json, out.data());
    CHECK(n > 0, "format_response returns positive byte count");

    out[n] = 0;
    const char* xml = reinterpret_cast<const char*>(out.data());
    CHECK(std::strstr(xml, "<Request") != nullptr,   "format_response produces <Request");
    CHECK(std::strstr(xml, "</Request>") != nullptr, "format_response produces </Request>");
    CHECK(std::strstr(xml, "type=\"set\"") != nullptr, "format_response has type=set");
    CHECK(std::strstr(xml, "id=\"7\"") != nullptr,     "format_response has id=7");
    CHECK(std::strstr(xml, "Frequency") != nullptr,    "format_response body preserved");
}

static void test_format_response_with_time() {
    const char* json =
        "{\"msg_type\":\"get\",\"id\":3,\"time\":12345,"
        "\"xml_body\":\"<Tuner/>\"}";

    std::vector<uint8_t> out(MAX_FRAME_BUFFER_BYTES);
    int n = format_response(json, out.data());
    CHECK(n > 0, "format_response with time -> positive byte count");

    out[n] = 0;
    const char* xml = reinterpret_cast<const char*>(out.data());
    CHECK(std::strstr(xml, "time=\"12345\"") != nullptr, "format_response has time=12345");
}

static void test_format_response_missing_required_field() {
    // Missing xml_body
    const char* json = "{\"msg_type\":\"set\",\"id\":1}";
    std::vector<uint8_t> out(MAX_FRAME_BUFFER_BYTES);
    int n = format_response(json, out.data());
    CHECK(n == -1, "format_response with missing xml_body -> -1");
}

static void test_format_response_roundtrip() {
    // Build a Request via format_response, then extract_frame it back
    const char* json =
        "{\"msg_type\":\"set\",\"id\":9,"
        "\"xml_body\":\"<Tuner><Frequency>200000000</Frequency></Tuner>\"}";

    std::vector<uint8_t> wire(MAX_FRAME_BUFFER_BYTES);
    int wire_len = format_response(json, wire.data());
    CHECK(wire_len > 0, "roundtrip: format_response produced bytes");

    std::vector<uint8_t> out(MAX_FRAME_BUFFER_BYTES);
    int out_len = 0;
    int t = extract_frame(wire.data(), wire_len, out.data(), &out_len);
    CHECK(t == 1, "roundtrip: extract_frame of format_response output -> type 1 (cmd)");

    const char* result = parse_message(out.data(), out_len, 1);
    CHECK(result != nullptr,             "roundtrip: parse_message -> non-null");
    CHECK(contains(result, "request"),   "roundtrip: msg_kind == request");
    CHECK(contains(result, "200000000"), "roundtrip: frequency value preserved");
    free_result(result);
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
    std::vector<uint8_t> out(MAX_FRAME_BUFFER_BYTES);
    int len = 0;
    extract_frame(f.data(), (int)f.size(), out.data(), &len);
    const char* json = parse_message(out.data(), len, 3);
    free_result(json);  // must not crash or leak
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
    test_xml_leading_whitespace();

    test_format_response_basic();
    test_format_response_with_time();
    test_format_response_missing_required_field();
    test_format_response_roundtrip();

    test_free_result_null_safe();
    test_free_result_real_pointer();

    std::printf("\n=== %d failure(s) ===\n", g_failures);
    return (g_failures == 0) ? 0 : 1;
}
