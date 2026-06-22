// drs-bridge/parsers/ddf1gtx/tests/test_frames_ddf1gtx.cpp
//
// Unit tests for ddf1gtx_parser.cpp — no external test framework.
// Exercises extract_frame, parse_message, format_response, and free_result
// across all three frame channels (EB200, wrapped XML, raw XML/DFData).
// DDF-1GTX-specific tests cover ScanRangeAdd, MeasureSettingsFFM with
// eAttSelect, DemodulationSettings with eAFBandwidth.

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
    pad(ta, 4);
    be32(ta, (uint32_t)opt_hdr.size());
    be32(ta, flags_lo);
    be32(ta, flags_hi);
    pad(ta, 16);
    ta.insert(ta.end(), opt_hdr.begin(), opt_hdr.end());
    ta.insert(ta.end(), periodic.begin(), periodic.end());
    return ta;
}

// Build a complete EB200 packet around a TraceData blob
static std::vector<uint8_t> make_eb200(uint16_t trace_tag,
                                        const std::vector<uint8_t>& trace_data)
{
    bool advanced = (trace_tag >= 5000);

    std::vector<uint8_t> ga;
    be16(ga, trace_tag);
    if (!advanced) {
        be16(ga, (uint16_t)trace_data.size());
    } else {
        pad(ga, 2);
        be32(ga, (uint32_t)trace_data.size());
        pad(ga, 16);
    }
    ga.insert(ga.end(), trace_data.begin(), trace_data.end());

    uint32_t data_size = 16u + (uint32_t)ga.size();

    std::vector<uint8_t> pkt;
    be32(pkt, 0x000EB200u);
    be16(pkt, 1u);
    be16(pkt, 2u);
    be16(pkt, 7u);
    be16(pkt, 0u);
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
    be32(h, (uint32_t)(freq_hz & 0xFFFFFFFFu));
    be32(h, bandwidth_hz);
    be16(h, demod);
    char dstr[8] = {};
    if (demod_str_ascii) std::strncpy(dstr, demod_str_ascii, 8);
    for (int i = 0; i < 8; ++i) be8(h, (uint8_t)dstr[i]);
    be32(h, (uint32_t)(freq_hz >> 32));
    pad(h, 6);
    be64(h, timestamp_ns);
    be16(h, (uint16_t)sig_source);
    assert(h.size() == 42);
    return h;
}

// Wrap raw XML bytes in the DDF-1GTX binary envelope.
// Uses non-EB200 magic (0xABCD1234 / 0xDCBA4321) — any value works for the
// content-based heuristic in extract_frame.
static std::vector<uint8_t> wrap_xml(const char* xml_str) {
    std::vector<uint8_t> v;
    uint32_t n = (uint32_t)strlen(xml_str);
    be32(v, 0xABCD1234u);
    be32(v, n);
    for (const char* p = xml_str; *p; ++p) be8(v, (uint8_t)*p);
    be32(v, 0xDCBA4321u);
    return v;
}

// Convert C-string to raw byte vector (no wrapper)
static std::vector<uint8_t> raw_bytes(const char* s) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(s);
    return std::vector<uint8_t>(p, p + strlen(s));
}

// Check that JSON string contains a substring
static bool json_has(const char* json, const char* needle) {
    return json && std::strstr(json, needle) != nullptr;
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
    auto opt_hdr = make_audio_opt_hdr(
        2, 2, 100000000ULL, 150000, 0, "FM",
        1718000000000000000ULL, 0);

    std::vector<uint8_t> periodic = {0x12, 0x34, 0x56, 0x78};
    auto ta  = make_conv_ta(2, 0x80000000u, opt_hdr, periodic);
    auto pkt = make_eb200(401, ta);

    std::vector<uint8_t> frame(MAX_FRAME_BUFFER_BYTES);
    int frame_len = 0;
    int r = extract_frame(pkt.data(), (int)pkt.size(), frame.data(), &frame_len);

    CHECK(r == 3);
    CHECK(frame_len == (int)pkt.size());

    const char* json = parse_message(frame.data(), frame_len, r);
    CHECK(json != nullptr);
    CHECK(json_has(json, "\"hw\":\"ddf1gtx\""));
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

    free_result(json);
}

// ---------------------------------------------------------------------------
// Test: EB200 IFPan (tag 501, conventional, no optional header)
// ---------------------------------------------------------------------------

static void test_eb200_ifpan() {
    std::vector<uint8_t> periodic;
    for (int i = 0; i < 3; ++i) {
        be16(periodic, (uint16_t)(500 + i * 10));
        be32(periodic, (uint32_t)(i * 1000));
    }
    auto ta  = make_conv_ta(3, 0x00000003u, {}, periodic);
    auto pkt = make_eb200(501, ta);

    std::vector<uint8_t> frame(MAX_FRAME_BUFFER_BYTES);
    int frame_len = 0;
    CHECK(extract_frame(pkt.data(), (int)pkt.size(), frame.data(), &frame_len) == 3);

    const char* json = parse_message(frame.data(), frame_len, 3);
    CHECK(json != nullptr);
    CHECK(json_has(json, "\"hw\":\"ddf1gtx\""));
    CHECK(json_has(json, "\"trace_tag\":501"));
    CHECK(json_has(json, "\"tag_name\":\"ifpan\""));
    CHECK(json_has(json, "\"n_items\":3"));
    CHECK(json_has(json, "\"sel_flags\":3"));
    CHECK(json_has(json, "\"periodic_data_bytes\":18"));
    free_result(json);
}

// ---------------------------------------------------------------------------
// Test: EB200 DFPScan (tag 5301, ADVANCED format)
// ---------------------------------------------------------------------------

static void test_eb200_dfpscan_advanced() {
    std::vector<uint8_t> periodic;
    for (int i = 0; i < 3; ++i) {
        be16(periodic, (uint16_t)(450 + i * 5));
        be16(periodic, (uint16_t)(900 + i * 10));
    }
    auto ta  = make_adv_ta(3, 0x00001001u, 0u, {}, periodic);
    auto pkt = make_eb200(5301, ta);

    std::vector<uint8_t> frame(MAX_FRAME_BUFFER_BYTES);
    int frame_len = 0;
    int r = extract_frame(pkt.data(), (int)pkt.size(), frame.data(), &frame_len);
    CHECK(r == 3);

    const char* json = parse_message(frame.data(), frame_len, r);
    CHECK(json != nullptr);
    CHECK(json_has(json, "\"hw\":\"ddf1gtx\""));
    CHECK(json_has(json, "\"tag_name\":\"dfpscan\""));
    CHECK(json_has(json, "\"trace_tag\":5301"));
    CHECK(json_has(json, "\"n_items\":3"));
    CHECK(json_has(json, "\"sel_flags\":4097"));
    CHECK(json_has(json, "\"periodic_data_bytes\":12"));
    free_result(json);
}

// ---------------------------------------------------------------------------
// Test: EB200 HRPan (tag 5601, advanced)
// ---------------------------------------------------------------------------

static void test_eb200_hrpan_advanced() {
    auto ta  = make_adv_ta(1, 0x00000001u, 0u, {}, {0x00, 0x01});
    auto pkt = make_eb200(5601, ta);

    std::vector<uint8_t> frame(MAX_FRAME_BUFFER_BYTES);
    int frame_len = 0;
    CHECK(extract_frame(pkt.data(), (int)pkt.size(), frame.data(), &frame_len) == 3);

    const char* json = parse_message(frame.data(), frame_len, 3);
    CHECK(json != nullptr);
    CHECK(json_has(json, "\"tag_name\":\"hrpan\""));
    CHECK(json_has(json, "\"trace_tag\":5601"));
    free_result(json);
}

// ---------------------------------------------------------------------------
// Test: EB200 CW (tag 801, conventional)
// ---------------------------------------------------------------------------

static void test_eb200_cw() {
    std::vector<uint8_t> periodic;
    be16(periodic, 700u);
    auto ta  = make_conv_ta(1, 0x00000001u, {}, periodic);
    auto pkt = make_eb200(801, ta);

    std::vector<uint8_t> frame(MAX_FRAME_BUFFER_BYTES);
    int frame_len = 0;
    CHECK(extract_frame(pkt.data(), (int)pkt.size(), frame.data(), &frame_len) == 3);

    const char* json = parse_message(frame.data(), frame_len, 3);
    CHECK(json != nullptr);
    CHECK(json_has(json, "\"tag_name\":\"cw\""));
    free_result(json);
}

// ---------------------------------------------------------------------------
// Test: EB200 PScan (tag 1201, conventional)
// ---------------------------------------------------------------------------

static void test_eb200_pscan() {
    std::vector<uint8_t> periodic;
    be16(periodic, 600u);
    auto ta  = make_conv_ta(1, 0x00000001u, {}, periodic);
    auto pkt = make_eb200(1201, ta);

    std::vector<uint8_t> frame(MAX_FRAME_BUFFER_BYTES);
    int frame_len = 0;
    CHECK(extract_frame(pkt.data(), (int)pkt.size(), frame.data(), &frame_len) == 3);

    const char* json = parse_message(frame.data(), frame_len, 3);
    CHECK(json != nullptr);
    CHECK(json_has(json, "\"tag_name\":\"pscan\""));
    free_result(json);
}

// ---------------------------------------------------------------------------
// Test: EB200 truncated header → incomplete (0)
// ---------------------------------------------------------------------------

static void test_eb200_truncated_header() {
    std::vector<uint8_t> buf;
    be32(buf, 0x000EB200u);
    be16(buf, 1u); be16(buf, 2u); be16(buf, 1u);

    std::vector<uint8_t> frame(MAX_FRAME_BUFFER_BYTES);
    int frame_len = 0;
    CHECK(extract_frame(buf.data(), (int)buf.size(), frame.data(), &frame_len) == 0);
}

// ---------------------------------------------------------------------------
// Test: EB200 DataSize too small → corrupt (-1)
// ---------------------------------------------------------------------------

static void test_eb200_bad_datasize() {
    std::vector<uint8_t> buf;
    be32(buf, 0x000EB200u);
    be16(buf, 1u); be16(buf, 2u); be16(buf, 1u); be16(buf, 0u);
    be32(buf, 4u);  // DataSize=4 < EB200_HDR_BYTES(16)

    std::vector<uint8_t> frame(MAX_FRAME_BUFFER_BYTES);
    int frame_len = 0;
    CHECK(extract_frame(buf.data(), (int)buf.size(), frame.data(), &frame_len) == -1);
}

// ---------------------------------------------------------------------------
// Test: EB200 valid header but data not yet arrived → incomplete (0)
// ---------------------------------------------------------------------------

static void test_eb200_data_incomplete() {
    auto ta  = make_conv_ta(1, 0u, {}, {0x00, 0x00});
    auto pkt = make_eb200(501, ta);

    std::vector<uint8_t> frame(MAX_FRAME_BUFFER_BYTES);
    int frame_len = 0;
    CHECK(extract_frame(pkt.data(), 20, frame.data(), &frame_len) == 0);
}

// ---------------------------------------------------------------------------
// Test: Non-EB200 magic, non-XML garbage → corrupt (-1)
// ---------------------------------------------------------------------------

static void test_garbage_input() {
    uint8_t buf[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01, 0x02, 0x03,
                      0x55, 0x66, 0x77, 0x88 };
    std::vector<uint8_t> frame(MAX_FRAME_BUFFER_BYTES);
    int frame_len = 0;
    CHECK(extract_frame(buf, (int)sizeof(buf), frame.data(), &frame_len) == -1);
}

// ---------------------------------------------------------------------------
// Test: DfMode — DDF-1GTX-specific eDFMODE enum value
// ---------------------------------------------------------------------------

static void test_wrapped_xml_dfmode() {
    const char* xml =
        "<Request type=\"set\" id=\"42\">"
        "<Command name=\"DfMode\">"
        "<Param name=\"eOperationMode\">DFMODE_FFM</Param>"
        "</Command></Request>";
    auto frame_bytes = wrap_xml(xml);

    std::vector<uint8_t> frame(MAX_FRAME_BUFFER_BYTES);
    int frame_len = 0;
    int r = extract_frame(frame_bytes.data(), (int)frame_bytes.size(), frame.data(), &frame_len);
    CHECK(r == 1);

    const char* json = parse_message(frame.data(), frame_len, r);
    CHECK(json != nullptr);
    CHECK(json_has(json, "\"hw\":\"ddf1gtx\""));
    CHECK(json_has(json, "\"channel\":\"control\""));
    CHECK(json_has(json, "\"msg_kind\":\"request\""));
    CHECK(json_has(json, "\"msg_id\":\"42\""));
    CHECK(json_has(json, "\"msg_type\":\"set\""));
    CHECK(json_has(json, "\"command_name\":\"DfMode\""));
    CHECK(json_has(json, "\"operation_mode\":\"DFMODE_FFM\""));
    CHECK(json_has(json, "raw_xml"));
    free_result(json);
}

// ---------------------------------------------------------------------------
// Test: MeasureSettingsFFM — iFrequency + eAttSelect
// ---------------------------------------------------------------------------

static void test_wrapped_xml_measure_settings_ffm() {
    const char* xml =
        "<Request type=\"set\" id=\"2\">"
        "<Command name=\"MeasureSettingsFFM\">"
        "<Param name=\"iFrequency\">145000000</Param>"
        "<Param name=\"eAttSelect\">ATT_AUTO</Param>"
        "</Command></Request>";
    auto frame_bytes = wrap_xml(xml);

    std::vector<uint8_t> frame(MAX_FRAME_BUFFER_BYTES);
    int frame_len = 0;
    CHECK(extract_frame(frame_bytes.data(), (int)frame_bytes.size(), frame.data(), &frame_len) == 1);

    const char* json = parse_message(frame.data(), frame_len, 1);
    CHECK(json != nullptr);
    CHECK(json_has(json, "\"command_name\":\"MeasureSettingsFFM\""));
    CHECK(json_has(json, "\"frequency_hz\":\"145000000\""));
    CHECK(json_has(json, "\"att_select\":\"ATT_AUTO\""));
    free_result(json);
}

// ---------------------------------------------------------------------------
// Test: MeasureSettingsFFM — iFreqBegin + iFreqEnd (scan band)
// ---------------------------------------------------------------------------

static void test_wrapped_xml_measure_settings_scan() {
    const char* xml =
        "<Request type=\"set\" id=\"3\">"
        "<Command name=\"MeasureSettingsFFM\">"
        "<Param name=\"iFreqBegin\">100000000</Param>"
        "<Param name=\"iFreqEnd\">200000000</Param>"
        "<Param name=\"eAttSelect\">ATT_0DB</Param>"
        "</Command></Request>";
    auto frame_bytes = wrap_xml(xml);

    std::vector<uint8_t> frame(MAX_FRAME_BUFFER_BYTES);
    int frame_len = 0;
    CHECK(extract_frame(frame_bytes.data(), (int)frame_bytes.size(), frame.data(), &frame_len) == 1);

    const char* json = parse_message(frame.data(), frame_len, 1);
    CHECK(json != nullptr);
    CHECK(json_has(json, "\"freq_begin_hz\":\"100000000\""));
    CHECK(json_has(json, "\"freq_end_hz\":\"200000000\""));
    CHECK(json_has(json, "\"att_select\":\"ATT_0DB\""));
    free_result(json);
}

// ---------------------------------------------------------------------------
// Test: DemodulationSettings — eDemodulation + eAFBandwidth
// ---------------------------------------------------------------------------

static void test_wrapped_xml_demod_settings() {
    const char* xml =
        "<Request type=\"set\" id=\"4\">"
        "<Command name=\"DemodulationSettings\">"
        "<Param name=\"eDemodulation\">DEMOD_FM</Param>"
        "<Param name=\"eAFBandwidth\">AFBW_300KHZ</Param>"
        "</Command></Request>";
    auto frame_bytes = wrap_xml(xml);

    std::vector<uint8_t> frame(MAX_FRAME_BUFFER_BYTES);
    int frame_len = 0;
    CHECK(extract_frame(frame_bytes.data(), (int)frame_bytes.size(), frame.data(), &frame_len) == 1);

    const char* json = parse_message(frame.data(), frame_len, 1);
    CHECK(json != nullptr);
    CHECK(json_has(json, "\"command_name\":\"DemodulationSettings\""));
    CHECK(json_has(json, "\"demodulation\":\"DEMOD_FM\""));
    CHECK(json_has(json, "\"af_bandwidth\":\"AFBW_300KHZ\""));
    free_result(json);
}

// ---------------------------------------------------------------------------
// Test: AudioMode SET
// ---------------------------------------------------------------------------

static void test_wrapped_xml_audiomode() {
    const char* xml =
        "<Request type=\"set\" id=\"5\">"
        "<Command name=\"AudioMode\">"
        "<Param name=\"eAudioMode\">AUDIO_MODE_32KHZ_16BIT_MONO</Param>"
        "</Command></Request>";
    auto frame_bytes = wrap_xml(xml);

    std::vector<uint8_t> frame(MAX_FRAME_BUFFER_BYTES);
    int frame_len = 0;
    CHECK(extract_frame(frame_bytes.data(), (int)frame_bytes.size(), frame.data(), &frame_len) == 1);

    const char* json = parse_message(frame.data(), frame_len, 1);
    CHECK(json != nullptr);
    CHECK(json_has(json, "\"command_name\":\"AudioMode\""));
    CHECK(json_has(json, "AUDIO_MODE_32KHZ_16BIT_MONO"));
    free_result(json);
}

// ---------------------------------------------------------------------------
// Test: ScanRangeAdd — iFreqBegin, iFreqEnd, eDFPanStep
// ---------------------------------------------------------------------------

static void test_wrapped_xml_scan_range_add() {
    const char* xml =
        "<Request type=\"set\" id=\"6\">"
        "<Command name=\"ScanRangeAdd\">"
        "<Param name=\"iFreqBegin\">87500000</Param>"
        "<Param name=\"iFreqEnd\">108000000</Param>"
        "<Param name=\"eDFPanStep\">DFPANSTEP_25KHZ</Param>"
        "</Command></Request>";
    auto frame_bytes = wrap_xml(xml);

    std::vector<uint8_t> frame(MAX_FRAME_BUFFER_BYTES);
    int frame_len = 0;
    int r = extract_frame(frame_bytes.data(), (int)frame_bytes.size(), frame.data(), &frame_len);
    CHECK(r == 1);

    const char* json = parse_message(frame.data(), frame_len, r);
    CHECK(json != nullptr);
    CHECK(json_has(json, "\"command_name\":\"ScanRangeAdd\""));
    CHECK(json_has(json, "\"freq_begin_hz\":\"87500000\""));
    CHECK(json_has(json, "\"freq_end_hz\":\"108000000\""));
    CHECK(json_has(json, "\"df_pan_step\":\"DFPANSTEP_25KHZ\""));
    free_result(json);
}

// ---------------------------------------------------------------------------
// Test: ScanRangeDeleteAll — no params, SET only
// ---------------------------------------------------------------------------

static void test_wrapped_xml_scan_range_delete_all() {
    const char* xml =
        "<Request type=\"set\" id=\"7\">"
        "<Command name=\"ScanRangeDeleteAll\">"
        "</Command></Request>";
    auto frame_bytes = wrap_xml(xml);

    std::vector<uint8_t> frame(MAX_FRAME_BUFFER_BYTES);
    int frame_len = 0;
    CHECK(extract_frame(frame_bytes.data(), (int)frame_bytes.size(), frame.data(), &frame_len) == 1);

    const char* json = parse_message(frame.data(), frame_len, 1);
    CHECK(json != nullptr);
    CHECK(json_has(json, "\"command_name\":\"ScanRangeDeleteAll\""));
    free_result(json);
}

// ---------------------------------------------------------------------------
// Test: TraceEnable command
// ---------------------------------------------------------------------------

static void test_wrapped_xml_trace_enable() {
    const char* xml =
        "<Request type=\"set\" id=\"8\">"
        "<Command name=\"TraceEnable\">"
        "<Param name=\"eTraceTag\">TRACETAG_AUDIO</Param>"
        "<Param name=\"zIP\">192.168.1.100</Param>"
        "<Param name=\"iPort\">9152</Param>"
        "</Command></Request>";
    auto frame_bytes = wrap_xml(xml);

    std::vector<uint8_t> frame(MAX_FRAME_BUFFER_BYTES);
    int frame_len = 0;
    CHECK(extract_frame(frame_bytes.data(), (int)frame_bytes.size(), frame.data(), &frame_len) == 1);

    const char* json = parse_message(frame.data(), frame_len, 1);
    CHECK(json != nullptr);
    CHECK(json_has(json, "\"command_name\":\"TraceEnable\""));
    CHECK(json_has(json, "\"trace_tag_str\":\"TRACETAG_AUDIO\""));
    CHECK(json_has(json, "\"trace_ip\":\"192.168.1.100\""));
    CHECK(json_has(json, "\"trace_port\":\"9152\""));
    free_result(json);
}

// ---------------------------------------------------------------------------
// Test: TraceDisable command
// ---------------------------------------------------------------------------

static void test_wrapped_xml_trace_disable() {
    const char* xml =
        "<Request type=\"set\" id=\"9\">"
        "<Command name=\"TraceDisable\">"
        "<Param name=\"eTraceTag\">TRACETAG_DFPSCAN</Param>"
        "<Param name=\"zIP\">192.168.1.100</Param>"
        "<Param name=\"iPort\">9152</Param>"
        "</Command></Request>";
    auto frame_bytes = wrap_xml(xml);

    std::vector<uint8_t> frame(MAX_FRAME_BUFFER_BYTES);
    int frame_len = 0;
    CHECK(extract_frame(frame_bytes.data(), (int)frame_bytes.size(), frame.data(), &frame_len) == 1);

    const char* json = parse_message(frame.data(), frame_len, 1);
    CHECK(json != nullptr);
    CHECK(json_has(json, "\"command_name\":\"TraceDisable\""));
    CHECK(json_has(json, "\"trace_tag_str\":\"TRACETAG_DFPSCAN\""));
    free_result(json);
}

// ---------------------------------------------------------------------------
// Test: <Reply> → type 2
// ---------------------------------------------------------------------------

static void test_wrapped_xml_reply() {
    const char* xml =
        "<Reply type=\"set\" id=\"42\">"
        "<Command name=\"DfMode\">"
        "</Command></Reply>";
    auto frame_bytes = wrap_xml(xml);

    std::vector<uint8_t> frame(MAX_FRAME_BUFFER_BYTES);
    int frame_len = 0;
    int r = extract_frame(frame_bytes.data(), (int)frame_bytes.size(), frame.data(), &frame_len);
    CHECK(r == 2);

    const char* json = parse_message(frame.data(), frame_len, r);
    CHECK(json != nullptr);
    CHECK(json_has(json, "\"hw\":\"ddf1gtx\""));
    CHECK(json_has(json, "\"msg_kind\":\"reply\""));
    CHECK(json_has(json, "\"command_name\":\"DfMode\""));
    free_result(json);
}

// ---------------------------------------------------------------------------
// Test: DeviceInfo GET reply
// ---------------------------------------------------------------------------

static void test_wrapped_xml_device_info_reply() {
    const char* xml =
        "<Reply type=\"get\" id=\"10\">"
        "<Command name=\"DeviceInfo\">"
        "</Command></Reply>";
    auto frame_bytes = wrap_xml(xml);

    std::vector<uint8_t> frame(MAX_FRAME_BUFFER_BYTES);
    int frame_len = 0;
    CHECK(extract_frame(frame_bytes.data(), (int)frame_bytes.size(), frame.data(), &frame_len) == 2);

    const char* json = parse_message(frame.data(), frame_len, 2);
    CHECK(json != nullptr);
    CHECK(json_has(json, "\"command_name\":\"DeviceInfo\""));
    CHECK(json_has(json, "\"msg_kind\":\"reply\""));
    free_result(json);
}

// ---------------------------------------------------------------------------
// Test: Wrapped XML incomplete → 0
// ---------------------------------------------------------------------------

static void test_wrapped_xml_incomplete() {
    const char* xml = "<Request type=\"set\" id=\"1\"><Command name=\"DfMode\"></Command></Request>";
    auto frame_bytes = wrap_xml(xml);

    std::vector<uint8_t> frame_buf(MAX_FRAME_BUFFER_BYTES);
    int frame_len = 0;
    int half = (int)frame_bytes.size() / 2;
    CHECK(extract_frame(frame_bytes.data(), half, frame_buf.data(), &frame_len) == 0);
}

// ---------------------------------------------------------------------------
// Test: Wrapped XML length field says 100 bytes but only 5 follow → 0
// ---------------------------------------------------------------------------

static void test_wrapped_xml_partial_body() {
    std::vector<uint8_t> v;
    be32(v, 0xABCD1234u);
    be32(v, 100u);
    be8(v, '<'); be8(v, 'R'); be8(v, 'e'); be8(v, 'q'); be8(v, '>');

    std::vector<uint8_t> frame_buf(MAX_FRAME_BUFFER_BYTES);
    int frame_len = 0;
    CHECK(extract_frame(v.data(), (int)v.size(), frame_buf.data(), &frame_len) == 0);
}

// ---------------------------------------------------------------------------
// Test: DDFCLRequest (preclassifier control) → type 1
// ---------------------------------------------------------------------------

static void test_ddfcl_request_wrapped() {
    const char* xml =
        "<?xml version=\"1.0\" ?>"
        "<DDFCLRequest id=\"10\" type=\"set\">"
        "<Command name=\"AnalysisIntervalMs\">50000</Command>"
        "</DDFCLRequest>";
    auto frame_bytes = wrap_xml(xml);

    std::vector<uint8_t> frame(MAX_FRAME_BUFFER_BYTES);
    int frame_len = 0;
    int r = extract_frame(frame_bytes.data(), (int)frame_bytes.size(), frame.data(), &frame_len);
    CHECK(r == 1);

    const char* json = parse_message(frame.data(), frame_len, r);
    CHECK(json != nullptr);
    CHECK(json_has(json, "\"channel\":\"preclassifier\""));
    CHECK(json_has(json, "\"msg_kind\":\"request\""));
    CHECK(json_has(json, "\"msg_id\":\"10\""));
    free_result(json);
}

// ---------------------------------------------------------------------------
// Test: DDFCLReply (raw, no wrapper) → type 2
// ---------------------------------------------------------------------------

static void test_ddfcl_reply_raw() {
    const char* xml =
        "<DDFCLReply id=\"10\" type=\"set\">"
        "<Command name=\"AnalysisIntervalMs\">50000</Command>"
        "</DDFCLReply>";
    auto frame_bytes = raw_bytes(xml);

    std::vector<uint8_t> frame(MAX_FRAME_BUFFER_BYTES);
    int frame_len = 0;
    int r = extract_frame(frame_bytes.data(), (int)frame_bytes.size(), frame.data(), &frame_len);
    CHECK(r == 2);

    const char* json = parse_message(frame.data(), frame_len, r);
    CHECK(json != nullptr);
    CHECK(json_has(json, "\"channel\":\"preclassifier\""));
    CHECK(json_has(json, "\"msg_kind\":\"reply\""));
    free_result(json);
}

// ---------------------------------------------------------------------------
// Test: DFData FORMAT02 preclassifier output (raw, port 9154) → type 2
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

    std::vector<uint8_t> frame(MAX_FRAME_BUFFER_BYTES);
    int frame_len = 0;
    int r = extract_frame(frame_bytes.data(), (int)frame_bytes.size(), frame.data(), &frame_len);
    CHECK(r == 2);

    const char* json = parse_message(frame.data(), frame_len, r);
    CHECK(json != nullptr);
    CHECK(json_has(json, "\"hw\":\"ddf1gtx\""));
    CHECK(json_has(json, "\"channel\":\"preclassifier_output\""));
    CHECK(json_has(json, "\"msg_kind\":\"dfdata\""));
    CHECK(json_has(json, "\"ddf_cl_id\":\"3\""));
    CHECK(json_has(json, "\"emitter_class\":\"Burst\""));
    CHECK(json_has(json, "\"center_freq_hz\":\"433920000\""));
    CHECK(json_has(json, "\"bearing_avg_deg\":\"245.7\""));
    CHECK(json_has(json, "\"level_avg_dbuv\":\"56.3\""));
    free_result(json);
}

// ---------------------------------------------------------------------------
// Test: DFData Hopper emitter class
// ---------------------------------------------------------------------------

static void test_dfdata_hopper() {
    const char* xml =
        "<DFData DDF-CL-ID=\"5\">"
        "<EmitterClass>Hopper</EmitterClass>"
        "<BearingAvg Unit=\"deg\">90.0</BearingAvg>"
        "</DFData>";
    auto frame_bytes = raw_bytes(xml);

    std::vector<uint8_t> frame(MAX_FRAME_BUFFER_BYTES);
    int frame_len = 0;
    CHECK(extract_frame(frame_bytes.data(), (int)frame_bytes.size(), frame.data(), &frame_len) == 2);

    const char* json = parse_message(frame.data(), frame_len, 2);
    CHECK(json != nullptr);
    CHECK(json_has(json, "\"emitter_class\":\"Hopper\""));
    free_result(json);
}

// ---------------------------------------------------------------------------
// Test: Raw XML with leading whitespace (Reply) → type 2
// ---------------------------------------------------------------------------

static void test_raw_xml_leading_ws() {
    const char* xml =
        "   \r\n<Reply type=\"get\" id=\"99\">"
        "<Command name=\"ModuleInfo\"></Command></Reply>";
    auto frame_bytes = raw_bytes(xml);

    std::vector<uint8_t> frame(MAX_FRAME_BUFFER_BYTES);
    int frame_len = 0;
    int r = extract_frame(frame_bytes.data(), (int)frame_bytes.size(), frame.data(), &frame_len);
    CHECK(r == 2);

    const char* json = parse_message(frame.data(), frame_len, r);
    CHECK(json != nullptr);
    CHECK(json_has(json, "\"msg_kind\":\"reply\""));
    CHECK(json_has(json, "\"command_name\":\"ModuleInfo\""));
    free_result(json);
}

// ---------------------------------------------------------------------------
// Test: Raw XML without closing tag → incomplete (0)
// ---------------------------------------------------------------------------

static void test_raw_xml_no_close() {
    const char* xml = "<Reply type=\"set\" id=\"1\"><Command name=\"DfMode\">";
    auto frame_bytes = raw_bytes(xml);

    std::vector<uint8_t> frame(MAX_FRAME_BUFFER_BYTES);
    int frame_len = 0;
    CHECK(extract_frame(frame_bytes.data(), (int)frame_bytes.size(), frame.data(), &frame_len) == 0);
}

// ---------------------------------------------------------------------------
// Test: buf_len < 4 → incomplete (0)
// ---------------------------------------------------------------------------

static void test_too_short() {
    uint8_t buf[] = {0x00, 0x0E};
    std::vector<uint8_t> frame(MAX_FRAME_BUFFER_BYTES);
    int frame_len = 0;
    CHECK(extract_frame(buf, 2, frame.data(), &frame_len) == 0);
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

    std::vector<uint8_t> frame(MAX_FRAME_BUFFER_BYTES);
    int n = format_response(json_in, frame.data());
    CHECK(n > 12);

    uint32_t len_field = ((uint32_t)frame[4] << 24) | ((uint32_t)frame[5] << 16) |
                         ((uint32_t)frame[6] << 8)  |  (uint32_t)frame[7];
    CHECK((int)len_field == n - 12);

    const char* xml = reinterpret_cast<const char*>(frame.data() + 8);
    int xml_len = (int)len_field;
    CHECK(std::string(xml, xml_len).find("<Request") != std::string::npos);
    CHECK(std::string(xml, xml_len).find("type=\"set\"") != std::string::npos);
    CHECK(std::string(xml, xml_len).find("id=\"42\"") != std::string::npos);
    CHECK(std::string(xml, xml_len).find("<Command name=\"DfMode\">") != std::string::npos);
    CHECK(std::string(xml, xml_len).find("DFMODE_FFM") != std::string::npos);
    CHECK(std::string(xml, xml_len).find("</Request>") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Test: format_response — ScanRangeAdd with frequency range params
// ---------------------------------------------------------------------------

static void test_format_response_scan_range_add() {
    const char* json_in =
        "{\"msg_type\":\"set\","
        "\"id\":6,"
        "\"command_name\":\"ScanRangeAdd\","
        "\"xml_body\":"
        "\"<Param name=\\\"iFreqBegin\\\">87500000</Param>"
        "<Param name=\\\"iFreqEnd\\\">108000000</Param>"
        "<Param name=\\\"eDFPanStep\\\">DFPANSTEP_25KHZ</Param>\"}";

    std::vector<uint8_t> wire(MAX_FRAME_BUFFER_BYTES);
    int n = format_response(json_in, wire.data());
    CHECK(n > 12);

    uint32_t len_field = ((uint32_t)wire[4] << 24) | ((uint32_t)wire[5] << 16) |
                         ((uint32_t)wire[6] << 8)  |  (uint32_t)wire[7];
    const char* xml = reinterpret_cast<const char*>(wire.data() + 8);
    int xml_len = (int)len_field;
    CHECK(std::string(xml, xml_len).find("ScanRangeAdd") != std::string::npos);
    CHECK(std::string(xml, xml_len).find("87500000") != std::string::npos);
    CHECK(std::string(xml, xml_len).find("108000000") != std::string::npos);
    CHECK(std::string(xml, xml_len).find("DFPANSTEP_25KHZ") != std::string::npos);
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

    std::vector<uint8_t> frame(MAX_FRAME_BUFFER_BYTES);
    int n = format_response(json_in, frame.data());
    CHECK(n > 12);

    uint32_t len_field = ((uint32_t)frame[4] << 24) | ((uint32_t)frame[5] << 16) |
                         ((uint32_t)frame[6] << 8)  |  (uint32_t)frame[7];
    CHECK((int)len_field == n - 12);

    const char* xml = reinterpret_cast<const char*>(frame.data() + 8);
    int xml_len = (int)len_field;
    CHECK(std::string(xml, xml_len).find("<DDFCLRequest") != std::string::npos);
    CHECK(std::string(xml, xml_len).find("id=\"10\"") != std::string::npos);
    CHECK(std::string(xml, xml_len).find("</DDFCLRequest>") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Test: format_response — missing required field → -1
// ---------------------------------------------------------------------------

static void test_format_response_missing_field() {
    std::vector<uint8_t> frame(MAX_FRAME_BUFFER_BYTES);

    const char* bad = "{\"msg_type\":\"set\",\"id\":1,\"xml_body\":\"<P/>\"}";
    CHECK(format_response(bad, frame.data()) == -1);  // missing command_name

    const char* bad2 = "{\"msg_type\":\"set\",\"command_name\":\"DfMode\",\"xml_body\":\"\"}";
    CHECK(format_response(bad2, frame.data()) == -1);  // missing id

    const char* bad3 = "{\"id\":1,\"command_name\":\"DfMode\",\"xml_body\":\"\"}";
    CHECK(format_response(bad3, frame.data()) == -1);  // missing msg_type
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

    std::vector<uint8_t> wire(MAX_FRAME_BUFFER_BYTES);
    int wire_len = format_response(json_in, wire.data());
    CHECK(wire_len > 0);

    std::vector<uint8_t> out_frame(MAX_FRAME_BUFFER_BYTES);
    int out_len = 0;
    int r = extract_frame(wire.data(), wire_len, out_frame.data(), &out_len);
    CHECK(r == 1);

    const char* json_out = parse_message(out_frame.data(), out_len, r);
    CHECK(json_out != nullptr);
    CHECK(json_has(json_out, "\"hw\":\"ddf1gtx\""));
    CHECK(json_has(json_out, "\"command_name\":\"DeviceInfo\""));
    CHECK(json_has(json_out, "\"msg_type\":\"get\""));
    free_result(json_out);
}

// ---------------------------------------------------------------------------
// Test: free_result with nullptr — must not crash
// ---------------------------------------------------------------------------

static void test_free_result_null() {
    free_result(nullptr);
    CHECK(true);
}

// ---------------------------------------------------------------------------
// Test: parse_message with nullptr/short frame → nullptr
// ---------------------------------------------------------------------------

static void test_parse_message_null() {
    CHECK(parse_message(nullptr, 0, 3)   == nullptr);
    CHECK(parse_message(nullptr, 100, 1) == nullptr);

    uint8_t tiny[2] = {0x00, 0x01};
    CHECK(parse_message(tiny, 2, 3) == nullptr);
}

// ---------------------------------------------------------------------------
// Test: EB200 Audio with IQ demodulation
// ---------------------------------------------------------------------------

static void test_eb200_audio_iq_demod() {
    auto opt_hdr = make_audio_opt_hdr(
        4, 4, 2400000000ULL, 200000, 4 /*IQ*/, "IQ",
        1718100000000000000ULL, 0);

    std::vector<uint8_t> periodic;
    be32(periodic, 0x00010002u);

    auto ta  = make_conv_ta(1, 0x80000000u, opt_hdr, periodic);
    auto pkt = make_eb200(401, ta);

    std::vector<uint8_t> frame(MAX_FRAME_BUFFER_BYTES);
    int frame_len = 0;
    CHECK(extract_frame(pkt.data(), (int)pkt.size(), frame.data(), &frame_len) == 3);

    const char* json = parse_message(frame.data(), frame_len, 3);
    CHECK(json != nullptr);
    CHECK(json_has(json, "\"demod\":4"));
    CHECK(json_has(json, "\"demod_str\":\"IQ\""));
    free_result(json);
}

// ---------------------------------------------------------------------------
// Test: EB200 unknown tag (e.g. 101) — still parsed, tag_name="unknown"
// ---------------------------------------------------------------------------

static void test_eb200_unknown_tag() {
    auto ta  = make_conv_ta(5, 0x00000001u, {}, {0,1,2,3,4,5,6,7,8,9});
    auto pkt = make_eb200(101, ta);

    std::vector<uint8_t> frame(MAX_FRAME_BUFFER_BYTES);
    int frame_len = 0;
    CHECK(extract_frame(pkt.data(), (int)pkt.size(), frame.data(), &frame_len) == 3);

    const char* json = parse_message(frame.data(), frame_len, 3);
    CHECK(json != nullptr);
    CHECK(json_has(json, "\"trace_tag\":101"));
    CHECK(json_has(json, "\"tag_name\""));
    free_result(json);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
    std::printf("Running DDF-1GTX parser tests...\n");
    std::fflush(stdout);

    test_eb200_audio();
    test_eb200_ifpan();
    test_eb200_dfpscan_advanced();
    test_eb200_hrpan_advanced();
    test_eb200_cw();
    test_eb200_pscan();
    test_eb200_truncated_header();
    test_eb200_bad_datasize();
    test_eb200_data_incomplete();
    test_garbage_input();
    test_wrapped_xml_dfmode();
    test_wrapped_xml_measure_settings_ffm();
    test_wrapped_xml_measure_settings_scan();
    test_wrapped_xml_demod_settings();
    test_wrapped_xml_audiomode();
    test_wrapped_xml_scan_range_add();
    test_wrapped_xml_scan_range_delete_all();
    test_wrapped_xml_trace_enable();
    test_wrapped_xml_trace_disable();
    test_wrapped_xml_reply();
    test_wrapped_xml_device_info_reply();
    test_wrapped_xml_incomplete();
    test_wrapped_xml_partial_body();
    test_ddfcl_request_wrapped();
    test_ddfcl_reply_raw();
    test_dfdata_format02();
    test_dfdata_hopper();
    test_raw_xml_leading_ws();
    test_raw_xml_no_close();
    test_too_short();
    test_format_response_control();
    test_format_response_scan_range_add();
    test_format_response_preclassifier();
    test_format_response_missing_field();
    test_format_response_roundtrip();
    test_free_result_null();
    test_parse_message_null();
    test_eb200_audio_iq_demod();
    test_eb200_unknown_tag();

    if (g_fails == 0) {
        std::printf("PASS  %d/%d checks\n", g_checks, g_checks);
        return 0;
    } else {
        std::printf("FAIL  %d/%d checks failed\n", g_fails, g_checks);
        return 1;
    }
}
