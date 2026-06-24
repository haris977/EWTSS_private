"""Publishes drs-bridge -> drs-server control events (variant registration etc.)
to Kafka `drs.control` per B1.3 design spec §6.
"""
import json
from typing import Protocol


class KafkaProducerLike(Protocol):
    async def send_and_wait(self, topic: str, value: bytes) -> None: ...


class ControlPublisher:
    def __init__(self, producer: KafkaProducerLike, topic: str = "drs.control"):
        self._producer = producer
        self._topic = topic

    async def publish_variant_registration(
        self,
        variant: str,
        precision_required_ms: float,
    ) -> None:
        body = {
            "event": "variant.registered",
            "variant": variant,
            "precision_required_ms": precision_required_ms,
        }
        await self._producer.send_and_wait(
            self._topic, json.dumps(body).encode("utf-8")
        )
