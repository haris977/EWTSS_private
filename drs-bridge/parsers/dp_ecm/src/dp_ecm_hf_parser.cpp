// drs-bridge/parsers/dp_ecm/src/dp_ecm_hf_parser.cpp
//
// DP-ECM-1071 HF Receiver/Processor/Exciter parser DLL.
// Protocol doc: DP-ECM-1074-6000-V1-ICD-0V04.
// Implements the 4-symbol ABI (sdfc_abi.h) on top of the shared frame core.
//
// Full scope:
//   Group 100 (SJC diagnostics):
//     100/2  — Get System Version     (28 bytes, HF: 4 floats + processor_id + tuner_id[3] + fpga_type_id)
//     100/4  — SRx Checksum           (1000 bytes)
//     100/6  — PBIT Status            (88 bytes, HF-specific layout)
//     100/8  — IBIT Status            (68 bytes, HF-specific layout)
//     100/10 — Temperature Status     (36 bytes, 9 floats)
//     100/14 — Fan Speed Status       (4 bytes)
//     100/17 — UART Port Select cmd   (4 bytes)
//     100/18 — UART Test Status       (4 bytes)
//     100/26 — CBIT Status            (8 bytes)
//   Group 109 (Date/Time): 109/11 cmd, 109/12 rsp
//   Group 111 (Signal/Scan):
//     cmds 3(16B),5,9,13,15,17,19,25; rsps 4(12B),6,10,14,16(40B/entry),18,20,26,211
//   Group 112 (Fast Scan/Simulation): cmds 1,5,13,37; rsps 2,6,14,38
//   Group 101 (SJC detection + jamming):
//     detection cmds: 25,27,37,39,43,47,55,69,83,85,87,94,100,102,104,106,140,158,160-186,200,202,204,210
//     detection rsps: 26,28,38,40,44,48,56,70,84,86,88,95,101,103,105,107,141,159,161-187,201,203,205
//     jamming cmds:   31,33,63,73,75,79,92
//     jamming rsps:   32,34,64,74,76,80,93
//   Group 106 (HF ECM jamming — immediate jam):
//     cmds 1,3,5,9,21,39,41,45,49,55; rsps 2,4,6,10,22,40,42,46,50,54,56
//   All other units fall through to raw_hex envelope (never crashes).
#include "sdfc_abi.h"
#include "sdfc_frame.h"
#include "sdfc_endian.h"
#include "json_writer.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <cstdio>

#include "hf/hf_groups.h"
#include "hf/hf_json_utils.h"
#include "hf/hf_shared.h"

using namespace sdfc;

static constexpr const char* HW_NAME = "dp_ecm_hf";

// =============================================================================
// Group 109 command decoder
// =============================================================================

// 109/11 — Set Date and Time (8 bytes). Same as VU.
static void decode_cmd_set_date_time(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "set_date_time cmd < 8 bytes"); return; }
    w.key_uint("day",     p[0]);
    w.key_uint("month",   p[1]);
    w.key_uint("year",    load_u16le(p + 2));
    w.key_uint("hour",    p[4]);
    w.key_uint("minute",  p[5]);
    w.key_uint("seconds", p[6]);
}

// 109/18 — Hopper Channelization Data response (per ICD Table 183).
// Message size: 4 + 4 + (28 × up to 16384) entries.
// Header: Channelization Data Count (uint32) + S_HOPPER_CHANNELIZATION_TOA (4B: H,M,S,rsv)
// S_HOPPER_CHANNELIZATION layout (28 bytes per entry):
//   @0  TOI (double,8B)  @8  Frequency Index (float→Hz)  @12 Pulse Length (float)
//   @16 Power Level (dBm) (float)  @20 Bandwidth (uint32)  @24 Frequency Band (uint32)
static void decode_hopper_channelization(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "hopper_channelization payload < 8 bytes"); return; }
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
    w.key_raw("hopper_channelizations", arr);
}

// 109/16 — Auto Threshold Value response (6404 bytes, per ICD Table 180/181).
// @0 Auto Threshold Bin Count (uint32, 4B)
// @4 Auto Threshold Bin Data  (float[1600], 6400B)
static void decode_auto_threshold_value(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 6404) { w.key_str("warning", "auto_threshold_value payload < 6404 bytes"); return; }
    uint32_t bin_count = load_u32le(p + 0);
    w.key_uint("auto_threshold_bin_count", bin_count);
    uint32_t emit = bin_count < 1600 ? bin_count : 1600;
    std::string arr = "[";
    for (uint32_t i = 0; i < emit; ++i) {
        if (i) arr += ',';
        char tmp[32];
        std::snprintf(tmp, sizeof(tmp), "%.4f",
            static_cast<double>(load_f32le(p + 4 + i * 4)));
        arr += tmp;
    }
    arr += "]";
    w.key_raw("auto_threshold_bin_data", arr);
}

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

// =============================================================================
// Group 200 HF jamming command/response decoders
// (VU jamming lives on Groups 101/106/108; HF consolidates all jamming on Group 200.)
// (Group 106 decode functions moved to src/hf/parser/g106_parser.cpp)
// =============================================================================

// 200/14, 200/15 — ECM Report / List Jam Report (variable streaming).
// count (uint32) + S_LIST_JAM_REPORT_REPLY × count (8B each). Same as VU 108/6.
static void decode_list_jam_report(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "list_jam_report payload < 4 bytes"); return; }
    uint32_t count = load_u32le(p + 0);
    w.key_uint("list_jam_freq_count", count);
    static const char* STATUS_NAMES[] = {
        "inactive", "active", "jammed", "within_protected_band"
    };
    const int ELEM = 8;
    std::string arr = "[";
    int off = 4;
    uint32_t emitted = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (off + ELEM > n) break;
        const uint8_t* e = p + off;
        JsonWriter f;
        f.key_uint("freq_hz", load_u32le(e + 0));
        uint16_t status = load_u16le(e + 4);
        f.key_uint("status", status);
        f.key_str("status_name", status < 4 ? STATUS_NAMES[status] : "unknown");
        if (emitted++) arr += ',';
        arr += f.str();
        off += ELEM;
    }
    arr += "]";
    w.key_raw("frequencies", arr);
}

// =============================================================================
// Group 111 new receiver command/response decoders
// =============================================================================

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

// =============================================================================
// Group 3 multi-channel status response decoder
// =============================================================================

// 3/18 — Read Channel Information response (16 bytes): 8× uint16 channel status.
static void decode_mrx_all_channels_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) { w.key_str("warning", "mrx_channels_rsp < 16 bytes"); return; }
    std::string arr = "[";
    for (int i = 0; i < 8; ++i) {
        if (i) arr += ',';
        char tmp[64];
        uint16_t status = load_u16le(p + i * 2);
        std::snprintf(tmp, sizeof(tmp),
            "{\"channel\":%d,\"status\":%u,\"state\":\"%s\"}",
            i + 1, status, status == 1 ? "open" : "closed");
        arr += tmp;
    }
    arr += "]";
    w.key_raw("channels", arr);
}

// 3/22 — MRX Individual Channel Init Status response (16 bytes, per ICD Table 219).
// 8 channels × uint16: 1 = initialization success, 0 = initialization failure.
static void decode_mrx_channel_init_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) { w.key_str("warning", "mrx_channel_init_status < 16 bytes"); return; }
    std::string arr = "[";
    for (int i = 0; i < 8; ++i) {
        if (i) arr += ',';
        char tmp[64];
        uint16_t status = load_u16le(p + i * 2);
        std::snprintf(tmp, sizeof(tmp),
            "{\"channel\":%d,\"init_status\":%u,\"state\":\"%s\"}",
            i + 1, status, status == 1 ? "success" : "failure");
        arr += tmp;
    }
    arr += "]";
    w.key_raw("channel_init_statuses", arr);
}

// =============================================================================
// Group 4 optical IQ command/response decoders
// =============================================================================

// 4/65 — Start IQ Data Streaming to GO2Monitor Optical command (4 bytes).
// @0 MRX_channel (uint16)  @2 Bandwidth (uint16)
static void decode_mrx_optical_iq_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) return;
    static const char* BW[] = {"10MHz","5MHz","2.5MHz","1MHz","240kHz","120kHz","60kHz",
                                "30kHz","15kHz","6kHz","3kHz","1.5kHz"};
    uint16_t bw = load_u16le(p + 2);
    w.key_uint("mrx_channel",  load_u16le(p + 0));
    w.key_uint("bw_selection", bw);
    w.key_str("bandwidth_name", bw < 12 ? BW[bw] : "unknown");
}

// 4/70 — Read Optical Port Availability Status response (12 bytes).
static void decode_mrx_optical_port_status_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 12) { w.key_str("warning", "optical_port_status < 12 bytes"); return; }
    w.key_uint("port_number",          load_u16le(p + 0));
    w.key_uint("port_id",              load_u16le(p + 2));
    w.key_uint("port_alive_status",    load_u16le(p + 4));
    w.key_str("port_alive",            load_u16le(p + 4) == 1 ? "available" : "unavailable");
    w.key_uint("already_transmitting", load_u16le(p + 6));
    w.key_uint("can_start_transfer",   load_u16le(p + 8));
}

// 4/72 — Read Optical Interface IP Address response (24 bytes).
// @0 IP_Address (4× uint8)  @4 Port_IDs (9× uint16)  @22 Reserved (2B)
static void decode_mrx_optical_ip_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 24) { w.key_str("warning", "optical_ip_rsp < 24 bytes"); return; }
    char ip[32];
    std::snprintf(ip, sizeof(ip), "%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
    w.key_str("ip_address", ip);
    std::string ports = "[";
    for (int i = 0; i < 9; ++i) {
        if (i) ports += ',';
        char tmp[8];
        std::snprintf(tmp, sizeof(tmp), "%u", load_u16le(p + 4 + i * 2));
        ports += tmp;
    }
    ports += "]";
    w.key_raw("port_ids", ports);
}

// =============================================================================
// Group 200 — new command/response decoders
// =============================================================================

// 200/41 — HPASU and PA Health Status command (4 bytes).
// @0 Selection (uint8): 0=HPASU, 1=PA-1, 2=PA-2, 3=PA-3, 4=PA-4. @1-3 Reserved.
static void decode_hpasu_health_status_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "hpasu_health_cmd < 4 bytes"); return; }
    static const char* SEL[] = {"hpasu", "pa_1", "pa_2", "pa_3", "pa_4"};
    uint8_t sel = p[0];
    w.key_uint("selection", sel);
    w.key_str("selection_name", sel < 5 ? SEL[sel] : "unknown");
}

// 200/42 — HPASU and PA Health Status response (4 bytes).
static void decode_hpasu_health_status_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "hpasu_health_rsp < 4 bytes"); return; }
    w.key_uint("health_status", p[0]);
    w.key_str("health_status_name", p[0] == 0 ? "healthy" : "fault");
}

// 200/55 — PA and SDU Health Status response (8 bytes).
// PA fault: 0=no_fault, 1=overdrive, 3=vswr, 4=bpm. SDU fault: 0=no_fault, 1=fault.
static void decode_pa_sdu_health_status_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "pa_sdu_health_rsp < 8 bytes"); return; }
    static const char* PA_FAULT[] = {"no_fault", "overdrive", "unknown", "vswr", "bpm"};
    for (int i = 0; i < 4; ++i) {
        char key[16];
        std::snprintf(key, sizeof(key), "pa%d_health", i + 1);
        w.key_uint(key, p[i]);
        char key2[32];
        std::snprintf(key2, sizeof(key2), "pa%d_health_name", i + 1);
        w.key_str(key2, p[i] < 5 ? PA_FAULT[p[i]] : "unknown");
    }
    w.key_uint("sdu_health", p[4]);
    w.key_str("sdu_health_name", p[4] == 0 ? "no_fault" : "fault");
}

// 200/56 — PA Soft Reboot command (4 bytes).
// @0 PA Selection (uint8): 0=PA-1, 1=PA-2, 2=PA-3, 3=PA-4. @1-3 Reserved.
static void decode_cmd_pa_soft_reboot(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "pa_soft_reboot_cmd < 4 bytes"); return; }
    w.key_uint("pa_selection", p[0]);
    char name[8];
    std::snprintf(name, sizeof(name), "pa_%u", p[0] + 1);
    w.key_str("pa_name", name);
}

// =============================================================================
// MRx Group 1 — diagnostics (shared with VU variant; same device, same messages)
// =============================================================================

// 1/2 — Get System Version Details response (16 bytes).
// @0 FW Version (float)  @4 Driver Version (float)  @8 FPGA Version (float)
// @12 RF Tuner ID (uint16)  @14 Reserved (uint16)
static void decode_mrx_system_version(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) { w.key_str("warning", "mrx_system_version < 16 bytes"); return; }
    w.key_double("fw_version",     static_cast<double>(load_f32le(p + 0)));
    w.key_double("driver_version", static_cast<double>(load_f32le(p + 4)));
    w.key_double("fpga_version",   static_cast<double>(load_f32le(p + 8)));
    w.key_uint("rf_tuner_id",      load_u16le(p + 12));
}

// 1/4 — Get Checksum Details response (1024 bytes, per ICD Table 201). Payload is a char array.
static void decode_mrx_checksum(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 1024) { w.key_str("warning", "mrx_checksum payload < 1024 bytes"); return; }
    const char* cs = reinterpret_cast<const char*>(p);
    w.key_str("sjc_fw_checksum", std::string(cs, strnlen(cs, 1024)));
}

// 1/6 — PBIT Status response (120 bytes, per ICD Table 203).
static void decode_mrx_pbit_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 120) { w.key_str("warning", "mrx_pbit_status < 120 bytes"); return; }
    w.key_uint("fpga_scratch_pad_test",    p[ 0]);
    w.key_uint("fpga_board_id_read",       p[ 1]);
    w.key_uint("processor_temp_status",    p[ 2]);
    w.key_uint("fan_temp_status",          p[ 3]);
    w.key_uint("fpga_temp_status",         p[ 4]);
    // p[5] reserved
    w.key_uint("rfpsu_temp_status",        p[ 6]);
    w.key_uint("fan_speed_ctrl_sensor",    p[ 7]);
    w.key_uint("fan_voltage_status",       p[ 8]);
    w.key_uint("rfsu_5v_status",           p[ 9]);
    w.key_uint("rfsu_8v5_status",          p[10]);
    w.key_uint("msata_detection_status",   p[11]);
    w.key_uint("lo1_pll_lock_status",      p[12]);
    w.key_uint("lo2_pll_lock_status",      p[13]);
    w.key_uint("bite_pll_lock_status",     p[14]);
    w.key_uint("tuner_detection_status",   p[15]);
    // p[16-17] reserved
    w.key_uint("tuner_scratchpad_test",    p[18]);
    // p[19-20] reserved
    w.key_uint("pll_lock_status",          p[21]);
    w.key_uint("adc_bonding_status",       p[22]);
    w.key_uint("storage_availability",     p[23]);
    w.key_uint("digital_5v_status",        p[24]);
    w.key_uint("digital_3v5_status",       p[25]);
    w.key_uint("digital_psu_temp_status",  p[26]);
    // p[27-123] reserved blocks
}

// 1/8 — IBIT Status response (112 bytes).
static void decode_mrx_ibit_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 112) { w.key_str("warning", "mrx_ibit_status < 112 bytes"); return; }
    w.key_uint("pll_lock_status",          p[ 0]);
    w.key_uint("adc_bonding_status",       p[ 1]);
    w.key_uint("msata_detection_status",   p[ 2]);
    w.key_uint("storage_availability",     p[ 3]);
    w.key_uint("tuner_lo1_pll_lock",       p[ 4]);
    w.key_uint("tuner_lo2_pll_lock",       p[ 5]);
    w.key_uint("tuner_bite_pll_lock",      p[ 6]);
    w.key_uint("adc1_link_status",         p[ 7]);
    // p[8-9] reserved
    w.key_uint("tuner_detection_status",   p[10]);
    // p[11-12] reserved
    w.key_uint("tuner_scratchpad_test",    p[13]);
    // p[14-111] reserved blocks
}

// 1/10 — Temperature Status response (36 bytes, 9 floats, per ICD Table 212/213).
static void decode_mrx_temperature(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 36) { w.key_str("warning", "mrx_temperature < 36 bytes"); return; }
    static const char* NAMES[] = {
        "bpt_temp_c", "psu_8156_temp_c", "tuner_temp_c", "psu_7255_temp_c",
        "processor_temp_c", "power_supply_temp_c", "control_board_temp_c",
        "rf_psu_temp_c", "fpga_temp_c"
    };
    for (int i = 0; i < 9; ++i)
        w.key_double(NAMES[i], static_cast<double>(load_f32le(p + i * 4)));
}

// 1/14 — Fan Speed Status response (4 bytes). Same layout as ECM 100/14.
static void decode_mrx_fan_speed(const uint8_t* p, int n, JsonWriter& w) {
    if (n >= 4) w.key_uint("fan_speed_rpm", load_u32le(p));
}

// 1/17 — UART Test command (4 bytes).
static void decode_mrx_uart_test_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) return;
    w.key_uint("uart_number", p[0]);
    w.key_str("uart_type", p[0] == 0 ? "rs232" : p[0] == 1 ? "rs422" : "unknown");
}

// 1/18 — UART Test response (4 bytes). Same layout as ECM 100/18.
static void decode_mrx_uart_test_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) return;
    w.key_uint("expected_data", p[0]);
    w.key_uint("observed_data", p[1]);
    w.key_uint("result",        p[2]);
    w.key_str("result_name", p[2] == 1 ? "pass" : "fail");
}

// 1/26 — CBIT Status response (8 bytes).
static void decode_mrx_cbit_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "mrx_cbit_status < 8 bytes"); return; }
    w.key_uint("drx_status",            p[0]);
    w.key_uint("voltage_status",        p[1]);
    w.key_uint("temperature_status",    p[2]);
    // p[3] reserved
    w.key_uint("tuner_detection_status",p[4]);
    // p[5-6] reserved
    w.key_uint("memory_status",         p[7]);
}

// =============================================================================
// MRx Group 3 — RF board / channel management
// =============================================================================

// 3/2 — Read Board Count response (4 bytes).
static void decode_mrx_board_count_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) return;
    uint16_t count = load_u16le(p + 0);
    w.key_uint("board_count", count);
    w.key_str("note", count == 1 ? "mrx_channels_accessible" : "mrx_channels_not_accessible");
    w.key_uint("available_tuner_id", load_u16le(p + 2));
}

// 3/18 and 3/22 — Channel status response (16 bytes).
// channel1_status + 7× reserved u16. Status 1=open/initialised, 0=closed/failed.
static void decode_mrx_channel_16b_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) return;
    w.key_uint("channel1_status", load_u16le(p + 0));
    w.key_str("channel1_name", load_u16le(p + 0) == 1 ? "open_or_success" : "closed_or_fail");
}

// 3/19 — Write Channel Information command (4 bytes).
static void decode_mrx_write_channel_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) return;
    w.key_uint("mrx_channel", load_u16le(p + 0));
    w.key_uint("channel_status", load_u16le(p + 2));
    w.key_str("channel_action", load_u16le(p + 2) == 1 ? "open" : "close");
}

// 3/24 — Read MRx and SRx Tuning Details response (8 bytes).
// @0 SRx Tuned (u8)  @1 MRx Tuned (u8)  @2 SRx Scan Mode (u16)
// @4 Tuned Center Freq (u16)  @6 Memory Scan Tuned (u8)  @7 BITE Selection (u8)
static void decode_mrx_tuning_details_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "mrx_tuning_details < 8 bytes"); return; }
    static const char* SRX_TUNED[] = {"not_tuned", "search_mode", "jam_mode"};
    w.key_uint("srx_tuned_status",        p[0]);
    w.key_str("srx_tuned_status_name",    p[0] < 3 ? SRX_TUNED[p[0]] : "unknown");
    w.key_uint("mrx_tuned_status",        p[1]);
    w.key_uint("srx_scan_mode_status",    load_u16le(p + 2));
    w.key_uint("tuned_center_freq_mhz",   load_u16le(p + 4));
    w.key_uint("memory_scan_tuned",       p[6]);
    w.key_uint("bite_selection",          p[7]);
    w.key_str("bite_selection_name",      p[7] == 0 ? "antenna" : "bite");
}

// =============================================================================
// MRx Group 5 — Tuner
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
// MRx Group 4 — Data acquisition
// =============================================================================

// 4/5 — Set Threshold command (8 bytes).
static void decode_mrx_set_threshold_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) return;
    w.key_uint("mrx_channel",   load_u16le(p + 0));
    w.key_double("threshold_dbm", static_cast<double>(load_f32le(p + 4)));
}

// 4/8 — Audio Data Acquisition response (per ICD Table 225).
// Message size: 4 + (audio_data_size × 2). Max 262144 samples. 8000Hz, 16-bit unsigned samples.
static void decode_mrx_audio_data_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "audio_data_rsp payload < 4 bytes"); return; }
    uint32_t audio_size = load_u32le(p + 0);
    w.key_uint("audio_data_size", audio_size);

    const uint32_t MAX_SAMPLES = 262144;
    uint32_t valid = audio_size < MAX_SAMPLES ? audio_size : MAX_SAMPLES;

    std::string arr = "[";
    for (uint32_t i = 0; i < valid; ++i) {
        int off = 4 + static_cast<int>(i) * 2;
        if (off + 2 > n) break;
        if (i) arr += ',';
        arr += std::to_string(load_u16le(p + off));
    }
    arr += "]";
    w.key_raw("audio_data", arr);
}

// 4/17 — Demodulation and Bandwidth Selection command (8 bytes).
static void decode_mrx_demod_bw_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) return;
    static const char* DEMOD[] = {"cw", "am", "fm", "lsb", "usb"};
    static const char* BW[]    = {"240kHz","120kHz","60kHz","30kHz","15kHz","6kHz","3kHz","1.5kHz"};
    uint16_t demod  = load_u16le(p + 2);
    uint16_t bw     = load_u16le(p + 4);
    uint16_t offset = load_u16le(p + 6);
    w.key_uint("mrx_channel",     load_u16le(p + 0));
    w.key_uint("demod_selection", demod);
    w.key_str("demod_name",       demod < 5 ? DEMOD[demod] : "unknown");
    w.key_uint("bw_selection",    bw);
    w.key_str("bandwidth_name",   bw < 8 ? BW[bw] : "unknown");
    w.key_uint("lsb_usb_offset",  offset);
}

// 4/39 and 4/41 — Configure/Read Memory Scan command (variable).
// Header: mrx_channel(2) + reserved(2) + freq_count(4).
// Per entry (12B): freq_val(double,8) + bw_val(uint16,2) + reserved(uint16,2).
static void decode_mrx_memory_scan_config_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) return;
    w.key_uint("mrx_channel",   load_u16le(p + 0));
    uint32_t count = load_u32le(p + 4);
    w.key_uint("freq_count", count);
    const int ELEM  = 12;
    const uint32_t MAX_ENTRIES = 10000;
    uint32_t valid = count < MAX_ENTRIES ? count : MAX_ENTRIES;
    int off = 8;
    uint32_t emit = 0;
    std::string arr = "[";
    for (uint32_t i = 0; i < valid; ++i) {
        if (off + ELEM > n) break;
        const uint8_t* e = p + off;
        JsonWriter f;
        f.key_double("freq_hz",  load_f64le(e + 0) * 1e6);
        f.key_uint("bw_sel",     load_u16le(e + 8));
        if (emit++) arr += ',';
        arr += f.str();
        off += ELEM;
    }
    arr += "]";
    w.key_raw("frequencies", arr);
    if (count > MAX_ENTRIES) {
        char msg[64];
        std::snprintf(msg, sizeof(msg), "truncated at %u of %u entries", MAX_ENTRIES, count);
        w.key_str("truncated", msg);
    }
}

// 4/42 — Read Memory Scan Data response (per ICD Table 239/240).
// Header: Total Available Count(4) + Scan Speed(2) + Reserved(2).
// S_RES_MEMORY_SCAN_LIST layout (20 bytes per entry, max 10000):
//   @0  Power Level (dBm) (float)  @4  Frequency Value (double)
//   @12 H:M:S:reserved (4B)        @16 Millisecond (uint16)  @18 Bandwidth List (uint16)
static void decode_mrx_memory_scan_data_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "memory_scan_data_rsp payload < 8 bytes"); return; }
    uint32_t count      = load_u32le(p + 0);
    uint16_t scan_speed = load_u16le(p + 4);
    w.key_uint("total_available_count",       count);
    w.key_uint("scan_speed_channels_per_sec", scan_speed);

    const int      ELEM      = 20;
    const uint32_t MAX_SLOTS = 10000;
    uint32_t valid = count < MAX_SLOTS ? count : MAX_SLOTS;

    std::string arr = "[";
    uint32_t emit = 0;
    for (uint32_t i = 0; i < valid; ++i) {
        int off = 8 + static_cast<int>(i) * ELEM;
        if (off + ELEM > n) break;
        const uint8_t* e = p + off;
        JsonWriter f;
        f.key_double("power_dbm", static_cast<double>(load_f32le(e +  0)));
        f.key_double("freq_hz",   load_f64le(e +  4) * 1e6);
        char toa[24];
        std::snprintf(toa, sizeof(toa), "%02u:%02u:%02u.%03u",
                      e[12], e[13], e[14], load_u16le(e + 16));
        f.key_str("toa", toa);
        f.key_uint("bandwidth_list", load_u16le(e + 18));
        if (emit++) arr += ',';
        arr += f.str();
    }
    arr += "]";
    w.key_raw("scan_data", arr);
}

// 4/61 — Read Smart Memory Scan Data command (12 bytes).
static void decode_mrx_smart_scan_read_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 12) return;
    w.key_double("freq_hz",   load_f64le(p + 0) * 1e6);
    w.key_uint("mrx_channel", load_u16le(p + 8));
}

// 4/62 — Read Smart Memory Scan Data response (16 bytes).
static void decode_mrx_smart_scan_read_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) return;
    w.key_double("freq_hz",    load_f64le(p +  0) * 1e6);
    w.key_uint("mrx_channel",  load_u16le(p +  8));
    w.key_double("amplitude",  static_cast<double>(load_f32le(p + 12)));
}

// 4/44 — DDC FFT Data response (fixed: 2+2+4096×4 = 16388 bytes).
static void decode_mrx_ddc_fft_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) return;
    uint16_t bin_count = load_u16le(p + 0);
    w.key_uint("bin_count", bin_count);
    uint32_t emit = bin_count < 4096 ? bin_count : 4096;
    const int HDR = 4;
    if (n < HDR + static_cast<int>(emit) * 4) emit = static_cast<uint32_t>((n - HDR) / 4);
    std::string arr = "[";
    for (uint32_t i = 0; i < emit; ++i) {
        if (i) arr += ',';
        char tmp[32];
        std::snprintf(tmp, sizeof(tmp), "%.2f",
            static_cast<double>(load_f32le(p + HDR + i * 4)));
        arr += tmp;
    }
    arr += "]";
    w.key_raw("ddc_fft_power_dbm", arr);
}

// Shared IQ status response (4 bytes): channel + nb_iq_status + wb_iq_status + reserved.
// Used for 4/34, 4/24, 6/14.
static void decode_mrx_iq_start_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) return;
    w.key_uint("mrx_channel",          p[0]);
    w.key_uint("narrowband_iq_status", p[1]);
    w.key_uint("wideband_iq_status",   p[2]);
    w.key_str("iq_type",               p[2] ? "wideband_10MHz_to_1MHz" : "narrowband_240kHz_and_below");
    //p[3] reserved
}

// 4/23 — Start IQ Data Logging command (20 bytes).
static void decode_mrx_iq_logging_start_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 20) return;
    static const char* BW[] = {
        "10MHz","5MHz","2.5MHz","1MHz","240kHz","120kHz","60kHz",
        "30kHz","15kHz","6kHz","3kHz","1.5kHz"
    };
    uint16_t bw = load_u16le(p + 2);
    w.key_uint("mrx_channel",      load_u16le(p + 0));
    w.key_uint("bw_selection",     bw);
    w.key_str("bandwidth_name",    bw < 12 ? BW[bw] : "unknown");
    w.key_double("center_freq_hz", static_cast<double>(load_f32le(p + 4)) * 1e6);
    w.key_uint("day",    load_u16le(p +  8));
    w.key_uint("month",  load_u16le(p + 10));
    w.key_uint("year",   load_u16le(p + 12));
    w.key_uint("hour",   load_u16le(p + 14));
    w.key_uint("minute", load_u16le(p + 16));
    w.key_uint("second", load_u16le(p + 18));
}

// 4/26 — Stop IQ Data Logging response (132 bytes).
// @0 channel(u16) @2 reserved(u16) @4 file_path(char[128])
static void decode_mrx_iq_logging_stop_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 132) return;
    w.key_uint("mrx_channel", load_u16le(p + 0));
    char path[129];
    std::memcpy(path, p + 4, 128);
    path[128] = '\0';
    w.key_str("file_path", path);
}

// 4/53 — Engage Channel command (12 bytes).
static void decode_mrx_engage_channel_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 12) return;
    w.key_uint("mrx_channel",       load_u16le(p + 0));
    w.key_double("center_freq_hz",  load_f64le(p + 4) * 1e6);
}

// =============================================================================
// MRx Group 6 — FH monitoring / GO2Monitor
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

// 6/9 — GO2Monitor Connection Establishment command (260 bytes).
// @0 IP Address (char[128])  @128 Port Number (char[128])  @256 mrx_channel (u16)
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
// MRx Group 7 — Signal BITE / miscellaneous
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

// Shared helper: decode 4-byte "selection + mrx_channel" command.
static void decode_mrx_sel_channel_cmd(const char* sel_key, const char* on_name, const char* off_name,
                                        const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) return;
    uint16_t sel = load_u16le(p + 0);
    w.key_uint(sel_key, sel);
    if (on_name && off_name) w.key_str("state", sel ? on_name : off_name);
    w.key_uint("mrx_channel", load_u16le(p + 2));
}

// Shared helper: decode DDC FFT command (4 bytes): channel + bw_selection.
static void decode_mrx_ddc_fft_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) return;
    static const char* BW[] = {"240kHz","120kHz","60kHz","30kHz","15kHz","6kHz","3kHz","1.5kHz"};
    uint16_t bw = load_u16le(p + 2);
    w.key_uint("mrx_channel",  load_u16le(p + 0));
    w.key_uint("bw_selection", bw);
    w.key_str("bandwidth_name", bw < 8 ? BW[bw] : "unknown");
}

// Shared helper: decode IQ streaming start command (4 bytes): channel + bw_selection.
static void decode_mrx_iq_start_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) return;
    static const char* BW[] = {
        "1MHz","240kHz","120kHz","60kHz","30kHz","15kHz","6kHz","3kHz","1.5kHz"
    };
    uint16_t bw = load_u16le(p + 2);
    w.key_uint("mrx_channel",  load_u16le(p + 0));
    w.key_uint("bw_selection", bw);
    w.key_str("bandwidth_name", bw < 9 ? BW[bw] : "unknown");
}

// =============================================================================
// ABI: extract_frame
// =============================================================================
extern "C" SDFC_EXPORT int extract_frame(const uint8_t* buf, size_t buf_len,
                                         uint8_t** out_frame, size_t* out_len) {
    if (!buf || !out_frame || !out_len) return -1;
    uint8_t* tmp = static_cast<uint8_t*>(std::malloc(MAX_FRAME_BUFFER_BYTES));
    if (!tmp) return -1;
    int tmp_len = 0;
    int result = extract_frame_core(buf, static_cast<int>(buf_len), tmp, &tmp_len);
    if (result <= 0) { std::free(tmp); return -1; }
    uint8_t* frame = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(tmp_len)));
    if (!frame) { std::free(tmp); return -1; }
    std::memcpy(frame, tmp, static_cast<size_t>(tmp_len));
    std::free(tmp);
    *out_frame = frame;
    *out_len = static_cast<size_t>(tmp_len);
    return 0;
}

// =============================================================================
// ABI: parse_message
// =============================================================================
extern "C" SDFC_EXPORT int parse_message(const uint8_t* frame, size_t frame_len,
                                         char** out_json, size_t* out_len) {
    if (!frame || !out_json || !out_len) return -1;
    int frame_type = FRAME_INCOMPLETE;
    if (frame_len >= static_cast<size_t>(MAGIC_LEN)) {
        if (std::memcmp(frame, CMD_HEADER,  MAGIC_LEN) == 0) frame_type = FRAME_COMMAND;
        else if (std::memcmp(frame, RESP_HEADER, MAGIC_LEN) == 0) frame_type = FRAME_RESPONSE;
    }
    if (frame_type == FRAME_INCOMPLETE) return -1;
    FrameHeader hdr;
    if (!decode_header(frame, static_cast<int>(frame_len), frame_type, hdr)) return -1;
    if (hdr.total_len > static_cast<int>(frame_len)) return -1;

    const uint8_t* payload = frame + hdr.payload_off;
    int plen = static_cast<int>(hdr.payload_size);
    if (hdr.payload_off + plen > static_cast<int>(frame_len)) return -1;

    JsonWriter w;
    w.key_str("hw", HW_NAME);
    if (frame_type == FRAME_COMMAND) w.key_str("frame_type", "command");
    w.key_uint("group_id", hdr.group_id);
    w.key_uint("unit_id",  hdr.unit_id);
    w.key_uint("message_size_bytes", hdr.payload_size);
    if (frame_type == FRAME_RESPONSE) {
        w.key_int("status", hdr.status);
        w.key_str("status_name", hdr.status == 0 ? "OnSuccess" : "Error");
    }

    bool decoded = false;

    // ------------------------------------------------------------------
    // FRAME_COMMAND dispatch
    // ------------------------------------------------------------------
    if (frame_type == FRAME_COMMAND) {
        if (hdr.group_id == 100) decoded = g100_parse_cmd(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id == 101) decoded = g101_parse_cmd(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id == 109) {
            switch (hdr.unit_id) {
                case 11: decode_cmd_set_date_time(payload, plen, w); decoded = true; break;
                case 15: /* Send Auto Threshold Value — 0 bytes */  decoded = true; break;
                case 17: /* Acquire Hopper Channelization — 0 bytes */ decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 111) {
            switch (hdr.unit_id) {
                case  3: decode_cmd_signal_bite(payload, plen, w);               decoded = true; break;
                case  5: decode_cmd_reference_input(payload, plen, w);           decoded = true; break;
                case  9: decode_cmd_send_protected_scan_list(payload, plen, w);  decoded = true; break;
                case 13: decode_cmd_protected_scan_enable(payload, plen, w);     decoded = true; break;
                case 15: /* PDW Channelization cmd — 0 bytes */                  decoded = true; break;
                case 17: decode_cmd_fh_splitband_enable(payload, plen, w);       decoded = true; break;
                case 19: decode_cmd_send_fh_splitband_freq(payload, plen, w);    decoded = true; break;
                case 21: decode_cmd_signal_bite_band(payload, plen, w);          decoded = true; break;
                case  7: /* Module HEALTH Status — 0 bytes */                   decoded = true; break;
                case 23: decode_cmd_spectrum_protected_band(payload, plen, w);  decoded = true; break;
                case 25: /* Get Storage Details — 0 bytes */                     decoded = true; break;
                case 27: /* Read Protected Band List — 0 bytes */               decoded = true; break;
                case 29: decode_cmd_auto_threshold_enable(payload, plen, w);    decoded = true; break;
                case 31: decode_cmd_hopper_channelization(payload, plen, w);   decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 112) {
            switch (hdr.unit_id) {
                case  1: decode_cmd_asu_sdu_config(payload, plen, w);            decoded = true; break;
                case  3: /* TRSDU Receiver Line Status — 0 bytes */           decoded = true; break;
                case  5: /* PA Receiver Line Status — 0 bytes */              decoded = true; break;
                case 13: decode_cmd_simulation_mode_config(payload, plen, w);    decoded = true; break;
                case 37: /* HF-specific fast scan cmd — 0 bytes */               decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 200) {
            // NOTE: unit IDs confirmed from ICD §5.1.10-5.1.13.
            // Immediate Jam, Ext Modulation, Prog Exciter unit IDs not yet assigned
            // in this ICD version — raw_hex fallback covers them.
            switch (hdr.unit_id) {
                case  9: decode_cmd_send_ecm_reports(payload, plen, w);             decoded = true; break;
                case 11: decode_cmd_start_list_jam(payload, plen, w);               decoded = true; break;
                case 13: /* Stop List Jam — 0 bytes */                               decoded = true; break;
                case 15: /* Get List Jam Report — 0 bytes, periodic cmd */           decoded = true; break;
                case 17: decode_cmd_start_follow_on_jam(payload, plen, w);          decoded = true; break;
                case 19: /* Stop Follow-on Jam — 0 bytes */                          decoded = true; break;
                case 21: decode_cmd_start_responsive_sweep_jam(payload, plen, w);   decoded = true; break;
                case 23: /* Stop Responsive Sweep Jam — 0 bytes (unit assumed ±1) */ decoded = true; break;
                case 41: decode_hpasu_health_status_cmd(payload, plen, w);          decoded = true; break;
                case 54: /* PA/SDU Health Status — 0 bytes */                        decoded = true; break;
                case 56: decode_cmd_pa_soft_reboot(payload, plen, w);               decoded = true; break;
                default: break;
            }
        // ECM Group 106 — Immediate Jamming commands
        } else if (hdr.group_id == 106) {
            decoded = g106_parse_cmd(hdr.unit_id, payload, plen, w);
        // MRx Group 1 — diagnostics
        } else if (hdr.group_id == 1) {
            switch (hdr.unit_id) {
                case  1: /* Get System Version — 0 bytes */    decoded = true; break;
                case  3: /* Get Checksum — 0 bytes */          decoded = true; break;
                case  5: /* PBIT — 0 bytes */                  decoded = true; break;
                case  7: /* IBIT — 0 bytes */                  decoded = true; break;
                case  9: /* Temperature — 0 bytes */           decoded = true; break;
                case 13: /* Fan Speed — 0 bytes */             decoded = true; break;
                case 17: decode_mrx_uart_test_cmd(payload, plen, w); decoded = true; break;
                case 25: /* CBIT — 0 bytes */                  decoded = true; break;
                case 33: /* Close All Channels — 0 bytes */    decoded = true; break;
                default: break;
            }
        // MRx Group 3 — RF board / channel management
        } else if (hdr.group_id == 3) {
            switch (hdr.unit_id) {
                case  1: /* Read Board Count — 0 bytes */      decoded = true; break;
                case 17: /* Read Channel Info — 0 bytes */     decoded = true; break;
                case 19: decode_mrx_write_channel_cmd(payload, plen, w); decoded = true; break;
                case 21: /* VUSHF Channel Status — 0 bytes */  decoded = true; break;
                case 23: /* Read Tuning Details — 0 bytes */   decoded = true; break;
                case 25: /* Get CBIT Status — 0 bytes */       decoded = true; break;
                default: break;
            }
        // MRx Group 4 — data acquisition
        } else if (hdr.group_id == 4) {
            switch (hdr.unit_id) {
                case  5: decode_mrx_set_threshold_cmd(payload, plen, w);            decoded = true; break;
                case  7: decode_mrx_channel_cmd(payload, plen, w);                  decoded = true; break;
                case  9: decode_mrx_channel_cmd(payload, plen, w);                  decoded = true; break;
                case 11: decode_mrx_channel_cmd(payload, plen, w);                  decoded = true; break;
                case 15: decode_mrx_channel_cmd(payload, plen, w);                  decoded = true; break;
                case 17: decode_mrx_demod_bw_cmd(payload, plen, w);                 decoded = true; break;
                case 23: decode_mrx_iq_logging_start_cmd(payload, plen, w);         decoded = true; break;
                case 25: decode_mrx_channel_cmd(payload, plen, w);                  decoded = true; break;
                case 33: decode_mrx_iq_start_cmd(payload, plen, w);                 decoded = true; break;
                case 35: decode_mrx_channel_cmd(payload, plen, w);                  decoded = true; break;
                case 39: decode_mrx_memory_scan_config_cmd(payload, plen, w);       decoded = true; break;
                case 41: decode_mrx_memory_scan_config_cmd(payload, plen, w);       decoded = true; break;
                case 43: decode_mrx_ddc_fft_cmd(payload, plen, w);                  decoded = true; break;
                case 53: decode_mrx_engage_channel_cmd(payload, plen, w);           decoded = true; break;
                case 55: decode_mrx_channel_cmd(payload, plen, w);                  decoded = true; break;
                case 57: decode_mrx_channel_cmd(payload, plen, w);                  decoded = true; break;
                case 59: decode_mrx_channel_cmd(payload, plen, w);                  decoded = true; break;
                case 61: decode_mrx_smart_scan_read_cmd(payload, plen, w);          decoded = true; break;
                case 63: decode_mrx_channel_cmd(payload, plen, w);                  decoded = true; break;
                case 65: decode_mrx_optical_iq_cmd(payload, plen, w);          decoded = true; break;
                case 67: decode_mrx_channel_cmd(payload, plen, w);              decoded = true; break; // Stop optical IQ
                case 69: decode_mrx_optical_iq_cmd(payload, plen, w);          decoded = true; break; // Read port status cmd
                case 71: decode_mrx_optical_iq_cmd(payload, plen, w);          decoded = true; break; // Read IP cmd
                default: break;
            }
        // MRx Group 5 — tuner
        } else if (hdr.group_id == 5) {
            switch (hdr.unit_id) {
                case  1: decode_mrx_set_center_freq_cmd(payload, plen, w);          decoded = true; break;
                case  3: decode_mrx_attenuation_cmd(payload, plen, w);              decoded = true; break;
                case  9: decode_mrx_channel_cmd(payload, plen, w);                  decoded = true; break; // Clear Center Freq
                case 13: /* Read AGC/MGC — 0 bytes */                               decoded = true; break;
                default: break;
            }
        // MRx Group 6 — FH monitoring / GO2Monitor
        } else if (hdr.group_id == 6) {
            switch (hdr.unit_id) {
                case  7: decode_mrx_fh_monitoring_cmd(payload, plen, w);            decoded = true; break;
                case  9: decode_mrx_go2monitor_connect_cmd(payload, plen, w);       decoded = true; break;
                case 11: /* GO2Monitor Disconnect — 0 bytes */                      decoded = true; break;
                case 13: decode_mrx_start_go2monitor_cmd(payload, plen, w);         decoded = true; break;
                case 15: /* Stop GO2Monitor — 0 bytes */                            decoded = true; break;
                default: break;
            }
        // MRx Group 7 — Signal BITE / misc
        } else if (hdr.group_id == 7) {
            switch (hdr.unit_id) {
                case  1: decode_mrx_signal_bite_cmd(payload, plen, w);              decoded = true; break;
                case  3: decode_mrx_sel_channel_cmd("bite_antenna_sel","bite","antenna",payload,plen,w); decoded=true; break;
                case  5: decode_mrx_sel_channel_cmd("ref_source_sel","external","internal",payload,plen,w); decoded=true; break;
                case  9: decode_mrx_sel_channel_cmd("afc_sel","on","off",payload,plen,w); decoded=true; break;
                case 11: decode_mrx_sel_channel_cmd("rf_squelch_sel","on","off",payload,plen,w); decoded=true; break;
                case 13: decode_mrx_channel_cmd(payload, plen, w);                  decoded = true; break; // IQ socket open
                case 15: decode_mrx_channel_cmd(payload, plen, w);                  decoded = true; break; // IQ socket close
                case 17: decode_mrx_spectrum_avg_cmd(payload, plen, w);            decoded = true; break;
                case 19: decode_mrx_sel_channel_cmd("rf_agc_sel","enable","disable",payload,plen,w); decoded=true; break;
                case 21: decode_mrx_audio_squelch_cmd(payload, plen, w);            decoded = true; break;
                case 23: decode_mrx_date_time_cmd(payload, plen, w);                decoded = true; break;
                default: break;
            }
        }
    // ------------------------------------------------------------------
    // FRAME_RESPONSE dispatch
    // ------------------------------------------------------------------
    } 
    else if (frame_type == FRAME_RESPONSE) {
        if (hdr.group_id == 100) decoded = g100_parse_rsp(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id == 101) decoded = g101_parse_rsp(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id == 109) {
            switch (hdr.unit_id) {
                case 12: /* Set Date Time ACK */                                decoded = true; break;
                case 16: decode_auto_threshold_value(payload, plen, w);        decoded = true; break;
                case 18: decode_hopper_channelization(payload, plen, w);       decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 111) {
            switch (hdr.unit_id) {
                case   4: decode_signal_bite_resp(payload, plen, w);    decoded = true; break;
                case   6: /* Reference Input ACK */                      decoded = true; break;
                case  10: /* Send Protected Scan List ACK */             decoded = true; break;
                case  14: /* Protected Scan Enable ACK */                decoded = true; break;
                case  16: decode_pdw_channelization(payload, plen, w);  decoded = true; break;
                case  18: /* FH Splitband Enable ACK */                  decoded = true; break;
                case  20: /* FH Splitband Freq ACK */                    decoded = true; break;
                case  22: decode_bite_observed_rsp(payload, plen, w);   decoded = true; break;
                case  26: decode_storage_details(payload, plen, w);     decoded = true; break;
                case 211: /* cross-group rsp to 101/210 — no struct decoder, falls through to raw_hex */ break;
                case   8: decode_module_health(payload, plen, w);            decoded = true; break;
                case  24: /* Spectrum Protected Band ACK */                   decoded = true; break;
                case  28: decode_read_protected_band_list(payload, plen, w); decoded = true; break;
                case  30: /* Auto Threshold Enable ACK */                     decoded = true; break;
                case  32: /* Hopper Channelization ACK */                     decoded = true; break;
                default:  break;
            }
        } else if (hdr.group_id == 112) {
            switch (hdr.unit_id) {
                case  2: decode_asu_sdu_config_rsp(payload, plen, w);    decoded = true; break;
                case  4: decode_trsdu_receiver_status(payload, plen, w); decoded = true; break;
                case  6: decode_pa_receiver_status(payload, plen, w);    decoded = true; break;
                case 14: /* Simulation Mode Config ACK */              decoded = true; break;
                case 38: /* HF fast scan ACK */                        decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 200) {
            switch (hdr.unit_id) {
                case 10: /* Send ECM Reports ACK */                         decoded = true; break;
                case 12: /* Start List Jam ACK */                           decoded = true; break;
                case 14: /* Stop List Jam ACK */                            decoded = true; break;
                case 16: decode_list_jam_report(payload, plen, w);         decoded = true; break;
                case 18: /* Start Follow-on Jam ACK */                      decoded = true; break;
                case 20: /* Stop Follow-on Jam ACK */                       decoded = true; break;
                case 22: /* Start Responsive Sweep Jam ACK */               decoded = true; break;
                case 24: /* Stop Responsive Sweep Jam ACK (unit assumed) */ decoded = true; break;
                case 42: decode_hpasu_health_status_rsp(payload, plen, w); decoded = true; break;
                case 55: decode_pa_sdu_health_status_rsp(payload, plen, w);decoded = true; break;
                case 57: /* PA Soft Reboot ACK */                           decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 106) {
            decoded = g106_parse_rsp(hdr.unit_id, payload, plen, w);
        // MRx Group 1 responses
        } else if (hdr.group_id == 1) {
            switch (hdr.unit_id) {
                case  2: decode_mrx_system_version(payload, plen, w); decoded = true; break;
                case  4: decode_mrx_checksum(payload, plen, w);        decoded = true; break;
                case  6: decode_mrx_pbit_status(payload, plen, w);     decoded = true; break;
                case  8: decode_mrx_ibit_status(payload, plen, w);     decoded = true; break;
                case 10: decode_mrx_temperature(payload, plen, w);     decoded = true; break;
                case 14: decode_mrx_fan_speed(payload, plen, w);       decoded = true; break;
                case 18: decode_mrx_uart_test_rsp(payload, plen, w);   decoded = true; break;
                case 26: decode_mrx_cbit_status(payload, plen, w);     decoded = true; break;
                case 34: /* Close All Channels ACK */                   decoded = true; break;
                default: break;
            }
        // MRx Group 3 responses
        } else if (hdr.group_id == 3) {
            switch (hdr.unit_id) {
                case  2: decode_mrx_board_count_rsp(payload, plen, w);      decoded = true; break;
                case 18: decode_mrx_all_channels_rsp(payload, plen, w);       decoded = true; break;
                case 20: /* Write Channel ACK */                               decoded = true; break;
                case 22: decode_mrx_channel_init_status(payload, plen, w);    decoded = true; break;
                case 24: decode_mrx_tuning_details_rsp(payload, plen, w);   decoded = true; break;
                case 26: decode_mrx_cbit_status(payload, plen, w);         decoded = true; break;
                default: break;
            }
        // MRx Group 4 responses
        } else if (hdr.group_id == 4) {
            switch (hdr.unit_id) {
                case  6: /* Set Threshold ACK */                             decoded = true; break;
                case  8: decode_mrx_audio_data_rsp(payload, plen, w);       decoded = true; break;
                case 10: /* Audio Start Play ACK */                          decoded = true; break;
                case 12: /* Audio Stop Play ACK */                           decoded = true; break;
                case 16: /* Audio FIFO Reset ACK */                          decoded = true; break;
                case 18: /* Demod/BW Select ACK */                           decoded = true; break;
                case 24: decode_mrx_iq_start_rsp(payload, plen, w);         decoded = true; break;
                case 26: decode_mrx_iq_logging_stop_rsp(payload, plen, w);  decoded = true; break;
                case 34: decode_mrx_iq_start_rsp(payload, plen, w);         decoded = true; break;
                case 36: /* Stop IQ Streaming ACK */                         decoded = true; break;
                case 40: /* Configure Memory Scan ACK */                     decoded = true; break;
                case 42: decode_mrx_memory_scan_data_rsp(payload, plen, w); decoded = true; break;
                case 44: decode_mrx_ddc_fft_rsp(payload, plen, w);          decoded = true; break;
                case 54: /* Engage Channel ACK */                            decoded = true; break;
                case 56: /* Disengage Channel ACK */                         decoded = true; break;
                case 58: /* Stop Memory Scan ACK */                          decoded = true; break;
                case 60: /* Smart Memory Scan Config ACK */                  decoded = true; break;
                case 62: decode_mrx_smart_scan_read_rsp(payload, plen, w);  decoded = true; break;
                case 64: /* Stop Smart Memory Scan ACK */                    decoded = true; break;
                case 66: /* Start Optical IQ ACK */                          decoded = true; break;
                case 68: /* Stop Optical IQ ACK */                           decoded = true; break;
                case 70: decode_mrx_optical_port_status_rsp(payload, plen, w); decoded = true; break;
                case 72: decode_mrx_optical_ip_rsp(payload, plen, w);       decoded = true; break;
                default: break;
            }
        // MRx Group 5 responses
        } else if (hdr.group_id == 5) {
            switch (hdr.unit_id) {
                case  2: /* Set Center Freq ACK */                           decoded = true; break;
                case  4: /* Attenuation Select ACK */                        decoded = true; break;
                case 10: /* Clear Center Freq ACK */                         decoded = true; break;
                case 14: decode_mrx_agc_status_rsp(payload, plen, w);       decoded = true; break;
                default: break;
            }
        // MRx Group 6 responses
        } else if (hdr.group_id == 6) {
            switch (hdr.unit_id) {
                case  8: /* FH Monitoring Config ACK */                      decoded = true; break;
                case 10: /* GO2Monitor Connect ACK */                        decoded = true; break;
                case 12: /* GO2Monitor Disconnect ACK */                     decoded = true; break;
                case 14: /* Start GO2Monitor Transmission ACK */              decoded = true; break;
                case 16: /* Stop GO2Monitor ACK */                           decoded = true; break;
                default: break;
            }
        // MRx Group 7 responses
        } else if (hdr.group_id == 7) {
            switch (hdr.unit_id) {
                case  2: decode_mrx_signal_bite_rsp(payload, plen, w); decoded = true; break;
                case  4: /* BITE/Antenna Select ACK */                  decoded = true; break;
                case  6: /* Ref Source Select ACK */                    decoded = true; break;
                case 10: /* AFC ACK */                                  decoded = true; break;
                case 12: /* RF Squelch ACK */                           decoded = true; break;
                case 14: /* IQ Socket Open ACK */                       decoded = true; break;
                case 16: /* IQ Socket Close ACK */                      decoded = true; break;
                case 18: /* Spectrum Avg Count ACK */                   decoded = true; break;
                case 20: /* Smart RF AGC ACK */                         decoded = true; break;
                case 22: /* Audio Squelch ACK */                        decoded = true; break;
                case 24: /* Set Date/Time ACK */                        decoded = true; break;
                default: break;
            }
        }
    }

    // Safe fallback: expose raw bytes so nothing is lost and the bridge never crashes.
    if (!decoded && plen > 0) {
        w.key_str("raw_hex", to_hex(payload, plen));
    }

    std::string json = w.str();
    char* result = static_cast<char*>(std::malloc(json.size() + 1));
    if (!result) return -1;
    std::memcpy(result, json.c_str(), json.size() + 1);
    *out_json = result;
    *out_len = json.size();
    return 0;
}

// =============================================================================
// ABI: format_response
// =============================================================================
// ---------------------------------------------------------------------------
// Group 109 encoders
// ---------------------------------------------------------------------------
static int encode_auto_threshold_value(const char* j, uint8_t* buf, int max_len) {
    const int TOTAL = 4 + 1600 * 4;
    if (max_len < TOTAL) return -1;
    std::memset(buf, 0, (size_t)TOTAL);
    long long bc = 0; json_find_int(j, "auto_threshold_bin_count", bc);
    if (bc > 1600) bc = 1600;
    store_u32le(buf, (uint32_t)bc);
    if (bc == 0) return TOTAL;
    const char* pd = std::strstr(j, "\"auto_threshold_bin_data\"");
    if (!pd) return TOTAL;
    const char* arr = std::strchr(pd, '['); if (!arr) return TOTAL; ++arr;
    int written = 0;
    while (written < (int)bc) {
        while (*arr == ' ' || *arr == '\t' || *arr == '\n' || *arr == ',') ++arr;
        if (*arr == ']' || !*arr) break;
        char* end; float v = (float)std::strtod(arr, &end);
        if (end == arr) break;
        store_f32le(buf + 4 + written * 4, v); arr = end; ++written;
    }
    return TOTAL;
}

// Shared encoder: hopper_channelization (109/18) and pdw_channelization (111/16).
// Wire: count(u32)@0 + toa[H,M,S,0]@4 + count×28B entries.
static int encode_channelization(const char* j, const char* arr_key, uint8_t* buf, int max_len) {
    long long count = 0; json_find_int(j, "channelization_count", count);
    if (count < 0) count = 0;
    int total = 8 + (int)count * 28;
    if (total > max_len) return -1;
    std::memset(buf, 0, (size_t)total);
    store_u32le(buf, (uint32_t)count);
    uint8_t th = 0, tm = 0, ts = 0;
    parse_toa_hms(j, "toa", th, tm, ts);
    buf[4] = th; buf[5] = tm; buf[6] = ts;
    if (count == 0) return 8;
    const char* ks = std::strstr(j, arr_key);
    if (!ks) return 8;
    const char* arr = std::strchr(ks, '['); if (!arr) return 8;
    const char* p = arr + 1; int written = 0;
    while (written < (int)count) {
        while (*p && *p != '{' && *p != ']') ++p;
        if (!*p || *p == ']') break;
        const char* os = p; int depth = 1; ++p;
        while (*p && depth > 0) { if (*p == '{') ++depth; else if (*p == '}') --depth; ++p; }
        std::string entry(os, p); const char* e = entry.c_str();
        double toi = 0, fihz = 0, pl = 0, pwr = 0;
        long long bw = 0, fb = 0;
        json_find_double(e, "toi",             toi);
        json_find_double(e, "freq_index_hz",   fihz);
        json_find_double(e, "pulse_length_ms", pl);
        json_find_double(e, "power_level_dbm", pwr);
        json_find_int(e,    "bandwidth",       bw);
        json_find_int(e,    "freq_band",       fb);
        uint8_t* slot = buf + 8 + written * 28;
        store_f64le(slot +  0, toi);
        store_f32le(slot +  8, (float)(fihz / 1e6));
        store_f32le(slot + 12, (float)pl);
        store_f32le(slot + 16, (float)pwr);
        store_u32le(slot + 20, (uint32_t)bw);
        store_u32le(slot + 24, (uint32_t)fb);
        ++written;
    }
    return 8 + written * 28;
}

static int encode_hopper_channelization(const char* j, uint8_t* buf, int max_len) {
    return encode_channelization(j, "\"hopper_channelizations\"", buf, max_len);
}

// ---------------------------------------------------------------------------
// Group 111 encoders
// ---------------------------------------------------------------------------
static int encode_signal_bite_resp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 12) return -1;
    std::memset(buf, 0, 12);
    double freq = 0, power = 0; long long result = 0;
    json_find_double(j, "bite_freq_hz",   freq);
    json_find_double(j, "bite_power_dbm", power);
    json_find_int(j,    "bite_result",    result);
    store_f32le(buf + 0, (float)(freq / 1e6));
    store_f32le(buf + 4, (float)power);
    store_u16le(buf + 8, (uint16_t)result);
    return 12;
}

static int encode_module_health(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    long long drx = 0, tuner = 0;
    json_find_int(j, "drx_health",      drx);
    json_find_int(j, "rf_tuner_health", tuner);
    buf[0] = (uint8_t)drx; buf[1] = (uint8_t)tuner;
    return 4;
}

static int encode_pdw_channelization(const char* j, uint8_t* buf, int max_len) {
    return encode_channelization(j, "\"channelization_data\"", buf, max_len);
}

static int encode_bite_observed_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 16) return -1;
    std::memset(buf, 0, 16);
    double freq = 0, power = 0; long long result = 0;
    json_find_double(j, "observed_bite_freq_mhz",  freq);
    json_find_double(j, "observed_bite_power_dbm", power);
    json_find_int(j,    "bite_result",             result);
    store_f64le(buf + 0, freq);
    store_f32le(buf + 8, (float)power);
    store_u16le(buf + 12, (uint16_t)result);
    return 16;
}

static int encode_storage_details(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 20) return -1;
    std::memset(buf, 0, 20);
    long long ds1 = 0, ds2 = 0, ds3 = 0; double avail = 0, total = 0;
    json_find_int(j,    "disk_space_1",         ds1);
    json_find_int(j,    "disk_space_2",         ds2);
    json_find_int(j,    "disk_space_3",         ds3);
    json_find_double(j, "available_disk_space", avail);
    json_find_double(j, "total_disk_space",     total);
    buf[0] = (uint8_t)ds1; buf[1] = (uint8_t)ds2; buf[2] = (uint8_t)ds3;
    store_f64le(buf +  4, avail); store_f64le(buf + 12, total);
    return 20;
}

static int encode_read_protected_band_list(const char* j, uint8_t* buf, int max_len) {
    long long count = 0; json_find_int(j, "protected_band_count", count);
    if (count < 0) count = 0;
    int total = 4 + (int)count * 8;
    if (total > max_len) return -1;
    std::memset(buf, 0, (size_t)total);
    store_u16le(buf, (uint16_t)count);
    if (count == 0) return 4;
    const char* pd = std::strstr(j, "\"protected_bands\"");
    if (!pd) return 4;
    const char* arr = std::strchr(pd, '['); if (!arr) return 4;
    const char* p = arr + 1; int written = 0;
    while (written < (int)count) {
        while (*p && *p != '{' && *p != ']') ++p;
        if (!*p || *p == ']') break;
        const char* os = p; int depth = 1; ++p;
        while (*p && depth > 0) { if (*p == '{') ++depth; else if (*p == '}') --depth; ++p; }
        std::string entry(os, p); const char* e = entry.c_str();
        double start = 0, stop = 0;
        json_find_double(e, "start_freq_hz", start); json_find_double(e, "stop_freq_hz", stop);
        uint8_t* slot = buf + 4 + written * 8;
        store_f32le(slot + 0, (float)(start / 1e6)); store_f32le(slot + 4, (float)(stop / 1e6));
        ++written;
    }
    return 4 + written * 8;
}

// ---------------------------------------------------------------------------
// Group 112 encoders
// ---------------------------------------------------------------------------
static int encode_asu_sdu_config_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    long long err = 0; json_find_int(j, "error_value", err);
    store_i16le(buf, (int16_t)err); return 4;
}

static int encode_trsdu_receiver_status(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    long long v = 0; json_find_int(j, "tr_sdu_health_status", v); buf[0] = (uint8_t)v; return 4;
}

static int encode_pa_receiver_status(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    long long v = 0; json_find_int(j, "power_amplifier_status", v); buf[0] = (uint8_t)v; return 4;
}

// ---------------------------------------------------------------------------
// Group 200 encoders
// ---------------------------------------------------------------------------
static int encode_list_jam_report(const char* j, uint8_t* buf, int max_len) {
    long long count = 0; json_find_int(j, "list_jam_freq_count", count);
    if (count < 0) count = 0;
    int total = 4 + (int)count * 8;
    if (total > max_len) return -1;
    std::memset(buf, 0, (size_t)total);
    store_u32le(buf, (uint32_t)count);
    if (count == 0) return 4;
    const char* pd = std::strstr(j, "\"frequencies\"");
    if (!pd) return 4;
    const char* arr = std::strchr(pd, '['); if (!arr) return 4;
    const char* p = arr + 1; int written = 0;
    while (written < (int)count) {
        while (*p && *p != '{' && *p != ']') ++p;
        if (!*p || *p == ']') break;
        const char* os = p; int depth = 1; ++p;
        while (*p && depth > 0) { if (*p == '{') ++depth; else if (*p == '}') --depth; ++p; }
        std::string entry(os, p); const char* e = entry.c_str();
        long long freq = 0, status = 0;
        json_find_int(e, "freq_hz", freq); json_find_int(e, "status", status);
        uint8_t* slot = buf + 4 + written * 8;
        store_u32le(slot + 0, (uint32_t)freq); store_u16le(slot + 4, (uint16_t)status);
        ++written;
    }
    return 4 + written * 8;
}

static int encode_hpasu_health_status_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    long long v = 0; json_find_int(j, "health_status", v); buf[0] = (uint8_t)v; return 4;
}

static int encode_pa_sdu_health_status_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 8) return -1;
    std::memset(buf, 0, 8);
    long long v = 0; char key[16];
    for (int i = 0; i < 4; ++i) {
        std::snprintf(key, sizeof(key), "pa%d_health", i + 1);
        v = 0; json_find_int(j, key, v); buf[i] = (uint8_t)v;
    }
    v = 0; json_find_int(j, "sdu_health", v); buf[4] = (uint8_t)v;
    return 8;
}

// ---------------------------------------------------------------------------
// Group 106 encoders moved to src/hf/format/g106_format.cpp
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// MRx Group 1 encoders
// ---------------------------------------------------------------------------
static int encode_mrx_system_version(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 16) return -1;
    std::memset(buf, 0, 16);
    double fw = 0, drv = 0, fpga = 0; long long tuner_id = 0;
    json_find_double(j, "fw_version",     fw);   json_find_double(j, "driver_version", drv);
    json_find_double(j, "fpga_version",   fpga); json_find_int(j,    "rf_tuner_id",    tuner_id);
    store_f32le(buf + 0, (float)fw); store_f32le(buf + 4, (float)drv); store_f32le(buf + 8, (float)fpga);
    store_u16le(buf + 12, (uint16_t)tuner_id);
    return 16;
}

static int encode_mrx_checksum(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 1024) return -1;
    std::memset(buf, 0, 1024);
    const char* key = std::strstr(j, "\"sjc_fw_checksum\"");
    if (!key) return 1024;
    const char* c = std::strchr(key, ':'); if (!c) return 1024;
    while (*c && *c != '"') ++c;
    if (*c != '"') return 1024; ++c;
    size_t idx = 0;
    while (*c && *c != '"' && idx < 1023) buf[idx++] = (uint8_t)*c++;
    return 1024;
}

static int encode_mrx_pbit_status(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 120) return -1;
    std::memset(buf, 0, 120);
    long long v = 0;
    auto gi = [&](const char* k, int i) { if (json_find_int(j, k, v)) buf[i] = (uint8_t)v; };
    gi("fpga_scratch_pad_test",    0); gi("fpga_board_id_read",      1);
    gi("processor_temp_status",    2); gi("fan_temp_status",          3);
    gi("fpga_temp_status",         4); gi("rfpsu_temp_status",        6);
    gi("fan_speed_ctrl_sensor",    7); gi("fan_voltage_status",       8);
    gi("rfsu_5v_status",           9); gi("rfsu_8v5_status",         10);
    gi("msata_detection_status",  11); gi("lo1_pll_lock_status",     12);
    gi("lo2_pll_lock_status",     13); gi("bite_pll_lock_status",    14);
    gi("tuner_detection_status",  15); gi("tuner_scratchpad_test",   18);
    gi("pll_lock_status",         21); gi("adc_bonding_status",      22);
    gi("storage_availability",    23); gi("digital_5v_status",       24);
    gi("digital_3v5_status",      25); gi("digital_psu_temp_status", 26);
    return 120;
}

static int encode_mrx_ibit_status(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 112) return -1;
    std::memset(buf, 0, 112);
    long long v = 0;
    auto gi = [&](const char* k, int i) { if (json_find_int(j, k, v)) buf[i] = (uint8_t)v; };
    gi("pll_lock_status",         0); gi("adc_bonding_status",      1);
    gi("msata_detection_status",  2); gi("storage_availability",    3);
    gi("tuner_lo1_pll_lock",      4); gi("tuner_lo2_pll_lock",      5);
    gi("tuner_bite_pll_lock",     6); gi("adc1_link_status",        7);
    gi("tuner_detection_status", 10); gi("tuner_scratchpad_test",  13);
    return 112;
}

static int encode_mrx_temperature(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 36) return -1;
    static const char* NAMES[] = {
        "bpt_temp_c", "psu_8156_temp_c", "tuner_temp_c", "psu_7255_temp_c",
        "processor_temp_c", "power_supply_temp_c", "control_board_temp_c",
        "rf_psu_temp_c", "fpga_temp_c"
    };
    for (int i = 0; i < 9; ++i) {
        double v = 0; json_find_double(j, NAMES[i], v); store_f32le(buf + i * 4, (float)v);
    }
    return 36;
}

static int encode_mrx_fan_speed(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    long long rpm = 0; json_find_int(j, "fan_speed_rpm", rpm);
    store_u32le(buf, (uint32_t)rpm); return 4;
}

static int encode_mrx_uart_test_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    long long v = 0;
    if (json_find_int(j, "expected_data", v)) buf[0] = (uint8_t)v;
    if (json_find_int(j, "observed_data", v)) buf[1] = (uint8_t)v;
    if (json_find_int(j, "result",        v)) buf[2] = (uint8_t)v;
    return 4;
}

static int encode_mrx_cbit_status(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 8) return -1;
    std::memset(buf, 0, 8);
    long long v = 0;
    auto gi = [&](const char* k, int i) { if (json_find_int(j, k, v)) buf[i] = (uint8_t)v; };
    gi("drx_status",             0); gi("voltage_status",        1);
    gi("temperature_status",     2); gi("tuner_detection_status",4);
    gi("memory_status",          7);
    return 8;
}

// ---------------------------------------------------------------------------
// MRx Group 3 encoders
// ---------------------------------------------------------------------------
static int encode_mrx_board_count_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    long long count = 0, tuner_id = 0;
    json_find_int(j, "board_count",        count);
    json_find_int(j, "available_tuner_id", tuner_id);
    store_u16le(buf + 0, (uint16_t)count); store_u16le(buf + 2, (uint16_t)tuner_id);
    return 4;
}

static int encode_mrx_channels_16b(const char* j, const char* arr_key,
                                    const char* status_key, uint8_t* buf, int max_len) {
    if (max_len < 16) return -1;
    std::memset(buf, 0, 16);
    const char* pd = std::strstr(j, arr_key); if (!pd) return 16;
    const char* arr = std::strchr(pd, '['); if (!arr) return 16;
    const char* p = arr + 1; int written = 0;
    while (written < 8) {
        while (*p && *p != '{' && *p != ']') ++p;
        if (!*p || *p == ']') break;
        const char* os = p; int depth = 1; ++p;
        while (*p && depth > 0) { if (*p == '{') ++depth; else if (*p == '}') --depth; ++p; }
        std::string entry(os, p);
        long long status = 0; json_find_int(entry.c_str(), status_key, status);
        store_u16le(buf + written * 2, (uint16_t)status); ++written;
    }
    return 16;
}

static int encode_mrx_tuning_details_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 8) return -1;
    std::memset(buf, 0, 8);
    long long v = 0;
    auto gi  = [&](const char* k, int i) { if (json_find_int(j, k, v)) buf[i] = (uint8_t)v; };
    auto gu  = [&](const char* k, int i) { if (json_find_int(j, k, v)) store_u16le(buf + i, (uint16_t)v); };
    gi("srx_tuned_status", 0); gi("mrx_tuned_status", 1);
    gu("srx_scan_mode_status",   2); gu("tuned_center_freq_mhz", 4);
    gi("memory_scan_tuned", 6); gi("bite_selection", 7);
    return 8;
}

// ---------------------------------------------------------------------------
// MRx Group 4 encoders
// ---------------------------------------------------------------------------
static int encode_mrx_audio_data_rsp(const char* j, uint8_t* buf, int max_len) {
    long long audio_size = 0; json_find_int(j, "audio_data_size", audio_size);
    if (audio_size < 0) audio_size = 0;
    if (audio_size > 262144) audio_size = 262144;
    int total = 4 + (int)audio_size * 2;
    if (total > max_len) return -1;
    std::memset(buf, 0, (size_t)total);
    store_u32le(buf, (uint32_t)audio_size);
    if (audio_size == 0) return 4;
    const char* pd = std::strstr(j, "\"audio_data\""); if (!pd) return total;
    const char* arr = std::strchr(pd, '['); if (!arr) return total; ++arr;
    int written = 0;
    while (written < (int)audio_size) {
        while (*arr == ' ' || *arr == '\t' || *arr == '\n' || *arr == ',') ++arr;
        if (*arr == ']' || !*arr) break;
        char* end; long long v = std::strtoll(arr, &end, 10);
        if (end == arr) break;
        store_u16le(buf + 4 + written * 2, (uint16_t)v); arr = end; ++written;
    }
    return total;
}

static int encode_mrx_iq_start_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    long long v = 0;
    if (json_find_int(j, "mrx_channel",          v)) buf[0] = (uint8_t)v;
    if (json_find_int(j, "narrowband_iq_status",  v)) buf[1] = (uint8_t)v;
    if (json_find_int(j, "wideband_iq_status",    v)) buf[2] = (uint8_t)v;
    return 4;
}

static int encode_mrx_iq_logging_stop_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 132) return -1;
    std::memset(buf, 0, 132);
    long long ch = 0; json_find_int(j, "mrx_channel", ch); store_u16le(buf, (uint16_t)ch);
    const char* key = std::strstr(j, "\"file_path\""); if (!key) return 132;
    const char* c = std::strchr(key, ':'); if (!c) return 132;
    while (*c && *c != '"') ++c; if (*c != '"') return 132; ++c;
    size_t idx = 0;
    while (*c && *c != '"' && idx < 127) buf[4 + idx++] = (uint8_t)*c++;
    return 132;
}

static int encode_mrx_memory_scan_data_rsp(const char* j, uint8_t* buf, int max_len) {
    long long count = 0, scan_speed = 0;
    json_find_int(j, "total_available_count",       count);
    json_find_int(j, "scan_speed_channels_per_sec", scan_speed);
    if (count < 0) count = 0; if (count > 10000) count = 10000;
    int total = 8 + (int)count * 20;
    if (total > max_len) return -1;
    std::memset(buf, 0, (size_t)total);
    store_u32le(buf + 0, (uint32_t)count); store_u16le(buf + 4, (uint16_t)scan_speed);
    if (count == 0) return 8;
    const char* pd = std::strstr(j, "\"scan_data\""); if (!pd) return 8;
    const char* arr = std::strchr(pd, '['); if (!arr) return 8;
    const char* p = arr + 1; int written = 0;
    while (written < (int)count) {
        while (*p && *p != '{' && *p != ']') ++p;
        if (!*p || *p == ']') break;
        const char* os = p; int depth = 1; ++p;
        while (*p && depth > 0) { if (*p == '{') ++depth; else if (*p == '}') --depth; ++p; }
        std::string entry(os, p); const char* e = entry.c_str();
        double power = 0, freq = 0; long long bw = 0;
        json_find_double(e, "power_dbm", power); json_find_double(e, "freq_hz", freq);
        json_find_int(e, "bandwidth_list", bw);
        uint8_t* slot = buf + 8 + written * 20;
        store_f32le(slot +  0, (float)power);
        store_f64le(slot +  4, freq / 1e6);
        store_u16le(slot + 18, (uint16_t)bw);
        ++written;
    }
    return 8 + written * 20;
}

static int encode_mrx_ddc_fft_rsp(const char* j, uint8_t* buf, int max_len) {
    long long bc = 0; json_find_int(j, "bin_count", bc);
    if (bc < 0) bc = 0; if (bc > 4096) bc = 4096;
    int total = 4 + (int)bc * 4;
    if (total > max_len) return -1;
    std::memset(buf, 0, (size_t)total);
    store_u16le(buf, (uint16_t)bc);
    if (bc == 0) return total;
    const char* pd = std::strstr(j, "\"ddc_fft_power_dbm\""); if (!pd) return total;
    const char* arr = std::strchr(pd, '['); if (!arr) return total; ++arr;
    int written = 0;
    while (written < (int)bc) {
        while (*arr == ' ' || *arr == '\t' || *arr == '\n' || *arr == ',') ++arr;
        if (*arr == ']' || !*arr) break;
        char* end; float v = (float)std::strtod(arr, &end);
        if (end == arr) break;
        store_f32le(buf + 4 + written * 4, v); arr = end; ++written;
    }
    return total;
}

static int encode_mrx_smart_scan_read_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 16) return -1;
    std::memset(buf, 0, 16);
    double freq = 0, amplitude = 0; long long ch = 0;
    json_find_double(j, "freq_hz",    freq);
    json_find_int(j,    "mrx_channel", ch);
    json_find_double(j, "amplitude",   amplitude);
    store_f64le(buf + 0, freq / 1e6);
    store_u16le(buf + 8, (uint16_t)ch);
    store_f32le(buf + 12, (float)amplitude);
    return 16;
}

static int encode_mrx_optical_port_status_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 12) return -1;
    std::memset(buf, 0, 12);
    long long v = 0;
    auto gu = [&](const char* k, int i) { if (json_find_int(j, k, v)) store_u16le(buf + i, (uint16_t)v); };
    gu("port_number", 0); gu("port_id", 2); gu("port_alive_status", 4);
    gu("already_transmitting", 6); gu("can_start_transfer", 8);
    return 12;
}

static int encode_mrx_optical_ip_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 24) return -1;
    std::memset(buf, 0, 24);
    const char* ip_key = std::strstr(j, "\"ip_address\"");
    if (ip_key) {
        const char* c = std::strchr(ip_key, ':'); if (c) { while (*c && *c != '"') ++c; }
        if (c && *c == '"') { ++c;
            unsigned a = 0, b = 0, cc = 0, d = 0;
            if (std::sscanf(c, "%u.%u.%u.%u", &a, &b, &cc, &d) == 4) {
                buf[0] = (uint8_t)a; buf[1] = (uint8_t)b; buf[2] = (uint8_t)cc; buf[3] = (uint8_t)d;
            }
        }
    }
    const char* pd = std::strstr(j, "\"port_ids\"");
    if (pd) {
        const char* arr = std::strchr(pd, '['); if (arr) { ++arr;
            for (int i = 0; i < 9; ++i) {
                while (*arr == ' ' || *arr == '\t' || *arr == '\n' || *arr == ',') ++arr;
                if (*arr == ']' || !*arr) break;
                char* end; long long v = std::strtoll(arr, &end, 10);
                if (end == arr) break;
                store_u16le(buf + 4 + i * 2, (uint16_t)v); arr = end;
            }
        }
    }
    return 24;
}

// ---------------------------------------------------------------------------
// MRx Group 5 encoders
// ---------------------------------------------------------------------------
static int encode_mrx_agc_status_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    long long rf = 0, ifatt = 0, agc = 0;
    json_find_int(j, "rf_attenuation_db", rf);
    json_find_int(j, "if_attenuation_db", ifatt);
    json_find_int(j, "agc_running",       agc);
    buf[0] = (uint8_t)rf; buf[1] = (uint8_t)ifatt; store_u16le(buf + 2, (uint16_t)agc);
    return 4;
}

// ---------------------------------------------------------------------------
// MRx Group 7 encoders
// ---------------------------------------------------------------------------
static int encode_mrx_signal_bite_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 16) return -1;
    std::memset(buf, 0, 16);
    double freq = 0, power = 0; long long result = 0;
    json_find_double(j, "observed_freq_hz",   freq);
    json_find_double(j, "observed_power_dbm", power);
    json_find_int(j,    "result",             result);
    store_f64le(buf + 0, freq / 1e6);
    store_f32le(buf + 8, (float)power);
    store_u16le(buf + 12, (uint16_t)result);
    return 16;
}

extern "C" SDFC_EXPORT int format_response(const char* kind, const char* kwargs_json,
                                           uint8_t** out_buf, size_t* out_len) {
    if (!kwargs_json || !out_buf || !out_len) return -1;
    long long group = 0, unit = 0, status = 0;
    if (!json_find_int(kwargs_json, "group_id", group)) return -1;
    if (!json_find_int(kwargs_json, "unit_id",  unit))  return -1;
    if (!json_find_int(kwargs_json, "status",   status)) return -1;

    uint8_t* payload = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(MAX_PAYLOAD)));
    if (!payload) return -1;
    int plen = 0;
    // is_ack: this unit_id is a known zero-payload ACK — skip payload_hex fallback.
    bool is_ack = false;

    typedef int (*EncFn)(const char*, uint8_t*, int);
    EncFn fn = nullptr;

    if (group == 100) {
        plen = g100_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack);
        if (plen < 0) { std::free(payload); return -1; }
    }
    else if (group == 101) {
        plen = g101_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack);
        if (plen < 0) { std::free(payload); return -1; }
    } else if (group == 109) {
        switch (unit) {
            case 12: is_ack = true;                     break; // Set Date/Time ACK
            case 16: fn = encode_auto_threshold_value;  break;
            case 18: fn = encode_hopper_channelization; break;
            default: break;
        }
    } else if (group == 111) {
        switch (unit) {
            case  4: fn = encode_signal_bite_resp;          break;
            case  6: is_ack = true;                         break; // Reference Input ACK
            case  8: fn = encode_module_health;             break;
            case 10: is_ack = true;                         break; // Send Protected Scan List ACK
            case 14: is_ack = true;                         break; // Protected Scan Enable ACK
            case 16: fn = encode_pdw_channelization;        break;
            case 18: is_ack = true;                         break; // FH Splitband Enable ACK
            case 20: is_ack = true;                         break; // FH Splitband Freq ACK
            case 22: fn = encode_bite_observed_rsp;         break;
            case 24: is_ack = true;                         break; // Spectrum Protected Band ACK
            case 26: fn = encode_storage_details;           break;
            case 28: fn = encode_read_protected_band_list;  break;
            case 30: is_ack = true;                         break; // Auto Threshold Enable ACK
            case 32: is_ack = true;                         break; // Hopper Channelization ACK
            // 211: cross-group stream rsp — no encoder; caller supplies payload_hex
            default: break;
        }
    } else if (group == 112) {
        switch (unit) {
            case  2: fn = encode_asu_sdu_config_rsp;    break;
            case  4: fn = encode_trsdu_receiver_status; break;
            case  6: fn = encode_pa_receiver_status;    break;
            case 14: is_ack = true;                     break; // Simulation Mode Config ACK
            case 38: is_ack = true;                     break; // HF fast scan ACK
            default: break;
        }
    } else if (group == 200) {
        switch (unit) {
            case 10: is_ack = true;                           break; // Send ECM Reports ACK
            case 12: is_ack = true;                           break; // Start List Jam ACK
            case 14: is_ack = true;                           break; // Stop List Jam ACK
            case 16: fn = encode_list_jam_report;             break;
            case 18: is_ack = true;                           break; // Start Follow-on Jam ACK
            case 20: is_ack = true;                           break; // Stop Follow-on Jam ACK
            case 22: is_ack = true;                           break; // Start Responsive Sweep Jam ACK
            case 24: is_ack = true;                           break; // Stop Responsive Sweep Jam ACK
            case 42: fn = encode_hpasu_health_status_rsp;    break;
            case 55: fn = encode_pa_sdu_health_status_rsp;   break;
            case 57: is_ack = true;                           break; // PA Soft Reboot ACK
            default: break;
        }
    } else if (group == 106) {
        plen = g106_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack);
        if (plen < 0) { std::free(payload); return -1; }
    } else if (group == 1) {   // MRx diagnostics
        switch (unit) {
            case  2: fn = encode_mrx_system_version; break;
            case  4: fn = encode_mrx_checksum;        break;
            case  6: fn = encode_mrx_pbit_status;     break;
            case  8: fn = encode_mrx_ibit_status;     break;
            case 10: fn = encode_mrx_temperature;     break;
            case 14: fn = encode_mrx_fan_speed;       break;
            case 18: fn = encode_mrx_uart_test_rsp;   break;
            case 26: fn = encode_mrx_cbit_status;     break;
            case 34: is_ack = true;                   break; // Close All Channels ACK
            default: break;
        }
    } else if (group == 3) {   // MRx RF board
        switch (unit) {
            case  2: fn = encode_mrx_board_count_rsp;  break;
            case 18: {
                plen = encode_mrx_channels_16b(kwargs_json, "\"channels\"", "status", payload, MAX_PAYLOAD);
                if (plen < 0) { std::free(payload); return -1; }
                break;
            }
            case 20: is_ack = true; break; // Write Channel ACK
            case 22: {
                plen = encode_mrx_channels_16b(kwargs_json, "\"channel_init_statuses\"", "init_status", payload, MAX_PAYLOAD);
                if (plen < 0) { std::free(payload); return -1; }
                break;
            }
            case 24: fn = encode_mrx_tuning_details_rsp; break;
            case 26: fn = encode_mrx_cbit_status;         break;
            default: break;
        }
    } else if (group == 4) {   // MRx data acquisition
        switch (unit) {
            case  6: is_ack = true;                        break; // Set Threshold ACK
            case  8: fn = encode_mrx_audio_data_rsp;       break;
            case 10: is_ack = true;                        break; // Audio Start Play ACK
            case 12: is_ack = true;                        break; // Audio Stop Play ACK
            case 16: is_ack = true;                        break; // Audio FIFO Reset ACK
            case 18: is_ack = true;                        break; // Demod/BW Select ACK
            case 24: fn = encode_mrx_iq_start_rsp;         break;
            case 26: fn = encode_mrx_iq_logging_stop_rsp;  break;
            case 34: fn = encode_mrx_iq_start_rsp;         break;
            case 36: is_ack = true;                        break; // Stop IQ Streaming ACK
            case 40: is_ack = true;                        break; // Configure Memory Scan ACK
            case 42: fn = encode_mrx_memory_scan_data_rsp; break;
            case 44: fn = encode_mrx_ddc_fft_rsp;          break;
            case 54: is_ack = true;                        break; // Engage Channel ACK
            case 56: is_ack = true;                        break; // Disengage Channel ACK
            case 58: is_ack = true;                        break; // Stop Memory Scan ACK
            case 60: is_ack = true;                        break; // Smart Memory Scan Config ACK
            case 62: fn = encode_mrx_smart_scan_read_rsp;  break;
            case 64: is_ack = true;                        break; // Stop Smart Memory Scan ACK
            case 66: is_ack = true;                        break; // Start Optical IQ ACK
            case 68: is_ack = true;                        break; // Stop Optical IQ ACK
            case 70: fn = encode_mrx_optical_port_status_rsp; break;
            case 72: fn = encode_mrx_optical_ip_rsp;          break;
            default: break;
        }
    } else if (group == 5) {   // MRx tuner
        switch (unit) {
            case  2: is_ack = true;                  break; // Set Center Freq ACK
            case  4: is_ack = true;                  break; // Attenuation Select ACK
            case 10: is_ack = true;                  break; // Clear Center Freq ACK
            case 14: fn = encode_mrx_agc_status_rsp; break;
            default: break;
        }
    } else if (group == 6) {   // MRx FH monitoring / GO2Monitor
        switch (unit) {
            case  8: is_ack = true; break; // FH Monitoring Config ACK
            case 10: is_ack = true; break; // GO2Monitor Connect ACK
            case 12: is_ack = true; break; // GO2Monitor Disconnect ACK
            case 14: is_ack = true; break; // Start GO2Monitor ACK
            case 16: is_ack = true; break; // Stop GO2Monitor ACK
            default: break;
        }
    } else if (group == 7) {   // MRx Signal BITE / misc
        switch (unit) {
            case  2: fn = encode_mrx_signal_bite_rsp; break;
            case  4: is_ack = true; break; // BITE/Antenna Select ACK
            case  6: is_ack = true; break; // Ref Source Select ACK
            case 10: is_ack = true; break; // AFC ACK
            case 12: is_ack = true; break; // RF Squelch ACK
            case 14: is_ack = true; break; // IQ Socket Open ACK
            case 16: is_ack = true; break; // IQ Socket Close ACK
            case 18: is_ack = true; break; // Spectrum Avg Count ACK
            case 20: is_ack = true; break; // Smart RF AGC ACK
            case 22: is_ack = true; break; // Audio Squelch ACK
            case 24: is_ack = true; break; // Set Date/Time ACK
            default: break;
        }
    }

    if (fn) {
        plen = fn(kwargs_json, payload, MAX_PAYLOAD);
        if (plen < 0) { std::free(payload); return -1; }
    }

    // Only fall through to payload_hex for truly unknown group/unit combinations
    // (e.g. raw stream responses). Known ACK responses skip this path.
    if (!is_ack && plen == 0) {
        const char* ph = std::strstr(kwargs_json, "\"payload_hex\"");
        if (ph) {
            const char* q = std::strchr(ph, ':');
            if (q) { q = std::strchr(q, '"'); }
            if (q) {
                ++q;
                while (*q && *q != '"') {
                    int hi = hex_nibble(*q++);
                    if (*q == '"' || !*q) break;
                    int lo = hex_nibble(*q++);
                    if (hi < 0 || lo < 0) { std::free(payload); return -1; }
                    if (plen >= MAX_PAYLOAD) { std::free(payload); return -1; }
                    payload[plen++] = static_cast<uint8_t>((hi << 4) | lo);
                }
            }
        }
    }

    int total = RESP_OVERHEAD + plen;
    uint8_t* buf = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(total)));
    if (!buf) { std::free(payload); return -1; }

    std::memcpy(buf, RESP_HEADER, MAGIC_LEN);
    store_i16le(buf + RESP_OFF_STATUS, static_cast<int16_t>(status));
    store_u32le(buf + RESP_OFF_SIZE,   static_cast<uint32_t>(plen));
    store_u16le(buf + RESP_OFF_GROUP,  static_cast<uint16_t>(group));
    store_u16le(buf + RESP_OFF_UNIT,   static_cast<uint16_t>(unit));
    if (plen > 0) std::memcpy(buf + RESP_OFF_PAYLOAD, payload, static_cast<size_t>(plen));
    std::memcpy(buf + RESP_OFF_PAYLOAD + plen, RESP_FOOTER, MAGIC_LEN);
    std::free(payload);
    *out_buf = buf;
    *out_len = static_cast<size_t>(total);
    return 0;
}

// =============================================================================
// ABI: free_result
// =============================================================================
extern "C" SDFC_EXPORT void free_result(void* ptr) {
    std::free(ptr);
}
