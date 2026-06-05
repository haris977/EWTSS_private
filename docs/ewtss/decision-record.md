# EWTSS v2 Architecture — Decision Record

**Audience:** architects, technical reviewers, future maintainers, customer architects.

**Purpose:** the canonical list of architectural decisions for EWTSS v2. One decision per section, ADR-style. Every entry states what was chosen, the empirical or constraint-based reason, what was rejected and why, and a pointer to the source spec for fuller context.

**Status convention:** *Accepted* = current canon. *Superseded by ADR-XXX* = replaced; kept for audit. *Deferred* = decision recorded but not yet activated.

This document supersedes the alternatives-analysis sections of [`v2-tech-stack-archive.md`](specs/v2-tech-stack-archive.md). The original v2 doc remains the implementation-detail reference; this Decision Record is the source of truth for architectural commitments.

---

## ADR-001 — Frontend architecture: Hybrid (WPF primary, browser deferred)

**Status:** Accepted 2026-05-02. Mode A active development on `feat/mvp4.5-dto-boundary`. Mode B deferred until customer-funded.

**Decision:** Hybrid frontend. Mode A — `Sg.App.exe` (C# WPF + STK ActiveX in-process) — is the primary deliverable. Mode B — `Sg.Server.exe` (ASP.NET Core) + `Sg.Web` (Angular + CesiumJS SPA) — is an opt-in future addition that reuses the same `Sg.Domain` (DTOs, `IScenarioBackend`, `StkScenarioBackend`).

**Why:** MVP4.5 validated WPF + STK ActiveX as STK-Insight-grade fidelity for power users on Windows workstations. MVP3 validated Cesium for browser delivery. The Hybrid keeps the validated desktop deliverable while preserving a low-cost upgrade path; cost of optionality is bounded because 90% of Mode B's prerequisites already shipped in MVP4.5 (DTO contracts, JSON round-trip tests, namespace fence, backend-mode DI).

**Rejected:**
- Pure Cesium (Option E only) — foregoes STK Insight fidelity for power users.
- Pure WPF, no browser path — forecloses customer browser requests; cheap optionality already shipped.
- Embedded HTTP listener inside WPF — re-introduces HTTP latency in the WPF data path; disqualified by Mode A perf goal.
- Symmetrical out-of-process backend from day 1 — pays HTTP latency on every WPF interaction; disqualified by MVP4.5 smooth-pan validation.

**Refs:** [`hybrid-frontend-design.md`](specs/hybrid-frontend-design.md), v2 §22.7, §25.4.

---

## ADR-002 — Telemetry stack: three-service partial SOA (Option B, scenario role replaced)

**Status:** Accepted 2026-04-07; partially superseded by ADR-001 (scenario authoring is now C#).

**Decision:**
- `drs-server` — Python 3.12 / FastAPI async — Kafka consumers, TimescaleDB writes, WebSocket push to telemetry clients. **Unchanged.**
- `drs-bridge` — Python config + C++ shared libs — asyncio TCP servers, one YAML profile per hardware variant. **Unchanged.**
- ~~`sg-service` (Python) — scenarios, STK computation, GIS, RBAC, PDF reports~~ — **replaced** by C# in the Hybrid (`Sg.App` for Mode A; `Sg.Server` for Mode B).

**Why:** Hardware telemetry is concurrency-heavy and protocol-driven; Python+C++ is well-suited and team-familiar. Scenario authoring + STK computation is COM-centric and benefits from C#'s native typing of the STK API.

**Rejected:**
- Single monolith — large surface, doesn't align with team specialisations or process-isolation needs for STK COM.
- Per-hardware microservice — overkill for the variant count (~12); YAML-profile-per-variant within `drs-bridge` is sufficient.

**Refs:** v2 §3, §6, §7.

---

## ADR-003 — Database: TimescaleDB (PostgreSQL 16 + Timescale 2.x)

**Status:** Accepted 2026-04-07.

**Decision:** TimescaleDB hypertables for telemetry time-series; regular PostgreSQL tables for scenarios, users, RBAC.

**Why:** Original MySQL deployment hit perf degradation after ~10min of sustained telemetry writes (the v2 trigger). TimescaleDB hypertables are designed for that ingest pattern and remain SQL-compatible.

**Rejected:**
- MySQL (current production) — perf-degradation incident is the precipitating event for v2.
- InfluxDB — less SQL-friendly; team familiarity favors PostgreSQL ecosystem.
- ClickHouse — overkill for the ingest volume; ops cost not warranted.

**Refs:** v2 §4.4.

---

## ADR-004 — Message bus: Kafka KRaft single-node

**Status:** Accepted 2026-04-07.

**Decision:** Apache Kafka 3.x in KRaft mode (no ZooKeeper). `aiokafka` library for both producer (`drs-bridge`) and consumer (`drs-server`).

**Why:** Replay + persistence semantics match the telemetry pipeline's need to recover from consumer downtime. KRaft removes ZooKeeper as an ops dependency.

**Rejected:**
- ZooKeeper-backed Kafka — deprecated by Apache; KRaft is the path forward.
- RabbitMQ — pub-sub-only semantics don't match the replay/persistence requirement.
- Custom TCP routing — reinvents the wheel; loses Kafka's tooling ecosystem.

**Refs:** v2 §3.

---

## ADR-005 — STK access: C# COM in-process

**Status:** Accepted 2026-05-02.

**Decision:** `AGI.STKObjects.Interop` PIA via C# COM, in-process within whichever host owns the deliverable (`Sg.App` in Mode A, `Sg.Server` in Mode B). The same `StkScenarioBackend` implementation runs in both hosts.

**Why:** STK's API is designed primarily for C#/.NET (more natural typing, fewer workarounds, in-process latency). MVP4.5 documented the gotchas; they are tractable. Python `agi.stk12` was Option B's path; the Hybrid replaces it with C#.

**Rejected:**
- Python `agi.stk12` as primary — sub-process and `ProcessPoolExecutor` overhead, COM threading complexity. Used by Option E; not chosen for the Hybrid.
- STK Connect text commands as primary — slower than typed COM, weaker audit trail. Used selectively (e.g. `Position.AssignGeodetic` style operations the typed pattern silently no-ops on; see ADR-014).

**Refs:** v2 §25.3, §25.3.5; [`mvp4.5-dto-boundary-and-perf-design.md`](specs/mvp4.5-dto-boundary-and-perf-design.md).

---

## ADR-006 — STK contract boundary: `IScenarioBackend` + DTO records

**Status:** Accepted 2026-05-01.

**Decision:** All view-models (in any frontend) and HTTP endpoints (in Mode B) depend on the `IScenarioBackend` interface and pure DTO records in `Sg.Domain.Contracts`. No COM types may leak into Contracts (enforced by `ContractsNamespaceFenceTests`). All DTOs are JSON round-trip safe (validated by `DtoJsonRoundTripTests`).

**Why:** Mode A and Mode B share the same contract surface; Mode B's HTTP API is a 1:1 mapping of `IScenarioBackend` methods. The DTO boundary is what keeps the WPF deliverable fast (zero serialisation in the data path) while keeping the browser future reachable (DTOs already serialise cleanly).

**Rejected:**
- Direct COM types in view-models — couples view-models to STK; blocks browser-future and Mock-for-test.
- Per-entity-kind backend interfaces (`IAircraftBackend`, `IFacilityBackend`, etc.) — tried in MVP4 baseline; deleted in MVP4.5 because the consolidated `IScenarioBackend` reduces 6 interfaces + 6 fakes to one of each.

**Refs:** [`mvp4.5-dto-boundary-and-perf-design.md`](specs/mvp4.5-dto-boundary-and-perf-design.md), v2 §25.3.

---

## ADR-007 — STK entity catalogue: typed entities, not metadata-driven

**Status:** Accepted 2026-05-02.

**Decision:** Each supported STK entity type has a typed DTO + `IScenarioBackend.Get/UpdateXxx` pair + dedicated WPF panel. Today: Aircraft, Facility, AreaTarget, Sensor, CoverageDefinition, FigureOfMerit (6). Forward catalogue: Transmitter, Receiver, Antenna (~3 more); maybe Satellite if scope expands.

**Why:** EWTSS's entity vocabulary is small and fixed (~10 types). The break-even point for a metadata-driven generic editor (schema + Connect-command translator + sub-model dropdowns + unit-pref orchestration) sits well past EWTSS's actual scope. Compile-time safety, audit clarity, and direct debuggability of typed code outweigh the per-entity boilerplate savings.

**Rejected:**
- Metadata-driven (JSON descriptors + generic property panel + Connect commands) — framework cost > savings for ~10 types; STK's silent-failure surface compounds debugging when one abstraction layer further from typed C#. Documented in [`metadata-driven-entity-editor-evaluation.md`](specs/metadata-driven-entity-editor-evaluation.md) with explicit revisit triggers.

**Refs:** Same.

---

## ADR-008 — Desktop GUI: WPF + WindowsFormsHost (not pure WinForms)

**Status:** Accepted 2026-05-01 (after a Phase 1 WinForms-rewrite spike).

**Decision:** WPF for the Mode A desktop deliverable. STK ActiveX hosted via `WindowsFormsHost`. The reference-repo `EWTSS_CSP_POC` uses the same stack.

**Why:** Branch `feat/mvp4.6-winforms-host` (Phase 1) tested whether WPF airspace was the cause of MVP4.5's pan glitchiness. It was not — the cause was permanent STK COM event subscriptions (see ADR-013). Pan smoothness was achieved on WPF + WindowsFormsHost without rewriting to WinForms. The branch is preserved for history; the rewrite was a wrong-trail diagnosis.

**Rejected:**
- Pure WinForms — empirical evidence above. Same fix applies on WPF; rewrite would be ~1 week of mechanical UI port for no perf gain.
- WPF without `WindowsFormsHost` (3D rendered via WPF native primitives) — STK ActiveX is the only first-party renderer; WPF can't display the STK globe directly.
- MAUI / WinUI 3 — not validated against STK ActiveX; would re-incur the diagnostic cycle MVP4.5 already paid.

**Refs:** v2 §25.3.5.

---

## ADR-009 — Browser SPA framework (Mode B): Angular + CesiumJS

**Status:** Decided in advance; activation deferred until Mode B is funded.

**Decision:** When Mode B ships, the SPA is Angular (TypeScript) hosting a CesiumJS scene, served as static assets by `Sg.Server`. Loads CZML emitted by the server (Phase B1 read-only) or DTOs over REST (Phase B2 authoring).

**Why:** Team familiarity is Angular (per v2 §8.3 decision record). CesiumJS validated by MVP3 for STK CZML rendering; mature pipeline.

**Rejected:**
- React + Cesium — team is Angular-fluent; no compelling reason to switch.
- ArcGIS Maps SDK — terrain pipeline + license cost vs CesiumJS open-source.
- Native browser app without a framework — scope mismatch.

**Refs:** v2 §8, §22.4.

---

## ADR-010 — Browser host: Electron (Mode B desktop browser deployment)

**Status:** Accepted 2026-04-07; activation deferred until Mode B ships.

**Decision:** When Mode B is delivered to operators on Windows workstations, the SPA is bundled in Electron. Air-gapped deployments do not depend on system browsers.

**Why:** Bundled Chromium avoids WebView2 dependency on air-gapped workstations; offline terrain tile serving (Martin) integrates more cleanly with a controlled browser runtime.

**Rejected:**
- Tauri — depends on system WebView; less reliable on air-gapped deployments.
- System browser only (no Electron) — assumes operator workstations have a modern browser installed and updated; not safe assumption for offline defense systems.

**Refs:** v2 §8.2.

---

## ADR-011 — Scenario file format: STK native (.sc, .vdf)

**Status:** Accepted 2026-04-07.

**Decision:** Scenarios are saved and loaded in STK's native `.sc` (text) or `.vdf` (binary, optionally password-protected) format. No proprietary format.

**Why:** Round-trip compatibility with STK Desktop / Insight is a customer requirement — scenarios authored in EWTSS must open cleanly in STK Insight and vice versa.

**Rejected:**
- Custom JSON / YAML scenario format — round-trip with STK loses information; doubles the test surface.
- CZML as canonical save — CZML is a visualisation format, not a scenario authoring format; lacks STK-specific metadata.

**Refs:** v2 §5.2.

---

## ADR-012 — STK licensing: one engine seat per deployment, install-time mode commitment

**Status:** Accepted 2026-05-02; clarified 2026-05-03 against the RFQ-defined two-workstation deployment shape.

**Decision:** A deployment commits to **either Mode A or Mode B at install time**, not both. The two modes do not coexist within a single deployment. Only WS1 (SG — Scenario Generator) hosts an STK-bearing process; WS2 (DRS) hosts no STK process. This means **one STK Engine licence seat per deployment** — held by the active WS1 process. `MVP4_BACKEND` is an install-time / configuration variable, not a runtime toggle.

**Why:** STK Engine licensing is per-process. The two-workstation deployment shape and the install-time mode commitment together ensure the licence count is unambiguous (always 1 per deployment) and the "two STK processes on one machine" failure mode is unreachable by construction. Switching modes after deployment is a WS1 reinstall, not a runtime concern.

**Rejected:**
- Coexisting Mode A and Mode B on the same workstation, gated by runtime env var — possible but would require an additional STK seat or licence-juggling, and no customer has asked for it. Out of scope; revisit if needed.
- Distributed STK across multiple workstations — STK Engine cannot be sharded; one process owns the engine for a given scenario. Not viable.
- Licence pooling / virtualisation — not supported by AGI's licensing terms.

**Refs:** [`hybrid-frontend-design.md`](specs/hybrid-frontend-design.md), v2 §18, [Deployment Guide §1](deployment-guide.md), [Architecture Overview §2.1](architecture-overview.md).

---

## ADR-013 — STK COM event subscription: on-demand only, never permanent

**Status:** Accepted 2026-05-01 (load-bearing for Mode A perf).

**Decision:** Zero permanent subscriptions on STK ActiveX controls (`AxAgUiAxVOCntrl`, `AxAgUiAx2DCntrl`). Mouse and editing events are subscribed only while the controller is in a placement or editing mode; unsubscribed on return to Idle.

**Why:** Empirically verified in MVP4.5: each permanent subscription on `MouseDownEvent` / `MouseMoveEvent` / `OnObjectEditing*` adds COM marshalling overhead per render tick, manifesting as a 1–2 second post-pan-release stall. The handler body is irrelevant; the cost is in the dispatch. The reference repo (`EWTSS_CSP_POC`, validated-smooth-pan) operates the same way.

**Rejected:**
- Permanent subscriptions with early-return handlers — empirically does not work; the dispatch cost remains.
- Pure WinForms hosting (instead of WPF + `WindowsFormsHost`) — also empirically does not fix it; ADR-008 documents the Phase 1 WinForms spike that confirmed this.

**Refs:** v2 §25.3.5.

---

## ADR-014 — Facility position write: `Position.AssignGeodetic` direct

**Status:** Accepted 2026-05-02.

**Decision:** Facility position is set via `IAgFacility.Position.AssignGeodetic(lat, lon, alt)` directly. Other position-write patterns are not used.

**Why:** The alternative pattern `ConvertTo(ePlanetodetic) + Assign(geo)` (snapshot-and-writeback) silently no-ops on this STK 12 build; the facility stays at STK's default position (40.04 °N, 75.60 °W — AGI HQ in Exton, PA) regardless of what's set on the snapshot. The reference repo uses `AssignGeodetic` directly; supersedes earlier guidance in v2 §25.3.3 row 5.

**Rejected:**
- `ConvertTo(ePlanetodetic) + Assign(geo)` — silent failure on this STK build.
- Connect command `SetPosition Geodetic` — works but slower; reserved as fallback if `AssignGeodetic` fails on a future STK version.

**Refs:** v2 §25.3.5.

---

## ADR-015 — Drag-edit commit: `ApplyObjectEditing()` on user MouseUp

**Status:** Accepted 2026-05-02.

**Decision:** When the user drags an STK edit handle and releases, `Sg.App` explicitly calls `_globe3D.ApplyObjectEditing()` to commit the in-progress drag to STK's COM entity state. Bridged via a `MouseUpEvent` subscription that's active only during `EditingEntity` mode.

**Why:** STK's drag handles update the visual position only; the underlying COM state stays at pre-drag values until something explicitly commits. `OnObjectEditingApply` / `OnObjectEditingStop` events fire only from the programmatic `ApplyObjectEditing()` / `StopObjectEditing()` methods, not from user mouse interaction. Without an explicit commit, `GetXxx` returns the OLD state and Apply pushes those OLD values back, undoing the drag.

**Rejected:**
- Wait for `OnObjectEditingApply` from user action — does not fire (verified empirically).
- Show STK's own edit toolbar with Apply/OK/Cancel buttons — the embedded ActiveX in our WPF host doesn't expose that toolbar.

**Refs:** v2 §25.3.5.

---

## ADR-016 — Placement / edit UX: match STK Desktop where possible

**Status:** Accepted 2026-05-02. **Revised 2026-05-04.**

**Decision:** Placement and editing UX should mimic STK Desktop's gestures and discoverability as closely as the embedded ActiveX environment permits. The specific keystrokes / gestures are **not architectural commitments** — they're implementation choices subject to revision as the v2 hardening team reviews STK Desktop UX rigorously.

**MVP4.5's current implementation:**
- Aircraft / area-target placement: left-click for waypoints / vertices, `Enter` to finalize, `Esc` to cancel.
- Editing entities on the globe: `StartObjectEditing` shows STK's native drag handles; user drags + releases; `Enter` applies the edit, `Esc` cancels.

**Why MVP4.5 settled on keyboard finalize specifically:** two STK ActiveX constraints forced the choice. **Right-click is consumed by STK's camera operations** on the 3D globe and never reaches subscribers as `button == 2`. **STK's `DblClick` event fires on every single click**, not just real double-clicks (verified empirically — see [v2 archive §25.3.5](specs/v2-tech-stack-archive.md)). With both standard finalize gestures unreachable, `Enter` at the WPF Window's `OnPreviewKeyDown` was the most reliable focus-agnostic finalize gesture available.

**Architectural principle (load-bearing):** the placement / edit experience should feel as close to STK Desktop as the constraints allow. Specific keystrokes are open for revision.

**Open for v2 hardening:**
- Review STK Desktop's actual placement / editing UX (drawing tools, "Insert by …" flows) and identify divergences from MVP4.5's mechanics.
- Consider toolbar buttons (always reachable, regardless of focus) as a complement or alternative to keyboard gestures.
- Where the same gesture works in STK Desktop and is reachable in our embedded ActiveX environment, prefer matching it.
- The two STK COM constraints above (right-click consumed, DblClick fires-per-click) are facts of the embedded ActiveX environment — they constrain the option space but don't dictate the chosen UX.

**Rejected** (specific gestures, not the architectural principle):
- Right-click finalize — empirically unreachable.
- STK `DblClick` subscription — empirically fires per single click.
- MD-timestamp-based double-click detection only — fails when users click slower than `SystemInformation.DoubleClickTime`.

**Refs:** v2 §25.3.5; [v2 Execution Plan](v2-execution-plan.md) — STK Desktop UX review is a v2 hardening work item.

---

## ADR-017 — STK Mock policy: test-only, never runtime fallback

**Status:** Accepted 2026-04-07.

**Decision:** `FakeScenarioBackend` is for unit tests only. The application never falls back to a mock when STK Engine is unavailable; it fails-fast at startup with an actionable error.

**Why:** MVP1 lesson — runtime mocks drift from real STK behavior over time; developers built false confidence and shipped bugs that real STK exposed. Fail-fast prevents this; mocks remain valuable for fast iteration in tests.

**Rejected:**
- Runtime mock fallback to keep the UI usable when STK is unavailable — surfaces fake state as real, which is unsafe for mission-critical authoring.
- No mock at all (tests require STK) — slows the test loop intolerably; integration tests already cover the real-STK path.

**Refs:** v2 §25.5.

---

## ADR-018 — WS2 DRS webapp: required browser frontend on the DRS workstation, served by drs-server

**Status:** Accepted 2026-05-13.

**Decision:** WS2 hosts a browser-based DRS webapp, served by `drs-server`, for the DRS Engineer persona. The webapp is independent of the SG-side Mode A / Mode B selection — it is present in every deployment. Framework: **Angular preferred** for skill alignment with E (SG-side Angular dev) and Mode B future component-library reuse; **React acceptable fallback** if dictated by the assigned developer's primary skill. Final framework call signs off at end of week 1 of the v2 hardening phase.

**Why:** RFQ Annexure A.1 §B (Health status display of LRUs, sub-systems, communication links), §F (Manage Logs — Sent Message log, Receive Message log per variant), §2.5 (DRS Application), and the deployment topology in RFQ Figure 3.1 (DRS as a distinct workstation) together require an operator-facing DRS surface. The v1 legacy system has a dedicated `/drs/*` Angular route cluster (monitor-scan, per-variant views, IP configuration, drs-login) serving the same persona — confirming the two-persona model is the established operator mental model, not a new invention. The earlier v2 design treated WS2 as headless infrastructure, which left RFQ §B Health-status-display and per-variant message-log requirements without an operator surface; ADR-018 closes that gap.

The two operator personas (SG Operator on WS1, DRS Engineer on WS2) are distinct: the SG Operator authors scenarios and runs exercises; the DRS Engineer monitors hardware, configures network parameters, and performs DRS-side diagnostics. Consolidating both into a single surface would either bloat `Sg.App` with DRS-side concerns or require the DRS Engineer to use a Windows workstation, neither of which matches RFQ deployment topology.

**Implications:**
- `drs-server` (FastAPI on WS2) gains the responsibility of serving the DRS webapp static assets and the webapp's REST + WebSocket endpoints, in addition to its telemetry-consumer role.
- The WS2 deployment package includes the built DRS webapp; first-run install on WS2 verifies the webapp loads from `http://localhost:<drs-server-port>/` (or chosen path) without manual configuration.
- Team grows from 6 to 7: a dedicated DRS-side frontend developer (G, Angular preferred / React fallback) is on the team, owning the webapp surfaces across the 17-week build per [v2 Execution Plan §3.7](v2-execution-plan.md#37-g-angular-preferred-else-react--drs-webapp-lead).
- Detailed UX wireframes for the DRS webapp surfaces are a Milestone-1 design deliverable tracked as [Design Backlog B1.1](design-backlog.md#-b11--detailed-ux-wireframes-sg-operator-and-drs-engineer-surfaces).
- The DRS webapp consumes drs-server's REST + WebSocket endpoints only — it never opens TCP sockets to hardware directly, never queries TimescaleDB directly. The layer split is enforced as a structural rule per [Architecture Overview §2.5](architecture-overview.md#25-system-level-layered-model).

**Rejected:**
- **WS2 stays headless; DRS Engineer concerns absorbed into `Sg.App` on WS1.** Would force DRS Engineer to be on Windows, contradicting RFQ deployment topology that has DRS as a separate workstation; and the same persona-bloat objection that drove the SG / DRS split in v1.
- **Native desktop app on WS2 instead of a webapp.** Would add cross-platform desktop tooling (Electron or similar) without a corresponding benefit; a browser served locally by drs-server is simpler operationally and reuses existing FastAPI auth.
- **Defer DRS webapp to a post-acceptance phase.** Would leave the RFQ §B Health-status-display + §F Manage Logs operator surfaces unimplemented at customer acceptance — non-compliant with the milestone deliverables.
- **Vue or Svelte for the webapp framework.** Not rejected on technical grounds, but Angular preferred for the alignment + reuse reasons stated; React acceptable so the team's existing skills carry weight.

**Refs:** RFQ Annexure A.1 §B + §F + §2.5; [Operator Playbook §9](operator-playbook.md#9-drs-engineer-workflows-ws2-webapp); [Architecture Overview §3.9](architecture-overview.md#39-drs-webapp--drs-engineer-surface-ws2); [v2 Execution Plan §3.7](v2-execution-plan.md#37-g-angular-preferred-else-react--drs-webapp-lead); [Design Backlog B1.1 + B1.2](design-backlog.md#1-milestone-1-design-items).

---

## ADR-019 — mvp4 is the reference codebase; v2 production Sg.App lives in `sg-app/`

**Status:** Accepted 2026-05-19. **Revised 2026-05-20** — namespace separation accepted (was rejected on 2026-05-19; see "Revision note" below).

**Decision:** The `mvp4/` tree (`Sg.Mvp4.App`, `Sg.Mvp4.Domain`, `Sg.Mvp4.Tests`) remains in the repo as the **reference codebase** for STK invariants, MVP4.5's STK ActiveX integration patterns, and the validated DTO boundary. It does **not** evolve into the v2 product. The v2 production Sg.App lives in a sibling tree at `sg-app/` (`sg-app/Sg.App/`, `sg-app/Sg.App.Tests/`, `sg-app/sg-app.sln`), built on `IHost` composition + `IConfiguration` + `IHttpClientFactory` + Serilog, with the production infrastructure (`IExerciseStateService`, banner host, exercise lifecycle commands) that mvp4 deliberately omits.

Namespaces and assembly names are **distinct** as of the 2026-05-20 revision: mvp4 uses `Sg.Mvp4.App` / `Sg.Mvp4.Domain` / `Sg.Mvp4.Tests` (producing `Sg.Mvp4.App.dll` etc.) and sg-app uses `Sg.App` / `Sg.App.Tests` (producing `Sg.App.dll`). The directories on disk are `mvp4/Sg.Mvp4.App/`, `mvp4/Sg.Mvp4.Domain/`, `mvp4/Sg.Mvp4.Tests/`; the solution file is `mvp4/Sg.Mvp4.sln`.

**Revision note (2026-05-20):** The original ADR (2026-05-19) left both trees on the `Sg.App` namespace on the grounds that the two assemblies never share a solution. Three concerns drove the revision: (1) two `Sg.App.dll` outputs on disk during local development invite confusion in tooling that scans `bin/` directories (Serilog sinks, NSSM service registrations, NuGet sources); (2) editor "Go to Definition" and other navigation features cannot disambiguate when both projects are open in the same workspace; (3) the reference role is more visible in `using` statements when `using Sg.Mvp4.App;` reads explicitly as "the reference codebase" rather than yet another `using Sg.App;`. The cost — one mechanical rename across 88 files — was small enough to retire the ambiguity permanently.

**Why:** Surfaced during the B1.3 Time Sync spec-validation pass (corrigenda F18). Phase 5 of B1.3 was originally pathed at `mvp4/Sg.App/...` (the pre-rename name; now `mvp4/Sg.Mvp4.App/...`), which led an implementer to file `TimeSyncStatusDto` and `SyncBannerService` into the reference tree before discovering that mvp4 deliberately lacks the foundations Phase 5 needed (no exercise-control commands, no `Microsoft.Extensions.Http`, no `IConfiguration`, no Admin navigation surface). Those features are *intentionally* absent in mvp4 — mvp4 is the verification codebase, not the production application. Continuing to evolve mvp4 toward the production target would have erased its reference value (every change adds production-only concerns that mask the STK invariants it was built to verify), and would have forced the production app to inherit MVP4.5-era WPF idioms that newer v2 work (composition root via `IHost`, `appsettings.json`-driven config) wouldn't want.

The split makes both purposes explicit. mvp4 stays small, STK-focused, and re-readable as the reference; sg-app gets the production-grade infrastructure that lets B1.x feature work land cleanly.

**Implications:**
- New v2 product code lands in `sg-app/`. mvp4 receives changes only to keep its STK reference current (STK SDK upgrades, COM-quirk fixes); functional additions go to sg-app.
- Tasks 15–16 of the B1.3 plan that landed in `mvp4/` during the spec-validation run are explicitly retained as reference artefacts; the canonical implementations are in `sg-app/`.
- Any new B1.x plan that names files under `mvp4/Sg.Mvp4.App/...` is wrong — plans must target `sg-app/Sg.App/...` (or `mvp4/Sg.Mvp4.App/...` only if the target is the reference codebase, which is rare). [Plan pre-flight checklist](plan-preflight-checklist.md) Pass-1 enforces this.
- mvp4's `Sg.Mvp4.sln` and sg-app's `sg-app.sln` remain separate. Neither is loaded into the other. The output assemblies have distinct names (`Sg.Mvp4.App.dll` vs `Sg.App.dll`), so there is no possibility of bin-directory collision even if both ever land in the same deployment artefact.
- A future "rename mvp4 to make its reference status visible from the filesystem" decision is open (renaming to `.mvp4/` is a common UNIX convention for not-actively-developed reference material). Tracked separately; not part of this ADR.

**Rejected:**
- **Evolve `mvp4/Sg.Mvp4.App` into the production v2 Sg.App.** Forces mvp4 to grow exercise control, `IConfiguration`, Admin navigation, and time-sync wiring it was never designed for. Destroys its value as the STK invariants reference, since every production-only concern added to it dilutes the reference signal.
- **Delete `mvp4/`** outright. Loses the reference value at a time when the production application is too young to absorb STK-quirk lessons unaided. Lab triage of STK install-state and COM-edge cases still relies on the mvp4 patterns being available to compare against.
- ~~**Use distinct namespaces (`Sg.Mvp4.App` vs `Sg.App`)** to make the split obvious in `using` statements.~~ **Was rejected on 2026-05-19; accepted on 2026-05-20 (see revision note above).** The original rejection rationale ("two assemblies in different solutions; `using Sg.App;` is unambiguous in each project's compilation context") underweighted the editor / tooling / bin-directory ambiguity that surfaces in mixed-workspace development. Retained here for audit.

**Refs:** [B1.3 corrigenda F18](plans/time-sync-corrigenda.md); [main README.md](../../README.md#whats-here); [sg-app/Sg.App/README.md](../../sg-app/Sg.App/README.md); [Plan pre-flight checklist](plan-preflight-checklist.md).

---

## Decision dependency graph

```
ADR-001 (Hybrid frontend) ─┬─→ ADR-005 (C# COM in-process)
                           ├─→ ADR-006 (IScenarioBackend + DTOs)
                           ├─→ ADR-008 (WPF + WindowsFormsHost)
                           ├─→ ADR-009 (Angular + CesiumJS, Mode B)
                           ├─→ ADR-010 (Electron host, Mode B)
                           └─→ ADR-012 (one-engine licence rule)

ADR-006 (DTO boundary) ───→ ADR-007 (typed entity catalogue)
                            (ADR-006 made browser-future cheap; ADR-007
                             made it unnecessary to also pursue metadata)

ADR-008 (WPF) ─→ ADR-013 (no permanent COM subs — load-bearing for ADR-008)
                ADR-014 (AssignGeodetic — STK COM realities)
                ADR-015 (ApplyObjectEditing — STK COM realities)
                ADR-016 (STK Desktop UX fidelity — keyboard finalize is MVP4.5's pragmatic choice)

ADR-002 (telemetry SOA) ─┬─→ ADR-003 (TimescaleDB)
                         ├─→ ADR-004 (Kafka KRaft)
                         └─→ ADR-018 (WS2 DRS webapp served by drs-server)

ADR-019 (mvp4 reference / sg-app production split) ── independent
        (codebase organisation, not architectural alternatives)
```

ADR-013, ADR-014, and ADR-015 are *implementation invariants* that must hold for ADR-008 (WPF) to deliver its perf and correctness goals — losing any one of them empirically broke MVP4.5's deliverable, so they're load-bearing. ADR-016 is a different category: a UX-fidelity *principle* (match STK Desktop where possible) plus MVP4.5's pragmatic gesture choices. The principle is load-bearing; the specific gestures are open for revision in v2 hardening.

ADR-018 (WS2 DRS webapp) is the second-persona commitment derived from RFQ Annexure A.1 §B + §F + §2.5. It is independent of ADR-001's SG-side Mode A / Mode B choice — the DRS webapp is required in every deployment.

ADR-019 (mvp4 reference / sg-app production split) is codebase organisation rather than an architectural alternative: it decides where v2 product code lives versus where MVP4.5's STK-validation reference stays preserved.

## Open decisions (not yet recorded)

The following are anticipated but not yet decided. They become ADRs when committed:

- **Authentication / authorisation for Mode B HTTP API** — out of scope for v2 today; required before Phase B1 ships.
- **Real-time push from Mode B server to browser** — REST polling vs WebSocket vs SignalR vs gRPC streaming. Phase B1 design decision.
- **Air-gapped scenario sync between Mode A workstations and Mode B server workstation** — file-based (Save then Load), live mirroring, or out of scope. Customer-driven.
- **GIS layer** — TBD which tile pipeline (Martin, MapTiler, custom). Currently inherited from v2 §8.7 and applies to Mode B only.

## Revision history

| Date | Change |
|---|---|
| 2026-05-03 | Initial Decision Record extracted from the v2 tech-stack design. ADRs 001–017 recorded. |
| 2026-05-13 | ADR-018 added (WS2 DRS webapp as required component, Angular preferred / React fallback) following RFQ Annexure A.1 review. |
