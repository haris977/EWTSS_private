# EWTSS Code Quality Analysis & Improvement Plan

## Executive Summary

The codebase is functional and demonstrates clear domain understanding, but carries significant technical debt in five areas: **security** (secrets hardcoded in source), **resilience** (no error recovery), **duplication** (hardware variants copy-pasted), **scalability** (unbounded memory queries, no pagination), and **observability** (inconsistent logging, no monitoring). The improvements below are grouped by priority and can be tackled incrementally.

---

## Critical — Address Before Next Deployment

### C1. Remove hardcoded secrets from source

**Files affected:**
- `drs_server/ui_constants/drs_constant.py` — JWT `SECRET_KEY` hardcoded
- `drs_server/utils/auth_utilis.py` — default `operator/operator` credentials created at startup
- `src/app/shared/interceptor/auth.interceptor.ts` — Geoserver `admin:geoserver` in `btoa()`

**Problem:** Any developer with repo access can forge JWT tokens. Default credentials are documented in the source and likely unchanged in field deployments.

**Fix:**
1. Load `SECRET_KEY` from environment only, with no fallback:
   ```python
   # settings.py
   SECRET_KEY: str  # pydantic raises on missing — intentional
   ```
2. Replace the startup-created `operator/operator` account with a first-run setup flow that forces password creation.
3. Remove Geoserver credentials from the frontend; proxy tile requests through `ewtss-backend` so the backend holds the credential.

---

### C2. Disable `debug=True` in both FastAPI apps

**Files:** `drs_server/main.py:36`, `ewtss-backend/main.py:52`

```python
app = FastAPI(debug=True)   # exposes stack traces to clients
```

**Fix:** Read from environment:
```python
app = FastAPI(debug=os.getenv("APP_DEBUG", "false").lower() == "true")
```

---

### C3. Restrict CORS — wildcard + credentials is dangerous

**Files:** `drs_server/main.py:38–46`, `ewtss-backend/main.py:56–64`

```python
origins = ["*"]
allow_credentials=True   # browsers block this combination, but it signals intent
```

**Fix:**
```python
origins = [os.getenv("FRONTEND_ORIGIN", "http://localhost:4200")]
app.add_middleware(CORSMiddleware, allow_origins=origins,
                  allow_credentials=True, allow_methods=["GET","POST","PUT","DELETE"],
                  allow_headers=["Authorization","Content-Type"])
```

---

### C4. Validate path parameters in the tile endpoint

**File:** `ewtss-backend/main.py` — `GET /{z}/{x}/{y}.png`

```python
file_path = os.path.join(TILE_DIRECTORY, str(z), str(x), f"{y}.png")
```

`z`, `x`, `y` are typed `int` by FastAPI but can be negative. A path like `/../../etc/passwd` becomes a directory traversal.

**Fix:**
```python
@app.get("/{z}/{x}/{y}.png")
async def serve_tile(z: int, x: int, y: int):
    if z < 0 or x < 0 or y < 0:
        raise HTTPException(status_code=400)
    file_path = os.path.realpath(os.path.join(TILE_DIRECTORY, str(z), str(x), f"{y}.png"))
    if not file_path.startswith(os.path.realpath(TILE_DIRECTORY)):
        raise HTTPException(status_code=403)
    ...
```

---

## High Priority — Address Within One Sprint

### H1. Add Kafka consumer reconnection and supervision

**Files:** `drs_server/services/*_kafka_setup.py` (18 files)

**Problem:** On any non-EOF Kafka error the consumer breaks its loop and exits silently. Data stops flowing; the service appears healthy.

**Fix — wrap every consumer in a supervisor:**
```python
def run_supervised(create_fn, process_fn, logger):
    backoff = 1
    while True:
        consumer = None
        try:
            consumer = create_fn()
            process_fn(consumer)
        except Exception as e:
            logger.error(f"Consumer failed: {e}. Restarting in {backoff}s")
            time.sleep(backoff)
            backoff = min(backoff * 2, 60)
        finally:
            if consumer:
                consumer.close()
```

Replace the current `break` on error with `raise` so the supervisor catches it.

**Also:** Switch to manual offset commit so messages are not lost if the DB write fails:
```python
consumer = Consumer({..., 'enable.auto.commit': False})
# After successful DB write:
consumer.commit(asynchronous=False)
```

---

### H2. Add pagination to all list endpoints

**Files:** `drs_server/services/*_drs_service.py` — every `get_*_report_list()` function

**Problem:** `db.query(FF).all()` loads the entire table into memory. At scale this causes OOM and request timeouts.

**Fix — add page/size parameters at the service layer:**
```python
def get_ff_report_page(db: Session, page: int = 0, size: int = 100):
    return (db.query(FF)
              .order_by(FF.id.desc())
              .offset(page * size)
              .limit(size)
              .all())
```

Update routers to accept `?page=0&size=100` query parameters. Return total count alongside data so the frontend can build pagination controls.

---

### H3. Add error handling to startup initialisation

**File:** `drs_server/main.py` — `startup_event()`

**Problem:** Ten+ initialisation calls with no error handling. A single failure (e.g., DB unreachable) stops all remaining initialisation silently.

**Fix:**
```python
@app.on_event("startup")
async def startup_event():
    db = next(get_db())
    try:
        steps = [
            ("master data", lambda: insert_master_data(db)),
            ("SRX health status", lambda: store_srx_health_status_details(db)),
            # ...
        ]
        for name, fn in steps:
            try:
                fn()
            except Exception as e:
                logger.error(f"Startup step '{name}' failed: {e}")
    finally:
        db.close()
```

---

### H4. Fix operator `=+` bug (not `+=`) in TCP server state machines

**Files:** `drs_bridge/*/random_*/tcp_server.py`

```python
DATA_RANDOM_FLOW =+1   # Sets to +1 every iteration — should be +=1
DATA_SG_FLOW =+1       # Same bug
```

These counters are used to gate one-time log writes. The bug causes the log write to trigger on every call, not just the first.

**Fix:** Replace all `=+1` with `+= 1` across all TCP server files.

---

### H5. Fix the Kafka producer/consumer unpacking bug in drs_bridge

**File:** `drs_bridge/srx/random_srx/tcp_server.py:72`

```python
producer, consumer = kafka_server.createProducer()   # createProducer returns one object
consumer.commit()  # AttributeError at runtime
```

**Fix:** Read `createProducer()`'s return value and either fix the unpacking or fix the function signature.

---

### H6. Replace `print()` with logger in drs_bridge

**Files:** `drs_bridge/*/tcp_server.py`

```python
print("Connection reset by peer")
print(f"An error occurred: {e}")
```

Errors written to stdout are lost in production. Replace with `logger.error(...)` using the existing logger pattern from `drs_server`.

---

## Medium Priority — Address Within One Month

### M1. Eliminate hardware-variant code duplication

**Problem:** Five near-identical `tcp_server.py` files. Five near-identical `*_kafka_setup.py` files. Twelve near-identical `store_*_health_status_details()` functions in `drs_server/main.py`.

**Plan — extract a `BaseTcpServer`:**

```python
# drs_bridge/base/tcp_server.py
class BaseTcpServer:
    def __init__(self, host, port, constants, kafka_broker):
        self.host = host
        self.port = port
        self.constants = constants

    def parse_message(self, data: bytes) -> dict:
        raise NotImplementedError  # variants override this

    def handle_client(self, conn):
        buffer = b""
        while True:
            chunk = conn.recv(4096)
            if not chunk:
                break
            buffer += chunk
            msg, buffer = self.try_extract_frame(buffer)
            if msg:
                self.publish(self.parse_message(msg))
```

Each variant becomes a subclass with only `parse_message()` overridden.

**Plan — extract a generic health status initialiser:**
```python
def initialise_health_status(db, model_class, constants_module, logger):
    if not db.query(model_class).first():
        fields = {k: getattr(constants_module, k) for k in model_class.__table__.columns.keys()
                  if hasattr(constants_module, k)}
        db.add(model_class(**fields))
        db.commit()
```

Replace twelve near-identical functions with twelve calls to this single function.

---

### M2. Standardise API response format

**Problem:** Endpoints return Pydantic models, raw dicts, ORM objects, and custom `{"statusCode": ..., "message": ...}` dicts depending on who wrote the route.

**Fix — a single typed envelope:**
```python
from typing import Generic, TypeVar, Optional
T = TypeVar("T")

class ApiResponse(BaseModel, Generic[T]):
    success: bool
    data: Optional[T] = None
    error: Optional[str] = None
    page: Optional[int] = None
    total: Optional[int] = None

# Usage
@router.get("/reports", response_model=ApiResponse[List[FFResponse]])
def list_reports(page: int = 0, size: int = 100, db: Session = Depends(get_db)):
    data = get_ff_report_page(db, page, size)
    return ApiResponse(success=True, data=data, page=page, total=count_ff(db))
```

---

### M3. Add composite indexes and fix column types

**File:** `drs_server/models/models.py`

| Issue | Fix |
|-------|-----|
| `toa_hours`, `toa_minutes`, `toa_seconds` as `String(255)` | Merge into single `toa = Column(Time)` |
| No composite index on `(response_group_id, response_command_id)` | Add `Index('ix_ff_group_cmd', 'response_group_id', 'response_command_id')` |
| `wideband_fft_data` JSON in FF table | Move to a child table `FFSpectrum(ff_id, data LONGBLOB)` |
| Boolean fields as `Integer` | Use `Column(Boolean)` |

---

### M4. Add log rotation

**File:** `drs_server/configs/loggerConfig.py`

```python
# Replace FileHandler with RotatingFileHandler
from logging.handlers import RotatingFileHandler

handler = RotatingFileHandler(
    fileName,
    maxBytes=10 * 1024 * 1024,   # 10 MB per file
    backupCount=5
)
```

Apply the same to all three backends. Without rotation, log files grow unboundedly and will fill the disk during extended test runs.

---

### M5. Unify logging strategy across drs_bridge and drs_server

**Problem:** `drs_server` uses Python's `logging` module; `drs_bridge` uses a custom `saveDrsLogs()` wrapper with `DrsLogType` / `LogLevel` enums. Operators cannot search logs from a single tool.

**Fix:**
1. Map `DrsLogType`/`LogLevel` enums to Python log levels.
2. Have `saveDrsLogs()` delegate to a standard `logging.Logger`:
   ```python
   def saveDrsLogs(log_level, log_type, message, *args):
       logger = logging.getLogger(f"drs.{log_type}")
       level = LOG_LEVEL_MAP.get(log_level, logging.INFO)
       logger.log(level, message, *args)
   ```
3. This is a backwards-compatible shim — no call sites need to change.

---

### M6. Fix Angular memory leaks and hardcoded API URL

**Files:** `src/app/shared/services/api.service.ts`, component files

1. **Move `baseUrl` to Angular environment:**
   ```typescript
   // src/environments/environment.ts
   export const environment = { production: false, apiBaseUrl: 'http://localhost:9000' };
   // environment.prod.ts
   export const environment = { production: true, apiBaseUrl: 'http://192.168.10.43:9000' };
   ```
   ```typescript
   // api.service.ts
   import { environment } from '../../../environments/environment';
   baseUrl = environment.apiBaseUrl;
   ```

2. **Use `takeUntilDestroyed` in components:**
   ```typescript
   import { takeUntilDestroyed } from '@angular/core/rxjs-interop';
   
   this.apiService.getData()
     .pipe(takeUntilDestroyed(this.destroyRef))
     .subscribe(...)
   ```

---

## Lower Priority — Ongoing Refactoring

### L1. Add `DEBUG` level logging

Current code logs only at `INFO` and `ERROR`. When debugging field issues, there is no way to increase verbosity without code changes. Add `logger.debug()` calls at:
- Message parse entry/exit in drs_bridge
- Each Kafka poll cycle (rate-limited to avoid log spam)
- DB query execution in service layer

Control verbosity via `LOG_LEVEL` environment variable:
```python
logger.setLevel(os.getenv("LOG_LEVEL", "INFO").upper())
```

---

### L2. Add Pydantic schemas to all unvalidated endpoints

Any endpoint accepting `str` path/query parameters that are passed to DB queries or file paths should have an explicit `Literal` or `Enum` type:

```python
from enum import Enum
class SortField(str, Enum):
    created_on = "created_on"
    id = "id"

@app.get("/scenario/logs/{key_sort}/{asc_desc}/{page}")
async def scenario_log_stream(key_sort: SortField, asc_desc: bool, page: int = Query(ge=0)):
    ...
```

This prevents SQL injection via path parameters and is automatically documented in Swagger.

---

### L3. Normalise DB session management

**Problem:** In Kafka consumer threads, `get_db()` is called once at startup and the session is held open for the lifetime of the thread. Long-lived sessions accumulate stale data in the SQLAlchemy identity map and can hit MySQL's `wait_timeout`.

**Fix:** Open a short-lived session per message batch, not per thread:
```python
def process_message(msg_value):
    with SessionLocal() as db:
        write_to_db(db, msg_value)
        db.commit()
```

---

### L4. Remove Windows-specific absolute paths from production code

**File:** `ewtss-backend/main.py`

```python
TILE_DIRECTORY_SHAPE = 'D:/Maps/Maps/Geographical_layers'
```

**Fix:** Read from environment variable with a relative default:
```python
TILE_DIRECTORY = os.getenv("TILE_DIRECTORY", "./maps/tiles")
```

---

## Improvement Tracking

| ID | Category | Effort | Impact | Status |
|----|----------|--------|--------|--------|
| C1 | Security | S | Critical | TODO |
| C2 | Security | XS | Critical | TODO |
| C3 | Security | S | Critical | TODO |
| C4 | Security | S | Critical | TODO |
| H1 | Resilience | M | High | TODO |
| H2 | Scalability | M | High | TODO |
| H3 | Reliability | S | High | TODO |
| H4 | Bug fix | XS | High | TODO |
| H5 | Bug fix | XS | High | TODO |
| H6 | Observability | S | Medium | TODO |
| M1 | Maintainability | L | High | TODO |
| M2 | API quality | M | Medium | TODO |
| M3 | Performance | M | Medium | TODO |
| M4 | Observability | S | Medium | TODO |
| M5 | Observability | M | Medium | TODO |
| M6 | Maintainability | M | Medium | TODO |
| L1 | Observability | M | Low | TODO |
| L2 | Security | M | Medium | TODO |
| L3 | Reliability | M | Low | TODO |
| L4 | Portability | XS | Low | TODO |

**Effort key:** XS < 2h · S < 1d · M < 1 week · L > 1 week
