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
