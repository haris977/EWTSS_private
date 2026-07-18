# EWTSS v1 — Legacy System Audit & Lessons Learned

**Audience:** v2 reviewers / auditors who want line-level evidence behind v2's design choices; v2 engineers who want to understand "what specifically are we fixing"; future maintainers who want to know what *not* to repeat.
**Status:** historical. v1 is the production system being replaced; this document freezes its known issues. Add to it only if a v1 issue is rediscovered while migrating that hadn't been captured before.
**Read time:** 15 minutes.

---

## What this document is (and isn't)

**Is:** a curated summary of what was wrong with EWTSS v1, with file:line citations, and a pointer to v2's structural fix for each. The audit chain — "v2 claims to fix X" → "X was specifically observed at file F line L" → "v2's fix lives at section S of doc D."

**Isn't:** a v1 user manual, a v1 maintenance guide, or a comprehensive code review of v1. The four detailed source documents below sit alongside this one in `docs/ewtss/legacy/` for the reader who needs the underlying material:

- [`legacy/ARCHITECTURE.md`](legacy/ARCHITECTURE.md) — v1 architecture documented as it stands.
- [`legacy/ARCHITECTURE_ANALYSIS.md`](legacy/ARCHITECTURE_ANALYSIS.md) — strengths / weaknesses / alternatives across 7 dimensions.
- [`legacy/CODE_ANALYSIS.md`](legacy/CODE_ANALYSIS.md) — code-quality findings prioritised Critical / High / Medium / Lower.
- [`legacy/ANTI_PATTERNS_AND_PERFORMANCE.md`](legacy/ANTI_PATTERNS_AND_PERFORMANCE.md) — the substantial one (879 lines): anti-pattern catalogue with code excerpts, tech-stack misuses, performance improvement plan.

This document is the bridge between those four and the v2 doc set. Specific anti-patterns are referred to as `AP-N` and tech-stack misuses as `TS-N` throughout the v2 docs; this document is the canonical map.

---

## 1. Anti-patterns observed in v1 — and how v2 fixes them

Severity reflects the v1 production impact, not the v2 design effort.

### Critical

| ID | Issue | v1 file:line | v2 fix |
|---|---|---|---|
| **AP-1** | Session-per-Application — `next(get_db())` extracts a SQLAlchemy session that lives for the whole process lifetime; identity map accumulates, connection never returns to pool, six pools held forever | `drs_server/services/srx_kafka_setup.py:72` (and 5 sibling files) | Session scoped per Kafka message via `async with AsyncSessionLocal() as db` — see [Developer Handbook §13](developer-handbook.md#13-drs-server-internal-design) "Design rules" |
| **AP-2** | Unmonitored daemon threads — six daemon threads spawned at startup with no reference held, no health check, no restart logic; silent exit = silent data loss | `drs_server/main.py:271-295` | `consumer_supervisor.py` with exponential backoff (1 s → 60 s cap); `GET /health/consumers` reports per-consumer liveness — see [Developer Handbook §13.2](developer-handbook.md#132-consumer-lifecycle) |
| **AP-3** | Kafka consumer breaks on error — any transient broker error / rebalance / network blip drops the consumer, ingest stops permanently until process restart | `drs_server/services/srx_kafka_setup.py:45-52` | Consumer raises on error; supervisor catches and restarts. No `break` on error path |
| **AP-13** | No TCP stream reassembly — `recv(4096)` without accumulation buffer; partial frames silently discarded; the existing `if footer in segment` guard *acknowledges* incomplete frames but skips them rather than buffering | `drs_bridge/srx/random_srx/tcp_server.py`, `network_utils.py:219-238` | Per-connection buffer + `try_extract_frame` loop in C++ parser; asyncio TCP server feeds bytes in chunks — see [Developer Handbook §12.3](developer-handbook.md#123-the-bidirectional-tcp-handler) |

### High

| ID | Issue | v1 file:line | v2 fix |
|---|---|---|---|
| **AP-4** | Auto-commit before DB write — `enable.auto.commit=True` commits the offset on a background timer regardless of DB-write success; crash between auto-commit and `db.commit()` = permanent message loss | `drs_server/services/srx_kafka_setup.py:14-21` | `enable.auto.commit=False`; manual commit only after successful DB write — see [Developer Handbook §13.6](developer-handbook.md#136-design-rules-load-bearing) |
| **AP-5** | Per-row commits + `synchronize_session=False` — bulk delete leaves stale identity-map entries; insert loop calls `db.commit()` once per row (100 rows = 100 round-trips) | `drs_server/services/srx_drs_service.py:199-236` (and `saveFHData`, `saveBurstData`) | Batched writes (100 messages / 500 ms whichever first); `bulk_insert_mappings` for ORM bulk inserts — see [Developer Handbook §13.2](developer-handbook.md#132-consumer-lifecycle) |
| **AP-6** | God table — `RadarTrackReport` with ~150 columns including 16 `Column(JSON)` fields; `db.query(...).all()` hydrates 150 cols × N rows + deserialises 16 JSON blobs per row; positional list-index field population (`track_report[10]`) silently `IndexError`s on short lists | `drs_server/models/models.py:492-642` | One generic `measurements` hypertable with `payload` (jsonb) + `measurement_scalars` for hot-column queries; per-variant decomposition where bulk-array data warrants — see [Developer Handbook §10.2](developer-handbook.md#102-time-series-measurement-schema-drs-server-writes) |
| **AP-7** | No foreign keys across 28 tables — only one `ForeignKey` exists (`UserToken → Users`); `FF`/`FH`/`Burst`/`RadarTrackReport` carry logical parent references with no constraint; orphaned report rows accumulate undetectably | `drs_server/models/models.py` | `ON DELETE CASCADE` on motion profiles + parameters; explicit FK constraints in v2 schema; `created_by` set to tombstone user on user deletion — see [Developer Handbook §10.5](developer-handbook.md#105-schema-rules) |
| **AP-9** | Confluent Kafka producer shared across `ThreadPoolExecutor(max_workers=5)` — `Producer.produce()` is not thread-safe; concurrent calls produce dropped messages, segfaults, or corrupted internal state | `ewtss-backend/producer/srx_new/Kafka_server.py:75-101` | One shared `AIOKafkaProducer` per bridge process, single asyncio event loop — no concurrent calls to `produce()` — see [Developer Handbook §12.6](developer-handbook.md#126-kafka-producer-tuning) |
| **AP-11** | `query.all()` with no limit on every list endpoint — at 10k+ rows, response size exceeds 100 MB; `get_freq_power()` issues 3 full-table scans per request | `drs_server/services/srx_drs_service.py:22, 44, 71, 131-145` | All read endpoints paginated and time-bounded — explicitly forbidden to use `.all()`; composite index `(session_id, recorded_at, message_type)` on `measurements` — see [Developer Handbook §13.5](developer-handbook.md#135-report-query-endpoints) |
| **AP-12** | `=+1` operator bug in TCP server state machines — `=+1` is assignment of `+1` (always sets to 1), `+=1` would be the intended increment; the one-time log triggers on every connection, not just the first | `drs_bridge/srx/random_srx/tcp_server.py` (and 4 variant copies) | Caught by removing the duplicated TCP-server-per-variant pattern — single asyncio handler with no per-connection state-machine flag of this kind |
| **AP-15** | ICD schema in CSV with `ast.literal_eval` — Python literals embedded in CSV cells; quoting errors silently `None`; no traceability to the original ICD Excel; duplicate copies per variant diverge over time | `drs_bridge/srx/commands/command.csv`, `network_utils.py` (load + dispatch) | ICD code generator reads ICD Excel directly, emits typed C++ constants + dispatch — see [`tools/icd_codegen` design](specs/icd-codegen-tool-design.md) and [ICD reference (COMM DF)](icd-reference-comm-df.md) |

### Medium

| ID | Issue | v1 file:line | v2 fix |
|---|---|---|---|
| **AP-8** | Shared mutable dict as thread synchronisation — check-then-act on `control_flags[name].is_set()` → `.set()` is not atomic; two simultaneous `/start-stream` requests can both pass the check and spawn duplicate threads | `ewtss-backend/producer/utils/control_flag.py`, `ewtss-backend/main.py:162-174` | Replaced by Kafka-based session-control topic + `SessionRegistry` in `drs-bridge` — see [Developer Handbook §12.4](developer-handbook.md#124-the-responserouter) |
| **AP-10** | Frontend polling instead of event-driven — 20+ Angular components use `setInterval(... 1000)` against REST endpoints; ~20 req/s baseline load even when no new data has arrived; backend already has `/ws/logs` WebSocket but unused for data | `src/...` (20+ `.ts` files) | Mode A: WPF subscribes to `drs-server` WebSocket topics. Mode B (future): Angular `WebSocketSubject` with `share()`. No polling for live data — see [Architecture Overview §4.7](architecture-overview.md) |
| **AP-14** | Magic bytes as inline literals in 4+ files — `b"\xaa\xab\xba\xbb"` scattered across `network_utils.py`, multiple `kafka_server.py` files, with inconsistent `bytes` vs escaped-string representations between command and response sides | `drs_bridge/srx/random_srx/network_utils.py:224` and 3+ siblings; `drs_constant.py:6-7` | Single `frame_constants.h` per variant generated from the ICD; constants imported everywhere — see [ICD reference §7.1](icd-reference-comm-df.md#71-frame-constants-header) |

---

## 2. Tech-stack misuses

| ID | Issue | v1 evidence | v2 resolution |
|---|---|---|---|
| **TS-1** | FastAPI used with synchronous blocking ORM — `def` route handlers + sync SQLAlchemy queries run on the threadpool (size 40); 10 polling components saturate the threadpool and queue all other requests | `drs_server/routers/srx_router.py:113-115` | All v2 route handlers `async def`; SQLAlchemy 2.0 async API + asyncpg; load-bearing per [Developer Handbook §13.6](developer-handbook.md#136-design-rules-load-bearing) |
| **TS-2** | Two Kafka clients in the same project — `confluent-kafka` (active) + `kafka-python` (effectively abandoned) both imported; doubles dependency surface, incompatible objects | `drs_server/requirements.txt:10, 28` | v2 uses only `aiokafka` (asyncio-native) for `drs-server`; `drs-bridge` uses the same. One client across the codebase |
| **TS-3** | `numpy==2.0.0` + `pandas==2.2.2` — known broken pair; pandas 2.2.x not built against numpy 2.0; ImportError or silent numeric corruption | `drs_server/requirements.txt:33-35` | v2 dependency set audited at lock-file time; CI builds against the locked versions; numpy 2.0 / pandas 2.2 pair specifically excluded |
| **TS-4** | Threading model for I/O-bound workload — every TCP connection + every Kafka consumer is a thread; 16+ threads share one GIL slot; under load, context-switching dominates | `drs_bridge/*/tcp_server.py` (5 copies) | `asyncio.start_server` for the TCP layer; `aiokafka` for Kafka; entire bridge runs in a single thread, supports 100+ concurrent connections without GIL contention |
| **TS-5** | `debug=True` in production FastAPI — exposes full Python tracebacks + ORM query strings + variable values to any client that triggers an error | `drs_server/main.py:36`; `ewtss-backend/main.py:52` | v2 reads debug flag from environment with secure default (`debug=False`); production deployment fails-fast if `APP_DEBUG=true` is set |
| **TS-6** | Polling against a Kafka backbone — frontend polls REST every second; 1 s minimum update latency; continuous load even at idle | All Angular live-data components | WebSocket push from `drs-server` only when new Kafka messages arrive; zero idle load — see [Architecture Overview §4](architecture-overview.md#4-control-and-data-flows) |

---

## 3. Performance findings — what v2 specifically improves

The legacy production system has known scaling ceilings driven by the anti-patterns above. The performance improvement plan in v1's audit ([`legacy/ANTI_PATTERNS_AND_PERFORMANCE.md`](legacy/ANTI_PATTERNS_AND_PERFORMANCE.md) Part 3) is tracked here as the v2 acceptance criteria.

| Improvement | v1 issue | Estimated v1 → v2 impact | v2 implementation |
|---|---|---|---|
| Pagination + composite indexes (P-1) | AP-11 | 50–200× query speedup at 10 k+ rows | All `drs-server` read endpoints paginated; composite index `(session_id, recorded_at, message_type)` |
| Batched insert (P-2) | AP-5 | 10–100× write throughput | Batched writes (100 msg / 500 ms); `bulk_insert_mappings` for ORM bulk |
| Per-message session scope (P-3) | AP-1 | Eliminates identity-map memory growth + stale reads | `async with AsyncSessionLocal()` in every consumer iteration |
| WebSocket push (P-4) | AP-10, TS-6 | Eliminates 20 req/s baseline; ≤100 ms latency | `drs-server` WS hub + per-variant topic subscription |
| Decompose JSON-array god-table (P-5) | AP-6 | Eliminates per-row JSON deserialisation; enables array-level queries | Generic `measurements` hypertable + `measurement_scalars`; per-variant child hypertables where bulk-array data warrants (e.g. FFT) |
| asyncio TCP servers (P-6) | TS-4, AP-13 | 100+ concurrent device connections per single core; no GIL contention | `asyncio.start_server` + per-connection accumulation buffer |
| SQLAlchemy 2.0 async API (P-7) | TS-1 | FastAPI event loop unblocked; throughput scales with connection count | `create_async_engine` + asyncpg + all routes `async def` |
| Reuse Kafka producer (P-8) | AP-9 | 2× produce throughput | Single shared `AIOKafkaProducer` per bridge process with `linger_ms=5`, `compression_type="lz4"` |

The headline ceiling improvements (10 → 100 instances; 200 → 2,000+ msg/s; 5 → 20 operators) are summarised in [Architecture Overview §8.2](architecture-overview.md#82-current-production-system-vs-v2--ceilings-and-failure-modes).

---

## 4. Items resolved by architectural change rather than fix

Several v1 findings — secrets management, CORS, default credentials, frontend token storage, Geoserver credentials in source — sit at the boundary between "code defect" and "architectural commitment." v2 addresses them via deployment-time policy rather than code change:

| v1 finding | v1 evidence | v2 treatment |
|---|---|---|
| JWT `SECRET_KEY` hardcoded in source | `drs_server/ui_constants/drs_constant.py` | v2 reads `SECRET_KEY` from environment with no default; pydantic raises if not set |
| Default `operator/operator` credentials at startup | `drs_server/utils/auth_utilis.py` | v2 has no default account; first-run setup forces password creation |
| CORS `["*"]` + `allow_credentials=True` | `drs_server/main.py:38-46`, `ewtss-backend/main.py:56-64` | v2 reads `FRONTEND_ORIGIN` from environment; explicit allow-list only |
| Geoserver `admin:geoserver` in `btoa()` in frontend | `src/app/shared/interceptor/auth.interceptor.ts` | v2 backend proxies tile requests; frontend never holds the credential |
| Tile-endpoint path-traversal vulnerability | `ewtss-backend/main.py` `/{z}/{x}/{y}.png` | v2 (Mode B) tile serving via Martin tile server with bound MBTiles, not filesystem-path-derived |
| Token in browser `localStorage` | `src/app/shared/...` | v2 (Mode B): `HttpOnly` cookie or short-lived in-memory token (open decision per [Decision Record](decision-record.md)); Mode A: no browser, n/a |

These are tracked as **R6** (licence) and the security-posture items in the [Risk Register](risk-register.md) §1, plus the open authentication decision in the [Decision Record](decision-record.md).

---

## 5. What's NOT being repeated by construction

Some v1 patterns are eliminated by v2's structural choices rather than by fixing the v1 code:

- **One TCP server file per hardware variant** (5 copies in v1) → eliminated by the generic `tcp_server.py` + per-variant YAML profile + per-variant C++ parser library. See [Developer Handbook §12.2](developer-handbook.md#122-module-responsibilities).
- **Kafka consumer file duplicated per variant** (6 copies in v1) → eliminated by generic consumer + per-variant topic-subscription config.
- **Per-variant `command.csv` schema files** → eliminated by the ICD codegen + `frame_constants.h` per variant.
- **Per-variant Python `parse_request_param.py` and `random_value_generator.py`** → eliminated; the C++ parser owns decode, the `ResponseRouter` `RandomGenerator` owns random data per [§12.4 of the developer handbook](developer-handbook.md#124-the-responserouter).
- **Production system locked to MySQL with no time-series partitioning** → replaced by PostgreSQL 16 + TimescaleDB 2.x with hypertables and retention policies per [§10.4 of the developer handbook](developer-handbook.md#104-timescaledb-hypertables).

If a future engineer finds themselves reaching for one of these patterns ("I'll just add another `tcp_server.py` for the new variant"), they're rebuilding the v1 problem. The architectural commitment is one config + one parser per variant — see [Architecture Overview §5](architecture-overview.md#5-key-trade-offs).

---

## 6. References

- **v1 source documents (sibling `legacy/` subfolder):**
  - [`legacy/ARCHITECTURE.md`](legacy/ARCHITECTURE.md) — v1 architecture as it stands.
  - [`legacy/ARCHITECTURE_ANALYSIS.md`](legacy/ARCHITECTURE_ANALYSIS.md) — strengths / weaknesses across 7 dimensions.
  - [`legacy/CODE_ANALYSIS.md`](legacy/CODE_ANALYSIS.md) — code-quality findings prioritised.
  - [`legacy/ANTI_PATTERNS_AND_PERFORMANCE.md`](legacy/ANTI_PATTERNS_AND_PERFORMANCE.md) — full anti-pattern catalogue with code excerpts.
- **v1 codebase READMEs** (live in the separate internal v1 repo — referenced here for traceability, not present in this repo):
  - `drs_bridge/README.md` — legacy bridge service.
  - `drs_server/README.md` — legacy telemetry consumer.
  - `ewtss-backend/README.md` — legacy scenario / RBAC service.
- **v2 doc set (where the fixes live):**
  - [Architecture Overview](architecture-overview.md) — system shape; §8.2 has the current-vs-v2 ceilings table.
  - [Developer Handbook](developer-handbook.md) — §10 schema, §11–§13 internal designs, §13.6 design rules.
  - [Risk Register](risk-register.md) — R7 specifically guards against re-introducing the permanent-COM-event-subscription regression observed during MVP4.5 (a v1-style anti-pattern in v2 territory).
