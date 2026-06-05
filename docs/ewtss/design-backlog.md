# EWTSS v2 — Design Backlog

**Audience:** project lead, architecture lead, engineering team, customer programme manager.
**Purpose:** the tracked backlog of design work items required to complete the v2 design before and alongside the v2 hardening phase. Items are split into **Milestone-1** (required before the 17-week build can fully cover the scope) and **Deferred** (post-acceptance / Mode B activation). Resolved items are retained in §3 for audit.
**Source authority:** RFQ Annexure A.1 (Scope of Development) and A.2 (List of Deliverables) define the requirement bar. Active v2 design docs ([architecture-overview.md](architecture-overview.md), [operator-playbook.md](operator-playbook.md), [decision-record.md](decision-record.md), [v2-execution-plan.md](v2-execution-plan.md)) define the architectural commitments.
**Cadence:** reviewed weekly during Milestone-1; at each phase gate during the v2 hardening phase; at customer-acceptance gate. Items are added, moved between sections, or retired by the project lead + architecture lead.

---

## How to read this backlog

| Marker | Meaning |
|---|---|
| 🎯 **B1.x — Milestone-1 design item** | Required before the v2 hardening phase covers this scope. Resolves during Milestone-1 SRS + Mock UI Wireframes work. |
| ⏳ **B2.x — Deferred design item** | Activates when a specific condition is met (Mode B funding, customer scope change, CC requirements arriving, etc.). |
| ✅ **B0.x — Resolved** | Was on the backlog; now complete. Kept for audit. |

Each row uses: ID, title, RFQ reference (where applicable), owner, blocking dependencies, target completion, status note.

---

## 1. Milestone-1 design items

These items must complete (signed off by project lead + architecture lead) before the corresponding scope in the v2 hardening phase can build to acceptance.

### 🎯 B1.1 — Detailed UX wireframes (SG operator and DRS engineer surfaces)

**RFQ reference:** Annexure A.2 Milestone 1 #3 — *Mock UI Wireframes for SG and DRS Applications*.
**Why headline:** explicit RFQ Milestone-1 deliverable. The wireframes are the substrate the build implements; without them, [B (SG-side WPF extensions)](v2-execution-plan.md#33-b-c-specialist--sgapp-extension) and [G (DRS webapp surfaces)](v2-execution-plan.md#37-g-angular-preferred-else-react--drs-webapp-lead) design-as-they-go and risk diverging from operator expectation.

**Scope — SG-side wireframes (`Sg.App` WPF in Mode A):**
- Login + main dashboard / landing
- Gaming area + AOP + FEBA + vulnerable-area authoring on the GIS map
- Scenario tree (Blue Line / Red Line platforms; emitters; sensors; coverage definitions; figures of merit)
- Entity placement and edit panels (per typed entity: Aircraft, Facility, AreaTarget, Sensor, CoverageDefinition, FigureOfMerit, Transmitter, Receiver, Antenna)
- Map view with all GIS tools (distance measurement, coordinate conversion DMS ↔ Decimal ↔ IGRS, Line of Sight, Area Coverage, RLOS, layer manager, symbol library, FF/FH/Burst colour coding)
- Map Management import dialog (Raster JPEG/TIFF/GeoTIFF; Vector Shape/KML; 3D DTED dt0/dt1/dt2 + DEM)
- EW Library (Emitter Library, Apriori Known Own/Enemy info base, emitter templates with CRUD and versioning)
- Environment configuration (precipitation, fog, atmospheric)
- Compute trigger + progress dialog
- Exercise control bar (Random / Scenario × Standalone / Integrated mode toggles; playback start / pause / resume / stop / replay)
- Random-mode parameter-range editor (per variant)
- Live telemetry panels (FF / FH / Burst / Health) with filter / search / sort
- Entity response log (Integrated mode)
- Reports panel + PDF template picker
- Recordings replay UI
- Admin: user / role / feature / permission management; 4-class log viewer (User / Sent Message / Receive Message / System) with filter / sort / export; time-sync admin; DB management (backup, restore, purge with multi-step confirmation)

**Scope — DRS-side wireframes (DRS webapp on WS2):**
- Login + dashboard (variants overview, health-status indicators)
- Per-variant monitor-scan with live message-rate
- Per-variant Sent Message Log + Receive Message Log (filter / sort)
- LRU + sub-system + communication-link health detail
- IP / network configuration per variant
- Per-variant control (restart TCP server, parser) with confirmation gates
- Kafka consumer health at-a-glance

**Owner:** project lead + architecture lead jointly drive; B contributes SG-side surfaces, G contributes DRS-side surfaces. Customer programme manager reviews for operator-fidelity to v1 expectations where applicable.
**Blocking dependencies:** none — can start immediately.
**Target:** Complete and signed off by **week −1** of the v2 hardening phase (i.e. one week before kick-off). Allow ~4–6 weeks of focused design work; do not start until B1.2 framework decision is in flight.
**Status:** Open. Workstream not yet started.

### 🎯 B1.2 — DRS webapp framework decision (Angular vs React)

**RFQ reference:** Annexure A.1 §1.4, §1.5, §2.5 — implied by the DRS application's user-facing surface requirements.
**Decision required:** Angular (preferred per [v2 Execution Plan §3.7](v2-execution-plan.md#37-g-angular-preferred-else-react--drs-webapp-lead) for skill alignment with E and Mode B future reuse) or React (acceptable fallback if dictated by G's primary skill).
**Owner:** project lead + architecture lead, with G's input on skill alignment.
**Blocking dependencies:** none — can decide immediately.
**Target:** Sign-off at **end of week 1** of v2 hardening phase (one week earlier than the original end-of-week-2 contracts gate, per the framework-decision-slip mitigation in [Execution Plan §7](v2-execution-plan.md#7-specialist-risks-per-person)).
**Status:** Open.

### 🎯 B1.3 — Time Synchronization service design (SG as Time Server)

**RFQ reference:** A.1 §F — *Time Synchronization. This feature facilitates to get the time from the Scenario generator designated as Time Server.*
**Scope:** protocol selection (NTP / PTP / custom); tolerance specification (max acceptable skew per consumer); operator-facing time-sync admin UI (admin view in Sg.App showing per-subsystem skew); time-source distribution to drs-bridge, drs-server, DRS webapp, and Entity Controllers via the integration API.
**Owner:** architecture lead + D (cross-stack lead) for implementation design.
**Blocking dependencies:** none.
**Target:** Designed and sign-off by **week 2** of v2 hardening (contracts gate); implementation can land later.
**Status:** **Designed + partially implemented.** Two-layer design pinned: internal (Meinberg NTP between SG and WS2, ≤10 ms target) + external (per-IRS adapter, deferred to per-variant spec). See:
- [Design spec](specs/time-sync-design.md)
- [Implementation plan](plans/time-sync-plan.md) (27 tasks across 8 phases)
- [Corrigenda](plans/time-sync-corrigenda.md) — 19 spec-validation findings; the corrigenda is the read for anyone picking up the open phases
- [drs-bridge runtime skeleton plan](plans/drs-bridge-runtime-skeleton.md) (6 tasks; shipped on main) — completes the Phase 4 service so drs-bridge is actually runnable, not just composed of testable pieces
- Install scripts: [`infrastructure/ntp/`](../../infrastructure/ntp/) (sg / ws2 install + smoke); see [Deployment Guide §4.5, §5.6, §5.7](deployment-guide.md#45-time-server-install--ws1-acts-as-stratum-1-b13)

Done end-to-end on `main` (originally on the spec-validation branch, merged + amended): Phase 1 install scripts (lab runs pending), Phase 2 `/time/status` endpoint, Phase 3 `SyncStateEngine` + Kafka publisher, Phase 4 drs-bridge YAML schema + TimeBeacon + TickLagDetector + Bridge lifecycle wiring + full runtime skeleton (config, profile loader, parser ctypes loader, transport, Runtime, signal-handled main), Phase 5 Sg.App banner host + gating + auto-pause, Phase 6 DRS webapp (Angular 18 scaffold + TimeSyncService + dashboard card + per-variant row + routed DashboardComponent with provideHttpClient). **Open phases:** 7 (load test — hardware-bound). Lab acceptance also needs the Meinberg MSI vendored + the install/smoke scripts run on hardware (see [B1.20](#-b120--meinberg-ntp-vendoring--phase-1-lab-acceptance-b13-follow-through)).

### 🎯 B1.4 — CC integration API surface

**RFQ reference:** A.1 §B *mission guidelines to CC*, and the deployment topology in RFQ Figure 3.1 (CC as a third workstation).
**Decision required:** specific REST endpoints, message-bus topics, and payload schemas that v2 exposes to the client-owned CC application. Anticipated touchpoints (per [Operator Playbook §12.1](operator-playbook.md#121-control-center-cc-integration-)): mission-guidelines delivery (SG → CC), exercise-lifecycle events (SG → CC), aggregate health push (DRS → CC), CC-initiated commands (CC → SG / DRS).
**Owner:** architecture lead + project lead, in dialogue with the client's CC team.
**Blocking dependencies:** **CC integration requirements from the client.** This item cannot proceed without input.
**Target:** Open until the client provides CC integration requirements. Mitigation: define v2's *exposed* API surface (the ones v2 can decide unilaterally — e.g. exercise-lifecycle event topics + health push) by end of week 2 of v2 hardening, even if CC-side consumption details are TBD.
**Status:** Open. **Externally blocked.** Project lead to chase the client on CC integration requirements at the design review.

### 🎯 B1.5 — Map Management import UI

**RFQ reference:** A.1 §E *Map Management* — Raster (JPEG, TIFF, GeoTIFF), Vector (Shape File, KML), 3D Data (DTED dt0/dt1/dt2, DEM).
**Scope:** operator-facing import dialog; layer-registration into STK; failure-handling UX (corrupt file, unsupported variant); persistence of imported layers per scenario.
**Owner:** B (SG-side) with architecture-lead review.
**Target:** Wireframe in B1.1; design spec by week 4 of v2 hardening; implementation aligned with B's GIS tooling work.
**Status:** Open. STK natively supports the formats; the UI surface is unspec'd.

### 🎯 B1.6 — GIS toolset operator UI

**RFQ reference:** A.1 §E — Distance Measurement, Coordinate Conversion (DMS ↔ Decimal Degree, lat/lon ↔ IGRS), Line of Sight, Area Coverage, RLOS, Layer Management, Symbol Library, Color Coding (FF / FH / Burst), Clutter data import.
**Scope:** operator-invokable tools in Sg.App; on-globe interactions; result rendering; report-friendly output (LoS visibility line, Area Coverage circle, RLOS coverage polygon).
**Owner:** B (SG-side) with architecture-lead review.
**Target:** Wireframe in B1.1; design spec by week 5 of v2 hardening.
**Status:** Open. STK supports most natively; operator-facing tool inventory is unspec'd.

### 🎯 B1.7 — EW Library UI (Emitter Library + Apriori info base + templates)

**RFQ reference:** A.1 §C *EW Library*; A.1 §B *Templates for emitters*.
**Scope:** Emitter Library CRUD UI; Apriori Known Own/Enemy info base CRUD UI; emitter templates with versioning; library-to-scenario drag-and-drop or selection flow; library backup / restore as part of the scenario package.
**Owner:** B with input from operator playbook §4.
**Target:** Wireframe in B1.1; design spec by week 5; implementation week 6–9.
**Status:** Open.

### 🎯 B1.8 — Environment modelling configuration UI

**RFQ reference:** A.1 §2.2 *Environment Modelling* — precipitation, fog, atmospheric conditions feeding link-budget.
**Scope:** per-scenario environment settings; per-tick environment if time-varying; STK environment-model parameter mapping.
**Owner:** B with architecture-lead review.
**Target:** Wireframe in B1.1; design spec by week 6.
**Status:** Open.

### 🎯 B1.9 — Exercise mode configuration UIs

**RFQ reference:** A.1 §A Random + Scenario; A.1 §1 Standalone + Integrated.
**Scope:** Random-mode parameter-range editor per variant; Scenario-mode computed-exercise picker; Integrated-mode readiness panel (entity-controller reachability check); new-emitter-mid-planning flow (RFQ §B).
**Owner:** B (SG-side) + A (drs-bridge coordination).
**Target:** Wireframe in B1.1; design spec by week 7; implementation across weeks 6–11 per execution plan.
**Status:** Open.

### 🎯 B1.10 — Telemetry display panel filter / search / sort UI

**RFQ reference:** A.1 §B *Display of parameters of emitter data with filters, search and sort options.*
**Scope:** live filter / search / sort controls for the FF / FH / Burst / Health panels in Sg.App; persistence of filter preferences per operator; export-filtered-rows flow.
**Owner:** B.
**Target:** Wireframe in B1.1; design spec by week 9.
**Status:** Open.

### 🎯 B1.11 — Report template catalogue + PDF designs

**RFQ reference:** A.1 §D *Report Generation Framework: Generation of a template-based report in .pdf format.*
**Scope:** define the report-template catalogue (per-exercise summary, access-interval table, post-exercise report, mission summary, etc.); per-template field-mapping to TimescaleDB queries; WeasyPrint stylesheet design.
**Owner:** E (server-side render) + B (UI trigger) + project lead (template selection).
**Target:** Catalogue agreed by week 4; per-template designs by week 10.
**Status:** Open.

### 🎯 B1.12 — Recordings replay UI

**RFQ reference:** A.1 §B *Facility to control the playback of mission scenarios.*
**Scope:** post-exercise replay surface — timeline scrubber, per-event log, GIS replay, telemetry replay.
**Owner:** B.
**Target:** Wireframe in B1.1; design spec by week 11.
**Status:** Open.

### 🎯 B1.13 — Log Management UI (4-class)

**RFQ reference:** A.1 §F *Manage Logs — User log, Sent Message log, Receive Message log, and System log.*
**Scope:** SG-side admin view in Sg.App; 4-class log viewer with filter / sort / export; per-class retention controls; correlation between the SG-side log and the DRS-side per-variant logs (cross-link).
**Owner:** B (UI) + E (server endpoints).
**Target:** Wireframe in B1.1; implementation week 8–13.
**Status:** Open.

### 🎯 B1.14 — DB Purge confirmation flow

**RFQ reference:** A.1 §G *DB Purge — There shall be facility to clear entire database.*
**Scope:** multi-step destructive-action UX — pre-purge backup prompt, two-step confirmation, irreversible action warning; admin-only RBAC gating.
**Owner:** B.
**Target:** Wireframe in B1.1; design spec by week 12.
**Status:** Open.

### 🎯 B1.15 — Blue Line / Red Line vocabulary mapping to v2 entity types

**RFQ reference:** A.1 §2.1 *Blue Line and Red Line Platforms.*
**Decision required:** how the RFQ's Blue Line (friendly) and Red Line (enemy / emitter) vocabulary maps onto v2's STK-derived entity types (Aircraft / Facility / AreaTarget / Sensor / Coverage / FOM / Transmitter / Receiver / Antenna). Likely outcome: a `side` attribute on each entity (Blue / Red / Neutral) with consistent color coding across the GIS + scenario tree + reports.
**Owner:** architecture lead + B.
**Target:** Decided in [Decision Record](decision-record.md) by week 2 of v2 hardening; implementation across the entity-property panels.
**Status:** Open. Not currently in any v2 doc.

### 🎯 B1.16 — RBAC role definitions for SG Operator vs DRS Engineer

**RFQ reference:** A.1 §F *User Management & Access Rights.*
**Decision required:** specific role definitions (SG Operator role, DRS Engineer role, Admin role); per-role feature permission grants; whether SG and DRS share the RBAC store or each side has its own (current architecture: shared store on TimescaleDB).
**Owner:** E + architecture lead.
**Target:** Role catalogue by week 3 of v2 hardening; implementation week 4–8.
**Status:** Open. Backing tables designed; role / permission mappings TBD.

### 🎯 B1.17 — Authentication and session-management flows beyond first-login

**RFQ reference:** A.1 §F *User Management & Access Rights* (generic authentication / authorisation requirement). Concrete flows derive from standard secure-session expectations for defence systems.
**Why this item:** [Command Flows §1](command-flows.md#1-authentication) covers the first-login happy paths (SG Operator + DRS Engineer). The auth lifecycle has several other flows that the build will need but aren't yet sequence-diagrammed.

**Scope — flows to design and sequence-diagram:**

| Flow | What it covers | RFQ touchpoint |
|---|---|---|
| **Logout** | Operator-initiated and admin-forced logout; server-side token invalidation; UI return to login screen | A.1 §F |
| **JWT refresh / silent token rotation** | Short-lived access token + refresh token pattern; in-flight request handling during rotation; refresh-token compromise mitigation | A.1 §F |
| **Failed login retry / lockout** | Configurable retry count; lockout duration; audit log entry per failed attempt; admin unlock | A.1 §F + §F Manage Logs (User log) |
| **Password change** | Operator self-service; old-password challenge; password-strength policy; force-change-on-first-login flag | A.1 §F |
| **Password reset** | Admin-initiated reset (operator forgot password); air-gapped delivery of new credentials (in-band UI vs out-of-band channel — design decision) | A.1 §F |
| **Session timeout** | Idle-timeout vs absolute-timeout; behaviour when operator is mid-exercise (do not lock the operator out of a running exercise; warn and extend on activity) | A.1 §F + §B (exercise execution must not be silently interrupted) |
| **Re-authentication on destructive admin actions** | Generalised pattern (DB Purge already has this per B1.14; same pattern needed for user-disable, role-elevation, hot-fix install, etc.) | A.1 §F |
| **Token-revocation on role / disable change** | Existing JWTs immediately invalidated when a user is disabled or has their role downgraded; revocation list mechanism (token-blacklist table or short access-token TTL + refresh check) | A.1 §F |
| **Concurrent session policy** | Whether one operator can be logged in from multiple browsers / Sg.App instances simultaneously; if not, lockout-other-session UX | implied by A.1 §F + classroom training topology |

**Owner:** E (drs-server `/auth/*` endpoint design + DB schema for token-revocation list), B (Sg.App side — token caching, refresh interceptor, lockout UI), G (DRS webapp side — same concerns mirrored).
**Blocking dependencies:** [B1.16](#-b116--rbac-role-definitions-for-sg-operator-vs-drs-engineer) (role definitions must exist before lockout / revocation flows can be concrete); [B1.13](#-b113--log-management-ui-4-class) (auth events feed the User log class).
**Target:** Designed by week 4 of v2 hardening; implementation across weeks 5–9 (alongside the base RBAC + JWT work in [E's allocation §3.6](v2-execution-plan.md#36-e-angular-cross-trained--drs-server-rest--auth--reports)). Each flow gets a Mermaid sequence diagram added to [command-flows.md §1](command-flows.md#1-authentication).
**Status:** Open. Items currently noted in [command-flows.md §1](command-flows.md#1-authentication) as out-of-scope for the first cut.

### 🎯 B1.18 — Air-gap dependency vendoring process detail

**RFQ reference:** A.1 §1 (air-gapped LAN deployment); A.2 (DVD delivery with vendored dependencies).
**Why this item:** the [Developer Handbook §15.7](developer-handbook.md#157-third-party-licence-allow-list) already covers the licence-allow-list side of dep adoption. What's not yet documented is the **mechanical vendoring process** — where wheels land, how offline-install is verified, what the PR-time check looks like, how non-pure-Python wheels are handled, and how the DVD packaging step picks up the vendored artefacts. Today it works for the dependencies already chosen; the process for adding a new dep mid-build is ad-hoc.

**Scope:**
- Directory layout under `packages/` for Python wheels (`packages/wheels/`), C++ libs (`packages/c++/`), parser `.dll` artefacts (`packages/parsers/`), DRS webapp dist (`packages/webapp/`), and third-party-licence index (`packages/THIRD-PARTY-LICENCES.md`).
- Process for adding a new Python dep: download wheel + transitive deps via `pip download --no-deps --dest packages/wheels/<dep>/`; verify offline install via `pip install --no-index --find-links=packages/wheels/<dep>/`; commit the wheel(s) + update `THIRD-PARTY-LICENCES.md` row; CI checks both succeed.
- Handling of non-pure-Python wheels (C-extension wheels): Windows-x64-only target now (post-WS2-Windows decision); pin Python ABI tag (cp312-cp312-win_amd64); verify wheel imports cleanly.
- DRS webapp dependency vendoring (npm packages): `npm pack` per dep, store under `packages/npm/`, install via `npm install --offline` (or use `--prefer-offline` with a verdaccio mirror during build).
- C++ third-party libraries: vendor source + build artefacts; pin MSVC version; commit `.dll` to `packages/c++/`.
- DVD packaging script (WiX MSI installer per [v2 Execution Plan §3.5](v2-execution-plan.md#35-d-cross-stack-lead--architect-reviewer-integration-test-owner)): consumes `packages/` directly; install verification runs offline at the customer site.
- PR-time enforcement: any change to a `requirements.txt`, `pyproject.toml`, `package.json`, or `CMakeLists.txt` must touch `packages/` and `packages/THIRD-PARTY-LICENCES.md` together; CI fails the PR otherwise.

**Owner:** D (cross-stack lead + integration test rig owner) — natural fit since the air-gapped install rehearsal is D's deliverable.
**Blocking dependencies:** none. Builds on the existing [Developer Handbook §15.7](developer-handbook.md#157-third-party-licence-allow-list) framing.
**Target:** Process defined and documented in Developer Handbook §15.8 (new subsection) by **end of week 7** of v2 hardening — before D's week-9 air-gapped install rehearsal. Failure cost is acceptable up to week 8 (ad-hoc vendoring works for the deps adopted in weeks 1–6); after week 8 the process becomes load-bearing.
**Status:** Open. Not a week-0 blocker; tracked for week-7 closure.

### ✅ B1.19 — Scenario library lifecycle and `computed_links` invalidation

**Design spec:** [`specs/scenario-management-design.md`](specs/scenario-management-design.md) (elevated from this backlog entry 2026-05-22 after user-led brainstorm settled the four foundational decisions: DB-of-truth, pre-recompute snapshots + immutable history for report provenance, partitioned `content_json` compute-inputs vs metadata, REPLACE semantics on recompute).

**RFQ reference:** A.1 §1.1 (scenario generation / storage / playback), §1.3 (scenarios can be created, modified, deleted, saved), §B Exercise Planning (modify and save), §G DB Purge. Surfaced during B1.3 brainstorming on 2026-05-14 — the design has a clean *runtime* mechanism for drs-bridge to know the active scenario ([command-flows §3.2](command-flows.md#32-standalone--scenario-mode-execution): `drs.session.control { exerciseId=X }` over Kafka); this item closed the *library-side lifecycle* gap around that mechanism.

**Decisions settled** (see spec for full design):

- ✅ Scenario vs Exercise model — Model A: one library entry per scenario, one current `compute_state ∈ {NOT_COMPUTED, COMPUTED, STALE}`.
- ✅ Edit-invalidates-compute — partitioned `content_json` (compute-inputs vs metadata); sha256 diff on `compute_inputs` only; DELETE `computed_links` + flip STALE.
- ✅ Recompute semantics — REPLACE (DELETE then INSERT). Hot-path scenario publisher query stays simple.
- ✅ Audit trail — immutable `scenario_compute_snapshots` table holds one row per successful compute; reports reference snapshots for provenance + bake their own link data for self-containment.
- ✅ Concurrent edit during execution — blocked at UI level; operator must Stop the exercise.
- ✅ Library schema — `scenarios` (regular table on WS2 PostgreSQL) + `scenario_compute_snapshots` (immutable history) + FK from `computed_links`.
- ✅ Cross-deployment portability — explicitly out of scope (single-site delivery confirmed 2026-05-22).
- ✅ DB Purge cascade — ordering documented in spec §4.9; full UX deferred to [B1.14](#-b114--db-purge-confirmation-flow).

**Owner:** B (Sg.App library + edit-invalidation + compute pipeline) + F (DB schema for `scenarios` + `scenario_compute_snapshots` + FK constraint on `computed_links`).
**Blocking dependencies:** none for design (closed). Implementation depends on [B1.7](#-b17--ew-library-ui-emitter-library--apriori-info-base--templates) for the EW Library template inlining at compute (spec §4.8).
**Target:** ~~Design spec by end of week 4 of v2 hardening~~ → design spec landed 2026-05-22. Implementation: B builds the library UI + edit-invalidation flow in weeks 6–11; F adds the schema migration in week 1; D reviews the contract.
**Status:** **Closed (design).** Implementation tracked separately.

### 🔥 B1.20 — Meinberg NTP vendoring + Phase 1 lab acceptance (B1.3 follow-through)

**Priority:** **highest of all open backlog items** — gates lab acceptance of [B1.3](#-b13--time-synchronization-service-design-sg-as-time-server). All B1.3 design work and most of the implementation has landed on `feat/b1-3-time-sync`; what remains is human-driven and cannot be done by an agent.

**RFQ reference:** A.1 §F Time Synchronization (the runtime contract has been designed and partially implemented; this item closes the validation loop).

**Scope:**

1. **Vendor the Meinberg NTP MSI.** On an internet-connected workstation, follow [`packages/installers/meinberg-ntp/README.md`](../../packages/installers/meinberg-ntp/README.md) to download `ntp-4.2.8p18-win64-setup.exe` (or current stable), verify the publisher signature with `signtool verify /pa <file>`, compute the SHA-256, copy the binary to `packages/installers/meinberg-ntp/ntp-4.2.8p18-win-x64-setup.msi`, then update the README and [`packages/THIRD-PARTY-LICENCES.md`](../../packages/THIRD-PARTY-LICENCES.md) with the hash. Commit binary + docs together.

2. **Distribute to the air-gap mirror** per the established process so lab workstations install from the mirror, not the internet.

3. **Phase 1 install on lab hardware** — run `infrastructure/ntp/sg-ntp-install.ps1` on WS1 (Stratum-1) and `infrastructure/ntp/ws2-ntp-install.ps1 -SgHost <ws1-host>` on WS2 (client). Each script's own post-install check (`ntpq -p` showing the right peer) must pass.

4. **Phase 1 acceptance smoke** — run `infrastructure/ntp/ntp-smoke.ps1` on WS2; assert max offset under 10 ms across the 5-minute window. The script ends with `PASS` or `FAIL` and a written log; capture both.

5. **Record the outcome.** If PASS, B1.3 Phase 1 is accepted; close the lab-acceptance item in this backlog and add the captured offsets to the deployment evidence pack. If FAIL, follow [Deployment Guide §7.4](deployment-guide.md#74-troubleshooting-time-sync); a real failure here is a design-feedback signal (the spec's ≤10 ms tolerance may need revisiting, or the LAN topology may need NIC pinning / wired-only enforcement).

**Owner:** ops / lab lead (binary download + workstation runs); D / architecture lead reviews the captured offsets.
**Blocking dependencies:** physical access to a WS1 + WS2 pair on the lab LAN, plus internet-connected workstation for the one-time MSI download.
**Target:** as early as the lab pair is available. **This is the only blocker between "B1.3 Phase 1 designed + scripted" and "B1.3 Phase 1 lab-accepted."**
**Status:** Open. Blocks the B1.3 lab-acceptance gate; no agent-side dependencies remain on this item.

---

### Reconciled pre-mortem items (B1.21–B1.42)

The [Design Post-Mortem](design-postmortem.md) recommended adding a batch of gaps to this backlog as "B1.17–B1.29", but those IDs were subsequently reused for other work (B1.17 auth flows, B1.18 vendoring, B1.19 scenario lifecycle, B1.20 NTP), so the pre-mortem's items were never actually logged here — they existed as analysis, not as tracked work. This section closes that gap: each row below is a real, owned, dated item carrying a **fresh** ID (B1.21+) and a pointer to the pre-mortem section that details it. (Several pre-mortem items were *already* closed elsewhere and are **not** repeated here: branching → [strategy §7](specs/repository-and-release-strategy.md), code review → strategy §7.1, per-repo test convention + webapp/DB test gaps → strategy §6.1, STK dev-seat scope → strategy §10, cross-repo contracts home → [`contracts/`](../../contracts/).)

Severity: 🔴 blocks acceptance · 🟠 materially slows/degrades · 🟡 manageable.

| ID | Item | Sev | Source | Owner | Target / status |
|---|---|---|---|---|---|
| B1.21 | **SRS document** (RFQ M1 #1) — draft from the [operator-playbook §14 traceability matrix](operator-playbook.md) spine | 🔴 | PM §3.2 | project lead + arch lead | End of Milestone-1 (before week 0). Open. |
| B1.22 | **ATP / STD / STR drafts** (RFQ M2 #2) | 🔴 | PM §3.1 | project lead + D | ATP draft wk12, customer sign-off wk14; STD/STR wks14–15. Open. |
| B1.23 | **User manuals** (RFQ M2 #5) — from the operator playbook, trainee-facing | 🔴 | PM §4.3 | B + G + project lead | Draft wk12, final wk17. Open. |
| B1.24 | **IRS document store + versioning + IRS→parser traceability** | 🟠 | PM §2.2 | C + D | Process by wk1; per-variant IRS as they arrive from client. Open. |
| B1.25 | **Typed-entity DTO spec** (Transmitter / Receiver / Antenna fields, ranges, relationships) | 🟠 | PM §2.5 | B + arch lead | Week 0 (Milestone-1 SRS work) — build-blocker for B. Open. |
| B1.26 | **Cross-repo contract-artifact freeze** (OpenAPI + scenario/DTO shapes + Kafka schemas) | 🟠 | PM §2.1 | F (OpenAPI), B (DTO/scenario), D (Kafka) | Week 2. **Partly done:** [`contracts/`](../../contracts/) scaffolded 2026-05-31 — Kafka + scenario schemas baselined, OpenAPI implemented-slice generated. Freeze = OpenAPI completed for the planned surface + B1.25 landed + each consumer signs off. |
| B1.27 | **Security design** — threat model + data classification + auth lifecycle | 🟠 | PM §3.8, §4.12 | D / security consultant + E | **Doc landed 2026-05-31** ([specs/security-design.md](specs/security-design.md)); auth lifecycle flows ([B1.17](#-b117--authentication-and-session-management-flows-beyond-first-login)) wks1–4. |
| B1.28 | **Security review + penetration testing** (execution: pen test, RBAC audit, input fuzzing, SQLi check) | 🟠 | PM §3.8 | external/internal security | Weeks 13–15 (findings need remediation time). Open. |
| B1.29 | **Sg.App UI automation harness** (FlaUI / Appium — critical operator flows) | 🟠 | PM §3.3 | B | Week 8. Open. |
| B1.30 | **drs-webapp e2e + coverage gate** (Cypress/Playwright smoke + karma-coverage threshold) | 🟡 | PM §3.4 / strategy §6.1 | G | With B1.2 framework decision. Open. |
| B1.31 | **STK install diagnostic / repair tool** (`stk-doctor`) — env vars, COM regs, host present, licence reachable | 🔴 | PM §4.1 | D | Week 9 (before air-gapped install rehearsal). Open. |
| B1.32 | **DVD install procedure** — finalise the [deployment-guide §5](deployment-guide.md) placeholder + rehearse | 🔴 | PM §4.2 | D | Draft wk9, sign-off Phase 6. Open. |
| B1.33 | **Air-gapped maintenance / hotfix procedure** (patch packaging, customer-IT approval, rollback) | 🟠 | PM §4.4 | D + project lead | Maintenance handbook, wk16. Open. |
| B1.34 | **Telemetry retention policy + disk-full handling** (per-class retention; backpressure behaviour; operator alert) | 🟠 | PM §4.5 | F | Spec pre-deploy; impl by Phase 6. Open. |
| B1.35 | **STK lease-licence expiry handling + renewal** (30/7/1-day warnings; day-366 procedure) | 🟠 | PM §4.6 | B + ops | Startup check in Sg.App; renewal in deploy guide. Open. |
| B1.36 | **IRS revision change-management** (mid-warranty IRS revs; build-time + runtime IRS-version stamping) | 🟠 | PM §4.7 | C + D | Maintenance handbook. Open. |
| B1.37 | **WS1↔WS2 network-partition resilience** (reconnect/backoff; compute pause/resume; operator indicator) | 🟠 | PM §4.8 | B + F + A | Resilience tests at Phase 5. Open. |
| B1.38 | **Power-failure / unclean-shutdown recovery** (atomic compute batches; resume-or-discard on restart) | 🟡 | PM §4.9 | B + F | Phase 5/6. Open. |
| B1.39 | **Third-party licence catalogue + allow-list** (MIT/Apache/BSD allow; GPL/AGPL/LGPL block — IP transfer) | 🟠 | PM §5.2 | D | Auto-generated, reviewed at each milestone gate. Open. |
| B1.40 | **Knowledge-transfer / hand-off plan** post-acceptance (customer team trained for warranty + IP handover) | 🟠 | PM §5.3 | project lead | Drafted wk14; sit-with weeks 15–17. Open. |
| B1.41 | **Customer issue-reporting workflow** (air-gapped: structured form + redacted log export + vendor tracker) | 🟡 | PM §5.6 | D + project lead | Warranty start. Open. |
| B1.42 | **Customer clarifications bundle** — classroom topology (single vs multi-SG), audit-log integrity, localisation, accessibility | 🟡 | PM §4.11, §4.12, §5.4, §5.5 | project lead + arch lead | Raise at the design review. Open. |

Lower-severity pre-mortem process items (perf-test reproducibility §3.6, STK patch-drift automation §3.7, recordings-replay correctness test §3.9, disaster-recovery for catastrophic HW failure §4.13, doc localisation/accessibility §5.7) remain documented in the pre-mortem and are folded into the relevant phase-gate criteria rather than carried as separate backlog rows.

### ✅ B1.43 — DRS instance addressing & discovery (ECS ↔ WS2 in Integrated mode)

**Design spec:** [`specs/drs-instance-addressing-design.md`](specs/drs-instance-addressing-design.md) (new gap, elevated straight to spec via brainstorm 2026-06-04 — not from the pre-mortem batch).

**RFQ reference:** A.1 §1.6 (Integrated mode), §2.5 (hardware-variant message exchange), §B (Integrated-mode readiness). Surfaced 2026-06-04 when reviewing how Entity Controller apps learn which WS2 port to connect to and how the system guarantees the right DRS instance is behind it.

**Why this item:** the runtime skeleton binds **one TCP server per variant** ([`runtime.py`](../../drs-bridge/src/drs_bridge/runtime.py)), but the architecture commits to up to ~100 DRS instances with multiples per variant. There was no per-instance addressing, no discovery story for client-owned ECS apps, and no identity guarantee that the expected variant is behind a given port.

**Decisions settled** (see spec §4):
- ✅ Port authority — **mixed** (honour IRS-fixed ports, allocate where free).
- ✅ Addressing — **per-instance**, not per-variant; stable logical id `variant#n` is the shared vocabulary across WS1 scenarios / WS2 roster / logs / Kafka.
- ✅ Source of truth — WS2 **deployment-level roster** (variant profile stays a pure template). **Hybrid store:** live in WS2 PostgreSQL (`roster` / `roster_entry` + append-only `roster_revision` audit), edited at runtime via the DRS-webapp through drs-server (the RFQ "IP/network configuration per variant" surface); **YAML export/import** is the version-control + provisioning + offline-diff format. Active roster selectable via an `is_active` pointer so lab/bench/campaign configs swap without rewriting addressing — logical ids keep scenarios portable across rosters. (Reverses the initial file-only decision once runtime webapp editing was weighed in.)
- ✅ Binding — **author-time by logical id**; ports resolved only at launch.
- ✅ ECS discovery — **export-only** (WS2 publishes an addressing table; nothing required of client ECS beyond their IRS).
- ✅ Identity — deterministic port→instance binding + **framing probation** + launch-time `/exercise/readiness` reconcile. **Source-IP checking deliberately dropped** (YAGNI; §8 decision log).
- ✅ Sync — the launch reconcile is the single WS1↔WS2 sync moment; catalogue ETag surfaces author-time/runtime drift before the run.

**Owner:** A (drs-bridge per-instance transport + roster consumer/hot-reload) + E (drs-server roster store, API, export/import + reconcile) + B (Sg.App catalogue cache + readiness panel) + G (DRS-webapp IP/network-config editing surface) + F (DB schema for `roster` / `roster_entry` / `roster_revision`); D reviews the WS1↔WS2 contract.
**Blocking dependencies:** none for design (closed). Implementation interacts with [B1.9](#-b19--exercise-mode-configuration-uis) (readiness panel UX) and [B1.37](#reconciled-pre-mortem-items-b121b142) (mid-exercise partition resilience — separate item).
**Target:** design spec landed 2026-06-04. Implementation: per-instance servers + roster loader (A), roster endpoints + reconcile (E), catalogue/readiness (B) — schedule with the Integrated-mode execution work.
**Status:** **Closed (design); implementation in progress.** Plan 1 (contracts + drs-bridge) landed [`plans/drs-instance-addressing-bridge-plan.md`](plans/drs-instance-addressing-bridge-plan.md) and is **implemented** on branch `feat/b1-43-drs-instance-addressing` (drs.roster contract + compacted topic, per-instance servers, roster consumer, hot-reload, framing probation, per-instance health — full unit suite green; parser/integration tests CI-gated on cmake). Plan 2 (drs-server roster store + DB foundation + reconcile + publisher) and Plan 3 (Sg.App + DRS-webapp surfaces) pending.

## 2. Deferred design items (activate on condition)

### ⏳ B2.1 — Mode B (browser SG frontend) UX design

**RFQ reference:** N/A — Mode B is an architectural optionality v2 carries, not an RFQ Milestone-1 deliverable.
**Activates when:** customer signal warrants Mode B.
**Scope:** Angular + CesiumJS SPA wireframes; OpenAPI freeze; TypeScript view-model mirroring strategy.
**Owner:** TBD when activated.
**Status:** Deferred per [ADR-001](decision-record.md).

### ⏳ B2.2 — GIS tile pipeline (offline DTED + ctb-tile + Martin) for Mode B

**RFQ reference:** N/A — Mode B-specific.
**Activates when:** Mode B Phase 1 kicks off.
**Owner:** GIS specialist (~6 weeks part-time procurement).
**Status:** Deferred.

### ⏳ B2.3 — Multi-user authoring concurrency model

**Activates when:** customer requests multi-author or simultaneous-edit support.
**Status:** Out of v2 scope. Documented in [Operator Playbook §6 Future expansion](operator-playbook.md) as out of scope.

### ⏳ B2.4 — Real-time scenario compute (mid-execution authoring with live recompute)

**Activates when:** customer requests this capability.
**Status:** Out of v2 scope; architectural rework if activated.

### ⏳ B2.5 — Cloud / multi-tenant deployment

**Activates when:** customer requests cloud delivery.
**Status:** Out of v2 scope; air-gapped LAN deployment is load-bearing for the whole design.

---

## 3. Resolved items (audit trail)

### ✅ B0.1 — DRS webapp inclusion in v2 scope

**Resolved:** 2026-05-13 in [commit 2cba85e](https://github.com/) — WS2 webapp formally scoped into the architecture as a required component, replacing the prior "WS2 is headless" framing.

### ✅ B0.2 — DRS webapp ownership

**Resolved:** 2026-05-13 in commits e3bd469 + 34d1a90 — G added to the team as DRS webapp lead.

### ✅ B0.3 — Two-persona operator model

**Resolved:** 2026-05-12 in commit beae0ba — [Operator Playbook](operator-playbook.md) formalised SG Operator + DRS Engineer as the two operator personas inside v2 scope.

### ✅ B0.4 — CC as external integration boundary

**Resolved:** 2026-05-13 in commit 2cba85e — CC documented as client-owned, out of v2 development scope, with integration APIs from SG and/or DRS TBD per CC requirements.

---

## 4. References

- [Operator Playbook §15](operator-playbook.md#15-open-design-questions--tracked-in-the-design-backlog) — was the previous landing place for design-question tracking; superseded by this backlog.
- [v2 Execution Plan](v2-execution-plan.md) — staffing, week-by-week ownership, integration test gates.
- [Architecture Overview](architecture-overview.md) — architectural commitments that constrain what the wireframes can specify.
- [Decision Record](decision-record.md) — load-bearing decisions backing the architecture.
- [Risk Register](risk-register.md) — programme + engineering risks the design must mitigate against.
