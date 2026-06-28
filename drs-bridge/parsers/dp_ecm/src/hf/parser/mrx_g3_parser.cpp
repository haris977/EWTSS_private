// drs-bridge/parsers/dp_ecm/src/hf/parser/mrx_g3_parser.cpp
// MRx Group 3 — RF Board / Channel Management: parse-side decode functions and dispatchers.
#include "sdfc_endian.h"
#include "json_writer.h"
#include "hf_groups.h"
using namespace sdfc;

// =============================================================================
// MRx Group 3 response decoders
// =============================================================================

// 3/18 — Read Channel Information response (16 bytes): 8× uint16 channel status.
static void decode_mrx_all_channels_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) { w.key_str("warning", "mrx_channels_rsp < 16 bytes"); return; }
    std::string arr = "[";
    for (int i = 0; i < 8; ++i) {
        if (i) arr += ',';
        char tmp[64];
        uint16_t status = load_u16le(p + i * 2);
        std::snprintf(tmp, sizeof(tmp),
            "{\"channel\":%d,\"status\":%u,\"state\":\"%s\"}",
            i + 1, status, status == 1 ? "open" : "closed");
        arr += tmp;
    }
    arr += "]";
    w.key_raw("channels", arr);
}

// 3/22 — MRX Individual Channel Init Status response (16 bytes, per ICD Table 219).
// 8 channels × uint16: 1 = initialization success, 0 = initialization failure.
static void decode_mrx_channel_init_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) { w.key_str("warning", "mrx_channel_init_status < 16 bytes"); return; }
    std::string arr = "[";
    for (int i = 0; i < 8; ++i) {
        if (i) arr += ',';
        char tmp[64];
        uint16_t status = load_u16le(p + i * 2);
        std::snprintf(tmp, sizeof(tmp),
            "{\"channel\":%d,\"init_status\":%u,\"state\":\"%s\"}",
            i + 1, status, status == 1 ? "success" : "failure");
        arr += tmp;
    }
    arr += "]";
    w.key_raw("channel_init_statuses", arr);
}

// 3/2 — Read Board Count response (4 bytes).
static void decode_mrx_board_count_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) return;
    uint16_t count = load_u16le(p + 0);
    w.key_uint("board_count", count);
    w.key_str("note", count == 1 ? "mrx_channels_accessible" : "mrx_channels_not_accessible");
    w.key_uint("available_tuner_id", load_u16le(p + 2));
}

// 3/18 and 3/22 — Channel status response (16 bytes).
// channel1_status + 7× reserved u16. Status 1=open/initialised, 0=closed/failed.
static void decode_mrx_channel_16b_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) return;
    w.key_uint("channel1_status", load_u16le(p + 0));
    w.key_str("channel1_name", load_u16le(p + 0) == 1 ? "open_or_success" : "closed_or_fail");
}

// 3/19 — Write Channel Information command (4 bytes).
static void decode_mrx_write_channel_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) return;
    w.key_uint("mrx_channel", load_u16le(p + 0));
    w.key_uint("channel_status", load_u16le(p + 2));
    w.key_str("channel_action", load_u16le(p + 2) == 1 ? "open" : "close");
}

// 3/24 — Read MRx and SRx Tuning Details response (8 bytes).
// @0 SRx Tuned (u8)  @1 MRx Tuned (u8)  @2 SRx Scan Mode (u16)
// @4 Tuned Center Freq (u16)  @6 Memory Scan Tuned (u8)  @7 BITE Selection (u8)
static void decode_mrx_tuning_details_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "mrx_tuning_details < 8 bytes"); return; }
    static const char* SRX_TUNED[] = {"not_tuned", "search_mode", "jam_mode"};
    w.key_uint("srx_tuned_status",        p[0]);
    w.key_str("srx_tuned_status_name",    p[0] < 3 ? SRX_TUNED[p[0]] : "unknown");
    w.key_uint("mrx_tuned_status",        p[1]);
    w.key_uint("srx_scan_mode_status",    load_u16le(p + 2));
    w.key_uint("tuned_center_freq_mhz",   load_u16le(p + 4));
    w.key_uint("memory_scan_tuned",       p[6]);
    w.key_uint("bite_selection",          p[7]);
    w.key_str("bite_selection_name",      p[7] == 0 ? "antenna" : "bite");
}

// 1/26 — CBIT Status response (8 bytes). Duplicated from mrx_g1_parser.cpp (static helper).
static void decode_mrx_cbit_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "mrx_cbit_status < 8 bytes"); return; }
    w.key_uint("drx_status",            p[0]);
    w.key_uint("voltage_status",        p[1]);
    w.key_uint("temperature_status",    p[2]);
    // p[3] reserved
    w.key_uint("tuner_detection_status",p[4]);
    // p[5-6] reserved
    w.key_uint("memory_status",         p[7]);
}

// =============================================================================
// MRx Group 3 dispatchers
// =============================================================================

bool mrx_g3_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  1: return true;                                // Read Board Count — 0 bytes
        case 17: return true;                                // Read Channel Info — 0 bytes
        case 19: decode_mrx_write_channel_cmd(p, n, w);     return true;
        case 21: return true;                                // VUSHF Channel Status — 0 bytes
        case 23: return true;                                // Read Tuning Details — 0 bytes
        case 25: return true;                                // Get CBIT Status — 0 bytes
        default: return false;
    }
}

bool mrx_g3_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  2: decode_mrx_board_count_rsp(p, n, w);       return true;
        case 18: decode_mrx_all_channels_rsp(p, n, w);      return true;
        case 20: return true;                                // Write Channel ACK
        case 22: decode_mrx_channel_init_status(p, n, w);   return true;
        case 24: decode_mrx_tuning_details_rsp(p, n, w);    return true;
        case 26: decode_mrx_cbit_status(p, n, w);           return true;
        default: return false;
    }
}
