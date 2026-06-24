// drs-bridge/parsers/dp_ecm/tests/test_frames_vu.cpp
//
// Golden-frame tests for the DP-ECM-1074 VU (30–6000 MHz) parser.
// Exercises the shared frame core + VU-specific decoders.
// No external test framework; exits non-zero on any failure.
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
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("FAIL: %s\n", msg); ++g_failures; } \
    else         { std::printf("ok:   %s\n", msg); } } while (0)

static bool contains(const char* hay, const char* needle) {
    return hay && std::strstr(hay, needle) != nullptr;
}

static std::vector<uint8_t> build_cmd(uint16_t group, uint16_t unit,
                                      const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> f;
    f.insert(f.end(), CMD_HEADER, CMD_HEADER + 4);
    uint8_t sz[4]; store_u32le(sz, static_cast<uint32_t>(payload.size()));
    f.insert(f.end(), sz, sz + 4);
    uint8_t g[2]; store_u16le(g, group); f.insert(f.end(), g, g + 2);
    uint8_t u[2]; store_u16le(u, unit);  f.insert(f.end(), u, u + 2);
    f.insert(f.end(), payload.begin(), payload.end());
    f.insert(f.end(), CMD_FOOTER, CMD_FOOTER + 4);
    return f;
}

static std::vector<uint8_t> build_resp(int16_t status, uint16_t group, uint16_t unit,
                                       const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> f;
    f.insert(f.end(), RESP_HEADER, RESP_HEADER + 4);
    uint8_t st[2]; store_i16le(st, status); f.insert(f.end(), st, st + 2);
    uint8_t sz[4]; store_u32le(sz, static_cast<uint32_t>(payload.size()));
    f.insert(f.end(), sz, sz + 4);
    uint8_t g[2]; store_u16le(g, group); f.insert(f.end(), g, g + 2);
    uint8_t u[2]; store_u16le(u, unit);  f.insert(f.end(), u, u + 2);
    f.insert(f.end(), payload.begin(), payload.end());
    f.insert(f.end(), RESP_FOOTER, RESP_FOOTER + 4);
    return f;
}

int main() {
    // 1. Frame core: command frame is extracted correctly (shared with HF).
    {
        auto f = build_cmd(200, 1, {});
        uint8_t* out_frame = nullptr;
        size_t out_len = 0;
        int t = extract_frame(f.data(), f.size(), &out_frame, &out_len);
        CHECK(t == 0, "vu: command frame -> rc 0");
        CHECK(out_len == f.size(), "vu: command out_len correct");
        CHECK(out_frame && std::memcmp(out_frame, CMD_HEADER, 4) == 0, "vu: command magic bytes");
        free_result(out_frame);
    }

    // 2. Frame core: incomplete frame -> rc -1.
    {
        auto f = build_resp(0, 200, 2, {0x01, 0x02, 0x03, 0x04});
        uint8_t* out_frame = nullptr;
        size_t out_len = 0;
        int t = extract_frame(f.data(), f.size() - 1, &out_frame, &out_len);
        CHECK(t == -1, "vu: truncated frame -> rc -1");
        free_result(out_frame);
    }

    // 3. Frame core: bad footer -> rc -1.
    {
        auto f = build_cmd(200, 1, {0xAA});
        f.back() = 0x00;
        uint8_t* out_frame = nullptr;
        size_t out_len = 0;
        int t = extract_frame(f.data(), f.size(), &out_frame, &out_len);
        CHECK(t == -1, "vu: bad footer -> rc -1");
        free_result(out_frame);
    }

    // 4. System Version (100/2, 28-byte payload) — VU has 4 float versions (vs 3 in HF).
    // VU layout: sjc_fw(f32) driver(f32) fpga(f32) bsp(f32) processor_id(u16)
    //            tuner[3](3xu16) fpga_type_id(u16)
    {
        std::vector<uint8_t> p(28, 0);
        store_u32le(p.data() + 0, 0x00010200); // fw_version_raw
        store_u16le(p.data() + 16, 12);        // VU: processor_id at offset 16
        store_u16le(p.data() + 24, 3);         // VU: fpga_type_id at offset 24
        auto f = build_resp(0, 100, 2, p);
        uint8_t* out_frame = nullptr;
        size_t out_len = 0;
        int t = extract_frame(f.data(), f.size(), &out_frame, &out_len);
        CHECK(t == 0 && out_frame && std::memcmp(out_frame, RESP_HEADER, 4) == 0, "vu: sysver frame -> rc 0, resp magic");
        char* j = nullptr;
        size_t j_len = 0;
        parse_message(out_frame, out_len, &j, &j_len);
        CHECK(contains(j, "\"hw\":\"dp_ecm_vu\""),    "vu: sysver hw=dp_ecm_vu");
        CHECK(contains(j, "\"group_id\":100"),         "vu: sysver group 100");
        CHECK(contains(j, "\"unit_id\":2"),            "vu: sysver unit 2");
        CHECK(contains(j, "\"processor_id\":12"),      "vu: sysver processor_id 12");
        CHECK(contains(j, "\"fpga_type_id\":3"),       "vu: sysver fpga_type_id 3");
        CHECK(contains(j, "\"status\":0"),             "vu: sysver status 0");
        free_result(j);
        free_result(out_frame);
    }

    // 5. FH Detection (101/40, 1 hopper) — VU band 30–6000 MHz, same structure as HF.
    {
        std::vector<uint8_t> p;
        uint8_t hc[4]; store_u16le(hc, 1); store_u16le(hc + 2, 0);
        p.insert(p.end(), hc, hc + 4);
        std::vector<uint8_t> hop(60, 0);  // VU S_HOPPER_DATA = 60 bytes (HF = 40)
        store_u32le(hop.data() + 0, 7);                        // hopper number
        float fmin = 150.0f; uint32_t b; std::memcpy(&b, &fmin, 4);
        store_u32le(hop.data() + 4, b);                        // min 150 MHz
        float fmax = 155.0f; std::memcpy(&b, &fmax, 4);
        store_u32le(hop.data() + 8, b);                        // max 155 MHz
        store_u16le(hop.data() + 32, 1);                       // active
        p.insert(p.end(), hop.begin(), hop.end());
        auto f = build_resp(0, 101, 40, p);
        uint8_t* out_frame = nullptr;
        size_t out_len = 0;
        extract_frame(f.data(), f.size(), &out_frame, &out_len);
        char* j = nullptr;
        size_t j_len = 0;
        parse_message(out_frame, out_len, &j, &j_len);
        CHECK(contains(j, "\"hopper_count\":1"),   "vu: fh hopper_count 1");
        CHECK(contains(j, "\"hopper_number\":7"),  "vu: fh hopper_number 7");
        // 150 MHz -> 1.5e8 Hz
        CHECK(contains(j, "1.5e+08") || contains(j, "150000000"),
              "vu: fh min_freq_hz 150 MHz");
        CHECK(contains(j, "\"active\":true"),      "vu: fh active true");
        free_result(j);
        free_result(out_frame);
    }

    // 6. Immediate Jam ACK (200/2, 8-byte payload) — VU-specific.
    {
        std::vector<uint8_t> p(8, 0);
        store_u16le(p.data() + 0, 42);   // jam_id
        store_u16le(p.data() + 4, 1);    // active = true
        auto f = build_resp(0, 200, 2, p);
        uint8_t* out_frame = nullptr;
        size_t out_len = 0;
        int t = extract_frame(f.data(), f.size(), &out_frame, &out_len);
        CHECK(t == 0, "vu: jam ack frame -> rc 0");
        char* j = nullptr;
        size_t j_len = 0;
        parse_message(out_frame, out_len, &j, &j_len);
        CHECK(contains(j, "\"group_id\":200"),           "vu: jam ack group 200");
        CHECK(contains(j, "\"unit_id\":2"),              "vu: jam ack unit 2");
        CHECK(contains(j, "\"jam_kind\":\"immediate_jam\""), "vu: jam kind");
        CHECK(contains(j, "\"jam_id\":42"),              "vu: jam_id 42");
        CHECK(contains(j, "\"jam_active\":true"),        "vu: jam active true");
        free_result(j);
        free_result(out_frame);
    }

    // 7. Follow-On Jam ACK (200/4) — status-only ACK (empty payload).
    {
        auto f = build_resp(0, 200, 4, {});
        uint8_t* out_frame = nullptr;
        size_t out_len = 0;
        extract_frame(f.data(), f.size(), &out_frame, &out_len);
        char* j = nullptr;
        size_t j_len = 0;
        parse_message(out_frame, out_len, &j, &j_len);
        CHECK(contains(j, "\"jam_kind\":\"follow_on_jam\""), "vu: follow-on jam kind");
        CHECK(contains(j, "\"status\":0"),                   "vu: follow-on jam status 0");
        free_result(j);
        free_result(out_frame);
    }

    // 8. Jam List ACK (200/6) with error status.
    {
        auto f = build_resp(-1, 200, 6, {});
        uint8_t* out_frame = nullptr;
        size_t out_len = 0;
        extract_frame(f.data(), f.size(), &out_frame, &out_len);
        char* j = nullptr;
        size_t j_len = 0;
        parse_message(out_frame, out_len, &j, &j_len);
        CHECK(contains(j, "\"jam_kind\":\"jam_list\""), "vu: jam list kind");
        CHECK(contains(j, "\"status\":-1"),             "vu: jam list error status -1");
        CHECK(contains(j, "\"status_name\":\"Error\""), "vu: jam list status_name Error");
        free_result(j);
        free_result(out_frame);
    }

    // 9. Responsive Sweep Jam ACK (200/8, 8-byte payload).
    {
        std::vector<uint8_t> p(8, 0);
        store_u16le(p.data() + 0, 99);   // jam_id
        store_u16le(p.data() + 4, 0);    // active = false (sweep ended)
        auto f = build_resp(0, 200, 8, p);
        uint8_t* out_frame = nullptr;
        size_t out_len = 0;
        extract_frame(f.data(), f.size(), &out_frame, &out_len);
        char* j = nullptr;
        size_t j_len = 0;
        parse_message(out_frame, out_len, &j, &j_len);
        CHECK(contains(j, "\"jam_kind\":\"responsive_sweep\""), "vu: resp sweep kind");
        CHECK(contains(j, "\"jam_id\":99"),                     "vu: resp sweep jam_id 99");
        CHECK(contains(j, "\"jam_active\":false"),              "vu: resp sweep not active");
        free_result(j);
        free_result(out_frame);
    }

    // 10. Unknown group/unit falls through to raw_hex without crashing.
    {
        auto f = build_resp(0, 999, 999, {0xCA, 0xFE, 0xBA, 0xBE});
        uint8_t* out_frame = nullptr;
        size_t out_len = 0;
        extract_frame(f.data(), f.size(), &out_frame, &out_len);
        char* j = nullptr;
        size_t j_len = 0;
        parse_message(out_frame, out_len, &j, &j_len);
        CHECK(j != nullptr,                           "vu: unknown unit -> not null");
        CHECK(contains(j, "\"raw_hex\":\"cafebabe\""), "vu: unknown unit -> raw_hex");
        free_result(j);
        free_result(out_frame);
    }

    // 11. format_response round-trips a 200/2 ACK frame.
    {
        const char* jr = "{\"group_id\":200,\"unit_id\":2,\"status\":0}";
        uint8_t* out_buf = nullptr;
        size_t n = 0;
        int rc = format_response("response", jr, &out_buf, &n);
        CHECK(rc == 0, "vu: format_response ACK rc 0");
        CHECK(n == RESP_OVERHEAD, "vu: format_response ACK length == 18");
        uint8_t* rt_frame = nullptr;
        size_t rt_len = 0;
        int t = extract_frame(out_buf, n, &rt_frame, &rt_len);
        CHECK(t == 0 && rt_frame && std::memcmp(rt_frame, RESP_HEADER, 4) == 0, "vu: format_response round-trip rc 0, resp magic");
        free_result(rt_frame);
        free_result(out_buf);
    }

    // 12. format_response with payload_hex embeds bytes correctly.
    {
        // jam_id = 42 (0x2A) little-endian = bytes [2a 00]; active = 1 = [01 00]
        // full 6-byte payload: 2a 00 00 00 01 00
        const char* jr = "{\"group_id\":200,\"unit_id\":2,\"status\":0,"
                         "\"payload_hex\":\"2a0000000100\"}";
        uint8_t* out_buf = nullptr;
        size_t n = 0;
        int rc = format_response("response", jr, &out_buf, &n);
        CHECK(rc == 0, "vu: format_response with payload rc 0");
        CHECK(n == RESP_OVERHEAD + 6, "vu: format_response with 6-byte payload");
        CHECK(load_u16le(out_buf + RESP_OFF_PAYLOAD) == 42,
              "vu: round-trip jam_id payload == 42");
        free_result(out_buf);
    }

    std::printf("\n%s (%d failures)\n",
                g_failures ? "TESTS FAILED" : "ALL TESTS PASSED", g_failures);
    return g_failures ? 1 : 0;
}
