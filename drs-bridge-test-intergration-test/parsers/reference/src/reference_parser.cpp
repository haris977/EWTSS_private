// drs-bridge/parsers/reference/src/reference_parser.cpp
//
// REFERENCE PARSER — implements sdfc_abi.h-compatible ABI.
//
// Synthetic frame format (NOT a real IRS):
//   byte 0:       0xAA              (magic)
//   byte 1:       payload_length    (uint8_t; 0-253)
//   bytes 2..N:   payload           (payload_length bytes)
//
// All frames are returned as type 1 (command).
// When payload_length == 6: bytes [2..5] are a LE uint32 timestamp_seconds,
// bytes [6..7] are reserved — this is the "time" subtype.
#include "reference_parser.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cstdint>

namespace {

constexpr uint8_t kMagic      = 0xAA;
constexpr int     kHeaderLen  = 2;

} // namespace

extern "C" {

int extract_frame(
    const uint8_t* buf,
    int            buf_len,
    uint8_t*       out_frame,
    int*           out_len)
{
    if (!buf || buf_len < kHeaderLen || !out_frame || !out_len) return -1;
    if (buf[0] != kMagic) return -1;               // corrupt: wrong magic
    int payload_len = static_cast<int>(buf[1]);
    int total       = kHeaderLen + payload_len;
    if (buf_len < total) return 0;                 // incomplete: need more bytes
    if (total > MAX_FRAME_BUFFER_BYTES) return -1; // corrupt: impossibly large
    std::memcpy(out_frame, buf, static_cast<size_t>(total));
    *out_len = total;
    return 1;  // type 1 = command frame
}

const char* parse_message(
    const uint8_t* frame,
    int            frame_len,
    int            frame_type)
{
    if (!frame || frame_len < kHeaderLen) return nullptr;
    if (frame[0] != kMagic) return nullptr;
    int payload_len = static_cast<int>(frame[1]);
    if (frame_len != kHeaderLen + payload_len) return nullptr;

    uint32_t ts_s = 0;
    if (payload_len >= 4) {
        ts_s = static_cast<uint32_t>(frame[2])
             | (static_cast<uint32_t>(frame[3]) << 8)
             | (static_cast<uint32_t>(frame[4]) << 16)
             | (static_cast<uint32_t>(frame[5]) << 24);
    }

    char buf[256];
    int n = std::snprintf(buf, sizeof(buf),
        "{\"frame_type\":%d,\"group_id\":0,\"unit_id\":1,"
        "\"kind\":\"time\",\"timestamp_seconds\":%u}",
        frame_type, ts_s);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(buf)) return nullptr;

    char* result = static_cast<char*>(std::malloc(static_cast<size_t>(n) + 1));
    if (!result) return nullptr;
    std::memcpy(result, buf, static_cast<size_t>(n) + 1);
    return result;
}

int format_response(
    const char* json_response,
    uint8_t*    out_frame)
{
    if (!json_response || !out_frame) return -1;
    // Minimal stub: emit a 2-byte reference frame with empty payload.
    out_frame[0] = kMagic;
    out_frame[1] = 0;
    return 2;
}

void free_result(const char* result)
{
    std::free(const_cast<char*>(result));
}

} // extern "C"
