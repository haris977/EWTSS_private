// drs-bridge/parsers/dp_ecm/src/hf/parser/mrx_g4_parser.cpp
// MRx Group 4 — Data Acquisition: parse-side decode functions and dispatchers.
#include "sdfc_endian.h"
#include "json_writer.h"
#include "hf_groups.h"
#include "hf_shared.h"
#include <cstring>
#include <cstdio>
#include <string>
using namespace sdfc;

// =============================================================================
// MRx Group 4 decode functions
// =============================================================================

// 4/65 — Start IQ Data Streaming to GO2Monitor Optical command (4 bytes).
// @0 MRX_channel (uint16)  @2 Bandwidth (uint16)
static void decode_mrx_optical_iq_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) return;
    static const char* BW[] = {"10MHz","5MHz","2.5MHz","1MHz","240kHz","120kHz","60kHz",
                                "30kHz","15kHz","6kHz","3kHz","1.5kHz"};
    uint16_t bw = load_u16le(p + 2);
    w.key_uint("mrx_channel",  load_u16le(p + 0));
    w.key_uint("bw_selection", bw);
    w.key_str("bandwidth_name", bw < 12 ? BW[bw] : "unknown");
}

// 4/70 — Read Optical Port Availability Status response (12 bytes).
static void decode_mrx_optical_port_status_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 12) { w.key_str("warning", "optical_port_status < 12 bytes"); return; }
    w.key_uint("port_number",          load_u16le(p + 0));
    w.key_uint("port_id",              load_u16le(p + 2));
    w.key_uint("port_alive_status",    load_u16le(p + 4));
    w.key_str("port_alive",            load_u16le(p + 4) == 1 ? "available" : "unavailable");
    w.key_uint("already_transmitting", load_u16le(p + 6));
    w.key_uint("can_start_transfer",   load_u16le(p + 8));
}

// 4/72 — Read Optical Interface IP Address response (24 bytes).
// @0 IP_Address (4× uint8)  @4 Port_IDs (9× uint16)  @22 Reserved (2B)
static void decode_mrx_optical_ip_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 24) { w.key_str("warning", "optical_ip_rsp < 24 bytes"); return; }
    char ip[32];
    std::snprintf(ip, sizeof(ip), "%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
    w.key_str("ip_address", ip);
    std::string ports = "[";
    for (int i = 0; i < 9; ++i) {
        if (i) ports += ',';
        char tmp[8];
        std::snprintf(tmp, sizeof(tmp), "%u", load_u16le(p + 4 + i * 2));
        ports += tmp;
    }
    ports += "]";
    w.key_raw("port_ids", ports);
}

// 4/5 — Set Threshold command (8 bytes).
static void decode_mrx_set_threshold_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) return;
    w.key_uint("mrx_channel",   load_u16le(p + 0));
    w.key_double("threshold_dbm", static_cast<double>(load_f32le(p + 4)));
}

// 4/8 — Audio Data Acquisition response (per ICD Table 225).
// Message size: 4 + (audio_data_size × 2). Max 262144 samples. 8000Hz, 16-bit unsigned samples.
static void decode_mrx_audio_data_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) { w.key_str("warning", "audio_data_rsp payload < 4 bytes"); return; }
    uint32_t audio_size = load_u32le(p + 0);
    w.key_uint("audio_data_size", audio_size);

    const uint32_t MAX_SAMPLES = 262144;
    uint32_t valid = audio_size < MAX_SAMPLES ? audio_size : MAX_SAMPLES;

    std::string arr = "[";
    for (uint32_t i = 0; i < valid; ++i) {
        int off = 4 + static_cast<int>(i) * 2;
        if (off + 2 > n) break;
        if (i) arr += ',';
        arr += std::to_string(load_u16le(p + off));
    }
    arr += "]";
    w.key_raw("audio_data", arr);
}

// 4/17 — Demodulation and Bandwidth Selection command (8 bytes).
static void decode_mrx_demod_bw_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) return;
    static const char* DEMOD[] = {"cw", "am", "fm", "lsb", "usb"};
    static const char* BW[]    = {"240kHz","120kHz","60kHz","30kHz","15kHz","6kHz","3kHz","1.5kHz"};
    uint16_t demod  = load_u16le(p + 2);
    uint16_t bw     = load_u16le(p + 4);
    uint16_t offset = load_u16le(p + 6);
    w.key_uint("mrx_channel",     load_u16le(p + 0));
    w.key_uint("demod_selection", demod);
    w.key_str("demod_name",       demod < 5 ? DEMOD[demod] : "unknown");
    w.key_uint("bw_selection",    bw);
    w.key_str("bandwidth_name",   bw < 8 ? BW[bw] : "unknown");
    w.key_uint("lsb_usb_offset",  offset);
}

// 4/39 and 4/41 — Configure/Read Memory Scan command (variable).
// Header: mrx_channel(2) + reserved(2) + freq_count(4).
// Per entry (12B): freq_val(double,8) + bw_val(uint16,2) + reserved(uint16,2).
static void decode_mrx_memory_scan_config_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) return;
    w.key_uint("mrx_channel",   load_u16le(p + 0));
    uint32_t count = load_u32le(p + 4);
    w.key_uint("freq_count", count);
    const int ELEM  = 12;
    const uint32_t MAX_ENTRIES = 10000;
    uint32_t valid = count < MAX_ENTRIES ? count : MAX_ENTRIES;
    int off = 8;
    uint32_t emit = 0;
    std::string arr = "[";
    for (uint32_t i = 0; i < valid; ++i) {
        if (off + ELEM > n) break;
        const uint8_t* e = p + off;
        JsonWriter f;
        f.key_double("freq_hz",  load_f64le(e + 0) * 1e6);
        f.key_uint("bw_sel",     load_u16le(e + 8));
        if (emit++) arr += ',';
        arr += f.str();
        off += ELEM;
    }
    arr += "]";
    w.key_raw("frequencies", arr);
    if (count > MAX_ENTRIES) {
        char msg[64];
        std::snprintf(msg, sizeof(msg), "truncated at %u of %u entries", MAX_ENTRIES, count);
        w.key_str("truncated", msg);
    }
}

// 4/42 — Read Memory Scan Data response (per ICD Table 239/240).
// Header: Total Available Count(4) + Scan Speed(2) + Reserved(2).
// S_RES_MEMORY_SCAN_LIST layout (20 bytes per entry, max 10000):
//   @0  Power Level (dBm) (float)  @4  Frequency Value (double)
//   @12 H:M:S:reserved (4B)        @16 Millisecond (uint16)  @18 Bandwidth List (uint16)
static void decode_mrx_memory_scan_data_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "memory_scan_data_rsp payload < 8 bytes"); return; }
    uint32_t count      = load_u32le(p + 0);
    uint16_t scan_speed = load_u16le(p + 4);
    w.key_uint("total_available_count",       count);
    w.key_uint("scan_speed_channels_per_sec", scan_speed);

    const int      ELEM      = 20;
    const uint32_t MAX_SLOTS = 10000;
    uint32_t valid = count < MAX_SLOTS ? count : MAX_SLOTS;

    std::string arr = "[";
    uint32_t emit = 0;
    for (uint32_t i = 0; i < valid; ++i) {
        int off = 8 + static_cast<int>(i) * ELEM;
        if (off + ELEM > n) break;
        const uint8_t* e = p + off;
        JsonWriter f;
        f.key_double("power_dbm", static_cast<double>(load_f32le(e +  0)));
        f.key_double("freq_hz",   load_f64le(e +  4) * 1e6);
        char toa[24];
        std::snprintf(toa, sizeof(toa), "%02u:%02u:%02u.%03u",
                      e[12], e[13], e[14], load_u16le(e + 16));
        f.key_str("toa", toa);
        f.key_uint("bandwidth_list", load_u16le(e + 18));
        if (emit++) arr += ',';
        arr += f.str();
    }
    arr += "]";
    w.key_raw("scan_data", arr);
}

// 4/61 — Read Smart Memory Scan Data command (12 bytes).
static void decode_mrx_smart_scan_read_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 12) return;
    w.key_double("freq_hz",   load_f64le(p + 0) * 1e6);
    w.key_uint("mrx_channel", load_u16le(p + 8));
}

// 4/62 — Read Smart Memory Scan Data response (16 bytes).
static void decode_mrx_smart_scan_read_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) return;
    w.key_double("freq_hz",    load_f64le(p +  0) * 1e6);
    w.key_uint("mrx_channel",  load_u16le(p +  8));
    w.key_double("amplitude",  static_cast<double>(load_f32le(p + 12)));
}

// 4/44 — DDC FFT Data response (fixed: 2+2+4096×4 = 16388 bytes).
static void decode_mrx_ddc_fft_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) return;
    uint16_t bin_count = load_u16le(p + 0);
    w.key_uint("bin_count", bin_count);
    uint32_t emit = bin_count < 4096 ? bin_count : 4096;
    const int HDR = 4;
    if (n < HDR + static_cast<int>(emit) * 4) emit = static_cast<uint32_t>((n - HDR) / 4);
    std::string arr = "[";
    for (uint32_t i = 0; i < emit; ++i) {
        if (i) arr += ',';
        char tmp[32];
        std::snprintf(tmp, sizeof(tmp), "%.2f",
            static_cast<double>(load_f32le(p + HDR + i * 4)));
        arr += tmp;
    }
    arr += "]";
    w.key_raw("ddc_fft_power_dbm", arr);
}

// Shared IQ status response (4 bytes): channel + nb_iq_status + wb_iq_status + reserved.
// Used for 4/34, 4/24, 6/14.
static void decode_mrx_iq_start_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) return;
    w.key_uint("mrx_channel",          p[0]);
    w.key_uint("narrowband_iq_status", p[1]);
    w.key_uint("wideband_iq_status",   p[2]);
    w.key_str("iq_type",               p[2] ? "wideband_10MHz_to_1MHz" : "narrowband_240kHz_and_below");
    //p[3] reserved
}

// 4/23 — Start IQ Data Logging command (20 bytes).
static void decode_mrx_iq_logging_start_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 20) return;
    static const char* BW[] = {
        "10MHz","5MHz","2.5MHz","1MHz","240kHz","120kHz","60kHz",
        "30kHz","15kHz","6kHz","3kHz","1.5kHz"
    };
    uint16_t bw = load_u16le(p + 2);
    w.key_uint("mrx_channel",      load_u16le(p + 0));
    w.key_uint("bw_selection",     bw);
    w.key_str("bandwidth_name",    bw < 12 ? BW[bw] : "unknown");
    w.key_double("center_freq_hz", static_cast<double>(load_f32le(p + 4)) * 1e6);
    w.key_uint("day",    load_u16le(p +  8));
    w.key_uint("month",  load_u16le(p + 10));
    w.key_uint("year",   load_u16le(p + 12));
    w.key_uint("hour",   load_u16le(p + 14));
    w.key_uint("minute", load_u16le(p + 16));
    w.key_uint("second", load_u16le(p + 18));
}

// 4/26 — Stop IQ Data Logging response (132 bytes).
// @0 channel(u16) @2 reserved(u16) @4 file_path(char[128])
static void decode_mrx_iq_logging_stop_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 132) return;
    w.key_uint("mrx_channel", load_u16le(p + 0));
    char path[129];
    std::memcpy(path, p + 4, 128);
    path[128] = '\0';
    w.key_str("file_path", path);
}

// 4/53 — Engage Channel command (12 bytes).
static void decode_mrx_engage_channel_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 12) return;
    w.key_uint("mrx_channel",       load_u16le(p + 0));
    w.key_double("center_freq_hz",  load_f64le(p + 4) * 1e6);
}

// Shared helper: decode DDC FFT command (4 bytes): channel + bw_selection.
static void decode_mrx_ddc_fft_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) return;
    static const char* BW[] = {"240kHz","120kHz","60kHz","30kHz","15kHz","6kHz","3kHz","1.5kHz"};
    uint16_t bw = load_u16le(p + 2);
    w.key_uint("mrx_channel",  load_u16le(p + 0));
    w.key_uint("bw_selection", bw);
    w.key_str("bandwidth_name", bw < 8 ? BW[bw] : "unknown");
}

// Shared helper: decode IQ streaming start command (4 bytes): channel + bw_selection.
static void decode_mrx_iq_start_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) return;
    static const char* BW[] = {
        "1MHz","240kHz","120kHz","60kHz","30kHz","15kHz","6kHz","3kHz","1.5kHz"
    };
    uint16_t bw = load_u16le(p + 2);
    w.key_uint("mrx_channel",  load_u16le(p + 0));
    w.key_uint("bw_selection", bw);
    w.key_str("bandwidth_name", bw < 9 ? BW[bw] : "unknown");
}

// =============================================================================
// MRx Group 4 dispatchers
// =============================================================================

bool mrx_g4_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  5: decode_mrx_set_threshold_cmd(p, n, w);            return true;
        case  7: decode_mrx_channel_cmd(p, n, w);                  return true;
        case  9: decode_mrx_channel_cmd(p, n, w);                  return true;
        case 11: decode_mrx_channel_cmd(p, n, w);                  return true;
        case 15: decode_mrx_channel_cmd(p, n, w);                  return true;
        case 17: decode_mrx_demod_bw_cmd(p, n, w);                 return true;
        case 23: decode_mrx_iq_logging_start_cmd(p, n, w);         return true;
        case 25: decode_mrx_channel_cmd(p, n, w);                  return true;
        case 33: decode_mrx_iq_start_cmd(p, n, w);                 return true;
        case 35: decode_mrx_channel_cmd(p, n, w);                  return true;
        case 39: decode_mrx_memory_scan_config_cmd(p, n, w);       return true;
        case 41: decode_mrx_memory_scan_config_cmd(p, n, w);       return true;
        case 43: decode_mrx_ddc_fft_cmd(p, n, w);                  return true;
        case 53: decode_mrx_engage_channel_cmd(p, n, w);           return true;
        case 55: decode_mrx_channel_cmd(p, n, w);                  return true;
        case 57: decode_mrx_channel_cmd(p, n, w);                  return true;
        case 59: decode_mrx_channel_cmd(p, n, w);                  return true;
        case 61: decode_mrx_smart_scan_read_cmd(p, n, w);          return true;
        case 63: decode_mrx_channel_cmd(p, n, w);                  return true;
        case 65: decode_mrx_optical_iq_cmd(p, n, w);               return true;
        case 67: decode_mrx_channel_cmd(p, n, w);                  return true;
        case 69: decode_mrx_optical_iq_cmd(p, n, w);               return true;
        case 71: decode_mrx_optical_iq_cmd(p, n, w);               return true;
        default: return false;
    }
}

bool mrx_g4_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  6: return true;                                        // Set Threshold ACK
        case  8: decode_mrx_audio_data_rsp(p, n, w);                return true;
        case 10: return true;                                        // Audio Start Play ACK
        case 12: return true;                                        // Audio Stop Play ACK
        case 16: return true;                                        // Audio FIFO Reset ACK
        case 18: return true;                                        // Demod/BW Select ACK
        case 24: decode_mrx_iq_start_rsp(p, n, w);                  return true;
        case 26: decode_mrx_iq_logging_stop_rsp(p, n, w);           return true;
        case 34: decode_mrx_iq_start_rsp(p, n, w);                  return true;
        case 36: return true;                                        // Stop IQ Streaming ACK
        case 40: return true;                                        // Configure Memory Scan ACK
        case 42: decode_mrx_memory_scan_data_rsp(p, n, w);          return true;
        case 44: decode_mrx_ddc_fft_rsp(p, n, w);                   return true;
        case 54: return true;                                        // Engage Channel ACK
        case 56: return true;                                        // Disengage Channel ACK
        case 58: return true;                                        // Stop Memory Scan ACK
        case 60: return true;                                        // Smart Memory Scan Config ACK
        case 62: decode_mrx_smart_scan_read_rsp(p, n, w);           return true;
        case 64: return true;                                        // Stop Smart Memory Scan ACK
        case 66: return true;                                        // Start Optical IQ ACK
        case 68: return true;                                        // Stop Optical IQ ACK
        case 70: decode_mrx_optical_port_status_rsp(p, n, w);       return true;
        case 72: decode_mrx_optical_ip_rsp(p, n, w);                return true;
        default: return false;
    }
}
