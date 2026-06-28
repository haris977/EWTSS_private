// drs-bridge/parsers/dp_ecm/src/hf/format/g111_format.cpp
// Group 111 — Signal/Scan: format-side encode functions and dispatcher.
#include "sdfc_endian.h"
#include "json_writer.h"
#include "hf_groups.h"
#include "hf_json_utils.h"
using namespace sdfc;

// ---------------------------------------------------------------------------
// Shared helper (static copy — encode_channelization is static in its TU;
// needed here by encode_pdw_channelization).
// Wire: count(u32)@0 + toa[H,M,S,0]@4 + count×28B entries.
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Group 111 encoders
// ---------------------------------------------------------------------------
static int encode_signal_bite_resp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 12) return -1;
    std::memset(buf, 0, 12);
    double freq = 0, power = 0; long long result = 0;
    json_find_double(j, "bite_freq_hz",   freq);
    json_find_double(j, "bite_power_dbm", power);
    json_find_int(j,    "bite_result",    result);
    store_f32le(buf + 0, (float)(freq / 1e6));
    store_f32le(buf + 4, (float)power);
    store_u16le(buf + 8, (uint16_t)result);
    return 12;
}

static int encode_bite_observed_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 16) return -1;
    std::memset(buf, 0, 16);
    double freq = 0, power = 0; long long result = 0;
    json_find_double(j, "observed_bite_freq_mhz",  freq);
    json_find_double(j, "observed_bite_power_dbm", power);
    json_find_int(j,    "bite_result",             result);
    store_f64le(buf + 0, freq);
    store_f32le(buf + 8, (float)power);
    store_u16le(buf + 12, (uint16_t)result);
    return 16;
}

static int encode_pdw_channelization(const char* j, uint8_t* buf, int max_len) {
    return encode_channelization(j, "\"channelization_data\"", buf, max_len);
}

static int encode_storage_details(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 20) return -1;
    std::memset(buf, 0, 20);
    long long ds1 = 0, ds2 = 0, ds3 = 0; double avail = 0, total = 0;
    json_find_int(j,    "disk_space_1",         ds1);
    json_find_int(j,    "disk_space_2",         ds2);
    json_find_int(j,    "disk_space_3",         ds3);
    json_find_double(j, "available_disk_space", avail);
    json_find_double(j, "total_disk_space",     total);
    buf[0] = (uint8_t)ds1; buf[1] = (uint8_t)ds2; buf[2] = (uint8_t)ds3;
    store_f64le(buf +  4, avail); store_f64le(buf + 12, total);
    return 20;
}

static int encode_module_health(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    long long drx = 0, tuner = 0;
    json_find_int(j, "drx_health",      drx);
    json_find_int(j, "rf_tuner_health", tuner);
    buf[0] = (uint8_t)drx; buf[1] = (uint8_t)tuner;
    return 4;
}

static int encode_read_protected_band_list(const char* j, uint8_t* buf, int max_len) {
    long long count = 0; json_find_int(j, "protected_band_count", count);
    if (count < 0) count = 0;
    int total = 4 + (int)count * 8;
    if (total > max_len) return -1;
    std::memset(buf, 0, (size_t)total);
    store_u16le(buf, (uint16_t)count);
    if (count == 0) return 4;
    const char* pd = std::strstr(j, "\"protected_bands\"");
    if (!pd) return 4;
    const char* arr = std::strchr(pd, '['); if (!arr) return 4;
    const char* p = arr + 1; int written = 0;
    while (written < (int)count) {
        while (*p && *p != '{' && *p != ']') ++p;
        if (!*p || *p == ']') break;
        const char* os = p; int depth = 1; ++p;
        while (*p && depth > 0) { if (*p == '{') ++depth; else if (*p == '}') --depth; ++p; }
        std::string entry(os, p); const char* e = entry.c_str();
        double start = 0, stop = 0;
        json_find_double(e, "start_freq_hz", start); json_find_double(e, "stop_freq_hz", stop);
        uint8_t* slot = buf + 4 + written * 8;
        store_f32le(slot + 0, (float)(start / 1e6)); store_f32le(slot + 4, (float)(stop / 1e6));
        ++written;
    }
    return 4 + written * 8;
}

int g111_format(long long unit_id, const char* json, uint8_t* buf, int max_len, bool& is_ack) {
    typedef int (*EncFn)(const char*, uint8_t*, int);
    EncFn fn = nullptr;
    switch (unit_id) {
        case  4: fn = encode_signal_bite_resp;          break;
        case  6: is_ack = true;                         break; // Reference Input ACK
        case  8: fn = encode_module_health;             break;
        case 10: is_ack = true;                         break; // Send Protected Scan List ACK
        case 14: is_ack = true;                         break; // Protected Scan Enable ACK
        case 16: fn = encode_pdw_channelization;        break;
        case 18: is_ack = true;                         break; // FH Splitband Enable ACK
        case 20: is_ack = true;                         break; // FH Splitband Freq ACK
        case 22: fn = encode_bite_observed_rsp;         break;
        case 24: is_ack = true;                         break; // Spectrum Protected Band ACK
        case 26: fn = encode_storage_details;           break;
        case 28: fn = encode_read_protected_band_list;  break;
        case 30: is_ack = true;                         break; // Auto Threshold Enable ACK
        case 32: is_ack = true;                         break; // Hopper Channelization ACK
        default: return 0;
    }
    if (is_ack) return 0;
    return fn(json, buf, max_len);
}
