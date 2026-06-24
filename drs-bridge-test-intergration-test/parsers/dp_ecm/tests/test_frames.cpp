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
    // 1. Command frame, no payload -> extract succeeds (rc 0), magic bytes are CMD.
    {
        auto f = build_cmd(101, 25, {});
        uint8_t* out_frame = nullptr;
        size_t out_len = 0;
        int t = extract_frame(f.data(), f.size(), &out_frame, &out_len);
        CHECK(t == 0, "command frame -> rc 0");
        CHECK(out_len == f.size(), "command out_len == frame size");
        CHECK(out_frame && std::memcmp(out_frame, CMD_HEADER, 4) == 0, "command magic bytes");
        free_result(out_frame);
    }

    // 2. Incomplete frame (one byte short) -> rc -1.
    {
        auto f = build_cmd(101, 25, {0x01, 0x02, 0x03, 0x04});
        uint8_t* out_frame = nullptr;
        size_t out_len = 0;
        int t = extract_frame(f.data(), f.size() - 1, &out_frame, &out_len);
        CHECK(t == -1, "truncated frame -> rc -1");
        free_result(out_frame);
    }

    // 3. Bad footer -> rc -1.
    {
        auto f = build_cmd(101, 25, {0xAA});
        f.back() = 0x00; // corrupt footer
        uint8_t* out_frame = nullptr;
        size_t out_len = 0;
        int t = extract_frame(f.data(), f.size(), &out_frame, &out_len);
        CHECK(t == -1, "bad footer -> rc -1");
        free_result(out_frame);
    }

    // 4. Unknown magic -> rc -1.
    {
        uint8_t junk[16] = {0x00,0x11,0x22,0x33, 0,0,0,0, 0,0,0,0, 0,0,0,0};
        uint8_t* out_frame = nullptr;
        size_t out_len = 0;
        int t = extract_frame(junk, 16, &out_frame, &out_len);
        CHECK(t == -1, "unknown magic -> rc -1");
        free_result(out_frame);
    }

    // 5. System Version response (100/2, 20-byte payload) parses with version fields.
    {
        std::vector<uint8_t> p(20, 0);
        store_u32le(p.data() + 0, 0x00020401);  // fw raw
        store_u16le(p.data() + 12, 7);          // HF: processor_id at offset 12
        auto f = build_resp(0, 100, 2, p);
        uint8_t* out_frame = nullptr;
        size_t out_len = 0;
        int t = extract_frame(f.data(), f.size(), &out_frame, &out_len);
        CHECK(t == 0 && out_frame && std::memcmp(out_frame, RESP_HEADER, 4) == 0, "sysver frame -> rc 0, resp magic");
        char* j = nullptr;
        size_t j_len = 0;
        parse_message(out_frame, out_len, &j, &j_len);
        CHECK(contains(j, "\"group_id\":100"), "sysver json group 100");
        CHECK(contains(j, "\"processor_id\":7"), "sysver json processor_id 7");
        CHECK(contains(j, "\"status\":0"), "sysver json status 0");
        free_result(j);
        free_result(out_frame);
    }

    // 6. FH detection response (101/40, 1 hopper) decodes hopper array.
    {
        std::vector<uint8_t> p;
        uint8_t hc[4]; store_u16le(hc, 1); store_u16le(hc + 2, 0); // count=1, reserved
        p.insert(p.end(), hc, hc + 4);
        std::vector<uint8_t> hop(40, 0);
        store_u32le(hop.data() + 0, 3);                 // hopper number
        // min freq 5.0 MHz as float
        float fmin = 5.0f; uint32_t b; std::memcpy(&b, &fmin, 4); store_u32le(hop.data() + 4, b);
        store_u16le(hop.data() + 32, 1);                // active
        p.insert(p.end(), hop.begin(), hop.end());
        auto f = build_resp(0, 101, 40, p);
        uint8_t* out_frame = nullptr;
        size_t out_len = 0;
        extract_frame(f.data(), f.size(), &out_frame, &out_len);
        char* j = nullptr;
        size_t j_len = 0;
        parse_message(out_frame, out_len, &j, &j_len);
        CHECK(contains(j, "\"hopper_count\":1"), "fh json hopper_count 1");
        CHECK(contains(j, "\"hopper_number\":3"), "fh json hopper_number 3");
        CHECK(contains(j, "\"min_freq_hz\":5e+06") || contains(j, "\"min_freq_hz\":5000000"),
              "fh json min_freq_hz 5 MHz");
        CHECK(contains(j, "\"active\":true"), "fh json active true");
        free_result(j);
        free_result(out_frame);
    }

    // 7. format_response builds a valid ACK frame that extract_frame round-trips.
    {
        const char* jr = "{\"group_id\":101,\"unit_id\":26,\"status\":0}";
        uint8_t* out_buf = nullptr;
        size_t n = 0;
        int rc = format_response("response", jr, &out_buf, &n);
        CHECK(rc == 0, "format_response ACK rc 0");
        CHECK(n == RESP_OVERHEAD, "format_response ACK length == 18");
        uint8_t* rt_frame = nullptr;
        size_t rt_len = 0;
        int t = extract_frame(out_buf, n, &rt_frame, &rt_len);
        CHECK(t == 0 && rt_frame && std::memcmp(rt_frame, RESP_HEADER, 4) == 0, "round-trip ACK -> rc 0, resp magic");
        free_result(rt_frame);
        free_result(out_buf);
    }

    // 8. Unknown unit -> raw_hex fallback, never crashes.
    {
        auto f = build_resp(0, 101, 999, {0xDE, 0xAD, 0xBE, 0xEF});
        uint8_t* out_frame = nullptr;
        size_t out_len = 0;
        extract_frame(f.data(), f.size(), &out_frame, &out_len);
        char* j = nullptr;
        size_t j_len = 0;
        parse_message(out_frame, out_len, &j, &j_len);
        CHECK(contains(j, "\"raw_hex\":\"deadbeef\""), "unknown unit -> raw_hex");
        free_result(j);
        free_result(out_frame);
    }

    // 9. Temperature response (100/10, 24-byte payload) decodes six temp fields.
    // HF layout: processor_temp_c, psu_temp_c, fan_temp_c, rf_psu_temp_c, digital_temp_c, fpga_temp_c.
    {
        std::vector<uint8_t> p(24, 0);
        float t0 = 35.5f, t1 = 28.0f, t2f = 52.1f, t3 = 40.0f, t4 = 38.5f, t5 = 61.3f;
        uint32_t b;
        std::memcpy(&b, &t0,  4); store_u32le(p.data() +  0, b);  // processor_temp_c
        std::memcpy(&b, &t1,  4); store_u32le(p.data() +  4, b);  // psu_temp_c
        std::memcpy(&b, &t2f, 4); store_u32le(p.data() +  8, b);  // fan_temp_c
        std::memcpy(&b, &t3,  4); store_u32le(p.data() + 12, b);  // rf_psu_temp_c
        std::memcpy(&b, &t4,  4); store_u32le(p.data() + 16, b);  // digital_temp_c
        std::memcpy(&b, &t5,  4); store_u32le(p.data() + 20, b);  // fpga_temp_c
        auto f = build_resp(0, 100, 10, p);
        uint8_t* out_frame = nullptr;
        size_t out_len = 0;
        int t2 = extract_frame(f.data(), f.size(), &out_frame, &out_len);
        CHECK(t2 == 0, "hf: temp frame -> rc 0");
        char* j = nullptr;
        size_t j_len = 0;
        parse_message(out_frame, out_len, &j, &j_len);
        CHECK(contains(j, "\"group_id\":100"),         "hf: temp group 100");
        CHECK(contains(j, "\"unit_id\":10"),           "hf: temp unit 10");
        CHECK(contains(j, "processor_temp_c"),         "hf: temp has processor_temp_c");
        CHECK(contains(j, "fpga_temp_c"),              "hf: temp has fpga_temp_c");
        free_result(j);
        free_result(out_frame);
    }

    // 10. Fan Speed response (100/14, 4 bytes) — simplest fixed-size response.
    {
        std::vector<uint8_t> p(4, 0);
        store_u32le(p.data(), 3600);  // fan_speed_rpm = 3600
        auto f = build_resp(0, 100, 14, p);
        uint8_t* out_frame = nullptr;
        size_t out_len = 0;
        int t2 = extract_frame(f.data(), f.size(), &out_frame, &out_len);
        CHECK(t2 == 0, "hf: fan speed frame -> rc 0");
        char* j = nullptr;
        size_t j_len = 0;
        parse_message(out_frame, out_len, &j, &j_len);
        CHECK(contains(j, "\"unit_id\":14"),          "hf: fan speed unit 14");
        CHECK(contains(j, "\"fan_speed_rpm\":3600"),  "hf: fan speed rpm 3600");
        free_result(j);
        free_result(out_frame);
    }

    std::printf("\n%s (%d failures)\n", g_failures ? "TESTS FAILED" : "ALL TESTS PASSED", g_failures);
    return g_failures ? 1 : 0;
}
