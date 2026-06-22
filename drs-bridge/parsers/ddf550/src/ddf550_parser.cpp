// drs-bridge/parsers/ddf550/src/ddf550_parser.cpp
//
// R&S DDF-550 Direction Finding Receiver parser DLL.
// ICD: R&S-DDF-550-ICD-V13  (§-references below are to that document)
//
// Implements sdfc_abi.h on four channels:
//   XML control      (TCP 9150) — BE binary wrapper; bidirectional
//   EB200 mass data  (TCP 9152) — big-endian binary; DDF → SDFC
//   DDFCL control    (TCP 9153) — BE binary wrapper; bidirectional
//   DDFCL output     (TCP 9154) — raw XML; DDF → SDFC  (FORMAT02 DFData)
//
// Frame type mapping (returned by extract_frame):
//   1 = <Request> / <DDFCLRequest>   (SDFC → DDF command)
//   2 = <Reply> / <DDFCLReply> / <DFData> (DDF → SDFC)
//   3 = EB200 binary streaming frame (DDF → SDFC, port 9152)
//
// NOTE: XML wrapper magic words (§3.3.1) are NOT specified in this ICD.
// The constants XML_MAGIC_START / XML_MAGIC_END below are placeholders.
// Confirm from a live capture or the R&S SCIF system manual before
// using format_response output on real hardware.  extract_frame uses a
// content-based heuristic (length field + first XML byte) so it works
// regardless of the actual magic values.

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
// EB200 and framing constants  (§5.1.7, §3.3.1)
// ---------------------------------------------------------------------------

static constexpr uint32_t EB200_MAGIC        = 0x000EB200u;  // BE: 00 0E B2 00
static constexpr int      EB200_HDR_BYTES    = 16;

// Conventional GenericAttribute header (Table 3): tag(2) + len(2)
static constexpr int GA_CONV_HDR  = 4;
// Advanced GenericAttribute header (Table 4): tag(2) + res(2) + len(4) + res(16)
static constexpr int GA_ADV_HDR   = 24;

// Conventional TraceAttribute header (Table 6): n(2) + res(1) + opt_len(1) + flags(4)
static constexpr int TA_CONV_HDR  = 8;
// Advanced TraceAttribute header (Table 7): n(4)+res(4)+opt_len(4)+flags_lo(4)+flags_hi(4)+res(16)
static constexpr int TA_ADV_HDR   = 36;

// Audio optional header (Table 12)
static constexpr int AUDIO_OPT_HDR_BYTES = 42;

// Selector flag bits (Table 8)
static constexpr uint32_t SEL_OPTIONAL_HEADER = 0x80000000u;

// Trace tag decimal values (Table 5)
static constexpr uint16_t TAG_AUDIO      = 401u;
static constexpr uint16_t TAG_IFPAN      = 501u;
static constexpr uint16_t TAG_CW         = 801u;
static constexpr uint16_t TAG_IF_IQ      = 901u;
static constexpr uint16_t TAG_VIDEO      = 1001u;
static constexpr uint16_t TAG_VIDEOPAN   = 1101u;
static constexpr uint16_t TAG_PSCAN      = 1201u;
static constexpr uint16_t TAG_SELCALL    = 1301u;
static constexpr uint16_t TAG_GPSCOMPASS = 1801u;
static constexpr uint16_t TAG_ANTLEVEL   = 1901u;
static constexpr uint16_t TAG_DFPSCAN    = 5301u;  // advanced
static constexpr uint16_t TAG_SIGP       = 5501u;  // advanced
static constexpr uint16_t TAG_HRPAN      = 5601u;  // advanced

// Tags >= 5000 use advanced GenericAttribute/TraceAttribute format
static constexpr uint16_t TAG_ADVANCED_THRESHOLD = 5000u;

// XML wrapper magic words — PLACEHOLDER; verify from live capture.
static constexpr uint32_t XML_MAGIC_START = 0x00000000u;
static constexpr uint32_t XML_MAGIC_END   = 0x00000000u;

// ---------------------------------------------------------------------------
// Big-endian read helpers  (DDF-550 is entirely big-endian)
// ---------------------------------------------------------------------------

static uint16_t load_u16be(const uint8_t* p) {
    return (static_cast<uint16_t>(p[0]) << 8) |
            static_cast<uint16_t>(p[1]);
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

static uint64_t load_u64be(const uint8_t* p) {
    return (static_cast<uint64_t>(load_u32be(p)) << 32) |
            static_cast<uint64_t>(load_u32be(p + 4));
}

static void store_u32be(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>((v >> 24) & 0xFFu);
    p[1] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    p[2] = static_cast<uint8_t>((v >>  8) & 0xFFu);
    p[3] = static_cast<uint8_t>( v        & 0xFFu);
}

// ---------------------------------------------------------------------------
// Helpers: XML scanning  (no LGPL)
// ---------------------------------------------------------------------------

// Find byte offset PAST the first "</tag>" occurrence.  Returns -1 if not found.
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

// Extract attribute value from xml[0..len).  Handles single and double quotes.
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

// Extract text between <tag> (with optional attributes) and </tag>.
static std::string xml_text(const char* xml, int len, const char* tag) {
    std::string open("<");
    open += tag;
    const char* end = xml + len;
    const char* p   = xml;
    while (p < end) {
        p = std::search(p, end, open.data(), open.data() + open.size());
        if (p >= end) break;
        const char* nx = p + open.size();
        if (nx >= end) break;
        if (*nx != ' ' && *nx != '>' && *nx != '/' &&
            *nx != '\r' && *nx != '\n') { p = nx; continue; }
        // Advance to closing '>'
        const char* gt = std::find(nx, end, '>');
        if (gt >= end) break;
        if (gt > nx && *(gt - 1) == '/') return {};  // self-closing
        const char* ts = gt + 1;
        char close[80];
        std::snprintf(close, sizeof(close), "</%s>", tag);
        const char* te = std::search(ts, end, close, close + strlen(close));
        if (te >= end) return {};
        return std::string(ts, te);
    }
    return {};
}

// Extract VALUE from <Param name="param_name">VALUE</Param>
static std::string xml_param_value(const char* xml, int len, const char* param_name) {
    std::string search(" name=\"");
    search += param_name;
    search += "\">";
    const char* end = xml + len;
    const char* p = std::search(xml, end, search.data(), search.data() + search.size());
    if (p >= end) return {};
    const char* vs = p + search.size();
    const char* ve = std::search(vs, end, "</Param>", "</Param>" + 8);
    if (ve >= end) return {};
    return std::string(vs, ve);
}

// ---------------------------------------------------------------------------
// JSON field helpers  (used by format_response)
// ---------------------------------------------------------------------------

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
// Enum-to-string helpers
// ---------------------------------------------------------------------------

static const char* trace_tag_name(uint16_t tag) {
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

static const char* demod_str(uint16_t v) {
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

// ---------------------------------------------------------------------------
// XML root tag classification (skips <?xml ...?> declaration)
// Returns frame type (1/2) and sets *root_tag, or returns 0 if unrecognised.
// ---------------------------------------------------------------------------

static int classify_xml_root(const uint8_t* data, int data_len,
                              const char** root_tag)
{
    const char* p   = reinterpret_cast<const char*>(data);
    const char* end = p + data_len;

    // Skip leading whitespace / BOM
    while (p < end && static_cast<unsigned char>(*p) <= 0x20u) ++p;
    if (p >= end || *p != '<') return 0;

    // Skip XML declaration <?xml ... ?>
    if (end - p >= 5 && memcmp(p, "<?xml", 5) == 0) {
        while (p < end - 1 && !(p[0] == '?' && p[1] == '>')) ++p;
        if (p < end - 1) p += 2;
        while (p < end && static_cast<unsigned char>(*p) <= 0x20u) ++p;
    }
    if (p >= end || *p != '<') return 0;

    static const struct { const char* tag; int ftype; } kRoots[] = {
        { "DDFCLRequest", 1 },   // check longer prefixes first
        { "DDFCLReply",   2 },
        { "Request",      1 },
        { "Reply",        2 },
        { "DFData",       2 },
    };
    int avail = (int)(end - p);
    for (auto& r : kRoots) {
        int tlen = (int)strlen(r.tag);
        if (avail > tlen + 1 && memcmp(p + 1, r.tag, (size_t)tlen) == 0) {
            char nx = p[1 + tlen];
            if (nx == ' ' || nx == '>' || nx == '\r' || nx == '\n') {
                if (root_tag) *root_tag = r.tag;
                return r.ftype;
            }
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Audio optional header decoder  (Table 12, §5.1.7.7)
// ---------------------------------------------------------------------------

static void decode_audio_opt_header(const uint8_t* oh, int oh_bytes, JsonWriter& j) {
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

// ---------------------------------------------------------------------------
// parse_eb200 — decodes one full EB200 packet into JSON
// ---------------------------------------------------------------------------

static std::string parse_eb200(const uint8_t* pkt, int pkt_len) {
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
    j.key_str ("hw",        "ddf550");
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

// ---------------------------------------------------------------------------
// parse_xml_ddf550 — decodes one XML frame (control / DDFCL / DFData) to JSON
// ---------------------------------------------------------------------------

static std::string parse_xml_ddf550(const uint8_t* frame, int frame_len,
                                     int frame_type)
{
    const char* xml     = reinterpret_cast<const char*>(frame);
    int         xml_len = frame_len;

    // Skip XML declaration if present
    if (xml_len > 5 && memcmp(xml, "<?xml", 5) == 0) {
        const char* end = xml + xml_len;
        const char* p   = xml;
        while (p < end - 1 && !(p[0] == '?' && p[1] == '>')) ++p;
        if (p < end - 1) p += 2;
        while (p < end && static_cast<unsigned char>(*p) <= 0x20u) ++p;
        xml     = p;
        xml_len = (int)(end - p);
    }

    bool is_ddfcl_req = (xml_len > 12 && memcmp(xml, "<DDFCLReques", 12) == 0);
    bool is_ddfcl_rep = (xml_len > 11 && memcmp(xml, "<DDFCLReply",  11) == 0);
    bool is_dfdata    = (xml_len >  7 && memcmp(xml, "<DFData",       7) == 0);

    const char* channel  = is_dfdata           ? "preclassifier_output"
                         : (is_ddfcl_req || is_ddfcl_rep) ? "preclassifier"
                                                           : "control";
    const char* msg_kind = (frame_type == 1)   ? "request"
                         : is_dfdata           ? "dfdata"
                                               : "reply";

    JsonWriter j;
    j.key_str("hw",       "ddf550");
    j.key_str("channel",  channel);
    j.key_str("msg_kind", msg_kind);

    // Root-level attributes
    std::string id_attr   = xml_attr(xml, xml_len, "id");
    std::string type_attr = xml_attr(xml, xml_len, "type");
    if (!id_attr.empty())   j.key_str("msg_id",   id_attr.c_str());
    if (!type_attr.empty()) j.key_str("msg_type", type_attr.c_str());

    // Command name — attribute of <Command name="...">
    {
        const char* tag_start = std::strstr(xml, "<Command");
        if (tag_start && (tag_start - xml) < xml_len) {
            int avail = xml_len - (int)(tag_start - xml);
            std::string cmd = xml_attr(tag_start, avail, "name");
            if (!cmd.empty()) j.key_str("command_name", cmd.c_str());
        }
    }

    // Key parameters — try both <Param name="...">VALUE</Param> and
    // direct <TagName>VALUE</TagName> forms.
    {
        std::string freq = xml_param_value(xml, xml_len, "iFrequency");
        if (freq.empty()) freq = xml_param_value(xml, xml_len, "iFreqBegin");
        if (freq.empty()) freq = xml_text(xml, xml_len, "iFrequency");
        if (!freq.empty()) j.key_str("frequency_hz", freq.c_str());
    }
    {
        std::string v = xml_param_value(xml, xml_len, "eOperationMode");
        if (v.empty()) v = xml_text(xml, xml_len, "eOperationMode");
        if (!v.empty()) j.key_str("operation_mode", v.c_str());
    }
    {
        std::string v = xml_param_value(xml, xml_len, "eAudioMode");
        if (v.empty()) v = xml_text(xml, xml_len, "eAudioMode");
        if (!v.empty()) j.key_str("audio_mode_str", v.c_str());
    }
    {
        std::string v = xml_param_value(xml, xml_len, "eDemodulation");
        if (v.empty()) v = xml_text(xml, xml_len, "eDemodulation");
        if (!v.empty()) j.key_str("demodulation", v.c_str());
    }

    // TraceEnable / TraceDisable / TraceDelete params
    {
        std::string tt = xml_param_value(xml, xml_len, "eTraceTag");
        std::string ip = xml_param_value(xml, xml_len, "zIP");
        std::string pt = xml_param_value(xml, xml_len, "iPort");
        if (!tt.empty()) j.key_str("trace_tag_str", tt.c_str());
        if (!ip.empty()) j.key_str("trace_ip",      ip.c_str());
        if (!pt.empty()) j.key_str("trace_port",    pt.c_str());
    }

    // DFData preclassifier output fields  (FORMAT02, §6.7.3)
    if (is_dfdata) {
        std::string cl_id  = xml_attr(xml, xml_len, "DDF-CL-ID");
        std::string eclass = xml_text(xml, xml_len, "EmitterClass");
        std::string cfreq  = xml_text(xml, xml_len, "CenterFrequency");
        std::string bear   = xml_text(xml, xml_len, "BearingAvg");
        std::string lvl    = xml_text(xml, xml_len, "LevelAvg");
        if (!cl_id.empty())  j.key_str("ddf_cl_id",       cl_id.c_str());
        if (!eclass.empty()) j.key_str("emitter_class",    eclass.c_str());
        if (!cfreq.empty())  j.key_str("center_freq_hz",   cfreq.c_str());
        if (!bear.empty())   j.key_str("bearing_avg_deg",  bear.c_str());
        if (!lvl.empty())    j.key_str("level_avg_dbuv",   lvl.c_str());
    }

    // Raw XML (capped) for Python-side deep parsing
    static constexpr int RAW_XML_CAP = 16384;
    const char* orig_xml = reinterpret_cast<const char*>(frame);
    int raw_len = (frame_len < RAW_XML_CAP) ? frame_len : RAW_XML_CAP;
    j.key_str("raw_xml", std::string(orig_xml, (size_t)raw_len));

    return j.str();
}

// ---------------------------------------------------------------------------
// extract_frame  (ABI entry point)
// ---------------------------------------------------------------------------
//
// Channel detection priority:
//   1. EB200: buf[0..3] == 0x000EB200 (BE)
//   2. Raw XML: first non-whitespace byte == '<'  (port 9154 DFData, or unwrapped)
//   3. Wrapped XML (§3.3.1): heuristic — buf[4..7] BE is a plausible XML length,
//      and buf[8] == '<'.  Magic words are not checked (values not in ICD).
//
// For wrapped XML (case 3): the binary envelope is stripped; out_frame receives
// the raw XML bytes only (consistent with the raw-XML and CA120 paths).
//
// Returns: 1=command  2=response  3=EB200 stream  0=incomplete  -1=corrupt

SDFC_EXPORT int extract_frame(const uint8_t* buf,
                               size_t         buf_len,
                               uint8_t**      out_frame,
                               size_t*        out_len)
{
    if (!buf || buf_len < 4 || !out_frame || !out_len) return -1;

    int ibuf = static_cast<int>(buf_len);

    // ---- EB200 path ----
    if (load_u32be(buf) == EB200_MAGIC) {
        if (ibuf < EB200_HDR_BYTES) return -1;
        uint32_t data_size = load_u32be(buf + 12);
        if (data_size < (uint32_t)EB200_HDR_BYTES) return -1;
        if (data_size > (uint32_t)MAX_FRAME_BUFFER_BYTES) return -1;
        if (ibuf < (int)data_size) return -1;
        auto* p = static_cast<uint8_t*>(std::malloc(data_size));
        if (!p) return -1;
        memcpy(p, buf, data_size);
        *out_frame = p;
        *out_len   = (size_t)data_size;
        return 0;
    }

    // ---- Raw XML path (e.g. DFData on port 9154, or pre-stripped XML) ----
    int start = 0;
    while (start < ibuf &&
           (buf[start] == ' '  || buf[start] == '\t' ||
            buf[start] == '\r' || buf[start] == '\n'))
        ++start;

    if (start < ibuf && buf[start] == '<') {
        const char* root_tag = nullptr;
        int ftype = classify_xml_root(buf + start, ibuf - start, &root_tag);
        if (!root_tag || ftype == 0) return -1;

        int end = xml_closing_end(buf + start, ibuf - start, root_tag);
        if (end < 0) return -1;
        int total = start + end;
        auto* p = static_cast<uint8_t*>(std::malloc((size_t)total));
        if (!p) return -1;
        memcpy(p, buf + start, (size_t)total);
        *out_frame = p;
        *out_len   = (size_t)total;
        return 0;
    }

    // ---- Wrapped XML path (§3.3.1): [magic4][len4 BE][xml_n][magic4] ----
    // Need at least: magic(4) + len(4) + first XML byte(1) = 9 bytes to probe.
    if (ibuf < 9) return -1;

    uint32_t xml_n = load_u32be(buf + 4);
    // Plausibility: a valid XML length is > 0 and leaves room for the 12-byte envelope.
    if (xml_n > 0 &&
        xml_n <= (uint32_t)(MAX_FRAME_BUFFER_BYTES - 12) &&
        buf[8] == '<')
    {
        int total_wrapped = 12 + (int)xml_n;   // magic(4) + len(4) + xml + magic(4)
        if (ibuf < total_wrapped) return -1;  // wait for full frame

        const char* root_tag = nullptr;
        int ftype = classify_xml_root(buf + 8, (int)xml_n, &root_tag);
        if (!root_tag || ftype == 0) return -1;

        // Strip the binary envelope; hand raw XML to parse_message.
        auto* p = static_cast<uint8_t*>(std::malloc(xml_n));
        if (!p) return -1;
        memcpy(p, buf + 8, xml_n);
        *out_frame = p;
        *out_len   = (size_t)xml_n;
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
    if (!frame || frame_len < 4 || !out_json || !out_len) return -1;

    int iframe = static_cast<int>(frame_len);
    std::string result;
    if (load_u32be(frame) == EB200_MAGIC) {
        result = parse_eb200(frame, iframe);
    } else {
        const char* root_tag = nullptr;
        int ftype = classify_xml_root(frame, iframe, &root_tag);
        if (root_tag && ftype > 0)
            result = parse_xml_ddf550(frame, iframe, ftype);
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
// Encodes a JSON descriptor into a DDF-550 wrapped XML wire frame.
//
// Required JSON fields:
//   "msg_type"     : "get" | "set"
//   "id"           : integer request correlation ID
//   "command_name" : DDF-550 Command name (e.g. "DfMode", "AudioMode")
//   "xml_body"     : inner XML (Param children), JSON-escaped
//
// Optional:
//   "channel"      : "preclassifier"  → DDFCLRequest root tag
//                    (any other value) → Request root tag
//
// Output: [magic_start(4 BE)][length(4 BE)][XML bytes][magic_end(4 BE)]
// Magic word values are XML_MAGIC_START / XML_MAGIC_END (placeholders —
// verify from live capture before using on real hardware).
//
// Returns total bytes written, or -1 on encoding error.

SDFC_EXPORT int format_response(const char* /*kind*/, const char* kwargs_json,
                                 uint8_t** out_buf, size_t* out_len)
{
    if (!kwargs_json || !out_buf || !out_len) return -1;

    std::string msg_type    = json_str_field(kwargs_json, "msg_type");
    std::string cmd_name    = json_str_field(kwargs_json, "command_name");
    std::string xml_body    = json_str_field(kwargs_json, "xml_body");
    std::string channel     = json_str_field(kwargs_json, "channel");
    long long   id          = json_int_field(kwargs_json, "id");

    if (msg_type.empty() || cmd_name.empty() || id < 0) return -1;

    bool use_ddfcl = (channel == "preclassifier");

    // Build the inner XML
    char xml_hdr[512];
    int xml_hdr_len;
    if (use_ddfcl) {
        xml_hdr_len = std::snprintf(xml_hdr, sizeof(xml_hdr),
            "<DDFCLRequest id=\"%lld\" type=\"%s\">",
            id, msg_type.c_str());
    } else {
        xml_hdr_len = std::snprintf(xml_hdr, sizeof(xml_hdr),
            "<Request type=\"%s\" id=\"%lld\">",
            msg_type.c_str(), id);
    }
    if (xml_hdr_len <= 0 || xml_hdr_len >= (int)sizeof(xml_hdr)) return -1;

    char cmd_open[256], cmd_close[256];
    int cmd_open_len = std::snprintf(cmd_open, sizeof(cmd_open),
        "<Command name=\"%s\">", cmd_name.c_str());
    int cmd_close_len;
    if (use_ddfcl) {
        cmd_close_len = std::snprintf(cmd_close, sizeof(cmd_close),
            "</Command></DDFCLRequest>");
    } else {
        cmd_close_len = std::snprintf(cmd_close, sizeof(cmd_close),
            "</Command></Request>");
    }
    if (cmd_open_len  <= 0 || cmd_open_len  >= (int)sizeof(cmd_open))  return -1;
    if (cmd_close_len <= 0 || cmd_close_len >= (int)sizeof(cmd_close)) return -1;

    int body_len = (int)xml_body.size();
    int xml_total = xml_hdr_len + cmd_open_len + body_len + cmd_close_len;

    // Binary envelope: 4 magic_start + 4 length + xml_total + 4 magic_end = xml_total + 12
    int frame_total = xml_total + 12;
    if (frame_total > MAX_FRAME_BUFFER_BYTES) return -1;

    auto* buf = static_cast<uint8_t*>(std::malloc((size_t)frame_total));
    if (!buf) return -1;
    uint8_t* p = buf;
    store_u32be(p, XML_MAGIC_START);   p += 4;
    store_u32be(p, (uint32_t)xml_total); p += 4;
    memcpy(p, xml_hdr,              (size_t)xml_hdr_len);   p += xml_hdr_len;
    memcpy(p, cmd_open,             (size_t)cmd_open_len);  p += cmd_open_len;
    memcpy(p, xml_body.c_str(),     (size_t)body_len);      p += body_len;
    memcpy(p, cmd_close,            (size_t)cmd_close_len); p += cmd_close_len;
    store_u32be(p, XML_MAGIC_END);

    *out_buf = buf;
    *out_len = (size_t)frame_total;
    return 0;
}

// ---------------------------------------------------------------------------
// free_result  (ABI entry point)
// ---------------------------------------------------------------------------

SDFC_EXPORT void free_result(void* ptr)
{
    std::free(ptr);
}
