// drs-bridge/parsers/ca120/src/CA120_parser_new.cpp
//
// VALIDATION CANDIDATE — NOT the production parser. ca120_parser.cpp remains
// the real DLL implementation; this file is a side-by-side test bed proving
// the new drs-bridge/parsers/utils/xml_to_json/ module (see its README.md)
// works correctly behind the real sdfc_abi.h ABI contract, before any
// decision is made to actually replace ca120_parser.cpp's XML path with it.
//
// Scope: XML channel (TCP 9001) ONLY. extract_frame/parse_message below
// mirror ca120_parser.cpp's XML-path logic byte-for-byte (root tag
// detection, xml_closing_end framing) but call into xml_to_json for the
// actual decode instead of the old per-field xml_attr/xml_text/xml_has
// scanning. The AMMOS binary channel (TCP 9200-9400) is intentionally NOT
// implemented here -- out of scope for validating the XML rework.
//
// format_response (JSON -> XML encode direction) is a stub for now: the
// encode-side design (schema-driven per-message-kind templates vs. a
// generic reverse-mirror) hasn't been decided yet. See the "how do we
// convert JSON to XML" discussion this session -- once that's designed,
// its own implementation replaces this stub.
//
// Note (added during the pugixml migration of the real ca120_parser.cpp):
// this file's approach and the real parser's are NOT the same thing. This
// file emits the full generic XML->JSON mirror shape ({hw, channel,
// msg_kind, body: {...}}) via xml_to_json/. The real ca120_parser.cpp uses
// pugixml too, but only to rebuild its existing curated field set
// (msg_type, subsystems, frequency_hz, raw_xml, etc.) -- same JSON
// contract as before, different parsing engine underneath. This file
// remains an unadopted validation candidate; see
// docs/ewtss/specs/xml-parsing-pugixml-migration-design.md §7.
//
// Build (from repo root; single line, shown wrapped for readability):
//   g++ -std=c++17 -Wall -Wextra
//       -I drs-bridge/parsers/dp_ecm/include
//       -I drs-bridge/parsers/utils/xml_to_json
//       -o drs-bridge/parsers/ca120/src/CA120_parser_new.exe
//       drs-bridge/parsers/ca120/src/CA120_parser_new.cpp
//       drs-bridge/parsers/utils/xml_to_json/xml_parser.cpp
//       drs-bridge/parsers/utils/xml_to_json/json_mirror.cpp
//       drs-bridge/parsers/utils/xml_to_json/xml_to_json.cpp
//       drs-bridge/parsers/ca120/tests/test_CA120_parser_new.cpp
//   ./drs-bridge/parsers/ca120/src/CA120_parser_new.exe

#include "sdfc_abi.h"

#include "../../utils/xml_to_json/xml_to_json.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>

namespace {

// ---------------------------------------------------------------------------
// Helpers: minimal XML scanning (identical to ca120_parser.cpp's, "no LGPL,
// no exceptions" -- see that file's own comment). Frame *boundary* detection
// is a separate concern from frame *content* parsing, so this stays
// hand-rolled string scanning even though xml_to_json now owns the content
// side.
// ---------------------------------------------------------------------------

// Find the byte offset PAST the first occurrence of "</tag>" in xml[0..len).
// Returns -1 if not found (i.e. the frame isn't complete yet).
int xml_closing_end(const uint8_t* xml, int len, const char* tag) {
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

// Root-tag table shared by extract_frame (frame typing) and parse_message
// (msg_kind hint) -- ca120_parser.cpp duplicates this inline in both
// functions; kept as one table here since both call sites need the exact
// same mapping.
struct RootTag { const char* tag; const char* kind_hint; };
constexpr RootTag kRootTags[] = {
    { "Request", "request" },
    { "Reply",   "reply"   },
    { "Event",   "reply"   },  // hint is irrelevant for Event: xml_to_json
                               // detects the <Event> root itself and always
                               // reports msg_kind "event" regardless of hint.
};

// Matches xml[0] == '<' followed immediately by one of kRootTags's names and
// a valid tag-name terminator (space, '>', or newline -- so "RequestFoo"
// does not falsely match "Request"). Returns nullptr if none match.
const RootTag* detect_root_tag(const char* xml, int len) {
    for (const auto& r : kRootTags) {
        int tlen = (int)strlen(r.tag);
        if (len > tlen + 1 && memcmp(xml + 1, r.tag, (size_t)tlen) == 0) {
            char nx = xml[1 + tlen];
            if (nx == ' ' || nx == '>' || nx == '\r' || nx == '\n') return &r;
        }
    }
    return nullptr;
}

}  // namespace

extern "C" {

// -----------------------------------------------------------------------------
// extract_frame -- XML channel only (see file header for scope note).
// -----------------------------------------------------------------------------
SDFC_EXPORT int extract_frame(const uint8_t* buf,
                               size_t         buf_len,
                               uint8_t**      out_frame,
                               size_t*        out_len)
{
    if (!buf || buf_len < 4 || !out_frame || !out_len) return -1;

    int ibuf = static_cast<int>(buf_len);

    int start = 0;
    while (start < ibuf && (buf[start] == ' '  || buf[start] == '\t' ||
                             buf[start] == '\r' || buf[start] == '\n'))
        ++start;

    if (start >= ibuf || buf[start] != '<') return -1;

    const char* xml = reinterpret_cast<const char*>(buf + start);
    int xml_avail    = ibuf - start;

    const RootTag* root = detect_root_tag(xml, xml_avail);
    if (!root) return -1;

    int end = xml_closing_end(buf + start, xml_avail, root->tag);
    if (end < 0) return -1;  // closing tag not yet received -- caller reads more

    // `end` is already the frame length measured from `buf + start` (where
    // the XML actually begins) -- NOT from the original `buf`. The real
    // ca120_parser.cpp computes `total = start + end` and then copies
    // `total` bytes starting at `buf + start`, which double-counts `start`
    // and over-reads `start` bytes past the frame's true end whenever there
    // is leading whitespace before the XML. Fixed here: copy exactly `end`
    // bytes from `buf + start`.
    int total = end;
    auto* p = static_cast<uint8_t*>(std::malloc((size_t)total));
    if (!p) return -1;
    memcpy(p, buf + start, (size_t)total);
    *out_frame = p;
    *out_len   = (size_t)total;
    return 0;
}

// -----------------------------------------------------------------------------
// parse_message -- decodes via xml_to_json instead of the old per-field
// xml_attr/xml_text/xml_has scanning.
//
// Two distinct failure modes, matching the design spec's "malformed XML ->
// emit an error JSON instead of failing the call" principle:
//   - frame doesn't even start with a recognized CA120 XML root tag
//     (Request/Reply/Event) -> this isn't CA120 XML at all -> return -1,
//     same as the real ca120_parser.cpp's "result.empty() -> return -1".
//   - frame DOES start with a recognized root tag, but is malformed *inside*
//     (mismatched closing tag, etc.) -> still return 0; the JSON payload
//     itself carries msg_kind:"malformed" + parse_error/parse_offset.
// -----------------------------------------------------------------------------
SDFC_EXPORT int parse_message(const uint8_t* frame, size_t frame_len,
                               char** out_json, size_t* out_len)
{
    if (!frame || frame_len < 4 || !out_json || !out_len) return -1;

    int iframe = static_cast<int>(frame_len);
    const char* xml = reinterpret_cast<const char*>(frame);

    const RootTag* root = detect_root_tag(xml, iframe);
    if (!root) return -1;

    std::string result = parse_xml_to_json(xml, frame_len, root->kind_hint, "ca120");
    if (result.empty()) return -1;  // defensive; xml_to_json never actually returns empty

    char* out = static_cast<char*>(std::malloc(result.size() + 1u));
    if (!out) return -1;
    memcpy(out, result.data(), result.size() + 1u);
    *out_json = out;
    *out_len  = result.size();
    return 0;
}

// -----------------------------------------------------------------------------
// format_response -- STUB. Encode direction (JSON -> XML) is not yet
// designed (see file header). Always reports "not implemented".
// -----------------------------------------------------------------------------
SDFC_EXPORT int format_response(const char* /*kind*/, const char* /*kwargs_json*/,
                                 uint8_t** /*out_buf*/, size_t* /*out_len*/)
{
    return -1;
}

// -----------------------------------------------------------------------------
// free_result
// -----------------------------------------------------------------------------
SDFC_EXPORT void free_result(void* ptr) {
    std::free(ptr);
}

}  // extern "C"
