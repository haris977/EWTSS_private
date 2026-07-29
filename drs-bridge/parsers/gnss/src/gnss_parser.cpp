// drs-bridge/parsers/gnss/src/gnss_parser.cpp
//
// Rugged GNSS Receiver (V3 Novus, SAFESYNC-V3TS1U-R-NAVIC&IRNSS L5+S BAND)
// NMEA-0183 parser DLL.
// Source: "RUGGED GNSS RECEIVER" IDD, V3 Novus, v1.0. drs-bridge acts as
// the CLIENT reading this device's live NMEA stream (the receiver has no
// host-to-device command protocol at all -- receive-only).
// Design doc: docs/ewtss/specs/gnss-parser-design.md
//
// Scope for this pass: extract_frame + parse_message (decode) only.
// format_response is stubbed (returns -1) -- deliberately deferred to a
// follow-up pass, not decided as a permanent no-op.
//
// Not to be confused with drs-bridge/parsers/rsec/rsec_parser.cpp's own
// "frame_variant 4" GNSS NMEA decoder -- that's a different physical GNSS
// unit (embedded in the Himashakti/BEL system RSEC tests), handling a
// thinner sentence set arriving multiplexed on RSEC's shared link. See
// design doc §1 for why these are separate parsers.
//
// Sentence field layouts are the FULL standard NMEA-0183 layout, verified
// against this ICD's own raw sample captures (not just the document's
// abbreviated Structure tables -- see design doc §3).
#include "sdfc_abi.h"
#include "json_writer.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using sdfc::JsonWriter;

// ============================================================
// Shared helpers
// ============================================================

// Splits the comma-delimited fields between [start, end) into strings.
// Empty fields (consecutive commas) are preserved as empty strings, since
// NMEA field position is meaningful (e.g. GSA's blank satellite slots).
static std::vector<std::string> split_fields(const char* start, const char* end) {
    std::vector<std::string> fields;
    const char* field_start = start;
    for (const char* p = start; p <= end; ++p) {
        if (p == end || *p == ',') {
            fields.emplace_back(field_start, p);
            field_start = p + 1;
        }
    }
    return fields;
}

// Returns fields[i] if present, else "" -- never throws/out-of-range.
static std::string field_at(const std::vector<std::string>& fields, size_t i) {
    return i < fields.size() ? fields[i] : std::string();
}

// Shared structure for $GPGSV/$GLGSV/$GAGSV/$GBGSV/$GIGSV (satellites in
// view for GPS/GLONASS/Galileo/BeiDou/IRNSS respectively -- identical
// field layout per the ICD, only the talker prefix differs).
// Fields: Total_Messages, Message_Number, Satellites_In_View,
// then 1-4 repeated groups of (PRN, Elevation, Azimuth, SNR),
// then a trailing Signal_ID (present in every sample capture, absent
// from the ICD's own Structure table -- see design doc §3).
static void decode_gsv(const std::vector<std::string>& f, JsonWriter& w) {
    w.key_str("total_messages",     field_at(f, 0));
    w.key_str("message_number",     field_at(f, 1));
    w.key_str("satellites_in_view", field_at(f, 2));

    // Remaining fields minus the trailing Signal_ID, divided into groups of 4.
    size_t remaining = f.size() > 3 ? f.size() - 3 : 0;
    size_t sat_field_count = remaining > 0 ? remaining - 1 : 0; // drop Signal_ID
    size_t group_count = sat_field_count / 4;

    std::vector<std::string> sats;
    for (size_t i = 0; i < group_count; ++i) {
        size_t base = 3 + i * 4;
        JsonWriter sw;
        sw.key_str("prn",          field_at(f, base + 0));
        sw.key_str("elevation_deg", field_at(f, base + 1));
        sw.key_str("azimuth_deg",  field_at(f, base + 2));
        sw.key_str("snr_db",       field_at(f, base + 3));
        sats.push_back(sw.str());
    }
    std::string arr = "[";
    for (size_t i = 0; i < sats.size(); ++i) { if (i) arr += ','; arr += sats[i]; }
    arr += ']';
    w.key_raw("satellites", arr);

    w.key_str("signal_id", field_at(f, f.size() > 0 ? f.size() - 1 : 0));
}

// $GNGLL: Latitude, N/S, Longitude, E/W, UTC Time, Status, FAA Mode.
// (The ICD's own table lists only Lat/Lng/Time/Status as one combined
// example each -- the raw sample confirms N/S, E/W, and a trailing FAA
// Mode field are each their own comma field; see design doc §3.)
static void decode_gngll(const std::vector<std::string>& f, JsonWriter& w) {
    w.key_str("latitude",    field_at(f, 0));
    w.key_str("ns",          field_at(f, 1));
    w.key_str("longitude",   field_at(f, 2));
    w.key_str("ew",          field_at(f, 3));
    w.key_str("utc_time",    field_at(f, 4));
    w.key_str("status",      field_at(f, 5));
    w.key_str("faa_mode",    field_at(f, 6));
}

// $GNZDA: UTC Time, Day, Month, Year, Local Zone Hours, Local Zone Minutes.
// (The ICD's table omits Local Zone Minutes -- the raw sample has both
// trailing fields; see design doc §3.)
static void decode_gnzda(const std::vector<std::string>& f, JsonWriter& w) {
    w.key_str("utc_time",           field_at(f, 0));
    w.key_str("day",                field_at(f, 1));
    w.key_str("month",              field_at(f, 2));
    w.key_str("year",               field_at(f, 3));
    w.key_str("local_zone_hours",   field_at(f, 4));
    w.key_str("local_zone_minutes", field_at(f, 5));
}

// $GNRMC: UTC Time, Status, Latitude, N/S, Longitude, E/W, Speed Over
// Ground, Course Over Ground, Date, Magnetic Variation, Mag Var
// Direction, FAA Mode, Nav Status. (The ICD's table lists only 7 of
// these 13 fields -- the raw sample confirms the full standard NMEA-0183
// RMC layout; see design doc §3.)
static void decode_gnrmc(const std::vector<std::string>& f, JsonWriter& w) {
    w.key_str("utc_time",           field_at(f, 0));
    w.key_str("status",             field_at(f, 1));
    w.key_str("latitude",           field_at(f, 2));
    w.key_str("ns",                 field_at(f, 3));
    w.key_str("longitude",          field_at(f, 4));
    w.key_str("ew",                 field_at(f, 5));
    w.key_str("speed_over_ground",  field_at(f, 6));
    w.key_str("course_over_ground", field_at(f, 7));
    w.key_str("date",               field_at(f, 8));
    w.key_str("magnetic_variation", field_at(f, 9));
    w.key_str("mag_var_direction",  field_at(f, 10));
    w.key_str("faa_mode",           field_at(f, 11));
    w.key_str("nav_status",         field_at(f, 12));
}

// $GNVTG: Course True, T, Course Magnetic, M, Speed(knots), N,
// Speed(km/h), K, FAA Mode. (The ICD's table omits Course Magnetic and
// FAA Mode -- the raw sample has both; see design doc §3.)
static void decode_gnvtg(const std::vector<std::string>& f, JsonWriter& w) {
    w.key_str("course_true",        field_at(f, 0));
    w.key_str("course_true_unit",   field_at(f, 1));
    w.key_str("course_magnetic",    field_at(f, 2));
    w.key_str("course_magnetic_unit", field_at(f, 3));
    w.key_str("speed_knots",        field_at(f, 4));
    w.key_str("speed_knots_unit",   field_at(f, 5));
    w.key_str("speed_kmh",          field_at(f, 6));
    w.key_str("speed_kmh_unit",     field_at(f, 7));
    w.key_str("faa_mode",           field_at(f, 8));
}

// $GNGGA: UTC Time, Latitude, N/S, Longitude, E/W, Fix Quality, Sats
// Used, HDOP, Altitude, Altitude Unit, Geoid Separation, Geoid Sep Unit,
// DGPS Age, DGPS Station ID. Full standard NMEA-0183 GGA layout (14
// fields) -- this one matches the ICD's own table once the raw sample's
// commas are recounted carefully; no discrepancy here (see design doc §3).
static void decode_gngga(const std::vector<std::string>& f, JsonWriter& w) {
    w.key_str("utc_time",              field_at(f, 0));
    w.key_str("latitude",              field_at(f, 1));
    w.key_str("ns",                    field_at(f, 2));
    w.key_str("longitude",             field_at(f, 3));
    w.key_str("ew",                    field_at(f, 4));
    w.key_str("fix_quality",           field_at(f, 5));
    w.key_str("satellites_used",       field_at(f, 6));
    w.key_str("hdop",                  field_at(f, 7));
    w.key_str("altitude",              field_at(f, 8));
    w.key_str("altitude_unit",         field_at(f, 9));
    w.key_str("geoid_separation",      field_at(f, 10));
    w.key_str("geoid_separation_unit", field_at(f, 11));
    w.key_str("dgps_age",              field_at(f, 12));
    w.key_str("dgps_station_id",       field_at(f, 13));
}

// $GNGSA: Fix Mode, Fix Type, 12 fixed Satellite ID slots (unused ones
// blank -- not a counted/variable-length list), PDOP, HDOP, VDOP, System
// ID. Full standard NMEA-0183 GSA layout (18 fields) -- also matches the
// ICD's table once recounted; the trailing System ID is the only field
// the table's prose omits (see design doc §3).
static void decode_gngsa(const std::vector<std::string>& f, JsonWriter& w) {
    w.key_str("fix_mode", field_at(f, 0));
    w.key_str("fix_type", field_at(f, 1));

    // Satellite ID fields are always plain digits or empty in this ICD's
    // samples (never quotes/backslashes), so manual JSON-string quoting
    // here is safe without needing JsonWriter's private escaping.
    std::vector<std::string> sat_ids;
    for (int i = 0; i < 12; ++i) {
        sat_ids.push_back(std::string("\"") + field_at(f, static_cast<size_t>(2 + i)) + "\"");
    }
    std::string arr = "[";
    for (size_t i = 0; i < sat_ids.size(); ++i) { if (i) arr += ','; arr += sat_ids[i]; }
    arr += ']';
    w.key_raw("satellite_ids", arr);

    w.key_str("pdop",      field_at(f, 14));
    w.key_str("hdop",      field_at(f, 15));
    w.key_str("vdop",      field_at(f, 16));
    w.key_str("system_id", field_at(f, 17));
}

// ============================================================
// ABI: extract_frame
//
// Line-based framing (no length table -- NMEA has no length field).
// The host discards exactly *out_len bytes from the front of its buffer
// after a successful call (see sdfc_abi.h), so any noise bytes before the
// first '$' are included IN the returned frame rather than silently
// dropped here -- dropping them here without adjusting *out_len would
// desync the host's buffer offset. parse_message is responsible for
// finding '$' inside the frame and ignoring anything before it.
// ============================================================
extern "C" SDFC_EXPORT int extract_frame(
    const uint8_t* buf, size_t buf_len,
    uint8_t** out_frame, size_t* out_len)
{
    if (!buf || buf_len == 0 || !out_frame || !out_len) return -1;

    const char* data = reinterpret_cast<const char*>(buf);

    // Find the first '$' -- if none yet, nothing to frame (wait for more data).
    size_t dollar = 0;
    bool found_dollar = false;
    for (size_t i = 0; i < buf_len; ++i) {
        if (data[i] == '$') { dollar = i; found_dollar = true; break; }
    }
    if (!found_dollar) return -1;

    // From '$', find the terminating '\n' (LF; '\r' before it is optional --
    // some transports strip it, so we don't require it).
    size_t lf = 0;
    bool found_lf = false;
    for (size_t i = dollar; i < buf_len; ++i) {
        if (data[i] == '\n') { lf = i; found_lf = true; break; }
    }
    if (!found_lf) return -1; // sentence not yet complete

    size_t total = lf + 1; // include everything from buf[0] through the '\n'

    auto* p = static_cast<uint8_t*>(std::malloc(total));
    if (!p) return -1;
    std::memcpy(p, buf, total);
    *out_frame = p;
    *out_len = total;
    return 0;
}

// ============================================================
// ABI: parse_message
//
// Finds '$' inside the frame (skipping any leading noise extract_frame
// had to include -- see its comment), validates the checksum (XOR of all
// bytes between '$' and '*', compared against the trailing 2 hex digits),
// extracts the sentence ID and comma-delimited fields, and dispatches to
// a per-sentence decoder (added in Tasks 4-7).
// ============================================================
extern "C" SDFC_EXPORT int parse_message(
    const uint8_t* frame, size_t frame_len, char** out_json, size_t* out_len)
{
    if (!frame || frame_len == 0 || !out_json || !out_len) return -1;

    const char* data = reinterpret_cast<const char*>(frame);
    const char* end = data + frame_len;

    const char* dollar = nullptr;
    for (const char* p = data; p < end; ++p) {
        if (*p == '$') { dollar = p; break; }
    }
    if (!dollar) return -1;

    const char* star = nullptr;
    for (const char* p = dollar + 1; p < end; ++p) {
        if (*p == '*') { star = p; break; }
    }
    if (!star || star + 3 > end) return -1; // need 2 hex digits after '*'

    // Checksum: XOR of every byte strictly between '$' and '*'.
    uint8_t computed = 0;
    for (const char* p = dollar + 1; p < star; ++p) computed ^= static_cast<uint8_t>(*p);

    auto hex_val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    int hi = hex_val(star[1]);
    int lo = hex_val(star[2]);
    if (hi < 0 || lo < 0) return -1;
    uint8_t expected = static_cast<uint8_t>((hi << 4) | lo);
    if (computed != expected) return -1; // fail closed on checksum mismatch

    // Sentence ID: from just after '$' up to the first comma.
    const char* first_comma = nullptr;
    for (const char* p = dollar + 1; p < star; ++p) {
        if (*p == ',') { first_comma = p; break; }
    }
    if (!first_comma) return -1; // every sentence here has at least one field
    std::string sentence_id(dollar + 1, first_comma);

    std::vector<std::string> fields = split_fields(first_comma + 1, star);

    JsonWriter w;
    w.key_str("message_name", sentence_id);

    if (sentence_id == "GPGSV" || sentence_id == "GLGSV" || sentence_id == "GAGSV" ||
        sentence_id == "GBGSV" || sentence_id == "GIGSV") {
        decode_gsv(fields, w);
    } else if (sentence_id == "GNGLL") {
        decode_gngll(fields, w);
    } else if (sentence_id == "GNZDA") {
        decode_gnzda(fields, w);
    } else if (sentence_id == "GNRMC") {
        decode_gnrmc(fields, w);
    } else if (sentence_id == "GNVTG") {
        decode_gnvtg(fields, w);
    } else if (sentence_id == "GNGGA") {
        decode_gngga(fields, w);
    } else if (sentence_id == "GNGSA") {
        decode_gngsa(fields, w);
    // --- SENTENCE_DISPATCH_INSERT_POINT: new else-if branches go above this line ---
    } else {
        return -1; // unrecognized sentence ID
    }

    std::string result = w.str();
    auto* out = static_cast<char*>(std::malloc(result.size() + 1));
    if (!out) return -1;
    std::memcpy(out, result.c_str(), result.size() + 1);
    *out_json = out;
    *out_len = result.size();
    return 0;
}

// ============================================================
// ABI: format_response
//
// Deferred. The receiver has no documented host-to-device command
// protocol; whether this DLL will ever need to send anything (and if so,
// what) is an explicit follow-up decision, not made here. Always returns
// -1 for now.
// ============================================================
extern "C" SDFC_EXPORT int format_response(
    const char* /*kind*/, const char* /*kwargs_json*/,
    uint8_t** /*out_buf*/, size_t* /*out_len*/)
{
    return -1;
}

// ============================================================
// ABI: free_result
// ============================================================
extern "C" SDFC_EXPORT void free_result(void* ptr) {
    std::free(ptr);
}
