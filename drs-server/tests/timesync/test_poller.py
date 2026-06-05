import asyncio
from datetime import datetime, timezone

import pytest
from unittest.mock import AsyncMock, MagicMock

from drs_server.timesync.ntp_monitor import NtpSample
from drs_server.timesync.poller import poll_loop


def _sample(offset_ms: float = 0.5) -> NtpSample:
    return NtpSample(
        offset_ms=offset_ms,
        jitter_ms=0.1,
        stratum=2,
        sampled_at=datetime.now(timezone.utc),
        peer="WS1-SG",
    )


@pytest.mark.asyncio
async def test_poll_loop_records_samples_then_exits_on_cancel():
    monitor = MagicMock()
    monitor.sample = AsyncMock(return_value=_sample())
    engine = AsyncMock()

    task = asyncio.create_task(poll_loop(monitor, engine, interval_s=0.02))
    await asyncio.sleep(0.08)
    task.cancel()
    try:
        await task
    except asyncio.CancelledError:
        pass

    assert monitor.sample.await_count >= 2
    assert engine.record.await_count == monitor.sample.await_count


@pytest.mark.asyncio
async def test_poll_loop_swallows_monitor_exceptions_and_keeps_going():
    monitor = MagicMock()
    monitor.sample = AsyncMock(side_effect=[RuntimeError("ntpq failed"), _sample(), _sample()])
    engine = AsyncMock()

    task = asyncio.create_task(poll_loop(monitor, engine, interval_s=0.02))
    await asyncio.sleep(0.10)
    task.cancel()
    try:
        await task
    except asyncio.CancelledError:
        pass

    assert engine.record.await_count >= 1


@pytest.mark.asyncio
async def test_poll_loop_exits_immediately_when_cancelled_during_sleep():
    monitor = MagicMock()
    monitor.sample = AsyncMock(return_value=_sample())
    engine = AsyncMock()

    task = asyncio.create_task(poll_loop(monitor, engine, interval_s=1.0))
    await asyncio.sleep(0.05)
    task.cancel()
    try:
        await task
    except asyncio.CancelledError:
        pass

    assert task.done()
