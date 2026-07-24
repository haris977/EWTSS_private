#pragma once

// CA120 snake_case -> original-XML-name lookup tables, used only by
// format_response()'s JSON->XML encoder (the "unmirror" direction).
//
// Why a table instead of a generic algorithm: parse_message's to_snake_case()
// (pugixml_generic_mirror.cpp) is lossy -- it cannot be inverted in general.
// Two real examples from this ICD:
//   "SupportsIQ_ConstellationData" -> "supports_iq_constellation_data"
//     Reversing the string alone can't tell you that "iq" should become the
//     acronym "IQ" (not "Iq"), or that the underscore before "constellation"
//     was literally part of the original tag name rather than a
//     to_snake_case-inserted word-boundary marker -- both kinds of
//     underscore look identical once flattened.
//   "ReservedFFPs" -> "reserved_ff_ps"
//     Naive title-casing "ff" gives "Ff", not the correct "FF".
// So every name below is an explicit, ICD-verified fact (transcribed
// straight from CA120_9001_Control_XML_to_JSON.md's paired XML/JSON
// examples, themselves compiled and run against the real parser per that
// doc's own verification method), not a computed guess. A key that reaches
// encode_node() (see ca120_parser.cpp) without an entry here throws -- safe
// failure (encoding just fails, rc=-1) instead of silently producing wrong
// wire bytes for hardware CA120 has never seen.
//
// Coverage: all 27 control-path commands documented in
// CA120_9001_Control_XML_to_JSON.md (ICD §5, R&S-CA120-ICD-V15), plus the
// handful of simpler synthetic shapes exercised only in
// tests/test_frames_ca120.cpp. Extend further tag-by-tag as new payloads
// come up -- each addition should come with a real ICD (or test) example
// proving the mapping, not a speculative guess at ICD vocabulary outside
// what's actually been read.
//
// Known context-sensitivity: a handful of snake_case keys mean a DIFFERENT
// thing (attribute vs. child element, or a different original spelling)
// depending on which tag they appear under -- see
// parent_attribute_overrides() below. "mode" is the one collision found so
// far: DCP's mode="multiChannel" attribute (§5.1.1) vs. Tuner's/
// BitstreamProcessing's <Mode> element (§5.3.2, test_frames_ca120.cpp).
// Global attribute_names()/element_names() below assume "mode" is an
// element (the common case); parent_attribute_overrides()["DCP"] overrides
// that back to an attribute specifically when the enclosing tag is DCP.
// Before adding a new name, check it isn't already present in the other
// role somewhere -- if it collides, it needs a context override too, not a
// second global-table entry.

#include <string>
#include <unordered_map>

namespace ca120_tags {

// Attribute names (global default). Original XML casing differs from the
// snake_case key only for classification_only and list_type.
inline const std::unordered_map<std::string, std::string>& attribute_names() {
    static const std::unordered_map<std::string, std::string> table = {
        {"type",               "type"},
        {"id",                 "id"},
        {"time",               "time"},
        {"source",             "source"},
        {"action",             "action"},
        {"unit",               "unit"},
        {"info",               "info"},
        {"syntax",             "syntax"},
        {"required",           "required"},
        {"datastreams",        "datastreams"},
        {"list_type",          "listType"},
        {"classification_only","classificationOnly"},
    };
    return table;
}

// Per-parent-tag attribute overrides -- checked BEFORE the global
// attribute_names()/element_names() tables. Keyed by the enclosing node's
// ORIGINAL tag name (not snake_case).
inline const std::unordered_map<std::string, std::unordered_map<std::string, std::string>>&
parent_attribute_overrides() {
    static const std::unordered_map<std::string, std::unordered_map<std::string, std::string>> table = {
        // <DCP type="detector" mode="multiChannel"> -- ICD §5.1.1. Elsewhere
        // (Tuner/Parameters, BitstreamProcessing) "mode" is the <Mode> element.
        {"DCP", {{"mode", "mode"}}},
    };
    return table;
}

// Child-element tag names (snake_case -> original PascalCase), covering all
// 27 commands in CA120_9001_Control_XML_to_JSON.md plus
// test_frames_ca120.cpp's simpler synthetic shapes.
inline const std::unordered_map<std::string, std::string>& element_names() {
    static const std::unordered_map<std::string, std::string> table = {
        // 5.1.1 StartApplication (MCP/SEARCH/FH)
        {"resource_manager",          "ResourceManager"},
        {"start_application",         "StartApplication"},
        {"resource_demand_class",     "ResourceDemandClass"},
        {"tuner",                     "Tuner"},
        {"ffp",                       "FFP"},
        {"local_storage",             "LocalStorage"},
        {"ignore_error",              "IgnoreError"},
        {"setup",                     "Setup"},
        {"parameters",                "Parameters"},
        {"coupling",                  "Coupling"},
        {"dcp",                       "DCP"},
        {"channels",                  "Channels"},
        {"detect_and_classify",       "DetectAndClassify"},
        {"change_to_production",      "ChangeToProduction"},
        {"fh_setup",                  "FHSetup"},
        {"synthesizer",               "Synthesizer"},
        {"quantity",                  "Quantity"},
        {"bandwidth",                 "Bandwidth"},
        {"fha",                       "FHA"},
        {"ip",                        "IP"},
        {"port",                      "Port"},
        {"guid",                      "GUID"},
        {"resources",                 "Resources"},

        // 5.1.2 LicenseAllocation
        {"license_allocation",        "LicenseAllocation"},
        {"license",                   "License"},
        {"name",                      "Name"},
        {"total_licenses",            "TotalLicenses"},
        {"available_licenses",        "AvailableLicenses"},

        // 5.1.3 XmlCommunicationDevice Filter Suppress
        {"xml_communication_device",  "XmlCommunicationDevice"},
        {"filter",                    "Filter"},
        {"detector",                  "Detector"},
        {"input",                     "Input"},
        {"all",                       "All"},
        {"advanced_filter",           "AdvancedFilter"},
        {"connection_establishment",  "ConnectionEstablishment"},

        // 5.1.4 / 5.2.5 Control RunningMode
        {"control",                   "Control"},
        {"running_mode",              "RunningMode"},

        // 5.2.1 / 5.2.2 FFT Parameters
        {"fft",                       "FFT"},
        {"fft_length",                "FFT_Length"},
        {"window_type",               "WindowType"},
        {"speed",                     "Speed"},
        {"display_mode",              "DisplayMode"},
        {"average_time",              "AverageTime"},
        {"measurement_time",          "MeasurementTime"},
        {"calculation_mode",          "CalculationMode"},
        {"time_signal_type",          "TimeSignalType"},
        {"frequency_offset",          "FrequencyOffset"},

        // 5.2.3 / 5.2.4 AnalogDemodulator Parameters
        {"analog_demodulator",        "AnalogDemodulator"},
        {"modulation_type",           "ModulationType"},
        {"audio_sample_rate",         "AudioSampleRate"},
        {"bfo_frequency",             "BFO_Frequency"},
        {"squelch_level",             "SquelchLevel"},
        {"squelch_status",            "SquelchStatus"},
        {"gain_control_mode",         "GainControlMode"},
        {"gain_control_value",        "GainControlValue"},

        // 5.2.6 / 5.2.7 Detector Parameters
        {"search_bandwidth",          "SearchBandwidth"},
        {"snr",                       "SNR"},
        {"threshold",                 "Threshold"},
        {"short_time_search_bandwidth","ShortTimeSearchBandwidth"},
        {"short_time_threshold",      "ShortTimeThreshold"},
        {"short_time_snr",            "ShortTimeSNR"},
        {"burst_min_duration",        "BurstMinDuration"},
        {"burst_max_duration",        "BurstMaxDuration"},

        // 5.2.8 DetectAndClassify Parameters
        {"bitstream_classification",  "BitstreamClassification"},
        {"class_progress_rule",       "ClassProgressRule"},
        {"reserved_ff_ps",            "ReservedFFPs"},
        {"scan_enabled",              "ScanEnabled"},
        {"scan_start_frequency",      "ScanStartFrequency"},
        {"scan_stop_frequency",       "ScanStopFrequency"},
        {"ask2_by_morse_replacement", "Ask2ByMorseReplacement"},
        {"decay_time",                "DecayTime"},

        // 5.3.1 / 5.3.2 Tuner Parameters
        {"frequency",                       "Frequency"},
        {"antenna_connector_vuhf_tuner",    "AntennaConnectorVUHF_Tuner"},
        {"antenna_connector_hf_tuner",      "AntennaConnectorHF_Tuner"},
        {"tuner_commutation_frequency",     "TunerCommutationFrequency"},
        {"antenna_control_automatic",       "AntennaControlAutomatic"},
        {"preselection",                    "Preselection"},
        {"attenuation",                     "Attenuation"},
        {"attenuation_hold_time",           "AttenuationHoldTime"},
        {"automatic_gain_control",          "AutomaticGainControl"},
        {"gain_control",                    "GainControl"},
        {"gain_control_time",               "GainControlTime"},
        {"if_panorama_averaging_mode",      "IFPanoramaAveragingMode"},
        {"if_panorama_span",                "IFPanoramaSpan"},
        {"if_panorama_step",                "IFPanoramaStep"},
        {"if_panorama_selectivity",         "IFPanoramaSelectivity"},
        {"pif_panorama_mode",               "PIFPanoramaMode"},
        {"pif_panorama_activity_time",      "PIFPanoramaActivityTime"},
        {"pif_panorama_observation_time",   "PIFPanoramaObservationTime"},
        {"squelch",                         "Squelch"},
        {"scan_step_frequency",             "ScanStepFrequency"},
        {"mode",                            "Mode"},  // default role: element (see DCP override above)
        {"automatic_attenuation",           "AutomaticAttenuation"},

        // 5.3.3 Tuner Capabilities
        {"capabilities",               "Capabilities"},

        // 5.4.1 HopperAutoSeparation
        {"hopper_auto_separation",     "HopperAutoSeparation"},
        {"duration",                   "Duration"},
        {"level",                      "Level"},
        {"symbol_rate",                "SymbolRate"},
        {"timing_separation",          "TimingSeparation"},
        {"record_unnamed",             "RecordUnnamed"},
        {"separation_duration_minimum","SeparationDurationMinimum"},
        {"separation_bursts_minimum",  "SeparationBurstsMinimum"},
        {"profile_list",               "ProfileList"},

        // 5.4.2 HopperFilterSeparation
        {"hopper_filter_separation",   "HopperFilterSeparation"},
        {"smart_processing",           "SmartProcessing"},

        // 5.4.3 FrequencyHopping HistogramParameters
        {"frequency_hopping",          "FrequencyHopping"},
        {"histogram_parameters",       "HistogramParameters"},
        {"signal_duration_range",      "SignalDurationRange"},
        {"frequency_range",            "FrequencyRange"},
        {"bandwidth_range",            "BandwidthRange"},
        {"symbolrate_range",           "SymbolrateRange"},
        {"shift_range",                "ShiftRange"},
        {"modulation_type_status",     "ModulationTypeStatus"},
        {"azimuth_range",              "AzimuthRange"},

        // 5.4.4 HopperFilterSeparation ProcessingStatus
        {"processing_status",          "ProcessingStatus"},
        {"status",                     "Status"},

        // 5.4.5 Enable All ST Processes
        {"hopper_online_recombination","HopperOnlineRecombination"},

        // 5.4.6 QueryHopperProduction
        {"query_hopper_production",    "QueryHopperProduction"},
        {"production_setup",           "ProductionSetup"},
        {"auto_recording",             "AutoRecording"},
        {"profile_names",              "ProfileNames"},
        {"min_coverage",                "MinCoverage"},
        {"radio_id",                   "RadioID"},
        {"enabled",                    "Enabled"},
        {"currently_active",           "CurrentlyActive"},

        // 5.5.1 / 5.5.2 DataStream
        {"data_stream",                "DataStream"},
        {"protocol",                   "Protocol"},

        // 5.6.1 EnableEmissionResults
        {"detector_updates",           "DetectorUpdates"},
        {"pause",                      "Pause"},

        // 5.6.2 EnableClassificationResults
        {"classification_mode",        "ClassificationMode"},

        // 5.6.3 Classifier ProcessingStatus
        {"classifier",                 "Classifier"},

        // 5.6.4 AvailableDemodulators / AvailableDecoders
        {"digital_demodulator",              "DigitalDemodulator"},
        {"bitstream_processing",             "BitstreamProcessing"},
        {"available_demodulators",           "AvailableDemodulators"},
        {"demodulator_info",                 "DemodulatorInfo"},
        {"demodulator_name",                 "DemodulatorName"},
        {"demodulator_version",              "DemodulatorVersion"},
        {"module_id",                        "ModuleID"},
        {"parameter_size",                   "ParameterSize"},
        {"supports_symbol_data",             "SupportsSymbolData"},
        {"supports_iq_constellation_data",   "SupportsIQ_ConstellationData"},
        {"supports_instant_data",            "SupportsInstantData"},
        {"supports_image_data",              "SupportsImageData"},
        {"supports_transmission_data",       "SupportsTransmissionData"},
        {"supports_audio_data",              "SupportsAudioData"},
        {"is_universal",                     "IsUniversal"},
        {"supports_special_data",            "SupportsSpecialData"},
        {"available_decoders",               "AvailableDecoders"},
        {"decoder",                          "Decoder"},
        {"decoder_name",                     "DecoderName"},
    };
    return table;
}

}  // namespace ca120_tags
