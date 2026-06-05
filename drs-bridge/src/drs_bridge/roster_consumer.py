"""Consumes the compacted Kafka `drs.roster` topic and hands each active-roster
snapshot to a callback. drs-bridge's first Kafka consumer.

The topic is compacted with a single key ('active'), so a fresh consumer
reading from earliest always sees the latest retained snapshot first, then
tails live updates. Each message is a FULL snapshot (no deltas).
"""

from __future__ import annotations

import json
import logging
from typing import Awaitable, Callable, Protocol, Union

from drs_bridge.profiles._schema import Roster

logger = logging.getLogger(__name__)

OnRoster = Callable[[Roster], Union[None, Awaitable[None]]]


class KafkaConsumerLike(Protocol):
    async def start(self) -> None: ...
    async def stop(self) -> None: ...
    def __aiter__(self): ...


class RosterConsumer:
    def __init__(self, consumer: KafkaConsumerLike, on_roster: OnRoster) -> None:
        self._consumer = consumer
        self._on_roster = on_roster

    async def start(self) -> None:
        await self._consumer.start()

    async def stop(self) -> None:
        await self._consumer.stop()

    async def run(self) -> None:
        """Consume until cancelled. Malformed/invalid snapshots are logged and
        skipped (forward-compatible; never crash the consumer loop)."""
        async for msg in self._consumer:
            try:
                body = json.loads(msg.value.decode("utf-8"))
                roster = Roster(**body)
            except Exception:
                logger.exception("failed to decode/validate drs.roster message; skipping")
                continue
            result = self._on_roster(roster)
            if result is not None:
                await result
            logger.info("applied roster snapshot %s (%d entries)", roster.etag, len(roster.entries))
