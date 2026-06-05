# Scenario Management — Design Spec

**Date:** 2026-05-22
**Status:** Approved (user-confirmed scope 2026-05-22 — single-site delivery, DB-of-truth, REPLACE-on-recompute)
**Author:** architecture review (brainstorm pass against [B1.19](../design-backlog.md))
**Supersedes:** the design sketch embedded in [B1.19](../design-backlog.md) — that entry now points at this spec.

## 1. Goal

Close the scenario-management gap in the v2 doc set. Define the full lifecycle of a scenario from creation through edit, save, compute, recompute, archive, and deletion — including the interaction between the in-memory STK state, the persistent `scenarios` library, and the `computed_links` hypertable.

**Audience:** B (C# specialist — owns Sg.App scenario library + edit-invalidation + compute pipeline), F (Sr Python — owns DB schema + foreign-key constraints), D (cross-stack lead — reviews the contract between B and F).

## 2. Context

The v2 design has clean *runtime* mechanisms for scenarios:
- [command-flows §2.6](../command-flows.md): the compute-link-analysis sequence (STK → batched INSERT into `computed_links`).
- [command-flows §3.2](../command-flows.md): Scenario-mode execution (`drs.session.control { exerciseId=X }` over Kafka).
- [architecture-overview §4.5](../architecture-overview.md#45-scenario-compute-and-link-analysis-write-back-scenario-modes-planning): scenario authoring → compute → `computed_links` write-back path.
- [Developer Handbook §10.1](../developer-handbook.md#101-scenario-planning-schema-scenario-publisher-writes): existing `computed_links` schema (telemetry/planning side).

What's **missing** today is the *library-side lifecycle* around those mechanisms: how scenarios accumulate over the system's lifecycle, how edits invalidate prior compute results, what guarantees the recompute mechanism makes about hot-path query simplicity, what audit trail backs report generation, and what the operator-facing library UI looks like.

[B1.19](../design-backlog.md) was added 2026-05-15 to track this; this spec elevates it from a backlog item to a canonical design record so B and F can implement against a frozen contract instead of inferring it from scattered notes.

## 3. Scope and non-scope

**In scope:**
- Persistent `scenarios` library on WS2 PostgreSQL — schema, state machine, save/compute flows, library UI shape.
- Edit-invalidates-compute mechanism with partitioned `content_json` (compute-inputs vs metadata).
- REPLACE-on-recompute semantics for `computed_links` + immutable per-compute snapshot history for report reproducibility.
- Concurrent-edit-during-execution policy (block at UI level).
- `.sc` / `.vdf` import/export as operator-triggered actions; DB-of-truth for runtime.
- Interaction with DB Purge ([B1.14](../design-backlog.md)) and EW Library ([B1.7](../design-backlog.md)).

**Out of scope (deferred or covered elsewhere):**
- **Cross-deployment portability** — v2 is single-site delivery. Operators do not move scenarios between deployment instances. A `.sc` / `.vdf` export is for inspection in STK Desktop / Insight on the same workstation, not for cross-site sharing.
- **Reports schema** — `reports` table sketched here for context but full schema lives with the reports B1.x item.
- **Mode B (browser-side) authoring** — when activated, Mode B's `Sg.Server` consumes the same `scenarios` table via REST; the table's contract is mode-agnostic. Mode-B-specific concerns (CZML emission, WebSocket fan-out for live updates) are in the Mode B spec.
- **Full per-save version history** — operator confirmed pre-recompute snapshots are sufficient; arbitrary edit-by-edit undo is out of scope.

## 4. Architecture

### 4.1 Data model

Three new tables on WS2 PostgreSQL (regular tables, not hypertables, except where noted) + a foreign-key from the existing `computed_links` hypertable:

```sql
-- Library of all scenarios known to this deployment.
CREATE TABLE scenarios (
    scenario_id              uuid        PRIMARY KEY,
    name                     text        NOT NULL,
    owner                    text        NOT NULL,
    content_json             jsonb       NOT NULL,
        -- Partitioned shape:
        --   { "compute_inputs": { entities, emitters, antennas, environment, time_window },
        --     "metadata":       { description, tags, operator_notes, ... } }
    compute_inputs_hash      text        NOT NULL,
        -- sha256(content_json->'compute_inputs') — drives edit-invalidation diff
    compute_state            text        NOT NULL
        CHECK (compute_state IN ('NOT_COMPUTED', 'COMPUTED', 'STALE')),
    last_computed_at         timestamptz NULL,
    last_computed_snapshot   uuid        NULL REFERENCES scenario_compute_snapshots(snapshot_id),
    created_at               timestamptz NOT NULL DEFAULT now(),
    created_by               text        NOT NULL,
    updated_at               timestamptz NOT NULL DEFAULT now(),
    updated_by               text        NOT NULL,
    archived_at              timestamptz NULL  -- soft delete; row preserved for report provenance
);

CREATE INDEX scenarios_owner_idx ON scenarios (owner) WHERE archived_at IS NULL;
CREATE INDEX scenarios_compute_state_idx ON scenarios (compute_state) WHERE archived_at IS NULL;

-- Immutable history: one row per successful compute. Reports reference this for provenance.
CREATE TABLE scenario_compute_snapshots (
    snapshot_id              uuid        PRIMARY KEY,
    scenario_id              uuid        NOT NULL REFERENCES scenarios(scenario_id),
    computed_at              timestamptz NOT NULL,
    computed_by              text        NOT NULL,
    content_json             jsonb       NOT NULL,  -- exact scenario state at compute time
    compute_inputs_hash      text        NOT NULL   -- matches scenarios.compute_inputs_hash at compute
);

CREATE INDEX scenario_compute_snapshots_scenario_idx
    ON scenario_compute_snapshots (scenario_id, computed_at DESC);
```

Existing `computed_links` hypertable gains a foreign key:

```sql
ALTER TABLE computed_links
    ADD COLUMN scenario_id uuid NOT NULL REFERENCES scenarios(scenario_id);
```

`computed_links` is REPLACE-on-recompute: rows are DELETEd for the scenario at recompute, then INSERTed fresh. No `snapshot_id` foreign key — link rows reflect only the current compute. Report durability is achieved by the `reports` table baking the link data it needs (see §4.6).

### 4.2 State machine

```
                  ┌────────────┐
       Create ───►│NOT_COMPUTED│◄────── (initial state on INSERT)
                  └─────┬──────┘
                        │ Compute (success)
                        ▼
                  ┌────────────┐
       ┌──────────│ COMPUTED   │──────────┐
       │ Recompute└─────┬──────┘ Edit     │
       │ (success)      │ (metadata only) │ Edit
       │                │ — no transition │ (compute_inputs change)
       │                │                 ▼
       │                │            ┌────────┐
       │                │            │ STALE  │
       │                │            └────┬───┘
       │                │                 │ Recompute (success)
       └────────────────┴─────────────────┘
```

**Notes:**
- Edits that only change `metadata` fields (name, description, tags, operator notes) UPDATE the row and bump `updated_at` but do NOT transition state.
- Edits that change `compute_inputs` fields (entities, emitters, antennas, environment, time window) DELETE the scenario's `computed_links` rows AND flip to STALE.
- A failed compute leaves the scenario in its prior state (STALE if it was STALE, NOT_COMPUTED if first-time) plus an orphaned snapshot row in `scenario_compute_snapshots` — see §4.5.
- Archive is a soft-delete state (`archived_at IS NOT NULL`); the row is preserved indefinitely so reports that reference its snapshots remain interpretable. Archived scenarios are hidden from the library UI list by default; visible via "Show archived" filter.

### 4.3 Save flow

```mermaid
sequenceDiagram
    actor Op as 👤 SG Operator
    participant Sg.App
    participant Backend as StkScenarioBackend
    participant DB as scenarios (PostgreSQL on WS2)

    Op->>Sg.App: File → Save (or autosave trigger)
    Sg.App->>Backend: Serialise current DTO graph → content_json
    Sg.App->>Sg.App: hash_new = sha256(content_json.compute_inputs)
    Sg.App->>DB: BEGIN TRANSACTION
    Sg.App->>DB: SELECT compute_inputs_hash FROM scenarios<br/>WHERE scenario_id=X FOR UPDATE
    DB-->>Sg.App: hash_old
    alt hash_new != hash_old (compute-inputs changed)
        Sg.App->>DB: DELETE FROM computed_links WHERE scenario_id=X
        Sg.App->>DB: UPDATE scenarios SET content_json=..., compute_inputs_hash=hash_new,<br/>compute_state='STALE', updated_at=now(), updated_by=Op
    else hash unchanged (metadata-only edit, or no-op)
        Sg.App->>DB: UPDATE scenarios SET content_json=..., updated_at=now(), updated_by=Op
        Note over Sg.App,DB: compute_state untouched; computed_links untouched
    end
    Sg.App->>DB: COMMIT
    Sg.App-->>Op: Toast "Scenario saved"
```

**Hot path note:** save is on operator action (or autosave at editor-defined cadence), so the cross-LAN PostgreSQL round-trip is acceptable — this is not a per-frame path.

**Concurrent-edit-during-execution:** if `scenario_id` is the currently-executing exercise (tracked by Sg.App's `IExerciseStateService`), the Save button is disabled at the UI level. Operator must Stop the exercise first. This is enforced client-side; the DB does not need to know about execution state.

### 4.4 Compute / recompute flow

Compute is split into three transactions so the cross-LAN write lock is never held during the multi-minute STK compute loop:

```mermaid
sequenceDiagram
    actor Op as 👤 SG Operator
    participant Sg.App
    participant Backend as StkScenarioBackend
    participant STK as STK 12 Engine
    participant DB as TimescaleDB (WS2)

    Op->>Sg.App: Compute → Compute Links
    Sg.App->>Backend: ComputeLinks(scenario_id)

    rect rgba(200,255,200,0.3)
        Note over Sg.App,DB: T1 — pre-compute (fast)
        Sg.App->>DB: BEGIN
        Sg.App->>DB: INSERT INTO scenario_compute_snapshots<br/>(snapshot_id=new uuid, scenario_id, computed_at=now,<br/>computed_by=Op, content_json=current, compute_inputs_hash)
        Sg.App->>DB: COMMIT
    end

    Backend->>STK: BeginUpdate + walk scenario
    loop For each tick T in [start, stop]
        Backend->>STK: per-tick compute (range, AER, signal strength)
        Backend->>Backend: Buffer batch (100 ticks)
        alt Buffer full
            Backend->>DB: BEGIN; INSERT INTO computed_links (tagged with scenario_id); COMMIT
        end
    end
    Backend->>STK: EndUpdate
    Backend->>DB: BEGIN; flush final batch; COMMIT

    rect rgba(200,255,200,0.3)
        Note over Sg.App,DB: T3 — post-compute (fast)
        Sg.App->>DB: BEGIN
        Sg.App->>DB: UPDATE scenarios SET compute_state='COMPUTED',<br/>last_computed_at=now, last_computed_snapshot=snapshot_id<br/>WHERE scenario_id=X
        Sg.App->>DB: COMMIT
    end
    Sg.App-->>Op: "Compute complete — exercise ready to execute"
```

**Failure recovery:**

| Failure point | State after failure | Recovery |
|---|---|---|
| T1 (snapshot insert) | scenario unchanged | Operator clicks Compute again |
| STK compute loop (mid) | scenario unchanged; orphaned snapshot row; partial `computed_links` rows present | Operator clicks Compute again — the save flow's hash-diff path will DELETE the partial rows; a fresh snapshot row is inserted; T3 updates `last_computed_snapshot` to the new one. Orphan snapshot remains in history (harmless; can be GC'd by retention if desired). |
| T3 (final state flip) | scenario_state stays prior; new snapshot exists; `computed_links` rows exist | Idempotent: operator clicks Compute again, the hash-diff fires DELETE+INSERT on `computed_links`, and T3 re-runs to flip to COMPUTED. |

The deliberate non-atomicity between snapshot creation and the final state flip is the trade-off for *not* holding a multi-minute write lock. The recovery model handles every failure case — at worst, the operator re-clicks Compute.

### 4.5 Recompute = save + compute

There is no separate "recompute" operation. To recompute a scenario, the operator triggers Compute. The save flow's hash-diff path DELETEs the prior `computed_links` (if state was COMPUTED and the operator edited compute-inputs) and the compute flow above writes the fresh data.

This is the REPLACE semantics from B1.19, preserved unchanged: the hot-path scenario publisher query stays simple — no version disambiguation, no `snapshot_id` filter, just `WHERE scenario_id=X AND group_id=... AND unit_id=... AND tick=T`.

### 4.6 Report durability (sketch)

Full reports design is a separate B1.x item; sketching the interface here so the boundary is clear:

```sql
CREATE TABLE reports (
    report_id        uuid        PRIMARY KEY,
    scenario_id      uuid        NOT NULL REFERENCES scenarios(scenario_id),
    snapshot_id      uuid        NOT NULL REFERENCES scenario_compute_snapshots(snapshot_id),
    generated_at     timestamptz NOT NULL,
    generated_by     text        NOT NULL,
    pdf_path         text        NOT NULL,
    baked_link_data  jsonb       NULL  -- materialised subset of computed_links for re-rendering / drill-through
);
```

A report references a specific `snapshot_id` for scenario-state provenance. It also bakes (in `baked_link_data` or in the rendered PDF itself) all the link data it needs — so even after the underlying `computed_links` rows are REPLACEd by a later recompute, the report remains valid and self-contained.

### 4.7 Library UI in Sg.App

The library is a tab/panel in Sg.App. Columns:

| Column | Source | Notes |
|---|---|---|
| Name | `scenarios.name` | clickable to open |
| State | `scenarios.compute_state` | badge: green = COMPUTED, amber = STALE, grey = NOT_COMPUTED |
| Owner | `scenarios.owner` | |
| Last computed | `scenarios.last_computed_at` | hyphen if NULL |
| Last edited | `scenarios.updated_at` | |
| Edited by | `scenarios.updated_by` | |

Operations:

- **Open** — load the scenario into STK via `IScenarioBackend.LoadFromJson(content_json)`; this pushes DTOs into STK programmatically (no `.sc` file involved).
- **New** — INSERT empty `scenarios` row with `compute_state='NOT_COMPUTED'`.
- **Duplicate** — INSERT new row with copied `content_json` + fresh `scenario_id`; `compute_state='NOT_COMPUTED'`.
- **Delete** — soft delete (set `archived_at`); confirmation dialog. Hard delete reserved for DB Purge ([B1.14](../design-backlog.md)).
- **Archive / Unarchive** — toggles `archived_at`. Archived scenarios hidden by default; "Show archived" filter reveals.
- **Export `.sc` / `.vdf`** — operator-triggered. Sg.App pushes the scenario into STK (if not already loaded), then calls `Root.SaveAs(...)`. The `.sc` file is a write-out artifact; not retained on disk by EWTSS.
- **Import `.sc` / `.vdf`** — operator selects a file → Sg.App loads it via STK COM → walks the in-memory scenario → serialises to DTO graph → INSERTs into `scenarios` with `compute_state='NOT_COMPUTED'`.

Filter + sort: by Name, State, Owner, Last computed. Bulk operations: archive, delete-confirmed.

### 4.8 EW Library cross-reference

Scenarios reference EW Library templates (emitters, antennas, etc.) by template ID. The `content_json.compute_inputs.emitters` array stores `{ template_id, instance_overrides: {...} }` pairs. EW Library schema is owned by [B1.7](../design-backlog.md); this spec assumes the template ID is a stable foreign-key target.

If an EW Library template is edited after a scenario referencing it has been computed, the scenario's `compute_inputs_hash` does NOT change (the template_id reference is unchanged). The scenario stays in COMPUTED state even though its effective inputs have drifted. Detecting this requires either:

- (a) Inlining the template body into the scenario at compute time (so the snapshot is self-contained — *recommended*).
- (b) Tracking template versions and re-hashing on template edits (more complex; deferred).

Recommendation: option (a) — compute-time template inlining means the `content_json` stored in `scenario_compute_snapshots` carries the EW Library template values that were live at compute time. Tracked as an explicit cross-link to B1.7 in the open items below.

### 4.9 DB Purge interaction

Per [B1.14](../design-backlog.md), the DB Purge admin action clears all scenarios + all `scenario_compute_snapshots` + all `computed_links` + EW Library + reports + telemetry. Cascade ordering:

1. Reports first (FK → snapshots).
2. `computed_links` (FK → scenarios).
3. `scenario_compute_snapshots` (FK → scenarios).
4. `scenarios`.
5. EW Library + telemetry tables.

Foreign-key constraints prevent partial purge — if any cascade fails, the whole operation rolls back. Confirmation UX in [B1.14](../design-backlog.md).

## 5. Error handling

Beyond the per-transaction failure modes in §4.4:

- **PostgreSQL connection lost mid-save**: client-side retry with exponential backoff; transaction either committed or rolled back fully — no partial save.
- **PostgreSQL connection lost mid-compute**: per-batch INSERT inside the compute loop fails; Sg.App raises a compute-aborted event; operator clicks Compute again. The hash-diff DELETEs partial rows.
- **STK crash mid-compute**: drs-server's compute supervisor logs the failure; same recovery as PostgreSQL mid-compute.
- **Concurrent recompute attempts**: prevented at UI level (the Compute button is disabled while a compute is in progress). DB-level: if a second compute were to start, both T1 transactions would insert distinct snapshot rows; both compute loops would race on `computed_links` INSERTs. The first to reach T3 wins. Not a designed state — the UI guard is the real defence.

## 6. Testing

| Test layer | What's covered | Where |
|---|---|---|
| Unit (Sg.App) | hash-diff invalidation on save; partitioned content_json shape; metadata-only edits don't transition state | `sg-app/Sg.App.Tests/ViewModels/ScenarioLibraryViewModelTests.cs` (new) |
| Unit (drs-server schema) | migration applies cleanly; FKs enforce cascade | `drs-server/tests/schema/test_scenarios_schema.py` (new) |
| Integration (with STK) | round-trip: DTO → STK → DTO → save → reload | extends `mvp4/Sg.Mvp4.Tests/Integration/StkBackendFixture.cs` pattern in sg-app |
| Integration (with PostgreSQL) | save → compute → snapshot inserted → final state flip → next edit DELETEs links + flips STALE | `drs-server/tests/integration/test_scenario_lifecycle.py` (new) |
| Manual | library UI list, filter, sort, archive, import/export | covered by [Operator Playbook §5](../operator-playbook.md) walkthroughs once UI lands |

## 7. RFQ alignment

| RFQ ref | Requirement | This spec |
|---|---|---|
| A.1 §1.1 | "Scenario generation / storage / playback" | §4.1 scenarios table + §4.4 compute + §4.7 library UI cover generation + storage. Playback = Scenario-mode execution per [command-flows §3.2](../command-flows.md). |
| A.1 §1.3 | "Scenarios can be created, modified, deleted, saved" | §4.7 library operations: New / Open / Save / Delete (soft). |
| §B | "Exercise planning: modify and save" | §4.3 save flow + §4.5 recompute = save + compute. |
| §G | "DB Purge" | §4.9 cascade ordering; full UX in B1.14. |

## 8. Open items + dependencies

- **EW Library template inlining at compute** (§4.8) — cross-link to [B1.7](../design-backlog.md); recommended approach (a) above. B1.7 owner should confirm.
- **Reports schema** (§4.6) — full design with the reports B1.x item; this spec sketches the interface only.
- **Autosave cadence** — operator-action save is the baseline; autosave is a UX choice (every N minutes? on idle? on entity-add commit?). Not blocking; can be added without schema change.
- **Mode B authoring** — when Mode B activates, `Sg.Server` consumes the same tables. Test the contract against a Mode B integration test in the Mode B Phase 1 plan.

## 9. References

- [B1.19 design backlog entry](../design-backlog.md) — original sketch; now points at this spec
- [command-flows §2.4 + §2.6](../command-flows.md) — save + compute sequences (unchanged by this spec, but live alongside it)
- [architecture-overview §4.4 + §4.5](../architecture-overview.md) — scenario save + compute paths
- [Developer Handbook §10.1](../developer-handbook.md) — existing `computed_links` schema
- [B1.7 EW Library](../design-backlog.md) — template foreign-key target
- [B1.14 DB Purge](../design-backlog.md) — cascade ordering
- RFQ Annexure A.1 §1.1, §1.3, §B, §G
