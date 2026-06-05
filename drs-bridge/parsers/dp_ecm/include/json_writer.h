// drs-bridge/parsers/dp_ecm/include/json_writer.h
//
// Minimal, dependency-free JSON object builder (header-only).
// Builds a flat/nested JSON string with correct escaping and number formatting.
// Internal helper only — never crosses the ABI as a C++ type.
//
// NOTE: for full JSON *parsing* on the encode path we recommend vendoring
// nlohmann/json (MIT, header-only, no LGPL) in a later step. This writer covers
// the decode (emit) path with zero external dependencies.
#ifndef SDFC_JSON_WRITER_H
#define SDFC_JSON_WRITER_H

#include <string>
#include <cstdio>
#include <cstdint>

namespace sdfc {

class JsonWriter {
public:
    JsonWriter() { buf_ += '{'; }

    void key_str(const char* k, const std::string& v) {
        sep(); quote(k); buf_ += ':'; quote(v);
    }
    void key_int(const char* k, long long v) {
        sep(); quote(k); buf_ += ':'; buf_ += std::to_string(v);
    }
    void key_uint(const char* k, unsigned long long v) {
        sep(); quote(k); buf_ += ':'; buf_ += std::to_string(v);
    }
    void key_double(const char* k, double v) {
        sep(); quote(k); buf_ += ':';
        char tmp[64];
        std::snprintf(tmp, sizeof(tmp), "%.6g", v);
        buf_ += tmp;
    }
    void key_bool(const char* k, bool v) {
        sep(); quote(k); buf_ += ':'; buf_ += (v ? "true" : "false");
    }
    // Begin a raw nested value (caller supplies a valid JSON fragment).
    void key_raw(const char* k, const std::string& json_fragment) {
        sep(); quote(k); buf_ += ':'; buf_ += json_fragment;
    }

    // Finalize and return the JSON text.
    std::string str() {
        std::string out = buf_;
        out += '}';
        return out;
    }

private:
    void sep() { if (need_comma_) buf_ += ','; need_comma_ = true; }
    void quote(const std::string& s) {
        buf_ += '"';
        for (char c : s) {
            switch (c) {
                case '"':  buf_ += "\\\""; break;
                case '\\': buf_ += "\\\\"; break;
                case '\n': buf_ += "\\n";  break;
                case '\r': buf_ += "\\r";  break;
                case '\t': buf_ += "\\t";  break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        char u[8];
                        std::snprintf(u, sizeof(u), "\\u%04x", c);
                        buf_ += u;
                    } else {
                        buf_ += c;
                    }
            }
        }
        buf_ += '"';
    }

    std::string buf_;
    bool need_comma_ = false;
};

// Convert a byte range to a lowercase hex string.
inline std::string to_hex(const uint8_t* p, int n) {
    static const char* H = "0123456789abcdef";
    std::string s;
    s.reserve(static_cast<size_t>(n) * 2);
    for (int i = 0; i < n; ++i) {
        s += H[(p[i] >> 4) & 0xF];
        s += H[p[i] & 0xF];
    }
    return s;
}

} // namespace sdfc

#endif // SDFC_JSON_WRITER_H
