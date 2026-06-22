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
//   Group 101 (SJC detection):
//     cmds 25,27,37,39,43,47,55,69,83,85,87,94,140,158,160,162,164,174,176,178,182,184,186,200,202,204,210
//     rsps 40,44,70,84,88,95,205
//   Group 109 (Date/Time): 109/11 cmd, 109/12 rsp
//   Group 111 (Signal/Scan):
//     cmds 3(16B),5,9,13,15,17,19,25; rsps 4(12B),6,10,14,16(40B/entry),18,20,26,211
//   Group 112 (Fast Scan/Simulation): cmds 1,5,13,37; rsps 2,6,14,38
//   Group 200 (HF ECM jamming): cmds 1,3,5,7,9,11,13,17,19,21; rsps 2,4,6,8,10,12,14,15,18,20,22
//   Group 106 rsp 54 (Start Immediate Jam ACK)
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

static constexpr const char* HW_NAME = "dp_ecm_hf";

// =============================================================================
// Group 100 response decoders — HF-specific layouts
// =============================================================================

// 100/2 — Get System Version Details (20 bytes).
// HF layout (differs from VU): 3 float version words (no BSP version) +
//   processor_id + 1 RF tuner ID + fpga_type_id + reserved.
// @0  SJC FW Version   (float)  @4  Driver Version (float)
// @8  FPGA Version     (float)  @12 Processor ID   (uint16)
// @14 RF Tuner ID      (uint16) @16 FPGA Type ID   (uint16)
// @18 Reserved         (uint16)
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
// HF SRx checksum is 1024 bytes (VU SRx is 1000 bytes). Emitted as hex.
static void decode_srx_checksum(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 1024) { w.key_str("warning", "srx_checksum payload < 1024 bytes"); return; }
    w.key_str("sjc_fw_checksum_hex", to_hex(p, 1024));
}

// 100/6 — PBIT Status (88 bytes).
// HF layout: extended hardware test points vs VU's 24-byte simple layout.
// Bytes 0-23: core status fields (same semantic positions as VU).
// Bytes 24-63: HF-specific hardware tests (exciter, PA, DDS, RF filters, synthesiser).
// Bytes 64-87: reserved / future use — emitted as raw_hex.
static void decode_pbit_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 88) { w.key_str("warning", "pbit_status payload < 88 bytes"); return; }
    // Core status (bytes 0-23, shared with VU)
    w.key_uint("combined_drx_status",           p[ 0]);
    w.key_uint("combined_temperature_status",   p[ 1]);
    w.key_uint("combined_voltage_status",       p[ 2]);
    w.key_uint("combined_exciter_status",       p[ 3]);
    w.key_uint("fpga_scratch_pad_test",         p[ 4]);
    w.key_uint("fpga_board_id",                 p[ 5]);
    w.key_uint("fpga_init_test",                p[ 6]);
    w.key_uint("fpga_temperature_status",       p[ 7]);
    w.key_uint("rf_psu_temp_sensor",            p[ 8]);
    w.key_uint("rf_psu_5v_monitor_status",      p[ 9]);
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
    w.key_uint("exciter_pll_status",            p[21]);
    w.key_uint("pa_status",                     p[22]);
    // p[23] reserved
    // HF-specific hardware tests (bytes 24-63)
    w.key_uint("dds_status",                    p[24]);
    w.key_uint("hf_input_filter_status",        p[25]);
    w.key_uint("hf_output_filter_status",       p[26]);
    w.key_uint("synthesiser_status",            p[27]);
    w.key_uint("adc1_status",                   p[28]);
    w.key_uint("adc2_status",                   p[29]);
    w.key_uint("dac_status",                    p[30]);
    w.key_uint("serial_comm_status",            p[31]);
    w.key_uint("pa_driver_status",              p[32]);
    w.key_uint("pa_output_filter_status",       p[33]);
    w.key_uint("exciter_output_status",         p[34]);
    w.key_uint("exciter_driver_status",         p[35]);
    w.key_uint("hf_band_filter_status",         p[36]);
    w.key_uint("lo_pll_lock_status",            p[37]);
    w.key_uint("bite_signal_status",            p[38]);
    w.key_uint("bite_pll_lock_status",          p[39]);
    // p[40-63] additional test points
    w.key_str("extended_test_results_hex", to_hex(p + 40, 24));
    // p[64-87] reserved
    w.key_str("reserved_hex", to_hex(p + 64, 24));
}

// 100/8 — IBIT Status (68 bytes).
// HF layout: extended vs VU's 8 bytes.
// Bytes 0-7: core IBIT fields (same semantic as VU).
// Bytes 8-63: HF-specific in-service BIT tests.
// Bytes 64-67: reserved.
static void decode_ibit_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 68) { w.key_str("warning", "ibit_status payload < 68 bytes"); return; }
    w.key_uint("combined_drx_status",   p[ 0]);
    w.key_uint("storage_health_status", p[ 1]);
    w.key_uint("drx_pll_health_test",   p[ 2]);
    w.key_uint("rf_tuner_health_test",  p[ 3]);
    w.key_uint("drx_adc_health_test",   p[ 4]);
    w.key_uint("msata_rw_test",         p[ 5]);
    w.key_uint("storage_avail_check",   p[ 6]);
    // p[7] reserved
    // HF-specific IBIT tests (bytes 8-63)
    w.key_uint("exciter_self_test",     p[ 8]);
    w.key_uint("pa_self_test",          p[ 9]);
    w.key_uint("dds_self_test",         p[10]);
    w.key_uint("lo_self_test",          p[11]);
    w.key_uint("adc1_self_test",        p[12]);
    w.key_uint("adc2_self_test",        p[13]);
    w.key_uint("dac_self_test",         p[14]);
    w.key_uint("synthesiser_self_test", p[15]);
    // p[16-63] additional IBIT results
    w.key_str("extended_ibit_hex", to_hex(p + 16, 48));
    // p[64-67] reserved
}

// 100/10 — Temperature Status (36 bytes, 9 floats).
// HF layout: 9 temperature sensors vs VU's 6.
// All values in degrees Celsius.
static void decode_temperature(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 36) { w.key_str("warning", "temperature payload < 36 bytes"); return; }
    w.key_double("internal_temp_c",    static_cast<double>(load_f32le(p +  0)));
    w.key_double("external_temp_c",    static_cast<double>(load_f32le(p +  4)));
    w.key_double("cpu_temp_c",         static_cast<double>(load_f32le(p +  8)));
    w.key_double("fpga_temp_c",        static_cast<double>(load_f32le(p + 12)));
    w.key_double("psu_temp_c",         static_cast<double>(load_f32le(p + 16)));
    w.key_double("rf_psu_temp_c",      static_cast<double>(load_f32le(p + 20)));
    w.key_double("fan_temp_c",         static_cast<double>(load_f32le(p + 24)));
    w.key_double("pa_temp_c",          static_cast<double>(load_f32le(p + 28)));
    w.key_double("exciter_temp_c",     static_cast<double>(load_f32le(p + 32)));
}

// 100/14 — Fan Speed Status (4 bytes). Same layout as VU.
static void decode_fan_speed_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "fan_speed_status payload < 4 bytes"); return; }
    w.key_uint("fan_speed_rpm", load_u32le(p + 0));
}

// 100/18 — UART Test Status (4 bytes). Same layout as VU.
static void decode_uart_test(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "uart_test payload < 4 bytes"); return; }
    w.key_uint("expected_data", p[0]);
    w.key_uint("observed_data", p[1]);
    w.key_uint("result",        p[2]);
}

// 100/26 — CBIT Status (8 bytes).
// HF layout at unit 26 (VU is at unit 30, 4 bytes).
// 8 uint8 status fields covering HF hardware subsystems.
static void decode_cbit_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "cbit_status payload < 8 bytes"); return; }
    w.key_uint("combined_rx_status",  p[0]);
    w.key_uint("combined_tx_status",  p[1]);
    w.key_uint("voltage_status",      p[2]);
    w.key_uint("temperature_status",  p[3]);
    w.key_uint("memory_status",       p[4]);
    w.key_uint("exciter_status",      p[5]);
    w.key_uint("pa_status",           p[6]);
    // p[7] reserved
}

// =============================================================================
// Group 100 command decoders
// =============================================================================

// 100/17 — UART Port Selection command (4 bytes).
// @0 Port Selection (uint8) + 3 reserved.
static void decode_cmd_uart_port_select(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "uart_port_select cmd < 4 bytes"); return; }
    w.key_uint("port_selection", p[0]);
}

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
    w.key_int("bw_index",          bw_idx);
    if (bw_idx >= 0 && bw_idx < 6) {
        w.key_double("bw_mhz",          BW_MHZ[bw_idx]);
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

// 101/44 — Wideband FFT Data response (variable format).
// HF layout per ICD Table 40:
//   StartFreqMHz(u32) @0, StopFreqMHz(u32) @4, StepKHz(u32) @8,
//   PointCount(u32) @12, PowerDbm[PointCount](f32) @16
// Output frequencies in Hz (SI); power in dBm.
static void decode_wideband_fft(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) { w.key_str("warning", "wideband_fft payload < 16 bytes"); return; }
    uint32_t start_mhz = load_u32le(p + 0);
    uint32_t stop_mhz  = load_u32le(p + 4);
    uint32_t step_khz  = load_u32le(p + 8);
    uint32_t count     = load_u32le(p + 12);
    w.key_double("start_freq_hz", static_cast<double>(start_mhz) * 1e6);
    w.key_double("stop_freq_hz",  static_cast<double>(stop_mhz)  * 1e6);
    w.key_double("step_hz",       static_cast<double>(step_khz)  * 1e3);
    w.key_uint("point_count", count);

    const int expected = 16 + static_cast<int>(count) * 4;
    if (n < expected) {
        w.key_str("warning", "wideband_fft payload truncated");
        count = static_cast<uint32_t>((n - 16) / 4);
    }
    const uint32_t EMIT_MAX = 2048;
    uint32_t emit = (count < EMIT_MAX) ? count : EMIT_MAX;
    std::string arr = "[";
    for (uint32_t i = 0; i < emit; ++i) {
        if (i) arr += ',';
        char tmp[32];
        std::snprintf(tmp, sizeof(tmp), "%.2f",
            static_cast<double>(load_f32le(p + 16 + i * 4)));
        arr += tmp;
    }
    arr += "]";
    w.key_raw("power_dbm", arr);
    if (emit < count) w.key_uint("points_omitted", count - emit);
}

// Shared helper: decode one HF S_DETECTED_FIXED_FREQUENCY entry (60 bytes).
// VU uses 36B; HF extends with ms-resolution timestamps, signal BW, and spare fields.
// Layout (60B):
//   @0  FreqMHz (float→Hz)  @4 CurrentPowerdBm (float)  @8 ActiveCount (uint32)
//   @12 MinPowerdBm (float) @16 MaxPowerdBm (float)
//   @20 TOA H:M:S:rsv (4B) @24 Duration H:M:S:rsv (4B)
//   @28 FreqActive (uint16) @30 Reserved (uint16)  @32 SNR (float)
//   @36 ToaMs (uint16)      @38 DurationMs (uint16) @40 SignalBwKhz (float)
//   @44 Reserved_a (uint32) @48 Reserved_b (uint32) @52 Reserved_c (uint32) @56 Reserved_d (uint32)
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
    // HF extension
    f.key_uint("toa_ms",              load_u16le(e + 36));
    f.key_uint("duration_ms",         load_u16le(e + 38));
    f.key_double("signal_bw_khz",     static_cast<double>(load_f32le(e + 40)));
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

    const int FF_ELEM = 60;
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

// 101/84 — Burst Detection response.
// HF S_DETECTED_BURST_FREQUENCY = 52 bytes (VU = 24 bytes).
// Layout (52B):
//   @0  FreqMHz (float→Hz)  @4 CurrentPowerdBm (float)  @8 PulseLengthMs (float)
//   @12 ActiveCount (uint32) @16 TOA H:M:S:rsv (4B) @20 SNR (float)
//   @24 MinFreqHz (double,8B) @32 MaxFreqHz (double,8B)
//   @40 SignalBwKhz (float)  @44 Reserved (uint32) @48 Confidence (float)
static void decode_burst_detection(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "burst_detection payload < 4 bytes"); return; }
    uint32_t burst_count = load_u32le(p + 0);
    w.key_uint("burst_count", burst_count);

    const int ELEM = 52;
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
        b.key_double("snr_db",           static_cast<double>(load_f32le(e + 20)));
        // HF extension
        b.key_double("min_freq_hz",      load_f64le(e + 24) * 1e6);
        b.key_double("max_freq_hz",      load_f64le(e + 32) * 1e6);
        b.key_double("signal_bw_khz",    static_cast<double>(load_f32le(e + 40)));
        b.key_double("confidence",       static_cast<double>(load_f32le(e + 48)));
        if (emitted++) arr += ',';
        arr += b.str();
        off += ELEM;
    }
    arr += "]";
    w.key_raw("bursts", arr);
}

// 101/88 — Stop Scan Speed response.
// Same S_FIXED_FREQUENCIES structure as FF detection; 60B/entry for HF.
static void decode_stop_scan_speed(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "stop_scan_speed payload < 4 bytes"); return; }
    uint32_t ff_count = load_u32le(p + 0);
    w.key_uint("ff_count", ff_count);

    const int FF_ELEM = 60;
    std::string arr = "[";
    int off = 4;
    uint32_t emitted = 0;
    for (uint32_t i = 0; i < ff_count; ++i) {
        if (off + FF_ELEM > n) break;
        JsonWriter f;
        decode_ff_entry_hf(p + off, f);
        if (emitted++) arr += ',';
        arr += f.str();
        off += FF_ELEM;
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

// 111/4 — Signal BITE Test response (12 bytes). HF-specific (VU = 111/22, 16 bytes).
// @0 BiteFreqHz (float, 4B, MHz stored) @4 BitePowerdBm (float) @8 BiteResult (uint16) @10 Reserved (uint16)
static void decode_signal_bite_resp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 12) { w.key_str("warning", "signal_bite_resp payload < 12 bytes"); return; }
    w.key_double("bite_freq_hz",   static_cast<double>(load_f32le(p + 0)) * 1e6);
    w.key_double("bite_power_dbm", static_cast<double>(load_f32le(p + 4)));
    w.key_uint("bite_result",      load_u16le(p + 8));
}

// 111/16 — PDW Channelization Data response (variable).
// HF S_FH_CHANNELIZATION = 40 bytes (VU = 28 bytes).
// Header: count (uint32) + S_FH_CHANNELIZATION_TOA (4B: H,M,S,rsv)
// Per-entry layout (40B):
//   @0  TOI (double,8B)       @8  FreqIndexMHz (float→Hz)  @12 PulseLengthMs (float)
//   @16 PowerLeveldBm (float) @20 Bandwidth (uint32)        @24 FreqBand (uint32)
//   @28 SignalType (uint16)   @30 HopperStartMag (uint16)   @32 SignalBwKhz (float)
//   @36 NbWbBand (uint32)
static void decode_pdw_channelization(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "pdw_channelization payload < 8 bytes"); return; }
    uint32_t count = load_u32le(p + 0);
    w.key_uint("channelization_count", count);
    char toa[16];
    std::snprintf(toa, sizeof(toa), "%02u:%02u:%02u", p[4], p[5], p[6]);
    w.key_str("toa", toa);

    const int ELEM = 40;
    int off = 8;
    std::string arr = "[";
    uint32_t emitted = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (off + ELEM > n) break;
        const uint8_t* e = p + off;
        JsonWriter c;
        c.key_double("toi",              load_f64le(e +  0));
        c.key_double("freq_index_hz",    static_cast<double>(load_f32le(e +  8)) * 1e6);
        c.key_double("pulse_length_ms",  static_cast<double>(load_f32le(e + 12)));
        c.key_double("power_level_dbm",  static_cast<double>(load_f32le(e + 16)));
        c.key_uint("bandwidth",           load_u32le(e + 20));
        c.key_uint("freq_band",           load_u32le(e + 24));
        // HF extension
        c.key_uint("signal_type",         load_u16le(e + 28));
        c.key_uint("hopper_start_mag",    load_u16le(e + 30));
        c.key_double("signal_bw_khz",     static_cast<double>(load_f32le(e + 32)));
        c.key_uint("nb_wb_band",          load_u32le(e + 36));
        if (emitted++) arr += ',';
        arr += c.str();
        off += ELEM;
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
// Group 112 command/response decoders — HF Fast Scan / Simulation Mode
// (VU Group 112 = ASU/SDU control — completely different from HF.)
// =============================================================================

// 112/1 — Start-Stop Fast Scan command.
// @0 FastScanCommand (uint8): 0=Stop, 1=Start  @1-3 Reserved
// @4 StartFreqMHz (float)  @8 StopFreqMHz (float)  @12 StepKHz (float)
static void decode_cmd_fast_scan_control(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "fast_scan_control cmd < 4 bytes"); return; }
    w.key_str("scan_command", p[0] == 0 ? "stop" : "start");
    if (n >= 16) {
        w.key_double("start_freq_hz", static_cast<double>(load_f32le(p +  4)) * 1e6);
        w.key_double("stop_freq_hz",  static_cast<double>(load_f32le(p +  8)) * 1e6);
        w.key_double("step_hz",       static_cast<double>(load_f32le(p + 12)) * 1e3);
    }
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

// 112/2 — Fast Scan Start/Stop response (4 bytes).
// @0 ScanStatus (uint8): 0=Stopped, 1=Running + 3 reserved.
static void decode_fast_scan_response(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "fast_scan_response < 4 bytes"); return; }
    w.key_str("scan_status", p[0] == 1 ? "running" : "stopped");
}

// =============================================================================
// Group 200 HF jamming command/response decoders
// (VU jamming lives on Groups 101/106/108; HF consolidates all jamming on Group 200.)
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

// 200/1 — Start Follow-on Jam command (44 bytes).
// S_TRACKING_WINDOW (16B) + S_TRACKING_INFO (28B). Same structure as VU 101/31.
static void decode_cmd_start_follow_on_jam(const uint8_t* p, int n, JsonWriter& w) {
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

// 200/5 — Start List Jam command (1228 bytes).
// S_LIST_JAMMING_INFO (24B) + S_LIST_JAMMING_FREQUENCIES (1204B). Same as VU 101/73.
static void decode_cmd_start_list_jam(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 28) { w.key_str("warning", "start_list_jam cmd < 28 bytes"); return; }
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

// 200/9 — Start Responsive Sweep Jam command (208 bytes). Same layout as VU 101/92.
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
static void decode_cmd_send_ecm_reports(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "send_ecm_reports cmd < 4 bytes"); return; }
    w.key_bool("ecm_reports_enabled", p[0] == 1);
}

// 200/17 — Start Immediate Jam command (variable).
// Uses S_JAM_CONFIGURATION (20B) to select jam mode, then mode-specific data follows.
// frequency_mode in S_JAM_CONFIGURATION selects: 0=single, 1=TDM, 2=FDM, 3=sweep, 4=comb_noise.
static void decode_cmd_start_immediate_jam(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 20) { w.key_str("warning", "start_immediate_jam cmd < 20 bytes"); return; }
    decode_jam_config_block(p, w);
    // Mode-specific data follows S_JAM_CONFIGURATION (20B)
    uint8_t mode = p[0];
    if (n > 20) {
        const uint8_t* q = p + 20;
        int rem = n - 20;
        if (mode == 0 && rem >= 8) {
            // Single frequency: jam_freq (double, MHz→Hz)
            w.key_double("jam_freq_hz", load_f64le(q) * 1e6);
        } else if (mode == 1 && rem >= 60) {
            // TDM: 6 frequencies (double×6) + modulating_signal + on_time + off_time
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
        } else if (mode == 2 && rem >= 48) {
            // FDM: 6 frequencies (double×6)
            std::string freqs = "[";
            for (int i = 0; i < 6; ++i) {
                if (i) freqs += ',';
                char tmp[32];
                std::snprintf(tmp, sizeof(tmp), "%.6g", load_f64le(q + i * 8) * 1e6);
                freqs += tmp;
            }
            freqs += "]";
            w.key_raw("fdm_frequencies_hz", freqs);
        } else if (mode == 4 && rem >= 12) {
            // Comb noise: start_freq (double) + stop_freq... minimal decode
            w.key_double("start_freq_hz", load_f64le(q) * 1e6);
        }
        // mode 3 (sweep) and others: emit raw
        else if (rem > 0) {
            w.key_str("jam_data_hex", to_hex(q, rem > 64 ? 64 : rem));
        }
    }
}

// 200/19 — Configure External Modulation Data command (8196 bytes). Same as VU 106/39.
static void decode_cmd_configure_ext_modulation(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "configure_ext_modulation cmd < 4 bytes"); return; }
    uint32_t audio_size = load_u32le(p + 0);
    w.key_uint("audio_data_size",     audio_size);
    w.key_bool("audio_buffer_present", n >= 4 + static_cast<int>(audio_size) * 2);
}

// 200/21 — Configure Programmable Exciter command (2052 bytes). Same as VU 106/49.
static void decode_cmd_configure_prog_exciter(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "configure_prog_exciter cmd < 4 bytes"); return; }
    w.key_uint("channel_no", load_u16le(p + 0));
    w.key_uint("data_size",  load_u16le(p + 2));
}

// =============================================================================
// Group 200 response decoders
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

// 200/20 — Configure External Modulation response (4 bytes). Same as VU 106/40.
static void decode_ext_modulation_response(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "ext_modulation_response < 4 bytes"); return; }
    w.key_uint("software_buffer_size", load_u32le(p + 0));
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

// 1/4 — Get Checksum Details response (1024 bytes). Same pattern as ECM.
static void decode_mrx_checksum(const uint8_t* p, int n, JsonWriter& w) {
    w.key_str("fw_checksum_hex", to_hex(p, n > 1024 ? 1024 : n));
}

// 1/6 — PBIT Status response (124 bytes).
static void decode_mrx_pbit_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 124) { w.key_str("warning", "mrx_pbit_status < 124 bytes"); return; }
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

// 1/10 — Temperature Status response (40 bytes, 10 floats). MRx-specific sensors.
static void decode_mrx_temperature(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 40) { w.key_str("warning", "mrx_temperature < 40 bytes"); return; }
    static const char* NAMES[] = {
        "bpt_temp_c", "psu_8156_temp_c", "tuner_temp_c", "psu_7255_temp_c",
        "processor_temp_c", "power_supply_temp_c", "fan_ctrl_board_temp_c",
        "rfpsu_temp_c", "digital_temp_c", "fpga_temp_c"
    };
    for (int i = 0; i < 10; ++i)
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
    w.key_uint("board_count", load_u16le(p + 0));
    w.key_str("note", load_u16le(p + 0) == 1 ? "mrx_channels_accessible" : "mrx_channels_not_accessible");
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

// 5/3 — Attenuation Selections command (16 bytes).
// ICD lists message size as 10; actual parameters total 16 (2+2+4+4+4). Use 16.
static void decode_mrx_attenuation_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 12) { w.key_str("warning", "mrx_attenuation < 12 bytes"); return; }
    w.key_uint("mrx_channel",          load_u16le(p + 0));
    w.key_double("rf_attenuation_db",  static_cast<double>(load_f32le(p + 4)));
    w.key_double("if_attenuation_db",  static_cast<double>(load_f32le(p + 8)));
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

// 4/8 — Audio Data Acquisition response (variable).
// Header: audio_data_size (uint32). Raw audio buffer follows (16-bit samples, 8kHz).
// Buffer is not emitted in JSON — just log size.
static void decode_mrx_audio_data_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) return;
    uint32_t audio_size = load_u32le(p + 0);
    w.key_uint("audio_data_size",    audio_size);
    w.key_uint("audio_bytes",        audio_size * 2);
    w.key_bool("buffer_present",     n >= 4 + static_cast<int>(audio_size) * 2);
    w.key_str("sample_rate_hz",      "8000");
    w.key_str("sample_bits",         "16");
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
    const uint32_t CAP = 100;
    int off = 8;
    uint32_t emit = 0;
    std::string arr = "[";
    for (uint32_t i = 0; i < count && i < CAP; ++i) {
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
    if (count > CAP) {
        char msg[64];
        std::snprintf(msg, sizeof(msg), "truncated at %u of %u entries", CAP, count);
        w.key_str("truncated", msg);
    }
}

// 4/42 — Read Memory Scan Data response (variable).
// Header: freq_count(4) + scan_speed(2) + reserved(2).
// Per entry (20B): power(f32,4) + freq(double,8) + H:M:S:rsv(4B) + ms(u16,2) + bw(u16,2).
static void decode_mrx_memory_scan_data_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) return;
    uint32_t count      = load_u32le(p + 0);
    uint16_t scan_speed = load_u16le(p + 4);
    w.key_uint("freq_count",    count);
    w.key_uint("scan_speed_channels_per_sec", scan_speed);
    const int ELEM  = 20;
    const uint32_t CAP = 100;
    int off = 8;
    uint32_t emit = 0;
    std::string arr = "[";
    for (uint32_t i = 0; i < count && i < CAP; ++i) {
        if (off + ELEM > n) break;
        const uint8_t* e = p + off;
        JsonWriter f;
        f.key_double("power_dbm",  static_cast<double>(load_f32le(e +  0)));
        f.key_double("freq_hz",    load_f64le(e +  4) * 1e6);
        char toa[24];
        std::snprintf(toa, sizeof(toa), "%02u:%02u:%02u.%03u",
                      e[12], e[13], e[14], load_u16le(e + 16));
        f.key_str("toa", toa);
        f.key_uint("bw_list", load_u16le(e + 18));
        if (emit++) arr += ',';
        arr += f.str();
        off += ELEM;
    }
    arr += "]";
    w.key_raw("scan_data", arr);
    if (count > CAP) {
        char msg[64];
        std::snprintf(msg, sizeof(msg), "truncated at %u of %u entries", CAP, count);
        w.key_str("truncated", msg);
    }
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
static void decode_mrx_go2monitor_connect_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 260) return;
    char ip[129], port[129];
    std::memcpy(ip,   p +   0, 128); ip[128]   = '\0';
    std::memcpy(port, p + 128, 128); port[128] = '\0';
    w.key_str("ip_address",    ip);
    w.key_str("port_number",   port);
    w.key_uint("mrx_channel",  load_u16le(p + 256));
}

// 6/13 — Start GO2Monitor Transmission command (144 bytes).
// @0 mrx_channel(u16) @2 bw(u16) @4 reserved(u16) @6 reserved(u16)
// @8 center_freq(double,8) @16 date(char[128])
static void decode_mrx_start_go2monitor_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 144) return;
    static const char* BW[] = {"1MHz","240kHz","120kHz","60kHz","30kHz"};
    uint16_t bw = load_u16le(p + 2);
    w.key_uint("mrx_channel",       load_u16le(p +  0));
    w.key_uint("bw_selection",      bw);
    w.key_str("bandwidth_name",     bw < 5 ? BW[bw] : "unknown");
    w.key_double("center_freq_hz",  load_f64le(p + 8) * 1e6);
    char date[129];
    std::memcpy(date, p + 16, 128);
    date[128] = '\0';
    w.key_str("date", date);
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

// Shared helper: decode 4-byte "mrx_channel + reserved" command.
static void decode_mrx_channel_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n >= 2) w.key_uint("mrx_channel", load_u16le(p));
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

    // ------------------------------------------------------------------
    // FRAME_COMMAND dispatch
    // ------------------------------------------------------------------
    if (frame_type == FRAME_COMMAND) {
        if (hdr.group_id == 100) {
            switch (hdr.unit_id) {
                case 17: decode_cmd_uart_port_select(payload, plen, w); decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 101) {
            switch (hdr.unit_id) {
                case  25: decode_cmd_set_threshold(payload, plen, w);           decoded = true; break;
                case  27: decode_cmd_set_resolution(payload, plen, w);          decoded = true; break;
                case  37: decode_cmd_configure_detection(payload, plen, w);     decoded = true; break;
                case  39: decode_cmd_start_fh_detection(payload, plen, w);      decoded = true; break;
                case  43: decode_cmd_get_wideband_fft(payload, plen, w);        decoded = true; break;
                case  47: decode_cmd_set_pulse_range(payload, plen, w);         decoded = true; break;
                case  55: decode_cmd_set_min_hops(payload, plen, w);            decoded = true; break;
                case  69: decode_cmd_start_ff_detection(payload, plen, w);      decoded = true; break;
                case  83: decode_cmd_start_burst_detection(payload, plen, w);   decoded = true; break;
                case  85: decode_cmd_start_scan_speed(payload, plen, w);        decoded = true; break;
                case  87: /* Stop Scan Speed — 0 bytes */                        decoded = true; break;
                case  94: decode_cmd_get_zoom_fft(payload, plen, w);            decoded = true; break;
                case 140: /* HF-specific — 0 bytes */                            decoded = true; break;
                case 158: decode_cmd_terminate_fft(payload, plen, w);           decoded = true; break;
                case 160: /* HF slow scan sequence cmd — 0 bytes */              decoded = true; break;
                case 162: /* HF slow scan sequence cmd — 0 bytes */              decoded = true; break;
                case 164: /* HF slow scan sequence cmd — 0 bytes */              decoded = true; break;
                case 174: /* HF slow scan sequence cmd — 0 bytes */              decoded = true; break;
                case 176: /* HF slow scan sequence cmd — 0 bytes */              decoded = true; break;
                case 178: /* HF slow scan sequence cmd — 0 bytes */              decoded = true; break;
                case 182: /* HF fast scan sequence cmd — 0 bytes */              decoded = true; break;
                case 184: /* HF fast scan sequence cmd — 0 bytes */              decoded = true; break;
                case 186: /* HF fast scan sequence cmd — 0 bytes */              decoded = true; break;
                case 200: /* HF wideband cmd — 0 bytes */                        decoded = true; break;
                case 202: /* HF wideband cmd — 0 bytes */                        decoded = true; break;
                case 204: /* HF data query cmd — 0 bytes; rsp on 101/205 */      decoded = true; break;
                case 210: /* HF cmd; rsp on Group 111/211 (cross-group) */       decoded = true; break;
                default:  break;
            }
        } else if (hdr.group_id == 109) {
            switch (hdr.unit_id) {
                case 11: decode_cmd_set_date_time(payload, plen, w); decoded = true; break;
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
                case 25: /* Get Storage Details — 0 bytes */                     decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 112) {
            switch (hdr.unit_id) {
                case  1: decode_cmd_fast_scan_control(payload, plen, w);         decoded = true; break;
                case  5: decode_cmd_auto_scan_band_config(payload, plen, w);     decoded = true; break;
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
                case 17: decode_mrx_sel_channel_cmd("avg_count_sel",nullptr,nullptr,payload,plen,w); decoded=true; break;
                case 19: decode_mrx_sel_channel_cmd("rf_agc_sel","enable","disable",payload,plen,w); decoded=true; break;
                case 21: decode_mrx_audio_squelch_cmd(payload, plen, w);            decoded = true; break;
                case 23: decode_mrx_date_time_cmd(payload, plen, w);                decoded = true; break;
                default: break;
            }
        }
    // ------------------------------------------------------------------
    // FRAME_RESPONSE dispatch
    // ------------------------------------------------------------------
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
                case  26: /* Set Threshold ACK */                    decoded = true; break;
                case  28: /* Set Resolution ACK */                   decoded = true; break;
                case  38: /* Configure Detection ACK */              decoded = true; break;
                case  40: decode_fh_detection(payload, plen, w);    decoded = true; break;
                case  44: decode_wideband_fft(payload, plen, w);    decoded = true; break;
                case  48: /* Set Pulse Range ACK */                  decoded = true; break;
                case  56: /* Set Min Hops ACK */                     decoded = true; break;
                case  70: decode_ff_detection(payload, plen, w);    decoded = true; break;
                case  84: decode_burst_detection(payload, plen, w); decoded = true; break;
                case  86: /* Start Scan Speed ACK */                 decoded = true; break;
                case  88: decode_stop_scan_speed(payload, plen, w); decoded = true; break;
                case  95: decode_zoom_fft(payload, plen, w);        decoded = true; break;
                case 141: /* HF-specific ACK */                      decoded = true; break;
                case 159: /* Terminate FFT ACK */                    decoded = true; break;
                case 161: /* HF slow scan ACK */                     decoded = true; break;
                case 163: /* HF slow scan ACK */                     decoded = true; break;
                case 165: /* HF slow scan ACK */                     decoded = true; break;
                case 175: /* HF slow scan ACK */                     decoded = true; break;
                case 177: /* HF slow scan ACK */                     decoded = true; break;
                case 179: /* HF slow scan ACK */                     decoded = true; break;
                case 183: /* HF fast scan ACK */                     decoded = true; break;
                case 185: /* HF fast scan ACK */                     decoded = true; break;
                case 187: /* HF fast scan ACK */                     decoded = true; break;
                case 201: /* HF wideband ACK */                      decoded = true; break;
                case 203: /* HF wideband ACK */                      decoded = true; break;
                case 205: /* HF data response — no struct decoder, falls through to raw_hex */ break;
                default:  break;
            }
        } else if (hdr.group_id == 109) {
            switch (hdr.unit_id) {
                case 12: /* Set Date Time ACK */ decoded = true; break;
                default: break;
            }
        } else if (hdr.group_id == 111) {
            switch (hdr.unit_id) {
                case   4: decode_signal_bite_resp(payload, plen, w);  decoded = true; break;
                case   6: /* Reference Input ACK */                    decoded = true; break;
                case  10: /* Send Protected Scan List ACK */           decoded = true; break;
                case  14: /* Protected Scan Enable ACK */              decoded = true; break;
                case  16: decode_pdw_channelization(payload, plen, w);decoded = true; break;
                case  18: /* FH Splitband Enable ACK */                decoded = true; break;
                case  20: /* FH Splitband Freq ACK */                  decoded = true; break;
                case  26: decode_storage_details(payload, plen, w);   decoded = true; break;
                case 211: /* cross-group rsp to 101/210 — no struct decoder, falls through to raw_hex */ break;
                default:  break;
            }
        } else if (hdr.group_id == 112) {
            switch (hdr.unit_id) {
                case  2: decode_fast_scan_response(payload, plen, w); decoded = true; break;
                case  6: /* Auto Scan Band Config ACK */               decoded = true; break;
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
        } else if (hdr.group_id == 106 && hdr.unit_id == 54) {
            decode_immediate_jam_ack(payload, plen, w); decoded = true;
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
                case 18: decode_mrx_channel_16b_rsp(payload, plen, w);      decoded = true; break;
                case 20: /* Write Channel ACK */                             decoded = true; break;
                case 22: decode_mrx_channel_16b_rsp(payload, plen, w);      decoded = true; break;
                case 24: decode_mrx_tuning_details_rsp(payload, plen, w);   decoded = true; break;
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
                case 14: decode_mrx_iq_start_rsp(payload, plen, w);         decoded = true; break;
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

extern "C" SDFC_EXPORT int format_response(const char* kind, const char* kwargs_json,
                                           uint8_t** out_buf, size_t* out_len) {
    if (!kwargs_json || !out_buf || !out_len) return -1;
    long long group = 0, unit = 0, status = 0;
    if (!json_find_int(kwargs_json, "group_id", group)) return -1;
    if (!json_find_int(kwargs_json, "unit_id",  unit))  return -1;
    if (!json_find_int(kwargs_json, "status",   status)) return -1;

    uint8_t payload[MAX_PAYLOAD];
    int plen = 0;
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
                if (hi < 0 || lo < 0) return -1;
                if (plen >= MAX_PAYLOAD) return -1;
                payload[plen++] = static_cast<uint8_t>((hi << 4) | lo);
            }
        }
    }

    int total = RESP_OVERHEAD + plen;
    uint8_t* buf = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(total)));
    if (!buf) return -1;

    std::memcpy(buf, RESP_HEADER, MAGIC_LEN);
    store_i16le(buf + RESP_OFF_STATUS, static_cast<int16_t>(status));
    store_u32le(buf + RESP_OFF_SIZE,   static_cast<uint32_t>(plen));
    store_u16le(buf + RESP_OFF_GROUP,  static_cast<uint16_t>(group));
    store_u16le(buf + RESP_OFF_UNIT,   static_cast<uint16_t>(unit));
    if (plen > 0) std::memcpy(buf + RESP_OFF_PAYLOAD, payload, static_cast<size_t>(plen));
    std::memcpy(buf + RESP_OFF_PAYLOAD + plen, RESP_FOOTER, MAGIC_LEN);
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
