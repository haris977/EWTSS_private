# EWTSS MVP3 — DataProvider-driven CZML + Ion SDK Rendering

Evolution of MVP1's CZML pipeline:

* **No more `ExportCZML` plugin.** CZML is generated in Python from STK
  DataProvider reads instead of STK's CZML Exporter plugin MSI.
* **No more JSON surgery** (`_patch_czml_sensors` is gone).
* **Native sensor rendering** via Cesium **Ion SDK 1.135** sensor primitives
  on the frontend — no cylinder approximations, no ENU quaternion math on the
  backend.
* **Directional antenna visualization.** The emitter renders a 4-layer
  Gaussian lobe (`-3 / -6 / -10 / -20 dB` iso-gain contours with a
  hot-to-cold color ramp) and is aimed **horizontally at the radar** each
  sample; the radar sensor is aimed **at nadir** (straight down). Each
  contour's ground footprint drapes over the ellipsoid.
* **Built-in measurement + playback controls.** Cesium's timeline /
  animation widgets are enabled for scrubbing, and Ion SDK's
  `viewerMeasureMixin` adds a distance/area/height toolbar.

```
scenario_builder            ──►  agi.stk12 COM (same as MVP1)
      │
      ▼
czml_builder_mvp3           ──►  reads Cartesian Position / Access Data / sensor pattern
      │                          DataProviders; emits dict packets directly
      │                          • aircraft (point + path)
      │                          • radar: agi_conicSensor, orientation = nadir quat
      │                          • emitter: 4× agi_customPatternSensor, orientation = aim-at-radar quat
      │                          • access polyline between aircraft
      ▼
fom_pipeline                ──►  FOM grid → PIL heatmap → CZML rectangle overlay
      │
      ▼
Angular 21 + @cesium/engine 22 + @cesium/widgets 14.3
  + @cesiumgs/ion-sdk-sensors + ion-sdk-measurements + ion-sdk-geometry
      │
      ▼
Cesium Viewer renders: aircraft tracks, conic + custom-pattern sensors via
                       Ion SDK, access polylines, FOM overlay, measurement
                       toolbar, timeline scrubber.
```

**Scope:** identical Kashmir two-aircraft-plus-sensors scenario as MVP1; same
`agi.stk12` binding; apples-to-apples A/B against MVP1 on the CZML output,
with Ion SDK providing proper sensor shading + analytical primitives.

---

## STK version note — MVP1 vs MVP3 vs MVP2

MVP1 and MVP3 both target **STK 12 / `agi.stk12`** on their own ports (8001
and 8003 respectively). MVP2 targets STK 13 / `agi.stk13` on 8002. Only one
STK Engine seat is needed per running backend — stop MVP1 before starting
MVP3, or run both against STK 12 via STK Desktop attach.

---

## Prerequisites

| Tool           | Version           | Notes                                                                                  |
|----------------|-------------------|----------------------------------------------------------------------------------------|
| Python         | 3.11 or 3.12      | 3.9 too old; 3.14 often lacks wheels for NumPy/SciPy/Pillow                             |
| Node.js        | 20 + LTS          | Angular 21 requires Node 20 or later                                                    |
| STK 12         | Optional          | Without STK, `MockStkService` generates a synthetic Kashmir scenario                    |
| `agi.stk12`    | Ships with STK 12 | COM Python binding — wheel in `bin\AgPythonAPI\`                                       |
| Cesium ion SDK | 1.135             | Ships with Ansys STK (`E:\Softwares\Ansys STK\Cesium-ion-SDK-1.135\packages\`)          |

### Picking the right Python on Windows

```bash
py -0                         # list installed Python versions
py -3.11 -m venv .venv
.venv\Scripts\activate
pip install -r requirements.txt
```

### Installing `agi.stk12`

Not on PyPI. The wheel ships with STK 12 under `bin\AgPythonAPI\`:

```bash
pip install "C:\Program Files\AGI\STK 12\bin\AgPythonAPI\agi_stk12-12.x.y-py3-none-any.whl"
```

Skip this step to run against `MockStkService` — the backend exercises the
exact same CZML code path with a synthetic root, so 23/23 tests pass without
STK installed.

---

## 1 — Backend (sg-service-mvp3)

```bash
cd mvp3/sg-service-mvp3

py -3.11 -m venv .venv
.venv\Scripts\activate
pip install -r requirements.txt

# Install agi.stk12 separately — ships inside STK install (see above)
pip install "C:\Program Files\AGI\STK 12\bin\AgPythonAPI\agi_stk12-12.x.y-py3-none-any.whl"   # optional

# Run tests — all 23 pass, no STK required
pytest tests/ -v

# Start the service
python -m uvicorn main:app --port 8003 --log-level info
```

Service starts on **http://localhost:8003**.

Startup log line:

* `using StkComServiceMvp3 (live agi.stk12)` — STK Engine / Desktop available.
* `agi.stk12 not installed (...) — using MockStkService` — synthetic CZML.
* `STK Engine failed to start (...) — using MockStkService` — package present
  but no seat (license collision or Desktop unreachable).

### HTTP endpoints

| Method | Path                              | Description                                    |
|--------|-----------------------------------|------------------------------------------------|
| GET    | `/health`                         | Liveness probe                                 |
| POST   | `/exercises`                      | Create a new exercise ID                       |
| POST   | `/exercises/{id}/compute`         | `{start_time, stop_time}` → triggers compute   |
| GET    | `/exercises/{id}/status`          | `created` / `running` / `ready` / `error: ...` |
| GET    | `/exercises/{id}/czml`            | Fetch the generated CZML (409 until `ready`)   |
| GET    | `/exercises/{id}/fom-image`       | Fetch the FOM heatmap PNG (404 if none)        |
| GET    | `/models/{filename}`              | Serve STK `.glb` model files for Cesium        |

*Not implemented in MVP3:* `/exercises/from-scenario-file` (MVP1 has it;
MVP3 scoped to the hardcoded Kashmir scenario).

---

## 2 — Frontend (Angular 21 + @cesium/engine 22 + Ion SDK)

### Install

```bash
cd mvp3/frontend
npm install --install-links
npm start
```

Opens on **http://localhost:4200**. Click **Run Full Flow** to create →
compute → poll → load the generated CZML.

The `--install-links` flag is **mandatory**: it tells npm to *copy* the
local Ion SDK packages into `node_modules` instead of symlinking them.
Without the copy, esbuild can't resolve each package's `@cesium/engine`
peer dependency from the SDK folder's path and the build fails with
`Could not resolve "@cesium/engine"`.

### Dependencies

```jsonc
// mvp3/frontend/package.json
{
  "dependencies": {
    "@cesium/engine":                 "^22.3.0",
    "@cesium/widgets":                ">=14.3.0 <14.4",
    "@cesiumgs/ion-sdk-sensors":      "file:../../../../Softwares/Ansys STK/Cesium-ion-SDK-1.135/packages/ion-sdk-sensors",
    "@cesiumgs/ion-sdk-measurements": "file:../../../../Softwares/Ansys STK/Cesium-ion-SDK-1.135/packages/ion-sdk-measurements",
    "@cesiumgs/ion-sdk-geometry":     "file:../../../../Softwares/Ansys STK/Cesium-ion-SDK-1.135/packages/ion-sdk-geometry"
  }
}
```

> **Why the widget pin?** `@cesium/widgets@14.4+` bumped its dep on
> `@cesium/engine` from `^22` to `^23`/`^24`, which causes npm to install a
> *second* `@cesium/engine` under
> `node_modules/@cesium/widgets/node_modules/` — and that second copy has
> its own `ContextLimits` singleton, so any patch or CZML-DataSource state
> applied to the v22 copy is invisible to the Viewer. Pinning widgets to
> `14.3.x` keeps a single engine instance shared across everything.

### What Ion SDK 1.135 gives us

| Package                           | In `node_modules`? | Used by MVP3?                                                                     |
|-----------------------------------|--------------------|-----------------------------------------------------------------------------------|
| `@cesiumgs/ion-sdk-sensors`       | yes                | `initializeSensors(viewer)` + `processConicSensor` + `processCustomPatternSensor`  |
| `@cesiumgs/ion-sdk-measurements`  | yes                | `viewerMeasureMixin(viewer)` — distance/area/height/point toolbar                  |
| `@cesiumgs/ion-sdk-geometry`      | yes                | Installed for future `FanGraphics` / `Vector` primitives; not wired yet            |

> **No viewshed / line-of-sight primitive.** The Ion SDK 1.135 bundle
> shipped with Ansys STK does **not** contain a dedicated viewshed or
> line-of-sight primitive. As a stand-in, MVP3's Gaussian peak contour
> renders its **ellipsoid-surface intersection** (`showEllipsoidSurfaces: true`),
> which effectively drapes the main-lobe footprint over the globe.

### Wiring in `cesium-viewer.ts`

```typescript
import { Ion, EllipsoidTerrainProvider, Cartesian3, CzmlDataSource,
         ArcGisMapServerImageryProvider } from '@cesium/engine';
import { Viewer } from '@cesium/widgets';

// ContextLimits + Ion SDK mixins aren't in the shipped .d.ts → namespace-cast
import * as CesiumEngine from '@cesium/engine';
import * as IonSensors from '@cesiumgs/ion-sdk-sensors';
import * as IonMeasurements from '@cesiumgs/ion-sdk-measurements';

const ContextLimits       = (CesiumEngine    as any).ContextLimits;
const initializeSensors   = (IonSensors      as any).initializeSensors;
const viewerMeasureMixin  = (IonMeasurements as any).viewerMeasureMixin;

Ion.defaultAccessToken = '';    // no cloud calls

this.viewer = new Viewer(container, {
  animation:        true,       // clock widget (play/pause/step)
  timeline:         true,       // scrubber bar
  baseLayerPicker:  false,
  geocoder:         false,
  homeButton:       false,
  sceneModePicker:  false,
  fullscreenButton: false,
  navigationHelpButton:                   false,
  navigationInstructionsInitiallyVisible: false,
  infoBox:            false,
  selectionIndicator: false,
  terrainProvider:    new EllipsoidTerrainProvider(),
  baseLayer:          false,    // added manually below (avoids a first-tile race)
});

// --- ANGLE/D3D11 driver quirk workaround ---
// On some AMD Radeon drivers (observed on 890M under ANGLE/D3D11), Cesium's
// Context constructor records zero for several GL caps — even though a fresh
// canvas.getContext('webgl').getParameter(MAX_TEXTURE_SIZE) returns 16384.
// RenderState.fromCache() and Texture constructor then throw with
// "Width must be less than or equal to the maximum texture size (0)" or
// "renderState.lineWidth is out of range".
// Force any zero caps to WebGL-minimum defaults so validation passes.
const defaults = {
  _maximumTextureSize: 4096, _minimumAliasedLineWidth: 1, _maximumAliasedLineWidth: 1,
  /* ...22 other caps... */
};
for (const [k, v] of Object.entries(defaults)) {
  if ((ContextLimits[k] ?? 0) < 1) ContextLimits[k] = v;
}

// Deferred imagery load (avoids race with first texture upload).
ArcGisMapServerImageryProvider.fromUrl(
  'https://services.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer',
).then(p => this.viewer.imageryLayers.addImageryProvider(p));

initializeSensors(this.viewer);     // processConicSensor + processCustomPatternSensor
viewerMeasureMixin(this.viewer, { units: IonMeasurements.MeasureUnits?.METERS });
```

### CSS bundles added to `angular.json > styles`

```jsonc
"styles": [
  "src/styles.scss",
  "node_modules/@cesium/widgets/Source/widgets.css",
  "node_modules/@cesiumgs/ion-sdk-measurements/Source/Measure/Measure.css",
  "node_modules/@cesiumgs/ion-sdk-measurements/Source/Viewer/Viewer.css"
]
```

### What's deliberately NOT used

* **No Cesium ion World Terrain** — `EllipsoidTerrainProvider`.
* **No 3D Tiles streaming** — no `IonResource`, no `Cesium3DTileset`.
* **No Ion imagery** — ArcGIS World Imagery via
  `ArcGisMapServerImageryProvider.fromUrl(...)`.
* **Ion token empty** — `Ion.defaultAccessToken = ''` silences the
  default-token warning; nothing reaches `cesium.com`.
* **Cesium aircraft `model`/`label`** — the CZML we emit omits `model.gltf`
  and `label` on aircraft packets (they hit the TextureAtlas path that the
  driver-cap patch recovers, but a `point` primitive is lighter-weight and
  sufficient at our zoom level).

### Unit tests

```bash
cd mvp3/frontend
npx ng test --watch=false
```

Uses Vitest (Angular 21 default).

---

## 3 — End-to-End Walkthrough

1. Start the backend (step 1 above).
2. Start the frontend (step 2 above).
3. Open **http://localhost:4200**.
4. Adjust **Start** / **Stop** times if desired (defaults = one-hour window starting 1 Jan 2025).
5. Click **Run Full Flow**. Log overlay shows:

   ```
   10:30:42  Exercise created: <uuid>
   10:30:42  Computation started — polling status
   10:30:43  Status: running
   10:30:43  Status: ready
   10:30:43  Done — loading CZML
   ```

6. The globe flies over Kashmir (~74 °E, 34 °N). On screen:

   * **EmitterAC** at ~1.9 km altitude (Kashmir Valley west→east).
   * **RadarAC** at ~4.7 km altitude (Zanskar foothills east→west).
   * **RadarCone** — cyan `agi_conicSensor`, pointed **straight down** at
     nadir. Visible as a narrow downward cone under the radar.
   * **EmissionCone** — four nested `agi_customPatternSensor` contours at
     −3 / −6 / −10 / −20 dB, aimed **horizontally across the valley at the
     radar**. Colors grade from hot red-orange (peak) through orange to
     faint yellow (−20 dB skirt). The −3 dB peak also drapes an opaque
     orange footprint on the ellipsoid where the main lobe illuminates
     the ground.
   * **Green access polyline** between the two aircraft during LOS intervals.
   * **FOM heatmap** (only if live STK produced a CoverageDefinition/FigureOfMerit).

7. **Timeline scrubber** (bottom): drag to seek to any moment in the
   scenario. Watch the emitter's lobe swing to track the radar as they pass.
8. **Animation clock** (bottom-left): play/pause, step, speed multiplier.
9. **Measurement toolbar** (top-right icon): select distance / area / height /
   horizontal / vertical / point, then click on the globe to measure.
   Results display in metres.

---

## File Structure

```
mvp3/
├── sg-service-mvp3/
│   ├── main.py                   FastAPI shell, port 8003, bounded STK shutdown
│   ├── stk_service.py            IStkService abstract
│   ├── stk_com_service_mvp3.py   Live agi.stk12 orchestrator (Engine → Desktop fallback)
│   ├── stk_mock_service.py       Synthetic-root fallback (exercises the live CZML code path)
│   ├── scenario_builder.py       Kashmir two-aircraft + sensors (verbatim copy from MVP1)
│   ├── czml_builder_mvp3.py      DataProvider → dict packets → json.dumps
│   │                              • aircraft packet (point + path)
│   │                              • agi_conicSensor builder (nadir-aimed)
│   │                              • agi_customPatternSensor builder (4 Gaussian contours,
│   │                                aim-at-target orientation, ellipsoid drape on peak)
│   ├── cone_geometry.py          ECEF helpers + _quat_align_z_to,
│   │                              nadir_quaternion_samples, aim_quaternion_samples
│   ├── fom_pipeline.py           read_fom_grid, render_heatmap_png, inject_rectangle_overlay
│   ├── computation_job.py        Thread-pool entry
│   ├── requirements.txt          fastapi, uvicorn, httpx, pytest, Pillow, numpy, scipy
│   ├── pytest.ini                filterwarnings for starlette
│   ├── .gitignore                .venv, czml_output/, fom_images/, *.whl
│   ├── czml_output/              Generated CZML files (one per exercise)
│   ├── fom_images/               Auto-generated FOM heatmap PNGs
│   └── tests/                    pytest suite — 23 tests, no STK required
└── frontend/
    ├── angular.json              Cesium Workers/Assets + Ion SDK CSS bundled
    ├── package.json              @cesium/engine@22, widgets pinned <14.4, 3 local SDK pkgs
    └── src/app/
        ├── cesium-viewer/        Viewer + ContextLimits patch + Ion SDK sensors + measurements
        └── scenario-loader/      Run Full Flow button + status polling + log overlay
```

---

## With Real STK — Validation Checklist

After confirming `MockStkService` works, repeat with a live STK session:

- [ ] Backend log: `using StkComServiceMvp3 (live agi.stk12)` (or STKDesktop fallback line).
- [ ] `StkComServiceMvp3: scenario built for exercise <id>` after `POST /compute`.
- [ ] `StkComServiceMvp3: CZML exported -> <path> (N.N KB)` with non-zero size.
- [ ] `czml_output/<id>.czml` contains these packet IDs:
  `document`, `EmitterAC`, `EmitterAC/EmissionCone/-3dB`, `EmitterAC/EmissionCone/-6dB`,
  `EmitterAC/EmissionCone/-10dB`, `EmitterAC/EmissionCone/-20dB`, `RadarAC`,
  `RadarAC/RadarCone`, `Access/EmitterAC-RadarAC`.
- [ ] Each sensor packet carries a time-dynamic `orientation.unitQuaternion`
      (not a reference) — length divisible by 5 (`[t, qx, qy, qz, qw]` samples).
- [ ] Gaussian contours use `agi_customPatternSensor` with `directions.unitSpherical`.
      Peak `(-3dB)` has `showIntersection: true` and `showEllipsoidSurfaces: true`;
      the other three have both `false`.
- [ ] RadarCone is a single `agi_conicSensor` with `outerHalfAngle` in radians.
- [ ] Cesium renders:
  - [ ] Radar cone points straight down under its aircraft throughout the run.
  - [ ] Emitter lobe points horizontally at the radar and tracks it as both planes move.
  - [ ] Emitter ground drape (peak contour's ellipsoid surface) is visible below the lobe.
  - [ ] Timeline scrubber advances smoothly; all geometry stays time-consistent.
  - [ ] Measurement toolbar icon appears top-right and the modes work.

---

## Key Differences vs MVP1 and MVP2

| Concern                 | MVP1                                                 | MVP2 (WIP)                                | MVP3                                                                 |
|-------------------------|------------------------------------------------------|-------------------------------------------|----------------------------------------------------------------------|
| STK binding             | `agi.stk12` (STK 12)                                 | `agi.stk13` (STK 13 / STK_ODTK 13)        | `agi.stk12` (STK 12)                                                 |
| Port                    | 8001                                                 | 8002                                      | 8003                                                                 |
| CZML source             | `ExportCZML` Connect Command (plugin)                | `ExportCZML` Connect Command (plugin)     | DataProvider reads in Python                                         |
| Sensor rendering        | `agi_conicSensor` rewritten to cylinder via JSON surgery | same as MVP1                          | Native `agi_conicSensor` + `agi_customPatternSensor` via Ion SDK      |
| Sensor pointing         | Whatever `ExportCZML` emits                           | same as MVP1                              | Explicit: radar nadir, emitter aimed at radar (time-dynamic quats)    |
| Antenna shapes          | Single half-angle cones only                         | same as MVP1                              | Cones *and* 4-layer elliptical Gaussian lobe on the emitter           |
| Plugin dependency       | CZML Exporter Plugin MSI required                    | same as MVP1                              | None                                                                 |
| Frontend                | Angular 17, open-source `cesium`                     | Angular 21 standalone, open-source `cesium` | Angular 21 standalone, `@cesium/engine 22` + widgets 14.3 + Ion SDK 1.135 |
| Timeline / animation    | Enabled (vanilla Cesium)                             | Disabled in current MVP2 build            | Enabled                                                              |
| Measurement widget      | Not available                                        | Not available                             | Ion SDK `viewerMeasureMixin` (distance/area/height)                   |
| Viewshed-like drape     | n/a                                                  | n/a                                       | Beam footprint via sensor `showEllipsoidSurfaces` on peak Gaussian    |
| Scenario source         | Hardcoded Kashmir                                    | Draw-first on Cesium globe                | Hardcoded Kashmir (same as MVP1, A/B compare)                         |
| Ion cloud services      | n/a                                                  | n/a                                       | **Deliberately disabled** — no tiles, no terrain, no imagery          |
| Unit tests              | ~14 pytest + karma                                   | 21 pytest + 7 vitest                      | 23 pytest (+ vitest pending)                                         |
| FOM heatmap             | Kashmir-specific, inline methods                     | Auto, inline methods                       | Pure functions (`fom_pipeline.py`), reusable                          |
| Scenario file upload    | `/exercises/from-scenario-file` implemented          | same as MVP1                              | not implemented (out of stage scope)                                 |

---

## Known driver quirk — ANGLE / AMD Radeon / D3D11

On some AMD Radeon drivers (observed on 890M under Chrome's default ANGLE
D3D11 backend) the Cesium `Context` constructor records `0` for several GL
capability queries — even though a plain
`canvas.getContext('webgl').getParameter(MAX_TEXTURE_SIZE)` from the same
tab returns `16384`. The downstream effect is every
`RenderState.fromCache()` or `Texture` constructor call throws:

```
Width must be less than or equal to the maximum texture size (0).
renderState.lineWidth is out of range.
```

MVP3's workaround sits in `cesium-viewer.ts`: immediately after
`new Viewer(...)` it walks the `ContextLimits` singleton and writes
conservative WebGL-minimum defaults into any field that's still `0`. On a
healthy driver the block is a no-op; on the buggy one it unblocks
validation everywhere.

If the error recurs after future dependency upgrades, first verify that
only *one* `@cesium/engine` version is present in `node_modules` (the
patch only reaches the instance it imports — see the widget pin above).

## What MVP3 proves

* **CZML can be generated cleanly without the CZML Exporter plugin**, via STK DataProviders.
* **Ion SDK handles `agi_conicSensor` + `agi_customPatternSensor` natively** — no cylinder translation, no ENU quaternion math hidden in post-processing, no JSON surgery on the output. The builder produces the correct packet shape directly.
* **Directional antenna patterns render cleanly** via per-contour custom sensors with a color ramp and an ellipsoid-surface drape, giving a "lobe-on-ground" view with the tools already in the SDK — no separate viewshed engine required.
* **The architecture splits cleanly** into one FastAPI shell, one abstract service interface, one live/mock service pair, a pure CZML builder, a pure FOM pipeline, and a pure geometry helper. Each file has one clear responsibility and is independently testable.

Ship target for stage C: end-to-end visual parity (or better) with MVP1's
CZML rendering — *now with directional antenna patterns, scrubbable
timeline, and click-to-measure* — on a backend with none of MVP1's plugin
or JSON-surgery failure modes.
