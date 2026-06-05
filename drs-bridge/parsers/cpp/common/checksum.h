// parsers/cpp/common/checksum.h
//
// CRC and checksum algorithms used by DRS hardware protocols.
// Each hardware variant specifies which algorithm it uses in its ICD.
// Confirm algorithm + polynomial per variant before using — it is NOT in the
// ICD Excel sheets (you must get it from the hardware spec or customer).
#pragma once
#include <cstdint>

// ── CRC-16/CCITT-FALSE ────────────────────────────────────────────────────────
// Polynomial: 0x1021, Init: 0xFFFF, RefIn: false, RefOut: false, XorOut: 0x0000
// Used by: comm_df, jvuhf, jhf (verify per variant)
inline uint16_t crc16_ccitt(const uint8_t* data, int len, uint16_t init = 0xFFFF) {
    uint16_t crc = init;
    for (int i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000u) ? ((crc << 1) ^ 0x1021u) : (crc << 1);
    }
    return crc;
}

// ── CRC-32 (IEEE 802.3) ───────────────────────────────────────────────────────
// Polynomial: 0x04C11DB7, standard Ethernet CRC
// Used by: some larger DRS variants with FFT payloads (verify per variant)
inline uint32_t crc32_ieee(const uint8_t* data, int len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (int i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
    }
    return ~crc;
}

// ── XOR checksum ─────────────────────────────────────────────────────────────
// Simple byte XOR over the payload range.
// Used by: SCD compact frames and some older hardware variants
inline uint8_t xor_checksum(const uint8_t* data, int len) {
    uint8_t sum = 0;
    for (int i = 0; i < len; ++i)
        sum ^= data[i];
    return sum;
}

// ── Additive sum checksum ─────────────────────────────────────────────────────
// Truncated to 16 bits. Used by a small number of DRS variants.
inline uint16_t sum16_checksum(const uint8_t* data, int len) {
    uint32_t sum = 0;
    for (int i = 0; i < len; ++i)
        sum += data[i];
    return static_cast<uint16_t>(sum & 0xFFFFu);
}
