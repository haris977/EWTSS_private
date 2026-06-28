// drs-bridge/parsers/dp_ecm/src/hf/format/mrx_g7_format.cpp
// MRx Group 7 — Signal BITE / misc: format-side encode functions and dispatcher.
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
// MRx Group 7 encoders
// =============================================================================

static int encode_mrx_signal_bite_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 16) return -1;
    std::memset(buf, 0, 16);
    double freq = 0, power = 0; long long result = 0;
    json_find_double(j, "observed_freq_hz",   freq);
    json_find_double(j, "observed_power_dbm", power);
    json_find_int(j,    "result",             result);
    store_f64le(buf + 0, freq / 1e6);
    store_f32le(buf + 8, (float)power);
    store_u16le(buf + 12, (uint16_t)result);
    return 16;
}

// =============================================================================
// MRx Group 7 format dispatcher
// =============================================================================

int mrx_g7_format(long long unit_id, const char* json, uint8_t* buf, int max_len, bool& is_ack) {
    switch (unit_id) {
        case  2: return encode_mrx_signal_bite_rsp(json, buf, max_len);
        case  4: case  6: case 10: case 12:
        case 14: case 16: case 18: case 20:
        case 22: case 24: is_ack = true; return 0;
        default: return 0;
    }
}
