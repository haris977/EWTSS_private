# dp_ecm — Data Patterns SDFC↔DRS parser DLLs (Family-A)

Shared C++ frame core + per-variant parser DLLs for the Data Patterns binary
protocol family (DP-ECM-1071 HF, DP-ECM-1074 VU, RDFS, COMM DF). Each variant
compiles to its own DLL but reuses the shared frame core.

## Layout

```
dp_ecm/
  include/
    sdfc_abi.h        # the frozen 4-symbol ABI (extract_frame/parse_message/format_response/free_result)
    sdfc_endian.h     # explicit little-endian load/store helpers (never struct-cast wire bytes)
    sdfc_frame.h      # frame constants, FrameHeader, frame scanner declarations
    json_writer.h     # minimal dependency-free JSON builder (emit path)
  src/
    sdfc_frame.cpp    # SHARED CORE: extract_frame_core + decode_header + validation
    dp_ecm_hf_parser.cpp  # DP-ECM-1071 HF variant: the 4 ABI symbols + per-unit decoders
  tests/
    test_frames.cpp   # golden-frame tests (no framework; asserts + exit code)
  dp_ecm_hf.def       # DLL export list (exactly the 4 ABI symbols)
  CMakeLists.txt
```

## Frame model (authoritative — DP-ECM-1071/1074, little-endian, no CRC)

| Type | Header | Footer | Overhead | Notes |
|---|---|---|---:|---|
| 1 command | `AA AB BA BB` | `CC CD DC DD` | 16 | size@4, group@8, unit@10 |
| 2 response | `EE EF FE FF` | `FF FE EF EE` | 18 | **status(i16)@4**, size@6, group@10, unit@12 |
| 3 stream | `EE EF FE FF` | `FF FE EF EE` | 16 | msg_num@4, size@8 (dedicated sockets) |

Groups: controller firmware = label+100 (100/101/106/108/109/111/112); monitor = label (1/3/4/5/6/7); **socket disambiguates**. Response unit = command unit + 1.

## Build & test

```bash
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build            # runs test_dp_ecm
```

Without CMake (quick check):
```bash
g++ -std=c++17 -Iinclude tests/test_frames.cpp src/sdfc_frame.cpp src/dp_ecm_hf_parser.cpp -o test_dp_ecm && ./test_dp_ecm
```

## Status

| File | Status |
|---|---|
| `dp_ecm_hf_parser.cpp` | **Increment 2** — decoders: 100/2 Version, 100/10 Temperature, 101/40 FH Detection, 101/44 Wideband FFT. 18 tests pass. |
| `dp_ecm_vu_parser.cpp` | **Increment 1** — decoders: 100/2 Version, 101/40 FH Detection, 200/2,4,6,8 Jam ACKs. 12 tests pass. |

## Adding the next per-unit decoder

1. Open the relevant ICD understanding doc and find the unit's payload table.
2. Write a `decode_<name>(const uint8_t* p, int n, JsonWriter& w)` in the
   variant `.cpp`, reading fields with `load_*le` helpers at explicit byte
   offsets, converting to SI units, bound-checking every array `count`.
3. Add an `else if` in `parse_message`'s `(group_id, unit_id)` dispatch.
4. Add a golden-frame test in the matching `test_frames*.cpp`.

## Next steps / open items

- HF increment 3: 101/70 FF Detection, 101/84 Burst Detection, 106/* Jamming,
  109/12 Date-Time, 111/* Group 11 operations.
- VU increment 2: 101/44 Wideband FFT, 101/70 FF Detection, 101/84 Burst,
  112/2 Fast Scan, 111/14 Protected Scan, 111/22 Signal BITE.
- Noise-tolerant `extract_frame` resync (scan past a bad candidate magic).
- Stream-socket entry for frame type 3 (IQ on ports 10021–10028).
- Replace the flat-int extractor in `format_response` with nlohmann/json (MIT).
- Confirm endianness + status field width against a live capture.
- Reconcile the Python host `parser_loader.py` to this ABI (status: R1).
