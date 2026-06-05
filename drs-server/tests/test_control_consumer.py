import json
from unittest.mock import AsyncMock, MagicMock

import pytest

from drs_server.control_consumer import ControlConsumer
from drs_server.timesync.sync_state_engine import SyncStateEngine, SyncThresholds


class _FakeMessage:
    def __init__(self, value: bytes) -> None:
        self.value = value


class _FakeKafkaConsumer:
    """Async iterator yielding the given messages, then stops."""

    def __init__(self, messages: list[bytes]) -> None:
        self._messages = [_FakeMessage(m) for m in messages]
        self.started = False
        self.stopped = False

    async def start(self) -> None:
        self.started = True

    async def stop(self) -> None:
        self.stopped = True

    def __aiter__(self):
        return self._iterate()

    async def _iterate(self):
        for m in self._messages:
            yield m


@pytest.mark.asyncio
async def test_variant_registered_event_calls_register_variant():
    msg = json.dumps({
        "event": "variant.registered",
        "variant": "rdfs",
        "precision_required_ms": 10.0,
    }).encode("utf-8")
    consumer = _FakeKafkaConsumer([msg])
    engine = SyncStateEngine(SyncThresholds())

    cc = ControlConsumer(consumer=consumer, engine=engine)
    await cc.start()
    await cc.run()
    await cc.stop()

    assert consumer.started is True
    assert consumer.stopped is True
    # The engine now knows about the variant.
    status = await engine.current_variant_status("rdfs")
    assert status.value in {"healthy", "warming", "drift_warn", "drift_alert", "sync_lost"}


@pytest.mark.asyncio
async def test_malformed_message_is_skipped_not_fatal():
    bad = b"\x00\xff\xfenot-valid-json"
    good = json.dumps({
        "event": "variant.registered", "variant": "iff", "precision_required_ms": 5.0,
    }).encode("utf-8")
    consumer = _FakeKafkaConsumer([bad, good])
    engine = SyncStateEngine(SyncThresholds())

    cc = ControlConsumer(consumer=consumer, engine=engine)
    await cc.run()

    # The good message was processed despite the bad one preceding it.
    status = await engine.current_variant_status("iff")
    assert status is not None


@pytest.mark.asyncio
async def test_unknown_event_type_is_ignored():
    msg = json.dumps({
        "event": "some.future.event", "variant": "iff",
    }).encode("utf-8")
    consumer = _FakeKafkaConsumer([msg])
    engine = SyncStateEngine(SyncThresholds())

    cc = ControlConsumer(consumer=consumer, engine=engine)
    await cc.run()

    # The variant was never registered, so querying it raises.
    with pytest.raises(KeyError):
        await engine.current_variant_status("iff")
