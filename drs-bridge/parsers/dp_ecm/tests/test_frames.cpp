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
    std::vector<uint8_t> outbuf(MAX_FRAME_BUFFER_BYTES);
    int out_len = 0;

    // 1. Command frame, no payload -> extract returns type 1.
    {
        auto f = build_cmd(101, 25, {});
        int t = extract_frame(f.data(), (int)f.size(), outbuf.data(), &out_len);
        CHECK(t == FRAME_COMMAND, "command frame -> type 1");
        CHECK(out_len == (int)f.size(), "command out_len == frame size");
    }

    // 2. Incomplete frame (one byte short) -> 0.
    {
        auto f = build_cmd(101, 25, {0x01, 0x02, 0x03, 0x04});
        int t = extract_frame(f.data(), (int)f.size() - 1, outbuf.data(), &out_len);
        CHECK(t == FRAME_INCOMPLETE, "truncated frame -> incomplete (0)");
    }

    // 3. Bad footer -> corrupt (-1).
    {
        auto f = build_cmd(101, 25, {0xAA});
        f.back() = 0x00; // corrupt footer
        int t = extract_frame(f.data(), (int)f.size(), outbuf.data(), &out_len);
        CHECK(t == FRAME_CORRUPT, "bad footer -> corrupt (-1)");
    }

    // 4. Unknown magic -> corrupt.
    {
        uint8_t junk[16] = {0x00,0x11,0x22,0x33, 0,0,0,0, 0,0,0,0, 0,0,0,0};
        int t = extract_frame(junk, 16, outbuf.data(), &out_len);
        CHECK(t == FRAME_CORRUPT, "unknown magic -> corrupt (-1)");
    }

    // 5. System Version response (100/2, 20-byte payload) parses with version fields.
    {
        std::vector<uint8_t> p(20, 0);
        store_u32le(p.data() + 0, 0x00020401);  // fw raw
        store_u16le(p.data() + 16, 7);          // processor id
        auto f = build_resp(0, 100, 2, p);
        int t = extract_frame(f.data(), (int)f.size(), outbuf.data(), &out_len);
        CHECK(t == FRAME_RESPONSE, "sysver frame -> type 2");
        const char* j = parse_message(outbuf.data(), out_len, t);
        CHECK(contains(j, "\"group_id\":100"), "sysver json group 100");
        CHECK(contains(j, "\"processor_id\":7"), "sysver json processor_id 7");
        CHECK(contains(j, "\"status\":0"), "sysver json status 0");
        free_result(j);
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
        int t = extract_frame(f.data(), (int)f.size(), outbuf.data(), &out_len);
        const char* j = parse_message(outbuf.data(), out_len, t);
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
        int n = format_response(jr, outbuf.data());
        CHECK(n == RESP_OVERHEAD, "format_response ACK length == 18");
        int t = extract_frame(outbuf.data(), n, std::vector<uint8_t>(MAX_FRAME_BUFFER_BYTES).data(), &out_len);
        CHECK(t == FRAME_RESPONSE, "round-trip ACK -> type 2");
    }

    // 8. Unknown unit -> raw_hex fallback, never crashes.
    {
        auto f = build_resp(0, 101, 999, {0xDE, 0xAD, 0xBE, 0xEF});
        int t = extract_frame(f.data(), (int)f.size(), outbuf.data(), &out_len);
        const char* j = parse_message(outbuf.data(), out_len, t);
        CHECK(contains(j, "\"raw_hex\":\"deadbeef\""), "unknown unit -> raw_hex");
        free_result(j);
    }

    std::printf("\n%s (%d failures)\n", g_failures ? "TESTS FAILED" : "ALL TESTS PASSED", g_failures);
    return g_failures ? 1 : 0;
}
