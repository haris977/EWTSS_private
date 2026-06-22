// start_cpp/vushf_rpe/src/vushf_rpe_parser.cpp
//
// VHF/UHF/SHF Receiver/Processor/Exciter parser DLL  —  30 MHz to 6000 MHz
// Protocol: DP-ECM-1074-6000-V1-ICD-0V04
//
// Implements the 4-symbol SDFC ABI (sdfc_abi.h):
//   extract_frame   — scan TCP stream for a complete command or response packet
//   parse_message   — decode a packet into a UTF-8 JSON string
//   format_response — encode {group_id, unit_id, status [, payload_hex]} → binary frame
//   free_result     — release memory returned by parse_message
//
// Decoders implemented (all RESPONSE frames):
//   Group 100 / Unit  2  — Get System Version       (20 bytes)
//   Group 100 / Unit 10  — Get Temperature Status   (16 bytes)
//   Group 101 / Unit 40  — FH Detection report      (4 + 40 * hopper_count)
//   Group 101 / Unit 44  — Wideband FFT Data        (16 + 4 * point_count)
//   Group 101 / Unit 70  — FF Detection report      (4 + 32 * freq_count)
//   Group 101 / Unit 84  — Burst Detection report   (4 + 36 * burst_count)
//   Group 200 / Unit  2  — Immediate Jam ACK        (VU-specific)
//   Group 200 / Unit  4  — Follow-On Jam ACK        (VU-specific)
//   Group 200 / Unit  6  — Jam List ACK             (VU-specific)
//   Group 200 / Unit  8  — Responsive Sweep Jam ACK (VU-specific)
//
// Every other group/unit falls through to a safe raw_hex envelope.
#include "sdfc_abi.h"
#include "sdfc_frame.h"
#include "sdfc_endian.h"
#include "json_writer.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <cstdio>

using namespace sdfc;

static constexpr const char* HW_NAME = "vushf_rpe";  // 30 – 6000 MHz

// =============================================================================
// Per-message decoders
// =============================================================================

// ─── Group 100 / Unit 2  —  Get System Version Details ───────────────────────
static void decode_system_version(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 20) { w.key_str("warning", "system_version payload < 20 bytes"); return; }
    w.key_uint("fw_version_raw",     load_u32le(p +  0));
    w.key_uint("driver_version_raw", load_u32le(p +  4));
    w.key_uint("fpga_version_raw",   load_u32le(p +  8));
    w.key_uint("bsp_version_raw",    load_u32le(p + 12));
    w.key_uint("processor_id",       load_u16le(p + 16));
    w.key_uint("fpga_type_id",       load_u16le(p + 18));
}

// ─── Group 100 / Unit 10  —  Get Temperature Status ──────────────────────────
static void decode_temperature(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) { w.key_str("warning", "temperature payload < 16 bytes"); return; }
    w.key_double("internal_temp_c",  static_cast<double>(load_f32le(p +  0)));
    w.key_double("external_temp_c",  static_cast<double>(load_f32le(p +  4)));
    w.key_double("cpu_temp_c",       static_cast<double>(load_f32le(p +  8)));
    w.key_double("fpga_temp_c",      static_cast<double>(load_f32le(p + 12)));
}

// ─── Group 101 / Unit 40  —  FH Detection Report (30 – 6000 MHz) ─────────────
// Same S_HOPPER_DATA layout as HF. Band range differs; the hardware fills
// min/max freq with values in [30, 6000] MHz.
static void decode_fh_detection(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "fh_detection payload < 4 bytes"); return; }
    uint16_t count = load_u16le(p);
    w.key_uint("hopper_count", count);

    std::string arr = "[";
    const int ELEM = 40;
    uint16_t emitted = 0;
    int off = 4;
    for (uint16_t i = 0; i < count; ++i) {
        if (off + ELEM > n) break;
        const uint8_t* e = p + off;
        JsonWriter h;
        h.key_uint("hopper_number",        load_u32le(e +  0));
        h.key_double("min_freq_hz",   static_cast<double>(load_f32le(e +  4)) * 1e6);
        h.key_double("max_freq_hz",   static_cast<double>(load_f32le(e +  8)) * 1e6);
        h.key_double("pulse_length_s",static_cast<double>(load_f32le(e + 12)) / 1e3);
        h.key_double("inter_hop_s",   static_cast<double>(load_f32le(e + 16)) / 1e3);
        h.key_uint("detected_count",       load_u32le(e + 20));
        char toa[12];
        std::snprintf(toa, sizeof(toa), "%02u:%02u:%02u", e[24], e[25], e[26]);
        h.key_str("toa", toa);
        h.key_double("power_dbm",     static_cast<double>(load_f32le(e + 28)));
        h.key_bool("active",          load_u16le(e + 32) == 1);
        h.key_double("snr_db",        static_cast<double>(load_f32le(e + 36)));
        if (emitted++) arr += ',';
        arr += h.str();
        off += ELEM;
    }
    arr += "]";
    w.key_raw("hoppers", arr);
}

// ─── Group 101 / Unit 44  —  Wideband FFT Data ───────────────────────────────
static void decode_wideband_fft(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) { w.key_str("warning", "wideband_fft payload < 16 bytes"); return; }
    uint32_t start_mhz = load_u32le(p +  0);
    uint32_t stop_mhz  = load_u32le(p +  4);
    uint32_t step_khz  = load_u32le(p +  8);
    uint32_t count     = load_u32le(p + 12);
    w.key_double("start_freq_hz", static_cast<double>(start_mhz) * 1e6);
    w.key_double("stop_freq_hz",  static_cast<double>(stop_mhz)  * 1e6);
    w.key_double("step_hz",       static_cast<double>(step_khz)  * 1e3);
    w.key_uint("point_count", count);

    if (n < 16 + static_cast<int>(count) * 4) {
        w.key_str("warning", "wideband_fft payload truncated");
        count = static_cast<uint32_t>((n - 16) / 4);
    }
    const uint32_t EMIT_MAX = 2048;
    uint32_t emit = (count < EMIT_MAX) ? count : EMIT_MAX;
    std::string arr = "[";
    for (uint32_t i = 0; i < emit; ++i) {
        if (i) arr += ',';
        char tmp[32];
        std::snprintf(tmp, sizeof(tmp), "%.2f",
                      static_cast<double>(load_f32le(p + 16 + i * 4)));
        arr += tmp;
    }
    arr += "]";
    w.key_raw("power_dbm", arr);
    if (emit < count) w.key_uint("points_omitted", count - emit);
}

// ─── Group 101 / Unit 70  —  FF Detection Report ─────────────────────────────
static void decode_ff_detection(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "ff_detection payload < 4 bytes"); return; }
    uint16_t count = load_u16le(p);
    w.key_uint("freq_count", count);

    std::string arr = "[";
    const int ELEM = 32;
    uint16_t emitted = 0;
    int off = 4;
    for (uint16_t i = 0; i < count; ++i) {
        if (off + ELEM > n) break;
        const uint8_t* e = p + off;
        JsonWriter h;
        h.key_double("freq_hz",       static_cast<double>(load_f32le(e +  0)) * 1e6);
        h.key_double("power_dbm",     static_cast<double>(load_f32le(e +  4)));
        h.key_double("bandwidth_hz",  static_cast<double>(load_f32le(e +  8)) * 1e3);
        char toa[12];
        std::snprintf(toa, sizeof(toa), "%02u:%02u:%02u", e[12], e[13], e[14]);
        h.key_str("toa", toa);
        h.key_double("duration_s",    static_cast<double>(load_f32le(e + 16)) / 1e3);
        h.key_bool("active",          load_u16le(e + 20) == 1);
        h.key_double("snr_db",        static_cast<double>(load_f32le(e + 24)));
        if (emitted++) arr += ',';
        arr += h.str();
        off += ELEM;
    }
    arr += "]";
    w.key_raw("detections", arr);
}

// ─── Group 101 / Unit 84  —  Burst Detection Report ──────────────────────────
static void decode_burst_detection(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "burst_detection payload < 4 bytes"); return; }
    uint16_t count = load_u16le(p);
    w.key_uint("burst_count", count);

    std::string arr = "[";
    const int ELEM = 36;
    uint16_t emitted = 0;
    int off = 4;
    for (uint16_t i = 0; i < count; ++i) {
        if (off + ELEM > n) break;
        const uint8_t* e = p + off;
        JsonWriter h;
        h.key_double("center_freq_hz",  static_cast<double>(load_f32le(e +  0)) * 1e6);
        h.key_double("power_dbm",       static_cast<double>(load_f32le(e +  4)));
        h.key_double("bandwidth_hz",    static_cast<double>(load_f32le(e +  8)) * 1e3);
        char toa[12];
        std::snprintf(toa, sizeof(toa), "%02u:%02u:%02u", e[12], e[13], e[14]);
        h.key_str("toa", toa);
        h.key_double("duration_s",      static_cast<double>(load_f32le(e + 16)) / 1e3);
        h.key_double("rep_period_s",    static_cast<double>(load_f32le(e + 20)) / 1e3);
        h.key_uint("detected_bursts",   load_u32le(e + 24));
        h.key_bool("active",            load_u16le(e + 28) == 1);
        h.key_double("snr_db",          static_cast<double>(load_f32le(e + 32)));
        if (emitted++) arr += ',';
        arr += h.str();
        off += ELEM;
    }
    arr += "]";
    w.key_raw("bursts", arr);
}

// ─── Group 200  —  VU Jamming ACK responses (DP-ECM-1074 §2 Group 200) ───────
// All four jam ACK variants share the same 8-byte layout:
//   jam_id(u16) @0   reserved(u16) @2   active(u16) @4   reserved(u16) @6
// A zero-length payload is legal (status-only ACK).
static void decode_jam_ack(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    static const char* KINDS[] = {
        nullptr, nullptr,
        "immediate_jam",    // unit 2
        nullptr,
        "follow_on_jam",    // unit 4
        nullptr,
        "jam_list",         // unit 6
        nullptr,
        "responsive_sweep"  // unit 8
    };
    const char* kind = (unit_id < 9 && KINDS[unit_id]) ? KINDS[unit_id] : "unknown_jam";
    w.key_str("jam_kind", kind);
    if (n >= 8) {
        w.key_uint("jam_id",     load_u16le(p + 0));
        w.key_bool("jam_active", load_u16le(p + 4) == 1);
    } else if (n > 0) {
        w.key_str("warning", "jam ack payload < 8 bytes");
        w.key_str("raw_hex", to_hex(p, n));
    }
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
    if (hdr.total_len > frame_len) return nullptr;

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

    bool decoded = false;
    if (frame_type == FRAME_RESPONSE) {
        if      (hdr.group_id == 100 && hdr.unit_id ==  2) {
            decode_system_version(payload, plen, w); decoded = true;
        } else if (hdr.group_id == 100 && hdr.unit_id == 10) {
            decode_temperature(payload, plen, w); decoded = true;
        } else if (hdr.group_id == 101 && hdr.unit_id == 40) {
            decode_fh_detection(payload, plen, w); decoded = true;
        } else if (hdr.group_id == 101 && hdr.unit_id == 44) {
            decode_wideband_fft(payload, plen, w); decoded = true;
        } else if (hdr.group_id == 101 && hdr.unit_id == 70) {
            decode_ff_detection(payload, plen, w); decoded = true;
        } else if (hdr.group_id == 101 && hdr.unit_id == 84) {
            decode_burst_detection(payload, plen, w); decoded = true;
        } else if (hdr.group_id == 200 &&
                   (hdr.unit_id == 2 || hdr.unit_id == 4 ||
                    hdr.unit_id == 6 || hdr.unit_id == 8)) {
            decode_jam_ack(hdr.unit_id, payload, plen, w); decoded = true;
        }
        // TODO: 112/2 fast scan, 111/14 protected scan, 111/22 signal BITE
    }

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
// =============================================================================
static bool json_find_int(const char* json, const char* key, long long& out) {
    std::string pat = std::string("\"") + key + "\"";
    const char* k = std::strstr(json, pat.c_str());
    if (!k) return false;
    const char* c = std::strchr(k, ':');
    if (!c) return false;
    while (*++c == ' ' || *c == '\t') {}
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

    uint8_t payload[MAX_PAYLOAD];
    int plen = 0;
    const char* ph = std::strstr(json_response, "\"payload_hex\"");
    if (ph) {
        const char* q = std::strchr(ph, ':');
        if (q) q = std::strchr(q, '"');
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

    std::memcpy(out_frame, RESP_HEADER, MAGIC_LEN);
    store_i16le(out_frame + RESP_OFF_STATUS, static_cast<int16_t>(status));
    store_u32le(out_frame + RESP_OFF_SIZE,   static_cast<uint32_t>(plen));
    store_u16le(out_frame + RESP_OFF_GROUP,  static_cast<uint16_t>(group));
    store_u16le(out_frame + RESP_OFF_UNIT,   static_cast<uint16_t>(unit));
    if (plen > 0)
        std::memcpy(out_frame + RESP_OFF_PAYLOAD, payload, static_cast<size_t>(plen));
    std::memcpy(out_frame + RESP_OFF_PAYLOAD + plen, RESP_FOOTER, MAGIC_LEN);
    return total;
}

// =============================================================================
// ABI: free_result
// =============================================================================
extern "C" SDFC_EXPORT void free_result(const char* result) {
    std::free(const_cast<char*>(result));
}
