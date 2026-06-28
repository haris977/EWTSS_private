// drs-bridge/parsers/dp_ecm/src/hf/parser/mrx_g6_parser.cpp
// MRx Group 6 — FH Monitoring / GO2Monitor: parse-side decode functions and dispatchers.
#include "sdfc_endian.h"
#include "json_writer.h"
#include "hf_groups.h"
#include "hf_shared.h"
#include <cstring>
#include <cstdio>
#include <string>
using namespace sdfc;

// =============================================================================
// MRx Group 6 decode functions
// =============================================================================

// 6/7 — FH Monitoring Configuration command (52 bytes).
static void decode_mrx_fh_monitoring_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 52) { w.key_str("warning", "mrx_fh_monitoring < 52 bytes"); return; }
    static const char* RBW[] = {"6.25kHz","12.5kHz","25kHz","50kHz","100kHz","200kHz"};
    static const char* INT[] = {
        "10us","20us","40us","80us","160us","320us","640us","1280us","2560us","5120us","10240us","2048us"
    };
    uint8_t rbw_idx = p[2];
    uint8_t int_idx = p[3];
    w.key_uint("mrx_channel",           load_u16le(p +  0));
    w.key_uint("rbw_index",             rbw_idx);
    w.key_str("rbw_name",               rbw_idx < 6  ? RBW[rbw_idx] : "unknown");
    w.key_uint("integration_time_index",int_idx);
    w.key_str("integration_time_name",  int_idx < 12 ? INT[int_idx] : "unknown");
    w.key_double("fh_start_freq_hz",    load_f64le(p +  4) * 1e6);
    w.key_double("fh_stop_freq_hz",     load_f64le(p + 12) * 1e6);
    w.key_double("hop_period_ms",       static_cast<double>(load_f32le(p + 20)));
    w.key_double("inter_hop_period_ms", static_cast<double>(load_f32le(p + 24)));
    w.key_double("power_level_dbm",     static_cast<double>(load_f32le(p + 28)));
    w.key_double("band_start_freq_hz",  static_cast<double>(load_u32le(p + 40)) * 1e6);
    w.key_double("band_stop_freq_hz",   static_cast<double>(load_u32le(p + 44)) * 1e6);
    w.key_bool("fh_80mhz_stream",       p[48] != 0);
    w.key_bool("fh_enabled",            p[49] != 0);
    w.key_bool("header_enabled",        p[50] != 0);
}

// 6/9 — GO2Monitor Connection Establishment command (260 bytes, per ICD Table 258).
// @0 ip_address(char[128]) @128 port_number(char[128]) @256 mrx_channel(uint16) @258 reserved(uint16).
static void decode_mrx_go2monitor_connect_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 260) { w.key_str("warning", "go2monitor_connect cmd < 260 bytes"); return; }
    const char* ip   = reinterpret_cast<const char*>(p +   0);
    const char* port = reinterpret_cast<const char*>(p + 128);
    w.key_str("ip_address",   std::string(ip,   strnlen(ip,   128)));
    w.key_str("port_number",  std::string(port, strnlen(port, 128)));
    w.key_uint("mrx_channel", load_u16le(p + 256));
}

// 6/13 — Start GO2Monitor Transmission command (144 bytes).
// @0 mrx_channel(u16) @2 bw(u16) @4 reserved(u16) @6 reserved(u16)
// @8 center_freq(double,8) @16 date(char[128])
static void decode_mrx_start_go2monitor_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 144) { w.key_str("warning", "start_go2monitor cmd < 144 bytes"); return; }
    static const char* BW[] = {"1MHz","240kHz","120kHz","60kHz","30kHz"};
    uint16_t bw = load_u16le(p + 2);
    w.key_uint("mrx_channel",       load_u16le(p +  0));
    w.key_uint("bw_selection",      bw);
    w.key_str("bandwidth_name",     bw < 5 ? BW[bw] : "unknown");
    w.key_double("center_freq_hz",  load_f64le(p + 8) * 1e6);
    const char* date = reinterpret_cast<const char*>(p + 16);
    w.key_str("date", std::string(date, strnlen(date, 128)));
}

// =============================================================================
// MRx Group 6 dispatchers
// =============================================================================

bool mrx_g6_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  7: decode_mrx_fh_monitoring_cmd(p, n, w);      return true;
        case  9: decode_mrx_go2monitor_connect_cmd(p, n, w); return true;
        case 11: return true;                                 // GO2Monitor Disconnect — 0 bytes
        case 13: decode_mrx_start_go2monitor_cmd(p, n, w);   return true;
        case 15: return true;                                 // Stop GO2Monitor — 0 bytes
        default: return false;
    }
}

bool mrx_g6_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    (void)p; (void)n; (void)w;
    switch (unit_id) {
        case  8: return true; // FH Monitoring Config ACK
        case 10: return true; // GO2Monitor Connect ACK
        case 12: return true; // GO2Monitor Disconnect ACK
        case 14: return true; // Start GO2Monitor ACK
        case 16: return true; // Stop GO2Monitor ACK
        default: return false;
    }
}
