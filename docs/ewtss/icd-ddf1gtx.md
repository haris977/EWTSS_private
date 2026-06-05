# ICD Reference — R&S DDF-1GTX HF Direction Finder

**Document source:** R&S-DDF-1GTX-ICD-V2  
**Role in EWTSS v2:** HF-band direction-finding sensor. High-speed scanning variant.  
**Protocol:** Identical to DDF-550 — see [icd-ddf550.md](icd-ddf550.md) for all byte-level
detail. This document records the DDF-1GTX-specific differences only.

---

## 1. Communication Channels

Same port layout as DDF-550:

| Port | Protocol | Direction | Purpose |
|---|---|---|---|
| TCP 9150 | Binary-wrapped XML | Bidirectional | Control / configuration |
| TCP 9152 | EB200 binary | Device → You | Mass data (traces) |
| TCP 9153 | Raw XML | Bidirectional | Preclassifier control |
| TCP 9154 | Raw XML | Device → You | Preclassifier output (FORMAT02) |

DDF-1GTX is a TCP server. Connect as a client.

---

## 2. Protocol Differences vs DDF-550

The DDF-1GTX-ICD-V2 is explicitly described as sharing the same TCP/XML and EB200
protocol as the DDF-550. The only relevant differences are hardware capability:

| Feature | DDF-550 | DDF-1GTX |
|---|---|---|
| Frequency range | Wideband | HF band |
| Scan speed | Standard | High-speed (GTX) |
| EB200 trace types | AUDIO, IFPAN, DF, PSCAN, VIDEO, GPS, etc. | Same set |
| XML commands | Same | Same |
| Preclassifier FORMAT02 | Yes | Yes |
| ICD version | V13 | V2 |

---

## 3. XML Control Channel

Identical to DDF-550 §2. Binary-wrapped XML on port 9150:

```
[MagicWordStart: INT32 BE][Length: INT32 BE][XML text][MagicWordEnd: INT32 BE]
```

Same `<Request>`/`<Reply>` structure, same command names (`DfMode`, `ScanRangeAdd`,
`TraceEnable`, etc.).

---

## 4. EB200 Mass Data Channel

Identical to DDF-550 §3. Same 16-byte header, same `Selector = 0x4242`, same trace tags.

---

## 5. Preclassifier Output

Identical to DDF-550 §4. FORMAT02 XML on port 9154. Same emitter classes (Static, Burst,
Hopper, Chirp). Same `<DFSelect>` filter command on port 9153.

---

## 6. Building `ddf1gtx_parser.dll`

The DLL implementation is identical to `ddf550_parser.dll`. You have two options:

### Option A — Shared source, different DLL name (recommended)

Reuse all source files from `ddf550_parser/`. Change only the DLL name and the
`"hw"` field in `format_response`:

```cpp
// In format_response, change:
"{\"hw\":\"ddf550\", ..."
// to:
"{\"hw\":\"ddf1gtx\", ..."
```

CMakeLists.txt:
```cmake
cmake_minimum_required(VERSION 3.20)
project(ddf1gtx_parser)
set(CMAKE_CXX_STANDARD 17)

# Reuse DDF-550 source tree — only the output DLL name differs.
add_library(ddf1gtx_parser SHARED
    ../ddf550_parser/src/ddf550_parser.cpp
    ../ddf550_parser/src/eb200_frame.cpp
    ../ddf550_parser/src/xml_wrapped.cpp
    ../ddf550_parser/src/preclass_xml.cpp
)
target_include_directories(ddf1gtx_parser PRIVATE
    ../ddf550_parser/include
)
target_compile_definitions(ddf1gtx_parser PRIVATE HW_TAG="ddf1gtx")
if(MSVC)
    target_compile_options(ddf1gtx_parser PRIVATE /W4 /wd4100)
endif()
```

Then in `format_response`:
```cpp
#ifndef HW_TAG
#  define HW_TAG "ddf550"
#endif

return snprintf(out, cap,
    "{\"hw\":\"" HW_TAG "\",\"stream\":\"%s\", ...}", ...);
```

### Option B — Copy

Copy the entire `ddf550_parser/` tree, rename to `ddf1gtx_parser/`,
and change the `"hw"` string. Straightforward but creates duplication to maintain.

---

## 7. Testing Checklist

- [ ] Same test cases as [ddf550 §8](icd-ddf550.md#step-8--testing-checklist)
- [ ] Verify `"hw":"ddf1gtx"` in JSON output (not `"ddf550"`)
- [ ] If real hardware available: send `DeviceInfo` GET request, confirm reply contains DDF-1GTX model string

---

## 8. Key Notes

- Do **not** create a completely separate implementation — the protocols are identical.
- The `"hw"` field in the JSON output is the only functional difference between the two DLLs.
- Keep `ddf550_parser.dll` and `ddf1gtx_parser.dll` in sync: any bug fix in the EB200 parser applies to both.
