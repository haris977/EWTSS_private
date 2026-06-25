# HF Parser — Per-Group File Restructure Design

**Date:** 2026-06-25
**Scope:** `drs-bridge/parsers/dp_ecm/` — `dp_ecm_hf` DLL only
**Audience:** C++ developer (Person C), integration lead
**Status:** Approved, pending implementation plan

---

## Problem

`src/dp_ecm_hf_parser.cpp` is ~3,900 lines — 13 protocol groups, all decode
and encode helpers, and the three ABI entry-points in one file. Navigation is
hard; a developer looking for Group 101 detection code must scroll past Group
100, embedded MRx diagnostics, and shared utilities to find it.

---

## Goal

Split the file into self-contained per-group pairs that are easy to explain
to a client or new contributor: *"every protocol group has exactly two files —
one for parsing, one for formatting."*

---

## Design

### Directory layout

```
drs-bridge/parsers/dp_ecm/
├── include/                    ← unchanged
│   ├── sdfc_abi.h
│   ├── sdfc_frame.h
│   ├── sdfc_endian.h
│   └── json_writer.h
└── src/
    ├── sdfc_frame.cpp           ← unchanged
    ├── dp_ecm_hf_parser.cpp     ← shrinks to ~150 lines: 3 ABI exports + dispatch
    └── hf/                      ← NEW: all group implementation files
        ├── hf_groups.h          ← dispatcher signatures (included only by main)
        ├── hf_json_utils.h      ← json_find_int/double, hex_nibble, parse_toa_hms,
        │                           MAX_PAYLOAD — shared by all format files
        ├── hf_shared.h          ← 4 decode functions shared between groups 101 & 200
        │
        ├── g100_parse.cpp       ← SJC Diagnostics: decode_* functions + cmd/rsp dispatch
        ├── g100_format.cpp      ← SJC Diagnostics: encode_* functions + format dispatch
        ├── g101_parse.cpp       ← SJC Detection+Jamming: decode
        ├── g101_format.cpp      ← SJC Detection+Jamming: encode
        ├── g106_parse.cpp       ← Immediate Jamming: decode
        ├── g106_format.cpp      ← Immediate Jamming: encode
        ├── g109_parse.cpp       ← Date/Time: decode
        ├── g109_format.cpp      ← Date/Time: encode
        ├── g111_parse.cpp       ← Signal/Scan: decode
        ├── g111_format.cpp      ← Signal/Scan: encode
        ├── g112_parse.cpp       ← Fast Scan/Simulation: decode
        ├── g112_format.cpp      ← Fast Scan/Simulation: encode
        ├── g200_parse.cpp       ← HF ECM Jamming: decode
        ├── g200_format.cpp      ← HF ECM Jamming: encode
        ├── mrx_g1_parse.cpp     ← MRx Diagnostics: decode
        ├── mrx_g1_format.cpp
        ├── mrx_g3_parse.cpp     ← MRx RF Board/Channel: decode
        ├── mrx_g3_format.cpp
        ├── mrx_g4_parse.cpp     ← MRx Data Acquisition: decode
        ├── mrx_g4_format.cpp
        ├── mrx_g5_parse.cpp     ← MRx Tuner: decode
        ├── mrx_g5_format.cpp
        ├── mrx_g6_parse.cpp     ← MRx FH Monitoring/GO2Monitor: decode
        ├── mrx_g6_format.cpp
        ├── mrx_g7_parse.cpp     ← MRx Signal BITE/misc: decode
        └── mrx_g7_format.cpp
```

**26 group `.cpp` files + 3 internal headers.** The `include/` directory and
`sdfc_frame.cpp` are untouched.

---

### Group interface (`hf_groups.h`)

Every group pair exposes exactly three functions. Example for Group 100;
all other groups follow the identical signature pattern:

```cpp
// hf/hf_groups.h
#pragma once
#include <cstdint>
#include "json_writer.h"

// Group 100 — SJC Diagnostics
bool g100_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
bool g100_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
int  g100_format   (long long unit_id, const char* json,
                    uint8_t* buf, int max_len, bool& is_ack);

// Group 101 — SJC Detection + Jamming
bool g101_parse_cmd(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
bool g101_parse_rsp(uint16_t unit_id, const uint8_t* p, int n, JsonWriter& w);
int  g101_format   (long long unit_id, const char* json,
                    uint8_t* buf, int max_len, bool& is_ack);

// … repeated for g106, g109, g111, g112, g200,
//   mrx_g1, mrx_g3, mrx_g4, mrx_g5, mrx_g6, mrx_g7
```

**Return-value contracts:**
- `parse_cmd` / `parse_rsp`: return `true` if `unit_id` was recognised, `false`
  to fall through to the `raw_hex` envelope in the main dispatcher.
- `format`: returns the number of payload bytes written; `-1` on error; sets
  `is_ack = true` for zero-payload ACKs (caller skips payload encoding).

All internal `decode_*` and `encode_*` helpers remain `static` inside their
`.cpp` — nothing beyond these three functions is visible outside the file.

---

### What each file contains

| File | Contents |
|---|---|
| `g{N}_parse.cpp` | All `decode_*` statics for that group + `g{N}_parse_cmd()` + `g{N}_parse_rsp()` implementations (switch statements on `unit_id`) |
| `g{N}_format.cpp` | All `encode_*` statics for that group + `g{N}_format()` implementation (switch + `EncFn fn` pattern matching the existing code) |
| `hf_json_utils.h` | `inline` helpers currently at lines 2537–2793 of the original: `json_find_int`, `json_find_double`, `hex_nibble`, `parse_toa_hms`, `MAX_PAYLOAD` constant |
| `hf_shared.h` | `inline` decode functions called by both groups 101 and 200: `decode_cmd_send_ecm_reports`, `decode_cmd_start_list_jam`, `decode_cmd_start_follow_on_jam`, `decode_cmd_start_responsive_sweep_jam` |

---

### Main file after restructure (`dp_ecm_hf_parser.cpp`, ~150 lines)

Only three responsibilities remain:

1. `extract_frame` — unchanged, already small (~15 lines).
2. `parse_message` — includes `hf_groups.h`, dispatches by `group_id`:

```cpp
if      (hdr.group_id == 100) decoded = g100_parse_cmd(hdr.unit_id, payload, plen, w);
else if (hdr.group_id == 101) decoded = g101_parse_cmd(hdr.unit_id, payload, plen, w);
else if (hdr.group_id == 106) decoded = g106_parse_cmd(hdr.unit_id, payload, plen, w);
// … 10 more groups
// response path mirrors command path with g{N}_parse_rsp
```

3. `format_response` — same pattern:

```cpp
if      (group == 100) plen = g100_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack);
else if (group == 101) plen = g101_format(unit, kwargs_json, payload, MAX_PAYLOAD, is_ack);
// … 10 more groups
```

---

### Shared decode functions (`hf_shared.h`)

Four command decoders are called from both Group 101 and Group 200 in the
current `parse_message`. They move to `hf_shared.h` as `inline` functions.
Both `g101_parse.cpp` and `g200_parse.cpp` include this header.

```cpp
// hf/hf_shared.h
#pragma once
// Decode functions shared between groups 101 and 200.
inline void decode_cmd_send_ecm_reports(...)       { … }
inline void decode_cmd_start_list_jam(...)          { … }
inline void decode_cmd_start_follow_on_jam(...)     { … }
inline void decode_cmd_start_responsive_sweep_jam(…){ … }
```

No duplication, no additional linkage.

---

### CMakeLists.txt changes

```cmake
# Collect all new group sources
file(GLOB HF_GROUP_SRCS src/hf/*.cpp)

add_library(dp_ecm_hf SHARED
    ${SDFC_CORE_SRC}
    src/dp_ecm_hf_parser.cpp
    ${HF_GROUP_SRCS}           # ← added
)
target_include_directories(dp_ecm_hf PRIVATE
    include
    src/hf                     # ← added so group files reach hf_*.h headers
)

# Test executable gets the same additions
add_executable(test_dp_ecm
    tests/test_frames.cpp
    ${SDFC_CORE_SRC}
    src/dp_ecm_hf_parser.cpp
    ${HF_GROUP_SRCS}           # ← added
)
target_include_directories(test_dp_ecm PRIVATE
    include
    src/hf                     # ← added
)
```

---

## What does not change

- `dp_ecm_vu_parser.cpp` and its test — untouched.
- The `.def` export file — the three exported symbols are identical.
- `include/` headers — no changes.
- The ABI itself — `extract_frame`, `parse_message`, `format_response`
  signatures and behaviour are preserved exactly.
- Test coverage — `tests/test_frames.cpp` exercises the ABI, not internal
  functions, so tests pass without modification.

---

## Approximate file sizes after split

| Group | `_parse.cpp` | `_format.cpp` |
|---|---|---|
| g100 | ~200 lines | ~200 lines |
| g101 | ~350 lines | ~350 lines |
| g106 | ~200 lines | ~100 lines |
| g109 | ~80 lines  | ~80 lines  |
| g111 | ~250 lines | ~200 lines |
| g112 | ~100 lines | ~80 lines  |
| g200 | ~350 lines | ~200 lines |
| mrx_g1 | ~100 lines | ~100 lines |
| mrx_g3 | ~80 lines  | ~80 lines  |
| mrx_g4 | ~200 lines | ~200 lines |
| mrx_g5 | ~80 lines  | ~50 lines  |
| mrx_g6 | ~80 lines  | ~50 lines  |
| mrx_g7 | ~120 lines | ~50 lines  |

All files land well under 400 lines — the threshold where a single-purpose file
becomes uncomfortable to navigate.
