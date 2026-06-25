// drs-bridge/parsers/dp_ecm/src/hf/hf_json_utils.h
// Lightweight JSON field-extraction helpers — inline, no external dependencies.
// Moved from dp_ecm_hf_parser.cpp (originally static); consumed by all hf/ group files.
#pragma once
#include <cstring>
#include <cstdlib>
#include <string>
#include <cstdio>
#include <cstdint>

inline bool json_find_int(const char* json, const char* key, long long& out) {
    std::string pat = std::string("\"") + key + "\"";
    const char* k = std::strstr(json, pat.c_str());
    if (!k) return false;
    const char* c = std::strchr(k, ':');
    if (!c) return false;
    ++c;
    while (*c == ' ' || *c == '\t') ++c;
    char* end = nullptr;
    long long v = std::strtoll(c, &end, 10);
    if (end == c) return false;
    out = v;
    return true;
}

inline bool json_find_double(const char* json, const char* key, double& out) {
    std::string pat = std::string("\"") + key + "\"";
    const char* k = std::strstr(json, pat.c_str());
    if (!k) return false;
    const char* c = std::strchr(k + pat.size(), ':');
    if (!c) return false;
    ++c;
    while (*c == ' ' || *c == '\t') ++c;
    char* end = nullptr;
    out = std::strtod(c, &end);
    return end != c;
}

inline int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Helper: parse "HH:MM:SS" toa string OR individual _h/_m/_s integer fields.
inline void parse_toa_hms(const char* j, const char* key,
                           uint8_t& h_out, uint8_t& m_out, uint8_t& s_out) {
    h_out = 0; m_out = 0; s_out = 0;
    char sub[64];
    long long v;
    std::snprintf(sub, sizeof(sub), "%s_h", key); if (json_find_int(j, sub, v)) h_out = (uint8_t)v;
    std::snprintf(sub, sizeof(sub), "%s_m", key); if (json_find_int(j, sub, v)) m_out = (uint8_t)v;
    std::snprintf(sub, sizeof(sub), "%s_s", key); if (json_find_int(j, sub, v)) s_out = (uint8_t)v;
    if (h_out || m_out || s_out) return;
    std::string pat = std::string("\"") + key + "\"";
    const char* k = std::strstr(j, pat.c_str());
    if (!k) return;
    const char* c = std::strchr(k + pat.size(), ':');
    if (!c) return; ++c;
    while (*c == ' ' || *c == '\t') ++c;
    if (*c != '"') return; ++c;
    unsigned hh = 0, mm = 0, ss = 0;
    if (std::sscanf(c, "%u:%u:%u", &hh, &mm, &ss) == 3) {
        h_out = (uint8_t)hh; m_out = (uint8_t)mm; s_out = (uint8_t)ss;
    }
}
