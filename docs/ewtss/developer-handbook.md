# EWTSS v2 — Developer Handbook

**Audience:** engineers working on the EWTSS codebase — current team, new hires, contractors.
**Read time:** dip in for the section you need; full read ~45 minutes.
**Purpose:** the operational reference for *implementing* the architecture. Architectural rationale lives in the [Decision Record](decision-record.md); this handbook explains how to be productive in the codebase the architecture produced.

This is a living document. Add to it whenever you hit a non-obvious gotcha that future-you would have wanted documented.

---

## Table of contents

1. [Codebase tour](#1-codebase-tour)
2. [Build, run, test](#2-build-run-test)
3. [Test strategy](#3-test-strategy)
4. [Architectural conventions](#4-architectural-conventions)
5. [STK COM realities (read before touching `StkScenarioBackend`)](#5-stk-com-realities-read-before-touching-stkscenariobackend)
6. [Implementation invariants](#6-implementation-invariants)
7. [Common debugging scenarios](#7-common-debugging-scenarios)
8. [How to add a new STK entity type](#8-how-to-add-a-new-stk-entity-type)
9. [How to add a new hardware variant (telemetry phase)](#9-how-to-add-a-new-hardware-variant-telemetry-phase)
10. [Database schema reference](#10-database-schema-reference)
11. [Mode A scenario authoring — internal design](#11-mode-a-scenario-authoring--internal-design)
12. [`drs-bridge` internal design](#12-drs-bridge-internal-design)
13. [`drs-server` internal design](#13-drs-server-internal-design)
14. [Diagnostic logging](#14-diagnostic-logging)
15. [Code conventions](#15-code-conventions)
16. [Pull request checklist](#16-pull-request-checklist)
17. [Where to find things](#17-where-to-find-things)

---

## 1. Codebase tour

### 1.1 C# side (Mode A primary, Mode B future)

```
mvp4/
├── Sg.Domain/                     The portable core. Referenced by
│   │                                   App, Server (future), and Tests.
│   ├── Contracts/                      Pure data + interface; no COM.
│   │   ├── AircraftDto.cs              record (Name, ColorHex, ...)
│   │   ├── FacilityDto.cs              record (Name, ColorHex, Lat, Lon, Alt)
│   │   ├── AreaTargetDto.cs            record (Name, vertices ...)
│   │   ├── SensorDto.cs                record + PointingMode enum ref
│   │   ├── CoverageDefinitionDto.cs    record (grid bounds, asset paths)
│   │   ├── FigureOfMeritDto.cs         record + FomKind/FomStatistic enum
│   │   ├── EntityNodeDto.cs            tree node for backend.Children
│   │   ├── ComputeResultDto.cs         result of ComputeAll
│   │   └── IScenarioBackend.cs         The contract. ALL backend ops here.
│   │
│   ├── Models/                         Plain enums + small mutable models
│   │   ├── EntityKind.cs
│   │   ├── PointingMode.cs
│   │   ├── FomKind.cs / FomStatistic.cs
│   │   ├── WaypointModel.cs            ObservableObject for VM binding
│   │   └── VertexModel.cs              same
│   │
│   ├── ViewModels/                     MVVM
│   │   ├── EntityViewModelBase.cs      IsDirty, Apply/Reset, Refresh hook
│   │   ├── AircraftViewModel.cs        binds AircraftDto to UI
│   │   ├── FacilityViewModel.cs
│   │   ├── AreaTargetViewModel.cs
│   │   ├── SensorViewModel.cs
│   │   ├── CoverageDefinitionViewModel.cs
│   │   ├── FigureOfMeritViewModel.cs
│   │   ├── ObjectTreeViewModel.cs      tree control VM
│   │   ├── ObjectTreeNodeViewModel.cs  per-node VM
│   │   ├── PropertyPanelHostViewModel.cs  swaps current entity panel
│   │   ├── EmptySelectionViewModel.cs  shown when nothing selected
│   │   └── MainWindowViewModel.cs      top-level VM
│   │
│   ├── Stk/
│   │   └── StkScenarioBackend.cs       The COM impl. ~700 lines. Read §5
│   │                                   before touching this file.
│   │
│   ├── Interaction/
│   │   ├── IInteractionController.cs   placement / edit state contract
│   │   ├── InteractionController.cs    impl: pending-points accumulator
│   │   ├── InteractionMode.cs          enum: Idle | PlacingX | EditingEntity
│   │   └── (IScenarioBackend dependency only; no STK COM here)
│   │
│   └── Services/
│       └── IFileDialogService.cs       UI-host-supplied; not implemented
│                                       in Domain (App provides WPF impl)
│
├── Sg.App/                        WPF host (Mode A)
│   ├── App.xaml + App.cs               DI bootstrap, RegisterBackend
│   ├── Program.cs                      [STAThread] WinForms-Main alternative;
│   │                                   currently unused (App.xaml owns startup)
│   ├── MainWindow.xaml + .xaml.cs      Window-level KeyDown routing
│   ├── Views/Shell/
│   │   ├── ObjectTreeView.xaml + .cs   tree control
│   │   ├── PropertyPanelHostView.xaml  hosts current entity panel
│   │   ├── StkDisplayHost.xaml + .cs   STK ActiveX hosting; the perf-sensitive
│   │   │                               file. Read §6 before touching.
│   │   └── SplashWindow.xaml           cold-start progress
│   ├── Views/Entities/
│   │   ├── AircraftPanel.xaml          per-entity property pages
│   │   ├── FacilityPanel.xaml
│   │   ├── AreaTargetPanel.xaml
│   │   ├── SensorPanel.xaml
│   │   ├── CoveragePanel.xaml
│   │   └── FomPanel.xaml
│   ├── Views/
│   │   ├── NewScenarioDialog.xaml
│   │   └── VdfPasswordDialog.xaml
│   └── Services/
│       └── FileDialogService.cs        WPF impl of IFileDialogService
│
├── Sg.Server/                     [FUTURE — Mode B] ASP.NET Core
│   └── Program.cs                      Map IScenarioBackend → REST endpoints
│
├── Sg.Web/                        [FUTURE — Mode B] Angular + CesiumJS
│   └── (Angular CLI workspace)
│
└── Sg.Tests/                      Unit + integration
    ├── Fakes/
    │   ├── FakeScenarioBackend.cs      In-memory IScenarioBackend impl;
    │   │                               consolidates the 6 deleted entity-
    │   │                               specific fakes
    │   ├── FakeFileDialogService.cs
    │   └── FakeInteractionController.cs
    ├── Contracts/
    │   ├── DtoJsonRoundTripTests.cs    Locks JSON contract per DTO
    │   ├── ContractsNamespaceFenceTests.cs  Forbids AGI.* in Contracts
    │   └── BackendModeTests.cs         InProcess/Remote DI mode resolution
    ├── ViewModels/                     One test class per entity VM
    ├── Interaction/
    │   └── InteractionControllerTests.cs
    └── Integration/
        ├── Scenario34AcceptanceTests.cs    [Apartment(STA)] full flow
        └── StkScenarioBackendIntegrationTests.cs
```

### 1.2 Python services (drs-server + drs-bridge)

Both are pure Python 3.11 packages using the modern src-layout:

```
drs-server/                              # FastAPI server on WS2
├── pyproject.toml                       # FastAPI + aiokafka + httpx + pydantic-settings
├── src/drs_server/
│   ├── main.py                          # FastAPI app, lifespan attached
│   ├── __main__.py                      # uvicorn entry: `python -m drs_server`
│   ├── config.py                        # ServerSettings (env-driven)
│   ├── lifespan.py                      # wires app.state + poll task + Kafka bridge
│   ├── api/
│   │   └── time_status.py               # GET /time/status
│   └── timesync/
│       ├── ntp_monitor.py               # subprocess wrapper around `ntpq`
│       ├── sync_state_engine.py         # 3-tier threshold state machine + SyncStatus enum
│       ├── timesync_publisher.py        # publishes Kafka `system.timesync` events
│       └── poller.py                    # supervised poll loop driving the engine
└── tests/                               # 25 tests, all pure pytest + AsyncMock

drs-bridge/                              # per-variant adapter on WS2
├── pyproject.toml                       # aiokafka + pydantic-settings + PyYAML
├── src/drs_bridge/
│   ├── main.py                          # entry: loads BridgeSettings, runs Runtime
│   ├── config.py                        # BridgeSettings (env-driven)
│   ├── runtime.py                       # Runtime orchestrator (factory-injected seams)
│   ├── bridge.py                        # per-variant lifecycle: register_variant, shutdown
│   ├── control_publisher.py             # Kafka `drs.control` writer
│   ├── health_publisher.py              # Kafka `system.health` writer (consumed by tick_lag_detector)
│   ├── parser_loader.py                 # ctypes wrapper for the 4-symbol C++ ABI
│   ├── profile_loader.py                # scans profiles/*.yaml
│   ├── profiles/
│   │   ├── _schema.py                   # Pydantic VariantProfile + TimeSignalConfig
│   │   └── rdfs.yaml                    # one variant profile shipped today
│   ├── timesync/
│   │   ├── time_beacon.py               # Pattern-2 periodic time-distribution coroutine
│   │   └── tick_lag_detector.py         # health-event publisher on tick-consume lag
│   └── transport.py                     # asyncio TCP command-server + UDP response-sender
└── tests/                               # 30 tests
```

C++ parser source / DLLs are NOT in this repo — they ship per variant when each IRS arrives. The `parser_loader` raises `FileNotFoundError` when no DLL is at the configured path; the rest of the runtime composes around fake parsers in tests.

**Build flow:** none for the server — `pip install -e .` runs from source. For drs-bridge, the C++ parser libraries build separately per [§9 of this handbook](#9-how-to-add-a-new-hardware-variant-telemetry-phase); the resulting `.dll` is dropped into a location the variant's YAML points at.

### 1.3 DRS webapp (Angular)

Owner: G ([v2 Execution Plan §3.7](v2-execution-plan.md#37-g-angular-preferred-else-react--drs-webapp-lead)). Framework: Angular 18, standalone components, scaffolded via `ng new` per [ADR-018](decision-record.md). The webapp is a browser-served SPA hosted by `drs-server` on WS2 for the DRS Engineer persona.

```
drs-webapp/                              # DRS Engineer SPA
├── angular.json
├── package.json                         # Angular 18.2, Karma + Jasmine tests
├── tsconfig.json                        # strict + Angular strictTemplates
├── src/
│   ├── main.ts
│   ├── index.html
│   ├── styles.scss
│   └── app/
│       ├── app.config.ts                # provideRouter, provideHttpClient
│       ├── app.routes.ts                # `/dashboard` + redirect from `/`
│       ├── app.component.{ts,html}      # just <router-outlet />
│       ├── services/
│       │   └── time-sync.service.ts     # HttpClient wrapper + 5s poll observable
│       ├── dashboard/
│       │   ├── dashboard.component.{ts,html,scss}   # /dashboard route
│       │   └── time-sync-card/
│       │       └── time-sync-card.component.{ts,html,scss}
│       └── variants/
│           └── health/
│               └── time-sync-row.component.ts  # per-variant row for Health detail
```

**Build flow:** `npm run build` → output in `dist/drs-webapp/`. In production this is copied alongside `drs-server` and served by FastAPI as static assets at the chosen root path. CI is paused (hosted runners disabled at the org level — see [`.github/disabled/README.md`](../../.github/disabled/README.md)); when re-enabled, the parked workflow at [`.github/disabled/ci.yml`](../../.github/disabled/ci.yml) builds + tests the webapp on every PR.

**Detailed UX wireframes** for each surface are a Milestone-1 deliverable per [Design Backlog B1.1](design-backlog.md#-b11--detailed-ux-wireframes-sg-operator-and-drs-engineer-surfaces). Auth / session-management flows beyond first-login are tracked in [B1.17](design-backlog.md#-b117--authentication-and-session-management-flows-beyond-first-login).

### 1.4 sg-app (v2 production WPF) — alongside mvp4

`sg-app/` is the v2 production target. mvp4 stays as the STK invariants reference codebase per [ADR-019](decision-record.md).

```
sg-app/
├── sg-app.sln
├── Sg.App/
│   ├── Sg.App.csproj                    # .NET 8 WPF + IHost + IConfiguration + IHttpClientFactory + Serilog + CommunityToolkit.Mvvm
│   ├── App.xaml.cs                      # IHost-based composition root
│   ├── MainWindow.xaml(.cs)             # banner host + exercise-control bar + content placeholder
│   ├── appsettings.json / .Development.json
│   ├── Contracts/TimeSyncStatusDto.cs   # /time/status response shape
│   ├── Converters/BannerLevelToBrushConverter.cs, BannerLevelToVisibilityConverter.cs
│   ├── Models/ExerciseState.cs, IExerciseStateService.cs
│   ├── Services/
│   │   ├── ExerciseStateService.cs      # Stopped → Armed → Running → Paused lifecycle
│   │   ├── SyncBannerService.cs         # banner-state coordinator
│   │   └── TimeSyncClient.cs            # HttpClient consumer of /time/status
│   ├── ViewModels/
│   │   ├── MainWindowViewModel.cs       # exercise commands, gating, polling loop, SYNC_LOST auto-pause
│   │   └── TimeSyncViewModel.cs         # Admin Time Sync view (manual Refresh)
│   └── Views/Admin/
│       ├── AdminShellView.xaml          # placeholder; nav host TBD
│       └── TimeSyncView.xaml            # offset / jitter / peer / status / last sync
└── Sg.App.Tests/                        # NUnit 4 + FluentAssertions, 17 tests
```

## 2. Build, run, test

The repo has five active codebases. Their local dev/test loops are documented separately below; common prerequisites first.

### 2.1 Prerequisites

| Service | OS | Toolchain |
|---|---|---|
| `mvp4/` (reference) | Windows 11 | STK 12 (licence-active) + .NET 8 SDK |
| `sg-app/` (v2 production WPF) | Windows 11 | .NET 8 SDK |
| `drs-server/` | Windows or Linux | Python 3.11 (`py -3.11` launcher on Windows) |
| `drs-bridge/` | Windows or Linux | Python 3.11 + a C++ parser DLL per active variant (none ship in the repo today) |
| `drs-webapp/` | any | Node 22 + npm; Chromium / Chrome for headless tests |

Cross-cutting: Git for Windows (Bash or PowerShell — repo supports both). Visual Studio 2022 (17.8+) or VS Code with C# Dev Kit + Python + Angular Language Service extensions covers everything.

### 2.2 mvp4 (reference codebase — Mode A WPF + STK ActiveX)

```
cd mvp4
dotnet build Sg.Mvp4.sln                          # ~10 s clean
dotnet run --project Sg.Mvp4.App --configuration Release
dotnet test --filter Category=Unit                # 84 tests, <1 s, no STK
dotnet test --filter Category=Integration         # requires live STK 12
```

Always use `Release` for perf-sensitive work — Debug pan/zoom is noticeably slower. Integration tier is `[Apartment(STA)]`-marked; run before merging changes that touch `StkScenarioBackend` or the interaction controller.

mvp4 is intentionally **excluded from CI** ([ADR-019](decision-record.md), [parked workflow](../../.github/disabled/ci.yml)) because its csproj references `AGI.STK*.Interop.dll` via HintPaths only present on a workstation with STK installed.

### 2.3 sg-app (v2 production WPF)

```
cd sg-app
dotnet restore sg-app.sln                         # first time only
dotnet build sg-app.sln                           # ~3 s clean
dotnet test sg-app.sln --filter Category=Unit     # 17 tests, ~1 s
dotnet run --project Sg.App                       # launches the WPF window
```

Configure the drs-server URL via `sg-app/Sg.App/appsettings.json` or `appsettings.Development.json` (`DrsServer:Url` + `DrsServer:PollSeconds`). The composition root in `App.xaml.cs` constructs an IHost with `IConfiguration` + `IHttpClientFactory` + Serilog + `IExerciseStateService` + the banner host; see [ADR-019](decision-record.md) for the mvp4-vs-sg-app split.

For end-to-end Time Sync wiring, point `DrsServer:Url` at a running drs-server (next section); without one the banner stays on `sync_lost`.

### 2.4 drs-server (Python FastAPI)

```powershell
cd drs-server
py -3.11 -m venv .venv                            # first time only
.\.venv\Scripts\pip install -e ".[dev]"
.\.venv\Scripts\pytest                            # 25 tests, <2 s
.\.venv\Scripts\python -m drs_server              # serves on :8000
```

Or POSIX shell:

```
cd drs-server
python3.11 -m venv .venv
./.venv/bin/pip install -e ".[dev]"
./.venv/bin/pytest
./.venv/bin/python -m drs_server
```

Environment variables (Pydantic-Settings, prefix `DRS_SERVER_`):

- `DRS_SERVER_NTPQ_PATH` — path to the Meinberg `ntpq` binary (default Windows path).
- `DRS_SERVER_KAFKA_BOOTSTRAP` — Kafka `bootstrap.servers` (default `localhost:9092`).
- `DRS_SERVER_POLL_SECONDS` — NTP poll interval (default 5).
- `DRS_SERVER_LOG_LEVEL`, `DRS_SERVER_HOST`, `DRS_SERVER_PORT`.

The FastAPI lifespan handler (see `drs_server/lifespan.py`) wires `NtpMonitor` + `SyncStateEngine` + `AIOKafkaProducer` + `TimesyncPublisher` and starts the supervised poll loop. `/time/status` returns the engine's current state; `/health` returns 200. Real `ntpq` is required at runtime (the Meinberg NTP install procedure is in [Deployment Guide §4.5 / §5.6](deployment-guide.md)); for local-dev without ntpq, point `DRS_SERVER_NTPQ_PATH` at a stub or use the integration test pattern in `tests/test_integration_time_status.py`.

### 2.5 drs-bridge (Python adapter)

```powershell
cd drs-bridge
py -3.11 -m venv .venv                            # first time only
.\.venv\Scripts\pip install -e ".[dev]"
.\.venv\Scripts\pytest                            # 30 tests, <1 s
.\.venv\Scripts\python -m drs_bridge              # starts the runtime
```

Environment variables (prefix `DRS_BRIDGE_`):

- `DRS_BRIDGE_PROFILES_DIR` — directory containing `*.yaml` variant profiles (default `src/drs_bridge/profiles/`).
- `DRS_BRIDGE_KAFKA_BOOTSTRAP`, `DRS_BRIDGE_LOG_LEVEL`.

The runtime (see `drs_bridge/runtime.py`) loads every YAML, opens the variant's parser DLL via ctypes (the loader exists; no real DLL ships in this repo — vendor per variant), starts a TCP command-server + UDP response-sender per variant, wires `ControlPublisher` + `HealthPublisher` + `Bridge`, calls `register_variant` per profile, and supervises the resulting tasks. Signal handlers route SIGINT/SIGTERM to clean shutdown.

To run locally without a real DLL, point the YAML's `parser_lib` at a stub path; the loader will raise `FileNotFoundError` and you can isolate the rest of the runtime via the factory-injected seam (see `tests/test_runtime.py`).

### 2.6 drs-webapp (Angular 18)

```
cd drs-webapp
npm ci                                            # first time, ~1 min
npm test -- --watch=false --browsers=ChromeHeadless   # 24 specs, ~5 s
npm run build                                     # production build into dist/
npm run start                                     # dev server on :4200
```

`npm run start` proxies nothing by default; for the dashboard to fetch `/time/status` from a locally-running drs-server, either add a `proxy.conf.json` to `angular.json` or change the service's base URL temporarily. The card stays on `loading...` if drs-server is unreachable — that's the documented behaviour.

In production, the built `dist/drs-webapp/` is served statically by drs-server per [ADR-018](decision-record.md). The webapp framework choice is [ADR-018](decision-record.md) (Angular preferred; React acceptable fallback — this codebase uses Angular).

### 2.7 Run multiple services together for end-to-end manual testing

1. Start drs-server: `cd drs-server && .\.venv\Scripts\python -m drs_server`. It tries to connect to Kafka at `localhost:9092` — start a local broker (KRaft single-node) or set `DRS_SERVER_KAFKA_BOOTSTRAP` to a reachable one. Without Kafka, the lifespan's `producer.start()` will retry indefinitely.
2. Start drs-webapp: `cd drs-webapp && npm run start`. Add a proxy entry if the default `/time/status` URL doesn't resolve.
3. Start sg-app: `cd sg-app && dotnet run --project Sg.App`. Confirm the banner host shows `healthy` (or whatever state the drs-server engine is in).

For the time-sync subsystem to converge to `healthy`, the drs-server's `NtpMonitor` needs a real local `ntpq` daemon. Install per [Deployment Guide §4.5 / §5.6](deployment-guide.md) or mock for local-dev.

## 3. Test strategy

### 3.1 What gets unit-tested

- View-model logic (e.g. `AircraftViewModel`'s IsDirty toggling, command enablement).
- `InteractionController` state transitions.
- DTO JSON round-trip (every DTO).
- Namespace fence (no AGI.* in Contracts).
- Backend-mode DI switch.

Unit tests use `FakeScenarioBackend` which implements `IScenarioBackend` over in-memory dictionaries. No COM, no STK.

### 3.2 What gets integration-tested

- `StkScenarioBackend` against real STK: AddEntity, UpdateXxx, GetXxx, ComputeAll, SaveAsSc/LoadSc.
- Scenario34 acceptance test — end-to-end author + save + reload.
- BackendModeTests' `InProcess_mode_resolves_StkScenarioBackend` requires STK.

### 3.3 What gets manually smoke-tested (today)

- Pan / zoom smoothness on the 3D globe.
- Click-to-place + Enter to finalize, for each placement-mode entity (Aircraft, Facility, AreaTarget).
- Drag-edit on globe, with Apply enabling after drag-release.
- Save .sc, close app, reopen, verify scenario loads.

Build a "smoke pass" runbook per release. Until that exists, run the [End-to-end walkthrough](../../mvp4/README.md#end-to-end-walkthrough) in the MVP4 README.

### 3.4 Adding a unit test for an entity VM

Pattern (see `AircraftViewModelTests.cs` for a working example):

```csharp
[Test]
public void Editing_a_field_marks_VM_dirty_until_Apply()
{
    var backend = new FakeScenarioBackend();
    backend.NewScenario("test", DateTime.UtcNow, DateTime.UtcNow.AddHours(1));
    backend.AddEntity(EntityKind.Aircraft, "A1", null);
    backend.Aircraft["A1"] = new AircraftDto("A1", "#FF0000", true,
        new[] { new WaypointDto(10, 20, 1500, 100) });

    var vm = new AircraftViewModel(backend, "A1");
    vm.IsDirty.Should().BeFalse();

    vm.ColorHex = "#00FF00";
    vm.IsDirty.Should().BeTrue();
    vm.ApplyCommand.CanExecute(null).Should().BeTrue();

    vm.ApplyCommand.Execute(null);
    vm.IsDirty.Should().BeFalse();
    backend.Aircraft["A1"].ColorHex.Should().Be("#00FF00");
}
```

`FakeScenarioBackend` exposes the entity dictionaries (`Aircraft`, `Facility`, `AreaTargets`, `Sensors`, `Coverages`, `Foms`) as test-helper state. Seed them in tests; assert on them after VM operations.

## 4. Architectural conventions

### 4.1 The DTO boundary

`Sg.Domain.Contracts` is the only namespace view-models depend on for state shape. The `IScenarioBackend` interface is the only way to read or write scenario state.

**Don't:**
- Pass `IAgAircraft` (or any `IAgXxx`) out of `StkScenarioBackend`.
- Reference `AGI.*` from `Contracts/`, `ViewModels/`, `Interaction/`.
- Add new fields to a DTO without thinking about the JSON wire format (Mode B reads these as JSON).

**Do:**
- Return new DTOs from `StkScenarioBackend.GetXxx` (don't mutate; records are immutable).
- Add new typed entity DTOs as new files alongside existing ones (per §8 below).
- Keep `Sg.Domain.Contracts.*` files free of `using AGI.*` (the namespace fence test enforces this).

### 4.2 MVVM patterns

We use CommunityToolkit.Mvvm. Conventions:

```csharp
public partial class AircraftViewModel : EntityViewModelBase
{
    private readonly IScenarioBackend _backend;
    private readonly string _path;
    private AircraftDto _committed;

    [ObservableProperty] private string _colorHex;
    [ObservableProperty] private bool   _pathVisible;

    public ObservableCollection<WaypointModel> Waypoints { get; } = new();

    public AircraftViewModel(IScenarioBackend backend, string path)
    {
        _backend = backend;
        _path    = path;
        _committed = backend.GetAircraft(path);     // load on construct

        _colorHex    = _committed.ColorHex;          // set the backing fields
        _pathVisible = _committed.PathVisible;       // (not properties, to
                                                     //  skip MarkDirty)
        foreach (var wp in _committed.Waypoints)
            Waypoints.Add(...);

        Waypoints.CollectionChanged += (_, _) => MarkDirty();
        PropertyChanged += (_, e) => {
            if (e.PropertyName is nameof(ColorHex) or nameof(PathVisible))
                MarkDirty();
        };
    }

    protected override void DoApply()
    {
        var dto = new AircraftDto(_committed.Name, ColorHex, PathVisible,
            Waypoints.Select(w => new WaypointDto(...)).ToList());
        _backend.UpdateAircraft(_path, dto);
        _committed = dto;
    }

    protected override void DoReset() { /* reset observables to _committed */ }

    public override void Refresh()
    {
        if (_backend.FindByPath(_path) is null) return;
        _committed = _backend.GetAircraft(_path);
        DoReset();
        IsDirty = false;
    }
}
```

Key points:
- Use field-backed `[ObservableProperty]` with the private leading-underscore name.
- Constructor sets backing fields (not properties) so MarkDirty doesn't fire from initial load.
- Hook `Waypoints.CollectionChanged` for collections; hook `PropertyChanged` for scalars.
- Override `DoApply`, `DoReset`, `Refresh` from `EntityViewModelBase`.

### 4.3 Backend access

```csharp
// Inside a view-model, controller, or other consumer:
private readonly IScenarioBackend _backend;

// Read:
var dto = _backend.GetAircraft(path);   // throws if not found

// Write:
_backend.UpdateAircraft(path, newDto);

// Subscribe to changes:
_backend.ScenarioChanged += (_, _) => { /* refresh, etc. */ };
```

`StkScenarioBackend` raises `ScenarioChanged` after every mutation. `ObjectTreeViewModel` subscribes to refresh the tree (with path-set short-circuit per ADR-013-adjacent). `PropertyPanelHostViewModel` subscribes to refresh the active VM during edit mode (with `EditingPath` gate).

### 4.4 Dependency injection

`Sg.App.App.RegisterBackend(services, mode)` wires `IScenarioBackend`:

- `mode == "InProcess"` → registers `StkScenarioBackend` as singleton, exposed via `IScenarioBackend`. This is the production path.
- `mode == "Remote"` → throws `NotImplementedException` (reserved for Mode B's HTTP backend).

`MVP4_BACKEND` env var picks the mode at startup; default is `InProcess`. For unit tests, inject `FakeScenarioBackend` directly without going through `RegisterBackend`.

## 5. STK COM realities (read before touching `StkScenarioBackend`)

These are the gotchas accumulated through MVP4 + MVP4.5. The full reference is in the [v2 archive §25.3.3 + §25.3.5](specs/v2-tech-stack-archive.md). Distilled here for quick reference. Each entry is a real bug we hit.

### 5.1 Bootstrap

- `AgSTKXApplication` must be constructed **before** any `AgStkObjectRoot`. `StkScenarioBackend.Initialize()` does this; do not bypass.
- STA threading is mandatory. Tests must be `[Apartment(STA)]`. Production WPF is STA via `[STAThread]` on the entrypoint.
- `Application.EnableVisualStyles()` + `SetCompatibleTextRenderingDefault(false)` are required even for WPF (we use `WindowsFormsHost`).
- Don't construct `AgSTKXApplication` twice — it's a process singleton; second construction is wasted cold-start time.

### 5.2 PIA quirks

- Use `((IAgStkObject)entity).InstanceName`, not `entity.InstanceName`. The typed PIA interfaces don't expose `InstanceName` directly; only `IAgStkObject` does.
- Aircraft graphics: must call `_obj.Graphics.SetAttributesType(AgEVeGfxAttributes.eAttributesBasic)` BEFORE casting to `IAgVeGfxAttributesBasic` to set color. Without it, the cast silently fails.
- Aircraft path visibility: property is `IsObjectGraphicsVisible`, not `Show`.
- Facility position: use `IAgFacility.Position.AssignGeodetic(lat, lon, alt)` directly. The `ConvertTo(ePlanetodetic) + Assign(geo)` snapshot pattern silently no-ops on this STK build (see ADR-014).
- Use `IAgPlanetodetic`, not `IAgGeodetic`, for facilities.
- `IAgVeWaypointsElement.Latitude` / `.Longitude` are `object` (COM VARIANT); cast to `double` explicitly.
- Time format: `"dd MMM yyyy HH:mm:ss.fff"` with `CultureInfo.InvariantCulture`. Non-English Windows installs fail without InvariantCulture.
- Pin `Root.UnitPreferences.SetCurrentUnit("DateFormat", "UTCG")` after `NewScenario`.
- `_IAgCoverageDefinition` (underscore-prefixed) is the hidden dispatch interface; use the public `IAgCoverageDefinition` for casts and field declarations.

### 5.3 Editing lifecycle

- `OnObjectEditingStop(false)` **commits** the edit; `(true)` cancels. The boolean is `revert`, not `apply`. **Easy to invert; don't.**
- `OnObjectEditingApply` and `OnObjectEditingStop` events fire only from programmatic `ApplyObjectEditing()` / `StopObjectEditing()` calls — **not from user mouse interaction on drag handles**. We bridge user drag-and-release to `ApplyObjectEditing()` via a `MouseUpEvent` subscription during `EditingEntity` mode (per ADR-015).
- `StartObjectEditing` requires an explicit `_globe3D.Refresh()` afterwards for the handles to actually paint. STK has them internally but `WindowsFormsHost` doesn't repaint until next pan.
- Use full STK registry path for `StartObjectEditing`: `/Application/STK/Scenario/{name}/Aircraft/A1`. The `*/Aircraft/A1` shorthand silently fails to start.

### 5.4 Event subscription discipline (load-bearing for perf)

- **Zero permanent subscriptions** on `_globe3D.MouseDownEvent` / `MouseMoveEvent` / `OnObjectEditing*` / `DblClick`. Each permanent subscription costs ~1–2 s of post-pan-release stall (not the handler body — the COM dispatch).
- Subscribe only when entering placement / editing mode; unsubscribe on return to Idle. See `_ensurePlacementEventsSubscribed` and `_ensureEditingEventsSubscribed` in `StkDisplayHost.xaml.cs`.
- STK's `DblClick` event fires on every single click (not just real double-clicks). Don't subscribe; detect real DCs from `MouseDownEvent` timestamp deltas if needed.
- Right-click is consumed by STK for camera ops; never reaches subscribers as `button == 2`. (This is an STK COM constraint — your finalize gesture has to be something other than right-click. MVP4.5 settled on `Enter` as the pragmatic choice; v2 hardening should review the broader UX per [ADR-016](decision-record.md).)

### 5.5 Rendering

- `manager.Scenes[0].Render()` is required after primitive updates for interactive previews (rubber-band line during placement). STK doesn't auto-render on `SetCartographic` mutation.
- `IAgStkGraphicsPolylinePrimitive.SetCartographic` expects **radians** for lat/lon (PolygonDrawing sample reference). Convert from `PickInfo`'s degrees.
- Don't subscribe `ScenarioChanged → _globe3D.Refresh()`. Causes deadlock under load (4+ Refresh calls inside one FinalizeAircraft). Refresh explicitly only after `StartObjectEditing` per ADR-008's invariants.

### 5.6 Cleanup

- Don't call `Marshal.ReleaseComObject` on a COM RCW before STK has finished with it (race condition with `Children.Unload`). Let GC handle the RCW lifetime; the samples do.
- App shutdown: STK COM teardown can hang on in-flight calculations. The `App.OnExit` 5-second daemon thread does `Environment.Exit(0)` if graceful disposal stalls. Don't remove this watchdog.

## 6. Implementation invariants

§6.1 through §6.3 are **load-bearing** for the architecture's promises — losing any one of them empirically broke MVP4.5's deliverable. Listed in the Decision Record as [ADR-013](decision-record.md), [ADR-014](decision-record.md), [ADR-015](decision-record.md).

§6.4 is a **UX-fidelity principle** plus MVP4.5's pragmatic gesture choices ([ADR-016](decision-record.md)) — the principle is load-bearing; the specific gestures are open for revision in v2 hardening.

### 6.1 No permanent COM event subscriptions

- `StkDisplayHost.xaml.cs` subscribes mouse / editing events through `_ensurePlacementEventsSubscribed` / `_ensureEditingEventsSubscribed` only when entering the corresponding mode.
- Unsubscribe on mode exit. **Reviewable in code review** — any `_globe3D.SomeEvent += ...` outside an `_ensure*Subscribed` method is suspect.
- The on-demand pattern uses typed delegate fields stored once in the constructor so `+=` and `-=` reference the same instance. See the existing `_onGlobe3DMouseDown` etc.

### 6.2 `Position.AssignGeodetic` for facilities

- `StkScenarioBackend.UpdateFacility` uses the direct `AssignGeodetic(lat, lon, alt)` call — **not** `ConvertTo + Assign`.
- If a future STK version regresses on `AssignGeodetic`, fall back to a Connect command (`SetPosition */Facility/F1 Geodetic ...`); both are documented.

### 6.3 `ApplyObjectEditing` on user MouseUp

- During `EditingEntity` mode, `StkDisplayHost` subscribes `_globe3D.MouseUpEvent` and calls `_globe3D.ApplyObjectEditing()` on every release.
- This commits STK's drag to COM and fires `OnObjectEditingApply`, which our handler hooks for `ScenarioChanged`.
- **Without this, `GetAircraft`/`GetFacility` after a drag returns the old state and Apply pushes the old values, undoing the drag.**

### 6.4 Match STK Desktop UX where possible (MVP4.5 currently uses keyboard finalize)

**Principle (load-bearing):** the placement / edit experience should mimic STK Desktop's gestures and discoverability as closely as the embedded ActiveX environment permits.

**MVP4.5's current implementation** (open for revision in v2 hardening — see [ADR-016](decision-record.md)):

- `MainWindow.OnPreviewKeyDown` (window-level) handles Enter and Esc.
- Enter routes to `controller.FinalizePlacement()` during placement, `controller.ApplyEdit()` during editing.
- Esc routes to `controller.Cancel()`.

**Why MVP4.5 uses keyboard:** two STK ActiveX constraints (see §5.4) make right-click and `DblClick` unreachable for finalize. With both standard gestures gone, keyboard at the Window level was the most reliable focus-agnostic gesture. **This was MVP4.5's pragmatic choice given the constraints, not the target UX.**

**v2 hardening should:** review STK Desktop's actual placement / editing UX (drawing tools, "Insert by …" flows) and bring the v2 implementation into closer alignment where the constraints allow. Toolbar buttons are a strong candidate as a complement to keyboard gestures (always-reachable, regardless of focus).
- Right-click and STK's `DblClick` event are unreliable here; don't add UI relying on them.

## 7. Common debugging scenarios

### 7.1 "Pan is sluggish"

1. Confirm the binary is **Release**, not Debug. Debug noticeably slower regardless of code.
2. Confirm `Sg.App.exe` has dedicated GPU preference set (Settings → System → Display → Graphics).
3. Verify no stray permanent event subscription was added — `git diff` against `StkDisplayHost.xaml.cs` for any `+=` outside `_ensure*Subscribed`.
4. Enable diagnostic logging (`set MVP4_DIAG=1`) and look for `ScenarioChanged → refreshing views` in the log; permanent view-refresh subscription regressions show up there.

### 7.2 "Drag handles don't work"

1. Confirm `OnObjectEditingStart` log line fires when entering edit mode (with `MVP4_DIAG=1`).
2. If it fires but handles don't paint, verify `_globe3D.Refresh()` is called immediately after `StartObjectEditing` (the explicit-Refresh fix per ADR-015).
3. If handles paint but drag doesn't commit to COM, verify the `MouseUpEvent` subscription during edit mode is calling `ApplyObjectEditing()`.
4. Verify the entity's STK path is `/Application/STK/Scenario/{name}/Aircraft/A1` form, not `*/Aircraft/A1`.

### 7.3 "Placement clicks don't register"

1. Diag log at `MouseDown ENTER` should appear with the screen coords. If not, `MouseDownEvent` isn't subscribed.
2. Diag log at `MouseDown ... lat=... lon=... valid=...` shows `PickInfo` result. If `valid=False`, STK's pick is failing (off-globe click, or scenario without a globe — check SceneManager init).
3. If diag shows valid lat/lon but no waypoint added: the `InteractionController.Mode` may not be `PlacingX`. Check the mode transition log line.

### 7.4 "Facility appears at lat 40, lon -75 instead of where I clicked"

That's AGI's HQ in Exton, PA — STK's default facility position. `UpdateFacility` is silent-no-oping. Check that the `AssignGeodetic` call is being used (per ADR-014). The `ConvertTo + Assign` pattern silently fails on this STK build.

### 7.5 "Property panel goes blank during edit"

`ObjectTreeViewModel.RebuildFromService` may be running on a `ScenarioChanged` and clearing `SelectedNode`. Verify the path-set-unchanged short-circuit is in place. Drag-edits should not rebuild the tree.

### 7.6 "Apply button stays grey after drag-edit"

Two parts must both work:
1. `MouseUp` during edit calls `ApplyObjectEditing()` (commits the drag).
2. `OnObjectEditingApply` raises `ScenarioChanged`; `PropertyPanelHostViewModel.ScenarioChanged` handler calls `vm.Refresh()` then `vm.MarkDirty()` so Apply enables.

If either is missing, Apply stays grey. Diag log shows both events firing.

### 7.7 "Tests pass but the app crashes at startup"

Most likely: STK licence unavailable or STK not registered. Check `C:\EWTSS\mvp4\logs\mvp4-<today>.log` for the actual exception. The startup error message in the splash window is intentional fail-fast (per ADR-017 — no runtime mock fallback).

## 8. How to add a new STK entity type

Per ADR-007 we extend the typed pattern, not the metadata-driven path. Forward catalogue: Transmitter, Receiver, Antenna; maybe Satellite.

Reference implementation: `AircraftDto` + everything around it. Use git to find the files touched when Aircraft was added; mirror exactly.

### 8.1 Files to add / touch

For a new entity type `Foo`:

1. **`Sg.Domain.Contracts/FooDto.cs`** — pure record with the entity's fields.
2. **`Sg.Domain.Contracts.IScenarioBackend.cs`** — add `FooDto GetFoo(string path);` and `void UpdateFoo(string path, FooDto dto);`.
3. **`Sg.Domain.Stk.StkScenarioBackend.cs`** — implement `GetFoo` and `UpdateFoo`. Use `((IAgStkObject)foo).InstanceName` patterns. Set unit preferences before reading/writing values that have unit semantics (e.g. `Frequency` in MHz). For Transmitter/Receiver, the parent path matters — reference repo's emitter code in the v2 archive shows the Facility → Sensor → Transmitter hierarchy for typical EW scenarios.
4. **`Sg.Domain.ViewModels/FooViewModel.cs`** — extends `EntityViewModelBase`. Bind DTO fields to observable properties. Implement `DoApply`, `DoReset`, `Refresh`.
5. **`Sg.App.Views.Entities/FooPanel.xaml`** + `.cs` — WPF UI for the entity. Mimic `AircraftPanel.xaml`'s shape (DataGrid for collections, NumericUpDown for numerics, ComboBox for enums, ColorPicker for color).
6. **`Sg.Domain.ViewModels.PropertyPanelHostViewModel.PanelFactory.Create`** — add a switch case mapping `EntityKind.Foo` to `new FooViewModel(...)`.
7. **`Sg.Domain.Models.EntityKind`** — add the enum value.
8. **`Sg.Tests.Fakes.FakeScenarioBackend`** — add a `Dictionary<string, FooDto> Foos { get; }` and implement `GetFoo` / `UpdateFoo` against it.
9. **`Sg.Tests.Contracts.DtoJsonRoundTripTests`** — add a round-trip test for `FooDto`.
10. **`Sg.Tests.ViewModels/FooViewModelTests.cs`** — at least: load-on-construct, edit-marks-dirty, apply-clears-dirty, refresh-from-backend.

### 8.2 Map-driven authoring (only if the entity is map-placed)

If the entity supports click-to-place on the globe (Aircraft / Facility / AreaTarget pattern):

11. **`Sg.Domain.Interaction.InteractionController`** — add a `BeginPlace(Foo)` mode and a `FinalizeFoo` method. Mirror `FinalizeAircraft` for routes / `FinalizeAreaTarget` for vertices.
12. **`Sg.App.Views.Shell.ObjectTreeView` toolbar** — add a `+ Foo` button.

If the entity is added via dialog or panel only (Sensor / Coverage / FOM pattern): skip 11–12; add a tree-context-menu entry instead.

### 8.3 Drag-edit on globe (only if entity has visible position to drag)

If supported (Aircraft / Facility / AreaTarget):

13. **`StkDisplayHost._buildStkPath`** — add the `EntityKind.Foo => "Foo"` case so `StartObjectEditing` builds the right STK registry path.

### 8.4 Estimated effort

For Tx / Rx / Antenna with well-understood STK COM patterns from the reference repo: ~1.5 days each, including tests. First time may take 3 days while you learn the entity's STK quirks.

## 9. How to add a new hardware variant (telemetry phase)

> **[Telemetry phase, not yet built.]** This section is the design-time procedure that the v2 hardening phase implements. Once `drs-bridge` ships, treat this as the canonical add-a-variant runbook.

The architectural commitment is that **a new hardware variant = one YAML profile + one C++ parser source file.** No `drs-bridge` Python changes, no `drs-server` changes, no schema changes — provided the variant fits the existing data-class taxonomy (health / FF / FH / burst / NMEA / radar-track). If you find yourself editing `drs-server` consumers or hypertable schemas, stop and reconsider whether the variant really needs a new data class or whether it fits an existing one.

### 9.1 Step-by-step

1. **Start from the reference template** — copy [`drs-bridge/parsers/reference/`](../../drs-bridge/parsers/reference/) to `drs-bridge/parsers/<variant>/`. The reference ships a buildable CMake project with the 4-symbol C ABI, a synthetic frame format, and a pytest integration test that exercises the ctypes binding end-to-end (locally when `cmake` is on PATH; the parked CI workflow at [`.github/disabled/ci.yml`](../../.github/disabled/ci.yml) will exercise it on every push once hosted runners are re-enabled). See §9.3 for the rename + per-step modification walkthrough.
2. **Write the YAML profile** — `drs-bridge/src/drs_bridge/profiles/<variant>.yaml`. See §9.2 for the schema; `parser_lib` should point at the shared library produced by the variant's CMake build (e.g. `../../parsers/<variant>/build/<variant>_parser.dll`).
3. **Generate the skeleton if you have an ICD Excel** — run `tools/icd_codegen` (see [`specs/icd-codegen-tool-design.md`](specs/icd-codegen-tool-design.md)). This produces the constants header + dispatch skeleton; you fill in the field-decode bodies on top of the reference parser's structure.
4. **Build** — from `drs-bridge/parsers/<variant>/`: `cmake -S . -B build && cmake --build build --config Release`. Produces `build/<variant>_parser.dll` (Windows) or `build/<variant>_parser.so` (Linux). CI commits the pre-built `.dll` to `packages/parsers/` so the DVD install does not require a C++ toolchain at the customer site.
5. **Add the Kafka topic** — `hw.<variant>.<kind>` and `entity.<variant>.response` (the latter only if Integrated mode is in use). Topic creation goes into the deployment scripts.
6. **Add the TimescaleDB row mapping** — only if the variant introduces a new message kind. Most variants reuse the existing `measurements` hypertable + `measurement_scalars` (see §10).
7. **Restart `drs-bridge`** — `systemctl restart drs-bridge` on WS2. The supervisor picks up the new YAML profile and starts a TCP server on the declared port.
8. **Verify** — connect a hardware device or simulator; confirm Kafka messages on the topic; confirm rows appear in the hypertable.

### 9.2 YAML profile schema

```yaml
# profiles/<variant>.yaml
name: <variant>                       # e.g. "rdfs", "comm_df"
port: 5485                            # TCP port the device connects to
kafka_topic: <variant>.drs.ui         # base Kafka topic; .cmd / .response variants derived
kafka_broker: "${KAFKA_BROKER}"
parser_lib: parsers/lib<variant>.dll  # relative to drs-bridge install root
max_connections: 20                   # concurrent device connections accepted
health_interval_ms: 1000              # health-status publish cadence
receive_buffer_bytes: 65536           # size to ICD max response size
frame_terminator: "magic_bytes"       # always "magic_bytes" for binary protocols
protocols: [sdfc_drs, scd_drs]        # which frame formats this device speaks
protocol_version: "IRS-RDFS-v2"       # for Kafka message metadata
```

`protocols` declares which frame formats the hardware speaks:

| Value | Meaning |
|---|---|
| `sdfc_drs` | Main 4-byte-header command/response format. CMD: `0xAA 0xAB 0xBA 0xBB`. RESP: `0xEE 0xEF 0xFE 0xFF`. |
| `scd_drs` | Compact 2-byte-header format. Frame headers: `0xAA 0xAA` / `0xEE 0xEE`. |
| `nmea` | ASCII NMEA sentences (GNSS only). |

The C++ parser reads this flag at compile time via a CMake define, so unused frame parsers are excluded from the shared library.

### 9.3 C++ parser interface contract

Every variant parser exports four C-linkage symbols. The canonical
reference implementation lives at
[`drs-bridge/parsers/reference/`](../../drs-bridge/parsers/reference/);
it builds out of the box via CMake on both Windows and Linux and is
exercised end-to-end by `drs-bridge/tests/test_reference_parser_integration.py`
(which the parked CI workflow at [`.github/disabled/ci.yml`](../../.github/disabled/ci.yml)
will run on every push once hosted runners are re-enabled — see
[`.github/disabled/README.md`](../../.github/disabled/README.md)).

**Symbols** (declared in `include/reference_parser.h`):

| Symbol | Purpose |
|---|---|
| `extract_frame(buf, length, out_frame, out_len) → int` | Scan input for a complete frame. On success allocate `*out_frame` + set `*out_len` and return 0. Return −1 if no complete frame is present. |
| `parse_message(frame, frame_len, out_json, out_len) → int` | Decode a frame (the one returned by `extract_frame`) into a heap-allocated JSON string. |
| `format_response(kind, kwargs_json, out_buf, out_len) → int` | Emit a frame for the named response kind, given a JSON object of arguments. |
| `free_result(ptr) → void` | Free anything returned via an out-pointer above. Safe to call with NULL. |

The `PARSER_EXPORT` macro in the header handles `__declspec(dllexport)`
on MSVC and `__attribute__((visibility("default")))` on GCC/Clang. The
`extern "C"` guards prevent C++ name mangling so ctypes resolves the
symbols.

**To onboard a new variant:**

1. **Copy the reference directory**:
   ```bash
   cp -r drs-bridge/parsers/reference/ drs-bridge/parsers/<variant>/
   ```
   On Windows PowerShell: `Copy-Item -Recurse drs-bridge/parsers/reference drs-bridge/parsers/<variant>`.

2. **Rename the source files**:
   ```bash
   cd drs-bridge/parsers/<variant>
   git mv include/reference_parser.h include/<variant>_parser.h
   git mv src/reference_parser.cpp src/<variant>_parser.cpp
   ```
   Adjust the `#include` line in the `.cpp` to reference the new header name.

3. **Update `CMakeLists.txt`**:
   - Change `project(reference_parser ...)` to `project(<variant>_parser ...)`.
   - Change the `add_library(reference_parser SHARED ...)` target name to `<variant>_parser`.
   - Update the source list to point at `src/<variant>_parser.cpp`.

4. **Replace the synthetic frame format** in `src/<variant>_parser.cpp`
   with your IRS layout. The patterns the reference demonstrates are
   idiomatic — keep the structure, fill in the bodies:
   - `extract_frame`: scan for the start-of-frame byte(s); read the
     length field per your IRS encoding; copy the frame bytes. The
     reference uses a 1-byte magic + 1-byte length; real variants
     typically need a 2- or 4-byte length field, optional CRC, etc.
   - `parse_message`: dispatch on the message type ID from the
     frame header; decode per-message-type bodies into JSON. The
     reference handles exactly one payload kind (6-byte time
     payload); your variant will have many.
   - `format_response`: dispatch on the `kind` string; per-kind, parse
     the relevant fields from `kwargs_json` and emit the frame bytes.
   - `free_result`: usually unchanged from the reference (just `free()`).

5. **Add a YAML profile** under `drs-bridge/src/drs_bridge/profiles/<variant>.yaml`
   with `parser_lib` pointing at the built shared library. Path is
   resolved relative to `DRS_BRIDGE_PROFILES_DIR` at runtime, so use
   a path like `parser_lib: ../../parsers/<variant>/build/<variant>_parser.dll`
   (or `.so` on Linux) — verify with a path-existence test before
   committing.

6. **Build + smoke test**:
   ```bash
   cd drs-bridge/parsers/<variant>
   cmake -S . -B build
   cmake --build build --config Release

   cd ../../..  # back to drs-bridge/
   .\.venv\Scripts\pytest.exe tests/profiles/test_profile_yaml.py -v
   ```
   The YAML round-trip test confirms your profile parses against the
   shared schema. End-to-end test with a real device is a separate
   per-variant exercise.

**Reference parser invariants** that your variant should preserve:

- All exported symbols use `extern "C"` so ctypes finds them without
  name mangling.
- `PARSER_EXPORT` macro handles platform-specific export markers.
- Every return-via-out-pointer is heap-allocated with `malloc`; the
  caller is documented as responsible for calling `free_result`.
- Frame buffers are size-bounded per IRS spec. The reference's 1-byte
  length field limits payload to ≤253 bytes; real IRS frames frequently
  need 2- or 4-byte length encoding. Adjust both the encoding side
  (`format_response`) and the decoding side (`extract_frame`) symmetrically.
- The Python wrapper in `drs-bridge/src/drs_bridge/parser_loader.py`
  already declares matching `argtypes`/`restype`. Variants don't need
  to modify the Python side as long as their C ABI exactly matches the
  4-symbol contract.

**Frame-scan caveat the reference doesn't fully handle:** the
reference's `extract_frame` returns −1 on the *first* magic byte it
finds if the declared length exceeds the buffer. Real variants with
noisy lines may need to advance past a bad magic-byte candidate and
keep scanning forward. The reference template's pattern is correct for
clean-line IRSes; production variants with noise tolerance need a
slightly different loop structure.

### 9.4 Python ctypes dispatcher (no changes needed for new variants)

The dispatcher is generic — it loads any parser that conforms to the interface. New variants do not edit this file.

```python
class ParserHandle:
    def __init__(self, lib_path: str):
        self.lib = ctypes.CDLL(lib_path)
        self.lib.extract_frame.restype  = ctypes.c_int
        self.lib.parse_message.restype  = ctypes.c_char_p
        self.lib.parse_message.argtypes = [
            ctypes.c_char_p, ctypes.c_int, ctypes.c_int    # frame, len, frame_type
        ]
        self.lib.format_response.restype  = ctypes.c_int
        self.lib.format_response.argtypes = [ctypes.c_char_p, ctypes.c_void_p]

    def extract(self, buf: bytes) -> tuple[bytes, int] | None:
        out_frame = ctypes.create_string_buffer(1048576 + 64)
        out_len   = ctypes.c_int(0)
        ftype = self.lib.extract_frame(buf, len(buf), out_frame, ctypes.byref(out_len))
        if ftype <= 0:
            return None
        return bytes(out_frame[:out_len.value]), ftype

    def parse(self, frame: bytes, frame_type: int) -> dict:
        result = self.lib.parse_message(frame, len(frame), frame_type)
        data   = json.loads(result)
        self.lib.free_result(result)
        return data

    def format_response(self, response_json: str, out_buf: ctypes.Array) -> int:
        return self.lib.format_response(
            response_json.encode("utf-8"),
            out_buf
        )
```

### 9.5 Migration from the legacy `command.csv` / `structure.csv` schema

The pre-v2 `drs_bridge` codebase used per-variant `command.csv` + `structure.csv` files — Python literals embedded in CSV cells, parsed at startup with `ast.literal_eval`. v2 replaces this with compiled C++ parsers. For each existing variant (SRX, MRX, GNSS, JSVUSHF variants):

1. Feed the variant's ICD Excel (or the existing `command.csv` if no ICD is at hand) into `tools/icd_codegen` (see the spec referenced in §9.1 step 3). The tool produces the constants header + dispatch skeleton.
2. Review the generated C++; constants and dispatch structure are auto-filled.
3. Hand-write the field-decode bodies (the parts requiring judgement: bit-packing, conditional layouts, enumeration semantics).
4. Delete the legacy `command.csv`, `structure.csv`, `parse_request_param.py`, and `random_value_generator.py` for that variant — the C++ parser absorbs all four responsibilities.

The four fragility points of the legacy approach (silent `ast.literal_eval` failures, no TCP stream reassembly, inline magic bytes, 50-branch if-elif dispatch) are all eliminated by the C++-parser approach.

## 10. Database schema reference

> **[Telemetry phase — schema lives on WS2; today consumed only by `Sg.App` for `computed_links` writes.]**

The full PostgreSQL 16 + TimescaleDB 2.x schema for EWTSS v2. Init scripts will live at `infrastructure/timescaledb/init/`. The schema separates into three logical domains: **scenario planning** (written by the scenario publisher — `Sg.App` today, `Sg.Server` future), **time-series telemetry** (written by `drs-server`), and **system** (RBAC, logs, hardware profiles — written by `drs-server`).

### 10.1 Scenario planning schema (scenario publisher writes)

```sql
exercises
  id, name, description, created_by, created_at, status
  -- status: draft | computed | ready | executing | complete

gaming_areas                         -- Area of Operation (AOP)
  id, exercise_id → exercises
  name, boundary_geojson, feba_geojson

entities                             -- Blue/Red line platforms deployed on map
  id, exercise_id → exercises
  name, type (RDFS|JVUHF|JHF|SJRR|JLB|JMB|JHB|AUS|PADS|...)
  side (blue|red), role
  position_lat, position_lon, position_alt
  is_mobile (bool)

entity_motion_profiles               -- time-dynamic movement for mobile entities
  id, entity_id → entities
  time_offset_sec, lat, lon, alt, speed_mps, heading_deg

emitters                             -- Red line emitters
  id, exercise_id → exercises
  name, type (FF|FH|Burst|RADAR|COM|SJRR|AUS|PADS)
  lat, lon, alt, is_mobile
  active_from_sec, active_to_sec

emitter_motion_profiles
  id, emitter_id → emitters
  time_offset_sec, lat, lon, alt, speed_mps, heading_deg

emitter_parameters                   -- type-specific RF parameters (key/value)
  id, emitter_id → emitters
  param_key (varchar), param_value (jsonb)
  -- e.g. key='frequency_hz', value={"min": 30e6, "max": 88e6}
  -- new emitter types require no schema change

antenna_profiles
  id, emitter_id → emitters
  pattern_type, azimuth_beamwidth_deg, elevation_beamwidth_deg
  scan_type (raster|sector|fixed), scan_rate_dps
  gain_pattern_json (jsonb)

environments
  id, exercise_id → exercises
  time_offset_sec, rainfall_mm_hr, fog_visibility_m
  temperature_c, humidity_pct, atmospheric_model

computed_links                       -- STK link-analysis output; TimescaleDB hypertable
  id, exercise_id → exercises
  computed_at (timestamptz)          -- partition key: exercise_start + tick_offset
  tick_sec (integer)                 -- seconds from exercise start (for scrubbing)
  entity_id → entities
  emitter_id → emitters
  range_m, azimuth_deg, elevation_deg
  doppler_hz, signal_strength_dbm
  path_loss_db, link_margin_db
  is_visible (bool)

emitter_library                      -- reusable emitter templates
  id, name, category, parameters_json (jsonb)
  created_by, is_system (bool)
```

### 10.2 Time-series measurement schema (`drs-server` writes)

Replaces the current production system's per-variant flat tables (FF, FH, Burst, RadarTrackReport × N variants). One generic model supports any hardware type without schema changes — the variant's distinguishing fields land in `measurement_scalars`, while the full parsed message lives in `measurements.payload` (jsonb).

```sql
drs_sessions
  id, exercise_id → exercises (nullable)
  hardware_type (varchar: srx|mrx|gnss|jvuhf|jhf|rdfs|...)
  instance_id (varchar: "RDFS-01", "JHF-02")
  started_at, ended_at

measurements                         -- TimescaleDB hypertable on recorded_at
  id (bigint)
  session_id → drs_sessions
  recorded_at (timestamptz)          -- partition key
  hardware_type (varchar)
  message_type (varchar: FF|FH|Burst|Health|Version|NMEA|...)
  group_id (integer)
  command_id (integer)
  payload (jsonb)                    -- full parsed message

measurement_scalars                  -- hot columns for fast indexed queries
  measurement_id → measurements
  frequency_hz (double)
  power_dbm (double)
  azimuth_deg (double)
  elevation_deg (double)
  -- composite index: (session_id, recorded_at, message_type)
```

### 10.3 System schema (shared)

```sql
users
  id, username, password_hash, first_name, last_name, role_id → roles

roles
  id, name (admin|operator|observer)

features
  id, name, description

role_features
  role_id → roles, feature_id → features

system_logs                          -- TimescaleDB hypertable on recorded_at
  id, recorded_at, level, source, message, user_id → users (nullable)

hardware_profiles                    -- replaces hardcoded constants files
  id, hardware_type (varchar)
  port (integer)
  kafka_topic (varchar)
  parser_lib (varchar)
  protocol_version (varchar)
  config_json (jsonb)
  is_active (bool)

ip_configurations
  id, hardware_type, instance_id
  ip_address, port, updated_at, updated_by → users
```

### 10.4 TimescaleDB hypertables

| Table | Partition key | Chunk interval | Retention policy |
|---|---|---|---|
| `computed_links` | `computed_at` (timestamptz) | Per exercise | Manual purge — exercises are long-lived |
| `measurements` | `recorded_at` | 1 day | Configurable per deployment (default 90 days) |
| `system_logs` | `recorded_at` | 7 days | 90-day default |

### 10.5 Schema rules

- **One source of truth per row.** `entities.position_lat/lon/alt` is the static position; `entity_motion_profiles` adds time-dynamic motion. Don't duplicate the static position into a `time_offset_sec=0` row.
- **`emitter_parameters` is intentionally jsonb-keyed.** Adding a new emitter type with new RF parameters does not require a schema migration. New types = new keys; existing query patterns survive.
- **`measurements.payload` is the canonical full record.** `measurement_scalars` is a denormalised projection for indexed queries — never the source of truth. Backfilling or repairing a row means updating both.
- **Foreign keys to mutable rows use `ON DELETE CASCADE` only on motion profiles and parameters.** Deleting an `exercise` cascades down (gaming areas, entities, emitters, environments, computed_links). Deleting a `user` does NOT cascade — set `created_by` to a tombstone user.

## 11. Mode A scenario authoring — internal design

The Mode A WPF host (`Sg.App` + `Sg.Domain`) factors into four layers with strict dependency direction. The system-level layer diagram lives in [Architecture Overview §2.4](architecture-overview.md#24-software-architecture-layers); this section is the file-by-file rule book for engineers working inside it.

### 11.1 Layered model with file-level mapping

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  LAYER 4 — PRESENTATION (WPF + ActiveX hosting)                             │
│  Sg.App/MainWindow.xaml + .xaml.cs                                     │
│  Sg.App/Views/Shell/                                                   │
│      ObjectTreeView, PropertyPanelHostView, StkDisplayHost, SplashWindow    │
│  Sg.App/Views/Entities/                                                │
│      AircraftPanel, FacilityPanel, AreaTargetPanel, SensorPanel,            │
│      CoveragePanel, FomPanel                                                │
│  Sg.App/Services/FileDialogService.cs    (WPF impl of IFileDialogService)│
│  Owns: XAML data binding, KeyDown routing, COM event subscription on the    │
│        ActiveX globe (on-demand only — see §6.1)                            │
│  Rule: No direct calls to StkScenarioBackend. Always go through view-models │
│        + IScenarioBackend.                                                   │
├─────────────────────────────────────────────────────────────────────────────┤
│  LAYER 3 — VIEW-MODELS (MVVM)                                               │
│  Sg.Domain/ViewModels/                                                 │
│      EntityViewModelBase    (IsDirty, Apply/Reset, Refresh contract)        │
│      AircraftViewModel, FacilityViewModel, AreaTargetViewModel,             │
│      SensorViewModel, CoverageDefinitionViewModel, FigureOfMeritViewModel   │
│      ObjectTreeViewModel, ObjectTreeNodeViewModel                           │
│      PropertyPanelHostViewModel, EmptySelectionViewModel                    │
│      MainWindowViewModel                                                    │
│  Owns: Bind DTOs to UI; mark dirty on user edit; commit via Apply.          │
│  Rule: Constructor reads via _backend.GetXxx; Apply writes via              │
│        _backend.UpdateXxx. No COM types. No XAML refs.                       │
├─────────────────────────────────────────────────────────────────────────────┤
│  LAYER 2 — DOMAIN (DTOs + the IScenarioBackend interface + state machine)   │
│  Sg.Domain/Contracts/                                                  │
│      AircraftDto, FacilityDto, AreaTargetDto, SensorDto,                    │
│      CoverageDefinitionDto, FigureOfMeritDto, EntityNodeDto,                │
│      ComputeResultDto, WaypointDto, VertexDto                               │
│      IScenarioBackend.cs                  (the contract — every backend op) │
│  Sg.Domain/Models/                                                     │
│      EntityKind, PointingMode, FomKind, FomStatistic, WaypointModel,        │
│      VertexModel                                                             │
│  Sg.Domain/Interaction/                                                │
│      IInteractionController, InteractionController, InteractionMode         │
│  Sg.Domain/Services/                                                   │
│      IFileDialogService                  (interface only; impl in App layer)│
│  Owns: DTOs (immutable records); the backend interface; the placement /     │
│        edit state machine (mode transitions, pending-points accumulation,   │
│        FinalizePlacement / ApplyEdit / Cancel)                              │
│  Rule: NO `using AGI.*` here. The ContractsNamespaceFenceTests integration  │
│        test fails the build if a COM type leaks into Sg.Domain.        │
│        Contracts. JSON-serialisable by construction (load-bearing for       │
│        Mode B).                                                              │
├─────────────────────────────────────────────────────────────────────────────┤
│  LAYER 1 — INFRASTRUCTURE (the STK adapter — only here lives COM)           │
│  Sg.Domain/Stk/StkScenarioBackend.cs                                   │
│  Owns: AGI.STKObjects.Interop COM calls; AgSTKXApplication bootstrap;       │
│        Position.AssignGeodetic; ApplyObjectEditing on user MouseUp;         │
│        the ~14 documented STK COM gotchas (§5).                              │
│  Rule: All `using AGI.*` lives here, nowhere else. Internal-visible to      │
│        Sg.App and Sg.Tests for the Root accessor; never public.   │
├─────────────────────────────────────────────────────────────────────────────┤
│  LAYER 0 — PLATFORM                                                         │
│  STK 12 Engine (process-level dependency, ActiveX in-process via            │
│  WindowsFormsHost). Single STK seat per deployment (ADR-012).               │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 11.2 Module responsibilities

| Module | Layer | Does | Does not |
|---|---|---|---|
| `MainWindow.xaml.cs` | 4 | Window lifecycle; routes Enter / Esc through `OnPreviewKeyDown` to the InteractionController | Touch DTOs directly; bypass view-models |
| `StkDisplayHost.xaml.cs` | 4 | Host the STK ActiveX in `WindowsFormsHost`; subscribe / unsubscribe COM events on demand only (`_ensurePlacementEventsSubscribed`, `_ensureEditingEventsSubscribed`); call `_globe3D.ApplyObjectEditing()` on MouseUp during edit | Hold any permanent `+= Event` subscriptions on `_globe3D`; do PickInfo outside placement modes |
| `ObjectTreeView` / `ObjectTreeViewModel` | 4 / 3 | Render the entity tree; rebuild only when the path *set* changes (path-set short-circuit) | Rebuild on every `ScenarioChanged` (would clear `SelectedNode` mid-edit) |
| `PropertyPanelHostViewModel` | 3 | Swap the active entity panel based on tree selection; on `ScenarioChanged`, refresh the active VM and `MarkDirty()` only when `EditingPath` matches | Refresh blindly (would clobber unsaved user edits) |
| `EntityViewModelBase` + per-entity VMs | 3 | Load DTO on construct; observe edits; commit via `_backend.UpdateXxx` on Apply | Touch COM; touch XAML; touch other entity VMs |
| `InteractionController` | 2 | Placement / edit state machine: pending-points accumulation; `FinalizePlacement()`; mode transitions; `Cancel()` | Know about COM; know about WPF; know about the STK ActiveX surface |
| DTOs (`AircraftDto` etc.) | 2 | Carry scenario state across the boundary as immutable records | Reference COM types; carry behaviour |
| `IScenarioBackend` | 2 | Contract surface: NewScenario, AddEntity, GetXxx, UpdateXxx, ComputeAll, SaveAsSc / LoadSc | Imply transport (works equally over in-process method calls and HTTP) |
| `StkScenarioBackend` | 1 | Implement `IScenarioBackend` against AGI.STKObjects PIA; fire `ScenarioChanged` after every mutation | Leak COM types upward; cache state across calls (read-through to STK) |

### 11.3 The state machine (Layer 2 detail)

Placement / edit state lives in `InteractionController`, not in view-models or in mouse handlers. This is what keeps the controller unit-testable with a `FakeScenarioBackend` and zero STK seat:

```
              ┌─────────────────────┐
              │       Idle           │
              └──┬───────────────┬───┘
        BeginPlace(Aircraft / Facility / AreaTarget)
                 │              │
                 ▼              │
         ┌───────────────┐      │  StartEditingOnMap(path)
         │ PlacingX       │      ▼
         │  pending pts   │  ┌──────────────────┐
         │  accumulate on │  │ EditingEntity    │
         │  MouseDown     │  │  STK drag        │
         └──────┬─────────┘  │  handles active  │
                │            └──────┬───────────┘
       Enter:                       │   Enter: ApplyEdit()
       FinalizePlacement()          │   Esc:    Cancel()
       Esc: Cancel()                ▼
                ▼              ┌────────┐
              ┌───────┐        │  Idle  │
              │ Idle  │        └────────┘
              └───────┘
```

Concrete tests cover every transition in `InteractionControllerTests.cs`; STK is not required to validate the state machine.

### 11.4 Cross-layer events

The single backend event that crosses layers is `IScenarioBackend.ScenarioChanged`. Subscribers:

| Subscriber | Layer | Reaction |
|---|---|---|
| `ObjectTreeViewModel.RebuildFromService` | 3 | Rebuild tree IF the path set differs from the cached set; otherwise short-circuit (avoids clearing `SelectedNode` mid-edit) |
| `PropertyPanelHostViewModel.OnScenarioChanged` | 3 | Re-read the active entity DTO, call `Current.Refresh()`, then `Current.MarkDirty()` so the user-visible Apply enables; gated on `EditingPath` matching the displayed entity |
| Diagnostic logging (`MVP4_DIAG=1`) | 4 | Emit a tag line when the event fires; useful for tracing perf regressions |
| **No view subscribes globally to refresh** | — | Specifically forbidden. The legacy `ScenarioChanged → _globe3D.Refresh()` pattern caused 4-deep refresh-during-finalize freezes (see §5.5). Refresh is called only after specific operations (`StartObjectEditing`). |

### 11.5 STK isolation rule (no `ProcessPoolExecutor` needed)

In the v2 archive's Python sg-service design, STK was isolated in a separate process via `ProcessPoolExecutor(max_workers=1)` because Python's GIL + STK COM blocking would have stalled the FastAPI event loop. **In Mode A's C# host, no such isolation is needed:**

- C# COM is in-process and synchronous, but WPF is a desktop GUI — it has natural single-thread-per-window semantics, not an event-loop server.
- STK calls block the dispatcher thread when made directly. The InteractionController and view-models call them synchronously; the user is intentionally blocked until the STK call completes.
- Long-running operations (compute) display the splash window or a "computing" indicator. There is no event loop to keep responsive.
- For tests that need responsiveness without STK, `FakeScenarioBackend` is the substitute — it returns synchronously in microseconds.

If a future need emerges to offload long compute jobs (e.g. weeks-long batch link analysis), the natural extension is `Task.Run(() => _backend.ComputeXxx(...))` with `Sg.App` showing a progress dialog. No process-pool architecture needed.

### 11.6 Boundary tests (load-bearing)

These tests fail the build if the layering breaks; they're the structural guarantees that keep Mode B viable as an additive deliverable.

- `ContractsNamespaceFenceTests` — scans `Sg.Domain.Contracts.*` for any `using AGI.*` import; failure means a COM type has leaked into the Domain layer.
- `DtoJsonRoundTripTests` — every DTO serialises with `System.Text.Json` and deserialises back to an equal value. Failure means a DTO has been changed in a way that breaks the wire-format contract Mode B will use.
- `BackendModeTests.InProcess_mode_resolves_StkScenarioBackend` — DI mode resolution: `MVP4_BACKEND=InProcess` produces a `StkScenarioBackend`. Failure means the production wiring is broken.
- `InteractionControllerTests` — ~25 tests covering every state transition with `FakeScenarioBackend` only; failure means the placement / edit state machine has regressed.

When adding a new entity type, the round-trip test for its DTO is mandatory ([§8](#8-how-to-add-a-new-stk-entity-type) covers the full file checklist).

## 12. `drs-bridge` internal design

> **[Telemetry phase, design-time — implementation in the v2 hardening phase will follow this layered model.]**

`drs-bridge` is the most structurally complex of the WS2 services because it is bidirectional (commands flow in from SDFC, responses flow back out) and mode-sensitive (Random / Scenario / Integrated change where response data comes from). The four-layer design isolates I/O from logic from byte-handling.

### 12.1 Layered model

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  LAYER 4 — LIFECYCLE & SUPERVISION                                          │
│  supervisor.py, health_reporter.py, config.py                               │
│  Owns: start/stop profile listeners, health monitoring, profile reload      │
│         without process restart                                              │
│  Input:  YAML profile files on disk                                         │
│  Output: Running asyncio TCP servers (one per profile); health metrics      │
├─────────────────────────────────────────────────────────────────────────────┤
│  LAYER 3 — RESPONSE ROUTING (mode-aware)                                    │
│  response_router.py: ResponseRouter, SessionRegistry, RandomGenerator,      │
│                       ScenarioPublisherClient, HardwareRelay                │
│  Owns: Given a parsed command + profile, return a response JSON appropriate │
│        to the current operating mode                                         │
│  Modes: RANDOM     → RandomGenerator.generate()                             │
│         SCENARIO   → ScenarioPublisherClient.get_scenario_response()        │
│                       (HTTP to Sg.App or Sg.Server)               │
│         INTEGRATED → HardwareRelay.forward()  (TCP to real hardware)        │
│  Rule: No frame bytes here. No Kafka calls here.                            │
├─────────────────────────────────────────────────────────────────────────────┤
│  LAYER 2 — PARSE / ENCODE (C++ via ctypes)                                  │
│  frame_dispatcher.py: ParserHandle (wraps ctypes calls)                     │
│  C++ shared libs: extract_frame(), parse_message(), format_response()       │
│  Owns: frame detection, type tagging, JSON decode, JSON encode              │
│  Input:  Raw bytes buffer (from layer 1)                                    │
│  Output: (frame_bytes, frame_type) upstream; binary response frames down    │
│  Rule: No network I/O. No Kafka. Pure byte ↔ JSON transformation.          │
├─────────────────────────────────────────────────────────────────────────────┤
│  LAYER 1 — TRANSPORT (asyncio TCP)                                          │
│  tcp_server.py: handle_client() coroutine, asyncio.start_server()           │
│  kafka_producer.py: AIOKafkaProducer (shared, one per bridge process)       │
│  Owns: accept TCP connections; accumulate bytes; feed layer 2;              │
│         receive response frames from layer 2; write back to SDFC;           │
│         publish both sides to Kafka                                         │
│  Rule: No business logic. No mode switching. Only I/O orchestration.        │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 12.2 Module responsibilities

| Module | Layer | Does | Does not |
|---|---|---|---|
| `supervisor.py` | 4 | Loads YAML profiles, starts one TCP server per profile, restarts crashed servers with exponential backoff (1 s → 60 s cap) | Parse frames, make Kafka calls |
| `health_reporter.py` | 4 | Publishes health metrics to `<variant>.system.health` Kafka topic at `health_interval_ms` cadence | Manage connections |
| `response_router.py` | 3 | Dispatches to correct generator based on active session mode | Touch binary frames |
| `random_gen.py` | 3 | Generates plausible random values within `min`/`max` bounds from YAML profile | Know about Kafka or TCP |
| `scenario_publisher_client.py` | 3 | Async HTTP GET to scenario publisher's `/exercises/{id}/responses` endpoint | Cache responses locally |
| `frame_dispatcher.py` | 2 | ctypes wrapper for the four C++ symbols (§9.3) | Any I/O |
| `tcp_server.py` | 1 | Buffer accumulation, frame loop, Kafka produce, TCP write | Any logic beyond I/O |
| `kafka_producer.py` | 1 | Single shared `AIOKafkaProducer`, `linger_ms=5`, `compression_type="lz4"` | Consume |

### 12.3 The bidirectional TCP handler

`drs-bridge` is bidirectional: SDFC sends commands, the bridge parses, generates or routes responses, and writes response bytes back over the same connection. All traffic also flows to Kafka for recording.

```
SDFC  ──CMD──►  drs-bridge  ──parse──►  ResponseRouter  ──format──►  SDFC
                    │                          │
                    ▼                          ▼
                Kafka (cmd record)      Kafka (response record)
```

One generic coroutine handles all hardware types. Per-connection state is local to the coroutine — no shared mutable state between connections:

```python
async def handle_client(
    reader: asyncio.StreamReader,
    writer: asyncio.StreamWriter,
    profile: HardwareProfile,
    parser: ParserHandle,
    producer: AIOKafkaProducer,
    response_router: ResponseRouter,
):
    buffer = b""
    resp_buf = (ctypes.c_uint8 * (1048576 + 64))()    # reusable encode buffer

    try:
        while True:
            chunk = await asyncio.wait_for(
                reader.read(profile.receive_buffer_bytes), timeout=30.0
            )
            if not chunk:
                break
            buffer += chunk

            while result := parser.extract(buffer):
                (frame, frame_type) = result
                buffer = buffer[len(frame):]

                if frame_type != 1:           # only handle SDFC→DRS commands here
                    continue

                # 1. decode command
                msg = parser.parse(frame, frame_type)

                # 2. record inbound command
                await producer.send(
                    f"{profile.kafka_topic}.cmd",
                    json.dumps(msg).encode()
                )

                # 3. generate response (mode-aware: random / scenario / integrated)
                response_json = await response_router.route(msg, profile)

                # 4. encode JSON → binary DRS→SDFC response frame
                n = parser.format_response(response_json, resp_buf)
                if n < 0:
                    logger.error(f"[{profile.name}] format_response failed: {response_json}")
                    continue

                # 5. send response bytes back to SDFC over same TCP connection
                writer.write(bytes(resp_buf[:n]))
                await writer.drain()

                # 6. record outbound response (for drs-server storage/display)
                await producer.send(
                    profile.kafka_topic,
                    response_json.encode()
                )

    except asyncio.TimeoutError:
        logger.warning(f"[{profile.name}] timeout: {writer.get_extra_info('peername')}")
    finally:
        writer.close()
        await writer.wait_closed()
```

### 12.4 The `ResponseRouter`

`ResponseRouter` is the mode-switching component that isolates all operating-mode logic from the TCP handler:

```python
class ResponseRouter:
    async def route(self, command: dict, profile: HardwareProfile) -> str:
        session = self.session_registry.active()
        if session is None or session.mode == OperatingMode.RANDOM:
            return self.random_gen.generate(command, profile)
        elif session.mode == OperatingMode.SCENARIO:
            return await self.scenario_client.get_scenario_response(
                session.exercise_id, command
            )
        else:                                  # OperatingMode.INTEGRATED
            return await self.hardware_relay.forward(command, profile)
```

In the Hybrid:
- **Mode A (today):** the `scenario_client` makes HTTP calls to `Sg.App` running on WS1, which exposes a small read-only HTTP server during exercise execution. The endpoint is `GET /exercises/{id}/responses?group_id=X&unit_id=Y&tick=Z`, returning the pre-computed scenario response from `computed_links`.
- **Mode B (future):** same endpoint, hosted on `Sg.Server`. No drs-bridge changes; the endpoint URL is configured per deployment.
- **Alternative considered but not recommended:** drs-bridge reads `computed_links` directly from PostgreSQL. Avoided because it puts SQL in drs-bridge, couples bridge to database schema, and bypasses the scenario publisher's session-state machine.

### 12.5 Error handling contract per layer

| Layer | On C++ parse error (-1) | On Kafka produce error | On TCP write error |
|---|---|---|---|
| 1 (Transport) | Log + drain buffer to next header | Log + retry with backoff; close connection after N failures | Log + close connection cleanly |
| 2 (Parse) | Return -1 to layer 1 (corrupt frame) | N/A | N/A |
| 3 (Routing) | N/A | N/A (no Kafka here) | N/A |
| 4 (Lifecycle) | N/A | Alert health reporter | Supervisor restarts listener |

### 12.6 Kafka producer tuning

Single shared producer across all hardware profiles. Tuning:

```python
producer = AIOKafkaProducer(
    bootstrap_servers=broker,
    compression_type="lz4",
    linger_ms=5,                  # micro-batching at high message rates
    max_batch_size=65536
)
```

`linger_ms=5` doubles throughput at 1,000–2,000 msg/s with no perceptible latency impact on device communication.

### 12.7 Build (CMake)

All parser libs build in a single `cmake --build` invocation. No internet required.

```cmake
foreach(HW srx mrx gnss jvuhf jhf rdfs sjrr jlb jmb jhb aus pads)
    add_library(${HW} SHARED
        ${HW}/${HW}_parser.cpp
        common/frame_buffer.cpp
        common/json_writer.cpp
    )
endforeach()
```

A new variant adds itself to this loop and ships a `<variant>/<variant>_parser.cpp` source file.

## 13. `drs-server` internal design

> **[Telemetry phase, design-time.]**

`drs-server` is a pure consumer: no TCP servers, no STK, no command routing. It consumes Kafka, persists to TimescaleDB, serves REST and WebSocket queries to the SG-side operator UI on WS1, and also serves the DRS webapp (static assets + REST/WS endpoints for health, monitor-scan, IP config, logs) to a local browser on WS2 for the DRS Engineer persona.

### 13.1 Layered model

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  LAYER 3 — API                                                              │
│  routers/: measurement_router.py, session_router.py, health_router.py       │
│  ws/: websocket_manager.py                                                  │
│  Owns: FastAPI route handlers (async def), WebSocket accept/broadcast,      │
│        request validation, response serialisation                            │
│  Rule: No DB queries. No Kafka. Delegates entirely to layer 2.              │
├─────────────────────────────────────────────────────────────────────────────┤
│  LAYER 2 — SERVICE                                                          │
│  services/: measurement_service.py, session_service.py,                     │
│             report_service.py, broadcast_service.py                         │
│  Owns: query composition, pagination, aggregation, fan-out to WebSocket     │
│        subscribers on new Kafka messages                                    │
│  Rule: No direct asyncpg/SQL. All DB access through layer 1.                │
├─────────────────────────────────────────────────────────────────────────────┤
│  LAYER 1 — REPOSITORY + INGEST                                              │
│  repos/: measurement_repo.py, session_repo.py                               │
│  ingest/: consumer_manager.py, kafka_consumer.py (one per topic group)      │
│  Owns: async SQLAlchemy models, hypertable INSERT, SELECT with chunk        │
│        exclusion; Kafka consumer lifecycle; manual offset commit only       │
│        AFTER successful DB write                                             │
│  Rule: No business logic. No HTTP. Pure DB + Kafka I/O.                    │
├─────────────────────────────────────────────────────────────────────────────┤
│  LAYER 0 — INFRASTRUCTURE                                                   │
│  configs/: database.py (asyncpg engine, TimescaleDB), kafka_config.py      │
│  Owns: Connection pool init, engine creation, consumer group registration,  │
│        FastAPI lifespan startup/shutdown                                    │
│  Rule: No business logic. Constructed once; shared via DI.                 │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 13.2 Consumer lifecycle

Consumers start and stop with the FastAPI app. No `@on_event("startup")` (deprecated); use `lifespan`:

```python
@asynccontextmanager
async def lifespan(app: FastAPI):
    await consumer_manager.start_all()
    yield
    await consumer_manager.stop_all()
```

Each consumer runs as a supervised asyncio task:

- Restarts on failure with exponential backoff (1 s → 60 s cap).
- Kafka offset committed only **after** successful DB write — at-least-once delivery semantics.
- Write buffer flushed every 100 messages **or** 500 ms (whichever first).
- `GET /health/consumers` exposes liveness + restart count per hardware type.

### 13.3 Kafka → DB → WebSocket fan-out

The ingest layer is the only place that bridges Kafka and WebSocket. Pure asyncio, no threads:

```python
# ingest/kafka_consumer.py
async def consume_loop(topic: str, service: MeasurementService):
    consumer = AIOKafkaConsumer(topic, ...)
    async for msg in consumer:
        try:
            data = json.loads(msg.value)
            await service.ingest(data)            # write to DB (layer 1 → repo)
            await service.broadcast(data)         # fan-out to WS clients (→ layer 3)
            await consumer.commit()               # manual offset commit only after success
        except Exception:
            logger.exception("ingest failed, offset not committed")
```

### 13.4 WebSocket topic structure

```
WS /ws/measurements/<variant>           live FF/FH/Burst/NMEA/etc. for one variant
WS /ws/health                           all hardware health status
WS /ws/exercise/{id}/status             exercise execution progress
WS /ws/logs                             system log stream
```

Each WebSocket connection subscribes server-side to the matching Kafka topics; the client need not know the topic names.

### 13.5 Report query endpoints

All read endpoints are paginated and time-bounded — **no unbounded `query.all()`** (this regression is what caused the legacy system's ~10-minute perf-degradation issue).

```
GET /measurements
    ?session_id=&hardware_type=&message_type=&from=&to=&page=&size=

GET /measurements/export
    ?session_id=&from=&to=          StreamingResponse (chunked CSV/JSON)

GET /health/summary
GET /health/{hardware_type}/{instance_id}/history
GET /sessions
GET /sessions/{id}/stats
```

### 13.6 Design rules (load-bearing)

- **All route handlers `async def`** — no sync SQLAlchemy. Sync handlers block the event loop and are the root cause of legacy throughput collapse under sustained load.
- **Sessions scoped per message, not per thread.** SQLAlchemy `AsyncSession` constructed inside the handler, not held in a thread-local.
- **No `query.all()`** — every query has `.offset()` + `.limit()` with index hints.
- **Manual Kafka offset commit after successful DB write** — at-least-once delivery; on crash, recovery replays uncommitted offsets.
- **Composite index `(session_id, recorded_at, message_type)` on `measurements`** — every query path uses this index.

### 13.7 Kafka conventions

The control-plane Kafka code in both services is small (~400 lines) and
already demonstrates the patterns new publishers/consumers should
follow. Read the existing files end-to-end before adding new ones — they
are the canonical template, the same way
[`drs-bridge/parsers/reference/`](../../drs-bridge/parsers/reference/) is
the canonical C++ parser template.

**Reference files** (all <70 lines each):

| File | Pattern shown |
|---|---|
| [`drs-bridge/src/drs_bridge/control_publisher.py`](../../drs-bridge/src/drs_bridge/control_publisher.py) | Minimal producer: `Protocol`-typed Kafka dep + one method per event |
| [`drs-bridge/src/drs_bridge/health_publisher.py`](../../drs-bridge/src/drs_bridge/health_publisher.py) | Generic event-type dispatch |
| [`drs-server/src/drs_server/control_consumer.py`](../../drs-server/src/drs_server/control_consumer.py) | Async-iterator consumer loop + forward-compatible event dispatch |
| [`drs-server/src/drs_server/timesync/timesync_publisher.py`](../../drs-server/src/drs_server/timesync/timesync_publisher.py) | Producer with enum + numeric typed payload |
| [`drs-server/src/drs_server/lifespan.py`](../../drs-server/src/drs_server/lifespan.py) | **Load-bearing:** factory-injection wiring so tests can swap fakes |
| [`drs-bridge/src/drs_bridge/runtime.py`](../../drs-bridge/src/drs_bridge/runtime.py) | Same factory pattern on bridge side |
| [`drs-server/tests/test_control_consumer.py`](../../drs-server/tests/test_control_consumer.py) | `_FakeKafkaConsumer` in-memory stand-in pattern for unit tests |
| [`drs-server/tests/test_kafka_broker_integration.py`](../../drs-server/tests/test_kafka_broker_integration.py) | Skip-if-broker-down real-broker contract test |

**Patterns to copy:**

1. **Protocol-typed Kafka dependency.** Production code depends on a
   small `Protocol` (`async def send_and_wait` / `async def start` /
   `async def stop` / `__aiter__`) — not directly on `aiokafka`. Tests
   pass fakes implementing the Protocol.
2. **Lazy-imported factory at the wiring layer.** `_default_producer_factory`
   in [lifespan.py](../../drs-server/src/drs_server/lifespan.py) and
   [runtime.py](../../drs-bridge/src/drs_bridge/runtime.py) defers the
   `aiokafka` import to the first call so unit tests that mock the
   factory don't pay the import cost.
3. **Forward-compatible event dispatch.** Consumers read `body.get("event")`
   and skip unknown event types with a debug log — never raise. Adding a
   new event type doesn't require coordinated deploys.
4. **Unique consumer-group-id in tests.** Integration tests use
   `uuid.uuid4()`-derived group IDs so reruns don't replay offsets.

**Topic naming convention:**

| Pattern | Use | Examples |
|---|---|---|
| `<area>.<purpose>` | Control-plane (low-volume, system events) | `drs.control`, `system.timesync`, `system.health` |
| `hw.<variant>.<kind>` | Data-plane (high-volume telemetry — Phase 2) | `hw.rdfs.ff`, `hw.comm_df.fh` (deferred to first-IRS work) |

**Serialization:**

- **Control plane:** UTF-8 JSON. Compact, debuggable in `kafka-console-consumer`,
  no schema-registry overhead. Used by every existing publisher.
- **Data plane:** decision deferred. Choice between JSON / Protobuf / msgpack
  is driven by per-frame size + serialization-CPU budget under the actual
  variant's volume profile — not a guess against synthetic data.

**Local broker for development:**

See [`infrastructure/kafka/README.md`](../../infrastructure/kafka/README.md).
Single docker-compose command brings up a single-node KRaft Kafka; an
idempotent Python script provisions the three control-plane topics; the
integration test ([`drs-server/tests/test_kafka_broker_integration.py`](../../drs-server/tests/test_kafka_broker_integration.py))
verifies end-to-end against the real broker.

The data-plane (`hw.<variant>.<kind>` topics + `measurements` hypertable +
batched asyncpg insert pattern) is intentionally NOT pre-scaffolded —
schema and payload shape are designed against the first real IRS, not
against synthetic data.

## 14. Diagnostic logging

Verbose logging for the C# desktop is gated by the `MVP4_DIAG` environment variable. Default off (zero overhead).

To enable:
```
set MVP4_DIAG=1
dotnet run --project Sg.App -c Release
```

Output appended to `Desktop\stk-debug.log`. Lines tagged:
- `[StkDisplayHost]` — mouse coords, mode transitions, editing-event firings, preview-create success/failure.
- `[PanelHost]` — `ScenarioChanged` subscriber gate: which entity, EditingPath, whether refresh ran.

The file is rotation-free — delete it manually between debug sessions if it grows unwieldy.

For `drs-server` and `drs-bridge` (telemetry phase), log to `C:\ProgramData\EWTSS\logs\`. Configuration uses standard Python `logging` with structured output (JSON when running under a Windows Service via NSSM or `sc.exe`, human-readable in interactive runs).

## 15. Code conventions

### 15.1 C#

- Target framework: `net8.0-windows` (Domain), `net8.0-windows` with `UseWPF` for App, `UseWindowsForms` for App and Domain (Domain needs `Application.DoEvents` for STK message-pumping in `PumpMessages`).
- Nullable reference types: `enable` everywhere.
- Underscore-prefixed private fields: `_committed`, `_path`, `_globe3D`. Matches the existing code; not the .NET runtime convention but consistent within the repo.
- One class per file. Filename matches class.
- Records for DTOs (immutable). Use `record` syntax.
- `[ObservableProperty]` for VM observable properties. Use the field-backing approach with leading underscore.
- `Sg.Domain.Stk.StkScenarioBackend` is **internal-visible** to `Sg.App` and `Sg.Tests` for the `Root` accessor; do not make `Root` public.
- XAML: prefer code-behind for view-specific wiring (it's not a sin); keep view-models pure C# with no XAML refs.

### 15.2 Comments

- Default to no comments. Well-named identifiers carry their meaning.
- Comment when the **why** is non-obvious — a hidden constraint, a workaround for a specific bug, an STK COM gotcha. The MVP4.5 codebase has good examples of this style.
- Don't comment what the code already says.
- Don't reference the current task or PR ("added for ticket #123") — that belongs in the commit message.

### 15.3 Tests

- Naming: `MethodName_scenario_expected_outcome`. Snake-case sentence-style; readable as English.
- Use `FluentAssertions` (`should().be(...)`) — already in the project.
- Use `[TestFixture, Category(TestCategories.Unit)]` for unit; `[TestFixture, Category(TestCategories.Integration)] + [Apartment(STA)]` for STK integration.
- Don't use `Moq` — `FakeScenarioBackend` is sufficient for our needs and avoids the framework drift.
- Don't introduce a runtime mock as a fallback; tests-only.

### 15.4 Git — commit + branching

**Commits:**
- One concern per commit. PR titles and commit messages explain the why, not what (the diff shows what).
- Commit messages reference the relevant ADR or spec section when committing architecture-bearing changes.
- Co-authored-by trailers are optional but used in this repo for AI-pair-programmed commits.

**Branching strategy — trunk-based with short-lived feature branches.** `main` is always deployable; every commit on `main` must pass CI before merge. Feature branches branch from `main` and merge back via squash-merged PRs, typical lifespan ≤ 5 working days. Long-running parallel workstreams (G's DRS webapp, F's drs-server consumers) use short-lived PRs that integrate frequently; do not keep an 8-week feature branch open. Hotfix branches: `hotfix/<issue>` from the last release tag, merged into `main` (and any in-flight release branch) via the same squash-PR flow.

Branch naming:
- `feat/<short-name>` — new feature or scope addition
- `fix/<short-name>` — bug fix
- `refactor/<short-name>` — non-functional improvement
- `docs/<short-name>` — docs-only
- `chore/<short-name>` — tooling, deps, CI infra

The `main` branch is protected: requires at least one approving review, CI green, up-to-date with `main` before merge.

### 15.5 CI infrastructure

**Pipeline:** GitHub Actions (or equivalent — Jenkins / GitLab CI if the project lead prefers self-hosted; default is GitHub Actions).

**Runners:**
- **Per-PR run** (every push to any feature branch or PR open):
  - C# unit tests (`dotnet test --filter Category=Unit`).
  - Python unit tests (drs-server, drs-bridge, ICD codegen tool).
  - C++ parser unit tests (golden-frame fixtures per variant).
  - DRS webapp build + unit tests (Angular/React).
  - Markdown link-check on `docs/`.
  - Lint (per-language: dotnet format, ruff for Python, clang-tidy for C++, eslint/prettier for the webapp).
- **Nightly run** (scheduled, on `main`):
  - Cumulative integration tests (all phases of [Execution Plan §6](v2-execution-plan.md#6-integration-testing-checkpoints)) on a dedicated CI rig with STK 12 installed.
  - Load tests at the current Phase gate's target throughput.
- **Pre-release** (manual trigger before customer DVD packaging):
  - Full integration suite + DVD install rehearsal on a clean two-workstation environment.

CI scripts live at `.github/workflows/` (or equivalent). Each runner pins its OS image + tool versions; STK 12 pin is captured as a runner-level constant and asserted at integration-test startup.

### 15.6 Code review

**SLA:**
- **Critical-path PRs** (changes to: parser ABI, OpenAPI spec, `IScenarioBackend`, RBAC schema, ADR docs): **24-hour turnaround** for first review.
- **Standard PRs:** **48-hour turnaround** for first review.
- Subsequent rounds after author updates: **24 hours**.

**Approval rule:**
- Single approving review required to merge into `main`.
- D (cross-stack lead) reviews any change to contract surfaces (parser ABI, OpenAPI, `IScenarioBackend`, RBAC, ADRs) — these PRs need both D's review and the owning author's review.
- Self-merge after the SLA window only on docs-only or chore PRs, and only with an explicit "self-merging — review pending" note.

**Etiquette:**
- Author drains review comments before requesting re-review.
- Comments are about the code, not the author. Disagreement gets escalated to D or the project lead; do not let a PR sit in review limbo.
- "Nit:" prefix for stylistic suggestions; "Block:" for must-fix items. Author resolves nits at their discretion; block comments require explicit resolution.

### 15.7 Third-party licence allow-list

The customer takes IP rights on delivery (per RFQ). Every third-party dependency we adopt must carry a licence compatible with that transfer.

**Allow-list (no further review needed):**
- MIT
- Apache 2.0
- BSD-2-Clause, BSD-3-Clause
- ISC
- Public Domain / CC0
- Unlicense
- Boost Software Licence 1.0
- zlib
- MPL-2.0 (only when used as a separate dynamic library, not statically linked)

**Block-list (do not adopt):**
- GPL (all versions) — copyleft contaminates the IP transfer
- AGPL — same plus network-use clause
- LGPL — linkage problem for C++ parser libraries
- Server Side Public Licence (SSPL)
- Custom / commercial licences without explicit project-lead approval

**Review-needed list (project lead approval before adoption):**
- EPL (Eclipse Public Licence)
- CDDL
- Any licence not in the above lists

When adding a new dependency:
1. Verify its licence is on the allow-list (or get project-lead approval).
2. Record the licence in `packages/THIRD-PARTY-LICENCES.md` (one row per dep with name, version, licence, source URL).
3. Vendor the dependency under `packages/` for air-gap install.
4. Verify offline install works (`pip install --no-index --find-links=packages/` for Python; equivalent for npm and CMake).

**Planned CI gate (currently paused):** the parked workflow at [`.github/disabled/ci.yml`](../../.github/disabled/ci.yml) will run `pip-licenses` (Python) + `license-checker` (npm) + manual scan of `packages/` (C++) on every PR and fail on disallowed licences once hosted runners are re-enabled. Until then, the licence check is a manual PR-review item. The `packages/THIRD-PARTY-LICENCES.md` is regenerated and committed alongside dep changes.

## 16. Pull request checklist

Before opening a PR for review:

**Build + test:**
- [ ] `dotnet build` clean (0 warnings, 0 errors).
- [ ] `dotnet test --filter Category=Unit` green.
- [ ] Python + C++ + webapp unit tests green (whichever the change touches).
- [ ] Manually run the affected feature in Release mode if the change touches `StkDisplayHost`, `InteractionController`, any view-model, or `StkScenarioBackend`.

**Architectural invariants:**
- [ ] If touching `Sg.Domain.Contracts`: did the JSON round-trip test cover the new shape? Are existing tests still green?
- [ ] If touching `StkDisplayHost`: any `+= ...` outside `_ensure*Subscribed`? If yes, justify in the PR description.
- [ ] If adding a new entity type: did §8's full file checklist get covered?
- [ ] If adding a new hardware variant: did §9's full checklist get covered? Golden-frame test corpus updated?
- [ ] If touching DRS webapp: framework-conventional structure followed; static assets copied into `drs-server/app/webapp_static/` on build.
- [ ] Diagnostic logging changes: gated by `MVP4_DIAG` env check (C#) or equivalent runtime flag (Python / webapp)?
- [ ] No `Moq`, no runtime mock fallback.
- [ ] No `MVP4_BACKEND=Remote` runtime path activated in production code.

**drs-server route handlers:**
- [ ] All new handlers are `async def` (no `def`).
- [ ] No `query.all()` — every query has `.offset()` + `.limit()`.
- [ ] DB session scoped per message / request, not held across `await`.
- [ ] Kafka offset commit happens after DB write, never auto-commit.

**Dependencies + licences (§15.7):**
- [ ] If adding a new dependency: licence is on the §15.7 allow-list (or project-lead-approved).
- [ ] If adding a new dependency: vendored under `packages/`; `packages/THIRD-PARTY-LICENCES.md` updated.
- [ ] If adding a new dependency: offline install verified (`pip install --no-index --find-links=packages/` or equivalent).

**Branching + review (§15.4–§15.6):**
- [ ] Branch name follows `<type>/<short-name>` convention.
- [ ] PR scope is single-concern; if not, split it.
- [ ] Commit messages explain the *why*; PR description summarises the change.
- [ ] Critical-path PRs (parser ABI, OpenAPI spec, `IScenarioBackend`, RBAC schema, ADRs) flagged in the title — D is auto-tagged for review.
- [ ] Self-merge only on docs-only or chore PRs after the SLA window with an explicit note.

## 17. Where to find things

- **Architectural choices and rationale:** [Decision Record](decision-record.md).
- **System overview and component responsibilities:** [Architecture Overview](architecture-overview.md).
- **Deployment, licences, troubleshooting:** [Deployment Guide](deployment-guide.md).
- **Concrete ICD-to-parser worked example (COMM DF Receiver):** [`icd-reference-comm-df.md`](icd-reference-comm-df.md). Use as the template when documenting the next hardware variant.
- **Project execution plan (staffing, parallel workstreams, critical path, gates):** [v2 Execution Plan](v2-execution-plan.md).
- **Risk register (active, retired, deferred):** [Risk Register](risk-register.md).
- **MVP4.5 design (DTO boundary, perf invariants):** [`mvp4.5-dto-boundary-and-perf-design.md`](specs/mvp4.5-dto-boundary-and-perf-design.md).
- **Hybrid frontend design:** [`hybrid-frontend-design.md`](specs/hybrid-frontend-design.md).
- **ICD code generator (`tools/icd_codegen`):** [`icd-codegen-tool-design.md`](specs/icd-codegen-tool-design.md). The Excel-ICD-to-C++/YAML/TypeScript skeleton generator referenced from §9.1.
- **Original v2 tech-stack analysis (archived):** [`v2-tech-stack-archive.md`](specs/v2-tech-stack-archive.md). Includes the full STK COM gotchas list in §25.3.3 + §25.3.5 — the canonical reference for any new STK debugging.
- **MVP4.5 README (operational notes):** `mvp4/README.md`.
- **Reference repo for STK + WPF integration patterns:** Constelli's `EWTSS_CSP_POC` (branch `scenario-tree-emitters`). The "Position.AssignGeodetic", "no permanent subscriptions", "RefreshStkViews after specific operations" patterns all came from cross-referencing this repo during MVP4.5.
- **STK's own samples:** `C:\Program Files\AGI\STK 12\bin\CodeSamples\CustomApplications\CSharp\` — `3DObjectEditing/Form1.cs`, `PolygonDrawing/Form1.cs`, `Events/Form1.cs`, `STKProTutorial/Form1.cs`. Authoritative for STK API usage.
