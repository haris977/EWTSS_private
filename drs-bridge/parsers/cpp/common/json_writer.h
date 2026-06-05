// parsers/cpp/common/json_writer.h
//
// Minimal stack-based JSON object builder.
// No heap allocations during field writing.
// finish() allocates exactly one char[] on the heap — caller owns it.
//
// Usage:
//   JsonWriter w;
//   w.set("frequency_hz", (int64_t)102400000);
//   w.set("power_dbm", -63.5);
//   const char* json = w.finish();   // heap-allocated
//   // ... use json ...
//   delete[] json;                   // or call free_result()
#pragma once
#include <cstdio>
#include <cstring>
#include <cstdint>

class JsonWriter {
public:
    // 64 KB — sufficient for any single DRS frame including FFT payloads
    static constexpr int BUF_SIZE = 65536;

    JsonWriter() {
        pos_   = buf_;
        *pos_++ = '{';
        first_ = true;
    }

    void set(const char* key, int32_t value) {
        comma(); quote(key); *pos_++ = ':';
        pos_ += std::snprintf(pos_, remaining(), "%d", value);
    }

    void set(const char* key, uint32_t value) {
        comma(); quote(key); *pos_++ = ':';
        pos_ += std::snprintf(pos_, remaining(), "%u", value);
    }

    void set(const char* key, int64_t value) {
        comma(); quote(key); *pos_++ = ':';
        pos_ += std::snprintf(pos_, remaining(), "%lld", static_cast<long long>(value));
    }

    void set(const char* key, uint64_t value) {
        comma(); quote(key); *pos_++ = ':';
        pos_ += std::snprintf(pos_, remaining(), "%llu", static_cast<unsigned long long>(value));
    }

    void set(const char* key, double value) {
        comma(); quote(key); *pos_++ = ':';
        pos_ += std::snprintf(pos_, remaining(), "%.6g", value);
    }

    void set(const char* key, bool value) {
        comma(); quote(key); *pos_++ = ':';
        const char* s = value ? "true" : "false";
        std::memcpy(pos_, s, std::strlen(s));
        pos_ += std::strlen(s);
    }

    void set(const char* key, const char* value) {
        comma(); quote(key); *pos_++ = ':'; quote(value);
    }

    // Returns true if the internal buffer has not overflowed.
    // Always check ok() after building, before calling finish().
    bool ok() const { return pos_ < (buf_ + BUF_SIZE - 64); }

    // Closes the JSON object and returns a heap-allocated copy.
    // Returns nullptr if the buffer overflowed.
    // Caller MUST call delete[] on the returned pointer (or free_result()).
    char* finish() {
        if (!ok()) return nullptr;
        *pos_++ = '}';
        *pos_   = '\0';
        int   len    = static_cast<int>(pos_ - buf_);
        char* result = new char[len + 1];
        std::memcpy(result, buf_, static_cast<std::size_t>(len + 1));
        return result;
    }

private:
    char  buf_[BUF_SIZE];
    char* pos_;
    bool  first_;

    void comma() {
        if (!first_) *pos_++ = ',';
        first_ = false;
    }

    // Writes a quoted, backslash-escaped JSON string key or value.
    void quote(const char* s) {
        *pos_++ = '"';
        while (*s) {
            if (*s == '"' || *s == '\\') *pos_++ = '\\';
            *pos_++ = *s++;
        }
        *pos_++ = '"';
    }

    int remaining() const {
        return static_cast<int>((buf_ + BUF_SIZE) - pos_);
    }
};
