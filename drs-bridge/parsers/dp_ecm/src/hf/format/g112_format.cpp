// drs-bridge/parsers/dp_ecm/src/hf/format/g112_format.cpp
// Group 112 — Fast Scan / Simulation: format-side encode functions and dispatcher.
#include "sdfc_endian.h"
#include "json_writer.h"
#include "hf_groups.h"
#include "hf_json_utils.h"
using namespace sdfc;

// ---------------------------------------------------------------------------
// Group 112 encoders
// ---------------------------------------------------------------------------
static int encode_asu_sdu_config_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    long long err = 0; json_find_int(j, "error_value", err);
    store_i16le(buf, (int16_t)err); return 4;
}

static int encode_trsdu_receiver_status(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    long long v = 0; json_find_int(j, "tr_sdu_health_status", v); buf[0] = (uint8_t)v; return 4;
}

static int encode_pa_receiver_status(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    long long v = 0; json_find_int(j, "power_amplifier_status", v); buf[0] = (uint8_t)v; return 4;
}

int g112_format(long long unit_id, const char* json, uint8_t* buf, int max_len, bool& is_ack) {
    typedef int (*EncFn)(const char*, uint8_t*, int);
    EncFn fn = nullptr;
    switch (unit_id) {
        case  2: fn = encode_asu_sdu_config_rsp;    break;
        case  4: fn = encode_trsdu_receiver_status; break;
        case  6: fn = encode_pa_receiver_status;    break;
        case 14: is_ack = true;                     break; // Simulation Mode Config ACK
        case 38: is_ack = true;                     break; // HF fast scan ACK
        default: return 0;
    }
    if (is_ack) return 0;
    return fn(json, buf, max_len);
}
