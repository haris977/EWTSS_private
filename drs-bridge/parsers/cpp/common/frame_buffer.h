// parsers/cpp/common/frame_buffer.h
//
// Safe multi-byte reads and writes on raw byte buffers.
// All DRS hardware protocols on this platform are little-endian.
// All functions are inline — zero call overhead in release builds.
#pragma once
#include <cstdint>
#include <cstring>

// ── Read helpers ──────────────────────────────────────────────────────────────

inline uint8_t  read_u8 (const uint8_t* p) { return *p; }

inline uint16_t read_u16(const uint8_t* p) {
    uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}

inline uint32_t read_u32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

inline int16_t read_i16(const uint8_t* p) {
    int16_t v;
    std::memcpy(&v, p, 2);
    return v;
}

inline int32_t read_i32(const uint8_t* p) {
    int32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

inline float read_f32(const uint8_t* p) {
    float v;
    std::memcpy(&v, p, 4);
    return v;
}

inline double read_f64(const uint8_t* p) {
    double v;
    std::memcpy(&v, p, 8);
    return v;
}

// ── Write helpers ─────────────────────────────────────────────────────────────

inline void write_u8 (uint8_t* p, uint8_t  v) { *p = v; }

inline void write_u16(uint8_t* p, uint16_t v) { std::memcpy(p, &v, 2); }

inline void write_u32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }

inline void write_i16(uint8_t* p, int16_t  v) { std::memcpy(p, &v, 2); }

inline void write_f32(uint8_t* p, float    v) { std::memcpy(p, &v, 4); }

// ── Magic byte matching ───────────────────────────────────────────────────────

// Returns true if buf[0..n) matches magic[0..n) exactly.
inline bool match_magic(const uint8_t* buf, const uint8_t* magic, int n) {
    return std::memcmp(buf, magic, static_cast<std::size_t>(n)) == 0;
}
