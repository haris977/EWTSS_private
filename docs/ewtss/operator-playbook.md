# EWTSS v2 — Operator Playbook

**Audience:** SG Operators, DRS Engineers, customer reviewers (acceptance), design-review participants, training material authors.
**Purpose:** describe what operators *do* with EWTSS v2 at workflow level — anchored on the RFQ scope (Annexure A.1 + A.2), with the legacy v1 system used only as a high-level reference for established user behaviour. Implementation-level gestures (specific clicks, keystrokes) are deliberately out of scope.
**Read time:** ~30 minutes.
**Source authority:** RFQ Annexure A.1 (Scope of Development) is canonical. Where the current v2 design has gaps vs the RFQ, this document describes the operator's intended workflow and flags the gap with 📐 for resolution in Milestone-1 SRS / Wireframe work.

---

## Status markers used throughout

- ✅ **Shipped** — works in MVP4.5 today on `Sg.App`.
- 🚧 **Planned (v2 hardening)** — in scope for the 17-week build per [v2 Execution Plan](v2-execution-plan.md); detailed component ownership is assigned.
- 📐 **Design gap** — required by RFQ; no v2 design spec exists yet. Resolves during Milestone-1 SRS + Mock UI Wireframes.
- 🔌 **External boundary** — workflow ends at an integration with a client-owned or third-party system; v2 exposes an API but does not own the consumer.

---

## 1. Personas and deployment scope

EWTSS v2 has **two operator personas inside the v2 development scope** plus **one external integration boundary**:

| Persona | Workstation | Primary surface | What they do |
|---|---|---|---|
| **SG Operator** (Scenario Operator) | WS1 — Scenario Generator | `Sg.App` C# WPF + STK ActiveX (Mode A, today) — future opt-in: `Sg.Web` Angular SPA (Mode B) | Authors scenarios, manages the EW library, plans and executes exercises, generates reports, administers users and logs. |
| **DRS Engineer** | WS2 — DRS (Device Replacement Software) | DRS webapp served by `drs-server` (browser-based; Angular preferred per [v2 Execution Plan §3.7](v2-execution-plan.md#37-g-angular-preferred-else-react--drs-webapp-lead), with React as acceptable fallback; final framework call at end of week 1 of build) 🚧 | Monitors hardware health and per-variant live data, configures hardware IP / network parameters, reviews sent/received message logs, performs DRS-side diagnostics. |
| **Control Center (CC)** 🔌 | Client-owned workstation, on the LAN | Client-owned application | Out of v2 development scope. EWTSS v2 exposes integration APIs from SG and/or DRS for CC to consume; the specific API surface is defined once CC integration requirements are available. |

Entity Controller Applications (RDFS, JV-UHF, JHF, SJRR, JLB, JMB, JHB-variants, AUS, PADS) sit on the LAN as separate workstations and are likewise external — DRS sends and receives IRS-compliant messages over TCP/UDP to them in Integrated mode.

```
            ┌─────────────────┐
            │  Control Center │ 🔌 client-owned, out of dev scope
            │   Application   │    (v2 exposes integration APIs TBD)
            └────────┬────────┘
                     │ LAN
   ┌─────────────────┼─────────────────┐
   │                 │                 │
   ▼                 ▼                 ▼
┌────────┐    ┌─────────────┐    ┌──────────────────────┐
│  WS1   │    │   WS2       │    │ Entity Controllers   │ 🔌
│   SG   │◄──►│   DRS       │◄──►│ JHF · JVUHF · RDFS · │ client-owned
│Sg.App  │    │ Webapp +    │    │ RSEC × N · SJRR ·    │ in Integrated
│  +STK  │    │ drs-bridge +│    │ PADS · AUS · …       │ mode
│        │    │ drs-server +│    │                      │
│        │    │ Kafka + DB  │    │                      │
└────────┘    └─────────────┘    └──────────────────────┘
```

---

## 2. System overview as operators see it

A complete operational cycle, narrated at workflow level:

1. **SG operator logs in** at WS1 and inspects the time-sync status (SG is the Time Server for the deployment).
2. **SG operator manages the EW library** — adds or updates emitter definitions and the apriori own/enemy info base — before authoring a new scenario, or reuses an existing library.
3. **SG operator authors a scenario** on the GIS map: defines the gaming area, AOP, FEBA, vulnerable areas; places Blue Line (friendly) and Red Line (enemy) platforms; configures emitter parameters and time-dynamic motion.
4. **SG operator runs scenario computation** — STK time-dynamic link analysis produces range, azimuth, elevation, Doppler, signal strength, signal offsets, and channel attenuation for every (sensor, emitter) pair across the exercise time window. Results are persisted.
5. **SG operator selects exercise mode** — Random vs Scenario, Standalone vs Integrated — and starts execution. In Scenario mode the computed link-analysis is replayed tick-by-tick; in Random mode synthetic data is generated from operator-configured parameter ranges.
6. **DRS Engineer at WS2** opens the DRS webapp and monitors hardware health, per-variant live data feeds, and message rates. In Integrated mode, DRS sends IRS-compliant messages over LAN to Entity Controller Applications and processes their responses.
7. **SG operator monitors the exercise live** on the GIS globe — Blue/Red Line platforms animate, emitter coverage colours the map per FF/FH/Burst type, telemetry flows from DRS through Kafka into the operator's display.
8. **Operator controls playback** as the exercise proceeds: start, pause, resume, stop, replay.
9. **At exercise end, SG operator generates a report** (template-based PDF) and reviews recordings.
10. **Admin tasks** — user management, log filtering / sort, DB backup / restore / purge — happen out of band, gated by RBAC.

---

## 3. Pre-exercise setup

### 3.1 Login and authorization

| Step | Operator action | RFQ reference | v2 surface | Status |
|---|---|---|---|---|
| 1 | SG Operator launches `Sg.App` on WS1 and logs in. | A.1 §F User Mgmt & Access Rights | Sg.App login dialog → RBAC tables in TimescaleDB | 🚧 Planned (v2 hardening; RBAC tables designed) |
| 2 | DRS Engineer opens the DRS webapp on WS2 and logs in (separate credentials and role from SG Operator). | A.1 §F | drs-server FastAPI auth → DRS webapp | 🚧 Planned (G weeks 3–5) |
| 3 | System enforces feature-level access on every operator action (e.g. "modify scenario" vs "view-only"). | A.1 §F | RBAC middleware on every mutating endpoint | 🚧 Planned |

### 3.2 Time synchronization

| Step | Operator action | RFQ reference | v2 surface | Status |
|---|---|---|---|---|
| 1 | SG Operator verifies SG is acting as Time Server for the deployment. | A.1 §F Time Synchronization | Sg.App banner host (clear when sync is healthy) + Admin → Time Sync view; drs-server `/time/status` REST aggregates state. | ✅ Designed ([B1.3](design-backlog.md#-b13--time-synchronization-service-design-sg-as-time-server)) |
| 2 | Time skew across the deployment is monitored; operator gets a warning if any subsystem drifts beyond tolerance. | implied by A.1 §F | Sg.App banner surfaces `drift_warn` / `drift_alert`; `sync_lost` auto-pauses a running exercise. | ✅ Designed ([B1.3](design-backlog.md#-b13--time-synchronization-service-design-sg-as-time-server)) |

### 3.3 Database readiness

| Step | Operator action | RFQ reference | v2 surface | Status |
|---|---|---|---|---|
| 1 | Operator confirms TimescaleDB is reachable and the latest schema is in place. | A.1 §G | drs-server `/health/db` endpoint surfaced in both Sg.App and the DRS webapp | 🚧 Planned |
| 2 | Operator optionally restores a previous database backup. | A.1 §G DB Backup & Restore | Sg.App admin → DB management view | 🚧 Planned |

---

## 4. EW Library management

The EW Library is the reusable catalogue the operator builds across scenarios. Per RFQ §C, it has two parts: the Emitter Library, and the Apriori Known Own/Enemy info base.

| Step | Operator action | RFQ reference | v2 surface | Status |
|---|---|---|---|---|
| 1 | Operator opens the Emitter Library view in Sg.App. | A.1 §C, A.1 §B Templates | Sg.App library panel | 📐 Design gap (CRUD on emitter records; backing store designed) |
| 2 | Operator creates, modifies, or deletes emitter entries with parameters (frequency, power, antenna pattern, scan pattern, etc.). | A.1 §2.3 Antenna & Scan Patterns | Library editor with typed parameter fields | 📐 Design gap |
| 3 | Operator saves an emitter as a **template** for reuse across scenarios. Templates are versioned. | A.1 §B Templates | Template storage + version tag | 📐 Design gap |
| 4 | Operator updates the Apriori Known Own/Enemy info base (e.g. expected operating frequency ranges per known threat class). | A.1 §C | Apriori library view | 📐 Design gap |
| 5 | Library entries are visible from the scenario authoring view for drag-and-drop or selection placement. | A.1 §B | Scenario authoring → emitter selector | 📐 Design gap |

---

## 5. Scenario authoring (SG operator)

### 5.1 Defining the scenario context

| Step | Operator action | RFQ reference | v2 surface | Status |
|---|---|---|---|---|
| 1 | Operator opens a fresh scenario (New) or selects an existing one from the **Scenario Library** (see §5.4). Importing from a `.sc` / `.vdf` file is also supported as an interoperability action with STK Desktop / Insight. | A.1 §B Exercise Planning | Sg.App Library panel (Open / New / Import). Library is DB-backed per [Scenario Management spec](specs/scenario-management-design.md). | ✅ Designed |
| 2 | Operator defines the **gaming area** (geographic region for the exercise). | A.1 §B AOP, A.1 §E.GIS | GIS view in Sg.App; polygon authoring on the globe | 🚧 Planned (extension of MVP4.5 AreaTarget) |
| 3 | Operator marks the **Area of Operation (AOP)**, **FEBA** (Forward Edge of Battle Area), and **vulnerable areas / locations**. | A.1 §B | Distinct map-layer types in Sg.App | 📐 Design gap |
| 4 | Operator sets exercise start / stop time and animation time scale. | A.1 §2.1 | Sg.App scenario properties panel | ✅ Shipped (start / stop times — MVP4.5) |

### 5.2 Blue Line (friendly) and Red Line (enemy) platforms

| Step | Operator action | RFQ reference | v2 surface | Status |
|---|---|---|---|---|
| 1 | Operator deploys friendly **sensor entities** (RDFS, JV/UHF, JHF, SJRR, etc.) on the map as Blue Line platforms — fixed or mobile, ground-borne or airborne. | A.1 §B Deployment of Entities, A.1 §2.1 Blue Line | Scenario tree + GIS placement; entity type per RFQ catalogue | 🚧 Planned (vocabulary mapping: Blue Line → friendly typed entities) |
| 2 | Operator deploys **emitters** (Red Line and Blue Line) on the map with time-dynamic motion profiles — position (lat/lon/alt), speed, activity over time. | A.1 §B Emitter location & movement, A.1 §2.1 | Scenario tree + waypoint authoring; templates from EW Library | 🚧 Planned (extension of MVP4.5 Aircraft / route authoring to Emitter type) |
| 3 | Operator configures emitter parameters per the EW Library template (frequency, power, antenna pattern, scan pattern). | A.1 §1.2, A.1 §2.3 | Property panel per entity | 🚧 Planned |
| 4 | Operator visualises the scenario in 2D and 3D, panning/zooming on the GIS map. | A.1 §H GUI, A.1 §E.GIS | STK 2D + 3D ActiveX globes in Sg.App | ✅ Shipped |
| 5 | Operator saves the scenario to the Library (DB-backed). Editing fields that affect link analysis flips the scenario to STALE and triggers DELETE of prior `computed_links` rows on save; cosmetic edits (name, description, notes) leave compute state intact. Operator can also **Export** as `.sc` / `.vdf` (optionally password-protected) for STK Desktop / Insight inspection — export does not modify the library row. | A.1 §1.3 | Sg.App File → Save (DB) + Library → Export. See [Scenario Management spec §4.3](specs/scenario-management-design.md#43-save-flow). | ✅ Designed |

### 5.3 Environment modelling

| Step | Operator action | RFQ reference | v2 surface | Status |
|---|---|---|---|---|
| 1 | Operator configures atmospheric conditions for the exercise: precipitation (rain, snow), fog, other atmospheric. | A.1 §2.2 | Scenario properties → Environment tab | 📐 Design gap |
| 2 | These factors feed the link-budget calculation in Scenario Computation (§7). | A.1 §2.2 | Pass-through to STK environment model | 📐 Design gap |

### 5.4 Scenario library and lifecycle

Scenarios accumulate over the system's lifecycle. The library is the operator-facing list of all scenarios known to the deployment, with state-aware badges so the operator can tell at a glance which scenarios are ready to execute and which need recompute. Full design in [Scenario Management spec](specs/scenario-management-design.md).

**The three compute states**, visible as badges on every library row:

| Badge | Meaning | Can execute? |
|---|---|---|
| 🟢 **COMPUTED** | Last save matched the last successful compute. `computed_links` table holds the current dataset. | Yes — scenario can be selected for Scenario-mode execution (§8). |
| 🟠 **STALE** | Scenario was edited since the last successful compute. Prior `computed_links` rows have been DELETEd. | No — operator must recompute (just click Compute) before execution. |
| ⚪ **NOT_COMPUTED** | Scenario was just created (New), imported (`.sc`/`.vdf`), or duplicated. Has never been computed. | No — operator must compute first. |

**Lifecycle operations** (all in the Library panel; see [command-flows §2.7](command-flows.md#27-open-scenario-from-library) for the sequences):

| Step | Operator action | RFQ reference | v2 surface | Status |
|---|---|---|---|---|
| 1 | Operator opens the Library; sees the list with columns Name, State (badge), Owner, Last edited, Last computed. Default filter hides archived scenarios. | A.1 §1.1, §1.3 | Sg.App Library panel | ✅ Designed |
| 2 | Operator filters / sorts (by Owner, State, Last edited) and searches by name. | A.1 §1.3 | Library filter + sort controls | ✅ Designed |
| 3 | Operator opens a scenario by clicking its row. Sg.App loads `content_json` from the DB and pushes the DTO graph into STK programmatically (no `.sc` file involved). | A.1 §1.3 | Library → Open | ✅ Designed |
| 4 | Operator duplicates a scenario — new row created with copied content, fresh ID, NOT_COMPUTED state. | A.1 §1.3 | Library → Duplicate | ✅ Designed |
| 5 | Operator archives a scenario (soft delete). Row preserved indefinitely so historical reports remain interpretable; hidden from default library list. | A.1 §1.3 | Library → Archive (right-click menu, with confirmation) | ✅ Designed |
| 6 | Operator unarchives a scenario by switching the library filter to **Show archived** and selecting Unarchive. | A.1 §1.3 | Library → filter + Unarchive | ✅ Designed |
| 7 | Concurrent edit during execution is blocked: if a scenario is the currently-executing exercise, its Save button is disabled in the editor. Operator must Stop the exercise before editing. | A.1 §B Exercise Planning safety | UI guard on Sg.App's `IExerciseStateService` | ✅ Designed |
| 8 | Hard delete (physical row removal) is reserved for the admin **DB Purge** action ([§11.4](#114-database-management) / B1.14) — never available from the library UI. This preserves snapshot references for reports. | A.1 §G | Admin only | ✅ Designed |

**Cross-deployment**: out of scope for v2. EWTSS v2 is single-site delivery. The `.sc`/`.vdf` export is for STK Desktop inspection or backup, not for moving scenarios between v2 deployments.

**Audit trail**: every successful compute writes an **immutable snapshot** of the scenario state as-computed (in the `scenario_compute_snapshots` table). Reports reference these snapshots for provenance — so a report run six months ago still names exactly which scenario state produced it, even if the scenario has been edited and recomputed multiple times since.

---

## 6. GIS toolset

The GIS is the foundation of scenario authoring. RFQ §E enumerates the required tools; STK ActiveX (Mode A) and Cesium (Mode B future) both natively support most. The list below is the **operator-facing tool inventory** — what the operator can do on the map.

| Tool | RFQ reference | v2 surface | Status |
|---|---|---|---|
| **Map Management** — load Raster (JPEG, TIFF, GeoTIFF), Vector (Shape File, KML), 3D Data (DTED dt0/dt1/dt2, DEM) | A.1 §E Map Management | Sg.App GIS data manager → STK map layer registration | 📐 Design gap (STK supports the formats; operator-facing import dialog and layer manager UI need spec) |
| **Symbol Library** — list of in-app symbols | A.1 §E Symbol Library | GIS symbol palette | 📐 Design gap |
| **Color Coding** — distinct colours for FF / FH / Burst emitter types | A.1 §E Color Coding | Emitter rendering style table | 📐 Design gap |
| **Layer Management** — enable / disable each map layer | A.1 §E Layer Management | Layer-list side panel | 📐 Design gap |
| **Distance Measurement** — point-to-point | A.1 §E Distance Measurement | GIS measurement tool | 📐 Design gap |
| **Coordinate Conversion** — DMS ↔ Decimal Degree; lat/lon ↔ IGRS | A.1 §E Coordinate Conversion | Coord conversion utility (toolbar + property entry) | 📐 Design gap |
| **Zoom / Pan** | A.1 §E Zoom/Pan | STK native pan/zoom | ✅ Shipped |
| **Line of Sight** — visibility between two locations | A.1 §E Line of Sight | STK access-analysis tool surfaced as an operator action | 🚧 Planned |
| **Area Coverage** — area visible from an observation point within a circular radius | A.1 §E Area Coverage | STK CoverageDefinition + FOM | ✅ Shipped (computation); 🚧 operator UI for ad-hoc query |
| **RLOS** — Radio Line of Sight using emitter power, receiver params, antenna characteristics, propagation model | A.1 §E RLOS | STK link analysis surfaced for ad-hoc query (parameters: Tx power, Rx params, Tx/Rx antenna characteristics, frequency-dependent propagation) | 🚧 Planned |
| **Clutter data insertion** — operator can insert latest clutter dataset | A.1 §E RLOS clutter | Map data manager → clutter dataset import | 📐 Design gap |

---

## 7. Scenario Computation (link analysis)

This is the bridge between authoring and execution: the operator triggers STK link analysis, which produces the time-dynamic dataset that drives Scenario-mode exercise execution. The compute state of every scenario is visible in the Library as a state badge (NOT_COMPUTED / COMPUTED / STALE — see §5.4); Compute is the operator action that transitions a scenario from NOT_COMPUTED or STALE to COMPUTED.

| Step | Operator action | RFQ reference | v2 surface | Status |
|---|---|---|---|---|
| 1 | Operator selects a scenario in the Library and triggers **Compute**. The scenario must be in NOT_COMPUTED or STALE state (COMPUTED scenarios show Compute as a no-op unless edited). | A.1 §2.4, A.1 §1.8 | Sg.App Library → Compute | ✅ Designed |
| 2 | STK runs time-dynamic link analysis per tick (default 1 s) for every (sensor, emitter) pair, considering: antenna pattern + scan pattern + FoV; propagation effects (attenuation); obstructions (terrain, buildings); atmospheric effects (rain, fog); relative position vectors; sensor parameters (Tx power, frequency). | A.1 §2.4 | StkScenarioBackend.ComputeLinks() | 🚧 Planned |
| 3 | Compute produces per-tick records: **Range** (delay), **Azimuth**, **Elevation**, **Doppler** (relative velocity), **Signal strength**, **Signal Offsets** (phase, time, amplitude across channels), **Channel attenuation**. | A.1 §2.4 | `computed_links` hypertable on TimescaleDB | 🚧 Planned |
| 4 | Operator monitors compute progress (tick rate, percent complete, ETA). | A.1 §H GUI | Sg.App compute progress dialog | 📐 Design gap |
| 5 | Compute writes results in three transactions per spec §4.4: T1 records the about-to-be-computed scenario state in an **immutable snapshot** (`scenario_compute_snapshots`); batched INSERTs into `computed_links` happen inside the compute loop; T3 flips the scenario state to COMPUTED. The library badge updates to 🟢. | implied | Cross-LAN PostgreSQL writes from Sg.App. See [command-flows §2.6](command-flows.md#26-compute-link-analysis-scenario-modes). | ✅ Designed |
| 6 | Operator can re-run compute after editing the scenario. There is no separate "Recompute" — the operator just clicks Compute. The save flow (§5.2) DELETEs prior `computed_links` rows on the edit that flipped the scenario to STALE; Compute writes fresh data. REPLACE semantics; no incremental compute. | implied | Sg.App Library → Compute (idempotent). See [Scenario Management spec §4.5](specs/scenario-management-design.md#45-recompute--save--compute). | ✅ Designed |
| 7 | If compute fails mid-loop (STK crash, LAN cut), scenario stays in its prior state; an orphaned snapshot row + partial `computed_links` rows may exist. Operator simply re-clicks Compute — the system is idempotent at every failure point (see [command-flows §2.6](command-flows.md#26-compute-link-analysis-scenario-modes) failure recovery matrix). | implied | Idempotent retry | ✅ Designed |
| 8 | Reports generated from a compute reference its **snapshot_id** for provenance + bake their own link data so they survive past the next recompute. | A.1 §1.5 Reports | reports table (sketched in spec §4.6; full design with the reports B1.x item) | 📐 Design sketched |

---

## 8. Exercise Execution

EWTSS supports **four mode combinations** (per RFQ §A and v2 design):

| Mode | Data source | Entity Controller Apps active? | STK required at runtime? |
|---|---|---|---|
| Standalone + Random | C++ random data generator inside DRS | No | No |
| Standalone + Scenario | `computed_links` time-series | No | Pre-compute only |
| Integrated + Random | C++ random data generator | Yes — DRS↔CC integration active | No |
| Integrated + Scenario | `computed_links` time-series | Yes — DRS↔CC integration active | Pre-compute only |

### 8.1 Exercise setup

| Step | Operator action | RFQ reference | v2 surface | Status |
|---|---|---|---|---|
| 1 | Operator selects the exercise (saved scenario or live Random configuration). | A.1 §B Exercise Planning | Sg.App exercise selector | 🚧 Planned |
| 2 | Operator chooses **Random** or **Scenario** mode. | A.1 §A | Mode toggle in exercise control panel | 🚧 Planned |
| 3 | Operator chooses **Standalone** or **Integrated**. | A.1 §1 (Standalone vs Integrated) | Mode toggle | 🚧 Planned |
| 4 | If Random: operator configures per-variant parameter ranges (frequency min/max, power range, etc.). | A.1 §A Random Mode | Random configuration view per variant | 📐 Design gap |
| 5 | If Scenario: operator picks the computed exercise; status must be `ready`. | A.1 §A Simulation through scenarios | Computed-exercise picker | 🚧 Planned |
| 6 | If Integrated: operator confirms Entity Controller Applications are reachable on the LAN (DRS surfaces this via health panel). | A.1 §1.6 | DRS webapp health view (G weeks 9–11) + Sg.App readiness check | 🚧 Planned |
| 7 | Operator delivers mission guidelines to CC for the exercise duration. 🔌 | A.1 §B Mission guidelines to CC | API exposed by SG for CC consumption — specific surface TBD per CC integration requirements | 🔌 External boundary |

### 8.2 Exercise control (playback)

| Step | Operator action | RFQ reference | v2 surface | Status |
|---|---|---|---|---|
| 1 | Operator presses **Start** — exercise begins; scenario clock advances. | A.1 §B Facility to control playback | Sg.App exercise control bar | 🚧 Planned |
| 2 | Operator can **Pause / Resume / Stop**. | A.1 §B | Same control bar | 🚧 Planned |
| 3 | Operator can **Rewind / Replay** a completed or paused exercise. | A.1 §B | Recording playback view | 🚧 Planned |
| 4 | Operator can **introduce new emitters mid-planning** (before execution or during a paused exercise). | A.1 §B | Scenario edit-mid-pause flow | 📐 Design gap |
| 5 | During execution, operator sees live GIS animation: Blue Line / Red Line platforms move, emitter coverage colours the map, sensor footprints animate. | A.1 §H GUI | STK 2D + 3D globes with FOM grid colouring | ✅ Shipped (compute + globe colouring); 🚧 live animation panel for telemetry-driven entities |
| 6 | Telemetry from DRS hardware (FF / FH / Burst / Health rows) flows in via WebSocket and renders in dedicated telemetry panels alongside the globe. | A.1 §B Display of parameters with filters, search, sort | Sg.App telemetry panels per data class | 🚧 Planned |
| 7 | Operator can filter, search, and sort telemetry data live. | A.1 §B | Telemetry panel filter / sort UI | 📐 Design gap |

---

## 9. DRS Engineer workflows (WS2 webapp)

The DRS Engineer's surface is a **browser-based webapp on WS2**, served by drs-server. Framework: Angular preferred per [v2 Execution Plan §3.7](v2-execution-plan.md#37-g-angular-preferred-else-react--drs-webapp-lead) (React as acceptable fallback if dictated by skill alignment); final call at end of week 1 of the build. Owner: G. The persona's responsibility is hardware-side health, configuration, and diagnostics — distinct from the SG Operator's scenario-authoring role.

| Step | Operator action | RFQ reference | v2 surface | Status |
|---|---|---|---|---|
| 1 | DRS Engineer logs in to the DRS webapp on WS2. | A.1 §F | Auth via drs-server (JWT shared with the SG-side auth) | 🚧 Planned (G weeks 3–5) |
| 2 | Engineer sees a **dashboard** of hardware variants in scope with health-status indicators per variant. | A.1 §B Health status display of LRUs, sub-systems and communication links | DRS webapp landing view | 🚧 Planned (G weeks 3–5) |
| 3 | Engineer drills into a specific variant (RDFS, JV-UHF, JHF, SJRR, JLB, JMB, JHB-types, AUS, PADS) for live monitor-scan view. | A.1 §B | Per-variant monitor-scan view | 🚧 Planned (G weeks 6–8 first 4–6 variants, weeks 12–13 variants 7–9, weeks 14–15 variants 10–12) |
| 4 | Engineer reviews **LRU and sub-system health** (per RFQ: LRU health, sub-system health, communication-link health). | A.1 §B Health status | Health panel per variant | 🚧 Planned (G weeks 9–11) |
| 5 | Engineer monitors **message rates** — sent and received messages over IRS for the active variant. | A.1 §1.6 | drs-server `/health/<variant>/messages` view consumed by webapp | 🚧 Planned (G weeks 6–8) |
| 6 | Engineer configures **hardware IP / network parameters** when a new device is wired in or a network change is needed. | implied by RFQ deployment model | DRS webapp IP-config panel; persisted in DRS configuration store (A surfaces the backend endpoints) | 🚧 Planned (G weeks 9–11) |
| 7 | Engineer reviews **Sent Message Log** and **Receive Message Log** for the variant under inspection, with filtering and sorting. | A.1 §F Manage Logs | Log viewer with filter / sort | 🚧 Planned (G weeks 9–11) |
| 8 | Engineer can restart a per-variant TCP server / parser if a diagnostic intervention is needed. | implied | DRS webapp control panel with confirmation gates on disruptive actions | 🚧 Planned (G weeks 12–13) |
| 9 | Engineer monitors Kafka consumer health (per-variant consumer-group lag) at a glance. | implied by A.1 §1.7 robustness | drs-server `/health/consumers` view, surfaced in webapp | 🚧 Planned |

Detailed UX (specific widget layouts, navigation patterns, accessibility primitives) is part of Milestone-1 Mock UI Wireframes — see [v2 Execution Plan §3.7 / §4.1](v2-execution-plan.md#37-g-angular-preferred-else-react--drs-webapp-lead).

---

## 10. Reports

| Step | Operator action | RFQ reference | v2 surface | Status |
|---|---|---|---|---|
| 1 | Operator opens the **Reports** view post-exercise. | A.1 §D Report Generation | Sg.App reports panel | 🚧 Planned |
| 2 | Operator selects a **report template** (per-exercise summary, access-interval table, post-exercise report, etc.). | A.1 §D Template-based | Template picker | 📐 Design gap (template catalogue not yet defined) |
| 3 | Report engine queries TimescaleDB for the relevant exercise data and renders a **PDF**. | A.1 §D | drs-server / Sg.App PDF generation (WeasyPrint backend on the server) | 🚧 Planned |
| 4 | Operator can export the PDF to local storage or DVD. | A.1 §D, A.1 §G | File save dialog | 🚧 Planned |
| 5 | Operator can review **recordings** (full exercise replay metadata + telemetry). | implied by A.1 §B playback | Recordings view in Sg.App | 📐 Design gap |

---

## 11. Administration

### 11.1 User management & access rights

| Step | Operator action | RFQ reference | v2 surface | Status |
|---|---|---|---|---|
| 1 | Admin opens **User Management** in Sg.App. | A.1 §F User Mgmt & Access Rights | Admin view; gated by RBAC | 🚧 Planned |
| 2 | Admin creates / modifies / deletes users, assigns roles, configures features and permissions per role. | A.1 §F | RBAC CRUD UI | 🚧 Planned |
| 3 | Admin manages DRS Engineer accounts separately (DRS webapp credentials). | A.1 §F | RBAC store shared across SG and DRS surfaces | 📐 Design gap (shared store designed; UI separation TBD) |

### 11.2 Log management

| Step | Operator action | RFQ reference | v2 surface | Status |
|---|---|---|---|---|
| 1 | Operator opens **Log Management** in Sg.App. | A.1 §F Manage Logs | Admin → Logs view | 🚧 Planned |
| 2 | Operator views the four required log classes: **User log**, **Sent Message log**, **Receive Message log**, **System log**. | A.1 §F (four log classes enumerated) | Log viewer with class selector | 📐 Design gap |
| 3 | Operator filters and sorts within each log class. | A.1 §F | Filter / sort controls | 📐 Design gap |
| 4 | Operator exports filtered logs (CSV or PDF). | implied | Export dialog | 📐 Design gap |

### 11.3 Time synchronization administration

| Step | Operator action | RFQ reference | v2 surface | Status |
|---|---|---|---|---|
| 1 | Admin verifies SG is the **Time Server** for the deployment. | A.1 §F Time Synchronization | Sg.App Admin → Time Sync view (offset / jitter / peer / last sync / status); backed by drs-server `/time/status`. | ✅ Designed ([B1.3](design-backlog.md#-b13--time-synchronization-service-design-sg-as-time-server)) |
| 2 | Admin monitors per-subsystem time skew. | A.1 §F | Same view (per-variant rows added once Phase 6 DRS webapp ships); Kafka `system.timesync` carries the events. | ✅ Designed ([B1.3](design-backlog.md#-b13--time-synchronization-service-design-sg-as-time-server)) |

### 11.4 Database management

| Step | Operator action | RFQ reference | v2 surface | Status |
|---|---|---|---|---|
| 1 | Admin **backs up** the database to a secondary storage medium (CD/DVD). | A.1 §G DB Backup & Restore | Admin → DB management view; `pg_dump` to chosen target | 🚧 Planned |
| 2 | Admin **restores** the database from a backup. | A.1 §G | Restore flow with confirmation gate | 🚧 Planned |
| 3 | Admin **imports** / **exports** the entire database. | A.1 §G | Import / export pipeline | 🚧 Planned |
| 4 | Admin **purges** the entire database (with strong confirmation). | A.1 §G DB Purge | DB Purge action with multi-step confirmation | 📐 Design gap |

---

## 12. External integrations (out of v2 development scope)

These boundaries are not part of v2 build but are exposed for external consumers.

### 12.1 Control Center (CC) integration 🔌

CC is a client-owned application that the v2 system integrates with. v2 exposes integration APIs from SG and/or DRS for CC to consume; the specific API surface (REST endpoints, message-bus topics, payload schemas) will be defined once CC integration requirements are available.

Workflow touchpoints where the CC boundary is anticipated:

| Touchpoint | Description | API surface |
|---|---|---|
| Mission guidelines delivery | SG operator sends mission guidelines to CC for the exercise duration. | SG → CC: REST API TBD per CC integration spec |
| Exercise lifecycle events | SG operator's start / pause / stop actions may need to notify CC. | SG → CC: REST or message bus TBD |
| Health and status push | Aggregate health from DRS + SG may be surfaced to CC. | DRS → CC: REST or WebSocket TBD |
| CC-initiated commands | CC may need to query exercise state or request specific data. | CC → SG / DRS: REST API TBD |

### 12.2 Entity Controller Application integration 🔌

Entity Controller Applications (RDFS, JV-UHF, JHF, SJRR, JLB, JMB, JHB-variants, AUS, PADS) are client-owned and reachable on the LAN. v2's DRS integrates with them in Integrated mode:

| Direction | Mechanism | RFQ reference |
|---|---|---|
| DRS → Entity Controller | TCP/UDP binary per the IRS for each variant | A.1 §1.6, A.1 §2.5 |
| Entity Controller → DRS | TCP/UDP responses per IRS | A.1 §1.6 |

---

## 13. v1 → v2 enhancements

The v1 system has been used operationally; v2 is an upgrade. Operators bring their existing mental model; v2 should preserve workflow shape and add capability — not redesign workflows from scratch.

| Area | v1 behaviour | v2 enhancement |
|---|---|---|
| Telemetry throughput | Degrades after ~10 min of sustained load; ceiling ~200 msg/s | Sustained 2,000 msg/s, indefinite duration, no degradation (structural fixes per [Legacy System Audit](legacy-system-audit.md)) |
| Concurrent DRS instances | ~10 (thread-per-client + Python GIL) | 100+ (asyncio, single event loop) |
| Hardware variant addition | ~8 files touched per variant; duplicated code | 1 YAML profile + 1 C++ parser library (no Python changes per variant); ICD codegen tool accelerates onboarding |
| Frontend rendering quality | OpenLayers 2D map | STK ActiveX 3D + 2D globes (Mode A); CesiumJS 3D (Mode B future) |
| STK integration | Out-of-process via Python `agi.stk12` HTTP-style backend | In-process COM (Mode A) — microsecond method-call latency, full STK API fidelity |
| Storage | MySQL with full-table scans on growth | PostgreSQL 16 + TimescaleDB 2.x — hypertable chunk exclusion + composite indexes from day one |
| Frontend separation | Single Angular SPA covering both scenario operator + DRS engineer workflows behind two logins | Two surfaces by persona: WS1 desktop app for SG operator; WS2 webapp for DRS engineer (matching the v1 user mental model: scenario operator vs DRS engineer) |
| Reports | Implicit / per-variant | Template-based PDF generation framework (RFQ §D) |

---

## 14. RFQ → v2 traceability matrix

A consolidated index of every numbered RFQ requirement in Annexure A.1 mapped to where it is satisfied in v2. This becomes the spine of the SRS (Milestone-1 deliverable #1).

| RFQ reference | Requirement | v2 satisfaction | Playbook section | Status |
|---|---|---|---|---|
| A.1 §1.1 | Scenario generation, storage, playback | Sg.App scenario authoring + STK + recordings | §5, §8 | 🚧 |
| A.1 §1.2 | Emitter parameter types for COM, RADAR, SJRR, AUS, PADS | Typed entities + EW Library templates | §4, §5.2 | 🚧 |
| A.1 §1.3 | Scenarios + emitters can be created / modified / deleted / saved | `.sc` and `.vdf` save / load | §5.1, §5.2 | ✅ |
| A.1 §1.4 | DRS for subsystems interfacing with sensor applications | drs-bridge + per-variant C++ parser libs | §9 | 🚧 |
| A.1 §1.5 | DRS unified across all sensors | One generic supervisor + YAML profile per variant | §9 | 🚧 |
| A.1 §1.6 | DRS sends/receives per IRS; SG interfaces with entity apps via DRS | TCP servers per variant; scenario publisher endpoint from SG | §8, §12.2 | 🚧 |
| A.1 §1.7 | Robustness under full load and out-of-bound input | Asyncio + supervised consumers + batched writes; load-test gates at Phase 7 (2,000 msg/s for 30 min) | (cross-cutting) | 🚧 |
| A.1 §1.8 | Ansys STK for path profiling + link analysis (composite coverage, propagation, RLOS) | StkScenarioBackend + ComputeLinks | §6, §7 | ✅ / 🚧 |
| A.1 §A Random Mode | Synthetic emitter data within parameter ranges | drs-bridge RandomGenerator + per-variant param ranges | §8.1 | 🚧 |
| A.1 §A Scenario Mode | Battlefield scenario with planned deployment | Scenario authoring + Compute + Scenario-mode playback | §5–§8 | 🚧 |
| A.1 §B Display of parameters with filter/search/sort | Live telemetry display | Sg.App telemetry panels | §8.2 | 🚧 / 📐 |
| A.1 §B Health status display | LRU / sub-system / comm link health | DRS webapp health view (G owns) | §9 | 🚧 |
| A.1 §B Exercise Planning (AOP, FEBA, Vulnerable areas) | Map-layer authoring | Scenario authoring | §5.1 | 📐 |
| A.1 §B Emitter location + movement | Time-dynamic motion profiles | Scenario authoring + STK Great Arc propagator | §5.2 | 🚧 |
| A.1 §B Exercise execution + mission guidelines to CC | Exercise control bar + CC integration API | Exercise execution | §8, §12.1 | 🚧 / 🔌 |
| A.1 §B New emitters during planning | Mid-planning edits | Scenario edit | §8.2 | 📐 |
| A.1 §B Playback control | Start / pause / resume / stop / rewind / replay | Exercise control bar | §8.2 | 🚧 |
| A.1 §B Templates for emitters | Library template CRUD + versioning | EW Library | §4 | 📐 |
| A.1 §C Emitter Library + Apriori info base | EW Library | EW Library views | §4 | 📐 |
| A.1 §D Report Generation | Template-based PDF | Reports panel + WeasyPrint | §10 | 🚧 |
| A.1 §E Map Management (Raster / Vector / 3D formats) | STK / Cesium native loading | GIS data manager | §6 | 📐 |
| A.1 §E Symbol Library | Standard symbol palette | GIS symbols | §6 | 📐 |
| A.1 §E Color Coding (FF / FH / Burst) | Emitter style table | GIS rendering | §6 | 📐 |
| A.1 §E Layer Management | Enable / disable layers | Layer panel | §6 | 📐 |
| A.1 §E Distance Measurement | Point-to-point | GIS tool | §6 | 📐 |
| A.1 §E Coordinate Conversion (lat/long ↔ IGRS, DMS ↔ Decimal) | Conversion utility | GIS tool | §6 | 📐 |
| A.1 §E Zoom / Pan | STK pan/zoom | GIS | §6 | ✅ |
| A.1 §E Line of Sight | STK access analysis | GIS tool | §6 | 🚧 |
| A.1 §E Area Coverage | STK CoverageDefinition + FOM | GIS / compute | §6 | ✅ / 🚧 |
| A.1 §E RLOS | STK link analysis with propagation model | GIS tool | §6 | 🚧 |
| A.1 §E Clutter data | Clutter dataset import | GIS data manager | §6 | 📐 |
| A.1 §F User Management & Access Rights | RBAC tables + middleware | Admin view | §3.1, §11.1 | 🚧 |
| A.1 §F Manage Logs (User / Sent Msg / Receive Msg / System) | Four-class log viewer with filter / sort | Admin → Logs | §11.2 | 📐 |
| A.1 §F Time Synchronization (SG = Time Server) | Meinberg NTP + drs-server `SyncStateEngine` + Sg.App banner/admin view | Admin → Time | §3.2, §11.3 | ✅ |
| A.1 §G DB Backup & Restore | Backup / restore to CD/DVD | DB Management view | §3.3, §11.4 | 🚧 |
| A.1 §G DB Purge | Purge with multi-step confirmation | DB Management view | §11.4 | 📐 |
| A.1 §H GUI | User-friendly, state-of-the-art UI | Sg.App + DRS webapp | (cross-cutting) | (cross-cutting) |
| A.1 §2.1 Blue Line / Red Line platforms | Friendly + enemy entity typing | Scenario authoring | §5.2 | 🚧 |
| A.1 §2.2 Environment Modelling (precipitation, fog) | STK environment configuration | Scenario environment tab | §5.3 | 📐 |
| A.1 §2.3 Antenna + Scan Patterns (raster, sector) | STK antenna / scan modelling | EW Library + per-entity panels | §4, §5.2 | 🚧 |
| A.1 §2.4 Scenario Computation (Range / Az / El / Doppler / Signal strength / Offsets / Channel attenuation) | StkScenarioBackend.ComputeLinks | Compute panel | §7 | 🚧 |
| A.1 §2.5 DRS Application emulates subsystem interfaces per IRS | drs-bridge supervisor + parsers + ResponseRouter | DRS engineer surfaces | §9 | 🚧 |
| A.2 Milestone 1 #1 SRS | Software Requirements Specification | (this doc is its spine) | — | 📐 |
| A.2 Milestone 1 #2 Software Architecture for SG | Architecture Overview doc | [architecture-overview.md](architecture-overview.md) | — | ✅ |
| A.2 Milestone 1 #3 Mock UI Wireframes for SG and DRS | Wireframe deliverable | — | — | 📐 |
| A.2 Milestone 1 #4 STK development license (1 year) | Procurement | — | — | (procurement) |
| A.2 Milestone 1 #5 STK Runtime Engine Deployment (Qty 2) | Procurement | — | — | (procurement) |
| A.2 Milestone 2 #1 Completed development + integration | v2 hardening phase (17 weeks) | [v2-execution-plan.md](v2-execution-plan.md) | — | 🚧 |
| A.2 Milestone 2 #2–4 ATP / STD / STR | Test artefacts | (acceptance test workstream) | — | 🚧 |
| A.2 Milestone 2 #5 User Manuals | Operator + DRS Engineer manuals | (this playbook is the substrate) | — | 📐 |
| A.2 Milestone 2 #6 Source code | Repository delivery | (this repo) | — | 🚧 |

---

## 15. Open design questions — tracked in the Design Backlog

The 📐 rows in the sections above and the design questions surfaced through this playbook are tracked as Milestone-1 backlog items in **[Design Backlog §1](design-backlog.md#1-milestone-1-design-items)**. The headline item is **B1.1 — Detailed UX wireframes** (RFQ Annexure A.2 Milestone 1 #3) covering both the SG-side `Sg.App` surfaces and the DRS webapp surfaces; the backlog also tracks B1.3 Time Synchronization, B1.4 CC integration API surface, B1.5 Map Management import UI, B1.6 GIS toolset UI, B1.7 EW Library UI, B1.8 Environment modelling, B1.9 Exercise mode configuration UIs, B1.10 Telemetry filter/sort, B1.11 Report templates, B1.12 Recordings replay, B1.13 4-class Log Management, B1.14 DB Purge confirmation, B1.15 Blue Line / Red Line vocabulary mapping, B1.16 RBAC role definitions.

The DRS webapp surfaces themselves (§9) are no longer open design questions — G owns them and they are scoped week-by-week in [v2 Execution Plan §3.7](v2-execution-plan.md#37-g-angular-preferred-else-react--drs-webapp-lead). What remains for those surfaces is detailed UX wireframes (B1.1).

---

## 16. References

- [Executive Brief](executive-brief.md) — 5-min summary.
- [Design Review Brief](design-review-brief.md) — 25-min design-review pre-read.
- [Architecture Overview](architecture-overview.md) — system architecture in 20-min depth.
- [Decision Record](decision-record.md) — 19 ADRs covering architectural commitments.
- [v2 Execution Plan](v2-execution-plan.md) — staffing, parallel workstreams, integration test gates.
- [Risk Register](risk-register.md) — active, retired, deferred risks.
- [Design Backlog](design-backlog.md) — Milestone-1 design items required before / alongside the v2 hardening phase (B1.1 detailed UX wireframes is the headline).
- [Deployment Guide](deployment-guide.md) — what runs where, hardware specs, licence handling.
- [v1 Legacy System Audit](legacy-system-audit.md) — anti-pattern catalogue + structural fixes.
