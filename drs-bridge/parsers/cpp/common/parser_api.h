// parsers/cpp/common/parser_api.h
// ABI CONTRACT — Version 1
//
// Rules:
//   1. All functions use C linkage (extern "C") — no C++ name mangling
//   2. No C++ types cross the boundary (no std::string, no exceptions)
//   3. No LGPL dependencies linked into this DLL
//   4. free_result() MUST be called after every parse_message()
//   5. out_frame buffer MUST be at least MAX_FRAME_BUFFER_BYTES
//
// Changing this header requires 24-hour cross-team review (Person D sign-off).
#pragma once
#include <cstdint>

static constexpr int MAX_FRAME_BUFFER_BYTES = 1048576 + 64; // 1 MB + overhead

extern "C" {

    // -------------------------------------------------------------------------
    // extract_frame
    //
    // Scans buf[0..buf_len) for the next complete, valid frame.
    //
    // Returns:
    //    1  — SDFC->DRS command frame   (header 0xAA 0xAB 0xBA 0xBB)
    //    2  — DRS->SDFC response frame  (header 0xEE 0xEF 0xFE 0xFF)
    //    3  — SCD compact frame         (header 0xAA 0xAA)
    //    0  — incomplete frame, call again with more bytes
    //   -1  — corrupt/unrecognised, Python must drain buffer and reconnect
    //
    // On success (1/2/3):
    //   Copies frame bytes into out_frame, sets *out_len to byte count.
    //   Does NOT advance buf — Python owns the accumulation buffer.
    // -------------------------------------------------------------------------
    __declspec(dllexport)
    int extract_frame(
        const uint8_t* buf,
        int            buf_len,
        uint8_t*       out_frame,
        int*           out_len
    );

    // -------------------------------------------------------------------------
    // parse_message
    //
    // Decodes a complete frame into a heap-allocated UTF-8 JSON string.
    // frame_type is the value returned by extract_frame (1, 2, or 3).
    //
    // Returns: pointer to new char[] — CALLER MUST CALL free_result()
    //          nullptr on catastrophic parse failure (Python logs and skips)
    //
    // JSON always contains: frame_type, group_id, unit_id
    // All numeric fields are in SI units (Hz, dBm, degrees, seconds).
    // -------------------------------------------------------------------------
    __declspec(dllexport)
    const char* parse_message(
        const uint8_t* frame,
        int            frame_len,
        int            frame_type
    );

    // -------------------------------------------------------------------------
    // format_response
    //
    // Encodes a JSON response dict into a binary DRS->SDFC frame.
    //
    // json_response MUST contain: "group_id" (int), "unit_id" (int),
    //                             "status" (int, 0 = OK)
    //
    // Returns: bytes written into out_frame, or -1 on encoding error.
    // out_frame must be caller-allocated with MIN MAX_FRAME_BUFFER_BYTES bytes.
    // -------------------------------------------------------------------------
    __declspec(dllexport)
    int format_response(
        const char* json_response,
        uint8_t*    out_frame
    );

    // -------------------------------------------------------------------------
    // free_result
    //
    // Releases memory allocated by parse_message.
    // Uses delete[] to match the new[] in parse_message.
    // Safe to call with nullptr.
    // -------------------------------------------------------------------------
    __declspec(dllexport)
    void free_result(const char* result);

} // extern "C"
