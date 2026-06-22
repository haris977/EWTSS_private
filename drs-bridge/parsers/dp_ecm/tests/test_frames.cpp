// drs-bridge/parsers/dp_ecm/tests/test_frames.cpp
//
// Self-contained golden-frame tests for the DP-ECM HF parser (no test framework).
// Builds byte-exact frames, runs them through extract_frame -> parse_message,
// and round-trips format_response. Exits non-zero on any failure.
#include "sdfc_abi.h"
#include "sdfc_frame.h"
#include "sdfc_endian.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace sdfc;

static int g_failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); ++g_failures; } \
                              else { std::printf("ok:   %s\n", msg); } } while (0)

static bool contains(const char* hay, const char* needle) {
    return hay && std::strstr(hay, needle) != nullptr;
}

// Build a COMMAND frame: hdr + size + group + unit + payload + footer.
static std::vector<uint8_t> build_cmd(uint16_t group, uint16_t unit,
                                      const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> f;
    f.insert(f.end(), CMD_HEADER, CMD_HEADER + 4);
    uint8_t sz[4]; store_u32le(sz, static_cast<uint32_t>(payload.size())); f.insert(f.end(), sz, sz + 4);
    uint8_t g[2]; store_u16le(g, group); f.insert(f.end(), g, g + 2);
    uint8_t u[2]; store_u16le(u, unit);  f.insert(f.end(), u, u + 2);
    f.insert(f.end(), payload.begin(), payload.end());
    f.insert(f.end(), CMD_FOOTER, CMD_FOOTER + 4);
    return f;
}

// Build a RESPONSE frame: hdr + status + size + group + unit + payload + footer.
static std::vector<uint8_t> build_resp(int16_t status, uint16_t group, uint16_t unit,
                                       const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> f;
    f.insert(f.end(), RESP_HEADER, RESP_HEADER + 4);
    uint8_t st[2]; store_i16le(st, status); f.insert(f.end(), st, st + 2);
    uint8_t sz[4]; store_u32le(sz, static_cast<uint32_t>(payload.size())); f.insert(f.end(), sz, sz + 4);
    uint8_t g[2]; store_u16le(g, group); f.insert(f.end(), g, g + 2);
    uint8_t u[2]; store_u16le(u, unit);  f.insert(f.end(), u, u + 2);
    f.insert(f.end(), payload.begin(), payload.end());
    f.insert(f.end(), RESP_FOOTER, RESP_FOOTER + 4);
    return f;
}

int main() {
    // 1. Command frame -> extract returns 0, frame_type visible in JSON.
    {
        auto f = build_cmd(101, 25, {});
        uint8_t* frame_buf = nullptr; size_t flen = 0;
        int rc = extract_frame(f.data(), f.size(), &frame_buf, &flen);
        CHECK(rc == 0,                "command frame -> extract returns 0");
        CHECK(flen == f.size(),       "command out_len == frame size");
        char* j = nullptr; size_t jlen = 0;
        parse_message(frame_buf, flen, &j, &jlen);
        CHECK(contains(j, "\"frame_type\":\"command\""), "command frame -> frame_type command");
        free_result(j);
        free_result(frame_buf);
    }

    // 2. Incomplete frame (one byte short) -> -1.
    {
        auto f = build_cmd(101, 25, {0x01, 0x02, 0x03, 0x04});
        uint8_t* frame_buf = nullptr; size_t flen = 0;
        int rc = extract_frame(f.data(), f.size() - 1, &frame_buf, &flen);
        CHECK(rc == -1, "truncated frame -> -1");
    }

    // 3. Bad footer -> -1.
    {
        auto f = build_cmd(101, 25, {0xAA});
        f.back() = 0x00;
        uint8_t* frame_buf = nullptr; size_t flen = 0;
        int rc = extract_frame(f.data(), f.size(), &frame_buf, &flen);
        CHECK(rc == -1, "bad footer -> -1");
    }

    // 4. Unknown magic -> -1.
    {
        uint8_t junk[16] = {0x00,0x11,0x22,0x33, 0,0,0,0, 0,0,0,0, 0,0,0,0};
        uint8_t* frame_buf = nullptr; size_t flen = 0;
        int rc = extract_frame(junk, 16, &frame_buf, &flen);
        CHECK(rc == -1, "unknown magic -> -1");
    }

    // 5. System Version response (100/2, 20-byte payload) parses with version fields.
    {
        std::vector<uint8_t> p(20, 0);
        store_u32le(p.data() + 0, 0x00020401);  // sjc_fw_version (float raw)
        store_u16le(p.data() + 12, 7);          // processor_id at offset 12
        auto f = build_resp(0, 100, 2, p);
        uint8_t* frame_buf = nullptr; size_t flen = 0;
        int rc = extract_frame(f.data(), f.size(), &frame_buf, &flen);
        CHECK(rc == 0, "sysver frame -> extract ok");
        char* j = nullptr; size_t jlen = 0;
        int prc = parse_message(frame_buf, flen, &j, &jlen);
        free_result(frame_buf);
        CHECK(prc == 0,                           "sysver parse ok");
        CHECK(contains(j, "\"group_id\":100"),    "sysver json group 100");
        CHECK(contains(j, "\"processor_id\":7"),  "sysver json processor_id 7");
        CHECK(contains(j, "\"status\":0"),         "sysver json status 0");
        free_result(j);
    }

    // 6. FH detection response (101/40, 1 hopper) decodes hopper array.
    {
        std::vector<uint8_t> p;
        uint8_t hc[4]; store_u16le(hc, 1); store_u16le(hc + 2, 0); // count=1, reserved
        p.insert(p.end(), hc, hc + 4);
        std::vector<uint8_t> hop(40, 0);
        store_u32le(hop.data() + 0, 3);                 // hopper number
        float fmin = 5.0f; uint32_t b; std::memcpy(&b, &fmin, 4); store_u32le(hop.data() + 4, b);
        store_u16le(hop.data() + 32, 1);                // active
        p.insert(p.end(), hop.begin(), hop.end());
        auto f = build_resp(0, 101, 40, p);
        uint8_t* frame_buf = nullptr; size_t flen = 0;
        extract_frame(f.data(), f.size(), &frame_buf, &flen);
        char* j = nullptr; size_t jlen = 0;
        parse_message(frame_buf, flen, &j, &jlen);
        free_result(frame_buf);
        CHECK(contains(j, "\"hopper_count\":1"), "fh json hopper_count 1");
        CHECK(contains(j, "\"hopper_number\":3"), "fh json hopper_number 3");
        CHECK(contains(j, "\"min_freq_hz\":5e+06") || contains(j, "\"min_freq_hz\":5000000"),
              "fh json min_freq_hz 5 MHz");
        CHECK(contains(j, "\"active\":true"), "fh json active true");
        free_result(j);
    }

    // 7. format_response builds a valid ACK frame that extract_frame round-trips.
    {
        const char* jr = "{\"group_id\":101,\"unit_id\":26,\"status\":0}";
        uint8_t* resp_buf = nullptr; size_t resp_len = 0;
        int rc = format_response("response", jr, &resp_buf, &resp_len);
        CHECK(rc == 0,                            "format_response ACK ok");
        CHECK(resp_len == (size_t)RESP_OVERHEAD,  "format_response ACK length == 18");
        uint8_t* frame_buf2 = nullptr; size_t flen2 = 0;
        int rc2 = extract_frame(resp_buf, resp_len, &frame_buf2, &flen2);
        free_result(resp_buf);
        CHECK(rc2 == 0, "round-trip ACK -> extract ok");
        char* j2 = nullptr; size_t jlen2 = 0;
        parse_message(frame_buf2, flen2, &j2, &jlen2);
        free_result(frame_buf2);
        CHECK(contains(j2, "\"frame_type\":\"response\""), "round-trip ACK -> response");
        free_result(j2);
    }

    // 8. Unknown unit -> raw_hex fallback, never crashes.
    {
        auto f = build_resp(0, 101, 999, {0xDE, 0xAD, 0xBE, 0xEF});
        uint8_t* frame_buf = nullptr; size_t flen = 0;
        extract_frame(f.data(), f.size(), &frame_buf, &flen);
        char* j = nullptr; size_t jlen = 0;
        parse_message(frame_buf, flen, &j, &jlen);
        free_result(frame_buf);
        CHECK(contains(j, "\"raw_hex\":\"deadbeef\""), "unknown unit -> raw_hex");
        free_result(j);
    }

    // 9. Temperature response (100/10, 36-byte payload) decodes temp fields.
    {
        std::vector<uint8_t> p(36, 0);
        float t_int = 35.5f, t_ext = 28.0f, t_cpu = 52.1f, t_fpga = 61.3f;
        uint32_t b;
        std::memcpy(&b, &t_int,  4); store_u32le(p.data() +  0, b);
        std::memcpy(&b, &t_ext,  4); store_u32le(p.data() +  4, b);
        std::memcpy(&b, &t_cpu,  4); store_u32le(p.data() +  8, b);
        std::memcpy(&b, &t_fpga, 4); store_u32le(p.data() + 12, b);
        auto f = build_resp(0, 100, 10, p);
        uint8_t* frame_buf = nullptr; size_t flen = 0;
        int rc = extract_frame(f.data(), f.size(), &frame_buf, &flen);
        CHECK(rc == 0, "hf: temp frame -> extract ok");
        char* j = nullptr; size_t jlen = 0;
        parse_message(frame_buf, flen, &j, &jlen);
        free_result(frame_buf);
        CHECK(contains(j, "\"group_id\":100"),   "hf: temp group 100");
        CHECK(contains(j, "\"unit_id\":10"),     "hf: temp unit 10");
        CHECK(contains(j, "internal_temp_c"),    "hf: temp has internal_temp_c");
        CHECK(contains(j, "fpga_temp_c"),        "hf: temp has fpga_temp_c");
        free_result(j);
    }

    // 10. Wideband FFT response (101/44) — header fields + first power value.
    {
        std::vector<uint8_t> p(16 + 3 * 4, 0);
        store_u32le(p.data() +  0, 1);    // start 1 MHz
        store_u32le(p.data() +  4, 30);   // stop 30 MHz
        store_u32le(p.data() +  8, 1000); // step 1000 kHz = 1 MHz
        store_u32le(p.data() + 12, 3);    // 3 points
        float pw0 = -45.5f; uint32_t b; std::memcpy(&b, &pw0, 4);
        store_u32le(p.data() + 16, b);    // power[0] = -45.5 dBm
        auto f = build_resp(0, 101, 44, p);
        uint8_t* frame_buf = nullptr; size_t flen = 0;
        int rc = extract_frame(f.data(), f.size(), &frame_buf, &flen);
        CHECK(rc == 0, "hf: fft frame -> extract ok");
        char* j = nullptr; size_t jlen = 0;
        parse_message(frame_buf, flen, &j, &jlen);
        free_result(frame_buf);
        CHECK(contains(j, "\"unit_id\":44"),           "hf: fft unit 44");
        CHECK(contains(j, "\"point_count\":3"),        "hf: fft point_count 3");
        CHECK(contains(j, "\"start_freq_hz\":1e+06") ||
              contains(j, "\"start_freq_hz\":1000000"), "hf: fft start_freq_hz 1 MHz");
        CHECK(contains(j, "power_dbm"),                "hf: fft has power_dbm array");
        free_result(j);
    }

    std::printf("\n%s (%d failures)\n", g_failures ? "TESTS FAILED" : "ALL TESTS PASSED", g_failures);
    return g_failures ? 1 : 0;
}
