# DRS Roster Store — DB Foundation + Roster CRUD API Implementation Plan (Plan 2a of B1.43)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stand up drs-server's first **database foundation** (SQLAlchemy 2.0 async + Alembic) and on it build the **roster store** — `roster` / `roster_entry` / `roster_revision` tables — plus the read/write/export-import REST endpoints, so the active roster can be authored at runtime (the RFQ "IP/network configuration per variant" surface) and projected for WS1 (`/roster/catalogue`) and client ECS (`/roster/addressing`).

**Architecture:** A new `drs_server.db` module owns the async engine + session factory (driver: `asyncpg`). Alembic manages schema. A **pure validation function** (`drs_server.roster.validation`) enforces the spec §5.4 rules over a list of entries with no DB dependency, so it is fully unit-tested. A `RosterRepository` (SQLAlchemy) does CRUD + version-bump + revision-append + the exactly-one-`is_active` invariant, covered by a capability-probed graceful-skip Postgres integration test. FastAPI routers depend on the repository via an injectable provider, so endpoint logic is unit-tested against a fake repository — no DB needed.

**Tech Stack:** Python 3.11, FastAPI, **SQLAlchemy 2.0 async**, **asyncpg**, **Alembic**, pydantic v2, pytest + pytest-asyncio (`asyncio_mode=auto`), ruff. PostgreSQL 16 (integration only).

**Spec:** [docs/ewtss/specs/drs-instance-addressing-design.md](../specs/drs-instance-addressing-design.md) §5.2–§5.4, §6.2 ([B1.43](../design-backlog.md)). **DB stack decision (2026-06-05): SQLAlchemy 2.0 async + Alembic** (cross-cutting; scenarios/RBAC/logs inherit it).

---

## Application target (multi-app repo — read this)

This plan modifies **`drs-server/`** only (path: `drs-server/`). It does **NOT** touch `drs-bridge/`, `sg-app/`, `drs-webapp/`, `mvp4/`, or `contracts/` (the `drs.roster` Kafka contract is already frozen by Plan 1; the `/roster/*` OpenAPI is regenerated from the FastAPI app in Task 4.2, not hand-edited). This is the **first DB-backed feature in drs-server** — the `drs_server.db` foundation it introduces is reused by all later DB work.

## Test-breadth statement

Unit tests in each task cover only the path required to wire the next task. Phase-level breadth lives in **Task 1.2** (validation rule matrix) and **Task 3.3** (the graceful-skip Postgres integration test exercising real CRUD + invariants end-to-end).

## Out of scope (deliberate — don't flag these as gaps)

- **`/exercise/readiness` reconcile, the `drs.roster` publisher, and the `system.health` connection-presence consumer** — Plan 2b. This plan is the store + CRUD only; nothing here publishes to Kafka.
- **Port allocation engine for `allocated` entries** — Plan 2a accepts an explicit port on every entry (the validation allows `allocated` without a port for *forward-compat*, but the auto-allocation algorithm is Plan 2b, alongside the publisher that drs-bridge consumes). Until then an `allocated` entry must carry an explicit port too.
- **Variant-existence validation** — server-side validation is structural (id uniqueness, host:port collision, irs_fixed needs a port). "Variant matches a loaded profile" is drs-bridge's defensive check (Plan 1) since the profile templates live in drs-bridge, not drs-server.
- **TimescaleDB / hypertables** — roster tables are regular tables; the foundation targets plain PostgreSQL 16. Timescale arrives with telemetry work.
- **Auth/RBAC on the endpoints** — B1.16/B1.17; endpoints are unguarded here. `updated_by`/`changed_by` are accepted as a plain string field for now (wired to real identity when auth lands).
- **Sg.App / DRS-webapp consumption** — Plan 3.

## Environment pre-flight

- **Python 3.11** floor (`requires-python = ">=3.11"`, ruff `target-version = "py311"`). 3.11-safe stdlib only; for UTC timestamps use `datetime.now(timezone.utc)` (never `datetime.utcnow()`).
- **New dependencies** (Task 0.1 adds to `drs-server/pyproject.toml`): `sqlalchemy[asyncio]>=2.0`, `asyncpg>=0.29`, `alembic>=1.13`. Install into the existing venv: `drs-server/.venv/Scripts/python.exe -m pip install -e ".[dev]"`. These must be vendored for the air-gap per B1.18 (note in Task 0.1; actual vendoring is that item's job).
- **New top-level dir:** `drs-server/alembic/` (migrations). Confirm `drs-server/.gitignore` does not ignore it; the SQLite/`*.db` and `__pycache__` ignores won't catch it, but verify in Task 0.2.
- **CLIs:** `alembic` (installed with the dep). Run via the venv: `drs-server/.venv/Scripts/alembic.exe` or `python -m alembic`. No Docker.
- **PostgreSQL:** NOT installed locally and NOT auto-installed. The integration test (Task 3.3) **capability-probes** `localhost:5432` and **skips** when absent (mirrors [`drs-server/tests/test_kafka_broker_integration.py`](../../drs-server/tests/test_kafka_broker_integration.py)). Unit tests never touch a DB.
- Run all commands from the repo root `e:/GitHub/ewtss-v2-pub`; pytest via `drs-server/.venv/Scripts/python.exe -m pytest ...`. Current branch: continue on `feat/b1-43-drs-instance-addressing` (or a fresh `feat/b1-43-roster-store` — author's choice; this plan assumes the former).

## Things this plan does NOT do (reviewer calibration)

- No retry/reconnect on the DB engine beyond SQLAlchemy pool defaults.
- No Pydantic-vs-ORM unification (SQLModel was not chosen); ORM models and API DTOs are separate types by design.
- No defensive `isinstance` guards on validated request bodies — FastAPI+Pydantic validate them at the boundary.
- The integration test is the *only* test that touches Postgres; everything else is DB-free by construction.

---

## Phase 0 — DB foundation

### Task 0.1: Add DB deps + `DATABASE_URL` setting + the `db` module

**Files:**
- Modify: `drs-server/pyproject.toml`
- Modify: `drs-server/src/drs_server/config.py`
- Create: `drs-server/src/drs_server/db.py`
- Test: `drs-server/tests/test_db_module.py`

- [ ] **Step 1: Add dependencies**

In `drs-server/pyproject.toml`, add to `dependencies`:
```toml
    "sqlalchemy[asyncio]>=2.0",
    "asyncpg>=0.29",
    "alembic>=1.13",
```
Then install: `drs-server/.venv/Scripts/python.exe -m pip install -e ".[dev]"` (run from `drs-server/`).
> Air-gap note: these three (+ their transitive deps: `greenlet`, `mako`, `markupsafe`) must be vendored as wheels per [B1.18](../design-backlog.md). That vendoring is B1.18's job; this step only declares them.

- [ ] **Step 2: Write the failing test**

Create `drs-server/tests/test_db_module.py`:
```python
def test_database_url_default():
    from drs_server.config import ServerSettings
    s = ServerSettings()
    assert s.database_url.startswith("postgresql+asyncpg://")


def test_make_engine_and_session_factory_constructs():
    from drs_server.db import make_engine, make_session_factory
    engine = make_engine("postgresql+asyncpg://u:p@localhost:5432/db")
    factory = make_session_factory(engine)
    assert engine is not None and factory is not None
```

- [ ] **Step 3: Run it to verify it fails**

Run: `drs-server/.venv/Scripts/python.exe -m pytest drs-server/tests/test_db_module.py -v`
Expected: FAIL — `database_url` attr missing / `drs_server.db` not importable.

- [ ] **Step 4: Add the setting + module**

In `drs-server/src/drs_server/config.py`, add to `ServerSettings` (and document the env var in the module docstring as `DRS_SERVER_DATABASE_URL`):
```python
    database_url: str = "postgresql+asyncpg://ewtss:ewtss@localhost:5432/ewtss"
```

Create `drs-server/src/drs_server/db.py`:
```python
"""Async SQLAlchemy engine + session factory for drs-server.

The first DB-backed feature is the roster store (B1.43 Plan 2a); scenarios,
RBAC, and logs inherit this foundation. Driver: asyncpg.
"""
from __future__ import annotations

from sqlalchemy.ext.asyncio import AsyncEngine, AsyncSession, async_sessionmaker, create_async_engine
from sqlalchemy.orm import DeclarativeBase


class Base(DeclarativeBase):
    """Declarative base for all drs-server ORM models."""


def make_engine(database_url: str) -> AsyncEngine:
    return create_async_engine(database_url, pool_pre_ping=True, future=True)


def make_session_factory(engine: AsyncEngine) -> async_sessionmaker[AsyncSession]:
    return async_sessionmaker(engine, expire_on_commit=False, class_=AsyncSession)
```

- [ ] **Step 5: Run it to verify it passes**

Run: `drs-server/.venv/Scripts/python.exe -m pytest drs-server/tests/test_db_module.py -v`
Expected: PASS (constructing an engine does not connect).

- [ ] **Step 6: Commit**
```bash
git add drs-server/pyproject.toml drs-server/src/drs_server/config.py drs-server/src/drs_server/db.py drs-server/tests/test_db_module.py
git commit -m "feat(server): async SQLAlchemy engine/session foundation + DATABASE_URL (B1.43)"
```

### Task 0.2: ORM models + Alembic baseline migration

**Files:**
- Create: `drs-server/src/drs_server/roster/__init__.py`
- Create: `drs-server/src/drs_server/roster/models.py`
- Create: `drs-server/alembic.ini`, `drs-server/alembic/env.py`, `drs-server/alembic/script.py.mako`, `drs-server/alembic/versions/0001_roster_baseline.py`
- Test: `drs-server/tests/roster/test_models_metadata.py`

- [ ] **Step 1: Write the failing test** (metadata-only; no DB connection)

Create `drs-server/tests/roster/__init__.py` (empty) and `drs-server/tests/roster/test_models_metadata.py`:
```python
def test_roster_tables_registered_on_metadata():
    from drs_server.db import Base
    import drs_server.roster.models  # noqa: F401  (registers tables)
    tables = set(Base.metadata.tables)
    assert {"roster", "roster_entry", "roster_revision"} <= tables


def test_roster_entry_columns():
    from drs_server.db import Base
    import drs_server.roster.models  # noqa: F401
    cols = set(Base.metadata.tables["roster_entry"].columns.keys())
    assert {"id", "roster_id", "instance_id", "variant", "host",
            "command_port", "command_protocol", "response_port",
            "response_protocol", "port_source", "enabled"} <= cols
```

- [ ] **Step 2: Run it to verify it fails**

Run: `drs-server/.venv/Scripts/python.exe -m pytest drs-server/tests/roster/test_models_metadata.py -v`
Expected: FAIL — `drs_server.roster.models` missing.

- [ ] **Step 3: Write the ORM models**

Create `drs-server/src/drs_server/roster/__init__.py` (empty) and `drs-server/src/drs_server/roster/models.py`:
```python
"""SQLAlchemy ORM models for the roster store (B1.43 spec §5.2).

roster          — named rosters; exactly one is_active=true at a time.
roster_entry    — per-instance addressing rows (FK -> roster).
roster_revision — append-only audit; one row per committed change.
"""
from __future__ import annotations

from datetime import datetime, timezone

from sqlalchemy import (
    Boolean, CheckConstraint, ForeignKey, Index, Integer, String, UniqueConstraint, func,
)
from sqlalchemy.orm import Mapped, mapped_column, relationship
from sqlalchemy.types import JSON

from drs_server.db import Base


def _utcnow() -> datetime:
    return datetime.now(timezone.utc)


class Roster(Base):
    __tablename__ = "roster"

    roster_id: Mapped[str] = mapped_column(String(64), primary_key=True)
    name: Mapped[str] = mapped_column(String(128), nullable=False)
    version: Mapped[int] = mapped_column(Integer, nullable=False, default=1)
    is_active: Mapped[bool] = mapped_column(Boolean, nullable=False, default=False)
    updated_at: Mapped[datetime] = mapped_column(default=_utcnow, onupdate=_utcnow)
    updated_by: Mapped[str] = mapped_column(String(128), nullable=False, default="system")

    entries: Mapped[list["RosterEntry"]] = relationship(
        back_populates="roster", cascade="all, delete-orphan", lazy="selectin",
    )

    # Exactly one active roster: a partial unique index over is_active=true.
    __table_args__ = (
        Index("uq_roster_single_active", "is_active",
              unique=True, postgresql_where=(is_active == True)),  # noqa: E712
    )


class RosterEntry(Base):
    __tablename__ = "roster_entry"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    roster_id: Mapped[str] = mapped_column(ForeignKey("roster.roster_id", ondelete="CASCADE"))
    instance_id: Mapped[str] = mapped_column(String(64), nullable=False)
    variant: Mapped[str] = mapped_column(String(64), nullable=False)
    host: Mapped[str] = mapped_column(String(255), nullable=False)
    command_port: Mapped[int] = mapped_column(Integer, nullable=False)
    command_protocol: Mapped[str] = mapped_column(String(3), nullable=False)
    response_port: Mapped[int] = mapped_column(Integer, nullable=False)
    response_protocol: Mapped[str] = mapped_column(String(3), nullable=False)
    port_source: Mapped[str] = mapped_column(String(16), nullable=False)
    enabled: Mapped[bool] = mapped_column(Boolean, nullable=False, default=True)

    roster: Mapped["Roster"] = relationship(back_populates="entries")

    __table_args__ = (
        UniqueConstraint("roster_id", "instance_id", name="uq_entry_roster_instance"),
        CheckConstraint("command_protocol in ('tcp','udp')", name="ck_entry_command_proto"),
        CheckConstraint("response_protocol in ('tcp','udp')", name="ck_entry_response_proto"),
        CheckConstraint("port_source in ('irs_fixed','allocated')", name="ck_entry_port_source"),
    )


class RosterRevision(Base):
    __tablename__ = "roster_revision"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    roster_id: Mapped[str] = mapped_column(String(64), nullable=False)
    version: Mapped[int] = mapped_column(Integer, nullable=False)
    changed_at: Mapped[datetime] = mapped_column(default=_utcnow)
    changed_by: Mapped[str] = mapped_column(String(128), nullable=False, default="system")
    snapshot_json: Mapped[dict] = mapped_column(JSON, nullable=False)

    __table_args__ = (
        UniqueConstraint("roster_id", "version", name="uq_revision_roster_version"),
    )
```

- [ ] **Step 4: Run it to verify it passes**

Run: `drs-server/.venv/Scripts/python.exe -m pytest drs-server/tests/roster/test_models_metadata.py -v`
Expected: PASS (metadata registration needs no DB).

- [ ] **Step 5: Initialise Alembic + write the baseline migration**

From `drs-server/`: `.venv/Scripts/alembic.exe init alembic` (creates `alembic.ini`, `alembic/env.py`, `alembic/script.py.mako`, `alembic/versions/`). Then:

(a) In `drs-server/alembic.ini`, leave `sqlalchemy.url` empty (env.py supplies it from settings).

(b) Replace `drs-server/alembic/env.py`'s config/run section so it reads the URL from `ServerSettings` and targets `Base.metadata`. The load-bearing parts:
```python
from drs_server.config import ServerSettings
from drs_server.db import Base
import drs_server.roster.models  # noqa: F401  (register tables on metadata)

target_metadata = Base.metadata

def _url() -> str:
    # Alembic runs migrations with a SYNC driver; swap asyncpg -> psycopg-style
    # sync URL is unnecessary here because we use a sync psycopg2? No — keep it
    # simple: run Alembic offline/online with the asyncpg URL via async engine.
    return ServerSettings().database_url
```
Use Alembic's **async** template pattern for `run_migrations_online` (create an `AsyncEngine` from `_url()`, `connection.run_sync(do_run_migrations)`). If `alembic init -t async alembic` was used instead, the async scaffold is already present — prefer `alembic init -t async alembic`.

(c) Create `drs-server/alembic/versions/0001_roster_baseline.py` (hand-authored from the models — do **not** rely on autogenerate for the baseline so the partial index is explicit):
```python
"""roster baseline (B1.43)

Revision ID: 0001_roster_baseline
Revises:
Create Date: 2026-06-05
"""
from alembic import op
import sqlalchemy as sa

revision = "0001_roster_baseline"
down_revision = None
branch_labels = None
depends_on = None


def upgrade() -> None:
    op.create_table(
        "roster",
        sa.Column("roster_id", sa.String(64), primary_key=True),
        sa.Column("name", sa.String(128), nullable=False),
        sa.Column("version", sa.Integer, nullable=False, server_default="1"),
        sa.Column("is_active", sa.Boolean, nullable=False, server_default=sa.false()),
        sa.Column("updated_at", sa.DateTime(timezone=True), nullable=False, server_default=sa.func.now()),
        sa.Column("updated_by", sa.String(128), nullable=False, server_default="system"),
    )
    op.create_index(
        "uq_roster_single_active", "roster", ["is_active"],
        unique=True, postgresql_where=sa.text("is_active = true"),
    )
    op.create_table(
        "roster_entry",
        sa.Column("id", sa.Integer, primary_key=True, autoincrement=True),
        sa.Column("roster_id", sa.String(64), sa.ForeignKey("roster.roster_id", ondelete="CASCADE"), nullable=False),
        sa.Column("instance_id", sa.String(64), nullable=False),
        sa.Column("variant", sa.String(64), nullable=False),
        sa.Column("host", sa.String(255), nullable=False),
        sa.Column("command_port", sa.Integer, nullable=False),
        sa.Column("command_protocol", sa.String(3), nullable=False),
        sa.Column("response_port", sa.Integer, nullable=False),
        sa.Column("response_protocol", sa.String(3), nullable=False),
        sa.Column("port_source", sa.String(16), nullable=False),
        sa.Column("enabled", sa.Boolean, nullable=False, server_default=sa.true()),
        sa.UniqueConstraint("roster_id", "instance_id", name="uq_entry_roster_instance"),
        sa.CheckConstraint("command_protocol in ('tcp','udp')", name="ck_entry_command_proto"),
        sa.CheckConstraint("response_protocol in ('tcp','udp')", name="ck_entry_response_proto"),
        sa.CheckConstraint("port_source in ('irs_fixed','allocated')", name="ck_entry_port_source"),
    )
    op.create_table(
        "roster_revision",
        sa.Column("id", sa.Integer, primary_key=True, autoincrement=True),
        sa.Column("roster_id", sa.String(64), nullable=False),
        sa.Column("version", sa.Integer, nullable=False),
        sa.Column("changed_at", sa.DateTime(timezone=True), nullable=False, server_default=sa.func.now()),
        sa.Column("changed_by", sa.String(128), nullable=False, server_default="system"),
        sa.Column("snapshot_json", sa.JSON, nullable=False),
        sa.UniqueConstraint("roster_id", "version", name="uq_revision_roster_version"),
    )


def downgrade() -> None:
    op.drop_table("roster_revision")
    op.drop_table("roster_entry")
    op.drop_index("uq_roster_single_active", table_name="roster")
    op.drop_table("roster")
```

(d) Confirm `drs-server/.gitignore` (or repo root) does not ignore `alembic/`. If there is no drs-server `.gitignore`, the root one applies; verify `alembic/versions/*.py` are trackable (`git status` shows them).

- [ ] **Step 6: Commit**
```bash
git add drs-server/src/drs_server/roster/ drs-server/alembic.ini drs-server/alembic/ drs-server/tests/roster/
git commit -m "feat(server): roster ORM models + Alembic baseline migration (B1.43)"
```

---

## Phase 1 — Validation (pure) + repository

### Task 1.1: DTOs + the pure validation function

**Files:**
- Create: `drs-server/src/drs_server/roster/schemas.py`
- Create: `drs-server/src/drs_server/roster/validation.py`
- Test: `drs-server/tests/roster/test_validation.py`

- [ ] **Step 1: Write the failing test (the rule matrix — phase breadth)**

Create `drs-server/tests/roster/test_validation.py`:
```python
import pytest

from drs_server.roster.schemas import RosterIn, RosterEntryIn, Endpoint
from drs_server.roster.validation import validate_roster, RosterValidationError


def _entry(instance_id="rdfs#1", port=5001, port_source="irs_fixed", enabled=True):
    return RosterEntryIn(
        instance_id=instance_id, variant="rdfs", host="127.0.0.1",
        command=Endpoint(port=port, protocol="tcp"),
        response=Endpoint(port=port + 1, protocol="udp"),
        port_source=port_source, enabled=enabled,
    )


def _roster(entries):
    return RosterIn(roster_id="lab", name="lab", entries=entries)


def test_valid_roster_passes():
    validate_roster(_roster([_entry("rdfs#1", 5001), _entry("rdfs#2", 5003)]))


def test_duplicate_instance_id_rejected():
    with pytest.raises(RosterValidationError, match="duplicate instance_id"):
        validate_roster(_roster([_entry("rdfs#1", 5001), _entry("rdfs#1", 5003)]))


def test_host_port_collision_among_enabled_rejected():
    with pytest.raises(RosterValidationError, match="host:port"):
        validate_roster(_roster([_entry("rdfs#1", 5001), _entry("rdfs#2", 5001)]))


def test_disabled_entry_excluded_from_collision_check():
    validate_roster(_roster([_entry("rdfs#1", 5001),
                             _entry("rdfs#2", 5001, enabled=False)]))


def test_irs_fixed_requires_port():
    e = _entry("rdfs#1")
    e.command.port = None  # type: ignore[assignment]
    with pytest.raises(RosterValidationError, match="irs_fixed"):
        validate_roster(_roster([e]))
```

- [ ] **Step 2: Run it to verify it fails**

Run: `drs-server/.venv/Scripts/python.exe -m pytest drs-server/tests/roster/test_validation.py -v`
Expected: FAIL — modules missing.

- [ ] **Step 3: Write the schemas + validation**

Create `drs-server/src/drs_server/roster/schemas.py`:
```python
"""Pydantic request/response DTOs for the roster API. Separate from the ORM
models by design (no SQLModel coupling)."""
from __future__ import annotations

from typing import Literal, Optional

from pydantic import BaseModel


class Endpoint(BaseModel):
    port: Optional[int] = None
    protocol: Literal["tcp", "udp"]


class RosterEntryIn(BaseModel):
    instance_id: str
    variant: str
    host: str
    command: Endpoint
    response: Endpoint
    port_source: Literal["irs_fixed", "allocated"]
    enabled: bool = True


class RosterIn(BaseModel):
    roster_id: str
    name: str
    entries: list[RosterEntryIn]


class CatalogueEntry(BaseModel):
    instance_id: str
    variant: str


class AddressingEntry(BaseModel):
    instance_id: str
    host: str
    command_port: int
    command_protocol: str
```

Create `drs-server/src/drs_server/roster/validation.py`:
```python
"""Pure, DB-free validation of a roster per spec §5.4. drs-server is the
authoritative validator on every write/import."""
from __future__ import annotations

from drs_server.roster.schemas import RosterIn


class RosterValidationError(ValueError):
    """Raised when a roster violates a structural rule. Message names the offender."""


def validate_roster(roster: RosterIn) -> None:
    seen_ids: set[str] = set()
    seen_addrs: dict[tuple[str, int, str], str] = {}
    for e in roster.entries:
        if e.instance_id in seen_ids:
            raise RosterValidationError(f"duplicate instance_id: {e.instance_id}")
        seen_ids.add(e.instance_id)

        if e.port_source == "irs_fixed" and e.command.port is None:
            raise RosterValidationError(
                f"irs_fixed entry {e.instance_id} must carry an explicit command port"
            )
        # Plan 2a: allocated entries must also carry an explicit port until the
        # Plan 2b allocation engine lands.
        if e.command.port is None:
            raise RosterValidationError(
                f"entry {e.instance_id} has no command port (auto-allocation is Plan 2b)"
            )

        if e.enabled:
            key = (e.host, e.command.port, e.command.protocol)
            if key in seen_addrs:
                raise RosterValidationError(
                    f"host:port collision: {e.instance_id} and {seen_addrs[key]} "
                    f"both bind {e.host}:{e.command.port}/{e.command.protocol}"
                )
            seen_addrs[key] = e.instance_id
```

- [ ] **Step 4: Run it to verify it passes**

Run: `drs-server/.venv/Scripts/python.exe -m pytest drs-server/tests/roster/test_validation.py -v`
Expected: PASS (5 tests).

- [ ] **Step 5: Commit**
```bash
git add drs-server/src/drs_server/roster/schemas.py drs-server/src/drs_server/roster/validation.py drs-server/tests/roster/test_validation.py
git commit -m "feat(server): roster DTOs + pure validation rules (B1.43)"
```

### Task 1.2: `RosterRepository` (SQLAlchemy CRUD + version/revision + active invariant)

**Files:**
- Create: `drs-server/src/drs_server/roster/repository.py`
- Test: covered by the integration test in Task 3.3 (repository touches the DB; no DB-free unit test). This task writes the code; Task 3.3 proves it.

- [ ] **Step 1: Write the repository**

Create `drs-server/src/drs_server/roster/repository.py`:
```python
"""RosterRepository — persistence + invariants for the roster store.

Validation (spec §5.4) is enforced by `validate_roster` BEFORE any write.
Each committed change bumps `version`, appends a `roster_revision`, and (for
set-active) maintains the exactly-one-is_active invariant.
"""
from __future__ import annotations

from sqlalchemy import select, update
from sqlalchemy.ext.asyncio import AsyncSession

from drs_server.roster import models
from drs_server.roster.schemas import RosterIn
from drs_server.roster.validation import validate_roster


class RosterNotFound(LookupError):
    pass


class RosterRepository:
    def __init__(self, session: AsyncSession) -> None:
        self._s = session

    async def upsert(self, roster_in: RosterIn, changed_by: str = "system") -> models.Roster:
        validate_roster(roster_in)  # authoritative; raises RosterValidationError
        existing = await self._s.get(models.Roster, roster_in.roster_id)
        if existing is None:
            row = models.Roster(roster_id=roster_in.roster_id, name=roster_in.name,
                                version=1, is_active=False, updated_by=changed_by)
            self._s.add(row)
        else:
            row = existing
            row.name = roster_in.name
            row.version = row.version + 1
            row.updated_by = changed_by
            # Replace entries wholesale (the snapshot is the desired state).
            for e in list(row.entries):
                await self._s.delete(e)
            await self._s.flush()
        row.entries = [
            models.RosterEntry(
                instance_id=e.instance_id, variant=e.variant, host=e.host,
                command_port=e.command.port, command_protocol=e.command.protocol,
                response_port=e.response.port, response_protocol=e.response.protocol,
                port_source=e.port_source, enabled=e.enabled,
            )
            for e in roster_in.entries
        ]
        await self._s.flush()
        self._s.add(models.RosterRevision(
            roster_id=row.roster_id, version=row.version, changed_by=changed_by,
            snapshot_json=roster_in.model_dump(),
        ))
        await self._s.commit()
        await self._s.refresh(row)
        return row

    async def get(self, roster_id: str) -> models.Roster:
        row = await self._s.get(models.Roster, roster_id)
        if row is None:
            raise RosterNotFound(roster_id)
        return row

    async def get_active(self) -> models.Roster | None:
        result = await self._s.execute(select(models.Roster).where(models.Roster.is_active.is_(True)))
        return result.scalar_one_or_none()

    async def set_active(self, roster_id: str, changed_by: str = "system") -> models.Roster:
        target = await self.get(roster_id)
        # Clear any current active, then set this one — within one transaction
        # so the partial unique index never sees two actives.
        await self._s.execute(update(models.Roster).where(models.Roster.is_active.is_(True))
                              .values(is_active=False))
        target.is_active = True
        target.updated_by = changed_by
        await self._s.commit()
        await self._s.refresh(target)
        return target
```

- [ ] **Step 2: Smoke-import it (no DB)**

Run: `drs-server/.venv/Scripts/python.exe -c "import drs_server.roster.repository; print('import ok')"`
Expected: `import ok` (proves no syntax/typing import errors; real behaviour is Task 3.3).

- [ ] **Step 3: Commit**
```bash
git add drs-server/src/drs_server/roster/repository.py
git commit -m "feat(server): RosterRepository CRUD + version/revision + active invariant (B1.43)"
```

---

## Phase 2 — REST endpoints (unit-tested against a fake repository)

### Task 2.1: Repository provider + read endpoints (catalogue, addressing, get, export)

**Files:**
- Create: `drs-server/src/drs_server/api/roster.py`
- Create: `drs-server/src/drs_server/roster/projections.py`
- Test: `drs-server/tests/roster/test_roster_api.py`

- [ ] **Step 1: Write the failing test (endpoints via a fake repo + FastAPI dependency override)**

Create `drs-server/tests/roster/test_roster_api.py`:
```python
import pytest
from fastapi import FastAPI
from fastapi.testclient import TestClient

from drs_server.api import roster as roster_api
from drs_server.roster.schemas import RosterIn, RosterEntryIn, Endpoint


class _FakeRepo:
    def __init__(self, active=None):
        self._active = active
        self.upserted = []
        self.activated = []

    async def get_active(self):
        return self._active

    async def upsert(self, roster_in, changed_by="system"):
        self.upserted.append(roster_in)
        return _ActiveStub(roster_in)

    async def set_active(self, roster_id, changed_by="system"):
        self.activated.append(roster_id)
        return _ActiveStub_byid(roster_id)


class _Entry:
    def __init__(self, iid, variant, host, cport):
        self.instance_id, self.variant, self.host = iid, variant, host
        self.command_port, self.command_protocol = cport, "tcp"


class _ActiveStub:
    def __init__(self, roster_in):
        self.roster_id, self.version = roster_in.roster_id, 7
        self.entries = [_Entry(e.instance_id, e.variant, e.host, e.command.port)
                        for e in roster_in.entries]


def _app(repo):
    app = FastAPI()
    app.include_router(roster_api.router)
    app.dependency_overrides[roster_api.get_repo] = lambda: repo
    return app


def _sample_active():
    s = _ActiveStub(RosterIn(roster_id="lab", name="lab", entries=[
        RosterEntryIn(instance_id="rdfs#1", variant="rdfs", host="10.0.0.5",
                      command=Endpoint(port=5001, protocol="tcp"),
                      response=Endpoint(port=5002, protocol="udp"),
                      port_source="irs_fixed", enabled=True),
    ]))
    return s


def test_catalogue_is_port_free_with_etag():
    repo = _FakeRepo(active=_sample_active())
    client = TestClient(_app(repo))
    r = client.get("/roster/catalogue")
    assert r.status_code == 200
    assert r.json() == [{"instance_id": "rdfs#1", "variant": "rdfs"}]
    assert r.headers["ETag"] == "lab@7"


def test_addressing_has_host_port():
    repo = _FakeRepo(active=_sample_active())
    client = TestClient(_app(repo))
    r = client.get("/roster/addressing")
    assert r.json() == [{"instance_id": "rdfs#1", "host": "10.0.0.5",
                         "command_port": 5001, "command_protocol": "tcp"}]


def test_catalogue_404_when_no_active_roster():
    client = TestClient(_app(_FakeRepo(active=None)))
    assert client.get("/roster/catalogue").status_code == 404
```
> Note: `_ActiveStub_byid` is referenced by the fake's `set_active`; define it in the test file as a tiny stub with `.roster_id`/`.version`/`.entries=[]`. (Add it next to `_ActiveStub`.) The write-endpoint tests in Task 2.2 use it.

- [ ] **Step 2: Run it to verify it fails**

Run: `drs-server/.venv/Scripts/python.exe -m pytest drs-server/tests/roster/test_roster_api.py -v`
Expected: FAIL — `drs_server.api.roster` missing.

- [ ] **Step 3: Write projections + the router**

Create `drs-server/src/drs_server/roster/projections.py`:
```python
"""Pure projections of a persisted roster into API shapes."""
from __future__ import annotations

from drs_server.roster.schemas import AddressingEntry, CatalogueEntry


def catalogue(roster) -> list[CatalogueEntry]:
    return [CatalogueEntry(instance_id=e.instance_id, variant=e.variant) for e in roster.entries]


def addressing(roster) -> list[AddressingEntry]:
    return [
        AddressingEntry(instance_id=e.instance_id, host=e.host,
                        command_port=e.command_port, command_protocol=e.command_protocol)
        for e in roster.entries
    ]


def etag(roster) -> str:
    return f"{roster.roster_id}@{roster.version}"
```

Create `drs-server/src/drs_server/api/roster.py`:
```python
"""Roster REST API (B1.43 spec §6.2). Read + write + export/import. The
/exercise/readiness reconcile and the drs.roster publisher are Plan 2b.
"""
from __future__ import annotations

from fastapi import APIRouter, Depends, HTTPException, Request, Response

from drs_server.roster import projections
from drs_server.roster.repository import RosterRepository, RosterNotFound
from drs_server.roster.schemas import RosterIn
from drs_server.roster.validation import RosterValidationError

router = APIRouter(prefix="/roster", tags=["roster"])


async def get_repo(request: Request) -> RosterRepository:
    """Provider: a request-scoped repository bound to a fresh session.
    Overridden in unit tests; wired to the real session factory in Task 3.1."""
    factory = request.app.state.session_factory
    async with factory() as session:
        yield RosterRepository(session)


@router.get("/catalogue")
async def get_catalogue(response: Response, repo: RosterRepository = Depends(get_repo)):
    roster = await repo.get_active()
    if roster is None:
        raise HTTPException(status_code=404, detail="no active roster")
    response.headers["ETag"] = projections.etag(roster)
    return projections.catalogue(roster)


@router.get("/addressing")
async def get_addressing(repo: RosterRepository = Depends(get_repo)):
    roster = await repo.get_active()
    if roster is None:
        raise HTTPException(status_code=404, detail="no active roster")
    return projections.addressing(roster)
```

> `get_repo` is an async generator dependency (FastAPI supports `yield` deps). In unit tests it's overridden with a plain lambda returning the fake repo — FastAPI accepts a non-generator override.

- [ ] **Step 4: Run it to verify it passes**

Run: `drs-server/.venv/Scripts/python.exe -m pytest drs-server/tests/roster/test_roster_api.py -v`
Expected: PASS (3 read-endpoint tests).

- [ ] **Step 5: Commit**
```bash
git add drs-server/src/drs_server/api/roster.py drs-server/src/drs_server/roster/projections.py drs-server/tests/roster/test_roster_api.py
git commit -m "feat(server): roster read endpoints (catalogue, addressing) (B1.43)"
```

### Task 2.2: Write + export/import endpoints

**Files:**
- Modify: `drs-server/src/drs_server/api/roster.py`
- Test: `drs-server/tests/roster/test_roster_api.py` (add cases)

- [ ] **Step 1: Write the failing tests**

Add to `drs-server/tests/roster/test_roster_api.py`:
```python
def test_put_roster_upserts_and_returns_etag():
    repo = _FakeRepo()
    client = TestClient(_app(repo))
    body = {
        "roster_id": "lab", "name": "lab",
        "entries": [{
            "instance_id": "rdfs#1", "variant": "rdfs", "host": "10.0.0.5",
            "command": {"port": 5001, "protocol": "tcp"},
            "response": {"port": 5002, "protocol": "udp"},
            "port_source": "irs_fixed", "enabled": True,
        }],
    }
    r = client.put("/roster/lab", json=body)
    assert r.status_code == 200
    assert r.json()["etag"] == "lab@7"
    assert repo.upserted and repo.upserted[0].roster_id == "lab"


def test_put_roster_validation_error_returns_422():
    repo = _FakeRepo()
    # make upsert raise the validation error
    async def _boom(roster_in, changed_by="system"):
        from drs_server.roster.validation import RosterValidationError
        raise RosterValidationError("duplicate instance_id: rdfs#1")
    repo.upsert = _boom
    client = TestClient(_app(repo))
    body = {"roster_id": "lab", "name": "lab", "entries": []}
    r = client.put("/roster/lab", json=body)
    assert r.status_code == 422
    assert "duplicate instance_id" in r.json()["detail"]


def test_post_set_active():
    repo = _FakeRepo()
    client = TestClient(_app(repo))
    r = client.post("/roster/lab/activate")
    assert r.status_code == 200
    assert repo.activated == ["lab"]


def test_export_returns_roster_document():
    repo = _FakeRepo(active=_sample_active())
    # add a get() to the fake returning the active stub
    async def _get(rid):
        return repo._active
    repo.get = _get
    client = TestClient(_app(repo))
    r = client.get("/roster/lab/export")
    assert r.status_code == 200
    assert r.json()["roster_id"] == "lab"
    assert r.json()["entries"][0]["instance_id"] == "rdfs#1"
```
(Add `_ActiveStub_byid` near `_ActiveStub` if not already: a stub with `roster_id`, `version=1`, `entries=[]`.)

- [ ] **Step 2: Run to verify fail**

Run: `drs-server/.venv/Scripts/python.exe -m pytest drs-server/tests/roster/test_roster_api.py -v`
Expected: FAIL — PUT/activate/export routes missing.

- [ ] **Step 3: Add the write + export/import routes**

Append to `drs-server/src/drs_server/api/roster.py`:
```python
@router.put("/{roster_id}")
async def put_roster(roster_id: str, body: RosterIn, repo: RosterRepository = Depends(get_repo)):
    if body.roster_id != roster_id:
        raise HTTPException(status_code=400, detail="roster_id in path != body")
    try:
        row = await repo.upsert(body)
    except RosterValidationError as e:
        raise HTTPException(status_code=422, detail=str(e))
    return {"roster_id": row.roster_id, "version": row.version, "etag": projections.etag(row)}


@router.post("/{roster_id}/activate")
async def activate_roster(roster_id: str, repo: RosterRepository = Depends(get_repo)):
    try:
        row = await repo.set_active(roster_id)
    except RosterNotFound:
        raise HTTPException(status_code=404, detail=f"roster {roster_id} not found")
    return {"roster_id": row.roster_id, "active": True, "etag": projections.etag(row)}


@router.get("/{roster_id}/export")
async def export_roster(roster_id: str, repo: RosterRepository = Depends(get_repo)):
    try:
        row = await repo.get(roster_id)
    except RosterNotFound:
        raise HTTPException(status_code=404, detail=f"roster {roster_id} not found")
    return {
        "roster_id": row.roster_id, "version": row.version,
        "entries": [
            {
                "instance_id": e.instance_id, "variant": e.variant, "host": e.host,
                "command": {"port": e.command_port, "protocol": e.command_protocol},
                "response": {"port": e.response_port, "protocol": e.response_protocol},
                "port_source": e.port_source, "enabled": e.enabled,
            }
            for e in row.entries
        ],
    }


@router.post("/import")
async def import_roster(body: RosterIn, repo: RosterRepository = Depends(get_repo)):
    try:
        row = await repo.upsert(body)
    except RosterValidationError as e:
        raise HTTPException(status_code=422, detail=str(e))
    return {"roster_id": row.roster_id, "version": row.version, "etag": projections.etag(row)}
```
> The export test's `_Entry` stub only sets command fields; extend it with `response_port=e.command_port+1`, `response_protocol="udp"`, `port_source="irs_fixed"`, `enabled=True` so `export_roster` can read them. Update `_Entry.__init__` accordingly in the test.

- [ ] **Step 4: Run to verify pass**

Run: `drs-server/.venv/Scripts/python.exe -m pytest drs-server/tests/roster/test_roster_api.py -v`
Expected: PASS (all read + write + export tests).

- [ ] **Step 5: Commit**
```bash
git add drs-server/src/drs_server/api/roster.py drs-server/tests/roster/test_roster_api.py
git commit -m "feat(server): roster write + export/import endpoints (B1.43)"
```

---

## Phase 3 — Wiring + Postgres integration

### Task 3.1: Wire the session factory + router into the app

**Files:**
- Modify: `drs-server/src/drs_server/lifespan.py`
- Modify: `drs-server/src/drs_server/main.py`
- Test: `drs-server/tests/test_main_construction.py` (extend)

- [ ] **Step 1: Write the failing test**

Add to `drs-server/tests/test_main_construction.py` (a construction-only check that the roster router is mounted and the session factory is created):
```python
def test_roster_router_mounted():
    from drs_server.main import app
    paths = {r.path for r in app.routes}
    assert "/roster/catalogue" in paths
    assert "/roster/addressing" in paths
```

- [ ] **Step 2: Run to verify fail**

Run: `drs-server/.venv/Scripts/python.exe -m pytest drs-server/tests/test_main_construction.py -v`
Expected: FAIL — roster routes not mounted.

- [ ] **Step 3: Wire it**

In `drs-server/src/drs_server/lifespan.py`, create the engine + session factory and store on `app.state` (add near the other `app.state.*` assignments; accept an optional `session_factory_impl` param for tests so no real DB is needed at construction):
```python
from drs_server.db import make_engine, make_session_factory
# in make_lifespan signature add: session_factory_impl: Callable | None = None
# inside lifespan(), before `yield`:
        if session_factory_impl is not None:
            app.state.session_factory = session_factory_impl
        else:
            engine = make_engine(database_url)   # add database_url param to make_lifespan
            app.state.db_engine = engine
            app.state.session_factory = make_session_factory(engine)
```
Add `database_url: str` to `make_lifespan(...)` params. On shutdown, `await app.state.db_engine.dispose()` if it was created.

In `drs-server/src/drs_server/main.py`:
```python
from drs_server.api import roster as roster_api
# pass database_url into make_lifespan:
    lifespan=make_lifespan(
        ntpq_path=_settings.ntpq_path,
        kafka_bootstrap=_settings.kafka_bootstrap,
        poll_seconds=_settings.poll_seconds,
        database_url=_settings.database_url,
    ),
# and after include_router(time_status.router):
app.include_router(roster_api.router)
```
> Construction (`from drs_server.main import app`) must not open a DB connection — `make_engine` is lazy (no connect until first use), so importing the app + listing routes stays DB-free. The `test_roster_router_mounted` test only inspects `app.routes`.

- [ ] **Step 4: Run to verify pass + full unit suite**

Run: `drs-server/.venv/Scripts/python.exe -m pytest drs-server/ -q`
Expected: PASS (new + existing; any Postgres integration test SKIPS — Task 3.3).

- [ ] **Step 5: Commit**
```bash
git add drs-server/src/drs_server/lifespan.py drs-server/src/drs_server/main.py drs-server/tests/test_main_construction.py
git commit -m "feat(server): wire session factory + roster router into app (B1.43)"
```

### Task 3.2: Postgres test fixture (capability-probed, graceful-skip)

**Files:**
- Create: `drs-server/tests/roster/conftest.py`

- [ ] **Step 1: Write the fixture**

Create `drs-server/tests/roster/conftest.py`:
```python
"""Capability-probed Postgres fixtures. Skips when no DB is reachable, so the
unit suite runs without a database (mirrors test_kafka_broker_integration.py)."""
from __future__ import annotations

import os
import socket

import pytest
import pytest_asyncio

_DB_URL = os.environ.get(
    "DRS_SERVER_TEST_DATABASE_URL",
    "postgresql+asyncpg://ewtss:ewtss@localhost:5432/ewtss_test",
)


def _pg_reachable(host: str = "localhost", port: int = 5432, timeout_s: float = 1.0) -> bool:
    try:
        with socket.create_connection((host, port), timeout=timeout_s):
            return True
    except OSError:
        return False


@pytest_asyncio.fixture
async def session():
    if not _pg_reachable():
        pytest.skip("PostgreSQL not reachable at localhost:5432; skipping DB integration test")
    from sqlalchemy.ext.asyncio import AsyncSession
    from drs_server.db import Base, make_engine, make_session_factory
    import drs_server.roster.models  # noqa: F401

    engine = make_engine(_DB_URL)
    async with engine.begin() as conn:
        await conn.run_sync(Base.metadata.drop_all)
        await conn.run_sync(Base.metadata.create_all)
    factory = make_session_factory(engine)
    async with factory() as s:  # type: AsyncSession
        yield s
    await engine.dispose()
```
> Uses `metadata.create_all` (not Alembic) for test isolation/speed; the Alembic baseline is exercised separately when you run `alembic upgrade head` against a real DB during deployment. The partial unique index is created by `create_all` from the model's `__table_args__`.

- [ ] **Step 2: Run (skips without DB)**

Run: `drs-server/.venv/Scripts/python.exe -m pytest drs-server/tests/roster/ -q`
Expected: PASS/SKIP — no failures.

- [ ] **Step 3: Commit**
```bash
git add drs-server/tests/roster/conftest.py
git commit -m "test(server): capability-probed Postgres session fixture (B1.43)"
```

### Task 3.3: Repository integration test (real CRUD + invariants) — graceful-skip

**Files:**
- Create: `drs-server/tests/roster/test_repository_integration.py`

- [ ] **Step 1: Write the integration test**

Create `drs-server/tests/roster/test_repository_integration.py`:
```python
import pytest

from drs_server.roster.repository import RosterRepository, RosterNotFound
from drs_server.roster.schemas import RosterIn, RosterEntryIn, Endpoint
from drs_server.roster.validation import RosterValidationError


def _entry(iid="rdfs#1", port=5001, enabled=True):
    return RosterEntryIn(
        instance_id=iid, variant="rdfs", host="127.0.0.1",
        command=Endpoint(port=port, protocol="tcp"),
        response=Endpoint(port=port + 1, protocol="udp"),
        port_source="irs_fixed", enabled=enabled,
    )


def _roster(rid="lab", entries=None):
    return RosterIn(roster_id=rid, name=rid, entries=entries or [_entry()])


@pytest.mark.asyncio
async def test_upsert_then_get_roundtrip(session):
    repo = RosterRepository(session)
    await repo.upsert(_roster(entries=[_entry("rdfs#1", 5001), _entry("rdfs#2", 5003)]))
    row = await repo.get("lab")
    assert {e.instance_id for e in row.entries} == {"rdfs#1", "rdfs#2"}
    assert row.version == 1


@pytest.mark.asyncio
async def test_re_upsert_bumps_version_and_appends_revision(session):
    repo = RosterRepository(session)
    await repo.upsert(_roster())
    await repo.upsert(_roster())  # second commit
    row = await repo.get("lab")
    assert row.version == 2
    from sqlalchemy import select, func
    from drs_server.roster import models
    n = (await session.execute(
        select(func.count()).select_from(models.RosterRevision)
        .where(models.RosterRevision.roster_id == "lab"))).scalar_one()
    assert n == 2


@pytest.mark.asyncio
async def test_validation_error_blocks_write(session):
    repo = RosterRepository(session)
    with pytest.raises(RosterValidationError):
        await repo.upsert(_roster(entries=[_entry("rdfs#1", 5001), _entry("rdfs#1", 5003)]))


@pytest.mark.asyncio
async def test_exactly_one_active(session):
    repo = RosterRepository(session)
    await repo.upsert(_roster("lab"))
    await repo.upsert(_roster("bench"))
    await repo.set_active("lab")
    await repo.set_active("bench")  # must atomically flip lab off
    active = await repo.get_active()
    assert active.roster_id == "bench"
    lab = await repo.get("lab")
    assert lab.is_active is False
```

- [ ] **Step 2: Run (skips without DB; PASS with a local Postgres)**

Run: `drs-server/.venv/Scripts/python.exe -m pytest drs-server/tests/roster/test_repository_integration.py -v`
Expected: SKIP (no DB here) or PASS (where Postgres is reachable). `[lab-only / integration]`

- [ ] **Step 3: Run the whole drs-server suite**

Run: `drs-server/.venv/Scripts/python.exe -m pytest drs-server/ -q`
Expected: all PASS or SKIP; no failures.

- [ ] **Step 4: Commit**
```bash
git add drs-server/tests/roster/test_repository_integration.py
git commit -m "test(server): roster repository integration (CRUD + invariants), graceful-skip (B1.43)"
```

---

## Phase 4 — Docs + contract

### Task 4.1: Regenerate the `/roster/*` OpenAPI into `contracts/`

**Files:**
- Modify: `contracts/drs-server-openapi.yaml`
- Modify: `contracts/README.md`

- [ ] **Step 1: Regenerate the OpenAPI from the live app**

Run (from repo root): `drs-server/.venv/Scripts/python.exe -c "import json; from drs_server.main import app; print(json.dumps(app.openapi()))" > contracts/drs-server-openapi.json` then curate into `contracts/drs-server-openapi.yaml` per the existing curation note in `contracts/README.md` (the `/roster/catalogue|addressing|{id}|{id}/export|{id}/activate|import` paths should now appear). Remove the temp `.json`.

- [ ] **Step 2: Update the contracts status table**

In `contracts/README.md`, move the `/roster/*` lines from "planned" to baselined under the drs-server REST API row (note: catalogue/addressing/export/import/activate landed in Plan 2a; `/exercise/readiness` is Plan 2b).

- [ ] **Step 3: Commit**
```bash
git add contracts/drs-server-openapi.yaml contracts/README.md
git commit -m "contracts: baseline /roster/* OpenAPI from drs-server app (B1.43)"
```

### Task 4.2: Update spec §9 + backlog + README index

**Files:**
- Modify: `docs/ewtss/specs/drs-instance-addressing-design.md` (§9 — mark roster file-vs-DB note already resolved; note Plan 2a landed the DB-backed store)
- Modify: `docs/ewtss/design-backlog.md` (B1.43 status — Plan 2a done)
- Modify: `docs/ewtss/README.md` (plans index — add this plan)

- [ ] **Step 1: Update the docs**

- In the spec, add a line under §6.2/§9 noting the DB store is implemented (SQLAlchemy 2.0 + Alembic) per Plan 2a; the publisher + reconcile remain Plan 2b.
- In `design-backlog.md` B1.43 Status: append "Plan 2a (DB foundation + roster store + CRUD/export-import endpoints) landed `plans/drs-roster-store-plan.md`; Plan 2b (publisher + reconcile + health-presence consumer) pending."
- `docs/ewtss/README.md` plans list: the row for `plans/drs-roster-store-plan.md` was added when this plan landed (repo convention) — update it only if scope materially changed during execution.

- [ ] **Step 2: Commit**
```bash
git add docs/ewtss/specs/drs-instance-addressing-design.md docs/ewtss/design-backlog.md docs/ewtss/README.md
git commit -m "docs(B1.43): record Plan 2a (DB foundation + roster store) landing"
```

---

## Definition of done (Plan 2a)

- `drs-server/.venv/Scripts/python.exe -m pytest drs-server/ -q` is green (Postgres integration PASS or SKIP).
- SQLAlchemy 2.0 async + Alembic foundation in place; `roster`/`roster_entry`/`roster_revision` tables defined + baselined.
- Roster CRUD + export/import endpoints work; validation (§5.4) authoritative on every write; version bump + revision append + exactly-one-active invariant exercised by the integration test.
- `/roster/*` OpenAPI baselined in `contracts/`.
- No Kafka publishing, no reconcile (Plan 2b); no drs-bridge/UI changes.

## Follow-on

- **Plan 2b — publisher + reconcile:** publish the active roster to the compacted `drs.roster` topic on every change + at startup (drs-bridge already consumes it — Plan 1); consume `system.health` `instance.*` events into a per-instance connection-presence map; implement `POST /exercise/readiness` returning `{configured, reachable, time_synced}` + roster-drift, reading the presence map + `SyncStateEngine.current_variant_status`. This is where the **real-broker integration test** (deferred from Plan 1) lands. Also the port-allocation engine for `allocated` entries.
- **Plan 3 — Sg.App + DRS-webapp surfaces** (after B1.1 wireframes).
