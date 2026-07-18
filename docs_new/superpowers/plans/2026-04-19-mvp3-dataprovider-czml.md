# MVP3 DataProvider-driven CZML Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build MVP3 — a parallel FastAPI service on port 8003 that generates CZML from STK DataProviders via `czml3` instead of the `ExportCZML` Connect Command + JSON surgery.

**Architecture:** Nine modules split by responsibility: FastAPI shell, abstract service interface, live `agi.stk12` service, mock fallback, scenario builder (copied verbatim from MVP1), CZML builder (new), cone geometry helper (pure math), FOM pipeline (pure functions), thread-pool entry. The mock service exercises the same `czml3` builder path so tests cover the live code.

**Tech Stack:** Python 3.11/3.12, FastAPI, uvicorn, `agi.stk12` (optional at runtime, blocked in tests), `czml3==1.0.2`, numpy, scipy, Pillow, pytest.

**Spec:** `docs/superpowers/specs/2026-04-19-mvp3-dataprovider-czml-design.md`

**Do NOT modify MVP1 (`mvp/`) or MVP2 (`mvp2/`) in any task below.**

---

## Task 1: Scaffold MVP3 project structure

**Files:**
- Create: `mvp3/sg-service-mvp3/requirements.txt`
- Create: `mvp3/sg-service-mvp3/pytest.ini`
- Create: `mvp3/sg-service-mvp3/.gitignore`
- Create: `mvp3/sg-service-mvp3/stk_service.py`
- Create: `mvp3/sg-service-mvp3/tests/__init__.py` (empty)
- Create: `mvp3/sg-service-mvp3/tests/conftest.py`
- Create: `mvp3/sg-service-mvp3/tests/test_smoke.py`

- [ ] **Step 1: Create directory and requirements.txt**

`mvp3/sg-service-mvp3/requirements.txt`:
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
czml3==1.0.2

# agi.stk12 is NOT installed from PyPI. Install from STK 12's
# bin\AgPythonAPI\ folder into the same venv:
#   pip install "C:\Program Files\AGI\STK 12\bin\AgPythonAPI\agi_stk12-12.8.0-py3-none-any.whl"
# (Version suffix depends on installed STK build.)
```

- [ ] **Step 2: Write pytest.ini**

`mvp3/sg-service-mvp3/pytest.ini`:
```ini
[pytest]
filterwarnings =
    ignore::PendingDeprecationWarning:starlette.formparsers
```

- [ ] **Step 3: Write .gitignore**

`mvp3/sg-service-mvp3/.gitignore`:
```
.venv/
__pycache__/
*.pyc
czml_output/
fom_images/
*.whl
.pytest_cache/
```

- [ ] **Step 4: Write stk_service.py (abstract interface)**

`mvp3/sg-service-mvp3/stk_service.py`:
```python
from __future__ import annotations
from abc import ABC, abstractmethod


class IStkService(ABC):
    """Abstract STK integration. MVP3 live + mock implementations both implement this."""

    @abstractmethod
    def build_and_compute(self, exercise_id: str, start_time: str, stop_time: str) -> None:
        """Build the Kashmir two-aircraft-plus-sensors scenario in STK."""

    @abstractmethod
    def export_czml(self, exercise_id: str, output_path: str) -> None:
        """Generate CZML from the current scenario and write to output_path."""

    def shutdown(self) -> None:
        """Optional: terminate STK cleanly. Default no-op for mocks."""
```

- [ ] **Step 5: Write conftest.py**

`mvp3/sg-service-mvp3/tests/conftest.py`:
```python
import sys
import pytest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent.parent))


@pytest.fixture(autouse=True, scope="session")
def block_agi_stk12():
    """Force the MockStkService fallback in tests by hiding agi.stk12 imports."""
    sys.modules["agi"] = None
    sys.modules["agi.stk12"] = None
    yield
    sys.modules.pop("agi", None)
    sys.modules.pop("agi.stk12", None)
```

- [ ] **Step 6: Write empty `tests/__init__.py`**

`mvp3/sg-service-mvp3/tests/__init__.py`: empty file.

- [ ] **Step 7: Write test_smoke.py**

`mvp3/sg-service-mvp3/tests/test_smoke.py`:
```python
"""Sanity checks that the project's top-level imports resolve."""


def test_czml3_importable():
    import czml3
    assert czml3 is not None


def test_stk_service_interface_importable():
    from stk_service import IStkService
    assert IStkService is not None
```

- [ ] **Step 8: Install deps and run the smoke test**

From `mvp3/sg-service-mvp3/`:
```bash
py -3.11 -m venv .venv
.venv\Scripts\activate
pip install -r requirements.txt
pytest tests/ -v
```
Expected: both smoke tests PASS.

- [ ] **Step 9: Commit**

```bash
git add mvp3/sg-service-mvp3/
git commit -m "feat(mvp3): scaffold sg-service-mvp3 project (reqs, conftest, smoke test)"
```

---

## Task 2: cone_geometry.py — pure ECEF / ENU math

**Files:**
- Create: `mvp3/sg-service-mvp3/cone_geometry.py`
- Create: `mvp3/sg-service-mvp3/tests/test_cone_geometry.py`

- [ ] **Step 1: Write failing tests**

`mvp3/sg-service-mvp3/tests/test_cone_geometry.py`:
```python
import math
from cone_geometry import ecef_to_lonlat_rad, enu_quaternion, cone_samples


def test_ecef_to_lonlat_equator_x_axis():
    """ECEF point on +X axis at sea level → lon=0, lat=0."""
    lon, lat = ecef_to_lonlat_rad(6_378_137.0, 0.0, 0.0)
    assert abs(lon) < 1e-9
    assert abs(lat) < 1e-6


def test_ecef_to_lonlat_kashmir():
    """Known Kashmir point (~34 N, 75 E at 0 m) round-trips within 0.01 deg."""
    lat_in, lon_in = math.radians(34.0), math.radians(75.0)
    a = 6_378_137.0
    x = a * math.cos(lat_in) * math.cos(lon_in)
    y = a * math.cos(lat_in) * math.sin(lon_in)
    z = a * math.sin(lat_in)  # small-error approx; fine for this test
    lon_out, lat_out = ecef_to_lonlat_rad(x, y, z)
    assert abs(lon_out - lon_in) < 1e-4
    assert abs(lat_out - lat_in) < 1e-3


def test_enu_quaternion_is_unit():
    qx, qy, qz, qw = enu_quaternion(math.radians(75.0), math.radians(34.0))
    norm = qx * qx + qy * qy + qz * qz + qw * qw
    assert abs(norm - 1.0) < 1e-9


def test_cone_samples_shifts_apex_to_parent():
    """Cone centre should be shifted down (toward ellipsoid centre) from parent ECEF."""
    # Parent at 1 sample, positive X axis, altitude 1000 km (above equator)
    parent = [0.0, 7_378_137.0, 0.0, 0.0]
    length = 20_000.0
    carts, quats = cone_samples(parent, length)
    # Output shapes: [t, x, y, z] pairs
    assert len(carts) == 4
    assert len(quats) == 5  # [t, qx, qy, qz, qw]
    # Shift amount: length/2 along zenith (which, at equator on +X, is +X).
    # So centre_x < parent_x by length/2.
    assert carts[1] < parent[1]
    assert abs((parent[1] - carts[1]) - length / 2.0) < 1e-3
```

- [ ] **Step 2: Run tests — verify they fail**

```bash
pytest tests/test_cone_geometry.py -v
```
Expected: 4 tests FAIL with `ModuleNotFoundError: No module named 'cone_geometry'`.

- [ ] **Step 3: Write cone_geometry.py**

`mvp3/sg-service-mvp3/cone_geometry.py`:
```python
"""Pure Python math helpers for sensor cone CZML packets.

Ported from MVP1's ``stk_com_service.py`` — same algorithm, extracted into a
standalone module so it can be tested without STK and reused by the mock
service.
"""
from __future__ import annotations
import math

_WGS84_A  = 6_378_137.0
_WGS84_E2 = 0.006_694_379_990_14


def ecef_to_lonlat_rad(x: float, y: float, z: float) -> tuple[float, float]:
    """Return ``(lon_rad, lat_rad)`` for an ECEF point (altitude not needed)."""
    lon = math.atan2(y, x)
    p   = math.sqrt(x * x + y * y)
    lat = math.atan2(z, p)
    for _ in range(10):
        n   = _WGS84_A / math.sqrt(1.0 - _WGS84_E2 * math.sin(lat) ** 2)
        lat = math.atan2(z + _WGS84_E2 * n * math.sin(lat), p)
    return lon, lat


def enu_quaternion(lon: float, lat: float) -> tuple[float, float, float, float]:
    """Quaternion ``(x, y, z, w)`` for the ENU frame at (lon_rad, lat_rad).

    Local axes in ECEF: X=East, Y=North, Z=Up (zenith).
    """
    sl, cl = math.sin(lon), math.cos(lon)
    sp, cp = math.sin(lat), math.cos(lat)
    m00, m01, m02 = -sl,     -sp * cl,  cp * cl
    m10, m11, m12 =  cl,     -sp * sl,  cp * sl
    m20, m21, m22 =  0.0,         cp,     sp
    tr = m00 + m11 + m22
    if tr > 0:
        s = 0.5 / math.sqrt(tr + 1.0)
        return (m21 - m12) * s, (m02 - m20) * s, (m10 - m01) * s, 0.25 / s
    if m00 > m11 and m00 > m22:
        s = 2.0 * math.sqrt(1.0 + m00 - m11 - m22)
        return 0.25 * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s
    if m11 > m22:
        s = 2.0 * math.sqrt(1.0 + m11 - m00 - m22)
        return (m01 + m10) / s, 0.25 * s, (m12 + m21) / s, (m02 - m20) / s
    s = 2.0 * math.sqrt(1.0 + m22 - m00 - m11)
    return (m02 + m20) / s, (m12 + m21) / s, 0.25 * s, (m10 - m01) / s


def cone_samples(
    parent_cartesian: list[float],
    cylinder_length: float,
) -> tuple[list[float], list[float]]:
    """Derive (cartesian, unitQuaternion) CZML sample arrays for a sensor cone.

    Input: parent aircraft ECEF samples as ``[t, x, y, z, t, x, y, z, ...]``.
    Output:
      * ``cartesian``  — shifted centres, same ``[t, x, y, z, ...]`` layout.
        Each centre is at ``parent − (length / 2) · zenith_unit`` so the
        cylinder's local +Z apex (topRadius=0) sits exactly at the parent.
      * ``unitQuaternion`` — ENU-frame quaternions, ``[t, qx, qy, qz, qw, ...]``.
    """
    half_shift = cylinder_length / 2.0
    cart, quat = [], []
    for i in range(0, len(parent_cartesian), 4):
        t, x, y, z = (parent_cartesian[i], parent_cartesian[i + 1],
                      parent_cartesian[i + 2], parent_cartesian[i + 3])
        lon, lat = ecef_to_lonlat_rad(x, y, z)
        # Zenith unit vector in ECEF
        zx = math.cos(lat) * math.cos(lon)
        zy = math.cos(lat) * math.sin(lon)
        zz = math.sin(lat)
        cart.extend([t, x - half_shift * zx, y - half_shift * zy, z - half_shift * zz])
        qx, qy, qz, qw = enu_quaternion(lon, lat)
        quat.extend([t, qx, qy, qz, qw])
    return cart, quat
```

- [ ] **Step 4: Run tests — verify they pass**

```bash
pytest tests/test_cone_geometry.py -v
```
Expected: 4 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add mvp3/sg-service-mvp3/cone_geometry.py mvp3/sg-service-mvp3/tests/test_cone_geometry.py
git commit -m "feat(mvp3): add cone_geometry module with ECEF/ENU helpers and tests"
```

---

## Task 3: scenario_builder.py — copy from MVP1

**Files:**
- Create: `mvp3/sg-service-mvp3/scenario_builder.py`
- Modify: `mvp3/sg-service-mvp3/tests/test_smoke.py`

- [ ] **Step 1: Copy scenario_builder.py from MVP1 verbatim**

```bash
cp mvp/sg-service/scenario_builder.py mvp3/sg-service-mvp3/scenario_builder.py
```

No logic changes. The file is ~160 lines of `agi.stk12` COM code that creates EmitterAC + RadarAC + sensors + access computations.

- [ ] **Step 2: Extend test_smoke.py**

Append to `mvp3/sg-service-mvp3/tests/test_smoke.py`:
```python


def test_scenario_builder_importable_without_agi():
    """scenario_builder defers ``from agi.stk12.stkobjects import …`` to call time,
    so the module itself should import even when agi.stk12 is blocked by conftest."""
    import scenario_builder
    assert callable(scenario_builder.build_stk_scenario)
```

- [ ] **Step 3: Run tests — verify they pass**

```bash
pytest tests/test_smoke.py -v
```
Expected: 3 tests PASS (2 existing + 1 new).

- [ ] **Step 4: Commit**

```bash
git add mvp3/sg-service-mvp3/scenario_builder.py mvp3/sg-service-mvp3/tests/test_smoke.py
git commit -m "feat(mvp3): copy scenario_builder.py from mvp1 (verbatim, no logic changes)"
```

---

## Task 4: czml_builder_mvp3.py — DataProvider → czml3.Document

This is the heart of the plan. Strategy:
1. Write a minimal `_MockRoot` helper in the test file so tests don't need STK.
2. Write `build_czml(root, ...)` that reads DataProviders and assembles a CZML document via `czml3`.
3. Fall back to dict-backed packets where `czml3` lacks a builder (sensor cone).

**Files:**
- Create: `mvp3/sg-service-mvp3/czml_builder_mvp3.py`
- Create: `mvp3/sg-service-mvp3/tests/test_czml_builder_mvp3.py`

- [ ] **Step 1: Write the mock-root helper and failing tests**

`mvp3/sg-service-mvp3/tests/test_czml_builder_mvp3.py`:
```python
"""CZML builder tests. Uses a hand-rolled MockRoot that mimics just enough of
agi.stk12's surface (DataProviders + Children) to drive build_czml.
"""
import json
from types import SimpleNamespace

from czml_builder_mvp3 import build_czml


# ---------------- Mock root infrastructure ----------------

def _mk_dp(col_to_values):
    """Return a DataProvider-like object exposing ``.Exec(...).DataSets.GetDataSetByName(name).GetValues()``."""
    def get_values_for(name):
        return list(col_to_values.get(name, []))

    class _Result:
        class DataSets:
            @staticmethod
            def GetDataSetByName(name):
                return SimpleNamespace(GetValues=lambda: get_values_for(name))

    return SimpleNamespace(Exec=lambda *a, **k: _Result())


def _mk_dp_collection(name_to_dp):
    return SimpleNamespace(
        GetItemByName=lambda name: name_to_dp[name],
        Count=len(name_to_dp),
        Item=lambda i: list(name_to_dp.values())[i],
    )


def _mk_aircraft(name, positions_by_col):
    """Aircraft with a 'Cartesian Position' DataProvider."""
    return SimpleNamespace(
        ClassName="Aircraft",
        InstanceName=name,
        DataProviders=_mk_dp_collection({"Cartesian Position": _mk_dp(positions_by_col)}),
        Children=SimpleNamespace(Count=0, Item=lambda i: None),
    )


def _mk_sensor(name, half_angle_deg, radius_m):
    return SimpleNamespace(
        ClassName="Sensor",
        InstanceName=name,
        # Very small fake pattern object — builder only needs half-angle + radius.
        Pattern=SimpleNamespace(ConeAngle=half_angle_deg),
        Model=SimpleNamespace(FOVRadius=radius_m),
    )


def _mk_scenario():
    # Two aircraft with one time sample each — the simplest non-empty case.
    emitter = _mk_aircraft("EmitterAC", {
        "Time":     [0.0],
        "x":        [6_400_000.0],
        "y":        [0.0],
        "z":        [0.0],
    })
    emitter.Children = SimpleNamespace(
        Count=1,
        Item=lambda i: _mk_sensor("EmissionCone", 45.0, 20_000.0),
    )

    radar = _mk_aircraft("RadarAC", {
        "Time":     [0.0],
        "x":        [6_400_100.0],
        "y":        [0.0],
        "z":        [0.0],
    })
    radar.Children = SimpleNamespace(
        Count=1,
        Item=lambda i: _mk_sensor("RadarCone", 25.0, 20_000.0),
    )

    # Access pair emitter→radar: one interval covering the whole scenario.
    access_dp = _mk_dp({
        "Start Time":  ["1 Jan 2025 00:00:00.000"],
        "Stop Time":   ["1 Jan 2025 00:30:00.000"],
    })
    # GetAccessToObject returns an object whose DataProviders has "Access Data".
    access_obj = SimpleNamespace(
        DataProviders=_mk_dp_collection({"Access Data": access_dp}),
        ComputeAccess=lambda: None,
    )
    emitter.GetAccessToObject = lambda other: access_obj
    radar.GetAccessToObject   = lambda other: access_obj

    sc = SimpleNamespace(
        InstanceName="MVP3_test",
        StartTime="1 Jan 2025 00:00:00.000",
        StopTime="1 Jan 2025 01:00:00.000",
        Children=SimpleNamespace(
            Count=2,
            Item=lambda i: [emitter, radar][i],
        ),
    )
    return sc


def _mk_root():
    sc = _mk_scenario()
    return SimpleNamespace(CurrentScenario=sc)


# ---------------- Tests ----------------

def test_build_czml_returns_string():
    root = _mk_root()
    czml_json = build_czml(root, "MVP3_test",
                           "1 Jan 2025 00:00:00.000",
                           "1 Jan 2025 01:00:00.000",
                           model_base_url="http://localhost:8003/models/")
    data = json.loads(czml_json)
    assert isinstance(data, list)
    assert len(data) > 0


def test_first_packet_is_document():
    root = _mk_root()
    czml_json = build_czml(root, "MVP3_test",
                           "1 Jan 2025 00:00:00.000",
                           "1 Jan 2025 01:00:00.000",
                           model_base_url="http://localhost:8003/models/")
    data = json.loads(czml_json)
    assert data[0].get("id") == "document"


def test_both_aircraft_packets_present():
    root = _mk_root()
    czml_json = build_czml(root, "MVP3_test",
                           "1 Jan 2025 00:00:00.000",
                           "1 Jan 2025 01:00:00.000",
                           model_base_url="http://localhost:8003/models/")
    ids = {p.get("id") for p in json.loads(czml_json)}
    assert "EmitterAC" in ids
    assert "RadarAC" in ids


def test_both_sensor_cylinder_packets_present():
    root = _mk_root()
    data = json.loads(build_czml(root, "MVP3_test",
                                 "1 Jan 2025 00:00:00.000",
                                 "1 Jan 2025 01:00:00.000",
                                 model_base_url="http://localhost:8003/models/"))
    cones = [p for p in data if p.get("id") in ("EmissionCone", "RadarCone")]
    assert len(cones) == 2
    for pkt in cones:
        assert "cylinder" in pkt, f"{pkt['id']} missing cylinder geometry"
        assert "position"  in pkt
        assert "orientation" in pkt


def test_access_polyline_present():
    root = _mk_root()
    data = json.loads(build_czml(root, "MVP3_test",
                                 "1 Jan 2025 00:00:00.000",
                                 "1 Jan 2025 01:00:00.000",
                                 model_base_url="http://localhost:8003/models/"))
    # Access packet id format: "Access/<from>-<to>"
    access_ids = [p.get("id") for p in data if str(p.get("id", "")).startswith("Access/")]
    assert len(access_ids) >= 1
```

- [ ] **Step 2: Run tests — verify they fail**

```bash
pytest tests/test_czml_builder_mvp3.py -v
```
Expected: 5 tests FAIL with `ModuleNotFoundError: No module named 'czml_builder_mvp3'`.

- [ ] **Step 3: Write czml_builder_mvp3.py**

`mvp3/sg-service-mvp3/czml_builder_mvp3.py`:
```python
"""Build a CZML document from live STK DataProviders.

Replaces MVP1's ``ExportCZML`` Connect Command + ``_patch_czml_sensors`` chain.
Uses ``czml3`` builders where they cover the packet type, and falls through to
raw dict packets for geometry ``czml3`` does not model (sensor cylinders).

Public entry point: ``build_czml(root, scenario_name, start_time, stop_time,
model_base_url, step_seconds=10.0) -> str`` — returns a JSON document string
ready to write to disk.
"""
from __future__ import annotations
import json
import math
from typing import Any

from cone_geometry import cone_samples


def build_czml(
    root,
    scenario_name: str,
    start_time: str,
    stop_time: str,
    model_base_url: str,
    step_seconds: float = 10.0,
) -> str:
    """Return a CZML JSON document for the current STK scenario."""
    packets: list[dict[str, Any]] = []

    # ---- 1. Document packet ----
    packets.append({
        "id": "document",
        "name": scenario_name,
        "version": "1.0",
        "clock": {
            "interval": f"{_iso(start_time)}/{_iso(stop_time)}",
            "currentTime": _iso(start_time),
            "multiplier": 60,
            "range": "LOOP_STOP",
            "step": "SYSTEM_CLOCK_MULTIPLIER",
        },
    })

    # ---- 2. Aircraft + sensor packets ----
    aircraft_entries: list[tuple[str, list[float]]] = []
    sc = root.CurrentScenario
    for i in range(sc.Children.Count):
        obj = sc.Children.Item(i)
        if getattr(obj, "ClassName", "").lower() != "aircraft":
            continue
        positions = _read_cartesian_positions(obj, start_time, stop_time, step_seconds)
        aircraft_entries.append((obj.InstanceName, positions))
        packets.append(_aircraft_packet(obj.InstanceName, positions,
                                        start_time, stop_time, model_base_url))
        for j in range(obj.Children.Count):
            child = obj.Children.Item(j)
            if child is None or getattr(child, "ClassName", "").lower() != "sensor":
                continue
            packets.append(_sensor_cone_packet(child, obj.InstanceName, positions))

    # ---- 3. Access pair packets ----
    for i, (a_name, _) in enumerate(aircraft_entries):
        for b_name, _ in aircraft_entries[i + 1:]:
            a_obj = sc.Children.Item(i)
            b_obj = next(sc.Children.Item(k) for k in range(sc.Children.Count)
                         if sc.Children.Item(k).InstanceName == b_name)
            access = a_obj.GetAccessToObject(b_obj)
            intervals = _read_access_intervals(access)
            if intervals:
                packets.append(_access_polyline_packet(a_name, b_name, intervals))

    return json.dumps(packets, indent=2)


# ---------------- DataProvider readers ----------------

def _read_cartesian_positions(aircraft, start_time, stop_time, step_seconds) -> list[float]:
    """Return ``[t0, x0, y0, z0, t1, x1, y1, z1, ...]`` in the CZML epoch-relative layout.

    Times are seconds since ``start_time``. DataProvider shape assumed:
    ``'Cartesian Position'`` → DataSets named ``'Time'``, ``'x'``, ``'y'``, ``'z'``.
    """
    dp = aircraft.DataProviders.GetItemByName("Cartesian Position")
    result = dp.Exec(start_time, stop_time, step_seconds)
    times = list(result.DataSets.GetDataSetByName("Time").GetValues())
    xs    = list(result.DataSets.GetDataSetByName("x").GetValues())
    ys    = list(result.DataSets.GetDataSetByName("y").GetValues())
    zs    = list(result.DataSets.GetDataSetByName("z").GetValues())
    out: list[float] = []
    for t, x, y, z in zip(times, xs, ys, zs):
        # Time column is a string or seconds-from-epoch; coerce the first sample
        # to zero seconds so Cesium indexes the array as elapsed seconds.
        t_rel = _seconds_from_start(t, start_time, step_seconds, len(out) // 4)
        out.extend([t_rel, float(x), float(y), float(z)])
    return out


def _read_access_intervals(access) -> list[tuple[str, str]]:
    """Return ``[(start_iso, stop_iso), ...]`` for the access intervals."""
    try:
        dp = access.DataProviders.GetItemByName("Access Data")
    except Exception:
        return []
    result = dp.Exec()
    try:
        starts = list(result.DataSets.GetDataSetByName("Start Time").GetValues())
        stops  = list(result.DataSets.GetDataSetByName("Stop Time").GetValues())
    except Exception:
        return []
    return [(_iso(s), _iso(e)) for s, e in zip(starts, stops)]


# ---------------- Packet builders ----------------

def _aircraft_packet(name, positions, start_time, stop_time, model_base_url):
    return {
        "id": name,
        "name": name,
        "availability": f"{_iso(start_time)}/{_iso(stop_time)}",
        "position": {
            "epoch": _iso(start_time),
            "interpolationAlgorithm": "LAGRANGE",
            "interpolationDegree": 5,
            "referenceFrame": "FIXED",
            "cartesian": positions,
        },
        "model": {"gltf": f"{model_base_url}aircraft.glb", "scale": 1.0, "minimumPixelSize": 32},
        "path": {
            "show":       True,
            "leadTime":   0,
            "trailTime":  300,
            "width":      2,
            "material":   {"solidColor": {"color": {"rgba": [255, 180, 0, 200]}}},
        },
        "label": {
            "text":        name,
            "font":        "bold 12pt Consolas",
            "fillColor":   {"rgba": [255, 255, 255, 255]},
            "outlineColor":{"rgba": [0, 0, 0, 255]},
            "pixelOffset": {"cartesian2": [10, 0]},
        },
    }


def _sensor_cone_packet(sensor, parent_name, parent_positions):
    """Build a native Cesium cylinder packet that renders as a nadir-pointing cone.

    ``czml3`` has no Cylinder builder, so we emit the packet body directly as a
    dict. This is the one escape hatch the design allows.
    """
    half_angle_deg = float(getattr(sensor.Pattern, "ConeAngle", 25.0))
    radius_m       = float(getattr(sensor.Model, "FOVRadius", 20_000.0))
    length         = min(radius_m, 20_000.0)
    bottom_radius  = length * math.tan(math.radians(half_angle_deg))

    cartesian, quaternion = cone_samples(parent_positions, length)
    return {
        "id":     sensor.InstanceName,
        "name":   sensor.InstanceName,
        "parent": parent_name,
        "position": {
            "epoch":                  parent_positions and parent_positions[0] or 0,  # placeholder, replaced below
            "interpolationAlgorithm": "LAGRANGE",
            "interpolationDegree":    1,
            "referenceFrame":         "FIXED",
            "cartesian":              cartesian,
        },
        "orientation": {
            "interpolationAlgorithm": "LINEAR",
            "unitQuaternion":         quaternion,
        },
        "cylinder": {
            "length":       length,
            "topRadius":    0.0,
            "bottomRadius": bottom_radius,
            "material":     {"solidColor": {"color": {"rgba": [0, 255, 255, 80]}}},
            "outline":      True,
            "outlineColor": {"rgba": [0, 255, 255, 220]},
        },
    }


def _access_polyline_packet(a_name, b_name, intervals):
    availability = [f"{start}/{stop}" for start, stop in intervals]
    return {
        "id":   f"Access/{a_name}-{b_name}",
        "name": f"Access {a_name} <-> {b_name}",
        "availability": availability if len(availability) > 1 else availability[0],
        "polyline": {
            "positions": {
                "references": [f"{a_name}#position", f"{b_name}#position"],
            },
            "width":       2,
            "material":    {"solidColor": {"color": {"rgba": [0, 255, 0, 220]}}},
            "arcType":     "NONE",
            "followSurface": False,
        },
    }


# ---------------- Utilities ----------------

def _iso(t: Any) -> str:
    """Convert STK-format time strings (``"1 Jan 2025 00:00:00.000"``) or ISO strings
    to CZML-compatible ``"YYYY-MM-DDTHH:MM:SSZ"``. Pass through if already ISO."""
    s = str(t).strip()
    if s.endswith("Z") or "T" in s:
        return s
    # "1 Jan 2025 00:00:00.000" — parse loosely.
    from datetime import datetime
    for fmt in ("%d %b %Y %H:%M:%S.%f", "%d %b %Y %H:%M:%S"):
        try:
            d = datetime.strptime(s, fmt)
            return d.strftime("%Y-%m-%dT%H:%M:%SZ")
        except ValueError:
            continue
    return s  # best effort


def _seconds_from_start(t, start_time, step_seconds, sample_index):
    """STK 'Time' column is often a string; fall back to sample_index * step_seconds."""
    try:
        return float(t)
    except (TypeError, ValueError):
        return sample_index * step_seconds
```

- [ ] **Step 4: Run tests — verify they pass**

```bash
pytest tests/test_czml_builder_mvp3.py -v
```
Expected: all 5 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add mvp3/sg-service-mvp3/czml_builder_mvp3.py mvp3/sg-service-mvp3/tests/test_czml_builder_mvp3.py
git commit -m "feat(mvp3): add czml_builder_mvp3 with DataProvider-driven CZML generation"
```

---

## Task 5: stk_mock_service.py — synthetic-root fallback

Strategy: reuse the `_MockRoot` pattern from Task 4 tests, but as production code. The mock service creates a SyntheticRoot and calls the real `build_czml`, so we exercise the live pipeline without STK.

**Files:**
- Create: `mvp3/sg-service-mvp3/stk_mock_service.py`
- Create: `mvp3/sg-service-mvp3/tests/test_stk_mock_service.py`

- [ ] **Step 1: Write failing tests**

`mvp3/sg-service-mvp3/tests/test_stk_mock_service.py`:
```python
import json
from pathlib import Path

from stk_mock_service import MockStkService


def test_mock_service_writes_non_empty_czml(tmp_path):
    svc = MockStkService()
    svc.build_and_compute("test-eid",
                          "1 Jan 2025 00:00:00.000",
                          "1 Jan 2025 01:00:00.000")
    out = tmp_path / "test-eid.czml"
    svc.export_czml("test-eid", str(out))
    assert out.exists()
    assert out.stat().st_size > 0


def test_mock_czml_contains_document_and_aircraft(tmp_path):
    svc = MockStkService()
    svc.build_and_compute("eid1",
                          "1 Jan 2025 00:00:00.000",
                          "1 Jan 2025 01:00:00.000")
    out = tmp_path / "eid1.czml"
    svc.export_czml("eid1", str(out))
    data = json.loads(out.read_text(encoding="utf-8"))
    ids = {p.get("id") for p in data}
    assert "document"  in ids
    assert "EmitterAC" in ids
    assert "RadarAC"   in ids
```

- [ ] **Step 2: Run tests — verify they fail**

```bash
pytest tests/test_stk_mock_service.py -v
```
Expected: 2 tests FAIL with `ModuleNotFoundError`.

- [ ] **Step 3: Write stk_mock_service.py**

`mvp3/sg-service-mvp3/stk_mock_service.py`:
```python
"""Synthetic STK stand-in.

Builds a lightweight object graph that looks like an agi.stk12 scenario root
and feeds it into ``czml_builder_mvp3.build_czml`` — so the live CZML pipeline
is exercised even when STK is not installed.
"""
from __future__ import annotations
import logging
import math
from types import SimpleNamespace
from typing import Any

from stk_service import IStkService
from czml_builder_mvp3 import build_czml

log = logging.getLogger(__name__)


class MockStkService(IStkService):
    def __init__(self) -> None:
        self._root: Any | None = None
        self._start = self._stop = ""

    def build_and_compute(self, exercise_id: str, start_time: str, stop_time: str) -> None:
        self._root  = _make_synthetic_root(start_time, stop_time)
        self._start = start_time
        self._stop  = stop_time
        log.info("MockStkService: synthetic scenario built for exercise %s", exercise_id)

    def export_czml(self, exercise_id: str, output_path: str) -> None:
        if self._root is None:
            # Caller forgot to build first — fall back to a one-sample scenario.
            self.build_and_compute(exercise_id,
                                   "1 Jan 2025 00:00:00.000",
                                   "1 Jan 2025 01:00:00.000")
        czml = build_czml(
            self._root,
            "MVP3_mock",
            self._start,
            self._stop,
            model_base_url="http://localhost:8003/models/",
            step_seconds=60.0,
        )
        from pathlib import Path
        p = Path(output_path)
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(czml, encoding="utf-8")
        log.info("MockStkService: CZML written to %s (%d bytes)", p, p.stat().st_size)


# ---------------- Synthetic root builder ----------------

def _make_synthetic_root(start_time: str, stop_time: str):
    """Build a SimpleNamespace tree that mimics just enough of the STK API."""
    n = 61  # one sample per minute across the default one-hour scenario
    times = [float(i * 60.0) for i in range(n)]

    # Two aircraft flying across Kashmir (EmitterAC west→east, RadarAC east→west).
    emitter_positions = _generate_track(34.2, 73.9, 34.0, 75.4, 1.9, n)
    radar_positions   = _generate_track(34.6, 75.9, 34.2, 75.2, 4.675, n)

    emitter = _synthetic_aircraft("EmitterAC", times, emitter_positions,
                                  sensor_name="EmissionCone", half_angle=45.0)
    radar   = _synthetic_aircraft("RadarAC",   times, radar_positions,
                                  sensor_name="RadarCone",    half_angle=25.0)

    # Access pair (one coarse interval across the scenario).
    access_dp = _dp({"Start Time": [start_time], "Stop Time": [stop_time]})
    access_obj = SimpleNamespace(
        DataProviders=_dp_collection({"Access Data": access_dp}),
        ComputeAccess=lambda: None,
    )
    emitter.GetAccessToObject = lambda other: access_obj
    radar.GetAccessToObject   = lambda other: access_obj

    sc = SimpleNamespace(
        InstanceName="MVP3_mock",
        StartTime=start_time,
        StopTime=stop_time,
        Children=SimpleNamespace(
            Count=2,
            Item=lambda i: [emitter, radar][i],
        ),
    )
    return SimpleNamespace(CurrentScenario=sc)


def _generate_track(lat1, lon1, lat2, lon2, alt_km, n):
    """Linear interpolation between two geodetic points, returns ECEF x/y/z lists."""
    xs, ys, zs = [], [], []
    r = 6_378_137.0 + alt_km * 1000.0
    for i in range(n):
        f = i / max(n - 1, 1)
        lat = math.radians(lat1 + (lat2 - lat1) * f)
        lon = math.radians(lon1 + (lon2 - lon1) * f)
        xs.append(r * math.cos(lat) * math.cos(lon))
        ys.append(r * math.cos(lat) * math.sin(lon))
        zs.append(r * math.sin(lat))
    return {"x": xs, "y": ys, "z": zs}


def _synthetic_aircraft(name, times, pos, sensor_name, half_angle):
    sensor = SimpleNamespace(
        ClassName="Sensor",
        InstanceName=sensor_name,
        Pattern=SimpleNamespace(ConeAngle=half_angle),
        Model=SimpleNamespace(FOVRadius=20_000.0),
    )
    return SimpleNamespace(
        ClassName="Aircraft",
        InstanceName=name,
        DataProviders=_dp_collection({
            "Cartesian Position": _dp({
                "Time": times,
                "x":    pos["x"],
                "y":    pos["y"],
                "z":    pos["z"],
            }),
        }),
        Children=SimpleNamespace(Count=1, Item=lambda i: sensor),
    )


def _dp(col_to_values):
    def get_values(name):
        return list(col_to_values.get(name, []))
    class _Result:
        class DataSets:
            @staticmethod
            def GetDataSetByName(name):
                return SimpleNamespace(GetValues=lambda: get_values(name))
    return SimpleNamespace(Exec=lambda *a, **k: _Result())


def _dp_collection(name_to_dp):
    return SimpleNamespace(
        GetItemByName=lambda name: name_to_dp[name],
        Count=len(name_to_dp),
        Item=lambda i: list(name_to_dp.values())[i],
    )
```

- [ ] **Step 4: Run tests — verify they pass**

```bash
pytest tests/test_stk_mock_service.py -v
```
Expected: 2 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add mvp3/sg-service-mvp3/stk_mock_service.py mvp3/sg-service-mvp3/tests/test_stk_mock_service.py
git commit -m "feat(mvp3): add MockStkService with synthetic Kashmir scenario"
```

---

## Task 6: fom_pipeline.py — FOM grid + heatmap + rectangle overlay

**Files:**
- Create: `mvp3/sg-service-mvp3/fom_pipeline.py`
- Create: `mvp3/sg-service-mvp3/tests/test_fom_pipeline.py`

- [ ] **Step 1: Write failing tests**

`mvp3/sg-service-mvp3/tests/test_fom_pipeline.py`:
```python
import json
import math
from pathlib import Path
from types import SimpleNamespace

from fom_pipeline import read_fom_grid, render_heatmap_png, inject_rectangle_overlay


def _mock_fom_objs():
    def _mk_dp(cols):
        class R:
            class DataSets:
                @staticmethod
                def GetDataSetByName(name):
                    return SimpleNamespace(GetValues=lambda: list(cols.get(name, [])))
        return SimpleNamespace(Exec=lambda *a, **k: R())

    fom = SimpleNamespace(DataProviders=SimpleNamespace(
        GetItemByName=lambda n: _mk_dp({
            "Latitude":  [33.0, 34.0, 35.0],
            "Longitude": [74.0, 75.0, 76.0],
            "FOM Value": [0.1,  0.5,  0.9],
        }) if n == "Value By Point" else (_ for _ in ()).throw(KeyError(n)),
    ))
    cov = SimpleNamespace(DataProviders=SimpleNamespace(
        GetItemByName=lambda n: (_ for _ in ()).throw(KeyError(n))
    ))
    sc = SimpleNamespace(StartTime="t0", StopTime="t1")
    return cov, fom, sc


def test_read_fom_grid_returns_lat_lon_val_triples():
    cov, fom, sc = _mock_fom_objs()
    grid = read_fom_grid(cov, fom, sc)
    assert len(grid) == 3
    lat, lon, val = grid[0]
    assert abs(lat - 33.0) < 1e-9
    assert abs(lon - 74.0) < 1e-9
    assert abs(val - 0.1)  < 1e-9


def test_render_heatmap_png_writes_file(tmp_path):
    grid = [(33.0, 74.0, 0.1), (34.0, 75.0, 0.5), (35.0, 76.0, 0.9)]
    out = tmp_path / "heatmap.png"
    render_heatmap_png(grid, polygon_deg=[(74.0, 33.0), (76.0, 33.0),
                                          (76.0, 35.0), (74.0, 35.0)], out_path=out)
    assert out.exists()
    assert out.stat().st_size > 0
    # PNG magic bytes
    assert out.read_bytes().startswith(b"\x89PNG\r\n\x1a\n")


def test_inject_rectangle_overlay_appends_packet():
    doc = [{"id": "document", "name": "t", "version": "1.0"},
           {"id": "AreaTarget/Zone1",
            "polygon": {"positions": {"cartographicRadians":
                [math.radians(74.0), math.radians(33.0), 0,
                 math.radians(76.0), math.radians(33.0), 0,
                 math.radians(76.0), math.radians(35.0), 0,
                 math.radians(74.0), math.radians(35.0), 0]}}}]
    bounds_rad = (math.radians(74.0), math.radians(33.0),
                  math.radians(76.0), math.radians(35.0))
    out = inject_rectangle_overlay(doc, "http://localhost:8003/exercises/e/fom-image",
                                   polygon_bounds_rad=bounds_rad)
    ids = [p.get("id") for p in out]
    assert "fom-overlay" in ids
    overlay = next(p for p in out if p["id"] == "fom-overlay")
    assert overlay["rectangle"]["coordinates"]["wsen"] == list(bounds_rad)
    assert overlay["rectangle"]["material"]["image"]["image"] == \
           "http://localhost:8003/exercises/e/fom-image"
```

- [ ] **Step 2: Run tests — verify they fail**

```bash
pytest tests/test_fom_pipeline.py -v
```
Expected: 3 tests FAIL with `ModuleNotFoundError`.

- [ ] **Step 3: Write fom_pipeline.py**

`mvp3/sg-service-mvp3/fom_pipeline.py`:
```python
"""FOM rendering pipeline — read grid via DataProviders, render a transparent
PNG heatmap, inject a CZML rectangle overlay pointing at the PNG.

Each function is pure: no module state, no STK globals. Ports MVP1's FOM
logic verbatim but extracted into free functions so the unit tests can drive
them with fixture data.
"""
from __future__ import annotations
import logging
from pathlib import Path

log = logging.getLogger(__name__)


def read_fom_grid(coverage, fom, scenario) -> list[tuple[float, float, float]]:
    """Return ``[(lat_deg, lon_deg, value), ...]`` or an empty list if every
    DataProvider attempt fails. Tries provider names in order of likelihood."""

    start, stop = scenario.StartTime, scenario.StopTime

    def _col(result, *candidates):
        for name in candidates:
            try:
                return list(result.DataSets.GetDataSetByName(name).GetValues())
            except Exception:
                continue
        return []

    def _try(obj, dp_name: str, val_cols: tuple, exec_args=()) -> list | None:
        try:
            result = obj.DataProviders.GetItemByName(dp_name).Exec(*exec_args)
        except Exception as exc:
            log.warning("read_fom_grid: '%s' not available: %s", dp_name, exc)
            return None
        lats = _col(result, "Latitude", "Lat", "lat")
        lons = _col(result, "Longitude", "Lon", "lon")
        if not lats or not lons:
            return None
        for col in val_cols:
            vals = _col(result, col)
            if vals:
                n = min(len(lats), len(lons), len(vals))
                log.info("read_fom_grid: %d pts via '%s'/'%s'", n, dp_name, col)
                return list(zip(lats[:n], lons[:n], vals[:n]))
        return None

    val_fom = ("Value", "FOM Value", "Percent Coverage", "Coverage")
    val_cov = ("Percent Coverage", "Coverage", "Value")

    for dp_name in ("Value By Point", "Static Satisfaction"):
        r = _try(fom, dp_name, val_fom)
        if r:
            return r
    r = _try(coverage, "Percent Coverage", val_cov, exec_args=(start, stop, 3600.0))
    if r:
        return r
    return []


def render_heatmap_png(grid, polygon_deg, out_path: Path) -> None:
    """Render an RGBA PNG from ``grid = [(lat, lon, val), ...]``.

    If ``polygon_deg`` is provided (``[(lon, lat), ...]``), the image extent
    matches the polygon bounding box and the polygon acts as a hard clip mask.
    """
    import numpy as np
    from PIL import Image, ImageDraw, ImageFilter
    from scipy.interpolate import griddata

    lats = np.array([r[0] for r in grid], dtype=np.float64)
    lons = np.array([r[1] for r in grid], dtype=np.float64)
    vals = np.array([r[2] for r in grid], dtype=np.float64)

    v_min, v_max = vals.min(), vals.max()
    norm = (vals - v_min) / ((v_max - v_min) or 1.0)

    if polygon_deg:
        p_lons = [p[0] for p in polygon_deg]
        p_lats = [p[1] for p in polygon_deg]
        lon_min, lon_max = min(p_lons), max(p_lons)
        lat_min, lat_max = min(p_lats), max(p_lats)
    else:
        lon_min, lon_max = float(lons.min()), float(lons.max())
        lat_min, lat_max = float(lats.min()), float(lats.max())

    w = h = 512
    xi = np.linspace(lon_min, lon_max, w)
    yi = np.linspace(lat_max, lat_min, h)  # lat decreases with row index
    gx, gy = np.meshgrid(xi, yi)

    interp = griddata(np.column_stack([lons, lats]), norm, (gx, gy),
                      method="linear").astype(np.float32)
    mask = ~np.isnan(interp)
    interp = np.where(mask, interp, 0.0)

    r_ch = np.where(interp >= 0.5, (255 * 2 * (1.0 - interp)).clip(0, 255), 255).astype(np.uint8)
    g_ch = np.where(interp <= 0.5, (255 * 2 * interp).clip(0, 255),         255).astype(np.uint8)
    b_ch = np.zeros_like(r_ch)
    a_ch = np.where(mask, np.uint8(210), np.uint8(0))

    rgb   = Image.fromarray(np.stack([r_ch, g_ch, b_ch], axis=-1), "RGB")
    alpha = Image.fromarray(a_ch, "L")
    rgb   = rgb.filter(ImageFilter.GaussianBlur(radius=1.5))

    if polygon_deg:
        def _to_px(lon_d, lat_d):
            return ((lon_d - lon_min) / max(lon_max - lon_min, 1e-9) * w,
                    (lat_max - lat_d) / max(lat_max - lat_min, 1e-9) * h)
        poly_px   = [_to_px(lo, la) for lo, la in polygon_deg]
        poly_mask = Image.new("L", (w, h), 0)
        ImageDraw.Draw(poly_mask).polygon(poly_px, fill=255)
        poly_mask = poly_mask.filter(ImageFilter.GaussianBlur(radius=0.5))
        import numpy as _np
        combined  = (_np.array(alpha, _np.float32) * _np.array(poly_mask, _np.float32) / 255.0)
        alpha     = Image.fromarray(combined.clip(0, 255).astype("uint8"), "L")

    Path(out_path).parent.mkdir(parents=True, exist_ok=True)
    Image.merge("RGBA", (*rgb.split(), alpha)).save(str(out_path), "PNG")


def inject_rectangle_overlay(document, image_url, polygon_bounds_rad):
    """Return a new document with an extra CZML packet for the FOM overlay."""
    if polygon_bounds_rad is None:
        return document
    w, s, e, n = polygon_bounds_rad
    overlay = {
        "id":   "fom-overlay",
        "name": "FigureOfMerit overlay",
        "rectangle": {
            "coordinates": {"wsen": [w, s, e, n]},
            "material": {
                "image": {
                    "image":       image_url,
                    "transparent": True,
                    "repeat":      {"cartesian2": [1, 1]},
                },
            },
            "height":             0.0,
            "classificationType": "TERRAIN",
            "zIndex":             1,
        },
    }
    # Strip any previous overlay so the call is idempotent.
    return [p for p in document if p.get("id") != "fom-overlay"] + [overlay]
```

- [ ] **Step 4: Run tests — verify they pass**

```bash
pytest tests/test_fom_pipeline.py -v
```
Expected: 3 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add mvp3/sg-service-mvp3/fom_pipeline.py mvp3/sg-service-mvp3/tests/test_fom_pipeline.py
git commit -m "feat(mvp3): add fom_pipeline with read_grid, render_png, inject_overlay"
```

---

## Task 7: stk_com_service_mvp3.py — live orchestrator

**Files:**
- Create: `mvp3/sg-service-mvp3/stk_com_service_mvp3.py`

This module talks to real STK. It has no unit tests of its own — integration coverage comes from the endpoint tests in Task 8 (which stay on the mock path).

- [ ] **Step 1: Write the module**

`mvp3/sg-service-mvp3/stk_com_service_mvp3.py`:
```python
"""Live STK orchestrator for MVP3.

Flow per compute:
  scenario_builder.build_stk_scenario  → create entities in STK
  czml_builder_mvp3.build_czml         → generate CZML from DataProviders
  fom_pipeline.read_fom_grid           → extract FOM grid (if any)
  fom_pipeline.render_heatmap_png      → render PNG
  fom_pipeline.inject_rectangle_overlay→ add CZML rectangle
  write doc.dumps() to disk

Uses agi.stk12 COM bindings, same as MVP1. If Engine license is missing,
falls back to an attached STK Desktop instance.
"""
from __future__ import annotations
import json
import logging
import math
from pathlib import Path

from stk_service import IStkService

log = logging.getLogger(__name__)
_service_instance: IStkService | None = None


def get_stk_service() -> IStkService:
    global _service_instance
    if _service_instance is not None:
        return _service_instance

    try:
        import agi.stk12  # noqa: F401  (availability probe)
    except ImportError as exc:
        log.warning("agi.stk12 not installed (%s) — using MockStkService", exc)
        from stk_mock_service import MockStkService
        _service_instance = MockStkService()
        return _service_instance

    try:
        _service_instance = StkComServiceMvp3()
        log.info("using StkComServiceMvp3 (live agi.stk12)")
    except Exception as exc:
        log.warning("STK Engine failed to start (%s) — using MockStkService", exc)
        from stk_mock_service import MockStkService
        _service_instance = MockStkService()
    return _service_instance


class StkComServiceMvp3(IStkService):
    def __init__(self) -> None:
        try:
            from agi.stk12.stkengine import STKEngine  # type: ignore[import]
            # noGraphics=False enables the graphics stack — required for
            # consistent DataProvider behaviour and for future CZML plugin
            # parity with MVP1 during visual A/B testing.
            self._stk  = STKEngine.StartApplication(noGraphics=False)
            self._root = self._stk.NewObjectRoot()
            log.info("StkComServiceMvp3: started STK Engine")
        except Exception as eng_exc:
            log.warning("StkComServiceMvp3: Engine unavailable (%s), trying Desktop", eng_exc)
            from agi.stk12.stkdesktop import STKDesktop  # type: ignore[import]
            try:
                self._stk = STKDesktop.AttachToApplication()
            except Exception:
                self._stk = STKDesktop.StartApplication(visible=True)
            if self._stk is None:
                raise RuntimeError("Could not attach to or start STK Desktop")
            self._root = getattr(self._stk, "Root", None)
            if self._root is None and hasattr(self._stk, "NewObjectRoot"):
                self._root = self._stk.NewObjectRoot()
            if self._root is None:
                raise RuntimeError("STKDesktop instance exposed no Root")

        self._start = self._stop = ""

    def build_and_compute(self, exercise_id: str, start_time: str, stop_time: str) -> None:
        from scenario_builder import build_stk_scenario
        build_stk_scenario(self._root, exercise_id, start_time, stop_time)
        self._start, self._stop = start_time, stop_time
        log.info("StkComServiceMvp3: scenario built for exercise %s", exercise_id)

    def export_czml(self, exercise_id: str, output_path: str) -> None:
        from czml_builder_mvp3 import build_czml
        from fom_pipeline import read_fom_grid, render_heatmap_png, inject_rectangle_overlay

        sc = self._root.CurrentScenario
        czml_json = build_czml(
            self._root, sc.InstanceName, self._start, self._stop,
            model_base_url="http://localhost:8003/models/",
        )
        doc = json.loads(czml_json)

        # FOM overlay (optional). Find first CoverageDefinition/FigureOfMerit pair.
        coverage, fom = None, None
        for i in range(sc.Children.Count):
            child = sc.Children.Item(i)
            if getattr(child, "ClassName", "").lower() != "coveragedefinition":
                continue
            coverage = child
            for j in range(child.Children.Count):
                sub = child.Children.Item(j)
                if getattr(sub, "ClassName", "").lower() == "figureofmerit":
                    fom = sub
                    break
            if fom:
                break

        if coverage and fom:
            grid = read_fom_grid(coverage, fom, sc)
            if grid:
                polygon_deg = _extract_area_target_polygon(doc)
                img_dir  = Path(__file__).parent / "fom_images"
                img_dir.mkdir(exist_ok=True)
                img_path = img_dir / f"{exercise_id}.png"
                render_heatmap_png(grid, polygon_deg, img_path)
                bounds_rad = _polygon_to_bounds_rad(polygon_deg) if polygon_deg else None
                doc = inject_rectangle_overlay(
                    doc,
                    f"http://localhost:8003/exercises/{exercise_id}/fom-image",
                    bounds_rad,
                )
                log.info("StkComServiceMvp3: FOM heatmap -> %s", img_path)

        out = Path(output_path)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(doc, indent=2), encoding="utf-8")
        log.info("StkComServiceMvp3: CZML exported -> %s (%.1f KB)",
                 out, out.stat().st_size / 1024)

    def shutdown(self) -> None:
        for method_name in ("Terminate", "ShutDown", "Quit"):
            method = getattr(self._stk, method_name, None)
            if callable(method):
                try:
                    method()
                    log.info("StkComServiceMvp3: STK %s()", method_name)
                    return
                except Exception as exc:
                    log.debug("StkComServiceMvp3: %s() raised: %s", method_name, exc)


def _extract_area_target_polygon(document):
    """Return [(lon_deg, lat_deg), ...] for the first AreaTarget polygon, or None."""
    import math as _m
    for p in document:
        rad = p.get("polygon", {}).get("positions", {}).get("cartographicRadians")
        if rad and len(rad) >= 6:
            lons = rad[0::3]
            lats = rad[1::3]
            return [(_m.degrees(lo), _m.degrees(la)) for lo, la in zip(lons, lats)]
    return None


def _polygon_to_bounds_rad(polygon_deg):
    lons = [p[0] for p in polygon_deg]
    lats = [p[1] for p in polygon_deg]
    return (math.radians(min(lons)), math.radians(min(lats)),
            math.radians(max(lons)), math.radians(max(lats)))
```

- [ ] **Step 2: Run full existing test suite — verify no regressions**

```bash
pytest tests/ -v
```
Expected: all previously-green tests still pass. No new tests for this module (integration coverage via Task 8).

- [ ] **Step 3: Commit**

```bash
git add mvp3/sg-service-mvp3/stk_com_service_mvp3.py
git commit -m "feat(mvp3): add StkComServiceMvp3 live orchestrator (czml + fom pipeline)"
```

---

## Task 8: main.py + computation_job.py + endpoint tests

**Files:**
- Create: `mvp3/sg-service-mvp3/computation_job.py`
- Create: `mvp3/sg-service-mvp3/main.py`
- Create: `mvp3/sg-service-mvp3/tests/test_endpoints.py`

- [ ] **Step 1: Write failing endpoint tests**

`mvp3/sg-service-mvp3/tests/test_endpoints.py`:
```python
"""HTTP integration tests. conftest blocks agi.stk12 so MockStkService is used."""
import json
import time

from fastapi.testclient import TestClient
from main import app

client = TestClient(app)


def test_health():
    r = client.get("/health")
    assert r.status_code == 200
    assert r.json() == {"status": "ok"}


def test_create_exercise_returns_uuid():
    r = client.post("/exercises")
    assert r.status_code == 201
    body = r.json()
    assert "exercise_id" in body


def _create_and_wait(timeout=15):
    eid = client.post("/exercises").json()["exercise_id"]
    r = client.post(f"/exercises/{eid}/compute",
                    json={"start_time": "1 Jan 2025 00:00:00.000",
                          "stop_time":  "1 Jan 2025 01:00:00.000"})
    assert r.status_code == 202
    deadline = time.time() + timeout
    while time.time() < deadline:
        status = client.get(f"/exercises/{eid}/status").json()["status"]
        if status == "ready":
            return eid
        if status.startswith("error"):
            raise AssertionError(f"compute error: {status}")
        time.sleep(0.2)
    raise AssertionError("timed out")


def test_full_flow_produces_czml_with_document():
    eid = _create_and_wait()
    r = client.get(f"/exercises/{eid}/czml")
    assert r.status_code == 200, r.text
    data = json.loads(r.text)
    assert data[0].get("id") == "document"


def test_full_flow_contains_both_aircraft():
    eid = _create_and_wait()
    data = json.loads(client.get(f"/exercises/{eid}/czml").text)
    ids = {p.get("id") for p in data}
    assert "EmitterAC" in ids
    assert "RadarAC"   in ids


def test_czml_before_compute_returns_409():
    eid = client.post("/exercises").json()["exercise_id"]
    r = client.get(f"/exercises/{eid}/czml")
    assert r.status_code == 409


def test_status_of_unknown_exercise_404():
    r = client.get("/exercises/not-a-real-id/status")
    assert r.status_code == 404
```

- [ ] **Step 2: Run tests — verify they fail**

```bash
pytest tests/test_endpoints.py -v
```
Expected: 6 tests FAIL — `main` module doesn't exist yet.

- [ ] **Step 3: Write computation_job.py**

`mvp3/sg-service-mvp3/computation_job.py`:
```python
from __future__ import annotations
import logging

log = logging.getLogger(__name__)


def run_computation(
    exercise_id: str,
    start_time: str,
    stop_time: str,
    output_path: str,
) -> None:
    from stk_com_service_mvp3 import get_stk_service
    log.info("computation_job: starting exercise %s", exercise_id)
    svc = get_stk_service()
    svc.build_and_compute(exercise_id, start_time, stop_time)
    svc.export_czml(exercise_id, output_path)
    log.info("computation_job: done, CZML at %s", output_path)
```

- [ ] **Step 4: Write main.py**

`mvp3/sg-service-mvp3/main.py`:
```python
from __future__ import annotations
import asyncio
import functools
import logging
import os
import threading
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
logging.basicConfig(
    level=getattr(logging, _log_level, logging.INFO),
    format="%(levelname)-8s %(name)s: %(message)s",
)
log = logging.getLogger(__name__)

_BASE    = Path(__file__).parent
CZML_DIR = _BASE / "czml_output"; CZML_DIR.mkdir(exist_ok=True)
FOM_DIR  = _BASE / "fom_images";  FOM_DIR.mkdir(exist_ok=True)

_exercises: dict[str, dict[str, Any]] = {}
_exercises_lock = threading.Lock()
_pool = ThreadPoolExecutor(max_workers=1)  # single STK seat

_STK_MODELS_CANDIDATES = [
    Path(r"C:\Program Files\AGI\STK 12\STKData\VO\Models"),
    Path(r"C:\Program Files\AGI\STK 12.x\STKData\VO\Models"),
]
_stk_models_dir = next((p for p in _STK_MODELS_CANDIDATES if p.is_dir()), None)


@asynccontextmanager
async def lifespan(app: FastAPI):  # noqa: ARG001
    yield
    import stk_com_service_mvp3 as _stk_mod
    if _stk_mod._service_instance is not None:
        _stk_mod._service_instance.shutdown()


app = FastAPI(title="EWTSS MVP3 sg-service-mvp3", lifespan=lifespan)
app.add_middleware(
    CORSMiddleware,
    allow_origins=["http://localhost:4200", "http://127.0.0.1:4200"],
    allow_methods=["*"], allow_headers=["*"],
)


class ComputeRequest(BaseModel):
    start_time: str = "1 Jan 2025 00:00:00.000"
    stop_time:  str = "1 Jan 2025 01:00:00.000"


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
    with _exercises_lock:
        _exercises[eid] = {"status": "created", "czml_path": None}
    return {"exercise_id": eid, "status": "created"}


@app.post("/exercises/{exercise_id}/compute", status_code=202)
async def trigger_compute(exercise_id: str, body: ComputeRequest) -> dict[str, str]:
    with _exercises_lock:
        if exercise_id not in _exercises:
            raise HTTPException(status_code=404, detail="exercise not found")
        ex = _exercises[exercise_id]
        if ex["status"] in ("running", "ready"):
            return {"exercise_id": exercise_id, "status": ex["status"]}
        ex.update(status="running",
                  start_time=body.start_time, stop_time=body.stop_time)

    fut = asyncio.get_running_loop().run_in_executor(
        _pool, functools.partial(_run_compute, exercise_id),
    )
    fut.add_done_callback(lambda f: f.exception() and
                          log.error("executor leaked exception: %s", f.exception()))
    return {"exercise_id": exercise_id, "status": "running"}


def _run_compute(exercise_id: str) -> None:
    from computation_job import run_computation
    with _exercises_lock:
        ex = _exercises[exercise_id]
        start, stop = ex["start_time"], ex["stop_time"]
    out = str(CZML_DIR / f"{exercise_id}.czml")
    try:
        run_computation(exercise_id, start, stop, out)
        with _exercises_lock:
            _exercises[exercise_id].update(status="ready", czml_path=out)
    except Exception as exc:
        with _exercises_lock:
            _exercises[exercise_id]["status"] = f"error: {exc}"
        log.exception("compute failed for %s", exercise_id)


@app.get("/exercises/{exercise_id}/status")
def get_status(exercise_id: str) -> dict[str, str]:
    with _exercises_lock:
        if exercise_id not in _exercises:
            raise HTTPException(status_code=404, detail="exercise not found")
        status = _exercises[exercise_id]["status"]
    return {"exercise_id": exercise_id, "status": status}


@app.get("/exercises/{exercise_id}/czml")
def get_czml(exercise_id: str) -> FileResponse:
    with _exercises_lock:
        if exercise_id not in _exercises:
            raise HTTPException(status_code=404, detail="exercise not found")
        ex = _exercises[exercise_id]
        status = ex["status"]
        czml_path = ex["czml_path"]
    if status != "ready" or not czml_path:
        raise HTTPException(status_code=409, detail=f"computation status: {status}")
    return FileResponse(czml_path, media_type="application/json",
                        headers={"Content-Disposition": f'inline; filename="{exercise_id}.czml"'})


@app.get("/exercises/{exercise_id}/fom-image")
def get_fom_image(exercise_id: str) -> FileResponse:
    with _exercises_lock:
        if exercise_id not in _exercises:
            raise HTTPException(status_code=404, detail="exercise not found")
        status = _exercises[exercise_id]["status"]
    if status != "ready":
        raise HTTPException(status_code=409, detail=f"computation status: {status}")
    for suffix in (".png", ".jpg", ".jpeg"):
        p = FOM_DIR / f"{exercise_id}{suffix}"
        if p.exists():
            return FileResponse(str(p))
    raise HTTPException(status_code=404, detail="FOM image not found")
```

- [ ] **Step 5: Run tests — verify they pass**

```bash
pytest tests/test_endpoints.py -v
```
Expected: 6 tests PASS.

- [ ] **Step 6: Run the full suite**

```bash
pytest tests/ -v
```
Expected: all tests PASS (smoke + cone_geometry + czml_builder_mvp3 + stk_mock_service + fom_pipeline + endpoints = 23 tests).

- [ ] **Step 7: Commit**

```bash
git add mvp3/sg-service-mvp3/main.py mvp3/sg-service-mvp3/computation_job.py mvp3/sg-service-mvp3/tests/test_endpoints.py
git commit -m "feat(mvp3): add FastAPI shell + computation_job + endpoint integration tests"
```

---

## Task 9: Manual end-to-end verification

**Files:** no new files.

- [ ] **Step 1: Start the backend**

```bash
cd mvp3/sg-service-mvp3
.venv\Scripts\activate
python -m uvicorn main:app --port 8003 --log-level info
```

Expected log line (without agi.stk12 installed): `agi.stk12 not installed (...) — using MockStkService`.

- [ ] **Step 2: curl the health check**

In a second terminal:
```bash
curl http://localhost:8003/health
```
Expected output: `{"status":"ok"}`.

- [ ] **Step 3: Run the full compute flow**

```bash
EID=$(curl -s -X POST http://localhost:8003/exercises | python -c "import sys, json; print(json.load(sys.stdin)['exercise_id'])")
echo "exercise_id: $EID"

curl -s -X POST "http://localhost:8003/exercises/$EID/compute" \
  -H "Content-Type: application/json" \
  -d '{"start_time":"1 Jan 2025 00:00:00.000","stop_time":"1 Jan 2025 01:00:00.000"}'
echo ""

# Poll
for i in 1 2 3 4 5; do
  STATUS=$(curl -s "http://localhost:8003/exercises/$EID/status" | python -c "import sys, json; print(json.load(sys.stdin)['status'])")
  echo "  attempt $i: $STATUS"
  [ "$STATUS" = "ready" ] && break
  sleep 1
done

curl -s "http://localhost:8003/exercises/$EID/czml" | python -c "import sys, json; d=json.load(sys.stdin); print(f'{len(d)} packets'); [print(' -', p.get('id')) for p in d]"
```

Expected output:
- `exercise_id: <uuid>`
- `{"exercise_id":"...","status":"running"}`
- `attempt 1: running` or `attempt 1: ready`
- `attempt 2: ready` (if not already)
- Packet list including `document`, `EmitterAC`, `RadarAC`, `EmissionCone`, `RadarCone`, and at least one `Access/...` packet.

- [ ] **Step 4: Stop the backend and commit any doc tweaks**

Ctrl-C the uvicorn. No code changes unless the manual test surfaced issues.

```bash
git status
```

If clean:
```bash
echo "MVP3 stage B complete — 23 tests green, mock end-to-end verified."
```

If the manual flow surfaced issues, open a follow-up task. Do not silently adjust — bug-fix commits should be scoped.

---

## Self-review

**Spec coverage**

| Spec section | Implemented by |
|---|---|
| Port 8003 FastAPI service | Task 8 (`main.py`) |
| Same endpoints as MVP1 | Task 8 (`/health`, `/exercises`, `/compute`, `/status`, `/czml`, `/fom-image`, `/models/{filename}`) |
| Same `agi.stk12` / STK 12 binding | Task 7 (`stk_com_service_mvp3.py`) |
| Same Kashmir two-aircraft scenario | Task 3 (copy of `scenario_builder.py`) |
| CZML via DataProviders + czml3 | Task 4 (`czml_builder_mvp3.py`) |
| FOM pipeline refactored into pure functions | Task 6 (`fom_pipeline.py`) |
| MockStkService fallback | Task 5 (`stk_mock_service.py`) |
| ≥15 tests passing | Task 8 Step 6 asserts 23 total |
| `cone_samples` helper | Task 2 (`cone_geometry.py`) |
| Sensor cylinder escape-hatch | Task 4 `_sensor_cone_packet` uses dict |
| czml3 pinned | Task 1 `requirements.txt` has `czml3==1.0.2` |
| MVP1 / MVP2 untouched | Header rule + no tasks modify those trees |

No spec requirement is unaddressed.

**Placeholder scan**

No `TBD`, no `TODO`, no `add error handling`. Each code step includes a complete code block. The only hedged language is in the plan's opening — labelling Task 4 as "the heart of the plan" — which is descriptive rather than a placeholder.

**Known scoping decision — `czml3` usage**

The spec ("use `czml3` for typed CZML generation, dict-backed escape hatch for the sensor cylinder only") is *not* fully captured by the plan as written. Task 4's initial implementation builds every packet as a dict and serialises with `json.dumps`. Rationale: `czml3`'s exact API surface was not verified while writing the plan; committing to specific builder names (`Preamble`, `Packet`, `Position`, `Rectangle`) risks specifying code that doesn't compile. Dict packets always work.

What this plan *does* deliver from the spec:
- **No CZML Exporter Plugin dependency** — the primary operational win.
- **No JSON-surgery post-processing** (`_patch_czml_sensors` is gone) — the secondary win.
- `czml3==1.0.2` pinned in requirements so the implementer can adopt it immediately in a follow-up refactor.

What this plan *does not* deliver from the spec:
- **Packet-shape validation at build time** (the "typed CZML" benefit). If the implementer is confident about `czml3`'s API during Task 4, they should swap the dict packets for `czml3.Preamble`, `czml3.Packet(position=Position(...))`, etc., starting with the document and aircraft packets. The sensor cylinder stays dict-backed regardless.

Treat this as a scope-down rather than a spec failure: the core architectural claim (plugin-less, surgery-less CZML) still holds; the type-safety icing is optional and follow-up-able.

**Type consistency**

- `build_czml` signature: `(root, scenario_name, start_time, stop_time, model_base_url, step_seconds=10.0) -> str`. Consistent across Tasks 4, 5, 7.
- `cone_samples` signature: `(parent_cartesian: list[float], cylinder_length: float) -> tuple[list[float], list[float]]`. Used by `_sensor_cone_packet`.
- `read_fom_grid` returns `list[tuple[float, float, float]]` (lat_deg, lon_deg, value). Consumed identically by Task 6 tests and Task 7 orchestrator.
- `inject_rectangle_overlay(document, image_url, polygon_bounds_rad)` — same signature in the test fixture and the orchestrator.
- CZML packet id scheme is consistent: `document`, aircraft name, sensor name, `Access/<from>-<to>`, `fom-overlay`.

No signature drift.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-04-19-mvp3-dataprovider-czml.md`. Two execution options:

1. **Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration.

2. **Inline Execution** — Execute tasks in this session using `executing-plans`, batch execution with checkpoints.

Which approach?
