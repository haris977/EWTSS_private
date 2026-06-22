// drs-bridge/parsers/esme/src/esme_parser_monvuhf.cpp
//
// R&S ESME MONUV (HF/VU) mass data parser DLL.
// ICD: R&S User Manual 4113.0075.02-03, Chapter 7 (Mass data output)
//
// Two parallel binary protocols, distinguished by the MagicWord at frame start:
//   EB200  0x000EB200 (big-endian)  — TCP 5565 default, or UDP bridge-chosen port
//   AMMOS  0xFB746572 (little-endian) — AIF I/Q data stream
// Control channel (not a data stream):
//   SCPI text lines, \n-terminated  — TCP 5555
//
// Frame type mapping (returned by extract_frame):
//   1 = SCPI command  (bridge → ESME, text line)
//   2 = SCPI response (ESME → bridge, text line)
//   3 = EB200 binary datagram (ESME → bridge, big-endian)
//   4 = AMMOS AIF binary frame (ESME → bridge, little-endian)
//
// Endianness rule (per §7.6 "EB200 protocol", §7.7 "AMMOS IF data stream"):
//   EB200 path : load_u16be / load_u32be / load_u64be
//   AMMOS path : load_u16le / load_u32le / load_u64le
//   SCPI       : UTF-8 text, no byte swap
//
// CRITICAL for RDFS bearing display: DFPan (trace tag 1401) optional header
// contains Azimuth + Level per measurement.  The PeriodicTraceData also carries
// per-step arrays gated by SelectorFlags (AZIMUTH=0x1000, DF_LEVEL=0x0800).
// Always use OptionalHeaderLength to locate PeriodicTraceData — never hardcode.

#include "sdfc_abi.h"
#include "json_writer.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>
#include <algorithm>

using namespace sdfc;

// ---------------------------------------------------------------------------
// Protocol constants
// ---------------------------------------------------------------------------

static constexpr uint32_t EB200_MAGIC = 0x000EB200u;  // BE bytes: 00 0E B2 00
static constexpr uint32_t AMMOS_MAGIC = 0xFB746572u;  // LE bytes: 72 65 74 FB

// EB200 header (§7.6.1 Table 7-2): 16 bytes fixed
static constexpr int EB200_HDR_BYTES = 16;

// GenericAttribute conventional (Table 7-3): Tag(2) + Length(2) = 4 bytes prefix
static constexpr int GA_CONV_HDR = 4;
// GenericAttribute advanced (Table 7-4): Tag(2)+res(2)+Length(4)+4×UINT32 res = 24 bytes
static constexpr int GA_ADV_HDR  = 24;

// TraceAttribute conventional (Table 7-6): N(2)+Ch(1)+OptLen(1)+Flags(4) = 8 bytes
static constexpr int TA_CONV_HDR = 8;
// TraceAttribute advanced (Table 7-7): N(4)+Ch(4)+OptLen(4)+FLo(4)+FHi(4)+4×UINT32 = 36 bytes
static constexpr int TA_ADV_HDR  = 36;

// Tags >= 5000 use advanced GenericAttribute / TraceAttribute format
static constexpr uint16_t TAG_ADVANCED_THRESHOLD = 5000u;

// AMMOS AIF: FrameHeader = 6 × 32-bit words (Table 7-40)
static constexpr int AMMOS_FRAME_HDR_WORDS = 6;
static constexpr int AMMOS_FRAME_HDR_BYTES = 24;
// AMMOS AIF: DataHeader = 19 × 32-bit words (Table 7-41)
static constexpr int AMMOS_DATA_HDR_WORDS  = 19;
static constexpr int AMMOS_DATA_HDR_BYTES  = 76;

// Trace tag values (§7.6 Table 7-5)
static constexpr uint16_t TAG_FSCAN      = 101u;
static constexpr uint16_t TAG_MSCAN      = 201u;
static constexpr uint16_t TAG_AUDIO      = 401u;
static constexpr uint16_t TAG_IFPAN      = 501u;
static constexpr uint16_t TAG_CW         = 801u;
static constexpr uint16_t TAG_IF         = 901u;
static constexpr uint16_t TAG_VIDEO      = 1001u;
static constexpr uint16_t TAG_VDPAN      = 1101u;
static constexpr uint16_t TAG_PSCAN      = 1201u;
static constexpr uint16_t TAG_SELCALL    = 1301u;
static constexpr uint16_t TAG_DFPAN      = 1401u;  // direction-finding — RDFS bearing
static constexpr uint16_t TAG_PIFPAN     = 1601u;
static constexpr uint16_t TAG_GPSCOMPASS = 1801u;
static constexpr uint16_t TAG_FMTRIGGER  = 2201u;
static constexpr uint16_t TAG_ZSPAN      = 2401u;
static constexpr uint16_t TAG_HRPAN      = 5601u;  // advanced (>= 5000)

// SelectorFlags (§7.6.3 Table 7-8) — which fields appear in PeriodicTraceData
// Sequence of data in PeriodicTraceData follows ascending flag order.
static constexpr uint32_t SEL_LEVEL           = 0x00000001u;  // INT16 [1/10 dBµV]
static constexpr uint32_t SEL_OFFSET          = 0x00000002u;  // INT32 [Hz]
static constexpr uint32_t SEL_FSTRENGTH       = 0x00000004u;  // INT16 [1/10 dBµV/m]
static constexpr uint32_t SEL_AM              = 0x00000008u;  // INT16 [1/10 %]
static constexpr uint32_t SEL_AM_POS          = 0x00000010u;  // INT16 [1/10 %]
static constexpr uint32_t SEL_AM_NEG          = 0x00000020u;  // INT16 [1/10 %]
static constexpr uint32_t SEL_FM              = 0x00000040u;  // INT32 [Hz]
static constexpr uint32_t SEL_FM_POS          = 0x00000080u;  // INT32 [Hz]
static constexpr uint32_t SEL_FM_NEG          = 0x00000100u;  // INT32 [Hz]
static constexpr uint32_t SEL_PM              = 0x00000200u;  // INT16 [1/100 rad]
static constexpr uint32_t SEL_BANDWIDTH       = 0x00000400u;  // INT32 [Hz]
static constexpr uint32_t SEL_DF_LEVEL        = 0x00000800u;  // INT16 [1/10 dBµV]
static constexpr uint32_t SEL_AZIMUTH         = 0x00001000u;  // INT16 [1/10 °] — BEARING
static constexpr uint32_t SEL_DF_QUALITY      = 0x00002000u;  // INT16 [1/10 %]
static constexpr uint32_t SEL_DF_FSTRENGTH    = 0x00004000u;  // INT16 [1/10 dBµV/m]
static constexpr uint32_t SEL_CHANNEL         = 0x00010000u;  // INT16
static constexpr uint32_t SEL_FREQ_LOW        = 0x00020000u;  // UINT32 [Hz]
static constexpr uint32_t SEL_ELEVATION       = 0x00040000u;  // INT16 [1/10 °]
static constexpr uint32_t SEL_DF_OMNIPHASE    = 0x00100000u;  // UINT32 [1/10 °]
static constexpr uint32_t SEL_FREQ_HIGH       = 0x00200000u;  // UINT32 [Hz × 2³²]
static constexpr uint32_t SEL_BANDWIDTH_CTR   = 0x00400000u;  // INT32 [Hz]
static constexpr uint32_t SEL_FREQ_OFFSET_REL = 0x00800000u;  // INT32 [fractional]
static constexpr uint32_t SEL_SWAP            = 0x02000000u;  // PeriodicTraceData byte-reversed
static constexpr uint32_t SEL_OPTIONAL_HEADER = 0x80000000u;  // OptionalHeader present

// ---------------------------------------------------------------------------
// Big-endian load helpers (EB200 path)
// ---------------------------------------------------------------------------

static uint16_t load_u16be(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}
static int16_t load_i16be(const uint8_t* p) {
    return static_cast<int16_t>(load_u16be(p));
}
static uint32_t load_u32be(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) <<  8) |
            static_cast<uint32_t>(p[3]);
}
static int32_t load_i32be(const uint8_t* p) {
    return static_cast<int32_t>(load_u32be(p));
}
static uint64_t load_u64be(const uint8_t* p) {
    return (static_cast<uint64_t>(load_u32be(p)) << 32) |
            static_cast<uint64_t>(load_u32be(p + 4));
}
static float load_f32be(const uint8_t* p) {
    uint32_t bits = load_u32be(p);
    float v; memcpy(&v, &bits, 4);
    return v;
}

// ---------------------------------------------------------------------------
// Little-endian load helpers (AMMOS path)
// ---------------------------------------------------------------------------

static uint32_t load_u32le(const uint8_t* p) {
    return  static_cast<uint32_t>(p[0])        |
           (static_cast<uint32_t>(p[1]) <<  8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
static int32_t load_i32le(const uint8_t* p) {
    return static_cast<int32_t>(load_u32le(p));
}

// ---------------------------------------------------------------------------
// Enum-to-string helpers
// ---------------------------------------------------------------------------

static const char* trace_tag_name(uint16_t tag) {
    switch (tag) {
        case TAG_FSCAN:      return "fscan";
        case TAG_MSCAN:      return "mscan";
        case TAG_AUDIO:      return "audio";
        case TAG_IFPAN:      return "ifpan";
        case TAG_CW:         return "cw";
        case TAG_IF:         return "if_iq";
        case TAG_VIDEO:      return "video";
        case TAG_VDPAN:      return "vdpan";
        case TAG_PSCAN:      return "pscan";
        case TAG_SELCALL:    return "selcall";
        case TAG_DFPAN:      return "dfpan";
        case TAG_PIFPAN:     return "pifpan";
        case TAG_GPSCOMPASS: return "gps_compass";
        case TAG_FMTRIGGER:  return "fmt_rigger";
        case TAG_ZSPAN:      return "zspan";
        case TAG_HRPAN:      return "hrpan";
        default:             return "unknown";
    }
}

static const char* compass_type_str(int16_t v) {
    switch (v) {
        case 0: return "unknown";
        case 1: return "uncomp";
        case 2: return "mag_north";
        case 3: return "true_north";
        case 4: return "bad";
        case 5: return "cog";
        case 6: return "cog_slow";
        default: return "invalid";
    }
}

// ---------------------------------------------------------------------------
// GPS header decoder (Table 7-33, 24 bytes, big-endian)
// Shared by DFPanTraceHeader (Table 7-27) and GPSCompass sample data.
// ---------------------------------------------------------------------------

static void decode_gps_header(const uint8_t* h, int avail, JsonWriter& j) {
    if (avail < 24) { j.key_str("gps_warning", "truncated"); return; }
    int16_t gps_valid  = load_i16be(h +  0);
    int16_t n_sat      = load_i16be(h +  2);
    int16_t lat_ref    = load_i16be(h +  4);  // 0x004E='N', 0x0053='S'
    int16_t lat_deg    = load_i16be(h +  6);
    float   lat_min    = load_f32be(h +  8);
    int16_t lon_ref    = load_i16be(h + 12);  // 0x0045='E', 0x0057='W'
    int16_t lon_deg    = load_i16be(h + 14);
    float   lon_min    = load_f32be(h + 16);
    float   dilution   = load_f32be(h + 20);

    j.key_int   ("gps_valid",      gps_valid);
    j.key_int   ("gps_sats",       n_sat);
    j.key_int   ("lat_ref",        lat_ref);
    j.key_int   ("lat_deg",        lat_deg);
    j.key_double("lat_min",        static_cast<double>(lat_min));
    j.key_int   ("lon_ref",        lon_ref);
    j.key_int   ("lon_deg",        lon_deg);
    j.key_double("lon_min",        static_cast<double>(lon_min));
    j.key_double("gps_dilution",   static_cast<double>(dilution));
}

// ---------------------------------------------------------------------------
// DFPan optional header decoder
// Table 7-27 (page 603) + continuation (page 604).
// Full header = 128 bytes (firmware >= VersionMinor 0x70).
// Earlier firmware: 96 bytes (ends at StepFreqDenominator).
// Always guard with actual oh_bytes for forward compatibility.
//
// Byte map (all big-endian):
//   0:  UINT32 Freq_low
//   4:  UINT32 Freq_high        → freq_hz = (Freq_high<<32)|Freq_low
//   8:  UINT32 FreqSpan
//  12:  INT32  DFThresholdMode  (0=OFF, 1=GATE, 2=NORM)
//  16:  INT32  DFThresholdValue [dBµV]
//  20:  UINT32 DFBandwidth [Hz]
//  24:  UINT32 StepWidth [Hz]
//  28:  UINT32 DFMeasureTime [µs]
//  32:  INT32  DFOption         (bit0: 1=DF possible)
//  36:  UINT16 CompassHeading   [1/10 °]
//  38:  INT16  CompassHeadingType
//  40:  INT32  AntennaFactor    [1/10 dB/m]
//  44:  INT32  DemodFreqChannel
//  48:  UINT32 DemodFreq_low
//  52:  UINT32 DemodFreq_high
//  56:  UINT64 OutputTimestamp  [ns, no leap secs]
//  64:  24 bytes GPSHeader (Table 7-33)
//  88:  UINT32 StepFreqNumerator [Hz]
//  92:  UINT32 StepFreqDenominator
//  96:  UINT64 DFBandwidthHighRes [mHz]  (>= v0x53)
// 104:  INT16  Level  [1/10 dBµV]        (>= v0x53)
// 106:  INT16  Azimuth [1/10 °]          (>= v0x53) — per-header bearing summary
// 108:  INT16  Quality [1/10 %]
// 110:  INT16  Elevation [1/10 °]
// 112:  INT16  Omniphase [1/10 °]
// 114:  6 bytes reserved
// 120:  UINT64 MeasureTimestamp [ns, no leap secs] (>= v0x70)
// ---------------------------------------------------------------------------

static void decode_dfpan_opt_header(const uint8_t* oh, int oh_bytes, JsonWriter& j) {
    if (oh_bytes >= 8) {
        uint32_t f_lo = load_u32be(oh + 0);
        uint32_t f_hi = load_u32be(oh + 4);
        uint64_t f_hz = (static_cast<uint64_t>(f_hi) << 32) | f_lo;
        j.key_int("df_center_freq_hz", static_cast<long long>(f_hz));
    }
    if (oh_bytes >= 12)  j.key_uint("df_freq_span_hz",          load_u32be(oh +  8));
    if (oh_bytes >= 16)  j.key_int ("df_threshold_mode",        load_i32be(oh + 12));
    if (oh_bytes >= 20)  j.key_int ("df_threshold_dbuv",        load_i32be(oh + 16));
    if (oh_bytes >= 24)  j.key_uint("df_bandwidth_hz",          load_u32be(oh + 20));
    if (oh_bytes >= 28)  j.key_uint("df_step_width_hz",         load_u32be(oh + 24));
    if (oh_bytes >= 32)  j.key_uint("df_measure_time_us",       load_u32be(oh + 28));
    if (oh_bytes >= 36)  j.key_int ("df_option",                load_i32be(oh + 32));
    if (oh_bytes >= 40) {
        j.key_double("compass_heading_deg", load_u16be(oh + 36) / 10.0);
        j.key_str   ("compass_type",        compass_type_str(load_i16be(oh + 38)));
    }
    if (oh_bytes >= 44)  j.key_int ("df_antenna_factor_01dbm",  load_i32be(oh + 40));
    if (oh_bytes >= 48)  j.key_int ("demod_freq_ch",            load_i32be(oh + 44));
    if (oh_bytes >= 56) {
        uint32_t d_lo = load_u32be(oh + 48);
        uint32_t d_hi = load_u32be(oh + 52);
        uint64_t d_hz = (static_cast<uint64_t>(d_hi) << 32) | d_lo;
        j.key_int("demod_freq_hz", static_cast<long long>(d_hz));
    }
    if (oh_bytes >= 64)  j.key_int("output_ts_ns", static_cast<long long>(load_u64be(oh + 56)));
    if (oh_bytes >= 88)  decode_gps_header(oh + 64, oh_bytes - 64, j);
    if (oh_bytes >= 92)  j.key_uint("step_freq_num_hz",  load_u32be(oh + 88));
    if (oh_bytes >= 96)  j.key_uint("step_freq_den",     load_u32be(oh + 92));
    // Extended fields (VersionMinor >= 0x53)
    if (oh_bytes >= 104) j.key_int("df_bw_highres_mhz",  static_cast<long long>(load_u64be(oh + 96)));
    if (oh_bytes >= 106) j.key_int("df_level_01dbuv",    load_i16be(oh + 104));
    if (oh_bytes >= 108) j.key_int("df_azimuth_01deg",   load_i16be(oh + 106));
    if (oh_bytes >= 110) j.key_int("df_quality_01pct",   load_i16be(oh + 108));
    if (oh_bytes >= 112) j.key_int("df_elevation_01deg", load_i16be(oh + 110));
    if (oh_bytes >= 114) j.key_int("df_omniphase_01deg", load_i16be(oh + 112));
    // Extended fields (VersionMinor >= 0x70)
    if (oh_bytes >= 128) j.key_int("df_measure_ts_ns",   static_cast<long long>(load_u64be(oh + 120)));
}

// ---------------------------------------------------------------------------
// FScan optional header decoder (Table 7-9, 48 bytes max, big-endian)
// Same layout used for MScan (Table 7-11 is a subset).
//
//  0: INT16 CycleCount
//  2: INT16 HoldTime [ms]
//  4: INT16 DwellTime [ms]
//  6: INT16 DirectionUp (0=decreasing, 1=increasing)
//  8: INT16 StopSignal (0=off, 1=on)
// 10: UINT32 StartFreq_low [Hz]
// 14: UINT32 StopFreq_low  [Hz]
// 18: UINT32 StepFreq      [Hz]
// 22: UINT32 StartFreq_high [Hz × 2³²]
// 26: UINT32 StopFreq_high  [Hz × 2³²]
// 30: 2 bytes reserved
// 32: UINT64 OutputTimestamp [ns, no leap secs]
// 40: INT16 Detector1–4 (4 × 2 bytes)
// ---------------------------------------------------------------------------

static void decode_fscan_opt_header(const uint8_t* oh, int oh_bytes, JsonWriter& j) {
    if (oh_bytes < 10) return;
    j.key_int ("cycle_count",   load_i16be(oh + 0));
    j.key_int ("hold_time_ms",  load_i16be(oh + 2));
    j.key_int ("dwell_time_ms", load_i16be(oh + 4));
    j.key_int ("dir_up",        load_i16be(oh + 6));
    j.key_int ("stop_signal",   load_i16be(oh + 8));
    if (oh_bytes >= 30) {
        uint32_t s_lo = load_u32be(oh + 10);
        uint32_t e_lo = load_u32be(oh + 14);
        uint32_t step = load_u32be(oh + 18);
        uint32_t s_hi = load_u32be(oh + 22);
        uint32_t e_hi = load_u32be(oh + 26);
        uint64_t start_hz = (static_cast<uint64_t>(s_hi) << 32) | s_lo;
        uint64_t stop_hz  = (static_cast<uint64_t>(e_hi) << 32) | e_lo;
        j.key_int ("start_freq_hz", static_cast<long long>(start_hz));
        j.key_int ("stop_freq_hz",  static_cast<long long>(stop_hz));
        j.key_uint("step_freq_hz",  step);
    }
    if (oh_bytes >= 40) {
        j.key_int("output_ts_ns", static_cast<long long>(load_u64be(oh + 32)));
    }
}

// ---------------------------------------------------------------------------
// IFPan optional header decoder (Table 7-17, 96 bytes max, big-endian)
//
//  0: UINT32 Freq_low  [Hz]
//  4: UINT32 FreqSpan  [Hz]
//  8: INT16  AvgTime   (unused, always 0)
// 10: INT16  AvgType1
// 12: UINT32 MeasureTime [µs]
// 16: UINT32 Freq_high [Hz × 2³²]
// 32: UINT64 OutputTimestamp [ns, no leap secs]
// ---------------------------------------------------------------------------

static void decode_ifpan_opt_header(const uint8_t* oh, int oh_bytes, JsonWriter& j) {
    if (oh_bytes < 8) return;
    uint32_t freq_lo   = load_u32be(oh +  0);
    uint32_t freq_span = load_u32be(oh +  4);
    j.key_uint("freq_span_hz", freq_span);
    if (oh_bytes >= 20) {
        uint32_t freq_hi = load_u32be(oh + 16);
        uint64_t freq_hz = (static_cast<uint64_t>(freq_hi) << 32) | freq_lo;
        j.key_int("center_freq_hz", static_cast<long long>(freq_hz));
    } else {
        j.key_uint("center_freq_lo_hz", freq_lo);
    }
    if (oh_bytes >= 40) {
        j.key_int("output_ts_ns", static_cast<long long>(load_u64be(oh + 32)));
    }
}

// ---------------------------------------------------------------------------
// PScan optional header decoder (Table 7-12, 70 bytes max, big-endian)
//
//  0: UINT32 StartFreq_low [Hz]
//  4: UINT32 StopFreq_low  [Hz]
//  8: UINT32 StepFreq      [Hz]
// 12: UINT32 StartFreq_high [Hz × 2³²]
// 16: UINT32 StopFreq_high  [Hz × 2³²]
// 20: 4 bytes reserved
// 24: UINT64 OutputTimestamp [ns, no leap secs]
// ---------------------------------------------------------------------------

static void decode_pscan_opt_header(const uint8_t* oh, int oh_bytes, JsonWriter& j) {
    if (oh_bytes < 20) return;
    uint32_t s_lo = load_u32be(oh +  0);
    uint32_t e_lo = load_u32be(oh +  4);
    uint32_t step = load_u32be(oh +  8);
    uint32_t s_hi = load_u32be(oh + 12);
    uint32_t e_hi = load_u32be(oh + 16);
    uint64_t start_hz = (static_cast<uint64_t>(s_hi) << 32) | s_lo;
    uint64_t stop_hz  = (static_cast<uint64_t>(e_hi) << 32) | e_lo;
    j.key_int ("start_freq_hz", static_cast<long long>(start_hz));
    j.key_int ("stop_freq_hz",  static_cast<long long>(stop_hz));
    j.key_uint("step_freq_hz",  step);
    if (oh_bytes >= 32) {
        j.key_int("output_ts_ns", static_cast<long long>(load_u64be(oh + 24)));
    }
}

// ---------------------------------------------------------------------------
// GPSCompass sample decoder (Table 7-32, per sample, big-endian)
// Called from decode_periodic_data for TAG_GPSCOMPASS.
// ---------------------------------------------------------------------------

static void decode_gpscompass_sample(const uint8_t* s, int avail, JsonWriter& j) {
    if (avail < 4) return;
    j.key_double("compass_heading_deg", load_u16be(s + 0) / 10.0);
    j.key_str   ("compass_type",        compass_type_str(load_i16be(s + 2)));
    // GPSHeader at offset 4 (24 bytes)
    if (avail >= 28) decode_gps_header(s + 4, avail - 4, j);
}

// ---------------------------------------------------------------------------
// PeriodicTraceData decoder
//
// Walks SelectorFlags in ascending bit order; each enabled flag contributes
// an array of n_items elements.  Only the first element of each array is
// emitted (sufficient for scalar display).  For FScan/PScan, FREQ_LOW and
// FREQ_HIGH are combined into a 64-bit frequency for the first step.
// ---------------------------------------------------------------------------

static void decode_periodic_data(const uint8_t* pd, int pd_bytes,
                                  uint32_t sel_flags, int n_items,
                                  uint16_t trace_tag, JsonWriter& j)
{
    if (n_items <= 0 || pd_bytes <= 0) return;

    j.key_int("n_items", n_items);

    // Descriptor for each flag: flag value, element size, key name.
    // Order MUST be ascending by flag value (Table 7-8 sequence).
    struct FlagDesc { uint32_t flag; int bytes; const char* key; };
    static const FlagDesc kDesc[] = {
        { SEL_LEVEL,           2, "level_01dbuv"        },
        { SEL_OFFSET,          4, "offset_hz"           },
        { SEL_FSTRENGTH,       2, "fstrength_01dbuvm"   },
        { SEL_AM,              2, "am_01pct"            },
        { SEL_AM_POS,          2, "am_pos_01pct"        },
        { SEL_AM_NEG,          2, "am_neg_01pct"        },
        { SEL_FM,              4, "fm_hz"               },
        { SEL_FM_POS,          4, "fm_pos_hz"           },
        { SEL_FM_NEG,          4, "fm_neg_hz"           },
        { SEL_PM,              2, "pm_01rad"            },
        { SEL_BANDWIDTH,       4, "bandwidth_hz"        },
        { SEL_DF_LEVEL,        2, "df_level_01dbuv"     },
        { SEL_AZIMUTH,         2, "azimuth_01deg"       },  // bearing — critical
        { SEL_DF_QUALITY,      2, "df_quality_01pct"    },
        { SEL_DF_FSTRENGTH,    2, "df_fstr_01dbuvm"     },
        { SEL_CHANNEL,         2, "channel"             },
        { SEL_FREQ_LOW,        4, "freq_low_hz"         },
        { SEL_ELEVATION,       2, "elevation_01deg"     },
        { SEL_DF_OMNIPHASE,    4, "df_omniphase_01deg"  },
        { SEL_FREQ_HIGH,       4, "freq_high"           },
        { SEL_BANDWIDTH_CTR,   4, "bw_center_hz"        },
        { SEL_FREQ_OFFSET_REL, 4, "freq_off_rel"        },
    };

    const uint8_t* p   = pd;
    int            rem = pd_bytes;

    // Track FREQ_LOW first-item so FREQ_HIGH can emit the combined 64-bit value.
    uint32_t freq_lo_first = 0;
    bool     has_freq_lo   = false;

    for (const auto& d : kDesc) {
        if (!(sel_flags & d.flag)) continue;
        int block = d.bytes * n_items;
        if (rem < d.bytes) break;  // truncated; can't read even first item

        // Emit first-item value as signed integer (Python reinterprets if needed)
        if (d.bytes == 2) {
            j.key_int(d.key, load_i16be(p));
        } else {
            if (d.flag == SEL_FREQ_LOW) {
                freq_lo_first = load_u32be(p);
                has_freq_lo   = true;
                j.key_uint(d.key, freq_lo_first);
            } else if (d.flag == SEL_FREQ_HIGH) {
                uint32_t f_hi = load_u32be(p);
                j.key_uint(d.key, f_hi);
                if (has_freq_lo) {
                    uint64_t f_hz = (static_cast<uint64_t>(f_hi) << 32) | freq_lo_first;
                    j.key_int("freq_hz", static_cast<long long>(f_hz));
                }
            } else {
                j.key_int(d.key, load_i32be(p));
            }
        }

        int advance = std::min(block, rem);
        p   += advance;
        rem -= advance;
    }

    if (trace_tag == TAG_DFPAN) j.key_bool("is_dfpan", true);
}

// ---------------------------------------------------------------------------
// parse_eb200 — decode one complete EB200 datagram into JSON
// ---------------------------------------------------------------------------

static std::string parse_eb200(const uint8_t* pkt, int pkt_len) {
    if (pkt_len < EB200_HDR_BYTES) return {};

    // EB200 header (Table 7-2, all big-endian)
    uint16_t ver_minor = load_u16be(pkt +  4);
    uint16_t ver_major = load_u16be(pkt +  6);
    uint16_t seq_lo    = load_u16be(pkt +  8);
    uint16_t seq_hi    = load_u16be(pkt + 10);
    uint32_t data_size = load_u32be(pkt + 12);
    uint32_t seq_num   = (static_cast<uint32_t>(seq_hi) << 16) | seq_lo;

    JsonWriter j;
    j.key_str ("hw",        "esme");
    j.key_str ("stream",    "eb200");
    j.key_uint("seq_num",   seq_num);
    j.key_uint("ver_major", ver_major);
    j.key_uint("ver_minor", ver_minor);
    j.key_uint("data_size", data_size);

    // Walk GenericAttribute list (one or more per datagram)
    const uint8_t* ga    = pkt + EB200_HDR_BYTES;
    int            avail = pkt_len - EB200_HDR_BYTES;
    if (avail < 2) return j.str();

    uint16_t trace_tag = load_u16be(ga);
    bool     is_adv    = (trace_tag >= TAG_ADVANCED_THRESHOLD);

    j.key_uint("trace_tag", trace_tag);
    j.key_str ("tag_name",  trace_tag_name(trace_tag));

    // Locate TraceData
    const uint8_t* td     = nullptr;
    int            td_len = 0;
    if (!is_adv) {
        if (avail < GA_CONV_HDR) return j.str();
        td_len = static_cast<int>(load_u16be(ga + 2));
        td     = ga + GA_CONV_HDR;
    } else {
        if (avail < GA_ADV_HDR) return j.str();
        td_len = static_cast<int>(load_u32be(ga + 4));
        td     = ga + GA_ADV_HDR;
    }
    int td_avail = static_cast<int>(pkt + pkt_len - td);
    if (td_len > td_avail) td_len = td_avail;
    if (td_len < 0) td_len = 0;

    // Decode TraceAttribute
    int      n_items   = 0;
    int      opt_len   = 0;
    uint32_t sel_flags = 0u;
    int      ta_hdr    = 0;

    if (!is_adv) {
        // Conventional (Table 7-6)
        if (td_len < TA_CONV_HDR) return j.str();
        n_items   = static_cast<int>(load_i16be(td + 0));
        uint8_t ch = td[2];
        opt_len   = static_cast<int>(td[3]);
        sel_flags = load_u32be(td + 4);
        ta_hdr    = TA_CONV_HDR;
        j.key_int ("channel",      ch);
        j.key_int ("n_trace_items", n_items);
        j.key_uint("sel_flags",    sel_flags);
        j.key_int ("opt_hdr_len",  opt_len);
    } else {
        // Advanced (Table 7-7)
        if (td_len < TA_ADV_HDR) return j.str();
        n_items         = static_cast<int>(load_u32be(td +  0));
        uint32_t ch     = load_u32be(td +  4);
        opt_len         = static_cast<int>(load_u32be(td +  8));
        sel_flags       = load_u32be(td + 12);
        uint32_t fl_hi  = load_u32be(td + 16);
        ta_hdr          = TA_ADV_HDR;
        j.key_uint("channel",       ch);
        j.key_uint("n_trace_items", static_cast<unsigned long long>(n_items));
        j.key_uint("sel_flags",     sel_flags);
        j.key_uint("sel_flags_hi",  fl_hi);
        j.key_uint("opt_hdr_len",   static_cast<unsigned long long>(opt_len));
    }

    int opt_avail = td_len - ta_hdr;
    if (opt_avail < 0) opt_avail = 0;
    if (opt_len > opt_avail) opt_len = opt_avail;

    // PeriodicTraceData start: always relative to OptionalHeaderLength (forward compat)
    const uint8_t* opt_hdr = td + ta_hdr;
    const uint8_t* pd      = opt_hdr + opt_len;
    int            pd_len  = td_len - ta_hdr - opt_len;
    if (pd_len < 0) pd_len = 0;
    j.key_int("periodic_data_bytes", pd_len);

    // Dispatch optional header decoder
    if (opt_len > 0) {
        switch (trace_tag) {
            case TAG_DFPAN:  decode_dfpan_opt_header (opt_hdr, opt_len, j); break;
            case TAG_FSCAN:
            case TAG_MSCAN:  decode_fscan_opt_header (opt_hdr, opt_len, j); break;
            case TAG_IFPAN:  decode_ifpan_opt_header (opt_hdr, opt_len, j); break;
            case TAG_PSCAN:  decode_pscan_opt_header (opt_hdr, opt_len, j); break;
            default: break;
        }
    }

    // Decode PeriodicTraceData
    if (pd_len > 0) {
        switch (trace_tag) {
            case TAG_DFPAN:
            case TAG_FSCAN:
            case TAG_MSCAN:
            case TAG_IFPAN:
            case TAG_PSCAN:
            case TAG_CW:
                decode_periodic_data(pd, pd_len, sel_flags, n_items, trace_tag, j);
                break;
            case TAG_GPSCOMPASS:
                decode_gpscompass_sample(pd, pd_len, j);
                break;
            default:
                break;
        }
    }

    return j.str();
}

// ---------------------------------------------------------------------------
// parse_ammos — decode one complete AMMOS AIF frame into JSON
// All fields are little-endian (§7.7).
// ---------------------------------------------------------------------------

static std::string parse_ammos(const uint8_t* pkt, int pkt_len) {
    if (pkt_len < AMMOS_FRAME_HDR_BYTES) return {};

    // Frame header (Table 7-40)
    uint32_t frame_len_words = load_u32le(pkt +  4);
    uint32_t frame_count     = load_u32le(pkt +  8);
    uint32_t frame_type      = load_u32le(pkt + 12);  // 0x01=32b IQ, 0x02=16b IQ
    uint32_t data_hdr_words  = load_u32le(pkt + 16);  // constant 19

    JsonWriter j;
    j.key_str ("hw",              "esme");
    j.key_str ("stream",          "ammos_aif");
    j.key_uint("frame_count",     frame_count);
    j.key_uint("frame_type",      frame_type);
    j.key_uint("frame_len_bytes", frame_len_words * 4u);
    j.key_uint("data_hdr_words",  data_hdr_words);

    if (pkt_len < AMMOS_FRAME_HDR_BYTES + AMMOS_DATA_HDR_BYTES) return j.str();

    // Data header (Table 7-41), starts immediately after frame header
    const uint8_t* dh = pkt + AMMOS_FRAME_HDR_BYTES;

    uint32_t db_count     = load_u32le(dh +  0);
    uint32_t db_len_words = load_u32le(dh +  4);  // excl. data block header, per block
    uint32_t ts_lo        = load_u32le(dh +  8);  // µs since epoch, low 32
    uint32_t ts_hi        = load_u32le(dh + 12);  // µs since epoch, high 32
    uint32_t statusword   = load_u32le(dh + 16);
    // dh+20: SignalSourceID  (= 0)
    // dh+24: SignalSourceState (= 0)
    uint32_t freq_lo      = load_u32le(dh + 28);
    uint32_t freq_hi      = load_u32le(dh + 32);
    uint32_t bandwidth    = load_u32le(dh + 36);
    uint32_t samplerate   = load_u32le(dh + 40);
    uint32_t interpolation= load_u32le(dh + 44);
    uint32_t decimation   = load_u32le(dh + 48);
    int32_t  ant_volt_ref = load_i32le(dh + 52);  // 0.1 dBµV, signed
    uint32_t start_ts_lo  = load_u32le(dh + 56);
    uint32_t start_ts_hi  = load_u32le(dh + 60);

    uint64_t ts_us      = (static_cast<uint64_t>(ts_hi) << 32) | ts_lo;
    uint64_t freq_hz    = (static_cast<uint64_t>(freq_hi) << 32) | freq_lo;
    uint64_t start_ts   = (static_cast<uint64_t>(start_ts_hi) << 32) | start_ts_lo;

    j.key_uint("db_count",           db_count);
    j.key_uint("db_len_words",        db_len_words);
    j.key_int ("timestamp_us",        static_cast<long long>(ts_us));
    j.key_uint("statusword",          statusword);
    j.key_int ("freq_hz",             static_cast<long long>(freq_hz));
    j.key_uint("bandwidth_hz",        bandwidth);
    j.key_uint("samplerate_hz",       samplerate);
    j.key_uint("interpolation",       interpolation);
    j.key_uint("decimation",          decimation);
    j.key_int ("ant_volt_ref_01dbuv", ant_volt_ref);
    j.key_int ("start_ts_us",         static_cast<long long>(start_ts));

    // Data block header (1 × 32-bit word) immediately follows data header
    int db_hdr_off = AMMOS_FRAME_HDR_BYTES + AMMOS_DATA_HDR_BYTES;
    if (pkt_len >= db_hdr_off + 4) {
        uint32_t db_status = load_u32le(pkt + db_hdr_off);
        j.key_uint("db_status",    db_status);
        j.key_uint("iq_data_words", db_len_words);
    }

    return j.str();
}

// ---------------------------------------------------------------------------
// parse_scpi — decode a SCPI text line into JSON
// ---------------------------------------------------------------------------

static std::string parse_scpi(const uint8_t* frame, int frame_len, int frame_type) {
    int len = frame_len;
    while (len > 0 && (frame[len - 1] == '\n' || frame[len - 1] == '\r')) --len;

    JsonWriter j;
    j.key_str("hw",       "esme");
    j.key_str("stream",   "scpi");
    j.key_str("msg_kind", frame_type == 1 ? "command" : "response");
    j.key_str("text",     std::string(reinterpret_cast<const char*>(frame),
                                      static_cast<size_t>(len)));
    return j.str();
}

// ---------------------------------------------------------------------------
// JSON field helper for format_response
// ---------------------------------------------------------------------------

static std::string json_str_field(const char* json, const char* key) {
    std::string k("\""); k += key; k += "\"";
    const char* p = std::strstr(json, k.c_str());
    if (!p) return {};
    p += k.size();
    while (*p == ' ' || *p == ':') ++p;
    if (*p != '"') return {};
    ++p;
    std::string val;
    while (*p && *p != '"') {
        if (*p == '\\' && *(p + 1)) {
            switch (*(p + 1)) {
                case '"':  val += '"';  p += 2; break;
                case '\\': val += '\\'; p += 2; break;
                case 'n':  val += '\n'; p += 2; break;
                case 'r':  val += '\r'; p += 2; break;
                case 't':  val += '\t'; p += 2; break;
                default:   val += *p++; break;
            }
        } else {
            val += *p++;
        }
    }
    return val;
}

// ---------------------------------------------------------------------------
// extract_frame  (ABI entry point)
// ---------------------------------------------------------------------------
//
// Channel detection:
//   1. EB200 magic (BE 0x000EB200) → type 3; length from DataSize field (offset 12)
//   2. AMMOS magic (LE 0xFB746572) → type 4; length from FrameLength (offset 4, ×4)
//   3. Printable ASCII (SCPI line)  → type 1 (command) or 2 (response); \n-terminated
//
// Returns: 1=SCPI cmd  2=SCPI resp  3=EB200  4=AMMOS  0=incomplete  -1=corrupt

SDFC_EXPORT int extract_frame(const uint8_t* buf,
                               size_t         buf_len,
                               uint8_t**      out_frame,
                               size_t*        out_len)
{
    if (!buf || buf_len < 1 || !out_frame || !out_len) return -1;

    int ibuf = static_cast<int>(buf_len);

    // ---- EB200 (big-endian magic) ----
    if (ibuf >= 4 && load_u32be(buf) == EB200_MAGIC) {
        if (ibuf < EB200_HDR_BYTES) return -1;
        uint32_t data_size = load_u32be(buf + 12);
        if (data_size < static_cast<uint32_t>(EB200_HDR_BYTES)) return -1;
        if (data_size > static_cast<uint32_t>(MAX_FRAME_BUFFER_BYTES)) return -1;
        if (ibuf < static_cast<int>(data_size)) return -1;
        auto* p = static_cast<uint8_t*>(std::malloc(data_size));
        if (!p) return -1;
        memcpy(p, buf, data_size);
        *out_frame = p;
        *out_len   = (size_t)data_size;
        return 0;
    }

    // ---- AMMOS (little-endian magic: bytes 72 65 74 FB) ----
    if (ibuf >= 4 && load_u32le(buf) == AMMOS_MAGIC) {
        if (ibuf < 8) return -1;
        uint32_t fl_words = load_u32le(buf + 4);
        if (fl_words < static_cast<uint32_t>(AMMOS_FRAME_HDR_WORDS)) return -1;
        uint32_t fl_bytes = fl_words * 4u;
        if (fl_bytes > static_cast<uint32_t>(MAX_FRAME_BUFFER_BYTES)) return -1;
        if (ibuf < static_cast<int>(fl_bytes)) return -1;
        auto* p = static_cast<uint8_t*>(std::malloc(fl_bytes));
        if (!p) return -1;
        memcpy(p, buf, fl_bytes);
        *out_frame = p;
        *out_len   = (size_t)fl_bytes;
        return 0;
    }

    // ---- SCPI text (\n-terminated) ----
    // Skip leading carriage returns (TCP stream may have stray \r)
    int start = 0;
    while (start < ibuf && (buf[start] == '\r' || buf[start] == ' ')) ++start;

    if (start < ibuf && buf[start] >= 0x20u) {
        int end = start;
        while (end < ibuf && buf[end] != '\n') ++end;
        if (end >= ibuf) return -1;  // incomplete line

        int line_len = end - start + 1;
        if (line_len > MAX_FRAME_BUFFER_BYTES) return -1;
        auto* p = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(line_len)));
        if (!p) return -1;
        memcpy(p, buf + start, static_cast<size_t>(line_len));
        *out_frame = p;
        *out_len   = static_cast<size_t>(line_len);

        // Numeric / '+' / '-' → response; alphabetic SCPI keyword → command
        unsigned char fc = buf[start];
        (void)((fc >= '0' && fc <= '9') || fc == '+' || fc == '-'); // ftype stored below
        return 0;
    }

    return -1;
}

// ---------------------------------------------------------------------------
// parse_message  (ABI entry point)
// ---------------------------------------------------------------------------

SDFC_EXPORT int parse_message(const uint8_t* frame, size_t frame_len,
                               char** out_json, size_t* out_len)
{
    if (!frame || frame_len < 1 || !out_json || !out_len) return -1;

    int iframe = static_cast<int>(frame_len);
    std::string result;
    if (iframe >= 4 && load_u32be(frame) == EB200_MAGIC) {
        result = parse_eb200(frame, iframe);
    } else if (iframe >= 4 && load_u32le(frame) == AMMOS_MAGIC) {
        result = parse_ammos(frame, iframe);
    } else {
        // SCPI: numeric/+/- → response (type 2), alpha → command (type 1)
        unsigned char fc = frame[0];
        int ftype = ((fc >= '0' && fc <= '9') || fc == '+' || fc == '-') ? 2 : 1;
        result = parse_scpi(frame, iframe, ftype);
    }

    if (result.empty()) return -1;

    char* out = static_cast<char*>(std::malloc(result.size() + 1u));
    if (!out) return -1;
    memcpy(out, result.data(), result.size() + 1u);
    *out_json = out;
    *out_len  = result.size();
    return 0;
}

// ---------------------------------------------------------------------------
// format_response  (ABI entry point)
// ---------------------------------------------------------------------------
//
// Encodes a JSON descriptor into a SCPI text command for TCP 5555.
//
// Required JSON fields:
//   "scpi_cmd"  : SCPI command string
//                 e.g. "TRAC:UDP:TAG \"172.16.0.1\",17222,\"DFPan\""
//
// Optional:
//   "crlf"      : "1" → append \r\n (default: \n)
//
// Returns total bytes written, or -1 on error.
//
// Example SCPI startup sequence (caller side):
//   format_response("{\"scpi_cmd\":\"TRAC:UDP:TAG \\\"192.168.1.10\\\",17222,\\\"DFPan\\\"\"}")
//   format_response("{\"scpi_cmd\":\"TRAC:UDP:FLAG \\\"192.168.1.10\\\",17222,\\\"OPT\\\",\\\"AZI\\\",\\\"DFL\\\"\"}")

SDFC_EXPORT int format_response(const char* /*kind*/, const char* kwargs_json,
                                 uint8_t** out_buf, size_t* out_len)
{
    if (!kwargs_json || !out_buf || !out_len) return -1;

    std::string cmd  = json_str_field(kwargs_json, "scpi_cmd");
    std::string crlf = json_str_field(kwargs_json, "crlf");
    if (cmd.empty()) return -1;

    const char* term     = (crlf == "1") ? "\r\n" : "\n";
    int         cmd_len  = static_cast<int>(cmd.size());
    int         term_len = static_cast<int>(strlen(term));
    int         total    = cmd_len + term_len;

    if (total >= MAX_FRAME_BUFFER_BYTES) return -1;

    auto* p = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(total)));
    if (!p) return -1;
    memcpy(p, cmd.data(), static_cast<size_t>(cmd_len));
    memcpy(p + cmd_len, term, static_cast<size_t>(term_len));
    *out_buf = p;
    *out_len = static_cast<size_t>(total);
    return 0;
}

// ---------------------------------------------------------------------------
// free_result  (ABI entry point)
// ---------------------------------------------------------------------------

SDFC_EXPORT void free_result(void* ptr)
{
    std::free(ptr);
}
