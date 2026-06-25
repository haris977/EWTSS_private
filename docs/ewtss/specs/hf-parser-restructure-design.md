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

Split the file into self-contained per-group files organised by concern:
all parsing (decode) logic lives under `hf/parser/`, all formatting (encode)
logic lives under `hf/format/`. One file per protocol group in each folder.

Easy to explain to a client: *"open `parser/` to find decode logic, open
`format/` to find encode logic — every group has a file in each."*

---

## Design

### Directory layout

```
drs-bridge/parsers/dp_ecm/
├── include/                       ← unchanged
│   ├── sdfc_abi.h
│   ├── sdfc_frame.h
│   ├── sdfc_endian.h
│   └── json_writer.h
└── src/
    ├── sdfc_frame.cpp              ← unchanged
    ├── dp_ecm_hf_parser.cpp        ← shrinks to ~150 lines: 3 ABI exports + dispatch
    └── hf/                         ← NEW
        ├── hf_groups.h             ← dispatcher signatures (included only by main)
        ├── hf_json_utils.h         ← json_find_int/double, hex_nibble, parse_toa_hms,
        │                              MAX_PAYLOAD — shared by all format files
        ├── hf_shared.h             ← 4 decode functions shared between groups 101 & 200
        │
        ├── parser/                 ← all decode (parse_message) logic
        │   ├── g100_parser.cpp     ← SJC Diagnostics
        │   ├── g101_parser.cpp     ← SJC Detection + Jamming
        │   ├── g106_parser.cpp     ← Immediate Jamming
        │   ├── g109_parser.cpp     ← Date/Time
        │   ├── g111_parser.cpp     ← Signal/Scan
        │   ├── g112_parser.cpp     ← Fast Scan/Simulation
        │   ├── g200_parser.cpp     ← HF ECM Jamming
        │   ├── mrx_g1_parser.cpp   ← MRx Diagnostics
        │   ├── mrx_g3_parser.cpp   ← MRx RF Board/Channel
        │   ├── mrx_g4_parser.cpp   ← MRx Data Acquisition
        │   ├── mrx_g5_parser.cpp   ← MRx Tuner
        │   ├── mrx_g6_parser.cpp   ← MRx FH Monitoring/GO2Monitor
        │   └── mrx_g7_parser.cpp   ← MRx Signal BITE/misc
        │
        └── format/                 ← all encode (format_response) logic
            ├── g100_format.cpp
            ├── g101_format.cpp
            ├── g106_format.cpp
            ├── g109_format.cpp
            ├── g111_format.cpp
            ├── g112_format.cpp
            ├── g200_format.cpp
            ├── mrx_g1_format.cpp
            ├── mrx_g3_format.cpp
            ├── mrx_g4_format.cpp
            ├── mrx_g5_format.cpp
            ├── mrx_g6_format.cpp
            └── mrx_g7_format.cpp
```

**13 parser files + 13 format files + 3 shared headers.** The `include/`
directory and `sdfc_frame.cpp` are untouched.

---

### Group interface (`hf_groups.h`)

Every group exposes exactly three functions, declared in `hf_groups.h` and
included only by `dp_ecm_hf_parser.cpp`. Example for Group 100; all other
groups follow the identical signature pattern:

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
- `format`: returns payload bytes written; `-1` on error; sets `is_ack = true`
  for zero-payload ACKs (caller skips payload encoding).

All internal `decode_*` and `encode_*` helpers remain `static` inside their
`.cpp` — nothing beyond these three functions is visible outside the file.

---

### What each file contains

| File | Contents |
|---|---|
| `parser/g{N}_parser.cpp` | All `decode_*` statics for that group + `g{N}_parse_cmd()` + `g{N}_parse_rsp()` (switch on `unit_id`) |
| `format/g{N}_format.cpp` | All `encode_*` statics for that group + `g{N}_format()` (switch + `EncFn fn` pattern matching existing code) |
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
Both `parser/g101_parser.cpp` and `parser/g200_parser.cpp` include this header.

```cpp
// hf/hf_shared.h
#pragma once
// Decode functions shared between groups 101 and 200.
inline void decode_cmd_send_ecm_reports(...)        { … }
inline void decode_cmd_start_list_jam(...)           { … }
inline void decode_cmd_start_follow_on_jam(...)      { … }
inline void decode_cmd_start_responsive_sweep_jam(…) { … }
```

No duplication, no additional linkage.

---

### CMakeLists.txt changes

```cmake
# Collect group sources from both subfolders
file(GLOB HF_PARSER_SRCS src/hf/parser/*.cpp)
file(GLOB HF_FORMAT_SRCS src/hf/format/*.cpp)

add_library(dp_ecm_hf SHARED
    ${SDFC_CORE_SRC}
    src/dp_ecm_hf_parser.cpp
    ${HF_PARSER_SRCS}           # ← added
    ${HF_FORMAT_SRCS}           # ← added
)
target_include_directories(dp_ecm_hf PRIVATE
    include
    src/hf                      # ← added so group files reach hf_*.h headers
)

# Test executable gets the same additions
add_executable(test_dp_ecm
    tests/test_frames.cpp
    ${SDFC_CORE_SRC}
    src/dp_ecm_hf_parser.cpp
    ${HF_PARSER_SRCS}           # ← added
    ${HF_FORMAT_SRCS}           # ← added
)
target_include_directories(test_dp_ecm PRIVATE
    include
    src/hf                      # ← added
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

| Group | `parser/` | `format/` |
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

All files land well under 400 lines.
