# Hybrid Desktop-Primary with Browser-Future Extension — Design

**Status:** Active proposal, awaiting user review.

**Authored:** 2026-05-02. Synthesises the Option D2 vs Option E debate from the [v2 tech-stack design](v2-tech-stack-archive.md) §14 and §22.7 in light of MVP4.5's working artefacts.

**Goal:** Ship the validated MVP4.5 C# WPF + STK ActiveX desktop app as the primary EWTSS deliverable today, AND preserve a low-cost upgrade path to a Cesium-based browser frontend later — **without paying any performance tax in the desktop deliverable for the future browser path.**

## TL;DR

The "hybrid" is not a code-shape inside one process. It is **two deliverables that share a contract**:

- **Today (Mode A) — Desktop:** `Sg.App.exe` runs the WPF UI against an in-process `StkScenarioBackend`. Single executable, full STK COM access, zero network in the data path. This is the working MVP4.5 product.
- **Later (Mode B) — Server + Browser:** `Sg.Server.exe` (ASP.NET Core, future) hosts its own in-process `StkScenarioBackend` behind an HTTP API. An Angular + CesiumJS SPA (`Sg.Web`, future) consumes that API. WPF and Server are mutually exclusive on a workstation per the STK one-engine rule.

The two modes share **the entire `Sg.Domain` project**: the DTO contracts, the `IScenarioBackend` interface, the view-models, and the `StkScenarioBackend` COM implementation. The ASP.NET Core wrapper and the Angular SPA are pure additions; they don't displace anything.

**Performance guarantee for Mode A:** the WPF process never makes a network call to reach STK. The forward-compatibility plumbing is contract-level only — interfaces, DTOs, JSON round-trip guarantees — none of which costs runtime cycles. Mode A's per-frame latency, click responsiveness, and pan smoothness stay exactly as MVP4.5 ships them.

## Why this option exists

§22.7 of the v2 tech-stack spec recommended Option E (Cesium primary, D2 fallback). MVP4.5 raised D2 from "documented fallback" to "validated, working, sample-fidelity alternative" (per §25.4). Customers may want either or both:

- **Power users on a fixed Windows workstation** want STK-Insight-grade authoring fidelity → desktop.
- **Distributed users / control rooms / situational-awareness clients** want zero-install browser access → web.
- **A single customer often wants both for different roles.**

Picking one frontend platform forecloses the other. Picking both as separate projects multiplies the team. The hybrid splits the difference: one shared backend codebase, two thin frontend wrappers — desktop today, web later — gated by deployment mode.

## Goals

1. **Mode A ships unchanged.** MVP4.5 is the deliverable; this design adds no code that runs in the WPF process.
2. **Mode A's performance is sacrosanct.** No HTTP, RPC, or serialisation layer between the WPF view-models and STK. The MVP4.5 perf wins (smooth pan, native click latency, in-process COM) are preserved bit-for-bit.
3. **The future browser path is reachable** with a known, finite scope of additional work that builds on existing artefacts.
4. **A single `IScenarioBackend` contract** is the only seam between the two modes. Both modes consume the same DTOs; the wire format for Mode B is the JSON shape already validated by MVP4.5's round-trip tests.
5. **Deployment is per-workstation** and gated by the existing `MVP4_BACKEND` environment variable.

## Non-goals

- Mode B's full authoring browser frontend is not specified at the implementation level here; that's a separate project (`Sg.Web`) when scoped.
- This design does not commit to a Mode B delivery date. It commits to keeping the door open at zero ongoing cost to Mode A.
- Live multi-user collaboration (two users editing the same scenario simultaneously) is out of scope. Browser users in Mode B see one operator's authoring state at a time.
- Mock-STK runtime fallback is out of scope (mocks remain test-only per the §25.5 lesson).

## Architecture

### Overall architecture — where the frontend swaps

The hybrid architecture has three horizontal layers. The bottom two are shared
across both deployment modes; only the top layer (frontend) changes. Mode B
also inserts an HTTP-bridge layer (the ASP.NET Core server) between its
frontend and the shared core, but the shared core itself is identical bytes.

```
              ┌──────── FRONTEND LAYER (the swap point) ────────┐
              │                                                  │
              │    Mode A (today)         Mode B (future)        │
              │  ┌──────────────────┐   ┌──────────────────────┐ │
              │  │  C# WPF          │   │  Angular + CesiumJS  │ │
              │  │  Sg.App.exe │   │  Sg.Web         │ │
              │  │                  │   │     (browser SPA)    │ │
              │  │  · WPF Views     │   │                      │ │
              │  │    (XAML)        │   │  · Angular comp's    │ │
              │  │  · MVVM bindings │   │  · CesiumJS scene    │ │
              │  │  · STK ActiveX   │   │  · TS HttpScenario-  │ │
              │  │    3D + 2D ctrls │   │    Backend (mirrors  │ │
              │  │                  │   │    C# IScenario-     │ │
              │  │                  │   │    Backend interface)│ │
              │  └────────┬─────────┘   └──────────┬───────────┘ │
              │           │                        │             │
              └───────────┼────────────────────────┼─────────────┘
                          │                        │
                in-process│                        │ HTTPS (REST)
                method    │                        │ + WebSocket
                calls     │                        │ for live events
                          │                        │
                          │             ┌──────────▼───────────────┐
                          │             │  HTTP-BRIDGE LAYER       │
                          │             │  (Mode B only)           │
                          │             │  Sg.Server.exe      │
                          │             │  ASP.NET Core            │
                          │             │   · REST endpoints, one  │
                          │             │     per IScenarioBackend │
                          │             │     method               │
                          │             │   · WebSocket hub for    │
                          │             │     ScenarioChanged push │
                          │             │   · OpenAPI doc =        │
                          │             │     wire-format contract │
                          │             └──────────┬───────────────┘
                          │                        │
                          │                in-proc │ method calls
                          │                        │
                          ▼                        ▼
              ┌────────── SHARED CORE LAYER (unchanged across modes) ──────┐
              │                                                            │
              │  Sg.Domain (same .csproj, same .dll, no fork)         │
              │   ┌────────────────────────────────────────────────────┐   │
              │   │  Sg.Domain.Contracts                          │   │
              │   │   · 8 DTO records (AircraftDto, FacilityDto, …)    │   │
              │   │   · IScenarioBackend interface                     │   │
              │   │   · No AGI.* / no COM types (namespace fence)      │   │
              │   ├────────────────────────────────────────────────────┤   │
              │   │  Sg.Domain.ViewModels                         │   │
              │   │   (consumed directly by Mode A; structure mirrored │   │
              │   │    in Mode B's TypeScript view-models)             │   │
              │   ├────────────────────────────────────────────────────┤   │
              │   │  Sg.Domain.Stk.StkScenarioBackend             │   │
              │   │   (the COM impl — only IScenarioBackend impl       │   │
              │   │    that actually talks to STK; reused unchanged    │   │
              │   │    by both modes)                                  │   │
              │   └─────────────────────────┬──────────────────────────┘   │
              │                             │                              │
              └─────────────────────────────┼──────────────────────────────┘
                                            │
                                            │ AGI.STKObjects.Interop
                                            │ (COM in-process call)
                                            │
                                            ▼
                                    ┌───────────────────┐
                                    │   STK 12 Engine   │
                                    │   (AGI)           │
                                    │                   │
                                    │   one-engine      │
                                    │   licence rule:   │
                                    │   max one         │
                                    │   consumer        │
                                    │   process per     │
                                    │   workstation     │
                                    └───────────────────┘
```

**What swaps when you change modes:**

| Layer | Mode A | Mode B | Same? |
|---|---|---|---|
| Frontend UI tech | WPF + XAML | Angular + CesiumJS | **Different** — the swap point |
| Frontend host | `Sg.App.exe` (Windows desktop process) | `Sg.Web` (browser, served as static assets) | **Different** |
| Transport from frontend to backend | In-process method calls | HTTPS + WebSocket | **Different** |
| HTTP bridge | (none — direct in-process) | `Sg.Server.exe` ASP.NET Core | **Mode B only** |
| `IScenarioBackend` contract | C# interface, called directly | Same C# interface, exposed as REST endpoints + TS mirror on browser | **Same shape, different binding** |
| `Sg.Domain` (DTOs, viewmodels, StkScenarioBackend) | Linked into the WPF process | Linked into the server process | **Identical bytes — same DLL** |
| STK Engine | In-process to `Sg.App.exe` | In-process to `Sg.Server.exe` | **Same — in-process to whichever host owns the deliverable** |

The frontend layer is what an operator perceives. Everything below the dashed
line is invariant across deployments — same code, same DTO shapes, same STK
COM access pattern. **Mode B does not introduce remote STK, remote computation,
or any new approximation; it just hosts the existing core behind an HTTP wall
so a browser can reach it.**

### Per-mode deployment view

The same architecture, drawn as the two distinct workstation deployments side
by side:

Per the RFQ, an EWTSS deployment is **two workstations** — WS1 (Scenario
Generator) and WS2 (DRS / Device Replacement Software) — and a deployment is
configured for **either Mode A or Mode B at install time**, not both. WS1 is
the only STK-bearing workstation in either configuration; only the WS1
process changes between modes. WS2 is identical across modes.

```
─── Mode A configuration on WS1 (today, primary) ──────────────────────
        ┌──────────────────────────────────────────────────────────┐
        │  WS1 — SG (Scenario Generator)                           │
        │  Sg.App.exe (WPF, .NET 8 / Windows)                 │
        │   ┌────────────┐    ┌────────────────────────────────┐   │
        │   │   Views    │ ── │ Sg.Domain                 │   │
        │   │  (XAML)    │    │  · Contracts (DTOs + interface)│   │
        │   └────────────┘    │  · ViewModels                  │   │
        │   ┌────────────┐    │  · Stk.StkScenarioBackend ────┐│   │
        │   │ ObjectTree │ ── │                              ↓│   │
        │   │ Property   │    └─────────────────────────────│┘   │
        │   │ Panel      │                                  ↓     │
        │   │ StkDisplay │ ── COM in-process ──→ STK 12 (AGI)     │
        │   └────────────┘                                          │
        │  Operator authors directly via WPF UI on this workstation │
        └─────────────────────────┬─────────────────────────────────┘
                                  │ LAN to WS2:
                                  │  · PostgreSQL writes (link-analysis)
                                  │  · drs_server WS reads (telemetry)
                ▲ no HTTP, no serialisation, no network in the data path

─── Mode B configuration on WS1 (future, opt-in) ──────────────────────
        ┌──────────────────────────────────────────────────────────┐
        │  WS1 — SG (Scenario Generator)                           │
        │  Sg.Server.exe (ASP.NET Core, .NET 8 / Windows)     │
        │   ┌────────────────────────────────┐                     │
        │   │ Sg.Domain                 │                     │
        │   │  · same DLL as Mode A          │                     │
        │   │  · Stk.StkScenarioBackend      │ ── COM in-process ──┼─→ STK 12 (AGI)
        │   └────────────────────────────────┘                     │
        │   ┌────────────────────────────────┐                     │
        │   │ REST + WebSocket endpoints     │  Serves static      │
        │   │ (one per IScenarioBackend mtd) │  Sg.Web SPA    │
        │   └────────────────────────────────┘  (Angular + Cesium) │
        │                                                          │
        │  Operator browser on this workstation (or LAN-attached   │
        │  non-workstation laptop) loads the SPA over HTTPS+WS     │
        └─────────────────────────┬─────────────────────────────────┘
                                  │ LAN to WS2: same as Mode A
```

WS2 is identical regardless of WS1's mode:

```
─── WS2 — DRS, both modes ─────────────────────────────────────────────
        ┌──────────────────────────────────────────────────────────┐
        │  WS2 — DRS (Device Replacement Software)                    │
        │                                                          │
        │  drs_bridge (Python + C++ parsers)                       │
        │     ↓ Kafka topics                                       │
        │  Kafka 3.x KRaft (single broker)                         │
        │     ↓ consumed by                                        │
        │  drs_server (Python FastAPI)                             │
        │     ↓ writes hypertables + reads link-analysis           │
        │  PostgreSQL 16 + TimescaleDB 2.x                         │
        │                                                          │
        │  No STK; no GPU.                                         │
        └──────────────────┬───────────────────────────────────────┘
                           │ TCP/UDP per hardware spec
                           ▼
                    DRS hardware instances
```

The shaded box (`Sg.Domain`) is identical bytes in both Mode A and Mode B
on WS1 — same `.csproj`, no fork, no copy-paste. STK runs in-process to
whichever WS1 process the deployment is configured for; **a deployment never
runs both processes simultaneously** so the one-engine licence rule (per the
deployment matrix below) is satisfied trivially. Switching modes is a WS1
reinstall, not a runtime toggle.

### What's already in MVP4.5 that supports Mode B

The MVP4.5 work shipped 90% of what Mode B needs. Concretely:

| Artefact | What it gives Mode B |
|---|---|
| `Sg.Domain.Contracts` namespace | Pure DTO surface; no COM types. Same DTOs serialise to JSON for HTTP transport. |
| `IScenarioBackend` interface | The exact RPC surface for Mode B's HTTP API. Each method becomes one endpoint. |
| `DtoJsonRoundTripTests` (8 tests) | Already verifies every DTO is `System.Text.Json` round-trip safe. The wire format is locked. |
| `ContractsNamespaceFenceTests` | Static guarantee that no COM type can leak into Contracts. Browser-side TypeScript can mirror these types verbatim. |
| `App.RegisterBackend(services, mode)` + `MVP4_BACKEND` env var | DI mode switch already accepts `"InProcess"` (works) and `"Remote"` (throws `NotImplementedException`). The hook for `HttpScenarioBackend` is already there. |
| `StkScenarioBackend` itself | Used unmodified by Mode B's server process. The implementation is independent of host (WPF vs ASP.NET Core). |
| `FakeScenarioBackend` | Same fake works for Mode B's server-side integration tests. |

**The only missing pieces are additions, not replacements.** Mode A keeps running on `StkScenarioBackend` directly; Mode B adds new components alongside.

### Mode B's missing pieces

| Component | Where it lives | Approx. effort |
|---|---|---|
| `Sg.Server` (ASP.NET Core minimal API + SignalR/WebSocket hub) | New project, references `Sg.Domain` | 1–2 weeks |
| `HttpScenarioBackend` (.NET implementation of `IScenarioBackend` for non-WPF .NET clients, if any — optional) | New file in `Sg.Domain.Http` namespace | 2–3 days |
| CZML emitter (`StkScenarioBackend` extension or wrapper) | Either a method `string ExportCzml()` on `StkScenarioBackend`, or a separate `CzmlExporter` class | 1 week |
| `Sg.Web` (Angular SPA, CesiumJS scene, TypeScript `HttpScenarioBackend` mirror) | New project, separate repo or sub-folder | 4–8 weeks for full authoring; 2–3 weeks for read-only viewer |
| Wire-format documentation | Generate OpenAPI from the ASP.NET Core endpoints, plus a `wire-format.md` for the SignalR events | 2–3 days |
| Integration tests across the wire | New test fixture that boots the server, hits HTTP endpoints, asserts state via `StkScenarioBackend` injected in-test | 1 week |

**Total Mode B effort estimate:** 6–8 weeks for read-only browser viewer; 12–16 weeks for full authoring browser. Phased delivery is supported (see §Phased rollout below).

## Performance preservation — explicit strategy

This is the load-bearing concern in the proposal. The strategy is enforced by code structure and tests, not just intent.

1. **`Sg.App` (the WPF process) does not depend on any HTTP, gRPC, or remoting framework.** Adding such a dependency is reviewable and rejectable.
2. **`Sg.App` resolves `IScenarioBackend` to `StkScenarioBackend` directly** via DI in `App.RegisterBackend("InProcess", ...)`. The `MVP4_BACKEND=Remote` mode is for the *future* server-mode of the same app or for a thin desktop client — the standalone deployment ships with `InProcess` hard-coded.
3. **The `Sg.Domain.Stk.StkScenarioBackend` runs in the same process as the WPF view-models.** All calls are method invocations, not network round-trips. Method-call latency (microseconds) ≪ HTTP RTT (milliseconds).
4. **No serialisation in the WPF data path.** DTOs are passed by reference between view-models and `StkScenarioBackend`. JSON serialisation (`System.Text.Json`) is exercised only by tests and by the future server.
5. **MVP4.5's perf-critical event-subscription discipline stays in `StkDisplayHost`.** No view-refresh subscription, on-demand mouse-event subscription per mode. The future Mode B server doesn't host this control at all.
6. **Browser-future-only types live outside `Sg.App`'s reference graph.** The `Sg.Server` and `Sg.Web` projects, when they're added, do not become dependencies of `Sg.App`. Mode A's executable footprint is unchanged.

### How we know Mode A perf is preserved

- **Build matrix:** Mode A builds and runs without `Sg.Server` existing at all. The CI / release pipeline can produce a Mode A artefact even before Mode B is started.
- **Reference graph test:** A unit test in `Sg.Tests` that asserts `Sg.App.dll` does not transitively reference any HTTP/network/server assembly (analogous to the namespace-fence test).
- **No latency budget changes:** the existing acceptance criteria in the MVP4.5 plan (smooth pan, no permanent COM event subs, on-demand subscription pattern) continue to apply unmodified.

## Phased rollout for Mode B

Mode B is delivered in phases so partial value is shippable.

### Phase B0 — Today (already done by MVP4.5)

- DTO contracts in `Sg.Domain.Contracts`
- `IScenarioBackend` interface
- `StkScenarioBackend` (in-process COM impl)
- JSON round-trip tests
- Namespace fence test
- Backend-mode DI switch (`MVP4_BACKEND` env var)
- "One-engine rule" documented (per MVP4.5 spec §4.8)

**No further Mode B work today.** The desktop deliverable ships first.

### Phase B1 — Read-only browser viewer (4–6 weeks when started)

- `Sg.Server` ASP.NET Core project hosting `StkScenarioBackend`
- `GET /api/scenario` returning `EntityNodeDto[]` tree snapshot
- `GET /api/aircraft/{path}` returning `AircraftDto`, etc. (one endpoint per typed entity)
- `GET /api/czml` emitting STK-format CZML for current scenario
- WebSocket hub broadcasting `ScenarioChanged` to connected clients
- Angular + Cesium SPA loading CZML, no editing UI yet
- Browser users see what's authored on the server side; authoring still happens elsewhere (could be the Mode A WPF on the same machine, with the server reading a saved `.sc` file — see "Save-bridge" below)

This phase delivers situational-awareness browser viewing for command-room operators while the WPF desktop remains the authoring tool.

### Phase B2 — Authoring browser (8–12 weeks when started)

- `POST/PUT /api/aircraft/{path}` endpoints calling `UpdateAircraft` etc.
- `POST /api/scenario/entities` for AddEntity, `DELETE /api/scenario/entities/{path}` for RemoveEntity
- TypeScript `HttpScenarioBackend` in the SPA mirroring the C# interface
- Angular view-models that bind the same DTO shapes
- CesiumJS-based map interaction: click-to-place, drag-edit handles via Cesium's primitives + custom hit-testing (NOT STK's drag handles, which only exist in the desktop ActiveX control)
- Browser-side validation against the same DTOs

This phase makes the browser a peer authoring frontend.

### Phase B-bridge (optional, between B1 and B2)

For customers wanting "WPF authors, browser views" without running the ASP.NET Core server full-time:

- WPF instance can write `.sc` (or its CZML export) to a shared filesystem location on save
- A lightweight static-file server hosts the CZML
- Cesium SPA loads from the static file
- One-way sync, no live updates beyond polling

This is a low-cost intermediate that doesn't require Phase B1's full server. Useful for early customer demos.

## Deployment matrix and the one-engine rule

Per the RFQ, an EWTSS deployment is two workstations and is configured for either Mode A or Mode B at install time. The two modes never coexist within a single deployment.

| Workstation | Mode A configuration | Mode B configuration | STK seats | Notes |
|---|---|---|---|---|
| **WS1 — SG (Scenario Generator)** | `Sg.App.exe` (WPF) | `Sg.Server.exe` (ASP.NET Core) + `Sg.Web` SPA static assets + browser on WS1 | 1 | The only STK-bearing process on this workstation. Operator authors directly via WPF (Mode A) or via browser to localhost (Mode B). |
| **WS2 — DRS** | `drs_bridge` + `drs_server` + Kafka KRaft + PostgreSQL 16 + TimescaleDB 2.x | Same as Mode A — WS2 is identical across modes | 0 | No STK. Hosts the entire telemetry pipeline plus the shared TimescaleDB that receives link-analysis output from WS1 and telemetry from `drs_server`. |

The MVP4.5 spec's STK Engine licence constraint ("at most one process per workstation can hold the engine handle") is satisfied **trivially** in this deployment shape: only WS1 hosts an STK process, and only one such process is configured per install. There is no concurrency-check needed because the modes are committed at install time, not toggled at runtime.

Switching modes after deployment is a WS1 reinstall: stop the running WS1 process, install the other host (`Sg.App.exe` ↔ `Sg.Server.exe`), update `MVP4_BACKEND`, restart. WS2 is unaffected. The same STK Engine licence seat carries over.

If a customer ever asks for a "thin client" Mode A configuration where the WPF UI runs on WS1 but talks to a remote `Sg.Server` over HTTP (i.e. `MVP4_BACKEND=Remote` actually wired up), that's an additional deployment shape outside the scope of this design. Not on the critical path; the current Hybrid serves the standard two-workstation deployment without it.

## Wire format and contract stability

The DTOs are the shared contract between modes. Stability rules:

- **Adding a field** to an existing DTO is a non-breaking change. Old clients tolerate unknown fields when reading; old servers serialise without the field for old clients.
- **Removing a field** is a breaking change requiring a version bump on the API.
- **Adding a new entity DTO + endpoint** is non-breaking.
- **Renaming a field** is breaking and prohibited; deprecate-and-add instead.
- The OpenAPI spec generated from `Sg.Server` becomes the canonical wire-format document for browser developers.

JSON round-trip tests guard the property-name shape; if a DTO field is renamed or its type changes, the test fails. Browser-side TypeScript types are generated from the OpenAPI document (or hand-written from the C# DTOs), keeping them in lockstep.

## Risks and trade-offs

| Risk | Mitigation |
|---|---|
| Mode B never gets built; the design effort is wasted | Mode B's "investment" is mostly the MVP4.5 work that already shipped (contracts, interface, tests). Marginal future-only effort = the OpenAPI doc + occasional contract review. ≈ 3–5 days of engineer time over the project lifetime to maintain optionality. |
| Mode B authoring duplicates the entire WPF UI in browser code | True. Two UIs is the cost of supporting both deployment models. Phased delivery (read-only first) keeps the cost proportional to demand. |
| WPF performance regresses because someone "helpfully" inserts an HTTP layer in the data path | Reference-graph test (proposed) catches it at build time. Code review against this design document also catches it. |
| The `IScenarioBackend` interface drifts as MVP4.5 evolves, leaving Mode B chasing changes | Treat the interface as a published contract once Mode B starts. Until Mode B starts, it's still internal and free to evolve. |
| One-engine licence rule confuses operators ("why can't I run both?") | Document in installer + startup error message. Already partially covered by MVP4.5's startup STK-init failure mode. |
| Cesium does not have STK's drag-handle editing primitive | Phase B2 reimplements drag editing using Cesium's primitives + custom hit testing. Effort is real but not a blocker; CesiumJS' existing primitive APIs are sufficient. Phase B1 (read-only) avoids the issue entirely. |
| STK CZML export fidelity is incomplete for some entity types | Confirmed during MVP3 that CZML covers Aircraft, Facility, AreaTarget, Sensor, Coverage, FOM. Validate per type during Phase B1. Add custom emission for any gap. |
| ASP.NET Core hosting on the same machine as STK competes with WPF for STK licence | The deployment matrix above forbids both running concurrently. Installer should refuse to install Mode B server on a machine already configured for Mode A daily use. |

## Comparison with alternatives

| Option | Description | Why hybrid is preferred |
|---|---|---|
| **A. Pure desktop, no browser path** | Ship MVP4.5 as is, never plan for browser | Forecloses customer requests for browser deployment; throws away cheap optionality the contract-level seam already gives us. |
| **B. Pure browser (rebuild as Cesium SPA + Python sg-service)** | Abandon the WPF deliverable; ship Option E | Discards the validated MVP4.5 effort; loses STK-Insight-grade authoring fidelity for power users. |
| **C. Embedded HTTP listener inside `Sg.App`** | WPF process exposes its own HTTP for browsers to view live | Conflates two concerns into one process: now WPF must service network traffic on the UI thread; raises perf-regression risk; couples lifetime of browser session to operator's WPF window being open. The phase-B-bridge save-and-static-host pattern achieves the same outcome with less risk. |
| **D. Run `IScenarioBackend` as out-of-process from day one (REST/gRPC) for both WPF and browser** | Symmetrical hosted backend; WPF is just one client | Pays HTTP latency on every WPF interaction. Smooth pan in MVP4.5 depends on in-process COM; routing through HTTP would re-introduce the very latency we just got rid of. **Disqualified by goal #2.** |
| **E. Hybrid (this design)** | WPF in-process today; ASP.NET Core server alongside (not in front) for browser future | Preserves Mode A perf, reuses MVP4.5 work, scopes Mode B as additive, supports phased delivery. |

## Open questions

These don't block approving the design but should be resolved before Phase B1 starts:

- **Scenario synchronisation between Mode A and Mode B** — if a customer runs Mode A on workstation X and wants Mode B browsers on workstations Y, Z, how do scenarios move between them? File-based (Save in WPF, Load in server) is the simplest; live mirroring is a separate project.
- **Authentication / authorisation** for the Mode B HTTP API — out of scope for design but needs an answer before Mode B ships. The v2 tech-stack design's RBAC notes apply.
- **Real-time CZML push** vs polling vs WebSocket DTO push — performance and complexity tradeoff for Phase B1.
- **Whether Mode B's STK process needs a licence different from Mode A's** — clarify with AGI commercial team.

## What changes in MVP4.5 today

**Nothing.** This design is forward-looking; it does not modify the MVP4.5 deliverable. The architecture seam (IScenarioBackend, DTOs, namespace fence, JSON round-trip tests, backend-mode DI) is already in place and was always intended to support exactly this hybrid path.

The only optional code-level addition worth considering before MVP4.5 ships is a **reference-graph test** that asserts `Sg.App.dll` doesn't transitively depend on any networking assembly. This is a guardrail against accidental future regressions; it's a single ~30-line test, deferrable until Phase B1 begins.

## Acceptance criteria

The design is implemented when:

1. ✅ MVP4.5 ships and operators use it on their Windows workstations (Mode A).
2. ⏳ When Mode B starts, the existing `Sg.Domain` project is reused unchanged. No copy, no fork. (Validated by `Sg.Server.csproj` referencing `Sg.Domain.csproj`.)
3. ⏳ The OpenAPI spec generated from `Sg.Server` matches the `IScenarioBackend` method shape and the JSON round-trip tests' DTO shapes.
4. ⏳ A Phase B1 server can be started while Mode A is not running, without any change to `Sg.App`.
5. ⏳ A Phase B1 browser viewer renders the same scenario the desktop authored, by loading CZML from the server.
6. ⏳ The reference-graph test (when added) passes — Mode A has no transitive network dependency.

## References

- `v2-tech-stack-archive.md` — §14 (Option D), §22 (visualization choice), §22.7 (recommendation), §25.3 (MVP4 / MVP4.5 STK COM findings)
- `mvp4.5-dto-boundary-and-perf-design.md` — DTO boundary, IScenarioBackend, namespace fence, backend-mode DI, browser-future readiness §5
- `2026-04-19-mvp3-dataprovider-czml-design.md` — MVP3 CZML emission patterns to reuse in Phase B1
- `mvp4-stk-native-embedded-design.md` — MVP4 baseline architecture
- `EWTSS_CSP_POC` reference repo — canonical C# + STK + WPF integration patterns
