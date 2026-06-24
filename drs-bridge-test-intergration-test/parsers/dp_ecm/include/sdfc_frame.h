// drs-bridge/parsers/dp_ecm/include/sdfc_frame.h
//
// Family-A (SDFC<->DRS) frame constants + decoded header model + frame scanner.
// Shared by every Data Patterns variant DLL (DP-ECM HF/VU, RDFS, COMM DF).
//
// Byte layouts (authoritative, little-endian, no CRC):
//   COMMAND  : [hdr 4][size u32 @4][group u16 @8][unit u16 @10][payload N][ftr 4]   overhead 16
//   RESPONSE : [hdr 4][status i16 @4][size u32 @6][group u16 @10][unit u16 @12][payload N][ftr 4] overhead 18
//   STREAM   : [hdr 4][msg_num u32 @4][size u32 @8][payload N][ftr 4]               overhead 16
#ifndef SDFC_FRAME_H
#define SDFC_FRAME_H

#include <cstdint>

namespace sdfc {

enum FrameType : int {
    FRAME_INCOMPLETE = 0,
    FRAME_COMMAND    = 1,
    FRAME_RESPONSE   = 2,
    FRAME_STREAM     = 3,
    FRAME_CORRUPT    = -1,
};

// ---- Magic bytes ----
constexpr uint8_t CMD_HEADER[4]  = {0xAA, 0xAB, 0xBA, 0xBB};
constexpr uint8_t CMD_FOOTER[4]  = {0xCC, 0xCD, 0xDC, 0xDD};
constexpr uint8_t RESP_HEADER[4] = {0xEE, 0xEF, 0xFE, 0xFF};
constexpr uint8_t RESP_FOOTER[4] = {0xFF, 0xFE, 0xEF, 0xEE};
// Stream shares the response magics; disambiguated by socket/build, not bytes.

// ---- Fixed overheads ----
constexpr int CMD_OVERHEAD    = 16; // 4 hdr + 4 size + 2 group + 2 unit + 4 ftr
constexpr int RESP_OVERHEAD   = 18; // 4 hdr + 2 status + 4 size + 2 group + 2 unit + 4 ftr
constexpr int STREAM_OVERHEAD = 16; // 4 hdr + 4 msg_num + 4 size + 4 ftr
constexpr int MAGIC_LEN       = 4;
constexpr int MAX_PAYLOAD     = 1048576; // 1 MB

// ---- Field offsets (command) ----
constexpr int CMD_OFF_SIZE  = 4;
constexpr int CMD_OFF_GROUP = 8;
constexpr int CMD_OFF_UNIT  = 10;
constexpr int CMD_OFF_PAYLOAD = 12;

// ---- Field offsets (response) ----
constexpr int RESP_OFF_STATUS = 4;
constexpr int RESP_OFF_SIZE   = 6;
constexpr int RESP_OFF_GROUP  = 10;
constexpr int RESP_OFF_UNIT   = 12;
constexpr int RESP_OFF_PAYLOAD = 14;

// Decoded, endianness-resolved view of a frame header.
struct FrameHeader {
    int      frame_type   = FRAME_INCOMPLETE;
    uint16_t group_id     = 0;
    uint16_t unit_id      = 0;
    int16_t  status       = 0;    // responses only
    uint32_t payload_size = 0;    // payload bytes only
    int      payload_off  = 0;    // byte offset of payload within the frame
    int      total_len    = 0;    // full frame length incl. header+footer
};

// Decode the header of a complete frame (frame_type from extract_frame).
// Returns true on success. Does not validate footer (extract_frame already did).
bool decode_header(const uint8_t* frame, int frame_len, int frame_type, FrameHeader& out);

// Core frame scanner — see sdfc_abi.h extract_frame contract.
int extract_frame_core(const uint8_t* buf, int buf_len, uint8_t* out_frame, int* out_len);

} // namespace sdfc

#endif // SDFC_FRAME_H
