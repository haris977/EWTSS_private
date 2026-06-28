// drs-bridge/parsers/dp_ecm/src/dp_ecm_hf_parser.cpp
//
// DP-ECM-1071 HF Receiver/Processor/Exciter parser DLL.
// Protocol doc: DP-ECM-1074-6000-V1-ICD-0V04.
// Implements the 4-symbol ABI (sdfc_abi.h) on top of the shared frame core.
//
// Full scope:
//   Group 100 (SJC diagnostics):
//     100/2  — Get System Version     (28 bytes, HF: 4 floats + processor_id + tuner_id[3] + fpga_type_id)
//     100/4  — SRx Checksum           (1000 bytes)
//     100/6  — PBIT Status            (88 bytes, HF-specific layout)
//     100/8  — IBIT Status            (68 bytes, HF-specific layout)
//     100/10 — Temperature Status     (36 bytes, 9 floats)
//     100/14 — Fan Speed Status       (4 bytes)
//     100/17 — UART Port Select cmd   (4 bytes)
//     100/18 — UART Test Status       (4 bytes)
//     100/26 — CBIT Status            (8 bytes)
//   Group 109 (Date/Time): 109/11 cmd, 109/12 rsp
//   Group 111 (Signal/Scan):
//     cmds 3(16B),5,9,13,15,17,19,25; rsps 4(12B),6,10,14,16(40B/entry),18,20,26,211
//   Group 112 (Fast Scan/Simulation): cmds 1,5,13,37; rsps 2,6,14,38
//   Group 101 (SJC detection + jamming):
//     detection cmds: 25,27,37,39,43,47,55,69,83,85,87,94,100,102,104,106,140,158,160-186,200,202,204,210
//     detection rsps: 26,28,38,40,44,48,56,70,84,86,88,95,101,103,105,107,141,159,161-187,201,203,205
//     jamming cmds:   31,33,63,73,75,79,92
//     jamming rsps:   32,34,64,74,76,80,93
//   Group 106 (HF ECM jamming — immediate jam):
//     cmds 1,3,5,9,21,39,41,45,49,55; rsps 2,4,6,10,22,40,42,46,50,54,56
//   All other units fall through to raw_hex envelope (never crashes).
#include "sdfc_abi.h"
#include "sdfc_frame.h"
#include "sdfc_endian.h"
#include "json_writer.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <cstdio>

#include "hf/hf_groups.h"
#include "hf/hf_json_utils.h"
#include "hf/hf_shared.h"

using namespace sdfc;

static constexpr const char* HW_NAME = "dp_ecm_hf";

// =============================================================================
// Group 111 decode functions moved to src/hf/parser/g111_parser.cpp
// =============================================================================

// =============================================================================
// Group 112 decode functions moved to src/hf/parser/g112_parser.cpp
// Group 112 encode functions moved to src/hf/format/g112_format.cpp
// =============================================================================

// =============================================================================
// Group 200 decode functions moved to src/hf/parser/g200_parser.cpp
// Group 200 encode functions moved to src/hf/format/g200_format.cpp
// =============================================================================

// =============================================================================
// Group 3 multi-channel status response decoder
// =============================================================================

// 3/18 — Read Channel Information response (16 bytes): 8× uint16 channel status.
static void decode_mrx_all_channels_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) { w.key_str("warning", "mrx_channels_rsp < 16 bytes"); return; }
    std::string arr = "[";
    for (int i = 0; i < 8; ++i) {
        if (i) arr += ',';
        char tmp[64];
        uint16_t status = load_u16le(p + i * 2);
        std::snprintf(tmp, sizeof(tmp),
            "{\"channel\":%d,\"status\":%u,\"state\":\"%s\"}",
            i + 1, status, status == 1 ? "open" : "closed");
        arr += tmp;
    }
    arr += "]";
    w.key_raw("channels", arr);
}

// 3/22 — MRX Individual Channel Init Status response (16 bytes, per ICD Table 219).
// 8 channels × uint16: 1 = initialization success, 0 = initialization failure.
static void decode_mrx_channel_init_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) { w.key_str("warning", "mrx_channel_init_status < 16 bytes"); return; }
    std::string arr = "[";
    for (int i = 0; i < 8; ++i) {
        if (i) arr += ',';
        char tmp[64];
        uint16_t status = load_u16le(p + i * 2);
        std::snprintf(tmp, sizeof(tmp),
            "{\"channel\":%d,\"init_status\":%u,\"state\":\"%s\"}",
            i + 1, status, status == 1 ? "success" : "failure");
        arr += tmp;
    }
    arr += "]";
    w.key_raw("channel_init_statuses", arr);
}

// =============================================================================
// Group 4 optical IQ command/response decoders
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

// =============================================================================
// MRx Group 1 decode functions moved to src/hf/parser/mrx_g1_parser.cpp
// Note: decode_mrx_cbit_status retained here — also used by Group 3 (3/26).
// =============================================================================

// 1/26 — CBIT Status response (8 bytes). Also used by Group 3 (3/26).
static void decode_mrx_cbit_status(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "mrx_cbit_status < 8 bytes"); return; }
    w.key_uint("drx_status",            p[0]);
    w.key_uint("voltage_status",        p[1]);
    w.key_uint("temperature_status",    p[2]);
    // p[3] reserved
    w.key_uint("tuner_detection_status",p[4]);
    // p[5-6] reserved
    w.key_uint("memory_status",         p[7]);
}

// =============================================================================
// MRx Group 3 — RF board / channel management
// =============================================================================

// 3/2 — Read Board Count response (4 bytes).
static void decode_mrx_board_count_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) return;
    uint16_t count = load_u16le(p + 0);
    w.key_uint("board_count", count);
    w.key_str("note", count == 1 ? "mrx_channels_accessible" : "mrx_channels_not_accessible");
    w.key_uint("available_tuner_id", load_u16le(p + 2));
}

// 3/18 and 3/22 — Channel status response (16 bytes).
// channel1_status + 7× reserved u16. Status 1=open/initialised, 0=closed/failed.
static void decode_mrx_channel_16b_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) return;
    w.key_uint("channel1_status", load_u16le(p + 0));
    w.key_str("channel1_name", load_u16le(p + 0) == 1 ? "open_or_success" : "closed_or_fail");
}

// 3/19 — Write Channel Information command (4 bytes).
static void decode_mrx_write_channel_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) return;
    w.key_uint("mrx_channel", load_u16le(p + 0));
    w.key_uint("channel_status", load_u16le(p + 2));
    w.key_str("channel_action", load_u16le(p + 2) == 1 ? "open" : "close");
}

// 3/24 — Read MRx and SRx Tuning Details response (8 bytes).
// @0 SRx Tuned (u8)  @1 MRx Tuned (u8)  @2 SRx Scan Mode (u16)
// @4 Tuned Center Freq (u16)  @6 Memory Scan Tuned (u8)  @7 BITE Selection (u8)
static void decode_mrx_tuning_details_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "mrx_tuning_details < 8 bytes"); return; }
    static const char* SRX_TUNED[] = {"not_tuned", "search_mode", "jam_mode"};
    w.key_uint("srx_tuned_status",        p[0]);
    w.key_str("srx_tuned_status_name",    p[0] < 3 ? SRX_TUNED[p[0]] : "unknown");
    w.key_uint("mrx_tuned_status",        p[1]);
    w.key_uint("srx_scan_mode_status",    load_u16le(p + 2));
    w.key_uint("tuned_center_freq_mhz",   load_u16le(p + 4));
    w.key_uint("memory_scan_tuned",       p[6]);
    w.key_uint("bite_selection",          p[7]);
    w.key_str("bite_selection_name",      p[7] == 0 ? "antenna" : "bite");
}

// =============================================================================
// MRx Group 5 — Tuner
// =============================================================================

// 5/14 — Read AGC/MGC Attenuation response (4 bytes).
static void decode_mrx_agc_status_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) return;
    w.key_uint("rf_attenuation_db", p[0]);
    w.key_uint("if_attenuation_db", p[1]);
    w.key_uint("agc_running",       load_u16le(p + 2));
    w.key_str("agc_status",         load_u16le(p + 2) ? "running" : "manual");
}

// 5/1 — Set Center Frequency command (12 bytes).
static void decode_mrx_set_center_freq_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 12) { w.key_str("warning", "mrx_set_center_freq < 12 bytes"); return; }
    w.key_uint("mrx_channel",         load_u16le(p + 0));
    w.key_uint("bite_antenna_sel",     load_u16le(p + 2));
    w.key_str("antenna_path",          load_u16le(p + 2) == 0 ? "antenna" : "bite");
    w.key_double("center_freq_hz",     load_f64le(p + 4) * 1e6);
}

// 5/3 — Attenuation Selection command (16 bytes, per ICD Table 256).
// @0 mrx_channel(uint16) @2 reserved(uint16) @4 rf_attenuation(float) @8 if_attenuation(float) @12 cal_value_debug(4B, not decoded).
static void decode_mrx_attenuation_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) { w.key_str("warning", "mrx_attenuation < 16 bytes"); return; }
    w.key_uint("mrx_channel",         load_u16le(p + 0));
    w.key_double("rf_attenuation_db", static_cast<double>(load_f32le(p + 4)));
    w.key_double("if_attenuation_db", static_cast<double>(load_f32le(p + 8)));
}

// =============================================================================
// MRx Group 4 — Data acquisition
// =============================================================================

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

// =============================================================================
// MRx Group 6 — FH monitoring / GO2Monitor
// =============================================================================

// 6/7 — FH Monitoring Configuration command (52 bytes).
static void decode_mrx_fh_monitoring_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 52) { w.key_str("warning", "mrx_fh_monitoring < 52 bytes"); return; }
    static const char* RBW[] = {"6.25kHz","12.5kHz","25kHz","50kHz","100kHz","200kHz"};
    static const char* INT[] = {
        "10us","20us","40us","80us","160us","320us","640us","1280us","2560us","5120us","10240us","2048us"
    };
    uint8_t rbw_idx = p[2];
    uint8_t int_idx = p[3];
    w.key_uint("mrx_channel",           load_u16le(p +  0));
    w.key_uint("rbw_index",             rbw_idx);
    w.key_str("rbw_name",               rbw_idx < 6  ? RBW[rbw_idx] : "unknown");
    w.key_uint("integration_time_index",int_idx);
    w.key_str("integration_time_name",  int_idx < 12 ? INT[int_idx] : "unknown");
    w.key_double("fh_start_freq_hz",    load_f64le(p +  4) * 1e6);
    w.key_double("fh_stop_freq_hz",     load_f64le(p + 12) * 1e6);
    w.key_double("hop_period_ms",       static_cast<double>(load_f32le(p + 20)));
    w.key_double("inter_hop_period_ms", static_cast<double>(load_f32le(p + 24)));
    w.key_double("power_level_dbm",     static_cast<double>(load_f32le(p + 28)));
    w.key_double("band_start_freq_hz",  static_cast<double>(load_u32le(p + 40)) * 1e6);
    w.key_double("band_stop_freq_hz",   static_cast<double>(load_u32le(p + 44)) * 1e6);
    w.key_bool("fh_80mhz_stream",       p[48] != 0);
    w.key_bool("fh_enabled",            p[49] != 0);
    w.key_bool("header_enabled",        p[50] != 0);
}

// 6/9 — GO2Monitor Connection Establishment command (260 bytes).
// @0 IP Address (char[128])  @128 Port Number (char[128])  @256 mrx_channel (u16)
// 6/9 — GO2Monitor Connection Establishment command (260 bytes, per ICD Table 258).
// @0 ip_address(char[128]) @128 port_number(char[128]) @256 mrx_channel(uint16) @258 reserved(uint16).
static void decode_mrx_go2monitor_connect_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 260) { w.key_str("warning", "go2monitor_connect cmd < 260 bytes"); return; }
    const char* ip   = reinterpret_cast<const char*>(p +   0);
    const char* port = reinterpret_cast<const char*>(p + 128);
    w.key_str("ip_address",   std::string(ip,   strnlen(ip,   128)));
    w.key_str("port_number",  std::string(port, strnlen(port, 128)));
    w.key_uint("mrx_channel", load_u16le(p + 256));
}

// 6/13 — Start GO2Monitor Transmission command (144 bytes).
// @0 mrx_channel(u16) @2 bw(u16) @4 reserved(u16) @6 reserved(u16)
// @8 center_freq(double,8) @16 date(char[128])
static void decode_mrx_start_go2monitor_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 144) { w.key_str("warning", "start_go2monitor cmd < 144 bytes"); return; }
    static const char* BW[] = {"1MHz","240kHz","120kHz","60kHz","30kHz"};
    uint16_t bw = load_u16le(p + 2);
    w.key_uint("mrx_channel",       load_u16le(p +  0));
    w.key_uint("bw_selection",      bw);
    w.key_str("bandwidth_name",     bw < 5 ? BW[bw] : "unknown");
    w.key_double("center_freq_hz",  load_f64le(p + 8) * 1e6);
    const char* date = reinterpret_cast<const char*>(p + 16);
    w.key_str("date", std::string(date, strnlen(date, 128)));
}

// =============================================================================
// MRx Group 7 — Signal BITE / miscellaneous
// =============================================================================

// 7/1 — Signal BITE command (12 bytes).
static void decode_mrx_signal_bite_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 12) return;
    w.key_double("bite_freq_hz", load_f64le(p + 0) * 1e6);
    w.key_uint("mrx_channel",    load_u16le(p + 8));
}

// 7/2 — Signal BITE response (16 bytes).
static void decode_mrx_signal_bite_rsp(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 16) return;
    w.key_double("observed_freq_hz",   load_f64le(p + 0) * 1e6);
    w.key_double("observed_power_dbm", static_cast<double>(load_f32le(p + 8)));
    w.key_uint("result",               load_u16le(p + 12));
    w.key_str("result_name",           load_u16le(p + 12) == 1 ? "pass" : "fail");
}

// 7/17 — Spectrum Average Count Selection command (8 bytes, per ICD Table 285).
// @0 averaging_enabled(uint16) @2 avg_count(uint16) @4 mrx_channel(uint16) @6 reserved(uint16).
static void decode_mrx_spectrum_avg_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) { w.key_str("warning", "spectrum_avg cmd < 8 bytes"); return; }
    w.key_bool("averaging_enabled", load_u16le(p + 0) == 1);
    w.key_uint("avg_count",         load_u16le(p + 2));
    w.key_uint("mrx_channel",       load_u16le(p + 4));
}

// 7/21 — Audio Squelch command (8 bytes).
static void decode_mrx_audio_squelch_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) return;
    w.key_uint("squelch_selection", load_u16le(p + 0));
    w.key_str("squelch_state",      load_u16le(p + 0) == 1 ? "on" : "off");
    w.key_uint("mrx_channel",       load_u16le(p + 2));
    w.key_double("threshold_dbm",   static_cast<double>(load_f32le(p + 4)));
}

// 7/23 — Set Date and Time command (8 bytes). Same layout as ECM Group 109/11.
static void decode_mrx_date_time_cmd(const uint8_t* p, int n, JsonWriter& w) {
    if (n < 8) return;
    w.key_uint("day",     p[0]);
    w.key_uint("month",   p[1]);
    w.key_uint("year",    load_u16le(p + 2));
    w.key_uint("hour",    p[4]);
    w.key_uint("minute",  p[5]);
    w.key_uint("seconds", p[6]);
}

// Shared helper: decode 4-byte "selection + mrx_channel" command.
static void decode_mrx_sel_channel_cmd(const char* sel_key, const char* on_name, const char* off_name,
                                        const uint8_t* p, int n, JsonWriter& w) {
    if (n < 4) return;
    uint16_t sel = load_u16le(p + 0);
    w.key_uint(sel_key, sel);
    if (on_name && off_name) w.key_str("state", sel ? on_name : off_name);
    w.key_uint("mrx_channel", load_u16le(p + 2));
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
// ABI: extract_frame
// =============================================================================
extern "C" SDFC_EXPORT int extract_frame(const uint8_t* buf, size_t buf_len,
                                         uint8_t** out_frame, size_t* out_len) {
    if (!buf || !out_frame || !out_len) return -1;
    uint8_t* tmp = static_cast<uint8_t*>(std::malloc(MAX_FRAME_BUFFER_BYTES));
    if (!tmp) return -1;
    int tmp_len = 0;
    int result = extract_frame_core(buf, static_cast<int>(buf_len), tmp, &tmp_len);
    if (result <= 0) { std::free(tmp); return -1; }
    uint8_t* frame = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(tmp_len)));
    if (!frame) { std::free(tmp); return -1; }
    std::memcpy(frame, tmp, static_cast<size_t>(tmp_len));
    std::free(tmp);
    *out_frame = frame;
    *out_len = static_cast<size_t>(tmp_len);
    return 0;
}

// =============================================================================
// ABI: parse_message
// =============================================================================
extern "C" SDFC_EXPORT int parse_message(const uint8_t* frame, size_t frame_len,
                                         char** out_json, size_t* out_len) {
    if (!frame || !out_json || !out_len) return -1;
    int frame_type = FRAME_INCOMPLETE;
    if (frame_len >= static_cast<size_t>(MAGIC_LEN)) {
        if (std::memcmp(frame, CMD_HEADER,  MAGIC_LEN) == 0) frame_type = FRAME_COMMAND;
        else if (std::memcmp(frame, RESP_HEADER, MAGIC_LEN) == 0) frame_type = FRAME_RESPONSE;
    }
    if (frame_type == FRAME_INCOMPLETE) return -1;
    FrameHeader hdr;
    if (!decode_header(frame, static_cast<int>(frame_len), frame_type, hdr)) return -1;
    if (hdr.total_len > static_cast<int>(frame_len)) return -1;

    const uint8_t* payload = frame + hdr.payload_off;
    int plen = static_cast<int>(hdr.payload_size);
    if (hdr.payload_off + plen > static_cast<int>(frame_len)) return -1;

    JsonWriter w;
    w.key_str("hw", HW_NAME);
    if (frame_type == FRAME_COMMAND) w.key_str("frame_type", "command");
    w.key_uint("group_id", hdr.group_id);
    w.key_uint("unit_id",  hdr.unit_id);
    w.key_uint("message_size_bytes", hdr.payload_size);
    if (frame_type == FRAME_RESPONSE) {
        w.key_int("status", hdr.status);
        w.key_str("status_name", hdr.status == 0 ? "OnSuccess" : "Error");
    }

    bool decoded = false;

    // ------------------------------------------------------------------
    // FRAME_COMMAND dispatch
    // ------------------------------------------------------------------
    if (frame_type == FRAME_COMMAND) {
        if (hdr.group_id == 100) decoded = g100_parse_cmd(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id == 101) decoded = g101_parse_cmd(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id == 109) decoded = g109_parse_cmd(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id == 111) {
            decoded = g111_parse_cmd(hdr.unit_id, payload, plen, w);
        } else if (hdr.group_id == 112) {
            decoded = g112_parse_cmd(hdr.unit_id, payload, plen, w);
        } else if (hdr.group_id == 200) {
            decoded = g200_parse_cmd(hdr.unit_id, payload, plen, w);
        // ECM Group 106 — Immediate Jamming commands
        } else if (hdr.group_id == 106) {
            decoded = g106_parse_cmd(hdr.unit_id, payload, plen, w);
        // MRx Group 1 — diagnostics
        } else if (hdr.group_id == 1) {
            decoded = mrx_g1_parse_cmd(hdr.unit_id, payload, plen, w);
        // MRx Group 3 — RF board / channel management
        } else if (hdr.group_id == 3) {
            switch (hdr.unit_id) {
                case  1: /* Read Board Count — 0 bytes */      decoded = true; break;
                case 17: /* Read Channel Info — 0 bytes */     decoded = true; break;
                case 19: decode_mrx_write_channel_cmd(payload, plen, w); decoded = true; break;
                case 21: /* VUSHF Channel Status — 0 bytes */  decoded = true; break;
                case 23: /* Read Tuning Details — 0 bytes */   decoded = true; break;
                case 25: /* Get CBIT Status — 0 bytes */       decoded = true; break;
                default: break;
            }
        // MRx Group 4 — data acquisition
        } else if (hdr.group_id == 4) {
            switch (hdr.unit_id) {
                case  5: decode_mrx_set_threshold_cmd(payload, plen, w);            decoded = true; break;
                case  7: decode_mrx_channel_cmd(payload, plen, w);                  decoded = true; break;
                case  9: decode_mrx_channel_cmd(payload, plen, w);                  decoded = true; break;
                case 11: decode_mrx_channel_cmd(payload, plen, w);                  decoded = true; break;
                case 15: decode_mrx_channel_cmd(payload, plen, w);                  decoded = true; break;
                case 17: decode_mrx_demod_bw_cmd(payload, plen, w);                 decoded = true; break;
                case 23: decode_mrx_iq_logging_start_cmd(payload, plen, w);         decoded = true; break;
                case 25: decode_mrx_channel_cmd(payload, plen, w);                  decoded = true; break;
                case 33: decode_mrx_iq_start_cmd(payload, plen, w);                 decoded = true; break;
                case 35: decode_mrx_channel_cmd(payload, plen, w);                  decoded = true; break;
                case 39: decode_mrx_memory_scan_config_cmd(payload, plen, w);       decoded = true; break;
                case 41: decode_mrx_memory_scan_config_cmd(payload, plen, w);       decoded = true; break;
                case 43: decode_mrx_ddc_fft_cmd(payload, plen, w);                  decoded = true; break;
                case 53: decode_mrx_engage_channel_cmd(payload, plen, w);           decoded = true; break;
                case 55: decode_mrx_channel_cmd(payload, plen, w);                  decoded = true; break;
                case 57: decode_mrx_channel_cmd(payload, plen, w);                  decoded = true; break;
                case 59: decode_mrx_channel_cmd(payload, plen, w);                  decoded = true; break;
                case 61: decode_mrx_smart_scan_read_cmd(payload, plen, w);          decoded = true; break;
                case 63: decode_mrx_channel_cmd(payload, plen, w);                  decoded = true; break;
                case 65: decode_mrx_optical_iq_cmd(payload, plen, w);          decoded = true; break;
                case 67: decode_mrx_channel_cmd(payload, plen, w);              decoded = true; break; // Stop optical IQ
                case 69: decode_mrx_optical_iq_cmd(payload, plen, w);          decoded = true; break; // Read port status cmd
                case 71: decode_mrx_optical_iq_cmd(payload, plen, w);          decoded = true; break; // Read IP cmd
                default: break;
            }
        // MRx Group 5 — tuner
        } else if (hdr.group_id == 5) {
            switch (hdr.unit_id) {
                case  1: decode_mrx_set_center_freq_cmd(payload, plen, w);          decoded = true; break;
                case  3: decode_mrx_attenuation_cmd(payload, plen, w);              decoded = true; break;
                case  9: decode_mrx_channel_cmd(payload, plen, w);                  decoded = true; break; // Clear Center Freq
                case 13: /* Read AGC/MGC — 0 bytes */                               decoded = true; break;
                default: break;
            }
        // MRx Group 6 — FH monitoring / GO2Monitor
        } else if (hdr.group_id == 6) {
            switch (hdr.unit_id) {
                case  7: decode_mrx_fh_monitoring_cmd(payload, plen, w);            decoded = true; break;
                case  9: decode_mrx_go2monitor_connect_cmd(payload, plen, w);       decoded = true; break;
                case 11: /* GO2Monitor Disconnect — 0 bytes */                      decoded = true; break;
                case 13: decode_mrx_start_go2monitor_cmd(payload, plen, w);         decoded = true; break;
                case 15: /* Stop GO2Monitor — 0 bytes */                            decoded = true; break;
                default: break;
            }
        // MRx Group 7 — Signal BITE / misc
        } else if (hdr.group_id == 7) {
            switch (hdr.unit_id) {
                case  1: decode_mrx_signal_bite_cmd(payload, plen, w);              decoded = true; break;
                case  3: decode_mrx_sel_channel_cmd("bite_antenna_sel","bite","antenna",payload,plen,w); decoded=true; break;
                case  5: decode_mrx_sel_channel_cmd("ref_source_sel","external","internal",payload,plen,w); decoded=true; break;
                case  9: decode_mrx_sel_channel_cmd("afc_sel","on","off",payload,plen,w); decoded=true; break;
                case 11: decode_mrx_sel_channel_cmd("rf_squelch_sel","on","off",payload,plen,w); decoded=true; break;
                case 13: decode_mrx_channel_cmd(payload, plen, w);                  decoded = true; break; // IQ socket open
                case 15: decode_mrx_channel_cmd(payload, plen, w);                  decoded = true; break; // IQ socket close
                case 17: decode_mrx_spectrum_avg_cmd(payload, plen, w);            decoded = true; break;
                case 19: decode_mrx_sel_channel_cmd("rf_agc_sel","enable","disable",payload,plen,w); decoded=true; break;
                case 21: decode_mrx_audio_squelch_cmd(payload, plen, w);            decoded = true; break;
                case 23: decode_mrx_date_time_cmd(payload, plen, w);                decoded = true; break;
                default: break;
            }
        }
    // ------------------------------------------------------------------
    // FRAME_RESPONSE dispatch
    // ------------------------------------------------------------------
    } 
    else if (frame_type == FRAME_RESPONSE) {
        if (hdr.group_id == 100) decoded = g100_parse_rsp(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id == 101) decoded = g101_parse_rsp(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id == 109) decoded = g109_parse_rsp(hdr.unit_id, payload, plen, w);
        else if (hdr.group_id == 111) {
            // unit 211 is a cross-group stream rsp to 101/210 — no struct decoder, falls through to raw_hex
            if (hdr.unit_id != 211) decoded = g111_parse_rsp(hdr.unit_id, payload, plen, w);
        } else if (hdr.group_id == 112) {
            decoded = g112_parse_rsp(hdr.unit_id, payload, plen, w);
        } else if (hdr.group_id == 200) {
            decoded = g200_parse_rsp(hdr.unit_id, payload, plen, w);
        } else if (hdr.group_id == 106) {
            decoded = g106_parse_rsp(hdr.unit_id, payload, plen, w);
        // MRx Group 1 responses
        } else if (hdr.group_id == 1) {
            decoded = mrx_g1_parse_rsp(hdr.unit_id, payload, plen, w);
        // MRx Group 3 responses
        } else if (hdr.group_id == 3) {
            switch (hdr.unit_id) {
                case  2: decode_mrx_board_count_rsp(payload, plen, w);      decoded = true; break;
                case 18: decode_mrx_all_channels_rsp(payload, plen, w);       decoded = true; break;
                case 20: /* Write Channel ACK */                               decoded = true; break;
                case 22: decode_mrx_channel_init_status(payload, plen, w);    decoded = true; break;
                case 24: decode_mrx_tuning_details_rsp(payload, plen, w);   decoded = true; break;
                case 26: decode_mrx_cbit_status(payload, plen, w);         decoded = true; break;
                default: break;
            }
        // MRx Group 4 responses
        } else if (hdr.group_id == 4) {
            switch (hdr.unit_id) {
                case  6: /* Set Threshold ACK */                             decoded = true; break;
                case  8: decode_mrx_audio_data_rsp(payload, plen, w);       decoded = true; break;
                case 10: /* Audio Start Play ACK */                          decoded = true; break;
                case 12: /* Audio Stop Play ACK */                           decoded = true; break;
                case 16: /* Audio FIFO Reset ACK */                          decoded = true; break;
                case 18: /* Demod/BW Select ACK */                           decoded = true; break;
                case 24: decode_mrx_iq_start_rsp(payload, plen, w);         decoded = true; break;
                case 26: decode_mrx_iq_logging_stop_rsp(payload, plen, w);  decoded = true; break;
                case 34: decode_mrx_iq_start_rsp(payload, plen, w);         decoded = true; break;
                case 36: /* Stop IQ Streaming ACK */                         decoded = true; break;
                case 40: /* Configure Memory Scan ACK */                     decoded = true; break;
                case 42: decode_mrx_memory_scan_data_rsp(payload, plen, w); decoded = true; break;
                case 44: decode_mrx_ddc_fft_rsp(payload, plen, w);          decoded = true; break;
                case 54: /* Engage Channel ACK */                            decoded = true; break;
                case 56: /* Disengage Channel ACK */                         decoded = true; break;
                case 58: /* Stop Memory Scan ACK */                          decoded = true; break;
                case 60: /* Smart Memory Scan Config ACK */                  decoded = true; break;
                case 62: decode_mrx_smart_scan_read_rsp(payload, plen, w);  decoded = true; break;
                case 64: /* Stop Smart Memory Scan ACK */                    decoded = true; break;
                case 66: /* Start Optical IQ ACK */                          decoded = true; break;
                case 68: /* Stop Optical IQ ACK */                           decoded = true; break;
                case 70: decode_mrx_optical_port_status_rsp(payload, plen, w); decoded = true; break;
                case 72: decode_mrx_optical_ip_rsp(payload, plen, w);       decoded = true; break;
                default: break;
            }
        // MRx Group 5 responses
        } else if (hdr.group_id == 5) {
            switch (hdr.unit_id) {
                case  2: /* Set Center Freq ACK */                           decoded = true; break;
                case  4: /* Attenuation Select ACK */                        decoded = true; break;
                case 10: /* Clear Center Freq ACK */                         decoded = true; break;
                case 14: decode_mrx_agc_status_rsp(payload, plen, w);       decoded = true; break;
                default: break;
            }
        // MRx Group 6 responses
        } else if (hdr.group_id == 6) {
            switch (hdr.unit_id) {
                case  8: /* FH Monitoring Config ACK */                      decoded = true; break;
                case 10: /* GO2Monitor Connect ACK */                        decoded = true; break;
                case 12: /* GO2Monitor Disconnect ACK */                     decoded = true; break;
                case 14: /* Start GO2Monitor Transmission ACK */              decoded = true; break;
                case 16: /* Stop GO2Monitor ACK */                           decoded = true; break;
                default: break;
            }
        // MRx Group 7 responses
        } else if (hdr.group_id == 7) {
            switch (hdr.unit_id) {
                case  2: decode_mrx_signal_bite_rsp(payload, plen, w); decoded = true; break;
                case  4: /* BITE/Antenna Select ACK */                  decoded = true; break;
                case  6: /* Ref Source Select ACK */                    decoded = true; break;
                case 10: /* AFC ACK */                                  decoded = true; break;
                case 12: /* RF Squelch ACK */                           decoded = true; break;
                case 14: /* IQ Socket Open ACK */                       decoded = true; break;
                case 16: /* IQ Socket Close ACK */                      decoded = true; break;
                case 18: /* Spectrum Avg Count ACK */                   decoded = true; break;
                case 20: /* Smart RF AGC ACK */                         decoded = true; break;
                case 22: /* Audio Squelch ACK */                        decoded = true; break;
                case 24: /* Set Date/Time ACK */                        decoded = true; break;
                default: break;
            }
        }
    }

    // Safe fallback: expose raw bytes so nothing is lost and the bridge never crashes.
    if (!decoded && plen > 0) {
        w.key_str("raw_hex", to_hex(payload, plen));
    }

    std::string json = w.str();
    char* result = static_cast<char*>(std::malloc(json.size() + 1));
    if (!result) return -1;
    std::memcpy(result, json.c_str(), json.size() + 1);
    *out_json = result;
    *out_len = json.size();
    return 0;
}

// =============================================================================
// ABI: format_response
// =============================================================================
// Group 111 encoders moved to src/hf/format/g111_format.cpp
// encode_channelization static helper also removed (lives in g109_format.cpp and g111_format.cpp)

// ---------------------------------------------------------------------------
// Group 112 encoders moved to src/hf/format/g112_format.cpp
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Group 200 encoders moved to src/hf/format/g200_format.cpp
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Group 106 encoders moved to src/hf/format/g106_format.cpp
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// MRx Group 1 encoders moved to src/hf/format/mrx_g1_format.cpp
// Note: encode_mrx_cbit_status retained here — also used by Group 3 (3/26).
// ---------------------------------------------------------------------------

static int encode_mrx_cbit_status(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 8) return -1;
    std::memset(buf, 0, 8);
    long long v = 0;
    auto gi = [&](const char* k, int i) { if (json_find_int(j, k, v)) buf[i] = (uint8_t)v; };
    gi("drx_status",             0); gi("voltage_status",        1);
    gi("temperature_status",     2); gi("tuner_detection_status",4);
    gi("memory_status",          7);
    return 8;
}

// ---------------------------------------------------------------------------
// MRx Group 3 encoders
// ---------------------------------------------------------------------------
static int encode_mrx_board_count_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    long long count = 0, tuner_id = 0;
    json_find_int(j, "board_count",        count);
    json_find_int(j, "available_tuner_id", tuner_id);
    store_u16le(buf + 0, (uint16_t)count); store_u16le(buf + 2, (uint16_t)tuner_id);
    return 4;
}

static int encode_mrx_channels_16b(const char* j, const char* arr_key,
                                    const char* status_key, uint8_t* buf, int max_len) {
    if (max_len < 16) return -1;
    std::memset(buf, 0, 16);
    const char* pd = std::strstr(j, arr_key); if (!pd) return 16;
    const char* arr = std::strchr(pd, '['); if (!arr) return 16;
    const char* p = arr + 1; int written = 0;
    while (written < 8) {
        while (*p && *p != '{' && *p != ']') ++p;
        if (!*p || *p == ']') break;
        const char* os = p; int depth = 1; ++p;
        while (*p && depth > 0) { if (*p == '{') ++depth; else if (*p == '}') --depth; ++p; }
        std::string entry(os, p);
        long long status = 0; json_find_int(entry.c_str(), status_key, status);
        store_u16le(buf + written * 2, (uint16_t)status); ++written;
    }
    return 16;
}

static int encode_mrx_tuning_details_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 8) return -1;
    std::memset(buf, 0, 8);
    long long v = 0;
    auto gi  = [&](const char* k, int i) { if (json_find_int(j, k, v)) buf[i] = (uint8_t)v; };
    auto gu  = [&](const char* k, int i) { if (json_find_int(j, k, v)) store_u16le(buf + i, (uint16_t)v); };
    gi("srx_tuned_status", 0); gi("mrx_tuned_status", 1);
    gu("srx_scan_mode_status",   2); gu("tuned_center_freq_mhz", 4);
    gi("memory_scan_tuned", 6); gi("bite_selection", 7);
    return 8;
}

// ---------------------------------------------------------------------------
// MRx Group 4 encoders
// ---------------------------------------------------------------------------
static int encode_mrx_audio_data_rsp(const char* j, uint8_t* buf, int max_len) {
    long long audio_size = 0; json_find_int(j, "audio_data_size", audio_size);
    if (audio_size < 0) audio_size = 0;
    if (audio_size > 262144) audio_size = 262144;
    int total = 4 + (int)audio_size * 2;
    if (total > max_len) return -1;
    std::memset(buf, 0, (size_t)total);
    store_u32le(buf, (uint32_t)audio_size);
    if (audio_size == 0) return 4;
    const char* pd = std::strstr(j, "\"audio_data\""); if (!pd) return total;
    const char* arr = std::strchr(pd, '['); if (!arr) return total; ++arr;
    int written = 0;
    while (written < (int)audio_size) {
        while (*arr == ' ' || *arr == '\t' || *arr == '\n' || *arr == ',') ++arr;
        if (*arr == ']' || !*arr) break;
        char* end; long long v = std::strtoll(arr, &end, 10);
        if (end == arr) break;
        store_u16le(buf + 4 + written * 2, (uint16_t)v); arr = end; ++written;
    }
    return total;
}

static int encode_mrx_iq_start_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    long long v = 0;
    if (json_find_int(j, "mrx_channel",          v)) buf[0] = (uint8_t)v;
    if (json_find_int(j, "narrowband_iq_status",  v)) buf[1] = (uint8_t)v;
    if (json_find_int(j, "wideband_iq_status",    v)) buf[2] = (uint8_t)v;
    return 4;
}

static int encode_mrx_iq_logging_stop_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 132) return -1;
    std::memset(buf, 0, 132);
    long long ch = 0; json_find_int(j, "mrx_channel", ch); store_u16le(buf, (uint16_t)ch);
    const char* key = std::strstr(j, "\"file_path\""); if (!key) return 132;
    const char* c = std::strchr(key, ':'); if (!c) return 132;
    while (*c && *c != '"') ++c; if (*c != '"') return 132; ++c;
    size_t idx = 0;
    while (*c && *c != '"' && idx < 127) buf[4 + idx++] = (uint8_t)*c++;
    return 132;
}

static int encode_mrx_memory_scan_data_rsp(const char* j, uint8_t* buf, int max_len) {
    long long count = 0, scan_speed = 0;
    json_find_int(j, "total_available_count",       count);
    json_find_int(j, "scan_speed_channels_per_sec", scan_speed);
    if (count < 0) count = 0; if (count > 10000) count = 10000;
    int total = 8 + (int)count * 20;
    if (total > max_len) return -1;
    std::memset(buf, 0, (size_t)total);
    store_u32le(buf + 0, (uint32_t)count); store_u16le(buf + 4, (uint16_t)scan_speed);
    if (count == 0) return 8;
    const char* pd = std::strstr(j, "\"scan_data\""); if (!pd) return 8;
    const char* arr = std::strchr(pd, '['); if (!arr) return 8;
    const char* p = arr + 1; int written = 0;
    while (written < (int)count) {
        while (*p && *p != '{' && *p != ']') ++p;
        if (!*p || *p == ']') break;
        const char* os = p; int depth = 1; ++p;
        while (*p && depth > 0) { if (*p == '{') ++depth; else if (*p == '}') --depth; ++p; }
        std::string entry(os, p); const char* e = entry.c_str();
        double power = 0, freq = 0; long long bw = 0;
        json_find_double(e, "power_dbm", power); json_find_double(e, "freq_hz", freq);
        json_find_int(e, "bandwidth_list", bw);
        uint8_t* slot = buf + 8 + written * 20;
        store_f32le(slot +  0, (float)power);
        store_f64le(slot +  4, freq / 1e6);
        store_u16le(slot + 18, (uint16_t)bw);
        ++written;
    }
    return 8 + written * 20;
}

static int encode_mrx_ddc_fft_rsp(const char* j, uint8_t* buf, int max_len) {
    long long bc = 0; json_find_int(j, "bin_count", bc);
    if (bc < 0) bc = 0; if (bc > 4096) bc = 4096;
    int total = 4 + (int)bc * 4;
    if (total > max_len) return -1;
    std::memset(buf, 0, (size_t)total);
    store_u16le(buf, (uint16_t)bc);
    if (bc == 0) return total;
    const char* pd = std::strstr(j, "\"ddc_fft_power_dbm\""); if (!pd) return total;
    const char* arr = std::strchr(pd, '['); if (!arr) return total; ++arr;
    int written = 0;
    while (written < (int)bc) {
        while (*arr == ' ' || *arr == '\t' || *arr == '\n' || *arr == ',') ++arr;
        if (*arr == ']' || !*arr) break;
        char* end; float v = (float)std::strtod(arr, &end);
        if (end == arr) break;
        store_f32le(buf + 4 + written * 4, v); arr = end; ++written;
    }
    return total;
}

static int encode_mrx_smart_scan_read_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 16) return -1;
    std::memset(buf, 0, 16);
    double freq = 0, amplitude = 0; long long ch = 0;
    json_find_double(j, "freq_hz",    freq);
    json_find_int(j,    "mrx_channel", ch);
    json_find_double(j, "amplitude",   amplitude);
    store_f64le(buf + 0, freq / 1e6);
    store_u16le(buf + 8, (uint16_t)ch);
    store_f32le(buf + 12, (float)amplitude);
    return 16;
}

static int encode_mrx_optical_port_status_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 12) return -1;
    std::memset(buf, 0, 12);
    long long v = 0;
    auto gu = [&](const char* k, int i) { if (json_find_int(j, k, v)) store_u16le(buf + i, (uint16_t)v); };
    gu("port_number", 0); gu("port_id", 2); gu("port_alive_status", 4);
    gu("already_transmitting", 6); gu("can_start_transfer", 8);
    return 12;
}

static int encode_mrx_optical_ip_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 24) return -1;
    std::memset(buf, 0, 24);
    const char* ip_key = std::strstr(j, "\"ip_address\"");
    if (ip_key) {
        const char* c = std::strchr(ip_key, ':'); if (c) { while (*c && *c != '"') ++c; }
        if (c && *c == '"') { ++c;
            unsigned a = 0, b = 0, cc = 0, d = 0;
            if (std::sscanf(c, "%u.%u.%u.%u", &a, &b, &cc, &d) == 4) {
                buf[0] = (uint8_t)a; buf[1] = (uint8_t)b; buf[2] = (uint8_t)cc; buf[3] = (uint8_t)d;
            }
        }
    }
    const char* pd = std::strstr(j, "\"port_ids\"");
    if (pd) {
        const char* arr = std::strchr(pd, '['); if (arr) { ++arr;
            for (int i = 0; i < 9; ++i) {
                while (*arr == ' ' || *arr == '\t' || *arr == '\n' || *arr == ',') ++arr;
                if (*arr == ']' || !*arr) break;
                char* end; long long v = std::strtoll(arr, &end, 10);
                if (end == arr) break;
                store_u16le(buf + 4 + i * 2, (uint16_t)v); arr = end;
            }
        }
    }
    return 24;
}

// ---------------------------------------------------------------------------
// MRx Group 5 encoders
// ---------------------------------------------------------------------------
static int encode_mrx_agc_status_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 4) return -1;
    std::memset(buf, 0, 4);
    long long rf = 0, ifatt = 0, agc = 0;
    json_find_int(j, "rf_attenuation_db", rf);
    json_find_int(j, "if_attenuation_db", ifatt);
    json_find_int(j, "agc_running",       agc);
    buf[0] = (uint8_t)rf; buf[1] = (uint8_t)ifatt; store_u16le(buf + 2, (uint16_t)agc);
    return 4;
}

// ---------------------------------------------------------------------------
// MRx Group 7 encoders
// ---------------------------------------------------------------------------
static int encode_mrx_signal_bite_rsp(const char* j, uint8_t* buf, int max_len) {
    if (max_len < 16) return -1;
    std::memset(buf, 0, 16);
    double freq = 0, power = 0; long long result = 0;
    json_find_double(j, "observed_freq_hz",   freq);
    json_find_double(j, "observed_power_dbm", power);
    json_find_int(j,    "result",             result);
    store_f64le(buf + 0, freq / 1e6);
    store_f32le(buf + 8, (float)power);
    store_u16le(buf + 12, (uint16_t)result);
    return 16;
}

extern "C" SDFC_EXPORT int format_response(const char* kind, const char* kwargs_json,
                                           uint8_t** out_buf, size_t* out_len) {
    if (!kwargs_json || !out_buf || !out_len) return -1;
    long long group = 0, unit = 0, status = 0;
    if (!json_find_int(kwargs_json, "group_id", group)) return -1;
    if (!json_find_int(kwargs_json, "unit_id",  unit))  return -1;
    if (!json_find_int(kwargs_json, "status",   status)) return -1;

    uint8_t* payload = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(MAX_PAYLOAD)));
    if (!payload) return -1;
    int plen = 0;
    // is_ack: this unit_id is a known zero-payload ACK — skip payload_hex fallback.
    bool is_ack = false;

    typedef int (*EncFn)(const char*, uint8_t*, int);
    EncFn fn = nullptr;

    if (group == 100) {
        plen = g100_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack);
        if (plen < 0) { std::free(payload); return -1; }
    }
    else if (group == 101) {
        plen = g101_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack);
        if (plen < 0) { std::free(payload); return -1; }
    } else if (group == 109) {
        plen = g109_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack);
        if (plen < 0) { std::free(payload); return -1; }
    } else if (group == 111) {
        plen = g111_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack);
        if (plen < 0) { std::free(payload); return -1; }
    } else if (group == 112) {
        plen = g112_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack);
        if (plen < 0) { std::free(payload); return -1; }
    } else if (group == 200) {
        plen = g200_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack);
        if (plen < 0) { std::free(payload); return -1; }
    } else if (group == 106) {
        plen = g106_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack);
        if (plen < 0) { std::free(payload); return -1; }
    } else if (group == 1) {   // MRx diagnostics
        plen = mrx_g1_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack);
        if (plen < 0) { std::free(payload); return -1; }
    } else if (group == 3) {   // MRx RF board
        switch (unit) {
            case  2: fn = encode_mrx_board_count_rsp;  break;
            case 18: {
                plen = encode_mrx_channels_16b(kwargs_json, "\"channels\"", "status", payload, MAX_PAYLOAD);
                if (plen < 0) { std::free(payload); return -1; }
                break;
            }
            case 20: is_ack = true; break; // Write Channel ACK
            case 22: {
                plen = encode_mrx_channels_16b(kwargs_json, "\"channel_init_statuses\"", "init_status", payload, MAX_PAYLOAD);
                if (plen < 0) { std::free(payload); return -1; }
                break;
            }
            case 24: fn = encode_mrx_tuning_details_rsp; break;
            case 26: fn = encode_mrx_cbit_status;         break;
            default: break;
        }
    } else if (group == 4) {   // MRx data acquisition
        switch (unit) {
            case  6: is_ack = true;                        break; // Set Threshold ACK
            case  8: fn = encode_mrx_audio_data_rsp;       break;
            case 10: is_ack = true;                        break; // Audio Start Play ACK
            case 12: is_ack = true;                        break; // Audio Stop Play ACK
            case 16: is_ack = true;                        break; // Audio FIFO Reset ACK
            case 18: is_ack = true;                        break; // Demod/BW Select ACK
            case 24: fn = encode_mrx_iq_start_rsp;         break;
            case 26: fn = encode_mrx_iq_logging_stop_rsp;  break;
            case 34: fn = encode_mrx_iq_start_rsp;         break;
            case 36: is_ack = true;                        break; // Stop IQ Streaming ACK
            case 40: is_ack = true;                        break; // Configure Memory Scan ACK
            case 42: fn = encode_mrx_memory_scan_data_rsp; break;
            case 44: fn = encode_mrx_ddc_fft_rsp;          break;
            case 54: is_ack = true;                        break; // Engage Channel ACK
            case 56: is_ack = true;                        break; // Disengage Channel ACK
            case 58: is_ack = true;                        break; // Stop Memory Scan ACK
            case 60: is_ack = true;                        break; // Smart Memory Scan Config ACK
            case 62: fn = encode_mrx_smart_scan_read_rsp;  break;
            case 64: is_ack = true;                        break; // Stop Smart Memory Scan ACK
            case 66: is_ack = true;                        break; // Start Optical IQ ACK
            case 68: is_ack = true;                        break; // Stop Optical IQ ACK
            case 70: fn = encode_mrx_optical_port_status_rsp; break;
            case 72: fn = encode_mrx_optical_ip_rsp;          break;
            default: break;
        }
    } else if (group == 5) {   // MRx tuner
        switch (unit) {
            case  2: is_ack = true;                  break; // Set Center Freq ACK
            case  4: is_ack = true;                  break; // Attenuation Select ACK
            case 10: is_ack = true;                  break; // Clear Center Freq ACK
            case 14: fn = encode_mrx_agc_status_rsp; break;
            default: break;
        }
    } else if (group == 6) {   // MRx FH monitoring / GO2Monitor
        switch (unit) {
            case  8: is_ack = true; break; // FH Monitoring Config ACK
            case 10: is_ack = true; break; // GO2Monitor Connect ACK
            case 12: is_ack = true; break; // GO2Monitor Disconnect ACK
            case 14: is_ack = true; break; // Start GO2Monitor ACK
            case 16: is_ack = true; break; // Stop GO2Monitor ACK
            default: break;
        }
    } else if (group == 7) {   // MRx Signal BITE / misc
        switch (unit) {
            case  2: fn = encode_mrx_signal_bite_rsp; break;
            case  4: is_ack = true; break; // BITE/Antenna Select ACK
            case  6: is_ack = true; break; // Ref Source Select ACK
            case 10: is_ack = true; break; // AFC ACK
            case 12: is_ack = true; break; // RF Squelch ACK
            case 14: is_ack = true; break; // IQ Socket Open ACK
            case 16: is_ack = true; break; // IQ Socket Close ACK
            case 18: is_ack = true; break; // Spectrum Avg Count ACK
            case 20: is_ack = true; break; // Smart RF AGC ACK
            case 22: is_ack = true; break; // Audio Squelch ACK
            case 24: is_ack = true; break; // Set Date/Time ACK
            default: break;
        }
    }

    if (fn) {
        plen = fn(kwargs_json, payload, MAX_PAYLOAD);
        if (plen < 0) { std::free(payload); return -1; }
    }

    // Only fall through to payload_hex for truly unknown group/unit combinations
    // (e.g. raw stream responses). Known ACK responses skip this path.
    if (!is_ack && plen == 0) {
        const char* ph = std::strstr(kwargs_json, "\"payload_hex\"");
        if (ph) {
            const char* q = std::strchr(ph, ':');
            if (q) { q = std::strchr(q, '"'); }
            if (q) {
                ++q;
                while (*q && *q != '"') {
                    int hi = hex_nibble(*q++);
                    if (*q == '"' || !*q) break;
                    int lo = hex_nibble(*q++);
                    if (hi < 0 || lo < 0) { std::free(payload); return -1; }
                    if (plen >= MAX_PAYLOAD) { std::free(payload); return -1; }
                    payload[plen++] = static_cast<uint8_t>((hi << 4) | lo);
                }
            }
        }
    }

    int total = RESP_OVERHEAD + plen;
    uint8_t* buf = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(total)));
    if (!buf) { std::free(payload); return -1; }

    std::memcpy(buf, RESP_HEADER, MAGIC_LEN);
    store_i16le(buf + RESP_OFF_STATUS, static_cast<int16_t>(status));
    store_u32le(buf + RESP_OFF_SIZE,   static_cast<uint32_t>(plen));
    store_u16le(buf + RESP_OFF_GROUP,  static_cast<uint16_t>(group));
    store_u16le(buf + RESP_OFF_UNIT,   static_cast<uint16_t>(unit));
    if (plen > 0) std::memcpy(buf + RESP_OFF_PAYLOAD, payload, static_cast<size_t>(plen));
    std::memcpy(buf + RESP_OFF_PAYLOAD + plen, RESP_FOOTER, MAGIC_LEN);
    std::free(payload);
    *out_buf = buf;
    *out_len = static_cast<size_t>(total);
    return 0;
}

// =============================================================================
// ABI: free_result
// =============================================================================
extern "C" SDFC_EXPORT void free_result(void* ptr) {
    std::free(ptr);
}
