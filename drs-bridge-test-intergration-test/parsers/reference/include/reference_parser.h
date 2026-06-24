// drs-bridge/parsers/reference/include/reference_parser.h
//
// REFERENCE PARSER — variant developer template (sdfc_abi.h ABI)
//
// Implements the same 4-symbol ABI as sdfc_abi.h so the reference DLL can be
// loaded by parser_loader.py alongside the dp_ecm family.
//
// Synthetic frame format (NOT a real IRS):
//   byte 0:     0xAA               (magic)
//   byte 1:     payload_length     (uint8_t)
//   bytes 2..N: payload            (payload_length bytes)
// All frames are treated as type 1 (command) by extract_frame.
#ifndef REFERENCE_PARSER_H
#define REFERENCE_PARSER_H

#include <stdint.h>

#ifdef _WIN32
#  define PARSER_EXPORT __declspec(dllexport)
#else
#  define PARSER_EXPORT __attribute__((visibility("default")))
#endif

static const int MAX_FRAME_BUFFER_BYTES = 1048576 + 64;

#ifdef __cplusplus
extern "C" {
#endif

// Scan buf[0..buf_len) for one complete reference frame.
// Returns: 1=complete frame copied to out_frame, 0=incomplete, -1=corrupt.
// Caller provides out_frame (>= MAX_FRAME_BUFFER_BYTES).
PARSER_EXPORT int extract_frame(
    const uint8_t* buf,
    int            buf_len,
    uint8_t*       out_frame,
    int*           out_len);

// Decode a complete frame into a heap-allocated JSON string.
// frame_type is the value returned by extract_frame (always 1 here).
// Returns heap char* the caller MUST release with free_result(), or NULL.
PARSER_EXPORT const char* parse_message(
    const uint8_t* frame,
    int            frame_len,
    int            frame_type);

// Encode a JSON response object into out_frame (caller-allocated, >= MAX_FRAME_BUFFER_BYTES).
// Returns bytes written, or -1 on error.
PARSER_EXPORT int format_response(
    const char* json_response,
    uint8_t*    out_frame);

// Free a pointer returned by parse_message. Safe with NULL.
PARSER_EXPORT void free_result(const char* result);

#ifdef __cplusplus
}
#endif

#endif // REFERENCE_PARSER_H
