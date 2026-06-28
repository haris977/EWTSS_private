// drs-bridge/parsers/dp_ecm/src/hf/parser/mrx_g1_parser.cpp
// MRx Group 1 — Diagnostics: parse-side decode functions and dispatchers.
#include "sdfc_endian.h"
#include "json_writer.h"
#include "hf_groups.h"
using namespace sdfc;

// =============================================================================
// MRx Group 1 response decoders
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
// MRx Group 1 dispatchers
// =============================================================================

bool mrx_g1_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  1: return true;                              // Get System Version — 0 bytes
        case  3: return true;                              // Get Checksum — 0 bytes
        case  5: return true;                              // PBIT — 0 bytes
        case  7: return true;                              // IBIT — 0 bytes
        case  9: return true;                              // Temperature — 0 bytes
        case 13: return true;                              // Fan Speed — 0 bytes
        case 17: decode_mrx_uart_test_cmd(p, n, w);       return true;
        case 25: return true;                              // CBIT — 0 bytes
        case 33: return true;                              // Close All Channels — 0 bytes
        default: return false;
    }
}

bool mrx_g1_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  2: decode_mrx_system_version(p, n, w); return true;
        case  4: decode_mrx_checksum(p, n, w);       return true;
        case  6: decode_mrx_pbit_status(p, n, w);    return true;
        case  8: decode_mrx_ibit_status(p, n, w);    return true;
        case 10: decode_mrx_temperature(p, n, w);    return true;
        case 14: decode_mrx_fan_speed(p, n, w);      return true;
        case 18: decode_mrx_uart_test_rsp(p, n, w);  return true;
        case 26: decode_mrx_cbit_status(p, n, w);    return true;
        case 34: return true;                         // Close All Channels ACK
        default: return false;
    }
}
