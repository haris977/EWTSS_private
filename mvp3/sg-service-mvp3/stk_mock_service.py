"""Synthetic STK stand-in.

Builds a lightweight object graph that looks like an agi.stk12 scenario root
and feeds it into ``czml_builder_mvp3.build_czml`` — so the live CZML pipeline
is exercised even when STK is not installed.
"""
from __future__ import annotations
import logging
import math
from pathlib import Path
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
        p = Path(output_path)
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(czml, encoding="utf-8")
        log.info("MockStkService: CZML written to %s (%d bytes)", p, p.stat().st_size)


# ---------------- Synthetic root builder ----------------

def _make_synthetic_root(start_time: str, stop_time: str):
    """Build a SimpleNamespace tree that mimics just enough of the STK API."""
    n = 61  # one sample per minute across the default one-hour scenario
    times = [float(i * 60.0) for i in range(n)]

    emitter_positions = _generate_track(34.2, 73.9, 34.0, 75.4, 1.9, n)
    radar_positions   = _generate_track(34.6, 75.9, 34.2, 75.2, 4.675, n)

    emitter = _synthetic_aircraft("EmitterAC", times, emitter_positions,
                                  sensor_name="EmissionCone", half_angle=45.0)
    radar   = _synthetic_aircraft("RadarAC",   times, radar_positions,
                                  sensor_name="RadarCone",    half_angle=25.0)

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
