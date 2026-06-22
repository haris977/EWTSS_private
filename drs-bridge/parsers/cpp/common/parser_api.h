// parsers/cpp/common/parser_api.h
// ABI CONTRACT — Version 2
//
// Canonical 4-symbol DLL interface shared by all SDFC device parsers.
// Aligned with parsers/dp_ecm/include/sdfc_abi.h.
//
// Rules:
//   1. All functions use C linkage (extern "C") — no C++ name mangling
//   2. No C++ types cross the boundary (no std::string, no exceptions)
//   3. No LGPL dependencies linked into this DLL
//   4. free_result() MUST be called after every successful extract_frame/parse_message/format_response
//   5. All output buffers are DLL-heap-allocated via std::malloc; caller frees with free_result()
//
// Changing this header requires 24-hour cross-team review (Person D sign-off).
#pragma once
#include <cstdint>
#include <cstddef>

static constexpr int MAX_FRAME_BUFFER_BYTES = 1048576 + 64; // 1 MB + overhead

extern "C" {

    // -------------------------------------------------------------------------
    // extract_frame
    //
    // Scans buf[0..buf_len) for the next complete, valid frame.
    //
    // Returns:
    //    0  — success: *out_frame points to DLL-malloc'd frame bytes, *out_len set
    //   -1  — failure: incomplete, corrupt, or unrecognised (no allocation made)
    //
    // Caller MUST call free_result(*out_frame) on success.
    // Does NOT advance buf — Python owns the accumulation buffer.
    // -------------------------------------------------------------------------
    __declspec(dllexport)
    int extract_frame(
        const uint8_t* buf,
        size_t         buf_len,
        uint8_t**      out_frame,
        size_t*        out_len
    );

    // -------------------------------------------------------------------------
    // parse_message
    //
    // Decodes a complete frame (from extract_frame) into a heap-allocated
    // UTF-8 JSON string. Frame type is inferred from magic bytes — NOT passed
    // as a parameter.
    //
    // Returns:
    //    0  — success: *out_json points to DLL-malloc'd NUL-terminated JSON, *out_len set
    //   -1  — failure (no allocation made)
    //
    // Caller MUST call free_result(*out_json) on success.
    // All numeric fields are in SI units (Hz, dBm, degrees, seconds).
    // -------------------------------------------------------------------------
    __declspec(dllexport)
    int parse_message(
        const uint8_t* frame,
        size_t         frame_len,
        char**         out_json,
        size_t*        out_len
    );

    // -------------------------------------------------------------------------
    // format_response
    //
    // Encodes a response into a binary outbound frame.
    //
    // kind        — frame kind selector string (e.g. "command", "scd")
    // kwargs_json — JSON dict of frame parameters (group_id, unit_id, status, ...)
    //
    // Returns:
    //    0  — success: *out_buf points to DLL-malloc'd frame bytes, *out_len set
    //   -1  — failure: unsupported kind, missing required fields, or alloc error
    //
    // Caller MUST call free_result(*out_buf) on success.
    // -------------------------------------------------------------------------
    __declspec(dllexport)
    int format_response(
        const char* kind,
        const char* kwargs_json,
        uint8_t**   out_buf,
        size_t*     out_len
    );

    // -------------------------------------------------------------------------
    // free_result
    //
    // Releases memory allocated by extract_frame, parse_message, or
    // format_response. Safe to call with nullptr.
    // -------------------------------------------------------------------------
    __declspec(dllexport)
    void free_result(void* ptr);

} // extern "C"
