# Building the parser DLLs

Each parser under `drs-bridge/parsers/` is a standalone CMake project: a
shared library target (the DLL `drs-bridge`/Python loads via `ctypes`) plus
a `test_*` executable target (plain-assert unit tests, no external
framework). This covers all six current parsers: DDF-550, DDF-1GTX, CA120,
RSEC, and dp_ecm HF/VU.

## DDF-550 / DDF-1GTX / CA120 / RSEC — MinGW-w64 toolchain

These four are configured against MSYS2's MinGW64 compiler
(`C:/msys64/mingw64/bin/c++.exe`), deliberately — not MSVC. The non-MSVC
branch in each `CMakeLists.txt` statically links the MinGW runtime
(`-static-libgcc -static-libstdc++ -static`) so Python's `ctypes.CDLL()`
can load the DLL without `libstdc++-6.dll`/`libgcc_s_seh-1.dll` needing to
be on `PATH` (Python 3.8+'s restricted DLL search path doesn't consult
`PATH` the way a normal EXE launch would).

**If `build/` already exists**, from inside it:
```
cmake --build . --target <name>          # builds the DLL
cmake --build . --target test_<name>     # builds the test exe
./test_<name>.exe                        # runs the tests
```
`<name>` is `ddf550`, `ddf1gtx`, `ca120`, or `rsec`.

**If `build/` doesn't exist** (fresh clone, or it was deleted), configure
it first:
```
cmake -S drs-bridge/parsers/<name> -B drs-bridge/parsers/<name>/build -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/c++.exe
```
then build as above. You also need to re-run this configure step (not
just rebuild) whenever a new `.cpp` file is added to a target — CMake only
re-scans the source list on configure.

## dp_ecm (HF + VU) — currently MSVC, not MinGW

Unlike the four above, `dp_ecm/build` is configured with the
**Visual Studio 18 2026** generator (MSVC), not MinGW. This isn't a code
requirement — `dp_ecm/hf/CMakeLists.txt` and `dp_ecm/vu/CMakeLists.txt`
are structured identically to the other four (`if(MSVC) ... else() ...`,
supporting either compiler) — it's a historical artifact of how the build
directory was first configured (Visual Studio is CMake's default Windows
generator when none is specified).

**Known gap:** the MSVC branch here has no equivalent of the other four's
static-runtime-link trick — no `/MT`, nothing addressing
`vcruntime140.dll`/`msvcp140.dll` availability. If these DLLs are loaded
via the same `ctypes.CDLL()` path as the others, they could hit the same
restricted-search-path failure the MinGW static link exists to avoid,
unless it happens to work today because those runtime DLLs are already on
`PATH` via the Visual Studio install. Not yet verified either way.

**If `build/` already exists**, from inside it:
```
cmake --build . --target dp_ecm_hf
cmake --build . --target dp_ecm_vu
cmake --build . --target test_dp_ecm_hf
cmake --build . --target test_dp_ecm_vu
```

**If `build/` doesn't exist**, matching the current MSVC setup:
```
cmake -S drs-bridge/parsers/dp_ecm -B drs-bridge/parsers/dp_ecm/build -G "Visual Studio 18 2026" -A x64
```
then build as above.

**To build dp_ecm on MinGW instead** (consistent toolchain with the other
four, and closes the runtime-DLL gap above):
```
cmake -S drs-bridge/parsers/dp_ecm -B drs-bridge/parsers/dp_ecm/build -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/c++.exe
cmake --build drs-bridge/parsers/dp_ecm/build --target dp_ecm_hf
cmake --build drs-bridge/parsers/dp_ecm/build --target dp_ecm_vu
```
Note this reconfigures the *existing* `dp_ecm/build` directory to a
different generator — CMake will refuse to reconfigure in-place across
generators, so delete `dp_ecm/build` first if it was previously configured
for Visual Studio, or point `-B` at a separate directory (e.g.
`dp_ecm/build_mingw`) to keep both configurations side by side.
