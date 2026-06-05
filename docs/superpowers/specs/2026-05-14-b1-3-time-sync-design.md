# B1.3 — Time Synchronisation Service Design

**Status:** Designed 2026-05-14 (brainstorming session). Pending implementation per the schedule in §7.
**Tracks:** [Design Backlog B1.3](../../ewtss/design-backlog.md#-b13--time-synchronization-service-design-sg-as-time-server).
**RFQ reference:** Annexure A.1 §F *Time Synchronization. This feature facilitates to get the time from the Scenario generator designated as Time Server.*

---

## 1. Problem

EWTSS v2 needs two distinct kinds of time synchronisation that share infrastructure but solve different problems:

1. **Wall-clock sync** — all v2 components (Sg.App on WS1, drs-bridge / drs-server / DRS webapp on WS2) and the client-owned Entity Controllers on the LAN must agree on "what time it is right now." Drives audit log timestamps, IRS frame timestamps the Entity Controllers parse, cross-component event correlation.

2. **Simulation-tick coordination** — during scenario execution, SG and drs-bridge must agree on "we are currently at tick T of exercise X" so per-tick processing happens at the simulation moment each tick was meant for.

These are different problems with different mechanisms; the design covers both as a single integrated story.

## 2. Constraints

- **RFQ §F** mandates SG is the Time Server.
- **Air-gapped LAN** — no internet, no upstream NTP, no public GPS server reachable.
- **Windows-only** after the WS2-Windows decision (commit `c33bb59`).
- **Per-variant precision requirements** — RFQ doesn't specify a global precision target; each variant's IRS may specify its own. The design must accommodate variants that need ≤10 ms as well as variants that need sub-ms (if any are encountered).
- **Classroom training simulator** — real-time pacing is required (instructors and trainees observe in real-time at 1 Hz tick rate per default).

## 3. Architecture — two-layer split

```
┌───── Layer A: Internal NTP (≤10 ms target, single protocol) ──────┐
│                                                                    │
│                    SG (WS1)                                        │
│                    Meinberg NTP daemon  ──── Stratum-1             │
│                                              (SG local RTC = SoT)  │
│                    UDP 123                                         │
│                          │                                         │
│                          │ LAN                                     │
│        ┌─────────────────┼─────────────────┐                       │
│        │                 │                 │                       │
│   drs-bridge         drs-server         DRS webapp (browser)       │
│   Meinberg client    Meinberg client    inherits drs-server clock  │
│   (Windows Service)  (Windows Service)  via GET /time/status       │
└────────────────────────────────────────────────────────────────────┘

┌───── Layer B: External per-IRS adapter (per-variant, in drs-bridge) ┐
│                                                                     │
│   drs-bridge  ──────────► Entity Controller (variant X) 🔌          │
│   reads NTP-synced       receives IRS frames carrying time-signal    │
│   wall-clock,            per the variant's IRS specification         │
│   formats per IRS        (embedded timestamp OR periodic beacon)     │
└─────────────────────────────────────────────────────────────────────┘
```

**Layer A** is one protocol (NTP via Meinberg) chosen by v2; v2 owns the implementation. **Layer B** is per-variant, defined by each variant's IRS specification; v2 implements the adapter side via the C++ parser library for that variant.

## 4. Layer A — Internal NTP with Meinberg

### 4.1 Protocol choice

**Meinberg NTP daemon for Windows** — free, open-source, mature NTP implementation. Common in defence-system deployments. ≤1 ms convergence on a quiet classroom LAN; our design target is ≤10 ms which gives 10× headroom.

Alternatives considered + rejected:
- **Windows W32Time built-in** — out-of-box accuracy is ~1–2 s; "high accuracy" mode improves to ~10–50 ms but Microsoft formally supports W32Time only at the 1-second level. Insufficient headroom; less battle-tested for the precision regime we need.
- **Custom Kafka heartbeat** — reuses existing infrastructure but Kafka delivery latency is variable (≥50 ms typical). More custom code than necessary for a solved problem.
- **PTP (IEEE 1588)** — overkill for classroom training; requires PTP-aware switches the customer may not have.

### 4.2 SG configuration

- Meinberg NTP daemon installed via vendored MSI from `packages/`.
- Stratum-1 server mode.
- `LOCAL` reference clock configured with `stratum 10 trust` (SG's local RTC is the authoritative source; the daemon serves even when no upstream is reachable — required for air-gapped operation).
- UDP 123 listening on the SG's LAN interface only.

### 4.3 WS2 configuration

- Meinberg NTP daemon installed via the same vendored MSI.
- Client mode pointing at the SG's LAN IP.
- Configured as a Windows Service (per [Developer Handbook §15.4](../../ewtss/developer-handbook.md), Automatic startup).
- Polls SG every 64 s by default (NTP standard); aggressive `minpoll 4 maxpoll 6` to keep convergence tight.

### 4.4 DRS webapp

The webapp runs in a browser on WS2 and inherits WS2's clock. It does not sync independently. It queries `GET /time/status` on drs-server to render the time-sync dashboard card. Displayed times in the webapp UI use WS2's local clock (which is NTP-synced to SG).

## 5. Layer B — Per-variant time adapter in drs-bridge

### 5.1 Two patterns

Most variants' IRS specs fall into one of two patterns:

**Pattern 1 — Embedded timestamp in every IRS frame** (most common):
- Every outbound IRS message carries a timestamp field.
- drs-bridge reads its NTP-synced wall-clock at format time.
- `parser.format_response(kind=<msg-kind>, payload=..., timestamp_ns=now)` produces IRS bytes with the timestamp embedded per the IRS spec.
- Entity Controller reads timestamp from each message; uses the message stream itself as the time-distribution channel.

**Pattern 2 — Periodic dedicated time-distribution message**:
- A separate IRS message type that's broadcast periodically (e.g., every 100 ms or 1 s).
- drs-bridge spawns a per-variant `TimeBeaconCoroutine` that periodically calls `parser.format_response(kind="time", timestamp_ns=now)` and sends the result to the Entity Controller.
- Runs independently of scenario tick processing.

Variants may use Pattern 1, Pattern 2, both, or neither (rare).

### 5.2 C++ parser ABI — unchanged

The existing 4-symbol ABI (`extract_frame`, `parse_message`, `format_response`, `free_result`) is sufficient. Pattern-2 time-signal flows through `format_response(kind="time", ...)`. Variants implement that kind only if their IRS requires periodic distribution; variants that don't never invoke it. No new ABI symbol.

### 5.3 YAML profile schema extension

Each variant's profile gains a `time_signal` block:

```yaml
variant: rdfs
parser_lib: parsers/rdfs/parser.dll
ports:
  command:  { host: 0.0.0.0, port: 5001, protocol: tcp }
  response: { host: 0.0.0.0, port: 5002, protocol: udp }
time_signal:
  embedded_in_messages: true        # Pattern 1: IRS frames carry inline timestamp
  periodic_distribution:
    enabled: false                   # Pattern 2: separate time beacon
    interval_ms: null                # only if enabled
  precision_required_ms: 10          # drs-bridge alerts if effective offset > this
```

`precision_required_ms` is the per-variant threshold; defaults to 10 (matches the global internal-sync target). Variants with stricter IRS specs declare lower values.

## 6. Tick coordination — Layer A + Layer B together

Already partially designed in [command-flows §3.2](../../ewtss/command-flows.md#32-standalone--scenario-mode-execution). This design adds the timing semantics:

1. **Real-time pacing.** SG paces tick publishing via a wall-clock loop. Tick T is published to Kafka `scenario.execution` at `exercise_start_wallclock + T * tick_interval_ms`. Because SG's clock is the NTP source, "now" on SG is canonical.

2. **drs-bridge tick processing.** drs-bridge consumes tick T → reads its wall-clock (≤1 ms-aligned with SG via NTP) → calls `parser.format_response(timestamp_ns=now)` → TCP/UDP to Entity Controller. The timestamp in the outgoing IRS frame is drs-bridge's wall-clock at the moment of formatting — which equals SG's wall-clock at that moment.

3. **Tick-lag detection.** drs-bridge tracks `consume_wallclock - intended_wallclock_for_tick_T` (where intended = `exercise_start + T * interval`). If consistent lag > 100 ms over 5 consecutive ticks, drs-bridge publishes a `tick.lag.warning` health event → operator alert. Lag > 500 ms triggers a stronger alert and offers a "pause exercise" prompt.

4. **Simulation clock = wall-clock under real-time pacing.** No separate "simulation time" abstraction is needed. If a future requirement adds time-warp (run exercise at 2× speed), this assumption needs revisiting; that is out of scope for v2.

5. **Pause / resume / replay** already covered in [command-flows §3.4](../../ewtss/command-flows.md#34-playback-control--pause--resume--stop). Pause halts tick publishing on SG; drs-bridge stops processing scenario ticks. Periodic time beacons (Pattern 2) continue during pause if the variant's IRS expects them (per-variant decision; YAML profile can declare).

## 7. Sync-loss handling — three-tier degradation

### 7.1 NTP convergence at startup

`Sg.App` refuses to start an exercise until all WS2 services report a healthy sync status. This avoids the failure mode where an exercise begins immediately after WS2 reboot while Meinberg is still converging.

`drs-server GET /time/status` returns:

```json
{
  "current_time": "2026-05-14T12:34:56.789Z",
  "ntp_offset_ms": 0.4,
  "ntp_jitter_ms": 0.2,
  "ntp_peer": "WS1-SG.local",
  "last_sync": "2026-05-14T12:34:50.000Z",
  "status": "healthy"
}
```

Status values: `"healthy"`, `"warming"`, `"drift"`, `"lost"`.

### 7.2 Three-tier thresholds

Admin-configurable defaults, aligned to the 10 ms internal sync target.

**Polling cadence:** `drs-server` polls the local NTP daemon every **5 s** by default. "5 consecutive readings" in the table below = 25 s observation window. Polling interval is admin-configurable per §8.

| Tier | Default threshold | Behaviour |
|---|---|---|
| `HEALTHY` | offset ≤ 10 ms sustained ≥ 30 s (= 6 readings at 5 s polling) | Green indicator. No banner. |
| `DRIFT_WARN` | offset > 10 ms for 5 consecutive readings | Yellow banner in `Sg.App` + DRS webapp. Exercise continues. Audit log entry. |
| `DRIFT_ALERT` | offset > 50 ms for 5 consecutive | Red banner + modal in `Sg.App`. Outbound IRS frames marked "sync-degraded" in the system log (and in-band if variant's IRS supports a degraded-flag bit). Exercise continues but operator prompted to consider pausing. |
| `SYNC_LOST` | offset > 200 ms **or** no NTP response > 60 s | Exercise auto-pauses. Operator must acknowledge before resume. Audit log + alarm. |

### 7.3 Per-variant override

If a variant's YAML declares `precision_required_ms: 1`, that variant individually transitions to ALERT at offset > 1 ms regardless of the global default. That variant's outbound IRS feed pauses while other variants continue. Operator sees per-variant status in the DRS webapp.

## 8. Operator surface

**Sg.App admin → Time Sync view** (new admin panel, RBAC `admin` role):
- SG's canonical clock (live tick).
- Per-WS2-service status card: drs-bridge, drs-server (offset, jitter, status, last sync timestamp).
- Per-variant status table: variant name, precision_required_ms, current offset, status.
- 24-hour drift chart (per service).
- Admin config: WARN / ALERT / LOST thresholds + polling interval (default 5 s).

**DRS webapp dashboard → Time Sync card** (always visible to DRS Engineer):
- Local offset from SG with traffic-light indicator.
- Last sync timestamp.
- Status: HEALTHY / WARN / ALERT / LOST.

**DRS webapp variant detail → per-variant Time Sync row** (in the existing Health detail panel from [command-flows §4.4](../../ewtss/command-flows.md#44-message-log-review-with-filter--sort)):
- Configured precision_required_ms.
- Current effective precision (NTP offset).
- Per-variant status.
- Last IRS frame timestamp.

## 9. Audit + alerting

Every state transition (HEALTHY ↔ WARN ↔ ALERT ↔ LOST, per service or per variant) writes a row to TimescaleDB. Feeds the **System log** class of the 4-class log management ([B1.13](../../ewtss/design-backlog.md#-b113--log-management-ui-4-class)).

Additional audit events:
- Meinberg daemon start / stop (Windows Service supervisor events).
- Admin threshold configuration changes.
- Operator acknowledgment of SYNC_LOST events.
- Exercise pause caused by sync-lost vs operator-initiated pause (distinguishable in the log).

## 10. Implementation breakdown

| Work item | Owner | Effort | Slots into |
|---|---|---|---|
| Vendor Meinberg NTP MSI under `packages/` | D | 0.5 day | B1.18 (vendoring process — first real consumer) |
| SG-side install: Stratum-1 server with `LOCAL` trust | D | 0.5 day | Week 1 infrastructure setup |
| WS2-side install: Meinberg client as Windows Service against SG | D | 0.5 day | Week 1 infrastructure setup |
| YAML profile schema extension (`time_signal` block) | A | 0.5 day | Week 2 contracts gate |
| `drs-bridge`: per-variant `TimeBeaconCoroutine` (Pattern 2 support) | A | 1 day | Week 6 (alongside Random mode rollout) |
| `drs-bridge`: tick-lag detection + health event publishing | A | 0.5 day | Week 9 |
| `drs-server`: `GET /time/status` endpoint | F | 0.5 day | Week 5 |
| `drs-server`: threshold-state engine + Kafka `system.timesync` topic publisher | F | 1 day | Week 8 |
| `drs-server`: per-variant per-service tracking against `precision_required_ms` | F | 1 day | Week 10 |
| `Sg.App`: exercise-control gating on `time/status == healthy` | B | 0.5 day | Week 8 |
| `Sg.App`: Admin → Time Sync view | B | 2 days | Week 10–11 |
| `Sg.App`: WARN / ALERT / SYNC_LOST banners + modals | B | 1 day | Week 11 |
| DRS webapp: Time Sync dashboard card | G | 1 day | Week 6–8 (alongside dashboard build) |
| DRS webapp: per-variant Time Sync row in Health detail | G | 0.5 day | Week 9–11 |
| **Total** | | **~11 dev-days** distributed | Weeks 1–11 |

C++ parser ABI extension: **none required.** Existing 4-symbol ABI sufficient.

## 11. Testing

| Test | Phase gate | Pass criterion |
|---|---|---|
| Meinberg convergence on dev LAN within 60 s after WS2 boot | Phase 1 (week 2) | ntp_offset_ms ≤ 10 within 60 s; sustained ≤ 10 over next 5 min |
| `/time/status` endpoint returns correct state for each tier | Phase 2 (week 5) | All four states reachable; transitions correctly logged |
| Tick coordination: tick T received at drs-bridge within tick_interval × 1.1 of SG publish time | Phase 4 (week 11) | p99 consume-lag < 110 ms for 1 Hz over 5-min run |
| Per-variant precision enforcement: variant with precision_required_ms=1 transitions to ALERT when injected drift > 1 ms | Phase 4 (week 11) | Per-variant status flips; IRS feed for that variant pauses |
| SYNC_LOST auto-pause: simulate NTP server crash on SG; verify WS2 detection and exercise auto-pause within 60 s | Phase 5 (week 13) | Loss detected within 60 s; Kafka publishes lost event; Sg.App auto-pauses; operator dialog appears |
| Operator UX: admin threshold change reflected within 30 s across all subscribers | Phase 5 (week 13) | Change persists in DB; broadcast via WebSocket; subscribers update; audit row written |
| 30-minute sustained NTP healthy under 2,000 msg/s load | Phase 7 (week 17) | Offset stays ≤ 10 ms; no degradation under load |
| DVD install: time sync works on freshly-installed two-WS deployment with no manual config | Phase 6 (week 15) | After OS install + DVD install, NTP converges to ≤ 10 ms within 60 s of WS2 boot |

## 12. Out of scope

- **GPS-based external time source on SG** — RFQ doesn't require it; air-gapped classroom uses SG's local RTC. Future customer requirement can add a GPS receiver to SG as a procurement add-on.
- **Time-warp / accelerated simulation** — not in RFQ; current design assumes wall-clock pacing.
- **Cross-deployment time-sync** — out of scope; single-site deployment only per RFQ.
- **Persistent NTP-offset history across reboots** — operationally nice but not required. State resets to "warming" on every WS2 reboot.
- **PTP migration path** — if a future variant's IRS requires <1 ms precision and customer infrastructure supports PTP, the design can absorb it: replace Meinberg NTP with a PTP daemon, keep the same `precision_required_ms` semantics, same threshold engine, same operator surfaces. Not in v2 scope.

## 13. Dependencies on other backlog items

- **B1.13** (Log Management UI 4-class) — Time Sync events feed the System log class.
- **B1.16** (RBAC role definitions) — admin threshold-config UI is RBAC-gated.
- **B1.18** (Air-gap vendoring process) — Meinberg NTP MSI is the first concrete vendored binary that the process must accommodate.

## 14. Open items

None within scope. Design is complete and ready for implementation per §10 schedule.

## 15. References

- RFQ Annexure A.1 §F — Time Synchronization mandate.
- [Architecture Overview §3.10](../../ewtss/architecture-overview.md#310-external-integrations-out-of-v2-development-scope-) — Entity Controller integration boundary.
- [Command Flows §5.4](../../ewtss/command-flows.md#54-time-synchronisation--sg-as-time-server) — initial sequence-diagram for SG-as-Time-Server (this design replaces the "protocol TBD" notes with concrete Meinberg + per-IRS adapter design).
- [Command Flows §3.2–§3.4](../../ewtss/command-flows.md#32-standalone--scenario-mode-execution) — scenario tick publishing pattern that Layer A + Layer B work with.
- [Design Backlog B1.3](../../ewtss/design-backlog.md#-b13--time-synchronization-service-design-sg-as-time-server) — the backlog item this design closes.
- [Developer Handbook §15.4–§15.7](../../ewtss/developer-handbook.md) — branching, CI, code review, licence allow-list (Meinberg NTP is on the allow-list as an open-source NTP daemon).
