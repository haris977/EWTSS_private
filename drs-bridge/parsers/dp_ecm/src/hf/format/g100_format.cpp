// drs-bridge/parsers/dp_ecm/src/hf/format/g100_format.cpp
// Group 100 — SJC Diagnostics: format-side encode functions and dispatcher.
#include "sdfc_endian.h"
#include "json_writer.h"
#include "hf_groups.h"
#include "hf_json_utils.h"
using namespace sdfc;

// ---------------------------------------------------------------------------
// Group 100 encoders
// ---------------------------------------------------------------------------
static int encode_system_version(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 20) return -1;
    std::memset(buf, 0, 20);
    double sjc_fw = 0, driver = 0, fpga = 0;
    long long proc_id = 0, tuner_id = 0, fpga_type = 0;
    json_find_double(j, "sjc_fw_version",  sjc_fw);
    json_find_double(j, "driver_version",  driver);
    json_find_double(j, "fpga_version",    fpga);
    json_find_int(j,    "processor_id",    proc_id);
    json_find_int(j,    "sjc_rf_tuner_id", tuner_id);
    json_find_int(j,    "fpga_type_id",    fpga_type);
    store_f32le(buf +  0, (float)sjc_fw);
    store_f32le(buf +  4, (float)driver);
    store_f32le(buf +  8, (float)fpga);
    store_u16le(buf + 12, (uint16_t)proc_id);
    store_u16le(buf + 14, (uint16_t)tuner_id);
    store_u16le(buf + 16, (uint16_t)fpga_type);
    return 20;
}

static int encode_srx_checksum(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 1024) return -1;
    std::memset(buf, 0, 1024);
    const char* key = std::strstr(j, "\"sjc_fw_checksum\"");
    if (!key) return 1024;
    const char* c = std::strchr(key, ':');
    if (!c) return 1024;
    while (*c && *c != '"') ++c;
    if (*c != '"') return 1024;
    ++c;
    size_t idx = 0;
    while (*c && *c != '"' && idx < 1023) buf[idx++] = (uint8_t)*c++;
    return 1024;
}

static int encode_pbit_status(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 24) return -1;
    std::memset(buf, 0, 24);
    long long v = 0;
    auto gi = [&](const char* k, int i) { if (json_find_int(j, k, v)) buf[i] = (uint8_t)v; };
    gi("combined_drx_status",         0);  gi("combined_temperature_status", 1);
    gi("combined_voltage_status",     2);  gi("combined_exciter_status",     3);
    gi("fpga_scratch_pad_test",       4);  gi("fpga_board_id",               5);
    gi("fpga_init_test",              6);  gi("fpga_temperature_status",     7);
    gi("rf_psu_temp_sensor",          8);  gi("rf_psu_5v_monitor_status",    9);
    gi("rf_psu_8v5_monitor_status",  10);  gi("fan_voltage_monitor_status", 11);
    gi("digital_5v_monitor_status",  12);  gi("digital_3v5_monitor_status", 13);
    gi("digital_psu_temp_status",    14);  gi("msata_detection_status",     15);
    gi("storage_avail_check_status", 16);  gi("fan_speed_ctrl_sensor_test", 17);
    gi("rf_tuner_health_status",     18);  gi("drx_pll_health_status",      19);
    gi("fan_temperature_status",     20);
    return 24;
}

static int encode_ibit_status(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 8) return -1;
    std::memset(buf, 0, 8);
    long long v = 0;
    auto gi = [&](const char* k, int i) { if (json_find_int(j, k, v)) buf[i] = (uint8_t)v; };
    gi("combined_drx_status",   0);  gi("storage_health_status", 1);
    gi("drx_pll_health_test",   2);  gi("rf_tuner_health_test",  3);
    gi("drx_adc_health_test",   4);  gi("msata_rw_test",         5);
    gi("storage_avail_check",   6);
    return 8;
}

static int encode_temperature(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 24) return -1;
    double v = 0;
    auto gd = [&](const char* k, int i) { v = 0; json_find_double(j, k, v); store_f32le(buf + i, (float)v); };
    gd("processor_temp_c",  0); gd("psu_temp_c",      4); gd("fan_temp_c",    8);
    gd("rf_psu_temp_c",    12); gd("digital_temp_c", 16); gd("fpga_temp_c",  20);
    return 24;
}

static int encode_fan_speed_status(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    long long rpm = 0; json_find_int(j, "fan_speed_rpm", rpm);
    store_u32le(buf, (uint32_t)rpm); return 4;
}

static int encode_ethernet_test(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 12) return -1;
    long long tx = 0, rx = 0, result = 0;
    json_find_int(j, "tx_data", tx); json_find_int(j, "rx_data", rx); json_find_int(j, "result", result);
    store_u32le(buf + 0, (uint32_t)tx); store_u32le(buf + 4, (uint32_t)rx); store_u32le(buf + 8, (uint32_t)result);
    return 12;
}

static int encode_uart_test(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    long long v = 0;
    if (json_find_int(j, "expected_data", v)) buf[0] = (uint8_t)v;
    if (json_find_int(j, "observed_data", v)) buf[1] = (uint8_t)v;
    if (json_find_int(j, "result",        v)) buf[2] = (uint8_t)v;
    return 4;
}

static int encode_fan_voltage_status(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 24) return -1;
    double v = 0;
    auto gd = [&](const char* k, int i) { v = 0; json_find_double(j, k, v); store_f32le(buf + i, (float)v); };
    gd("fan_adc_voltage_v", 0); gd("rf1_voltage_v",  4); gd("rf2_voltage_v",  8);
    gd("rf3_voltage_v",    12); gd("digital_5v_v",  16); gd("digital_3v3_v", 20);
    return 24;
}

static int encode_pps_test(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 16) return -1;
    std::memset(buf, 0, 16);
    long long on = 0, off = 0, status = 0, result = 0;
    json_find_int(j, "on_period_us", on); json_find_int(j, "off_period_us", off);
    json_find_int(j, "pps_status", status); json_find_int(j, "result", result);
    store_u32le(buf + 0, (uint32_t)on); store_u32le(buf + 4, (uint32_t)off);
    store_u32le(buf + 8, (uint32_t)status); buf[12] = (uint8_t)result;
    return 16;
}

static int encode_fpga_temperature_details(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    double temp = 0; json_find_double(j, "fpga_temperature", temp);
    store_f32le(buf, (float)temp); return 4;
}

static int encode_cbit_status(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    long long v = 0;
    if (json_find_int(j, "tuner_board_id_status", v)) buf[0] = (uint8_t)v;
    if (json_find_int(j, "voltage_status",         v)) buf[1] = (uint8_t)v;
    if (json_find_int(j, "temperature_status",     v)) buf[2] = (uint8_t)v;
    if (json_find_int(j, "memory_status",          v)) buf[3] = (uint8_t)v;
    return 4;
}

// ---------------------------------------------------------------------------
// Group 100 format dispatcher
// ---------------------------------------------------------------------------
int g100_format(long long unit_id, const char* json, uint8_t* buf, int max_len, bool& is_ack) {
    typedef int (*EncFn)(const char*, uint8_t*, int);
    EncFn fn = nullptr;
    switch (unit_id) {
        case  2: fn = encode_system_version;           break;
        case  4: fn = encode_srx_checksum;             break;
        case  6: fn = encode_pbit_status;              break;
        case  8: fn = encode_ibit_status;              break;
        case 10: fn = encode_temperature;              break;
        case 12: is_ack = true;                        break; // Set Fan Speed ACK
        case 14: fn = encode_fan_speed_status;         break;
        case 16: fn = encode_ethernet_test;            break;
        case 18: fn = encode_uart_test;                break;
        case 22: fn = encode_fan_voltage_status;       break;
        case 24: fn = encode_pps_test;                 break;
        case 26: is_ack = true;                        break; // RS422 Test ACK
        case 28: fn = encode_fpga_temperature_details; break;
        case 30: fn = encode_cbit_status;              break;
        default: return 0; // unknown unit — caller falls through to payload_hex
    }
    if (is_ack) return 0;
    return fn(json, buf, max_len);
}
