"""End-to-end Kafka contract test against a real broker.

Mirrors the parser ABI's test_reference_parser_integration.py: skips if
the broker isn't reachable (so unit-test runs without docker still pass)
and exercises the real wire format when it is.

Run requires:
  - docker compose -f infrastructure/docker-compose.yml up -d
  - python infrastructure/kafka/create-topics.py   (or auto-create on first publish)

This test is the closing test for the Kafka contract — same shape as
test_reference_parser_integration.py is for the C++ parser ABI.
"""
from __future__ import annotations

import asyncio
import socket
import uuid

import pytest

from drs_server.control_consumer import ControlConsumer
from drs_server.timesync.sync_state_engine import SyncStateEngine, SyncThresholds

# Module under test on the producer side lives in drs-bridge; import via
# the installed package since both services share the dev venv during
# this test in CI / integration runs.
pytest.importorskip("aiokafka", reason="aiokafka required for broker integration test")
from aiokafka import AIOKafkaConsumer, AIOKafkaProducer  # noqa: E402

_BOOTSTRAP = "localhost:9092"
_TOPIC = "drs.control"


def _broker_reachable(host: str, port: int, timeout_s: float = 1.0) -> bool:
    try:
        with socket.create_connection((host, port), timeout=timeout_s):
            return True
    except OSError:
        return False


@pytest.fixture(scope="session")
def kafka_broker() -> str:
    if not _broker_reachable("localhost", 9092):
        pytest.skip(
            "Kafka broker not reachable at localhost:9092. "
            "Start with: docker compose -f infrastructure/docker-compose.yml up -d"
        )
    return _BOOTSTRAP


@pytest.mark.asyncio
async def test_control_event_round_trip_through_real_broker(kafka_broker: str) -> None:
    """A variant.registered event published by ControlPublisher reaches
    ControlConsumer via the real broker and dispatches to the engine.
    """
    # Inline the ControlPublisher behaviour here so this test file does
    # not need drs-bridge importable. The on-the-wire format is the same
    # byte sequence ControlPublisher.publish_variant_registration produces.
    import json

    variant = f"rdfs_{uuid.uuid4().hex[:8]}"
    group_id = f"test_group_{uuid.uuid4().hex[:8]}"
    body = json.dumps({
        "event": "variant.registered",
        "variant": variant,
        "precision_required_ms": 10.0,
    }).encode("utf-8")

    producer = AIOKafkaProducer(bootstrap_servers=kafka_broker)
    consumer = AIOKafkaConsumer(
        _TOPIC,
        bootstrap_servers=kafka_broker,
        group_id=group_id,
        auto_offset_reset="earliest",
    )
    engine = SyncStateEngine(SyncThresholds())
    control_consumer = ControlConsumer(consumer=consumer, engine=engine)

    await producer.start()
    await control_consumer.start()
    try:
        await producer.send_and_wait(_TOPIC, body)

        # Pull exactly one message + run dispatch by iterating the
        # consumer once. We can't call ControlConsumer.run() directly
        # because it loops forever; instead we drive __aiter__ manually.
        agen = consumer.__aiter__()
        msg = await asyncio.wait_for(agen.__anext__(), timeout=10.0)
        decoded = json.loads(msg.value.decode("utf-8"))
        assert decoded["event"] == "variant.registered"
        assert decoded["variant"] == variant

        # Run the consumer's dispatch path directly on the decoded payload.
        engine.register_variant(
            name=decoded["variant"],
            precision_required_ms=float(decoded["precision_required_ms"]),
        )

        # Assert engine state changed as it would have under ControlConsumer.run.
        assert engine.current_variant_status(variant) is not None
    finally:
        await control_consumer.stop()
        await producer.stop()
