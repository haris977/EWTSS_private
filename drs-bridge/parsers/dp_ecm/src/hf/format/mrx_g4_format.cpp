// drs-bridge/parsers/dp_ecm/src/hf/format/mrx_g4_format.cpp
// MRx Group 4 — Data Acquisition: format-side encode functions and dispatcher.
#include "sdfc_endian.h"
#include "json_writer.h"
#include "hf_groups.h"
#include "hf_json_utils.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace sdfc;

// =============================================================================
// MRx Group 4 encoders
// =============================================================================

static int encode_mrx_audio_data_rsp(const char* j, uint8_t* buf, int max_len) {
    long long audio_size = 0; json_find_int(j, "audio_data_size", audio_size);
    if (audio_size < 0) audio_size = 0;
    if (audio_size > 262144) audio_size = 262144;
    int total = 4 + (int)audio_size * 2;
    if (total > max_len) return -1;
    std::memset(buf, 0, (size_t)total);
    store_u32le(buf, (uint32_t)audio_size);
    if (audio_size == 0) return 4;
    const char* pd = std::strstr(j, "\"audio_data\""); if (!pd) return total;
    const char* arr = std::strchr(pd, '['); if (!arr) return total; ++arr;
    int written = 0;
    while (written < (int)audio_size) {
        while (*arr == ' ' || *arr == '\t' || *arr == '\n' || *arr == ',') ++arr;
        if (*arr == ']' || !*arr) break;
        char* end; long long v = std::strtoll(arr, &end, 10);
        if (end == arr) break;
        store_u16le(buf + 4 + written * 2, (uint16_t)v); arr = end; ++written;
    }
    return total;
}

static int encode_mrx_iq_start_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    long long v = 0;
    if (json_find_int(j, "mrx_channel",          v)) buf[0] = (uint8_t)v;
    if (json_find_int(j, "narrowband_iq_status",  v)) buf[1] = (uint8_t)v;
    if (json_find_int(j, "wideband_iq_status",    v)) buf[2] = (uint8_t)v;
    return 4;
}

static int encode_mrx_iq_logging_stop_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 132) return -1;
    std::memset(buf, 0, 132);
    long long ch = 0; json_find_int(j, "mrx_channel", ch); store_u16le(buf, (uint16_t)ch);
    const char* key = std::strstr(j, "\"file_path\""); if (!key) return 132;
    const char* c = std::strchr(key, ':'); if (!c) return 132;
    while (*c && *c != '"') ++c; if (*c != '"') return 132; ++c;
    size_t idx = 0;
    while (*c && *c != '"' && idx < 127) buf[4 + idx++] = (uint8_t)*c++;
    return 132;
}

static int encode_mrx_memory_scan_data_rsp(const char* j, uint8_t* buf, int max_len) {
    long long count = 0, scan_speed = 0;
    json_find_int(j, "total_available_count",       count);
    json_find_int(j, "scan_speed_channels_per_sec", scan_speed);
    if (count < 0) count = 0; if (count > 10000) count = 10000;
    int total = 8 + (int)count * 20;
    if (total > max_len) return -1;
    std::memset(buf, 0, (size_t)total);
    store_u32le(buf + 0, (uint32_t)count); store_u16le(buf + 4, (uint16_t)scan_speed);
    if (count == 0) return 8;
    const char* pd = std::strstr(j, "\"scan_data\""); if (!pd) return 8;
    const char* arr = std::strchr(pd, '['); if (!arr) return 8;
    const char* p = arr + 1; int written = 0;
    while (written < (int)count) {
        while (*p && *p != '{' && *p != ']') ++p;
        if (!*p || *p == ']') break;
        const char* os = p; int depth = 1; ++p;
        while (*p && depth > 0) { if (*p == '{') ++depth; else if (*p == '}') --depth; ++p; }
        std::string entry(os, p); const char* e = entry.c_str();
        double power = 0, freq = 0; long long bw = 0;
        json_find_double(e, "power_dbm", power); json_find_double(e, "freq_hz", freq);
        json_find_int(e, "bandwidth_list", bw);
        uint8_t* slot = buf + 8 + written * 20;
        store_f32le(slot +  0, (float)power);
        store_f64le(slot +  4, freq / 1e6);
        store_u16le(slot + 18, (uint16_t)bw);
        ++written;
    }
    return 8 + written * 20;
}

static int encode_mrx_ddc_fft_rsp(const char* j, uint8_t* buf, int max_len) {
    long long bc = 0; json_find_int(j, "bin_count", bc);
    if (bc < 0) bc = 0; if (bc > 4096) bc = 4096;
    int total = 4 + (int)bc * 4;
    if (total > max_len) return -1;
    std::memset(buf, 0, (size_t)total);
    store_u16le(buf, (uint16_t)bc);
    if (bc == 0) return total;
    const char* pd = std::strstr(j, "\"ddc_fft_power_dbm\""); if (!pd) return total;
    const char* arr = std::strchr(pd, '['); if (!arr) return total; ++arr;
    int written = 0;
    while (written < (int)bc) {
        while (*arr == ' ' || *arr == '\t' || *arr == '\n' || *arr == ',') ++arr;
        if (*arr == ']' || !*arr) break;
        char* end; float v = (float)std::strtod(arr, &end);
        if (end == arr) break;
        store_f32le(buf + 4 + written * 4, v); arr = end; ++written;
    }
    return total;
}

static int encode_mrx_smart_scan_read_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 16) return -1;
    std::memset(buf, 0, 16);
    double freq = 0, amplitude = 0; long long ch = 0;
    json_find_double(j, "freq_hz",    freq);
    json_find_int(j,    "mrx_channel", ch);
    json_find_double(j, "amplitude",   amplitude);
    store_f64le(buf + 0, freq / 1e6);
    store_u16le(buf + 8, (uint16_t)ch);
    store_f32le(buf + 12, (float)amplitude);
    return 16;
}

static int encode_mrx_optical_port_status_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 12) return -1;
    std::memset(buf, 0, 12);
    long long v = 0;
    auto gu = [&](const char* k, int i) { if (json_find_int(j, k, v)) store_u16le(buf + i, (uint16_t)v); };
    gu("port_number", 0); gu("port_id", 2); gu("port_alive_status", 4);
    gu("already_transmitting", 6); gu("can_start_transfer", 8);
    return 12;
}

static int encode_mrx_optical_ip_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 24) return -1;
    std::memset(buf, 0, 24);
    const char* ip_key = std::strstr(j, "\"ip_address\"");
    if (ip_key) {
        const char* c = std::strchr(ip_key, ':'); if (c) { while (*c && *c != '"') ++c; }
        if (c && *c == '"') { ++c;
            unsigned a = 0, b = 0, cc = 0, d = 0;
            if (std::sscanf(c, "%u.%u.%u.%u", &a, &b, &cc, &d) == 4) {
                buf[0] = (uint8_t)a; buf[1] = (uint8_t)b; buf[2] = (uint8_t)cc; buf[3] = (uint8_t)d;
            }
        }
    }
    const char* pd = std::strstr(j, "\"port_ids\"");
    if (pd) {
        const char* arr = std::strchr(pd, '['); if (arr) { ++arr;
            for (int i = 0; i < 9; ++i) {
                while (*arr == ' ' || *arr == '\t' || *arr == '\n' || *arr == ',') ++arr;
                if (*arr == ']' || !*arr) break;
                char* end; long long v = std::strtoll(arr, &end, 10);
                if (end == arr) break;
                store_u16le(buf + 4 + i * 2, (uint16_t)v); arr = end;
            }
        }
    }
    return 24;
}

// =============================================================================
// MRx Group 4 format dispatcher
// =============================================================================

int mrx_g4_format(long long unit_id, const char* json, uint8_t* buf, int max_len, bool& is_ack) {
    typedef int (*EncFn)(const char*, uint8_t*, int);
    EncFn fn = nullptr;
    switch (unit_id) {
        case  6: is_ack = true; break; // Set Threshold ACK
        case  8: fn = encode_mrx_audio_data_rsp;        break;
        case 10: is_ack = true; break; // Audio Start Play ACK
        case 12: is_ack = true; break; // Audio Stop Play ACK
        case 16: is_ack = true; break; // Audio FIFO Reset ACK
        case 18: is_ack = true; break; // Demod/BW Select ACK
        case 24: fn = encode_mrx_iq_start_rsp;          break;
        case 26: fn = encode_mrx_iq_logging_stop_rsp;   break;
        case 34: fn = encode_mrx_iq_start_rsp;          break;
        case 36: is_ack = true; break; // Stop IQ Streaming ACK
        case 40: is_ack = true; break; // Configure Memory Scan ACK
        case 42: fn = encode_mrx_memory_scan_data_rsp;  break;
        case 44: fn = encode_mrx_ddc_fft_rsp;           break;
        case 54: is_ack = true; break; // Engage Channel ACK
        case 56: is_ack = true; break; // Disengage Channel ACK
        case 58: is_ack = true; break; // Stop Memory Scan ACK
        case 60: is_ack = true; break; // Smart Memory Scan Config ACK
        case 62: fn = encode_mrx_smart_scan_read_rsp;   break;
        case 64: is_ack = true; break; // Stop Smart Memory Scan ACK
        case 66: is_ack = true; break; // Start Optical IQ ACK
        case 68: is_ack = true; break; // Stop Optical IQ ACK
        case 70: fn = encode_mrx_optical_port_status_rsp; break;
        case 72: fn = encode_mrx_optical_ip_rsp;          break;
        default: return 0;
    }
    if (is_ack) return 0;
    return fn(json, buf, max_len);
}
