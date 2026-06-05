"""
All tests use MockStkService — ansys.stk not required.
PySTK import is blocked via the session-scoped fixture in conftest.py.
"""
import time

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
    assert r.status_code == 200, r.text
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
    assert r.status_code == 200, r.text
    packets = r.json()
    assert packets[0]["id"] == "document"
