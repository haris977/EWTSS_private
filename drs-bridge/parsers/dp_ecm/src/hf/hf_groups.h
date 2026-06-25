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
