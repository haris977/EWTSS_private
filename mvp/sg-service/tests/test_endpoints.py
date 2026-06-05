"""
Integration tests for the FastAPI endpoints.
All tests run with MockStkService (agi.stk12 is not required).
"""
import sys
import time

# Force MockStkService by making agi.stk12 unimportable before main imports it
sys.modules.setdefault("agi", None)        # type: ignore[assignment]
sys.modules.setdefault("agi.stk12", None)  # type: ignore[assignment]

from fastapi.testclient import TestClient  # noqa: E402
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

    # Poll until ready (MockStkService is fast — should finish within 10s)
    deadline = time.time() + 15
    while time.time() < deadline:
        r = client.get(f"/exercises/{eid}/status")
        if r.json()["status"] == "ready":
            break
        time.sleep(0.2)

    assert r.json()["status"] == "ready", f"timed out, status={r.json()['status']}"


def test_czml_endpoint_returns_valid_json():
    # Create + compute
    r = client.post("/exercises", json={"name": "CZML Test"})
    eid = r.json()["exercise_id"]
    client.post(f"/exercises/{eid}/compute")

    # Wait for ready
    deadline = time.time() + 15
    while time.time() < deadline:
        if client.get(f"/exercises/{eid}/status").json()["status"] == "ready":
            break
        time.sleep(0.2)

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
