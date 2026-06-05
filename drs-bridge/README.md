# drs-bridge

Per-variant adapter service. Loads YAML variant profiles, drives the C++
parser library for each IRS, mediates message flow between physical (or
simulated) hardware and Kafka topics. Owns Pattern-2 time-beacon coroutines
and consume-lag detection (B1.3).

See [architecture-overview.md](../docs/ewtss/architecture-overview.md) and
[B1.3 design spec](../docs/ewtss/specs/time-sync-design.md).

## Layout

```
drs-bridge/
  pyproject.toml
  src/drs_bridge/
    __init__.py
    main.py
    profiles/            # YAML profile loading + Pydantic models
    timesync/            # Pattern-2 beacon + tick-lag detector
    parser_loader.py     # ctypes binding for the 4-symbol C ABI
  parsers/
    reference/           # Canonical C++ parser template (CMake + 4-symbol ABI)
                         # Copy + modify per variant; see Developer Handbook §9.3
  tests/
    conftest.py
    profiles/
    timesync/
    test_reference_parser_integration.py   # End-to-end ctypes round-trip (local: skips if cmake absent; CI: paused — see ../.github/disabled/README.md)
```

## Prerequisites

- Python 3.11+ — the `py -3.11` launcher must resolve.
- **Optional:** CMake 3.20+ on PATH — for the reference C++ parser to build (its integration test runs only if the `.dll`/`.so` builds; otherwise it skips cleanly with a clear reason).
- **Optional:** Docker Desktop — for the Kafka real-broker integration test if you run it from here too.

## Setup

```powershell
cd drs-bridge
py -3.11 -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -e .[dev]
```

## Build the reference C++ parser (optional, for the integration test)

```powershell
cd parsers\reference
cmake -S . -B build
cmake --build build --config Release
cd ..\..
```

The build produces `parsers/reference/build/Release/reference_parser.dll` (Windows) or `.../reference_parser.so` (Linux). The pytest session-scoped fixture in `tests/conftest.py` builds this on first use and caches per test session.

New hardware variants copy the entire `parsers/reference/` directory and modify per their IRS — see [Developer Handbook §9.3](../docs/ewtss/developer-handbook.md#93-c-parser-interface-contract) for the rename + per-step walkthrough.

## Test

```powershell
.\.venv\Scripts\pytest.exe              # 32 tests; 2 skipped if cmake absent
.\.venv\Scripts\pytest.exe -v
.\.venv\Scripts\pytest.exe tests/profiles/    # YAML profile tests only
.\.venv\Scripts\pytest.exe tests/timesync/    # B1.3 Pattern-2 tests only
```

## Run (dev)

Bridge runtime requires a Kafka broker, YAML variant profiles, and hardware/simulator on TCP. For the local control-plane-only smoke test:

```powershell
# Bring up Kafka (from repo root)
docker compose -f ..\infrastructure\docker-compose.yml up -d
.\.venv\Scripts\python.exe ..\infrastructure\kafka\create-topics.py

# Then from drs-bridge/
.\.venv\Scripts\python.exe -m drs_bridge.main
```

Full variant integration (parser .dll + simulated hardware) is the Phase 2 / Phase 4 work — see the [drs-bridge runtime skeleton plan](../docs/ewtss/plans/drs-bridge-runtime-skeleton.md).

## Configuration

| Var | Default | What |
|---|---|---|
| `DRS_BRIDGE__KAFKA_BOOTSTRAP` | `localhost:9092` | Kafka broker |
| `DRS_BRIDGE__PROFILES_DIR` | `src/drs_bridge/profiles/` | YAML variant profiles |

See `src/drs_bridge/config.py` for the full list.

## Lint

```powershell
.\.venv\Scripts\ruff.exe check src/ tests/
.\.venv\Scripts\ruff.exe format src/ tests/
```
