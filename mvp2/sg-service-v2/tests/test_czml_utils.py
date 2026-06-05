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
    assert 'orientation' in sensor_pkt
    assert 'cartesian' in sensor_pkt.get('position', {})


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
