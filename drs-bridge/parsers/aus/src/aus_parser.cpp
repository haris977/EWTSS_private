// drs-bridge/parsers/aus/src/aus_parser.cpp
//
// AUS Protocol Analyser parser DLL.
// Source: AUS-C2 REST API — GET /api/v4.2/system (JSON over HTTP, port 5000).
// Implements the 4-symbol ABI defined in sdfc_abi.h.
//
// Scope:
//   extract_frame   — validates raw HTTP body is a complete /system JSON object;
//                     returns frame_type 3 (streaming detection data)
//   parse_message   — iterates devices_info + remote_id_devices_info + sensors_info,
//                     returns a normalized bridge JSON string (malloc'd, caller frees)
//   format_response — no-op (AUS-C2 is a read-only REST API from the bridge side)
//   free_result     — std::free wrapper
//
// Output JSON keys per detection:
//   id, type, model, detection_engine, status, source
//   frequency, bw_khz, modulation, frequency_hop
//   detect_counter, rssi_latest_dbm
//   finder, lat, lng, altitude_m, lf_error_radius_m
//   azimuth_deg, bearing_sensor_id
//   home_lat, home_lng
//   threat_level, is_near_probability
//   defense_freq_count, defense_freq_0 … defense_freq_3
//   rc_id, rc_finder, rc_lat, rc_lng, rc_azimuth_deg
//
// Sentinels: unavailable GPS = -999.0, unavailable float = -1.0, unavailable int = -1
#include "sdfc_abi.h"
#include "json_writer.h"
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

using nlohmann::json;
using sdfc::JsonWriter;

// ============================================================
// Helpers: safe JSON value extraction (never throws)
// ============================================================

static std::string str_val(const json& j, const char* key, const char* def = "") {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return def;
    return it->is_string() ? it->get<std::string>() : def;
}

static double dbl_val(const json& j, const char* key, double def = -1.0) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return def;
    return it->is_number() ? it->get<double>() : def;
}

static int int_val(const json& j, const char* key, int def = 0) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return def;
    return it->is_number() ? it->get<int>() : def;
}

static bool bool_val(const json& j, const char* key) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return false;
    return it->is_boolean() ? it->get<bool>() : false;
}

// ============================================================
// Build a JSON array from a list of already-serialized objects
// ============================================================

static std::string make_array(const std::vector<std::string>& items) {
    std::string a = "[";
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i) a += ',';
        a += items[i];
    }
    a += ']';
    return a;
}

// ============================================================
// Serialise one device entry from devices_info or remote_id_devices_info
// ============================================================

static std::string emit_device(const json& dev, const std::string& source) {
    JsonWriter jw;

    jw.key_str("id",               str_val(dev, "id"));
    jw.key_str("type",             str_val(dev, "type", "drone"));
    jw.key_str("model",            str_val(dev, "model"));
    jw.key_str("detection_engine", str_val(dev, "detection_engine"));
    jw.key_str("status",           str_val(dev, "status"));
    jw.key_str("source",           source);

    // RF characteristics
    jw.key_str("frequency",      str_val(dev, "frequency"));
    jw.key_int("bw_khz",         int_val(dev, "bw_khz", 0));
    jw.key_str("modulation",     str_val(dev, "modulation"));
    jw.key_bool("frequency_hop", bool_val(dev, "frequency_hop"));
    jw.key_int("detect_counter", int_val(dev, "detect_counter", 0));

    // Latest RSSI (first element of the signals history array)
    int rssi = 0;
    {
        auto it = dev.find("signals");
        if (it != dev.end() && it->is_array() && !it->empty())
            rssi = (*it)[0].get<int>();
    }
    jw.key_int("rssi_latest_dbm", rssi);

    // Geo-location
    jw.key_str("finder", str_val(dev, "finder"));

    double lat = -999.0, lng = -999.0;
    {
        auto it = dev.find("gps");
        if (it != dev.end() && it->is_object()) {
            lat = dbl_val(*it, "lat", -999.0);
            lng = dbl_val(*it, "lng", -999.0);
        }
    }
    jw.key_double("lat",              lat);
    jw.key_double("lng",              lng);
    jw.key_double("altitude_m",       dbl_val(dev, "drone_height", -1.0));
    jw.key_double("lf_error_radius_m", dbl_val(dev, "lf_error_radius", -1.0));

    // DF bearing — take the first sensor's azimuth from ld_result
    double azimuth = -1.0;
    std::string bearing_sensor_id;
    {
        auto it = dev.find("ld_result");
        if (it != dev.end() && it->is_object()) {
            for (auto& [sid, bearing] : it->items()) {
                if (bearing.is_object()) {
                    azimuth = dbl_val(bearing, "azimuth", -1.0);
                    bearing_sensor_id = sid;
                    break;
                }
            }
        }
    }
    jw.key_double("azimuth_deg",     azimuth);
    jw.key_str("bearing_sensor_id",  bearing_sensor_id);

    // Home GPS (drone's launch/home point)
    double hlat = -999.0, hlng = -999.0;
    {
        auto it = dev.find("home_gps");
        if (it != dev.end() && it->is_object()) {
            hlat = dbl_val(*it, "lat", -999.0);
            hlng = dbl_val(*it, "lng", -999.0);
        }
    }
    jw.key_double("home_lat", hlat);
    jw.key_double("home_lng", hlng);

    // Threat assessment
    int threat = 0;
    {
        auto it = dev.find("threat");
        if (it != dev.end() && it->is_object())
            threat = int_val(*it, "level", 0);
    }
    jw.key_int("threat_level",          threat);
    jw.key_double("is_near_probability", dbl_val(dev, "is_near_probability", -1.0));

    // Suggested jamming frequencies (up to 4 emitted as flat fields)
    int freq_count = 0;
    {
        auto it = dev.find("defense_frequencies");
        if (it != dev.end() && it->is_array()) {
            freq_count = static_cast<int>(it->size());
            int emit = (freq_count < 4) ? freq_count : 4;
            for (int i = 0; i < emit; ++i) {
                std::string fkey  = "defense_freq_" + std::to_string(i);
                std::string fval  = (*it)[i].is_string() ? (*it)[i].get<std::string>() : "";
                jw.key_str(fkey.c_str(), fval);
            }
        }
    }
    jw.key_int("defense_freq_count", freq_count);

    // Paired RC controller (embedded inside drone record when correlated)
    std::string rc_id, rc_finder;
    double rc_lat = -999.0, rc_lng = -999.0, rc_az = -1.0;
    {
        auto it = dev.find("paired_rc");
        if (it != dev.end() && it->is_object()) {
            const json& rc = *it;
            rc_id     = str_val(rc, "id");
            rc_finder = str_val(rc, "finder");
            auto git = rc.find("gps");
            if (git != rc.end() && git->is_object()) {
                rc_lat = dbl_val(*git, "lat", -999.0);
                rc_lng = dbl_val(*git, "lng", -999.0);
            }
            auto lit = rc.find("ld_result");
            if (lit != rc.end() && lit->is_object()) {
                for (auto& [sid, bearing] : lit->items()) {
                    if (bearing.is_object()) {
                        rc_az = dbl_val(bearing, "azimuth", -1.0);
                        break;
                    }
                }
            }
        }
    }
    jw.key_str("rc_id",          rc_id);
    jw.key_str("rc_finder",      rc_finder);
    jw.key_double("rc_lat",      rc_lat);
    jw.key_double("rc_lng",      rc_lng);
    jw.key_double("rc_azimuth_deg", rc_az);

    return jw.str();
}

// ============================================================
// Serialise one sensor entry from sensors_info
// ============================================================

static std::string emit_sensor(const std::string& sid, const json& sensor) {
    JsonWriter jw;
    jw.key_str("sensor_id",     sid);
    jw.key_str("status",        str_val(sensor, "status", "disconnected"));
    jw.key_bool("is_df_enabled", bool_val(sensor, "is_df_enabled"));

    double lat = -999.0, lng = -999.0;
    {
        auto it = sensor.find("gps");
        if (it != sensor.end() && it->is_object()) {
            lat = dbl_val(*it, "lat", -999.0);
            lng = dbl_val(*it, "lng", -999.0);
        }
    }
    jw.key_double("lat",        lat);
    jw.key_double("lng",        lng);
    jw.key_double("compass_deg", dbl_val(sensor, "compass", -1.0));
    return jw.str();
}

// ============================================================
// ABI: extract_frame
//
// Input: raw bytes from HTTP response body (GET /api/vX.Y/system).
// Validates the buffer contains a complete JSON object that looks like
// an AUS-C2 /system response (must start with '{', end with '}', and
// contain the key "devices_info").
// Returns 3 (streaming frame) on success, 0 if incomplete, -1 if invalid.
// ============================================================

extern "C" SDFC_EXPORT int extract_frame(
    const uint8_t* buf, size_t buf_len,
    uint8_t**      out_frame, size_t* out_len)
{
    if (!buf || buf_len == 0 || !out_frame || !out_len) return -1;

    const char* data = reinterpret_cast<const char*>(buf);
    int ibuf = static_cast<int>(buf_len);

    // Skip leading whitespace
    int start = 0;
    while (start < ibuf && (data[start] == ' '  || data[start] == '\t' ||
                              data[start] == '\n' || data[start] == '\r'))
        ++start;

    // Must open with '{'
    if (start >= ibuf || data[start] != '{') return -1;

    // Must contain the mandatory "devices_info" key to qualify as a /system response
    std::string_view sv(data, buf_len);
    if (sv.find("\"devices_info\"") == std::string_view::npos) return -1;

    // Find the last non-whitespace character — must be '}'
    int end = ibuf - 1;
    while (end >= 0 && (data[end] == ' '  || data[end] == '\t' ||
                         data[end] == '\n' || data[end] == '\r'))
        --end;

    if (end < start || data[end] != '}') return -1; // buffer is truncated

    int frame_bytes = end - start + 1;
    auto* p = static_cast<uint8_t*>(std::malloc(static_cast<std::size_t>(frame_bytes)));
    if (!p) return -1;
    std::memcpy(p, data + start, static_cast<std::size_t>(frame_bytes));
    *out_frame = p;
    *out_len   = static_cast<size_t>(frame_bytes);
    return 0;
}

// ============================================================
// ABI: parse_message
//
// Input: validated JSON bytes from extract_frame.
// Parses the AUS-C2 /system response and returns a heap-allocated JSON
// string containing a flat detection list and sensor status list.
// Caller MUST free the returned pointer with free_result().
// ============================================================

extern "C" SDFC_EXPORT int parse_message(
    const uint8_t* frame, size_t frame_len, char** out_json, size_t* out_len)
{
    if (!frame || frame_len == 0 || !out_json || !out_len) return -1;

    json root;
    try {
        root = json::parse(frame, frame + frame_len);
    } catch (const json::parse_error&) {
        return -1;
    }

    bool has_danger = root.value("has_danger", false);

    // Collect detections from both device tables
    std::vector<std::string> dets;
    auto collect_devices = [&](const char* table, const std::string& source) {
        auto it = root.find(table);
        if (it == root.end() || !it->is_object()) return;
        for (auto& [k, device] : it->items())
            dets.push_back(emit_device(device, source));
    };
    collect_devices("devices_info",          "devices");
    collect_devices("remote_id_devices_info", "remote_id");

    // Collect sensor statuses
    std::vector<std::string> sensors;
    {
        auto it = root.find("sensors_info");
        if (it != root.end() && it->is_object())
            for (auto& [sid, sensor] : it->items())
                sensors.push_back(emit_sensor(sid, sensor));
    }

    // Assemble top-level envelope
    JsonWriter top;
    top.key_int("frame_type",      3);
    top.key_bool("has_danger",     has_danger);
    top.key_int("detection_count", static_cast<int>(dets.size()));
    top.key_int("sensor_count",    static_cast<int>(sensors.size()));
    top.key_raw("detections",      make_array(dets));
    top.key_raw("sensors",         make_array(sensors));

    std::string result = top.str();
    auto* out = static_cast<char*>(std::malloc(result.size() + 1));
    if (!out) return -1;
    std::memcpy(out, result.c_str(), result.size() + 1);
    *out_json = out;
    *out_len  = result.size();
    return 0;
}

// ============================================================
// ABI: format_response
//
// AUS-C2 REST API is read-only from the bridge's perspective.
// The bridge polls GET /system — it never sends a binary response frame.
// Always returns -1 (no outbound frame to build).
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
