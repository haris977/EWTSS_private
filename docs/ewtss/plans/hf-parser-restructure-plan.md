# HF Parser Per-Group Restructure — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split `dp_ecm_hf_parser.cpp` (~3,900 lines) into 26 per-group files under `src/hf/parser/` and `src/hf/format/`, leaving the main file as a ~150-line thin ABI dispatcher, while preserving identical ABI behaviour.

**Architecture:** Pure structural refactor — zero logic changes. Each protocol group gets a `g{N}_parser.cpp` (all `decode_*` statics + `parse_cmd`/`parse_rsp` dispatcher) and a `g{N}_format.cpp` (all `encode_*` statics + `format` dispatcher). The main file calls one group function per `group_id`. Three shared headers carry utilities reused across groups.

**Tech Stack:** C++17, MSVC (cl.exe), CMake 3.20, Windows x64. DLL output: `dp_ecm_hf.dll`.

## Global Constraints

- ABI identical: `extract_frame`, `parse_message`, `format_response`, `free_result` — signatures and behaviour unchanged.
- `dp_ecm_vu_parser.cpp` and `tests/test_frames_vu.cpp` — **do not touch**.
- `include/` headers — **do not modify**.
- `dp_ecm_hf.def` — **do not modify**.
- All `decode_*` / `encode_*` helpers stay `static` inside their new `.cpp` — no new exports.
- Every new `.cpp` starts with `#include "sdfc_endian.h"`, `#include "json_writer.h"`, `#include "hf_groups.h"`, and `using namespace sdfc;`.
- Format files additionally include `#include "hf_json_utils.h"`.
- Parser files for groups 101 and 200 additionally include `#include "hf_shared.h"`.
- `MAX_PAYLOAD`, `RESP_*` constants — defined in `include/sdfc_frame.h`, already available in main.
- Build directory: `drs-bridge/parsers/dp_ecm/build/`

---

## Baseline — establish before Task 1

```
cmake --build drs-bridge/parsers/dp_ecm/build --config Release
drs-bridge/parsers/dp_ecm/build/Release/test_dp_ecm.exe
```

Expected: all assertions pass, process exits 0. Record this as the green baseline.

> **TDD note:** This is a pure refactor — no new behaviour to specify with failing tests. The discipline here is: *green before every task, green after every task*. The existing `test_frames.cpp` covers the ABI. Run it after every commit below.

---

## File Map

| Action | Path |
|---|---|
| Create | `src/hf/hf_groups.h` |
| Create | `src/hf/hf_json_utils.h` |
| Create | `src/hf/hf_shared.h` |
| Create | `src/hf/parser/g100_parser.cpp` |
| Create | `src/hf/format/g100_format.cpp` |
| Create | `src/hf/parser/g101_parser.cpp` |
| Create | `src/hf/format/g101_format.cpp` |
| Create | `src/hf/parser/g106_parser.cpp` |
| Create | `src/hf/format/g106_format.cpp` |
| Create | `src/hf/parser/g109_parser.cpp` |
| Create | `src/hf/format/g109_format.cpp` |
| Create | `src/hf/parser/g111_parser.cpp` |
| Create | `src/hf/format/g111_format.cpp` |
| Create | `src/hf/parser/g112_parser.cpp` |
| Create | `src/hf/format/g112_format.cpp` |
| Create | `src/hf/parser/g200_parser.cpp` |
| Create | `src/hf/format/g200_format.cpp` |
| Create | `src/hf/parser/mrx_g1_parser.cpp` |
| Create | `src/hf/format/mrx_g1_format.cpp` |
| Create | `src/hf/parser/mrx_g3_parser.cpp` |
| Create | `src/hf/format/mrx_g3_format.cpp` |
| Create | `src/hf/parser/mrx_g4_parser.cpp` |
| Create | `src/hf/format/mrx_g4_format.cpp` |
| Create | `src/hf/parser/mrx_g5_parser.cpp` |
| Create | `src/hf/format/mrx_g5_format.cpp` |
| Create | `src/hf/parser/mrx_g6_parser.cpp` |
| Create | `src/hf/format/mrx_g6_format.cpp` |
| Create | `src/hf/parser/mrx_g7_parser.cpp` |
| Create | `src/hf/format/mrx_g7_format.cpp` |
| Modify | `src/dp_ecm_hf_parser.cpp` |
| Modify | `CMakeLists.txt` |

All paths are relative to `drs-bridge/parsers/dp_ecm/`.

---

## Task 1: Scaffolding — directories, CMakeLists, shared headers

**Files:**
- Create: `src/hf/hf_groups.h`
- Create: `src/hf/hf_json_utils.h`
- Create: `src/hf/hf_shared.h`
- Modify: `CMakeLists.txt`
- Modify: `src/dp_ecm_hf_parser.cpp` (strip shared statics, add includes)

**Interfaces:**
- Produces: `hf_groups.h` (stub, grows per task), `hf_json_utils.h`, `hf_shared.h` — all group files consume these.

- [ ] **Step 1: Create `src/hf/` and subdirectories**

```
mkdir drs-bridge/parsers/dp_ecm/src/hf
mkdir drs-bridge/parsers/dp_ecm/src/hf/parser
mkdir drs-bridge/parsers/dp_ecm/src/hf/format
```

- [ ] **Step 2: Create `src/hf/hf_json_utils.h`**

Move the four static helpers from `dp_ecm_hf_parser.cpp` (lines ~2537–2813) into this header as `inline` functions. The bodies are unchanged — just add `inline` and remove `static`.

```cpp
// drs-bridge/parsers/dp_ecm/src/hf/hf_json_utils.h
#pragma once
#include <cstring>
#include <cstdlib>
#include <string>
#include <cstdio>

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
```

- [ ] **Step 3: Create `src/hf/hf_shared.h`**

Move four decode functions used by both groups 101 and 200, plus `decode_mrx_channel_cmd` used by groups 4, 5, and 7, into this header as `inline`. Bodies are verbatim from `dp_ecm_hf_parser.cpp`. Their original line ranges:

| Function | Original lines (approx) |
|---|---|
| `decode_cmd_send_ecm_reports` | ~975–981 |
| `decode_cmd_start_follow_on_jam` | ~830–858 |
| `decode_cmd_start_list_jam` | ~859–931 |
| `decode_cmd_start_responsive_sweep_jam` | ~932–974 |
| `decode_mrx_channel_cmd` | ~1991–1995 |

```cpp
// drs-bridge/parsers/dp_ecm/src/hf/hf_shared.h
#pragma once
#include <cstdint>
#include "json_writer.h"
#include "hf_json_utils.h"
using namespace sdfc;

// Decode functions shared by multiple groups — bodies verbatim from original.
inline void decode_cmd_send_ecm_reports(const uint8_t* p, int n, JsonWriter& w) { /* verbatim body */ }
inline void decode_cmd_start_follow_on_jam(const uint8_t* p, int n, JsonWriter& w) { /* verbatim body */ }
inline void decode_cmd_start_list_jam(const uint8_t* p, int n, JsonWriter& w) { /* verbatim body */ }
inline void decode_cmd_start_responsive_sweep_jam(const uint8_t* p, int n, JsonWriter& w) { /* verbatim body */ }
inline void decode_mrx_channel_cmd(const uint8_t* p, int n, JsonWriter& w) { /* verbatim body */ }
```

> Copy function bodies verbatim from the original file. Replace `static void` with `inline void`.

- [ ] **Step 4: Create `src/hf/hf_groups.h` (stub)**

```cpp
// drs-bridge/parsers/dp_ecm/src/hf/hf_groups.h
#pragma once
#include <cstdint>
#include "json_writer.h"
using namespace sdfc;

// Dispatcher declarations — one parse_cmd, parse_rsp, format per group.
// Populated incrementally by Tasks 2–12.
```

This file grows as each group task adds its three declarations. Leave it as a stub for now.

- [ ] **Step 5: Update `CMakeLists.txt`**

Replace the existing `add_library(dp_ecm_hf ...)` and `add_executable(test_dp_ecm ...)` blocks:

```cmake
# Collect group sources
file(GLOB HF_PARSER_SRCS src/hf/parser/*.cpp)
file(GLOB HF_FORMAT_SRCS src/hf/format/*.cpp)

add_library(dp_ecm_hf SHARED
    ${SDFC_CORE_SRC}
    src/dp_ecm_hf_parser.cpp
    ${HF_PARSER_SRCS}
    ${HF_FORMAT_SRCS}
)
target_include_directories(dp_ecm_hf PRIVATE
    include
    src/hf
)

if(MSVC)
    target_compile_options(dp_ecm_hf PRIVATE /W4 /permissive-)
    set_target_properties(dp_ecm_hf PROPERTIES LINK_FLAGS
        "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/dp_ecm_hf.def")
else()
    target_compile_options(dp_ecm_hf PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(test_dp_ecm
    tests/test_frames.cpp
    ${SDFC_CORE_SRC}
    src/dp_ecm_hf_parser.cpp
    ${HF_PARSER_SRCS}
    ${HF_FORMAT_SRCS}
)
target_include_directories(test_dp_ecm PRIVATE
    include
    src/hf
)
if(NOT MSVC)
    target_compile_options(test_dp_ecm PRIVATE -Wall -Wextra)
endif()
add_test(NAME dp_ecm_frames COMMAND test_dp_ecm)
```

- [ ] **Step 6: Strip shared statics from `dp_ecm_hf_parser.cpp` and add includes**

In `dp_ecm_hf_parser.cpp`, after the existing `#include` block (around line 33), add:

```cpp
#include "hf/hf_groups.h"
#include "hf/hf_json_utils.h"
#include "hf/hf_shared.h"
```

Then delete from `dp_ecm_hf_parser.cpp` the following static definitions (they now live in the headers):
- `static bool json_find_int(...)` (~line 2537)
- `static bool json_find_double(...)` (~line 2552)
- `static int hex_nibble(...)` (~line 2565)
- `static void parse_toa_hms(...)` (~line 2793)
- `static void decode_cmd_send_ecm_reports(...)` (~line 975)
- `static void decode_cmd_start_follow_on_jam(...)` (~line 830)
- `static void decode_cmd_start_list_jam(...)` (~line 859)
- `static void decode_cmd_start_responsive_sweep_jam(...)` (~line 932)
- `static void decode_mrx_channel_cmd(...)` (~line 1991)

- [ ] **Step 7: Re-run CMake configure then build**

```
cmake -S drs-bridge/parsers/dp_ecm -B drs-bridge/parsers/dp_ecm/build
cmake --build drs-bridge/parsers/dp_ecm/build --config Release
```

Expected: zero errors, zero warnings about undefined symbols.

- [ ] **Step 8: Run tests**

```
drs-bridge/parsers/dp_ecm/build/Release/test_dp_ecm.exe
```

Expected: all pass.

- [ ] **Step 9: Commit**

```
git add drs-bridge/parsers/dp_ecm/src/hf/ drs-bridge/parsers/dp_ecm/CMakeLists.txt drs-bridge/parsers/dp_ecm/src/dp_ecm_hf_parser.cpp
git commit -m "refactor(hf): scaffolding — hf/ dirs, shared headers, CMakeLists"
```

---

## Task 2: Group 100 — SJC Diagnostics

**Files:**
- Create: `src/hf/parser/g100_parser.cpp`
- Create: `src/hf/format/g100_format.cpp`
- Modify: `src/hf/hf_groups.h` (add declarations)
- Modify: `src/dp_ecm_hf_parser.cpp` (replace group 100 switch blocks with calls)

**Interfaces:**
- Produces: `g100_parse_cmd`, `g100_parse_rsp`, `g100_format`

- [ ] **Step 1: Create `src/hf/parser/g100_parser.cpp`**

Move the following `static void decode_*` functions verbatim from `dp_ecm_hf_parser.cpp` into this file (copy bodies exactly, keep `static`):

| Function | Original lines (approx) |
|---|---|
| `decode_system_version` | 55–63 |
| `decode_srx_checksum` | 67–71 |
| `decode_pbit_status` | 79–103 |
| `decode_ibit_status` | 106–116 |
| `decode_temperature` | 119–127 |
| `decode_fan_speed_status` | 130–133 |
| `decode_uart_test` | 136–141 |
| `decode_cbit_status` | 144–150 |
| `decode_cmd_uart_port_select` | 158–161 |
| `decode_ethernet_test` | ~1202–1210 |
| `decode_fan_voltage_status` | ~1211–1221 |
| `decode_pps_test` | ~1222–1231 |
| `decode_fpga_temperature_details` | ~1232–1237 |
| `decode_cmd_set_fan_speed` | ~1238–1247 |

Then append the two dispatcher functions:

```cpp
// drs-bridge/parsers/dp_ecm/src/hf/parser/g100_parser.cpp
#include "sdfc_endian.h"
#include "json_writer.h"
#include "hf_groups.h"
using namespace sdfc;

// ... (moved static decode_* functions above) ...

bool g100_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  1: return true;                                  // Get System Version — 0 bytes
        case  3: return true;                                  // Get SRX Checksum — 0 bytes
        case  5: return true;                                  // Get PBIT Status — 0 bytes
        case  7: return true;                                  // Get IBIT Status — 0 bytes
        case  9: return true;                                  // Get Temperature — 0 bytes
        case 11: decode_cmd_set_fan_speed(p, n, w);           return true;
        case 13: return true;                                  // Fan Speed Status — 0 bytes
        case 15: return true;                                  // Ethernet Test — 0 bytes
        case 17: decode_cmd_uart_port_select(p, n, w);        return true;
        case 21: return true;                                  // Fan Voltage Status — 0 bytes
        case 23: return true;                                  // PPS Test — 0 bytes
        case 25: return true;                                  // RS422 Test — 0 bytes
        case 27: return true;                                  // FPGA Temperature — 0 bytes
        case 29: return true;                                  // CBIT Status — 0 bytes
        default: return false;
    }
}

bool g100_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  2: decode_system_version(p, n, w);           return true;
        case  4: decode_srx_checksum(p, n, w);             return true;
        case  6: decode_pbit_status(p, n, w);              return true;
        case  8: decode_ibit_status(p, n, w);              return true;
        case 10: decode_temperature(p, n, w);              return true;
        case 12: return true;                              // Set Fan Speed ACK
        case 14: decode_fan_speed_status(p, n, w);         return true;
        case 16: decode_ethernet_test(p, n, w);            return true;
        case 18: decode_uart_test(p, n, w);                return true;
        case 22: decode_fan_voltage_status(p, n, w);       return true;
        case 24: decode_pps_test(p, n, w);                 return true;
        case 26: return true;                              // RS422 Test Status
        case 28: decode_fpga_temperature_details(p, n, w); return true;
        case 30: decode_cbit_status(p, n, w);              return true;
        default: return false;
    }
}
```

- [ ] **Step 2: Create `src/hf/format/g100_format.cpp`**

Move the following `static int encode_*` functions verbatim from `dp_ecm_hf_parser.cpp`:

| Function | Original lines (approx) |
|---|---|
| `encode_system_version` | ~2818–2860 |
| `encode_srx_checksum` | ~2861–2875 |
| `encode_pbit_status` | ~2876–2910 |
| `encode_ibit_status` | ~2911–2930 |
| `encode_temperature` | ~2931–2945 |
| `encode_fan_speed_status` | ~2946–2953 |
| `encode_uart_test` | ~2954–2963 (check location) |
| `encode_ethernet_test` | (find by name in format section) |
| `encode_fan_voltage_status` | (find by name) |
| `encode_pps_test` | (find by name) |
| `encode_fpga_temperature_details` | (find by name) |
| `encode_cbit_status` | (find by name) |

Then append:

```cpp
// drs-bridge/parsers/dp_ecm/src/hf/format/g100_format.cpp
#include "sdfc_endian.h"
#include "json_writer.h"
#include "hf_groups.h"
#include "hf_json_utils.h"
using namespace sdfc;

// ... (moved static encode_* functions above) ...

int g100_format(long long unit_id, const char* json, uint8_t* buf, int max_len, bool& is_ack) {
    typedef int (*EncFn)(const char*, uint8_t*, int);
    EncFn fn = nullptr;
    switch (unit_id) {
        case  2: fn = encode_system_version;           break;
        case  4: fn = encode_srx_checksum;             break;
        case  6: fn = encode_pbit_status;              break;
        case  8: fn = encode_ibit_status;              break;
        case 10: fn = encode_temperature;              break;
        case 12: is_ack = true;                        break; // Set Fan Speed ACK
        case 14: fn = encode_fan_speed_status;         break;
        case 16: fn = encode_ethernet_test;            break;
        case 18: fn = encode_uart_test;                break;
        case 22: fn = encode_fan_voltage_status;       break;
        case 24: fn = encode_pps_test;                 break;
        case 26: is_ack = true;                        break; // RS422 Test ACK
        case 28: fn = encode_fpga_temperature_details; break;
        case 30: fn = encode_cbit_status;              break;
        default: return 0; // unknown unit — caller falls through to payload_hex
    }
    if (is_ack) return 0;
    return fn(json, buf, max_len);
}
```

- [ ] **Step 3: Add declarations to `hf_groups.h`**

```cpp
// Group 100 — SJC Diagnostics
bool g100_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
bool g100_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
int  g100_format   (long long unit_id, const char* json,
                    uint8_t* buf, int max_len, bool& is_ack);
```

- [ ] **Step 4: Replace group 100 blocks in `dp_ecm_hf_parser.cpp`**

In `parse_message`, find the `if (hdr.group_id == 100)` command block and replace it with:
```cpp
if (hdr.group_id == 100) decoded = g100_parse_cmd(hdr.unit_id, payload, plen, w);
```

Find the `if (hdr.group_id == 100)` response block and replace it with:
```cpp
if (hdr.group_id == 100) decoded = g100_parse_rsp(hdr.unit_id, payload, plen, w);
```

In `format_response`, find the `if (group == 100)` block and replace it with:
```cpp
if (group == 100) plen = g100_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack);
```

Delete all `decode_*` functions listed in Step 1 from `dp_ecm_hf_parser.cpp` (they are now in `g100_parser.cpp`). Delete all `encode_*` functions listed in Step 2 (now in `g100_format.cpp`).

- [ ] **Step 5: Build**

```
cmake --build drs-bridge/parsers/dp_ecm/build --config Release
```

Expected: zero errors.

- [ ] **Step 6: Test**

```
drs-bridge/parsers/dp_ecm/build/Release/test_dp_ecm.exe
```

Expected: all pass.

- [ ] **Step 7: Commit**

```
git add drs-bridge/parsers/dp_ecm/src/hf/ drs-bridge/parsers/dp_ecm/src/dp_ecm_hf_parser.cpp
git commit -m "refactor(hf): extract group 100 (SJC diagnostics) into parser/ and format/"
```

---

## Task 3: Group 101 — SJC Detection + Jamming

**Files:**
- Create: `src/hf/parser/g101_parser.cpp`
- Create: `src/hf/format/g101_format.cpp`
- Modify: `src/hf/hf_groups.h`, `src/dp_ecm_hf_parser.cpp`

- [ ] **Step 1: Create `src/hf/parser/g101_parser.cpp`**

Include `"hf_shared.h"` (for the four shared decode functions).

Move these `decode_*` functions verbatim (they remain `static`):

| Function | Original lines (approx) |
|---|---|
| `decode_cmd_set_threshold` | ~169 |
| `decode_cmd_set_resolution` | ~176 |
| `decode_cmd_configure_detection` | ~188 |
| `decode_cmd_start_fh_detection` | ~205 |
| `decode_cmd_get_wideband_fft` | ~212 |
| `decode_cmd_set_pulse_range` | ~221 |
| `decode_cmd_set_min_hops` | ~230 |
| `decode_cmd_start_ff_detection` | ~236 |
| `decode_cmd_start_burst_detection` | ~245 |
| `decode_cmd_start_scan_speed` | ~254 |
| `decode_cmd_get_zoom_fft` | ~263 |
| `decode_cmd_terminate_fft` | ~280 |
| `decode_fh_detection` | ~296 |
| `decode_wideband_fft` | ~333 |
| `decode_ff_entry_hf` | ~356 |
| `decode_ff_detection` | ~375 |
| `decode_burst_detection` | ~424 |
| `decode_stop_scan_speed` | ~457 |
| `decode_zoom_fft` | ~480 |
| `decode_cmd_tracking_config` | ~1248 |
| `decode_cmd_set_flatness_mode` | ~1257 |
| `decode_cmd_set_integration_time` | ~1269 |
| `decode_cmd_set_multi_band_fh` | ~1281 |
| `decode_cmd_set_narrow_band_fh` | ~1288 |

Then append:

```cpp
bool g101_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  25: decode_cmd_set_threshold(p, n, w);                    return true;
        case  27: decode_cmd_set_resolution(p, n, w);                   return true;
        case  31: decode_cmd_start_follow_on_jam(p, n, w);              return true;
        case  33: return true;                                           // Stop Follow-on Jam — 0 bytes
        case  37: decode_cmd_configure_detection(p, n, w);              return true;
        case  39: decode_cmd_start_fh_detection(p, n, w);               return true;
        case  43: decode_cmd_get_wideband_fft(p, n, w);                 return true;
        case  47: decode_cmd_set_pulse_range(p, n, w);                  return true;
        case  55: decode_cmd_set_min_hops(p, n, w);                     return true;
        case  63: decode_cmd_tracking_config(p, n, w);                  return true;
        case  69: decode_cmd_start_ff_detection(p, n, w);               return true;
        case  73: decode_cmd_start_list_jam(p, n, w);                   return true;
        case  75: return true;                                           // Stop List Jam — 0 bytes
        case  79: decode_cmd_send_ecm_reports(p, n, w);                 return true;
        case  83: decode_cmd_start_burst_detection(p, n, w);            return true;
        case  85: decode_cmd_start_scan_speed(p, n, w);                 return true;
        case  87: return true;                                           // Stop Scan Speed — 0 bytes
        case  92: decode_cmd_start_responsive_sweep_jam(p, n, w);       return true;
        case  94: decode_cmd_get_zoom_fft(p, n, w);                     return true;
        case 100: decode_cmd_set_flatness_mode(p, n, w);                return true;
        case 102: decode_cmd_set_integration_time(p, n, w);             return true;
        case 104: decode_cmd_set_multi_band_fh(p, n, w);                return true;
        case 106: decode_cmd_set_narrow_band_fh(p, n, w);               return true;
        case 140: return true;                                           // HF-specific — 0 bytes
        case 158: decode_cmd_terminate_fft(p, n, w);                    return true;
        case 160: case 162: case 164:
        case 174: case 176: case 178: return true;                       // HF slow scan — 0 bytes
        case 182: case 184: case 186: return true;                       // HF fast scan — 0 bytes
        case 200: case 202: return true;                                 // HF wideband — 0 bytes
        case 204: return true;                                           // HF data query — 0 bytes
        case 210: return true;                                           // cross-group cmd — 0 bytes
        default: return false;
    }
}

bool g101_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  26: return true;                                           // Set Threshold ACK
        case  28: return true;                                           // Set Resolution ACK
        case  32: return true;                                           // Start Follow-on Jam ACK
        case  34: return true;                                           // Stop Follow-on Jam ACK
        case  38: return true;                                           // Configure Detection ACK
        case  40: decode_fh_detection(p, n, w);                         return true;
        case  44: decode_wideband_fft(p, n, w);                         return true;
        case  48: return true;                                           // Set Pulse Range ACK
        case  56: return true;                                           // Set Min Hops ACK
        case  64: return true;                                           // Auto/Manual Tracking ACK
        case  70: decode_ff_detection(p, n, w);                         return true;
        case  74: return true;                                           // Start List Jam ACK
        case  76: return true;                                           // Stop List Jam ACK
        case  80: return true;                                           // Send ECM Reports ACK
        case  84: decode_burst_detection(p, n, w);                      return true;
        case  86: return true;                                           // Start Scan Speed ACK
        case  88: decode_stop_scan_speed(p, n, w);                      return true;
        case  93: return true;                                           // Start Responsive Sweep Jam ACK
        case  95: decode_zoom_fft(p, n, w);                             return true;
        case 101: return true;                                           // Set Flatness Mode ACK
        case 103: return true;                                           // Set Integration Time ACK
        case 105: return true;                                           // Set Multi-Band FH ACK
        case 107: return true;                                           // Set Narrow-Band FH ACK
        case 141: return true;                                           // HF-specific ACK
        case 159: return true;                                           // Terminate FFT ACK
        case 161: case 163: case 165:
        case 175: case 177: case 179: return true;                       // HF slow scan ACKs
        case 183: case 185: case 187: return true;                       // HF fast scan ACKs
        case 201: case 203: return true;                                 // HF wideband ACKs
        default: return false;
    }
}
```

- [ ] **Step 2: Create `src/hf/format/g101_format.cpp`**

Move these `encode_*` functions verbatim from the format section of `dp_ecm_hf_parser.cpp`:

| Function | Look for in lines ~2570–2953 |
|---|---|
| `encode_fh_detection` | ~2580 |
| `encode_wideband_fft` | (find by name) |
| `encode_ff_detection` | (find by name) |
| `encode_burst_detection` | (find by name) |
| `encode_stop_scan_speed` | (find by name) |
| `encode_zoom_fft` | (find by name) |

Then append:

```cpp
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
```

- [ ] **Step 3: Add to `hf_groups.h`**

```cpp
// Group 101 — SJC Detection + Jamming
bool g101_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
bool g101_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
int  g101_format   (long long unit_id, const char* json,
                    uint8_t* buf, int max_len, bool& is_ack);
```

- [ ] **Step 4: Replace group 101 blocks in `dp_ecm_hf_parser.cpp`**

Replace `else if (hdr.group_id == 101)` command switch with:
```cpp
else if (hdr.group_id == 101) decoded = g101_parse_cmd(hdr.unit_id, payload, plen, w);
```

Replace `else if (hdr.group_id == 101)` response switch with:
```cpp
else if (hdr.group_id == 101) decoded = g101_parse_rsp(hdr.unit_id, payload, plen, w);
```

Replace `else if (group == 101)` format block with:
```cpp
else if (group == 101) plen = g101_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack);
```

Delete all moved `decode_*` and `encode_*` functions from `dp_ecm_hf_parser.cpp`.

- [ ] **Step 5: Build, test, commit**

```
cmake --build drs-bridge/parsers/dp_ecm/build --config Release
drs-bridge/parsers/dp_ecm/build/Release/test_dp_ecm.exe
git add drs-bridge/parsers/dp_ecm/src/hf/ drs-bridge/parsers/dp_ecm/src/dp_ecm_hf_parser.cpp
git commit -m "refactor(hf): extract group 101 (SJC detection+jamming)"
```

---

## Task 4: Group 106 — Immediate Jamming

**Files:**
- Create: `src/hf/parser/g106_parser.cpp`
- Create: `src/hf/format/g106_format.cpp`
- Modify: `src/hf/hf_groups.h`, `src/dp_ecm_hf_parser.cpp`

- [ ] **Step 1: Create `src/hf/parser/g106_parser.cpp`**

Move these decode functions verbatim:

| Function | Original lines (approx) |
|---|---|
| `decode_cmd_start_immediate_jam` | ~982 |
| `decode_cmd_generate_multi_freq_tdm` | ~991 |
| `decode_cmd_generate_multi_carrier_fdm` | ~1012 |
| `decode_cmd_configure_ext_modulation` | ~1029 |
| `decode_cmd_generate_sweep_freq` | ~1048 |
| `decode_cmd_generate_comb_noise` | ~1089 |
| `decode_cmd_enable_pa` | ~1106 |
| `decode_cmd_enable_pa_sdu` | ~1113 |
| `decode_cmd_configure_prog_exciter` | ~1120 |
| `decode_list_jam_report` | ~1142 |
| `decode_ext_modulation_response` | ~1170 |
| `decode_stop_immediate_jam_rsp` | ~1177 |
| `decode_immediate_jam_ack` | ~1186 |

Then append:

```cpp
bool g106_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  1: decode_cmd_start_immediate_jam(p, n, w);          return true;
        case  3: decode_cmd_generate_multi_freq_tdm(p, n, w);      return true;
        case  5: decode_cmd_generate_multi_carrier_fdm(p, n, w);   return true;
        case  9: return true;                                       // Stop Immediate Jamming — 0 bytes
        case 21: decode_cmd_enable_pa(p, n, w);                    return true;
        case 39: decode_cmd_configure_ext_modulation(p, n, w);     return true;
        case 41: decode_cmd_generate_sweep_freq(p, n, w);          return true;
        case 45: decode_cmd_enable_pa_sdu(p, n, w);                return true;
        case 49: decode_cmd_configure_prog_exciter(p, n, w);       return true;
        case 55: decode_cmd_generate_comb_noise(p, n, w);          return true;
        default: return false;
    }
}

bool g106_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  2: return true;                                       // Start Immediate Jam ACK
        case  4: return true;                                       // TDM Jam ACK
        case  6: return true;                                       // FDM Jam ACK
        case 10: decode_stop_immediate_jam_rsp(p, n, w);           return true;
        case 22: return true;                                       // Enable PA ACK
        case 40: decode_ext_modulation_response(p, n, w);          return true;
        case 42: return true;                                       // Sweep Jam ACK
        case 46: return true;                                       // Enable PA+SDU ACK
        case 50: return true;                                       // Configure Prog Exciter ACK
        case 54: decode_immediate_jam_ack(p, n, w);                return true;
        case 56: return true;                                       // Comb Noise ACK
        default: return false;
    }
}
```

- [ ] **Step 2: Create `src/hf/format/g106_format.cpp`**

Move these encode functions verbatim (find them in the format section, ~lines 3256–3378):

| Function |
|---|
| `encode_stop_immediate_jam_rsp` |
| `encode_ext_modulation_response` |
| `encode_immediate_jam_ack` |
| `encode_list_jam_report` |

Then append:

```cpp
int g106_format(long long unit_id, const char* json, uint8_t* buf, int max_len, bool& is_ack) {
    typedef int (*EncFn)(const char*, uint8_t*, int);
    EncFn fn = nullptr;
    switch (unit_id) {
        case  2: is_ack = true; break; // Start Immediate Jam ACK
        case  4: is_ack = true; break; // TDM Jam ACK
        case  6: is_ack = true; break; // FDM Jam ACK
        case 10: fn = encode_stop_immediate_jam_rsp;  break;
        case 22: is_ack = true; break; // Enable PA ACK
        case 40: fn = encode_ext_modulation_response; break;
        case 42: is_ack = true; break; // Sweep Jam ACK
        case 46: is_ack = true; break; // Enable PA+SDU ACK
        case 50: is_ack = true; break; // Configure Prog Exciter ACK
        case 54: fn = encode_immediate_jam_ack;       break;
        case 56: is_ack = true; break; // Comb Noise ACK
        default: return 0;
    }
    if (is_ack) return 0;
    return fn(json, buf, max_len);
}
```

- [ ] **Step 3: Add to `hf_groups.h`**

```cpp
// Group 106 — ECM Immediate Jamming
bool g106_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
bool g106_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
int  g106_format   (long long unit_id, const char* json,
                    uint8_t* buf, int max_len, bool& is_ack);
```

- [ ] **Step 4: Replace group 106 blocks in `dp_ecm_hf_parser.cpp`, delete moved functions, build, test, commit**

```
cmake --build drs-bridge/parsers/dp_ecm/build --config Release
drs-bridge/parsers/dp_ecm/build/Release/test_dp_ecm.exe
git commit -m "refactor(hf): extract group 106 (immediate jamming)"
```

---

## Task 5: Group 109 — Date/Time

**Files:**
- Create: `src/hf/parser/g109_parser.cpp`
- Create: `src/hf/format/g109_format.cpp`
- Modify: `src/hf/hf_groups.h`, `src/dp_ecm_hf_parser.cpp`

- [ ] **Step 1: Create `src/hf/parser/g109_parser.cpp`**

Move these verbatim:

| Function | Original lines (approx) |
|---|---|
| `decode_cmd_set_date_time` | ~502 |
| `decode_hopper_channelization` | ~518 |
| `decode_auto_threshold_value` | ~553 |

Then append:

```cpp
bool g109_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case 11: decode_cmd_set_date_time(p, n, w); return true;
        case 15: return true; // Send Auto Threshold Value — 0 bytes
        case 17: return true; // Acquire Hopper Channelization — 0 bytes
        default: return false;
    }
}

bool g109_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case 12: return true;                              // Set Date/Time ACK
        case 16: decode_auto_threshold_value(p, n, w);    return true;
        case 18: decode_hopper_channelization(p, n, w);   return true;
        default: return false;
    }
}
```

- [ ] **Step 2: Create `src/hf/format/g109_format.cpp`**

Move these encode functions verbatim (find in lines ~3030–3101):

| Function |
|---|
| `encode_auto_threshold_value` |
| `encode_hopper_channelization` |

Then append:

```cpp
int g109_format(long long unit_id, const char* json, uint8_t* buf, int max_len, bool& is_ack) {
    typedef int (*EncFn)(const char*, uint8_t*, int);
    EncFn fn = nullptr;
    switch (unit_id) {
        case 12: is_ack = true;                     break; // Set Date/Time ACK
        case 16: fn = encode_auto_threshold_value;  break;
        case 18: fn = encode_hopper_channelization; break;
        default: return 0;
    }
    if (is_ack) return 0;
    return fn(json, buf, max_len);
}
```

- [ ] **Step 3: Add to `hf_groups.h`, replace in main, delete moved functions, build, test, commit**

```cpp
// Group 109 — Date/Time
bool g109_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
bool g109_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
int  g109_format   (long long unit_id, const char* json,
                    uint8_t* buf, int max_len, bool& is_ack);
```

```
cmake --build drs-bridge/parsers/dp_ecm/build --config Release
drs-bridge/parsers/dp_ecm/build/Release/test_dp_ecm.exe
git commit -m "refactor(hf): extract group 109 (date/time)"
```

---

## Task 6: Group 111 — Signal/Scan

**Files:**
- Create: `src/hf/parser/g111_parser.cpp`
- Create: `src/hf/format/g111_format.cpp`
- Modify: `src/hf/hf_groups.h`, `src/dp_ecm_hf_parser.cpp`

- [ ] **Step 1: Create `src/hf/parser/g111_parser.cpp`**

Move these verbatim:

| Function | Original lines (approx) |
|---|---|
| `decode_cmd_signal_bite` | ~577 |
| `decode_cmd_signal_bite_band` | ~587 |
| `decode_cmd_reference_input` | ~593 |
| `decode_cmd_send_protected_scan_list` | ~602 |
| `decode_cmd_protected_scan_enable` | ~625 |
| `decode_cmd_fh_splitband_enable` | ~631 |
| `decode_cmd_send_fh_splitband_freq` | ~637 |
| `decode_signal_bite_resp` | ~648 |
| `decode_bite_observed_rsp` | ~660 |
| `decode_pdw_channelization` | ~673 |
| `decode_storage_details` | ~706 |
| `decode_module_health` | ~1300 |
| `decode_cmd_spectrum_protected_band` | ~1309 |
| `decode_read_protected_band_list` | ~1317 |
| `decode_cmd_auto_threshold_enable` | ~1341 |
| `decode_cmd_hopper_channelization` | ~1349 |

Then append:

```cpp
bool g111_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  3: decode_cmd_signal_bite(p, n, w);               return true;
        case  5: decode_cmd_reference_input(p, n, w);           return true;
        case  7: return true;                                    // Module HEALTH Status — 0 bytes
        case  9: decode_cmd_send_protected_scan_list(p, n, w);  return true;
        case 13: decode_cmd_protected_scan_enable(p, n, w);     return true;
        case 15: return true;                                    // PDW Channelization — 0 bytes
        case 17: decode_cmd_fh_splitband_enable(p, n, w);       return true;
        case 19: decode_cmd_send_fh_splitband_freq(p, n, w);    return true;
        case 21: decode_cmd_signal_bite_band(p, n, w);          return true;
        case 23: decode_cmd_spectrum_protected_band(p, n, w);   return true;
        case 25: return true;                                    // Get Storage Details — 0 bytes
        case 27: return true;                                    // Read Protected Band List — 0 bytes
        case 29: decode_cmd_auto_threshold_enable(p, n, w);     return true;
        case 31: decode_cmd_hopper_channelization(p, n, w);     return true;
        default: return false;
    }
}

bool g111_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  4: decode_signal_bite_resp(p, n, w);       return true;
        case  6: return true;                            // Reference Input ACK
        case  8: decode_module_health(p, n, w);         return true;
        case 10: return true;                            // Send Protected Scan List ACK
        case 14: return true;                            // Protected Scan Enable ACK
        case 16: decode_pdw_channelization(p, n, w);    return true;
        case 18: return true;                            // FH Splitband Enable ACK
        case 20: return true;                            // FH Splitband Freq ACK
        case 22: decode_bite_observed_rsp(p, n, w);     return true;
        case 24: return true;                            // Spectrum Protected Band ACK
        case 26: decode_storage_details(p, n, w);       return true;
        case 28: decode_read_protected_band_list(p, n, w); return true;
        case 30: return true;                            // Auto Threshold Enable ACK
        case 32: return true;                            // Hopper Channelization ACK
        default: return false;
    }
}
```

- [ ] **Step 2: Create `src/hf/format/g111_format.cpp`**

Move these encode functions verbatim (find in lines ~3102–3185):

| Function |
|---|
| `encode_signal_bite_resp` |
| `encode_bite_observed_rsp` |
| `encode_pdw_channelization` |
| `encode_storage_details` |
| `encode_module_health` |
| `encode_read_protected_band_list` |

Then append:

```cpp
int g111_format(long long unit_id, const char* json, uint8_t* buf, int max_len, bool& is_ack) {
    typedef int (*EncFn)(const char*, uint8_t*, int);
    EncFn fn = nullptr;
    switch (unit_id) {
        case  4: fn = encode_signal_bite_resp;          break;
        case  6: is_ack = true;                         break; // Reference Input ACK
        case  8: fn = encode_module_health;             break;
        case 10: is_ack = true;                         break; // Send Protected Scan List ACK
        case 14: is_ack = true;                         break; // Protected Scan Enable ACK
        case 16: fn = encode_pdw_channelization;        break;
        case 18: is_ack = true;                         break; // FH Splitband Enable ACK
        case 20: is_ack = true;                         break; // FH Splitband Freq ACK
        case 22: fn = encode_bite_observed_rsp;         break;
        case 24: is_ack = true;                         break; // Spectrum Protected Band ACK
        case 26: fn = encode_storage_details;           break;
        case 28: fn = encode_read_protected_band_list;  break;
        case 30: is_ack = true;                         break; // Auto Threshold Enable ACK
        case 32: is_ack = true;                         break; // Hopper Channelization ACK
        default: return 0;
    }
    if (is_ack) return 0;
    return fn(json, buf, max_len);
}
```

- [ ] **Step 3: Add to `hf_groups.h`, replace in main, delete moved functions, build, test, commit**

```cpp
// Group 111 — Signal/Scan
bool g111_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
bool g111_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
int  g111_format   (long long unit_id, const char* json,
                    uint8_t* buf, int max_len, bool& is_ack);
```

```
cmake --build drs-bridge/parsers/dp_ecm/build --config Release
drs-bridge/parsers/dp_ecm/build/Release/test_dp_ecm.exe
git commit -m "refactor(hf): extract group 111 (signal/scan)"
```

---

## Task 7: Group 112 — Fast Scan / Simulation

**Files:**
- Create: `src/hf/parser/g112_parser.cpp`
- Create: `src/hf/format/g112_format.cpp`
- Modify: `src/hf/hf_groups.h`, `src/dp_ecm_hf_parser.cpp`

- [ ] **Step 1: Create `src/hf/parser/g112_parser.cpp`**

Move these verbatim:

| Function | Original lines (approx) |
|---|---|
| `decode_cmd_asu_sdu_config` | ~721 |
| `decode_cmd_auto_scan_band_config` | ~730 |
| `decode_cmd_simulation_mode_config` | ~756 |
| `decode_asu_sdu_config_rsp` | ~764 |
| `decode_trsdu_receiver_status` | ~772 |
| `decode_pa_receiver_status` | ~780 |

Then append:

```cpp
bool g112_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  1: decode_cmd_asu_sdu_config(p, n, w);         return true;
        case  3: return true;                                 // TRSDU Receiver Line Status — 0 bytes
        case  5: return true;                                 // PA Receiver Line Status — 0 bytes
        case 13: decode_cmd_simulation_mode_config(p, n, w); return true;
        case 37: return true;                                 // HF fast scan cmd — 0 bytes
        default: return false;
    }
}

bool g112_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  2: decode_asu_sdu_config_rsp(p, n, w);       return true;
        case  4: decode_trsdu_receiver_status(p, n, w);    return true;
        case  6: decode_pa_receiver_status(p, n, w);       return true;
        case 14: return true;                               // Simulation Mode Config ACK
        case 38: return true;                               // HF fast scan ACK
        default: return false;
    }
}
```

- [ ] **Step 2: Create `src/hf/format/g112_format.cpp`**

Move these encode functions (find in lines ~3186–3207):

| Function |
|---|
| `encode_asu_sdu_config_rsp` |
| `encode_trsdu_receiver_status` |
| `encode_pa_receiver_status` |

Then append:

```cpp
int g112_format(long long unit_id, const char* json, uint8_t* buf, int max_len, bool& is_ack) {
    typedef int (*EncFn)(const char*, uint8_t*, int);
    EncFn fn = nullptr;
    switch (unit_id) {
        case  2: fn = encode_asu_sdu_config_rsp;    break;
        case  4: fn = encode_trsdu_receiver_status; break;
        case  6: fn = encode_pa_receiver_status;    break;
        case 14: is_ack = true;                     break; // Simulation Mode Config ACK
        case 38: is_ack = true;                     break; // HF fast scan ACK
        default: return 0;
    }
    if (is_ack) return 0;
    return fn(json, buf, max_len);
}
```

- [ ] **Step 3: Add to `hf_groups.h`, replace in main, delete moved functions, build, test, commit**

```cpp
// Group 112 — Fast Scan / Simulation
bool g112_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
bool g112_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
int  g112_format   (long long unit_id, const char* json,
                    uint8_t* buf, int max_len, bool& is_ack);
```

```
cmake --build drs-bridge/parsers/dp_ecm/build --config Release
drs-bridge/parsers/dp_ecm/build/Release/test_dp_ecm.exe
git commit -m "refactor(hf): extract group 112 (fast scan/simulation)"
```

---

## Task 8: Group 200 — HF ECM Jamming

**Files:**
- Create: `src/hf/parser/g200_parser.cpp`
- Create: `src/hf/format/g200_format.cpp`
- Modify: `src/hf/hf_groups.h`, `src/dp_ecm_hf_parser.cpp`

- [ ] **Step 1: Create `src/hf/parser/g200_parser.cpp`**

Include `"hf_shared.h"` (reuses the four shared decode functions).

Move these verbatim (decode functions unique to group 200):

| Function | Original lines (approx) |
|---|---|
| `decode_jam_config_block` | ~792 |
| `decode_hpasu_health_status_cmd` | ~1447 |
| `decode_hpasu_health_status_rsp` | ~1456 |
| `decode_pa_sdu_health_status_rsp` | ~1464 |
| `decode_cmd_pa_soft_reboot` | ~1481 |

Then append:

```cpp
bool g200_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  9: decode_cmd_send_ecm_reports(p, n, w);              return true;
        case 11: decode_cmd_start_list_jam(p, n, w);                return true;
        case 13: return true;                                        // Stop List Jam — 0 bytes
        case 15: return true;                                        // Get List Jam Report — 0 bytes
        case 17: decode_cmd_start_follow_on_jam(p, n, w);           return true;
        case 19: return true;                                        // Stop Follow-on Jam — 0 bytes
        case 21: decode_cmd_start_responsive_sweep_jam(p, n, w);    return true;
        case 23: return true;                                        // Stop Responsive Sweep Jam — 0 bytes
        case 41: decode_hpasu_health_status_cmd(p, n, w);           return true;
        case 54: return true;                                        // PA/SDU Health Status — 0 bytes
        case 56: decode_cmd_pa_soft_reboot(p, n, w);                return true;
        default: return false;
    }
}

bool g200_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case 10: return true;                                        // Send ECM Reports ACK
        case 12: return true;                                        // Start List Jam ACK
        case 14: return true;                                        // Stop List Jam ACK
        case 16: return true;                                        // List Jam Report (decoded via format path)
        case 18: return true;                                        // Start Follow-on Jam ACK
        case 20: return true;                                        // Stop Follow-on Jam ACK
        case 22: return true;                                        // Start Responsive Sweep Jam ACK
        case 24: return true;                                        // Stop Responsive Sweep Jam ACK
        case 42: decode_hpasu_health_status_rsp(p, n, w);           return true;
        case 55: decode_pa_sdu_health_status_rsp(p, n, w);          return true;
        case 57: return true;                                        // PA Soft Reboot ACK
        default: return false;
    }
}
```

- [ ] **Step 2: Create `src/hf/format/g200_format.cpp`**

Move these encode functions verbatim (find in lines ~3208–3255):

| Function |
|---|
| `encode_list_jam_report` |
| `encode_hpasu_health_status_rsp` |
| `encode_pa_sdu_health_status_rsp` |

Then append:

```cpp
int g200_format(long long unit_id, const char* json, uint8_t* buf, int max_len, bool& is_ack) {
    typedef int (*EncFn)(const char*, uint8_t*, int);
    EncFn fn = nullptr;
    switch (unit_id) {
        case 10: is_ack = true;                           break; // Send ECM Reports ACK
        case 12: is_ack = true;                           break; // Start List Jam ACK
        case 14: is_ack = true;                           break; // Stop List Jam ACK
        case 16: fn = encode_list_jam_report;             break;
        case 18: is_ack = true;                           break; // Start Follow-on Jam ACK
        case 20: is_ack = true;                           break; // Stop Follow-on Jam ACK
        case 22: is_ack = true;                           break; // Start Responsive Sweep Jam ACK
        case 24: is_ack = true;                           break; // Stop Responsive Sweep Jam ACK
        case 42: fn = encode_hpasu_health_status_rsp;    break;
        case 55: fn = encode_pa_sdu_health_status_rsp;   break;
        case 57: is_ack = true;                           break; // PA Soft Reboot ACK
        default: return 0;
    }
    if (is_ack) return 0;
    return fn(json, buf, max_len);
}
```

- [ ] **Step 3: Add to `hf_groups.h`, replace in main, delete moved functions, build, test, commit**

```cpp
// Group 200 — HF ECM Jamming
bool g200_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
bool g200_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
int  g200_format   (long long unit_id, const char* json,
                    uint8_t* buf, int max_len, bool& is_ack);
```

```
cmake --build drs-bridge/parsers/dp_ecm/build --config Release
drs-bridge/parsers/dp_ecm/build/Release/test_dp_ecm.exe
git commit -m "refactor(hf): extract group 200 (HF ECM jamming)"
```

---

## Task 9: MRx Group 1 — Diagnostics

**Files:**
- Create: `src/hf/parser/mrx_g1_parser.cpp`
- Create: `src/hf/format/mrx_g1_format.cpp`
- Modify: `src/hf/hf_groups.h`, `src/dp_ecm_hf_parser.cpp`

- [ ] **Step 1: Create `src/hf/parser/mrx_g1_parser.cpp`**

Move these verbatim:

| Function | Original lines (approx) |
|---|---|
| `decode_mrx_system_version` | ~1496 |
| `decode_mrx_checksum` | ~1505 |
| `decode_mrx_pbit_status` | ~1512 |
| `decode_mrx_ibit_status` | ~1543 |
| `decode_mrx_temperature` | ~1561 |
| `decode_mrx_fan_speed` | ~1573 |
| `decode_mrx_uart_test_cmd` | ~1578 |
| `decode_mrx_uart_test_rsp` | ~1585 |
| `decode_mrx_cbit_status` | ~1594 |

Then append:

```cpp
bool mrx_g1_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  1: return true;                              // Get System Version — 0 bytes
        case  3: return true;                              // Get Checksum — 0 bytes
        case  5: return true;                              // PBIT — 0 bytes
        case  7: return true;                              // IBIT — 0 bytes
        case  9: return true;                              // Temperature — 0 bytes
        case 13: return true;                              // Fan Speed — 0 bytes
        case 17: decode_mrx_uart_test_cmd(p, n, w);       return true;
        case 25: return true;                              // CBIT — 0 bytes
        case 33: return true;                              // Close All Channels — 0 bytes
        default: return false;
    }
}

bool mrx_g1_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  2: decode_mrx_system_version(p, n, w); return true;
        case  4: decode_mrx_checksum(p, n, w);       return true;
        case  6: decode_mrx_pbit_status(p, n, w);    return true;
        case  8: decode_mrx_ibit_status(p, n, w);    return true;
        case 10: decode_mrx_temperature(p, n, w);    return true;
        case 14: decode_mrx_fan_speed(p, n, w);      return true;
        case 18: decode_mrx_uart_test_rsp(p, n, w);  return true;
        case 26: decode_mrx_cbit_status(p, n, w);    return true;
        case 34: return true;                         // Close All Channels ACK
        default: return false;
    }
}
```

- [ ] **Step 2: Create `src/hf/format/mrx_g1_format.cpp`**

Move these encode functions verbatim (find in the MRx encoder section ~3280–3378):

| Function |
|---|
| `encode_mrx_system_version` |
| `encode_mrx_checksum` |
| `encode_mrx_pbit_status` |
| `encode_mrx_ibit_status` |
| `encode_mrx_temperature` |
| `encode_mrx_fan_speed` |
| `encode_mrx_uart_test_rsp` |
| `encode_mrx_cbit_status` |

Then append:

```cpp
int mrx_g1_format(long long unit_id, const char* json, uint8_t* buf, int max_len, bool& is_ack) {
    typedef int (*EncFn)(const char*, uint8_t*, int);
    EncFn fn = nullptr;
    switch (unit_id) {
        case  2: fn = encode_mrx_system_version; break;
        case  4: fn = encode_mrx_checksum;        break;
        case  6: fn = encode_mrx_pbit_status;     break;
        case  8: fn = encode_mrx_ibit_status;     break;
        case 10: fn = encode_mrx_temperature;     break;
        case 14: fn = encode_mrx_fan_speed;       break;
        case 18: fn = encode_mrx_uart_test_rsp;   break;
        case 26: fn = encode_mrx_cbit_status;     break;
        case 34: is_ack = true;                   break; // Close All Channels ACK
        default: return 0;
    }
    if (is_ack) return 0;
    return fn(json, buf, max_len);
}
```

- [ ] **Step 3: Add to `hf_groups.h`, replace in main, delete moved functions, build, test, commit**

```cpp
// MRx Group 1 — Diagnostics
bool mrx_g1_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
bool mrx_g1_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
int  mrx_g1_format   (long long unit_id, const char* json,
                      uint8_t* buf, int max_len, bool& is_ack);
```

In `parse_message`, the MRx group 1 block is currently labelled with a comment; replace with:
```cpp
else if (hdr.group_id == 1) decoded = (frame_type == FRAME_COMMAND)
    ? mrx_g1_parse_cmd(hdr.unit_id, payload, plen, w)
    : mrx_g1_parse_rsp(hdr.unit_id, payload, plen, w);
```

In `format_response`:
```cpp
else if (group == 1) plen = mrx_g1_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack);
```

```
cmake --build drs-bridge/parsers/dp_ecm/build --config Release
drs-bridge/parsers/dp_ecm/build/Release/test_dp_ecm.exe
git commit -m "refactor(hf): extract MRx group 1 (diagnostics)"
```

---

## Task 10: MRx Group 3 — RF Board / Channel Management

**Files:**
- Create: `src/hf/parser/mrx_g3_parser.cpp`
- Create: `src/hf/format/mrx_g3_format.cpp`
- Modify: `src/hf/hf_groups.h`, `src/dp_ecm_hf_parser.cpp`

- [ ] **Step 1: Create `src/hf/parser/mrx_g3_parser.cpp`**

Move these verbatim:

| Function | Original lines (approx) |
|---|---|
| `decode_mrx_all_channels_rsp` | ~1362 |
| `decode_mrx_channel_init_status` | ~1380 |
| `decode_mrx_board_count_rsp` | ~1610 |
| `decode_mrx_channel_16b_rsp` | ~1620 |
| `decode_mrx_write_channel_cmd` | ~1627 |
| `decode_mrx_tuning_details_rsp` | ~1637 |

Then append:

```cpp
bool mrx_g3_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  1: return true;                                // Read Board Count — 0 bytes
        case 17: return true;                                // Read Channel Info — 0 bytes
        case 19: decode_mrx_write_channel_cmd(p, n, w);     return true;
        case 21: return true;                                // VUSHF Channel Status — 0 bytes
        case 23: return true;                                // Read Tuning Details — 0 bytes
        case 25: return true;                                // Get CBIT Status — 0 bytes
        default: return false;
    }
}

bool mrx_g3_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  2: decode_mrx_board_count_rsp(p, n, w);       return true;
        case 18: decode_mrx_channel_16b_rsp(p, n, w);       return true;
        case 20: return true;                                // Write Channel ACK
        case 22: decode_mrx_channel_init_status(p, n, w);   return true;
        case 24: decode_mrx_tuning_details_rsp(p, n, w);    return true;
        case 26: decode_mrx_cbit_status(p, n, w);           return true;
        default: return false;
    }
}
```

> `decode_mrx_cbit_status` is already in `mrx_g1_parser.cpp` as `static`. For `mrx_g3_parser.cpp` to call it you have two options: (a) duplicate the small function body (it's ~6 lines), or (b) move it to `hf_shared.h` as `inline`. **Recommended: duplicate** — it keeps each file fully self-contained and the function is trivially small.

- [ ] **Step 2: Create `src/hf/format/mrx_g3_format.cpp`**

Move `encode_mrx_board_count_rsp`, `encode_mrx_tuning_details_rsp` verbatim.

`encode_mrx_channels_16b` (defined at ~line 3392) is a static helper used only by this group — move it here too.

Then append:

```cpp
int mrx_g3_format(long long unit_id, const char* json, uint8_t* buf, int max_len, bool& is_ack) {
    switch (unit_id) {
        case  2: return encode_mrx_board_count_rsp(json, buf, max_len);
        case 18: return encode_mrx_channels_16b(json, "\"channels\"", "status", buf, max_len);
        case 20: is_ack = true; return 0;                  // Write Channel ACK
        case 22: return encode_mrx_channels_16b(json, "\"channel_init_statuses\"", "init_status", buf, max_len);
        case 24: return encode_mrx_tuning_details_rsp(json, buf, max_len);
        case 26: return encode_mrx_cbit_status(json, buf, max_len);
        default: return 0;
    }
}
```

> `encode_mrx_cbit_status` is in `mrx_g1_format.cpp` as `static`. Same resolution as the parser side — duplicate the small body here.

- [ ] **Step 3: Add to `hf_groups.h`, replace in main, delete moved functions, build, test, commit**

```cpp
// MRx Group 3 — RF Board / Channel Management
bool mrx_g3_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
bool mrx_g3_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
int  mrx_g3_format   (long long unit_id, const char* json,
                      uint8_t* buf, int max_len, bool& is_ack);
```

```
cmake --build drs-bridge/parsers/dp_ecm/build --config Release
drs-bridge/parsers/dp_ecm/build/Release/test_dp_ecm.exe
git commit -m "refactor(hf): extract MRx group 3 (RF board/channel)"
```

---

## Task 11: MRx Group 4 — Data Acquisition

**Files:**
- Create: `src/hf/parser/mrx_g4_parser.cpp`
- Create: `src/hf/format/mrx_g4_format.cpp`
- Modify: `src/hf/hf_groups.h`, `src/dp_ecm_hf_parser.cpp`

- [ ] **Step 1: Create `src/hf/parser/mrx_g4_parser.cpp`**

Move these verbatim (all unique to group 4):

| Function | Original lines (approx) |
|---|---|
| `decode_mrx_optical_iq_cmd` | ~1402 |
| `decode_mrx_optical_port_status_rsp` | ~1413 |
| `decode_mrx_optical_ip_rsp` | ~1425 |
| `decode_mrx_set_threshold_cmd` | ~1686 |
| `decode_mrx_audio_data_rsp` | ~1694 |
| `decode_mrx_demod_bw_cmd` | ~1714 |
| `decode_mrx_memory_scan_config_cmd` | ~1732 |
| `decode_mrx_memory_scan_data_rsp` | ~1767 |
| `decode_mrx_smart_scan_read_cmd` | ~1800 |
| `decode_mrx_smart_scan_read_rsp` | ~1807 |
| `decode_mrx_ddc_fft_rsp` | ~1815 |
| `decode_mrx_iq_start_rsp` | ~1836 |
| `decode_mrx_iq_logging_start_cmd` | ~1846 |
| `decode_mrx_iq_logging_stop_rsp` | ~1867 |
| `decode_mrx_engage_channel_cmd` | ~1877 |
| `decode_mrx_ddc_fft_cmd` | ~2006 |
| `decode_mrx_iq_start_cmd` | ~2016 |

Then append (note `decode_mrx_channel_cmd` comes from `hf_shared.h`):

```cpp
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
```

- [ ] **Step 2: Create `src/hf/format/mrx_g4_format.cpp`**

Move these encode functions verbatim (find in MRx encoder section):

| Function |
|---|
| `encode_mrx_audio_data_rsp` |
| `encode_mrx_iq_start_rsp` |
| `encode_mrx_iq_logging_stop_rsp` |
| `encode_mrx_memory_scan_data_rsp` |
| `encode_mrx_ddc_fft_rsp` |
| `encode_mrx_smart_scan_read_rsp` |
| `encode_mrx_optical_port_status_rsp` |
| `encode_mrx_optical_ip_rsp` |

Then append:

```cpp
int mrx_g4_format(long long unit_id, const char* json, uint8_t* buf, int max_len, bool& is_ack) {
    typedef int (*EncFn)(const char*, uint8_t*, int);
    EncFn fn = nullptr;
    switch (unit_id) {
        case  6: is_ack = true; break; // Set Threshold ACK
        case  8: fn = encode_mrx_audio_data_rsp;        break;
        case 10: is_ack = true; break; // Audio Start Play ACK
        case 12: is_ack = true; break; // Audio Stop Play ACK
        case 16: is_ack = true; break; // Audio FIFO Reset ACK
        case 18: is_ack = true; break; // Demod/BW Select ACK
        case 24: fn = encode_mrx_iq_start_rsp;          break;
        case 26: fn = encode_mrx_iq_logging_stop_rsp;   break;
        case 34: fn = encode_mrx_iq_start_rsp;          break;
        case 36: is_ack = true; break; // Stop IQ Streaming ACK
        case 40: is_ack = true; break; // Configure Memory Scan ACK
        case 42: fn = encode_mrx_memory_scan_data_rsp;  break;
        case 44: fn = encode_mrx_ddc_fft_rsp;           break;
        case 54: is_ack = true; break; // Engage Channel ACK
        case 56: is_ack = true; break; // Disengage Channel ACK
        case 58: is_ack = true; break; // Stop Memory Scan ACK
        case 60: is_ack = true; break; // Smart Memory Scan Config ACK
        case 62: fn = encode_mrx_smart_scan_read_rsp;   break;
        case 64: is_ack = true; break; // Stop Smart Memory Scan ACK
        case 66: is_ack = true; break; // Start Optical IQ ACK
        case 68: is_ack = true; break; // Stop Optical IQ ACK
        case 70: fn = encode_mrx_optical_port_status_rsp; break;
        case 72: fn = encode_mrx_optical_ip_rsp;          break;
        default: return 0;
    }
    if (is_ack) return 0;
    return fn(json, buf, max_len);
}
```

- [ ] **Step 3: Add to `hf_groups.h`, replace in main, delete moved functions, build, test, commit**

```cpp
// MRx Group 4 — Data Acquisition
bool mrx_g4_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
bool mrx_g4_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
int  mrx_g4_format   (long long unit_id, const char* json,
                      uint8_t* buf, int max_len, bool& is_ack);
```

```
cmake --build drs-bridge/parsers/dp_ecm/build --config Release
drs-bridge/parsers/dp_ecm/build/Release/test_dp_ecm.exe
git commit -m "refactor(hf): extract MRx group 4 (data acquisition)"
```

---

## Task 12: MRx Groups 5, 6, 7 — Tuner, FH Monitoring, Signal BITE

**Files:**
- Create: `src/hf/parser/mrx_g5_parser.cpp`, `mrx_g5_format.cpp`
- Create: `src/hf/parser/mrx_g6_parser.cpp`, `mrx_g6_format.cpp`
- Create: `src/hf/parser/mrx_g7_parser.cpp`, `mrx_g7_format.cpp`
- Modify: `src/hf/hf_groups.h`, `src/dp_ecm_hf_parser.cpp`

- [ ] **Step 1: Create `src/hf/parser/mrx_g5_parser.cpp` (MRx Tuner)**

Move verbatim: `decode_mrx_set_center_freq_cmd` (~1664), `decode_mrx_attenuation_cmd` (~1674), `decode_mrx_agc_status_rsp` (~1655).

```cpp
bool mrx_g5_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case 1: decode_mrx_set_center_freq_cmd(p, n, w); return true;
        case 3: decode_mrx_attenuation_cmd(p, n, w);     return true;
        case 9: decode_mrx_channel_cmd(p, n, w);         return true; // Clear Center Freq
        case 13: return true;                             // Read AGC/MGC — 0 bytes
        default: return false;
    }
}

bool mrx_g5_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  2: return true;                            // Set Center Freq ACK
        case  4: return true;                            // Attenuation Select ACK
        case 10: return true;                            // Clear Center Freq ACK
        case 14: decode_mrx_agc_status_rsp(p, n, w);   return true;
        default: return false;
    }
}
```

- [ ] **Step 2: Create `src/hf/format/mrx_g5_format.cpp`**

Move `encode_mrx_agc_status_rsp` verbatim. Then:

```cpp
int mrx_g5_format(long long unit_id, const char* json, uint8_t* buf, int max_len, bool& is_ack) {
    switch (unit_id) {
        case  2: is_ack = true; return 0;                // Set Center Freq ACK
        case  4: is_ack = true; return 0;                // Attenuation Select ACK
        case 10: is_ack = true; return 0;                // Clear Center Freq ACK
        case 14: return encode_mrx_agc_status_rsp(json, buf, max_len);
        default: return 0;
    }
}
```

- [ ] **Step 3: Create `src/hf/parser/mrx_g6_parser.cpp` (MRx FH Monitoring/GO2Monitor)**

Move verbatim: `decode_mrx_fh_monitoring_cmd` (~1888), `decode_mrx_go2monitor_connect_cmd` (~1917), `decode_mrx_start_go2monitor_cmd` (~1929).

```cpp
bool mrx_g6_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  7: decode_mrx_fh_monitoring_cmd(p, n, w);      return true;
        case  9: decode_mrx_go2monitor_connect_cmd(p, n, w); return true;
        case 11: return true;                                 // GO2Monitor Disconnect — 0 bytes
        case 13: decode_mrx_start_go2monitor_cmd(p, n, w);   return true;
        case 15: return true;                                 // Stop GO2Monitor — 0 bytes
        default: return false;
    }
}

bool mrx_g6_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  8: return true; // FH Monitoring Config ACK
        case 10: return true; // GO2Monitor Connect ACK
        case 12: return true; // GO2Monitor Disconnect ACK
        case 14: return true; // Start GO2Monitor ACK
        case 16: return true; // Stop GO2Monitor ACK
        default: return false;
    }
}
```

- [ ] **Step 4: Create `src/hf/format/mrx_g6_format.cpp`**

No encode functions (all ACKs). Just:

```cpp
int mrx_g6_format(long long unit_id, const char* json, uint8_t* buf, int max_len, bool& is_ack) {
    switch (unit_id) {
        case  8: case 10: case 12: case 14: case 16: is_ack = true; return 0;
        default: return 0;
    }
}
```

- [ ] **Step 5: Create `src/hf/parser/mrx_g7_parser.cpp` (MRx Signal BITE/misc)**

Move verbatim: `decode_mrx_signal_bite_cmd` (~1946), `decode_mrx_signal_bite_rsp` (~1953), `decode_mrx_spectrum_avg_cmd` (~1963), `decode_mrx_audio_squelch_cmd` (~1971), `decode_mrx_date_time_cmd` (~1980), `decode_mrx_sel_channel_cmd` (~1996).

```cpp
bool mrx_g7_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  1: decode_mrx_signal_bite_cmd(p, n, w);                                                   return true;
        case  3: decode_mrx_sel_channel_cmd("bite_antenna_sel","bite","antenna",p,n,w);                  return true;
        case  5: decode_mrx_sel_channel_cmd("ref_source_sel","external","internal",p,n,w);               return true;
        case  9: decode_mrx_sel_channel_cmd("afc_sel","on","off",p,n,w);                                 return true;
        case 11: decode_mrx_sel_channel_cmd("rf_squelch_sel","on","off",p,n,w);                          return true;
        case 13: decode_mrx_channel_cmd(p, n, w);                                                        return true; // IQ socket open
        case 15: decode_mrx_channel_cmd(p, n, w);                                                        return true; // IQ socket close
        case 17: decode_mrx_spectrum_avg_cmd(p, n, w);                                                   return true;
        case 19: decode_mrx_sel_channel_cmd("rf_agc_sel","enable","disable",p,n,w);                      return true;
        case 21: decode_mrx_audio_squelch_cmd(p, n, w);                                                  return true;
        case 23: decode_mrx_date_time_cmd(p, n, w);                                                      return true;
        default: return false;
    }
}

bool mrx_g7_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w) {
    switch (unit_id) {
        case  2: decode_mrx_signal_bite_rsp(p, n, w); return true;
        case  4: return true;  // BITE/Antenna Select ACK
        case  6: return true;  // Ref Source Select ACK
        case 10: return true;  // AFC ACK
        case 12: return true;  // RF Squelch ACK
        case 14: return true;  // IQ Socket Open ACK
        case 16: return true;  // IQ Socket Close ACK
        case 18: return true;  // Spectrum Avg Count ACK
        case 20: return true;  // Smart RF AGC ACK
        case 22: return true;  // Audio Squelch ACK
        case 24: return true;  // Set Date/Time ACK
        default: return false;
    }
}
```

- [ ] **Step 6: Create `src/hf/format/mrx_g7_format.cpp`**

Move `encode_mrx_signal_bite_rsp` verbatim. Then:

```cpp
int mrx_g7_format(long long unit_id, const char* json, uint8_t* buf, int max_len, bool& is_ack) {
    switch (unit_id) {
        case  2: return encode_mrx_signal_bite_rsp(json, buf, max_len);
        case  4: case  6: case 10: case 12:
        case 14: case 16: case 18: case 20:
        case 22: case 24: is_ack = true; return 0;
        default: return 0;
    }
}
```

- [ ] **Step 7: Add all three groups to `hf_groups.h`**

```cpp
// MRx Group 5 — Tuner
bool mrx_g5_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
bool mrx_g5_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
int  mrx_g5_format   (long long unit_id, const char* json, uint8_t* buf, int max_len, bool& is_ack);

// MRx Group 6 — FH Monitoring / GO2Monitor
bool mrx_g6_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
bool mrx_g6_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
int  mrx_g6_format   (long long unit_id, const char* json, uint8_t* buf, int max_len, bool& is_ack);

// MRx Group 7 — Signal BITE / misc
bool mrx_g7_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
bool mrx_g7_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
int  mrx_g7_format   (long long unit_id, const char* json, uint8_t* buf, int max_len, bool& is_ack);
```

- [ ] **Step 8: Replace groups 5, 6, 7 blocks in `dp_ecm_hf_parser.cpp`, delete moved functions, build, test, commit**

```
cmake --build drs-bridge/parsers/dp_ecm/build --config Release
drs-bridge/parsers/dp_ecm/build/Release/test_dp_ecm.exe
git commit -m "refactor(hf): extract MRx groups 5/6/7 (tuner, FH, BITE)"
```

---

## Task 13: Final Cleanup — trim main, verify, final commit

**Files:**
- Modify: `src/dp_ecm_hf_parser.cpp`

- [ ] **Step 1: Verify main file length**

```
(Get-Content drs-bridge/parsers/dp_ecm/src/dp_ecm_hf_parser.cpp).Count
```

Expected: ≤ 200 lines. If more, search for any leftover static functions that weren't deleted in earlier tasks and remove them.

- [ ] **Step 2: Verify the final structure of `dp_ecm_hf_parser.cpp`**

The file should contain only:
1. `#include` block (sdfc headers + hf_groups.h + hf_json_utils.h + hf_shared.h)
2. `static constexpr const char* HW_NAME`
3. `extern "C" SDFC_EXPORT int extract_frame(...)` (~15 lines)
4. `extern "C" SDFC_EXPORT int parse_message(...)` — 13-line group dispatch + common JSON preamble + raw_hex fallback
5. `extern "C" SDFC_EXPORT int format_response(...)` — 13-line group dispatch + payload_hex fallback + frame assembly
6. `extern "C" SDFC_EXPORT void free_result(...)`

- [ ] **Step 3: Full test run**

```
cmake --build drs-bridge/parsers/dp_ecm/build --config Release
drs-bridge/parsers/dp_ecm/build/Release/test_dp_ecm.exe
```

Expected: all pass, identical result to baseline.

- [ ] **Step 4: Final commit**

```
git add drs-bridge/parsers/dp_ecm/src/dp_ecm_hf_parser.cpp
git commit -m "refactor(hf): trim main file to thin ABI dispatcher (~150 lines)"
```

---

## Self-review checklist

- [x] All 13 groups covered: 100, 101, 106, 109, 111, 112, 200, mrx_g1, mrx_g3, mrx_g4, mrx_g5, mrx_g6, mrx_g7
- [x] ABI exports unchanged: `extract_frame`, `parse_message`, `format_response`, `free_result`
- [x] `dp_ecm_vu_parser.cpp` not touched
- [x] `hf_shared.h` covers all cross-group functions: 4 jamming decoders + `decode_mrx_channel_cmd`
- [x] `decode_mrx_cbit_status` and `encode_mrx_cbit_status` duplicate noted in Tasks 9/10 with rationale
- [x] `encode_mrx_channels_16b` moved to `mrx_g3_format.cpp` as private static
- [x] `payload_hex` fallback and frame assembly remain in main's `format_response`
- [x] `hex_nibble` remains accessible in main via `hf_json_utils.h` include
- [x] `MAX_PAYLOAD` stays in main (from `sdfc_frame.h`) — format functions use `max_len` parameter
- [x] `file(GLOB)` in CMakeLists picks up both subdirectories; note for maintainers: re-run cmake configure when adding new group files
- [x] No placeholder steps — all dispatcher function bodies are shown completely
