# C++ Parser Dev Environment — Installation Steps (Offline Client Machine)

**Maintained by:** Claude (generated from repo inspection, 2026-07-01)
**Audience:** The C++ developer (Person C) installing the `drs-bridge/parsers/*` build
toolchain on the client's air-gapped Windows machine.
**Assumes:** the installers/files below are already sitting on your USB/external drive.
This is only the **install procedure** to run on the target machine.

---

## 1. Install VS Build Tools 2022 (MSVC compiler + Windows SDK)

This gives you `cl.exe`, `link.exe`, `nmake`/MSBuild, and the Windows SDK — the only
compiler this repo targets (see [cppdeveloper.md §9](cppdeveloper.md#9-build-and-deployment)).

1. Copy the offline layout folder (produced ahead of time with `vs_buildtools.exe --layout`)
   onto the client machine, e.g. `D:\offline\vs2022_buildtools\`.
2. Run, from inside that folder:
   ```powershell
   .\vs_buildtools.exe --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --lang en-US
   ```
3. Let it finish (no internet calls — it installs from the local layout). Reboot if prompted.
4. Verify: open **"Developer PowerShell for VS 2022"** from the Start menu and run:
   ```powershell
   cl
   ```
   You should see the Microsoft C/C++ compiler banner, not "command not found."

---

## 2. Install CMake

1. Copy the CMake Windows x64 ZIP (e.g. `cmake-3.30.5-windows-x86_64.zip`) to the client,
   e.g. `C:\tools\cmake\`.
2. Extract it there (no installer needed).
3. Add `C:\tools\cmake\cmake-3.30.5-windows-x86_64\bin` to the system `PATH`:
   ```powershell
   [Environment]::SetEnvironmentVariable(
     "Path",
     $env:Path + ";C:\tools\cmake\cmake-3.30.5-windows-x86_64\bin",
     "Machine"
   )
   ```
4. Open a **new** terminal (PATH changes don't apply to already-open shells) and verify:
   ```powershell
   cmake --version
   ```
   Needs to report **3.20 or higher**.

---

## 3. Install Python 3.11 (optional — only if you need to run the pytest/ctypes integration test)

Skip this whole step if you only need the parsers to compile to `.dll`. Only needed if
you also want `.\.venv\Scripts\pytest.exe` in `drs-bridge` to run (not skip) the C++
integration test.

1. Run the copied installer, e.g. `python-3.11.9-amd64.exe`.
2. Check **"Add python.exe to PATH"** during install.
3. Verify:
   ```powershell
   py -3.11 --version
   ```
4. If you also copied a Python wheels folder (`offline_wheels/`) for `drs-bridge`'s
   dependencies:
   ```powershell
   cd drs-bridge
   py -3.11 -m venv .venv
   .\.venv\Scripts\Activate.ps1
   pip install --no-index --find-links=offline_wheels -e ".[dev]"
   ```

---

## 4. Install Git for Windows (optional)

1. Run the copied Git for Windows installer `.exe`.
2. Accept defaults (Git Bash + PATH integration) unless you have a preference.
3. Verify:
   ```powershell
   git --version
   ```

---

## 5. Install VS Code + C++ extensions (skip if using the Visual Studio 2022 IDE instead)

1. Run the copied VS Code installer `.exe`.
2. Install the two extensions from their local `.vsix` files (this repo's
   `.vscode/extensions.json` currently only lists C#/.NET + Markdown extensions — these two
   are missing for C++ work):
   ```powershell
   code --install-extension ms-vscode.cpptools.vsix
   code --install-extension ms-vscode.cmake-tools.vsix
   ```
3. Verify: open VS Code → Extensions panel → both should show as installed (green check,
   no "Install" button).

---

## 6. Fix the `aus` parser's offline blocker

`drs-bridge/parsers/aus/CMakeLists.txt` defaults to downloading `nlohmann/json` from
GitHub via `FetchContent` at configure time — this **fails with no internet**. Do this
once, before your first offline build:

1. Copy the pre-downloaded `json.hpp` (v3.11.3) to:
   ```
   drs-bridge\parsers\aus\vendor\nlohmann\json.hpp
   ```
2. Open `drs-bridge/parsers/aus/CMakeLists.txt` and comment out lines 16–23:
   ```cmake
   # include(FetchContent)
   # FetchContent_Declare(
   #     nlohmann_json
   #     GIT_REPOSITORY https://github.com/nlohmann/json.git
   #     GIT_TAG        v3.11.3
   #     GIT_SHALLOW    TRUE
   # )
   # FetchContent_MakeAvailable(nlohmann_json)
   ```
3. Add this line in its place:
   ```cmake
   target_include_directories(aus PRIVATE vendor)
   ```
   Add the same line for the `test_aus` target further down the file.
4. Remove (or leave as no-ops) the two `target_link_libraries(... nlohmann_json::nlohmann_json)`
   lines — header-only, nothing to link.

---

## 7. Verify the whole toolchain — build every parser target

Open **"Developer PowerShell for VS 2022"** (needed so `cl.exe` is on `PATH`), `cd` into
`drs-bridge/parsers/`, then run:

```powershell
# Reference template
cd reference; cmake -S . -B build; cmake --build build --config Release; cd ..

# Shared cpp/ (comm_df, rdfs)
cd cpp; cmake -S . -B build; cmake --build build --config Release; cd ..

# dp_ecm — both variants together, or individually
cd dp_ecm; cmake -S . -B build; cmake --build build --config Release; cd ..
cd dp_ecm; cmake -S hf -B hf/build; cmake --build hf/build --config Release; cd ..
cd dp_ecm; cmake -S vu -B vu/build; cmake --build vu/build --config Release; cd ..

# Remaining single-target parsers
foreach ($p in "ca120","ddf1gtx","ddf550","esme","rsec","aus") {
  cd $p; cmake -S . -B build; cmake --build build --config Release; cd ..
}
```

Each should finish with **no network activity** and produce a `.dll` under
`build/Release/` (or `hf/build/Release/dp_ecm_hf.dll` / `vu/build/Release/dp_ecm_vu.dll`
for the split `dp_ecm` build). If `aus` tries to reach GitHub and hangs or errors on DNS
resolution, step 6 wasn't applied correctly.
