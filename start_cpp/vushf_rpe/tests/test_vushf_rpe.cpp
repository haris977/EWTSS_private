// start_cpp/vushf_rpe/tests/test_vushf_rpe.cpp
//
// Golden-frame tests for vushf_rpe_parser (30–6000 MHz).
// No external framework; exits non-zero on any failure.
#include "sdfc_abi.h"
#include "sdfc_frame.h"
#include "sdfc_endian.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace sdfc;

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("FAIL: %s\n", msg); ++g_fail; } \
    else         { std::printf("ok:   %s\n", msg); } } while(0)

static bool has(const char* s, const char* n) { return s && std::strstr(s, n); }

static std::vector<uint8_t> make_rsp(int16_t st, uint16_t g, uint16_t u,
                                     const std::vector<uint8_t>& d) {
    std::vector<uint8_t> f;
    f.insert(f.end(), RESP_HEADER, RESP_HEADER + 4);
    uint8_t sb[2]; store_i16le(sb, st); f.insert(f.end(), sb, sb+2);
    uint8_t sz[4]; store_u32le(sz, (uint32_t)d.size()); f.insert(f.end(), sz, sz+4);
    uint8_t gb[2]; store_u16le(gb, g); f.insert(f.end(), gb, gb+2);
    uint8_t ub[2]; store_u16le(ub, u); f.insert(f.end(), ub, ub+2);
    f.insert(f.end(), d.begin(), d.end());
    f.insert(f.end(), RESP_FOOTER, RESP_FOOTER + 4);
    return f;
}

int main() {
    std::vector<uint8_t> buf(MAX_FRAME_BUFFER_BYTES);
    int len = 0;

    // ── System Version  100/2 ───────────────────────────────────────────────
    {   std::vector<uint8_t> p(20,0);
        store_u32le(p.data()+0, 0x00030200);
        store_u16le(p.data()+16, 8);
        auto f = make_rsp(0,100,2,p);
        int t = extract_frame(f.data(),(int)f.size(),buf.data(),&len);
        auto* j = parse_message(buf.data(),len,t);
        CHECK(has(j,"\"hw\":\"vushf_rpe\""),   "vu: hw=vushf_rpe");
        CHECK(has(j,"\"processor_id\":8"),     "vu: processor_id 8");
        free_result(j); }

    // ── Temperature  100/10 ─────────────────────────────────────────────────
    {   std::vector<uint8_t> p(16,0);
        float ti=38.0f; uint32_t b; std::memcpy(&b,&ti,4);
        store_u32le(p.data()+0,b);
        auto f = make_rsp(0,100,10,p);
        int t = extract_frame(f.data(),(int)f.size(),buf.data(),&len);
        auto* j = parse_message(buf.data(),len,t);
        CHECK(has(j,"internal_temp_c"), "vu: temp internal_temp_c");
        free_result(j); }

    // ── FH Detection  101/40  (VU band: 150 MHz) ────────────────────────────
    {   std::vector<uint8_t> p;
        uint8_t hc[4]; store_u16le(hc,1); store_u16le(hc+2,0);
        p.insert(p.end(),hc,hc+4);
        std::vector<uint8_t> hop(40,0);
        store_u32le(hop.data()+0, 3);
        float fm=150.0f; uint32_t b; std::memcpy(&b,&fm,4);
        store_u32le(hop.data()+4,b);
        store_u16le(hop.data()+32,1);
        p.insert(p.end(),hop.begin(),hop.end());
        auto f = make_rsp(0,101,40,p);
        int t = extract_frame(f.data(),(int)f.size(),buf.data(),&len);
        auto* j = parse_message(buf.data(),len,t);
        CHECK(has(j,"\"hopper_count\":1"),  "vu: fh hopper_count 1");
        CHECK(has(j,"1.5e+08")||has(j,"150000000"), "vu: fh 150 MHz");
        free_result(j); }

    // ── Wideband FFT  101/44  (VU band: 100–200 MHz) ────────────────────────
    {   std::vector<uint8_t> p(16+1*4,0);
        store_u32le(p.data()+0,100);   // start 100 MHz
        store_u32le(p.data()+4,200);   // stop  200 MHz
        store_u32le(p.data()+8,1000);  // step  1 MHz
        store_u32le(p.data()+12,1);
        auto f = make_rsp(0,101,44,p);
        int t = extract_frame(f.data(),(int)f.size(),buf.data(),&len);
        auto* j = parse_message(buf.data(),len,t);
        CHECK(has(j,"\"point_count\":1"), "vu: fft point_count 1");
        CHECK(has(j,"power_dbm"),         "vu: fft power_dbm");
        free_result(j); }

    // ── FF Detection  101/70 ─────────────────────────────────────────────────
    {   std::vector<uint8_t> p;
        uint8_t fc[4]; store_u16le(fc,1); store_u16le(fc+2,0);
        p.insert(p.end(),fc,fc+4);
        std::vector<uint8_t> det(32,0);
        float fq=433.0f; uint32_t b; std::memcpy(&b,&fq,4);
        store_u32le(det.data()+0,b);
        store_u16le(det.data()+20,1);
        p.insert(p.end(),det.begin(),det.end());
        auto f = make_rsp(0,101,70,p);
        int t = extract_frame(f.data(),(int)f.size(),buf.data(),&len);
        auto* j = parse_message(buf.data(),len,t);
        CHECK(has(j,"\"freq_count\":1"),  "vu: ff freq_count 1");
        CHECK(has(j,"\"active\":true"),   "vu: ff active");
        free_result(j); }

    // ── Burst Detection  101/84 ──────────────────────────────────────────────
    {   std::vector<uint8_t> p;
        uint8_t bc[4]; store_u16le(bc,2); store_u16le(bc+2,0);
        p.insert(p.end(),bc,bc+4);
        for (int i = 0; i < 2; ++i) {
            std::vector<uint8_t> bst(36,0);
            float cf=900.0f; uint32_t b; std::memcpy(&b,&cf,4);
            store_u32le(bst.data()+0,b);
            p.insert(p.end(),bst.begin(),bst.end());
        }
        auto f = make_rsp(0,101,84,p);
        int t = extract_frame(f.data(),(int)f.size(),buf.data(),&len);
        auto* j = parse_message(buf.data(),len,t);
        CHECK(has(j,"\"burst_count\":2"), "vu: burst_count 2");
        free_result(j); }

    // ── Immediate Jam ACK  200/2 ─────────────────────────────────────────────
    {   std::vector<uint8_t> p(8,0);
        store_u16le(p.data()+0, 77);  // jam_id
        store_u16le(p.data()+4, 1);   // active
        auto f = make_rsp(0,200,2,p);
        int t = extract_frame(f.data(),(int)f.size(),buf.data(),&len);
        auto* j = parse_message(buf.data(),len,t);
        CHECK(has(j,"\"group_id\":200"),               "vu: jam group 200");
        CHECK(has(j,"\"jam_kind\":\"immediate_jam\""),  "vu: jam kind immediate");
        CHECK(has(j,"\"jam_id\":77"),                   "vu: jam_id 77");
        CHECK(has(j,"\"jam_active\":true"),             "vu: jam active");
        free_result(j); }

    // ── Follow-On Jam ACK  200/4 (empty payload) ────────────────────────────
    {   auto f = make_rsp(0,200,4,{});
        int t = extract_frame(f.data(),(int)f.size(),buf.data(),&len);
        auto* j = parse_message(buf.data(),len,t);
        CHECK(has(j,"\"jam_kind\":\"follow_on_jam\""), "vu: follow-on jam kind");
        free_result(j); }

    // ── Jam List ACK  200/6  error status ────────────────────────────────────
    {   auto f = make_rsp(-3,200,6,{});
        int t = extract_frame(f.data(),(int)f.size(),buf.data(),&len);
        auto* j = parse_message(buf.data(),len,t);
        CHECK(has(j,"\"jam_kind\":\"jam_list\""),    "vu: jam list kind");
        CHECK(has(j,"\"status\":-3"),                "vu: jam list status -3");
        free_result(j); }

    // ── Responsive Sweep ACK  200/8 ──────────────────────────────────────────
    {   std::vector<uint8_t> p(8,0);
        store_u16le(p.data()+0, 12);  // jam_id
        store_u16le(p.data()+4, 0);   // not active (sweep finished)
        auto f = make_rsp(0,200,8,p);
        int t = extract_frame(f.data(),(int)f.size(),buf.data(),&len);
        auto* j = parse_message(buf.data(),len,t);
        CHECK(has(j,"\"jam_kind\":\"responsive_sweep\""), "vu: resp sweep kind");
        CHECK(has(j,"\"jam_active\":false"),               "vu: jam not active");
        free_result(j); }

    // ── format_response with payload_hex ────────────────────────────────────
    {   // jam_id=77 (0x4D) LE = bytes [4d 00]; active=1 = [01 00]
        const char* jr = "{\"group_id\":200,\"unit_id\":2,\"status\":0,"
                         "\"payload_hex\":\"4d0000000100\"}";
        int n = format_response(jr, buf.data());
        CHECK(n == RESP_OVERHEAD + 6, "vu: format_response 6-byte payload");
        CHECK(load_u16le(buf.data() + RESP_OFF_PAYLOAD) == 77,
              "vu: round-trip jam_id == 77"); }

    // ── Unknown group/unit -> raw_hex, no crash ───────────────────────────────
    {   auto f = make_rsp(0,112,4,{0xDE,0xAD,0xBE,0xEF});
        int t = extract_frame(f.data(),(int)f.size(),buf.data(),&len);
        auto* j = parse_message(buf.data(),len,t);
        CHECK(has(j,"\"raw_hex\":\"deadbeef\""), "vu: unknown unit raw_hex");
        free_result(j); }

    std::printf("\n%s (%d failures)\n",
                g_fail ? "TESTS FAILED" : "ALL TESTS PASSED", g_fail);
    return g_fail ? 1 : 0;
}
