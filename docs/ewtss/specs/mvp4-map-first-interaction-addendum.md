# MVP4 — Map-First Interaction Addendum

**Date:** 2026-04-23
**Status:** Approved by user; supersedes form-only authoring flow in the original design spec §4.2
**Amends:** [`mvp4-stk-native-embedded-design.md`](./mvp4-stk-native-embedded-design.md)
**Motivation:** The original design's form-based flow ("click `+ Aircraft`, then type lat/lon in a property panel, then click Apply") was unsatisfying during the Task 5 smoke test. User wants STK-Insight-style click-to-place and drag-handle editing.

---

## 1. Spike findings (all YES)

A spike against STK 12's `CodeSamples/` confirmed all four required capabilities exist in the ActiveX layer, with working reference implementations:

1. **Click → lat/lon** — `control.PickInfo(x, y)` returns `IAgPickInfoData` with `.Lat` / `.Lon` / `.Alt` (degrees) + `.IsLatLonAltValid` guard. Works identically on 3D (`AxAgUiAxVOCntrl`) and 2D (`AxAgUiAx2DCntrl`).  Sample: `Events/Form1.cs:163-185`.
2. **Pick entity identity** — Same `PickInfo` also returns `.ObjPath` (STK full path) + `.IsObjPathValid`. Resolve via `stkRoot.GetObjectFromPath(path)`.  Sample: `PlaceFinder/Form1.cs:250-258`.
3. **Temporary placement visuals** — `IAgStkGraphicsPolylinePrimitive` + `IAgStkGraphicsPointBatchPrimitive` added to the scene's `Primitives` collection. Updated on every `MouseMoveEvent` via `camera.WindowToCartographic("Earth", ref position)` to convert pixel → world. Does NOT pollute the scenario object tree. Sample: `PolygonDrawing/Form1.cs:22-52, 169-210`.
4. **Drag handles on existing entities** — Preferred path: `control.StartObjectEditing(objectPath)` activates STK's built-in waypoint/vertex drag handles. Lifecycle events `OnObjectEditingStart`, `OnObjectEditingApply`, `OnObjectEditingStop`. Sample: `3DObjectEditing/Form1.cs:66-122`.

**Consequences for MVP4:**
- No WPF overlay is needed (the "fragile" approach the v2 design doc §22.2 warned about is sidestepped).
- `StartObjectEditing` is 3D-only and single-entity-at-a-time — 2D editing and multi-handle editing are deferred to post-MVP4.
- Temporary primitives (rubber-band lines, ghost markers) must be removed explicitly on mode-exit; they don't self-clean.

---

## 2. Interaction state machine

A single controller drives all map interaction. It is the gatekeeper between raw ActiveX mouse events and domain mutations.

```
                ┌─────────────┐
                │    Idle     │ ◄──── initial state
                └──────┬──────┘
                       │
   ┌──────────┬────────┴──────────┬────────────────┐
   │          │                   │                │
   ▼          ▼                   ▼                ▼
+ Fac     + Aircraft          + AreaTarget      dbl-click /
click     click               click             "Edit on Map"
   │          │                   │                │
   ▼          ▼                   ▼                ▼
Placing   PlacingAircraft     PlacingAreaTarget   Editing
Facility  (collecting         (collecting          Entity
          waypoints)           polygon verts)     (StartObjectEditing)
   │          │                   │                │
   │          │                   │                │
   ▼ click    ▼ click             ▼ click          ▼ Apply/Stop
(1 click)  add waypt            add vertex       STK handles +
           (Esc/dblclk          (dblclk closes    OnObjectEditingApply
           finalize route)      polygon)          → refresh panel
   │          │                   │                │
   └──────────┴────────┬──────────┴────────────────┘
                       │
                       ▼
                    Idle
```

**States:**

| State | Entry | Exit | Visible hint |
|---|---|---|---|
| `Idle` | initial; after any mode exit | any Add click or dblclick on tree | Status bar: "Ready." |
| `PlacingFacility` | `+ Facility` button click | valid pick → Create → Idle; Esc → Idle | Status bar: "Click on the map to place a facility. Esc to cancel." |
| `PlacingAircraft` | `+ Aircraft` button click | dblclick / Enter finalizes; Esc cancels with object discarded | "Click to add waypoints. Double-click to finish. Esc to cancel." |
| `PlacingAreaTarget` | `+ AreaTarget` button click | dblclick closes polygon; Esc cancels | "Click to add polygon vertices. Double-click to close. Esc to cancel." |
| `EditingEntity` | `StartEditingOnMap(path)` | Apply button (property panel) → `ApplyObjectEditing()`; Cancel → `StopObjectEditing(true)` | "Editing {path} — drag handles, then Apply in the panel." |

**Invariants:**
- Only one state may be active at a time.
- Entering any placement state while already in a placement or editing state first cancels the current state (no cross-state stacking).
- `ScenarioChanged` only fires after a mode commits a mutation — not during in-progress mouse moves.
- Pressing Esc is a universal "back to Idle" — cancels the current placement, discarding any in-progress entity.

---

## 3. Components (new)

### 3.1 `Sg.Mvp4.Domain.Interaction.InteractionMode` (enum)

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

### 3.2 `Sg.Mvp4.Domain.Interaction.IInteractionController` (interface)

The domain-layer contract. View layer (StkDisplayHost) calls into it on mouse events; other view-models observe its state.

```csharp
public interface IInteractionController
{
    InteractionMode Mode { get; }
    string? EditingPath { get; }        // non-null during EditingEntity only
    string StatusHint { get; }          // localised string for status bar

    event EventHandler? ModeChanged;

    // Called by the tree toolbar buttons:
    void BeginPlace(EntityKind kind);

    // Called by StkDisplayHost mouse handlers:
    void OnMapMouseDown(double latDeg, double lonDeg, double altM, bool isValidPick);
    void OnMapMouseMove(double latDeg, double lonDeg, double altM, bool isValidPick);
    void OnMapMouseDblClick(double latDeg, double lonDeg, double altM, bool isValidPick);

    // Called by the object tree (double-click) or property panel button:
    void StartEditingOnMap(string entityPath);

    // Called by property panel Apply button during EditingEntity:
    void ApplyEdit();
    void CancelEdit();

    // Esc key handler:
    void Cancel();
}
```

### 3.3 `Sg.Mvp4.Domain.Interaction.InteractionController` (concrete)

Holds the state and drives the `IStkRootService` mutations. Not COM-aware — COM lives behind `IStkRootService` already.

Unit tests use `FakeStkRootService` to verify:
- `BeginPlace(Facility)` + first valid pick → creates facility at that lat/lon → `Mode == Idle`
- `BeginPlace(Aircraft)` + 3 clicks + dblclick → creates aircraft with 3 waypoints
- `BeginPlace(AreaTarget)` + 3 clicks + dblclick → creates area target with 3 vertices
- `Cancel()` during any placement → Idle, no entities added
- `StartEditingOnMap("Aircraft1")` → `Mode == EditingEntity`, `EditingPath == "Aircraft1"`

### 3.4 `Sg.Mvp4.Domain.Services.IStkRootService` — extensions

`IStkRootService.AddEntity(kind, name, parentPath)` currently creates the entity with STK defaults and returns. To support click-to-place we add:

```csharp
// New methods on IStkRootService:

/// <summary>Sets a facility's lat/lon/alt via IAgFacility.Position.AssignGeodetic.</summary>
void SetFacilityPosition(string path, double latDeg, double lonDeg, double altM);

/// <summary>Appends a waypoint to an aircraft's Route.Waypoints and re-propagates.</summary>
void AppendAircraftWaypoint(string path, double latDeg, double lonDeg, double altM, double speedMps);

/// <summary>Appends a polygon vertex to an area target's pattern.</summary>
void AppendAreaTargetVertex(string path, double latDeg, double lonDeg);

/// <summary>Finalizes an aircraft route (propagates after last waypoint added).</summary>
void FinalizeAircraftRoute(string path);

/// <summary>Resolves the scenario entity at a given pick path (e.g. "*/Aircraft/A1" → "A1").</summary>
string? ResolvePickPath(string stkObjectPath);
```

`FakeStkRootService` mirrors these for unit-testing the controller.

### 3.5 `Sg.Mvp4.App.Views.Shell.StkDisplayHost` — wiring

The host subscribes to `MouseDownEvent`, `MouseMoveEvent`, `MouseUpEvent`, `OnLBtnDblClk` on both 3D and 2D controls. On each event it calls `PickInfo(x, y)` to resolve world coordinates and then forwards to the injected `IInteractionController`.

For placement visualization (rubber-band lines, vertex markers), the host manages a set of `IAgStkGraphicsPolylinePrimitive` / `IAgStkGraphicsPointBatchPrimitive` instances created lazily on first placement start and cleared on mode-exit.

For STK's native drag-handle editing, the host listens to the three `OnObjectEditing*` events on the 3D control and notifies the controller; the controller then updates the tree/panel accordingly.

---

## 4. Tree + panel integration

Changes to existing tasks:

- **Task 5 (done)** — `ObjectTreeView.xaml.cs` toolbar buttons currently call `Vm.AddChildCommand.Execute((kind, null, uniqueName))`. After this addendum, they call `_controller.BeginPlace(kind)`. The `Delete` button and tree-selection behavior are unchanged. Additionally, double-click on a tree node calls `_controller.StartEditingOnMap(node.Path)`.
- **Task 6 (not yet started)** — `PropertyPanelHostView` gains a header row observing `IInteractionController.Mode`. When `Mode == EditingEntity && EditingPath == currentPanelPath`, the Apply button on the panel also commits the STK edit via `controller.ApplyEdit()`.
- **Tasks 7-12 (not yet started)** — Each entity panel shows an "Edit on Map" button for spatial entities (Aircraft, Facility, AreaTarget). Clicking it calls `controller.StartEditingOnMap(path)`. When editing completes (via Apply or external commit), the panel re-reads fields from STK. Non-spatial fields (color, label, FOM type, etc.) continue to use the form's text/combo inputs with no map involvement.
- **Task 15 (Compute)** — unchanged.

---

## 5. UX details

- **Status bar:** a new binding on `IInteractionController.StatusHint` drives `MainStatusBar`'s first text slot. Replaces the Task 3 placeholder `"STK: (not yet connected)"`.
- **Toolbar button state:** `+ Aircraft` / `+ Facility` / `+ AreaTarget` render "pressed" while their corresponding placement mode is active.
- **Keyboard:** `Esc` at any time invokes `controller.Cancel()`. `Enter` during `PlacingAircraft` / `PlacingAreaTarget` finalizes the placement. `Del` while a tree node is selected triggers `RemoveCommand`.
- **Double-click semantics:** on the tree → edit-on-map. On the globe (empty space) during placement → finalize. On the globe (over an entity) while Idle → select + edit-on-map.
- **Right-click:** deferred. Could open a context menu in future iterations.

---

## 6. Scope

**In MVP4:**
- All 5 interaction states (Idle + 3 placement + editing)
- Click-to-place for Aircraft / Facility / AreaTarget with live preview (rubber-band + vertex markers)
- `StartObjectEditing`-based drag editing on 3D for Aircraft / Facility / AreaTarget
- Status-bar hints + Esc/Enter/Del keyboard
- Unit tests for the controller state machine (no STK required — uses `FakeStkRootService` + fake pick inputs)

**Deferred (post-MVP4):**
- 2D map drag editing (`StartObjectEditing` is 3D-only)
- Multi-entity editing at once
- Custom-primitive drag handles (Path B from the spike — only needed if 2D drag becomes a requirement)
- Context-menu actions on the map
- Rubber-banded insertion of waypoints between existing ones
- Snap-to-grid / snap-to-coast features

---

## 7. Risk review

| Risk | Likelihood | Mitigation |
|---|---|---|
| `WindowToCartographic` throws on off-globe cursor during placement | High (every mouse-move) | Controller only acts on `isValidPick == true`; MouseMove handler catches and ignores invalid picks |
| Mouse-move PickInfo causes visible jank | Medium | Throttle MouseMove handling (at most every 16ms); only update rubber-band at throttled rate |
| `StartObjectEditing` double-dispatch if user clicks "Edit on Map" twice | Low | Controller rejects `StartEditingOnMap` when already in `EditingEntity` |
| Primitive cleanup on mode cancel is forgotten | Low | Mode-exit always calls `ClearPlacementVisuals()` — assertion in controller |
| User cancels Aircraft mid-waypoint-placement → partial entity persists | Medium | During `PlacingAircraft` the aircraft is NOT created until finalization; waypoints are held in controller state until commit |

---

## 8. Timeline impact

| Original | Revised |
|---|---|
| Task 5: ObjectTree + VM (done) | Unchanged except tree toolbar now calls controller (1-line change in Task 5B) |
| — | **Task 5B (new): Interaction controller + placement modes** (~1 week) |
| — | **Task 5C (new): Edit-on-map via StartObjectEditing** (~3 days) |
| Task 6: Property panel framework | + Edit-on-Map button integration (~0.5 day) |
| Tasks 7–10: Aircraft/Facility/AreaTarget/Sensor panels | Each +0.5 day for Edit-on-Map button + post-commit refresh |
| Tasks 11–12: Coverage + FOM | Unchanged (no spatial editing) |
| Tasks 13–17 | Unchanged |

Net impact: **+1.5 weeks** to MVP4. Original estimate 5 weeks → revised 6.5 weeks.

Scope preserved; UX significantly better; no architectural compromise.
