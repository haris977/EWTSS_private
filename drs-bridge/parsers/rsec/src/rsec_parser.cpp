// drs-bridge/parsers/rsec/src/rsec_parser.cpp
//
// HIMSHAKTI RSEC parser — DRS-Bridge Layer 2
// IRS: DLRL/HIMSHAKTI/RSEC/2025/IRS, Version 1.0, Date 21-07-2025
// Device: Radar Segment Entity Controller (RSEC)
//
// *** ENDIANNESS WARNING ***
// IRS v1.0 does not explicitly state byte order for Variant A/B frames.
// ASSUMPTION: BIG-ENDIAN (network byte order), based on DLRL convention.
// MUST confirm with ESMP integration team before live integration.
// This file uses be_ helpers throughout; flip to le_ if confirmed otherwise.
//
// Frame type return values (extract_frame):
//   1 = Variant A — main protocol (ESMP/ECMP/CC), SOM 0xAAAA
//   2 = Variant B — BB Rx / RFPS, SOM 0xAAABBABB
//   3 = Variant C — SCU servo, SOF 0x24 (non-NMEA)
//   4 = Variant D — GNSS NMEA ASCII, starts with '$' + 'G'/'B'/'P'
//   0 = incomplete frame, need more data
//  -1 = corrupt frame (header matched, length/footer invalid)

#include "sdfc_abi.h"
#include "sdfc_endian.h"
#include "json_writer.h"

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <string>

// ---------------------------------------------------------------------------
// Big-endian read helpers (Variant A/B assumed big-endian per DLRL convention)
// ---------------------------------------------------------------------------

static inline uint16_t load_be16(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

static inline int16_t load_be16s(const uint8_t* p) {
    return static_cast<int16_t>(load_be16(p));
}

static inline uint32_t load_be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) <<  8) |
            static_cast<uint32_t>(p[3]);
}

static inline void store_be16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[1] = static_cast<uint8_t>( v       & 0xFF);
}

static inline void store_be32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>((v >> 24) & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[2] = static_cast<uint8_t>((v >>  8) & 0xFF);
    p[3] = static_cast<uint8_t>( v        & 0xFF);
}

// ---------------------------------------------------------------------------
// Frame constants
// ---------------------------------------------------------------------------

// Variant A
static constexpr uint16_t VA_SOM = 0xAAAAu;
static constexpr uint16_t VA_EOM = 0xEEEEu;
static constexpr int      VA_OVERHEAD = 12; // 2(SOM)+2(cmd)+2(seq)+4(bodylen)+2(EOM)

// Variant B
// SOM bytes: AA AB BA BB  => stored as uint32 big-endian = 0xAAABBABB
static constexpr uint8_t VB_SOM_B0 = 0xAA;
static constexpr uint8_t VB_SOM_B1 = 0xAB;
static constexpr uint8_t VB_SOM_B2 = 0xBA;
static constexpr uint8_t VB_SOM_B3 = 0xBB;
// EOM bytes: CC CD DC DD
static constexpr uint8_t VB_EOM_B0 = 0xCC;
static constexpr uint8_t VB_EOM_B1 = 0xCD;
static constexpr uint8_t VB_EOM_B2 = 0xDC;
static constexpr uint8_t VB_EOM_B3 = 0xDD;
static constexpr uint16_t VB_CMD_GROUP = 0x0064u; // always 100
static constexpr int      VB_OVERHEAD = 16; // 4(SOM)+4(bodylen)+2(group)+2(uid)+4(EOM)

// Variant C (SCU)
static constexpr uint8_t VC_SOF = 0x24u; // '$'
static constexpr uint8_t VC_EOF = 0x0Du; // CR

// Variant D (GNSS NMEA) also starts with '$' but second byte is 'G'/'B'/'P'

// Sanity cap — largest expected frame
static constexpr uint32_t MAX_BODY_LEN = (1u << 20); // 1 MB

// ---------------------------------------------------------------------------
// SCU XOR checksum
// Covers DataLen byte through last Data byte (NOT including SOF/EOF/checksum)
// ---------------------------------------------------------------------------

static uint8_t scu_checksum(const uint8_t* buf, int from, int to_exclusive) {
    uint8_t xr = 0;
    for (int i = from; i < to_exclusive; ++i) xr ^= buf[i];
    return xr;
}

// ---------------------------------------------------------------------------
// extract_frame — Variant A
// Frame layout: [SOM 2B][CmdCode 2B][SeqNo 2B][BodyLen 4B][Body N B][EOM 2B]
// Returns bytes consumed into out_frame or 0/−1.
// ---------------------------------------------------------------------------

static int try_extract_variant_a(const uint8_t* buf, int len,
                                  uint8_t* out_frame, int* out_len) {
    if (len < 2) return 0;
    if (load_be16(buf) != VA_SOM) return -2; // not Variant A

    if (len < VA_OVERHEAD) return 0; // need more bytes

    uint32_t body_len = load_be32(buf + 6);
    if (body_len > MAX_BODY_LEN) return -1; // sanity check

    int total = VA_OVERHEAD + static_cast<int>(body_len);
    if (len < total) return 0; // incomplete

    // Validate EOM
    uint16_t eom = load_be16(buf + total - 2);
    if (eom != VA_EOM) return -1;

    if (total > MAX_FRAME_BUFFER_BYTES) return -1;
    std::memcpy(out_frame, buf, static_cast<size_t>(total));
    *out_len = total;
    return 1;
}

// ---------------------------------------------------------------------------
// extract_frame — Variant B
// Frame layout: [SOM 4B][BodyLen 4B][CmdGroup 2B][CmdUnitID 2B][Body N B][EOM 4B]
// ---------------------------------------------------------------------------

static int try_extract_variant_b(const uint8_t* buf, int len,
                                  uint8_t* out_frame, int* out_len) {
    if (len < 4) return 0;
    if (buf[0] != VB_SOM_B0 || buf[1] != VB_SOM_B1 ||
        buf[2] != VB_SOM_B2 || buf[3] != VB_SOM_B3) return -2;

    if (len < VB_OVERHEAD) return 0;

    uint32_t body_len = load_be32(buf + 4);
    if (body_len > MAX_BODY_LEN) return -1;

    int total = VB_OVERHEAD + static_cast<int>(body_len);
    if (len < total) return 0;

    // Validate EOM
    const uint8_t* eom = buf + total - 4;
    if (eom[0] != VB_EOM_B0 || eom[1] != VB_EOM_B1 ||
        eom[2] != VB_EOM_B2 || eom[3] != VB_EOM_B3) return -1;

    if (total > MAX_FRAME_BUFFER_BYTES) return -1;
    std::memcpy(out_frame, buf, static_cast<size_t>(total));
    *out_len = total;
    return 2;
}

// ---------------------------------------------------------------------------
// extract_frame — Variant C (SCU servo)
// Frame layout: [0x24][DataLen][CmdCode][Data N B][XOR_Checksum][0x0D]
// DataLen = 1(CmdCode) + N(data bytes)
// Checksum = XOR( buf[1..1+DataLen] ) i.e. DataLen byte through last data byte
// ---------------------------------------------------------------------------

static int try_extract_variant_c(const uint8_t* buf, int len,
                                  uint8_t* out_frame, int* out_len) {
    if (len < 2) return 0;
    if (buf[0] != VC_SOF) return -2;

    // Distinguish from Variant D (NMEA '$G'/'$P'/'$B')
    uint8_t b1 = buf[1];
    if (b1 == 'G' || b1 == 'g' || b1 == 'P' || b1 == 'p' || b1 == 'B' || b1 == 'b')
        return -2; // It's a NMEA sentence, not SCU

    uint8_t data_len = buf[1]; // includes CmdCode byte
    if (data_len == 0) return -1; // DataLen=0 is invalid (must include at least CmdCode)

    int total = 1 + 1 + data_len + 1 + 1; // SOF + DataLen + CmdCode+Data + Checksum + EOF
    if (len < total) return 0;

    // Validate EOF
    if (buf[total - 1] != VC_EOF) return -1;

    // Validate checksum: XOR of buf[1] (DataLen byte) through buf[1+DataLen] (last data byte)
    // Checksum byte sits at buf[2 + data_len]; EOF at buf[3 + data_len].
    uint8_t expected = scu_checksum(buf, 1, 2 + static_cast<int>(data_len));
    uint8_t actual   = buf[2 + data_len];
    if (expected != actual) return -1;

    if (total > MAX_FRAME_BUFFER_BYTES) return -1;
    std::memcpy(out_frame, buf, static_cast<size_t>(total));
    *out_len = total;
    return 3;
}

// ---------------------------------------------------------------------------
// extract_frame — Variant D (GNSS NMEA ASCII)
// Format: $TTMMM,field1,...,fieldN*CS\r\n
// Ends with \r\n (0x0D 0x0A).
// ---------------------------------------------------------------------------

static int try_extract_variant_d(const uint8_t* buf, int len,
                                  uint8_t* out_frame, int* out_len) {
    if (len < 2) return 0;
    if (buf[0] != '$') return -2;

    // Must be a GNSS talker (GP, GL, GN, GA, GB, etc.)
    uint8_t b1 = buf[1];
    bool is_gnss = (b1 == 'G' || b1 == 'g' || b1 == 'P' || b1 == 'p' || b1 == 'B' || b1 == 'b');
    if (!is_gnss) return -2;

    // Scan for \r\n terminator
    for (int i = 2; i < len - 1; ++i) {
        if (buf[i] == 0x0D && buf[i + 1] == 0x0A) {
            int total = i + 2;
            if (total > MAX_FRAME_BUFFER_BYTES) return -1;
            std::memcpy(out_frame, buf, static_cast<size_t>(total));
            *out_len = total;
            return 4;
        }
    }
    return 0; // not yet complete
}

// ---------------------------------------------------------------------------
// extract_frame — ABI entry point
// Tries Variant A first (most common), then B, then C, then D.
// If buf[0..1] is 0xAAAA → Variant A; 0xAAAB... → B; 0x24+letter → D; 0x24+byte → C.
// ---------------------------------------------------------------------------

SDFC_EXPORT int extract_frame(const uint8_t* buf, size_t buf_len,
                               uint8_t** out_frame, size_t* out_len) {
    if (!buf || buf_len == 0 || !out_frame || !out_len) return -1;

    // Temporary caller-buffer pattern: use a local stack buffer for the
    // try_extract_variant_* helpers (which still use the old int/ptr API),
    // then copy the result into a malloc'd block.
    static thread_local uint8_t tmp[MAX_FRAME_BUFFER_BYTES];
    int tmp_len = 0;
    int ibuf    = static_cast<int>(buf_len);
    int result  = -1;

    if (ibuf >= 2 && load_be16(buf) == VA_SOM) {
        int r = try_extract_variant_a(buf, ibuf, tmp, &tmp_len);
        if (r != -2) { result = r; goto done; }
    }

    if (ibuf >= 4 && buf[0] == VB_SOM_B0 && buf[1] == VB_SOM_B1) {
        int r = try_extract_variant_b(buf, ibuf, tmp, &tmp_len);
        if (r != -2) { result = r; goto done; }
    }

    if (ibuf >= 1 && buf[0] == VC_SOF) {
        if (ibuf >= 2) {
            uint8_t b1 = buf[1];
            if (b1 == 'G' || b1 == 'g' || b1 == 'P' || b1 == 'p' || b1 == 'B' || b1 == 'b') {
                int r = try_extract_variant_d(buf, ibuf, tmp, &tmp_len);
                if (r != -2) { result = r; goto done; }
            } else {
                int r = try_extract_variant_c(buf, ibuf, tmp, &tmp_len);
                if (r != -2) { result = r; goto done; }
            }
        } else {
            return -1; // need one more byte to decide
        }
    }

done:
    if (result <= 0 || tmp_len <= 0) return -1;
    auto* p = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(tmp_len)));
    if (!p) return -1;
    std::memcpy(p, tmp, static_cast<size_t>(tmp_len));
    *out_frame = p;
    *out_len   = static_cast<size_t>(tmp_len);
    return 0;
}

// ===========================================================================
// PARSE HELPERS
// ===========================================================================

static std::string hex_dump(const uint8_t* p, int n, int max_bytes = 64) {
    int cap = std::min(n, max_bytes);
    std::string s;
    s.reserve(static_cast<size_t>(cap) * 3);
    char tmp[4];
    for (int i = 0; i < cap; ++i) {
        std::snprintf(tmp, sizeof(tmp), "%02X ", p[i]);
        s += tmp;
    }
    if (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

// ---------------------------------------------------------------------------
// JPRO decoder — 24 bytes packed structure
// Layout (IRS §5.x, ECMP interface):
//   Bytes 0–1:  DRFM Mode (UINT16 BE)
//   Byte  2:    num_responses (UINT8) — 1 to 3
//   Bytes 3–9:  Technique 1 (7 bytes): JammingType(1B) + FreqMHz×10(4B BE) + Params(2B BE)
//   Bytes 10–16: Technique 2 (7 bytes)
//   Bytes 17–23: Technique 3 (7 bytes)
// ---------------------------------------------------------------------------

static std::string decode_jpro(const uint8_t* p, int avail) {
    if (avail < 24) return "\"jpro_truncated\"";

    uint16_t drfm_mode    = load_be16(p + 0);
    uint8_t  num_resp     = p[2];

    // Build JSON array of techniques
    std::string tech_arr = "[";
    int count = std::min(static_cast<int>(num_resp), 3);
    for (int i = 0; i < count; ++i) {
        const uint8_t* t = p + 3 + i * 7;
        uint8_t  jtype  = t[0];
        uint32_t raw_f  = load_be32(t + 1); // frequency in MHz×10
        uint16_t params = load_be16(t + 5);
        double   freq_mhz = raw_f / 10.0;

        char entry[128];
        std::snprintf(entry, sizeof(entry),
            "%s{\"jamming_type\":%u,\"freq_mhz\":%.1f,\"params\":%u}",
            (i > 0 ? "," : ""), jtype, freq_mhz, params);
        tech_arr += entry;
    }
    tech_arr += "]";

    char out[256];
    std::snprintf(out, sizeof(out),
        "{\"drfm_mode\":%u,\"num_responses\":%u,\"techniques\":%s}",
        drfm_mode, num_resp, tech_arr.c_str());
    return out;
}

// ---------------------------------------------------------------------------
// Warner Library record decoder — 53 bytes per record (IRS §5.2)
// Layout:
//   0:    RecordNumber  (UINT8)
//   1:    NextModeRecordNumber (UINT8) — 0 = end of chain
//   2:    RecordType    (UINT8)
//   3:    EmitterCategory (UINT8)
//   4–7:  FreqLowKHz    (UINT32 BE)
//   8–11: FreqHighKHz   (UINT32 BE)
//   12–15: PRILowUsec   (UINT32 BE)
//   16–19: PRIHighUsec  (UINT32 BE)
//   20–23: PWLowNsec    (UINT32 BE)
//   24–27: PWHighNsec   (UINT32 BE)
//   28–31: ScanLowMs    (UINT32 BE)
//   32–35: ScanHighMs   (UINT32 BE)
//   36:   JammingMode   (UINT8)
//   37:   RESERVED      (1B padding to align JPRO)
//   38–61: JPRO         (24 bytes)
//   Total: 62 bytes
// Note: If IRS does not include RESERVED byte, total = 61 bytes.
//       Using 62 here; adjust per confirmed IRS table.
// ---------------------------------------------------------------------------

static constexpr int WARNER_RECORD_SIZE = 62;

static std::string decode_warner_record(const uint8_t* p, int avail) {
    if (avail < WARNER_RECORD_SIZE) {
        char err[64];
        std::snprintf(err, sizeof(err),
            "{\"error\":\"record_truncated\",\"avail\":%d}", avail);
        return err;
    }

    uint8_t  rec_no        = p[0];
    uint8_t  next_rec_no   = p[1];
    uint8_t  rec_type      = p[2];
    uint8_t  emitter_cat   = p[3];
    uint32_t freq_low_khz  = load_be32(p + 4);
    uint32_t freq_high_khz = load_be32(p + 8);
    uint32_t pri_low_us    = load_be32(p + 12);
    uint32_t pri_high_us   = load_be32(p + 16);
    uint32_t pw_low_ns     = load_be32(p + 20);
    uint32_t pw_high_ns    = load_be32(p + 24);
    uint32_t scan_low_ms   = load_be32(p + 28);
    uint32_t scan_high_ms  = load_be32(p + 32);
    uint8_t  jamming_mode  = p[36];
    // p[37] = reserved
    std::string jpro = decode_jpro(p + 38, avail - 38);

    char out[512];
    std::snprintf(out, sizeof(out),
        "{\"record_no\":%u,\"next_mode_no\":%u,\"rec_type\":%u,"
        "\"emitter_cat\":%u,\"freq_low_khz\":%u,\"freq_high_khz\":%u,"
        "\"pri_low_us\":%u,\"pri_high_us\":%u,"
        "\"pw_low_ns\":%u,\"pw_high_ns\":%u,"
        "\"scan_low_ms\":%u,\"scan_high_ms\":%u,"
        "\"jamming_mode\":%u,\"jpro\":%s}",
        rec_no, next_rec_no, rec_type, emitter_cat,
        freq_low_khz, freq_high_khz,
        pri_low_us, pri_high_us,
        pw_low_ns, pw_high_ns,
        scan_low_ms, scan_high_ms,
        jamming_mode, jpro.c_str());
    return out;
}

// ===========================================================================
// VARIANT A MESSAGE PARSERS
// Body starts at frame offset 10 (after SOM+CmdCode+SeqNo+BodyLen).
// ===========================================================================

// ---- ESMP → RSEC: Active Track Data (0x1502) — periodic 1 Hz, NEVER ACK ----
// Body layout (IRS §5.2, Table — Active Track):
//   0:   SystemMode   (UINT8): current ESMP operating mode
//   1:   NumTracks    (UINT8): number of track entries (0–N)
//   Per track (24 bytes each):
//     0–1:   TrackID       (UINT16 BE): 1–500 ESM-correlated, 501–550 manual
//     2–3:   DOA           (INT16 BE): tenths of degrees, ÷10 = degrees (0.0°–359.9°)
//     4–7:   FreqKHz       (UINT32 BE): frequency in KHz
//     8–11:  PRIUsec       (UINT32 BE): PRI in microseconds
//     12–15: PWNsec        (UINT32 BE): pulse width in nanoseconds
//     16–19: ScanPeriodMs  (UINT32 BE): scan period in milliseconds
//     20–21: AmplitudedBm  (INT16 BE): amplitude in dBm × 1 (direct dBm)
//     22:    TrackStatus   (UINT8): bit flags — b0=active, b1=manual, b2=jamming
//     23:    EmitterCat    (UINT8): emitter category code

static constexpr int ACTIVE_TRACK_ENTRY_SIZE = 24;

static void parse_active_track(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 2) { w.key_str("parse_error", "body_too_short"); return; }

    uint8_t sys_mode   = body[0];
    uint8_t num_tracks = body[1];
    w.key_int("system_mode",   sys_mode);
    w.key_int("num_tracks",    num_tracks);

    if (num_tracks == 0) return;

    int expected = 2 + num_tracks * ACTIVE_TRACK_ENTRY_SIZE;
    if (body_len < expected) {
        w.key_str("parse_warning", "body_shorter_than_expected");
    }

    std::string arr = "[";
    int max_parse = std::min(static_cast<int>(num_tracks),
                             (body_len - 2) / ACTIVE_TRACK_ENTRY_SIZE);
    for (int i = 0; i < max_parse; ++i) {
        const uint8_t* e = body + 2 + i * ACTIVE_TRACK_ENTRY_SIZE;
        uint16_t track_id  = load_be16(e + 0);
        int16_t  doa_raw   = load_be16s(e + 2);
        uint32_t freq_khz  = load_be32(e + 4);
        uint32_t pri_us    = load_be32(e + 8);
        uint32_t pw_ns     = load_be32(e + 12);
        uint32_t scan_ms   = load_be32(e + 16);
        int16_t  amp_dbm   = load_be16s(e + 20);
        uint8_t  status    = e[22];
        uint8_t  emit_cat  = e[23];

        double doa_deg   = doa_raw / 10.0;
        double freq_mhz  = freq_khz / 1000.0;
        bool   is_manual = (track_id >= 501 && track_id <= 550);

        char entry[256];
        std::snprintf(entry, sizeof(entry),
            "%s{\"track_id\":%u,\"doa_deg\":%.1f,\"freq_mhz\":%.3f,"
            "\"pri_us\":%u,\"pw_ns\":%u,\"scan_ms\":%u,"
            "\"amplitude_dbm\":%d,\"status\":\"0x%02X\","
            "\"emitter_cat\":%u,\"manual\":%s}",
            (i > 0 ? "," : ""),
            track_id, doa_deg, freq_mhz,
            pri_us, pw_ns, scan_ms,
            static_cast<int>(amp_dbm), status,
            emit_cat, is_manual ? "true" : "false");
        arr += entry;
    }
    arr += "]";
    w.key_raw("tracks", arr);
}

// ---- ESMP → RSEC: Operational Data (0x1504) — periodic 1 Hz, NEVER ACK ----
// Body layout (IRS §5.2):
//   0:    ESMPStatus     (UINT8): 0=Init, 1=Operational, 2=Fault, 3=Standby
//   1:    NumActiveTracks (UINT8)
//   2:    NumManualTracks (UINT8)
//   3:    ScanStatus     (UINT8): 0=Idle, 1=Scanning, 2=Directed
//   4–5:  ScanBandIndex  (UINT16 BE): currently scanning band
//   6:    HWStatus       (UINT8): hardware status flags
//   7:    ReservedOpData (UINT8): reserved

static void parse_operational_data(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 8) { w.key_str("parse_error", "body_too_short"); return; }

    static const char* esmp_status_str[] = {"init", "operational", "fault", "standby"};
    static const char* scan_status_str[] = {"idle", "scanning", "directed", "unknown"};

    uint8_t  esmp_status  = body[0];
    uint8_t  num_active   = body[1];
    uint8_t  num_manual   = body[2];
    uint8_t  scan_status  = body[3];
    uint16_t scan_band    = load_be16(body + 4);
    uint8_t  hw_status    = body[6];

    const char* esmp_str = (esmp_status < 4) ? esmp_status_str[esmp_status] : "unknown";
    const char* scan_str = (scan_status < 3) ? scan_status_str[scan_status] : "unknown";

    w.key_str("esmp_status",         esmp_str);
    w.key_int("esmp_status_code",    esmp_status);
    w.key_int("num_active_tracks",   num_active);
    w.key_int("num_manual_tracks",   num_manual);
    w.key_str("scan_status",         scan_str);
    w.key_int("scan_band_index",     scan_band);
    w.key_str("hw_status_hex",       hex_dump(&hw_status, 1));
}

// ---- ESMP → RSEC: Purge Response (0x1505) ----
// Body layout: Result(UINT8): 0=success, 1=fail, 2=track_not_found

static void parse_purge_response(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 1) { w.key_str("parse_error", "body_too_short"); return; }
    uint8_t result = body[0];
    w.key_int("result", result);
    w.key_str("result_text",
        result == 0 ? "success" :
        result == 1 ? "fail" :
        result == 2 ? "track_not_found" : "unknown");
}

// ---- ESMP → RSEC: NACK (0x1509) ----
// Body layout: ErrorCode(UINT8): reason for rejection

static void parse_nack(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 1) { w.key_str("parse_error", "body_too_short"); return; }
    uint8_t err = body[0];
    w.key_int("error_code", err);
    w.key_str("error_text",
        err == 0 ? "ok" :
        err == 1 ? "invalid_cmd" :
        err == 2 ? "seq_error" :
        err == 3 ? "busy" :
        err == 4 ? "param_out_of_range" : "unknown");
}

// ---- ESMP → RSEC: ACK (0x150A) ----
// Body: empty or optional 1-byte status

static void parse_ack(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len >= 1)
        w.key_int("ack_status", body[0]);
    w.key_str("ack", "ok");
}

// ---- RSEC → ESMP: Time Data (0x1002) ----
// Body layout (IRS §5.2):
//   0–1:  Year         (UINT16 BE)
//   2:    Month        (UINT8): 1–12
//   3:    Day          (UINT8): 1–31
//   4:    Hour         (UINT8): 0–23
//   5:    Minute       (UINT8): 0–59
//   6:    Second       (UINT8): 0–59
//   7–8:  Millisecond  (UINT16 BE): 0–999

static void parse_time_data(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 9) { w.key_str("parse_error", "body_too_short"); return; }
    uint16_t yr  = load_be16(body + 0);
    uint8_t  mo  = body[2];
    uint8_t  dy  = body[3];
    uint8_t  hr  = body[4];
    uint8_t  mn  = body[5];
    uint8_t  sc  = body[6];
    uint16_t ms  = load_be16(body + 7);

    char ts[32];
    std::snprintf(ts, sizeof(ts), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
        yr, mo, dy, hr, mn, sc, ms);
    w.key_str("timestamp_utc", ts);
}

// ---- RSEC → ESMP: Load Warner Library (0x1003) ----
// Body layout:
//   0:    NumRecords (UINT8): number of records following
//   1+:   Records (WARNER_RECORD_SIZE bytes each)
// Note: ESMP stops periodic messages (0x1502/0x1504) during load.

static void parse_load_warner(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 1) { w.key_str("parse_error", "body_too_short"); return; }
    uint8_t num_records = body[0];
    w.key_int("num_records", num_records);

    if (num_records == 0) return;

    std::string arr = "[";
    int offset = 1;
    int count  = 0;
    while (count < num_records && (offset + WARNER_RECORD_SIZE) <= body_len) {
        if (count > 0) arr += ",";
        arr += decode_warner_record(body + offset, body_len - offset);
        offset += WARNER_RECORD_SIZE;
        ++count;
    }
    arr += "]";
    w.key_raw("records", arr);
    if (count < num_records)
        w.key_str("parse_warning", "body_truncated_fewer_records_than_declared");
}

// ---- RSEC → ESMP: Delete Warner (0x1004) ----
// Body: RecordNumber(UINT8), 0=delete all

static void parse_delete_warner(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 1) { w.key_str("parse_error", "body_too_short"); return; }
    w.key_int("record_number", body[0]);
    w.key_str("action", body[0] == 0 ? "delete_all" : "delete_one");
}

// ---- RSEC → ESMP: Lockout Frequency Bands (0x1005) ----
// Body: NumBands(UINT8) + N×[FreqLowKHz(UINT32) + FreqHighKHz(UINT32)]

static void parse_lockout_bands(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 1) { w.key_str("parse_error", "body_too_short"); return; }
    uint8_t num_bands = body[0];
    w.key_int("num_bands", num_bands);

    std::string arr = "[";
    for (int i = 0; i < num_bands && (1 + i * 8 + 8) <= body_len; ++i) {
        const uint8_t* b = body + 1 + i * 8;
        uint32_t lo = load_be32(b + 0);
        uint32_t hi = load_be32(b + 4);
        char entry[80];
        std::snprintf(entry, sizeof(entry),
            "%s{\"freq_low_khz\":%u,\"freq_high_khz\":%u}", (i > 0 ? "," : ""), lo, hi);
        arr += entry;
    }
    arr += "]";
    w.key_raw("bands", arr);
}

// ---- RSEC → ESMP: Lockout Sectors (0x1006) ----
// Body: NumSectors(UINT8) + N×[StartDOA(UINT16 ÷10=deg) + EndDOA(UINT16 ÷10=deg)]

static void parse_lockout_sectors(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 1) { w.key_str("parse_error", "body_too_short"); return; }
    uint8_t num_sec = body[0];
    w.key_int("num_sectors", num_sec);

    std::string arr = "[";
    for (int i = 0; i < num_sec && (1 + i * 4 + 4) <= body_len; ++i) {
        const uint8_t* s = body + 1 + i * 4;
        double start_deg = load_be16(s + 0) / 10.0;
        double end_deg   = load_be16(s + 2) / 10.0;
        char entry[80];
        std::snprintf(entry, sizeof(entry),
            "%s{\"start_deg\":%.1f,\"end_deg\":%.1f}", (i > 0 ? "," : ""), start_deg, end_deg);
        arr += entry;
    }
    arr += "]";
    w.key_raw("sectors", arr);
}

// ---- RSEC → ESMP: Auto Purge (0x1008) ----
// Body: Enable(UINT8): 0=disable, 1=enable; TimeoutSec(UINT8)

static void parse_auto_purge(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 2) { w.key_str("parse_error", "body_too_short"); return; }
    w.key_bool("enabled",         body[0] != 0);
    w.key_int("timeout_sec",      body[1]);
}

// ---- RSEC → ESMP: Purge Passive Track (0x1010) ----
// Body: TrackID(UINT16)

static void parse_purge_passive(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 2) { w.key_str("parse_error", "body_too_short"); return; }
    w.key_int("track_id", load_be16(body));
}

// ---- RSEC → ESMP: Platform Heading (0x1014) ----
// Body: Heading(UINT16): tenths of degrees, ÷10 = degrees (0.0°–359.9°)

static void parse_platform_heading(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 2) { w.key_str("parse_error", "body_too_short"); return; }
    w.key_double("heading_deg", load_be16(body) / 10.0);
}

// ---- RSEC → ESMP: Set Scan Bands (0x1118) ----
// Body: NumBands(UINT8) + N×[FreqLowKHz(UINT32) + FreqHighKHz(UINT32)]

static void parse_set_scan_bands(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 1) { w.key_str("parse_error", "body_too_short"); return; }
    uint8_t num_bands = body[0];
    w.key_int("num_bands", num_bands);

    std::string arr = "[";
    for (int i = 0; i < num_bands && (1 + i * 8 + 8) <= body_len; ++i) {
        const uint8_t* b = body + 1 + i * 8;
        uint32_t lo = load_be32(b + 0);
        uint32_t hi = load_be32(b + 4);
        char entry[80];
        std::snprintf(entry, sizeof(entry),
            "%s{\"freq_low_khz\":%u,\"freq_high_khz\":%u}", (i > 0 ? "," : ""), lo, hi);
        arr += entry;
    }
    arr += "]";
    w.key_raw("bands", arr);
}

// ---- RSEC → ESMP: Directed Search (0x1114) ----
// Body: FreqCentreKHz(UINT32) + BandwidthKHz(UINT32) + DOA(UINT16 ÷10) + SectorWidth(UINT16 ÷10)

static void parse_directed_search(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 12) { w.key_str("parse_error", "body_too_short"); return; }
    w.key_uint("centre_freq_khz",  load_be32(body + 0));
    w.key_uint("bandwidth_khz",    load_be32(body + 4));
    w.key_double("doa_deg",        load_be16(body + 8) / 10.0);
    w.key_double("sector_width_deg", load_be16(body + 10) / 10.0);
}

// ---- RSEC → ECMP: Semi-Auto Track (0x1101) ----
// Body: TrackID(UINT16)

static void parse_ecmp_semi_track(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 2) { w.key_str("parse_error", "body_too_short"); return; }
    w.key_int("track_id", load_be16(body));
}

// ---- RSEC → ECMP: Semi-Auto Jam (0x1102) ----
// Body: TrackID(UINT16) + JPRO(24B)

static void parse_ecmp_semi_jam(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 2) { w.key_str("parse_error", "body_too_short"); return; }
    w.key_int("track_id", load_be16(body));
    if (body_len >= 26)
        w.key_raw("jpro", decode_jpro(body + 2, body_len - 2));
    else
        w.key_str("parse_warning", "jpro_data_missing_or_truncated");
}

// ---- RSEC → ECMP: Semi-Auto Track & Jam (0x1103) ----
// Body: TrackID(UINT16) + JPRO(24B)

static void parse_ecmp_track_and_jam(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    parse_ecmp_semi_jam(body, body_len, w); // identical layout
}

// ---- RSEC → ECMP: Break Track (0x1104) ----
// Body: TrackID(UINT16)

static void parse_ecmp_break_track(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 2) { w.key_str("parse_error", "body_too_short"); return; }
    w.key_int("track_id", load_be16(body));
}

// ---- RSEC → ECMP: Stop Jam (0x1105) ----
// Body: TrackID(UINT16)

static void parse_ecmp_stop_jam(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 2) { w.key_str("parse_error", "body_too_short"); return; }
    w.key_int("track_id", load_be16(body));
}

// ---- RSEC → ECMP: Change JPRO (0x1106) ----
// Body: TrackID(UINT16) + NewJPRO(24B)

static void parse_ecmp_change_jpro(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    parse_ecmp_semi_jam(body, body_len, w); // identical layout
}

// ---- ECMP → RSEC: EA Operational Data Semi (0x1153) ----
// Body layout (IRS §5.3):
//   0–1:  TrackID       (UINT16 BE): track being jammed
//   2–3:  DRFMMode      (UINT16 BE): active DRFM mode
//   4:    EAStatus      (UINT8): 0=idle, 1=tracking, 2=jamming, 3=fault
//   5:    RFPowerdBm    (INT8): transmit power in dBm
//   6–7:  FreqMHz×10    (UINT16 BE): jammer centre freq, ÷10 = MHz
//   8:    TechniqueIndex (UINT8): active technique 1–3
//   9:    FaultCode      (UINT8): 0=none

static void parse_ea_operational_data(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 10) { w.key_str("parse_error", "body_too_short"); return; }

    static const char* ea_status_str[] = {"idle", "tracking", "jamming", "fault"};

    uint16_t track_id   = load_be16(body + 0);
    uint16_t drfm_mode  = load_be16(body + 2);
    uint8_t  ea_status  = body[4];
    int8_t   rf_power   = static_cast<int8_t>(body[5]);
    uint16_t freq_raw   = load_be16(body + 6);
    uint8_t  tech_idx   = body[8];
    uint8_t  fault_code = body[9];

    const char* ea_str = (ea_status < 4) ? ea_status_str[ea_status] : "unknown";

    w.key_int("track_id",      track_id);
    w.key_int("drfm_mode",     drfm_mode);
    w.key_str("ea_status",     ea_str);
    w.key_int("ea_status_code", ea_status);
    w.key_int("rf_power_dbm",  rf_power);
    w.key_double("freq_mhz",   freq_raw / 10.0);
    w.key_int("technique_idx", tech_idx);
    w.key_int("fault_code",    fault_code);
}

// ---- RSEC → ECMP Manual: Start Manual EA (0x6001) ----
// Body: Frequency(UINT32 MHz BE) + JPRO(24B)

static void parse_ecmp_manual_start(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 4) { w.key_str("parse_error", "body_too_short"); return; }
    w.key_uint("freq_mhz", load_be32(body + 0));
    if (body_len >= 28)
        w.key_raw("jpro", decode_jpro(body + 4, body_len - 4));
}

// ---- Top-level Variant A dispatcher ----

static const char* parse_variant_a_frame(const uint8_t* frame, int frame_len) {
    // Frame: [0xAAAA 2B][CmdCode 2B][SeqNo 2B][BodyLen 4B][Body][0xEEEE 2B]
    if (frame_len < VA_OVERHEAD) return nullptr;

    uint16_t cmd_code = load_be16(frame + 2);
    uint16_t seq_no   = load_be16(frame + 4);
    uint32_t body_len = load_be32(frame + 6);
    const uint8_t* body = frame + 10;

    sdfc::JsonWriter w;
    w.key_int("frame_variant", 1);
    w.key_int("cmd_code",      cmd_code);
    char cmd_hex[8];
    std::snprintf(cmd_hex, sizeof(cmd_hex), "0x%04X", cmd_code);
    w.key_str("cmd_code_hex",  cmd_hex);
    w.key_int("seq_no",        seq_no);
    w.key_uint("body_len",     body_len);

    int blen = static_cast<int>(body_len);

    switch (cmd_code) {
        // ESMP periodic (never ACK)
        case 0x1502: w.key_str("msg_type", "active_track_data");    parse_active_track(body, blen, w);       break;
        case 0x1504: w.key_str("msg_type", "operational_data");     parse_operational_data(body, blen, w);   break;
        // ESMP aperiodic responses
        case 0x1505: w.key_str("msg_type", "purge_response");       parse_purge_response(body, blen, w);     break;
        case 0x1509: w.key_str("msg_type", "nack");                 parse_nack(body, blen, w);               break;
        case 0x150A: w.key_str("msg_type", "ack");                  parse_ack(body, blen, w);                break;
        // RSEC → ESMP commands
        case 0x100F: w.key_str("msg_type", "restart_emitter_processing");                                    break;
        case 0x1002: w.key_str("msg_type", "time_data");            parse_time_data(body, blen, w);          break;
        case 0x1003: w.key_str("msg_type", "load_warner_library");  parse_load_warner(body, blen, w);        break;
        case 0x1004: w.key_str("msg_type", "delete_warner");        parse_delete_warner(body, blen, w);      break;
        case 0x1005: w.key_str("msg_type", "lockout_freq_bands");   parse_lockout_bands(body, blen, w);      break;
        case 0x1006: w.key_str("msg_type", "lockout_sectors");      parse_lockout_sectors(body, blen, w);    break;
        case 0x1008: w.key_str("msg_type", "auto_purge");           parse_auto_purge(body, blen, w);         break;
        case 0x1010: w.key_str("msg_type", "purge_passive_track");  parse_purge_passive(body, blen, w);      break;
        case 0x1011: w.key_str("msg_type", "purge_all");                                                     break;
        case 0x1014: w.key_str("msg_type", "platform_heading");     parse_platform_heading(body, blen, w);   break;
        case 0x1118: w.key_str("msg_type", "set_scan_bands");       parse_set_scan_bands(body, blen, w);     break;
        case 0x1114: w.key_str("msg_type", "directed_search");      parse_directed_search(body, blen, w);    break;
        // RSEC → ECMP semi-auto
        case 0x1101: w.key_str("msg_type", "semi_auto_track");      parse_ecmp_semi_track(body, blen, w);    break;
        case 0x1102: w.key_str("msg_type", "semi_auto_jam");        parse_ecmp_semi_jam(body, blen, w);      break;
        case 0x1103: w.key_str("msg_type", "semi_auto_track_jam");  parse_ecmp_track_and_jam(body, blen, w); break;
        case 0x1104: w.key_str("msg_type", "break_track");          parse_ecmp_break_track(body, blen, w);   break;
        case 0x1105: w.key_str("msg_type", "stop_jam");             parse_ecmp_stop_jam(body, blen, w);      break;
        case 0x1106: w.key_str("msg_type", "change_jpro");          parse_ecmp_change_jpro(body, blen, w);   break;
        // ECMP → RSEC
        case 0x1153: w.key_str("msg_type", "ea_operational_data");  parse_ea_operational_data(body, blen, w); break;
        // RSEC → ECMP manual
        case 0x6001: w.key_str("msg_type", "ecmp_manual_start");    parse_ecmp_manual_start(body, blen, w);   break;
        case 0x0FA4: w.key_str("msg_type", "ecmp_manual_0x0FA4");
            w.key_str("raw_body_hex", hex_dump(body, blen));                                                  break;
        case 0x0FA5: w.key_str("msg_type", "ecmp_manual_0x0FA5");
            w.key_str("raw_body_hex", hex_dump(body, blen));                                                  break;
        case 0x0FA6: w.key_str("msg_type", "ecmp_manual_0x0FA6");
            w.key_str("raw_body_hex", hex_dump(body, blen));                                                  break;
        case 0x0FA7: w.key_str("msg_type", "ecmp_manual_0x0FA7");
            w.key_str("raw_body_hex", hex_dump(body, blen));                                                  break;
        case 0x0FB0: w.key_str("msg_type", "ecmp_manual_0x0FB0");
            w.key_str("raw_body_hex", hex_dump(body, blen));                                                  break;
        case 0x1120: w.key_str("msg_type", "ecmp_manual_0x1120");
            w.key_str("raw_body_hex", hex_dump(body, blen));                                                  break;
        case 0x1121: w.key_str("msg_type", "ecmp_manual_0x1121");
            w.key_str("raw_body_hex", hex_dump(body, blen));                                                  break;
        case 0x111D: w.key_str("msg_type", "ecmp_manual_0x111D");
            w.key_str("raw_body_hex", hex_dump(body, blen));                                                  break;
        default:
            w.key_str("msg_type", "unknown");
            if (blen > 0)
                w.key_str("raw_body_hex", hex_dump(body, blen));
            break;
    }

    std::string json = w.str();
    char* out = static_cast<char*>(std::malloc(json.size() + 1));
    if (!out) return nullptr;
    std::memcpy(out, json.c_str(), json.size() + 1);
    return out;
}

// ===========================================================================
// VARIANT B MESSAGE PARSERS
// Body starts at frame offset 12 (after SOM+BodyLen+CmdGroup+CmdUnitID).
// CmdGroup is always 0x0064; CmdUnitID is the actual command.
// ===========================================================================

// ---- RSEC → BB Rx: SFB Selection (CmdUnitID=0x1126) ----
// Body: NumBands(UINT8) + N×[FreqLow(UINT32 MHz BE) + FreqHigh(UINT32 MHz BE)]

static void parse_bb_sfb_selection(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 1) { w.key_str("parse_error", "body_too_short"); return; }
    uint8_t num = body[0];
    w.key_int("num_bands", num);

    std::string arr = "[";
    for (int i = 0; i < num && (1 + i * 8 + 8) <= body_len; ++i) {
        const uint8_t* b = body + 1 + i * 8;
        char entry[80];
        std::snprintf(entry, sizeof(entry),
            "%s{\"freq_low_mhz\":%u,\"freq_high_mhz\":%u}",
            (i > 0 ? "," : ""), load_be32(b), load_be32(b + 4));
        arr += entry;
    }
    arr += "]";
    w.key_raw("bands", arr);
}

// ---- RSEC → BB Rx: RF Sector Blank (CmdUnitID=0x1127) ----
// Body: NumSectors(UINT8) + N×[StartDOA(UINT16 ÷10) + EndDOA(UINT16 ÷10)]

static void parse_bb_sector_blank(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 1) { w.key_str("parse_error", "body_too_short"); return; }
    uint8_t num = body[0];
    w.key_int("num_sectors", num);

    std::string arr = "[";
    for (int i = 0; i < num && (1 + i * 4 + 4) <= body_len; ++i) {
        const uint8_t* s = body + 1 + i * 4;
        char entry[80];
        std::snprintf(entry, sizeof(entry),
            "%s{\"start_deg\":%.1f,\"end_deg\":%.1f}",
            (i > 0 ? "," : ""),
            load_be16(s + 0) / 10.0, load_be16(s + 2) / 10.0);
        arr += entry;
    }
    arr += "]";
    w.key_raw("sectors", arr);
}

// ---- RSEC → BB Rx: CAL ON/OFF (CmdUnitID=0x1128) ----
// Body: Enable(UINT8): 0=off, 1=on

static void parse_bb_cal(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 1) { w.key_str("parse_error", "body_too_short"); return; }
    w.key_bool("calibration_enabled", body[0] != 0);
}

// ---- RSEC → RFPS: IQ Data Logging (CmdUnitID=0x1009) ----
// Body: Enable(UINT8) + FreqMHz(UINT32) + Duration(UINT32 ms)

static void parse_rfps_iq_logging(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 9) { w.key_str("parse_error", "body_too_short"); return; }
    w.key_bool("enabled",       body[0] != 0);
    w.key_uint("freq_mhz",      load_be32(body + 1));
    w.key_uint("duration_ms",   load_be32(body + 5));
}

// ---- RFPS → RSEC: IQ Logging Response (CmdUnitID=0x1076) ----
// Body: Result(UINT8): 0=success, NumSamples(UINT32)

static void parse_rfps_iq_response(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 5) { w.key_str("parse_error", "body_too_short"); return; }
    w.key_int("result",       body[0]);
    w.key_uint("num_samples", load_be32(body + 1));
}

// ---- RFPS → RSEC: Finger Printing Response (CmdUnitID=0x3507 = 13575) ----
// Body: MatchConfidence(UINT8 0–100) + EmitterID(UINT16) + ModelName(16B ASCII null-pad)

static void parse_rfps_fingerprint_response(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 3) { w.key_str("parse_error", "body_too_short"); return; }
    uint8_t  confidence = body[0];
    uint16_t emitter_id = load_be16(body + 1);
    w.key_int("match_confidence_pct", confidence);
    w.key_int("emitter_id",           emitter_id);
    if (body_len >= 19) {
        char name[17] = {};
        std::memcpy(name, body + 3, 16);
        name[16] = '\0';
        w.key_str("model_name", name);
    }
}

// ---- Top-level Variant B dispatcher ----

static const char* parse_variant_b_frame(const uint8_t* frame, int frame_len) {
    if (frame_len < VB_OVERHEAD) return nullptr;

    uint32_t body_len  = load_be32(frame + 4);
    uint16_t cmd_group = load_be16(frame + 8);
    uint16_t cmd_uid   = load_be16(frame + 10);
    const uint8_t* body = frame + 12;
    int blen = static_cast<int>(body_len);

    sdfc::JsonWriter w;
    w.key_int("frame_variant", 2);
    w.key_int("cmd_group",     cmd_group);
    w.key_int("cmd_uid",       cmd_uid);
    char uid_hex[8];
    std::snprintf(uid_hex, sizeof(uid_hex), "0x%04X", cmd_uid);
    w.key_str("cmd_uid_hex",   uid_hex);
    w.key_uint("body_len",     body_len);

    switch (cmd_uid) {
        case 0x1126: w.key_str("msg_type", "bb_sfb_selection");         parse_bb_sfb_selection(body, blen, w);         break;
        case 0x1127: w.key_str("msg_type", "bb_rf_sector_blank");       parse_bb_sector_blank(body, blen, w);          break;
        case 0x1128: w.key_str("msg_type", "bb_cal_onoff");             parse_bb_cal(body, blen, w);                   break;
        case 0x1009: w.key_str("msg_type", "rfps_iq_data_logging");     parse_rfps_iq_logging(body, blen, w);          break;
        case 0x1076: w.key_str("msg_type", "rfps_iq_logging_response"); parse_rfps_iq_response(body, blen, w);         break;
        case 0x3507: w.key_str("msg_type", "rfps_fingerprint_response"); parse_rfps_fingerprint_response(body, blen, w); break;
        default:
            w.key_str("msg_type", "unknown");
            if (blen > 0) w.key_str("raw_body_hex", hex_dump(body, blen));
            break;
    }

    std::string json = w.str();
    char* out = static_cast<char*>(std::malloc(json.size() + 1));
    if (!out) return nullptr;
    std::memcpy(out, json.c_str(), json.size() + 1);
    return out;
}

// ===========================================================================
// VARIANT C — SCU SERVO MESSAGES
// Frame: [0x24][DataLen][CmdCode][Data N B][Checksum][0x0D]
// DataLen = 1(CmdCode) + N  →  N = DataLen - 1
// ===========================================================================

// SCU CmdCode definitions (single-byte, RSEC→SCU direction)
// Notation 0x5001 in IRS = subsystem-prefix 0x50 + command 0x01; wire byte is 0x01
static constexpr uint8_t SCU_CMD_POSITION_SLEW = 0x01;
static constexpr uint8_t SCU_CMD_SPIN          = 0x02;
static constexpr uint8_t SCU_CMD_SECTOR_SCAN   = 0x03;
static constexpr uint8_t SCU_CMD_STANDBY       = 0x04;
static constexpr uint8_t SCU_CMD_BIT           = 0x05;
static constexpr uint8_t SCU_CMD_STOP          = 0x06;
static constexpr uint8_t SCU_CMD_FEEDBACK      = 0xA0; // SCU→RSEC continuous feedback

static const char* parse_variant_c_frame(const uint8_t* frame, int frame_len) {
    if (frame_len < 5) return nullptr; // minimum: SOF+DataLen+CmdCode+Checksum+EOF

    uint8_t data_len = frame[1]; // includes CmdCode
    uint8_t cmd_code = frame[2];
    const uint8_t* data = frame + 3;         // data payload (DataLen-1 bytes)
    int n = static_cast<int>(data_len) - 1;  // payload byte count

    sdfc::JsonWriter w;
    w.key_int("frame_variant", 3);
    w.key_int("cmd_code",      cmd_code);
    char cmd_hex[6];
    std::snprintf(cmd_hex, sizeof(cmd_hex), "0x%02X", cmd_code);
    w.key_str("cmd_code_hex",  cmd_hex);
    w.key_int("data_len",      data_len);

    switch (cmd_code) {
        case SCU_CMD_POSITION_SLEW:
            w.key_str("msg_type", "scu_position_slew");
            // Data: Azimuth(UINT16 BE ÷10=deg) + Elevation(INT16 BE ÷10=deg)
            if (n >= 4) {
                w.key_double("azimuth_deg",   load_be16(data + 0) / 10.0);
                w.key_double("elevation_deg", load_be16s(data + 2) / 10.0);
            }
            break;

        case SCU_CMD_SPIN:
            w.key_str("msg_type", "scu_spin");
            // Data: SpeedRPM(UINT16 BE): rotational speed
            if (n >= 2)
                w.key_int("speed_rpm", load_be16(data));
            break;

        case SCU_CMD_SECTOR_SCAN:
            w.key_str("msg_type", "scu_sector_scan");
            // Data: ScanStart(UINT16 ÷10) + ScanEnd(UINT16 ÷10) + ScanSpeed(UINT16 BE RPM)
            if (n >= 6) {
                w.key_double("scan_start_deg", load_be16(data + 0) / 10.0);
                w.key_double("scan_end_deg",   load_be16(data + 2) / 10.0);
                w.key_int("scan_speed_rpm",    load_be16(data + 4));
            }
            break;

        case SCU_CMD_STANDBY:
            w.key_str("msg_type", "scu_standby");
            break;

        case SCU_CMD_BIT:
            w.key_str("msg_type", "scu_bit");
            break;

        case SCU_CMD_STOP:
            w.key_str("msg_type", "scu_stop");
            break;

        case SCU_CMD_FEEDBACK:
            w.key_str("msg_type", "scu_continuous_feedback");
            // Data: Azimuth(UINT16 ÷10) + Elevation(INT16 ÷10) + Status(UINT8)
            if (n >= 5) {
                w.key_double("azimuth_deg",   load_be16(data + 0) / 10.0);
                w.key_double("elevation_deg", load_be16s(data + 2) / 10.0);
                w.key_int("servo_status",     data[4]);
            }
            break;

        default:
            w.key_str("msg_type", "scu_unknown");
            if (n > 0) w.key_str("raw_data_hex", hex_dump(data, n));
            break;
    }

    std::string json = w.str();
    char* out = static_cast<char*>(std::malloc(json.size() + 1));
    if (!out) return nullptr;
    std::memcpy(out, json.c_str(), json.size() + 1);
    return out;
}

// ===========================================================================
// VARIANT D — GNSS NMEA ASCII
// Sentences: $GPRMC, $GPGGA, $GPHDT, $GLRMC, $GNRMC
// Format: $TTMMM,f1,f2,...,fN*CS\r\n
// ===========================================================================

// Extract Nth comma-separated field (0-indexed after the talker ID)
static std::string nmea_field(const char* sentence, int n) {
    int field = 0;
    const char* p = sentence;
    while (*p && *p != ',') ++p; // skip talker+sentence type
    while (*p) {
        if (*p == ',') {
            if (field == n) {
                ++p;
                const char* start = p;
                while (*p && *p != ',' && *p != '*' && *p != '\r' && *p != '\n') ++p;
                return std::string(start, static_cast<size_t>(p - start));
            }
            ++field;
        }
        ++p;
    }
    return "";
}

// Convert NMEA lat/lon DDDMM.MMMM to decimal degrees
static double nmea_to_decimal(const std::string& val, char hemi) {
    if (val.empty()) return 0.0;
    double raw = std::stod(val);
    int deg    = static_cast<int>(raw / 100);
    double min = raw - deg * 100.0;
    double dd  = deg + min / 60.0;
    if (hemi == 'S' || hemi == 'W') dd = -dd;
    return dd;
}

static const char* parse_variant_d_frame(const uint8_t* frame, int frame_len) {
    if (frame_len < 6) return nullptr;

    // Null-terminate a working copy (safe since frame_len <= MAX_FRAME_BUFFER_BYTES)
    std::string raw(reinterpret_cast<const char*>(frame),
                    static_cast<size_t>(frame_len));
    const char* s = raw.c_str();

    // Extract sentence type identifier (e.g. "GPRMC", "GPGGA", "GPHDT", "GNRMC", "GLRMC")
    char type_str[8] = {};
    {
        const char* start = s + 1; // skip '$'
        int i = 0;
        while (*start && *start != ',' && i < 6) {
            type_str[i++] = *start++;
        }
    }

    sdfc::JsonWriter w;
    w.key_int("frame_variant", 4);
    w.key_str("sentence_type", type_str);

    // $xxRMC — Recommended Minimum
    bool is_rmc = (type_str[2] == 'R' && type_str[3] == 'M' && type_str[4] == 'C');
    // $xxGGA — Fix data
    bool is_gga = (type_str[2] == 'G' && type_str[3] == 'G' && type_str[4] == 'A');
    // $xxHDT — Heading True
    bool is_hdt = (type_str[2] == 'H' && type_str[3] == 'D' && type_str[4] == 'T');

    if (is_rmc) {
        // $GPRMC,hhmmss.ss,A,DDDMM.MM,N,DDDMM.MM,E,knots,true_course,DDMMYY,mag_var,E*CS
        w.key_str("msg_type", "gnss_rmc");
        std::string utc    = nmea_field(s, 0); // hhmmss.ss
        std::string status = nmea_field(s, 1); // A=active, V=void
        std::string lat    = nmea_field(s, 2);
        std::string ns     = nmea_field(s, 3); // N/S
        std::string lon    = nmea_field(s, 4);
        std::string ew     = nmea_field(s, 5); // E/W
        std::string speed  = nmea_field(s, 6); // knots
        std::string course = nmea_field(s, 7); // true course (degrees)
        std::string date   = nmea_field(s, 8); // DDMMYY

        w.key_str("utc_time",        utc);
        w.key_str("status",          status);
        w.key_str("date",            date);
        if (!lat.empty() && !ns.empty())
            w.key_double("latitude_deg",  nmea_to_decimal(lat, ns.empty() ? 'N' : ns[0]));
        if (!lon.empty() && !ew.empty())
            w.key_double("longitude_deg", nmea_to_decimal(lon, ew.empty() ? 'E' : ew[0]));
        if (!speed.empty())  w.key_double("speed_knots",   std::stod(speed));
        if (!course.empty()) w.key_double("true_course_deg", std::stod(course));

    } else if (is_gga) {
        // $GPGGA,hhmmss.ss,lat,N,lon,E,quality,num_sv,hdop,alt,M,geo_sep,M,,*CS
        w.key_str("msg_type", "gnss_gga");
        std::string utc     = nmea_field(s, 0);
        std::string lat     = nmea_field(s, 1);
        std::string ns      = nmea_field(s, 2);
        std::string lon     = nmea_field(s, 3);
        std::string ew      = nmea_field(s, 4);
        std::string quality = nmea_field(s, 5); // 0=invalid, 1=GPS, 2=DGPS
        std::string num_sv  = nmea_field(s, 6);
        std::string hdop    = nmea_field(s, 7);
        std::string alt     = nmea_field(s, 8); // metres above MSL

        w.key_str("utc_time",    utc);
        if (!lat.empty() && !ns.empty())
            w.key_double("latitude_deg",  nmea_to_decimal(lat, ns.empty() ? 'N' : ns[0]));
        if (!lon.empty() && !ew.empty())
            w.key_double("longitude_deg", nmea_to_decimal(lon, ew.empty() ? 'E' : ew[0]));
        if (!quality.empty()) w.key_int("fix_quality",      std::stoi(quality));
        if (!num_sv.empty())  w.key_int("num_satellites",   std::stoi(num_sv));
        if (!hdop.empty())    w.key_double("hdop",          std::stod(hdop));
        if (!alt.empty())     w.key_double("altitude_m",    std::stod(alt));

    } else if (is_hdt) {
        // $GPHDT,heading_deg,T*CS
        w.key_str("msg_type", "gnss_hdt");
        std::string hdg = nmea_field(s, 0);
        if (!hdg.empty())
            w.key_double("heading_deg_true", std::stod(hdg));

    } else {
        w.key_str("msg_type", "gnss_other");
        w.key_str("raw_sentence", raw.substr(0, static_cast<size_t>(
            std::min(frame_len, 128))));
    }

    std::string json = w.str();
    char* out = static_cast<char*>(std::malloc(json.size() + 1));
    if (!out) return nullptr;
    std::memcpy(out, json.c_str(), json.size() + 1);
    return out;
}

// ===========================================================================
// ABI EXPORTS
// ===========================================================================

SDFC_EXPORT int parse_message(const uint8_t* frame, size_t frame_len,
                               char** out_json, size_t* out_len) {
    if (!frame || frame_len == 0 || !out_json || !out_len) return -1;

    int iframe = static_cast<int>(frame_len);
    char* raw  = nullptr;
    try {
        if (iframe >= 2 && load_be16(frame) == VA_SOM) {
            raw = parse_variant_a_frame(frame, iframe);
        } else if (iframe >= 4 && frame[0] == VB_SOM_B0 && frame[1] == VB_SOM_B1) {
            raw = parse_variant_b_frame(frame, iframe);
        } else if (iframe >= 2 && frame[0] == VC_SOF) {
            uint8_t b1 = frame[1];
            if (b1 == 'G' || b1 == 'g' || b1 == 'P' || b1 == 'p' || b1 == 'B' || b1 == 'b')
                raw = parse_variant_d_frame(frame, iframe);
            else
                raw = parse_variant_c_frame(frame, iframe);
        }
    } catch (...) {
        return -1;
    }

    if (!raw) return -1;
    *out_json = raw;
    *out_len  = std::strlen(raw);
    return 0;
}

// ---------------------------------------------------------------------------
// format_response — Builds a Variant A outbound frame from JSON description.
// Expected JSON keys:
//   "cmd_code"    (int, required): command code e.g. 0x150A for ACK
//   "seq_no"      (int, optional, default 0): sequence number
//   "payload_hex" (string, optional): hex-encoded body bytes, e.g. "00 AA BB"
//
// Writes into out_frame (must be >= MAX_FRAME_BUFFER_BYTES).
// Returns bytes written, or -1 on error.
// ---------------------------------------------------------------------------

SDFC_EXPORT int format_response(const char* /*kind*/, const char* kwargs_json,
                                 uint8_t** out_buf, size_t* out_len) {
    if (!kwargs_json || !out_buf || !out_len) return -1;

    // Minimal JSON key extractor — no external dependency
    auto find_int = [](const char* json, const char* key, long long fallback) -> long long {
        char search[64];
        std::snprintf(search, sizeof(search), "\"%s\":", key);
        const char* p = std::strstr(json, search);
        if (!p) return fallback;
        p += std::strlen(search);
        while (*p == ' ') ++p;
        char* end;
        // Accept hex (0x...) and decimal
        long long v = std::strtoll(p, &end, 0);
        return (end > p) ? v : fallback;
    };

    auto find_str = [](const char* json, const char* key,
                       char* out_buf, int buf_sz) -> bool {
        char search[64];
        std::snprintf(search, sizeof(search), "\"%s\":\"", key);
        const char* p = std::strstr(json, search);
        if (!p) return false;
        p += std::strlen(search);
        int i = 0;
        while (*p && *p != '"' && i < buf_sz - 1)
            out_buf[i++] = *p++;
        out_buf[i] = '\0';
        return i > 0;
    };

    long long cmd_code_ll = find_int(kwargs_json, "cmd_code", -1);
    if (cmd_code_ll < 0 || cmd_code_ll > 0xFFFF) return -1;
    uint16_t cmd_code = static_cast<uint16_t>(cmd_code_ll);
    uint16_t seq_no   = static_cast<uint16_t>(find_int(kwargs_json, "seq_no", 0));

    // Decode payload_hex into bytes
    uint8_t payload[4096] = {};
    int payload_len = 0;
    {
        char hex_str[8192] = {};
        if (find_str(kwargs_json, "payload_hex", hex_str, sizeof(hex_str))) {
            const char* p = hex_str;
            while (*p && payload_len < static_cast<int>(sizeof(payload))) {
                while (*p == ' ') ++p;
                if (!*p) break;
                char byte_str[3] = {p[0], (p[1] ? p[1] : '\0'), '\0'};
                char* end;
                long long bval = std::strtoll(byte_str, &end, 16);
                if (end == byte_str) break;
                payload[payload_len++] = static_cast<uint8_t>(bval);
                p += (p[1] ? 2 : 1);
            }
        }
    }

    int total = VA_OVERHEAD + payload_len;
    if (total > MAX_FRAME_BUFFER_BYTES) return -1;

    auto* frame = static_cast<uint8_t*>(std::malloc((size_t)total));
    if (!frame) return -1;

    // Build Variant A frame
    store_be16(frame + 0, VA_SOM);
    store_be16(frame + 2, cmd_code);
    store_be16(frame + 4, seq_no);
    store_be32(frame + 6, static_cast<uint32_t>(payload_len));
    if (payload_len > 0)
        std::memcpy(frame + 10, payload, static_cast<size_t>(payload_len));
    store_be16(frame + 10 + payload_len, VA_EOM);

    *out_buf = frame;
    *out_len = (size_t)total;
    return 0;
}

SDFC_EXPORT void free_result(void* ptr) {
    std::free(ptr);
}
