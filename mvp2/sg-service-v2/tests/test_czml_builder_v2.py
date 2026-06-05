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


def test_empty_entities_returns_only_document_packet():
    packets = build_synthetic_czml("eid2", START, STOP, [])
    assert len(packets) == 1
    assert packets[0]["id"] == "document"


def test_single_waypoint_aircraft_produces_two_samples():
    single_wp = [{
        "type": "Feature",
        "geometry": {"type": "LineString", "coordinates": [[74.0, 34.0, 1900.0]]},
        "properties": {"entityType": "Aircraft", "name": "AC_Static", "speedMs": 0.0},
    }]
    packets = build_synthetic_czml("eid3", START, STOP, single_wp)
    ac = next(p for p in packets if "Aircraft/AC_Static" in p.get("id", ""))
    cart = ac["position"]["cartesian"]
    # Single waypoint → two samples: [0, x, y, z, duration, x, y, z]
    assert len(cart) == 8
    assert cart[0] == 0.0
    assert cart[4] == 3600.0   # duration of 1-hour scenario
    # Both samples at same position
    assert cart[1:4] == cart[5:8]
