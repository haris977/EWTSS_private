"""Pattern-2 periodic time-distribution coroutine.

Per B1.3 design spec §5.1 Pattern 2. Reads NTP-synced wall-clock, asks the
variant's parser to format a time-signal message via the existing format_response
ABI (kind="time"), and sends to the Entity Controller. Variants whose IRS
doesn't require periodic distribution skip this coroutine entirely.
"""
import asyncio
import time
from typing import Protocol


class Parser(Protocol):
    def format_response(self, kind: str, timestamp_ns: int, **kwargs) -> bytes: ...


class Sender(Protocol):
    async def send(self, payload: bytes) -> None: ...


class TimeBeaconCoroutine:
    def __init__(
        self,
        variant: str,
        parser: Parser,
        sender: Sender,
        interval_ms: int,
    ):
        self._variant = variant
        self._parser = parser
        self._sender = sender
        self._interval_s = interval_ms / 1000.0

    async def run(self) -> None:
        while True:
            timestamp_ns = time.time_ns()
            payload = self._parser.format_response(
                kind="time", timestamp_ns=timestamp_ns
            )
            await self._sender.send(payload)
            await asyncio.sleep(self._interval_s)
