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
// extract_frame return values (matches the canonical sdfc_abi.h contract):
//   0  = success, a complete frame was copied to *out_frame/*out_len
//  -1  = no complete frame available (incomplete, corrupt, or no header matched)
// Frame variant (A/B/C/D) is NOT encoded in this return value — it is
// re-detected from magic bytes inside parse_message() and reported there as
// the "frame_variant" JSON field (1=A, 2=B, 3=C, 4=D).

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
// JPRO decoder — 24 bytes packed structure (IRS §5.5.1.1, pp.43-47)
// Layout:
//   Bytes 0-1 (Byte1&Byte2): DRFM Reception & Transmission Mode, bit-encoded
//                            UINT16 BE (validation flags b0-5, recall mode
//                            b6-8, store/recall mode b9-10, IBW b11-12,
//                            reserved b13-15).
//   Byte  2   (Byte3):       NumResponses (1-3; +32 if repeat-on-completion).
//   Bytes 3-9, 10-16, 17-23: up to 3 response blocks, 7 bytes each:
//     +0 Technique        (UINT8): 1-16 jamming technique / 32,64,128 angle-deception
//     +1 TechniqueValue   (UINT8): meaning depends on Technique (BW×3or10MHz,
//                                  start-dwell×250ms, etc. — Byte5 in IRS)
//     +2 NumCycles        (UINT8): technique duration/cycles (Byte6)
//     +3 VelocityProfile  (UINT8): Byte7
//     +4 EndRangeOrDoppler(UINT8): Byte8
//     +5 SubTypeCode      (UINT8): Blink/SRM/SSRM or false-target subtype (Byte9)
//     +6 NonLinearProfile (UINT8): Byte10
// Total = 2 + 1 + 3*7 = 24 bytes.
// ---------------------------------------------------------------------------

static std::string decode_jpro(const uint8_t* p, int avail) {
    if (avail < 24) return "\"jpro_truncated\"";

    uint16_t drfm_mode = load_be16(p + 0);
    uint8_t  resp_byte = p[2];
    bool     repeat     = (resp_byte & 0x20) != 0; // +32 = repeat flag
    uint8_t  num_resp    = resp_byte & 0x1F;

    std::string tech_arr = "[";
    int count = std::min(static_cast<int>(num_resp), 3);
    for (int i = 0; i < count; ++i) {
        const uint8_t* t = p + 3 + i * 7;
        uint8_t technique         = t[0];
        uint8_t technique_value   = t[1];
        uint8_t num_cycles        = t[2];
        uint8_t velocity_profile  = t[3];
        uint8_t end_range_doppler = t[4];
        uint8_t subtype_code      = t[5];
        uint8_t non_linear_profile = t[6];

        sdfc::JsonWriter entry_w;
        entry_w.key_int("technique",           technique);
        entry_w.key_int("technique_value",     technique_value);
        entry_w.key_int("num_cycles",          num_cycles);
        entry_w.key_int("velocity_profile",    velocity_profile);
        entry_w.key_int("end_range_or_doppler",end_range_doppler);
        entry_w.key_int("subtype_code",        subtype_code);
        entry_w.key_int("non_linear_profile",  non_linear_profile);
        if (i > 0) tech_arr += ",";
        tech_arr += entry_w.str();
    }
    tech_arr += "]";

    sdfc::JsonWriter jpro_w;
    jpro_w.key_int("drfm_mode",      drfm_mode);
    jpro_w.key_int("num_responses",  num_resp);
    jpro_w.key_bool("repeat",        repeat);
    jpro_w.key_raw("techniques",     tech_arr);
    jpro_w.key_str("raw_hex",        hex_dump(p, 24));
    return jpro_w.str();
}

// ---------------------------------------------------------------------------
// Warner Library record decoder — 226 bytes per record (IRS §5.2.2, pp.10-15)
// Layout (all multi-byte fields BE):
//   0–1:    RecordNumber          (UINT16)
//   2–5:    CentralRadarDBNumber  (UINT32)
//   6–9:    Frequency             (UINT32, KHz)
//   10–13:  FrequencyAttributes   (UINT32, bit-encoded)
//   14–15:  FrequencyTolerance    (UINT16, KHz)
//   16–19:  PW                    (UINT32, ns)
//   20–21:  PWTolerance           (UINT16, ns)
//   22–25:  PRF                   (UINT32, Hz)
//   26–29:  PRIAttributes         (UINT32, bit-encoded)
//   30–31:  PRFTolerance          (UINT16, Hz)
//   32:     ThreatThresholdAmplitude (UINT8, dBm)
//   33:     ScanType              (UINT8, bit-encoded)
//   34–35:  AntennaScanPeriod     (UINT16, ms)
//   36–37:  ASPTolerance          (UINT16, ms)
//   38–61:  JPRONumber            (24 bytes, see decode_jpro)
//   62:     ThreatLevel           (UINT8)
//   63:     NTDSCode              (UINT8)
//   64:     Platform              (UINT8)
//   65:     OperationalRole       (UINT8)
//   66:     ConfidenceLevel       (UINT8)
//   67–74:  RadarName             (8 bytes ASCII)
//   75:     RadarMode             (UINT8)
//   76–77:  NextModeRecordNumber  (UINT16) — 0 = end of chain
//   78–81:  FrequencyMinimum      (UINT32, KHz)
//   82–85:  FrequencyMaximum      (UINT32, KHz)
//   86–89:  PWMinimum             (UINT32, ns)
//   90–93:  PWMaximum             (UINT32, ns)
//   94–97:  PGRI                  (UINT32, ns)
//   98–161: SpotPRFs              (16 x UINT32, Hz)
//   162–225: SpotFrequencies      (16 x UINT32, KHz)
//   Total: 226 bytes
// ---------------------------------------------------------------------------

static constexpr int WARNER_RECORD_SIZE = 226;

static std::string decode_warner_record(const uint8_t* p, int avail) {
    if (avail < WARNER_RECORD_SIZE) {
        sdfc::JsonWriter err;
        err.key_str("error", "record_truncated");
        err.key_int("avail", avail);
        return err.str();
    }

    uint16_t rec_no          = load_be16(p + 0);
    uint32_t central_db_no    = load_be32(p + 2);
    uint32_t freq_khz         = load_be32(p + 6);
    uint32_t freq_attr        = load_be32(p + 10);
    uint16_t freq_tol_khz     = load_be16(p + 14);
    uint32_t pw_ns            = load_be32(p + 16);
    uint16_t pw_tol_ns        = load_be16(p + 20);
    uint32_t prf_hz           = load_be32(p + 22);
    uint32_t pri_attr         = load_be32(p + 26);
    uint16_t prf_tol_hz       = load_be16(p + 30);
    uint8_t  threat_thresh    = p[32];
    uint8_t  scan_type        = p[33];
    uint16_t asp_ms           = load_be16(p + 34);
    uint16_t asp_tol_ms       = load_be16(p + 36);
    std::string jpro          = decode_jpro(p + 38, avail - 38);
    uint8_t  threat_level     = p[62];
    uint8_t  ntds_code        = p[63];
    uint8_t  platform         = p[64];
    uint8_t  operational_role = p[65];
    uint8_t  confidence_level = p[66];

    char radar_name[9] = {};
    std::memcpy(radar_name, p + 67, 8);
    radar_name[8] = '\0';

    uint8_t  radar_mode        = p[75];
    uint16_t next_mode_rec_no  = load_be16(p + 76);
    uint32_t freq_min_khz      = load_be32(p + 78);
    uint32_t freq_max_khz      = load_be32(p + 82);
    uint32_t pw_min_ns         = load_be32(p + 86);
    uint32_t pw_max_ns         = load_be32(p + 90);
    uint32_t pgri_ns           = load_be32(p + 94);

    std::string spot_prfs = "[";
    for (int i = 0; i < 16; ++i) {
        char e[16];
        std::snprintf(e, sizeof(e), "%s%u", (i > 0 ? "," : ""), load_be32(p + 98 + i * 4));
        spot_prfs += e;
    }
    spot_prfs += "]";

    std::string spot_freqs = "[";
    for (int i = 0; i < 16; ++i) {
        char e[16];
        std::snprintf(e, sizeof(e), "%s%u", (i > 0 ? "," : ""), load_be32(p + 162 + i * 4));
        spot_freqs += e;
    }
    spot_freqs += "]";

    sdfc::JsonWriter w;
    w.key_int("record_no", rec_no);
    w.key_int("central_radar_db_no", central_db_no);
    w.key_int("freq_khz", freq_khz);
    w.key_int("freq_attributes", freq_attr);
    w.key_int("freq_tolerance_khz", freq_tol_khz);
    w.key_int("pw_ns", pw_ns);
    w.key_int("pw_tolerance_ns", pw_tol_ns);
    w.key_int("prf_hz", prf_hz);
    w.key_int("pri_attributes", pri_attr);
    w.key_int("prf_tolerance_hz", prf_tol_hz);
    w.key_int("threat_threshold_dbm", threat_thresh);
    w.key_int("scan_type", scan_type);
    w.key_int("asp_ms", asp_ms);
    w.key_int("asp_tolerance_ms", asp_tol_ms);
    w.key_raw("jpro", jpro);
    w.key_int("threat_level", threat_level);
    w.key_int("ntds_code", ntds_code);
    w.key_int("platform", platform);
    w.key_int("operational_role", operational_role);
    w.key_int("confidence_level", confidence_level);
    w.key_str("radar_name", radar_name);
    w.key_int("radar_mode", radar_mode);
    w.key_int("next_mode_record_no", next_mode_rec_no);
    w.key_int("freq_min_khz", freq_min_khz);
    w.key_int("freq_max_khz", freq_max_khz);
    w.key_int("pw_min_ns", pw_min_ns);
    w.key_int("pw_max_ns", pw_max_ns);
    w.key_int("pgri_ns", pgri_ns);
    w.key_raw("spot_prfs_hz", spot_prfs);
    w.key_raw("spot_frequencies_khz", spot_freqs);
    return w.str();
}

// ===========================================================================
// VARIANT A MESSAGE PARSERS
// Body starts at frame offset 10 (after SOM+CmdCode+SeqNo+BodyLen).
// ===========================================================================

// ---- ESMP → RSEC: Active Track Data (0x1502) — periodic 1 Hz, NEVER ACK ----
// Body layout (IRS §5.3.1, Data Elements Table, pp.26-31): a single flat
// track record — no header, no repeat count, starts directly at Track No.
// Fixed portion is 94 bytes:
//   0–1:   TrackNo           (UINT16 BE): 1–500
//   2–3:   TrackStatus       (UINT16 BE): bit-encoded
//   4–5:   DOA               (UINT16 BE): tenths of degrees, ÷10 = degrees
//   6–9:   Frequency         (UINT32 BE): ×10 = KHz
//   10–13: FrequencyAttribs  (UINT32 BE): bit-encoded
//   14–17: PW                (UINT32 BE): ×10 = ns
//   18–21: PRI               (UINT32 BE): ÷10 = microseconds
//   22–25: PRF               (UINT32 BE): Hz
//   26–29: PRIAttribs        (UINT32 BE): bit-encoded
//   30:    Amplitude         (UINT8):     ×-1 = dBm
//   31:    ScanType          (UINT8):     bit-encoded
//   32–33: AntennaScanPeriod (UINT16 BE): ms
//   34–37: TOFA              (UINT32 BE): seconds since 00:00:00 current date
//   38–41: TOLA              (UINT32 BE): seconds since 00:00:00 current date
//   42–43: ActivityCount     (UINT16 BE)
//   44–45: TrackAge          (UINT16 BE): seconds
//   46–47: TrackHitCount     (UINT16 BE)
//   48–52: Identity1(4)+ConfidenceLevel1(1)
//   53–57: Identity2(4)+ConfidenceLevel2(1)
//   58–62: Identity3(4)+ConfidenceLevel3(1)
//   63–67: Identity4(4)+ConfidenceLevel4(1)
//   68–72: Identity5(4)+ConfidenceLevel5(1)
//   73:    Reserved          (UINT8)
//   74–77: FrequencyMinimum  (UINT32 BE): ×10 = KHz
//   78–81: FrequencyMaximum  (UINT32 BE): ×10 = KHz
//   82–85: PWMinimum         (UINT32 BE): ×10 = ns
//   86–89: PWMaximum         (UINT32 BE): ×10 = ns
//   90–93: PGRI              (UINT32 BE): ×10 = ns
// 94+: Spot PRFs/PWs (interleaved pairs) then Spot Frequencies — variable
//   length, count driven by subfields inside PRIAttribs/FrequencyAttribs
//   (bit positions not yet available); emitted as raw hex pending that.

static constexpr int ACTIVE_TRACK_FIXED_SIZE = 94;

static void parse_active_track(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    uint16_t track_no        = load_be16(body + 0);
    uint16_t track_status    = load_be16(body + 2);
    uint16_t doa_raw         = load_be16(body + 4);
    uint32_t freq_raw        = load_be32(body + 6);
    uint32_t freq_attributes = load_be32(body + 10);
    uint32_t pw_raw          = load_be32(body + 14);
    uint32_t pri_raw         = load_be32(body + 18);
    uint32_t prf_hz          = load_be32(body + 22);
    uint32_t pri_attributes  = load_be32(body + 26);
    uint8_t  amp_raw         = body[30];
    uint8_t  scan_type       = body[31];
    uint16_t asp_ms          = load_be16(body + 32);
    uint32_t tofa_s          = load_be32(body + 34);
    uint32_t tola_s          = load_be32(body + 38);
    uint16_t activity_count  = load_be16(body + 42);
    uint16_t track_age_s     = load_be16(body + 44);
    uint16_t track_hit_count = load_be16(body + 46);
    uint32_t identity1       = load_be32(body + 48);
    uint8_t  confidence1     = body[52];
    uint32_t identity2       = load_be32(body + 53);
    uint8_t  confidence2     = body[57];
    uint32_t identity3       = load_be32(body + 58);
    uint8_t  confidence3     = body[62];
    uint32_t identity4       = load_be32(body + 63);
    uint8_t  confidence4     = body[67];
    uint32_t identity5       = load_be32(body + 68);
    uint8_t  confidence5     = body[72];
    uint32_t freq_min_raw    = load_be32(body + 74);
    uint32_t freq_max_raw    = load_be32(body + 78);
    uint32_t pw_min_raw      = load_be32(body + 82);
    uint32_t pw_max_raw      = load_be32(body + 86);
    uint32_t pgri_raw        = load_be32(body + 90);

    w.key_int("track_no",           track_no);
    w.key_int("track_status",       track_status);
    w.key_double("doa_deg",         doa_raw / 10.0);
    w.key_int("freq_khz",           static_cast<long long>(freq_raw) * 10);
    w.key_int("freq_attributes",    freq_attributes);
    w.key_int("pw_ns",              static_cast<long long>(pw_raw) * 10);
    w.key_double("pri_us",          pri_raw / 10.0);
    w.key_int("prf_hz",             prf_hz);
    w.key_int("pri_attributes",     pri_attributes);
    w.key_int("amplitude_dbm",      -static_cast<long long>(amp_raw));
    w.key_int("scan_type",          scan_type);
    w.key_int("asp_ms",             asp_ms);
    w.key_int("tofa_s",             tofa_s);
    w.key_int("tola_s",             tola_s);
    w.key_int("activity_count",     activity_count);
    w.key_int("track_age_s",        track_age_s);
    w.key_int("track_hit_count",    track_hit_count);
    w.key_int("identity1",          identity1);
    w.key_int("confidence_level1",  confidence1);
    w.key_int("identity2",          identity2);
    w.key_int("confidence_level2",  confidence2);
    w.key_int("identity3",          identity3);
    w.key_int("confidence_level3",  confidence3);
    w.key_int("identity4",          identity4);
    w.key_int("confidence_level4",  confidence4);
    w.key_int("identity5",          identity5);
    w.key_int("confidence_level5",  confidence5);
    w.key_int("freq_min_khz",       static_cast<long long>(freq_min_raw) * 10);
    w.key_int("freq_max_khz",       static_cast<long long>(freq_max_raw) * 10);
    w.key_int("pw_min_ns",          static_cast<long long>(pw_min_raw) * 10);
    w.key_int("pw_max_ns",          static_cast<long long>(pw_max_raw) * 10);
    w.key_int("pgri_ns",            static_cast<long long>(pgri_raw) * 10);

    if (body_len > ACTIVE_TRACK_FIXED_SIZE) {
        w.key_str("spot_data_hex",
                  sdfc::to_hex(body + ACTIVE_TRACK_FIXED_SIZE,
                               body_len - ACTIVE_TRACK_FIXED_SIZE));
    }
}

// ---- ESMP → RSEC: Operational Data (0x1504) — periodic 1 Hz ----
// Body layout (IRS §5.3.2, Data Elements Table, pp.34-36):
// Fixed header (25 bytes):
//   0–1:   NumActiveTracks  (UINT16 BE)
//   2–3:   NumPassive       (UINT16 BE)
//   4–5:   NumCWTracks      (UINT16 BE)
//   6–7:   NumWarnerTracks  (UINT16 BE)
//   8–9:   NumLockOnTracks  (UINT16 BE)
//   10–11: TrackInformation (UINT16 BE): bit-encoded
//   12–13: LinkStatus       (UINT16 BE): bit-encoded, "sub-systems" link status
//   14–15: PlatformHeading  (UINT16 BE): ÷10 = degrees
//   16–19: PulseCount1(NB)  (UINT32 BE)
//   20–23: PulseCount2(BB)  (UINT32 BE)
//   24:    NumThreats(n)    (UINT8):     1–10, drives the repeated block below
// Repeated threat entry (36 bytes each, n times, starting at offset 25):
//   +0–1:  EmitterNumber    (UINT16 BE)
//   +2–5:  Frequency        (UINT32 BE): ×10 = KHz
//   +6–7:  ThreatAzimuth    (UINT16 BE): ÷10 = degrees
//   +8–9:  Elevation        (UINT16 BE): ÷10 = degrees, raw wire mapping only
//                           (0–300→0.0–30.0; 3500–3599→350.0–359.9, which
//                           represents -10.0..-0.1 — no sign remap applied here)
//   +10:   ThreatStatus     (UINT8):     bit-encoded, 8 bits
//   +11–34:JPRONumber       (24 bytes):  see decode_jpro()
//   +35:   RTGStatus        (UINT8):     0–1
// Fixed footer (28 bytes, after the last threat entry):
//   +0–1:  ServoPositionPort       (UINT16 BE): ÷10 = degrees
//   +2–3:  ServoPositionStarboard  (UINT16 BE): ÷10 = degrees
//   +4–7:  LinkStatus              (UINT32 BE): bit-encoded, "LRUs of EA
//                                   sub-system" link status (distinct field
//                                   from the header's 2-byte LinkStatus)
//   +8–27: TxHealthStatus          (5 × UINT32 BE): bit-encoded array

static constexpr int OPDATA_HEADER_SIZE = 25;
static constexpr int OPDATA_THREAT_SIZE = 36;

static void parse_operational_data(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    uint16_t num_active_tracks = load_be16(body + 0);
    uint16_t num_passive       = load_be16(body + 2);
    uint16_t num_cw_tracks     = load_be16(body + 4);
    uint16_t num_warner_tracks = load_be16(body + 6);
    uint16_t num_lock_on       = load_be16(body + 8);
    uint16_t track_information = load_be16(body + 10);
    uint16_t link_status       = load_be16(body + 12);
    uint16_t platform_heading  = load_be16(body + 14);
    uint32_t pulse_count_nb    = load_be32(body + 16);
    uint32_t pulse_count_bb    = load_be32(body + 20);
    uint8_t  num_threats       = body[24];

    w.key_int("num_active_tracks",  num_active_tracks);
    w.key_int("num_passive",        num_passive);
    w.key_int("num_cw_tracks",      num_cw_tracks);
    w.key_int("num_warner_tracks",  num_warner_tracks);
    w.key_int("num_lock_on_tracks", num_lock_on);
    w.key_int("track_information",  track_information);
    w.key_int("link_status",        link_status);
    w.key_double("platform_heading_deg", platform_heading / 10.0);
    w.key_int("pulse_count_nb",     pulse_count_nb);
    w.key_int("pulse_count_bb",     pulse_count_bb);
    w.key_int("num_threats",        num_threats);

    std::string threats = "[";
    for (int i = 0; i < num_threats; ++i) {
        const uint8_t* t = body + OPDATA_HEADER_SIZE + i * OPDATA_THREAT_SIZE;
        uint16_t emitter_no    = load_be16(t + 0);
        uint32_t freq_raw      = load_be32(t + 2);
        uint16_t azimuth_raw   = load_be16(t + 6);
        uint16_t elevation_raw = load_be16(t + 8);
        uint8_t  threat_status = t[10];
        int      jpro_offset   = OPDATA_HEADER_SIZE + i * OPDATA_THREAT_SIZE + 11;
        std::string jpro       = decode_jpro(t + 11, body_len - jpro_offset);
        uint8_t  rtg_status    = t[35];

        sdfc::JsonWriter tw;
        tw.key_int("emitter_number",  emitter_no);
        tw.key_int("freq_khz",        static_cast<long long>(freq_raw) * 10);
        tw.key_double("azimuth_deg",  azimuth_raw / 10.0);
        tw.key_double("elevation_deg", elevation_raw / 10.0);
        tw.key_int("threat_status",   threat_status);
        tw.key_raw("jpro",            jpro);
        tw.key_int("rtg_status",      rtg_status);
        if (i > 0) threats += ",";
        threats += tw.str();
    }
    threats += "]";
    w.key_raw("threats", threats);

    const uint8_t* footer = body + OPDATA_HEADER_SIZE + num_threats * OPDATA_THREAT_SIZE;
    uint16_t servo_port      = load_be16(footer + 0);
    uint16_t servo_starboard = load_be16(footer + 2);
    uint32_t lru_link_status = load_be32(footer + 4);

    w.key_double("servo_position_port_deg",      servo_port / 10.0);
    w.key_double("servo_position_starboard_deg", servo_starboard / 10.0);
    w.key_int("lru_link_status", lru_link_status);

    std::string tx_health = "[";
    for (int i = 0; i < 5; ++i) {
        uint32_t v = load_be32(footer + 8 + i * 4);
        if (i > 0) tx_health += ",";
        tx_health += std::to_string(v);
    }
    tx_health += "]";
    w.key_raw("tx_health_status", tx_health);
}

// ---- ESMP → RSEC: Purge Response (0x1505) ----
// Body layout (IRS §5.3.3, Data Elements Table, p.36):
//   0–1: NumberOfTracks (UINT16 BE): 0–500, count of purged tracks
//   2+:  TrackNumber (UINT16 BE) x NumberOfTracks: 1–500 each

static void parse_purge_response(const uint8_t* body, int /*body_len*/, sdfc::JsonWriter& w) {
    uint16_t num_tracks = load_be16(body + 0);
    w.key_int("num_tracks", num_tracks);

    std::string arr = "[";
    for (int i = 0; i < num_tracks; ++i) {
        if (i > 0) arr += ",";
        arr += std::to_string(load_be16(body + 2 + i * 2));
    }
    arr += "]";
    w.key_raw("track_numbers", arr);
}

// ---- ESMP<->RSEC: ACK/NACK (0x1509 ESMP->RSEC §5.3.4; 0x150A RSEC->ESMP §5.2.9) ----
// Body (5 bytes, both directions, same layout):
//   0–1: MessageCode          (UINT16): code of the message being acknowledged
//   2–3: SequenceNumber       (UINT16): sequence number of that message instance
//   4:   AcknowledgementStatus(UINT8):
//          0 = Invalid message (NACK)
//          1 = Message received completely (ACK)
//          2 = Message received completely & execution successful (ACK)
//          3 = Message received completely, but failed to execute

static void parse_ack_nack(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 5) { w.key_str("parse_error", "body_too_short"); return; }
    uint16_t acked_cmd_code = load_be16(body + 0);
    uint16_t acked_seq_no   = load_be16(body + 2);
    uint8_t  ack_status     = body[4];

    char cmd_hex[8];
    std::snprintf(cmd_hex, sizeof(cmd_hex), "0x%04X", acked_cmd_code);

    w.key_str("acked_cmd_code_hex", cmd_hex);
    w.key_int("acked_seq_no",       acked_seq_no);
    w.key_int("ack_status",         ack_status);
}

// ---- RSEC → ESMP: Time Data (0x1002) ----
// Body layout (IRS §5.2.11, p.25): single field —
//   0–3: System Time in Seconds (UINT32 BE): seconds elapsed from epoch
//        1970-01-01T00:00:00.000. Range 0-0xFFFFFFFF.

static void parse_time_data(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 4) { w.key_str("parse_error", "body_too_short"); return; }
    uint32_t epoch_sec = load_be32(body);
    w.key_uint("system_time_epoch_sec", epoch_sec);
}

// ---- RSEC → ESMP: Load Warner Library (0x1003) ----
// Body layout (IRS §5.2.2, p.10):
//   0:    New Set/Modify Flag (UINT8): 1=new set (replace library), 0=modify/append
//   1–2:  Number of Library Records (UINT16, 0-500)
//   3+:   Records (WARNER_RECORD_SIZE bytes each)
// Note: ESMP stops periodic messages (0x1502/0x1504) during load.

static void parse_load_warner(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 3) { w.key_str("parse_error", "body_too_short"); return; }
    uint8_t  new_set_modify = body[0];
    uint16_t num_records    = load_be16(body + 1);
    w.key_bool("new_set", new_set_modify != 0);
    w.key_int("num_library_records", num_records);

    if (num_records == 0) return;

    std::string arr = "[";
    int offset = 3;
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
// Body (IRS §5.2.3, p.16): NumberOfLibraryRecords(UINT16, 0-500) +
// repeated RecordNumber(UINT16), 'Number of Library Records' times.
// 0 = library considered empty (no entries to delete individually).

static void parse_delete_warner(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 2) { w.key_str("parse_error", "body_too_short"); return; }
    uint16_t num_records = load_be16(body);
    w.key_int("num_library_records", num_records);

    std::string arr = "[";
    int count = 0;
    for (int i = 0; i < num_records && (2 + i * 2 + 2) <= body_len; ++i) {
        if (i > 0) arr += ",";
        arr += std::to_string(load_be16(body + 2 + i * 2));
        ++count;
    }
    arr += "]";
    w.key_raw("record_numbers", arr);
    if (count < num_records)
        w.key_str("parse_warning", "body_truncated_fewer_records_than_declared");
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
        sdfc::JsonWriter entry_w;
        entry_w.key_int("freq_low_khz",  lo);
        entry_w.key_int("freq_high_khz", hi);
        if (i > 0) arr += ",";
        arr += entry_w.str();
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
        sdfc::JsonWriter entry_w;
        entry_w.key_double("start_deg", start_deg);
        entry_w.key_double("end_deg",   end_deg);
        if (i > 0) arr += ",";
        arr += entry_w.str();
    }
    arr += "]";
    w.key_raw("sectors", arr);
}

// ---- RSEC → ESMP: Set Auto Purge Time (0x1008) ----
// Body (IRS §5.2.6, p.19): single field —
//   0–1: Track Age (UINT16, seconds, 0-600). 0 = Auto Purge Off;
//        >0 = passive-track age threshold for purging.

static void parse_auto_purge(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 2) { w.key_str("parse_error", "body_too_short"); return; }
    w.key_int("track_age_sec", load_be16(body));
}

// ---- RSEC → ESMP: Purge Passive Tracks (0x1010) ----
// Body (IRS §5.2.7, p.19-20): NumberOfTracks(UINT16, 0-500) +
// repeated TrackNumber(UINT16), 'Number of Tracks' times.
// 0 = purge all passive tracks; >0 = purge the listed tracks.

static void parse_purge_passive(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 2) { w.key_str("parse_error", "body_too_short"); return; }
    uint16_t num_tracks = load_be16(body);
    w.key_int("num_tracks", num_tracks);

    std::string arr = "[";
    int count = 0;
    for (int i = 0; i < num_tracks && (2 + i * 2 + 2) <= body_len; ++i) {
        if (i > 0) arr += ",";
        arr += std::to_string(load_be16(body + 2 + i * 2));
        ++count;
    }
    arr += "]";
    w.key_raw("track_numbers", arr);
    if (count < num_tracks)
        w.key_str("parse_warning", "body_truncated_fewer_tracks_than_declared");
}

// ---- RSEC → ESMP: Platform Heading (0x1014) ----
// Body: Heading(UINT16): tenths of degrees, ÷10 = degrees (0.0°–359.9°)

static void parse_platform_heading(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 2) { w.key_str("parse_error", "body_too_short"); return; }
    w.key_double("heading_deg", load_be16(body) / 10.0);
}

// ---- RSEC → ESMP: Set Scan Bands (0x1118) ----
// Body (IRS §5.2.8, pp.20-22):
//   0: NumberOfFrequencyBands (UINT8, 1-16)
//   1: DF_ComputationMode     (UINT8): 1=ADF, 2=BLI, 3=ADF+BLI
//   2: Sector_Selection_BLI   (UINT8): valid if DF_ComputationMode is 2 or 3
//   3+: repeated per band (14 bytes each), 'NumberOfFrequencyBands' times:
//     +0 ScanBandIndex   (UINT8, 1-16)
//     +1 StartFrequency  (UINT16, MHz, 1000-18000)
//     +3 StopFrequency   (UINT16, MHz, 1000-18000)
//     +5 RFCUAttenuation (UINT8, dB)
//     +6 SSUAttenuation  (UINT8, dB)
//     +7 DigRxThreshold  (UINT8, dB)
//     +8 IFBWSelection   (UINT8): 1=+/-250MHz, 2=+/-20MHz
//     +9 DetectionTime   (UINT16, ms)
//     +11 CollectionTime (UINT16, ms)
//     +13 NumberOfRevisits (UINT8)

static constexpr int SCAN_BAND_ENTRY_SIZE = 14;

static void parse_set_scan_bands(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 3) { w.key_str("parse_error", "body_too_short"); return; }
    uint8_t num_bands   = body[0];
    uint8_t df_mode     = body[1];
    uint8_t sector_bli  = body[2];
    w.key_int("num_bands",              num_bands);
    w.key_int("df_computation_mode",    df_mode);
    w.key_int("sector_selection_bli",   sector_bli);

    std::string arr = "[";
    int count = 0;
    for (int i = 0; i < num_bands && (3 + i * SCAN_BAND_ENTRY_SIZE + SCAN_BAND_ENTRY_SIZE) <= body_len; ++i) {
        const uint8_t* b = body + 3 + i * SCAN_BAND_ENTRY_SIZE;
        uint8_t  band_idx        = b[0];
        uint16_t start_freq_mhz  = load_be16(b + 1);
        uint16_t stop_freq_mhz   = load_be16(b + 3);
        uint8_t  rfcu_att        = b[5];
        uint8_t  ssu_att         = b[6];
        uint8_t  digrx_thresh    = b[7];
        uint8_t  ifbw_sel        = b[8];
        uint16_t detection_ms    = load_be16(b + 9);
        uint16_t collection_ms   = load_be16(b + 11);
        uint8_t  num_revisits    = b[13];

        sdfc::JsonWriter entry_w;
        entry_w.key_int("scan_band_index",     band_idx);
        entry_w.key_int("start_freq_mhz",      start_freq_mhz);
        entry_w.key_int("stop_freq_mhz",       stop_freq_mhz);
        entry_w.key_int("rfcu_attenuation",    rfcu_att);
        entry_w.key_int("ssu_attenuation",     ssu_att);
        entry_w.key_int("digrx_threshold",     digrx_thresh);
        entry_w.key_int("if_bw_selection",     ifbw_sel);
        entry_w.key_int("detection_time_ms",   detection_ms);
        entry_w.key_int("collection_time_ms",  collection_ms);
        entry_w.key_int("num_revisits",        num_revisits);
        if (i > 0) arr += ",";
        arr += entry_w.str();
        ++count;
    }
    arr += "]";
    w.key_raw("bands", arr);
    if (count < num_bands)
        w.key_str("parse_warning", "body_truncated_fewer_bands_than_declared");
}

// ---- RSEC → ESMP: Select Directed Search Mode (0x1114) ----
// Body (IRS §5.2.10, pp.24-25), 7 bytes total:
//   0–1: Frequency       (UINT16, MHz, 1000-18000): directed RF frequency
//   2:   RFAttenuation   (UINT8, dB, 0-30)
//   3:   IFAttenuation   (UINT8, dB, 0-30)
//   4:   Threshold       (UINT8, dB, 0-63 -> -63 to -0)
//   5:   IFBWSelection   (UINT8): 1=+/-250MHz, 2=+/-20MHz
//   6:   ScanQuadrant    (UINT8): 1=(0-90deg), 2=(90-180), 3=(180-270), 4=(270-0)

static void parse_directed_search(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 7) { w.key_str("parse_error", "body_too_short"); return; }
    w.key_uint("frequency_mhz",    load_be16(body + 0));
    w.key_int("rf_attenuation_db", body[2]);
    w.key_int("if_attenuation_db", body[3]);
    w.key_int("threshold_db",      body[4]);
    w.key_int("if_bw_selection",   body[5]);
    w.key_int("scan_quadrant",     body[6]);
}

// ---- RSEC → ECMP: Track Command semi-auto (0x1101) ----
// Body (IRS §5.5.1, p.42-43), 41 bytes:
//   0–1:  TrackNo    (UINT16, 1-500)
//   2–3:  DOA        (UINT16, tenths-of-degree, ÷10)
//   4–7:  Frequency  (UINT32, KHz, 1000000-1800000 -> 1000MHz-18GHz)
//   8–11: PW         (UINT32, ns)
//   12–15: PRF       (UINT32, Hz)
//   16:   ThreatLevel(UINT8)
//   17–40: JPRONumber(24 bytes)

static void parse_ecmp_semi_track(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 17) { w.key_str("parse_error", "body_too_short"); return; }
    w.key_int("track_no",       load_be16(body + 0));
    w.key_double("doa_deg",     load_be16(body + 2) / 10.0);
    w.key_uint("freq_khz",      load_be32(body + 4));
    w.key_uint("pw_ns",         load_be32(body + 8));
    w.key_uint("prf_hz",        load_be32(body + 12));
    w.key_int("threat_level",   body[16]);
    if (body_len >= 41)
        w.key_raw("jpro", decode_jpro(body + 17, body_len - 17));
    else
        w.key_str("parse_warning", "jpro_data_missing_or_truncated");
}

// ---- RSEC → ECMP: Jam Command (0x1102) ----
// Body (IRS §5.5.2, p.50): NumberOfEmitters(UINT8, 1-50) +
// repeated [EmitterNumber(UINT16, 1-500) + JamDuration(UINT16 sec, 0-100;
// 0 = jam indefinitely)], 'Number of Emitters' times. No JPRO in this message.

static void parse_ecmp_jam(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 1) { w.key_str("parse_error", "body_too_short"); return; }
    uint8_t num_emitters = body[0];
    w.key_int("num_emitters", num_emitters);

    std::string arr = "[";
    int count = 0;
    for (int i = 0; i < num_emitters && (1 + i * 4 + 4) <= body_len; ++i) {
        const uint8_t* e = body + 1 + i * 4;
        sdfc::JsonWriter entry_w;
        entry_w.key_int("emitter_number",    load_be16(e));
        entry_w.key_int("jam_duration_sec",  load_be16(e + 2));
        if (i > 0) arr += ",";
        arr += entry_w.str();
        ++count;
    }
    arr += "]";
    w.key_raw("emitters", arr);
    if (count < num_emitters)
        w.key_str("parse_warning", "body_truncated_fewer_emitters_than_declared");
}

// ---- RSEC → ECMP: Track and Jam Command semi-auto (0x1103) ----
// Body (IRS §5.5.3, pp.51-52), 69 bytes:
//   0–1:  TrackNo          (UINT16, 1-500)
//   2–3:  DOA              (UINT16, ÷10 deg)
//   4–7:  Frequency        (UINT32, KHz)
//   8–11: FrequencyAttributes (UINT32, bit-encoded)
//   12–15: PW              (UINT32, ns)
//   16–19: PRF             (UINT32, Hz)
//   20–23: PRIAttributes   (UINT32, bit-encoded)
//   24:   Amplitude        (INT8, dBm)
//   25:   ScanType         (UINT8, bit-encoded)
//   26–27: ASP             (UINT16, ms)
//   28–31: PWMinimum       (UINT32, ns)
//   32–35: PWMaximum       (UINT32, ns)
//   36–39: SpotFrequencies (UINT32, KHz)
//   40:   ThreatLevel      (UINT8)
//   41–42: Elevation       (UINT16, ÷10 deg)
//   43–66: JPRONumber      (24 bytes)
//   67–68: JamDuration     (UINT16, sec; 0 = indefinite)

static void parse_ecmp_track_and_jam(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 43) { w.key_str("parse_error", "body_too_short"); return; }
    w.key_int("track_no",          load_be16(body + 0));
    w.key_double("doa_deg",        load_be16(body + 2) / 10.0);
    w.key_uint("freq_khz",         load_be32(body + 4));
    w.key_uint("freq_attributes",  load_be32(body + 8));
    w.key_uint("pw_ns",            load_be32(body + 12));
    w.key_uint("prf_hz",           load_be32(body + 16));
    w.key_uint("pri_attributes",   load_be32(body + 20));
    w.key_int("amplitude_dbm",     static_cast<int8_t>(body[24]));
    w.key_int("scan_type",         body[25]);
    w.key_int("asp_ms",            load_be16(body + 26));
    w.key_uint("pw_min_ns",        load_be32(body + 28));
    w.key_uint("pw_max_ns",        load_be32(body + 32));
    w.key_uint("spot_freq_khz",    load_be32(body + 36));
    w.key_int("threat_level",      body[40]);
    w.key_double("elevation_deg",  load_be16(body + 41) / 10.0);
    if (body_len >= 67)
        w.key_raw("jpro", decode_jpro(body + 43, body_len - 43));
    else
        w.key_str("parse_warning", "jpro_data_missing_or_truncated");
    if (body_len >= 69)
        w.key_int("jam_duration_sec", load_be16(body + 67));
}

// ---- RSEC → ECMP: Break Track (0x1104) ----
// Body (IRS §5.5.5, p.53): NumberOfEmitters(UINT8, 1-50) +
// repeated EmitterNumber(UINT16, 1-500), 'Number of Emitters' times.

static void parse_ecmp_break_track(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 1) { w.key_str("parse_error", "body_too_short"); return; }
    uint8_t num_emitters = body[0];
    w.key_int("num_emitters", num_emitters);

    std::string arr = "[";
    int count = 0;
    for (int i = 0; i < num_emitters && (1 + i * 2 + 2) <= body_len; ++i) {
        if (i > 0) arr += ",";
        arr += std::to_string(load_be16(body + 1 + i * 2));
        ++count;
    }
    arr += "]";
    w.key_raw("emitter_numbers", arr);
    if (count < num_emitters)
        w.key_str("parse_warning", "body_truncated_fewer_emitters_than_declared");
}

// ---- RSEC → ECMP: Stop Jam (0x1105) ----
// Body (IRS §5.5.4, pp.52-53): NumberOfEmitters(UINT8, 1-50) +
// repeated EmitterNumber(UINT16, 1-500), 'Number of Emitters' times.

static void parse_ecmp_stop_jam(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 1) { w.key_str("parse_error", "body_too_short"); return; }
    uint8_t num_emitters = body[0];
    w.key_int("num_emitters", num_emitters);

    std::string arr = "[";
    int count = 0;
    for (int i = 0; i < num_emitters && (1 + i * 2 + 2) <= body_len; ++i) {
        if (i > 0) arr += ",";
        arr += std::to_string(load_be16(body + 1 + i * 2));
        ++count;
    }
    arr += "]";
    w.key_raw("emitter_numbers", arr);
    if (count < num_emitters)
        w.key_str("parse_warning", "body_truncated_fewer_emitters_than_declared");
}

// ---- RSEC → ECMP: Change JPRO (0x1106) ----
// Body (IRS §5.5.6, p.54), 26 bytes: TrackNo(UINT16, 1-500) + JPRONumber(24B).

static void parse_ecmp_change_jpro(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 2) { w.key_str("parse_error", "body_too_short"); return; }
    w.key_int("track_no", load_be16(body));
    if (body_len >= 26)
        w.key_raw("jpro", decode_jpro(body + 2, body_len - 2));
    else
        w.key_str("parse_warning", "jpro_data_missing_or_truncated");
}

// ---- ECMP → RSEC: EA Operational Data (Semi) (0x1153) ----
// Body (IRS §5.6.1, pp.55-56): NoOfTracks(UINT8, 1-50) +
// repeated per-track (36 bytes), 'No of Tracks' times:
//   +0 EmitterNumber (UINT16, 501-550)
//   +2 Frequency     (UINT32, MHz, 1000-18000)
//   +6 Azimuth       (UINT16, ÷10 deg)
//   +8 Elevation     (UINT16, ÷10 deg, 0-30)
//   +10 ThreatStatus (UINT8, bit-encoded — see §5.6.1.1)
//   +11 JPRONumber   (24 bytes)
//   +35 RTGStatus    (UINT8): 1=RTG ON, 0=RTG OFF

static constexpr int EA_OP_DATA_ENTRY_SIZE = 36;

static void parse_ea_operational_data(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 1) { w.key_str("parse_error", "body_too_short"); return; }
    uint8_t num_tracks = body[0];
    w.key_int("num_tracks", num_tracks);

    std::string arr = "[";
    int count = 0;
    for (int i = 0; i < num_tracks && (1 + i * EA_OP_DATA_ENTRY_SIZE + EA_OP_DATA_ENTRY_SIZE) <= body_len; ++i) {
        const uint8_t* e = body + 1 + i * EA_OP_DATA_ENTRY_SIZE;
        uint16_t emitter_no  = load_be16(e + 0);
        uint32_t freq_mhz    = load_be32(e + 2);
        double   azimuth_deg = load_be16(e + 6) / 10.0;
        double   elev_deg    = load_be16(e + 8) / 10.0;
        uint8_t  threat_stat = e[10];
        std::string jpro     = decode_jpro(e + 11, EA_OP_DATA_ENTRY_SIZE - 11 - 1);
        uint8_t  rtg_status  = e[35];

        char threat_hex[8];
        std::snprintf(threat_hex, sizeof(threat_hex), "0x%02X", threat_stat);

        sdfc::JsonWriter entry_w;
        entry_w.key_int("emitter_number",  emitter_no);
        entry_w.key_int("freq_mhz",        freq_mhz);
        entry_w.key_double("azimuth_deg",  azimuth_deg);
        entry_w.key_double("elevation_deg",elev_deg);
        entry_w.key_str("threat_status",   threat_hex);
        entry_w.key_raw("jpro",            jpro);
        entry_w.key_int("rtg_status",      rtg_status);
        if (i > 0) arr += ",";
        arr += entry_w.str();
        ++count;
    }
    arr += "]";
    w.key_raw("tracks", arr);
    if (count < num_tracks)
        w.key_str("parse_warning", "body_truncated_fewer_tracks_than_declared");
}

// ---- RSEC → ECMP Manual: Track Command (Manual) (0x6001) ----
// Body (IRS §5.7.1, pp.57-58), 45 bytes:
//   0–1:  TrackNo    (UINT16, 501-550)
//   2–3:  DOA        (UINT16, ÷10 deg)
//   4–7:  Frequency  (UINT32, KHz)
//   8–11: FrequencyAttributes (UINT32, bit-encoded)
//   12–15: PW        (UINT32, ns)
//   16–19: PRF       (UINT32, Hz)
//   20:   ThreatLevel(UINT8)
//   21–44: JPRONumber(24 bytes)

static void parse_ecmp_manual_start(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 21) { w.key_str("parse_error", "body_too_short"); return; }
    w.key_int("track_no",         load_be16(body + 0));
    w.key_double("doa_deg",       load_be16(body + 2) / 10.0);
    w.key_uint("freq_khz",        load_be32(body + 4));
    w.key_uint("freq_attributes", load_be32(body + 8));
    w.key_uint("pw_ns",           load_be32(body + 12));
    w.key_uint("prf_hz",          load_be32(body + 16));
    w.key_int("threat_level",     body[20]);
    if (body_len >= 45)
        w.key_raw("jpro", decode_jpro(body + 21, body_len - 21));
    else
        w.key_str("parse_warning", "jpro_data_missing_or_truncated");
}

// ---- RSEC → ECMP Manual: Set Forbidden Frequency Bands (0x1120) ----
// Body (IRS §5.7.8, pp.65-66): NumberOfForbiddenFrequencyBands(UINT8, 0-16;
// 0 = remove all existing lockout bands at ES Processor) +
// repeated [StartFrequency(UINT16, MHz, 1000-18000) +
// StopFrequency(UINT16, MHz, 1000-18000)], 'Number of Forbidden Frequency
// Bands' times. Distinct from the ESMP-side Lockout Frequency Bands (0x1005),
// which uses UINT32 KHz fields instead of UINT16 MHz fields.

static void parse_forbidden_bands(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 1) { w.key_str("parse_error", "body_too_short"); return; }
    uint8_t num_bands = body[0];
    w.key_int("num_bands", num_bands);

    std::string arr = "[";
    int count = 0;
    for (int i = 0; i < num_bands && (1 + i * 4 + 4) <= body_len; ++i) {
        const uint8_t* b = body + 1 + i * 4;
        sdfc::JsonWriter entry_w;
        entry_w.key_int("start_freq_mhz", load_be16(b + 0));
        entry_w.key_int("stop_freq_mhz",  load_be16(b + 2));
        if (i > 0) arr += ",";
        arr += entry_w.str();
        ++count;
    }
    arr += "]";
    w.key_raw("bands", arr);
    if (count < num_bands)
        w.key_str("parse_warning", "body_truncated_fewer_bands_than_declared");
}

// ---- RSEC → ECMP Manual: Reset EA Subsystem (0x0FB0) ----
// Body (IRS §5.7.9, p.66): none — pure trigger, no data element table.
// Dispatched directly with no decoder, same as Purge All (0x1011).

// ---- RSEC → ECMP Manual: Set Prohibited Sectors (0x1121) ----
// Body (IRS §5.7.11, pp.67-68): NumberOfSectors(UINT8, 1-4) +
// repeated [EntryID(UINT8, 1-4) + StartAngle(UINT16, ÷10 deg, 0-359.9) +
// StopAngle(UINT16, ÷10 deg, 0-359.9)], 'Number Of Sectors' times.
// Distinct from the ESMP-side Lockout Sectors (0x1006), which has no
// EntryID field.

static void parse_prohibited_sectors(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 1) { w.key_str("parse_error", "body_too_short"); return; }
    uint8_t num_sectors = body[0];
    w.key_int("num_sectors", num_sectors);

    std::string arr = "[";
    int count = 0;
    for (int i = 0; i < num_sectors && (1 + i * 5 + 5) <= body_len; ++i) {
        const uint8_t* s = body + 1 + i * 5;
        uint8_t entry_id   = s[0];
        double  start_deg  = load_be16(s + 1) / 10.0;
        double  stop_deg   = load_be16(s + 3) / 10.0;
        sdfc::JsonWriter entry_w;
        entry_w.key_int("entry_id",    entry_id);
        entry_w.key_double("start_deg",start_deg);
        entry_w.key_double("stop_deg", stop_deg);
        if (i > 0) arr += ",";
        arr += entry_w.str();
        ++count;
    }
    arr += "]";
    w.key_raw("sectors", arr);
    if (count < num_sectors)
        w.key_str("parse_warning", "body_truncated_fewer_sectors_than_declared");
}

// ---- RSEC → ECMP Manual: Update Track (Manual) (0x0FA8) ----
// Body (IRS §5.7.6, Data Element Table, p.63-64): fixed 3-byte header +
// repeated variable-size entries.
//   0-1: EmitterNumber      (UINT16 BE): 501-550, track ID
//   2:   NumberOfParameters (UINT8):     1-12
//   3+:  repeated NumberOfParameters times:
//          ParameterCode  (UINT8):  1-12
//          ParameterValue (size "as applicable" per code — see
//                          update_track_param_size(); not interpreted
//                          here, just raw bytes — parameter semantics are
//                          drs-server's job, not this DLL's)
//
// update_track_param_size() only exists to know how many bytes each
// ParameterValue occupies so the next ParameterCode can be found — it is
// structural (byte framing), not a semantic decode. The per-code byte
// widths come from the field tables used throughout this IRS (§5.7.1/
// §5.7.3), since §5.7.6 itself doesn't spell them out.
// This message was previously not wired into the dispatcher at all (not
// even a raw-hex stub) — command code confirmed against the IRS 2026-07-28.

static int update_track_param_size(uint8_t code) {
    static const int SIZES[] = {0, 2, 4, 4, 4, 4, 4, 1, 1, 2, 1, 2, 24};
    return (code >= 1 && code <= 12) ? SIZES[code] : -1;
}

static void parse_update_track_manual(const uint8_t* body, int /*body_len*/, sdfc::JsonWriter& w) {
    uint16_t emitter_number = load_be16(body + 0);
    uint8_t  num_params     = body[2];
    w.key_int("emitter_number",  emitter_number);
    w.key_int("num_parameters",  num_params);

    std::string arr = "[";
    int off = 3;
    for (int i = 0; i < num_params; ++i) {
        uint8_t code = body[off];
        int size = update_track_param_size(code);
        const uint8_t* v = body + off + 1;

        sdfc::JsonWriter entry_w;
        entry_w.key_int("parameter_code",      code);
        entry_w.key_str("parameter_value_hex", hex_dump(v, size));
        if (i > 0) arr += ",";
        arr += entry_w.str();
        off += 1 + size;
    }
    arr += "]";
    w.key_raw("parameter_updates", arr);
}

// ---- RSEC → ECMP Manual: Change Mode (Manual) (0x0FAE) ----
// Body (IRS §5.7.7, p.64), 1 byte: ModeValue(UINT8): 1=Semi Auto, 2=Manual
// Mode. This message was previously not wired into the dispatcher at all
// (not even a raw-hex stub) — command code confirmed against the IRS
// 2026-07-28.

static void parse_change_mode_manual(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 1) { w.key_str("parse_error", "body_too_short"); return; }
    w.key_int("mode_value", body[0]);
}

// ---- Top-level Variant A dispatcher ----

static char* parse_variant_a_frame(const uint8_t* frame, int frame_len) {
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
        case 0x1509: w.key_str("msg_type", "ack_nack_esmp_to_rsec"); parse_ack_nack(body, blen, w);           break;
        case 0x150A: w.key_str("msg_type", "ack_nack_rsec_to_esmp"); parse_ack_nack(body, blen, w);           break;
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
        case 0x1102: w.key_str("msg_type", "semi_auto_jam");        parse_ecmp_jam(body, blen, w);           break;
        case 0x1103: w.key_str("msg_type", "semi_auto_track_jam");  parse_ecmp_track_and_jam(body, blen, w); break;
        case 0x1104: w.key_str("msg_type", "break_track");          parse_ecmp_break_track(body, blen, w);   break;
        case 0x1105: w.key_str("msg_type", "stop_jam");             parse_ecmp_stop_jam(body, blen, w);      break;
        case 0x1106: w.key_str("msg_type", "change_jpro");          parse_ecmp_change_jpro(body, blen, w);   break;
        // ECMP → RSEC
        case 0x1153: w.key_str("msg_type", "ea_operational_data");  parse_ea_operational_data(body, blen, w); break;
        // RSEC → ECMP manual
        case 0x6001: w.key_str("msg_type", "ecmp_manual_start");    parse_ecmp_manual_start(body, blen, w);   break;
        // §5.7.5 Break Track (Manual) — identical wire shape to semi-auto Break Track (0x1104).
        case 0x0FA4: w.key_str("msg_type", "break_track_manual");   parse_ecmp_break_track(body, blen, w);    break;
        // §5.7.2 Jam Command (Manual) — identical wire shape to semi-auto Jam Command (0x1102).
        case 0x0FA5: w.key_str("msg_type", "jam_command_manual");   parse_ecmp_jam(body, blen, w);            break;
        // §5.7.4 Stop Jam (Manual) — identical wire shape to semi-auto Stop Jam (0x1105).
        case 0x0FA6: w.key_str("msg_type", "stop_jam_manual");      parse_ecmp_stop_jam(body, blen, w);       break;
        // §5.7.3 Track and Jam Command (Manual) — identical wire shape to semi-auto (0x1103).
        case 0x0FA7: w.key_str("msg_type", "track_and_jam_manual"); parse_ecmp_track_and_jam(body, blen, w);  break;
        // §5.7.6 Update Track (Manual) — was not wired in at all before 2026-07-28.
        case 0x0FA8: w.key_str("msg_type", "update_track_manual");  parse_update_track_manual(body, blen, w); break;
        // §5.7.9 Reset EA Subsystem — no data element table, trigger-only (like Purge All).
        case 0x0FB0: w.key_str("msg_type", "reset_ea_subsystem");                                            break;
        // §5.7.7 Change Mode (Manual) — was not wired in at all before 2026-07-28.
        case 0x0FAE: w.key_str("msg_type", "change_mode_manual");   parse_change_mode_manual(body, blen, w);  break;
        // §5.7.8 Set Forbidden Frequency Bands — distinct shape from ESMP-side Lockout Bands (0x1005).
        case 0x1120: w.key_str("msg_type", "set_forbidden_bands");  parse_forbidden_bands(body, blen, w);     break;
        // §5.7.10 Platform Heading Data to EA Processor — was not wired in at all before 2026-07-28;
        // same 2-byte ÷10-deg shape as the ESMP-side Platform Heading (0x1014).
        case 0x1108: w.key_str("msg_type", "platform_heading_to_ea"); parse_platform_heading(body, blen, w);  break;
        // §5.7.11 Set Prohibited Sectors — distinct shape from ESMP-side Lockout Sectors (0x1006): adds EntryID.
        case 0x1121: w.key_str("msg_type", "set_prohibited_sectors"); parse_prohibited_sectors(body, blen, w); break;
        // §5.7.12 ECM operational status — identical wire shape to EA Operational Data Semi (0x1153).
        case 0x111D: w.key_str("msg_type", "ecm_operational_status"); parse_ea_operational_data(body, blen, w); break;
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
// Body layout (IRS §5.4.1, Data Element Table, p.37): fixed 3 bytes.
//   0: SFBValue     (UINT8): 1=SFB1(2.2-18GHz), 2=SFB2(2.5-18GHz),
//                            3=SFB3(4-18GHz), 4=SFB4(6-18GHz)
//   1: ScanQuadrant  (UINT8): bit-encoded, bit0-3 = quadrants 1-4, bit4-7 unused
//   2: Threshold     (UINT8): ×-1 = dBm

static void parse_bb_sfb_selection(const uint8_t* body, int /*body_len*/, sdfc::JsonWriter& w) {
    uint8_t sfb_value     = body[0];
    uint8_t scan_quadrant = body[1];
    uint8_t threshold_raw = body[2];

    w.key_int("sfb_value",           sfb_value);
    w.key_int("scan_quadrant",       scan_quadrant);
    w.key_int("threshold_dbm",       -static_cast<long long>(threshold_raw));
}

// ---- RSEC → BB Rx: RF Sector Blank (CmdUnitID=0x1127) ----
// Body layout (IRS §5.4.2, Data Element Table, p.38): fixed 1 byte.
//   0: RFSectorValue (UINT8): quadrant selection for sector blanking —
//      Q1(0-90deg)/Q2(90-180deg)/Q3(180-270deg)/Q4(270-360deg).
//      NOTE: table prints "Range 0-360" for a 1-byte field, which can't fit;
//      Description frames this as Q1-Q4 quadrant selection, same shape as
//      0x1126's ScanQuadrant bitmask — treated as a bitmask/code here, not
//      a literal degree value. Emitted as a plain int, no bit-by-bit split.

static void parse_bb_sector_blank(const uint8_t* body, int /*body_len*/, sdfc::JsonWriter& w) {
    uint8_t rf_sector_value = body[0];
    w.key_int("rf_sector_value", rf_sector_value);
}

// ---- RSEC → BB Rx: CAL ON/OFF (CmdUnitID=0x1128) ----
// Body: Enable(UINT8): 0=off, 1=on

static void parse_bb_cal(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 1) { w.key_str("parse_error", "body_too_short"); return; }
    w.key_bool("calibration_enabled", body[0] != 0);
}

// ---- RSEC → RFPS: IQ Data Logging (CmdUnitID=0x1009) ----
// Body (IRS §5.8.1, p.70-71), 9 bytes:
//   0–1: TrackFrequency (UINT16, MHz, 1000-40000): band value to log
//   2:   RFAttenuation  (UINT8, dB, 0-30)
//   3:   IFAttenuation  (UINT8, dB, 0-30)
//   4:   Threshold      (UINT8, dB, 30-95 -> -95 to -30)
//   5:   IFBWSelection  (UINT8): 1=+/-500MHz, 2=+/-20MHz
//   6:   Sector         (UINT8, 1-4)
//   7–8: NumOfPulses    (UINT16, 0-10000): pulses to record for IQ logging

static void parse_rfps_iq_logging(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 9) { w.key_str("parse_error", "body_too_short"); return; }
    w.key_uint("track_frequency_mhz", load_be16(body + 0));
    w.key_int("rf_attenuation_db",    body[2]);
    w.key_int("if_attenuation_db",    body[3]);
    w.key_int("threshold_db",         body[4]);
    w.key_int("if_bw_selection",      body[5]);
    w.key_int("sector",               body[6]);
    w.key_uint("num_of_pulses",       load_be16(body + 7));
}

// ---- RFPS → RSEC: IQ Logging Response (CmdUnitID=0x1076) ----
// Body: Result(UINT8): 0=success, NumSamples(UINT32)

static void parse_rfps_iq_response(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 5) { w.key_str("parse_error", "body_too_short"); return; }
    w.key_int("result",       body[0]);
    w.key_uint("num_samples", load_be32(body + 1));
}

// ---- RFPS → RSEC: Finger Printing Response (CmdUnitID=0x3507) ----
// Body (IRS §5.9.2, pp.72-78), 165 bytes. All multi-byte fields BE.
// NOTE: the IRS's own Name/Identifier list also mentions a "PhaseCode" string
// field between PhaseCode_Length and PhaseCode_ChipTime, but the IRS's typed
// Data Element Table never gives it a byte size — it is omitted here per the
// typed table; flag to BEL/DLRL if the real wire format does include it.
//   0–1:   ESMTrackNumber        (UINT16, 1-500)
//   2–3:   RFPSTrackNumber       (UINT16, 1-500)
//   4:     LibraryMatchFlag      (UINT8, 0-1)
//   5–6:   DatabaseServerStatus  (UINT16)
//   7–26:  CarrierName           (20B ASCII)
//   27–46: CarrierClass          (20B ASCII)
//   47–66: RadarName             (20B ASCII)
//   67–76: RadarMode             (10B ASCII)
//   77:    ConfidenceLevel       (UINT8, 0-100)
//   78:    TypeOfEmitterSignal   (UINT8)
//   79:    TypeOfFrequencyAgility(UINT8)
//   80:    TypeOfPWAgility       (UINT8)
//   81:    TypeOfModulation      (UINT8)
//   82:    TypeOfPRI             (UINT8)
//   83:    TypeOfScan            (UINT8)
//   84–87: FrequencyMin          (UINT32, MHz)
//   88–91: FrequencyMax          (UINT32, MHz)
//   92–95: FrequencyAvg          (UINT32, MHz)
//   96–99: PRIMin                (UINT32, usec)
//   100–103: PRIMax              (UINT32, usec)
//   104–107: PRIAvg              (UINT32, usec)
//   108–111: PRFAvg              (UINT32, Hz)
//   112–115: PWMin               (UINT32, nsec)
//   116–119: PWMax               (UINT32, nsec)
//   120–123: PWAvg               (UINT32, nsec)
//   124–127: RiseTime            (UINT32, nsec)
//   128–131: FallTime            (UINT32, nsec)
//   132:   NumAgileFreq          (UINT8)
//   133:   NumPRILevels          (UINT8)
//   134–137: JitterPercentage    (UINT32)
//   138–141: ChirpRateFMModFreq  (UINT32, MHz/usec x 0.0001)
//   142–143: PhaseCodeLength     (UINT16)
//   144–147: PhaseCodeChipTime   (UINT32, nsec)
//   148:   NumFreqSteps          (UINT8)
//   149–152: SteppedFMMinFreq    (UINT32, MHz)
//   153–156: SteppedFMMaxFreq    (UINT32, MHz)
//   157–160: SteppedFMBitTime    (UINT32, nsec)
//   161–164: FMDeviation         (UINT32, MHz)

static void parse_rfps_fingerprint_response(const uint8_t* body, int body_len, sdfc::JsonWriter& w) {
    if (body_len < 165) { w.key_str("parse_error", "body_too_short"); return; }

    auto ascii_field = [&](int offset, int len, const char* key) {
        char buf[24] = {};
        std::memcpy(buf, body + offset, static_cast<size_t>(len));
        buf[len] = '\0';
        w.key_str(key, buf);
    };

    w.key_int("esm_track_number",   load_be16(body + 0));
    w.key_int("rfps_track_number",  load_be16(body + 2));
    w.key_int("library_match_flag", body[4]);
    w.key_int("database_server_status", load_be16(body + 5));
    ascii_field(7,  20, "carrier_name");
    ascii_field(27, 20, "carrier_class");
    ascii_field(47, 20, "radar_name");
    ascii_field(67, 10, "radar_mode");
    w.key_int("confidence_level",         body[77]);
    w.key_int("type_of_emitter_signal",   body[78]);
    w.key_int("type_of_frequency_agility",body[79]);
    w.key_int("type_of_pw_agility",       body[80]);
    w.key_int("type_of_modulation",       body[81]);
    w.key_int("type_of_pri",              body[82]);
    w.key_int("type_of_scan",             body[83]);
    w.key_uint("freq_min_mhz",   load_be32(body + 84));
    w.key_uint("freq_max_mhz",   load_be32(body + 88));
    w.key_uint("freq_avg_mhz",   load_be32(body + 92));
    w.key_uint("pri_min_us",     load_be32(body + 96));
    w.key_uint("pri_max_us",     load_be32(body + 100));
    w.key_uint("pri_avg_us",     load_be32(body + 104));
    w.key_uint("prf_avg_hz",     load_be32(body + 108));
    w.key_uint("pw_min_ns",      load_be32(body + 112));
    w.key_uint("pw_max_ns",      load_be32(body + 116));
    w.key_uint("pw_avg_ns",      load_be32(body + 120));
    w.key_uint("rise_time_ns",   load_be32(body + 124));
    w.key_uint("fall_time_ns",   load_be32(body + 128));
    w.key_int("num_agile_freq",  body[132]);
    w.key_int("num_pri_levels",  body[133]);
    w.key_uint("jitter_percentage",      load_be32(body + 134));
    w.key_uint("chirp_rate_fm_mod_freq", load_be32(body + 138));
    w.key_int("phase_code_length",       load_be16(body + 142));
    w.key_uint("phase_code_chip_time_ns",load_be32(body + 144));
    w.key_int("num_freq_steps",          body[148]);
    w.key_uint("stepped_fm_min_freq_mhz",load_be32(body + 149));
    w.key_uint("stepped_fm_max_freq_mhz",load_be32(body + 153));
    w.key_uint("stepped_fm_bit_time_ns", load_be32(body + 157));
    w.key_uint("fm_deviation_mhz",       load_be32(body + 161));
}

// ---- Top-level Variant B dispatcher ----

static char* parse_variant_b_frame(const uint8_t* frame, int frame_len) {
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

static char* parse_variant_c_frame(const uint8_t* frame, int frame_len) {
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

    // SCU angle fields use BAM (binary angle measurement) encoding:
    // resolution 0.0054 deg/LSB over a UINT16 == 360.0/65536.0 deg/count.
    // This differs from the tenths-of-a-degree (÷10) encoding used by every
    // other RSEC angle field (DOA, Start/Stop Angle, Platform Heading, etc.).
    static constexpr double SCU_ANGLE_LSB_DEG = 360.0 / 65536.0;

    switch (cmd_code) {
        case SCU_CMD_POSITION_SLEW:
            w.key_str("msg_type", "scu_position_slew");
            // Data (IRS §5.11.1, p.82-83): PositionAngle(UINT16 BE, BAM, 0-359.99deg).
            // Single field only — the servo has one rotation axis, no elevation.
            if (n >= 2)
                w.key_double("position_angle_deg", load_be16(data + 0) * SCU_ANGLE_LSB_DEG);
            break;

        case SCU_CMD_SPIN:
            w.key_str("msg_type", "scu_spin");
            // Data (IRS §5.11.2, p.83): Speed(UINT8, rpm, 1-200) + Direction(UINT8: 0x00=CW, 0xFF=CCW)
            if (n >= 2) {
                w.key_int("speed_rpm", data[0]);
                w.key_int("direction", data[1]);
            }
            break;

        case SCU_CMD_SECTOR_SCAN:
            w.key_str("msg_type", "scu_sector_scan");
            // Data (IRS §5.11.3, p.84): StartAngle(UINT16 BAM) + StopAngle(UINT16 BAM)
            // + SpeedReserved1(1B) + DirectionReserved2(1B). The IRS's own field
            // table copies Speed/Direction semantics onto these last two bytes,
            // but names them "Reserved" — surfaced as spare bytes, not decoded
            // as speed/direction, pending ICD clarification.
            if (n >= 6) {
                w.key_double("start_angle_deg", load_be16(data + 0) * SCU_ANGLE_LSB_DEG);
                w.key_double("stop_angle_deg",  load_be16(data + 2) * SCU_ANGLE_LSB_DEG);
                w.key_str("reserved_bytes_hex", hex_dump(data + 4, 2));
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
            // Data (IRS §5.12.1, p.86-87), 4 bytes. No elevation field exists.
            //   +0 FeedbackCode (UINT8, packed bits):
            //        bit0 = BIT command checksum, bit1 = BIT command received,
            //        bit2 = BIT readiness for operation, bits3-5 = reserved,
            //        bits6-7 = Current SCU Status (00=emergency stop,10=manual,11=auto)
            //   +1 ServoStatus (UINT8, packed bits):
            //        bit0 = encoder OK, bit1 = amplifier OK, bits2-7 = reserved
            //   +2 Azimuth (UINT16 BE, BAM, 0-360 deg)
            if (n >= 4) {
                uint8_t feedback_code = data[0];
                uint8_t servo_status  = data[1];
                w.key_bool("bit_cmd_checksum_ok",   (feedback_code & 0x01) != 0);
                w.key_bool("bit_cmd_received",      (feedback_code & 0x02) != 0);
                w.key_bool("bit_ready_for_operation",(feedback_code & 0x04) != 0);
                w.key_int("current_scu_status", (feedback_code >> 6) & 0x03);
                w.key_bool("servo_encoder_ok",   (servo_status & 0x01) != 0);
                w.key_bool("servo_amplifier_ok", (servo_status & 0x02) != 0);
                w.key_double("azimuth_deg", load_be16(data + 2) * SCU_ANGLE_LSB_DEG);
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

static char* parse_variant_d_frame(const uint8_t* frame, int frame_len) {
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
