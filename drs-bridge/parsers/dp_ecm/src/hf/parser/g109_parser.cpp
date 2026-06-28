// drs-bridge/parsers/dp_ecm/src/hf/parser/g109_parser.cpp
// Group 109 — Date/Time: parse-side decode functions and dispatchers.
#include "sdfc_endian.h"
#include "json_writer.h"
#include "hf_groups.h"
#include "hf_json_utils.h"
using namespace sdfc;

// =============================================================================
// Group 109 command decoder
// =============================================================================

// 109/11 — Set Date and Time (8 bytes). Same as VU.
static void decode_cmd_set_date_time(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "set_date_time cmd < 8 bytes"); return; }
    w.key_uint("day",     p[0]);
    w.key_uint("month",   p[1]);
    w.key_uint("year",    load_u16le(p + 2));
    w.key_uint("hour",    p[4]);
    w.key_uint("minute",  p[5]);
    w.key_uint("seconds", p[6]);
}

// 109/18 — Hopper Channelization Data response (per ICD Table 183).
// Message size: 4 + 4 + (28 × up to 16384) entries.
// Header: Channelization Data Count (uint32) + S_HOPPER_CHANNELIZATION_TOA (4B: H,M,S,rsv)
// S_HOPPER_CHANNELIZATION layout (28 bytes per entry):
//   @0  TOI (double,8B)  @8  Frequency Index (float→Hz)  @12 Pulse Length (float)
//   @16 Power Level (dBm) (float)  @20 Bandwidth (uint32)  @24 Frequency Band (uint32)
static void decode_hopper_channelization(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "hopper_channelization payload < 8 bytes"); return; }
    uint32_t count = load_u32le(p + 0);
    w.key_uint("channelization_count", count);
    char toa[16];
    std::snprintf(toa, sizeof(toa), "%02u:%02u:%02u", p[4], p[5], p[6]);
    w.key_str("toa", toa);

    const int      ELEM      = 28;
    const uint32_t MAX_SLOTS = 16384;
    uint32_t valid = count < MAX_SLOTS ? count : MAX_SLOTS;

    std::string arr = "[";
    uint32_t emitted = 0;
    for (uint32_t i = 0; i < valid; ++i) {
        int off = 8 + static_cast<int>(i) * ELEM;
        if (off + ELEM > n) break;
        const uint8_t* e = p + off;
        JsonWriter c;
        c.key_double("toi",             load_f64le(e +  0));
        c.key_double("freq_index_hz",   static_cast<double>(load_f32le(e +  8)) * 1e6);
        c.key_double("pulse_length_ms", static_cast<double>(load_f32le(e + 12)));
        c.key_double("power_level_dbm", static_cast<double>(load_f32le(e + 16)));
        c.key_uint("bandwidth",          load_u32le(e + 20));
        c.key_uint("freq_band",          load_u32le(e + 24));
        if (emitted++) arr += ',';
        arr += c.str();
    }
    arr += "]";
    w.key_raw("hopper_channelizations", arr);
}

// 109/16 — Auto Threshold Value response (6404 bytes, per ICD Table 180/181).
// @0 Auto Threshold Bin Count (uint32, 4B)
// @4 Auto Threshold Bin Data  (float[1600], 6400B)
static void decode_auto_threshold_value(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 6404) { w.key_str("warning", "auto_threshold_value payload < 6404 bytes"); return; }
    uint32_t bin_count = load_u32le(p + 0);
    w.key_uint("auto_threshold_bin_count", bin_count);
    uint32_t emit = bin_count < 1600 ? bin_count : 1600;
    std::string arr = "[";
    for (uint32_t i = 0; i < emit; ++i) {
        if (i) arr += ',';
        char tmp[32];
        std::snprintf(tmp, sizeof(tmp), "%.4f",
            static_cast<double>(load_f32le(p + 4 + i * 4)));
        arr += tmp;
    }
    arr += "]";
    w.key_raw("auto_threshold_bin_data", arr);
}

bool g109_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case 11: decode_cmd_set_date_time(p, n, w); return true;
        case 15: return true; // Send Auto Threshold Value — 0 bytes
        case 17: return true; // Acquire Hopper Channelization — 0 bytes
        default: return false;
    }
}

bool g109_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case 12: return true;                              // Set Date/Time ACK
        case 16: decode_auto_threshold_value(p, n, w);    return true;
        case 18: decode_hopper_channelization(p, n, w);   return true;
        default: return false;
    }
}
