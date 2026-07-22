#pragma once

// JSON->XML encoder for DFJob / DFData (preclassifier output, TCP 9154) --
// the encode-side counterpart of the DFJob/DFData decode path in
// classify_xml_root() (ddf550_parser.cpp). Unlike build_command_xml's
// Param/Struct/Array vocabulary (pugixml_generic_unmirror.h), DFJob/DFData
// use a much larger, closed set of ~38 known tag names, enumerated from
// DDFSystemControlInterfacePreClassifier.pdf. 7 of those cannot be safely
// reconstructed by a generic snake_case reversal: DFData, DFJob,
// DFStationData, DFStationName, DFStationLongitude, DFStationLatitude,
// DDF-CL-ID -- their DF/DDF acronym prefix collapses during decode (e.g.
// "DFStationData" -> "df_station_data"; naively reversing gives
// "DfStationData", not the real "DFStationData"). So this encoder uses one
// explicit lookup table for every known tag rather than an algorithm --
// any key not in the table throws, instead of silently guessing a wrong
// tag name.
//
// Output is raw, unwrapped XML text. DDFSystemControlInterfacePreClassifier.pdf
// gives no evidence of a magic-word envelope on port 9154 (unlike 9150's
// control channel) -- confirmed independently via DRS_BRIDGE_PORTS.md and
// the manual's own unwrapped examples. Callers frame this text however the
// 9154 wire contract actually requires; that framing decision is out of
// scope here.
//
// Field order in the output XML follows nlohmann::json's own key iteration
// order (alphabetical by default, not insertion order) -- not necessarily
// the order shown in the ICD's worked examples. XML element order is not
// semantically significant to a well-behaved consumer reading by tag name;
// if a real DDF-CL client ever proves order-sensitive, that's a targeted
// follow-up (explicit per-field ordering), not something guessed into this
// version.

#include <string>

#include "json.hpp"

// Builds "<DFJob>...</DFJob>" from a JSON object shaped exactly like
// parse_message()'s own mirrored "df_job" body value (see
// DDF550_9150_Control_XML_to_JSON.md Part 2, DFJob example).
std::string build_dfjob_xml(const nlohmann::json& df_job);

// Builds "<DFStationData>...</DFStationData>" from a JSON object shaped
// exactly like parse_message()'s own mirrored "df_station_data" body
// value. This is FORMAT02/03's standalone, once-per-analysis-period usage
// (root_tag "DFStationData", channel "preclassifier_output", msg_kind
// "df_station_data") -- distinct from FORMAT01, which nests the same
// content as a child of <DFData> instead (handled already, inside
// build_dfdata_xml, via the ordinary "df_station_data" key lookup).
std::string build_df_station_data_xml(const nlohmann::json& df_station_data);

// Builds "<DFData ...>...</DFData>" from a JSON object shaped exactly like
// parse_message()'s own mirrored "df_data" body value -- any of
// FORMAT01/02/03; the shape itself determines which tags appear, no format
// flag needed.
//
// Special case: "ddf-cl-id" at the TOP level of df_data becomes an
// attribute on the <DFData> root (FORMAT02/03's usage); the same key
// nested inside "df_station_data" becomes a child <DDF-CL-ID> element
// (FORMAT01's usage) -- these are genuinely different wire shapes that
// happen to mirror to the same JSON key on decode, disambiguated here by
// nesting depth, not guessed.
std::string build_dfdata_xml(const nlohmann::json& df_data);
