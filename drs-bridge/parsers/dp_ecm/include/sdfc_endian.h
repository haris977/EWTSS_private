// drs-bridge/parsers/dp_ecm/include/sdfc_endian.h
//
// Explicit little-endian read/write helpers for the SDFC<->DRS family.
//
// WHY THIS EXISTS: never cast a struct over wire bytes. Struct layout depends on
// compiler padding/alignment and host endianness; reading field-by-field at
// explicit offsets is portable and correct. The Data Patterns ICDs do not state
// endianness; we treat the wire as little-endian (x86 host) — confirm against a
// live capture before trusting float/double fields (open question).
#ifndef SDFC_ENDIAN_H
#define SDFC_ENDIAN_H

#include <cstdint>
#include <cstring>

namespace sdfc {

// ---- Little-endian reads (bounds are the caller's responsibility) ----
inline uint16_t load_u16le(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) |
           (static_cast<uint16_t>(p[1]) << 8);
}

// ---- Big-endian reads (network byte order) ----
// Confirmed against a live ECS capture (2026-07-01): the CMD/RESP frame header
// fields (payload_size, group_id, unit_id, status) are big-endian on the wire.
// Updated 2026-07-06: CMD payload fields are now also read big-endian, extending
// the pattern confirmed for the Set Date/Time field (year decoded as 2026 under
// BE vs. 59911 under LE against a live capture) to the rest of the CMD payload
// decoders. See dp_ecm/README.md for the capture evidence.
inline uint16_t load_u16be(const uint8_t* p) {
    return static_cast<uint16_t>(p[1]) |
           (static_cast<uint16_t>(p[0]) << 8);
}

inline int16_t load_i16be(const uint8_t* p) {
    return static_cast<int16_t>(load_u16be(p));
}

inline uint32_t load_u32be(const uint8_t* p) {
    return  static_cast<uint32_t>(p[3])        |
           (static_cast<uint32_t>(p[2]) << 8)  |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[0]) << 24);
}

inline int32_t load_i32be(const uint8_t* p) {
    return static_cast<int32_t>(load_u32be(p));
}

inline uint64_t load_u64be(const uint8_t* p) {
    return (static_cast<uint64_t>(load_u32be(p)) << 32) |
            static_cast<uint64_t>(load_u32be(p + 4));
}

inline float load_f32be(const uint8_t* p) {
    uint32_t bits = load_u32be(p);
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

inline double load_f64be(const uint8_t* p) {
    uint64_t bits = load_u64be(p);
    double d;
    std::memcpy(&d, &bits, sizeof(d));
    return d;
}

inline int16_t load_i16le(const uint8_t* p) {
    return static_cast<int16_t>(load_u16le(p));
}

inline uint32_t load_u32le(const uint8_t* p) {
    return  static_cast<uint32_t>(p[0])        |
           (static_cast<uint32_t>(p[1]) << 8)  |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline int32_t load_i32le(const uint8_t* p) {
    return static_cast<int32_t>(load_u32le(p));
}

inline uint64_t load_u64le(const uint8_t* p) {
    return  static_cast<uint64_t>(load_u32le(p)) |
           (static_cast<uint64_t>(load_u32le(p + 4)) << 32);
}

inline float load_f32le(const uint8_t* p) {
    uint32_t bits = load_u32le(p);
    float f;
    std::memcpy(&f, &bits, sizeof(f));   // type-pun safely
    return f;
}

inline double load_f64le(const uint8_t* p) {
    uint64_t bits = load_u64le(p);
    double d;
    std::memcpy(&d, &bits, sizeof(d));
    return d;
}

// ---- Little-endian writes ----
inline void store_u16le(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

inline void store_i16le(uint8_t* p, int16_t v) {
    store_u16le(p, static_cast<uint16_t>(v));
}

inline void store_u32le(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

// ---- Big-endian writes (network byte order) ----
// Payload fields going out to ECS also need big-endian: a client test against
// real ECS showed garbage/negative values decoded from our little-endian
// payload floats once the header switched to big-endian. This only affects
// the encode/write direction (RESP payload we send) — the decode/read side
// (CMD payload we receive) is intentionally left on load_*le pending its own
// capture-based confirmation. Do not use these for reading inbound frames.
inline void store_u16be(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[1] = static_cast<uint8_t>(v & 0xFF);
}

inline void store_i16be(uint8_t* p, int16_t v) {
    store_u16be(p, static_cast<uint16_t>(v));
}

inline void store_u32be(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>((v >> 24) & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[3] = static_cast<uint8_t>(v & 0xFF);
}

inline void store_i32be(uint8_t* p, int32_t v) {
    store_u32be(p, static_cast<uint32_t>(v));
}

inline void store_u64be(uint8_t* p, uint64_t v) {
    store_u32be(p,     static_cast<uint32_t>(v >> 32));
    store_u32be(p + 4, static_cast<uint32_t>(v & 0xFFFFFFFFu));
}

inline void store_f32be(uint8_t* p, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    store_u32be(p, bits);
}

inline void store_f64be(uint8_t* p, double v) {
    uint64_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    store_u64be(p, bits);
}

inline void store_i32le(uint8_t* p, int32_t v) {
    store_u32le(p, static_cast<uint32_t>(v));
}

inline void store_u64le(uint8_t* p, uint64_t v) {
    store_u32le(p,     static_cast<uint32_t>(v & 0xFFFFFFFFu));
    store_u32le(p + 4, static_cast<uint32_t>(v >> 32));
}

inline void store_f32le(uint8_t* p, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    store_u32le(p, bits);
}

inline void store_f64le(uint8_t* p, double v) {
    uint64_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    store_u64le(p, bits);
}

} // namespace sdfc

#endif // SDFC_ENDIAN_H
