// drs-bridge/parsers/dp_ecm/src/hf/parser/mrx_g5_parser.cpp
// MRx Group 5 — Tuner: parse-side decode functions and dispatchers.
#include "sdfc_endian.h"
#include "json_writer.h"
#include "hf_groups.h"
#include "hf_shared.h"
#include <cstring>
#include <cstdio>
#include <string>
using namespace sdfc;

// =============================================================================
// MRx Group 5 decode functions
// =============================================================================

// 5/14 — Read AGC/MGC Attenuation response (4 bytes).
static void decode_mrx_agc_status_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) return;
    w.key_uint("rf_attenuation_db", p[0]);
    w.key_uint("if_attenuation_db", p[1]);
    w.key_uint("agc_running",       load_u16le(p + 2));
    w.key_str("agc_status",         load_u16le(p + 2) ? "running" : "manual");
}

// 5/1 — Set Center Frequency command (12 bytes).
static void decode_mrx_set_center_freq_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 12) { w.key_str("warning", "mrx_set_center_freq < 12 bytes"); return; }
    w.key_uint("mrx_channel",         load_u16le(p + 0));
    w.key_uint("bite_antenna_sel",     load_u16le(p + 2));
    w.key_str("antenna_path",          load_u16le(p + 2) == 0 ? "antenna" : "bite");
    w.key_double("center_freq_hz",     load_f64le(p + 4) * 1e6);
}

// 5/3 — Attenuation Selection command (16 bytes, per ICD Table 256).
// @0 mrx_channel(uint16) @2 reserved(uint16) @4 rf_attenuation(float) @8 if_attenuation(float) @12 cal_value_debug(4B, not decoded).
static void decode_mrx_attenuation_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) { w.key_str("warning", "mrx_attenuation < 16 bytes"); return; }
    w.key_uint("mrx_channel",         load_u16le(p + 0));
    w.key_double("rf_attenuation_db", static_cast<double>(load_f32le(p + 4)));
    w.key_double("if_attenuation_db", static_cast<double>(load_f32le(p + 8)));
}

// =============================================================================
// MRx Group 5 dispatchers
// =============================================================================

bool mrx_g5_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  1: decode_mrx_set_center_freq_cmd(p, n, w); return true;
        case  3: decode_mrx_attenuation_cmd(p, n, w);     return true;
        case  9: decode_mrx_channel_cmd(p, n, w);         return true; // Clear Center Freq
        case 13: return true;                             // Read AGC/MGC — 0 bytes
        default: return false;
    }
}

bool mrx_g5_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  2: return true;                            // Set Center Freq ACK
        case  4: return true;                            // Attenuation Select ACK
        case 10: return true;                            // Clear Center Freq ACK
        case 14: decode_mrx_agc_status_rsp(p, n, w);   return true;
        default: return false;
    }
}
