# Kafka Infrastructure Layer — Design Spec

**Date:** 2026-05-21
**Status:** Approved (user-confirmed scope 2026-05-21)
**Author:** architecture review

## 1. Goal

Ship the **data-shape-independent** Kafka infrastructure layer so the first
hardware-variant developer (Phase 2 of v2 execution plan) has a local broker
+ topic conventions + a real-broker integration test to copy from. Same
"close the contract before first-dev-touches-it" motivation that drove the
[reference C++ parser template](../plans/reference-parser-template.md)
three days ago.

**Explicit non-goal:** any schema or payload-shape decisions that depend on
real IRS data. Those wait for the first IRS document. Customer-confirmed
caveat 2026-05-21: *the actual data from the customer's hardware will differ
from the legacy v1 reference; this infrastructure layer must remain
independent of any specific data shape.*

## 2. Context

### 2.1 What exists today

Control-plane Kafka producers + consumers are scaffolded in both services
(~400 lines total):

- [`drs-bridge/src/drs_bridge/control_publisher.py`](../../../drs-bridge/src/drs_bridge/control_publisher.py)
  publishes `variant.registered` events to `drs.control`
- [`drs-bridge/src/drs_bridge/health_publisher.py`](../../../drs-bridge/src/drs_bridge/health_publisher.py)
  publishes per-variant health events to `system.health`
- [`drs-server/src/drs_server/control_consumer.py`](../../../drs-server/src/drs_server/control_consumer.py)
  consumes `drs.control` and dispatches to `SyncStateEngine.register_variant`
- [`drs-server/src/drs_server/timesync/timesync_publisher.py`](../../../drs-server/src/drs_server/timesync/timesync_publisher.py)
  publishes `system.timesync` state transitions
- Factory-injection pattern in both
  [`drs-server/src/drs_server/lifespan.py`](../../../drs-server/src/drs_server/lifespan.py)
  and
  [`drs-bridge/src/drs_bridge/runtime.py`](../../../drs-bridge/src/drs_bridge/runtime.py)
- Unit tests use `_FakeKafkaConsumer` / `_FakeProducer` in-memory stand-ins
  (no broker required)

### 2.2 What's missing — and which gaps this spec covers

| Gap | Covered here? |
|---|---|
| No local Kafka broker (devs can't exercise `aiokafka` against a real broker) | **Yes** |
| No topic-creation convention (`drs.control`, `system.timesync`, `system.health` only exist as string literals in code) | **Yes** |
| No real-broker integration test (the Kafka equivalent of the parser ABI's `test_reference_parser_integration.py`) | **Yes** |
| Patterns for new publishers/consumers (Protocol typing, factory injection, fake-test scaffold) not documented as conventions | **Yes** |
| Data-plane `hw.<variant>.<kind>` publisher/consumer template | **No — data-shape-dependent** |
| `measurements` hypertable schema + `asyncpg` batched-insert pattern | **No — data-shape-dependent** |
| TimescaleDB local stack | **No — defer with the schema work** |
| CI integration of real-broker test | **No — local-only first; CI is a follow-up** |

## 3. Architecture

### 3.1 New artefacts

```
infrastructure/
  docker-compose.yml              ← Kafka KRaft single-node (NEW)
  kafka/
    README.md                     ← stack docs (NEW)
    create-topics.py              ← idempotent topic provisioning (NEW)
  ntp/                            ← unchanged
```

Tests:
```
drs-server/tests/
  test_kafka_broker_integration.py  ← end-to-end via real broker (NEW)
```

Docs:
```
docs/ewtss/developer-handbook.md  ← new §11.x "Kafka conventions" subsection
docs/ewtss/README.md              ← B1.x index updated
docs/ewtss/design-postmortem.md   ← §3.5 partially resolved
docs/ewtss/architecture-overview.md ← infra tree updated
README.md                         ← infrastructure/ bullet updated
```

### 3.2 Component design

**docker-compose.yml** — single Kafka KRaft broker (no Zookeeper, single-node
quorum). Bind: `localhost:9092`. Named volume so topics survive `down`/`up`;
`docker compose down -v` resets. Healthcheck via `kafka-topics --list`.

**`infrastructure/kafka/create-topics.py`** — Python script using `aiokafka`'s
admin client. Idempotent (creates only if missing). Creates the three known
control-plane topics today (`drs.control`, `system.timesync`, `system.health`);
documents in code-comments that data-plane `hw.<variant>.<kind>` topics are
NOT created here and will be added per-variant in Phase 2 work. Partition
count = 1 (single-node), replication = 1.

**`drs-server/tests/test_kafka_broker_integration.py`** — pytest module.
Session-scoped fixture probes `localhost:9092`; if no broker reachable, the
entire module is skipped. When broker is reachable:

1. Creates a unique consumer-group-id (so reruns don't replay old state)
2. Instantiates a real `AIOKafkaProducer` + the existing `ControlPublisher`
3. Instantiates a real `AIOKafkaConsumer` + the existing `ControlConsumer`
   wired to a real `SyncStateEngine`
4. Publishes a `variant.registered` event from the publisher
5. Asserts the consumer dispatches and the engine has the variant registered

This is the **closing test** for the Kafka contract — same shape as
`test_reference_parser_integration.py` is for the C++ parser ABI.

**Developer Handbook §11.x "Kafka conventions"** — short subsection (~30 lines)
documenting:
- Topic naming convention (`<area>.<purpose>` for control plane;
  `hw.<variant>.<kind>` for data plane — but data plane is Phase 2)
- Default serialization (UTF-8 JSON for control plane; data plane payload
  format deferred to first-IRS work)
- The Protocol-typed-dependency + factory-injection pattern, citing
  existing files as templates
- How to start the local broker for development
- Where the integration test lives + how to run it

### 3.3 What this spec deliberately omits

| Omission | Reason |
|---|---|
| TimescaleDB in docker-compose | No code uses Postgres today (drs-server is in-memory); adding the service would be premature scaffolding |
| `hw.<variant>.<kind>` topic publisher/consumer template | Payload schema is data-shape-dependent; first-IRS work designs it |
| `measurements` hypertable + asyncpg insert pattern | Schema decision; waits for real data |
| Protobuf / msgpack alternative serialization | JSON is the established choice for control plane; data-plane perf-sensitive serialization decision waits for first variant's volume profile |
| CI job running the integration test | Adds 60-90 s to CI per push; local-only first, lift to CI if/when adoption justifies it |
| Multi-broker / replication / production tuning | Dev convenience layer; production tuning is a separate B1.x item |

### 3.4 Data flow (integration test path)

```
test                                                                            
  │                                                                             
  ├─ ControlPublisher.publish_variant_registration("rdfs", 10.0)                
  │     │                                                                       
  │     └─→ AIOKafkaProducer.send_and_wait("drs.control", b'{...}')             
  │           │                                                                 
  │           └─→ Kafka broker (localhost:9092, topic="drs.control")            
  │                 │                                                           
  │                 └─→ AIOKafkaConsumer (group_id=<unique-per-run>)            
  │                       │                                                     
  │                       └─→ ControlConsumer.run() loops once                  
  │                             │                                               
  │                             └─→ engine.register_variant("rdfs", 10.0)       
  │                                                                             
  └─ assert engine.current_variant_status("rdfs") is not None                   
```

The fakes used in unit tests collapse the middle two steps into a list
yield; the real-broker test exercises the network + serialization path.

## 4. Error handling

- **Broker not reachable at test start:** session-scoped fixture probes
  `localhost:9092`; on connection refused / timeout, calls `pytest.skip` for
  the entire module (mirrors `test_reference_parser_integration.py`'s
  `cmake-not-on-PATH` skip pattern).
- **Broker takes >5 s to be ready after compose-up:** the
  `infrastructure/kafka/README.md` documents the healthcheck pattern; the
  integration test uses a 30 s timeout on connection setup to absorb cold
  starts but does not orchestrate compose itself (devs run compose).
- **Topic doesn't exist when consumer subscribes:** default Kafka behaviour
  is auto-create with single partition; the integration test relies on
  this. (For production, the dedicated topic-creation script provisions in
  advance.)
- **Re-runs leaving consumer-group state:** each test run generates a unique
  `group_id` via `uuid.uuid4()` to avoid offset replay.

## 5. Testing

| Test layer | What's covered | Where |
|---|---|---|
| Unit (existing, untouched) | publisher/consumer logic with fake broker | `drs-bridge/tests/`, `drs-server/tests/` |
| New: real-broker integration | end-to-end serialization + transport | `drs-server/tests/test_kafka_broker_integration.py` (skips if broker not reachable) |
| Manual smoke | broker comes up, topics created, listed back | documented in `infrastructure/kafka/README.md` |

## 6. Open decisions (none blocking)

- **CI runs the new integration test?** Defer. Add later if test failures
  in local-only mode start being missed by reviewers.
- **Persistent vs ephemeral Kafka volume?** Use named volume; document
  `docker compose down -v` resets. Devs prefer this for iterating on topic
  changes without re-creating state every time.

## 7. References

- [Reference C++ parser template plan](../plans/reference-parser-template.md)
  — same closing-the-contract motivation, same skip-if-tool-missing
  integration-test pattern
- [Developer Handbook §11 drs-bridge internal design](../../ewtss/developer-handbook.md)
- [Design Post-Mortem §3.5 integration test environment](../../ewtss/design-postmortem.md#35--high-integration-test-environment--physical--logical-home-unclear)
  — gap this spec partially resolves
- [v2 Execution Plan §1 + §6 Phase 1-2 contracts](../../ewtss/v2-execution-plan.md)
