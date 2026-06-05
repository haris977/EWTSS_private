#pragma once
#include <cstdint>
#include <cstddef>

// Three frame types defined in the COMM DF ICD §3.
// extract_frame() in the real variant parser returns one of these as an int;
// here we embed it in the result struct so the caller knows which layout was decoded.
enum class BinaryFrameType { Unknown, SdfcResponse, SdfcCommand, ScdCompact };

struct BinaryFrame {
    BinaryFrameType type;
    uint16_t        group_id;   // for ScdCompact: stores cmd_code (no group/unit split in SCD)
    uint16_t        unit_id;    // for ScdCompact: always 0
    int16_t         status;     // SDFC Response only; 0 for other types
    const uint8_t*  payload;    // points into the caller's buffer — do NOT free, do NOT use
                                // after the source buffer is gone
    uint32_t        payload_len;
};

// Try to detect and parse a complete binary frame starting at buf[0].
// Returns true on success and populates `out`.
// Returns false if buf doesn't start with a recognised magic sequence, the
// data is too short, or the footer check fails.
// Does NOT allocate memory.
bool parse_binary_frame(const uint8_t* buf, size_t len, BinaryFrame& out);

// Quick check: does buf[0] look like a binary frame (0xAA or 0xEE prefix)?
// Use this to route incoming TCP data to the binary parser vs the XML parser.
bool is_binary_frame(const uint8_t* buf, size_t len);

// How many bytes does the complete frame occupy?
// Returns 0 if there aren't enough bytes yet to determine the length.
// Returns SIZE_MAX if the header is recognised but the size field is out of range.
size_t binary_frame_total_size(const uint8_t* buf, size_t len);

// Serialise a parsed frame to JSON into caller-supplied buffer.
// Returns bytes written (excluding null-terminator), or -1 on overflow / error.
int binary_frame_to_json(const BinaryFrame& f, char* out, size_t cap);
