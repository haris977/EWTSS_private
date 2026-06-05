// parsers/cpp/comm_df/comm_df_parser.cpp
//
// COMM_DF hardware variant parser — reference implementation.
// Implements the 4-symbol ABI defined in common/parser_api.h.
//
// Hardware: COMM_DF (Communications Direction Finder)
// Protocol: SDFC/DRS binary over TCP, little-endian, CRC-16/CCITT
#include "comm_df_frame_types.h"
#include "../common/parser_api.h"
#include "../common/frame_buffer.h"
#include "../common/checksum.h"
#include "../common/json_writer.h"

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <algorithm>

using namespace comm_df;

// ─────────────────────────────────────────────────────────────────────────────
// INTERNAL HELPERS
// ─────────────────────────────────────────────────────────────────────────────

// Converts up to max_bytes of data into a "AA BB CC ..." hex string.
// Returns a new char[] — caller must delete[].
static char* to_hex_string(const uint8_t* data, int len, int max_bytes = 64) {
    int  n      = std::min(len, max_bytes);
    int  out_sz = n * 3 + 1;
    char* out   = new char[out_sz];
    char* p     = out;
    for (int i = 0; i < n; ++i)
        p += std::snprintf(p, 4, "%02X ", data[i]);
    if (n > 0) *(p - 1) = '\0'; // trim trailing space
    else        *p       = '\0';
    return out;
}

// Validates CRC-16/CCITT over bytes [4 .. crc_offset).
// CRC covers everything after the 4-byte magic header up to (not including)
// the 2-byte CRC field itself.
static bool validate_crc(const uint8_t* frame, int crc_offset) {
    uint16_t computed = crc16_ccitt(frame + 4, crc_offset - 4);
    uint16_t stored   = read_u16(frame + crc_offset);
    return computed == stored;
}

// ─────────────────────────────────────────────────────────────────────────────
// GROUP-LEVEL PARSERS
// Each function decodes the payload for one group_id + unit_id combination
// and fills the JsonWriter with SI-unit fields.
// ─────────────────────────────────────────────────────────────────────────────

static void parse_group_system_mgmt(const uint8_t* payload, int payload_len,
                                     uint16_t unit_id, JsonWriter& w) {
    switch (unit_id) {

        case RESP_SYSTEM_VERSION: {
            // ICD layout: fw_major(u16) fw_minor(u16) fw_patch(u16)
            //             hw_revision(u8) serial_number(u32)
            // Total: 11 bytes
            if (payload_len < 11) { w.set("parse_error", "payload_too_short"); return; }
            w.set("fw_major",      (int32_t)read_u16(payload + 0));
            w.set("fw_minor",      (int32_t)read_u16(payload + 2));
            w.set("fw_patch",      (int32_t)read_u16(payload + 4));
            w.set("hw_revision",   (int32_t)read_u8 (payload + 6));
            w.set("serial_number", (uint32_t)read_u32(payload + 7));
            break;
        }

        case RESP_SET_DEVICE_ID: {
            // ICD layout: result_code(u8) — 0=ok, 1=id_conflict, 2=invalid_range
            if (payload_len < 1) { w.set("parse_error", "payload_too_short"); return; }
            w.set("device_id_result", (int32_t)read_u8(payload + 0));
            break;
        }

        case RESP_RESET: {
            // No payload fields — status already written from frame header
            break;
        }

        default: {
            char* hex = to_hex_string(payload, payload_len);
            w.set("raw_hex", hex);
            delete[] hex;
        }
    }
}

static void parse_group_rf_control(const uint8_t* payload, int payload_len,
                                    uint16_t unit_id, JsonWriter& w) {
    switch (unit_id) {

        case RESP_SET_FREQUENCY: {
            // ICD layout: center_freq_khz(u32) bandwidth_khz(u16)
            // Convert: kHz -> Hz
            if (payload_len < 6) { w.set("parse_error", "payload_too_short"); return; }
            uint32_t freq_khz      = read_u32(payload + 0);
            uint16_t bandwidth_khz = read_u16(payload + 4);
            w.set("center_frequency_hz", (int64_t)freq_khz * 1000LL);
            w.set("bandwidth_hz",        (int32_t)bandwidth_khz * 1000);
            break;
        }

        case RESP_SET_THRESHOLD: {
            // ICD layout: threshold_dbuv_tenths(i16) — signed, tenths of dBuV/m
            // Convert: dBuV/m -> dBm using 50-ohm assumption: dBm = dBuV - 107
            if (payload_len < 2) { w.set("parse_error", "payload_too_short"); return; }
            int16_t raw  = read_i16(payload + 0);
            double  dbuv = raw / 10.0;
            double  dbm  = dbuv - 107.0;
            w.set("threshold_dbm", dbm);
            break;
        }

        case RESP_SET_BANDWIDTH: {
            // ICD layout: bandwidth_khz(u16)
            if (payload_len < 2) { w.set("parse_error", "payload_too_short"); return; }
            uint16_t bandwidth_khz = read_u16(payload + 0);
            w.set("bandwidth_hz", (int32_t)bandwidth_khz * 1000);
            break;
        }

        case RESP_SET_GAIN: {
            // ICD layout: gain_db_tenths(i16) — signed, tenths of dB
            if (payload_len < 2) { w.set("parse_error", "payload_too_short"); return; }
            int16_t raw = read_i16(payload + 0);
            w.set("gain_db", raw / 10.0);
            break;
        }

        default: {
            char* hex = to_hex_string(payload, payload_len);
            w.set("raw_hex", hex);
            delete[] hex;
        }
    }
}

static void parse_group_measurement(const uint8_t* payload, int payload_len,
                                     uint16_t unit_id, JsonWriter& w) {
    switch (unit_id) {

        case RESP_SCAN_RESULT: {
            // ICD layout: frequency_khz(u32) power_dbuv_tenths(i16)
            //             confidence(u8, 0-100) timestamp_ms(u32)
            // Total: 11 bytes
            // Convert: kHz->Hz, dBuV->dBm
            if (payload_len < 11) { w.set("parse_error", "payload_too_short"); return; }
            uint32_t freq_khz  = read_u32(payload + 0);
            int16_t  pwr_raw   = read_i16(payload + 4);
            uint8_t  conf      = read_u8 (payload + 6);
            uint32_t ts_ms     = read_u32(payload + 7);

            w.set("frequency_hz",   (int64_t)freq_khz * 1000LL);
            w.set("power_dbm",      (pwr_raw / 10.0) - 107.0);
            w.set("confidence_pct", (int32_t)conf);
            w.set("timestamp_ms",   (uint32_t)ts_ms);
            break;
        }

        case RESP_AOA_RESULT: {
            // ICD layout: azimuth_tenths(u16, 0-3599) elevation_tenths(i16)
            //             quality(u8, 0-100)
            // Total: 5 bytes
            // Convert: tenths of degree -> degrees (double)
            if (payload_len < 5) { w.set("parse_error", "payload_too_short"); return; }
            uint16_t az_raw  = read_u16(payload + 0);
            int16_t  el_raw  = read_i16(payload + 2);
            uint8_t  quality = read_u8 (payload + 4);

            w.set("azimuth_deg",   az_raw / 10.0);
            w.set("elevation_deg", el_raw / 10.0);
            w.set("aoa_quality",   (int32_t)quality);
            break;
        }

        case RESP_FFT_RESULT: {
            // ICD layout: start_freq_khz(u32) step_khz(u16)
            //             num_bins(u16) bins[](i16 each, dBuV tenths)
            // Variable length — num_bins determines array size
            if (payload_len < 8) { w.set("parse_error", "payload_too_short"); return; }
            uint32_t start_khz = read_u32(payload + 0);
            uint16_t step_khz  = read_u16(payload + 4);
            uint16_t num_bins  = read_u16(payload + 6);

            w.set("fft_start_frequency_hz", (int64_t)start_khz * 1000LL);
            w.set("fft_step_hz",            (int32_t)step_khz  * 1000);
            w.set("fft_num_bins",           (int32_t)num_bins);
            // FFT bin array is large — emit count only; raw FFT data goes as metadata
            // Full bin decode would require a larger JSON builder or a binary blob field
            break;
        }

        default: {
            char* hex = to_hex_string(payload, payload_len);
            w.set("raw_hex", hex);
            delete[] hex;
        }
    }
}

static void parse_group_gnss(const uint8_t* payload, int payload_len,
                              uint16_t unit_id, JsonWriter& w) {
    switch (unit_id) {

        case RESP_POSITION: {
            // ICD layout: latitude_deg_1e7(i32) longitude_deg_1e7(i32)
            //             altitude_mm(i32) fix_type(u8)
            // Total: 13 bytes
            // Convert: 1e-7 degrees -> degrees (double)
            if (payload_len < 13) { w.set("parse_error", "payload_too_short"); return; }
            int32_t lat_raw  = read_i32(payload + 0);
            int32_t lon_raw  = read_i32(payload + 4);
            int32_t alt_mm   = read_i32(payload + 8);
            uint8_t fix_type = read_u8 (payload + 12);

            w.set("latitude_deg",  lat_raw * 1e-7);
            w.set("longitude_deg", lon_raw * 1e-7);
            w.set("altitude_m",    alt_mm  / 1000.0);
            w.set("gnss_fix_type", (int32_t)fix_type);
            break;
        }

        case RESP_SATELLITES: {
            // ICD layout: num_visible(u8) num_used(u8) hdop_tenths(u16)
            // Total: 4 bytes
            if (payload_len < 4) { w.set("parse_error", "payload_too_short"); return; }
            w.set("satellites_visible", (int32_t)read_u8 (payload + 0));
            w.set("satellites_used",    (int32_t)read_u8 (payload + 1));
            w.set("hdop",               read_u16(payload + 2) / 10.0);
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

    // ── SDFC->DRS command frame ───────────────────────────────────────────────
    // First 4 bytes: 0xAA 0xAB 0xBA 0xBB
    if (buf_len >= 4 && match_magic(buf, CMD_HEADER, 4)) {
        if (buf_len < CMD_OVERHEAD) return 0; // wait for more bytes

        uint32_t payload_len = read_u32(buf + OFFSET_LENGTH);
        if (payload_len > static_cast<uint32_t>(MAX_PAYLOAD)) return -1;

        int total = CMD_OVERHEAD + static_cast<int>(payload_len);
        if (buf_len < total) return 0;

        // Validate footer
        if (!match_magic(buf + total - 4, CMD_FOOTER, 4)) return -1;

        // Validate CRC (covers bytes 4 to total-6)
        if (!validate_crc(buf, total - 6)) return -1;

        std::memcpy(out_frame, buf, static_cast<std::size_t>(total));
        *out_len = total;
        return 1;
    }

    // ── DRS->SDFC response frame ──────────────────────────────────────────────
    // First 4 bytes: 0xEE 0xEF 0xFE 0xFF
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

    // ── SCD compact frame ─────────────────────────────────────────────────────
    // First 2 bytes: 0xAA 0xAA
    // NOTE: shares first byte with CMD_HEADER (0xAA) — second byte differs
    if (buf_len >= 2 && match_magic(buf, SCD_HEADER, 2)) {
        if (buf_len < SCD_OVERHEAD) return 0;

        uint16_t payload_len = read_u16(buf + 2);
        if (payload_len > static_cast<uint16_t>(MAX_PAYLOAD)) return -1;

        int total = SCD_OVERHEAD + static_cast<int>(payload_len);
        if (buf_len < total) return 0;

        if (!match_magic(buf + total - 2, SCD_FOOTER, 2)) return -1;

        // SCD uses XOR checksum over bytes [2 .. total-3)
        uint8_t computed = xor_checksum(buf + 2, total - 5);
        uint8_t stored   = buf[total - 3];
        if (computed != stored) return -1;

        std::memcpy(out_frame, buf, static_cast<std::size_t>(total));
        *out_len = total;
        return 3;
    }

    // No recognised header at buf[0]
    return -1;
}


__declspec(dllexport)
const char* parse_message(const uint8_t* frame, int frame_len, int frame_type) {

    if (!frame || frame_len < 4) return nullptr;
    if (frame_type < 1 || frame_type > 3) return nullptr;

    JsonWriter w;

    // ── Common header fields ──────────────────────────────────────────────────
    switch (frame_type) {
        case 1: w.set("frame_type", "command");  break;
        case 2: w.set("frame_type", "response"); break;
        case 3: w.set("frame_type", "scd");      break;
    }

    // ── CMD and RESP frames ───────────────────────────────────────────────────
    if (frame_type == 1 || frame_type == 2) {
        if (frame_len < 12) return nullptr;

        uint16_t group_id = read_u16(frame + OFFSET_GROUP_ID);
        uint16_t unit_id  = read_u16(frame + OFFSET_UNIT_ID);
        w.set("group_id", (int32_t)group_id);
        w.set("unit_id",  (int32_t)unit_id);

        // Payload starts after the frame header
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
            case GROUP_SYSTEM_MGMT:
                parse_group_system_mgmt(payload, payload_len, unit_id, w);
                break;
            case GROUP_RF_CONTROL:
                parse_group_rf_control(payload, payload_len, unit_id, w);
                break;
            case GROUP_MEASUREMENT:
                parse_group_measurement(payload, payload_len, unit_id, w);
                break;
            case GROUP_GNSS:
                parse_group_gnss(payload, payload_len, unit_id, w);
                break;
            default: {
                char* hex = to_hex_string(payload, payload_len);
                w.set("raw_hex", hex);
                delete[] hex;
            }
        }
    }

    // ── SCD compact frame ─────────────────────────────────────────────────────
    else if (frame_type == 3) {
        if (frame_len < SCD_OVERHEAD) return nullptr;
        uint16_t       payload_len = read_u16(frame + 2);
        const uint8_t* payload     = frame + SCD_HEADER_SIZE;
        w.set("payload_len", (int32_t)payload_len);
        // SCD payload is hardware-variant specific — decode per ICD section 5
        char* hex = to_hex_string(payload, payload_len);
        w.set("raw_hex", hex);
        delete[] hex;
    }

    if (!w.ok()) return nullptr;
    return w.finish(); // heap-allocated — caller MUST call free_result()
}


__declspec(dllexport)
int format_response(const char* json_response, uint8_t* out_frame) {

    if (!json_response || !out_frame) return -1;

    // Extract the three mandatory fields from JSON using sscanf.
    // Avoids pulling in a full JSON parser dependency.
    int group_id = -1, unit_id = -1, status = -1;
    const char* p = json_response;
    while (*p) {
        if (std::sscanf(p, "\"group_id\":%d", &group_id) == 1) {}
        if (std::sscanf(p, "\"unit_id\":%d",  &unit_id)  == 1) {}
        if (std::sscanf(p, "\"status\":%d",   &status)   == 1) {}
        ++p;
    }
    if (group_id < 0 || unit_id < 0 || status < 0) return -1;

    // Build DRS->SDFC response frame (0-payload minimal response)
    // Layout: [header:4][length:4][group_id:2][unit_id:2][status:2][crc:2][footer:4]
    uint8_t* out = out_frame;

    std::memcpy(out, RESP_HEADER, 4);                          out += 4;
    write_u32(out, 0);                                         out += 4; // payload length = 0
    write_u16(out, static_cast<uint16_t>(group_id));           out += 2;
    write_u16(out, static_cast<uint16_t>(unit_id));            out += 2;
    write_u16(out, static_cast<uint16_t>(status));             out += 2;

    // CRC over bytes [4 .. current position)
    uint16_t crc = crc16_ccitt(out_frame + 4,
                                static_cast<int>(out - out_frame) - 4);
    write_u16(out, crc);                                       out += 2;

    std::memcpy(out, RESP_FOOTER, 4);                          out += 4;

    return static_cast<int>(out - out_frame);
}


__declspec(dllexport)
void free_result(const char* result) {
    delete[] result; // matches new char[] in JsonWriter::finish()
    // delete[] nullptr is a safe no-op in C++11 and later
}

} // extern "C"
