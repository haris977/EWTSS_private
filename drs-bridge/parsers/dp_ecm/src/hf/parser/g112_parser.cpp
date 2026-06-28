// drs-bridge/parsers/dp_ecm/src/hf/parser/g112_parser.cpp
// Group 112 — Fast Scan / Simulation: parse-side decode functions and dispatchers.
#include "sdfc_endian.h"
#include "json_writer.h"
#include "hf_groups.h"
#include "hf_json_utils.h"
using namespace sdfc;

// =============================================================================
// Group 112 command/response decoders — ASU/SDU + Auto Scan + Simulation Mode
// =============================================================================

// 112/1 — ASU/SDU Configuration command (8 bytes, per ICD Table 192).
// @0 ASU SDU Signal Name (uint32, 4B)  @4 ASU SDU Signal Value (uint32, 4B)
static void decode_cmd_asu_sdu_config(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "asu_sdu_config cmd < 8 bytes"); return; }
    w.key_uint("asu_sdu_signal_name",  load_u32le(p + 0));
    w.key_uint("asu_sdu_signal_value", load_u32le(p + 4));
}

// 112/5 — Auto Scan Band Configuration command.
// @0 BandCount (uint8) + 3 reserved + BandCount × S_SCAN_BAND (12B each).
// S_SCAN_BAND: StartFreqMHz (float) + StopFreqMHz (float) + DwellTimeMs (float).
static void decode_cmd_auto_scan_band_config(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "auto_scan_band_config cmd < 4 bytes"); return; }
    uint8_t band_count = p[0];
    w.key_uint("band_count", band_count);
    const int ELEM = 12;
    int off = 4;
    std::string arr = "[";
    uint8_t emitted = 0;
    for (uint8_t i = 0; i < band_count; ++i) {
        if (off + ELEM > n) break;
        const uint8_t* e = p + off;
        JsonWriter b;
        b.key_double("start_freq_hz",  static_cast<double>(load_f32le(e + 0)) * 1e6);
        b.key_double("stop_freq_hz",   static_cast<double>(load_f32le(e + 4)) * 1e6);
        b.key_double("dwell_time_ms",  static_cast<double>(load_f32le(e + 8)));
        if (emitted++) arr += ',';
        arr += b.str();
        off += ELEM;
    }
    arr += "]";
    w.key_raw("scan_bands", arr);
}

// 112/13 — Simulation Mode Configuration command.
// @0 SimMode (uint8): 0=Disable, 1=Enable + 3 reserved.
// Remaining payload contains simulation parameters; emitted as raw_hex.
static void decode_cmd_simulation_mode_config(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "simulation_mode_config cmd < 4 bytes"); return; }
    w.key_str("sim_mode", p[0] == 0 ? "disable" : "enable");
    if (n > 4) w.key_str("sim_params_hex", to_hex(p + 4, n - 4));
}

// 112/2 — ASU/SDU Enable/Disable response (4 bytes, per ICD Table 193).
// @0 Error Value (int16_t, 2B)  @2 Reserved (int16_t, 2B)
static void decode_asu_sdu_config_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "asu_sdu_config_rsp payload < 4 bytes"); return; }
    w.key_int("error_value", static_cast<int16_t>(load_u16le(p + 0)));
    // p[2-3] reserved
}

// 112/4 — TRSDU Receiver Line Status response (4 bytes, per ICD Table 195).
// @0 TR SDU Health Status (uint8, 1B)  @1-3 Reserved (3 bytes)
static void decode_trsdu_receiver_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "trsdu_receiver_status payload < 4 bytes"); return; }
    w.key_uint("tr_sdu_health_status", p[0]);
    // p[1-3] reserved
}

// 112/6 — Power Amplifier Receiver Line Status response (4 bytes, per ICD Table 197).
// @0 Power Amplifier Status (uint8, 1B)  @1-3 Reserved (3 bytes)
static void decode_pa_receiver_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "pa_receiver_status payload < 4 bytes"); return; }
    w.key_uint("power_amplifier_status", p[0]);
    // p[1-3] reserved
}

bool g112_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  1: decode_cmd_asu_sdu_config(p, n, w);         return true;
        case  3: return true;                                 // TRSDU Receiver Line Status — 0 bytes
        case  5: return true;                                 // PA Receiver Line Status — 0 bytes
        case 13: decode_cmd_simulation_mode_config(p, n, w); return true;
        case 37: return true;                                 // HF fast scan cmd — 0 bytes
        default: return false;
    }
}

bool g112_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  2: decode_asu_sdu_config_rsp(p, n, w);       return true;
        case  4: decode_trsdu_receiver_status(p, n, w);    return true;
        case  6: decode_pa_receiver_status(p, n, w);       return true;
        case 14: return true;                               // Simulation Mode Config ACK
        case 38: return true;                               // HF fast scan ACK
        default: return false;
    }
}
