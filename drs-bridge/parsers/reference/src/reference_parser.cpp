// drs-bridge/parsers/reference/src/reference_parser.cpp
//
// REFERENCE PARSER IMPLEMENTATION — variant developer template
//
// Synthetic frame format (NOT a real IRS):
//   byte 0:       0xAA              (magic; distinguishes our frames from noise)
//   byte 1:       payload_length    (uint8_t; max 253 because total <= 255)
//   bytes 2..N:   payload           (payload_length bytes)
//
// Currently supports one response kind:
//   "time"  — payload is 4-byte little-endian timestamp_seconds (uint32_t)
//             followed by 2 reserved zero bytes.
//
// Real variant developers: replace the synthetic format with your IRS
// frame structure and add new "kinds" in format_response's dispatch table.
#include "reference_parser.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <new>

namespace {

constexpr uint8_t kFrameMagic = 0xAA;
constexpr size_t  kHeaderLen  = 2;     // magic byte + length byte
constexpr size_t  kTimePayloadLen = 6; // 4-byte seconds + 2-byte reserved

// Minimal JSON int extractor: finds `"<key>":<int>` in a JSON object string
// and returns the int via *out. Returns 0 on success, -1 if not found or
// malformed. Variant developers will likely replace this with a real JSON
// library; we hand-roll here to keep the template free of dependencies.
int json_extract_int64(const char* json, const char* key, int64_t* out) {
    if (!json || !key || !out) return -1;
    char needle[64];
    int n = std::snprintf(needle, sizeof(needle), "\"%s\":", key);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(needle)) return -1;
    const char* p = std::strstr(json, needle);
    if (!p) return -1;
    p += n;
    while (*p == ' ' || *p == '\t') ++p;
    char* end = nullptr;
    long long v = std::strtoll(p, &end, 10);
    if (end == p) return -1;
    *out = static_cast<int64_t>(v);
    return 0;
}

// Allocate and copy `n` bytes. Returns NULL on OOM.
uint8_t* alloc_copy(const uint8_t* src, size_t n) {
    uint8_t* p = static_cast<uint8_t*>(std::malloc(n));
    if (!p) return nullptr;
    std::memcpy(p, src, n);
    return p;
}

// Allocate and copy a null-terminated string (including the terminator).
char* alloc_string(const char* s, size_t len_excl_null) {
    char* p = static_cast<char*>(std::malloc(len_excl_null + 1));
    if (!p) return nullptr;
    std::memcpy(p, s, len_excl_null);
    p[len_excl_null] = '\0';
    return p;
}

} // namespace

extern "C" {

int extract_frame(
    const uint8_t* buf,
    size_t length,
    uint8_t** out_frame,
    size_t* out_len)
{
    if (!buf || !out_frame || !out_len) return -1;
    // Scan for the magic byte. Discard any preamble bytes (frame sync).
    for (size_t i = 0; i + kHeaderLen <= length; ++i) {
        if (buf[i] != kFrameMagic) continue;
        uint8_t payload_len = buf[i + 1];
        size_t total = kHeaderLen + payload_len;
        if (i + total > length) return -1; // incomplete frame
        uint8_t* copy = alloc_copy(buf + i, total);
        if (!copy) return -1;
        *out_frame = copy;
        *out_len = total;
        return 0;
    }
    return -1;
}

int parse_message(
    const uint8_t* frame,
    size_t frame_len,
    char** out_json,
    size_t* out_len)
{
    if (!frame || !out_json || !out_len) return -1;
    if (frame_len < kHeaderLen) return -1;
    if (frame[0] != kFrameMagic) return -1;
    uint8_t payload_len = frame[1];
    if (frame_len != kHeaderLen + payload_len) return -1;

    // Only the time payload is known to the reference parser.
    if (payload_len != kTimePayloadLen) return -1;
    const uint8_t* p = frame + kHeaderLen;
    uint32_t ts = static_cast<uint32_t>(p[0])
                | (static_cast<uint32_t>(p[1]) << 8)
                | (static_cast<uint32_t>(p[2]) << 16)
                | (static_cast<uint32_t>(p[3]) << 24);

    char buf[64];
    int n = std::snprintf(buf, sizeof(buf),
        "{\"kind\":\"time\",\"timestamp_seconds\":%u}", ts);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(buf)) return -1;
    char* copy = alloc_string(buf, static_cast<size_t>(n));
    if (!copy) return -1;
    *out_json = copy;
    *out_len = static_cast<size_t>(n);
    return 0;
}

int format_response(
    const char* kind,
    const char* kwargs_json,
    uint8_t** out_buf,
    size_t* out_len)
{
    if (!kind || !out_buf || !out_len) return -1;

    if (std::strcmp(kind, "time") == 0) {
        int64_t ts_ns = 0;
        if (json_extract_int64(kwargs_json, "timestamp_ns", &ts_ns) != 0) return -1;
        uint32_t ts_s = static_cast<uint32_t>(ts_ns / 1000000000LL);
        const size_t total = kHeaderLen + kTimePayloadLen;
        uint8_t* buf = static_cast<uint8_t*>(std::malloc(total));
        if (!buf) return -1;
        buf[0] = kFrameMagic;
        buf[1] = kTimePayloadLen;
        buf[2] = static_cast<uint8_t>(ts_s & 0xFF);
        buf[3] = static_cast<uint8_t>((ts_s >> 8)  & 0xFF);
        buf[4] = static_cast<uint8_t>((ts_s >> 16) & 0xFF);
        buf[5] = static_cast<uint8_t>((ts_s >> 24) & 0xFF);
        buf[6] = 0;  // reserved
        buf[7] = 0;  // reserved
        *out_buf = buf;
        *out_len = total;
        return 0;
    }

    // Unknown kind. Real variant parsers expand the dispatch above.
    return -1;
}

void free_result(void* ptr) {
    std::free(ptr);
}

} // extern "C"
