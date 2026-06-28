// drs-bridge/parsers/dp_ecm/src/hf/parser/g200_parser.cpp
// Group 200 — HF ECM Jamming: parse-side decode functions and dispatchers.
#include "sdfc_endian.h"
#include "json_writer.h"
#include "hf_groups.h"
#include "hf_shared.h"
using namespace sdfc;

// =============================================================================
// Group 200 decode functions (unique to this group)
// =============================================================================

// 200/41 — HPASU and PA Health Status command (4 bytes).
// @0 Selection (uint8): 0=HPASU, 1=PA-1, 2=PA-2, 3=PA-3, 4=PA-4. @1-3 Reserved.
static void decode_hpasu_health_status_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "hpasu_health_cmd < 4 bytes"); return; }
    static const char* SEL[] = {"hpasu", "pa_1", "pa_2", "pa_3", "pa_4"};
    uint8_t sel = p[0];
    w.key_uint("selection", sel);
    w.key_str("selection_name", sel < 5 ? SEL[sel] : "unknown");
}

// 200/42 — HPASU and PA Health Status response (4 bytes).
static void decode_hpasu_health_status_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "hpasu_health_rsp < 4 bytes"); return; }
    w.key_uint("health_status", p[0]);
    w.key_str("health_status_name", p[0] == 0 ? "healthy" : "fault");
}

// 200/55 — PA and SDU Health Status response (8 bytes).
// PA fault: 0=no_fault, 1=overdrive, 3=vswr, 4=bpm. SDU fault: 0=no_fault, 1=fault.
static void decode_pa_sdu_health_status_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "pa_sdu_health_rsp < 8 bytes"); return; }
    static const char* PA_FAULT[] = {"no_fault", "overdrive", "unknown", "vswr", "bpm"};
    for (int i = 0; i < 4; ++i) {
        char key[16];
        std::snprintf(key, sizeof(key), "pa%d_health", i + 1);
        w.key_uint(key, p[i]);
        char key2[32];
        std::snprintf(key2, sizeof(key2), "pa%d_health_name", i + 1);
        w.key_str(key2, p[i] < 5 ? PA_FAULT[p[i]] : "unknown");
    }
    w.key_uint("sdu_health", p[4]);
    w.key_str("sdu_health_name", p[4] == 0 ? "no_fault" : "fault");
}

// 200/56 — PA Soft Reboot command (4 bytes).
// @0 PA Selection (uint8): 0=PA-1, 1=PA-2, 2=PA-3, 3=PA-4. @1-3 Reserved.
static void decode_cmd_pa_soft_reboot(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "pa_soft_reboot_cmd < 4 bytes"); return; }
    w.key_uint("pa_selection", p[0]);
    char name[8];
    std::snprintf(name, sizeof(name), "pa_%u", p[0] + 1);
    w.key_str("pa_name", name);
}

// =============================================================================
// Group 200 dispatchers
// =============================================================================

bool g200_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  9: decode_cmd_send_ecm_reports(p, n, w);              return true;
        case 11: decode_cmd_start_list_jam(p, n, w);                return true;
        case 13: return true;                                        // Stop List Jam — 0 bytes
        case 15: return true;                                        // Get List Jam Report — 0 bytes
        case 17: decode_cmd_start_follow_on_jam(p, n, w);           return true;
        case 19: return true;                                        // Stop Follow-on Jam — 0 bytes
        case 21: decode_cmd_start_responsive_sweep_jam(p, n, w);    return true;
        case 23: return true;                                        // Stop Responsive Sweep Jam — 0 bytes
        case 41: decode_hpasu_health_status_cmd(p, n, w);           return true;
        case 54: return true;                                        // PA/SDU Health Status — 0 bytes
        case 56: decode_cmd_pa_soft_reboot(p, n, w);                return true;
        default: return false;
    }
}

bool g200_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case 10: return true;                                        // Send ECM Reports ACK
        case 12: return true;                                        // Start List Jam ACK
        case 14: return true;                                        // Stop List Jam ACK
        case 16: return true;                                        // List Jam Report (decoded via format path)
        case 18: return true;                                        // Start Follow-on Jam ACK
        case 20: return true;                                        // Stop Follow-on Jam ACK
        case 22: return true;                                        // Start Responsive Sweep Jam ACK
        case 24: return true;                                        // Stop Responsive Sweep Jam ACK
        case 42: decode_hpasu_health_status_rsp(p, n, w);           return true;
        case 55: decode_pa_sdu_health_status_rsp(p, n, w);          return true;
        case 57: return true;                                        // PA Soft Reboot ACK
        default: return false;
    }
}
