import json

import pytest
from unittest.mock import AsyncMock

from drs_server.timesync.timesync_publisher import TimesyncPublisher
from drs_server.timesync.sync_state_engine import SyncStatus


@pytest.mark.asyncio
async def test_publish_transition_emits_kafka_message():
    producer = AsyncMock()
    publisher = TimesyncPublisher(producer=producer, topic="system.timesync")
    await publisher.publish_transition(
        scope="global",
        old_status=SyncStatus.HEALTHY,
        new_status=SyncStatus.DRIFT_WARN,
        offset_ms=15.0,
    )
    producer.send_and_wait.assert_called_once()
    args, kwargs = producer.send_and_wait.call_args
    assert args[0] == "system.timesync"
    raw = kwargs["value"] if "value" in kwargs else args[1]
    body = json.loads(raw.decode("utf-8"))
    assert body["scope"] == "global"
    assert body["from"] == "healthy"
    assert body["to"] == "drift_warn"
    assert body["offset_ms"] == 15.0
    assert "at" in body
    # at is an ISO-8601 UTC timestamp ending in "Z" (no `+00:00Z`).
    assert body["at"].endswith("Z")
    assert "+00:00" not in body["at"]
