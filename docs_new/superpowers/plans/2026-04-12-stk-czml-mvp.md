# STK + CZML + CesiumJS MVP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Validate the full STK → Python COM → CZML export → CesiumJS load-and-render pipeline in a minimal, self-contained proof-of-concept before committing to full production implementation.

**Architecture:** Minimal FastAPI service (`mvp/sg-service/`) exposes three HTTP endpoints — create exercise, trigger computation, serve CZML. A `get_stk_service()` factory selects `StkComService` when `agi.stk12` is importable, falling back to `MockStkService` automatically. A standalone Angular app (`mvp/frontend/`) loads the CZML URL into a CesiumJS viewer. No Kafka, no TimescaleDB, no RBAC, no Electron — only the integration path under test.

**Tech Stack:** Python 3.10+, FastAPI 0.111, pytest 8, httpx (async test client), `agi.stk12` COM (Windows, STK 12 must be running), Angular 17, CesiumJS 1.140, TypeScript 5, Pillow, numpy, scipy.

**Out of scope for MVP:** Martin tile server (flat ellipsoid used instead — terrain does not affect CZML load validation), Kafka, TimescaleDB, RBAC/JWT, Electron packaging, drs-bridge.

---

## Post-Plan Additions (implemented after initial tasks)

The following capabilities were added after the original 9-task plan was completed:

### CZML Sensor Cone Patch
`StkComService.export_czml()` now calls `_patch_czml_sensors()` after `ExportCZML`. This replaces every `agi_conicSensor` extension packet (ignored by stock CesiumJS) with a native `cylinder` entity:
- Nadir-pointing orientation computed via per-sample ENU quaternion from ECEF position
- Cylinder center offset downward by `length/2` so `topRadius=0` (apex) sits exactly at the aircraft
- Label packet injected for each sensor

### FOM Heatmap Auto-Generation
After CZML export, `_auto_export_fom_image()` runs automatically when the scenario contains a `CoverageDefinition` with a `FigureOfMerit` child:
1. **`_read_fom_grid`** — reads per-point FOM values via STK DataProviders. `'Value By Point'` on `AgFigureOfMerit` is called with no time arguments (static DP); `'Percent Coverage'` on `AgCoverageDefinition` uses `Exec(start, stop, stepTime)`.
2. **`_render_fom_heatmap`** — uses `scipy.interpolate.griddata` (linear) to interpolate scattered points onto a 512×512 grid. Image extent matches the AreaTarget polygon bounding box so the PNG aligns with the CZML rectangle. The AreaTarget polygon is applied as a hard alpha mask (sub-pixel antialiasing only).
3. **`inject_fom_rectangle`** — parses the AreaTarget `cartographicRadians` polygon from the CZML, derives WSEN bounds in radians, and injects a `rectangle` entity with an image material pointing to `GET /exercises/{id}/fom-image`.

### STK 3-D Model Serving
`GET /models/{filename}` recursively searches `STKData/VO/Models/` for the requested `.glb` filename and serves it. Required because `ExportCZML` writes bare filenames without subdirectory paths.

### Additional HTTP Endpoints
| Endpoint | Purpose |
|---|---|
| `POST /exercises/from-scenario-file` | Upload `.sc`/`.vdf`, compute, return exercise ID |
| `POST /exercises/{id}/fom-image` | Upload FOM PNG manually with auto-derived bounds |
| `GET /exercises/{id}/fom-image` | Serve stored FOM PNG |
| `GET /models/{filename}` | Serve STK `.glb` model files |

### Graceful STK Shutdown
`main.py` uses a FastAPI `lifespan` context to call `StkComService.shutdown()` before the Python interpreter exits, preventing an access-violation crash from STK's own `atexit` handler firing after `sys.meta_path` is `None`.

### Python Package Requirements (updated)
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
```

---

## File Map

```
mvp/
  sg-service/
    main.py                  ← FastAPI app + CORS + exercise state dict
    stk_service.py           ← IStkService abstract base class
    stk_com_service.py       ← StkComService (agi.stk12 COM)
    stk_mock_service.py      ← MockStkService (synthetic CZML, no STK needed)
    czml_builder.py          ← build_synthetic_czml() — standalone CZML generator
    scenario_builder.py      ← build_stk_scenario() — STK COM object setup
    computation_job.py       ← run_computation() called in ProcessPoolExecutor
    requirements.txt
    tests/
      conftest.py
      test_czml_builder.py
      test_mock_service.py
      test_stk_factory.py
      test_endpoints.py
  frontend/
    src/
      main.ts                ← CESIUM_BASE_URL set before all imports
      app/
        app.component.ts
        app.component.html
        cesium-viewer/
          cesium-viewer.component.ts
          cesium-viewer.component.html
        scenario-loader/
          scenario-loader.component.ts
          scenario-loader.component.html
        app.module.ts
    angular.json
    package.json
    tsconfig.json
```

---

## Task 1: sg-service scaffold

**Files:**
- Create: `mvp/sg-service/requirements.txt`
- Create: `mvp/sg-service/main.py`
- Create: `mvp/sg-service/tests/conftest.py`

- [ ] **Step 1: Create the requirements file**

```
# mvp/sg-service/requirements.txt
fastapi==0.111.0
uvicorn[standard]==0.30.1
httpx==0.27.0
pytest==8.2.2
pytest-asyncio==0.23.7
anyio==4.4.0
```

- [ ] **Step 2: Create the FastAPI app with health endpoint**

```python
# mvp/sg-service/main.py
from __future__ import annotations

import asyncio
import uuid
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path
from typing import Any

from fastapi import FastAPI, BackgroundTasks, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse
from pydantic import BaseModel

app = FastAPI(title="EWTSS MVP sg-service")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["http://localhost:4200", "http://127.0.0.1:4200"],
    allow_methods=["*"],
    allow_headers=["*"],
)

# In-memory store: exercise_id → {"status": str, "czml_path": str | None}
_exercises: dict[str, dict[str, Any]] = {}

CZML_DIR = Path("czml_output")
CZML_DIR.mkdir(exist_ok=True)

_pool = ProcessPoolExecutor(max_workers=2)


class ExerciseIn(BaseModel):
    name: str
    start_time: str = "1 Jan 2025 00:00:00.000"
    stop_time:  str = "1 Jan 2025 01:00:00.000"


@app.get("/health")
def health() -> dict[str, str]:
    return {"status": "ok"}


@app.post("/exercises", status_code=201)
def create_exercise(body: ExerciseIn) -> dict[str, str]:
    eid = str(uuid.uuid4())
    _exercises[eid] = {
        "name":       body.name,
        "start_time": body.start_time,
        "stop_time":  body.stop_time,
        "status":     "created",
        "czml_path":  None,
    }
    return {"exercise_id": eid, "status": "created"}


@app.post("/exercises/{exercise_id}/compute", status_code=202)
async def trigger_compute(exercise_id: str, background_tasks: BackgroundTasks) -> dict[str, str]:
    if exercise_id not in _exercises:
        raise HTTPException(status_code=404, detail="exercise not found")
    if _exercises[exercise_id]["status"] in ("running", "ready"):
        return {"exercise_id": exercise_id, "status": _exercises[exercise_id]["status"]}
    _exercises[exercise_id]["status"] = "running"
    background_tasks.add_task(_run_compute_background, exercise_id)
    return {"exercise_id": exercise_id, "status": "running"}


async def _run_compute_background(exercise_id: str) -> None:
    from computation_job import run_computation
    ex = _exercises[exercise_id]
    output_path = str(CZML_DIR / f"{exercise_id}.czml")
    loop = asyncio.get_event_loop()
    try:
        await loop.run_in_executor(
            _pool,
            run_computation,
            exercise_id,
            ex["start_time"],
            ex["stop_time"],
            output_path,
        )
        _exercises[exercise_id]["status"]    = "ready"
        _exercises[exercise_id]["czml_path"] = output_path
    except Exception as exc:
        _exercises[exercise_id]["status"] = f"error: {exc}"


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
    return FileResponse(
        ex["czml_path"],
        media_type="application/json",
        headers={"Content-Disposition": f'inline; filename="{exercise_id}.czml"'},
    )
```

- [ ] **Step 3: Create the pytest conftest**

```python
# mvp/sg-service/tests/conftest.py
import sys
from pathlib import Path

# Make the sg-service package importable from tests/
sys.path.insert(0, str(Path(__file__).parent.parent))
```

- [ ] **Step 4: Install dependencies and run health check**

```bash
cd mvp/sg-service
pip install -r requirements.txt
uvicorn main:app --reload --port 8001
# In another terminal:
curl http://localhost:8001/health
# Expected: {"status":"ok"}
```

- [ ] **Step 5: Commit**

```bash
git add mvp/sg-service/main.py mvp/sg-service/requirements.txt mvp/sg-service/tests/conftest.py
git commit -m "mvp: sg-service skeleton with health, exercise CRUD, and compute trigger"
```

---

## Task 2: IStkService interface + CZML builder

**Files:**
- Create: `mvp/sg-service/stk_service.py`
- Create: `mvp/sg-service/czml_builder.py`
- Create: `mvp/sg-service/tests/test_czml_builder.py`

- [ ] **Step 1: Write the failing test**

```python
# mvp/sg-service/tests/test_czml_builder.py
import json
import pytest
from czml_builder import build_synthetic_czml


def test_czml_document_packet():
    packets = build_synthetic_czml("ex-001", "1 Jan 2025 00:00:00.000", "1 Jan 2025 01:00:00.000")
    assert isinstance(packets, list)
    doc = packets[0]
    assert doc["id"] == "document"
    assert doc["version"] == "1.0"
    assert "clock" in doc


def test_czml_has_entity_packet():
    packets = build_synthetic_czml("ex-001", "1 Jan 2025 00:00:00.000", "1 Jan 2025 01:00:00.000")
    entity_ids = [p["id"] for p in packets if p["id"] != "document"]
    assert len(entity_ids) >= 1


def test_czml_entity_has_position():
    packets = build_synthetic_czml("ex-001", "1 Jan 2025 00:00:00.000", "1 Jan 2025 01:00:00.000")
    entity = next(p for p in packets if p["id"] != "document")
    assert "position" in entity
    pos = entity["position"]
    assert "cartographicDegrees" in pos
    # Must be [time, lon, lat, alt, time, lon, lat, alt, ...]
    coords = pos["cartographicDegrees"]
    assert len(coords) % 4 == 0
    assert len(coords) >= 8   # at least two sample points


def test_czml_is_json_serialisable():
    packets = build_synthetic_czml("ex-001", "1 Jan 2025 00:00:00.000", "1 Jan 2025 01:00:00.000")
    text = json.dumps(packets)
    reloaded = json.loads(text)
    assert reloaded[0]["id"] == "document"
```

- [ ] **Step 2: Run tests to confirm they fail**

```bash
cd mvp/sg-service
pytest tests/test_czml_builder.py -v
# Expected: ModuleNotFoundError: No module named 'czml_builder'
```

- [ ] **Step 3: Implement the IStkService interface**

```python
# mvp/sg-service/stk_service.py
from __future__ import annotations
from abc import ABC, abstractmethod


class IStkService(ABC):
    """Contract that both StkComService and MockStkService fulfil."""

    @abstractmethod
    def build_and_compute(
        self,
        exercise_id: str,
        start_time: str,
        stop_time: str,
    ) -> None:
        """Build scenario in STK and run all link/coverage computations."""

    @abstractmethod
    def export_czml(self, exercise_id: str, output_path: str) -> None:
        """Export the computed scenario to a CZML file at output_path."""
```

- [ ] **Step 4: Implement czml_builder.py**

```python
# mvp/sg-service/czml_builder.py
"""
Generates a minimal valid CZML document for a moving platform.
Used by MockStkService and as a reference for the CesiumJS loader tests.
"""
from __future__ import annotations

import math
from datetime import datetime, timedelta, timezone


def _stk_time_to_iso(stk_time: str) -> str:
    """Convert STK time string '1 Jan 2025 00:00:00.000' to ISO-8601 UTC."""
    dt = datetime.strptime(stk_time.strip(), "%d %b %Y %H:%M:%S.%f")
    return dt.replace(tzinfo=timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def build_synthetic_czml(
    exercise_id: str,
    start_time: str,
    stop_time: str,
) -> list[dict]:
    """
    Build a synthetic CZML document with one moving platform entity.

    The platform traces a slow circular ground track at 5 km altitude,
    centred on (35°E, 33°N) — Middle-East AoI representative.

    cartographicDegrees format: [t0, lon0, lat0, alt0, t1, lon1, lat1, alt1, ...]
    where t is seconds since epoch.
    """
    start_iso = _stk_time_to_iso(start_time)
    stop_iso  = _stk_time_to_iso(stop_time)

    start_dt = datetime.strptime(start_iso, "%Y-%m-%dT%H:%M:%SZ")
    stop_dt  = datetime.strptime(stop_iso,  "%Y-%m-%dT%H:%M:%SZ")
    duration = int((stop_dt - start_dt).total_seconds())

    STEP = 60   # one sample per minute
    CENTER_LON, CENTER_LAT, RADIUS_DEG, ALT_M = 35.0, 33.0, 0.8, 5000.0

    cartographic: list[float] = []
    t = 0
    while t <= duration:
        angle = math.radians(t * 360.0 / duration)
        lon = CENTER_LON + RADIUS_DEG * math.cos(angle)
        lat = CENTER_LAT + RADIUS_DEG * math.sin(angle)
        cartographic.extend([float(t), lon, lat, ALT_M])
        t += STEP

    return [
        {
            "id": "document",
            "name": f"Exercise {exercise_id}",
            "version": "1.0",
            "clock": {
                "interval":    f"{start_iso}/{stop_iso}",
                "currentTime": start_iso,
                "multiplier":  60,
                "step":        "SYSTEM_CLOCK_MULTIPLIER",
                "range":       "LOOP_STOP",
            },
        },
        {
            "id":           f"platform/{exercise_id}",
            "name":         "Platform",
            "availability": f"{start_iso}/{stop_iso}",
            "position": {
                "epoch":                 start_iso,
                "interpolationAlgorithm": "LAGRANGE",
                "interpolationDegree":   5,
                "cartographicDegrees":   cartographic,
            },
            "path": {
                "leadTime":  300,
                "trailTime": 300,
                "width":     2,
                "material": {
                    "solidColor": {"color": {"rgba": [255, 200, 0, 200]}}
                },
            },
            "point": {
                "color":    {"rgba": [255, 200, 0, 255]},
                "pixelSize": 10,
                "outlineColor": {"rgba": [0, 0, 0, 255]},
                "outlineWidth": 1,
            },
            "label": {
                "text":           "Platform",
                "font":           "12pt sans-serif",
                "fillColor":      {"rgba": [255, 255, 255, 255]},
                "outlineColor":   {"rgba": [0, 0, 0, 255]},
                "outlineWidth":   2,
                "style":          "FILL_AND_OUTLINE",
                "verticalOrigin": "BOTTOM",
                "pixelOffset":    {"cartesian2": [0, -12]},
            },
        },
    ]
```

- [ ] **Step 5: Run tests — all should pass**

```bash
cd mvp/sg-service
pytest tests/test_czml_builder.py -v
# Expected: 4 passed
```

- [ ] **Step 6: Commit**

```bash
git add mvp/sg-service/stk_service.py mvp/sg-service/czml_builder.py mvp/sg-service/tests/test_czml_builder.py
git commit -m "mvp: IStkService interface + czml_builder synthetic CZML generator"
```

---

## Task 3: MockStkService

**Files:**
- Create: `mvp/sg-service/stk_mock_service.py`
- Create: `mvp/sg-service/tests/test_mock_service.py`

- [ ] **Step 1: Write the failing test**

```python
# mvp/sg-service/tests/test_mock_service.py
import json
import tempfile
from pathlib import Path

import pytest

from stk_mock_service import MockStkService
from stk_service import IStkService


def test_mock_is_istk_service():
    assert issubclass(MockStkService, IStkService)


def test_mock_build_and_compute_succeeds():
    svc = MockStkService()
    # Should not raise
    svc.build_and_compute(
        "ex-001",
        "1 Jan 2025 00:00:00.000",
        "1 Jan 2025 01:00:00.000",
    )


def test_mock_export_czml_writes_valid_file():
    svc = MockStkService()
    svc.build_and_compute(
        "ex-002",
        "1 Jan 2025 00:00:00.000",
        "1 Jan 2025 01:00:00.000",
    )
    with tempfile.NamedTemporaryFile(suffix=".czml", delete=False) as f:
        output_path = f.name

    svc.export_czml("ex-002", output_path)

    content = Path(output_path).read_text(encoding="utf-8")
    packets = json.loads(content)
    assert isinstance(packets, list)
    assert packets[0]["id"] == "document"


def test_mock_export_czml_contains_correct_exercise_id():
    svc = MockStkService()
    svc.build_and_compute(
        "ex-003",
        "1 Jan 2025 00:00:00.000",
        "1 Jan 2025 01:00:00.000",
    )
    with tempfile.NamedTemporaryFile(suffix=".czml", delete=False, mode="w") as f:
        output_path = f.name

    svc.export_czml("ex-003", output_path)
    packets = json.loads(Path(output_path).read_text(encoding="utf-8"))
    entity_ids = [p["id"] for p in packets if p["id"] != "document"]
    assert any("ex-003" in eid for eid in entity_ids)
```

- [ ] **Step 2: Run tests to confirm they fail**

```bash
cd mvp/sg-service
pytest tests/test_mock_service.py -v
# Expected: ModuleNotFoundError: No module named 'stk_mock_service'
```

- [ ] **Step 3: Implement MockStkService**

```python
# mvp/sg-service/stk_mock_service.py
from __future__ import annotations

import json
from pathlib import Path

from czml_builder import build_synthetic_czml
from stk_service import IStkService


class MockStkService(IStkService):
    """
    STK-free fallback service.
    Stores exercise parameters in memory; export_czml() writes synthetic CZML.
    Used when agi.stk12 is not installed, or during CI/test runs.
    """

    def __init__(self) -> None:
        self._exercises: dict[str, dict[str, str]] = {}

    def build_and_compute(
        self,
        exercise_id: str,
        start_time: str,
        stop_time: str,
    ) -> None:
        """Record exercise parameters — computation is deferred to export_czml."""
        self._exercises[exercise_id] = {
            "start_time": start_time,
            "stop_time":  stop_time,
        }

    def export_czml(self, exercise_id: str, output_path: str) -> None:
        """Write synthetic CZML to output_path using czml_builder."""
        ex = self._exercises.get(exercise_id)
        if ex is None:
            raise ValueError(
                f"exercise {exercise_id!r} not found — call build_and_compute first"
            )
        packets = build_synthetic_czml(
            exercise_id,
            ex["start_time"],
            ex["stop_time"],
        )
        Path(output_path).write_text(
            json.dumps(packets, indent=2),
            encoding="utf-8",
        )
```

- [ ] **Step 4: Run tests — all should pass**

```bash
cd mvp/sg-service
pytest tests/test_mock_service.py -v
# Expected: 4 passed
```

- [ ] **Step 5: Commit**

```bash
git add mvp/sg-service/stk_mock_service.py mvp/sg-service/tests/test_mock_service.py
git commit -m "mvp: MockStkService — synthetic CZML, no STK dependency"
```

---

## Task 4: StkComService + get_stk_service() factory

**Files:**
- Create: `mvp/sg-service/stk_com_service.py`
- Create: `mvp/sg-service/tests/test_stk_factory.py`

- [ ] **Step 1: Write the failing tests**

```python
# mvp/sg-service/tests/test_stk_factory.py
import sys
import importlib
import pytest

from stk_mock_service import MockStkService


def test_factory_returns_mock_when_agi_not_installed(monkeypatch):
    """Ensure get_stk_service() falls back to MockStkService if agi.stk12 is absent."""
    # Simulate agi.stk12 being unimportable
    monkeypatch.setitem(sys.modules, "agi", None)
    monkeypatch.setitem(sys.modules, "agi.stk12", None)

    import stk_com_service
    importlib.reload(stk_com_service)

    svc = stk_com_service.get_stk_service()
    assert isinstance(svc, MockStkService)


def test_factory_returns_mock_on_com_connection_failure(monkeypatch):
    """If STK COM raises on attach, factory must still return MockStkService."""
    import types

    fake_agi = types.ModuleType("agi")
    fake_stk12 = types.ModuleType("agi.stk12")

    class FakeDesktop:
        @staticmethod
        def AttachToApplication():
            raise OSError("STK is not running")

    fake_stk12.stkdesktop = types.SimpleNamespace(STKDesktop=FakeDesktop)
    fake_agi.stk12 = fake_stk12

    monkeypatch.setitem(sys.modules, "agi", fake_agi)
    monkeypatch.setitem(sys.modules, "agi.stk12", fake_stk12)
    monkeypatch.setitem(sys.modules, "agi.stk12.stkdesktop", fake_stk12.stkdesktop)

    import stk_com_service
    importlib.reload(stk_com_service)

    svc = stk_com_service.get_stk_service()
    assert isinstance(svc, MockStkService)
```

- [ ] **Step 2: Run to confirm failure**

```bash
cd mvp/sg-service
pytest tests/test_stk_factory.py -v
# Expected: ModuleNotFoundError: No module named 'stk_com_service'
```

- [ ] **Step 3: Implement StkComService + factory**

```python
# mvp/sg-service/stk_com_service.py
from __future__ import annotations

import logging
from pathlib import Path

from stk_service import IStkService

log = logging.getLogger(__name__)


class StkComService(IStkService):
    """
    Live STK integration via agi.stk12 COM automation.
    STK 12 must already be running (or this service will start it).
    Call get_stk_service() instead of instantiating directly.
    """

    def __init__(self) -> None:
        from agi.stk12.stkdesktop import STKDesktop  # type: ignore[import]
        from agi.stk12.stkobjects import AgESTKObjectType  # type: ignore[import]

        self._AgESTKObjectType = AgESTKObjectType
        try:
            self._stk = STKDesktop.AttachToApplication()
            log.info("StkComService: attached to running STK instance")
        except Exception:
            self._stk = STKDesktop.StartApplication(visible=True)
            log.info("StkComService: started new STK instance")

        self._root = self._stk.Root

    @property
    def root(self):  # noqa: ANN201
        return self._root

    def build_and_compute(
        self,
        exercise_id: str,
        start_time: str,
        stop_time: str,
    ) -> None:
        """
        Create an STK scenario, add entities, and run access/coverage computation.
        Delegates entity/sensor setup to scenario_builder.
        """
        from scenario_builder import build_stk_scenario

        build_stk_scenario(
            root=self._root,
            exercise_id=exercise_id,
            start_time=start_time,
            stop_time=stop_time,
        )
        log.info("StkComService: computation complete for exercise %s", exercise_id)

    def export_czml(self, exercise_id: str, output_path: str) -> None:
        """
        Export the current STK scenario to a CZML file.
        Uses STK's built-in ExportDataTo Connect command.
        """
        # STK Connect command: ExportDataTo / "<path>" CZML
        # The leading '/' refers to the scenario root object.
        abs_path = str(Path(output_path).resolve())
        result = self._root.ExecuteCommand(f'ExportDataTo / "{abs_path}" CZML')
        log.info("StkComService: CZML exported to %s (result: %s)", abs_path, result)


def get_stk_service() -> IStkService:
    """
    Return StkComService when agi.stk12 is available and STK responds;
    fall back silently to MockStkService otherwise.
    This is the ONLY place that decides which service is used.
    """
    from stk_mock_service import MockStkService

    try:
        import agi.stk12  # noqa: F401
        svc = StkComService()
        log.info("get_stk_service: using StkComService (live STK)")
        return svc
    except Exception as exc:
        log.warning(
            "get_stk_service: STK unavailable (%s), using MockStkService", exc
        )
        return MockStkService()
```

- [ ] **Step 4: Run tests — all should pass**

```bash
cd mvp/sg-service
pytest tests/test_stk_factory.py -v
# Expected: 2 passed
```

- [ ] **Step 5: Commit**

```bash
git add mvp/sg-service/stk_com_service.py mvp/sg-service/tests/test_stk_factory.py
git commit -m "mvp: StkComService + get_stk_service() factory with MockStkService fallback"
```

---

## Task 5: scenario_builder.py

**Files:**
- Create: `mvp/sg-service/scenario_builder.py`

The scenario builder runs inside `StkComService.build_and_compute()`. It creates the minimum STK scenario needed to demonstrate a CZML export: one ground facility, one aircraft on a simple great-circle track, and one conical sensor on the aircraft. Because it calls the real STK COM API, it has no automated test — validation is done by the end-to-end checklist in Task 9.

- [ ] **Step 1: Implement scenario_builder.py**

```python
# mvp/sg-service/scenario_builder.py
"""
Builds a minimal STK scenario via agi.stk12 COM.
Called exclusively by StkComService.build_and_compute().

Scenario content:
  - GroundFacility: "BaseStation" at (33.0N, 35.0E, 200m)
  - Aircraft:       "Platform"   — straight-line path from (33.0N, 33.0E, 5000m)
                                    to (33.0N, 37.0E, 5000m) over the exercise window
  - Sensor:         "Radar" on Platform — HalfAngle 15°, conical pattern

After build_stk_scenario() returns, the caller calls export_czml() which uses
STK's ExportDataTo to capture entity trajectories + access intervals + sensor
cone geometry into the CZML file.
"""
from __future__ import annotations

import logging

log = logging.getLogger(__name__)


def build_stk_scenario(
    root,
    exercise_id: str,
    start_time: str,
    stop_time: str,
) -> None:
    """
    Create or replace an STK scenario and populate it with the MVP entities.

    Args:
        root:        agi.stk12 IAgStkObjectRoot — from StkComService._root
        exercise_id: used as the scenario name
        start_time:  STK-format string, e.g. "1 Jan 2025 00:00:00.000"
        stop_time:   STK-format string, e.g. "1 Jan 2025 01:00:00.000"
    """
    from agi.stk12.stkobjects import (  # type: ignore[import]
        AgESTKObjectType,
        AgEVePropagatorType,
    )

    scenario_name = f"MVP_{exercise_id.replace('-', '_')}"
    log.info("scenario_builder: creating scenario %s", scenario_name)

    root.NewScenario(scenario_name)
    sc = root.CurrentScenario
    sc.StartTime = start_time
    sc.StopTime  = stop_time

    # ── Ground Facility ───────────────────────────────────────────────
    facility = sc.Children.New(AgESTKObjectType.eFacility, "BaseStation")
    facility.Position.AssignGeodetic(33.0, 35.0, 0.2)  # lat, lon, alt(km)
    log.info("scenario_builder: added BaseStation facility")

    # ── Aircraft on a straight path ───────────────────────────────────
    aircraft = sc.Children.New(AgESTKObjectType.eAircraft, "Platform")
    route = aircraft.Route
    route.SetPropagatorType(AgEVePropagatorType.ePropagatorGreatArc)

    propagator = route.Propagator
    propagator.EphemerisInterval.SetExplicitInterval(start_time, stop_time)

    # Two waypoints: west → east at constant altitude 5 km
    wp_start = propagator.Waypoints.Add()
    wp_start.Latitude  =  33.0
    wp_start.Longitude =  33.0
    wp_start.Altitude  =   5.0   # km
    wp_start.Speed     = 150.0   # m/s

    wp_stop = propagator.Waypoints.Add()
    wp_stop.Latitude  =  33.0
    wp_stop.Longitude =  37.0
    wp_stop.Altitude  =   5.0
    wp_stop.Speed     = 150.0

    propagator.Propagate()
    log.info("scenario_builder: added Platform aircraft")

    # ── Conical Sensor on the aircraft ────────────────────────────────
    sensor = aircraft.Children.New(AgESTKObjectType.eSensor, "Radar")
    sensor.SetPatternType(1)   # 1 = eSensorPatternSimpleConic
    sensor.CommonTasks.SetPatternSimpleConic(15.0, 1)   # 15° half-angle, 1 = body-fixed
    log.info("scenario_builder: added Radar sensor")

    # ── Access computation: Sensor → BaseStation ──────────────────────
    access = sensor.GetAccessToObject(facility)
    access.ComputeAccess()
    log.info("scenario_builder: access computation complete")
```

- [ ] **Step 2: Commit**

```bash
git add mvp/sg-service/scenario_builder.py
git commit -m "mvp: scenario_builder — STK COM scenario with facility, aircraft, sensor, access"
```

---

## Task 6: computation_job.py + endpoint tests

**Files:**
- Create: `mvp/sg-service/computation_job.py`
- Create: `mvp/sg-service/tests/test_endpoints.py`

- [ ] **Step 1: Implement computation_job.py**

This function runs inside `ProcessPoolExecutor` — it must be a plain function (not async) and must not reference the main process's `_exercises` dict.

```python
# mvp/sg-service/computation_job.py
"""
Runs inside ProcessPoolExecutor.
Must import its own IStkService — cannot share state with the main process.
"""
from __future__ import annotations

import logging

log = logging.getLogger(__name__)


def run_computation(
    exercise_id: str,
    start_time: str,
    stop_time: str,
    output_path: str,
) -> None:
    """
    Called by main.py._run_compute_background via run_in_executor.
    Selects the appropriate STK service, runs computation, writes CZML.

    Raises on any error — the caller sets exercise status to "error: <msg>".
    """
    from stk_com_service import get_stk_service

    log.info("computation_job: starting exercise %s", exercise_id)
    svc = get_stk_service()
    svc.build_and_compute(exercise_id, start_time, stop_time)
    svc.export_czml(exercise_id, output_path)
    log.info("computation_job: done, CZML at %s", output_path)
```

- [ ] **Step 2: Write the failing endpoint tests**

```python
# mvp/sg-service/tests/test_endpoints.py
"""
Integration tests for the FastAPI endpoints.
All tests run with MockStkService (agi.stk12 is not required).
"""
import asyncio
import json
import sys
import time
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

# Force MockStkService by making agi.stk12 unimportable before main imports it
sys.modules.setdefault("agi", None)          # type: ignore[assignment]
sys.modules.setdefault("agi.stk12", None)    # type: ignore[assignment]

from main import app  # noqa: E402 — must be after sys.modules patching

client = TestClient(app)


def test_health():
    r = client.get("/health")
    assert r.status_code == 200
    assert r.json() == {"status": "ok"}


def test_create_exercise():
    r = client.post("/exercises", json={"name": "Test Exercise"})
    assert r.status_code == 201
    body = r.json()
    assert "exercise_id" in body
    assert body["status"] == "created"


def test_create_and_compute_status_flow():
    # Create
    r = client.post("/exercises", json={"name": "Compute Test"})
    eid = r.json()["exercise_id"]

    # Trigger compute
    r = client.post(f"/exercises/{eid}/compute")
    assert r.status_code == 202
    assert r.json()["status"] in ("running", "ready")

    # Poll until ready (MockStkService is fast — should finish within 3s)
    deadline = time.time() + 10
    while time.time() < deadline:
        r = client.get(f"/exercises/{eid}/status")
        if r.json()["status"] == "ready":
            break
        time.sleep(0.1)

    assert r.json()["status"] == "ready", f"timed out, status={r.json()['status']}"


def test_czml_endpoint_returns_valid_json():
    # Create + compute
    r = client.post("/exercises", json={"name": "CZML Test"})
    eid = r.json()["exercise_id"]
    client.post(f"/exercises/{eid}/compute")

    # Wait for ready
    deadline = time.time() + 10
    while time.time() < deadline:
        if client.get(f"/exercises/{eid}/status").json()["status"] == "ready":
            break
        time.sleep(0.1)

    # Fetch CZML
    r = client.get(f"/exercises/{eid}/czml")
    assert r.status_code == 200
    packets = r.json()
    assert isinstance(packets, list)
    assert packets[0]["id"] == "document"


def test_czml_endpoint_returns_409_before_compute():
    r = client.post("/exercises", json={"name": "Not Yet Computed"})
    eid = r.json()["exercise_id"]
    r = client.get(f"/exercises/{eid}/czml")
    assert r.status_code == 409


def test_status_404_for_unknown_exercise():
    r = client.get("/exercises/does-not-exist/status")
    assert r.status_code == 404
```

- [ ] **Step 3: Run tests to confirm failure**

```bash
cd mvp/sg-service
pytest tests/test_endpoints.py -v
# Expected: ModuleNotFoundError: No module named 'computation_job'
```

- [ ] **Step 4: Run tests — all should pass**

```bash
cd mvp/sg-service
pytest tests/test_endpoints.py -v
# Expected: 6 passed
```

- [ ] **Step 5: Run the full test suite**

```bash
cd mvp/sg-service
pytest tests/ -v
# Expected: all tests pass (test_stk_factory.py may have 1 skip on non-Windows)
```

- [ ] **Step 6: Commit**

```bash
git add mvp/sg-service/computation_job.py mvp/sg-service/tests/test_endpoints.py
git commit -m "mvp: computation_job.py + endpoint integration tests (MockStkService)"
```

---

## Task 7: Angular frontend scaffold + CesiumJS build setup

**Files:**
- Create: `mvp/frontend/` (Angular workspace)
- Modify: `mvp/frontend/angular.json`
- Modify: `mvp/frontend/src/main.ts`
- Create: `mvp/frontend/src/app/app.module.ts`
- Create: `mvp/frontend/src/app/app.component.ts`
- Create: `mvp/frontend/src/app/app.component.html`

- [ ] **Step 1: Scaffold the Angular app**

```bash
cd mvp
npx @angular/cli@17 new frontend \
  --routing=false \
  --style=scss \
  --standalone=false \
  --skip-git
cd frontend
npm install cesium@1.116.0
```

- [ ] **Step 2: Configure angular.json for CesiumJS assets**

Open `mvp/frontend/angular.json`. Find the `"assets"` array under `projects.frontend.architect.build.options` and replace it with:

```json
"assets": [
  "src/favicon.ico",
  "src/assets",
  {
    "glob": "**",
    "input": "node_modules/cesium/Build/Cesium",
    "output": "cesium"
  }
],
```

In the same `options` block, add `allowedCommonJsDependencies` (inside `"architect.build.options"`):

```json
"allowedCommonJsDependencies": ["cesium"]
```

- [ ] **Step 3: Set CESIUM_BASE_URL as the first statement in main.ts**

Replace `mvp/frontend/src/main.ts` entirely with:

```typescript
// CESIUM_BASE_URL MUST be set before any Cesium import.
// If this assignment comes after a Cesium import, WebWorkers silently fail.
(window as any).CESIUM_BASE_URL = '/cesium';

import { platformBrowserDynamic } from '@angular/platform-browser-dynamic';
import { AppModule } from './app/app.module';

platformBrowserDynamic()
  .bootstrapModule(AppModule)
  .catch(err => console.error(err));
```

- [ ] **Step 4: Replace AppComponent with a minimal layout**

```typescript
// mvp/frontend/src/app/app.component.ts
import { Component } from '@angular/core';

@Component({
  selector: 'app-root',
  templateUrl: './app.component.html',
  styleUrls: ['./app.component.scss'],
})
export class AppComponent {}
```

```html
<!-- mvp/frontend/src/app/app.component.html -->
<div style="display:flex; height:100vh; overflow:hidden;">
  <app-scenario-loader style="width:300px; flex-shrink:0; padding:16px; background:#161b22; color:#e6edf3;"></app-scenario-loader>
  <app-cesium-viewer style="flex:1;"></app-cesium-viewer>
</div>
```

- [ ] **Step 5: Verify the app builds without errors**

```bash
cd mvp/frontend
ng build
# Expected: Build at dist/frontend — no errors.
# Warnings about commonjs dependencies are acceptable (cesium is in allowedCommonJsDependencies).
```

- [ ] **Step 6: Commit**

```bash
git add mvp/frontend/
git commit -m "mvp: Angular frontend scaffold + CesiumJS 1.116 build config (CESIUM_BASE_URL, asset copy)"
```

---

## Task 8: CesiumViewerComponent (NgZone isolation, flat ellipsoid)

**Files:**
- Create: `mvp/frontend/src/app/cesium-viewer/cesium-viewer.component.ts`
- Create: `mvp/frontend/src/app/cesium-viewer/cesium-viewer.component.html`
- Modify: `mvp/frontend/src/app/app.module.ts`

The MVP uses a flat `EllipsoidTerrainProvider` (no Martin tile server required). This is intentional — it isolates the CZML load validation from the terrain pipeline. The component exposes a `loadCzml(url: string)` method called by `ScenarioLoaderComponent`.

- [ ] **Step 1: Implement CesiumViewerComponent**

```typescript
// mvp/frontend/src/app/cesium-viewer/cesium-viewer.component.ts
import {
  AfterViewInit,
  ChangeDetectionStrategy,
  Component,
  NgZone,
  OnDestroy,
} from '@angular/core';
import {
  Viewer,
  EllipsoidTerrainProvider,
  CzmlDataSource,
  JulianDate,
} from 'cesium';

@Component({
  selector: 'app-cesium-viewer',
  templateUrl: './cesium-viewer.component.html',
  changeDetection: ChangeDetectionStrategy.OnPush,
})
export class CesiumViewerComponent implements AfterViewInit, OnDestroy {
  private viewer!: Viewer;

  constructor(private ngZone: NgZone) {}

  ngAfterViewInit(): void {
    // ALL Cesium code runs outside Angular zone to prevent 60fps CD cycles.
    this.ngZone.runOutsideAngular(() => {
      this.viewer = new Viewer('cesiumContainer', {
        animation:           true,
        timeline:            true,
        baseLayerPicker:     false,
        geocoder:            false,
        homeButton:          true,
        sceneModePicker:     false,
        navigationHelpButton: false,
        infoBox:             true,
        // Flat ellipsoid — no Ion/Bing/terrain server needed for MVP
        terrainProvider:  new EllipsoidTerrainProvider(),
        imageryProvider:  false as any,   // suppress Cesium Ion/Bing request
      });
    });
  }

  /** Load a CZML URL into the viewer, clear any previously loaded source. */
  async loadCzml(url: string): Promise<void> {
    return this.ngZone.runOutsideAngular(async () => {
      // Remove previous CZML sources
      this.viewer.dataSources.removeAll();

      const ds = await CzmlDataSource.load(url);
      await this.viewer.dataSources.add(ds);

      // Zoom to the loaded entities and start the timeline clock
      await this.viewer.zoomTo(ds);
      this.viewer.clock.shouldAnimate = true;
    });
  }

  ngOnDestroy(): void {
    this.ngZone.runOutsideAngular(() => {
      if (this.viewer && !this.viewer.isDestroyed()) {
        this.viewer.destroy();
      }
    });
  }
}
```

```html
<!-- mvp/frontend/src/app/cesium-viewer/cesium-viewer.component.html -->
<div id="cesiumContainer" style="width:100%; height:100%;"></div>
```

- [ ] **Step 2: Add global Cesium container style to styles.scss**

Add to `mvp/frontend/src/styles.scss`:

```scss
html, body { margin: 0; padding: 0; height: 100%; overflow: hidden; }

// Cesium injects its own toolbar — reset any body font-size that interferes
#cesiumContainer {
  font-size: 14px;
}
```

- [ ] **Step 3: Register the component in AppModule**

```typescript
// mvp/frontend/src/app/app.module.ts
import { NgModule } from '@angular/core';
import { BrowserModule } from '@angular/platform-browser';
import { HttpClientModule } from '@angular/common/http';
import { FormsModule } from '@angular/forms';

import { AppComponent } from './app.component';
import { CesiumViewerComponent } from './cesium-viewer/cesium-viewer.component';
import { ScenarioLoaderComponent } from './scenario-loader/scenario-loader.component';

@NgModule({
  declarations: [
    AppComponent,
    CesiumViewerComponent,
    ScenarioLoaderComponent,
  ],
  imports: [
    BrowserModule,
    HttpClientModule,
    FormsModule,
  ],
  bootstrap: [AppComponent],
})
export class AppModule {}
```

- [ ] **Step 4: Verify the app builds and dev server starts**

```bash
cd mvp/frontend
ng serve --port 4200
# Open http://localhost:4200 in a browser
# Expected: Blank Cesium globe (dark background, timeline bar at bottom,
#           home button visible). No console errors.
#           If canvas is blank with errors in console, check CESIUM_BASE_URL
#           is the FIRST line in main.ts (before all imports).
```

- [ ] **Step 5: Commit**

```bash
git add mvp/frontend/src/app/cesium-viewer/ mvp/frontend/src/app/app.module.ts mvp/frontend/src/styles.scss
git commit -m "mvp: CesiumViewerComponent with NgZone isolation + flat ellipsoid, loadCzml() API"
```

---

## Task 9: ScenarioLoaderComponent + end-to-end validation

**Files:**
- Create: `mvp/frontend/src/app/scenario-loader/scenario-loader.component.ts`
- Create: `mvp/frontend/src/app/scenario-loader/scenario-loader.component.html`

- [ ] **Step 1: Implement ScenarioLoaderComponent**

```typescript
// mvp/frontend/src/app/scenario-loader/scenario-loader.component.ts
import { Component, ViewChild } from '@angular/core';
import { HttpClient } from '@angular/common/http';
import { firstValueFrom } from 'rxjs';
import { CesiumViewerComponent } from '../cesium-viewer/cesium-viewer.component';

const SG_BASE = 'http://localhost:8001';

@Component({
  selector: 'app-scenario-loader',
  templateUrl: './scenario-loader.component.html',
})
export class ScenarioLoaderComponent {
  @ViewChild(CesiumViewerComponent) cesiumViewer!: CesiumViewerComponent;

  exerciseName = 'MVP Test';
  exerciseId   = '';
  status       = '';
  log: string[] = [];

  constructor(private http: HttpClient) {}

  private addLog(msg: string): void {
    this.log = [...this.log, `${new Date().toISOString().slice(11, 19)} ${msg}`];
  }

  async runFullFlow(): Promise<void> {
    this.log = [];
    this.status = 'creating';
    this.addLog('POST /exercises');

    try {
      // 1. Create exercise
      const created: any = await firstValueFrom(
        this.http.post(`${SG_BASE}/exercises`, { name: this.exerciseName })
      );
      this.exerciseId = created.exercise_id;
      this.addLog(`Created: ${this.exerciseId}`);

      // 2. Trigger compute
      this.status = 'computing';
      this.addLog('POST /exercises/{id}/compute');
      await firstValueFrom(
        this.http.post(`${SG_BASE}/exercises/${this.exerciseId}/compute`, {})
      );

      // 3. Poll until ready
      this.addLog('Polling status...');
      await this.pollUntilReady();

      // 4. Load CZML
      this.status = 'loading';
      const czmlUrl = `${SG_BASE}/exercises/${this.exerciseId}/czml`;
      this.addLog(`Loading CZML: ${czmlUrl}`);

      // Get the CesiumViewerComponent reference from the parent app component
      const cesium = document.querySelector('app-cesium-viewer') as any;
      if (cesium?.__ngContext__) {
        // Angular component reference via ViewChild is not available here
        // since ScenarioLoader is a sibling — use a shared service or event.
        // For MVP simplicity, dispatch a custom event:
        window.dispatchEvent(
          new CustomEvent('load-czml', { detail: { url: czmlUrl } })
        );
        this.addLog('CZML load event dispatched to viewer');
      } else {
        window.dispatchEvent(
          new CustomEvent('load-czml', { detail: { url: czmlUrl } })
        );
        this.addLog('CZML load event dispatched');
      }

      this.status = 'ready';
      this.addLog('Done — check the globe for the moving platform.');
    } catch (err: any) {
      this.status = 'error';
      this.addLog(`Error: ${err.message ?? err}`);
    }
  }

  private async pollUntilReady(): Promise<void> {
    const deadline = Date.now() + 30_000;
    while (Date.now() < deadline) {
      const s: any = await firstValueFrom(
        this.http.get(`${SG_BASE}/exercises/${this.exerciseId}/status`)
      );
      this.addLog(`Status: ${s.status}`);
      if (s.status === 'ready') return;
      if (s.status.startsWith('error')) throw new Error(s.status);
      await new Promise(r => setTimeout(r, 500));
    }
    throw new Error('Timed out waiting for computation');
  }
}
```

```html
<!-- mvp/frontend/src/app/scenario-loader/scenario-loader.component.html -->
<h3 style="margin:0 0 12px; color:#79c0ff; font-size:13px; letter-spacing:1px; text-transform:uppercase;">
  CZML MVP Loader
</h3>

<label style="font-size:12px; color:#8b949e;">Exercise name</label><br>
<input [(ngModel)]="exerciseName"
       style="width:100%; margin:4px 0 12px; padding:6px; background:#0d1117; border:1px solid #30363d; color:#e6edf3; border-radius:4px; font-size:13px;">

<button (click)="runFullFlow()"
        [disabled]="status === 'computing' || status === 'loading'"
        style="width:100%; padding:8px; background:#1f6feb; color:#fff; border:none; border-radius:6px; cursor:pointer; font-size:13px;">
  {{ status === 'computing' ? 'Computing…' : status === 'loading' ? 'Loading CZML…' : 'Run' }}
</button>

<div *ngIf="exerciseId" style="margin-top:10px; font-size:11px; color:#484f58; word-break:break-all;">
  ID: {{ exerciseId }}
</div>

<div style="margin-top:14px; font-size:11px; color:#8b949e; height:200px; overflow-y:auto; background:#0d1117; border:1px solid #21262d; border-radius:4px; padding:8px; font-family:monospace;">
  <div *ngFor="let line of log">{{ line }}</div>
</div>
```

- [ ] **Step 2: Add CommonModule to AppModule for *ngFor/*ngIf**

Add `CommonModule` to the `imports` array in `mvp/frontend/src/app/app.module.ts`:

```typescript
import { CommonModule } from '@angular/common';
// ...
imports: [
  BrowserModule,
  HttpClientModule,
  FormsModule,
  CommonModule,
],
```

- [ ] **Step 3: Wire the load-czml event in CesiumViewerComponent**

Add an event listener in `ngAfterViewInit` inside the `runOutsideAngular` block:

```typescript
// Add inside ngAfterViewInit → runOutsideAngular block, after viewer init:
window.addEventListener('load-czml', (e: Event) => {
  const url = (e as CustomEvent<{ url: string }>).detail.url;
  this.loadCzml(url).catch(err => console.error('CZML load error:', err));
});
```

- [ ] **Step 4: Build and smoke-test the frontend**

```bash
cd mvp/frontend
ng build
ng serve --port 4200
# Open http://localhost:4200
# Expected: sidebar panel with "Exercise name" input + "Run" button;
#           Cesium globe fills the right side; no console errors.
```

- [ ] **Step 5: End-to-end validation with MockStkService**

In a separate terminal, start sg-service:

```bash
cd mvp/sg-service
uvicorn main:app --port 8001 --log-level info
```

In the browser at `http://localhost:4200`:

1. Enter an exercise name and click **Run**
2. Watch the log panel — expected sequence:
   ```
   POST /exercises
   Created: <uuid>
   POST /exercises/{id}/compute
   Polling status...
   Status: running
   Status: ready
   Loading CZML: http://localhost:8001/exercises/<id>/czml
   CZML load event dispatched
   Done — check the globe for the moving platform.
   ```
3. The Cesium globe should zoom to the Middle East (33°N, 35°E) and show a yellow dot tracing a circular path. The timeline bar at the bottom should animate at 60× speed.

**If the globe stays blank after "Done":** Open DevTools → Network tab → find the `/czml` request. If it's 200 with JSON content, the issue is in the event handler. Check that the `load-czml` listener was registered before the event fired.

**If the `/czml` request returns 409:** The computation background task did not finish before the frontend polled. The 30-second polling timeout should be sufficient for MockStkService — if it consistently times out, check that `uvicorn` has no import errors on startup.

- [ ] **Step 6: End-to-end validation with real STK (when available)**

Prerequisites: STK 12 must be running on the same machine. `agi.stk12` must be installed (`pip install agi.stk12`).

Stop and restart sg-service:

```bash
uvicorn main:app --port 8001 --log-level debug
```

The startup log should show:
```
get_stk_service: using StkComService (live STK)
```
(not "MockStkService")

Run the browser flow again. Additional validation steps:

- [ ] Log shows `StkComService: attached to running STK instance` or `started new STK instance`
- [ ] Log shows `scenario_builder: computation complete for exercise <id>`
- [ ] The CZML file written to `czml_output/<id>.czml` is valid JSON with `"id": "document"` as first packet
- [ ] Open the CZML file — confirm it contains entity packets beyond the document packet (STK should export trajectories, sensor cone geometry, and access intervals)
- [ ] In the CesiumJS viewer, the Platform aircraft traces its east-bound path over 1 hour
- [ ] The Radar sensor cone is visible as a translucent wedge extending from the Platform
- [ ] The timeline bar shows a shaded access interval segment where the sensor sees the BaseStation
- [ ] Zoom to BaseStation — it renders as a point/billboard at (33°N, 35°E)

- [ ] **Step 7: Commit**

```bash
git add mvp/frontend/src/app/scenario-loader/ mvp/frontend/src/app/app.module.ts mvp/frontend/src/app/cesium-viewer/cesium-viewer.component.ts
git commit -m "mvp: ScenarioLoaderComponent + end-to-end CZML flow validated"
```

---

## Self-Review

### Spec coverage

| Spec requirement (Section 17) | Covered by |
|---|---|
| `agi.stk12` COM automation in Python | Task 4 (StkComService), Task 5 (scenario_builder) |
| `MockStkService` generates CZML for STK-unavailable dev | Tasks 2–3 |
| `get_stk_service()` factory with ImportError fallback | Task 4 |
| `ProcessPoolExecutor` isolation for STK calls | Task 6 (main.py `_pool`) |
| `ExportDataTo / "{path}" CZML` Connect command | Task 4 (StkComService.export_czml) |
| `GET /exercises/{id}/czml` endpoint | Task 6 (main.py) |
| `CzmlDataSource.load(url)` in Angular | Task 9 (CesiumViewerComponent.loadCzml) |
| `viewer.zoomTo(dataSource)` | Task 8 (loadCzml) |
| `CESIUM_BASE_URL` set before all imports in main.ts | Task 7 |
| `allowedCommonJsDependencies: ["cesium"]` in angular.json | Task 7 |
| NgZone isolation (`runOutsideAngular`) | Task 8 |
| `ChangeDetectionStrategy.OnPush` | Task 8 |
| `viewer.destroy()` in ngOnDestroy | Task 8 |
| `animation: true, timeline: true` on Viewer | Task 8 |
| Status polling (`GET /exercises/{id}/status`) | Task 6 + Task 9 |

### Not covered in MVP (intentional scope exclusions)

- Martin tile server / quantized-mesh terrain (Section 8.7) — flat ellipsoid used; separate workstream
- TimescaleDB `computed_links` (Section 4) — not needed to validate CZML pipeline
- RBAC / JWT (Section 5.4) — not needed for pipeline validation
- Kafka integration (Section 3) — not needed for pipeline validation
- Electron packaging (Section 8.5 CSP) — Angular dev server sufficient for MVP
- `sensor.SetPatternType` exact enum value — verify against installed `agi.stk12` docs; `1` is the expected value for `eSensorPatternSimpleConic` but the enum member name should be confirmed

### Type consistency check

- `IStkService` defines `build_and_compute(exercise_id, start_time, stop_time)` — both `MockStkService` and `StkComService` implement this signature ✓
- `IStkService` defines `export_czml(exercise_id, output_path)` — both implement this ✓
- `computation_job.run_computation(exercise_id, start_time, stop_time, output_path)` — matches how `main.py` calls it via `run_in_executor` ✓
- `CesiumViewerComponent.loadCzml(url: string)` — called by `ScenarioLoaderComponent` via custom event ✓
- `czml_builder.build_synthetic_czml(exercise_id, start_time, stop_time)` — called by `MockStkService.export_czml` ✓
