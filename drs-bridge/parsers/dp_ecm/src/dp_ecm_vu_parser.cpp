// drs-bridge/parsers/dp_ecm/src/dp_ecm_vu_parser.cpp
//
// DP-ECM-1074 VU (VHF/UHF/SHF) Receiver/Processor/Exciter parser DLL.
// Hardware band: 30 MHz – 6000 MHz.  Protocol doc: DP-ECM-1074-6000-V1-ICD-0V04.
// Implements the 4-symbol ABI (sdfc_abi.h) on top of the shared frame core.
//
// Increment 2 scope (cumulative):
//   Group 100 (SJC diagnostics) — full response decoders per ICD §4.1.1:
//     100/2  — Get System Version    (28 bytes)
//     100/4  — SRx Checksum          (1000 bytes)
//     100/6  — PBIT Status           (88 bytes, 25 fields + BITE_PATH_VALIDATION + RPE_CONFIG)
//     100/8  — IBIT Status           (68 bytes, 14 fields + BITE_PATH_VALIDATION)
//     100/10 — Temperature Status    (36 bytes, 7 floats: proc/psu/fan/rfpsu/fpga/digital/tuner)
//     100/14 — Fan Speed Status      (4 bytes)
//     100/18 — UART Test Status      (4 bytes)
//     100/26 — CBIT Status           (8 bytes)
//   Group 101 (SJC detection) — commands + responses per ICD §4.1.2:
//     101/25 cmd — Set Threshold & Integration Time (8 bytes: reserved×2 + intg_time + threshold)
//     101/26 rsp — Set Threshold ACK    (0 bytes)
//     101/27 cmd — Set Resolution       (4 bytes: reserved + resolution enum 0-5 = 6.25-200kHz)
//     101/28 rsp — Set Resolution ACK   (0 bytes)
//     101/37 cmd — Configure Detection  (12 bytes: freq range, atten, mode, AGC)
//     101/38 rsp — Configure Detection ACK (0 bytes)
//     101/40 rsp — FH Detection         (4 + 60*count, VU-extended 60B entries)
//     101/47 cmd — Set Pulse Range      (8 bytes: max_pulse_ms + min_pulse_ms)
//     101/48 rsp — Set Pulse Range ACK  (0 bytes)
//     101/55 cmd — Set Min Hops Count   (4 bytes: uint32)
//     101/56 rsp — Set Min Hops ACK     (0 bytes)
//   Group 200 (VU jamming, VU-specific):
//     200/2  — Immediate Jam ACK
//     200/4  — Follow-On Jam ACK
//     200/6  — Jam List ACK
//     200/8  — Responsive Sweep Jam ACK
//   All other units fall through to raw_hex envelope (never crashes).
#include "sdfc_abi.h"
#include "sdfc_frame.h"
#include "sdfc_endian.h"
#include "json_writer.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <cstdio>

using namespace sdfc;

// ---- variant identity (the ONLY thing the VU sister DLL changes here) ----
static constexpr const char* HW_NAME = "dp_ecm_vu";

// =============================================================================
// Per-message decoders (payload -> JSON fields).
// Each receives the payload pointer, payload byte count, and the JsonWriter.
// All status fields: 1 = PASS, 0 = FAIL (per ICD §3 note).
// =============================================================================

// 100/2 — Get System Version Details (28 bytes).
// Per ICD §4.1.1.1 Table 4 (VU-specific layout):
//   @0  SJC FW Version    (float,  4B)
//   @4  Driver Version    (float,  4B)
//   @8  FPGA Version      (float,  4B)
//   @12 BSP Version       (float,  4B)  — VU only (HF omits this field)
//   @16 Processor ID      (uint16, 2B)
//   @18 RF Tuner ID[0]    (uint16, 2B)  — VU carries 3 tuner IDs
//   @20 RF Tuner ID[1]    (uint16, 2B)
//   @22 RF Tuner ID[2]    (uint16, 2B)
//   @24 FPGA Type ID      (uint16, 2B)
//   @26 Reserved          (uint16, 2B)
static void decode_system_version(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 28) { w.key_str("warning", "system_version payload < 28 bytes"); return; }
    w.key_double("sjc_fw_version",    static_cast<double>(load_f32le(p +  0)));
    w.key_double("driver_version",    static_cast<double>(load_f32le(p +  4)));
    w.key_double("fpga_version",      static_cast<double>(load_f32le(p +  8)));
    w.key_double("bsp_version",       static_cast<double>(load_f32le(p + 12)));
    w.key_uint("processor_id",        load_u16le(p + 16));
    char tuner[32];
    std::snprintf(tuner, sizeof(tuner), "[%u,%u,%u]",
                  load_u16le(p + 18), load_u16le(p + 20), load_u16le(p + 22));
    w.key_raw("sjc_rf_tuner_ids", tuner);
    w.key_uint("fpga_type_id",        load_u16le(p + 24));
}

// 100/4 — Get SRx Checksum Details (1000 bytes).
// Per ICD §4.1.1.2: SJC FW Checksum char[1000] (VU SRx).
// Note: MRx Group 1/Unit 4 is 1024 bytes — different command/group entirely.
static void decode_srx_checksum(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 1000) { w.key_str("warning", "srx_checksum payload < 1000 bytes"); return; }
    w.key_str("sjc_fw_checksum_hex", to_hex(p, 1000));
}

// 100/6 — PBIT Status (88 bytes).
// Per ICD Table 8 (fields @0-@27) + Table 9 (S_RES_BITE_PATH_VALIDATION @28-@75,
// S_RPE_CONFIG_FILE_STATUS @76-@87).
// S_WBFSR_RES_VUHF_BITE_TEST (12B each): bite_freq_hz(f32@0), bite_power(f32@4),
// bite_result(u16@8), reserved(u16@10). 4 entries in BITE_PATH_VALIDATION.
static void decode_pbit_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 88) { w.key_str("warning", "pbit_status payload < 88 bytes"); return; }
    w.key_uint("fpga_scratch_pad_test_status",    p[ 0]);
    w.key_uint("fpga_board_id_read",              p[ 1]);
    w.key_uint("processor_temperature_status",    p[ 2]);
    w.key_uint("fan_temperature_status",          p[ 3]);
    w.key_uint("fpga_temperature_status",         p[ 4]);
    w.key_uint("power_supply_temperature_status", p[ 5]);
    w.key_uint("rf_psu_temp_status",              p[ 6]);
    w.key_uint("fan_speed_control_sens_status",   p[ 7]);
    w.key_uint("fan_voltage_status",              p[ 8]);
    w.key_uint("rf_psu_5v_status",                p[ 9]);
    w.key_uint("rf_psu_8v_status",                p[10]);
    w.key_uint("msata_detection_status",          p[11]);
    w.key_uint("lo1_pll_lock_status",             p[12]);
    w.key_uint("lo2_pll_lock_status",             p[13]);
    w.key_uint("bite_pll_lock_status",            p[14]);
    w.key_uint("tuner_detection_status",          p[15]);
    w.key_uint("tuner_scratchpad_test_status",    p[16]);
    w.key_uint("pll_lock_status",                 p[17]);
    w.key_uint("adc_bonding_status",              p[18]);
    w.key_uint("storage_availability_status",     p[19]);
    w.key_uint("dac_bonding_status_0",            p[20]);
    w.key_uint("dac_bonding_status_1",            p[21]);
    // p[22-23] reserved
    w.key_uint("digital_5v_status",               p[24]);
    w.key_uint("digital_3v5_status",              p[25]);
    w.key_uint("digital_psu_temp_status",         p[26]);
    // p[27] reserved
    // S_RES_BITE_PATH_VALIDATION: 4 × S_WBFSR_RES_VUHF_BITE_TEST @28-@75
    std::string bite_arr = "[";
    for (int i = 0; i < 4; ++i) {
        const uint8_t* b = p + 28 + i * 12;
        JsonWriter bw;
        bw.key_double("bite_freq_hz", static_cast<double>(load_f32le(b + 0)));
        bw.key_double("bite_power",   static_cast<double>(load_f32le(b + 4)));
        bw.key_uint("bite_result",    load_u16le(b + 8));
        if (i) bite_arr += ',';
        bite_arr += bw.str();
    }
    bite_arr += "]";
    w.key_raw("bite_path_validation", bite_arr);
    // S_RPE_CONFIG_FILE_STATUS @76-@87
    const uint8_t* r = p + 76;
    w.key_uint("rpe_pll_cfg_status",        r[ 0]);
    w.key_uint("rpe_adc_cfg_status",        r[ 1]);
    w.key_uint("rpe_dac1_cfg_status",       r[ 2]);
    w.key_uint("rpe_dac2_cfg_status",       r[ 3]);
    w.key_uint("rpe_dac_atten_cfg_status",  r[ 4]);
    w.key_uint("rpe_tuner_lo1_cfg_status",  r[ 5]);
    w.key_uint("rpe_tuner_lo2_cfg_status",  r[ 6]);
    w.key_uint("rpe_tuner_bite_cfg_status", r[ 7]);
    w.key_uint("rpe_lo_atten_cfg_status",   r[ 8]);
    w.key_uint("rpe_rf_gain_cal_status",    r[ 9]);
    // r[10-11] reserved
}

// 100/8 — IBIT Status (68 bytes).
// Per ICD Table 11 (fields @0-@19) + Table 12 (S_RES_BITE_PATH_VALIDATION @20-@67).
// S_WBFSR_RES_VUHF_BITE_TEST (12B each): bite_freq_hz(f32@0), bite_power(f32@4),
// bite_result(u16@8), reserved(u16@10). 4 entries.
static void decode_ibit_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 68) { w.key_str("warning", "ibit_status payload < 68 bytes"); return; }
    w.key_uint("pll_lock_status",              p[ 0]);
    w.key_uint("adc_bonding_status",           p[ 1]);
    w.key_uint("msata_detection_status",       p[ 2]);
    w.key_uint("storage_availability_status",  p[ 3]);
    w.key_uint("tuner_lo1_pll_lock_status",    p[ 4]);
    w.key_uint("tuner_lo2_pll_lock_status",    p[ 5]);
    w.key_uint("tuner_bite_pll_lock_status",   p[ 6]);
    w.key_uint("adc_link_status",              p[ 7]);
    // p[8-9] reserved
    w.key_uint("tuner_detection_status",       p[10]);
    // p[11-12] reserved
    w.key_uint("tuner_scratchpad_test_status", p[13]);
    // p[14-15] reserved
    w.key_uint("dac_bonding_status_0",         p[16]);
    w.key_uint("dac_bonding_status_1",         p[17]);
    // p[18-19] reserved
    // S_RES_BITE_PATH_VALIDATION: 4 × S_WBFSR_RES_VUHF_BITE_TEST @20-@67
    std::string bite_arr = "[";
    for (int i = 0; i < 4; ++i) {
        const uint8_t* b = p + 20 + i * 12;
        JsonWriter bw;
        bw.key_double("bite_freq_hz", static_cast<double>(load_f32le(b + 0)));
        bw.key_double("bite_power",   static_cast<double>(load_f32le(b + 4)));
        bw.key_uint("bite_result",    load_u16le(b + 8));
        if (i) bite_arr += ',';
        bite_arr += bw.str();
    }
    bite_arr += "]";
    w.key_raw("bite_path_validation", bite_arr);
}

// 100/10 — Temperature Status (36 bytes, 7 floats + 8B reserved).
// Per ICD Table 17 (VU §4.1.1.6): all temperatures in degrees Celsius.
//   @0  Processor Temperature  (float)
//   @4  PSU Temperature        (float)
//   @8  Fan Temperature        (float)
//   @12 RFPSU Temperature      (float)
//   @16 FPGA Temperature       (float)
//   @20 Digital Temperature    (float)
//   @24 Tuner Temperature      (float)
//   @28 Reserved               (2×float)
static void decode_temperature(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 36) { w.key_str("warning", "temperature payload < 36 bytes"); return; }
    w.key_double("processor_temp_c",   static_cast<double>(load_f32le(p +  0)));
    w.key_double("psu_temp_c",         static_cast<double>(load_f32le(p +  4)));
    w.key_double("fan_temp_c",         static_cast<double>(load_f32le(p +  8)));
    w.key_double("rf_psu_temp_c",      static_cast<double>(load_f32le(p + 12)));
    w.key_double("fpga_temp_c",        static_cast<double>(load_f32le(p + 16)));
    w.key_double("digital_temp_c",     static_cast<double>(load_f32le(p + 20)));
    w.key_double("tuner_temp_c",       static_cast<double>(load_f32le(p + 24)));
}

// 100/14 — Fan Speed Status (4 bytes).
// Per ICD Table 21: Fan Speed in RPM (uint32).
static void decode_fan_speed_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "fan_speed_status payload < 4 bytes"); return; }
    w.key_uint("fan_speed_rpm", load_u32le(p + 0));
}

// 100/18 — UART Test Status (4 bytes).
// Per ICD Table 27:
//   @0 Expected Data  (uint8)
//   @1 Observed Data  (uint8)
//   @2 Result         (uint8) — 1 = PASS, 0 = FAIL
//   @3 Reserved       (uint8)
static void decode_uart_test(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "uart_test payload < 4 bytes"); return; }
    w.key_uint("expected_data", p[0]);
    w.key_uint("observed_data", p[1]);
    w.key_uint("result",        p[2]);
    //reserved p[3]
}

// 100/26 — CBIT Status (8 bytes).
// Per ICD Table 14 (VU §4.1.1.5): DRX/voltage/temperature/tuner_detection + memory.
//   @0 DRX Status              (uint8)
//   @1 Voltage Status          (uint8)
//   @2 Temperature Status      (uint8)
//   @3 Reserved                (uint8)
//   @4 Tuner Detection Status  (uint8)
//   @5 Reserved x2             (uint8)
//   @7 Memory Status           (uint8)
static void decode_cbit_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "cbit_status payload < 8 bytes"); return; }
    w.key_uint("drx_status",             p[0]);
    w.key_uint("voltage_status",         p[1]);
    w.key_uint("temperature_status",     p[2]);
    // p[3] reserved
    w.key_uint("tuner_detection_status", p[4]);
    // p[5-6] reserved
    w.key_uint("memory_status",          p[7]);
}

// =============================================================================
// Group 101 command decoders (ECM Client -> hardware).
// All Group 101 responses are 0-byte ACKs — no payload to decode.
// =============================================================================

// 101/25 — Set Threshold & Integration Time (8 bytes).
// Per ICD Table 25 (VU §4.1.2.1):
//   @0 Reserved              (uint8)
//   @1 Reserved              (uint8)
//   @2 Integration Time Sel  (uint16, 0-17: 10us to 1310720us)
//   @4 Threshold (in dBm)    (float)
static void decode_cmd_set_threshold(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "set_threshold cmd payload < 8 bytes"); return; }
    static const int INT[] = {
    10, 20, 40, 80, 160, 320, 640, 1280,
    2560, 5120, 10240, 20480, 40960, 81920,
    163840, 327680, 655360, 1310720
    };
    static const int minimumPulseLength[] = {
    320, 320, 320, 320, 480, 960, 1920, 3840,
    7680, 15360, 30720, 30720, 30720,
    30720, 30720, 30720, 30720,30720
    };

    static const int maximumPulseLength[] = {
        12000, 12000, 12000, 12000, 12000, 12000, 12000, 12000,
        12000, 20000, 50000, 50000, 50000,
        50000, 50000, 50000, 50000,50000
    };
    int int_idx = load_u16le(p+2);
    w.key_uint("integration_time_sel", load_u16le(p + 2));
    w.key_int("integration_time_name_us",int_idx < 18 ? INT[int_idx] : -1);
    w.key_int("fh_max_pulse_us",int_idx < 18 ? maximumPulseLength[int_idx] : -1);
    w.key_int("fh_min_pulse_us",int_idx < 18 ? minimumPulseLength[int_idx] : -1);
    w.key_double("threshold_dbm",      static_cast<double>(load_f32le(p + 4)));

}

// 101/27 — Set Resolution (4 bytes).
// Per ICD Table 27 (VU §4.1.2.2): resolution enum 0-5 maps to kHz values.
//   @0 Reserved    (uint8)
//   @1 Resolution  (uint8, 0-5: 6.25/12.5/25/50/100/200 kHz)
//   @2 Reserved x2 (uint8)
static void decode_cmd_set_resolution(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "set_resolution cmd payload < 4 bytes"); return; }
    static const double RES_KHZ[] = {6.25, 12.5, 25.0, 50.0, 100.0, 200.0};
    uint8_t res_idx = p[1];
    w.key_uint("resolution_index", res_idx);
    if (res_idx < 6) {
        w.key_double("resolution_khz", RES_KHZ[res_idx]);
    } else {
        w.key_str("warning", "resolution index out of range 0-5");
    }
}

// 101/47 — Set Min/Max FH Pulse Range (8 bytes).
// Per ICD Table 29 (VU §4.1.2.3): two floats in milliseconds.
//   @0 Max Pulse Length (ms)  (float)
//   @4 Min Pulse Length (ms)  (float)
static void decode_cmd_set_pulse_range(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) {
        w.key_str("warning", "set_pulse_range cmd payload < 8 bytes");
        return;
    }
    w.key_double("fh_max_pulse_ms", static_cast<double>(load_f32le(p + 0)));
    w.key_double("fh_min_pulse_ms", static_cast<double>(load_f32le(p + 4)));
}

// 101/55 — Set Minimum Hops Count (4 bytes).
// Per ICD Table 45.
//   @0 Min Hops Count (uint32)
static void decode_cmd_set_min_hops(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { 
        w.key_str("warning", "set_min_hops cmd payload < 4 bytes"); 
        return; 
    }
    w.key_uint("min_hops_count", load_u32le(p + 0));
}

// 101/37 — Configure Detection (12 bytes).
// Per ICD Table 37 (VU §4.1.2.6):
//   @0  Start Frequency MHz         (uint16)
//   @2  Stop Frequency MHz          (uint16)
//   @4  RF Attenuation dB           (uint8)
//   @5  IF Attenuation dB           (uint8)
//   @6  Detection Mode              (uint8) — 0=FH, 1=FF, 2=Burst, 3=Combined
//   @7  AGC On/Off                  (uint8) — 1=On, 0=Off
//   @8  Zoom-Spectrum Report        (uint8) — 1=Enable, 0=Disable
//   @9  Antenna Selection           (uint8) — 0=Omni, 1=Directional
//   @10 Reserved x2                 (uint8)
static void decode_cmd_configure_detection(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 12) {
        w.key_str("warning", "configure_detection cmd payload < 12 bytes");
        return;
    }
    static const char* MODE_NAMES[] = {
        "hopper_frequency", "fixed_frequency", "burst_frequency", "combined_signal"
    };
    uint8_t mode = p[6];
    w.key_double("start_freq_hz",        static_cast<double>(load_u16le(p + 0)) * 1e6);
    w.key_double("stop_freq_hz",         static_cast<double>(load_u16le(p + 2)) * 1e6);
    w.key_uint("rf_attenuation_db",      p[4]);
    w.key_uint("if_attenuation_db",      p[5]);
    w.key_uint("detection_mode",         mode);
    w.key_str("detection_mode_name",     mode < 4 ? MODE_NAMES[mode] : "unknown");
    w.key_bool("agc_enabled",            p[7] != 0);
    w.key_bool("zoom_spectrum_enabled",  p[8] != 0);
    w.key_uint("antenna_selection",      p[9]);
}

// 101/39 — Start FH Detection command (8 bytes).
// Per ICD Table 51: start/stop freq as float MHz (distinct from 101/37 which uses uint16).
static void decode_cmd_start_fh_detection(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "start_fh_detection cmd < 8 bytes"); return; }
    w.key_double("start_freq_hz", static_cast<double>(load_f32le(p + 0)) * 1e6);
    w.key_double("stop_freq_hz",  static_cast<double>(load_f32le(p + 4)) * 1e6);
}

// 101/43 — Get Wideband FFT Data command (8 bytes).
// Per ICD Table 49: IF Attenuation is UNSIGNED char (unlike 101/69/83/85 which are signed).
static void decode_cmd_get_wideband_fft(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "get_wideband_fft cmd < 8 bytes"); return; }
    w.key_double("start_freq_hz",   static_cast<double>(load_u16le(p + 0)) * 1e6);
    w.key_double("stop_freq_hz",    static_cast<double>(load_u16le(p + 2)) * 1e6);
    w.key_uint("rf_attenuation_db", p[4]);
    w.key_uint("if_attenuation_db", p[5]);
}

// 101/69 — Start FF Detection command (8 bytes).
// Per ICD Table 53: freq-sweep layout, IF attenuation signed char.
static void decode_cmd_start_ff_detection(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "start_ff_detection cmd < 8 bytes"); return; }
    w.key_double("start_freq_hz",   static_cast<double>(load_u16le(p + 0)) * 1e6);
    w.key_double("stop_freq_hz",    static_cast<double>(load_u16le(p + 2)) * 1e6);
    w.key_uint("rf_attenuation_db", p[4]);
    w.key_int("if_attenuation_db",  static_cast<int8_t>(p[5]));
}

// 101/83 — Start Burst Detection command (8 bytes).
// Per ICD Table 57: same freq-sweep layout; IF attenuation signed.
static void decode_cmd_start_burst_detection(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "start_burst_detection cmd < 8 bytes"); return; }
    w.key_double("start_freq_hz",   static_cast<double>(load_u16le(p + 0)) * 1e6);
    w.key_double("stop_freq_hz",    static_cast<double>(load_u16le(p + 2)) * 1e6);
    w.key_uint("rf_attenuation_db", p[4]);
    w.key_int("if_attenuation_db",  static_cast<int8_t>(p[5]));
}

// 101/85 — Start Scan Speed command (8 bytes).
// Per ICD Table 59: same freq-sweep layout; IF attenuation signed.
static void decode_cmd_start_scan_speed(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "start_scan_speed cmd < 8 bytes"); return; }
    w.key_double("start_freq_hz",   static_cast<double>(load_u16le(p + 0)) * 1e6);
    w.key_double("stop_freq_hz",    static_cast<double>(load_u16le(p + 2)) * 1e6);
    w.key_uint("rf_attenuation_db", p[4]);
    w.key_int("if_attenuation_db",  static_cast<int8_t>(p[5]));
}

// 101/87 — Stop Scan Speed command: 0 bytes, no payload to decode.

// 101/94 — Get Zoom Band FFT Data command (8 bytes). Per ICD Table 52.
// center_freq_mhz (float) + bw_index (int32). BW: 0=2.5 MHz, 1=5 MHz, 2=10 MHz.
static void decode_cmd_get_zoom_fft(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "get_zoom_fft cmd < 8 bytes"); return; }
    static const double BW_MHZ[] = {2.5, 5.0, 10.0};
    int32_t bw_idx = load_i32le(p + 4);
    w.key_double("center_freq_hz", static_cast<double>(load_f32le(p + 0)) * 1e6);
    w.key_int("bw_index", bw_idx);
    if (bw_idx >= 0 && bw_idx < 3)
        w.key_double("bw_mhz", BW_MHZ[bw_idx]);
}

// 101/100 — Set Flatness Mode command (12 bytes).
// Per ICD Table 63: mode enum (uint32), start/stop freq (float MHz).
// Mode: 0=Disable, 1=Min Point, 2=Avg Point.
static void decode_cmd_set_flatness_mode(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 12) { w.key_str("warning", "set_flatness_mode cmd < 12 bytes"); return; }
    static const char* MODE_NAMES[] = {"disable", "min_point", "avg_point"};
    uint32_t mode = load_u32le(p + 0);
    w.key_uint("flatness_mode",     mode);
    // w.key_str("flatness_mode_name", mode < 3 ? MODE_NAMES[mode] : "unknown");
    w.key_double("start_freq_hz",   static_cast<double>(load_f32le(p + 4)) * 1e6);
    w.key_double("stop_freq_hz",    static_cast<double>(load_f32le(p + 8)) * 1e6);
}

// 101/102 — Set Integration Time command (4 bytes).
// Per ICD Table 65: uint32 index 0–15 mapping to integration time in microseconds.
static void decode_cmd_set_integration_time(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { 
        w.key_str("warning", "set_integration_time cmd < 4 bytes"); 
        return; 
    }
    static const uint32_t TIME_US[] = {
        20, 40, 80, 160, 320, 640, 1280, 2560, 5120, 10240,
        20480, 40960, 81920, 16380, 327680, 655360
    };
    uint32_t idx = load_u32le(p + 0);
    w.key_uint("integration_time_index", idx);
    if (idx < 16) {
        w.key_uint("integration_time_us", TIME_US[idx]);
    }
}

// 101/104 — Set Multi-Band FH Mode command (4 bytes).
// Per ICD Table 67: mode selection (uint8) + 3 reserved.
static void decode_cmd_set_multifh_mode(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { 
        w.key_str("warning", "set_multifh_mode cmd < 4 bytes"); 
        return; 
    }
    w.key_uint("multi_fh_mode", p[0]);
}

// 101/106 — Set Narrow Band FH command (4 bytes).
// Per ICD Table 69: mode selection (uint8) + 3 reserved.
static void decode_cmd_set_narrowband_fh(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { 
        w.key_str("warning", "set_narrowband_fh cmd < 4 bytes"); 
        return; 
    }
    w.key_uint("narrow_band_fh_mode", p[0]);
}

// 101/158 — Terminate FFT Thread command (4 bytes).
// Per ICD Table 55: Terminate Flag (uint8) + 3 reserved. Value 0 = terminate.
static void decode_cmd_terminate_fft(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "terminate_fft cmd < 4 bytes"); return; }
    w.key_bool("terminate", p[0] == 0);
}

// 101/140 — Configure Center Frequency for Fast Scan mode (32 bytes). Per ICD Table 56.
// @2 source_selection (uint8) + @4 bandwidth_selection (uint8) + @5 system_operator_mode (uint8, 0=Cal/1=Verif/2=RT)
// @6 calib_verify_mode (uint8) + @8 center_freq (float MHz) + @12 start_freq (float MHz)
// @16 stop_freq (float MHz) + @20 threshold (float dBm) + @24 attenuation (float dB) + @28 jam_antenna_selection (uint8).
static void decode_cmd_configure_center_freq(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 32) { w.key_str("warning", "configure_center_freq cmd < 32 bytes"); return; }
    static const char* OP_MODE[] = {"calibration", "verification", "real_time"};
    uint8_t op_mode = p[5];
    w.key_uint("source_selection",      p[2]);
    w.key_uint("bandwidth_selection",   p[4]);
    w.key_uint("system_operator_mode",  op_mode);
    if (op_mode < 3) w.key_str("system_operator_mode_name", OP_MODE[op_mode]);
    w.key_uint("calib_verify_mode",     p[6]);
    w.key_double("center_freq_hz",      static_cast<double>(load_f32le(p +  8)) * 1e6);
    w.key_double("start_freq_hz",       static_cast<double>(load_f32le(p + 12)) * 1e6);
    w.key_double("stop_freq_hz",        static_cast<double>(load_f32le(p + 16)) * 1e6);
    w.key_double("threshold_dbm",       static_cast<double>(load_f32le(p + 20)));
    w.key_double("attenuation_db",      static_cast<double>(load_f32le(p + 24)));
    w.key_uint("jam_antenna_selection", p[28]);
}

// 101/160 — Spectrum Acquisition Parameters command (8 bytes). Per ICD Table 59.
// start_freq (uint16 MHz) + stop_freq (uint16 MHz) + rf_atten (uint8) + if_atten (uint8)
// + agc_selection (uint8) + zoom_spectrum_selection (uint8).
static void decode_cmd_spectrum_acq_params(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "spectrum_acq_params cmd < 8 bytes"); return; }
    w.key_double("start_freq_hz",          static_cast<double>(load_u16le(p + 0)) * 1e6);
    w.key_double("stop_freq_hz",           static_cast<double>(load_u16le(p + 2)) * 1e6);
    w.key_uint("rf_attenuation_db",        p[4]);
    w.key_uint("if_attenuation_db",        p[5]);
    w.key_uint("agc_selection",            p[6]);
    w.key_bool("zoom_spectrum_enabled",    p[7] != 0);
}

// 101/162 — AGC Enable Parameters command (4 bytes). Per ICD Table 61.
// agc_selection (uint8, 0=Disable/1=Enable) + 3 reserved.
static void decode_cmd_agc_enable(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "agc_enable cmd < 4 bytes"); return; }
    w.key_bool("agc_enabled", p[0] != 0);
}

// 101/176 — Set Burst Pulse Range command (24 bytes). Per ICD Table 65.
// max_pulse_ms (float) + min_pulse_ms (float) + threshold_dbm (float)
// + min_bw_khz (float) + max_bw_khz (float) + broadcast_band_enable (uint8) + 3 reserved.
static void decode_cmd_set_burst_pulse_range(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 24) { w.key_str("warning", "set_burst_pulse_range cmd < 24 bytes"); return; }
    w.key_double("max_pulse_length_ms",              static_cast<double>(load_f32le(p +  0)));
    w.key_double("min_pulse_length_ms",              static_cast<double>(load_f32le(p +  4)));
    w.key_double("burst_threshold_dbm",              static_cast<double>(load_f32le(p +  8)));
    w.key_double("burst_min_bandwidth_khz",          static_cast<double>(load_f32le(p + 12)));
    w.key_double("burst_max_bandwidth_khz",          static_cast<double>(load_f32le(p + 16)));
    w.key_bool("broadcast_band_detection_enabled",   p[20] != 0);
}

// 101/178 — Configure Signal Sidelobe Enable command (4 bytes). Per ICD Table 67.
// signal_sidelobe_enable (uint8, 0=Disable/1=Enable) + 3 reserved.
static void decode_cmd_signal_sidelobe_enable(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "signal_sidelobe_enable cmd < 4 bytes"); return; }
    w.key_bool("signal_sidelobe_enabled", p[0] != 0);
}

// 101/210 — VUHF Wideband Enable/Disable command (4 bytes). Per ICD Table 99.
// vuhf_wideband_enable (uint8, 0=Disable/1=Enable) + 3 reserved.
// NOTE: ICD lists response as group 111 unit 211 (unusual cross-group ACK — verify with vendor).
static void decode_cmd_vuhf_wideband_enable(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "vuhf_wideband_enable cmd < 4 bytes"); return; }
    w.key_bool("vuhf_wideband_enabled", p[0] != 0);
}

// 101/182 — Wideband Pulse Agility Enable/Disable command (4 bytes). Per ICD Table 69.
// wideband_pulse_agility_enable (uint8, 0=Disable/1=Enable) + 3 reserved.
static void decode_cmd_wideband_pulse_agility_enable(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "wideband_pulse_agility_enable cmd < 4 bytes"); return; }
    w.key_bool("wideband_pulse_agility_enabled", p[0] != 0);
}

// 101/174 — Multiple FH Split Enable command (4 bytes). Per ICD Table 54.
// toa_deinterleaving_split_enable (uint8) + 3 reserved. 0=Disable, 1=Enable.
static void decode_cmd_multifh_split_enable(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "multifh_split_enable cmd < 4 bytes"); return; }
    w.key_bool("toa_deinterleaving_split_enable", p[0] != 0);
}

// 101/164 — Zoom-Report Enable/Disable command (4 bytes). Per ICD Table 63.
// zoom_report_enable (uint8, 0=Disable/1=Enable) + 3 reserved.
static void decode_cmd_zoom_report_enable(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "zoom_report_enable cmd < 4 bytes"); return; }
    w.key_bool("zoom_report_enabled", p[0] != 0);
}

// 111/3 — Signal BITE command (16 bytes). Per ICD Table 81.
// channel_number (uint32) + bite_ref_selection (uint32) + bite_freq_mhz (float) + bite_atten_dbm (float).
static void decode_cmd_111_signal_bite(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) { w.key_str("warning", "111_signal_bite cmd < 16 bytes"); return; }
    w.key_uint("channel_number",      load_u32le(p +  0));
    w.key_uint("bite_ref_selection",  load_u32le(p +  4));
    w.key_double("bite_freq_hz",      static_cast<double>(load_f32le(p +  8)) * 1e6);
    w.key_double("bite_atten_dbm",    static_cast<double>(load_f32le(p + 12)));
}

// 111/21 — Signal BITE Test command (4 bytes).
// Per ICD Table 74: Band Selection (uint8) + 3 reserved. Enum 0–4.
// Bands: 0=1-2.5MHz, 1=2.5-5MHz, 2=5-9MHz, 3=9-16MHz, 4=16-30MHz.
static void decode_cmd_signal_bite(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "signal_bite cmd < 4 bytes"); return; }
    static const char* BAND_NAMES[] = {
        "1-2.5MHz", "2.5-5MHz", "5-9MHz", "9-16MHz", "16-30MHz"
    };
    uint8_t band = p[0];
    w.key_uint("band_selection", band);
    w.key_str("band_name", band < 5 ? BAND_NAMES[band] : "unknown");
}

// =============================================================================
// Group 101 jamming command decoders (added with ICD §3.2.2)
// =============================================================================

// 101/31 — Start Follow-On Jamming command (44 bytes).
// Per ICD Tables 115/112/113: S_TRACKING_WINDOW (16B) + S_TRACKING_INFO (28B).
// S_TRACKING_WINDOW @0:
//   @0  Hopper Start Freq (float, MHz)   @4 Hopper Stop Freq (float, MHz)
//   @8  Detection Start Freq (uint16, MHz) @10 Detection Stop Freq (uint16, MHz)
//   @12 Responsive/Immediate Selection (uint8) — 0=Responsive, 1=Immediate  @13-15 Reserved
// S_TRACKING_INFO @16:
//   @16 Hop Period ms (float)  @20 Inter Period ms (float)
//   @24 PA Power Level (uint32, Table 114)  @28 Modulation Type (uint32, Table 144)
//   @32 FM Deviation (uint32, Table 123)  @36 Exciter Mod Signal (uint32, Table 125)
//   @40 Hopper Power Level (float, dBm) — detected power of the hopper
static void decode_cmd_start_follow_on_jam(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 44) { w.key_str("warning", "start_follow_on_jam cmd < 44 bytes"); return; }
    static const unsigned PA_POWER_W[]   = {63, 125, 250, 500, 1000};
    static const double   FM_DEV_KHZ[]   = {1.5, 3.0, 5.0, 12.5, 25.0, 50.0, 100.0,
                                             150.0, 250.0, 500.0, 1000.0};
    static const char*    EXCITER_SIG[]  = {nullptr, nullptr,
                                             "single", "two_tone", "wgn", "pink_noise", "swept"};

    // S_TRACKING_WINDOW
    w.key_double("hopper_start_freq_hz",    static_cast<double>(load_f32le(p +  0)) * 1e6);
    w.key_double("hopper_stop_freq_hz",     static_cast<double>(load_f32le(p +  4)) * 1e6);
    w.key_double("detection_start_freq_hz", static_cast<double>(load_u16le(p +  8)) * 1e6);
    w.key_double("detection_stop_freq_hz",  static_cast<double>(load_u16le(p + 10)) * 1e6);
    w.key_uint("follow_on_selection",        p[12]);

    // S_TRACKING_INFO
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

// 101/33 — Stop Follow-On Jamming / Stop Responsive Sweep Jam: 0 bytes, shared unit ID.

// 101/63 — Set Auto/Manual Tracking Configuration command (4 bytes).
// Per ICD Table 119: Tracking Configuration (uint8, default 0) + 3 reserved.
static void decode_cmd_set_tracking_config(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "set_tracking_config cmd < 4 bytes"); return; }
    w.key_uint("tracking_config", p[0]);
}

// 101/73 — Start List (FF/Burst) Jamming command (1228 bytes).
// Per ICD Tables 121/122/127: S_LIST_JAMMING_INFO (24B) + S_LIST_JAMMING_FREQUENCIES (1204B).
// S_LIST_JAMMING_INFO @0:
//   @0 Start Freq (uint16, MHz)  @2 Stop Freq (uint16, MHz)
//   @4 Jam Freq Count (uint16, max 100)  @6 Occupancy Threshold (uint16)
//   @8 Jam Cycle Count (uint16)  @10 Look Through Cycle Count (uint16)
//   @12 FF/Burst Selection (uint32): 0=FF, 1=Burst
//   @16 Modulation Type (uint8)  @17 FM Deviation (uint8, Table 123)
//   @18 PA Power Level (uint8, Table 124)  @19 Exciter Mod Signal (uint8, Table 125)
//   @20 Jam Mode (uint8, Table 126): 0=TDM, 1=FDM  @21-23 Reserved
// S_LIST_JAMMING_FREQUENCIES @24 (fixed-size arrays, 100 slots each):
//   @24 Freq Count (uint16)  @26 Reserved (uint16)
//   @28    Frequencies (100×float, MHz)   → 400B
//   @428   Bandwidths  (100×float, kHz)   → 400B
//   @828   Thresholds  (100×float, dBm)   → 400B
static void decode_cmd_start_list_jam(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 28) { w.key_str("warning", "start_list_jam cmd < 28 bytes"); return; }
    static const unsigned PA_POWER_W[] = {63, 125, 250, 500, 1000};
    static const double   FM_DEV_KHZ[] = {1.5, 3.0, 5.0, 12.5, 25.0, 50.0, 100.0,
                                           150.0, 250.0, 500.0, 1000.0};
    static const std::string EXCITER_NAME[] = {"single", "two_tone", "white_gaussian_noise", "pink_noise", "swept"};
    // S_LIST_JAMMING_INFO
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
    w.key_str("exciter_modulating_signal_name",EXCITER_NAME[p[19]]);
    w.key_str("jam_mode", p[20] == 0 ? "tdm" : p[20] == 1 ? "fdm" : "unknown");
    // S_LIST_JAMMING_FREQUENCIES
    uint16_t freq_count = load_u16le(p + 24);
    w.key_uint("list_freq_count", freq_count);
    if (freq_count == 0 || freq_count > 100) return;

    const int FREQ_BASE = 28;   // 24 (info) + 4 (count + reserved)
    const int BW_BASE   = 428;  // FREQ_BASE + 100*4 (fixed-size freq slots)
    const int THR_BASE  = 828;  // BW_BASE + 100*4 (fixed-size bw slots)

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

// 101/75 — Stop List Jamming: 0 bytes, no decoder.

// 101/79 — Send ECM Reports command (4 bytes).
// Per ICD Table 138: ECM Report State (uint8, 1=Enable, 0=Disable) + 3 reserved.
static void decode_cmd_send_ecm_reports(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "send_ecm_reports cmd < 4 bytes"); return; }
    w.key_bool("ecm_reports_enabled", p[0] == 1);
}

// 101/92 — Start Responsive Sweep Jam command (208 bytes).
// Per ICD Tables 131/132/133/134:
//   @0   Detection Start Freq (double, 8B, MHz)  @8  Detection Stop Freq (double, 8B, MHz)
//   S_CMD_RES_SWEEP_JAM @16 (192B):
//   @16  Sweep Start Freq (double, 8B, MHz)  @24 Sweep Stop Freq (double, 8B, MHz)
//   @32  Sweep Rate (uint8)  @33 Modulation Type (uint8)
//   @34  Sweep Step (uint8, Table 132)  @35 Power Level (uint8, Table 133)
//   @36  Frequency Sweep Time (float, 4B, ms)
//   @40  Protected band start[10] (10×double, 80B, MHz)
//   @120 Protected band stop[10]  (10×double, 80B, MHz)
//   @200 Num protected bands (uint8)  @201 Freq Sweep Time Selection (uint8): 0=Auto, 1=Manual
//   @202 SSB Type (uint8)  @203 FM Deviation (uint8)
//   @204 Sweep Modulating Signal (uint8)  @205-207 Reserved
static void decode_cmd_start_responsive_sweep_jam(const uint8_t* p, int n, JsonWriter& w) {
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
        starts += "]"; 
        stops += "]";
        w.key_raw("protected_band_starts_hz", starts);
        w.key_raw("protected_band_stops_hz",  stops);
    }

    w.key_bool("freq_sweep_time_auto",      p[201] == 0);
    w.key_uint("ssb_type",                   p[202]);
    w.key_uint("fm_deviation",               p[203]);
    w.key_uint("sweep_modulating_signal",    p[204]);
}

// =============================================================================
// Group 106 command decoders (ECM jamming outputs — §3.2.2.2)
// Shared helper: decode S_JAM_CONFIGURATION block (20 bytes, ICD Table 140).
// Caller must guarantee at least 20 bytes at p.
// =============================================================================
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
    w.key_uint("num_frequencies",  p[1]);
    w.key_uint("range_selection",  p[2]);
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

// 106/1 — Start Immediate Jam command (28 bytes).
// Per ICD Table 152: S_JAM_CONFIGURATION (20B) + Single Frequency (double, 8B, MHz).
static void decode_cmd_start_immediate_jam(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 28) { 
        w.key_str("warning", "start_immediate_jam cmd < 28 bytes"); 
        return; 
    }
    decode_jam_config_block(p, w);
    w.key_double("jam_freq_hz", load_f64le(p + 20) * 1e6);
}

// 106/3 — Generate Multi Frequency TDM command (80 bytes).
// Per ICD Table 158: S_JAM_CONFIGURATION (20B) + S_TDM_CONFIGURATION (60B).
// S_TDM_CONFIGURATION @20:
//   @0  TDM Frequencies (6×double, 48B, MHz)
//   @48 Modulating Signal Hz (uint32)  @52 Freq ON Time µs (uint32)  @56 Freq OFF Time µs (uint32)
static void decode_cmd_generate_tdm(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 80) { 
        w.key_str("warning", "generate_tdm cmd < 80 bytes"); 
        return; 
    }
    decode_jam_config_block(p, w);
    const uint8_t* q = p + 20;
    std::string freqs = "[";
    for (int i = 0; i < 6; ++i) {
        if (i) freqs += ',';
        char tmp[32];
        std::snprintf(tmp, sizeof(tmp), "%.6g", load_f64le(q + i * 8) * 1e6);
        freqs += tmp;
    }
    freqs += "]";
    w.key_raw("tdm_frequencies_hz",   freqs);
    w.key_uint("modulating_signal_hz", load_u32le(q + 48));
    w.key_uint("freq_on_time_us",      load_u32le(q + 52));
    w.key_uint("freq_off_time_us",     load_u32le(q + 56));
}

// 106/5 — Generate Multi Carrier FDM command (68 bytes).
// Per ICD Table 160: S_JAM_CONFIGURATION (20B) + S_FDM_CONFIGURATION (48B).
// S_FDM_CONFIGURATION: FDM Frequencies (6×double, 48B, MHz).
static void decode_cmd_generate_fdm(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 68) { w.key_str("warning", "generate_fdm cmd < 68 bytes"); return; }
    decode_jam_config_block(p, w);
    const uint8_t* q = p + 20;
    std::string freqs = "[";
    for (int i = 0; i < 6; ++i) {
        if (i) freqs += ',';
        char tmp[32];
        std::snprintf(tmp, sizeof(tmp), "%.6g", load_f64le(q + i * 8) * 1e6);
        freqs += tmp;
    }
    freqs += "]";
    w.key_raw("fdm_frequencies_hz", freqs);
}

// 106/9 — Stop Immediate Jamming: 0 bytes command.

// 106/21 — Enable Power Amplifier command (4 bytes).
// Per ICD Table 174: Enable/Disable PA (uint8, 1=Enable, 0=Disable) + 3 reserved.
static void decode_cmd_enable_pa(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "enable_pa cmd < 4 bytes"); return; }
    w.key_bool("pa_enabled", p[0] == 1);
}

// 106/39 — Configure External Modulation Data command (8196 bytes).
// Per ICD Table 154: Audio Data Size (uint32) + External Audio Data (uint16 × 4096).
// The 8192-byte audio buffer is not decoded to JSON; size and presence are reported only.
static void decode_cmd_configure_ext_modulation(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "configure_ext_modulation cmd < 4 bytes"); return; }
    uint32_t audio_size = load_u32le(p + 0);
    w.key_uint("audio_data_size",     audio_size);
    w.key_bool("audio_buffer_present", n >= 4 + static_cast<int>(audio_size) * 2);
}

// 106/41 — Generate Sweep Frequency command (192 bytes).
// Per ICD Tables 162/163/164: identical layout to S_CMD_RES_SWEEP_JAM (101/92 @16)
// but occupies the full 192B frame (no 16B detection-freq prefix). Power uses Table 164
// (max 750W) — distinct from Table 133 (max 1000W) used by 101/92.
static void decode_cmd_generate_sweep(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 192) { w.key_str("warning", "generate_sweep cmd < 192 bytes"); return; }
    static const double   SWEEP_STEP_KHZ[] = {2.5, 5.0, 12.5, 25.0, 50.0, 100.0};
    static const unsigned POWER_W[]        = {63, 125, 250, 500, 750};

    w.key_double("sweep_start_freq_hz", load_f64le(p +  0) * 1e6);
    w.key_double("sweep_stop_freq_hz",  load_f64le(p +  8) * 1e6);
    w.key_uint("sweep_rate",            p[16]);
    w.key_uint("modulation_type",       p[17]);
    uint8_t step_idx = p[18];
    w.key_uint("sweep_step_index", step_idx);
    if (step_idx < 6) w.key_double("sweep_step_khz", SWEEP_STEP_KHZ[step_idx]);
    uint8_t pwr_idx = p[19];
    w.key_uint("power_level_index", pwr_idx);
    if (pwr_idx < 5) w.key_uint("power_level_w", POWER_W[pwr_idx]);
    w.key_double("freq_sweep_time_ms", static_cast<double>(load_f32le(p + 20)));

    uint8_t n_bands = p[184];
    w.key_uint("num_protected_bands", n_bands);
    if (n_bands > 0 && n_bands <= 10) {
        std::string starts = "[", stops = "[";
        for (uint8_t i = 0; i < n_bands; ++i) {
            if (i) { 
                starts += ','; 
                stops += ','; 
            }
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

    w.key_bool("freq_sweep_time_auto",   p[185] == 0);
    w.key_uint("ssb_type",               p[186]);
    w.key_uint("fm_deviation",           p[187]);
    w.key_uint("sweep_modulating_signal",p[188]);
}

// 106/45 — Enable PA and SDU command (4 bytes).
// Per ICD Table 172: Enable/Disable PA and SDU (uint8, 1=Enable, 0=Disable) + 3 reserved.
static void decode_cmd_enable_pa_sdu(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "enable_pa_sdu cmd < 4 bytes"); return; }
    w.key_bool("pa_sdu_enabled", p[0] == 1);
}

// 106/49 — Configure Programmable Exciter Modulating Signal command (2052 bytes).
// Per ICD Table 156: S_EXCITER_PROG_NOISE:
//   @0 Channel No (uint16, range 1-4)  @2 Data Size (uint16, max 1024)
//   @4 Data Values (uint16 × 1024, 2048B) — raw DSP waveform data, not decoded.
static void decode_cmd_configure_prog_exciter(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "configure_prog_exciter cmd < 4 bytes"); return; }
    w.key_uint("channel_no", load_u16le(p + 0));
    w.key_uint("data_size",  load_u16le(p + 2));
}

// 106/55 — Generate Comb Noise command (20 bytes).
// Per ICD Table 167: start/stop freq (double, 8B, MHz), step enum (Table 168), power (Table 164).
static void decode_cmd_generate_comb_noise(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 20) { 
        w.key_str("warning", "generate_comb_noise cmd < 20 bytes"); 
        return; 
    }
    static const double   STEP_KHZ[] = {1.5625, 3.125, 6.25, 12.5};
    static const unsigned POWER_W[]  = {63, 125, 250, 500, 750};

    w.key_double("start_freq_hz", load_f64le(p +  0) * 1e6);
    w.key_double("stop_freq_hz",  load_f64le(p +  8) * 1e6);
    uint8_t step_idx = p[16];
    w.key_uint("comb_step_index", step_idx);
    if (step_idx < 4) w.key_double("comb_step_khz", STEP_KHZ[step_idx]);
    uint8_t pwr_idx = p[17];
    w.key_uint("power_level_index", pwr_idx);
    if (pwr_idx < 5) w.key_uint("power_level_w", POWER_W[pwr_idx]);
}

// =============================================================================
// Group 109 command decoders
// =============================================================================

// 109/11 — Set Date and Time command (8 bytes).
// Per ICD Table 177: Day(uint8) Month(uint8) Year(uint16) Hour(uint8) Minute(uint8)
//   Seconds(uint8) Reserved(uint8).
static void decode_cmd_set_date_time(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { 
        w.key_str("warning", "set_date_time cmd < 8 bytes"); 
        return; 
    }
    w.key_uint("day",     p[0]);
    w.key_uint("month",   p[1]);
    w.key_uint("year",    load_u16le(p + 2));
    w.key_uint("hour",    p[4]);
    w.key_uint("minute",  p[5]);
    w.key_uint("seconds", p[6]);
}

// 109/15 — Auto Threshold Value: 0-byte command.

// 109/17 — Acquire Hopper Channelization Data: 0-byte command.
// Response 109/18 has identical structure to 111/16 PDW Channelization — reuses decode_pdw_channelization.

// =============================================================================
// Group 112 command decoders (ASU/SDU and PA control — §3.2.2.6)
// =============================================================================

// 112/1 — ASU/SDU Configuration command (8 bytes).
// Per ICD Table 192: ASU SDU Signal Name (uint32) + ASU SDU Signal Value (uint32).
static void decode_cmd_asu_sdu_config(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "asu_sdu_config cmd < 8 bytes"); return; }
    w.key_uint("signal_name",  load_u32le(p + 0));
    w.key_uint("signal_value", load_u32le(p + 4));
}

// 112/37 — PA Frequency Configuration command (12 bytes).
// start_freq (float) + stop_freq (float) + center_freq (float). Frequencies in MHz per ICD.
static void decode_cmd_pa_freq_config(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 12) { w.key_str("warning", "pa_freq_config cmd < 12 bytes"); return; }
    w.key_double("start_freq_hz",  static_cast<double>(load_f32le(p + 0)) * 1e6);
    w.key_double("stop_freq_hz",   static_cast<double>(load_f32le(p + 4)) * 1e6);
    w.key_double("center_freq_hz", static_cast<double>(load_f32le(p + 8)) * 1e6);
}

// 112/3 — TRSDU Receiver Line Status: 0-byte command.

// 112/5 — Start/Stop Detection command (8 bytes). Per ICD Table 101.
// start_stop(u8@0): 0=Stop, 1=Start.
// detection_mode(u8@1): 0=Directed, 1=Simulation, 2=Offline, 3=Auto Scan.
// file_logging(u8@2): 0=Disable, 1=Enable.
// mission_plan_index(u8@3): 0=No DB, 1–9=loaded DB index.
static void decode_cmd_start_stop_detection(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "start_stop_detection payload < 4 bytes"); return; }
    w.key_uint("start_stop",          p[0]);
    w.key_uint("detection_mode",      p[1]);
    w.key_uint("file_logging",        p[2]);
    w.key_uint("mission_plan_index",  p[3]);
}
// 112/6 — Start/Stop Detection response: 0 bytes (pure ACK).

// =============================================================================
// MRX command decoders — Groups 3, 4, 5, 6, 7 (§3.3.x, port 10015)
// =============================================================================

// 3/19 — Write Channel Information command (4 bytes). Per ICD Table 222.
// channel_number (uint16) + channel_status: 1=Open, 0=Closed (uint16).
static void decode_cmd_mrx_write_channel(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "mrx_write_channel cmd < 4 bytes"); return; }
    w.key_uint("channel_number", load_u16le(p + 0));
    uint16_t status = load_u16le(p + 2);
    w.key_uint("channel_status", status);
    w.key_str("channel_op", status == 1 ? "open" : "close");
}

// Generic MRX 4-byte command: channel_number (uint16) + reserved (uint16).
// Used by: 4/7 Audio Acq, 4/9 Audio Start, 4/11 Audio Stop, 4/15 Audio FIFO Reset,
//          4/25 Stop IQ Log, 4/35 Stop IQ Stream, 4/57 Stop Memory Scan,
//          4/67 Stop IQ Optical, 7/13 IQ Socket Connect, 7/15 IQ Socket Close.
static void decode_cmd_mrx_ch_only(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) return;
    w.key_uint("channel_number", load_u16le(p + 0));
}

// Generic MRX 4-byte command: channel_number (uint16) + bandwidth/param (uint16).
// Used by: 4/33 Start IQ Stream, 4/43 DDC FFT, 4/65 Start IQ Optical,
//          4/69 Optical Port Status, 4/71 Optical IP.
static void decode_cmd_mrx_ch_bw(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) return;
    w.key_uint("channel_number",      load_u16le(p + 0));
    w.key_uint("bandwidth_selection", load_u16le(p + 2));
}

// 4/5 — Set Threshold command (8 bytes). Per ICD Table 232.
// channel (uint16) + reserved (uint16) + threshold (float, dBm, range 0 to -150).
static void decode_cmd_mrx_set_threshold(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "mrx_set_threshold cmd < 8 bytes"); return; }
    w.key_uint("channel_number", load_u16le(p + 0));
    w.key_double("threshold_dbm", static_cast<double>(load_f32le(p + 4)));
}

// 4/17 — Demodulation and Bandwidth Selection command (8 bytes). Per ICD Table 234.
// channel (uint16) + demod_selection (uint16) + bw_selection (uint16) + lsb_usb_offset_khz (uint16).
// Demod: 0=CW, 1=AM, 2=FM, 3=LSB, 4=USB.
static void decode_cmd_mrx_demod_bw(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "mrx_demod_bw cmd < 8 bytes"); return; }
    static const char* DEMOD_NAMES[] = {"cw", "am", "fm", "lsb", "usb"};
    w.key_uint("channel_number",      load_u16le(p + 0));
    uint16_t demod = load_u16le(p + 2);
    w.key_uint("demod_selection",     demod);
    w.key_str("demod_name",           demod < 5 ? DEMOD_NAMES[demod] : "unknown");
    w.key_uint("bandwidth_selection", load_u16le(p + 4));
    w.key_uint("lsb_usb_offset_khz", load_u16le(p + 6));
}

// 4/23 — Start IQ Data Logging in MSATA command (24 bytes). Per ICD Table 250.
// channel (uint16) + iq_bw (uint16) + center_freq_mhz (float) +
//   day/month/year/hour/minute/second (6× uint16) + reserved (4B).
static void decode_cmd_mrx_start_iq_log(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 24) { w.key_str("warning", "mrx_start_iq_log cmd < 24 bytes"); return; }
    w.key_uint("channel_number",      load_u16le(p + 0));
    w.key_uint("bandwidth_selection", load_u16le(p + 2));
    w.key_double("center_freq_hz",    static_cast<double>(load_f32le(p + 4)) * 1e6);
    char dt[24];
    std::snprintf(dt, sizeof(dt), "%04u-%02u-%02u %02u:%02u:%02u",
                  load_u16le(p + 12),  // year
                  load_u16le(p + 10),  // month
                  load_u16le(p +  8),  // day
                  load_u16le(p + 14),  // hour
                  load_u16le(p + 16),  // minute
                  load_u16le(p + 18)); // second
    w.key_str("datetime", dt);
}

// 4/39, 4/41 — Configure / Read Memory Scan command (variable). Per ICD Tables 236, 238.
// channel (uint16) + reserved (uint16) + freq_count (uint32) + S_CMD_MEMORY_SCAN_LIST × count.
// S_CMD_MEMORY_SCAN_LIST (12B): Frequency (double, MHz→Hz) + Bandwidth (uint16) + Reserved (uint16).
static void decode_cmd_mrx_mem_scan(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { 
        w.key_str("warning", "mrx_mem_scan cmd < 8 bytes"); 
        return; 
    }
    w.key_uint("channel_number",  load_u16le(p + 0));
    uint32_t count = load_u32le(p + 4);
    w.key_uint("frequency_count", count);

    const int ELEM = 12;
    int off = 8;
    std::string arr = "[";
    uint32_t emitted = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (off + ELEM > n) break;
        const uint8_t* e = p + off;
        JsonWriter h;
        h.key_double("freq_hz",           load_f64le(e + 0) * 1e6);
        h.key_uint("bandwidth_selection", load_u16le(e + 8));
        if (emitted++) arr += ',';
        arr += h.str();
        off += ELEM;
    }
    arr += "]";
    w.key_raw("frequency_list", arr);
}

// 4/53 — Channel Engagement command (12 bytes). Per ICD Table 281.
// channel (uint16) + reserved (uint16) + center_freq_mhz (double, 8B).
static void decode_cmd_channel_engagement(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 12) { w.key_str("warning", "channel_engagement cmd < 12 bytes"); return; }
    w.key_uint("channel_number", load_u16le(p + 0));
    w.key_double("center_freq_hz", load_f64le(p + 4) * 1e6);
}

// 5/1 — Set Center Frequency command (12 bytes). Per ICD Table 254.
// channel (uint16) + bite_antenna_selection (uint16) + center_freq_mhz (double, 8B).
static void decode_cmd_mrx_center_freq(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 12) { w.key_str("warning", "mrx_center_freq cmd < 12 bytes"); return; }
    w.key_uint("channel_number",         load_u16le(p + 0));
    w.key_uint("bite_antenna_selection", load_u16le(p + 2));
    w.key_double("center_freq_hz",       load_f64le(p + 4) * 1e6);
}

// 5/3 — Attenuation Selection command (16 bytes). Per ICD Table 256.
// channel (uint16) + reserved (uint16) + rf_atten (float) + if_atten (float) + cal_value (float).
static void decode_cmd_mrx_attenuation(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) { w.key_str("warning", "mrx_attenuation cmd < 16 bytes"); return; }
    w.key_uint("channel_number",  load_u16le(p +  0));
    w.key_double("rf_atten_db",   static_cast<double>(load_f32le(p +  4)));
    w.key_double("if_atten_db",   static_cast<double>(load_f32le(p +  8)));
    // w.key_double("cal_value_db",  static_cast<double>(load_f32le(p + 12)));
}

// 6/7 — Configure FH Monitoring for FH IQ Streaming command (52 bytes). Per ICD Table 290.
static void decode_cmd_fh_monitoring(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 52) { w.key_str("warning", "fh_monitoring cmd < 52 bytes"); return; }
    w.key_uint("channel_number",            load_u16le(p +  0));
    w.key_uint("resolution_bandwidth",      p[2]);
    w.key_uint("integration_time_index",    p[3]);
    w.key_double("fh_start_freq_hz",        load_f64le(p +  4) * 1e6);
    w.key_double("fh_stop_freq_hz",         load_f64le(p + 12) * 1e6);
    w.key_double("hop_period_s",            static_cast<double>(load_f32le(p + 20)));
    w.key_double("inter_hop_period_s",      static_cast<double>(load_f32le(p + 24)));
    w.key_double("power_level_dbm",         static_cast<double>(load_f32le(p + 28)));
    w.key_uint("band_start_freq",           load_u32le(p + 40));
    w.key_uint("band_stop_freq",            load_u32le(p + 44));
    w.key_uint("fh_80mhz_stream_selection", p[48]);
    w.key_uint("fh_enable",                 p[49]);
    w.key_uint("header_selection_enable",   p[50]);
}

// 6/9 — GO2Monitor Connection Establishment command (260 bytes). Per ICD Table 258.
// ip_address (char[128]) + port_number (char[128]) + channel (uint16) + reserved (uint16).
static void decode_cmd_go2mon_connect(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 260) { w.key_str("warning", "go2mon_connect cmd < 260 bytes"); return; }
    w.key_str("ip_address",    std::string(reinterpret_cast<const char*>(p +   0), strnlen(reinterpret_cast<const char*>(p +   0), 128)));
    w.key_str("port_number",   std::string(reinterpret_cast<const char*>(p + 128), strnlen(reinterpret_cast<const char*>(p + 128), 128)));
    w.key_uint("channel_number", load_u16le(p + 256));
}

// 6/13 — Start GO2Monitor Transmission command (144 bytes). Per ICD Table 260.
// channel (uint16) + bw (uint16) + reserved×2 (uint16) + center_freq_mhz (double, 8B) + date (char[128]).
static void decode_cmd_go2mon_start(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 144) { w.key_str("warning", "go2mon_start cmd < 144 bytes"); return; }
    w.key_uint("channel_number",      load_u16le(p + 0));
    w.key_uint("bandwidth_selection", load_u16le(p + 2));
    w.key_double("center_freq_hz",    load_f64le(p + 8) * 1e6);
    w.key_str("date_str", std::string(reinterpret_cast<const char*>(p + 16), strnlen(reinterpret_cast<const char*>(p + 16), 128)));
}

// 7/1 — Signal BITE command (12 bytes). Per ICD Table 273.
// bite_freq_mhz (double, 8B) + channel (uint16) + reserved (uint16).
static void decode_cmd_mrx_signal_bite(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 12) { w.key_str("warning", "mrx_signal_bite cmd < 12 bytes"); return; }
    w.key_double("bite_freq_hz",  load_f64le(p + 0) * 1e6);
    w.key_uint("channel_number",  load_u16le(p + 8));
}

// 7/3 — BITE/ANTENNA Switch Selection (4 bytes). Per ICD Table 275.
// bite_antenna_selection (uint16) + channel (uint16). 0=Antenna, 1=BITE.
static void decode_cmd_mrx_bite_ant(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) return;
    w.key_uint("bite_antenna_selection", load_u16le(p + 0));
    w.key_uint("channel_number",         load_u16le(p + 2));
}

// 7/5 — Internal/External Reference Source Selection (4 bytes). Per ICD Table 277.
// source_selection (uint16) + channel (uint16). 0=Internal (OCXO), 1=External.
static void decode_cmd_mrx_ref_source(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) return;
    w.key_uint("ref_source_selection", load_u16le(p + 0));
    w.key_uint("channel_number",       load_u16le(p + 2));
}

// 7/9 — AFC Enable/Disable (4 bytes). Per ICD Table 291. 0=Disable, 1=Enable.
static void decode_cmd_mrx_afc(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) return;
    w.key_bool("afc_enabled",    load_u16le(p + 0) != 0);
    w.key_uint("channel_number", load_u16le(p + 2));
}

// 7/11 — RF Squelch ON/OFF (4 bytes). Per ICD Table 279. 0=OFF, 1=ON.
static void decode_cmd_mrx_rf_squelch(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) return;
    w.key_bool("rf_squelch_enabled", load_u16le(p + 0) != 0);
    w.key_uint("channel_number",     load_u16le(p + 2));
}

// 7/17 — Spectrum Averaging Count (8 bytes). Per ICD Table 285.
// is_enabled (uint16) + avg_count (uint16) + channel (uint16) + reserved (uint16).
// avg_count: 1-4. is_enabled: 1=Enable, 0=Disable.
static void decode_cmd_mrx_spectrum_avg(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "mrx_spectrum_avg cmd < 8 bytes"); return; }
    w.key_bool("averaging_enabled", load_u16le(p + 0) != 0);
    w.key_uint("average_count",     load_u16le(p + 2));
    w.key_uint("channel_number",    load_u16le(p + 4));
}

// 7/19 — Smart RF AGC (4 bytes). Per ICD Table 287. 0=Disable, 1=Enable.
static void decode_cmd_mrx_rf_agc(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) return;
    w.key_bool("rf_agc_enabled", load_u16le(p + 0) != 0);
    w.key_uint("channel_number", load_u16le(p + 2));
}

// 7/21 — Audio Squelch ON/OFF (8 bytes). Per ICD Table 289.
// squelch_selection (uint16) + channel (uint16) + threshold (float, dBm). 0=OFF, 1=ON.
static void decode_cmd_mrx_audio_squelch(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "mrx_audio_squelch cmd < 8 bytes"); return; }
    w.key_bool("audio_squelch_enabled", load_u16le(p + 0) != 0);
    w.key_uint("channel_number",        load_u16le(p + 2));
    w.key_double("threshold_dbm",       static_cast<double>(load_f32le(p + 4)));
}

// 7/23 — Set Date and Time command (8 bytes). Per ICD Table 315.
// day (uint8) + month (uint8) + year (uint16) + hour (uint8) + minute (uint8) + seconds (uint8) + reserved (uint8).
static void decode_cmd_set_datetime(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "set_datetime cmd < 8 bytes"); return; }
    w.key_uint("day",     p[0]);
    w.key_uint("month",   p[1]);
    w.key_uint("year",    load_u16le(p + 2));
    w.key_uint("hour",    p[4]);
    w.key_uint("minute",  p[5]);
    w.key_uint("seconds", p[6]);
}

// 111/5 — Reference Input Selection command (4 bytes).
// Per ICD Table 77: Reference Selection (uint16) + Reserved (uint16). 0=INTERNAL, 1=EXTERNAL.
static void decode_cmd_reference_input(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "reference_input cmd < 4 bytes"); return; }
    uint16_t sel = load_u16le(p + 0);
    w.key_uint("reference_selection", sel);
    w.key_str("reference_name", sel == 0 ? "internal" : sel == 1 ? "external" : "unknown");
}

// 111/9 — Send Protected Scan List command (variable).
// Per ICD Table 81: count (uint16) + reserved (uint16) + S_PROTECTED_BAND_LIST × count (8B each).
// Per-entry: start_freq_mhz (float) + stop_freq_mhz (float). Max 1000 entries.
static void decode_cmd_send_protected_scan_list(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "send_protected_scan_list cmd < 4 bytes"); return; }
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

// 111/13 — Protected Scan Enable/Disable command (4 bytes).
// Per ICD Table 79: enable_disable (uint16) + reserved (uint16). 1=Enable, 0=Disable.
static void decode_cmd_protected_scan_enable(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { 
        w.key_str("warning", "protected_scan_enable cmd < 4 bytes"); 
        return; 
    }
    w.key_bool("protected_scan_enabled", load_u16le(p + 0) == 1);
}

// 111/17 — FH Split band Enable/Disable command (4 bytes).
// Per ICD Table 85: enable_disable (uint8) + reserved (3×uint8). 1=Enable, 0=Disable.
static void decode_cmd_fh_splitband_enable(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "fh_splitband_enable cmd < 4 bytes"); return; }
    w.key_bool("fh_splitband_enabled", p[0] == 1);
}

// 111/19 — Send FH Split band Frequency command (4 bytes).
// Per ICD Table 87: split band frequency (float, MHz).
static void decode_cmd_send_fh_splitband_freq(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "fh_splitband_freq cmd < 4 bytes"); return; }
    w.key_double("splitband_freq_hz", static_cast<double>(load_f32le(p + 0)) * 1e6);
}

// 111/23 — Protected-Band Spectrum Enable/Disable command (4 bytes).
// Per ICD Table 89: enable_disable (uint16) + reserved (uint16). 1=Enable, 0=Disable.
static void decode_cmd_protected_spectrum_enable(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "protected_spectrum_enable cmd < 4 bytes"); return; }
    w.key_bool("protected_spectrum_enabled", load_u16le(p + 0) == 1);
}

// 111/29 — Auto Threshold Enable/Disable command (4 bytes).
// Per ICD Table 95: enable_disable (uint8) + reserved (3×uint8). 1=Enable, 0=Disable.
static void decode_cmd_auto_threshold_enable(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "auto_threshold_enable cmd < 4 bytes"); return; }
    w.key_bool("auto_threshold_enabled", p[0] == 1);
}

// 111/31 — Hopper Channelization Enable/Disable command (16 bytes).
// Per ICD Table 97: start_freq (float, MHz) + stop_freq (float, MHz) +
//   hop_period (float, ms) + enable (uint8) + reserved (3×uint8). 1=Enable, 0=Disable.
static void decode_cmd_hopper_channelization_enable(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) { w.key_str("warning", "hopper_channelization_enable cmd < 16 bytes"); return; }
    w.key_double("start_freq_hz",         static_cast<double>(load_f32le(p + 0)) * 1e6);
    w.key_double("stop_freq_hz",          static_cast<double>(load_f32le(p + 4)) * 1e6);
    w.key_double("hop_period_ms",         static_cast<double>(load_f32le(p + 8)));
    w.key_bool("channelization_enabled",  p[12] == 1);
}

// =============================================================================
// Group 101 response decoders
// =============================================================================

// 101/40 — FH Detection response: HopperCount(u16) Reserved(u16) S_HOPPER_DATA[count]
// VU S_HOPPER_DATA = 60 bytes (HF = 40 bytes — no extension):
//   @0  number(u32)  @4  minFreqMHz(f32)   @8  maxFreqMHz(f32)
//   @12 pulseLenMs(f32)  @16 interHopMs(f32)  @20 detected(u32)
//   @24 TOA(4xu8)    @28 powerDbm(f32)     @32 active(u16)
//   @34 reserved(u16)   @36 snr(f32)
//   — VU extension bytes @40-@59 (detected freq range, BW, hop rate, confidence):
//   @40 minFreqDetMHz(f32)  @44 maxFreqDetMHz(f32)  @48 signalBwKhz(f32)
//   @52 hopRate(f32)        @56 confidence(f32)
// Frequencies stored in MHz by hardware; SI output is Hz (× 1e6).
// VU band: 30 MHz – 6000 MHz.
static void decode_fh_detection(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "fh payload < 4 bytes"); return; }
    uint16_t count = load_u16le(p + 0);
    w.key_uint("hopper_count", count);

    std::string arr = "[";
    int off = 4;
    const int ELEM = 60;
    uint16_t emitted = 0;
    for (uint16_t i = 0; i < count; ++i) {
        if (off + ELEM > n) break;
        const uint8_t* e = p + off;
        JsonWriter h;
        h.key_uint("hopper_number",        load_u32le(e +  0));
        h.key_double("min_freq_hz",  static_cast<double>(load_f32le(e +  4)) * 1e6);
        h.key_double("max_freq_hz",  static_cast<double>(load_f32le(e +  8)) * 1e6);
        h.key_double("pulse_length_s",     static_cast<double>(load_f32le(e + 12)) / 1e3);
        h.key_double("inter_hop_period_s", static_cast<double>(load_f32le(e + 16)) / 1e3);
        h.key_uint("detected_count",       load_u32le(e + 20));
        char toa[16];
        std::snprintf(toa, sizeof(toa), "%02u:%02u:%02u", e[24], e[25], e[26]);
        h.key_str("toa", toa);
        h.key_double("power_dbm",  static_cast<double>(load_f32le(e + 28)));
        h.key_bool("active",       load_u16le(e + 32) == 1);
        h.key_double("snr_db",     static_cast<double>(load_f32le(e + 36)));
        // VU-specific extension bytes 40-59
        h.key_double("min_freq_detected_hz", static_cast<double>(load_f32le(e + 40)) * 1e6);
        h.key_double("max_freq_detected_hz", static_cast<double>(load_f32le(e + 44)) * 1e6);
        h.key_double("signal_bw_khz",        static_cast<double>(load_f32le(e + 48)));
        h.key_double("hop_rate",             static_cast<double>(load_f32le(e + 52)));
        h.key_double("confidence",           static_cast<double>(load_f32le(e + 56)));
        if (emitted++) arr += ',';
        arr += h.str();
        off += ELEM;
    }
    arr += "]";
    w.key_raw("hoppers", arr);
}

// Shared helper: decode one S_DETECTED_FIXED_FREQUENCY entry (36 bytes).
// Used by decode_ff_detection (101/70) and decode_stop_scan_speed (101/88).
// Layout per ICD Tables 54 / 62:
//   @0  Freq MHz (float)  @4 Current Power dBm (float)  @8 Active Count (uint32)
//   @12 Min Power dBm (float)  @16 Max Power dBm (float)
//   @20 TOA H:M:S:reserved (4B)  @24 Duration H:M:S:reserved (4B)
//   @28 Freq Active (uint16)  @30 Reserved (uint16)  @32 SNR (float)
static void decode_ff_entry(const uint8_t* e, JsonWriter& f) {
    f.key_double("freq_hz",          static_cast<double>(load_f32le(e +  0)) * 1e6);
    f.key_double("current_power_dbm",static_cast<double>(load_f32le(e +  4)));
    f.key_uint("active_count",        load_u32le(e +  8));
    f.key_double("min_power_dbm",    static_cast<double>(load_f32le(e + 12)));
    f.key_double("max_power_dbm",    static_cast<double>(load_f32le(e + 16)));
    char toa[16];
    std::snprintf(toa, sizeof(toa), "%02u:%02u:%02u", e[20], e[21], e[22]);
    f.key_str("toa", toa);
    char dur[16];
    std::snprintf(dur, sizeof(dur), "%02u:%02u:%02u", e[24], e[25], e[26]);
    f.key_str("duration", dur);
    f.key_bool("freq_active", load_u16le(e + 28) == 1);
    f.key_double("snr_db",    static_cast<double>(load_f32le(e + 32)));
}

// 101/44 — Wideband FFT Data response (4 + 1600*4 + 4 = 6408 bytes).
// Per ICD Table 50: FFT Bin Count (uint32) + 1600 power floats + Scan Speed (uint32).
static void decode_wideband_fft_1600(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "wideband_fft payload < 8 bytes"); return; }
    uint32_t bin_count = load_u32le(p + 0);
    w.key_uint("fft_bin_count", bin_count);

    const int FFT_BYTES = 1600 * 4;
    if (n < 4 + FFT_BYTES + 4) w.key_str("warning", "wideband_fft payload truncated");

    uint32_t emit = (bin_count < 1600) ? bin_count : 1600;
    int max_from_buf = (n - 4) / 4;
    if (static_cast<int>(emit) > max_from_buf) emit = static_cast<uint32_t>(max_from_buf);

    std::string arr = "[";
    for (uint32_t i = 0; i < emit; ++i) {
        if (i) arr += ',';
        char tmp[32];
        std::snprintf(tmp, sizeof(tmp), "%.2f",
            static_cast<double>(load_f32le(p + 4 + i * 4)));
        arr += tmp;
    }
    arr += "]";
    w.key_raw("power_dbm", arr);

    int scan_off = 4 + FFT_BYTES;
    if (scan_off + 4 <= n) {
        w.key_uint("scan_speed", load_u32le(p + scan_off));
    }
}

// 101/70 — FF Detection response.
// Per ICD Table 54 (last/authoritative table):
//   Part 1: S_RES_WIDEBAND_FFT_DATA — bin count (uint32) + 1600 floats + scan speed (uint32) = 6408 B
//   Part 2: S_FIXED_FREQUENCIES — ff count (uint32) + S_DETECTED_FIXED_FREQUENCY × count (36B each)
static void decode_ff_detection(const uint8_t* p, int n, JsonWriter& w) {
    const int FFT_BLOCK = 4 + 1600 * 4 + 4;
    if (n < FFT_BLOCK) {
        w.key_str("warning", "ff_detection payload too short for wideband FFT block");
        return;
    }

    // Part 1
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

    // Part 2
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
        decode_ff_entry(ff_p + off, f);
        if (emitted++) ff_arr += ',';
        ff_arr += f.str();
        off += FF_ELEM;
    }
    ff_arr += "]";
    w.key_raw("fixed_frequencies", ff_arr);
}

// 101/84 — Burst Detection response.
// Per ICD Table 58 (last/authoritative — S_RES_VUHF_BURST_DETECTION):
//   Detected Burst Count (uint32) + S_DETECTED_BURST_FREQUENCY × count (24B each).
// S_DETECTED_BURST_FREQUENCY layout:
//   @0  Freq MHz (float)  @4 Current Power dBm (float)  @8 Pulse Length (float, ms)
//   @12 Active Count (uint32)  @16 TOA H:M:S:reserved (4B)  @20 SNR (float)
static void decode_burst_detection(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "burst_detection payload < 4 bytes"); return; }
    uint32_t burst_count = load_u32le(p + 0);
    w.key_uint("burst_count", burst_count);

    const int ELEM = 24;
    std::string arr = "[";
    int off = 4;
    uint32_t emitted = 0;
    for (uint32_t i = 0; i < burst_count; ++i) {
        if (off + ELEM > n) break;
        const uint8_t* e = p + off;
        JsonWriter b;
        b.key_double("freq_hz",          static_cast<double>(load_f32le(e +  0)) * 1e6);
        b.key_double("current_power_dbm",static_cast<double>(load_f32le(e +  4)));
        b.key_double("pulse_length_ms",  static_cast<double>(load_f32le(e +  8)));
        b.key_uint("active_count",        load_u32le(e + 12));
        char toa[16];
        std::snprintf(toa, sizeof(toa), "%02u:%02u:%02u", e[16], e[17], e[18]);
        b.key_str("toa", toa);
        b.key_double("snr_db", static_cast<double>(load_f32le(e + 20)));
        if (emitted++) arr += ',';
        arr += b.str();
        off += ELEM;
    }
    arr += "]";
    w.key_raw("bursts", arr);
}

// 101/88 — Stop Scan Speed response.
// Per ICD Table 62 (last/authoritative — S_RES_VUHF_STOP_SCAN_SPEED):
//   S_FIXED_FREQUENCIES: Detected FF Count (uint32) + S_DETECTED_FIXED_FREQUENCY × count (36B each).
//   Same 36-byte struct as FF detection (decode_ff_entry).
static void decode_stop_scan_speed(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "stop_scan_speed payload < 4 bytes"); return; }
    uint32_t ff_count = load_u32le(p + 0);
    w.key_uint("ff_count", ff_count);

    const int FF_ELEM = 36;
    std::string arr = "[";
    int off = 4;
    uint32_t emitted = 0;
    for (uint32_t i = 0; i < ff_count; ++i) {
        if (off + FF_ELEM > n) break;
        JsonWriter f;
        decode_ff_entry(p + off, f);
        if (emitted++) arr += ',';
        arr += f.str();
        off += FF_ELEM;
    }
    arr += "]";
    w.key_raw("fixed_frequencies", arr);
}

// 101/95 — Zoom Band FFT Data response (1600 * 4 = 6400 bytes).
// Per ICD Table 73: Wide Band FFT Data (float × 1600). No bin count or scan speed field.
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

// 101/205 — Get SRx MRx Status response (8 bytes). Per ICD Table 76.
// srx_tuned (uint8) + mrx_tuned (uint8) + reserved (uint16) + tuned_center_freq (uint16)
// + memory_scan_tuned (uint8) + reserved (uint8).
static void decode_srx_mrx_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "srx_mrx_status payload < 8 bytes"); return; }
    w.key_uint("srx_tuned_status",          p[0]);
    w.key_uint("mrx_tuned_status",          p[1]);
    w.key_uint("tuned_center_freq",         load_u16le(p + 4));
    w.key_uint("memory_scan_tuned_status",  p[6]);
}

// 111/4 — Signal BITE response (12 bytes). Per ICD Table 82.
// bite_freq_mhz (float) + bite_power_dbm (float) + bite_result (uint16, 1=PASS/0=FAIL) + reserved (uint16).
static void decode_111_signal_bite_resp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 12) { w.key_str("warning", "111_signal_bite_resp payload < 12 bytes"); return; }
    w.key_double("bite_freq_hz",   static_cast<double>(load_f32le(p + 0)) * 1e6);
    w.key_double("bite_power_dbm", static_cast<double>(load_f32le(p + 4)));
    w.key_uint("bite_result",      load_u16le(p + 8));
    w.key_str("bite_result_name",  load_u16le(p + 8) == 1 ? "PASS" : "FAIL");
}

// 111/22 — Signal BITE Test response (16 bytes).
// Per ICD Table 75:
//   @0  Observed BITE Frequency (double, MHz) → emitted in Hz
//   @8  Observed BITE Power Level (float, dBm)  — expected -45 ± 3 dBm
//   @12 Observed BITE Result (uint16) — 1=PASS, 0=FAIL
//   @14 Reserved (uint16)
static void decode_signal_bite(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) { w.key_str("warning", "signal_bite payload < 16 bytes"); return; }
    w.key_double("bite_freq_hz",   load_f64le(p + 0) * 1e6);
    w.key_double("bite_power_dbm", static_cast<double>(load_f32le(p + 8)));
    w.key_uint("bite_result",      load_u16le(p + 12));
}

// 111/8 — Module Health Status response (4 bytes).
// Per ICD Table 76: DRX Health (uint8) + RF Tuner Health (uint8) + Reserved (2×uint8).
// 1=PASS, 0=FAIL.
static void decode_module_health(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "module_health payload < 4 bytes"); return; }
    w.key_uint("drx_health",      p[0]);
    w.key_uint("rf_tuner_health", p[1]);
}

// 111/16 — PDW Channelization Data response (variable).
// Per ICD Table 84 (last/authoritative — S_RES_VUHF_FH_CHANNELIZATION):
//   count (uint32) + S_FH_CHANNELIZATION_TOA (4B: H,M,S,reserved) +
//   S_FH_CHANNELIZATION × count (28B each, max 8*1024*2).
// S_FH_CHANNELIZATION layout:
//   @0  TOI             (double, 8B)   — Time of Intercept
//   @8  Frequency Index (float, 4B)   — in MHz
//   @12 Pulse Length    (float, 4B)   — in ms
//   @16 Power Level     (float, 4B)   — in dBm
//   @20 Bandwidth       (uint32, 4B)
//   @24 Frequency Band  (uint32, 4B)
static void decode_pdw_channelization(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "pdw_channelization payload < 8 bytes"); return; }
    uint32_t count = load_u32le(p + 0);
    w.key_uint("channelization_count", count);

    char toa[16];
    std::snprintf(toa, sizeof(toa), "%02u:%02u:%02u", p[4], p[5], p[6]);
    w.key_str("toa", toa);

    const int ELEM = 28;
    int off = 8; // 4B count + 4B TOA struct
    std::string arr = "[";
    uint32_t emitted = 0;
    for (uint32_t i = 0; i < count; ++i) {
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
        off += ELEM;
    }
    arr += "]";
    w.key_raw("channelization_data", arr);
}

// 111/26 — Get Storage Details response (20 bytes).
// Per ICD Table 92:
//   @0  Disk Space 1       (uint8)
//   @1  Disk Space 2       (uint8)
//   @2  Disk Space 3       (uint8)
//   @3  Reserved           (uint8)
//   @4  Available Disk Space (double, 8B)
//   @12 Total Disk Space    (double, 8B)
static void decode_storage_details(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 20) { w.key_str("warning", "storage_details payload < 20 bytes"); return; }
    w.key_uint("disk_space_1",           p[0]);
    w.key_uint("disk_space_2",           p[1]);
    w.key_uint("disk_space_3",           p[2]);
    w.key_double("available_disk_space", load_f64le(p +  4));
    w.key_double("total_disk_space",     load_f64le(p + 12));
}

// 111/28 — Read Protected Band List response (variable).
// Per ICD Table 94: same structure as Send Protected Scan List (111/9 cmd):
//   count (uint16) + reserved (uint16) + S_PROTECTED_BAND_LIST × count (8B each).
// Per-entry: start_freq_mhz (float) + stop_freq_mhz (float).
static void decode_protected_band_list(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "protected_band_list payload < 4 bytes"); return; }
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

// =============================================================================
// Group 106 response decoders
// =============================================================================

// 106/10 — Stop Immediate Jamming response (4 bytes).
// Per ICD Table 171: Exciter Retry Count (uint8) + 3 reserved.
static void decode_stop_jam_response(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "stop_jam response < 4 bytes"); return; }
    w.key_uint("exciter_retry_count", p[0]);
}

// 106/40 — Configure External Modulation Data response (4 bytes).
// Per ICD Table 155: Software buffer size (uint32).
static void decode_ext_modulation_response(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "ext_modulation_response < 4 bytes"); return; }
    w.key_uint("software_buffer_size", load_u32le(p + 0));
}

// =============================================================================
// Group 108 response/report decoders
// =============================================================================

// 108/6 — List Jam Report (variable, streaming report from hardware).
// Per ICD Table 176: count (uint32) + S_LIST_JAM_REPORT_REPLY × count (8B each, max 100).
// S_LIST_JAM_REPORT_REPLY:
//   @0 Frequency (uint32, Hz — ICD explicitly labels field as "Frequency (Hz)")
//   @4 Frequency Status (uint16): 0=Inactive, 1=Active, 2=Jammed, 3=Within Protected Band
//   @6 Reserved (uint16)
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
// Group 109 response decoders
// =============================================================================

// 109/16 — Auto Threshold Value response (6404 bytes).
// Per ICD Table 181: bin count (uint32) + threshold bin data (float × 1600).
static void decode_auto_threshold(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "auto_threshold payload < 4 bytes"); return; }
    uint32_t bin_count = load_u32le(p + 0);
    w.key_uint("bin_count", bin_count);

    uint32_t emit = (bin_count < 1600) ? bin_count : 1600;
    int max_from_buf = (n - 4) / 4;
    if (static_cast<int>(emit) > max_from_buf) emit = static_cast<uint32_t>(max_from_buf);

    std::string arr = "[";
    for (uint32_t i = 0; i < emit; ++i) {
        if (i) arr += ',';
        char tmp[32];
        std::snprintf(tmp, sizeof(tmp), "%.2f",
            static_cast<double>(load_f32le(p + 4 + i * 4)));
        arr += tmp;
    }
    arr += "]";
    w.key_raw("threshold_dbm", arr);
}

// =============================================================================
// Group 112 response decoders (ASU/SDU and PA control — §3.2.2.6)
// =============================================================================

// 112/2 — ASU/SDU Configuration response (4 bytes). Per ICD Table 193.
// Error Value (int16) + Reserved (uint16).
static void decode_asu_sdu_response(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "asu_sdu_response < 4 bytes"); return; }
    w.key_int("error_value", load_i16le(p + 0));
}

// 112/4 — TRSDU Receiver Line Status response (4 bytes). Per ICD Table 195.
// TR_SDU_health_status (uint8) + 3 reserved. 0=T/R SDU Ok, 1=T/R SDU Fault.
static void decode_trsdu_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "trsdu_status payload < 4 bytes"); return; }
    uint8_t status = p[0];
    w.key_uint("trsdu_health_status", status);
    w.key_str("trsdu_status_name", status == 0 ? "ok" : "fault");
}

// 112/6 — Start/Stop Detection ACK: 0 bytes. Supersedes former PA Receiver Line Status.

// =============================================================================
// MRX (Monitoring Receiver, port 10015) response decoders — Groups 1, 3, 4, 7
// =============================================================================

// 1/2 — MRX Get System Version Details response (16 bytes). Per ICD Table 199.
// FW version (float) + Driver (float) + FPGA (float) + MRX Tuner ID (uint16) + Reserved (uint16).
static void decode_mrx_system_version(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) { w.key_str("warning", "mrx_system_version payload < 16 bytes"); return; }
    w.key_double("monitoring_fw_version", static_cast<double>(load_f32le(p +  0)));
    w.key_double("driver_version",        static_cast<double>(load_f32le(p +  4)));
    w.key_double("fpga_version",          static_cast<double>(load_f32le(p +  8)));
    w.key_uint("mrx_tuner_id",             load_u16le(p + 12));
}

// 1/4 — Get MRX Checksum Details response (1024 bytes). Per ICD Table 201.
// SJC FW Checksum (char[1024]) — emitted as hex string.
static void decode_mrx_checksum(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 1024) { w.key_str("warning", "mrx_checksum payload < 1024 bytes"); return; }
    w.key_str("mrx_fw_checksum_hex", to_hex(p, 1024));
}

// 1/6 — MRX PBIT Status response (120 bytes). Per ICD Table 204.
// Sequential layout: 22 status fields across @0-@23, then 96 reserved bytes.
// Item 6 at @5 is reserved (skipped). Items 17, 19 are 2-byte reserved gaps.
static void decode_mrx_pbit_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 120) { 
        w.key_str("warning", "mrx_pbit_status payload < 120 bytes"); 
        return; 
    }
    w.key_uint("fpga_scratch_pad_test",  p[ 0]);
    w.key_uint("fpga_board_id_status",   p[ 1]);
    w.key_uint("processor_temp_status",  p[ 2]);
    w.key_uint("fan_temp_status",        p[ 3]);
    w.key_uint("fpga_temp_status",       p[ 4]);
    w.key_uint("rf_psu_temp_status",     p[ 6]);
    w.key_uint("fan_speed_ctrl_status",  p[ 7]);
    w.key_uint("fan_voltage_status",     p[ 8]);
    w.key_uint("rfsu_5v_status",         p[ 9]);
    w.key_uint("rfsu_8v5_status",        p[10]);
    w.key_uint("msata_detection_status", p[11]);
    w.key_uint("lo1_pll_lock_status",    p[12]);
    w.key_uint("lo2_pll_lock_status",    p[13]);
    w.key_uint("bite_pll_lock_status",   p[14]);
    w.key_uint("tuner_detection_status", p[15]);
    w.key_uint("tuner_scratchpad_test",  p[18]);
    w.key_uint("pll_lock_status",        p[21]);
    w.key_uint("adc_bonding_status",     p[22]);
    w.key_uint("storage_avail_status",   p[23]);
}

// 1/8 — MRX IBIT Status response (112 bytes). Per ICD Table 207.
// Fields at @0-@7 (8 single-byte), @10 (1), @13 (1). Remaining bytes are reserved.
static void decode_mrx_ibit_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 112) { w.key_str("warning", "mrx_ibit_status payload < 112 bytes"); return; }
    w.key_uint("pll_lock_status",        p[ 0]);
    w.key_uint("adc_bonding_status",     p[ 1]);
    w.key_uint("msata_detection_status", p[ 2]);
    w.key_uint("storage_avail_status",   p[ 3]);
    w.key_uint("tuner_lo1_pll_lock",     p[ 4]);
    w.key_uint("tuner_lo2_pll_lock",     p[ 5]);
    w.key_uint("tuner_bite_pll_lock",    p[ 6]);
    w.key_uint("adc1_link_status",       p[ 7]);
    w.key_uint("tuner_detection_status", p[10]);
    w.key_uint("tuner_scratch_pad_test", p[13]);
}

// 1/10 — MRX Temperature Status response (36 bytes, 9 floats, all in Celsius).
// Per ICD Table 213: BPT, PSU-8156, Tuner, PSU-7255, Processor, PSU,
//   ControlBoard, RFPSU, FPGA.
static void decode_mrx_temperature(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 36) { w.key_str("warning", "mrx_temperature payload < 36 bytes"); return; }
    w.key_double("bpt_temp_c",            static_cast<double>(load_f32le(p +  0)));
    w.key_double("psu_8156_temp_c",       static_cast<double>(load_f32le(p +  4)));
    w.key_double("tuner_temp_c",          static_cast<double>(load_f32le(p +  8)));
    w.key_double("psu_7255_temp_c",       static_cast<double>(load_f32le(p + 12)));
    w.key_double("processor_temp_c",      static_cast<double>(load_f32le(p + 16)));
    w.key_double("psu_temp_c",            static_cast<double>(load_f32le(p + 20)));
    w.key_double("control_board_temp_c",  static_cast<double>(load_f32le(p + 24)));
    w.key_double("rf_psu_temp_c",         static_cast<double>(load_f32le(p + 28)));
    w.key_double("fpga_temp_c",           static_cast<double>(load_f32le(p + 32)));
}

// 1/14 — Fan Speed Details response (4 bytes). Per ICD Table 223.
// fan_speed_rpm(int32@0).
static void decode_fan_speed_resp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "fan_speed payload < 4 bytes"); return; }
    w.key_int("fan_speed_rpm", load_i32le(p + 0));
}

// 1/17 — UART Test Status command (4 bytes). Per ICD Table 225.
// uart_number(u8@0): 0=RS232, 1=RS422.
static void decode_cmd_uart_test_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 1) { w.key_str("warning", "uart_test_status cmd < 1 byte"); return; }
    w.key_uint("uart_number", p[0]);
    w.key_str("uart_type",p[0]==0?"RS232":p[0]==1?"RS422":"invalid");
}

// 1/18 — UART Test Status response (4 bytes). Per ICD Table 226.
// expected_data(u8@0), observed_data(u8@1), result(u8@2): 0=Fail, 1=Pass.
static void decode_uart_test_resp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 3) { w.key_str("warning", "uart_test_resp < 3 bytes"); return; }
    w.key_uint("expected_data",  p[0]);
    w.key_uint("observed_data",  p[1]);
    w.key_bool("uart_pass",      p[2] == 1);
}

// 3/2 — MRX Board Count and Tuner ID response (4 bytes). Per ICD Table 217.
// Available Board Count (uint16) + Available MRX Tuner ID (uint16).
// Note: MRX channels are inaccessible unless board_count == 1.
static void decode_mrx_board_count(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "mrx_board_count payload < 4 bytes"); return; }
    w.key_uint("available_board_count",  load_u16le(p + 0));
    w.key_uint("available_mrx_tuner_id", load_u16le(p + 2));
}

// 3/18, 3/22 — MRX Channel Status responses (16 bytes each, 8× uint16).
// Per ICD Tables 221, 219. For 3/18: 1=Open, 0=Closed. For 3/22: 1=Init OK, 0=Init Fail.
static void decode_mrx_channel_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) { w.key_str("warning", "mrx_channel_status payload < 16 bytes"); return; }
    std::string arr = "[";
    for (int i = 0; i < 8; ++i) {
        if (i) arr += ',';
        arr += std::to_string(load_u16le(p + i * 2));
    }
    arr += "]";
    w.key_raw("channel_status", arr);
}

// 3/26 — MRX CBIT Status response (8 bytes). Per ICD Table 210.
// @0 drx_status  @1 voltage_status  @2 temperature_status  @3 reserved
// @4 tuner_detection_status  @5-6 reserved  @7 memory_status
static void decode_mrx_cbit_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "mrx_cbit_status payload < 8 bytes"); return; }
    w.key_uint("drx_status",             p[0]);
    w.key_uint("voltage_status",         p[1]);
    w.key_uint("temperature_status",     p[2]);
    w.key_uint("tuner_detection_status", p[4]);
    w.key_uint("memory_status",          p[7]);
}

// 3/24 — MRx & SRx Tuning Information response (8 bytes). Per ICD Table 239.
// srx_tuned_status(u8@0), mrx_tuned_status(u8@1), srx_scan_mode_status(u16@2),
// tuned_center_freq(u16@4), memory_scan_tuned_status(u8@6), bite_selection(u8@7).
static void decode_mrx_srx_tuning_info(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "mrx_srx_tuning_info payload < 8 bytes"); return; }
    w.key_uint("srx_tuned_status",          p[0]);
    w.key_uint("mrx_tuned_status",          p[1]);
    w.key_uint("srx_scan_mode_status",      load_u16le(p + 2));
    w.key_uint("tuned_center_freq",         load_u16le(p + 4));
    w.key_uint("memory_scan_tuned_status",  p[6]);
    w.key_uint("bite_selection",            p[7]);
}

// 5/14 — Attenuation Details response (4 bytes). Per ICD Table 241.
// rf_attenuation(u8@0), if_attenuation(u8@1), agc_running_status(u16@2).
static void decode_attenuation_details(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "attenuation_details payload < 4 bytes"); return; }
    w.key_uint("rf_attenuation",      p[0]);
    w.key_uint("if_attenuation",      p[1]);
    w.key_uint("agc_running_status",  load_u16le(p + 2));
}

// 4/8 — Audio Data Acquisition response (variable: 4 + audio_size × 2 bytes).
// Per ICD Table 225: Audio Data Size (uint32) + Audio Data Buffer (uint16[]).
// Raw audio (PCM 8kHz 16-bit) not decoded — emit metadata only.
static void decode_mrx_audio_data(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "mrx_audio_data payload < 4 bytes"); return; }
    uint32_t audio_size = load_u32le(p + 0);
    w.key_uint("audio_data_size_samples", audio_size);
    w.key_bool("audio_buffer_present", n >= static_cast<int>(4 + audio_size * 2));
}

// 4/34, 4/24 — IQ Streaming Start / IQ Log Start response (4 bytes). Per ICD Tables 246, 251.
// channel (uint8) + narrow_iq_status (uint8) + wide_iq_status (uint8) + reserved (uint8).
static void decode_mrx_iq_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "mrx_iq_status payload < 4 bytes"); return; }
    w.key_uint("channel_number",   p[0]);
    w.key_uint("narrow_iq_status", p[1]);
    w.key_uint("wide_iq_status",   p[2]);
}

// 4/26 — Stop IQ Data Logging response (132 bytes). Per ICD Table 253.
// channel_number (uint16) + reserved (uint16) + file_path (char[128]).
static void decode_mrx_iq_log_stop(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 132) { w.key_str("warning", "mrx_iq_log_stop payload < 132 bytes"); return; }
    w.key_uint("channel_number", load_u16le(p + 0));
    char path[129] = {};
    std::memcpy(path, p + 4, 128);
    w.key_str("file_path", path);
}

// 4/42 — Read Memory Scan Data response (variable). Per ICD Tables 239-240.
// count (uint32) + scan_speed (uint16) + reserved (uint16) + S_RES_MEMORY_SCAN_LIST × count.
// S_RES_MEMORY_SCAN_LIST (20B): power (float) + frequency_mhz (double) +
//   hour/min/sec/reserved (4×uint8) + millisecond (uint16) + bandwidth (uint16).
static void decode_mrx_mem_scan_data(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "mrx_mem_scan_data payload < 8 bytes"); return; }
    uint32_t count = load_u32le(p + 0);
    w.key_uint("available_count",   count);
    w.key_uint("scan_speed_ch_sec", load_u16le(p + 4));

    const int ELEM = 20;
    int off = 8;
    std::string arr = "[";
    uint32_t emitted = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (off + ELEM > n) break;
        const uint8_t* e = p + off;
        JsonWriter h;
        h.key_double("power_dbm",    static_cast<double>(load_f32le(e + 0)));
        h.key_double("freq_hz",      load_f64le(e + 4) * 1e6);
        char toa[12];
        std::snprintf(toa, sizeof(toa), "%02u:%02u:%02u", e[12], e[13], e[14]);
        h.key_str("toa", toa);
        h.key_uint("millisecond",    load_u16le(e + 16));
        h.key_uint("bandwidth_code", load_u16le(e + 18));
        if (emitted++) arr += ',';
        arr += h.str();
        off += ELEM;
    }
    arr += "]";
    w.key_raw("scan_results", arr);
}

// 4/61 — Read Smart Memory Scan Data command (12 bytes). Per ICD Table 264.
// frequency_value(double@0,MHz?), channel_number(uint16@8).
static void decode_cmd_smart_mem_scan_read(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 12) { w.key_str("warning", "smart_mem_scan_read cmd < 12 bytes"); return; }
    w.key_double("frequency_value",  load_f64le(p + 0));
    w.key_uint("channel_number",     load_u16le(p + 8));
}

// 4/62 — Read Smart Memory Scan Data response (16 bytes). Per ICD Table 265.
// frequency_value(double@0), channel_number(uint16@8), amplitude(float@12).
static void decode_smart_mem_scan_data(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) { w.key_str("warning", "smart_mem_scan_data payload < 16 bytes"); return; }
    w.key_double("frequency_value", load_f64le(p +  0));
    w.key_uint("channel_number",    load_u16le(p +  8));
    w.key_double("amplitude",       static_cast<double>(load_f32le(p + 12)));
}

// 4/63 — Stop Smart Memory Scan command (4 bytes). Per ICD Table 266.
// channel_number(uint16@0) + reserved(uint16@2) — reuses decode_cmd_mrx_ch_only.
// 4/64 — Stop Smart Memory Scan ACK: 0 bytes.

// 4/44 — DDC FFT Data response (4 + 4096×4 = 16388 bytes). Per ICD Table 244.
// bin_count (uint16) + reserved (uint16) + DDC FFT data (float[4096]).
// Only the first bin_count entries are valid signal data.
static void decode_mrx_ddc_fft(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { 
        w.key_str("warning", "mrx_ddc_fft payload < 4 bytes"); 
        return; }
    uint16_t bin_count = load_u16le(p + 0);
    w.key_uint("bin_count", bin_count);
    const uint32_t MAX_BINS = 4096;
    uint32_t emit = (static_cast<uint32_t>(bin_count) < MAX_BINS) ? bin_count : MAX_BINS;
    if (n < static_cast<int>(4 + emit * 4)) {
        emit = static_cast<uint32_t>((n - 4) / 4);
        w.key_str("warning", "mrx_ddc_fft payload truncated");
    }
    std::string arr = "[";
    for (uint32_t i = 0; i < emit; ++i) {
        if (i) arr += ',';
        char tmp[32];
        std::snprintf(tmp, sizeof(tmp), "%.4g",
                      static_cast<double>(load_f32le(p + 4 + i * 4)));
        arr += tmp;
    }
    arr += "]";
    w.key_raw("fft_data", arr);
}

// 4/70 — Optical Port Availability Status response (12 bytes). Per ICD Table 270.
// port_number, port_id, port_alive_status, already_trans_status,
// able_to_start_transfer, reserved — all uint16.
static void decode_mrx_optical_port_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 12) { w.key_str("warning", "mrx_optical_port_status payload < 12 bytes"); return; }
    w.key_uint("port_number",   load_u16le(p +  0));
    w.key_uint("port_id",       load_u16le(p +  2));
    w.key_uint("port_alive",    load_u16le(p +  4));
    w.key_uint("already_trans", load_u16le(p +  6));
    w.key_uint("able_to_start", load_u16le(p +  8));
}

// 4/72 — Optical Interface IP Address response (24 bytes). Per ICD Table 272.
// ip_address (4×uint8) + port_ids (9×uint16) + reserved (uint16).
static void decode_mrx_optical_ip(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 24) { w.key_str("warning", "mrx_optical_ip payload < 24 bytes"); return; }
    char ip[20];
    std::snprintf(ip, sizeof(ip), "%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
    w.key_str("ip_address", ip);
    std::string ports = "[";
    for (int i = 0; i < 9; ++i) {
        if (i) ports += ',';
        ports += std::to_string(load_u16le(p + 4 + i * 2));
    }
    ports += "]";
    w.key_raw("port_ids", ports);
}

// 7/2 — MRX Signal BITE response (16 bytes). Per ICD Table 274.
// Observed Frequency (double, 8B MHz→Hz) + Power Level (float) +
// Result (uint16) + Reserved (uint16).
static void decode_mrx_signal_bite_resp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) { 
        w.key_str("warning", "mrx_signal_bite_resp payload < 16 bytes"); 
        return; 
    }
    w.key_double("observed_freq_hz",   load_f64le(p + 0) * 1e6);
    w.key_double("observed_power_dbm", static_cast<double>(load_f32le(p + 8)));
    w.key_uint("result",               load_u16le(p + 12));
}

// =============================================================================
// Group 200 command decoders (VU jamming — §3.2.2.8)
// =============================================================================

// 200/5 — External Modulation Jam Data command (8196 bytes). Per ICD Table 179.
// audio_data_size(uint32@0): valid sample count. audio_data: 4096×uint16 @ offset 4.
static void decode_cmd_ext_modulation_jam_data(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "ext_modulation_jam_data payload < 4 bytes"); return; }
    uint32_t cnt = load_u32le(p + 0);
    if (cnt > 4096) cnt = 4096;
    w.key_uint("audio_data_size", cnt);
    if (cnt > 0 && n >= (int)(4 + cnt * 2)) {
        std::string arr("[");
        for (uint32_t i = 0; i < cnt; i++) {
            char tmp[12];
            snprintf(tmp, sizeof(tmp), "%u", load_u16le(p + 4 + i * 2));
            arr += tmp;
            if (i + 1 < cnt) arr += ',';
        }
        arr += ']';
        w.key_raw("audio_data", arr.c_str());
    }
}

// 200/6 — External Modulation Jam Data response (4 bytes). Per ICD Table 180.
// software_buffer_size(uint32@0).
static void decode_ext_modulation_buffer_size(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "ext_modulation_buffer_size < 4 bytes"); return; }
    w.key_uint("software_buffer_size", load_u32le(p + 0));
}

// 200/9 — ECM Report State command (1 byte).
// ecm_report_state(u8@0): 0=Disable, 1=Enable.
static void decode_cmd_ecm_report_state(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 1) { w.key_str("warning", "ecm_report_state payload < 1 byte"); return; }
    w.key_bool("ecm_report_enabled", p[0] != 0);
}
// 200/10 — ECM Report State ACK: 0 bytes.

// 200/7 — Exciter Programmable Noise command (2052 bytes). Per ICD Table 181.
// channel_no(uint16@0, range 1-4), data_size(uint16@2, max 1024),
// data_values(uint16×1024@4).
static void decode_cmd_exciter_prog_noise(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "exciter_prog_noise payload < 4 bytes"); return; }
    w.key_uint("channel_no", load_u16le(p + 0));
    uint16_t cnt = load_u16le(p + 2);
    if (cnt > 1024) cnt = 1024;
    w.key_uint("data_size", cnt);
    if (cnt > 0 && n >= (int)(4 + cnt * 2)) {
        std::string arr("[");
        for (uint16_t i = 0; i < cnt; i++) {
            char tmp[12];
            snprintf(tmp, sizeof(tmp), "%u", load_u16le(p + 4 + i * 2));
            arr += tmp;
            if (i + 1 < cnt) arr += ',';
        }
        arr += ']';
        w.key_raw("data_values", arr.c_str());
    }
}
// 200/8 — Exciter Programmable Noise ACK: 0 bytes.

// 200/11 — Start List (Fixed Frequency) Jamming command (9040 bytes). Per ICD Table 139.
// S_LIST_JAMMING_INFO (@0, 36B): ff_burst_selection(uint32) + 4×DP_RPE_LIST_JAM_PA_INFO(8B each).
//   Each PA info (@4+i*8): modulation_type(u8), fm_deviation(u8), pa_power_level(u8),
//   exciter_modulating_signal(u8), jam_mode(u8), reserved[3].
// S_LIST_JAMMING_FREQUENCIES (@36, 9004B): freq_count(u16), reserved(u16),
//   frequencies[1000](float,MHz), thresholds[1000](float,dBm), priorities[1000](u8).
static void decode_cmd_list_fixed_freq_jam(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 36) { w.key_str("warning", "list_fixed_freq_jam payload < 36 bytes"); return; }
    w.key_uint("ff_burst_selection", load_u32le(p + 0));
    for (int i = 0; i < 4; i++) {
        const uint8_t* pa = p + 4 + i * 8;
        char k[48];
        snprintf(k, sizeof(k), "pa%d_modulation_type",           i); w.key_uint(k, pa[0]);
        snprintf(k, sizeof(k), "pa%d_fm_deviation",              i); w.key_uint(k, pa[1]);
        snprintf(k, sizeof(k), "pa%d_pa_power_level",            i); w.key_uint(k, pa[2]);
        snprintf(k, sizeof(k), "pa%d_exciter_modulating_signal", i); w.key_uint(k, pa[3]);
        snprintf(k, sizeof(k), "pa%d_jam_mode",                  i); w.key_uint(k, pa[4]);
    }
    if (n < 9040) { w.key_str("warning", "list_fixed_freq_jam payload < 9040 bytes"); return; }
    const uint8_t* fq = p + 36;
    uint16_t cnt = load_u16le(fq + 0);
    if (cnt > 1000) cnt = 1000;
    w.key_uint("freq_count", cnt);
    if (cnt > 0) {
        std::string farr("["), tarr("["), parr("[");
        for (uint16_t j = 0; j < cnt; j++) {
            char tmp[32];
            snprintf(tmp, sizeof(tmp), "%.1f", static_cast<double>(load_f32le(fq +    4 + j * 4)) * 1e6);
            farr += tmp;
            snprintf(tmp, sizeof(tmp), "%.2f", static_cast<double>(load_f32le(fq + 4004 + j * 4)));
            tarr += tmp;
            snprintf(tmp, sizeof(tmp), "%u",   fq[8004 + j]);
            parr += tmp;
            if (j + 1 < cnt) { farr += ','; tarr += ','; parr += ','; }
        }
        farr += ']'; tarr += ']'; parr += ']';
        w.key_raw("list_jam_freq_hz",       farr.c_str());
        w.key_raw("list_jam_threshold_dbm", tarr.c_str());
        w.key_raw("jam_priority",           parr.c_str());
    }
}
// 200/12 — Start List (Fixed Frequency) Jamming ACK: 0 bytes.

// 200/21 — Responsive Sweep Jam command (248 bytes). Per ICD Tables 148/149.
// @0  band_start_freq(double,MHz)  @8  band_stop_freq(double,MHz)
// @16 sweep_step(u8)  @17 pa_power_level(u8)  @18 fh_responsive_count(u8)
// @19 num_protected_bands(u8,max10)  @20 reserved  @21 fm_deviation(u8)
// @22 reserved  @23 freq_switch_time_sel(u8)
// @24  protected_band_start[10] (double,MHz, 80B)
// @104 protected_band_stop[10]  (double,MHz, 80B)
// @184 S_CMD_RPE_SWEEP_JAM[4] × 16B: sweep_start(double,MHz) + sweep_stop(double,MHz).
static void decode_cmd_200_responsive_sweep_jam(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 248) { w.key_str("warning", "200_responsive_sweep_jam payload < 248 bytes"); return; }
    static const double  SWEEP_STEP_KHZ[] = {2.5, 5.0, 12.5, 25.0, 50.0, 100.0};
    static const unsigned POWER_W[]       = {63, 125, 250, 500};
    w.key_double("band_start_freq_hz",  load_f64le(p + 0) * 1e6);
    w.key_double("band_stop_freq_hz",   load_f64le(p + 8) * 1e6);
    uint8_t step_idx = p[16];
    w.key_uint("sweep_step_index", step_idx);
    if (step_idx < 6) w.key_double("sweep_step_khz", SWEEP_STEP_KHZ[step_idx]);
    uint8_t pwr_idx = p[17];
    w.key_uint("pa_power_level_index", pwr_idx);
    if (pwr_idx < 4) w.key_uint("pa_power_level_w", POWER_W[pwr_idx]);
    w.key_uint("fh_responsive_count",  p[18]);
    uint8_t n_bands = p[19];
    w.key_uint("num_protected_bands",  n_bands);
    w.key_uint("fm_deviation",         p[21]);
    w.key_uint("freq_switch_time_sel", p[23]);
    if (n_bands > 0 && n_bands <= 10) {
        std::string starts("["), stops("[");
        for (uint8_t i = 0; i < n_bands; i++) {
            if (i) { starts += ','; stops += ','; }
            char tmp[32];
            std::snprintf(tmp, sizeof(tmp), "%.6g", load_f64le(p +  24 + i * 8) * 1e6);
            starts += tmp;
            std::snprintf(tmp, sizeof(tmp), "%.6g", load_f64le(p + 104 + i * 8) * 1e6);
            stops += tmp;
        }
        starts += ']'; stops += ']';
        w.key_raw("protected_band_starts_hz", starts);
        w.key_raw("protected_band_stops_hz",  stops);
    }
    for (int i = 0; i < 4; i++) {
        const uint8_t* s = p + 184 + i * 16;
        char k[32];
        snprintf(k, sizeof(k), "sweep%d_start_hz", i); w.key_double(k, load_f64le(s + 0) * 1e6);
        snprintf(k, sizeof(k), "sweep%d_stop_hz",  i); w.key_double(k, load_f64le(s + 8) * 1e6);
    }
}
// 200/22 — Responsive Sweep Jam ACK: 0 bytes.

// 200/41 — HPASU Health Status query command (4 bytes). Per ICD Table 198.
// hpasu_pa_selection(u8@0): 0=HPASU, 1=PA-1, 2=PA-2, 3=PA-3, 4=PA-4.
static void decode_cmd_hpasu_health_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 1) { w.key_str("warning", "hpasu_health_status cmd < 1 byte"); return; }
    w.key_uint("hpasu_pa_selection", p[0]);
}

// 200/42 — HPASU Health Status response (4 bytes).
// hpasu_health_status(u8@0).
static void decode_hpasu_health_status_resp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 1) { w.key_str("warning", "hpasu_health_status resp < 1 byte"); return; }
    w.key_uint("hpasu_health_status", p[0]);
}

// 200/56 — PA Soft Reboot command (4 bytes). Per ICD Table 204.
// pa_selection(u8@0): 0=PA-1, 1=PA-2, 2=PA-3, 3=PA-4.
static void decode_cmd_pa_soft_reboot(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 1) { w.key_str("warning", "pa_soft_reboot cmd < 1 byte"); return; }
    w.key_uint("pa_selection", p[0]);
}
// 200/57 — PA Soft Reboot ACK: 0 bytes.

// 200/54 — PA and SDU Health Status query: 0 bytes.
// 200/55 — PA and SDU Health Status response (8 bytes). Per ICD Table 202.
// pa1_health(u8@0), pa2_health(u8@1), pa3_health(u8@2), pa4_health(u8@3)
//   PA Fault: 0=No Fault, 1=Over Drive Fault, 3=VSWR Fault, 4=BPM Fault.
// sdu_health(u8@4): 0=No Fault, 1=Fault.
static void decode_resp_pa_sdu_health_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 5) { w.key_str("warning", "pa_sdu_health_status resp < 5 bytes"); return; }
    w.key_uint("pa1_health_status", p[0]);
    w.key_uint("pa2_health_status", p[1]);
    w.key_uint("pa3_health_status", p[2]);
    w.key_uint("pa4_health_status", p[3]);
    w.key_uint("sdu_health_status", p[4]);
}

// 200/17 — Wide Band Tracking command (68 bytes). Per ICD Tables 130/131/135.
// @0  fh_list_jam_count (uint32)
// @4  S_TRACKING_WINDOW[0] (24 bytes): hopper_start/stop_freq (float, MHz), hop_period_ms,
//     inter_period_ms, tolerance, hopper_power_level (all float)
// @28 S_TRACKING_WINDOW[1] — same layout
// @52 S_TRACKING_INFO (16 bytes): pa_power_level, modulation_type, fm_deviation,
//     exciter_modulating_signal (all uint32)
static void decode_cmd_wideband_tracking(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 68) { w.key_str("warning", "wideband_tracking payload < 68 bytes"); return; }
    w.key_uint("fh_list_jam_count", load_u32le(p + 0));
    // S_TRACKING_WINDOW[0] @ offset 4
    w.key_double("w0_hopper_start_freq_hz", static_cast<double>(load_f32le(p +  4)) * 1e6);
    w.key_double("w0_hopper_stop_freq_hz",  static_cast<double>(load_f32le(p +  8)) * 1e6);
    w.key_double("w0_hop_period_ms",        static_cast<double>(load_f32le(p + 12)));
    w.key_double("w0_inter_period_ms",      static_cast<double>(load_f32le(p + 16)));
    w.key_double("w0_tolerance",            static_cast<double>(load_f32le(p + 20)));
    w.key_double("w0_hopper_power_level",   static_cast<double>(load_f32le(p + 24)));
    // S_TRACKING_WINDOW[1] @ offset 28
    w.key_double("w1_hopper_start_freq_hz", static_cast<double>(load_f32le(p + 28)) * 1e6);
    w.key_double("w1_hopper_stop_freq_hz",  static_cast<double>(load_f32le(p + 32)) * 1e6);
    w.key_double("w1_hop_period_ms",        static_cast<double>(load_f32le(p + 36)));
    w.key_double("w1_inter_period_ms",      static_cast<double>(load_f32le(p + 40)));
    w.key_double("w1_tolerance",            static_cast<double>(load_f32le(p + 44)));
    w.key_double("w1_hopper_power_level",   static_cast<double>(load_f32le(p + 48)));
    // S_TRACKING_INFO @ offset 52
    w.key_uint("pa_power_level",            load_u32le(p + 52));
    w.key_uint("modulation_type",           load_u32le(p + 56));
    w.key_uint("fm_deviation",              load_u32le(p + 60));
    w.key_uint("exciter_modulating_signal", load_u32le(p + 64));
}
// 200/18 — Wide Band Tracking ACK: 0 bytes.

// 200/* — VU Jamming ACK responses (Group 200, VU-specific).
// unit_id 2 = Immediate Jam ACK
// unit_id 4 = Follow-On Jam ACK
// unit_id 6 = Jam List ACK
// unit_id 8 = Responsive Sweep Jam ACK
// All share the same 8-byte ACK layout:
//   @0 JamId   (uint16) — echoes the jam ID from the request
//   @2 Reserved (uint16)
//   @4 Active  (uint16) — 1 = jamming active, 0 = stopped
//   @6 Reserved (uint16)
// A zero-length payload means the ACK carries status only (frame header status field).
static void decode_vu_jam_ack(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    static const char* NAMES[] = {nullptr, nullptr,
        "immediate_jam",    // unit 2
        nullptr,
        "follow_on_jam",    // unit 4
        nullptr,
        "jam_list",         // unit 6
        nullptr,
        "responsive_sweep"  // unit 8
    };
    const char* kind = (unit_id < 9 && NAMES[unit_id]) ? NAMES[unit_id] : "unknown_jam";
    w.key_str("jam_kind", kind);

    if (n >= 8) {
        w.key_uint("jam_id",     load_u16le(p + 0));
        w.key_bool("jam_active", load_u16le(p + 4) == 1);
    } else if (n > 0) {
        w.key_str("warning", "jam ack payload < 8 bytes");
        w.key_str("raw_hex", to_hex(p, n));
    }
    // n == 0: status-only ACK, no extra fields needed.
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
    w.key_str("frame_type", frame_type == FRAME_COMMAND ? "command"
                          : frame_type == FRAME_STREAM  ? "stream"
                          : "response");
    w.key_uint("group_id", hdr.group_id);
    w.key_uint("unit_id",  hdr.unit_id);
    w.key_uint("message_size_bytes", hdr.payload_size);
    if (frame_type == FRAME_RESPONSE) {
        w.key_int("status", hdr.status);
        w.key_str("status_name", hdr.status == 0 ? "OnSuccess" : "Error");
    }

    bool decoded = false;
    if (frame_type == FRAME_COMMAND) {
        if (hdr.group_id == 101) {
            switch (hdr.unit_id) {
                case  25: decode_cmd_set_threshold(payload, plen, w);              decoded = true; break;
                case  27: decode_cmd_set_resolution(payload, plen, w);             decoded = true; break;
                case  31: decode_cmd_start_follow_on_jam(payload, plen, w);        decoded = true; break;
                case  33: /* Stop Follow-On Jam / Stop Responsive Sweep — 0 bytes */decoded = true; break;
                case  37: decode_cmd_configure_detection(payload, plen, w);        decoded = true; break;
                case  39: decode_cmd_start_fh_detection(payload, plen, w);         decoded = true; break;
                case  43: decode_cmd_get_wideband_fft(payload, plen, w);           decoded = true; break;
                case  47: decode_cmd_set_pulse_range(payload, plen, w);            decoded = true; break;
                case  55: decode_cmd_set_min_hops(payload, plen, w);               decoded = true; break;
                case  63: decode_cmd_set_tracking_config(payload, plen, w);        decoded = true; break;
                case  69: decode_cmd_start_ff_detection(payload, plen, w);         decoded = true; break;
                case  73: decode_cmd_start_list_jam(payload, plen, w);             decoded = true; break;
                case  75: /* Stop List Jamming — 0 bytes */                         decoded = true; break;
                case  79: decode_cmd_send_ecm_reports(payload, plen, w);           decoded = true; break;
                case  83: decode_cmd_start_burst_detection(payload, plen, w);      decoded = true; break;
                case  85: decode_cmd_start_scan_speed(payload, plen, w);           decoded = true; break;
                case  87: /* Stop Scan Speed — 0 bytes */                           decoded = true; break;
                case  92: decode_cmd_start_responsive_sweep_jam(payload, plen, w); decoded = true; break;
                case  94: decode_cmd_get_zoom_fft(payload, plen, w);               decoded = true; break;
                case 100: decode_cmd_set_flatness_mode(payload, plen, w);          decoded = true; break;
                case 102: decode_cmd_set_integration_time(payload, plen, w);       decoded = true; break;
                case 104: decode_cmd_set_multifh_mode(payload, plen, w);           decoded = true; break;
                case 106: decode_cmd_set_narrowband_fh(payload, plen, w);          decoded = true; break;
                case 140: decode_cmd_configure_center_freq(payload, plen, w);      decoded = true; break;
                case 158: decode_cmd_terminate_fft(payload, plen, w);              decoded = true; break;
                case 160: decode_cmd_spectrum_acq_params(payload, plen, w);        decoded = true; break;
                case 162: decode_cmd_agc_enable(payload, plen, w);                 decoded = true; break;
                case 164: decode_cmd_zoom_report_enable(payload, plen, w);         decoded = true; break;
                case 174: decode_cmd_multifh_split_enable(payload, plen, w);       decoded = true; break;
                case 176: decode_cmd_set_burst_pulse_range(payload, plen, w);      decoded = true; break;
                case 178: decode_cmd_signal_sidelobe_enable(payload, plen, w);       decoded = true; break;
                case 182: decode_cmd_wideband_pulse_agility_enable(payload, plen, w);decoded = true; break;
                case 184: /* System Reboot — 0 bytes */                              decoded = true; break;
                case 186: /* (101/186) — 0 bytes */                                  decoded = true; break;
                case 200: /* Engage Center Frequency — 0 bytes */                    decoded = true; break;
                case 202: /* Disengage Center Frequency — 0 bytes */                 decoded = true; break;
                case 204: /* Get SRx MRx Status — 0 bytes */                        decoded = true; break;
                case 210: decode_cmd_vuhf_wideband_enable(payload, plen, w);        decoded = true; break;
                default:  break;
            }
        } else if (hdr.group_id == 111) {
            switch (hdr.unit_id) {
                case  3: decode_cmd_111_signal_bite(payload, plen, w);              decoded = true; break;
                case  5: decode_cmd_reference_input(payload, plen, w);              decoded = true; break;
                case  7: /* Module Health Status — 0 bytes */                        decoded = true; break;
                case  9: decode_cmd_send_protected_scan_list(payload, plen, w);     decoded = true; break;
                case 13: decode_cmd_protected_scan_enable(payload, plen, w);        decoded = true; break;
                case 15: /* PDW Channelization Data — 0 bytes */                     decoded = true; break;
                case 17: decode_cmd_fh_splitband_enable(payload, plen, w);          decoded = true; break;
                case 19: decode_cmd_send_fh_splitband_freq(payload, plen, w);       decoded = true; break;
                case 21: decode_cmd_signal_bite(payload, plen, w);                  decoded = true; break;
                case 23: decode_cmd_protected_spectrum_enable(payload, plen, w);    decoded = true; break;
                case 25: /* Get Storage Details — 0 bytes */                         decoded = true; break;
                case 27: /* Read Protected Band List — 0 bytes */                    decoded = true; break;
                case 29: decode_cmd_auto_threshold_enable(payload, plen, w);        decoded = true; break;
                case 31: decode_cmd_hopper_channelization_enable(payload, plen, w); decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 106) {
            switch (hdr.unit_id) {
                case  1: decode_cmd_start_immediate_jam(payload, plen, w);       decoded = true; break;
                case  3: decode_cmd_generate_tdm(payload, plen, w);              decoded = true; break;
                case  5: decode_cmd_generate_fdm(payload, plen, w);              decoded = true; break;
                case  9: /* Stop Immediate Jamming — 0 bytes */                   decoded = true; break;
                case 21: decode_cmd_enable_pa(payload, plen, w);                 decoded = true; break;
                case 39: decode_cmd_configure_ext_modulation(payload, plen, w);  decoded = true; break;
                case 41: decode_cmd_generate_sweep(payload, plen, w);            decoded = true; break;
                case 45: decode_cmd_enable_pa_sdu(payload, plen, w);             decoded = true; break;
                case 49: decode_cmd_configure_prog_exciter(payload, plen, w);    decoded = true; break;
                case 55: decode_cmd_generate_comb_noise(payload, plen, w);       decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 109) {
            switch (hdr.unit_id) {
                case 11: decode_cmd_set_date_time(payload, plen, w); decoded = true; break;
                case 15: /* Auto Threshold — 0 bytes */               decoded = true; break;
                case 17: /* Acquire Hopper Channelization — 0 bytes */decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 112) {
            switch (hdr.unit_id) {
                case  1: decode_cmd_asu_sdu_config(payload, plen, w); decoded = true; break;
                case  3: /* TRSDU Status — 0 bytes */                  decoded = true; break;
                case  5: decode_cmd_start_stop_detection(payload, plen, w); decoded = true; break;
                case 37: decode_cmd_pa_freq_config(payload, plen, w);  decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 200) {
            switch (hdr.unit_id) {
                case  3: /* 200/3 — 0 bytes */                                 decoded = true; break;
                case  5: decode_cmd_ext_modulation_jam_data(payload, plen, w); decoded = true; break;
                case  7: decode_cmd_exciter_prog_noise(payload, plen, w);      decoded = true; break;
                case  9: decode_cmd_ecm_report_state(payload, plen, w);        decoded = true; break;
                case 11: decode_cmd_list_fixed_freq_jam(payload, plen, w);  decoded = true; break;
                case 13: /* 200/13 — 0 bytes */                             decoded = true; break;
                case 17: decode_cmd_wideband_tracking(payload, plen, w);         decoded = true; break;
                case 19: /* 200/19 — 0 bytes */                                  decoded = true; break;
                case 21: decode_cmd_200_responsive_sweep_jam(payload, plen, w);  decoded = true; break;
                case 41: decode_cmd_hpasu_health_status(payload, plen, w);       decoded = true; break;
                case 54: /* PA and SDU Health Status — 0 bytes */                decoded = true; break;
                case 56: decode_cmd_pa_soft_reboot(payload, plen, w);            decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 1) {
            // MRX diagnostics commands — all 0-byte (port 10015)
            switch (hdr.unit_id) {
                case  1: /* Get MRX System Version */  decoded = true; break;
                case  3: /* Get MRX Checksum */        decoded = true; break;
                case  5: /* MRX PBIT Status */         decoded = true; break;
                case  7: /* MRX IBIT Status */         decoded = true; break;
                case  9: /* MRX Temperature Status */  decoded = true; break;
                case 13: /* Fan Speed Details */       decoded = true; break;
                case 17: decode_cmd_uart_test_status(payload, plen, w); decoded = true; break;
                case 25: /* CBIT Status — 0 bytes */   decoded = true; break;
                case 33: /* Close All Channels */      decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 3) {
            switch (hdr.unit_id) {
                case  1: /* Read Board Count */                              decoded = true; break;
                case 17: /* Read Channel Info */                             decoded = true; break;
                case 19: decode_cmd_mrx_write_channel(payload, plen, w);   decoded = true; break;
                case 21: /* MRX Channel Status */                            decoded = true; break;
                case 23: /* MRx & SRx Tuning Info — 0 bytes */              decoded = true; break;
                case 25: /* MRX CBIT Status */                               decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 4) {
            switch (hdr.unit_id) {
                case  5: decode_cmd_mrx_set_threshold(payload, plen, w);    decoded = true; break;
                case  7: decode_cmd_mrx_ch_only(payload, plen, w);          decoded = true; break;
                case  9: decode_cmd_mrx_ch_only(payload, plen, w);          decoded = true; break;
                case 11: decode_cmd_mrx_ch_only(payload, plen, w);          decoded = true; break;
                case 15: decode_cmd_mrx_ch_only(payload, plen, w);          decoded = true; break;
                case 17: decode_cmd_mrx_demod_bw(payload, plen, w);         decoded = true; break;
                case 23: decode_cmd_mrx_start_iq_log(payload, plen, w);     decoded = true; break;
                case 25: decode_cmd_mrx_ch_only(payload, plen, w);          decoded = true; break;
                case 33: decode_cmd_mrx_ch_bw(payload, plen, w);            decoded = true; break;
                case 35: decode_cmd_mrx_ch_only(payload, plen, w);          decoded = true; break;
                case 39: decode_cmd_mrx_mem_scan(payload, plen, w);         decoded = true; break;
                case 41: decode_cmd_mrx_mem_scan(payload, plen, w);         decoded = true; break;
                case 43: decode_cmd_mrx_ch_bw(payload, plen, w);            decoded = true; break;
                case 53: decode_cmd_channel_engagement(payload, plen, w);    decoded = true; break;
                case 55: decode_cmd_mrx_ch_only(payload, plen, w);          decoded = true; break;
                case 57: decode_cmd_mrx_ch_only(payload, plen, w);          decoded = true; break;
                case 59: decode_cmd_mrx_ch_only(payload, plen, w);          decoded = true; break;
                case 61: decode_cmd_smart_mem_scan_read(payload, plen, w);  decoded = true; break;
                case 63: decode_cmd_mrx_ch_only(payload, plen, w);          decoded = true; break;
                case 65: decode_cmd_mrx_ch_bw(payload, plen, w);            decoded = true; break;
                case 67: decode_cmd_mrx_ch_only(payload, plen, w);          decoded = true; break;
                case 69: decode_cmd_mrx_ch_bw(payload, plen, w);            decoded = true; break;
                case 71: decode_cmd_mrx_ch_bw(payload, plen, w);            decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 5) {
            switch (hdr.unit_id) {
                case  1: decode_cmd_mrx_center_freq(payload, plen, w);   decoded = true; break;
                case  3: decode_cmd_mrx_attenuation(payload, plen, w);   decoded = true; break;
                case 13: /* Attenuation Details — 0 bytes */              decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 6) {
            switch (hdr.unit_id) {
                case  7: decode_cmd_fh_monitoring(payload, plen, w);   decoded = true; break;
                case  9: decode_cmd_go2mon_connect(payload, plen, w);  decoded = true; break;
                case 11: /* GO2Mon Disconnect — 0 bytes */               decoded = true; break;
                case 13: decode_cmd_go2mon_start(payload, plen, w);    decoded = true; break;
                case 15: /* Stop GO2Mon — 0 bytes */                     decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 7) {
            switch (hdr.unit_id) {
                case  1: decode_cmd_mrx_signal_bite(payload, plen, w);    decoded = true; break;
                case  3: decode_cmd_mrx_bite_ant(payload, plen, w);       decoded = true; break;
                case  5: decode_cmd_mrx_ref_source(payload, plen, w);     decoded = true; break;
                case  9: decode_cmd_mrx_afc(payload, plen, w);            decoded = true; break;
                case 11: decode_cmd_mrx_rf_squelch(payload, plen, w);     decoded = true; break;
                case 13: decode_cmd_mrx_ch_only(payload, plen, w);        decoded = true; break;
                case 15: decode_cmd_mrx_ch_only(payload, plen, w);        decoded = true; break;
                case 17: decode_cmd_mrx_spectrum_avg(payload, plen, w);   decoded = true; break;
                case 19: decode_cmd_mrx_rf_agc(payload, plen, w);         decoded = true; break;
                case 21: decode_cmd_mrx_audio_squelch(payload, plen, w);  decoded = true; break;
                case 23: decode_cmd_set_datetime(payload, plen, w);        decoded = true; break;
                default: break;
            }
        }
    } else if (frame_type == FRAME_RESPONSE) {
        if (hdr.group_id == 100) {
            switch (hdr.unit_id) {
                case  2: decode_system_version(payload, plen, w);   decoded = true; break;
                case  4: decode_srx_checksum(payload, plen, w);     decoded = true; break;
                case  6: decode_pbit_status(payload, plen, w);      decoded = true; break;
                case  8: decode_ibit_status(payload, plen, w);      decoded = true; break;
                case 10: decode_temperature(payload, plen, w);      decoded = true; break;
                case 14: decode_fan_speed_status(payload, plen, w); decoded = true; break;
                case 18: decode_uart_test(payload, plen, w);        decoded = true; break;
                case 26: decode_cbit_status(payload, plen, w);      decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 101) {
            switch (hdr.unit_id) {
                case  26: /* Set Threshold ACK */                      decoded = true; break;
                case  28: /* Set Resolution ACK */                     decoded = true; break;
                case  32: /* Start Follow-On Jamming ACK */            decoded = true; break;
                case  34: /* Stop operation ACK */                     decoded = true; break;
                case  38: /* Configure Detection ACK */                decoded = true; break;
                case  40: decode_fh_detection(payload, plen, w);       decoded = true; break;
                case  44: decode_wideband_fft_1600(payload, plen, w);  decoded = true; break;
                case  48: /* Set Pulse Range ACK */                    decoded = true; break;
                case  56: /* Set Min Hops ACK */                       decoded = true; break;
                case  64: /* Set Tracking Config ACK */                decoded = true; break;
                case  70: decode_ff_detection(payload, plen, w);       decoded = true; break;
                case  74: /* Start List Jamming ACK */                 decoded = true; break;
                case  76: /* Stop List Jamming ACK */                  decoded = true; break;
                case  80: /* Send ECM Reports ACK */                   decoded = true; break;
                case  84: decode_burst_detection(payload, plen, w);    decoded = true; break;
                case  86: /* Start Scan Speed ACK */                   decoded = true; break;
                case  88: decode_stop_scan_speed(payload, plen, w);    decoded = true; break;
                case  93: /* Start Responsive Sweep Jam ACK */         decoded = true; break;
                case  95: decode_zoom_fft(payload, plen, w);           decoded = true; break;
                case 101: /* Set Flatness Mode ACK */                  decoded = true; break;
                case 103: /* Set Integration Time ACK */               decoded = true; break;
                case 105: /* Set Multi FH Mode ACK */                  decoded = true; break;
                case 107: /* Set Narrow Band FH ACK */                 decoded = true; break;
                case 141: /* Configure Center Frequency ACK */         decoded = true; break;
                case 159: /* Terminate FFT Thread ACK */               decoded = true; break;
                case 161: /* Spectrum Acq Params ACK */                decoded = true; break;
                case 163: /* AGC Enable ACK */                         decoded = true; break;
                case 165: /* Zoom-Report Enable ACK */                 decoded = true; break;
                case 175: /* Multiple FH Split Enable ACK */           decoded = true; break;
                case 177: /* Set Burst Pulse Range ACK */               decoded = true; break;
                case 179: /* Signal Sidelobe Enable ACK */              decoded = true; break;
                case 183: /* Wideband Pulse Agility Enable ACK */       decoded = true; break;
                case 185: /* System Reboot ACK */                       decoded = true; break;
                case 187: /* (101/187) ACK */                           decoded = true; break;
                case 201: /* Engage Center Frequency ACK */             decoded = true; break;
                case 203: /* Disengage Center Frequency ACK */          decoded = true; break;
                case 205: decode_srx_mrx_status(payload, plen, w);     decoded = true; break;
                default:  break;
            }
        } else if (hdr.group_id == 111) {
            switch (hdr.unit_id) {
                case  4: decode_111_signal_bite_resp(payload, plen, w);     decoded = true; break;
                case  6: /* Reference Input Selection ACK */                 decoded = true; break;
                case  8: decode_module_health(payload, plen, w);            decoded = true; break;
                case 10: /* Send Protected Scan List ACK */                  decoded = true; break;
                case 14: /* Protected Scan Enable/Disable ACK */             decoded = true; break;
                case 16: decode_pdw_channelization(payload, plen, w);       decoded = true; break;
                case 18: /* FH Split band Enable/Disable ACK */              decoded = true; break;
                case 20: /* Send FH Split band Frequency ACK */              decoded = true; break;
                case 22: decode_signal_bite(payload, plen, w);              decoded = true; break;
                case 24: /* Protected-Band Spectrum Enable/Disable ACK */    decoded = true; break;
                case 26: decode_storage_details(payload, plen, w);          decoded = true; break;
                case 28: decode_protected_band_list(payload, plen, w);      decoded = true; break;
                case 30: /* Auto Threshold Enable/Disable ACK */             decoded = true; break;
                case 32: /* Hopper Channelization Enable/Disable ACK */      decoded = true; break;
                case 211: /* VUHF Wideband Enable ACK (from 101/210 cmd) */ decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 106) {
            switch (hdr.unit_id) {
                case  2: /* Start Immediate Jam ACK */                     decoded = true; break;
                case  4: /* Generate TDM ACK */                            decoded = true; break;
                case  6: /* Generate FDM ACK */                            decoded = true; break;
                case 10: decode_stop_jam_response(payload, plen, w);      decoded = true; break;
                case 22: /* Enable PA ACK */                               decoded = true; break;
                case 40: decode_ext_modulation_response(payload, plen, w);decoded = true; break;
                case 42: /* Generate Sweep ACK */                          decoded = true; break;
                case 46: /* Enable PA SDU ACK */                           decoded = true; break;
                case 50: /* Configure Prog Exciter ACK */                  decoded = true; break;
                case 56: /* Generate Comb Noise ACK */                     decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 108) {
            switch (hdr.unit_id) {
                case 6: decode_list_jam_report(payload, plen, w); decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 109) {
            switch (hdr.unit_id) {
                case 12: /* Set Date Time ACK */                       decoded = true; break;
                case 16: decode_auto_threshold(payload, plen, w);      decoded = true; break;
                case 18: decode_pdw_channelization(payload, plen, w);  decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 112) {
            switch (hdr.unit_id) {
                case  2: decode_asu_sdu_response(payload, plen, w);   decoded = true; break;
                case  4: decode_trsdu_status(payload, plen, w);        decoded = true; break;
                case  6: /* Start/Stop Detection ACK — 0 bytes */       decoded = true; break;
                case 38: /* PA Freq Config ACK */                       decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 1) {
            // MRX diagnostic responses (port 10015)
            switch (hdr.unit_id) {
                case  2: decode_mrx_system_version(payload, plen, w);  decoded = true; break;
                case  4: decode_mrx_checksum(payload, plen, w);         decoded = true; break;
                case  6: decode_mrx_pbit_status(payload, plen, w);      decoded = true; break;
                case  8: decode_mrx_ibit_status(payload, plen, w);      decoded = true; break;
                case 10: decode_mrx_temperature(payload, plen, w);      decoded = true; break;
                case 14: decode_fan_speed_resp(payload, plen, w);       decoded = true; break;
                case 18: decode_uart_test_resp(payload, plen, w);        decoded = true; break;
                case 26: decode_mrx_cbit_status(payload, plen, w);      decoded = true; break;
                case 34: /* Close All Channels ACK */                    decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 3) {
            switch (hdr.unit_id) {
                case  2: decode_mrx_board_count(payload, plen, w);     decoded = true; break;
                case 18: decode_mrx_channel_status(payload, plen, w);  decoded = true; break;
                case 20: /* Write Channel ACK */                         decoded = true; break;
                case 22: decode_mrx_channel_status(payload, plen, w);       decoded = true; break;
                case 24: decode_mrx_srx_tuning_info(payload, plen, w);      decoded = true; break;
                case 26: decode_mrx_cbit_status(payload, plen, w);          decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 4) {
            switch (hdr.unit_id) {
                case  6: /* Set Threshold ACK */                              decoded = true; break;
                case  8: decode_mrx_audio_data(payload, plen, w);            decoded = true; break;
                case 10: /* Audio Start Play ACK */                           decoded = true; break;
                case 12: /* Audio Stop Play ACK */                            decoded = true; break;
                case 16: /* Audio FIFO Reset ACK */                           decoded = true; break;
                case 18: /* Demod/BW Selection ACK */                         decoded = true; break;
                case 24: decode_mrx_iq_status(payload, plen, w);             decoded = true; break;
                case 26: decode_mrx_iq_log_stop(payload, plen, w);           decoded = true; break;
                case 34: decode_mrx_iq_status(payload, plen, w);             decoded = true; break;
                case 36: /* Stop IQ Streaming ACK */                          decoded = true; break;
                case 40: /* Configure Memory Scan ACK */                      decoded = true; break;
                case 42: decode_mrx_mem_scan_data(payload, plen, w);         decoded = true; break;
                case 44: decode_mrx_ddc_fft(payload, plen, w);               decoded = true; break;
                case 54: /* Channel Engagement ACK */                         decoded = true; break;
                case 58: /* Stop Memory Scan ACK */                           decoded = true; break;
                case 60: /* Configure Memory Scan ACK */                      decoded = true; break;
                case 62: decode_smart_mem_scan_data(payload, plen, w);        decoded = true; break;
                case 64: /* Stop Smart Memory Scan ACK — 0 bytes */           decoded = true; break;
                case 66: /* Start IQ Optical ACK */                           decoded = true; break;
                case 68: /* Stop IQ Optical ACK */                            decoded = true; break;
                case 70: decode_mrx_optical_port_status(payload, plen, w);   decoded = true; break;
                case 72: decode_mrx_optical_ip(payload, plen, w);            decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 5) {
            switch (hdr.unit_id) {
                case  2: /* Set Center Freq ACK */                           decoded = true; break;
                case  4: /* Attenuation Select ACK */                        decoded = true; break;
                case 14: decode_attenuation_details(payload, plen, w);       decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 6) {
            switch (hdr.unit_id) {
                case  8: /* FH Monitoring Config ACK */  decoded = true; break;
                case 10: /* GO2Mon Connect ACK */        decoded = true; break;
                case 12: /* GO2Mon Disconnect ACK */     decoded = true; break;
                case 14: /* GO2Mon Start ACK */          decoded = true; break;
                case 16: /* GO2Mon Stop ACK */           decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 7) {
            switch (hdr.unit_id) {
                case  2: decode_mrx_signal_bite_resp(payload, plen, w); decoded = true; break;
                case  4: /* BITE/ANT Select ACK */   decoded = true; break;
                case  6: /* Ref Source ACK */         decoded = true; break;
                case 10: /* AFC Select ACK */         decoded = true; break;
                case 12: /* RF Squelch ACK */         decoded = true; break;
                case 14: /* IQ Socket Connect ACK */  decoded = true; break;
                case 16: /* IQ Socket Close ACK */    decoded = true; break;
                case 18: /* Spectrum Avg ACK */       decoded = true; break;
                case 20: /* RF AGC ACK */             decoded = true; break;
                case 22: /* Audio Squelch ACK */      decoded = true; break;
                case 24: /* Set Date/Time ACK */      decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 200) {
            switch (hdr.unit_id) {
                case  2:
                    decode_vu_jam_ack(hdr.unit_id, payload, plen, w); decoded = true; break;
                case  4: /* 200/3 ACK — 0 bytes */                            decoded = true; break;
                case  6: decode_ext_modulation_buffer_size(payload, plen, w); decoded = true; break;
                case  8: /* Exciter Prog Noise ACK — 0 bytes */               decoded = true; break;
                case 10: /* ECM Report State ACK — 0 bytes */           decoded = true; break;
                case 12: /* List Fixed Freq Jam ACK — 0 bytes */        decoded = true; break;
                case 14: /* 200/13 ACK — 0 bytes */                    decoded = true; break;
                case 18: /* Wide Band Tracking ACK — 0 bytes */        decoded = true; break;
                case 20: /* 200/19 ACK — 0 bytes */                    decoded = true; break;
                case 22: /* Responsive Sweep Jam ACK — 0 bytes */      decoded = true; break;
                case 42: decode_hpasu_health_status_resp(payload, plen, w);   decoded = true; break;
                case 55: decode_resp_pa_sdu_health_status(payload, plen, w);  decoded = true; break;
                case 57: /* PA Soft Reboot ACK — 0 bytes */                   decoded = true; break;
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
static bool json_find_int(const char* json, const char* key, long long& out) {
    std::string pat = std::string("\"") + key + "\"";
    const char* k = std::strstr(json, pat.c_str());
    if (!k) return false;
    const char* c = std::strchr(k, ':');
    if (!c) return false;
    ++c;
    while (*c == ' ' || *c == '\t') ++c;
    char* end = nullptr;
    long long v = std::strtoll(c, &end, 10);
    if (end == c) return false;
    out = v;
    return true;
}

static bool json_find_double(const char* json, const char* key, double& out) {
    std::string pat = std::string("\"") + key + "\"";
    const char* k = std::strstr(json, pat.c_str());
    if (!k) return false;
    const char* c = std::strchr(k + pat.size(), ':');
    if (!c) return false;
    ++c;
    while (*c == ' ' || *c == '\t') ++c;
    char* end = nullptr;
    out = std::strtod(c, &end);
    return end != c;
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Parse "HH:MM:SS" toa string or _h/_m/_s integer fields into h/m/s bytes.
static void parse_toa_hms(const char* j, const char* key,
                           uint8_t& h_out, uint8_t& m_out, uint8_t& s_out) {
    h_out = 0; m_out = 0; s_out = 0;
    char sub[64]; long long v;
    std::snprintf(sub, sizeof(sub), "%s_h", key); if (json_find_int(j, sub, v)) h_out = (uint8_t)v;
    std::snprintf(sub, sizeof(sub), "%s_m", key); if (json_find_int(j, sub, v)) m_out = (uint8_t)v;
    std::snprintf(sub, sizeof(sub), "%s_s", key); if (json_find_int(j, sub, v)) s_out = (uint8_t)v;
    if (h_out || m_out || s_out) return;
    std::string pat = std::string("\"") + key + "\"";
    const char* k = std::strstr(j, pat.c_str()); if (!k) return;
    const char* c = std::strchr(k + pat.size(), ':'); if (!c) return; ++c;
    while (*c == ' ' || *c == '\t') ++c;
    if (*c != '"') return; ++c;
    unsigned hh = 0, mm = 0, ss = 0;
    if (std::sscanf(c, "%u:%u:%u", &hh, &mm, &ss) == 3) {
        h_out = (uint8_t)hh; m_out = (uint8_t)mm; s_out = (uint8_t)ss;
    }
}

// Parse bool field: handles true/false literals and 0/1 integers.
static bool json_find_bool(const char* json, const char* key, bool& out) {
    std::string pat = std::string("\"") + key + "\"";
    const char* k = std::strstr(json, pat.c_str()); if (!k) return false;
    const char* c = std::strchr(k + pat.size(), ':'); if (!c) return false; ++c;
    while (*c == ' ' || *c == '\t') ++c;
    if (std::strncmp(c, "true",  4) == 0) { out = true;  return true; }
    if (std::strncmp(c, "false", 5) == 0) { out = false; return true; }
    long long v = 0;
    if (json_find_int(json, key, v)) { out = (v != 0); return true; }
    return false;
}

// Encode 4 × S_WBFSR_RES_VUHF_BITE_TEST entries (48 bytes) from "bite_path_validation" array.
static void encode_bite_path_validation(const char* j, uint8_t* buf) {
    std::memset(buf, 0, 48);
    const char* bpv = std::strstr(j, "\"bite_path_validation\""); if (!bpv) return;
    const char* arr = std::strchr(bpv, '['); if (!arr) return;
    const char* p = arr + 1; int written = 0;
    while (written < 4) {
        while (*p && *p != '{' && *p != ']') ++p;
        if (!*p || *p == ']') break;
        const char* os = p; int depth = 1; ++p;
        while (*p && depth > 0) { if (*p == '{') ++depth; else if (*p == '}') --depth; ++p; }
        std::string entry(os, p); const char* e = entry.c_str();
        double freq_hz = 0, power = 0; long long result = 0;
        json_find_double(e, "bite_freq_hz", freq_hz);
        json_find_double(e, "bite_power",   power);
        json_find_int(e,    "bite_result",  result);
        uint8_t* slot = buf + written * 12;
        store_f32le(slot + 0, (float)freq_hz);
        store_f32le(slot + 4, (float)power);
        store_u16le(slot + 8, (uint16_t)result);
        ++written;
    }
}

// ---------------------------------------------------------------------------
// Group 100 encoders
// ---------------------------------------------------------------------------
static int encode_system_version(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 28) return -1;
    std::memset(buf, 0, 28);
    double sjc_fw = 0, driver = 0, fpga = 0, bsp = 0;
    long long proc_id = 0, fpga_type = 0;
    json_find_double(j, "sjc_fw_version", sjc_fw);
    json_find_double(j, "driver_version", driver);
    json_find_double(j, "fpga_version",   fpga);
    json_find_double(j, "bsp_version",    bsp);
    json_find_int(j,    "processor_id",   proc_id);
    json_find_int(j,    "fpga_type_id",   fpga_type);
    store_f32le(buf +  0, (float)sjc_fw);
    store_f32le(buf +  4, (float)driver);
    store_f32le(buf +  8, (float)fpga);
    store_f32le(buf + 12, (float)bsp);
    store_u16le(buf + 16, (uint16_t)proc_id);
    const char* ids_key = std::strstr(j, "\"sjc_rf_tuner_ids\"");
    if (ids_key) {
        const char* arr = std::strchr(ids_key, '[');
        if (arr) { ++arr;
            for (int i = 0; i < 3; ++i) {
                while (*arr == ' ' || *arr == '\t' || *arr == '\n' || *arr == ',') ++arr;
                if (*arr == ']' || !*arr) break;
                char* end; long long v = std::strtoll(arr, &end, 10);
                if (end == arr) break;
                store_u16le(buf + 18 + i * 2, (uint16_t)v); arr = end;
            }
        }
    }
    store_u16le(buf + 24, (uint16_t)fpga_type);
    return 28;
}

static int encode_srx_checksum(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 1000) return -1;
    std::memset(buf, 0, 1000);
    const char* key = std::strstr(j, "\"sjc_fw_checksum_hex\""); if (!key) return 1000;
    const char* c = std::strchr(key, ':'); if (!c) return 1000;
    while (*c && *c != '"') ++c; if (*c != '"') return 1000; ++c;
    size_t idx = 0;
    while (*c && *c != '"' && idx < 1000) {
        int hi = hex_nibble(*c++); if (*c == '"' || !*c) break;
        int lo = hex_nibble(*c++); if (hi < 0 || lo < 0) break;
        buf[idx++] = (uint8_t)((hi << 4) | lo);
    }
    return 1000;
}

static int encode_pbit_status(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 88) return -1;
    std::memset(buf, 0, 88);
    long long v = 0;
    auto gi  = [&](const char* k, int i) { if (json_find_int(j, k, v)) buf[i]      = (uint8_t)v; };
    auto gi2 = [&](const char* k, int i) { if (json_find_int(j, k, v)) buf[76 + i] = (uint8_t)v; };
    gi("fpga_scratch_pad_test_status",    0);  gi("fpga_board_id_read",              1);
    gi("processor_temperature_status",    2);  gi("fan_temperature_status",           3);
    gi("fpga_temperature_status",         4);  gi("power_supply_temperature_status",  5);
    gi("rf_psu_temp_status",              6);  gi("fan_speed_control_sens_status",    7);
    gi("fan_voltage_status",              8);  gi("rf_psu_5v_status",                 9);
    gi("rf_psu_8v_status",               10);  gi("msata_detection_status",           11);
    gi("lo1_pll_lock_status",            12);  gi("lo2_pll_lock_status",              13);
    gi("bite_pll_lock_status",           14);  gi("tuner_detection_status",           15);
    gi("tuner_scratchpad_test_status",   16);  gi("pll_lock_status",                  17);
    gi("adc_bonding_status",             18);  gi("storage_availability_status",      19);
    gi("dac_bonding_status_0",           20);  gi("dac_bonding_status_1",             21);
    gi("digital_5v_status",              24);  gi("digital_3v5_status",               25);
    gi("digital_psu_temp_status",        26);
    encode_bite_path_validation(j, buf + 28);
    gi2("rpe_pll_cfg_status",        0);  gi2("rpe_adc_cfg_status",        1);
    gi2("rpe_dac1_cfg_status",       2);  gi2("rpe_dac2_cfg_status",       3);
    gi2("rpe_dac_atten_cfg_status",  4);  gi2("rpe_tuner_lo1_cfg_status",  5);
    gi2("rpe_tuner_lo2_cfg_status",  6);  gi2("rpe_tuner_bite_cfg_status", 7);
    gi2("rpe_lo_atten_cfg_status",   8);  gi2("rpe_rf_gain_cal_status",    9);
    return 88;
}

static int encode_ibit_status(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 68) return -1;
    std::memset(buf, 0, 68);
    long long v = 0;
    auto gi = [&](const char* k, int i) { if (json_find_int(j, k, v)) buf[i] = (uint8_t)v; };
    gi("pll_lock_status",              0);  gi("adc_bonding_status",           1);
    gi("msata_detection_status",       2);  gi("storage_availability_status",  3);
    gi("tuner_lo1_pll_lock_status",    4);  gi("tuner_lo2_pll_lock_status",    5);
    gi("tuner_bite_pll_lock_status",   6);  gi("adc_link_status",              7);
    gi("tuner_detection_status",      10);  gi("tuner_scratchpad_test_status", 13);
    gi("dac_bonding_status_0",        16);  gi("dac_bonding_status_1",         17);
    encode_bite_path_validation(j, buf + 20);
    return 68;
}

static int encode_temperature(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 36) return -1;
    std::memset(buf, 0, 36);
    double v = 0;
    auto gd = [&](const char* k, int i) { v = 0; json_find_double(j, k, v); store_f32le(buf + i, (float)v); };
    gd("processor_temp_c",  0); gd("psu_temp_c",      4); gd("fan_temp_c",    8);
    gd("rf_psu_temp_c",    12); gd("fpga_temp_c",     16); gd("digital_temp_c",20);
    gd("tuner_temp_c",     24);
    return 36;
}

static int encode_fan_speed_status(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    long long rpm = 0; json_find_int(j, "fan_speed_rpm", rpm);
    store_u32le(buf, (uint32_t)rpm); return 4;
}

static int encode_uart_test(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4); long long v = 0;
    if (json_find_int(j, "expected_data", v)) buf[0] = (uint8_t)v;
    if (json_find_int(j, "observed_data", v)) buf[1] = (uint8_t)v;
    if (json_find_int(j, "result",        v)) buf[2] = (uint8_t)v;
    return 4;
}

static int encode_cbit_status(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 8) return -1;
    std::memset(buf, 0, 8); long long v = 0;
    if (json_find_int(j, "drx_status",             v)) buf[0] = (uint8_t)v;
    if (json_find_int(j, "voltage_status",         v)) buf[1] = (uint8_t)v;
    if (json_find_int(j, "temperature_status",     v)) buf[2] = (uint8_t)v;
    if (json_find_int(j, "tuner_detection_status", v)) buf[4] = (uint8_t)v;
    if (json_find_int(j, "memory_status",          v)) buf[7] = (uint8_t)v;
    return 8;
}

// ---------------------------------------------------------------------------
// Group 101 encoders
// ---------------------------------------------------------------------------
// VU S_HOPPER_DATA = 60 bytes; "hoppers" array key; toa "HH:MM:SS"; active bool.
// VU extension @40-@59: min/max_freq_detected_hz, signal_bw_khz, hop_rate, confidence.
static int encode_fh_detection(const char* j, uint8_t* buf, int max_len) {
    long long hc_ll = 0;
    if (!json_find_int(j, "hopper_count", hc_ll) || hc_ll < 0) return -1;
    int hc = (int)hc_ll;
    if (4 + hc * 60 > max_len) return -1;
    store_u16le(buf + 0, (uint16_t)hc); store_u16le(buf + 2, 0);
    if (hc == 0) return 4;
    const char* hops_key = std::strstr(j, "\"hoppers\""); if (!hops_key) return 4;
    const char* arr = std::strchr(hops_key, '['); if (!arr) return 4;
    const char* p = arr + 1; int written = 0;
    while (written < hc) {
        while (*p && *p != '{' && *p != ']') ++p;
        if (!*p || *p == ']') break;
        const char* os = p; int depth = 1; ++p;
        while (*p && depth > 0) { if (*p == '{') ++depth; else if (*p == '}') --depth; ++p; }
        std::string entry(os, p); const char* e = entry.c_str();
        long long hopper_number = 0, detected_count = 0;
        double min_freq_hz = 0, max_freq_hz = 0, pulse_s = 0, inter_s = 0;
        double power_dbm = 0, snr_db = 0;
        double min_det_hz = 0, max_det_hz = 0, bw_khz = 0, hop_rate = 0, confidence = 0;
        bool active = false;
        json_find_int(e,    "hopper_number",        hopper_number);
        json_find_int(e,    "detected_count",       detected_count);
        json_find_double(e, "min_freq_hz",          min_freq_hz);
        json_find_double(e, "max_freq_hz",          max_freq_hz);
        json_find_double(e, "pulse_length_s",       pulse_s);
        json_find_double(e, "inter_hop_period_s",   inter_s);
        json_find_double(e, "power_dbm",            power_dbm);
        json_find_bool(e,   "active",               active);
        json_find_double(e, "snr_db",               snr_db);
        json_find_double(e, "min_freq_detected_hz", min_det_hz);
        json_find_double(e, "max_freq_detected_hz", max_det_hz);
        json_find_double(e, "signal_bw_khz",        bw_khz);
        json_find_double(e, "hop_rate",             hop_rate);
        json_find_double(e, "confidence",           confidence);
        uint8_t th = 0, tm = 0, ts = 0;
        parse_toa_hms(e, "toa", th, tm, ts);
        uint8_t* slot = buf + 4 + written * 60;
        store_u32le(slot +  0, (uint32_t)hopper_number);
        store_f32le(slot +  4, (float)(min_freq_hz / 1e6));
        store_f32le(slot +  8, (float)(max_freq_hz / 1e6));
        store_f32le(slot + 12, (float)(pulse_s * 1e3));
        store_f32le(slot + 16, (float)(inter_s * 1e3));
        store_u32le(slot + 20, (uint32_t)detected_count);
        slot[24] = th; slot[25] = tm; slot[26] = ts; slot[27] = 0;
        store_f32le(slot + 28, (float)power_dbm);
        store_u16le(slot + 32, active ? 1 : 0);
        store_u16le(slot + 34, 0);
        store_f32le(slot + 36, (float)snr_db);
        store_f32le(slot + 40, (float)(min_det_hz / 1e6));
        store_f32le(slot + 44, (float)(max_det_hz / 1e6));
        store_f32le(slot + 48, (float)bw_khz);
        store_f32le(slot + 52, (float)hop_rate);
        store_f32le(slot + 56, (float)confidence);
        ++written;
    }
    return 4 + written * 60;
}

static int encode_wideband_fft(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 6408) return -1;
    std::memset(buf, 0, 6408);
    long long bin_count = 0, scan_speed = 0;
    json_find_int(j, "fft_bin_count", bin_count);
    json_find_int(j, "scan_speed",    scan_speed);
    store_u32le(buf + 0,    (uint32_t)bin_count);
    store_u32le(buf + 6404, (uint32_t)scan_speed);
    return 6408;
}

// VU 101/70: FFT block (6408B) + ff_count(u32) + "fixed_frequencies" entries (36B each).
// toa/duration as "HH:MM:SS" strings; freq_active as bool.
static int encode_ff_detection(const char* j, uint8_t* buf, int max_len) {
    const int FFT_BLOCK = 4 + 1600 * 4 + 4;
    const int FF_ELEM   = 36;
    long long fc_ll = 0;
    if (!json_find_int(j, "ff_count", fc_ll) || fc_ll < 0) return -1;
    int fc = (int)fc_ll;
    if (FFT_BLOCK + 4 + fc * FF_ELEM > max_len) return -1;
    std::memset(buf, 0, (size_t)FFT_BLOCK);
    long long bin_count = 0, scan_speed = 0;
    json_find_int(j, "fft_bin_count", bin_count);
    json_find_int(j, "scan_speed",    scan_speed);
    store_u32le(buf + 0,    (uint32_t)bin_count);
    store_u32le(buf + 6404, (uint32_t)scan_speed);
    store_u32le(buf + FFT_BLOCK, (uint32_t)fc);
    if (fc == 0) return FFT_BLOCK + 4;
    const char* det = std::strstr(j, "\"fixed_frequencies\""); if (!det) return FFT_BLOCK + 4;
    const char* arr = std::strchr(det, '['); if (!arr) return FFT_BLOCK + 4;
    const char* p = arr + 1; int written = 0;
    while (written < fc) {
        while (*p && *p != '{' && *p != ']') ++p;
        if (!*p || *p == ']') break;
        const char* os = p; int depth = 1; ++p;
        while (*p && depth > 0) { if (*p == '{') ++depth; else if (*p == '}') --depth; ++p; }
        std::string entry(os, p); const char* e = entry.c_str();
        long long active_count = 0;
        double freq_hz = 0, cp = 0, mnp = 0, mxp = 0, snr = 0;
        bool freq_active = false;
        json_find_double(e, "freq_hz",           freq_hz);
        json_find_double(e, "current_power_dbm", cp);
        json_find_int(e,    "active_count",      active_count);
        json_find_double(e, "min_power_dbm",     mnp);
        json_find_double(e, "max_power_dbm",     mxp);
        json_find_bool(e,   "freq_active",       freq_active);
        json_find_double(e, "snr_db",            snr);
        uint8_t th = 0, tm = 0, ts = 0, dh = 0, dm = 0, ds = 0;
        parse_toa_hms(e, "toa",      th, tm, ts);
        parse_toa_hms(e, "duration", dh, dm, ds);
        uint8_t* slot = buf + FFT_BLOCK + 4 + written * FF_ELEM;
        store_f32le(slot +  0, (float)(freq_hz / 1e6));
        store_f32le(slot +  4, (float)cp);
        store_u32le(slot +  8, (uint32_t)active_count);
        store_f32le(slot + 12, (float)mnp);
        store_f32le(slot + 16, (float)mxp);
        slot[20] = th; slot[21] = tm; slot[22] = ts; slot[23] = 0;
        slot[24] = dh; slot[25] = dm; slot[26] = ds; slot[27] = 0;
        store_u16le(slot + 28, freq_active ? 1 : 0);
        store_u16le(slot + 30, 0);
        store_f32le(slot + 32, (float)snr);
        ++written;
    }
    return FFT_BLOCK + 4 + written * FF_ELEM;
}

// VU 101/84: "bursts" array key; toa "HH:MM:SS".
static int encode_burst_detection(const char* j, uint8_t* buf, int max_len) {
    const int ELEM = 24;
    long long bc_ll = 0;
    if (!json_find_int(j, "burst_count", bc_ll) || bc_ll < 0) return -1;
    int bc = (int)bc_ll;
    if (4 + bc * ELEM > max_len) return -1;
    store_u32le(buf, (uint32_t)bc);
    if (bc == 0) return 4;
    const char* det = std::strstr(j, "\"bursts\""); if (!det) return 4;
    const char* arr = std::strchr(det, '['); if (!arr) return 4;
    const char* p = arr + 1; int written = 0;
    while (written < bc) {
        while (*p && *p != '{' && *p != ']') ++p;
        if (!*p || *p == ']') break;
        const char* os = p; int depth = 1; ++p;
        while (*p && depth > 0) { if (*p == '{') ++depth; else if (*p == '}') --depth; ++p; }
        std::string entry(os, p); const char* e = entry.c_str();
        long long active_count = 0;
        double freq_hz = 0, cp = 0, pulse_ms = 0, snr = 0;
        json_find_double(e, "freq_hz",           freq_hz);
        json_find_double(e, "current_power_dbm", cp);
        json_find_double(e, "pulse_length_ms",   pulse_ms);
        json_find_int(e,    "active_count",      active_count);
        json_find_double(e, "snr_db",            snr);
        uint8_t th = 0, tm = 0, ts = 0;
        parse_toa_hms(e, "toa", th, tm, ts);
        uint8_t* slot = buf + 4 + written * ELEM;
        store_f32le(slot +  0, (float)(freq_hz / 1e6));
        store_f32le(slot +  4, (float)cp);
        store_f32le(slot +  8, (float)pulse_ms);
        store_u32le(slot + 12, (uint32_t)active_count);
        slot[16] = th; slot[17] = tm; slot[18] = ts; slot[19] = 0;
        store_f32le(slot + 20, (float)snr);
        ++written;
    }
    return 4 + written * ELEM;
}

// VU 101/88: "fixed_frequencies" array; same 36B layout as FF detection.
static int encode_stop_scan_speed(const char* j, uint8_t* buf, int max_len) {
    const int FF_ELEM = 36;
    long long fc_ll = 0;
    if (!json_find_int(j, "ff_count", fc_ll) || fc_ll < 0) return -1;
    int fc = (int)fc_ll;
    if (4 + fc * FF_ELEM > max_len) return -1;
    store_u32le(buf, (uint32_t)fc);
    if (fc == 0) return 4;
    const char* det = std::strstr(j, "\"fixed_frequencies\""); if (!det) return 4;
    const char* arr = std::strchr(det, '['); if (!arr) return 4;
    const char* p = arr + 1; int written = 0;
    while (written < fc) {
        while (*p && *p != '{' && *p != ']') ++p;
        if (!*p || *p == ']') break;
        const char* os = p; int depth = 1; ++p;
        while (*p && depth > 0) { if (*p == '{') ++depth; else if (*p == '}') --depth; ++p; }
        std::string entry(os, p); const char* e = entry.c_str();
        long long active_count = 0;
        double freq_hz = 0, cp = 0, mnp = 0, mxp = 0, snr = 0;
        bool freq_active = false;
        json_find_double(e, "freq_hz",           freq_hz);
        json_find_double(e, "current_power_dbm", cp);
        json_find_int(e,    "active_count",      active_count);
        json_find_double(e, "min_power_dbm",     mnp);
        json_find_double(e, "max_power_dbm",     mxp);
        json_find_bool(e,   "freq_active",       freq_active);
        json_find_double(e, "snr_db",            snr);
        uint8_t th = 0, tm = 0, ts = 0, dh = 0, dm = 0, ds = 0;
        parse_toa_hms(e, "toa",      th, tm, ts);
        parse_toa_hms(e, "duration", dh, dm, ds);
        uint8_t* slot = buf + 4 + written * FF_ELEM;
        store_f32le(slot +  0, (float)(freq_hz / 1e6));
        store_f32le(slot +  4, (float)cp);
        store_u32le(slot +  8, (uint32_t)active_count);
        store_f32le(slot + 12, (float)mnp);
        store_f32le(slot + 16, (float)mxp);
        slot[20] = th; slot[21] = tm; slot[22] = ts; slot[23] = 0;
        slot[24] = dh; slot[25] = dm; slot[26] = ds; slot[27] = 0;
        store_u16le(slot + 28, freq_active ? 1 : 0);
        store_u16le(slot + 30, 0);
        store_f32le(slot + 32, (float)snr);
        ++written;
    }
    return 4 + written * FF_ELEM;
}

// VU 101/95: raw 6400 bytes (1600 × f32), no count header — inverse of decode_zoom_fft.
static int encode_zoom_fft(const char* j, uint8_t* buf, int max_len) {
    const int TOTAL = 1600 * 4;
    if (max_len < TOTAL) return -1;
    std::memset(buf, 0, (size_t)TOTAL);
    const char* pd = std::strstr(j, "\"power_dbm\""); if (!pd) return TOTAL;
    const char* arr = std::strchr(pd, '['); if (!arr) return TOTAL; ++arr;
    int written = 0;
    while (written < 1600) {
        while (*arr == ' ' || *arr == '\t' || *arr == '\n' || *arr == ',') ++arr;
        if (*arr == ']' || !*arr) break;
        char* end; float v = (float)std::strtod(arr, &end);
        if (end == arr) break;
        store_f32le(buf + written * 4, v); arr = end; ++written;
    }
    return TOTAL;
}

// 101/205 — Get SRx MRx Status response encoder (8 bytes). Per ICD Table 76.
static int encode_srx_mrx_status(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 8) return -1;
    std::memset(buf, 0, 8);
    long long v = 0;
    if (json_find_int(j, "srx_tuned_status",         v)) buf[0] = (uint8_t)v;
    if (json_find_int(j, "mrx_tuned_status",         v)) buf[1] = (uint8_t)v;
    if (json_find_int(j, "tuned_center_freq",        v)) store_u16le(buf + 4, (uint16_t)v);
    if (json_find_int(j, "memory_scan_tuned_status", v)) buf[6] = (uint8_t)v;
    return 8;
}

// ---------------------------------------------------------------------------
// Group 109 encoders
// ---------------------------------------------------------------------------
static int encode_auto_threshold(const char* j, uint8_t* buf, int max_len) {
    const int TOTAL = 4 + 1600 * 4;
    if (max_len < TOTAL) return -1;
    std::memset(buf, 0, (size_t)TOTAL);
    long long bc = 0; json_find_int(j, "bin_count", bc);
    if (bc > 1600) bc = 1600;
    store_u32le(buf, (uint32_t)bc);
    if (bc == 0) return TOTAL;
    const char* pd = std::strstr(j, "\"threshold_dbm\""); if (!pd) return TOTAL;
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
    const char* ks = std::strstr(j, arr_key); if (!ks) return 8;
    const char* arr = std::strchr(ks, '['); if (!arr) return 8;
    const char* p = arr + 1; int written = 0;
    while (written < (int)count) {
        while (*p && *p != '{' && *p != ']') ++p;
        if (!*p || *p == ']') break;
        const char* os = p; int depth = 1; ++p;
        while (*p && depth > 0) { if (*p == '{') ++depth; else if (*p == '}') --depth; ++p; }
        std::string entry(os, p); const char* e = entry.c_str();
        double toi = 0, fihz = 0, pl = 0, pwr = 0; long long bw = 0, fb = 0;
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

static int encode_pdw_channelization(const char* j, uint8_t* buf, int max_len) {
    return encode_channelization(j, "\"channelization_data\"", buf, max_len);
}

// ---------------------------------------------------------------------------
// Group 111 encoders
// ---------------------------------------------------------------------------
// 111/4 — Signal BITE response encoder (12 bytes). Per ICD Table 82.
static int encode_111_signal_bite_resp(const char* j, uint8_t* buf, int max_len) {
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

// VU: freq stored as f64 (MHz) at @0; 16 bytes total.
static int encode_signal_bite_resp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 16) return -1;
    std::memset(buf, 0, 16);
    double freq = 0, power = 0; long long result = 0;
    json_find_double(j, "bite_freq_hz",   freq);
    json_find_double(j, "bite_power_dbm", power);
    json_find_int(j,    "bite_result",    result);
    store_f64le(buf + 0, freq / 1e6);
    store_f32le(buf + 8, (float)power);
    store_u16le(buf + 12, (uint16_t)result);
    return 16;
}

static int encode_module_health(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4); long long drx = 0, tuner = 0;
    json_find_int(j, "drx_health",      drx);
    json_find_int(j, "rf_tuner_health", tuner);
    buf[0] = (uint8_t)drx; buf[1] = (uint8_t)tuner; return 4;
}

static int encode_storage_details(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 20) return -1;
    std::memset(buf, 0, 20);
    long long ds1 = 0, ds2 = 0, ds3 = 0; double avail = 0, tot = 0;
    json_find_int(j,    "disk_space_1",         ds1);
    json_find_int(j,    "disk_space_2",         ds2);
    json_find_int(j,    "disk_space_3",         ds3);
    json_find_double(j, "available_disk_space", avail);
    json_find_double(j, "total_disk_space",     tot);
    buf[0] = (uint8_t)ds1; buf[1] = (uint8_t)ds2; buf[2] = (uint8_t)ds3;
    store_f64le(buf +  4, avail); store_f64le(buf + 12, tot);
    return 20;
}

static int encode_protected_band_list(const char* j, uint8_t* buf, int max_len) {
    long long count = 0; json_find_int(j, "protected_band_count", count);
    if (count < 0) count = 0;
    int total = 4 + (int)count * 8;
    if (total > max_len) return -1;
    std::memset(buf, 0, (size_t)total);
    store_u16le(buf, (uint16_t)count);
    if (count == 0) return 4;
    const char* pd = std::strstr(j, "\"protected_bands\""); if (!pd) return 4;
    const char* arr = std::strchr(pd, '['); if (!arr) return 4;
    const char* p = arr + 1; int written = 0;
    while (written < (int)count) {
        while (*p && *p != '{' && *p != ']') ++p;
        if (!*p || *p == ']') break;
        const char* os = p; int depth = 1; ++p;
        while (*p && depth > 0) { if (*p == '{') ++depth; else if (*p == '}') --depth; ++p; }
        std::string entry(os, p); const char* e = entry.c_str();
        double start = 0, stop = 0;
        json_find_double(e, "start_freq_hz", start);
        json_find_double(e, "stop_freq_hz",  stop);
        uint8_t* slot = buf + 4 + written * 8;
        store_f32le(slot + 0, (float)(start / 1e6));
        store_f32le(slot + 4, (float)(stop  / 1e6));
        ++written;
    }
    return 4 + written * 8;
}

// ---------------------------------------------------------------------------
// Group 106 encoders
// ---------------------------------------------------------------------------
static int encode_stop_jam_response(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    long long v = 0; json_find_int(j, "exciter_retry_count", v); buf[0] = (uint8_t)v; return 4;
}

static int encode_ext_modulation_response(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    long long v = 0; json_find_int(j, "software_buffer_size", v);
    store_u32le(buf, (uint32_t)v); return 4;
}

// ---------------------------------------------------------------------------
// Group 108 encoder
// ---------------------------------------------------------------------------
static int encode_list_jam_report(const char* j, uint8_t* buf, int max_len) {
    long long count = 0; json_find_int(j, "list_jam_freq_count", count);
    if (count < 0) count = 0;
    int total = 4 + (int)count * 8;
    if (total > max_len) return -1;
    std::memset(buf, 0, (size_t)total);
    store_u32le(buf, (uint32_t)count);
    if (count == 0) return 4;
    const char* pd = std::strstr(j, "\"frequencies\""); if (!pd) return 4;
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
        store_u32le(slot + 0, (uint32_t)freq);
        store_u16le(slot + 4, (uint16_t)status);
        ++written;
    }
    return 4 + written * 8;
}

// ---------------------------------------------------------------------------
// Group 112 encoders
// ---------------------------------------------------------------------------
static int encode_asu_sdu_response(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    long long err = 0; json_find_int(j, "error_value", err);
    store_i16le(buf, (int16_t)err); return 4;
}

static int encode_trsdu_status(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    long long v = 0; json_find_int(j, "trsdu_health_status", v); buf[0] = (uint8_t)v; return 4;
}


// ---------------------------------------------------------------------------
// Group 200 encoders
// ---------------------------------------------------------------------------
static int encode_ext_modulation_buffer_size(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    long long v = 0; json_find_int(j, "software_buffer_size", v);
    store_u32le(buf, (uint32_t)v); return 4;
}

static int encode_hpasu_health_status_resp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    long long v = 0; json_find_int(j, "hpasu_health_status", v);
    buf[0] = (uint8_t)v; return 4;
}

static int encode_pa_sdu_health_status(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 8) return -1;
    std::memset(buf, 0, 8);
    long long v = 0;
    if (json_find_int(j, "pa1_health_status", v)) buf[0] = (uint8_t)v;
    if (json_find_int(j, "pa2_health_status", v)) buf[1] = (uint8_t)v;
    if (json_find_int(j, "pa3_health_status", v)) buf[2] = (uint8_t)v;
    if (json_find_int(j, "pa4_health_status", v)) buf[3] = (uint8_t)v;
    if (json_find_int(j, "sdu_health_status",  v)) buf[4] = (uint8_t)v;
    return 8;
}

static int encode_vu_jam_ack(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 8) return -1;
    std::memset(buf, 0, 8);
    long long jam_id = 0; bool jam_active = false;
    json_find_int(j,  "jam_id",     jam_id);
    json_find_bool(j, "jam_active", jam_active);
    store_u16le(buf + 0, (uint16_t)jam_id);
    store_u16le(buf + 4, jam_active ? 1 : 0);
    return 8;
}

// ---------------------------------------------------------------------------
// MRx Group 1 encoders (VU field names from decode_mrx_* functions)
// ---------------------------------------------------------------------------
static int encode_mrx_system_version(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 16) return -1;
    std::memset(buf, 0, 16);
    double fw = 0, drv = 0, fpga = 0; long long tuner_id = 0;
    json_find_double(j, "monitoring_fw_version", fw);
    json_find_double(j, "driver_version",        drv);
    json_find_double(j, "fpga_version",          fpga);
    json_find_int(j,    "mrx_tuner_id",          tuner_id);
    store_f32le(buf + 0, (float)fw);
    store_f32le(buf + 4, (float)drv);
    store_f32le(buf + 8, (float)fpga);
    store_u16le(buf + 12, (uint16_t)tuner_id);
    return 16;
}

static int encode_mrx_checksum(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 1024) return -1;
    std::memset(buf, 0, 1024);
    const char* key = std::strstr(j, "\"mrx_fw_checksum_hex\""); if (!key) return 1024;
    const char* c = std::strchr(key, ':'); if (!c) return 1024;
    while (*c && *c != '"') ++c; if (*c != '"') return 1024; ++c;
    size_t idx = 0;
    while (*c && *c != '"' && idx < 1024) {
        int hi = hex_nibble(*c++); if (*c == '"' || !*c) break;
        int lo = hex_nibble(*c++); if (hi < 0 || lo < 0) break;
        buf[idx++] = (uint8_t)((hi << 4) | lo);
    }
    return 1024;
}

static int encode_mrx_pbit_status(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 120) return -1;
    std::memset(buf, 0, 120);
    long long v = 0;
    auto gi = [&](const char* k, int i) { if (json_find_int(j, k, v)) buf[i] = (uint8_t)v; };
    gi("fpga_scratch_pad_test",  0);  gi("fpga_board_id_status",    1);
    gi("processor_temp_status",  2);  gi("fan_temp_status",          3);
    gi("fpga_temp_status",       4);  gi("rf_psu_temp_status",       6);
    gi("fan_speed_ctrl_status",  7);  gi("fan_voltage_status",       8);
    gi("rfsu_5v_status",         9);  gi("rfsu_8v5_status",         10);
    gi("msata_detection_status",11);  gi("lo1_pll_lock_status",     12);
    gi("lo2_pll_lock_status",   13);  gi("bite_pll_lock_status",    14);
    gi("tuner_detection_status",15);  gi("tuner_scratchpad_test",   18);
    gi("pll_lock_status",       21);  gi("adc_bonding_status",      22);
    gi("storage_avail_status",  23);
    return 120;
}

static int encode_mrx_ibit_status(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 112) return -1;
    std::memset(buf, 0, 112);
    long long v = 0;
    auto gi = [&](const char* k, int i) { if (json_find_int(j, k, v)) buf[i] = (uint8_t)v; };
    gi("pll_lock_status",         0);  gi("adc_bonding_status",      1);
    gi("msata_detection_status",  2);  gi("storage_avail_status",    3);
    gi("tuner_lo1_pll_lock",      4);  gi("tuner_lo2_pll_lock",      5);
    gi("tuner_bite_pll_lock",     6);  gi("adc1_link_status",        7);
    gi("tuner_detection_status", 10);  gi("tuner_scratch_pad_test", 13);
    return 112;
}

static int encode_mrx_temperature(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 36) return -1;
    static const char* NAMES[] = {
        "bpt_temp_c", "psu_8156_temp_c", "tuner_temp_c", "psu_7255_temp_c",
        "processor_temp_c", "psu_temp_c", "control_board_temp_c",
        "rf_psu_temp_c", "fpga_temp_c"
    };
    for (int i = 0; i < 9; ++i) {
        double v = 0; json_find_double(j, NAMES[i], v); store_f32le(buf + i * 4, (float)v);
    }
    return 36;
}

static int encode_fan_speed_resp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    long long v = 0; json_find_int(j, "fan_speed_rpm", v);
    store_i32le(buf, (int32_t)v); return 4;
}

static int encode_uart_test_resp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    long long v = 0;
    if (json_find_int(j, "expected_data", v)) buf[0] = (uint8_t)v;
    if (json_find_int(j, "observed_data", v)) buf[1] = (uint8_t)v;
    bool pass = false; json_find_bool(j, "uart_pass", pass); buf[2] = pass ? 1 : 0;
    return 4;
}

// ---------------------------------------------------------------------------
// MRx Group 3 encoders
// ---------------------------------------------------------------------------
static int encode_attenuation_details(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    long long v = 0;
    if (json_find_int(j, "rf_attenuation",     v)) buf[0] = (uint8_t)v;
    if (json_find_int(j, "if_attenuation",     v)) buf[1] = (uint8_t)v;
    if (json_find_int(j, "agc_running_status", v)) store_u16le(buf + 2, (uint16_t)v);
    return 4;
}

static int encode_mrx_srx_tuning_info(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 8) return -1;
    std::memset(buf, 0, 8);
    long long v = 0;
    if (json_find_int(j, "srx_tuned_status",         v)) buf[0] = (uint8_t)v;
    if (json_find_int(j, "mrx_tuned_status",         v)) buf[1] = (uint8_t)v;
    if (json_find_int(j, "srx_scan_mode_status",     v)) store_u16le(buf + 2, (uint16_t)v);
    if (json_find_int(j, "tuned_center_freq",        v)) store_u16le(buf + 4, (uint16_t)v);
    if (json_find_int(j, "memory_scan_tuned_status", v)) buf[6] = (uint8_t)v;
    if (json_find_int(j, "bite_selection",           v)) buf[7] = (uint8_t)v;
    return 8;
}

static int encode_mrx_board_count_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    long long count = 0, tuner_id = 0;
    json_find_int(j, "available_board_count",  count);
    json_find_int(j, "available_mrx_tuner_id", tuner_id);
    store_u16le(buf + 0, (uint16_t)count);
    store_u16le(buf + 2, (uint16_t)tuner_id);
    return 4;
}

// VU channel_status is a plain array [v0..v7]; both 3/18 and 3/22 use the same key.
static int encode_mrx_channel_status(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 16) return -1;
    std::memset(buf, 0, 16);
    const char* pd = std::strstr(j, "\"channel_status\""); if (!pd) return 16;
    const char* arr = std::strchr(pd, '['); if (!arr) return 16; ++arr;
    for (int i = 0; i < 8; ++i) {
        while (*arr == ' ' || *arr == '\t' || *arr == '\n' || *arr == ',') ++arr;
        if (*arr == ']' || !*arr) break;
        char* end; long long v = std::strtoll(arr, &end, 10);
        if (end == arr) break;
        store_u16le(buf + i * 2, (uint16_t)v); arr = end;
    }
    return 16;
}

// ---------------------------------------------------------------------------
// MRx Group 4 encoders
// ---------------------------------------------------------------------------
static int encode_mrx_audio_data_rsp(const char* j, uint8_t* buf, int max_len) {
    long long audio_size = 0;
    json_find_int(j, "audio_data_size_samples", audio_size);
    if (audio_size < 0) audio_size = 0;
    if (audio_size > 262144) audio_size = 262144;
    int total = 4 + (int)audio_size * 2;
    if (total > max_len) return -1;
    std::memset(buf, 0, (size_t)total);
    store_u32le(buf, (uint32_t)audio_size);
    return total;
}

static int encode_mrx_iq_status(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4); long long v = 0;
    if (json_find_int(j, "channel_number",   v)) buf[0] = (uint8_t)v;
    if (json_find_int(j, "narrow_iq_status", v)) buf[1] = (uint8_t)v;
    if (json_find_int(j, "wide_iq_status",   v)) buf[2] = (uint8_t)v;
    return 4;
}

static int encode_mrx_iq_log_stop(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 132) return -1;
    std::memset(buf, 0, 132);
    long long ch = 0; json_find_int(j, "channel_number", ch); store_u16le(buf, (uint16_t)ch);
    const char* key = std::strstr(j, "\"file_path\""); if (!key) return 132;
    const char* c = std::strchr(key, ':'); if (!c) return 132;
    while (*c && *c != '"') ++c; if (*c != '"') return 132; ++c;
    size_t idx = 0;
    while (*c && *c != '"' && idx < 127) buf[4 + idx++] = (uint8_t)*c++;
    return 132;
}

static int encode_mrx_mem_scan_data_rsp(const char* j, uint8_t* buf, int max_len) {
    long long count = 0, scan_speed = 0;
    json_find_int(j, "available_count",   count);
    json_find_int(j, "scan_speed_ch_sec", scan_speed);
    if (count < 0) count = 0; if (count > 10000) count = 10000;
    int total = 8 + (int)count * 20;
    if (total > max_len) return -1;
    std::memset(buf, 0, (size_t)total);
    store_u32le(buf + 0, (uint32_t)count);
    store_u16le(buf + 4, (uint16_t)scan_speed);
    if (count == 0) return 8;
    const char* pd = std::strstr(j, "\"scan_results\""); if (!pd) return 8;
    const char* arr = std::strchr(pd, '['); if (!arr) return 8;
    const char* p = arr + 1; int written = 0;
    while (written < (int)count) {
        while (*p && *p != '{' && *p != ']') ++p;
        if (!*p || *p == ']') break;
        const char* os = p; int depth = 1; ++p;
        while (*p && depth > 0) { if (*p == '{') ++depth; else if (*p == '}') --depth; ++p; }
        std::string entry(os, p); const char* e = entry.c_str();
        double power = 0, freq = 0; long long ms = 0, bw = 0;
        json_find_double(e, "power_dbm",     power);
        json_find_double(e, "freq_hz",       freq);
        json_find_int(e,    "millisecond",   ms);
        json_find_int(e,    "bandwidth_code",bw);
        uint8_t th = 0, tm = 0, ts = 0;
        parse_toa_hms(e, "toa", th, tm, ts);
        uint8_t* slot = buf + 8 + written * 20;
        store_f32le(slot +  0, (float)power);
        store_f64le(slot +  4, freq / 1e6);
        slot[12] = th; slot[13] = tm; slot[14] = ts;
        store_u16le(slot + 16, (uint16_t)ms);
        store_u16le(slot + 18, (uint16_t)bw);
        ++written;
    }
    return 8 + written * 20;
}

static int encode_smart_mem_scan_data(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 16) return -1;
    std::memset(buf, 0, 16);
    double fv = 0.0; json_find_double(j, "frequency_value", fv); store_f64le(buf + 0, fv);
    long long v = 0;
    if (json_find_int(j, "channel_number", v)) store_u16le(buf + 8, (uint16_t)v);
    double amp = 0.0; json_find_double(j, "amplitude", amp); store_f32le(buf + 12, (float)amp);
    return 16;
}

static int encode_mrx_ddc_fft_rsp(const char* j, uint8_t* buf, int max_len) {
    long long bc = 0; json_find_int(j, "bin_count", bc);
    if (bc < 0) bc = 0; if (bc > 4096) bc = 4096;
    int total = 4 + (int)bc * 4;
    if (total > max_len) return -1;
    std::memset(buf, 0, (size_t)total);
    store_u16le(buf, (uint16_t)bc);
    if (bc == 0) return total;
    const char* pd = std::strstr(j, "\"fft_data\""); if (!pd) return total;
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

static int encode_mrx_optical_port_status_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 12) return -1;
    std::memset(buf, 0, 12); long long v = 0;
    auto gu = [&](const char* k, int i) { if (json_find_int(j, k, v)) store_u16le(buf + i, (uint16_t)v); };
    gu("port_number", 0); gu("port_id", 2); gu("port_alive", 4);
    gu("already_trans", 6); gu("able_to_start", 8);
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
    if (pd) { const char* arr = std::strchr(pd, '['); if (arr) { ++arr;
        for (int i = 0; i < 9; ++i) {
            while (*arr == ' ' || *arr == '\t' || *arr == '\n' || *arr == ',') ++arr;
            if (*arr == ']' || !*arr) break;
            char* end; long long v = std::strtoll(arr, &end, 10);
            if (end == arr) break;
            store_u16le(buf + 4 + i * 2, (uint16_t)v); arr = end;
        }
    } }
    return 24;
}

// ---------------------------------------------------------------------------
// MRx Group 7 encoder
// ---------------------------------------------------------------------------
// VU: freq as f64 (MHz) at @0; 16 bytes total.
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
    bool is_ack = false;

    typedef int (*EncFn)(const char*, uint8_t*, int);
    EncFn fn = nullptr;

    if (group == 100) {
        switch (unit) {
            case  2: fn = encode_system_version;   break;
            case  4: fn = encode_srx_checksum;     break;
            case  6: fn = encode_pbit_status;      break;
            case  8: fn = encode_ibit_status;      break;
            case 10: fn = encode_temperature;      break;
            case 14: fn = encode_fan_speed_status; break;
            case 18: fn = encode_uart_test;        break;
            case 26: fn = encode_cbit_status;      break;
            default: break;
        }
    } else if (group == 101) {
        switch (unit) {
            case  26: is_ack = true; break; // Set Threshold ACK
            case  28: is_ack = true; break; // Set Resolution ACK
            case  32: is_ack = true; break; // Start Follow-On Jam ACK
            case  34: is_ack = true; break; // Stop operation ACK
            case  38: is_ack = true; break; // Configure Detection ACK
            case  40: fn = encode_fh_detection;    break;
            case  44: fn = encode_wideband_fft;    break;
            case  48: is_ack = true; break; // Set Pulse Range ACK
            case  56: is_ack = true; break; // Set Min Hops ACK
            case  64: is_ack = true; break; // Set Tracking Config ACK
            case  70: fn = encode_ff_detection;    break;
            case  74: is_ack = true; break; // Start List Jamming ACK
            case  76: is_ack = true; break; // Stop List Jamming ACK
            case  80: is_ack = true; break; // Send ECM Reports ACK
            case  84: fn = encode_burst_detection; break;
            case  86: is_ack = true; break; // Start Scan Speed ACK
            case  88: fn = encode_stop_scan_speed; break;
            case  93: is_ack = true; break; // Start Responsive Sweep Jam ACK
            case  95: fn = encode_zoom_fft;        break;
            case 101: is_ack = true; break; // Set Flatness Mode ACK
            case 103: is_ack = true; break; // Set Integration Time ACK
            case 105: is_ack = true; break; // Set Multi FH Mode ACK
            case 107: is_ack = true; break; // Set Narrow Band FH ACK
            case 141: is_ack = true; break; // Configure Center Frequency ACK
            case 159: is_ack = true; break; // Terminate FFT Thread ACK
            case 161: is_ack = true; break; // Spectrum Acq Params ACK
            case 163: is_ack = true; break; // AGC Enable ACK
            case 165: is_ack = true; break; // Zoom-Report Enable ACK
            case 175: is_ack = true; break; // Multiple FH Split Enable ACK
            case 177: is_ack = true; break; // Set Burst Pulse Range ACK
            case 179: is_ack = true; break; // Signal Sidelobe Enable ACK
            case 183: is_ack = true; break; // Wideband Pulse Agility Enable ACK
            case 185: is_ack = true; break; // System Reboot ACK
            case 187: is_ack = true; break; // (101/187) ACK
            case 201: is_ack = true; break; // Engage Center Frequency ACK
            case 203: is_ack = true; break; // Disengage Center Frequency ACK
            case 205: fn = encode_srx_mrx_status; break;
            default: break;
        }
    } else if (group == 109) {
        switch (unit) {
            case 12: is_ack = true;                  break; // Set Date/Time ACK
            case 16: fn = encode_auto_threshold;     break;
            case 18: fn = encode_pdw_channelization; break;
            default: break;
        }
    } else if (group == 111) {
        switch (unit) {
            case  4: fn = encode_111_signal_bite_resp; break;
            case  6: is_ack = true;                    break; // Reference Input ACK
            case  8: fn = encode_module_health;        break;
            case 10: is_ack = true;                    break; // Send Protected Scan List ACK
            case 14: is_ack = true;                    break; // Protected Scan Enable ACK
            case 16: fn = encode_pdw_channelization;   break;
            case 18: is_ack = true;                    break; // FH Split band Enable ACK
            case 20: is_ack = true;                    break; // FH Split band Freq ACK
            case 22: fn = encode_signal_bite_resp;     break;
            case 24: is_ack = true;                    break; // Protected-Band Spectrum ACK
            case 26: fn = encode_storage_details;      break;
            case 28: fn = encode_protected_band_list;  break;
            case 30: is_ack = true;                    break; // Auto Threshold Enable ACK
            case 32:  is_ack = true;                    break; // Hopper Channelization Enable ACK
            case 211: is_ack = true;                    break; // VUHF Wideband Enable ACK (from 101/210 cmd)
            default: break;
        }
    } else if (group == 106) {
        switch (unit) {
            case  2: is_ack = true;                       break; // Start Immediate Jam ACK
            case  4: is_ack = true;                       break; // Generate TDM ACK
            case  6: is_ack = true;                       break; // Generate FDM ACK
            case 10: fn = encode_stop_jam_response;       break;
            case 22: is_ack = true;                       break; // Enable PA ACK
            case 40: fn = encode_ext_modulation_response; break;
            case 42: is_ack = true;                       break; // Generate Sweep ACK
            case 46: is_ack = true;                       break; // Enable PA SDU ACK
            case 50: is_ack = true;                       break; // Configure Prog Exciter ACK
            case 56: is_ack = true;                       break; // Generate Comb Noise ACK
            default: break;
        }
    } else if (group == 108) {
        switch (unit) {
            case 6: fn = encode_list_jam_report; break;
            default: break;
        }
    } else if (group == 112) {
        switch (unit) {
            case  2: fn = encode_asu_sdu_response;   break;
            case  4: fn = encode_trsdu_status;       break;
            case  6: is_ack = true;                  break; // Start/Stop Detection ACK
            case 38: is_ack = true;                  break; // PA Freq Config ACK
            default: break;
        }
    } else if (group == 200) {
        switch (unit) {
            case 2:
                fn = encode_vu_jam_ack; break;
            case 4: is_ack = true; break; // 200/3 ACK
            case 6: fn = encode_ext_modulation_buffer_size; break;
            case 8: is_ack = true; break; // Exciter Prog Noise ACK
            case 10: is_ack = true; break; // ECM Report State ACK
            case 12: is_ack = true; break; // List Fixed Freq Jam ACK
            case 14: is_ack = true; break; // 200/13 ACK
            case 18: is_ack = true; break; // Wide Band Tracking ACK
            case 20: is_ack = true; break; // 200/19 ACK
            case 22: is_ack = true; break; // Responsive Sweep Jam ACK
            case 42: fn = encode_hpasu_health_status_resp;  break;
            case 55: fn = encode_pa_sdu_health_status;      break;
            case 57: is_ack = true;                         break; // PA Soft Reboot ACK
            default: break;
        }
    } else if (group == 1) {   // MRx diagnostics
        switch (unit) {
            case  2: fn = encode_mrx_system_version; break;
            case  4: fn = encode_mrx_checksum;       break;
            case  6: fn = encode_mrx_pbit_status;    break;
            case  8: fn = encode_mrx_ibit_status;    break;
            case 10: fn = encode_mrx_temperature;    break;
            case 14: fn = encode_fan_speed_resp;     break;
            case 18: fn = encode_uart_test_resp;     break;
            case 26: fn = encode_cbit_status;        break; // CBIT Status
            case 34: is_ack = true;                  break; // Close All Channels ACK
            default: break;
        }
    } else if (group == 3) {   // MRx RF board
        switch (unit) {
            case  2: fn = encode_mrx_board_count_rsp; break;
            case 18: {
                plen = encode_mrx_channel_status(kwargs_json, payload, MAX_PAYLOAD);
                if (plen < 0) { std::free(payload); return -1; }
                break;
            }
            case 20: is_ack = true; break; // Write Channel ACK
            case 22: {
                plen = encode_mrx_channel_status(kwargs_json, payload, MAX_PAYLOAD);
                if (plen < 0) { std::free(payload); return -1; }
                break;
            }
            case 24: fn = encode_mrx_srx_tuning_info; break;
            case 26: fn = encode_cbit_status;         break; // MRx CBIT — same 8B layout
            default: break;
        }
    } else if (group == 4) {   // MRx data acquisition
        switch (unit) {
            case  6: is_ack = true;                           break; // Set Threshold ACK
            case  8: fn = encode_mrx_audio_data_rsp;          break;
            case 10: is_ack = true;                           break; // Audio Start Play ACK
            case 12: is_ack = true;                           break; // Audio Stop Play ACK
            case 16: is_ack = true;                           break; // Audio FIFO Reset ACK
            case 18: is_ack = true;                           break; // Demod/BW Selection ACK
            case 24: fn = encode_mrx_iq_status;               break;
            case 26: fn = encode_mrx_iq_log_stop;             break;
            case 34: fn = encode_mrx_iq_status;               break;
            case 36: is_ack = true;                           break; // Stop IQ Streaming ACK
            case 40: is_ack = true;                           break; // Configure Memory Scan ACK
            case 42: fn = encode_mrx_mem_scan_data_rsp;       break;
            case 44: fn = encode_mrx_ddc_fft_rsp;             break;
            case 54: is_ack = true;                           break; // Channel Engagement ACK
            case 58: is_ack = true;                           break; // Stop Memory Scan ACK
            case 60: is_ack = true;                           break; // Configure Memory Scan ACK
            case 62: fn = encode_smart_mem_scan_data;         break;
            case 64: is_ack = true;                           break; // Stop Smart Memory Scan ACK
            case 66: is_ack = true;                           break; // Start Optical IQ ACK
            case 68: is_ack = true;                           break; // Stop Optical IQ ACK
            case 70: fn = encode_mrx_optical_port_status_rsp; break;
            case 72: fn = encode_mrx_optical_ip_rsp;          break;
            default: break;
        }
    } else if (group == 5) {   // MRx RF control
        switch (unit) {
            case  2: is_ack = true;                    break; // Set Center Freq ACK
            case  4: is_ack = true;                    break; // Attenuation Select ACK
            case 14: fn = encode_attenuation_details;  break;
            default: break;
        }
    } else if (group == 6) {   // MRx GO2Mon + FH Monitoring (ACKs only)
        switch (unit) {
            case 8: case 10: case 12: case 14: case 16: is_ack = true; break;
            default: break;
        }
    } else if (group == 7) {   // MRx signal BITE and RF control
        switch (unit) {
            case  2: fn = encode_mrx_signal_bite_rsp; break;
            case  4: is_ack = true; break; // BITE/ANT Select ACK
            case  6: is_ack = true; break; // Ref Source ACK
            case 10: is_ack = true; break; // AFC Select ACK
            case 12: is_ack = true; break; // RF Squelch ACK
            case 14: is_ack = true; break; // IQ Socket Connect ACK
            case 16: is_ack = true; break; // IQ Socket Close ACK
            case 18: is_ack = true; break; // Spectrum Avg ACK
            case 20: is_ack = true; break; // RF AGC ACK
            case 22: is_ack = true; break; // Audio Squelch ACK
            case 24: is_ack = true; break; // Set Date/Time ACK
            default: break;
        }
    }

    if (fn) {
        plen = fn(kwargs_json, payload, MAX_PAYLOAD);
        if (plen < 0) { std::free(payload); return -1; }
    } else if (!is_ack) {
        const char* ph = std::strstr(kwargs_json, "\"payload_hex\"");
        if (ph) {
            const char* q = std::strchr(ph, ':');
            if (q) { q = std::strchr(q, '"'); }
            if (q) { ++q;
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
