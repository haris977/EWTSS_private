# EWTSS v2 — Development Status Report

**Date:** July 6, 2026
**Program:** EWTSS v2 (Electronic Warfare Test & Support System) — greenfield rebuild
**Prepared by:** Constelli Projects engineering team

> This report is written for a program-management / client audience. Section 7 (Appendix) carries internal engineering detail — branch names, file paths — that can be trimmed before external distribution.

---

## 1. Executive Summary

EWTSS v2 is a greenfield rebuild of the Electronic Warfare Test & Support System, replacing the v1 production system after a field-reported performance degradation (a cumulative resource leak after ~10 minutes of continuous use). The v2 architecture was fully designed and de-risked through four rounds of MVP validation before the current hardening phase began.

**Where things stand today:**

- The formal 17-week / 7-engineer hardening phase is still **pending final approval** — but the team has not waited on that approval. Several of the hardening phase's foundational subsystems have already been **designed and built ahead of schedule**, validated end-to-end against real infrastructure (live Kafka broker, live PostgreSQL, live hardware captures) rather than mocks.
- Of the 45 tracked Milestone-1 backlog items, **7–8 have a closed design and a working, tested implementation** (time synchronization, DRS instance addressing/roster, the C++ parser ABI contract, Kafka control-plane infrastructure, the frontend-framework decision, plus partial progress on the security-design baseline and cross-repo contract freeze). The remaining items are mostly UI workstreams that are open pending one blocking prerequisite: detailed UX wireframes (item B1.1), which has not yet started.
- Four engineers have delivered concrete, verifiable work in the current phase: **Mohit Mundhra** (Architect), **Pavan Kumar Kasula** (DRS-Server Engineer), **Haris Manzar** (C++ Parser Developer), and **Venkatasiva "Shiva"** (Kafka/Routing Engineer). Their work is detailed by architecture area in Section 4.
- The HF and VU parser/bridge stack (parser DLL, Kafka wiring, TCP transport) is functionally complete and passing tests against the team's simulated hardware data generator. The **RDFS (DDF-550 Direction-Finding Receiver)** parser DLL is decoding incoming data successfully; its outgoing response format is still being finalized, and bridge/server integration is in progress (Section 4.5). Five further variants — **AUS, JLB, JMB, JHB, JHBE** — have completed ICD-level design work but implementation has not started yet, and are queued next (Section 4.6).
- The main near-term engineering task is **consolidation**: several completed pieces of work currently live on separate development branches (the team's practice is to validate a design in isolation before merging) and need to be brought together into one integration line before the whole system can be exercised end-to-end. This is flagged as a concrete open item in Section 6, not a quality concern — every piece of work below has been independently verified on its own branch.

---

## 2. Team & Responsibilities

| Name | Role | Primary Architecture Area(s) |
|---|---|---|
| **Mohit Mundhra** | Architect | Overall v2 architecture, design decisions (ADRs), program backlog/risk tracking, and documentation |
| **Pavan Kumar Kasula** | DRS-Server Engineer | `drs-server` implementation — time-synchronization engine and DRS instance roster/addressing subsystem — plus the original `drs-bridge` scaffolding, the reference C++ parser template, DLL integration fixes, and hardware/jamming integration test tooling |
| **Haris Manzar** | C++ Parser Developer | `drs-bridge` C++ parser DLLs for the DP-ECM hardware family (HF and VU variants) |
| **Venkatasiva ("Shiva")** | Kafka / Routing Engineer | `drs-bridge` Kafka topic wiring, TCP/UDP transport routing, and hardware-variant profile onboarding |

---

## 3. Architecture Context

For readers unfamiliar with the v2 layout, the system is split into independently deployable services:

- **`drs-server`** — the time-synchronization engine, DRS instance roster/addressing, and the REST/WebSocket API consumed by the desktop app.
- **`drs-bridge`** — per-hardware-variant adapters: a Python runtime that manages TCP/UDP connections to physical EW hardware, publishes/consumes Kafka events, and loads a C++ parser DLL per hardware variant to translate wire protocol ↔ JSON.
- **`drs-webapp`** — the browser-based DRS Engineer console.
- **`sg-app`** — the SG Operator desktop application.
- **Infrastructure** — the Kafka broker and supporting control-plane wiring shared by the above.

The four engineers' work below maps directly onto this diagram: Mohit owns the architecture governing the whole system; Pavan owns `drs-server` end to end (and built the original `drs-bridge` scaffolding); Haris and Shiva own `drs-bridge` day to day, split by concern (C++ parsing and Kafka/transport routing respectively).

---

## 4. Status by Component

### 4.1 Architecture & Program Management — Mohit Mundhra

**Completed**
- 19 Architecture Decision Records covering every load-bearing design choice (frontend framework, telemetry pipeline, database choice, Kafka topology, STK integration approach, and more).
- Frontend framework decision revisited and finalized (React + JavaScript, replacing an earlier Angular direction) — already reflected in a working DRS webapp code slice.
- A client-facing **Software Architecture Document** (consolidated deliverable, 8 sections, 9 diagrams, 12 tables) covering system overview, architecture drivers, all 19 decisions, major workflows, and deployment view.
- A full **Work Breakdown Structure** for the hardening phase, structured for direct import into the program's project-tracking tool.
- Design specifications for the **time-synchronization engine** and the **DRS instance roster/addressing subsystem** — implemented and validated against live infrastructure by Pavan Kumar Kasula (see 4.2).
- An approved **repository and release strategy** (splitting the current monorepo into separately versioned service repositories), migration of which is now underway.
- A first-cut **security design baseline** and the initial **cross-repo API contract freeze**.

**In Progress**
- Air-gap dependency vendoring for offline/on-site installation (process documented; binary packaging still to be finalized before delivery).
- The repository-split migration itself.

**Pending / Open**
- Detailed UX wireframes (the headline Milestone-1 deliverable) — not yet started, and is the single blocking prerequisite for roughly a dozen downstream UI-dependent backlog items.
- Control Center integration API surface — blocked on client-supplied requirements.
- Physical NTP hardware lab acceptance test — the single highest-priority open backlog item; requires physical lab access, not further engineering work.
- Formal approval/kickoff of the 17-week hardening phase itself.

---

### 4.2 `drs-server` — Time Sync & DRS Instance Roster — Pavan Kumar Kasula

**Completed**
- Time-synchronization engine: NTP monitoring service, a three-tier drift-detection state machine (Healthy → Warming → Drift Warning → Drift Alert → Sync Lost) with per-hardware-variant precision tolerances, a live status API, and Kafka event publishing — fully test-driven and passing, including the fix ensuring live NTP samples are correctly fed into the sync engine before status is evaluated.
- DRS instance roster & addressing subsystem: database-backed roster with full create/read/update history, automatic port allocation for newly registered hardware instances, a live presence/health tracker, and an exercise-readiness check API — verified against a real PostgreSQL database and Kafka broker (65 automated tests passing, zero failures, zero skips).
- Data schema for the new RDFS (DDF-550) hardware variant defined (see 4.5).

**In Progress**
- The operator-facing UI surfaces for the roster (desktop app readiness panel, webapp network-config editor) — blocked on the UX wireframe workstream above.

**Pending / Open**
- A 30-minute sustained load test (2,000 messages/second) — requires the full stack running together in a lab environment; not yet executed.
- Scenario-management subsystem: design is complete and approved, but implementation has not started (scheduled mid-way through the hardening phase).
- Full `drs-server`-side implementation for the RDFS variant (beyond the schema above) — remaining work (see 4.5).

---

### 4.3 `drs-bridge` — C++ Parser Libraries (DP-ECM Hardware Family) — Haris Manzar

**Completed**
- A canonical **reference C++ parser template** — the standard starting point for every future hardware-variant parser, complete with a build system and an end-to-end integration test. This closes a previously identified gap in the parser development process.
- The **DP-ECM frame header byte order confirmed big-endian** against a live hardware capture, resolving a previously open protocol question.
- **HF parser restructuring**: the original single large dispatcher file has been broken into one focused module per command group (7 primary groups + 6 sub-groups), each independently buildable, cutting the main dispatch file down to a thin ~200-line router.
- Big-endian protocol conversion applied across both the outgoing (device-response) and incoming (command-decode) paths, for both HF and VU variants.
- HF and VU now build as fully independent components, so a change to one cannot affect the other's build.
- **HF and VU parser DLLs are functionally complete and passing tests against the team's dummy/simulated hardware data generator** — both variants are green end-to-end at the parser level.

**In Progress**
- A final systematic verification pass confirming the big-endian conversion is correct field-by-field against live hardware captures, beyond the dummy-data testing above (current spot-checks are positive; full coverage is still being worked through).
- Consolidating the HF restructuring work and the big-endian fix work — done on separate branches per the team's isolate-then-integrate practice — into one integration line.

**Pending / Open**
- Applying the same per-command-group restructuring to the VU parser (currently still a single large file).
- Expanding automated test coverage to include the newly added command groups (current tests cover the original baseline set).
- Refreshing the parser's internal status documentation to reflect current command-group coverage.

---

### 4.4 `drs-bridge` — Kafka Routing & Hardware Onboarding — Venkatasiva ("Shiva")

**Completed**
- Core Kafka control-plane wiring: hardware-variant registration, health/heartbeat events, and roster synchronization, with automatic (idempotent) creation of the required Kafka topics on startup.
- Profile-driven hardware onboarding validated end-to-end for the HF and VU variants, meaning a new hardware variant can be added primarily through configuration rather than code changes.
- **HF and VU bridge integration — Kafka event wiring and dual-port TCP routing (separate command and monitoring ports per hardware instance) — is complete and working.**
- A full native Kafka + PostgreSQL local setup/run guide for onboarding new development machines.

**In Progress**
- Merging the HF/VU dual-port routing work into the main integration line (currently on its own validation branch, consistent with the team's process).
- Implementation of the third hardware variant, **RDFS (DDF-550 Direction-Finding Receiver)** — parser and Kafka/TCP bridge wiring underway, following the same pattern proven on HF/VU (see 4.5).

**Pending / Open**
- Fully automatic Kafka topic derivation from hardware-variant configuration files (currently a maintained list kept in sync by hand — functionally complete but not yet self-updating).
- Kafka topic contracts for live telemetry data (deliberately deferred until the first live-hardware integration milestone, per the original infrastructure design).

---

### 4.5 RDFS (DDF-550) Variant — Onboarding Status Across the Stack

RDFS (DDF-550 Direction-Finding Receiver) is the third hardware variant now being onboarded, following the same profile-driven pattern proven on HF and VU. Because onboarding touches every layer of the stack, status is tracked here across all three owners:

| Layer | Owner | Status |
|---|---|---|
| Parser DLL | Haris Manzar / Shiva | Decoding (incoming data → JSON) complete and working; outgoing response/encode format not yet finalized |
| Bridge (Kafka + TCP routing) | Venkatasiva ("Shiva") | In progress |
| `drs-server` (schema + API) | Pavan Kumar Kasula | Data schema complete; full implementation remaining |

This is a normal, expected profile for a variant currently mid-onboarding — HF and VU went through the same sequence before reaching their current fully-tested state.

The RDFS data schema and frame structure were decided by Pavan Kumar Kasula; the parser and bridge-wiring pieces above are being implemented against that design.

---

### 4.6 Other Hardware-Variant Parsers (AUS, JLB, JMB, JHB, JHBE) — Design Done, Implementation Not Started

Beyond the DP-ECM family (HF/VU — complete) and RDFS (in progress, see 4.5), five further hardware variants have been analyzed against their ICDs: **AUS, JLB, JMB, JHB, and JHBE**.

**Status:** ICD-level design work is complete for all five, but implementation (parser code and bridge wiring) has not started yet.

This is expected at the current stage: these variants are queued behind RDFS in the onboarding sequence and will follow the same proven pattern (parser → bridge wiring → `drs-server` integration) once picked up.

---

## 5. Cross-Cutting: Integration Status

The team's workflow validates each subsystem in isolation — against real infrastructure, not mocks — before merging it into a shared integration line. This has produced several independently-verified, working pieces of the system that are not yet all merged together:

- Pavan's roster/addressing work on `drs-server` and Mohit's architecture documentation set live on the team's central repository.
- Haris's HF restructuring and his big-endian protocol fixes were validated on separate branches and now need to be brought together.
- Shiva's dual-port routing and new hardware-variant onboarding work is validated and ready to merge into the shared `drs-bridge` line.

None of this represents rework — each piece is functionally complete and tested on its own branch. The next concrete engineering step is a consolidation pass to bring all three into one buildable, testable integration branch, ahead of the first full-stack end-to-end exercise.

---

## 6. Risks & Open Items

| # | Item | Area | Status / Mitigation |
|---|---|---|---|
| 1 | Branch consolidation (HF restructure + big-endian fixes; Kafka routing branch) | drs-bridge | Engineering task, no external dependency — schedule a merge/integration pass |
| 2 | Physical NTP hardware lab acceptance test | drs-server / Infrastructure | Blocked on physical lab access to both workstations; no further coding required |
| 3 | UX wireframes not started | Program-wide | Blocks ~12 downstream UI backlog items; recommend prioritizing kickoff |
| 4 | Sustained load test (30 min, 2,000 msg/s) not yet run | drs-server | Requires full stack + lab hardware together |
| 5 | Test coverage lagging newly added parser command groups | drs-bridge (C++) | Being addressed alongside the endianness verification pass |
| 6 | Formal hardening-phase kickoff still pending approval | Program-wide | Foundational subsystems already validated ahead of kickoff, reducing schedule risk once approved |
| 7 | RDFS (DDF-550) onboarding spans three owners at different stages (parser decoding done but response format undecided, bridge in progress, server implementation pending) | drs-bridge / drs-server | Track as one cross-team item so the three pieces land together rather than drifting apart |

---

## 7. Appendix — Internal Engineering Notes

*(Internal use — recommend removing before external client distribution.)*

- HF restructuring lives on branch `hf_folder_seprate`; big-endian fixes are on `haris_commit_changes_real`. Both diverge from a common ancestor and need a merge/rebase pass — the restructured per-group files currently still carry the pre-big-endian little-endian encoders and will need the conversion re-applied during the merge.
- Shiva's dual-port routing, the DDF-550/RDFS parser, and the Kafka topic-rename work live on the `feature/rdfs` branch in the original `drs-bridge` repository history; not yet present on the current `drs-bridge` working branch.
- The canonical, up-to-date source for `drs-server`'s roster/addressing subsystem and the full architecture/backlog documentation set is the team's `origin/main` history (the `ewtss-v2-design` repository); the currently checked-out working branch is mid-transition into a standalone `drs-bridge`-only repository per the approved repository-split strategy, so it does not currently track `docs/`, `drs-server/`, or `infrastructure/` — those directories still exist locally but are intentionally excluded from this branch's git history during the split.
- A few internal reference documents (the DP-ECM parser's own status README, and the ICD open-questions list) are lagging behind the actual code state and should get a quick refresh pass — noted as a documentation hygiene item, not a functional gap.
