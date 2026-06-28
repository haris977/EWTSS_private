// drs-bridge/parsers/dp_ecm/src/hf/parser/g111_parser.cpp
// Group 111 — Signal/Scan: parse-side decode functions and dispatchers.
#include "sdfc_endian.h"
#include "json_writer.h"
#include "hf_groups.h"
#include "hf_json_utils.h"
using namespace sdfc;

// =============================================================================
// Group 111 command decoders — HF-specific where noted
// =============================================================================

// 111/3 — Signal BITE Test command (16 bytes). HF-specific (VU = 111/21, 4 bytes).
// @0  BandSelection (uint8) @1 Reserved (uint8) @2 BiteMode (uint16)
// @4  BiteFreqMHz (float, 4B) → Hz  @8 PowerLeveldBm (float) @12 Reserved (uint32)
static void decode_cmd_signal_bite(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) { w.key_str("warning", "signal_bite cmd < 16 bytes"); return; }
    w.key_uint("band_selection",    p[0]);
    w.key_uint("bite_mode",         load_u16le(p + 2));
    w.key_double("bite_freq_hz",    static_cast<double>(load_f32le(p + 4)) * 1e6);
    w.key_double("power_level_dbm", static_cast<double>(load_f32le(p + 8)));
}

// 111/21 — Signal BITE Test (band select, 4 bytes, per ICD Table §3.2.1.3.1).
// @0 band_selection (uint8): selects frequency band for 1–30 MHz BITE; @1-3 reserved.
static void decode_cmd_signal_bite_band(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "signal_bite_band cmd < 4 bytes"); return; }
    w.key_uint("band_selection", p[0]);
}

// 111/5 — Reference Input Selection (4 bytes). Same as VU.
static void decode_cmd_reference_input(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "reference_input cmd < 4 bytes"); return; }
    uint16_t sel = load_u16le(p + 0);
    w.key_uint("reference_selection", sel);
    w.key_str("reference_name", sel == 0 ? "internal" : sel == 1 ? "external" : "unknown");
}

// 111/9 — Send Protected Scan List (variable). Same as VU.
// count (uint16) + reserved (uint16) + S_PROTECTED_BAND_LIST × count (8B each).
static void decode_cmd_send_protected_scan_list(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "send_protected_scan_list cmd < 4 bytes"); return; }
    uint16_t count = load_u16le(p + 0);
    w.key_uint("protected_band_count", count);
    const int ELEM = 8;
    std::string arr = "[";
    int off = 4;   //we are considering footer here such that we can't mistakenly count it here
    uint16_t emitted = 0;
    for (uint16_t i = 0; i < count; ++i) {
        if (off + ELEM > n) break;
        const uint8_t* e = p + off;
        JsonWriter b;
        b.key_double("start_freq_hz", static_cast<double>(load_f32le(e + 0)) * 1e6);
        b.key_double("stop_freq_hz",  static_cast<double>(load_f32le(e + 4)) * 1e6);
        if (emitted++) arr += ',';
        arr += b.str();
        off += ELEM;
    }
    arr += "]";
    w.key_raw("protected_bands", arr);
}

// 111/13 — Protected Scan Enable/Disable (4 bytes). Same as VU.
static void decode_cmd_protected_scan_enable(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "protected_scan_enable cmd < 4 bytes"); return; }
    w.key_bool("protected_scan_enabled", load_u16le(p + 0) == 1);
}

// 111/17 — FH Splitband Enable/Disable (4 bytes). Same as VU.
static void decode_cmd_fh_splitband_enable(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "fh_splitband_enable cmd < 4 bytes"); return; }
    w.key_bool("fh_splitband_enabled", p[0] == 1);
}

// 111/19 — Send FH Splitband Frequency (4 bytes). Same as VU.
static void decode_cmd_send_fh_splitband_freq(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "fh_splitband_freq cmd < 4 bytes"); return; }
    w.key_double("splitband_freq_hz", static_cast<double>(load_f32le(p + 0)) * 1e6);
}

// =============================================================================
// Group 111 response decoders — HF-specific where noted
// =============================================================================

// 111/4 — Signal BITE Test response (12 bytes).
// @0 BiteFreqHz (float, 4B, MHz stored) @4 BitePowerdBm (float) @8 BiteResult (uint16) @10 Reserved (uint16)
static void decode_signal_bite_resp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 12) { w.key_str("warning", "signal_bite_resp payload < 12 bytes"); return; }
    w.key_double("bite_freq_hz",   static_cast<double>(load_f32le(p + 0)) * 1e6);
    w.key_double("bite_power_dbm", static_cast<double>(load_f32le(p + 4)));
    w.key_uint("bite_result",      load_u16le(p + 8));
}

// 111/22 — Observed BITE Frequency response (16 bytes, per ICD).
// @0  Observed BITE Frequency (MHz) (double, 8B)
// @8  Observed BITE Power level (dBm) (float, 4B)
// @12 Observed BITE Result: 1=Pass, 0=Fail (uint16, 2B)
// @14 Reserved (uint16, 2B)
static void decode_bite_observed_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) { w.key_str("warning", "bite_observed_rsp payload < 16 bytes"); return; }
    w.key_double("observed_bite_freq_mhz", load_f64le(p +  0));
    w.key_double("observed_bite_power_dbm", static_cast<double>(load_f32le(p + 8)));
    w.key_uint("bite_result",              load_u16le(p + 12));
    // p[14-15] reserved
}

// 111/16 — PDW Channelization Data response (per ICD, page 64).
// Header: Channelization Data Count (uint32) + S_FH_CHANNELIZATION_TOA (4B: H,M,S,rsv)
// S_FH_CHANNELIZATION layout (28 bytes per entry, max 16384 entries):
//   @0  TOI (double,8B)       @8  Frequency Index (float→Hz)  @12 Pulse Length (float)
//   @16 Power Level (dBm) (float)  @20 Bandwidth (uint32)     @24 Frequency Band (uint32)
static void decode_pdw_channelization(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "pdw_channelization payload < 8 bytes"); return; }
    uint32_t count = load_u32le(p + 0);
    w.key_uint("channelization_count", count);
    char toa[16];
    std::snprintf(toa, sizeof(toa), "%02u:%02u:%02u", p[4], p[5], p[6]);
    w.key_str("toa", toa);

    const int      ELEM      = 28;
    const uint32_t MAX_SLOTS = 16384;
    uint32_t valid = count < MAX_SLOTS ? count : MAX_SLOTS;

    std::string arr = "[";
    uint32_t emitted = 0;
    for (uint32_t i = 0; i < valid; ++i) {
        int off = 8 + static_cast<int>(i) * ELEM;
        if (off + ELEM > n) break;
        const uint8_t* e = p + off;
        JsonWriter c;
        c.key_double("toi",             load_f64le(e +  0));
        c.key_double("freq_index_hz",   static_cast<double>(load_f32le(e +  8)) * 1e6);
        c.key_double("pulse_length_ms", static_cast<double>(load_f32le(e + 12)));
        c.key_double("power_level_dbm", static_cast<double>(load_f32le(e + 16)));
        c.key_uint("bandwidth",          load_u32le(e + 20));
        c.key_uint("freq_band",          load_u32le(e + 24));
        if (emitted++) arr += ',';
        arr += c.str();
    }
    arr += "]";
    w.key_raw("channelization_data", arr);
}

// 111/26 — Get Storage Details response (20 bytes). Same as VU.
static void decode_storage_details(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 20) { w.key_str("warning", "storage_details payload < 20 bytes"); return; }
    w.key_uint("disk_space_1",            p[0]);
    w.key_uint("disk_space_2",            p[1]);
    w.key_uint("disk_space_3",            p[2]);
    w.key_double("available_disk_space",  load_f64le(p +  4));
    w.key_double("total_disk_space",      load_f64le(p + 12));
}

// 111/8 — Module HEALTH Status response (4 bytes).
// @0 DRX_Health (uint8, 1=PASS) @1 RF_Tuner_Health (uint8) @2-3 reserved
static void decode_module_health(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "module_health payload < 4 bytes"); return; }
    w.key_uint("drx_health",      p[0]);
    w.key_str("drx_health_name",  p[0] == 1 ? "pass" : "fail");
    w.key_uint("rf_tuner_health", p[1]);
    w.key_str("rf_tuner_health_name", p[1] == 1 ? "pass" : "fail");
}

// 111/23 — Send Spectrum Protected-Band Enable/Disable command (4 bytes).
static void decode_cmd_spectrum_protected_band(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "spectrum_protected_band cmd < 4 bytes"); return; }
    w.key_uint("protected_spectrum_enabled", load_u16le(p + 0));
    w.key_str("state", load_u16le(p + 0) == 1 ? "enable" : "disable");
}

// 111/28 — Read Protected Band List response (variable).
// count (uint16) + reserved (uint16) + S_PROTECTED_BAND_LIST × count (8B each).
static void decode_read_protected_band_list(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "read_protected_band_list payload < 4 bytes"); return; }
    uint16_t count = load_u16le(p + 0);
    w.key_uint("protected_band_count", count);
    const int ELEM = 8;
    std::string arr = "[";
    int off = 4;
    uint16_t emitted = 0;
    for (uint16_t i = 0; i < count; ++i) {
        if (off + ELEM > n) break;
        const uint8_t* e = p + off;
        JsonWriter b;
        b.key_double("start_freq_hz", static_cast<double>(load_f32le(e + 0)) * 1e6);
        b.key_double("stop_freq_hz",  static_cast<double>(load_f32le(e + 4)) * 1e6);
        if (emitted++) arr += ',';
        arr += b.str();
        off += ELEM;
    }
    arr += "]";
    w.key_raw("protected_bands", arr);
}

// 111/29 — Auto Threshold Enable/Disable command (4 bytes, per ICD §3.2.1.3).
// @0 auto_threshold_enable (uint8): 1=enable, 0=disable; @1-3 reserved.
static void decode_cmd_auto_threshold_enable(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "auto_threshold_enable cmd < 4 bytes"); return; }
    w.key_bool("auto_threshold_enabled", p[0] != 0);
}

// 111/31 — Hopper Channelization Enable/Disable command (16 bytes).
// @0 HopperStartFreq (float, MHz) @4 HopperStopFreq (float, MHz)
// @8 HopperHopPeriod (float)  @12 Enable (uint8)  @13-15 reserved
static void decode_cmd_hopper_channelization(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) { w.key_str("warning", "hopper_channelization cmd < 16 bytes"); return; }
    w.key_double("hopper_start_freq_hz",  static_cast<double>(load_f32le(p +  0)) * 1e6);
    w.key_double("hopper_stop_freq_hz",   static_cast<double>(load_f32le(p +  4)) * 1e6);
    w.key_double("hop_period_ms",         static_cast<double>(load_f32le(p +  8)));
    w.key_bool("channelization_enabled",  p[12] != 0);
}

bool g111_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  3: decode_cmd_signal_bite(p, n, w);               return true;
        case  5: decode_cmd_reference_input(p, n, w);           return true;
        case  7: return true;                                    // Module HEALTH Status — 0 bytes
        case  9: decode_cmd_send_protected_scan_list(p, n, w);  return true;
        case 13: decode_cmd_protected_scan_enable(p, n, w);     return true;
        case 15: return true;                                    // PDW Channelization — 0 bytes
        case 17: decode_cmd_fh_splitband_enable(p, n, w);       return true;
        case 19: decode_cmd_send_fh_splitband_freq(p, n, w);    return true;
        case 21: decode_cmd_signal_bite_band(p, n, w);          return true;
        case 23: decode_cmd_spectrum_protected_band(p, n, w);   return true;
        case 25: return true;                                    // Get Storage Details — 0 bytes
        case 27: return true;                                    // Read Protected Band List — 0 bytes
        case 29: decode_cmd_auto_threshold_enable(p, n, w);     return true;
        case 31: decode_cmd_hopper_channelization(p, n, w);     return true;
        default: return false;
    }
}

bool g111_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  4: decode_signal_bite_resp(p, n, w);       return true;
        case  6: return true;                            // Reference Input ACK
        case  8: decode_module_health(p, n, w);         return true;
        case 10: return true;                            // Send Protected Scan List ACK
        case 14: return true;                            // Protected Scan Enable ACK
        case 16: decode_pdw_channelization(p, n, w);    return true;
        case 18: return true;                            // FH Splitband Enable ACK
        case 20: return true;                            // FH Splitband Freq ACK
        case 22: decode_bite_observed_rsp(p, n, w);     return true;
        case 24: return true;                            // Spectrum Protected Band ACK
        case 26: decode_storage_details(p, n, w);       return true;
        case 28: decode_read_protected_band_list(p, n, w); return true;
        case 30: return true;                            // Auto Threshold Enable ACK
        case 32: return true;                            // Hopper Channelization ACK
        default: return false;
    }
}
