// start_cpp/hf_rpe/tests/test_hf_rpe.cpp
//
// Golden-frame tests for hf_rpe_parser (1–30 MHz).
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

static std::vector<uint8_t> make_cmd(uint16_t g, uint16_t u,
                                     const std::vector<uint8_t>& d) {
    std::vector<uint8_t> f;
    f.insert(f.end(), CMD_HEADER, CMD_HEADER + 4);
    uint8_t sz[4]; store_u32le(sz, (uint32_t)d.size()); f.insert(f.end(), sz, sz+4);
    uint8_t gb[2]; store_u16le(gb, g); f.insert(f.end(), gb, gb+2);
    uint8_t ub[2]; store_u16le(ub, u); f.insert(f.end(), ub, ub+2);
    f.insert(f.end(), d.begin(), d.end());
    f.insert(f.end(), CMD_FOOTER, CMD_FOOTER + 4);
    return f;
}
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

    // ── Frame core ──────────────────────────────────────────────────────────
    { auto f = make_cmd(101,25,{});
      CHECK(extract_frame(f.data(),(int)f.size(),buf.data(),&len)==FRAME_COMMAND,
            "hf: cmd -> type 1"); }

    { auto f = make_cmd(101,25,{1,2,3});
      CHECK(extract_frame(f.data(),(int)f.size()-1,buf.data(),&len)==FRAME_INCOMPLETE,
            "hf: truncated -> 0"); }

    { auto f = make_cmd(101,25,{0xAA}); f.back()=0x00;
      CHECK(extract_frame(f.data(),(int)f.size(),buf.data(),&len)==FRAME_CORRUPT,
            "hf: bad footer -> -1"); }

    // ── System Version  100/2 ───────────────────────────────────────────────
    {   std::vector<uint8_t> p(20,0);
        store_u32le(p.data()+0, 0x00020100);
        store_u16le(p.data()+16, 5);
        auto f = make_rsp(0,100,2,p);
        int t = extract_frame(f.data(),(int)f.size(),buf.data(),&len);
        CHECK(t==FRAME_RESPONSE, "hf: sysver frame type 2");
        auto* j = parse_message(buf.data(),len,t);
        CHECK(has(j,"\"hw\":\"hf_rpe\""),       "hf: sysver hw=hf_rpe");
        CHECK(has(j,"\"group_id\":100"),         "hf: sysver group 100");
        CHECK(has(j,"\"unit_id\":2"),            "hf: sysver unit 2");
        CHECK(has(j,"\"processor_id\":5"),       "hf: sysver processor_id 5");
        CHECK(has(j,"\"status\":0"),             "hf: sysver status 0");
        free_result(j); }

    // ── Temperature  100/10 ─────────────────────────────────────────────────
    {   std::vector<uint8_t> p(16,0);
        float ti=42.5f,te=30.0f,tc=55.1f,tf=63.7f; uint32_t b;
        std::memcpy(&b,&ti,4); store_u32le(p.data()+0,b);
        std::memcpy(&b,&te,4); store_u32le(p.data()+4,b);
        std::memcpy(&b,&tc,4); store_u32le(p.data()+8,b);
        std::memcpy(&b,&tf,4); store_u32le(p.data()+12,b);
        auto f = make_rsp(0,100,10,p);
        int t = extract_frame(f.data(),(int)f.size(),buf.data(),&len);
        auto* j = parse_message(buf.data(),len,t);
        CHECK(has(j,"\"group_id\":100"),     "hf: temp group 100");
        CHECK(has(j,"\"unit_id\":10"),       "hf: temp unit 10");
        CHECK(has(j,"internal_temp_c"),      "hf: temp internal_temp_c");
        CHECK(has(j,"fpga_temp_c"),          "hf: temp fpga_temp_c");
        free_result(j); }

    // ── FH Detection  101/40 ────────────────────────────────────────────────
    {   std::vector<uint8_t> p;
        uint8_t hc[4]; store_u16le(hc,1); store_u16le(hc+2,0);
        p.insert(p.end(),hc,hc+4);
        std::vector<uint8_t> hop(40,0);
        store_u32le(hop.data()+0, 9);
        float fm=5.0f; uint32_t b; std::memcpy(&b,&fm,4);
        store_u32le(hop.data()+4,b);
        store_u16le(hop.data()+32,1);  // active
        p.insert(p.end(),hop.begin(),hop.end());
        auto f = make_rsp(0,101,40,p);
        int t = extract_frame(f.data(),(int)f.size(),buf.data(),&len);
        auto* j = parse_message(buf.data(),len,t);
        CHECK(has(j,"\"hopper_count\":1"),  "hf: fh hopper_count 1");
        CHECK(has(j,"\"hopper_number\":9"), "hf: fh hopper_number 9");
        CHECK(has(j,"5e+06")||has(j,"5000000"), "hf: fh min_freq_hz 5 MHz");
        CHECK(has(j,"\"active\":true"),     "hf: fh active true");
        free_result(j); }

    // ── Wideband FFT  101/44 ─────────────────────────────────────────────────
    {   std::vector<uint8_t> p(16+2*4,0);
        store_u32le(p.data()+0,1);   // start 1 MHz
        store_u32le(p.data()+4,30);  // stop  30 MHz
        store_u32le(p.data()+8,500); // step  500 kHz
        store_u32le(p.data()+12,2);  // 2 points
        float pw=-50.0f; uint32_t b; std::memcpy(&b,&pw,4);
        store_u32le(p.data()+16,b);
        auto f = make_rsp(0,101,44,p);
        int t = extract_frame(f.data(),(int)f.size(),buf.data(),&len);
        auto* j = parse_message(buf.data(),len,t);
        CHECK(has(j,"\"unit_id\":44"),      "hf: fft unit 44");
        CHECK(has(j,"\"point_count\":2"),   "hf: fft point_count 2");
        CHECK(has(j,"power_dbm"),           "hf: fft power_dbm array");
        free_result(j); }

    // ── FF Detection  101/70 ─────────────────────────────────────────────────
    {   std::vector<uint8_t> p;
        uint8_t fc[4]; store_u16le(fc,1); store_u16le(fc+2,0);
        p.insert(p.end(),fc,fc+4);
        std::vector<uint8_t> det(32,0);
        float fq=15.5f; uint32_t b; std::memcpy(&b,&fq,4);
        store_u32le(det.data()+0,b);       // 15.5 MHz
        store_u16le(det.data()+20,1);      // active
        p.insert(p.end(),det.begin(),det.end());
        auto f = make_rsp(0,101,70,p);
        int t = extract_frame(f.data(),(int)f.size(),buf.data(),&len);
        auto* j = parse_message(buf.data(),len,t);
        CHECK(has(j,"\"unit_id\":70"),      "hf: ff unit 70");
        CHECK(has(j,"\"freq_count\":1"),    "hf: ff freq_count 1");
        CHECK(has(j,"\"active\":true"),     "hf: ff active true");
        free_result(j); }

    // ── Burst Detection  101/84 ──────────────────────────────────────────────
    {   std::vector<uint8_t> p;
        uint8_t bc[4]; store_u16le(bc,1); store_u16le(bc+2,0);
        p.insert(p.end(),bc,bc+4);
        std::vector<uint8_t> bst(36,0);
        float cf=20.0f; uint32_t b; std::memcpy(&b,&cf,4);
        store_u32le(bst.data()+0,b);       // center 20 MHz
        store_u32le(bst.data()+24,5);      // 5 detected bursts
        p.insert(p.end(),bst.begin(),bst.end());
        auto f = make_rsp(0,101,84,p);
        int t = extract_frame(f.data(),(int)f.size(),buf.data(),&len);
        auto* j = parse_message(buf.data(),len,t);
        CHECK(has(j,"\"unit_id\":84"),          "hf: burst unit 84");
        CHECK(has(j,"\"burst_count\":1"),        "hf: burst_count 1");
        CHECK(has(j,"\"detected_bursts\":5"),    "hf: detected_bursts 5");
        free_result(j); }

    // ── format_response round-trip ───────────────────────────────────────────
    {   const char* jr = "{\"group_id\":101,\"unit_id\":26,\"status\":0}";
        int n = format_response(jr, buf.data());
        CHECK(n==RESP_OVERHEAD, "hf: format_response ACK == 18 bytes");
        std::vector<uint8_t> tmp(MAX_FRAME_BUFFER_BYTES);
        int t2=0;
        CHECK(extract_frame(buf.data(),n,tmp.data(),&t2)==FRAME_RESPONSE,
              "hf: format_response round-trip"); }

    // ── Unknown unit -> raw_hex fallback ─────────────────────────────────────
    {   auto f = make_rsp(0,109,12,{0x01,0x02,0x03,0x04});
        int t = extract_frame(f.data(),(int)f.size(),buf.data(),&len);
        auto* j = parse_message(buf.data(),len,t);
        CHECK(has(j,"\"raw_hex\":\"01020304\""), "hf: unknown unit -> raw_hex");
        free_result(j); }

    std::printf("\n%s (%d failures)\n",
                g_fail ? "TESTS FAILED" : "ALL TESTS PASSED", g_fail);
    return g_fail ? 1 : 0;
}
