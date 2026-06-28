// drs-bridge/parsers/dp_ecm/src/hf/format/mrx_g5_format.cpp
// MRx Group 5 — Tuner: format-side encode functions and dispatcher.
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
// MRx Group 5 encoders
// =============================================================================

static int encode_mrx_agc_status_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    long long rf = 0, ifatt = 0, agc = 0;
    json_find_int(j, "rf_attenuation_db", rf);
    json_find_int(j, "if_attenuation_db", ifatt);
    json_find_int(j, "agc_running",       agc);
    buf[0] = (uint8_t)rf; buf[1] = (uint8_t)ifatt; store_u16le(buf + 2, (uint16_t)agc);
    return 4;
}

// =============================================================================
// MRx Group 5 format dispatcher
// =============================================================================

int mrx_g5_format(long long unit_id, const char* json, uint8_t* buf, int max_len, bool& is_ack) {
    switch (unit_id) {
        case  2: is_ack = true; return 0;                // Set Center Freq ACK
        case  4: is_ack = true; return 0;                // Attenuation Select ACK
        case 10: is_ack = true; return 0;                // Clear Center Freq ACK
        case 14: return encode_mrx_agc_status_rsp(json, buf, max_len);
        default: return 0;
    }
}
