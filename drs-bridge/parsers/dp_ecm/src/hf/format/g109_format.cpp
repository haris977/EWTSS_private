// drs-bridge/parsers/dp_ecm/src/hf/format/g109_format.cpp
// Group 109 — Date/Time: format-side encode functions and dispatcher.
#include "sdfc_endian.h"
#include "json_writer.h"
#include "hf_groups.h"
#include "hf_json_utils.h"
using namespace sdfc;

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

int g109_format(long long unit_id, const char* json, uint8_t* buf, int max_len, bool& is_ack) {
    typedef int (*EncFn)(const char*, uint8_t*, int);
    EncFn fn = nullptr;
    switch (unit_id) {
        case 12: is_ack = true;                     break; // Set Date/Time ACK
        case 16: fn = encode_auto_threshold_value;  break;
        case 18: fn = encode_hopper_channelization; break;
        default: return 0;
    }
    if (is_ack) return 0;
    return fn(json, buf, max_len);
}
