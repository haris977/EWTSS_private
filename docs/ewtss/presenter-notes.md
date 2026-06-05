# EWTSS v2 Design Review — Presenter Notes

**Audience:** dev team (7 engineers) + project manager — *all have v1 background, so skip the "why rebuild" sell* · **Budget:** 60–90 min + Q&A · **Frame:** design review, not an execution-scope-locking session (PM drives execution separately)

**Pre-reads sent 2 days ahead:**
- `docs/ewtss/architecture-diagram.md` (5 min — the visual)
- `docs/ewtss/operator-playbook.md` §1 + §14 (10 min — personas + RFQ → v2 traceability matrix)
- `docs/ewtss/decision-record.md` (skim ADR-001, -005, -013, -017, -018, -019 — load-bearing ones)

---

## Act 1 — Frame the meeting · 3–5 min

Skip the legacy audit; team lived it. Land three things only:

1. **This meeting is a design review.** Goal: walk the design end-to-end so everyone has the same mental model. Execution-scope locking happens in a separate PM-led session.
2. **The RFQ (Annexure A.1 + A.2) is the reference bar.** Every architecture choice exists to answer something from the RFQ. We close the meeting with a traceability matrix showing the alignment.
3. **Where the doc set lives** — `docs/ewtss/README.md` is the navigation index; everything is reachable from there.

---

## Act 2 — Design overview · 15–20 min

**Open the architecture diagram live:** `docs/ewtss/architecture-diagram.md` (renders in GitHub).

**Walk top-down through the 7 layers** (`docs/ewtss/architecture-overview.md` §2.5):

| Layer | What |
|---|---|
| L6 Presentation | Sg.App (WPF, WS1) + DRS webapp (Angular, WS2) |
| L5 API gateway | IScenarioBackend (in-process) + drs-server FastAPI |
| L4 Business logic | Sg.Domain (C#) + drs-server services (Python) |
| L3 Message bus + persistence | Kafka 3.x KRaft + PostgreSQL 16 + TimescaleDB |
| L2 Protocol bridge | drs-bridge (asyncio + C++ parsers per variant) |
| L1 Transport | TCP/IP over LAN |
| L0 Hardware / external | DRS devices, SDFC, STK 12 |

**Two-workstation split:** WS1 hosts SG-side L4–L6; WS2 hosts L2–L5 + DRS-webapp L6. Cross-LAN only at **L4↔L3** (PostgreSQL `computed_links` writes from Sg.App) and **L5↔L6** (drs-server REST/WS reads consumed by Sg.App + DRS webapp).

**Hybrid framing (ADR-001):** today = **Mode A** (WPF + STK ActiveX in-process). Future opt-in = **Mode B** (Angular+Cesium SPA + ASP.NET Core). Shared Sg.Domain. Saves cost today, preserves optionality.

**5–6 ADRs to land** (from `docs/ewtss/decision-record.md`):
- **ADR-001** Hybrid frontend
- **ADR-005** One STK Engine seat / WS1 only — drives the WS1/WS2 split + licence economics
- **ADR-013** Event subscription discipline (load-bearing for pan smoothness)
- **ADR-017** No runtime mocks — integration tier requires real STK + real Kafka + real DB
- **ADR-018** DRS webapp on WS2 — browser-based, served by drs-server
- **ADR-019** mvp4 (reference) / sg-app (production) namespace split

---

## Act 3 — How operators and trainees interact · 12–15 min

`docs/ewtss/operator-playbook.md` is the canonical doc — open it live for this act.

**Two personas (§1):**

| Persona | Workstation | Their app | What they do |
|---|---|---|---|
| **SG Operator** | WS1 | Sg.App (WPF) | Authors scenarios, runs link-analysis compute, controls exercise lifecycle, reviews reports, manages EW library |
| **DRS Engineer** | WS2 | DRS webapp (browser) | Monitors hardware health + per-variant live data, configures hardware IP/network, reviews sent/received message logs, runs DRS-side diagnostics |

**Walk the SG Operator workflow** (Operator Playbook §3–§10) end to end:

1. **Pre-exercise setup (§3)** — login, scenario selection, EW library prep
2. **Scenario authoring (§5)** — add aircraft / facility / sensor entities, set parameters, click-to-place on globe (mvp4 + sg-app pattern; STK in-process)
3. **Scenario computation (§7)** — link analysis runs locally on WS1, results write to TimescaleDB on WS2
4. **Exercise execution (§8)** — Random / Scenario / Integrated modes; control via toolbar (start, pause, resume, stop)
5. **Live monitoring** — operator UI shows live telemetry from drs-bridge via WebSocket
6. **Reports (§10)** — PDF generation from drs-server's REST endpoints

**DRS Engineer workflow (§9):** browser-based on WS2. Per-variant health tabs (signal strength, packet loss, last-seen), IP config forms, log viewer with filter/sort. Distinct from SG Operator — different audience, different surface.

**Classroom training implications** (RFQ-implied, design-postmortem §4.11 deferred):
- v2 today: **single SG workstation per deployment.** Instructor + trainees share one WS1 in turn-taking mode (not simultaneous multi-user).
- Multi-station classroom (5 trainee + 1 instructor WS1s) is **out of current scope** — flag if customer expects otherwise. This is one of the items to clarify in the design review with PM.
- **User manuals** (RFQ Milestone 2 #5) are the trainee-facing deliverable; built from the Operator Playbook starting week 12.

**UX dimension still open:** **B1.1 detailed UX wireframes** (Milestone-1 deliverable, `docs/ewtss/design-backlog.md` headline item). Without these, B's Sg.App work and G's DRS webapp work will design-as-they-go. Flag as a PM-tracked deliverable.

---

## Act 4 — Data + control flows · 15–20 min

Open `docs/ewtss/command-flows.md` live — it has Mermaid sequence diagrams for every flow.

### Data flow — three paths to understand

**Path 1: Telemetry (high-volume, hardware → operator UI)** — `command-flows.md` §3.1 (Random) + §3.2 (Scenario) + §5.1 (live broadcast):
```
DRS hardware → TCP (L1) → drs-bridge (L2, C++ parser via ctypes)
            → Kafka topic hw.<variant>.<kind> (L3)
            → drs-server consumer (L4) → TimescaleDB hypertable (L3)
                                       → WebSocket fan-out (L5)
                                       → operator UI on WS1 (L6) AND DRS webapp on WS2 (L6)
```
*Target: 2,000 msg/s sustained for 30 min (RFQ §F).* Acceptance gated by Phase 7 load test.

**Path 2: Scenario authoring + compute** (lower-volume, operator → hardware):
```
SG operator click-to-place (L6) → Sg.App view-model (L4)
       → IScenarioBackend.UpdateXxx (L5 in-process) → StkScenarioBackend → STK Engine (L0)
       → compute link analysis → computed_links rows → PostgreSQL on WS2 (L3, cross-LAN)
       → drs-bridge ResponseRouter (L2) reads on each device response → emits per-IRS frame to hardware
```
*Path 3 latency p99 ≤ 30 ms* — measured at Phase 4 + Phase 5 gates.

**Path 3: Reports + historical queries** — drs-server REST `/measurements`, `/reports`, `/messages`. Aggregations over TimescaleDB hypertables (chunk exclusion + composite indexes from day one — structural fix for v1's query-degrades-with-table-growth problem).

### Control flow — the sequences worth walking

Open `docs/ewtss/command-flows.md` and walk these in order:

- **§1 Authentication** — JWT issued by sg-side, validated by drs-server (shared secret, no inter-service HTTP)
- **§3.1–§3.2 Exercise execution per mode** — Standalone Random, Standalone Scenario, Integrated. Critical to understand the three modes early.
- **§3.4 Playback control** — pause / resume / stop semantics (RFQ §B mandate)
- **§4.4 Message log review** — DRS Engineer's diagnostic flow
- **§5.4 Time synchronisation (B1.3)** — Meinberg NTP between SG and WS2, three-tier `SyncStateEngine` (HEALTHY → DRIFT_WARN → DRIFT_ALERT → SYNC_LOST), exercise auto-pause on `sync_lost`. Open `docs/ewtss/specs/time-sync-design.md` if anyone wants depth.
- **§6 Error recovery** — what happens when the LAN cuts mid-exercise (auto-reconnect; cached state)

---

## Act 5 — RFQ alignment · 8–10 min

Open `docs/ewtss/operator-playbook.md` §14 — the RFQ → v2 traceability matrix. This is the spine of the SRS deliverable (Milestone 1 #1).

**Walk through the major RFQ asks:**

| RFQ section | What's asked | How v2 answers |
|---|---|---|
| §1.1 SG | Scenario authoring with EW emitter library | Sg.App + Sg.Domain + StkScenarioBackend (Mode A); future Mode B via Angular SPA |
| §1.2 SG emitter parameters | Correct emitter params per COM / RADAR / SJRR / AUS / PADS subtypes | Typed entities — B1.x adds Transmitter / Receiver / Antenna DTOs |
| §1.6 DRS comms | Send/receive per available IRS | drs-bridge generic asyncio TCP + per-variant C++ parser library (one YAML profile + one .dll per variant) |
| §A 100 instances @ 20 Hz | 2,000 msg/s sustained | Kafka KRaft + TimescaleDB hypertables; verified at Phase 7 gate |
| §B Playback control | Pause/resume/stop mission playback | Exercise lifecycle in Sg.App + matching drs-bridge state machine; command-flows §3.4 |
| §F Time sync | Internal time sync across SG ↔ WS2 | B1.3 design — Meinberg NTP + three-tier SyncStateEngine; ≤10 ms convergence |
| Milestone 1 deliverables | SRS, dev licence, runtime engines, IRS docs | Open: SRS + ATP drafts (B1.18); dev licence allocation TBD with customer; IRS docs per-variant TBD |
| Milestone 2 deliverables | ATP, STD, STR, user manuals, hardware | User manuals built from Operator Playbook; ATP authored from this design's acceptance criteria |

**Open items worth surfacing as RFQ-driven** (`docs/ewtss/design-backlog.md`):
- B1.1 UX wireframes (Milestone-1 deliverable)
- B1.17 IRS document store + version management
- B1.18 SRS / ATP / STD / STR drafts
- B1.20 Lab acceptance (Phase 1 NTP smoke)
- B1.22 User manuals

---

## Act 6 — Execution status (light) · 5 min

*Keep it short — PM drives execution scope separately. Just orient the room on where things stand.*

**Landed recently** (`README.md` §Status):
- Reference C++ parser template (closes parser-ABI contract gap)
- Kafka infrastructure layer (local KRaft broker + control-plane topics)
- drs-bridge + drs-server runtime skeletons
- B1.3 Time Sync — drs-server side + sg-app Phase 5 (banner host + auto-pause)
- Doc reorg → `docs/ewtss/{specs,plans}/`
- Repository + release strategy settled — polyrepo split (6 repos), namespaced SemVer tags, `ewtss-release` coordination repo + committed per-repo Claude Code standard (`docs/ewtss/specs/repository-and-release-strategy.md`). *PM-led; orient only.*

**For engineers' follow-up reading:** open `docs/ewtss/v2-execution-plan.md` §3 and find your own `§3.x` bullet — that's your scope. Cross-engineer dependencies are documented there too. Bring questions to the PM-led execution session.

**Two operational items worth flagging to PM** (not for resolution today):
- CI hosted runners disabled at GitHub Enterprise org level — workflow parked at `.github/disabled/`
- STK 12 dev licence allocation — single seat vs multi-seat unclear; needs clarification with customer

---

## Q&A — 10–15 min

**Be ready for:**
- *"How does v2 prevent the v1 telemetry collapse?"* → Path 1 structural changes: async event loop, no per-row commits, hypertable chunk exclusion, no synchronous cross-service HTTP on the hot path (audit AP-1, AP-9, TS-1, TS-4).
- *"What if a customer adds a non-standard ICD?"* → ICD codegen tool produces parser skeletons in hours; reference parser template is the starting point per variant.
- *"What's the cross-WS deployment model?"* → `docs/ewtss/deployment-guide.md` §1 (workstation roles), §5 (install procedure).
- *"How is trainee/multi-user concurrency handled?"* → Today: single SG workstation per deployment; turn-taking in classroom. Multi-station is out of scope unless customer asks (deferred per design-postmortem §4.11).
- *"What's the licensing footprint?"* → ADR-005 (one STK seat per deployment); ADR-006 (perpetual local `.lic`).
- *"Does the operator UI work without STK running?"* → No (ADR-017 no runtime mocks); STK must be reachable at startup.

---

## Doc index — keep open in browser tabs

```
docs/ewtss/architecture-diagram.md        ← Act 2 visual
docs/ewtss/architecture-overview.md       ← Act 2 deep-dive
docs/ewtss/decision-record.md             ← Act 2 ADRs (5–6 highlights)
docs/ewtss/operator-playbook.md           ← Act 3 personas + workflows; Act 5 §14 traceability
docs/ewtss/command-flows.md               ← Act 4 sequence diagrams (data + control)
docs/ewtss/specs/time-sync-design.md      ← Act 4 §5.4 depth
docs/ewtss/design-backlog.md              ← Act 5 + Act 6 open items
README.md (top-level §Status)             ← Act 6 "what's landed"
docs/ewtss/README.md                      ← navigation index (show once)
```

**Don't open during the talk** (link for follow-up only):
- `docs/ewtss/v2-execution-plan.md` — execution scope, PM-led session
- `docs/ewtss/specs/repository-and-release-strategy.md` — polyrepo split + release model + per-repo tooling standard; PM-led, surface at execution session
- `docs/ewtss/developer-handbook.md` — daily reference, per-engineer
- `docs/ewtss/risk-register.md` — PM-tracked, surface at execution session
- `docs/ewtss/icd-reference-comm-df.md` — C++ developer's worked example
- `docs/ewtss/design-postmortem.md` — pre-mortem (PM + architect read after)
- `docs/ewtss/deployment-guide.md` — for whoever owns customer-site install
