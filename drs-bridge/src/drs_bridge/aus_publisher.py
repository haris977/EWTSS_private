"""Publishes AUS-C2 detection results to Kafka.

Topic:  aus.detections  (overridable via profile kafka_topic)
Key:    variant name as UTF-8 bytes
Value:  normalized JSON from parse_message (UTF-8)

One Kafka message per /system poll cycle — the value is the full
bridge JSON produced by the C++ aus.dll (frame_type, has_danger,
detection_count, detections[], sensors[]).
"""

from __future__ import annotations

import logging
from typing import Protocol

logger = logging.getLogger(__name__)

TOPIC_DETECTIONS = "aus.detections"


class KafkaProducerLike(Protocol):
    async def send_and_wait(self, topic: str, key: bytes, value: bytes) -> None: ...


class AusPublisher:
    def __init__(
        self,
        producer: KafkaProducerLike,
        variant: str = "aus",
        topic: str = TOPIC_DETECTIONS,
    ) -> None:
        self._producer = producer
        self._variant = variant
        self._topic = topic
        self._key = variant.encode("utf-8")

    async def publish(self, json_str: str) -> None:
        """Publish a single normalized JSON string from the C++ DLL."""
        try:
            await self._producer.send_and_wait(
                self._topic,
                key=self._key,
                value=json_str.encode("utf-8"),
            )
            logger.debug("published aus frame variant=%s len=%d", self._variant, len(json_str))
        except Exception:
            logger.exception("failed to publish aus frame variant=%s", self._variant)

    # Backward-compat alias used by existing runtime code
    async def publish_result(self, json_str: str) -> None:
        await self.publish(json_str)
