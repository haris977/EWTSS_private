"""Consumes Kafka `drs.control` events and dispatches to SyncStateEngine.

drs-bridge writes `variant.registered` events to this topic when each
variant boots (see drs-bridge/src/drs_bridge/control_publisher.py).
drs-server consumes them so the SyncStateEngine learns about per-variant
precision thresholds and can report per-variant status via
`engine.current_variant_status(variant_name)`.

Other event types on `drs.control` are ignored (forward-compatible:
future events can be added without breaking this consumer).
"""
from __future__ import annotations

import json
import logging
from typing import Protocol

from drs_server.timesync.sync_state_engine import SyncStateEngine

logger = logging.getLogger(__name__)


class KafkaConsumerLike(Protocol):
    async def start(self) -> None: ...
    async def stop(self) -> None: ...
    def __aiter__(self): ...


class ControlConsumer:
    """Wraps a Kafka consumer + dispatch logic. Run via `await consumer.run()`."""

    def __init__(self, consumer: KafkaConsumerLike, engine: SyncStateEngine) -> None:
        self._consumer = consumer
        self._engine = engine

    async def start(self) -> None:
        await self._consumer.start()

    async def stop(self) -> None:
        await self._consumer.stop()

    async def run(self) -> None:
        """Consume forever until cancelled. Parse + dispatch each message.

        Malformed messages are logged + skipped (forward-compatible);
        unrecognised event types are also skipped.
        """
        async for msg in self._consumer:
            try:
                body = json.loads(msg.value.decode("utf-8"))
            except Exception:
                logger.exception("failed to decode drs.control message")
                continue
            if body.get("event") == "variant.registered":
                variant = body.get("variant")
                precision = body.get("precision_required_ms")
                if isinstance(variant, str) and isinstance(precision, (int, float)):
                    self._engine.register_variant(
                        name=variant, precision_required_ms=float(precision)
                    )
                    logger.info("registered variant %s with precision %s ms", variant, precision)
                else:
                    logger.warning("malformed variant.registered: %s", body)
            else:
                logger.debug("ignoring drs.control event: %s", body.get("event"))
