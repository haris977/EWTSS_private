# Database Ownership Model — SG App ↔ DRS Server

**Status:** Active design record.
**Audience:** SG App (C#/WPF) developers **and** DRS Server (Python/FastAPI) developers. This is the shared contract for who owns, creates, migrates, writes, and reads every table in the v2 database. Read it before touching schema on either side.
**Supersedes:** any proposal to split schema ownership by domain across two migration systems (e.g. EF Core on SG + Alembic on DRS). See §7.

---

## 1. The rule (one sentence)

**`drs-server` owns, creates, and migrates the entire database schema via a single Alembic migration set; every other process — `Sg.App` included — is a runtime SQL *client* to that schema and issues no DDL.**

Everything else in this document follows from that one rule.

## 2. Why a single owner

The database is one shared PostgreSQL 16 + TimescaleDB instance on **WS2**, reached over the LAN by processes on both workstations. A single migration authority is a deliberate architectural choice, not an accident of who built what first:

| Property | Single owner (chosen) | Two owners split by domain (rejected, §7) |
|---|---|---|
| Cross-domain foreign keys | **DB-enforced** (`computed_links.scenario_id → scenarios`, `drs_sessions.exercise_id → exercises`, …) | Cannot be safely DB-enforced across two migration systems → downgraded to app-level integrity |
| DB-Purge cascade ([scenario-management-design §4.9](scenario-management-design.md#49-db-purge-interaction)) | Works — FK cascade ordering is real | Breaks — no cross-system FK to cascade on |
| Deployment ordering | Trivial — drs-server migrates once, everyone connects | Fragile — one migrator must run before the other, cross-LAN |
| Schema-change coordination | One PR, one tool, one owner | Two migration histories that can diverge |
| Migration tooling in the stack | One (SQLAlchemy 2.0 async + Alembic) | Two (Alembic **and** EF Core) |

The dual-owner model spends most of its design effort *mitigating problems it creates for itself*. The single-owner model does not have those problems.

## 3. Writer ≠ owner: the distinction that trips people up

The schema is described in three **logical domains** (see [developer-handbook §10](../developer-handbook.md#10-database-schema-reference)). **Domain names describe write ownership — which process is the source of truth for the data — not schema ownership.** DDL ownership is always `drs-server`.

| Domain | Written by (source of truth) | DDL owned + migrated by |
|---|---|---|
| **Scenario planning** | `Sg.App` today (`Sg.Server` in future Mode B) | **`drs-server` / Alembic** |
| **Time-series telemetry** | `drs-server` | `drs-server` / Alembic |
| **System** (RBAC, logs, roster) | `drs-server` | `drs-server` / Alembic |

The scenario-planning row is the one that surprises people: **`Sg.App` writes those tables at runtime but does not own their DDL.** It connects to WS2 PostgreSQL and issues DML (`INSERT` / `UPDATE` / `DELETE`) directly — but the tables themselves are created and altered only by `drs-server`'s Alembic migrations.

### 3.1 How `Sg.App` knows the schema without owning it (the schema contract)

The natural SG-developer question: *"If I don't own the DDL, how do I know the column names and types to write correct SQL?"* The answer is the same as for every other drs-server-owned interface `Sg.App` already consumes (the REST API, the Kafka topics): **owning the schema is the right to *change* it, not the right to *hide* it.** The schema is a **published, versioned contract**, and `Sg.App` codes against the contract — never against `drs-server` source, and never by owning the DDL.

**Important framing:** `Sg.App` writes `computed_links` / `scenarios` by **direct SQL over the LAN**, not through a REST endpoint. That means the **relational table shape *is* the wire contract** between the two repos — exactly like `drs-server-openapi.yaml` is the wire contract for REST and the `kafka/*.schema.json` files are for Kafka. So it is governed by the same [`contracts/` rule](../../contracts/README.md): *the producer (`drs-server`) changes the contract artifact first; the consumer (`Sg.App`) builds against the artifact.* A breaking column change is a major bump that triggers a coordinated SG bump, tracked in the release manifest.

**Source-of-truth chain (most authoritative last):**

| Layer | Artifact | Role for `Sg.App` |
|---|---|---|
| Machine source of truth | `drs-server/alembic/` migrations | Defines the actual shape; not consumed directly by SG |
| Human-readable reference | [developer-handbook §10.1](../developer-handbook.md#101-scenario-planning-schema-scenario-publisher-writes) | The columns SG writes, in prose — the day-to-day lookup |
| Frozen cross-boundary contract | `contracts/` | What SG **pins and builds against** — see below |

Two `contracts/` artifacts bear on the SG write path:

- **`scenario-content-json-schema.json`** — already exists; pins the `content_json` blob shape (the `compute_inputs` vs `metadata` partition SG serialises into the `scenarios` table), including the **stable per-instance `id`** on each entity/emitter (referenced by `computed_links.entity_id/emitter_id`) and the optional DRS `instance_binding` from which IRS `group_id/unit_id` are ICD-derived (ADR-020).
- **The relational write-surface** — the exact columns/types SG `INSERT`s into `computed_links` and writes into `scenarios` / `scenario_compute_snapshots` / `reports`. This is a cross-boundary interface, so it lives in `contracts/` alongside the OpenAPI and Kafka schemas: **[`contracts/scenario-write-surface.sql`](../../contracts/scenario-write-surface.sql)** is the DDL contract SG builds against. It is a **pre-build freeze item (week 2), owner F (schema) + B (write shapes), accepted in writing by the SG repo owner** ([contracts freeze gate](../../contracts/README.md#ownership--gate)). Status is *partial / contract-first*: `scenarios` / `scenario_compute_snapshots` / `reports` and **all of `computed_links`** (`scenario_id` keying + IRS `group_id/unit_id` + physics + opaque entity/emitter ids) are frozen. The `exercises`-vs-`scenarios` model and entity/emitter identity are **resolved** per [ADR-020](../decision-record.md#adr-020--scenario-is-the-definition-exercise-is-the-run-drs-server-owned-random-mode-is-ws2-only) (the `RESOLVED` block in the file) — no longer pending reconciliation; there is no entity/emitter FK (they are opaque `content_json` ids). "Partial" now means only that the migration isn't in Alembic yet. The change flow in §5.3 applies to every change against it.

**Drift protection — how SG knows it's writing against the *right* version:**

1. **Version assertion at startup.** Alembic stamps the applied migration id in the DB's `alembic_version` table. `Sg.App` reads it on connect and asserts it matches the schema revision it was built/frozen against; on mismatch it **fails fast with a clear "DB schema vX expected, found vY — coordinate with drs-server" error** rather than issuing SQL against an unknown shape.
2. **A capability-probed integration test on the SG side.** One graceful-skip test (per the repo's [testing convention](repository-and-release-strategy.md)) that runs SG's real write path against a freshly-migrated database catches any column/type drift before release — the executable form of the contract.

The net for the SG developer: you never guess the schema and you never read Python source for it. You look it up in developer-handbook §10.1, build against the frozen `contracts/` artifact, assert the version at runtime, and any change you need comes to you through §5.3 as a new frozen contract + migration.

## 4. Table inventory — owner / writer / reader

All tables below are created and migrated **only** by `drs-server` (Alembic, under `drs-server/alembic/`). The columns that vary are *who writes the rows* and *who reads them*.

> **Scenario vs exercise ([ADR-020](../decision-record.md#adr-020--scenario-is-the-definition-exercise-is-the-run-drs-server-owned-random-mode-is-ws2-only)).** A **scenario** is the authored, computed *definition* (Sg.App-written, scenario-planning domain). An **exercise** is a *run* of a scenario — or a Random-mode run with no scenario — and is a **drs-server-written** row in the execution/telemetry domain (§4.2). The scenario definition (entities, emitters, AOP, environment) lives in `scenarios.content_json`, **not** in normalized per-scenario child tables. `computed_links` is keyed on the scenario (`scenario_id`), computed once and replayed on every run.

### 4.1 Scenario-planning domain — DDL: drs-server · writes: Sg.App

| Table | Writes | Reads | Notes |
|---|---|---|---|
| `scenarios` | Sg.App | Sg.App, drs-server | Scenario library + state machine; **definition lives in `content_json`** (entities/emitters/AOP/environment), not child tables ([scenario-management-design §4.1](scenario-management-design.md#41-data-model)) |
| `scenario_compute_snapshots` | Sg.App | Sg.App (reports) | Immutable per-compute history |
| `computed_links` *(hypertable)* | Sg.App | drs-server / drs-bridge (Scenario-mode execution) | **The STK link-analysis output (RF link budget).** Per-tick physics: `range_m, azimuth_deg, elevation_deg, doppler_hz, signal_strength_dbm, path_loss_db, link_margin_db, is_visible`. Publisher hot-path key `(scenario_id, group_id, unit_id, tick_sec)` where `group_id`/`unit_id` are IRS addressing bound at compute (ICD §4); `entity_id`/`emitter_id` are opaque logical ids from `content_json` (no FK, display/provenance). REPLACE-on-recompute. |
| `reports` | Sg.App | Sg.App | Bakes `baked_link_data` for reproducibility |
| `emitter_library` | Sg.App | Sg.App | Reusable emitter templates (EW Library, B1.7 — cross-scenario) |

*(The old normalized definition tables — `exercises`-as-root, `gaming_areas`/`aoi`, `entities`, `emitters`, `emitter_parameters`, `antenna_profiles`, `environments` — are superseded by `content_json` per ADR-020 and are not part of this schema.)*

### 4.2 Execution / telemetry domain — DDL + writes: drs-server

| Table | Writes | Reads | Notes |
|---|---|---|---|
| `exercises` | **drs-server** | drs-server, Sg.App (reports) | **A run** (ADR-020). `scenario_id → scenarios` **nullable** (null = Random-mode run). Both modes: Scenario runs are requested by Sg.App over Kafka `drs.session.control` and minted by drs-server; Random runs are started from the drs_webapp (WS2-only). Parent of `drs_sessions`. |
| `drs_sessions` | drs-server | drs-server, Sg.App (reports) | `exercise_id → exercises` (nullable) |
| `measurements` *(hypertable)* | drs-server | drs-server, Sg.App (reports) | Generic — variant fields in `measurement_scalars`, full message in `payload` jsonb. **Not** `drs_telemetry`. |
| `measurement_scalars` | drs-server | drs-server | Hot indexed columns |

### 4.3 System domain — DDL + writes: drs-server

| Table | Writes | Reads | Notes |
|---|---|---|---|
| `users`, `roles`, `features`, `role_features` | drs-server | both | RBAC |
| `system_logs` *(hypertable)* | drs-server | both | |
| `hardware_profiles` | drs-server | both | Variant TEMPLATE metadata (non-addressing) |
| `roster`, `roster_entry`, `roster_revision` | drs-server | drs-server, drs-bridge | Per-instance addressing (B1.43). **Supersedes** any `drs_instances` / `ip_configurations` sketch. |

> There is **no** `ecs_commands` table and no per-variant config table (e.g. `jhf_config`). Control-Center / ECS is a **client-owned external integration** (surface TBD); response-mode logic lives in **drs-bridge's `ResponseRouter`**, not the DB. Per-variant behaviour is data (generic jsonb in `content_json` / `emitter_library.parameters_json` / `hardware_profiles.config_json` / roster + drs-bridge YAML profiles), never a new table — see §6.

## 5. What this means for you

### 5.1 SG App (C#/WPF) developers

- You are a **write client**. Connect to WS2 PostgreSQL and issue SQL directly (the `computed_links` INSERT batches, the `scenarios` save/hash-diff flow — see [command-flows §2.4 + §2.6](../command-flows.md)).
- **How you know the schema:** you don't own the DDL, but the schema is a published contract, not a secret — look it up in [developer-handbook §10.1](../developer-handbook.md#101-scenario-planning-schema-scenario-publisher-writes), build against the frozen `contracts/` artifact, and assert the `alembic_version` at startup. Full mechanism in **§3.1**.
- **Do not add EF Core / EF migrations, a `DbContext`-driven schema, or any C#-side DDL.** There is no C# migration tooling in v2. If you need a new column or table in the scenario-planning domain, you do not create it — you request it from the DRS-server schema owner (see §5.3).
- Write access is scoped to the scenario-planning tables. Telemetry and system tables are **read-only** for you (reports join `drs_sessions` / `measurements`; auth reads RBAC).
- The cross-LAN round-trip to WS2 PostgreSQL is acceptable on operator-action paths (save, compute) but is **not** a per-frame path — keep it off the hot render loop.

### 5.2 DRS Server (Python/FastAPI) developers

- You **own the whole schema.** Every `CREATE TABLE` / `ALTER TABLE` for all three domains lives in `drs-server/alembic/`, including the scenario-planning tables that Sg.App writes at runtime.
- Foundation is **SQLAlchemy 2.0 async + Alembic + asyncpg** (established by the roster store, [drs-roster-store-plan](../plans/drs-roster-store-plan.md)). New tables in any domain follow that foundation.
- Own cross-domain FK constraints and the DB-Purge cascade ordering. Because you own both sides of every FK, keep them DB-enforced — do not weaken them to app-level checks.
- You are the source of truth for telemetry + system data; you *read* scenario-planning data (e.g. `computed_links` during Scenario-mode execution) but never write it.

### 5.3 Cross-boundary schema changes

When the scenario-planning schema needs to change (SG needs a new column/table):

1. SG raises the change against the **DRS-server schema owner** (team-member F).
2. F adds the Alembic migration in `drs-server/alembic/`, with a test in `drs-server/tests/schema/`.
3. Any read-only ORM model on the *reading* side is updated **in the same PR** (one-directional: only where a service reads another's tables).

There is no symmetric two-way model-sync burden, because there is only one schema owner.

## 6. Anti-pattern guardrails

- **No per-variant tables.** A new hardware/emitter variant must require **zero schema change** — variant-specific fields live as generic jsonb in `content_json.compute_inputs` (per-scenario emitter instances), `emitter_library.parameters_json` (templates), `hardware_profiles.config_json`, the roster, and drs-bridge YAML profiles. (The old normalized `emitter_parameters` table is absorbed into `content_json` — see §4.1.) A `jhf_config`-style table reintroduces the exact per-variant-table anti-pattern v2 removed (see [developer-handbook §9](../developer-handbook.md), [v1 Legacy System Audit](../legacy-system-audit.md)).
- **Use the canonical names.** `computed_links` (not `scenario_compute_links`), `gaming_areas` (not `aoi`), `measurements`/`measurement_scalars` (not `drs_telemetry`), `roster`/`roster_entry` (not `drs_instances`).
- **Know what `computed_links` holds.** It is the RF **link budget** per tick (path loss, signal strength, link margin, AER, doppler) — *not* a session-to-scenario binding table. Session/scenario association is carried by `scenario_id` on the row plus `drs_sessions.exercise_id`, not by a separate "compute links" table.

## 7. Rejected alternative — domain-split dual ownership

A proposal to split schema ownership by domain — SG App owning `scenarios`/`computed_links`/etc. via **EF Core migrations** and DRS Server owning telemetry via **Alembic** — was evaluated and **rejected**. It:

- introduces a second migration system into the stack;
- forces cross-domain FKs down to app-level integrity (a regression that breaks the DB-Purge cascade);
- creates a cross-LAN deployment-ordering dependency (an SG-side migrator on WS1 issuing DDL to the WS2 DB, racing the DRS-side migrator);
- imposes a two-way read-only-model maintenance burden.

Every one of those costs is a self-inflicted consequence of splitting ownership. The single-owner model in §1 avoids all of them. Assigning DDL ownership to `drs-server` is **not** the same as assigning the *scenario domain* to the DRS team — write ownership (§3) stays with SG; only the migration tooling is centralised.

## 8. Open question (not settled here)

**Session lifecycle signalling** — who sets session start/end (e.g. `drs_sessions.started_at` / `ended_at`). This is an *execution-flow* question, not a schema-ownership one: `Sg.App`'s exercise-state service drives execution and emits `drs.session.control { exerciseId }` over Kafka, and drs-server/drs-bridge react. Resolve it in the exercise-execution flow ([command-flows §3.2](../command-flows.md)), not in the DB ownership model.

## 9. References

- [developer-handbook §10](../developer-handbook.md#10-database-schema-reference) — full schema reference + the write-ownership vs schema-ownership note.
- [scenario-management-design](scenario-management-design.md) — `scenarios` lifecycle, `computed_links` REPLACE semantics, snapshots, reports, DB-Purge cascade (§4.9).
- [command-flows §2.4 + §2.6](../command-flows.md) — save + compute sequences (Sg.App writing `computed_links`).
- [drs-roster-store-plan](../plans/drs-roster-store-plan.md) — the SQLAlchemy 2.0 async + Alembic foundation drs-server owns.
- [architecture-diagram](../architecture-diagram.md) — WS1/WS2 topology; edge E10 (`Sg.App → PostgreSQL, computed_links over LAN`).
