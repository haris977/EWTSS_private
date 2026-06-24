import json
from types import SimpleNamespace

import pytest

from drs_bridge.kafka.roster_consumer import RosterConsumer
from drs_bridge.profiles._schema import Roster


class _FakeConsumer:
    """Mimics aiokafka's start/stop + async iteration over messages."""

    def __init__(self, messages):
        self._messages = messages
        self.started = False
        self.stopped = False

    async def start(self):
        self.started = True

    async def stop(self):
        self.stopped = True

    async def __aiter__(self):
        for m in self._messages:
            yield m


def _msg(body: dict):
    return SimpleNamespace(value=json.dumps(body).encode("utf-8"))


def _snapshot(version=1):
    return {
        "roster_id": "lab-full",
        "version": version,
        "entries": [],
    }


@pytest.mark.asyncio
async def test_consumer_decodes_and_invokes_callback():
    received: list[Roster] = []
    consumer = RosterConsumer(
        _FakeConsumer([_msg(_snapshot(1)), _msg(_snapshot(2))]),
        on_roster=lambda r: received.append(r),
    )
    await consumer.run()
    assert [r.version for r in received] == [1, 2]
    assert received[-1].etag == "lab-full@2"


@pytest.mark.asyncio
async def test_consumer_skips_malformed_message():
    received = []
    bad = SimpleNamespace(value=b"not json")
    consumer = RosterConsumer(
        _FakeConsumer([bad, _msg(_snapshot(5))]),
        on_roster=lambda r: received.append(r),
    )
    await consumer.run()  # must not raise
    assert [r.version for r in received] == [5]
