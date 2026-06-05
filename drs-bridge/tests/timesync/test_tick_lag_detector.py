import pytest
from unittest.mock import AsyncMock
from datetime import datetime, timedelta, timezone

from drs_bridge.timesync.tick_lag_detector import TickLagDetector


@pytest.mark.asyncio
async def test_no_warning_when_lag_under_threshold():
    publisher = AsyncMock()
    detector = TickLagDetector(
        publisher=publisher,
        warn_threshold_ms=100,
        alert_threshold_ms=500,
        consecutive_required=5,
    )
    exercise_start = datetime.now(timezone.utc)
    interval_ms = 1000
    for t in range(10):
        intended = exercise_start + timedelta(milliseconds=t * interval_ms)
        consumed = intended + timedelta(milliseconds=20)  # only 20 ms lag
        await detector.record_tick(tick=t, intended=intended, consumed=consumed)
    publisher.publish.assert_not_called()


@pytest.mark.asyncio
async def test_warning_after_five_consecutive_over_warn_threshold():
    publisher = AsyncMock()
    detector = TickLagDetector(
        publisher=publisher,
        warn_threshold_ms=100,
        alert_threshold_ms=500,
        consecutive_required=5,
    )
    exercise_start = datetime.now(timezone.utc)
    interval_ms = 1000
    for t in range(5):
        intended = exercise_start + timedelta(milliseconds=t * interval_ms)
        consumed = intended + timedelta(milliseconds=150)  # 150 ms > 100 warn
        await detector.record_tick(tick=t, intended=intended, consumed=consumed)
    publisher.publish.assert_called_once()
    args, kwargs = publisher.publish.call_args
    assert args[0] == "tick.lag.warning"


@pytest.mark.asyncio
async def test_alert_after_five_consecutive_over_alert_threshold():
    publisher = AsyncMock()
    detector = TickLagDetector(
        publisher=publisher,
        warn_threshold_ms=100,
        alert_threshold_ms=500,
        consecutive_required=5,
    )
    exercise_start = datetime.now(timezone.utc)
    interval_ms = 1000
    for t in range(5):
        intended = exercise_start + timedelta(milliseconds=t * interval_ms)
        consumed = intended + timedelta(milliseconds=600)  # 600 ms > 500 alert
        await detector.record_tick(tick=t, intended=intended, consumed=consumed)
    # Both warning and alert should fire (or just alert depending on semantics).
    # Verify alert fired at least once.
    alert_calls = [c for c in publisher.publish.call_args_list if c.args[0] == "tick.lag.alert"]
    assert len(alert_calls) >= 1
