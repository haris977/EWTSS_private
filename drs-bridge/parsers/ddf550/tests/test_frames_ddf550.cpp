// drs-bridge/parsers/ddf550/tests/test_frames_ddf550.cpp
//
// Unit tests for ddf550_parser.cpp — no external test framework.
// Exercises extract_frame, parse_message, format_response, and free_result
// across all three frame channels (EB200, wrapped XML, raw XML/DFData).
//
// ABI note: these tests call the real sdfc_abi.h contract (malloc'd
// out-params, 0/-1 return codes, no frame_type argument — parse_message
// infers type from magic bytes). Prior to 2026-07-07 this file called a
// different, incompatible ABI shape (direct buffers, frame-type-as-return-
// value) and did not compile against the current header.

#include "sdfc_abi.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Helpers: big-endian frame builders
// ---------------------------------------------------------------------------

static void be8(std::vector<uint8_t>& v, uint8_t x)   { v.push_back(x); }
static void be16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back((x >> 8) & 0xFF);
    v.push_back( x       & 0xFF);
}
static void be32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((x >> 24) & 0xFF);
    v.push_back((x >> 16) & 0xFF);
    v.push_back((x >>  8) & 0xFF);
    v.push_back( x        & 0xFF);
}
static void be64(std::vector<uint8_t>& v, uint64_t x) {
    be32(v, (uint32_t)(x >> 32));
    be32(v, (uint32_t)(x & 0xFFFFFFFFu));
}
static void pad(std::vector<uint8_t>& v, int n) {
    for (int i = 0; i < n; ++i) v.push_back(0);
}

// Build a conventional TraceAttribute
static std::vector<uint8_t> make_conv_ta(int16_t n_items, uint32_t sel_flags,
                                          const std::vector<uint8_t>& opt_hdr,
                                          const std::vector<uint8_t>& periodic)
{
    std::vector<uint8_t> ta;
    // n_items (INT16 BE) + reserved + opt_len (UINT8)
    be16(ta, (uint16_t)n_items);
    be8(ta, 0);
    be8(ta, (uint8_t)opt_hdr.size());
    be32(ta, sel_flags);
    ta.insert(ta.end(), opt_hdr.begin(), opt_hdr.end());
    ta.insert(ta.end(), periodic.begin(), periodic.end());
    return ta;
}

// Build an advanced TraceAttribute
static std::vector<uint8_t> make_adv_ta(uint32_t n_items, uint32_t flags_lo,
                                         uint32_t flags_hi,
                                         const std::vector<uint8_t>& opt_hdr,
                                         const std::vector<uint8_t>& periodic)
{
    std::vector<uint8_t> ta;
    be32(ta, n_items);
    pad(ta, 4);                         // reserved
    be32(ta, (uint32_t)opt_hdr.size()); // opt_hdr_len
    be32(ta, flags_lo);
    be32(ta, flags_hi);
    pad(ta, 16);                        // 4×UINT32 reserved
    ta.insert(ta.end(), opt_hdr.begin(), opt_hdr.end());
    ta.insert(ta.end(), periodic.begin(), periodic.end());
    return ta;
}

// Build a complete EB200 packet around a TraceData blob
static std::vector<uint8_t> make_eb200(uint16_t trace_tag,
                                        const std::vector<uint8_t>& trace_data)
{
    bool advanced = (trace_tag >= 5000);

    // Generic Attribute
    std::vector<uint8_t> ga;
    be16(ga, trace_tag);
    if (!advanced) {
        be16(ga, (uint16_t)trace_data.size());
    } else {
        pad(ga, 2);                             // reserved
        be32(ga, (uint32_t)trace_data.size());
        pad(ga, 16);                            // 4×UINT32 reserved
    }
    ga.insert(ga.end(), trace_data.begin(), trace_data.end());

    uint32_t data_size = 16u + (uint32_t)ga.size();

    std::vector<uint8_t> pkt;
    be32(pkt, 0x000EB200u);  // magic
    be16(pkt, 1u);           // version minor
    be16(pkt, 2u);           // version major = 2
    be16(pkt, 7u);           // seq num low
    be16(pkt, 0u);           // seq num high
    be32(pkt, data_size);
    pkt.insert(pkt.end(), ga.begin(), ga.end());
    return pkt;
}

// Build a 42-byte Audio optional header (Table 12)
static std::vector<uint8_t> make_audio_opt_hdr(int16_t  audio_mode,
                                                int16_t  frame_len_bytes,
                                                uint64_t freq_hz,
                                                uint32_t bandwidth_hz,
                                                uint16_t demod,
                                                const char* demod_str_ascii,
                                                uint64_t timestamp_ns,
                                                int16_t  sig_source)
{
    std::vector<uint8_t> h;
    be16(h, (uint16_t)audio_mode);
    be16(h, (uint16_t)frame_len_bytes);
    be32(h, (uint32_t)(freq_hz & 0xFFFFFFFFu));   // Freq_low
    be32(h, bandwidth_hz);
    be16(h, demod);
    // DemodulationString: 8 bytes, left-aligned, NUL-padded
    char dstr[8] = {};
    if (demod_str_ascii) std::strncpy(dstr, demod_str_ascii, 8);
    for (int i = 0; i < 8; ++i) be8(h, (uint8_t)dstr[i]);
    be32(h, (uint32_t)(freq_hz >> 32));  // Freq_high
    pad(h, 6);                            // reserved
    be64(h, timestamp_ns);
    be16(h, (uint16_t)sig_source);
    assert(h.size() == 42);
    return h;
}

// Wrap raw XML bytes in the DDF-550 binary envelope.
// Uses non-EB200 magic (0xABCD1234 / 0xDCBA4321) — any value works for the
// content-based heuristic in extract_frame.
static std::vector<uint8_t> wrap_xml(const char* xml_str) {
    std::vector<uint8_t> v;
    uint32_t n = (uint32_t)strlen(xml_str);
    be32(v, 0xABCD1234u);  // magic_start (placeholder)
    be32(v, n);            // XML length
    for (const char* p = xml_str; *p; ++p) be8(v, (uint8_t)*p);
    be32(v, 0xDCBA4321u);  // magic_end (placeholder)
    return v;
}

// Convert C-string to raw byte vector (no wrapper)
static std::vector<uint8_t> raw_bytes(const char* s) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(s);
    return std::vector<uint8_t>(p, p + strlen(s));
}

// Check that JSON string contains a substring
static bool json_has(const std::string& json, const char* needle) {
    return json.find(needle) != std::string::npos;
}

// ---------------------------------------------------------------------------
// ABI-correct call wrappers (sdfc_abi.h: malloc'd out-params, 0/-1 return)
// ---------------------------------------------------------------------------

static bool try_extract(const uint8_t* buf, size_t len, std::vector<uint8_t>& out_frame) {
    uint8_t* frame  = nullptr;
    size_t   fLen   = 0;
    int rc = extract_frame(buf, len, &frame, &fLen);
    if (rc != 0) return false;
    out_frame.assign(frame, frame + fLen);
    free_result(frame);
    return true;
}

static bool try_parse(const uint8_t* frame, size_t len, std::string& out_json) {
    char*  json = nullptr;
    size_t jLen = 0;
    int rc = parse_message(frame, len, &json, &jLen);
    if (rc != 0) return false;
    out_json.assign(json, jLen);
    free_result(json);
    return true;
}

static bool try_format(const char* kind, const char* kwargs_json, std::vector<uint8_t>& out_wire) {
    uint8_t* buf  = nullptr;
    size_t   bLen = 0;
    int rc = format_response(kind, kwargs_json, &buf, &bLen);
    if (rc != 0) return false;
    out_wire.assign(buf, buf + bLen);
    free_result(buf);
    return true;
}

// ---------------------------------------------------------------------------
// Test infrastructure: lightweight check counter
// ---------------------------------------------------------------------------

static int g_checks = 0;
static int g_fails  = 0;

static void check(bool cond, const char* expr, const char* file, int line) {
    ++g_checks;
    if (!cond) {
        ++g_fails;
        std::fprintf(stderr, "FAIL %s:%d  %s\n", file, line, expr);
    }
}
#define CHECK(x) check((x), #x, __FILE__, __LINE__)

// ---------------------------------------------------------------------------
// Test: EB200 Audio trace — full optional header decode
// ---------------------------------------------------------------------------

static void test_eb200_audio() {
    // Build Audio optional header: FM, 100 MHz, 150 kHz BW, 32kHz 16bit mono (mode 2)
    auto opt_hdr = make_audio_opt_hdr(
        /*mode=*/2, /*frame_len=*/2, /*freq=*/100000000ULL,
        /*bw=*/150000, /*demod=*/0, /*str=*/"FM",
        /*ts_ns=*/1718000000000000000ULL, /*src=*/0);

    // Two audio samples (2 bytes each), arbitrary values
    std::vector<uint8_t> periodic = {0x12, 0x34, 0x56, 0x78};

    // sel_flags = SEL_OPTIONAL_HEADER (0x80000000)
    auto ta = make_conv_ta(/*n_items=*/2, 0x80000000u, opt_hdr, periodic);
    auto pkt = make_eb200(/*tag=*/401, ta);

    std::vector<uint8_t> frame;
    CHECK(try_extract(pkt.data(), pkt.size(), frame));
    CHECK(frame.size() == pkt.size());

    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));
    CHECK(json_has(json, "\"hw\":\"ddf550\""));
    CHECK(json_has(json, "\"stream\":\"eb200\""));
    CHECK(json_has(json, "\"trace_tag\":401"));
    CHECK(json_has(json, "\"tag_name\":\"audio\""));
    CHECK(json_has(json, "\"n_items\":2"));
    CHECK(json_has(json, "\"audio_mode\":2"));
    CHECK(json_has(json, "\"frame_length\":2"));
    CHECK(json_has(json, "\"freq_hz\":100000000"));
    CHECK(json_has(json, "\"bandwidth_hz\":150000"));
    CHECK(json_has(json, "\"demod\":0"));
    CHECK(json_has(json, "\"demod_str\":\"FM\""));
    CHECK(json_has(json, "\"demod_name\":\"FM\""));
    CHECK(json_has(json, "\"signal_source\":0"));
    CHECK(json_has(json, "\"periodic_data_bytes\":4"));
}

// ---------------------------------------------------------------------------
// Test: EB200 IFPan (tag 501, conventional, no optional header)
// ---------------------------------------------------------------------------

static void test_eb200_ifpan() {
    // sel_flags = LEVEL(0x1) | OFFSET(0x2) = 0x3
    // Each item: LEVEL(INT16=2B) + OFFSET(INT32=4B) = 6 bytes; n=3 → 18 bytes
    std::vector<uint8_t> periodic;
    for (int i = 0; i < 3; ++i) {
        be16(periodic, (uint16_t)(500 + i * 10));  // LEVEL: 500, 510, 520 (×0.1 dBµV)
        be32(periodic, (uint32_t)(i * 1000));       // OFFSET: 0, 1000, 2000 Hz
    }
    auto ta  = make_conv_ta(3, 0x00000003u, {}, periodic);
    auto pkt = make_eb200(501, ta);

    std::vector<uint8_t> frame;
    CHECK(try_extract(pkt.data(), pkt.size(), frame));

    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));
    CHECK(json_has(json, "\"trace_tag\":501"));
    CHECK(json_has(json, "\"tag_name\":\"ifpan\""));
    CHECK(json_has(json, "\"n_items\":3"));
    CHECK(json_has(json, "\"sel_flags\":3"));
    CHECK(json_has(json, "\"opt_hdr_len\":0"));
    CHECK(json_has(json, "\"periodic_data_bytes\":18"));
}

// ---------------------------------------------------------------------------
// Test: EB200 PScan (tag 1201, conventional)
// ---------------------------------------------------------------------------

static void test_eb200_pscan() {
    std::vector<uint8_t> periodic;
    be16(periodic, 600u);  // one LEVEL item
    auto ta  = make_conv_ta(1, 0x00000001u, {}, periodic);
    auto pkt = make_eb200(1201, ta);

    std::vector<uint8_t> frame;
    CHECK(try_extract(pkt.data(), pkt.size(), frame));

    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));
    CHECK(json_has(json, "\"tag_name\":\"pscan\""));
    CHECK(json_has(json, "\"trace_tag\":1201"));
}

// ---------------------------------------------------------------------------
// Test: EB200 DFPScan (tag 5301, ADVANCED format)
// ---------------------------------------------------------------------------

static void test_eb200_dfpscan_advanced() {
    // sel_flags_lo = LEVEL(0x1) | AZIMUTH(0x1000) = 0x1001
    // Each item: INT16(LEVEL) + INT16(AZIMUTH) = 4 bytes; n=3 → 12 bytes
    std::vector<uint8_t> periodic;
    for (int i = 0; i < 3; ++i) {
        be16(periodic, (uint16_t)(450 + i * 5));  // LEVEL (×0.1 dBµV)
        be16(periodic, (uint16_t)(900 + i * 10)); // AZIMUTH (×0.1°) = 90°, 91°, 92°
    }
    auto ta  = make_adv_ta(3, 0x00001001u, 0u, {}, periodic);
    auto pkt = make_eb200(5301, ta);

    std::vector<uint8_t> frame;
    CHECK(try_extract(pkt.data(), pkt.size(), frame));

    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));
    CHECK(json_has(json, "\"tag_name\":\"dfpscan\""));
    CHECK(json_has(json, "\"trace_tag\":5301"));
    CHECK(json_has(json, "\"n_items\":3"));
    CHECK(json_has(json, "\"sel_flags\":4097"));   // 0x1001 = 4097
    CHECK(json_has(json, "\"periodic_data_bytes\":12"));
}

// ---------------------------------------------------------------------------
// Test: EB200 SigP (tag 5501, advanced, n_items=0, no periodic data)
// ---------------------------------------------------------------------------

static void test_eb200_sigp_advanced() {
    auto ta  = make_adv_ta(0, 0u, 0u, {}, {});
    auto pkt = make_eb200(5501, ta);

    std::vector<uint8_t> frame;
    CHECK(try_extract(pkt.data(), pkt.size(), frame));

    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));
    CHECK(json_has(json, "\"tag_name\":\"sigp\""));
    CHECK(json_has(json, "\"trace_tag\":5501"));
}

// ---------------------------------------------------------------------------
// Test: EB200 CW (tag 801, conventional)
// ---------------------------------------------------------------------------

static void test_eb200_cw() {
    std::vector<uint8_t> periodic;
    be16(periodic, 700u);  // one LEVEL item
    auto ta  = make_conv_ta(1, 0x00000001u, {}, periodic);
    auto pkt = make_eb200(801, ta);

    std::vector<uint8_t> frame;
    CHECK(try_extract(pkt.data(), pkt.size(), frame));

    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));
    CHECK(json_has(json, "\"tag_name\":\"cw\""));
}

// ---------------------------------------------------------------------------
// Test: EB200 truncated header → no complete frame (-1)
// ---------------------------------------------------------------------------

static void test_eb200_truncated_header() {
    // Only 10 bytes — not enough for the 16-byte EB200 header
    std::vector<uint8_t> buf;
    be32(buf, 0x000EB200u);
    be16(buf, 1u); be16(buf, 2u); be16(buf, 1u);  // 10 bytes total

    std::vector<uint8_t> frame;
    CHECK(!try_extract(buf.data(), buf.size(), frame));
}

// ---------------------------------------------------------------------------
// Test: EB200 DataSize too small → no complete frame (-1)
// ---------------------------------------------------------------------------

static void test_eb200_bad_datasize() {
    std::vector<uint8_t> buf;
    be32(buf, 0x000EB200u);
    be16(buf, 1u); be16(buf, 2u); be16(buf, 1u); be16(buf, 0u);
    be32(buf, 4u);  // DataSize=4 — less than EB200_HDR_BYTES(16), corrupt

    std::vector<uint8_t> frame;
    CHECK(!try_extract(buf.data(), buf.size(), frame));
}

// ---------------------------------------------------------------------------
// Test: EB200 valid header but data not yet arrived → no complete frame (-1)
//
// Current sdfc_abi.h contract collapses "incomplete" and "corrupt" into a
// single -1 return (see icd-open-questions.md D5 — EB200 does not yet
// distinguish "wait for more bytes" from "corrupt"; out of scope for this
// pass, tracked separately). This test asserts today's actual behavior.
// ---------------------------------------------------------------------------

static void test_eb200_data_incomplete() {
    auto ta  = make_conv_ta(1, 0u, {}, {0x00, 0x00});
    auto pkt = make_eb200(501, ta);

    std::vector<uint8_t> frame;
    // Provide only first 20 bytes of a 38-byte packet
    CHECK(!try_extract(pkt.data(), 20, frame));
}

// ---------------------------------------------------------------------------
// Test: Non-EB200 magic, non-XML garbage → no complete frame (-1)
// ---------------------------------------------------------------------------

static void test_garbage_input() {
    uint8_t buf[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01, 0x02, 0x03,
                      0x55, 0x66, 0x77, 0x88 };
    std::vector<uint8_t> frame;
    // buf[8]=0x55 (not '<') so neither EB200 nor wrapped XML path matches
    CHECK(!try_extract(buf, sizeof(buf), frame));
}

// ---------------------------------------------------------------------------
// Test: Wrapped XML <Request> → command
// ---------------------------------------------------------------------------

static void test_wrapped_xml_request() {
    const char* xml =
        "<Request type=\"set\" id=\"42\">"
        "<Command name=\"DfMode\">"
        "<Param name=\"eOperationMode\">DFMODE_FFM</Param>"
        "</Command></Request>";
    auto frame_bytes = wrap_xml(xml);

    std::vector<uint8_t> frame;
    CHECK(try_extract(frame_bytes.data(), frame_bytes.size(), frame));

    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));
    CHECK(json_has(json, "\"hw\":\"ddf550\""));
    CHECK(json_has(json, "\"channel\":\"control\""));
    CHECK(json_has(json, "\"msg_kind\":\"request\""));
    // Generic mirror: id/type are attributes of <Request>, nested under
    // body.request; command name and the single Param are nested further
    // under body.request.command / .command.param.
    CHECK(json_has(json, "\"body\":{\"request\":{\"type\":\"set\",\"id\":\"42\""));
    CHECK(json_has(json, "\"command\":{\"name\":\"DfMode\""));
    CHECK(json_has(json, "\"param\":{\"name\":\"eOperationMode\",\"#text\":\"DFMODE_FFM\"}"));
}

// ---------------------------------------------------------------------------
// Test: Wrapped XML <Reply> → response
// ---------------------------------------------------------------------------

static void test_wrapped_xml_reply() {
    const char* xml =
        "<Reply type=\"set\" id=\"42\">"
        "<Command name=\"DfMode\">"
        "</Command></Reply>";
    auto frame_bytes = wrap_xml(xml);

    std::vector<uint8_t> frame;
    CHECK(try_extract(frame_bytes.data(), frame_bytes.size(), frame));

    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));
    CHECK(json_has(json, "\"msg_kind\":\"reply\""));
    // Command has an attribute (name) but no Param children and no direct
    // text, so it mirrors to just {"name":"DfMode"} — no "param" key at all.
    CHECK(json_has(json, "\"command\":{\"name\":\"DfMode\"}"));
}

// ---------------------------------------------------------------------------
// Test: Wrapped XML DfMode GET — request has no params, reply echoes mode
// ---------------------------------------------------------------------------

static void test_wrapped_xml_dfmode_get_request() {
    const char* xml =
        "<Request type=\"get\" id=\"50\">"
        "<Command name=\"DfMode\">"
        "</Command></Request>";
    auto frame_bytes = wrap_xml(xml);

    std::vector<uint8_t> frame;
    CHECK(try_extract(frame_bytes.data(), frame_bytes.size(), frame));

    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));
    CHECK(json_has(json, "\"msg_kind\":\"request\""));
    CHECK(json_has(json, "\"type\":\"get\""));
    // No Param children/text under Command → mirrors to just {"name":...},
    // i.e. no "param" key present at all (the new shape's equivalent of the
    // old curated "params":{}).
    CHECK(json_has(json, "\"command\":{\"name\":\"DfMode\"}"));
    CHECK(!json_has(json, "\"param\""));
}

static void test_wrapped_xml_dfmode_get_reply() {
    const char* xml =
        "<Reply type=\"get\" id=\"50\">"
        "<Command name=\"DfMode\">"
        "<Param name=\"eOperationMode\">DFMODE_SCAN</Param>"
        "</Command></Reply>";
    auto frame_bytes = wrap_xml(xml);

    std::vector<uint8_t> frame;
    CHECK(try_extract(frame_bytes.data(), frame_bytes.size(), frame));

    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));
    CHECK(json_has(json, "\"msg_kind\":\"reply\""));
    CHECK(json_has(json, "\"type\":\"get\""));
    CHECK(json_has(json, "\"command\":{\"name\":\"DfMode\""));
    CHECK(json_has(json, "\"param\":{\"name\":\"eOperationMode\",\"#text\":\"DFMODE_SCAN\"}"));
}

// ---------------------------------------------------------------------------
// Test: Wrapped XML MeasureSettingsFFM with iFrequency
// ---------------------------------------------------------------------------

static void test_wrapped_xml_freq() {
    const char* xml =
        "<Request type=\"set\" id=\"1\">"
        "<Command name=\"MeasureSettingsFFM\">"
        "<Param name=\"iFrequency\">145000000</Param>"
        "</Command></Request>";
    auto frame_bytes = wrap_xml(xml);

    std::vector<uint8_t> frame;
    CHECK(try_extract(frame_bytes.data(), frame_bytes.size(), frame));

    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));
    CHECK(json_has(json, "\"command\":{\"name\":\"MeasureSettingsFFM\""));
    // All values are now strings under the generic mirror — no int coercion.
    CHECK(json_has(json, "\"param\":{\"name\":\"iFrequency\",\"#text\":\"145000000\"}"));
}

// ---------------------------------------------------------------------------
// Test: Wrapped XML MeasureSettingsFFM GET — request has no params, reply
// echoes the current measurement settings
// ---------------------------------------------------------------------------

static void test_wrapped_xml_measuresettingsffm_get_request() {
    const char* xml =
        "<Request type=\"get\" id=\"51\">"
        "<Command name=\"MeasureSettingsFFM\">"
        "</Command></Request>";
    auto frame_bytes = wrap_xml(xml);

    std::vector<uint8_t> frame;
    CHECK(try_extract(frame_bytes.data(), frame_bytes.size(), frame));

    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));
    CHECK(json_has(json, "\"type\":\"get\""));
    CHECK(json_has(json, "\"command\":{\"name\":\"MeasureSettingsFFM\"}"));
    CHECK(!json_has(json, "\"param\""));
}

static void test_wrapped_xml_measuresettingsffm_get_reply() {
    const char* xml =
        "<Reply type=\"get\" id=\"51\">"
        "<Command name=\"MeasureSettingsFFM\">"
        "<Param name=\"iFrequency\">145000000</Param>"
        "<Param name=\"iBandwidth\">12500</Param>"
        "</Command></Reply>";
    auto frame_bytes = wrap_xml(xml);

    std::vector<uint8_t> frame;
    CHECK(try_extract(frame_bytes.data(), frame_bytes.size(), frame));

    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));
    CHECK(json_has(json, "\"msg_kind\":\"reply\""));
    CHECK(json_has(json, "\"command\":{\"name\":\"MeasureSettingsFFM\""));
    // Two sibling Params under the same Command → a "param" JSON array
    // containing both mirrored values.
    CHECK(json_has(json, "\"param\":["));
    CHECK(json_has(json, "\"name\":\"iFrequency\",\"#text\":\"145000000\""));
    CHECK(json_has(json, "\"name\":\"iBandwidth\",\"#text\":\"12500\""));
}

// ---------------------------------------------------------------------------
// Test: Wrapped XML AudioMode GET
// ---------------------------------------------------------------------------

static void test_wrapped_xml_audiomode() {
    const char* xml =
        "<Request type=\"get\" id=\"5\">"
        "<Command name=\"AudioMode\">"
        "<Param name=\"eAudioMode\">AUDIO_MODE_32KHZ_16BIT_MONO</Param>"
        "</Command></Request>";
    auto frame_bytes = wrap_xml(xml);

    std::vector<uint8_t> frame;
    CHECK(try_extract(frame_bytes.data(), frame_bytes.size(), frame));

    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));
    CHECK(json_has(json, "\"command\":{\"name\":\"AudioMode\""));
    CHECK(json_has(json, "\"name\":\"eAudioMode\",\"#text\":\"AUDIO_MODE_32KHZ_16BIT_MONO\""));
}

// ---------------------------------------------------------------------------
// Test: Wrapped XML TraceEnable command
// ---------------------------------------------------------------------------

static void test_wrapped_xml_trace_enable() {
    const char* xml =
        "<Request type=\"set\" id=\"7\">"
        "<Command name=\"TraceEnable\">"
        "<Param name=\"eTraceTag\">TRACETAG_AUDIO</Param>"
        "<Param name=\"zIP\">192.168.1.100</Param>"
        "<Param name=\"iPort\">9152</Param>"
        "</Command></Request>";
    auto frame_bytes = wrap_xml(xml);

    std::vector<uint8_t> frame;
    CHECK(try_extract(frame_bytes.data(), frame_bytes.size(), frame));

    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));
    CHECK(json_has(json, "\"command\":{\"name\":\"TraceEnable\""));
    CHECK(json_has(json, "\"param\":["));
    CHECK(json_has(json, "\"name\":\"eTraceTag\",\"#text\":\"TRACETAG_AUDIO\""));
    CHECK(json_has(json, "\"name\":\"zIP\",\"#text\":\"192.168.1.100\""));
    // iPort's numeric-looking value stays a JSON string under the mirror.
    CHECK(json_has(json, "\"name\":\"iPort\",\"#text\":\"9152\""));
}

// ---------------------------------------------------------------------------
// Test: Wrapped XML TraceDisable command
// ---------------------------------------------------------------------------

static void test_wrapped_xml_trace_disable() {
    const char* xml =
        "<Request type=\"set\" id=\"8\">"
        "<Command name=\"TraceDisable\">"
        "<Param name=\"eTraceTag\">TRACETAG_AUDIO</Param>"
        "</Command></Request>";
    auto frame_bytes = wrap_xml(xml);

    std::vector<uint8_t> frame;
    CHECK(try_extract(frame_bytes.data(), frame_bytes.size(), frame));

    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));
    CHECK(json_has(json, "\"command\":{\"name\":\"TraceDisable\""));
    // Single Param → a "param" object (not an array).
    CHECK(json_has(json, "\"param\":{\"name\":\"eTraceTag\",\"#text\":\"TRACETAG_AUDIO\"}"));
}

// ---------------------------------------------------------------------------
// Test: Wrapped XML TraceDelete command — tears down the whole trace
// connection (identified by the same zIP/iPort pair TraceEnable set up)
// ---------------------------------------------------------------------------

static void test_wrapped_xml_trace_delete() {
    const char* xml =
        "<Request type=\"set\" id=\"9\">"
        "<Command name=\"TraceDelete\">"
        "<Param name=\"zIP\">192.168.1.100</Param>"
        "<Param name=\"iPort\">9152</Param>"
        "</Command></Request>";
    auto frame_bytes = wrap_xml(xml);

    std::vector<uint8_t> frame;
    CHECK(try_extract(frame_bytes.data(), frame_bytes.size(), frame));

    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));
    CHECK(json_has(json, "\"command\":{\"name\":\"TraceDelete\""));
    CHECK(json_has(json, "\"param\":["));
    CHECK(json_has(json, "\"name\":\"zIP\",\"#text\":\"192.168.1.100\""));
    CHECK(json_has(json, "\"name\":\"iPort\",\"#text\":\"9152\""));
}

// ---------------------------------------------------------------------------
// Test: DemodulationSettings — 13 params, only 1 (eDemodulation) was ever on
// the old hardcoded whitelist. Proves generic capture: all 13 sibling Params
// end up as a "param" array nested under body.request.command, each mirrored
// to {"name":...,"#text":...}. Under the generic mirror ALL values are JSON
// strings (no int/bool coercion) — this also pins that the former
// int/bool-typed old-shape params (iBfoFrequency, bAfc, etc.) are now plain
// strings like everything else.
// ---------------------------------------------------------------------------

static void test_wrapped_xml_demodulation_settings() {
    const char* xml =
        "<Request type=\"set\" id=\"123\">"
        "<Command name=\"DemodulationSettings\">"
        "<Param name=\"eDemodulation\">MOD_FM</Param>"
        "<Param name=\"iBfoFrequency\">1</Param>"
        "<Param name=\"iAfFrequency\">1</Param>"
        "<Param name=\"eAfBandwidth\">BW_25</Param>"
        "<Param name=\"iAfThreshold\">1</Param>"
        "<Param name=\"bUseAfThreshold\">true</Param>"
        "<Param name=\"iPassbandFrequency\">1</Param>"
        "<Param name=\"eLevelIndicator\">LEVEL_INDICATOR_RMS</Param>"
        "<Param name=\"bAfc\">true</Param>"
        "<Param name=\"eGainSelect\">GAIN_AUTO</Param>"
        "<Param name=\"iGainValue\">5</Param>"
        "<Param name=\"eGainTiming\">GC_FAST</Param>"
        "<Param name=\"bStereoDecoder\">false</Param>"
        "</Command></Request>";
    auto frame_bytes = wrap_xml(xml);

    std::vector<uint8_t> frame;
    CHECK(try_extract(frame_bytes.data(), frame_bytes.size(), frame));

    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));
    CHECK(json_has(json, "\"command\":{\"name\":\"DemodulationSettings\""));
    CHECK(json_has(json, "\"param\":["));
    CHECK(json_has(json, "\"name\":\"eDemodulation\",\"#text\":\"MOD_FM\""));
    CHECK(json_has(json, "\"name\":\"iBfoFrequency\",\"#text\":\"1\""));
    CHECK(json_has(json, "\"name\":\"iAfFrequency\",\"#text\":\"1\""));
    CHECK(json_has(json, "\"name\":\"eAfBandwidth\",\"#text\":\"BW_25\""));
    CHECK(json_has(json, "\"name\":\"iAfThreshold\",\"#text\":\"1\""));
    CHECK(json_has(json, "\"name\":\"bUseAfThreshold\",\"#text\":\"true\""));
    CHECK(json_has(json, "\"name\":\"iPassbandFrequency\",\"#text\":\"1\""));
    CHECK(json_has(json, "\"name\":\"eLevelIndicator\",\"#text\":\"LEVEL_INDICATOR_RMS\""));
    CHECK(json_has(json, "\"name\":\"bAfc\",\"#text\":\"true\""));
    CHECK(json_has(json, "\"name\":\"eGainSelect\",\"#text\":\"GAIN_AUTO\""));
    CHECK(json_has(json, "\"name\":\"iGainValue\",\"#text\":\"5\""));
    CHECK(json_has(json, "\"name\":\"eGainTiming\",\"#text\":\"GC_FAST\""));
    CHECK(json_has(json, "\"name\":\"bStereoDecoder\",\"#text\":\"false\""));
}

// ---------------------------------------------------------------------------
// Test: <Param> value containing an XML entity reference is decoded, not
// left as raw bytes -- pins the disclosed, accepted pugixml behavior change
// from design spec §3.2 (xml-parsing-pugixml-migration-design.md): pugixml's
// default parse flags decode XML entities (&amp; -> &), whereas the deleted
// hand-rolled scanning helpers (xml_attr/xml_text/xml_param_value) returned
// raw, undecoded substring bytes verbatim. This is NOT testing a bug -- it
// confirms the accepted, disclosed new behavior stays stable going forward.
//
// Verified empirically against the vendored pugixml build (single decode
// pass, standard XML semantics): a single-escaped entity "&amp;" decodes to
// "&". A *double*-escaped "&amp;amp;" decodes to the literal text "&amp;"
// (only the outer entity is consumed), not to a bare "&" -- so this test
// uses one level of escaping to exactly match the design spec's own example
// (§3.2: "decode XML entities (&amp; -> &)").
// ---------------------------------------------------------------------------

static void test_param_value_entity_decoded() {
    const char* xml =
        "<Request type=\"set\" id=\"1\">"
        "<Command name=\"TestCmd\">"
        "<Param name=\"zNote\">A &amp; B</Param>"
        "</Command></Request>";
    auto frame_bytes = wrap_xml(xml);

    std::vector<uint8_t> frame;
    CHECK(try_extract(frame_bytes.data(), frame_bytes.size(), frame));

    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));
    // Decoded value: pugixml turned the "&amp;" entity into a literal "&".
    // Nested under body.request.command.param, mirrored to {"name":...,"#text":...}.
    CHECK(json_has(json, "\"param\":{\"name\":\"zNote\",\"#text\":\"A & B\"}"));
    // Discriminating: the raw, undecoded entity text must NOT survive into
    // the JSON -- that would mean entity decoding silently regressed back to
    // the old hand-rolled scanner's raw-bytes-verbatim behavior.
    CHECK(!json_has(json, "A &amp; B"));
}

// ---------------------------------------------------------------------------
// Test: Wrapped XML ScanRangeAdd command
// ---------------------------------------------------------------------------

static void test_wrapped_xml_scanrange_add() {
    const char* xml =
        "<Request type=\"set\" id=\"20\">"
        "<Command name=\"ScanRangeAdd\">"
        "<Param name=\"iStartFrequency\">30000000</Param>"
        "<Param name=\"iStopFrequency\">88000000</Param>"
        "<Param name=\"iStepFrequency\">25000</Param>"
        "</Command></Request>";
    auto frame_bytes = wrap_xml(xml);

    std::vector<uint8_t> frame;
    CHECK(try_extract(frame_bytes.data(), frame_bytes.size(), frame));

    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));
    CHECK(json_has(json, "\"command\":{\"name\":\"ScanRangeAdd\""));
    CHECK(json_has(json, "\"param\":["));
    CHECK(json_has(json, "\"name\":\"iStartFrequency\",\"#text\":\"30000000\""));
    CHECK(json_has(json, "\"name\":\"iStopFrequency\",\"#text\":\"88000000\""));
    CHECK(json_has(json, "\"name\":\"iStepFrequency\",\"#text\":\"25000\""));
}

// ---------------------------------------------------------------------------
// Test: Wrapped XML ScanRangeDeleteAll command — no params, clears every
// previously added scan range
// ---------------------------------------------------------------------------

static void test_wrapped_xml_scanrange_delete_all() {
    const char* xml =
        "<Request type=\"set\" id=\"21\">"
        "<Command name=\"ScanRangeDeleteAll\">"
        "</Command></Request>";
    auto frame_bytes = wrap_xml(xml);

    std::vector<uint8_t> frame;
    CHECK(try_extract(frame_bytes.data(), frame_bytes.size(), frame));

    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));
    // No Param children/text under Command → mirrors to just {"name":...},
    // i.e. no "param" key present at all.
    CHECK(json_has(json, "\"command\":{\"name\":\"ScanRangeDeleteAll\"}"));
    CHECK(!json_has(json, "\"param\""));
}

// ---------------------------------------------------------------------------
// Test: a self-closing <Param name="X"/> must not swallow a later param's
// closing tag (the gap flagged before implementing this fix).
// ---------------------------------------------------------------------------

static void test_xml_param_self_closing_does_not_corrupt_scan() {
    const char* xml =
        "<Request type=\"set\" id=\"1\">"
        "<Command name=\"TestCmd\">"
        "<Param name=\"iEmpty\"/>"
        "<Param name=\"eNext\">RealValue</Param>"
        "</Command></Request>";
    auto frame_bytes = wrap_xml(xml);

    std::vector<uint8_t> frame;
    CHECK(try_extract(frame_bytes.data(), frame_bytes.size(), frame));

    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));
    // Both Params must appear in the "param" array in order: the self-closing
    // <Param name="iEmpty"/> (no #text, since it has no text) followed by
    // <Param name="eNext">RealValue</Param>. If the self-closing tag had
    // corrupted the scan, eNext's value would be missing or mangled.
    CHECK(json_has(json, "\"param\":[{\"name\":\"iEmpty\"}"));
    CHECK(json_has(json, "\"name\":\"eNext\",\"#text\":\"RealValue\""));
}

// ---------------------------------------------------------------------------
// Test: Wrapped XML incomplete → no complete frame (-1)
// ---------------------------------------------------------------------------

static void test_wrapped_xml_incomplete() {
    const char* xml = "<Request type=\"set\" id=\"1\"><Command name=\"DfMode\"></Command></Request>";
    auto frame_bytes = wrap_xml(xml);

    std::vector<uint8_t> frame;
    // Provide only half the wrapped frame
    size_t half = frame_bytes.size() / 2;
    CHECK(!try_extract(frame_bytes.data(), half, frame));
}

// ---------------------------------------------------------------------------
// Test: Wrapped XML length says 100 bytes but packet ends — no complete frame (-1)
// ---------------------------------------------------------------------------

static void test_wrapped_xml_partial_body() {
    std::vector<uint8_t> v;
    be32(v, 0xABCD1234u);  // magic_start
    be32(v, 100u);          // claims 100 bytes of XML
    // Only 5 XML bytes follow
    be8(v, '<'); be8(v, 'R'); be8(v, 'e'); be8(v, 'q'); be8(v, '>');

    std::vector<uint8_t> frame;
    CHECK(!try_extract(v.data(), v.size(), frame));
}

// ---------------------------------------------------------------------------
// Test: <Event> async notification frame (D6) → recognised, msg_kind=event
// ---------------------------------------------------------------------------

static void test_wrapped_xml_event() {
    const char* xml =
        "<Event type=\"notify\" id=\"1\">"
        "<Command name=\"ScanComplete\">"
        "</Command></Event>";
    auto frame_bytes = wrap_xml(xml);

    std::vector<uint8_t> frame;
    CHECK(try_extract(frame_bytes.data(), frame_bytes.size(), frame));

    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));
    CHECK(json_has(json, "\"msg_kind\":\"event\""));
    CHECK(json_has(json, "\"command\":{\"name\":\"ScanComplete\"}"));
}

// ---------------------------------------------------------------------------
// Test: <DFSelect> preclassifier filter command (port 9153, per icd-ddf550.md
// §4) → recognised, classified as a preclassifier request
// ---------------------------------------------------------------------------

static void test_raw_xml_dfselect() {
    const char* xml =
        "<DFSelect>"
        "<EmitterClass>Hopper</EmitterClass>"
        "<EmitterClass>Burst</EmitterClass>"
        "</DFSelect>";
    auto frame_bytes = raw_bytes(xml);

    std::vector<uint8_t> frame;
    CHECK(try_extract(frame_bytes.data(), frame_bytes.size(), frame));

    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));
    CHECK(json_has(json, "\"channel\":\"preclassifier\""));
    CHECK(json_has(json, "\"msg_kind\":\"request\""));
}

// ---------------------------------------------------------------------------
// Test: DDFCLRequest (wrapped, preclassifier control) → command
// ---------------------------------------------------------------------------

static void test_ddfcl_request_wrapped() {
    const char* xml =
        "<?xml version=\"1.0\" ?>"
        "<DDFCLRequest id=\"10\" type=\"set\">"
        "<Command name=\"AnalysisIntervalMs\">50000</Command>"
        "</DDFCLRequest>";
    auto frame_bytes = wrap_xml(xml);

    std::vector<uint8_t> frame;
    CHECK(try_extract(frame_bytes.data(), frame_bytes.size(), frame));

    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));
    CHECK(json_has(json, "\"channel\":\"preclassifier\""));
    CHECK(json_has(json, "\"msg_kind\":\"request\""));
    CHECK(json_has(json, "\"body\":{\"ddfcl_request\":{\"id\":\"10\""));
    // AnalysisIntervalMs's "50000" is a direct-text Command body, not a
    // <Param> — Command has an attribute (name) AND text but no element
    // children, so it mirrors to {"name":...,"#text":...}, same shape as a
    // Param leaf — must still reach a structured field, not just raw_xml.
    CHECK(json_has(json, "\"command\":{\"name\":\"AnalysisIntervalMs\",\"#text\":\"50000\"}"));
}

// ---------------------------------------------------------------------------
// Test: Reply with no <Command> child at all — no "command" key must be
// emitted under body.reply, and decoding must not scan past the frame buffer
// looking for a tag that isn't there (strstr-on-non-null-terminated-buffer
// risk).
// ---------------------------------------------------------------------------

static void test_raw_xml_reply_without_command_tag() {
    const char* xml = "<Reply type=\"get\" id=\"1\"></Reply>";
    auto frame_bytes = raw_bytes(xml);

    std::vector<uint8_t> frame;
    CHECK(try_extract(frame_bytes.data(), frame_bytes.size(), frame));

    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));
    CHECK(json_has(json, "\"msg_kind\":\"reply\""));
    CHECK(json_has(json, "\"body\":{\"reply\":{\"type\":\"get\",\"id\":\"1\"}}"));
    // Under the generic mirror, the old curated keys "command_name"/
    // "command_value" never exist in ANY shape, so checking their absence no
    // longer discriminates real Command-decoding behavior. The real
    // equivalent: with no <Command> child element at all, no "command" key
    // is emitted under body.reply.
    CHECK(!json_has(json, "\"command\":"));
}

// ---------------------------------------------------------------------------
// Test: DDFCLReply (raw, no wrapper) → response
// ---------------------------------------------------------------------------

static void test_ddfcl_reply_raw() {
    const char* xml =
        "<DDFCLReply id=\"10\" type=\"set\">"
        "<Command name=\"AnalysisIntervalMs\">50000</Command>"
        "</DDFCLReply>";
    auto frame_bytes = raw_bytes(xml);

    std::vector<uint8_t> frame;
    CHECK(try_extract(frame_bytes.data(), frame_bytes.size(), frame));

    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));
    CHECK(json_has(json, "\"channel\":\"preclassifier\""));
    CHECK(json_has(json, "\"msg_kind\":\"reply\""));
    CHECK(json_has(json, "\"command\":{\"name\":\"AnalysisIntervalMs\",\"#text\":\"50000\"}"));
}

// ---------------------------------------------------------------------------
// Test: DFData FORMAT02 preclassifier output (raw, port 9154) → dfdata
// ---------------------------------------------------------------------------

static void test_dfdata_format02() {
    const char* xml =
        "<DFData DDF-CL-ID=\"3\">"
        "<EmitterClass>Burst</EmitterClass>"
        "<CenterFrequency Unit=\"Hz\">433920000</CenterFrequency>"
        "<BearingAvg Unit=\"deg\">245.7</BearingAvg>"
        "<LevelAvg Unit=\"dBuV\">56.3</LevelAvg>"
        "</DFData>";
    auto frame_bytes = raw_bytes(xml);

    std::vector<uint8_t> frame;
    CHECK(try_extract(frame_bytes.data(), frame_bytes.size(), frame));

    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));
    CHECK(json_has(json, "\"channel\":\"preclassifier_output\""));
    CHECK(json_has(json, "\"msg_kind\":\"dfdata\""));
    // DDF-CL-ID's hyphens are preserved literally (not folded to underscore)
    // per the mirror's snake_case rule — only "DDF-CL-ID" -> lowercase
    // "ddf-cl-id", hyphens kept as-is.
    CHECK(json_has(json, "\"body\":{\"df_data\":{\"ddf-cl-id\":\"3\""));
    // EmitterClass has no attributes and only text, and appears once here →
    // a plain JSON string, not an object.
    CHECK(json_has(json, "\"emitter_class\":\"Burst\""));
    // Fields with a Unit attribute AND text (CenterFrequency/BearingAvg/
    // LevelAvg) mirror to {"unit":...,"#text":...} — this replaces the old
    // curated separate "fields"/"units" objects with one flat per-field
    // object each.
    CHECK(json_has(json, "\"center_frequency\":{\"unit\":\"Hz\",\"#text\":\"433920000\"}"));
    CHECK(json_has(json, "\"bearing_avg\":{\"unit\":\"deg\",\"#text\":\"245.7\"}"));
    CHECK(json_has(json, "\"level_avg\":{\"unit\":\"dBuV\",\"#text\":\"56.3\"}"));
}

// ---------------------------------------------------------------------------
// Test: DFData with Hopper emitter class
// ---------------------------------------------------------------------------

static void test_dfdata_hopper() {
    const char* xml =
        "<DFData DDF-CL-ID=\"5\">"
        "<EmitterClass>Hopper</EmitterClass>"
        "<StartFrequency Unit=\"Hz\">430000000</StartFrequency>"
        "<StopFrequency Unit=\"Hz\">440000000</StopFrequency>"
        "<BearingAvg Unit=\"deg\">90.0</BearingAvg>"
        "</DFData>";
    auto frame_bytes = raw_bytes(xml);

    std::vector<uint8_t> frame;
    CHECK(try_extract(frame_bytes.data(), frame_bytes.size(), frame));

    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));
    CHECK(json_has(json, "\"emitter_class\":\"Hopper\""));
    // Hopper's frequency RANGE (start/stop), missing before this fix (D7).
    CHECK(json_has(json, "\"start_frequency\":{\"unit\":\"Hz\",\"#text\":\"430000000\"}"));
    CHECK(json_has(json, "\"stop_frequency\":{\"unit\":\"Hz\",\"#text\":\"440000000\"}"));
}

// ---------------------------------------------------------------------------
// Test: DFData with Chirp emitter class (frequency-sweeping, like Hopper)
// ---------------------------------------------------------------------------

static void test_dfdata_chirp() {
    const char* xml =
        "<DFData DDF-CL-ID=\"6\">"
        "<EmitterClass>Chirp</EmitterClass>"
        "<StartFrequency Unit=\"Hz\">100000000</StartFrequency>"
        "<StopFrequency Unit=\"Hz\">108000000</StopFrequency>"
        "<BearingAvg Unit=\"deg\">180.0</BearingAvg>"
        "</DFData>";
    auto frame_bytes = raw_bytes(xml);

    std::vector<uint8_t> frame;
    CHECK(try_extract(frame_bytes.data(), frame_bytes.size(), frame));

    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));
    CHECK(json_has(json, "\"emitter_class\":\"Chirp\""));
    CHECK(json_has(json, "\"start_frequency\":{\"unit\":\"Hz\",\"#text\":\"100000000\"}"));
    CHECK(json_has(json, "\"stop_frequency\":{\"unit\":\"Hz\",\"#text\":\"108000000\"}"));
    CHECK(json_has(json, "\"bearing_avg\":{\"unit\":\"deg\",\"#text\":\"180.0\"}"));
}

// ---------------------------------------------------------------------------
// Test: DFData with whitespace between elements (sanity check: pretty-printed XML)
// ---------------------------------------------------------------------------

static void test_dfdata_with_whitespace() {
    // Pretty-printed XML with newlines and indentation between child elements --
    // confirms pugixml's default parsing (which discards inter-element whitespace,
    // since parse_ws_pcdata/parse_ws_pcdata_single are not set) still extracts all
    // fields correctly. NOTE: this is a plain sanity check, not a regression test
    // for the node_element filter below -- whitespace-only text between elements
    // never becomes a node under pugi::parse_default, so this fixture can't
    // exercise that filter either way. See test_dfdata_with_cdata() for the real
    // regression test (a stray CDATA child is what actually triggers the bug).
    const char* xml =
        "<DFData DDF-CL-ID=\"7\">\n"
        "  <EmitterClass>Burst</EmitterClass>\n"
        "  <CenterFrequency Unit=\"Hz\">450000000</CenterFrequency>\n"
        "  <BearingAvg Unit=\"deg\">120.5</BearingAvg>\n"
        "</DFData>";
    auto frame_bytes = raw_bytes(xml);

    std::vector<uint8_t> frame;
    CHECK(try_extract(frame_bytes.data(), frame_bytes.size(), frame));

    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));
    CHECK(json_has(json, "\"ddf-cl-id\":\"7\""));
    CHECK(json_has(json, "\"emitter_class\":\"Burst\""));
    CHECK(json_has(json, "\"center_frequency\":{\"unit\":\"Hz\",\"#text\":\"450000000\"}"));
    CHECK(json_has(json, "\"bearing_avg\":{\"unit\":\"deg\",\"#text\":\"120.5\"}"));
    // Also confirm no empty-string key sneaks in here either -- though under
    // pugi::parse_default this was never actually at risk for whitespace nodes
    // (see comment above); the genuine proof of the node_element filter is
    // test_dfdata_with_cdata() below.
    CHECK(!json_has(json, "\"\":\""));
}

// ---------------------------------------------------------------------------
// Test: DFData with a stray CDATA child (regression: skip non-element nodes)
// ---------------------------------------------------------------------------

static void test_dfdata_with_cdata() {
    // This is the actual regression test for the node_element filter in the
    // DFData field-extraction loop. Unlike inter-element whitespace (see
    // test_dfdata_with_whitespace() above), pugi::parse_cdata IS on by default,
    // so a stray <![CDATA[...]]> as a direct child of <DFData> parses into a
    // real pugi::node_cdata node with an empty name() and non-empty text().
    // Without the "field.type() != pugi::node_element" filter, this node would
    // be iterated as a "field" and inject a spurious "": "stray" key into the
    // fields JSON object.
    const char* xml =
        "<DFData DDF-CL-ID=\"8\">"
        "<EmitterClass>Hopper</EmitterClass>"
        "<![CDATA[stray]]>"
        "<BearingAvg Unit=\"deg\">45.0</BearingAvg>"
        "</DFData>";
    auto frame_bytes = raw_bytes(xml);

    std::vector<uint8_t> frame;
    CHECK(try_extract(frame_bytes.data(), frame_bytes.size(), frame));

    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));
    CHECK(json_has(json, "\"ddf-cl-id\":\"8\""));
    CHECK(json_has(json, "\"emitter_class\":\"Hopper\""));
    CHECK(json_has(json, "\"bearing_avg\":{\"unit\":\"deg\",\"#text\":\"45.0\"}"));
    // The real proof: no spurious empty-string key from the CDATA node.
    CHECK(!json_has(json, "\"\":\""));
}

// ---------------------------------------------------------------------------
// Test: Raw XML with leading whitespace (Reply) → response
// ---------------------------------------------------------------------------

static void test_raw_xml_leading_ws() {
    const char* xml =
        "   \r\n<Reply type=\"get\" id=\"99\">"
        "<Command name=\"ModuleInfo\"></Command></Reply>";
    auto frame_bytes = raw_bytes(xml);

    std::vector<uint8_t> frame;
    CHECK(try_extract(frame_bytes.data(), frame_bytes.size(), frame));

    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));
    CHECK(json_has(json, "\"msg_kind\":\"reply\""));
    CHECK(json_has(json, "\"command\":{\"name\":\"ModuleInfo\"}"));
}

// ---------------------------------------------------------------------------
// Test: Raw XML without closing tag → no complete frame (-1)
// ---------------------------------------------------------------------------

static void test_raw_xml_no_close() {
    const char* xml = "<Reply type=\"set\" id=\"1\"><Command name=\"DfMode\">";
    auto frame_bytes = raw_bytes(xml);

    std::vector<uint8_t> frame;
    CHECK(!try_extract(frame_bytes.data(), frame_bytes.size(), frame));
}

// ---------------------------------------------------------------------------
// Test: buf_len < 4 → no complete frame (-1)
// ---------------------------------------------------------------------------

static void test_too_short() {
    uint8_t buf[] = {0x00, 0x0E};
    std::vector<uint8_t> frame;
    CHECK(!try_extract(buf, 2, frame));
}

// ---------------------------------------------------------------------------
// Test: format_response — control channel (Request)
// ---------------------------------------------------------------------------

static void test_format_response_control() {
    const char* json_in =
        "{\"msg_type\":\"set\","
        "\"id\":42,"
        "\"command_name\":\"DfMode\","
        "\"xml_body\":\"<Param name=\\\"eOperationMode\\\">DFMODE_FFM</Param>\"}";

    std::vector<uint8_t> wire;
    CHECK(try_format("request", json_in, wire));
    CHECK(wire.size() > 12);  // must have envelope + some XML

    // Check the binary envelope structure: [magic4][len4 BE][xml_bytes][magic4]
    // len at offset 4 (BE) should equal wire.size() - 12
    uint32_t len_field = ((uint32_t)wire[4] << 24) | ((uint32_t)wire[5] << 16) |
                         ((uint32_t)wire[6] << 8)  |  (uint32_t)wire[7];
    CHECK((size_t)len_field == wire.size() - 12);

    // XML content is at offset 8
    std::string xml(reinterpret_cast<const char*>(wire.data() + 8), (size_t)len_field);
    CHECK(xml.find("<Request") != std::string::npos);
    CHECK(xml.find("type=\"set\"") != std::string::npos);
    CHECK(xml.find("id=\"42\"") != std::string::npos);
    CHECK(xml.find("<Command name=\"DfMode\">") != std::string::npos);
    CHECK(xml.find("DFMODE_FFM") != std::string::npos);
    CHECK(xml.find("</Request>") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Test: format_response — preclassifier channel (DDFCLRequest)
// ---------------------------------------------------------------------------

static void test_format_response_preclassifier() {
    const char* json_in =
        "{\"msg_type\":\"set\","
        "\"id\":10,"
        "\"command_name\":\"AnalysisIntervalMs\","
        "\"channel\":\"preclassifier\","
        "\"xml_body\":\"50000\"}";

    std::vector<uint8_t> wire;
    CHECK(try_format("request", json_in, wire));

    uint32_t len_field = ((uint32_t)wire[4] << 24) | ((uint32_t)wire[5] << 16) |
                         ((uint32_t)wire[6] << 8)  |  (uint32_t)wire[7];
    CHECK((size_t)len_field == wire.size() - 12);

    std::string xml(reinterpret_cast<const char*>(wire.data() + 8), (size_t)len_field);
    CHECK(xml.find("<DDFCLRequest") != std::string::npos);
    CHECK(xml.find("id=\"10\"") != std::string::npos);
    CHECK(xml.find("type=\"set\"") != std::string::npos);
    CHECK(xml.find("AnalysisIntervalMs") != std::string::npos);
    CHECK(xml.find("50000") != std::string::npos);
    CHECK(xml.find("</DDFCLRequest>") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Test: format_response — missing required field → error
// ---------------------------------------------------------------------------

static void test_format_response_missing_field() {
    std::vector<uint8_t> wire;

    // Missing "command_name"
    const char* bad = "{\"msg_type\":\"set\",\"id\":1,\"xml_body\":\"<P/>\"}";
    CHECK(!try_format("request", bad, wire));

    // Missing "id"
    const char* bad2 = "{\"msg_type\":\"set\",\"command_name\":\"DfMode\",\"xml_body\":\"\"}";
    CHECK(!try_format("request", bad2, wire));

    // Missing "msg_type"
    const char* bad3 = "{\"id\":1,\"command_name\":\"DfMode\",\"xml_body\":\"\"}";
    CHECK(!try_format("request", bad3, wire));
}

// ---------------------------------------------------------------------------
// Test: format_response roundtrip — produce frame, extract_frame recognises it
// ---------------------------------------------------------------------------

static void test_format_response_roundtrip() {
    const char* json_in =
        "{\"msg_type\":\"get\","
        "\"id\":7,"
        "\"command_name\":\"DeviceInfo\","
        "\"xml_body\":\"\"}";

    std::vector<uint8_t> wire;
    CHECK(try_format("request", json_in, wire));

    std::vector<uint8_t> out_frame;
    CHECK(try_extract(wire.data(), wire.size(), out_frame));

    std::string json_out;
    CHECK(try_parse(out_frame.data(), out_frame.size(), json_out));
    CHECK(json_has(json_out, "\"command\":{\"name\":\"DeviceInfo\"}"));
    CHECK(json_has(json_out, "\"type\":\"get\""));
}

// ---------------------------------------------------------------------------
// Test: free_result with nullptr — must not crash
// ---------------------------------------------------------------------------

static void test_free_result_null() {
    free_result(nullptr);  // must not crash
    CHECK(true);
}

// ---------------------------------------------------------------------------
// Test: free_result with real pointer — must not crash
// ---------------------------------------------------------------------------

static void test_free_result_real() {
    auto ta  = make_conv_ta(1, 0u, {}, {0x00, 0x00});
    auto pkt = make_eb200(801, ta);

    std::vector<uint8_t> frame;
    CHECK(try_extract(pkt.data(), pkt.size(), frame));
    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));  // free_result called inside try_parse
    CHECK(true);
}

// ---------------------------------------------------------------------------
// Test: parse_message with nullptr/short frame → error
// ---------------------------------------------------------------------------

static void test_parse_message_null() {
    char* out_json = nullptr;
    size_t out_len = 0;
    CHECK(parse_message(nullptr, 0, &out_json, &out_len)   != 0);
    CHECK(parse_message(nullptr, 100, &out_json, &out_len) != 0);

    uint8_t tiny[2] = {0x00, 0x01};
    CHECK(parse_message(tiny, 2, &out_json, &out_len) != 0);
}

// ---------------------------------------------------------------------------
// Test: EB200 Audio trace with IQ demodulation
// ---------------------------------------------------------------------------

static void test_eb200_audio_iq_demod() {
    auto opt_hdr = make_audio_opt_hdr(
        4, 4, 2400000000ULL, 200000, 4 /*IQ*/, "IQ",
        1718100000000000000ULL, 0);

    std::vector<uint8_t> periodic;
    be32(periodic, 0x00010002u);  // one stereo pair (4-byte frame)

    auto ta  = make_conv_ta(1, 0x80000000u, opt_hdr, periodic);
    auto pkt = make_eb200(401, ta);

    std::vector<uint8_t> frame;
    CHECK(try_extract(pkt.data(), pkt.size(), frame));

    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));
    CHECK(json_has(json, "\"demod\":4"));
    CHECK(json_has(json, "\"demod_str\":\"IQ\""));
    CHECK(json_has(json, "\"frame_length\":4"));
}

// ---------------------------------------------------------------------------
// Test: EB200 Audio squelch (audio_mode=0) — minimal optional header
// ---------------------------------------------------------------------------

static void test_eb200_audio_squelch() {
    // Squelch = mode 0 — DDF sends one packet with AudioMode=0, no samples
    auto opt_hdr = make_audio_opt_hdr(0, 1, 100000000ULL, 0, 0, "FM",
                                      0ULL, 0);
    auto ta  = make_conv_ta(0, 0x80000000u, opt_hdr, {});
    auto pkt = make_eb200(401, ta);

    std::vector<uint8_t> frame;
    CHECK(try_extract(pkt.data(), pkt.size(), frame));

    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));
    CHECK(json_has(json, "\"audio_mode\":0"));
    CHECK(json_has(json, "\"n_items\":0"));
    CHECK(json_has(json, "\"periodic_data_bytes\":0"));
}

// ---------------------------------------------------------------------------
// Test: EB200 unknown tag (e.g. FSCAN = 101)
// ---------------------------------------------------------------------------

static void test_eb200_unknown_tag() {
    auto ta  = make_conv_ta(5, 0x00000001u, {}, {0,1,2,3,4,5,6,7,8,9});
    auto pkt = make_eb200(101, ta);

    std::vector<uint8_t> frame;
    CHECK(try_extract(pkt.data(), pkt.size(), frame));

    std::string json;
    CHECK(try_parse(frame.data(), frame.size(), json));
    CHECK(json_has(json, "\"trace_tag\":101"));
    // FScan is not explicitly named in our table → "unknown"
    // (or it could be named if added — just check tag number present)
    CHECK(json_has(json, "\"tag_name\""));
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
    std::printf("Running DDF-550 parser tests...\n");
    std::fflush(stdout);

    test_eb200_audio();
    test_eb200_ifpan();
    test_eb200_pscan();
    test_eb200_dfpscan_advanced();
    test_eb200_sigp_advanced();
    test_eb200_cw();
    test_eb200_truncated_header();
    test_eb200_bad_datasize();
    test_eb200_data_incomplete();
    test_garbage_input();
    test_wrapped_xml_request();
    test_wrapped_xml_reply();
    test_wrapped_xml_dfmode_get_request();
    test_wrapped_xml_dfmode_get_reply();
    test_wrapped_xml_freq();
    test_wrapped_xml_measuresettingsffm_get_request();
    test_wrapped_xml_measuresettingsffm_get_reply();
    test_wrapped_xml_audiomode();
    test_wrapped_xml_trace_enable();
    test_wrapped_xml_trace_disable();
    test_wrapped_xml_trace_delete();
    test_wrapped_xml_demodulation_settings();
    test_param_value_entity_decoded();
    test_wrapped_xml_scanrange_add();
    test_wrapped_xml_scanrange_delete_all();
    test_xml_param_self_closing_does_not_corrupt_scan();
    test_wrapped_xml_incomplete();
    test_wrapped_xml_partial_body();
    test_wrapped_xml_event();
    test_raw_xml_dfselect();
    test_ddfcl_request_wrapped();
    test_raw_xml_reply_without_command_tag();
    test_ddfcl_reply_raw();
    test_dfdata_format02();
    test_dfdata_hopper();
    test_dfdata_chirp();
    test_dfdata_with_whitespace();
    test_dfdata_with_cdata();
    test_raw_xml_leading_ws();
    test_raw_xml_no_close();
    test_too_short();
    test_format_response_control();
    test_format_response_preclassifier();
    test_format_response_missing_field();
    test_format_response_roundtrip();
    test_free_result_null();
    test_free_result_real();
    test_parse_message_null();
    test_eb200_audio_iq_demod();
    test_eb200_audio_squelch();
    test_eb200_unknown_tag();

    if (g_fails == 0) {
        std::printf("PASS  %d/%d checks\n", g_checks, g_checks);
        return 0;
    } else {
        std::printf("FAIL  %d/%d checks failed\n", g_fails, g_checks);
        return 1;
    }
}
