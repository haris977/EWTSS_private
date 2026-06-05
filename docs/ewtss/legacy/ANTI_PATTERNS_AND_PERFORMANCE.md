# EWTSS — Anti-Patterns, Tech Stack Issues & Performance Analysis

> All references are to the working directory `e:\Sandbox\17-05-2025\`.

---

## Part 1 — Anti-Patterns

### AP-1 · Session-per-Application (SQLAlchemy)

**Category:** ORM misuse — Severity: Critical

**Evidence:**

```python
# drs_server/services/srx_kafka_setup.py:72-78
def startStoringSrxDataIntoDataBase():
    consumer = createConsumer()
    producer = createProducer()
    db = next(get_db())          # generator consumed once — session opened here
    try:
        consumeMessages(consumer, producer, db)   # infinite while loop
    finally:
        consumer.close()         # db.close() is MISSING
```

`get_db()` is a generator that yields one session then closes it in `finally`. Calling `next()` on it extracts the session but never reaches the `finally` clause. The SQLAlchemy session lives for the entire process lifetime — accumulating identity-map objects, holding a database connection, and never expiring its internal cache.

The same pattern exists in all six Kafka setup files:
`srx_kafka_setup.py:72`, `mrx_kafka_setup.py:72`, `gnss_kafka_setup.py:69`, `drs_kafka_setup.py:63`, `jsvushf_srx_kafka_setup.py:72`, `jsvushf_gnss_kafka_setup.py:69`.

**Consequence:** Six database connections permanently held. Any object fetched in an early request is never evicted from the identity map — queries that should hit the DB return stale cached rows.

**Fix:**
```python
def consume_loop(consumer, producer):
    for msg in consumer:
        with SessionLocal() as db:          # session scoped to one message
            process_and_store(msg, db)
            db.commit()
```

---

### AP-2 · Unmonitored Daemon Threads

**Category:** Thread lifecycle — Severity: Critical

**Evidence:**

```python
# drs_server/main.py:271-295 (startup_event)
thread = threading.Thread(target=startStoringSrxDataIntoDataBase)
thread.daemon = True
thread.start()
# repeated for mrx, gnss, jsvushf_srx, jsvushf_gnss, data_availability
```

Six daemon threads are created with no reference kept, no health check, no restart logic. When a thread crashes (Kafka error, DB timeout, unhandled exception), it exits silently. The FastAPI process appears healthy, endpoints respond, but data has stopped flowing. There is no way to detect this without inspecting MySQL for stale timestamps.

**Fix:**
```python
# Supervisor that restarts threads on failure
class SupervisedThread:
    def __init__(self, target, name):
        self.target = target
        self.name = name
        self._thread = None

    def start(self):
        self._thread = threading.Thread(target=self._run, name=self.name, daemon=True)
        self._thread.start()

    def _run(self):
        backoff = 1
        while True:
            try:
                self.target()
            except Exception as e:
                logger.error(f"Thread {self.name} crashed: {e}. Restarting in {backoff}s")
                time.sleep(backoff)
                backoff = min(backoff * 2, 60)

    def is_alive(self):
        return self._thread and self._thread.is_alive()
```

Expose `/health/threads` that returns liveness of each supervised thread.

---

### AP-3 · Kafka Consumer Breaks on Error — No Recovery

**Category:** Fault tolerance — Severity: Critical

**Evidence:**

```python
# drs_server/services/srx_kafka_setup.py:45-52
if msg.error():
    if msg.error().code() == KafkaError._PARTITION_EOF:
        continue
    else:
        srx_logger.info(f"Consumer error: {msg.error()}")
        break    # ← exits the while True loop permanently
```

Any transient Kafka error (broker restart, rebalance timeout, network blip) causes the consumer to `break` and exit `consumeMessages()`. The thread's `finally` block closes the consumer, and ingest stops permanently until the process is restarted. This is a silent data loss scenario.

**Fix:** Raise the exception so the supervisor in AP-2 catches and restarts it.

```python
else:
    srx_logger.error(f"Consumer error: {msg.error()}")
    raise KafkaException(msg.error())   # supervisor will restart
```

---

### AP-4 · Auto-Commit with Synchronous DB Writes

**Category:** Message delivery guarantee — Severity: High

**Evidence:**

```python
# drs_server/services/srx_kafka_setup.py:14-21
def createConsumer():
    return Consumer({
        'bootstrap.servers': srx_constant.SRX_KAFKA_BROKER,
        'group.id': srx_constant.SRX_GROUP_ID,
        'enable.auto.commit': True,     # offset committed before DB write
        ...
    })
```

With `enable.auto.commit=True`, Kafka commits the offset on a background timer regardless of whether the database write succeeded. If the process crashes after the auto-commit but before `db.commit()`, the message is permanently lost — the offset has advanced past it.

**Fix:**
```python
Consumer({'enable.auto.commit': False, ...})
# After successful DB write:
consumer.commit(asynchronous=False)
```

---

### AP-5 · Bulk Delete with `synchronize_session=False` Followed by Per-Row Inserts

**Category:** ORM / transaction misuse — Severity: High

**Evidence:**

```python
# drs_server/services/srx_drs_service.py:199-236
def saveFFData(db: Session, group_id: int, command_id: int, storing_data_list: list):
    db.query(FF).filter(
        FF.response_command_id == command_id,
        FF.response_group_id == group_id
    ).delete(synchronize_session=False)   # bulk delete, session unaware

    for storing_data in storing_data_list:
        new_request = FF(...)
        db.add(new_request)
        db.commit()        # ← one commit per row
```

Two compounded problems:
1. `synchronize_session=False` means the session's identity map still holds references to the deleted rows. Any subsequent access to those objects returns stale/deleted data without an error.
2. Calling `db.commit()` inside the loop issues one `COMMIT` statement per row. For 100 incoming records, this is 100 round-trips to MySQL. A batch of 100 rows that could be inserted in <5ms instead takes 100× the commit overhead.

The same pattern exists in `saveFHData()` (lines 243-274) and `saveBurstData()` (lines 280-312).

**Fix:**
```python
def saveFFData(db: Session, group_id: int, command_id: int, storing_data_list: list):
    db.query(FF).filter(
        FF.response_command_id == command_id,
        FF.response_group_id == group_id
    ).delete(synchronize_session='fetch')   # keep session consistent

    db.bulk_insert_mappings(FF, [build_mapping(d) for d in storing_data_list])
    db.commit()    # single commit for the entire batch
```

---

### AP-6 · God Table — `RadarTrackReport` with 150 Columns and 16 JSON Fields

**Category:** Database design / God Object — Severity: High

**Evidence:**

`drs_server/models/models.py:492-642` — The `RadarTrackReport` model has approximately 150 columns including 16 `Column(JSON)` fields:

```python
pri_1_to_64_list       = Column(JSON)
hop_frequency_1_to_64  = Column(JSON)
# ... 14 more JSON columns
```

The service method that reads it (`srx_drs_service.py:94`) loads the entire table:
```python
db.query(RadarTrackReport).all()
```

At 1 000 rows, this hydrates `150 columns × 1 000 rows` of Python objects into the interpreter's heap, plus deserialises 16 JSON blobs per row. There is no index on any column except the primary key.

The write function (`savaRadarTrackReport()` — note the typo) populates fields by positional list index:
```python
pri_1_to_64_list = {"pri_1_to_64_list": track_report[10]}
```

If the incoming list has fewer than 145 elements, this raises an `IndexError` and silently kills that consumer message.

**Fix:**
- Decompose into 3–4 normalised tables: `RadarTrackSummary`, `RadarTrackSignal`, `RadarTrackFrequencyHops`
- Move array data (`pri_1_to_64_list`) to a child table with one row per hop
- Use keyword-based unpacking from a validated Pydantic model, not positional indexing

---

### AP-7 · No Foreign Keys Across 28 Tables

**Category:** Database integrity — Severity: High

**Evidence:**

`drs_server/models/models.py` contains 28 ORM model classes. Only one `ForeignKey` exists in the entire file:

```python
# models.py:459
user_id = Column(Integer, ForeignKey("users.id"))   # UserToken → Users
```

Yet `FF`, `FH`, `Burst`, and `RadarTrackReport` all carry `response_group_id` and `response_command_id` — which reference logical parent entities — but without foreign key constraints. Orphaned report rows accumulate with no way to detect or clean them up. Cascading deletes are impossible.

---

### AP-8 · Shared Mutable Dict as Thread Synchronisation Primitive

**Category:** Concurrency — Severity: Medium

**Evidence:**

```python
# ewtss-backend/producer/utils/control_flag.py:3-10
control_flags = {
    "srx": threading.Event(),
    ...
    "stream_scenario": threading.Event()
}
```

```python
# ewtss-backend/main.py:162-174
if control_flags[name].is_set():           # check
    return {"status": "already_running"}
thread = threading.Thread(target=..., daemon=True)
thread.start()
control_flags[name].set()                  # set — not atomic with check above
```

The check-then-act sequence (`.is_set()` → `.set()`) is not atomic. Two simultaneous `/start-stream` HTTP requests can both pass the `is_set()` check and spawn duplicate threads for the same stream. A Kafka producer topic would then receive duplicate messages.

The same `control_flags` dict is also imported directly into producer modules. Any module can read or write flags without the API layer knowing.

**Fix:** Use a `threading.Lock` around the check-and-start sequence, or redesign around a proper task registry.

---

### AP-9 · Confluent Kafka Producer Shared Across ThreadPoolExecutor Workers

**Category:** Thread safety — Severity: High

**Evidence:**

```python
# ewtss-backend/producer/srx_new/Kafka_server.py:75-101
producer = Producer(producer_config)          # created once in main thread
executor = concurrent.futures.ThreadPoolExecutor(max_workers=5)

while True:
    msg = consumer.poll(1.0)
    executor.submit(process_and_produce, request_data, producer, ...)
    # same producer passed to up to 5 concurrent workers
```

`confluent_kafka.Producer.produce()` is **not thread-safe**. Calling it from multiple threads simultaneously without a lock causes undefined behavior — dropped messages, segfaults, or corrupted internal state.

**Fix:** Either create one producer per worker thread, or serialise all `produce()` calls through a `threading.Lock`, or use a single-threaded producer pattern with async I/O.

---

### AP-10 · Frontend Polling Instead of Event-Driven Updates

**Category:** Architectural — Severity: Medium

**Evidence:**

More than 20 Angular components use `setInterval` to poll REST endpoints:

```typescript
// Pattern repeated in gnss.component.ts, servo-digital-rdfs.component.ts,
// drs-flow.component.ts, and 17+ more
this.intervalId = setInterval(() => {
    this.getTableData();
}, 1000);
```

At 20 active components polling at 1 s intervals, the frontend generates a steady 20 HTTP requests per second to `drs_server`. Each request triggers `db.query(FF).all()` — a full table scan. This load exists even when no new data has arrived.

The backend already has WebSocket endpoints (`ewtss-backend/main.py` exports `/ws/logs`). The infrastructure for push-based updates is partially present but unused for data delivery.

**Fix:** Push reports over a WebSocket or Server-Sent Events stream. Components subscribe once and receive updates only when new data arrives.

---

### AP-11 · `query.all()` With No Limit on Every List Endpoint

**Category:** Performance / ORM — Severity: High

**Evidence:**

Every list query in `drs_server/services/srx_drs_service.py` loads the full table:

```python
# Lines 22, 44, 71 — one per report type
ff_report_list  = db.query(FF).all()
fh_report_list  = db.query(FH).all()
burst_report_list = db.query(Burst).all()
```

`get_freq_power()` (lines 131-145) fires three independent full-table scans in one request:
```python
ffAllData    = db.query(FF).all()
fhAllData    = db.query(FH).all()
burstAllData = db.query(Burst).all()
```

After a 1-hour test run (typical usage), these tables contain tens of thousands of rows. The endpoint loads everything into Python memory, converts each ORM object to a Pydantic model, and serialises the result. The response size easily exceeds 100 MB.

---

### AP-12 · `=+` Operator Bug in TCP Server State Machines

**Category:** Logic bug — Severity: High

**Evidence:**

```python
# drs_bridge/srx/random_srx/tcp_server.py (and mrx, gnss, jsvushf variants)
if DATA_RANDOM_FLOW == 0:
    saveDrsLogs(...)
    DATA_RANDOM_FLOW =+1    # ← sets to positive 1 every time, not increments
```

`=+1` is assignment of `+1` (i.e., always sets to 1). `+=1` would be the intended increment. The state variable resets to 1 on every pass through the condition, so `DATA_RANDOM_FLOW == 0` can never be true after the first iteration — except that the variable is a local, re-initialised at the top of `handle_client_connection()`. The effect is that the one-time log write triggers on **every** connection, not just the first.

The same bug appears in `DATA_SG_FLOW =+1`.

---

## Part 2 — Tech Stack Misuse

### TS-1 · FastAPI Used With Synchronous Blocking ORM (No Async Benefit)

FastAPI is built on Starlette's asyncio event loop. Its performance advantage comes from `async def` endpoints and `await`-based I/O. All route handlers in `drs_server/routers/` are defined as `def` (not `async def`) and call synchronous SQLAlchemy queries:

```python
# drs_server/routers/srx_router.py:113-115
@router.get("/ff/", response_model=list[FFCreate])
def fetch_all_ff_reports_data(db: Session = Depends(get_db)):
    return get_ff_report_list(db)   # blocking .all()
```

FastAPI runs synchronous `def` endpoints in a thread pool (threadpool size defaults to 40). With 10 concurrent polling components each triggering a full-table scan, all 40 threadpool slots can saturate, causing requests to queue. The async event loop is blocked from serving other routes.

**Options (in order of preference):**
1. Use **SQLAlchemy 2.0 async engine** + `async def` endpoints + `asyncpg`/`aiomysql`
2. Use **`run_in_executor`** to offload heavy synchronous queries without blocking
3. Accept the sync model but add proper pagination so individual queries are fast

The project already uses `SQLAlchemy==2.0.30` — the async API is available but unused.

---

### TS-2 · Two Kafka Client Libraries Imported in the Same Project

**Evidence:**

```
# drs_server/requirements.txt:10, 28
confluent-kafka==2.5.0
kafka-python==2.0.2
```

`kafka-python` is an unmaintained pure-Python implementation (last real release 2022, project effectively abandoned). `confluent-kafka` is the actively supported C-extension client backed by `librdkafka`. Both are present in the same project. This doubles the dependency footprint, creates confusion about which client is used where, and `kafka-python`'s objects are not compatible with `confluent-kafka`'s.

**Fix:** Remove `kafka-python` entirely. Use only `confluent-kafka`.

---

### TS-3 · `numpy==2.0.0` + `pandas==2.2.2` — Known Breaking Combination

**Evidence:**

```
# drs_server/requirements.txt:33-35
numpy==2.0.0
pandas==2.2.2
```

NumPy 2.0 introduced breaking ABI changes. Pandas 2.2.x was not compiled against NumPy 2.0 — it requires `numpy >=1.26.0,<2`. Importing both in the same environment raises `ImportError` or produces silent numeric corruption depending on the platform.

**Fix:** Pin to a compatible pair:
```
numpy==1.26.4
pandas==2.2.2
```
Or upgrade pandas to `>=2.3` which adds NumPy 2.0 support.

---

### TS-4 · Threading Model Chosen for an I/O-Bound Workload

Every TCP connection in `drs_bridge` spawns a `threading.Thread`. Every Kafka consumer in `drs_server` runs in a `threading.Thread`. All of this I/O-bound work is constrained by Python's GIL, which means only one thread can execute Python bytecode at a time. For 10 simultaneous device connections + 6 Kafka consumers, 16 threads share a single GIL slot.

`asyncio` eliminates the GIL bottleneck for I/O-bound work because it never blocks the interpreter — it suspends coroutines while waiting for I/O. FastAPI is already running an asyncio event loop; the bridge services could be written as asyncio TCP servers at zero additional dependency cost.

**Alternative:** Replace `drs_bridge` TCP servers with `asyncio.start_server()`. The entire bridge can then run in a single thread with no GIL contention, supporting hundreds of concurrent device connections.

---

### TS-5 · `debug=True` in Both FastAPI Applications

```python
# drs_server/main.py:36
app = FastAPI(debug=True)

# ewtss-backend/main.py:52
app = FastAPI(debug=True)
```

`debug=True` does two things: enables Starlette's debug exception handler (returns full Python tracebacks in HTTP responses) and enables Python's `__debug__` assertions. In production this exposes internal stack traces, ORM query strings, and variable values to any client that triggers an error.

---

### TS-6 · Angular `setInterval` Polling on a Platform with Kafka

The system uses Kafka as its messaging backbone — an event-driven, push-based bus. The frontend discards this event model entirely and polls REST endpoints every second. The result is a continuously-loaded server with no reduction in DB queries during idle periods, and a one-second minimum latency for any update to reach the UI.

The backend already exposes a WebSocket endpoint. The right choice for live data display (which is the core UI use case) is:

```typescript
// Replace polling with a WebSocket subscription
const ws = new WebSocket(`ws://server/ws/srx-reports`);
ws.onmessage = (event) => this.updateTable(JSON.parse(event.data));
```

Angular's `WebSocketSubject` (from `rxjs/webSocket`) wraps this natively. Server pushes only when new Kafka messages arrive — zero idle load.

---

## Part 3 — Performance Improvement Plan

### P-1 · Replace Full-Table Scans with Paginated Queries + Composite Indexes

**Estimated impact: 50–200× query speedup at 10k+ rows**

Current queries load entire tables. Add pagination at the service layer:

```python
# drs_server/services/srx_drs_service.py — replace get_ff_report_list()
def get_ff_report_page(db: Session, page: int = 0, size: int = 100,
                        group_id: int | None = None) -> tuple[list[FFCreate], int]:
    q = db.query(FF)
    if group_id is not None:
        q = q.filter(FF.response_group_id == group_id)
    total = q.count()
    rows  = q.order_by(FF.id.desc()).offset(page * size).limit(size).all()
    return [FFCreate.model_validate(r) for r in rows], total
```

Add a composite index to the models used for filtering:

```python
# drs_server/models/models.py — inside each report class
__table_args__ = (
    Index('ix_ff_group_cmd', 'response_group_id', 'response_command_id'),
)
```

Also add to `FH`, `Burst`, `RadarTrackReport`.

---

### P-2 · Batch Insert Instead of Per-Row Commits

**Estimated impact: 10–100× write throughput improvement**

```python
# Current (srx_drs_service.py:228-236) — N commits
for storing_data in storing_data_list:
    db.add(FF(**fields))
    db.commit()

# Fix — single commit
db.bulk_insert_mappings(FF, [build_ff_mapping(d) for d in storing_data_list])
db.commit()
```

For 100-row batches, this changes 100 `BEGIN/INSERT/COMMIT` cycles to 1.

---

### P-3 · Scope DB Session per Message, Not per Thread

**Estimated impact: Eliminates identity-map memory growth; prevents stale reads**

```python
# Replace the session-per-thread pattern
def consumeMessages(consumer):
    consumer.subscribe([TOPIC])
    while True:
        msg = consumer.poll(1.0)
        if msg is None or msg.error():
            continue
        with SessionLocal() as db:
            try:
                data = json.loads(msg.value())
                write_to_db(db, data)
                db.commit()
                consumer.commit(asynchronous=False)
            except Exception as e:
                logger.error(f"Failed to process message: {e}")
                db.rollback()
```

Each message gets a fresh session — no identity map accumulation, no stale cache reads, and the connection is returned to the pool between messages.

---

### P-4 · Replace `setInterval` Polling with WebSocket Push

**Estimated impact: Eliminates 20 req/sec baseline load; reduces UI latency to <100ms**

Backend side — broadcast new messages to subscribed clients as they arrive from Kafka:

```python
# drs_server/main.py — add a WebSocket manager
class ConnectionManager:
    def __init__(self): self.active: list[WebSocket] = []
    async def broadcast(self, data: dict):
        for ws in self.active:
            await ws.send_json(data)

manager = ConnectionManager()

@app.websocket("/ws/srx/ff")
async def ws_ff(websocket: WebSocket):
    await websocket.accept()
    manager.active.append(websocket)
    try:
        await websocket.receive_text()   # wait for disconnect
    finally:
        manager.active.remove(websocket)
```

Consumer thread calls `asyncio.run_coroutine_threadsafe(manager.broadcast(msg), app.state.loop)` on each new Kafka message.

Frontend side:

```typescript
// Replace setInterval with WebSocketSubject
import { webSocket } from 'rxjs/webSocket';

this.ffStream$ = webSocket('ws://server/ws/srx/ff').pipe(
    takeUntilDestroyed(this.destroyRef)
);
```

---

### P-5 · Move `RadarTrackReport` JSON Arrays to Child Table

**Estimated impact: Eliminates JSON deserialisation per row; enables array-level queries**

```python
# New schema
class RadarTrackReport(Base):
    id             = Column(Integer, primary_key=True)
    group_id       = Column(Integer, index=True)
    command_id     = Column(Integer, index=True)
    # scalar fields only
    frequency      = Column(Float)
    power_dbm      = Column(Float)
    # ...

class RadarTrackHop(Base):
    id             = Column(Integer, primary_key=True)
    report_id      = Column(Integer, ForeignKey("radar_track_report.id"), index=True)
    hop_index      = Column(SmallInteger)
    frequency_hz   = Column(Float)
    pri_us         = Column(Float)
```

Querying hop data for a single report becomes a simple join; the parent table stays narrow and cache-friendly.

---

### P-6 · Switch drs_bridge TCP Servers to asyncio

**Estimated impact: Supports 100+ concurrent device connections on a single core; eliminates GIL contention**

```python
# drs_bridge/base/async_tcp_server.py
import asyncio, json
from aiokafka import AIOKafkaProducer

class AsyncTcpServer:
    def __init__(self, host: str, port: int, topic: str, kafka_broker: str):
        self.host, self.port, self.topic, self.kafka_broker = host, port, topic, kafka_broker

    async def handle(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
        buffer = b""
        async with AIOKafkaProducer(bootstrap_servers=self.kafka_broker) as producer:
            while True:
                try:
                    chunk = await asyncio.wait_for(reader.read(4096), timeout=30.0)
                except asyncio.TimeoutError:
                    break                             # dead connection
                if not chunk:
                    break
                buffer += chunk
                while (frame := self.extract_frame(buffer)) is not None:
                    msg, buffer = frame
                    await producer.send(self.topic, json.dumps(self.parse(msg)).encode())

    async def serve(self):
        server = await asyncio.start_server(self.handle, self.host, self.port)
        async with server:
            await server.serve_forever()

    def extract_frame(self, buffer: bytes):
        raise NotImplementedError

    def parse(self, frame: bytes) -> dict:
        raise NotImplementedError
```

Each hardware variant overrides only `extract_frame()` and `parse()`. All five TCP server files collapse into five small subclasses.

---

### P-7 · Use SQLAlchemy 2.0 Async API in drs_server

**Estimated impact: FastAPI event loop unblocked; throughput scales with concurrent connections**

The project already has `SQLAlchemy==2.0.30`. Switching to the async engine requires:

```python
# drs_server/configs/database.py
from sqlalchemy.ext.asyncio import create_async_engine, AsyncSession, async_sessionmaker

engine = create_async_engine(
    settings.DATABASE_URL.replace("mysql+pymysql", "mysql+aiomysql"),
    pool_size=20, max_overflow=10
)
AsyncSessionLocal = async_sessionmaker(engine, expire_on_commit=False)

async def get_db():
    async with AsyncSessionLocal() as session:
        yield session
```

```python
# drs_server/routers/srx_router.py
@router.get("/ff/")
async def fetch_ff_reports(page: int = 0, size: int = 100,
                            db: AsyncSession = Depends(get_db)):
    result = await db.execute(
        select(FF).order_by(FF.id.desc()).offset(page * size).limit(size)
    )
    return result.scalars().all()
```

---

### P-8 · Reduce Kafka Producer Overhead in ewtss-backend

The `ewtss-backend` producer creates a new `Producer` instance on each call in some paths. Creating a Confluent Kafka producer involves connecting to the broker and resolving metadata — it should be created once and reused.

```python
# ewtss-backend/producer/srx_new/Kafka_server.py
# Move producer creation outside of the message loop
_producer: Producer | None = None

def get_producer() -> Producer:
    global _producer
    if _producer is None:
        _producer = Producer({'bootstrap.servers': KAFKA_BROKER,
                               'compression.type': 'lz4',
                               'batch.num.messages': 1000,
                               'linger.ms': 5})
    return _producer
```

Adding `linger.ms=5` and `batch.num.messages=1000` enables micro-batching, which can double throughput for high-frequency scenarios.

---

### AP-13 · No TCP Stream Reassembly — Partial Frames Silently Dropped

**Category:** Protocol correctness — Severity: Critical

**Evidence:**

```python
# drs_bridge/srx/random_srx/tcp_server.py (and all variants)
data = connection.recv(4096)      # fixed-size read, no accumulation

# drs_bridge/srx/random_srx/network_utils.py:219-238
def process_sequence_command(connection, command, ...):
    delimiter = b"\xaa\xab\xba\xbb"
    segments = command.split(delimiter)
    for segment in segments[1:]:
        if b'\xcc\xcd\xdc\xdd' in segment:   # ← only processes complete frames
            ...                               # silently discards the rest
```

`recv(4096)` is a best-effort read — TCP is a stream protocol and delivers bytes in arbitrary chunks. A 6,404-byte FFT response (Group 1, CmdID 44) will routinely arrive across two or more `recv()` calls. The `split(delimiter)` approach handles *multiple complete frames in one `recv()`* (the multi-frame case), but there is no accumulation buffer for the inverse case — a *partial frame split across two reads*. The second call to `recv()` starts a fresh `data` variable with no memory of the incomplete prefix, so the partial frame is discarded.

At 10 Hz per device this is intermittent and hard to notice. At 100 devices × 20 Hz with 6 KB responses the probability of a split read approaches certainty.

**Evidence of the gap:** `process_sequence_command` checks `if b'\xcc\xcd\xdc\xdd' in segment` before processing — this guard exists precisely because the code already knows incomplete frames arrive, but the response is to skip them rather than buffer them.

**Fix:** Maintain a per-connection accumulation buffer and consume complete frames in a loop:

```python
buffer = b""
while True:
    chunk = connection.recv(65536)
    if not chunk:
        break
    buffer += chunk
    while True:
        frame, frame_type, consumed = try_extract_frame(buffer)
        if frame is None:
            break          # need more bytes
        buffer = buffer[consumed:]
        handle_frame(frame, frame_type)
```

The v2 design (`asyncio` TCP server + C++ `extract_frame`) addresses this at the architecture level.

---

### AP-14 · Frame Magic Bytes as Inline Literals in Multiple Files

**Category:** Maintainability — Severity: Medium

**Evidence:**

The command frame delimiter `b"\xaa\xab\xba\xbb"` appears as an inline literal in at least four separate files:

```python
# drs_bridge/srx/random_srx/network_utils.py:224
delimiter = b"\xaa\xab\xba\xbb"

# drs_bridge/srx/sg_srx/kafka_server.py:93
segments = data.split(b"\xaa\xab\xba\xbb")

# drs_bridge/mrx/sg_mrx/kafka_server.py:102
segments = data.split(b"\xaa\xab\xba\xbb")

# drs_bridge/jsvushf_mrx/ (mirrors mrx exactly)
```

The response header/footer are stored differently — as escaped string literals in `drs_constant.py`:

```python
# drs_bridge/constants/drs_constant.py:6-7
RESPONSE_HEADER = "\\xEE\\xEF\\xFE\\xFF"   # escaped string, not bytes
RESPONSE_FOOTER = "\\xFF\\xFE\\xEF\\xEE"
```

These are then assembled and re-converted at send time via `bytes.fromhex()` and string replace operations — an unnecessary indirection that differs from how the command header is handled in the same codebase.

**Consequences:**
1. A protocol version change that updates any magic byte requires hunting down every occurrence across all variant directories.
2. The inconsistency between `bytes` literals (command header) and escaped `str` (response header) means a developer editing one context cannot assume the other works the same way.
3. There is no compile-time or import-time check that the constants are consistent across files.

**Fix:** Define all magic byte sequences in a single `constants/frame_constants.py`, import everywhere:

```python
# drs_bridge/constants/frame_constants.py
CMD_HEADER  = b"\xaa\xab\xba\xbb"
CMD_FOOTER  = b"\xcc\xcd\xdc\xdd"
RESP_HEADER = b"\xee\xef\xfe\xff"
RESP_FOOTER = b"\xff\xfe\xef\xee"
SCD_HEADER  = b"\xaa\xaa"
SCD_FOOTER  = b"\xee\xee"
```

In the v2 design this is handled by `{hw}_frame_types.h` — all magic bytes live in the C++ header generated from the ICD document.

---

### AP-15 · ICD Schema Transcribed Into Fragile CSV with `ast.literal_eval`

**Category:** Developer tooling / maintainability — Severity: High

**Evidence:**

```python
# drs_bridge/srx/commands/command.csv (excerpt, representative)
# command_name,group_id,command_id,response_id,...,request_parameters,response_parameters
# "Get System Version",100,1,2,...,"[{'field': 'fw_version', 'type': 'uint32', ...}]",...
```

```python
# drs_bridge/srx/random_srx/network_utils.py (command loading)
import ast, csv

with open("commands/command.csv") as f:
    command_data = list(csv.DictReader(f))

# At dispatch time:
request_params = ast.literal_eval(row["request_parameters"])  # Python literal in a CSV cell
```

The ICD is essentially transcribed manually from the original document into these CSV files, with parameter field schemas embedded as Python literals inside CSV cells. This creates several failure modes:

1. **Silent parse failure:** Any quoting error, encoding issue, or special character in a field name causes `ast.literal_eval` to return `None`. The command silently receives no parameters — no exception, no log entry.
2. **No traceability:** There is no link between `command.csv` and the original ICD Excel. If the ICD is revised, developers must manually diff the CSV against the new document.
3. **Duplication per variant:** `drs_bridge/srx/commands/command.csv` and `ewtss-backend/producer/srx_new/commands/command.csv` are separate copies of the same data with different column names. They have diverged and must be kept in sync manually.
4. **No schema validation:** There is no type checking, range validation, or completeness check on the CSV. A missing `response_id` column value is indistinguishable from a command that has no response.

**Fix:** Replace the CSV-based approach with the ICD code generator (Section 20 of the design spec). The generator reads the authoritative ICD Excel directly, produces C++ constants and dispatch skeletons, and eliminates the manual transcription step entirely.

---

## Summary Table

| ID | Category | File | Impact | Effort |
|----|----------|------|--------|--------|
| AP-1 | Session-per-application | `*_kafka_setup.py:72` | Memory leak, stale reads | S |
| AP-2 | Unmonitored daemon threads | `main.py:271-295` | Silent data loss | S |
| AP-3 | Consumer exits on error | `srx_kafka_setup.py:52` | Silent data loss | XS |
| AP-4 | Auto-commit before DB write | `srx_kafka_setup.py:14` | Message loss on crash | XS |
| AP-5 | Per-row commits + sync=False | `srx_drs_service.py:199` | Write throughput | S |
| AP-6 | 150-column god table | `models.py:492` | Query performance | L |
| AP-7 | No foreign keys | `models.py` | Data integrity | M |
| AP-8 | Shared mutable flag dict | `control_flag.py:3` | Race condition, dup streams | S |
| AP-9 | Producer across thread pool | `Kafka_server.py:75` | Crash / message loss | S |
| AP-10 | Polling instead of WebSocket | 20+ `.ts` files | 20 req/s idle load | M |
| AP-11 | `query.all()` no limit | `srx_drs_service.py:22` | OOM / timeout | S |
| AP-12 | `=+1` vs `+=1` bug | `tcp_server.py` | Incorrect state | XS |
| AP-13 | No TCP stream reassembly | `*/tcp_server.py`, `network_utils.py` | Partial frames silently dropped | S |
| AP-14 | Magic bytes as inline literals | `network_utils.py` ×4, `drs_constant.py` | Protocol change requires grep hunt | XS |
| AP-15 | ICD schema in `ast.literal_eval` CSV | `*/commands/command.csv` | Silent parse failures, no traceability | L |
| TS-1 | Sync ORM in async FastAPI | `routers/srx_router.py` | Threadpool saturation | M |
| TS-2 | Dual Kafka clients | `requirements.txt:10,28` | Confusion, bloat | XS |
| TS-3 | numpy 2.0 + pandas 2.2 | `requirements.txt:33-35` | Runtime ImportError | XS |
| TS-4 | Thread model for I/O workload | `drs_bridge/*/tcp_server.py` | GIL contention | L |
| TS-5 | `debug=True` in production | `main.py:36` | Info disclosure | XS |
| TS-6 | Polling vs Kafka push | Angular components | Wasted load | M |
| P-1 | No pagination / no indexes | `*_drs_service.py` | 50–200× speedup | S |
| P-2 | Per-row commits | `*_drs_service.py` | 10–100× write throughput | S |
| P-3 | Session scope | `*_kafka_setup.py` | Stability | S |
| P-4 | WebSocket push | `main.py` + Angular | Eliminate idle load | M |
| P-5 | JSON arrays in main table | `models.py:492` | Query performance | L |
| P-6 | asyncio bridge | `drs_bridge/*/tcp_server.py` | 100× concurrency | L |
| P-7 | SQLAlchemy async API | `drs_server/` | Event loop unblocked | L |
| P-8 | Producer per-call | `Kafka_server.py` | 2× produce throughput | S |

**Effort key:** XS < 2h · S < 1d · M < 1 week · L > 1 week
