// drs-bridge/parsers/dp_ecm/src/dp_ecm_hf_parser.cpp
//
// DP-ECM-1071 HF Receiver/Processor/Exciter parser DLL (Family-A binary).
// Implements the 4-symbol ABI (sdfc_abi.h) on top of the shared frame core.
//
// Increment 1 scope: extract_frame fully delegated to the core; parse_message
// decodes the frame envelope for ALL messages (group/unit/status/raw payload),
// with fully-worked decoders for two representative messages:
//   - SJC Group 100 / Unit 2  : Get System Version response (20 bytes)
//   - SJC Group 101 / Unit 40 : FH Detection response (4 + 40*count)
// All other units fall through to a safe raw_hex envelope (never crashes).
// format_response emits an ACK response (status echo, empty payload) from
// {group_id, unit_id, status}, plus optional raw payload via "payload_hex".
//
// Per the project rules this is an INCREMENTAL scaffold: the remaining per-unit
// decoders (Configure Detection, Burst, Wideband FFT, jamming, MRX, ...) are
// added one switch-case at a time against the DP-ECM-1071 understanding doc.
#include "sdfc_abi.h"
#include "sdfc_frame.h"
#include "sdfc_endian.h"
#include "json_writer.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <cstdio>

using namespace sdfc;

// ---- variant identity (the ONLY thing the VU sister DLL changes here) ----
static constexpr const char* HW_NAME = "dp_ecm_hf";

// =============================================================================
// Per-message decoders (payload -> JSON fields). Each gets the payload pointer,
// payload length, and the JsonWriter to append decoded fields onto.
// =============================================================================

// SJC 100/2 — Get System Version Details (20 bytes).
// Layout per DP-ECM-1071 understanding doc (representative; confirm packed offsets).
static void decode_system_version(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 20) { w.key_str("warning", "system_version payload < 20 bytes"); return; }
    // 5 x uint32 version words + identifiers (exact field map per ICD revision).
    w.key_uint("fw_version_raw",     load_u32le(p + 0));
    w.key_uint("driver_version_raw", load_u32le(p + 4));
    w.key_uint("fpga_version_raw",   load_u32le(p + 8));
    w.key_uint("bsp_version_raw",    load_u32le(p + 12));
    w.key_uint("processor_id",       load_u16le(p + 16));
    w.key_uint("fpga_type_id",       load_u16le(p + 18));
}

// SJC 101/40 — FH Detection response: HopperCount(u16) Reserved(u16) S_HOPPER_DATA[count]
// S_HOPPER_DATA = 40 bytes: number(u32), minFreqMHz(f32), maxFreqMHz(f32),
//   pulseLenMs(f32), interHopMs(f32), detected(u32), TOA(4xu8), powerDbm(f32),
//   active(u16), reserved(u16), snr(f32).
static void decode_fh_detection(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "fh payload < 4 bytes"); return; }
    uint16_t count = load_u16le(p + 0);
    w.key_uint("hopper_count", count);

    std::string arr = "[";
    int off = 4;
    const int ELEM = 40;
    uint16_t emitted = 0;
    for (uint16_t i = 0; i < count; ++i) {
        if (off + ELEM > n) break;                 // bound-check: never over-read
        const uint8_t* e = p + off;
        JsonWriter h;
        h.key_uint("hopper_number", load_u32le(e + 0));
        h.key_double("min_freq_hz", static_cast<double>(load_f32le(e + 4))  * 1e6);
        h.key_double("max_freq_hz", static_cast<double>(load_f32le(e + 8))  * 1e6);
        h.key_double("pulse_length_s",    static_cast<double>(load_f32le(e + 12)) / 1e3);
        h.key_double("inter_hop_period_s",static_cast<double>(load_f32le(e + 16)) / 1e3);
        h.key_uint("detected_count", load_u32le(e + 20));
        char toa[16];
        std::snprintf(toa, sizeof(toa), "%02u:%02u:%02u", e[24], e[25], e[26]);
        h.key_str("toa", toa);
        h.key_double("power_dbm", static_cast<double>(load_f32le(e + 28)));
        h.key_bool("active", load_u16le(e + 32) == 1);
        h.key_double("snr_db", static_cast<double>(load_f32le(e + 36)));
        if (emitted++) arr += ',';
        arr += h.str();
        off += ELEM;
    }
    arr += "]";
    w.key_raw("hoppers", arr);
}

// =============================================================================
// ABI: extract_frame
// =============================================================================
extern "C" SDFC_EXPORT int extract_frame(const uint8_t* buf, int buf_len,
                                         uint8_t* out_frame, int* out_len) {
    return extract_frame_core(buf, buf_len, out_frame, out_len);
}

// =============================================================================
// ABI: parse_message
// =============================================================================
extern "C" SDFC_EXPORT const char* parse_message(const uint8_t* frame, int frame_len,
                                                 int frame_type) {
    FrameHeader hdr;
    if (!decode_header(frame, frame_len, frame_type, hdr)) return nullptr;
    if (hdr.total_len > frame_len) return nullptr;   // truncated

    const uint8_t* payload = frame + hdr.payload_off;
    int plen = static_cast<int>(hdr.payload_size);
    if (hdr.payload_off + plen > frame_len) return nullptr;

    JsonWriter w;
    w.key_str("hw", HW_NAME);
    w.key_str("frame_type", frame_type == FRAME_COMMAND ? "command"
                          : frame_type == FRAME_STREAM  ? "stream"
                          : "response");
    w.key_uint("group_id", hdr.group_id);
    w.key_uint("unit_id",  hdr.unit_id);
    w.key_uint("message_size_bytes", hdr.payload_size);
    if (frame_type == FRAME_RESPONSE) {
        w.key_int("status", hdr.status);
        w.key_str("status_name", hdr.status == 0 ? "OnSuccess" : "Error");
    }

    // Dispatch on (group, unit). Only responses carry decodable payloads here.
    bool decoded = false;
    if (frame_type == FRAME_RESPONSE) {
        if (hdr.group_id == 100 && hdr.unit_id == 2) {
            decode_system_version(payload, plen, w); decoded = true;
        } else if (hdr.group_id == 101 && hdr.unit_id == 40) {
            decode_fh_detection(payload, plen, w); decoded = true;
        }
    }
    // Safe fallback: expose raw bytes so nothing is lost and the bridge never crashes.
    if (!decoded && plen > 0) {
        w.key_str("raw_hex", to_hex(payload, plen));
    }

    std::string json = w.str();
    char* result = static_cast<char*>(std::malloc(json.size() + 1));
    if (!result) return nullptr;
    std::memcpy(result, json.c_str(), json.size() + 1);
    return result;
}

// =============================================================================
// ABI: format_response
//   Minimal flat-int extractor (group_id, unit_id, status). Optional
//   "payload_hex":"aabb.." is decoded into the response payload. A full JSON
//   parser (nlohmann/json, MIT) replaces this extractor in a later step.
// =============================================================================
static bool json_find_int(const char* json, const char* key, long long& out) {
    std::string pat = std::string("\"") + key + "\"";
    const char* k = std::strstr(json, pat.c_str());
    if (!k) return false;
    const char* c = std::strchr(k, ':');
    if (!c) return false;
    ++c;
    while (*c == ' ' || *c == '\t') ++c;
    char* end = nullptr;
    long long v = std::strtoll(c, &end, 10);
    if (end == c) return false;
    out = v;
    return true;
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

extern "C" SDFC_EXPORT int format_response(const char* json_response, uint8_t* out_frame) {
    if (!json_response || !out_frame) return -1;
    long long group = 0, unit = 0, status = 0;
    if (!json_find_int(json_response, "group_id", group)) return -1;
    if (!json_find_int(json_response, "unit_id",  unit))  return -1;
    if (!json_find_int(json_response, "status",   status)) return -1;

    // Optional payload from "payload_hex".
    uint8_t payload[MAX_PAYLOAD];
    int plen = 0;
    const char* ph = std::strstr(json_response, "\"payload_hex\"");
    if (ph) {
        const char* q = std::strchr(ph, ':');
        if (q) { q = std::strchr(q, '"'); }            // opening quote of value
        if (q) {
            ++q;
            while (*q && *q != '"') {
                int hi = hex_nibble(*q++);
                if (*q == '"' || !*q) break;
                int lo = hex_nibble(*q++);
                if (hi < 0 || lo < 0) return -1;
                if (plen >= MAX_PAYLOAD) return -1;
                payload[plen++] = static_cast<uint8_t>((hi << 4) | lo);
            }
        }
    }

    int total = RESP_OVERHEAD + plen;
    if (total > MAX_FRAME_BUFFER_BYTES) return -1;

    std::memcpy(out_frame, RESP_HEADER, MAGIC_LEN);                 // [0..3]
    store_i16le(out_frame + RESP_OFF_STATUS, static_cast<int16_t>(status)); // [4..5]
    store_u32le(out_frame + RESP_OFF_SIZE,   static_cast<uint32_t>(plen));  // [6..9]
    store_u16le(out_frame + RESP_OFF_GROUP,  static_cast<uint16_t>(group)); // [10..11]
    store_u16le(out_frame + RESP_OFF_UNIT,   static_cast<uint16_t>(unit));  // [12..13]
    if (plen > 0) std::memcpy(out_frame + RESP_OFF_PAYLOAD, payload, static_cast<size_t>(plen));
    std::memcpy(out_frame + RESP_OFF_PAYLOAD + plen, RESP_FOOTER, MAGIC_LEN);
    return total;
}

// =============================================================================
// ABI: free_result
// =============================================================================
extern "C" SDFC_EXPORT void free_result(const char* result) {
    std::free(const_cast<char*>(result));
}
