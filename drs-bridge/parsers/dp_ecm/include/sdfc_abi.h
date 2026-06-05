// drs-bridge/parsers/dp_ecm/include/sdfc_abi.h
//
// SDFC<->DRS PARSER ABI — Version 1 (canonical)
// ----------------------------------------------
// This is the single, frozen contract every Family-A parser DLL exports.
// It supersedes the two conflicting historical headers
// (parsers/cpp/common/parser_api.h and parsers/reference/include/reference_parser.h).
//
// Rules (do not break without cross-team sign-off):
//   1. All four symbols use C linkage (extern "C") — no C++ name mangling.
//   2. No C++ types cross the boundary (no std::string, no exceptions escape).
//   3. No LGPL-licensed code is linked into the DLL.
//   4. parse_message() returns heap memory — the caller MUST call free_result().
//   5. Caller-allocated buffers (out_frame) MUST be >= MAX_FRAME_BUFFER_BYTES.
//
// Frame model (authoritative, per DP-ECM-1071/1074 vendor ICDs):
//   type 1 = SDFC->DRS command   (header AA AB BA BB, footer CC CD DC DD, 16B overhead)
//   type 2 = DRS->SDFC response  (header EE EF FE FF, footer FF FE EF EE, 18B overhead,
//                                 signed int16 status at offset 4)
//   type 3 = streaming/IQ frame  (header EE EF FE FF, footer FF FE EF EE, 16B overhead,
//                                 reserved for dedicated stream sockets)
//   All multi-byte integers are LITTLE-ENDIAN. There is NO frame CRC.
#ifndef SDFC_ABI_H
#define SDFC_ABI_H

#include <cstdint>

// 1 MB max payload + generous frame overhead headroom.
static constexpr int MAX_FRAME_BUFFER_BYTES = 1048576 + 64;

#ifdef _WIN32
#  define SDFC_EXPORT __declspec(dllexport)
#else
#  define SDFC_EXPORT __attribute__((visibility("default")))
#endif

extern "C" {

// -----------------------------------------------------------------------------
// extract_frame
//   Scans buf[0..buf_len) for the next complete, valid frame.
//   Returns:
//      1 — command frame   (SDFC->DRS)
//      2 — response frame  (DRS->SDFC)
//      3 — streaming frame  (reserved — only emitted by stream-socket builds)
//      0 — incomplete: no full frame yet, call again with more bytes
//     -1 — corrupt: a header matched but length/footer validation failed
//   On success (1/2/3): copies the complete frame into out_frame, sets *out_len
//   to the frame byte count. Does NOT advance buf — the host owns the buffer and
//   slices off *out_len bytes from the front after a successful call.
//
//   NOTE (increment 1): assumes the frame begins at buf[0] (clean-line contract,
//   matching the documented host loop). Noise-tolerant resync is a later step.
// -----------------------------------------------------------------------------
SDFC_EXPORT int extract_frame(const uint8_t* buf,
                              int            buf_len,
                              uint8_t*       out_frame,
                              int*           out_len);

// -----------------------------------------------------------------------------
// parse_message
//   Decodes a complete frame into a heap-allocated UTF-8 JSON string.
//   frame_type is the value returned by extract_frame (1, 2, or 3).
//   Returns a pointer the CALLER MUST free with free_result(), or nullptr on
//   unrecoverable parse failure.
//   JSON always contains: frame_type, group_id, unit_id (+ status on responses).
//   All numeric scalars are in SI units (Hz, dBm, seconds).
// -----------------------------------------------------------------------------
SDFC_EXPORT const char* parse_message(const uint8_t* frame,
                                      int            frame_len,
                                      int            frame_type);

// -----------------------------------------------------------------------------
// format_response
//   Encodes a JSON response object into a binary DRS->SDFC response frame.
//   json_response MUST contain: "group_id" (int), "unit_id" (int), "status" (int).
//   Optional "payload_hex" (string of hex bytes) is appended as the payload.
//   Writes into out_frame (caller-allocated, >= MAX_FRAME_BUFFER_BYTES).
//   Returns the number of bytes written, or -1 on encoding error.
// -----------------------------------------------------------------------------
SDFC_EXPORT int format_response(const char* json_response,
                                uint8_t*    out_frame);

// -----------------------------------------------------------------------------
// free_result
//   Releases memory returned by parse_message. Safe to call with nullptr.
// -----------------------------------------------------------------------------
SDFC_EXPORT void free_result(const char* result);

} // extern "C"

#endif // SDFC_ABI_H
