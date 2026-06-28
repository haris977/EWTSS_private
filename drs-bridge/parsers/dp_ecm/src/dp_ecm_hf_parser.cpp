// drs-bridge/parsers/dp_ecm/src/dp_ecm_hf_parser.cpp
//
// DP-ECM-1071 HF Receiver/Processor/Exciter parser DLL.
// Protocol doc: DP-ECM-1074-6000-V1-ICD-0V04.
// Implements the 4-symbol ABI (sdfc_abi.h) on top of the shared frame core.
// Group detail: see src/hf/parser/ and src/hf/format/ per-group files.
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

    if (frame_type == FRAME_COMMAND) {
        if      (hdr.group_id == 100) decoded = g100_parse_cmd(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id == 101) decoded = g101_parse_cmd(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id == 106) decoded = g106_parse_cmd(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id == 109) decoded = g109_parse_cmd(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id == 111) decoded = g111_parse_cmd(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id == 112) decoded = g112_parse_cmd(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id == 200) decoded = g200_parse_cmd(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id ==   1) decoded = mrx_g1_parse_cmd(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id ==   3) decoded = mrx_g3_parse_cmd(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id ==   4) decoded = mrx_g4_parse_cmd(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id ==   5) decoded = mrx_g5_parse_cmd(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id ==   6) decoded = mrx_g6_parse_cmd(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id ==   7) decoded = mrx_g7_parse_cmd(hdr.unit_id, payload, plen, w);
    } else if (frame_type == FRAME_RESPONSE) {
        if      (hdr.group_id == 100) decoded = g100_parse_rsp(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id == 101) decoded = g101_parse_rsp(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id == 106) decoded = g106_parse_rsp(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id == 109) decoded = g109_parse_rsp(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id == 111) {
            // unit 211 is a cross-group stream rsp to 101/210 — no struct decoder, falls through to raw_hex
            if (hdr.unit_id != 211) decoded = g111_parse_rsp(hdr.unit_id, payload, plen, w);
        }
        else if (hdr.group_id == 112) decoded = g112_parse_rsp(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id == 200) decoded = g200_parse_rsp(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id ==   1) decoded = mrx_g1_parse_rsp(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id ==   3) decoded = mrx_g3_parse_rsp(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id ==   4) decoded = mrx_g4_parse_rsp(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id ==   5) decoded = mrx_g5_parse_rsp(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id ==   6) decoded = mrx_g6_parse_rsp(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id ==   7) decoded = mrx_g7_parse_rsp(hdr.unit_id, payload, plen, w);
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

    auto check = [&](int r) -> bool { if (r < 0) { std::free(payload); return false; } plen = r; return true; };
    if      (group == 100) { if (!check(g100_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack))) return -1; }
    else if (group == 101) { if (!check(g101_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack))) return -1; }
    else if (group == 106) { if (!check(g106_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack))) return -1; }
    else if (group == 109) { if (!check(g109_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack))) return -1; }
    else if (group == 111) { if (!check(g111_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack))) return -1; }
    else if (group == 112) { if (!check(g112_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack))) return -1; }
    else if (group == 200) { if (!check(g200_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack))) return -1; }
    else if (group ==   1) { if (!check(mrx_g1_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack))) return -1; }
    else if (group ==   3) { if (!check(mrx_g3_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack))) return -1; }
    else if (group ==   4) { if (!check(mrx_g4_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack))) return -1; }
    else if (group ==   5) { if (!check(mrx_g5_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack))) return -1; }
    else if (group ==   6) { if (!check(mrx_g6_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack))) return -1; }
    else if (group ==   7) { if (!check(mrx_g7_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack))) return -1; }

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
