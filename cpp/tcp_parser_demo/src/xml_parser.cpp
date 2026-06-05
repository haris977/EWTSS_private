#include "xml_parser.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

// ── Minimal XML helpers ───────────────────────────────────────────────────────
// These cover the subset of XML the hardware actually sends: a single root
// element with attributes, and child elements (with attributes or text content).
// They deliberately do NOT handle CDATA, namespaces, or entities — you do not
// need a full XML parser for a fixed hardware protocol.

// Find the value of `attr_name="..."` within an XML string.
// Returns true and fills out[0..cap-1] (null-terminated) on success.
// Stops searching at the first '>' it finds, so it stays within the
// current opening tag and does not bleed into child element content.
static bool attr_val(const char* xml, const char* attr_name, char* out, size_t cap) {
    // Build the search needle: attr_name="
    char needle[128];
    snprintf(needle, sizeof(needle), "%s=\"", attr_name);

    const char* p = strstr(xml, needle);
    if (!p) return false;

    // Make sure the match is inside the opening tag (before the first '>').
    const char* tag_close = strchr(xml, '>');
    if (tag_close && p > tag_close) return false;

    p += strlen(needle);
    size_t i = 0;
    while (*p && *p != '"' && i < cap - 1)
        out[i++] = *p++;
    out[i] = '\0';
    return (*p == '"');
}

// Find text content between <tag>TEXT</tag>.
// Returns true and fills out on success.
static bool elem_text(const char* xml, const char* tag, char* out, size_t cap) {
    char open_tag[128], close_tag[128];
    snprintf(open_tag,  sizeof(open_tag),  "<%s>",  tag);
    snprintf(close_tag, sizeof(close_tag), "</%s>", tag);

    const char* start = strstr(xml, open_tag);
    if (!start) return false;
    start += strlen(open_tag);

    const char* end = strstr(start, close_tag);
    if (!end) return false;

    size_t len = (size_t)(end - start);
    if (len >= cap) len = cap - 1;
    memcpy(out, start, len);
    out[len] = '\0';
    return true;
}

// Advance past the current self-closing <Hopper ... /> element.
// Returns a pointer to the NEXT '<Hopper' in the string, or nullptr if none.
static const char* next_hopper(const char* from) {
    // A self-closing element ends with '/>'; a non-self-closing one ends with '</Hopper>'.
    const char* end = strstr(from, "/>");
    if (!end) end = strstr(from, "</Hopper>");
    if (!end) return nullptr;
    return strstr(end + 2, "<Hopper");
}

// ── Public: XML → JSON ────────────────────────────────────────────────────────
int parse_xml_to_json(const char* xml, char* out, size_t cap) {
    if (!xml || !out || cap < 64) return -1;

    // ── Step 1: extract root element attributes ───────────────────────────────
    // Find the start of <DrsMessage so that attr_val restricts its search to
    // the root element's attribute list (not child elements).
    const char* root = strstr(xml, "<DrsMessage");
    if (!root) return -1;

    char gid_s[16]={}, uid_s[16]={}, st_s[16]={}, ts_s[32]={};
    attr_val(root, "GroupId",   gid_s, sizeof(gid_s));
    attr_val(root, "UnitId",    uid_s, sizeof(uid_s));
    attr_val(root, "Status",    st_s,  sizeof(st_s));
    attr_val(root, "Timestamp", ts_s,  sizeof(ts_s));

    int       group_id = atoi(gid_s);
    int       unit_id  = atoi(uid_s);
    int       status   = atoi(st_s);
    long long ts       = atoll(ts_s);

    // ── Step 2: write the common JSON header ─────────────────────────────────
    size_t pos = 0;
    int n = snprintf(out + pos, cap - pos,
        "{\"frame_type\":\"xml\",\"group_id\":%d,\"unit_id\":%d,"
        "\"status\":%d,\"timestamp\":%lld",
        group_id, unit_id, status, ts);
    if (n <= 0) return -1; pos += (size_t)n;

    // ── Step 3: decode type-specific payload ──────────────────────────────────
    // Mirror the same group_id / unit_id dispatch used in binary_parser.cpp
    // so the two parsers produce structurally identical JSON for the same message.

    if (group_id == 101 && unit_id == 40) {
        // FH Detection — hopper list
        char count_s[16] = {};
        elem_text(xml, "HopperCount", count_s, sizeof(count_s));
        n = snprintf(out + pos, cap - pos,
                     ",\"hopper_count\":%s,\"hoppers\":[", count_s);
        if (n > 0) pos += (size_t)n;

        const char* h = strstr(xml, "<Hopper");
        bool first = true;
        while (h) {
            char ch[16]={}, freq[32]={}, power[16]={}, dwell[16]={};
            attr_val(h, "Channel",     ch,    sizeof(ch));
            attr_val(h, "FrequencyHz", freq,  sizeof(freq));
            attr_val(h, "PowerDbm",    power, sizeof(power));
            attr_val(h, "DwellUs",     dwell, sizeof(dwell));

            // Write as numbers (not quoted strings) so the JSON schema matches
            // the binary parser's output — consumers can't tell which parser ran.
            n = snprintf(out + pos, cap - pos,
                "%s{\"channel\":%s,\"frequency_hz\":%s,"
                "\"power_dbm\":%s,\"dwell_us\":%s}",
                first ? "" : ",", ch, freq, power, dwell);
            if (n > 0) pos += (size_t)n;
            first = false;

            h = next_hopper(h);
        }
        n = snprintf(out + pos, cap - pos, "]");
        if (n > 0) pos += (size_t)n;

    } else if (group_id == 101 && unit_id == 84) {
        // Burst Detection
        char count_s[16] = {};
        elem_text(xml, "BurstCount", count_s, sizeof(count_s));
        n = snprintf(out + pos, cap - pos,
                     ",\"burst_count\":%s,\"bursts\":[", count_s);
        if (n > 0) pos += (size_t)n;

        const char* b = strstr(xml, "<Burst");
        bool first = true;
        while (b) {
            char start_us[32]={}, dur[16]={}, freq[32]={}, power[16]={}, bw[16]={};
            attr_val(b, "StartUs",     start_us, sizeof(start_us));
            attr_val(b, "DurationUs",  dur,      sizeof(dur));
            attr_val(b, "FrequencyHz", freq,     sizeof(freq));
            attr_val(b, "PowerDbm",    power,    sizeof(power));
            attr_val(b, "BandwidthHz", bw,       sizeof(bw));

            n = snprintf(out + pos, cap - pos,
                "%s{\"start_us\":%s,\"duration_us\":%s,"
                "\"frequency_hz\":%s,\"power_dbm\":%s,\"bandwidth_hz\":%s}",
                first ? "" : ",", start_us, dur, freq, power, bw);
            if (n > 0) pos += (size_t)n;
            first = false;

            const char* end = strstr(b, "/>");
            if (!end) end = strstr(b, "</Burst>");
            if (!end) break;
            b = strstr(end + 2, "<Burst");
        }
        n = snprintf(out + pos, cap - pos, "]");
        if (n > 0) pos += (size_t)n;

    } else if (group_id == 100 && unit_id == 2) {
        // System Version
        char fw[16]={}, drv[16]={}, fpga[16]={}, bsp[16]={}, proc[16]={};
        elem_text(xml, "FwVersion",     fw,   sizeof(fw));
        elem_text(xml, "DriverVersion", drv,  sizeof(drv));
        elem_text(xml, "FpgaVersion",   fpga, sizeof(fpga));
        elem_text(xml, "BspVersion",    bsp,  sizeof(bsp));
        elem_text(xml, "ProcessorId",   proc, sizeof(proc));

        n = snprintf(out + pos, cap - pos,
            ",\"fw_version\":\"%s\",\"driver_version\":\"%s\","
            "\"fpga_version\":\"%s\",\"bsp_version\":\"%s\","
            "\"processor_id\":%s",
            fw, drv, fpga, bsp, proc);
        if (n > 0) pos += (size_t)n;
    }
    // Unknown group/unit: the base fields (group_id, unit_id, status, timestamp)
    // are already written — enough for the Kafka consumer to log and investigate.

    n = snprintf(out + pos, cap - pos, "}");
    if (n > 0) pos += (size_t)n;
    if (pos < cap) out[pos] = '\0';
    return (int)pos;
}
