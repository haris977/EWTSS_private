# MVP3 — DataProvider-driven CZML Generation — Design

## Goal

Replace MVP1's CZML generation chain (STK `ExportCZML` Connect Command +
`_patch_czml_sensors` JSON surgery) with a pipeline that reads raw STK
DataProviders and writes CZML directly via the `czml3` Python library.

Stage B of the work: a parallel FastAPI backend that mirrors MVP1's endpoint
shape and scenario content, so we can A/B the CZML output against MVP1.
Stage C (not in this spec) will be the frontend refresh and any scenario
parameterization, once stage B validates the approach.

## Motivation

MVP1's pipeline has three operational weights:

1. It depends on AGI's **CZML Exporter Plugin MSI** being installed and
   enabled in the STK session. A missing or disabled plugin makes the whole
   service unusable, and the failure mode is a cryptic `ExportCZML failed`
   error.
2. After STK emits CZML, `_patch_czml_sensors` performs JSON surgery to
   rewrite `agi_conicSensor` packets into native Cesium cylinder packets.
   The surgery is done with nested dict manipulation, hand-rolled ENU
   quaternion math, and per-packet edge cases. Correct today; brittle
   across STK versions.
3. Every CZML packet is built as a Python `dict` and serialised with
   `json.dumps`. There is no schema check — typos in field names or
   interval formats flow straight into the output and surface only when
   CesiumJS fails to render.

A DataProvider-driven pipeline with typed CZML builders removes all three:
no plugin dependency, no post-hoc surgery, packets validated at build time.

## Scope

**In scope (stage B):**

- Parallel FastAPI service on port **8003** with MVP1's endpoint shape.
- Same hardcoded Kashmir two-aircraft-plus-sensors scenario as MVP1.
- Same STK binding (`agi.stk12`, STK 12) so the A/B against MVP1 is fair.
- CZML generation via `czml3` driven by STK DataProviders.
- FOM pipeline refactored from MVP1 into three pure functions (unchanged
  behaviour — MVP1's FOM path already avoids the CZML plugin).
- Test suite with ≥15 tests covering the CZML builder, the quaternion
  math, the FOM pipeline, and the HTTP endpoints.
- MockStkService fallback that generates synthetic CZML via the same
  `czml3` code path, so the builder is exercised without an STK install.

**Not in scope:**

- Any frontend work — stage C. For stage B we validate with `curl` and,
  if needed, by pointing MVP1's Angular frontend (cloned into MVP3) at
  port 8003.
- Draw-first scenario authoring (MVP2's territory).
- Scenario parameterisation beyond the existing `exerciseId` and
  `scenarioTime`.
- Deprecating MVP1 or MVP2 — both stay intact as references.
- Upgrading to STK 13 / `agi.stk13`. Stage B stays on STK 12 to keep the
  comparison against MVP1 clean.

## Architecture

### File layout

```
mvp3/
└── sg-service-mvp3/
    ├── main.py                   FastAPI shell, port 8003
    ├── stk_service.py            IStkService abstract interface
    ├── stk_com_service_mvp3.py   agi.stk12 live service; orchestrates pipeline
    ├── stk_mock_service.py       Synthetic-CZML fallback (no STK required)
    ├── scenario_builder.py       Kashmir two-aircraft + sensors
    ├── czml_builder_mvp3.py      DataProvider → czml3 generation
    ├── cone_geometry.py          ENU quaternion / cylinder sample helpers
    ├── fom_pipeline.py           FOM grid read + heatmap PNG + rectangle overlay
    ├── computation_job.py        Thread-pool entry
    ├── requirements.txt          fastapi, uvicorn, agi.stk12, czml3, Pillow, numpy, scipy
    ├── pytest.ini                filterwarnings config (same as MVP2)
    ├── .gitignore
    └── tests/
        ├── conftest.py
        ├── test_czml_builder_mvp3.py
        ├── test_cone_geometry.py
        ├── test_fom_pipeline.py
        └── test_endpoints.py
```

MVP1 and MVP2 are not touched.

### Module responsibilities

- **`scenario_builder.py`** — pure `agi.stk12` COM. Creates the two aircraft,
  the sensors, and the access computations. No CZML knowledge. Copied from
  MVP1's `scenario_builder.py` without change.

- **`cone_geometry.py`** — pure Python math. Exports one function:

  ```python
  def cone_samples(
      parent_cartesian: list[float],  # [t, x, y, z, t, x, y, z, ...]
      cylinder_length: float,
  ) -> tuple[list[float], list[float]]:
      """Return (shifted_cartesian, unit_quaternion) for the sensor cone,
      both in time-series CZML format."""
  ```

  Shifts the cone centre so the apex sits at the parent ECEF position, and
  derives the ENU quaternion at each sample. Extracted verbatim from MVP1's
  `_cone_position_and_orientation` + `_enu_quaternion` + `_ecef_to_lonlat_rad`
  with no logic changes.

- **`czml_builder_mvp3.py`** — the replacement for `ExportCZML` +
  `_patch_czml_sensors`. Exports:

  ```python
  def build_czml(
      root,                  # agi.stk12 IAgStkObjectRoot
      scenario_name: str,
      start_time: str,       # STK-format, e.g. "1 Jan 2025 00:00:00.000"
      stop_time: str,
      model_base_url: str,   # prefix for .glb models, served by GET /models/{f}
      step_seconds: float = 10.0,
  ) -> czml3.Document:
      ...
  ```

  Internally reads four DataProvider kinds:

  1. `Cartesian Position` on each aircraft, at `step_seconds` cadence over
     `[start, stop]`, yielding ECEF Cartesians.
  2. Sensor geometry — from the STK sensor object directly (half-angle,
     cone radius); feeds `cone_geometry.cone_samples` together with the
     parent aircraft's Cartesians.
  3. `Access Data` on each aircraft pair, yielding interval
     `[(start_jd, stop_jd), ...]` lists.
  4. `DateUnit` conversion utilities on the root for consistent epoch
     formatting.

  For each aircraft: emits a `czml3.Packet` with `position.cartesian`,
  `model.gltf`, and a `path` trail. For each sensor: emits a Packet with
  a time-dynamic cylinder (topRadius=0, bottomRadius=`length·tan(half)`)
  plus `position.cartesian` and `orientation.unitQuaternion` from
  `cone_samples`. For each access pair: emits a time-dynamic
  polyline referencing the two endpoints' positions, with `availability`
  set to the access intervals.

  Returns a `czml3.Document`. No file I/O.

- **`fom_pipeline.py`** — three pure functions, each independently testable:

  ```python
  def read_fom_grid(coverage, fom, scenario) -> list[tuple[float, float, float]]: ...
  def render_heatmap_png(grid, polygon_deg, out_path: Path) -> None: ...
  def inject_rectangle_overlay(
      document: czml3.Document,
      image_url: str,
      polygon_bounds_rad: tuple[float, float, float, float],  # w, s, e, n
  ) -> czml3.Document: ...
  ```

  `read_fom_grid` is MVP1's `_read_fom_grid` verbatim (the multi-name
  DataProvider probe with Grid-Point-Locations fallback). `render_heatmap_png`
  is MVP1's `_render_fom_heatmap`, unwrapped into a free function.
  `inject_rectangle_overlay` replaces MVP1's dict-append with a typed
  `czml3.Packet` wrapping `Rectangle(coordinates=RectangleCoordinates(wsen=...))`.

- **`stk_com_service_mvp3.py`** — orchestrator. Flow:

  1. `scenario_builder.build_stk_scenario(root, exercise_id, start, stop)`
  2. `doc = czml_builder_mvp3.build_czml(root, sc.InstanceName, start, stop,
      model_base_url="http://localhost:8003/models/")`
  3. If a `CoverageDefinition` + `FigureOfMerit` exist:
     - `grid = fom_pipeline.read_fom_grid(cov, fom, sc)`
     - if grid: render PNG, then `doc = fom_pipeline.inject_rectangle_overlay(doc, ...)`
  4. Write `doc.dumps()` to `czml_output/<exercise_id>.czml`.

- **`stk_mock_service.py`** — the `MockStkService` used when `agi.stk12` is
  not installed. It calls `czml_builder_mvp3.build_czml` with a mock `root`
  that returns synthetic DataProvider results (fixed aircraft trajectories,
  fixed sensor patterns, one access interval). This exercises the real
  CZML builder code in tests and local dev.

- **`main.py`** — the FastAPI shell. Same endpoint shape as MVP1:

  - `GET /health`
  - `POST /exercises`
  - `POST /exercises/{id}/compute`
  - `GET /exercises/{id}/status`
  - `GET /exercises/{id}/czml`
  - `GET /exercises/{id}/fom-image`
  - `GET /models/{filename}`

  Uses the same thread-pool + in-memory exercise dict pattern as MVP1/MVP2.
  Port 8003. CORS allows `http://localhost:4200`.

### Data flow

```
POST /exercises/{id}/compute
    ↓
ThreadPoolExecutor (max_workers=1 — one STK seat)
    ↓
computation_job.run_computation
    ↓
stk_com_service_mvp3.build_and_compute
    ↓  scenario_builder.build_stk_scenario      → STK objects created
    ↓  czml_builder_mvp3.build_czml              → czml3.Document
    ↓  fom_pipeline.read_fom_grid                → grid list  (if FOM present)
    ↓  fom_pipeline.render_heatmap_png           → fom_images/<id>.png
    ↓  fom_pipeline.inject_rectangle_overlay     → augmented Document
    ↓  document.dumps() → czml_output/<id>.czml
    ↓
GET /exercises/{id}/czml  →  FileResponse
GET /exercises/{id}/fom-image  →  FileResponse
```

## CZML escape hatches

Two packet shapes we know `czml3` may not cover natively; both have a
clean fallback:

- **Sensor cylinder with time-dynamic orientation**: if `czml3` has no
  `Cylinder` builder, emit the packet body as a dict and wrap with
  `czml3.Packet.model_validate(dict)` (or the equivalent constructor).
  Same escape hatch MVP1 uses today, but localized to one function.

- **Custom `availability` intervals on an access polyline**: `czml3`
  supports `availability` as a CZML interval-value string; the CZML
  syntax is `"start/stop"` with `/` separator. If we need multiple
  discontinuous intervals we may need to split into multiple packets or
  use the interval-list form — escape to dict if the library's model
  is too narrow.

## Error handling

- `czml_builder_mvp3.build_czml` raises `RuntimeError` if a required
  DataProvider is missing (e.g., `Cartesian Position` not found) with a
  message naming the offending object and provider. No silent empties.
- `fom_pipeline.read_fom_grid` returns `[]` on every failure path (same
  behaviour as MVP1) so an FOM-less scenario proceeds without a hard
  fail.
- `fom_pipeline.inject_rectangle_overlay` is a no-op if
  `polygon_bounds_rad` is `None` (no AreaTarget polygon extractable).
- STK Engine startup falls back to STKDesktop attach, same as MVP1 and
  MVP2. If both fail, `get_stk_service` logs a warning and returns
  `MockStkService`.

## Testing

Same discipline as MVP1/MVP2: mock STK, run the CZML code for real.

- `test_cone_geometry.py` — **new in MVP3.** Fixes a known ECEF point at
  a known lat/lon, asserts `cone_samples` returns the expected shifted
  centre and ENU quaternion. Previously this math lived inside
  `StkComService` and was only validated end-to-end by manual Cesium
  rendering.
- `test_czml_builder_mvp3.py` — feeds a mock `root` with fixed
  DataProviders. Asserts the returned `czml3.Document` contains the
  document packet first, followed by packets for both aircraft, both
  sensors, and both access pairs. Asserts position arrays parse as
  `[t, x, y, z, ...]` with the expected length.
- `test_fom_pipeline.py` — three tests, one per pure function:
  - `read_fom_grid` with a mock fom/coverage returning fixed lat/lon/value.
  - `render_heatmap_png` on a 3×3 grid — asserts a non-zero-byte PNG is
    written and its dimensions are 512×512.
  - `inject_rectangle_overlay` on a minimal `czml3.Document` — asserts an
    extra packet appears with `wsen` in radians and the correct image URL.
- `test_endpoints.py` — HTTP integration. Same structure as
  MVP2's `test_endpoints.py`: `conftest.py` blocks the `agi.stk12` import
  so `MockStkService` is used. Walks through create → compute → status →
  czml, asserts JSON parses and contains expected packet ids.

Target: **≥15 tests green** before stage B is considered done.

## Success criterion

`POST /compute` → poll `/status` until `ready` → fetch `/czml` →
render in MVP1's Angular frontend pointed at port 8003. Visual result
(aircraft trajectories, sensor cones, access polylines over Kashmir)
should match MVP1's CZML output closely enough that the difference is
imperceptible to a human reviewer at normal viewing zoom.

## Non-goals confirmed

- No performance/throughput targets for stage B — single scenario,
  single user, no SLOs.
- No TLS, no auth, same as MVP1/MVP2.
- No offline wheel vending beyond what MVP1 already does.
- No Docker, no CI.

## Open questions

- **Does `czml3` expose a `Cylinder` builder or do we fall through to
  dict-backed packets?** Verified at stage B implementation start — if it
  doesn't, the escape hatch above applies and stage B still lands.
- **Will `Cartesian Position` DataProvider names differ on Ubuntu vs
  Windows STK builds?** The multi-name probe pattern from MVP1's
  `_read_fom_grid` will extend to this as a defensive measure.

Both are implementation-time questions, not spec blockers.
