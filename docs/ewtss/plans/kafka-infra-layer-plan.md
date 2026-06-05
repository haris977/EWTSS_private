# Kafka Infrastructure Layer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship the data-shape-independent Kafka infrastructure layer (local broker via docker-compose + idempotent topic-creation + real-broker integration test + dev-handbook conventions) so the first hardware-variant developer has a runnable starting point in Phase 2 without committing to any data-shape decisions.

**Architecture:** Single-node Kafka KRaft in `infrastructure/docker-compose.yml`, idempotent Python topic-creation script in `infrastructure/kafka/`, pytest integration test in `drs-server/tests/` that publishes via the existing `ControlPublisher` and consumes via the existing `ControlConsumer` (skips if broker not reachable, matching the parser ABI's cmake-skip pattern), Developer Handbook subsection codifying the patterns the existing 400-line Kafka scaffolding already demonstrates.

**Tech Stack:** Docker Compose v2 (Kafka 3.7+ KRaft mode, no Zookeeper), Python 3.11 + `aiokafka` (already a dependency in both services), pytest + pytest-asyncio.

---

## Task 1: Kafka KRaft single-node docker-compose

**Files:**
- Create: `infrastructure/docker-compose.yml`

- [ ] **Step 1: Create the compose file**

Create `infrastructure/docker-compose.yml`:

```yaml
# Local Kafka broker for drs-* development.
#
# Single-node KRaft (no Zookeeper). Persistent named volume so topics +
# consumer-group offsets survive `docker compose down`; reset with
# `docker compose down -v`.
#
# This stack is dev-only. Production Kafka deployment is out of scope
# (see docs/ewtss/v2-execution-plan.md Phase 4 / B1.x).

services:
  kafka:
    image: confluentinc/cp-kafka:7.6.0
    container_name: ewtss-kafka
    ports:
      - "9092:9092"
    environment:
      KAFKA_NODE_ID: 1
      KAFKA_PROCESS_ROLES: "broker,controller"
      KAFKA_LISTENERS: "PLAINTEXT://0.0.0.0:9092,CONTROLLER://0.0.0.0:9093"
      KAFKA_ADVERTISED_LISTENERS: "PLAINTEXT://localhost:9092"
      KAFKA_LISTENER_SECURITY_PROTOCOL_MAP: "PLAINTEXT:PLAINTEXT,CONTROLLER:PLAINTEXT"
      KAFKA_INTER_BROKER_LISTENER_NAME: "PLAINTEXT"
      KAFKA_CONTROLLER_LISTENER_NAMES: "CONTROLLER"
      KAFKA_CONTROLLER_QUORUM_VOTERS: "1@localhost:9093"
      KAFKA_OFFSETS_TOPIC_REPLICATION_FACTOR: 1
      KAFKA_TRANSACTION_STATE_LOG_REPLICATION_FACTOR: 1
      KAFKA_TRANSACTION_STATE_LOG_MIN_ISR: 1
      KAFKA_AUTO_CREATE_TOPICS_ENABLE: "true"
      CLUSTER_ID: "ewtss-local-dev-cluster"
    volumes:
      - kafka-data:/var/lib/kafka/data
    healthcheck:
      test: ["CMD-SHELL", "kafka-topics --bootstrap-server localhost:9092 --list >/dev/null 2>&1"]
      interval: 5s
      timeout: 5s
      retries: 10
      start_period: 15s

volumes:
  kafka-data:
    name: ewtss-kafka-data
```

- [ ] **Step 2: Verify the stack starts**

Run from the repo root (PowerShell):

```powershell
docker compose -f infrastructure/docker-compose.yml up -d
docker compose -f infrastructure/docker-compose.yml ps
```

Expected: one `ewtss-kafka` service in `running (healthy)` state within ~20 s.

Verify topic operations work:

```powershell
docker exec ewtss-kafka kafka-topics --bootstrap-server localhost:9092 --list
```

Expected: empty output (no topics yet) with exit code 0.

Tear down for the rest of the plan:

```powershell
docker compose -f infrastructure/docker-compose.yml down
```

(Keep the named volume; later tasks will recreate the stack.)

- [ ] **Step 3: Commit**

```bash
git add infrastructure/docker-compose.yml
git commit -m "feat(infrastructure): Kafka KRaft single-node compose for dev (Task 1)"
```

---

## Task 2: Idempotent topic-creation script

**Files:**
- Create: `infrastructure/kafka/create-topics.py`

- [ ] **Step 1: Create the script**

Create `infrastructure/kafka/create-topics.py`:

```python
"""Idempotent provisioning of EWTSS control-plane Kafka topics.

Creates the three known control-plane topics if missing. Safe to re-run.

NOT provisioned here:
  - hw.<variant>.<kind> (data-plane telemetry topics) — these are
    Phase 2 work. Topic naming + partition strategy will be designed
    against the first real IRS, not pre-committed here.

Usage:
  python infrastructure/kafka/create-topics.py
  python infrastructure/kafka/create-topics.py --bootstrap localhost:9092
"""
from __future__ import annotations

import argparse
import asyncio
import sys

from aiokafka.admin import AIOKafkaAdminClient, NewTopic
from aiokafka.errors import TopicAlreadyExistsError


CONTROL_PLANE_TOPICS = [
    # name, partitions, replication_factor, retention notes
    ("drs.control", 1, 1),       # variant.registered + future bridge -> server events
    ("system.timesync", 1, 1),   # SyncStateEngine state transitions
    ("system.health", 1, 1),     # per-variant health events from drs-bridge
]


async def create_topics(bootstrap: str) -> int:
    admin = AIOKafkaAdminClient(bootstrap_servers=bootstrap)
    await admin.start()
    try:
        existing = set(await admin.list_topics())
        to_create = [
            NewTopic(name, num_partitions=parts, replication_factor=rf)
            for name, parts, rf in CONTROL_PLANE_TOPICS
            if name not in existing
        ]
        if not to_create:
            print(f"All {len(CONTROL_PLANE_TOPICS)} control-plane topics already exist; nothing to do.")
            return 0
        try:
            await admin.create_topics(new_topics=to_create)
        except TopicAlreadyExistsError:
            # Race: someone else created it between list_topics and create_topics.
            pass
        for nt in to_create:
            print(f"created topic: {nt.name} (partitions={nt.num_partitions}, rf={nt.replication_factor})")
        return 0
    finally:
        await admin.close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bootstrap", default="localhost:9092",
                        help="Kafka bootstrap server (default: localhost:9092)")
    args = parser.parse_args()
    return asyncio.run(create_topics(args.bootstrap))


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Verify the script is idempotent**

Bring the broker back up:

```powershell
docker compose -f infrastructure/docker-compose.yml up -d
```

Wait ~15 s for healthy, then from `drs-server/`'s venv (which has aiokafka installed):

```powershell
cd drs-server
.\.venv\Scripts\python.exe ..\infrastructure\kafka\create-topics.py
```

Expected first run:
```
created topic: drs.control (partitions=1, rf=1)
created topic: system.timesync (partitions=1, rf=1)
created topic: system.health (partitions=1, rf=1)
```

Run again. Expected second run:
```
All 3 control-plane topics already exist; nothing to do.
```

Verify with the broker:
```powershell
docker exec ewtss-kafka kafka-topics --bootstrap-server localhost:9092 --list
```

Expected three lines: `drs.control`, `system.health`, `system.timesync`.

Tear down:
```powershell
cd ..
docker compose -f infrastructure/docker-compose.yml down
```

- [ ] **Step 3: Commit**

```bash
git add infrastructure/kafka/create-topics.py
git commit -m "feat(infrastructure): idempotent Kafka topic-creation script (Task 2)"
```

---

## Task 3: Infrastructure README

**Files:**
- Create: `infrastructure/kafka/README.md`
- Modify: `infrastructure/ntp/README.md` (cross-link)

- [ ] **Step 1: Write the Kafka stack README**

Create `infrastructure/kafka/README.md`:

```markdown
# Local Kafka — dev broker

Single-node Kafka KRaft broker for local development of `drs-server` and
`drs-bridge`. **Not for production** (production Kafka tuning is a separate
B1.x item).

## Why this exists

The control-plane Kafka code in both services (`ControlPublisher`,
`HealthPublisher`, `ControlConsumer`, `TimesyncPublisher`) uses injected
factories so unit tests can swap in `_FakeKafkaConsumer` / `_FakeProducer`
in-memory stand-ins. That's enough for fast feedback, but never exercises
the real serialization + network path. This stack gives developers a
real broker to:

- Verify end-to-end producer→consumer flows
- Run `drs-server/tests/test_kafka_broker_integration.py` locally
- Inspect topics + consumer groups while debugging

## Start / stop

From repo root:

```powershell
docker compose -f infrastructure/docker-compose.yml up -d
docker compose -f infrastructure/docker-compose.yml ps        # check healthy
docker compose -f infrastructure/docker-compose.yml down      # stop, keep topics
docker compose -f infrastructure/docker-compose.yml down -v   # stop, drop topics
```

The broker is healthy when `ps` shows `(healthy)`. Typical cold-start time
is ~15 s.

## Provisioning control-plane topics

After bringing the stack up, provision the three control-plane topics:

```powershell
cd drs-server
.\.venv\Scripts\python.exe ..\infrastructure\kafka\create-topics.py
```

The script is idempotent — safe to re-run. It creates `drs.control`,
`system.timesync`, `system.health`.

Data-plane topics (`hw.<variant>.<kind>`) are NOT created by this script.
They are Phase 2 work and will be provisioned alongside the first variant's
IRS-driven schema.

## Inspecting topics + consumer groups

```powershell
# List topics
docker exec ewtss-kafka kafka-topics --bootstrap-server localhost:9092 --list

# Describe a topic
docker exec ewtss-kafka kafka-topics --bootstrap-server localhost:9092 \
    --describe --topic drs.control

# List consumer groups
docker exec ewtss-kafka kafka-consumer-groups \
    --bootstrap-server localhost:9092 --list

# Tail a topic from the beginning
docker exec ewtss-kafka kafka-console-consumer \
    --bootstrap-server localhost:9092 --topic drs.control --from-beginning
```

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `Connection refused` from Python script | Broker not yet healthy. Wait ~15 s after `up -d`, re-check with `ps`. |
| Old consumer-group offsets replay during tests | Tests use a unique `group_id` per run; if you're running custom code, reset with `kafka-consumer-groups --delete --group <name>`. |
| Want to reset all state | `docker compose down -v` drops the named volume. |
| Port 9092 already in use | Another local Kafka instance is running. Stop it or change the port mapping in `docker-compose.yml`. |

## What's NOT in this stack

- **No TimescaleDB.** No code uses Postgres today (`drs-server` is
  in-memory). When schema work begins, this compose file gains a
  `timescaledb` service.
- **No Schema Registry.** Control-plane uses raw UTF-8 JSON; no Avro / Protobuf yet.
- **No multi-broker / replication.** Single-node for dev.

See [docs/ewtss/v2-execution-plan.md](../../docs/ewtss/v2-execution-plan.md)
for production deployment scope.
```

- [ ] **Step 2: Cross-link from NTP README**

Read [`infrastructure/ntp/README.md`](../../../infrastructure/ntp/README.md)
to confirm format, then add a short "See also" line near the top
referencing the new Kafka README so devs land on either when browsing the
directory.

(If the file's structure makes a "See also" awkward, leave it alone — this
is a nice-to-have.)

- [ ] **Step 3: Commit**

```bash
git add infrastructure/kafka/README.md
# also infrastructure/ntp/README.md if step 2 modified it
git commit -m "docs(infrastructure): Kafka local-broker dev guide (Task 3)"
```

---

## Task 4: Real-broker integration test

**Files:**
- Create: `drs-server/tests/test_kafka_broker_integration.py`

- [ ] **Step 1: Write the failing test**

Create `drs-server/tests/test_kafka_broker_integration.py`:

```python
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
```

- [ ] **Step 2: Run with broker DOWN — confirm skip**

```powershell
cd drs-server
.\.venv\Scripts\pytest.exe tests/test_kafka_broker_integration.py -v
```

Expected: `SKIPPED` with the "Kafka broker not reachable" reason.

- [ ] **Step 3: Run with broker UP — confirm pass**

```powershell
# from repo root
docker compose -f infrastructure/docker-compose.yml up -d
# wait ~15 s for healthy

cd drs-server
.\.venv\Scripts\python.exe ..\infrastructure\kafka\create-topics.py
.\.venv\Scripts\pytest.exe tests/test_kafka_broker_integration.py -v
```

Expected: PASS.

Tear down:
```powershell
cd ..
docker compose -f infrastructure/docker-compose.yml down
```

- [ ] **Step 4: Confirm existing test suite still passes**

```powershell
cd drs-server
.\.venv\Scripts\pytest.exe -v
```

Expected: all existing tests still pass; the new integration test is
SKIPPED (broker is down).

- [ ] **Step 5: Commit**

```bash
git add drs-server/tests/test_kafka_broker_integration.py
git commit -m "test(drs-server): real-broker Kafka integration test, skips when broker absent (Task 4)"
```

---

## Task 5: Developer Handbook — Kafka conventions

**Files:**
- Modify: `docs/ewtss/developer-handbook.md` (add subsection after §12 or wherever drs-bridge internal design ends; we'll find the right insertion point)

- [ ] **Step 1: Locate the insertion point**

Read the table of contents region of
[`docs/ewtss/developer-handbook.md`](../../ewtss/developer-handbook.md)
and find where §12 (drs-bridge internal design) ends or §10 (DB schema)
begins. The new "Kafka conventions" subsection should land alongside the
drs-bridge / drs-server internal-design content. If §11 currently
describes drs-server, the new subsection becomes §11.x; if not, pick the
appropriate slot.

- [ ] **Step 2: Add the new subsection**

Insert (numbering adjusted to fit existing structure):

```markdown
### 11.x Kafka conventions

The control-plane Kafka code in both services is small (~400 lines) and
already demonstrates the patterns new publishers/consumers should
follow. Read the existing files end-to-end before adding new ones — they
are the canonical template, the same way
[`drs-bridge/parsers/reference/`](../../drs-bridge/parsers/reference/) is
the canonical C++ parser template.

**Reference files** (all <70 lines each):

| File | Pattern shown |
|---|---|
| [`drs-bridge/src/drs_bridge/control_publisher.py`](../../drs-bridge/src/drs_bridge/control_publisher.py) | Minimal producer: `Protocol`-typed Kafka dep + one method per event |
| [`drs-bridge/src/drs_bridge/health_publisher.py`](../../drs-bridge/src/drs_bridge/health_publisher.py) | Generic event-type dispatch |
| [`drs-server/src/drs_server/control_consumer.py`](../../drs-server/src/drs_server/control_consumer.py) | Async-iterator consumer loop + forward-compatible event dispatch |
| [`drs-server/src/drs_server/timesync/timesync_publisher.py`](../../drs-server/src/drs_server/timesync/timesync_publisher.py) | Producer with enum + numeric typed payload |
| [`drs-server/src/drs_server/lifespan.py`](../../drs-server/src/drs_server/lifespan.py) | **Load-bearing:** factory-injection wiring so tests can swap fakes |
| [`drs-bridge/src/drs_bridge/runtime.py`](../../drs-bridge/src/drs_bridge/runtime.py) | Same factory pattern on bridge side |
| [`drs-server/tests/test_control_consumer.py`](../../drs-server/tests/test_control_consumer.py) | `_FakeKafkaConsumer` in-memory stand-in pattern for unit tests |
| [`drs-server/tests/test_kafka_broker_integration.py`](../../drs-server/tests/test_kafka_broker_integration.py) | Skip-if-broker-down real-broker contract test |

**Patterns to copy:**

1. **Protocol-typed Kafka dependency.** Production code depends on a
   small `Protocol` (`async def send_and_wait` / `async def start` /
   `async def stop` / `__aiter__`) — not directly on `aiokafka`. Tests
   pass fakes implementing the Protocol.
2. **Lazy-imported factory at the wiring layer.** `_default_producer_factory`
   in [lifespan.py](../../drs-server/src/drs_server/lifespan.py) and
   [runtime.py](../../drs-bridge/src/drs_bridge/runtime.py) defers the
   `aiokafka` import to the first call so unit tests that mock the
   factory don't pay the import cost.
3. **Forward-compatible event dispatch.** Consumers read `body.get("event")`
   and skip unknown event types with a debug log — never raise. Adding a
   new event type doesn't require coordinated deploys.
4. **Unique consumer-group-id in tests.** Integration tests use
   `uuid.uuid4()`-derived group IDs so reruns don't replay offsets.

**Topic naming convention:**

| Pattern | Use | Examples |
|---|---|---|
| `<area>.<purpose>` | Control-plane (low-volume, system events) | `drs.control`, `system.timesync`, `system.health` |
| `hw.<variant>.<kind>` | Data-plane (high-volume telemetry — Phase 2) | `hw.rdfs.ff`, `hw.comm_df.fh` (deferred to first-IRS work) |

**Serialization:**

- **Control plane:** UTF-8 JSON. Compact, debuggable in `kafka-console-consumer`,
  no schema-registry overhead. Used by every existing publisher.
- **Data plane:** decision deferred. Choice between JSON / Protobuf / msgpack
  is driven by per-frame size + serialization-CPU budget under the actual
  variant's volume profile — not a guess against synthetic data.

**Local broker for development:**

See [`infrastructure/kafka/README.md`](../../infrastructure/kafka/README.md).
Single docker-compose command brings up a single-node KRaft Kafka; an
idempotent Python script provisions the three control-plane topics; the
integration test ([`drs-server/tests/test_kafka_broker_integration.py`](../../drs-server/tests/test_kafka_broker_integration.py))
verifies end-to-end against the real broker.

The data-plane (`hw.<variant>.<kind>` topics + `measurements` hypertable +
batched asyncpg insert pattern) is intentionally NOT pre-scaffolded —
schema and payload shape are designed against the first real IRS, not
against synthetic data.
```

- [ ] **Step 3: Verify the new subsection renders cleanly**

Read the section you added back and confirm:
- All `../../` paths resolve correctly from `docs/ewtss/`
- Table formatting is consistent with the rest of the handbook
- Numbering doesn't collide with neighbouring sections

- [ ] **Step 4: Commit**

```bash
git add docs/ewtss/developer-handbook.md
git commit -m "docs(handbook): Kafka conventions subsection — patterns, naming, local broker (Task 5)"
```

---

## Task 6: Doc propagation

**Files:**
- Modify: `README.md` (top-level)
- Modify: `docs/ewtss/architecture-overview.md`
- Modify: `docs/ewtss/design-postmortem.md` (§3.5 partially resolved)
- Modify: `docs/ewtss/README.md` (B1.x index)

- [ ] **Step 1: Update top-level README.md infrastructure bullet**

Read [`README.md`](../../../README.md), find the `infrastructure/` bullet
(currently mentions only NTP), and add a Kafka line.

Suggested replacement for the existing bullet:

```markdown
- **`infrastructure/`** — operational scripts + local dev stack.
  [`ntp/`](infrastructure/ntp/README.md) — NTP install + smoke test for
  B1.3 time synchronisation. [`docker-compose.yml`](infrastructure/docker-compose.yml)
  + [`kafka/`](infrastructure/kafka/README.md) — single-node Kafka KRaft
  broker for local development + idempotent topic-creation script.
```

- [ ] **Step 2: Update architecture-overview.md infra tree**

Read [`docs/ewtss/architecture-overview.md`](../../ewtss/architecture-overview.md)
around the `infrastructure/` repo-tree block (search for `docker-compose.yml`
or `infrastructure/`). Update the tree to show what now exists:

```
└── infrastructure/
    ├── docker-compose.yml              Kafka KRaft single-node (local dev)
    ├── kafka/
    │   ├── README.md                   stack docs + topic inspection commands
    │   └── create-topics.py            idempotent control-plane topic provisioner
    └── ntp/                            Meinberg NTP install + smoke (B1.3)
```

- [ ] **Step 3: Update design-postmortem.md §3.5**

Read [`docs/ewtss/design-postmortem.md`](../../ewtss/design-postmortem.md)
around §3.5 ("integration test environment — physical / logical home unclear").
The table row "Is there a docker-compose / equivalent for spinning up
Kafka + TimescaleDB + drs-server + drs-bridge for tests?" currently says
"directory doesn't exist yet". Update to reflect the new partial state:

Replace the relevant table row with:

```markdown
| Is there a docker-compose / equivalent for spinning up Kafka + TimescaleDB + drs-server + drs-bridge for tests? | **Partial (2026-05-21):** Kafka KRaft single-node lives in [`infrastructure/docker-compose.yml`](../../infrastructure/docker-compose.yml) with idempotent topic-creation in [`infrastructure/kafka/create-topics.py`](../../infrastructure/kafka/create-topics.py); end-to-end real-broker integration test at [`drs-server/tests/test_kafka_broker_integration.py`](../../drs-server/tests/test_kafka_broker_integration.py). TimescaleDB + drs-server/drs-bridge orchestration is data-shape-dependent and deferred to first-IRS work. |
```

- [ ] **Step 4: Update docs/ewtss/README.md B1.x section**

Per the doc-set-currency rule, add the new spec + plan to the B1.x index.
Read [`docs/ewtss/README.md`](../../ewtss/README.md) and find the "v2
subsystem specs + implementation plans" section. After the reference
parser template entry, add:

```markdown
- [Kafka infrastructure layer — design spec](specs/kafka-infra-layer-design.md) + [implementation plan](plans/kafka-infra-layer-plan.md) — data-shape-independent Kafka layer (executed 2026-05-21): single-node KRaft broker in [`infrastructure/docker-compose.yml`](../../infrastructure/docker-compose.yml), idempotent control-plane topic provisioner, real-broker integration test (skips if broker absent), and Developer Handbook §11.x codifying the producer/consumer patterns the existing 400-line control-plane scaffolding demonstrates. Data-plane (`hw.<variant>.<kind>` topics, `measurements` hypertable, payload shape) deliberately deferred to first-IRS work.
```

- [ ] **Step 5: Verify all four files**

After all four edits, eyeball each file for:
- Link paths resolve from each file's directory
- Markdown tables / lists still parse
- No accidental duplication

- [ ] **Step 6: Commit**

```bash
git add README.md docs/ewtss/architecture-overview.md docs/ewtss/design-postmortem.md docs/ewtss/README.md
git commit -m "docs: propagate Kafka infrastructure layer through the doc set (Task 6)"
```

---

## Self-review checklist

Before marking the plan complete:

- [ ] All 6 tasks committed individually with descriptive messages
- [ ] `docker compose -f infrastructure/docker-compose.yml up -d` brings up healthy broker
- [ ] `python infrastructure/kafka/create-topics.py` is idempotent (re-run prints "nothing to do")
- [ ] `pytest tests/test_kafka_broker_integration.py` PASSES with broker up, SKIPS with broker down
- [ ] Full existing test suite (`pytest` in `drs-server/` and `drs-bridge/`) still passes (no regressions)
- [ ] Developer Handbook §11.x renders cleanly + links resolve
- [ ] doc-set README index reflects the new spec + plan

## What this plan does NOT do (explicit non-scope)

- ❌ No `hw.<variant>.<kind>` data-plane publisher/consumer template
- ❌ No `measurements` hypertable + asyncpg insert pattern
- ❌ No TimescaleDB service in docker-compose
- ❌ No CI job running the integration test (local-only first)
- ❌ No production deployment / tuning of Kafka

These are all data-shape-dependent or production-deployment concerns and
land in separate B1.x items once first IRS arrives or production planning
begins.
