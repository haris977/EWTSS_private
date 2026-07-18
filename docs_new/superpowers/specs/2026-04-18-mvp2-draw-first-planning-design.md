# MVP2 — Draw-First Scenario Planning Design

**Date:** 2026-04-18
**Status:** Draft — pending user review
**Branch:** feat/mvp-stk-czml-validation

---

## Goal

Extend the STK + CZML + CesiumJS proof-of-concept (MVP) with an operator-facing
draw-first planning workflow. The operator draws entities on a CesiumJS globe,
configures them via a form panel, submits the plan, and receives STK-computed
CZML rendered on the same globe.

The secondary goal is to evaluate `ansys.stk` (PySTK) as a replacement for the
`agi.stk12` COM bindings used in MVP.

---

## Out of Scope

- Edit handles / vertex drag after initial placement
- Multi-user collaboration or scenario save/load
- Kafka, TimescaleDB, RBAC, Electron
- Replacing or modifying any file in `mvp/sg-service/` or `mvp/frontend/`

---

## Architecture

```
Angular frontend (mvp2/frontend)
  DrawingToolbarComponent   — mode buttons (AreaTarget / Aircraft / Facility / Sensor)
  EntitySidebarComponent    — list of created entities + form panel
  ScenarioPlannerComponent  — submit plan, poll status, load CZML
  CesiumViewerComponent     — globe, drawing handlers, CZML render
        │
        │ HTTP  (port 8002)
        ▼
FastAPI  sg-service-v2  (mvp2/sg-service-v2)
  POST /exercises                  create exercise
  POST /exercises/{id}/compute     accept PlanningResult, trigger STK
  GET  /exercises/{id}/status      pending | running | ready | error
  GET  /exercises/{id}/czml        serve CZML file
  GET  /exercises/{id}/fom-image   serve FOM heatmap PNG
  GET  /models/{filename}          serve STK .glb model files
        │
        │ ansys.stk (PySTK)   [in-process STK Engine]
        ▼
  StkPytkService
  scenario_builder_v2.py
  czml_utils.py              (pure-Python CZML patch + FOM render — copied from MVP)
```

**Port:** MVP runs on 8001; MVP2 runs on **8002**. Both must not run simultaneously
on a machine with a single STK Engine license (see §License Constraints).

---

## 1 — Section 1: Architecture Decisions

### 1.1 Visualization platform

CesiumJS is used for both drawing and result rendering. STK globe streaming via
PySTK `jupyterwidgets.GlobeWidget` (Remote Frame Buffer) was evaluated and
rejected: RFB's ~30–60 ms round-trip latency makes draw-first globe interaction
impractical (each click requires a server round-trip before the vertex appears).
CesiumJS runs entirely in the browser — zero interaction latency.

### 1.2 STK API: PySTK (`ansys.stk`) exclusively

MVP uses `agi.stk12` COM bindings. MVP2 uses `ansys.stk` (PySTK) exclusively.

**Reason:** Mixing both libraries in the same Python process has undefined
license behavior and may consume two STK Engine seats.

**Known PySTK gap:** AreaTarget polygon boundary assignment is not exposed as a
Python API in any current PySTK release. A Connect command fallback is used for
that one call (see §4.2). This gap is explicitly documented as an evaluation
finding.

### 1.3 STK Engine startup model

PySTK `STKEngine.start_application()` starts an in-process Engine. There is no
"attach to existing" path for Engine mode in PySTK. The service owns the Engine
lifetime: start on `__init__`, stop on `shutdown()`.

---

## 2 — Section 2: Drawing Tools (Angular)

### 2.1 DrawingStateService

Singleton Angular service shared across all drawing components:

```typescript
interface DrawingState {
  mode: 'none' | 'AreaTarget' | 'Aircraft' | 'Facility' | 'Sensor';
  inProgressCoords: Cartesian3[];   // vertices accumulated so far
  entities: PlanningEntity[];       // completed entities
}
```

### 2.2 Per-mode event handling

Each mode creates a `ScreenSpaceEventHandler` on activation and destroys it on
mode change. No persistent global handler.

| Mode | Left-click | Double-click | Result |
|---|---|---|---|
| AreaTarget | Add vertex, draw live polyline | Close polygon, save entity | GeoJSON Polygon |
| Aircraft | Add waypoint, draw live polyline | Finish route, save entity | GeoJSON LineString |
| Facility | Place point, save entity immediately | — | GeoJSON Point |
| Sensor | No globe interaction | — | Form-only, attached to parent entity |

### 2.3 Visual feedback during drawing

- Temporary `PolylineGraphics` connects accumulated vertices to cursor position
- Completed entities rendered as permanent Cesium entities (polygon fill, polyline, billboard)
- No edit handles after placement (Approach A — minimal tooling for MVP2 scope)

### 2.4 DrawingToolbarComponent

Four mode buttons. Active mode highlighted. Clicking an active mode button
cancels the current in-progress entity and returns to `none`.

### 2.5 EntitySidebarComponent

Lists completed entities. Each row shows entity type, name, and a delete button.
Selecting a row opens a form panel for:

- **AreaTarget:** name only
- **Aircraft:** name, speed (m/s), altitude (m, applied uniformly to all waypoints if not drawn with altitude)
- **Facility:** name, altitude (m)
- **Sensor:** name, parent entity (dropdown of Aircraft/Facility), half-angle (degrees)

---

## 3 — Section 3: Data Model

### 3.1 Frontend entity representation

Each drawn entity is a GeoJSON Feature with a `properties.entityType` discriminator:

```typescript
interface PlanningEntity {
  type: 'Feature';
  geometry:
    | { type: 'Polygon';    coordinates: [number, number][][]; }   // AreaTarget
    | { type: 'LineString'; coordinates: [number, number, number][]; }  // Aircraft [lon,lat,alt_m]
    | { type: 'Point';      coordinates: [number, number, number]; }    // Facility [lon,lat,alt_m]
    | { type: 'Point';      coordinates: [number, number, number]; };   // Sensor (parent's position)
  properties: {
    entityType:    'AreaTarget' | 'Aircraft' | 'Facility' | 'Sensor';
    name:          string;
    speedMs?:      number;    // Aircraft only
    halfAngleDeg?: number;    // Sensor only
    parentEntity?: string;    // Sensor only — name of parent Aircraft or Facility
  };
}
```

### 3.2 PlanningResult (HTTP POST body)

```typescript
interface PlanningResult {
  exerciseId:   string;            // UUID, generated client-side
  scenarioTime: {
    start: string;                 // ISO-8601 UTC
    stop:  string;
  };
  entities: PlanningEntity[];
}
```

### 3.3 Coordinate conventions

| Field | Unit | Order |
|---|---|---|
| GeoJSON coordinates | degrees | `[longitude, latitude, altitude?]` |
| STK `SetState PatternOld` | degrees | `Lat <n> Lon <n>` — **swapped from GeoJSON** |
| STK waypoint altitude | km | divide metres by 1000 |
| STK facility altitude | metres | pass directly |
| PySTK waypoint speed | knots | multiply m/s by 1.94384 |
| CZML `wsen` rectangle | radians | no conversion needed — derived from cartographicRadians |

---

## 4 — Section 4: STK Object Creation (`scenario_builder_v2.py`)

### 4.0 API

`ansys.stk` (PySTK) throughout. `agi.stk12` is not imported in any MVP2 file.

### 4.1 `StkPytkService` — startup and shutdown

```python
class StkPytkService(IStkService):

    def __init__(self) -> None:
        from ansys.stk.application import STKEngine
        self._engine = STKEngine.start_application()
        self._root   = self._engine.root
        log.info("StkPytkService: STK Engine started (PySTK)")

    def shutdown(self) -> None:
        try:
            self._engine.stop_application()
            log.info("StkPytkService: STK Engine stopped")
        except Exception as exc:
            log.warning("StkPytkService: shutdown error: %s", exc)
```

### 4.2 AreaTarget — polygon via Connect command (documented PySTK gap)

```python
def _add_area_target(root, scenario, name, polygon_lonlat):
    from ansys.stk.objects import STKObjectType
    at = scenario.children.new(STKObjectType.AREA_TARGET, name)
    # PySTK does not expose boundary vertex assignment — Connect command is the
    # only supported path. Documented as a PySTK evaluation finding.
    coords = " ".join(f"Lat {lat:.6f} Lon {lon:.6f}" for lon, lat in polygon_lonlat)
    root.execute_command(f"SetState */AreaTarget/{name} PatternOld {coords}")
    return at
```

**Gotcha — GeoJSON coordinate swap:** GeoJSON `[lon, lat]`; STK expects `Lat <n> Lon <n>`.
The comprehension unpacks `lon, lat in polygon_lonlat` correctly.

**Gotcha — closing vertex:** GeoJSON rings repeat the first vertex. Pass the full
ring including the duplicate — STK tolerates it.

### 4.3 Aircraft — multi-waypoint GreatArc

```python
def _add_aircraft(scenario, name, waypoints_lonlat_altm, speed_ms):
    from ansys.stk.objects import STKObjectType
    aircraft  = scenario.children.new(STKObjectType.AIRCRAFT, name)
    route     = aircraft.route
    speed_kn  = speed_ms * 1.94384   # PySTK waypoint speed unit: knots
    for lon, lat, alt_m in waypoints_lonlat_altm:
        wp           = route.waypoints.add()
        wp.latitude  = lat
        wp.longitude = lon
        wp.altitude  = alt_m / 1000.0   # km — same as agi.stk12 COM
        wp.speed     = speed_kn
    route.propagate()
    return aircraft
```

**Gotcha — speed unit changed from MVP:** `agi.stk12` COM used km/s; PySTK uses
**knots**. Conversion: `speed_ms * 1.94384`. Wrong unit produces correct geometry
with incorrect timing.

**Validation test:** Create a 1-hour scenario with a known great-circle distance;
confirm aircraft reaches the endpoint waypoint at the expected time.

### 4.4 Facility

```python
def _add_facility(scenario, name, lat, lon, alt_m=0.0):
    from ansys.stk.objects import STKObjectType
    facility = scenario.children.new(STKObjectType.FACILITY, name)
    facility.position.assign_planetodetic(lat, lon, alt_m)   # alt in metres
    return facility
```

**Gotcha — altitude units differ by object type:** Aircraft waypoints use km;
Facility `assign_planetodetic` uses metres.

### 4.5 Sensor (form-only)

```python
def _add_sensor(parent, name, half_angle_deg):
    from ansys.stk.objects import STKObjectType
    from ansys.stk.objects.sensor import SensorPattern
    sensor = parent.children.new(STKObjectType.SENSOR, name)
    sensor.set_pattern_type(SensorPattern.SIMPLE_CONIC)
    sensor.common_tasks.set_pattern_simple_conic(half_angle_deg, 1)
    return sensor
```

### 4.6 CoverageDefinition + FigureOfMerit

Auto-created when the entity list contains ≥1 AreaTarget and ≥1 Aircraft.
Produces the same DataProvider shape that `czml_utils._read_fom_grid_v2` reads.

```python
def _add_coverage_fom(scenario, area_target_name, asset_objects):
    from ansys.stk.objects import STKObjectType
    from ansys.stk.objects.coverage_definition import CoverageBounds
    cov = scenario.children.new(STKObjectType.COVERAGE_DEFINITION, f"Cov_{area_target_name}")
    cov.grid.bounds_type = CoverageBounds.CUSTOM_REGIONS
    cov.grid.bounds.area_targets.add(f"AreaTarget/{area_target_name}")
    for obj in asset_objects:
        cov.asset_list.add(obj.instance_name)
    cov.compute_accesses()
    fom = cov.children.new(STKObjectType.FIGURE_OF_MERIT, f"FOM_{area_target_name}")
    fom.set_definition_type(1)   # 1 = SimpleAccessCoverage
    fom.data_definition.compute()
    return cov, fom
```

**Module license warning:** `CoverageDefinition` and `FigureOfMerit` require the
**Coverage Analyzer** add-on module. Base STK Engine is not sufficient. Confirm
this module is included in your Engine license before enabling this path.

### 4.7 Top-level orchestration

```python
def build_stk_scenario_v2(root, exercise_id, start_time, stop_time, entities):
    from ansys.stk.objects import STKObjectType

    try:
        root.close_scenario()
    except Exception:
        pass
    root.new_scenario(f"MVP2_{exercise_id.replace('-', '_')}")
    sc = root.current_scenario
    sc.start_time = start_time
    sc.stop_time  = stop_time

    stk_objects = {}

    for ent in entities:
        etype = ent["properties"]["entityType"]
        name  = ent["properties"]["name"]

        if etype == "AreaTarget":
            ring = ent["geometry"]["coordinates"][0]   # GeoJSON exterior ring [(lon,lat),...]
            stk_objects[name] = _add_area_target(root, sc, name, ring)

        elif etype == "Aircraft":
            coords   = ent["geometry"]["coordinates"]  # [[lon,lat,alt_m],...]
            speed_ms = ent["properties"].get("speedMs", 150.0)
            stk_objects[name] = _add_aircraft(sc, name, coords, speed_ms)

        elif etype == "Facility":
            lon, lat = ent["geometry"]["coordinates"][:2]
            alt_m    = ent["geometry"]["coordinates"][2] if len(ent["geometry"]["coordinates"]) > 2 else 0.0
            stk_objects[name] = _add_facility(sc, name, lat, lon, alt_m)

        elif etype == "Sensor":
            parent_name = ent["properties"]["parentEntity"]
            half_angle  = ent["properties"].get("halfAngleDeg", 25.0)
            parent_obj  = stk_objects.get(parent_name)
            if parent_obj is None:
                raise ValueError(f"Sensor {name}: parent '{parent_name}' not yet created — "
                                 "reorder entities so parents appear before sensors")
            stk_objects[name] = _add_sensor(parent_obj, name, half_angle)

    # Auto CoverageDefinition + FOM when AreaTarget + Aircraft both present
    at_names      = [e["properties"]["name"] for e in entities if e["properties"]["entityType"] == "AreaTarget"]
    aircraft_objs = [stk_objects[e["properties"]["name"]] for e in entities if e["properties"]["entityType"] == "Aircraft"]
    if at_names and aircraft_objs:
        _add_coverage_fom(sc, at_names[0], aircraft_objs)

    # Access computation — all aircraft pairs
    for i, a in enumerate(aircraft_objs):
        for b in aircraft_objs[i + 1:]:
            a.get_access_to_object(b).compute_access()
```

### 4.8 Runtime validation items

| Item | Risk | Validation |
|---|---|---|
| Waypoint speed = knots | Page was truncated; converted from "nautical miles/hour" research finding | Check aircraft arrives at endpoint at expected wall-clock time |
| `CoverageBounds.CUSTOM_REGIONS` enum name | Docs page partial | If `ImportError`, try integer `2`; `print(dir(CoverageBounds))` at startup |
| `fom.set_definition_type(1)` int value | Carried from agi.stk12 COM | Compare FOM output to known scenario |
| `root.execute_command()` method name | May be `execute_connect_command` in some PySTK versions | Check at startup; AreaTarget polygon test will fail loudly |
| `root.new_scenario()` / `root.current_scenario` | PySTK snake_case assumed | Confirm against installed PySTK version |

---

## 5 — Section 5: Results Pipeline

### 5.1 CZML export

PySTK's `root.execute_command()` calls the same underlying Connect interface as
`agi.stk12`'s `root.ExecuteCommand()`. The `ExportCZML` plugin command string is
identical:

```python
def _export_czml(self, output_path, scenario_name):
    abs_path = str(output_path.resolve()).replace('\\', '/')
    cmd = f'ExportCZML */Scenario/{scenario_name} "{abs_path}" http://localhost:8002/models/'
    try:
        self._root.execute_command(cmd)
    except Exception as exc:
        raise RuntimeError(f"ExportCZML failed — ensure CZML Export plugin is installed: {exc}")
    if not output_path.exists() or output_path.stat().st_size == 0:
        raise RuntimeError("ExportCZML produced no output file")
```

> `STKEngine.start_application()` must be called with the equivalent of
> `noGraphics=False` so the CZML Export plugin loads. Verify against PySTK's
> `start_application()` keyword arguments at integration time.

### 5.2 Pure-Python CZML utilities — `czml_utils.py`

These functions from MVP's `StkComService` contain no STK API calls.
They are **copied verbatim** into `mvp2/sg-service-v2/czml_utils.py`.
MVP's `stk_com_service.py` is not modified.

| Function | Source in MVP | Purpose |
|---|---|---|
| `patch_czml_sensors(path)` | `_patch_czml_sensors` | Replace `agi_conicSensor` with native Cesium cylinders |
| `inject_fom_rectangle(czml_path, image_url)` | `inject_fom_rectangle` | Add georeferenced FOM PNG rectangle overlay |
| `render_fom_heatmap(grid, img_path, polygon_deg)` | `_render_fom_heatmap` | scipy griddata → polygon-clipped RGBA PNG |
| `extract_area_target_polygon(czml_path)` | `_extract_area_target_polygon` | Read first AreaTarget polygon from CZML |

All heatmap quality decisions from MVP are preserved:
- `griddata(method='linear')` for smooth interpolation
- Image extent = polygon bounding box (matches CZML `wsen` bounds exactly)
- Polygon alpha mask at `radius=0.5` (sub-pixel antialiasing, no feathering)
- Solid boundary — no alpha channel blur

### 5.3 FOM discovery

```python
def _find_coverage_fom(self, scenario):
    for child in scenario.children:           # PySTK iterable — no .Count/.Item(i)
        if child.class_name.lower() != 'coveragedefinition':
            continue
        for sub in child.children:
            if sub.class_name.lower() == 'figureofmerit':
                return child, sub
    return None, None
```

### 5.4 FOM DataProvider reading

`_read_fom_grid_v2` mirrors MVP's `_read_fom_grid` with PySTK DataProvider access.
The DataProvider binding is the highest-risk call in the v2 pipeline —
`dir(obj.data_providers)` should be logged at first integration to confirm
the actual attribute/method names.

```python
def _read_fom_grid_v2(self, coverage, fom_obj, scenario):
    # PySTK start_time/stop_time may be datetime objects — convert to STK string format
    # if Exec() requires strings (same format as agi.stk12: "1 Jan 2025 00:00:00.000").
    start = scenario.start_time
    stop  = scenario.stop_time

    def _try_dp(obj, dp_name, val_cols, exec_args=()):
        try:
            try:
                dp = obj.data_providers[dp_name]
            except (TypeError, KeyError):
                dp = obj.data_providers.get_item_by_name(dp_name)
            result = dp.exec(*exec_args)
            lats = _get_col(result, 'Latitude', 'Lat', 'lat')
            lons = _get_col(result, 'Longitude', 'Lon', 'lon')
            if not lats:
                return None
            for col in val_cols:
                vals = _get_col(result, col)
                if vals:
                    log.info("_read_fom_grid_v2: %d pts via '%s'/'%s'", len(lats), dp_name, col)
                    return list(zip(lats, lons, vals))
        except Exception as exc:
            log.warning("_read_fom_grid_v2: '%s' raised: %s", dp_name, exc)
        return None

    val_cols_fom = ('FOM Value', 'Value', 'Satisfaction')
    val_cols_cov = ('Percent Coverage', 'Coverage', 'Value')

    # FOM static DataProviders — no time args (same gotcha as MVP)
    for dp_name in ('Value By Point', 'Static Satisfaction'):
        result = _try_dp(fom_obj, dp_name, val_cols_fom, exec_args=())
        if result:
            return result

    # Coverage time-based DataProvider
    result = _try_dp(coverage, 'Percent Coverage', val_cols_cov,
                     exec_args=(start, stop, 3600.0))
    if result:
        return result

    return []
```

### 5.5 Full `export_czml` orchestration

```python
def export_czml(self, exercise_id, output_path):
    from czml_utils import patch_czml_sensors, inject_fom_rectangle, \
                           render_fom_heatmap, extract_area_target_polygon
    out = Path(output_path)
    out.parent.mkdir(parents=True, exist_ok=True)

    sc = self._root.current_scenario
    self._export_czml(out, sc.instance_name)
    patch_czml_sensors(out)

    coverage, fom_obj = self._find_coverage_fom(sc)
    if fom_obj:
        grid = self._read_fom_grid_v2(coverage, fom_obj, sc)
        if grid:
            img_dir  = Path("fom_images")
            img_dir.mkdir(exist_ok=True)
            img_path = img_dir / f"{exercise_id}.png"
            polygon_deg = extract_area_target_polygon(out)
            render_fom_heatmap(grid, img_path, polygon_deg=polygon_deg)
            inject_fom_rectangle(
                out, f"http://localhost:8002/exercises/{exercise_id}/fom-image"
            )
        else:
            log.warning("StkPytkService: FOM DataProviders returned no data — "
                        "use POST /exercises/%s/fom-image to upload manually", exercise_id)
    else:
        log.info("StkPytkService: no FigureOfMerit — skipping heatmap")
```

### 5.6 Polling flow

```
POST /exercises              → 201, {exerciseId}
POST /exercises/{id}/compute → 202, status=running
                               ThreadPoolExecutor(max_workers=1)  ← must stay 1;
                               two simultaneous STK computations = license violation
GET  /exercises/{id}/status  → {status: pending | running | ready | error}
GET  /exercises/{id}/czml    → serve czml_output/{id}.czml
GET  /exercises/{id}/fom-image → serve fom_images/{id}.png
GET  /models/{filename}      → serve STK .glb model files
```

---

## License Constraints

| Constraint | Detail |
|---|---|
| One Engine license at a time | Do not run `mvp/sg-service` (agi.stk12) and `mvp2/sg-service-v2` (PySTK) simultaneously |
| `max_workers=1` mandatory | Multiple concurrent STK computations in one process still violate single-seat licensing |
| Coverage Analyzer module | Required for `CoverageDefinition` + `FigureOfMerit`; confirm module is on your Engine license |
| `noGraphics=False` equivalent | Required for CZML Export plugin to load; verify PySTK `start_application()` flag |

---

## File Structure

```
mvp2/
├── sg-service-v2/
│   ├── main.py                FastAPI: same endpoint structure as MVP, port 8002
│   ├── stk_service.py         IStkService interface (unchanged from MVP)
│   ├── stk_pystk_service.py   PySTK implementation (StkPytkService)
│   ├── stk_mock_service.py    Delegates to czml_builder (reused from MVP, no changes)
│   ├── scenario_builder_v2.py build_stk_scenario_v2() — PySTK entity creation
│   ├── czml_builder.py        Synthetic CZML (reused from MVP, no changes)
│   ├── czml_utils.py          Pure-Python CZML patch + FOM render (copied from MVP)
│   ├── computation_job.py     run_computation_v2() — ThreadPoolExecutor wrapper
│   ├── requirements.txt       fastapi, uvicorn, httpx, pytest, Pillow, numpy, scipy, ansys-stk
│   ├── czml_output/
│   ├── fom_images/
│   └── tests/
└── frontend/
    └── src/app/
        ├── cesium-viewer/     Globe + drawing handlers
        ├── drawing-toolbar/   Mode buttons
        ├── entity-sidebar/    Entity list + form panel
        └── scenario-planner/  HTTP flow: build PlanningResult → compute → poll → load CZML
```

---

## PySTK Evaluation Findings (to be updated during implementation)

| Area | Finding |
|---|---|
| Aircraft waypoints | Clean Python API; speed unit (knots) differs from agi.stk12 (km/s) |
| Facility position | Clean; `assign_planetodetic` well-documented |
| Sensor pattern | Clean; `SensorPattern` enum available |
| CoverageDefinition | Clean area_targets collection API |
| AreaTarget boundary | **Gap** — polygon vertices require Connect command fallback |
| DataProvider access | Unknown — needs runtime validation |
| Scenario creation | `new_scenario()` / `current_scenario` needs confirmation against installed version |
| CZML Export plugin | Shared Connect command — same requirement as MVP |
