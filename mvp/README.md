# EWTSS MVP — STK + CZML + CesiumJS Validation

Minimal proof-of-concept that validates the full pipeline:

```
Scenario parameters  →  FastAPI sg-service  →  STK (or mock) computation
  →  CZML export  →  Angular + CesiumJS render  →  LOS / RLOS visualisation
```

No Kafka, no TimescaleDB, no RBAC, no Electron. One FastAPI service, one Angular app.

**Scenario:** Two aircraft over Kashmir — an emitter (low altitude, Kashmir Valley) and a radar
(higher altitude, Zanskar foothills). STK computes geometry-based LOS intervals. The mock service
uses a Gaussian terrain model of the Pir Panjal and Himalayan ridges to synthesise intermittent
LOS loss (~78 % of sim-time blocked).

---

## Prerequisites

| Tool | Version | Notes |
|---|---|---|
| Python | 3.10 + | `py -3.x` launcher on Windows |
| Node.js | 18 + LTS | [nodejs.org/en/download](https://nodejs.org/en/download) |
| STK 12 | Optional | Without STK, MockStkService generates synthetic CZML automatically |

Verify after installation:

```bash
python --version   # or: py --version
node --version
npm --version
```

> **Note on scientific packages:** `requirements.txt` includes `numpy` and `scipy` for FOM heatmap rendering. These are installed automatically by `pip install -r requirements.txt`. On air-gapped machines, download the wheels from PyPI and install with `--no-index --find-links=<dir>`.

---

## 1 — Backend (sg-service)

```bash
cd mvp/sg-service

# Install Python dependencies (once) — includes numpy, scipy, Pillow
pip install -r requirements.txt

# Run tests — no STK required, all should pass
pytest tests/ -v

# Start the service
python -m uvicorn main:app --port 8001 --log-level info
```

Service starts on **http://localhost:8001**.

Log line on startup:
- `using StkComService (live STK)` — STK Desktop / Engine is running and `agi.stk12` is installed
- `using MockStkService` — STK not available; synthetic CZML is generated instead

### Using live STK

Install the Python bindings and make sure STK 12 is running before starting the service:

```bash
pip install agi.stk12
# Start STK 12 Desktop, then:
python -m uvicorn main:app --port 8001 --log-level info
```

For production / headless deployment, install **STK Desktop Engine** instead of the full STK Desktop.
The service detects the engine automatically — no GUI will be launched.

#### CZML Export Plugin (required for live STK)

The service exports CZML via STK's `ExportCZML` Connect Command, which requires the **CZML Export** plugin:

1. Open STK 12
2. Go to **Utilities → Plugin Scripts…**
3. Click **Add** and browse to the plugin script — it ships with STK as:
   ```
   C:\Program Files\AGI\STK 12\bin\Plugins\CZMLExport\CZMLExport.pl
   ```
4. Enable the plugin and click **OK**

> **Without the plugin**, `export_czml()` raises `RuntimeError: ExportCZML Connect Command failed`.
> The plugin must be installed in every STK instance (Desktop and Engine) used by this service.

---

## 2 — Frontend

```bash
cd mvp/frontend

# Install dependencies (once)
npm install

# Start dev server
npm start
```

Opens on **http://localhost:4200**.

---

## 3 — End-to-End Walkthrough

1. Start sg-service in one terminal (step 1 above)
2. Start the frontend in another terminal (step 2 above)
3. Open **http://localhost:4200** in a browser
4. Enter an exercise name and click **Run Full Flow**
5. Watch the log panel — expected sequence:

```
POST /exercises             → 201 Created: <uuid>
POST /exercises/{id}/compute
Polling status…
Status: running
Status: ready
Dispatching load-czml → http://localhost:8001/exercises/<id>/czml
Done
```

6. The globe flies to **Kashmir** (34 °N, 75 °E) and shows:
   - **Orange disc** — Emitter aircraft orbiting the Kashmir Valley at ~1 900 m
   - **Cyan cone** — Radar aircraft orbiting the Zanskar foothills at ~4 250 m
   - **Green line** — LOS (visible only during terrain-clear intervals, ~22 % of sim-time)
   - **Amber glowing line** — RLOS (same intervals as LOS)
   - **FOM heatmap** — red/green gradient overlay clipped to the AreaTarget polygon (auto-generated from STK CoverageDefinition/FigureOfMerit)
   - Timeline scrubber advances; LOS lines appear and disappear as the emitter passes into ridge shadow

### Toolbar controls

| Control | Action |
|---|---|
| ⏮ / ◀ / ⏯ / ▶ | Restart / step back 1 min / play-pause / step forward 1 min |
| Speed buttons | Set clock multiplier (1× … 300×); default 30× |
| Street / Satellite | Switch base map layer |
| Flat / 3D | Switch between ellipsoid and ArcGIS World Elevation terrain |
| Timeline scrubber | Drag to seek to any point in the scenario |

---

## With Real STK — Validation Checklist

After confirming MockStkService works, repeat with a live STK session:

- [ ] sg-service log shows `StkComService: attached to running STK instance`
- [ ] Log shows `StkComService: computation complete for exercise <id>`
- [ ] `czml_output/<id>.czml` is valid JSON with a `"document"` packet
- [ ] CZML contains `emitter/<id>` and `radar/<id>` entity packets with time-dynamic positions
- [ ] EmitterAC traces its west→east path across Kashmir Valley at 1.9 km
- [ ] RadarAC traces its east→west path over Zanskar foothills at 4.675 km
- [ ] Emitter renders as a flat horizontal disc (omnidirectional RF emission pattern)
- [ ] Radar renders as a downward nadir-pointing cone with apex at aircraft position (detection pattern)
- [ ] LOS / RLOS polylines are visible only during STK-computed access intervals
- [ ] Timeline scrubber advances and LOS lines appear / disappear correctly
- [ ] Log shows `_read_fom_grid: N points via 'Value By Point'/'FOM Value'` when a FigureOfMerit exists in the scenario
- [ ] FOM heatmap PNG is written to `fom_images/<id>.png` and visible as a georeferenced overlay on the globe
- [ ] Heatmap is clipped to the AreaTarget polygon — no colour bleeding outside the boundary
- [ ] 3-D model files (`.glb`) are served correctly by `GET /models/{filename}` for air-gapped Cesium rendering

---

## File Structure

```
mvp/
├── sg-service/
│   ├── main.py               FastAPI: exercise CRUD, compute trigger, CZML + FOM serve,
│   │                           STK model serving, scenario file upload, graceful shutdown
│   ├── stk_service.py        IStkService abstract interface (build_and_compute, export_czml, shutdown)
│   ├── stk_com_service.py    Live STK via agi.stk12 COM (STK Engine or Desktop)
│   │                           · CZML sensor patch: agi_conicSensor → native cylinder (ENU orientation)
│   │                           · FOM heatmap: auto-reads CoverageDefinition/FigureOfMerit DataProviders,
│   │                             renders scipy griddata PNG, injects georeferenced rectangle into CZML
│   ├── stk_mock_service.py   Delegates to czml_builder — no STK required
│   ├── czml_builder.py       Synthetic CZML: Gaussian terrain model, LOS computation
│   ├── scenario_builder.py   STK COM: two aircraft + conic sensors + access computation
│   ├── computation_job.py    run_computation() — called in ThreadPoolExecutor
│   ├── requirements.txt      fastapi, uvicorn, httpx, pytest, Pillow, numpy, scipy
│   ├── czml_output/          Generated CZML files (one per exercise)
│   ├── fom_images/           Auto-generated FOM heatmap PNGs (one per exercise)
│   ├── scenario_uploads/     Uploaded .sc / .vdf scenario files
│   └── tests/                pytest suite (no STK required)
└── frontend/
    └── src/app/
        ├── cesium-viewer/    Globe, playback controls, timeline scrubber, map/terrain toggle
        └── scenario-loader/  HTTP flow: create → compute → poll → load CZML
```

### Additional endpoints (live STK only)

| Method | Path | Description |
|---|---|---|
| `POST` | `/exercises/from-scenario-file` | Upload `.sc`/`.vdf`, auto-compute, return exercise ID |
| `POST` | `/exercises/{id}/fom-image` | Upload FOM PNG manually; auto-derive bounds from AreaTarget |
| `GET`  | `/exercises/{id}/fom-image` | Serve the stored FOM PNG |
| `GET`  | `/models/{filename}` | Serve STK `.glb` model files from `STKData/VO/Models/` tree |
