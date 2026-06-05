# EWTSS v2 — Executive Brief

**Audience:** CEO, CTO, customer executives.
**Read time:** 5 minutes.
**Decision asked:** approval to proceed with the Hybrid architecture as scoped below.

---

## What we are building

A next-generation EWTSS (Electronic Warfare Test & Support System) that:

- Authors and computes scenarios for GNSS / DRS hardware testing using STK as the simulation engine.
- Ingests live telemetry from up to **100 DRS instances at 10–20 Hz each** without the perf degradation observed in the current production system after ~10 minutes of sustained load.
- Supports **12+ hardware variants** (RDFS, JV/UHF, JHF, SJRR, JLB, JMB, JHB×4, AUS, PADS) through one configuration system, so adding a new variant takes one config file and one C++ parser, not a refactor.
- Delivers an STK-Insight-grade scenario authoring experience to SG operators on Windows workstations (SG-side); a dedicated DRS Engineer webapp on the DRS workstation (WS2) for hardware diagnostics, health, and per-variant monitoring; an opt-in browser SG-side frontend (Mode B) deferred to a future phase pending customer signal.

## The architectural decision

| Layer | Choice |
|---|---|
| SG-side authoring frontend (today) | **C# WPF** desktop app — `Sg.App.exe` — STK ActiveX in-process for sample-fidelity rendering. Used by the Scenario Operator on WS1. |
| SG-side authoring frontend (future, opt-in) | **Angular + CesiumJS** browser SPA — `Sg.Web` — served by an ASP.NET Core server (`Sg.Server.exe`) that reuses the same `Sg.Domain` core. |
| DRS-side engineering frontend | **Browser webapp on WS2** served by `drs-server` — hardware health, per-variant monitor-scan, IP / network configuration, message logs. Used by the DRS Engineer. Framework choice deferred to Milestone-1 design. |
| Telemetry consumer | **Python FastAPI** — `drs-server` — Kafka consumers, TimescaleDB writes, WebSocket push to SG-side frontend, plus serves the DRS webapp. |
| Hardware bridge | **Python config + C++ shared libs** — `drs-bridge` — asyncio TCP servers, one YAML profile per hardware variant. |
| Database | **TimescaleDB** (PostgreSQL 16 + Timescale 2.x) replacing MySQL. |
| Message bus | **Kafka 3.x in KRaft mode** (no ZooKeeper). |

The single most important choice is the **Hybrid frontend**: ship the desktop deliverable today, keep the door open for browser delivery, pay the browser cost only if a customer asks for it.

## Why this beats the alternatives

- **MVP3 validated** that an Electron + Angular + CesiumJS browser stack can render EWTSS scenarios. We can ship a browser frontend if we choose to.
- **MVP4 + MVP4.5 validated** that a C# WPF + STK ActiveX desktop stack can deliver STK-Insight-grade fidelity (smooth pan, drag-handle waypoint editing, native FOM grid colouring). We can ship a desktop frontend that power users will recognise.
- (The MVP roadmap — what each of MVP1–MVP4.5 built, in what order, on which stack — is in §10 of the [Architecture Overview](architecture-overview.md#10-mvp-roadmap--how-the-architecture-was-validated).)
- The **Hybrid** lets us ship the validated desktop today without foreclosing the browser future. **Roughly 90% of the work needed to enable the browser path has already shipped in MVP4.5** (the JSON-shaped contract, the in-process backend abstraction, the round-trip-tested DTOs). The remaining 10% is the actual server and SPA, paid only if a customer funds it.
- Picking only one frontend would either give up STK-grade fidelity (browser only) or close off browser delivery permanently (desktop only). The Hybrid avoids both.

A full options analysis sits in the [Decision Record](decision-record.md); rejected alternatives include pure-browser, pure-desktop, embedded HTTP listener inside WPF, and symmetrical HTTP-from-day-one. The first two are foreclosing; the last two break performance.

## Cost and timeline

| Phase | Scope | Effort | Status |
|---|---|---|---|
| MVP1–MVP4 | Validation cycles, end-to-end POCs of both frontend candidates | ~6 months elapsed | **Done.** Branches preserved on git. |
| MVP4.5 | Production-quality C# WPF authoring app — DTO contract boundary, perf polish, drag-edit, scenario save/load | ~6 weeks | **Done.** Working on `feat/mvp4.5-dto-boundary`. Smooth pan, full authoring, all unit + integration tests passing. |
| **v2 hardening + telemetry pipeline** | Build `drs-server`, `drs-bridge`, TimescaleDB integration, Kafka, C++ parsers per variant, full RBAC and PDF reports; phased build with integration-test checkpoints at each phase | **~4 months (17 weeks), 7 engineers** | **Pending — needs approval.** |
| Mode B Phase 1 (read-only browser viewer) | ASP.NET Core server + Angular SPA showing scenarios authored in WPF | ~1.5 months | **Deferred.** Activate when a customer requests browser delivery. |
| Mode B Phase 2 (full browser authoring) | Angular SPA implementing the full scenario authoring flow | ~3 months | **Deferred.** Customer-funded. |

Critical path is the telemetry pipeline (`drs-server` + `drs-bridge` + database migration), not the frontend.

**Team composition** for the v2 hardening phase (7 engineers; full per-person ownership in the [v2 Execution Plan §2](v2-execution-plan.md#2-team-composition)):
- Senior Python developer — `drs-server` lead
- Python + C# polyglot — `drs-bridge` Python primary, scenario publisher endpoint integration
- C# specialist — `Sg.App` extension (Transmitter / Receiver / Antenna typed entities) + Mode A telemetry display panels
- C++ specialist — `drs-bridge` C++ parser libraries (one per priority hardware variant)
- Cross-stack lead — architect, PR review across all streams, integration test rig owner, infrastructure
- SG-side Angular developer (cross-trained to Python during ramp) — `drs-server` REST + RBAC + reports for v2 hardening; returns to Angular when Mode B activates
- DRS-side Angular-preferred (or React fallback) developer — DRS webapp on WS2: health dashboard, per-variant monitor-scan, IP / network configuration, message logs, per-variant control

When Mode B activates (post-acceptance, customer-funded), the Angular developer's primary work returns to the SPA + a part-time GIS specialist (~6 weeks) is added for offline tile data preparation.

## Top 3 risks and mitigations

1. **STK Engine licensing.** Per-process, one Engine seat per deployment. *Mitigation:* per-deployment install-time commitment to either Mode A or Mode B (the modes do not coexist); only WS1 hosts an STK process; the second-process problem is unreachable by construction.
2. **Mode B never gets built.** The Hybrid's optionality only pays off if the option is exercised. If the customer never asks for browser delivery, ~10% of MVP4.5's effort (the contract boundary plumbing) was insurance that didn't get used. *Mitigation:* low-cost insurance — that 10% is also load-bearing for testability and audit clarity, so the value isn't binary on Mode B activation.
3. **STK COM API surface drift.** AGI ships new STK versions periodically; the ~17 documented gotchas in the v2 design's §25.3.3 / §25.3.5 are version-specific and may shift. *Mitigation:* fail-fast on STK availability (no runtime mocks), keep the integration test tier as a regression net, pin to a known-good STK 12 release for the production deployment.

## Current status

- **MVP4.5 is feature-complete and stable** on branch `feat/mvp4.5-dto-boundary`. 77 unit tests + 4 STK-required integration tests passing. Smooth pan, full placement (Aircraft / Facility / AreaTarget), drag-edit on globe with Apply commit. Diagnostic logging gated behind an env flag. (Specific UX gestures are MVP4.5's pragmatic choices given STK ActiveX constraints; v2 hardening will align with STK Desktop UX where possible — see [ADR-016](decision-record.md).)
- **Telemetry pipeline production-build is unstarted.** Spec-validation scaffolds for `drs-server`, `drs-bridge`, and the v2 production `sg-app/` landed in May 2026 alongside the B1.3 Time Sync subsystem implementation (Phases 1–5 of B1.3); these surface real prerequisites and design refinements (19 corrigenda findings) ahead of the official build phase. The bulk of v2 build effort still lies ahead.
- **Architecture decisions are complete.** [Decision Record](decision-record.md) lists 19 ADRs covering every load-bearing choice. Open decisions (Mode B auth, real-time push protocol, scenario sync) are explicitly listed and don't block v2 telemetry work.

## What we are asking for

1. **Approval of the Hybrid architecture** as the v2 frontend strategy — desktop-primary today, browser-deferred.
2. **Approval to begin the v2 hardening phase** — telemetry pipeline construction, ~4 months (17 weeks), 7 engineers, phased build with integration-test checkpoints at each phase.
3. **Defer SG-side Mode B (browser frontend) activation** until a customer signal warrants it. No engineering work allocated until then. (The DRS-side webapp on WS2 is unaffected — it is required, present in every deployment, and included in the v2 hardening phase scope.)

If approved, the team starts on `drs-server` and `drs-bridge` in parallel; `Sg.App` extension (Transmitter / Receiver entity types) runs alongside on the existing C# track.

## References

- [Decision Record](decision-record.md) — 19 ADRs covering every architectural choice.
- [Architecture Overview](architecture-overview.md) — 5–8 page deeper read for the technical executive.
- [Design Review Brief](design-review-brief.md) — 25-min pre-read targeted at the design-review meeting (CEO + engineering team).
- [Operator Playbook](operator-playbook.md) — workflow-level description of what operators do, anchored on RFQ Annexure A.1.
- [Design Backlog](design-backlog.md) — Milestone-1 design items still outstanding (UX wireframes, etc.).
- [Hybrid design spec](specs/hybrid-frontend-design.md) — full proposal for the chosen architecture.
- [v2 tech-stack archive](specs/v2-tech-stack-archive.md) — exhaustive analysis of all alternatives considered (~5,000 lines, archived).
