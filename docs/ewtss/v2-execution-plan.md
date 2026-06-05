# EWTSS v2 — Execution Plan

**Audience:** project lead, engineering leads, the customer programme manager, and the engineers who will execute the v2 hardening phase.
**Purpose:** the operational plan for building the telemetry pipeline + extending Mode A to feature-completeness. Staffing, parallel workstreams, critical path, and what "done" looks like at each gate.
**Read time:** 15 minutes.
**Status:** approved / pending approval per the [Executive Brief](executive-brief.md). This document only matters once the v2 hardening phase is funded.

---

## 1. Scope of this plan

This document plans the **v2 hardening phase** as scoped in the executive brief — the **~4 month / ~17 week** effort to:

- Build `drs-server` (telemetry consumer + REST/WS API on WS2, also serves the DRS webapp static assets).
- Build `drs-bridge` (Python supervisor + C++ parser libs on WS2).
- Build the **DRS webapp** (browser SPA on WS2 served by drs-server — Angular preferred / React fallback per [ADR-018](decision-record.md#adr-018--ws2-drs-webapp-required-browser-frontend-on-the-drs-workstation-served-by-drs-server)).
- Migrate the database to PostgreSQL 16 + TimescaleDB 2.x.
- Stand up Kafka 3.x KRaft on WS2.
- Implement C++ parser libraries for the customer-prioritised hardware variants.
- Extend `Sg.App` (Mode A) with Transmitter / Receiver / Antenna typed entities.
- Wire the scenario publisher to drs-bridge for Scenario-mode operation.

**Not in scope here:**

- Mode B Phase 1 (read-only browser viewer) and Phase 2 (full browser authoring) — separate plans, activate only if customer-funded.
- Customer site installation, customer-acceptance testing — separate engagement.
- Maintenance phase post-acceptance.

---

## 2. Team composition

The v2 architecture spans six technical domains: Python async infrastructure, C++ binary protocol parsing, STK COM automation, Angular/CesiumJS SG-side frontend, DRS-side webapp frontend, GIS data preparation. The realised team has seven people with a mix of specialist and polyglot skills, which gives natural cross-stream coverage in addition to per-domain ownership.

| ID | Person | Skills | Primary role | Effective FTE allocation |
|---|---|---|---|---|
| **F** | Senior Python | Python | `drs-server` lead — FastAPI scaffolding, async SQLAlchemy + asyncpg, Kafka consumer architecture, batched writes, WebSocket hub, TimescaleDB integration | Python 1.0 |
| **A** | Polyglot IC | Python + C# | `drs-bridge` Python layer + scenario publisher endpoint integration on the C# side | Python 0.7 + C# 0.3 |
| **B** | C# specialist | C# | `Sg.App` extension — Transmitter / Receiver / Antenna typed entities; Mode A telemetry display panels; STK compute pipeline integration | C# 1.0 |
| **C** | C++ specialist | C++ | `drs-bridge` C++ parser libraries — `extract_frame` / `parse_message` / `format_response` ABI; per-variant parsers; golden-frame test corpus; fuzz harness | C++ 1.0 |
| **D** | Cross-stack lead | Reviewer + IC across stacks | Architect / lead — code review across all streams, cross-stream integration tests, customer demo prep, infrastructure setup (Kafka topics, TimescaleDB schema, Windows Services via NSSM, WiX MSI installer packaging) | ~0.5 review + 0.5 Python infra |
| **E** | SG-side frontend developer | Angular (primary) — cross-trained to Python during ramp | Weeks 1–2: Python ramp + take ownership of `drs-server` REST query endpoints + RBAC + JWT auth from week 3. Returns to Angular when Mode B activates (post-ship). | Python 0.7 (after week 2 ramp) |
| **G** | DRS-side frontend developer | Angular (preferred) or React | `DRS webapp` lead — browser SPA on WS2, served by `drs-server`. Health dashboard, per-variant monitor-scan, IP / network configuration, message logs, per-variant control. Framework choice (Angular vs React) decided at Milestone-1 design; Angular preferred for skill alignment with E and Mode B future reuse. | Frontend 1.0 |

**Total effective FTE by stack:**

| Stack | Total FTE | Original plan needed |
|---|---|---|
| Python (drs-server + drs-bridge layer + infra + ICD codegen) | F (1.0) + A (0.7) + E (0.7 from week 3) + D (0.5) ≈ **2.9 FTE** | 3 Python seats |
| C# (Sg.App extension + scenario publisher + telemetry panels) | B (1.0) + A (0.3) ≈ **1.3 FTE** | 1 C# seat |
| C++ (parser libraries) | C (1.0) ≈ **1.0 FTE** | 1 C++ seat |
| Frontend (DRS webapp on WS2) | G (1.0) ≈ **1.0 FTE** | 1 Frontend seat (added scope: DRS webapp surfaced from RFQ review) |
| Architect / cross-stream review | D (0.5) ≈ **0.5 FTE** | implicit in original plan |

Adding F to the team was the leverage point for the Python stack — without F, Python would have been understaffed at ~1.7 FTE against the 3-seat requirement. G covers the DRS-side frontend so the DRS webapp scope (RFQ §B Health status display + §F Manage Logs + per-variant monitor-scan) does not compete with E's REST-endpoint workload or land in maintenance after acceptance.

**Mode B roles (deferred):**

- Frontend developer (Angular / Cesium) — covered by E when Mode B activates; no separate hire needed (existing Angular skill in-house is the de-risking that the [Risk Register R8](risk-register.md) calls out).
- GIS specialist (offline tile data prep, ~6 weeks part-time) — separate procurement, activates with Mode B.

---

## 3. Per-person module ownership

### 3.1 F (Senior Python) — `drs-server` lead

- FastAPI scaffolding with `lifespan`-managed Kafka consumers, async SQLAlchemy sessions.
- Per-variant Kafka consumer services (one file per hardware variant; ~40 lines each — see [Developer Handbook §13](developer-handbook.md#13-drs-server-internal-design)).
- TimescaleDB integration: hypertable creation scripts, write-batching (100 messages or 500 ms), manual offset commit only after successful write.
- WebSocket fan-out: subscribe to Kafka topics, broadcast to subscribed clients. Auto-reconnect on consumer crash with exponential backoff.
- Health and metrics endpoints (`/health/consumers`, `/health/<variant>/<instance>`).
- Performance hardening to SLA in weeks 12–17.

### 3.2 A (Python + C# polyglot) — `drs-bridge` Python primary; C# scenario publisher integration

- asyncio TCP server scaffolding: `handle_client` coroutine, stream-reassembly buffer (the legacy system's `recv(4096)`-without-buffer bug fixed structurally here).
- `ResponseRouter` mode-switching: consumes `drs.session.control` Kafka topic, dispatches to `RandomGenerator` / `ScenarioPublisherClient` / `HardwareRelay` per active session mode. See [Developer Handbook §12.4](developer-handbook.md#124-the-responserouter).
- `SessionRegistry`: per-hardware session state, replacing legacy global `isRandom` DB flag.
- `ParserHandle` ctypes wrapper around the C++ parser ABI.
- ICD codegen tool ([spec](specs/icd-codegen-tool-design.md)) — implemented in parallel with the first two C++ parsers, used from variant 3 onward.
- C# flex (~30%): integrates B's scenario publisher endpoint with the bridge's `ScenarioPublisherClient`; pairs on STK compute pipeline review.

### 3.3 B (C# specialist) — `Sg.App` extension

- New typed entities: Transmitter, Receiver, Antenna. Per [Developer Handbook §8](developer-handbook.md#8-how-to-add-a-new-stk-entity-type) — DTO + view-model + WPF panel + `IScenarioBackend` methods + tests, ~1.5 days each.
- Scenario publisher HTTP endpoint: `GET /exercises/{id}/responses?group_id=X&unit_id=Y&tick=Z` returning the pre-computed scenario response from `computed_links`. Hosted from `Sg.App` itself during exercise execution; `drs-bridge`'s `ResponseRouter` consumes it in SCENARIO mode.
- Compute → write-to-TimescaleDB pipeline: `IScenarioBackend.ComputeLinks(exerciseId)` runs STK link-analysis tick-by-tick, batches results, writes to PostgreSQL on WS2 over LAN. Already specified in [Architecture Overview §4.5](architecture-overview.md#45-scenario-compute-and-link-analysis-write-back-scenario-modes-planning).
- Mode A telemetry display panels: subscribe to `drs-server` WebSocket, render incoming telemetry alongside the scenario globe. New WPF view-models per data class (FF, FH, Burst, Health).
- **STK Desktop UX review (~3–5 days, weeks 1–2):** review STK Desktop's actual placement / editing UX (drawing tools, "Insert by …" flows) and bring `Sg.App` into closer alignment where the embedded ActiveX environment allows. MVP4.5's keyboard-finalize gesture is a pragmatic choice given STK COM constraints — v2 should improve discoverability where possible (toolbar buttons, status hints, gesture parity with STK Desktop). See [ADR-016](decision-record.md).
- **[Contingent — engage only if Phase 4 / Phase 5 latency measurement triggers] In-memory `computed_links` cache on Sg.App (~2–3 days):** after compute completes, hold the result in memory keyed by `(exercise_id, group_id, unit_id, tick)` and serve ResponseRouter requests from the cache instead of round-tripping to PostgreSQL on WS2 over LAN. Drops Path 3 latency from ~10–17 ms p99 to <1 ms p99. Memory cost ~36 MB per 1-hour scenario — negligible. **Do NOT pre-optimise** — implement only if the integration test measurements (§6.2) show the cross-LAN path approaching IRS budget. See [Architecture Overview §4.5](architecture-overview.md#45-scenario-compute-and-link-analysis-write-back-scenario-modes-planning).
- Save / load and PDF report generation for the WPF host.

### 3.4 C (C++ specialist) — `drs-bridge` parser libraries

- The four-symbol parser ABI from [Developer Handbook §9.3](developer-handbook.md#93-c-parser-interface-contract) — `extract_frame`, `parse_message`, `format_response`, `free_result`.
- Common library: `frame_buffer`, `json_writer`, header-scan helpers — shared across variants.
- One parser library per hardware variant in scope. First variant takes ~3–5 days; subsequent variants ~1.5–3 days each (faster with codegen from variant 3 onward).
- CMake build: shared library targets (`.dll` for Windows — WS1 + WS2 are both Windows per [ADR-018](decision-record.md) + [Deployment Guide §1](deployment-guide.md)), Python ctypes bindings verification.
- Golden-frame test corpus per variant; fuzz harness for `extract_frame` against random input bytes.
- Migration of the legacy `command.csv` / `structure.csv` for existing variants (SRX, MRX, GNSS, JSVUSHF) into the C++-parser model.

### 3.5 D (cross-stack lead) — architect, reviewer, integration test owner

- Architecture review on every PR across all streams; gatekeeper for the load-bearing rules in [Developer Handbook §13.6](developer-handbook.md#136-design-rules-load-bearing) (no `def` route handlers, no `query.all()`, manual offset commit only after DB write, etc.).
- **Integration test rig owner from week 1.** Builds and grows a cumulative integration test suite (see §6) that the project demos against at every phase checkpoint. Includes the SDFC simulator, entity-controller app simulator, synthetic Kafka producer, multi-instance load simulator, and STK fixture scenarios.
- **Phase checkpoint integration testing** — runs the full cumulative suite at the end of each phase. A phase only passes its gate when its integration test passes plus all prior phases' integration tests still pass (regression coverage).
- Air-gapped install rehearsal in a sandbox VM (week 9 onward); customer demo prep + customer acceptance test artefacts.
- Hands-on Python infrastructure work (~30% time, weeks 1–8 heaviest): Kafka topic creation scripts, TimescaleDB hypertable init, KRaft single-broker setup, Windows Service units (via NSSM or `sc.exe`), Windows installer packaging (WiX / MSI).

### 3.6 E (Angular, cross-trained) — `drs-server` REST + auth + reports

- **Weeks 1–2: Python ramp.** Pair with F on drs-server scaffolding to build Python fluency in the codebase's idioms (async SQLAlchemy, asyncpg sessions, Pydantic models, FastAPI lifespan).
- **Weeks 3–17: full Python contributor.** REST query endpoints (paginated, time-bounded, no `query.all()`): `/measurements`, `/measurements/export` (StreamingResponse), `/sessions`, `/sessions/{id}/stats`.
- RBAC: users / roles / features / permissions tables and middleware, JWT issuance and refresh.
- PDF report generation (WeasyPrint): scenario summary, access-interval tables, post-exercise report.
- Auth integration: token verify on every mutating route.
- Returns to Angular skill set when Mode B activates (post-acceptance).

### 3.7 G (Angular preferred, else React) — DRS webapp lead

- **Weeks 1–2: Framework decision + scaffolding.** Confirm framework (Angular preferred per skill alignment with E; React acceptable fallback). Set up build pipeline; produce a "hello-world" SPA served by drs-server at `/`. Pair with F + E on the REST + WebSocket contract design so DRS webapp endpoints are scoped into the same OpenAPI surface (separate `/webapp/...` prefix or shared namespace — Milestone-1 design call).
- **Weeks 3–5: Webapp shell + auth + first dashboard.** Login flow consuming E's JWT issuance; auth-guarded routes; landing dashboard placeholder; consumption of first `/health/<variant>` and `/sessions` endpoints. First end-to-end smoke: login → dashboard → live data card from one variant.
- **Weeks 6–8: Per-variant monitor-scan (first 4–6 variants).** Live message-rate display via WebSocket consuming F's WS hub. Variant tab navigation. Per-variant message-class counts. Parallel with F's Random-mode rollout.
- **Weeks 9–11: Message log viewer + IP configuration.** Sent Message Log and Receive Message Log per variant with filter / sort (RFQ §F Manage Logs). IP / network configuration panel (CRUD against drs-server's hardware-config endpoints — A coordinates the backend surface). Health detail view (LRU + sub-system + communication-link health per RFQ §B).
- **Weeks 12–13: Per-variant control + variants 7–9 coverage.** Restart TCP server / parser via drs-server endpoints. Operator confirmation gates on disruptive actions. Monitor-scan rollout completes for variants 7–9.
- **Weeks 14–15: Variants 10–12 + accessibility + customer demo prep.** Final variant monitor-scan coverage. Keyboard navigation, screen-reader labels for instructor environments where applicable. Demo script.
- **Weeks 16–17: Integration test + DVD packaging.** Webapp included in WS2 deployment package; first-run install verifies webapp loads via `http://localhost:8000/` (or chosen path). Acceptance demo: DRS Engineer logs in on WS2, navigates all surfaces against live integrated-mode traffic.
- **Cross-cutting:** E pair-reviews G's auth integration in week 3 (shared JWT); G pair-reviews E's RBAC view-gating in week 8. D reviews every G PR against the architecture rules (no direct DB or Kafka access from webapp; everything through drs-server).
- **When Mode B activates (post-ship):** G + E together on the Mode B SG-side SPA (`Sg.Web`). Existing Angular component library from the DRS webapp is partially reusable for Mode B (data tables, log viewers, filter / sort controls).

---

## 4. Parallel workstreams (week-by-week, 17-week build)

The architecture explicitly enables independent parallel work from day 1 — the team ships in parallel because they touch disjoint parts of the codebase. Person IDs below match §2 / §3.

### 4.1 Weeks 1–2: scaffolding and contracts

```
[F]   drs-server FastAPI scaffold; async engine + asyncpg pool;
      first hypertable creation; first aiokafka consumer (echo)
[A]   drs-bridge asyncio TCP echo server; Kafka KRaft setup; topic creation
[B]   Sg.App scaffold for Transmitter typed entity (DTO + IScenarioBackend
      method signatures, no impl yet) — unblocks downstream consumers
[C]   First-variant parser based on the reference template at
      drs-bridge/parsers/reference/ (4-symbol ABI + CMake + ctypes test
      already in place — pre-shipped 2026-05-21). Copy + replace synthetic
      frame format with first IRS layout + golden-frame tests.   → CRITICAL PATH
[D]   Architecture review of contracts; Kafka topic creation scripts;
      TimescaleDB hypertable init; Decision Record updates
[E]   Python ramp — pair with F on drs-server idioms; first end-to-end
      consumer + DB write learning exercise
[G]   DRS webapp framework decision (Angular preferred, else React) signed
      off with D + project lead. SPA build pipeline + "hello world"
      served by drs-server at the chosen webapp path. Pair with F + E
      on REST + WebSocket contract scoping for webapp endpoints.
```

**Gate at end of week 2:** ABI signed off; OpenAPI spec for drs-server frozen (including DRS-webapp endpoint surface); `IScenarioBackend` extensions agreed; topic naming + hypertable schema signed off; DRS webapp framework decision signed off; webapp scaffold serves successfully from drs-server on a clean checkout.

### 4.2 Weeks 3–5: first end-to-end message flow

```
[F]   Per-variant Kafka consumers wired → TimescaleDB writers; batched
      writes operational; offset-after-write semantics verified
[A]   ResponseRouter + SessionRegistry; ParserHandle ctypes wrapper.
      First end-to-end smoke test: SDFC simulator → asyncio TCP →
      extract_frame → parse_message → Kafka → drs-server consumer →
      TimescaleDB row appears.   → CRITICAL PATH (week 4)
[B]   Transmitter typed entity end-to-end (DTO, VM, WPF panel, tests).
      Scenario publisher endpoint stubbed (returns 501 until populated).
[C]   parse_message bodies for first 3 priority hardware variants
      (e.g. SRX or RDFS first; then 2 follow-ons)
[D]   Cross-stream integration test rig; smoke pass review
[E]   /sessions, /sessions/{id} endpoints (now Python-fluent);
      RBAC tables + token issue/verify
[G]   DRS webapp shell + auth flow (consumes E's JWT issuance);
      auth-guarded routes; landing dashboard placeholder; consumption
      of first /health/<variant> endpoint. First end-to-end smoke:
      login → dashboard → live data card from one variant.
```

**Gate at end of week 5:** First hardware variant operational end-to-end on dev LAN. Bytes from a simulated device land as a row in TimescaleDB; the SG-side operator UI on WS1 shows the row in real time; the DRS Engineer can log in to the DRS webapp on WS2 and see one live data card for the same variant.

### 4.3 Weeks 6–8: Random mode + 4–6 priority variants

```
[F]   WebSocket fan-out to subscribed clients; consumer supervisor with
      exponential backoff; restart-count health metric;
      /health/consumers endpoint
[A]   RandomGenerator: per-hardware random data within YAML-declared
      ranges. drs.control.<variant> topic for start/stop/config.
      Random mode end-to-end smoke test.
[B]   Receiver + Antenna typed entities. Start scenario publisher
      endpoint design (returns 501 from the WPF host).
[C]   parse_message + format_response for variants 4–6.
      ICD codegen tool integration with the first 3 variants
      (regenerate constants headers, verify identical to hand-written).
[D]   First load test at 1,000 msg/s sustained — measure where the
      bottleneck appears
[E]   /measurements paginated endpoint; /measurements/export
      (StreamingResponse for large queries); JWT refresh tokens
[G]   Per-variant monitor-scan views for first 4–6 variants. Live
      message-rate display via WebSocket (consumes F's WS hub).
      Variant tab navigation + per-class message counters.
      Pair-review with E on RBAC view-gating.
```

**Gate at end of week 8:** Random mode operational for 4+ variants. drs-server sustaining 1,000 msg/s in load test.

### 4.4 Weeks 9–11: Scenario mode + compute pipeline + variants 7–9

```
[F]   Hot-path index review on /measurements queries; streaming-export
      memory profile; retention policy enforcement on hypertables
[A]   ScenarioPublisherClient (HTTP to Sg.App); Audit log of
      scenario authoring operations (consumed from ScenarioChanged events).
      Hardware-config backend endpoints surface for G's IP-config panel.
[B]   Compute → computed_links pipeline operational (per-tick STK
      compute, batched DB writes). Scenario publisher endpoint
      returning real responses from computed_links.   → CRITICAL PATH
      FF + Health telemetry display panels in Mode A.
[C]   parse_message for variants 7–9 (depending on customer priority).
      Fuzz harness for extract_frame across all variants.
[D]   Performance hardening to 1,500 msg/s gate; air-gapped install
      rehearsal in a sandbox VM
[E]   /reports endpoints; PDF report generation (WeasyPrint) — scenario
      summary, access-interval tables; post-exercise report
[G]   Message-log viewer (Sent / Received per variant) with filter +
      sort + export (RFQ §F Manage Logs). IP / network configuration
      panel (CRUD against A's hardware-config endpoints). LRU +
      sub-system + communication-link health detail view (RFQ §B).
```

**Gate at end of week 11:** Scenario mode operational. First compute → publish → DB → UI round-trip works. drs-server sustaining 1,500 msg/s.

### 4.5 Weeks 12–13: Integrated mode + variants 10–12

```
[F]   Performance hardening to 2,000 msg/s SLA; query-latency budgets met
[A]   HardwareRelay (Integrated mode): TCP forward to entity controller
      applications. Entity-response consumption back into Kafka.
[B]   FH + Burst telemetry display panels. STK compute regression suite
      over the priority-variant scenarios.
[C]   parse_message for variants 10–12 (final priority list).
      All variants documented with golden-frame fixture per [ICD reference](icd-reference-comm-df.md).
[D]   Integrated mode end-to-end smoke test on at least one variant
[E]   Audit log review surface for the operator; admin user-management UI
      backend support
[G]   Per-variant control surfaces (restart TCP server / parser via
      drs-server endpoints, with confirmation gates on disruptive
      actions). Monitor-scan rollout completes for variants 7–9 in
      the DRS webapp.
```

**Gate at end of week 13:** Integrated mode demonstrable on at least 1 variant. 9–12 variants total operational across Random + Scenario + Integrated modes (subject to customer priority list).

### 4.6 Weeks 14–15: Customer-prioritised remaining variants + air-gapped rehearsal

```
[F]   Final consumer optimisation; multi-instance load test
      (10 → 50 → 100 instances)
[A]   Final ResponseRouter polish; integrated-mode entity-app simulator
      for acceptance test
[B]   Final Mode A polish: scenario file dialog, Save As .vdf with
      password, splash-window cold-start verification
[C]   Customer-driven variant additions per signed scope; ICD-revision
      change-management procedure dry-run
[D]   Air-gapped install rehearsal on a clean two-workstation deployment;
      DVD packaging dry-run
[E]   Customer acceptance test prep — synthetic Kafka producer + scenario
      fixtures + step-by-step demo script
[G]   Monitor-scan rollout completes for variants 10–12. Accessibility
      pass (keyboard nav, screen-reader labels) for instructor
      environments where applicable. DRS webapp included in WS2 DVD
      package (built static assets shipped under drs-server's
      webapp_static/). DRS Engineer demo script.
```

**Gate at end of week 15:** All customer-priority variants operational. DVD packaging works on a clean two-workstation install. DRS webapp loads from `http://localhost:<drs-server-port>/` on a freshly-installed WS2 with no manual fix-ups.

### 4.7 Weeks 16–17: Performance hardening + integration test + customer acceptance prep

```
All roles
- Full integration test on a clean two-workstation deployment.
- Live SDFC session, scenario mode, all active variants, frontend showing
  real-time detections + scenario globe + entity response log
  simultaneously.
- 2,000 msg/s sustained for ≥30 minutes without degradation.
- Air-gapped install rehearsal final pass.
- DVD packaging finalised.
- Customer acceptance test artefacts complete.
```

**Gate at end of week 17:** Build complete — ready for customer acceptance test.

---

## 5. Critical path

The minimum chain that must complete before end-to-end testing can begin:

```
Week 1                     Week 2                       Week 4
┌──────────────────┐       ┌──────────────────────┐     ┌──────────────────────┐
│ Infrastructure   │  ──►  │ Parser ABI contract  │ ──► │ First end-to-end     │
│ (Kafka, DB,      │       │ frozen (extract_     │     │ message flow         │
│ topics, tables)  │       │ frame + parse_       │     │ (SDFC → bytes →      │
│ — D + F + A      │       │ message signatures)  │     │ Kafka → DB row)      │
│                  │       │ — C                  │     │ — A + C              │
└──────────────────┘       └──────────────────────┘     └──────────────────────┘
                                                                  │
                                                                  ▼
                          Week 5                         Week 9–11
                          ┌──────────────────────┐     ┌──────────────────────┐
                          │ ResponseRouter       │ ──► │ Scenario mode round- │
                          │ round-trip (C++      │     │ trip (Sg.App    │
                          │ format_response →    │     │ → drs-bridge →       │
                          │ TCP write back to    │     │ entity apps in       │
                          │ SDFC)                │     │ Integrated mode)     │
                          │ — A + C              │     │ — All people         │
                          └──────────────────────┘     └──────────────────────┘
                                                                  │
                                                                  ▼
                                                        Week 16–17
                                                        ┌──────────────────────┐
                                                        │ Full integration     │
                                                        │ test → customer      │
                                                        │ acceptance prep      │
                                                        └──────────────────────┘
```

**Items NOT on the critical path** (can slip without blocking the gate):

- Mode A telemetry display panels (B) — graceful degradation: WPF without telemetry view still ships, panels added in maintenance.
- PDF report generation (E) — same: ships in maintenance if needed.
- Performance hardening to 2,000 msg/s — if SLA targets aren't met by week 17, ship at 1,500 msg/s gate and tune in maintenance per measured field load.
- Variants beyond customer-priority list — ship as-needed in maintenance.

---

## 6. Integration testing checkpoints

Each phase ends with a **cumulative integration test pass** — not just a functional gate. Every checkpoint reruns all prior checkpoint tests plus the new phase's coverage. A phase doesn't pass its gate until every prior phase's test still passes; regressions in earlier phases block forward motion.

D owns the integration test rig and runs each checkpoint. Other team members contribute fixtures and assertions for the components they own. Pass criteria are pre-agreed before each phase starts (no moving goalposts at gate time).

### 6.1 Test rig

The integration test rig is built incrementally — D extends it phase by phase. Components, by the phase they first appear:

| Component | Introduced in | Purpose |
|---|---|---|
| **SDFC simulator** | Phase 2 | Simulates the scenario controller's TCP frames into `drs-bridge`; drives one variant initially, then all priority variants |
| **CI bring-up harness** | Phase 1 | Spins up Kafka KRaft + PostgreSQL + drs-server + drs-bridge in a single `docker-compose` for the integration test environment |
| **Synthetic Kafka producer** | Phase 3 | Drives load-test scenarios at configurable msg/s rate |
| **Multi-instance simulator** | Phase 3 | Simulates N concurrent DRS instances (10 → 50 → 100) for capacity testing |
| **STK fixture scenarios** | Phase 4 | Pre-built `.sc` scenarios covering each compute path (point-to-point links, area coverage, mobile entities); used as deterministic input for the compute → publish round-trip |
| **Entity-controller app simulator** | Phase 5 | Simulates entity workstation TCP endpoints for Integrated mode round-trip |
| **Air-gapped install rig** | Phase 6 | Clean two-workstation VM environment for DVD install rehearsal |

The rig lives at `infrastructure/integration-tests/` in the repo. Each phase's tests live in a numbered subdirectory (`phase-1/`, `phase-2/`, …) so the cumulative test invocation is `pytest infrastructure/integration-tests/` and the per-phase invocation is `pytest infrastructure/integration-tests/phase-N/`.

### 6.2 Per-phase test scope and pass criteria

| Phase | End | Test scope (cumulative) | Pass criteria |
|---|---|---|---|
| **1 — Scaffolding + contracts** | Week 2 | ABI smoke: `drs-bridge/parsers/reference/` builds and the existing `tests/test_reference_parser_integration.py` passes against the live `.dll`/`.so` (already green in CI as of 2026-05-21); OpenAPI spec validates against the empty drs-server scaffold; Kafka topics + hypertables created and reachable | Of the four original contract artefacts (parser ABI, YAML profile schema, IScenarioBackend extensions, OpenAPI), the parser ABI is closed (reference parser shipped 2026-05-21); the other three are signed off by D + project lead and the smoke harness passes on a clean checkout |
| **2 — First end-to-end** | Week 5 | Phase 1 + SDFC simulator → C++ parser → bridge → Kafka → drs-server → TimescaleDB row → WS broadcast → operator UI display, on dev LAN, for 1 priority variant. Replay over 5 minutes; assert no message loss, no consumer crash. **Plus: DRS Engineer logs into the DRS webapp on WS2 and sees the same variant's live data card.** | Round-trip latency under 500 ms p99; zero offset-commit-before-DB-write violations; SG-side UI on WS1 displays the expected number of rows; DRS webapp on WS2 shows live data for the same variant from the same WS broadcast |
| **3 — Random mode + 4–6 variants** | Week 8 | Phase 1–2 tests pass + 4-variant Random mode simultaneous + 1,000 msg/s sustained for 10 minutes via synthetic Kafka producer + multi-instance simulator at 10 concurrent instances | Zero consumer crashes; offset commit verified against DB row count (no gaps); WebSocket fan-out delivers all messages to subscribed clients; consumer supervisor restart count = 0 |
| **4 — Scenario mode + compute pipeline** | Week 11 | Phase 1–3 tests pass + scenario authoring on Sg.App → compute → write to `computed_links` over LAN → drs-bridge `ScenarioPublisherClient` retrieves responses → Random + Scenario modes both active simultaneously on different variant instances. **Plus: Path 3 cross-LAN latency measurement** — drs-bridge → Sg.App scenario publisher endpoint → WS2 PostgreSQL `computed_links` lookup → response. Histogram captured at p50, p95, p99 over a 5-minute sustained scenario run. | Compute → write throughput acceptable (~100 ticks/s); 1,500 msg/s sustained on telemetry side. **Path 3 latency p99 ≤ 30 ms** (well within typical IRS budget; if exceeded, trigger the in-memory cache fallback per [§3.3](#33-b-c-specialist--sgapp-extension)). |
| **5 — Integrated mode** | Week 13 | Phase 1–4 tests pass + entity-controller app simulator + 1 variant in Integrated mode + Scenario-mode flow active simultaneously + 9–12 variants total operational. **Plus: Path 3 latency re-measured under Integrated load** (entity-app responses on the wire concurrently with ResponseRouter scenario lookups — verifies WS2 PostgreSQL doesn't contend under combined read/write pressure from drs-server's telemetry writes + Sg.App's `computed_links` reads). | Entity-app TCP responses round-trip through Kafka into TimescaleDB; entity-response log visible in operator UI; no cross-mode data corruption. **Path 3 latency p99 ≤ 30 ms under Integrated load** (combined drs-server write + Sg.App read concurrency on WS2 PostgreSQL must not degrade Path 3 measurably). |
| **6 — All priority variants + DVD install** | Week 15 | Phase 1–5 tests pass + DVD-installed clean two-workstation deployment + all customer-priority variants + Random/Scenario/Integrated modes simultaneously across the variant set + DRS webapp accessible from WS2 browser after clean install (no manual webapp setup steps) | Clean install completes in under 90 minutes per WS; first-run verification per [Deployment Guide §4.4 + §5.4](deployment-guide.md#44-first-run-verification) passes; no manual fix-ups required during install; DRS webapp loads on first WS2 boot |
| **7 — Performance + acceptance** | Week 17 | Phase 1–6 tests pass + 100-instance load simulator + 2,000 msg/s sustained for ≥30 minutes + customer acceptance demo script end-to-end | 2,000 msg/s sustained for 30+ minutes without consumer crashes or DB-write backpressure; query-latency budgets met under that load (per [Architecture Overview §8.2](architecture-overview.md#82-current-production-system-vs-v2--ceilings-and-failure-modes)); demo script runs without scripted intervention |

### 6.3 Test discipline rules

- **No phase passes its gate without an integration test pass.** Function-level success is necessary but not sufficient.
- **Cumulative coverage means cumulative cost.** By Phase 7, the full suite takes hours to run end-to-end. CI runs the per-phase suite on every PR; the cumulative suite runs nightly and at gate time.
- **Failing tests block forward motion.** If Phase 4's test surfaces a regression in Phase 2's flow, Phase 4's gate doesn't pass until Phase 2's flow is fixed.
- **D + project lead pre-agree pass criteria before each phase starts.** This avoids moving goalposts at gate review and gives engineers a clear target during the phase.
- **Test artefacts are committed.** Fixtures, golden frames, reference scenarios, and assertion scripts all live in the repo — not in D's local environment.

---

## 7. Specialist risks (per person)

These are the role-specific risks that don't show up in the general [Risk Register](risk-register.md) — they're tied to specific work in a specific week.

| Risk | Owner | Mitigation |
|---|---|---|
| **E's Python ramp doesn't complete by week 3** — E is the second-largest Python contributor; if the ramp takes 3+ weeks instead of 2, REST endpoints + RBAC + PDF reports compress against the wall in weeks 14–17 | E + D + F | Pair E with F intensively in week 1–2; if ramp is incomplete by end of week 2, have F absorb E's REST endpoint scope and reduce PDF reports to the minimum (defer admin user-mgmt UI backend support to maintenance) |
| **DRS webapp framework decision slips past week 2** — G's scaffolding cannot begin until Angular-vs-React is signed off; every week of delay compresses G's downstream surfaces (monitor-scan, log viewer, IP config) against the back end of the build | G + D + project lead | Decision gate at end of week 1 (not end of week 2) with project-lead final call; D pre-evaluates both options in week 0 (component-library availability, build-toolchain fit with Windows WS2, accessibility primitives); if G's primary skill is React, default to React with no further escalation |
| **G — DRS webapp scope creep** — customer requests additional WS2-side surfaces (custom report builder, multi-variant comparison views, real-time alerting) mid-build | G + project lead | Lock weeks-12-onwards scope at end of week 11; new surfaces are chargeable additions per [Risk Register P3](risk-register.md); G's Angular component library makes additive views ~1 day each, but each one is logged and estimated |
| **G ↔ E contract drift** — both touch drs-server's REST + WebSocket surface; uncoordinated changes risk a webapp / SG-frontend divergence that ships separately and breaks under integration test | G + E + D | OpenAPI is the single source of truth for both clients; D reviews any endpoint signature change for both consumers in the same PR; weekly G+E sync from week 3 onward |
| **STK COM 12 patch-version drift breaks `Sg.App` mid-sprint** | B | Pin STK 12 minor version per development environment; CI runs integration tier on the pinned version; the ~14 documented gotchas in [v2 archive §25.3.5](specs/v2-tech-stack-archive.md) are version-sensitive |
| **`drs-server` async session leak under sustained load** | F + D | Load-test from week 6 onward (synthetic Kafka producer at target rate); session-scope-per-message rule enforced in PR review by D |
| **C++ parser bug discovered late in customer-priority variant** | C | Golden-frame test corpus per variant from day 1; fuzz harness from week 9; D reviews every parser PR against the golden-frame fixture for correctness |
| **ICD codegen tool produces output that drifts from hand-written first-variant code** | A | Regenerate first-variant skeletons in week 7–8 and diff against hand-written; treat any divergence as a codegen bug; A owns both sides so the feedback loop is internal |
| **Customer adds a non-standard ICD** (proprietary framing, no `extract_frame` magic-byte scan, e.g. NMEA ASCII variant) | C + project lead | Triage on receipt; bridge can special-case the parser; chargeable scope addition |
| **`Sg.App` extension scope creep** — customer asks for a 4th typed entity beyond Transmitter / Receiver / Antenna | B + project lead | The typed-entity pattern is well-understood (~1.5 days each per [Developer Handbook §8](developer-handbook.md#8-how-to-add-a-new-stk-entity-type)); chargeable scope addition; no architectural impact; A can pair on the addition since polyglot |
| **D's review bandwidth saturated as PR volume scales in weeks 6–13** | D + project lead | Weekly review-load triage; F + A escalate review priority for critical-path PRs; non-critical PRs can wait 24 h for D |
| **Path 3 latency exceeds IRS budget under Integrated load** — drs-bridge → Sg.App → WS2 PostgreSQL round-trip during sustained scenario execution + concurrent telemetry writes | F + B + D | Measured at Phase 4 (week 11) and re-measured at Phase 5 (week 13) per [§6.2](#62-per-phase-test-scope-and-pass-criteria). Mitigation already specced: in-memory `computed_links` cache on Sg.App ([§3.3](#33-b-c-specialist--sgapp-extension)), implementable in 2–3 days. Engage only if measurements warrant. |

---

## 8. Acceptance gates and what "done" means

Each gate requires both functional completion AND a passing integration test (see §6 for what each phase's test covers). Functional success is necessary but not sufficient — the gate doesn't pass until the cumulative test suite passes.

| Gate | Week | "Done" definition | Integration test required |
|---|---|---|---|
| **Kick-off** | 0 | Plan approved; team in place; development environments operational on each engineer's workstation | (none — pre-build) |
| **Contracts frozen** | 2 | C++ parser ABI signed off; YAML profile schema signed off; `IScenarioBackend` extensions signed off; OpenAPI spec for `drs-server` signed off (including DRS-webapp endpoint surface); DRS webapp framework decision (Angular / React) signed off; E's Python ramp on track | Phase 1 test passes (§6.2) |
| **First end-to-end** | 5 | One hardware variant — bytes from a simulated device land as a row in TimescaleDB; the SG-side operator UI on WS1 shows the row in real-time; the DRS webapp on WS2 shows live data for the same variant | Phase 2 test passes; Phase 1 still passes |
| **Random mode** | 8 | At least 4 variants operational in Random mode end-to-end; 1,000 msg/s sustained in load test | Phase 3 test passes; Phases 1–2 still pass |
| **Scenario mode** | 11 | At least 1 variant operational in Scenario mode (compute → DB → drs-bridge → device → UI); 1,500 msg/s sustained | Phase 4 test passes; Phases 1–3 still pass |
| **Integrated mode** | 13 | At least 1 variant operational in Integrated mode (DRS → entity controller app → response → DB → UI); 9–12 variants total in scope | Phase 5 test passes; Phases 1–4 still pass |
| **All priority variants + DVD packaging** | 15 | All customer-priority variants operational; DVD install dry-run passes on clean two-workstation deployment | Phase 6 test passes; Phases 1–5 still pass |
| **Performance + acceptance prep** | 17 | 2,000 msg/s sustained for ≥ 30 minutes without degradation; query-latency budgets met under that load; customer acceptance test prep complete | Phase 7 (full cumulative) test passes |

---

## 9. References

- [Executive Brief](executive-brief.md) — top-level scope, cost, timeline.
- [Architecture Overview](architecture-overview.md) — what gets built (component responsibilities, data flows, layered model).
- [Decision Record](decision-record.md) — architectural commitments that constrain the plan.
- [Developer Handbook](developer-handbook.md) — the day-to-day reference for the engineers executing this plan.
- [Risk Register](risk-register.md) — programme-level and engineering risks tracked across the build.
- [v2 tech-stack archive §23](specs/v2-tech-stack-archive.md) — the original staffing analysis (5-person specialist team), archived for historical comparison with the realised 7-person team (6 core + G for DRS webapp) this plan describes.
