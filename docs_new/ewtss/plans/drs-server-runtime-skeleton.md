# drs-server Runtime Skeleton Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `drs-server` actually serve `/time/status` at runtime. Today the package has `NtpMonitor`, `SyncStateEngine` (with per-variant tracking + scoped callbacks), `TimesyncPublisher`, and a `/time/status` route — but the FastAPI app never instantiates them, never assigns to `app.state`, never starts a poll loop, never wires the engine → publisher callback. Hitting the endpoint in production today would 500.

**Architecture:** A modern FastAPI **lifespan** handler (`@asynccontextmanager`) constructs `NtpMonitor` (configurable `ntpq` path), `SyncStateEngine` (default thresholds), `AIOKafkaProducer`, `TimesyncPublisher`, attaches them to `app.state`, wires a sync→async bridge so engine transitions become Kafka publishes, and starts a supervised poll task that loops `monitor.sample()` → `engine.record(sample)` on a configurable interval. On lifespan exit, the poll task is cancelled, the producer stopped. A new `cli.py` entry calls `uvicorn.run` against the FastAPI app and a dedicated `__main__.py` makes `python -m drs_server` work.

**Tech Stack:** Python 3.11 + FastAPI (already in pyproject) + uvicorn[standard] (already) + aiokafka (already) + pydantic-settings (new — small, well-established). No new heavyweight deps.

**Application target (pre-flight Pass 1):** This plan modifies `drs-server/` exclusively. `drs-bridge`, `sg-app`, `mvp4`, `drs-webapp` are NOT touched. The plan's only dependency on those is a *runtime* one: drs-bridge's `ControlPublisher` writes to `drs.control` and drs-server eventually consumes that; that consumer is **out of scope** for this skeleton (it lands per-variant per-feature in B1.x feature work).

**Test breadth statement (pre-flight Pass 3):** Each task's unit tests cover only the path required to wire the next task. The end-to-end runtime composition is covered by Task 5's integration test (`test_app_lifespan_serves_time_status`), which boots the full app via FastAPI's `LifespanManager` (or `TestClient`-equivalent) with a mocked `NtpMonitor.sample` and an `AsyncMock` Kafka producer.

**Out of scope (deliberate; will be flagged by reviewers as missing — keep them out):**
- Real Kafka broker connection — tests inject `AsyncMock` producer; production wires real `AIOKafkaProducer` via the factory seam in lifespan.
- Real `ntpq` subprocess — production reads the daemon installed by `infrastructure/ntp/sg-ntp-install.ps1` (lab runs of which are tracked under B1.20).
- Kafka consumer for `drs.control` (variant registration) — out of B1.3 scope; lands per-variant when each IRS ships.
- RBAC / JWT middleware on `/time/status` — RFQ-deferred; B1.3 design spec deliberately leaves `/time/status` unauthenticated (read-only operator-visible status).
- WebSocket push of `system.timesync` events to sg-app — `system.timesync` Kafka topic is the contract; sg-app polls `/time/status` per its existing `MainWindowViewModel.StartPolling` path. WebSocket fan-out is a future optimisation.
- Prometheus / OpenTelemetry observability.

**Application skeletons referenced (pre-flight Pass 2):** Every attribute the plan adds to `app.state` is initialised in the lifespan handler in Task 3; no Task references `app.state.<x>` before it's been put there. `NtpMonitor`, `SyncStateEngine`, `SyncStatus`, `TimesyncPublisher` already exist at their respective `drs_server.timesync.*` paths; this plan does not modify them.

**Adds dependency** `pydantic-settings>=2.0` (same as drs-bridge plan). Add to Task 1.

---

## Task 1: Config module — Pydantic Settings

**Files:**
- Create: `drs-server/src/drs_server/config.py`
- Test: `drs-server/tests/test_config.py`
- Modify: `drs-server/pyproject.toml` (add `pydantic-settings>=2.0` dependency)

- [ ] **Step 1: Add the dependency**

In `drs-server/pyproject.toml` under `[project] dependencies`, add `"pydantic-settings>=2.0",` after the existing pydantic entry.

- [ ] **Step 2: Install**

```
cd drs-server && .\.venv\Scripts\pip.exe install -e ".[dev]"
```

- [ ] **Step 3: Failing test**

File: `drs-server/tests/test_config.py`

```python
import pytest

from drs_server.config import ServerSettings


def test_defaults(monkeypatch: pytest.MonkeyPatch):
    for k in [
        "DRS_SERVER_NTPQ_PATH", "DRS_SERVER_KAFKA_BOOTSTRAP",
        "DRS_SERVER_POLL_SECONDS", "DRS_SERVER_LOG_LEVEL",
        "DRS_SERVER_HOST", "DRS_SERVER_PORT",
    ]:
        monkeypatch.delenv(k, raising=False)
    s = ServerSettings()
    assert s.ntpq_path == r"C:\Program Files\NTP\bin\ntpq.exe"
    assert s.kafka_bootstrap == "localhost:9092"
    assert s.poll_seconds == 5
    assert s.log_level == "INFO"
    assert s.host == "0.0.0.0"
    assert s.port == 8000


def test_env_override(monkeypatch: pytest.MonkeyPatch):
    monkeypatch.setenv("DRS_SERVER_NTPQ_PATH", "/usr/bin/ntpq")
    monkeypatch.setenv("DRS_SERVER_KAFKA_BOOTSTRAP", "broker.lan:9092")
    monkeypatch.setenv("DRS_SERVER_POLL_SECONDS", "10")
    monkeypatch.setenv("DRS_SERVER_LOG_LEVEL", "DEBUG")
    monkeypatch.setenv("DRS_SERVER_HOST", "127.0.0.1")
    monkeypatch.setenv("DRS_SERVER_PORT", "9000")
    s = ServerSettings()
    assert s.ntpq_path == "/usr/bin/ntpq"
    assert s.kafka_bootstrap == "broker.lan:9092"
    assert s.poll_seconds == 10
    assert s.log_level == "DEBUG"
    assert s.host == "127.0.0.1"
    assert s.port == 9000
```

- [ ] **Step 4: Run, expect ImportError**

```
cd drs-server && .\.venv\Scripts\pytest.exe tests/test_config.py -v
```

- [ ] **Step 5: Implement**

File: `drs-server/src/drs_server/config.py`

```python
"""Runtime configuration sourced from environment variables.

DRS_SERVER_NTPQ_PATH       — absolute path to the Meinberg ntpq binary
                             (default: C:\\Program Files\\NTP\\bin\\ntpq.exe)
DRS_SERVER_KAFKA_BOOTSTRAP — Kafka bootstrap.servers (default: localhost:9092)
DRS_SERVER_POLL_SECONDS    — NtpMonitor poll interval (default: 5)
DRS_SERVER_LOG_LEVEL       — Python logging level (default: INFO)
DRS_SERVER_HOST            — uvicorn bind host (default: 0.0.0.0)
DRS_SERVER_PORT            — uvicorn bind port (default: 8000)
"""
from pydantic_settings import BaseSettings, SettingsConfigDict


class ServerSettings(BaseSettings):
    model_config = SettingsConfigDict(env_prefix="DRS_SERVER_", env_file=None)

    ntpq_path: str = r"C:\Program Files\NTP\bin\ntpq.exe"
    kafka_bootstrap: str = "localhost:9092"
    poll_seconds: int = 5
    log_level: str = "INFO"
    host: str = "0.0.0.0"
    port: int = 8000
```

- [ ] **Step 6: Run, expect 2 passed**

```
.\.venv\Scripts\pytest.exe tests/test_config.py -v
```

Full suite:
```
.\.venv\Scripts\pytest.exe -v
```
Expected: 15 passed (13 prior + 2 new).

- [ ] **Step 7: Commit**

```
git -C e:/GitHub/ewtss-v2-pub add drs-server/pyproject.toml drs-server/src/drs_server/config.py drs-server/tests/test_config.py
git -C e:/GitHub/ewtss-v2-pub commit -m "feat(drs-server): ServerSettings env-driven config (runtime skeleton Task 1)"
```

Verify `git -C e:/GitHub/ewtss-v2-pub branch --show-current` reads `main` before commit.

---

## Task 2: Supervised poll task

**Files:**
- Create: `drs-server/src/drs_server/timesync/poller.py`
- Test: `drs-server/tests/timesync/test_poller.py`

A self-contained async task that loops `monitor.sample()` → `engine.record(sample)` on a configurable interval. Catches exceptions per iteration (so a transient `ntpq` failure doesn't kill the loop), respects cancellation cleanly.

- [ ] **Step 1: Failing test**

File: `drs-server/tests/timesync/test_poller.py`

```python
import asyncio
from datetime import datetime, timezone

import pytest
from unittest.mock import AsyncMock, MagicMock

from drs_server.timesync.ntp_monitor import NtpSample
from drs_server.timesync.poller import poll_loop


def _sample(offset_ms: float = 0.5) -> NtpSample:
    return NtpSample(
        offset_ms=offset_ms,
        jitter_ms=0.1,
        stratum=2,
        sampled_at=datetime.now(timezone.utc),
        peer="WS1-SG",
    )


@pytest.mark.asyncio
async def test_poll_loop_records_samples_then_exits_on_cancel():
    monitor = MagicMock()
    monitor.sample = AsyncMock(return_value=_sample())
    engine = AsyncMock()

    task = asyncio.create_task(poll_loop(monitor, engine, interval_s=0.02))
    await asyncio.sleep(0.08)  # allow ~4 iterations at 20 ms
    task.cancel()
    try:
        await task
    except asyncio.CancelledError:
        pass

    assert monitor.sample.await_count >= 2
    assert engine.record.await_count == monitor.sample.await_count


@pytest.mark.asyncio
async def test_poll_loop_swallows_monitor_exceptions_and_keeps_going():
    monitor = MagicMock()
    # First call raises, subsequent calls succeed.
    monitor.sample = AsyncMock(side_effect=[RuntimeError("ntpq failed"), _sample(), _sample()])
    engine = AsyncMock()

    task = asyncio.create_task(poll_loop(monitor, engine, interval_s=0.02))
    await asyncio.sleep(0.10)
    task.cancel()
    try:
        await task
    except asyncio.CancelledError:
        pass

    # First iteration failed before engine.record was reached;
    # subsequent iterations succeed.
    assert engine.record.await_count >= 1


@pytest.mark.asyncio
async def test_poll_loop_exits_immediately_when_cancelled_during_sleep():
    monitor = MagicMock()
    monitor.sample = AsyncMock(return_value=_sample())
    engine = AsyncMock()

    task = asyncio.create_task(poll_loop(monitor, engine, interval_s=1.0))
    await asyncio.sleep(0.05)  # let the first iteration complete; now sleeping
    task.cancel()
    try:
        await task
    except asyncio.CancelledError:
        pass

    assert task.done()
```

- [ ] **Step 2: Run, expect ImportError**

```
.\.venv\Scripts\pytest.exe tests/timesync/test_poller.py -v
```

- [ ] **Step 3: Implement**

File: `drs-server/src/drs_server/timesync/poller.py`

```python
"""Supervised poll loop that drives SyncStateEngine from NtpMonitor samples.

Designed to be wrapped in an `asyncio.Task` from the FastAPI lifespan.
Exits cleanly on cancellation; logs and skips iterations where the
monitor raises (transient `ntpq` failures), so one bad poll doesn't
kill the entire state-engine feed.
"""
from __future__ import annotations

import asyncio
import logging

from drs_server.timesync.ntp_monitor import NtpMonitor
from drs_server.timesync.sync_state_engine import SyncStateEngine

logger = logging.getLogger(__name__)


async def poll_loop(
    monitor: NtpMonitor,
    engine: SyncStateEngine,
    interval_s: float,
) -> None:
    """Loop until cancelled.

    Each iteration:
      1. Call monitor.sample().
      2. Pass the sample to engine.record().
      3. Sleep `interval_s` before the next iteration.

    Exceptions from step 1 or 2 are logged and the loop continues to
    the sleep. CancelledError from the sleep propagates out cleanly.
    """
    while True:
        try:
            sample = await monitor.sample()
            await engine.record(sample)
        except asyncio.CancelledError:
            raise
        except Exception:
            logger.exception("poll iteration failed; continuing")
        await asyncio.sleep(interval_s)
```

- [ ] **Step 4: Run, expect 3 passed**

```
.\.venv\Scripts\pytest.exe tests/timesync/test_poller.py -v
```

Full suite:
```
.\.venv\Scripts\pytest.exe -v
```
Expected: 18 passed (15 prior + 3 new).

- [ ] **Step 5: Commit**

```
git -C e:/GitHub/ewtss-v2-pub add drs-server/src/drs_server/timesync/poller.py drs-server/tests/timesync/test_poller.py
git -C e:/GitHub/ewtss-v2-pub commit -m "feat(drs-server): supervised NTP poll loop (runtime skeleton Task 2)"
```

---

## Task 3: FastAPI lifespan + app.state wiring

**Files:**
- Create: `drs-server/src/drs_server/lifespan.py`
- Test: `drs-server/tests/test_lifespan.py`

The lifespan handler is the modern (FastAPI ≥0.93) replacement for `@app.on_event("startup"/"shutdown")`. It is an `@asynccontextmanager`-decorated async generator that runs setup, yields, then runs teardown.

This lifespan:
1. Constructs `NtpMonitor(ntpq_path=settings.ntpq_path)`.
2. Constructs `SyncStateEngine(thresholds=SyncThresholds())`.
3. Constructs `AIOKafkaProducer` via a factory (so tests inject a mock).
4. Awaits `producer.start()`.
5. Constructs `TimesyncPublisher(producer, topic="system.timesync")`.
6. Registers an engine `on_transition` callback that schedules `publisher.publish_transition(...)` as a fire-and-forget asyncio task (sync→async bridge; the engine's callback is sync `(str, SyncStatus, SyncStatus) -> None`).
7. Stores monitor + engine + producer + publisher on `app.state`.
8. Starts the supervised `poll_loop` as a task.
9. **Yields** (FastAPI serves requests here).
10. On exit: cancels the poll task, awaits cancellation, stops the producer.

The factory-injected producer is important because the integration test (Task 5) replaces it with an `AsyncMock`.

- [ ] **Step 1: Failing test**

File: `drs-server/tests/test_lifespan.py`

```python
from unittest.mock import AsyncMock, MagicMock

import pytest
from fastapi import FastAPI

from drs_server.lifespan import make_lifespan


@pytest.mark.asyncio
async def test_lifespan_wires_app_state_and_starts_producer():
    producer = AsyncMock()
    app = FastAPI(lifespan=make_lifespan(
        ntpq_path="dummy",
        kafka_bootstrap="dummy:9092",
        poll_seconds=1,
        producer_factory=lambda bootstrap: producer,
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
            while True:
                import asyncio
                await asyncio.sleep(0.01)
        except Exception:
            raise

    app = FastAPI(lifespan=make_lifespan(
        ntpq_path="dummy",
        kafka_bootstrap="dummy:9092",
        poll_seconds=1,
        producer_factory=lambda bootstrap: producer,
        poll_loop_impl=fake_poll,
    ))
    async with app.router.lifespan_context(app):
        # Give the poll task a moment to start.
        import asyncio
        await asyncio.sleep(0.02)
        assert poll_task_ref.get("called") is True
    # After the lifespan exits, the task should be cancelled. We don't
    # assert task.done() directly (we don't expose it) — the producer.stop
    # being awaited is our signal that teardown ran.
    producer.stop.assert_awaited_once()


@pytest.mark.asyncio
async def test_engine_transition_callback_dispatches_to_publisher():
    """The lifespan must wire a sync->async bridge so engine transitions
    fire publisher.publish_transition as a fire-and-forget task."""
    import asyncio

    producer = AsyncMock()
    captured_publishers = []

    app = FastAPI(lifespan=make_lifespan(
        ntpq_path="dummy",
        kafka_bootstrap="dummy:9092",
        poll_seconds=1,
        producer_factory=lambda bootstrap: producer,
    ))
    async with app.router.lifespan_context(app):
        engine = app.state.sync_state_engine
        publisher = app.state.timesync_publisher
        # Replace the publisher's publish_transition with a spy so we can
        # observe the bridge.
        publisher.publish_transition = AsyncMock()

        # Fire a synthetic transition through the engine callbacks.
        # _transition_to is an internal method, but it's the simplest way
        # to invoke the callbacks deterministically.
        from drs_server.timesync.sync_state_engine import SyncStatus
        await engine._transition_to(SyncStatus.HEALTHY)
        # Allow the bridge's fire-and-forget task to run.
        await asyncio.sleep(0.02)

        publisher.publish_transition.assert_awaited()
        kwargs = publisher.publish_transition.call_args.kwargs
        assert kwargs["scope"] == "global"
        assert kwargs["new_status"] == SyncStatus.HEALTHY
```

- [ ] **Step 2: Run, expect ImportError**

```
.\.venv\Scripts\pytest.exe tests/test_lifespan.py -v
```

- [ ] **Step 3: Implement**

File: `drs-server/src/drs_server/lifespan.py`

```python
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

from drs_server.timesync.ntp_monitor import NtpMonitor
from drs_server.timesync.poller import poll_loop as _real_poll_loop
from drs_server.timesync.sync_state_engine import SyncStateEngine, SyncStatus, SyncThresholds
from drs_server.timesync.timesync_publisher import TimesyncPublisher

logger = logging.getLogger(__name__)


def _default_producer_factory(bootstrap: str):
    # Imported lazily so unit tests that mock the factory don't pay aiokafka import cost.
    from aiokafka import AIOKafkaProducer
    return AIOKafkaProducer(bootstrap_servers=bootstrap)


def make_lifespan(
    ntpq_path: str,
    kafka_bootstrap: str,
    poll_seconds: int,
    producer_factory: Callable | None = None,
    poll_loop_impl: Callable | None = None,
):
    """Returns an @asynccontextmanager-compatible lifespan for FastAPI."""
    factory = producer_factory or _default_producer_factory
    loop_impl = poll_loop_impl or _real_poll_loop

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

        app.state.ntp_monitor = monitor
        app.state.sync_state_engine = engine
        app.state.kafka_producer = producer
        app.state.timesync_publisher = publisher

        poll_task = asyncio.create_task(
            loop_impl(monitor, engine, interval_s=poll_seconds),
            name="drs-server-ntp-poll",
        )

        try:
            yield
        finally:
            logger.info("drs-server lifespan shutting down")
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
```

- [ ] **Step 4: Run, expect 3 passed**

```
.\.venv\Scripts\pytest.exe tests/test_lifespan.py -v
```

Full suite:
```
.\.venv\Scripts\pytest.exe -v
```
Expected: 21 passed (18 prior + 3 new).

- [ ] **Step 5: Commit**

```
git -C e:/GitHub/ewtss-v2-pub add drs-server/src/drs_server/lifespan.py drs-server/tests/test_lifespan.py
git -C e:/GitHub/ewtss-v2-pub commit -m "feat(drs-server): FastAPI lifespan wires app.state + poll loop + Kafka bridge (runtime skeleton Task 3)"
```

---

## Task 4: Replace `main.py` to use the lifespan + add CLI entry

**Files:**
- Modify: `drs-server/src/drs_server/main.py` (currently constructs `FastAPI(title=..., version=...)` without lifespan)
- Create: `drs-server/src/drs_server/__main__.py` (so `python -m drs_server` works)
- Test: `drs-server/tests/test_main_construction.py` (verifies the constructed app has lifespan + correct routes)

- [ ] **Step 1: Failing test**

File: `drs-server/tests/test_main_construction.py`

```python
def test_app_has_lifespan_attached():
    from drs_server.main import app
    # FastAPI stores the lifespan context as router.lifespan_context.
    assert app.router.lifespan_context is not None


def test_app_has_time_status_route():
    from drs_server.main import app
    route_paths = {getattr(r, "path", None) for r in app.routes}
    assert "/time/status" in route_paths
    assert "/health" in route_paths


def test_main_module_runs_uvicorn(monkeypatch):
    """`python -m drs_server` should call uvicorn.run with the app + host + port."""
    called = {}

    def fake_run(app_or_path, host, port, **kwargs):
        called["args"] = (app_or_path, host, port)

    import drs_server.__main__ as entry
    monkeypatch.setattr(entry, "uvicorn", type("U", (), {"run": fake_run}))
    entry.main()
    assert called["args"][1] == "0.0.0.0"
    assert called["args"][2] == 8000
```

- [ ] **Step 2: Run, expect a mix of failures (lifespan not attached, __main__ doesn't exist)**

```
.\.venv\Scripts\pytest.exe tests/test_main_construction.py -v
```

- [ ] **Step 3: Replace `main.py`**

```python
"""FastAPI app construction. The lifespan handler wires the time-sync
subsystem; routes are included from drs_server.api.*.
"""
import logging
import logging.config

from fastapi import FastAPI

from drs_server.api import time_status
from drs_server.config import ServerSettings
from drs_server.lifespan import make_lifespan

logger = logging.getLogger(__name__)


def configure_logging(level: str) -> None:
    logging.config.dictConfig(
        {
            "version": 1,
            "disable_existing_loggers": False,
            "formatters": {
                "default": {
                    "format": "%(asctime)s %(levelname)-7s %(name)s: %(message)s",
                },
            },
            "handlers": {
                "console": {"class": "logging.StreamHandler", "formatter": "default"},
            },
            "root": {"level": level, "handlers": ["console"]},
        }
    )


_settings = ServerSettings()
configure_logging(_settings.log_level)

app = FastAPI(
    title="EWTSS drs-server",
    version="0.1.0",
    lifespan=make_lifespan(
        ntpq_path=_settings.ntpq_path,
        kafka_bootstrap=_settings.kafka_bootstrap,
        poll_seconds=_settings.poll_seconds,
    ),
)
app.include_router(time_status.router)


@app.get("/health")
async def health() -> dict[str, str]:
    return {"status": "ok"}
```

- [ ] **Step 4: Create `__main__.py`**

File: `drs-server/src/drs_server/__main__.py`

```python
"""Allow `python -m drs_server` to launch the uvicorn server."""
import uvicorn

from drs_server.config import ServerSettings


def main() -> None:
    settings = ServerSettings()
    uvicorn.run(
        "drs_server.main:app",
        host=settings.host,
        port=settings.port,
        log_level=settings.log_level.lower(),
    )


if __name__ == "__main__":
    main()
```

- [ ] **Step 5: Run, expect 3 passed**

```
.\.venv\Scripts\pytest.exe tests/test_main_construction.py -v
```

Full suite:
```
.\.venv\Scripts\pytest.exe -v
```
Expected: 24 passed (21 prior + 3 new).

- [ ] **Step 6: Commit**

```
git -C e:/GitHub/ewtss-v2-pub add drs-server/src/drs_server/main.py drs-server/src/drs_server/__main__.py drs-server/tests/test_main_construction.py
git -C e:/GitHub/ewtss-v2-pub commit -m "feat(drs-server): main.py wires lifespan + __main__.py adds python -m entry (runtime skeleton Task 4)"
```

---

## Task 5: Integration test — `/time/status` end-to-end through lifespan

**Files:**
- Create: `drs-server/tests/test_integration_time_status.py`

This boots the FastAPI app via the lifespan context (with a mocked Kafka producer + mocked `NtpMonitor.sample`) and hits `/time/status` via `httpx.AsyncClient`. The test proves the lifespan wiring actually serves the endpoint with real engine state — closing the integration loop F16 was concerned about.

- [ ] **Step 1: Write the test**

File: `drs-server/tests/test_integration_time_status.py`

```python
"""Integration test: boots the FastAPI app via lifespan with a mocked
NtpMonitor.sample + AsyncMock Kafka producer, then hits /time/status
and verifies the engine actually drove a real response.
"""
from datetime import datetime, timezone
from unittest.mock import AsyncMock

import pytest
from httpx import ASGITransport, AsyncClient
from fastapi import FastAPI

from drs_server.api import time_status
from drs_server.lifespan import make_lifespan
from drs_server.timesync.ntp_monitor import NtpSample


@pytest.mark.asyncio
async def test_time_status_returns_engine_state_after_lifespan_boot(monkeypatch):
    fake_sample = NtpSample(
        offset_ms=0.4,
        jitter_ms=0.2,
        stratum=2,
        sampled_at=datetime.now(timezone.utc),
        peer="WS1-SG.local",
    )

    # Patch NtpMonitor.sample so we don't run real ntpq.
    monkeypatch.setattr(
        "drs_server.timesync.ntp_monitor.NtpMonitor.sample",
        AsyncMock(return_value=fake_sample),
    )

    producer = AsyncMock()

    app = FastAPI(lifespan=make_lifespan(
        ntpq_path="dummy",
        kafka_bootstrap="dummy:9092",
        poll_seconds=1,
        producer_factory=lambda bootstrap: producer,
    ))
    app.include_router(time_status.router)

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        # The lifespan starts when we enter the client context (httpx handles it).
        response = await client.get("/time/status")
        assert response.status_code == 200
        body = response.json()
        assert body["ntp_offset_ms"] == pytest.approx(0.4)
        assert body["ntp_jitter_ms"] == pytest.approx(0.2)
        assert body["ntp_peer"] == "WS1-SG.local"
        # status starts as WARMING per SyncStateEngine's initial state.
        assert body["status"] == "warming"
        assert "current_time" in body
        assert "last_sync" in body

    producer.start.assert_awaited_once()
    producer.stop.assert_awaited_once()
```

- [ ] **Step 2: Run, expect 1 passed**

```
.\.venv\Scripts\pytest.exe tests/test_integration_time_status.py -v
```

Full suite:
```
.\.venv\Scripts\pytest.exe -v
```
Expected: 25 passed (24 prior + 1 new).

- [ ] **Step 3: Commit**

```
git -C e:/GitHub/ewtss-v2-pub add drs-server/tests/test_integration_time_status.py
git -C e:/GitHub/ewtss-v2-pub commit -m "test(drs-server): integration test for /time/status via lifespan (runtime skeleton Task 5)"
```

---

## Self-review checklist run

- ✅ **Spec coverage** — every gap from the user's "drs-server can't actually serve /time/status" concern has a task: config (T1), supervised poll task (T2), lifespan + app.state wiring + sync→async bridge (T3), main entry + CLI (T4), end-to-end integration test (T5).
- ✅ **Placeholder scan** — no TBD / TODO. The sync→async bridge in T3 reads `engine._window[-1]` to get the latest sample's offset_ms; that's a documented design choice (the engine's callback signature doesn't carry offset, so the bridge has to peek). Not a placeholder.
- ✅ **Type consistency** — `NtpMonitor.sample`, `NtpSample`, `SyncStateEngine.record`, `SyncStatus`, `TimesyncPublisher.publish_transition`, `SyncThresholds` are all referenced with their existing signatures. No new types introduced.
- ✅ **Application target unambiguous** — header says drs-server only; drs-bridge / sg-app / mvp4 / drs-webapp explicitly excluded.
- ✅ **Skeleton mapping** — every `app.state.<attr>` is initialised in T3's lifespan. No T references attributes before T3 puts them there. `__main__.py` is a fresh file with no prior dependencies.
- ✅ **Environment pre-flight** — Python 3.11 pinned; pydantic-settings is the only new dep (added in T1); fastapi + uvicorn + aiokafka already in deps; no new top-level dirs; no `.gitignore` change needed.
- ✅ **Content traps grep** — no `datetime.utcnow`, no `asyncio.get_event_loop`. The bridge uses `asyncio.create_task` (correct).
- ✅ **Parameterise rather than magic-number** — lifespan takes factories (`producer_factory`, `poll_loop_impl`) so tests inject without monkey-patching globals. ntpq path / kafka bootstrap / poll seconds all flow through ServerSettings.
- ✅ **Test breadth statement** — header carries it.
- ✅ **Out of scope list** — header carries it (real Kafka, real ntpq, drs.control consumer, RBAC, WebSocket push, observability).

---

## Execution handoff

Plan complete. Two execution options:

**1. Subagent-Driven (recommended)** — fresh subagent per task + branch-state guardrails (matching the drs-bridge skeleton pattern).
**2. Inline Execution** — batch with checkpoints.

Going with subagent-driven; identical flow to the drs-bridge runtime skeleton.
