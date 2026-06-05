# CLAUDE.md

Operating guide for AI coding agents (and new contributors) working in this repo.
For setup, build, test, and run commands, **see [README.md](README.md) — do not duplicate it here.** This file captures the conventions and gotchas that aren't obvious from the code.

## What this repo is

EWTSS v2 (Electronic Warfare Test & Support System, 2nd gen): architecture/design
docs, spec-validation artefacts, and the preserved MVP codebases that validated the
design. The v1 production system was delivered to a client with a reported
performance degradation after ~10 min of continuous use (a cumulative resource
leak); v2 is a greenfield rebuild addressing it. Root-cause analysis lives in
[docs/ewtss/legacy/ANTI_PATTERNS_AND_PERFORMANCE.md](docs/ewtss/legacy/ANTI_PATTERNS_AND_PERFORMANCE.md).

## Confidentiality

**This code is proprietary and confidential.** Do not share it outside the company,
paste it into external services, or use it for any external training. When in doubt,
keep it local.

## Repo map (orientation)

| Path | Stack | Role |
|---|---|---|
| `docs/ewtss/` | Markdown | **Canonical v2 doc set.** Start at its [README](docs/ewtss/README.md). |
| `docs/superpowers/` | Markdown | Chronological MVP1–MVP4.5 design archive. Read-only; not load-bearing. |
| `drs-server/` | Python 3.11 + FastAPI | v2 server: time-sync engine, Kafka consumer, REST/WS API. Most complete service. |
| `drs-bridge/` | Python 3.11 + C++ (ctypes) | Per-variant hardware adapters. `parsers/reference/` is the canonical C++ parser template. |
| `drs-webapp/` | Angular 18 | DRS Engineer browser SPA. |
| `sg-app/` | .NET 8 WPF | Production v2 desktop app scaffold (`Sg.*` namespaces). Where v2 product code lands. |
| `mvp4/` | .NET 8 WPF + STK 12 | Reference codebase that validates STK invariants. Not the v2 product. |
| `mvp/`, `mvp2/`, `mvp3/` | Angular + Python | Preserved CesiumJS/CZML browser MVPs. Validation artefacts, not maintained. |
| `infrastructure/` | PowerShell, Docker | NTP install scripts, local Kafka KRaft stack. |

## Terminology

- **DRS = Device Replacement Software** (per RFQ) — software that replaces real EW
  hardware in test scenarios. It is **not** "Data Recording Subsystem" or "Digital
  Receiver System"; both appear in older docs and are wrong.
- "DRS" as a workstation label (WS2) is colloquial: WS2 hosts the `drs-server` /
  `drs-bridge` services that talk to DRS instances on the LAN.
- C# namespaces are `Sg.*` (the `Sg.Mvp4.*` prefix was dropped to match v2 identity);
  `mvp4/` retains `Sg.Mvp4.*` as the reference codebase.

## Documentation conventions

This repo treats its doc set as a maintained product. Two rules are non-negotiable:

1. **Keep README indexes current in the same change.** When you add a doc to a
   directory that has a README index (e.g. [docs/ewtss/README.md](docs/ewtss/README.md)),
   or materially change a referenced doc's scope, update that README's summary row in
   the *same* commit — not as a follow-up. Cross-reference lists ("Where to find
   things") are part of the index surface too.

2. **Spec/plan location and naming for v2 (B1.x) work:**
   - Specs → `docs/ewtss/specs/<topic>-design.md`
   - Plans → `docs/ewtss/plans/<topic>-plan.md` (or `<topic>.md`)
   - Corrigenda → `docs/ewtss/plans/<topic>-corrigenda.md` (read these *first* if present)
   - **No** `YYYY-MM-DD-` date prefix and **no** `b1-x-` tracking-ID prefix in
     filenames — the path + name describe content; the date lives in the file's own
     header and git history; the tracking ID lives in
     [docs/ewtss/design-backlog.md](docs/ewtss/design-backlog.md).
   - This overrides the `superpowers:brainstorming` / `writing-plans` skill defaults
     (which write to `docs/superpowers/` with date prefixes). `docs/superpowers/`
     stays read-only as the pre-v2 archive.

## Gotchas

- **STK 12.9 — one `AgSTKXApplication` per process.** Constructing a second STK
  application in a process (even after disposing the first) native-crashes the host
  with no managed exception. The `mvp4` test suite uses a single shared backend via
  `[SetUpFixture]`; per-test isolation is `CloseScenario()` + `NewScenario()`. Preserve
  this invariant when touching `StkScenarioBackend`.
- **`directory_iterator::directory_iterator: ... ""` from STK** means an empty
  `HKCU:\Software\AGI\STK\...` path, **not** a code bug. Do not modify test code to
  "fix" it — verify the desktop app (`dotnet run --project mvp4/Sg.Mvp4.App`)
  reproduces, then repair per-user STK state (`AgNewUserSetup.exe` + `STKXNewUser.exe`,
  no admin needed) or run installer Repair.
- The README's old `agacsrv.exe` reference is stale; the STK COM host is
  `AgSTKEngineHost.exe`.

## When making changes

- Prefer architecture-level reasoning before code: this is mission-critical software
  with a known resource-leak history. When fixing or reviewing, watch for cumulative
  consumption — connection-pool exhaustion, thread accumulation, unbounded consumer
  loops, growing in-memory structures.
- CI is **paused** (org-level hosted runners disabled); the workflow is parked at
  `.github/disabled/ci.yml`. Run the per-service test commands from the README locally.
