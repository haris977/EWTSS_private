# Local Kafka — dev broker (native KRaft, no Docker)

Single-node Kafka KRaft broker for local development of `drs-bridge`.
No Docker — the deployment target is an air-gapped Windows LAN, so the
native path matches production more closely.

## Prerequisites

- **JDK 17** (Temurin): `winget install EclipseAdoptium.Temurin.17.JDK`
- **Apache Kafka 3.x** (Scala 2.13): extract to e.g. `C:\kafka\kafka_2.13-3.9.1`

> **Windows 11 gotcha:** `wmic` was removed but `kafka-server-start.bat` calls
> it for heap detection, so the broker exits 255. Fix: edit that `.bat` and
> replace the `wmic os get osarchitecture …` block with a fixed
> `set KAFKA_HEAP_OPTS=-Xmx1G -Xms1G`.

## Start the broker

```powershell
$K = "C:\kafka\kafka_2.13-3.9.1"
$env:JAVA_HOME = "C:\Program Files\Eclipse Adoptium\jdk-17.0.19.10-hotspot"
# One-time: format the KRaft metadata store
$uuid = & "$K\bin\windows\kafka-storage.bat" random-uuid
& "$K\bin\windows\kafka-storage.bat" format -t $uuid -c "$K\config\kraft\server.properties"
# Start the broker (leave this window open)
& "$K\bin\windows\kafka-server-start.bat" "$K\config\kraft\server.properties"
```

Broker is up when `localhost:9092` accepts connections (~5–15 s cold start).

## Provision all topics

From the repo root, with `aiokafka` installed in the bridge venv:

```powershell
.\.venv\Scripts\python.exe infrastructure\kafka\create-topics.py
```

Idempotent — safe to re-run. Creates:

| Topic | Kind | Notes |
|-------|------|-------|
| `drs.control` | control-plane | drs-bridge → drs-server events |
| `system.timesync` | control-plane | sync-state transitions |
| `system.health` | control-plane | tick-lag, connect/disconnect, hw errors |
| `drs.roster` | control-plane | compacted snapshot, key `active` |
| `hw.dp_ecm_hf.telemetry` | data-plane | decoded HF response frames |
| `hw.dp_ecm_vu.telemetry` | data-plane | decoded VU response frames |

## Inspect topics

```powershell
$K = "C:\kafka\kafka_2.13-3.9.1"
& "$K\bin\windows\kafka-topics.bat" --bootstrap-server localhost:9092 --list
& "$K\bin\windows\kafka-topics.bat" --bootstrap-server localhost:9092 --describe --topic hw.dp_ecm_hf.telemetry
& "$K\bin\windows\kafka-console-consumer.bat" --bootstrap-server localhost:9092 --topic hw.dp_ecm_hf.telemetry --from-beginning
```

## Message schemas

All schemas live in `contracts/kafka/`. Raw UTF-8 JSON — no Schema Registry,
no Avro/Protobuf. See individual `.schema.json` files for field definitions.

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| Broker exits 255, `'wmic' is not recognized` | Patch `kafka-server-start.bat` — see Prerequisites |
| `Connection refused` from create-topics.py | Broker not ready; wait a few seconds |
| Want to reset all state | Stop broker, delete `log.dirs` dir from `server.properties` |
