// drs-bridge/parsers/rfdds/src/rfdds_parser.cpp
//
// RFDDS/GJSS/RFCMS/PMSWG parser DLL — mimics device-side hardware for a
// real CPD (Command Post Display) client.
// Source: RFDDS_ICD.pdf ("Himashakti System (GJSS, RFCMS & RFDDS) —
// Interface Control Description", DLRL/DRDO), sections 4 (Jammer unit) and
// 5 (Detection unit), Appendix-A (master command-code table).
// Design doc: docs/ewtss/specs/rfdds-parser-design.md
//
// Direction split (deliberate, see design doc §4):
//   parse_message()   decodes the 20 CPD->device "1xx" requests.
//   format_response()  encodes the 20 device->CPD "2xx" replies.
// extract_frame() only determines frame boundaries for the 20 request
// codes (the DLL never needs to re-frame its own outgoing replies).
//
// Every decoded JSON object carries "command_code" and "message_name" as
// its first two keys (discriminator requirement, design doc §4) since a
// single command-code byte is the only signal distinguishing one request
// from another — there is no separate frame-type/magic-byte signal here.
#include "sdfc_abi.h"
#include "sdfc_endian.h"
#include "json_writer.h"
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using nlohmann::json;
using sdfc::JsonWriter;
using namespace sdfc; // load_*be / store_*be

// ============================================================
// Shared helpers
// ============================================================

// Reads n bytes at p as ASCII text, trimming trailing NUL padding.
static std::string read_fixed_str(const uint8_t* p, int n) {
    const char* c = reinterpret_cast<const char*>(p);
    int len = 0;
    while (len < n && c[len] != '\0') ++len;
    return std::string(c, static_cast<size_t>(len));
}

// Writes s into an n-byte buffer, zero-padded or truncated to fit.
static void write_fixed_str(uint8_t* buf, int n, const std::string& s) {
    int copy = static_cast<int>(s.size());
    if (copy > n) copy = n;
    std::memcpy(buf, s.data(), static_cast<size_t>(copy));
    if (copy < n) std::memset(buf + copy, 0, static_cast<size_t>(n - copy));
}

static void append_u8(std::vector<uint8_t>& v, uint8_t x) { v.push_back(x); }

static void append_u16be(std::vector<uint8_t>& v, uint16_t x) {
    uint8_t b[2]; store_u16be(b, x); v.insert(v.end(), b, b + 2);
}
static void append_u32be(std::vector<uint8_t>& v, uint32_t x) {
    uint8_t b[4]; store_u32be(b, x); v.insert(v.end(), b, b + 4);
}
static void append_i32be(std::vector<uint8_t>& v, int32_t x) {
    uint8_t b[4]; store_i32be(b, x); v.insert(v.end(), b, b + 4);
}
static void append_f32be(std::vector<uint8_t>& v, float x) {
    uint8_t b[4]; store_f32be(b, x); v.insert(v.end(), b, b + 4);
}
static void append_f64be(std::vector<uint8_t>& v, double x) {
    uint8_t b[8]; store_f64be(b, x); v.insert(v.end(), b, b + 8);
}
static void append_fixed_str(std::vector<uint8_t>& v, int n, const std::string& s) {
    size_t start = v.size();
    v.resize(start + static_cast<size_t>(n));
    write_fixed_str(v.data() + start, n, s);
}

// jkey: safe integer/double/string getters from a JSON kwargs object,
// used throughout format_response encoders. Returns `def` if key is
// absent, null, or the wrong type — never throws.
static long long jint(const json& j, const char* key, long long def = 0) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return def;
    return it->is_number() ? it->get<long long>() : def;
}
static double jdouble(const json& j, const char* key, double def = 0.0) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return def;
    return it->is_number() ? it->get<double>() : def;
}
static std::string jstr(const json& j, const char* key, const char* def = "") {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return def;
    return it->is_string() ? it->get<std::string>() : def;
}

// ============================================================
// ABI: extract_frame  (Task 2 fills this in)
// ============================================================
extern "C" SDFC_EXPORT int extract_frame(
    const uint8_t* buf, size_t buf_len,
    uint8_t** out_frame, size_t* out_len)
{
    if (!buf || buf_len == 0 || !out_frame || !out_len) return -1;
    uint8_t code = buf[0];

    size_t total = 0;
    switch (code) {
        case 101: total = 9;  break; // Connect
        case 107: total = 1;  break; // Manual_health_check_request
        case 105: total = 1;  break; // Stop_Jam
        case 103: total = 7;  break; // GNSS_Start_Jam
        case 106: total = 1;  break; // Stop_Jam_GNSS
        case 113: total = 2;  break; // NAV_data_enable/disable
        case 109: total = 1;  break; // Bite_mode_request
        case 117: total = 21; break; // Extract_param_req
        case 111: total = 151; break; // Threat_Library_addition
        case 115: total = 1;  break; // Threat_Library_export_request
        case 116: total = 27; break; // Threat_Library_delete_request
        case 121: total = 1;  break; // SBC_shutdown_req
        case 118: total = 1;  break; // SKYCOPE_health_status_req
        case 120: total = 1;  break; // SKYCOPE_shutdown_req
        case 119: total = 2;  break; // Detection_configuration
        case 122: total = 2;  break; // Detection_Fusion_Request
        case 123: total = 10; break; // Radar_Blanking_Req

        case 102: { // Start_Jam: fixed 15B header, count(u8) @14, unit 24B
            if (buf_len < 15) return -1;
            uint8_t band_count = buf[14];
            total = 15 + static_cast<size_t>(band_count) * 24;
            break;
        }
        case 104: { // GNSS_Spoof: fixed 57B header, count(u16be) @55, unit 16B
            if (buf_len < 57) return -1;
            uint16_t waypoints = load_u16be(buf + 55);
            total = 57 + static_cast<size_t>(waypoints) * 16;
            break;
        }
        case 114: { // Threat_Library_import_request: fixed 5B header, count(u32be) @1, unit 150B
            if (buf_len < 5) return -1;
            uint32_t threat_count = load_u32be(buf + 1);
            total = 5 + static_cast<size_t>(threat_count) * 150;
            break;
        }
        default:
            return -1; // unrecognized command code
    }

    if (buf_len < total) return -1; // not enough data yet

    auto* p = static_cast<uint8_t*>(std::malloc(total));
    if (!p) return -1;
    std::memcpy(p, buf, total);
    *out_frame = p;
    *out_len = total;
    return 0;
}

// ============================================================
// Decoders — CPD -> device requests (parse_message)
// ============================================================

// 101 Connect: Command_Code(1) + True North double(8) @1
static void decode_connect(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "Connect payload < 8 bytes"); return; }
    w.key_double("true_north", load_f64be(p));
}

// 107 Manual_health_check_request: no fields beyond Command_Code.
static void decode_manual_health_check_request(const uint8_t*, int, JsonWriter&) {}

// 102 Start_Jam: 15B header + band_count x 24B repeated blocks.
static void decode_start_jam(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 14) { w.key_str("warning", "Start_Jam payload < 14 bytes"); return; }
    w.key_uint("jam_duration",         load_u16be(p + 0));
    w.key_uint("jam_power",            p[2]);
    w.key_uint("jam_mode",             p[3]);
    w.key_uint("jam_type",             p[4]);
    w.key_bool("ism_434mhz",           p[5] != 0);
    w.key_bool("ism_865mhz",           p[6] != 0);
    w.key_bool("ism_915mhz",           p[7] != 0);
    w.key_bool("ism_2450mhz",          p[8] != 0);
    w.key_bool("ism_5800mhz",          p[9] != 0);
    w.key_uint("modulation_scheme_1",  p[10]);
    w.key_uint("modulation_scheme_2",  p[11]);
    w.key_uint("modulation_scheme_3",  p[12]);
    uint8_t band_count = p[13];
    w.key_int("band_count", band_count);

    std::vector<std::string> bands;
    const int header = 14;
    const int unit = 24;
    for (int i = 0; i < band_count; ++i) {
        int off = header + i * unit;
        if (off + unit > n) break;
        JsonWriter bw;
        bw.key_str("jam_frequency",      read_fixed_str(p + off, 8));
        bw.key_str("jam_freq_bandwidth", read_fixed_str(p + off + 8, 4));
        bw.key_int("dwell_time_us",      load_i32be(p + off + 12));
        bw.key_double("step_size_mhz",   load_f64be(p + off + 16));
        bands.push_back(bw.str());
    }
    std::string arr = "[";
    for (size_t i = 0; i < bands.size(); ++i) { if (i) arr += ','; arr += bands[i]; }
    arr += ']';
    w.key_raw("bands", arr);
}

// 105 Stop_Jam: no fields beyond Command_Code.
static void decode_stop_jam(const uint8_t*, int, JsonWriter&) {}

// 103 GNSS_Start_Jam: antenna(u8@0)+jam_power(u8@1)+satellite_selection(i32be@2)
static void decode_gnss_start_jam(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 6) { w.key_str("warning", "GNSS_Start_Jam payload < 6 bytes"); return; }
    w.key_uint("antenna",              p[0]);
    w.key_uint("jam_power",            p[1]);
    w.key_int("satellite_selection",   load_i32be(p + 2));
}

// 104 GNSS_Spoof: see field table in design/plan docs. Always decodes the
// full 56-byte fixed body + waypoint array, per the flagged framing
// assumption (design spec §7) -- do not gate on spoof_mode.
static void decode_gnss_spoof(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 56) { w.key_str("warning", "GNSS_Spoof payload < 56 bytes"); return; }
    w.key_uint("antenna",                     p[0]);
    w.key_uint("jam_power",                   p[1]);
    w.key_uint("elevation_mask_deg",          p[2]);
    w.key_uint("initial_jam_time_sec",        load_u16be(p + 3));
    w.key_int("satellite_selection",          load_i32be(p + 5));
    w.key_uint("spoof_mode",                  p[9]);
    w.key_double("latitude",                  load_f64be(p + 10));
    w.key_double("longitude",                 load_f64be(p + 18));
    w.key_int("target_altitude_m",            load_i32be(p + 26));
    w.key_int("target_speed_mps",             load_i32be(p + 30));
    w.key_int("target_trajectory_time_sec",   load_i32be(p + 34));
    w.key_int("rotation_count",                load_i32be(p + 38));
    w.key_int("rotation_duration_sec",        load_i32be(p + 42));
    w.key_int("circle_radius_m",              load_i32be(p + 46));
    w.key_int("bearing_angle_deg",            load_i32be(p + 50));
    uint16_t waypoint_count = load_u16be(p + 54);
    w.key_int("waypoint_count", waypoint_count);

    std::vector<std::string> waypoints;
    const int header = 56;
    const int unit = 16;
    for (int i = 0; i < waypoint_count; ++i) {
        int off = header + i * unit;
        if (off + unit > n) break;
        JsonWriter ww;
        ww.key_double("latitude",  load_f64be(p + off));
        ww.key_double("longitude", load_f64be(p + off + 8));
        waypoints.push_back(ww.str());
    }
    std::string arr = "[";
    for (size_t i = 0; i < waypoints.size(); ++i) { if (i) arr += ','; arr += waypoints[i]; }
    arr += ']';
    w.key_raw("waypoints", arr);
}

// 106 Stop_Jam_GNSS: no fields beyond Command_Code.
static void decode_stop_jam_gnss(const uint8_t*, int, JsonWriter&) {}

// 113 NAV_data_enable/disable: enable(u8@0), 1=enable, 0=disable.
static void decode_nav_data_enable_disable(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 1) { w.key_str("warning", "NAV_data payload < 1 byte"); return; }
    w.key_bool("enable", p[0] != 0);
}

// 109 Bite_mode_request: no fields beyond Command_Code.
static void decode_bite_mode_request(const uint8_t*, int, JsonWriter&) {}

// 121 SBC_shutdown_req: no fields beyond Command_Code.
static void decode_sbc_shutdown_req(const uint8_t*, int, JsonWriter&) {}

// 117 Extract_param_req: center_frequency_mhz(f64be@0), instantaneous_bandwidth_khz(f64be@8), doa_deg(u32be@16)
static void decode_extract_param_req(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 20) { w.key_str("warning", "Extract_param_req payload < 20 bytes"); return; }
    w.key_double("center_frequency_mhz",           load_f64be(p + 0));
    w.key_double("instantaneous_bandwidth_khz",    load_f64be(p + 8));
    w.key_uint("doa_deg",                          load_u32be(p + 16));
}

// 111 Threat_Library_addition: frequency_parameter_type(u8@0),
// frequency_lower_or_center_mhz(f64be@1), frequency_higher_mhz(f64be@9),
// instantaneous_bandwidth_khz(f64be@17), burst_period(50B str@25),
// burst_width(50B str@75), label(25B str@125).
static void decode_threat_library_addition(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 150) { w.key_str("warning", "Threat_Library_addition payload < 150 bytes"); return; }
    w.key_uint("frequency_parameter_type",         p[0]);
    w.key_double("frequency_lower_or_center_mhz",  load_f64be(p + 1));
    w.key_double("frequency_higher_mhz",           load_f64be(p + 9));
    w.key_double("instantaneous_bandwidth_khz",    load_f64be(p + 17));
    w.key_str("burst_period",  read_fixed_str(p + 25, 50));
    w.key_str("burst_width",   read_fixed_str(p + 75, 50));
    w.key_str("label",         read_fixed_str(p + 125, 25));
}

// Shared 150-byte threat-block decoder, used by both import and export-ack.
static std::string decode_threat_block(const uint8_t* p) {
    JsonWriter tw;
    tw.key_uint("frequency_parameter_type",        p[0]);
    tw.key_double("frequency_lower_or_center_mhz", load_f64be(p + 1));
    tw.key_double("frequency_higher_mhz",          load_f64be(p + 9));
    tw.key_double("instantaneous_bandwidth_khz",   load_f64be(p + 17));
    tw.key_str("burst_period", read_fixed_str(p + 25, 50));
    tw.key_str("burst_width",  read_fixed_str(p + 75, 50));
    tw.key_str("label",        read_fixed_str(p + 125, 25));
    return tw.str();
}

// Shared 150-byte threat-block encoder, used by both import and export-ack.
static void encode_threat_block(std::vector<uint8_t>& buf, const json& t) {
    append_u8(buf, static_cast<uint8_t>(jint(t, "frequency_parameter_type")));
    append_f64be(buf, jdouble(t, "frequency_lower_or_center_mhz"));
    append_f64be(buf, jdouble(t, "frequency_higher_mhz"));
    append_f64be(buf, jdouble(t, "instantaneous_bandwidth_khz"));
    append_fixed_str(buf, 50, jstr(t, "burst_period"));
    append_fixed_str(buf, 50, jstr(t, "burst_width"));
    append_fixed_str(buf, 25, jstr(t, "label"));
}

// 114 Threat_Library_import_request: threat_count(u32be@0) + N x 150B threat blocks.
static void decode_threat_library_import_request(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "Threat_Library_import_request payload < 4 bytes"); return; }
    uint32_t threat_count = load_u32be(p + 0);
    w.key_uint("threat_count", threat_count);
    std::vector<std::string> blocks;
    for (uint32_t i = 0; i < threat_count; ++i) {
        int off = 4 + static_cast<int>(i) * 150;
        if (off + 150 > n) break;
        blocks.push_back(decode_threat_block(p + off));
    }
    std::string arr = "[";
    for (size_t i = 0; i < blocks.size(); ++i) { if (i) arr += ','; arr += blocks[i]; }
    arr += ']';
    w.key_raw("threats", arr);
}

// 115 Threat_Library_export_request: no fields beyond Command_Code.
static void decode_threat_library_export_request(const uint8_t*, int, JsonWriter&) {}

// 116 Threat_Library_delete_request: delete_type(1-byte char@0), label(25B str@1).
static void decode_threat_library_delete_request(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 26) { w.key_str("warning", "Threat_Library_delete_request payload < 26 bytes"); return; }
    std::string dt(1, static_cast<char>(p[0]));
    w.key_str("delete_type", dt);
    w.key_str("label", read_fixed_str(p + 1, 25));
}

// 118 SKYCOPE_health_status_req: no fields beyond Command_Code.
static void decode_skycope_health_status_req(const uint8_t*, int, JsonWriter&) {}

// 120 SKYCOPE_shutdown_req: no fields beyond Command_Code.
static void decode_skycope_shutdown_req(const uint8_t*, int, JsonWriter&) {}

// 119 Detection_configuration: skycope_flag(u8@0).
static void decode_detection_configuration(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 1) { w.key_str("warning", "Detection_configuration payload < 1 byte"); return; }
    w.key_uint("skycope_flag", p[0]);
}

// 122 Detection_Fusion_Request: fusion_mode(u8@0).
static void decode_detection_fusion_request(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 1) { w.key_str("warning", "Detection_Fusion_Request payload < 1 byte"); return; }
    w.key_uint("fusion_mode", p[0]);
}

// 123 Radar_Blanking_Req: radar_lat_x1e6(i32be@0), radar_lng_x1e6(i32be@4), blanking_enable(u8@8).
static void decode_radar_blanking_req(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 9) { w.key_str("warning", "Radar_Blanking_Req payload < 9 bytes"); return; }
    w.key_int("radar_lat_x1e6",   load_i32be(p + 0));
    w.key_int("radar_lng_x1e6",   load_i32be(p + 4));
    w.key_bool("blanking_enable", p[8] != 0);
}

// ============================================================
// Encoders — device -> CPD replies (format_response)
// ============================================================

// 201 Health_Status: sdr1..sdr6(1B each) + pa1..pa4(1B each) = 10 bytes payload.
static std::vector<uint8_t> encode_health_status(const json& kw) {
    std::vector<uint8_t> buf;
    append_u8(buf, 201);
    append_u8(buf, static_cast<uint8_t>(jint(kw, "sdr1")));
    append_u8(buf, static_cast<uint8_t>(jint(kw, "sdr2")));
    append_u8(buf, static_cast<uint8_t>(jint(kw, "sdr3")));
    append_u8(buf, static_cast<uint8_t>(jint(kw, "sdr4")));
    append_u8(buf, static_cast<uint8_t>(jint(kw, "sdr5")));
    append_u8(buf, static_cast<uint8_t>(jint(kw, "sdr6")));
    append_u8(buf, static_cast<uint8_t>(jint(kw, "pa1")));
    append_u8(buf, static_cast<uint8_t>(jint(kw, "pa2")));
    append_u8(buf, static_cast<uint8_t>(jint(kw, "pa3")));
    append_u8(buf, static_cast<uint8_t>(jint(kw, "pa4")));
    return buf;
}

// 207 Manual_health_status_reply: tsu_status(1B) + gnss_receiver_status(1B).
static std::vector<uint8_t> encode_manual_health_status_reply(const json& kw) {
    std::vector<uint8_t> buf;
    append_u8(buf, 207);
    append_u8(buf, static_cast<uint8_t>(jint(kw, "tsu_status")));
    append_u8(buf, static_cast<uint8_t>(jint(kw, "gnss_receiver_status")));
    return buf;
}

// 203 Start_Jam_Ack: pa1/pa2/pa3(1B each) + frequency_count(1B) + N x frequency(8B double).
static std::vector<uint8_t> encode_start_jam_ack(const json& kw) {
    std::vector<uint8_t> buf;
    append_u8(buf, 203);
    append_u8(buf, static_cast<uint8_t>(jint(kw, "pa1")));
    append_u8(buf, static_cast<uint8_t>(jint(kw, "pa2")));
    append_u8(buf, static_cast<uint8_t>(jint(kw, "pa3")));
    auto it = kw.find("frequencies");
    if (it == kw.end() || !it->is_array()) return {}; // invalid input
    append_u8(buf, static_cast<uint8_t>(it->size()));
    for (const auto& f : *it) {
        if (!f.is_number()) return {};
        append_f64be(buf, f.get<double>());
    }
    return buf;
}

// 204 Stop_Jam_Ack: no fields beyond Command_Code.
static std::vector<uint8_t> encode_stop_jam_ack(const json&) {
    return { 204 };
}

// 205 Jam_Spoof_Ack: pa4(1B). Real 2-byte message per the ICD's own table.
static std::vector<uint8_t> encode_jam_spoof_ack(const json& kw) {
    return { 205, static_cast<uint8_t>(jint(kw, "pa4")) };
}

// 206 Stop_Jam_GNSS_Ack: no fields beyond Command_Code.
static std::vector<uint8_t> encode_stop_jam_gnss_ack(const json&) {
    return { 206 };
}

// 108/208 GNSS_Start_Jam_Ack (dual code -- Appendix-A literally assigns
// 108, breaking the 1xx/2xx convention; almost certainly a typo for 208).
// Decision: implement both. Optional kwargs "command_code" selects which
// byte to emit; defaults to 208.
static std::vector<uint8_t> encode_gnss_start_jam_ack(const json& kw) {
    long long code = jint(kw, "command_code", 208);
    if (code != 108 && code != 208) return {}; // invalid input
    return { static_cast<uint8_t>(code), static_cast<uint8_t>(jint(kw, "pa4")) };
}

// 213 NAV_data_enable/disable_ack: no fields beyond Command_Code.
static std::vector<uint8_t> encode_nav_data_enable_disable_ack(const json&) {
    return { 213 };
}

// 209 Bite_mode_reply: receiver_antenna_status, fixed 16-byte ASCII string.
static std::vector<uint8_t> encode_bite_mode_reply(const json& kw) {
    std::vector<uint8_t> buf;
    append_u8(buf, 209);
    append_fixed_str(buf, 16, jstr(kw, "receiver_antenna_status"));
    return buf;
}

// 217 Extract_param_reply: center_frequency_mhz(f64be) + instantaneous_bandwidth_khz(f32be) + burst_period(50B str) + burst_width(50B str)
static std::vector<uint8_t> encode_extract_param_reply(const json& kw) {
    std::vector<uint8_t> buf;
    append_u8(buf, 217);
    append_f64be(buf, jdouble(kw, "center_frequency_mhz"));
    append_f32be(buf, static_cast<float>(jdouble(kw, "instantaneous_bandwidth_khz")));
    append_fixed_str(buf, 50, jstr(kw, "burst_period"));
    append_fixed_str(buf, 50, jstr(kw, "burst_width"));
    return buf;
}

// 211 Threat_Library_addition_ack: ack(u8) + error_code(1-byte ASCII char).
static std::vector<uint8_t> encode_threat_library_addition_ack(const json& kw) {
    std::string ec = jstr(kw, "error_code", " ");
    return { 211, static_cast<uint8_t>(jint(kw, "ack")), static_cast<uint8_t>(ec.empty() ? ' ' : ec[0]) };
}

// 214 Threat_Library_import_ack: ack(u8) + error_code(1-byte char).
static std::vector<uint8_t> encode_threat_library_import_ack(const json& kw) {
    std::string ec = jstr(kw, "error_code", " ");
    return { 214, static_cast<uint8_t>(jint(kw, "ack")), static_cast<uint8_t>(ec.empty() ? ' ' : ec[0]) };
}

// 215 Threat_Library_export_ack: threat_count(u32be) + N x 150B threat blocks.
static std::vector<uint8_t> encode_threat_library_export_ack(const json& kw) {
    std::vector<uint8_t> buf;
    append_u8(buf, 215);
    auto it = kw.find("threats");
    if (it == kw.end() || !it->is_array()) return {};
    append_u32be(buf, static_cast<uint32_t>(it->size()));
    for (const auto& t : *it) encode_threat_block(buf, t);
    return buf;
}

// 216 Threat_Library_delete_ack: ack(u8) + error_code(1-byte char).
static std::vector<uint8_t> encode_threat_library_delete_ack(const json& kw) {
    std::string ec = jstr(kw, "error_code", " ");
    return { 216, static_cast<uint8_t>(jint(kw, "ack")), static_cast<uint8_t>(ec.empty() ? ' ' : ec[0]) };
}

// 220 SKYCOPE_shutdown_reply: no fields beyond Command_Code.
static std::vector<uint8_t> encode_skycope_shutdown_reply(const json&) {
    return { 220 };
}

// 219 Detection_configuration_ack: no fields beyond Command_Code.
static std::vector<uint8_t> encode_detection_configuration_ack(const json&) {
    return { 219 };
}

// 222 Detection_Fusion_Reply: no fields beyond Command_Code.
static std::vector<uint8_t> encode_detection_fusion_reply(const json&) {
    return { 222 };
}

// 223 Radar_Blanking_Reply: no fields beyond Command_Code.
static std::vector<uint8_t> encode_radar_blanking_reply(const json&) {
    return { 223 };
}

// 218 SKYCOPE_health_status_reply: 3 length-prefixed strings (sensor_id,
// disk_usage, system_time) interleaved with fixed scalar fields, in exact
// ICD table order. Direction per Appendix-A is PMSWG (prose text in §5.4/
// §5.5 says GJSS -- a documented ICD inconsistency, doesn't affect wire bytes).
static std::vector<uint8_t> encode_skycope_health_status_reply(const json& kw) {
    std::vector<uint8_t> buf;
    append_u8(buf, 218);

    std::string sensor_id = jstr(kw, "sensor_id");
    if (sensor_id.size() > 255) return {}; // length byte can't represent more
    append_u8(buf, static_cast<uint8_t>(sensor_id.size()));
    buf.insert(buf.end(), sensor_id.begin(), sensor_id.end());

    append_u16be(buf, static_cast<uint16_t>(jint(kw, "detection_range_m")));
    append_i32be(buf, static_cast<int32_t>(jint(kw, "gps_latitude_x1e6")));
    append_i32be(buf, static_cast<int32_t>(jint(kw, "gps_longitude_x1e6")));
    append_u16be(buf, static_cast<uint16_t>(jint(kw, "cpu_temperature_x10")));
    append_u16be(buf, static_cast<uint16_t>(jint(kw, "cpu_usage_pct_x10")));

    std::string disk_usage = jstr(kw, "disk_usage");
    if (disk_usage.size() > 255) return {};
    append_u8(buf, static_cast<uint8_t>(disk_usage.size()));
    buf.insert(buf.end(), disk_usage.begin(), disk_usage.end());

    append_u16be(buf, static_cast<uint16_t>(jint(kw, "gpu_temperature_x10")));
    append_u16be(buf, static_cast<uint16_t>(jint(kw, "gpu_usage_pct_x10")));
    append_u16be(buf, static_cast<uint16_t>(jint(kw, "memory_usage_pct_x10")));
    append_u8(buf, static_cast<uint8_t>(jint(kw, "power_consumption")));

    std::string system_time = jstr(kw, "system_time");
    if (system_time.size() > 255) return {};
    append_u8(buf, static_cast<uint8_t>(system_time.size()));
    buf.insert(buf.end(), system_time.begin(), system_time.end());

    append_u32be(buf, static_cast<uint32_t>(jint(kw, "uptime_x100")));
    append_u16be(buf, static_cast<uint16_t>(jint(kw, "xpu_frequency")));
    append_u16be(buf, static_cast<uint16_t>(jint(kw, "nfz_height_m")));
    append_i32be(buf, static_cast<int32_t>(jint(kw, "nfz_latitude_x1e6")));
    append_i32be(buf, static_cast<int32_t>(jint(kw, "nfz_longitude_x1e6")));
    return buf;
}

// 202 Targets_Detected: target_count(u32be@0) + N x 101B target blocks.
// Note: SBC_shutdown_req (121) has no corresponding reply anywhere in the
// ICD -- that's a real asymmetry, not a gap to fill in here.
static std::vector<uint8_t> encode_targets_detected(const json& kw) {
    std::vector<uint8_t> buf;
    append_u8(buf, 202);
    auto it = kw.find("targets");
    if (it == kw.end() || !it->is_array()) return {};
    append_u32be(buf, static_cast<uint32_t>(it->size()));
    for (const auto& t : *it) {
        append_u8(buf, static_cast<uint8_t>(jint(t, "skycope_or_rfdd_flag")));
        append_u32be(buf, static_cast<uint32_t>(jint(t, "target_id")));
        append_u8(buf, static_cast<uint8_t>(jint(t, "band_id")));
        append_f64be(buf, jdouble(t, "center_frequency_mhz"));
        append_f32be(buf, static_cast<float>(jdouble(t, "bandwidth_khz")));
        append_f32be(buf, static_cast<float>(jdouble(t, "signal_strength_dbm")));
        append_fixed_str(buf, 6, jstr(t, "time_of_first_arrival"));
        append_fixed_str(buf, 6, jstr(t, "time_of_last_arrival"));
        append_u32be(buf, static_cast<uint32_t>(jint(t, "doa_deg_x1000")));
        append_u32be(buf, static_cast<uint32_t>(jint(t, "elevation_deg_x1000")));
        append_u8(buf, static_cast<uint8_t>(jint(t, "threat_confidence_pct")));
        append_fixed_str(buf, 25, jstr(t, "target_model_label"));
        append_i32be(buf, static_cast<int32_t>(jint(t, "drone_lat_x1e6")));
        append_i32be(buf, static_cast<int32_t>(jint(t, "drone_lng_x1e6")));
        append_i32be(buf, static_cast<int32_t>(jint(t, "remote_lat_x1e6")));
        append_i32be(buf, static_cast<int32_t>(jint(t, "remote_lng_x1e6")));
        std::string emitter = jstr(t, "emitter_type", "U");
        append_u8(buf, emitter.empty() ? 'U' : static_cast<uint8_t>(emitter[0]));
        append_i32be(buf, static_cast<int32_t>(jint(t, "home_lat_x1e6")));
        append_i32be(buf, static_cast<int32_t>(jint(t, "home_lng_x1e6")));
        append_u32be(buf, static_cast<uint32_t>(jint(t, "distance_m")));
        append_u32be(buf, static_cast<uint32_t>(jint(t, "height_m")));
    }
    return buf;
}

// ============================================================
// ABI: parse_message  (Tasks 3-11 add case labels to the switch below)
// ============================================================
extern "C" SDFC_EXPORT int parse_message(
    const uint8_t* frame, size_t frame_len, char** out_json, size_t* out_len)
{
    if (!frame || frame_len == 0 || !out_json || !out_len) return -1;
    uint8_t code = frame[0];
    const uint8_t* p = frame + 1;
    int n = static_cast<int>(frame_len) - 1;
    JsonWriter w;
    w.key_int("command_code", code);

    switch (code) {
        case 101: w.key_str("message_name", "Connect"); decode_connect(p, n, w); break;
        case 107: w.key_str("message_name", "Manual_health_check_request"); decode_manual_health_check_request(p, n, w); break;
        case 102: w.key_str("message_name", "Start_Jam"); decode_start_jam(p, n, w); break;
        case 105: w.key_str("message_name", "Stop_Jam"); decode_stop_jam(p, n, w); break;
        case 103: w.key_str("message_name", "GNSS_Start_Jam"); decode_gnss_start_jam(p, n, w); break;
        case 104: w.key_str("message_name", "GNSS_Spoof"); decode_gnss_spoof(p, n, w); break;
        case 106: w.key_str("message_name", "Stop_Jam_GNSS"); decode_stop_jam_gnss(p, n, w); break;
        case 113: w.key_str("message_name", "NAV_data_enable/disable"); decode_nav_data_enable_disable(p, n, w); break;
        case 109: w.key_str("message_name", "Bite_mode_request"); decode_bite_mode_request(p, n, w); break;
        case 121: w.key_str("message_name", "SBC_shutdown_req"); decode_sbc_shutdown_req(p, n, w); break;
        case 117: w.key_str("message_name", "Extract_param_req"); decode_extract_param_req(p, n, w); break;
        case 111: w.key_str("message_name", "Threat_Library_addition"); decode_threat_library_addition(p, n, w); break;
        case 114: w.key_str("message_name", "Threat_Library_import_request"); decode_threat_library_import_request(p, n, w); break;
        case 115: w.key_str("message_name", "Threat_Library_export_request"); decode_threat_library_export_request(p, n, w); break;
        case 116: w.key_str("message_name", "Threat_Library_delete_request"); decode_threat_library_delete_request(p, n, w); break;
        case 118: w.key_str("message_name", "SKYCOPE_health_status_req"); decode_skycope_health_status_req(p, n, w); break;
        case 120: w.key_str("message_name", "SKYCOPE_shutdown_req"); decode_skycope_shutdown_req(p, n, w); break;
        case 119: w.key_str("message_name", "Detection_configuration"); decode_detection_configuration(p, n, w); break;
        case 122: w.key_str("message_name", "Detection_Fusion_Request"); decode_detection_fusion_request(p, n, w); break;
        case 123: w.key_str("message_name", "Radar_Blanking_Req"); decode_radar_blanking_req(p, n, w); break;
        default:
            return -1;
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
// ABI: format_response  (Tasks 3-11 add else-if branches below)
// ============================================================
extern "C" SDFC_EXPORT int format_response(
    const char* kind, const char* kwargs_json,
    uint8_t** out_buf, size_t* out_len)
{
    if (!kind || !kwargs_json || !out_buf || !out_len) return -1;
    json kw;
    try {
        kw = json::parse(kwargs_json);
    } catch (const json::parse_error&) {
        return -1;
    }

    std::vector<uint8_t> buf;

    if (std::strcmp(kind, "Health_Status") == 0) {
        buf = encode_health_status(kw);
    } else if (std::strcmp(kind, "Manual_health_status_reply") == 0) {
        buf = encode_manual_health_status_reply(kw);
    } else if (std::strcmp(kind, "Start_Jam_Ack") == 0) {
        buf = encode_start_jam_ack(kw);
    } else if (std::strcmp(kind, "Stop_Jam_Ack") == 0) {
        buf = encode_stop_jam_ack(kw);
    } else if (std::strcmp(kind, "Jam_Spoof_Ack") == 0) {
        buf = encode_jam_spoof_ack(kw);
    } else if (std::strcmp(kind, "Stop_Jam_GNSS_Ack") == 0) {
        buf = encode_stop_jam_gnss_ack(kw);
    } else if (std::strcmp(kind, "GNSS_Start_Jam_Ack") == 0) {
        buf = encode_gnss_start_jam_ack(kw);
    } else if (std::strcmp(kind, "NAV_data_enable/disable_ack") == 0) {
        buf = encode_nav_data_enable_disable_ack(kw);
    } else if (std::strcmp(kind, "Bite_mode_reply") == 0) {
        buf = encode_bite_mode_reply(kw);
    } else if (std::strcmp(kind, "Targets_Detected") == 0) {
        buf = encode_targets_detected(kw);
    } else if (std::strcmp(kind, "Extract_param_reply") == 0) {
        buf = encode_extract_param_reply(kw);
    } else if (std::strcmp(kind, "Threat_Library_addition_ack") == 0) {
        buf = encode_threat_library_addition_ack(kw);
    } else if (std::strcmp(kind, "Threat_Library_import_ack") == 0) {
        buf = encode_threat_library_import_ack(kw);
    } else if (std::strcmp(kind, "Threat_Library_export_ack") == 0) {
        buf = encode_threat_library_export_ack(kw);
    } else if (std::strcmp(kind, "Threat_Library_delete_ack") == 0) {
        buf = encode_threat_library_delete_ack(kw);
    } else if (std::strcmp(kind, "SKYCOPE_health_status_reply") == 0) {
        buf = encode_skycope_health_status_reply(kw);
    } else if (std::strcmp(kind, "SKYCOPE_shutdown_reply") == 0) {
        buf = encode_skycope_shutdown_reply(kw);
    } else if (std::strcmp(kind, "Detection_configuration_ack") == 0) {
        buf = encode_detection_configuration_ack(kw);
    } else if (std::strcmp(kind, "Detection_Fusion_Reply") == 0) {
        buf = encode_detection_fusion_reply(kw);
    } else if (std::strcmp(kind, "Radar_Blanking_Reply") == 0) {
        buf = encode_radar_blanking_reply(kw);
    } else {
        return -1;
    }
    if (buf.empty()) return -1;

    auto* out = static_cast<uint8_t*>(std::malloc(buf.size()));
    if (!out) return -1;
    std::memcpy(out, buf.data(), buf.size());
    *out_buf = out;
    *out_len = buf.size();
    return 0;
}

// ============================================================
// ABI: free_result
// ============================================================
extern "C" SDFC_EXPORT void free_result(void* ptr) {
    std::free(ptr);
}
