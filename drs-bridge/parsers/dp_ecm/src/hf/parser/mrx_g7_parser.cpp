// drs-bridge/parsers/dp_ecm/src/hf/parser/mrx_g7_parser.cpp
// MRx Group 7 — Signal BITE / misc: parse-side decode functions and dispatchers.
#include "sdfc_endian.h"
#include "json_writer.h"
#include "hf_groups.h"
#include "hf_shared.h"
#include <cstring>
#include <cstdio>
#include <string>
using namespace sdfc;

// =============================================================================
// MRx Group 7 decode functions
// =============================================================================

// 7/1 — Signal BITE command (12 bytes).
static void decode_mrx_signal_bite_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 12) return;
    w.key_double("bite_freq_hz", load_f64le(p + 0) * 1e6);
    w.key_uint("mrx_channel",    load_u16le(p + 8));
}

// 7/2 — Signal BITE response (16 bytes).
static void decode_mrx_signal_bite_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) return;
    w.key_double("observed_freq_hz",   load_f64le(p + 0) * 1e6);
    w.key_double("observed_power_dbm", static_cast<double>(load_f32le(p + 8)));
    w.key_uint("result",               load_u16le(p + 12));
    w.key_str("result_name",           load_u16le(p + 12) == 1 ? "pass" : "fail");
}

// 7/17 — Spectrum Average Count Selection command (8 bytes, per ICD Table 285).
// @0 averaging_enabled(uint16) @2 avg_count(uint16) @4 mrx_channel(uint16) @6 reserved(uint16).
static void decode_mrx_spectrum_avg_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "spectrum_avg cmd < 8 bytes"); return; }
    w.key_bool("averaging_enabled", load_u16le(p + 0) == 1);
    w.key_uint("avg_count",         load_u16le(p + 2));
    w.key_uint("mrx_channel",       load_u16le(p + 4));
}

// 7/21 — Audio Squelch command (8 bytes).
static void decode_mrx_audio_squelch_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) return;
    w.key_uint("squelch_selection", load_u16le(p + 0));
    w.key_str("squelch_state",      load_u16le(p + 0) == 1 ? "on" : "off");
    w.key_uint("mrx_channel",       load_u16le(p + 2));
    w.key_double("threshold_dbm",   static_cast<double>(load_f32le(p + 4)));
}

// 7/23 — Set Date and Time command (8 bytes). Same layout as ECM Group 109/11.
static void decode_mrx_date_time_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) return;
    w.key_uint("day",     p[0]);
    w.key_uint("month",   p[1]);
    w.key_uint("year",    load_u16le(p + 2));
    w.key_uint("hour",    p[4]);
    w.key_uint("minute",  p[5]);
    w.key_uint("seconds", p[6]);
}

// =============================================================================
// MRx Group 7 dispatchers
// =============================================================================

bool mrx_g7_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  1: decode_mrx_signal_bite_cmd(p, n, w);                                                   return true;
        case  3: decode_mrx_sel_channel_cmd("bite_antenna_sel","bite","antenna",p,n,w);                  return true;
        case  5: decode_mrx_sel_channel_cmd("ref_source_sel","external","internal",p,n,w);               return true;
        case  9: decode_mrx_sel_channel_cmd("afc_sel","on","off",p,n,w);                                 return true;
        case 11: decode_mrx_sel_channel_cmd("rf_squelch_sel","on","off",p,n,w);                          return true;
        case 13: decode_mrx_channel_cmd(p, n, w);                                                        return true; // IQ socket open
        case 15: decode_mrx_channel_cmd(p, n, w);                                                        return true; // IQ socket close
        case 17: decode_mrx_spectrum_avg_cmd(p, n, w);                                                   return true;
        case 19: decode_mrx_sel_channel_cmd("rf_agc_sel","enable","disable",p,n,w);                      return true;
        case 21: decode_mrx_audio_squelch_cmd(p, n, w);                                                  return true;
        case 23: decode_mrx_date_time_cmd(p, n, w);                                                      return true;
        default: return false;
    }
}

bool mrx_g7_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  2: decode_mrx_signal_bite_rsp(p, n, w); return true;
        case  4: return true;  // BITE/Antenna Select ACK
        case  6: return true;  // Ref Source Select ACK
        case 10: return true;  // AFC ACK
        case 12: return true;  // RF Squelch ACK
        case 14: return true;  // IQ Socket Open ACK
        case 16: return true;  // IQ Socket Close ACK
        case 18: return true;  // Spectrum Avg Count ACK
        case 20: return true;  // Smart RF AGC ACK
        case 22: return true;  // Audio Squelch ACK
        case 24: return true;  // Set Date/Time ACK
        default: return false;
    }
}
