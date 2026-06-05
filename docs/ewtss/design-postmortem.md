# EWTSS v2 — Design Post-Mortem (Pre-Mortem)

**Audience:** project lead, architecture lead, engineering team, customer programme manager.
**Purpose:** identify gaps, missing links, and failure modes in the current v2 design *before* they materialise in development, testing, or production. Anchored on RFQ Annexure A.1 + A.2 as the requirement bar; v1 legacy code at `E:\Sandbox\17-05-2025\` consulted for established operator behaviour where applicable. Honest review — what is missing, why it will fail, where it will fail, and how severely.
**Status authority:** this document is an analytical artefact, not a prescription. Items it surfaces feed the [Design Backlog](design-backlog.md), [Risk Register](risk-register.md), and (where decided) the [Decision Record](decision-record.md). Mitigation owners must be assigned before items become actionable.
**Severity scale:** 🔴 Critical (will block acceptance) · 🟠 High (will materially slow or degrade delivery) · 🟡 Medium (manageable but worth surfacing) · 🟢 Low (operational hygiene).

---

## 1. Framing — what this analysis covers and excludes

The design has been reviewed against three lenses:

- **Stage 1: Development (week 0 to week 17 of the v2 hardening phase).** Will the team be able to build to the design? Are the contracts crisp enough? Are the workstreams independent enough to actually run in parallel? Are dev-environment dependencies (licences, vendoring, cross-platform builds) realistic?
- **Stage 2: Testing.** Can the design be verified end-to-end? Are integration scenarios reproducible? Are non-functional requirements (perf, security, robustness) testable with the rigs described?
- **Stage 3: Production.** Will the system survive a real customer site for the 1-year warranty period? What goes wrong on day 1 of training, on day 30 of sustained load, on day 366 when something expires?
- **Cross-cutting:** documentation, deliverables, hand-off, IP rights, multi-language, accessibility.

Excluded from this analysis: (a) architecturally settled items already documented (the 19 ADRs); (b) items that exist as known design gaps and are already tracked in the [Design Backlog](design-backlog.md) — they are referenced but not re-litigated.

---

## 2. Stage 1 — Development gaps and failure modes

### 2.1 🔴 Critical: contracts that are claimed "frozen at week 2" but have no artefact yet

The execution plan's Phase 1 acceptance gate at end of week 2 requires four contract artefacts to be signed off: parser ABI, YAML profile schema, `IScenarioBackend` extensions, OpenAPI spec for drs-server. **The parser ABI was closed 2026-05-21 by the reference parser template (see row 1 below); the other three still have no concrete artefact in the repo today:**

| Contract | Where it should live | Current state | Failure mode |
|---|---|---|---|
| ~~**C++ parser ABI**~~ — **CLOSED 2026-05-21** | [`drs-bridge/parsers/reference/include/reference_parser.h`](../../drs-bridge/parsers/reference/include/reference_parser.h) is the canonical 4-symbol header; a buildable reference implementation lives at [`drs-bridge/parsers/reference/`](../../drs-bridge/parsers/reference/) with CMake config + a pytest integration test exercising the ctypes binding end-to-end on CI. | Closed. New variants copy the reference directory + modify (see [Developer Handbook §9.3](developer-handbook.md#93-c-parser-interface-contract)). | Original gap retained for audit: without a concrete header the first-variant parser would have become the de-facto ABI definition, locking in choices made without architectural review. |
| **YAML profile schema** | `drs-bridge/profiles/_schema.yaml` or similar; or as JSON Schema | Not in the repo. Mentioned but not specified | Each variant's YAML drifts in shape; drs-bridge dispatcher accumulates per-variant special cases. The same anti-pattern that v1 hit (per [legacy-system-audit.md](legacy-system-audit.md) AP-11). |
| **`IScenarioBackend` extensions** for Transmitter / Receiver / Antenna | `mvp4/Sg.Mvp4.Domain/Contracts/` (DTOs) + `IScenarioBackend.cs` (methods) | Existing `IScenarioBackend` has the 6 entity-types in scope today. The 3 new ones (Transmitter / Receiver / Antenna per execution plan §3.3) are not specified — what fields, what ranges, what relationships to existing entities. | B has to invent the contract during the build. Late discovery that an emitter requires a `pulse_modulation` field B didn't model. |
| **OpenAPI spec for drs-server** | `drs/drs-server/openapi.yaml` or generated from FastAPI | Not in the repo. The execution plan §4.1 names "OpenAPI spec frozen" as a gate item, but the spec itself has no existence in design today. | E + G + A all consume this surface. Without a frozen spec at week 2, E's `/measurements` endpoints, G's webapp consumption, and A's `ScenarioPublisherClient` all develop against moving targets. |

**Mitigation:** make the four-contract delivery an explicit pre-build milestone (week −2 to week 0). Each artefact has an owner: D for parser ABI + YAML schema; B for `IScenarioBackend` extensions; F for OpenAPI. Sign-off requires the consuming party (A for ABI; G for OpenAPI; etc.) to accept the artefact in writing.

### 2.2 🟠 High: IRS documents (per-variant) — provenance and versioning are unspecified

RFQ §1.6 mandates that DRS sends and receives messages "as per the available IRS." Today's design assumes the IRS for each of the 12+ hardware variants will arrive from the client; only one variant (COMM DF) has a reference IRS doc in the repo ([icd-reference-comm-df.md](icd-reference-comm-df.md)) which is itself flagged as the template.

| Concern | Failure mode |
|---|---|
| **Where do IRS documents live?** | If IRS docs are delivered ad-hoc (email, PDFs in a SharePoint), C cannot trace which version of an IRS produced a parser. Mid-build IRS revisions break parsers silently. |
| **How are they versioned?** | The icd-reference template doesn't specify how to track IRS revisions. Customer revs an IRS at week 8; C rebuilds the parser, but the previous test corpus was for v1.0 of the IRS and is no longer valid. |
| **Who owns IRS-to-parser traceability?** | No documented owner. Required for the audit trail at acceptance (Milestone 2 STR / STD). |

**Mitigation:** dedicated `docs/ewtss/icds/` directory or external doc store with version-controlled IRS documents per variant. Each parser library declares its IRS version (build-time constant). Golden-frame test corpus is tagged with IRS version. New backlog item recommended: **B1.17 — IRS document store and version management process.**

### 2.3 ✅ RESOLVED: cross-platform build pipeline → single-platform (Windows)

**Resolved 2026-05-14:** WS2 switched from Linux to Windows ([Deployment Guide §1](deployment-guide.md) + [v2 Execution Plan §3.4](v2-execution-plan.md#34-c-c-specialist--drs-bridge-parser-libraries) updated). Both WS1 and WS2 are Windows, so the cross-platform build matrix collapses to a single platform.

C++ parser libraries build as `.dll` only. CI uses a single Windows runner with pinned MSVC version captured in the parser CMakeLists. Pre-built `.dll` artefacts are committed to `packages/parsers/` so the DVD install does not require a C++ toolchain at the customer site.

**Original gap context, retained for audit:** Prior to the 2026-05-14 decision, the design had WS1 = Windows 11 and WS2 = Linux with C++ parser libraries needing to build as both `.so` and `.dll`. That introduced a cross-platform build matrix the execution plan didn't specify (which CI runner, which OS images, which glibc / MSVC pins). The Linux-on-WS2 assumption was not RFQ-mandated; v1 legacy deployments run on Windows. Switching WS2 to Windows for deployment consistency with WS1 (and with v1 customer-IT familiarity) eliminates the gap entirely.

### 2.4 🟠 High: STK 12 dev licence allocation

RFQ Milestone 1 #4 ships "a lease license of STK development Software for one year." Singular. The team has 7 engineers. At minimum B, D, and the integration-test rig need STK locally; ideally also F for compute-pipeline review and A for ResponseRouter integration testing.

**Failure mode:** floating-licence contention. B holds the lease overnight running long compute tests; A blocked on integration testing. Slip risk multiplied by team size.

**Mitigation:** clarify with the client whether the "lease license" is a single seat or a multi-seat lease; if single, budget for additional dev seats; document the seat-sharing protocol; install the dev licence on a shared dev STK box accessible over LAN if the licence type permits.

### 2.5 🟡 Medium: Sg.Domain new typed entities — DTO shape unspecified

The execution plan §3.3 says B will build Transmitter / Receiver / Antenna typed entities at 1.5 days each per [Developer Handbook §8](developer-handbook.md#8-how-to-add-a-new-stk-entity-type). The pattern is well-understood; the per-entity DTO is not.

| Entity | What fields does RFQ §1.2 / §2.3 imply? | What's specified in design? |
|---|---|---|
| **Transmitter** | Frequency, power, antenna pattern, scan pattern, modulation | None specified |
| **Receiver** | Frequency range, sensitivity, antenna characteristics, noise figure | None specified |
| **Antenna** | Gain pattern (per RFQ §2.3 raster / sector), polarisation, beamwidth | None specified |

**Failure mode:** B builds DTOs from imagination; integration with STK compute reveals missing fields (e.g. STK link analysis needs polarisation and the DTO doesn't carry it); rework needed at week 9. Possibly also fails RFQ §1.2 audit (correct emitter parameters per COM / RADAR / SJRR / AUS / PADS subtypes).

**Mitigation:** specify the three DTOs in Milestone-1 SRS work; review against RFQ §2.3 antenna patterns and STK Engine's link analysis inputs. Add as a Milestone-1 backlog item.

### 2.6 🟡 Medium: air-gap dependency vendoring — process undefined for mid-build additions

The design says all Python wheels and C++ deps must be vendorable for air-gap install. Today this works for the dependencies already chosen. The process for adding a new dependency mid-build is undocumented:

- Who approves new deps?
- Who fetches the wheel + transitive deps + verifies they install offline?
- Where do new wheels land in the repo (`packages/`)?
- What if the new dep is non-pure-Python (e.g. a C-extension wheel that's not Windows-x64-compatible, or that targets a different Python ABI)?

**Failure mode:** F discovers at week 6 that `aiofiles` is needed for streaming exports. F installs it via `pip install aiofiles`. Tests pass on dev. Acceptance install on a freshly-imaged WS2 fails with "no matching distribution found" because the wheel was never vendored. Risk R5 in the register catches the air-gap vendoring problem but not the process gap.

**Mitigation:** PR checklist item: "new dependency added? wheel vendored under `packages/`, install verified offline." D enforces in review. Documented at the top of `packages/README.md` (which doesn't yet exist).

### 2.7 🟡 Medium: git branching strategy

Not specified. The current MVP4.5 work is on `feat/mvp4.5-dto-boundary`. The 17-week v2 hardening phase will produce many parallel streams (F, A, B, C, E, G all touching disjoint areas).

**Failure mode:** trunk-based development on `main` causes long-running PRs to hit merge conflicts on shared files (OpenAPI spec, RBAC schema, drs-server entry points). Or — feature-branch fragmentation: G's webapp branch stays out of `main` for 8 weeks and rebases become unmanageable.

**Mitigation:** decide: trunk-based with short-lived branches and feature flags; or GitFlow-lite (`develop` + per-feature branches + weekly integration). Document in [Developer Handbook §16](developer-handbook.md#16-pull-request-checklist) before week 0.

### 2.8 🟡 Medium: CI infrastructure — partial (workflow exists, runners unavailable)

The execution plan §6 talks about an integration test rig that runs at phase gates. Status as of 2026-05-21:

- **Workflow YAML in place** — a 4-job matrix (`drs-server`, `drs-bridge`, `drs-webapp`, `sg-app`) was authored and committed; `mvp4` deliberately excluded for licence reasons ([ADR-019](decision-record.md)).
- **Currently paused** — GitHub Actions hosted runners are disabled at the GitHub Enterprise org level for this repository. The workflow has been parked at [`.github/disabled/ci.yml`](../../.github/disabled/ci.yml) (see [`.github/disabled/README.md`](../../.github/disabled/README.md)) until runners are provisioned. Revival is a one-line `git mv` once policy changes.
- **Open questions:** test parallelisation (unit on every push vs integration nightly) and physical rig home (dedicated test workstation on LAN vs cloud-hosted vs D's workstation) are still unspecified.

**Failure mode:** no automated regression coverage between phase gates while runners stay disabled. Phase 4 integration breaks Phase 2 flow; no one notices until the cumulative test at end of phase 4.

**Mitigation:** pursue hosted-runner enablement with the GitHub Enterprise Administrator; failing that, stand up a self-hosted runner on a team workstation (the team already has Python / Node / .NET / STK 12 installed). Workflow revival is mechanical once a runner is available.

### 2.9 🟢 Low: code review SLA

D is the cross-stack lead and reviewer. Specialist risk in [Execution Plan §7](v2-execution-plan.md#7-specialist-risks-per-person) notes "D's review bandwidth saturated as PR volume scales in weeks 6–13." No SLA is specified — what's the expected turnaround?

**Failure mode:** PRs pile up; engineers context-switch to other tasks while waiting; reviews become rubber-stamps under pressure.

**Mitigation:** PR review SLA of (e.g.) 24 hours for critical-path PRs, 48 hours otherwise. Documented and tracked.

---

## 3. Stage 2 — Testing gaps and failure modes

### 3.1 🔴 Critical: no acceptance test procedure (ATP) document — RFQ Milestone 2 #2 deliverable

The customer requires an ATP per RFQ Annexure A.2. None exists. The integration test gates in [v2-execution-plan.md §6](v2-execution-plan.md#6-integration-testing-checkpoints) describe the developer-facing gate criteria, but the ATP is the customer-facing acceptance criteria — a different artefact.

**Failure mode:** acceptance test conducted ad-hoc at customer site. Pass / fail criteria interpreted on the day. Disputes about what "acceptance" means.

**Mitigation:** draft ATP no later than week 12 of the build, sign-off by the customer programme manager at week 14. Should cover every RFQ §1.A–§G + §2 requirement with a specific test case and expected result. New backlog item: **B1.18 — ATP / STD / STR drafts.**

### 3.2 🟠 High: no SRS document — RFQ Milestone 1 #1 deliverable

The [Operator Playbook §14 traceability matrix](operator-playbook.md#14-rfq--v2-traceability-matrix) is the substrate of the SRS but is not in SRS form. The formal SRS is a separate deliverable.

**Failure mode:** Milestone 1 fails the client review for missing SRS. Or — SRS is rushed at week −1 and is inconsistent with the design docs the build is following.

**Mitigation:** SRS drafted by end of Milestone 1 (before v2 hardening phase begins). Author: project lead with architecture lead review. Reuses the traceability matrix as its spine. Same backlog item as 3.1.

### 3.3 🟠 High: SG-side `Sg.App` automated UI test coverage — absent

MVP4.5 has 77 unit-tier tests and 4 integration tests (via `StkBackendFixture`). All of that exercises domain logic and STK integration; **none of it exercises the WPF UI as an operator would.**

| What's not tested today | Failure mode |
|---|---|
| Click on "+ Aircraft" toolbar button | Button styling regression ships unnoticed |
| Press Enter / Esc to finalize / cancel placement | Keyboard handler regression breaks operator workflow |
| Drag-edit on globe → MouseUp → ApplyObjectEditing | Drag commit regression (the exact failure ADR-015 fixed) recurs |
| Property panel field validation | Operator enters invalid lat/lon; app crashes or silently corrupts scenario |
| File dialog → Save As .vdf with password | Password handling regression silently saves without encryption |

**Failure mode:** UI regressions ship; customer reports issues at acceptance; remediation requires manual QA cycles the team didn't plan for.

**Mitigation:** introduce a lightweight UI automation layer (FlaUI / Appium / White) for the critical Sg.App flows by week 8. Add as backlog: **B1.19 — Sg.App UI automation harness.**

### 3.4 🟠 High: DRS webapp test strategy — undefined

G builds the DRS webapp through weeks 3–15. The execution plan §3.7 lists G's deliverables but doesn't mention webapp testing. Standard webapp testing layers:

- Unit (Jest / Vitest for Angular / React)
- Component (Storybook + visual regression)
- End-to-end (Cypress / Playwright)

None specified. Same problem as 3.3 — UI regressions ship.

**Mitigation:** define G's testing approach as part of the framework decision (B1.2). Add unit + e2e harness gates to the per-phase integration tests. Recommended additions to backlog or **B1.2 extension.**

### 3.5 🟠 High: integration test environment — physical / logical home unclear

The integration test rig spans: SDFC simulator, drs-bridge, drs-server, Kafka, TimescaleDB, Sg.App, STK, DRS webapp, entity-controller simulator. That's 8+ components.

| Question | Current state |
|---|---|
| Where does this run physically? | Unspecified. Inferred: D's workstation or a dedicated test box. |
| Is there a docker-compose / equivalent for spinning up Kafka + TimescaleDB + drs-server + drs-bridge for tests? | **Partial (2026-05-21):** Kafka KRaft single-node lives in [`infrastructure/docker-compose.yml`](../../infrastructure/docker-compose.yml) with idempotent topic-creation in [`infrastructure/kafka/create-topics.py`](../../infrastructure/kafka/create-topics.py); end-to-end real-broker integration test at [`drs-server/tests/test_kafka_broker_integration.py`](../../drs-server/tests/test_kafka_broker_integration.py). TimescaleDB + drs-server/drs-bridge orchestration is data-shape-dependent and deferred to first-IRS work. |
| How does WS1-side STK integrate with WS2-side services on the rig? | Cross-LAN setup; no documented procedure. |
| Can integration tests run without STK (mocked)? | No — per ADR-017 (no runtime mocks). So integration tests need real STK on the rig. |

**Failure mode:** D builds the rig on D's laptop; D goes on holiday; no one else can run integration tests; phase gate slips.

**Mitigation:** dedicated test workstation (the same dual-WS configuration as the customer deployment) reachable by the whole team. Cost: one more set of hardware in the team area, plus an STK seat. Add to Milestone-1 logistics.

### 3.6 🟡 Medium: performance test reproducibility

Phase 7 gate: "2,000 msg/s sustained for ≥ 30 minutes." Many implicit variables:

- Synthetic Kafka producer's CPU profile?
- Message-payload size distribution? (FF small, Burst large?)
- Disk subsystem on the test box (SSD vs NVMe vs spinning)?
- Network MTU on the test LAN?

**Failure mode:** 2,000 msg/s passes on the dev rig; fails at the customer site because the customer's WS2 has a SATA SSD instead of NVMe; or the customer's LAN has 9000-byte jumbo frames vs the dev rig's 1500-byte standard.

**Mitigation:** specify the test rig's hardware profile in the ATP; require the customer to match or exceed it. Or — run the Phase 7 test at the customer site as part of acceptance, not at the dev office.

### 3.7 🟡 Medium: STK COM patch-drift detection — manual, not automated

Risk R3 in the register flags STK COM 12 patch-version drift as Medium/High. Mitigation: pin STK 12 minor version per dev environment. But:

- How is the pin enforced? Manually, per developer?
- What happens when AGI / Ansys auto-pushes a security patch?
- Is there a CI gate that fails on STK version mismatch?

**Failure mode:** dev pulls a Windows update that includes an STK patch; CI breaks; reverts manually; trust in the regression suite erodes.

**Mitigation:** STK version captured in a constant in the integration test setup; on startup, the test asserts the running STK version matches and refuses to run otherwise. Automated rather than honour-system.

### 3.8 🟡 Medium: security testing — absent

The doc set has zero coverage of:

- Penetration testing — who, when, what scope?
- JWT secret rotation procedure
- RBAC permission audit (does every endpoint check the right permission?)
- Input validation fuzzing beyond C++ parsers (e.g. `Sg.App` waypoint coords with NaN, +Inf, very large altitudes)
- SQL injection check on the dynamic queries in drs-server

The RFQ doesn't explicitly mandate this, but defence systems usually require it for acceptance. Customer's own security review at delivery may surface findings the team didn't anticipate.

**Failure mode:** customer security audit blocks acceptance pending pen test results.

**Mitigation:** budget a security pass (internal or external) before customer acceptance. New backlog item: **B1.20 — Security review and penetration testing.** Plan it for weeks 13–15 so any findings have remediation time.

### 3.9 🟡 Medium: recordings replay correctness — no test gate

RFQ §B mandates "Facility to control the playback of mission scenarios." The execution plan §3.3 mentions B will build the recordings replay UI as part of Sg.App polish. But:

- What recording format? Is it a Kafka topic dump + scenario file? A WebSocket message capture?
- How is replay correctness verified? Time-aligned with the original recording?
- What happens if the source scenario changed between recording and replay?

**Failure mode:** recordings save fine but replay corrupts or skips data; operators distrust the feature and stop using it.

**Mitigation:** record-replay-compare automated test that records a synthetic exercise, replays it, and diff'd row-by-row against the original. Add as a Phase 5 or Phase 6 test criterion.

### 3.10 🟡 Medium: time / timezone edge cases

| Scenario | Failure mode |
|---|---|
| Scenario authored at UTC; exercise played at customer's local time | Timestamps in reports show wrong time |
| Exercise crosses a DST transition | 1-hour gap or duplicated hour in telemetry timeline |
| Time-sync skew exceeds tolerance | Logs from WS1 and WS2 drift; correlation queries return wrong results |
| STK and PostgreSQL use different time zones internally | `computed_links` timestamps look wrong in TimescaleDB |

**Mitigation:** UTC everywhere; explicit timezone metadata on every report; Time-Sync design (B1.3) specifies a 10 ms tolerance with a three-tier `HEALTHY → DRIFT_WARN → DRIFT_ALERT → SYNC_LOST` state machine and a `sync_lost`-triggered exercise auto-pause. See [B1.3 design spec](specs/time-sync-design.md) and [Design Backlog B1.3](design-backlog.md#-b13--time-synchronization-service-design-sg-as-time-server).

---

## 4. Stage 3 — Production gaps and failure modes

### 4.1 🔴 Critical: customer-site STK 12 install state — no diagnostic tool

We just hit this in MVP4.5 development: STK 12.9.1 install state on a dev machine was incomplete (missing env vars, missing COM registrations, missing per-user setup). Several hours of triage. The customer site will install STK from scratch from the Milestone 1 #4 (lease license) and Milestone 1 #5 (Runtime Engine Qty 2) DVDs. **Nothing in the v2 design helps the customer's installer / sysadmin diagnose a partial install.**

**Failure mode:** classroom training delayed by hours or days while the vendor team troubleshoots STK install remotely.

**Mitigation:** ship a small `stk-doctor.exe` (or equivalent) with the v2 installer that verifies: env vars set, HKLM + HKCU COM registrations, `AgSTKEngineHost.exe` present, license server reachable, `AgStkObjectRoot` instantiable, `Documents\STK 12\` populated. Output an actionable diagnostic message for each failure mode. Add as backlog: **B1.21 — STK install diagnostic / repair tool.**

### 4.2 🔴 Critical: DVD install procedure — explicitly says "does not yet exist"

[Deployment Guide §5](deployment-guide.md#5-installation-procedure-ws2--drs-workstation-telemetry-phase) reads: *"This section describes a deployment that does not yet exist; procedure will be finalised as part of the v2 hardening phase."*

Production install procedure is the final-mile deliverable. Today it's a placeholder.

**Failure mode:** install scripts go untested until acceptance week; install fails at the customer site; remote troubleshooting consumes days of acceptance window.

**Mitigation:** install procedure must be drafted and rehearsed by week 9 (air-gapped install rehearsal per execution plan §4.4); refined through weeks 14–15; signed off at Phase 6 gate.

### 4.3 🔴 Critical: user manuals — RFQ Milestone 2 #5 deliverable, no draft

The operator playbook is internal architecture documentation. The user manual is a customer deliverable — what the operator opens at the customer site, in trainee-friendly language.

**Failure mode:** Milestone 2 fails customer review for missing or insufficient user manuals.

**Mitigation:** draft user manuals from the operator playbook starting week 12; review with customer programme manager at week 14; finalise by week 17. Should cover both SG Operator and DRS Engineer personas. **B1.22 — User manuals.**

### 4.4 🟠 High: maintenance / hotfix procedure for air-gapped sites

1-year warranty per RFQ. Air-gapped customer site means hotfixes can't be pushed automatically. Procedure for hotfix delivery:

| Question | Current state |
|---|---|
| Does a hotfix come as a DVD reissue or a patch DVD? | Undefined |
| How is the hotfix verified at the customer site (without internet for checksums)? | Undefined |
| Who has signing authority for hotfixes? | Undefined |
| Does the customer's IT security team need to scan / approve every patch? | Likely yes, but not in the procedure |
| Rollback procedure if the hotfix breaks something? | Undefined |

**Failure mode:** critical bug discovered week 3 of warranty; team produces a fix in a day; takes 6 weeks to get it onto the customer's air-gapped network. Operator workaround consumes ops time.

**Mitigation:** maintenance handbook drafted at week 16. Should cover: patch packaging, customer IT-security approval workflow, rollback, change-log per patch, version tracking. **B1.23 — Maintenance / hotfix procedure for air-gapped sites.**

### 4.5 🟠 High: long-running exercise telemetry retention — disk fills

Targeted load: 100 instances × 20 Hz = 2,000 msg/s sustained. At average 200 bytes per row (TimescaleDB internal storage, after compression) × 2,000 msg/s × 86,400 s/day = ~34 GB/day. For a 2-week exercise: ~480 GB. Customer WS2 sized at (e.g.) 1 TB → fills in ~1 month at sustained peak.

| Concern | Current state |
|---|---|
| Retention policy on hypertables | Mentioned in deployment-guide §5.4 but not specified |
| Disk-full alerts to operator | Not specified |
| Behaviour when disk fills (Kafka backpressure, drs-server crash, DB errors) | Not analysed |
| Pre-emptive archive procedure | Not specified |

**Failure mode:** disk fills mid-exercise. Kafka producers block; drs-server logs disk-full errors; operator session locks up; data loss between disk-full and admin intervention.

**Mitigation:** retention policy specified before deployment (per-class: telemetry 7 days, audit logs 90 days, scenario metadata indefinite). Disk-usage alert surfaced in DRS webapp + Sg.App admin view. Auto-purge enforced. **B1.24 — Telemetry retention policy + disk-full handling.**

### 4.6 🟠 High: STK lease licence expiry — day 366 failure

RFQ Milestone 1 #4: 1-year lease licence. Customer needs to renew or transition to perpetual before day 366.

| Concern | Current state |
|---|---|
| Operator warning when licence approaches expiry | Risk R6 mentions but no specific UX |
| Procedure for licence renewal / replacement | Not in deployment guide |
| Behaviour on day 366 if licence expires | STK Engine refuses to start; Sg.App fails with COM exception |

**Failure mode:** day 366, classroom training scheduled; STK fails to start; customer ops scrambles for an updated lease file.

**Mitigation:** Sg.App startup checks licence days-to-expiry; warns operator at 30 / 7 / 1 day(s) remaining. Document renewal procedure. **B1.25 — Licence expiry handling and renewal procedure.**

### 4.7 🟠 High: ICD revision mid-warranty

Risk R9 in the register covers "C++ parser variant count exceeds 12+ planned" — i.e. new variants. But: **existing variants where the customer revises the IRS mid-warranty** is not covered.

**Failure mode:** customer's RDFS hardware spec gets revised at month 6 of warranty; existing parser silently misparses some message fields; operator sees garbage data; root cause takes days to find.

**Mitigation:** parser library declares its IRS-version at build time and at runtime; drs-bridge logs the (variant, IRS-version) tuple on every restart; client-supplied IRS revisions trigger a parser rebuild + golden-frame validation. Procedure documented in maintenance handbook. New backlog item: **B1.26 — IRS revision change-management procedure.**

### 4.8 🟠 High: network partition between WS1 and WS2

Cross-LAN data flows:
- WS1 → WS2: PostgreSQL writes (computed_links)
- WS2 → WS1: drs-server WebSocket (telemetry) + REST (queries)

Behaviour on a brief LAN cut is not specified.

| Scenario | Likely current behaviour | Should be |
|---|---|---|
| LAN cut for 5 s | Sg.App's WebSocket subscription drops; reconnect logic? unclear | Auto-reconnect with exponential backoff; cached recent state shown until reconnect |
| LAN cut for 5 min | PostgreSQL writes from WS1 fail; compute pipeline errors out; partial computed_links written | Compute pauses with retry; operator sees status; resume on reconnect |
| WS2 reboot during exercise | All in-flight telemetry lost; Kafka offsets are durable but operator sees a gap | Operator sees a clear "WS2 connection lost" indicator; reconnection re-subscribes; clear gap in the report |

**Failure mode:** brief network blip kills the operator's session; classroom training restarts from the beginning.

**Mitigation:** specify reconnection behaviour in each cross-LAN client (Sg.App WebSocket consumer; Sg.App PostgreSQL client; drs-bridge ScenarioPublisherClient). Add resilience tests at Phase 5 gate.

### 4.9 🟡 Medium: power-failure / unclean-shutdown recovery

| Component | Recovery on dirty shutdown |
|---|---|
| STK Engine | Process recovery via STK's own; scenario state is in `.sc` file (saved at intervals?) |
| PostgreSQL | WAL recovery automatic; durable |
| Kafka KRaft | Log recovery automatic; durable |
| drs-bridge | Per-connection state in coroutines = lost; needs reconnection from upstream |
| Sg.App | In-memory scenario state lost if not saved |

The combined recovery procedure ("operator power-cycles the WS1 box during a compute") is not documented. Worst case: scenario in mid-compute when power drops; `computed_links` table has partial data for the in-flight exercise; operator can't tell whether the data is trustworthy.

**Mitigation:** atomic compute-batch markers in TimescaleDB (insert all-or-nothing per scenario); on Sg.App startup detect partial compute and offer to resume / discard. Documented recovery procedure for operators. **B1.27 — Power-failure recovery procedure.**

### 4.10 🟡 Medium: operator-UI fatigue / discoverability

Sg.App combines: scenario authoring, GIS toolset (11+ tools), entity-property panels (9 entity types), exercise control, live telemetry, reports, admin. The DRS webapp combines: 12+ variant tabs, health, IP config, logs, control. Both are dense.

**Failure mode:** trainee operators in classroom training cannot find a feature; instructors fall back on demonstrating instead of training; operator never internalises the workflow.

**Mitigation:** information-architecture review during UX wireframes (B1.1). Specifically: progressive disclosure for advanced features (collapse expert-mode panels by default); searchable command palette (Ctrl+P style); on-hover hints; consistent navigation chrome.

### 4.11 🟡 Medium: multi-user concurrency in SG-side

RFQ doesn't mandate multi-user authoring, and v2 design explicitly defers it. But classroom training might involve:

- Instructor logged into one Sg.App instance for setup
- Trainees logged into the same WS1 (or different WS1s in a multi-station classroom?)

The RFQ is ambiguous on whether classroom training requires multiple SG workstations.

**Failure mode:** classroom uses 5 trainee stations + 1 instructor station; v2 supports only 1 SG workstation per deployment; classroom procurement assumed otherwise.

**Mitigation:** clarify with the customer during the design review whether multi-SG-workstation deployments are needed. If yes, scope grows; if no, document the classroom topology explicitly in the deployment guide.

### 4.12 🟡 Medium: audit log integrity

Logs are in TimescaleDB hypertables. Defence systems typically require append-only audit logs (tamper-evident).

**Failure mode:** customer's security audit flags log integrity gap. Acceptance contingent on hash-chained logs or external sink.

**Mitigation:** clarify customer requirements during design review. If mandated: append-only PostgreSQL role for the audit-log table; per-row hash chain; or off-WS log forwarding to a separate appliance.

### 4.13 🟢 Low: disaster-recovery for catastrophic hardware failure

Two-workstation deployment; if WS1 or WS2 die catastrophically (motherboard / RAID failure):

- Backup workstation procedure?
- Image-based recovery?
- Customer cold-spare commitment?

Currently undocumented.

**Mitigation:** disaster-recovery section in deployment guide; specify the customer's backup workstation expectation; rehearse swap-in procedure once.

---

## 5. Cross-cutting gaps

### 5.1 🔴 Critical: SRS / ATP / STD / STR — RFQ Milestone deliverables, all undrafted

Summary of items already in §3.1 and §3.2 — but worth surfacing as a unified gap. These four documents are explicit RFQ deliverables across both milestones. Today the doc set has none of them.

**Mitigation:** the [Operator Playbook §14 traceability matrix](operator-playbook.md#14-rfq--v2-traceability-matrix) is the spine the SRS is drawn from. Plan the four documents into the Milestone-1 (SRS) and Milestone-2 (ATP / STD / STR) timeline. Each has a customer-facing review gate.

### 5.2 🟠 High: third-party-library licence catalogue

Every Python wheel, every C++ dependency, every npm package (G's webapp) carries a licence (MIT / Apache 2.0 / GPL / etc.). RFQ §note: "IP rights of Scenario generator with DRS belongs to the customer."

**Failure mode:** customer's legal audit at delivery finds GPL-licensed code that contaminates the IP transfer.

**Mitigation:** licence catalogue auto-generated from the dep manifest (`pip-licenses`, npm `license-checker`, etc.) and reviewed by D before each milestone delivery. Allow-list: MIT, Apache 2.0, BSD-3. Block: GPL, AGPL, LGPL (linkage problem for the C++ parsers). **B1.28 — Third-party licence audit + allow-list.**

### 5.3 🟠 High: hand-off / knowledge-transfer plan post-acceptance

1-year warranty + customer takes IP. After warranty, the customer maintains. Knowledge transfer plan undefined:

- Does the vendor train a customer team during the build or after?
- Source-code walkthrough? Sit-with sessions?
- Architecture-document hand-off ceremony?
- Customer's first 6 months of warranty: who answers their questions?

**Mitigation:** training plan drafted at week 14; customer team identified by the programme manager; sit-with weeks at the vendor office or at the customer site during week 15–17.

### 5.4 🟡 Medium: language / internationalisation

RFQ doesn't specify language. Probably English. But classroom training in some defence environments uses local language for trainee-facing UI.

**Failure mode:** customer review surfaces a requirement for localised UI at acceptance.

**Mitigation:** clarify during design review. If localisation needed, scope grows materially; if not, document English-only.

### 5.5 🟡 Medium: accessibility

WCAG / Section 508 / equivalent. Not mentioned. Classroom training might have trainees with accessibility needs (visual / motor).

**Failure mode:** instructor / trainee cannot use the system effectively; customer surfaces this late.

**Mitigation:** baseline accessibility heuristics in UX wireframes (B1.1); minimum: keyboard-only navigation for both Sg.App and DRS webapp; screen-reader compatibility for DRS webapp.

### 5.6 🟡 Medium: error reporting / telemetry from the customer site

How does the vendor learn about issues at the customer site during warranty? The site is air-gapped — no auto-telemetry.

**Failure mode:** customer encounters an issue, doesn't report it (it's tolerable); team unaware until acceptance review for the next phase; pattern of similar issues across multiple customers undetectable.

**Mitigation:** structured incident-reporting form for the customer; logs export procedure with redaction guidance; vendor-side issue tracker that aggregates reports. **B1.29 — Customer issue-reporting workflow.**

### 5.7 🟢 Low: documentation localisation + accessibility

The operator playbook + user manuals are English-only Markdown. PDF / Word for customer hand-off. Accessibility (alt text on diagrams, structured headings) — unclear.

**Mitigation:** PDF / Word user-manual versions generated from Markdown; alt-text reviewed during user-manual finalisation.

---

## 6. Summary — top gaps ranked by severity × likelihood

### 6.1 Critical (will block acceptance if unmitigated)

| # | Gap | Section |
|---|---|---|
| 1 | ~~Four contracts~~ Three contracts (~~parser ABI~~ closed 2026-05-21, YAML schema, IScenarioBackend extensions, OpenAPI) claimed "frozen at week 2" — three artefacts still without concrete form today | §2.1 |
| 2 | SRS / ATP / STD / STR — RFQ deliverables, none drafted | §3.1, §3.2, §5.1 |
| 3 | Customer-site STK 12 install state — no diagnostic tool, after the MVP4.5 experience showing how easily this can be broken | §4.1 |
| 4 | DVD install procedure — explicitly placeholder | §4.2 |
| 5 | User manuals — RFQ Milestone 2 #5 deliverable, no draft | §4.3 |

### 6.2 High (will materially slow or degrade delivery)

| # | Gap | Section |
|---|---|---|
| 6 | IRS document provenance + versioning | §2.2 |
| ~~7~~ | ~~Cross-platform build pipeline (Linux + Windows)~~ — **RESOLVED 2026-05-14** by switching WS2 to Windows. | §2.3 |
| 8 | STK 12 dev licence allocation across 7 engineers | §2.4 |
| 9 | SG-side Sg.App automated UI test coverage absent | §3.3 |
| 10 | DRS webapp test strategy undefined | §3.4 |
| 11 | Integration test environment physical home unclear | §3.5 |
| 12 | Maintenance / hotfix procedure for air-gapped sites | §4.4 |
| 13 | Telemetry retention / disk-full handling | §4.5 |
| 14 | STK lease licence day-366 expiry handling | §4.6 |
| 15 | IRS revision mid-warranty change-management | §4.7 |
| 16 | Network-partition resilience between WS1 ↔ WS2 | §4.8 |
| 17 | Third-party licence catalogue + allow-list (IP transfer) | §5.2 |
| 18 | Hand-off / knowledge-transfer plan post-acceptance | §5.3 |

### 6.3 Medium and lower

All §2.5–§5.7 items not listed above — manageable but worth surfacing.

---

## 7. Recommended next steps

1. **Land the remaining three-contract artefacts before week 0** — ~~parser ABI~~ (closed 2026-05-21, see [`drs-bridge/parsers/reference/`](../../drs-bridge/parsers/reference/)), YAML schema, IScenarioBackend extensions, OpenAPI spec, each owned and signed off. Without these, the "Phase 1 contracts frozen at week 2" gate is hand-wavy. This is the single highest-leverage pre-build action.
2. **Draft the SRS by end of Milestone 1** — using the operator playbook traceability matrix as spine.
3. **Plan the ATP / STD / STR drafts into weeks 12–14 of the build** — not weeks 16–17 — so the customer programme manager has time to review.
4. **Add the items called out in §6.1 + §6.2 as new backlog rows** — specifically B1.17 (IRS), B1.18 (SRS+ATP+STD+STR), B1.19 (Sg.App UI), B1.20 (security), B1.21 (STK install diagnostic), B1.22 (user manuals), B1.23 (maintenance), B1.24 (retention), B1.25 (licence expiry), B1.26 (IRS revision), B1.27 (power recovery), B1.28 (licence catalogue), B1.29 (customer issue reporting).
5. **Get CI running before week 0** — workflow YAML is authored and parked at [`.github/disabled/ci.yml`](../../.github/disabled/ci.yml); blocked on GitHub Enterprise admin enabling hosted runners (or standing up a self-hosted runner). No design fixes the cumulative test rig if CI doesn't run on every PR. See §2.8.
6. **Decide git branching strategy + code-review SLA before week 0** — both are low cost and prevent operational thrash later.
7. **Schedule a security review pass for weeks 13–15** — before customer acceptance, so any findings have time for remediation.
8. **Clarify with the client during the design review:** classroom-training topology (single SG vs multi-station SG), audit-log integrity requirements, localisation, accessibility, CC integration API surface (B1.4 already blocked on this).

---

## 8. What this analysis does *not* claim

- That the architectural decisions in [decision-record.md](decision-record.md) are wrong. They are not; the architecture is sound. The gaps surfaced here are at the contract / process / artefact / operational layer, not the architecture layer.
- That the 17-week build is unachievable. It is achievable; the gaps in §2 are pre-build items that can be addressed in Milestone 1 (weeks −4 to 0) without slipping the 17-week schedule.
- That the team is under-resourced. The 7-person team is correctly scoped; the gaps in §3 / §4 are about process and deliverables, not headcount.
- That every gap is critical. Most are medium; only the items in §6.1 are critical-path for acceptance.

This is a structural review of *what's not documented* — not a vote of no confidence in the design.

---

## 9. References

- [Operator Playbook](operator-playbook.md) — RFQ → v2 traceability matrix is the substrate of the SRS.
- [Design Backlog](design-backlog.md) — Milestone-1 design items; this analysis surfaces additional items (B1.17–B1.29) for the backlog.
- [Architecture Overview](architecture-overview.md) — the architecture this analysis reviews against the RFQ.
- [Decision Record](decision-record.md) — 19 ADRs; architecturally sound, not the gap source.
- [Risk Register](risk-register.md) — overlapping items (R1, R3, R5, R6, R7, R9, P1, P3); this analysis surfaces additional risks the register can absorb.
- [v2 Execution Plan](v2-execution-plan.md) — staffing and timeline; this analysis identifies pre-build prerequisites (§7 items 1, 5, 6).
- RFQ Annexure A.1 (Scope of Development) + A.2 (List of Deliverables) — the requirement bar this analysis measures against.
- v1 legacy code at `E:\Sandbox\17-05-2025\` — consulted for established operator behaviour (e.g. the `/drs/*` Angular routes confirming the DRS Engineer persona).
