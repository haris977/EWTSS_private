// drs-bridge/parsers/dp_ecm/src/hf/parser/g101_parser.cpp
// Group 101 — SJC Detection + Jamming: parse-side decode functions and dispatchers.
#include "sdfc_endian.h"
#include "json_writer.h"
#include "hf_groups.h"
#include "hf_shared.h"
using namespace sdfc;

// =============================================================================
// Group 101 command decoders
// (Shared structures with VU; HF-specific only where noted.)
// =============================================================================

// 101/25 — Set Threshold (8 bytes). Per ICD Table 39.
static void decode_cmd_set_threshold(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "set_threshold cmd < 8 bytes"); return; }
    w.key_uint("adc_channel",     p[0]);
    w.key_double("threshold_dbm", static_cast<double>(load_f32le(p + 4)));
}

// 101/27 — Set Resolution (4 bytes). Per ICD Table 41.
static void decode_cmd_set_resolution(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "set_resolution cmd < 4 bytes"); return; }
    static const double RES_KHZ[] = {
        0.390625, 0.78125, 1.5625, 3.125, 6.25, 12.5, 25.0, 50.0, 100.0
    };
    uint8_t res_idx = p[1];
    w.key_uint("channel_no",       p[0]);
    w.key_uint("resolution_index", res_idx);
    if (res_idx < 9) w.key_double("resolution_khz", RES_KHZ[res_idx]);
}

// 101/37 — Configure Detection (12 bytes). Per ICD Table 47.
static void decode_cmd_configure_detection(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 12) { w.key_str("warning", "configure_detection cmd < 12 bytes"); return; }
    static const char* MODE_NAMES[] = {
        "hopper_frequency", "fixed_frequency", "burst_frequency", "combined_signal"
    };
    uint8_t mode = p[6];
    w.key_double("start_freq_hz", static_cast<double>(load_u16le(p + 0)) * 1e6);
    w.key_double("stop_freq_hz",  static_cast<double>(load_u16le(p + 2)) * 1e6);
    w.key_uint("rf_attenuation_db", p[4]);
    w.key_int("if_attenuation_db",  static_cast<int8_t>(p[5]));
    w.key_uint("detection_mode",    mode);
    w.key_str("detection_mode_name", mode < 4 ? MODE_NAMES[mode] : "unknown");
    w.key_bool("zoom_spectrum_enabled", p[7] != 0);
    w.key_bool("agc_enabled",           p[8] != 0);
}

// 101/39 — Start FH Detection (8 bytes). Per ICD Table 51.
static void decode_cmd_start_fh_detection(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "start_fh_detection cmd < 8 bytes"); return; }
    w.key_double("start_freq_hz", static_cast<double>(load_f32le(p + 0)) * 1e6);
    w.key_double("stop_freq_hz",  static_cast<double>(load_f32le(p + 4)) * 1e6);
}

// 101/43 — Get Wideband FFT Data (8 bytes). Per ICD Table 49.
static void decode_cmd_get_wideband_fft(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "get_wideband_fft cmd < 8 bytes"); return; }
    w.key_double("start_freq_hz",    static_cast<double>(load_u16le(p + 0)) * 1e6);
    w.key_double("stop_freq_hz",     static_cast<double>(load_u16le(p + 2)) * 1e6);
    w.key_uint("rf_attenuation_db",  p[4]);
    w.key_uint("if_attenuation_db",  p[5]);
}

// 101/47 — Set Min/Max Pulse Range (16 bytes). Per ICD Table 43.
static void decode_cmd_set_pulse_range(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) { w.key_str("warning", "set_pulse_range cmd < 16 bytes"); return; }
    w.key_double("fh_max_pulse_ms",    static_cast<double>(load_f32le(p +  0)));
    w.key_double("fh_min_pulse_ms",    static_cast<double>(load_f32le(p +  4)));
    w.key_double("burst_max_pulse_ms", static_cast<double>(load_f32le(p +  8)));
    w.key_double("burst_min_pulse_ms", static_cast<double>(load_f32le(p + 12)));
}

// 101/55 — Set Minimum Hops Count (4 bytes). Per ICD Table 45.
static void decode_cmd_set_min_hops(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "set_min_hops cmd < 4 bytes"); return; }
    w.key_uint("min_hops_count", load_u32le(p + 0));
}

// 101/69 — Start FF Detection (8 bytes). Per ICD Table 53.
static void decode_cmd_start_ff_detection(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "start_ff_detection cmd < 8 bytes"); return; }
    w.key_double("start_freq_hz",    static_cast<double>(load_u16le(p + 0)) * 1e6);
    w.key_double("stop_freq_hz",     static_cast<double>(load_u16le(p + 2)) * 1e6);
    w.key_uint("rf_attenuation_db",  p[4]);
    w.key_int("if_attenuation_db",   static_cast<int8_t>(p[5]));
}

// 101/83 — Start Burst Detection (8 bytes). Per ICD Table 57.
static void decode_cmd_start_burst_detection(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "start_burst_detection cmd < 8 bytes"); return; }
    w.key_double("start_freq_hz",    static_cast<double>(load_u16le(p + 0)) * 1e6);
    w.key_double("stop_freq_hz",     static_cast<double>(load_u16le(p + 2)) * 1e6);
    w.key_uint("rf_attenuation_db",  p[4]);
    w.key_int("if_attenuation_db",   static_cast<int8_t>(p[5]));
}

// 101/85 — Start Scan Speed (8 bytes). Per ICD Table 59.
static void decode_cmd_start_scan_speed(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "start_scan_speed cmd < 8 bytes"); return; }
    w.key_double("start_freq_hz",    static_cast<double>(load_u16le(p + 0)) * 1e6);
    w.key_double("stop_freq_hz",     static_cast<double>(load_u16le(p + 2)) * 1e6);
    w.key_uint("rf_attenuation_db",  p[4]);
    w.key_int("if_attenuation_db",   static_cast<int8_t>(p[5]));
}

// 101/94 — Get Zoom Band FFT Data (16 bytes). Per ICD Table 71.
static void decode_cmd_get_zoom_fft(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) { w.key_str("warning", "get_zoom_fft cmd < 16 bytes"); return; }
    static const double BW_MHZ[]    = {0.5, 1.0, 2.0, 4.0, 6.0, 8.0};
    static const int    BIN_COUNT[] = {1280, 1280, 1280, 1463, 1536, 1576};
    int32_t bw_idx      = load_i32le(p + 4);
    int32_t noise_level = load_i32le(p + 8);
    w.key_double("center_freq_hz", static_cast<double>(load_f32le(p + 0)) * 1e6);
    w.key_int("bw_index", bw_idx);
    if (bw_idx >= 0 && bw_idx < 6) {
        w.key_double("bw_mhz", BW_MHZ[bw_idx]);
        w.key_uint("expected_bin_count", static_cast<unsigned>(BIN_COUNT[bw_idx]));
    }
    w.key_bool("noise_leveling_enabled", noise_level == 0);
    w.key_double("threshold_dbm",        static_cast<double>(load_f32le(p + 12)));
}

// 101/158 — Terminate FFT Thread (4 bytes). Per ICD Table 55.
static void decode_cmd_terminate_fft(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "terminate_fft cmd < 4 bytes"); return; }
    w.key_bool("terminate", p[0] == 0);
}

// =============================================================================
// Group 101 response decoders — HF-specific element sizes
// =============================================================================

// 101/40 — FH Detection response.
// HF S_HOPPER_DATA = 40 bytes (VU = 60 bytes — VU adds extension fields).
// Layout per ICD:
//   @0  HopperNumber  (uint32)  @4  MinFreqMHz (float)   @8  MaxFreqMHz (float)
//   @12 PulseLenMs    (float)   @16 InterHopMs (float)   @20 DetectedCount (uint32)
//   @24 TOA H:M:S:rsv (4B)      @28 PowerDbm   (float)   @32 Active (uint16)
//   @34 Reserved      (uint16)  @36 SNR         (float)
static void decode_fh_detection(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "fh payload < 4 bytes"); return; }
    uint16_t count = load_u16le(p + 0);
    w.key_uint("hopper_count", count);

    std::string arr = "[";
    int off = 4;
    const int ELEM = 40;
    uint16_t emitted = 0;
    for (uint16_t i = 0; i < count; ++i) {
        if (off + ELEM > n) break;
        const uint8_t* e = p + off;
        JsonWriter h;
        h.key_uint("hopper_number",         load_u32le(e +  0));
        h.key_double("min_freq_hz",  static_cast<double>(load_f32le(e +  4)) * 1e6);
        h.key_double("max_freq_hz",  static_cast<double>(load_f32le(e +  8)) * 1e6);
        h.key_double("pulse_length_s",      static_cast<double>(load_f32le(e + 12)) / 1e3);
        h.key_double("inter_hop_period_s",  static_cast<double>(load_f32le(e + 16)) / 1e3);
        h.key_uint("detected_count",        load_u32le(e + 20));
        char toa[16];
        std::snprintf(toa, sizeof(toa), "%02u:%02u:%02u", e[24], e[25], e[26]);
        h.key_str("toa", toa);
        h.key_double("power_dbm",   static_cast<double>(load_f32le(e + 28)));
        h.key_bool("active",        load_u16le(e + 32) == 1);
        h.key_double("snr_db",      static_cast<double>(load_f32le(e + 36)));
        if (emitted++) arr += ',';
        arr += h.str();
        off += ELEM;
    }
    arr += "]";
    w.key_raw("hoppers", arr);
}

// 101/44 — Wideband FFT Data response (6408 bytes, per ICD Table 50).
// @0    FFT Bin Count        (uint32)
// @4    Wide Band FFT Data   (float[1600])
// @6404 Scan Speed           (uint32)
static void decode_wideband_fft(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 6408) { w.key_str("warning", "wideband_fft payload < 6408 bytes"); return; }
    w.key_uint("fft_bin_count", load_u32le(p + 0));
    std::string arr = "[";
    for (int i = 0; i < 1600; ++i) {
        if (i) arr += ',';
        char tmp[32];
        std::snprintf(tmp, sizeof(tmp), "%.2f",
            static_cast<double>(load_f32le(p + 4 + i * 4)));
        arr += tmp;
    }
    arr += "]";
    w.key_raw("wideband_fft_data", arr);
    w.key_uint("scan_speed", load_u32le(p + 6404));
}

// Shared helper: decode one S_DETECTED_FIXED_FREQUENCY entry (36 bytes, per ICD Table 54/62).
// Layout (36B):
//   @0  Fixed Frequency (MHz→Hz) (float)  @4  Current Power Level (dBm) (float)
//   @8  Active Count (uint32)             @12 Min Power Level (dBm) (float)
//   @16 Max Power Level (dBm) (float)     @20 S_FIXED_FREQ_TOA H:M:S:rsv (4B)
//   @24 S_FIXED_FREQ_DURATION H:M:S:rsv (4B)  @28 Freq Active (uint16)  @30 Reserved (uint16)
//   @32 SNR (float)
static void decode_ff_entry_hf(const uint8_t* e, JsonWriter& f) {
    f.key_double("freq_hz",           static_cast<double>(load_f32le(e +  0)) * 1e6);
    f.key_double("current_power_dbm", static_cast<double>(load_f32le(e +  4)));
    f.key_uint("active_count",         load_u32le(e +  8));
    f.key_double("min_power_dbm",     static_cast<double>(load_f32le(e + 12)));
    f.key_double("max_power_dbm",     static_cast<double>(load_f32le(e + 16)));
    char toa[16];
    std::snprintf(toa, sizeof(toa), "%02u:%02u:%02u", e[20], e[21], e[22]);
    f.key_str("toa", toa);
    char dur[16];
    std::snprintf(dur, sizeof(dur), "%02u:%02u:%02u", e[24], e[25], e[26]);
    f.key_str("duration", dur);
    f.key_bool("freq_active", load_u16le(e + 28) == 1);
    f.key_double("snr_db",    static_cast<double>(load_f32le(e + 32)));
}

// 101/70 — FF Detection response.
// Part 1: S_RES_WIDEBAND_FFT_DATA (bin_count uint32 + 1600 floats + scan_speed uint32 = 6408B)
// Part 2: S_FIXED_FREQUENCIES (ff_count uint32 + S_DETECTED_FIXED_FREQUENCY × count, 60B each for HF)
static void decode_ff_detection(const uint8_t* p, int n, JsonWriter& w) {
    const int FFT_BLOCK = 4 + 1600 * 4 + 4;
    if (n < FFT_BLOCK) {
        w.key_str("warning", "ff_detection payload too short for wideband FFT block");
        return;
    }
    uint32_t bin_count = load_u32le(p + 0);
    w.key_uint("fft_bin_count", bin_count);
    uint32_t emit_fft = (bin_count < 1600) ? bin_count : 1600;
    std::string fft_arr = "[";
    for (uint32_t i = 0; i < emit_fft; ++i) {
        if (i) fft_arr += ',';
        char tmp[32];
        std::snprintf(tmp, sizeof(tmp), "%.2f",
            static_cast<double>(load_f32le(p + 4 + i * 4)));
        fft_arr += tmp;
    }
    fft_arr += "]";
    w.key_raw("wideband_power_dbm", fft_arr);
    w.key_uint("scan_speed", load_u32le(p + 4 + 1600 * 4));

    const uint8_t* ff_p  = p + FFT_BLOCK;
    int            ff_rem = n - FFT_BLOCK;
    if (ff_rem < 4) return;
    uint32_t ff_count = load_u32le(ff_p + 0);
    w.key_uint("ff_count", ff_count);

    const int FF_ELEM = 36;
    std::string ff_arr = "[";
    int off = 4;
    uint32_t emitted = 0;
    for (uint32_t i = 0; i < ff_count; ++i) {
        if (off + FF_ELEM > ff_rem) break;
        JsonWriter f;
        decode_ff_entry_hf(ff_p + off, f);
        if (emitted++) ff_arr += ',';
        ff_arr += f.str();
        off += FF_ELEM;
    }
    ff_arr += "]";
    w.key_raw("fixed_frequencies", ff_arr);
}

// 101/84 — Burst Detection response (per ICD Table 58).
// Message size: 4 + (24 × Detected Burst Count), max 1600 entries.
// S_DETECTED_BURST_FREQUENCY layout (24 bytes per slot):
//   @0  Fixed Frequency in MHz (float→Hz)  @4  Current Power Level in dBm (float)
//   @8  Pulse Length (float)               @12 Active Count (uint32)
//   @16 TOA H:M:S:reserved (4 bytes)       @20 SNR (float)
static void decode_burst_detection(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "burst_detection payload < 4 bytes"); return; }
    uint32_t burst_count = load_u32le(p + 0);
    w.key_uint("burst_count", burst_count);

    const int      ELEM      = 24;
    const uint32_t MAX_SLOTS = 1600;
    uint32_t valid = burst_count < MAX_SLOTS ? burst_count : MAX_SLOTS;

    std::string arr = "[";
    uint32_t emitted = 0;
    for (uint32_t i = 0; i < valid; ++i) {
        int off = 4 + static_cast<int>(i) * ELEM;
        if (off + ELEM > n) break;
        const uint8_t* e = p + off;
        JsonWriter b;
        b.key_double("freq_hz",           static_cast<double>(load_f32le(e +  0)) * 1e6);
        b.key_double("current_power_dbm", static_cast<double>(load_f32le(e +  4)));
        b.key_double("pulse_length",      static_cast<double>(load_f32le(e +  8)));
        b.key_uint("active_count",         load_u32le(e + 12));
        char toa[16];
        std::snprintf(toa, sizeof(toa), "%02u:%02u:%02u", e[16], e[17], e[18]);
        b.key_str("toa", toa);
        b.key_double("snr_db",            static_cast<double>(load_f32le(e + 20)));
        if (emitted++) arr += ',';
        arr += b.str();
    }
    arr += "]";
    w.key_raw("bursts", arr);
}

// 101/88 — Stop Scan Speed response (per ICD Table 62).
// Fixed-size message: 4 + (36 × 1600) = 57604 bytes. Always 1600 slots; ff_count says how many valid.
static void decode_stop_scan_speed(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 57604) { w.key_str("warning", "stop_scan_speed payload < 57604 bytes"); return; }
    uint32_t ff_count = load_u32le(p + 0);
    w.key_uint("ff_count", ff_count);

    const int      FF_ELEM   = 36;
    const uint32_t MAX_SLOTS = 1600;
    uint32_t valid = ff_count < MAX_SLOTS ? ff_count : MAX_SLOTS;

    std::string arr = "[";
    uint32_t emitted = 0;
    for (uint32_t i = 0; i < valid; ++i) {
        int off = 4 + static_cast<int>(i) * FF_ELEM;
        JsonWriter f;
        decode_ff_entry_hf(p + off, f);
        if (emitted++) arr += ',';
        arr += f.str();
    }
    arr += "]";
    w.key_raw("fixed_frequencies", arr);
}

// 101/95 — Zoom Band FFT Data response (1600 floats). Same as VU.
static void decode_zoom_fft(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "zoom_fft payload < 4 bytes"); return; }
    uint32_t emit = static_cast<uint32_t>(n / 4);
    if (emit > 1600) emit = 1600;
    w.key_uint("sample_count", emit);
    std::string arr = "[";
    for (uint32_t i = 0; i < emit; ++i) {
        if (i) arr += ',';
        char tmp[32];
        std::snprintf(tmp, sizeof(tmp), "%.2f",
            static_cast<double>(load_f32le(p + i * 4)));
        arr += tmp;
    }
    arr += "]";
    w.key_raw("power_dbm", arr);
}

// =============================================================================
// Group 101 new detection/jamming command decoders
// =============================================================================

// 101/63 — Set Auto/Manual Tracking Configuration (4 bytes).
static void decode_cmd_tracking_config(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "tracking_config cmd < 4 bytes"); return; }
    w.key_uint("tracking_configuration", p[0]);
    w.key_str("tracking_mode", p[0] == 0 ? "auto" : "manual");
}

// 101/100 — Set Flatness Mode command (12 bytes).
// @0 FlatnessModeSelect (uint32): 0=Disable, 1=Min Point, 2=Avg Point
// @4 StartFrequencyMHz (float)  @8 StopFrequencyMHz (float)
static void decode_cmd_set_flatness_mode(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 12) { w.key_str("warning", "set_flatness_mode cmd < 12 bytes"); return; }
    static const char* MODE[] = {"disable", "min_point", "avg_point"};
    uint32_t mode = load_u32le(p + 0);
    w.key_uint("flatness_mode", mode);
    w.key_str("flatness_mode_name", mode < 3 ? MODE[mode] : "unknown");
    w.key_double("start_freq_hz", static_cast<double>(load_f32le(p + 4)) * 1e6);
    w.key_double("stop_freq_hz",  static_cast<double>(load_f32le(p + 8)) * 1e6);
}

// 101/102 — Set Integration Time command (4 bytes).
// @0 IntegrationTimeIndex (uint32): 0=20us, 1=40us, ... 15=655360us
static void decode_cmd_set_integration_time(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "set_integration_time cmd < 4 bytes"); return; }
    static const uint32_t INT_US[] = {
        20, 40, 80, 160, 320, 640, 1280, 2560, 5120, 10240, 20480, 40960, 81920, 163840, 327680, 655360
    };
    uint32_t idx = load_u32le(p + 0);
    w.key_uint("integration_time_index", idx);
    if (idx < 16) w.key_uint("integration_time_us", INT_US[idx]);
}

// 101/104 — Set Multi-Band FH Mode command (4 bytes).
// @0 MultiFHModeSelection (uint8): 0=Disable, 1=Enable + 3 reserved
static void decode_cmd_set_multi_band_fh(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "set_multi_band_fh cmd < 4 bytes"); return; }
    w.key_uint("multi_fh_mode", p[0]);
    w.key_str("state", p[0] ? "enable" : "disable");
}

// 101/106 — Set Narrow Band FH command (4 bytes).
static void decode_cmd_set_narrow_band_fh(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "set_narrow_band_fh cmd < 4 bytes"); return; }
    w.key_uint("narrow_band_fh_mode", p[0]);
    w.key_str("state", p[0] ? "enable" : "disable");
}

// =============================================================================
// Group 101 dispatchers
// =============================================================================

bool g101_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  25: decode_cmd_set_threshold(p, n, w);                    return true;
        case  27: decode_cmd_set_resolution(p, n, w);                   return true;
        case  31: decode_cmd_start_follow_on_jam(p, n, w);              return true;
        case  33: return true;                                           // Stop Follow-on Jam — 0 bytes
        case  37: decode_cmd_configure_detection(p, n, w);              return true;
        case  39: decode_cmd_start_fh_detection(p, n, w);               return true;
        case  43: decode_cmd_get_wideband_fft(p, n, w);                 return true;
        case  47: decode_cmd_set_pulse_range(p, n, w);                  return true;
        case  55: decode_cmd_set_min_hops(p, n, w);                     return true;
        case  63: decode_cmd_tracking_config(p, n, w);                  return true;
        case  69: decode_cmd_start_ff_detection(p, n, w);               return true;
        case  73: decode_cmd_start_list_jam(p, n, w);                   return true;
        case  75: return true;                                           // Stop List Jam — 0 bytes
        case  79: decode_cmd_send_ecm_reports(p, n, w);                 return true;
        case  83: decode_cmd_start_burst_detection(p, n, w);            return true;
        case  85: decode_cmd_start_scan_speed(p, n, w);                 return true;
        case  87: return true;                                           // Stop Scan Speed — 0 bytes
        case  92: decode_cmd_start_responsive_sweep_jam(p, n, w);       return true;
        case  94: decode_cmd_get_zoom_fft(p, n, w);                     return true;
        case 100: decode_cmd_set_flatness_mode(p, n, w);                return true;
        case 102: decode_cmd_set_integration_time(p, n, w);             return true;
        case 104: decode_cmd_set_multi_band_fh(p, n, w);                return true;
        case 106: decode_cmd_set_narrow_band_fh(p, n, w);               return true;
        case 140: return true;                                           // HF-specific — 0 bytes
        case 158: decode_cmd_terminate_fft(p, n, w);                    return true;
        case 160: case 162: case 164:
        case 174: case 176: case 178: return true;                       // HF slow scan — 0 bytes
        case 182: case 184: case 186: return true;                       // HF fast scan — 0 bytes
        case 200: case 202: return true;                                 // HF wideband — 0 bytes
        case 204: return true;                                           // HF data query — 0 bytes
        case 210: return true;                                           // cross-group cmd — 0 bytes
        default: return false;
    }
}

bool g101_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  26: return true;                                           // Set Threshold ACK
        case  28: return true;                                           // Set Resolution ACK
        case  32: return true;                                           // Start Follow-on Jam ACK
        case  34: return true;                                           // Stop Follow-on Jam ACK
        case  38: return true;                                           // Configure Detection ACK
        case  40: decode_fh_detection(p, n, w);                         return true;
        case  44: decode_wideband_fft(p, n, w);                         return true;
        case  48: return true;                                           // Set Pulse Range ACK
        case  56: return true;                                           // Set Min Hops ACK
        case  64: return true;                                           // Auto/Manual Tracking ACK
        case  70: decode_ff_detection(p, n, w);                         return true;
        case  74: return true;                                           // Start List Jam ACK
        case  76: return true;                                           // Stop List Jam ACK
        case  80: return true;                                           // Send ECM Reports ACK
        case  84: decode_burst_detection(p, n, w);                      return true;
        case  86: return true;                                           // Start Scan Speed ACK
        case  88: decode_stop_scan_speed(p, n, w);                      return true;
        case  93: return true;                                           // Start Responsive Sweep Jam ACK
        case  95: decode_zoom_fft(p, n, w);                             return true;
        case 101: return true;                                           // Set Flatness Mode ACK
        case 103: return true;                                           // Set Integration Time ACK
        case 105: return true;                                           // Set Multi-Band FH ACK
        case 107: return true;                                           // Set Narrow-Band FH ACK
        case 141: return true;                                           // HF-specific ACK
        case 159: return true;                                           // Terminate FFT ACK
        case 161: case 163: case 165:
        case 175: case 177: case 179: return true;                       // HF slow scan ACKs
        case 183: case 185: case 187: return true;                       // HF fast scan ACKs
        case 201: case 203: return true;                                 // HF wideband ACKs
        default: return false;
    }
}
