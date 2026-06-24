"""Publishes decoded dp_ecm frames to the hw.<variant>.telemetry data-plane topics.

Topic naming follows the hw.<variant>.<kind> convention:
  hw.dp_ecm_hf.telemetry — DP-ECM-1071 HF frames
  hw.dp_ecm_vu.telemetry — DP-ECM-1074 VU frames

Each message is keyed by instance_id for per-instance partition affinity
(all frames from the same DRS instance are ordered within one partition).
"""
import json
from typing import Protocol


class KafkaProducerLike(Protocol):
    async def send_and_wait(
        self,
        topic: str,
        value: bytes,
        *,
        key: bytes | None = None,
    ) -> None: ...


class TelemetryPublisher:
    """Publish a single decoded frame envelope to the variant's telemetry topic."""

    def __init__(self, producer: KafkaProducerLike) -> None:
        self._producer = producer

    async def publish(
        self,
        instance_id: str,
        variant: str,
        timestamp_ns: int,
        parsed: dict,
    ) -> None:
        """Publish one parsed frame.

        Args:
            instance_id:  Logical DRS instance id (e.g. 'dp_ecm_hf#1').
            variant:      Parser variant name (e.g. 'dp_ecm_hf').
            timestamp_ns: Wall-clock receipt time, nanoseconds since Unix epoch.
            parsed:       parse_message() output — dict with frame_type/group_id/unit_id
                          plus open per-unit payload fields in SI units.
        """
        topic = f"hw.{variant}.telemetry"
        body = {
            "instance_id": instance_id,
            "variant": variant,
            "timestamp_ns": timestamp_ns,
            "frame": parsed,
        }
        await self._producer.send_and_wait(
            topic,
            json.dumps(body).encode("utf-8"),
            key=instance_id.encode("utf-8"),
        )
