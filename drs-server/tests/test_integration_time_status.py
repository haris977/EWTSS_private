"""Integration test: boots the FastAPI app via lifespan with a mocked
NtpMonitor.sample + AsyncMock Kafka producer, then hits /time/status
and verifies the engine actually drove a real response.
"""
from datetime import datetime, timezone
from unittest.mock import AsyncMock

import pytest
from httpx import ASGITransport, AsyncClient
from fastapi import FastAPI

from drs_server.api import time_status
from drs_server.lifespan import make_lifespan
from drs_server.timesync.ntp_monitor import NtpSample


@pytest.mark.asyncio
async def test_time_status_returns_engine_state_after_lifespan_boot(monkeypatch):
    fake_sample = NtpSample(
        offset_ms=0.4,
        jitter_ms=0.2,
        stratum=2,
        sampled_at=datetime.now(timezone.utc),
        peer="WS1-SG.local",
    )

    # Patch NtpMonitor.sample so we don't run real ntpq.
    monkeypatch.setattr(
        "drs_server.timesync.ntp_monitor.NtpMonitor.sample",
        AsyncMock(return_value=fake_sample),
    )

    producer = AsyncMock()

    # Minimal Kafka consumer fake — never yields, blocks until cancelled.
    class _FakeConsumer:
        async def start(self) -> None: pass
        async def stop(self) -> None: pass
        def __aiter__(self): return self
        async def __anext__(self):
            import asyncio
            await asyncio.sleep(3600)
            raise StopAsyncIteration

    app = FastAPI(lifespan=make_lifespan(
        ntpq_path="dummy",
        kafka_bootstrap="dummy:9092",
        poll_seconds=1,
        producer_factory=lambda bootstrap: producer,
        consumer_factory=lambda bootstrap: _FakeConsumer(),
    ))
    app.include_router(time_status.router)

    transport = ASGITransport(app=app)
    # ASGITransport does not auto-trigger the FastAPI lifespan in httpx 0.28.x,
    # so we drive it explicitly via app.router.lifespan_context. This is the
    # same pattern used by the existing lifespan unit tests, and it wires
    # app.state before the client makes any requests.
    async with app.router.lifespan_context(app):
        async with AsyncClient(transport=transport, base_url="http://test") as client:
            response = await client.get("/time/status")
            assert response.status_code == 200
            body = response.json()
            assert body["ntp_offset_ms"] == pytest.approx(0.4)
            assert body["ntp_jitter_ms"] == pytest.approx(0.2)
            assert body["ntp_peer"] == "WS1-SG.local"
            # status starts as WARMING per SyncStateEngine's initial state
            # (one or two samples isn't enough to transition to HEALTHY).
            assert body["status"] in ("warming", "healthy")
            assert "current_time" in body
            assert "last_sync" in body

    producer.start.assert_awaited_once()
    producer.stop.assert_awaited_once()
