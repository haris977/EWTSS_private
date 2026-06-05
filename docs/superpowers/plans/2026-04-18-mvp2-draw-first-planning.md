# MVP2 — Draw-First Scenario Planning Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a draw-first scenario planning tool where an operator draws AreaTarget / Aircraft / Facility / Sensor entities on a CesiumJS globe, submits the plan to a FastAPI service, and receives STK-computed CZML rendered back on the same globe.

**Architecture:** Angular frontend draws GeoJSON entities → `POST /exercises/{id}/compute` with a `PlanningResult` body → `sg-service-v2` (FastAPI, port 8002) dispatches to `StkPytkService` (ansys.stk PySTK, in-process Engine) or `MockStkService` → CZML + FOM heatmap returned and rendered in CesiumJS.

**Tech Stack:** Python 3.10+, FastAPI 0.111, ansys-stk (PySTK), numpy, scipy, Pillow, Angular 17+, CesiumJS (npm cesium)

---

> **Scope note:** Backend (Tasks 1–7) and frontend (Tasks 8–13) are independent subsystems. The backend is fully testable without the frontend. Consider implementing and validating Tasks 1–7 before starting Task 8.

---

## File Map

```
mvp2/
├── sg-service-v2/
│   ├── main.py                     FastAPI app, port 8002, lifespan shutdown
│   ├── stk_service.py              IStkService interface (v2 signature with entities param)
│   ├── stk_pystk_service.py        StkPytkService — ansys.stk Engine lifecycle + computation
│   ├── stk_mock_service.py         MockStkService — delegates to czml_builder_v2
│   ├── scenario_builder_v2.py      build_stk_scenario_v2() — PySTK entity creation
│   ├── czml_builder_v2.py          build_synthetic_czml() — no-STK CZML from entity list
│   ├── czml_utils.py               patch_czml_sensors, inject_fom_rectangle,
│   │                               render_fom_heatmap, extract_area_target_polygon
│   │                               (pure Python, copied logic from MVP stk_com_service.py)
│   ├── computation_job.py          run_computation() — executor wrapper
│   ├── requirements.txt
│   └── tests/
│       ├── conftest.py
│       ├── test_czml_utils.py
│       ├── test_czml_builder_v2.py
│       ├── test_scenario_builder_v2.py
│       └── test_endpoints.py
└── frontend/
    └── src/app/
        ├── drawing-state.service.ts
        ├── types.ts                PlanningEntity, PlanningResult interfaces
        ├── cesium-viewer/          Globe + ScreenSpaceEventHandlers
        ├── drawing-toolbar/        Mode buttons
        ├── entity-sidebar/         Entity list + form panel
        └── scenario-planner/       HTTP flow: create → compute → poll → load CZML
```

---

## Task 1: Backend scaffold

**Files:**
- Create: `mvp2/sg-service-v2/requirements.txt`
- Create: `mvp2/sg-service-v2/stk_service.py`

- [ ] **Step 1: Create the directory structure**

```bash
mkdir -p mvp2/sg-service-v2/tests
mkdir -p mvp2/sg-service-v2/czml_output
mkdir -p mvp2/sg-service-v2/fom_images
touch mvp2/sg-service-v2/tests/__init__.py
```

- [ ] **Step 2: Write requirements.txt**

```
fastapi==0.111.0
uvicorn[standard]==0.30.1
httpx==0.27.0
pytest==8.2.2
pytest-asyncio==0.23.7
anyio==4.4.0
Pillow>=10.0.0
numpy>=1.26.0
scipy>=1.13.0
ansys-stk
```

- [ ] **Step 3: Write stk_service.py** (v2 interface — entities param added to build_and_compute)

`mvp2/sg-service-v2/stk_service.py`:
```python
from __future__ import annotations
from abc import ABC, abstractmethod


class IStkService(ABC):

    @abstractmethod
    def build_and_compute(
        self,
        exercise_id: str,
        start_time: str,
        stop_time: str,
        entities: list[dict],
    ) -> None: ...

    @abstractmethod
    def export_czml(self, exercise_id: str, output_path: str) -> None: ...

    def shutdown(self) -> None:
        pass
```

- [ ] **Step 4: Install dependencies**

```bash
cd mvp2/sg-service-v2
pip install -r requirements.txt
```

Expected: packages install without error. `ansys-stk` may warn about STK Engine not being present — that is expected.

- [ ] **Step 5: Commit**

```bash
git add mvp2/sg-service-v2/requirements.txt mvp2/sg-service-v2/stk_service.py mvp2/sg-service-v2/tests/__init__.py
git commit -m "feat(mvp2): scaffold sg-service-v2 with IStkService interface"
```

---

## Task 2: czml_utils.py

Extract the pure-Python CZML helpers from MVP's `stk_com_service.py` into a standalone module. These functions have no STK API calls — they manipulate JSON and render PNGs.

**Files:**
- Create: `mvp2/sg-service-v2/czml_utils.py`
- Create: `mvp2/sg-service-v2/tests/conftest.py`
- Create: `mvp2/sg-service-v2/tests/test_czml_utils.py`

- [ ] **Step 1: Write the failing tests**

`mvp2/sg-service-v2/tests/conftest.py`:
```python
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent.parent))
```

`mvp2/sg-service-v2/tests/test_czml_utils.py`:
```python
import json, math
from pathlib import Path
import pytest
from czml_utils import patch_czml_sensors, inject_fom_rectangle, extract_area_target_polygon


SAMPLE_CZML_WITH_SENSOR = [
    {"id": "document", "name": "test", "version": "1.0",
     "clock": {"interval": "2025-01-01T00:00:00Z/2025-01-01T01:00:00Z",
               "currentTime": "2025-01-01T00:00:00Z"}},
    {"id": "Aircraft/AC1", "name": "AC1",
     "position": {"epoch": "2025-01-01T00:00:00Z",
                  "cartesian": [0, 6378137.0+1000, 0, 0,
                                3600, 6378137.0+1000, 0, 0]}},
    {"id": "Aircraft/AC1/Sensor1", "name": "Sensor1",
     "parent": "Aircraft/AC1",
     "agi_conicSensor": {
         "outerHalfAngle": 0.436,   # 25 deg
         "radius": 15000,
         "lateralSurfaceMaterial": {
             "solidColor": {"color": {"rgba": [0, 255, 255, 100]}}
         },
     }},
]

SAMPLE_CZML_WITH_AREA_TARGET = [
    {"id": "document", "name": "test", "version": "1.0",
     "clock": {"interval": "2025-01-01T00:00:00Z/2025-01-01T01:00:00Z",
               "currentTime": "2025-01-01T00:00:00Z"}},
    {"id": "AreaTarget/Zone1", "name": "Zone1",
     "polygon": {"positions": {
         # Kashmir approx square: lon 73.5, lat 33.5 … lon 75.5, lat 35.5
         "cartographicRadians": [
             math.radians(73.5), math.radians(33.5), 0,
             math.radians(75.5), math.radians(33.5), 0,
             math.radians(75.5), math.radians(35.5), 0,
             math.radians(73.5), math.radians(35.5), 0,
             math.radians(73.5), math.radians(33.5), 0,
         ]
     }}},
]


def test_patch_czml_sensors_replaces_agi_conic(tmp_path):
    p = tmp_path / "test.czml"
    p.write_text(json.dumps(SAMPLE_CZML_WITH_SENSOR), encoding="utf-8")
    patch_czml_sensors(p)
    patched = json.loads(p.read_text())
    sensor_pkt = next(pkt for pkt in patched if "Sensor1" in pkt.get("id", ""))
    assert "agi_conicSensor" not in sensor_pkt
    assert "cylinder" in sensor_pkt
    assert sensor_pkt["cylinder"]["topRadius"] == 0.0


def test_extract_area_target_polygon_returns_degrees(tmp_path):
    p = tmp_path / "test.czml"
    p.write_text(json.dumps(SAMPLE_CZML_WITH_AREA_TARGET), encoding="utf-8")
    poly = extract_area_target_polygon(p)
    assert poly is not None
    assert len(poly) == 5
    # First vertex should be close to (73.5, 33.5) in degrees
    assert abs(poly[0][0] - 73.5) < 0.01
    assert abs(poly[0][1] - 33.5) < 0.01


def test_inject_fom_rectangle_adds_overlay(tmp_path):
    czml = list(SAMPLE_CZML_WITH_AREA_TARGET) + [
        {"id": "CoverageDefinition/Cov1/FigureOfMerit/FOM1", "name": "FOM1"}
    ]
    p = tmp_path / "test.czml"
    p.write_text(json.dumps(czml), encoding="utf-8")
    inject_fom_rectangle(p, "http://localhost:8002/exercises/test-id/fom-image")
    result = json.loads(p.read_text())
    overlay = next((pkt for pkt in result if pkt.get("id") == "fom-overlay"), None)
    assert overlay is not None
    wsen = overlay["rectangle"]["coordinates"]["wsen"]
    assert len(wsen) == 4
    # Values should be in radians — west should be ~1.28 rad (73.5 deg)
    assert abs(wsen[0] - math.radians(73.5)) < 0.01
```

- [ ] **Step 2: Run tests — confirm they fail**

```bash
cd mvp2/sg-service-v2
pytest tests/test_czml_utils.py -v
```

Expected: `ModuleNotFoundError: No module named 'czml_utils'`

- [ ] **Step 3: Write czml_utils.py**

`mvp2/sg-service-v2/czml_utils.py` — copy the following functions verbatim from `mvp/sg-service/stk_com_service.py`, renaming private methods to public:

- `_ecef_to_lonlat_rad` → keep as `_ecef_to_lonlat_rad` (private helper)
- `_enu_quaternion` → keep as `_enu_quaternion` (private helper)
- `_cone_position_and_orientation` → keep as `_cone_position_and_orientation` (private helper)
- `_patch_czml_sensors` → rename to `patch_czml_sensors(path: Path)`
- `inject_fom_rectangle` → keep name, remove `@staticmethod`
- `_extract_area_target_polygon` → rename to `extract_area_target_polygon(czml_path: Path)`
- `_render_fom_heatmap` → rename to `render_fom_heatmap(grid, img_path, polygon_deg=None)`

Add this module header and imports:

```python
"""
Pure-Python CZML post-processing utilities.
No STK API calls — safe to import without STK installed.
Copied logic from mvp/sg-service/stk_com_service.py static methods.
"""
from __future__ import annotations
import json
import logging
import math
from pathlib import Path

log = logging.getLogger(__name__)
```

The function bodies are **identical** to the MVP source. Do not modify them. Reference lines in MVP source: `_ecef_to_lonlat_rad` at line 109, `_patch_czml_sensors` at line 247, `inject_fom_rectangle` at line 177, `_extract_area_target_polygon` at line ~400, `_render_fom_heatmap` at line ~410.

- [ ] **Step 4: Run tests — confirm they pass**

```bash
pytest tests/test_czml_utils.py -v
```

Expected:
```
PASSED tests/test_czml_utils.py::test_patch_czml_sensors_replaces_agi_conic
PASSED tests/test_czml_utils.py::test_extract_area_target_polygon_returns_degrees
PASSED tests/test_czml_utils.py::test_inject_fom_rectangle_adds_overlay
```

- [ ] **Step 5: Commit**

```bash
git add mvp2/sg-service-v2/czml_utils.py mvp2/sg-service-v2/tests/conftest.py mvp2/sg-service-v2/tests/test_czml_utils.py
git commit -m "feat(mvp2): add czml_utils.py with sensor patch, FOM rectangle, heatmap render"
```

---

## Task 3: czml_builder_v2.py + MockStkService

**Files:**
- Create: `mvp2/sg-service-v2/czml_builder_v2.py`
- Create: `mvp2/sg-service-v2/stk_mock_service.py`
- Create: `mvp2/sg-service-v2/tests/test_czml_builder_v2.py`

- [ ] **Step 1: Write failing tests**

`mvp2/sg-service-v2/tests/test_czml_builder_v2.py`:
```python
import json, math
from czml_builder_v2 import build_synthetic_czml

START = "1 Jan 2025 00:00:00.000"
STOP  = "1 Jan 2025 01:00:00.000"

ENTITIES = [
    {
        "type": "Feature",
        "geometry": {"type": "Polygon", "coordinates": [
            [[73.5, 33.5], [75.5, 33.5], [75.5, 35.5], [73.5, 35.5], [73.5, 33.5]]
        ]},
        "properties": {"entityType": "AreaTarget", "name": "Zone1"},
    },
    {
        "type": "Feature",
        "geometry": {"type": "LineString",
                     "coordinates": [[74.0, 34.0, 1900], [75.0, 34.0, 1900]]},
        "properties": {"entityType": "Aircraft", "name": "AC1", "speedMs": 150.0},
    },
    {
        "type": "Feature",
        "geometry": {"type": "Point", "coordinates": [74.5, 34.5, 0]},
        "properties": {"entityType": "Facility", "name": "Site1"},
    },
]


def test_document_packet_is_first():
    packets = build_synthetic_czml("eid1", START, STOP, ENTITIES)
    assert packets[0]["id"] == "document"
    assert "clock" in packets[0]


def test_area_target_polygon_packet_present():
    packets = build_synthetic_czml("eid1", START, STOP, ENTITIES)
    at = next((p for p in packets if "AreaTarget/Zone1" in p.get("id", "")), None)
    assert at is not None
    rad = at["polygon"]["positions"]["cartographicRadians"]
    # First lon should be radians(73.5)
    assert abs(rad[0] - math.radians(73.5)) < 0.01


def test_aircraft_position_track_present():
    packets = build_synthetic_czml("eid1", START, STOP, ENTITIES)
    ac = next((p for p in packets if "Aircraft/AC1" in p.get("id", "")), None)
    assert ac is not None
    assert "position" in ac
    cart = ac["position"]["cartesian"]
    # [t, x, y, z, t, x, y, z] — 8 values for 2 waypoints
    assert len(cart) == 8
    assert cart[0] == 0.0    # first sample at t=0


def test_facility_packet_present():
    packets = build_synthetic_czml("eid1", START, STOP, ENTITIES)
    fac = next((p for p in packets if "Facility/Site1" in p.get("id", "")), None)
    assert fac is not None
    assert "position" in fac


def test_sensor_entities_are_skipped():
    entities_with_sensor = ENTITIES + [{
        "type": "Feature",
        "geometry": {"type": "Point", "coordinates": [74.0, 34.0, 1900]},
        "properties": {"entityType": "Sensor", "name": "Snsr1",
                       "parentEntity": "AC1", "halfAngleDeg": 25.0},
    }]
    packets = build_synthetic_czml("eid1", START, STOP, entities_with_sensor)
    sensor_pkt = next((p for p in packets if "Sensor" in p.get("id", "")), None)
    assert sensor_pkt is None
```

- [ ] **Step 2: Run tests — confirm they fail**

```bash
pytest tests/test_czml_builder_v2.py -v
```

Expected: `ModuleNotFoundError: No module named 'czml_builder_v2'`

- [ ] **Step 3: Write czml_builder_v2.py**

`mvp2/sg-service-v2/czml_builder_v2.py`:
```python
"""
Builds synthetic CZML from a PlanningResult entity list — no STK required.
Used by MockStkService for testing and development.
"""
from __future__ import annotations
import math
from datetime import datetime, timezone


def _parse_stk_time(s: str) -> datetime:
    return datetime.strptime(s.strip(), "%d %b %Y %H:%M:%S.%f").replace(tzinfo=timezone.utc)


def _geodetic_to_ecef(lon_deg: float, lat_deg: float, alt_m: float) -> tuple[float, float, float]:
    a  = 6_378_137.0
    e2 = 0.006_694_379_990_14
    lat = math.radians(lat_deg)
    lon = math.radians(lon_deg)
    N = a / math.sqrt(1 - e2 * math.sin(lat) ** 2)
    x = (N + alt_m) * math.cos(lat) * math.cos(lon)
    y = (N + alt_m) * math.cos(lat) * math.sin(lon)
    z = (N * (1 - e2) + alt_m) * math.sin(lat)
    return x, y, z


def build_synthetic_czml(
    exercise_id: str,
    start_time_str: str,
    stop_time_str: str,
    entities: list[dict],
) -> list[dict]:
    t0 = _parse_stk_time(start_time_str)
    t1 = _parse_stk_time(stop_time_str)
    iso0     = t0.strftime("%Y-%m-%dT%H:%M:%SZ")
    iso1     = t1.strftime("%Y-%m-%dT%H:%M:%SZ")
    interval = f"{iso0}/{iso1}"
    duration = (t1 - t0).total_seconds()

    packets: list[dict] = [{
        "id": "document", "name": f"MVP2 {exercise_id}", "version": "1.0",
        "clock": {"interval": interval, "currentTime": iso0,
                  "multiplier": 30, "range": "LOOP_STOP",
                  "step": "SYSTEM_CLOCK_MULTIPLIER"},
    }]

    for ent in entities:
        etype = ent["properties"]["entityType"]
        name  = ent["properties"]["name"]

        if etype == "AreaTarget":
            ring = ent["geometry"]["coordinates"][0]
            flat: list[float] = []
            for lon, lat in ring:
                flat += [math.radians(lon), math.radians(lat), 0.0]
            packets.append({
                "id": f"AreaTarget/{name}", "name": name,
                "polygon": {
                    "positions": {"cartographicRadians": flat},
                    "material": {"solidColor": {"color": {"rgba": [255, 165, 0, 80]}}},
                    "outline": True,
                    "outlineColor": {"rgba": [255, 165, 0, 255]},
                    "height": 0.0,
                },
            })

        elif etype == "Aircraft":
            coords = ent["geometry"]["coordinates"]   # [[lon, lat, alt_m], ...]
            n = len(coords)
            cartesian: list[float] = []
            for i, wp in enumerate(coords):
                lon, lat, alt_m = wp[0], wp[1], wp[2] if len(wp) > 2 else 0.0
                t_s = duration * i / max(n - 1, 1)
                x, y, z = _geodetic_to_ecef(lon, lat, alt_m)
                cartesian += [t_s, x, y, z]
            packets.append({
                "id": f"Aircraft/{name}", "name": name,
                "availability": interval,
                "position": {
                    "epoch": iso0,
                    "interpolationAlgorithm": "LAGRANGE",
                    "interpolationDegree": 1,
                    "cartesian": cartesian,
                },
                "point": {"pixelSize": 8, "color": {"rgba": [0, 200, 255, 255]}},
                "label": {"text": name, "font": "bold 12pt sans-serif",
                          "fillColor": {"rgba": [255, 255, 255, 255]},
                          "style": "FILL_AND_OUTLINE",
                          "outlineColor": {"rgba": [0, 0, 0, 255]}, "outlineWidth": 2},
                "path": {"show": True, "leadTime": 0, "trailTime": 7200, "width": 2,
                         "material": {"solidColor": {"color": {"rgba": [0, 200, 255, 180]}}}},
            })

        elif etype == "Facility":
            coords = ent["geometry"]["coordinates"]
            lon, lat = coords[0], coords[1]
            alt_m = coords[2] if len(coords) > 2 else 0.0
            packets.append({
                "id": f"Facility/{name}", "name": name,
                "position": {"cartographicDegrees": [lon, lat, alt_m]},
                "point": {"pixelSize": 10, "color": {"rgba": [255, 255, 0, 255]}},
                "label": {"text": name, "font": "bold 11pt sans-serif",
                          "fillColor": {"rgba": [255, 255, 0, 255]},
                          "style": "FILL_AND_OUTLINE",
                          "outlineColor": {"rgba": [0, 0, 0, 255]}, "outlineWidth": 2,
                          "pixelOffset": {"cartesian2": [0, -16]}},
            })
        # Sensor: no independent position — skip in synthetic output

    return packets
```

- [ ] **Step 4: Write stk_mock_service.py**

`mvp2/sg-service-v2/stk_mock_service.py`:
```python
from __future__ import annotations
import json
import logging
from pathlib import Path

from czml_builder_v2 import build_synthetic_czml
from stk_service import IStkService

log = logging.getLogger(__name__)

_DEFAULT_START = "1 Jan 2025 00:00:00.000"
_DEFAULT_STOP  = "1 Jan 2025 01:00:00.000"


class MockStkService(IStkService):

    def __init__(self) -> None:
        self._exercises: dict[str, dict] = {}

    def build_and_compute(self, exercise_id, start_time, stop_time, entities):
        self._exercises[exercise_id] = {
            "start_time": start_time,
            "stop_time":  stop_time,
            "entities":   entities,
        }

    def export_czml(self, exercise_id, output_path):
        ex = self._exercises.get(exercise_id) or {
            "start_time": _DEFAULT_START,
            "stop_time":  _DEFAULT_STOP,
            "entities":   [],
        }
        packets = build_synthetic_czml(
            exercise_id, ex["start_time"], ex["stop_time"], ex["entities"]
        )
        Path(output_path).write_text(json.dumps(packets, indent=2), encoding="utf-8")
```

- [ ] **Step 5: Run tests — confirm they pass**

```bash
pytest tests/test_czml_builder_v2.py -v
```

Expected: all 5 tests PASSED.

- [ ] **Step 6: Commit**

```bash
git add mvp2/sg-service-v2/czml_builder_v2.py mvp2/sg-service-v2/stk_mock_service.py mvp2/sg-service-v2/tests/test_czml_builder_v2.py
git commit -m "feat(mvp2): add czml_builder_v2 and MockStkService"
```

---

## Task 4: main.py + computation_job.py

**Files:**
- Create: `mvp2/sg-service-v2/main.py`
- Create: `mvp2/sg-service-v2/computation_job.py`

- [ ] **Step 1: Write computation_job.py**

`mvp2/sg-service-v2/computation_job.py`:
```python
from __future__ import annotations
import logging

log = logging.getLogger(__name__)


def run_computation(
    exercise_id: str,
    start_time: str,
    stop_time: str,
    output_path: str,
    entities: list[dict],
) -> None:
    from stk_pystk_service import get_stk_service

    log.info("computation_job: starting exercise %s", exercise_id)
    svc = get_stk_service()
    svc.build_and_compute(exercise_id, start_time, stop_time, entities)
    svc.export_czml(exercise_id, output_path)
    log.info("computation_job: done, CZML at %s", output_path)
```

- [ ] **Step 2: Write main.py**

`mvp2/sg-service-v2/main.py`:
```python
from __future__ import annotations
import asyncio
import functools
import logging
import os
import uuid
from concurrent.futures import ThreadPoolExecutor
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Any

from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse
from pydantic import BaseModel

_log_level = os.environ.get("LOG_LEVEL", "INFO").upper()
logging.basicConfig(level=getattr(logging, _log_level, logging.INFO),
                    format="%(levelname)-8s %(name)s: %(message)s")

log = logging.getLogger(__name__)

_STK_MODELS_CANDIDATES = [
    Path(r"C:\Program Files\AGI\STK 12\STKData\VO\Models"),
    Path(r"C:\Program Files\AGI\STK 12.x\STKData\VO\Models"),
]
_stk_models_dir = next((p for p in _STK_MODELS_CANDIDATES if p.is_dir()), None)

_exercises: dict[str, dict[str, Any]] = {}
_pool = ThreadPoolExecutor(max_workers=1)  # single seat — must stay 1

CZML_DIR = Path("czml_output"); CZML_DIR.mkdir(exist_ok=True)
FOM_DIR  = Path("fom_images");  FOM_DIR.mkdir(exist_ok=True)


@asynccontextmanager
async def lifespan(app: FastAPI):  # noqa: ARG001
    yield
    from stk_pystk_service import _service_instance
    if _service_instance is not None:
        _service_instance.shutdown()


app = FastAPI(title="EWTSS MVP2 sg-service-v2", lifespan=lifespan)
app.add_middleware(CORSMiddleware,
                   allow_origins=["http://localhost:4200", "http://127.0.0.1:4200"],
                   allow_methods=["*"], allow_headers=["*"])


class ScenarioTime(BaseModel):
    start: str = "1 Jan 2025 00:00:00.000"
    stop:  str = "1 Jan 2025 01:00:00.000"

class PlanningResult(BaseModel):
    exerciseId:   str
    scenarioTime: ScenarioTime
    entities:     list[dict]


@app.get("/health")
def health() -> dict[str, str]:
    return {"status": "ok"}


@app.get("/models/{filename}")
def get_model(filename: str) -> FileResponse:
    if _stk_models_dir:
        matches = list(_stk_models_dir.rglob(filename))
        if matches:
            return FileResponse(str(matches[0]), media_type="model/gltf-binary")
    raise HTTPException(status_code=404, detail=f"Model '{filename}' not found")


@app.post("/exercises", status_code=201)
def create_exercise() -> dict[str, str]:
    eid = str(uuid.uuid4())
    _exercises[eid] = {"status": "created", "czml_path": None}
    return {"exercise_id": eid, "status": "created"}


@app.post("/exercises/{exercise_id}/compute", status_code=202)
async def trigger_compute(exercise_id: str, body: PlanningResult) -> dict[str, str]:
    if exercise_id not in _exercises:
        raise HTTPException(status_code=404, detail="exercise not found")
    ex = _exercises[exercise_id]
    if ex["status"] in ("running", "ready"):
        return {"exercise_id": exercise_id, "status": ex["status"]}
    ex.update(status="running", plan=body.model_dump())
    loop = asyncio.get_event_loop()
    loop.run_in_executor(_pool, functools.partial(_run_compute, exercise_id))
    return {"exercise_id": exercise_id, "status": "running"}


def _run_compute(exercise_id: str) -> None:
    from computation_job import run_computation
    ex   = _exercises[exercise_id]
    plan = ex["plan"]
    out  = str(CZML_DIR / f"{exercise_id}.czml")
    try:
        run_computation(
            exercise_id,
            plan["scenarioTime"]["start"],
            plan["scenarioTime"]["stop"],
            out,
            plan["entities"],
        )
        _exercises[exercise_id].update(status="ready", czml_path=out)
    except Exception as exc:
        _exercises[exercise_id]["status"] = f"error: {exc}"
        log.exception("compute failed for %s", exercise_id)


@app.get("/exercises/{exercise_id}/status")
def get_status(exercise_id: str) -> dict[str, str]:
    if exercise_id not in _exercises:
        raise HTTPException(status_code=404, detail="exercise not found")
    return {"exercise_id": exercise_id, "status": _exercises[exercise_id]["status"]}


@app.get("/exercises/{exercise_id}/czml")
def get_czml(exercise_id: str) -> FileResponse:
    if exercise_id not in _exercises:
        raise HTTPException(status_code=404, detail="exercise not found")
    ex = _exercises[exercise_id]
    if ex["status"] != "ready" or not ex["czml_path"]:
        raise HTTPException(status_code=409, detail=f"computation status: {ex['status']}")
    return FileResponse(ex["czml_path"], media_type="application/json",
                        headers={"Content-Disposition": f'inline; filename="{exercise_id}.czml"'})


@app.get("/exercises/{exercise_id}/fom-image")
def get_fom_image(exercise_id: str) -> FileResponse:
    for suffix in (".png", ".jpg", ".jpeg"):
        p = FOM_DIR / f"{exercise_id}{suffix}"
        if p.exists():
            return FileResponse(str(p))
    raise HTTPException(status_code=404, detail="FOM image not found")
```

- [ ] **Step 3: Write stk_pystk_service.py (factory only for now)**

`mvp2/sg-service-v2/stk_pystk_service.py`:
```python
from __future__ import annotations
import logging
from stk_service import IStkService

log = logging.getLogger(__name__)
_service_instance: IStkService | None = None


def get_stk_service() -> IStkService:
    global _service_instance
    if _service_instance is not None:
        return _service_instance
    try:
        from ansys.stk.application import STKEngine  # type: ignore[import]
        _service_instance = StkPytkService()
        log.info("using StkPytkService (live PySTK Engine)")
    except Exception as exc:
        log.warning("PySTK unavailable (%s) — using MockStkService", exc)
        from stk_mock_service import MockStkService
        _service_instance = MockStkService()
    return _service_instance


class StkPytkService(IStkService):
    """Placeholder — implemented in Task 7."""

    def __init__(self) -> None:
        from ansys.stk.application import STKEngine  # type: ignore[import]
        self._engine = STKEngine.start_application()
        self._root   = self._engine.root
        log.info("StkPytkService: Engine started")

    def build_and_compute(self, exercise_id, start_time, stop_time, entities):
        from scenario_builder_v2 import build_stk_scenario_v2
        build_stk_scenario_v2(self._root, exercise_id, start_time, stop_time, entities)

    def export_czml(self, exercise_id, output_path):
        raise NotImplementedError("implemented in Task 7")

    def shutdown(self):
        try:
            self._engine.stop_application()
        except Exception:
            pass
```

- [ ] **Step 4: Verify service starts**

```bash
cd mvp2/sg-service-v2
python -m uvicorn main:app --port 8002 --log-level info
```

Expected log line: `using MockStkService` (PySTK not installed yet, or Engine not running).
Open `http://localhost:8002/health` — should return `{"status": "ok"}`.
Stop with Ctrl+C.

- [ ] **Step 5: Commit**

```bash
git add mvp2/sg-service-v2/main.py mvp2/sg-service-v2/computation_job.py mvp2/sg-service-v2/stk_pystk_service.py
git commit -m "feat(mvp2): add main.py FastAPI app and computation_job"
```

---

## Task 5: Endpoint integration tests

**Files:**
- Create: `mvp2/sg-service-v2/tests/test_endpoints.py`

- [ ] **Step 1: Write tests**

`mvp2/sg-service-v2/tests/test_endpoints.py`:
```python
"""
All tests use MockStkService — ansys.stk not required.
Block PySTK import so the factory falls back to Mock automatically.
"""
import sys, time
sys.modules.setdefault("ansys", None)          # type: ignore[assignment]
sys.modules.setdefault("ansys.stk", None)      # type: ignore[assignment]

from fastapi.testclient import TestClient
from main import app

client = TestClient(app)

PLAN = {
    "exerciseId": "test-eid-001",
    "scenarioTime": {"start": "1 Jan 2025 00:00:00.000", "stop": "1 Jan 2025 01:00:00.000"},
    "entities": [
        {
            "type": "Feature",
            "geometry": {"type": "Polygon", "coordinates": [
                [[73.5, 33.5], [75.5, 33.5], [75.5, 35.5], [73.5, 35.5], [73.5, 33.5]]
            ]},
            "properties": {"entityType": "AreaTarget", "name": "Zone1"},
        },
        {
            "type": "Feature",
            "geometry": {"type": "LineString",
                         "coordinates": [[74.0, 34.0, 1900], [75.0, 34.0, 1900]]},
            "properties": {"entityType": "Aircraft", "name": "AC1", "speedMs": 150.0},
        },
    ],
}


def test_health():
    r = client.get("/health")
    assert r.status_code == 200


def test_create_exercise():
    r = client.post("/exercises")
    assert r.status_code == 201
    assert "exercise_id" in r.json()


def _create_and_wait(plan=PLAN, timeout=15):
    eid = client.post("/exercises").json()["exercise_id"]
    r = client.post(f"/exercises/{eid}/compute", json={**plan, "exerciseId": eid})
    assert r.status_code == 202
    deadline = time.time() + timeout
    while time.time() < deadline:
        status = client.get(f"/exercises/{eid}/status").json()["status"]
        if status == "ready":
            return eid
        if status.startswith("error"):
            raise AssertionError(f"compute error: {status}")
        time.sleep(0.2)
    raise AssertionError("timed out waiting for ready")


def test_full_flow_mock_czml():
    eid = _create_and_wait()
    r = client.get(f"/exercises/{eid}/czml")
    assert r.status_code == 200
    packets = r.json()
    assert packets[0]["id"] == "document"
    ids = {p["id"] for p in packets}
    assert "AreaTarget/Zone1" in ids
    assert "Aircraft/AC1" in ids


def test_czml_409_before_compute():
    eid = client.post("/exercises").json()["exercise_id"]
    r = client.get(f"/exercises/{eid}/czml")
    assert r.status_code == 409


def test_status_404_unknown():
    r = client.get("/exercises/no-such-id/status")
    assert r.status_code == 404


def test_compute_with_no_entities():
    plan = {**PLAN, "entities": []}
    eid = _create_and_wait(plan)
    r = client.get(f"/exercises/{eid}/czml")
    assert r.status_code == 200
    packets = r.json()
    assert packets[0]["id"] == "document"
```

- [ ] **Step 2: Run tests — confirm they fail**

```bash
pytest tests/test_endpoints.py -v
```

Expected: some tests fail because `main.py` imports `stk_pystk_service` which may error. Debug until mock fallback works.

- [ ] **Step 3: Run full test suite**

```bash
pytest tests/ -v
```

Expected: all tests in `test_czml_utils.py`, `test_czml_builder_v2.py`, `test_endpoints.py` PASS.

- [ ] **Step 4: Commit**

```bash
git add mvp2/sg-service-v2/tests/test_endpoints.py
git commit -m "test(mvp2): add endpoint integration tests with MockStkService"
```

---

## Task 6: scenario_builder_v2.py

**Files:**
- Create: `mvp2/sg-service-v2/scenario_builder_v2.py`
- Create: `mvp2/sg-service-v2/tests/test_scenario_builder_v2.py`

- [ ] **Step 1: Write failing tests**

`mvp2/sg-service-v2/tests/test_scenario_builder_v2.py`:
```python
"""
Tests for build_stk_scenario_v2.
All PySTK objects are mocked — no real STK Engine required.
"""
from unittest.mock import MagicMock, patch, call
import pytest
from scenario_builder_v2 import build_stk_scenario_v2

START = "1 Jan 2025 00:00:00.000"
STOP  = "1 Jan 2025 01:00:00.000"


def _make_root():
    """Build a MagicMock that mimics the PySTK root object."""
    root = MagicMock()
    sc   = MagicMock()
    root.current_scenario = sc
    sc.children.new.return_value = MagicMock()
    return root, sc


def test_area_target_connect_command():
    root, sc = _make_root()
    entities = [{
        "type": "Feature",
        "geometry": {"type": "Polygon", "coordinates": [
            [[73.5, 33.5], [75.5, 33.5], [75.5, 35.5], [73.5, 33.5]]
        ]},
        "properties": {"entityType": "AreaTarget", "name": "Zone1"},
    }]
    with patch("scenario_builder_v2._import_stk_types"):
        build_stk_scenario_v2(root, "eid1", START, STOP, entities)

    cmd_args = [str(c) for c in root.execute_command.call_args_list]
    assert any("SetState */AreaTarget/Zone1 PatternOld" in str(c) for c in cmd_args), \
        f"No SetState command found. Calls: {cmd_args}"
    # Verify Lat/Lon order (STK expects Lat first)
    cmd = next(c for c in cmd_args if "SetState */AreaTarget/Zone1" in c)
    assert "Lat 33.5" in cmd and "Lon 73.5" in cmd


def test_aircraft_waypoints_speed_in_knots():
    root, sc = _make_root()
    aircraft_mock = MagicMock()
    sc.children.new.return_value = aircraft_mock
    wp_mock = MagicMock()
    aircraft_mock.route.waypoints.add.return_value = wp_mock

    entities = [{
        "type": "Feature",
        "geometry": {"type": "LineString",
                     "coordinates": [[74.0, 34.0, 1900.0], [75.0, 34.0, 1900.0]]},
        "properties": {"entityType": "Aircraft", "name": "AC1", "speedMs": 150.0},
    }]
    with patch("scenario_builder_v2._import_stk_types"):
        build_stk_scenario_v2(root, "eid1", START, STOP, entities)

    # 150 m/s * 1.94384 = 291.576 knots
    assert abs(wp_mock.speed - 150.0 * 1.94384) < 0.01
    # altitude should be in km: 1900m / 1000 = 1.9
    assert abs(wp_mock.altitude - 1.9) < 0.001


def test_facility_altitude_in_metres():
    root, sc = _make_root()
    fac_mock = MagicMock()
    sc.children.new.return_value = fac_mock

    entities = [{
        "type": "Feature",
        "geometry": {"type": "Point", "coordinates": [74.5, 34.5, 500.0]},
        "properties": {"entityType": "Facility", "name": "Site1"},
    }]
    with patch("scenario_builder_v2._import_stk_types"):
        build_stk_scenario_v2(root, "eid1", START, STOP, entities)

    # assign_planetodetic(lat, lon, alt_m) — alt is 500.0 metres, NOT converted
    fac_mock.position.assign_planetodetic.assert_called_once_with(34.5, 74.5, 500.0)


def test_sensor_attached_to_parent():
    root, sc = _make_root()
    aircraft_mock = MagicMock()
    sensor_mock   = MagicMock()
    def _new_side(obj_type, name):
        if "AIRCRAFT" in str(obj_type) or name == "AC1":
            return aircraft_mock
        return sensor_mock
    sc.children.new.side_effect = _new_side

    entities = [
        {"type": "Feature",
         "geometry": {"type": "LineString",
                      "coordinates": [[74.0, 34.0, 1900.0], [75.0, 34.0, 1900.0]]},
         "properties": {"entityType": "Aircraft", "name": "AC1", "speedMs": 150.0}},
        {"type": "Feature",
         "geometry": {"type": "Point", "coordinates": [74.0, 34.0, 1900.0]},
         "properties": {"entityType": "Sensor", "name": "Snsr1",
                        "parentEntity": "AC1", "halfAngleDeg": 25.0}},
    ]
    with patch("scenario_builder_v2._import_stk_types"):
        build_stk_scenario_v2(root, "eid1", START, STOP, entities)

    aircraft_mock.children.new.assert_called()


def test_coverage_fom_created_when_area_target_and_aircraft_present():
    root, sc = _make_root()
    entities = [
        {"type": "Feature",
         "geometry": {"type": "Polygon", "coordinates": [
             [[73.5, 33.5], [75.5, 33.5], [75.5, 35.5], [73.5, 33.5]]
         ]},
         "properties": {"entityType": "AreaTarget", "name": "Zone1"}},
        {"type": "Feature",
         "geometry": {"type": "LineString",
                      "coordinates": [[74.0, 34.0, 1900.0], [75.0, 34.0, 1900.0]]},
         "properties": {"entityType": "Aircraft", "name": "AC1", "speedMs": 150.0}},
    ]
    with patch("scenario_builder_v2._import_stk_types"):
        build_stk_scenario_v2(root, "eid1", START, STOP, entities)

    # CoverageDefinition and FigureOfMerit must be created as children of scenario
    names_created = [str(c) for c in sc.children.new.call_args_list]
    assert any("Cov_Zone1" in n for n in names_created), \
        f"No CoverageDefinition created. Calls: {names_created}"
```

- [ ] **Step 2: Run tests — confirm they fail**

```bash
pytest tests/test_scenario_builder_v2.py -v
```

Expected: `ModuleNotFoundError: No module named 'scenario_builder_v2'`

- [ ] **Step 3: Write scenario_builder_v2.py**

`mvp2/sg-service-v2/scenario_builder_v2.py`:
```python
"""
Builds an STK scenario from PlanningResult entities via ansys.stk (PySTK).
API: agi.stk12 COM is NOT imported here — PySTK exclusively.
"""
from __future__ import annotations
import logging

log = logging.getLogger(__name__)


def _import_stk_types():
    """Centralise PySTK imports so tests can patch this one function."""
    from ansys.stk.objects import STKObjectType                         # type: ignore
    from ansys.stk.objects.sensor import SensorPattern                  # type: ignore
    from ansys.stk.objects.coverage_definition import CoverageBounds    # type: ignore
    return STKObjectType, SensorPattern, CoverageBounds


def _add_area_target(root, scenario, name: str, polygon_lonlat: list):
    STKObjectType, _, _ = _import_stk_types()
    at = scenario.children.new(STKObjectType.AREA_TARGET, name)
    coords = " ".join(f"Lat {lat:.6f} Lon {lon:.6f}" for lon, lat in polygon_lonlat)
    root.execute_command(f"SetState */AreaTarget/{name} PatternOld {coords}")
    log.info("scenario_builder_v2: AreaTarget %s (%d vertices)", name, len(polygon_lonlat))
    return at


def _add_aircraft(scenario, name: str, waypoints_lonlat_altm: list, speed_ms: float):
    STKObjectType, _, _ = _import_stk_types()
    aircraft  = scenario.children.new(STKObjectType.AIRCRAFT, name)
    route     = aircraft.route
    speed_kn  = speed_ms * 1.94384   # knots — PySTK waypoint speed unit
    for wp in waypoints_lonlat_altm:
        lon, lat, alt_m = wp[0], wp[1], wp[2] if len(wp) > 2 else 0.0
        w           = route.waypoints.add()
        w.latitude  = lat
        w.longitude = lon
        w.altitude  = alt_m / 1000.0   # km
        w.speed     = speed_kn
    route.propagate()
    log.info("scenario_builder_v2: aircraft %s (%d waypoints)", name, len(waypoints_lonlat_altm))
    return aircraft


def _add_facility(scenario, name: str, lat: float, lon: float, alt_m: float = 0.0):
    STKObjectType, _, _ = _import_stk_types()
    facility = scenario.children.new(STKObjectType.FACILITY, name)
    facility.position.assign_planetodetic(lat, lon, alt_m)   # alt in metres
    log.info("scenario_builder_v2: facility %s (%.4f, %.4f)", name, lat, lon)
    return facility


def _add_sensor(parent, name: str, half_angle_deg: float):
    STKObjectType, SensorPattern, _ = _import_stk_types()
    sensor = parent.children.new(STKObjectType.SENSOR, name)
    sensor.set_pattern_type(SensorPattern.SIMPLE_CONIC)
    sensor.common_tasks.set_pattern_simple_conic(half_angle_deg, 1)
    log.info("scenario_builder_v2: sensor %s (half-angle %.1f°)", name, half_angle_deg)
    return sensor


def _add_coverage_fom(scenario, area_target_name: str, asset_objects: list):
    STKObjectType, _, CoverageBounds = _import_stk_types()
    cov = scenario.children.new(STKObjectType.COVERAGE_DEFINITION, f"Cov_{area_target_name}")
    try:
        cov.grid.bounds_type = CoverageBounds.CUSTOM_REGIONS
    except Exception:
        cov.grid.bounds_type = 2   # integer fallback if enum name differs
    cov.grid.bounds.area_targets.add(f"AreaTarget/{area_target_name}")
    for obj in asset_objects:
        cov.asset_list.add(obj.instance_name)
    cov.compute_accesses()
    fom = cov.children.new(STKObjectType.FIGURE_OF_MERIT, f"FOM_{area_target_name}")
    fom.set_definition_type(1)   # SimpleAccessCoverage
    fom.data_definition.compute()
    log.info("scenario_builder_v2: CoverageDefinition + FOM created for %s", area_target_name)
    return cov, fom


def build_stk_scenario_v2(root, exercise_id, start_time, stop_time, entities):
    STKObjectType, _, _ = _import_stk_types()

    try:
        root.close_scenario()
    except Exception:
        pass
    root.new_scenario(f"MVP2_{exercise_id.replace('-', '_')}")
    sc = root.current_scenario
    sc.start_time = start_time
    sc.stop_time  = stop_time

    stk_objects: dict[str, object] = {}

    for ent in entities:
        etype = ent["properties"]["entityType"]
        name  = ent["properties"]["name"]

        if etype == "AreaTarget":
            ring = ent["geometry"]["coordinates"][0]
            stk_objects[name] = _add_area_target(root, sc, name, ring)

        elif etype == "Aircraft":
            coords   = ent["geometry"]["coordinates"]
            speed_ms = ent["properties"].get("speedMs", 150.0)
            stk_objects[name] = _add_aircraft(sc, name, coords, speed_ms)

        elif etype == "Facility":
            coords = ent["geometry"]["coordinates"]
            lon, lat = coords[0], coords[1]
            alt_m = coords[2] if len(coords) > 2 else 0.0
            stk_objects[name] = _add_facility(sc, name, lat, lon, alt_m)

        elif etype == "Sensor":
            parent_name = ent["properties"]["parentEntity"]
            half_angle  = ent["properties"].get("halfAngleDeg", 25.0)
            parent_obj  = stk_objects.get(parent_name)
            if parent_obj is None:
                raise ValueError(
                    f"Sensor '{name}': parent '{parent_name}' not yet created. "
                    "Reorder entities so parents appear before sensors."
                )
            stk_objects[name] = _add_sensor(parent_obj, name, half_angle)

    # Auto CoverageDefinition + FOM
    at_names      = [e["properties"]["name"] for e in entities
                     if e["properties"]["entityType"] == "AreaTarget"]
    aircraft_objs = [stk_objects[e["properties"]["name"]] for e in entities
                     if e["properties"]["entityType"] == "Aircraft"]
    if at_names and aircraft_objs:
        _add_coverage_fom(sc, at_names[0], aircraft_objs)

    # Access computation — all aircraft pairs
    for i, a in enumerate(aircraft_objs):
        for b in aircraft_objs[i + 1:]:
            a.get_access_to_object(b).compute_access()
            log.info("scenario_builder_v2: access %s ↔ %s",
                     a.instance_name, b.instance_name)
```

- [ ] **Step 4: Run tests — confirm they pass**

```bash
pytest tests/test_scenario_builder_v2.py -v
```

Expected: all 5 tests PASS.

- [ ] **Step 5: Run full suite**

```bash
pytest tests/ -v
```

Expected: all tests PASS.

- [ ] **Step 6: Commit**

```bash
git add mvp2/sg-service-v2/scenario_builder_v2.py mvp2/sg-service-v2/tests/test_scenario_builder_v2.py
git commit -m "feat(mvp2): add scenario_builder_v2 with PySTK entity creation"
```

---

## Task 7: Complete StkPytkService (FOM pipeline)

**Files:**
- Modify: `mvp2/sg-service-v2/stk_pystk_service.py`

- [ ] **Step 1: Replace the placeholder export_czml and add FOM pipeline**

Replace the entire `StkPytkService` class body in `stk_pystk_service.py` with:

```python
class StkPytkService(IStkService):

    def __init__(self) -> None:
        from ansys.stk.application import STKEngine  # type: ignore[import]
        self._engine = STKEngine.start_application()
        self._root   = self._engine.root
        log.info("StkPytkService: STK Engine started (PySTK)")

    def build_and_compute(self, exercise_id, start_time, stop_time, entities):
        from scenario_builder_v2 import build_stk_scenario_v2
        build_stk_scenario_v2(self._root, exercise_id, start_time, stop_time, entities)
        log.info("StkPytkService: scenario built for exercise %s", exercise_id)

    def export_czml(self, exercise_id, output_path):
        from czml_utils import (patch_czml_sensors, inject_fom_rectangle,
                                render_fom_heatmap, extract_area_target_polygon)
        from pathlib import Path

        out = Path(output_path)
        out.parent.mkdir(parents=True, exist_ok=True)

        sc = self._root.current_scenario
        self._export_czml_connect(out, sc.instance_name)
        patch_czml_sensors(out)

        coverage, fom_obj = self._find_coverage_fom(sc)
        if fom_obj:
            grid = self._read_fom_grid_v2(coverage, fom_obj, sc)
            if grid:
                img_dir  = Path("fom_images"); img_dir.mkdir(exist_ok=True)
                img_path = img_dir / f"{exercise_id}.png"
                polygon_deg = extract_area_target_polygon(out)
                render_fom_heatmap(grid, img_path, polygon_deg=polygon_deg)
                inject_fom_rectangle(
                    out,
                    f"http://localhost:8002/exercises/{exercise_id}/fom-image",
                )
                log.info("StkPytkService: FOM heatmap → %s", img_path)
            else:
                log.warning("StkPytkService: FOM DataProviders returned no data for %s",
                            exercise_id)
        else:
            log.info("StkPytkService: no FigureOfMerit — skipping heatmap")

    def _export_czml_connect(self, output_path, scenario_name):
        abs_path = str(output_path.resolve()).replace('\\', '/')
        cmd = (f'ExportCZML */Scenario/{scenario_name} "{abs_path}" '
               f'http://localhost:8002/models/')
        try:
            self._root.execute_command(cmd)
        except Exception as exc:
            raise RuntimeError(
                f"ExportCZML failed — ensure CZML Export plugin is installed in STK: {exc}"
            )
        if not output_path.exists() or output_path.stat().st_size == 0:
            raise RuntimeError("ExportCZML produced no output file")
        log.info("StkPytkService: CZML exported → %s (%.1f KB)",
                 output_path, output_path.stat().st_size / 1024)

    def _find_coverage_fom(self, scenario):
        for child in scenario.children:
            if child.class_name.lower() != 'coveragedefinition':
                continue
            for sub in child.children:
                if sub.class_name.lower() == 'figureofmerit':
                    return child, sub
        return None, None

    def _read_fom_grid_v2(self, coverage, fom_obj, scenario) -> list:
        start = scenario.start_time
        stop  = scenario.stop_time

        def _get_col(result, *candidates):
            for name in candidates:
                try:
                    return list(result.data_sets.get_data_set_by_name(name).get_values())
                except Exception:
                    pass
            return []

        def _try_dp(obj, dp_name, val_cols, exec_args=()):
            try:
                try:
                    dp = obj.data_providers[dp_name]
                except (TypeError, KeyError, AttributeError):
                    dp = obj.data_providers.get_item_by_name(dp_name)
                result = dp.exec(*exec_args)
                lats = _get_col(result, 'Latitude', 'Lat', 'lat')
                lons = _get_col(result, 'Longitude', 'Lon', 'lon')
                if not lats:
                    return None
                for col in val_cols:
                    vals = _get_col(result, col)
                    if vals:
                        log.info("_read_fom_grid_v2: %d pts via '%s'/'%s'",
                                 len(lats), dp_name, col)
                        return list(zip(lats, lons, vals))
            except Exception as exc:
                log.warning("_read_fom_grid_v2: '%s' raised: %s", dp_name, exc)
            return None

        val_fom = ('FOM Value', 'Value', 'Satisfaction')
        val_cov = ('Percent Coverage', 'Coverage', 'Value')

        for dp in ('Value By Point', 'Static Satisfaction'):
            r = _try_dp(fom_obj, dp, val_fom, exec_args=())
            if r:
                return r

        r = _try_dp(coverage, 'Percent Coverage', val_cov,
                    exec_args=(start, stop, 3600.0))
        if r:
            return r

        return []

    def shutdown(self):
        try:
            self._engine.stop_application()
            log.info("StkPytkService: Engine stopped")
        except Exception as exc:
            log.warning("StkPytkService: shutdown error: %s", exc)
```

- [ ] **Step 2: Run full test suite to confirm nothing broke**

```bash
pytest tests/ -v
```

Expected: all tests PASS (StkPytkService is only exercised when PySTK is available).

- [ ] **Step 3: Commit**

```bash
git add mvp2/sg-service-v2/stk_pystk_service.py
git commit -m "feat(mvp2): complete StkPytkService with FOM pipeline"
```

---

> **Backend checkpoint:** At this point `sg-service-v2` is fully functional with MockStkService. Start the service and confirm manually before proceeding to the frontend:
> ```bash
> cd mvp2/sg-service-v2
> python -m uvicorn main:app --port 8002 --log-level info
> curl -X POST http://localhost:8002/exercises
> # Note the exercise_id, then:
> curl -X POST http://localhost:8002/exercises/{eid}/compute \
>   -H "Content-Type: application/json" \
>   -d '{"exerciseId":"{eid}","scenarioTime":{"start":"1 Jan 2025 00:00:00.000","stop":"1 Jan 2025 01:00:00.000"},"entities":[]}'
> curl http://localhost:8002/exercises/{eid}/status
> curl http://localhost:8002/exercises/{eid}/czml
> ```

---

## Task 8: Angular frontend scaffold

**Files:**
- Create: `mvp2/frontend/` (new Angular app)

- [ ] **Step 1: Scaffold the app**

```bash
cd mvp2
npx @angular/cli@latest new frontend --routing=false --style=scss --standalone
cd frontend
npm install cesium
```

- [ ] **Step 2: Configure angular.json to copy Cesium assets**

In `mvp2/frontend/angular.json`, under `projects.frontend.architect.build.options`, add:

```json
"assets": [
  "src/favicon.ico",
  "src/assets",
  { "glob": "**/*", "input": "node_modules/cesium/Build/Cesium", "output": "cesium" }
],
"scripts": [
  "node_modules/cesium/Build/Cesium/Cesium.js"
]
```

- [ ] **Step 3: Set CESIUM_BASE_URL in index.html**

In `mvp2/frontend/src/index.html`, add before `</head>`:

```html
<script>window.CESIUM_BASE_URL = '/cesium/';</script>
```

- [ ] **Step 4: Verify the app builds**

```bash
cd mvp2/frontend
npm start
```

Expected: app compiles and opens on `http://localhost:4200`. A blank Angular page is fine.

- [ ] **Step 5: Commit**

```bash
git add mvp2/frontend/
git commit -m "feat(mvp2): scaffold Angular frontend with CesiumJS"
```

---

## Task 9: TypeScript types + DrawingStateService

**Files:**
- Create: `mvp2/frontend/src/app/types.ts`
- Create: `mvp2/frontend/src/app/drawing-state.service.ts`
- Create: `mvp2/frontend/src/app/drawing-state.service.spec.ts`

- [ ] **Step 1: Write types.ts**

`mvp2/frontend/src/app/types.ts`:
```typescript
export type EntityType = 'AreaTarget' | 'Aircraft' | 'Facility' | 'Sensor';
export type DrawingMode = 'none' | EntityType;

export interface PlanningEntity {
  type: 'Feature';
  geometry:
    | { type: 'Polygon';    coordinates: [number, number][][]; }
    | { type: 'LineString'; coordinates: [number, number, number][]; }
    | { type: 'Point';      coordinates: [number, number, number]; };
  properties: {
    entityType:    EntityType;
    name:          string;
    speedMs?:      number;
    halfAngleDeg?: number;
    parentEntity?: string;
  };
}

export interface ScenarioTime {
  start: string;   // "1 Jan 2025 00:00:00.000"
  stop:  string;
}

export interface PlanningResult {
  exerciseId:   string;
  scenarioTime: ScenarioTime;
  entities:     PlanningEntity[];
}
```

- [ ] **Step 2: Write DrawingStateService**

`mvp2/frontend/src/app/drawing-state.service.ts`:
```typescript
import { Injectable, signal } from '@angular/core';
import { DrawingMode, PlanningEntity } from './types';

@Injectable({ providedIn: 'root' })
export class DrawingStateService {
  readonly mode    = signal<DrawingMode>('none');
  readonly entities = signal<PlanningEntity[]>([]);

  // Coordinates being collected for the current in-progress entity
  readonly inProgressCoords = signal<[number, number, number][]>([]);

  setMode(mode: DrawingMode): void {
    if (this.mode() === mode) {
      this.mode.set('none');
      this.inProgressCoords.set([]);
    } else {
      this.mode.set(mode);
      this.inProgressCoords.set([]);
    }
  }

  addCoord(lon: number, lat: number, altM: number = 0): void {
    this.inProgressCoords.update(c => [...c, [lon, lat, altM]]);
  }

  commitEntity(entity: PlanningEntity): void {
    this.entities.update(es => [...es, entity]);
    this.mode.set('none');
    this.inProgressCoords.set([]);
  }

  deleteEntity(name: string): void {
    this.entities.update(es => es.filter(e => e.properties.name !== name));
  }

  clearAll(): void {
    this.entities.set([]);
    this.mode.set('none');
    this.inProgressCoords.set([]);
  }
}
```

- [ ] **Step 3: Write service tests**

`mvp2/frontend/src/app/drawing-state.service.spec.ts`:
```typescript
import { TestBed } from '@angular/core/testing';
import { DrawingStateService } from './drawing-state.service';

describe('DrawingStateService', () => {
  let svc: DrawingStateService;
  beforeEach(() => { TestBed.configureTestingModule({}); svc = TestBed.inject(DrawingStateService); });

  it('starts in none mode', () => expect(svc.mode()).toBe('none'));

  it('sets mode on setMode', () => { svc.setMode('Aircraft'); expect(svc.mode()).toBe('Aircraft'); });

  it('toggles back to none when same mode set again', () => {
    svc.setMode('Aircraft'); svc.setMode('Aircraft');
    expect(svc.mode()).toBe('none');
  });

  it('accumulates coords', () => {
    svc.addCoord(74.0, 34.0); svc.addCoord(75.0, 34.0);
    expect(svc.inProgressCoords().length).toBe(2);
  });

  it('commitEntity appends entity and resets mode', () => {
    svc.setMode('Facility');
    svc.commitEntity({ type: 'Feature',
      geometry: { type: 'Point', coordinates: [74.0, 34.0, 0] },
      properties: { entityType: 'Facility', name: 'Site1' } });
    expect(svc.entities().length).toBe(1);
    expect(svc.mode()).toBe('none');
  });

  it('deleteEntity removes by name', () => {
    svc.commitEntity({ type: 'Feature',
      geometry: { type: 'Point', coordinates: [74.0, 34.0, 0] },
      properties: { entityType: 'Facility', name: 'Site1' } });
    svc.deleteEntity('Site1');
    expect(svc.entities().length).toBe(0);
  });
});
```

- [ ] **Step 4: Run tests**

```bash
cd mvp2/frontend
npx ng test --watch=false --browsers=ChromeHeadless
```

Expected: all `DrawingStateService` tests PASS.

- [ ] **Step 5: Commit**

```bash
git add mvp2/frontend/src/app/types.ts mvp2/frontend/src/app/drawing-state.service.ts mvp2/frontend/src/app/drawing-state.service.spec.ts
git commit -m "feat(mvp2): add PlanningEntity types and DrawingStateService"
```

---

## Task 10: CesiumViewerComponent

**Files:**
- Create: `mvp2/frontend/src/app/cesium-viewer/cesium-viewer.component.ts`
- Create: `mvp2/frontend/src/app/cesium-viewer/cesium-viewer.component.html`
- Create: `mvp2/frontend/src/app/cesium-viewer/cesium-viewer.component.scss`

- [ ] **Step 1: Create component files**

`mvp2/frontend/src/app/cesium-viewer/cesium-viewer.component.html`:
```html
<div id="cesiumContainer" style="width:100%;height:100%;"></div>
```

`mvp2/frontend/src/app/cesium-viewer/cesium-viewer.component.scss`:
```scss
:host { display: block; width: 100%; height: 100%; }
```

`mvp2/frontend/src/app/cesium-viewer/cesium-viewer.component.ts`:
```typescript
import {
  Component, OnInit, OnDestroy, Input, OnChanges, SimpleChanges
} from '@angular/core';
import { DrawingStateService } from '../drawing-state.service';
import { DrawingMode, PlanningEntity } from '../types';
import * as Cesium from 'cesium';

@Component({
  selector: 'app-cesium-viewer',
  standalone: true,
  templateUrl: './cesium-viewer.component.html',
  styleUrls: ['./cesium-viewer.component.scss'],
})
export class CesiumViewerComponent implements OnInit, OnDestroy, OnChanges {
  @Input() czmlUrl: string | null = null;

  private viewer!: Cesium.Viewer;
  private handler: Cesium.ScreenSpaceEventHandler | null = null;
  private tempPolyline: Cesium.Entity | null = null;

  constructor(private drawState: DrawingStateService) {}

  ngOnInit(): void {
    this.viewer = new Cesium.Viewer('cesiumContainer', {
      animation: true, timeline: true, baseLayerPicker: false,
      geocoder: false, homeButton: false, sceneModePicker: true,
      terrainProvider: new Cesium.EllipsoidTerrainProvider(),
    });
    this.viewer.camera.flyTo({
      destination: Cesium.Cartesian3.fromDegrees(74.5, 34.2, 500_000),
    });

    // Re-attach handler whenever mode changes
    (this.drawState.mode as any).subscribe
      ? null  // Angular signal — use effect instead
      : null;

    // Poll mode change via interval (simple approach for signals)
    let lastMode: DrawingMode = 'none';
    setInterval(() => {
      const m = this.drawState.mode();
      if (m !== lastMode) { lastMode = m; this._attachHandler(m); }
    }, 100);
  }

  ngOnChanges(changes: SimpleChanges): void {
    if (changes['czmlUrl'] && this.czmlUrl && this.viewer) {
      this._loadCzml(this.czmlUrl);
    }
  }

  ngOnDestroy(): void {
    this.handler?.destroy();
    this.viewer?.destroy();
  }

  private _attachHandler(mode: DrawingMode): void {
    this.handler?.destroy();
    this.handler = null;
    if (this.tempPolyline) {
      this.viewer.entities.remove(this.tempPolyline);
      this.tempPolyline = null;
    }
    if (mode === 'none') return;

    this.handler = new Cesium.ScreenSpaceEventHandler(this.viewer.scene.canvas);

    if (mode === 'Facility') {
      this.handler.setInputAction((e: Cesium.ScreenSpaceEventHandler.PositionedEvent) => {
        const pos = this._screenToLonLat(e.position);
        if (!pos) return;
        const name = `Facility_${Date.now()}`;
        this.drawState.commitEntity({
          type: 'Feature',
          geometry: { type: 'Point', coordinates: [pos.lon, pos.lat, 0] },
          properties: { entityType: 'Facility', name },
        });
        this.viewer.entities.add({ name, position: Cesium.Cartesian3.fromDegrees(pos.lon, pos.lat),
          point: { pixelSize: 12, color: Cesium.Color.YELLOW } });
      }, Cesium.ScreenSpaceEventType.LEFT_CLICK);
      return;
    }

    if (mode === 'Aircraft' || mode === 'AreaTarget') {
      this.handler.setInputAction((e: Cesium.ScreenSpaceEventHandler.PositionedEvent) => {
        const pos = this._screenToLonLat(e.position);
        if (!pos) return;
        this.drawState.addCoord(pos.lon, pos.lat, 0);
        this._refreshTempPolyline();
      }, Cesium.ScreenSpaceEventType.LEFT_CLICK);

      this.handler.setInputAction((_e: Cesium.ScreenSpaceEventHandler.PositionedEvent) => {
        const coords = this.drawState.inProgressCoords();
        if (coords.length < 2) return;
        const name = `${mode}_${Date.now()}`;
        if (mode === 'Aircraft') {
          this.drawState.commitEntity({
            type: 'Feature',
            geometry: { type: 'LineString', coordinates: coords },
            properties: { entityType: 'Aircraft', name, speedMs: 150 },
          });
          this.viewer.entities.add({ name,
            polyline: { positions: Cesium.Cartesian3.fromDegreesArrayHeights(coords.flat()),
                        width: 3, material: new Cesium.PolylineDashMaterialProperty({
                          color: Cesium.Color.CYAN }) } });
        } else {
          // Close polygon: repeat first vertex
          const ring = [...coords, coords[0]];
          this.drawState.commitEntity({
            type: 'Feature',
            geometry: { type: 'Polygon', coordinates: [ring] },
            properties: { entityType: 'AreaTarget', name },
          });
          this.viewer.entities.add({ name,
            polygon: { hierarchy: Cesium.Cartesian3.fromDegreesArray(coords.flatMap(c => [c[0], c[1]])),
                       material: Cesium.Color.ORANGE.withAlpha(0.3),
                       outline: true, outlineColor: Cesium.Color.ORANGE } });
        }
      }, Cesium.ScreenSpaceEventType.LEFT_DOUBLE_CLICK);
    }
  }

  private _refreshTempPolyline(): void {
    const coords = this.drawState.inProgressCoords();
    if (this.tempPolyline) this.viewer.entities.remove(this.tempPolyline);
    if (coords.length < 2) return;
    this.tempPolyline = this.viewer.entities.add({
      polyline: {
        positions: Cesium.Cartesian3.fromDegreesArray(coords.flatMap(c => [c[0], c[1]])),
        width: 2, material: Cesium.Color.WHITE.withAlpha(0.6),
      },
    });
  }

  private _screenToLonLat(pos: Cesium.Cartesian2): { lon: number; lat: number } | null {
    const cart = this.viewer.camera.pickEllipsoid(pos, this.viewer.scene.globe.ellipsoid);
    if (!cart) return null;
    const carto = Cesium.Cartographic.fromCartesian(cart);
    return { lon: Cesium.Math.toDegrees(carto.longitude),
             lat: Cesium.Math.toDegrees(carto.latitude) };
  }

  private _loadCzml(url: string): void {
    const source = new Cesium.CzmlDataSource();
    source.load(url).then(ds => {
      this.viewer.dataSources.add(ds);
      this.viewer.zoomTo(ds);
    });
  }
}
```

- [ ] **Step 2: Verify it compiles**

```bash
cd mvp2/frontend
npx ng build --configuration development 2>&1 | tail -20
```

Expected: no TypeScript errors. (Cesium type errors about `Viewer` are acceptable if `cesium` types are installed.)

If Cesium types are missing:
```bash
npm install --save-dev @types/cesium
```

- [ ] **Step 3: Commit**

```bash
git add mvp2/frontend/src/app/cesium-viewer/
git commit -m "feat(mvp2): add CesiumViewerComponent with drawing mode handlers"
```

---

## Task 11: DrawingToolbarComponent + EntitySidebarComponent

**Files:**
- Create: `mvp2/frontend/src/app/drawing-toolbar/drawing-toolbar.component.ts`
- Create: `mvp2/frontend/src/app/drawing-toolbar/drawing-toolbar.component.html`
- Create: `mvp2/frontend/src/app/entity-sidebar/entity-sidebar.component.ts`
- Create: `mvp2/frontend/src/app/entity-sidebar/entity-sidebar.component.html`
- Create: `mvp2/frontend/src/app/entity-sidebar/entity-sidebar.component.scss`

- [ ] **Step 1: Write DrawingToolbarComponent**

`mvp2/frontend/src/app/drawing-toolbar/drawing-toolbar.component.html`:
```html
<div class="toolbar">
  <button [class.active]="mode() === 'AreaTarget'" (click)="setMode('AreaTarget')">
    ⬡ Area Target
  </button>
  <button [class.active]="mode() === 'Aircraft'" (click)="setMode('Aircraft')">
    ✈ Aircraft Route
  </button>
  <button [class.active]="mode() === 'Facility'" (click)="setMode('Facility')">
    📍 Facility
  </button>
  <button [class.active]="mode() === 'Sensor'" (click)="setMode('Sensor')">
    📡 Sensor
  </button>
  <span class="hint" *ngIf="mode() !== 'none'">
    <ng-container *ngIf="mode() === 'Facility'">Click globe to place</ng-container>
    <ng-container *ngIf="mode() === 'Aircraft'">Click waypoints, double-click to finish</ng-container>
    <ng-container *ngIf="mode() === 'AreaTarget'">Click vertices, double-click to close</ng-container>
    <ng-container *ngIf="mode() === 'Sensor'">Fill form below</ng-container>
  </span>
</div>
```

`mvp2/frontend/src/app/drawing-toolbar/drawing-toolbar.component.ts`:
```typescript
import { Component } from '@angular/core';
import { CommonModule } from '@angular/common';
import { DrawingStateService } from '../drawing-state.service';
import { DrawingMode } from '../types';

@Component({
  selector: 'app-drawing-toolbar',
  standalone: true,
  imports: [CommonModule],
  templateUrl: './drawing-toolbar.component.html',
  styles: [`.toolbar{display:flex;gap:8px;padding:8px;background:#1a1a2e;}
            button{padding:6px 12px;cursor:pointer;border:1px solid #444;background:#2a2a4e;color:#fff;}
            button.active{background:#4a4aff;border-color:#8888ff;}
            .hint{color:#aaa;font-size:12px;align-self:center;}`]
})
export class DrawingToolbarComponent {
  constructor(private drawState: DrawingStateService) {}
  mode = this.drawState.mode;
  setMode(m: DrawingMode): void { this.drawState.setMode(m); }
}
```

- [ ] **Step 2: Write EntitySidebarComponent**

`mvp2/frontend/src/app/entity-sidebar/entity-sidebar.component.html`:
```html
<div class="sidebar">
  <h3>Entities ({{ entities().length }})</h3>
  <ul>
    <li *ngFor="let e of entities()">
      <span class="type">{{ e.properties.entityType }}</span>
      <span class="name">{{ e.properties.name }}</span>
      <button (click)="delete(e.properties.name)">✕</button>
    </li>
  </ul>

  <div class="sensor-form" *ngIf="drawState.mode() === 'Sensor'">
    <h4>Add Sensor</h4>
    <label>Name <input [(ngModel)]="sensorName" placeholder="Snsr1"/></label>
    <label>Parent <input [(ngModel)]="sensorParent" placeholder="Aircraft name"/></label>
    <label>Half-angle (°) <input type="number" [(ngModel)]="sensorHalfAngle" min="1" max="90"/></label>
    <button (click)="addSensor()">Add Sensor</button>
  </div>
</div>
```

`mvp2/frontend/src/app/entity-sidebar/entity-sidebar.component.ts`:
```typescript
import { Component } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { DrawingStateService } from '../drawing-state.service';

@Component({
  selector: 'app-entity-sidebar',
  standalone: true,
  imports: [CommonModule, FormsModule],
  templateUrl: './entity-sidebar.component.html',
  styleUrls: ['./entity-sidebar.component.scss'],
})
export class EntitySidebarComponent {
  sensorName     = '';
  sensorParent   = '';
  sensorHalfAngle = 25;

  constructor(public drawState: DrawingStateService) {}

  entities = this.drawState.entities;

  delete(name: string): void { this.drawState.deleteEntity(name); }

  addSensor(): void {
    if (!this.sensorName || !this.sensorParent) return;
    const parent = this.drawState.entities()
      .find(e => e.properties.name === this.sensorParent);
    const coords = parent
      ? (parent.geometry.type === 'LineString'
          ? parent.geometry.coordinates[0] as [number,number,number]
          : parent.geometry.coordinates as [number,number,number])
      : [0, 0, 0] as [number,number,number];
    this.drawState.commitEntity({
      type: 'Feature',
      geometry: { type: 'Point', coordinates: coords },
      properties: {
        entityType: 'Sensor', name: this.sensorName,
        parentEntity: this.sensorParent, halfAngleDeg: this.sensorHalfAngle,
      },
    });
    this.sensorName = ''; this.sensorParent = '';
  }
}
```

`mvp2/frontend/src/app/entity-sidebar/entity-sidebar.component.scss`:
```scss
.sidebar { padding: 12px; background: #1a1a2e; color: #fff; min-width: 220px; }
h3, h4 { margin: 0 0 8px; }
ul { list-style: none; padding: 0; margin: 0 0 12px; }
li { display: flex; gap: 6px; align-items: center; margin-bottom: 4px; font-size: 13px; }
.type { color: #aaa; font-size: 11px; min-width: 70px; }
.name { flex: 1; }
button { background: transparent; border: none; color: #f55; cursor: pointer; }
.sensor-form { border-top: 1px solid #444; padding-top: 10px; }
label { display: block; margin-bottom: 6px; font-size: 12px; }
input { width: 100%; margin-top: 2px; padding: 3px; background: #2a2a4e; border: 1px solid #555; color: #fff; }
.sensor-form button { background: #4a4aff; color: #fff; padding: 5px 10px; border: none; cursor: pointer; margin-top: 6px; }
```

- [ ] **Step 3: Compile check**

```bash
npx ng build --configuration development 2>&1 | tail -10
```

Expected: no errors.

- [ ] **Step 4: Commit**

```bash
git add mvp2/frontend/src/app/drawing-toolbar/ mvp2/frontend/src/app/entity-sidebar/
git commit -m "feat(mvp2): add DrawingToolbarComponent and EntitySidebarComponent"
```

---

## Task 12: ScenarioPlannerComponent + AppComponent wiring

**Files:**
- Create: `mvp2/frontend/src/app/scenario-planner/scenario-planner.component.ts`
- Create: `mvp2/frontend/src/app/scenario-planner/scenario-planner.component.html`
- Modify: `mvp2/frontend/src/app/app.component.ts`
- Modify: `mvp2/frontend/src/app/app.component.html`
- Modify: `mvp2/frontend/src/app/app.config.ts`

- [ ] **Step 1: Write ScenarioPlannerComponent**

`mvp2/frontend/src/app/scenario-planner/scenario-planner.component.html`:
```html
<div class="planner">
  <div class="times">
    <label>Start <input [(ngModel)]="startTime"/></label>
    <label>Stop  <input [(ngModel)]="stopTime"/></label>
  </div>
  <button [disabled]="running" (click)="submit()">
    {{ running ? 'Computing…' : 'Run Scenario' }}
  </button>
  <button (click)="clear()">Clear All</button>
  <div class="log">
    <div *ngFor="let line of log">{{ line }}</div>
  </div>
</div>
```

`mvp2/frontend/src/app/scenario-planner/scenario-planner.component.ts`:
```typescript
import { Component, Output, EventEmitter } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { HttpClient } from '@angular/common/http';
import { DrawingStateService } from '../drawing-state.service';

const API = 'http://localhost:8002';

@Component({
  selector: 'app-scenario-planner',
  standalone: true,
  imports: [CommonModule, FormsModule],
  templateUrl: './scenario-planner.component.html',
  styles: [`.planner{padding:12px;background:#111;color:#fff;font-size:13px;}
            .times{display:flex;gap:8px;margin-bottom:8px;}
            label{display:flex;flex-direction:column;font-size:11px;}
            input{background:#222;border:1px solid #555;color:#fff;padding:3px;}
            button{margin:4px 4px 0 0;padding:5px 12px;background:#4a4aff;color:#fff;border:none;cursor:pointer;}
            button:disabled{opacity:.5;}
            .log{margin-top:8px;font-size:11px;font-family:monospace;max-height:120px;overflow:auto;}`]
})
export class ScenarioPlannerComponent {
  @Output() czmlReady = new EventEmitter<string>();

  startTime = '1 Jan 2025 00:00:00.000';
  stopTime  = '1 Jan 2025 01:00:00.000';
  running   = false;
  log: string[] = [];

  constructor(private http: HttpClient, private drawState: DrawingStateService) {}

  submit(): void {
    this.running = true;
    this.log = [];
    const entities = this.drawState.entities();

    // Step 1 — create exercise
    this.http.post<{exercise_id: string}>(`${API}/exercises`, {}).subscribe({
      next: ({ exercise_id }) => {
        this._log(`Exercise created: ${exercise_id}`);
        const plan = {
          exerciseId:   exercise_id,
          scenarioTime: { start: this.startTime, stop: this.stopTime },
          entities,
        };

        // Step 2 — trigger compute
        this.http.post<{status: string}>(`${API}/exercises/${exercise_id}/compute`, plan)
          .subscribe({
            next: () => {
              this._log('Computation started — polling…');
              this._poll(exercise_id);
            },
            error: e => this._fail(`compute failed: ${e.message}`),
          });
      },
      error: e => this._fail(`create failed: ${e.message}`),
    });
  }

  private _poll(eid: string, attempts = 0): void {
    if (attempts > 120) { this._fail('timed out after 120s'); return; }
    setTimeout(() => {
      this.http.get<{status: string}>(`${API}/exercises/${eid}/status`).subscribe({
        next: ({ status }) => {
          this._log(`Status: ${status}`);
          if (status === 'ready') {
            this.running = false;
            this.czmlReady.emit(`${API}/exercises/${eid}/czml`);
          } else if (status.startsWith('error')) {
            this._fail(status);
          } else {
            this._poll(eid, attempts + 1);
          }
        },
        error: e => this._fail(e.message),
      });
    }, 1000);
  }

  clear(): void { this.drawState.clearAll(); this.log = []; }

  private _log(msg: string): void { this.log = [...this.log, msg]; }
  private _fail(msg: string): void {
    this.running = false;
    this._log(`ERROR: ${msg}`);
  }
}
```

- [ ] **Step 2: Wire into AppComponent**

`mvp2/frontend/src/app/app.component.ts`:
```typescript
import { Component, signal } from '@angular/core';
import { DrawingToolbarComponent } from './drawing-toolbar/drawing-toolbar.component';
import { EntitySidebarComponent }  from './entity-sidebar/entity-sidebar.component';
import { ScenarioPlannerComponent } from './scenario-planner/scenario-planner.component';
import { CesiumViewerComponent }   from './cesium-viewer/cesium-viewer.component';

@Component({
  selector: 'app-root',
  standalone: true,
  imports: [DrawingToolbarComponent, EntitySidebarComponent,
            ScenarioPlannerComponent, CesiumViewerComponent],
  template: `
    <div class="shell">
      <div class="top-bar">
        <app-drawing-toolbar/>
        <app-scenario-planner (czmlReady)="czmlUrl.set($event)"/>
      </div>
      <div class="main">
        <app-entity-sidebar/>
        <app-cesium-viewer class="globe" [czmlUrl]="czmlUrl()"/>
      </div>
    </div>`,
  styles: [`
    .shell   { display:flex; flex-direction:column; height:100vh; background:#0d0d1a; }
    .top-bar { display:flex; gap:0; border-bottom:1px solid #333; }
    .main    { display:flex; flex:1; overflow:hidden; }
    .globe   { flex:1; }
  `]
})
export class AppComponent {
  czmlUrl = signal<string | null>(null);
}
```

- [ ] **Step 3: Add HttpClient to app.config.ts**

`mvp2/frontend/src/app/app.config.ts`:
```typescript
import { ApplicationConfig } from '@angular/core';
import { provideHttpClient } from '@angular/common/http';

export const appConfig: ApplicationConfig = {
  providers: [provideHttpClient()]
};
```

- [ ] **Step 4: Build and verify**

```bash
npx ng build --configuration development 2>&1 | tail -10
```

Expected: no TypeScript errors.

- [ ] **Step 5: Commit**

```bash
git add mvp2/frontend/src/app/scenario-planner/ mvp2/frontend/src/app/app.component.ts mvp2/frontend/src/app/app.config.ts
git commit -m "feat(mvp2): add ScenarioPlannerComponent and wire full app layout"
```

---

## Task 13: End-to-end manual validation

- [ ] **Step 1: Start the backend**

```bash
cd mvp2/sg-service-v2
python -m uvicorn main:app --port 8002 --log-level info
```

Expected: `using MockStkService`

- [ ] **Step 2: Start the frontend**

```bash
cd mvp2/frontend
npm start
```

Open `http://localhost:4200`.

- [ ] **Step 3: Draw entities**

1. Click **⬡ Area Target** — click 4 points on the globe forming a rough square over Kashmir (74°E, 34°N region), double-click to close
2. Click **✈ Aircraft Route** — click 2 waypoints inside the area, double-click to finish
3. Click **📍 Facility** — click a point inside the area
4. In the EntitySidebar Sensor form: name=`Snsr1`, parent=the Aircraft name, half-angle=25, click **Add Sensor**

Expected: sidebar shows 4 entities.

- [ ] **Step 4: Submit scenario**

Click **Run Scenario**. Watch the log panel:

```
Exercise created: <uuid>
Computation started — polling…
Status: running
Status: ready
```

Expected: globe loads CZML with AreaTarget polygon (orange), Aircraft path (cyan), Facility (yellow dot). Timeline scrubber active.

- [ ] **Step 5: Run full backend test suite one final time**

```bash
cd mvp2/sg-service-v2
pytest tests/ -v
```

Expected: all tests PASS.

- [ ] **Step 6: Final commit**

```bash
git add .
git commit -m "feat(mvp2): complete draw-first MVP2 frontend + verified end-to-end with MockStkService"
```

---

## Self-Review

### Spec coverage

| Spec section | Task |
|---|---|
| §1 Architecture — CesiumJS draw-first, port 8002, PySTK exclusively | Tasks 4, 7, 8 |
| §1 RFB rejected (rationale documented) | Design doc only — no code needed |
| §2 DrawingStateService, modes, ScreenSpaceEventHandler per mode | Tasks 9, 10 |
| §2 No edit handles | Tasks 10, 11 — not implemented, correct |
| §3 GeoJSON Feature data model, PlanningResult body | Tasks 4, 9 |
| §3 Coordinate conventions table | Tasks 4, 6 (ECEF conversion, knots, km) |
| §4 StkPytkService startup/shutdown | Tasks 4, 7 |
| §4 AreaTarget Connect command gap documented | Task 6 |
| §4 Aircraft knots conversion | Task 6 (tested), Task 7 |
| §4 Facility altitude metres | Task 6 (tested) |
| §4 Sensor form-only, parent lookup | Tasks 6, 11 |
| §4 Coverage + FOM auto-created | Task 6 (tested) |
| §4 Runtime validation items (enum, speed unit, DataProvider) | Documented in code comments; validated at STK integration |
| §5 CZML export via Connect command | Task 7 |
| §5 czml_utils pure-Python copy | Task 2 (tested) |
| §5 FOM discovery, DataProvider v2 reading | Task 7 |
| §5 Polling flow unchanged, max_workers=1 | Task 4 |
| License constraints — single Engine, not simultaneous with MVP | Tasks 4, 7 comments; README update needed |
| Sensor drawing — form only | Task 11 (form in sidebar) |

### README update needed (not in tasks above)

After Task 7, add a `mvp2/README.md` with:
- License warning: do not run `mvp/sg-service` (port 8001) and `mvp2/sg-service-v2` (port 8002) simultaneously
- `pip install -r requirements.txt` and `python -m uvicorn main:app --port 8002`
- `npm install && npm start` in `mvp2/frontend`
- PySTK runtime validation checklist from §4.8 of design spec
