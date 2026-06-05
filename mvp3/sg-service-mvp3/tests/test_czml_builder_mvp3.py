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
    # radius_m is kept in the signature for call-site compatibility but unused —
    # the builder pins the visualisation cylinder length at 20 km regardless.
    del radius_m
    return SimpleNamespace(
        ClassName="Sensor",
        InstanceName=name,
        Pattern=SimpleNamespace(ConeAngle=half_angle_deg),
    )


def _mk_scenario():
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

    access_dp = _mk_dp({
        "Start Time":  ["1 Jan 2025 00:00:00.000"],
        "Stop Time":   ["1 Jan 2025 00:30:00.000"],
    })
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


def test_sensor_packets_use_ion_sdk_shapes():
    """EmissionCone → multiple nested agi_customPatternSensor contours
    (Gaussian antenna lobe). RadarCone → single agi_conicSensor."""
    root = _mk_root()
    data = json.loads(build_czml(root, "MVP3_test",
                                 "1 Jan 2025 00:00:00.000",
                                 "1 Jan 2025 01:00:00.000",
                                 model_base_url="http://localhost:8003/models/"))
    by_id = {p.get("id"): p for p in data}

    # Four nested Gaussian contours for the emitter
    expected_lobe_ids = {
        "EmitterAC/EmissionCone/-3dB",
        "EmitterAC/EmissionCone/-6dB",
        "EmitterAC/EmissionCone/-10dB",
        "EmitterAC/EmissionCone/-20dB",
    }
    assert expected_lobe_ids <= set(by_id.keys()), \
        f"Expected Gaussian contours missing; found: {sorted(by_id)}"
    for lobe_id in expected_lobe_ids:
        pkt = by_id[lobe_id]
        assert "agi_customPatternSensor" in pkt
        assert "agi_conicSensor" not in pkt
        directions = pkt["agi_customPatternSensor"]["directions"]["unitSpherical"]
        assert len(directions) % 2 == 0
        assert len(directions) >= 4

    # Radar still a single agi_conicSensor
    ra = by_id.get("RadarAC/RadarCone")
    assert ra is not None, "RadarCone packet missing"
    assert "agi_conicSensor" in ra
    assert "agi_customPatternSensor" not in ra

    # Only the peak (-3dB) contour draws the intersection line; the others
    # suppress it so the ground doesn't get a stack of overlapping rings.
    peak = by_id["EmitterAC/EmissionCone/-3dB"]
    assert peak["agi_customPatternSensor"]["showIntersection"] is True
    for lobe_id in expected_lobe_ids - {"EmitterAC/EmissionCone/-3dB"}:
        assert by_id[lobe_id]["agi_customPatternSensor"]["showIntersection"] is False

    for pkt in (*[by_id[i] for i in expected_lobe_ids], ra):
        # Position still rides the parent aircraft by reference, but
        # orientation is now an explicit time-dynamic unitQuaternion list
        # (aiming the sensor without the aircraft's heading).
        assert pkt["position"].get("reference", "").endswith("#position")
        ori = pkt["orientation"]
        assert "reference" not in ori, f"{pkt['id']} orientation must be explicit"
        assert isinstance(ori.get("unitQuaternion"), list)
        # Layout: [t, qx, qy, qz, qw] per sample → length divisible by 5.
        assert len(ori["unitQuaternion"]) % 5 == 0
        assert len(ori["unitQuaternion"]) >= 5


def test_access_polyline_present():
    root = _mk_root()
    data = json.loads(build_czml(root, "MVP3_test",
                                 "1 Jan 2025 00:00:00.000",
                                 "1 Jan 2025 01:00:00.000",
                                 model_base_url="http://localhost:8003/models/"))
    access_ids = [p.get("id") for p in data if str(p.get("id", "")).startswith("Access/")]
    assert len(access_ids) >= 1
