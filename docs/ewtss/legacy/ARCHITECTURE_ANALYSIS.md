# EWTSS Architecture Analysis — Strengths, Weaknesses & Alternatives

## 1. Overall Approach — Event-Driven Microservices via Kafka

### What Works
- **Decoupling:** drs_bridge, drs_server, and ewtss-backend never call each other directly. Kafka absorbs timing differences between hardware ingest and API serving.
- **Replay potential:** Kafka's offset-based model means messages can in principle be replayed if a consumer falls behind.
- **Separation of concerns:** Protocol translation (bridge), storage + query (drs_server), and business logic (ewtss-backend) are cleanly separated into independent processes.

### What Doesn't Work

**Kafka used as fire-and-forget, not as a durable bus**

Consumer auto-commit is enabled (`'enable.auto.commit': True`). If drs_server crashes between receiving a message and writing to MySQL, the offset is already committed and the message is silently lost. The system gets Kafka's operational overhead without Kafka's durability guarantees.

**No consumer group strategy**

All consumers use a single group ID per hardware variant. If drs_server is restarted while the bridge is producing at high rate, it will either replay a large backlog or skip it entirely depending on `auto.offset.reset`. There is no controlled catchup mechanism.

**Consumer threads are not supervised**

When a Kafka error occurs (broker restart, network blip), the consumer loop does a `break` and exits silently. No watchdog restarts it. Data stops flowing without any alert.

### Alternative

- Switch to **manual offset commit** (`enable.auto.commit=False`) — commit only after successful DB write.
- Add a **supervisor loop** around each consumer thread. Use exponential backoff to reconnect on failure:

```python
def run_consumer_with_supervision(create_consumer_fn, process_fn):
    backoff = 1
    while True:
        try:
            consumer = create_consumer_fn()
            process_fn(consumer)
        except Exception as e:
            logger.error(f"Consumer crashed: {e}. Restarting in {backoff}s")
            time.sleep(backoff)
            backoff = min(backoff * 2, 60)
```

- Consider replacing Confluent Kafka with **aiokafka** (async) to align with FastAPI's event loop and avoid thread proliferation.

---

## 2. Protocol Bridge Design — Blocking TCP + Threads

### What Works
- Simple: one thread per device connection is easy to reason about.
- Works adequately at small scale (< 10 simultaneous devices).

### What Doesn't Work

**Blocking recv() with no timeout**

`connection.recv(4096)` blocks the thread indefinitely if the hardware stops sending. With 10 devices connected, 10 threads hang. No timeout = no way to detect dead connections.

**Fixed 4 096-byte buffer**

Hardware messages that exceed 4 096 bytes will be silently truncated. The parsing code does not check for incomplete frames or accumulate a receive buffer.

**No protocol framing**

There is no header that declares message length or type. The server reads whatever bytes arrive and passes them to the parser. On a noisy TCP connection (partial reads, coalesced writes), parsing will produce corrupt data with no error raised.

**One-thread-per-connection does not scale**

Adding a new hardware variant today requires copying ~300 lines of TCP server code. Each variant has its own `tcp_server.py` with identical structure. The system currently has at least 5 such copies.

### Alternative

**asyncio-based TCP server with a shared protocol parser:**

```python
async def handle_device(reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
    buffer = b""
    while True:
        chunk = await asyncio.wait_for(reader.read(4096), timeout=30.0)
        if not chunk:
            break
        buffer += chunk
        while True:
            frame, buffer = try_parse_frame(buffer)  # handles partial reads
            if frame is None:
                break
            await publish_to_kafka(frame)
```

This eliminates thread-per-connection overhead, handles partial reads correctly, and supports timeouts without blocking.

**Abstract a `BaseTcpServer` class** with the common connection lifecycle, leaving only protocol-specific parsing in subclasses. Five near-identical files become one base class and five small subclasses.

---

## 3. Database Design — Wide, Flat Tables

### What Works
- Single-table reads are fast for simple queries.
- Flat schema is easy to explain to non-engineers.

### What Doesn't Work

**Denormalised time storage**

`toa_hours`, `toa_minutes`, `toa_seconds`, `toa_reserved` are four separate `String(255)` columns for a single time-of-arrival value. This wastes storage, makes range queries impossible, and introduces type confusion (String for what is a number).

**JSON column alongside structured columns**

`wideband_fft_data = Column(JSON)` stores unstructured array data in a relational table. The result is a hybrid — some attributes are queryable, one is not. Indexes cannot be placed on the JSON content, so any query filtering on FFT data does a full table scan.

**No pagination at the ORM layer**

`db.query(FF).all()` is called in service files. At 1 million rows (realistic for a long test run), this allocates hundreds of megabytes in Python memory and times out before the response is sent.

**Missing composite indexes**

Queries filter by `(response_group_id, response_command_id)` but only individual column indexes exist. Every query does two index lookups instead of one.

### Alternative

- **Normalise TOA:** Use a single `DateTime` or `Time` column.
- **Move wideband FFT data to a separate child table** or store in object storage (filesystem / MinIO) referenced by ID. The relational table stores metadata; binary/array data lives outside.
- **Add `.limit()` + `.offset()` to all service queries:**

```python
def get_ff_report_page(db: Session, page: int = 0, page_size: int = 100):
    return db.query(FF).order_by(FF.id.desc()).offset(page * page_size).limit(page_size).all()
```

- **Add composite index:**

```python
__table_args__ = (Index('ix_ff_group_cmd', 'response_group_id', 'response_command_id'),)
```

---

## 4. Configuration & Secrets Management

### What Works
- `.env` file pattern is a reasonable baseline for local development.
- `python-dotenv` + `pydantic BaseSettings` is an established approach.

### What Doesn't Work

**JWT secret key hardcoded in source**

`SECRET_KEY = '7120d7628....'` in `ui_constants/drs_constant.py` means the secret is in version control history permanently. Any developer with repo access can forge JWT tokens for any user.

**CORS wildcard with credentials**

```python
origins = ["*"]
allow_credentials=True
```

`allow_credentials=True` combined with `allow_origins=["*"]` is rejected by modern browsers — but the intent is to allow all origins. In a permissive browser or during testing, this permits cross-origin requests with cookies/auth headers from any domain.

**Default credentials created at startup**

User `operator/operator` is inserted if no user exists. This default account is documented in the codebase and is likely unchanged in field deployments.

**Hardcoded IP in Angular**

`baseUrl = 'http://192.168.10.43:9000'` means the frontend must be recompiled for every deployment site. Angular's `environment.ts` mechanism exists precisely for this.

### Alternative

- **Move all secrets to environment variables only — no defaults for security-sensitive values:**

```python
class Settings(BaseSettings):
    SECRET_KEY: str  # No default — will raise if not set
    DB_PASSWORD: str  # No default
```

- **Use Angular environments:**

```typescript
// environment.prod.ts
export const environment = { apiBaseUrl: 'http://192.168.10.43:9000' };
```

- **Scope CORS to known frontend origin:**

```python
origins = [os.getenv("FRONTEND_ORIGIN", "http://localhost:4200")]
```

- **Force password change on first login** rather than shipping with `operator/operator`.

---

## 5. Thread Management

### What Works
- Threading is simple and compatible with synchronous SQLAlchemy.

### What Doesn't Work

**Unbounded thread creation**

There is no cap on how many threads can be spawned. Each Kafka consumer topic gets a thread; each hardware TCP connection gets a thread. 18 Kafka consumer threads + N device threads + uvicorn worker threads share a single Python GIL. Under load, context-switching overhead dominates.

**Daemon threads lose in-flight data on shutdown**

All threads are `daemon=True`. When the main process exits (e.g., Ctrl-C), all threads are killed immediately — even if they are mid-transaction with MySQL. Partially written rows, uncommitted Kafka offsets, and dropped TCP connections are the result.

**No health monitoring**

There is no way to know whether a consumer thread is alive. A thread can silently die (Kafka error, DB connection drop) and the system continues serving stale data without any indication.

### Alternative

- **Use a thread pool with a bounded size:** `concurrent.futures.ThreadPoolExecutor(max_workers=24)`
- **Replace daemon threads with graceful shutdown hooks:**

```python
stop_event = threading.Event()

def consumer_thread(stop_event):
    while not stop_event.is_set():
        msg = consumer.poll(1.0)
        ...

@app.on_event("shutdown")
async def shutdown():
    stop_event.set()
    for t in threads:
        t.join(timeout=10)
```

- **Add a health endpoint that reports per-thread liveness.**

---

## 6. API Design — Inconsistent Contracts

### What Works
- FastAPI's automatic OpenAPI generation when Pydantic schemas are used.
- Dependency injection for DB sessions is correctly implemented.

### What Doesn't Work

**Mixed response formats**

Some endpoints return Pydantic models, others return raw dicts with a custom `statusCode` field, others return SQLAlchemy ORM objects directly. The frontend must handle all three shapes.

**No versioning**

All routes are at `/endpoint`. Adding a breaking change requires coordinating an instant cutover between frontend and all three backends.

**No pagination contract**

Endpoints that could return large datasets (FF reports, FH reports) have no page/limit parameters. Clients have no way to request a subset.

### Alternative

- **Standardise on a single envelope:**

```python
class ApiResponse(BaseModel, Generic[T]):
    success: bool
    data: Optional[T]
    error: Optional[str]
    page: Optional[int]
    total: Optional[int]
```

- **Version APIs at the router level:** `APIRouter(prefix="/api/v1")`
- **Mandate Pydantic schemas** for all request bodies and response models. Enforce via a linting rule.

---

## 7. Frontend Architecture

### What Works
- Modern Angular standalone components reduce boilerplate.
- Centralised `ApiService` and `AuthInterceptor` are the right structural choices.

### What Doesn't Work

**Token stored in localStorage**

`localStorage` is accessible to any JavaScript on the page. An XSS vulnerability anywhere in the app (including third-party libraries like OpenLayers) can exfiltrate the token.

**No observable unsubscription**

Components that subscribe to HTTP observables without `takeUntilDestroyed` or `async` pipe will leak memory when the component is destroyed.

**Geoserver credentials in source**

`btoa('admin:geoserver')` in the auth interceptor hardcodes credentials that are visible in the compiled bundle and in browser DevTools.

### Alternative

- **Use `HttpOnly` cookies for JWT** instead of localStorage. The browser sends them automatically and JavaScript cannot read them.
- **Use the `async` pipe in templates** instead of manual `.subscribe()` calls — automatic unsubscription on destroy.
- **Move Geoserver credentials to the backend** — proxy tile requests through ewtss-backend so frontend never holds credentials.

---

## Summary

| Decision | Current Approach | Recommended Alternative |
|----------|-----------------|------------------------|
| Kafka reliability | Auto-commit, no reconnect | Manual commit + supervised consumer loop |
| TCP protocol bridge | Blocking thread-per-connection | asyncio `StreamReader` with frame buffer |
| Code sharing across variants | Copy-paste (~5× duplication) | `BaseTcpServer` + variant subclasses |
| Database large rows | `query.all()` — no limit | Paginated queries, composite indexes |
| Secrets | Hardcoded in source / defaults | Env vars with no fallback for secrets |
| Thread lifecycle | Unmonitored daemon threads | Bounded pool + graceful shutdown |
| API contracts | Mixed response shapes | Typed envelope + versioned routers |
| Frontend auth token | localStorage | HttpOnly cookie or short-lived token |
