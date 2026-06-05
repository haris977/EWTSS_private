# EWTSS v2 — C++ Developer Reference

**Audience:** The C++ developer joining the EWTSS v2 hardening team (Person C in the execution plan).
**Purpose:** Everything you need to know to be productive — what you own, how your code fits into the
system, what is already decided, what is left ambiguous, and what to build next.
**Read time:** ~45 minutes for the full document. If you are short on time, start with §2 (Your Role)
and §5 (Data Flow) — they are the load-bearing sections.

---

## Table of Contents

1. [Beginner-Friendly Project Overview](#1-beginner-friendly-project-overview)
2. [Your Role — What the C++ Developer Owns](#2-your-role--what-the-c-developer-owns)
3. [Architecture from the C++ Perspective](#3-architecture-from-the-c-perspective)
4. [Important Files, Classes, and Modules](#4-important-files-classes-and-modules)
5. [Data Flow — End to End](#5-data-flow--end-to-end)
6. [Threading and Concurrency](#6-threading-and-concurrency)
7. [Qt-Related Responsibilities](#7-qt-related-responsibilities)
8. [Networking and Protocol Responsibilities](#8-networking-and-protocol-responsibilities)
9. [Build and Deployment](#9-build-and-deployment)
10. [The C++ Parser ABI Contract (Critical)](#10-the-c-parser-abi-contract-critical)
11. [Adding a New Hardware Variant — Step by Step](#11-adding-a-new-hardware-variant--step-by-step)
12. [External Dependencies and Libraries](#12-external-dependencies-and-libraries)
13. [Coding Standards and Patterns](#13-coding-standards-and-patterns)
14. [Potential Future Tasks](#14-potential-future-tasks)
15. [Open Questions and Ambiguities](#15-open-questions-and-ambiguities)
16. [Assumptions (Separated Clearly)](#16-assumptions-separated-clearly)

---

## 1. Beginner-Friendly Project Overview

### What is EWTSS v2?

EWTSS stands for **Electronic Warfare Test and Support System**, version 2. It is a Windows desktop
application used by military operators to:

1. **Plan electronic warfare exercises** — operators draw a map scenario (aircraft flight paths,
   ground facilities, sensor areas) using a 3D globe, run a simulation in STK (a commercial
   aerospace/defence tool), and generate expected signal detection data.

2. **Run the exercise against real hardware** — Physical DRS (Device Replacement Software) hardware
   boxes are connected to the system. These boxes simulate electronic warfare receiver equipment.
   The system sends commands to the hardware, receives responses containing RF measurements
   (detected frequencies, signal strengths, azimuths), and records everything for analysis.

### Two Workstations

The system runs across two Windows 11 PCs on a private LAN:

| Workstation | Role | What runs on it |
|---|---|---|
| **WS1 — Scenario Generator (SG)** | Operator plans and controls exercises | `Sg.App.exe` (C# WPF desktop app), STK 12 Engine |
| **WS2 — DRS Workstation** | Connects to physical hardware, records telemetry | `drs-bridge` (Python), `drs-server` (Python FastAPI), Kafka, PostgreSQL + TimescaleDB |

### Where C++ Fits

The C++ code lives entirely on **WS2**, inside a component called `drs-bridge`. The hardware devices
speak proprietary binary protocols over TCP. Your C++ parser libraries are the component that:

- **Detects** complete binary frames arriving from the hardware
- **Decodes** those frames into structured JSON
- **Encodes** JSON responses back into binary frames to send to the hardware

Everything else (TCP networking, Kafka messaging, database writes, UI display) is handled by Python
and other team members. **Your job is binary parsing — nothing more and nothing less.**

### Scale

- 12+ different hardware variants, each with its own binary protocol (ICD)
- Up to 100 hardware devices connected simultaneously
- 1,000–2,000 messages per second sustained throughput

---

## 2. Your Role — What the C++ Developer Owns

You are **Person C** in the 17-week v2 hardening execution plan. Your specific ownership:

### Confirmed Responsibilities

| Responsibility | Source |
|---|---|
| Write one C++ shared library (`.dll`) per hardware variant | Developer Handbook §9, Execution Plan |
| Implement `extract_frame` — binary frame detection and reassembly from TCP stream | Developer Handbook §9.3 |
| Implement `parse_message` — decode complete binary frame into JSON string | Developer Handbook §9.3 |
| Implement `format_response` — encode JSON response dict into binary DRS→SDFC frame | Developer Handbook §9.3 |
| Implement `free_result` — free the JSON string allocated by `parse_message` | Developer Handbook §9.3 |
| Write golden-frame unit tests (one fixture per response type per variant) | Developer Handbook §15.5 |
| Provide CMakeLists.txt entries for new variants | Developer Handbook §12.7 |
| Work with ICD Excel documents provided by the customer | ICD Codegen Design |
| Convert hardware-native units to SI units inside the parser | Developer Handbook §9.3 |
| Migrate legacy `command.csv` / `structure.csv` per-variant data to C++ parsers | Developer Handbook §9.5 |

### Inferred Responsibilities (High Confidence)

| Responsibility | Basis for Inference |
|---|---|
| Help build or validate the `tools/icd_codegen` Python tool | Codegen design doc explicitly says "C++ developer adding hardware variants" is the target audience |
| Provide the reference ICD fixture for codegen tests | Codegen design doc §10 — COMM DF ICD is the reference fixture |
| Review generated C++ skeleton output for correctness | Codegen tool produces dispatch scaffolding; you fill in field-decode bodies |
| Debug `ctypes` integration failures at the Python boundary | You own the ABI; integration bugs between Python and C++ are a shared responsibility with the `drs-bridge` Python developer (Person A) |

### What You Do NOT Own

| Area | Owner |
|---|---|
| TCP server / async I/O in `drs-bridge` | Person A (Python developer) |
| Kafka message production | Person A (Python developer) |
| TimescaleDB schema and writes | Person F (drs-server lead) |
| C# WPF scenario authoring app (`Sg.App`) | Person B (C# developer) |
| STK COM integration | Person B (C# developer) |
| DRS webapp (Angular/React browser UI) | Person G |
| System architecture and cross-stack ADRs | Person D (architect) |
| REST API routes in `drs-server` | Person E |

---

## 3. Architecture from the C++ Perspective

### System Layers (Zoomed into WS2)

```
WS1                                WS2
┌────────────────────┐             ┌───────────────────────────────────────────────────────────────┐
│                    │             │                                                               │
│  Sg.App.exe        │ ◄─HTTP──────│  drs-bridge (Python)                                         │
│  (C# WPF + STK)    │             │  ┌─────────────────────────────────────────────────────┐     │
│                    │             │  │ Layer 4: Supervisor / Lifecycle (supervisor.py)      │     │
│                    │             │  │ Layer 3: ResponseRouter (mode: Random/Scenario/HW)   │     │
└────────────────────┘             │  │ Layer 2: *** YOUR C++ CODE (via ctypes) ***          │     │
                                   │  │   frame_dispatcher.py → librdfs.dll                  │     │
                                   │  │                        → libjvuhf.dll                │     │
                                   │  │                        → libcomm_df.dll  ...          │     │
                                   │  │ Layer 1: TCP server / Kafka producer                 │     │
                                   │  └─────────────────────────────────────────────────────┘     │
                                   │          │ (Kafka)                                           │
                                   │  drs-server (Python FastAPI)                                │
                                   │    → TimescaleDB (measurements hypertable)                   │
                                   │    → WebSocket broadcast to DRS webapp                      │
                                   └───────────────────────────────────────────────────────────────┘
                                               │ (TCP, binary protocol)
                                         Physical DRS Hardware
                                         (RDFS, JHF, JVUHF, etc.)
```

### Your C++ Code in Context

The Python `drs-bridge` loads your shared library using Python's `ctypes` module:

```
Physical hardware ──(binary TCP)──► drs-bridge Python TCP handler
                                         │
                                         │ calls ctypes
                                         ▼
                             YOUR C++ library (.dll)
                                extract_frame()    ← "Is this a complete frame?"
                                parse_message()    ← "What does this frame mean?"
                                format_response()  ← "Encode this JSON as bytes"
                                free_result()      ← "Release the JSON string memory"
                                         │
                                         │ returns JSON string
                                         ▼
                             Python: route to Kafka, send response
```

Your C++ libraries are **pure data transformation** — bytes in, JSON out. No networking, no Kafka,
no threading, no UI. The rest of the system is built around you.

### drs-bridge Four-Layer Model

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  LAYER 4 — LIFECYCLE & SUPERVISION                                          │
│  supervisor.py, health_reporter.py, config.py                               │
│  Loads YAML profiles, starts/restarts TCP servers per hardware variant      │
├─────────────────────────────────────────────────────────────────────────────┤
│  LAYER 3 — RESPONSE ROUTING (mode-aware)                                    │
│  response_router.py                                                         │
│  Decides: return random data, scenario data, or relay to real hardware      │
├─────────────────────────────────────────────────────────────────────────────┤
│  LAYER 2 — PARSE / ENCODE  ◄── THIS IS YOU                                  │
│  frame_dispatcher.py + YOUR C++ shared libraries                            │
│  extract_frame(), parse_message(), format_response(), free_result()         │
│  Input: raw bytes buffer  Output: JSON string (or binary frame)             │
├─────────────────────────────────────────────────────────────────────────────┤
│  LAYER 1 — TRANSPORT (asyncio TCP)                                          │
│  tcp_server.py: accept connections, accumulate bytes, drive layer 2,        │
│  kafka_producer.py: publish parsed messages                                 │
└─────────────────────────────────────────────────────────────────────────────┘
```

**The rule for Layer 2:** No network I/O. No Kafka. No Python. No threading. Pure byte ↔ JSON
transformation.

---

## 4. Important Files, Classes, and Modules

### Files You Will Write / Own

```
drs-bridge/                          [FUTURE — does not exist yet]
├── bridge/
│   └── parsers/
│       ├── CMakeLists.txt           ← Build system for all variants
│       ├── common/
│       │   ├── parser_api.h         ← The 4-symbol ABI contract (shared header)
│       │   ├── frame_buffer.cpp     ← Shared byte-buffer utilities
│       │   └── json_writer.cpp      ← Shared JSON string builder
│       │
│       ├── rdfs/
│       │   ├── rdfs_frame_types.h   ← Auto-generated constants (magic bytes, IDs)
│       │   └── rdfs_parser.cpp      ← Hand-completed field decoders
│       │
│       ├── jvuhf/
│       │   ├── jvuhf_frame_types.h
│       │   └── jvuhf_parser.cpp
│       │
│       ├── jhf/ ...
│       ├── sjrr/ ...
│       ├── jlb/ ...
│       ├── jmb/ ...
│       ├── jhb/ ...        (×4 variants)
│       ├── aus/ ...
│       ├── pads/ ...
│       └── comm_df/        ← REFERENCE VARIANT (used in tests and ICD codegen)
│           ├── comm_df_frame_types.h
│           └── comm_df_parser.cpp
│
└── profiles/
    ├── rdfs.yaml            ← YAML profile (not C++, but you provide the values)
    ├── jvuhf.yaml
    └── ...
```

### Files You Will Read / Depend On

```
docs/ewtss/icd-reference-comm-df.md    ← COMM DF ICD worked example — read this first
docs/ewtss/developer-handbook.md §9    ← Add-a-variant step-by-step procedure
docs/ewtss/developer-handbook.md §12   ← drs-bridge internal design
docs/ewtss/specs/icd-codegen-tool-design.md   ← Codegen tool spec
docs/superpowers/specs/2026-05-14-b1-3-time-sync-design.md   ← Time sync per-variant adapter
```

### Key Structures (C++)

#### `parser_api.h` — The ABI Contract

```cpp
// parsers/common/parser_api.h — this header is fixed; deviations break Python integration
extern "C" {
    int         extract_frame(const uint8_t* buf, int buf_len,
                               uint8_t* out_frame, int* out_len);
    const char* parse_message(const uint8_t* frame, int frame_len, int frame_type);
    int         format_response(const char* json_response, uint8_t* out_frame);
    void        free_result(const char* result);
}
```

#### `{hw}_frame_types.h` — Auto-Generated Constants

```cpp
// AUTO-GENERATED by tools/icd_codegen — do not edit after generation
namespace comm_df {
    constexpr uint8_t CMD_HEADER[4]  = {0xAA, 0xAB, 0xBA, 0xBB};
    constexpr uint8_t RESP_HEADER[4] = {0xEE, 0xEF, 0xFE, 0xFF};
    constexpr uint8_t SCD_HEADER[2]  = {0xAA, 0xAA};
    constexpr int     CMD_OVERHEAD   = 16;
    constexpr int     RESP_OVERHEAD  = 18;
    constexpr int     SCD_OVERHEAD   = 12;
    constexpr int     MAX_PAYLOAD    = 1048576;
    constexpr uint16_t GROUP_SYSTEM_MGMT  = 100;
    constexpr uint16_t CMD_GET_SYSTEM_VERSION = 1;
    constexpr uint16_t RESP_SYSTEM_VERSION    = 2;
    // ... one constant per command/response
}
```

#### Python `ctypes` Dispatcher — How Python Calls Your Code

```python
# frame_dispatcher.py — written by Person A; you need to understand this
class ParserHandle:
    def __init__(self, lib_path: str):
        self.lib = ctypes.CDLL(lib_path)
        self.lib.extract_frame.restype  = ctypes.c_int
        self.lib.parse_message.restype  = ctypes.c_char_p
        self.lib.format_response.restype = ctypes.c_int
        # ... argtypes set here

    def extract(self, buf: bytes) -> tuple[bytes, int] | None:
        out_frame = ctypes.create_string_buffer(1048576 + 64)
        out_len   = ctypes.c_int(0)
        ftype = self.lib.extract_frame(buf, len(buf), out_frame, ctypes.byref(out_len))
        if ftype <= 0:
            return None
        return bytes(out_frame[:out_len.value]), ftype

    def parse(self, frame: bytes, frame_type: int) -> dict:
        result = self.lib.parse_message(frame, len(frame), frame_type)
        data   = json.loads(result)
        self.lib.free_result(result)   # ← You must match allocator in free_result
        return data
```

---

## 5. Data Flow — End to End

### Normal Message Flow (Command → Response)

```
1. Physical SDFC hardware sends a binary TCP command to drs-bridge

2. tcp_server.py accumulates bytes into a buffer

3. YOUR extract_frame() is called:
   - Scans buffer for a valid SDFC→DRS command header (0xAA 0xAB 0xBA 0xBB)
   - Validates the length field and footer bytes
   - Returns frame_type=1 (SDFC→DRS command) and copies frame bytes
   - Returns 0 if frame is incomplete (caller waits for more data)
   - Returns -1 if corrupt (caller drains buffer, reconnects)

4. YOUR parse_message() is called with frame_type=1:
   - Reads the group_id and unit_id from the frame header
   - Dispatches to the matching parse_group_NNN() function
   - Decodes each field (using uint32, uint16, float32, etc. unpacking)
   - Converts hardware-native units → SI units (kHz→Hz, dBuV→dBm, etc.)
   - Returns a heap-allocated JSON string like:
     {"frame_type":"command","group_id":101,"unit_id":25,"threshold_dbm":-80.0}

5. Python records the decoded command to Kafka (for audit)

6. ResponseRouter decides the response:
   - RANDOM mode: Python generates fake values within min/max bounds
   - SCENARIO mode: Python fetches pre-computed scenario data from Sg.App
   - INTEGRATED mode: Python relays to real hardware

7. Python has a JSON response string, calls YOUR format_response():
   - Receives JSON like: {"group_id":101,"unit_id":26,"status":0,"result":"ok"}
   - Encodes into binary DRS→SDFC response frame (header 0xEE 0xEF 0xFE 0xFF)
   - Returns number of bytes written into out_frame buffer

8. Python sends binary response bytes back to SDFC over the same TCP connection

9. Python also publishes decoded response JSON to Kafka → drs-server → TimescaleDB
```

### Frame Formats You Must Handle

| Name | Direction | Header (4 bytes) | Footer (4 bytes) | Overhead |
|---|---|---|---|---|
| `sdfc_drs` command | SDFC → DRS | `0xAA 0xAB 0xBA 0xBB` | `0xCC 0xCD 0xDC 0xDD` | 16 bytes |
| `sdfc_drs` response | DRS → SDFC | `0xEE 0xEF 0xFE 0xFF` | `0xFF 0xFE 0xEF 0xEE` | 18 bytes |
| `scd_drs` compact | bidirectional | `0xAA 0xAA` | `0xEE 0xEE` | 12 bytes |

> **Important design note:** The SDFC command header starts with `0xAA 0xAB...` and the SCD compact
> header starts with `0xAA 0xAA`. They share a common first byte. This is why `extract_frame` must
> return the frame type — it eliminates the ambiguity when `parse_message` is called.

### extract_frame Return Value Semantics

| Return | Meaning |
|---|---|
| `1` | Complete SDFC→DRS command frame found |
| `2` | Complete DRS→SDFC response frame found |
| `3` | Complete SCD compact frame found |
| `0` | Incomplete frame — buffer needs more bytes |
| `-1` | Corrupt/unrecognised header — Python drains buffer and reconnects |

### JSON Output Format (what parse_message returns)

All output keys use **SI units**. Convert in the parser, not in Python:

```json
{
  "frame_type": "response",
  "group_id": 101,
  "unit_id": 70,
  "status": 0,
  "frequency_hz": 102400000,
  "power_dbm": -63.5,
  "azimuth_deg": 214.3,
  "elevation_deg": 3.1
}
```

| Hardware unit | SI unit key in JSON |
|---|---|
| kHz | `frequency_hz` (multiply × 1000) |
| dBuV/m | `power_dbm` (convert to dBm) |
| degrees (azimuth) | `azimuth_deg` |
| degrees (elevation) | `elevation_deg` |

---

## 6. Threading and Concurrency

### C++ Parser Threading Requirements

**You do not need to write any threading code.** The drs-bridge Python layer handles all
concurrency using `asyncio` — a single-threaded cooperative multitasking model.

What this means for your C++:

- **One parser library instance per hardware process** — each variant runs as its own
  `drs-bridge` process with its own library instance. There is no shared state between
  hardware types at the library level.

- **One coroutine per TCP connection** — each connected hardware device has its own asyncio
  coroutine. Python calls your library functions sequentially within that coroutine — no two
  calls to the same library instance are concurrent.

- **Your functions must be reentrant** — because multiple connections can share the same
  library, and Python may call your functions from different async tasks (which ultimately
  run on one thread but interleave). In practice this means: **do not use global or static
  mutable state in your parsers.**

### Per-Connection State (Python, Not Yours)

```python
# tcp_server.py — the Python TCP handler
async def handle_client(...):
    buffer = b""                              # per-connection accumulation buffer
    resp_buf = (ctypes.c_uint8 * (1048576 + 64))()  # pre-allocated encode buffer
    while True:
        chunk = await reader.read(...)
        buffer += chunk
        while result := parser.extract(buffer):   # calls YOUR extract_frame
            (frame, frame_type) = result
            buffer = buffer[len(frame):]           # Python advances the buffer
            msg = parser.parse(frame, frame_type)  # calls YOUR parse_message
            ...
```

Python owns the accumulation buffer and connection lifecycle. Your `extract_frame` receives a
snapshot of that buffer each call — you do not hold state between calls.

### Thread-Safety Summary

| Concern | Status |
|---|---|
| Multiple connections to same variant | Safe: Python asyncio is single-threaded |
| Global/static state in parser | **Forbidden** — causes subtle corruption under async interleaving |
| Shared `resp_buf` per connection | Safe: Python passes the buffer each call |
| Memory allocated by `parse_message` | Must be freed by `free_result` only; Python calls both in the same coroutine |

---

## 7. Qt-Related Responsibilities

**None.** This project does not use Qt anywhere.

The desktop GUI (`Sg.App`) is written in C# with WPF (Windows Presentation Foundation) — a
Microsoft UI framework for Windows. That part of the system is owned by Person B (C# developer).

Your C++ code is a headless shared library with no UI component.

---

## 8. Networking and Protocol Responsibilities

### What You Do NOT Own

You do **not** write TCP socket code. Python's `asyncio.start_server()` handles all networking in
`tcp_server.py`. You receive an already-accumulated byte buffer and return frames from it.

### What You DO Own — Binary Protocol Parsing

You are responsible for understanding the hardware binary protocols described in the ICD
(Interface Control Document) Excel files provided by the customer. Specifically:

**Frame detection (`extract_frame`):**
- Scan the byte buffer for a valid frame header (magic bytes)
- Validate the length field to determine if enough bytes are available
- Validate the footer bytes match expectations
- Handle TCP stream reassembly — a single TCP chunk may contain partial frames; your function
  returns 0 (incomplete) until a complete frame is present

**Message decoding (`parse_message`):**
- Extract the frame header fields: `group_id`, `unit_id`, `status` (for responses)
- Dispatch to the correct group parser based on `group_id`
- Within each group, dispatch to the correct command parser based on `unit_id`
- Decode payload fields using the ICD's type definitions (`uint32_t`, `uint16_t`, `float32`,
  fixed-length arrays, etc.)
- Serialize to a JSON string

**Response encoding (`format_response`):**
- Receive a JSON string with `group_id`, `unit_id`, and `status`
- Look up the expected response payload fields from constants in `{hw}_frame_types.h`
- Write the binary response frame including header, payload, footer, and length field

### Protocol Variants

| Protocol | Notes |
|---|---|
| `sdfc_drs` | Main binary protocol for most variants. Two sub-formats: CMD and RESP |
| `scd_drs` | Compact format used by some variants alongside `sdfc_drs` |
| `nmea` | ASCII NMEA sentences for GNSS-capable variants only |

The YAML profile for each hardware variant declares which protocols it uses. CMake compile-time
defines can be used to exclude unused frame parsers from the compiled library.

---

## 9. Build and Deployment

### Build System: CMake

All parser libraries are built together in a single CMake project:

```cmake
# drs-bridge/bridge/parsers/CMakeLists.txt
cmake_minimum_required(VERSION 3.20)
project(drs_parsers)

# Shared utilities — linked into every variant library
add_library(parser_common STATIC
    common/frame_buffer.cpp
    common/json_writer.cpp
)

# One shared library per hardware variant
foreach(HW srx mrx gnss jvuhf jhf rdfs sjrr jlb jmb jhb aus pads comm_df)
    add_library(${HW} SHARED
        ${HW}/${HW}_parser.cpp
        common/frame_buffer.cpp
        common/json_writer.cpp
    )
    target_include_directories(${HW} PRIVATE common)
    set_target_properties(${HW} PROPERTIES
        PREFIX "lib"
        OUTPUT_NAME "${HW}"
    )
endforeach()
```

Adding a new hardware variant = adding its name to the `foreach` loop above and providing the
`<variant>/<variant>_parser.cpp` source file.

### Build Commands

```bat
REM From the drs-bridge directory (Developer Command Prompt for VS 2022):
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release

REM Or use the convenience script:
build_parsers.bat
```

The output `.dll` files land in `build/Release/lib<variant>.dll`.

### Deployment Policy (Critical)

**CI pre-builds and commits the `.dll` files to `packages/parsers/`.** This is a deliberate design
decision. The customer site for DVD installation does **not** have a C++ toolchain. You build on
your development machine; the artifacts are versioned and shipped as pre-built binaries. This means:

1. Every time you change a parser, rebuild all variants and commit the updated `.dll` to the repo.
2. The CI pipeline runs C++ unit tests against the pre-built binaries on every PR.
3. The Python `drs-bridge` loads these pre-built `.dll` files at runtime via `ctypes.CDLL()`.

### Compiler Requirements

- **Compiler:** MSVC (Visual Studio 2022, cl.exe) — the libraries must be ABI-compatible with
  the Python runtime on WS2 (Windows 11 x64).
- **Standard:** C++17 minimum (needed for structured bindings, `if constexpr`, etc.).
- **No exceptions across the DLL boundary** — Python `ctypes` does not handle C++ exceptions.
  All error conditions are communicated through return values (`-1` for errors).
- **No standard library objects across the DLL boundary** — do not return `std::string` or
  `std::vector`; use `const char*` (heap-allocated) per the ABI contract.

### Air-Gap Install Requirement

The customer operates in an air-gapped environment (no internet). All dependencies must be
vendored under `packages/`. For C++:
- No internet-required CMake downloads (`FetchContent` with a URL is **forbidden**)
- All third-party headers/libraries vendored under `packages/cpp/`
- CMake `find_package` or manual `include_directories` pointing to `packages/cpp/`

---

## 10. The C++ Parser ABI Contract (Critical)

This is the most important technical specification in the project. Deviations break the Python
`ctypes` integration and are considered contract violations requiring cross-team review (24-hour SLA
per Developer Handbook §15.6).

### The Four Exported Symbols

```cpp
// parsers/common/parser_api.h
// Include in every variant's parser.cpp

#pragma once
#include <cstdint>

extern "C" {

    /**
     * Scans buf for the next complete frame.
     *
     * Returns:
     *   1  = SDFC→DRS command   (header 0xAA 0xAB 0xBA 0xBB)
     *   2  = DRS→SDFC response  (header 0xEE 0xEF 0xFE 0xFF)
     *   3  = SCD compact frame  (header 0xAA 0xAA)
     *   0  = incomplete frame — need more bytes
     *  -1  = corrupt / unrecognised — caller drains buffer and reconnects
     *
     * On success (return 1/2/3): writes frame bytes to out_frame, sets *out_len.
     */
    int extract_frame(
        const uint8_t* buf, int buf_len,
        uint8_t* out_frame, int* out_len
    );

    /**
     * Decodes a complete frame into a JSON string.
     * frame_type is the value returned by extract_frame (1, 2, or 3).
     *
     * Returns: heap-allocated JSON string (new[]).
     *          Caller MUST call free_result() to release it.
     *          Returns nullptr on catastrophic parse failure (log and skip).
     */
    const char* parse_message(
        const uint8_t* frame, int frame_len, int frame_type
    );

    /**
     * Encodes a JSON response dict into a DRS→SDFC binary response frame.
     *
     * json_response: UTF-8 JSON; MUST contain "group_id" (int), "unit_id" (int),
     *                "status" (int) keys.
     * out_frame: caller-allocated buffer; at least MAX_PAYLOAD + RESP_OVERHEAD bytes.
     *
     * Returns: bytes written, or -1 on encoding error.
     */
    int format_response(
        const char* json_response,
        uint8_t* out_frame
    );

    /**
     * Frees the JSON string returned by parse_message.
     * MUST use delete[] (matching the new[] in parse_message).
     */
    void free_result(const char* result);
}
```

### Parser Invariants (Non-Negotiable)

| Invariant | Why |
|---|---|
| No Kafka, no network, no Python calls from C++ | Separation of concerns; Layer 2 rule |
| No exceptions across the DLL boundary | Python ctypes cannot catch C++ exceptions |
| `free_result` must `delete[]` the same buffer `parse_message` `new[]`-allocated | Mismatched allocators are undefined behaviour |
| All numeric output in SI units | Python, Kafka, database code assumes SI; hardware-native units stay inside the parser |
| No global or static mutable state | Reentrance safety; multiple connections share the same library instance |
| `out_frame` buffer size >= `MAX_PAYLOAD + RESP_OVERHEAD` | Python pre-allocates 1,048,576 + 64 bytes; do not write past that |

### Memory Contract

```
parse_message allocates:  result = new char[json_len + 1]
Python reads result via:  json.loads(result_c_char_p)
free_result releases:     delete[] result
```

Python calls `free_result` immediately after reading. Do not assume the pointer is valid after
`free_result` returns. If `parse_message` returns `nullptr`, Python logs and skips — the caller
handles the null check.

---

## 11. Adding a New Hardware Variant — Step by Step

This is the core add-a-variant procedure from Developer Handbook §9, reformatted for quick reference.

### Prerequisites

- The hardware's ICD Excel document (from the customer)
- The variant name (e.g. `rdfs`, `jvuhf`) — used as namespace and filename prefix
- The hardware's TCP port number — goes into the YAML profile

### Steps

```
1. Run the ICD codegen tool (generates skeleton):

   python tools/icd_codegen/icd_codegen.py \
     --icd    "ICD_RDFS.xlsx"              \
     --hw     rdfs                          \
     --port   5485                          \
     --out-parsers  drs-bridge/parsers/src/ \
     --out-profiles drs-bridge/profiles/

   Output:
     drs-bridge/parsers/src/rdfs/rdfs_frame_types.h   ← auto-generated, do not edit
     drs-bridge/parsers/src/rdfs/rdfs_parser.cpp       ← skeleton, fill in bodies
     drs-bridge/profiles/rdfs.yaml                     ← YAML profile, edit port/broker

2. Review rdfs_frame_types.h:
   - Verify magic bytes match the ICD
   - Verify group IDs and command/response unit IDs
   - Correct any extraction errors manually

3. Complete rdfs_parser.cpp field-decode bodies:
   - Each generated case has a TODO comment listing ICD fields
   - Decode each field using the correct C type (uint32_t, uint16_t, float, etc.)
   - Handle endianness (most DRS hardware is little-endian; confirm with ICD)
   - Convert hardware-native units to SI units before writing JSON

4. Add the variant to CMakeLists.txt:
   foreach(HW ... rdfs ...)    ← add rdfs to the list

5. Build and confirm compilation:
   cmake --build build --config Release

6. Write golden-frame unit tests:
   - Capture at least one real binary frame per command type (from hardware or simulator)
   - Assert extract_frame returns correct frame_type and frame length
   - Assert parse_message returns expected JSON fields and values
   - Golden frames live in: drs-bridge/parsers/tests/fixtures/<variant>/

7. Edit drs-bridge/profiles/rdfs.yaml:
   - Set port to the hardware's TCP port
   - Set kafka_topic, kafka_broker
   - Set receive_buffer_bytes to at least the ICD's max response size
   - Verify protocols list matches what the hardware actually uses

8. Add the Kafka topic to deployment scripts:
   - hw.rdfs.drs.ui (parsed messages from hardware)
   - hw.rdfs.drs.ui.cmd (inbound commands, for audit)

9. Test end-to-end:
   - Connect hardware device or simulator
   - Confirm Kafka messages appear on hw.rdfs.drs.ui
   - Confirm rows appear in the measurements hypertable in TimescaleDB
```

### Migrating a Legacy Variant (from command.csv)

For variants already in v1's `command.csv` / `structure.csv` format:

1. Feed the existing `command.csv` to the codegen tool (if it can parse it) or hand-transcribe
   the constants into `{hw}_frame_types.h`
2. Follow steps 3–9 above
3. Delete the legacy: `command.csv`, `structure.csv`, `parse_request_param.py`,
   `random_value_generator.py`

The four v1 fragility points that you are fixing by doing this:
- Silent `ast.literal_eval` failures on malformed CSV cells
- No TCP stream reassembly (v1 assumed one frame per TCP segment)
- Inline magic bytes scattered as string literals across 50-branch if-elif chains
- No compile-time type checking of field offsets

---

## 12. External Dependencies and Libraries

### C++ (Your Side)

| Library | Version | Use | Licence | Status |
|---|---|---|---|---|
| C++ Standard Library (MSVC) | C++17 | `<cstdint>`, `<cstring>`, endian utilities | Platform — no licence concern | Already bundled with MSVC |
| `nlohmann/json` (if used) | ≥ 3.11 | JSON serialisation in `json_writer.cpp` | MIT ✓ | To confirm — if not using a bespoke writer |
| No other runtime deps required | — | — | — | The design intentionally keeps the library minimal |

> **LGPL is forbidden** for C++ parser libraries (Developer Handbook §15.7) because static
> linkage of an LGPL library contaminates the IP transfer to the customer. Only use MIT,
> Apache 2.0, BSD, ISC, Boost, or public domain libraries. Check with Person D before
> adopting any new C++ dependency.

### Python Side (You Interact With)

| Library | Version | Use in drs-bridge |
|---|---|---|
| `ctypes` (stdlib) | Any | Load your `.dll` and call the 4 C symbols |
| `aiokafka` | ≥ 0.8 | Async Kafka producer (owned by Person A) |
| `pyyaml` | ≥ 6.0 | Load `profiles/*.yaml` |

### ICD Codegen Tool (Python — You Contribute)

| Library | Version | Use |
|---|---|---|
| `openpyxl` | ≥ 3.1 | Read ICD Excel workbooks |
| `jinja2` | ≥ 3.1 | Render C++/YAML/TypeScript templates |
| `markupsafe` | ≥ 2.1 | jinja2 dependency |

All vendored under `tools/icd_codegen/packages/` for air-gap install.

---

## 13. Coding Standards and Patterns

### C++ Conventions

```cpp
// File structure per variant:
#include "<variant>_frame_types.h"   // constants (auto-generated)
#include "../common/parser_api.h"    // the 4-symbol ABI
#include "../common/json_writer.h"   // JSON builder helper
using namespace <variant>;           // bring constants into scope

// Namespace: one per hardware variant
namespace rdfs { ... }

// Naming: snake_case for functions, UPPER_CASE for constants
// Constants live in the auto-generated _frame_types.h; do not duplicate in parser.cpp

// Error propagation: return -1 (extract_frame, format_response) or nullptr (parse_message)
// No exceptions across the DLL boundary

// Unit conversion: inline, at the point of assignment
// Bad:  w.set("frequency", raw_khz);
// Good: w.set("frequency_hz", static_cast<double>(raw_khz) * 1000.0);
```

### JSON Writer Helper

The common `json_writer.cpp` provides a minimal JSON string builder. Usage pattern:

```cpp
static void parse_group_101(const uint8_t* payload, int len,
                             uint16_t unit_id, JsonWriter& w) {
    switch (unit_id) {
        case RESP_SYSTEM_VERSION: {
            uint32_t fw_ver;
            std::memcpy(&fw_ver, payload + 0, 4);   // check ICD endianness
            w.set("fw_version", fw_ver);
            // ... decode remaining fields
            break;
        }
        default:
            w.set("raw_hex", to_hex(payload, len));  // graceful unknown-command fallback
    }
}
```

### No-Comment Default

The project follows a no-comment default. Only comment when the **why** is non-obvious:
- A hidden ICD constraint (e.g. "length field is in 16-bit words, not bytes")
- A hardware quirk (e.g. "RDFS firmware bug: unit_id 0x42 uses response header but is sent as command frame_type")
- A surprising bit-packing layout not visible from the field types

Do not comment what the code already says.

### Test Pattern for Golden-Frame Fixtures

```cpp
// tests/<variant>/test_<variant>_parser.cpp
TEST(RdfsParser, ExtractFrame_CompleteCommandFrame_ReturnsFrameTypeOne) {
    // Golden binary fixture from real hardware capture
    const uint8_t raw[] = {
        0xAA, 0xAB, 0xBA, 0xBB,   // CMD_HEADER
        0x00, 0x00, 0x00, 0x08,   // length = 8 bytes payload
        0x00, 0x64,               // group_id = 100
        0x00, 0x01,               // unit_id  = 1
        // ... payload ...
        0xCC, 0xCD, 0xDC, 0xDD,   // CMD_FOOTER
    };
    uint8_t out_frame[4096];
    int     out_len = 0;

    int result = extract_frame(raw, sizeof(raw), out_frame, &out_len);

    EXPECT_EQ(result, 1);
    EXPECT_EQ(out_len, sizeof(raw));
}

TEST(RdfsParser, ParseMessage_SystemVersionResponse_CorrectJson) {
    // ... golden frame ...
    const char* json = parse_message(frame, frame_len, 2);
    ASSERT_NE(json, nullptr);
    auto doc = nlohmann::json::parse(json);
    EXPECT_EQ(doc["group_id"], 100);
    EXPECT_EQ(doc["unit_id"], 2);
    EXPECT_TRUE(doc.contains("fw_version"));
    free_result(json);
}
```

### CI Checks Applied to Your Code

| Check | Tool | When |
|---|---|---|
| Code style | `clang-tidy` | Every PR |
| Compilation (all variants) | MSVC + CMake | Every PR |
| Golden-frame unit tests | CMake CTest | Every PR |
| No LGPL/GPL dependencies | Manual scan of `packages/cpp/` | Every PR |
| Pre-built `.dll` committed | CI artifact check | Pre-release |

---

## 14. Potential Future Tasks

These are tasks not yet confirmed in the execution plan but highly likely based on the
architecture trajectory and explicit design document references:

### Near-Term (v2 Hardening Phase, 17 Weeks)

| Task | Basis |
|---|---|
| Implement parsers for 6–8 variants (Weeks 1–12) | Execution plan Person C deliverables |
| Build the ICD codegen tool as a 1-week parallel task | Codegen design doc §13 recommendation |
| Write COMM DF parser as the reference/first variant | Codegen design — COMM DF is the reference ICD fixture |
| Write golden-frame test suite per variant | Developer Handbook §15.5 CI requirement |
| Implement TCP stream reassembly correctly | AP-13 (legacy anti-pattern): v1 assumed one frame per TCP segment |
| Add time-sync adapter in parser (per-variant timestamp handling) | Time-sync design §4 Layer B |

### Medium-Term (Post-v2)

| Task | Basis |
|---|---|
| Add parsers for Transmitter, Receiver, Antenna entity types | Developer Handbook §8 — forward catalogue |
| Add satellite entity type parser | Developer Handbook §8 — "maybe satellite" in forward catalogue |
| Support NMEA ASCII frame type for GNSS variants | YAML profile supports `nmea` protocol flag |
| Profile and optimise parser throughput for 2,000 msg/s gate | Execution Plan Phase 6 (Week 16–17) |
| Support ICD revision updates (manual merge or re-run of codegen) | Codegen design §2 — ICD revision management |

### Long-Term / Mode B

| Task | Basis |
|---|---|
| TypeScript type generation validation (from codegen tool output) | Codegen design §8 — TypeScript interfaces for Mode B Angular SPA |
| Real-time compute parser for scenario generator | Architecture overview — Integrated + Scenario mode future |

---

## 15. Open Questions and Ambiguities

These are items the design documents identify as unclear, deferred, or explicitly open:

### Confirmed Open Items (Documents Say "Unresolved")

| Item | Where Documented | Impact on C++ |
|---|---|---|
| **ICD non-standard sheet names:** Some customer ICDs may use different Excel sheet names than what the codegen tool expects. `--sheet-map` override exists but real ICD compatibility is unverified. | Codegen design §13 "Drop" criteria | Could require manual constants transcription for non-conformant ICDs |
| **NMEA protocol details:** The YAML profile supports `nmea` but no NMEA frame format is documented in the ICD reference doc. | ICD reference doc (omission) | You will need to source NMEA spec separately for GNSS variants |
| **checksum/CRC algorithm per variant:** The codegen tool explicitly does not generate checksum code because "algorithm + polynomial are not in ICD Excel." | Codegen design §2 | You must confirm CRC algorithm from hardware spec / customer, not from ICD |
| **Bit-packing and conditional field layouts:** Acknowledged as requiring "judgement" beyond codegen. | Codegen design §2 | Plan for non-trivial field decoding time per variant |
| **Endianness confirmation per variant:** Not documented per variant; assume little-endian but must verify from ICD or hardware test. | (Gap — not mentioned explicitly) | Could cause subtle parsing failures if assumed wrong |

### Inferred Gaps (Not Explicitly Stated as Open, But Potentially Problematic)

| Item | Concern |
|---|---|
| `common/json_writer.h` implementation is not specified | Only the interface is shown in examples. You will need to either write this utility or confirm the team is using a third-party library like `nlohmann/json` |
| `common/frame_buffer.cpp` contents are not specified | The CMakeLists.txt references it, but no source is provided. Presumably a byte-buffer utility — clarify with Person A |
| Maximum payload size varies per ICD | The constant `MAX_PAYLOAD = 1048576` (1 MB) is the default, but some variants (like FFT data at 6,404 bytes) are much smaller. The Python `ctypes` buffer is pre-allocated at 1 MB + 64 bytes — confirm this is always sufficient |
| Error JSON format on parse failure | When `parse_message` encounters an unexpected unit_id, it returns `raw_hex`. Is that the expected fallback? Confirm with Person F (drs-server) what it does with raw_hex payloads |
| `format_response` completeness | The design shows encoding "group_id, unit_id, status" fields but not the full response payload. How does drs-bridge know the correct response payload for RANDOM mode? It appears Python generates the payload independently; you only encode headers. Clarify with Person A. |

### Risk Register Entries Directly Affecting You

| Risk ID | Description | Current Mitigation |
|---|---|---|
| **R1** | C++ build complexity — MSVC toolchain setup, ctypes ABI, cross-language integration | Early parser ABI contract lock; integration tests from Week 3 |
| **R3** | Parser complexity — 12+ variants, each with unique ICD quirks | ICD codegen tool for mechanical parts; Person D review on complex variants |
| **R4** | ICD incompleteness — customer ICDs may be incomplete or have undocumented fields | Weekly ICD review with customer contact (P1 programme risk mitigation) |

---

## 16. Assumptions (Separated Clearly)

The following are assumptions not explicitly confirmed in the design documents. Verify these before
starting implementation:

### High-Confidence Assumptions (Very Likely True)

| Assumption | Evidence |
|---|---|
| Hardware protocols are little-endian | Common for embedded DSP/RF hardware; COMM DF ICD example uses `uint32_t` without explicit endian note |
| Python runtime on WS2 is 64-bit CPython (not PyPy) | Windows 11 default Python install |
| MSVC 2022 is the C++ compiler | Execution plan mentions "Developer Command Prompt for VS 2022"; build_parsers.bat implies MSVC |
| The `common/` utilities (json_writer, frame_buffer) are team-shared and you contribute to them | CMakeLists.txt links all variants against them; they are not per-variant |

### Medium-Confidence Assumptions (Verify with Person A or D)

| Assumption | What to Verify |
|---|---|
| `json_writer.h` is a custom class, not `nlohmann/json` | Ask Person A or D; if nlohmann is used, it must be vendored and is MIT-licensed (allowed) |
| The YAML `protocols` field controls compile-time `#ifdef` in the parser, not runtime switching | Handbook §9.2 says "C++ parser reads this flag at compile time via a CMake define" — confirm CMake passes this as a `-D` flag |
| All 12+ variant names are finalised | Execution plan lists: srx, mrx, gnss, jvuhf, jhf, rdfs, sjrr, jlb, jmb, jhb (×4), aus, pads — confirm this list with the customer |
| format_response needs to produce a full binary response frame (not just headers) | The RANDOM mode router generates the JSON payload independently; clarify what `format_response` encodes |

### Low-Confidence Assumptions (Must Verify Before Starting)

| Assumption | Risk if Wrong |
|---|---|
| ICD Excel files are available at project start | If delayed, C++ implementation cannot begin. Parser ABI is on the critical path (blocks Week 3 integration test) |
| All variants use the same `parser_api.h` ABI | If a variant needs a different interface, the Python dispatcher needs changes (Person A involvement) |
| The golden-frame test fixtures can be generated from a hardware simulator | If real hardware is required, testing cannot happen in CI (nightly run requires STK; parser tests should not require hardware) |

---

## Diagram: C++ Developer's Complete Interaction Map

```
                                Customer
                                    │
                               ICD Excel files
                                    │
                                    ▼
                         tools/icd_codegen/          ← You contribute to this tool
                         (Python, ~1 week)
                                    │ generates
                                    ▼
                    ┌───────────────────────────────┐
                    │  {hw}_frame_types.h (auto)    │
                    │  {hw}_parser.cpp   (skeleton) │
                    │  {hw}.yaml         (profile)  │
                    └───────────────────────────────┘
                                    │ you complete the
                                    │ field-decode bodies
                                    ▼
                    ┌───────────────────────────────┐
                    │   C++ Parser Library (.dll)   │  ← Your primary deliverable
                    │                               │
                    │  extract_frame()              │
                    │  parse_message()              │
                    │  format_response()            │
                    │  free_result()                │
                    └───────────────────────────────┘
                           │ built by CMake
                           │ committed to packages/parsers/
                           │
                           ▼ loaded at runtime via ctypes
                    ┌───────────────────────────────┐
                    │   frame_dispatcher.py         │  ← Person A owns this
                    │   (Python ctypes wrapper)     │
                    └───────────────────────────────┘
                           │
                           ▼
                    ┌───────────────────────────────┐
                    │   drs-bridge TCP server       │  ← Person A owns this
                    │   + ResponseRouter            │
                    │   + Kafka producer            │
                    └───────────────────────────────┘
                           │
                           ▼
                    ┌───────────────────────────────┐
                    │   Kafka + drs-server          │  ← Person F owns this
                    │   + TimescaleDB               │
                    └───────────────────────────────┘
                           │
                           ▼
                    ┌───────────────────────────────┐
                    │   DRS webapp                  │  ← Person G owns this
                    │   (browser UI for DRS Eng.)   │
                    └───────────────────────────────┘
```

---

## Quick-Reference Table: Confirmed vs Inferred

| Requirement | Confirmed | Inferred |
|---|---|---|
| Implement 4-symbol C ABI | ✅ Developer Handbook §9.3 | |
| One `.dll` per hardware variant | ✅ Developer Handbook §9 | |
| CMake build system | ✅ Developer Handbook §12.7 | |
| SI units in JSON output | ✅ Developer Handbook §9.3 | |
| No global/static mutable state | ✅ (implied by async ctypes use) | |
| No exceptions across DLL boundary | ✅ ctypes constraint | |
| Pre-build DLLs committed to repo | ✅ Developer Handbook §9 step 4 | |
| LGPL libraries forbidden | ✅ Developer Handbook §15.7 | |
| Write golden-frame tests | ✅ Developer Handbook §15.5 | |
| Contribute to ICD codegen tool | | ✅ Codegen design §1 audience |
| Endianness: little-endian | | ✅ Common for this class of hardware |
| json_writer.h is custom (not nlohmann) | | ⚠️ Verify with Person A |
| NMEA frame format details available | | ⚠️ Not documented — must source |
| CRC algorithm in hardware spec (not ICD) | ✅ Codegen design §2 | |

---

*This document was generated from a full analysis of the EWTSS v2 design repository as of
2026-05-19. Update this document when ADRs change, the parser ABI evolves, or new hardware
variants are confirmed.*
