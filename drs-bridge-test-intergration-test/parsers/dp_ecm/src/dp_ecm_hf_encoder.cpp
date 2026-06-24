// dp_ecm_hf_encoder.cpp
//
// format_response implementation for the DP-ECM HF variant (1-30 MHz).
// Uses nlohmann/json for robust, schema-safe JSON parsing.
//
// JSON input contract for format_response:
//   - Detection arrays always use the key "detections" regardless of type
//     (hopper / fixed-freq / burst — the group_id+unit_id says which kind)
//   - All scalar field names are ICD-faithful (match parse_message output)
//   - Missing fields produce their zero/default value — no silent corruption
//   - Wrong field names return -1 where the field is required (count/size fields)
//
// Binary output is byte-identical to what parse_message expects to decode.
//
// Prerequisites:
//   include/json.hpp  (nlohmann/json single-header, v3.x)
//   Download: https://github.com/nlohmann/json/releases/latest -> json.hpp

#include "sdfc_abi.h"
#include "sdfc_frame.h"
#include "sdfc_endian.h"
#include "json.hpp"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>
#include <algorithm>

using json = nlohmann::json;
using namespace sdfc;

// ---------------------------------------------------------------------------
// Helper: safe JSON parse — returns false on any parse error
// ---------------------------------------------------------------------------
static bool safe_parse(const char* raw, json& out) {
    try {
        out = json::parse(raw);
        return true;
    } catch (...) {
        return false;
    }
}

// ---------------------------------------------------------------------------
// Helper: TOA field
// Accepts toa_h/toa_m/toa_s integer fields OR "toa": "HH:MM:SS" string.
// prefix = "toa" → reads toa_h, toa_m, toa_s (or "toa": "HH:MM:SS")
// prefix = "duration" → reads duration_h, duration_m, duration_s
// ---------------------------------------------------------------------------
static void get_hms(const json& j, const char* prefix,
                    uint8_t& h, uint8_t& m, uint8_t& s) {
    h = m = s = 0;
    std::string sh = std::string(prefix) + "_h";
    std::string sm = std::string(prefix) + "_m";
    std::string ss = std::string(prefix) + "_s";
    h = (uint8_t)j.value(sh, 0);
    m = (uint8_t)j.value(sm, 0);
    s = (uint8_t)j.value(ss, 0);
    if (h || m || s) return;
    auto it = j.find(prefix);
    if (it != j.end() && it->is_string()) {
        unsigned hh = 0, mm = 0, ss2 = 0;
        std::sscanf(it->get<std::string>().c_str(), "%u:%u:%u", &hh, &mm, &ss2);
        h = (uint8_t)hh; m = (uint8_t)mm; s = (uint8_t)ss2;
    }
}

// ============================================================================
// Group 100 — System / BITE / Hardware diagnostics
// ============================================================================

// 100/2  System Version — 20B
// @0  sjc_fw_version(f32)  @4  driver_version(f32)  @8  fpga_version(f32)
// @12 processor_id(u16)    @14 sjc_rf_tuner_id(u16) @16 fpga_type_id(u16)
static int enc_system_version(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 20) return -1;
    std::memset(buf, 0, 20);
    store_f32le(buf +  0, (float)j.value("sjc_fw_version",  0.0));
    store_f32le(buf +  4, (float)j.value("driver_version",  0.0));
    store_f32le(buf +  8, (float)j.value("fpga_version",    0.0));
    store_u16le(buf + 12, (uint16_t)j.value("processor_id",    0));
    store_u16le(buf + 14, (uint16_t)j.value("sjc_rf_tuner_id", 0));
    store_u16le(buf + 16, (uint16_t)j.value("fpga_type_id",    0));
    return 20;
}

// 100/4  SRX Checksum — 1024B
// @0  sjc_fw_checksum as raw string bytes (up to 1023 chars + null)
static int enc_srx_checksum(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 1024) return -1;
    std::memset(buf, 0, 1024);
    std::string ck = j.value("sjc_fw_checksum", "");
    size_t n = std::min(ck.size(), (size_t)1023);
    std::memcpy(buf, ck.data(), n);
    return 1024;
}

// 100/6  PBIT Status — 24B
// Each byte maps to one status field (21 fields, rest reserved=0)
static int enc_pbit_status(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 24) return -1;
    std::memset(buf, 0, 24);
    buf[ 0] = (uint8_t)j.value("combined_drx_status",         0);
    buf[ 1] = (uint8_t)j.value("combined_temperature_status", 0);
    buf[ 2] = (uint8_t)j.value("combined_voltage_status",     0);
    buf[ 3] = (uint8_t)j.value("combined_exciter_status",     0);
    buf[ 4] = (uint8_t)j.value("fpga_scratch_pad_test",       0);
    buf[ 5] = (uint8_t)j.value("fpga_board_id",               0);
    buf[ 6] = (uint8_t)j.value("fpga_init_test",              0);
    buf[ 7] = (uint8_t)j.value("fpga_temperature_status",     0);
    buf[ 8] = (uint8_t)j.value("rf_psu_temp_sensor",          0);
    buf[ 9] = (uint8_t)j.value("rf_psu_5v_monitor_status",    0);
    buf[10] = (uint8_t)j.value("rf_psu_8v5_monitor_status",   0);
    buf[11] = (uint8_t)j.value("fan_voltage_monitor_status",  0);
    buf[12] = (uint8_t)j.value("digital_5v_monitor_status",   0);
    buf[13] = (uint8_t)j.value("digital_3v5_monitor_status",  0);
    buf[14] = (uint8_t)j.value("digital_psu_temp_status",     0);
    buf[15] = (uint8_t)j.value("msata_detection_status",      0);
    buf[16] = (uint8_t)j.value("storage_avail_check_status",  0);
    buf[17] = (uint8_t)j.value("fan_speed_ctrl_sensor_test",  0);
    buf[18] = (uint8_t)j.value("rf_tuner_health_status",      0);
    buf[19] = (uint8_t)j.value("drx_pll_health_status",       0);
    buf[20] = (uint8_t)j.value("fan_temperature_status",      0);
    return 24;
}

// 100/8  IBIT Status — 8B
static int enc_ibit_status(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 8) return -1;
    std::memset(buf, 0, 8);
    buf[0] = (uint8_t)j.value("combined_drx_status",   0);
    buf[1] = (uint8_t)j.value("storage_health_status", 0);
    buf[2] = (uint8_t)j.value("drx_pll_health_test",   0);
    buf[3] = (uint8_t)j.value("rf_tuner_health_test",  0);
    buf[4] = (uint8_t)j.value("drx_adc_health_test",   0);
    buf[5] = (uint8_t)j.value("msata_rw_test",         0);
    buf[6] = (uint8_t)j.value("storage_avail_check",   0);
    return 8;
}

// 100/10 Temperature — 24B (6 × f32)
static int enc_temperature(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 24) return -1;
    store_f32le(buf +  0, (float)j.value("processor_temp_c", 0.0));
    store_f32le(buf +  4, (float)j.value("psu_temp_c",       0.0));
    store_f32le(buf +  8, (float)j.value("fan_temp_c",       0.0));
    store_f32le(buf + 12, (float)j.value("rf_psu_temp_c",    0.0));
    store_f32le(buf + 16, (float)j.value("digital_temp_c",   0.0));
    store_f32le(buf + 20, (float)j.value("fpga_temp_c",      0.0));
    return 24;
}

// 100/14 Fan Speed Status — 4B
static int enc_fan_speed_status(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    store_u32le(buf, (uint32_t)j.value("fan_speed_rpm", 0));
    return 4;
}

// 100/16 Ethernet Test — 12B
static int enc_ethernet_test(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 12) return -1;
    store_u32le(buf + 0, (uint32_t)j.value("tx_data", 0));
    store_u32le(buf + 4, (uint32_t)j.value("rx_data", 0));
    store_u32le(buf + 8, (uint32_t)j.value("result",  0));
    return 12;
}

// 100/18 UART Test — 4B
static int enc_uart_test(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    buf[0] = (uint8_t)j.value("expected_data", 0);
    buf[1] = (uint8_t)j.value("observed_data", 0);
    buf[2] = (uint8_t)j.value("result",        0);
    return 4;
}

// 100/22 Fan Voltage Status — 24B (6 × f32)
static int enc_fan_voltage_status(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 24) return -1;
    store_f32le(buf +  0, (float)j.value("fan_adc_voltage_v", 0.0));
    store_f32le(buf +  4, (float)j.value("rf1_voltage_v",     0.0));
    store_f32le(buf +  8, (float)j.value("rf2_voltage_v",     0.0));
    store_f32le(buf + 12, (float)j.value("rf3_voltage_v",     0.0));
    store_f32le(buf + 16, (float)j.value("digital_5v_v",      0.0));
    store_f32le(buf + 20, (float)j.value("digital_3v3_v",     0.0));
    return 24;
}

// 100/24 PPS Test — 16B
static int enc_pps_test(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 16) return -1;
    std::memset(buf, 0, 16);
    store_u32le(buf + 0, (uint32_t)j.value("on_period_us",  0));
    store_u32le(buf + 4, (uint32_t)j.value("off_period_us", 0));
    store_u32le(buf + 8, (uint32_t)j.value("pps_status",    0));
    buf[12] = (uint8_t)j.value("result", 0);
    return 16;
}

// 100/28 FPGA Temperature Details — 4B
static int enc_fpga_temperature_details(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    store_f32le(buf, (float)j.value("fpga_temperature", 0.0));
    return 4;
}

// 100/30 CBIT Status — 4B
static int enc_cbit_status(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    buf[0] = (uint8_t)j.value("tuner_board_id_status", 0);
    buf[1] = (uint8_t)j.value("voltage_status",        0);
    buf[2] = (uint8_t)j.value("temperature_status",    0);
    buf[3] = (uint8_t)j.value("memory_status",         0);
    return 4;
}

// ============================================================================
// Group 101 — FH / FF / Burst Detection, FFT, Scan
// ============================================================================

// 101/40 FH Detection
// Wire: hopper_count(u16) + reserved(u16) + hopper_count × S_HOPPER_DATA(40B)
// S_HOPPER_DATA: @0 hopper_number(u32) @4 min_freq_mhz(f32) @8 max_freq_mhz(f32)
//   @12 pulse_len_ms(f32) @16 inter_hop_ms(f32) @20 detected_count(u32)
//   @24 toa[H,M,S,0](4B) @28 power_dbm(f32) @32 freq_active(u16) @34 rsv(u16)
//   @36 snr_db(f32)
// JSON key: "detections" array (each element = one hopper)
static int enc_fh_detection(const json& j, uint8_t* buf, int max_len) {
    int hc = j.value("hopper_count", 0);
    if (hc <= 0 || 4 + hc * 40 > max_len) return -1;

    store_u16le(buf + 0, (uint16_t)hc);
    store_u16le(buf + 2, 0);

    if (!j.contains("detections") || !j["detections"].is_array()) return -1;
    const auto& arr = j["detections"];

    int written = 0;
    for (const auto& e : arr) {
        if (written >= hc) break;
        uint8_t* s = buf + 4 + written * 40;

        store_u32le(s +  0, (uint32_t)e.value("hopper_number",       0));
        store_f32le(s +  4, (float)(e.value("min_freq_hz",          0.0) / 1e6));
        store_f32le(s +  8, (float)(e.value("max_freq_hz",          0.0) / 1e6));
        store_f32le(s + 12, (float)(e.value("pulse_length_s",       0.0) * 1e3));
        store_f32le(s + 16, (float)(e.value("inter_hop_period_s",   0.0) * 1e3));
        store_u32le(s + 20, (uint32_t)e.value("detected_count",     0));
        s[24] = (uint8_t)e.value("toa_h",   0);
        s[25] = (uint8_t)e.value("toa_m",   0);
        s[26] = (uint8_t)e.value("toa_s",   0);
        s[27] = 0;
        store_f32le(s + 28, (float)e.value("power_dbm",             0.0));
        store_u16le(s + 32, (uint16_t)e.value("freq_active",        0));
        store_u16le(s + 34, 0);
        store_f32le(s + 36, (float)e.value("snr_db",                0.0));
        ++written;
    }
    return 4 + written * 40;
}

// 101/44 Wideband FFT
// Wire: start_MHz(u32) + stop_MHz(u32) + step_kHz(u32) + point_count(u32)
//       + point_count × power_dbm(f32)
static int enc_wideband_fft(const json& j, uint8_t* buf, int max_len) {
    int pc = j.value("point_count", 0);
    int total = 16 + pc * 4;
    if (total > max_len) return -1;
    std::memset(buf, 0, (size_t)total);
    store_u32le(buf +  0, (uint32_t)(j.value("start_freq_hz", 0.0) / 1e6));
    store_u32le(buf +  4, (uint32_t)(j.value("stop_freq_hz",  0.0) / 1e6));
    store_u32le(buf +  8, (uint32_t)(j.value("step_freq_hz",  0.0) / 1e3));
    store_u32le(buf + 12, (uint32_t)pc);
    if (j.contains("power_dbm") && j["power_dbm"].is_array()) {
        const auto& pa = j["power_dbm"];
        for (int i = 0; i < pc && i < (int)pa.size(); ++i)
            store_f32le(buf + 16 + i * 4, (float)pa[i].get<double>());
    }
    return total;
}

// 101/70 FF Detection
// Wire: S_RES_WIDEBAND_FFT_DATA(6408B, zeroed) + ff_count(u32)
//       + ff_count × S_DETECTED_FIXED_FREQUENCY(36B)
// S_DETECTED_FIXED_FREQUENCY: @0 freq_mhz(f32) @4 current_power_dbm(f32)
//   @8 active_count(u32) @12 min_power_dbm(f32) @16 max_power_dbm(f32)
//   @20 toa[H,M,S,0](4B) @24 dur[H,M,S,0](4B)
//   @28 freq_active(u16)+rsv(u16) @32 snr_db(f32)
// JSON key: "detections" array  count key: "ff_count"
static int enc_ff_detection(const json& j, uint8_t* buf, int max_len) {
    const int FFT_BLOCK = 4 + 1600 * 4 + 4;
    const int FF_ELEM   = 36;

    int fc = j.value("ff_count", 0);
    if (fc < 0) fc = 0;
    if (FFT_BLOCK + 4 + fc * FF_ELEM > max_len) return -1;

    std::memset(buf, 0, (size_t)FFT_BLOCK);
    store_u32le(buf + FFT_BLOCK, (uint32_t)fc);
    if (fc == 0) return FFT_BLOCK + 4;

    if (!j.contains("detections") || !j["detections"].is_array())
        return FFT_BLOCK + 4;
    const auto& arr = j["detections"];

    int written = 0;
    for (const auto& e : arr) {
        if (written >= fc) break;
        uint8_t h = 0, m = 0, s = 0, dh = 0, dm = 0, ds = 0;
        get_hms(e, "toa",      h,  m,  s);
        get_hms(e, "duration", dh, dm, ds);

        uint8_t* sl = buf + FFT_BLOCK + 4 + written * FF_ELEM;
        store_f32le(sl +  0, (float)(e.value("freq_hz",           0.0) / 1e6));
        store_f32le(sl +  4, (float)e.value("current_power_dbm", 0.0));
        store_u32le(sl +  8, (uint32_t)e.value("active_count",   0));
        store_f32le(sl + 12, (float)e.value("min_power_dbm",     0.0));
        store_f32le(sl + 16, (float)e.value("max_power_dbm",     0.0));
        sl[20] = h;  sl[21] = m;  sl[22] = s;  sl[23] = 0;
        sl[24] = dh; sl[25] = dm; sl[26] = ds; sl[27] = 0;
        store_u16le(sl + 28, (uint16_t)e.value("freq_active", 0));
        store_u16le(sl + 30, 0);
        store_f32le(sl + 32, (float)e.value("snr_db", 0.0));
        ++written;
    }
    return FFT_BLOCK + 4 + written * FF_ELEM;
}

// 101/84 Burst Detection
// Wire: burst_count(u32) + burst_count × S_DETECTED_BURST_FREQUENCY(24B)
// S_DETECTED_BURST_FREQUENCY: @0 freq_mhz(f32) @4 current_power_dbm(f32)
//   @8 pulse_length_ms(f32) @12 active_count(u32) @16 toa[H,M,S,0](4B)
//   @20 snr_db(f32)
// JSON key: "detections" array  count key: "burst_count"
static int enc_burst_detection(const json& j, uint8_t* buf, int max_len) {
    const int ELEM = 24;
    int bc = j.value("burst_count", 0);
    if (bc < 0) bc = 0;
    if (4 + bc * ELEM > max_len) return -1;

    store_u32le(buf, (uint32_t)bc);
    if (bc == 0) return 4;

    if (!j.contains("detections") || !j["detections"].is_array()) return 4;
    const auto& arr = j["detections"];

    int written = 0;
    for (const auto& e : arr) {
        if (written >= bc) break;
        uint8_t h = 0, m = 0, s = 0;
        get_hms(e, "toa", h, m, s);

        uint8_t* sl = buf + 4 + written * ELEM;
        store_f32le(sl +  0, (float)(e.value("freq_hz",           0.0) / 1e6));
        store_f32le(sl +  4, (float)e.value("current_power_dbm", 0.0));
        store_f32le(sl +  8, (float)e.value("pulse_length_ms",   0.0));
        store_u32le(sl + 12, (uint32_t)e.value("active_count",   0));
        sl[16] = h; sl[17] = m; sl[18] = s; sl[19] = 0;
        store_f32le(sl + 20, (float)e.value("snr_db", 0.0));
        ++written;
    }
    return 4 + written * ELEM;
}

// 101/88 Stop Scan Speed
// Wire: ff_count(u32) + 1600 × S_DETECTED_FIXED_FREQUENCY(36B) = 57604B fixed
// JSON key: "detections" array  count key: "ff_count"
static int enc_stop_scan_speed(const json& j, uint8_t* buf, int max_len) {
    const int TOTAL = 4 + 1600 * 36;
    if (max_len < TOTAL) return -1;
    std::memset(buf, 0, (size_t)TOTAL);

    int fc = j.value("ff_count", 0);
    if (fc > 1600) fc = 1600;
    store_u32le(buf, (uint32_t)fc);
    if (fc == 0) return TOTAL;

    if (!j.contains("detections") || !j["detections"].is_array()) return TOTAL;
    const auto& arr = j["detections"];

    int written = 0;
    for (const auto& e : arr) {
        if (written >= fc) break;
        uint8_t h = 0, m = 0, s = 0, dh = 0, dm = 0, ds = 0;
        get_hms(e, "toa",      h,  m,  s);
        get_hms(e, "duration", dh, dm, ds);

        uint8_t* sl = buf + 4 + written * 36;
        store_f32le(sl +  0, (float)(e.value("freq_hz",           0.0) / 1e6));
        store_f32le(sl +  4, (float)e.value("current_power_dbm", 0.0));
        store_u32le(sl +  8, (uint32_t)e.value("active_count",   0));
        store_f32le(sl + 12, (float)e.value("min_power_dbm",     0.0));
        store_f32le(sl + 16, (float)e.value("max_power_dbm",     0.0));
        sl[20] = h;  sl[21] = m;  sl[22] = s;  sl[23] = 0;
        sl[24] = dh; sl[25] = dm; sl[26] = ds; sl[27] = 0;
        store_u16le(sl + 28, (uint16_t)e.value("freq_active", 0));
        store_u16le(sl + 30, 0);
        store_f32le(sl + 32, (float)e.value("snr_db", 0.0));
        ++written;
    }
    return TOTAL;
}

// 101/95 Zoom FFT
// Wire: sample_count(u32) + sample_count × power_dbm(f32)
static int enc_zoom_fft(const json& j, uint8_t* buf, int max_len) {
    int sc = j.value("sample_count", 0);
    if (sc < 0 || sc > 1600) sc = 0;
    int total = 4 + sc * 4;
    if (total > max_len) return -1;
    std::memset(buf, 0, (size_t)total);
    store_u32le(buf, (uint32_t)sc);
    if (sc == 0) return 4;
    if (!j.contains("power_dbm") || !j["power_dbm"].is_array()) return total;
    const auto& arr = j["power_dbm"];
    for (int i = 0; i < sc && i < (int)arr.size(); ++i)
        store_f32le(buf + 4 + i * 4, (float)arr[i].get<double>());
    return total;
}

// ============================================================================
// Group 109 — Auto Threshold / Channelization
// ============================================================================

// 109/16 Auto Threshold Value
// Wire: bin_count(u32) + 1600 × threshold_value(f32) = 6404B fixed
static int enc_auto_threshold_value(const json& j, uint8_t* buf, int max_len) {
    const int TOTAL = 4 + 1600 * 4;
    if (max_len < TOTAL) return -1;
    std::memset(buf, 0, (size_t)TOTAL);

    int bc = j.value("auto_threshold_bin_count", 0);
    if (bc > 1600) bc = 1600;
    store_u32le(buf, (uint32_t)bc);
    if (bc == 0) return TOTAL;

    if (!j.contains("auto_threshold_bin_data") || !j["auto_threshold_bin_data"].is_array())
        return TOTAL;
    const auto& arr = j["auto_threshold_bin_data"];
    for (int i = 0; i < bc && i < (int)arr.size(); ++i)
        store_f32le(buf + 4 + i * 4, (float)arr[i].get<double>());
    return TOTAL;
}

// Shared channelization encoder (109/18 and 111/16)
// Wire: count(u32) @0 + toa[H,M,S,0] @4 + count × Entry(28B) @8
// Entry: toi(f64)@0 freq_index_hz(f32,MHz)@8 pulse_length_ms(f32)@12
//        power_level_dbm(f32)@16 bandwidth(u32)@20 freq_band(u32)@24
static int enc_channelization(const json& j, const char* arr_key,
                               uint8_t* buf, int max_len) {
    int count = j.value("channelization_count", 0);
    if (count < 0) count = 0;
    int total = 8 + count * 28;
    if (total > max_len) return -1;
    std::memset(buf, 0, (size_t)total);
    store_u32le(buf, (uint32_t)count);

    uint8_t h = 0, m = 0, s = 0;
    get_hms(j, "toa", h, m, s);
    buf[4] = h; buf[5] = m; buf[6] = s;

    if (count == 0) return 8;
    if (!j.contains(arr_key) || !j[arr_key].is_array()) return 8;
    const auto& arr = j[arr_key];

    int written = 0;
    for (const auto& e : arr) {
        if (written >= count) break;
        uint8_t* sl = buf + 8 + written * 28;
        store_f64le(sl +  0, e.value("toi",             0.0));
        store_f32le(sl +  8, (float)(e.value("freq_index_hz", 0.0) / 1e6));
        store_f32le(sl + 12, (float)e.value("pulse_length_ms", 0.0));
        store_f32le(sl + 16, (float)e.value("power_level_dbm", 0.0));
        store_u32le(sl + 20, (uint32_t)e.value("bandwidth",    0));
        store_u32le(sl + 24, (uint32_t)e.value("freq_band",    0));
        ++written;
    }
    return 8 + written * 28;
}

// 109/18 Hopper Channelization
static int enc_hopper_channelization(const json& j, uint8_t* buf, int max_len) {
    return enc_channelization(j, "hopper_channelizations", buf, max_len);
}

// ============================================================================
// Group 111 — Signal BITE / Module Health / PDW / Storage / Bands
// ============================================================================

// 111/4  Signal BITE Response — 12B
// @0 bite_freq_mhz(f32) @4 bite_power_dbm(f32) @8 bite_result(u16)
static int enc_signal_bite_resp(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 12) return -1;
    std::memset(buf, 0, 12);
    store_f32le(buf + 0, (float)(j.value("bite_freq_hz",   0.0) / 1e6));
    store_f32le(buf + 4, (float)j.value("bite_power_dbm", 0.0));
    store_u16le(buf + 8, (uint16_t)j.value("bite_result", 0));
    return 12;
}

// 111/8  Module Health — 4B
static int enc_module_health(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    buf[0] = (uint8_t)j.value("drx_health",      0);
    buf[1] = (uint8_t)j.value("rf_tuner_health", 0);
    return 4;
}

// 111/16 PDW Channelization — same wire as 109/18 but different array key
static int enc_pdw_channelization(const json& j, uint8_t* buf, int max_len) {
    return enc_channelization(j, "channelization_data", buf, max_len);
}

// 111/22 BITE Observed Response — 16B
// @0 observed_bite_freq_mhz(f64) @8 observed_bite_power_dbm(f32) @12 bite_result(u16)
static int enc_bite_observed_rsp(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 16) return -1;
    std::memset(buf, 0, 16);
    store_f64le(buf + 0, j.value("observed_bite_freq_mhz",  0.0));
    store_f32le(buf + 8, (float)j.value("observed_bite_power_dbm", 0.0));
    store_u16le(buf + 12, (uint16_t)j.value("bite_result",  0));
    return 16;
}

// 111/26 Storage Details — 20B
// @0 disk_space_1-3(u8×3) @3 rsv @4 available_disk_space(f64) @12 total_disk_space(f64)
static int enc_storage_details(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 20) return -1;
    std::memset(buf, 0, 20);
    buf[0] = (uint8_t)j.value("disk_space_1", 0);
    buf[1] = (uint8_t)j.value("disk_space_2", 0);
    buf[2] = (uint8_t)j.value("disk_space_3", 0);
    store_f64le(buf +  4, j.value("available_disk_space", 0.0));
    store_f64le(buf + 12, j.value("total_disk_space",     0.0));
    return 20;
}

// 111/28 Read Protected Band List
// Wire: count(u16)+rsv(u16) + count × {start_freq_mhz(f32)+stop_freq_mhz(f32)}
static int enc_read_protected_band_list(const json& j, uint8_t* buf, int max_len) {
    int count = j.value("protected_band_count", 0);
    if (count < 0) count = 0;
    int total = 4 + count * 8;
    if (total > max_len) return -1;
    std::memset(buf, 0, (size_t)total);
    store_u16le(buf, (uint16_t)count);
    if (count == 0) return 4;
    if (!j.contains("protected_bands") || !j["protected_bands"].is_array()) return 4;
    const auto& arr = j["protected_bands"];
    int written = 0;
    for (const auto& e : arr) {
        if (written >= count) break;
        uint8_t* sl = buf + 4 + written * 8;
        store_f32le(sl + 0, (float)(e.value("start_freq_hz", 0.0) / 1e6));
        store_f32le(sl + 4, (float)(e.value("stop_freq_hz",  0.0) / 1e6));
        ++written;
    }
    return 4 + written * 8;
}

// ============================================================================
// Group 112 — ASU/SDU/TRSDU/PA Configuration
// ============================================================================

// 112/2  ASU/SDU Config Response — 4B (@0 error_value i16)
static int enc_asu_sdu_config_rsp(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    store_i16le(buf, (int16_t)j.value("error_value", 0));
    return 4;
}

// 112/4  TRSDU Receiver Status — 4B
static int enc_trsdu_receiver_status(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    buf[0] = (uint8_t)j.value("tr_sdu_health_status", 0);
    return 4;
}

// 112/6  PA Receiver Status — 4B
static int enc_pa_receiver_status(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    buf[0] = (uint8_t)j.value("power_amplifier_status", 0);
    return 4;
}

// ============================================================================
// Group 200 — Jam Reports / Health
// ============================================================================

// 200/16 List Jam Report
// Wire: count(u32) + count × {freq_hz(u32)+status(u16)+rsv(u16)}
static int enc_list_jam_report(const json& j, uint8_t* buf, int max_len) {
    int count = j.value("list_jam_freq_count", 0);
    if (count < 0) count = 0;
    int total = 4 + count * 8;
    if (total > max_len) return -1;
    std::memset(buf, 0, (size_t)total);
    store_u32le(buf, (uint32_t)count);
    if (count == 0) return 4;
    if (!j.contains("frequencies") || !j["frequencies"].is_array()) return 4;
    const auto& arr = j["frequencies"];
    int written = 0;
    for (const auto& e : arr) {
        if (written >= count) break;
        uint8_t* sl = buf + 4 + written * 8;
        store_u32le(sl + 0, (uint32_t)e.value("freq_hz", 0));
        store_u16le(sl + 4, (uint16_t)e.value("status",  0));
        ++written;
    }
    return 4 + written * 8;
}

// 200/42 HPASU Health Status — 4B
static int enc_hpasu_health_status_rsp(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    buf[0] = (uint8_t)j.value("health_status", 0);
    return 4;
}

// 200/55 PA/SDU Health Status — 8B
// @0-3 pa1-pa4 health  @4 sdu_health
static int enc_pa_sdu_health_status_rsp(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 8) return -1;
    std::memset(buf, 0, 8);
    for (int i = 0; i < 4; ++i) {
        std::string k = "pa" + std::to_string(i + 1) + "_health";
        buf[i] = (uint8_t)j.value(k, 0);
    }
    buf[4] = (uint8_t)j.value("sdu_health", 0);
    return 8;
}

// ============================================================================
// Group 106 — Jamming Control
// ============================================================================

// 106/10 Stop Immediate Jam Response — 4B
static int enc_stop_immediate_jam_rsp(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    buf[0] = (uint8_t)j.value("exciter_retry_count", 0);
    return 4;
}

// 106/40 Ext Modulation Response — 4B
static int enc_ext_modulation_response(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    store_u32le(buf, (uint32_t)j.value("software_buffer_size", 0));
    return 4;
}

// 106/54 Immediate Jam ACK — 8B
// @0 jam_id(u16) @2 rsv(u16) @4 jam_active(u16) @6 rsv(u16)
static int enc_immediate_jam_ack(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 8) return -1;
    std::memset(buf, 0, 8);
    store_u16le(buf + 0, (uint16_t)j.value("jam_id",     0));
    store_u16le(buf + 4, (uint16_t)j.value("jam_active", 0));
    return 8;
}

// ============================================================================
// MRx Group 1 — System / BITE
// ============================================================================

// 1/2  MRx System Version — 16B
static int enc_mrx_system_version(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 16) return -1;
    std::memset(buf, 0, 16);
    store_f32le(buf +  0, (float)j.value("fw_version",     0.0));
    store_f32le(buf +  4, (float)j.value("driver_version", 0.0));
    store_f32le(buf +  8, (float)j.value("fpga_version",   0.0));
    store_u16le(buf + 12, (uint16_t)j.value("rf_tuner_id", 0));
    return 16;
}

// 1/4  MRx Checksum — 1024B
static int enc_mrx_checksum(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 1024) return -1;
    std::memset(buf, 0, 1024);
    std::string ck = j.value("sjc_fw_checksum", "");
    size_t n = std::min(ck.size(), (size_t)1023);
    std::memcpy(buf, ck.data(), n);
    return 1024;
}

// 1/6  MRx PBIT Status — 120B
static int enc_mrx_pbit_status(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 120) return -1;
    std::memset(buf, 0, 120);
    buf[ 0] = (uint8_t)j.value("fpga_scratch_pad_test",    0);
    buf[ 1] = (uint8_t)j.value("fpga_board_id_read",       0);
    buf[ 2] = (uint8_t)j.value("processor_temp_status",    0);
    buf[ 3] = (uint8_t)j.value("fan_temp_status",          0);
    buf[ 4] = (uint8_t)j.value("fpga_temp_status",         0);
    buf[ 6] = (uint8_t)j.value("rfpsu_temp_status",        0);
    buf[ 7] = (uint8_t)j.value("fan_speed_ctrl_sensor",    0);
    buf[ 8] = (uint8_t)j.value("fan_voltage_status",       0);
    buf[ 9] = (uint8_t)j.value("rfsu_5v_status",           0);
    buf[10] = (uint8_t)j.value("rfsu_8v5_status",          0);
    buf[11] = (uint8_t)j.value("msata_detection_status",   0);
    buf[12] = (uint8_t)j.value("lo1_pll_lock_status",      0);
    buf[13] = (uint8_t)j.value("lo2_pll_lock_status",      0);
    buf[14] = (uint8_t)j.value("bite_pll_lock_status",     0);
    buf[15] = (uint8_t)j.value("tuner_detection_status",   0);
    buf[18] = (uint8_t)j.value("tuner_scratchpad_test",    0);
    buf[21] = (uint8_t)j.value("pll_lock_status",          0);
    buf[22] = (uint8_t)j.value("adc_bonding_status",       0);
    buf[23] = (uint8_t)j.value("storage_availability",     0);
    buf[24] = (uint8_t)j.value("digital_5v_status",        0);
    buf[25] = (uint8_t)j.value("digital_3v5_status",       0);
    buf[26] = (uint8_t)j.value("digital_psu_temp_status",  0);
    return 120;
}

// 1/8  MRx IBIT Status — 112B
static int enc_mrx_ibit_status(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 112) return -1;
    std::memset(buf, 0, 112);
    buf[ 0] = (uint8_t)j.value("pll_lock_status",         0);
    buf[ 1] = (uint8_t)j.value("adc_bonding_status",      0);
    buf[ 2] = (uint8_t)j.value("msata_detection_status",  0);
    buf[ 3] = (uint8_t)j.value("storage_availability",    0);
    buf[ 4] = (uint8_t)j.value("tuner_lo1_pll_lock",      0);
    buf[ 5] = (uint8_t)j.value("tuner_lo2_pll_lock",      0);
    buf[ 6] = (uint8_t)j.value("tuner_bite_pll_lock",     0);
    buf[ 7] = (uint8_t)j.value("adc1_link_status",        0);
    buf[10] = (uint8_t)j.value("tuner_detection_status",  0);
    buf[13] = (uint8_t)j.value("tuner_scratchpad_test",   0);
    return 112;
}

// 1/10 MRx Temperature — 36B (9 × f32)
static int enc_mrx_temperature(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 36) return -1;
    static const char* NAMES[] = {
        "bpt_temp_c", "psu_8156_temp_c", "tuner_temp_c", "psu_7255_temp_c",
        "processor_temp_c", "power_supply_temp_c", "control_board_temp_c",
        "rf_psu_temp_c", "fpga_temp_c"
    };
    for (int i = 0; i < 9; ++i)
        store_f32le(buf + i * 4, (float)j.value(NAMES[i], 0.0));
    return 36;
}

// 1/14 MRx Fan Speed — 4B
static int enc_mrx_fan_speed(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    store_u32le(buf, (uint32_t)j.value("fan_speed_rpm", 0));
    return 4;
}

// 1/18 MRx UART Test — 4B
static int enc_mrx_uart_test_rsp(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    buf[0] = (uint8_t)j.value("expected_data", 0);
    buf[1] = (uint8_t)j.value("observed_data", 0);
    buf[2] = (uint8_t)j.value("result",        0);
    return 4;
}

// 1/26  MRx CBIT Status — 8B
static int enc_mrx_cbit_status(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 8) return -1;
    std::memset(buf, 0, 8);
    buf[0] = (uint8_t)j.value("drx_status",              0);
    buf[1] = (uint8_t)j.value("voltage_status",          0);
    buf[2] = (uint8_t)j.value("temperature_status",      0);
    buf[4] = (uint8_t)j.value("tuner_detection_status",  0);
    buf[7] = (uint8_t)j.value("memory_status",           0);
    return 8;
}

// ============================================================================
// MRx Group 3 — Board / Channel / Tuning
// ============================================================================

// 3/2  Board Count Response — 4B
static int enc_mrx_board_count_rsp(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    store_u16le(buf + 0, (uint16_t)j.value("board_count",        0));
    store_u16le(buf + 2, (uint16_t)j.value("available_tuner_id", 0));
    return 4;
}

// 3/18 Channel Status  (arr_key="channels",             status_key="status")
// 3/22 Channel Init    (arr_key="channel_init_statuses", status_key="init_status")
// Wire: 8 × u16 = 16B (one per channel)
static int enc_mrx_channels_16b(const json& j, const char* arr_key,
                                 const char* status_key, uint8_t* buf, int max_len) {
    if (max_len < 16) return -1;
    std::memset(buf, 0, 16);
    if (!j.contains(arr_key) || !j[arr_key].is_array()) return 16;
    const auto& arr = j[arr_key];
    int written = 0;
    for (const auto& e : arr) {
        if (written >= 8) break;
        store_u16le(buf + written * 2, (uint16_t)e.value(status_key, 0));
        ++written;
    }
    return 16;
}

// 3/24 Tuning Details Response — 8B
static int enc_mrx_tuning_details_rsp(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 8) return -1;
    std::memset(buf, 0, 8);
    buf[0] = (uint8_t)j.value("srx_tuned_status",     0);
    buf[1] = (uint8_t)j.value("mrx_tuned_status",     0);
    store_u16le(buf + 2, (uint16_t)j.value("srx_scan_mode_status",   0));
    store_u16le(buf + 4, (uint16_t)j.value("tuned_center_freq_mhz",  0));
    buf[6] = (uint8_t)j.value("memory_scan_tuned", 0);
    buf[7] = (uint8_t)j.value("bite_selection",    0);
    return 8;
}

// ============================================================================
// MRx Group 4 — Audio / IQ / Memory Scan / FFT / Smart Scan / Optical
// ============================================================================

// 4/8  Audio Data Response — 4 + audio_data_size×2 bytes
static int enc_mrx_audio_data_rsp(const json& j, uint8_t* buf, int max_len) {
    int sz = j.value("audio_data_size", 0);
    if (sz < 0) sz = 0;
    if (sz > 262144) sz = 262144;
    int total = 4 + sz * 2;
    if (total > max_len) return -1;
    std::memset(buf, 0, (size_t)total);
    store_u32le(buf, (uint32_t)sz);
    if (sz == 0) return 4;
    if (!j.contains("audio_data") || !j["audio_data"].is_array()) return total;
    const auto& arr = j["audio_data"];
    for (int i = 0; i < sz && i < (int)arr.size(); ++i)
        store_u16le(buf + 4 + i * 2, (uint16_t)arr[i].get<int>());
    return total;
}

// 4/24, 4/34  IQ Start Response — 4B
static int enc_mrx_iq_start_rsp(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    buf[0] = (uint8_t)j.value("mrx_channel",          0);
    buf[1] = (uint8_t)j.value("narrowband_iq_status",  0);
    buf[2] = (uint8_t)j.value("wideband_iq_status",    0);
    return 4;
}

// 4/26 IQ Logging Stop Response — 132B
// @0 mrx_channel(u16) @2 rsv(u16) @4 file_path(char[128])
static int enc_mrx_iq_logging_stop_rsp(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 132) return -1;
    std::memset(buf, 0, 132);
    store_u16le(buf, (uint16_t)j.value("mrx_channel", 0));
    std::string fp = j.value("file_path", "");
    size_t n = std::min(fp.size(), (size_t)127);
    std::memcpy(buf + 4, fp.data(), n);
    return 132;
}

// 4/42 Memory Scan Data Response
// Wire: count(u32) + scan_speed(u16) + rsv(u16) + count × Entry(20B)
// Entry: power_dbm(f32)@0 freq_mhz(f64)@4 rsv(u16)@12 bandwidth(u16)@18 ×wait actually
//   @0 power_dbm(f32) @4 freq_mhz(f64) @18 bandwidth_list(u16)
static int enc_mrx_memory_scan_data_rsp(const json& j, uint8_t* buf, int max_len) {
    int count = j.value("total_available_count", 0);
    if (count < 0) count = 0;
    if (count > 10000) count = 10000;
    int total = 8 + count * 20;
    if (total > max_len) return -1;
    std::memset(buf, 0, (size_t)total);
    store_u32le(buf + 0, (uint32_t)count);
    store_u16le(buf + 4, (uint16_t)j.value("scan_speed_channels_per_sec", 0));
    if (count == 0) return 8;
    if (!j.contains("scan_data") || !j["scan_data"].is_array()) return 8;
    const auto& arr = j["scan_data"];
    int written = 0;
    for (const auto& e : arr) {
        if (written >= count) break;
        uint8_t* sl = buf + 8 + written * 20;
        store_f32le(sl +  0, (float)e.value("power_dbm", 0.0));
        store_f64le(sl +  4, e.value("freq_hz", 0.0) / 1e6);
        store_u16le(sl + 18, (uint16_t)e.value("bandwidth_list", 0));
        ++written;
    }
    return 8 + written * 20;
}

// 4/44 DDC FFT Response — 4 + bin_count×4 bytes
static int enc_mrx_ddc_fft_rsp(const json& j, uint8_t* buf, int max_len) {
    int bc = j.value("bin_count", 0);
    if (bc < 0) bc = 0;
    if (bc > 4096) bc = 4096;
    int total = 4 + bc * 4;
    if (total > max_len) return -1;
    std::memset(buf, 0, (size_t)total);
    store_u16le(buf, (uint16_t)bc);
    if (bc == 0) return total;
    if (!j.contains("ddc_fft_power_dbm") || !j["ddc_fft_power_dbm"].is_array())
        return total;
    const auto& arr = j["ddc_fft_power_dbm"];
    for (int i = 0; i < bc && i < (int)arr.size(); ++i)
        store_f32le(buf + 4 + i * 4, (float)arr[i].get<double>());
    return total;
}

// 4/62 Smart Scan Read Response — 16B
// @0 freq_mhz(f64) @8 mrx_channel(u16) @12 amplitude(f32)
static int enc_mrx_smart_scan_read_rsp(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 16) return -1;
    std::memset(buf, 0, 16);
    store_f64le(buf +  0, j.value("freq_hz", 0.0) / 1e6);
    store_u16le(buf +  8, (uint16_t)j.value("mrx_channel", 0));
    store_f32le(buf + 12, (float)j.value("amplitude",    0.0));
    return 16;
}

// 4/70 Optical Port Status — 12B (6 × u16)
static int enc_mrx_optical_port_status_rsp(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 12) return -1;
    std::memset(buf, 0, 12);
    store_u16le(buf +  0, (uint16_t)j.value("port_number",          0));
    store_u16le(buf +  2, (uint16_t)j.value("port_id",              0));
    store_u16le(buf +  4, (uint16_t)j.value("port_alive_status",    0));
    store_u16le(buf +  6, (uint16_t)j.value("already_transmitting", 0));
    store_u16le(buf +  8, (uint16_t)j.value("can_start_transfer",   0));
    return 12;
}

// 4/72 Optical IP Response — 24B
// @0 ip_address(4B parsed from "A.B.C.D") @4 port_ids(9 × u16 = 18B)
static int enc_mrx_optical_ip_rsp(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 24) return -1;
    std::memset(buf, 0, 24);
    std::string ip = j.value("ip_address", "");
    if (!ip.empty()) {
        unsigned a = 0, b = 0, c = 0, d = 0;
        if (std::sscanf(ip.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
            buf[0] = (uint8_t)a; buf[1] = (uint8_t)b;
            buf[2] = (uint8_t)c; buf[3] = (uint8_t)d;
        }
    }
    if (j.contains("port_ids") && j["port_ids"].is_array()) {
        const auto& arr = j["port_ids"];
        for (int i = 0; i < 9 && i < (int)arr.size(); ++i)
            store_u16le(buf + 4 + i * 2, (uint16_t)arr[i].get<int>());
    }
    return 24;
}

// ============================================================================
// MRx Group 5 — AGC
// ============================================================================

// 5/14 AGC Status Response — 4B
static int enc_mrx_agc_status_rsp(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    buf[0] = (uint8_t)j.value("rf_attenuation_db", 0);
    buf[1] = (uint8_t)j.value("if_attenuation_db", 0);
    store_u16le(buf + 2, (uint16_t)j.value("agc_running", 0));
    return 4;
}

// ============================================================================
// MRx Group 7 — Signal BITE
// ============================================================================

// 7/2  Signal BITE Response — 16B
// @0 observed_freq_mhz(f64) @8 observed_power_dbm(f32) @12 result(u16)
static int enc_mrx_signal_bite_rsp(const json& j, uint8_t* buf, int max_len) {
    if (max_len < 16) return -1;
    std::memset(buf, 0, 16);
    store_f64le(buf +  0, j.value("observed_freq_hz", 0.0) / 1e6);
    store_f32le(buf +  8, (float)j.value("observed_power_dbm", 0.0));
    store_u16le(buf + 12, (uint16_t)j.value("result", 0));
    return 16;
}

// ============================================================================
// ABI: format_response
// ============================================================================
extern "C" SDFC_EXPORT int format_response(const char* /*kind*/,
                                            const char* kwargs_json,
                                            uint8_t** out_buf, size_t* out_len) {
    if (!kwargs_json || !out_buf || !out_len) return -1;

    json j;
    if (!safe_parse(kwargs_json, j)) return -1;

    int group  = j.value("group_id", -1);
    int unit   = j.value("unit_id",  -1);
    int status = j.value("status",    0);
    if (group < 0 || unit < 0) return -1;

    uint8_t* payload = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(MAX_PAYLOAD)));
    if (!payload) return -1;
    int plen = 0;

    // Dispatch: group/unit → encoder function
    typedef int (*EncFn)(const json&, uint8_t*, int);
    EncFn fn = nullptr;

    // Group 100
    if      (group == 100 && unit ==  2) fn = enc_system_version;
    else if (group == 100 && unit ==  4) fn = enc_srx_checksum;
    else if (group == 100 && unit ==  6) fn = enc_pbit_status;
    else if (group == 100 && unit ==  8) fn = enc_ibit_status;
    else if (group == 100 && unit == 10) fn = enc_temperature;
    else if (group == 100 && unit == 14) fn = enc_fan_speed_status;
    else if (group == 100 && unit == 16) fn = enc_ethernet_test;
    else if (group == 100 && unit == 18) fn = enc_uart_test;
    else if (group == 100 && unit == 22) fn = enc_fan_voltage_status;
    else if (group == 100 && unit == 24) fn = enc_pps_test;
    else if (group == 100 && unit == 28) fn = enc_fpga_temperature_details;
    else if (group == 100 && unit == 30) fn = enc_cbit_status;
    // Group 101
    else if (group == 101 && unit == 40) fn = enc_fh_detection;
    else if (group == 101 && unit == 44) fn = enc_wideband_fft;
    else if (group == 101 && unit == 70) fn = enc_ff_detection;
    else if (group == 101 && unit == 84) fn = enc_burst_detection;
    else if (group == 101 && unit == 88) fn = enc_stop_scan_speed;
    else if (group == 101 && unit == 95) fn = enc_zoom_fft;
    // Group 109
    else if (group == 109 && unit == 16) fn = enc_auto_threshold_value;
    else if (group == 109 && unit == 18) fn = enc_hopper_channelization;
    // Group 111
    else if (group == 111 && unit ==  4) fn = enc_signal_bite_resp;
    else if (group == 111 && unit ==  8) fn = enc_module_health;
    else if (group == 111 && unit == 16) fn = enc_pdw_channelization;
    else if (group == 111 && unit == 22) fn = enc_bite_observed_rsp;
    else if (group == 111 && unit == 26) fn = enc_storage_details;
    else if (group == 111 && unit == 28) fn = enc_read_protected_band_list;
    // Group 112
    else if (group == 112 && unit ==  2) fn = enc_asu_sdu_config_rsp;
    else if (group == 112 && unit ==  4) fn = enc_trsdu_receiver_status;
    else if (group == 112 && unit ==  6) fn = enc_pa_receiver_status;
    // Group 200
    else if (group == 200 && unit == 16) fn = enc_list_jam_report;
    else if (group == 200 && unit == 42) fn = enc_hpasu_health_status_rsp;
    else if (group == 200 && unit == 55) fn = enc_pa_sdu_health_status_rsp;
    // Group 106
    else if (group == 106 && unit == 10) fn = enc_stop_immediate_jam_rsp;
    else if (group == 106 && unit == 40) fn = enc_ext_modulation_response;
    else if (group == 106 && unit == 54) fn = enc_immediate_jam_ack;
    // MRx Group 1
    else if (group ==   1 && unit ==  2) fn = enc_mrx_system_version;
    else if (group ==   1 && unit ==  4) fn = enc_mrx_checksum;
    else if (group ==   1 && unit ==  6) fn = enc_mrx_pbit_status;
    else if (group ==   1 && unit ==  8) fn = enc_mrx_ibit_status;
    else if (group ==   1 && unit == 10) fn = enc_mrx_temperature;
    else if (group ==   1 && unit == 14) fn = enc_mrx_fan_speed;
    else if (group ==   1 && unit == 18) fn = enc_mrx_uart_test_rsp;
    else if (group ==   1 && unit == 26) fn = enc_mrx_cbit_status;
    // MRx Group 3
    else if (group ==   3 && unit ==  2) fn = enc_mrx_board_count_rsp;
    else if (group ==   3 && (unit == 18 || unit == 22)) {
        const char* ak = (unit == 18) ? "channels" : "channel_init_statuses";
        const char* sk = (unit == 18) ? "status"   : "init_status";
        plen = enc_mrx_channels_16b(j, ak, sk, payload, MAX_PAYLOAD);
        if (plen < 0) { std::free(payload); return -1; }
    }
    else if (group ==   3 && unit == 24) fn = enc_mrx_tuning_details_rsp;
    else if (group ==   3 && unit == 26) fn = enc_mrx_cbit_status;
    // MRx Group 4
    else if (group ==   4 && unit ==  8) fn = enc_mrx_audio_data_rsp;
    else if (group ==   4 && (unit == 24 || unit == 34)) fn = enc_mrx_iq_start_rsp;
    else if (group ==   4 && unit == 26) fn = enc_mrx_iq_logging_stop_rsp;
    else if (group ==   4 && unit == 42) fn = enc_mrx_memory_scan_data_rsp;
    else if (group ==   4 && unit == 44) fn = enc_mrx_ddc_fft_rsp;
    else if (group ==   4 && unit == 62) fn = enc_mrx_smart_scan_read_rsp;
    else if (group ==   4 && unit == 70) fn = enc_mrx_optical_port_status_rsp;
    else if (group ==   4 && unit == 72) fn = enc_mrx_optical_ip_rsp;
    // MRx Group 5
    else if (group ==   5 && unit == 14) fn = enc_mrx_agc_status_rsp;
    // MRx Group 7
    else if (group ==   7 && unit ==  2) fn = enc_mrx_signal_bite_rsp;

    if (fn) {
        plen = fn(j, payload, MAX_PAYLOAD);
        if (plen < 0) { std::free(payload); return -1; }
    }

    // payload_hex fallback: if no structured encoder matched and caller provided
    // raw hex bytes, embed them directly (useful for unknown/future unit IDs)
    if (plen == 0 && j.contains("payload_hex") && j["payload_hex"].is_string()) {
        std::string hex = j["payload_hex"].get<std::string>();
        for (size_t i = 0; i + 1 < hex.size() && plen < MAX_PAYLOAD; i += 2) {
            auto nibble = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = nibble(hex[i]), lo = nibble(hex[i + 1]);
            if (hi < 0 || lo < 0) { std::free(payload); return -1; }
            payload[plen++] = static_cast<uint8_t>((hi << 4) | lo);
        }
    }

    int total = RESP_OVERHEAD + plen;
    uint8_t* frame = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(total)));
    if (!frame) { std::free(payload); return -1; }

    std::memcpy(frame, RESP_HEADER, MAGIC_LEN);
    store_i16le(frame + RESP_OFF_STATUS, static_cast<int16_t>(status));
    store_u32le(frame + RESP_OFF_SIZE,   static_cast<uint32_t>(plen));
    store_u16le(frame + RESP_OFF_GROUP,  static_cast<uint16_t>(group));
    store_u16le(frame + RESP_OFF_UNIT,   static_cast<uint16_t>(unit));
    if (plen > 0)
        std::memcpy(frame + RESP_OFF_PAYLOAD, payload, static_cast<size_t>(plen));
    std::memcpy(frame + RESP_OFF_PAYLOAD + plen, RESP_FOOTER, MAGIC_LEN);

    std::free(payload);
    *out_buf = frame;
    *out_len = static_cast<size_t>(total);
    return 0;
}
