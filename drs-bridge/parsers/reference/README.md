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
generators like MSVC); `build/reference_parser.so` on Linux (the
`PREFIX ""` line in CMakeLists drops the conventional `lib` prefix
so the name is platform-symmetric).

## Build (CI)

The parked CI workflow at `.github/disabled/ci.yml` (currently inactive
— hosted runners disabled at the GitHub Enterprise org level; see
[`.github/disabled/README.md`](../../../.github/disabled/README.md))
builds this parser on the ubuntu-latest runner before running
drs-bridge's pytest. Once runners are re-enabled, the runtime test
`tests/test_reference_parser_integration.py` will load the resulting
shared library and exercise the ctypes binding end-to-end. If the
binding signatures in `drs-bridge/src/drs_bridge/parser_loader.py`
ever drift from the C ABI declared here, that test fails. Until then,
devs with `cmake` on PATH can run the test locally; without `cmake` it
skips cleanly.
