// drs-bridge/parsers/reference/include/reference_parser.h
//
// REFERENCE PARSER — variant developer template
//
// This header declares the 4-symbol ABI every variant parser must export.
// The reference implementation under src/reference_parser.cpp uses a
// deliberately fake synthetic frame format. Real variants replace the
// bodies with their IRS-specific logic; the ABI is universal.
//
// All symbols use C linkage (extern "C") so ctypes can find them on
// Windows + Linux without name mangling.
#ifndef REFERENCE_PARSER_H
#define REFERENCE_PARSER_H

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#  define PARSER_EXPORT __declspec(dllexport)
#else
#  define PARSER_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// extract_frame: scan `buf` for a complete frame. On success, set
// *out_frame to a newly-allocated copy of the frame bytes, set *out_len
// to the frame length, and return 0. Returns -1 if no complete frame
// is present. Caller MUST call free_result(*out_frame) when done.
PARSER_EXPORT int extract_frame(
    const uint8_t* buf,
    size_t length,
    uint8_t** out_frame,
    size_t* out_len);

// parse_message: decode a frame (returned by extract_frame) into a
// newly-allocated JSON string. On success, set *out_json + *out_len and
// return 0. Returns -1 on malformed input. Caller MUST call free_result.
PARSER_EXPORT int parse_message(
    const uint8_t* frame,
    size_t frame_len,
    char** out_json,
    size_t* out_len);

// format_response: emit a frame for the given response kind.
//   kind          — null-terminated string, e.g. "time"
//   kwargs_json   — null-terminated JSON object, e.g. {"timestamp_ns":1234567890}
//   *out_buf      — set to a newly-allocated frame buffer
//   *out_len      — set to its length
// Returns 0 on success, -1 if `kind` is unknown or kwargs_json is malformed.
// Caller MUST call free_result(*out_buf).
PARSER_EXPORT int format_response(
    const char* kind,
    const char* kwargs_json,
    uint8_t** out_buf,
    size_t* out_len);

// free_result: free any pointer returned via an out-parameter above.
// Safe to call with NULL.
PARSER_EXPORT void free_result(void* ptr);

#ifdef __cplusplus
}
#endif

#endif // REFERENCE_PARSER_H
