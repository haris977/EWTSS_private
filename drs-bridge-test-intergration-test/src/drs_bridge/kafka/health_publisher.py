"""Publishes per-variant health events (tick lag, hardware error etc.) to
Kafka `system.health` per B1.3 design spec §6 step 3.
"""
import json
from typing import Protocol


class KafkaProducerLike(Protocol):
    async def send_and_wait(self, topic: str, value: bytes) -> None: ...


class HealthPublisher:
    def __init__(self, producer: KafkaProducerLike, topic: str = "system.health"):
        self._producer = producer
        self._topic = topic

    async def publish(self, event_type: str, payload: dict) -> None:
        body = {"event": event_type, **payload}
        await self._producer.send_and_wait(
            self._topic, json.dumps(body).encode("utf-8")
        )
