# EWTSS v2 — Risk Register

**Audience:** project sponsor (CEO/CTO), customer programme manager, project lead, technical leads.
**Purpose:** consolidated view of known risks for the v2 programme, with likelihood, impact, owner, and mitigation. Updated whenever a risk materialises, is retired, or a new one is identified during build.
**Cadence:** review at each phase gate (kick-off, mid-build, integration test, customer acceptance). Risks marked *retired* below have already been resolved by the work shipped in MVP4.5 and the Hybrid architecture decision.

---

## 1. Active risks (engineering)

| # | Risk | Likelihood | Impact | Owner | Mitigation |
|---|---|---|---|---|---|
| R1 | **C++ parser build complexity on Windows** — CMake + MSVC builds for 12+ shared libraries; edge cases on different MSVC redistributables | Medium | Medium | C++ developer | `build_parsers.bat` wraps the toolchain; CI builds on the matrix of supported MSVC versions; pre-compiled `.dll`s shipped on the deployment DVD as a fallback |
| R2 | **TimescaleDB schema migration for existing customer data** | Low | Low | Python developer (drs-server) | Greenfield build — v2 deploys to a fresh PostgreSQL instance. Legacy data export is out of scope. |
| R3 | **STK COM automation instability across STK 12 minor versions** | Medium | High | Senior C# developer | Pin to a known-good STK 12 minor version per deployment; integration test tier as regression net (the ~14 documented gotchas in [v2 archive §25.3.3 + §25.3.5](specs/v2-tech-stack-archive.md) are version-sensitive); fail-fast on STK availability per [ADR-017](decision-record.md) |
| R5 | **AIOKafka / aiokafka / asyncpg air-gap vendoring failure** — pure-Python wheels need to install offline | Low | Medium | Python developer (drs-bridge infra) | All dependencies are pure Python or have wheels for Windows x64; vendor under `packages/` and use `pip install --no-index --find-links=packages/`; verified for each dependency before commit |
| R6 | **STK Engine licence file expiry / licence-server unreachable on air-gapped LAN** | Low | High | Customer integration + ops | Use perpetual local `.lic` file rather than network licence; document renewal procedure in deployment guide; alarm in `Sg.App` startup if licence days-to-expiry < 30 |
| R7 | **Permanent COM event subscription regression on `StkDisplayHost`** — silent re-introduction of a `+= ...` pattern that breaks pan smoothness; this is structurally similar to the v1 anti-patterns catalogued in the [Legacy System Audit](legacy-system-audit.md), specifically the "subscribe globally then forget" pattern v1 used for thread lifecycle | Medium | Medium | Senior C# developer | PR checklist item ([Developer Handbook §16](developer-handbook.md#16-pull-request-checklist)) — any `+= ...` outside `_ensure*Subscribed` is a review block; diagnostic logging gated by `MVP4_DIAG=1` makes regressions visible in 30 s |
| R8 | **Mode B never gets built — Hybrid optionality unused** | Medium | Low (financial) | Project lead | The contract-boundary work is dual-purpose (testability + browser-future), so its value isn't binary on Mode B activation. If Mode B is never funded, ~10% of MVP4.5's effort was spent on insurance that paid off in test isolation alone — acceptable |
| R9 | **C++ parser variant count exceeds the 12+ planned** — customer requests integration of variants not in current scope | Low | Medium | Project lead + C++ developer | ICD codegen tool ([spec](specs/icd-codegen-tool-design.md)) produces skeletons in hours, not days; new variants are scoped per-ICD and chargeable; first variant takes longest, marginal cost decreases |
| R10 | **CesiumJS terrain rendering artefacts at AoI boundaries** *(activates only when Mode B Phase 1 ships)* | Medium | Low | GIS specialist + frontend developer (when Mode B activates) | Tile overlap margins in `ctb-tile`; CesiumJS `skirtHeight` parameter; tested against representative AoI before delivery |

---

## 2. Active risks (programme)

| # | Risk | Likelihood | Impact | Owner | Mitigation |
|---|---|---|---|---|---|
| P1 | **Telemetry pipeline build slips beyond 4-month / 17-week estimate** | Medium | High | Project lead | Critical-path tracking ([v2 Execution Plan §5](v2-execution-plan.md#5-critical-path)); phased build with integration-test checkpoints ([§6](v2-execution-plan.md#6-integration-testing-checkpoints)) — phase gates surface schedule slip early; the C++ parser API contract is the earliest critical-path item — frozen in week 2 to unblock all other work |
| P2 | **Specialist hiring delay for GIS work** *(activates only when Mode B Phase 1 ships)* | Medium | Low | Project lead (when Mode B activates) | Part-time engagement (~6 weeks); start procurement Week 1; Martin tile server can serve a synthetic DEM for development if real DTED is delayed |
| P3 | **Customer scope expansion mid-build** — new requirements introduced during the v2 hardening phase | Medium | Medium | Project lead | Scope-change protocol with the customer programme manager; any new variant or feature is logged, estimated, and chargeable; daily-build branches preserve the mid-stream state if a redirect is requested |
| P4 | **Installer DVD size exceeds capacity** — once Mode B's Cesium tiles + offline imagery are bundled | Low | Medium | Python developer (drs-bridge infra) + GIS specialist | Bound MBTiles to confirmed AoI before final packaging; measure the bundle size at week-4 checkpoint; multi-DVD or USB delivery is a fallback |
| P5 | **Documentation drift between archive and audience-targeted docs** | Medium | Low | Architecture lead | The audience-targeted set in `docs/ewtss/` is the canonical surface (active subsystem specs + plans live under `docs/ewtss/specs/` and `docs/ewtss/plans/`); the pre-v2 MVP design archive (`docs/superpowers/`) is annotated read-only. ADR additions go into [decision-record.md](decision-record.md), not the archive |

---

## 3. Retired risks (resolved by MVP4.5 / Hybrid decision)

The following risks were live during the design / brainstorming phase and have been resolved by work that has already shipped. Kept here so the audit trail is complete.

| # | Risk | How it was retired |
|---|---|---|
| X1 | WebView2 absence on older Windows 10 | Hybrid uses native WPF (Mode A) — no WebView2 dependency. Mode B (when activated) bundles its own Chromium via Electron, also no WebView2. |
| X2 | Electron memory footprint (~200 MB) on customer workstations | Customer workstations have ≥ 16 GB RAM; footprint is non-issue per the [Deployment Guide §2.1](deployment-guide.md#21-ws1--sg-scenario-generator-mode-a-configuration). Mode B is opt-in only. |
| X3 | Team Rust skill gap (Tauri) | Tauri not selected. Hybrid uses .NET / WPF (existing C# expertise) for Mode A and Angular + CesiumJS (existing skill set) for Mode B. |
| X4 | Pan smoothness on STK ActiveX globe under WPF + WindowsFormsHost | Resolved in MVP4.5 via on-demand event subscription discipline ([ADR-013](decision-record.md)). Validated. |
| X5 | Drag-edit not committing to STK COM state | Resolved in MVP4.5 via `ApplyObjectEditing()` on user MouseUp ([ADR-015](decision-record.md)). Validated. |
| X6 | Facility position silently no-oping at STK's HQ default | Resolved in MVP4.5 via `Position.AssignGeodetic` directly ([ADR-014](decision-record.md)). Validated. |
| X7 | Property panel blanking during edit (tree rebuild on `ScenarioChanged`) | Resolved in MVP4.5 via path-set short-circuit on `RebuildFromService`. Validated. |
| X8 | Right-click consumed by STK's camera ops — finalize gesture unreachable | Resolved in MVP4.5 via window-level `OnPreviewKeyDown` (Enter / Esc) ([ADR-016](decision-record.md)). Validated. |
| X9 | Architecture lock-in to either browser or desktop | Resolved by Hybrid architecture decision ([ADR-001](decision-record.md)) — desktop today, browser opt-in future, shared `Sg.Domain` core. |
| X10 | STK Components not bundled in deployment Runtime Engine licences (was R4) | Verified with Ansys/AGI pre-Milestone-2 — STK Components confirmed bundled in the customer's perpetual Runtime Engine licence. Procurement fallback ("compute on dev, deliver `.sc`") not needed. |

---

## 4. Risks deferred — activate when condition is met

These risks are dormant today because they apply only to phases not yet started. They become active when their gating condition is met. Listed for completeness and to avoid surprise when a phase opens.

| # | Risk | Activates when | Owner-to-be |
|---|---|---|---|
| D1 | DTED Level 1/2 source data acquisition delay | Mode B Phase 1 kicks off | GIS specialist |
| D2 | Drift between C# `Sg.Domain` DTOs and TypeScript mirror types in `Sg.Web` | Mode B Phase 2 kicks off | Frontend developer |
| D3 | Multi-user concurrency on Mode B's authoring SPA | Customer requests multi-user authoring | Backend developer (Mode B) |
| D4 | OpenAPI contract testing across the C# server / TS client boundary | Mode B Phase 1 kicks off | Backend + frontend developers |
| D5 | Mode B authentication scheme — currently undecided ([Decision Record open decisions](decision-record.md#open-decisions-not-yet-recorded)) | Mode B Phase 1 kicks off | Architecture lead |

---

## 5. Risk-register usage

- **At kick-off:** review every active row, confirm owner, confirm mitigation is funded.
- **Weekly during build:** triage any risk whose likelihood has changed; promote to mitigation execution.
- **At phase gates:** retire mitigated risks (move to §3); identify new risks from the just-completed phase.
- **At customer acceptance:** the active-risk set must be either retired or accepted in writing by the customer.

The register lives in this file; project status reports cite row numbers (`R3`, `P1`, `D2`) rather than restating risks. New risks get the next available number in their section and never get renumbered — historical references stay valid forever.

---

## 6. References

- [Executive Brief](executive-brief.md) — top 3 risks called out for executive sponsors (subset of the full register here).
- [Decision Record](decision-record.md) — per-ADR risk discussion alongside the architectural commitments. ADR rationale often references the same risks documented here.
- [Deployment Guide §3](deployment-guide.md#3-licensing) — full STK licensing detail (verification evidence behind the X10 / former R4 retirement).
- [Architecture Overview §5](architecture-overview.md#5-key-trade-offs) — architectural trade-offs that shape the residual risk profile.
- [v2 Execution Plan](v2-execution-plan.md) — staffing and critical-path detail used by P1 / P3 mitigations.
- [v2 tech-stack archive §12 + §23.5](specs/v2-tech-stack-archive.md) — original risk discussions, archived.
