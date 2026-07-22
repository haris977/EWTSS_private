#include "dfjob_dfdata_unmirror.h"

#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include "pugixml.hpp"

namespace {

// Every known DFJob/DFData tag name (DDFSystemControlInterfacePreClassifier.pdf
// §6.2.2 "Job definition" / §6.2.3 "Classification data", all of FORMAT01/
// 02/03), snake_case JSON key -> exact original XML tag. One explicit table
// for all of them -- not just the 7 that strictly need it -- so there is
// exactly one mechanism to audit against the ICD, not two.
const std::unordered_map<std::string, const char*> kTagLookup = {
    {"df_data",                "DFData"},
    {"df_job",                 "DFJob"},
    {"emitter_class",          "EmitterClass"},
    {"frequency",               "Frequency"},
    {"bandwidth",               "Bandwidth"},
    {"df_station_data",         "DFStationData"},
    {"df_station_name",         "DFStationName"},
    {"df_station_longitude",    "DFStationLongitude"},
    {"df_station_latitude",     "DFStationLatitude"},
    {"ddf-cl-id",               "DDF-CL-ID"},
    {"bearing_avg",             "BearingAvg"},
    {"bearing_std_dev",         "BearingStdDev"},
    {"quality",                 "Quality"},
    {"level_avg",               "LevelAvg"},
    {"level_std_dev",           "LevelStdDev"},
    {"first_detect",            "FirstDetect"},
    {"last_detect",             "LastDetect"},
    {"frequency_min",           "FrequencyMin"},
    {"frequency_max",           "FrequencyMax"},
    {"frequency_count",         "FrequencyCount"},
    {"frequency_list",          "FrequencyList"},
    {"frequency_list_report",   "FrequencyListReport"},
    {"frequency_report",        "FrequencyReport"},
    {"channel_spacing",         "ChannelSpacing"},
    {"burst_duration",          "BurstDuration"},
    {"start_frequency",         "StartFrequency"},
    {"center_frequency",        "CenterFrequency"},
    {"stop_frequency",          "StopFrequency"},
    {"elevation_avg",           "ElevationAvg"},
    {"elevation_std_dev",       "ElevationStdDev"},
    {"first_detection_time",    "FirstDetectionTime"},
    {"last_detection_time",     "LastDetectionTime"},
    {"scan_range_id",           "ScanRangeId"},
    {"burst_bandwidth",         "BurstBandwidth"},
    {"frequency_start",         "FrequencyStart"},
    {"frequency_stop",          "FrequencyStop"},
    {"frequency_step",          "FrequencyStep"},
};

const char* lookup_tag(const std::string& key) {
    auto it = kTagLookup.find(key);
    if (it == kTagLookup.end()) {
        throw std::runtime_error(
            "build_dfdata_xml/build_dfjob_xml: unknown key \"" + key +
            "\" -- not in the known DFJob/DFData tag vocabulary");
    }
    return it->second;
}

void append_child_value(pugi::xml_node& parent, const std::string& tag, const nlohmann::json& value);

// Fills `node`'s attributes/text/children from `value`. `value` is either
// a plain string (a pure leaf, e.g. "Static" for EmitterClass, or the
// literal comma-separated text of FrequencyList), or an object that may
// carry a "unit" attribute, "#text", and/or further nested known-tag
// children (e.g. DFStationData's fields, FrequencyListReport's repeated
// FrequencyReport, HwInfo-style nesting).
void fill_node(pugi::xml_node& node, const nlohmann::json& value) {
    if (value.is_string()) {
        node.text().set(value.get<std::string>().c_str());
        return;
    }
    if (!value.is_object()) {
        throw std::runtime_error(
            "build_dfdata_xml/build_dfjob_xml: expected a string or object value");
    }
    if (value.contains("unit"))
        node.append_attribute("Unit") = value.at("unit").get<std::string>().c_str();
    if (value.contains("#text"))
        node.text().set(value.at("#text").get<std::string>().c_str());

    for (auto& [key, child_value] : value.items()) {
        if (key == "unit" || key == "#text") continue;
        append_child_value(node, lookup_tag(key), child_value);
    }
}

// Appends one or more <tag>...</tag> children under `parent` from `value`
// -- a JSON array means a repeated sibling tag (mirror's one-or-many rule,
// same as pugixml_generic_unmirror.h), an empty object {} means an empty
// element, anything else recurses via fill_node.
void append_child_value(pugi::xml_node& parent, const std::string& tag, const nlohmann::json& value) {
    if (value.is_array()) {
        for (const auto& entry : value) append_child_value(parent, tag, entry);
        return;
    }
    pugi::xml_node node = parent.append_child(tag.c_str());
    if (value.is_object() && value.empty()) return;  // {} -> empty element
    fill_node(node, value);
}

std::string print_node(pugi::xml_node& node) {
    std::ostringstream oss;
    node.print(oss, "", pugi::format_raw);
    return oss.str();
}

} // namespace

std::string build_dfjob_xml(const nlohmann::json& df_job) {
    if (!df_job.is_object())
        throw std::runtime_error("build_dfjob_xml: \"df_job\" must be an object");

    pugi::xml_document doc;
    pugi::xml_node root = doc.append_child("DFJob");
    for (auto& [key, value] : df_job.items())
        append_child_value(root, lookup_tag(key), value);

    return print_node(root);
}

std::string build_df_station_data_xml(const nlohmann::json& df_station_data) {
    if (!df_station_data.is_object())
        throw std::runtime_error("build_df_station_data_xml: \"df_station_data\" must be an object");

    pugi::xml_document doc;
    pugi::xml_node root = doc.append_child("DFStationData");
    for (auto& [key, value] : df_station_data.items())
        append_child_value(root, lookup_tag(key), value);

    return print_node(root);
}

std::string build_dfdata_xml(const nlohmann::json& df_data) {
    if (!df_data.is_object())
        throw std::runtime_error("build_dfdata_xml: \"df_data\" must be an object");

    pugi::xml_document doc;
    pugi::xml_node root = doc.append_child("DFData");

    // "ddf-cl-id" at THIS level (root of df_data) is FORMAT02/03's usage --
    // an attribute on <DFData> itself, not a child element. The same key
    // nested inside "df_station_data" (FORMAT01) goes through the normal
    // child-element path below instead, via fill_node/append_child_value.
    if (df_data.contains("ddf-cl-id"))
        root.append_attribute("DDF-CL-ID") = df_data.at("ddf-cl-id").get<std::string>().c_str();

    for (auto& [key, value] : df_data.items()) {
        if (key == "ddf-cl-id") continue;  // handled above, as an attribute
        append_child_value(root, lookup_tag(key), value);
    }

    return print_node(root);
}
