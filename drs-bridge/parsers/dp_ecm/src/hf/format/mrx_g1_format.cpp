// drs-bridge/parsers/dp_ecm/src/hf/format/mrx_g1_format.cpp
// MRx Group 1 — Diagnostics: format-side encode functions and dispatcher.
#include "sdfc_endian.h"
#include "json_writer.h"
#include "hf_groups.h"
#include "hf_json_utils.h"
using namespace sdfc;

// =============================================================================
// MRx Group 1 encoders
// =============================================================================

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

// =============================================================================
// MRx Group 1 format dispatcher
// =============================================================================

int mrx_g1_format(long long unit_id, const char* json, uint8_t* buf, int max_len, bool& is_ack) {
    typedef int (*EncFn)(const char*, uint8_t*, int);
    EncFn fn = nullptr;
    switch (unit_id) {
        case  2: fn = encode_mrx_system_version; break;
        case  4: fn = encode_mrx_checksum;        break;
        case  6: fn = encode_mrx_pbit_status;     break;
        case  8: fn = encode_mrx_ibit_status;     break;
        case 10: fn = encode_mrx_temperature;     break;
        case 14: fn = encode_mrx_fan_speed;       break;
        case 18: fn = encode_mrx_uart_test_rsp;   break;
        case 26: fn = encode_mrx_cbit_status;     break;
        case 34: is_ack = true;                   break; // Close All Channels ACK
        default: return 0;
    }
    if (is_ack) return 0;
    return fn(json, buf, max_len);
}
