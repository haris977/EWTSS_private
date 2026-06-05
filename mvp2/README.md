# EWTSS MVP2 — Draw-First Scenario Planning

Evolution of MVP1: users draw entities on a Cesium globe, the backend builds an
STK scenario from those entities via `agi.stk13` (STK 13 / STK_ODTK 13), and
the resulting CZML is rendered back on the same globe.

```
User draws on CesiumJS globe (Angular)
    → PlanningResult (GeoJSON entities + scenario time)
    → FastAPI sg-service-v2
    → agi.stk13 builds scenario + computes access / FOM
    → CZML export → Cesium render (timeline, LOS, optional FOM heatmap)
```

**Scope:** AreaTarget polygons, Aircraft routes, Facility points, Sensor cones.
No hardcoded scenario — everything is drawn by the user.

---

## STK version note — MVP1 (STK 12) vs MVP2 (STK 13)

MVP1 uses `agi.stk12` with STK 12 on port 8001. MVP2 uses `agi.stk13` with
STK 13 / STK_ODTK 13 on port 8002. The two STK versions have **separate
Engine licenses** and can run side-by-side if both are licensed. If you only
hold one STK Engine seat, stop the other service before starting this one.

---

## Prerequisites

| Tool       | Version          | Notes                                                                                 |
|------------|------------------|---------------------------------------------------------------------------------------|
| Python     | 3.11 or 3.12     | 3.9 is too old; 3.14 often lacks binary wheels for NumPy/SciPy/Pillow                 |
| Node.js    | 20 + LTS         | Angular 21 requires Node 20 or later                                                  |
| STK 13     | Required for live STK | Ships as "STK_ODTK 13"; STK Engine Runtime registered at `HKLM\SOFTWARE\AGI\STK_ODTK Engine Runtime` |
| agi.stk13  | 13.1.0           | COM Python binding for STK 13 — wheel ships inside the STK install (`bin\AgPythonAPI\`) |

### Picking the right Python on Windows

Use the `py` launcher to pin a specific version instead of relying on `PATH` order:

```bash
py -0                         # list installed Python versions
py -3.11 --version            # verify 3.11 is available
py -3.11 -m venv .venv        # create a venv pinned to 3.11
.venv\Scripts\activate
pip install -r requirements.txt
```

> If `pip install` fails with *"Could not find a version that satisfies the
> requirement … Requires-Python >=3.10"*, `pip` is running under an older
> Python. Check with `python --version` (or `where python`) and re-run through
> `py -3.11 -m pip …`.

### Installing `agi.stk13`

`agi.stk13` is AGI/Ansys' COM Python binding for STK 13 — the direct
successor of `agi.stk12`. The wheel is **not** on PyPI; it ships inside
the STK install under `bin\AgPythonAPI\`. Install it into the same venv:

```bash
pip install "C:\Program Files\AGI\STK_ODTK 13\bin\AgPythonAPI\agi_stk13-13.1.0-py3-none-any.whl"
```

The exact version suffix (`13.1.0`) may vary with your installed STK build —
look in that folder for the current filename.

> **Why `agi.stk13` over PySTK (`ansys.stk.core`)?** PySTK is a 0.x dev
> wheel with an API that may shift before its 1.0 release. `agi.stk13` is
> a stable 13.x COM wrapper using the same PascalCase shape as MVP1's
> `agi.stk12`, so the porting cost is minimal and CZML export works via
> the same Connect-command path MVP1 already relies on.

Skip this step to run the backend against `MockStkService` instead — all 21
backend tests pass without STK installed.

---

## 1 — Backend (sg-service-v2)

```bash
cd mvp2/sg-service-v2

# Create a venv pinned to Python 3.11 or 3.12 (see "Picking the right Python" above)
py -3.11 -m venv .venv
.venv\Scripts\activate

# Install Python dependencies (once)
pip install -r requirements.txt

# Install agi.stk13 separately — ships inside STK install (see above)
pip install "C:\Program Files\AGI\STK_ODTK 13\bin\AgPythonAPI\agi_stk13-13.1.0-py3-none-any.whl"   # optional

# Run tests — all 21 pass, no STK required
pytest tests/ -v

# Start the service
python -m uvicorn main:app --port 8002 --log-level info
```

Service starts on **http://localhost:8002**.

Startup log line:
- `using StkComServiceV13 (live agi.stk13)` — `agi.stk13` installed, STK Engine started
- `agi.stk13 package not installed ... — using MockStkService` — synthetic CZML will be generated
- `STK Engine failed to start ... — using MockStkService` — package installed but Engine couldn't start

### Using live STK

Install the `agi.stk13` wheel (see "Installing `agi.stk13`" above), then start
the service:

```bash
python -m uvicorn main:app --port 8002 --log-level info
```

The service starts STK Engine in-process via `STKEngine.StartApplication(noGraphics=False)`
(graphics stack enabled because the CZML Exporter Plugin requires it). If the
Engine can't be started — for example when no license is available — it falls
back to attaching to a running `STKDesktop` instance.
For headless deployment, install **STK Desktop Engine** instead of the full STK
Desktop — STK Engine is detected automatically, no GUI is launched.

#### CZML Export plugin (required for live STK)

The service exports CZML via STK's `ExportCZML` Connect Command, which requires
AGI's **CZML Exporter Plugin**. Install the plugin MSI from AGI — it registers
itself under `HKLM\SOFTWARE\AGI\STK CZML Exporter Plugin 13` (or the v12
fallback key on older installs) and is auto-loaded by STK Engine on startup.

On startup the service reads the registry and logs one of:

```
INFO     stk_com_service_v13: CZML Exporter Plugin registered at C:\Program Files\AGI\STK CZML Exporter Plugin 13
```

or, if the MSI isn't installed:

```
WARNING  stk_com_service_v13: CZML Exporter Plugin not found in registry (HKLM\SOFTWARE\AGI\STK CZML Exporter Plugin 13 / 12). Install the plugin MSI from AGI before exporting CZML.
```

If `ExportCZML` later fails, the raised `RuntimeError` echoes the plugin
state so you can tell immediately whether it's a missing-install problem or
a different Connect-command issue.

*Legacy fallback:* if you only have the plugin script (not the MSI), you can
still enable it manually via STK's **Utilities → Plugin Scripts…** dialog and
add `C:\Program Files\AGI\STK 12\bin\Plugins\CZMLExport\CZMLExport.pl`.

### HTTP endpoints

| Method | Path                                | Description                                          |
|--------|-------------------------------------|------------------------------------------------------|
| GET    | `/health`                           | Liveness probe                                       |
| POST   | `/exercises`                        | Create a new exercise ID                             |
| POST   | `/exercises/{id}/compute`           | Submit a `PlanningResult` and start computation      |
| GET    | `/exercises/{id}/status`            | Poll `created` / `running` / `ready` / `error: …`    |
| GET    | `/exercises/{id}/czml`              | Fetch the generated CZML (409 until `ready`)         |
| GET    | `/exercises/{id}/fom-image`         | Fetch the FOM heatmap PNG (404 if none)              |
| GET    | `/models/{filename}`                | Serve STK `.glb` model files for Cesium              |

---

## 2 — Frontend

```bash
cd mvp2/frontend

# Install dependencies (once)
npm install

# Start dev server
npm start
```

Opens on **http://localhost:4200**.

Runs against the backend at `http://localhost:8002` (hardcoded in
`scenario-planner.ts`).

### Unit tests

```bash
cd mvp2/frontend
npx ng test --watch=false
```

Uses Vitest (Angular 21 default). 7 tests cover `DrawingStateService` and app
bootstrap.

---

## 3 — End-to-End Walkthrough

1. Start the backend (step 1 above)
2. Start the frontend (step 2 above)
3. Open **http://localhost:4200**

Drawing flow:

| Toolbar button | Action                                                                 |
|----------------|------------------------------------------------------------------------|
| Area Target    | Click vertices on the globe, double-click to close the polygon         |
| Aircraft Route | Click waypoints, double-click to finish; altitude is set to 0 m        |
| Facility       | Click once on the globe to drop a point                                |
| Sensor         | Fill the sidebar form (name, parent entity, half-angle) and submit     |

Each committed entity appears in the **Entities** sidebar. Click `x` to delete.

Click **Run Scenario** to send the `PlanningResult` to the backend:

```
Exercise created: <uuid>
Computation started - polling...
Status: running
Status: ready
```

The globe then loads the returned CZML. Timeline scrubber becomes active;
`CesiumViewer.zoomTo(ds)` auto-frames the new entities.

---

## File Structure

```
mvp2/
├── sg-service-v2/
│   ├── main.py                   FastAPI: exercise CRUD, compute trigger, CZML serve
│   ├── stk_service.py            IStkService abstract interface
│   ├── stk_com_service_v13.py    Live agi.stk13 implementation (FOM pipeline)
│   ├── stk_mock_service.py       Delegates to czml_builder_v2 — no STK required
│   ├── czml_builder_v2.py        Synthetic CZML for MockStkService
│   ├── czml_utils.py             Pure-Python helpers (sensor patch, FOM overlay)
│   ├── scenario_builder_v2.py    agi.stk13: AreaTarget, Aircraft, Facility, Sensor, CoverageDefinition
│   ├── computation_job.py        run_computation — called in ThreadPoolExecutor
│   ├── requirements.txt          fastapi, uvicorn, httpx, pytest, Pillow, numpy, scipy (agi.stk13 installed separately)
│   └── tests/                    pytest suite (21 tests, no STK required)
└── frontend/
    └── src/app/
        ├── types.ts              PlanningEntity, PlanningResult, DrawingMode
        ├── drawing-state.service.ts  Signal-based drawing state
        ├── cesium-viewer/        Globe + drawing mode handlers
        ├── drawing-toolbar/      Mode buttons with contextual hints
        ├── entity-sidebar/       Entity list + sensor form
        ├── scenario-planner/     HTTP flow: create → compute → poll → emit CZML URL
        └── app.ts                Shell layout wiring the four components
```

---

## With Real STK — Validation Checklist

After confirming `MockStkService` works, repeat with a live STK session:

- [ ] Backend log shows `using StkComServiceV13 (live agi.stk13)`
- [ ] `StkComServiceV13: started STK Engine (graphics enabled)` appears on startup
- [ ] `StkComServiceV13: scenario built for exercise <id>` after `POST /compute`
- [ ] `StkComServiceV13: CZML exported -> <path> (N.N KB)` with non-zero size
- [ ] `czml_output/<id>.czml` contains `emitter`/`radar`-style packets with time-dynamic positions
- [ ] Drawn AreaTarget polygon appears as orange filled polygon on the globe
- [ ] Drawn Aircraft route renders as a cyan line, animates along the timeline
- [ ] Drawn Facility renders as a yellow point marker
- [ ] Drawn Sensor cone attaches to its parent entity and moves with it
- [ ] When both AreaTarget and Aircraft exist, `CoverageDefinition + FOM` is created and `fom_images/<id>.png` is written
- [ ] FOM heatmap appears as a georeferenced overlay clipped to the AreaTarget polygon
- [ ] Log shows `_read_fom_grid_v2: N pts via '<provider>'/'<column>'` when a FOM exists
- [ ] Log warns if only one AreaTarget gets coverage when the user drew multiple

---

## Key Differences vs. MVP1

| Concern             | MVP1                          | MVP2                                   |
|---------------------|-------------------------------|----------------------------------------|
| STK binding         | `agi.stk12` COM (STK 12)      | `agi.stk13` COM (STK 13 / STK_ODTK 13) |
| Port                | 8001                          | 8002                                   |
| Scenario source     | Hardcoded Kashmir scenario    | Draw-first on Cesium globe             |
| Input contract      | Exercise name only            | `PlanningResult` (GeoJSON entities)    |
| Angular             | Older, NgModule-based         | Angular 21 standalone + new control flow |
| Unit tests          | 14 pytest + karma             | 21 pytest + 7 vitest                   |
| FOM heatmap         | Kashmir-specific              | Auto from any AreaTarget + Aircraft    |
| License constraint  | Single STK Engine seat        | Separate STK 12 vs STK 13 seats — can run side-by-side if both licensed |
