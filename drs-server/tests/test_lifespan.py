from unittest.mock import AsyncMock

import pytest
from fastapi import FastAPI

from drs_server.lifespan import make_lifespan


class _FakeKafkaConsumer:
    """Minimal async-iterable fake for tests that don't exercise the consumer."""

    def __init__(self, bootstrap: str) -> None:  # noqa: ARG002
        pass

    async def start(self) -> None:
        pass

    async def stop(self) -> None:
        pass

    def __aiter__(self):
        return self

    async def __anext__(self):
        # Block until cancelled; never actually yields a message.
        import asyncio
        await asyncio.sleep(3600)
        raise StopAsyncIteration


def _fake_consumer_factory(bootstrap: str) -> _FakeKafkaConsumer:
    return _FakeKafkaConsumer(bootstrap)


@pytest.mark.asyncio
async def test_lifespan_wires_app_state_and_starts_producer():
    producer = AsyncMock()
    app = FastAPI(lifespan=make_lifespan(
        ntpq_path="dummy",
        kafka_bootstrap="dummy:9092",
        poll_seconds=1,
        producer_factory=lambda bootstrap: producer,
        consumer_factory=_fake_consumer_factory,
    ))
    async with app.router.lifespan_context(app):
        assert app.state.ntp_monitor is not None
        assert app.state.sync_state_engine is not None
        assert app.state.kafka_producer is producer
        assert app.state.timesync_publisher is not None
        producer.start.assert_awaited_once()
    producer.stop.assert_awaited_once()


@pytest.mark.asyncio
async def test_lifespan_starts_and_cancels_poll_task():
    producer = AsyncMock()
    poll_task_ref = {}

    async def fake_poll(monitor, engine, interval_s):
        poll_task_ref["called"] = True
        try:
            import asyncio
            while True:
                await asyncio.sleep(0.01)
        except Exception:
            raise

    app = FastAPI(lifespan=make_lifespan(
        ntpq_path="dummy",
        kafka_bootstrap="dummy:9092",
        poll_seconds=1,
        producer_factory=lambda bootstrap: producer,
        poll_loop_impl=fake_poll,
        consumer_factory=_fake_consumer_factory,
    ))
    async with app.router.lifespan_context(app):
        import asyncio
        await asyncio.sleep(0.02)
        assert poll_task_ref.get("called") is True
    producer.stop.assert_awaited_once()


@pytest.mark.asyncio
async def test_engine_transition_callback_dispatches_to_publisher():
    """The lifespan must wire a sync->async bridge so engine transitions
    fire publisher.publish_transition as a fire-and-forget task."""
    import asyncio

    producer = AsyncMock()

    app = FastAPI(lifespan=make_lifespan(
        ntpq_path="dummy",
        kafka_bootstrap="dummy:9092",
        poll_seconds=1,
        producer_factory=lambda bootstrap: producer,
        consumer_factory=_fake_consumer_factory,
    ))
    async with app.router.lifespan_context(app):
        engine = app.state.sync_state_engine
        publisher = app.state.timesync_publisher
        publisher.publish_transition = AsyncMock()

        from drs_server.timesync.sync_state_engine import SyncStatus
        await engine._transition_to(SyncStatus.HEALTHY)
        await asyncio.sleep(0.02)

        publisher.publish_transition.assert_awaited()
        kwargs = publisher.publish_transition.call_args.kwargs
        assert kwargs["scope"] == "global"
        assert kwargs["new_status"] == SyncStatus.HEALTHY
