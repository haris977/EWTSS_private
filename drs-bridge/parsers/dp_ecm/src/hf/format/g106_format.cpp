// drs-bridge/parsers/dp_ecm/src/hf/format/g106_format.cpp
// Group 106 — ECM Immediate Jamming: format-side encode functions and dispatcher.
#include "sdfc_endian.h"
#include "json_writer.h"
#include "hf_groups.h"
#include "hf_json_utils.h"
using namespace sdfc;

// ---------------------------------------------------------------------------
// Group 106 encoders
// ---------------------------------------------------------------------------
static int encode_stop_immediate_jam_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    long long v = 0; json_find_int(j, "exciter_retry_count", v); buf[0] = (uint8_t)v; return 4;
}

static int encode_ext_modulation_response(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    long long v = 0; json_find_int(j, "software_buffer_size", v);
    store_u32le(buf, (uint32_t)v); return 4;
}

static int encode_immediate_jam_ack(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 8) return -1;
    std::memset(buf, 0, 8);
    long long jam_id = 0, jam_active = 0;
    json_find_int(j, "jam_id", jam_id); json_find_int(j, "jam_active", jam_active);
    store_u16le(buf + 0, (uint16_t)jam_id); store_u16le(buf + 4, (uint16_t)jam_active);
    return 8;
}

// ---------------------------------------------------------------------------
// Group 106 dispatcher
// ---------------------------------------------------------------------------
int g106_format(long long unit_id, const char* json, uint8_t* buf, int max_len, bool& is_ack) {
    typedef int (*EncFn)(const char*, uint8_t*, int);
    EncFn fn = nullptr;
    switch (unit_id) {
        case  2: is_ack = true; break; // Start Immediate Jam ACK
        case  4: is_ack = true; break; // TDM Jam ACK
        case  6: is_ack = true; break; // FDM Jam ACK
        case 10: fn = encode_stop_immediate_jam_rsp;  break;
        case 22: is_ack = true; break; // Enable PA ACK
        case 40: fn = encode_ext_modulation_response; break;
        case 42: is_ack = true; break; // Sweep Jam ACK
        case 46: is_ack = true; break; // Enable PA+SDU ACK
        case 50: is_ack = true; break; // Configure Prog Exciter ACK
        case 54: fn = encode_immediate_jam_ack;       break;
        case 56: is_ack = true; break; // Comb Noise ACK
        default: return 0;
    }
    if (is_ack) return 0;
    return fn(json, buf, max_len);
}
