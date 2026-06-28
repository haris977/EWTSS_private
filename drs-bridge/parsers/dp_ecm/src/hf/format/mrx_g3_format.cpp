// drs-bridge/parsers/dp_ecm/src/hf/format/mrx_g3_format.cpp
// MRx Group 3 — RF Board / Channel Management: format-side encode functions and dispatcher.
#include "sdfc_endian.h"
#include "json_writer.h"
#include "hf_groups.h"
#include "hf_json_utils.h"
using namespace sdfc;

// =============================================================================
// MRx Group 3 encoders
// =============================================================================

static int encode_mrx_board_count_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    long long count = 0, tuner_id = 0;
    json_find_int(j, "board_count",        count);
    json_find_int(j, "available_tuner_id", tuner_id);
    store_u16le(buf + 0, (uint16_t)count); store_u16le(buf + 2, (uint16_t)tuner_id);
    return 4;
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

// encode_mrx_cbit_status — duplicated from mrx_g1_format.cpp (static helper).
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
// MRx Group 3 format dispatcher
// =============================================================================

int mrx_g3_format(long long unit_id, const char* json, uint8_t* buf, int max_len, bool& is_ack) {
    switch (unit_id) {
        case  2: return encode_mrx_board_count_rsp(json, buf, max_len);
        case 18: return encode_mrx_channels_16b(json, "\"channels\"", "status", buf, max_len);
        case 20: is_ack = true; return 0;                  // Write Channel ACK
        case 22: return encode_mrx_channels_16b(json, "\"channel_init_statuses\"", "init_status", buf, max_len);
        case 24: return encode_mrx_tuning_details_rsp(json, buf, max_len);
        case 26: return encode_mrx_cbit_status(json, buf, max_len);
        default: return 0;
    }
}
