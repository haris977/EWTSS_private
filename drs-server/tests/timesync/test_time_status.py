from datetime import datetime
from unittest.mock import AsyncMock

from fastapi import FastAPI
from fastapi.testclient import TestClient

from drs_server.api.time_status import router
from drs_server.timesync.ntp_monitor import NtpSample
from drs_server.timesync.sync_state_engine import SyncStatus


def make_app() -> FastAPI:
    app = FastAPI()
    monitor = AsyncMock()
    monitor.sample.return_value = NtpSample(
        offset_ms=0.4,
        jitter_ms=0.2,
        stratum=2,
        sampled_at=datetime(2026, 5, 14, 12, 34, 56),
        peer="WS1-SG.local",
    )
    engine = AsyncMock()
    engine.current_status.return_value = SyncStatus.HEALTHY
    app.state.ntp_monitor = monitor
    app.state.sync_state_engine = engine
    app.include_router(router)
    return app


def test_get_time_status_returns_healthy_when_offset_under_threshold():
    client = TestClient(make_app())
    response = client.get("/time/status")
    assert response.status_code == 200
    body = response.json()
    assert body["status"] == "healthy"
    assert body["ntp_offset_ms"] == 0.4
    assert body["ntp_jitter_ms"] == 0.2
    assert body["ntp_peer"] == "WS1-SG.local"
    assert "current_time" in body
    assert "last_sync" in body
