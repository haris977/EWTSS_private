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
