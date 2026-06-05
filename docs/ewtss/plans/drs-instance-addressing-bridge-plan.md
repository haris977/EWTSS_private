# DRS Instance Addressing — Contracts + drs-bridge Implementation Plan (Plan 1 of 3)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make drs-bridge drive **one TCP command server per DRS *instance*** (not per variant), reconciled live from an active roster delivered over a compacted Kafka `drs.roster` topic, with hot-reload on roster change and a framing-probation identity guard on each inbound ECS connection.

**Architecture:** A new `drs.roster` Kafka contract carries the active roster snapshot (`roster_id`, `version`, `entries[]`). drs-bridge gains its first Kafka *consumer* (mirroring drs-server's `ControlConsumer`) that deserialises each snapshot into a Pydantic `Roster` and hands it to a refactored `Runtime`, which diffs against currently-bound instances and binds/tears-down/rebinds per-instance servers. Each inbound TCP connection is *provisional* until the variant's `extract_frame` (wired through ctypes) accepts a frame; connections that never frame within a byte ceiling or idle timeout are closed and logged. No database and no drs-server changes are in this plan — drs-bridge is built **contract-first against fixture snapshots**.

**Tech Stack:** Python 3.11 (target `py311`), pydantic v2 (`>=2.6`), aiokafka (`>=0.10`), PyYAML, pytest `>=8` + pytest-asyncio (`asyncio_mode = "auto"`), ruff. C++ reference parser built via CMake (integration test only).

**Spec:** [docs/ewtss/specs/drs-instance-addressing-design.md](../specs/drs-instance-addressing-design.md) ([B1.43](../design-backlog.md)).

---

## Application target (multi-app repo — read this)

This plan modifies exactly three areas:
- **`drs-bridge/`** (path: `drs-bridge/`) — the only *service* touched.
- **`contracts/`** — adds the `drs.roster` Kafka schema.
- **`infrastructure/kafka/`** — provisions the compacted topic.

It does **NOT** touch `drs-server/`, `sg-app/`, `drs-webapp/`, `mvp4/`, or `mvp*/`. Those are later plans or reference code. Where a path is ambiguous, it is always under `drs-bridge/`.

## Test-breadth statement

Unit tests in each task cover only the path required to wire the next task. Phase-level breadth lives in **Task 3.6** (hot-reload diff across add/remove/rebind) and **Task 5.1** (end-to-end integration with the real parser + a live broker, graceful-skip).

## Out of scope (deliberate — don't flag these as gaps)

- **drs-server roster store, DB tables, `/roster/*` REST, `/exercise/readiness` reconcile, and the `drs.roster` *publisher*** — Plan 2. The system has no DB foundation yet ([infrastructure/kafka/README.md](../../infrastructure/kafka/README.md): *"No code uses Postgres today"*); standing up Postgres/Timescale + a migration/access layer is a cross-cutting decision deferred to Plan 2 (coordinated with F + scenario-management). In this plan drs-bridge is the **consumer**; snapshots come from test fixtures.
- **Sg.App + DRS-webapp surfaces** — Plan 3 (needs [B1.1](../design-backlog.md) wireframes).
- **Data-plane**: parsing accepted frames into measurements and publishing `hw.<variant>.<kind>` topics — deferred to first-IRS work ([kafka-infra-layer-design §3.3](../specs/kafka-infra-layer-design.md)). Here, once probation clears, a frame is attributed to its `instance_id` and logged; it is not parsed into telemetry.
- **Port allocation for `allocated` instances** — drs-server allocates (Plan 2); every roster entry drs-bridge receives carries explicit ports.
- **Cross-entry roster validation** (duplicate id / host:port collision across entries) — authoritative on the drs-server writer (Plan 2). drs-bridge is defensive only: a bad entry that fails to bind is isolated to `FAILED_BIND`, never sinks siblings.
- **Source-IP allowlist** — removed in spec §8.
- **UDP-inbound command binding (GNSS variants).** Plan 1 binds **TCP** command servers only (the reference parser + every current profile is TCP). A roster entry whose `command.protocol == "udp"` is defensively isolated (`FAILED_BIND` / `UDP_COMMAND_UNSUPPORTED`). UDP-inbound command handling and its advisory (non-fatal) framing — spec §7.2 — is deferred to first-IRS/data-plane work. The **UDP `response`** channel (DRS→ECS, outbound) is unaffected; the `UdpSender` is unchanged.
- **B1.3 per-instance time-beacon interaction.** The roster refactor changes the time-beacon's cardinality from per-*variant* to per-*instance*; resolving that is out of scope here. Plan 1 preserves the control-plane variant registration (so drs-server's `SyncStateEngine` still learns per-variant precision via `drs.control`) by calling `publish_variant_registration` directly, but does **not** start per-instance time beacons. Pattern-2 periodic distribution under per-instance addressing is a Plan-2+ interaction.
- **Kafka consumer retry/backoff beyond aiokafka defaults**, and **`/roster/*` OpenAPI** (no Plan-1 component consumes the REST surface; it is frozen in Plan 2 where drs-server implements it and WS1 consumes it).

## Environment pre-flight

- **Python 3.11** is the floor (`requires-python = ">=3.11"`, ruff `target-version = "py311"`). Use 3.11-safe stdlib only.
- **No new top-level directories.** `drs.roster.schema.json` lands under the existing `contracts/kafka/`; all code lands under existing `drs-bridge/src/drs_bridge/` and `drs-bridge/tests/`. No `.gitignore` change needed (the reference-parser `build/` dir is already handled by the existing conftest fixture).
- **CLIs:** `pytest` (installed via `pip install -e ".[dev]"` in `drs-bridge/`). `cmake` + a Kafka broker are needed **only** by Task 5.1, which graceful-skips when they are absent (mirrors [`tests/conftest.py`](../../drs-bridge/tests/conftest.py) and [`test_reference_parser_integration.py`](../../drs-bridge/tests/test_reference_parser_integration.py)). No new always-on dependency.
- **aiokafka** is already a dependency; `AIOKafkaConsumer` needs no new package.
- Run all commands from `drs-bridge/` unless stated. Activate the venv: `drs-bridge/.venv/Scripts/python.exe` (Windows) is the interpreter.

---

## Phase 0 — `drs.roster` contract + topic

### Task 0.1: Add the `drs.roster` Kafka schema to `contracts/`

**Files:**
- Create: `contracts/kafka/drs.roster.schema.json`
- Modify: `contracts/README.md` (status table)

- [ ] **Step 1: Write the schema**

Create `contracts/kafka/drs.roster.schema.json`:

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "ewtss/contracts/kafka/drs.roster.schema.json",
  "title": "drs.roster",
  "description": "drs-server -> drs-bridge active-roster snapshot. Published to the COMPACTED topic 'drs.roster' under the single key 'active'; the latest retained message is the live roster. drs-bridge reconciles its per-instance servers against each snapshot. One snapshot fully describes the desired state (no deltas).",
  "type": "object",
  "required": ["roster_id", "version", "entries"],
  "properties": {
    "roster_id": { "type": "string", "description": "Stable id of this roster, e.g. 'lab-full'." },
    "version": { "type": "integer", "minimum": 1, "description": "Monotonic version; bumped on every committed change. ETag is roster_id@version." },
    "entries": {
      "type": "array",
      "items": {
        "type": "object",
        "required": ["instance_id", "variant", "host", "command", "response", "port_source", "enabled"],
        "properties": {
          "instance_id": { "type": "string", "description": "Logical id, '<variant>#<n>', e.g. 'rdfs#3'." },
          "variant": { "type": "string", "description": "Variant template name, e.g. 'rdfs'." },
          "host": { "type": "string", "description": "Bind host for this instance's servers." },
          "command": { "$ref": "#/$defs/endpoint" },
          "response": { "$ref": "#/$defs/endpoint" },
          "port_source": { "type": "string", "enum": ["irs_fixed", "allocated"] },
          "enabled": { "type": "boolean", "description": "Disabled instances are not bound (operator out-of-service)." }
        }
      }
    }
  },
  "$defs": {
    "endpoint": {
      "type": "object",
      "required": ["port", "protocol"],
      "properties": {
        "port": { "type": "integer", "minimum": 0, "maximum": 65535 },
        "protocol": { "type": "string", "enum": ["tcp", "udp"] }
      }
    }
  }
}
```

- [ ] **Step 2: Update the contracts status table**

In `contracts/README.md`, add this row to the status table (after the `Kafka system.health` row):

```markdown
| Kafka `drs.roster` | [`kafka/drs.roster.schema.json`](kafka/drs.roster.schema.json) | drs-server → drs-bridge | **baselined** (compacted; full-snapshot active roster, key `active`) — consumer shipped in [B1.43](../docs/ewtss/design-backlog.md) drs-bridge plan; producer in Plan 2 |
```

- [ ] **Step 3: Commit**

```bash
git add contracts/kafka/drs.roster.schema.json contracts/README.md
git commit -m "contracts: add drs.roster compacted active-roster schema (B1.43)"
```

### Task 0.2: Provision the compacted `drs.roster` topic

**Files:**
- Modify: `infrastructure/kafka/create-topics.py`

- [ ] **Step 1: Write the failing test**

Create `infrastructure/kafka/test_create_topics_config.py`:

```python
"""Unit check: drs.roster is declared as a compacted topic."""
import importlib.util
from pathlib import Path

_MOD_PATH = Path(__file__).resolve().parent / "create-topics.py"
_spec = importlib.util.spec_from_file_location("create_topics", _MOD_PATH)
create_topics = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(create_topics)


def test_drs_roster_is_compacted():
    by_name = {t[0]: t for t in create_topics.CONTROL_PLANE_TOPICS}
    assert "drs.roster" in by_name, "drs.roster must be provisioned"
    name, parts, rf, config = by_name["drs.roster"]
    assert config.get("cleanup.policy") == "compact"
    assert parts == 1, "compacted active-roster topic is single-partition"
```

- [ ] **Step 2: Run it to verify it fails**

Run: `python -m pytest infrastructure/kafka/test_create_topics_config.py -v`
Expected: FAIL — `drs.roster` not in `CONTROL_PLANE_TOPICS`, and/or tuple unpack error (current tuples are 3-element).

- [ ] **Step 3: Add the topic with a per-topic config**

In `infrastructure/kafka/create-topics.py`, change the topic list to carry an optional config dict and pass it to `NewTopic`:

```python
CONTROL_PLANE_TOPICS = [
    # name, partitions, replication_factor, topic_configs
    ("drs.control", 1, 1, {}),       # variant.registered + future bridge -> server events
    ("system.timesync", 1, 1, {}),   # SyncStateEngine state transitions
    ("system.health", 1, 1, {}),     # per-variant health events from drs-bridge
    ("drs.roster", 1, 1, {"cleanup.policy": "compact"}),  # active-roster snapshot, key 'active'
]
```

And update the `NewTopic` construction inside `create_topics()`:

```python
        to_create = [
            NewTopic(
                name,
                num_partitions=parts,
                replication_factor=rf,
                topic_configs=configs or None,
            )
            for name, parts, rf, configs in CONTROL_PLANE_TOPICS
            if name not in existing
        ]
```

Also update the count-message and the loop that prints created topics — both currently unpack 3-tuples. Change the summary print to `f"All {len(CONTROL_PLANE_TOPICS)} control-plane topics already exist; nothing to do."` (unchanged) and leave the `for nt in to_create` loop as-is (it iterates `NewTopic` objects, not the tuples).

- [ ] **Step 4: Run it to verify it passes**

Run: `python -m pytest infrastructure/kafka/test_create_topics_config.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add infrastructure/kafka/create-topics.py infrastructure/kafka/test_create_topics_config.py
git commit -m "infra(kafka): provision compacted drs.roster topic (B1.43)"
```

---

## Phase 1 — Roster model (drs-bridge)

### Task 1.1: Add `Roster` / `RosterEntry` Pydantic models

**Files:**
- Modify: `drs-bridge/src/drs_bridge/profiles/_schema.py`
- Test: `drs-bridge/tests/profiles/test_roster_schema.py`

- [ ] **Step 1: Write the failing test**

Create `drs-bridge/tests/profiles/test_roster_schema.py`:

```python
import pytest
from pydantic import ValidationError

from drs_bridge.profiles._schema import Roster, RosterEntry


def _entry(instance_id="rdfs#1", variant="rdfs", port=5001, enabled=True):
    return {
        "instance_id": instance_id,
        "variant": variant,
        "host": "127.0.0.1",
        "command": {"port": port, "protocol": "tcp"},
        "response": {"port": port + 1, "protocol": "udp"},
        "port_source": "irs_fixed",
        "enabled": enabled,
    }


def test_roster_parses_valid_snapshot():
    roster = Roster(roster_id="lab-full", version=7, entries=[_entry()])
    assert roster.roster_id == "lab-full"
    assert roster.version == 7
    assert roster.entries[0].instance_id == "rdfs#1"
    assert roster.entries[0].command.protocol == "tcp"


def test_roster_entry_rejects_bad_protocol():
    with pytest.raises(ValidationError):
        RosterEntry(**{**_entry(), "command": {"port": 5001, "protocol": "sctp"}})


def test_roster_requires_version():
    with pytest.raises(ValidationError):
        Roster(roster_id="x", entries=[])
```

- [ ] **Step 2: Run it to verify it fails**

Run: `python -m pytest tests/profiles/test_roster_schema.py -v`
Expected: FAIL — `ImportError: cannot import name 'Roster'`.

- [ ] **Step 3: Add the models + make `PortConfig.host` and `VariantProfile.ports` optional**

Three edits in `drs-bridge/src/drs_bridge/profiles/_schema.py`. Note: `ports` is made **optional** here (not removed) so the *old* `runtime.py` — which still reads `profile.ports` until Task 3.4 — keeps working and every commit stays green. The actual removal + `rdfs.yaml` cleanup happens in **Task 3.5**, after the old runtime/test are gone.

(a) Give `PortConfig.host` a default — roster endpoints (`command`/`response`) carry only `port`+`protocol`; the entry carries its own `host`:

```python
class PortConfig(BaseModel):
    host: str = "0.0.0.0"
    port: int
    protocol: Literal["tcp", "udp"]
```

(b) Make `VariantProfile.ports` optional (roster-era profiles omit it; old runtime still reads it where present):

```python
class VariantProfile(BaseModel):
    variant: str
    parser_lib: str
    ports: dict[str, PortConfig] = {}
    time_signal: TimeSignalConfig
```

(c) Append the roster models (reusing `PortConfig`):

```python
class RosterEntry(BaseModel):
    instance_id: str
    variant: str
    host: str
    command: PortConfig
    response: PortConfig
    port_source: Literal["irs_fixed", "allocated"]
    enabled: bool


class Roster(BaseModel):
    roster_id: str
    version: int = Field(ge=1)
    entries: list[RosterEntry]

    @property
    def etag(self) -> str:
        return f"{self.roster_id}@{self.version}"
```

- [ ] **Step 4: Run it to verify it passes**

Run: `python -m pytest tests/profiles/ -v`
Expected: PASS — new roster tests green; existing `test_profile_schema.py` / `test_profile_yaml.py` still green (`ports` still accepted where present).

- [ ] **Step 5: Commit**

```bash
git add src/drs_bridge/profiles/_schema.py tests/profiles/test_roster_schema.py
git commit -m "feat(bridge): Roster/RosterEntry models + ports optional on VariantProfile (B1.43)"
```

---

## Phase 2 — Roster Kafka consumer (drs-bridge's first consumer)

### Task 2.1: `RosterConsumer` — decode `drs.roster` snapshots

**Files:**
- Create: `drs-bridge/src/drs_bridge/roster_consumer.py`
- Test: `drs-bridge/tests/test_roster_consumer.py`

- [ ] **Step 1: Write the failing test**

Create `drs-bridge/tests/test_roster_consumer.py`:

```python
import json
from types import SimpleNamespace

import pytest

from drs_bridge.roster_consumer import RosterConsumer
from drs_bridge.profiles._schema import Roster


class _FakeConsumer:
    """Mimics aiokafka's start/stop + async iteration over messages."""
    def __init__(self, messages):
        self._messages = messages
        self.started = False
        self.stopped = False

    async def start(self):
        self.started = True

    async def stop(self):
        self.stopped = True

    async def __aiter__(self):
        for m in self._messages:
            yield m


def _msg(body: dict):
    return SimpleNamespace(value=json.dumps(body).encode("utf-8"))


def _snapshot(version=1):
    return {
        "roster_id": "lab-full",
        "version": version,
        "entries": [],
    }


@pytest.mark.asyncio
async def test_consumer_decodes_and_invokes_callback():
    received: list[Roster] = []
    consumer = RosterConsumer(
        _FakeConsumer([_msg(_snapshot(1)), _msg(_snapshot(2))]),
        on_roster=lambda r: received.append(r),
    )
    await consumer.run()
    assert [r.version for r in received] == [1, 2]
    assert received[-1].etag == "lab-full@2"


@pytest.mark.asyncio
async def test_consumer_skips_malformed_message():
    received = []
    bad = SimpleNamespace(value=b"not json")
    consumer = RosterConsumer(
        _FakeConsumer([bad, _msg(_snapshot(5))]),
        on_roster=lambda r: received.append(r),
    )
    await consumer.run()  # must not raise
    assert [r.version for r in received] == [5]
```

- [ ] **Step 2: Run it to verify it fails**

Run: `python -m pytest tests/test_roster_consumer.py -v`
Expected: FAIL — `ModuleNotFoundError: drs_bridge.roster_consumer`.

- [ ] **Step 3: Write the consumer**

Create `drs-bridge/src/drs_bridge/roster_consumer.py` (mirrors drs-server's `ControlConsumer` shape):

```python
"""Consumes the compacted Kafka `drs.roster` topic and hands each active-roster
snapshot to a callback. drs-bridge's first Kafka consumer.

The topic is compacted with a single key ('active'), so a fresh consumer
reading from earliest always sees the latest retained snapshot first, then
tails live updates. Each message is a FULL snapshot (no deltas).
"""
from __future__ import annotations

import json
import logging
from typing import Awaitable, Callable, Protocol, Union

from drs_bridge.profiles._schema import Roster

logger = logging.getLogger(__name__)

OnRoster = Callable[[Roster], Union[None, Awaitable[None]]]


class KafkaConsumerLike(Protocol):
    async def start(self) -> None: ...
    async def stop(self) -> None: ...
    def __aiter__(self): ...


class RosterConsumer:
    def __init__(self, consumer: KafkaConsumerLike, on_roster: OnRoster) -> None:
        self._consumer = consumer
        self._on_roster = on_roster

    async def start(self) -> None:
        await self._consumer.start()

    async def stop(self) -> None:
        await self._consumer.stop()

    async def run(self) -> None:
        """Consume until cancelled. Malformed/invalid snapshots are logged and
        skipped (forward-compatible; never crash the consumer loop)."""
        async for msg in self._consumer:
            try:
                body = json.loads(msg.value.decode("utf-8"))
                roster = Roster(**body)
            except Exception:
                logger.exception("failed to decode/validate drs.roster message; skipping")
                continue
            result = self._on_roster(roster)
            if result is not None:
                await result
            logger.info("applied roster snapshot %s (%d entries)", roster.etag, len(roster.entries))
```

- [ ] **Step 4: Run it to verify it passes**

Run: `python -m pytest tests/test_roster_consumer.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/drs_bridge/roster_consumer.py tests/test_roster_consumer.py
git commit -m "feat(bridge): RosterConsumer for compacted drs.roster topic (B1.43)"
```

---

## Phase 3 — Per-instance servers + hot-reload (`runtime.py`)

> This phase refactors `Runtime` from **profile-driven, one-server-per-variant** to **roster-driven, one-server-per-instance**. The existing `tests/test_runtime.py` asserts the old behaviour and is **replaced** in Task 3.5.

### Task 3.1: Add `garbage_ceiling` + `probation_idle_timeout` to settings

**Files:**
- Modify: `drs-bridge/src/drs_bridge/config.py`
- Test: `drs-bridge/tests/test_config.py` (add a case)

- [ ] **Step 1: Write the failing test**

Add to `drs-bridge/tests/test_config.py`:

```python
def test_probation_defaults():
    from drs_bridge.config import BridgeSettings
    s = BridgeSettings()
    assert s.probation_garbage_ceiling_bytes == 4096
    assert s.probation_idle_timeout_s == 10.0
```

- [ ] **Step 2: Run it to verify it fails**

Run: `python -m pytest tests/test_config.py::test_probation_defaults -v`
Expected: FAIL — `AttributeError`.

- [ ] **Step 3: Add the settings**

In `drs-bridge/src/drs_bridge/config.py`, add to `BridgeSettings`:

```python
    probation_garbage_ceiling_bytes: int = 4096
    probation_idle_timeout_s: float = 10.0
```

Update the module docstring's env list with the two new vars (`DRS_BRIDGE_PROBATION_GARBAGE_CEILING_BYTES`, `DRS_BRIDGE_PROBATION_IDLE_TIMEOUT_S`).

- [ ] **Step 4: Run it to verify it passes**

Run: `python -m pytest tests/test_config.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/drs_bridge/config.py tests/test_config.py
git commit -m "feat(bridge): probation thresholds in settings (B1.43)"
```

### Task 3.2: `extract_frame` ctypes wiring in `ParserHandle`

**Files:**
- Modify: `drs-bridge/src/drs_bridge/parser_loader.py`
- Test: `drs-bridge/tests/test_parser_loader.py` (add a case using the built reference parser)

> The reference C++ ABI (`drs-bridge/parsers/reference/include/reference_parser.h`) is: `int extract_frame(const uint8_t* buf, size_t length, uint8_t** out_frame, size_t* out_len)` — returns **0** on a complete frame (sets `*out_frame`/`*out_len`), **-1** otherwise (incomplete or no magic). The docstring in `parser_loader.py` currently shows a stale 3-arg signature; fix it to the 4-arg form.

- [ ] **Step 1: Write the failing test**

Add to `drs-bridge/tests/test_parser_loader.py`:

```python
def test_extract_frame_accepts_reference_time_frame(built_reference_parser):
    from drs_bridge.parser_loader import load_parser
    handle = load_parser(built_reference_parser)
    # reference frame: 0xAA magic, len=6, 4-byte LE seconds=1, 2 reserved
    frame = bytes([0xAA, 0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00])
    rc, extracted = handle.extract_frame(frame)
    assert rc == 0
    assert extracted == frame

    rc2, extracted2 = handle.extract_frame(b"\x00\x00\x00")  # no magic
    assert rc2 == -1
    assert extracted2 is None
```

- [ ] **Step 2: Run it to verify it fails**

Run: `python -m pytest tests/test_parser_loader.py::test_extract_frame_accepts_reference_time_frame -v`
Expected: FAIL — `NotImplementedError` (or SKIP if cmake absent; on a dev box with cmake it must FAIL first).

- [ ] **Step 3: Wire `extract_frame`**

In `drs-bridge/src/drs_bridge/parser_loader.py`, bind the symbol in `ParserHandle.__init__` (after the `format_response` binding):

```python
        self._extract_frame = lib.extract_frame
        self._extract_frame.argtypes = [
            ctypes.POINTER(ctypes.c_uint8),                   # buf
            ctypes.c_size_t,                                  # length
            ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),   # out_frame
            ctypes.POINTER(ctypes.c_size_t),                  # out_len
        ]
        self._extract_frame.restype = ctypes.c_int
```

Replace the `extract_frame` body:

```python
    def extract_frame(self, buf: bytes) -> tuple[int, bytes | None]:
        """Scan `buf` for a complete frame. Returns (rc, frame_bytes).

        rc == 0  -> a complete frame was extracted (frame_bytes set).
        rc != 0  -> no complete frame yet (frame_bytes is None). Callers buffer
                    more bytes and retry; the framing-probation logic in
                    transport.py decides when 'no frame yet' becomes a failure.
        """
        if not buf:
            return -1, None
        in_buf = (ctypes.c_uint8 * len(buf)).from_buffer_copy(buf)
        out_frame = ctypes.POINTER(ctypes.c_uint8)()
        out_len = ctypes.c_size_t(0)
        rc = self._extract_frame(
            ctypes.cast(in_buf, ctypes.POINTER(ctypes.c_uint8)),
            ctypes.c_size_t(len(buf)),
            ctypes.byref(out_frame),
            ctypes.byref(out_len),
        )
        if rc != 0:
            return rc, None
        try:
            frame = bytes(
                ctypes.cast(out_frame, ctypes.POINTER(ctypes.c_uint8 * out_len.value)).contents
            )
        finally:
            self._free_result(ctypes.cast(out_frame, ctypes.c_void_p))
        return 0, frame
```

Also fix the stale ABI comment at the top of the file to the 4-arg `extract_frame` signature.

- [ ] **Step 4: Run it to verify it passes**

Run: `python -m pytest tests/test_parser_loader.py -v`
Expected: PASS (or SKIP if cmake absent — acceptable on toolchain-less boxes).

- [ ] **Step 5: Commit**

```bash
git add src/drs_bridge/parser_loader.py tests/test_parser_loader.py
git commit -m "feat(bridge): wire extract_frame through ctypes (B1.43)"
```

### Task 3.3: Framing-probation connection handler in `transport.py`

**Files:**
- Modify: `drs-bridge/src/drs_bridge/transport.py`
- Test: `drs-bridge/tests/test_transport.py` (add probation cases)

> The probation logic is generic over a *frame detector* `Callable[[bytes], tuple[int, bytes | None]]` so unit tests inject a pure-Python fake (no DLL). In production the detector is `ParserHandle.extract_frame`.

- [ ] **Step 1: Write the failing test**

Add to `drs-bridge/tests/test_transport.py`:

```python
import asyncio
import pytest

from drs_bridge.transport import probation_connection


def _ref_detector(buf: bytes):
    # mimic the reference parser: 0xAA magic, len byte, then payload
    for i in range(len(buf) - 1):
        if buf[i] == 0xAA:
            need = 2 + buf[i + 1]
            if i + need <= len(buf):
                return 0, bytes(buf[i:i + need])
            return -1, None
    return -1, None


class _FakeReader:
    def __init__(self, chunks):
        self._chunks = list(chunks)

    async def read(self, n):
        if self._chunks:
            return self._chunks.pop(0)
        return b""  # EOF


class _FakeWriter:
    def __init__(self):
        self.closed = False

    def close(self):
        self.closed = True

    async def wait_closed(self):
        return None


@pytest.mark.asyncio
async def test_probation_accepts_segmented_frame():
    frames = []
    reader = _FakeReader([b"\xAA\x06\x01\x00", b"\x00\x00\x00\x00"])  # split valid frame
    writer = _FakeWriter()
    await probation_connection(
        reader, writer, instance_id="rdfs#1", detector=_ref_detector,
        on_frame=lambda iid, f: frames.append((iid, f)),
        garbage_ceiling=4096, idle_timeout=1.0,
    )
    assert frames and frames[0][0] == "rdfs#1"
    assert frames[0][1] == bytes([0xAA, 0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00])


@pytest.mark.asyncio
async def test_probation_closes_on_garbage_ceiling():
    on_close = []
    reader = _FakeReader([b"\x00" * 5000])  # never frames, exceeds ceiling
    writer = _FakeWriter()
    await probation_connection(
        reader, writer, instance_id="rdfs#1", detector=_ref_detector,
        on_frame=lambda iid, f: None,
        garbage_ceiling=4096, idle_timeout=1.0,
        on_reject=lambda iid, reason: on_close.append(reason),
    )
    assert writer.closed
    assert on_close == ["FRAMING_MISMATCH_CEILING"]


@pytest.mark.asyncio
async def test_probation_closes_on_idle_timeout():
    on_close = []

    class _SlowReader:
        async def read(self, n):
            await asyncio.sleep(10)  # never returns within idle_timeout
            return b""

    writer = _FakeWriter()
    await probation_connection(
        _SlowReader(), writer, instance_id="rdfs#1", detector=_ref_detector,
        on_frame=lambda iid, f: None,
        garbage_ceiling=4096, idle_timeout=0.05,
        on_reject=lambda iid, reason: on_close.append(reason),
    )
    assert writer.closed
    assert on_close == ["FRAMING_MISMATCH_IDLE"]
```

- [ ] **Step 2: Run it to verify it fails**

Run: `python -m pytest tests/test_transport.py -k probation -v`
Expected: FAIL — `ImportError: cannot import name 'probation_connection'`.

- [ ] **Step 3: Implement `probation_connection` + rework the server**

In `drs-bridge/src/drs_bridge/transport.py`, add the imports and the probation handler, and rewrite `start_command_server` to use it:

```python
from typing import Awaitable, Callable, Optional

# (existing) CommandHandler kept for back-compat callers; new types below:
FrameDetector = Callable[[bytes], "tuple[int, bytes | None]"]
FrameHandler = Callable[[str, bytes], Optional[Awaitable[None]]]
RejectHandler = Callable[[str, str], None]


async def probation_connection(
    reader,
    writer,
    *,
    instance_id: str,
    detector: FrameDetector,
    on_frame: FrameHandler,
    garbage_ceiling: int,
    idle_timeout: float,
    on_reject: RejectHandler | None = None,
) -> None:
    """Handle one inbound connection under framing probation.

    The connection is PROVISIONAL until `detector` extracts the first valid
    frame. Until then: buffer bytes, retrying the detector each read. If the
    buffer exceeds `garbage_ceiling` with no frame, or no bytes arrive within
    `idle_timeout`, close and report FRAMING_MISMATCH_*. Once a frame is
    extracted, probation clears; subsequent frames flow to `on_frame` and the
    idle timeout no longer applies (a quiet but valid peer is fine).
    """
    import asyncio

    buffer = bytearray()
    cleared = False
    try:
        while True:
            if not cleared:
                try:
                    data = await asyncio.wait_for(reader.read(4096), timeout=idle_timeout)
                except asyncio.TimeoutError:
                    if on_reject:
                        on_reject(instance_id, "FRAMING_MISMATCH_IDLE")
                    return
            else:
                data = await reader.read(4096)
            if not data:
                return  # EOF
            buffer += data
            # Drain as many complete frames as the buffer holds.
            while True:
                rc, frame = detector(bytes(buffer))
                if rc != 0 or frame is None:
                    break
                cleared = True
                del buffer[: len(frame)]
                result = on_frame(instance_id, frame)
                if result is not None:
                    await result
            if not cleared and len(buffer) > garbage_ceiling:
                if on_reject:
                    on_reject(instance_id, "FRAMING_MISMATCH_CEILING")
                return
    finally:
        writer.close()
        try:
            await writer.wait_closed()
        except Exception:
            pass
```

> **Note on frame-boundary draining:** `del buffer[:len(frame)]` assumes the extracted frame sits at the front of the buffer. The reference parser discards preamble before the magic byte, so a frame may not start at offset 0. For Plan 1's purposes (probation = "did we ever see one valid frame") this is acceptable: the first cleared frame is what matters, and the data-plane re-parse is out of scope. A precise resync offset is a data-plane concern (deferred). Leave a `# TODO(data-plane): exact resync offset from extract_frame` comment here so the next phase addresses it. *(This is the one deliberate simplification in the connection loop — see "Out of scope".)*

Now rewrite `start_command_server` to bind probation per connection:

```python
async def start_command_server(
    host: str,
    port: int,
    *,
    instance_id: str,
    detector: FrameDetector,
    on_frame: FrameHandler,
    garbage_ceiling: int,
    idle_timeout: float,
    on_connect: Callable[[str], None] | None = None,
    on_reject: RejectHandler | None = None,
) -> "asyncio.Server":
    """Start a TCP server whose connections run under framing probation."""
    import asyncio

    async def _handle(reader, writer):
        if on_connect:
            on_connect(instance_id)
        await probation_connection(
            reader, writer,
            instance_id=instance_id, detector=detector, on_frame=on_frame,
            garbage_ceiling=garbage_ceiling, idle_timeout=idle_timeout,
            on_reject=on_reject,
        )

    return await asyncio.start_server(_handle, host, port)
```

- [ ] **Step 4: Replace the obsolete old-signature server test**

`tests/test_transport.py::test_command_server_delivers_received_bytes_to_handler` calls the old positional `start_command_server("127.0.0.1", 0, handler)` and will no longer compile against the new keyword-only signature. **Delete that test** and replace it with one exercising the new signature end-to-end on a real loopback socket (the `_ref_detector` helper added in Step 1 is in scope):

```python
@pytest.mark.asyncio
async def test_command_server_routes_first_frame_with_instance_id():
    frames = []
    server = await start_command_server(
        "127.0.0.1", 0,
        instance_id="rdfs#1", detector=_ref_detector,
        on_frame=lambda iid, f: frames.append((iid, f)),
        garbage_ceiling=4096, idle_timeout=1.0,
    )
    port = server.sockets[0].getsockname()[1]
    try:
        reader, writer = await asyncio.open_connection("127.0.0.1", port)
        writer.write(bytes([0xAA, 0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00]))
        await writer.drain()
        await asyncio.sleep(0.05)
        writer.close()
        assert frames == [("rdfs#1", bytes([0xAA, 0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00]))]
    finally:
        server.close()
        await server.wait_closed()
```

`test_udp_sender_sends_payload` is untouched (the `UdpSender` is unchanged).

- [ ] **Step 5: Run it to verify it passes**

Run: `python -m pytest tests/test_transport.py -v`
Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add src/drs_bridge/transport.py tests/test_transport.py
git commit -m "feat(bridge): framing-probation connection handler (B1.43)"
```

### Task 3.4: Refactor `Runtime` to roster-driven per-instance binding

**Files:**
- Modify: `drs-bridge/src/drs_bridge/runtime.py`
- Test: `drs-bridge/tests/test_runtime.py` (replaced — see Task 3.5)

- [ ] **Step 1: Write the failing test**

Create `drs-bridge/tests/test_runtime_roster.py`:

```python
from pathlib import Path
from unittest.mock import AsyncMock, MagicMock

import pytest

from drs_bridge.runtime import Runtime
from drs_bridge.profiles._schema import Roster


def _write_profile(profiles_dir: Path, variant: str) -> None:
    (profiles_dir / f"{variant}.yaml").write_text(
        f"""variant: {variant}
parser_lib: parsers/{variant}/parser.dll
time_signal:
  embedded_in_messages: true
  periodic_distribution:
    enabled: false
    interval_ms: null
  precision_required_ms: 10
""",
        encoding="utf-8",
    )


def _roster(*instances) -> Roster:
    entries = [
        {
            "instance_id": iid, "variant": variant, "host": "127.0.0.1",
            "command": {"port": 0, "protocol": "tcp"},
            "response": {"port": 0, "protocol": "udp"},
            "port_source": "irs_fixed", "enabled": enabled,
        }
        for (iid, variant, enabled) in instances
    ]
    return Roster(roster_id="t", version=1, entries=entries)


def _runtime(profiles_dir, servers):
    """Runtime with all I/O faked; command_server_factory records bind calls."""
    def server_factory(host, port, **kw):
        srv = AsyncMock()
        servers.append((kw["instance_id"], srv))
        return srv
    return Runtime(
        profiles_dir=profiles_dir,
        kafka_bootstrap="dummy:9092",
        kafka_producer_factory=lambda b: AsyncMock(),
        parser_factory=lambda p: MagicMock(extract_frame=lambda buf: (-1, None)),
        sender_factory=lambda host, port: AsyncMock(),
        command_server_factory=server_factory,
        roster_consumer_factory=lambda bootstrap, on_roster: AsyncMock(),
    )


@pytest.mark.asyncio
async def test_apply_roster_binds_one_server_per_enabled_instance(tmp_path):
    _write_profile(tmp_path, "rdfs")
    servers = []
    rt = _runtime(tmp_path, servers)
    await rt.start()
    await rt.apply_roster(_roster(("rdfs#1", "rdfs", True), ("rdfs#2", "rdfs", True),
                                  ("rdfs#9", "rdfs", False)))
    bound = sorted(iid for iid, _ in servers)
    assert bound == ["rdfs#1", "rdfs#2"]  # disabled rdfs#9 not bound
    await rt.shutdown()


@pytest.mark.asyncio
async def test_parser_loaded_once_per_variant(tmp_path):
    _write_profile(tmp_path, "rdfs")
    calls = []
    rt = Runtime(
        profiles_dir=tmp_path, kafka_bootstrap="d:9092",
        kafka_producer_factory=lambda b: AsyncMock(),
        parser_factory=lambda p: calls.append(p) or MagicMock(extract_frame=lambda buf: (-1, None)),
        sender_factory=lambda host, port: AsyncMock(),
        command_server_factory=lambda host, port, **kw: AsyncMock(),
        roster_consumer_factory=lambda bootstrap, on_roster: AsyncMock(),
    )
    await rt.start()
    await rt.apply_roster(_roster(("rdfs#1", "rdfs", True), ("rdfs#2", "rdfs", True)))
    assert len(calls) == 1  # one .dll load for two rdfs instances
    await rt.shutdown()
```

- [ ] **Step 2: Run it to verify it fails**

Run: `python -m pytest tests/test_runtime_roster.py -v`
Expected: FAIL — `Runtime.__init__() got an unexpected keyword argument 'roster_consumer_factory'` / no `apply_roster`.

- [ ] **Step 3: Rewrite `runtime.py`**

Replace `drs-bridge/src/drs_bridge/runtime.py` with the roster-driven version:

```python
"""Composes config + profile templates + parsers + transport + Kafka into a
runnable service. Roster-driven: the active roster (received over Kafka
`drs.roster`) decides which per-instance servers are bound. Hot-reloads on
each new snapshot.
"""
from __future__ import annotations

import asyncio
import logging
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

from drs_bridge.bridge import Bridge
from drs_bridge.control_publisher import ControlPublisher
from drs_bridge.health_publisher import HealthPublisher
from drs_bridge.parser_loader import load_parser
from drs_bridge.profile_loader import load_profiles
from drs_bridge.profiles._schema import Roster, RosterEntry
from drs_bridge.roster_consumer import RosterConsumer
from drs_bridge.transport import UdpSender, start_command_server

logger = logging.getLogger(__name__)


def _default_kafka_producer_factory(bootstrap: str):
    from aiokafka import AIOKafkaProducer
    return AIOKafkaProducer(bootstrap_servers=bootstrap)


def _default_roster_consumer_factory(bootstrap: str, on_roster):
    from aiokafka import AIOKafkaConsumer
    # group_id=None + earliest => always read the retained compacted snapshot,
    # then tail live updates. Single-partition compacted topic.
    consumer = AIOKafkaConsumer(
        "drs.roster",
        bootstrap_servers=bootstrap,
        auto_offset_reset="earliest",
        group_id=None,
        enable_auto_commit=False,
    )
    return RosterConsumer(consumer, on_roster=on_roster)


async def _default_sender_factory(host: str, port: int):
    return await UdpSender.connect(host, port)


@dataclass
class _BoundInstance:
    entry: RosterEntry
    server: object
    sender: object


class Runtime:
    def __init__(
        self,
        profiles_dir: Path,
        kafka_bootstrap: str,
        kafka_producer_factory: Callable | None = None,
        parser_factory: Callable | None = None,
        sender_factory: Callable | None = None,
        command_server_factory: Callable | None = None,
        roster_consumer_factory: Callable | None = None,
        garbage_ceiling: int = 4096,
        idle_timeout: float = 10.0,
    ) -> None:
        self._profiles_dir = profiles_dir
        self._kafka_bootstrap = kafka_bootstrap
        self._kafka_producer_factory = kafka_producer_factory or _default_kafka_producer_factory
        self._parser_factory = parser_factory or load_parser
        self._sender_factory = sender_factory or _default_sender_factory
        self._command_server_factory = command_server_factory or start_command_server
        self._roster_consumer_factory = roster_consumer_factory or _default_roster_consumer_factory
        self._garbage_ceiling = garbage_ceiling
        self._idle_timeout = idle_timeout

        self._producer = None
        self._health = None
        self._control = None
        self._profiles = {}
        self._parsers: dict[str, object] = {}        # variant -> parser (loaded once)
        self._registered_variants: set[str] = set()  # control-plane dedupe
        self._bound: dict[str, _BoundInstance] = {}   # instance_id -> bound
        self.bridge: Bridge | None = None
        self._roster_consumer = None
        self._consumer_task: asyncio.Task | None = None
        self._lock = asyncio.Lock()
        self._stop_event = asyncio.Event()

    async def start(self) -> None:
        logger.info("Runtime starting; profiles_dir=%s kafka=%s", self._profiles_dir, self._kafka_bootstrap)
        self._producer = self._kafka_producer_factory(self._kafka_bootstrap)
        await self._producer.start()
        control = ControlPublisher(producer=self._producer)
        self._health = HealthPublisher(producer=self._producer)
        self.bridge = Bridge(control_publisher=control, health_publisher=self._health)
        self._control = control
        self._profiles = load_profiles(self._profiles_dir)
        self._roster_consumer = self._roster_consumer_factory(self._kafka_bootstrap, self._on_roster)
        await self._roster_consumer.start()
        self._consumer_task = asyncio.create_task(self._roster_consumer.run(), name="roster-consumer")

    def _on_roster(self, roster: Roster):
        return self.apply_roster(roster)

    def _parser_for(self, variant: str):
        if variant not in self._parsers:
            profile = self._profiles[variant]
            self._parsers[variant] = self._parser_factory(self._profiles_dir / profile.parser_lib)
        return self._parsers[variant]

    async def apply_roster(self, roster: Roster) -> None:
        """Diff the desired (enabled) instances against bound ones; bind added,
        tear down removed/disabled, rebind addressing changes. Defensive:
        an entry whose variant has no profile, or that fails to bind, is
        isolated (logged + FAILED_BIND health) — siblings are unaffected.
        """
        async with self._lock:
            desired = {e.instance_id: e for e in roster.entries if e.enabled}
            # Remove instances no longer desired (gone or disabled).
            for iid in list(self._bound):
                if iid not in desired or self._addr(self._bound[iid].entry) != self._addr(desired[iid]):
                    await self._teardown(iid)
            # Bind new/changed.
            for iid, entry in desired.items():
                if iid in self._bound:
                    continue
                await self._bind(entry)
            logger.info("roster %s applied; bound=%s", roster.etag, sorted(self._bound))

    @staticmethod
    def _addr(e: RosterEntry):
        return (e.host, e.command.port, e.command.protocol, e.response.port, e.response.protocol)

    async def _ensure_variant_registered(self, variant: str) -> None:
        """Announce the variant to the drs-server control plane exactly once,
        so SyncStateEngine learns its precision threshold. Per-instance time
        beacons are NOT started here (see plan 'Out of scope')."""
        if variant in self._registered_variants:
            return
        profile = self._profiles[variant]
        await self._control.publish_variant_registration(
            variant=variant,
            precision_required_ms=profile.time_signal.precision_required_ms,
        )
        self._registered_variants.add(variant)

    async def _make_sender(self, host, port):
        result = self._sender_factory(host, port)
        return await result if asyncio.iscoroutine(result) else result

    async def _bind(self, entry: RosterEntry) -> None:
        if entry.variant not in self._profiles:
            logger.error("instance %s names unknown variant %s; isolating", entry.instance_id, entry.variant)
            await self._health.publish("instance.failed_bind",
                                       {"instance_id": entry.instance_id, "reason": "UNKNOWN_VARIANT"})
            return
        if entry.command.protocol != "tcp":
            logger.error("instance %s uses %s command transport; only tcp supported in Plan 1; isolating",
                         entry.instance_id, entry.command.protocol)
            await self._health.publish("instance.failed_bind",
                                       {"instance_id": entry.instance_id, "reason": "UDP_COMMAND_UNSUPPORTED"})
            return
        try:
            await self._ensure_variant_registered(entry.variant)
            parser = self._parser_for(entry.variant)
            sender = await self._make_sender(entry.host, entry.response.port)

            def _on_frame(instance_id: str, frame: bytes) -> None:
                logger.debug("instance=%s frame=%dB", instance_id, len(frame))

            def _on_connect(instance_id: str) -> None:
                asyncio.create_task(self._health.publish("instance.connected", {"instance_id": instance_id}))

            def _on_reject(instance_id: str, reason: str) -> None:
                logger.warning("instance=%s rejected: %s", instance_id, reason)
                asyncio.create_task(self._health.publish("instance.framing_mismatch",
                                                         {"instance_id": instance_id, "reason": reason}))

            server = self._command_server_factory(
                entry.host, entry.command.port,
                instance_id=entry.instance_id, detector=parser.extract_frame,
                on_frame=_on_frame, garbage_ceiling=self._garbage_ceiling,
                idle_timeout=self._idle_timeout, on_connect=_on_connect, on_reject=_on_reject,
            )
            server = await server if asyncio.iscoroutine(server) else server
            self._bound[entry.instance_id] = _BoundInstance(entry=entry, server=server, sender=sender)
            logger.info("bound instance=%s variant=%s %s:%d", entry.instance_id, entry.variant,
                        entry.host, entry.command.port)
        except Exception:
            logger.exception("failed to bind instance %s; isolating", entry.instance_id)
            await self._health.publish("instance.failed_bind",
                                       {"instance_id": entry.instance_id, "reason": "BIND_ERROR"})

    async def _teardown(self, instance_id: str) -> None:
        bound = self._bound.pop(instance_id, None)
        if bound is None:
            return
        try:
            bound.server.close()
            await bound.server.wait_closed()
        except Exception:
            logger.exception("server close failed for %s", instance_id)
        try:
            await bound.sender.close()
        except Exception:
            logger.exception("sender close failed for %s", instance_id)
        logger.info("tore down instance=%s", instance_id)

    async def run_until_stopped(self) -> None:
        await self._stop_event.wait()

    def request_stop(self) -> None:
        self._stop_event.set()

    async def shutdown(self) -> None:
        logger.info("Runtime shutting down")
        if self._consumer_task is not None:
            self._consumer_task.cancel()
            try:
                await self._consumer_task
            except asyncio.CancelledError:
                pass
        if self._roster_consumer is not None:
            try:
                await self._roster_consumer.stop()
            except Exception:
                logger.exception("roster consumer stop failed")
        for iid in list(self._bound):
            await self._teardown(iid)
        if self.bridge is not None:
            await self.bridge.shutdown()
        if self._producer is not None:
            await self._producer.stop()
```

- [ ] **Step 4: Run it to verify it passes**

Run: `python -m pytest tests/test_runtime_roster.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/drs_bridge/runtime.py tests/test_runtime_roster.py
git commit -m "feat(bridge): roster-driven per-instance binding in Runtime (B1.43)"
```

### Task 3.5: Remove the superseded profile-driven runtime test

**Files:**
- Delete: `drs-bridge/tests/test_runtime.py`

- [ ] **Step 1: Confirm what it tested**

`tests/test_runtime.py` asserts the old profile-driven `start()` (one server per profile, `_variant_tasks` populated from the profiles dir). That behaviour is replaced by roster-driven binding (Task 3.4), so the file is obsolete; `test_runtime_roster.py` is its successor.

- [ ] **Step 2: Delete it**

```bash
git rm tests/test_runtime.py
```

- [ ] **Step 3: Now finish the `ports` removal (deferred from Task 1.1)**

With the old `runtime.py` replaced (Task 3.4) and old `test_runtime.py` gone, nothing reads `profile.ports` anymore. Complete the spec §5.2 change:

(a) In `drs-bridge/src/drs_bridge/profiles/_schema.py`, remove the `ports` field from `VariantProfile`:

```python
class VariantProfile(BaseModel):
    variant: str
    parser_lib: str
    time_signal: TimeSignalConfig
```

(b) In `drs-bridge/src/drs_bridge/profiles/rdfs.yaml`, delete the `ports:` block (the `command`/`response` mapping); leave `variant`, `parser_lib`, `time_signal`.

(c) In `drs-bridge/tests/profiles/test_profile_schema.py::test_minimal_profile_valid`, drop the `ports={...}` kwarg from the `VariantProfile(...)` construction:

```python
def test_minimal_profile_valid():
    profile = VariantProfile(
        variant="rdfs",
        parser_lib="parsers/rdfs/parser.dll",
        time_signal=TimeSignalConfig(
            embedded_in_messages=True,
            periodic_distribution={"enabled": False, "interval_ms": None},
            precision_required_ms=10.0,
        ),
    )
    assert profile.variant == "rdfs"
    assert profile.time_signal.embedded_in_messages is True
```

- [ ] **Step 4: Run the suite**

```bash
python -m pytest tests/ -v --ignore=tests/test_reference_parser_integration.py --ignore=tests/test_roster_integration.py
```
Expected: PASS — `test_runtime_roster.py` covers Runtime; profile tests reflect the ports-free template; no `profile.ports` references remain (`grep -rn "\.ports" src/ tests/` returns nothing).

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "refactor(bridge): drop profile-driven runtime test + remove ports from VariantProfile (B1.43)"
```

### Task 3.6: Hot-reload diff test (phase breadth)

**Files:**
- Test: `drs-bridge/tests/test_runtime_roster.py` (add cases)

- [ ] **Step 1: Write the failing tests**

Add to `drs-bridge/tests/test_runtime_roster.py`:

```python
@pytest.mark.asyncio
async def test_hot_reload_adds_removes_and_keeps_unaffected(tmp_path):
    _write_profile(tmp_path, "rdfs")
    servers = []
    rt = _runtime(tmp_path, servers)
    await rt.start()
    await rt.apply_roster(_roster(("rdfs#1", "rdfs", True), ("rdfs#2", "rdfs", True)))
    first_for_1 = rt._bound["rdfs#1"].server

    # v2: drop #2, add #3, leave #1 untouched.
    await rt.apply_roster(_roster(("rdfs#1", "rdfs", True), ("rdfs#3", "rdfs", True)))
    assert set(rt._bound) == {"rdfs#1", "rdfs#3"}
    assert rt._bound["rdfs#1"].server is first_for_1  # unaffected: same server object
    await rt.shutdown()


@pytest.mark.asyncio
async def test_hot_reload_rebinds_on_address_change(tmp_path):
    _write_profile(tmp_path, "rdfs")
    servers = []
    rt = _runtime(tmp_path, servers)
    await rt.start()
    await rt.apply_roster(_roster(("rdfs#1", "rdfs", True)))
    original = rt._bound["rdfs#1"].server

    # Same id, different command port -> rebind.
    changed = Roster(roster_id="t", version=2, entries=[{
        "instance_id": "rdfs#1", "variant": "rdfs", "host": "127.0.0.1",
        "command": {"port": 12345, "protocol": "tcp"},
        "response": {"port": 0, "protocol": "udp"},
        "port_source": "irs_fixed", "enabled": True,
    }])
    await rt.apply_roster(changed)
    assert rt._bound["rdfs#1"].server is not original  # rebound
    await rt.shutdown()


@pytest.mark.asyncio
async def test_unknown_variant_isolated(tmp_path):
    _write_profile(tmp_path, "rdfs")
    servers = []
    rt = _runtime(tmp_path, servers)
    await rt.start()
    await rt.apply_roster(_roster(("rdfs#1", "rdfs", True), ("ghost#1", "ghost", True)))
    assert set(rt._bound) == {"rdfs#1"}  # ghost isolated, rdfs bound
    await rt.shutdown()
```

- [ ] **Step 2: Run them**

Run: `python -m pytest tests/test_runtime_roster.py -v`
Expected: PASS (the Task 3.4 implementation already supports these; if the address-change rebind fails, fix `_addr`/diff in `apply_roster`).

- [ ] **Step 3: Commit**

```bash
git add tests/test_runtime_roster.py
git commit -m "test(bridge): hot-reload diff breadth (add/remove/rebind/isolate) (B1.43)"
```

### Task 3.7: Update `main.py` wiring + leaked-task guard

**Files:**
- Modify: `drs-bridge/src/drs_bridge/main.py`
- Test: `drs-bridge/tests/test_runtime_roster.py` (add a teardown-cleanliness case)

- [ ] **Step 1: Write the failing test**

Add to `drs-bridge/tests/test_runtime_roster.py`:

```python
import asyncio

@pytest.mark.asyncio
async def test_shutdown_leaves_no_leaked_tasks(tmp_path):
    _write_profile(tmp_path, "rdfs")
    servers = []
    rt = _runtime(tmp_path, servers)
    before = {t for t in asyncio.all_tasks()}
    await rt.start()
    await rt.apply_roster(_roster(("rdfs#1", "rdfs", True)))
    await rt.shutdown()
    await asyncio.sleep(0)  # let cancellations settle
    after = {t for t in asyncio.all_tasks() if not t.done()}
    leaked = after - before - {asyncio.current_task()}
    assert not leaked, f"leaked tasks: {leaked}"
```

- [ ] **Step 2: Run it to verify it fails (or passes)**

Run: `python -m pytest tests/test_runtime_roster.py::test_shutdown_leaves_no_leaked_tasks -v`
Expected: PASS if Task 3.4's `shutdown()` cancels `_consumer_task` correctly; if it FAILs (consumer task leaked), fix `shutdown()` to await the cancelled task.

- [ ] **Step 3: Confirm `main.py` passes probation settings**

In `drs-bridge/src/drs_bridge/main.py`, update `run()` to thread the probation settings from `BridgeSettings` into `Runtime`:

```python
async def run(profiles_dir: Path, kafka_bootstrap: str,
              garbage_ceiling: int = 4096, idle_timeout: float = 10.0) -> None:
    runtime = Runtime(profiles_dir=profiles_dir, kafka_bootstrap=kafka_bootstrap,
                      garbage_ceiling=garbage_ceiling, idle_timeout=idle_timeout)
    loop = asyncio.get_running_loop()
    _install_signal_handlers(loop, runtime)
    await runtime.start()
    try:
        await runtime.run_until_stopped()
    finally:
        await runtime.shutdown()
```

And in `main()`:

```python
    asyncio.run(run(
        profiles_dir=settings.profiles_dir, kafka_bootstrap=settings.kafka_bootstrap,
        garbage_ceiling=settings.probation_garbage_ceiling_bytes,
        idle_timeout=settings.probation_idle_timeout_s,
    ))
```

- [ ] **Step 4: Run the full unit suite**

Run: `python -m pytest tests/ -v --ignore=tests/test_reference_parser_integration.py`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/drs_bridge/main.py tests/test_runtime_roster.py
git commit -m "feat(bridge): wire probation settings + leaked-task guard (B1.43)"
```

---

## Phase 4 — Health event schema alignment

### Task 4.1: Document the new `system.health` event types in the contract

**Files:**
- Modify: `contracts/kafka/system.health.schema.json`
- Test: `drs-bridge/tests/test_health_events.py`

> drs-bridge now emits three new health events (`instance.connected`, `instance.framing_mismatch`, `instance.failed_bind`). `system.health` is an open envelope (payload deliberately open per the contract). Add a unit test asserting the bridge emits the expected shapes so the Plan-2 consumer can rely on them; pin the field names in the schema description.

- [ ] **Step 1: Write the failing test**

Create `drs-bridge/tests/test_health_events.py`:

```python
import json
import pytest

from drs_bridge.health_publisher import HealthPublisher


class _CapturingProducer:
    def __init__(self):
        self.sent = []

    async def send_and_wait(self, topic, value):
        self.sent.append((topic, json.loads(value.decode("utf-8"))))


@pytest.mark.asyncio
async def test_instance_health_events_shape():
    prod = _CapturingProducer()
    hp = HealthPublisher(prod)
    await hp.publish("instance.connected", {"instance_id": "rdfs#1"})
    await hp.publish("instance.framing_mismatch", {"instance_id": "rdfs#1", "reason": "FRAMING_MISMATCH_CEILING"})
    await hp.publish("instance.failed_bind", {"instance_id": "rdfs#9", "reason": "BIND_ERROR"})
    topics = {t for t, _ in prod.sent}
    assert topics == {"system.health"}
    events = [b["event"] for _, b in prod.sent]
    assert events == ["instance.connected", "instance.framing_mismatch", "instance.failed_bind"]
    assert all("instance_id" in b for _, b in prod.sent)
```

- [ ] **Step 2: Run it to verify it passes**

Run: `python -m pytest tests/test_health_events.py -v`
Expected: PASS (HealthPublisher already supports arbitrary event+payload; this pins the contract drs-server will consume).

- [ ] **Step 3: Pin the event names in the schema description**

In `contracts/kafka/system.health.schema.json`, extend the top-level `description` to enumerate the drs-bridge instance events: `instance.connected {instance_id}`, `instance.framing_mismatch {instance_id, reason}`, `instance.failed_bind {instance_id, reason}` — these feed the Plan-2 `/exercise/readiness` connection-presence (`reachable`) field. (Add to the description text; do not constrain the open payload.)

- [ ] **Step 4: Commit**

```bash
git add contracts/kafka/system.health.schema.json tests/test_health_events.py
git commit -m "feat(bridge): per-instance health events + contract note (B1.43)"
```

---

## Phase 5 — Integration test (graceful-skip)

### Task 5.1: End-to-end — published roster → per-instance servers → frame routing + probation + hot-reload

**Files:**
- Test: `drs-bridge/tests/test_roster_integration.py`

> Graceful-skips when CMake is unavailable (reuses the `built_reference_parser` session fixture in [`tests/conftest.py`](../../drs-bridge/tests/conftest.py)). This test drives `Runtime.apply_roster` directly with a real parser and real loopback TCP sockets — it does **not** require a Kafka broker (the consumer is exercised in Task 2.1; here we test the binding + probation + hot-reload path with the real DLL). Mark `[integration]`.

- [ ] **Step 1: Write the test**

Create `drs-bridge/tests/test_roster_integration.py`:

```python
import asyncio
from pathlib import Path
from unittest.mock import AsyncMock

import pytest

from drs_bridge.runtime import Runtime
from drs_bridge.parser_loader import load_parser
from drs_bridge.profiles._schema import Roster


def _write_profile(profiles_dir: Path, variant: str, dll: Path) -> None:
    (profiles_dir / f"{variant}.yaml").write_text(
        f"""variant: {variant}
parser_lib: {dll.as_posix()}
time_signal:
  embedded_in_messages: true
  periodic_distribution:
    enabled: false
    interval_ms: null
  precision_required_ms: 10
""",
        encoding="utf-8",
    )


def _entry(iid, port, enabled=True):
    return {
        "instance_id": iid, "variant": "ref", "host": "127.0.0.1",
        "command": {"port": port, "protocol": "tcp"},
        "response": {"port": 0, "protocol": "udp"},
        "port_source": "irs_fixed", "enabled": enabled,
    }


def _free_port() -> int:
    import socket
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


@pytest.mark.asyncio
async def test_end_to_end_binding_probation_and_hot_reload(tmp_path, built_reference_parser):
    # profile points parser_lib at the freshly-built reference DLL (absolute path)
    _write_profile(tmp_path, "ref", built_reference_parser)
    frames: list[tuple[str, bytes]] = []

    rt = Runtime(
        profiles_dir=tmp_path, kafka_bootstrap="unused:9092",
        kafka_producer_factory=lambda b: AsyncMock(),
        parser_factory=load_parser,                       # REAL parser
        sender_factory=lambda host, port: AsyncMock(),
        roster_consumer_factory=lambda bootstrap, on_roster: AsyncMock(),
        idle_timeout=1.0, garbage_ceiling=4096,
    )
    # capture frames by patching the on_frame via a subclass hook:
    rt._test_frames = frames
    await rt.start()

    p1, p2 = _free_port(), _free_port()
    await rt.apply_roster(Roster(roster_id="lab", version=1, entries=[_entry("ref#1", p1), _entry("ref#2", p2)]))
    assert set(rt._bound) == {"ref#1", "ref#2"}

    # Connect to ref#1 and send a valid reference 'time' frame; probation clears.
    reader, writer = await asyncio.open_connection("127.0.0.1", p1)
    frame = bytes([0xAA, 0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00])
    writer.write(frame)
    await writer.drain()
    await asyncio.sleep(0.2)
    writer.close()

    # Connect to ref#2 and send garbage; probation closes it (server stays up).
    r2, w2 = await asyncio.open_connection("127.0.0.1", p2)
    w2.write(b"\x00" * 5000)
    await w2.drain()
    await asyncio.sleep(0.2)
    w2.close()
    assert "ref#2" in rt._bound  # server itself still bound after rejecting one connection

    # Hot-reload: re-address ref#1 to a new port; only ref#1 rebinds.
    p1b = _free_port()
    original_1 = rt._bound["ref#1"].server
    original_2 = rt._bound["ref#2"].server
    await rt.apply_roster(Roster(roster_id="lab", version=2,
                                 entries=[_entry("ref#1", p1b), _entry("ref#2", p2)]))
    assert rt._bound["ref#1"].server is not original_1   # re-addressed -> rebound
    assert rt._bound["ref#2"].server is original_2        # unchanged -> same server object

    await rt.shutdown()
```

> **Wiring note for the worker:** to assert frame routing, expose the captured frames. Simplest: in `Runtime._bind`, change the inner `_on_frame` to also append to an optional `self._test_frames` list when present:
> ```python
> def _on_frame(instance_id: str, frame: bytes) -> None:
>     logger.debug("instance=%s frame=%dB", instance_id, len(frame))
>     hook = getattr(self, "_test_frames", None)
>     if hook is not None:
>         hook.append((instance_id, frame))
> ```
> Then add `assert ("ref#1", frame) in frames` after the first `sleep`. Keep the hook tiny and clearly test-only.

- [ ] **Step 2: Run it**

Run: `python -m pytest tests/test_roster_integration.py -v`
Expected: PASS (or SKIP if cmake is unavailable).

- [ ] **Step 3: Run the whole suite**

Run: `python -m pytest tests/ -v`
Expected: all PASS or SKIP; no failures.

- [ ] **Step 4: Commit**

```bash
git add tests/test_roster_integration.py src/drs_bridge/runtime.py
git commit -m "test(bridge): end-to-end roster binding + probation + hot-reload (B1.43)"
```

---

## Phase 6 — Docs + backlog reconciliation

### Task 6.1: Record the resolved sub-decisions + plan landing

**Files:**
- Modify: `docs/ewtss/specs/drs-instance-addressing-design.md` (§9 — mark roster-delivery resolved)
- Modify: `docs/ewtss/design-backlog.md` (B1.43 — note Plan 1 landed)
- Modify: `docs/ewtss/README.md` (plans list, if it indexes plans)

- [ ] **Step 1: Mark the roster-delivery sub-decision resolved**

In the spec §9, change the "Roster delivery to drs-bridge" bullet to note it is **resolved: compacted Kafka `drs.roster`, key `active`, `group_id=None` + earliest**, implemented in this plan.

- [ ] **Step 2: Update the B1.43 backlog status**

In `docs/ewtss/design-backlog.md`, append to the B1.43 entry's Status: *"Plan 1 (contracts + drs-bridge) landed `docs/ewtss/plans/drs-instance-addressing-bridge-plan.md`; Plan 2 (drs-server + DB) and Plan 3 (UI) pending."*

- [ ] **Step 3: (README index already current)**

The `docs/ewtss/README.md` row for this plan was added when the plan landed (repo convention: index current in the same change). No action unless the plan's scope summary materially changed during execution — if so, update that row.

- [ ] **Step 4: Commit**

```bash
git add docs/ewtss/specs/drs-instance-addressing-design.md docs/ewtss/design-backlog.md docs/ewtss/README.md
git commit -m "docs(B1.43): record Plan 1 landing + resolved roster-delivery decision"
```

---

## Definition of done (Plan 1)

- `python -m pytest tests/ -v` in `drs-bridge/` is green (integration test PASS or SKIP).
- `drs.roster` schema is in `contracts/` and the topic is provisioned compacted.
- drs-bridge binds one TCP command server **per enabled instance**, reconciled from a `drs.roster` snapshot, hot-reloading on change with per-instance isolation.
- Each inbound connection runs under framing probation; non-framing peers are closed + logged; per-instance health events are emitted for the Plan-2 reconcile.
- No drs-server, DB, or UI code was touched.

## Follow-on plans (not in this plan)

- **Plan 2 — drs-server roster store + DB foundation:** Postgres/Timescale + migration/access layer (first DB feature; cross-cutting), `roster`/`roster_entry`/`roster_revision` tables, write/export/import endpoints, the `drs.roster` publisher, the `/roster/*` + `/exercise/readiness` OpenAPI freeze, and the reconcile reading the health events this plan emits.
- **Plan 3 — Sg.App + DRS-webapp surfaces:** catalogue cache + readiness panel (B) and the IP/network-config editing surface (G), after [B1.1](../design-backlog.md) wireframes.
