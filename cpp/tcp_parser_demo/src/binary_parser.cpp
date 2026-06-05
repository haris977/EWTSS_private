#include "binary_parser.h"
#include <cstring>
#include <cstdio>
#include <climits>

// ── Frame magic constants (ICD §3) ───────────────────────────────────────────
static const uint8_t SDFC_RESP_HDR[4] = {0xEE, 0xEF, 0xFE, 0xFF}; // DRS → SDFC
static const uint8_t SDFC_RESP_FTR[4] = {0xFF, 0xFE, 0xEF, 0xEE};
static const uint8_t SDFC_CMD_HDR[4]  = {0xAA, 0xAB, 0xBA, 0xBB}; // SDFC → DRS
static const uint8_t SDFC_CMD_FTR[4]  = {0xCC, 0xCD, 0xDC, 0xDD};
static const uint8_t SCD_HDR[2]       = {0xAA, 0xAA};              // SCD compact
static const uint8_t SCD_FTR[2]       = {0xEE, 0xEE};

// Fixed overhead bytes (everything except the variable payload):
//   SDFC Response: 4(hdr)+2(status)+4(size)+2(grp)+2(unit)+4(ftr) = 18
//   SDFC Command:  4(hdr)+4(size)+2(grp)+2(unit)+4(ftr)            = 16
//   SCD Compact:   2(hdr)+2(code)+2(seq)+4(len)+2(ftr)             = 12
static constexpr size_t   RESP_OVERHEAD = 18;
static constexpr size_t   CMD_OVERHEAD  = 16;
static constexpr size_t   SCD_OVERHEAD  = 12;
static constexpr uint32_t MAX_PAYLOAD   = 1048576; // 1 MB per ICD §2

// ── Little-endian helpers ────────────────────────────────────────────────────
// The ICD specifies all multi-byte integers as little-endian.
// Never use memcpy + cast for multi-byte reads — that's undefined behaviour on
// platforms that don't support unaligned access and won't work if the TCP
// buffer is not aligned. Read byte-by-byte instead.

static inline uint16_t le16(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static inline int16_t le16s(const uint8_t* p) {
    return (int16_t)le16(p);
}
static inline uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0]
         | ((uint32_t)p[1] <<  8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

// ── Public: quick protocol detection ────────────────────────────────────────
bool is_binary_frame(const uint8_t* buf, size_t len) {
    if (!buf || len < 2) return false;
    // All three binary frame types start with 0xAA or 0xEE.
    // XML messages always start with '<' (0x3C).
    return (buf[0] == 0xAA || buf[0] == 0xEE);
}

// ── Public: determine total frame size ──────────────────────────────────────
size_t binary_frame_total_size(const uint8_t* buf, size_t len) {
    if (!buf || len < 2) return 0; // need at least 2 bytes to identify type

    if (len >= 4 && memcmp(buf, SDFC_RESP_HDR, 4) == 0) {
        if (len < 10) return 0; // need header(4)+status(2)+size(4) = 10
        uint32_t sz = le32(buf + 6);
        return (sz > MAX_PAYLOAD) ? SIZE_MAX : RESP_OVERHEAD + sz;
    }
    if (len >= 4 && memcmp(buf, SDFC_CMD_HDR, 4) == 0) {
        if (len < 8) return 0; // need header(4)+size(4) = 8
        uint32_t sz = le32(buf + 4);
        return (sz > MAX_PAYLOAD) ? SIZE_MAX : CMD_OVERHEAD + sz;
    }
    if (buf[0] == SCD_HDR[0] && buf[1] == SCD_HDR[1]) {
        if (len < 10) return 0; // need header(2)+code(2)+seq(2)+len(4) = 10
        uint32_t sz = le32(buf + 6);
        return (sz > MAX_PAYLOAD) ? SIZE_MAX : SCD_OVERHEAD + sz;
    }
    return SIZE_MAX; // unrecognised first bytes
}

// ── Public: parse a complete frame ──────────────────────────────────────────
bool parse_binary_frame(const uint8_t* buf, size_t len, BinaryFrame& out) {
    if (!buf || len < 2) return false;
    out = {};

    // ── SDFC Response (DRS → SDFC) ──────────────────────────────────────────
    // Layout: [hdr:4][status:2][size:4][group:2][unit:2][payload:size][ftr:4]
    if (len >= 4 && memcmp(buf, SDFC_RESP_HDR, 4) == 0) {
        if (len < RESP_OVERHEAD) return false;
        uint32_t sz       = le32(buf + 6);
        if (sz > MAX_PAYLOAD) return false;
        size_t   total    = RESP_OVERHEAD + sz;
        if (len < total) return false;
        if (memcmp(buf + total - 4, SDFC_RESP_FTR, 4) != 0) return false; // bad footer

        out.type        = BinaryFrameType::SdfcResponse;
        out.status      = le16s(buf + 4);
        out.group_id    = le16(buf + 10);
        out.unit_id     = le16(buf + 12);
        out.payload     = buf + 14; // payload starts right after the header fields
        out.payload_len = sz;
        return true;
    }

    // ── SDFC Command (SDFC → DRS) ────────────────────────────────────────────
    // Layout: [hdr:4][size:4][group:2][unit:2][payload:size][ftr:4]
    if (len >= 4 && memcmp(buf, SDFC_CMD_HDR, 4) == 0) {
        if (len < CMD_OVERHEAD) return false;
        uint32_t sz       = le32(buf + 4);
        if (sz > MAX_PAYLOAD) return false;
        size_t   total    = CMD_OVERHEAD + sz;
        if (len < total) return false;
        if (memcmp(buf + total - 4, SDFC_CMD_FTR, 4) != 0) return false;

        out.type        = BinaryFrameType::SdfcCommand;
        out.status      = 0;
        out.group_id    = le16(buf + 8);
        out.unit_id     = le16(buf + 10);
        out.payload     = buf + 12;
        out.payload_len = sz;
        return true;
    }

    // ── SCD Compact (second byte == 0xAA distinguishes from SDFC command) ───
    // Layout: [hdr:2][cmd_code:2][seq:2][length:4][data:length][ftr:2]
    if (buf[0] == SCD_HDR[0] && buf[1] == SCD_HDR[1]) {
        if (len < SCD_OVERHEAD) return false;
        uint32_t sz       = le32(buf + 6);
        if (sz > MAX_PAYLOAD) return false;
        size_t   total    = SCD_OVERHEAD + sz;
        if (len < total) return false;
        if (memcmp(buf + total - 2, SCD_FTR, 2) != 0) return false;

        out.type        = BinaryFrameType::ScdCompact;
        out.status      = 0;
        out.group_id    = le16(buf + 2); // SCD has no group/unit split — store cmd_code in group_id
        out.unit_id     = le16(buf + 4); // seq number stored in unit_id for convenience
        out.payload     = buf + 10;
        out.payload_len = sz;
        return true;
    }

    return false;
}

// ── JSON helpers ─────────────────────────────────────────────────────────────

// Append hex bytes into out[pos..cap]. Returns new pos.
static size_t append_hex(const uint8_t* data, size_t data_len,
                          char* out, size_t pos, size_t cap) {
    for (size_t i = 0; i < data_len && pos + 3 < cap; ++i) {
        int n = snprintf(out + pos, cap - pos, "%02X", data[i]);
        if (n > 0) pos += (size_t)n;
    }
    return pos;
}

// ── Payload decoders ──────────────────────────────────────────────────────────

// Group 100, Unit 2: System Version response (26-byte payload, ICD §4.4)
static int decode_system_version(const uint8_t* p, uint32_t plen,
                                  char* out, size_t cap) {
    if (plen < 26) return -1;
    uint32_t fw   = le32(p + 0);
    uint32_t drv  = le32(p + 4);
    uint32_t fpga = le32(p + 8);
    uint32_t bsp  = le32(p + 12);
    uint16_t proc = le16(p + 16);
    uint16_t rf0  = le16(p + 18);
    uint16_t rf1  = le16(p + 20);
    uint16_t rf2  = le16(p + 22);
    uint16_t ftype = le16(p + 24);
    // Version fields are packed uint32; major.minor.patch encoding is ICD-revision-specific.
    // Outputting raw values here — update this to extract major/minor/patch once the
    // version-packing scheme is confirmed in your ICD revision.
    return snprintf(out, cap,
        "\"fw_version\":%u,\"driver_version\":%u,\"fpga_version\":%u,"
        "\"bsp_version\":%u,\"processor_id\":%u,"
        "\"rf_tuner_ids\":[%u,%u,%u],\"fpga_type_id\":%u",
        fw, drv, fpga, bsp, proc, rf0, rf1, rf2, ftype);
}

// Group 101, Unit 84: Burst Detection response
// Payload layout: [burst_count:4][burst_count × 52 bytes] (ICD §4.2)
// 52-byte burst record — field offsets are ICD-confirmed below:
//   [0..7]   start_us      uint64 LE
//   [8..11]  duration_us   uint32 LE
//   [12..15] frequency_hz  uint32 LE
//   [16..19] power_dbm     float32 IEEE 754 LE
//   [20..23] bandwidth_hz  uint32 LE
//   [24..51] reserved
static int decode_burst_detection(const uint8_t* p, uint32_t plen,
                                   char* out, size_t cap) {
    if (plen < 4) return -1;
    uint32_t burst_count = le32(p);
    if (plen < 4u + burst_count * 52u) return -1;

    size_t pos = 0;
    int    n   = snprintf(out + pos, cap - pos,
                          "\"burst_count\":%u,\"bursts\":[", burst_count);
    if (n <= 0) return -1; pos += (size_t)n;

    for (uint32_t i = 0; i < burst_count && pos < cap; ++i) {
        const uint8_t* b = p + 4 + i * 52;

        // 8-byte LE uint64 for start timestamp
        uint64_t start_us = (uint64_t)b[0]        | ((uint64_t)b[1] << 8)
                          | ((uint64_t)b[2] << 16) | ((uint64_t)b[3] << 24)
                          | ((uint64_t)b[4] << 32) | ((uint64_t)b[5] << 40)
                          | ((uint64_t)b[6] << 48) | ((uint64_t)b[7] << 56);
        uint32_t dur_us  = le32(b + 8);
        uint32_t freq_hz = le32(b + 12);

        // Reinterpret 4 raw bytes as IEEE 754 float32 — safe via memcpy
        uint32_t pow_bits = le32(b + 16);
        float    power_dbm;
        memcpy(&power_dbm, &pow_bits, sizeof(float));

        uint32_t bw_hz = le32(b + 20);

        n = snprintf(out + pos, cap - pos,
            "%s{\"start_us\":%llu,\"duration_us\":%u,"
            "\"frequency_hz\":%u,\"power_dbm\":%.2f,\"bandwidth_hz\":%u}",
            (i == 0 ? "" : ","),
            (unsigned long long)start_us, dur_us, freq_hz, (double)power_dbm, bw_hz);
        if (n > 0) pos += (size_t)n;
    }
    if (pos < cap) out[pos++] = ']';
    if (pos < cap) out[pos]   = '\0';
    return (int)pos;
}

// ── Public: frame → JSON ─────────────────────────────────────────────────────
int binary_frame_to_json(const BinaryFrame& f, char* out, size_t cap) {
    if (!out || cap < 64) return -1;

    const char* type_str =
        f.type == BinaryFrameType::SdfcResponse ? "sdfc_response" :
        f.type == BinaryFrameType::SdfcCommand  ? "sdfc_command"  :
        f.type == BinaryFrameType::ScdCompact   ? "scd_compact"   : "unknown";

    size_t pos = 0;
    int    n;

    // Common header fields
    if (f.type == BinaryFrameType::SdfcResponse) {
        n = snprintf(out + pos, cap - pos,
            "{\"frame_type\":\"%s\",\"group_id\":%u,\"unit_id\":%u,\"status\":%d",
            type_str, f.group_id, f.unit_id, f.status);
    } else if (f.type == BinaryFrameType::SdfcCommand) {
        n = snprintf(out + pos, cap - pos,
            "{\"frame_type\":\"%s\",\"group_id\":%u,\"unit_id\":%u",
            type_str, f.group_id, f.unit_id);
    } else {
        // SCD: group_id holds cmd_code, unit_id holds sequence number
        n = snprintf(out + pos, cap - pos,
            "{\"frame_type\":\"%s\",\"cmd_code\":%u,\"seq\":%u",
            type_str, f.group_id, f.unit_id);
    }
    if (n <= 0) return -1; pos += (size_t)n;

    // Decode known payload shapes; fall back to hex for unknown ones.
    // Add new cases here as you implement more ICD command groups.
    char extra[2048] = {};
    int  extra_len   = 0;

    if (f.type == BinaryFrameType::SdfcResponse) {
        if      (f.group_id == 100 && f.unit_id == 2)  // System Version
            extra_len = decode_system_version(f.payload, f.payload_len, extra, sizeof(extra));
        else if (f.group_id == 101 && f.unit_id == 84) // Burst Detection
            extra_len = decode_burst_detection(f.payload, f.payload_len, extra, sizeof(extra));
        // TODO: add group 101 unit 40 (FH Detection) and unit 44 (FFT) once ICD confirms offsets
    }

    if (extra_len > 0) {
        n = snprintf(out + pos, cap - pos, ",%s", extra);
        if (n > 0) pos += (size_t)n;
    }

    // Always include a hex preview of the first 16 payload bytes for debugging.
    // Remove this in production if payload is large (e.g. FFT = 6404 bytes).
    size_t preview = f.payload_len < 16 ? f.payload_len : 16;
    n = snprintf(out + pos, cap - pos,
        ",\"payload_len\":%u,\"payload_preview\":\"", f.payload_len);
    if (n > 0) pos += (size_t)n;
    pos = append_hex(f.payload, preview, out, pos, cap);
    n = snprintf(out + pos, cap - pos, "\"}");
    if (n > 0) pos += (size_t)n;

    if (pos < cap) out[pos] = '\0';
    return (int)pos;
}
