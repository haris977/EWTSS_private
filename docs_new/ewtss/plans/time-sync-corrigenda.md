# B1.3 Time Sync Plan — Corrigenda & Validation Findings

> Captured while executing [the B1.3 plan](time-sync-plan.md) via
> `superpowers:subagent-driven-development` against branch
> `feat/b1-3-time-sync`. Records blockers and ambiguities the plan would have
> handed to a real implementing engineer, with the fix that was applied or
> deferred. Reference document — not a plan to execute.

## Scope of this exercise

This branch is **not** intended to be merged. It is a spec-validation pass
that uses live TDD subagents to compile and run the plan's code, surfacing
any blocker the spec would have produced once the real engineers started. The
goal is to feed those findings back into the plan, the design spec, and the
broader writing-plans process before development actually begins.

Phases executed end-to-end so far:

- Phase 1 — Tasks 2, 3, 4 (Phase 1 install + smoke scripts) authored. Task 1
  partially executed (provenance docs only; MSI vendoring remains manual).
- Phase 2 — Tasks 5, 6 (NtpMonitor + `/time/status`) executed under TDD with
  two-stage subagent review.

## Findings

### F1. Plan paths assumed scaffolding that did not exist

**Symptom.** Plan referenced `drs/drs-server/app/services/...`,
`from app.services.xxx import ...`, and `cd drs/drs-server` throughout. The
repo had only `mvp1`-`mvp4` directories; none of the v2 services existed yet.

**Fix applied.** Scaffolded minimal Python packages in `drs-server/` and
`drs-bridge/` using a modern src-layout (`src/<pkg>/`). Updated subagent
prompts to specify the corrected paths inline. Phase 2 code was written into
the scaffolded paths, not the plan's.

**General lesson.** A plan that names a file path is asserting that the
parent directory already exists or will be created by an earlier task. Verify
that every path the plan touches has an explicit creation step somewhere
earlier — either a scaffolding task at the top of the plan or a directory
that already lives in the repo. If neither is true, the plan is incomplete.

### F2. Forward-referenced types in tests

**Symptom.** Task 6's test imported `SyncStateEngine`, which is defined in
Task 7. Pure-text plans don't flag this; the import would have ImportError'd
at the first test run.

**Fix applied.** Task 6 was extended to introduce `sync_state_engine.py`
with only the `SyncStatus` enum, leaving the full engine class for Task 7.
The plan's original test had an unused `SyncStateEngine` import; the
subagent prompt explicitly dropped it.

**General lesson.** When tests in Task N import names that don't exist until
Task M (M>N), either re-order, split the introducing type into its own
earlier task, or pre-declare a minimum scaffolding. Plans should be
topologically valid against their own import graph.

### F3. Deprecated stdlib APIs in plan code

**Symptom.** Plan used `datetime.utcnow()` (deprecated as of Python 3.12)
in 17 code blocks. Tests produced `DeprecationWarning` on Python 3.11.9.

**Fix applied.** Replaced with `datetime.now(timezone.utc)` in the
implementation (commit `cd4654c`) and across all plan code blocks
(commit `222860e`). Imports updated to include `timezone`.

**General lesson.** Plans should target the Python version the project
actually pins, not the version the author wrote against years ago.
A linter pass over the plan's code blocks before publication would catch
this class of issue — adding a doctest harness to plan files is feasible
but expensive; a simpler heuristic is to grep for known-deprecated APIs.

### F4. Vendored-binary task has a non-codable step

**Symptom.** Task 1 instructed downloading the Meinberg NTP MSI from the
public web. A subagent cannot do this against an air-gapped repo (and
shouldn't even if it could — provenance & signature verification need a
human).

**Fix applied.** Task 1 was executed as "docs-only": `packages/installers/meinberg-ntp/README.md`
and `packages/THIRD-PARTY-LICENCES.md` were written with placeholders for
the SHA-256 hash, and the README documents the manual procedure. The
binary itself is still pending.

**General lesson.** Plans should clearly mark tasks that require human
action (binary downloads, hardware operations, vendor approvals) with a
visible "manual" tag. Mixing automatable and non-automatable steps inside
a single numbered task creates ambiguity for both human reviewers and
agents.

### F5. Hardware-dependent acceptance gates

**Symptom.** Tasks 4 and 27 (Phase 1 smoke test + 30-min sustained load
test) require physical WS1 + WS2 hardware. Subagents executed the script
authoring but cannot run the script.

**Fix applied.** Script authored and committed (`373205a`); script's
intended manual run is documented in the corrigenda + execution plan.

**General lesson.** Mark acceptance gates as "code-only" vs. "requires
lab". An execution plan that mixes the two without labelling them will
mislead a project tracker into thinking the gate has actually been
exercised.

### F6. Python target version mismatch with dev environment

**Symptom.** Plan + scaffolding pyproject said `requires-python = ">=3.12"`
(originally). Dev box had 3.9, 3.11, and 3.14 — no 3.12.

**Fix applied.** Relaxed scaffold to `>=3.11`. No 3.12-specific syntax was
used in Phase 1/2 code, so the relaxation is safe so far. Phase 3+ should
re-check.

**General lesson.** Either pin the version the project's lab/CI provides,
or invest in `pyenv install` / `winget install` instructions inside the
plan. Pinning a version the developer can't install is a friction tax.

### F7. Node/Angular toolchain not pre-installed

**Symptom.** Phase 6 (Tasks 21-23, 26) needs Angular CLI. Dev box has
Node 24 and npm 11, but no `ng`.

**Fix applied.** None yet — Phase 6 not executed. Scaffolding for
`drs-webapp/` is deferred to when those tasks run.

**General lesson.** Plans that span multiple language stacks should
verify each stack's prerequisites in an explicit Phase-0 setup task,
not assume the dev box has every CLI globally installed.

### F8. `.gitignore` conflicted with documentation under `packages/`

**Symptom.** The repo's `.gitignore` excludes `packages/` wholesale
(to keep vendored binaries out of git). The plan creates documentation
files (`README.md`, `THIRD-PARTY-LICENCES.md`) under `packages/` that
do need tracking.

**Fix applied.** `.gitignore` adjusted to negate-include `.md`/`README*`
text files while still excluding binaries (commit `47b847d`).

**General lesson.** When a plan introduces a new top-level directory,
the writing-plans pass should also reconcile the new directory with the
project's existing `.gitignore` and CODEOWNERS-style rules.

### F9. Shell working-directory drift across tool calls

**Symptom.** The PowerShell tool persists CWD between calls; the Bash
tool does not. Sequences mixing the two led to commits that fired from
the wrong directory.

**Fix applied.** Workaround was to use `git -C <repo-root>` explicitly
from Bash when CWD state was unclear.

**General lesson.** Plans don't need to address this directly, but plan
*executors* should prefer absolute paths in commands rather than relying
on `cd` state. Add this to the writing-plans skill's "commands should
work from anywhere in the repo" guidance.

### F10. Reviewer subagents hallucinate findings

**Symptom.** Task 6's haiku code-quality reviewer flagged six issues; on
inspection, half were factually wrong (claimed PEP 604 was 3.10+ when we
target 3.11+; claimed a comment was stale when the actual file had the
correct text). The rest were scope creep beyond the plan's specification.

**Fix applied.** The controller (this session) rejected the false-flag
findings with documented rationale and proceeded.

**General lesson.** The two-stage review (spec compliance + code quality)
is valuable, but reviewer output must itself be verified, especially when
the reviewer runs on a smaller model. A "review the reviewer" check by
the controller is part of the safety net. Don't auto-fix every finding;
auto-fix is appropriate only for verifiable Critical issues.

### F12. Test asserted dict against bytes (Task 9 plan bug)

**Symptom.** The Task 9 test asserted `body["scope"] == "global"` against
`kwargs["value"]`, but the implementation passed `json.dumps(body).encode("utf-8")`
to the producer. Bytes aren't subscriptable with string keys; the test
would have failed at runtime with `TypeError: byte indices must be integers`.

**Fix applied.** Plan updated: test decodes the bytes via
`json.loads(raw.decode("utf-8"))` before asserting fields. Implementation
unchanged.

**General lesson.** When a test mocks a Kafka producer (or any
serialise-then-send API), the test must apply the same encoding the
producer would apply. Write a tiny `_assert_kafka_message_eq(call, expected)`
helper that decodes the payload, so the asymmetry is visible.

### F13. ISO 8601 + "Z" suffix double-counted UTC

**Symptom.** Task 9 implementation used
`datetime.now(timezone.utc).isoformat() + "Z"`, which produces
`"2026-05-15T12:34:56.789+00:00Z"` — both `+00:00` and `Z` mean UTC,
and `Z` after an offset is not a valid ISO 8601 string. Downstream JSON
parsers (Pydantic, JavaScript `Date.parse`) reject it.

**Fix applied.** Plan now defines a `_utc_now_iso_z()` helper that does
`isoformat(timespec="microseconds").replace("+00:00", "Z")`, and the test
explicitly asserts `body["at"].endswith("Z")` and `"+00:00" not in body["at"]`.

**General lesson.** When a plan mixes timezone-aware datetimes with a
manual `"Z"` suffix, it's almost always wrong. Pin the formatting once
in a shared helper or rely on Pydantic's automatic serialization, and
test the exact string shape — assertions like `"at" in body` are too
weak to catch this.

### F11. Test coverage in tight TDD plans is by-design narrow

**Symptom.** Reviewer flagged Task 6 for only testing the HEALTHY path.
The plan explicitly specified one test for Task 6 because the engine's
state-machine tests live in Task 7.

**Fix applied.** Findings rejected as scope creep. Phase 2 integration
test (Task 24) is the right home for cross-state coverage.

**General lesson.** Plans should explicitly state where breadth of test
coverage lives (per-task unit vs. per-phase integration), so reviewers
can calibrate. Add a sentence in the plan header like "Unit tests in
each task cover only the path required to wire the next task; phase-level
breadth lives in Tasks 24/25/27."

### F14. Hard-coded deque size decoupled from threshold settings

**Symptom.** Plan code initialised the sliding window with
`deque(maxlen=10)` while the default thresholds require
`max(healthy_required_samples, drift_required_samples) + 1 = 7` slots. With
default thresholds the tests passed, but the magic number `10` would
become quietly wrong if either threshold were tuned up — the engine
would either over-retain history or evict needed samples.

**Fix applied.** maxlen is now derived from the thresholds:
`max(healthy_required_samples, drift_required_samples) + 1`. Plan code
block updated; commit `3a99859` applies the refactor.

**General lesson.** Buffer sizes that exist *because of* configurable
parameters should be derived from those parameters, not magic-numbered.
Add a `_window_capacity` helper or assertion the next time a similar
pattern shows up.

### F15. State-mutating "getter" silently widens the method's contract

**Symptom.** `SyncStateEngine.current_status()` reads like a getter but
calls `_transition_to(SYNC_LOST)` when silence is detected — mutating
state and firing callbacks. Spec authors and reviewers both initially
read past this, but the side effect matters under concurrent calls (a
future `asyncio.gather()` could double-fire transitions).

**Fix applied.** Method retained, but a docstring now states explicitly
that the call may transition the engine and that concurrent callers
must serialise. No behaviour change — the silence check belongs with
the status query.

**General lesson.** Methods named like getters (`current_*`, `get_*`)
should either be side-effect free, or carry an unmistakable hint in the
name (`current_status_and_check_silence` was rejected as ugly; a
prominent docstring was the compromise). Reviewing for "does this name
match what the method actually does" is worth doing on the first pass.

### F16. Callback contract did not carry scope; Task 9 would have wired wrong

**Symptom.** Plan defined `TransitionCallback = Callable[[SyncStatus, SyncStatus], None]`
— `(old, new)`. The same callback list fires for both global transitions
and per-variant transitions, but downstream consumers cannot tell which
is which. Task 9's `publish_transition(scope="global"|"variant:name", ...)`
explicitly needs scope on every event; with the original callback shape,
the only options were a fragile heuristic (compare states against last
known) or a parallel callback registry — neither what the spec assumed.

Both the Task 8 implementer subagent and the code-quality reviewer
independently flagged this as a Task 9 blocker.

**Fix applied.** Callback signature widened to
`Callable[[str, SyncStatus, SyncStatus], None]` — scope is `"global"`
for engine-level transitions and the variant name (e.g. `"rdfs_strict"`)
for per-variant transitions. `_transition_to` fires `cb("global", ...)`
and the per-variant loop fires `cb(variant_name, ...)`. The existing
Task 7 callback test was updated to assert the 3-tuple. Plan code
blocks for Tasks 7-8 synced (commit `97ad5ac`).

**General lesson.** When designing a callback that may fire from multiple
contexts (different scopes, different subsystems, different sources),
include the source in the callback contract from day one. Adding it
later forces every existing registration site to be rewritten. The
B1.3 plan separated "global engine" and "per-variant" into different
tasks (7 and 8) but used a single callback list spanning both — a
classic example of decomposition by feature rather than by callback
identity.

### F17. Stale Moq import + missing Category attribute in Task 15 test

**Symptom.** Plan's Task 15 test (Phase 5, C# WPF integration) included a
`using Moq;` import accompanied by an inline meta-comment ("wait — we said
no Moq; use a TestHttpMessageHandler instead. Replace with a tiny fake
handler."). The import was dead, and the comment is an authoring artefact
that shouldn't have shipped. Additionally, the test used `[Test]` with no
`Category` attribute; the plan's run command filters
`--filter Category=Unit`, so the test would have been silently skipped.

**Fix applied.** Plan now imports only what the test actually uses, and
the test is annotated `[Test, Category(TestCategories.Unit)]` to match
the existing convention used across `Sg.Tests`.

**General lesson.** Plans that interleave authoring decisions with code
("wait — let's switch from X to Y") should resolve the decision before
publication. The comment looked like a TODO but was really a typo of
intent. A linter sweep for "wait", "TBD", or "remove this" inside plan
code blocks would catch this class of artefact.

### F18. Phase 5 paths point at mvp4 but the actual target is the v2 Sg.App (not yet scaffolded)

**Symptom.** Phase 5 task headers use paths like
`mvp4/Sg.App/Services/TimeSyncClient.cs`,
`mvp4/Sg.App/ViewModels/MainWindowViewModel.cs`, etc. Reading those
literally, the implementer (and a code-review agent) targets `mvp4/`,
which contains the *reference* application: a scenario editor +
STK-compute client that deliberately does not implement exercise
control, an Admin nav surface, `Microsoft.Extensions.Http`-based DI,
or `IConfiguration`-driven settings. Those features are intentionally
absent in mvp4 — they belong to the v2 product application, which is
the actual Phase 5 target.

The v2 Sg.App does not yet exist in the repo: there's no parallel C#
project for the production app the way `drs-server/` and `drs-bridge/`
were scaffolded for the Python services in Phase 0.

The plan also mislocates the main view-model — even within mvp4, it
lives in `Sg.Domain.ViewModels`, not `Sg.App.ViewModels`.

**Fix applied.** None in code. During this spec-validation run, Tasks
15 and 16 were executed against `mvp4/` because the path strings said
so; the artefacts produced (`TimeSyncStatusDto`, `TimeSyncClient`,
`BannerLevel`, `SyncBannerService` plus their NUnit tests) are
intentionally reference-grade and align with the role mvp4 plays —
"verify the API surface and JSON shape, then port to the main app".
Tasks 17-20 paused because the v2 Sg.App they actually target does not
exist yet.

**General lesson.** Every cross-stack plan needs to be explicit about
**which** application it targets when the repo contains more than one
candidate ("reference" vs. "product", "mvp" vs. "v2"). Path strings are
a weak signal — readers will land on whichever directory exists first.
The plan header should state in one sentence: "Phase 5 modifies the v2
product Sg.App (path: TBD; scaffolded by [BX.Y]). mvp4/Sg.App is the
reference codebase only and is not modified by this plan."

The remediation is a v2 Sg.App scaffolding task (mirroring the
`drs-server/` and `drs-bridge/` skeletons), with all Phase 5 paths
re-pointed at it. Until that scaffolding lands, Tasks 17-20 are
correctly blocked; Tasks 15 and 16 should either be (a) re-implemented
in the new scaffold once it exists, or (b) accepted as deliberate
reference artefacts in `mvp4/` per the original mvp4 charter.

### F19. Phase 4 Task 14 assumed a drs-bridge `Bridge` skeleton that did not exist

**Symptom.** Task 14's wiring code references `self._variant_tasks`,
`self._control_publisher`, `self._tick_lag_detectors`, `self._health_publisher`
on an unnamed enclosing class — the plan assumes drs-bridge already has
a `Bridge` lifecycle owner with task tracking, a Kafka control-plane
publisher, a Kafka health publisher, and a per-variant detector
registry. None of those existed; drs-bridge had only the package skeleton
from commit `47b847d` (`main.py` was a stub `raise SystemExit`).

This is the second instance of the F18 pattern: a phase task wires
features into infrastructure that an earlier task should have built but
the plan never scheduled.

**Fix applied.** Scaffolded the minimal foundation as part of Task 14
(commit `6904392`):
- `drs-bridge/src/drs_bridge/bridge.py` — `Bridge` class with
  `register_variant(profile, parser, sender)`, `tick_lag_detector_for(variant)`,
  and `shutdown()`. Owns `_variant_tasks: dict[str, list[asyncio.Task]]`
  and `_tick_lag_detectors: dict[str, TickLagDetector]`.
- `drs-bridge/src/drs_bridge/control_publisher.py` — `ControlPublisher`
  with `publish_variant_registration(variant, precision_required_ms)`,
  writes JSON to Kafka `drs.control`.
- `drs-bridge/src/drs_bridge/health_publisher.py` — `HealthPublisher.publish`
  writes JSON to Kafka `system.health`. Satisfies the `HealthPublisher`
  Protocol that `TickLagDetector` (Task 13) consumes.

Both publishers take a `KafkaProducerLike` Protocol-typed producer so
tests use `AsyncMock` and production wires `aiokafka.AIOKafkaProducer`.
Five new tests cover registration semantics + shutdown cancellation;
real Kafka I/O and the per-tick ResponseRouter (which `tick_lag_detector_for`
hands off to) remain TODO.

**General lesson.** Same as F18: every cross-phase wiring task must
list the skeleton it expects, and that skeleton must be a separate
earlier task (or a documented pre-flight). Detect by grep'ing for
`self._<attr>` references inside plan code blocks where no class
declaration sits in the same plan.

### F20. Phase 6 used deprecated Angular test patterns

**Symptom.** Task 21's plan code uses `HttpClientTestingModule` from
`@angular/common/http/testing`. That module is deprecated as of Angular 15
(2022); the recommended pattern is `provideHttpClient()` +
`provideHttpClientTesting()` standalone helpers. Angular 18 still
compiles it, but the deprecation warnings clutter test runs and the
deprecated module is slated for removal.

**Fix applied.** Task 21's spec rewritten in the dispatch prompt to use
the modern provider helpers. Tasks 22 and 23 don't go through that path
(they spy on `TimeSyncService` directly with `jasmine.createSpyObj`),
so no further changes needed.

**General lesson.** When a plan targets a fast-moving framework
(Angular, React, Spring Boot), pin the framework version in the plan
header *and* audit each code block against the deprecation list for
that version before publishing. Angular's quarterly major-version
cadence means a plan written 18 months ago needs at least one re-read.

### F21. Phase 6 Task 26 assumed a DashboardComponent that did not exist

**Symptom.** Task 26 instructed the implementer to add
`<app-time-sync-card>` to `dashboard.component.html` — implying a
`DashboardComponent` already existed. After `ng new` the scaffold has
only `AppComponent`; nothing under `dashboard/`. The Task 22 card
component lives inside `dashboard/time-sync-card/` but the parent
dashboard page is not created by the plan or the scaffold.

Same shape as F18 (Phase 5 / sg-app) and F19 (Phase 4 / drs-bridge):
a cross-phase wiring task references a host that an earlier task should
have built but the plan never scheduled.

**Fix applied.** Task 26 scope expanded inline to also create
`DashboardComponent` (standalone, imports `TimeSyncCardComponent`),
add `/dashboard` + redirect routes in `app.routes.ts`, add
`provideHttpClient()` to `app.config.ts` (without which the polling
service's HTTP injection fails at runtime), and reduce
`app.component.html` to `<router-outlet />` so the dashboard becomes
the landing surface.

**General lesson.** This is the **third** instance of the F18 family
(F18 sg-app, F19 drs-bridge Bridge skeleton, F21 DRS webapp dashboard).
The pattern is now well-established: every cross-phase wiring task
needs an explicit "host that this task wires into already exists"
pre-flight check. The [plan pre-flight checklist](../plan-preflight-checklist.md)
Pass 2 covers this; future plan authors should run it.

## Status after this run

Phases exercised end-to-end on `feat/b1-3-time-sync`:

- **Phase 1** — install + smoke scripts authored and committed; the
  lab-side runs (`sg-ntp-install.ps1`, `ws2-ntp-install.ps1`,
  `ntp-smoke.ps1`) remain manual (hardware-dependent, F5).
- **Phase 2** — `NtpMonitor` + `/time/status` complete. drs-server tests
  green (3/3 in this phase, 13/13 cumulative).
- **Phase 3** — `SyncStateEngine` (with the F14 maxlen + F15 docstring
  refactors), per-variant tracking, scoped callbacks (F16), and the
  Kafka `system.timesync` publisher (F12 + F13 caught pre-execution).
  13/13 drs-server tests cumulative.
- **Phase 4** — drs-bridge YAML profile schema (Task 10), RDFS variant
  YAML (Task 11), `TimeBeaconCoroutine` (Task 12), `TickLagDetector`
  (Task 13), and `Bridge` lifecycle wiring + control/health publishers
  (Task 14, plus the F19 scaffold). 16/16 drs-bridge unit tests green.
- **Phase 5** — Tasks 15 and 16 landed in `mvp4/` as deliberate
  reference artefacts (84/84 in mvp4). The v2 `sg-app/` scaffold was
  added (IHost + IConfiguration + IHttpClientFactory + Serilog +
  `IExerciseStateService` + banner host + converters). Tasks 17–20
  re-ran against `sg-app/`: gating, polling-loop kickoff from
  composition root, SYNC_LOST auto-pause with `PauseReason`, and the
  Admin → Time Sync view scaffold. 17/17 sg-app unit tests green.
- **Phase 6** — DRS webapp Angular scaffold (`ng new` via npx with
  Angular 18.2, standalone components, SCSS, strict TS). Tasks 21 (
  `TimeSyncService` HTTP client + 5-second poll observable), 22
  (`TimeSyncCardComponent` dashboard card with status-coloured border
  and offset/jitter/peer/last-sync), 23 (`TimeSyncRowComponent` per-
  variant row with `effectiveStatus` HEALTHY/ALERT/WARMING getter),
  and 26 (`DashboardComponent` at `/dashboard` with the card wired in
  + `provideHttpClient()` in `app.config.ts` + `<router-outlet />` in
  `app.component.html`). 24/24 Karma + Jasmine specs pass under
  ChromeHeadless. Surfaced F20 (Angular test deprecations) + F21
  (third instance of the F18 host-scaffold gap).
- **drs-bridge runtime skeleton** — the separate
  [2026-05-20 plan](../plans/drs-bridge-runtime-skeleton.md)
  added BridgeSettings env-driven config, profile_loader,
  ctypes-based parser_loader, asyncio TCP/UDP transport, factory-
  injected Runtime orchestrator, and cross-platform main with signal
  handlers. 30/30 drs-bridge tests pass. drs-bridge is now an
  actually-runnable service.
- **drs-server runtime skeleton** — the separate
  [2026-05-20 plan](../plans/drs-server-runtime-skeleton.md)
  added ServerSettings, supervised NTP poll loop, FastAPI lifespan with
  app.state wiring + sync→async bridge for engine transitions → Kafka
  publisher, modernised main.py + `python -m drs_server` CLI entry, and
  an integration test that boots the whole app through the lifespan
  with surgical mocks (closes the F16 integration-gap concern).
  25/25 drs-server tests pass. drs-server is now actually-runnable;
  `/time/status` would return real engine state in production rather
  than 500-ing. **Surfaced no new corrigenda findings** — the
  pre-flight checklist caught everything before dispatch.

## Open items not yet validated

- ~~**Phase 4** — drs-bridge YAML schema validation; `TimeBeacon` coroutine
  cancellation semantics; `TickLagDetector` interaction with aiokafka.~~
  **Done in this run.** Tasks 10-14 landed (`f63c69b` → `6904392`).
  Surfaced F19. Open follow-ups: real Kafka producer wiring in production,
  the per-tick ResponseRouter that consumes `tick_lag_detector_for(variant)`,
  parser-DLL loader, and TCP/UDP sender — all outside B1.3 scope.
- ~~**Phase 6** — DRS webapp (Angular). The webapp project itself is
  not scaffolded; the entire toolchain step (Node + Angular CLI +
  `ng new`) is implicit in the plan.~~ **Done in this run.** Tasks 21-23
  + 26 landed against an Angular 18 scaffold; surfaced F20 + F21.
- **Phase 7** — 30-min sustained load test under 2,000 msg/s. Requires
  live Kafka + drs-bridge + lab hardware.
- **B1.3 lab acceptance** — the Meinberg MSI download (F4) and the
  Phase 1 install/smoke runs (F5) are the only blockers for declaring
  Phase 1 truly accepted on real hardware.
- **Navigation host in sg-app** — Task 20 created `TimeSyncView` but
  did not wire it into `AdminShellView` or a top-level navigation
  surface. That belongs in B1.1 wireframes / a future shell-navigation
  task.

## Recommendations for future B1.x plans

These were the 7 raw recommendations the spec-validation run produced. They have since been baked into:

- **The canonical artefact:** [`docs/ewtss/plan-preflight-checklist.md`](../plan-preflight-checklist.md) — a 4-pass checklist (environment & scope → skeleton mapping → content sanity → reviewer-handoff) anchored back to specific findings here. Run this against any new plan before saving.
- **The `superpowers:writing-plans` skill:** the cached SKILL.md was extended with self-review checks 4–10 covering the same ground. Future plan authors running that skill on this machine see the additions automatically.

The original 7 recommendations are preserved below for reference:

1. **Phase 0: scaffolding task.** Every B1.x plan should start with an
   explicit "verify or create the directories and toolchain this plan
   touches" task. Don't assume the layout exists.
2. **Path-convention pin.** State up front whether the plan uses
   src-layout (`pkg/src/pkg/...`) or flat layout. Match the project's
   existing pattern. Once pinned, every code block uses it consistently.
3. **Mark manual steps.** Add a `[manual]` tag to any task or step that
   requires human action (binary download, lab hardware, vendor
   approval). The execution layer reports manual steps separately from
   automated steps.
4. **Test scope statement.** State which task owns unit-level coverage
   and which owns phase-level integration coverage. Reviewers calibrate
   accordingly.
5. **Pin Python version once.** Either match what's installable on the
   target lab, or add an explicit "install Python X.Y" task. Don't pin a
   version the engineer can't get.
6. **Reconcile gitignore.** When adding a new top-level directory, the
   plan should include or note the gitignore adjustment required to
   track its docs while excluding its binaries.
7. **Forward-reference linter.** Before publishing, verify that every
   symbol imported in a test exists by the time that test runs. A simple
   grep across `from X import Y` against `class Y` / `def Y` ordering
   would catch most cases.
