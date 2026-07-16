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
// extract_frame returns 0 on a complete frame, -1 otherwise (incomplete or
// corrupt — sdfc_abi.h's contract does not distinguish the two). Frame type
// is inferred separately, inside parse_message, from the frame's own bytes:
//   <Request> / <DDFCLRequest> / <DFSelect>        (SDFC → DDF command)
//   <Reply> / <DDFCLReply> / <DFData> / <Event>    (DDF → SDFC)
//   EB200 binary streaming frame (port 9152)        (DDF → SDFC)
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
#include "pugixml.hpp"
#include "pugixml_helpers.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>
#include <algorithm>
#include <vector>
#include <utility>

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

static uint16_t be_u16(const uint8_t* p) {
    return (static_cast<uint16_t>(p[0]) << 8) |
            static_cast<uint16_t>(p[1]);
}

static int16_t be_i16(const uint8_t* p) {
    return static_cast<int16_t>(be_u16(p));
}

static uint32_t be_u32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) <<  8) |
            static_cast<uint32_t>(p[3]);
}

static uint64_t be_u64(const uint8_t* p) {
    return (static_cast<uint64_t>(be_u32(p)) << 32) |
            static_cast<uint64_t>(be_u32(p + 4));
}

static void be_store_u32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>((v >> 24) & 0xFFu);
    p[1] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    p[2] = static_cast<uint8_t>((v >>  8) & 0xFFu);
    p[3] = static_cast<uint8_t>( v        & 0xFFu);
}

// ---------------------------------------------------------------------------
// Content parsing now uses pugixml (find_first from pugixml_helpers.h) --
// content parsing only; xml_closing_end below is frame-boundary detection
// and still byte-scanning, see extract_frame.
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

// Type by the ICD's own Hungarian-notation prefix (i=int, b=bool, else
// string) rather than sniffing the value text — a param named with an 'i'
// prefix is authoritatively an integer per the naming convention, whereas
// guessing from "is this all digits" would mis-cast e.g. a zero-padded ID.
static void write_typed_param(JsonWriter& j, const std::string& name, const std::string& val) {
    char prefix = name.empty() ? '\0' : name[0];
    if (prefix == 'i' && !val.empty()) {
        char* endp = nullptr;
        long long n = std::strtoll(val.c_str(), &endp, 10);
        if (endp && *endp == '\0') { j.key_int(name.c_str(), n); return; }
    } else if (prefix == 'b') {
        if (val == "true")  { j.key_bool(name.c_str(), true);  return; }
        if (val == "false") { j.key_bool(name.c_str(), false); return; }
    }
    j.key_str(name.c_str(), val);
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
        { "Event",        2 },   // async status/alarm/scan-complete (D6): DDF -> SDFC
        { "DFSelect",     1 },   // preclassifier filter command (icd-ddf550.md §4): SDFC -> DDF
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
    int16_t  audio_mode  = be_i16(oh + 0x00);
    int16_t  frame_len   = be_i16(oh + 0x02);
    uint32_t freq_low    = be_u32(oh + 0x04);
    uint32_t bandwidth   = be_u32(oh + 0x08);
    uint16_t demod       = be_u16(oh + 0x0C);
    char     demod_name[9] = {};
    memcpy(demod_name, oh + 0x0E, 8);  // ASCII left-aligned NUL-padded
    uint32_t freq_high   = be_u32(oh + 0x16);
    // 6 bytes reserved at 0x1A
    uint64_t timestamp_ns = be_u64(oh + 0x20);
    int16_t  sig_source   = be_i16(oh + 0x28);

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
    uint16_t ver_minor  = be_u16(pkt + 4);
    uint16_t ver_major  = be_u16(pkt + 6);
    uint16_t seq_lo     = be_u16(pkt + 8);
    uint16_t seq_hi     = be_u16(pkt + 10);
    uint32_t data_size  = be_u32(pkt + 12);
    (void)ver_minor;

    uint32_t seq_num = ((uint32_t)seq_hi << 16) | seq_lo;

    // GenericAttribute starts immediately after the 16-byte header
    const uint8_t* ga     = pkt + EB200_HDR_BYTES;
    int            ga_avail = pkt_len - EB200_HDR_BYTES;
    if (ga_avail < 2) return {};

    uint16_t trace_tag  = be_u16(ga);
    bool     is_advanced = (trace_tag >= TAG_ADVANCED_THRESHOLD);

    // Locate TraceData and its length
    const uint8_t* td    = nullptr;
    int            td_len = 0;
    if (!is_advanced) {
        if (ga_avail < GA_CONV_HDR) return {};
        td_len = (int)be_u16(ga + 2);
        td     = ga + GA_CONV_HDR;
    } else {
        if (ga_avail < GA_ADV_HDR) return {};
        td_len = (int)be_u32(ga + 4);
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
        int16_t  n_items   = be_i16(td + 0);
        uint8_t  opt_len   = td[3];
        uint32_t sel_flags = be_u32(td + 4);

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
        uint32_t n_items   = be_u32(td +  0);
        uint32_t opt_len   = be_u32(td +  8);
        uint32_t flags_lo  = be_u32(td + 12);
        uint32_t flags_hi  = be_u32(td + 16);

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

/*
    "DDFCLRequest", 1
    "DDFCLReply",   2 
    "Request",      1 
    "Reply",        2 
    "DFData",       2 
    "Event",        2
    "DFSelect",     1 
*/
// impl_parse_xml_ddf550 holds the real logic; parse_xml_ddf550 (below)
// wraps it in try/catch so no C++ exception escapes toward parse_message's
// extern "C" boundary (sdfc_abi.h rule 2) -- same rationale and idiom as
// ca120_parser.cpp's impl_parse_xml/parse_xml split.
static std::string impl_parse_xml_ddf550(const uint8_t* frame, int frame_len,
                                          int frame_type)
{
    pugi::xml_document doc;
    pugi::xml_parse_result presult =
        doc.load_buffer(frame, static_cast<size_t>(frame_len));
    // See ca120_parser.cpp's parse_xml for why this new failure path is an
    // accepted, intentional behavior change (design spec §3).
    if (!presult) return {};

    pugi::xml_node root = doc.first_child();
    const char* root_name = root.name();

    bool is_ddfcl_req = (std::strcmp(root_name, "DDFCLRequest") == 0);
    bool is_ddfcl_rep = (std::strcmp(root_name, "DDFCLReply")   == 0);
    bool is_dfdata    = (std::strcmp(root_name, "DFData")       == 0);
    bool is_event     = (std::strcmp(root_name, "Event")        == 0);
    bool is_dfselect  = (std::strcmp(root_name, "DFSelect")     == 0);

    const char* channel  = is_dfdata                                     ? "preclassifier_output"
                         : (is_ddfcl_req || is_ddfcl_rep || is_dfselect) ? "preclassifier"
                                                                          : "control";
    const char* msg_kind = (frame_type == 1) ? "request"
                         : is_dfdata         ? "dfdata"
                         : is_event          ? "event"
                                             : "reply";

    JsonWriter j;
    j.key_str("hw",       "ddf550");
    j.key_str("channel",  channel);
    j.key_str("msg_kind", msg_kind);

    const char* id_attr   = root.attribute("id").value();
    const char* type_attr = root.attribute("type").value();
    if (*id_attr)   j.key_str("msg_id",   id_attr);
    if (*type_attr) j.key_str("msg_type", type_attr);

    // Command name -- first <Command> anywhere in the document.
    pugi::xml_node cmd = find_first(root, "Command");
    if (cmd) {
        const char* cmd_name = cmd.attribute("name").value();
        if (*cmd_name) j.key_str("command_name", cmd_name);

        // Direct-text Command body (e.g. AnalysisIntervalMs's "50000") --
        // only meaningful when Command has no element children (otherwise
        // this would just be inter-tag whitespace around <Param> children).
        bool has_element_child = false;
        for (pugi::xml_node c : cmd.children())
            if (c.type() == pugi::node_element) { has_element_child = true; break; }
        if (!has_element_child) {
            std::string val = cmd.text().get();
            size_t a = val.find_first_not_of(" \t\r\n");
            if (a != std::string::npos) {
                size_t b = val.find_last_not_of(" \t\r\n");
                j.key_str("command_value", val.substr(a, b - a + 1));
            }
        }
    }

    // Generic param capture -- every <Param name="X">Y</Param> anywhere in
    // the document, typed by the ICD's Hungarian-notation prefix.
    {
        JsonWriter params;
        for (pugi::xpath_node xn : root.select_nodes(".//Param")) {
            pugi::xml_node p = xn.node();
            std::string name  = p.attribute("name").value();
            std::string value = p.text().get();
            write_typed_param(params, name, value);
        }
        j.key_raw("params", params.str());
    }

    // DFData preclassifier output fields -- every direct child of the
    // root, generic over field name. Only iterate element nodes, skipping
    // whitespace and text nodes between elements.
    if (is_dfdata) {
        const char* cl_id = root.attribute("DDF-CL-ID").value();
        if (*cl_id) j.key_str("ddf_cl_id", cl_id);

        JsonWriter fields, units;
        bool has_units = false;
        for (pugi::xml_node field : root.children()) {
            if (field.type() != pugi::node_element) continue;
            const char* tag = field.name();
            fields.key_str(tag, field.text().get());
            const char* unit = field.attribute("Unit").value();
            if (*unit) { units.key_str(tag, unit); has_units = true; }
        }
        j.key_raw("fields", fields.str());
        if (has_units) j.key_raw("units", units.str());
    }

    return j.str();
}

static std::string parse_xml_ddf550(const uint8_t* frame, int frame_len, int frame_type) {
    try {
        return impl_parse_xml_ddf550(frame, frame_len, frame_type);
    } catch (...) {
        return {};
    }
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
    if (be_u32(buf) == EB200_MAGIC) {
        if (ibuf < EB200_HDR_BYTES) return -1;
        uint32_t data_size = be_u32(buf + 12);
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

    uint32_t xml_n = be_u32(buf + 4);
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
    if (be_u32(frame) == EB200_MAGIC) {
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
    be_store_u32(p, XML_MAGIC_START);   p += 4;
    be_store_u32(p, (uint32_t)xml_total); p += 4;
    memcpy(p, xml_hdr,              (size_t)xml_hdr_len);   p += xml_hdr_len;
    memcpy(p, cmd_open,             (size_t)cmd_open_len);  p += cmd_open_len;
    memcpy(p, xml_body.c_str(),     (size_t)body_len);      p += body_len;
    memcpy(p, cmd_close,            (size_t)cmd_close_len); p += cmd_close_len;
    be_store_u32(p, XML_MAGIC_END);

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
