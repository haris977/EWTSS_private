// drs-bridge/parsers/dp_ecm/src/hf/format/g101_format.cpp
// Group 101 — SJC Detection + Jamming: format-side encode functions and dispatcher.
#include "sdfc_endian.h"
#include "json_writer.h"
#include "hf_groups.h"
#include "hf_json_utils.h"
using namespace sdfc;

// ---------------------------------------------------------------------------
// Group 101 encoders
// ---------------------------------------------------------------------------

// Serialize FH Detection response payload for Group 101 / Unit 40.
// S_HOPPER_DATA (40B): @0 hopper_number(u32) @4 min_freq_mhz(f32) @8 max_freq_mhz(f32)
//   @12 pulse_len_ms(f32) @16 inter_hop_ms(f32) @20 detected_count(u32)
//   @24 toa_h(u8) @25 toa_m(u8) @26 toa_s(u8) @27 rsv(u8)
//   @28 power_dbm(f32) @32 freq_active(u16) @34 rsv(u16) @36 snr_db(f32)
// format_response input contract: array key is "detections" for all detection types.
// Scalar TOA fields are integers: toa_h, toa_m, toa_s (not "HH:MM:SS" string).
static int encode_fh_detection(const char* json, uint8_t* buf, int max_len) {
    long long hopper_count_ll = 0;
    if (!json_find_int(json, "hopper_count", hopper_count_ll) || hopper_count_ll <= 0)
        return -1;
    int hc = static_cast<int>(hopper_count_ll);
    if (4 + hc * 40 > max_len) return -1;

    store_u16le(buf + 0, static_cast<uint16_t>(hc));
    store_u16le(buf + 2, 0);

    const char* det = std::strstr(json, "\"detections\"");
    if (!det) return -1;
    const char* arr = std::strchr(det, '[');
    if (!arr) return -1;

    const char* p = arr + 1;
    int written = 0;
    while (written < hc) {
        while (*p && *p != '{' && *p != ']') ++p;
        if (!*p || *p == ']') break;

        const char* obj_start = p;
        int depth = 1; ++p;
        while (*p && depth > 0) {
            if (*p == '{') ++depth;
            else if (*p == '}') --depth;
            ++p;
        }
        std::string entry(obj_start, p);
        const char* e = entry.c_str();

        long long hopper_number = 0, detected_count = 0, toa_h = 0, toa_m = 0, toa_s = 0, freq_active = 0;
        double min_freq_hz = 0, max_freq_hz = 0, pulse_length_s = 0, inter_hop_period_s = 0, power_dbm = 0, snr_db = 0;

        json_find_int(e,    "hopper_number",      hopper_number);
        json_find_int(e,    "detected_count",     detected_count);
        json_find_int(e,    "toa_h",              toa_h);
        json_find_int(e,    "toa_m",              toa_m);
        json_find_int(e,    "toa_s",              toa_s);
        json_find_int(e,    "freq_active",        freq_active);
        json_find_double(e, "min_freq_hz",        min_freq_hz);
        json_find_double(e, "max_freq_hz",        max_freq_hz);
        json_find_double(e, "pulse_length_s",     pulse_length_s);
        json_find_double(e, "inter_hop_period_s", inter_hop_period_s);
        json_find_double(e, "power_dbm",          power_dbm);
        json_find_double(e, "snr_db",             snr_db);

        uint8_t* slot = buf + 4 + written * 40;
        store_u32le(slot +  0, static_cast<uint32_t>(hopper_number));
        store_f32le(slot +  4, static_cast<float>(min_freq_hz / 1e6));
        store_f32le(slot +  8, static_cast<float>(max_freq_hz / 1e6));
        store_f32le(slot + 12, static_cast<float>(pulse_length_s * 1e3));
        store_f32le(slot + 16, static_cast<float>(inter_hop_period_s * 1e3));
        store_u32le(slot + 20, static_cast<uint32_t>(detected_count));
        slot[24] = static_cast<uint8_t>(toa_h);
        slot[25] = static_cast<uint8_t>(toa_m);
        slot[26] = static_cast<uint8_t>(toa_s);
        slot[27] = 0;
        store_f32le(slot + 28, static_cast<float>(power_dbm));
        store_u16le(slot + 32, static_cast<uint16_t>(freq_active));
        store_u16le(slot + 34, 0);
        store_f32le(slot + 36, static_cast<float>(snr_db));
        ++written;
    }

    return 4 + written * 40;
}

// Serialize FF Detection response payload for Group 101 / Unit 70.
// Wire: S_RES_WIDEBAND_FFT_DATA (6408B, zeroed) + ff_count (u32) + S_DETECTED_FIXED_FREQUENCY × N (36B each).
// S_DETECTED_FIXED_FREQUENCY (36B):
//   @0  freq (f32,MHz)  @4  current_power_dbm (f32)  @8  active_count (u32)
//   @12 min_power_dbm (f32)  @16 max_power_dbm (f32)
//   @20 toa H:M:S:0 (4B)  @24 dur H:M:S:0 (4B)
//   @28 freq_active (u16)+rsv (u16)  @32 snr_db (f32)
static int encode_ff_detection(const char* json, uint8_t* buf, int max_len) {
    const int FFT_BLOCK = 4 + 1600 * 4 + 4;
    const int FF_ELEM   = 36;

    long long ff_count_ll = 0;
    if (!json_find_int(json, "ff_count", ff_count_ll) || ff_count_ll < 0)
        return -1;
    int fc = static_cast<int>(ff_count_ll);
    if (FFT_BLOCK + 4 + fc * FF_ELEM > max_len) return -1;

    std::memset(buf, 0, static_cast<size_t>(FFT_BLOCK));
    store_u32le(buf + FFT_BLOCK, static_cast<uint32_t>(fc));

    if (fc == 0) return FFT_BLOCK + 4;

    const char* det = std::strstr(json, "\"detections\"");
    if (!det) return FFT_BLOCK + 4;
    const char* arr = std::strchr(det, '[');
    if (!arr) return FFT_BLOCK + 4;

    const char* p = arr + 1;
    int written = 0;
    while (written < fc) {
        while (*p && *p != '{' && *p != ']') ++p;
        if (!*p || *p == ']') break;
        const char* obj_start = p;
        int depth = 1; ++p;
        while (*p && depth > 0) {
            if (*p == '{') ++depth; else if (*p == '}') --depth; ++p;
        }
        std::string entry(obj_start, p);
        const char* e = entry.c_str();

        long long active_count = 0, toa_h = 0, toa_m = 0, toa_s = 0;
        long long dur_h = 0, dur_m = 0, dur_s = 0, freq_active = 0;
        double freq_hz = 0, current_power_dbm = 0, min_power_dbm = 0;
        double max_power_dbm = 0, snr_db = 0;

        json_find_double(e, "freq_hz",           freq_hz);
        json_find_double(e, "current_power_dbm", current_power_dbm);
        json_find_int(e,    "active_count",      active_count);
        json_find_double(e, "min_power_dbm",     min_power_dbm);
        json_find_double(e, "max_power_dbm",     max_power_dbm);
        json_find_int(e,    "toa_h",             toa_h);
        json_find_int(e,    "toa_m",             toa_m);
        json_find_int(e,    "toa_s",             toa_s);
        json_find_int(e,    "duration_h",        dur_h);
        json_find_int(e,    "duration_m",        dur_m);
        json_find_int(e,    "duration_s",        dur_s);
        json_find_int(e,    "freq_active",       freq_active);
        json_find_double(e, "snr_db",            snr_db);

        uint8_t* slot = buf + FFT_BLOCK + 4 + written * FF_ELEM;
        store_f32le(slot +  0, static_cast<float>(freq_hz / 1e6));
        store_f32le(slot +  4, static_cast<float>(current_power_dbm));
        store_u32le(slot +  8, static_cast<uint32_t>(active_count));
        store_f32le(slot + 12, static_cast<float>(min_power_dbm));
        store_f32le(slot + 16, static_cast<float>(max_power_dbm));
        slot[20] = static_cast<uint8_t>(toa_h);
        slot[21] = static_cast<uint8_t>(toa_m);
        slot[22] = static_cast<uint8_t>(toa_s);
        slot[23] = 0;
        slot[24] = static_cast<uint8_t>(dur_h);
        slot[25] = static_cast<uint8_t>(dur_m);
        slot[26] = static_cast<uint8_t>(dur_s);
        slot[27] = 0;
        store_u16le(slot + 28, static_cast<uint16_t>(freq_active));
        store_u16le(slot + 30, 0);
        store_f32le(slot + 32, static_cast<float>(snr_db));
        ++written;
    }
    return FFT_BLOCK + 4 + written * FF_ELEM;
}

// Serialize Burst Detection response payload for Group 101 / Unit 84.
// Wire: burst_count (u32) + S_DETECTED_BURST_FREQUENCY × N (24B each).
// S_DETECTED_BURST_FREQUENCY (24B):
//   @0  freq (f32,MHz)  @4  current_power_dbm (f32)  @8  pulse_length_ms (f32)
//   @12 active_count (u32)  @16 toa H:M:S:0 (4B)  @20 snr_db (f32)
static int encode_burst_detection(const char* json, uint8_t* buf, int max_len) {
    const int ELEM = 24;

    long long burst_count_ll = 0;
    if (!json_find_int(json, "burst_count", burst_count_ll) || burst_count_ll < 0)
        return -1;
    int bc = static_cast<int>(burst_count_ll);
    if (4 + bc * ELEM > max_len) return -1;

    store_u32le(buf, static_cast<uint32_t>(bc));

    if (bc == 0) return 4;

    const char* det = std::strstr(json, "\"detections\"");
    if (!det) return 4;
    const char* arr = std::strchr(det, '[');
    if (!arr) return 4;

    const char* p = arr + 1;
    int written = 0;
    while (written < bc) {
        while (*p && *p != '{' && *p != ']') ++p;
        if (!*p || *p == ']') break;
        const char* obj_start = p;
        int depth = 1; ++p;
        while (*p && depth > 0) {
            if (*p == '{') ++depth; else if (*p == '}') --depth; ++p;
        }
        std::string entry(obj_start, p);
        const char* e = entry.c_str();

        long long active_count = 0, toa_h = 0, toa_m = 0, toa_s = 0;
        double freq_hz = 0, current_power_dbm = 0, pulse_length_ms = 0, snr_db = 0;

        json_find_double(e, "freq_hz",           freq_hz);
        json_find_double(e, "current_power_dbm", current_power_dbm);
        json_find_double(e, "pulse_length_ms",   pulse_length_ms);
        json_find_int(e,    "active_count",      active_count);
        json_find_int(e,    "toa_h",             toa_h);
        json_find_int(e,    "toa_m",             toa_m);
        json_find_int(e,    "toa_s",             toa_s);
        json_find_double(e, "snr_db",            snr_db);

        uint8_t* slot = buf + 4 + written * ELEM;
        store_f32le(slot +  0, static_cast<float>(freq_hz / 1e6));
        store_f32le(slot +  4, static_cast<float>(current_power_dbm));
        store_f32le(slot +  8, static_cast<float>(pulse_length_ms));
        store_u32le(slot + 12, static_cast<uint32_t>(active_count));
        slot[16] = static_cast<uint8_t>(toa_h);
        slot[17] = static_cast<uint8_t>(toa_m);
        slot[18] = static_cast<uint8_t>(toa_s);
        slot[19] = 0;
        store_f32le(slot + 20, static_cast<float>(snr_db));
        ++written;
    }
    return 4 + written * ELEM;
}

static int encode_wideband_fft(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 6408) return -1;
    std::memset(buf, 0, 6408);
    long long bin_count = 0, scan_speed = 0;
    json_find_int(j, "fft_bin_count", bin_count); json_find_int(j, "scan_speed", scan_speed);
    store_u32le(buf + 0, (uint32_t)bin_count); store_u32le(buf + 6404, (uint32_t)scan_speed);
    return 6408;
}

static int encode_stop_scan_speed(const char* j, uint8_t* buf, int max_len) {
    const int TOTAL = 4 + 1600 * 36;
    if (max_len < TOTAL) return -1;
    std::memset(buf, 0, (size_t)TOTAL);
    long long ff_count = 0;
    json_find_int(j, "ff_count", ff_count);
    if (ff_count > 1600) ff_count = 1600;
    store_u32le(buf, (uint32_t)ff_count);
    if (ff_count == 0) return TOTAL;
    const char* det = std::strstr(j, "\"detections\"");
    if (!det) return TOTAL;
    const char* arr = std::strchr(det, '[');
    if (!arr) return TOTAL;
    const char* p = arr + 1; int written = 0;
    while (written < (int)ff_count) {
        while (*p && *p != '{' && *p != ']') ++p;
        if (!*p || *p == ']') break;
        const char* os = p; int depth = 1; ++p;
        while (*p && depth > 0) { if (*p == '{') ++depth; else if (*p == '}') --depth; ++p; }
        std::string entry(os, p); const char* e = entry.c_str();
        long long ac = 0, fa = 0;
        double fhz = 0, cp = 0, mnp = 0, mxp = 0, snr = 0;
        json_find_double(e, "freq_hz",           fhz); json_find_double(e, "current_power_dbm", cp);
        json_find_int(e,    "active_count",      ac);  json_find_double(e, "min_power_dbm",     mnp);
        json_find_double(e, "max_power_dbm",     mxp); json_find_int(e,    "freq_active",       fa);
        json_find_double(e, "snr_db",            snr);
        uint8_t th = 0, tm = 0, ts = 0, dh = 0, dm = 0, ds = 0;
        parse_toa_hms(e, "toa",      th, tm, ts);
        parse_toa_hms(e, "duration", dh, dm, ds);
        uint8_t* slot = buf + 4 + written * 36;
        store_f32le(slot +  0, (float)(fhz / 1e6)); store_f32le(slot +  4, (float)cp);
        store_u32le(slot +  8, (uint32_t)ac);        store_f32le(slot + 12, (float)mnp);
        store_f32le(slot + 16, (float)mxp);
        slot[20] = th; slot[21] = tm; slot[22] = ts; slot[23] = 0;
        slot[24] = dh; slot[25] = dm; slot[26] = ds; slot[27] = 0;
        store_u16le(slot + 28, (uint16_t)fa); store_u16le(slot + 30, 0);
        store_f32le(slot + 32, (float)snr);
        ++written;
    }
    return TOTAL;
}

static int encode_zoom_fft(const char* j, uint8_t* buf, int max_len) {
    long long sc = 0; json_find_int(j, "sample_count", sc);
    if (sc < 0 || sc > 1600) sc = 0;
    int total = 4 + (int)sc * 4;
    if (total > max_len) return -1;
    std::memset(buf, 0, (size_t)total);
    store_u32le(buf, (uint32_t)sc);
    if (sc == 0) return 4;
    const char* pd = std::strstr(j, "\"power_dbm\"");
    if (!pd) return total;
    const char* arr = std::strchr(pd, '['); if (!arr) return total; ++arr;
    int written = 0;
    while (written < (int)sc) {
        while (*arr == ' ' || *arr == '\t' || *arr == '\n' || *arr == ',') ++arr;
        if (*arr == ']' || !*arr) break;
        char* end; float v = (float)std::strtod(arr, &end);
        if (end == arr) break;
        store_f32le(buf + 4 + written * 4, v); arr = end; ++written;
    }
    return total;
}

// ---------------------------------------------------------------------------
// Group 101 format dispatcher
// ---------------------------------------------------------------------------
int g101_format(long long unit_id, const char* json, uint8_t* buf, int max_len, bool& is_ack) {
    typedef int (*EncFn)(const char*, uint8_t*, int);
    EncFn fn = nullptr;
    switch (unit_id) {
        case  26: is_ack = true; break; // Set Threshold ACK
        case  28: is_ack = true; break; // Set Resolution ACK
        case  32: is_ack = true; break; // Start Follow-on Jam ACK
        case  34: is_ack = true; break; // Stop Follow-on Jam ACK
        case  38: is_ack = true; break; // Configure Detection ACK
        case  40: fn = encode_fh_detection;   break;
        case  44: fn = encode_wideband_fft;   break;
        case  48: is_ack = true; break; // Set Pulse Range ACK
        case  56: is_ack = true; break; // Set Min Hops ACK
        case  64: is_ack = true; break; // Tracking Config ACK
        case  70: fn = encode_ff_detection;   break;
        case  74: is_ack = true; break; // Start List Jam ACK
        case  76: is_ack = true; break; // Stop List Jam ACK
        case  80: is_ack = true; break; // Send ECM Reports ACK
        case  84: fn = encode_burst_detection; break;
        case  86: is_ack = true; break; // Start Scan Speed ACK
        case  88: fn = encode_stop_scan_speed; break;
        case  93: is_ack = true; break; // Start Responsive Sweep Jam ACK
        case  95: fn = encode_zoom_fft;        break;
        case 101: is_ack = true; break; // Set Flatness Mode ACK
        case 103: is_ack = true; break; // Set Integration Time ACK
        case 105: is_ack = true; break; // Set Multi-Band FH ACK
        case 107: is_ack = true; break; // Set Narrow-Band FH ACK
        case 141: is_ack = true; break; // HF-specific ACK
        case 159: is_ack = true; break; // Terminate FFT ACK
        case 161: case 163: case 165:
        case 175: case 177: case 179: is_ack = true; break; // HF slow scan ACKs
        case 183: case 185: case 187: is_ack = true; break; // HF fast scan ACKs
        case 201: case 203: is_ack = true; break; // HF wideband ACKs
        default: return 0;
    }
    if (is_ack) return 0;
    return fn(json, buf, max_len);
}
