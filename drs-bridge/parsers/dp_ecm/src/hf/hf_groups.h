// drs-bridge/parsers/dp_ecm/src/hf/hf_groups.h
// Dispatcher declarations — one parse_cmd, parse_rsp, format per group.
// Populated incrementally by Tasks 2–12.
#pragma once
#include <cstdint>
#include "json_writer.h"
using namespace sdfc;

// Group 100 — SJC Diagnostics
bool g100_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
bool g100_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
int  g100_format   (long long unit_id, const char* json,
                    uint8_t* buf, int max_len, bool& is_ack);

// Group 101 — SJC Detection + Jamming
bool g101_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
bool g101_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
int  g101_format   (long long unit_id, const char* json,
                    uint8_t* buf, int max_len, bool& is_ack);

// Group 106 — ECM Immediate Jamming
bool g106_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
bool g106_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
int  g106_format   (long long unit_id, const char* json,
                    uint8_t* buf, int max_len, bool& is_ack);

// Group 109 — Date/Time
bool g109_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
bool g109_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
int  g109_format   (long long unit_id, const char* json,
                    uint8_t* buf, int max_len, bool& is_ack);

// Group 111 — Signal/Scan
bool g111_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
bool g111_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
int  g111_format   (long long unit_id, const char* json,
                    uint8_t* buf, int max_len, bool& is_ack);

// Group 112 — Fast Scan / Simulation
bool g112_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
bool g112_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
int  g112_format   (long long unit_id, const char* json,
                    uint8_t* buf, int max_len, bool& is_ack);

// Group 200 — HF ECM Jamming
bool g200_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
bool g200_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
int  g200_format   (long long unit_id, const char* json,
                    uint8_t* buf, int max_len, bool& is_ack);

// MRx Group 1 — Diagnostics
bool mrx_g1_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
bool mrx_g1_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
int  mrx_g1_format   (long long unit_id, const char* json,
                      uint8_t* buf, int max_len, bool& is_ack);

// MRx Group 3 — RF Board / Channel Management
bool mrx_g3_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
bool mrx_g3_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
int  mrx_g3_format   (long long unit_id, const char* json,
                      uint8_t* buf, int max_len, bool& is_ack);

// MRx Group 4 — Data Acquisition
bool mrx_g4_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
bool mrx_g4_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
int  mrx_g4_format   (long long unit_id, const char* json,
                      uint8_t* buf, int max_len, bool& is_ack);

// MRx Group 5 — Tuner
bool mrx_g5_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
bool mrx_g5_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
int  mrx_g5_format   (long long unit_id, const char* json,
                      uint8_t* buf, int max_len, bool& is_ack);

// MRx Group 6 — FH Monitoring / GO2Monitor
bool mrx_g6_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
bool mrx_g6_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
int  mrx_g6_format   (long long unit_id, const char* json,
                      uint8_t* buf, int max_len, bool& is_ack);

// MRx Group 7 — Signal BITE / misc
bool mrx_g7_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
bool mrx_g7_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
int  mrx_g7_format   (long long unit_id, const char* json,
                      uint8_t* buf, int max_len, bool& is_ack);
