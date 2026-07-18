# XML Content-Parsing Pugixml Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the hand-rolled `xml_attr`/`xml_text`/`xml_has`-style byte
scanning in `ca120_parser.cpp`, `ddf550_parser.cpp`, and `ddf1gtx_parser.cpp`
with pugixml-based DOM navigation, while keeping each parser's existing
`parse_message` JSON output byte-for-byte identical.

**Architecture:** Each of the three files gets `#include "pugixml.hpp"`
added, its `parse_xml*` content-parsing function rewritten to parse the
frame with `pugi::xml_document::load_buffer` and pull the exact same JSON
fields via `pugi::xml_node` attribute/child/text navigation instead of
string search. `extract_frame`'s frame-boundary detection (`xml_closing_end`)
and `format_response` are untouched in all three files. Each parser is an
independent CMake project with no shared build target, so pugixml's single
source file is added directly to each variant's library and test targets
(three separate compiles of `pugixml.cpp`, not one shared static lib).

**Tech Stack:** C++17, pugixml 1.16 (MIT/zlib, already vendored at
`drs-bridge/parsers/utils/pugixml-1.16/`), CMake 3.20+, existing
plain-assert test executables (no new test framework).

**Spec:** [xml-parsing-pugixml-migration-design.md](../specs/xml-parsing-pugixml-migration-design.md)
— read this first for the full rationale; this plan implements it task by
task.

**Scope note:** this plan modifies exactly three files' content-parsing
logic — `drs-bridge/parsers/ca120/src/ca120_parser.cpp`,
`drs-bridge/parsers/ddf550/src/ddf550_parser.cpp`,
`drs-bridge/parsers/ddf1gtx/src/ddf1gtx_parser.cpp` — plus their
CMakeLists.txt, one new shared header (`drs-bridge/parsers/utils/pugixml_helpers.h`,
Task 0), and one shared ABI header comment (Task 4).
`drs-bridge/parsers/utils/xml_to_json/` and
`drs-bridge/parsers/ca120/src/CA120_parser_new.cpp` are reference/legacy
validation artifacts and are explicitly **not** touched by any code task
(Task 4 only adds a clarifying comment to the latter). All shell commands
below assume the current working directory is the repo root
(`ewtss-v2-design-main/`).

**Things this plan deliberately does NOT do** (say so up front so a
reviewer doesn't flag these as gaps):
- Does not change any JSON field name, type, or presence condition in any
  of the three parsers' output — see each task's Interfaces block.
- Does not touch `format_response` (already library-free, see spec §5) or
  `extract_frame`'s frame-boundary detection (see spec §4).
- Does not adopt the generic XML→JSON mirror shape from `xml_to_json/` /
  `CA120_parser_new.cpp` — explicitly rejected for this work, see spec §1.
- Does not add a shared CMake `pugixml` static-library target — `pugixml.cpp`
  is compiled once per variant target (Option A, spec §6), a deliberate
  simplicity-over-build-time tradeoff given no top-level aggregating
  CMakeLists exists today.
- Does not fix the pre-existing unbounded-`strstr` risk in DDF-1GTX's old
  Command-name lookup beyond what naturally falls out of switching to
  pugixml (Task 3 notes this as an incidental fix, not a goal).

## Global Constraints

- C++17 (already the project standard in every touched CMakeLists.txt).
- pugixml (MIT/zlib) is the one approved exception to `sdfc_abi.h` rule 3
  ("No LGPL-licensed code is linked into the DLL") — see Task 4. No other
  third-party dependency is introduced.
- Every existing test in `test_frames_ca120.cpp`, `test_frames_ddf550.cpp`,
  `test_frames_ddf1gtx.cpp` must continue to pass, byte-identical, after
  each task — these are the parity check for this refactor, not new tests
  to write.
- `extract_frame` (frame-boundary detection, `xml_closing_end`) and
  `format_response` (encode direction) are **not modified** in any of the
  three files.
- CMake: no shared `pugixml` static-library target — add
  `../utils/pugixml-1.16/pugixml.cpp` directly to each variant's
  `add_library`/`add_executable` source lists (Option A from the spec's
  §6), plus `target_include_directories(... PRIVATE ../utils/pugixml-1.16)`
  on both the DLL and its test executable target.
- The non-MSVC static-linking flags already present in `ddf550/CMakeLists.txt`
  (`-static-libgcc -static-libstdc++ -static`, needed so `ctypes.CDLL()` can
  load the DLL without `libstdc++-6.dll` on `PATH`) must remain untouched.

---

### Task 0: Shared pugixml navigation helper

**Files:**
- Create: `drs-bridge/parsers/utils/pugixml_helpers.h`

**Interfaces:**
- Consumes: `pugi::xml_node` (from `pugixml.hpp`, already vendored at `drs-bridge/parsers/utils/pugixml-1.16/`).
- Produces: `find_first(pugi::xml_node root, const char* tag) -> pugi::xml_node`
  — used identically by Tasks 1, 2, and 3. This is the only helper factored
  out here: DDF-1GTX's `param_value`/`tag_or_param` (Task 3) have exactly
  one caller each and are not duplicated anywhere else, so they stay local
  to `ddf1gtx_parser.cpp` rather than moving here.

- [ ] **Step 1: Create the header**

```cpp
#pragma once

#include <string>

#include "pugixml.hpp"

// Returns the first descendant-or-self element named `tag`, in document
// order, or a null node if none exists. A null node's .text().get() and
// .attribute(...).value() both safely return "" (pugixml guarantee), so
// callers don't need to check for null before reading. Deliberately
// unscoped (searches the whole subtree, not just direct children) --
// several ICDs nest the tag of interest two levels deep (e.g. CA120's
// DataStream under Control under Request).
inline pugi::xml_node find_first(pugi::xml_node root, const char* tag) {
    std::string xpath = std::string(".//") + tag;
    return root.select_node(xpath.c_str()).node();
}
```

`inline` (not `static`) because this header is included by three separate
translation units across three separate DLLs — `static` would give each
its own internal-linkage copy anyway (harmless but redundant), `inline` is
the conventional choice for a header-only free function.

- [ ] **Step 2: Verify it compiles standalone**

```bash
g++ -std=c++17 -I drs-bridge/parsers/utils/pugixml-1.16 -I drs-bridge/parsers/utils -fsyntax-only -x c++ -c drs-bridge/parsers/utils/pugixml_helpers.h
```
Expected: no output, exit code 0 (syntax-only check — there's no `.cpp`
consumer yet, this just confirms the header parses and its `#include`
resolves).

- [ ] **Step 3: Commit**

```bash
git add drs-bridge/parsers/utils/pugixml_helpers.h
git commit -m "feat(utils): add shared pugixml find_first helper for the parser migration"
```

---

### Task 1: CA120 — pugixml migration

**Files:**
- Modify: `drs-bridge/parsers/ca120/CMakeLists.txt`
- Modify: `drs-bridge/parsers/ca120/src/ca120_parser.cpp:1-200` (helpers section) and `:671-758` (`parse_xml`)
- Test: `drs-bridge/parsers/ca120/tests/test_frames_ca120.cpp` (unchanged — used as the parity check)

**Interfaces:**
- Consumes: `drs-bridge/parsers/utils/pugixml-1.16/pugixml.hpp` / `.cpp` (already vendored, MIT/zlib); `find_first(pugi::xml_node, const char*)` from `drs-bridge/parsers/utils/pugixml_helpers.h` (Task 0 — must be complete before this task starts).
- Produces: no new interface — `parse_message`'s JSON output (`hw`, `channel`, `msg_kind`, `msg_type`, `msg_id`, `event_source`, `subsystems`, `datastream_action`, `datastream_type`, `stream_ip`, `stream_port`, `app_guid`, `status`, `frequency_hz`, `bandwidth_hz`, `raw_xml`) is unchanged.

- [ ] **Step 1: Confirm the baseline (before touching any code)**

Run (from repo root):
```bash
cmake -S drs-bridge/parsers/ca120 -B drs-bridge/parsers/ca120/build
cmake --build drs-bridge/parsers/ca120/build
ctest --test-dir drs-bridge/parsers/ca120/build --output-on-failure
```
Expected: the `ca120_frames` test passes. This is the baseline the pugixml rewrite must match exactly.

- [ ] **Step 2: Add pugixml to the CMake build**

Replace `drs-bridge/parsers/ca120/CMakeLists.txt` in full:

```cmake
cmake_minimum_required(VERSION 3.20)
project(ca120_parser CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

# ---- CA120 parser DLL ----
add_library(ca120 SHARED
    src/ca120_parser.cpp
    ../utils/pugixml-1.16/pugixml.cpp
)
target_include_directories(ca120 PRIVATE ../dp_ecm/include ../utils/pugixml-1.16 ../utils)

if(MSVC)
    target_compile_options(ca120 PRIVATE /W4 /permissive-)
    set_target_properties(ca120 PROPERTIES LINK_FLAGS
        "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/ca120.def")
else()
    target_compile_options(ca120 PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---- Unit tests (no external framework; plain asserts) ----
enable_testing()

add_executable(test_ca120
    tests/test_frames_ca120.cpp
    src/ca120_parser.cpp
    ../utils/pugixml-1.16/pugixml.cpp
)
target_include_directories(test_ca120 PRIVATE ../dp_ecm/include ../utils/pugixml-1.16 ../utils)
if(NOT MSVC)
    target_compile_options(test_ca120 PRIVATE -Wall -Wextra)
endif()
add_test(NAME ca120_frames COMMAND test_ca120)
```

- [ ] **Step 3: Replace the helpers section and `parse_xml` with pugixml**

In `drs-bridge/parsers/ca120/src/ca120_parser.cpp`, add the includes near
the top (after the existing `#include "json_writer.h"` at line 17):

```cpp
#include "json_writer.h"
#include "pugixml.hpp"
#include "pugixml_helpers.h"
```

Delete the entire `Helpers: minimal XML scanning (no LGPL, no exceptions)`
block (lines 84–200: `xml_closing_end`, `xml_attr`, `xml_text`, `xml_has`,
`json_str_field`, `json_int_field`) **except** `xml_closing_end` (still used
by `extract_frame`), `json_str_field`, and `json_int_field` (still used by
`format_response`) — keep those three, delete only `xml_attr`, `xml_text`,
`xml_has`. Replace that section header comment (the three deleted helpers
are gone, `find_first` comes from the shared header included above, not a
local definition):

```cpp
// ---------------------------------------------------------------------------
// Content parsing now uses pugixml (find_first from pugixml_helpers.h) --
// content parsing only; xml_closing_end below is frame-boundary detection
// and still byte-scanning, see extract_frame.
// ---------------------------------------------------------------------------
```

Then replace the entire `parse_xml` function (originally lines 671–758)
with:

```cpp
// impl_parse_xml holds the real logic; parse_xml (below) wraps it in
// try/catch so no C++ exception ever escapes toward parse_message's
// extern "C" boundary, matching sdfc_abi.h rule 2. pugixml's load_buffer
// itself does not throw (it returns a result object), but the JsonWriter/
// std::string work here could theoretically raise std::bad_alloc, and
// select_node's XPath evaluation can theoretically raise
// pugi::xpath_exception if PUGIXML_NO_EXCEPTIONS is ever undefined and a
// malformed expression is ever constructed -- not reachable today since
// every tag name used here is a fixed literal, but the guard costs nothing
// and matches the same try/catch idiom already used in
// drs-bridge/parsers/utils/xml_to_json/xml_to_json.cpp.
static std::string impl_parse_xml(const uint8_t* frame, int frame_len, int frame_type) {
    pugi::xml_document doc;
    pugi::xml_parse_result presult =
        doc.load_buffer(frame, static_cast<size_t>(frame_len));
    // Stricter than the old string-scanning code, which had no notion of
    // well-formedness and would silently emit a sparse/partial JSON for
    // malformed input. extract_frame already rejects most malformed input
    // (it requires a matching close tag by name); this catches the
    // remaining corner case where a well-closed-looking frame is malformed
    // *inside*. Accepted, intentional behavior change -- see design spec §3.
    if (!presult) return {};

    pugi::xml_node root = doc.first_child();

    JsonWriter j;
    j.key_str("hw",       "ca120");
    j.key_str("channel",  "xml");

    bool is_event = (std::strcmp(root.name(), "Event") == 0);
    j.key_str("msg_kind", (frame_type == 1) ? "request"
                        : is_event          ? "event"
                                            : "reply");

    const char* type_attr = root.attribute("type").value();
    const char* id_attr   = root.attribute("id").value();
    const char* src_attr  = root.attribute("source").value();
    if (*type_attr) j.key_str("msg_type",     type_attr);
    if (*id_attr)   j.key_str("msg_id",       id_attr);
    if (*src_attr)  j.key_str("event_source", src_attr);

    static const struct { const char* tag; const char* name; } kSubsystems[] = {
        { "ResourceManager",       "resource_manager"       },
        { "Control",               "control"                },
        { "Tuner",                 "tuner"                  },
        { "tuner",                 "tuner"                  },
        { "FFT",                   "fft"                    },
        { "DetectAndClassify",     "detect_and_classify"    },
        { "FrequencyHopping",      "frequency_hopping"      },
        { "Squelch",               "squelch"                },
        { "Classifier",            "classifier"             },
        { "AnalogDemodulator",     "analog_demodulator"     },
        { "DigitalDemodulator",    "digital_demodulator"    },
        { "BitstreamProcessing",   "bitstream_processing"   },
        { "HopperFilterSeparation","hopper_filter_separation"},
    };
    std::string sub_json("[");
    bool first = true;
    for (auto& s : kSubsystems) {
        if (find_first(root, s.tag)) {
            if (!first) sub_json += ',';
            sub_json += '"';
            sub_json += s.name;
            sub_json += '"';
            first = false;
        }
    }
    sub_json += ']';
    j.key_raw("subsystems", sub_json);

    pugi::xml_node ds = find_first(root, "DataStream");
    if (ds) {
        const char* action = ds.attribute("action").value();
        const char* dtype  = ds.attribute("type").value();
        if (*action) j.key_str("datastream_action", action);
        if (*dtype)  j.key_str("datastream_type",   dtype);
        const char* ip   = ds.child("IP").text().get();
        const char* port = ds.child("Port").text().get();
        if (*ip)   j.key_str("stream_ip",   ip);
        if (*port) j.key_str("stream_port", port);
    }

    pugi::xml_node start_app = find_first(root, "StartApplication");
    if (start_app) {
        const char* guid = start_app.attribute("guid").value();
        if (*guid) j.key_str("app_guid", guid);
    }

    const char* status = find_first(root, "Status").text().get();
    const char* freq   = find_first(root, "Frequency").text().get();
    const char* bw     = find_first(root, "Bandwidth").text().get();
    if (*status) j.key_str("status",      status);
    if (*freq)   j.key_str("frequency_hz", freq);
    if (*bw)     j.key_str("bandwidth_hz", bw);

    static constexpr int RAW_XML_CAP = 16384;
    int raw_len = (frame_len < RAW_XML_CAP) ? frame_len : RAW_XML_CAP;
    j.key_str("raw_xml", std::string(reinterpret_cast<const char*>(frame), (size_t)raw_len));

    return j.str();
}

static std::string parse_xml(const uint8_t* frame, int frame_len, int frame_type) {
    try {
        return impl_parse_xml(frame, frame_len, frame_type);
    } catch (...) {
        return {};
    }
}
```

`parse_message`'s call site (`result = parse_xml(frame, iframe, ftype);`)
does not change — `parse_xml`'s public signature is identical to the
original, only its body is now a thin try/catch wrapper around
`impl_parse_xml`.

- [ ] **Step 4: Rebuild and confirm parity**

```bash
cmake --build drs-bridge/parsers/ca120/build
ctest --test-dir drs-bridge/parsers/ca120/build --output-on-failure
```
Expected: `ca120_frames` passes with the exact same assertions as the Step 1
baseline — no test file changes needed.

- [ ] **Step 5: Commit**

```bash
git add drs-bridge/parsers/ca120/CMakeLists.txt drs-bridge/parsers/ca120/src/ca120_parser.cpp
git commit -m "refactor(ca120): replace hand-rolled XML scanning with pugixml"
```

---

### Task 2: DDF-550 — pugixml migration

**Files:**
- Modify: `drs-bridge/parsers/ddf550/CMakeLists.txt`
- Modify: `drs-bridge/parsers/ddf550/src/ddf550_parser.cpp:118-239` (helpers) and `:523-625` (`parse_xml_ddf550`)
- Test: `drs-bridge/parsers/ddf550/tests/test_frames_ddf550.cpp` (unchanged — parity check)

**Interfaces:**
- Consumes: same pugixml headers as Task 1 (independent CMake project, own
  compile); `find_first(pugi::xml_node, const char*)` from
  `drs-bridge/parsers/utils/pugixml_helpers.h` (Task 0 — must be complete
  before this task starts).
- Produces: no new interface — `parse_message`'s JSON output (`hw`, `channel`,
  `msg_kind`, `msg_id`, `msg_type`, `command_name`, `command_value`,
  `params`, `ddf_cl_id`, `fields`, `units`) is unchanged.

- [ ] **Step 1: Confirm the baseline**

```bash
cmake -S drs-bridge/parsers/ddf550 -B drs-bridge/parsers/ddf550/build
cmake --build drs-bridge/parsers/ddf550/build
ctest --test-dir drs-bridge/parsers/ddf550/build --output-on-failure
```
Expected: `ddf550_frames` passes — this is the baseline to match.

- [ ] **Step 2: Add pugixml to the CMake build**

Replace `drs-bridge/parsers/ddf550/CMakeLists.txt` in full:

```cmake
cmake_minimum_required(VERSION 3.20)
project(ddf550_parser CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

# ---- DDF-550 parser DLL ----
add_library(ddf550 SHARED
    src/ddf550_parser.cpp
    ../utils/pugixml-1.16/pugixml.cpp
)
target_include_directories(ddf550 PRIVATE ../dp_ecm/include ../utils/pugixml-1.16 ../utils)

if(MSVC)
    target_compile_options(ddf550 PRIVATE /W4 /permissive-)
    set_target_properties(ddf550 PROPERTIES LINK_FLAGS
        "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/ddf550.def")
else()
    target_compile_options(ddf550 PRIVATE -Wall -Wextra -Wpedantic)
    # Static-link the MinGW runtime so ctypes.CDLL() can load this DLL
    # without libstdc++-6.dll/libgcc_s_seh-1.dll needing to be on PATH
    # (Python's ctypes uses the restricted DLL search path since 3.8+,
    # which does not consult PATH the way a normal EXE launch would).
    target_link_options(ddf550 PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

# ---- Unit tests (no external framework; plain asserts) ----
enable_testing()

add_executable(test_ddf550
    tests/test_frames_ddf550.cpp
    src/ddf550_parser.cpp
    ../utils/pugixml-1.16/pugixml.cpp
)
target_include_directories(test_ddf550 PRIVATE ../dp_ecm/include ../utils/pugixml-1.16 ../utils)
if(NOT MSVC)
    target_compile_options(test_ddf550 PRIVATE -Wall -Wextra)
endif()
add_test(NAME ddf550_frames COMMAND test_ddf550)
```

- [ ] **Step 3: Replace the helpers section and `parse_xml_ddf550` with pugixml**

Add the includes after `#include "json_writer.h"` (line 28):

```cpp
#include "json_writer.h"
#include "pugixml.hpp"
#include "pugixml_helpers.h"
```

In the `Helpers: XML scanning (no LGPL)` block (lines 118–222): keep
`xml_closing_end` (used by `extract_frame`) unchanged; delete `xml_attr`,
`xml_all_params`, `xml_all_dfdata_fields`. Keep `write_typed_param`
(lines 228–239) exactly as-is — it operates on already-extracted
`std::string` values and has no XML-scanning logic of its own. Replace the
section header comment (`find_first` comes from the shared header included
above, not a local definition):

```cpp
// ---------------------------------------------------------------------------
// Content parsing now uses pugixml (find_first from pugixml_helpers.h) --
// content parsing only; xml_closing_end above is frame-boundary detection
// and still byte-scanning, see extract_frame.
// ---------------------------------------------------------------------------
```

Replace the entire `parse_xml_ddf550` function (originally lines 523–625)
with:

```cpp
// impl_parse_xml_ddf550 holds the real logic; parse_xml_ddf550 (below)
// wraps it in try/catch so no C++ exception escapes toward parse_message's
// extern "C" boundary (sdfc_abi.h rule 2) -- same rationale and idiom as
// ca120_parser.cpp's impl_parse_xml/parse_xml split.
static std::string impl_parse_xml_ddf550(const uint8_t* frame, int frame_len,
                                          int frame_type)
{
    pugi::xml_document doc;
    pugi::xml_parse_result presult =
        doc.load_buffer(frame, static_cast<size_t>(frame_len));
    // See ca120_parser.cpp's parse_xml for why this new failure path is an
    // accepted, intentional behavior change (design spec §3).
    if (!presult) return {};

    pugi::xml_node root = doc.first_child();
    const char* root_name = root.name();

    bool is_ddfcl_req = (std::strcmp(root_name, "DDFCLRequest") == 0);
    bool is_ddfcl_rep = (std::strcmp(root_name, "DDFCLReply")   == 0);
    bool is_dfdata    = (std::strcmp(root_name, "DFData")       == 0);
    bool is_event     = (std::strcmp(root_name, "Event")        == 0);
    bool is_dfselect  = (std::strcmp(root_name, "DFSelect")     == 0);

    const char* channel  = is_dfdata                                     ? "preclassifier_output"
                         : (is_ddfcl_req || is_ddfcl_rep || is_dfselect) ? "preclassifier"
                                                                          : "control";
    const char* msg_kind = (frame_type == 1) ? "request"
                         : is_dfdata         ? "dfdata"
                         : is_event          ? "event"
                                             : "reply";

    JsonWriter j;
    j.key_str("hw",       "ddf550");
    j.key_str("channel",  channel);
    j.key_str("msg_kind", msg_kind);

    const char* id_attr   = root.attribute("id").value();
    const char* type_attr = root.attribute("type").value();
    if (*id_attr)   j.key_str("msg_id",   id_attr);
    if (*type_attr) j.key_str("msg_type", type_attr);

    // Command name -- first <Command> anywhere in the document.
    pugi::xml_node cmd = find_first(root, "Command");
    if (cmd) {
        const char* cmd_name = cmd.attribute("name").value();
        if (*cmd_name) j.key_str("command_name", cmd_name);

        // Direct-text Command body (e.g. AnalysisIntervalMs's "50000") --
        // only meaningful when Command has no element children (otherwise
        // this would just be inter-tag whitespace around <Param> children).
        bool has_element_child = false;
        for (pugi::xml_node c : cmd.children())
            if (c.type() == pugi::node_element) { has_element_child = true; break; }
        if (!has_element_child) {
            std::string val = cmd.text().get();
            size_t a = val.find_first_not_of(" \t\r\n");
            if (a != std::string::npos) {
                size_t b = val.find_last_not_of(" \t\r\n");
                j.key_str("command_value", val.substr(a, b - a + 1));
            }
        }
    }

    // Generic param capture -- every <Param name="X">Y</Param> anywhere in
    // the document, typed by the ICD's Hungarian-notation prefix.
    {
        JsonWriter params;
        for (pugi::xpath_node xn : root.select_nodes(".//Param")) {
            pugi::xml_node p = xn.node();
            std::string name  = p.attribute("name").value();
            std::string value = p.text().get();
            write_typed_param(params, name, value);
        }
        j.key_raw("params", params.str());
    }

    // DFData preclassifier output fields -- every direct child of the
    // root, generic over field name.
    if (is_dfdata) {
        const char* cl_id = root.attribute("DDF-CL-ID").value();
        if (*cl_id) j.key_str("ddf_cl_id", cl_id);

        JsonWriter fields, units;
        bool has_units = false;
        for (pugi::xml_node field : root.children()) {
            const char* tag = field.name();
            fields.key_str(tag, field.text().get());
            const char* unit = field.attribute("Unit").value();
            if (*unit) { units.key_str(tag, unit); has_units = true; }
        }
        j.key_raw("fields", fields.str());
        if (has_units) j.key_raw("units", units.str());
    }

    return j.str();
}

static std::string parse_xml_ddf550(const uint8_t* frame, int frame_len, int frame_type) {
    try {
        return impl_parse_xml_ddf550(frame, frame_len, frame_type);
    } catch (...) {
        return {};
    }
}
```

`parse_message`'s call site (`result = parse_xml_ddf550(frame, iframe, ftype);`)
does not change — only the function body is now a thin try/catch wrapper
around `impl_parse_xml_ddf550`.

- [ ] **Step 4: Rebuild and confirm parity**

```bash
cmake --build drs-bridge/parsers/ddf550/build
ctest --test-dir drs-bridge/parsers/ddf550/build --output-on-failure
```
Expected: `ddf550_frames` passes with the exact same assertions as Step 1's baseline.

- [ ] **Step 5: Commit**

```bash
git add drs-bridge/parsers/ddf550/CMakeLists.txt drs-bridge/parsers/ddf550/src/ddf550_parser.cpp
git commit -m "refactor(ddf550): replace hand-rolled XML scanning with pugixml"
```

---

### Task 3: DDF-1GTX — pugixml migration

**Files:**
- Modify: `drs-bridge/parsers/ddf1gtx/CMakeLists.txt`
- Modify: `drs-bridge/parsers/ddf1gtx/src/ddf1gtx_parser.cpp:115-192` (helpers) and `:465-600` (`parse_xml_ddf1gtx`)
- Test: `drs-bridge/parsers/ddf1gtx/tests/test_frames_ddf1gtx.cpp` (unchanged — parity check)

**Interfaces:**
- Consumes: same pugixml headers as Tasks 1–2 (independent CMake project,
  own compile); `find_first(pugi::xml_node, const char*)` from
  `drs-bridge/parsers/utils/pugixml_helpers.h` (Task 0 — must be complete
  before this task starts). `param_value`/`tag_or_param` are new to this
  task and defined locally (single caller, not duplicated elsewhere — see
  Task 0's rationale for why they don't move to the shared header).
- Produces: no new interface — `parse_message`'s JSON output (`hw`, `channel`,
  `msg_kind`, `msg_id`, `msg_type`, `command_name`, `operation_mode`,
  `frequency_hz`, `freq_begin_hz`, `freq_end_hz`, `att_select`,
  `demodulation`, `af_bandwidth`, `audio_mode_str`, `df_pan_step`,
  `trace_tag_str`, `trace_ip`, `trace_port`, `ddf_cl_id`, `emitter_class`,
  `center_freq_hz`, `bearing_avg_deg`, `level_avg_dbuv`, `raw_xml`) is
  unchanged.

- [ ] **Step 1: Confirm the baseline**

```bash
cmake -S drs-bridge/parsers/ddf1gtx -B drs-bridge/parsers/ddf1gtx/build
cmake --build drs-bridge/parsers/ddf1gtx/build
ctest --test-dir drs-bridge/parsers/ddf1gtx/build --output-on-failure
```
Expected: `ddf1gtx_frames` passes — this is the baseline to match.

- [ ] **Step 2: Add pugixml to the CMake build**

Replace `drs-bridge/parsers/ddf1gtx/CMakeLists.txt` in full:

```cmake
cmake_minimum_required(VERSION 3.20)
project(ddf1gtx_parser CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

# ---- DDF-1GTX parser DLL ----
add_library(ddf1gtx SHARED
    src/ddf1gtx_parser.cpp
    ../utils/pugixml-1.16/pugixml.cpp
)
target_include_directories(ddf1gtx PRIVATE ../dp_ecm/include ../utils/pugixml-1.16 ../utils)

if(MSVC)
    target_compile_options(ddf1gtx PRIVATE /W4 /permissive-)
    set_target_properties(ddf1gtx PROPERTIES LINK_FLAGS
        "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/ddf1gtx.def")
else()
    target_compile_options(ddf1gtx PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---- Unit tests (no external framework; plain asserts) ----
enable_testing()

add_executable(test_ddf1gtx
    tests/test_frames_ddf1gtx.cpp
    src/ddf1gtx_parser.cpp
    ../utils/pugixml-1.16/pugixml.cpp
)
target_include_directories(test_ddf1gtx PRIVATE ../dp_ecm/include ../utils/pugixml-1.16 ../utils)
if(NOT MSVC)
    target_compile_options(test_ddf1gtx PRIVATE -Wall -Wextra)
endif()
add_test(NAME ddf1gtx_frames COMMAND test_ddf1gtx)
```

- [ ] **Step 3: Replace the helpers section and `parse_xml_ddf1gtx` with pugixml**

Add the includes (the file's existing includes end around line 28, next to
`sdfc_endian.h`/`json_writer.h` — add alongside those):

```cpp
#include "pugixml.hpp"
#include "pugixml_helpers.h"
```

In the `Helpers: XML scanning (no LGPL)` block (lines 115–192): keep
`xml_closing_end` (used by `extract_frame`) unchanged; delete `xml_attr`,
`xml_text`, `xml_param_value`. Replace the section header comment
(`find_first` comes from the shared header included above) and add
`param_value`/`tag_or_param` (single-caller, kept local per Task 0's
rationale):

```cpp
// ---------------------------------------------------------------------------
// Content parsing now uses pugixml (find_first from pugixml_helpers.h;
// param_value/tag_or_param below are specific to this file's Param-lookup
// pattern) -- content parsing only; xml_closing_end above is
// frame-boundary detection and still byte-scanning, see extract_frame.
// ---------------------------------------------------------------------------

// Returns the value of the first <Param name="param_name">VALUE</Param>
// anywhere in the document, or "" if not found. This incidentally fixes a
// pre-existing bug in the old xml_param_value/Command-name lookup, which
// used unbounded std::strstr on a malloc'd frame buffer with no guaranteed
// trailing NUL -- pugixml operates on the parsed tree, so there is no
// equivalent overrun risk.
static std::string param_value(pugi::xml_node root, const char* name) {
    for (pugi::xpath_node xn : root.select_nodes(".//Param")) {
        pugi::xml_node p = xn.node();
        if (std::strcmp(p.attribute("name").value(), name) == 0) return p.text().get();
    }
    return {};
}

// Tries <Param name="X">, falls back to a direct <X> tag -- matches the
// repeated "if (v.empty()) v = xml_text(...)" pattern in the original.
static std::string tag_or_param(pugi::xml_node root, const char* name) {
    std::string v = param_value(root, name);
    if (!v.empty()) return v;
    return find_first(root, name).text().get();
}
```

Replace the entire `parse_xml_ddf1gtx` function (originally lines 465–600)
with:

```cpp
// impl_parse_xml_ddf1gtx holds the real logic; parse_xml_ddf1gtx (below)
// wraps it in try/catch so no C++ exception escapes toward parse_message's
// extern "C" boundary (sdfc_abi.h rule 2) -- same rationale and idiom as
// ca120_parser.cpp's impl_parse_xml/parse_xml split.
static std::string impl_parse_xml_ddf1gtx(const uint8_t* frame, int frame_len,
                                           int frame_type)
{
    pugi::xml_document doc;
    pugi::xml_parse_result presult =
        doc.load_buffer(frame, static_cast<size_t>(frame_len));
    // See ca120_parser.cpp's parse_xml for why this new failure path is an
    // accepted, intentional behavior change (design spec §3).
    if (!presult) return {};

    pugi::xml_node root = doc.first_child();
    const char* root_name = root.name();

    bool is_ddfcl_req = (std::strcmp(root_name, "DDFCLRequest") == 0);
    bool is_ddfcl_rep = (std::strcmp(root_name, "DDFCLReply")   == 0);
    bool is_dfdata    = (std::strcmp(root_name, "DFData")       == 0);

    const char* channel  = is_dfdata                       ? "preclassifier_output"
                         : (is_ddfcl_req || is_ddfcl_rep)   ? "preclassifier"
                                                             : "control";
    const char* msg_kind = (frame_type == 1) ? "request"
                         : is_dfdata         ? "dfdata"
                                             : "reply";

    JsonWriter j;
    j.key_str("hw",       "ddf1gtx");
    j.key_str("channel",  channel);
    j.key_str("msg_kind", msg_kind);

    const char* id_attr   = root.attribute("id").value();
    const char* type_attr = root.attribute("type").value();
    if (*id_attr)   j.key_str("msg_id",   id_attr);
    if (*type_attr) j.key_str("msg_type", type_attr);

    // Command name -- first <Command> anywhere in the document.
    {
        pugi::xml_node cmd = find_first(root, "Command");
        const char* cmd_name = cmd.attribute("name").value();
        if (*cmd_name) j.key_str("command_name", cmd_name);
    }

    // --- DfMode (§4.1): eOperationMode ---
    {
        std::string v = tag_or_param(root, "eOperationMode");
        if (!v.empty()) j.key_str("operation_mode", v.c_str());
    }
    // --- MeasureSettingsFFM (§4.2): iFrequency, iFreqBegin, iFreqEnd, eAttSelect ---
    {
        std::string v = tag_or_param(root, "iFrequency");
        if (!v.empty()) j.key_str("frequency_hz", v.c_str());
    }
    {
        std::string v = tag_or_param(root, "iFreqBegin");
        if (!v.empty()) j.key_str("freq_begin_hz", v.c_str());
    }
    {
        std::string v = tag_or_param(root, "iFreqEnd");
        if (!v.empty()) j.key_str("freq_end_hz", v.c_str());
    }
    {
        std::string v = tag_or_param(root, "eAttSelect");
        if (!v.empty()) j.key_str("att_select", v.c_str());
    }
    // --- DemodulationSettings (§4.3): eDemodulation, eAFBandwidth ---
    {
        std::string v = tag_or_param(root, "eDemodulation");
        if (!v.empty()) j.key_str("demodulation", v.c_str());
    }
    {
        std::string v = tag_or_param(root, "eAFBandwidth");
        if (!v.empty()) j.key_str("af_bandwidth", v.c_str());
    }
    // --- AudioMode (§4.4): eAudioMode ---
    {
        std::string v = tag_or_param(root, "eAudioMode");
        if (!v.empty()) j.key_str("audio_mode_str", v.c_str());
    }
    // --- ScanRangeAdd (§4.8): eDFPanStep ---
    {
        std::string v = tag_or_param(root, "eDFPanStep");
        if (!v.empty()) j.key_str("df_pan_step", v.c_str());
    }
    // --- TraceEnable/TraceDisable/TraceDelete (§4.10-4.12): eTraceTag, zIP, iPort ---
    {
        std::string tt = param_value(root, "eTraceTag");
        std::string ip = param_value(root, "zIP");
        std::string pt = param_value(root, "iPort");
        if (!tt.empty()) j.key_str("trace_tag_str", tt.c_str());
        if (!ip.empty()) j.key_str("trace_ip",      ip.c_str());
        if (!pt.empty()) j.key_str("trace_port",    pt.c_str());
    }

    // --- DFData preclassifier output (FORMAT02, §6.7.3) ---
    if (is_dfdata) {
        const char* cl_id  = root.attribute("DDF-CL-ID").value();
        std::string eclass = find_first(root, "EmitterClass").text().get();
        std::string cfreq  = find_first(root, "CenterFrequency").text().get();
        std::string bear   = find_first(root, "BearingAvg").text().get();
        std::string lvl    = find_first(root, "LevelAvg").text().get();
        if (*cl_id)           j.key_str("ddf_cl_id",      cl_id);
        if (!eclass.empty())  j.key_str("emitter_class",   eclass.c_str());
        if (!cfreq.empty())   j.key_str("center_freq_hz",  cfreq.c_str());
        if (!bear.empty())    j.key_str("bearing_avg_deg", bear.c_str());
        if (!lvl.empty())     j.key_str("level_avg_dbuv",  lvl.c_str());
    }

    // Raw XML (capped) for Python-side deep parsing
    static constexpr int RAW_XML_CAP = 16384;
    const char* orig_xml = reinterpret_cast<const char*>(frame);
    int raw_len = (frame_len < RAW_XML_CAP) ? frame_len : RAW_XML_CAP;
    j.key_str("raw_xml", std::string(orig_xml, (size_t)raw_len));

    return j.str();
}

static std::string parse_xml_ddf1gtx(const uint8_t* frame, int frame_len, int frame_type) {
    try {
        return impl_parse_xml_ddf1gtx(frame, frame_len, frame_type);
    } catch (...) {
        return {};
    }
}
```

`parse_message`'s call site (`result = parse_xml_ddf1gtx(frame, iframe, ftype);`)
does not change — only the function body is now a thin try/catch wrapper
around `impl_parse_xml_ddf1gtx`.

- [ ] **Step 4: Rebuild and confirm parity**

```bash
cmake --build drs-bridge/parsers/ddf1gtx/build
ctest --test-dir drs-bridge/parsers/ddf1gtx/build --output-on-failure
```
Expected: `ddf1gtx_frames` passes with the exact same assertions as Step 1's baseline.

- [ ] **Step 5: Commit**

```bash
git add drs-bridge/parsers/ddf1gtx/CMakeLists.txt drs-bridge/parsers/ddf1gtx/src/ddf1gtx_parser.cpp
git commit -m "refactor(ddf1gtx): replace hand-rolled XML scanning with pugixml"
```

---

### Task 4: Update shared ABI documentation

**Files:**
- Modify: `drs-bridge/parsers/dp_ecm/include/sdfc_abi.h:11`
- Modify: `drs-bridge/parsers/ca120/src/CA120_parser_new.cpp:1-20` (header comment only)
- Already modified (done during the design/plan-writing session, not a new
  step — just needs including in this task's commit): `drs-bridge/parsers/utils/xml_to_json/README.md`
  (superseded note added at the top, pointing to the design spec).

**Interfaces:**
- Consumes: nothing new.
- Produces: nothing new — comment-only changes, verified by a clean
  rebuild of all three parsers (no behavior change possible from a comment
  edit, but a rebuild confirms nothing else in the shared header was
  accidentally touched).

`sdfc_abi.h` is the actual shared ABI header `ca120_parser.cpp`,
`ddf550_parser.cpp`, and `ddf1gtx_parser.cpp` all include (confirmed via
each file's `#include "sdfc_abi.h"`). `parser_api.h` (`drs-bridge/parsers/cpp/common/`)
is a separate ABI contract used by a different parser family (the binary
`rdfs`/`comm_df` parsers under `parsers/cpp/`) that this migration does not
touch — it is **not** edited here, since none of those parsers link
pugixml and its "No LGPL dependencies" comment remains fully accurate for
that family as-is.

- [ ] **Step 1: Update `sdfc_abi.h`'s rule 3 comment**

In `drs-bridge/parsers/dp_ecm/include/sdfc_abi.h`, change:

```cpp
//   3. No LGPL-licensed code is linked into the DLL.
```

to:

```cpp
//   3. No LGPL-licensed code is linked into the DLL. pugixml (MIT/zlib) is
//      an approved exception — linked into ca120/ddf550/ddf1gtx for XML
//      content parsing (see docs/ewtss/specs/xml-parsing-pugixml-migration-design.md).
//      The rule's intent is "no copyleft/LGPL," not "zero third-party code."
```

- [ ] **Step 2: Clarify `CA120_parser_new.cpp`'s header comment**

In `drs-bridge/parsers/ca120/src/CA120_parser_new.cpp`, after the existing
header comment block (ends around line 20, before the `Build (from repo
root...)` comment), add:

```cpp
// Note (added during the pugixml migration of the real ca120_parser.cpp):
// this file's approach and the real parser's are NOT the same thing. This
// file emits the full generic XML->JSON mirror shape ({hw, channel,
// msg_kind, body: {...}}) via xml_to_json/. The real ca120_parser.cpp uses
// pugixml too, but only to rebuild its existing curated field set
// (msg_type, subsystems, frequency_hz, raw_xml, etc.) -- same JSON
// contract as before, different parsing engine underneath. This file
// remains an unadopted validation candidate; see
// docs/ewtss/specs/xml-parsing-pugixml-migration-design.md §7.
```

- [ ] **Step 3: Rebuild all three parsers to confirm no regression**

```bash
cmake --build drs-bridge/parsers/ca120/build
cmake --build drs-bridge/parsers/ddf550/build
cmake --build drs-bridge/parsers/ddf1gtx/build
ctest --test-dir drs-bridge/parsers/ca120/build --output-on-failure
ctest --test-dir drs-bridge/parsers/ddf550/build --output-on-failure
ctest --test-dir drs-bridge/parsers/ddf1gtx/build --output-on-failure
```
Expected: all three still pass (comment-only changes to a shared header
can only break something if a stray syntax error was introduced — this
step catches that).

- [ ] **Step 4: Commit**

```bash
git add drs-bridge/parsers/dp_ecm/include/sdfc_abi.h drs-bridge/parsers/ca120/src/CA120_parser_new.cpp drs-bridge/parsers/utils/xml_to_json/README.md
git commit -m "docs(abi): note pugixml as an approved sdfc_abi.h rule-3 exception"
```

---

## Notes for whoever executes this plan

- Task 0 must complete before Tasks 1–3 (all three `#include
  "pugixml_helpers.h"`). Tasks 1–3 are independent of each other (three
  separate DLLs, three separate CMake projects) — safe to run in parallel
  across subagents/engineers once Task 0 is done. Task 4 should run last
  since its comment update describes the end state Tasks 0–3 produce.
- `docs/` is gitignored on the current branch (`rdfs_gloabal_parser`) as
  part of an in-progress repo split — this does not affect any file this
  plan touches (all under `drs-bridge/parsers/`), only the design spec and
  this plan document itself, which were intentionally left as local,
  uncommitted files per the design conversation.
