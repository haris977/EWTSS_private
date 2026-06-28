// drs-bridge/parsers/dp_ecm/src/dp_ecm_hf_parser.cpp
//
// DP-ECM-1071 HF Receiver/Processor/Exciter parser DLL.
// Protocol doc: DP-ECM-1074-6000-V1-ICD-0V04.
// Implements the 4-symbol ABI (sdfc_abi.h) on top of the shared frame core.
//
// Full scope:
//   Group 100 (SJC diagnostics):
//     100/2  — Get System Version     (28 bytes, HF: 4 floats + processor_id + tuner_id[3] + fpga_type_id)
//     100/4  — SRx Checksum           (1000 bytes)
//     100/6  — PBIT Status            (88 bytes, HF-specific layout)
//     100/8  — IBIT Status            (68 bytes, HF-specific layout)
//     100/10 — Temperature Status     (36 bytes, 9 floats)
//     100/14 — Fan Speed Status       (4 bytes)
//     100/17 — UART Port Select cmd   (4 bytes)
//     100/18 — UART Test Status       (4 bytes)
//     100/26 — CBIT Status            (8 bytes)
//   Group 109 (Date/Time): 109/11 cmd, 109/12 rsp
//   Group 111 (Signal/Scan):
//     cmds 3(16B),5,9,13,15,17,19,25; rsps 4(12B),6,10,14,16(40B/entry),18,20,26,211
//   Group 112 (Fast Scan/Simulation): cmds 1,5,13,37; rsps 2,6,14,38
//   Group 101 (SJC detection + jamming):
//     detection cmds: 25,27,37,39,43,47,55,69,83,85,87,94,100,102,104,106,140,158,160-186,200,202,204,210
//     detection rsps: 26,28,38,40,44,48,56,70,84,86,88,95,101,103,105,107,141,159,161-187,201,203,205
//     jamming cmds:   31,33,63,73,75,79,92
//     jamming rsps:   32,34,64,74,76,80,93
//   Group 106 (HF ECM jamming — immediate jam):
//     cmds 1,3,5,9,21,39,41,45,49,55; rsps 2,4,6,10,22,40,42,46,50,54,56
//   All other units fall through to raw_hex envelope (never crashes).
#include "sdfc_abi.h"
#include "sdfc_frame.h"
#include "sdfc_endian.h"
#include "json_writer.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <cstdio>

#include "hf/hf_groups.h"
#include "hf/hf_json_utils.h"
#include "hf/hf_shared.h"

using namespace sdfc;

static constexpr const char* HW_NAME = "dp_ecm_hf";

// =============================================================================
// Group 111 decode functions moved to src/hf/parser/g111_parser.cpp
// =============================================================================

// =============================================================================
// Group 112 decode functions moved to src/hf/parser/g112_parser.cpp
// Group 112 encode functions moved to src/hf/format/g112_format.cpp
// =============================================================================

// =============================================================================
// Group 200 decode functions moved to src/hf/parser/g200_parser.cpp
// Group 200 encode functions moved to src/hf/format/g200_format.cpp
// =============================================================================

// =============================================================================
// MRx Group 3 decode functions moved to src/hf/parser/mrx_g3_parser.cpp
// MRx Group 3 encode functions moved to src/hf/format/mrx_g3_format.cpp
// =============================================================================

// =============================================================================
// MRx Group 4 decode functions moved to src/hf/parser/mrx_g4_parser.cpp
// MRx Group 4 encode functions moved to src/hf/format/mrx_g4_format.cpp
// =============================================================================

// =============================================================================
// MRx Group 1 decode functions moved to src/hf/parser/mrx_g1_parser.cpp
// =============================================================================

// =============================================================================
// MRx Group 5 — Tuner
// =============================================================================

// 5/14 — Read AGC/MGC Attenuation response (4 bytes).
static void decode_mrx_agc_status_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) return;
    w.key_uint("rf_attenuation_db", p[0]);
    w.key_uint("if_attenuation_db", p[1]);
    w.key_uint("agc_running",       load_u16le(p + 2));
    w.key_str("agc_status",         load_u16le(p + 2) ? "running" : "manual");
}

// 5/1 — Set Center Frequency command (12 bytes).
static void decode_mrx_set_center_freq_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 12) { w.key_str("warning", "mrx_set_center_freq < 12 bytes"); return; }
    w.key_uint("mrx_channel",         load_u16le(p + 0));
    w.key_uint("bite_antenna_sel",     load_u16le(p + 2));
    w.key_str("antenna_path",          load_u16le(p + 2) == 0 ? "antenna" : "bite");
    w.key_double("center_freq_hz",     load_f64le(p + 4) * 1e6);
}

// 5/3 — Attenuation Selection command (16 bytes, per ICD Table 256).
// @0 mrx_channel(uint16) @2 reserved(uint16) @4 rf_attenuation(float) @8 if_attenuation(float) @12 cal_value_debug(4B, not decoded).
static void decode_mrx_attenuation_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) { w.key_str("warning", "mrx_attenuation < 16 bytes"); return; }
    w.key_uint("mrx_channel",         load_u16le(p + 0));
    w.key_double("rf_attenuation_db", static_cast<double>(load_f32le(p + 4)));
    w.key_double("if_attenuation_db", static_cast<double>(load_f32le(p + 8)));
}

// =============================================================================
// MRx Group 6 — FH monitoring / GO2Monitor
// =============================================================================

// 6/7 — FH Monitoring Configuration command (52 bytes).
static void decode_mrx_fh_monitoring_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 52) { w.key_str("warning", "mrx_fh_monitoring < 52 bytes"); return; }
    static const char* RBW[] = {"6.25kHz","12.5kHz","25kHz","50kHz","100kHz","200kHz"};
    static const char* INT[] = {
        "10us","20us","40us","80us","160us","320us","640us","1280us","2560us","5120us","10240us","2048us"
    };
    uint8_t rbw_idx = p[2];
    uint8_t int_idx = p[3];
    w.key_uint("mrx_channel",           load_u16le(p +  0));
    w.key_uint("rbw_index",             rbw_idx);
    w.key_str("rbw_name",               rbw_idx < 6  ? RBW[rbw_idx] : "unknown");
    w.key_uint("integration_time_index",int_idx);
    w.key_str("integration_time_name",  int_idx < 12 ? INT[int_idx] : "unknown");
    w.key_double("fh_start_freq_hz",    load_f64le(p +  4) * 1e6);
    w.key_double("fh_stop_freq_hz",     load_f64le(p + 12) * 1e6);
    w.key_double("hop_period_ms",       static_cast<double>(load_f32le(p + 20)));
    w.key_double("inter_hop_period_ms", static_cast<double>(load_f32le(p + 24)));
    w.key_double("power_level_dbm",     static_cast<double>(load_f32le(p + 28)));
    w.key_double("band_start_freq_hz",  static_cast<double>(load_u32le(p + 40)) * 1e6);
    w.key_double("band_stop_freq_hz",   static_cast<double>(load_u32le(p + 44)) * 1e6);
    w.key_bool("fh_80mhz_stream",       p[48] != 0);
    w.key_bool("fh_enabled",            p[49] != 0);
    w.key_bool("header_enabled",        p[50] != 0);
}

// 6/9 — GO2Monitor Connection Establishment command (260 bytes).
// @0 IP Address (char[128])  @128 Port Number (char[128])  @256 mrx_channel (u16)
// 6/9 — GO2Monitor Connection Establishment command (260 bytes, per ICD Table 258).
// @0 ip_address(char[128]) @128 port_number(char[128]) @256 mrx_channel(uint16) @258 reserved(uint16).
static void decode_mrx_go2monitor_connect_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 260) { w.key_str("warning", "go2monitor_connect cmd < 260 bytes"); return; }
    const char* ip   = reinterpret_cast<const char*>(p +   0);
    const char* port = reinterpret_cast<const char*>(p + 128);
    w.key_str("ip_address",   std::string(ip,   strnlen(ip,   128)));
    w.key_str("port_number",  std::string(port, strnlen(port, 128)));
    w.key_uint("mrx_channel", load_u16le(p + 256));
}

// 6/13 — Start GO2Monitor Transmission command (144 bytes).
// @0 mrx_channel(u16) @2 bw(u16) @4 reserved(u16) @6 reserved(u16)
// @8 center_freq(double,8) @16 date(char[128])
static void decode_mrx_start_go2monitor_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 144) { w.key_str("warning", "start_go2monitor cmd < 144 bytes"); return; }
    static const char* BW[] = {"1MHz","240kHz","120kHz","60kHz","30kHz"};
    uint16_t bw = load_u16le(p + 2);
    w.key_uint("mrx_channel",       load_u16le(p +  0));
    w.key_uint("bw_selection",      bw);
    w.key_str("bandwidth_name",     bw < 5 ? BW[bw] : "unknown");
    w.key_double("center_freq_hz",  load_f64le(p + 8) * 1e6);
    const char* date = reinterpret_cast<const char*>(p + 16);
    w.key_str("date", std::string(date, strnlen(date, 128)));
}

// =============================================================================
// MRx Group 7 — Signal BITE / miscellaneous
// =============================================================================

// 7/1 — Signal BITE command (12 bytes).
static void decode_mrx_signal_bite_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 12) return;
    w.key_double("bite_freq_hz", load_f64le(p + 0) * 1e6);
    w.key_uint("mrx_channel",    load_u16le(p + 8));
}

// 7/2 — Signal BITE response (16 bytes).
static void decode_mrx_signal_bite_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) return;
    w.key_double("observed_freq_hz",   load_f64le(p + 0) * 1e6);
    w.key_double("observed_power_dbm", static_cast<double>(load_f32le(p + 8)));
    w.key_uint("result",               load_u16le(p + 12));
    w.key_str("result_name",           load_u16le(p + 12) == 1 ? "pass" : "fail");
}

// 7/17 — Spectrum Average Count Selection command (8 bytes, per ICD Table 285).
// @0 averaging_enabled(uint16) @2 avg_count(uint16) @4 mrx_channel(uint16) @6 reserved(uint16).
static void decode_mrx_spectrum_avg_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "spectrum_avg cmd < 8 bytes"); return; }
    w.key_bool("averaging_enabled", load_u16le(p + 0) == 1);
    w.key_uint("avg_count",         load_u16le(p + 2));
    w.key_uint("mrx_channel",       load_u16le(p + 4));
}

// 7/21 — Audio Squelch command (8 bytes).
static void decode_mrx_audio_squelch_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) return;
    w.key_uint("squelch_selection", load_u16le(p + 0));
    w.key_str("squelch_state",      load_u16le(p + 0) == 1 ? "on" : "off");
    w.key_uint("mrx_channel",       load_u16le(p + 2));
    w.key_double("threshold_dbm",   static_cast<double>(load_f32le(p + 4)));
}

// 7/23 — Set Date and Time command (8 bytes). Same layout as ECM Group 109/11.
static void decode_mrx_date_time_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) return;
    w.key_uint("day",     p[0]);
    w.key_uint("month",   p[1]);
    w.key_uint("year",    load_u16le(p + 2));
    w.key_uint("hour",    p[4]);
    w.key_uint("minute",  p[5]);
    w.key_uint("seconds", p[6]);
}

// Shared helper: decode 4-byte "selection + mrx_channel" command.
static void decode_mrx_sel_channel_cmd(const char* sel_key, const char* on_name, const char* off_name,
                                        const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) return;
    uint16_t sel = load_u16le(p + 0);
    w.key_uint(sel_key, sel);
    if (on_name && off_name) w.key_str("state", sel ? on_name : off_name);
    w.key_uint("mrx_channel", load_u16le(p + 2));
}

// =============================================================================
// ABI: extract_frame
// =============================================================================
extern "C" SDFC_EXPORT int extract_frame(const uint8_t* buf, size_t buf_len,
                                         uint8_t** out_frame, size_t* out_len) {
    if (!buf || !out_frame || !out_len) return -1;
    uint8_t* tmp = static_cast<uint8_t*>(std::malloc(MAX_FRAME_BUFFER_BYTES));
    if (!tmp) return -1;
    int tmp_len = 0;
    int result = extract_frame_core(buf, static_cast<int>(buf_len), tmp, &tmp_len);
    if (result <= 0) { std::free(tmp); return -1; }
    uint8_t* frame = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(tmp_len)));
    if (!frame) { std::free(tmp); return -1; }
    std::memcpy(frame, tmp, static_cast<size_t>(tmp_len));
    std::free(tmp);
    *out_frame = frame;
    *out_len = static_cast<size_t>(tmp_len);
    return 0;
}

// =============================================================================
// ABI: parse_message
// =============================================================================
extern "C" SDFC_EXPORT int parse_message(const uint8_t* frame, size_t frame_len,
                                         char** out_json, size_t* out_len) {
    if (!frame || !out_json || !out_len) return -1;
    int frame_type = FRAME_INCOMPLETE;
    if (frame_len >= static_cast<size_t>(MAGIC_LEN)) {
        if (std::memcmp(frame, CMD_HEADER,  MAGIC_LEN) == 0) frame_type = FRAME_COMMAND;
        else if (std::memcmp(frame, RESP_HEADER, MAGIC_LEN) == 0) frame_type = FRAME_RESPONSE;
    }
    if (frame_type == FRAME_INCOMPLETE) return -1;
    FrameHeader hdr;
    if (!decode_header(frame, static_cast<int>(frame_len), frame_type, hdr)) return -1;
    if (hdr.total_len > static_cast<int>(frame_len)) return -1;

    const uint8_t* payload = frame + hdr.payload_off;
    int plen = static_cast<int>(hdr.payload_size);
    if (hdr.payload_off + plen > static_cast<int>(frame_len)) return -1;

    JsonWriter w;
    w.key_str("hw", HW_NAME);
    if (frame_type == FRAME_COMMAND) w.key_str("frame_type", "command");
    w.key_uint("group_id", hdr.group_id);
    w.key_uint("unit_id",  hdr.unit_id);
    w.key_uint("message_size_bytes", hdr.payload_size);
    if (frame_type == FRAME_RESPONSE) {
        w.key_int("status", hdr.status);
        w.key_str("status_name", hdr.status == 0 ? "OnSuccess" : "Error");
    }

    bool decoded = false;

    // ------------------------------------------------------------------
    // FRAME_COMMAND dispatch
    // ------------------------------------------------------------------
    if (frame_type == FRAME_COMMAND) {
        if (hdr.group_id == 100) decoded = g100_parse_cmd(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id == 101) decoded = g101_parse_cmd(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id == 109) decoded = g109_parse_cmd(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id == 111) {
            decoded = g111_parse_cmd(hdr.unit_id, payload, plen, w);
        } else if (hdr.group_id == 112) {
            decoded = g112_parse_cmd(hdr.unit_id, payload, plen, w);
        } else if (hdr.group_id == 200) {
            decoded = g200_parse_cmd(hdr.unit_id, payload, plen, w);
        // ECM Group 106 — Immediate Jamming commands
        } else if (hdr.group_id == 106) {
            decoded = g106_parse_cmd(hdr.unit_id, payload, plen, w);
        // MRx Group 1 — diagnostics
        } else if (hdr.group_id == 1) {
            decoded = mrx_g1_parse_cmd(hdr.unit_id, payload, plen, w);
        // MRx Group 3 — RF board / channel management
        } else if (hdr.group_id == 3) {
            decoded = mrx_g3_parse_cmd(hdr.unit_id, payload, plen, w);
        // MRx Group 4 — data acquisition
        } else if (hdr.group_id == 4) {
            decoded = mrx_g4_parse_cmd(hdr.unit_id, payload, plen, w);
        // MRx Group 5 — tuner
        } else if (hdr.group_id == 5) {
            switch (hdr.unit_id) {
                case  1: decode_mrx_set_center_freq_cmd(payload, plen, w);          decoded = true; break;
                case  3: decode_mrx_attenuation_cmd(payload, plen, w);              decoded = true; break;
                case  9: decode_mrx_channel_cmd(payload, plen, w);                  decoded = true; break; // Clear Center Freq
                case 13: /* Read AGC/MGC — 0 bytes */                               decoded = true; break;
                default: break;
            }
        // MRx Group 6 — FH monitoring / GO2Monitor
        } else if (hdr.group_id == 6) {
            switch (hdr.unit_id) {
                case  7: decode_mrx_fh_monitoring_cmd(payload, plen, w);            decoded = true; break;
                case  9: decode_mrx_go2monitor_connect_cmd(payload, plen, w);       decoded = true; break;
                case 11: /* GO2Monitor Disconnect — 0 bytes */                      decoded = true; break;
                case 13: decode_mrx_start_go2monitor_cmd(payload, plen, w);         decoded = true; break;
                case 15: /* Stop GO2Monitor — 0 bytes */                            decoded = true; break;
                default: break;
            }
        // MRx Group 7 — Signal BITE / misc
        } else if (hdr.group_id == 7) {
            switch (hdr.unit_id) {
                case  1: decode_mrx_signal_bite_cmd(payload, plen, w);              decoded = true; break;
                case  3: decode_mrx_sel_channel_cmd("bite_antenna_sel","bite","antenna",payload,plen,w); decoded=true; break;
                case  5: decode_mrx_sel_channel_cmd("ref_source_sel","external","internal",payload,plen,w); decoded=true; break;
                case  9: decode_mrx_sel_channel_cmd("afc_sel","on","off",payload,plen,w); decoded=true; break;
                case 11: decode_mrx_sel_channel_cmd("rf_squelch_sel","on","off",payload,plen,w); decoded=true; break;
                case 13: decode_mrx_channel_cmd(payload, plen, w);                  decoded = true; break; // IQ socket open
                case 15: decode_mrx_channel_cmd(payload, plen, w);                  decoded = true; break; // IQ socket close
                case 17: decode_mrx_spectrum_avg_cmd(payload, plen, w);            decoded = true; break;
                case 19: decode_mrx_sel_channel_cmd("rf_agc_sel","enable","disable",payload,plen,w); decoded=true; break;
                case 21: decode_mrx_audio_squelch_cmd(payload, plen, w);            decoded = true; break;
                case 23: decode_mrx_date_time_cmd(payload, plen, w);                decoded = true; break;
                default: break;
            }
        }
    // ------------------------------------------------------------------
    // FRAME_RESPONSE dispatch
    // ------------------------------------------------------------------
    } 
    else if (frame_type == FRAME_RESPONSE) {
        if (hdr.group_id == 100) decoded = g100_parse_rsp(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id == 101) decoded = g101_parse_rsp(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id == 109) decoded = g109_parse_rsp(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id == 111) {
            // unit 211 is a cross-group stream rsp to 101/210 — no struct decoder, falls through to raw_hex
            if (hdr.unit_id != 211) decoded = g111_parse_rsp(hdr.unit_id, payload, plen, w);
        } else if (hdr.group_id == 112) {
            decoded = g112_parse_rsp(hdr.unit_id, payload, plen, w);
        } else if (hdr.group_id == 200) {
            decoded = g200_parse_rsp(hdr.unit_id, payload, plen, w);
        } else if (hdr.group_id == 106) {
            decoded = g106_parse_rsp(hdr.unit_id, payload, plen, w);
        // MRx Group 1 responses
        } else if (hdr.group_id == 1) {
            decoded = mrx_g1_parse_rsp(hdr.unit_id, payload, plen, w);
        // MRx Group 3 responses
        } else if (hdr.group_id == 3) {
            decoded = mrx_g3_parse_rsp(hdr.unit_id, payload, plen, w);
        // MRx Group 4 responses
        } else if (hdr.group_id == 4) {
            decoded = mrx_g4_parse_rsp(hdr.unit_id, payload, plen, w);
        // MRx Group 5 responses
        } else if (hdr.group_id == 5) {
            switch (hdr.unit_id) {
                case  2: /* Set Center Freq ACK */                           decoded = true; break;
                case  4: /* Attenuation Select ACK */                        decoded = true; break;
                case 10: /* Clear Center Freq ACK */                         decoded = true; break;
                case 14: decode_mrx_agc_status_rsp(payload, plen, w);       decoded = true; break;
                default: break;
            }
        // MRx Group 6 responses
        } else if (hdr.group_id == 6) {
            switch (hdr.unit_id) {
                case  8: /* FH Monitoring Config ACK */                      decoded = true; break;
                case 10: /* GO2Monitor Connect ACK */                        decoded = true; break;
                case 12: /* GO2Monitor Disconnect ACK */                     decoded = true; break;
                case 14: /* Start GO2Monitor Transmission ACK */              decoded = true; break;
                case 16: /* Stop GO2Monitor ACK */                           decoded = true; break;
                default: break;
            }
        // MRx Group 7 responses
        } else if (hdr.group_id == 7) {
            switch (hdr.unit_id) {
                case  2: decode_mrx_signal_bite_rsp(payload, plen, w); decoded = true; break;
                case  4: /* BITE/Antenna Select ACK */                  decoded = true; break;
                case  6: /* Ref Source Select ACK */                    decoded = true; break;
                case 10: /* AFC ACK */                                  decoded = true; break;
                case 12: /* RF Squelch ACK */                           decoded = true; break;
                case 14: /* IQ Socket Open ACK */                       decoded = true; break;
                case 16: /* IQ Socket Close ACK */                      decoded = true; break;
                case 18: /* Spectrum Avg Count ACK */                   decoded = true; break;
                case 20: /* Smart RF AGC ACK */                         decoded = true; break;
                case 22: /* Audio Squelch ACK */                        decoded = true; break;
                case 24: /* Set Date/Time ACK */                        decoded = true; break;
                default: break;
            }
        }
    }

    // Safe fallback: expose raw bytes so nothing is lost and the bridge never crashes.
    if (!decoded && plen > 0) {
        w.key_str("raw_hex", to_hex(payload, plen));
    }

    std::string json = w.str();
    char* result = static_cast<char*>(std::malloc(json.size() + 1));
    if (!result) return -1;
    std::memcpy(result, json.c_str(), json.size() + 1);
    *out_json = result;
    *out_len = json.size();
    return 0;
}

// =============================================================================
// ABI: format_response
// =============================================================================
// Group 111 encoders moved to src/hf/format/g111_format.cpp
// encode_channelization static helper also removed (lives in g109_format.cpp and g111_format.cpp)

// ---------------------------------------------------------------------------
// Group 112 encoders moved to src/hf/format/g112_format.cpp
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Group 200 encoders moved to src/hf/format/g200_format.cpp
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Group 106 encoders moved to src/hf/format/g106_format.cpp
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// MRx Group 1 encoders moved to src/hf/format/mrx_g1_format.cpp
// MRx Group 3 encoders moved to src/hf/format/mrx_g3_format.cpp
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// MRx Group 4 encoders moved to src/hf/format/mrx_g4_format.cpp
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// MRx Group 5 encoders
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// MRx Group 7 encoders
// ---------------------------------------------------------------------------
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

extern "C" SDFC_EXPORT int format_response(const char* kind, const char* kwargs_json,
                                           uint8_t** out_buf, size_t* out_len) {
    if (!kwargs_json || !out_buf || !out_len) return -1;
    long long group = 0, unit = 0, status = 0;
    if (!json_find_int(kwargs_json, "group_id", group)) return -1;
    if (!json_find_int(kwargs_json, "unit_id",  unit))  return -1;
    if (!json_find_int(kwargs_json, "status",   status)) return -1;

    uint8_t* payload = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(MAX_PAYLOAD)));
    if (!payload) return -1;
    int plen = 0;
    // is_ack: this unit_id is a known zero-payload ACK — skip payload_hex fallback.
    bool is_ack = false;

    typedef int (*EncFn)(const char*, uint8_t*, int);
    EncFn fn = nullptr;

    if (group == 100) {
        plen = g100_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack);
        if (plen < 0) { std::free(payload); return -1; }
    }
    else if (group == 101) {
        plen = g101_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack);
        if (plen < 0) { std::free(payload); return -1; }
    } else if (group == 109) {
        plen = g109_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack);
        if (plen < 0) { std::free(payload); return -1; }
    } else if (group == 111) {
        plen = g111_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack);
        if (plen < 0) { std::free(payload); return -1; }
    } else if (group == 112) {
        plen = g112_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack);
        if (plen < 0) { std::free(payload); return -1; }
    } else if (group == 200) {
        plen = g200_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack);
        if (plen < 0) { std::free(payload); return -1; }
    } else if (group == 106) {
        plen = g106_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack);
        if (plen < 0) { std::free(payload); return -1; }
    } else if (group == 1) {   // MRx diagnostics
        plen = mrx_g1_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack);
        if (plen < 0) { std::free(payload); return -1; }
    } else if (group == 3) {   // MRx RF board
        plen = mrx_g3_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack);
        if (plen < 0) { std::free(payload); return -1; }
    } else if (group == 4) {   // MRx data acquisition
        plen = mrx_g4_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack);
        if (plen < 0) { std::free(payload); return -1; }
    } else if (group == 5) {   // MRx tuner
        switch (unit) {
            case  2: is_ack = true;                  break; // Set Center Freq ACK
            case  4: is_ack = true;                  break; // Attenuation Select ACK
            case 10: is_ack = true;                  break; // Clear Center Freq ACK
            case 14: fn = encode_mrx_agc_status_rsp; break;
            default: break;
        }
    } else if (group == 6) {   // MRx FH monitoring / GO2Monitor
        switch (unit) {
            case  8: is_ack = true; break; // FH Monitoring Config ACK
            case 10: is_ack = true; break; // GO2Monitor Connect ACK
            case 12: is_ack = true; break; // GO2Monitor Disconnect ACK
            case 14: is_ack = true; break; // Start GO2Monitor ACK
            case 16: is_ack = true; break; // Stop GO2Monitor ACK
            default: break;
        }
    } else if (group == 7) {   // MRx Signal BITE / misc
        switch (unit) {
            case  2: fn = encode_mrx_signal_bite_rsp; break;
            case  4: is_ack = true; break; // BITE/Antenna Select ACK
            case  6: is_ack = true; break; // Ref Source Select ACK
            case 10: is_ack = true; break; // AFC ACK
            case 12: is_ack = true; break; // RF Squelch ACK
            case 14: is_ack = true; break; // IQ Socket Open ACK
            case 16: is_ack = true; break; // IQ Socket Close ACK
            case 18: is_ack = true; break; // Spectrum Avg Count ACK
            case 20: is_ack = true; break; // Smart RF AGC ACK
            case 22: is_ack = true; break; // Audio Squelch ACK
            case 24: is_ack = true; break; // Set Date/Time ACK
            default: break;
        }
    }

    if (fn) {
        plen = fn(kwargs_json, payload, MAX_PAYLOAD);
        if (plen < 0) { std::free(payload); return -1; }
    }

    // Only fall through to payload_hex for truly unknown group/unit combinations
    // (e.g. raw stream responses). Known ACK responses skip this path.
    if (!is_ack && plen == 0) {
        const char* ph = std::strstr(kwargs_json, "\"payload_hex\"");
        if (ph) {
            const char* q = std::strchr(ph, ':');
            if (q) { q = std::strchr(q, '"'); }
            if (q) {
                ++q;
                while (*q && *q != '"') {
                    int hi = hex_nibble(*q++);
                    if (*q == '"' || !*q) break;
                    int lo = hex_nibble(*q++);
                    if (hi < 0 || lo < 0) { std::free(payload); return -1; }
                    if (plen >= MAX_PAYLOAD) { std::free(payload); return -1; }
                    payload[plen++] = static_cast<uint8_t>((hi << 4) | lo);
                }
            }
        }
    }

    int total = RESP_OVERHEAD + plen;
    uint8_t* buf = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(total)));
    if (!buf) { std::free(payload); return -1; }

    std::memcpy(buf, RESP_HEADER, MAGIC_LEN);
    store_i16le(buf + RESP_OFF_STATUS, static_cast<int16_t>(status));
    store_u32le(buf + RESP_OFF_SIZE,   static_cast<uint32_t>(plen));
    store_u16le(buf + RESP_OFF_GROUP,  static_cast<uint16_t>(group));
    store_u16le(buf + RESP_OFF_UNIT,   static_cast<uint16_t>(unit));
    if (plen > 0) std::memcpy(buf + RESP_OFF_PAYLOAD, payload, static_cast<size_t>(plen));
    std::memcpy(buf + RESP_OFF_PAYLOAD + plen, RESP_FOOTER, MAGIC_LEN);
    std::free(payload);
    *out_buf = buf;
    *out_len = static_cast<size_t>(total);
    return 0;
}

// =============================================================================
// ABI: free_result
// =============================================================================
extern "C" SDFC_EXPORT void free_result(void* ptr) {
    std::free(ptr);
}
