// drs-bridge/parsers/dp_ecm/src/dp_ecm_vu_parser.cpp
//
// DP-ECM-1074 VU (VHF/UHF/SHF) Receiver/Processor/Exciter parser DLL.
// Hardware band: 30 MHz – 6000 MHz.  Protocol doc: DP-ECM-1074-6000-V1-ICD-0V04.
// Implements the 4-symbol ABI (sdfc_abi.h) on top of the shared frame core.
//
// Increment 2 scope (cumulative):
//   Group 100 (SJC diagnostics) — full response decoders per ICD §3:
//     100/2  — Get System Version    (20 bytes)
//     100/4  — SRx Checksum          (1024 bytes)
//     100/6  — PBIT Status           (24 bytes)
//     100/8  — IBIT Status           (8 bytes)
//     100/10 — Temperature Status    (24 bytes, 6 floats)
//     100/12 — Set Fan Speed ACK     (0 bytes, status only)
//     100/14 — Fan Speed Status      (4 bytes)
//     100/16 — Ethernet Test Status  (12 bytes)
//     100/18 — UART Test Status      (4 bytes)
//     100/22 — Fan Voltage Status    (24 bytes, 6 floats)
//     100/24 — PPS Test Status       (16 bytes)
//     100/26 — RS422 Test Status     (16 bytes, 4 x 4-byte group)
//     100/28 — FPGA Temperature      (4 bytes)
//     100/30 — CBIT Status           (4 bytes)
//   Group 101 (SJC detection) — commands + responses per ICD §3.2.1.2:
//     101/25 cmd — Set Threshold        (8 bytes: channel + threshold dBm)
//     101/26 rsp — Set Threshold ACK    (0 bytes)
//     101/27 cmd — Set Resolution       (4 bytes: channel + resolution enum)
//     101/28 rsp — Set Resolution ACK   (0 bytes)
//     101/37 cmd — Configure Detection  (12 bytes: freq range, atten, mode, AGC)
//     101/38 rsp — Configure Detection ACK (0 bytes)
//     101/40 rsp — FH Detection         (4 + 40*count)
//     101/47 cmd — Set Pulse Range      (16 bytes: 4 floats ms)
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

// 100/2 — Get System Version Details (20 bytes).
// Per ICD Table 4:
//   @0  SJC FW Version    (float,  4B)
//   @4  Driver Version    (float,  4B)
//   @8  FPGA Version      (float,  4B)
//   @12 Processor ID      (uint16, 2B)
//   @14 SJC RF Tuner ID   (uint16, 2B)
//   @16 FPGA Type ID      (uint16, 2B)
//   @18 Reserved          (uint16, 2B)
static void decode_system_version(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 20) { w.key_str("warning", "system_version payload < 20 bytes"); return; }
    w.key_double("sjc_fw_version",    static_cast<double>(load_f32le(p +  0)));
    w.key_double("driver_version",    static_cast<double>(load_f32le(p +  4)));
    w.key_double("fpga_version",      static_cast<double>(load_f32le(p +  8)));
    w.key_uint("processor_id",        load_u16le(p + 12));
    w.key_uint("sjc_rf_tuner_id",     load_u16le(p + 14));
    w.key_uint("fpga_type_id",        load_u16le(p + 16));
}

// 100/4 — Get SRx Checksum Details (1024 bytes).
// Per ICD Table 6: SJC FW Checksum (char[1024]).
// Emitted as hex; the Python consumer can decode if ASCII.
static void decode_srx_checksum(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 1024) { w.key_str("warning", "srx_checksum payload < 1024 bytes"); return; }
    w.key_str("sjc_fw_checksum_hex", to_hex(p, 1024));
}

// 100/6 — PBIT Status (24 bytes).
// Per ICD Table 8: 21 status fields (uint8 each) + 3 reserved bytes.
// 1 = PASS, 0 = FAIL.
static void decode_pbit_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 24) { w.key_str("warning", "pbit_status payload < 24 bytes"); return; }
    w.key_uint("combined_drx_status",           p[0]);
    w.key_uint("combined_temperature_status",   p[1]);
    w.key_uint("combined_voltage_status",       p[2]);
    w.key_uint("combined_msata_status",         p[3]);
    w.key_uint("fpga_scratch_pad_test",         p[4]);
    w.key_uint("fpga_board_id",                 p[5]);
    w.key_uint("fpga_init_test",                p[6]);
    w.key_uint("fpga_temperature_status",       p[7]);
    w.key_uint("rf_psu_temp_sensor",            p[8]);
    w.key_uint("rf_psu_5v_monitor_status",      p[9]);
    w.key_uint("rf_psu_8v5_monitor_status",     p[10]);
    w.key_uint("fan_voltage_monitor_status",    p[11]);
    w.key_uint("digital_5v_monitor_status",     p[12]);
    w.key_uint("digital_3v5_monitor_status",    p[13]);
    w.key_uint("digital_psu_temp_status",       p[14]);
    w.key_uint("msata_detection_status",        p[15]);
    w.key_uint("storage_avail_check_status",    p[16]);
    w.key_uint("fan_speed_ctrl_sensor_test",    p[17]);
    w.key_uint("rf_tuner_health_status",        p[18]);
    w.key_uint("drx_pll_health_status",         p[19]);
    w.key_uint("fan_temperature_status",        p[20]);
    // p[21..23] reserved
}

// 100/8 — IBIT Status (8 bytes).
// Per ICD Table 10: 7 status fields + 1 reserved byte.
// 1 = PASS, 0 = FAIL.
static void decode_ibit_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "ibit_status payload < 8 bytes"); return; }
    w.key_uint("combined_drx_status",   p[0]);
    w.key_uint("storage_health_status", p[1]);
    w.key_uint("drx_pll_health_test",   p[2]);
    w.key_uint("rf_tuner_health_test",  p[3]);
    w.key_uint("drx_adc_health_test",   p[4]);
    w.key_uint("msata_rw_test",         p[5]);
    w.key_uint("storage_avail_check",   p[6]);
    // p[7] reserved
}

// 100/10 — Temperature Status (24 bytes, 6 floats).
// Per ICD Table 16: all temperatures in degrees Celsius.
//   @0  Processor Temperature  (float)
//   @4  Power Supply Temp      (float)
//   @8  Fan Temperature        (float)
//   @12 RF PSU Temperature     (float)
//   @16 Digital Temperature    (float)
//   @20 FPGA Temperature       (float)
static void decode_temperature(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 24) { w.key_str("warning", "temperature payload < 24 bytes"); return; }
    w.key_double("processor_temp_c",   static_cast<double>(load_f32le(p +  0)));
    w.key_double("psu_temp_c",         static_cast<double>(load_f32le(p +  4)));
    w.key_double("fan_temp_c",         static_cast<double>(load_f32le(p +  8)));
    w.key_double("rf_psu_temp_c",      static_cast<double>(load_f32le(p + 12)));
    w.key_double("digital_temp_c",     static_cast<double>(load_f32le(p + 16)));
    w.key_double("fpga_temp_c",        static_cast<double>(load_f32le(p + 20)));
}

// 100/12 — Set Fan Speed ACK (0 bytes payload, status only).
// Nothing to decode in the payload; status field in the frame header carries result.

// 100/14 — Fan Speed Status (4 bytes).
// Per ICD Table 21: Fan Speed in RPM (uint32).
static void decode_fan_speed_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "fan_speed_status payload < 4 bytes"); return; }
    w.key_uint("fan_speed_rpm", load_u32le(p + 0));
}

// 100/16 — Ethernet Test Status (12 bytes).
// Per ICD Table 24: TX Data, RX Data, Result (uint32 each).
// Result: 1 = PASS, 0 = FAIL.
static void decode_ethernet_test(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 12) { w.key_str("warning", "ethernet_test payload < 12 bytes"); return; }
    w.key_uint("tx_data", load_u32le(p + 0));
    w.key_uint("rx_data", load_u32le(p + 4));
    w.key_uint("result",  load_u32le(p + 8));
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

// 20 response id is not there atleast in the sequence manner

// 100/22 — Read Fan Voltage Status (24 bytes, 6 floats).
// Per ICD Table 29: all voltages in volts.
//   @0  Fan ADC Voltage  (float)
//   @4  RF 1 Voltage     (float)
//   @8  RF 2 Voltage     (float)
//   @12 RF 3 Voltage     (float)
//   @16 Digital 5V       (float)
//   @20 Digital 3.3V     (float)
static void decode_fan_voltage(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 24) { w.key_str("warning", "fan_voltage payload < 24 bytes"); return; }
    w.key_double("fan_adc_voltage_v",  static_cast<double>(load_f32le(p +  0)));
    w.key_double("rf1_voltage_v",      static_cast<double>(load_f32le(p +  4)));
    w.key_double("rf2_voltage_v",      static_cast<double>(load_f32le(p +  8)));
    w.key_double("rf3_voltage_v",      static_cast<double>(load_f32le(p + 12)));
    w.key_double("digital_5v_v",       static_cast<double>(load_f32le(p + 16)));
    w.key_double("digital_3v3_v",      static_cast<double>(load_f32le(p + 20)));
}

// 100/24 — PPS Test Status (16 bytes).
// Per ICD Table 32:
//   @0  ON Period   (uint32)
//   @4  OFF Period  (uint32)
//   @8  PPS Status  (uint32)
//   @12 Result      (uint8) — 1 = PASS, 0 = FAIL
//   @13 Reserved x3 (uint8)
static void decode_pps_test(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) { w.key_str("warning", "pps_test payload < 16 bytes"); return; }
    w.key_uint("on_period",  load_u32le(p + 0));
    w.key_uint("off_period", load_u32le(p + 4));
    w.key_uint("pps_status", load_u32le(p + 8));
    w.key_uint("result",     p[12]);
    //reseved 3 B
}

// 100/26 — RS422 Test Status (16 bytes).
// Per ICD Tables 34–35: 4 instances of S_ATP_RS422_GROUP_DATA (4 bytes each).
// Per-group layout (corrected per ICD; the "1*8" column count in Table 34 is a typo):
//   @0 Write Data   (uint8)
//   @1 Read Data    (uint8)
//   @2 Test Result  (uint8) — 1 = PASS, 0 = FAIL
//   @3 Reserved     (uint8)
static void decode_rs422_test(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) { 
        w.key_str("warning", "rs422_test payload < 16 bytes"); 
        return; 
    }
    std::string arr = "[";
    for (int i = 0; i < 4; ++i) {
        const uint8_t* g = p + i * 4;
        JsonWriter grp;
        grp.key_uint("write_data",  g[0]);
        grp.key_uint("read_data",   g[1]);
        grp.key_uint("test_result", g[2]);
        if (i) arr += ',';
        arr += grp.str();
    }
    arr += "]";
    w.key_raw("rs422_groups", arr);
}

// 100/28 — FPGA Temperature Details (4 bytes).
// Per ICD Table 38: FPGA Temperature (float, degrees Celsius).
static void decode_fpga_temperature(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "fpga_temperature payload < 4 bytes"); return; }
    w.key_double("fpga_temp_c", static_cast<double>(load_f32le(p + 0)));
}

// 100/30 — CBIT Status (4 bytes).
// Per ICD Table 13: 4 status fields (uint8 each).
// 1 = PASS, 0 = FAIL.
static void decode_cbit_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "cbit_status payload < 4 bytes"); return; }
    w.key_uint("tuner_board_id_status", p[0]);
    w.key_uint("voltage_status",        p[1]);
    w.key_uint("temperature_status",    p[2]);
    w.key_uint("memory_status",         p[3]);
}

// =============================================================================
// Group 101 command decoders (ECM Client -> hardware).
// All Group 101 responses are 0-byte ACKs — no payload to decode.
// =============================================================================

// 101/25 — Set Threshold (8 bytes).
// Per ICD Table 39:
//   @0 ADC Channel Number (uint8)
//   @1 Reserved x3       (uint8)
//   @4 Threshold          (float, dBm)
static void decode_cmd_set_threshold(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "set_threshold cmd payload < 8 bytes"); return; }
    w.key_uint("adc_channel",    p[0]);
    w.key_double("threshold_dbm", static_cast<double>(load_f32le(p + 4)));
}

// 101/27 — Set Resolution (4 bytes).
// Per ICD Table 41: resolution enum 0–8 maps to kHz values.
//   @0 Channel No  (uint8)
//   @1 Resolution  (uint8) — enum index
//   @2 Reserved x2 (uint8)
static void decode_cmd_set_resolution(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "set_resolution cmd payload < 4 bytes"); return; }
    static const double RES_KHZ[] = {
        0.390625, 0.78125, 1.5625, 3.125, 6.25, 12.5, 25.0, 50.0, 100.0
    };
    uint8_t res_idx = p[1];
    w.key_uint("channel_no",       p[0]);
    w.key_uint("resolution_index", res_idx);
    if (res_idx < 9) {
        w.key_double("resolution_khz", RES_KHZ[res_idx]);
    } else {
        w.key_str("warning", "resolution index out of range 0-8");
    }
}

// 101/47 — Set Min/Max Pulse Range (16 bytes).
// Per ICD Table 43: all values in milliseconds (float).
//   @0  Max Pulse Length ms        (float)
//   @4  Min Pulse Length ms        (float)
//   @8  Burst Max Pulse Length ms  (float)
//   @12 Burst Min Pulse Length ms  (float)
static void decode_cmd_set_pulse_range(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) { 
        w.key_str("warning", "set_pulse_range cmd payload < 16 bytes"); 
        return; 
    }
    w.key_double("fh_max_pulse_ms",    static_cast<double>(load_f32le(p +  0)));
    w.key_double("fh_min_pulse_ms",    static_cast<double>(load_f32le(p +  4)));
    w.key_double("burst_max_pulse_ms", static_cast<double>(load_f32le(p +  8)));
    w.key_double("burst_min_pulse_ms", static_cast<double>(load_f32le(p + 12)));
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
// Per ICD Table 47:
//   @0  Start Frequency MHz         (uint16)
//   @2  Stop Frequency MHz          (uint16)
//   @4  RF Attenuation dB           (uint8, unsigned)
//   @5  IF Attenuation dB           (int8, signed)
//   @6  Detection Mode              (uint8) — 0=FH, 1=Fixed, 2=Burst, 3=Combined
//   @7  Zoom-Spectrum Report En/Dis (uint8)
//   @8  AGC Enable/Disable          (uint8)
//   @9  Reserved x3                 (uint8)
static void decode_cmd_configure_detection(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 12) { 
        w.key_str("warning", "configure_detection cmd payload < 12 bytes"); 
        return; 
    }
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

// 101/94 — Get Zoom Band FFT Data command (16 bytes).
// Per ICD Table 71: center freq (float MHz), BW index (int), noise leveling (int), threshold (float dBm).
// Zoom BW index → MHz/bins: 0=0.5/1280, 1=1/1280, 2=2/1280, 3=4/1463, 4=6/1536, 5=8/1576.
// Noise leveling: 0=Enable, 1=Disable.
static void decode_cmd_get_zoom_fft(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) { 
        w.key_str("warning", "get_zoom_fft cmd < 16 bytes"); 
        return; 
    }
    static const double BW_MHZ[]    = {0.5, 1.0, 2.0, 4.0, 6.0, 8.0};
    static const int    BIN_COUNT[] = {1280, 1280, 1280, 1463, 1536, 1576};
    int32_t bw_idx      = load_i32le(p + 4);
    int32_t noise_level = load_i32le(p + 8);
    w.key_double("center_freq_hz", static_cast<double>(load_f32le(p + 0)) * 1e6);
    w.key_int("bw_index",          bw_idx);
    if (bw_idx >= 0 && bw_idx < 6) {
        w.key_double("bw_mhz",          BW_MHZ[bw_idx]);
        w.key_uint("expected_bin_count", static_cast<unsigned>(BIN_COUNT[bw_idx]));
    }
    w.key_bool("noise_leveling_enabled", noise_level == 0);
    w.key_double("threshold_dbm",        static_cast<double>(load_f32le(p + 12)));
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

// 112/3 — TRSDU Receiver Line Status: 0-byte command.
// 112/5 — PA Receiver Line Status: 0-byte command.

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
    w.key_double("cal_value_db",  static_cast<double>(load_f32le(p + 12)));
}

// 6/9 — GO2Monitor Connection Establishment command (260 bytes). Per ICD Table 258.
// ip_address (char[128]) + port_number (char[128]) + channel (uint16) + reserved (uint16).
static void decode_cmd_go2mon_connect(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 260) { w.key_str("warning", "go2mon_connect cmd < 260 bytes"); return; }
    char ip[129] = {}, port[129] = {};
    std::memcpy(ip,   p +   0, 128);
    std::memcpy(port, p + 128, 128);
    w.key_str("ip_address",    ip);
    w.key_str("port_number",   port);
    w.key_uint("channel_number", load_u16le(p + 256));
}

// 6/13 — Start GO2Monitor Transmission command (144 bytes). Per ICD Table 260.
// channel (uint16) + bw (uint16) + reserved×2 (uint16) + center_freq_mhz (double, 8B) + date (char[128]).
static void decode_cmd_go2mon_start(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 144) { w.key_str("warning", "go2mon_start cmd < 144 bytes"); return; }
    w.key_uint("channel_number",      load_u16le(p + 0));
    w.key_uint("bandwidth_selection", load_u16le(p + 2));
    w.key_double("center_freq_hz",    load_f64le(p + 8) * 1e6);
    char date[129] = {};
    std::memcpy(date, p + 16, 128);
    w.key_str("date_str", date);
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
// S_HOPPER_DATA = 40 bytes: number(u32), minFreqMHz(f32), maxFreqMHz(f32),
//   pulseLenMs(f32), interHopMs(f32), detected(u32), TOA(4xu8), powerDbm(f32),
//   active(u16), reserved(u16), snr(f32).
// Frequencies stored in MHz by hardware; SI output is Hz (× 1e6).
// VU band: 30 MHz – 6000 MHz.
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

// 112/6 — PA Receiver Line Status response (4 bytes). Per ICD Table 197.
// pa_status (uint8) + 3 reserved.
// 0=No Fault, 1=Thermal Fault, 2=VSWR Fault, 3=BPM Fault, 4=Overdrive Fault.
static void decode_pa_receiver_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "pa_receiver_status payload < 4 bytes"); return; }
    static const char* STATUS_NAMES[] = {
        "no_fault", "thermal_fault", "vswr_fault", "bpm_fault", "overdrive_fault"
    };
    uint8_t status = p[0];
    w.key_uint("pa_status", status);
    w.key_str("pa_status_name", status < 5 ? STATUS_NAMES[status] : "no_fault");
}

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
extern "C" SDFC_EXPORT int extract_frame(const uint8_t* buf, int buf_len,
                                         uint8_t* out_frame, int* out_len) {
    return extract_frame_core(buf, buf_len, out_frame, out_len);
}

// =============================================================================
// ABI: parse_message
// =============================================================================
extern "C" SDFC_EXPORT const char* parse_message(const uint8_t* frame, int frame_len,
                                                 int frame_type) {
    FrameHeader hdr;
    if (!decode_header(frame, frame_len, frame_type, hdr)) return nullptr;
    if (hdr.total_len > frame_len) return nullptr;

    const uint8_t* payload = frame + hdr.payload_off;
    int plen = static_cast<int>(hdr.payload_size);
    if (hdr.payload_off + plen > frame_len) return nullptr;

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
                case 158: decode_cmd_terminate_fft(payload, plen, w);              decoded = true; break;
                default:  break;
            }
        } else if (hdr.group_id == 111) {
            switch (hdr.unit_id) {
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
                case 1: decode_cmd_asu_sdu_config(payload, plen, w); decoded = true; break;
                case 3: /* TRSDU Status — 0 bytes */                  decoded = true; break;
                case 5: /* PA Receiver Status — 0 bytes */            decoded = true; break;
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
                case 33: /* Close All Channels */      decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 3) {
            switch (hdr.unit_id) {
                case  1: /* Read Board Count */                              decoded = true; break;
                case 17: /* Read Channel Info */                             decoded = true; break;
                case 19: decode_cmd_mrx_write_channel(payload, plen, w);   decoded = true; break;
                case 21: /* MRX Channel Status */                            decoded = true; break;
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
                case 57: decode_cmd_mrx_ch_only(payload, plen, w);          decoded = true; break;
                case 65: decode_cmd_mrx_ch_bw(payload, plen, w);            decoded = true; break;
                case 67: decode_cmd_mrx_ch_only(payload, plen, w);          decoded = true; break;
                case 69: decode_cmd_mrx_ch_bw(payload, plen, w);            decoded = true; break;
                case 71: decode_cmd_mrx_ch_bw(payload, plen, w);            decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 5) {
            switch (hdr.unit_id) {
                case 1: decode_cmd_mrx_center_freq(payload, plen, w);  decoded = true; break;
                case 3: decode_cmd_mrx_attenuation(payload, plen, w);  decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 6) {
            switch (hdr.unit_id) {
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
                case 12: /* Set Fan Speed ACK — no payload */        decoded = true; break;
                case 14: decode_fan_speed_status(payload, plen, w); decoded = true; break;
                case 16: decode_ethernet_test(payload, plen, w);    decoded = true; break;
                case 18: decode_uart_test(payload, plen, w);        decoded = true; break;
                case 22: decode_fan_voltage(payload, plen, w);      decoded = true; break;
                case 24: decode_pps_test(payload, plen, w);         decoded = true; break;
                case 26: decode_rs422_test(payload, plen, w);       decoded = true; break;
                case 28: decode_fpga_temperature(payload, plen, w); decoded = true; break;
                case 30: decode_cbit_status(payload, plen, w);      decoded = true; break;
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
                case 159: /* Terminate FFT Thread ACK */               decoded = true; break;
                default:  break;
            }
        } else if (hdr.group_id == 111) {
            switch (hdr.unit_id) {
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
                case 2: decode_asu_sdu_response(payload, plen, w);   decoded = true; break;
                case 4: decode_trsdu_status(payload, plen, w);        decoded = true; break;
                case 6: decode_pa_receiver_status(payload, plen, w);  decoded = true; break;
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
                case 34: /* Close All Channels ACK */                    decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 3) {
            switch (hdr.unit_id) {
                case  2: decode_mrx_board_count(payload, plen, w);     decoded = true; break;
                case 18: decode_mrx_channel_status(payload, plen, w);  decoded = true; break;
                case 20: /* Write Channel ACK */                         decoded = true; break;
                case 22: decode_mrx_channel_status(payload, plen, w);  decoded = true; break;
                case 26: decode_mrx_cbit_status(payload, plen, w);     decoded = true; break;
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
                case 58: /* Stop Memory Scan ACK */                           decoded = true; break;
                case 66: /* Start IQ Optical ACK */                           decoded = true; break;
                case 68: /* Stop IQ Optical ACK */                            decoded = true; break;
                case 70: decode_mrx_optical_port_status(payload, plen, w);   decoded = true; break;
                case 72: decode_mrx_optical_ip(payload, plen, w);            decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 5) {
            switch (hdr.unit_id) {
                case 2: /* Set Center Freq ACK */    decoded = true; break;
                case 4: /* Attenuation Select ACK */ decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 6) {
            switch (hdr.unit_id) {
                case 10: /* GO2Mon Connect ACK */    decoded = true; break;
                case 12: /* GO2Mon Disconnect ACK */ decoded = true; break;
                case 14: /* GO2Mon Start ACK */      decoded = true; break;
                case 16: /* GO2Mon Stop ACK */       decoded = true; break;
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
                default: break;
            }
        } else if (hdr.group_id == 200 &&
                   (hdr.unit_id == 2 || hdr.unit_id == 4 ||
                    hdr.unit_id == 6 || hdr.unit_id == 8)) {
            decode_vu_jam_ack(hdr.unit_id, payload, plen, w); decoded = true;
        }
    }

    // Safe fallback: expose raw bytes so nothing is lost and the bridge never crashes.
    if (!decoded && plen > 0) {
        w.key_str("raw_hex", to_hex(payload, plen));
    }

    std::string json = w.str();
    char* result = static_cast<char*>(std::malloc(json.size() + 1));
    if (!result) return nullptr;
    std::memcpy(result, json.c_str(), json.size() + 1);
    return result;
}

// =============================================================================
// ABI: format_response
//   Encodes a JSON response object {group_id, unit_id, status [, payload_hex]}
//   into a binary DRS->SDFC response frame. Optional "payload_hex":"aabb.."
//   is decoded into the frame payload.
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

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

extern "C" SDFC_EXPORT int format_response(const char* json_response, uint8_t* out_frame) {
    if (!json_response || !out_frame) return -1;
    long long group = 0, unit = 0, status = 0;
    if (!json_find_int(json_response, "group_id", group)) return -1;
    if (!json_find_int(json_response, "unit_id",  unit))  return -1;
    if (!json_find_int(json_response, "status",   status)) return -1;

    uint8_t payload[MAX_PAYLOAD];
    int plen = 0;
    const char* ph = std::strstr(json_response, "\"payload_hex\"");
    if (ph) {
        const char* q = std::strchr(ph, ':');
        if (q) { q = std::strchr(q, '"'); }
        if (q) {
            ++q;
            while (*q && *q != '"') {
                int hi = hex_nibble(*q++);
                if (*q == '"' || !*q) break;
                int lo = hex_nibble(*q++);
                if (hi < 0 || lo < 0) return -1;
                if (plen >= MAX_PAYLOAD) return -1;
                payload[plen++] = static_cast<uint8_t>((hi << 4) | lo);
            }
        }
    }

    int total = RESP_OVERHEAD + plen;
    if (total > MAX_FRAME_BUFFER_BYTES) return -1;

    std::memcpy(out_frame, RESP_HEADER, MAGIC_LEN);
    store_i16le(out_frame + RESP_OFF_STATUS, static_cast<int16_t>(status));
    store_u32le(out_frame + RESP_OFF_SIZE,   static_cast<uint32_t>(plen));
    store_u16le(out_frame + RESP_OFF_GROUP,  static_cast<uint16_t>(group));
    store_u16le(out_frame + RESP_OFF_UNIT,   static_cast<uint16_t>(unit));
    if (plen > 0) std::memcpy(out_frame + RESP_OFF_PAYLOAD, payload, static_cast<size_t>(plen));
    std::memcpy(out_frame + RESP_OFF_PAYLOAD + plen, RESP_FOOTER, MAGIC_LEN);
    return total;
}

// =============================================================================
// ABI: free_result
// =============================================================================
extern "C" SDFC_EXPORT void free_result(const char* result) {
    std::free(const_cast<char*>(result));
}
