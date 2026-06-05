# MVP4.5 — DTO Boundary + STK Performance Patterns — Design Spec

**Date:** 2026-05-01
**Status:** Draft for user review
**Builds on:** [`mvp4-stk-native-embedded-design.md`](./mvp4-stk-native-embedded-design.md), [`mvp4-map-first-interaction-addendum.md`](./mvp4-map-first-interaction-addendum.md), [`v2-tech-stack-archive.md`](./v2-tech-stack-archive.md) §25
**Reference repo:** `Constelli-Projects/EWTSS_CSP_POC` branch `scenario-tree-emitters` — a Constelli C# STK integration POC that the team validated as having significantly smoother STK interaction than MVP4

---

## 1. Goal

Two concurrent improvements to MVP4:

1. **Performance.** Adopt the STK-integration patterns from the reference repo that explain its smoother pan/zoom and editing experience. These are localised, low-risk additions to the COM-touching code.
2. **Browser-future readiness.** Introduce a DTO boundary between view-models and the STK service so that, when a Cesium-based browser frontend is added later, the desktop app's contract becomes a network-shaped REST surface with no domain-layer churn.

The constraint: the DTO boundary must NOT degrade interactive performance. High-frequency operations (drag handles, mouse picks, pan/zoom) keep direct COM access. The DTO boundary is for transactional save points (form Apply, scenario load, persistence).

**This is an MVP-scope effort.** The entity catalog (Aircraft / Facility / AreaTarget / Sensor / CoverageDefinition / FigureOfMerit) reflects MVP4's existing surface. When the final EWTSS project lands and the domain pivots to EWTSS-specific entities (Emitters with full RF parameters, AOI, Lines, CC, RDFS / JSVUSHF hardware, per the reference repo), the catalog gets re-keyed but the architectural patterns established here stay.

---

## 2. Architecture overview

```
┌─────────────────────────────────────────────────────────────────┐
│  Sg.App (WPF)                                              │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │ MainWindow / panels / object tree / display host           │ │
│  └─────────────────────┬──────────────────────────────────────┘ │
│                        │ DI                                     │
│  ┌─────────────────────┴──────────────────────────────────────┐ │
│  │ View-Models (per panel; one batched call per Apply)        │ │
│  └─────────────────────┬──────────────────────────────────────┘ │
└────────────────────────┼────────────────────────────────────────┘
                         │
         ┌───────────────┼─────────────────────────┐
         │ Sg.Domain.Contracts                │
         │ (DTOs + interfaces, no COM, no WPF)     │
         │   - AircraftDto, FacilityDto, ...       │
         │   - IScenarioBackend                    │
         │     · GetAircraft(path) -> AircraftDto  │  network-shaped
         │     · UpdateAircraft(path, dto)         │  today: in-proc
         │     · ComputeAll() -> ComputeResultDto  │  future: HTTP
         └───────────────┬─────────────────────────┘
                         │ implements
         ┌───────────────┴─────────────────────────┐
         │ Sg.Domain.Stk                      │
         │ (COM-touching, in-process, today's only │
         │  implementation)                         │
         │   - StkScenarioBackend (concrete)       │
         │   - Holds AgSTKXApplication (object?)   │
         │   - Uses dynamic, BeginUpdate/EndUpdate │
         │   - DoEvents() after every mutation     │
         └───────────────┬─────────────────────────┘
                         │ COM
                         ▼
                  STK 12 Engine

┌──── INTERACTIVE PATH (bypasses DTO boundary by design) ─────┐
│ StkDisplayHost ──→ AxAgUiAxVOCntrl.GetOcx().ScreenXYToLatLon
│                ──→ direct AgStkObjectRoot.StartObjectEditing
│ Mouse pick + drag never crosses the contract; stays at COM  │
│ for full per-frame perf.                                    │
└─────────────────────────────────────────────────────────────┘
```

### Key principles

1. **One namespace, two projects-worth of discipline.** `Sg.Domain.Contracts` (DTO records + `IScenarioBackend`) and `Sg.Domain.Stk` (COM impl) live in the same `Sg.Domain` project for now — but enforced as separate folders/namespaces. Project split deferred until the browser becomes a real near-term ask (then it's a 1-day mechanical move).

2. **Two-path access pattern.**
   - **Form Apply path (DTO boundary):** User clicks Apply → ViewModel calls `_backend.UpdateAircraft(path, dto)` → one batched COM operation inside `BeginUpdate`/`EndUpdate`. Network-friendly, browser-friendly later.
   - **Interactive path (direct COM):** Drag handles via STK's `StartObjectEditing`, click-to-place via `GetOcx().ScreenXYToLatLon`, rubber-band preview via direct primitives. These bypass the DTO contract entirely because they're real-time UI feedback that needs zero latency.

3. **Performance patterns live in the STK impl layer.** All nine reference-repo patterns are internal to `Sg.Domain.Stk` and `Sg.App.Views.Shell.StkDisplayHost`. They don't surface in the contract.

4. **Contract is shaped for future HTTP transport.** Every `IScenarioBackend` method takes/returns DTOs (records), strings, primitives. No `IAgStkObject`, no `Color` (use string `#RRGGBB`), no event handlers across the seam. When a server is eventually added, the contract serialises to JSON without rework.

---

## 3. Entity contract shape

### 3.1 DTO records (immutable, JSON-friendly)

```csharp
namespace Sg.Domain.Contracts;

public sealed record AircraftDto(
    string Name, string ColorHex, bool PathVisible,
    IReadOnlyList<WaypointDto> Waypoints);

public sealed record WaypointDto(
    double LatitudeDeg, double LongitudeDeg, double AltitudeMeters, double SpeedMps);

public sealed record FacilityDto(
    string Name, string ColorHex,
    double LatitudeDeg, double LongitudeDeg, double AltitudeMeters);

public sealed record AreaTargetDto(
    string Name, string OutlineColorHex, bool FillEnabled, string FillColorHex,
    IReadOnlyList<VertexDto> Vertices);

public sealed record VertexDto(double LatitudeDeg, double LongitudeDeg);

public sealed record SensorDto(
    string Name, string ParentPath, string ColorHex, bool ShowIntersection,
    double OuterHalfAngleDeg, double InnerHalfAngleDeg,
    PointingMode Pointing, double FixedAzimuthDeg, double FixedElevationDeg,
    string? TargetPath);

public sealed record CoverageDefinitionDto(
    string Name,
    double LatMin, double LatMax, double LonMin, double LonMax,
    double LatStep, double LonStep,
    double MinAltitude, double MinElevationAngle,
    IReadOnlyList<string> AssetPaths);

public sealed record FigureOfMeritDto(
    string Name, string ParentPath, FomKind Kind, FomStatistic Statistic);

public sealed record EntityNodeDto(
    EntityKind Kind, string Name, string Path,
    IReadOnlyList<EntityNodeDto> Children);

public sealed record ComputeResultDto(
    bool Success, string? Message, DateTime ComputedAtUtc);
//      Future: per-FOM grid values, access intervals — placeholder for browser-future
```

Existing enums (`EntityKind`, `PointingMode`, `FomKind`, `FomStatistic`) stay in `Sg.Domain.Models`; Contracts references that namespace.

### 3.2 `IScenarioBackend` interface

```csharp
namespace Sg.Domain.Contracts;

public interface IScenarioBackend
{
    // Scenario lifecycle
    string?  ScenarioName { get; }
    DateTime StartTimeUtc { get; }
    DateTime StopTimeUtc  { get; }
    void NewScenario(string name, DateTime startUtc, DateTime stopUtc);
    void CloseScenario();

    // Tree access (snapshot — full subtree returned in one call)
    IReadOnlyList<EntityNodeDto> Children { get; }
    EntityNodeDto? FindByPath(string path);

    // Tree mutation
    EntityNodeDto AddEntity(EntityKind kind, string name, string? parentPath);
    void RemoveEntity(string path);

    // Per-entity Get / Update (transactional, batched)
    AircraftDto           GetAircraft         (string path);
    FacilityDto           GetFacility         (string path);
    AreaTargetDto         GetAreaTarget       (string path);
    SensorDto             GetSensor           (string path);
    CoverageDefinitionDto GetCoverage         (string path);
    FigureOfMeritDto      GetFom              (string path);

    void UpdateAircraft  (string path, AircraftDto           dto);
    void UpdateFacility  (string path, FacilityDto           dto);
    void UpdateAreaTarget(string path, AreaTargetDto         dto);
    void UpdateSensor    (string path, SensorDto             dto);
    void UpdateCoverage  (string path, CoverageDefinitionDto dto);
    void UpdateFom       (string path, FigureOfMeritDto      dto);

    // Compute + persistence
    ComputeResultDto ComputeAll();
    void SaveAsSc (string path);
    void SaveAsVdf(string path, string password);
    void LoadSc   (string path);
    void LoadVdf  (string path, string password);
    string? CurrentPath { get; }
    bool    CurrentIsPackaged { get; }
    string  SuggestedPath(string scenarioName);

    // Change notifications
    event EventHandler? ScenarioChanged;
}
```

### 3.3 What's deliberately NOT in the contract

- **No spatial-incremental mutators** (`AppendAircraftWaypoint`, `SetFacilityPosition`, `AppendAreaTargetVertex`). Their use case is click-to-place, which bypasses the contract by design (§4.2). The placement flow accumulates a DTO locally and calls `UpdateXxx` once at finalize.
- **No COM types**, no `IAgStkObject`, no `Color` (use string `#RRGGBB`).
- **No `Marshal.*`, no event handlers across the seam.** `ScenarioChanged` is a plain `EventHandler` for in-proc; in a future server impl, a SignalR adapter wraps and re-fires it locally.
- **No incremental edit-tracking events** (`OnObjectEditingApply`, etc.) — these are STK ActiveX events, app-layer only. Stay in `StkDisplayHost`.

### 3.4 Why batched DTO Update vs property setters

- Aligns with reference's `CreateOrUpdateEmitterFull(...)` — proven to work
- Internal impl wraps the whole Update in `BeginUpdate()` / `EndUpdate()` (perf pattern §4.4) so a 10-property edit triggers one render, not 10
- One-call ergonomics ("Apply commits everything at once") match the form UX
- HTTP-friendly: one PUT per Update

### 3.5 Future evolution (final EWTSS project)

When the final EWTSS project lands and the domain shifts to EWTSS-specific entities (Emitters with RF parameters, AOI, Lines, CC, RDFS/JSVUSHF hardware), the **entity catalogue is re-keyed but the patterns persist**:

- DTO records (immutable, JSON-friendly)
- One `IScenarioBackend` interface, per-entity Get/Update
- Transactional Updates (full DTO in, batched COM internally)
- Snapshot tree access via `Children` and `FindByPath`
- Spatial mutators bypass the contract; interactive path stays direct-COM
- ComputeAll returns a result DTO; persistence takes file-path strings

The MVP4.5 contract stands as a template. Re-keying it for the EWTSS domain is mechanical when the time comes.

---

## 4. Performance patterns + interactive path + view-model migration

### 4.1 Where the 9 patterns from the reference repo land

All in `Sg.Domain.Stk` (the `IScenarioBackend` impl) and `Sg.App.Views.Shell.StkDisplayHost`. None surface in the Contracts namespace.

| # | Pattern | Where |
|---|---|---|
| 1 | `CreateControl()` on both ActiveX views before any `LoadVDF` / `LoadScenario` | `StkDisplayHost` ctor — explicit call right after `Host3D.Child = ...`. Before the engine bootstraps a scenario into them. |
| 2 | `Application.DoEvents()` after every scene-mutating COM call | Helper method `_pumpMessages()` inside `StkScenarioBackend`; called after every Update / AddEntity / RemoveEntity / SaveAs / LoadScenario. View layer also calls it after `RefreshStkViews()`. |
| 3 | `MouseMode = 0` + `AdvancedPickMode = false` on 2D control | `StkDisplayHost` after `_stk2DView.CreateControl()`, via `((dynamic)_stk2DView.GetOcx()).MouseMode = 0` etc. |
| 4 | `BeginUpdate()` / `EndUpdate()` batching | Internal to every `UpdateXxx(path, dto)` method in `StkScenarioBackend`. Pattern: `_root.BeginUpdate(); try { /* all writes */ } finally { _root.EndUpdate(); }`. |
| 5 | `dynamic` for COM access where typed PIA is awkward | Pervasive in `StkScenarioBackend`. Replaces approximately half the typed-cast paths in current MVP4. Examples: `((dynamic)_root.GetObjectFromPath(path)).Unload()`, `dynamic platform = ...`. |
| 6 | `AgSTKXApplication` held as `private object? _stkxApp` (untyped) | Top of `StkScenarioBackend`. Assigned once; never re-created; released only on `Dispose`. |
| 7 | Pragmatic mix of typed COM + `ExecuteCommand` | Whichever is simpler. Examples: typed `Children.New(eFacility, name)` for create; `ExecuteCommand("Remove / */Facility/{name}")` for delete; `ExecuteCommand("Zoom Scenario/* LatLon ...")` for zoom. |
| 8 | `GetOcx().ScreenXYToLatLon` for picks instead of `PickInfo` | `StkDisplayHost._forwardDown3D/2D` and `_forwardMove3D/2D`. Bypasses the Ax-wrapper marshalling per click. |
| 9 | Co-bootstrap order: `AgSTKXApplication` first → controls created → `CreateControl()` → `LoadVDF` | `App.OnStartup` and `StkDisplayHost` ctor coordinate this. Same as MVP4's order today, but with the explicit `CreateControl()` step inserted. |

### 4.2 Interactive path — direct COM, contract bypass

Three operations stay outside the DTO contract because they're real-time UI feedback, not transactional saves:

| Operation | Path | Why bypass |
|---|---|---|
| Click-to-place new entity | `StkDisplayHost.OnMouseDown` → `IInteractionController.OnMapMouseDown(lat, lon, ...)` → controller accumulates points in a local in-memory DTO; rubber-band preview updates via direct `IAgStkGraphicsPolylinePrimitive.SetCartographic` | Each click would otherwise fire `Update(dto)` → re-write the whole STK entity. With the controller-local DTO, STK is touched only at finalize: `AddEntity(...)` + one `UpdateXxx(...)`. |
| Drag handles on existing entity | `StkDisplayHost._onModeChanged(EditingEntity)` → `_globe3D.StartObjectEditing(stkPath)` → STK runs the drag UX itself; we listen to `OnObjectEditingApply` / `OnObjectEditingStop` | STK's native drag handles AND coordinate readout are exactly the desired UX. No DTO round-trip per mouse-move. On commit, the controller calls `GetXxx(path)` once to refresh local state. |
| Pan / zoom / rotate the globe | Native STK behavior on the ActiveX control | Not our code at all. |

The controller-local DTO accumulator pattern is new in MVP4.5. Today's MVP4 calls `_stk.AppendAircraftWaypoint(...)` per click, hitting STK each time. After refactor: clicks update the in-memory `AircraftDto` and the preview primitives only; STK is touched once on finalize.

### 4.3 View-model migration

Current shape (e.g. `AircraftViewModel`):

```csharp
private readonly IAircraftBackend _backend;
[ObservableProperty] private string _colorHex;
// ...
protected override void DoApply()
{
    _backend.ColorHex    = ColorHex;     // per-property COM call
    _backend.PathVisible = PathVisible;  // per-property COM call
    _backend.SetWaypoints(Waypoints.Select(...).ToList());
}
```

Target shape:

```csharp
private readonly IScenarioBackend _backend;
private readonly string _path;
private AircraftDto _committed;          // last-applied state
[ObservableProperty] private string _colorHex;
// ...
protected override void DoApply()
{
    var dto = new AircraftDto(_path.Split('/')[^1], ColorHex, PathVisible,
                              Waypoints.Select(w => new WaypointDto(...)).ToList());
    _backend.UpdateAircraft(_path, dto);  // ONE call, batched internally
    _committed = dto;
}

protected override void DoReset()
{
    var dto = _committed;
    ColorHex    = dto.ColorHex;
    PathVisible = dto.PathVisible;
    Waypoints.Clear();
    foreach (var wp in dto.Waypoints)
        Waypoints.Add(new WaypointModel { /* from DTO */ });
}
```

Same pattern for the other five entity VMs. Each VM ends up smaller because there's no property-bag indirection layer.

### 4.4 Files removed / renamed

The 6 existing `IXxxBackend` interfaces and 6 `ComXxxBackend` impls are deleted. Their logic migrates into the typed Get/Update methods on `StkScenarioBackend`.

```
Sg.Domain/ViewModels/
  IAircraftBackend.cs        ← deleted
  IFacilityBackend.cs        ← deleted
  IAreaTargetBackend.cs      ← deleted
  ISensorBackend.cs          ← deleted
  ICoverageBackend.cs        ← deleted
  IFomBackend.cs             ← deleted

Sg.Domain/Services/
  ComAccessors.cs            ← contents migrated to StkScenarioBackend, file deleted
  StkRootService.cs          ← renamed to Sg.Domain/Stk/StkScenarioBackend.cs
  IStkRootService.cs         ← renamed to Sg.Domain/Contracts/IScenarioBackend.cs
  PersistenceService.cs      ← contents folded into StkScenarioBackend
  IPersistenceService.cs     ← deleted (rolled into IScenarioBackend)
```

`IFileDialogService` and `IInteractionController` stay in their current locations (they're WPF-app-local, not on the contract).

---

## 5. Browser-future readiness

Items MVP4.5 does **NOT** add now but the architecture permits later, with each item's "cost when needed" estimate.

### 5.1 Project split (when needed: ~1 day)

When a server is added, today's namespace structure becomes three projects:
- `Sg.Contracts` (Contracts + Models)
- `Sg.Stk.Adapter` (Stk)
- `Sg.Domain` (Interaction + ViewModels + WPF-friendly Services)

Folder structure stays the same; `csproj` boundaries change. Tests adjust their references; nothing else.

**Why defer:** project split adds CI complexity and inter-project compile time today for zero immediate value. The namespace fence (no `using AGI.STKObjects` allowed inside `Contracts/`) is enforceable via an `.editorconfig` rule today.

### 5.2 HTTP transport — `Sg.Stk.HttpClient` (when needed: ~3 days)

A second `IScenarioBackend` impl that wraps an `HttpClient`. Each interface method maps to one REST call:

```csharp
public sealed class HttpScenarioBackend : IScenarioBackend
{
    public AircraftDto GetAircraft(string path) =>
        _http.GetFromJsonAsync<AircraftDto>($"/scenario/aircraft/{Uri.EscapeDataString(path)}").Result;

    public void UpdateAircraft(string path, AircraftDto dto) =>
        _http.PutAsJsonAsync($"/scenario/aircraft/{Uri.EscapeDataString(path)}", dto).Wait();
    // ... etc, mechanical
}
```

Because every contract method is JSON-shapeable today, this is pure plumbing — no domain decisions deferred.

### 5.3 ASP.NET Core server — `Sg.Server` (when needed: ~1 week)

Hosts `StkScenarioBackend` server-side, exposes the contract as REST endpoints + a SignalR hub for `ScenarioChanged` events:

```
POST   /scenario                          (NewScenario)
GET    /scenario/children                 → IReadOnlyList<EntityNodeDto>
POST   /scenario/entities                 (AddEntity)
DELETE /scenario/entities/{path}          (RemoveEntity)
GET    /scenario/aircraft/{path}          → AircraftDto
PUT    /scenario/aircraft/{path}          (UpdateAircraft)
... (5 more entity types, same pattern)
POST   /scenario/compute                  → ComputeResultDto
POST   /scenario/save                     (SaveAsSc/SaveAsVdf)
POST   /scenario/load                     (LoadSc/LoadVdf)
WS     /scenario/changes                  ← ScenarioChanged push
```

### 5.4 CZML export hook (when needed: ~3 days)

`IScenarioBackend.ExportCzml() : string` method, implemented by `StkScenarioBackend` using STK DataProviders (the MVP3 approach). Browser frontend consumes this and feeds Cesium.

**Why defer:** browser-only concern. Adding the method now (returning empty string from `StkScenarioBackend`) costs nothing structurally but commits to a method signature we may want to refine. Better designed alongside the actual browser consumer.

### 5.5 Cesium browser frontend (when needed: ~2–3 weeks)

Separate Angular/React project. Talks to `Sg.Server` via REST + SignalR. Consumes CZML for visualization. Shares no code with the WPF app, but consumes the same JSON DTOs (potentially via a TypeScript types file generated from the C# contracts).

The boundary work in MVP4.5 is what makes the browser frontend 2–3 weeks instead of 6.

### 5.6 What MVP4.5 explicitly DOES include for browser-readiness

Cheap-now / expensive-later items that we lock in:

1. **All DTOs are `sealed record` with JSON-friendly properties.** No COM types, no `Color`, no `IAg*`. Test: `JsonSerializer.Serialize(dto)` round-trips losslessly.
2. **`IScenarioBackend` is the single contract.** No view-model talks to STK directly. (Exception: interactive operations via `StkDisplayHost` — those are inherently in-process and don't cross any future seam.)
3. **Namespace fence enforced by `.editorconfig`.** A rule that disallows `using AGI.*` inside `Sg.Domain.Contracts.*`. Catches accidental contamination.
4. **`SaveAsSc(string path)` etc. take string paths, not `FileInfo`.** Browser-future maps these to server-relative paths.
5. **`ScenarioChanged` is a plain `EventHandler`.** In-proc today; HTTP/SignalR adapter later wraps it.
6. **`ComputeResultDto` exists** even though today it carries minimal payload. Locks the shape.

### 5.7 What MVP4.5 explicitly does NOT include and why

1. **No HTTP server, no HTTP client** — pure speculation today.
2. **No CZML method on the contract** — defer until the consumer exists; the right shape may differ once we know what Cesium needs.
3. **No project split** — namespace discipline is enough; CI cost not justified yet.
4. **No multi-user concerns** — auth, sessions, server-side scenario isolation. All deferred to whenever multi-user is a real ask.
5. **No DB layer** (the reference repo has MySQL + EF Core; we don't). MVP4's `.sc`/`.vdf` files stay the source of truth.

### 5.8 Single-license invariant

**Rule:** at any moment, in any deployment configuration, exactly one process in the deployment holds an `AgSTKXApplication` instance. Never two.

This drives three concrete design constraints:

#### Mutually-exclusive run modes (boot-time, not feature-toggle)

The WPF app has exactly two startup modes:

| Mode | When | STK location |
|---|---|---|
| **Standalone** (today's MVP4.5) | Air-gapped workstation, no server deployed | Desktop process holds `AgSTKXApplication` via `StkScenarioBackend` |
| **Client** (future, after server is added) | Server is deployed and reachable | Desktop process has **zero STK references** at runtime; talks to server via `HttpScenarioBackend` |

Selected at app startup via configuration (e.g., `appsettings.json`'s `Backend = "InProcess" | "Remote"` or environment variable). **No fallback** between modes — if `Remote` mode can't reach the server, the app surfaces an error and exits. It does NOT silently bootstrap a local STK Engine.

The DI registration encodes this:

```csharp
// In App.OnStartup, after reading config
if (config.Backend == "InProcess")
    services.AddSingleton<IScenarioBackend, StkScenarioBackend>();
else if (config.Backend == "Remote")
    services.AddSingleton<IScenarioBackend, HttpScenarioBackend>();
else
    throw new InvalidOperationException("Backend mode must be 'InProcess' or 'Remote'");
```

`HttpScenarioBackend` exists in the future (§5.2); for MVP4.5 only `InProcess` is selectable. The config switch is added now so the wiring is in place.

#### Server-side: single tenant by design

When the server lands (§5.3), it hosts exactly one `IScenarioBackend` instance backed by one `AgSTKXApplication`. Concurrent HTTP requests serialize on this single root (the `IScenarioBackend` impl is thread-affinitised to its STA thread).

**Multi-user authoring** (multiple operators editing different scenarios simultaneously) is therefore **not supported by the architecture as designed**. Adding it later requires either:

- One server per user, each with its own STK Engine license seat (RFQ Milestone 1 has 2 seats — at most 2 concurrent users), OR
- A queueing layer that serializes multi-user requests onto the single backend (acceptable if user count is small and edits are infrequent).

This is a deliberate limit. Worth surfacing to the customer: the architecture aligns with the licensing budget, not with arbitrary concurrent-user counts.

#### Browser frontend: zero STK

The Cesium browser frontend (§5.5) holds **no STK references** — it consumes CZML from the server and renders via Cesium's native pipeline. STK lives only on the server. The browser's STK Engine license footprint is zero.

#### Runtime guard (today and forever)

Every `IScenarioBackend` impl that wraps `AgSTKXApplication` performs the license check the reference repo does, at construction:

```csharp
if (!_stkApp.IsFeatureAvailable(AgEFeatureCodes.eFeatureCodeGlobeControl))
{
    MessageBox.Show("Required STK license is not available...",
                    "License Error", MessageBoxButton.OK, MessageBoxImage.Stop);
    throw new Exception("STK License not available.");
}
```

This is the only enforcement we ship. Two-process collision is detected by STK itself (HRESULT `0x80040154` from `CoCreateInstance` when the license is already in use); we catch and present the message cleanly. **No file-locks, no port-checks, no programmatic mutual-exclusion machinery** — those are over-engineering for an air-gapped Windows workstation deployment where the operator controls process lifecycle.

#### Spec-documented deployment rule

The MVP4.5 spec doc and the README will state explicitly:

> **One-engine rule.** At any time, at most one EWTSS process — either the standalone desktop app, or the server (if deployed) — may run on a given workstation. Running both simultaneously will fail with an STK license error on whichever started second. To switch from standalone to client mode: stop the desktop app, start the server, change `appsettings.json` `Backend = "Remote"`, restart the desktop app.

---

## 6. Testing impact

### 6.1 Fakes consolidate (6 → 1)

Today's test fakes:

```
Sg.Tests/Fakes/
  FakeAircraftBackend.cs       ← deleted
  FakeFacilityBackend.cs       ← deleted
  FakeAreaTargetBackend.cs     ← deleted
  FakeSensorBackend.cs         ← deleted
  FakeCoverageBackend.cs       ← deleted
  FakeFomBackend.cs            ← deleted
  FakeStkRootService.cs        ← renamed → FakeScenarioBackend.cs
  FakePersistenceService.cs    ← rolled into FakeScenarioBackend
  FakeFileDialogService.cs     ← stays
  FakeInteractionController.cs ← stays
```

`FakeScenarioBackend` exposes typed `GetXxx`/`UpdateXxx` methods plus public test-helpers (`Aircraft`, `Facilities`, etc. as `Dictionary<string, AircraftDto>`) so tests can pre-populate state and inspect after Apply.

```csharp
public sealed class FakeScenarioBackend : IScenarioBackend
{
    public Dictionary<string, AircraftDto>           Aircraft     { get; } = new();
    public Dictionary<string, FacilityDto>           Facilities   { get; } = new();
    public Dictionary<string, AreaTargetDto>         AreaTargets  { get; } = new();
    public Dictionary<string, SensorDto>             Sensors      { get; } = new();
    public Dictionary<string, CoverageDefinitionDto> Coverages    { get; } = new();
    public Dictionary<string, FigureOfMeritDto>      Foms         { get; } = new();

    public AircraftDto GetAircraft(string path) => Aircraft[path];
    public void UpdateAircraft(string path, AircraftDto dto)
    {
        Aircraft[path] = dto;
        ScenarioChanged?.Invoke(this, EventArgs.Empty);
    }
    // ... etc per entity
}
```

### 6.2 New: DTO round-trip tests (locks the JSON contract)

One small fixture proving every contract DTO survives `System.Text.Json` round-trip without loss. Cheap insurance that the contract is browser-shippable.

```csharp
[TestFixture, Category(TestCategories.Unit)]
public class DtoJsonRoundTripTests
{
    [Test]
    public void AircraftDto_round_trips_through_json()
    {
        var dto = new AircraftDto("A1", "#FF0000", true,
            new[] { new WaypointDto(34.2, 74.5, 1500, 100) });
        var json = JsonSerializer.Serialize(dto);
        var back = JsonSerializer.Deserialize<AircraftDto>(json);
        back.Should().BeEquivalentTo(dto);
    }
    // One test per DTO: Facility, AreaTarget, Sensor, Coverage, Fom, EntityNode, ComputeResult
    // 8 tests total
}
```

### 6.3 View-model tests retarget to new contract

Existing VM tests (e.g., `AircraftViewModelTests`) keep their semantics but rewire to `FakeScenarioBackend`:

```csharp
// Before
var back = new FakeAircraftBackend("A1");
var vm = new AircraftViewModel(back);
vm.ColorHex = "#FF0000";
vm.MarkDirty();
vm.ApplyCommand.Execute(null);
back.ColorHex.Should().Be("#FF0000");

// After
var back = new FakeScenarioBackend();
back.Aircraft["A1"] = new AircraftDto("A1", "#00FFFF", true, Array.Empty<WaypointDto>());
var vm = new AircraftViewModel(back, "A1");
vm.ColorHex = "#FF0000";
vm.MarkDirty();
vm.ApplyCommand.Execute(null);
back.Aircraft["A1"].ColorHex.Should().Be("#FF0000");
```

Same test count, same behaviours verified, different wiring. Estimated effort: ~2 hours per entity × 6 = ~1.5 days.

### 6.4 Integration tests retarget — same coverage, new contract

`Scenario34AcceptanceTests` and `StkRootServiceIntegrationTests` continue to drive a real `StkScenarioBackend`. Method names update (`AddEntity` stays; `SetFacilityPosition` → `UpdateFacility(path, dto)`; etc.). Expected post-refactor: 7 integration tests still passing. No new integration tests required for MVP4.5; the perf patterns are validated by the existing visual smoke-test, not by automated tests.

### 6.5 Backend-mode DI test (one new test)

Verifies that `App.OnStartup`'s mode selection wires the right impl:

```csharp
[Test]
public void Backend_InProcess_resolves_StkScenarioBackend()
{
    var services = new ServiceCollection();
    AppStartup.RegisterBackend(services, "InProcess");
    var sp = services.BuildServiceProvider();
    sp.GetRequiredService<IScenarioBackend>().Should().BeOfType<StkScenarioBackend>();
}
```

(Test for `Remote` mode lands when `HttpScenarioBackend` exists.)

### 6.6 Test counts (rough projection)

| Category | Today | After MVP4.5 |
|---|---|---|
| Unit | 79 | ~80–85 (lose some via fake consolidation, add ~8 DTO round-trip + 1 DI mode) |
| Integration | 7 | 7 (retargeted, same coverage) |

Net test count is roughly stable.

### 6.7 What we deliberately don't test

- **Single-license invariant via integration test.** Would require spawning two STK processes on the build agent, which is brittle and license-burning. The runtime guard (`IsFeatureAvailable` check) is enough; manual smoke-test covers the deployment-rule case.
- **Perf patterns via assertions** (e.g., "verify `BeginUpdate` was called inside `UpdateAircraft`"). Implementation-coupled and brittle. The user's smoke-test is the qualitative perf gate; we don't try to automate it.
- **HTTP transport** (since `HttpScenarioBackend` doesn't exist in MVP4.5).

---

## 7. Acceptance criteria

MVP4.5 is complete when:

1. **No regression in functional behaviour.** Every flow exercised by the MVP4 visual smoke-test (place facility/aircraft/area-target via map clicks; edit via property panels; edit via STK drag handles; save/load `.sc` and `.vdf`; compute coverage; close cleanly) works the same way.
2. **Pan/zoom feel matches the reference repo.** Subjective, but qualitatively close enough that the team would not call it "laggy" anymore. Verification: side-by-side test of MVP4.5 vs the reference repo on the same workstation.
3. **Contract surface is JSON-clean.** All DTOs round-trip through `System.Text.Json` without loss (8 unit tests).
4. **One-engine rule observable.** Starting a second `StkScenarioBackend` while one is already alive surfaces a clean license error, not a crash.
5. **All existing unit tests pass after retargeting.** Integration tier intact.
6. **`.editorconfig` rule prevents `using AGI.*` inside `Contracts` namespace.** Verified by deliberately introducing a bad import and confirming the build flags it.

---

## 8. Timeline estimate (single C# dev fluent in WPF + COM)

| Phase | Work | Effort |
|---|---|---|
| 1 | Define Contracts namespace + DTO records + IScenarioBackend interface | 0.5 day |
| 2 | Implement StkScenarioBackend (consolidate ComAccessors + StkRootService + PersistenceService) | 1.5 days |
| 3 | Apply 9 perf patterns inside StkScenarioBackend + StkDisplayHost | 1 day |
| 4 | Migrate the 6 entity view-models to DTO Get/Update | 1 day |
| 5 | Migrate test fakes (delete 6, add FakeScenarioBackend) + retarget VM tests | 1.5 days |
| 6 | DTO JSON round-trip tests + DI mode test + .editorconfig fence | 0.5 day |
| 7 | Visual smoke-test, integration test retarget, README/notes update | 1 day |
| **Total** | | **~7 days** |

Fits inside one calendar week of focused work.

---

## 9. Out of scope (reaffirmed)

- Server / browser / HTTP — all deferred until concrete consumer exists
- Multi-user concerns
- DB layer
- EWTSS-domain entity catalogue change (will arrive in the final EWTSS project, separate spec)
- Authentication / RBAC
- Logging / observability beyond what MVP4 already has (Serilog file)
