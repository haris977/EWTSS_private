// drs-bridge/parsers/dp_ecm/src/hf/hf_shared.h
// Decode functions shared by multiple HF groups — inline, bodies verbatim from
// dp_ecm_hf_parser.cpp (originally static; promoted to inline for header use).
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include "json_writer.h"
#include "sdfc_endian.h"
#include "hf_json_utils.h"
using namespace sdfc;

// 101/31 / 200/17 — Start Follow-On Jamming command (44 bytes, per ICD Table 115).
// S_TRACKING_WINDOW (16B): hopper_start/stop_freq, detection_start/stop_freq, follow_on_sel, 3×reserved.
// S_TRACKING_INFO  (28B): hop_period, inter_period, pa_power, modulation_type, fm_dev, exciter_sig, hopper_power.
inline void decode_cmd_start_follow_on_jam(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 44) { w.key_str("warning", "start_follow_on_jam cmd < 44 bytes"); return; }
    static const unsigned PA_POWER_W[]  = {63, 125, 250, 500, 1000};
    static const double   FM_DEV_KHZ[]  = {1.5, 3.0, 5.0, 12.5, 25.0, 50.0, 100.0,
                                            150.0, 250.0, 500.0, 1000.0};
    static const char*    EXCITER_SIG[] = {nullptr, nullptr,
                                            "single", "two_tone", "wgn", "pink_noise", "swept"};
    w.key_double("hopper_start_freq_hz",    static_cast<double>(load_f32le(p +  0)) * 1e6);
    w.key_double("hopper_stop_freq_hz",     static_cast<double>(load_f32le(p +  4)) * 1e6);
    w.key_double("detection_start_freq_hz", static_cast<double>(load_u16le(p +  8)) * 1e6);
    w.key_double("detection_stop_freq_hz",  static_cast<double>(load_u16le(p + 10)) * 1e6);
    w.key_uint("follow_on_selection",        p[12]);
    w.key_double("hop_period_ms",   static_cast<double>(load_f32le(p + 16)));
    w.key_double("inter_period_ms", static_cast<double>(load_f32le(p + 20)));
    uint32_t pa_idx = load_u32le(p + 24);
    w.key_uint("pa_power_level_index", pa_idx);
    if (pa_idx < 5) w.key_uint("pa_power_w", PA_POWER_W[pa_idx]);
    w.key_uint("modulation_type",  load_u32le(p + 28));
    uint32_t fm_idx = load_u32le(p + 32);
    w.key_uint("fm_deviation_index", fm_idx);
    if (fm_idx < 11) w.key_double("fm_deviation_khz", FM_DEV_KHZ[fm_idx]);
    uint32_t exc_idx = load_u32le(p + 36);
    w.key_uint("exciter_mod_signal_index", exc_idx);
    if (exc_idx >= 2 && exc_idx <= 6) w.key_str("exciter_mod_signal", EXCITER_SIG[exc_idx]);
    w.key_double("hopper_power_level_dbm", static_cast<double>(load_f32le(p + 40)));
}

// 101/73 / 200/11 — Start List (FF & Burst) Jamming command (1228 bytes, per ICD Table 121).
// S_LIST_JAMMING_INFO (24B) + S_LIST_JAMMING_FREQUENCIES (1204B).
inline void decode_cmd_start_list_jam(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 1228) { w.key_str("warning", "start_list_jam cmd < 1228 bytes"); return; }
    static const unsigned PA_POWER_W[] = {63, 125, 250, 500, 1000};
    static const double   FM_DEV_KHZ[] = {1.5, 3.0, 5.0, 12.5, 25.0, 50.0, 100.0,
                                           150.0, 250.0, 500.0, 1000.0};
    w.key_double("start_freq_hz",    static_cast<double>(load_u16le(p +  0)) * 1e6);
    w.key_double("stop_freq_hz",     static_cast<double>(load_u16le(p +  2)) * 1e6);
    w.key_uint("jam_freq_count",      load_u16le(p +  4));
    w.key_uint("occupancy_threshold", load_u16le(p +  6));
    w.key_uint("jam_cycle_count",     load_u16le(p +  8));
    w.key_uint("look_through_count",  load_u16le(p + 10));
    uint32_t jam_sel = load_u32le(p + 12);
    w.key_uint("jam_selection", jam_sel);
    w.key_str("jam_selection_name", jam_sel == 0 ? "fixed_frequency"
                                  : jam_sel == 1 ? "burst" : "unknown");
    w.key_uint("modulation_type",  p[16]);
    uint8_t fm_idx = p[17];
    w.key_uint("fm_deviation_index", fm_idx);
    if (fm_idx < 11) w.key_double("fm_deviation_khz", FM_DEV_KHZ[fm_idx]);
    uint8_t pa_idx = p[18];
    w.key_uint("pa_power_level_index", pa_idx);
    if (pa_idx < 5) w.key_uint("pa_power_w", PA_POWER_W[pa_idx]);
    w.key_uint("exciter_mod_signal",  p[19]);
    w.key_str("jam_mode", p[20] == 0 ? "tdm" : p[20] == 1 ? "fdm" : "unknown");

    uint16_t freq_count = load_u16le(p + 24);
    w.key_uint("list_freq_count", freq_count);
    if (freq_count == 0 || freq_count > 100) return;

    const int FREQ_BASE = 28;
    const int BW_BASE   = 428;
    const int THR_BASE  = 828;

    if (FREQ_BASE + static_cast<int>(freq_count) * 4 > n) return;
    std::string freqs = "[";
    for (uint16_t i = 0; i < freq_count; ++i) {
        if (i) freqs += ',';
        char tmp[32];
        std::snprintf(tmp, sizeof(tmp), "%.6g",
            static_cast<double>(load_f32le(p + FREQ_BASE + i * 4)) * 1e6);
        freqs += tmp;
    }
    freqs += "]";
    w.key_raw("jam_frequencies_hz", freqs);

    if (BW_BASE + static_cast<int>(freq_count) * 4 <= n) {
        std::string bws = "[";
        for (uint16_t i = 0; i < freq_count; ++i) {
            if (i) bws += ',';
            char tmp[32];
            std::snprintf(tmp, sizeof(tmp), "%.6g",
                static_cast<double>(load_f32le(p + BW_BASE + i * 4)));
            bws += tmp;
        }
        bws += "]";
        w.key_raw("jam_bandwidths_khz", bws);
    }
    if (THR_BASE + static_cast<int>(freq_count) * 4 <= n) {
        std::string thrs = "[";
        for (uint16_t i = 0; i < freq_count; ++i) {
            if (i) thrs += ',';
            char tmp[32];
            std::snprintf(tmp, sizeof(tmp), "%.6g",
                static_cast<double>(load_f32le(p + THR_BASE + i * 4)));
            thrs += tmp;
        }
        thrs += "]";
        w.key_raw("jam_thresholds_dbm", thrs);
    }
}

// 101/92 / 200/21 — Start Responsive Sweep Jam command (208 bytes, per ICD Table 131).
// @0 detection_start/stop (2×double=16B) + S_CMD_RES_SWEEP_JAM (192B).
inline void decode_cmd_start_responsive_sweep_jam(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 208) { w.key_str("warning", "start_responsive_sweep_jam cmd < 208 bytes"); return; }
    static const double   SWEEP_STEP_KHZ[] = {2.5, 5.0, 12.5, 25.0, 50.0, 100.0};
    static const unsigned POWER_W[]        = {63, 125, 250, 500, 1000};

    w.key_double("detection_start_freq_hz", load_f64le(p +  0) * 1e6);
    w.key_double("detection_stop_freq_hz",  load_f64le(p +  8) * 1e6);
    w.key_double("sweep_start_freq_hz",     load_f64le(p + 16) * 1e6);
    w.key_double("sweep_stop_freq_hz",      load_f64le(p + 24) * 1e6);
    w.key_uint("sweep_rate", p[32]);
    w.key_uint("modulation_type", p[33]);
    uint8_t step_idx = p[34];
    w.key_uint("sweep_step_index", step_idx);
    if (step_idx < 6) w.key_double("sweep_step_khz", SWEEP_STEP_KHZ[step_idx]);
    uint8_t pwr_idx = p[35];
    w.key_uint("power_level_index", pwr_idx);
    if (pwr_idx < 5) w.key_uint("power_level_w", POWER_W[pwr_idx]);
    w.key_double("freq_sweep_time_ms", static_cast<double>(load_f32le(p + 36)));

    uint8_t n_bands = p[200];
    w.key_uint("num_protected_bands", n_bands);
    if (n_bands > 0 && n_bands <= 10) {
        std::string starts = "[", stops = "[";
        for (uint8_t i = 0; i < n_bands; ++i) {
            if (i) { starts += ','; stops += ','; }
            char tmp[32];
            std::snprintf(tmp, sizeof(tmp), "%.6g", load_f64le(p +  40 + i * 8) * 1e6);
            starts += tmp;
            std::snprintf(tmp, sizeof(tmp), "%.6g", load_f64le(p + 120 + i * 8) * 1e6);
            stops += tmp;
        }
        starts += "]"; stops += "]";
        w.key_raw("protected_band_starts_hz", starts);
        w.key_raw("protected_band_stops_hz",  stops);
    }
    w.key_bool("freq_sweep_time_auto",   p[201] == 0);
    w.key_uint("ssb_type",               p[202]);
    w.key_uint("fm_deviation",           p[203]);
    w.key_uint("sweep_modulating_signal",p[204]);
}

// 200/13 — Send ECM Reports command (4 bytes). Same structure as VU 101/79.
// 101/79
inline void decode_cmd_send_ecm_reports(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "send_ecm_reports cmd < 4 bytes"); return; }
    w.key_bool("ecm_reports_enabled", p[0] == 1);
}

// Shared helper: decode 4-byte "mrx_channel + reserved" command.
inline void decode_mrx_channel_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n >= 2) w.key_uint("mrx_channel", load_u16le(p));
}
