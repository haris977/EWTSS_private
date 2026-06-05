// parsers/cpp/rdfs/rdfs_parser.cpp
//
// RDFS (RF Direction Finding System) hardware variant parser.
// Implements the 4-symbol ABI defined in common/parser_api.h.
//
// Hardware: RDFS
// Protocol: SDFC/DRS binary over TCP, little-endian, CRC-16/CCITT
// TCP port: 5485 (from profiles/rdfs.yaml)
#include "rdfs_frame_types.h"
#include "../common/parser_api.h"
#include "../common/frame_buffer.h"
#include "../common/checksum.h"
#include "../common/json_writer.h"

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <algorithm>

using namespace rdfs;

// ─────────────────────────────────────────────────────────────────────────────
// INTERNAL HELPERS
// ─────────────────────────────────────────────────────────────────────────────

static char* to_hex_string(const uint8_t* data, int len, int max_bytes = 64) {
    int   n      = std::min(len, max_bytes);
    int   out_sz = n * 3 + 1;
    char* out    = new char[out_sz];
    char* p      = out;
    for (int i = 0; i < n; ++i)
        p += std::snprintf(p, 4, "%02X ", data[i]);
    if (n > 0) *(p - 1) = '\0';
    else        *p       = '\0';
    return out;
}

static bool validate_crc(const uint8_t* frame, int crc_offset) {
    uint16_t computed = crc16_ccitt(frame + 4, crc_offset - 4);
    uint16_t stored   = read_u16(frame + crc_offset);
    return computed == stored;
}

// ─────────────────────────────────────────────────────────────────────────────
// GROUP-LEVEL PARSERS
// ─────────────────────────────────────────────────────────────────────────────

static void parse_group_device_ctrl(const uint8_t* payload, int payload_len,
                                     uint16_t unit_id, JsonWriter& w) {
    switch (unit_id) {

        case RESP_INIT_DEVICE: {
            // ICD layout: device_type(u8) firmware_rev(u16) capability_flags(u32)
            // Total: 7 bytes
            if (payload_len < 7) { w.set("parse_error", "payload_too_short"); return; }
            w.set("device_type",      (int32_t)read_u8 (payload + 0));
            w.set("firmware_rev",     (int32_t)read_u16(payload + 1));
            w.set("capability_flags", (uint32_t)read_u32(payload + 3));
            break;
        }

        case RESP_SET_MODE: {
            // ICD layout: active_mode(u8)
            if (payload_len < 1) { w.set("parse_error", "payload_too_short"); return; }
            w.set("active_mode", (int32_t)read_u8(payload + 0));
            break;
        }

        case RESP_STATUS: {
            // ICD layout: mode(u8) temp_c_tenths(i16) uptime_sec(u32)
            //             error_flags(u16)
            // Total: 9 bytes
            if (payload_len < 9) { w.set("parse_error", "payload_too_short"); return; }
            uint8_t  mode        = read_u8 (payload + 0);
            int16_t  temp_raw    = read_i16(payload + 1);
            uint32_t uptime_sec  = read_u32(payload + 3);
            uint16_t error_flags = read_u16(payload + 7);

            w.set("mode",            (int32_t)mode);
            w.set("temperature_c",   temp_raw / 10.0);
            w.set("uptime_sec",      (uint32_t)uptime_sec);
            w.set("error_flags",     (int32_t)error_flags);
            break;
        }

        case RESP_SHUTDOWN: {
            // No payload — shutdown acknowledged
            break;
        }

        default: {
            char* hex = to_hex_string(payload, payload_len);
            w.set("raw_hex", hex);
            delete[] hex;
        }
    }
}

static void parse_group_rf_scan(const uint8_t* payload, int payload_len,
                                 uint16_t unit_id, JsonWriter& w) {
    switch (unit_id) {

        case RESP_SCAN_ACK: {
            // ICD layout: scan_id(u32) — identifier for this scan session
            if (payload_len < 4) { w.set("parse_error", "payload_too_short"); return; }
            w.set("scan_id", (uint32_t)read_u32(payload + 0));
            break;
        }

        case RESP_STOP_ACK: {
            // ICD layout: scan_id(u32) num_detections(u16)
            if (payload_len < 6) { w.set("parse_error", "payload_too_short"); return; }
            w.set("scan_id",        (uint32_t)read_u32(payload + 0));
            w.set("num_detections", (int32_t)read_u16(payload + 4));
            break;
        }

        case RESP_SET_SCAN_RANGE: {
            // ICD layout: start_freq_khz(u32) stop_freq_khz(u32) step_khz(u16)
            // Total: 10 bytes
            // Convert kHz -> Hz
            if (payload_len < 10) { w.set("parse_error", "payload_too_short"); return; }
            uint32_t start_khz = read_u32(payload + 0);
            uint32_t stop_khz  = read_u32(payload + 4);
            uint16_t step_khz  = read_u16(payload + 8);

            w.set("scan_start_frequency_hz", (int64_t)start_khz * 1000LL);
            w.set("scan_stop_frequency_hz",  (int64_t)stop_khz  * 1000LL);
            w.set("scan_step_hz",            (int32_t)step_khz  * 1000);
            break;
        }

        default: {
            char* hex = to_hex_string(payload, payload_len);
            w.set("raw_hex", hex);
            delete[] hex;
        }
    }
}

static void parse_group_signal_data(const uint8_t* payload, int payload_len,
                                     uint16_t unit_id, JsonWriter& w) {
    switch (unit_id) {

        case RESP_DETECTIONS: {
            // ICD layout: num_detections(u16) then for each detection:
            //   frequency_khz(u32) power_dbuv_tenths(i16) confidence(u8)
            //   azimuth_tenths(u16) elevation_tenths(i16) timestamp_ms(u32)
            // Per-detection size: 15 bytes
            // We emit the first detection's fields (most common use case).
            // Multi-detection payloads: Person F (drs-server) aggregates from Kafka.
            if (payload_len < 2) { w.set("parse_error", "payload_too_short"); return; }
            uint16_t num_det = read_u16(payload + 0);
            w.set("num_detections", (int32_t)num_det);

            if (num_det > 0 && payload_len >= 2 + 15) {
                const uint8_t* d      = payload + 2;
                uint32_t       f_khz  = read_u32(d + 0);
                int16_t        p_raw  = read_i16(d + 4);
                uint8_t        conf   = read_u8 (d + 6);
                uint16_t       az_raw = read_u16(d + 7);
                int16_t        el_raw = read_i16(d + 9);
                uint32_t       ts_ms  = read_u32(d + 11);

                w.set("frequency_hz",   (int64_t)f_khz * 1000LL);
                w.set("power_dbm",      (p_raw / 10.0) - 107.0);
                w.set("confidence_pct", (int32_t)conf);
                w.set("azimuth_deg",    az_raw / 10.0);
                w.set("elevation_deg",  el_raw / 10.0);
                w.set("timestamp_ms",   (uint32_t)ts_ms);
            }
            break;
        }

        case RESP_SPECTRUM: {
            // ICD layout: start_freq_khz(u32) step_khz(u16) num_bins(u16)
            //             followed by num_bins * i16 (power values in dBuV tenths)
            if (payload_len < 8) { w.set("parse_error", "payload_too_short"); return; }
            uint32_t start_khz = read_u32(payload + 0);
            uint16_t step_khz  = read_u16(payload + 4);
            uint16_t num_bins  = read_u16(payload + 6);

            w.set("spectrum_start_frequency_hz", (int64_t)start_khz * 1000LL);
            w.set("spectrum_step_hz",            (int32_t)step_khz  * 1000);
            w.set("spectrum_num_bins",            (int32_t)num_bins);
            // Raw bin data is too large for JSON — signal for downstream to
            // request binary blob separately if needed
            break;
        }

        default: {
            char* hex = to_hex_string(payload, payload_len);
            w.set("raw_hex", hex);
            delete[] hex;
        }
    }
}

static void parse_group_diagnostics(const uint8_t* payload, int payload_len,
                                     uint16_t unit_id, JsonWriter& w) {
    switch (unit_id) {

        case RESP_HEALTH: {
            // ICD layout: overall_health(u8) rf_chain_ok(u8) dsp_ok(u8)
            //             memory_ok(u8) temp_ok(u8) voltage_mv(u16)
            // Total: 7 bytes
            if (payload_len < 7) { w.set("parse_error", "payload_too_short"); return; }
            w.set("health_overall",  (int32_t)read_u8 (payload + 0));
            w.set("rf_chain_ok",     (bool)(read_u8(payload + 1) != 0));
            w.set("dsp_ok",          (bool)(read_u8(payload + 2) != 0));
            w.set("memory_ok",       (bool)(read_u8(payload + 3) != 0));
            w.set("temp_ok",         (bool)(read_u8(payload + 4) != 0));
            w.set("supply_voltage_v", read_u16(payload + 5) / 1000.0);
            break;
        }

        case RESP_SELF_TEST: {
            // ICD layout: test_result(u8) — 0=pass, non-zero=fail code
            //             fail_component(u8) — 0=none
            if (payload_len < 2) { w.set("parse_error", "payload_too_short"); return; }
            w.set("self_test_result",    (int32_t)read_u8(payload + 0));
            w.set("fail_component_code", (int32_t)read_u8(payload + 1));
            break;
        }

        default: {
            char* hex = to_hex_string(payload, payload_len);
            w.set("raw_hex", hex);
            delete[] hex;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// EXPORTED ABI FUNCTIONS
// ─────────────────────────────────────────────────────────────────────────────

extern "C" {

__declspec(dllexport)
int extract_frame(const uint8_t* buf, int buf_len,
                  uint8_t* out_frame, int* out_len) {

    if (!buf || buf_len < 2 || !out_frame || !out_len) return -1;

    if (buf_len >= 4 && match_magic(buf, CMD_HEADER, 4)) {
        if (buf_len < CMD_OVERHEAD) return 0;
        uint32_t payload_len = read_u32(buf + OFFSET_LENGTH);
        if (payload_len > static_cast<uint32_t>(MAX_PAYLOAD)) return -1;
        int total = CMD_OVERHEAD + static_cast<int>(payload_len);
        if (buf_len < total) return 0;
        if (!match_magic(buf + total - 4, CMD_FOOTER, 4)) return -1;
        if (!validate_crc(buf, total - 6)) return -1;
        std::memcpy(out_frame, buf, static_cast<std::size_t>(total));
        *out_len = total;
        return 1;
    }

    if (buf_len >= 4 && match_magic(buf, RESP_HEADER, 4)) {
        if (buf_len < RESP_OVERHEAD) return 0;
        uint32_t payload_len = read_u32(buf + OFFSET_LENGTH);
        if (payload_len > static_cast<uint32_t>(MAX_PAYLOAD)) return -1;
        int total = RESP_OVERHEAD + static_cast<int>(payload_len);
        if (buf_len < total) return 0;
        if (!match_magic(buf + total - 4, RESP_FOOTER, 4)) return -1;
        if (!validate_crc(buf, total - 6)) return -1;
        std::memcpy(out_frame, buf, static_cast<std::size_t>(total));
        *out_len = total;
        return 2;
    }

    if (buf_len >= 2 && match_magic(buf, SCD_HEADER, 2)) {
        if (buf_len < SCD_OVERHEAD) return 0;
        uint16_t payload_len = read_u16(buf + 2);
        if (payload_len > static_cast<uint16_t>(MAX_PAYLOAD)) return -1;
        int total = SCD_OVERHEAD + static_cast<int>(payload_len);
        if (buf_len < total) return 0;
        if (!match_magic(buf + total - 2, SCD_FOOTER, 2)) return -1;
        uint8_t computed = xor_checksum(buf + 2, total - 5);
        if (computed != buf[total - 3]) return -1;
        std::memcpy(out_frame, buf, static_cast<std::size_t>(total));
        *out_len = total;
        return 3;
    }

    return -1;
}


__declspec(dllexport)
const char* parse_message(const uint8_t* frame, int frame_len, int frame_type) {

    if (!frame || frame_len < 4) return nullptr;
    if (frame_type < 1 || frame_type > 3) return nullptr;

    JsonWriter w;

    switch (frame_type) {
        case 1: w.set("frame_type", "command");  break;
        case 2: w.set("frame_type", "response"); break;
        case 3: w.set("frame_type", "scd");      break;
    }

    if (frame_type == 1 || frame_type == 2) {
        if (frame_len < 12) return nullptr;
        uint16_t group_id = read_u16(frame + OFFSET_GROUP_ID);
        uint16_t unit_id  = read_u16(frame + OFFSET_UNIT_ID);
        w.set("group_id", (int32_t)group_id);
        w.set("unit_id",  (int32_t)unit_id);

        int payload_offset = CMD_HEADER_SIZE;
        if (frame_type == 2) {
            if (frame_len < 14) return nullptr;
            uint16_t status = read_u16(frame + OFFSET_STATUS);
            w.set("status", (int32_t)status);
            payload_offset = RESP_HEADER_SIZE;
        }

        int            payload_len = static_cast<int>(read_u32(frame + OFFSET_LENGTH));
        const uint8_t* payload     = frame + payload_offset;

        switch (group_id) {
            case GROUP_DEVICE_CTRL:
                parse_group_device_ctrl(payload, payload_len, unit_id, w);
                break;
            case GROUP_RF_SCAN:
                parse_group_rf_scan(payload, payload_len, unit_id, w);
                break;
            case GROUP_SIGNAL_DATA:
                parse_group_signal_data(payload, payload_len, unit_id, w);
                break;
            case GROUP_DIAGNOSTICS:
                parse_group_diagnostics(payload, payload_len, unit_id, w);
                break;
            default: {
                char* hex = to_hex_string(payload, payload_len);
                w.set("raw_hex", hex);
                delete[] hex;
            }
        }
    }
    else if (frame_type == 3) {
        if (frame_len < SCD_OVERHEAD) return nullptr;
        uint16_t       payload_len = read_u16(frame + 2);
        const uint8_t* payload     = frame + SCD_HEADER_SIZE;
        w.set("payload_len", (int32_t)payload_len);
        char* hex = to_hex_string(payload, payload_len);
        w.set("raw_hex", hex);
        delete[] hex;
    }

    if (!w.ok()) return nullptr;
    return w.finish();
}


__declspec(dllexport)
int format_response(const char* json_response, uint8_t* out_frame) {

    if (!json_response || !out_frame) return -1;

    int group_id = -1, unit_id = -1, status = -1;
    const char* p = json_response;
    while (*p) {
        if (std::sscanf(p, "\"group_id\":%d", &group_id) == 1) {}
        if (std::sscanf(p, "\"unit_id\":%d",  &unit_id)  == 1) {}
        if (std::sscanf(p, "\"status\":%d",   &status)   == 1) {}
        ++p;
    }
    if (group_id < 0 || unit_id < 0 || status < 0) return -1;

    uint8_t* out = out_frame;
    std::memcpy(out, RESP_HEADER, 4);                        out += 4;
    write_u32(out, 0);                                       out += 4;
    write_u16(out, static_cast<uint16_t>(group_id));         out += 2;
    write_u16(out, static_cast<uint16_t>(unit_id));          out += 2;
    write_u16(out, static_cast<uint16_t>(status));           out += 2;
    uint16_t crc = crc16_ccitt(out_frame + 4,
                                static_cast<int>(out - out_frame) - 4);
    write_u16(out, crc);                                     out += 2;
    std::memcpy(out, RESP_FOOTER, 4);                        out += 4;

    return static_cast<int>(out - out_frame);
}


__declspec(dllexport)
void free_result(const char* result) {
    delete[] result;
}

} // extern "C"
