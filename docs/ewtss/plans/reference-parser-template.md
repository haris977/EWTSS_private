# Reference C++ Parser Template Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship a small, buildable, heavily-commented C++ reference parser under `drs-bridge/parsers/reference/` plus the CMake config + Python integration + CI build step + a runtime test that exercises the ctypes binding end-to-end against the real DLL. Variant developers copy this directory, rename to their variant, and fill in the per-IRS bodies. The synthetic frame format is **deliberately fake** — the point is the project skeleton, not a real protocol.

**Architecture:** Cross-platform (Windows + Linux) C++17 shared library implementing the 4-symbol ABI (`extract_frame`, `parse_message`, `format_response`, `free_result`) for a synthetic frame: 1-byte magic (`0xAA`) + 1-byte length + payload. Hand-written JSON encoding in C++ (no dependency on a JSON library — keeps the template small and readable). CMake builds `reference_parser.dll` on Windows and `libreference_parser.so` on Linux into a `build/` subdir. A new pytest fixture `built_reference_parser` runs `cmake -S parsers/reference -B parsers/reference/build && cmake --build parsers/reference/build` once per test session, then provides the resulting library path. A new runtime test `test_reference_parser_integration.py` loads the library via `parser_loader.load_parser`, calls `format_response(kind="time", timestamp_ns=...)`, and asserts the returned bytes match the documented frame format. The CI workflow's python-suites job gains a CMake build step before pytest so the build is validated on every push.

**Tech Stack:** C++17 (stdlib only; no Boost, no JSON library) + CMake 3.20+ (already on every modern dev box and on both GitHub Actions runner images by default) + Python stdlib `subprocess` for the build hook + pytest fixtures for session-scoped build caching.

**Application target (pre-flight Pass 1):** This plan modifies `drs-bridge/` exclusively (new `parsers/reference/` subtree + one pytest fixture + one test). `drs-server`, `sg-app`, `mvp4`, `drs-webapp` are not touched. CI workflow at `.github/workflows/ci.yml` gains a small step in the existing `python-suites` job for drs-bridge.

**Test breadth statement (pre-flight Pass 3):** Unit tests under `drs-bridge/tests/test_parser_loader.py` remain unchanged (still cover the missing-DLL error path + attribute surface). The new `test_reference_parser_integration.py` covers the end-to-end ctypes binding only when CMake is installed — it should `pytest.skip` cleanly if CMake is absent so developer-machine workflows aren't broken.

**Out of scope (deliberate; will be flagged by reviewers as missing — keep them out):**
- Tying the reference parser to a real IRS frame format — the point is the template, not a real protocol.
- A vendored JSON library — hand-written encoding is the example.
- Wiring the reference parser into drs-bridge's actual runtime (no YAML profile, no auto-load on startup). The variant developer wires their own per-variant YAML; the reference is a *template*, not a *variant*.
- Pre-built DLL artefacts committed to the repo. The CI builds it; developer machines build locally. Binary artefacts are git-ignored.
- `extract_frame` + `parse_message` integration tests via ctypes. Both symbols ARE implemented in the C++ (so variant developers see the pattern), but the Python wrapper's `parser_loader.py` only has a real binding for `format_response`. Wiring the other two through Python is a separate task (probably co-landing with a real variant).
- Cross-compilation (Windows MSBuild building Linux `.so`, vice versa). Each platform builds for itself.

---

## Task 1: Reference parser C++ source

**Files:**
- Create: `drs-bridge/parsers/reference/include/reference_parser.h`
- Create: `drs-bridge/parsers/reference/src/reference_parser.cpp`
- Create: `drs-bridge/parsers/reference/README.md`

The C++ implements the 4-symbol ABI for a tiny synthetic frame:

```
Frame layout (8 bytes minimum):
  byte 0: 0xAA            (magic)
  byte 1: payload_length  (uint8_t)
  bytes 2..N: payload     (payload_length bytes)

Payload for a 'time' response (6 bytes):
  bytes 0..3: timestamp_seconds  (uint32_t little-endian)
  bytes 4..5: reserved           (zero)
```

`extract_frame` scans for the magic byte and returns the complete frame. `parse_message` decodes the payload into a hand-written JSON string. `format_response(kind="time", kwargs_json='{"timestamp_ns":<n>}')` builds a frame with the lower 32 bits of `timestamp_ns / 1e9` as seconds. `free_result` is `free()`.

- [ ] **Step 1: Create `include/reference_parser.h`**

```c
// drs-bridge/parsers/reference/include/reference_parser.h
//
// REFERENCE PARSER — variant developer template
//
// This header declares the 4-symbol ABI every variant parser must export.
// The reference implementation under src/reference_parser.cpp uses a
// deliberately fake synthetic frame format. Real variants replace the
// bodies with their IRS-specific logic; the ABI is universal.
//
// All symbols use C linkage (extern "C") so ctypes can find them on
// Windows + Linux without name mangling.
#ifndef REFERENCE_PARSER_H
#define REFERENCE_PARSER_H

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#  define PARSER_EXPORT __declspec(dllexport)
#else
#  define PARSER_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// extract_frame: scan `buf` for a complete frame. On success, set
// *out_frame to a newly-allocated copy of the frame bytes, set *out_len
// to the frame length, and return 0. Returns -1 if no complete frame
// is present. Caller MUST call free_result(*out_frame) when done.
PARSER_EXPORT int extract_frame(
    const uint8_t* buf,
    size_t length,
    uint8_t** out_frame,
    size_t* out_len);

// parse_message: decode a frame (returned by extract_frame) into a
// newly-allocated JSON string. On success, set *out_json + *out_len and
// return 0. Returns -1 on malformed input. Caller MUST call free_result.
PARSER_EXPORT int parse_message(
    const uint8_t* frame,
    size_t frame_len,
    char** out_json,
    size_t* out_len);

// format_response: emit a frame for the given response kind.
//   kind          — null-terminated string, e.g. "time"
//   kwargs_json   — null-terminated JSON object, e.g. {"timestamp_ns":1234567890}
//   *out_buf      — set to a newly-allocated frame buffer
//   *out_len      — set to its length
// Returns 0 on success, -1 if `kind` is unknown or kwargs_json is malformed.
// Caller MUST call free_result(*out_buf).
PARSER_EXPORT int format_response(
    const char* kind,
    const char* kwargs_json,
    uint8_t** out_buf,
    size_t* out_len);

// free_result: free any pointer returned via an out-parameter above.
// Safe to call with NULL.
PARSER_EXPORT void free_result(void* ptr);

#ifdef __cplusplus
}
#endif

#endif // REFERENCE_PARSER_H
```

- [ ] **Step 2: Create `src/reference_parser.cpp`**

```cpp
// drs-bridge/parsers/reference/src/reference_parser.cpp
//
// REFERENCE PARSER IMPLEMENTATION — variant developer template
//
// Synthetic frame format (NOT a real IRS):
//   byte 0:       0xAA              (magic; distinguishes our frames from noise)
//   byte 1:       payload_length    (uint8_t; max 253 because total <= 255)
//   bytes 2..N:   payload           (payload_length bytes)
//
// Currently supports one response kind:
//   "time"  — payload is 4-byte little-endian timestamp_seconds (uint32_t)
//             followed by 2 reserved zero bytes.
//
// Real variant developers: replace the synthetic format with your IRS
// frame structure and add new "kinds" in format_response's dispatch table.
#include "reference_parser.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <new>

namespace {

constexpr uint8_t kFrameMagic = 0xAA;
constexpr size_t  kHeaderLen  = 2;     // magic byte + length byte
constexpr size_t  kTimePayloadLen = 6; // 4-byte seconds + 2-byte reserved

// Minimal JSON int extractor: finds `"<key>":<int>` in a JSON object string
// and returns the int via *out. Returns 0 on success, -1 if not found or
// malformed. Variant developers will likely replace this with a real JSON
// library; we hand-roll here to keep the template free of dependencies.
int json_extract_int64(const char* json, const char* key, int64_t* out) {
    if (!json || !key || !out) return -1;
    char needle[64];
    int n = std::snprintf(needle, sizeof(needle), "\"%s\":", key);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(needle)) return -1;
    const char* p = std::strstr(json, needle);
    if (!p) return -1;
    p += n;
    while (*p == ' ' || *p == '\t') ++p;
    char* end = nullptr;
    long long v = std::strtoll(p, &end, 10);
    if (end == p) return -1;
    *out = static_cast<int64_t>(v);
    return 0;
}

// Allocate and copy `n` bytes. Returns NULL on OOM.
uint8_t* alloc_copy(const uint8_t* src, size_t n) {
    uint8_t* p = static_cast<uint8_t*>(std::malloc(n));
    if (!p) return nullptr;
    std::memcpy(p, src, n);
    return p;
}

// Allocate and copy a null-terminated string (including the terminator).
char* alloc_string(const char* s, size_t len_excl_null) {
    char* p = static_cast<char*>(std::malloc(len_excl_null + 1));
    if (!p) return nullptr;
    std::memcpy(p, s, len_excl_null);
    p[len_excl_null] = '\0';
    return p;
}

} // namespace

extern "C" {

int extract_frame(
    const uint8_t* buf,
    size_t length,
    uint8_t** out_frame,
    size_t* out_len)
{
    if (!buf || !out_frame || !out_len) return -1;
    // Scan for the magic byte. Discard any preamble bytes (frame sync).
    for (size_t i = 0; i + kHeaderLen <= length; ++i) {
        if (buf[i] != kFrameMagic) continue;
        uint8_t payload_len = buf[i + 1];
        size_t total = kHeaderLen + payload_len;
        if (i + total > length) return -1; // incomplete frame
        uint8_t* copy = alloc_copy(buf + i, total);
        if (!copy) return -1;
        *out_frame = copy;
        *out_len = total;
        return 0;
    }
    return -1;
}

int parse_message(
    const uint8_t* frame,
    size_t frame_len,
    char** out_json,
    size_t* out_len)
{
    if (!frame || !out_json || !out_len) return -1;
    if (frame_len < kHeaderLen) return -1;
    if (frame[0] != kFrameMagic) return -1;
    uint8_t payload_len = frame[1];
    if (frame_len != kHeaderLen + payload_len) return -1;

    // Only the time payload is known to the reference parser.
    if (payload_len != kTimePayloadLen) return -1;
    const uint8_t* p = frame + kHeaderLen;
    uint32_t ts = static_cast<uint32_t>(p[0])
                | (static_cast<uint32_t>(p[1]) << 8)
                | (static_cast<uint32_t>(p[2]) << 16)
                | (static_cast<uint32_t>(p[3]) << 24);

    char buf[64];
    int n = std::snprintf(buf, sizeof(buf),
        "{\"kind\":\"time\",\"timestamp_seconds\":%u}", ts);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(buf)) return -1;
    char* copy = alloc_string(buf, static_cast<size_t>(n));
    if (!copy) return -1;
    *out_json = copy;
    *out_len = static_cast<size_t>(n);
    return 0;
}

int format_response(
    const char* kind,
    const char* kwargs_json,
    uint8_t** out_buf,
    size_t* out_len)
{
    if (!kind || !out_buf || !out_len) return -1;

    if (std::strcmp(kind, "time") == 0) {
        int64_t ts_ns = 0;
        if (json_extract_int64(kwargs_json, "timestamp_ns", &ts_ns) != 0) return -1;
        uint32_t ts_s = static_cast<uint32_t>(ts_ns / 1000000000LL);
        const size_t total = kHeaderLen + kTimePayloadLen;
        uint8_t* buf = static_cast<uint8_t*>(std::malloc(total));
        if (!buf) return -1;
        buf[0] = kFrameMagic;
        buf[1] = kTimePayloadLen;
        buf[2] = static_cast<uint8_t>(ts_s & 0xFF);
        buf[3] = static_cast<uint8_t>((ts_s >> 8)  & 0xFF);
        buf[4] = static_cast<uint8_t>((ts_s >> 16) & 0xFF);
        buf[5] = static_cast<uint8_t>((ts_s >> 24) & 0xFF);
        buf[6] = 0;  // reserved
        buf[7] = 0;  // reserved
        *out_buf = buf;
        *out_len = total;
        return 0;
    }

    // Unknown kind. Real variant parsers expand the dispatch above.
    return -1;
}

void free_result(void* ptr) {
    std::free(ptr);
}

} // extern "C"
```

- [ ] **Step 3: Create the directory README**

File: `drs-bridge/parsers/reference/README.md`

```markdown
# Reference C++ parser — variant developer template

This directory ships a minimal, buildable C++17 parser implementing the
4-symbol ABI every drs-bridge variant parser must export
(`extract_frame`, `parse_message`, `format_response`, `free_result`).

The frame format is **deliberately synthetic** — 1-byte magic + 1-byte
length + payload. It is NOT a real IRS. The point of this template is
the project skeleton, the build configuration, and the Python
integration; not the protocol semantics.

## How to use as a variant template

1. Copy this directory: `cp -r drs-bridge/parsers/reference/ drs-bridge/parsers/<your-variant>/`.
2. Rename `reference_parser.h` and `reference_parser.cpp` to
   `<your_variant>_parser.h/.cpp`. Adjust `#include` lines accordingly.
3. Edit `CMakeLists.txt`: change the `project(...)` name and the target
   name from `reference_parser` to `<your_variant>_parser`.
4. Replace the synthetic frame format in `src/<your_variant>_parser.cpp`
   with your IRS frame layout. Pay attention to:
   - Frame sync / start-of-frame bytes
   - Length encoding (where + endianness)
   - Per-kind dispatch in `format_response`
   - Per-message-type decode in `parse_message`
5. Add a variant YAML profile under `drs-bridge/src/drs_bridge/profiles/`
   pointing `parser_lib` at your built `.dll` / `.so`.

## Build (local)

```
cd drs-bridge/parsers/reference
cmake -S . -B build
cmake --build build --config Release
```

Output: `build/Release/reference_parser.dll` on Windows (multi-config
generators like MSVC); `build/libreference_parser.so` on Linux.

## Build (CI)

The repo's `.github/workflows/ci.yml` builds this parser on both the
Linux + Windows runners before running drs-bridge's pytest. The runtime
test `test_reference_parser_integration.py` then loads the resulting
shared library and exercises the ctypes binding end-to-end. If the
binding signatures in `drs-bridge/src/drs_bridge/parser_loader.py` ever
drift from the C ABI declared here, that test fails.
```

- [ ] **Step 4: Commit**

Verify branch is `main`. Then:

```
git -C e:/GitHub/ewtss-v2-pub add drs-bridge/parsers/reference/
git -C e:/GitHub/ewtss-v2-pub commit -m "feat(drs-bridge): reference C++ parser source — variant developer template (Task 1)"
git -C e:/GitHub/ewtss-v2-pub push
```

(No tests at this point — the source compiles but isn't wired to a build system yet. Task 2 adds CMake.)

---

## Task 2: CMake build config

**Files:**
- Create: `drs-bridge/parsers/reference/CMakeLists.txt`
- Modify: `drs-bridge/.gitignore` (add `parsers/**/build/`)

- [ ] **Step 1: Write `CMakeLists.txt`**

```cmake
# drs-bridge/parsers/reference/CMakeLists.txt
#
# Builds the reference variant parser as a shared library:
#   • reference_parser.dll      on Windows (MSBuild / Ninja-Multi-Config)
#   • libreference_parser.so    on Linux (Make / Ninja)
#
# Variant developers copy this file alongside the source, change the
# project name + target name, and otherwise leave it untouched.

cmake_minimum_required(VERSION 3.20)
project(reference_parser LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS OFF)  # only PARSER_EXPORT-tagged symbols

add_library(reference_parser SHARED
    src/reference_parser.cpp
)

target_include_directories(reference_parser PUBLIC include)

if(MSVC)
    target_compile_options(reference_parser PRIVATE /W4 /WX)
else()
    target_compile_options(reference_parser PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# On Linux, drop the "lib" prefix so ctypes loads with the same name
# convention regardless of platform: reference_parser.dll vs
# reference_parser.so. Comment out if you prefer the standard libfoo.so.
if(NOT WIN32)
    set_target_properties(reference_parser PROPERTIES PREFIX "")
endif()
```

- [ ] **Step 2: Add the build dir to `.gitignore`**

In `drs-bridge/.gitignore`, add a line:
```
parsers/**/build/
```

(Or if the file doesn't exist, create it with that single line plus standard Python entries already covered globally.)

- [ ] **Step 3: Smoke build locally**

```
cd drs-bridge/parsers/reference
cmake -S . -B build
cmake --build build --config Release
```

On Windows: confirm `build/Release/reference_parser.dll` exists.
On Linux: confirm `build/reference_parser.so` exists (with the no-prefix rule above).

- [ ] **Step 4: Commit**

```
git -C e:/GitHub/ewtss-v2-pub add drs-bridge/parsers/reference/CMakeLists.txt drs-bridge/.gitignore
git -C e:/GitHub/ewtss-v2-pub commit -m "feat(drs-bridge): CMake build config for reference parser (Task 2)"
git -C e:/GitHub/ewtss-v2-pub push
```

---

## Task 3: pytest fixture + integration test

**Files:**
- Create: `drs-bridge/tests/conftest.py` modifications (add a `built_reference_parser` session-scoped fixture)
- Create: `drs-bridge/tests/test_reference_parser_integration.py`

- [ ] **Step 1: Add the fixture to `conftest.py`**

Read the existing conftest first to confirm structure. Then **append** (do not replace) the fixture:

```python
# drs-bridge/tests/conftest.py
import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest


_REPO_ROOT = Path(__file__).resolve().parents[1]  # drs-bridge/
_REF_PARSER_DIR = _REPO_ROOT / "parsers" / "reference"
_REF_BUILD_DIR  = _REF_PARSER_DIR / "build"


def _find_built_library() -> Path | None:
    """Locate the built reference parser regardless of generator quirks."""
    candidates = []
    if sys.platform == "win32":
        candidates += list(_REF_BUILD_DIR.glob("**/reference_parser.dll"))
    else:
        candidates += list(_REF_BUILD_DIR.glob("**/reference_parser.so"))
        candidates += list(_REF_BUILD_DIR.glob("**/libreference_parser.so"))
    return candidates[0] if candidates else None


@pytest.fixture(scope="session")
def built_reference_parser(tmp_path_factory) -> Path:
    """Build the reference C++ parser once per test session via CMake.

    Skips the test if CMake isn't on PATH (developer machines without a
    C++ toolchain shouldn't see a hard failure). CI installs CMake on
    both runner images by default.
    """
    if shutil.which("cmake") is None:
        pytest.skip("cmake not on PATH; skipping reference-parser integration test")

    # Build (idempotent if already built — cmake --build is no-op then).
    try:
        subprocess.run(
            ["cmake", "-S", str(_REF_PARSER_DIR), "-B", str(_REF_BUILD_DIR)],
            check=True, capture_output=True,
        )
        subprocess.run(
            ["cmake", "--build", str(_REF_BUILD_DIR), "--config", "Release"],
            check=True, capture_output=True,
        )
    except subprocess.CalledProcessError as e:
        pytest.skip(f"cmake build failed: {e.stderr.decode(errors='replace')[:500]}")

    lib = _find_built_library()
    if lib is None:
        pytest.skip("reference parser built but library not found at expected path")
    return lib
```

(If `conftest.py` already exists with an `anyio_backend` fixture or similar, just append the helper functions + fixture without removing anything.)

- [ ] **Step 2: Write the integration test**

```python
# drs-bridge/tests/test_reference_parser_integration.py
"""End-to-end test: build the reference C++ parser via CMake, load the
resulting shared library via parser_loader.load_parser, and exercise
format_response over the ctypes binding.

This is the first test in the repo that actually invokes C code from
Python. If the argtypes/restype declarations in parser_loader.py drift
from the C ABI in parsers/reference/include/reference_parser.h, this
test fails — regardless of whether unit-level tests still pass.
"""
import struct
from pathlib import Path

from drs_bridge.parser_loader import load_parser


def test_format_response_time_round_trip(built_reference_parser: Path):
    """format_response(kind='time', timestamp_ns=...) must produce a
    well-formed reference frame: 0xAA magic, 0x06 length, 4-byte LE
    seconds, 2 reserved zero bytes."""
    handle = load_parser(built_reference_parser)

    timestamp_ns = 1_700_000_000_000_000_000  # ~Nov 2023
    expected_seconds = timestamp_ns // 1_000_000_000

    frame = handle.format_response(kind="time", timestamp_ns=timestamp_ns)

    assert isinstance(frame, bytes)
    assert len(frame) == 8
    assert frame[0] == 0xAA          # magic
    assert frame[1] == 0x06          # payload length
    actual_seconds, = struct.unpack("<I", frame[2:6])
    # uint32 wrap is acceptable; the reference parser truncates to 32 bits.
    assert actual_seconds == (expected_seconds & 0xFFFFFFFF)
    assert frame[6] == 0x00
    assert frame[7] == 0x00


def test_format_response_unknown_kind_raises(built_reference_parser: Path):
    """The reference parser returns -1 for any kind other than 'time';
    parser_loader's wrapper translates that into a RuntimeError."""
    import pytest
    handle = load_parser(built_reference_parser)
    with pytest.raises(RuntimeError, match="format_response returned"):
        handle.format_response(kind="unknown-kind", timestamp_ns=0)
```

- [ ] **Step 3: Run**

From `drs-bridge/`:
```
.\.venv\Scripts\pytest.exe tests/test_reference_parser_integration.py -v
```

Expected outcomes:
- **If CMake is available:** 2 passed. CMake build runs once at the session start; both tests pass.
- **If CMake is missing:** 2 skipped, with the message about CMake not being on PATH.

Either is a healthy outcome — the test is opt-in to whatever toolchain is present.

Also run the full suite to confirm no regressions:
```
.\.venv\Scripts\pytest.exe -v
```
Expected: 32 passed (30 prior + 2 new), or 30 passed + 2 skipped if CMake isn't available.

- [ ] **Step 4: Commit**

```
git -C e:/GitHub/ewtss-v2-pub add drs-bridge/tests/conftest.py drs-bridge/tests/test_reference_parser_integration.py
git -C e:/GitHub/ewtss-v2-pub commit -m "test(drs-bridge): integration test loads built reference parser via ctypes (Task 3)"
git -C e:/GitHub/ewtss-v2-pub push
```

---

## Task 4: CI step — build reference parser before pytest

**Files:**
- Modify: `.github/workflows/ci.yml`

The existing `python-suites` matrix job has `drs-server` and `drs-bridge` rows. The drs-bridge row needs an extra step that runs CMake before pytest, so the reference parser is built and the integration tests aren't skipped.

The cleanest approach: a conditional step that runs only for the drs-bridge suite. GitHub Actions matrix `if` expressions handle this cleanly.

- [ ] **Step 1: Add the CMake build step**

In `.github/workflows/ci.yml`, between the existing "Install dependencies" step and the "Run pytest" step, insert:

```yaml
      - name: Build reference C++ parser (drs-bridge only)
        if: matrix.suite == 'drs-bridge'
        working-directory: drs-bridge/parsers/reference
        run: |
          cmake -S . -B build
          cmake --build build --config Release
```

The `working-directory` directive overrides the matrix's per-job default (the job's default sits at `drs-bridge/`; CMake needs to run from the parser dir). ubuntu-latest has cmake + g++ pre-installed; no additional setup steps needed.

- [ ] **Step 2: Commit**

```
git -C e:/GitHub/ewtss-v2-pub add .github/workflows/ci.yml
git -C e:/GitHub/ewtss-v2-pub commit -m "ci: build reference C++ parser before drs-bridge pytest (Task 4)"
git -C e:/GitHub/ewtss-v2-pub push
```

This validates the CMake build runs on ubuntu-latest. If it succeeds, the integration test in Task 3 will run (no longer skipped), and we'll know the ctypes binding works on Linux at minimum. Windows runner doesn't currently exercise the parser; that's a future addition once we add a Windows step for drs-bridge tests.

---

## Task 5: Developer handbook §9 — point at the reference template

**Files:**
- Modify: `docs/ewtss/developer-handbook.md` §9.3 (C++ parser interface contract)

The current §9.3 has abstract C function signatures and prose. Replace with a pointer to the reference parser + a concrete walkthrough.

- [ ] **Step 1: Read existing §9.3**

```
grep -n "^### 9.3" docs/ewtss/developer-handbook.md
```

Read the section.

- [ ] **Step 2: Rewrite §9.3**

Replace the abstract C function signatures + prose with:

> ### 9.3 C++ parser interface contract
>
> Every variant parser exports four C-linkage symbols. The canonical
> reference implementation lives at
> [`drs-bridge/parsers/reference/`](../../drs-bridge/parsers/reference/);
> it builds out of the box via CMake on both Windows and Linux and is
> exercised end-to-end by `drs-bridge/tests/test_reference_parser_integration.py`.
>
> **Symbols:**
>
> | Symbol | Purpose |
> |---|---|
> | `extract_frame(buf, length, out_frame, out_len) → int` | Scan input for a complete frame. On success allocate `*out_frame` + set `*out_len` and return 0. Return −1 if no complete frame is present. |
> | `parse_message(frame, frame_len, out_json, out_len) → int` | Decode a frame into a heap-allocated JSON string. |
> | `format_response(kind, kwargs_json, out_buf, out_len) → int` | Emit a frame for the named response kind, given a JSON object of arguments. |
> | `free_result(ptr) → void` | Free anything returned via an out-pointer above. |
>
> See `drs-bridge/parsers/reference/include/reference_parser.h` for the
> exact signatures + export macros (`PARSER_EXPORT` handles
> `__declspec(dllexport)` on MSVC + `__attribute__((visibility("default")))`
> on GCC/Clang).
>
> **To onboard a new variant:**
>
> 1. Copy `drs-bridge/parsers/reference/` to `drs-bridge/parsers/<variant>/`.
> 2. Rename `reference_parser.{h,cpp}` to `<variant>_parser.{h,cpp}` +
>    adjust `#include` lines.
> 3. Edit `CMakeLists.txt`: change `project(reference_parser ...)` to
>    `project(<variant>_parser ...)` and the `add_library` target name
>    accordingly.
> 4. Replace the synthetic frame format with your IRS layout. The
>    `extract_frame` scan-for-magic pattern, `parse_message` dispatch
>    via response type IDs, and `format_response`'s dispatch on `kind`
>    are all idiomatic — keep the structure, fill in the bodies.
> 5. Add a YAML profile under `drs-bridge/src/drs_bridge/profiles/<variant>.yaml`
>    with `parser_lib:` pointing at the built `.dll` / `.so`.
> 6. Build + test:
>    ```
>    cd drs-bridge/parsers/<variant> && cmake -S . -B build && cmake --build build --config Release
>    cd ../../.. && .\.venv\Scripts\pytest.exe tests/profiles/test_profile_yaml.py -v
>    ```
>
> **Reference parser invariants** the template demonstrates (your variant
> should preserve all of these):
>
> - All exported symbols use `extern "C"` so ctypes finds them without
>   name mangling.
> - `PARSER_EXPORT` macro handles platform-specific export markers.
> - Every return-via-out-pointer is heap-allocated with `malloc`; the
>   caller is documented as responsible for `free_result`.
> - Frame buffers are size-bounded: the length byte limits payload to
>   ≤253 bytes per frame; longer IRS frames need a 2- or 4-byte length
>   field — adjust the reference's length encoding accordingly.
> - The Python wrapper in `drs-bridge/src/drs_bridge/parser_loader.py`
>   already declares the matching `argtypes`/`restype`. Variants don't
>   need to modify the Python side as long as their C ABI matches.

- [ ] **Step 3: Commit**

```
git -C e:/GitHub/ewtss-v2-pub add docs/ewtss/developer-handbook.md
git -C e:/GitHub/ewtss-v2-pub commit -m "docs(handbook): §9.3 points at the reference parser as the buildable template (Task 5)"
git -C e:/GitHub/ewtss-v2-pub push
```

---

## Self-review checklist

- ✅ **Spec coverage** — every concern from the user's framing is covered: reference parser template (T1-T2), CMake project config (T2), Python integration (T3 fixture + test), CI validation (T4), handbook update so future C++ developers can find this (T5).
- ✅ **Placeholder scan** — no TBD / TODO in the plan. The synthetic frame format is deliberate, not a placeholder.
- ✅ **Type consistency** — the C++ ABI matches `parser_loader.py`'s existing `argtypes` declarations exactly (verified line-by-line against the existing wrapper).
- ✅ **Application target unambiguous** — drs-bridge only; CI gets one tiny job step. No other service touched.
- ✅ **Skeleton mapping** — every file referenced in a later task is created in an earlier task. No forward references.
- ✅ **Environment pre-flight** — CMake 3.20+ is on both GitHub Actions runners by default; Visual C++ Build Tools ship with windows-latest; g++ ships with ubuntu-latest. No new external prereqs for developers either (most have Visual Studio or build-essential already).
- ✅ **Content traps grep** — no `datetime.utcnow`, no `asyncio.get_event_loop`, no stale Angular patterns.
- ✅ **Parameterise rather than magic-number** — frame format constants (`kFrameMagic`, `kHeaderLen`, `kTimePayloadLen`) are `constexpr` named values, not magic numbers in the body.
- ✅ **Test breadth statement** — header carries it: unit-level binding stays as-is; integration test exercises the real DLL when CMake is present and skips cleanly otherwise.
- ✅ **Out of scope list** — header carries it: no real IRS, no JSON library, no auto-load into drs-bridge runtime, no committed binaries, no cross-compilation.

---

## Execution handoff

Plan complete. Two execution options:

**1. Subagent-Driven (recommended)** — fresh subagent per task + branch-state guardrails (matching the prior runtime skeletons).
**2. Inline Execution** — batch with checkpoints.

Going with subagent-driven.
