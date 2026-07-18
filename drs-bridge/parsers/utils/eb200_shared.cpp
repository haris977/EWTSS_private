#include "eb200_shared.h"

#include <cstring>

#include "sdfc_endian.h"

using namespace sdfc;

const char* trace_tag_name(uint16_t tag) {
    switch (tag) {
        case TAG_AUDIO:      return "audio";
        case TAG_IFPAN:      return "ifpan";
        case TAG_CW:         return "cw";
        case TAG_IF_IQ:      return "if_iq";
        case TAG_VIDEO:      return "video";
        case TAG_VIDEOPAN:   return "videopan";
        case TAG_PSCAN:      return "pscan";
        case TAG_SELCALL:    return "selcall";
        case TAG_GPSCOMPASS: return "gps_compass";
        case TAG_ANTLEVEL:   return "ant_level";
        case TAG_DFPSCAN:    return "dfpscan";
        case TAG_SIGP:       return "sigp";
        case TAG_HRPAN:      return "hrpan";
        default:             return "unknown";
    }
}

const char* demod_str(uint16_t v) {
    switch (v) {
        case 0: return "FM";
        case 1: return "AM";
        case 2: return "PULS";
        case 3: return "PM";
        case 4: return "IQ";
        case 5: return "ISB";
        case 6: return "CW";
        case 7: return "USB";
        case 8: return "LSB";
        case 9: return "TV";
        default: return "UNKNOWN";
    }
}

void decode_audio_opt_header(const uint8_t* oh, int oh_bytes, JsonWriter& j) {
    if (oh_bytes < AUDIO_OPT_HDR_BYTES) {
        j.key_str("warning", "audio optional header truncated");
        return;
    }
    int16_t  audio_mode  = load_i16be(oh + 0x00);
    int16_t  frame_len   = load_i16be(oh + 0x02);
    uint32_t freq_low    = load_u32be(oh + 0x04);
    uint32_t bandwidth   = load_u32be(oh + 0x08);
    uint16_t demod       = load_u16be(oh + 0x0C);
    char     demod_name[9] = {};
    memcpy(demod_name, oh + 0x0E, 8);  // ASCII left-aligned NUL-padded
    uint32_t freq_high   = load_u32be(oh + 0x16);
    // 6 bytes reserved at 0x1A
    uint64_t timestamp_ns = load_u64be(oh + 0x20);
    int16_t  sig_source   = load_i16be(oh + 0x28);

    uint64_t freq_hz = ((uint64_t)freq_high << 32) | freq_low;

    j.key_int ("audio_mode",    audio_mode);
    j.key_int ("frame_length",  frame_len);
    j.key_uint("freq_hz",       freq_hz);
    j.key_uint("bandwidth_hz",  bandwidth);
    j.key_int ("demod",         demod);
    j.key_str ("demod_str",     demod_str(demod));
    if (demod_name[0]) j.key_str("demod_name", demod_name);
    j.key_int ("timestamp_ns",  static_cast<long long>(timestamp_ns));
    j.key_int ("signal_source", sig_source);
}

std::string parse_eb200(const uint8_t* pkt, int pkt_len, const char* hw) {
    if (pkt_len < EB200_HDR_BYTES) return {};

    // EB200 header  (§5.1.7.2)
    uint16_t ver_minor  = load_u16be(pkt + 4);
    uint16_t ver_major  = load_u16be(pkt + 6);
    uint16_t seq_lo     = load_u16be(pkt + 8);
    uint16_t seq_hi     = load_u16be(pkt + 10);
    uint32_t data_size  = load_u32be(pkt + 12);
    (void)ver_minor;

    uint32_t seq_num = ((uint32_t)seq_hi << 16) | seq_lo;

    // GenericAttribute starts immediately after the 16-byte header
    const uint8_t* ga     = pkt + EB200_HDR_BYTES;
    int            ga_avail = pkt_len - EB200_HDR_BYTES;
    if (ga_avail < 2) return {};

    uint16_t trace_tag  = load_u16be(ga);
    bool     is_advanced = (trace_tag >= TAG_ADVANCED_THRESHOLD);

    // Locate TraceData and its length
    const uint8_t* td    = nullptr;
    int            td_len = 0;
    if (!is_advanced) {
        if (ga_avail < GA_CONV_HDR) return {};
        td_len = (int)load_u16be(ga + 2);
        td     = ga + GA_CONV_HDR;
    } else {
        if (ga_avail < GA_ADV_HDR) return {};
        td_len = (int)load_u32be(ga + 4);
        td     = ga + GA_ADV_HDR;
    }
    // Clamp to actual bytes available
    int td_avail = (int)(pkt + pkt_len - td);
    if (td_len > td_avail) td_len = td_avail;
    if (td_len < 0) td_len = 0;

    JsonWriter j;
    j.key_str ("hw",        hw);
    j.key_str ("stream",    "eb200");
    j.key_uint("trace_tag", trace_tag);
    j.key_str ("tag_name",  trace_tag_name(trace_tag));
    j.key_uint("seq_num",   seq_num);
    j.key_uint("ver_major", ver_major);
    j.key_uint("data_size", data_size);

    // Decode the TraceAttribute
    if (!is_advanced) {
        // Conventional TraceAttribute  (Table 6)
        if (td_len < TA_CONV_HDR) return j.str();
        int16_t  n_items   = load_i16be(td + 0);
        uint8_t  opt_len   = td[3];
        uint32_t sel_flags = load_u32be(td + 4);

        j.key_int ("n_items",     n_items);
        j.key_uint("sel_flags",   sel_flags);
        j.key_uint("opt_hdr_len", opt_len);

        int opt_avail = td_len - TA_CONV_HDR;
        if (opt_avail < 0) opt_avail = 0;
        if ((int)opt_len > opt_avail) opt_len = (uint8_t)opt_avail;

        const uint8_t* opt_hdr = td + TA_CONV_HDR;

        int periodic_len = td_len - TA_CONV_HDR - (int)opt_len;
        if (periodic_len < 0) periodic_len = 0;
        j.key_int("periodic_data_bytes", periodic_len);

        if (trace_tag == TAG_AUDIO &&
            (sel_flags & SEL_OPTIONAL_HEADER) &&
            (int)opt_len >= AUDIO_OPT_HDR_BYTES)
        {
            decode_audio_opt_header(opt_hdr, (int)opt_len, j);
        }
    } else {
        // Advanced TraceAttribute  (Table 7)
        if (td_len < TA_ADV_HDR) return j.str();
        uint32_t n_items   = load_u32be(td +  0);
        uint32_t opt_len   = load_u32be(td +  8);
        uint32_t flags_lo  = load_u32be(td + 12);
        uint32_t flags_hi  = load_u32be(td + 16);

        j.key_uint("n_items",      n_items);
        j.key_uint("sel_flags",    flags_lo);
        j.key_uint("sel_flags_hi", flags_hi);
        j.key_uint("opt_hdr_len",  opt_len);

        int opt_avail = td_len - TA_ADV_HDR;
        if (opt_avail < 0) opt_avail = 0;
        if ((int)opt_len > opt_avail) opt_len = (uint32_t)opt_avail;

        int periodic_len = td_len - TA_ADV_HDR - (int)opt_len;
        if (periodic_len < 0) periodic_len = 0;
        j.key_int("periodic_data_bytes", periodic_len);
    }

    return j.str();
}
