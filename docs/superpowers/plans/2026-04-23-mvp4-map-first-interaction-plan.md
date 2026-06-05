# MVP4 — Map-First Interaction Plan (Addendum)

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Extend MVP4 with STK-Insight-style click-to-place and drag-handle editing on the embedded STK globe, per the design addendum.

**Architecture:** An `IInteractionController` state machine in `Sg.Mvp4.Domain.Interaction` gatekeepers all map interaction. `StkDisplayHost` subscribes to ActiveX `MouseDown/Move/Up/DblClk` events, converts each to (lat, lon, alt) via `control.PickInfo(x, y)`, and forwards to the controller. The controller orchestrates `IStkRootService` mutations and temporary scene primitives for placement preview. STK's native `StartObjectEditing` is used for drag-handle edits on existing entities.

**Tech stack (new bits only):** `AGI.STKX` (`AgSTKXApplication`, `IAgPickInfoData`), `IAgStkGraphicsScene.Primitives` (polyline + point-batch primitives), `AxAgUiAxVOCntrl.StartObjectEditing` / `OnObjectEditing*` events.

**Amends:** Original plan [`2026-04-22-mvp4-stk-native-embedded.md`](./2026-04-22-mvp4-stk-native-embedded.md). Inserts **Task 5B** and **Task 5C** between Tasks 5 and 6. Modifies Tasks 6–10 (notes in §Modifications below).

**Design spec:** [`mvp4-map-first-interaction-addendum.md`](../../ewtss/specs/mvp4-map-first-interaction-addendum.md)

---

## File structure — new files (existing files left unchanged unless noted)

```
mvp4/
├── Sg.Mvp4.Domain/
│   ├── Interaction/                           NEW — interaction layer
│   │   ├── InteractionMode.cs                 enum: Idle | PlacingFacility | PlacingAircraft
│   │   │                                            | PlacingAreaTarget | EditingEntity
│   │   ├── IInteractionController.cs          interface (contract in addendum §3.2)
│   │   └── InteractionController.cs           concrete state machine
│   └── Services/
│       ├── IStkRootService.cs                 MODIFIED — adds 5 spatial mutators + ResolvePickPath
│       └── StkRootService.cs                  MODIFIED — COM impl of the 5 new methods
├── Sg.Mvp4.App/
│   └── Views/Shell/
│       ├── StkDisplayHost.xaml.cs             MODIFIED — wires mouse events, placement primitives
│       └── ObjectTreeView.xaml.cs             MODIFIED — toolbar buttons call BeginPlace; dbl-click → StartEditingOnMap
└── Sg.Mvp4.Tests/
    ├── Fakes/FakeStkRootService.cs            MODIFIED — fakes the 5 new methods
    └── Interaction/                           NEW — controller unit tests
        └── InteractionControllerTests.cs
```

---

## Task 5B: Interaction controller + placement modes

**Files:**
- Create: `mvp4/Sg.Mvp4.Domain/Interaction/InteractionMode.cs`
- Create: `mvp4/Sg.Mvp4.Domain/Interaction/IInteractionController.cs`
- Create: `mvp4/Sg.Mvp4.Domain/Interaction/InteractionController.cs`
- Modify: `mvp4/Sg.Mvp4.Domain/Services/IStkRootService.cs` (add 5 methods)
- Modify: `mvp4/Sg.Mvp4.Domain/Services/StkRootService.cs` (implement 5 methods)
- Modify: `mvp4/Sg.Mvp4.Tests/Fakes/FakeStkRootService.cs` (mirror the 5 methods for tests)
- Create: `mvp4/Sg.Mvp4.Tests/Interaction/InteractionControllerTests.cs`
- Modify: `mvp4/Sg.Mvp4.App/App.xaml.cs` (register `IInteractionController`)

**Scope note:** This task does NOT touch `StkDisplayHost.xaml.cs` (no mouse wiring yet) and does NOT modify `ObjectTreeView.xaml.cs`. Those belong to Task 5C. Here we build the domain layer only so controller semantics are unit-testable before any WPF wiring.

- [ ] **Step 1: Enum + interface**

Create `mvp4/Sg.Mvp4.Domain/Interaction/InteractionMode.cs`:

```csharp
namespace Sg.Mvp4.Domain.Interaction;

public enum InteractionMode
{
    Idle,
    PlacingFacility,
    PlacingAircraft,
    PlacingAreaTarget,
    EditingEntity
}
```

Create `mvp4/Sg.Mvp4.Domain/Interaction/IInteractionController.cs`:

```csharp
using Sg.Mvp4.Domain.Models;

namespace Sg.Mvp4.Domain.Interaction;

public interface IInteractionController
{
    InteractionMode Mode { get; }
    string?         EditingPath { get; }
    string          StatusHint { get; }

    event EventHandler? ModeChanged;

    void BeginPlace(EntityKind kind);
    void OnMapMouseDown (double latDeg, double lonDeg, double altM, bool isValidPick);
    void OnMapMouseMove (double latDeg, double lonDeg, double altM, bool isValidPick);
    void OnMapMouseDblClick(double latDeg, double lonDeg, double altM, bool isValidPick);
    void StartEditingOnMap(string entityPath);
    void ApplyEdit();
    void CancelEdit();
    void Cancel();
}
```

- [ ] **Step 2: Extend `IStkRootService` with spatial mutators**

Modify `mvp4/Sg.Mvp4.Domain/Services/IStkRootService.cs`. Append to the interface:

```csharp
void SetFacilityPosition      (string path, double latDeg, double lonDeg, double altM);
void AppendAircraftWaypoint   (string path, double latDeg, double lonDeg, double altM, double speedMps);
void AppendAreaTargetVertex   (string path, double latDeg, double lonDeg);
void FinalizeAircraftRoute    (string path);
string? ResolvePickPath       (string stkObjectPath);   // e.g. "*/Aircraft/A1" → "A1"
```

- [ ] **Step 3: Stub the new methods in `StkRootService.cs`**

Throw `NotImplementedException("Step 4")` for now so the build still works; real COM impls land in Step 4.

- [ ] **Step 4: COM implementations in `StkRootService.cs`**

Implement each method:

```csharp
public void SetFacilityPosition(string path, double latDeg, double lonDeg, double altM)
{
    if (FindComByPath(path) is not IAgFacility fac)
        throw new InvalidOperationException($"Facility not found: '{path}'.");
    fac.Position.AssignGeodetic(latDeg, lonDeg, altM);
}

public void AppendAircraftWaypoint(string path, double latDeg, double lonDeg, double altM, double speedMps)
{
    if (FindComByPath(path) is not IAgAircraft ac)
        throw new InvalidOperationException($"Aircraft not found: '{path}'.");
    // Ensure a great-arc propagator is set (idempotent — STK allows repeat calls).
    ac.SetRouteType(AgEVePropagatorType.ePropagatorGreatArc);
    var route = (IAgVePropagatorGreatArc)ac.Route;
    route.Method = AgEVeWayPtCompMethod.eDetermineTimeAccFromVel;
    var wp = route.Waypoints.Add();
    wp.Latitude  = latDeg;
    wp.Longitude = lonDeg;
    wp.Altitude  = altM;
    wp.Speed     = speedMps <= 0 ? 100 : speedMps;
}

public void FinalizeAircraftRoute(string path)
{
    if (FindComByPath(path) is not IAgAircraft ac) return;
    ((IAgVePropagatorGreatArc)ac.Route).Propagate();
}

public void AppendAreaTargetVertex(string path, double latDeg, double lonDeg)
{
    if (FindComByPath(path) is not IAgAreaTarget at)
        throw new InvalidOperationException($"AreaTarget not found: '{path}'.");
    at.AreaType = AgEAreaType.ePattern;
    var pattern = (IAgAreaTypePatternCollection)at.AreaTypeData;
    pattern.Add(latDeg, lonDeg);
}

public string? ResolvePickPath(string stkObjectPath)
{
    if (string.IsNullOrWhiteSpace(stkObjectPath)) return null;
    // STK paths look like "*/Aircraft/A1" or "*/Aircraft/A1/Sensor/S1".
    // Strip "*/Class/" prefix; keep name/name2/...
    var parts = stkObjectPath.Split('/', StringSplitOptions.RemoveEmptyEntries);
    if (parts.Length < 2) return null;
    // Alternating "Class" / "Name" pairs after "*"; we want just the names.
    var names = new List<string>();
    for (var i = 1; i < parts.Length; i += 2)
        if (i + 1 < parts.Length) names.Add(parts[i + 1]);
    return names.Count == 0 ? null : string.Join("/", names);
}
```

Note: the exact STK COM names (`AgEVePropagatorType`, `AgEVeWayPtCompMethod`, `IAgAreaTypePatternCollection`) were introduced in the original plan Task 7 and may need small adjustments if they differ from the installed `AGI.STKObjects.Interop.dll`. If a name doesn't resolve, use VS Object Browser or reflection to find the correct equivalent — the shape is stable.

- [ ] **Step 5: Mirror in `FakeStkRootService`**

Add instance fields + methods to `mvp4/Sg.Mvp4.Tests/Fakes/FakeStkRootService.cs`:

```csharp
private readonly Dictionary<string, (double lat, double lon, double alt)>  _facilityPos = new();
private readonly Dictionary<string, List<(double lat, double lon, double alt, double speed)>> _aircraftWaypoints = new();
private readonly Dictionary<string, List<(double lat, double lon)>>        _areaVerts = new();
private readonly HashSet<string> _finalizedAircraft = new();

public void SetFacilityPosition(string path, double lat, double lon, double alt)
    => _facilityPos[path] = (lat, lon, alt);

public void AppendAircraftWaypoint(string path, double lat, double lon, double alt, double speed)
    => (_aircraftWaypoints[path] = _aircraftWaypoints.GetValueOrDefault(path, new()))
       .Add((lat, lon, alt, speed));

public void AppendAreaTargetVertex(string path, double lat, double lon)
    => (_areaVerts[path] = _areaVerts.GetValueOrDefault(path, new()))
       .Add((lat, lon));

public void FinalizeAircraftRoute(string path) => _finalizedAircraft.Add(path);

public string? ResolvePickPath(string stkObjectPath)
{
    if (string.IsNullOrWhiteSpace(stkObjectPath)) return null;
    var parts = stkObjectPath.Split('/', StringSplitOptions.RemoveEmptyEntries);
    if (parts.Length < 2) return null;
    var names = new List<string>();
    for (var i = 1; i < parts.Length; i += 2)
        if (i + 1 < parts.Length) names.Add(parts[i + 1]);
    return names.Count == 0 ? null : string.Join("/", names);
}

// Test helpers:
public IReadOnlyDictionary<string, (double, double, double)> FacilityPositions => _facilityPos;
public IReadOnlyDictionary<string, List<(double, double, double, double)>> AircraftWaypoints => _aircraftWaypoints;
public IReadOnlyDictionary<string, List<(double, double)>> AreaVertices => _areaVerts;
public IReadOnlySet<string> FinalizedAircraft => _finalizedAircraft;
```

- [ ] **Step 6: Write failing controller tests**

Create `mvp4/Sg.Mvp4.Tests/Interaction/InteractionControllerTests.cs`:

```csharp
using FluentAssertions;
using NUnit.Framework;
using Sg.Mvp4.Domain.Interaction;
using Sg.Mvp4.Domain.Models;
using Sg.Mvp4.Tests.Fakes;

namespace Sg.Mvp4.Tests.Interaction;

[TestFixture, Category(TestCategories.Unit)]
public class InteractionControllerTests
{
    private FakeStkRootService _svc = null!;
    private InteractionController _c = null!;

    [SetUp]
    public void SetUp()
    {
        _svc = new FakeStkRootService();
        _svc.NewScenario("T", DateTime.UtcNow, DateTime.UtcNow.AddHours(1));
        _c   = new InteractionController(_svc);
    }

    [Test]
    public void Starts_in_Idle()
    {
        _c.Mode.Should().Be(InteractionMode.Idle);
        _c.EditingPath.Should().BeNull();
    }

    [Test]
    public void BeginPlace_Facility_enters_PlacingFacility_with_helpful_hint()
    {
        _c.BeginPlace(EntityKind.Facility);
        _c.Mode.Should().Be(InteractionMode.PlacingFacility);
        _c.StatusHint.Should().Contain("facility");
    }

    [Test]
    public void Facility_placement_creates_entity_at_click_location_then_returns_to_Idle()
    {
        _c.BeginPlace(EntityKind.Facility);
        _c.OnMapMouseDown(34.2, 74.5, 0.0, isValidPick: true);

        _svc.Children.Should().ContainSingle(n => n.Kind == EntityKind.Facility);
        var fac = _svc.Children.Single();
        _svc.FacilityPositions[fac.Path].Should().Be((34.2, 74.5, 0.0));
        _c.Mode.Should().Be(InteractionMode.Idle);
    }

    [Test]
    public void Facility_placement_with_invalid_pick_stays_in_placement_mode()
    {
        _c.BeginPlace(EntityKind.Facility);
        _c.OnMapMouseDown(0, 0, 0, isValidPick: false);
        _c.Mode.Should().Be(InteractionMode.PlacingFacility);
        _svc.Children.Should().BeEmpty();
    }

    [Test]
    public void Aircraft_placement_collects_waypoints_until_dblclick_finalizes()
    {
        _c.BeginPlace(EntityKind.Aircraft);
        _c.OnMapMouseDown(34.2, 74.5, 1500, isValidPick: true);
        _c.OnMapMouseDown(36.7, 77.1, 1500, isValidPick: true);
        _c.OnMapMouseDblClick(38.0, 78.5, 1500, isValidPick: true);

        _svc.Children.Should().ContainSingle(n => n.Kind == EntityKind.Aircraft);
        var ac = _svc.Children.Single();
        _svc.AircraftWaypoints[ac.Path].Should().HaveCount(3);
        _svc.FinalizedAircraft.Should().Contain(ac.Path);
        _c.Mode.Should().Be(InteractionMode.Idle);
    }

    [Test]
    public void AreaTarget_placement_collects_vertices_until_dblclick_closes_polygon()
    {
        _c.BeginPlace(EntityKind.AreaTarget);
        _c.OnMapMouseDown(34.0, 74.0, 0, isValidPick: true);
        _c.OnMapMouseDown(36.0, 74.0, 0, isValidPick: true);
        _c.OnMapMouseDown(36.0, 76.0, 0, isValidPick: true);
        _c.OnMapMouseDblClick(34.0, 76.0, 0, isValidPick: true);

        _svc.Children.Should().ContainSingle(n => n.Kind == EntityKind.AreaTarget);
        var at = _svc.Children.Single();
        _svc.AreaVertices[at.Path].Should().HaveCount(4);
        _c.Mode.Should().Be(InteractionMode.Idle);
    }

    [Test]
    public void Cancel_during_Aircraft_placement_discards_partial_entity()
    {
        _c.BeginPlace(EntityKind.Aircraft);
        _c.OnMapMouseDown(34.2, 74.5, 1500, isValidPick: true);
        _c.Cancel();

        _c.Mode.Should().Be(InteractionMode.Idle);
        _svc.Children.Should().BeEmpty();
    }

    [Test]
    public void StartEditingOnMap_enters_EditingEntity_with_path()
    {
        _svc.AddEntity(EntityKind.Aircraft, "A1", null);
        _c.StartEditingOnMap("A1");
        _c.Mode.Should().Be(InteractionMode.EditingEntity);
        _c.EditingPath.Should().Be("A1");
    }

    [Test]
    public void ModeChanged_fires_on_every_transition()
    {
        var count = 0;
        _c.ModeChanged += (_, _) => count++;

        _c.BeginPlace(EntityKind.Facility);                                   // 1: Idle → PlacingFacility
        _c.OnMapMouseDown(10, 10, 0, isValidPick: true);                      // 2: PlacingFacility → Idle
        _c.StartEditingOnMap(_svc.Children.Single().Path);                    // 3: Idle → EditingEntity
        _c.CancelEdit();                                                       // 4: EditingEntity → Idle

        count.Should().Be(4);
    }

    [Test]
    public void Beginning_new_placement_while_in_placement_cancels_current()
    {
        _c.BeginPlace(EntityKind.Aircraft);
        _c.OnMapMouseDown(34, 74, 1500, isValidPick: true);
        _c.BeginPlace(EntityKind.Facility);   // should cancel the aircraft

        _c.Mode.Should().Be(InteractionMode.PlacingFacility);
        _svc.Children.Should().BeEmpty();   // aircraft was discarded
    }
}
```

- [ ] **Step 7: Implement `InteractionController`**

Create `mvp4/Sg.Mvp4.Domain/Interaction/InteractionController.cs`:

```csharp
using Sg.Mvp4.Domain.Models;
using Sg.Mvp4.Domain.Services;

namespace Sg.Mvp4.Domain.Interaction;

public sealed class InteractionController : IInteractionController
{
    private readonly IStkRootService _stk;

    private string? _pendingEntityPath;   // aircraft/areaTarget being actively built
    private int     _pendingPointCount;   // counts clicks during placement

    public InteractionMode Mode { get; private set; } = InteractionMode.Idle;
    public string? EditingPath { get; private set; }

    public string StatusHint => Mode switch
    {
        InteractionMode.Idle              => "Ready.",
        InteractionMode.PlacingFacility   => "Click on the map to place a facility. Esc to cancel.",
        InteractionMode.PlacingAircraft   => "Click to add waypoints. Double-click to finish. Esc to cancel.",
        InteractionMode.PlacingAreaTarget => "Click to add polygon vertices. Double-click to close. Esc to cancel.",
        InteractionMode.EditingEntity     => $"Editing {EditingPath} — drag handles, then Apply in the panel.",
        _ => ""
    };

    public event EventHandler? ModeChanged;

    public InteractionController(IStkRootService stk) { _stk = stk; }

    public void BeginPlace(EntityKind kind)
    {
        ResetPlacementState();
        Mode = kind switch
        {
            EntityKind.Facility           => InteractionMode.PlacingFacility,
            EntityKind.Aircraft           => InteractionMode.PlacingAircraft,
            EntityKind.AreaTarget         => InteractionMode.PlacingAreaTarget,
            _ => throw new ArgumentException($"Placement not supported for {kind}.", nameof(kind))
        };
        RaiseModeChanged();
    }

    public void OnMapMouseDown(double lat, double lon, double alt, bool isValidPick)
    {
        if (!isValidPick) return;

        switch (Mode)
        {
            case InteractionMode.PlacingFacility:
                var fName = UniqueName("Facility", EntityKind.Facility);
                _stk.AddEntity(EntityKind.Facility, fName, null);
                _stk.SetFacilityPosition(fName, lat, lon, alt);
                ReturnToIdle();
                break;

            case InteractionMode.PlacingAircraft:
                if (_pendingEntityPath is null)
                {
                    var aName = UniqueName("Aircraft", EntityKind.Aircraft);
                    _stk.AddEntity(EntityKind.Aircraft, aName, null);
                    _pendingEntityPath = aName;
                }
                _stk.AppendAircraftWaypoint(_pendingEntityPath, lat, lon, alt > 0 ? alt : 1500, 100);
                _pendingPointCount++;
                break;

            case InteractionMode.PlacingAreaTarget:
                if (_pendingEntityPath is null)
                {
                    var tName = UniqueName("AreaTarget", EntityKind.AreaTarget);
                    _stk.AddEntity(EntityKind.AreaTarget, tName, null);
                    _pendingEntityPath = tName;
                }
                _stk.AppendAreaTargetVertex(_pendingEntityPath, lat, lon);
                _pendingPointCount++;
                break;
        }
    }

    public void OnMapMouseMove(double lat, double lon, double alt, bool isValidPick)
    {
        // No state change on move; StkDisplayHost uses the latest coords to update preview primitives.
    }

    public void OnMapMouseDblClick(double lat, double lon, double alt, bool isValidPick)
    {
        switch (Mode)
        {
            case InteractionMode.PlacingAircraft when _pendingEntityPath is not null:
                if (isValidPick)
                {
                    _stk.AppendAircraftWaypoint(_pendingEntityPath, lat, lon, alt > 0 ? alt : 1500, 100);
                    _pendingPointCount++;
                }
                if (_pendingPointCount >= 2)
                {
                    _stk.FinalizeAircraftRoute(_pendingEntityPath);
                    ReturnToIdle();
                }
                break;

            case InteractionMode.PlacingAreaTarget when _pendingEntityPath is not null:
                if (isValidPick)
                {
                    _stk.AppendAreaTargetVertex(_pendingEntityPath, lat, lon);
                    _pendingPointCount++;
                }
                if (_pendingPointCount >= 3)
                    ReturnToIdle();
                break;
        }
    }

    public void StartEditingOnMap(string entityPath)
    {
        if (Mode != InteractionMode.Idle) Cancel();
        EditingPath = entityPath;
        Mode = InteractionMode.EditingEntity;
        RaiseModeChanged();
    }

    public void ApplyEdit()
    {
        // Task 5C wires the StartObjectEditing/ApplyObjectEditing COM bridge.
        EditingPath = null;
        ReturnToIdle();
    }

    public void CancelEdit()
    {
        EditingPath = null;
        ReturnToIdle();
    }

    public void Cancel()
    {
        // Discard any partially-built aircraft/areaTarget.
        if (_pendingEntityPath is not null)
        {
            try { _stk.RemoveEntity(_pendingEntityPath); } catch { /* best-effort */ }
        }
        ResetPlacementState();
        EditingPath = null;
        ReturnToIdle();
    }

    // ---------------- helpers ----------------

    private void ReturnToIdle()
    {
        ResetPlacementState();
        Mode = InteractionMode.Idle;
        RaiseModeChanged();
    }

    private void ResetPlacementState()
    {
        _pendingEntityPath = null;
        _pendingPointCount = 0;
    }

    private void RaiseModeChanged() => ModeChanged?.Invoke(this, EventArgs.Empty);

    private string UniqueName(string baseName, EntityKind kind)
    {
        var taken = new HashSet<string>(_stk.Children.Select(c => c.Name));
        for (var i = 1; i <= 10_000; i++)
        {
            var candidate = $"{baseName}{i}";
            if (!taken.Contains(candidate)) return candidate;
        }
        throw new InvalidOperationException("Could not generate unique name.");
    }
}
```

Test note: in `Cancel()`, `_stk.RemoveEntity` may throw if the partial entity was never actually added (edge case). The `try/catch` swallows this; the state is reset either way.

- [ ] **Step 8: Register in DI**

In `mvp4/Sg.Mvp4.App/App.xaml.cs` (inside `OnStartup`, before `MainWindow` registration):

```csharp
services.AddSingleton<IInteractionController, InteractionController>();
```

Add `using Sg.Mvp4.Domain.Interaction;` at the top.

- [ ] **Step 9: Run tests**

```bash
cd mvp4 && dotnet test --filter Category=Unit
```

Expected: **30 tests passing** (20 prior + 10 new).

```bash
cd mvp4 && dotnet build Sg.Mvp4.sln
```

Expected: 0 warnings, 0 errors.

- [ ] **Step 10: Commit**

```bash
git add mvp4/Sg.Mvp4.Domain/Interaction \
        mvp4/Sg.Mvp4.Domain/Services \
        mvp4/Sg.Mvp4.Tests/Interaction \
        mvp4/Sg.Mvp4.Tests/Fakes/FakeStkRootService.cs \
        mvp4/Sg.Mvp4.App/App.xaml.cs
git commit -m "feat(mvp4): interaction controller + placement state machine + spatial mutators on IStkRootService"
```

---

## Task 5C: Wire map clicks + tree double-click + placement visuals

**Files:**
- Modify: `mvp4/Sg.Mvp4.App/Views/Shell/StkDisplayHost.xaml.cs` (mouse event wiring + placement preview primitives)
- Modify: `mvp4/Sg.Mvp4.App/Views/Shell/ObjectTreeView.xaml.cs` (toolbar buttons call `BeginPlace`; dbl-click calls `StartEditingOnMap`)
- Modify: `mvp4/Sg.Mvp4.App/MainWindow.xaml` (status bar binds `StatusHint` from controller)
- Modify: `mvp4/Sg.Mvp4.Domain/ViewModels/MainWindowViewModel.cs` (exposes `StatusHint` by forwarding from `IInteractionController`)

- [ ] **Step 1: Inject `IInteractionController` into `StkDisplayHost`**

Constructor signature becomes `(IStkRootService stk, IInteractionController controller)`. Store `controller` in a field.

- [ ] **Step 2: Wire ActiveX mouse events**

After `Host3D.Child = _globe3D;` and `Host2D.Child = _map2D;`, subscribe:

```csharp
_globe3D.MouseDownEvent    += (s, e) => ForwardMouseDown((AxAGI.STKX.AxAgUiAxVOCntrl)s, e.x, e.y);
_globe3D.MouseMoveEvent    += (s, e) => ForwardMouseMove((AxAGI.STKX.AxAgUiAxVOCntrl)s, e.x, e.y);
_globe3D.OnLBtnDblClk      += (s, e) => ForwardDblClick ((AxAGI.STKX.AxAgUiAxVOCntrl)s, e.x, e.y);

_map2D.MouseDownEvent      += (s, e) => ForwardMouseDown2D(_map2D, e.x, e.y);
_map2D.MouseMoveEvent      += (s, e) => ForwardMouseMove2D(_map2D, e.x, e.y);
_map2D.OnLBtnDblClk        += (s, e) => ForwardDblClick2D (_map2D, e.x, e.y);
```

Exact event-arg field names may differ; consult the generated wrappers via IntelliSense. The pattern is: `e.x`, `e.y` are control-relative pixel coordinates.

Helper methods (implementation):

```csharp
private void ForwardMouseDown(AxAgUiAxVOCntrl ctl, int x, int y)
{
    var pi = ctl.PickInfo(x, y);
    _controller.OnMapMouseDown(pi.Lat, pi.Lon, pi.Alt, pi.IsLatLonAltValid);
}

private void ForwardMouseMove(AxAgUiAxVOCntrl ctl, int x, int y)
{
    // Throttle: ignore moves closer than 16ms apart (controller decides what to do with them).
    if (Environment.TickCount - _lastMoveTick < 16) return;
    _lastMoveTick = Environment.TickCount;
    var pi = ctl.PickInfo(x, y);
    _controller.OnMapMouseMove(pi.Lat, pi.Lon, pi.Alt, pi.IsLatLonAltValid);
}

private void ForwardDblClick(AxAgUiAxVOCntrl ctl, int x, int y)
{
    var pi = ctl.PickInfo(x, y);
    _controller.OnMapMouseDblClick(pi.Lat, pi.Lon, pi.Alt, pi.IsLatLonAltValid);
}
```

Same for the 2D helpers with `AxAgUiAx2DCntrl`. Add `private int _lastMoveTick = 0;` field.

- [ ] **Step 3: Placement visuals (rubber-band polyline + vertex dots)**

On `controller.ModeChanged`, create/dispose scene primitives:
- `PlacingAircraft` or `PlacingAreaTarget` → create an `IAgStkGraphicsPolylinePrimitive` (the in-progress line) and an `IAgStkGraphicsPointBatchPrimitive` (vertex markers).
- On exit → remove them from `scene.Primitives`.

Follow `PolygonDrawing/Form1.cs:22-52` for primitive construction and `:169-210` for update-on-mouse-move (`polyline.SetCartographic(AgECoordinateFrame.eCoordinateFrameCentralBody, array_of_lat_lon_alt)`).

Given the sample patterns, this is ~60 lines of code. Keep it contained in `StkDisplayHost` private methods `_createPlacementPrimitives()` / `_updateRubberBand(lat, lon, alt)` / `_disposePlacementPrimitives()`.

- [ ] **Step 4: Object-editing lifecycle (`StartObjectEditing` wiring)**

Subscribe to the 3D control's editing lifecycle events:

```csharp
_globe3D.OnObjectEditingStart += ...;
_globe3D.OnObjectEditingApply += (_, _) => _controller.ApplyEdit();
_globe3D.OnObjectEditingStop  += ...;
```

On `controller.ModeChanged` → `EditingEntity`: call `_globe3D.StartObjectEditing(controller.EditingPath!)`. On `Idle` from `EditingEntity`: call `_globe3D.StopObjectEditing(false)` if not already stopped.

Follow `3DObjectEditing/Form1.cs:66-122` for signatures.

- [ ] **Step 5: Update `ObjectTreeView.xaml.cs`**

Change constructor to accept `IInteractionController controller`. Replace:

```csharp
private void AddAtRoot(EntityKind kind, string baseName)
{
    var name = UniqueName(baseName, Vm.Nodes.Select(n => n.Name));
    Vm.AddChildCommand.Execute((kind, null, name));
}
```

with:

```csharp
private void AddAtRoot(EntityKind kind, string baseName)
{
    _controller.BeginPlace(kind);
}
```

(The unique-name logic now lives in `InteractionController`.)

Add a `TreeView.MouseDoubleClick` handler:

```csharp
private void Tree_MouseDoubleClick(object sender, MouseButtonEventArgs e)
{
    if (Vm.SelectedPath is string path)
        _controller.StartEditingOnMap(path);
}
```

Update the XAML to wire `MouseDoubleClick="Tree_MouseDoubleClick"` on `<TreeView>`.

- [ ] **Step 6: Status bar binding**

In `mvp4/Sg.Mvp4.Domain/ViewModels/MainWindowViewModel.cs`, add:

```csharp
private readonly IInteractionController _controller;

public string StatusHint => _controller.StatusHint;

// In ctor:
_controller = controller;
_controller.ModeChanged += (_, _) => OnPropertyChanged(nameof(StatusHint));
```

Update ctor parameters to include `IInteractionController controller`.

In `MainWindow.xaml`, replace the literal `"STK: (not yet connected)"` StatusBarItem with:

```xml
<StatusBarItem Content="{Binding StatusHint}" />
```

- [ ] **Step 7: Run tests**

```bash
cd mvp4 && dotnet test --filter Category=Unit
```

Expected: still **30 passing** (controller tests unchanged; no new tests added for the View wiring — it's not unit-testable without a WPF test host).

```bash
cd mvp4 && dotnet build Sg.Mvp4.sln
```

Expected: 0 warnings, 0 errors.

- [ ] **Step 8: Commit**

```bash
git add mvp4/Sg.Mvp4.App/Views/Shell mvp4/Sg.Mvp4.App/MainWindow.xaml mvp4/Sg.Mvp4.Domain/ViewModels/MainWindowViewModel.cs
git commit -m "feat(mvp4): wire map clicks + tree dbl-click + placement visuals + StartObjectEditing bridge"
```

- [ ] **Step 9: User smoke-test** (document in the PR description for reviewer):

On the user's machine:
1. `dotnet run --project Sg.Mvp4.App`
2. Click `+ Aircraft` → status bar reads "Click to add waypoints...". Click on map 3 times → dots appear at each click + line between them. Double-click → aircraft committed with a visible track.
3. Click `+ Facility` → click on map → facility appears at that lat/lon.
4. Click `+ AreaTarget` → click 4 corners → double-click → polygon drawn on map.
5. Esc during any placement → placement discarded, globe returns to clean state.
6. Double-click an aircraft in the tree → status bar reads "Editing..." → STK's native drag handles appear on the aircraft's waypoints.

---

## Modifications to Tasks 6–10 in the original plan

These tasks stay structurally the same, with small additions to integrate map-first editing:

**Task 6 (PropertyPanelHost + EntityViewModelBase):**
- `PropertyPanelHostViewModel` additionally observes `IInteractionController.ModeChanged`. When `Mode == EditingEntity` and the selected entity's path matches `EditingPath`, the panel displays a "Commit Edit" indicator (e.g., highlighted Apply button) to make it clear the pending Apply will commit the STK drag edits.
- No schema change to `EntityViewModelBase`. The "Edit on Map" surface lives in concrete panels (Tasks 7–10).

**Task 7 (AircraftPanel), Task 8 (FacilityPanel), Task 9 (AreaTargetPanel):**
- Each panel adds a secondary button next to "Reset": **"Edit on Map"**.
- The button's command calls `_controller.StartEditingOnMap(entityPath)`.
- When `controller.ModeChanged` fires and the panel was in edit mode, it re-reads all fields from COM (since STK's drag may have mutated waypoints / polygon vertices / facility position).

**Task 10 (SensorPanel):**
- Sensors have no 3D spatial handles that `StartObjectEditing` exposes (they attach to the parent). No "Edit on Map" button.

**Task 11 (CoveragePanel), Task 12 (FomPanel):**
- Unchanged — no spatial editing.

---

## Self-review summary

Before dispatching Task 5B, verify:
- The 5 new `IStkRootService` methods match the COM types actually exported by `AGI.STKObjects.Interop.dll` (will surface as compile errors if not)
- `InteractionController`'s minimum-points rules (2 for aircraft, 3 for area target) match the STK requirements for a valid route / polygon. Aircraft with 1 waypoint will compute-propagate but not animate; area target with 2 vertices won't render as a polygon.
- `FakeStkRootService` test helpers expose state in a form the controller tests can reach without exposing COM internals.

Sub-agent execution notes:
- Task 5B is **Domain + Tests only** — entirely unit-testable, no STK required in the sandbox. Safe for subagent execution with full validation.
- Task 5C is **WPF wiring + COM events** — cannot be fully unit-tested. Subagent validates `dotnet build` clean; user validates via visual smoke-test.
