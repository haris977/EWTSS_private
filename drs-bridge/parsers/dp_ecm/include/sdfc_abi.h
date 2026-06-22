// drs-bridge/parsers/dp_ecm/include/sdfc_abi.h
//
// SDFC<->DRS PARSER ABI — canonical 4-symbol contract
// -----------------------------------------------------
// Every Family-A variant DLL (DP-ECM HF/VU, RDFS, COMM DF) exports exactly
// these four symbols. Aligns with parsers/reference/include/reference_parser.h.
//
// Rules (do not break without cross-team sign-off):
//   1. All four symbols use C linkage (extern "C") — no C++ name mangling.
//   2. No C++ types cross the boundary (no std::string, no exceptions escape).
//   3. No LGPL-licensed code is linked into the DLL.
//   4. All out-pointers are DLL-heap-allocated — caller MUST call free_result().
//
// Frame model (authoritative, per DP-ECM-1071/1074 vendor ICDs):
//   command  = SDFC->DRS  (header AA AB BA BB, footer CC CD DC DD, 16B overhead)
//   response = DRS->SDFC  (header EE EF FE FF, footer FF FE EF EE, 18B overhead,
//                          signed int16 status at offset 4)
//   All multi-byte integers are LITTLE-ENDIAN. There is NO frame CRC.
#ifndef SDFC_ABI_H
#define SDFC_ABI_H

#include <cstddef>
#include <cstdint>

// Internal upper bound: 1 MB max payload + frame overhead. Used inside the DLL
// only — callers never allocate this buffer.
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
//   On success: sets *out_frame to a DLL-heap-allocated copy of the frame bytes,
//   sets *out_len to the frame byte count, and returns 0.
//   Returns -1 if no complete frame is present (incomplete or corrupt header).
//   Caller MUST call free_result(*out_frame) when done.
//   Does NOT advance buf — the host slices *out_len bytes from the front after
//   a successful call.
// -----------------------------------------------------------------------------
SDFC_EXPORT int extract_frame(const uint8_t* buf,
                              size_t         buf_len,
                              uint8_t**      out_frame,
                              size_t*        out_len);

// -----------------------------------------------------------------------------
// parse_message
//   Decodes a complete frame (returned by extract_frame) into a heap-allocated
//   UTF-8 JSON string. Frame type (command vs response) is inferred from the
//   frame's magic bytes — no frame_type parameter is needed.
//   On success: sets *out_json + *out_len and returns 0.
//   Returns -1 on malformed input. Caller MUST call free_result(*out_json).
//   JSON always contains: hw, frame_type, group_id, unit_id (+ status on responses).
//   All numeric scalars are in SI units (Hz, dBm, seconds).
// -----------------------------------------------------------------------------
SDFC_EXPORT int parse_message(const uint8_t* frame,
                              size_t         frame_len,
                              char**         out_json,
                              size_t*        out_len);

// -----------------------------------------------------------------------------
// format_response
//   Encodes a response into a binary DRS->SDFC response frame.
//   kind        — null-terminated string describing the response kind (e.g. "response")
//   kwargs_json — null-terminated JSON object; MUST contain "group_id" (int),
//                 "unit_id" (int), "status" (int). Optional "payload_hex" (hex
//                 string) is appended as the frame payload.
//   On success: sets *out_buf to a DLL-heap-allocated frame buffer, sets *out_len
//   to its byte count, and returns 0. Returns -1 on encoding error.
//   Caller MUST call free_result(*out_buf).
// -----------------------------------------------------------------------------
SDFC_EXPORT int format_response(const char* kind,
                                const char* kwargs_json,
                                uint8_t**   out_buf,
                                size_t*     out_len);

// -----------------------------------------------------------------------------
// free_result
//   Releases any pointer returned via an out-parameter above. Safe to call
//   with NULL.
// -----------------------------------------------------------------------------
SDFC_EXPORT void free_result(void* ptr);

} // extern "C"

#endif // SDFC_ABI_H
