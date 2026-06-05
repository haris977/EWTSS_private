"""FastAPI lifespan handler that wires the time-sync subsystem.

Replaces the deprecated `@app.on_event("startup"/"shutdown")` pattern.
Use via `app = FastAPI(lifespan=make_lifespan(...))`.

Factory parameters exist so the integration test can inject mock
Kafka producers / fake poll loops without touching real infrastructure.
"""
from __future__ import annotations

import asyncio
import logging
from contextlib import asynccontextmanager
from typing import AsyncIterator, Callable

from fastapi import FastAPI

from drs_server.control_consumer import ControlConsumer
from drs_server.timesync.ntp_monitor import NtpMonitor
from drs_server.timesync.poller import poll_loop as _real_poll_loop
from drs_server.timesync.sync_state_engine import SyncStateEngine, SyncStatus, SyncThresholds
from drs_server.timesync.timesync_publisher import TimesyncPublisher

logger = logging.getLogger(__name__)


def _default_producer_factory(bootstrap: str):
    # Imported lazily so unit tests that mock the factory don't pay aiokafka import cost.
    from aiokafka import AIOKafkaProducer
    return AIOKafkaProducer(bootstrap_servers=bootstrap)


def _default_consumer_factory(bootstrap: str):
    # Imported lazily so unit tests that mock the factory don't pay aiokafka import cost.
    from aiokafka import AIOKafkaConsumer
    return AIOKafkaConsumer(
        "drs.control",
        bootstrap_servers=bootstrap,
        group_id="drs-server",
        auto_offset_reset="earliest",
    )


def make_lifespan(
    ntpq_path: str,
    kafka_bootstrap: str,
    poll_seconds: int,
    producer_factory: Callable | None = None,
    poll_loop_impl: Callable | None = None,
    consumer_factory: Callable | None = None,
):
    """Returns an @asynccontextmanager-compatible lifespan for FastAPI."""
    factory = producer_factory or _default_producer_factory
    loop_impl = poll_loop_impl or _real_poll_loop
    consumer_factory_resolved = consumer_factory or _default_consumer_factory

    @asynccontextmanager
    async def lifespan(app: FastAPI) -> AsyncIterator[None]:
        logger.info("drs-server lifespan starting (ntpq=%s kafka=%s poll=%ss)",
                    ntpq_path, kafka_bootstrap, poll_seconds)

        monitor = NtpMonitor(ntpq_path=ntpq_path)
        engine = SyncStateEngine(thresholds=SyncThresholds())
        producer = factory(kafka_bootstrap)
        await producer.start()
        publisher = TimesyncPublisher(producer=producer, topic="system.timesync")

        # Sync -> async bridge: engine callbacks are sync; we schedule the
        # publisher call as a fire-and-forget task so the engine isn't
        # blocked on Kafka I/O.
        def _on_transition(scope: str, old: SyncStatus, new: SyncStatus) -> None:
            # offset_ms is the latest sample's offset; engine doesn't expose
            # it directly via the callback, so we pull the most recent
            # window entry. If empty (first transition), use 0.0.
            try:
                latest = engine._window[-1] if engine._window else None  # type: ignore[attr-defined]
                offset_ms = float(latest.offset_ms) if latest is not None else 0.0
            except Exception:
                offset_ms = 0.0
            asyncio.create_task(
                publisher.publish_transition(
                    scope=scope, old_status=old, new_status=new, offset_ms=offset_ms,
                ),
                name=f"timesync-publish-{scope}-{new.value}",
            )

        engine.on_transition(_on_transition)

        consumer = consumer_factory_resolved(kafka_bootstrap)
        control_consumer = ControlConsumer(consumer=consumer, engine=engine)
        await control_consumer.start()

        app.state.ntp_monitor = monitor
        app.state.sync_state_engine = engine
        app.state.kafka_producer = producer
        app.state.timesync_publisher = publisher
        app.state.control_consumer = control_consumer

        poll_task = asyncio.create_task(
            loop_impl(monitor, engine, interval_s=poll_seconds),
            name="drs-server-ntp-poll",
        )
        consumer_task = asyncio.create_task(
            control_consumer.run(),
            name="drs-server-control-consumer",
        )

        try:
            yield
        finally:
            logger.info("drs-server lifespan shutting down")
            consumer_task.cancel()
            try:
                await consumer_task
            except asyncio.CancelledError:
                pass
            except Exception:
                logger.exception("consumer task raised during shutdown")
            try:
                await control_consumer.stop()
            except Exception:
                logger.exception("control consumer stop failed")
            poll_task.cancel()
            try:
                await poll_task
            except asyncio.CancelledError:
                pass
            except Exception:
                logger.exception("poll task raised during shutdown")
            try:
                await producer.stop()
            except Exception:
                logger.exception("producer stop failed")

    return lifespan
