# EWTSS v2 — Deployment Guide

**Audience:** ops engineers, customer integrators, support staff.
**Read time:** 30 minutes (referenced from operational runbooks; not read top-to-bottom routinely).
**Purpose:** procedural reference for installing, configuring, and operating an EWTSS v2 deployment. Architecture rationale lives in the [Architecture Overview](architecture-overview.md); this guide assumes you accept the architecture and need to deploy it.

> **Status:** parts of this guide describe components that are **not yet built** (the telemetry pipeline, Mode B server). Sections covering those are explicitly marked with **[Telemetry phase]** or **[Mode B]**. The Mode A desktop deliverable (`Sg.App.exe`) is shippable today.

---

## 1. Deployment topology

An EWTSS v2 deployment within development scope consists of **two workstations** (WS1 + WS2). The Control Center workstation and per-variant Entity Controller Applications also sit on the LAN but are client-owned (out of v2 development scope; v2 exposes integration APIs from SG and/or DRS for them to consume — see [ADR-018](decision-record.md) and [Architecture Overview §3.10](architecture-overview.md#310-external-integrations-out-of-v2-development-scope-)). A WS1 is installed for **either SG-side Mode A or SG-side Mode B** at install time; the two SG modes do not coexist within a single deployment, and switching SG modes is a reinstall operation rather than a runtime toggle. WS2 (drs-bridge + drs-server + DRS webapp + Kafka + TimescaleDB) is identical across SG-mode choices.

| Workstation | Role | Stack |
|---|---|---|
| **WS1 — SG (Scenario Generator)** | Authors scenarios, runs STK link-analysis compute, writes results to TimescaleDB on WS2 | Windows 11. STK 12 in-process. **Mode A:** `Sg.App.exe` (WPF). **Mode B:** `Sg.Server.exe` (ASP.NET Core) + browser served from `Sg.Web` static assets. |
| **WS2 — DRS (Device Replacement Software)** | Hardware ingest, telemetry consumer, scenario-output store, DRS Engineer frontend host. Identical across SG modes. | Windows 11 (matches WS1 OS for deployment consistency; v1 legacy ran on Windows). `drs_bridge` (Python + C++ parsers) + `drs_server` (Python FastAPI — also serves the DRS webapp static assets and webapp REST/WS endpoints to a local browser on WS2 for the DRS Engineer) + Kafka 3.x KRaft single-broker + PostgreSQL 16 + TimescaleDB 2.x. No STK, no GPU. |

```
┌── WS1 — Scenario Generator ─────────────────────────┐
│                                                      │
│  Mode A:  Sg.App.exe  (WPF + STK in-process)   │
│           Operator authors directly via WPF UI       │
│                                                      │
│  ── OR ──                                            │
│                                                      │
│  Mode B:  Sg.Server.exe (ASP.NET Core + STK    │
│           in-process) + serves Sg.Web SPA;     │
│           Operator opens browser to localhost        │
│                                                      │
│  STK 12 Engine + GPU                                 │
│                                                      │
│  Writes link-analysis output ─────┐                  │
└───────────────────────────────────┼──────────────────┘
                                    │ LAN: PostgreSQL 5432
                                    │      drs_server :8000
                                    │
┌───────────────────────────────────▼──────────────────┐
│  WS2 — DRS                                           │
│                                                      │
│  drs_bridge (Python + C++) ── hardware ingest        │
│  drs_server (Python FastAPI) ── Kafka consumer +     │
│      REST/WebSocket API                              │
│  Kafka 3.x KRaft (single-broker)                     │
│  PostgreSQL 16 + TimescaleDB 2.x                     │
│      ↑ stores SG link-analysis output                │
│      ↑ stores telemetry hypertables (drs_server)     │
│                                                      │
└──────────────────┬───────────────────────────────────┘
                   │ TCP/UDP per hardware spec
                   │
            ┌──────▼────────┐
            │ DRS hardware  │
            └───────────────┘
```

**Mode commitment is per-deployment, install-time.** The `MVP4_BACKEND` environment variable selects the mode at install / configuration time, not at runtime. The WS1 install is either WPF or ASP.NET Core — never both. This simplifies the licensing story: there is no concurrency check needed because no two STK-bearing processes can be running on the same machine by construction.

**Browser placement (Mode B):** the typical configuration has the operator sit at WS1 and run a local browser to `localhost`. If the customer wants to allow operator browsers on additional non-workstation laptops on the same LAN, open WS1's HTTPS/WebSocket ports to that subnet — no other infrastructure change needed.

## 2. Per-machine requirements

### 2.1 WS1 — SG (Scenario Generator), Mode A configuration

The default Mode A configuration: operator works directly in the WPF UI on WS1.

| Component | Requirement |
|---|---|
| OS | Windows 11 (Home Single Language or Pro). Windows 10 21H2+ acceptable but not the test target. |
| CPU | x64, 4 cores minimum, 8 cores recommended. |
| RAM | 16 GB minimum, 32 GB recommended for scenarios with > 50 entities. |
| GPU | **Dedicated GPU strongly recommended** (NVIDIA GTX 1650 / RTX 3050 or better, or AMD equivalent). Integrated GPU works but pan/zoom is sluggish even with the perf fixes shipped in MVP4.5. |
| Display | 1920×1080 minimum; HiDPI displays supported. |
| Disk | SSD strongly recommended for STK scenario load times. ~5 GB for STK install + ~500 MB for the EWTSS app + scenario storage. |
| Software | STK 12 (Engine + UI), .NET 8 Runtime, Visual C++ Redistributable 2015–2022, `Sg.App.exe`. |
| Network | LAN access to WS2 (PostgreSQL port 5432 outbound, drs_server REST/WebSocket port outbound). No internet at runtime. |
| User account | Local administrator for first-run install + GPU preference setup; standard user for daily operation. |
| STK seat | 1 (Engine, with `eFeatureCodeGlobeControl` feature). |

### 2.2 WS1 — SG (Scenario Generator), Mode B configuration

When the deployment is configured for Mode B, the same physical workstation runs the ASP.NET Core server instead of the WPF app. STK still lives in-process on WS1; only the user-facing surface changes.

| Component | Requirement |
|---|---|
| OS | Windows 11 (Home / Pro). |
| CPU / RAM / GPU / Disk | Same as Mode A above (STK runs in-process here too). |
| Software | STK 12 (Engine + UI), .NET 8 + ASP.NET Core 8 Runtime, `Sg.Server.exe`, `Sg.Web` static SPA assets, a modern browser (Edge / Chrome / Firefox) for the operator. |
| Network | Inbound: HTTPS (443 or as configured) + WebSocket from operator browsers (typically localhost; possibly LAN if non-workstation operator laptops are on the network). Outbound to WS2 same as Mode A. |
| User account | Local administrator for install; standard user runs the operator browser. |
| STK seat | 1 (same licence as Mode A). |

### 2.3 WS2 — DRS (Device Replacement Software)

WS2 is identical across SG-side Mode A and Mode B deployments. It hosts the entire telemetry pipeline (drs-bridge + Kafka + TimescaleDB + drs-server), the shared database, and the DRS webapp. The DRS webapp is served by drs-server to a local browser on WS2 itself; the DRS Engineer logs in to that browser for hardware health, per-variant monitor-scan, IP configuration, and message logs. Framework: **Angular preferred** per [ADR-018](decision-record.md#adr-018--ws2-drs-webapp-required-browser-frontend-on-the-drs-workstation-served-by-drs-server) + [v2 Execution Plan §3.7](v2-execution-plan.md#37-g-angular-preferred-else-react--drs-webapp-lead), with React as an acceptable fallback; final framework call signs off at end of week 1 of the v2 hardening phase.

| Component | Requirement |
|---|---|
| OS | Windows 11 (matches WS1 OS for deployment consistency and v1 legacy familiarity). |
| CPU | 8 cores minimum, 16 recommended (covers Kafka + PostgreSQL + drs_server + drs_bridge concurrent load). |
| RAM | 32 GB minimum, 64 GB for sustained 100-DRS load. |
| Disk | SSD for the database directory (TimescaleDB hypertable writes). 1 TB recommended; sized to retention policy. |
| GPU | Not required. |
| Software | Python 3.12, PostgreSQL 16 + TimescaleDB 2.x extension, Apache Kafka 3.x in KRaft mode (single broker), `drs_server`, `drs_bridge` + compiled C++ parser libraries (one per active hardware variant), MSVC 2022 build tools for parser builds (DVD ships pre-built `.dll` artefacts; build tools needed only if rebuilding on-site), Windows Service supervision via NSSM or `sc.exe`. |
| Network | LAN-facing inbound: PostgreSQL (5432, restricted to WS1 only), `drs_server` REST/WebSocket (8000 default). LAN-facing outbound (or direct cabling) to DRS hardware on TCP/UDP per protocol spec. No internet at runtime. |
| User account | `postgres` user owns DB; dedicated service users for `drs_server` and `drs_bridge`. |
| STK seat | 0. |

## 3. Licensing

> **Development licences are confirmed available** via the vendor team (3+ STK development seats, separate from the deliverable). The licensing risk this section documents is specifically the **client deployment workstation** — the perpetual STK Runtime Engine licences that ship as part of the Milestone 2 deliverable per RFQ A2 Item 5.

### 3.1 STK licence tiers relevant to EWTSS

| Licence | Held by | What it permits |
|---|---|---|
| **STK Professional / development licences** | Vendor / development team — **NOT part of deliverable** | Full programmatic COM API, scenario creation, all analysis, `agi.stk12` Python automation, CZML export. Covers all development and testing of Mode A and Mode B. |
| **STK Runtime Engine** (perpetual, Qty 2 — deliverable per RFQ A2 Item 5) | Client deployment workstations (WS1) | Load and display pre-built `.sc` scenario files; embed STK ActiveX globe controls. **Programmatic computation capability not guaranteed** — depends on whether STK Components is bundled with the Runtime Engine licence. |
| **STK Components** | Separate purchase — **not specified in RFQ** | Full programmatic COM API on deployment machines. Required for Mode A and Mode B to perform link-analysis computation at the client site. |
| **STK Engine (headless)** | Alternative deployment SKU | Headless (no GUI), supports the full `agi.stk12` COM API for scenario creation and access computation. Different SKU from STK Desktop and from STK Components. Started via `STKEngine.StartApplication()`. Confirmed via MVP integration testing as a viable deployment target. |

**Development is fully unblocked.** All Mode A and Mode B code can be built, tested, and demonstrated using the vendor team's existing development licences. The only open question concerns the **client side** — what the customer can run after delivery.

### 3.2 The deployment-time risk

The RFQ specifies STK Runtime Engine (Qty 2, perpetual) as the deliverable. If those licences do not include STK Components:

- Client workstations can **load and display** pre-built `.sc` scenario files, with the `Sg.App` WPF UI fully functional for browsing, editing non-spatial fields, and exporting reports.
- Client workstations **cannot** programmatically create scenarios or run access / link-analysis / RLOS / coverage computation locally.
- Both Mode A and Mode B would need link-analysis computation to happen on a vendor-licensed machine, with results transferred to the customer site as `.sc` + `computed_links` data — defeating the purpose of an integrated authoring system.

**One deployment path is unaffected by this risk:** running STK only as a *display* surface on WS1 (loading a pre-built `.sc`), with computation having occurred on a vendor-licensed machine before delivery. The Qty 2 perpetual licences fully cover this workflow. EWTSS v2 does not currently target this fallback because it removes the operator's ability to author new scenarios at the customer site, but it remains a valid contingency if the licence question resolves badly.

### 3.3 Capability matrix — development vs deployment

| Capability | Dev machine (vendor licences) | Client workstation (Runtime Engine only) | Client workstation (Runtime Engine + Components) |
|---|---|---|---|
| Embed STK ActiveX globe (Mode A / Mode B) | ✓ | ✓ | ✓ |
| Load pre-built `.sc` and display | ✓ | ✓ | ✓ |
| Create STK scenario programmatically | ✓ | **⚠️ Unconfirmed** | ✓ |
| Run access / link / RLOS / coverage analysis | ✓ | **⚠️ Unconfirmed** | ✓ |
| `agi.stk12` Python COM automation | ✓ | **⚠️ Unconfirmed** | ✓ |
| C# `IAgStkObjectRoot` COM automation (Mode A / Mode B) | ✓ | **⚠️ Unconfirmed** | ✓ |
| CZML export with computed results (Mode B) | ✓ | **⚠️ Unconfirmed** | ✓ |

The `⚠️ Unconfirmed` rows resolve to ✓ if STK Components is bundled with the Runtime Engine licence; they resolve to ✗ if not. This is the single binary question driving licence-cost planning.

### 3.4 Questions to verify with Ansys / AGI before Milestone 2

These four questions need answers before the licence cost can be locked:

1. **Do the perpetual STK Runtime Engine licences (Qty 2) include STK Components for programmatic scenario creation and analysis?** This is the single most important question. A "yes" clears the path for Mode A and Mode B on client workstations with no further licence purchase.
2. **If not bundled, what is the cost of adding STK Components to the two perpetual deployment licences?** This becomes a budget line item if Components must be purchased separately.
3. **Is CZML export available via the Runtime Engine COM API alone?** Relevant to Mode B only: if a scenario is already loaded (computed on a dev machine and transferred), can the Runtime Engine export it to CZML for browser delivery? A "yes" makes a "compute on dev, deliver CZML, view in browser" workflow viable on Runtime-only client workstations.
4. **Is STK Engine (headless) a lower-cost deployment alternative to STK Components?** **Confirmed via MVP integration testing as viable.** STK Desktop Engine runs headless (no GUI), supports the full `agi.stk12` COM API, and is started via `STKEngine.StartApplication()`. Verify pricing for the two client workstation seats. Key API differences from STK Desktop that the integration handles in `StkScenarioBackend`:
   - Root acquisition: `engine.NewObjectRoot()` instead of `desktop.Root`
   - One instance per process (singleton pattern enforced — see [Developer Handbook §6](developer-handbook.md#6-implementation-invariants))
   - Process isolation via `ProcessPoolExecutor(max_workers=1)` recommended for crash isolation in any future Python-side path

### 3.5 Architecture risk by licence scenario

The picked architecture is the Hybrid (Mode A primary, Mode B deferred). The licence-risk profile per scenario:

| Licence scenario | Mode A (today) | Mode B (future) |
|---|---|---|
| **Runtime Engine only on WS1 — Components NOT bundled** | ⚠️ Authoring works for browsing / non-spatial editing but link-analysis compute cannot run on WS1. Workflow becomes "compute on dev machine, transfer `.sc` + `computed_links` to WS1." | ⚠️ Same compute restriction. CZML viewer works if the dev-side compute also produced CZML. |
| **Runtime Engine + Components confirmed on WS1** | ✓ Fully covered | ✓ Fully covered |
| **STK Engine (headless) sourced as alternative** | ✓ Fully covered. WS1 runs the WPF UI; STK Engine in-process for compute. | ✓ Fully covered. Same engine in-process inside `Sg.Server`. |

Mode A and Mode B are **licensing-equivalent** from the customer's perspective: both use the same in-process STK seat on WS1, both ask the same question of the Runtime-Engine bundle. Resolving the licence question for one resolves it for the other.

### 3.6 Recommendation

| Decision point | Recommendation |
|---|---|
| **Architecture choice today** | Hybrid (Mode A primary, Mode B deferred) — picked per [Decision Record ADR-001](decision-record.md). Licence cost is identical to the Mode B-only and Mode A-only alternatives; only one STK seat per deployment is consumed regardless. |
| **Development** | Proceed immediately — vendor licences confirmed sufficient. |
| **Deployment licence verification** | Verify Runtime Engine ± Components with Ansys / AGI **before Milestone 2 delivery.** The answer applies equally to Mode A and Mode B, so the verification only needs to happen once per deployment. |
| **If Components confirmed in Runtime Engine** | No further action — Mode A on client workstations is fully covered. |
| **If Components not bundled** | Either add Components to the Qty 2 deployment licences (budget impact to assess) **OR** source STK Engine (headless) as the alternative SKU **OR** fall back to "compute on dev, deliver `.sc`" workflow (degraded operator experience but no licence-cost impact). |
| **Concurrency check on WS1** | Not needed by construction. Mode A and Mode B never coexist within a single deployment ([ADR-012](decision-record.md)); the mode-switch path is a redeployment, not a runtime toggle. One STK process per workstation, ever. |

### 3.7 Operational notes

- `Sg.App.exe` (Mode A) and `Sg.Server.exe` (Mode B) both check for the `eFeatureCodeGlobeControl` STK feature at startup. Missing licence surfaces as an actionable error in the splash window before the main UI / API loads.
- Engine licence is supplied either as a local licence file (`.lic`) on WS1, or via AGI's licence-server protocol pointing at a customer-side licence server.
- WS2 needs zero STK seats. The DRS workstation has no STK installation, no GPU requirement, and no licensing exposure.

### 3.8 TimescaleDB

- Community Edition (Apache 2.0) is sufficient for v2's needs. Hypertables, retention policies, continuous aggregates are all in the community tier.
- TimescaleDB Cloud or self-hosted Enterprise are not required.

### 3.9 Other components

- Apache Kafka — Apache 2.0, no licence cost.
- Python, FastAPI, aiokafka, all `drs-server` / `drs-bridge` dependencies — open source.
- CesiumJS — open source (Apache 2.0).
- Angular — MIT.
- Electron (Mode B operator hosting if Electron-bundled) — MIT.

## 4. Installation procedure (WS1 — Mode A, desktop today)

On the SG (Scenario Generator) workstation when the deployment is configured for Mode A:

### 4.1 Prerequisites

1. Install **STK 12** from AGI's installer. Default location `C:\Program Files\AGI\STK 12\`. Verify the Primary Interop Assemblies are present at `C:\Program Files\AGI\STK 12\bin\Primary Interop Assemblies\`. Required PIAs: `AGI.STKObjects.Interop.dll`, `AGI.STKUtil.Interop.dll`, `AGI.STKVgt.Interop.dll`, `AGI.STKX.Interop.dll`, `AGI.STKX.Controls.Interop.dll`, `AGI.STKGraphics.Interop.dll`, `AxAGI.STKX.Interop.dll`.
2. Install **.NET 8 Desktop Runtime** (Windows x64).
3. Install **Visual C++ Redistributable 2015–2022 (x64)**.
4. Verify STK Engine licence is operational by launching STK Insight once and confirming a default scenario opens.

### 4.2 EWTSS app install

(Deployment artefact format TBD — currently developer-built; production installer is part of the v2 hardening phase.)

1. Copy the publish output of `mvp4/Sg.Mvp4.App` (Release configuration, framework-dependent) to a target directory, e.g. `C:\Program Files\EWTSS\mvp4\`.
2. Ensure the directory `C:\EWTSS\mvp4\logs\` exists and is writable by the operator user (Serilog writes daily logs there).
3. Ensure the directory `C:\EWTSS\mvp4\scenarios\` exists and is writable (default scenario save target).

### 4.3 GPU preference setup (critical)

This step is mandatory on machines with both integrated and dedicated GPUs (most laptops; many desktops):

1. Open **Settings → System → Display → Graphics**.
2. Click **Browse**, navigate to `Sg.App.exe`, select.
3. Click the entry, click **Options**, choose **High performance**, click **Save**.
4. Restart the application.

Without this step, Windows defaults the .NET WPF executable to the integrated GPU and STK's globe interaction is sluggish regardless of code quality. This is per Decision Record ADR-008's invariants and v2 archive §25.3.2.

A future installer should automate this via the `HKCU\Software\Microsoft\DirectX\UserGpuPreferences` registry key.

### 4.4 First-run verification

1. Launch `Sg.App.exe`.
2. Observe the splash window cycling through cold-start status messages (15–60 s for STK Engine init).
3. Main window should open with an empty `Untitled` scenario.
4. Pan / zoom the 3D globe — should be smooth on the dedicated GPU.
5. Click **+ Aircraft** in the tree, left-click 3 points on the globe, press Enter — aircraft should appear at the click locations with the route propagated.
6. `File > Save As .sc` — save to `C:\EWTSS\mvp4\scenarios\test\test.sc`. Reopen STK Insight, load the file, verify the aircraft is present.

If any step fails, see §7 Troubleshooting.

### 4.5 Time server install — WS1 acts as Stratum-1 (B1.3)

WS1 is the deployment's time source per RFQ A.1 §F and [B1.3 design spec](specs/time-sync-design.md). The Meinberg NTP daemon runs on WS1 as a Stratum-1 server (LOCAL reference clock, stratum 10 fudge — air-gapped LAN has no upstream). WS2 (§5.6) and any other LAN consumer sync to it.

**Prerequisites:**
- The Meinberg NTP MSI (`ntp-4.2.8p18-win-x64-setup.msi` or current stable) must already be present at `packages/installers/meinberg-ntp/`. This is a one-time vendoring step — see [`packages/installers/meinberg-ntp/README.md`](../../packages/installers/meinberg-ntp/README.md) for the download + SHA-256 + publisher-signature-verification procedure that ops runs on a workstation with internet access before air-gap delivery.
- WS1 LAN-side NIC IP / hostname must be known so WS2 can be pointed at it later.

**Procedure (run as Administrator on WS1):**

```powershell
cd <repo-root>\infrastructure\ntp
.\sg-ntp-install.ps1
```

The script:
1. Runs the MSI with `/qn /l*v` logging (output at `%TEMP%\meinberg-ntp-install.log`).
2. Copies `sg-ntp.conf` (LOCAL ref clock, stratum 10, LAN-only restricts) to `C:\Program Files\NTP\etc\ntp.conf`.
3. Creates `C:\ProgramData\EWTSS\logs\ntp-stats\` for daemon statistics.
4. Sets the NTP Windows Service to Automatic startup and starts it.
5. Waits 30 s for daemon initialisation.
6. Runs `ntpq -p` and asserts `LOCAL(0)` is the chosen peer.

A clean install ends with `SG NTP install complete.`. Anything else means the install did not converge — see [§7.4 Troubleshooting time sync](#74-troubleshooting-time-sync).

**Uninstall:** `.\ntp-uninstall.ps1` (also under `infrastructure\ntp\`).

## 5. Installation procedure (WS2 — DRS workstation) [Telemetry phase]

WS2 hosts the entire telemetry pipeline, the shared TimescaleDB, and the DRS webapp (served by drs-server to a local browser on WS2 for the DRS Engineer persona). Identical configuration regardless of whether WS1 is Mode A or Mode B. This section describes a deployment that does not yet exist; procedure will be finalised as part of the v2 hardening phase.

### 5.1 Database

1. Install PostgreSQL 16.
2. Install TimescaleDB 2.x extension (`apt install timescaledb-2-postgresql-16` or equivalent).
3. Run init scripts from `infrastructure/timescaledb/init/` to create the EWTSS schema, hypertables, and retention policies.
4. Create the `drs_user` role with write access to telemetry hypertables and read access to RBAC tables.

### 5.2 Kafka KRaft

1. Install Kafka 3.x.
2. Configure KRaft (single-broker, no ZooKeeper) per `infrastructure/kafka/server.properties` template.
3. Create topics for each active hardware variant + message kind: `hw.<variant>.<kind>`. Partition counts per the variant's expected concurrent device count (typically 4–16).
4. Configure retention: 24 hours on raw telemetry topics; long-term storage is in TimescaleDB.

### 5.3 drs-server

1. Create a Python 3.12 venv at `C:\Program Files\EWTSS\drs-server\.venv\`.
2. `pip install -r requirements.txt` (offline-capable: bundle wheels in `packages/` directory).
3. Configure `.env` with database connection, Kafka broker address, RBAC initial admin credentials.
4. Run database migrations.
5. Configure Windows Service via NSSM (`nssm install EWTSS-drs-server "C:\Program Files\EWTSS\drs-server\.venv\Scripts\python.exe" -m app.main`) or `sc.exe`. Set startup type to Automatic.
6. `systemctl enable --now drs-server`.

### 5.4 First-run verification (telemetry phase)

1. Health check `GET http://<telemetry-workstation>:8000/health` returns 200.
2. Connect to PostgreSQL, confirm hypertables exist and are empty.
3. Connect a test bridge or send synthetic Kafka messages; confirm rows appear in TimescaleDB.

### 5.5 drs-bridge (also on WS2) [Telemetry phase]

`drs-bridge` runs alongside `drs-server` on WS2 — single workstation hosts the full telemetry pipeline.

#### 5.5.1 Build C++ parser libs

1. For each hardware variant in scope at the customer site, compile the corresponding parser library from `drs\drs-bridge\bridge\parsers\<variant>\parser.cpp` to `<variant>\parser.dll`. CI commits pre-built `.dll` artefacts to `packages/parsers/` so the DVD install does not require a C++ toolchain at the customer site.
2. Place the compiled library where `drs-bridge` expects it (path declared in the variant's YAML profile).

#### 5.5.2 Configure and start drs-bridge

1. Create a Python 3.12 venv at `C:\Program Files\EWTSS\drs-bridge\.venv\`.
2. Install dependencies (`pip install -r requirements.txt`).
3. For each active hardware variant, drop the YAML profile from `drs\drs-bridge\bridge\profiles\` into `C:\Program Files\EWTSS\drs-bridge\profiles\`.
4. Configure `.env` with the local Kafka broker address (loopback or `localhost:9092` since Kafka runs on the same machine).
5. Configure Windows Service via NSSM (similar to the drs-server service above). Set startup type to Automatic.
6. `systemctl enable --now drs-bridge`.

#### 5.5.3 Verification

1. Connect a hardware device or simulator to the configured port.
2. Confirm Kafka messages appear on the corresponding `hw.<variant>.<kind>` topic.
3. Confirm rows appear in the TimescaleDB hypertable downstream.

### 5.6 Time client install — WS2 syncs to WS1 (B1.3)

Per [B1.3 design spec](specs/time-sync-design.md), WS2 runs Meinberg NTP in client mode, pointed at WS1. The drs-server `NtpMonitor` reads the local daemon state via `ntpq -c "rv 0 ..."` and feeds the three-tier `SyncStateEngine` that broadcasts on Kafka `system.timesync` and exposes `GET /time/status`.

**Prerequisites:**
- WS1 NTP install (§4.5) must already be complete and `ntpq -p` on WS1 must show `LOCAL(0)` as chosen.
- WS1 LAN IP / hostname known (passed to the script as `-SgHost`).
- Meinberg NTP MSI present at `packages/installers/meinberg-ntp/` (same one used in §4.5).

**Procedure (run as Administrator on WS2):**

```powershell
cd <repo-root>\infrastructure\ntp
.\ws2-ntp-install.ps1 -SgHost <ws1-hostname-or-ip>
```

The script:
1. Runs the MSI (`/qn /l*v`).
2. Loads `ws2-ntp.conf` template, substitutes `SG_HOST` with the value from the `-SgHost` parameter, writes the result to `C:\Program Files\NTP\etc\ntp.conf`. The config uses `iburst minpoll 4 maxpoll 6` for aggressive convergence.
3. Sets the NTP Windows Service to Automatic startup and starts it.
4. Waits 60 s for initial convergence.
5. Runs `ntpq -p` and asserts a synced peer matching `<SgHost>` (or marked with `*` for the system peer).
6. Prints `ntpq -c "rv 0 offset,jitter,stratum"` so the install log captures the initial offset.

A clean install ends with `WS2 NTP install complete. Sync status: ...`. If sync fails, the script throws and points at `C:\ProgramData\EWTSS\logs\ntp-ws2.log` for diagnosis — see [§7.4 Troubleshooting time sync](#74-troubleshooting-time-sync).

**Uninstall:** `.\ntp-uninstall.ps1`.

### 5.7 Time sync acceptance smoke test (Phase 1 gate)

Phase 1 of B1.3 is accepted when WS2's NTP offset against WS1 stays under **10 ms** across a 5-minute observation window.

**On WS2, after §5.6 completes:**

```powershell
cd <repo-root>\infrastructure\ntp
.\ntp-smoke.ps1
```

The script samples `ntpq -c "rv 0 offset,jitter,stratum"` every 5 s for 5 minutes (60 samples). Tolerance defaults to 10 ms; override via `-ToleranceMs` for tighter or looser thresholds. The script ends with one of:

- **`PASS — NTP convergence within 10 ms across 300 s observation window.`** — Phase 1 gate met.
- **`FAIL — max offset NN.NNN ms exceeds 10 ms tolerance.`** — investigate before proceeding to drs-server / drs-bridge installs. Common causes are network latency to WS1 (rare on a LAN), a misconfigured fudge on WS1, or WS2 having competing time services (Windows Time Service must be disabled — the Meinberg installer does this, but a Group Policy could re-enable it).

For the runtime observability side of this (drs-server's `/time/status` REST endpoint + `system.timesync` Kafka events + sg-app banner host), see the [B1.3 design spec §7 + §11](specs/time-sync-design.md).

## 6. Troubleshooting

### 7.1 Mode A — `Sg.App.exe`

| Symptom | Cause | Fix |
|---|---|---|
| Splash window shows "Could not initialize STK Engine" | STK 12 not installed / not registered, or licence unavailable | Reinstall STK 12; verify licence with `regsvr32 agacsrv.exe` (or repair STK install); confirm licence server reachable |
| App launches but globe is laggy on pan/zoom | Running on integrated GPU | §4.3 — set `Sg.App.exe` to High Performance in Windows Graphics Settings |
| App launches but globe shows no entities even after Save+Reload | Scenario file format issue or STK culture mismatch | Verify `C:\EWTSS\mvp4\logs\mvp4-<today>.log` for stack trace; the app uses `CultureInfo.InvariantCulture` for STK time formats — non-English Windows installs that haven't restarted post-install can occasionally desync |
| Aircraft / area target placement: clicking doesn't add waypoints | Mode wasn't entered (toolbar button click missed) | Confirm status bar shows "Left-click to add waypoints, press Enter to finish." If not, click the + Aircraft toolbar button again |
| Aircraft placement: pressing Enter does nothing | Focus is on the STK ActiveX control which doesn't receive WPF KeyDown | The fix is in MVP4.5 (Window-level PreviewKeyDown) — if this happens on a Release build, check that the deployed binary is current, not a stale Debug build |
| Drag-edit on globe: handles don't appear after double-click in tree | StartObjectEditing was called but the view didn't refresh | This was fixed in MVP4.5 with explicit `_globe3D.Refresh()` after StartObjectEditing — verify the deployed binary; if confirmed current, file a bug |
| Drag-edit: dragged a waypoint, clicked Apply, path snapped back | ApplyObjectEditing wasn't called on MouseUp | Per ADR-015 fix — should be in current MVP4.5. Verify deployed binary timestamp |
| Shutdown leaves an orphan `Agi.Stk12.exe` process | STK COM teardown stuck on an in-flight calculation | The 5-second watchdog in `App.OnExit` force-exits; check the daily log for "Graceful shutdown exceeded 5s" warning. If frequent, investigate the in-flight operation |
| App fails to write log files | `C:\EWTSS\mvp4\logs\` directory missing or not writable | Create the directory; grant write permission to the operator user |

### 7.2 Mode B [Future]

(Telemetry-phase troubleshooting will be added when the components ship.)

### 7.3 Diagnostic logging

Verbose tracing for STK COM events, mouse coordinates, mode transitions is gated behind the `MVP4_DIAG` environment variable. To enable:

```
set MVP4_DIAG=1
"C:\Program Files\EWTSS\mvp4\Sg.Mvp4.App.exe"
```

Output is appended to `Desktop\stk-debug.log`. The file is rotation-free; delete it manually between debug sessions. **Disable in production** — the verbose trace is for investigating specific issues, not routine operations.

When asking for support help, include:
- The diagnostic log (`Desktop\stk-debug.log`) for the affected session.
- The Serilog daily log (`C:\EWTSS\mvp4\logs\mvp4-<date>.log`).
- The git commit / build version of the deployed binary (visible in About dialog or by inspecting `Sg.App.exe` properties).

### 7.4 Troubleshooting time sync

| Symptom | Cause | Fix |
|---|---|---|
| `sg-ntp-install.ps1` throws "Meinberg NTP MSI not found" | Vendoring step skipped | Download the Meinberg MSI per `packages/installers/meinberg-ntp/README.md`, copy to that directory, retry. |
| `sg-ntp-install.ps1` ends with "ntpq -p output missing LOCAL reference clock" | `sg-ntp.conf` got mangled by a manual edit, or the Meinberg install left no `etc\` directory | Reinstall using `.\ntp-uninstall.ps1` then re-run `.\sg-ntp-install.ps1`. If the failure persists, inspect `%TEMP%\meinberg-ntp-install.log` for the MSI's own error trail. |
| `ws2-ntp-install.ps1` throws "no synced peer against `<SgHost>`" | WS1 daemon not yet running, WS1 firewall blocking UDP 123, or `<SgHost>` not reachable | On WS1: `Get-Service NTP` shows Running; `ntpq -p` shows `*LOCAL(0)`; firewall rule allows inbound UDP 123 from the WS2 subnet. From WS2: `Test-NetConnection -ComputerName <SgHost> -Port 123 -InformationLevel Detailed`. |
| `ntp-smoke.ps1` FAILs with max offset > 10 ms | Competing Windows Time Service, a wireless NIC contributing jitter, or thermal RTC drift on under-spec'd hardware | Verify Windows Time Service is `Disabled` (Meinberg install does this; a GPO may have re-enabled it: `Get-Service w32time`). Move the workstation onto wired LAN. If the platform's clock genuinely drifts faster than spec, replace or document a wider tolerance with a written stakeholder approval. |
| drs-server `/time/status` returns `status: "lost"` even though `ntp-smoke.ps1` passes | drs-server's `NtpMonitor` cannot run `ntpq` (wrong path, or service running as a user without execute permission on `C:\Program Files\NTP\bin\ntpq.exe`) | Confirm `NtpMonitor.__init__` was given the right path, or that the default `C:\Program Files\NTP\bin\ntpq.exe` resolves under the service account. Log line at startup will show the chosen path. |
| sg-app banner stays "drift_alert" or "sync_lost" though the lab clocks are aligned | sg-app `appsettings.json` points at the wrong drs-server URL, or the URL is reachable but returns 5xx | Hit `GET <DrsServer:Url>/time/status` manually from WS1 — if that returns clean JSON, restart `Sg.App.exe`; if it 5xx's, see the drs-server log. |

## 7. Operational runbook reference

| Operation | Procedure |
|---|---|
| Daily startup | Operator launches `Sg.App.exe`. No manual telemetry restart required if WS2 services are configured as Windows Services with Automatic startup. |
| Daily shutdown | Operator closes the WPF window; STK COM teardown completes within 5 s. Telemetry services remain running. |
| Backup | Scenario files (`.sc`, `.vdf`) are flat files in `C:\EWTSS\mvp4\scenarios\` on WS1; back up via standard file-level backup. TimescaleDB telemetry + link-analysis output backed up via `pg_dump` on WS2 per site policy. |
| Disaster recovery | Restore TimescaleDB from `pg_dump`. Scenarios from filesystem backup. STK reinstallation does not require licence migration if the licence is server-pulled. |
| New hardware variant | Add YAML profile + C++ parser source per [Architecture Overview §3.6](architecture-overview.md#36-drs-bridge--hardware-bridges); rebuild the `.dll`; restart `drs-bridge` Windows service. No `drs-server` changes. |
| New STK entity type (Mode A) | Engineering work, not deployment work — handled per the typed-entity template in the [Developer Handbook](developer-handbook.md). |
| Upgrade STK version | Test compatibility against the documented STK 12 gotchas in v2 archive §25.3.3 + §25.3.5 first. STK ActiveX behaviour can shift between STK versions; expect a debug cycle. |

## 8. Air-gapped deployment

EWTSS is designed for offline / air-gapped sites. Specific accommodations:

- **No internet at runtime.** Standard deployment has no outbound internet from any workstation.
- **Pip wheels bundled.** `pip install --no-index --find-links=packages/` for `drs-server` and `drs-bridge` works against a local wheels directory.
- **Meinberg NTP MSI bundled.** The B1.3 time-sync layer needs `packages/installers/meinberg-ntp/ntp-4.2.8p18-win-x64-setup.msi` (or current stable). Downloaded once on an internet-connected workstation, signature-verified, then placed in the air-gap mirror per [`packages/installers/meinberg-ntp/README.md`](../../packages/installers/meinberg-ntp/README.md) before lab delivery.
- **NPM packages bundled** [Mode B] — when Mode B's Angular SPA ships, the build artefact is static assets; no `npm install` at the customer site.
- **Cesium tiles + terrain offline** [Mode B] — Martin tile server preloaded with the customer's geographic area of interest, ~6 weeks one-time GIS-specialist work per the v2 archive §8.7.
- **Documentation included** in the install bundle (this `docs/ewtss/` directory, no external links required).

## 9. Reference: file system locations

| Workstation | Path | Purpose | Writable by |
|---|---|---|---|
| WS1 | `C:\Program Files\EWTSS\mvp4\` | EWTSS install root (Mode A or Mode B) | install user only |
| WS1 | `C:\Program Files\AGI\STK 12\` | STK install | install user only |
| WS1 | `C:\EWTSS\mvp4\logs\` | Serilog daily logs | operator user |
| WS1 | `C:\EWTSS\mvp4\scenarios\` | Default scenario save location | operator user |
| WS1 | `Desktop\stk-debug.log` | MVP4_DIAG-gated verbose trace (when env var set) | operator user |
| WS2 | `C:\Program Files\EWTSS\drs-server\` | drs-server install root [Telemetry phase] | install user |
| WS2 | `C:\Program Files\EWTSS\drs-bridge\` | drs-bridge install root [Telemetry phase] | install user |
| WS2 | `C:\Program Files\EWTSS\drs-webapp\` | DRS webapp static assets (built bundle served by drs-server) [Telemetry phase] | install user |
| WS2 | `C:\Program Files\PostgreSQL\16\data\` | TimescaleDB data directory (default PostgreSQL Windows install location) | NT AUTHORITY\NetworkService |
| WS2 | `C:\Program Files\Kafka\` | Kafka KRaft install root | install user |
| WS2 | `C:\ProgramData\EWTSS\logs\` | drs-server, drs-bridge log output [Telemetry phase] | service users |

## 10. References

- [Decision Record](decision-record.md) — architectural commitments and the one-engine licence rule (ADR-012).
- [Architecture Overview](architecture-overview.md) — what runs where and why.
- [Developer Handbook](developer-handbook.md) — how to extend the system (new entity types, new hardware variants, debugging conventions).
- [Hybrid design spec](specs/hybrid-frontend-design.md) — Mode A and Mode B's deployment matrix.
- [v2 tech-stack archive](specs/v2-tech-stack-archive.md) §9 (Deployment), §18 (Licensing) — fuller context.
- MVP4 README at `mvp4/README.md` — operational notes and known issues for the desktop deliverable.
