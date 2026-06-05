# EWTSS v2 — Design, Spec-Validation Artefacts, and MVP Archive

This repository contains the architecture and design documentation for EWTSS v2
(Electronic Warfare Test & Support System, second generation), spec-validation
artefacts that pre-flight implementation plans, and the preserved MVP codebases
that validated the design choices.

> **CI status (paused 2026-05-21):** GitHub Actions hosted runners are disabled
> at the org level for this repository, so the workflow has been parked at
> [`.github/disabled/ci.yml`](.github/disabled/README.md) until runners are
> provisioned. Revival is a one-line `git mv` once that policy changes. The
> intended scope (`drs-server`, `drs-bridge`, `drs-webapp`, `sg-app` — `mvp4`
> excluded for licence reasons; see [ADR-019](docs/ewtss/decision-record.md))
> is unchanged.

## Quick start

### Prerequisites (one-time install on a Windows dev box)

| Tool | Version | Used by | Notes |
|---|---|---|---|
| **Python 3.11+** | 3.11.x preferred | `drs-server`, `drs-bridge` | `py -3.11` launcher must be on PATH |
| **Node.js 22+** | 22.x LTS | `drs-webapp` | `node` + `npm` on PATH |
| **.NET 8 SDK** | 8.0.x | `sg-app`, `mvp4` | `dotnet --version` confirms |
| **STK 12** | 12.9+ | `mvp4` only | Default install path `C:\Program Files\AGI\STK 12\`; COM-registered. See [mvp4/README.md §Prerequisites](mvp4/README.md) for the exact PIA list. |
| **CMake 3.20+** | 3.20.x | `drs-bridge/parsers/reference/` (and per-variant parsers) | Required for the C++ parser integration test to execute (skips cleanly if absent) |
| **MSVC / VS Build Tools 2022** | 17.8+ | C++ parsers on Windows | Comes with Visual Studio 2022; standalone Build Tools also fine |
| **Docker Desktop** | latest | `infrastructure/docker-compose.yml` (local Kafka broker) | Optional — required only for the real-broker Kafka integration test |

### Clone

```powershell
git clone https://github.com/mohit-m_constell/ewtss-v2-design.git
cd ewtss-v2-design
```

### Per-service setup, build, test, run

Each service is self-contained; you only need to set up the ones you're working on.

#### `drs-server` (Python 3.11 + FastAPI)

```powershell
cd drs-server
py -3.11 -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -e .[dev]
.\.venv\Scripts\pytest.exe              # run tests (29 collected; 1 skips without Kafka broker)
uvicorn drs_server.main:app --reload    # dev server on http://localhost:8000
```

#### `drs-bridge` (Python 3.11 + ctypes-loaded C++ parsers)

```powershell
cd drs-bridge
py -3.11 -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -e .[dev]

# Optional: build the reference C++ parser (required for its integration test to run, not skip)
cd parsers\reference
cmake -S . -B build
cmake --build build --config Release
cd ..\..

.\.venv\Scripts\pytest.exe              # run tests (32 collected; 2 skip without cmake)
```

#### `drs-webapp` (Angular 18)

```powershell
cd drs-webapp
npm install                              # first time only
npm run start                            # dev server on http://localhost:4200 (proxies /time + /health to drs-server)
npm test -- --watch=false --browsers=ChromeHeadless  # headless test run
npm run build                            # production build to dist/drs-webapp/
```

#### `sg-app` (production v2 WPF — .NET 8)

```powershell
dotnet build sg-app/sg-app.sln
dotnet test  sg-app/sg-app.sln --filter Category=Unit       # 17 unit tests
dotnet run --project sg-app/Sg.App/Sg.App.csproj
```

Configure `sg-app/Sg.App/appsettings.json` with the drs-server URL (default `http://localhost:8000`) before running.

#### `mvp4` (reference codebase — .NET 8 + STK 12)

```powershell
dotnet build mvp4/Sg.Mvp4.sln
dotnet test  mvp4/Sg.Mvp4.sln --filter Category=Unit         # 77 unit tests (no STK required)
dotnet test  mvp4/Sg.Mvp4.sln --filter Category=Integration  # 4 integration tests (requires STK 12)
dotnet run   --project mvp4/Sg.Mvp4.App/Sg.Mvp4.App.csproj   # requires STK 12 + valid licence
```

See [mvp4/README.md](mvp4/README.md) for the end-to-end walkthrough + troubleshooting table.

### Optional: bring up local infra

```powershell
# Kafka KRaft single-node (for drs-server + drs-bridge Kafka integration tests against a real broker)
docker compose -f infrastructure/docker-compose.yml up -d
cd drs-server
.\.venv\Scripts\python.exe ..\infrastructure\kafka\create-topics.py   # idempotent
```

Tear down: `docker compose -f infrastructure/docker-compose.yml down` (keeps topics) or `down -v` (drops topics).

### End-to-end manual smoke (one dev box, all services)

For ad-hoc full-stack testing on a single workstation, see [Developer Handbook §2.7](docs/ewtss/developer-handbook.md#27-run-multiple-services-together-for-end-to-end-manual-testing).

## What's here

- **`docs/ewtss/`** — the canonical v2 design doc set (executive brief, design
  review brief, operator playbook, architecture overview, decision record,
  deployment guide, developer handbook, execution plan, risk register, design
  backlog, ICD reference, legacy v1 audit). Start here — index at
  [`docs/ewtss/README.md`](docs/ewtss/README.md).
- **`docs/superpowers/`** — historical design specs and implementation plans
  for MVP1 through MVP4.5 (the chronological design pipeline that produced
  the v2 architecture). Active v2 subsystem specs + plans live alongside
  the canonical doc set in [`docs/ewtss/specs/`](docs/ewtss/specs/) and
  [`docs/ewtss/plans/`](docs/ewtss/plans/).
- **`mvp4/`** — the C# WPF + STK ActiveX **reference codebase**. Verifies
  STK invariants and serves as the design reference for the production v2
  application; it is not the v2 product itself.
  Builds on Windows with .NET 8 + STK 12 licence.
  Run tests: `dotnet test mvp4/Sg.Mvp4.sln --filter Category=Unit`.
- **`mvp3/`, `mvp2/`, `mvp/`** — preserved CZML / CesiumJS browser-frontend
  MVPs. Validation artefacts; not actively maintained.
- **`sg-app/`** — production v2 Sg.App scaffold (.NET 8 WPF). IHost
  composition root + IConfiguration + IHttpClientFactory + Serilog +
  exercise lifecycle + banner host. Where the v2 product code lands.
  Run tests: `dotnet test sg-app/sg-app.sln --filter Category=Unit`.
- **`drs-server/`** — production v2 server scaffold (Python 3.11 + FastAPI).
  Owns the three-tier time-sync state engine + `drs.control` Kafka consumer +
  REST + WebSocket API for Sg.App and the DRS webapp + Kafka publisher for
  `system.timesync`. Runtime skeleton landed; B1.3 drs-server side
  complete.
  Run tests: `cd drs-server && .\.venv\Scripts\pytest.exe` (after first-time
  `py -3.11 -m venv .venv && .\.venv\Scripts\pip install -e .[dev]`).
- **`drs-bridge/`** — production v2 per-variant adapter scaffold (Python).
  YAML profile loader + per-variant time-beacon coroutine + Kafka consume-lag
  detector + `ControlPublisher`/`HealthPublisher` to `drs.control` and
  `system.health`. Runtime skeleton landed; data-plane
  (`hw.<variant>.<kind>` topics + hypertable inserts) deferred to
  first-IRS work.
  [`parsers/reference/`](drs-bridge/parsers/reference/) is the canonical C++
  parser template: 4-symbol C ABI + CMake build + pytest integration test
  that exercises the ctypes binding end-to-end (runs locally when `cmake`
  is on PATH; will run on every push once CI is re-enabled — see
  [`.github/disabled/README.md`](.github/disabled/README.md)). New hardware
  variants copy this directory + modify (see
  [Developer Handbook §9.3](docs/ewtss/developer-handbook.md#93-c-parser-interface-contract)).
- **`drs-webapp/`** — production v2 DRS Engineer browser SPA (Angular 18).
  Scaffolded 2026-05-20 (`npx ng new` baseline); proxy config wires `/time`
  and `/health` to a locally-running drs-server during dev. Phase 6
  surfaces (health monitor, IP config, log viewer, time-sync detail
  per [B1.3 §Phase 6](docs/ewtss/plans/time-sync-plan.md)) are open work.
  Run tests: `cd drs-webapp && npx ng test --watch=false --browsers=ChromeHeadless`.
- **`contracts/`** — the versioned source of truth for every cross-repo
  interface (drs-server OpenAPI, Kafka topic JSON Schemas, scenario
  `content_json` schema). Producers change the artifact here first; consumers
  build against it. See [`contracts/README.md`](contracts/README.md). Becomes
  `ewtss-release/contracts/` after the polyrepo split
  ([strategy §3.2](docs/ewtss/specs/repository-and-release-strategy.md)).
- **`packages/`** — vendored third-party installers (offline deployment). See
  [`packages/THIRD-PARTY-LICENCES.md`](packages/THIRD-PARTY-LICENCES.md).
- **`infrastructure/`** — operational scripts + local dev stack.
  [`ntp/`](infrastructure/ntp/README.md) — NTP install + smoke test for
  B1.3 time synchronisation. [`docker-compose.yml`](infrastructure/docker-compose.yml)
  + [`kafka/`](infrastructure/kafka/README.md) — single-node Kafka KRaft
  broker for local development + idempotent topic-creation script.

## Time server (Meinberg NTP) install

The B1.3 design pins **Meinberg NTP** as the internal time-sync layer between
SG (WS1) and WS2 (drs-server, drs-bridge). Target convergence is **≤10 ms**.
Full design rationale: [`docs/ewtss/specs/time-sync-design.md`](docs/ewtss/specs/time-sync-design.md).

### One-time: vendor the Meinberg NTP MSI

The installer itself is not committed to the repo (size + provenance). On
a workstation with internet access:

```powershell
$url  = "https://www.meinbergglobal.com/download/ntp/windows/ntp-4.2.8p18-win64-setup.exe"
$dest = "packages\installers\meinberg-ntp\ntp-4.2.8p18-win-x64-setup.msi"
Invoke-WebRequest -Uri $url -OutFile $dest
Get-FileHash -Algorithm SHA256 $dest
signtool verify /pa $dest      # verify publisher signature
```

Update the SHA-256 in [`packages/installers/meinberg-ntp/README.md`](packages/installers/meinberg-ntp/README.md)
and [`packages/THIRD-PARTY-LICENCES.md`](packages/THIRD-PARTY-LICENCES.md), then
copy the binary into the air-gap mirror that the lab workstations install
from.

### On WS1 (Scenario Generator — acts as Stratum-1 server)

Open an elevated PowerShell, then:

```powershell
cd <repo-root>\infrastructure\ntp
.\sg-ntp-install.ps1
```

The script installs Meinberg NTP, drops the SG-side config in place
(`LOCAL` reference clock, stratum 10, LAN-only restricts), starts the NTP
Windows Service, and verifies via `ntpq -p`.

### On WS2 (DRS workstation — acts as client)

Open an elevated PowerShell, then:

```powershell
cd <repo-root>\infrastructure\ntp
.\ws2-ntp-install.ps1 -SgHost <ws1-ip-or-hostname>
```

The script installs Meinberg NTP, renders the WS2 config template with the
SG host name, starts the service, waits 60 s, and verifies a synced peer.

### Smoke acceptance gate (Phase 1)

On WS2, after the install completes:

```powershell
cd <repo-root>\infrastructure\ntp
.\ntp-smoke.ps1
```

Expected: **PASS** within 5 minutes with max offset under 10 ms. Failure
indicates the Phase 1 gate has not been met — investigate before continuing.

### Uninstall

```powershell
cd <repo-root>\infrastructure\ntp
.\ntp-uninstall.ps1
```

For wider deployment context (where time fits with the rest of the install
order, troubleshooting common failures, air-gap considerations), see the
[Deployment Guide §5](docs/ewtss/deployment-guide.md).

## What this repo is NOT

- Not yet a deployable production system. The v2 service skeletons exist as
  runnable scaffolds — drs-server is the most complete (B1.3 time-sync end
  to end including the `drs.control` consumer); sg-app has Phase 5 (banner
  host + auto-pause); drs-bridge has the runtime skeleton, control-plane
  Kafka publishers, and a reference C++ parser template — its data-plane
  (`hw.<variant>.<kind>` telemetry topics + hypertable inserts) is
  deliberately deferred to first-IRS work. Phase 6 (DRS webapp surfaces)
  is open. Phase 7 (30-min sustained load test) is hardware-bound and
  out of CI scope.
- Not an executable demo. The mvp4 reference requires STK 12 + a valid licence
  on a Windows host. sg-app needs `appsettings.json` configured with a
  reachable drs-server URL to do anything useful at runtime.
- Not the v1 production codebase. v1 lives in a separate internal repo and is
  being replaced by the v2 design captured here.

## Reading paths

- *Design / decision-making readers* → start at [`docs/ewtss/README.md`](docs/ewtss/README.md)
  for audience-targeted paths (executive, architect, ops, engineer, programme
  manager).
- *Engineers picking up a B1.x feature* → read the relevant plan in
  [`docs/ewtss/plans/`](docs/ewtss/plans/) and the corresponding
  design spec in [`docs/ewtss/specs/`](docs/ewtss/specs/). If
  there's a `*-corrigenda.md` next to the plan, read it first — it captures
  the spec-validation findings that updated the plan. Pre-v2 MVP design
  pipeline is preserved at [`docs/superpowers/`](docs/superpowers/) (not
  load-bearing for current work).

## Status

- v2 design: **approved per the executive brief.**
- v2 hardening phase (telemetry pipeline build):
  - **B1.3 Time Synchronisation** — drs-server side + sg-app Phase 5
    integration landed; drs-bridge runtime skeleton landed; DRS webapp
    Phase 6 surfaces + Phase 7 load test open.
  - **Reference C++ parser template** — shipped 2026-05-21 (closes the
    parser-ABI contract gap; new variants copy + modify).
  - **Kafka infrastructure layer** — shipped 2026-05-21 (local KRaft
    broker via docker-compose, idempotent control-plane topic
    provisioner, real-broker integration test). Data-plane payload
    shape deferred to first-IRS work.
  - **CI** — paused 2026-05-21 pending GitHub Enterprise admin
    enabling hosted runners; workflow parked at
    [`.github/disabled/ci.yml`](.github/disabled/ci.yml).
  - Remaining B1.x items pending: full backlog at
    [`docs/ewtss/design-backlog.md`](docs/ewtss/design-backlog.md).
