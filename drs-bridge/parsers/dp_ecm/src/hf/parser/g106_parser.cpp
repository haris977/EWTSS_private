// drs-bridge/parsers/dp_ecm/src/hf/parser/g106_parser.cpp
// Group 106 — ECM Immediate Jamming: parse-side decode functions and dispatchers.
#include "sdfc_endian.h"
#include "json_writer.h"
#include "hf_groups.h"
using namespace sdfc;

// =============================================================================
// Group 106 shared helper
// =============================================================================

// Shared helper: decode S_JAM_CONFIGURATION block (20 bytes). Same layout as VU Group 106.
static void decode_jam_config_block(const uint8_t* p, JsonWriter& w) {
    static const char*    FREQ_MODE[]   = {"single", "tdm", "fdm", "sweep", "comb_noise"};
    static const unsigned PA_POWER_W[]  = {63, 125, 250, 500, 750};
    static const char*    MOD_TYPE[]    = {nullptr, "cw", "fm", "am", "ssb", "wideband_noise"};
    static const char*    EXCITER_SIG[] = {nullptr,
                                            "external", "single", "two_tone",
                                            "wgn", "pink_noise", "swept", "programmable"};
    static const double   FM_DEV_KHZ[]  = {1.5, 3.0, 5.0, 12.5, 25.0, 50.0, 100.0,
                                            150.0, 250.0, 500.0, 1000.0};
    static const double   WBN_BW_MHZ[]  = {0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0, 20.0, 29.0};

    uint8_t fm = p[0];
    w.key_uint("frequency_mode", fm);
    w.key_str("frequency_mode_name", fm < 5 ? FREQ_MODE[fm] : "unknown");
    w.key_uint("num_frequencies", p[1]);
    w.key_uint("range_selection", p[2]);
    uint8_t pa_idx = p[3];
    w.key_uint("pa_power_selection_index", pa_idx);
    if (pa_idx < 5) w.key_uint("pa_power_w", PA_POWER_W[pa_idx]);
    uint8_t mod = p[4];
    w.key_uint("modulation", mod);
    if (mod >= 1 && mod <= 5) w.key_str("modulation_name", MOD_TYPE[mod]);
    uint8_t exc = p[5];
    w.key_uint("exciter_mod_signal", exc);
    if (exc >= 1 && exc <= 7) w.key_str("exciter_mod_signal_name", EXCITER_SIG[exc]);
    uint8_t fm_idx = p[6];
    w.key_uint("fm_deviation_index", fm_idx);
    if (fm_idx < 11) w.key_double("fm_deviation_khz", FM_DEV_KHZ[fm_idx]);
    w.key_str("ssb_type", p[7] == 0 ? "lsb" : p[7] == 1 ? "usb" : "unknown");
    w.key_double("sweep_switch_time_ms", load_f64le(p + 8));
    uint8_t wbn_idx = p[16];
    w.key_uint("wbn_bw_selection_index", wbn_idx);
    if (wbn_idx < 10) w.key_double("wbn_bandwidth_mhz", WBN_BW_MHZ[wbn_idx]);
}

// =============================================================================
// Group 106 command decoders
// =============================================================================

// 106/1 — Start Immediate Jam (Single Frequency) command (28 bytes, per ICD Table 152).
// S_JAM_CONFIGURATION (20B) + Single Frequency in MHz (8B, double).
static void decode_cmd_start_immediate_jam(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 28) { w.key_str("warning", "start_immediate_jam cmd < 28 bytes"); return; }
    decode_jam_config_block(p, w);
    w.key_double("jam_freq_hz", load_f64le(p + 20) * 1e6);
}

// 106/3 — Generate Multi Frequency TDM command (80 bytes, per ICD Table 158).
// S_JAM_CONFIGURATION (20B) + S_TDM_CONFIGURATION (60B).
// S_TDM_CONFIGURATION: tdm_frequencies[6] (6×double=48B) + modulating_signal (uint32) + on_time_us (uint32) + off_time_us (uint32).
static void decode_cmd_generate_multi_freq_tdm(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 80) { w.key_str("warning", "generate_multi_freq_tdm cmd < 80 bytes"); return; }
    decode_jam_config_block(p, w);
    const uint8_t* t = p + 20;
    std::string freqs = "[";
    for (int i = 0; i < 6; ++i) {
        if (i) freqs += ',';
        char tmp[32];
        std::snprintf(tmp, sizeof(tmp), "%.6g", load_f64le(t + i * 8) * 1e6);
        freqs += tmp;
    }
    freqs += "]";
    w.key_raw("tdm_frequencies_hz",  freqs);
    w.key_uint("modulating_signal",  load_u32le(t + 48));
    w.key_uint("on_time_us",         load_u32le(t + 52));
    w.key_uint("off_time_us",        load_u32le(t + 56));
}

// 106/5 — Generate Multi Carrier FDM command (68 bytes, per ICD Table 160).
// S_JAM_CONFIGURATION (20B) + S_FDM_CONFIGURATION (48B).
// S_FDM_CONFIGURATION: fdm_frequencies[6] (6×double=48B).
static void decode_cmd_generate_multi_carrier_fdm(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 68) { w.key_str("warning", "generate_multi_carrier_fdm cmd < 68 bytes"); return; }
    decode_jam_config_block(p, w);
    const uint8_t* f = p + 20;
    std::string freqs = "[";
    for (int i = 0; i < 6; ++i) {
        if (i) freqs += ',';
        char tmp[32];
        std::snprintf(tmp, sizeof(tmp), "%.6g", load_f64le(f + i * 8) * 1e6);
        freqs += tmp;
    }
    freqs += "]";
    w.key_raw("fdm_frequencies_hz", freqs);
}

// 106/39 — Configure External Modulation Jam Data command (8196 bytes, per ICD Table 154).
// S_EXT_MODULATION_JAM_DATA: audio_data_size (uint32) + external_audio_data (4096 × uint16).
static void decode_cmd_configure_ext_modulation(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8196) { w.key_str("warning", "configure_ext_modulation cmd < 8196 bytes"); return; }
    uint32_t audio_size = load_u32le(p + 0);
    w.key_uint("audio_data_size", audio_size);
    const uint32_t MAX_SAMPLES = 4096;
    uint32_t valid = audio_size < MAX_SAMPLES ? audio_size : MAX_SAMPLES;
    std::string arr = "[";
    for (uint32_t i = 0; i < valid; ++i) {
        if (i) arr += ',';
        arr += std::to_string(load_u16le(p + 4 + i * 2));
    }
    arr += "]";
    w.key_raw("audio_data", arr);
}

// 106/41 — Generate Sweep Frequency command (192 bytes, per ICD Table 162).
// Layout: sweep_start/stop (2×double=16B) + sweep_rate/mod/step/power (4×uint8=4B)
//         + sweep_time (float=4B) + protected_start[10]/stop[10] (2×80B=160B)
//         + num_bands/time_sel/ssb/fm_dev/sweep_mod (5×uint8) + 3×reserved.
static void decode_cmd_generate_sweep_freq(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 192) { w.key_str("warning", "generate_sweep_freq cmd < 192 bytes"); return; }
    static const double   SWEEP_STEP_KHZ[] = {2.5, 5.0, 12.5, 25.0, 50.0, 100.0};
    static const unsigned POWER_W[]        = {63, 125, 250, 500, 750};

    w.key_double("sweep_start_freq_hz",    load_f64le(p +  0) * 1e6);
    w.key_double("sweep_stop_freq_hz",     load_f64le(p +  8) * 1e6);
    w.key_uint("sweep_rate",               p[16]);
    w.key_uint("modulation_type",          p[17]);
    uint8_t step_idx = p[18];
    w.key_uint("sweep_step_index", step_idx);
    if (step_idx < 6) w.key_double("sweep_step_khz", SWEEP_STEP_KHZ[step_idx]);
    uint8_t pwr_idx = p[19];
    w.key_uint("power_level_index", pwr_idx);
    if (pwr_idx < 5) w.key_uint("power_level_w", POWER_W[pwr_idx]);
    w.key_double("freq_sweep_time_ms",     static_cast<double>(load_f32le(p + 20)));

    uint8_t n_bands = p[184];
    w.key_uint("num_protected_bands", n_bands);
    if (n_bands > 0 && n_bands <= 10) {
        std::string starts = "[", stops = "[";
        for (uint8_t i = 0; i < n_bands; ++i) {
            if (i) { starts += ','; stops += ','; }
            char tmp[32];
            std::snprintf(tmp, sizeof(tmp), "%.6g", load_f64le(p +  24 + i * 8) * 1e6);
            starts += tmp;
            std::snprintf(tmp, sizeof(tmp), "%.6g", load_f64le(p + 104 + i * 8) * 1e6);
            stops += tmp;
        }
        starts += "]"; stops += "]";
        w.key_raw("protected_band_starts_hz", starts);
        w.key_raw("protected_band_stops_hz",  stops);
    }
    w.key_bool("freq_sweep_time_auto",    p[185] == 0);
    w.key_uint("ssb_type",               p[186]);
    w.key_uint("fm_deviation",           p[187]);
    w.key_uint("sweep_modulating_signal",p[188]);
}

// 106/55 — Generate Comb Noise command (20 bytes, per ICD Table 167).
// @0 comb_start_freq (double, MHz→Hz) @8 comb_stop_freq (double) @16 comb_step (uint8) @17 power_level (uint8) @18-19 reserved.
static void decode_cmd_generate_comb_noise(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 20) { w.key_str("warning", "generate_comb_noise cmd < 20 bytes"); return; }
    static const double   COMB_STEP_KHZ[] = {1.5625, 3.125, 6.25, 12.5};
    static const unsigned POWER_W[]       = {63, 125, 250, 500, 750};

    w.key_double("comb_start_freq_hz", load_f64le(p + 0) * 1e6);
    w.key_double("comb_stop_freq_hz",  load_f64le(p + 8) * 1e6);
    uint8_t step_idx = p[16];
    w.key_uint("comb_step_index", step_idx);
    if (step_idx < 4) w.key_double("comb_step_khz", COMB_STEP_KHZ[step_idx]);
    uint8_t pwr_idx = p[17];
    w.key_uint("power_level_index", pwr_idx);
    if (pwr_idx < 5) w.key_uint("power_level_w", POWER_W[pwr_idx]);
}

// 106/21 — Enable/Disable Power Amplifier command (4 bytes, per ICD Table 174).
// @0 enable (uint8): 1=enable, 0=disable; @1-3 reserved.
static void decode_cmd_enable_pa(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "enable_pa cmd < 4 bytes"); return; }
    w.key_bool("pa_enabled", p[0] == 1);
}

// 106/45 — Enable/Disable PA and SDU Controls command (4 bytes, per ICD Table 172).
// @0 enable (uint8): 1=enable, 0=disable; @1-3 reserved.
static void decode_cmd_enable_pa_sdu(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "enable_pa_sdu cmd < 4 bytes"); return; }
    w.key_bool("pa_sdu_enabled", p[0] == 1);
}

// 106/49 — Configure Programmable Exciter Modulating Signal command (2052 bytes, per ICD Table 156).
// S_EXCITER_PROG_NOISE: channel_no (uint16) + data_size (uint16) + data_values (1024 × uint16).
static void decode_cmd_configure_prog_exciter(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 2052) { w.key_str("warning", "configure_prog_exciter cmd < 2052 bytes"); return; }
    w.key_uint("channel_no", load_u16le(p + 0));
    uint16_t data_size = load_u16le(p + 2);
    w.key_uint("data_size", data_size);
    const uint16_t MAX_SAMPLES = 1024;
    uint16_t valid = data_size < MAX_SAMPLES ? data_size : MAX_SAMPLES;
    std::string arr = "[";
    for (uint16_t i = 0; i < valid; ++i) {
        if (i) arr += ',';
        arr += std::to_string(load_u16le(p + 4 + i * 2));
    }
    arr += "]";
    w.key_raw("data_values", arr);
}

// =============================================================================
// Group 106 response decoders
// =============================================================================

// 200/20 — Configure External Modulation response (4 bytes). Same as VU 106/40.
static void decode_ext_modulation_response(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "ext_modulation_response < 4 bytes"); return; }
    w.key_uint("software_buffer_size", load_u32le(p + 0));
}

// 106/10 — Stop Jamming response (4 bytes, per ICD Table 171).
// @0 Exciter Retry Count (uint8)  @1 Reserved (3 bytes)
static void decode_stop_immediate_jam_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "stop_immediate_jam_rsp payload < 4 bytes"); return; }
    w.key_uint("exciter_retry_count", p[0]);
    // p[1-3] reserved
}

// 106/54 — Start Immediate Jam ACK (8 bytes).
// HF-specific response on Group 106 Unit 54.
// @0 JamId (uint16)  @2 Reserved (uint16)  @4 Active (uint16)  @6 Reserved (uint16)
static void decode_immediate_jam_ack(const uint8_t* p, int n, JsonWriter& w) {
    if (n >= 8) {
        w.key_uint("jam_id",     load_u16le(p + 0));
        w.key_bool("jam_active", load_u16le(p + 4) == 1);
    } else if (n > 0) {
        w.key_str("warning", "immediate_jam_ack payload < 8 bytes");
        w.key_str("raw_hex", to_hex(p, n));
    }
}

// =============================================================================
// Group 106 dispatchers
// =============================================================================

bool g106_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  1: decode_cmd_start_immediate_jam(p, n, w);          return true;
        case  3: decode_cmd_generate_multi_freq_tdm(p, n, w);      return true;
        case  5: decode_cmd_generate_multi_carrier_fdm(p, n, w);   return true;
        case  9: return true;                                       // Stop Immediate Jamming — 0 bytes
        case 21: decode_cmd_enable_pa(p, n, w);                    return true;
        case 39: decode_cmd_configure_ext_modulation(p, n, w);     return true;
        case 41: decode_cmd_generate_sweep_freq(p, n, w);          return true;
        case 45: decode_cmd_enable_pa_sdu(p, n, w);                return true;
        case 49: decode_cmd_configure_prog_exciter(p, n, w);       return true;
        case 55: decode_cmd_generate_comb_noise(p, n, w);          return true;
        default: return false;
    }
}

bool g106_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  2: return true;                                       // Start Immediate Jam ACK
        case  4: return true;                                       // TDM Jam ACK
        case  6: return true;                                       // FDM Jam ACK
        case 10: decode_stop_immediate_jam_rsp(p, n, w);           return true;
        case 22: return true;                                       // Enable PA ACK
        case 40: decode_ext_modulation_response(p, n, w);          return true;
        case 42: return true;                                       // Sweep Jam ACK
        case 46: return true;                                       // Enable PA+SDU ACK
        case 50: return true;                                       // Configure Prog Exciter ACK
        case 54: decode_immediate_jam_ack(p, n, w);                return true;
        case 56: return true;                                       // Comb Noise ACK
        default: return false;
    }
}
