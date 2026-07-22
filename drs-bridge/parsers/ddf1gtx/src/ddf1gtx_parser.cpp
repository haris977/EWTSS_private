// drs-bridge/parsers/ddf1gtx/src/ddf1gtx_parser.cpp
//
// R&S DDF-1GTX Direction Finding Receiver parser DLL.
// ICD: ICD-DDF1GTX_22_08_25  (§-references below are to that document)
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
// Protocol notes (§3, §5.1.7):
//   XML wrapper: 4B MagicStart + 4B Length(BE) + N bytes XML + 4B MagicEnd
//   The ICD does not specify magic word values; XML_MAGIC_START/END below are
//   placeholders. extract_frame uses a content-based heuristic (plausible
//   length field + first XML byte) so it works regardless of actual magic values.
//   EB200 header: magic 0x000EB200, VersionMinor/Major, SeqNumLow/High, DataSize.
//   Trace tags >= 5000 use the advanced GenericAttribute/TraceAttribute layout.

#include "sdfc_abi.h"
#include "sdfc_endian.h"
#include "json_writer.h"
#include "json_kwargs.h"
#include "eb200_shared.h"
#include "pugixml.hpp"
#include "pugixml_generic_mirror.h"
#include "pugixml_generic_unmirror.h"
#include "dfjob_dfdata_unmirror.h"
#include "json.hpp"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>
#include <algorithm>
#include <vector>
#include <utility>

using namespace sdfc;

// ---------------------------------------------------------------------------
// EB200 protocol constants, enum-to-string tables, decode_audio_opt_header,
// and parse_eb200 now live in drs-bridge/parsers/utils/eb200_shared.h --
// identical between DDF-550 and DDF-1GTX (see that header's comment).
// XML_MAGIC_START/END stay local: each ICD's wrapper magic word is an
// independently-tracked open question (D1 for DDF-550, a separate one for
// DDF-1GTX) that happens to share a placeholder value today but is not
// guaranteed to resolve to the same real value.
// ---------------------------------------------------------------------------

static constexpr uint32_t XML_MAGIC_START = 0x00000000u;
static constexpr uint32_t XML_MAGIC_END   = 0x00000000u;

// ---------------------------------------------------------------------------
// Helpers: XML scanning  (no LGPL)
// ---------------------------------------------------------------------------

// xml_closing_end / json_str_field / json_int_field now live in
// drs-bridge/parsers/utils/json_kwargs.h (shared across the whole SDFC
// family so escape handling stays identical everywhere).
//
// Content parsing uses pugixml (find_first from pugixml_helpers.h;
// param_value/tag_or_param below are specific to this file's Param-lookup
// pattern) -- content parsing only; xml_closing_end is frame-boundary
// detection and still byte-scanning, see extract_frame.

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
        { "Event",        2 },   // async status/alarm/scan-complete: DDF -> SDFC
        { "DFSelect",     1 },   // preclassifier filter command: SDFC -> DDF
        { "FormatSelect", 1 },   // preclassifier output-format command: SDFC -> DDF (DDFSystemControlInterfacePreClassifier.pdf §6.2.1)
        { "DFJob",        2 },   // preclassifier job definition: DDF -> SDFC, pushed on connect / job change (ibid. §6.2.2)
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

// impl_parse_xml_ddf1gtx holds the real logic; parse_xml_ddf1gtx (below)
// wraps it in try/catch so no C++ exception escapes toward parse_message's
// extern "C" boundary (sdfc_abi.h rule 2) -- same rationale and idiom as
// ca120_parser.cpp's impl_parse_xml/parse_xml split.
//
// channel/msg_kind are DDF-1GTX-specific (root-tag vocabulary/channel
// taxonomy differ per ICD), derived from `root_tag` -- the same string
// classify_xml_root() already found by byte-scanning in parse_message,
// before we know whether pugixml will succeed, so channel/msg_kind stay
// correct even on the malformed path. The tree->JSON mirroring itself (the
// part that must be byte-for-byte identical across CA120/DDF-550/DDF-1GTX)
// is shared via pugixml_generic_mirror.h.
static std::string impl_parse_xml_ddf1gtx(const uint8_t* frame, int frame_len,
                                           int frame_type, const char* root_tag)
{
    bool is_ddfcl_req    = (std::strcmp(root_tag, "DDFCLRequest") == 0);
    bool is_ddfcl_rep    = (std::strcmp(root_tag, "DDFCLReply")   == 0);
    bool is_dfdata       = (std::strcmp(root_tag, "DFData")       == 0);
    bool is_event        = (std::strcmp(root_tag, "Event")        == 0);
    bool is_dfselect     = (std::strcmp(root_tag, "DFSelect")     == 0);
    bool is_formatselect = (std::strcmp(root_tag, "FormatSelect") == 0);
    bool is_dfjob        = (std::strcmp(root_tag, "DFJob")        == 0);

    // DFJob groups with DFData ("preclassifier_output": DDF -> SDFC data on
    // 9154). FormatSelect groups with DFSelect ("preclassifier": SDFC -> DDF
    // control on 9154) -- matches the existing DFSelect precedent, even
    // though "preclassifier" is shared with 9153 traffic too.
    const char* channel  = (is_dfdata || is_dfjob)                                          ? "preclassifier_output"
                         : (is_ddfcl_req || is_ddfcl_rep || is_dfselect || is_formatselect) ? "preclassifier"
                                                                                              : "control";
    const char* msg_kind = (frame_type == 1) ? "request"
                         : is_dfdata         ? "dfdata"
                         : is_dfjob          ? "dfjob"
                         : is_event          ? "event"
                                             : "reply";

    pugi::xml_document doc;
    pugi::xml_parse_result presult =
        doc.load_buffer(frame, static_cast<size_t>(frame_len));
    // See ca120_parser.cpp's parse_xml for why this new failure path is an
    // accepted, intentional behavior change (design spec §3).
    //
    // Also: pugixml's default parse flags decode XML entities (&amp; -> &),
    // normalize EOL, and normalize attribute whitespace -- the old hand-rolled
    // scanning never did any of this (returned raw bytes verbatim). Accepted,
    // disclosed deviation from strict byte-identical output -- see design
    // spec §3.2. Low practical impact: no real ICD sample uses entities.
    if (!presult) return build_malformed_envelope("ddf1gtx", channel, presult);

    return build_mirror_envelope("ddf1gtx", channel, msg_kind, doc.first_child());
}

static std::string parse_xml_ddf1gtx(const uint8_t* frame, int frame_len, int frame_type,
                                      const char* root_tag) {
    try {
        return impl_parse_xml_ddf1gtx(frame, frame_len, frame_type, root_tag);
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
// the raw XML bytes only (consistent with the raw-XML path).
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
        result = parse_eb200(frame, iframe, "ddf1gtx");
    } else {
        const char* root_tag = nullptr;
        int ftype = classify_xml_root(frame, iframe, &root_tag);
        if (root_tag && ftype > 0)
            result = parse_xml_ddf1gtx(frame, iframe, ftype, root_tag);
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
// Encodes a JSON descriptor into a DDF-1GTX wire frame. Two independent
// shapes, selected by "msg_kind":
//
// 1. "request" | "reply" -- control (9150) or preclassifier-control (9153)
//    idiom. The "command" field takes exactly the JSON shape
//    parse_message() itself produces at body.<request|reply>.command (see
//    docs/ewtss/specs/rdfs-generic-mirror-json-contract.md) -- a caller (or
//    a simulated/random-mode generator standing in for DRS's hardware
//    role) can hand back that sub-object verbatim instead of hand-building
//    an XML string. See pugixml_generic_unmirror.h for exactly which
//    command shapes are in scope (Command+Param(s)/Struct/Array, nested to
//    any depth).
//
//    Required JSON fields:
//      "msg_kind"  : "request" | "reply"   -- selects Request/DDFCLRequest
//                    vs Reply/DDFCLReply (DRS's own direction: "reply" is
//                    what DRS sends acting as the hardware answering a
//                    command)
//      "msg_type"  : "get" | "set"          -- the wire "type" attribute
//      "id"        : integer request/reply correlation ID
//      "command"   : {"name": "...", "param"/"struct"/"array": <...>}
//    Optional:
//      "channel"   : "preclassifier"  → DDFCLRequest/DDFCLReply root tag
//                    (any other value) → Request/Reply root tag
//    Output: [magic_start(4 BE)][length(4 BE)][XML bytes][magic_end(4 BE)]
//    Magic word values are XML_MAGIC_START / XML_MAGIC_END (placeholders —
//    verify from live capture before using on real hardware).
//
// 2. "dfjob" | "dfdata" -- preclassifier output (9154) idiom. Raw,
//    unwrapped XML, no magic-word envelope (see dfjob_dfdata_unmirror.h).
//    Required JSON fields:
//      "msg_kind"  : "dfjob" | "dfdata"
//      "df_job"    : (msg_kind "dfjob") the mirrored "df_job" body value
//      "df_data"   : (msg_kind "dfdata") the mirrored "df_data" body value
//    Output: raw XML bytes, verbatim, no envelope.
//
// Returns 0 on success (byte count is communicated via *out_len, per the
// sdfc_abi.h 0/-1 convention), or -1 on encoding error (missing/invalid
// fields, or a shape build_command_xml()/build_dfjob_xml()/
// build_dfdata_xml() doesn't support).

SDFC_EXPORT int format_response(const char* /*kind*/, const char* kwargs_json,
                                 uint8_t** out_buf, size_t* out_len)
{
    if (!kwargs_json || !out_buf || !out_len) return -1;

    std::string cmd_xml;
    std::string msg_kind, msg_type, channel;
    long long id = -1;
    try {
        nlohmann::json kwargs = nlohmann::json::parse(kwargs_json);
        if (!kwargs.is_object()) return -1;

        msg_kind = kwargs.value("msg_kind", "");

        // DFJob/DFData (preclassifier output, TCP 9154): raw, unwrapped XML
        // text, no id/type/channel wrapper at all -- a completely different
        // shape from Request/Reply/DDFCLRequest/DDFCLReply below. See
        // dfjob_dfdata_unmirror.h for the tag-lookup rationale.
        if (msg_kind == "dfjob" || msg_kind == "dfdata") {
            const char* key = (msg_kind == "dfjob") ? "df_job" : "df_data";
            if (!kwargs.contains(key)) return -1;
            std::string xml = (msg_kind == "dfjob")
                ? build_dfjob_xml(kwargs.at(key))
                : build_dfdata_xml(kwargs.at(key));

            auto* buf = static_cast<uint8_t*>(std::malloc(xml.size()));
            if (!buf) return -1;
            memcpy(buf, xml.data(), xml.size());
            *out_buf = buf;
            *out_len = xml.size();
            return 0;
        }

        msg_type = kwargs.value("msg_type", "");
        channel  = kwargs.value("channel", "");
        if (!kwargs.contains("id") || !kwargs.at("id").is_number_integer()) return -1;
        id = kwargs.at("id").get<long long>();

        if ((msg_kind != "request" && msg_kind != "reply") || msg_type.empty() || id < 0)
            return -1;
        if (!kwargs.contains("command")) return -1;

        cmd_xml = build_command_xml(kwargs.at("command"));
    } catch (...) {
        return -1;
    }

    bool use_ddfcl = (channel == "preclassifier");
    bool is_reply  = (msg_kind == "reply");

    // Build the outer XML (matches ICD attribute order per real examples:
    // DDFCLRequest/DDFCLReply are id-then-type, Request/Reply are type-then-id)
    char xml_hdr[512];
    int xml_hdr_len;
    if (use_ddfcl) {
        xml_hdr_len = std::snprintf(xml_hdr, sizeof(xml_hdr),
            "<%s id=\"%lld\" type=\"%s\">",
            is_reply ? "DDFCLReply" : "DDFCLRequest", id, msg_type.c_str());
    } else {
        xml_hdr_len = std::snprintf(xml_hdr, sizeof(xml_hdr),
            "<%s type=\"%s\" id=\"%lld\">",
            is_reply ? "Reply" : "Request", msg_type.c_str(), id);
    }
    if (xml_hdr_len <= 0 || xml_hdr_len >= (int)sizeof(xml_hdr)) return -1;

    char xml_close[64];
    int xml_close_len = std::snprintf(xml_close, sizeof(xml_close), "</%s>",
        use_ddfcl ? (is_reply ? "DDFCLReply" : "DDFCLRequest")
                  : (is_reply ? "Reply" : "Request"));
    if (xml_close_len <= 0 || xml_close_len >= (int)sizeof(xml_close)) return -1;

    int body_len = (int)cmd_xml.size();
    int xml_total = xml_hdr_len + body_len + xml_close_len;

    // Binary envelope: 4 magic_start + 4 length + xml_total + 4 magic_end = xml_total + 12
    int frame_total = xml_total + 12;
    if (frame_total > MAX_FRAME_BUFFER_BYTES) return -1;

    auto* buf = static_cast<uint8_t*>(std::malloc((size_t)frame_total));
    if (!buf) return -1;
    uint8_t* p = buf;
    store_u32be(p, XML_MAGIC_START);   p += 4;
    store_u32be(p, (uint32_t)xml_total); p += 4;
    memcpy(p, xml_hdr,   (size_t)xml_hdr_len); p += xml_hdr_len;
    memcpy(p, cmd_xml.c_str(), (size_t)body_len); p += body_len;
    memcpy(p, xml_close, (size_t)xml_close_len); p += xml_close_len;
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
