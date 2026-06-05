"""Publishes sync-state transitions to Kafka `system.timesync` topic.

Per B1.3 design spec §9 — every state transition writes a structured event
that Sg.App and DRS webapp consume to update banners / cards.
"""
import json
from datetime import datetime, timezone

from aiokafka import AIOKafkaProducer

from drs_server.timesync.sync_state_engine import SyncStatus


def _utc_now_iso_z() -> str:
    """ISO-8601 UTC timestamp formatted with trailing `Z`, not `+00:00`."""
    return (
        datetime.now(timezone.utc)
        .isoformat(timespec="microseconds")
        .replace("+00:00", "Z")
    )


class TimesyncPublisher:
    def __init__(self, producer: AIOKafkaProducer, topic: str = "system.timesync"):
        self._producer = producer
        self._topic = topic

    async def publish_transition(
        self,
        scope: str,
        old_status: SyncStatus,
        new_status: SyncStatus,
        offset_ms: float,
    ) -> None:
        body = {
            "scope": scope,
            "from": old_status.value,
            "to": new_status.value,
            "offset_ms": offset_ms,
            "at": _utc_now_iso_z(),
        }
        payload = json.dumps(body).encode("utf-8")
        await self._producer.send_and_wait(self._topic, value=payload)
