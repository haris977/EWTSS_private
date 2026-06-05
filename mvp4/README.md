# EWTSS MVP4 — STK-Native Embedded Authoring

Single-process C# WPF desktop application that authors, computes, and visualises
STK 12 scenarios using STK's embedded ActiveX globe as the sole renderer. Sibling
to `mvp/` (MVP1), `mvp2/`, `mvp3/`. Completely different frontend stack from those:
no Cesium, no Python, no server.

## Architecture at a glance

```
Sg.Mvp4.App.exe   (C# WPF · .NET 8 · Windows-only)
    │
    │   +-- MainWindow
    │        +-- ObjectTreeView          (TreeView + Add/Delete toolbar +
    │        │                            map-first placement triggers)
    │        +-- StkDisplayHost          (WindowsFormsHost → ActiveX 3D + 2D)
    │        +-- PropertyPanelHostView   (DataTemplate per EntityKind)
    │
    +-- IInteractionController ── orchestrates click-to-place + StartObjectEditing
    │
    +-- IStkRootService ──→ AgStkObjectRoot (COM, out-of-process STK 12)

Scenario files: C:\EWTSS\mvp4\scenarios\<name>\<name>.sc (+ .vdf via Save As)
Logs:           C:\EWTSS\mvp4\logs\mvp4-YYYYMMDD.log
```

## Supported entity types

Aircraft · Facility · AreaTarget · Sensor (Simple Conic) · CoverageDefinition · FigureOfMerit.

## Map-first interaction

- `+ Aircraft / + Facility / + AreaTarget` toolbar buttons enter a placement mode.
  Left-click on the globe to add waypoints / vertices. Yellow rubber-band polyline
  previews the in-progress shape; orange-red dots mark committed points.
- **Press `Enter` to finish** (aircraft route, polygon close, edit commit).
  **Press `Esc` to cancel** (placement abort, edit revert).
- Right-click is consumed by STK for camera operations on the 3D globe and is
  not available as a finalize gesture.
- Facility placement is single-click (no Enter needed).
- Double-click an entity in the tree to enter STK's native drag-handle edit mode
  (Aircraft / Facility / AreaTarget; Sensor / Coverage / FOM are not directly
  drag-editable on the map — edit those via the right-pane property panel).
- After dragging an edit handle and releasing, the panel auto-refreshes from STK
  and `Apply` enables; click `Apply` (or press `Enter`) to commit.

### Keyboard shortcuts

| Key | Mode | Action |
|---|---|---|
| `Enter` | Placing aircraft / area target | Finalise with collected waypoints / vertices |
| `Enter` | Editing entity | Apply current handle positions, exit edit mode |
| `Esc` | Any active mode (placing or editing) | Cancel; revert in-progress changes |

## Prerequisites

- Windows 11
- STK 12 installed at default path (`C:\Program Files\AGI\STK 12\`) and COM-registered.
  Required PIAs at `C:\Program Files\AGI\STK 12\bin\Primary Interop Assemblies\`:
  AGI.STKObjects.Interop.dll, AGI.STKUtil.Interop.dll, AGI.STKVgt.Interop.dll,
  AGI.STKX.Interop.dll, AGI.STKX.Controls.Interop.dll, AGI.STKGraphics.Interop.dll,
  AxAGI.STKX.Interop.dll
- .NET 8 SDK (or 9 SDK that targets net8.0-windows)
- Visual Studio 2022 (17.8+) for editing

## Build

```
cd mvp4
dotnet build
```

## Test

Unit tier (no STK — runs in CI):

```
dotnet test --filter Category=Unit
```

Integration + acceptance tier (requires STK 12):

```
dotnet test --filter Category=Integration
```

## Run

```
dotnet run --project Sg.Mvp4.App
```

For best performance: assign the `Sg.Mvp4.App.exe` to use the dedicated GPU via
Windows Settings → System → Display → Graphics. Without this, Windows defaults
to the integrated GPU and the STK globe interaction will be sluggish.

Release build is faster than Debug:

```
dotnet run --project Sg.Mvp4.App --configuration Release
```

## End-to-end walkthrough

1. Launch — splash → main window with empty `Untitled` scenario.
2. `File > New Scenario…` — name + start/stop UTC.
3. Place an Aircraft: click `+ Aircraft` toolbar → left-click on globe to add
   waypoints → press `Enter` to finalize. Yellow rubber-band shows the route
   preview, orange-red dots mark committed waypoints. Press `Esc` to cancel.
4. Place a Facility: click `+ Facility` → single left-click on globe to commit.
5. Place an AreaTarget: click `+ AreaTarget` → left-click 3+ corners → press
   `Enter` to close polygon. Press `Esc` to cancel.
6. Add a Sensor under Aircraft: select Aircraft in tree → use the property panel
   (or right-click context menu when implemented) → adjust half-angle / pointing.
7. Add a CoverageDefinition + FigureOfMerit: use the property panel to set grid
   bounds, assets list, FOM type/statistic.
8. `Compute > Compute All Coverage` → STK colors the FOM grid natively.
9. Scrub STK's native time slider — aircraft animates, sensor cones attach.
10. `File > Save As` → pick `.sc` or `.vdf` (with optional password).
11. Open the saved file in STK Desktop / Insight to verify round-trip compatibility.

## MVP4.5 status (DTO boundary + perf)

This branch (`feat/mvp4.5-dto-boundary`) refactors MVP4 in two ways:

1. **STK integration patterns** lifted from Constelli's `EWTSS_CSP_POC` reference repo:
   `CreateControl()` before `LoadVDF`, `Application.DoEvents()` after every COM
   mutation, `BeginUpdate`/`EndUpdate` batching, `dynamic` typing for COM, `object?`
   for `AgSTKXApplication`, and `GetOcx().ScreenXYToLatLon` for 2D picks.
2. **DTO boundary** between view-models and the STK service. View-models depend on
   `IScenarioBackend` (in `Sg.Domain.Contracts`) and never see COM types
   directly. Today's only impl is `StkScenarioBackend` (in-process COM); a future
   `HttpScenarioBackend` for browser-frontend deployments slots in via the same
   contract.

### One-engine rule

At any time, at most one EWTSS process — either the standalone desktop app, or the
server (if deployed) — may run on a given workstation. Running both simultaneously
will fail with an STK license error on whichever started second. To switch from
standalone to client mode: stop the desktop app, set `MVP4_BACKEND=Remote` in the
environment, restart. (Remote mode is reserved for the future ASP.NET Core server;
not implemented in MVP4.5.)

### What changed vs MVP4

- `IStkRootService` + 6 `IXxxBackend` interfaces deleted; replaced by single
  `IScenarioBackend` with per-entity `Get<Kind>(path)` / `Update<Kind>(path, dto)`
  methods.
- Click-to-place placement now accumulates a DTO locally during clicks; STK is
  touched once at finalize (instead of per-click `AppendXxx` calls).
- `FakeScenarioBackend` consolidates 6 prior entity-specific fakes.
- New: 8 DTO JSON round-trip tests + namespace-fence test guard contract surface.

### Smooth-pan behaviour notes

Empirical findings while diagnosing pan / placement / editing on real STK 12:

- **No permanent COM event subscriptions on STK ActiveX controls.** Every
  subscribed `MouseDownEvent` / `MouseMoveEvent` / `OnObjectEditing*` adds COM
  marshalling overhead per render frame; on this STK build it manifested as a
  1–2 s delay after every pan-release. The `StkDisplayHost` now subscribes
  on-demand: mouse events only while a placement mode is active, editing events
  only while in `EditingEntity` mode. Idle pan has zero subscribers — same as
  the reference `EWTSS_CSP_POC` repo.
- **STK fires `DblClick` on every single click.** Verified via diagnostic log;
  the event isn't a true "double-click" notification. We don't subscribe.
  Real double-clicks are detected from `MouseDownEvent` timestamp deltas using
  `SystemInformation.DoubleClickTime` / `DoubleClickSize`.
- **STK consumes right-click on the 3D globe** for camera operations and never
  raises a `MouseDownEvent` with `button == 2`. Right-click as a finalize
  gesture is therefore unreachable; finalisation is keyboard-driven (`Enter`).
- **`StartObjectEditing` requires an explicit `Refresh()` afterwards** for
  drag handles to actually paint on this build of STK. The reference repo does
  the same.
- **Drag handles do not commit edits to entity COM state automatically.** STK
  only updates the visual position during a drag; the underlying
  `IAgFacility.Position` / `IAgVePropagatorGreatArc.Waypoints` etc. stay at
  pre-drag values until `_globe3D.ApplyObjectEditing()` is called explicitly.
  We invoke `ApplyObjectEditing()` on `MouseUpEvent` during edit mode so the
  drag is committed before any subsequent panel `Refresh`.
- **Tree skips rebuild when entity-set is unchanged.** A drag-edit fires
  `ScenarioChanged` (waypoint moved), but the tree's path set is identical.
  Rebuilding would wipe `SelectedNode` and hide the active panel mid-edit, so
  `RebuildFromService` short-circuits when paths are unchanged.
- **Facility position uses `Position.AssignGeodetic(lat, lon, alt)`** — the
  single-shot setter. The earlier `ConvertTo(ePlanetodetic) + Assign` path
  silently no-ops on this STK build, leaving facilities at AGI's HQ default
  position (40.04 °N, 75.60 °W).

## Diagnostic logging

Verbose mouse / mode / editing-event tracing is gated behind the `MVP4_DIAG`
environment variable. Set `MVP4_DIAG=1` (or `MVP4_DIAG=true`) before launching
to enable. Output is appended to `Desktop\stk-debug.log` so it's trivial to
share. Off by default — zero overhead when not enabled.

```
set MVP4_DIAG=1
dotnet run --project Sg.Mvp4.App -c Release
```

When the variable is unset, all `_diagLog` / `_diag` calls early-return without
writing anything.

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `AgStkObjectRoot` ctor throws | STK 12 not registered | Settings → Apps → "STK 12" → Modify → Repair (12.9+); on older 12.x, `regsvr32 agacsrv.exe` |
| `NewScenario` throws `directory_iterator::directory_iterator: ... ""` | STK MSI install incomplete: env vars `STK_INSTALL_DIR` / `STK_CONFIG_DIR` unset, COM CLSIDs unregistered, or per-user setup never propagated | Settings → Apps → "STK 12" → Modify → Repair (re-runs every install custom-action). Reboot after to refresh env vars in all shells. Verify with `[Environment]::GetEnvironmentVariable('STK_INSTALL_DIR','Machine')` |
| Integration test host crashes natively (no managed exception) | More than one `AgSTKXApplication` lifecycle in the test process. STK 12.9 native-crashes on init→dispose→init within one process | Tests must use the shared `StkBackendFixture` ([Sg.Mvp4.Tests/Integration/StkBackendFixture.cs](Sg.Mvp4.Tests/Integration/StkBackendFixture.cs)) — never construct a second `StkScenarioBackend` while another is alive |
| "Cannot find AxAGI.STKX.Interop" build error | Missing `AxAGI.STKX.Interop.dll` | `aximp "C:\Program Files\AGI\STK 12\bin\AgStkX.ocx"` in VS2022 Dev Prompt, or repair STK |
| Compute throws license error | STK Engine license unavailable | Verify license-server reachability; check STK Insight launches |
| Globe pan/zoom is sluggish | Running on integrated GPU | Add `Sg.Mvp4.App.exe` to Windows Graphics Settings → High performance |
| Window doesn't open after long wait | STK COM init failed | Check `C:\EWTSS\mvp4\logs\mvp4-<today>.log` for stack trace |
| Shutdown leaves orphan STK process | Graceful teardown >5s | 5-second watchdog in `App.OnExit` force-exits; check log for warning |

## Acceptance criteria (per design spec §7.4)

1. ✅ Operator authors a scenario from scratch with one of each of the 6 entity types.
2. ✅ Operator saves and reloads as both `.sc` and `.vdf`.
3. ✅ Operator opens the attached `Scenario34.czml`-equivalent `.sc` / `.vdf` and sees it render.
4. ✅ Compute colors the FOM grid on the globe.
5. ✅ A `.sc` written by MVP4 opens cleanly in STK Desktop / Insight.
6. ✅ App shutdown leaves no orphan `Agi.Stk12.exe` after 5 s.

## What's NOT in MVP4

Per the design spec §7.5:
- No Python, FastAPI, Kafka, SQLite, or server-side code
- No multi-user / RBAC / login
- No live DRS data overlays, no Kafka subscribers
- No FOM heatmap export (STK renders natively)
- No satellite / ground-vehicle / missile / ship / comm-object types
- No sensor patterns other than Simple Conic
- No propagator types other than Great Arc
- No mock STK — dev needs STK 12
- No cross-platform — Windows desktop only
- No browser delivery, no PDF export, no scenario-catalog UI, no auto-save

## See also

- Design spec: `docs/ewtss/specs/mvp4-stk-native-embedded-design.md`
- Map-first interaction addendum: `docs/ewtss/specs/mvp4-map-first-interaction-addendum.md`
- Implementation plan: `docs/superpowers/plans/2026-04-22-mvp4-stk-native-embedded.md`
- Map-first plan addendum: `docs/superpowers/plans/2026-04-23-mvp4-map-first-interaction-plan.md`
