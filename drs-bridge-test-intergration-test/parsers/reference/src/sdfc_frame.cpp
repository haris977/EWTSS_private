// drs-bridge/parsers/dp_ecm/src/sdfc_frame.cpp
//
// Shared Family-A frame scanner + header decoder. No I/O, no allocation beyond
// the caller-provided out_frame buffer. Pure, testable byte logic.
#include "sdfc_frame.h"
#include "sdfc_endian.h"
#include "sdfc_abi.h"

#include <cstring>

namespace sdfc {

static bool magic_at(const uint8_t* p, const uint8_t (&magic)[4]) {
    return std::memcmp(p, magic, MAGIC_LEN) == 0;
}

bool decode_header(const uint8_t* frame, int frame_len, int frame_type, FrameHeader& out) {
    out = FrameHeader{};
    out.frame_type = frame_type;

    if (frame_type == FRAME_COMMAND) {
        if (frame_len < CMD_OVERHEAD) return false;
        out.payload_size = load_u32le(frame + CMD_OFF_SIZE);
        out.group_id     = load_u16le(frame + CMD_OFF_GROUP);
        out.unit_id      = load_u16le(frame + CMD_OFF_UNIT);
        out.payload_off  = CMD_OFF_PAYLOAD;
        out.total_len    = CMD_OVERHEAD + static_cast<int>(out.payload_size);
        return true;
    }
    if (frame_type == FRAME_RESPONSE) {
        if (frame_len < RESP_OVERHEAD) return false;
        out.status       = load_i16le(frame + RESP_OFF_STATUS);
        out.payload_size = load_u32le(frame + RESP_OFF_SIZE);
        out.group_id     = load_u16le(frame + RESP_OFF_GROUP);
        out.unit_id      = load_u16le(frame + RESP_OFF_UNIT);
        out.payload_off  = RESP_OFF_PAYLOAD;
        out.total_len    = RESP_OVERHEAD + static_cast<int>(out.payload_size);
        return true;
    }
    if (frame_type == FRAME_STREAM) {
        if (frame_len < STREAM_OVERHEAD) return false;
        // msg_number at offset 4, size at offset 8
        out.payload_size = load_u32le(frame + 8);
        out.payload_off  = 12;
        out.total_len    = STREAM_OVERHEAD + static_cast<int>(out.payload_size);
        return true;
    }
    return false;
}

// Try to frame as a given type starting at buf[0].
//   returns >0 frame type + sets *out_len when complete & footer-valid,
//           0 incomplete (need more bytes),
//          -1 corrupt (header matched but length/footer invalid).
static int try_frame(const uint8_t* buf, int buf_len,
                     int overhead, int size_off,
                     const uint8_t (&footer)[4],
                     int frame_type,
                     uint8_t* out_frame, int* out_len) {
    // Need enough bytes to read the size field.
    if (buf_len < size_off + 4) return FRAME_INCOMPLETE;

    uint32_t payload_size = load_u32le(buf + size_off);
    if (payload_size > static_cast<uint32_t>(MAX_PAYLOAD)) return FRAME_CORRUPT;

    int total = overhead + static_cast<int>(payload_size);
    if (total > MAX_FRAME_BUFFER_BYTES) return FRAME_CORRUPT;
    if (buf_len < total) return FRAME_INCOMPLETE; // wait for the rest

    // Footer must sit at the tail.
    const uint8_t* ftr = buf + (total - MAGIC_LEN);
    if (!magic_at(ftr, footer)) return FRAME_CORRUPT;

    std::memcpy(out_frame, buf, static_cast<size_t>(total));
    *out_len = total;
    return frame_type;
}

int extract_frame_core(const uint8_t* buf, int buf_len, uint8_t* out_frame, int* out_len) {
    *out_len = 0;
    if (!buf || buf_len < MAGIC_LEN) return FRAME_INCOMPLETE;

    // Increment-1 contract: the frame begins at buf[0] (clean line). The host
    // slices *out_len bytes after a successful return. Noise-tolerant resync
    // (scan forward past a bad candidate) is a documented later enhancement.
    if (magic_at(buf, CMD_HEADER)) {
        return try_frame(buf, buf_len, CMD_OVERHEAD, CMD_OFF_SIZE,
                         CMD_FOOTER, FRAME_COMMAND, out_frame, out_len);
    }
    if (magic_at(buf, RESP_HEADER)) {
        // Default to RESPONSE on the control plane. Stream-socket builds map the
        // same magic to FRAME_STREAM (handled by a dedicated entry point later).
        return try_frame(buf, buf_len, RESP_OVERHEAD, RESP_OFF_SIZE,
                         RESP_FOOTER, FRAME_RESPONSE, out_frame, out_len);
    }
    // No recognized header at buf[0].
    return FRAME_CORRUPT;
}

} // namespace sdfc
