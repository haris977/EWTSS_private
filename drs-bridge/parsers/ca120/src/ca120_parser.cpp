// drs-bridge/parsers/ca120/src/ca120_parser.cpp
//
// R&S CA120 Multichannel Signal Analysis parser DLL.
// ICD: R&S-CA120-ICD-V15  (all §-references below are to that document)
//
// Implements sdfc_abi.h on two channels:
//   XML control (TCP 9001)   — bidirectional UTF-8 XML; no length prefix
//   AMMOS binary (TCP 9200–9400) — magic-synced binary; one port per active stream
//
// Frame type mapping (returned by extract_frame, consumed by parse_message):
//   1 = XML <Request>         (DRS-bridge → CA120)
//   2 = XML <Reply> / <Event> (CA120 → DRS-bridge)
//   3 = AMMOS streaming frame (CA120 → DRS-bridge)

#include "sdfc_abi.h"
#include "sdfc_endian.h"
#include "json_writer.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>
#include <algorithm>

using namespace sdfc;

// ---------------------------------------------------------------------------
// AMMOS FrameType constants (§6.11.2)
// ---------------------------------------------------------------------------

static constexpr uint32_t AMMOS_MAGIC      = 0xFB746572u;
static constexpr uint32_t AMMOS_MAX_WORDS  = 0x100000u;

// IF data variants (§6.1) — share the same data header layout
static constexpr uint32_t FT_IFDATA_32RE_32IM_FIX            = 0x01u;
static constexpr uint32_t FT_IFDATA_16RE_16IM_FIX            = 0x02u;
static constexpr uint32_t FT_IFDATA_16RE_16RE_FIX            = 0x03u;
static constexpr uint32_t FT_IFDATA_32RE_32IM_FIX_RESCALED   = 0x04u;
static constexpr uint32_t FT_IFDATA_32RE_32IM_FLOAT_RESCALED = 0x05u;
// DDCE multichannel IF variants — different header (doubles for bw/sr)
static constexpr uint32_t FT_DDCE_IFDATA_32RE_32IM_FIX       = 0x60u;
static constexpr uint32_t FT_DDCE_IFDATA_16RE_16IM_FIX       = 0x61u;
// Spectrum (§6.4)
static constexpr uint32_t FT_SPECDATA_FLOAT                  = 0x13u;
static constexpr uint32_t FT_SEGMENTATION_SPECDATA_FLOAT     = 0x14u;
// Decoder outputs
static constexpr uint32_t FT_DECODER_TEXT_DATA               = 0x40u;
static constexpr uint32_t FT_IMAGEDATA                       = 0x160u;
static constexpr uint32_t FT_TRANSMISSION_SYSTEM_RESULT_DATA = 0x170u;
// Audio (§6.2)
static constexpr uint32_t FT_AUDIODATA                       = 0x100u;
// Detection (§6.8)
static constexpr uint32_t FT_EMISSION_LIST_DATA              = 0x110u;
static constexpr uint32_t FT_SPECTRALDETECTOR_DATA           = 0x111u;
// Level
static constexpr uint32_t FT_LEVELDATA                       = 0x120u;
static constexpr uint32_t FT_LEVELDATA_DEM                   = 0x121u;
// Symbol (§6.5)
static constexpr uint32_t FT_SYMBOLDATA                      = 0x130u;
// Burst emission (§6.8.3)
static constexpr uint32_t FT_BURST_EMISSIONS_LIST            = 0x150u;
static constexpr uint32_t FT_EXT_BURST_EMISSIONS_LIST        = 0x151u;
// Demodulation
static constexpr uint32_t FT_INSTANTANEOUSDATA               = 0x140u;
static constexpr uint32_t FT_TIMEDOMAIN_DATA                 = 0x180u;
// Statistics (§6.9)
static constexpr uint32_t FT_HISTOGRAM_DATA                  = 0x190u;
static constexpr uint32_t FT_HOP_DENSITY_WATERFALL_DATA      = 0x220u;
// PDW/IQDW (§6.10)
static constexpr uint32_t FT_PULSE_DESCRIPTION_WORD_DATA     = 0x200u;
static constexpr uint32_t FT_IQDW_16RE_16IM_FIX              = 0x201u;  // may exceed AMMOS_MAX_WORDS
static constexpr uint32_t FT_IQDW_32RE_32IM_FIX              = 0x202u;  // may exceed AMMOS_MAX_WORDS
static constexpr uint32_t FT_PULSE_REPETITION_WORD_DATA      = 0x210u;
static constexpr uint32_t FT_BLANKING_DESCRIPTION_WORD_DATA  = 0x211u;

// ---------------------------------------------------------------------------
// Helpers: split 64-bit frequency  (§4.2 note)
// ---------------------------------------------------------------------------

static uint64_t freq64(const uint8_t* lo, const uint8_t* hi) {
    return ((uint64_t)load_u32le(hi) << 32) | load_u32le(lo);
}

// ---------------------------------------------------------------------------
// Helpers: minimal XML scanning  (no LGPL, no exceptions)
// ---------------------------------------------------------------------------

// Find the byte offset PAST the first occurrence of "</tag>" in xml[0..len).
// Returns -1 if not found.
static int xml_closing_end(const uint8_t* xml, int len, const char* tag) {
    char close[80];
    std::snprintf(close, sizeof(close), "</%s>", tag);
    int clen = (int)strlen(close);
    const char* data = reinterpret_cast<const char*>(xml);
    for (int i = 0; i <= len - clen; ++i) {
        if (memcmp(data + i, close, (size_t)clen) == 0)
            return i + clen;
    }
    return -1;
}

// Extract the value of attribute named `attr` (requires ' attr=' prefix).
// Returns empty string if not found.
static std::string xml_attr(const char* xml, int len, const char* attr) {
    std::string key(" ");
    key += attr;
    key += '=';
    const char* end = xml + len;
    const char* p   = xml;
    while (p < end) {
        p = std::search(p, end, key.data(), key.data() + key.size());
        if (p >= end) break;
        p += (int)key.size();
        if (p >= end) break;
        char q = *p;
        if (q != '"' && q != '\'') { continue; }
        const char* vs = p + 1;
        const char* ve = std::find(vs, end, q);
        if (ve >= end) break;
        return std::string(vs, ve);
    }
    return {};
}

// Extract text content between <tag> and </tag>. Returns empty if not found.
static std::string xml_text(const char* xml, int len, const char* tag) {
    char open[80], close[80];
    std::snprintf(open,  sizeof(open),  "<%s>",  tag);
    std::snprintf(close, sizeof(close), "</%s>", tag);
    const char* end = xml + len;
    const char* p = std::search(xml, end, open, open + strlen(open));
    if (p >= end) return {};
    p += strlen(open);
    const char* q = std::search(p, end, close, close + strlen(close));
    if (q >= end) return {};
    return std::string(p, q);
}

// Check whether a tag <tagname ...> or <tagname/> exists (word-boundary safe).
static bool xml_has(const char* xml, int len, const char* tag) {
    std::string open("<");
    open += tag;
    const char* end = xml + len;
    const char* p   = xml;
    while (p < end) {
        p = std::search(p, end, open.data(), open.data() + open.size());
        if (p >= end) break;
        const char* nx = p + open.size();
        if (nx < end && (*nx == ' ' || *nx == '>' || *nx == '/' ||
                         *nx == '\r' || *nx == '\n'))
            return true;
        p = nx;
    }
    return false;
}

// Extract a JSON string field value from a flat JSON object string.
// Stops at the first unescaped closing quote — suitable for field values
// that do not contain escaped Unicode sequences beyond the basics.
static std::string json_str_field(const char* json, const char* key) {
    std::string k("\"");
    k += key;
    k += "\"";
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
                case '/':  val += '/';  p += 2; break;
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

static long long json_int_field(const char* json, const char* key) {
    std::string k("\"");
    k += key;
    k += "\"";
    const char* p = std::strstr(json, k.c_str());
    if (!p) return -1LL;
    p += k.size();
    while (*p == ' ' || *p == ':') ++p;
    if (*p == '-' || (*p >= '0' && *p <= '9'))
        return std::strtoll(p, nullptr, 10);
    return -1LL;
}

// ---------------------------------------------------------------------------
// Enum-to-string helpers (§6.12, §7.x)
// ---------------------------------------------------------------------------

static const char* demod_type_str(uint32_t v) {
    switch (v) {
        case 0:           return "FM";
        case 1:           return "AM";
        case 5:           return "ISB";
        case 6:           return "CW";
        case 7:           return "USB";
        case 8:           return "LSB";
        case 0x100u:      return "DIGITAL";
        case 0xFFFFFFFFu: return "UNKNOWN";
        default:          return "OTHER";
    }
}

// Spectrum StatusWord bits 7-4: window type (§7.3)
// enum values are × 0x10 of the raw 4-bit nibble
static const char* spectrum_window_str(uint32_t nibble_7_4) {
    switch (nibble_7_4 & 0xFu) {
        case 0: return "RECT";
        case 1: return "HAMMING";
        case 2: return "HANN";
        case 3: return "KAISER";
        case 4: return "BLACKMAN";
        default: return "OTHER";
    }
}

// Spectrum StatusWord bits 3-0: display mode (§7.2)
static const char* spectrum_display_str(uint32_t nibble_3_0) {
    switch (nibble_3_0 & 0xFu) {
        case 0: return "AVERAGING";
        case 1: return "MINHOLD";
        case 2: return "PEAKHOLD";
        case 3: return "PEAKHOLDSHORTTIME";
        case 4: return "MINHOLDSHORTTIME";
        case 5: return "DIFFERENCE";
        case 6: return "CLEARWRITE";
        default: return "OTHER";
    }
}

// Spectrum StatusWord bits 15-12: sample source (§7.6)
static const char* spectrum_samplesource_str(uint32_t nibble_15_12) {
    switch (nibble_15_12 & 0xFu) {
        case 0: return "baseband";
        case 1: return "fm_instantaneous";
        case 2: return "envelope";
        case 3: return "baseband_squared";
        default: return "other";
    }
}

// Histogram StatusWord: histogram type (§6.9.1 enum_HISTOGRAM_DATA)
static const char* histogram_type_str(uint32_t v) {
    switch (v) {
        case 0x1: return "duration_us";
        case 0x2: return "frequency_hz";
        case 0x3: return "bandwidth_hz";
        case 0x4: return "level_dBm";
        case 0x5: return "symbol_rate_Bd";
        case 0x6: return "freq_shift_hz";
        case 0x7: return "modulation_type";
        case 0x8: return "timing";
        case 0x9: return "enhanced_timing";
        case 0xA: return "azimuth";
        default:  return "unknown";
    }
}

// ---------------------------------------------------------------------------
// AMMOS stream decoders — each writes into a JsonWriter
// ---------------------------------------------------------------------------

// §6.1  IF Data (FrameTypes 0x01–0x05)
// Data_Header starts at frame+24; DataHeaderLength from frame[0x10].
static void decode_if_data(const uint8_t* dh, int dh_bytes,
                            uint32_t frame_type, JsonWriter& j)
{
    (void)frame_type;  // variant (32-bit fix, 16-bit, float, rescaled) noted in stream name
    if (dh_bytes < 0x38) {
        j.key_str("warning", "IF data header truncated (< 0x38 bytes)");
        return;
    }
    j.key_uint("datablock_count",       load_u32le(dh + 0x00));
    j.key_uint("datablock_length_words",load_u32le(dh + 0x04));
    j.key_int ("timestamp_us",          (int64_t)load_u64le(dh + 0x08));
    uint32_t sw = load_u32le(dh + 0x10);
    j.key_bool("change_flag",           (sw >> 31) & 1u);
    j.key_bool("dbfs_flag",             (sw >> 30) & 1u);
    j.key_uint("user_flags",            sw & 0xFFu);
    j.key_uint("source_id",             load_u32le(dh + 0x14));
    j.key_uint("source_state",          load_u32le(dh + 0x18));
    j.key_uint("freq_hz",               (uint32_t)freq64(dh + 0x1C, dh + 0x20));
    j.key_uint("bandwidth_hz",          load_u32le(dh + 0x24));
    j.key_uint("sample_rate_hz",        load_u32le(dh + 0x28));
    j.key_uint("interpolation",         load_u32le(dh + 0x2C));
    j.key_uint("decimation",            load_u32le(dh + 0x30));
    j.key_uint("ant_voltage_ref_0_1dBuV", load_u32le(dh + 0x34));
    // Extended fields present when DataHeaderLength > 14 words (§6.1)
    if (dh_bytes > 56 && dh_bytes >= 0x4C) {
        j.key_int ("ext_timestamp_ns",      (int64_t)load_u64le(dh + 0x38));
        j.key_uint("ext_sample_counter_lo", load_u32le(dh + 0x40));
        j.key_uint("ext_sample_counter_hi", load_u32le(dh + 0x44));
        uint32_t kf = load_u32le(dh + 0x48);
        if (kf != 0x80000000u)
            j.key_uint("ext_kfactor_0_1dBm", kf);
    }
}

// §6.1 DDCE multichannel IF (FrameTypes 0x60–0x61)
// Uses double for Bandwidth and Samplerate; timestamp is nanoseconds (not µs).
static void decode_ddce_if_data(const uint8_t* dh, int dh_bytes, JsonWriter& j) {
    if (dh_bytes < 0x2C) {
        j.key_str("warning", "DDCE IF header truncated (< 0x2C bytes)");
        return;
    }
    j.key_uint("source_id",                load_u32le(dh + 0x00));
    j.key_uint("ant_voltage_ref_0_1dBuV",  load_u32le(dh + 0x04));
    // 0x08 reserved
    j.key_int ("timestamp_ns",             (int64_t)load_u64le(dh + 0x0C));  // ns, not µs
    j.key_uint("freq_hz",                  (uint32_t)freq64(dh + 0x14, dh + 0x18));
    j.key_double("bandwidth_hz",           load_f64le(dh + 0x1C));
    j.key_double("sample_rate_hz",         load_f64le(dh + 0x24));
}

// §6.2  Audio Data (FrameType 0x100)
// 10-field header — distilled doc only had 2; the full layout is used here.
static void decode_audio_data(const uint8_t* dh, int dh_bytes, JsonWriter& j) {
    if (dh_bytes < 0x24) {
        j.key_str("warning", "Audio header truncated (< 0x24 bytes)");
        return;
    }
    j.key_uint("sample_rate_hz",     load_u32le(dh + 0x00));
    uint32_t sw = load_u32le(dh + 0x04);
    j.key_bool("squelch_ch0",        sw & 1u);
    j.key_bool("squelch_ch1",        (sw >> 1) & 1u);
    j.key_uint("center_freq_hz",     (uint32_t)freq64(dh + 0x08, dh + 0x0C));
    j.key_uint("demod_bandwidth_hz", load_u32le(dh + 0x10));
    j.key_str ("demod_type",         demod_type_str(load_u32le(dh + 0x14)));
    j.key_uint("sample_count",       load_u32le(dh + 0x18));
    j.key_uint("channel_count",      load_u32le(dh + 0x1C));
    j.key_uint("sample_size_bytes",  load_u32le(dh + 0x20));
    if (dh_bytes >= 0x2C)
        j.key_int("ext_timestamp_ns", (int64_t)load_u64le(dh + 0x24));
}

// §6.4  Spectrum Data (FrameTypes 0x13 / 0x14)
static void decode_spectrum(const uint8_t* dh, int dh_bytes, JsonWriter& j) {
    if (dh_bytes < 0x28) {
        j.key_str("warning", "Spectrum header truncated (< 0x28 bytes)");
        return;
    }
    j.key_int ("timestamp_us",       (int64_t)load_u64le(dh + 0x00));
    j.key_uint("center_freq_hz",     (uint32_t)freq64(dh + 0x08, dh + 0x0C));
    j.key_uint("sample_rate_hz",     load_u32le(dh + 0x10));
    j.key_uint("fft_length",         load_u32le(dh + 0x14));
    uint32_t sw = load_u32le(dh + 0x18);
    j.key_uint("scan_id",            (sw >> 18) & 0x1Fu);
    j.key_bool("fragment_flag",      (sw >> 17) & 1u);
    j.key_bool("blanking_flag",      (sw >> 16) & 1u);
    j.key_str ("sample_source",      spectrum_samplesource_str((sw >> 12) & 0xFu));
    j.key_bool("dbfs_flag",          (sw >> 10) & 1u);
    j.key_bool("invalidity_flag",    (sw >> 9)  & 1u);
    j.key_str ("level_type",         ((sw >> 8) & 1u) ? "log" : "linear");
    j.key_str ("window_type",        spectrum_window_str((sw >> 4) & 0xFu));
    j.key_str ("display_mode",       spectrum_display_str(sw & 0xFu));
    j.key_double("ref_value",        (double)load_f32le(dh + 0x1C));
    j.key_uint("left_bin",           load_u32le(dh + 0x20));
    j.key_uint("right_bin",          load_u32le(dh + 0x24));
    if (dh_bytes >= 0x2C) {
        uint32_t kf = load_u32le(dh + 0x28);
        if (kf != 0x80000000u) j.key_uint("ext_kfactor", kf);
    }
    if (dh_bytes >= 0x30) {
        uint32_t sr_hi = load_u32le(dh + 0x2C);
        if (sr_hi) j.key_uint("sample_rate_high_word", sr_hi);
    }
}

// §6.5  Symbol Data (FrameType 0x130)
static void decode_symbol_data(const uint8_t* dh, int dh_bytes, JsonWriter& j) {
    if (dh_bytes < 0x30) {
        j.key_str("warning", "Symbol header truncated (< 0x30 bytes)");
        return;
    }
    j.key_int ("timestamp_us",    (int64_t)load_u64le(dh + 0x00));
    j.key_uint("center_freq_hz",  (uint32_t)freq64(dh + 0x08, dh + 0x0C));
    j.key_double("freq_deviation_hz", (double)load_f32le(dh + 0x10));
    // packedcharModulationType: char[8] zero-padded ANSI at offset 0x14
    char mod[9] = {};
    memcpy(mod, dh + 0x14, 8);
    j.key_str("modulation_type",  mod);
    // 0x1C: StatusWord (SoftDecisionType, MorseData, MappingType)
    j.key_uint("status_word",     load_u32le(dh + 0x1C));
    j.key_uint("channel_count",   load_u32le(dh + 0x20));
    j.key_double("symbol_rate",   (double)load_f32le(dh + 0x24));
    j.key_uint("symbol_valency",  load_u32le(dh + 0x28));
    j.key_uint("symbol_count",    load_u32le(dh + 0x2C));
}

// §6.6  Time Domain Data (FrameType 0x180)
static void decode_timedomain(const uint8_t* dh, int dh_bytes, JsonWriter& j) {
    if (dh_bytes < 0x18) {
        j.key_str("warning", "Time domain header truncated (< 0x18 bytes)");
        return;
    }
    j.key_int ("timestamp_us",   (int64_t)load_u64le(dh + 0x00));
    j.key_uint("center_freq_hz", (uint32_t)freq64(dh + 0x08, dh + 0x0C));
    j.key_uint("status_word",    load_u32le(dh + 0x10));
    j.key_double("sample_rate",  (double)load_f32le(dh + 0x14));
    if (dh_bytes >= 0x1C) j.key_uint("sample_count", load_u32le(dh + 0x18));
}

// §6.6  Instantaneous Demodulation Data (FrameType 0x140) — adds SamplesPerSymbol
static void decode_instantaneous(const uint8_t* dh, int dh_bytes, JsonWriter& j) {
    decode_timedomain(dh, dh_bytes, j);  // first fields are identical
    if (dh_bytes >= 0x20)
        j.key_double("samples_per_symbol", (double)load_f32le(dh + 0x1C));
}

// §6.8.1  Detector Emission List (FrameType 0x110)
// Bandwidth is a plain uint32 at 0x10 (differs from 0x111 which splits it).
static void decode_emission_list(const uint8_t* dh, int dh_bytes, JsonWriter& j) {
    if (dh_bytes < 0x14) {
        j.key_str("warning", "Emission list header truncated (< 0x14 bytes)");
        return;
    }
    j.key_uint("emission_count",  load_u32le(dh + 0x00));
    j.key_uint("status_word",     load_u32le(dh + 0x04));
    j.key_uint("center_freq_hz",  (uint32_t)freq64(dh + 0x08, dh + 0x0C));
    j.key_uint("bandwidth_hz",    load_u32le(dh + 0x10));
    // Data body: parallel arrays of EmissionID, bigtimeTimeStamp, CenterFreq_L/H,
    //            Bandwidth, Level (dBm/Hz), SignalStatus (enum) — not decoded inline.
}

// §6.8.2  Spectral Detector List (FrameType 0x111)
// Bandwidth is SPLIT 64-bit (Low at 0x10, High at 0x14) — unlike 0x110.
static void decode_spectral_detector(const uint8_t* dh, int dh_bytes, JsonWriter& j) {
    if (dh_bytes < 0x24) {
        j.key_str("warning", "Spectral detector header truncated (< 0x24 bytes)");
        return;
    }
    j.key_uint("emission_count",    load_u32le(dh + 0x00));
    j.key_uint("status_word",       load_u32le(dh + 0x04));
    j.key_uint("center_freq_hz",    (uint32_t)freq64(dh + 0x08, dh + 0x0C));
    j.key_uint("bandwidth_hz",      (uint32_t)freq64(dh + 0x10, dh + 0x14));
    j.key_int ("timestamp_us",      (int64_t)load_u64le(dh + 0x18));
    j.key_uint("category_count",    load_u32le(dh + 0x20));
}

// §6.8.3  Burst Emission List (FrameTypes 0x150 / 0x151)
static void decode_burst_emission(const uint8_t* dh, int dh_bytes, JsonWriter& j) {
    if (dh_bytes < 0x2C) {
        j.key_str("warning", "Burst emission header truncated (< 0x2C bytes)");
        return;
    }
    j.key_uint("snapshot_center_freq_hz",  (uint32_t)freq64(dh + 0x00, dh + 0x04));
    j.key_uint("snapshot_bandwidth_hz",    load_u32le(dh + 0x08));
    // Extended bandwidth split 64-bit at 0x0C/0x10
    j.key_uint("ext_snapshot_bandwidth_hz",(uint32_t)freq64(dh + 0x0C, dh + 0x10));
    j.key_int ("snapshot_start_us",        (int64_t)load_u64le(dh + 0x14));
    j.key_uint("snapshot_length_ms",       load_u32le(dh + 0x1C));
    j.key_bool("end_flag",                 load_u32le(dh + 0x20) != 0u);
    j.key_uint("dataset_count",            load_u32le(dh + 0x24));
    j.key_uint("dataset_length_words",     load_u32le(dh + 0x28));
}

// §6.9.1  Histogram Data (FrameType 0x190)
static void decode_histogram(const uint8_t* dh, int dh_bytes, JsonWriter& j) {
    if (dh_bytes < 0x20) {
        j.key_str("warning", "Histogram header truncated (< 0x20 bytes)");
        return;
    }
    j.key_int ("timestamp_us",     (int64_t)load_u64le(dh + 0x00));
    uint32_t ht = load_u32le(dh + 0x08);
    j.key_str ("histogram_type",   histogram_type_str(ht));
    j.key_uint("histogram_type_id", ht);
    // 0x0C: HistogramBorders (16 bytes: start/end as uint64 or float per type)
    j.key_uint("bin_count",        load_u32le(dh + 0x1C));
}

// §6.10.1  PDW Data (FrameType 0x200)
// Header has mixed-width GUID fields (uint16 at 0x0C/0x0E, char[8] at 0x10).
static void decode_pdw(const uint8_t* dh, int dh_bytes, JsonWriter& j) {
    if (dh_bytes < 0x20) {
        j.key_str("warning", "PDW header truncated (< 0x20 bytes)");
        return;
    }
    j.key_uint("pdw_count",        load_u32le(dh + 0x00));
    j.key_uint("pdw_size_bytes",   load_u32le(dh + 0x04));
    // GUID (128-bit): Ext_GuidData1(u32)+GuidData2(u16)+GuidData3(u16)+GuidData4(char[8])
    char guid[40];
    std::snprintf(guid, sizeof(guid), "%08x-%04x-%04x-%02x%02x%02x%02x%02x%02x%02x%02x",
        load_u32le(dh + 0x08),
        load_u16le(dh + 0x0C),
        load_u16le(dh + 0x0E),
        dh[0x10], dh[0x11], dh[0x12], dh[0x13],
        dh[0x14], dh[0x15], dh[0x16], dh[0x17]);
    j.key_str("emitter_guid",      guid);
    j.key_uint("emitter_id",       (uint32_t)load_u64le(dh + 0x18));
    // Body: NoOfPDWs × SizeOfPDWs bytes of PDW records — not expanded here.
}

// ---------------------------------------------------------------------------
// parse_ammos — dispatches Data_Header decode by FrameType
// ---------------------------------------------------------------------------

static std::string parse_ammos(const uint8_t* frame, int frame_len) {
    if (frame_len < 24) return {};

    // Frame_Header (§4.2): 6 × uint32
    uint32_t frame_count       = load_u32le(frame + 0x08);
    uint32_t frame_type        = load_u32le(frame + 0x0C);
    uint32_t data_header_words = load_u32le(frame + 0x10);
    int      dh_bytes          = (int)((uint64_t)data_header_words * 4u);
    int      dh_avail          = frame_len - 24;

    JsonWriter j;
    j.key_str("hw", "ca120");

    // Always include the hex FrameType for routing / debugging
    char ft_hex[12];
    std::snprintf(ft_hex, sizeof(ft_hex), "0x%X", frame_type);
    j.key_str("frame_type_hex", ft_hex);
    j.key_uint("frame_count",   frame_count);

    // Data_Header pointer (capped at actual available bytes)
    const uint8_t* dh = frame + 24;
    int dh_cap = (dh_bytes < dh_avail) ? dh_bytes : dh_avail;

    switch (frame_type) {

    // ---- IF Data (standard variants) ----
    case FT_IFDATA_32RE_32IM_FIX:
    case FT_IFDATA_16RE_16IM_FIX:
    case FT_IFDATA_16RE_16RE_FIX:
    case FT_IFDATA_32RE_32IM_FIX_RESCALED:
    case FT_IFDATA_32RE_32IM_FLOAT_RESCALED:
        j.key_str("stream", "if_data");
        decode_if_data(dh, dh_cap, frame_type, j);
        break;

    // ---- DDCE multichannel IF (double bw/sr; ns timestamp) ----
    case FT_DDCE_IFDATA_32RE_32IM_FIX:
    case FT_DDCE_IFDATA_16RE_16IM_FIX:
        j.key_str("stream", "ddce_if_data");
        decode_ddce_if_data(dh, dh_cap, j);
        break;

    // ---- Spectrum ----
    case FT_SPECDATA_FLOAT:
        j.key_str("stream", "spectrum");
        decode_spectrum(dh, dh_cap, j);
        break;
    case FT_SEGMENTATION_SPECDATA_FLOAT:
        j.key_str("stream", "segmentation_spectrum");
        decode_spectrum(dh, dh_cap, j);  // same header; segmentation data is in body
        break;

    // ---- Audio ----
    case FT_AUDIODATA:
        j.key_str("stream", "audio");
        decode_audio_data(dh, dh_cap, j);
        break;

    // ---- Symbol ----
    case FT_SYMBOLDATA:
        j.key_str("stream", "symbol_data");
        decode_symbol_data(dh, dh_cap, j);
        break;

    // ---- Time domain / instantaneous ----
    case FT_TIMEDOMAIN_DATA:
        j.key_str("stream", "time_domain");
        decode_timedomain(dh, dh_cap, j);
        break;
    case FT_INSTANTANEOUSDATA:
        j.key_str("stream", "instantaneous");
        decode_instantaneous(dh, dh_cap, j);
        break;

    // ---- Detector / emission ----
    case FT_EMISSION_LIST_DATA:
        j.key_str("stream", "emission_list");
        decode_emission_list(dh, dh_cap, j);
        break;
    case FT_SPECTRALDETECTOR_DATA:
        j.key_str("stream", "spectral_detector_list");
        decode_spectral_detector(dh, dh_cap, j);
        break;
    case FT_BURST_EMISSIONS_LIST:
    case FT_EXT_BURST_EMISSIONS_LIST:
        j.key_str("stream", "burst_emission_list");
        decode_burst_emission(dh, dh_cap, j);
        break;

    // ---- Statistics ----
    case FT_HISTOGRAM_DATA:
        j.key_str("stream", "histogram");
        decode_histogram(dh, dh_cap, j);
        break;
    case FT_HOP_DENSITY_WATERFALL_DATA:
        j.key_str("stream", "hop_density_waterfall");
        // no byte-table in ICD for this header; body fields only
        break;

    // ---- PDW / IQDW ----
    case FT_PULSE_DESCRIPTION_WORD_DATA:
        j.key_str("stream", "pdw");
        decode_pdw(dh, dh_cap, j);
        break;
    case FT_IQDW_16RE_16IM_FIX:
        j.key_str("stream", "iqdw_16bit");
        break;
    case FT_IQDW_32RE_32IM_FIX:
        j.key_str("stream", "iqdw_32bit");
        break;
    case FT_PULSE_REPETITION_WORD_DATA:
        j.key_str("stream", "prw");
        break;
    case FT_BLANKING_DESCRIPTION_WORD_DATA:
        j.key_str("stream", "blanking_description");
        break;

    // ---- Decoder outputs ----
    case FT_DECODER_TEXT_DATA:
        j.key_str("stream", "decoder_text");
        break;
    case FT_IMAGEDATA:
        j.key_str("stream", "decoder_image");
        break;
    case FT_TRANSMISSION_SYSTEM_RESULT_DATA:
        j.key_str("stream", "transmission_system_result");
        break;

    // ---- Level indicators ----
    case FT_LEVELDATA:
    case FT_LEVELDATA_DEM:
        j.key_str("stream", "level_indicator");
        break;

    // ---- Fallback for unknown/reserved types ----
    default:
        j.key_str("stream", "unknown");
        break;
    }

    return j.str();
}

// ---------------------------------------------------------------------------
// parse_xml — converts an XML Request / Reply / Event frame to JSON
// ---------------------------------------------------------------------------
//
// The CA120 XML protocol is capability-tree style: clients set/get nodes
// inside named subsystem trees rather than invoking numbered commands.
// The JSON output provides routing metadata + extracted signal parameters.
// The full raw XML is also emitted so the Python bridge can do richer parsing
// using its own XML library.
//
// Known root node names (§3.2 of understanding doc, §5.x of ICD):
//   ResourceManager, Control, Tuner/tuner, FFT, DetectAndClassify,
//   FrequencyHopping, Squelch, Classifier, AnalogDemodulator,
//   DigitalDemodulator, BitstreamProcessing, HopperFilterSeparation

static std::string parse_xml(const uint8_t* frame, int frame_len, int frame_type) {
    const char* xml = reinterpret_cast<const char*>(frame);

    JsonWriter j;
    j.key_str("hw",       "ca120");
    j.key_str("channel",  "xml");

    // Determine outermost element kind
    bool is_event = false;
    if (frame_len > 6) {
        if (memcmp(xml, "<Event", 6) == 0)  is_event = true;
    }
    j.key_str("msg_kind", (frame_type == 1) ? "request"
                        : is_event          ? "event"
                                            : "reply");

    // Root-level attributes
    std::string type_attr = xml_attr(xml, frame_len, "type");
    std::string id_attr   = xml_attr(xml, frame_len, "id");
    std::string src_attr  = xml_attr(xml, frame_len, "source");
    if (!type_attr.empty()) j.key_str("msg_type",  type_attr.c_str());
    if (!id_attr.empty())   j.key_str("msg_id",    id_attr.c_str());
    if (!src_attr.empty())  j.key_str("event_source", src_attr.c_str());

    // Collect all subsystem root-node names present in this message
    static const struct { const char* tag; const char* name; } kSubsystems[] = {
        { "ResourceManager",       "resource_manager"       },
        { "Control",               "control"                },
        { "Tuner",                 "tuner"                  },
        { "tuner",                 "tuner"                  },
        { "FFT",                   "fft"                    },
        { "DetectAndClassify",     "detect_and_classify"    },
        { "FrequencyHopping",      "frequency_hopping"      },
        { "Squelch",               "squelch"                },
        { "Classifier",            "classifier"             },
        { "AnalogDemodulator",     "analog_demodulator"     },
        { "DigitalDemodulator",    "digital_demodulator"    },
        { "BitstreamProcessing",   "bitstream_processing"   },
        { "HopperFilterSeparation","hopper_filter_separation"},
    };
    std::string sub_json("[");
    bool first = true;
    for (auto& s : kSubsystems) {
        if (xml_has(xml, frame_len, s.tag)) {
            if (!first) sub_json += ',';
            sub_json += '"';
            sub_json += s.name;
            sub_json += '"';
            first = false;
        }
    }
    sub_json += ']';
    j.key_raw("subsystems", sub_json);

    // DataStream action/type and target endpoint (§5.5)
    if (xml_has(xml, frame_len, "DataStream")) {
        std::string action = xml_attr(xml, frame_len, "action");
        std::string dtype  = xml_attr(xml, frame_len, "type");
        if (!action.empty()) j.key_str("datastream_action", action.c_str());
        if (!dtype.empty())  j.key_str("datastream_type",   dtype.c_str());
        std::string ip   = xml_text(xml, frame_len, "IP");
        std::string port = xml_text(xml, frame_len, "Port");
        if (!ip.empty())   j.key_str("stream_ip",   ip.c_str());
        if (!port.empty()) j.key_str("stream_port",  port.c_str());
    }

    // StartApplication GUID in reply (§9.1.1) — the application context GUID
    if (xml_has(xml, frame_len, "StartApplication")) {
        std::string guid = xml_attr(xml, frame_len, "guid");
        if (!guid.empty()) j.key_str("app_guid", guid.c_str());
    }

    // Event detection parameters (§9.1.3 — direct children on <Event>)
    std::string status = xml_text(xml, frame_len, "Status");
    std::string freq   = xml_text(xml, frame_len, "Frequency");
    std::string bw     = xml_text(xml, frame_len, "Bandwidth");
    if (!status.empty()) j.key_str("status",       status.c_str());
    if (!freq.empty())   j.key_str("frequency_hz",  freq.c_str());
    if (!bw.empty())     j.key_str("bandwidth_hz",  bw.c_str());

    // Full raw XML for Python-side deep parsing
    // Capped to avoid unbounded JSON for very large replies (e.g. AvailableDemodulators)
    static constexpr int RAW_XML_CAP = 16384;
    int raw_len = (frame_len < RAW_XML_CAP) ? frame_len : RAW_XML_CAP;
    j.key_str("raw_xml", std::string(xml, (size_t)raw_len));

    return j.str();
}

// ---------------------------------------------------------------------------
// extract_frame  (ABI entry point)
// ---------------------------------------------------------------------------
//
// Channel detection:
//   AMMOS: buf[0..3] == 0xFB746572
//   XML:   first non-whitespace char == '<'
//
// Returns:
//   1  = XML <Request>  (DRS-bridge → CA120 command)
//   2  = XML <Reply> or <Event> (CA120 → DRS-bridge)
//   3  = AMMOS binary stream frame (CA120 → DRS-bridge)
//   0  = incomplete frame, call again with more data
//  -1  = corrupt / unrecognised framing

SDFC_EXPORT int extract_frame(const uint8_t* buf,
                               size_t         buf_len,
                               uint8_t**      out_frame,
                               size_t*        out_len)
{
    if (!buf || buf_len < 4 || !out_frame || !out_len) return -1;

    int ibuf = static_cast<int>(buf_len);

    // ---- AMMOS path ----
    if (load_u32le(buf) == AMMOS_MAGIC) {
        if (ibuf < 8) return -1;  // need FrameLength
        uint32_t frame_words = load_u32le(buf + 4);
        if (frame_words < 6u) return -1;  // minimum 6-word frame header

        // IQDW (0x201 / 0x202) may legally exceed AMMOS_MAX_WORDS (§6.10 note)
        bool is_iqdw = false;
        if (ibuf >= 16) {
            uint32_t ft = load_u32le(buf + 12);
            is_iqdw = (ft == FT_IQDW_16RE_16IM_FIX || ft == FT_IQDW_32RE_32IM_FIX);
        } else if (frame_words > AMMOS_MAX_WORDS) {
            return -1;  // wait for FrameType field before deciding
        }

        if (!is_iqdw && frame_words > AMMOS_MAX_WORDS) return -1;

        // Guard against integer overflow before multiply
        if (frame_words > 0x3FFFFFFFu) return -1;
        int total = (int)(frame_words * 4u);
        if (!is_iqdw && total > MAX_FRAME_BUFFER_BYTES) return -1;
        if (ibuf < total) return -1;

        auto* p = static_cast<uint8_t*>(std::malloc((size_t)total));
        if (!p) return -1;
        memcpy(p, buf, (size_t)total);
        *out_frame = p;
        *out_len   = (size_t)total;
        return 0;
    }

    // ---- XML path ----
    // Skip leading whitespace
    int start = 0;
    while (start < ibuf && (buf[start] == ' '  || buf[start] == '\t' ||
                             buf[start] == '\r' || buf[start] == '\n'))
        ++start;

    if (start >= ibuf || buf[start] != '<') return -1;

    // Identify root tag and assign frame type
    const char* xml = reinterpret_cast<const char*>(buf + start);
    int xml_avail   = ibuf - start;

    struct { const char* tag; int ftype; } roots[] = {
        { "Request", 1 },
        { "Reply",   2 },
        { "Event",   2 },
    };
    int  ftype    = 0;
    const char* root_tag = nullptr;
    for (auto& r : roots) {
        int tlen = (int)strlen(r.tag);
        if (xml_avail > tlen + 1 &&
            memcmp(xml + 1, r.tag, (size_t)tlen) == 0)
        {
            char nx = xml[1 + tlen];
            if (nx == ' ' || nx == '>' || nx == '\r' || nx == '\n') {
                ftype    = r.ftype;
                root_tag = r.tag;
                break;
            }
        }
    }
    if (!root_tag) return -1;

    int end = xml_closing_end(buf + start, xml_avail, root_tag);
    if (end < 0) return -1;  // closing tag not yet received

    int total = start + end;
    auto* p = static_cast<uint8_t*>(std::malloc((size_t)total));
    if (!p) return -1;
    memcpy(p, buf + start, (size_t)total);
    *out_frame = p;
    *out_len   = (size_t)total;
    return 0;
}

// ---------------------------------------------------------------------------
// parse_message  (ABI entry point)
// ---------------------------------------------------------------------------

SDFC_EXPORT int parse_message(const uint8_t* frame, size_t frame_len,
                               char** out_json, size_t* out_len)
{
    if (!frame || frame_len < 4 || !out_json || !out_len) return -1;

    int iframe = static_cast<int>(frame_len);
    std::string result;
    if (load_u32le(frame) == AMMOS_MAGIC) {
        result = parse_ammos(frame, iframe);
    } else {
        // Infer XML frame type from root tag
        struct { const char* tag; int ftype; } roots[] = {
            { "Request", 1 },
            { "Reply",   2 },
            { "Event",   2 },
        };
        const char* xml = reinterpret_cast<const char*>(frame);
        int ftype = 0;
        for (auto& r : roots) {
            int tlen = (int)strlen(r.tag);
            if (iframe > tlen + 1 &&
                memcmp(xml + 1, r.tag, (size_t)tlen) == 0)
            {
                char nx = xml[1 + tlen];
                if (nx == ' ' || nx == '>' || nx == '\r' || nx == '\n') {
                    ftype = r.ftype;
                    break;
                }
            }
        }
        if (ftype > 0)
            result = parse_xml(frame, iframe, ftype);
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
// Encodes a JSON descriptor into a CA120 XML <Request> wire frame.
//
// Required JSON fields:
//   "msg_type"   : "set" | "get" | "suppress"
//   "id"         : integer request correlation ID
//   "xml_body"   : inner XML string (children of <Request>), JSON-escaped
// Optional:
//   "time"       : integer CA120 time value (µs)
//
// Writes UTF-8 XML bytes to out_frame.
// Returns total bytes written, or -1 on encoding error.

SDFC_EXPORT int format_response(const char* /*kind*/, const char* kwargs_json,
                                 uint8_t** out_buf, size_t* out_len)
{
    if (!kwargs_json || !out_buf || !out_len) return -1;

    std::string msg_type = json_str_field(kwargs_json, "msg_type");
    std::string xml_body = json_str_field(kwargs_json, "xml_body");
    long long   id       = json_int_field(kwargs_json, "id");
    long long   time_val = json_int_field(kwargs_json, "time");

    if (msg_type.empty() || xml_body.empty() || id < 0) return -1;

    // Build opening tag
    char hdr[256];
    int  hlen;
    if (time_val > 0) {
        hlen = std::snprintf(hdr, sizeof(hdr),
            "<Request type=\"%s\" id=\"%lld\" time=\"%lld\">",
            msg_type.c_str(), id, time_val);
    } else {
        hlen = std::snprintf(hdr, sizeof(hdr),
            "<Request type=\"%s\" id=\"%lld\">",
            msg_type.c_str(), id);
    }
    if (hlen <= 0 || hlen >= (int)sizeof(hdr)) return -1;

    static const char kFooter[]   = "</Request>";
    static const int  kFooterLen  = (int)(sizeof(kFooter) - 1);

    int body_len  = (int)xml_body.size();
    int total     = hlen + body_len + kFooterLen;

    if (total > MAX_FRAME_BUFFER_BYTES) return -1;

    auto* buf = static_cast<uint8_t*>(std::malloc((size_t)total));
    if (!buf) return -1;
    uint8_t* p = buf;
    memcpy(p, hdr,               (size_t)hlen);       p += hlen;
    memcpy(p, xml_body.c_str(),  (size_t)body_len);   p += body_len;
    memcpy(p, kFooter,           (size_t)kFooterLen);

    *out_buf = buf;
    *out_len = (size_t)total;
    return 0;
}

// ---------------------------------------------------------------------------
// free_result  (ABI entry point)
// ---------------------------------------------------------------------------

SDFC_EXPORT void free_result(void* ptr)
{
    std::free(ptr);
}
