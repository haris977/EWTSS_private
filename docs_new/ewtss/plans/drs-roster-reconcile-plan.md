# DRS Roster — Publisher + Presence + Readiness Reconcile Implementation Plan (Plan 2b of B1.43)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the roster store *live*: drs-server **publishes** the active roster to the compacted `drs.roster` Kafka topic (which drs-bridge already consumes — Plan 1), **tracks per-instance connection presence** from `system.health`, and serves **`POST /exercise/readiness`** so SG can reconcile a scenario's required instances against the roster + reachability + time-sync before an Integrated-mode run. Also adds the **port-allocation engine** for `allocated` entries.

**Architecture:** A `RosterPublisher` (mirroring `TimesyncPublisher`) sends the active-roster snapshot to `drs.roster` keyed `active`; the roster write endpoints publish after each commit, and the lifespan publishes once at startup. A `PresenceConsumer` (mirroring `ControlConsumer`) consumes `system.health` and maintains an in-memory per-instance connection count in `app.state.instance_presence`; drs-bridge gains an `instance.disconnected` event so presence clears on teardown. A new `/exercise/readiness` endpoint composes active-roster membership + presence + `SyncStateEngine.current_variant_status` into a per-instance verdict. The port allocator fills `allocated` entries missing a port at upsert time.

**Tech Stack:** Python 3.11; drs-server (FastAPI, SQLAlchemy 2.0 async, aiokafka) + drs-bridge (aiokafka, asyncio). pytest + pytest-asyncio. Real PostgreSQL + Kafka available locally for integration tests.

**Spec:** [docs/ewtss/specs/drs-instance-addressing-design.md](../specs/drs-instance-addressing-design.md) §6.1 (reachable = connection-presence), §6.2 (publisher), §6.4 (reconcile), §7.4 (time-sync) ([B1.43](../design-backlog.md)). Builds on Plan 1 (drs-bridge consumer + health events) + Plan 2a (roster store).

---

## Application target (multi-app repo)

Touches **`drs-server/`** (publisher, presence consumer, readiness, allocator — most of the work), **`drs-bridge/`** (one addition: emit `instance.disconnected`), and **`contracts/`** (`/exercise/readiness` OpenAPI). Does NOT touch `sg-app/`, `drs-webapp/`, `mvp4/`.

## Test-breadth statement

Unit tests per task cover the path to wire the next. Phase breadth lives in **Task 3.2** (readiness verdict matrix) and **Task 5.1** (real-broker end-to-end: PUT roster → `drs.roster` snapshot → drs-bridge binds).

## Out of scope (deliberate)

- **Sg.App / DRS-webapp readiness-panel UI** — Plan 3 (B1.1 wireframes). This plan serves the `/exercise/readiness` *data contract* the panel renders.
- **Auth on the endpoints** — B1.16/B1.17.
- **Persisting presence** — presence is in-memory (rebuilt from `system.health` on restart); not a DB table.
- **Mid-exercise partition recovery** — B1.37. This plan covers launch-time reachability only.
- **drs-bridge consuming its own published roster end-to-end in CI** — the cross-service loop is exercised by the local integration test (Task 5.1), graceful-skip without a broker.

## Environment pre-flight

- Python 3.11; run pytest via each service's `.venv`. Postgres on `localhost:5432` (role `ewtss`, DB `ewtss_test`) + native Kafka on `localhost:9092` for integration tests — see [infrastructure/kafka/README.md](../../infrastructure/kafka/README.md) + top-level README. Integration tests **graceful-skip** when absent.
- New setting: `DRS_SERVER_ALLOCATED_PORT_BASE` (default `5500`). No new third-party deps. No new top-level dirs.
- Use `datetime.now(timezone.utc)`; for any `Z` timestamp reuse `timesync_publisher._utc_now_iso_z` pattern (`.isoformat(timespec="microseconds").replace("+00:00","Z")`).

## Things this plan does NOT do (reviewer calibration)

- No retry/backoff on the publisher beyond aiokafka defaults; a publish failure logs and does not fail the originating write (the compacted topic + next write reconcile).
- Presence is a simple per-instance connection **count** (connect=+1, disconnect=−1; reachable = count>0) — not a per-connection registry.
- `time_synced` is `status == HEALTHY` (strict); WARMING/DRIFT_*/SYNC_LOST/unregistered → not synced.

---

## Phase 1 — RosterPublisher + wiring

### Task 1.1: `snapshot()` projection (shared by export + publish)

**Files:** Modify `drs-server/src/drs_server/roster/projections.py`; Test `drs-server/tests/roster/test_projections.py`

The publish payload and the `GET /{id}/export` body are the **same shape** (`roster_id`, `version`, `entries[]` with `command`/`response` objects) — the shape drs-bridge's `Roster` consumes (`contracts/kafka/drs.roster.schema.json`). Extract it once.

- [ ] **Step 1: Failing test** — create `drs-server/tests/roster/test_projections.py`:
```python
from drs_server.roster.projections import snapshot


class _E:
    def __init__(self):
        self.instance_id, self.variant, self.host = "rdfs#1", "rdfs", "10.0.0.5"
        self.command_port, self.command_protocol = 5001, "tcp"
        self.response_port, self.response_protocol = 5002, "udp"
        self.port_source, self.enabled = "irs_fixed", True


class _R:
    roster_id, version = "lab", 7
    entries = [_E()]


def test_snapshot_matches_drs_roster_shape():
    s = snapshot(_R())
    assert s == {
        "roster_id": "lab", "version": 7,
        "entries": [{
            "instance_id": "rdfs#1", "variant": "rdfs", "host": "10.0.0.5",
            "command": {"port": 5001, "protocol": "tcp"},
            "response": {"port": 5002, "protocol": "udp"},
            "port_source": "irs_fixed", "enabled": True,
        }],
    }
```

- [ ] **Step 2: Run → fail** (`snapshot` missing): `drs-server/.venv/Scripts/python.exe -m pytest drs-server/tests/roster/test_projections.py -v`

- [ ] **Step 3: Implement** — append to `projections.py`:
```python
def snapshot(roster) -> dict:
    """The drs.roster / export shape (matches contracts/kafka/drs.roster.schema.json
    and drs-bridge's Roster model). Shared by GET /{id}/export and the publisher."""
    return {
        "roster_id": roster.roster_id,
        "version": roster.version,
        "entries": [
            {
                "instance_id": e.instance_id, "variant": e.variant, "host": e.host,
                "command": {"port": e.command_port, "protocol": e.command_protocol},
                "response": {"port": e.response_port, "protocol": e.response_protocol},
                "port_source": e.port_source, "enabled": e.enabled,
            }
            for e in roster.entries
        ],
    }
```
Then refactor `api/roster.py::_export_doc` to `return projections.snapshot(row)` (delete the inline dict; behaviour identical — the existing `test_export_returns_roster_document` still passes).

- [ ] **Step 4: Run → pass** (projection test + existing roster api tests).
- [ ] **Step 5: Commit** — `feat(server): shared roster snapshot projection (export + publish) (B1.43)`

### Task 1.2: `RosterPublisher`

**Files:** Create `drs-server/src/drs_server/roster/publisher.py`; Test `drs-server/tests/roster/test_publisher.py`

- [ ] **Step 1: Failing test**:
```python
import json
import pytest
from drs_server.roster.publisher import RosterPublisher


class _CapturingProducer:
    def __init__(self): self.sent = []
    async def send_and_wait(self, topic, value, key=None):
        self.sent.append((topic, key, json.loads(value.decode("utf-8"))))


class _E:
    instance_id, variant, host = "rdfs#1", "rdfs", "10.0.0.5"
    command_port, command_protocol = 5001, "tcp"
    response_port, response_protocol = 5002, "udp"
    port_source, enabled = "irs_fixed", True


class _R:
    roster_id, version, entries = "lab", 3, [_E()]


@pytest.mark.asyncio
async def test_publish_active_uses_compaction_key_and_snapshot():
    prod = _CapturingProducer()
    await RosterPublisher(prod).publish(_R())
    topic, key, body = prod.sent[0]
    assert topic == "drs.roster"
    assert key == b"active"                      # compaction key
    assert body["roster_id"] == "lab" and body["version"] == 3
    assert body["entries"][0]["command"]["port"] == 5001


@pytest.mark.asyncio
async def test_publish_none_is_noop():
    prod = _CapturingProducer()
    await RosterPublisher(prod).publish(None)    # no active roster -> nothing sent
    assert prod.sent == []
```

- [ ] **Step 2: Run → fail.**
- [ ] **Step 3: Implement** `drs-server/src/drs_server/roster/publisher.py`:
```python
"""Publishes the active roster snapshot to the compacted Kafka `drs.roster`
topic (key 'active'); drs-bridge consumes it (B1.43 Plan 1)."""
from __future__ import annotations

import json
import logging
from typing import Protocol

from drs_server.roster import projections

logger = logging.getLogger(__name__)


class KafkaProducerLike(Protocol):
    async def send_and_wait(self, topic: str, value: bytes, key: bytes | None = None) -> None: ...


class RosterPublisher:
    def __init__(self, producer: KafkaProducerLike, topic: str = "drs.roster") -> None:
        self._producer = producer
        self._topic = topic

    async def publish(self, roster) -> None:
        """Publish the given (active) roster as the retained compacted snapshot.
        No-op when roster is None (no active roster yet)."""
        if roster is None:
            return
        payload = json.dumps(projections.snapshot(roster)).encode("utf-8")
        await self._producer.send_and_wait(self._topic, value=payload, key=b"active")
        logger.info("published drs.roster snapshot %s@%s", roster.roster_id, roster.version)
```

- [ ] **Step 4: Run → pass.**
- [ ] **Step 5: Commit** — `feat(server): RosterPublisher -> compacted drs.roster (B1.43)`

### Task 1.3: wire publisher into lifespan + write endpoints

**Files:** Modify `lifespan.py`, `api/roster.py`; Test `drs-server/tests/roster/test_roster_api.py` (add a publish-on-write case)

- [ ] **Step 1: Failing test** — add to `test_roster_api.py` (the fake app already overrides `get_repo`; add a publisher override + assert it's called on activate/PUT):
```python
def test_put_then_activate_publishes_active(monkeypatch):
    repo = _FakeRepo(rows={"lab": _sample_active()})
    published = []

    class _Pub:
        async def publish(self, roster): published.append(roster)

    app = _app(repo)
    app.dependency_overrides[roster_api.get_publisher] = lambda: _Pub()
    client = TestClient(app)
    assert client.post("/roster/lab/activate").status_code == 200
    assert len(published) == 1            # active roster republished after activate
```

- [ ] **Step 2: Run → fail** (`get_publisher` missing).
- [ ] **Step 3: Implement.**
  (a) In `api/roster.py`, add a publisher provider + publish-after-write. Add:
```python
from drs_server.roster.publisher import RosterPublisher

def get_publisher(request: Request) -> RosterPublisher:
    return request.app.state.roster_publisher
```
  After each successful write (`put_roster`, `import_roster`, `activate_roster`), publish the **active** roster. Inject `pub: RosterPublisher = Depends(get_publisher)` into those three endpoints, and after the repo call add:
```python
    active = await repo.get_active()
    await pub.publish(active)
```
  (For `put_roster`/`import_roster`: only the active roster is published; if the edited roster isn't active, `get_active()` returns the unchanged active one — still correct to republish.)
  (b) In `lifespan.py`, construct the publisher from the existing producer and store it, and publish the current active roster once at startup. Where `app.state.kafka_producer = producer` is set, add:
```python
    from drs_server.roster.publisher import RosterPublisher
    from drs_server.roster.repository import RosterRepository
    app.state.roster_publisher = RosterPublisher(producer)
    # publish current active roster at startup (best-effort)
    try:
        async with app.state.session_factory() as s:
            await app.state.roster_publisher.publish(await RosterRepository(s).get_active())
    except Exception:
        logger.exception("startup roster publish failed")
```
  (guard: only when `session_factory` is a real factory; in the time-sync integration test that injects `session_factory_impl`, this still works if it yields a session — otherwise wrap in the try/except which already swallows.)

- [ ] **Step 4: Run → pass** (full drs-server unit suite).
- [ ] **Step 5: Commit** — `feat(server): publish active roster on write + at startup (B1.43)`

---

## Phase 2 — Per-instance presence

### Task 2.1: drs-bridge emits `instance.disconnected`

**Files:** Modify `drs-bridge/src/drs_bridge/transport.py`, `runtime.py`; Test `drs-bridge/tests/test_transport.py`

Presence needs a teardown signal. Add an `on_disconnect(instance_id)` callback fired when a connection closes (after it had connected), symmetric to `on_connect`.

- [ ] **Step 1: Failing test** — add to `drs-bridge/tests/test_transport.py`:
```python
@pytest.mark.asyncio
async def test_probation_fires_on_disconnect_after_eof():
    events = []
    reader = _FakeReader([b"\xAA\x06\x01\x00\x00\x00\x00\x00"])  # one frame then EOF
    writer = _FakeWriter()
    await probation_connection(
        reader, writer, instance_id="rdfs#1", detector=_ref_detector,
        on_frame=lambda iid, f: None, garbage_ceiling=4096, idle_timeout=1.0,
        on_disconnect=lambda iid: events.append(iid),
    )
    assert events == ["rdfs#1"]
```

- [ ] **Step 2: Run → fail** (`on_disconnect` kwarg unknown).
- [ ] **Step 3: Implement** — in `transport.py`, add `on_disconnect: Callable[[str], None] | None = None` to both `probation_connection` and `start_command_server`; in `probation_connection`'s `finally`, after closing the writer, call `if on_disconnect: on_disconnect(instance_id)`. Thread it through `start_command_server._handle`. In `runtime.py::_bind`, add an `_on_disconnect` that publishes `instance.disconnected`:
```python
            def _on_disconnect(instance_id: str) -> None:
                asyncio.create_task(self._health.publish("instance.disconnected", {"instance_id": instance_id}))
```
  and pass `on_disconnect=_on_disconnect` to `self._command_server_factory(...)`.

- [ ] **Step 4: Run → pass** (`tests/test_transport.py`, `tests/test_runtime_roster.py`).
- [ ] **Step 5: Commit** — `feat(bridge): emit instance.disconnected on connection teardown (B1.43)`

### Task 2.2: drs-server presence registry + consumer

**Files:** Create `drs-server/src/drs_server/roster/presence.py`, `drs-server/src/drs_server/roster/presence_consumer.py`; Tests alongside

- [ ] **Step 1: Failing tests** — `drs-server/tests/roster/test_presence.py`:
```python
import json
import pytest
from types import SimpleNamespace
from drs_server.roster.presence import InstancePresence
from drs_server.roster.presence_consumer import PresenceConsumer


def test_presence_count_and_reachable():
    p = InstancePresence()
    assert not p.reachable("rdfs#1")
    p.apply("instance.connected", "rdfs#1")
    assert p.reachable("rdfs#1")
    p.apply("instance.connected", "rdfs#1")   # 2nd connection
    p.apply("instance.disconnected", "rdfs#1")
    assert p.reachable("rdfs#1")              # still 1 open
    p.apply("instance.disconnected", "rdfs#1")
    assert not p.reachable("rdfs#1")


class _FakeConsumer:
    def __init__(self, msgs): self._m = msgs
    async def start(self): ...
    async def stop(self): ...
    async def __aiter__(self):
        for m in self._m: yield m


def _msg(event, iid): return SimpleNamespace(value=json.dumps({"event": event, "instance_id": iid}).encode())


@pytest.mark.asyncio
async def test_consumer_updates_presence_and_ignores_unrelated():
    p = InstancePresence()
    c = PresenceConsumer(_FakeConsumer([
        _msg("instance.connected", "rdfs#1"),
        _msg("tick_lag_warn", "rdfs#1"),       # unrelated health event -> ignored
        SimpleNamespace(value=b"not json"),    # malformed -> skipped
    ]), p)
    await c.run()
    assert p.reachable("rdfs#1")
```

- [ ] **Step 2: Run → fail.**
- [ ] **Step 3: Implement.** `presence.py`:
```python
"""In-memory per-instance connection presence (B1.43 §6.1). Rebuilt from
system.health on restart; not persisted."""
from __future__ import annotations


class InstancePresence:
    def __init__(self) -> None:
        self._open: dict[str, int] = {}

    def apply(self, event: str, instance_id: str) -> None:
        if event == "instance.connected":
            self._open[instance_id] = self._open.get(instance_id, 0) + 1
        elif event == "instance.disconnected":
            n = self._open.get(instance_id, 0) - 1
            if n > 0:
                self._open[instance_id] = n
            else:
                self._open.pop(instance_id, None)

    def reachable(self, instance_id: str) -> bool:
        return self._open.get(instance_id, 0) > 0
```
`presence_consumer.py` (mirror `ControlConsumer`):
```python
"""Consumes system.health and updates InstancePresence from drs-bridge
instance.connected / instance.disconnected events. Other events ignored."""
from __future__ import annotations

import json
import logging
from typing import Protocol

from drs_server.roster.presence import InstancePresence

logger = logging.getLogger(__name__)

_PRESENCE_EVENTS = {"instance.connected", "instance.disconnected"}


class KafkaConsumerLike(Protocol):
    async def start(self) -> None: ...
    async def stop(self) -> None: ...
    def __aiter__(self): ...


class PresenceConsumer:
    def __init__(self, consumer: KafkaConsumerLike, presence: InstancePresence) -> None:
        self._consumer = consumer
        self._presence = presence

    async def start(self) -> None: await self._consumer.start()
    async def stop(self) -> None: await self._consumer.stop()

    async def run(self) -> None:
        async for msg in self._consumer:
            try:
                body = json.loads(msg.value.decode("utf-8"))
            except Exception:
                logger.exception("failed to decode system.health message"); continue
            event = body.get("event")
            iid = body.get("instance_id")
            if event in _PRESENCE_EVENTS and isinstance(iid, str):
                self._presence.apply(event, iid)
```

- [ ] **Step 4: Run → pass.**
- [ ] **Step 5: Commit** — `feat(server): instance presence registry + system.health consumer (B1.43)`

### Task 2.3: wire presence consumer into lifespan

**Files:** Modify `lifespan.py`; Test `drs-server/tests/test_lifespan.py` or `test_main_construction.py`

- [ ] **Step 1: Failing test** — add to `test_main_construction.py` a check that `app.state` exposes presence after lifespan start (use the existing lifespan test harness pattern; if none, assert the attribute is set in `make_lifespan` via a minimal `TestClient(app)` context that injects fakes). Concretely, extend the existing time-sync lifespan integration test to assert `app.state.instance_presence` exists.

- [ ] **Step 2–3: Implement** — in `lifespan.py`: create `InstancePresence()` + a `PresenceConsumer` on the `system.health` topic (add a `presence_consumer_factory` param defaulting to a real `AIOKafkaConsumer("system.health", group_id="drs-server-presence", auto_offset_reset="latest")`), store `app.state.instance_presence`, start a `presence_task = asyncio.create_task(presence_consumer.run())`, and cancel/stop it in the `finally` block (mirror the control-consumer lifecycle exactly). Use `auto_offset_reset="latest"` — presence is *current* connections; replaying old connect/disconnect from earliest would be misleading.

- [ ] **Step 4: Run → pass** (full unit suite).
- [ ] **Step 5: Commit** — `feat(server): wire presence consumer into lifespan (B1.43)`

---

## Phase 3 — `/exercise/readiness` reconcile

### Task 3.1: readiness schema + endpoint

**Files:** Create `drs-server/src/drs_server/api/exercise.py`; Modify `schemas.py`, `main.py`; Test `drs-server/tests/roster/test_readiness.py`

- [ ] **Step 1: Failing test** — `drs-server/tests/roster/test_readiness.py`:
```python
from fastapi import FastAPI
from fastapi.testclient import TestClient
from drs_server.api import exercise as ex
from drs_server.roster.presence import InstancePresence
from drs_server.timesync.sync_state_engine import SyncStateEngine, SyncThresholds, SyncStatus


class _E:
    def __init__(self, iid, variant): self.instance_id, self.variant = iid, variant


class _Active:
    roster_id, version = "lab", 7
    entries = [_E("rdfs#1", "rdfs"), _E("jhf#1", "jhf")]


class _Repo:
    async def get_active(self): return _Active()


def _app(presence, engine):
    app = FastAPI(); app.include_router(ex.router)
    app.dependency_overrides[ex.get_repo] = lambda: _Repo()
    app.state.instance_presence = presence
    app.state.sync_state_engine = engine
    return app


def test_readiness_per_instance_verdict():
    presence = InstancePresence(); presence.apply("instance.connected", "rdfs#1")
    engine = SyncStateEngine(SyncThresholds())
    engine.register_variant(name="rdfs", precision_required_ms=10.0)
    engine._variants["rdfs"]["status"] = SyncStatus.HEALTHY   # force healthy for the test
    client = TestClient(_app(presence, engine))
    r = client.post("/exercise/readiness", json={"needs": ["rdfs#1", "jhf#1", "ghost#1"], "catalogue_etag": "lab@7"})
    assert r.status_code == 200
    body = r.json()
    by = {i["instance_id"]: i for i in body["instances"]}
    assert by["rdfs#1"] == {"instance_id": "rdfs#1", "configured": True, "reachable": True, "time_synced": True}
    assert by["jhf#1"]["configured"] is True and by["jhf#1"]["reachable"] is False   # jhf registered? no -> time_synced False
    assert by["ghost#1"]["configured"] is False
    assert body["roster_drift"] is False
    assert body["ready"] is False   # not all green


def test_readiness_flags_roster_drift():
    client = TestClient(_app(InstancePresence(), SyncStateEngine(SyncThresholds())))
    r = client.post("/exercise/readiness", json={"needs": ["rdfs#1"], "catalogue_etag": "lab@3"})
    assert r.json()["roster_drift"] is True   # authored @3 != active @7
```

- [ ] **Step 2: Run → fail.**
- [ ] **Step 3: Implement.** In `schemas.py` add:
```python
class ReadinessRequest(BaseModel):
    needs: list[str]
    catalogue_etag: Optional[str] = None
```
Create `api/exercise.py`:
```python
"""POST /exercise/readiness — launch-time reconcile (B1.43 §6.4). Per required
instance: configured (in active roster) + reachable (connection-presence) +
time_synced (variant SyncStatus == HEALTHY). Plus roster-drift vs the active ETag."""
from __future__ import annotations

from fastapi import APIRouter, Depends, HTTPException, Request

from drs_server.roster import projections
from drs_server.roster.repository import RosterRepository
from drs_server.roster.schemas import ReadinessRequest
from drs_server.timesync.sync_state_engine import SyncStatus

router = APIRouter(prefix="/exercise", tags=["exercise"])


async def get_repo(request: Request) -> RosterRepository:
    factory = request.app.state.session_factory
    async with factory() as session:
        yield RosterRepository(session)


@router.post("/readiness")
async def readiness(body: ReadinessRequest, request: Request, repo: RosterRepository = Depends(get_repo)):
    active = await repo.get_active()
    if active is None:
        raise HTTPException(status_code=409, detail="no active roster")
    presence = request.app.state.instance_presence
    engine = request.app.state.sync_state_engine
    by_id = {e.instance_id: e for e in active.entries}

    async def _time_synced(variant: str) -> bool:
        try:
            return (await engine.current_variant_status(variant)) == SyncStatus.HEALTHY
        except KeyError:
            return False

    instances = []
    for iid in body.needs:
        entry = by_id.get(iid)
        configured = entry is not None
        instances.append({
            "instance_id": iid,
            "configured": configured,
            "reachable": presence.reachable(iid),
            "time_synced": (await _time_synced(entry.variant)) if configured else False,
        })
    roster_drift = body.catalogue_etag is not None and body.catalogue_etag != projections.etag(active)
    ready = bool(instances) and all(
        i["configured"] and i["reachable"] and i["time_synced"] for i in instances
    ) and not roster_drift
    return {"instances": instances, "roster_drift": roster_drift, "ready": ready,
            "active_etag": projections.etag(active)}
```
Mount in `main.py`: `from drs_server.api import exercise as exercise_api` + `app.include_router(exercise_api.router)`.

- [ ] **Step 4: Run → pass.**
- [ ] **Step 5: Commit** — `feat(server): /exercise/readiness reconcile (configured/reachable/time_synced + drift) (B1.43)`

### Task 3.2: readiness breadth — ready=true path

- [ ] **Step 1: Add test** to `test_readiness.py` asserting `ready: True` when all needed instances are configured + reachable + healthy + no drift (register both variants healthy, mark both present, matching etag). **Run → pass** (logic already supports it). **Commit** — `test(server): readiness all-green ready=true breadth (B1.43)`

---

## Phase 4 — Port-allocation engine

### Task 4.1: allocator + relax validation for `allocated`

**Files:** Modify `validation.py`, `repository.py`, `config.py`; Create `drs-server/src/drs_server/roster/allocation.py`; Tests

- [ ] **Step 1: Failing test** — `drs-server/tests/roster/test_allocation.py`:
```python
import pytest
from drs_server.roster.allocation import allocate_ports
from drs_server.roster.schemas import RosterIn, RosterEntryIn, Endpoint


def _e(iid, port, source="irs_fixed"):
    return RosterEntryIn(instance_id=iid, variant="rdfs", host="127.0.0.1",
                         command=Endpoint(port=port, protocol="tcp"),
                         response=Endpoint(port=(port + 1 if port else None), protocol="udp"),
                         port_source=source, enabled=True)


def test_allocates_missing_ports_avoiding_taken():
    r = RosterIn(roster_id="lab", name="lab", entries=[
        _e("rdfs#1", 5500, "irs_fixed"),                 # explicit, occupies 5500
        _e("rdfs#2", None, "allocated"),                 # needs command port
        _e("rdfs#3", None, "allocated"),
    ])
    allocate_ports(r, base=5500)
    ports = [e.command.port for e in r.entries]
    assert ports[0] == 5500
    assert ports[1] == 5501 and ports[2] == 5502        # next free from base, skipping 5500
    assert all(e.command.port is not None for e in r.entries)
    assert all(e.response.port is not None for e in r.entries)   # response filled too
```

- [ ] **Step 2: Run → fail.**
- [ ] **Step 3: Implement** `allocation.py`:
```python
"""Deterministic port allocation for `allocated` roster entries that omit a
port. Assigns the lowest free command port from `base`, avoiding ports already
taken by any entry (explicit or previously allocated). Response port = command+1
when missing. (B1.43 §9 — Plan 2b.)"""
from __future__ import annotations

from drs_server.roster.schemas import RosterIn


def allocate_ports(roster: RosterIn, base: int = 5500) -> None:
    taken: set[int] = {e.command.port for e in roster.entries if e.command.port is not None}
    taken |= {e.response.port for e in roster.entries if e.response.port is not None}
    nxt = base

    def _free() -> int:
        nonlocal nxt
        while nxt in taken:
            nxt += 1
        taken.add(nxt)
        return nxt

    for e in roster.entries:
        if e.command.port is None:
            e.command.port = _free()
        if e.response.port is None:
            e.response.port = _free()
```
In `validation.py`, **remove** the "allocated must carry an explicit port (Plan 2b)" block (the `if e.command.port is None: raise ...` that wasn't the irs_fixed one) so allocator-filled entries validate; keep the `irs_fixed` rule. In `config.py` add `allocated_port_base: int = 5500` (env `DRS_SERVER_ALLOCATED_PORT_BASE`). In `repository.py::upsert`, call `allocate_ports(roster_in, base)` **before** `validate_roster` (so collision validation sees the filled ports) — the base comes from settings; thread it via the repo constructor: `RosterRepository(session, allocated_port_base=...)` defaulting to 5500, and pass the setting where the repo is constructed (the `get_repo` provider reads `request.app.state` — add `allocated_port_base` to app.state in lifespan, default 5500).

- [ ] **Step 4: Run → pass** (allocation + validation + repository tests; the Plan-2a integration test that used explicit ports still passes — allocator is a no-op when ports are present).
- [ ] **Step 5: Commit** — `feat(server): port-allocation engine for allocated entries (B1.43)`

---

## Phase 5 — Integration (real broker + DB, graceful-skip)

### Task 5.1: end-to-end — PUT roster publishes a consumable `drs.roster` snapshot

**Files:** `drs-server/tests/roster/test_publish_integration.py`

- [ ] **Step 1: Write the test** (capability-probe Kafka; skip if absent):
```python
import asyncio, json, socket, uuid
import pytest

pytest.importorskip("aiokafka")
from aiokafka import AIOKafkaConsumer, AIOKafkaProducer  # noqa: E402

from drs_server.roster.publisher import RosterPublisher


def _kafka() -> bool:
    try:
        with socket.create_connection(("localhost", 9092), timeout=1.0): return True
    except OSError: return False


class _E:
    instance_id, variant, host = "rdfs#1", "rdfs", "10.0.0.5"
    command_port, command_protocol = 5001, "tcp"
    response_port, response_protocol = 5002, "udp"
    port_source, enabled = "irs_fixed", True


@pytest.mark.asyncio
async def test_published_snapshot_lands_on_drs_roster():
    if not _kafka():
        pytest.skip("Kafka broker not reachable; see infrastructure/kafka/README.md")
    rid = f"lab_{uuid.uuid4().hex[:8]}"

    class _R:
        roster_id, version, entries = rid, 9, [_E()]

    producer = AIOKafkaProducer(bootstrap_servers="localhost:9092")
    consumer = AIOKafkaConsumer("drs.roster", bootstrap_servers="localhost:9092",
                                group_id=f"t_{uuid.uuid4().hex[:8]}", auto_offset_reset="earliest")
    await producer.start(); await consumer.start()
    try:
        await RosterPublisher(producer).publish(_R())

        async def _find():
            async for m in consumer:
                if m.key == b"active":
                    d = json.loads(m.value.decode())
                    if d["roster_id"] == rid:
                        return d
        got = await asyncio.wait_for(_find(), timeout=15.0)
        assert got["version"] == 9
        assert got["entries"][0]["command"]["port"] == 5001   # drs-bridge-consumable shape
    finally:
        await consumer.stop(); await producer.stop()
```

- [ ] **Step 2: Run** (with a broker up): PASS; without: SKIP. `[integration]`
- [ ] **Step 3: Run the full drs-server + drs-bridge suites** (PG + Kafka up) — expect all green.
- [ ] **Step 4: Commit** — `test(server): drs.roster publish lands consumable snapshot, graceful-skip (B1.43)`

---

## Phase 6 — Contract + docs

### Task 6.1: `/exercise/readiness` OpenAPI + docs/backlog

**Files:** `contracts/drs-server-openapi.yaml`, `contracts/README.md`, spec §9/§6.4, `design-backlog.md`

- [ ] **Step 1:** Regenerate the OpenAPI (`drs-server/.venv/Scripts/python.exe -c "import json; from drs_server.main import app; print(json.dumps(app.openapi()))"`) and curate the `/exercise/readiness` path + `ReadinessRequest` schema into `contracts/drs-server-openapi.yaml`; move `/exercise/readiness` from "planned" to baselined in `contracts/README.md`.
- [ ] **Step 2:** Spec: mark §6.4 reconcile + §6.2 publisher + §9 port-allocation as implemented (Plan 2b). Backlog B1.43 Status: Plan 2b landed (publisher + presence + reconcile + allocator); only Plan 3 (UI) remains. Update the README plans index.
- [ ] **Step 3: Commit** — `docs(B1.43): record Plan 2b (publisher + presence + reconcile + allocator) landing`

---

## Definition of done (Plan 2b)

- drs-server publishes the active roster to `drs.roster` (key `active`) on every write + at startup; the snapshot shape matches `contracts/kafka/drs.roster.schema.json` and drs-bridge's consumer.
- drs-bridge emits `instance.connected`/`instance.disconnected`; drs-server tracks presence and serves `POST /exercise/readiness` with per-instance `{configured, reachable, time_synced}` + `roster_drift` + `ready`.
- `allocated` entries without a port are filled deterministically.
- Both suites green (PG + Kafka up); the publish integration test passes locally, skips without a broker. `/exercise/readiness` in the OpenAPI.

## Follow-on

- **Plan 3** — Sg.App readiness panel + DRS-webapp IP/network-config editing surface (after [B1.1](../design-backlog.md) wireframes), consuming `/roster/*` + `/exercise/readiness`.
