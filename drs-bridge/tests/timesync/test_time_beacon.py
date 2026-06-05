import asyncio

import pytest
from unittest.mock import AsyncMock, MagicMock

from drs_bridge.timesync.time_beacon import TimeBeaconCoroutine


@pytest.mark.asyncio
async def test_beacon_sends_at_configured_interval():
    sender = AsyncMock()
    parser = MagicMock()
    parser.format_response.return_value = b"beacon_bytes"

    beacon = TimeBeaconCoroutine(
        variant="rdfs",
        parser=parser,
        sender=sender,
        interval_ms=50,
    )
    task = asyncio.create_task(beacon.run())
    await asyncio.sleep(0.18)  # allow ~3 ticks at 50 ms
    task.cancel()
    try:
        await task
    except asyncio.CancelledError:
        pass

    # Allow ±1 tick for scheduling jitter (expect ~3-4 sends in 180 ms).
    assert 2 <= sender.send.await_count <= 4
    # Every send was preceded by a parser.format_response(kind="time", ...) call.
    assert parser.format_response.call_count == sender.send.await_count
    last_call_kwargs = parser.format_response.call_args.kwargs
    assert last_call_kwargs["kind"] == "time"
    assert isinstance(last_call_kwargs["timestamp_ns"], int)


@pytest.mark.asyncio
async def test_beacon_stops_on_cancel():
    sender = AsyncMock()
    parser = MagicMock()
    parser.format_response.return_value = b"x"

    beacon = TimeBeaconCoroutine("rdfs", parser, sender, interval_ms=10)
    task = asyncio.create_task(beacon.run())
    await asyncio.sleep(0.05)
    task.cancel()
    try:
        await task
    except asyncio.CancelledError:
        pass
    initial_count = sender.send.await_count
    await asyncio.sleep(0.05)
    # No additional sends after cancel
    assert sender.send.await_count == initial_count
