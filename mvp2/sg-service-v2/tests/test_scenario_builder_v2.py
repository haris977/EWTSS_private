"""
Tests for build_stk_scenario_v2 against the agi.stk13 COM API shape.
All STK objects are mocked — no real STK Engine required.
"""
from unittest.mock import MagicMock, patch, ANY
import pytest
from scenario_builder_v2 import build_stk_scenario_v2

START = "1 Jan 2025 00:00:00.000"
STOP  = "1 Jan 2025 01:00:00.000"


def _make_root():
    """Build a MagicMock that mimics the agi.stk13 root object (PascalCase API)."""
    root = MagicMock()
    sc   = MagicMock()
    root.CurrentScenario = sc
    sc.Children.New.return_value = MagicMock()
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

    cmd_args = [str(c) for c in root.ExecuteCommand.call_args_list]
    assert any("SetState */AreaTarget/Zone1 PatternOld" in str(c) for c in cmd_args), \
        f"No SetState command found. Calls: {cmd_args}"
    # Verify Lat/Lon order (STK expects Lat first)
    cmd = next(c for c in cmd_args if "SetState */AreaTarget/Zone1" in c)
    assert "Lat 33.5" in cmd and "Lon 73.5" in cmd


def test_aircraft_waypoint_speed_and_altitude_in_km():
    """STK's native distance/speed units are km/km-per-sec — convert from m/s and m."""
    root, sc = _make_root()
    aircraft_mock = MagicMock()
    sc.Children.New.return_value = aircraft_mock
    wp_mock = MagicMock()
    aircraft_mock.Route.Waypoints.Add.return_value = wp_mock

    entities = [{
        "type": "Feature",
        "geometry": {"type": "LineString",
                     "coordinates": [[74.0, 34.0, 1900.0], [75.0, 34.0, 1900.0]]},
        "properties": {"entityType": "Aircraft", "name": "AC1", "speedMs": 150.0},
    }]
    with patch("scenario_builder_v2._import_stk_types"):
        build_stk_scenario_v2(root, "eid1", START, STOP, entities)

    # 150 m/s == 0.15 km/s
    assert abs(wp_mock.Speed - 0.15) < 1e-6
    # 1900 m == 1.9 km
    assert abs(wp_mock.Altitude - 1.9) < 1e-6


def test_facility_altitude_converted_to_km():
    root, sc = _make_root()
    fac_mock = MagicMock()
    sc.Children.New.return_value = fac_mock

    entities = [{
        "type": "Feature",
        "geometry": {"type": "Point", "coordinates": [74.5, 34.5, 500.0]},
        "properties": {"entityType": "Facility", "name": "Site1"},
    }]
    with patch("scenario_builder_v2._import_stk_types"):
        build_stk_scenario_v2(root, "eid1", START, STOP, entities)

    # AssignPlanetodetic(lat, lon, alt_km) — 500 m becomes 0.5 km.
    fac_mock.Position.AssignPlanetodetic.assert_called_once_with(34.5, 74.5, 0.5)


def test_sensor_attached_to_parent():
    root, sc = _make_root()
    aircraft_mock = MagicMock()
    sensor_mock   = MagicMock()
    def _new_side(obj_type, name):
        if "AIRCRAFT" in str(obj_type).upper() or name == "AC1":
            return aircraft_mock
        return sensor_mock
    sc.Children.New.side_effect = _new_side

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

    aircraft_mock.Children.New.assert_called_once_with(ANY, "Snsr1")
    # sensor is aircraft_mock.Children.New.return_value (the parent's child factory).
    aircraft_mock.Children.New.return_value.SetPatternType.assert_called_once()


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

    names_created = [str(c) for c in sc.Children.New.call_args_list]
    assert any("Cov_Zone1" in n for n in names_created), \
        f"No CoverageDefinition created. Calls: {names_created}"
