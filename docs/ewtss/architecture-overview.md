# EWTSS v2 — Architecture Overview

**Audience:** CTO, customer architects, new technical hires.
**Read time:** 20 minutes.
**Purpose:** describe the chosen architecture in enough depth to assess feasibility, plan integrations, and onboard new technical staff. Alternatives analysis is deliberately excluded; see the [Decision Record](decision-record.md) for rejected options and rationale, and the [v2 tech-stack archive](specs/v2-tech-stack-archive.md) for the exhaustive original analysis.

---

## 1. System purpose and context

EWTSS supports authoring, computation, telemetry capture, and analysis of GNSS / DRS hardware test scenarios. It is a Windows-based defence simulation system, run on isolated workstations or LAN-connected workstation clusters. Operators (mission specialists) author scenarios in STK terms (vehicles, sensors, transmitters, coverage definitions, figures of merit), run computations against STK's analytical engine, and capture live telemetry from up to ~100 DRS instances during exercise execution.

The current production system has scaling and stability issues: telemetry write performance degrades after ~10 minutes of sustained load, and adding a new hardware variant requires non-trivial Python + database changes. v2 is a greenfield rebuild addressing both.

### 1.1 Quantitative scope

| Dimension | Value |
|---|---|
| DRS hardware variants in scope | 12+ (RDFS, JV/UHF, JHF, SJRR, JLB, JMB, JHB×4, AUS, PADS) |
| Concurrent DRS instances per deployment | Up to 100 |
| Per-instance message rate | ~10–20 Hz (medium rate) |
| Total sustained message throughput | ~1,000–2,000 msg/s |
| Concurrent UI operators | 2–5 |
| Deployment environment | Air-gapped LAN, two Windows workstations (WS1 + WS2) |
| Delivery format | DVD with vendored dependencies + source code |
| STK integration | Ansys STK 12 for pre-computed link analysis |
| Scenario computation model | Pre-computed batch (not real-time) |
| Engineering team (today, v2 hardening phase) | Senior C# (×1), Senior Python (×1), Python (×2), C++ (×1), Cross-stack lead (×1), DRS-webapp frontend developer (Angular preferred, ×1) |
| Engineering team (when Mode B activates, deferred) | + SG-side Frontend Angular/Cesium primary (E returns to Angular skill set; DRS-webapp G assists), + GIS specialist (~6 weeks part-time) |

## 2. System composition

EWTSS v2 has four major component groups:

- **Scenario authoring + STK computation** (today: C# WPF; future: also ASP.NET Core server + browser SPA)
- **Hardware bridges** (`drs-bridge`)
- **Telemetry pipeline** (`drs-server` + Kafka + TimescaleDB)
- **Frontends — two by persona:**
  - **SG-side frontend** on WS1 for the Scenario Operator: scenario authoring, exercise control, reports, admin. Today: `Sg.App` WPF (Mode A). Future opt-in: `Sg.Web` Angular SPA (Mode B).
  - **DRS-side webapp** on WS2 for the DRS Engineer: hardware health, per-variant monitor-scan, IP/network configuration, message logs. Browser-based, served by `drs-server`. Framework choice (Angular / React / Vue / other) deferred to Milestone-1 design.

### 2.1 High-level diagram

An EWTSS v2 deployment has **two workstations within development scope** — WS1 (SG — Scenario Generator) and WS2 (DRS — Device Replacement Software). The RFQ deployment also includes a client-owned **Control Center (CC)** workstation and per-variant **Entity Controller Applications** on the LAN; these are external to v2 development. v2 exposes integration APIs from SG and/or DRS for them to consume — the specific API surface for CC integration is defined once CC requirements are available.

On the v2-developed side, WS1 is installed for **either Mode A or Mode B at install time** for the SG frontend; the two SG modes do not coexist within a single deployment. The DRS-side webapp on WS2 is independent of the SG-mode choice and is present in every deployment.

```
┌─── WS1 — Scenario Generator (SG) ────────────────────────────────────┐
│                                                                      │
│  ── Mode A configuration (today, primary) ──                         │
│   Sg.App.exe  (C# WPF + STK ActiveX in-process)                 │
│   ├─ Operator authors scenarios via WPF UI                           │
│   ├─ WPF Views / MVVM ─→ Sg.Domain (DTOs + IScenarioBackend)    │
│   │                       ↓                                          │
│   │                       StkScenarioBackend (in-process COM)        │
│   │                       ↓                                          │
│   │                       STK 12 Engine                              │
│   └─ Writes link-analysis output to TimescaleDB on WS2 (over LAN)    │
│                                                                      │
│  ── OR ──                                                            │
│                                                                      │
│  ── Mode B configuration (future, opt-in) ──                         │
│   Sg.Server.exe  (ASP.NET Core + STK ActiveX in-process)        │
│   ├─ Hosts Sg.Domain (same DLL as Mode A)                       │
│   ├─ REST + WebSocket endpoints (one per IScenarioBackend method)    │
│   ├─ Serves Sg.Web SPA static assets                            │
│   ├─ Operator browser on this same workstation (or LAN-attached      │
│   │   non-workstation laptop) loads the SPA                          │
│   └─ Writes link-analysis output to TimescaleDB on WS2 (over LAN)    │
│                                                                      │
│   STK 12 Engine + dedicated GPU                                      │
│   1 STK Engine licence seat                                          │
└──────────────────────────┬───────────────────────────────────────────┘
                           │ LAN
                           │   PostgreSQL 5432 (link-analysis writes)
                           │   drs_server :8000 (REST/WS reads)
                           │
┌──────────────────────────▼───────────────────────────────────────────┐
│                                                                      │
│  WS2 — DRS (Device Replacement Software)                             │
│  Identical configuration regardless of WS1's SG mode.                │
│                                                                      │
│  ┌──────────────────────────────────────────────────────────────┐    │
│  │  DRS webapp  (browser on WS2, served by drs_server)          │    │
│  │  DRS Engineer surface:                                        │    │
│  │   ├─ Health status: LRUs, sub-systems, communication links    │    │
│  │   ├─ Per-variant monitor-scan, message rate, message logs     │    │
│  │   └─ IP / network configuration                               │    │
│  │  Framework: TBD per Milestone-1 design                        │    │
│  └──────────────────────────────────────────────────────────────┘    │
│                                                                      │
│  ┌──────────────────────────────┐  ┌──────────────────────────────┐  │
│  │  drs_bridge (Python + C++)   │  │  drs_server (Python FastAPI) │  │
│  │  ├─ asyncio TCP server per   │  │  ├─ Kafka consumers (per     │  │
│  │  │  hardware port            │  │  │   topic family)           │  │
│  │  ├─ Per-hardware YAML profile│  │  ├─ TimescaleDB writers      │  │
│  │  └─ C++ parser .dll per     │  │  ├─ WebSocket hub (live      │  │
│  │     hardware variant         │  │  │   telemetry to WS1)       │  │
│  │                              │  │  ├─ REST (historical reports)│  │
│  │                              │  │  └─ Serves DRS webapp static │  │
│  └────────────┬─────────────────┘  │     assets to local browser  │  │
│               │                    └──────┬───────────────────────┘  │
│               │ Kafka topics              │                          │
│               │  hw.<variant>.<kind>      │                          │
│               ▼                           ▼                          │
│  ┌──────────────────────────┐  ┌────────────────────────────────┐    │
│  │  Kafka 3.x KRaft         │  │  PostgreSQL 16 + TimescaleDB   │    │
│  │  (single broker)         │  │  - hypertables: telemetry      │    │
│  │                          │  │  - regular tables: users,      │    │
│  │                          │  │    RBAC, scenario metadata,    │    │
│  │                          │  │    link-analysis output (←WS1) │    │
│  └──────────────────────────┘  └────────────────────────────────┘    │
│                                                                      │
│  No STK; no GPU requirement                                          │
└─────────────────┬────────────────────────────────────────────────────┘
                  │ binary protocols (UDP / TCP per hardware spec)
                  │
       ┌──────────▼───────────┐         ┌───────────────────────────┐
       │  DRS hardware        │         │  External (out of dev     │
       │  instances           │         │  scope, client-owned):    │
       │  12+ variants:       │         │   • Control Center (CC) 🔌│
       │  RDFS, JV/UHF, JHF,  │         │     v2 exposes integration│
       │  SJRR, JLB, JMB,     │         │     APIs (TBD per CC reqs)│
       │  JHB×4, AUS, PADS, … │         │   • Entity Controller     │
       │                      │         │     Applications 🔌       │
       │                      │         │     (per-variant IRS over │
       │                      │         │     LAN TCP/UDP from DRS) │
       └──────────────────────┘         └───────────────────────────┘
```

WS1 is the only STK-bearing workstation; WS2 hosts the entire telemetry pipeline plus the shared TimescaleDB plus the DRS webapp served to the local browser for the DRS Engineer. Data flow has three directions on the LAN and one local:

- **WS1 → WS2** (writes, cross-LAN): scenario authoring + STK link-analysis output (PostgreSQL connection, occasional, low volume).
- **WS2 → WS1** (reads, cross-LAN): telemetry stream from `drs_server` for the SG Operator's frontend (WebSocket from drs_server, sustained, up to ~2,000 msg/s at peak).
- **WS2 ↔ browser on WS2** (local): the DRS webapp is served by `drs_server` and accessed from a browser on WS2 itself; the persona is the DRS Engineer (distinct from the SG Operator on WS1).
- **External, out of v2 dev scope, on the LAN**: Control Center (client-owned; v2 exposes integration APIs TBD per CC requirements), Entity Controller Applications (per-variant IRS-compliant TCP/UDP from `drs_bridge`).

Mode commitment for the SG frontend is per-deployment, install-time. Switching from Mode A to Mode B (or vice versa) is a reinstall on WS1, not a runtime toggle. WS2 (DRS-bridge + DRS-server + DRS webapp) is unaffected by the switch — it is present in every deployment.

### 2.2 Project layout (C# side)

Per [ADR-019](decision-record.md), the C# side is split across two top-level directories with distinct namespaces and assembly names — `mvp4/` holds the reference codebase (`Sg.Mvp4.*`), and `sg-app/` holds the v2 production WPF (`Sg.App`). This keeps the bin-directory + editor tooling unambiguous when both are open in the same workspace.

```
mvp4/                                 Reference codebase (Mode A WPF + STK ActiveX)
├── Sg.Mvp4.Domain/                       Portable core
│   ├── Contracts/                            DTOs + IScenarioBackend (no COM types)
│   ├── ViewModels/                           MVVM view-models bound to DTOs
│   ├── Stk/                                  COM impl (StkScenarioBackend in-process)
│   ├── Models/                               Plain enums + small records
│   ├── Interaction/                          InteractionController for placement / edit
│   └── Services/                             Domain-side services (port-side; no COM)
│
├── Sg.Mvp4.App/                          Reference WPF host
│   ├── App.xaml + .cs                        DI bootstrap, RegisterBackend(...)
│   ├── MainWindow.xaml + .cs
│   ├── Views/                                XAML panels + StkDisplayHost
│   └── Services/                             App-tier services
│
├── Sg.Mvp4.Tests/                        Unit + integration tests
│   ├── Fakes/                                In-memory IScenarioBackend impl
│   ├── Contracts/                            DTO JSON round-trip + namespace-fence tests
│   ├── ViewModels/, Interaction/, Services/  Unit tests
│   └── Integration/                          [Apartment(STA)] STK-required tests
│
└── Sg.Mvp4.sln                           Reference codebase solution

sg-app/                               v2 production WPF (.NET 8)
├── Sg.App/                               Production WPF host
│   ├── App.xaml + .cs                        IHost composition root, IConfiguration,
│   │                                         IHttpClientFactory, Serilog, banner host
│   ├── MainWindow.xaml + .cs
│   ├── Contracts/, Models/, Services/        Production-side types (no STK COM today)
│   ├── ViewModels/, Views/, Converters/      WPF UI surface
│   └── appsettings.json + appsettings.Development.json
│
├── Sg.App.Tests/                         Unit tests for production app
└── sg-app.sln                            Production solution
```

`Sg.Mvp4.Domain` is the reference portable core (same DTO + `IScenarioBackend` shapes that the production `Sg.App` consumes). Both projects ship as their own `.dll` (`Sg.Mvp4.App.dll` vs `Sg.App.dll`) so a mixed-workspace build doesn't shadow either's bin output.

**Future Mode B** (the browser-based Angular + CesiumJS SPA on WS1) would land in a third top-level directory (`sg-web/` or similar) when activated — not under `mvp4/`. It is deliberately not scaffolded today; see [ADR-001](decision-record.md) on the Hybrid architecture decision.

### 2.3 Project layout (Python + C++ side)

```
drs/
├── drs-server/                         FastAPI app
│   ├── app/main.py                     uvicorn entrypoint
│   ├── app/consumers/                  Kafka consumers, one per topic family
│   ├── app/storage/                    TimescaleDB models + write helpers
│   ├── app/ws/                         WebSocket hub
│   ├── app/api/                        REST routes (reports, historical queries,
│   │                                   DRS webapp endpoints)
│   └── app/webapp_static/              Built DRS webapp static assets,
│                                       served by drs-server to local browser
│
├── drs-webapp/                         DRS Engineer SPA — Angular 18 (per ADR-018)
│   └── (Milestone-1 design call; built artefacts copied into
│        drs-server/app/webapp_static/ for serving)
│
├── drs-bridge/                         Python config + C++ parser libs
│   ├── src/drs_bridge/main.py          asyncio TCP server scaffolding
│   ├── src/drs_bridge/parser_loader.py ctypes binding for the 4-symbol C ABI
│   ├── src/drs_bridge/profiles/        YAML, one per hardware variant
│   │   ├── rdfs.yaml
│   │   ├── jvuhf.yaml
│   │   └── …
│   └── parsers/                        C++ source per variant (one CMake project each)
│       ├── reference/                  Canonical 4-symbol ABI + CMake template
│       │   ├── include/reference_parser.h
│       │   ├── src/reference_parser.cpp
│       │   └── CMakeLists.txt
│       ├── rdfs/                       (copied from reference/, modified per IRS)
│       └── jvuhf/                      (ditto)
│
└── infrastructure/
    ├── docker-compose.yml              Kafka KRaft single-node (local dev)
    ├── kafka/
    │   ├── README.md                   stack docs + topic inspection commands
    │   └── create-topics.py            idempotent control-plane topic provisioner
    └── ntp/                            Meinberg NTP install + smoke (B1.3)
```

### 2.4 Software architecture layers

Both Mode A and Mode B factor the same way once you look past the transport. The layers below describe the dependency direction (top depends on bottom; bottom never knows about top). The Domain layer is shared by both modes byte-for-byte; everything above and below it can swap without disturbing the contract.

**Mode A (today — desktop):**

```
┌─────────────────────────────────────────────────────────┐
│  Presentation layer                                     │
│  ───────────────────                                    │
│  WPF Views (XAML) + WindowsFormsHost (STK ActiveX)      │
│  Sg.App/Views/, MainWindow.xaml                         │
│            ▲                                            │
│            │ data-binds to                              │
│            ▼                                            │
│  View-models (MVVM)                                     │
│  Sg.Domain/ViewModels/                                  │
│  AircraftViewModel, FacilityViewModel, …                │
│            ▲                                            │
│            │ calls                                      │
│            ▼                                            │
├─────────────────────────────────────────────────────────┤
│  Domain layer  (pure C#, no COM imports)                │
│  ───────────────                                        │
│  DTOs                  → Sg.Domain/Contracts/           │
│  IScenarioBackend      → the boundary interface         │
│  InteractionController → placement / edit state machine │
│            ▲                                            │
│            │ implemented by                             │
│            ▼                                            │
├─────────────────────────────────────────────────────────┤
│  Infrastructure layer  (STK adapter — only here lives   │
│  ─────────────────────  the COM dependency)             │
│  StkScenarioBackend                                     │
│  Sg.Domain/Stk/StkScenarioBackend.cs                    │
│            │                                            │
│            ▼ in-process COM                             │
├─────────────────────────────────────────────────────────┤
│  Platform layer                                         │
│  ──────────────                                         │
│  STK 12 Engine (process-level dependency, ActiveX)      │
└─────────────────────────────────────────────────────────┘
```

**Mode B (future — browser):**

```
┌─────────────── browser process ────────────────┐
│  Presentation layer                            │
│  Angular components + CesiumJS host            │
│  Sg.Web/                                       │
│            ▲                                   │
│            │ binds to                          │
│            ▼                                   │
│  TS view-models  (mirror of C# Domain VMs)     │
│            ▲                                   │
│            │ calls                             │
│            ▼                                   │
│  TS HttpScenarioBackend                        │
│  (one fetch / WS call per IScenarioBackend     │
│   method)                                      │
└────────────────────┬───────────────────────────┘
                     │ HTTP + WebSocket
                     │ (OpenAPI contract)
┌────────────────────▼─────────────── server process ───┐
│  Controller layer                                     │
│  ASP.NET Core endpoints                               │
│  Sg.Server/Program.cs                            │
│  one route per IScenarioBackend method                │
│            ▲                                           │
│            │ calls                                     │
│            ▼                                           │
├───────────────────────────────────────────────────────┤
│  Domain layer  (SAME DLL as Mode A)                   │
│  Sg.Domain.dll                                   │
│            ▲                                           │
│            │ implemented by                            │
│            ▼                                           │
├───────────────────────────────────────────────────────┤
│  Infrastructure layer  (SAME class as Mode A)         │
│  StkScenarioBackend                                   │
└────────────────────┬──────────────────────────────────┘
                     │ in-process COM
                     ▼
                STK 12 Engine
```

**Telemetry pipeline (WS2 — same for both modes):**

```
DRS hardware
   │ TCP/UDP per-variant binary
   ▼
drs-bridge       (Python asyncio + C++ parser .dll)
   │ produces JSON
   ▼
Kafka (KRaft, single broker)
   │ topic hw.<variant>.<kind>
   ▼
drs-server       (Python FastAPI async)
   │ writes to                         │ pushes via
   ▼                                    ▼
TimescaleDB                          WebSocket → frontend (WPF or SPA)
```

**Dependency rules (load-bearing, enforced by tests):**

- DTOs in `Sg.Domain.Contracts` import nothing from `AGI.STKObjects.Interop` or `AGI.STKX`. The `ContractsNamespaceFenceTests` integration test fails the build if a COM type leaks into the Contracts namespace. This is what makes the Domain layer JSON-serialisable, and what unblocks Mode B without re-doing the contract surface.
- View-models depend on `IScenarioBackend` (the interface), never on `StkScenarioBackend` (the implementation). Tests substitute a `FakeScenarioBackend` that runs in <1 ms per call without an STK seat.
- `InteractionController` knows about DTOs and `IScenarioBackend` but nothing about WPF, Angular, or the mouse event source. Mode A hands it `MouseDownEvent` from `WindowsFormsHost`; Mode B (future) will hand it pointer events from CesiumJS — same controller, two callers.
- `StkScenarioBackend` is the single chokepoint for COM. Every gotcha logged in §25.3.5 of the v2 archive (silent `ConvertTo+Assign` no-op, `ApplyObjectEditing` commit semantics, on-demand event subscriptions, etc.) is enforced inside this one class. If you find a COM type elsewhere, that's a bug — fix it back behind the boundary.

### 2.5 System-level layered model

The diagrams in §2.4 are *per-component* layered views. This subsection collapses them into a single cross-system layered model — useful for reasoning about which components own which responsibilities and which calls are legitimate.

Seven horizontal layers span the entire EWTSS v2 system, from physical hardware up to the operator's screen. Each layer communicates only with its adjacent neighbours; no layer skips levels.

```
┌──────────────────────────────────────────────────────────────────────────────┐
│  L6  PRESENTATION  — two surfaces by persona                                 │
│      SG-side (WS1, Scenario Operator):                                       │
│        Mode A:  Sg.App  (WPF + STK ActiveX in WindowsFormsHost)              │
│        Mode B:  Sg.Web  (Electron shell + Angular app + CesiumJS viewer)     │
│      DRS-side (WS2, DRS Engineer):                                           │
│        DRS webapp (browser on WS2, served by drs-server; Angular 18)         │
│      Communicates with: L5 only                                              │
│      Mode A: in-process method calls into Sg.Domain                          │
│      Mode B / DRS webapp: HTTP/REST + WebSocket over the wire to L5          │
├──────────────────────────────────────────────────────────────────────────────┤
│  L5  API GATEWAY / IN-PROCESS BOUNDARY                                       │
│      Mode A:  IScenarioBackend interface inside Sg.App's DI container       │
│              + Sg.App's scenario publisher endpoint (HTTP, exec-only)        │
│      Mode B:  Sg.Server (ASP.NET Core)  +  drs-server (Python FastAPI)      │
│      Always:  drs-server (Python FastAPI) — REST + WebSocket + auth         │
│      Communicates with: L4 (services below), L6 (presentation above)         │
│      Rule: validates inputs; never performs business logic itself.           │
├──────────────────────────────────────────────────────────────────────────────┤
│  L4  BUSINESS LOGIC + SCENARIO DOMAIN                                        │
│      Sg.Domain (C#):  scenario lifecycle, IScenarioBackend impl,            │
│                       InteractionController, view-models, DTOs              │
│      drs-server services (Python):  query/aggregation, broadcast service,   │
│                                      RBAC, JWT issuance, PDF reports        │
│      Communicates with: L3 (Kafka + DB) and L5 (API above) only              │
│      Rule: never opens TCP sockets, never speaks COM directly                │
│            (COM is wrapped behind StkScenarioBackend in L4-L1 adapter).      │
├──────────────────────────────────────────────────────────────────────────────┤
│  L3  MESSAGE BUS + PERSISTENCE                                               │
│      Kafka KRaft (single broker)  │  TimescaleDB (PostgreSQL 16 + Timescale)│
│      — durable event log; hypertable storage; async decoupling between       │
│        services on WS1 (Sg.App) and WS2 (drs-server, drs-bridge).            │
│      Communicates with: L2 (bridge below) and L4 (services above) only.      │
│      Rule: stores state and forwards events; never decides what to do.       │
├──────────────────────────────────────────────────────────────────────────────┤
│  L2  PROTOCOL BRIDGE                                                         │
│      drs-bridge:  asyncio TCP server  +  C++ parser libs  +  ResponseRouter │
│      — bidirectional: parses commands from SDFC, emits responses to SDFC,    │
│        publishes both sides to Kafka.                                        │
│      Communicates with: L1 (TCP below) and L3 (Kafka above) only.            │
│      Rule: per-connection state in coroutines only; no shared mutable state. │
├──────────────────────────────────────────────────────────────────────────────┤
│  L1  TRANSPORT                                                               │
│      TCP/IP LAN — 1 Gbps, point-to-point between SDFC and drs-bridge,       │
│      plus LAN PostgreSQL 5432 (WS1 → WS2) and drs-server REST/WS (WS2 → WS1).│
├──────────────────────────────────────────────────────────────────────────────┤
│  L0  HARDWARE / EXTERNAL SYSTEMS                                             │
│      DRS hardware devices (real, integrated mode only)  │  SDFC application  │
│      STK 12 Engine (COM, in-process on WS1 only)                             │
│      Entity controller applications (LAN, integrated mode only)              │
└──────────────────────────────────────────────────────────────────────────────┘
```

#### Layer communication rules

These rules are load-bearing — violations are the kind of pattern that empirically broke the legacy system (see [Legacy System Audit](legacy-system-audit.md): AP-1 / AP-9 / TS-1 / TS-4 are all instances of layers reaching past their immediate neighbour or skipping the message bus to call across services synchronously).

- **L6 (Presentation) calls L5 (API gateway) only.** Never queries the DB, never publishes to Kafka, never reads `.sc` files directly. In Mode A this is enforced by the `IScenarioBackend` DI registration; in Mode B by the OpenAPI surface plus CSP rules in the Electron shell.
- **L5 (API gateway) calls L4 (services) only.** Validates input, accepts auth, marshals the response — but never reads from PostgreSQL or produces to Kafka itself. Route handlers are thin (`async def` returning a service result; per [Developer Handbook §13.6](developer-handbook.md#136-design-rules-load-bearing)).
- **L4 (Business logic) calls L3 (Kafka + DB) only.** Never opens TCP sockets, never imports `agi.stk12` or `AGI.STKObjects.Interop`. STK COM is wrapped — the only L4 component that *does* talk to STK is `StkScenarioBackend` (an L4 → L0 adapter explicitly approved by [ADR-006](decision-record.md)).
- **L3 (Bus + DB) is passive.** Stores state, forwards events. Never decides what to do — no triggers, no stored procedures with side effects beyond data integrity.
- **L2 (Bridge) calls L3 (Kafka publish / consume) and L1 (TCP read / write) only.** Never calls L4 services synchronously over HTTP for high-frequency paths (the one exception: ResponseRouter in SCENARIO mode calls Sg.App's read-only scenario publisher endpoint per request — explicitly low-frequency, ≤5 ms p99 latency target, see [Architecture Overview §4.5](#45-scenario-compute-and-link-analysis-write-back-scenario-modes-planning)).
- **L1 (Transport) is the cable.** No state, no logic — addressed and configured at deployment time.
- **L0 (External) is opaque.** EWTSS treats DRS hardware, SDFC, entity controller apps, and STK Engine as third-party black boxes governed by their own contracts (ICDs for hardware; the AGI STK COM interface for STK).

#### Communication patterns by mode + workstation

The vertical layers are constant; what changes per mode is *which physical workstation hosts which layer*.

| Layer | WS1 (Mode A) | WS1 (Mode B) | WS2 (always) | LAN traffic |
|---|---|---|---|---|
| L6 Presentation | Sg.App WPF | Sg.Web SPA in browser | DRS webapp in browser on WS2 (DRS Engineer surface) | none |
| L5 API gateway | IScenarioBackend in-process + Sg.App scenario publisher endpoint | Sg.Server ASP.NET Core | drs-server FastAPI | drs-server REST/WS to L6 on WS1 |
| L4 Business logic | Sg.Domain in-process | Sg.Domain in-process (server-side) | drs-server services | none direct (services don't call across LAN) |
| L3 Bus + DB | (writes to L3 on WS2 over LAN) | (writes to L3 on WS2 over LAN) | Kafka KRaft + TimescaleDB | PostgreSQL 5432 from WS1 |
| L2 Protocol bridge | (none on WS1) | (none on WS1) | drs-bridge | (none — bridge is local to WS2) |
| L1 Transport | (none) | (none) | TCP/IP to DRS hardware | TCP per hardware spec |

The split point is L3: anything above L3 lives on WS1; L3 + below lives on WS2; L3 is the shared message bus and database. Cross-LAN calls are only L4(WS1) ↔ L3(WS2) and L5(WS2) ↔ L6(WS1) — and both are point-to-point, not service-to-service.

#### Cross-cutting concerns (per layer they apply at)

These don't belong in a single layer — they're middleware that wraps multiple layers. Listed where they live:

| Concern | Layer | Implementation |
|---|---|---|
| Structured logging | All L4–L5 services | Shared log format with service name + per-connection hardware-id context injected at middleware level |
| Health endpoints | L5 of every service | `GET /health` returns `{"status": "ok", "checks": {...}}`; drs-bridge additionally exposes per-hardware connection state |
| Auth (JWT) | L5 | sg-side issues; drs-server validates same JWT (shared secret, no inter-service HTTP); drs-bridge has no auth (LAN-perimeter is the boundary) |
| Trace context | All services | Trace ID injected by L5 middleware on inbound request; propagated via Kafka message headers across L3 to L2 |
| Audit log | L4 (Sg.Domain `ScenarioChanged` event) | Subscriber writes to TimescaleDB `system_logs` hypertable; never sideways from another L4 component |

The pattern: cross-cutting concerns are *implemented* via middleware / DI / shared libraries — they're never implemented by one layer reaching sideways into another to ask for the same service.

## 3. Component responsibilities

The component names below (`Sg.Domain`, `Sg.App`, `Sg.Server`, `Sg.Web`) are the **logical** project names from the Hybrid architecture. Per [ADR-019](decision-record.md), they map to on-disk projects as follows:

| Logical name (used in §3.1–§3.4) | On-disk reference (mvp4) | On-disk production (sg-app) |
|---|---|---|
| `Sg.Domain` | `mvp4/Sg.Mvp4.Domain/` | inlined into `sg-app/Sg.App/{Contracts,Models,Services}/` (no separate project) |
| `Sg.App` | `mvp4/Sg.Mvp4.App/` | `sg-app/Sg.App/` |
| `Sg.Server` | not yet built (Mode B deferred) | not yet built |
| `Sg.Web` | not yet built (Mode B deferred) | not yet built |

The reference codebase under `mvp4/` is where the architectural patterns are demonstrated (`StkScenarioBackend`, `InteractionController`, the namespace fence). The production code at `sg-app/Sg.App/` is currently the IHost composition root + time-sync banner host + exercise lifecycle — scenario-authoring + STK COM integration are pending Phase 5/6 work that will mirror the mvp4 reference. Subsequent §§ use the logical names for architectural clarity.

### 3.1 `Sg.Domain` — the portable core

The contract surface for everything STK-related. Owns:

- **DTO records** in `Contracts/` — `AircraftDto`, `FacilityDto`, `AreaTargetDto`, `SensorDto`, `CoverageDefinitionDto`, `FigureOfMeritDto`, `EntityNodeDto`, `ComputeResultDto`, plus child types (`WaypointDto`, `VertexDto`). Pure data; no COM imports (enforced by `ContractsNamespaceFenceTests`).
- **`IScenarioBackend` interface** — the single boundary between consumers (view-models, future HTTP endpoints) and the STK-side implementation. Methods cover scenario lifecycle, tree access, per-entity Get/Update, compute, and persistence.
- **`StkScenarioBackend` class** — the only `IScenarioBackend` implementation that talks to STK. Uses `AGI.STKObjects.Interop` PIA via in-process COM. Enforces the MVP4.5 invariants (on-demand event subscription, `AssignGeodetic` for facility position, `ApplyObjectEditing` on user MouseUp, etc. — see Decision Record ADR-013 through ADR-016).
- **View-models** — `AircraftViewModel`, `FacilityViewModel`, etc. — bind DTOs to the UI, dispatch user edits to `IScenarioBackend.UpdateXxx`. Used directly by Mode A; structure mirrored in Mode B's TypeScript view-models.
- **`InteractionController`** — the placement / edit state machine. Click-to-place pending DTO accumulation; finalize / cancel transitions; drag-edit lifecycle. (The specific finalize gesture — keyboard `Enter` in MVP4.5 — is an implementation detail of the host, not part of the controller; see [ADR-016](decision-record.md).)

### 3.2 `Sg.App` — the Mode A WPF host

A thin shell over `Sg.Domain`. Responsibilities:

- DI container setup (`App.xaml.cs::RegisterBackend(InProcess)`) wires `IScenarioBackend → StkScenarioBackend`.
- `MainWindow` hosts the STK ActiveX controls in `WindowsFormsHost`, plus the object tree, property panel, and toolbar.
- Window-level `OnPreviewKeyDown` routes Enter / Esc to the `InteractionController`.
- File dialogs (New scenario, Open .sc, Save As .vdf with optional password).
- Splash window (cold-start STK init takes 15–60 s; visible progress is required).

### 3.3 `Sg.Server` (deferred) — Mode B ASP.NET Core host

When Mode B activates, this project hosts the same `Sg.Domain` core behind an HTTP API:

- `GET /api/scenario` returns the entity tree (`EntityNodeDto[]`).
- `GET/PUT /api/aircraft/{path}`, `GET/PUT /api/facility/{path}`, etc. — one resource per typed entity.
- `POST /api/scenario/entities` (add) and `DELETE /api/scenario/entities/{path}` (remove).
- `GET /api/czml` emits scenario as STK CZML for direct CesiumJS loading.
- WebSocket hub (`/ws/scenario`) broadcasts `ScenarioChanged` events to all connected clients.

OpenAPI spec generated from the endpoints is the canonical wire-format contract for browser developers.

### 3.4 `Sg.Web` (deferred) — Mode B Angular + CesiumJS SPA

Phase B1 (read-only, ~4–6 weeks):
- Loads scenario via CZML for fast initial render.
- WebSocket subscription for live updates.
- No authoring — consumers see what the WPF operator authored.

Phase B2 (full authoring, ~8–12 weeks):
- TypeScript view-models mirroring C# `Sg.Domain.ViewModels` shape.
- TypeScript `HttpScenarioBackend` mirroring `IScenarioBackend` interface (each method = one fetch call).
- CesiumJS-based map interaction for click-to-place, drag-edit (custom hit testing on Cesium primitives, not STK drag handles which only exist on the desktop ActiveX).

### 3.5 `drs-server` — telemetry consumer + DRS webapp host

Python 3.12 + FastAPI (async). Responsibilities:

- Subscribe to Kafka topics for each active hardware variant + message kind.
- Deserialise messages, validate against expected shape, write to TimescaleDB hypertables.
- Maintain WebSocket subscriptions from clients (frontend + analytical tools); push relevant updates per subscription filter.
- Serve historical queries via REST (`/api/reports/...`).
- Manage user / role / feature / permission tables (RBAC inherited from current production schema, simplified).
- **Serve the DRS webapp** (§3.9) — static SPA assets to the local browser on WS2, plus the REST + WebSocket endpoints the webapp consumes for health status, per-variant monitor-scan, IP configuration, and message logs.

### 3.6 `drs-bridge` — hardware bridges

Python config + C++ parser libs. Per hardware variant:

- One YAML profile declaring TCP/UDP port, message types, parser library path, Kafka topic mapping.
- One C++ shared library that takes raw bytes in and emits structured JSON out. No Kafka or Python knowledge in C++.
- Python asyncio TCP server reads bytes from the device, dispatches to the C++ parser, publishes parsed JSON to Kafka via aiokafka.

Adding a new variant = one YAML file + one C++ source file; no Python changes, no `drs-server` changes.

### 3.7 Database — TimescaleDB

PostgreSQL 16 with the TimescaleDB 2.x extension. Schema:

- **Hypertables** (time-series, automatically partitioned): `dr_health_status`, `ff_reports`, `fh_reports`, `burst_reports`, plus per-variant tables. Retention policies configured per data class.
- **Regular tables**: users, roles, features, permissions, scenarios (metadata only — actual scenario files live on disk in `.sc` / `.vdf`), gaming areas, emitters, antennas, modulation profiles.
- **Indexes**: `(time, device_id)` composite on hypertables; standard B-tree on RBAC tables.

Init scripts in `infrastructure/timescaledb/init/`. Run once at first deployment.

### 3.8 Message bus — Kafka KRaft

Apache Kafka 3.x in KRaft mode (no ZooKeeper). Single-broker for v2 deployment; can scale out later if telemetry volume warrants. Topics:

- `hw.<variant>.<message-kind>` — one topic per (variant, kind) pair, e.g. `hw.rdfs.health`, `hw.jvuhf.burst`.
- Partition count tuned per variant's expected concurrent device count.
- Retention: 24 h on telemetry topics by default (raw stream); long-term storage is in TimescaleDB.

### 3.9 DRS webapp — DRS Engineer surface (WS2)

Browser-based SPA served by `drs-server` (§3.5) on WS2. Framework: **Angular preferred** per [v2 Execution Plan §3.7](v2-execution-plan.md#37-g-angular-preferred-else-react--drs-webapp-lead) (skill alignment with E and Mode B future component-library reuse); **React acceptable fallback**. Final framework call signs off at end of week 1 of the v2 hardening phase per [ADR-018](decision-record.md#adr-018--ws2-drs-webapp-required-browser-frontend-on-the-drs-workstation-served-by-drs-server). Independent of the SG-side Mode A / Mode B selection — present in every deployment.

Operator-facing surfaces (consumed by the DRS Engineer at WS2, gated by RBAC):

- **Dashboard** with hardware-variant health-status indicators (LRUs, sub-systems, communication links).
- **Per-variant monitor-scan** showing live message rates, message structure samples, and per-message-class counts.
- **IP / network configuration** for hardware variants — persisted in the configuration store on WS2.
- **Message logs** — Sent Message Log and Receive Message Log per variant, with filtering and sorting.
- **Per-variant control** — restart TCP server / parser if a diagnostic intervention is needed.
- **Kafka consumer health** — at-a-glance consumer-group lag.

All interactions go through `drs-server`'s REST and WebSocket endpoints. The webapp never opens TCP sockets to hardware directly (that responsibility is `drs-bridge`'s, behind drs-server) and never queries TimescaleDB directly (always via drs-server). The layer split is enforced as a structural rule per §2.5.

### 3.10 External integrations (out of v2 development scope) 🔌

Two external systems sit on the LAN; v2 exposes integration APIs for them and does not own the consumer side.

- **Control Center (CC)** — a client-owned application on its own workstation. v2 exposes integration APIs from SG and/or DRS for CC to consume; the specific surface (REST endpoints, message-bus topics, payload schemas) is defined once CC integration requirements are available from the client. Anticipated touchpoints: mission-guidelines delivery (SG → CC); exercise-lifecycle events (SG → CC); aggregate health push (DRS → CC); CC-initiated commands (CC → SG / DRS). See [Operator Playbook §12.1](operator-playbook.md#121-control-center-cc-integration-).
- **Entity Controller Applications** — per-variant, client-owned, reachable on the LAN. v2's `drs-bridge` integrates with them in Integrated mode via TCP/UDP per the IRS for each variant. Bidirectional: DRS → Entity Controller (IRS-compliant frames driven by exercise data) and Entity Controller → DRS (IRS-compliant responses).

## 4. Control and data flows

The system runs in one of four operating-mode combinations. The mode dimensions are independent: **deployment scope** (Standalone or Integrated) and **simulation source** (Random or Scenario). All four flows reuse the same components — only the active topics, the data source, and the presence of entity controller applications change.

### 4.1 Operating modes overview

| Mode | Deployment | Entity controller apps on LAN | DRS data source | STK required |
|---|---|---|---|---|
| **Standalone + Random** | WS1 + WS2 only | None | C++ random generator | No |
| **Standalone + Scenario** | WS1 + WS2 only | None | `computed_links` table (STK pre-computed) | Yes (WS1) |
| **Integrated + Random** | WS1 + WS2 + entity workstations | Yes | C++ random generator | No |
| **Integrated + Scenario** | WS1 + WS2 + entity workstations | Yes | `computed_links` table | Yes (WS1) |

- **Standalone** is for development, system checkout, and operator dry-runs. DRS messages are produced but only flow to local TimescaleDB and the operator's frontend.
- **Integrated** is the primary operational mode (per RFQ): DRS messages additionally flow to entity controller applications over LAN TCP, and entity responses flow back through `drs-bridge` into Kafka.
- **Random** drives DRS values from operator-configured parameter ranges (no scenario, no STK).
- **Scenario** drives DRS values from a pre-computed `computed_links` time-series table, populated by STK link-analysis at planning time on WS1.

The four modes share Kafka topics with role-specific activity:

| Topic | Producer | Consumer | Active in |
|---|---|---|---|
| `hw.<variant>.<kind>` | drs-bridge | drs-server | All modes |
| `entity.<variant>.response` | drs-bridge | drs-server | Integrated modes only |
| `scenario.execution` | scenario publisher (Mode A: `Sg.App`; Mode B future: `Sg.Server`) | drs-bridge | Scenario modes only |
| `drs.control.<variant>` | drs-server | drs-bridge | Random modes (start/stop/config) |

### 4.2 Scenario authoring (Mode A — desktop, today)

1. Operator launches `Sg.App.exe`. App constructs `StkScenarioBackend` (initialises `AgSTKXApplication`, creates `AgStkObjectRoot`, loads or creates an `Untitled` scenario). Splash window shows progress.
2. Operator clicks "Add Aircraft" toolbar button → `InteractionController.BeginPlace(Aircraft)` → mode transitions to `PlacingAircraft` → `StkDisplayHost` subscribes mouse events on the 3D control (on-demand, per ADR-013).
3. Operator left-clicks 4 points on the globe → each click is a `MouseDownEvent` → `_forwardDown3D(x, y)` calls `PickInfo` to get lat/lon → `InteractionController.OnMapMouseDown` accumulates the point in `_pendingPoints`. A yellow rubber-band polyline + orange-red committed-vertex dots render in real time via `IAgStkGraphicsPrimitive` (placement preview).
4. Operator presses Enter → `MainWindow.OnPreviewKeyDown` routes to `controller.FinalizePlacement()` → `FinalizeAircraft()` constructs an `AircraftDto` from pending points + sensible defaults → calls `_stk.AddEntity(EntityKind.Aircraft, name, null)` then `_stk.UpdateAircraft(name, dto)`. STK creates the aircraft, propagates the route, fires `ScenarioChanged`.
5. `ScenarioChanged` event → `ObjectTreeViewModel` checks if the path set changed (new aircraft → yes → rebuild tree); `PropertyPanelHostViewModel` no-op (no current entity in edit mode).
6. New aircraft appears in the tree. Operator selects it; `AircraftViewModel` is constructed with the path, reads the DTO via `_backend.GetAircraft(path)`, populates the panel.

### 4.3 Drag-edit on globe (Mode A)

1. Operator double-clicks an aircraft in the tree → `controller.StartEditingOnMap(path)` → mode transitions to `EditingEntity`.
2. `_onModeChanged`: subscribes editing events (Start / Apply / Stop / Cancel + MouseUp); calls `_globe3D.StartObjectEditing(stkPath)` then forces `_globe3D.Refresh()` so handles paint immediately (per ADR-008's invariants).
3. Operator drags a waypoint handle on the globe and releases. STK's drag updates only the visual position; COM state is not yet committed.
4. `MouseUp` fires → bridge handler calls `_globe3D.ApplyObjectEditing()` (per ADR-015) → STK commits the drag to COM and fires `OnObjectEditingApply`.
5. Apply handler raises `ScenarioChanged` → `PropertyPanelHostViewModel` re-reads the aircraft via `GetAircraft`, updates the panel, marks the VM dirty so the user can see the change is saved.
6. Operator presses Enter → `controller.ApplyEdit()` → mode → Idle → editing-events unsubscribed.

### 4.4 Scenario save (Mode A)

Per [Scenario Management design spec](specs/scenario-management-design.md): the database is the source of truth. `File > Save` serialises the current STK DTO graph to `content_json` (partitioned into `compute_inputs` + `metadata`) and writes it to the `scenarios` table on WS2 PostgreSQL. The save handler computes `sha256(content_json.compute_inputs)`, compares against the row's stored hash, and on mismatch DELETEs the scenario's `computed_links` rows + flips `compute_state` to STALE — all in a single PostgreSQL transaction. Metadata-only edits (name, description, tags, operator notes) skip the DELETE.

`File > Export .sc / .vdf` is a separate operator-triggered flow for STK Desktop / Insight interoperability: `StkScenarioBackend` calls `_root.SaveAs(...)` to write the file. EWTSS v2 does not retain `.sc`/`.vdf` files on disk between exports; the runtime path is purely DB-driven.

Full save + export + import sequences in [command-flows §2.4](command-flows.md#24-save-scenario-to-library) and [§2.10](command-flows.md#210-export-sc--vdf-and-import-sc--vdf).

### 4.5 Scenario compute and link-analysis write-back (Scenario modes, planning)

This runs once before any Scenario-mode execution. It is the bridge between scenario authoring and runtime DRS replay. Per [Scenario Management design §4.4](specs/scenario-management-design.md#44-compute--recompute-flow), compute is split into three transactions so the cross-LAN PostgreSQL write lock is never held during the multi-minute STK loop:

1. Operator finishes authoring (entities, emitters, sensors, environment, antenna patterns placed on the globe).
2. Operator triggers compute: toolbar button → `_backend.ComputeLinks(scenario_id)` on `IScenarioBackend`.
3. **T1 — pre-compute snapshot (fast):** INSERT a row into `scenario_compute_snapshots` capturing the about-to-be-computed scenario state (immutable history; reports reference this).
4. **STK compute loop:** `StkScenarioBackend` walks the time interval tick-by-tick (default 1 s):
   - update mobile entity / emitter positions;
   - compute access intervals (line-of-sight visibility);
   - compute AER (azimuth, elevation, range), path loss, signal strength per (entity, emitter) pair.
   Each 100-tick batch is INSERTed into the `computed_links` hypertable in its own short transaction over the LAN PostgreSQL connection.
5. **T3 — post-compute state flip (fast):** UPDATE `scenarios SET compute_state='COMPUTED', last_computed_snapshot=<new>, last_computed_at=now()`. Operator can now start execution.

```
Mode A WPF              StkScenarioBackend       LAN              WS2 TimescaleDB
   │                          │                   │                    │
   │── ComputeLinks(id) ─────►│                   │                    │
   │── T1: INSERT scenario_compute_snapshots ──────────────────────────►│
   │                          │── per-tick STK    │                    │
   │                          │   AER + path loss │                    │
   │                          │── batch 100 ticks │                    │
   │                          │── (BEGIN, INSERT computed_links, COMMIT)─►│
   │                          │       (repeat per batch)                │
   │                          │                   │                    │
   │── T3: UPDATE scenarios SET compute_state='COMPUTED' ──────────────►│
   │                          │                   │                    │
   │◄── ComputeResultDto ─────│                   │                    │
```

**REPLACE semantics** (per spec §4.5): there is no separate "Recompute" operation. To recompute a STALE scenario, the operator clicks Compute. The save flow has already DELETEd prior `computed_links` rows (when the edit happened); compute writes a fresh snapshot + fresh links + flips state to COMPUTED. The hot-path scenario publisher query in §4.6 stays simple — no `snapshot_id` filter, no version disambiguation — because only one generation of `computed_links` rows exists at any time.

**Snapshot history**: `scenario_compute_snapshots` is immutable — one row per successful compute. Reports reference `snapshot_id` for provenance and bake the link data they need, so reports survive past the next recompute even though `computed_links` is REPLACE-only. See spec §4.6.

**Failure recovery** is documented in [command-flows §2.6](command-flows.md#26-compute-link-analysis-scenario-modes) and spec §4.4 — at worst, operator re-clicks Compute; idempotent at every failure point.

#### Optional: in-memory cache of `computed_links` on Sg.App (latency mitigation)

During execution, drs-bridge's `ResponseRouter` calls Sg.App's scenario publisher endpoint per SDFC command in SCENARIO mode (~10–17 ms p99 over LAN: HTTP round-trip + Sg.App's TimescaleDB query for the matching `(group_id, unit_id, tick)`). For typical IRS budgets this is comfortable, but if measurements at the Phase 4 / Phase 5 integration test (per [v2 Execution Plan §6](v2-execution-plan.md#6-integration-testing-checkpoints)) show this path running tight against the SLA, an in-memory cache on Sg.App is the targeted mitigation:

- **What:** after compute completes, Sg.App keeps the computed_links result in-memory keyed by `(exercise_id, group_id, unit_id, tick)`. ResponseRouter requests are served from the cache instead of round-tripping to PostgreSQL.
- **Latency:** drops from ~10–17 ms p99 to <1 ms p99.
- **Memory cost:** ~36 MB per 1-hour scenario × 10 entities × 10 emitters (negligible).
- **Trigger:** enable only if Phase 4 or Phase 5 latency measurement shows the cross-LAN path approaching IRS budget. Don't pre-optimise — the design's stated load points work without the cache.
- **Invalidation:** straightforward — the cache is read-only after compute, replaced wholesale on next compute.

This is captured as a contingent task in [v2 Execution Plan §3.3](v2-execution-plan.md#33-b-c-specialist--sgapp-extension) (B's responsibilities) and tracked in [Risk Register R7-adjacent](risk-register.md).

### 4.6 Standalone + Scenario execution

DRS messages flow into Kafka and into the operator's frontend. **No entity workstations** — `drs-bridge` TCP servers stay running but no entity apps connect to them. The operator uses this mode to verify scenario behaviour before integrated runs.

```
Sg.App          drs-bridge               Kafka                drs-server          WPF telemetry
(scenario publisher) (formatter)                                                       panels
   │                      │                       │                       │                  │
   │── start exercise ────────────────────────────│                       │                  │
   │                      │                       │                       │                  │
   ├─ tick T: read computed_links from TimescaleDB                        │                  │
   │── publish ──────────────────────────────────►│  scenario.execution   │                  │
   │                      │◄──────────────────────│                       │                  │
   │                      │── C++ formatter:                              │                  │
   │                      │   computed values → IRS bytes                 │                  │
   │                      │── publish ───────────►│  hw.<variant>.<kind>  │                  │
   │                      │                       │──────────────────────►│                  │
   │                      │                       │                       │── write DB       │
   │                      │                       │                       │── WS broadcast ─►│ display
   │                      │                       │                       │                  │
   │── tick T+1 ──────────────────────────────────│  (repeats per tick)   │                  │
```

In Mode A today, the scenario publisher is `Sg.App` itself (the WPF host already has the scenario in COM and reads `computed_links` directly). In Mode B (future), the scenario publisher role moves to `Sg.Server`; the SPA sees the same telemetry over WebSocket.

### 4.7 Integrated + Scenario execution

Adds entity controller applications on the LAN. `drs-bridge` additionally TCP-sends the formatted IRS messages to each connected entity controller, and forwards entity responses back into Kafka on a separate topic. This is the primary mission-execution mode.

```
Sg.App        Kafka            drs-bridge                     Entity apps (LAN)
                                                                    drs-server (WS2)
   │                  │                  │                                │
   │── tick T ────────│                  │                                │
   │── publish ──────►│ scenario.execution                                │
   │                  │─────────────────►│── C++ formatter: IRS bytes     │
   │                  │                  │── TCP send ──────────────────►│ entity processes
   │                  │                  │◄─ TCP response ───────────────│  IRS, displays bearing,
   │                  │                  │                                │  freq, signal strength,
   │                  │                  │── publish ─────────────────►│  jamming params, etc.
   │                  │ entity.<variant>.response                          │
   │                  │                  │                                │
   │                  │◄─ publish        │── publish ─────────────────►│
   │                  │ hw.<variant>.<kind>                                │
   │                  │                  │                                │
   ▼                  ▼                  ▼                                ▼
                                                                    drs-server
                                                                       │
                                                                       ├── write TimescaleDB
                                                                       └── WS broadcast → WPF
                                                                                          (globe + panels +
                                                                                           entity response log)
```

The operator UI on WS1 sees the same live globe animation (entity / emitter motion driven by STK scenario) plus the live measurement stream (driven by `drs-server` WebSocket) plus an entity response log (acks, health, operator commands from entity apps). `pause/resume/stop` from the WPF stops the scenario publisher, which stops feeding new ticks; `drs-bridge` stops sending to entity apps.

### 4.8 Random modes (Standalone + Integrated)

Random replaces the scenario publisher with a per-variant random data generator inside the C++ parser. No `computed_links`, no STK, no scenario.

- Operator configures parameter ranges (frequency min/max, power range, etc.) in the WPF.
- Operator starts a stream → `drs-server` publishes a control message on `drs.control.<variant>` → `drs-bridge` consumes it and begins generating bytes via `C++ generate_random(ranges)`.
- Bytes flow to Kafka `hw.<variant>.<kind>` and (in Integrated) also TCP-out to entity apps.
- The rest of the path (write DB, WS broadcast, frontend display) is identical to the Scenario modes.

This mode is for development and system checkout; the WPF globe shows a health/status view rather than a scenario animation.

### 4.9 Telemetry capture (post-MVP, all modes)

1. DRS hardware writes a packet on its TCP/UDP port.
2. `drs-bridge` asyncio handler receives the bytes, calls the C++ parser library for that variant, gets structured JSON.
3. Bridge publishes to Kafka topic `hw.<variant>.<message-kind>`.
4. `drs-server` consumer reads the message, validates, writes to the appropriate TimescaleDB hypertable.
5. Server's WebSocket hub broadcasts to clients with matching subscription filters.
6. Frontend (WPF in Mode A; Angular in Mode B) updates dashboards in real time.

## 5. Key trade-offs

The architecture is shaped by four load-bearing trade-offs. The Decision Record covers all 18 architectural decisions; these four define the system's character.

### 5.1 In-process STK COM (Mode A) vs out-of-process

In-process COM (chosen) gives microsecond method-call latency and native typing of the STK API. The cost is that the application must be Windows-only, must run as a single process per workstation, and inherits STK's COM threading constraints (STA apartment, no `Marshal.ReleaseComObject` races). For a desktop authoring tool this is the right trade. (See ADR-005.)

### 5.2 Typed entity DTOs vs metadata-driven generic editor

Each STK entity type has its own typed DTO + view-model + WPF panel. Adding a new type costs ~8 files of boilerplate. The alternative — a metadata-driven generic editor with JSON descriptors — was evaluated and rejected for EWTSS's small fixed catalogue (~10 types). Compile-time safety and audit clarity matter more than per-entity boilerplate savings at this scale. (See ADR-007.)

### 5.3 No permanent COM event subscriptions

The MVP4.5 perf-debug cycle proved that permanent subscriptions to STK ActiveX events (mouse, editing, double-click) cost ~1–2 seconds of post-pan-release latency regardless of handler body. The architecture mandates on-demand subscription only — events are subscribed when entering placement / editing mode, unsubscribed on return to Idle. This is load-bearing for the desktop deliverable's UX promise. (See ADR-013.)

### 5.4 Hybrid frontend with shared core

Mode A and Mode B share `Sg.Domain` byte-for-byte; the frontend layer (WPF vs Angular+Cesium) and transport (in-process vs HTTP) are the only differences. This keeps Mode B's marginal cost low when activated, but introduces two authoring UIs that must stay consistent if both modes ship. The Hybrid is a bet that this UI-consistency tax is worth the optionality. (See ADR-001 and the [Hybrid design spec](specs/hybrid-frontend-design.md).)

## 6. Deployment matrix (summary)

An EWTSS v2 deployment within development scope is two workstations (WS1 + WS2). The Control Center workstation and per-variant Entity Controller Applications also sit on the LAN but are client-owned (see §3.10). WS1 is configured at install time for either Mode A or Mode B SG frontend; the two SG modes do not coexist. WS2 (drs-bridge + drs-server + DRS webapp + Kafka + TimescaleDB) is identical across SG modes. Detailed configuration in the [Deployment Guide](deployment-guide.md).

| Workstation | Mode A configuration | Mode B configuration | STK seats | Network |
|---|---|---|---|---|
| **WS1 — SG (Scenario Generator)** | `Sg.App.exe` (WPF; operator authors directly) | `Sg.Server.exe` (ASP.NET Core) + `Sg.Web` SPA + browser on WS1 (or LAN-attached operator laptop) | 1 | LAN to WS2 (PostgreSQL + drs_server) |
| **WS2 — DRS (Device Replacement Software)** | `drs_bridge` + `drs_server` (which also serves the DRS webapp) + Kafka KRaft + PostgreSQL 16 + TimescaleDB 2.x; DRS Engineer browser on WS2 | Same as Mode A (WS2 is identical across SG modes) | 0 | LAN to WS1, cabled / LAN to DRS hardware |

The one-engine licence rule (ADR-012) is satisfied trivially: only WS1 hosts an STK process at all, and only one such process is configured per install. Switching SG modes is a reinstall on WS1, not a runtime toggle; the DRS-side webapp on WS2 is unaffected by SG-mode switching.

## 7. Security boundaries (high-level)

- **Trust zones:** operator workstations are trusted; the telemetry pipeline is trusted; the LAN is presumed isolated. Internet is not present in the standard deployment.
- **Authentication:** RBAC tables in PostgreSQL; users authenticate to `drs-server` via short-lived tokens (specifics deferred to telemetry-phase design). Mode A authoring is single-user per workstation today; multi-user concurrency is out of scope.
- **STK licensing:** per-process; managed via STK's own licence-server protocol or local licence files. Not in EWTSS scope to redistribute.
- **Database access:** `drs-server` and the C# scenario hosts are the only writers. Read-only analytical clients connect via the `drs-server` REST API, not directly to PostgreSQL.
- **Audit:** all scenario authoring operations are observable through the `IScenarioBackend.ScenarioChanged` event; logging this is part of the deployment hardening (deferred to telemetry phase).

## 8. Performance characteristics

### 8.1 Mode A vs Mode B (per-interaction)

| Concern | Mode A (today) | Mode B (future) |
|---|---|---|
| Pan / zoom on STK globe | Native (validated MVP4.5 — no permanent COM subs) | CesiumJS WebGL via browser; per-frame is GPU-bound |
| Click-to-place latency | Microseconds (in-process method calls) | ~10s of ms (HTTP round-trip per accumulated action; WebSocket push for state echo) |
| Scenario load (fresh `.sc`) | 1–5 s for typical scenarios; STK COM-bound | Same on the server; browser additionally streams CZML over WebSocket |
| Cold start | 15–60 s (STK Engine init) | Server cold start similar; browser SPA hot-load is sub-second after server is warm |
| Telemetry ingest target | 100 DRS × 10–20 Hz = ~2,000 msg/s sustained | (telemetry pipeline is shared between modes; same target) |

All cold-start and STK-bound numbers are sensitive to the hardware GPU profile (per ADR-008-adjacent: GPU preference must be set at deployment, see [Deployment Guide §4.3](deployment-guide.md#43-gpu-preference-setup-critical)).

### 8.2 Current production system vs v2 — ceilings and failure modes

The legacy production system has known scaling ceilings that v2 is built to clear. These are the empirical drivers for the architectural choices in §5.

| Dimension | Current ceiling | v2 ceiling | What unblocks the change |
|---|---|---|---|
| Concurrent DRS instances | ~10 (thread-per-client + Python GIL) | 100+ | asyncio (one event loop, no thread contention); single shared `AIOKafkaProducer` per bridge |
| Hardware variants supported | 3 (SRX/MRX/GNSS + JSVUSHF) — duplicated code per variant | Unlimited (linear in YAML + parser source files) | One YAML profile + one C++ parser library per variant; no Python changes for new variants |
| Sustained message throughput | Degrades past ~200 msg/s (per-row commits + sync SQLAlchemy) | 2,000+ msg/s | Batched writes (100 msg / 500 ms); async SQLAlchemy + asyncpg; manual offset commit after batch |
| Database query latency | Degrades with table growth (no indexes, full scans) | Near-constant (TimescaleDB chunk exclusion + composite index `(session_id, recorded_at, message_type)`) | Hypertable partitioning + indexes from day 1 |
| Concurrent UI operators | Degrades past ~5 (threadpool saturation; sync handlers block event loop) | 20+ | All FastAPI handlers `async def`; WebSocket fan-out replaces polling |
| Cost of adding a new DRS type | ~8 files touched per variant (Python schema, ORM, Kafka producer, route, UI) | 1 YAML + 1 C++ parser source file | Generic `drs-bridge` supervisor + dispatch; ICD codegen tool (where applicable) |

The **~10-minute perf-degradation observed in production** that the executive brief calls out resolves to two compounding causes: per-row PostgreSQL commits saturating the legacy system's connection pool, and synchronous SQLAlchemy sessions blocking the FastAPI event loop. v2 fixes both structurally — batched writes plus async sessions, with the rule that no route handler is `def` (only `async def`) enforced in code review.

For the full anti-pattern catalogue (AP-1 … AP-15) with line-level v1 evidence and explicit pointers to v2's structural fixes, see the [v1 Legacy System Audit](legacy-system-audit.md).

## 9. Where this architecture is going next

1. **Mode A productisation** — extend `Sg.App` with Transmitter / Receiver / Antenna typed entities (per the typed-not-metadata decision). Estimated 3–6 weeks.
2. **Telemetry pipeline build** — `drs-server`, `drs-bridge`, TimescaleDB schema, Kafka KRaft, C++ parsers per variant, plus the DRS webapp on WS2. Estimated 4 months (17 weeks), 7 engineers (the v2 hardening phase). Phased build with integration-test checkpoints at each phase. Detailed plan in [v2 Execution Plan](v2-execution-plan.md).
3. **Mode B Phase 1 (read-only browser viewer)** — when customer-funded. Estimated 1.5 months.
4. **Mode B Phase 2 (full browser authoring)** — when customer-funded. Estimated 3 months.

Open architectural decisions, not yet committed (per the Decision Record's "Open decisions" section): Mode B authentication scheme, Mode B real-time push protocol (REST polling vs WebSocket vs SignalR vs gRPC streaming), scenario sync between Mode A workstations and Mode B server (file-based vs live mirroring), GIS tile pipeline (Martin vs MapTiler vs custom).

### 9.1 Mode B activation checklist

When a customer signal warrants activating Mode B, the following work items unlock — none of them are speculative, and each has a starting reference in the existing documentation:

- **Mode B technical design** — the CZML-bridge architecture (STK in-process on the server; CZML as the wire format; CesiumJS-native rendering; physics-accurate not approximated visualisation) is fully specified in [v2 archive §17](specs/v2-tech-stack-archive.md). Lift it into a dated spec under `docs/ewtss/specs/` as the Mode B design starting point.
- **Frontend internal-layer decomposition** — the four-layer Angular / Cesium / Electron model (Electron shell → Angular app → feature modules → Cesium visualisation, with strict imports at each boundary) is in [v2 archive §21.6](specs/v2-tech-stack-archive.md). Becomes load-bearing for the frontend developer once hired.
- **GIS tile pipeline** — the Martin tile server + MBTiles approach, including the ~6-week DTED-acquisition + `ctb-tile` conversion + Martin validation procedure, is in [v2 archive §8.7 + §23.5](specs/v2-tech-stack-archive.md).
- **OpenAPI contract** — the `IScenarioBackend` interface in `Sg.Domain` is the source of truth; generate the OpenAPI spec from `Sg.Server`'s endpoints; freeze that as the wire contract for the SPA.
- **TypeScript view-model mirroring** — the C# view-models in `Sg.Domain.ViewModels` define the shape the SPA's TS view-models mirror. Hand-written today; codegen from the C# types could replace this if drift becomes a problem.
- **Re-open the four open decisions** above (auth, push protocol, scenario sync, tile pipeline) — they don't block Mode A work, but each is a Mode B prerequisite.

**Until Mode B is funded, none of the above is engineering-active.** The Hybrid's optionality lives in the contract boundary that already shipped in MVP4.5 (DTOs, `IScenarioBackend`, JSON round-trip tests, namespace fence — see the [Hybrid design spec](specs/hybrid-frontend-design.md)). The Mode A team does not pay this cost twice.

## 10. MVP roadmap — how the architecture was validated

The current architecture is not theoretical. Each load-bearing decision in the [Decision Record](decision-record.md) is backed by an MVP that empirically validated (or invalidated) a candidate approach. The MVPs ran in sequence over ~6 months; each is preserved as a git branch.

| MVP | What it validated | Stack | Outcome |
|---|---|---|---|
| **MVP1** | Cesium-based browser frontend with `ExportCZML` plugin chain | Angular + CesiumJS + Electron · Python `agi.stk12` backend (port 8001) | Shipped. Proved Cesium + STK feasible; revealed CZML plugin as fragile (post-processing required to convert STK extension packets). |
| **MVP2** | Browser-side draw-first scenario authoring | Same as MVP1 + Cesium primitive authoring · STK 13 (`agi.stk13`, port 8002) | Shipped. Proved authoring in the browser is viable; established the "compute after submit" pattern. |
| **MVP3** | Refined Cesium approach — DataProvider-driven CZML, native sensor primitives | Cesium Ion SDK 1.135 (replaces ExportCZML plugin); Gaussian-lobe antennas; per-sample quaternions for horizontal / nadir aiming; STK 12 (port 8003) | Shipped. **Validated the entire Mode B (browser) path.** Proved we can ship a browser frontend if a customer asks for it. |
| **MVP4** | C# WPF + STK ActiveX in-process — full alternative architecture | C# WPF + WindowsFormsHost · STK 12 ActiveX in-process · map-first click-to-place authoring · native `StartObjectEditing` drag handles · no HTTP backend | Shipped. **Validated the Mode A (desktop) path.** Proved STK-Insight-grade fidelity is achievable; surfaced ~14 STK COM gotchas (documented in v2 archive §25.3.3). |
| **MVP4.5** | Production-quality C# WPF — DTO boundary, perf polish, drag-edit commit, scenario save / load | MVP4 stack + `IScenarioBackend` + DTO Contracts + namespace-fence test + on-demand event subs + `Position.AssignGeodetic` + `ApplyObjectEditing` on MouseUp | **Current state of the C# track**, on branch `feat/mvp4.5-dto-boundary`. Smooth pan, full placement (Aircraft / Facility / AreaTarget) with keyboard finalize, drag-edit on globe with Apply commit. 77 unit + 4 integration tests passing. Made the Hybrid frontend possible: ~90 % of the contract / boundary plumbing required to enable Mode B already shipped here. |

**Why this matters for the architecture decision:**

- **Both Option E (Cesium-based, MVP3) and Option D2 (STK-native desktop, MVP4) are validated.** The choice between them is no longer hypothetical — we have working code on each path.
- The Hybrid (chosen) leans on **MVP4.5's contract boundary** for browser-future readiness. The DTO records, `IScenarioBackend`, JSON round-trip tests, and namespace fence are the load-bearing pieces; without them, Mode B would be a from-scratch effort instead of an additive deliverable.
- **The WS2 DRS webapp ([ADR-018](decision-record.md#adr-018--ws2-drs-webapp-required-browser-frontend-on-the-drs-workstation-served-by-drs-server)) is not explicitly MVP-validated**, but the v1 legacy system has a directly analogous Angular `/drs/*` route cluster (per-variant monitor-scan, IP configuration, drs-login) serving the same persona — confirming the workflow shape is established operator behaviour, not a new invention. The v2 implementation will be a fresh build (likely Angular for skill / reuse alignment), but the operator's mental model is carried forward from v1.
- Several of the architecture's non-obvious invariants (ADR-013 on-demand event subs, ADR-014 `AssignGeodetic`, ADR-015 `ApplyObjectEditing` on MouseUp) were learned the hard way in MVP4 → MVP4.5. They survive in the Decision Record because removing any one of them re-introduces a regression that was empirically painful.
- **MVP2 is omitted from active reference** because MVP3 supersedes it on the Cesium track. The branches are preserved on git for audit but are not the canonical reference.

For the full empirical findings — including the complete list of STK COM gotchas, the ANGLE / D3D11 driver workaround, and the WPF + WindowsFormsHost input-pipeline analysis — see §25 of the [v2 tech-stack archive](specs/v2-tech-stack-archive.md).

## 11. References

- [Decision Record](decision-record.md) — 19 ADRs covering every architectural commitment, with rationale and rejected alternatives.
- [Executive Brief](executive-brief.md) — 1-page CEO/CTO summary.
- [Design Review Brief](design-review-brief.md) — 25-min pre-read for the design-review meeting.
- [Operator Playbook](operator-playbook.md) — workflow-level operator-facing description, anchored on RFQ Annexure A.1, with RFQ → v2 traceability matrix.
- [Design Backlog](design-backlog.md) — Milestone-1 design items required before / alongside the v2 hardening phase (UX wireframes, Time Sync design, CC integration API, etc.).
- [Deployment Guide](deployment-guide.md) — what runs where, hardware specs, licence handling.
- [Developer Handbook](developer-handbook.md) — implementation conventions, STK gotchas, debugging, full DB schema, drs-bridge / drs-server internal layered design.
- [Risk Register](risk-register.md) — consolidated view of active, retired, and deferred risks for the v2 programme.
- [v2 Execution Plan](v2-execution-plan.md) — staffing, parallel workstreams, critical path, and acceptance gates for the v2 hardening phase.
- [ICD reference (COMM DF)](icd-reference-comm-df.md) — concrete worked example of an ICD-to-parser mapping; the template for documenting future hardware variants.
- [Hybrid design spec](specs/hybrid-frontend-design.md) — full proposal for the chosen frontend architecture.
- [MVP4.5 design spec](specs/mvp4.5-dto-boundary-and-perf-design.md) — DTO boundary, IScenarioBackend, namespace fence, perf invariants.
- [ICD codegen tool design](specs/icd-codegen-tool-design.md) — Excel-ICD-to-C++/YAML/TypeScript skeleton generator.
- [v2 tech-stack archive](specs/v2-tech-stack-archive.md) — exhaustive analysis of all alternatives considered (~5,000 lines), including §15 (operating-modes control / data flow), §17 (Mode B technical design), §21 (per-service layered models), §25 (post-implementation findings per MVP).
