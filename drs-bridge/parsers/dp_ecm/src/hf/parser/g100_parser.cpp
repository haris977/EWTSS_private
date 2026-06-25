// drs-bridge/parsers/dp_ecm/src/hf/parser/g100_parser.cpp
// Group 100 — SJC Diagnostics: parse-side decode functions and dispatchers.
#include "sdfc_endian.h"
#include "json_writer.h"
#include "hf_groups.h"
using namespace sdfc;

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
    const char* cs = reinterpret_cast<const char*>(p);
    w.key_str("sjc_fw_checksum", std::string(cs, strnlen(cs, 1024)));
}

// 100/6 — PBIT Status (24 bytes, per ICD §3.x).
static void decode_pbit_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 24) { w.key_str("warning", "pbit_status payload < 24 bytes"); return; }
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
    // p[21] reserved
}

// 100/8 — IBIT Status (8 bytes, per ICD Table 10).
static void decode_ibit_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "ibit_status payload < 8 bytes"); return; }
    w.key_uint("combined_drx_status",   p[ 0]);
    w.key_uint("storage_health_status", p[ 1]);
    w.key_uint("drx_pll_health_test",   p[ 2]);
    w.key_uint("rf_tuner_health_test",  p[ 3]);
    w.key_uint("drx_adc_health_test",   p[ 4]);
    w.key_uint("msata_rw_test",         p[ 5]);
    w.key_uint("storage_avail_check",   p[ 6]);
    // p[7] reserved
}

// 100/10 — Temperature Status (24 bytes, 6 floats, per ICD Table 16).
static void decode_temperature(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 24) { w.key_str("warning", "temperature payload < 24 bytes"); return; }
    w.key_double("processor_temp_c",   static_cast<double>(load_f32le(p +  0)));
    w.key_double("psu_temp_c",         static_cast<double>(load_f32le(p +  4)));
    w.key_double("fan_temp_c",         static_cast<double>(load_f32le(p +  8)));
    w.key_double("rf_psu_temp_c",      static_cast<double>(load_f32le(p + 12)));
    w.key_double("digital_temp_c",     static_cast<double>(load_f32le(p + 16)));
    w.key_double("fpga_temp_c",        static_cast<double>(load_f32le(p + 20)));
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

// 100/30 — CBIT Status (4 bytes, per ICD).
static void decode_cbit_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "cbit_status payload < 4 bytes"); return; }
    w.key_uint("tuner_board_id_status", p[0]);
    w.key_uint("voltage_status",        p[1]);
    w.key_uint("temperature_status",    p[2]);
    w.key_uint("memory_status",         p[3]);
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
// Group 100 new diagnostic decoders
// =============================================================================

// 100/16 — Ethernet Test Status response (12 bytes).
// @0 TX_Data (uint32)  @4 RX_Data (uint32)  @8 Result (uint32, 1=PASS,0=FAIL)
static void decode_ethernet_test(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 12) { w.key_str("warning", "ethernet_test payload < 12 bytes"); return; }
    w.key_uint("tx_data", load_u32le(p + 0));
    w.key_uint("rx_data", load_u32le(p + 4));
    w.key_uint("result",  load_u32le(p + 8));
}

// 100/22 — Read Fan Voltage Status response (24 bytes, 6 floats).
static void decode_fan_voltage_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 24) { w.key_str("warning", "fan_voltage payload < 24 bytes"); return; }
    w.key_double("fan_adc_voltage_v", static_cast<double>(load_f32le(p +  0)));
    w.key_double("rf1_voltage_v",     static_cast<double>(load_f32le(p +  4)));
    w.key_double("rf2_voltage_v",     static_cast<double>(load_f32le(p +  8)));
    w.key_double("rf3_voltage_v",     static_cast<double>(load_f32le(p + 12)));
    w.key_double("digital_5v_v",      static_cast<double>(load_f32le(p + 16)));
    w.key_double("digital_3v3_v",     static_cast<double>(load_f32le(p + 20)));
}

// 100/24 — PPS Test Status response (16 bytes).
static void decode_pps_test(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) { w.key_str("warning", "pps_test payload < 16 bytes"); return; }
    w.key_uint("on_period_us",  load_u32le(p +  0));
    w.key_uint("off_period_us", load_u32le(p +  4));
    w.key_uint("pps_status",    load_u32le(p +  8));
    w.key_uint("result",        p[12]);
    w.key_str("result_name", p[12] == 1 ? "pass" : "fail");
}

// 100/28 — FPGA Temperature Details response (4 bytes).
static void decode_fpga_temperature_details(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "fpga_temperature < 4 bytes"); return; }
    w.key_double("fpga_temperature", static_cast<double>(load_f32le(p + 0)));
}

// 100/11 — Set Fan Speed command (4 bytes).
static void decode_cmd_set_fan_speed(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "set_fan_speed cmd < 4 bytes"); return; }
    w.key_uint("fan_speed_rpm", load_u32le(p + 0));
}

// =============================================================================
// Group 100 dispatchers
// =============================================================================

bool g100_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  1: return true;                                  // Get System Version — 0 bytes
        case  3: return true;                                  // Get SRX Checksum — 0 bytes
        case  5: return true;                                  // Get PBIT Status — 0 bytes
        case  7: return true;                                  // Get IBIT Status — 0 bytes
        case  9: return true;                                  // Get Temperature — 0 bytes
        case 11: decode_cmd_set_fan_speed(p, n, w);           return true;
        case 13: return true;                                  // Fan Speed Status — 0 bytes
        case 15: return true;                                  // Ethernet Test — 0 bytes
        case 17: decode_cmd_uart_port_select(p, n, w);        return true;
        case 21: return true;                                  // Fan Voltage Status — 0 bytes
        case 23: return true;                                  // PPS Test — 0 bytes
        case 25: return true;                                  // RS422 Test — 0 bytes
        case 27: return true;                                  // FPGA Temperature — 0 bytes
        case 29: return true;                                  // CBIT Status — 0 bytes
        default: return false;
    }
}

bool g100_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  2: decode_system_version(p, n, w);           return true;
        case  4: decode_srx_checksum(p, n, w);             return true;
        case  6: decode_pbit_status(p, n, w);              return true;
        case  8: decode_ibit_status(p, n, w);              return true;
        case 10: decode_temperature(p, n, w);              return true;
        case 12: return true;                              // Set Fan Speed ACK
        case 14: decode_fan_speed_status(p, n, w);         return true;
        case 16: decode_ethernet_test(p, n, w);            return true;
        case 18: decode_uart_test(p, n, w);                return true;
        case 22: decode_fan_voltage_status(p, n, w);       return true;
        case 24: decode_pps_test(p, n, w);                 return true;
        case 26: return true;                              // RS422 Test Status
        case 28: decode_fpga_temperature_details(p, n, w); return true;
        case 30: decode_cbit_status(p, n, w);              return true;
        default: return false;
    }
}
