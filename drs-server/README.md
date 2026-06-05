# drs-server

FastAPI service that owns persistent state for the v2 platform:
TimescaleDB telemetry, scenario metadata, time-sync state engine, RBAC, REST + WebSocket API for SG-Workstation (Sg.App) and the DRS webapp.

See [architecture-overview.md](../docs/ewtss/architecture-overview.md) for the
wider picture and [B1.3 design spec](../docs/ewtss/specs/time-sync-design.md)
for the time-sync subsystem this scaffold currently covers.

## Layout

```
drs-server/
  pyproject.toml
  src/drs_server/
    __init__.py
    main.py              # FastAPI app entry
    timesync/            # B1.3 subsystem
    api/                 # REST routes
  tests/
    conftest.py
    timesync/
```

## Prerequisites

- Python 3.11+ — the `py -3.11` launcher must resolve.
- Optional: Docker Desktop (for the Kafka real-broker integration test; otherwise it skips).

## Setup

```powershell
cd drs-server
py -3.11 -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -e .[dev]
```

The `[dev]` extra pulls in `pytest`, `pytest-asyncio`, `respx`, and `ruff`.

## Test

```powershell
.\.venv\Scripts\pytest.exe              # 29 tests; 1 skipped if Kafka broker not running
.\.venv\Scripts\pytest.exe -v           # verbose
.\.venv\Scripts\pytest.exe tests/timesync/  # subset
```

The skip reason for the Kafka integration test prints clearly: it expects `localhost:9092` to be reachable. Bring up the local broker with:

```powershell
docker compose -f ..\infrastructure\docker-compose.yml up -d
.\.venv\Scripts\python.exe ..\infrastructure\kafka\create-topics.py
```

## Run (dev)

```powershell
uvicorn drs_server.main:app --reload    # http://localhost:8000
```

Health: `curl http://localhost:8000/health`
Time-sync status: `curl http://localhost:8000/time/status`

## Configuration

Env vars (or `.env` next to the venv):

| Var | Default | What |
|---|---|---|
| `DRS_SERVER__NTPQ_PATH` | `ntpq` | Path to the Meinberg `ntpq` binary |
| `DRS_SERVER__KAFKA_BOOTSTRAP` | `localhost:9092` | Kafka broker for `system.timesync` + `drs.control` |
| `DRS_SERVER__POLL_SECONDS` | `10` | NTP poll cadence |

See `src/drs_server/config.py` (Pydantic Settings) for the full list.

## Lint

```powershell
.\.venv\Scripts\ruff.exe check src/ tests/
.\.venv\Scripts\ruff.exe format src/ tests/
```
