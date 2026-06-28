// drs-bridge/parsers/dp_ecm/src/hf/format/g200_format.cpp
// Group 200 — HF ECM Jamming: format-side encode functions and dispatcher.
#include "sdfc_endian.h"
#include "json_writer.h"
#include "hf_groups.h"
#include "hf_json_utils.h"
using namespace sdfc;

// ---------------------------------------------------------------------------
// Group 200 encoders
// ---------------------------------------------------------------------------
static int encode_list_jam_report(const char* j, uint8_t* buf, int max_len) {
    long long count = 0; json_find_int(j, "list_jam_freq_count", count);
    if (count < 0) count = 0;
    int total = 4 + (int)count * 8;
    if (total > max_len) return -1;
    std::memset(buf, 0, (size_t)total);
    store_u32le(buf, (uint32_t)count);
    if (count == 0) return 4;
    const char* pd = std::strstr(j, "\"frequencies\"");
    if (!pd) return 4;
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
        store_u32le(slot + 0, (uint32_t)freq); store_u16le(slot + 4, (uint16_t)status);
        ++written;
    }
    return 4 + written * 8;
}

static int encode_hpasu_health_status_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    long long v = 0; json_find_int(j, "health_status", v); buf[0] = (uint8_t)v; return 4;
}

static int encode_pa_sdu_health_status_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 8) return -1;
    std::memset(buf, 0, 8);
    long long v = 0; char key[16];
    for (int i = 0; i < 4; ++i) {
        std::snprintf(key, sizeof(key), "pa%d_health", i + 1);
        v = 0; json_find_int(j, key, v); buf[i] = (uint8_t)v;
    }
    v = 0; json_find_int(j, "sdu_health", v); buf[4] = (uint8_t)v;
    return 8;
}

// ---------------------------------------------------------------------------
// Group 200 dispatcher
// ---------------------------------------------------------------------------
int g200_format(long long unit_id, const char* json, uint8_t* buf, int max_len, bool& is_ack) {
    typedef int (*EncFn)(const char*, uint8_t*, int);
    EncFn fn = nullptr;
    switch (unit_id) {
        case 10: is_ack = true;                           break; // Send ECM Reports ACK
        case 12: is_ack = true;                           break; // Start List Jam ACK
        case 14: is_ack = true;                           break; // Stop List Jam ACK
        case 16: fn = encode_list_jam_report;             break;
        case 18: is_ack = true;                           break; // Start Follow-on Jam ACK
        case 20: is_ack = true;                           break; // Stop Follow-on Jam ACK
        case 22: is_ack = true;                           break; // Start Responsive Sweep Jam ACK
        case 24: is_ack = true;                           break; // Stop Responsive Sweep Jam ACK
        case 42: fn = encode_hpasu_health_status_rsp;    break;
        case 55: fn = encode_pa_sdu_health_status_rsp;   break;
        case 57: is_ack = true;                           break; // PA Soft Reboot ACK
        default: return 0;
    }
    if (is_ack) return 0;
    return fn(json, buf, max_len);
}
