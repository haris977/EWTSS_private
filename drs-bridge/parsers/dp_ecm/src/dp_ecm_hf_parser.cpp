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
// MRx Group 5 decode functions moved to src/hf/parser/mrx_g5_parser.cpp
// MRx Group 5 encode functions moved to src/hf/format/mrx_g5_format.cpp
// =============================================================================

// =============================================================================
// MRx Group 6 decode functions moved to src/hf/parser/mrx_g6_parser.cpp
// MRx Group 6 encode functions moved to src/hf/format/mrx_g6_format.cpp
// =============================================================================

// =============================================================================
// MRx Group 7 decode functions moved to src/hf/parser/mrx_g7_parser.cpp
// MRx Group 7 encode functions moved to src/hf/format/mrx_g7_format.cpp
// decode_mrx_sel_channel_cmd promoted to inline in hf_shared.h
// =============================================================================

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
            decoded = mrx_g5_parse_cmd(hdr.unit_id, payload, plen, w);
        // MRx Group 6 — FH monitoring / GO2Monitor
        } else if (hdr.group_id == 6) {
            decoded = mrx_g6_parse_cmd(hdr.unit_id, payload, plen, w);
        // MRx Group 7 — Signal BITE / misc
        } else if (hdr.group_id == 7) {
            decoded = mrx_g7_parse_cmd(hdr.unit_id, payload, plen, w);
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
            decoded = mrx_g5_parse_rsp(hdr.unit_id, payload, plen, w);
        // MRx Group 6 responses
        } else if (hdr.group_id == 6) {
            decoded = mrx_g6_parse_rsp(hdr.unit_id, payload, plen, w);
        // MRx Group 7 responses
        } else if (hdr.group_id == 7) {
            decoded = mrx_g7_parse_rsp(hdr.unit_id, payload, plen, w);
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
// MRx Group 5 encoders moved to src/hf/format/mrx_g5_format.cpp
// MRx Group 7 encoders moved to src/hf/format/mrx_g7_format.cpp
// ---------------------------------------------------------------------------

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
        plen = mrx_g5_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack);
        if (plen < 0) { std::free(payload); return -1; }
    } else if (group == 6) {   // MRx FH monitoring / GO2Monitor
        plen = mrx_g6_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack);
        if (plen < 0) { std::free(payload); return -1; }
    } else if (group == 7) {   // MRx Signal BITE / misc
        plen = mrx_g7_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack);
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
