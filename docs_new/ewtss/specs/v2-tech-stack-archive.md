# EWTSS v2 — Tech Stack & Architecture Design

**Date:** 2026-04-07 (initial); §25 added 2026-05-01; §25.5 / §25.6 added 2026-05-03.
**Scope:** Greenfield rebuild of EWTSS as a new codebase. Existing codebase is reference only.
**Decision context:** Internal team decision-making — evaluating tech stack for an upgraded system supporting more DRS hardware variants and higher concurrent device counts.

> **⚠ READ THIS FIRST — 2026-05-03 update:** This document grew over MVP1–4.5 into a ~5,000-line exhaustive analysis. It now serves primarily as the **rationale archive and implementation-detail reference**. The canonical source of architectural commitments is the [**EWTSS Decision Record**](../../ewtss/decision-record.md) — start there for the current chosen architecture (Hybrid: C# WPF primary, Cesium browser deferred) and one-line rationale for each decision. Use this v2 doc when you need fuller context on rejected alternatives (§14, §17, §22), MVP1–4.5 findings (§25), or implementation-level details (§5–§10). Audience-targeted summaries (Executive Brief, Architecture Overview, Deployment Guide, Developer Handbook) are planned next.

---

## Table of Contents

- [1. Requirements Summary](#1-requirements-summary)
- [2. Recommended Architecture — Option E: Three-Service SOA + Electron + Angular + CesiumJS + CZML](#2-recommended-architecture--option-e-three-service-soa--electron--angular--cesiumjs--czml)
  - [2.1 Why Option E supersedes Option B](#21-why-option-e-supersedes-option-b)
- [3. Overall Architecture](#3-overall-architecture)
  - [2.2 Service Responsibilities](#22-service-responsibilities)
  - [2.3 Key Architectural Decisions](#23-key-architectural-decisions)
- [4. Data Model & Schema](#4-data-model--schema)
  - [4.1 Scenario Planning Schema (sg-service owns writes)](#41-scenario-planning-schema-sg-service-owns-writes)
  - [4.2 Time-Series Measurement Schema (drs-server owns writes)](#42-time-series-measurement-schema-drs-server-owns-writes)
  - [4.3 System Schema (shared)](#43-system-schema-shared)
  - [4.4 TimescaleDB Hypertables](#44-timescaledb-hypertables)
- [5. sg-service Design](#5-sg-service-design)
  - [5.1 Directory Structure](#51-directory-structure)
  - [5.2 STK Computation Pipeline](#52-stk-computation-pipeline)
  - [5.3 GIS Endpoints](#53-gis-endpoints)
  - [5.4 Key REST Endpoints](#54-key-rest-endpoints)
- [6. drs-server Design](#6-drs-server-design)
  - [6.1 Directory Structure](#61-directory-structure)
  - [6.2 Consumer Lifecycle](#62-consumer-lifecycle)
  - [6.3 WebSocket Topics](#63-websocket-topics)
  - [6.4 Report Query Endpoints](#64-report-query-endpoints)
- [7. drs-bridge Design](#7-drs-bridge-design)
  - [7.1 Directory Structure](#71-directory-structure)
  - [7.2 Hardware Profile YAML](#72-hardware-profile-yaml)
  - [7.3 C++ Parser Interface](#73-c-parser-interface)
  - [7.4 asyncio TCP Server](#74-asyncio-tcp-server)
  - [7.5 Kafka Producer](#75-kafka-producer)
  - [7.6 CMake Build](#76-cmake-build)
  - [7.7 Relationship to Existing `command.csv` / `structure.csv` Schema](#77-relationship-to-existing-commandcsv--structurecsv-schema)
  - [What the existing system does](#what-the-existing-system-does)
  - [How the v2 design replaces it](#how-the-v2-design-replaces-it)
  - [Migration path for existing variants](#migration-path-for-existing-variants)
- [8. Frontend Design (Electron + Angular + CesiumJS)](#8-frontend-design-electron--angular--cesiumjs)
  - [8.1 Directory Structure](#81-directory-structure)
  - [8.2 Technology Choices](#82-technology-choices)
  - [8.3 Angular vs React Summary (Decision Record)](#83-angular-vs-react-summary-decision-record)
  - [8.4 WebSocket Data Flow](#84-websocket-data-flow)
  - [8.5 CesiumJS Angular Integration — Build Pipeline](#85-cesiumjs-angular-integration--build-pipeline)
  - [8.6 CesiumJS Angular Integration — NgZone Isolation](#86-cesiumjs-angular-integration--ngzone-isolation)
  - [8.7 Offline Terrain and Imagery — Martin Tile Server](#87-offline-terrain-and-imagery--martin-tile-server)
  - [Martin tile server](#martin-tile-server)
  - [CesiumJS configuration](#cesiumjs-configuration)
  - [Tile data preparation pipeline (GIS specialist — one-time, ~4–6 weeks)](#tile-data-preparation-pipeline-gis-specialist--one-time-46-weeks)
  - [Installer sizing impact](#installer-sizing-impact)
  - [8.8 CesiumJS Performance Strategy and 2D/3D Switching](#88-cesiumjs-performance-strategy-and-2d3d-switching)
  - [Entity rendering APIs](#entity-rendering-apis)
  - [2D/3D switching policy](#2d3d-switching-policy)
- [9. Deployment](#9-deployment)
  - [9.1 Physical Deployment](#91-physical-deployment)
  - [9.2 Directory Layout](#92-directory-layout)
  - [9.3 Shared .env Configuration](#93-shared-env-configuration)
  - [9.4 Startup Sequence](#94-startup-sequence)
  - [9.5 DVD Delivery Layout](#95-dvd-delivery-layout)
- [10. Tech Stack Comparison — Current vs New](#10-tech-stack-comparison--current-vs-new)
- [11. Scalability Assessment](#11-scalability-assessment)
- [12. Risks & Mitigations](#12-risks--mitigations)
- [13. Team Ownership Map](#13-team-ownership-map)
- [14. Alternative Frontend — Option D: C# WPF + STK ActiveX Visualization](#14-alternative-frontend--option-d-c-wpf--stk-activex-visualization)
  - [14.1 Overview](#141-overview)
  - [14.2 Option D1 — Python Computes, C# Visualises](#142-option-d1--python-computes-c-visualises)
  - [14.3 Option D2 — C# Owns All STK Interaction](#143-option-d2--c-owns-all-stk-interaction)
  - [14.4 Frontend Layer Comparison — Option B vs D](#144-frontend-layer-comparison--option-b-vs-d)
  - [14.5 Team Ownership — Option D](#145-team-ownership--option-d)
  - [14.6 Discussion Points for Team/Customer Review](#146-discussion-points-for-teamcustomer-review)
  - [14.7 Recommendation Summary](#147-recommendation-summary)
- [15. Operating Modes — Control & Data Flow (Option D2)](#15-operating-modes--control--data-flow-option-d2)
  - [15.1 Mode Definitions](#151-mode-definitions)
  - [15.2 Standalone + Random Mode](#152-standalone--random-mode)
  - [15.3 Standalone + Scenario Mode](#153-standalone--scenario-mode)
  - [15.4 Integrated + Random Mode](#154-integrated--random-mode)
  - [15.5 Integrated + Scenario Mode](#155-integrated--scenario-mode)
  - [15.6 Mode Comparison Summary](#156-mode-comparison-summary)
  - [15.7 Kafka Topics by Mode](#157-kafka-topics-by-mode)
- [16. Operating Modes — Control & Data Flow (Option B)](#16-operating-modes--control--data-flow-option-b)
  - [16.1 Key Differences vs Option D2](#161-key-differences-vs-option-d2)
  - [16.2 Standalone + Random Mode](#162-standalone--random-mode)
  - [16.3 Standalone + Scenario Mode](#163-standalone--scenario-mode)
  - [16.4 Integrated + Random Mode](#164-integrated--random-mode)
  - [16.5 Integrated + Scenario Mode](#165-integrated--scenario-mode)
  - [16.6 Mode Comparison Summary (Option B)](#166-mode-comparison-summary-option-b)
  - [16.7 Side-by-Side Flow Comparison — Option B vs Option D2](#167-side-by-side-flow-comparison--option-b-vs-option-d2)
- [17. Option E — Python STK + CZML Bridge + Angular/CesiumJS (Electron now, Browser-ready)](#17-option-e--python-stk--czml-bridge--angularcesiumjs-electron-now-browser-ready)
  - [17.1 The CZML Bridge — Key Concept](#171-the-czml-bridge--key-concept)
  - [17.2 Architecture](#172-architecture)
  - [17.3 What Changes vs Option B](#173-what-changes-vs-option-b)
  - [17.4 What Changes vs Option D2](#174-what-changes-vs-option-d2)
  - [17.5 CZML Export — How it Works](#175-czml-export--how-it-works)
  - [17.6 Browser Portability Path](#176-browser-portability-path)
  - [17.7 Operating Modes (Option E)](#177-operating-modes-option-e)
  - [17.8 Full Options Comparison — B vs D2 vs E](#178-full-options-comparison--b-vs-d2-vs-e)
  - [17.9 Discussion Points for Team/Customer Review](#179-discussion-points-for-teamcustomer-review)
- [18. STK Licensing — Deployment Workstation Risk](#18-stk-licensing--deployment-workstation-risk)
  - [18.1 License Tiers Relevant to This Project](#181-license-tiers-relevant-to-this-project)
  - [18.2 The Deployment-Time Risk](#182-the-deployment-time-risk)
  - [18.3 Capability Matrix — Development vs Deployment](#183-capability-matrix--development-vs-deployment)
  - [18.4 Questions to Verify with Ansys/AGI](#184-questions-to-verify-with-ansysagi)
  - [18.5 Architecture Risk by License Scenario (Deployment Only)](#185-architecture-risk-by-license-scenario-deployment-only)
  - [18.6 Option B and Option E Are Licensing-Equivalent](#186-option-b-and-option-e-are-licensing-equivalent)
  - [18.7 Recommendation](#187-recommendation)
- [19. ICD Protocol Mapping — COMM DF Receiver Interface](#19-icd-protocol-mapping--comm-df-receiver-interface)
  - [19.1 Transport Layer](#191-transport-layer)
  - [19.2 Packet Framing — SDFC↔DRS](#192-packet-framing--sdfcdrs)
  - [Command packet (SDFC → DRS)](#command-packet-sdfc--drs)
  - [Response packet (DRS → SDFC)](#response-packet-drs--sdfc)
  - [19.3 Packet Framing — SCD↔DRS](#193-packet-framing--scddrs)
  - [19.4 Command Groups and IDs](#194-command-groups-and-ids)
  - [Group 0 — System Management (Group ID: 100)](#group-0--system-management-group-id-100)
  - [Group 1 — RF Measurement and Detection (Group ID: 101)](#group-1--rf-measurement-and-detection-group-id-101)
  - [Group 11 — BITE (Built-In Test Equipment) (Group ID: 111)](#group-11--bite-built-in-test-equipment-group-id-111)
  - [19.5 RF Configuration Parameters](#195-rf-configuration-parameters)
  - [19.6 FF Detection Command Sequence](#196-ff-detection-command-sequence)
  - [19.7 C++ Parser Implementation Notes](#197-c-parser-implementation-notes)
  - [19.8 Hardware Profile YAML for COMM DF Receiver](#198-hardware-profile-yaml-for-comm-df-receiver)
  - [19.9 Kafka Topic Schema](#199-kafka-topic-schema)
  - [19.10 Mapping to TimescaleDB Schema](#1910-mapping-to-timescaledb-schema)
- [20. ICD Code Generator — `tools/icd_codegen`](#20-icd-code-generator--toolsicd_codegen)
  - [20.1 Tool Structure](#201-tool-structure)
  - [20.2 CLI Usage](#202-cli-usage)
  - [20.3 Expected ICD Excel Sheet Conventions](#203-expected-icd-excel-sheet-conventions)
  - [20.4 `excel_reader.py` — What It Extracts](#204-excel_readerpy--what-it-extracts)
  - [20.5 Generated C++ Output](#205-generated-c-output)
  - [20.6 Generated TypeScript Output](#206-generated-typescript-output)
  - [20.7 Generated Hardware Profile YAML](#207-generated-hardware-profile-yaml)
  - [20.8 What the Generator Does NOT Produce](#208-what-the-generator-does-not-produce)
  - [20.9 Offline Vendoring](#209-offline-vendoring)
- [21. Low Level Design — Layered Architecture](#21-low-level-design--layered-architecture)
  - [21.1 Design Validation Summary](#211-design-validation-summary)
  - [21.2 System-Level Layered Model](#212-system-level-layered-model)
  - [21.3 drs-bridge — Internal Layers](#213-drs-bridge--internal-layers)
  - [Key module responsibilities](#key-module-responsibilities)
  - [Error handling contract per layer](#error-handling-contract-per-layer)
  - [21.4 drs-server — Internal Layers](#214-drs-server--internal-layers)
  - [Kafka consumer → WebSocket push path](#kafka-consumer--websocket-push-path)
  - [Key design rules](#key-design-rules)
  - [21.5 sg-service — Internal Layers](#215-sg-service--internal-layers)
  - [STK isolation rule](#stk-isolation-rule)
  - [Scenario response endpoint](#scenario-response-endpoint)
  - [21.6 Frontend — Internal Layers](#216-frontend--internal-layers)
  - [WebSocket subscription pattern (live-monitor)](#websocket-subscription-pattern-live-monitor)
  - [21.7 Cross-Layer Data Flows](#217-cross-layer-data-flows)
  - [Flow A — Command/Response (SDFC ↔ drs-bridge)](#flow-a--commandresponse-sdfc--drs-bridge)
  - [Flow B — Telemetry Recording + UI Push](#flow-b--telemetry-recording--ui-push)
  - [Flow C — Scenario Computation + CZML Load](#flow-c--scenario-computation--czml-load)
  - [21.8 Operating Mode Switching](#218-operating-mode-switching)
  - [21.9 Cross-Cutting Concerns](#219-cross-cutting-concerns)
  - [Structured Logging](#structured-logging)
  - [Health Endpoints](#health-endpoints)
  - [Auth (sg-service and drs-server)](#auth-sg-service-and-drs-server)
  - [Offline Vendoring Checklist](#offline-vendoring-checklist)
- [22. Visualization Platform Analysis — CesiumJS vs STK Display vs ArcGIS](#22-visualization-platform-analysis--cesiumjs-vs-stk-display-vs-arcgis)
  - [22.1 Candidate Definitions](#221-candidate-definitions)
  - [22.2 Implementation Complexity](#222-implementation-complexity)
  - [CesiumJS — Medium (≈ 2 weeks)](#cesiumjs--medium--2-weeks)
  - [STK native display — Low for scenario display, Medium for live overlays](#stk-native-display--low-for-scenario-display-medium-for-live-overlays)
  - [ArcGIS Maps SDK — High (≈ 3+ weeks)](#arcgis-maps-sdk--high--3-weeks)
  - [22.3 Air-Gapped Deployment Constraints](#223-air-gapped-deployment-constraints)
  - [CesiumJS](#cesiumjs)
  - [STK native display](#stk-native-display)
  - [ArcGIS Maps SDK](#arcgis-maps-sdk)
  - [22.4 Rendering Performance at Scale](#224-rendering-performance-at-scale)
  - [EWTSS entity count baseline](#ewtss-entity-count-baseline)
  - [CesiumJS performance tiers](#cesiumjs-performance-tiers)
  - [STK native display performance](#stk-native-display-performance)
  - [ArcGIS Maps SDK performance](#arcgis-maps-sdk-performance)
  - [22.5 2D / 3D Capability](#225-2d--3d-capability)
  - [22.6 Summary Decision Matrix](#226-summary-decision-matrix)
  - [22.7 Decision and Fallback](#227-decision-and-fallback)
  - [22.8 Terrain Decision — Resolved](#228-terrain-decision--resolved)
- [23. Team Composition and Development Staffing](#23-team-composition-and-development-staffing)
  - [23.1 Overview](#231-overview)
  - [23.2 Per-Role Module Ownership](#232-per-role-module-ownership)
  - [Python Developer 1 (Senior) — sg-service](#python-developer-1-senior--sg-service)
  - [Python Developer 2 — drs-server](#python-developer-2--drs-server)
  - [Python Developer 3 — drs-bridge Python layer, infrastructure, installer](#python-developer-3--drs-bridge-python-layer-infrastructure-installer)
  - [C++ Developer — drs-bridge parser library](#c-developer--drs-bridge-parser-library)
  - [Frontend Developer (Senior) — Angular, Electron, CesiumJS](#frontend-developer-senior--angular-electron-cesiumjs)
  - [GIS Specialist — Tile data preparation (part-time, ~6 weeks)](#gis-specialist--tile-data-preparation-part-time-6-weeks)
  - [23.3 Parallel Workstreams](#233-parallel-workstreams)
  - [23.4 Critical Path](#234-critical-path)
  - [23.5 Specialist Risks and Mitigations](#235-specialist-risks-and-mitigations)
- [24. STK Globe Visualization in a Web Browser](#24-stk-globe-visualization-in-a-web-browser)
  - [24.1 Why STK's Renderer Cannot Run in a Browser](#241-why-stks-renderer-cannot-run-in-a-browser)
  - [24.2 Available Approaches — Comparison](#242-available-approaches--comparison)
  - [24.3 Why Option E Is the Right Answer to This Question](#243-why-option-e-is-the-right-answer-to-this-question)
- [25. Post-Implementation Findings — MVP1 through MVP4](#25-post-implementation-findings--mvp1-through-mvp4)
  - [25.1 MVP roadmap recap](#251-mvp-roadmap-recap)
  - [25.2 What MVP3 confirmed about Cesium-based approach (Option E)](#252-what-mvp3-confirmed-about-cesium-based-approach-option-e)
  - [25.3 What MVP4 + MVP4.5 confirmed about STK-native approach (Option D2)](#253-what-mvp4--mvp45-confirmed-about-stk-native-approach-option-d2)
  - [25.4 Updated recommendation vs §22.7](#254-updated-recommendation-vs-227)
  - [25.5 Code complexity — Hybrid (D2 + future E) vs Option E head-to-head](#255-code-complexity--hybrid-d2--future-e-vs-option-e-head-to-head)
  - [25.6 Lessons that apply regardless of frontend choice](#256-lessons-that-apply-regardless-of-frontend-choice)

---


## 1. Requirements Summary
[↑ Table of Contents](#table-of-contents)

| Dimension | Value |
|---|---|
| DRS hardware variants | 12+ (RDFS, JV/UHF, JHF, SJRR, JLB, JMB, JHB×4, AUS, PADS) |
| Concurrent DRS instances | Up to 100 |
| Message rate per instance | ~10–20 Hz (medium rate) |
| Total message throughput | ~1,000–2,000 msg/sec |
| Concurrent UI operators | 2–5 |
| Deployment | Air-gapped LAN, Windows workstations |
| Delivery | DVD with vendored dependencies + source code |
| STK integration | Ansys STK 12 for pre-computed link analysis |
| Scenario computation | Pre-computed batch (not real-time) |
| Team | Python (×2), C++ (×1), Angular/React (×1) |

---

## 2. Recommended Architecture — Option E: Three-Service SOA + Electron + Angular + CesiumJS + CZML
[↑ Table of Contents](#table-of-contents)

> Option B was the initial selection and remains documented throughout Sections 3–13 as the baseline architecture. Option E supersedes it — the backend, schema, drs-bridge, and deployment are identical; only the STK computation output (CZML added) and CesiumJS loading differ. See Section 17 for the full Option E design and Section 18 for the licensing rationale.

### 2.1 Why Option E supersedes Option B
[↑ Table of Contents](#table-of-contents)

Five candidate architectures were evaluated:

| Option | Description | Status |
|---|---|---|
| A | Consolidated Python services + Electron | Rejected — doesn't address per-variant duplication or schema |
| B | Three-service SOA + Electron + Angular + CesiumJS | Superseded by E — identical licensing, E is strictly better |
| C | C++ TCP core + Python orchestration | Rejected — overengineered for 100-device target |
| D | Three-service SOA + C# WPF + STK ActiveX (D1/D2) | Open for discussion — see Section 14; D1 is fallback if deployment license unresolved |
| **E** | Three-service SOA + Electron + Angular + CesiumJS + CZML | **Recommended** — same license as B, adds STK-accurate visualisation + browser portability |

Option E was selected because it is strictly better than Option B at identical licensing cost:

**Inherited from Option B (unchanged):**
- Three services match the three physical workstation roles in the RFQ deployment
- C++ developer owns protocol parsers in a well-isolated shared library boundary
- Python developers own the two FastAPI services with full async SQLAlchemy
- Angular developer owns the frontend with no language context switch
- Kafka is justified — drs-bridge and drs-server may run on separate machines
- TimescaleDB solves the full-table-scan problem permanently via automatic time partitioning
- Electron over Tauri: air-gapped deployment makes bundled Chromium preferable over WebView2 runtime dependency
- Angular over React: team familiarity, reactive forms superiority for complex scenario planning UI, built-in security guardrails

**Added by Option E over Option B:**
- STK exports scenario as CZML after computation — CesiumJS loads it natively, no manual primitive reconstruction
- RLOS cones, coverage areas, and access intervals render with full STK physics accuracy in CesiumJS
- Timeline scrubbing is CesiumJS-native from CZML animation — no per-tick REST calls
- Browser portability: remove the Electron shell, Angular app runs unchanged in any browser
- Same Python `agi.stk12` license required — confirmed available on vendor development machines
- MockStkService generates synthetic CZML for development and STK-unavailable environments

---

## 3. Overall Architecture
[↑ Table of Contents](#table-of-contents)

```
┌─────────────────────────────────────────────────────────┐
│  SCENARIO GENERATOR WORKSTATION                         │
│                                                         │
│  ┌─────────────────────────────────────────────────┐   │
│  │  Electron Desktop App                           │   │
│  │  Angular + CesiumJS (3D/2D GIS)                │   │
│  │  Chromium runtime (bundled)                     │   │
│  └────────┬──────────────────────┬─────────────────┘   │
│           │ HTTP/WebSocket        │ HTTP/WebSocket       │
│           ▼                      ▼                      │
│  ┌────────────────┐   ┌──────────────────────────┐     │
│  │  sg-service    │   │  drs-server              │     │
│  │  Python/FastAPI│   │  Python/FastAPI (async)  │     │
│  │                │   │  async SQLAlchemy        │     │
│  │  Scenarios     │   │  Kafka consumers         │     │
│  │  Exercises     │   │  TimescaleDB writes      │     │
│  │  Emitter lib   │   │  REST report queries     │     │
│  │  RBAC/Users    │   │  WebSocket push          │     │
│  │  STK jobs      │   │  Health status           │     │
│  │  GIS data      │   └──────────┬───────────────┘     │
│  │  Logs          │              │                      │
│  │  DB backup     │              │                      │
│  │  STK Runtime   │              │                      │
│  └────────┬───────┘              │                      │
│           └──────────┬───────────┘                      │
│                      ▼                                   │
│           ┌──────────────────────┐                      │
│           │  TimescaleDB         │                      │
│           │  PostgreSQL 16 +     │                      │
│           │  Timescale 2.x       │                      │
│           └──────────────────────┘                      │
│                                                         │
│  ┌──────────────────────────────┐                      │
│  │  Kafka (KRaft, single node)  │◄─────────────────────┤
│  └──────────────────────────────┘                      │
└─────────────────────────────────────────────────────────┘
                        ▲
                        │ Kafka produce/consume
                        │
┌───────────────────────┴─────────────────────────────────┐
│  DRS WORKSTATION                                        │
│                                                         │
│  ┌────────────────────────────────────────────────┐    │
│  │  drs-bridge                                    │    │
│  │  Python process (config loader, supervisor)    │    │
│  │  asyncio TCP servers (one per active profile)  │    │
│  │  C++ shared libs (one per hardware type)       │    │
│  │  libsrx / libmrx / libgnss / librdfs / ...    │    │
│  └────────────────────┬───────────────────────────┘    │
│                       │ TCP connections                 │
│                       ▼                                 │
│  DRS hardware instances                                 │
│  (RDFS, JV/UHF, JHF, SJRR, JLB, JMB,                 │
│   JHB×4, AUS, PADS — up to 100 simultaneous)          │
└─────────────────────────────────────────────────────────┘
```

### 2.2 Service Responsibilities
[↑ Table of Contents](#table-of-contents)

| Service | Language | Owns |
|---|---|---|
| **sg-service** | Python 3.12 / FastAPI | Scenario CRUD, exercise planning, STK computation jobs, emitter library, RBAC, logs, DB backup/restore, GIS file serving, PDF report generation |
| **drs-server** | Python 3.12 / FastAPI async | Kafka consumers, TimescaleDB writes, report queries (paginated), WebSocket push to frontend, hardware health status |
| **drs-bridge** | Python config + C++ core | TCP server lifecycle per hardware profile, protocol framing/parsing via C++ libs, Kafka production, device health reporting |
| **Frontend** | Angular + CesiumJS (Electron) | 3D/2D scenario visualisation, exercise configuration, live DRS status, report display, user management |
| **TimescaleDB** | PostgreSQL 16 + Timescale 2.x | All persistent data — scenarios, emitters, measurements, users, logs |
| **Kafka** | Single-node KRaft | drs-bridge → drs-server message bus; one topic per hardware variant |

### 2.3 Key Architectural Decisions
[↑ Table of Contents](#table-of-contents)

- **Kafka runs KRaft mode** — no ZooKeeper; one less process to manage on an air-gapped workstation
- **STK computation runs as background jobs in sg-service** — triggered by operator, results stored to TimescaleDB; never blocks the API event loop (runs in `ProcessPoolExecutor`)
- **drs-bridge is a separate OS process** — a crash in the C++ parser layer cannot take down the API services
- **All three Python services share one TimescaleDB instance** — no cross-service HTTP calls for data
- **No cross-service HTTP calls at runtime** — services communicate only via DB and Kafka

---

## 4. Data Model & Schema
[↑ Table of Contents](#table-of-contents)

### 4.1 Scenario Planning Schema (sg-service owns writes)
[↑ Table of Contents](#table-of-contents)

```sql
exercises
  id, name, description, created_by, created_at, status
  -- status: draft | computed | ready | executing | complete

gaming_areas                         -- Area of Operation (AOP)
  id, exercise_id → exercises
  name, boundary_geojson, feba_geojson

entities                             -- Blue/Red line platforms deployed on map
  id, exercise_id → exercises
  name, type (RDFS|JVUHF|JHF|SJRR|JLB|JMB|JHB|AUS|PADS|...)
  side (blue|red), role
  position_lat, position_lon, position_alt
  is_mobile (bool)

entity_motion_profiles               -- time-dynamic movement for mobile entities
  id, entity_id → entities
  time_offset_sec, lat, lon, alt, speed_mps, heading_deg

emitters                             -- Red line emitters
  id, exercise_id → exercises
  name, type (FF|FH|Burst|RADAR|COM|SJRR|AUS|PADS)
  lat, lon, alt, is_mobile
  active_from_sec, active_to_sec

emitter_motion_profiles
  id, emitter_id → emitters
  time_offset_sec, lat, lon, alt, speed_mps, heading_deg

emitter_parameters                   -- type-specific RF parameters (key/value)
  id, emitter_id → emitters
  param_key (varchar), param_value (jsonb)
  -- e.g. key='frequency_hz', value={"min": 30e6, "max": 88e6}
  -- new emitter types require no schema change

antenna_profiles
  id, emitter_id → emitters
  pattern_type, azimuth_beamwidth_deg, elevation_beamwidth_deg
  scan_type (raster|sector|fixed), scan_rate_dps
  gain_pattern_json (jsonb)

environments
  id, exercise_id → exercises
  time_offset_sec, rainfall_mm_hr, fog_visibility_m
  temperature_c, humidity_pct, atmospheric_model

computed_links                       -- STK job output; TimescaleDB hypertable
  id, exercise_id → exercises
  computed_at (timestamptz)          -- partition key: exercise_start + tick_offset
  tick_sec (integer)                 -- seconds from exercise start (for scrubbing)
  entity_id → entities
  emitter_id → emitters
  range_m, azimuth_deg, elevation_deg
  doppler_hz, signal_strength_dbm
  path_loss_db, link_margin_db
  is_visible (bool)

emitter_library                      -- reusable emitter templates
  id, name, category, parameters_json (jsonb)
  created_by, is_system (bool)
```

### 4.2 Time-Series Measurement Schema (drs-server owns writes)
[↑ Table of Contents](#table-of-contents)

Replaces the current per-variant flat tables (FF, FH, Burst, RadarTrackReport × N variants). One generic model supports any hardware type without schema changes.

```sql
drs_sessions
  id, exercise_id → exercises (nullable)
  hardware_type (varchar: srx|mrx|gnss|jvuhf|jhf|rdfs|...)
  instance_id (varchar: "RDFS-01", "JHF-02")
  started_at, ended_at

measurements                         -- TimescaleDB hypertable on recorded_at
  id (bigint)
  session_id → drs_sessions
  recorded_at (timestamptz)          -- partition key
  hardware_type (varchar)
  message_type (varchar: FF|FH|Burst|Health|Version|NMEA|...)
  group_id (integer)
  command_id (integer)
  payload (jsonb)                    -- full parsed message

measurement_scalars                  -- hot columns for fast indexed queries
  measurement_id → measurements
  frequency_hz (double)
  power_dbm (double)
  azimuth_deg (double)
  elevation_deg (double)
  -- composite index: (session_id, recorded_at, message_type)
```

### 4.3 System Schema (shared)
[↑ Table of Contents](#table-of-contents)

```sql
users
  id, username, password_hash, first_name, last_name, role_id → roles

roles
  id, name (admin|operator|observer)

features
  id, name, description

role_features
  role_id → roles, feature_id → features

system_logs                          -- TimescaleDB hypertable on recorded_at
  id, recorded_at, level, source, message, user_id → users (nullable)

hardware_profiles                    -- replaces hardcoded constants files
  id, hardware_type (varchar)
  port (integer)
  kafka_topic (varchar)
  parser_lib (varchar)
  protocol_version (varchar)
  config_json (jsonb)
  is_active (bool)

ip_configurations
  id, hardware_type, instance_id
  ip_address, port, updated_at, updated_by → users
```

### 4.4 TimescaleDB Hypertables
[↑ Table of Contents](#table-of-contents)

| Table | Partition key | Chunk interval | Retention policy |
|---|---|---|---|
| `computed_links` | `computed_at` (timestamptz) | Per exercise | Manual purge |
| `measurements` | `recorded_at` | 1 day | Configurable |
| `system_logs` | `recorded_at` | 7 days | 90-day default |

---

## 5. sg-service Design
[↑ Table of Contents](#table-of-contents)

### 5.1 Directory Structure
[↑ Table of Contents](#table-of-contents)

```
sg-service/
  main.py
  config.py
  database.py                    ← async SQLAlchemy engine (asyncpg driver)
  models/
    scenario.py
    library.py
    system.py
  routers/
    exercises.py
    entities.py
    emitters.py
    computation.py               ← trigger STK job, poll status
    library.py
    gis.py
    users.py
    logs.py
    backup.py
    reports.py
  services/
    stk_service.py               ← IStkService interface
    stk_com_service.py           ← real STK via agi.stk12
    stk_mock_service.py          ← Friis + geometric fallback
    computation_job.py           ← orchestrates STK, writes computed_links
    gis_service.py               ← DTED, LOS, RLOS, coordinate conversion
    report_service.py            ← WeasyPrint PDF generation
    backup_service.py            ← pg_dump / pg_restore wrapper
  stk/
    scenario_builder.py
    link_analyzer.py
    propagation.py
  templates/
    report_base.html
    exercise_summary.html
    link_analysis_report.html
  requirements.txt
```

### 5.2 STK Computation Pipeline
[↑ Table of Contents](#table-of-contents)

```
POST /exercises/{id}/compute
  └── computation_job.py:run_computation(exercise_id)
        ├── Load exercise, entities, emitters from DB
        ├── Build STK scenario (scenario_builder.py)
        │     ├── Create STK scenario with exercise time window
        │     ├── Add entity objects (sensor types → STK Receivers/Transmitters)
        │     ├── Add emitters with RF parameters + antenna patterns
        │     ├── Load DTED terrain
        │     └── Set atmospheric environment
        ├── Run time-dynamic link analysis (link_analyzer.py)
        │     ├── For each tick (default 1 sec):
        │     │     ├── Update mobile entity/emitter positions
        │     │     ├── Compute access intervals (visibility)
        │     │     ├── Compute AER (azimuth, elevation, range)
        │     │     ├── Compute link budget (path loss, signal strength)
        │     │     └── Buffer rows → computed_links
        │     └── Commit every 100 ticks
        └── Update exercise status → ready
```

STK service selection at startup — **must be a module-level singleton**. STKEngine allows only one instance per Python process; calling `StartApplication()` a second time raises `"Only one STKEngine instance is allowed per Python process."` The factory caches on first call:

```python
_stk_service: IStkService | None = None

def get_stk_service() -> IStkService:
    global _stk_service
    if _stk_service is not None:
        return _stk_service
    try:
        from agi.stk12.stkengine import STKEngine
        svc = StkComService()          # STKEngine started inside __init__
        _stk_service = svc
    except Exception:
        _stk_service = MockStkService()
    return _stk_service
```

`StkComService.__init__` tries STKEngine first (headless, production path), then STKDesktop as a fallback (dev machines with STK Desktop installed):

```python
class StkComService(IStkService):
    def __init__(self) -> None:
        try:
            from agi.stk12.stkengine import STKEngine
            self._stk  = STKEngine.StartApplication()
            # STKEngine returns IAgStkEngineApplication — root via NewObjectRoot(),
            # NOT via .Root (which only exists on IAgStkDesktopApplication)
            self._root = self._stk.NewObjectRoot()
        except Exception:
            from agi.stk12.stkdesktop import STKDesktop
            try:
                self._stk = STKDesktop.AttachToApplication()
            except Exception:
                self._stk = STKDesktop.StartApplication(visible=False)
            self._root = self._stk.Root
```

### 5.3 GIS Endpoints
[↑ Table of Contents](#table-of-contents)

```
GET  /gis/tiles/{z}/{x}/{y}        ← offline raster tile server
GET  /gis/dted/{lat}/{lon}         ← elevation lookup
POST /gis/los                      ← line-of-sight between two points
POST /gis/rlos                     ← radio line-of-sight
POST /gis/coverage                 ← area coverage from observation point
GET  /gis/shapes/{layer}           ← shape files / KML as GeoJSON
POST /gis/convert/coordinates      ← lat/lon ↔ IGRS
```

### 5.4 Key REST Endpoints
[↑ Table of Contents](#table-of-contents)

```
POST   /auth/login
GET    /exercises                  ← paginated
POST   /exercises
PUT    /exercises/{id}             ← draft only
POST   /exercises/{id}/compute     ← async STK job
GET    /exercises/{id}/status      ← job polling
POST   /exercises/{id}/execute     ← begin exercise
POST   /exercises/{id}/stop
GET    /library/emitters
POST   /library/emitters
GET    /reports/{exercise_id}      ← download PDF
POST   /backup/export
POST   /backup/import
POST   /backup/purge
WS     /logs                       ← live system log stream
```

---

## 6. drs-server Design
[↑ Table of Contents](#table-of-contents)

### 6.1 Directory Structure
[↑ Table of Contents](#table-of-contents)

```
drs-server/
  main.py                        ← FastAPI lifespan (no @on_event)
  config.py
  database.py                    ← async SQLAlchemy + asyncpg
  models/
    measurements.py
    health.py
  routers/
    sessions.py
    measurements.py              ← paginated, always filtered
    health.py
    websocket.py
    config.py
  services/
    consumer_manager.py
    consumer_supervisor.py       ← restarts with exponential backoff
    message_router.py
    measurement_writer.py        ← batched writes (100 msg or 500ms)
    websocket_manager.py
    health_aggregator.py
  consumers/
    base_consumer.py
    srx_consumer.py
    mrx_consumer.py
    gnss_consumer.py
    jvuhf_consumer.py
    jhf_consumer.py
    rdfs_consumer.py
    sjrr_consumer.py
    jlb_consumer.py
    jmb_consumer.py
    jhb_consumer.py
    aus_consumer.py
    pads_consumer.py             ← new hardware type = new file, ~40 lines
  requirements.txt
```

### 6.2 Consumer Lifecycle
[↑ Table of Contents](#table-of-contents)

```python
@asynccontextmanager
async def lifespan(app: FastAPI):
    await consumer_manager.start_all()
    yield
    await consumer_manager.stop_all()
```

Each consumer runs as a supervised asyncio task:
- Restarts on failure with exponential backoff (1s → 60s cap)
- Kafka offset committed only **after** successful DB write
- Write buffer flushed every 100 messages **or** 500ms (whichever first)
- `GET /health/consumers` exposes liveness + restart count per hardware type

### 6.3 WebSocket Topics
[↑ Table of Contents](#table-of-contents)

```
WS /ws/measurements/{hardware_type}   ← live FF/FH/Burst/NMEA data
WS /ws/health                         ← all hardware health status
WS /ws/exercise/{id}/status           ← exercise execution progress
WS /ws/logs                           ← system log stream
```

### 6.4 Report Query Endpoints
[↑ Table of Contents](#table-of-contents)

All queries paginated and time-bounded — no unbounded `query.all()`:

```
GET /measurements
    ?session_id=&hardware_type=&message_type=&from=&to=&page=&size=

GET /measurements/export
    ?session_id=&from=&to=         ← StreamingResponse (chunked CSV/JSON)

GET /health/summary
GET /health/{hardware_type}/{instance_id}/history
GET /sessions
GET /sessions/{id}/stats
```

---

## 7. drs-bridge Design
[↑ Table of Contents](#table-of-contents)

### 7.1 Directory Structure
[↑ Table of Contents](#table-of-contents)

```
drs-bridge/
  main.py
  config.py
  supervisor.py
  tcp_server.py                  ← generic asyncio TCP server
  frame_dispatcher.py            ← ctypes interface to C++ parsers
  kafka_producer.py              ← AIOKafka, single shared producer
  health_reporter.py
  profiles/
    srx.yaml
    mrx.yaml
    gnss.yaml
    jvuhf.yaml
    jhf.yaml
    rdfs.yaml
    sjrr.yaml
    jlb.yaml
    jmb.yaml
    jhb.yaml
    aus.yaml
    pads.yaml                    ← new hardware type = new yaml only
  parsers/
    lib{hw}.so / lib{hw}.dll     ← compiled per hardware type
    src/
      common/
        frame_buffer.h/.cpp
        json_writer.h/.cpp       ← no external JSON dependency
      {hw}/
        {hw}_parser.h/.cpp
        {hw}_parser_test.cpp
      CMakeLists.txt
  requirements.txt
  build_parsers.bat
```

### 7.2 Hardware Profile YAML
[↑ Table of Contents](#table-of-contents)

```yaml
# profiles/rdfs.yaml
name: rdfs
port: 5485
kafka_topic: rdfs.drs.ui
kafka_broker: "${KAFKA_BROKER}"
parser_lib: parsers/librdfs.dll
max_connections: 20
health_interval_ms: 1000
receive_buffer_bytes: 65536          # size to ICD max response; FFT = 6,404 bytes
frame_terminator: "magic_bytes"      # framing via header/footer byte sequences, not newlines
protocols: [sdfc_drs, scd_drs]       # which frame formats this device uses (see Section 19.2–19.3)
protocol_version: "IRS-RDFS-v2"
```

`protocols` declares which frame format(s) the hardware uses:
- `sdfc_drs` — the main 4-byte-header command/response format (`0xAA/0xAB/0xBA/0xBB` / `0xEE/0xEF/0xFE/0xFF`)
- `scd_drs` — the compact 2-byte-header format (`0xAA 0xAA` / `0xEE 0xEE`)
- `nmea` — ASCII NMEA sentences (GNSS only)

The C++ parser reads this flag at compile-time via a CMake define so unused frame parsers are excluded from the shared lib.

Adding a new hardware variant: **new YAML file only**. No Python changes required.

### 7.3 C++ Parser Interface
[↑ Table of Contents](#table-of-contents)

Every parser exposes exactly two C functions:

```cpp
// parsers/common/parser_api.h
extern "C" {
    // Scans buf for the next complete frame.
    // Returns frame type on success:
    //   1 = SDFC→DRS command   (header 0xAA/0xAB/0xBA/0xBB, 16-byte overhead)
    //   2 = DRS→SDFC response  (header 0xEE/0xEF/0xFE/0xFF, 18-byte overhead)
    //   3 = SCD↔DRS compact    (header 0xAA/0xAA,           12-byte overhead)
    //   0 = incomplete frame (need more bytes)
    //  -1 = corrupt / unrecognised header (caller should drain and reconnect)
    // out_frame receives the complete frame bytes; out_len receives their count.
    int extract_frame(
        const uint8_t* buf, int buf_len,
        uint8_t* out_frame, int* out_len
    );

    // Decodes a complete frame into a JSON string.
    // frame_type is the value returned by extract_frame (1, 2, or 3).
    // Returns a heap-allocated JSON string; caller must call free_result().
    const char* parse_message(const uint8_t* frame, int frame_len, int frame_type);

    // Encodes a JSON response dict into a DRS→SDFC response frame (frame type 2).
    // json_response: UTF-8 JSON string produced by the response generator.
    //   Must contain "group_id" (int), "unit_id" (int), "status" (int) keys.
    // out_frame: caller-provided buffer; must be at least MAX_PAYLOAD + RESP_OVERHEAD bytes.
    // Returns number of bytes written to out_frame, or -1 on encoding error.
    int format_response(
        const char* json_response,
        uint8_t* out_frame
    );

    void free_result(const char* result);
}
```

Returning the frame type from `extract_frame` avoids a second header scan inside `parse_message` and removes the ambiguity when the SDFC→DRS command header (`0xAA...`) and the SCD compact header (`0xAA 0xAA`) share a common first byte.

The Python ctypes dispatcher passes the frame type through:

```python
class ParserHandle:
    def __init__(self, lib_path: str):
        self.lib = ctypes.CDLL(lib_path)
        self.lib.extract_frame.restype  = ctypes.c_int
        self.lib.parse_message.restype  = ctypes.c_char_p
        self.lib.parse_message.argtypes = [
            ctypes.c_char_p, ctypes.c_int, ctypes.c_int   # frame, len, frame_type
        ]

    def extract(self, buf: bytes) -> tuple[bytes, int] | None:
        """Returns (frame_bytes, frame_type) or None if incomplete."""
        out_frame = ctypes.create_string_buffer(1048576 + 64)
        out_len   = ctypes.c_int(0)
        ftype = self.lib.extract_frame(buf, len(buf), out_frame, ctypes.byref(out_len))
        if ftype <= 0:
            return None
        return bytes(out_frame[:out_len.value]), ftype

    def parse(self, frame: bytes, frame_type: int) -> dict:
        result = self.lib.parse_message(frame, len(frame), frame_type)
        data   = json.loads(result)
        self.lib.free_result(result)
        return data

    def format_response(self, response_json: str, out_buf: ctypes.Array) -> int:
        """Encodes a JSON response dict into a DRS→SDFC binary frame.
        Returns byte count written to out_buf, or -1 on error."""
        return self.lib.format_response(
            response_json.encode("utf-8"),
            out_buf
        )
```

Parser output (example FF detection response, frame_type=2):
```json
{
  "frame_type": "response",
  "group_id": 101,
  "unit_id": 70,
  "status": 0,
  "frequency_hz": 102400000,
  "power_dbm": -63.5,
  "azimuth_deg": 214.3,
  "elevation_deg": 3.1
}
```

The C++ parser has no Kafka, no Python, and no network dependency — pure byte-in / JSON-out.

### 7.4 asyncio TCP Server
[↑ Table of Contents](#table-of-contents)

drs-bridge is **bidirectional**: SDFC sends commands over TCP, drs-bridge parses them, generates or routes responses, and writes response bytes back over the same TCP connection. All traffic is also produced to Kafka for recording in drs-server.

```
SDFC  ──CMD──►  drs-bridge  ──parse──►  response_router  ──format──►  SDFC
                    │                          │
                    ▼                          ▼
                Kafka (cmd record)      Kafka (response record)
```

One generic coroutine handles all hardware types. Per-connection state is local to the coroutine — no shared mutable state between connections:

```python
async def handle_client(
    reader: asyncio.StreamReader,
    writer: asyncio.StreamWriter,
    profile: HardwareProfile,
    parser: ParserHandle,
    producer: AIOKafkaProducer,
    response_router: ResponseRouter,   # mode-aware: random / scenario / passthrough
):
    buffer = b""
    resp_buf = (ctypes.c_uint8 * (1048576 + 64))()   # reusable encode buffer

    try:
        while True:
            chunk = await asyncio.wait_for(
                reader.read(profile.receive_buffer_bytes), timeout=30.0
            )
            if not chunk:
                break
            buffer += chunk

            while result := parser.extract(buffer):
                (frame, frame_type) = result
                buffer = buffer[len(frame):]

                if frame_type != 1:          # only handle SDFC→DRS commands (type 1)
                    continue

                # 1 — decode command
                msg = parser.parse(frame, frame_type)

                # 2 — record inbound command to Kafka
                await producer.send(
                    f"{profile.kafka_topic}.cmd",
                    json.dumps(msg).encode()
                )

                # 3 — generate response (mode-aware: random / scenario / passthrough)
                response_json = await response_router.route(msg, profile)

                # 4 — encode JSON → binary DRS→SDFC response frame
                n = parser.format_response(response_json, resp_buf)
                if n < 0:
                    logger.error(f"[{profile.name}] format_response failed: {response_json}")
                    continue

                # 5 — send response bytes back to SDFC over same TCP connection
                writer.write(bytes(resp_buf[:n]))
                await writer.drain()

                # 6 — record outbound response to Kafka (for drs-server storage/display)
                await producer.send(
                    profile.kafka_topic,
                    response_json.encode()
                )

    except asyncio.TimeoutError:
        logger.warning(f"[{profile.name}] timeout: {writer.get_extra_info('peername')}")
    finally:
        writer.close()
        await writer.wait_closed()
```

`ResponseRouter` is the mode-switching component that isolates all operating-mode logic from the TCP handler (see Section 21.5 for full design):

```python
class ResponseRouter:
    async def route(self, command: dict, profile: HardwareProfile) -> str:
        session = self.session_registry.active()
        if session is None or session.mode == OperatingMode.RANDOM:
            return self.random_gen.generate(command, profile)
        elif session.mode == OperatingMode.SCENARIO:
            return await self.sg_client.get_scenario_response(
                session.exercise_id, command
            )
        else:                                  # OperatingMode.INTEGRATED
            return await self.hardware_relay.forward(command, profile)
```

### 7.5 Kafka Producer
[↑ Table of Contents](#table-of-contents)

```python
producer = AIOKafkaProducer(
    bootstrap_servers=broker,
    compression_type="lz4",
    linger_ms=5,                 ← micro-batching at high msg rates
    max_batch_size=65536
)
```

Single shared producer across all hardware profiles. `linger_ms=5` doubles throughput at 1,000–2,000 msg/sec with no perceptible latency impact on device communication.

### 7.6 CMake Build
[↑ Table of Contents](#table-of-contents)

```cmake
foreach(HW srx mrx gnss jvuhf jhf rdfs sjrr jlb jmb jhb aus pads)
    add_library(${HW} SHARED
        ${HW}/${HW}_parser.cpp
        common/frame_buffer.cpp
        common/json_writer.cpp
    )
endforeach()
```

All parser libs built in one `cmake --build` invocation. No internet required.

### 7.7 Relationship to Existing `command.csv` / `structure.csv` Schema
[↑ Table of Contents](#table-of-contents)

The existing `drs_bridge` codebase already solves the ICD-codec problem, but via a different mechanism. Understanding the mapping shows exactly what the v2 design replaces and why.

#### What the existing system does

Each hardware variant ships with two CSV files:

```
drs_bridge/srx/commands/command.csv    ← (command_id, group_id) → request/response schema
drs_bridge/srx/commands/structure.csv  ← named structures with field types, sizes, ranges
```

`command.csv` columns:
```
command_name | group_id | command_id | response_id | request_size | response_size
           | request_parameters | response_parameters
```

`request_parameters` and `response_parameters` are Python-literal-serialized lists embedded in CSV cells, loaded at startup with `ast.literal_eval`. The Python dispatch code then walks these lists to decode incoming bytes positionally.

This is the proto-ICD codec — it was presumably transcribed manually from ICD documents. It works, but it has four critical fragility points:

| Problem | Detail |
|---|---|
| **Fragile serialisation** | `ast.literal_eval` on a multi-KB CSV cell — any quoting error silently returns `None`, dropping the command |
| **No stream reassembly** | `recv(4096)` reads without accumulation; partial frames across TCP segments are lost under load |
| **Inline magic bytes** | `b"\xaa\xab\xba\xbb"` hardcoded in 4+ files; a protocol version change requires hunting every occurrence |
| **Giant if-elif dispatch** | `process_command()` in `network_utils.py` has 50+ `elif` branches on `(group_id, unit_id)` pairs; adding a command requires edits in two services |

#### How the v2 design replaces it

| Existing mechanism | v2 replacement | Benefit |
|---|---|---|
| `command.csv` + `structure.csv` (per variant) | C++ parser shared lib (per variant) | Compiled, type-safe, no runtime CSV parse |
| `ast.literal_eval` field decode | C++ struct unpacking | No silent parse failures |
| `recv(4096)` without buffer | asyncio stream reader with accumulation buffer + `extract_frame` loop | Handles fragmented TCP delivery correctly |
| Inline `b"\xaa\xab\xba\xbb"` in Python | `frame_constants.h` per parser | Single source of truth, caught at compile time |
| 50-branch `if-elif` dispatch | `switch(group_id)` + `switch(unit_id)` in C++ | O(1) dispatch; new command = new `case` block |
| Manual CSV transcription from ICD | ICD codegen utility (Section 20) | C++ skeleton generated from the ICD Excel directly |

#### Migration path for existing variants

The existing `command.csv` files contain all the field-level detail needed to write the v2 C++ parsers. For each existing hardware variant (SRX, MRX, GNSS, JSVUSHF variants), the migration is:

1. Feed the variant's ICD Excel (or manually confirmed `command.csv`) into the Section 20 codegen tool
2. Review the generated C++ skeleton — constants and dispatch structure are auto-filled
3. Implement the field-decode bodies (the parts that need human judgement about bit-packing and enumeration semantics)
4. Delete the `command.csv` and `structure.csv` files for that variant

The Python `parse_request_param.py` and `random_value_generator.py` files in each variant directory also become dead weight once the C++ parser owns the decode — they can be deleted at the same time.

---

## 8. Frontend Design (Electron + Angular + CesiumJS)
[↑ Table of Contents](#table-of-contents)

### 8.1 Directory Structure
[↑ Table of Contents](#table-of-contents)

```
frontend/
  electron/
    main.ts                      ← Electron main process
    preload.ts                   ← contextBridge, secure IPC
    package.json
  src/
    app/
      app.routes.ts
      app.config.ts
      core/
        auth/                    ← JWT interceptor, AuthGuard
        api/                     ← sg.service.ts, drs.service.ts
        websocket/               ← WebSocketService (RxJS WebSocketSubject)
      store/                     ← Angular Signals state
      pages/
        login/
        dashboard/               ← exercise list, system health overview
        exercise-planner/        ← main scenario configuration (map + panels)
        exercise-execution/      ← live exercise view + DRS status
        emitter-library/
        reports/
        user-management/
        log-management/
        db-management/
        ip-configuration/
      components/
        map/
          scenario-map/          ← CesiumJS 3D/2D viewer (core)
          entity-marker/
          emitter-marker/
          coverage-overlay/      ← RLOS / area coverage as Cesium primitives
          terrain-loader/        ← DTED/DEM streaming
          layer-manager/
        scenario/
          entity-panel/
          emitter-panel/
          motion-profile-editor/ ← waypoint/timeline editor
          environment-panel/
          computation-progress/
        drs/
          hardware-grid/         ← live health for all DRS instances
          measurement-table/     ← paginated report data
          live-chart/            ← real-time frequency/power (ngx-charts)
        shared/
  angular.json
  package.json
  electron-builder.json          ← MSI/NSIS installer config
  vendor/
    cesium/                      ← CesiumJS fully vendored (offline)
```

### 8.2 Technology Choices
[↑ Table of Contents](#table-of-contents)

| Concern | Choice | Rationale |
|---|---|---|
| 3D/2D GIS | CesiumJS | DTED/DEM terrain native, 3D globe, offline tile support |
| Desktop shell | Electron | Bundled Chromium — no WebView2 runtime dependency on air-gapped workstations |
| State management | Angular Signals | Lightweight, fine-grained reactivity, no NgRx boilerplate |
| REST calls | Angular HttpClient + RxJS operators | Caching, pagination, shareReplay for shared streams |
| Live data | RxJS WebSocketSubject | Native Angular, auto-reconnect, composable with observables |
| Forms | Angular Reactive Forms | Complex nested scenario planning forms with validation |
| Charts | ngx-charts | Angular-native, sufficient for frequency/power plots |
| PDF export | Backend (WeasyPrint) | `GET /reports/{id}` returns file download |
| Styling | Tailwind CSS | Utility-first, no runtime overhead, dense data UI friendly |

### 8.3 Angular vs React Summary (Decision Record)
[↑ Table of Contents](#table-of-contents)

Angular was selected over React for this project:
- Current EWTSS codebase is Angular; developer is proficient
- Angular Reactive Forms significantly better for complex nested scenario planning forms
- Built-in XSS sanitisation and `CanActivate` auth guards suit a defence application
- RxJS WebSocketSubject is idiomatic for live data streams
- C# background of Python developers maps well to Angular's OOP patterns
- CesiumJS is framework-agnostic — no advantage to either framework for the 3D map

### 8.4 WebSocket Data Flow
[↑ Table of Contents](#table-of-contents)

No polling anywhere. Three subscriptions replace all `setInterval` calls:

```typescript
// core/websocket/websocket.service.ts
this.measurements$ = webSocket(`ws://localhost:8001/ws/measurements/${hardwareType}`)
  .pipe(
    retry({ delay: 2000 }),          // auto-reconnect
    takeUntilDestroyed(destroyRef)
  );
```

### 8.5 CesiumJS Angular Integration — Build Pipeline
[↑ Table of Contents](#table-of-contents)

CesiumJS ships WebWorkers, WASM modules, and static assets that Angular's build system does not handle automatically. Three changes are required in `angular.json` and `main.ts`:

**`angular.json` — copy Cesium static files to dist:**
```json
"assets": [
  "src/favicon.ico",
  "src/assets",
  { "glob": "**", "input": "node_modules/cesium/Build/Cesium", "output": "cesium" }
],
"allowedCommonJsDependencies": ["cesium"]
```

**`src/main.ts` — set `CESIUM_BASE_URL` before any Cesium import:**
```typescript
// Must be the FIRST statement — before all imports
(window as any).CESIUM_BASE_URL = '/cesium';

import { bootstrapApplication } from '@angular/platform-browser';
import { appConfig } from './app/app.config';
import { AppComponent } from './app/app.component';

bootstrapApplication(AppComponent, appConfig);
```

If `CESIUM_BASE_URL` is set after the Cesium module loads, Cesium silently fails to initialise WebWorkers. The symptom is a blank canvas with no error in the console.

**`electron/main.ts` — enable WebGL and allow `blob:` worker URLs:**
```typescript
new BrowserWindow({
  webPreferences: {
    contextIsolation: true,
    nodeIntegration: false,
    webgl: true,
  }
});

// Content Security Policy — must allow blob: for Cesium WebWorkers
session.defaultSession.webRequest.onHeadersReceived((details, callback) => {
  callback({
    responseHeaders: {
      ...details.responseHeaders,
      'Content-Security-Policy': [
        "default-src 'self'; script-src 'self' 'unsafe-eval'; " +
        "worker-src blob:; img-src 'self' blob: data:; style-src 'self' 'unsafe-inline'"
      ]
    }
  });
});
```

Missing `worker-src blob:` silently kills Cesium's terrain and imagery workers — no error, just degraded rendering.

---

### 8.6 CesiumJS Angular Integration — NgZone Isolation
[↑ Table of Contents](#table-of-contents)

CesiumJS runs a continuous WebGL render loop (~60 fps) that is entirely outside Angular's change detection. If the Viewer is created inside Angular's zone, **every Cesium frame tick triggers Angular's full change detection cycle** — 60 CD runs per second on top of the WebSocket update rate. This causes severe performance degradation under load.

**Rule: everything Cesium-related runs outside NgZone. Angular→Cesium updates are explicit `runOutsideAngular` calls. Cesium→Angular callbacks use `run()`.**

```typescript
// shared/cesium/cesium-viewer.component.ts
@Component({
  selector: 'app-cesium-viewer',
  template: `<div id="cesiumContainer" style="width:100%;height:100%"></div>`,
  changeDetection: ChangeDetectionStrategy.OnPush
})
export class CesiumViewerComponent implements AfterViewInit, OnDestroy {
  private viewer!: Viewer;

  constructor(
    private ngZone: NgZone,
    private renderMode: RenderModeService
  ) {}

  ngAfterViewInit(): void {
    // ── Entire viewer init is outside Angular ─────────────────────
    this.ngZone.runOutsideAngular(() => {
      this.viewer = new Viewer('cesiumContainer', {
        animation: true,
        timeline: true,
        baseLayerPicker: false,
        geocoder: false,
        homeButton: false,
        sceneModePicker: false,     // we control 2D/3D manually
        navigationHelpButton: false,
        // Imagery and terrain configured in Section 8.7
        imageryProvider: false,     // disable default Ion/Bing request
        terrainProvider: new EllipsoidTerrainProvider(), // flat until configured
      });

      // Monitor entity count for auto-2D suggestion (Section 8.8)
      this.viewer.scene.postRender.addEventListener(() => {
        const count = this.viewer.entities.values.length;
        if (count > 500) {
          this.ngZone.run(() => this.renderMode.checkThreshold(count));
        }
      });
    });
  }

  // Called from Angular (e.g., CZML loaded notification) → update Cesium outside zone
  loadCzml(url: string): void {
    this.ngZone.runOutsideAngular(async () => {
      const ds = await CzmlDataSource.load(url);
      this.viewer.dataSources.add(ds);
      this.viewer.zoomTo(ds);
    });
  }

  // Morphs must also run outside zone — they trigger internal Cesium animations
  setSceneMode(mode: '2D' | '3D' | 'COLUMBUS'): void {
    this.ngZone.runOutsideAngular(() => {
      const duration = 1.5;
      if (mode === '2D')       this.viewer.scene.morphTo2D(duration);
      else if (mode === '3D')  this.viewer.scene.morphTo3D(duration);
      else                     this.viewer.scene.morphToColumbusView(duration);
    });
  }

  ngOnDestroy(): void {
    // Essential — leaks WebGL context without destroy()
    this.ngZone.runOutsideAngular(() => this.viewer?.destroy());
  }
}
```

`ChangeDetectionStrategy.OnPush` on the component ensures Angular's CD only runs on explicit `markForCheck()` calls, not on every parent update cycle.

---

### 8.7 Offline Terrain and Imagery — Martin Tile Server
[↑ Table of Contents](#table-of-contents)

CesiumJS on startup contacts three external services that must be suppressed for air-gapped deployment:

| Default service | What it fetches | How to suppress |
|---|---|---|
| Cesium Ion | Terrain tiles + imagery | Set `imageryProvider: false` in Viewer constructor |
| Bing Maps | Satellite imagery | Not loaded if `imageryProvider: false` |
| Cesium World Terrain | 3D terrain mesh | Replaced with local `CesiumTerrainProvider` (see below) |

**3D terrain with elevation accuracy is a confirmed requirement** — RLOS/LOS coverage cones must display correctly against real-world terrain in the CesiumJS view. The confirmed approach is a Martin tile server bundled in the installer.

#### Martin tile server

Martin is a single Rust binary (~10 MB, no runtime dependencies) that serves MBTiles files over HTTP as a standards-compliant tile server. It replaces both the DTED terrain server and the raster imagery server.

```yaml
# vendor/martin/martin.yaml
listen_addresses: '127.0.0.1:8090'
mbtiles:
  terrain:
    path: data/terrain/terrain.mbtiles
    content_type: application/vnd.quantized-mesh
    content_encoding: gzip
  imagery:
    path: data/imagery/imagery.mbtiles
```

Martin runs as a separate process from sg-service. This isolation is deliberate: STK computation jobs on sg-service are CPU-intensive and long-running; tile requests from the frontend must never queue behind them.

**Startup sequence addition** (`start_drs_services_all.bat`):
```bat
start "" "vendor\martin\martin.exe" --config vendor\martin\martin.yaml
```

#### CesiumJS configuration

```typescript
// cesium-viewer.component.ts — Viewer constructor options
const terrainProvider = await CesiumTerrainProvider.fromUrl(
  'http://localhost:8090/terrain',
  { requestVertexNormals: true }   // normals required for correct lighting on terrain
);
const imageryProvider = new UrlTemplateImageryProvider({
  url:          'http://localhost:8090/imagery/{z}/{x}/{y}',
  fileExtension: 'png',
  maximumLevel:  17,
});

this.viewer = new Viewer('cesiumContainer', {
  terrainProvider,
  imageryProvider,
  baseLayerPicker:       false,
  geocoder:              false,
  homeButton:            false,
  navigationHelpButton:  false,
  infoBox:               false,
});
```

#### Tile data preparation pipeline (GIS specialist — one-time, ~4–6 weeks)

```
DTED Level 1/2 (from DISA/NGA controlled repository)
  ↓  ctb-tile --output-format=Mesh --profile=geodetic \
               --start-zoom=0 --end-zoom=14 \
               /path/to/dted  ./terrain_tiles/
quantized-mesh directory tree  (terrain_tiles/)
  ↓  mb-util --image-format=pbf terrain_tiles/ terrain.mbtiles
terrain.mbtiles  (bundled in installer at data/terrain/)

Raster imagery source (GeoTIFF or existing OpenLayers tile cache)
  ↓  gdal2tiles / mb-util conversion
imagery.mbtiles  (bundled in installer at data/imagery/)
```

This is a one-time data preparation step performed by the GIS specialist. The resulting MBTiles files are checked into a separate asset repository (not the source repo) and included in the installer bundle.

#### Installer sizing impact

| Component | Approximate size |
|---|---|
| `terrain.mbtiles` (zoom 0–14, AoI-bounded) | 200 MB – 800 MB |
| `imagery.mbtiles` (zoom 0–17, AoI-bounded) | 100 MB – 400 MB |
| Martin binary | ~10 MB |
| Electron shell | ~200 MB |
| CesiumJS assets | ~120 MB |
| Python packages (vendored) | ~100 MB |
| **Total** | **730 MB – 1.6 GB** |

Fits on a single-layer DVD (4.7 GB) with margin. Area-of-interest bounding at tile generation time is the primary lever for controlling installer size.

---

### 8.8 CesiumJS Performance Strategy and 2D/3D Switching
[↑ Table of Contents](#table-of-contents)

#### Entity rendering APIs

CesiumJS exposes two rendering APIs with different performance profiles:

| API | Usage | 60 fps threshold | Notes |
|---|---|---|---|
| **Entity API** (`viewer.entities.add()`) | Scenario entities, emitters, coverage cones | ~300–500 entities | Easy; JS-managed; GC pressure at scale |
| **Primitive API** (`PointPrimitiveCollection`, `BillboardCollection`) | Real-time detection markers, track dots | 10,000–50,000 points | GPU-side; near-zero JS overhead per point |

**Policy for EWTSS:** Use the Entity API for CZML-loaded scenario content (STK-computed tracks, coverage cones, access interval annotations). Use `PointPrimitiveCollection` for high-frequency real-time overlays (signal detection events, bearing lines arriving via WebSocket).

```typescript
// For live detection markers — use Primitive API, not Entity API
private detectionPoints = new Cesium.PointPrimitiveCollection();

constructor() {
  this.ngZone.runOutsideAngular(() => {
    this.viewer.scene.primitives.add(this.detectionPoints);
  });
}

addDetection(lon: number, lat: number, powerDbm: number): void {
  this.ngZone.runOutsideAngular(() => {
    this.detectionPoints.add({
      position: Cartesian3.fromDegrees(lon, lat),
      color:    Color.fromCssColorString(this.powerToColour(powerDbm)),
      pixelSize: 4,
    });
  });
}
```

#### 2D/3D switching policy

| Entity count | Policy | Mechanism |
|---|---|---|
| < 300 | 3D globe (default) | No action |
| 300–500 | Suggest 2D via toast | `renderMode.checkThreshold()` emits warning |
| > 500 | Auto-switch to 2D with notification | `scene.morphTo2D(1.5)` called automatically |
| User manual | Toggle button always available | `setSceneMode()` in toolbar |

In 2D mode, WebGL draw calls drop by ~60%: no atmosphere rendering, no depth sorting, no terrain tessellation. CZML data remains loaded and fully functional in 2D — entity positions, availability intervals, and timeline playback are unaffected by the mode switch.

```typescript
// services/render-mode.service.ts
@Injectable({ providedIn: 'root' })
export class RenderModeService {
  private entityCount$ = new BehaviorSubject<number>(0);
  readonly warning$ = new Subject<string>();

  checkThreshold(count: number): void {
    this.entityCount$.next(count);
    if (count > 500 && this.currentMode === '3D') {
      this.warning$.next(
        `${count} entities detected — switching to 2D map for performance`
      );
      this.setMode('2D');
    } else if (count > 300) {
      this.warning$.next(
        `${count} entities — consider switching to 2D map for better performance`
      );
    }
  }
}
```

Columbus View (2.5D tilted map, `morphToColumbusView()`) is available as an intermediate option for users who want geographic context without the full 3D perspective overhead.

---

## 9. Deployment
[↑ Table of Contents](#table-of-contents)

### 9.1 Physical Deployment
[↑ Table of Contents](#table-of-contents)

```
SCENARIO GENERATOR WORKSTATION
  ewtss-sg.exe      (Electron — installed via MSI)
  sg-service        (Python uvicorn, port 8000)
  drs-server        (Python uvicorn, port 8001)
  kafka             (KRaft single-node, port 9092)
  timescaledb       (PostgreSQL 16, port 5432)
  STK Runtime       (COM server, AGI STK 12)

DRS WORKSTATION
  drs-bridge        (Python + C++ shared libs)

ENTITY WORKSTATIONS (LAN)
  JHF / JV/UHF / RDFS / RISC (JLB/JMB/JHB×4) / PADS / AUS
  Entity Controller Applications (unchanged, per RFQ requirement)
```

### 9.2 Directory Layout
[↑ Table of Contents](#table-of-contents)

```
C:\EWTSS\
  bin\
    start_all.bat
    stop_all.bat
    start_sg_service.bat
    start_drs_server.bat
    start_kafka.bat
    start_drs_bridge.bat          ← run on DRS workstation
  sg-service\
  drs-server\
  drs-bridge\
  kafka\                          ← KRaft mode, no ZooKeeper
  timescaledb\
  gis-data\
    tiles\
    dted\
    shapes\
  db-backups\
  logs\
  .env                            ← single shared config file
```

### 9.3 Shared .env Configuration
[↑ Table of Contents](#table-of-contents)

```ini
SG_WORKSTATION_IP=192.168.1.10
DRS_WORKSTATION_IP=192.168.1.11
KAFKA_BROKER=192.168.1.10:9092
SG_SERVICE_PORT=8000
DRS_SERVER_PORT=8001
DB_HOST=localhost
DB_PORT=5432
DB_NAME=ewtss_db
DB_USER=ewtss
DB_PASSWORD=changeme
GIS_TILES_PATH=C:\EWTSS\gis-data\tiles
DTED_PATH=C:\EWTSS\gis-data\dted
SHAPES_PATH=C:\EWTSS\gis-data\shapes
BACKUP_PATH=C:\EWTSS\db-backups
STK_ENABLED=true
STK_INSTALL_PATH=C:\Program Files\AGI\STK 12
```

### 9.4 Startup Sequence
[↑ Table of Contents](#table-of-contents)

```batch
1. net start postgresql-x64-16          (TimescaleDB)
2. kafka-server-start.bat               (Kafka KRaft)
3. uvicorn sg-service main:app :8000    (sg-service)
4. uvicorn drs-server main:app :8001    (drs-server)
5. python drs-bridge/main.py            (DRS workstation)
6. ewtss-sg.exe                         (Electron app)
```

### 9.5 DVD Delivery Layout
[↑ Table of Contents](#table-of-contents)

```
DVD:\
  INSTALL.md
  install_prerequisites.bat       ← PostgreSQL+Timescale, Kafka, VC++ runtime
  ewtss-sg-setup.exe              ← Electron MSI
  source\
    sg-service\
    drs-server\
    drs-bridge\
      parsers\src\                ← C++ source (RFQ requirement)
    frontend\
  packages\
    sg-service\packages\          ← vendored pip packages
    drs-server\packages\
    drs-bridge\packages\
  db\
    init.sql                      ← TimescaleDB schema
    seed.sql                      ← hardware profiles, roles, admin user
  gis-data\
  stk-runtime\                    ← STK Runtime Engine (Qty 2, perpetual)
```

---

## 10. Tech Stack Comparison — Current vs New
[↑ Table of Contents](#table-of-contents)

| Concern | Current EWTSS | New EWTSS v2 | Why changed |
|---|---|---|---|
| **Backend framework** | FastAPI (sync def) | FastAPI (async def) | Unblock event loop; async SQLAlchemy |
| **ORM** | SQLAlchemy sync | SQLAlchemy 2.0 async + asyncpg | Non-blocking DB calls |
| **Database** | MySQL (PyMySQL) | TimescaleDB (PostgreSQL 16) | Time-series partitioning; no full-table scans |
| **DB schema** | Flat wide tables per variant | Normalised measurement rows + jsonb | New hardware types need no schema change |
| **Message broker** | Kafka + ZooKeeper | Kafka KRaft (no ZooKeeper) | One less process to manage |
| **Kafka client** | confluent-kafka + kafka-python (both) | aiokafka (drs-bridge + drs-server) | Single async client; kafka-python removed |
| **TCP bridge** | Thread-per-client Python | asyncio + C++ shared libs | GIL-free I/O; 100+ connections on one core |
| **Hardware variants** | Duplicated Python files per variant | One config profile (YAML) per variant | New type = new YAML; no code change |
| **Protocol parsing** | Python byte manipulation | C++ shared library | Correct tool; isolated; independently testable |
| **Frontend delivery** | Browser tab (Angular) | Electron desktop app (Angular) | Desktop feel; bundled runtime for air-gap |
| **Frontend data** | setInterval polling (20 req/sec) | WebSocket push | Zero idle load; <100ms update latency |
| **Consumer lifecycle** | Daemon threads, no supervision | Supervised asyncio tasks with backoff | Silent crash detection; automatic restart |
| **Session scoping** | Session-per-thread (memory leak) | Session-per-message | No identity map growth |
| **Kafka offset commit** | Auto-commit (data loss risk) | Manual commit after DB write | At-least-once delivery guarantee |
| **Report queries** | query.all() (full table scan) | Paginated + time-bounded + indexed | Stays fast regardless of data volume |
| **STK integration** | Not present | agi.stk12 COM + MockStkService fallback | Physics-based link analysis for scenario computation |

---

## 11. Scalability Assessment
[↑ Table of Contents](#table-of-contents)

| Dimension | Current ceiling | New system ceiling |
|---|---|---|
| Concurrent DRS instances | ~10 (thread-per-client, GIL) | 100+ (asyncio, one event loop) |
| Hardware variants | 3 (SRX/MRX/GNSS + JSVUSHF) — duplicated code | Unlimited — one YAML per variant |
| Message throughput | Degrades past ~200 msg/sec (per-row commits) | 2,000+ msg/sec (batched writes, async) |
| DB query performance | Degrades with table growth (no indexes, full scans) | Constant-time (TimescaleDB partitioning + composite indexes) |
| Concurrent operators | Degrades past ~5 (threadpool saturation) | 20+ (async event loop, WebSocket push) |
| Adding new DRS type | ~8 files to copy + wire | 1 YAML + 1 C++ parser source file |

---

## 12. Risks & Mitigations
[↑ Table of Contents](#table-of-contents)

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| C++ parser build complexity on Windows | Medium | Medium | CMake + MSVC; `build_parsers.bat` wraps the build; pre-compiled libs shipped on DVD |
| TimescaleDB schema migration for existing data | Low | Low | Greenfield build; no migration needed |
| STK COM automation instability | Medium | High | MockStkService fallback; STK calls isolated in ProcessPoolExecutor |
| STK Components not bundled in Runtime Engine (client deployment) | Medium | High | See Section 18 — verify with Ansys/AGI before finalising architecture; D1 fallback available |
| AIOKafka air-gap vendoring | Low | Medium | AIOKafka is pure Python; straightforward pip package vendoring |
| WebView2 absence on older Windows 10 | N/A | N/A | Electron bundles Chromium — no WebView2 dependency |
| Electron memory footprint (~200MB) | Low | Low | Military workstations have sufficient RAM; installer size not a constraint |
| Team Rust skill gap (Tauri) | N/A | N/A | Tauri not selected; Electron requires Node.js only |

---

## 13. Team Ownership Map
[↑ Table of Contents](#table-of-contents)

| Service / Component | Owner |
|---|---|
| sg-service (FastAPI, STK, GIS) | Python developer 1 |
| drs-server (async consumers, TimescaleDB) | Python developer 2 |
| drs-bridge (C++ parsers, asyncio TCP) | C++ developer (parsers) + Python developer (config/supervisor) |
| Frontend (Angular, CesiumJS, Electron) | Angular/React developer |
| DB schema, init scripts | Python developer 1 |
| Kafka configuration, topics | Python developer 2 |
| CMake build, C++ common library | C++ developer |

---

## 14. Alternative Frontend — Option D: C# WPF + STK ActiveX Visualization
[↑ Table of Contents](#table-of-contents)

> **Status: Open for team/customer discussion.** This section documents a viable alternative to Option B's Electron + Angular + CesiumJS frontend. All backend services (drs-bridge, drs-server, sg-service, Kafka, TimescaleDB) are **identical** in both options — only the frontend layer changes.

---

### 14.1 Overview
[↑ Table of Contents](#table-of-contents)

Option D replaces the Electron + Angular + CesiumJS frontend with a native C# WPF desktop application that embeds STK's own 2D/3D globe controls directly. STK ships two ActiveX controls for this purpose:

- `AxAgUiAxVOCntrl` — the STK 3D globe viewer
- `AxAgUiAx2DCntrl` — the STK 2D map viewer

These are hosted in WPF via `WindowsFormsHost`, giving a native Windows desktop app with STK's physics-accurate globe embedded directly in the UI.

```csharp
// WPF XAML — STK globe embedded in layout
<WindowsFormsHost Grid.Row="0" Grid.Column="1">
    <stk:AxAgUiAxVOCntrl x:Name="stkGlobe3D" />
</WindowsFormsHost>
<WindowsFormsHost Grid.Row="1" Grid.Column="1">
    <stk:AxAgUiAx2DCntrl x:Name="stkMap2D" />
</WindowsFormsHost>

// C# code-behind — connect controls to STK scenario
IAgStkObjectRoot root = new AgStkObjectRootClass();
stkGlobe3D.SetAGISTKObject(root);
stkMap2D.SetAGISTKObject(root);
root.LoadScenario(@"C:\EWTSS\scenarios\exercise1.sc");
// RLOS cones, coverage areas, entity movement, access intervals
// all rendered natively by STK — no manual primitive construction
```

The STK Runtime Engine licenses (Qty 2, perpetual — already in RFQ Milestone 1) cover this deployment.

---

### 14.2 Option D1 — Python Computes, C# Visualises
[↑ Table of Contents](#table-of-contents)

Python sg-service continues to own STK computation via `agi.stk12`. The C# WPF app connects to STK only for visualisation — it loads the pre-computed scenario file and calls REST/WebSocket APIs for all other data.

```
┌─────────────────────────────────────────────────────────┐
│  SCENARIO GENERATOR WORKSTATION                         │
│                                                         │
│  ┌──────────────────────────────────────────────────┐  │
│  │  C# WPF Desktop Application                      │  │
│  │                                                   │  │
│  │  ┌────────────────────┐  ┌─────────────────────┐ │  │
│  │  │ STK 3D Globe       │  │ STK 2D Map          │ │  │
│  │  │ AxAgUiAxVOCntrl    │  │ AxAgUiAx2DCntrl     │ │  │
│  │  │ (WindowsFormsHost) │  │ (WindowsFormsHost)  │ │  │
│  │  └────────┬───────────┘  └──────────┬──────────┘ │  │
│  │           └──────────┬──────────────┘             │  │
│  │                      │ SetAGISTKObject()           │  │
│  │                      ▼                             │  │
│  │           ┌──────────────────┐                    │  │
│  │           │  STK Runtime     │◄── loads .sc file  │  │
│  │           │  (COM server)    │    from shared path │  │
│  │           └──────────────────┘                    │  │
│  │                                                   │  │
│  │  Panels: scenarios, entities, emitters, DRS data  │  │
│  │  HttpClient ──► sg-service REST (port 8000)       │  │
│  │  WebSocket  ──► sg-service WS  (port 8000)        │  │
│  │  WebSocket  ──► drs-server WS  (port 8001)        │  │
│  └──────────────────────────────────────────────────┘  │
│                                                         │
│  ┌────────────────────┐   ┌──────────────────────────┐ │
│  │  sg-service        │   │  drs-server              │ │
│  │  Python/FastAPI    │   │  Python/FastAPI (async)  │ │
│  │                    │   │                          │ │
│  │  Scenario CRUD     │   │  Kafka consumers         │ │
│  │  RBAC / Users      │   │  TimescaleDB writes      │ │
│  │  GIS serving       │   │  WebSocket push          │ │
│  │  PDF reports       │   │  Health status           │ │
│  │  DB backup         │   └──────────┬───────────────┘ │
│  │                    │              │                  │
│  │  STK computation ──┼──────────────────────────────► │
│  │  agi.stk12 COM     │  saves .sc to C:\EWTSS\        │ │
│  │  ProcessPoolExec.  │  scenarios\ (shared path)      │ │
│  └────────┬───────────┘              │                  │
│           └──────────┬───────────────┘                  │
│                      ▼                                   │
│           ┌──────────────────────┐                      │
│           │  TimescaleDB         │                      │
│           │  PostgreSQL 16 +     │                      │
│           │  Timescale 2.x       │                      │
│           └──────────────────────┘                      │
│                                                         │
│  ┌──────────────────────────────┐                      │
│  │  Kafka (KRaft, single node)  │                      │
│  └──────────────────────────────┘                      │
└─────────────────────────────────────────────────────────┘
                        ▲
                        │ Kafka produce/consume
                        │
┌───────────────────────┴─────────────────────────────────┐
│  DRS WORKSTATION  (identical to Option B)               │
│                                                         │
│  drs-bridge (Python + C++ shared libs)                  │
│  asyncio TCP servers ──► C++ parsers ──► Kafka produce  │
│                                                         │
│  TCP connections from DRS hardware instances            │
│  (RDFS, JV/UHF, JHF, SJRR, JLB, JMB, JHB×4,          │
│   AUS, PADS — up to 100 simultaneous)                  │
└─────────────────────────────────────────────────────────┘
```

**Data flow:**
1. Operator triggers computation via WPF UI → `POST /exercises/{id}/compute`
2. sg-service runs STK link analysis, writes `computed_links` to TimescaleDB, saves `.sc` file to `C:\EWTSS\scenarios\`
3. WPF app receives completion notification (WebSocket), loads the `.sc` file
4. Timeline scrubbing in the STK globe replays entity movement natively
5. Live DRS data (measurements, health) continues to arrive via WebSocket from drs-server

**Pros:**
- Python computation pipeline unchanged — lowest risk path to D
- STK COM connections can coexist (Python + C# both connected to same STK instance)
- REST/WebSocket API contracts with Python services are identical to Option B
- Straightforward migration if team later decides to move computation to C#

**Cons:**
- Two STK COM clients in play simultaneously — requires careful session management
- Python `agi.stk12` COM automation is less ergonomic than native C# COM interop
- `ProcessPoolExecutor` isolation still needed in Python for STK calls

---

### 14.3 Option D2 — C# Owns All STK Interaction
[↑ Table of Contents](#table-of-contents)

The C# WPF application takes full ownership of STK — both computation and visualisation. STK COM automation was designed for C#/.NET; this is the most natural integration. Python sg-service is simplified: it handles scenario CRUD, RBAC, GIS, PDF reports, and DB writes, but has no STK dependency.

```
┌─────────────────────────────────────────────────────────┐
│  SCENARIO GENERATOR WORKSTATION                         │
│                                                         │
│  ┌──────────────────────────────────────────────────┐  │
│  │  C# WPF Desktop Application                      │  │
│  │                                                   │  │
│  │  ┌────────────────────┐  ┌─────────────────────┐ │  │
│  │  │ STK 3D Globe       │  │ STK 2D Map          │ │  │
│  │  │ AxAgUiAxVOCntrl    │  │ AxAgUiAx2DCntrl     │ │  │
│  │  │ (WindowsFormsHost) │  │ (WindowsFormsHost)  │ │  │
│  │  └────────┬───────────┘  └──────────┬──────────┘ │  │
│  │           └──────────┬──────────────┘             │  │
│  │                      │ SetAGISTKObject()           │  │
│  │                      ▼                             │  │
│  │           ┌──────────────────┐                    │  │
│  │           │  STK Runtime     │                    │  │
│  │           │  (COM server)    │◄── IAgStkObjectRoot│  │
│  │           │                  │    built from DB   │  │
│  │           │  Link analysis   │    scenario data   │  │
│  │           │  Propagation     │    (no .sc file    │  │
│  │           │  Access intervals│     on disk)       │  │
│  │           └──────────────────┘                    │  │
│  │                      │                             │  │
│  │           computed_links results                   │  │
│  │                      │                             │  │
│  │  HttpClient ──► POST /exercises/{id}/links         │  │
│  │  HttpClient ──► sg-service REST  (port 8000)       │  │
│  │  WebSocket  ──► sg-service WS    (port 8000)       │  │
│  │  WebSocket  ──► drs-server WS    (port 8001)       │  │
│  └──────────────────────────────────────────────────┘  │
│                          │                              │
│           REST calls ────┤                              │
│                          ▼                              │
│  ┌────────────────────┐   ┌──────────────────────────┐ │
│  │  sg-service        │   │  drs-server              │ │
│  │  Python/FastAPI    │   │  Python/FastAPI (async)  │ │
│  │                    │   │                          │ │
│  │  Scenario CRUD     │   │  Kafka consumers         │ │
│  │  RBAC / Users      │   │  TimescaleDB writes      │ │
│  │  GIS serving       │   │  WebSocket push          │ │
│  │  PDF reports       │   │  Health status           │ │
│  │  DB backup         │   └──────────┬───────────────┘ │
│  │                    │              │                  │
│  │  NO STK dependency │              │                  │
│  │  (STK fully in C#) │              │                  │
│  └────────┬───────────┘              │                  │
│           └──────────┬───────────────┘                  │
│                      ▼                                   │
│           ┌──────────────────────┐                      │
│           │  TimescaleDB         │                      │
│           │  PostgreSQL 16 +     │                      │
│           │  Timescale 2.x       │                      │
│           └──────────────────────┘                      │
│                                                         │
│  ┌──────────────────────────────┐                      │
│  │  Kafka (KRaft, single node)  │                      │
│  └──────────────────────────────┘                      │
└─────────────────────────────────────────────────────────┘
                        ▲
                        │ Kafka produce/consume
                        │
┌───────────────────────┴─────────────────────────────────┐
│  DRS WORKSTATION  (identical to Option B)               │
│                                                         │
│  drs-bridge (Python + C++ shared libs)                  │
│  asyncio TCP servers ──► C++ parsers ──► Kafka produce  │
│                                                         │
│  TCP connections from DRS hardware instances            │
│  (RDFS, JV/UHF, JHF, SJRR, JLB, JMB, JHB×4,          │
│   AUS, PADS — up to 100 simultaneous)                  │
└─────────────────────────────────────────────────────────┘
```

**Data flow:**
1. Operator configures exercise in WPF UI; WPF fetches scenario data via `GET /exercises/{id}`
2. Operator triggers computation in WPF → C# builds STK scenario from exercise data, runs link analysis
3. C# posts computed results to sg-service: `POST /exercises/{id}/links` (batch insert to `computed_links`)
4. Exercise status updated to `ready`; STK globe already has the scenario loaded — visualisation is immediate
5. Live DRS data continues via WebSocket from drs-server

**Pros:**
- C# COM automation is the primary design intent of the STK API — more natural, better typed, fewer workarounds
- No `agi.stk12` in Python — sg-service has no STK dependency, simpler to test and deploy
- No `ProcessPoolExecutor` needed — STK runs on the C# UI thread (or a dedicated background thread)
- Single STK COM connection — no session contention
- STK scenario is in memory in the C# app — visualisation loads instantly after computation

**Cons:**
- C# developer's scope significantly expands: C++ parsers + WPF app + STK computation
- sg-service loses the STK computation logic — team must agree on the API contract for posting computed_links
- If STK license is unavailable at runtime, there is no Python fallback — the WPF app cannot start the computation pipeline (MockStkService fallback must be re-implemented in C#)

---

### 14.4 Frontend Layer Comparison — Option B vs D
[↑ Table of Contents](#table-of-contents)

| Concern | Option B (Electron + Angular + CesiumJS) | Option D (C# WPF + STK ActiveX) |
|---|---|---|
| **3D terrain** | CesiumJS terrain from DTED files | STK's own terrain engine, DTED native |
| **RLOS / coverage rendering** | Manual CesiumJS corridor/cone primitives built from `computed_links` | STK native — rendered automatically from scenario objects |
| **Timeline scrubbing** | Custom Angular component reading `computed_links` from DB | STK native timeline control, frame-accurate |
| **Access interval display** | Computed from DB rows, rendered as overlays | STK native access intervals, exactly matching computation |
| **Link budget overlay** | Custom chart components | STK Data Provider outputs directly in viewer |
| **UI richness (forms, tables, logs)** | Angular Reactive Forms — strong | WPF data binding + MVVM — capable but more verbose |
| **Desktop installer size** | ~200MB (Chromium bundled) | ~20MB (no browser runtime) |
| **Build toolchain** | Node.js + Angular CLI + Electron Builder | .NET SDK only |
| **Air-gap vendoring** | npm vendoring + pip vendoring | NuGet vendoring + pip vendoring (no Node.js) |
| **STK license required on workstation** | No (CesiumJS is independent) | Yes (STK Runtime Engine — already in RFQ) |
| **Future web access possibility** | Yes — remove Electron shell, Angular app runs in browser | No — WPF is Windows desktop only |
| **Angular developer utility** | Full ownership | Needs WPF/XAML pivot (significant reskilling) |
| **C# developer load** | Low — parsers only | High — parsers + WPF + STK (D2) or parsers + WPF (D1) |
| **Python STK complexity** | agi.stk12 COM in ProcessPoolExecutor | D1: unchanged / D2: removed entirely |

---

### 14.5 Team Ownership — Option D
[↑ Table of Contents](#table-of-contents)

**Option D1:**

| Component | Owner |
|---|---|
| sg-service (FastAPI, STK computation, GIS) | Python developer 1 |
| drs-server (async consumers, TimescaleDB) | Python developer 2 |
| drs-bridge (C++ parsers, asyncio TCP) | C++ developer (parsers) + Python developer (config) |
| C# WPF app (UI, STK visualisation) | C# developer(s) — Python devs with C# background |
| Angular developer | Redeployable to sg-service frontend utilities or test tooling |
| DB schema, init scripts | Python developer 1 |
| Kafka configuration | Python developer 2 |
| CMake build, C++ common library | C++ developer |

**Option D2:**

| Component | Owner |
|---|---|
| sg-service (FastAPI, GIS, RBAC — no STK) | Python developer 1 |
| drs-server (async consumers, TimescaleDB) | Python developer 2 |
| drs-bridge (C++ parsers, asyncio TCP) | C++ developer (parsers) + Python developer (config) |
| C# WPF app (UI + STK computation + visualisation) | C++ developer + Python devs with C# background |
| Angular developer | Redeployable — role needs redefinition |
| DB schema, init scripts | Python developer 1 |
| Kafka configuration | Python developer 2 |

---

### 14.6 Discussion Points for Team/Customer Review
[↑ Table of Contents](#table-of-contents)

The following questions should be resolved before selecting Option B or Option D:

1. **Angular developer's role:** Option D significantly reduces or eliminates the Angular developer's primary domain. Is the team willing to retrain on WPF/XAML, or is retaining Angular expertise a priority?

2. **C# developer capacity:** Option D2 gives the C++ developer a substantially larger scope (parsers + WPF + STK). Is that feasible within the project timeline?

3. **Visualisation accuracy requirement:** Does the client require that RLOS cones and coverage areas in the UI exactly match the STK computation (favours D), or is a well-implemented CesiumJS approximation acceptable (favours B)?

4. **STK license availability during development:** Option D requires the STK Runtime Engine on the development machine. Option B's frontend can be developed without any STK dependency. When will the Milestone 1 STK licenses be available?

5. **Future portability:** Is there any possibility the application will need to run in a browser or on non-Windows platforms in future? If yes, Option B preserves that path; Option D closes it.

6. **Mock/fallback for STK-unavailable environments:** Option B has a clean Python MockStkService. Option D (especially D2) needs an equivalent mock in C# to allow development without an active STK license.

---

### 14.7 Recommendation Summary
[↑ Table of Contents](#table-of-contents)

| | Option B | Option D1 | Option D2 |
|---|---|---|---|
| **Visualisation fidelity** | Good (CesiumJS approximation) | Exact (STK native) | Exact (STK native) |
| **Team fit (current)** | Best — all roles utilised | Good — Angular dev redeployment needed | Moderate — C# scope heavy |
| **Implementation risk** | Low | Medium | Medium-High |
| **Installer footprint** | ~200MB | ~20MB | ~20MB |
| **Backend complexity** | Moderate (STK in Python) | Moderate (STK in Python) | Lower (STK in C#) |
| **Best for** | Balanced team, future flexibility | STK fidelity without disrupting Python backend | Cleanest STK integration, highest C# commitment |

**Suggested discussion outcome:** If the client places high value on STK visualisation accuracy (RLOS cones, access intervals matching computation exactly), **Option D1** is the lowest-risk path to get there — Python backend unchanged, C# WPF app added for visualisation. Option D2 is architecturally cleaner but requires honest assessment of the C# developer's capacity. Option B remains the right choice if team balance and future web portability outweigh visualisation fidelity.

---

## 15. Operating Modes — Control & Data Flow (Option D2)
[↑ Table of Contents](#table-of-contents)

The RFQ defines two operating modes: **Standalone** and **Integrated**. Each mode also supports two simulation sub-modes: **Random** and **Scenario**. This section documents the complete flow of control and data for all four combinations under Option D2.

---

### 15.1 Mode Definitions
[↑ Table of Contents](#table-of-contents)

| Mode | Deployment | Entity Applications Present | DRS Purpose |
|---|---|---|---|
| **Standalone** | Single or two workstations (SG + DRS), no entity workstations on LAN | No | Operator plans/tests exercises; visualises results locally without entity app involvement |
| **Integrated** | Full LAN: SG workstation + DRS workstation + all entity workstations | Yes | DRS sends IRS-compliant TCP messages to entity controller applications in lieu of physical devices |

| Simulation Sub-mode | Drives data from | STK required |
|---|---|---|
| **Random** | Parameter ranges configured by operator; drs-bridge generates data within bounds | No |
| **Scenario** | Pre-computed `computed_links` table driven by STK physics analysis | Yes |

---

### 15.2 Standalone + Random Mode
[↑ Table of Contents](#table-of-contents)

**Purpose:** Operator verifies DRS parameter ranges and message formatting without entity applications or STK. Used during development and system checkout.

```
┌─────────────────────────────────────────────────────────────────┐
│  CONTROL FLOW                                                   │
│                                                                 │
│  1. Operator configures hardware profiles and parameter         │
│     ranges (frequency min/max, power range, etc.) in WPF       │
│     └── WPF ──► POST /hardware-profiles  ──► sg-service        │
│                                                                 │
│  2. Operator starts random data stream                          │
│     └── WPF ──► POST /sessions/start-random ──► drs-server     │
│                 {hardware_type, instance_id, ranges}            │
│                                                                 │
│  3. drs-server publishes start command to Kafka                 │
│     └── Kafka topic: drs.control.{hardware_type}               │
│                                                                 │
│  4. drs-bridge consumes control message, begins                 │
│     generating random IRS-formatted data internally            │
│     └── C++ parser: generate_random(ranges) ──► bytes          │
│                                                                 │
│  5. drs-bridge publishes generated messages to Kafka            │
│     └── Kafka topic: {hardware_type}.drs.ui                    │
│                                                                 │
│  6. drs-server consumes, writes to TimescaleDB,                 │
│     broadcasts over WebSocket                                   │
│     └── WS /ws/measurements/{hardware_type} ──► WPF            │
│                                                                 │
│  7. WPF displays live measurements in data panels               │
│     STK globe: no scenario loaded — health/status view only    │
│                                                                 │
│  NOTE: No entity applications connected.                        │
│  TCP servers on drs-bridge are running but idle.               │
└─────────────────────────────────────────────────────────────────┘
```

```
DATA FLOW

drs-bridge                    Kafka                  drs-server          WPF
  │                             │                        │                │
  │── generate_random() ───────►│                        │                │
  │   C++ formats IRS bytes     │  {hw_type}.drs.ui      │                │
  │── publish(msg) ────────────►│──────────────────────►│                │
  │                             │                        │── write to DB  │
  │                             │                        │── broadcast ──►│
  │                             │                        │   WebSocket    │
  │                             │                        │                │── display
  │                             │                        │                │   in panels
```

---

### 15.3 Standalone + Scenario Mode
[↑ Table of Contents](#table-of-contents)

**Purpose:** Operator plans a full exercise, computes link analysis via STK, and replays the scenario locally to verify results before integrating with entity applications. No entity workstations required.

```
┌─────────────────────────────────────────────────────────────────┐
│  PHASE 1 — PLANNING                                             │
│                                                                 │
│  1. Operator creates exercise in WPF                            │
│     └── WPF ──► POST /exercises ──► sg-service                 │
│                                                                 │
│  2. Operator places entities and emitters on STK globe          │
│     └── WPF ──► POST /exercises/{id}/entities                  │
│     └── WPF ──► POST /exercises/{id}/emitters                  │
│     └── WPF ──► POST /exercises/{id}/environment               │
│                                                                 │
│  3. Operator triggers computation                               │
│     └── WPF calls: C# STK service (in-process)                 │
│                                                                 │
│  4. C# builds STK scenario from exercise data                   │
│     └── GET /exercises/{id} ──► sg-service ──► DB              │
│     └── IAgStkObjectRoot: add entities, emitters,              │
│         terrain (DTED), environment, antenna patterns          │
│                                                                 │
│  5. C# runs time-dynamic link analysis                          │
│     └── For each tick (1 sec default):                         │
│         ├── Update mobile entity/emitter positions             │
│         ├── Compute access intervals (visibility)              │
│         ├── Compute AER, path loss, signal strength            │
│         └── Buffer results                                      │
│                                                                 │
│  6. C# posts computed results to sg-service                     │
│     └── POST /exercises/{id}/links (batched, 100 ticks)        │
│     └── sg-service writes to computed_links (TimescaleDB)      │
│     └── Exercise status updated: computed → ready              │
│                                                                 │
│  STK globe already has the scenario loaded —                   │
│  operator can scrub the timeline to review results             │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│  PHASE 2 — EXECUTION (Standalone)                               │
│                                                                 │
│  7. Operator starts exercise                                    │
│     └── WPF ──► POST /exercises/{id}/execute ──► sg-service    │
│                                                                 │
│  8. sg-service reads computed_links for tick T,                 │
│     publishes to Kafka scenario execution topic                 │
│     └── Kafka topic: scenario.execution                        │
│     └── Payload: {tick_sec, entity_id, emitter_id,            │
│                   range_m, azimuth_deg, power_dbm, ...}        │
│                                                                 │
│  9. drs-bridge consumes scenario.execution messages            │
│     └── C++ formatter converts computed values                 │
│         to IRS-compliant TCP bytes for each hardware type      │
│     └── In Standalone: no entity apps connected;              │
│         formatted messages published back to Kafka             │
│         (topic: {hw_type}.drs.ui) for local display            │
│                                                                 │
│  10. drs-server consumes, writes to TimescaleDB,               │
│      broadcasts over WebSocket                                  │
│      └── WS /ws/measurements/{hardware_type} ──► WPF           │
│                                                                 │
│  11. WPF displays live exercise in STK globe                   │
│      └── Entity movement animated on globe (from STK scenario)  │
│      └── Data panels show measurement stream                   │
│      └── RLOS cones update per tick                            │
└─────────────────────────────────────────────────────────────────┘
```

```
DATA FLOW — Execution Phase (Standalone)

sg-service            Kafka               drs-bridge            drs-server         WPF (STK)
  │                     │                     │                     │                  │
  │── read tick T ──    │                     │                     │                  │
  │   computed_links    │  scenario.execution │                     │                  │
  │── publish ─────────►│────────────────────►│                     │                  │
  │   tick payload      │                     │── C++ format ──     │                  │
  │                     │                     │   IRS bytes         │                  │
  │                     │  {hw}.drs.ui        │                     │                  │
  │                     │◄────────────────────│── publish ──────────│                  │
  │                     │                     │                     │── write DB       │
  │                     │                     │                     │── broadcast ────►│
  │                     │                     │                     │   WebSocket      │── update
  │                     │                     │                     │                  │   globe
  │── tick T+1 ────────►│  (repeats per tick) │                     │                  │
```

---

### 15.4 Integrated + Random Mode
[↑ Table of Contents](#table-of-contents)

**Purpose:** Full classroom training with entity applications active. DRS generates random emitter data within configured ranges and delivers IRS-compliant messages directly to entity controller applications on the LAN.

```
┌─────────────────────────────────────────────────────────────────┐
│  CONTROL FLOW                                                   │
│                                                                 │
│  1–4. Same as Standalone Random (parameter config,             │
│       stream start, drs-bridge generates data)                 │
│                                                                 │
│  5. drs-bridge sends IRS TCP messages to                        │
│     entity controller applications on LAN                      │
│     └── Each active entity app is a connected TCP client       │
│         on its registered IP:port                              │
│     └── C++ parser formats random data per hardware IRS:       │
│         ├── RDFS entity ──► IRS-RDFS-v2 message bytes          │
│         ├── JHF entity  ──► IRS-JHF message bytes              │
│         ├── JV/UHF      ──► IRS-JVUHF message bytes           │
│         └── ... (one formatter per hardware type C++ lib)      │
│                                                                 │
│  6. Entity applications receive and process IRS messages       │
│     └── Entity apps respond (ACK, health status, commands)     │
│                                                                 │
│  7. drs-bridge receives entity responses                        │
│     └── Publishes to Kafka: {hw_type}.entity.response          │
│                                                                 │
│  8. drs-server consumes all topics, writes to TimescaleDB,     │
│     broadcasts over WebSocket                                   │
│     └── WPF shows live sent + received message logs            │
└─────────────────────────────────────────────────────────────────┘
```

```
DATA FLOW — Integrated Random

drs-bridge              Kafka                drs-server           WPF
  │                       │                      │                  │
  │── generate_random()   │                      │                  │
  │   C++ formats IRS     │  {hw}.drs.ui         │                  │
  │── publish ───────────►│─────────────────────►│── write DB       │
  │                       │                      │── broadcast ────►│
  │                       │                      │                  │
  │── TCP send ──────────────────────────────────────────────────►  │
  │   IRS bytes           │            Entity Controller Apps (LAN) │
  │                       │                      │                  │
  │◄─ TCP receive ─────────────────────────────────────────────────  │
  │   entity response     │  {hw}.entity.response│                  │
  │── publish ───────────►│─────────────────────►│── write DB       │
  │                       │                      │── broadcast ────►│── display
  │                       │                      │                  │   response log
```

---

### 15.5 Integrated + Scenario Mode
[↑ Table of Contents](#table-of-contents)

**Purpose:** Full mission exercise. Scenario drives entity applications with physics-accurate data computed by STK. This is the primary operational mode for classroom-based training as described in the RFQ.

```
┌─────────────────────────────────────────────────────────────────┐
│  PHASE 1 — PLANNING  (same as Standalone Scenario, steps 1–6)  │
│  Exercise configured, STK computation run by C#,               │
│  computed_links stored in TimescaleDB, status = ready           │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│  PHASE 2 — PRE-EXECUTION CHECKS (Integrated only)               │
│                                                                 │
│  7. Operator verifies entity application connectivity           │
│     └── WPF ──► GET /health/entities ──► drs-server            │
│         Shows which entity workstations are connected           │
│         to drs-bridge TCP servers                              │
│                                                                 │
│  8. Operator issues mission briefing / start command            │
│     └── WPF ──► POST /exercises/{id}/execute                   │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│  PHASE 3 — EXECUTION (Integrated Scenario)                      │
│                                                                 │
│  9. sg-service reads computed_links tick by tick,               │
│     publishes to Kafka scenario.execution                       │
│                                                                 │
│  10. drs-bridge consumes each tick payload:                     │
│      └── For each entity–emitter pair in the tick:             │
│          ├── Retrieve hardware type for entity                  │
│          ├── C++ formatter: converts (range, azimuth,          │
│          │   elevation, power, doppler) → IRS TCP bytes        │
│          └── TCP send to connected entity application           │
│                                                                 │
│  11. Entity applications process IRS messages:                  │
│      ├── RDFS: displays bearing, frequency, signal strength    │
│      ├── JHF: receives jamming parameters                      │
│      ├── JV/UHF: receives V/UHF band parameters               │
│      └── RISC: receives radar track data                       │
│                                                                 │
│  12. Entity applications respond to drs-bridge:                 │
│      └── Health status, ACKs, operator commands               │
│                                                                 │
│  13. Simultaneously: Kafka → drs-server → TimescaleDB          │
│      └── WS broadcast ──► WPF                                  │
│          ├── STK globe animates entity movement                │
│          ├── RLOS cones update per tick                        │
│          ├── Measurement panels show live IRS data             │
│          └── Entity response log shows ack/health stream       │
│                                                                 │
│  14. Operator can pause/resume/stop exercise at any time        │
│      └── WPF ──► POST /exercises/{id}/pause                    │
│          sg-service stops publishing ticks to Kafka            │
│          drs-bridge stops sending to entity apps               │
└─────────────────────────────────────────────────────────────────┘
```

```
DATA FLOW — Integrated Scenario Execution

sg-service         Kafka              drs-bridge            Entity Apps (LAN)
  │                  │                    │                       │
  │── read tick T    │                    │                       │
  │   computed_links │ scenario.execution │                       │
  │── publish ──────►│───────────────────►│                       │
  │                  │                    │── C++ format          │
  │                  │                    │   IRS bytes           │
  │                  │                    │── TCP send ──────────►│
  │                  │                    │                       │── process IRS
  │                  │                    │◄─ TCP response ───────│   message
  │                  │  {hw}.entity.resp  │                       │
  │                  │◄───────────────────│── publish             │
  │                  │                    │                       │
  │                  │  {hw}.drs.ui       │                       │
  │                  │◄───────────────────│── publish             │
  │                  │                    │                       │
  │                  ▼                    │                       │
             drs-server                  │                       │
               │                         │                       │
               │── write TimescaleDB     │                       │
               │── WS broadcast ──────────────────────────────►  │
               │                                          WPF (STK globe + panels)
  │── tick T+1 ──────────────────────────────────────────────────  │
  │   (repeats every configured tick interval)                     │
```

---

### 15.6 Mode Comparison Summary
[↑ Table of Contents](#table-of-contents)

| | Standalone + Random | Standalone + Scenario | Integrated + Random | Integrated + Scenario |
|---|---|---|---|---|
| **STK needed** | No | Yes (C# in-process) | No | Yes (C# in-process) |
| **Entity apps connected** | No | No | Yes | Yes |
| **drs-bridge TCP servers** | Running, idle | Running, idle | Active — entity apps connected | Active — entity apps connected |
| **Data source** | C++ random generator | `computed_links` table | C++ random generator | `computed_links` table |
| **IRS messages sent to** | Kafka only (local display) | Kafka only (local display) | Kafka + entity apps via TCP | Kafka + entity apps via TCP |
| **sg-service role** | Config store, REST API | Config + computed_links store | Config store, REST API | Tick publisher from computed_links |
| **WPF STK globe** | Health/status view | Full scenario replay | Health/status view | Live scenario with entity responses |
| **Primary use** | Dev/checkout | Exercise design & verification | Rehearsal / unscripted training | Full mission exercise |

---

### 15.7 Kafka Topics by Mode
[↑ Table of Contents](#table-of-contents)

| Topic | Producer | Consumer | Active in |
|---|---|---|---|
| `{hw_type}.drs.ui` | drs-bridge | drs-server | All modes |
| `{hw_type}.entity.response` | drs-bridge | drs-server | Integrated modes only |
| `scenario.execution` | sg-service | drs-bridge | Scenario modes only |
| `drs.control.{hw_type}` | drs-server | drs-bridge | Random modes (start/stop/config) |

---

## 16. Operating Modes — Control & Data Flow (Option B)
[↑ Table of Contents](#table-of-contents)

Option B shares the same backend services, Kafka topology, and drs-bridge behaviour as Option D2. The differences are confined to **where STK computation runs** and **how results are visualised**. This section documents all four mode combinations for direct comparison with Section 15.

---

### 16.1 Key Differences vs Option D2
[↑ Table of Contents](#table-of-contents)

| Concern | Option D2 | Option B |
|---|---|---|
| **STK computation** | C# in-process (WPF, same thread as UI) | Python `ProcessPoolExecutor` inside sg-service |
| **Computation trigger** | C# calls STK COM directly, posts results to sg-service REST | Frontend calls `POST /exercises/{id}/compute`, sg-service runs job |
| **Visualisation engine** | STK ActiveX globe — renders natively from scenario object | CesiumJS — reads `computed_links` from DB, builds primitives manually |
| **RLOS / coverage display** | STK native — exact match to computation | CesiumJS corridor/cone primitives — approximate reconstruction |
| **Timeline scrubbing** | STK native timeline control | Angular component reads `computed_links` via REST, animates on CesiumJS |
| **STK unavailable fallback** | No mock in C# (must be implemented separately) | `MockStkService` in Python — Friis + geometric LOS from DTED |
| **drs-bridge behaviour** | Identical | Identical |
| **Kafka topics** | Identical | Identical |
| **Entity app interaction** | Identical | Identical |
| **TimescaleDB writes** | Identical | Identical |

**Everything from Sections 15.2–15.7 that involves drs-bridge, drs-server, Kafka, entity applications, and TimescaleDB applies unchanged to Option B.** The sections below document only the frontend and computation differences for each mode.

---

### 16.2 Standalone + Random Mode
[↑ Table of Contents](#table-of-contents)

```
┌─────────────────────────────────────────────────────────────────┐
│  CONTROL FLOW                                                   │
│                                                                 │
│  1. Operator configures hardware profiles and parameter         │
│     ranges in Angular UI (browser tab inside Electron)          │
│     └── Angular ──► POST /hardware-profiles ──► sg-service     │
│                                                                 │
│  2. Operator starts random data stream                          │
│     └── Angular ──► POST /sessions/start-random ──► drs-server │
│                 {hardware_type, instance_id, ranges}            │
│                                                                 │
│  3–6. Identical to Option D2 Standalone + Random               │
│       drs-bridge generates random data → Kafka → drs-server     │
│       → TimescaleDB + WebSocket broadcast                       │
│                                                                 │
│  7. Angular receives WebSocket messages via RxJS                │
│     WebSocketSubject                                            │
│     └── WS /ws/measurements/{hardware_type} ──► Angular        │
│     └── Live data rendered in measurement tables and           │
│         ngx-charts frequency/power plots                       │
│     └── CesiumJS globe: health/status markers only             │
│         (no scenario loaded in random mode)                    │
│                                                                 │
│  NOTE: No entity applications connected.                        │
│  drs-bridge TCP servers running but idle.                      │
└─────────────────────────────────────────────────────────────────┘
```

```
DATA FLOW

drs-bridge           Kafka               drs-server         Angular (Electron)
  │                    │                     │                    │
  │── generate_random()│                     │                    │
  │   C++ formats IRS  │  {hw}.drs.ui        │                    │
  │── publish ────────►│────────────────────►│── write DB         │
  │                    │                     │── WS broadcast ───►│
  │                    │                     │   WebSocketSubject │── update tables
  │                    │                     │                    │   + charts
```

**Difference from D2:** Angular replaces the WPF data panels; RxJS `WebSocketSubject` replaces `System.Net.WebSockets`. CesiumJS globe shows no scenario in random mode (same as D2's STK globe). Functionally identical.

---

### 16.3 Standalone + Scenario Mode
[↑ Table of Contents](#table-of-contents)

```
┌─────────────────────────────────────────────────────────────────┐
│  PHASE 1 — PLANNING                                             │
│                                                                 │
│  1. Operator creates exercise in Angular UI                     │
│     └── Angular ──► POST /exercises ──► sg-service             │
│                                                                 │
│  2. Operator places entities and emitters on CesiumJS globe     │
│     └── Click on globe → Angular sends coordinates to          │
│         sg-service:                                            │
│         POST /exercises/{id}/entities                          │
│         POST /exercises/{id}/emitters                          │
│         POST /exercises/{id}/environment                       │
│                                                                 │
│  3. Operator triggers computation                               │
│     └── Angular ──► POST /exercises/{id}/compute ──► sg-service│
│                                                                 │
│  4. sg-service runs STK computation in ProcessPoolExecutor      │
│     (isolated subprocess — STK COM is Windows COM-bound,       │
│      must not run on asyncio event loop thread)                │
│     └── StkComService OR MockStkService (if STK unavailable)   │
│         ├── Build scenario from exercise DB data               │
│         ├── Add entities, emitters, DTED terrain               │
│         ├── Set atmospheric environment                        │
│         └── Run time-dynamic link analysis                     │
│                                                                 │
│  5. sg-service writes computed_links to TimescaleDB             │
│     └── Batch insert every 100 ticks                           │
│     └── Exercise status: computed → ready                      │
│     └── WebSocket notification to Angular:                     │
│         WS /ws/exercise/{id}/status ──► Angular                │
│                                                                 │
│  6. Angular receives completion; operator scrubs timeline       │
│     └── Angular calls: GET /exercises/{id}/links?tick={T}      │
│         ──► sg-service ──► TimescaleDB                        │
│     └── CesiumJS animates entity positions from response       │
│     └── RLOS cones reconstructed as CesiumJS corridor          │
│         primitives from (azimuth, range, elevation) values     │
│                                                                 │
│  NOTE: Unlike Option D2, the CesiumJS globe does not have      │
│  a live STK scenario object in memory. All visualisation       │
│  is reconstructed from computed_links rows fetched from DB.    │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│  PHASE 2 — EXECUTION (Standalone)                               │
│                                                                 │
│  7. Operator starts exercise                                    │
│     └── Angular ──► POST /exercises/{id}/execute ──► sg-service│
│                                                                 │
│  8–11. Identical to Option D2 Standalone Scenario              │
│        sg-service ticks → Kafka scenario.execution →           │
│        drs-bridge formats IRS → publishes to {hw}.drs.ui →     │
│        drs-server → TimescaleDB + WebSocket broadcast           │
│                                                                 │
│  12. Angular renders live execution on CesiumJS globe           │
│      └── WebSocket stream updates entity positions on globe    │
│      └── RLOS cones rebuilt per tick from incoming data        │
│      └── Measurement panels updated via WebSocketSubject       │
│                                                                 │
│  VISUALISATION FIDELITY NOTE:                                   │
│  CesiumJS RLOS cones are geometrically constructed from        │
│  (azimuth, elevation, range, path_loss) values in              │
│  computed_links. They visually approximate the STK result      │
│  but are not physics-rendered by STK. In Option D2, the        │
│  same values drive STK's own rendering engine — the cones      │
│  are identical to what the computation produced.               │
└─────────────────────────────────────────────────────────────────┘
```

```
DATA FLOW — Planning Phase (Option B specific)

Angular (Electron)        sg-service (Python)              TimescaleDB
  │                            │                                │
  │── POST /compute ──────────►│                                │
  │                            │── ProcessPoolExecutor          │
  │                            │   StkComService.run()          │
  │                            │   (or MockStkService)          │
  │                            │── write computed_links ───────►│
  │                            │   (batched, 100 ticks/commit)  │
  │◄─ WS status: ready ────────│                                │
  │                            │                                │
  │── GET /links?tick=T ──────►│── SELECT computed_links ──────►│
  │◄─ {range, az, el, ...} ────│◄──────────────────────────────│
  │                            │                                │
  │── CesiumJS: build RLOS     │                                │
  │   cone primitive from data │                                │
```

---

### 16.4 Integrated + Random Mode
[↑ Table of Contents](#table-of-contents)

```
┌─────────────────────────────────────────────────────────────────┐
│  CONTROL FLOW                                                   │
│                                                                 │
│  1–4. Same as Option B Standalone + Random                      │
│       Angular configures ranges, starts stream,                │
│       drs-bridge generates random IRS data                     │
│                                                                 │
│  5–8. Identical to Option D2 Integrated + Random               │
│       drs-bridge sends IRS TCP to entity apps (LAN)            │
│       Entity apps respond                                       │
│       Responses published to Kafka → drs-server                │
│       WebSocket broadcast → Angular                            │
│                                                                 │
│  9. Angular receives entity response stream                     │
│     └── WS /ws/measurements/{hw_type} ──► Angular              │
│     └── Message log component shows sent + received            │
│     └── CesiumJS globe shows entity health status markers      │
└─────────────────────────────────────────────────────────────────┘
```

**Difference from D2:** Angular + CesiumJS replace the WPF panels for display. All drs-bridge ↔ entity application TCP communication is identical. Functionally equivalent.

---

### 16.5 Integrated + Scenario Mode
[↑ Table of Contents](#table-of-contents)

```
┌─────────────────────────────────────────────────────────────────┐
│  PHASE 1 — PLANNING  (see Section 16.3, steps 1–6)             │
│  Exercise configured in Angular, STK run by Python sg-service  │
│  in ProcessPoolExecutor, computed_links stored, status = ready  │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│  PHASE 2 — PRE-EXECUTION CHECKS (Integrated only)               │
│                                                                 │
│  7. Operator verifies entity application connectivity           │
│     └── Angular ──► GET /health/entities ──► drs-server        │
│         Health grid component shows connected entity apps      │
│                                                                 │
│  8. Operator issues start command                               │
│     └── Angular ──► POST /exercises/{id}/execute ──► sg-service│
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│  PHASE 3 — EXECUTION (Integrated Scenario)                      │
│                                                                 │
│  9–13. Identical to Option D2 Integrated + Scenario            │
│        sg-service ticks computed_links → Kafka →               │
│        drs-bridge C++ formats IRS → TCP to entity apps →       │
│        entity responses → Kafka → drs-server →                 │
│        WebSocket broadcast                                      │
│                                                                 │
│  14. Angular renders live execution on CesiumJS globe           │
│      └── WebSocketSubject stream updates entity positions      │
│          ├── Entity markers move on CesiumJS globe per tick    │
│          ├── RLOS cones rebuilt each tick from WebSocket data  │
│          ├── Measurement tables updated live                   │
│          └── Entity response log shows ACK/health stream       │
│                                                                 │
│  15. Operator pause/resume/stop                                 │
│      └── Angular ──► POST /exercises/{id}/pause                │
│          sg-service stops publishing ticks to Kafka            │
│          drs-bridge stops sending to entity apps               │
└─────────────────────────────────────────────────────────────────┘
```

```
DATA FLOW — Integrated Scenario Execution (Option B)

sg-service        Kafka             drs-bridge          Entity Apps (LAN)
  │                 │                   │                      │
  │── read tick T   │ scenario.execution│                      │
  │── publish ─────►│──────────────────►│                      │
  │                 │                   │── C++ format IRS     │
  │                 │                   │── TCP send ─────────►│
  │                 │                   │                      │── process msg
  │                 │                   │◄─ TCP response ───────│
  │                 │ {hw}.entity.resp  │                      │
  │                 │◄──────────────────│── publish            │
  │                 │ {hw}.drs.ui       │                      │
  │                 │◄──────────────────│── publish            │
  │                 ▼                   │                      │
            drs-server                 │                      │
               │── write DB            │                      │
               │── WS broadcast ───────────────────────────►  │
               │                                    Angular (Electron)
               │                                    CesiumJS globe:
               │                                    ├── entity markers move
               │                                    ├── RLOS cones rebuilt
               │                                    └── measurement panels
  │── tick T+1 ────────────────────────────────────────────────  │
```

---

### 16.6 Mode Comparison Summary (Option B)
[↑ Table of Contents](#table-of-contents)

| | Standalone + Random | Standalone + Scenario | Integrated + Random | Integrated + Scenario |
|---|---|---|---|---|
| **STK needed** | No | Yes (Python ProcessPoolExecutor) | No | Yes (Python ProcessPoolExecutor) |
| **Entity apps connected** | No | No | Yes | Yes |
| **drs-bridge TCP servers** | Running, idle | Running, idle | Active | Active |
| **Data source** | C++ random generator | `computed_links` table | C++ random generator | `computed_links` table |
| **IRS messages sent to** | Kafka only | Kafka only | Kafka + entity apps via TCP | Kafka + entity apps via TCP |
| **Visualisation** | CesiumJS health markers | CesiumJS with reconstructed primitives | CesiumJS health markers | CesiumJS with reconstructed primitives |
| **Primary use** | Dev/checkout | Exercise design & verification | Rehearsal / unscripted training | Full mission exercise |

---

### 16.7 Side-by-Side Flow Comparison — Option B vs Option D2
[↑ Table of Contents](#table-of-contents)

| Flow Step | Option B | Option D2 |
|---|---|---|
| **Operator places entities on map** | Click on CesiumJS globe | Click on STK ActiveX globe |
| **Computation trigger** | `POST /exercises/{id}/compute` → Python sg-service | C# calls STK COM in-process → `POST /exercises/{id}/links` |
| **STK runs on** | Python `ProcessPoolExecutor` (subprocess) | C# background thread (same process as UI) |
| **Computation result storage** | sg-service writes `computed_links` | C# posts `computed_links` to sg-service REST |
| **Visualisation after compute** | Angular fetches `computed_links` rows → CesiumJS primitives | STK globe already has scenario in memory — immediate |
| **Timeline scrubbing** | Angular REST calls per tick → CesiumJS rebuild | STK native timeline control |
| **RLOS cone accuracy** | Geometric approximation from stored values | Physics-accurate, native to STK renderer |
| **Exercise execution tick** | sg-service → Kafka → drs-bridge (identical) | sg-service → Kafka → drs-bridge (identical) |
| **IRS TCP to entity apps** | drs-bridge C++ (identical) | drs-bridge C++ (identical) |
| **Live data to UI** | WebSocketSubject → Angular | System.Net.WebSockets → WPF MVVM |
| **STK unavailable fallback** | MockStkService (Python Friis + geometric LOS) | Must implement mock in C# |
| **Kafka topics** | Identical to D2 | Same four topics |

---

## 17. Option E — Python STK + CZML Bridge + Angular/CesiumJS (Electron now, Browser-ready)
[↑ Table of Contents](#table-of-contents)

> **Status: Open for team/customer discussion.** Option E is a hybrid that retains the best properties of both Option B and Option D2. All backend services and drs-bridge are **identical** to both options — only the computation output format and frontend rendering pipeline change.

---

### 17.1 The CZML Bridge — Key Concept
[↑ Table of Contents](#table-of-contents)

The fundamental problem with Option B's visualisation is that CesiumJS primitives (RLOS cones, coverage areas, access intervals) are **reconstructed approximations** built from stored `computed_links` values. They look similar but do not faithfully represent the physics STK computed.

Option D2 solves this by keeping STK's scenario object in memory and letting STK render natively — but this locks the frontend to WPF/ActiveX, which cannot run in a browser.

**CZML (Cesium Language)** is the bridge that resolves this tension:

- STK 12 can export any scenario — including all computed access intervals, RLOS results, coverage areas, sensor cone sweeps, and entity trajectories — as a **CZML file** (JSON-based, open standard)
- CesiumJS natively loads and renders CZML — it is CesiumJS's own animation format
- A CesiumJS viewer loaded with a CZML file renders **exactly what STK computed**, including physics-accurate sensor cones, link analysis overlays, and time-dynamic entity movement
- No primitive reconstruction. No approximation. No ActiveX controls.

This means:
- STK computation accuracy → preserved (Python `agi.stk12` runs the physics)
- STK visualisation fidelity → preserved (CZML carries the full scenario output to CesiumJS)
- Web portability → preserved (Angular + CesiumJS runs in any browser; Electron is just a shell)
- Angular developer → fully utilised
- WPF / C# frontend → not needed

---

### 17.2 Architecture
[↑ Table of Contents](#table-of-contents)

```
┌─────────────────────────────────────────────────────────┐
│  SCENARIO GENERATOR WORKSTATION                         │
│                                                         │
│  ┌─────────────────────────────────────────────────┐   │
│  │  Electron Desktop App (removable for browser)   │   │
│  │  Angular + CesiumJS                             │   │
│  │  Chromium runtime (bundled)                     │   │
│  │                                                 │   │
│  │  CesiumJS loads CZML ──────────────────────┐   │   │
│  │  ├── STK-accurate entity trajectories      │   │   │
│  │  ├── Physics-accurate RLOS cones           │   │   │
│  │  ├── Coverage areas                        │   │   │
│  │  ├── Access interval overlays              │   │   │
│  │  └── Time-dynamic animation (native)       │   │   │
│  │                                            │   │   │
│  │  REST + WebSocket ─────────────────────────┘   │   │
│  └────────┬──────────────────────┬─────────────────┘   │
│           │ HTTP/WebSocket        │ HTTP/WebSocket       │
│           ▼                      ▼                      │
│  ┌────────────────────────┐  ┌──────────────────────┐  │
│  │  sg-service            │  │  drs-server          │  │
│  │  Python/FastAPI        │  │  Python/FastAPI async│  │
│  │                        │  │                      │  │
│  │  Scenario CRUD         │  │  Kafka consumers     │  │
│  │  RBAC / Users          │  │  TimescaleDB writes  │  │
│  │  GIS serving           │  │  WebSocket push      │  │
│  │  PDF reports           │  │  Health status       │  │
│  │  DB backup             │  └──────────┬───────────┘  │
│  │                        │             │               │
│  │  STK computation  ─────┼─────────────────────────►  │
│  │  agi.stk12 COM         │   writes computed_links     │
│  │  ProcessPoolExecutor   │   to TimescaleDB            │
│  │                        │                             │
│  │  CZML export ──────────┼──────────────────────────►  │
│  │  STK → .czml file      │   served via               │
│  │  stored at             │   GET /exercises/{id}/czml  │
│  │  C:\EWTSS\czml\        │   Angular loads into        │
│  │                        │   CesiumJS viewer           │
│  └────────┬───────────────┘             │               │
│           └──────────────┬──────────────┘               │
│                          ▼                               │
│           ┌──────────────────────┐                      │
│           │  TimescaleDB         │                      │
│           │  PostgreSQL 16 +     │                      │
│           │  Timescale 2.x       │                      │
│           └──────────────────────┘                      │
│                                                         │
│  ┌──────────────────────────────┐                      │
│  │  Kafka (KRaft, single node)  │                      │
│  └──────────────────────────────┘                      │
└─────────────────────────────────────────────────────────┘
                        ▲
                        │ Kafka produce/consume
                        │
┌───────────────────────┴─────────────────────────────────┐
│  DRS WORKSTATION  (identical to Options B and D2)       │
│                                                         │
│  drs-bridge (Python + C++ shared libs)                  │
│  asyncio TCP servers ──► C++ parsers ──► Kafka produce  │
│                                                         │
│  TCP connections from DRS hardware instances            │
│  (RDFS, JV/UHF, JHF, SJRR, JLB, JMB, JHB×4,          │
│   AUS, PADS — up to 100 simultaneous)                  │
└─────────────────────────────────────────────────────────┘
```

---

### 17.3 What Changes vs Option B
[↑ Table of Contents](#table-of-contents)

| Concern | Option B | Option E |
|---|---|---|
| **STK computation** | Python `ProcessPoolExecutor` | Python `ProcessPoolExecutor` (identical) |
| **Computation output** | `computed_links` rows only | `computed_links` rows **+** CZML file export |
| **CZML export** | Not present | `StkComService` exports `.czml` after computation |
| **Visualisation engine** | CesiumJS — manual primitive reconstruction | CesiumJS — loads CZML directly |
| **RLOS cone accuracy** | Geometric approximation | Physics-accurate (CZML carries STK output) |
| **Timeline scrubbing** | Angular REST calls per tick → CesiumJS rebuild | CesiumJS native timeline from CZML animation |
| **Frontend framework** | Angular (identical) | Angular (identical) |
| **Desktop shell** | Electron (identical) | Electron (identical) |
| **Future browser path** | Remove Electron shell | Remove Electron shell (identical path) |
| **MockStkService** | Generates `computed_links`, no CZML | Generates `computed_links` + synthetic CZML |
| **STK unavailable fallback** | Full mock via Python | Full mock via Python (identical) |

---

### 17.4 What Changes vs Option D2
[↑ Table of Contents](#table-of-contents)

| Concern | Option D2 | Option E |
|---|---|---|
| **STK computation** | C# in-process (WPF) | Python `ProcessPoolExecutor` |
| **Frontend** | C# WPF + ActiveX STK globe | Angular + CesiumJS (Electron) |
| **Visualisation accuracy** | STK native ActiveX rendering | CZML-driven CesiumJS (STK-accurate, not native) |
| **Browser portability** | Not possible (WPF is Windows-only) | Yes — remove Electron shell |
| **Angular developer utility** | Reduced / redeployment needed | Full ownership |
| **C# developer scope** | High (WPF + STK computation) | Low (C++ parsers only — same as Option B) |
| **STK mock fallback** | Must implement in C# | Python MockStkService (already designed) |

---

### 17.5 CZML Export — How it Works
[↑ Table of Contents](#table-of-contents)

After STK computation completes, `StkComService` exports the scenario as CZML using STK's `ExportCZML` Connect Command:

```python
# sg-service/stk_com_service.py
def export_czml(self, exercise_id: str, output_path: str) -> None:
    out = Path(output_path)
    out.parent.mkdir(parents=True, exist_ok=True)
    sc_name  = self._root.CurrentScenario.InstanceName
    abs_path = str(out.resolve()).replace('\\', '/')
    model_base = "http://localhost:8001/models/"
    cmd = f'ExportCZML */Scenario/{sc_name} "{abs_path}" {model_base}'
    self._root.ExecuteCommand(cmd)
```

**CZML Export Plugin — required installation step:**

The `ExportCZML` Connect Command is provided by an optional STK plugin, not bundled in the base install.  Install it once per STK instance:

1. Open STK 12
2. Go to **Utilities → Plugin Scripts…**
3. Click **Add** and browse to:
   ```
   C:\Program Files\AGI\STK 12\bin\Plugins\CZMLExport\CZMLExport.pl
   ```
4. Enable the plugin and click **OK**

> Without the plugin, `ExecuteCommand("ExportCZML …")` raises `Command has failed`.
> The plugin must be installed in every STK Desktop / Engine instance used by sg-service.

STK Engine must also be started with `noGraphics=False` to enable the graphics stack that the plugin requires — headless mode (`noGraphics=True`) does not support CZML export.

The `model_base` URL (`http://localhost:8001/models/`) points to the sg-service's `/models/{filename}` route, which recursively searches the local STK model tree (`STKData/VO/Models/`).  This keeps the deployment fully air-gapped — no outbound calls to `assets.agi.com`.

Angular loads CZML directly into CesiumJS:

```typescript
// components/map/scenario-map.component.ts
async loadScenarioCzml(exerciseId: string): Promise<void> {
  // Fetch CZML from sg-service
  const czmlUrl = `/exercises/${exerciseId}/czml`;

  // CesiumJS natively parses and animates CZML
  const dataSource = await CzmlDataSource.load(czmlUrl);
  this.viewer.dataSources.add(dataSource);

  // STK-computed entity trajectories, RLOS cones, and access
  // intervals all render automatically — no manual primitives
  this.viewer.zoomTo(dataSource);
}
```

The CZML file contains everything STK computed: entity positions at every tick, sensor cone geometry, access interval shading, link budget colour-coding. CesiumJS renders this with its own physics-accurate terrain engine using the same DTED data.

**MockStkService** generates a synthetic CZML from the `computed_links` rows for development without an STK license:

```python
# sg-service/services/stk_mock_service.py
def export_czml(self, exercise_id: str, output_path: str) -> None:
    """Generate synthetic CZML from computed_links for dev/offline use."""
    links = self.db.query(ComputedLink).filter_by(exercise_id=exercise_id).all()
    czml = build_czml_from_links(links)   # positions + simplified cones
    with open(output_path, 'w') as f:
        json.dump(czml, f)
```

---

### 17.6 Browser Portability Path
[↑ Table of Contents](#table-of-contents)

Option E is designed to be browser-deployable with a single change:

```
Today (Electron desktop):
  ewtss-sg.exe
    └── Chromium (bundled)
          └── Angular app
                └── CesiumJS ← loads CZML from localhost

Future (browser tab):
  Chrome / Edge (system browser)
    └── Angular app (same build, zero changes)
          └── CesiumJS ← loads CZML from http://sg-workstation-ip:8000
```

The only change is pointing the Angular API base URL from `localhost` to the SG workstation IP. The Angular app, CesiumJS, CZML loading, and WebSocket subscriptions are all identical. No code changes required for the frontend.

Electron is removed from the deployment. The DVD install no longer needs the Electron MSI — a single `npm run build` output served by a lightweight static file server (or by sg-service's existing static file endpoint) replaces it.

---

### 17.7 Operating Modes (Option E)
[↑ Table of Contents](#table-of-contents)

The operating mode flows are **identical to Option B** (Section 16) with one replacement in Scenario modes:

**In Planning Phase (Standalone + Scenario and Integrated + Scenario):**

Replace Step 6 of Section 16.3:

| Step | Option B | Option E |
|---|---|---|
| After STK computation completes | Angular fetches `computed_links` rows per tick → manually builds CesiumJS primitives | sg-service exports CZML file; Angular calls `GET /exercises/{id}/czml` → `CzmlDataSource.load()` → CesiumJS renders natively |
| Timeline scrubbing | Angular REST call per tick to fetch positions | CesiumJS native timeline control driven by CZML |
| RLOS cone rendering | CesiumJS corridor primitive (approximate) | CZML geometry from STK export (physics-accurate) |

Everything else — Random modes, Integrated TCP delivery to entity apps, Kafka topics, drs-server writes, WebSocket streams — is identical to Option B.

---

### 17.8 Full Options Comparison — B vs D2 vs E
[↑ Table of Contents](#table-of-contents)

| Concern | Option B | Option D2 | Option E |
|---|---|---|---|
| **STK computation** | Python (ProcessPoolExecutor) | C# (in-process) | Python (ProcessPoolExecutor) |
| **Visualisation** | CesiumJS (approximate reconstruction) | STK ActiveX native | CesiumJS via CZML (STK-accurate) |
| **RLOS / coverage accuracy** | Approximate | Exact | Exact (CZML carries STK output) |
| **Timeline scrubbing** | Angular REST per tick | STK native control | CesiumJS native (CZML-driven) |
| **Frontend framework** | Angular | WPF / XAML | Angular |
| **Desktop shell** | Electron | Native WPF exe | Electron |
| **Browser portability** | Yes (remove Electron) | No (WPF only) | Yes (remove Electron) |
| **Angular developer utility** | Full | Reduced / redeployment | Full |
| **C# developer scope** | Low (parsers only) | High (WPF + STK) | Low (parsers only) |
| **STK mock fallback** | Python MockStkService | Must implement in C# | Python MockStkService + synthetic CZML |
| **Installer size** | ~200MB (Chromium) | ~20MB | ~200MB (Chromium) |
| **Build toolchain** | Node.js + .NET SDK (STK) | .NET SDK only | Node.js + .NET SDK (STK) |
| **Air-gap vendoring** | npm + pip | NuGet + pip | npm + pip (identical to B) |
| **New concept to learn** | None for team | WPF/XAML for Angular dev | CZML format (well-documented, JSON) |
| **Implementation risk** | Low | Medium-High | Low-Medium |
| **Best for** | Team balance, future flexibility | STK fidelity, C# heavy team | STK fidelity + future flexibility |

---

### 17.9 Discussion Points for Team/Customer Review
[↑ Table of Contents](#table-of-contents)

1. **CZML fidelity vs ActiveX fidelity:** CZML carries everything STK computed and CesiumJS renders it faithfully. The only difference from D2 is that D2 uses STK's own OpenGL renderer (via ActiveX) while E uses CesiumJS's WebGL renderer driven by the same data. For training purposes, is this distinction meaningful to the customer?

2. **CZML export completeness:** STK 12's CZML export covers entity trajectories, access intervals, and sensor cones. Verify with the STK license that all required overlay types (RLOS shading, coverage grids, link budget colour maps) are included in the export. If any are missing, they fall back to CesiumJS approximation.

3. **CZML file size and streaming strategy:** A 1-hour exercise at 1-second tick resolution with 20 entities and 50 emitters produces a CZML file in the range of 50–200 MB. `CzmlDataSource.load(url)` (the naive approach) fetches and parses the entire file before rendering begins — unacceptable at that size. The correct approach for the main app is **SSE-streamed CZML**:

   - **Backend:** sg-service exposes a `GET /exercises/{id}/czml/stream` endpoint that writes CZML packets to a Server-Sent Events stream as each entity's trajectory is sampled. The document packet is sent first (establishes the clock), followed by one packet per entity, then LOS/RLOS polyline packets.
   - **Frontend:** Angular creates a `CzmlDataSource`, adds it to the Cesium viewer, then opens an `EventSource` connection to the stream endpoint. Each SSE message is fed to `dataSource.process(packet)`. Cesium renders each entity as its packet arrives — the globe is live within ~1 second of the stream opening.
   - **Result:** First render latency is decoupled from total file size. A 200 MB scenario starts displaying in under 2 seconds on a LAN; the full dataset populates progressively.
   - **Fallback:** The static `GET /exercises/{id}/czml` file endpoint is retained for STK validation tooling, offline export, and scenarios small enough that full-file load is acceptable (< ~5 MB, typically < 10 entities at 60 s step).

4. **STK Engine confirmed as the headless deployment target.** `STKEngine` (not `STKDesktop`) is the correct class for server/deployed use. The two application types have different root acquisition APIs — this must be handled explicitly in `StkComService.__init__` (see Section 5.2). STK Engine allows exactly one instance per Python process; `get_stk_service()` must be a singleton (see Section 5.2).

5. **Access interval API — DataProviders, not `ComputedAccessIntervals`.** The Python `agi.stk12` binding does **not** expose `ComputedAccessIntervals` as a direct property on the access object (`AgStkAccess`). Attempting to read it raises `AttributeError`. The correct pattern after `access.ComputeAccess()` is:

   ```python
   dp      = access.DataProviders.GetDataPrvIntervalFromPath('Access')
   result  = dp.Exec(start_time, stop_time)
   starts  = result.DataSets.GetDataSetByName('Start Time').GetValues()
   stops   = result.DataSets.GetDataSetByName('Stop Time').GetValues()
   ```

   This is consistent with the DataProviders pattern already used for LLA trajectory sampling.

6. **Access computation geometry — platform-to-platform, not sensor-to-platform.** `sensor.GetAccessToObject(platform)` only fires intervals when the target platform falls inside the sensor's field of view. A nadir-pointing sensor on an aircraft at 2 km will never illuminate a target at 5 km altitude (above it). For geometric line-of-sight between two airborne platforms (LOS/RLOS), use `platform.GetAccessToObject(other_platform)` — this computes pure ellipsoid (or terrain) occlusion regardless of sensor orientation.

7. **STK license on development machines:** Confirmed available. Both Option B and Option E require `agi.stk12` on the Python development machine to run `StkComService`. MockStkService covers development without STK.

5. **Browser deployment timeline:** If there is a concrete future requirement for browser-based access (e.g., a control centre workstation that cannot install Electron), Option E is the right choice now. If that requirement is speculative, Option B is simpler and Option E can be migrated to later by adding the CZML export step.

---

## 18. STK Licensing — Deployment Workstation Risk
[↑ Table of Contents](#table-of-contents)

> **Development licenses are confirmed available (3+ STK development licenses, separate from deliverable).** The risk documented in this section is specific to the **client's deployment workstations** — the two perpetual Runtime Engine licenses that ship as part of the Milestone 2 deliverable.

---

### 18.1 License Tiers Relevant to This Project
[↑ Table of Contents](#table-of-contents)

| License | Held by | What it permits |
|---|---|---|
| **STK Professional / development licenses (3+)** | Vendor team — NOT part of deliverable | Full programmatic COM API, scenario creation, all analysis, `agi.stk12` Python automation, CZML export. Covers all development and testing of Options B, D2, and E. |
| **STK Runtime Engine** (perpetual, Qty 2 — deliverable per RFQ A2 Item 5) | Client deployment workstations | Load and display pre-built `.sc` scenario files; embed STK ActiveX globe controls. Programmatic computation capability **not guaranteed** — depends on whether Components is bundled. |
| **STK Components** | Separate purchase — not specified in RFQ | Full programmatic COM API on deployment machines. Required for Options B, D2, and E to perform computation at the client site. |

**Development is fully unblocked.** All computation code for Options B, D2, and E can be built, tested, and demonstrated using the vendor team's existing development licenses. The open question is solely about what the **client** can run after delivery.

---

### 18.2 The Deployment-Time Risk
[↑ Table of Contents](#table-of-contents)

The RFQ specifies STK Runtime Engine (Qty 2, perpetual) as the deliverable. If these licenses do not include STK Components:

- Client workstations can **load and display** pre-built `.sc` scenario files
- Client workstations **cannot** programmatically create scenarios or run access/link/RLOS/coverage analysis
- Options B, D2, and E would require the operator to compute exercises on a vendor-licensed machine, export results, and transfer them to the client workstation — defeating the purpose of an integrated system

**Option D1 is the only option unaffected:** computation runs on the development-licensed machine, the `.sc` file is transferred to the client workstation, and the Runtime Engine displays it. The Qty 2 perpetual licenses fully cover this workflow.

---

### 18.3 Capability Matrix — Development vs Deployment
[↑ Table of Contents](#table-of-contents)

| Capability | Dev machine (vendor licenses) | Client workstation (Runtime Engine) |
|---|---|---|
| Embed STK ActiveX globe in WPF (D1/D2) | ✓ | ✓ |
| Load pre-built `.sc` file and display (D1) | ✓ | ✓ |
| Create STK scenario programmatically (B, D2, E) | ✓ | **⚠️ Unconfirmed** |
| Run access/link/RLOS/coverage analysis (B, D2, E) | ✓ | **⚠️ Unconfirmed** |
| `agi.stk12` Python COM automation (B, E) | ✓ | **⚠️ Unconfirmed** |
| C# `IAgStkObjectRoot` COM automation (D2) | ✓ | **⚠️ Unconfirmed** |
| CZML export with computed results (E) | ✓ | **⚠️ Unconfirmed** |

---

### 18.4 Questions to Verify with Ansys/AGI
[↑ Table of Contents](#table-of-contents)

Only the deployment-side questions remain open:

1. **Do the perpetual STK Runtime Engine licenses (Qty 2) include STK Components for programmatic scenario creation and analysis?**
   This is the single most important question. A yes clears the path for Options B, D2, and E on client workstations.

2. **If not bundled, what is the cost of adding STK Components to the two perpetual deployment licenses?**
   This becomes a budget line item in the proposal if Components must be purchased separately for the Qty 2 client licenses.

3. **Is CZML export available via the Runtime Engine COM API?**
   If a scenario is already loaded (computed on a dev machine and transferred), can the Runtime Engine export it to CZML? If yes, Option E may work on client workstations even with Runtime Engine only — the operator recomputes on a dev machine, transfers the CZML, and the client workstation loads it in CesiumJS.

4. **Is STK Engine (headless) a lower-cost deployment alternative to STK Components?**
   **Confirmed via MVP integration.** STK Desktop Engine is the correct deployment target — it runs headless (no GUI), supports the full `agi.stk12` COM API for scenario creation and access computation, and is started via `STKEngine.StartApplication()`. It is a separate SKU from STK Desktop and from STK Components. Verify pricing with Ansys/AGI for the two client workstation licenses. Key API differences from STK Desktop that must be handled in `StkComService` (already implemented in MVP):
   - Root acquisition: `engine.NewObjectRoot()` instead of `desktop.Root`
   - One instance per Python process (singleton pattern required — see Section 5.2)
   - `ProcessPoolExecutor(max_workers=1)` recommended for crash isolation (see Section 21.5)

---

### 18.5 Architecture Risk by License Scenario (Deployment Only)
[↑ Table of Contents](#table-of-contents)

| Scenario | Option B | Option D1 | Option D2 | Option E |
|---|---|---|---|---|
| **Runtime Engine only (Components not bundled)** | ✗ No on-site computation | ✓ Fully covered | ✗ No on-site computation | ✗ No on-site computation |
| **Runtime Engine + Components confirmed** | ✓ Fully covered | ✓ Fully covered | ✓ Fully covered | ✓ Fully covered |
| **CZML export available in Runtime Engine** | Not applicable | Not applicable | Not applicable | ✓ Pre-generated CZML workflow viable |
| **MockStkService fallback (dev/offline)** | ✓ Python Friis + geometric | N/A | ✗ Needs C# mock | ✓ Python Friis + synthetic CZML |

---

### 18.6 Option B and Option E Are Licensing-Equivalent
[↑ Table of Contents](#table-of-contents)

**Option B and Option E require exactly the same STK license** — both use Python `agi.stk12` COM automation. Option E adds one extra step after computation (CZML export via the same COM API) that requires no additional license tier beyond what Option B already needs.

```
Option B:  agi.stk12 runs analysis → writes computed_links to TimescaleDB
Option E:  agi.stk12 runs analysis → writes computed_links to TimescaleDB
                                    → exports CZML via STK COM command
                                      (same license, no additional tier)
```

Consequences:

- **The vendor team's existing development licenses cover Option E in full** — confirmed working, same as Option B.
- **The deployment-side question (Runtime Engine ± Components) is identical for both.** Resolving it for Option B automatically resolves it for Option E and vice versa.
- **Option E is strictly better than Option B** for the same licensing cost: it adds physics-accurate CesiumJS visualisation via CZML and browser portability at zero additional license expenditure. The only additional development effort is wiring in the CZML export step and the `CzmlDataSource.load()` call in Angular.

---

### 18.7 Recommendation
[↑ Table of Contents](#table-of-contents)

**Option E is the recommended architecture.**

It delivers everything Option B delivers, plus STK-accurate visualisation (no CesiumJS primitive approximation) and a clear browser deployment path — at identical licensing cost and identical deployment-side risk.

| Decision point | Recommendation |
|---|---|
| **Architecture choice today** | Option E — strictly better than B at same license cost |
| **Development** | Proceed immediately — vendor licenses confirmed sufficient |
| **Deployment license verification** | Verify Runtime Engine ± Components with Ansys/AGI before Milestone 2 delivery. Answer applies equally to both B and E. |
| **If Components confirmed in Runtime Engine** | Option E fully covered on client workstations — no further action |
| **If Components not bundled** | Add to Qty 2 deployment licenses (budget impact to assess) OR fall back to Option D1 for STK-accurate display without deployment-time computation |
| **Fallback if STK unavailable at runtime** | MockStkService (Python Friis + synthetic CZML) — covers development and any deployment without STK license |

---

## 19. ICD Protocol Mapping — COMM DF Receiver Interface
[↑ Table of Contents](#table-of-contents)

This section documents how the COMM DF Receiver ICD maps to the drs-bridge C++ parser interface defined in Section 7. The ICD describes the TCP/IP interface between the SDFC (scenario controller) and the DRS hardware unit.

> **Source:** ICD_EWTSS (example ICD for COMM DF Receiver). This section uses it as the reference protocol for drs-bridge parser implementation. Other hardware variants (RDFS, JV/UHF, JHF, etc.) will have their own ICDs; the framing pattern documented here is expected to be consistent.

---

### 19.1 Transport Layer
[↑ Table of Contents](#table-of-contents)

| Parameter | Value |
|---|---|
| Protocol | TCP/IP |
| Bandwidth | 1 Gbps |
| Direction | Full-duplex: SDFC ↔ DRS |
| Frame format | Fixed header + variable payload + fixed footer |
| Max payload | 1 MB per message |

There are two distinct frame formats on the wire: the **SDFC↔DRS format** (command/response with 4-byte headers) and the **SCD↔DRS format** (compact 2-byte header, used for direct controller communication). Both must be handled by the C++ parser.

---

### 19.2 Packet Framing — SDFC↔DRS
[↑ Table of Contents](#table-of-contents)

#### Command packet (SDFC → DRS)

```
┌────────────────┬──────────────┬────────────────┬────────────────┬───────────────────┬────────────────┐
│ Header         │ Message Size │ Command        │ Command        │ Message Data      │ Footer         │
│ 4 bytes        │ 4 bytes      │ Group ID       │ Unit ID        │ 0 – 1,048,576 B   │ 4 bytes        │
│ 0xAA/AB/BA/BB  │ uint32 LE    │ 2 bytes ushort │ 2 bytes ushort │                   │ 0xCC/CD/DC/DD  │
└────────────────┴──────────────┴────────────────┴────────────────┴───────────────────┴────────────────┘
```

Header magic bytes: `0xAA 0xAB 0xBA 0xBB`
Footer magic bytes: `0xCC 0xCD 0xDC 0xDD`
Total fixed overhead: **16 bytes** (4 header + 4 size + 2 group + 2 unit + 4 footer)

#### Response packet (DRS → SDFC)

```
┌────────────────┬────────────────┬──────────────┬─────────────────┬─────────────────┬───────────────────┬────────────────┐
│ Header         │ Message Status │ Message Size │ Response        │ Response        │ Message Data      │ Footer         │
│ 4 bytes        │ 2 bytes short  │ 4 bytes      │ Group ID        │ Unit ID         │ 0 – 1,048,576 B   │ 4 bytes        │
│ 0xEE/EF/FE/FF  │ int16 LE       │ uint32 LE    │ 2 bytes ushort  │ 2 bytes ushort  │                   │ 0xFF/FE/EF/EE  │
└────────────────┴────────────────┴──────────────┴─────────────────┴─────────────────┴───────────────────┴────────────────┘
```

Header magic bytes: `0xEE 0xEF 0xFE 0xFF`
Footer magic bytes: `0xFF 0xFE 0xEF 0xEE`
Total fixed overhead: **18 bytes** (4 header + 2 status + 4 size + 2 group + 2 unit + 4 footer)

---

### 19.3 Packet Framing — SCD↔DRS
[↑ Table of Contents](#table-of-contents)

Used for direct controller-to-DRS messaging (compact format):

```
┌───────────────┬──────────────────┬────────────────────┬──────────────────────┬───────────────────┬──────────────┐
│ Header        │ Command Code     │ Sequence Number    │ Message Data Length  │ Message Data      │ Footer       │
│ 2 bytes       │ 2 bytes ushort   │ 2 bytes ushort     │ 4 bytes uint         │ 0 – 1,048,576 B   │ 2 bytes      │
│ 0xAA 0xAA     │                  │                    │                      │                   │ 0xEE 0xEE    │
└───────────────┴──────────────────┴────────────────────┴──────────────────────┴───────────────────┴──────────────┘
```

Fixed overhead: **12 bytes** (2 header + 2 code + 2 seq + 4 length + 2 footer)

---

### 19.4 Command Groups and IDs
[↑ Table of Contents](#table-of-contents)

The ICD organises commands into numbered groups. Each group has a Group ID embedded in the packet; the Command/Response Unit IDs distinguish individual operations within the group.

#### Group 0 — System Management (Group ID: 100)

| Operation | Command Unit ID | Response Unit ID | Response Payload |
|---|---|---|---|
| Get System Version | 1 | 2 | 26 bytes: FW version, Driver version, FPGA version, BSP version, Processor ID, RF Tuner ID ×3, FPGA Type ID |
| PBIT Status | 5 | 6 | TBD |
| Module Health Status | 31 | — | Per-module health bitmask |

System Version response layout (26 bytes):

```
Offset  Size  Field
0       4     Firmware Version (uint32)
4       4     Driver Version (uint32)
8       4     FPGA Version (uint32)
12      4     BSP Version (uint32)
16      2     Processor ID (uint16)
18      2     RF Tuner ID [0] (uint16)
20      2     RF Tuner ID [1] (uint16)
22      2     RF Tuner ID [2] (uint16)
24      2     FPGA Type ID (uint16)
```

#### Group 1 — RF Measurement and Detection (Group ID: 101)

| Operation | Command Unit ID | Response Unit ID | Response Payload |
|---|---|---|---|
| Set Threshold | 25 | — | ACK only |
| Set Resolution | 27 | — | ACK only |
| Set Min/Max Pulse Range | 47 | — | ACK only |
| Configure Detection | 37 | — | ACK only |
| Get FFT Data | 43 | 44 | (1600 × 4) + 4 bytes = 6,404 bytes (1600 bins × float32 + 4-byte header) |
| Start FH Detection | 39 | 40 | 2 + 2 + (64 × hopper_count) bytes |
| Start FF Detection | 69 | 70 | TBD (fixed-frequency detection result) |
| Start Burst Detection | 83 | 84 | 4 + (52 × burst_count) bytes |

#### Group 11 — BITE (Built-In Test Equipment) (Group ID: 111)

| Operation | Command Unit ID | Response Unit ID | Response Payload |
|---|---|---|---|
| BITE Enable | 1 | — | ACK only |
| BITE Disable | 1 | — | ACK only |
| Signal BITE Test | 3 | 4 | 12 bytes |
| All Channel BITE Test | — | — | TBD |

---

### 19.5 RF Configuration Parameters
[↑ Table of Contents](#table-of-contents)

The ICD defines four RF station types with distinct frequency plans:

| RF Station | Frequency Range | Channel Count | Channel Spacing | Max Hop Rate |
|---|---|---|---|---|
| VHF CNR | 30–88 MHz | 2,320 | 25 kHz | 250 hops/sec |
| Bluetooth | 2.402–2.480 GHz | 79 | 1 MHz | 1,600 hops/sec |
| Motorola | 902–928 MHz | 50 | 0.5 MHz | 11 hops/sec |
| SDR | 30–512 MHz | — | — | 500 hops/sec |

These parameters inform the `Set Resolution`, `Set Threshold`, and `Set Min/Max Pulse Range` command payloads. The C++ parser must validate frequency values against the RF station type's declared range.

---

### 19.6 FF Detection Command Sequence
[↑ Table of Contents](#table-of-contents)

The ICD defines a mandatory sequence for Fixed Frequency detection (Sheet1 of ICD):

```
1. Set Threshold        (Group 1, CmdID 25)
2. Set Resolution       (Group 1, CmdID 27)
3. Configure Detection  (Group 1, CmdID 37)
4. BITE Enable          (Group 11, CmdID 1)
5. Reference measurement
6. Start FF Detection   (Group 1, CmdID 69) → Response (RespID 70)
```

The asyncio TCP server (Section 7.4) handles each command atomically — the parser will emit one JSON event per parsed response. The sg-service is responsible for sequencing these commands in order; drs-bridge is stateless with respect to command ordering.

---

### 19.7 C++ Parser Implementation Notes
[↑ Table of Contents](#table-of-contents)

The framing constants defined in Section 19.2 map directly to the `extract_frame` function in the Section 7.3 parser API:

```cpp
// parsers/comm_df/frame_constants.h

// SDFC→DRS command frame markers
static constexpr uint8_t CMD_HEADER[4] = {0xAA, 0xAB, 0xBA, 0xBB};
static constexpr uint8_t CMD_FOOTER[4] = {0xCC, 0xCD, 0xDC, 0xDD};

// DRS→SDFC response frame markers
static constexpr uint8_t RESP_HEADER[4] = {0xEE, 0xEF, 0xFE, 0xFF};
static constexpr uint8_t RESP_FOOTER[4] = {0xFF, 0xFE, 0xEF, 0xEE};

// SCD↔DRS compact frame markers
static constexpr uint8_t SCD_HEADER[2]  = {0xAA, 0xAA};
static constexpr uint8_t SCD_FOOTER[2]  = {0xEE, 0xEE};

// Fixed overhead sizes
static constexpr int CMD_FRAME_OVERHEAD  = 16;   // 4+4+2+2+4
static constexpr int RESP_FRAME_OVERHEAD = 18;   // 4+2+4+2+2+4
static constexpr int SCD_FRAME_OVERHEAD  = 12;   // 2+2+2+4+2
static constexpr int MAX_PAYLOAD_BYTES   = 1048576;
```

`extract_frame` logic:

1. Scan buffer for any of the three header magic sequences.
2. Once header found, read the `Message Size` field at the appropriate offset.
3. Validate size ≤ `MAX_PAYLOAD_BYTES`.
4. Check that the buffer holds at least `overhead + size` bytes.
5. Verify footer magic at `overhead + size - footer_len`.
6. Return the complete frame; advance buffer past it.

`parse_message` logic per group:

```cpp
const char* parse_message(const uint8_t* frame, int frame_len) {
    FrameHeader hdr = decode_header(frame);
    JsonWriter w;
    w.set("group_id",    hdr.group_id);
    w.set("unit_id",     hdr.unit_id);
    w.set("frame_type",  hdr.is_response ? "response" : "command");
    if (hdr.is_response) w.set("status", hdr.status);

    switch (hdr.group_id) {
        case 100: parse_group0(frame + hdr.payload_offset, hdr.payload_len, w); break;
        case 101: parse_group1(frame + hdr.payload_offset, hdr.payload_len, w); break;
        case 111: parse_group11(frame + hdr.payload_offset, hdr.payload_len, w); break;
        default:  w.set("raw_hex", to_hex(frame + hdr.payload_offset, hdr.payload_len));
    }
    return w.release();  // caller calls free_result()
}
```

Example JSON output for a Group 0 System Version response:

```json
{
  "group_id": 100,
  "unit_id": 2,
  "frame_type": "response",
  "status": 0,
  "fw_version": "2.4.1",
  "driver_version": "1.0.3",
  "fpga_version": "3.2.0",
  "bsp_version": "1.1.0",
  "processor_id": 7,
  "rf_tuner_ids": [1, 2, 3],
  "fpga_type_id": 4
}
```

Example JSON output for a Group 1 FH Detection response (3 hoppers):

```json
{
  "group_id": 101,
  "unit_id": 40,
  "frame_type": "response",
  "status": 0,
  "hopper_count": 3,
  "hoppers": [
    {"channel": 14, "frequency_hz": 48350000, "power_dbm": -61.2, "dwell_us": 4000},
    {"channel": 27, "frequency_hz": 56750000, "power_dbm": -58.8, "dwell_us": 3900},
    {"channel": 41, "frequency_hz": 65150000, "power_dbm": -63.1, "dwell_us": 4100}
  ]
}
```

Example JSON output for a Group 1 Burst Detection response (2 bursts):

```json
{
  "group_id": 101,
  "unit_id": 84,
  "frame_type": "response",
  "status": 0,
  "burst_count": 2,
  "bursts": [
    {"start_us": 1712394820100000, "duration_us": 12400, "frequency_hz": 34200000, "power_dbm": -55.4, "bandwidth_hz": 25000},
    {"start_us": 1712394820150000, "duration_us": 9800,  "frequency_hz": 34225000, "power_dbm": -57.1, "bandwidth_hz": 25000}
  ]
}
```

---

### 19.8 Hardware Profile YAML for COMM DF Receiver
[↑ Table of Contents](#table-of-contents)

Using the framing constants and RF parameters from the ICD, the hardware profile for the COMM DF Receiver variant is:

```yaml
name: comm_df
port: 5490
kafka_topic: comm_df.drs.ui
kafka_broker: "${KAFKA_BROKER}"
parser_lib: parsers/libcomm_df.dll
max_connections: 4
health_interval_ms: 1000
receive_buffer_bytes: 65536        # large buffer: FFT response is 6,404 bytes per call
frame_terminator: "magic_bytes"    # framing via header/footer magic, not newlines
protocol_version: "ICD-COMM-DF-v1"
group_ids: [100, 101, 111]
```

`receive_buffer_bytes` is set to 65536 (64 KB) rather than the 8192 default because the FFT Data response (Group 1, CmdID 44) is 6,404 bytes per call and Burst Detection payloads scale with burst count.

---

### 19.9 Kafka Topic Schema
[↑ Table of Contents](#table-of-contents)

Messages produced by the `comm_df` drs-bridge parser map to the following Kafka topics consumed by drs-server:

| Topic | Source group | Message types | TimescaleDB target |
|---|---|---|---|
| `comm_df.drs.ui` | 101 | FF, FH, Burst detection results | `measurements` hypertable |
| `comm_df.system.version` | 100 | System version response | `system_versions` table |
| `comm_df.system.health` | 100 (CmdID 31) | Module health status | `health_status` hypertable |
| `comm_df.bite.result` | 111 | Signal/All channel BITE results | `system_logs` hypertable |

This topic naming follows the `<hardware_type>.<domain>.<subtype>` convention established for all hardware variants.

---

### 19.10 Mapping to TimescaleDB Schema
[↑ Table of Contents](#table-of-contents)

ICD response fields map to the `measurements` hypertable columns defined in Section 5:

| ICD field | measurements column | Notes |
|---|---|---|
| `recorded_at` (bridge timestamp) | `recorded_at` | Partition key; bridge adds wall-clock timestamp on receive |
| `group_id` / `unit_id` | `message_type` | Encoded as `"G{group_id}_U{unit_id}"` string for query convenience |
| `session_id` (context from sg-service) | `session_id` | Injected by drs-server consumer from active session context |
| `hardware_type` = `"comm_df"` | `hardware_type` | Constant per bridge instance |
| `hopper_count`, hopper array | JSONB `payload` column | Full FH record stored in JSONB; scalar `frequency_hz` duplicated to `measurement_scalars` |
| `burst_count`, burst array | JSONB `payload` column | Full burst array in JSONB; scalar `power_dbm` duplicated to `measurement_scalars` |
| FFT 1600-bin float32 array | JSONB `payload` column | Stored as JSON array; large — consider separate `fft_data` hypertable if query patterns warrant |
| `fw_version`, `driver_version`, etc. | `system_versions` table | Flat columns; inserted once per session startup |

> **FFT data volume note:** At 1600 bins × float32 × 20 Hz = ~128 KB/sec per COMM DF instance. At 100 instances this is ~12.8 MB/sec of FFT data alone. If FFT data is not needed for post-session analysis, add a `store_fft: false` flag to the hardware profile YAML to drop these messages at the drs-bridge level before Kafka production.

---

## 20. ICD Code Generator — `tools/icd_codegen`
[↑ Table of Contents](#table-of-contents)

The existing system hand-transcribes ICD documents into `command.csv` + `structure.csv` files (see Section 7.7). With 12+ hardware variants, each requiring its own ICD-derived parser, a code generator that reads the ICD Excel directly and produces C++ skeletons, YAML profiles, and TypeScript types is a significant force-multiplier for the C++ developer.

> **Scope:** The generator writes *skeletons* — constants, frame detection, group/command dispatch structure. The field-level decode bodies (bit-packing, enumeration handling, conditional layouts) require developer judgment and remain hand-written. Generated code is meant to be edited; it is not regenerated after the developer fills it in.

---

### 20.1 Tool Structure
[↑ Table of Contents](#table-of-contents)

```
tools/icd_codegen/
  icd_codegen.py              ← CLI entry point
  excel_reader.py             ← extracts protocol facts from ICD Excel sheets
  generators/
    cpp_parser.py             ← produces {hw}_parser.cpp + {hw}_frame_types.h
    yaml_profile.py           ← produces profiles/{hw}.yaml
    typescript_types.py       ← produces {hw}.types.ts
  templates/
    parser.cpp.j2             ← Jinja2 template: frame detect + group dispatch skeleton
    frame_types.h.j2          ← magic byte constants
    profile.yaml.j2
    types.ts.j2
  tests/
    test_excel_reader.py      ← tests against the COMM DF ICD as reference fixture
    fixtures/
      ICD_COMM_DF.xlsx        ← reference ICD for tests (copy of the example file)
  requirements.txt            ← openpyxl>=3.1, jinja2>=3.1 (both offline-vendorable)
```

---

### 20.2 CLI Usage
[↑ Table of Contents](#table-of-contents)

```bash
python tools/icd_codegen/icd_codegen.py \
  --icd     "ICD_EWTSS.xlsx"            \
  --hw      comm_df                     \
  --port    5490                        \
  --out-parsers  drs-bridge/parsers/src/     \
  --out-profiles drs-bridge/profiles/        \
  --out-types    frontend/src/app/models/
```

Optional flags:
```
--sheet-map '{"Command-Response Structures": "Frame Format", "Group 1 Commands": "RF Commands"}'
            Override expected sheet names when an ICD uses non-standard naming.

--protocols sdfc_drs,scd_drs
            Declare which frame formats this device uses (defaults to sdfc_drs only).

--max-payload 1048576
            Override max payload bytes (default 1 MB from ICD).

--dry-run   Print what would be generated without writing files.
```

---

### 20.3 Expected ICD Excel Sheet Conventions
[↑ Table of Contents](#table-of-contents)

The generator relies on these sheet names and column headers. When a new ICD arrives, verify these match before running:

| Sheet name | Columns used | What is extracted |
|---|---|---|
| `Command-Response Structures` | Header, Footer, Size field offset | Frame format table: magic byte sequences, field widths, overhead sizes |
| `Group N Commands` (one per group) | Group ID, Cmd ID, Resp ID, Payload size, Field name, Type, Size | Group + command/response unit IDs; payload byte layouts |
| `All_commands` | Command name, Group, Cmd ID, Field name, Type, Min, Max | Human-readable command names → semantic constant identifiers |
| `RF Config and Params` | Station, Freq range, Channels, Spacing, Max hop rate | `receive_buffer_bytes` sizing guidance |

If a sheet is missing or columns differ, use `--sheet-map` to remap. The tool reports unrecognised or missing sheets clearly rather than silently skipping them.

---

### 20.4 `excel_reader.py` — What It Extracts
[↑ Table of Contents](#table-of-contents)

```python
@dataclass
class FrameFormat:
    name: str                    # "sdfc_cmd", "sdfc_resp", "scd"
    header_bytes: bytes          # e.g. b"\xaa\xab\xba\xbb"
    footer_bytes: bytes
    size_field_offset: int       # byte offset of the length field
    size_field_width: int        # bytes (4 for uint32)
    fixed_overhead: int          # total bytes excluding payload

@dataclass
class CommandDef:
    group_id: int
    command_id: int
    response_id: int | None
    command_name: str            # "Get System Version"
    constant_name: str           # "CMD_GET_SYSTEM_VERSION" (generated)
    payload_fields: list[FieldDef]
    response_fields: list[FieldDef]

@dataclass
class FieldDef:
    name: str                    # "fw_version"
    c_type: str                  # "uint32_t"
    byte_size: int
    is_array: bool
    array_count: int | None
    value_min: float | None
    value_max: float | None

@dataclass
class IcdDocument:
    hw_name: str
    frame_formats: list[FrameFormat]
    groups: dict[int, list[CommandDef]]   # group_id → commands
    max_payload_bytes: int
    rf_stations: list[RfStation]
```

```python
def read_icd(path: str, hw_name: str, sheet_map: dict = None) -> IcdDocument:
    wb = openpyxl.load_workbook(path, read_only=True, data_only=True)
    doc = IcdDocument(hw_name=hw_name, ...)
    _parse_frame_formats(wb, doc, sheet_map)
    _parse_command_groups(wb, doc, sheet_map)
    _parse_rf_params(wb, doc, sheet_map)
    return doc
```

---

### 20.5 Generated C++ Output
[↑ Table of Contents](#table-of-contents)

Given a `CommandDef` for "Get System Version" (Group 100, CmdID 1, RespID 2):

**`{hw}_frame_types.h`** (generated, do not edit):
```cpp
// AUTO-GENERATED by tools/icd_codegen — do not edit
// Source: ICD_COMM_DF.xlsx  hw: comm_df

#pragma once
#include <cstdint>

namespace comm_df {

// Frame type tags (match extract_frame return values)
constexpr int FRAME_SDFC_CMD  = 1;
constexpr int FRAME_SDFC_RESP = 2;
constexpr int FRAME_SCD       = 3;

// SDFC→DRS command frame
constexpr uint8_t CMD_HEADER[4]  = {0xAA, 0xAB, 0xBA, 0xBB};
constexpr uint8_t CMD_FOOTER[4]  = {0xCC, 0xCD, 0xDC, 0xDD};
constexpr int     CMD_OVERHEAD   = 16;

// DRS→SDFC response frame
constexpr uint8_t RESP_HEADER[4] = {0xEE, 0xEF, 0xFE, 0xFF};
constexpr uint8_t RESP_FOOTER[4] = {0xFF, 0xFE, 0xEF, 0xEE};
constexpr int     RESP_OVERHEAD  = 18;

// SCD compact frame
constexpr uint8_t SCD_HEADER[2]  = {0xAA, 0xAA};
constexpr uint8_t SCD_FOOTER[2]  = {0xEE, 0xEE};
constexpr int     SCD_OVERHEAD   = 12;

constexpr int MAX_PAYLOAD = 1048576;

// Group IDs
constexpr uint16_t GROUP_SYSTEM_MGMT  = 100;
constexpr uint16_t GROUP_RF_DETECT    = 101;
constexpr uint16_t GROUP_BITE         = 111;

// Command Unit IDs — Group 100
constexpr uint16_t CMD_GET_SYSTEM_VERSION = 1;
constexpr uint16_t RESP_SYSTEM_VERSION    = 2;
constexpr uint16_t CMD_PBIT_STATUS        = 5;
// ... (one constant per command)

// Command Unit IDs — Group 101
constexpr uint16_t CMD_SET_THRESHOLD      = 25;
constexpr uint16_t CMD_SET_RESOLUTION     = 27;
constexpr uint16_t CMD_GET_FFT_DATA       = 43;
constexpr uint16_t RESP_FFT_DATA          = 44;
// ... etc.

} // namespace comm_df
```

**`{hw}_parser.cpp`** skeleton (generated, then hand-completed):
```cpp
// AUTO-GENERATED skeleton — fill in parse_group_NNN() bodies
#include "comm_df_frame_types.h"
#include "../common/parser_api.h"
#include "../common/json_writer.h"
using namespace comm_df;

// ── Frame extraction ─────────────────────────────────────────────────────────

int extract_frame(const uint8_t* buf, int buf_len,
                  uint8_t* out_frame, int* out_len) {
    // TODO: scan for CMD_HEADER, RESP_HEADER, SCD_HEADER
    // Return FRAME_SDFC_CMD / FRAME_SDFC_RESP / FRAME_SCD / 0 / -1
    // (generated: header scan loop + length validation + footer check)
    return 0;  // replace with generated implementation
}

// ── Message parsing ───────────────────────────────────────────────────────────

static void parse_group_100(const uint8_t* payload, int len,
                             uint16_t unit_id, JsonWriter& w) {
    switch (unit_id) {
        case RESP_SYSTEM_VERSION: {
            // TODO: decode 26-byte System Version response
            // Fields: fw_version(uint32), driver_version(uint32),
            //         fpga_version(uint32), bsp_version(uint32),
            //         processor_id(uint16), rf_tuner_ids[3](uint16),
            //         fpga_type_id(uint16)
            break;
        }
        // ... one case per command (generated)
        default:
            w.set("raw_hex", to_hex(payload, len));
    }
}

static void parse_group_101(const uint8_t* payload, int len,
                             uint16_t unit_id, JsonWriter& w) {
    switch (unit_id) {
        case RESP_FFT_DATA: {
            // TODO: decode 6404-byte FFT response
            // Fields: bin_count(uint32) + bins[1600](float32)
            break;
        }
        // ... generated cases for FH, FF, Burst, etc.
        default:
            w.set("raw_hex", to_hex(payload, len));
    }
}

const char* parse_message(const uint8_t* frame, int frame_len, int frame_type) {
    JsonWriter w;
    FrameHeader hdr = decode_header(frame, frame_type);
    w.set("frame_type",  frame_type == FRAME_SDFC_RESP ? "response" : "command");
    w.set("group_id",    hdr.group_id);
    w.set("unit_id",     hdr.unit_id);
    if (frame_type == FRAME_SDFC_RESP) w.set("status", hdr.status);

    switch (hdr.group_id) {
        case GROUP_SYSTEM_MGMT: parse_group_100(frame + hdr.payload_offset, hdr.payload_len, hdr.unit_id, w); break;
        case GROUP_RF_DETECT:   parse_group_101(frame + hdr.payload_offset, hdr.payload_len, hdr.unit_id, w); break;
        case GROUP_BITE:        parse_group_111(frame + hdr.payload_offset, hdr.payload_len, hdr.unit_id, w); break;
        default: w.set("raw_hex", to_hex(frame + hdr.payload_offset, hdr.payload_len));
    }
    return w.release();
}

void free_result(const char* result) { delete[] result; }
```

---

### 20.6 Generated TypeScript Output
[↑ Table of Contents](#table-of-contents)

```typescript
// AUTO-GENERATED by tools/icd_codegen — do not edit
// Source: ICD_COMM_DF.xlsx  hw: comm_df

export interface CommDfSystemVersion {
  frameType: 'response';
  groupId: 100;
  unitId: 2;
  status: number;
  fwVersion: string;
  driverVersion: string;
  fpgaVersion: string;
  bspVersion: string;
  processorId: number;
  rfTunerIds: [number, number, number];
  fpgaTypeId: number;
}

export interface CommDfFftData {
  frameType: 'response';
  groupId: 101;
  unitId: 44;
  status: number;
  binCount: number;
  bins: number[];            // 1600 float32 values
}

export interface CommDfFhDetection {
  frameType: 'response';
  groupId: 101;
  unitId: 40;
  status: number;
  hopperCount: number;
  hoppers: Array<{
    channel: number;
    frequencyHz: number;
    powerDbm: number;
    dwellUs: number;
  }>;
}

// Union type for all COMM DF messages — use in Angular component inputs
export type CommDfMessage =
  | CommDfSystemVersion
  | CommDfFftData
  | CommDfFhDetection
  // ... one per response type
  ;
```

---

### 20.7 Generated Hardware Profile YAML
[↑ Table of Contents](#table-of-contents)

```yaml
# AUTO-GENERATED by tools/icd_codegen — safe to edit port/broker/topic
# Source: ICD_COMM_DF.xlsx  hw: comm_df  generated: 2026-04-11

name: comm_df
port: 5490                          # --port argument
kafka_topic: comm_df.drs.ui
kafka_broker: "${KAFKA_BROKER}"
parser_lib: parsers/libcomm_df.dll
max_connections: 4
health_interval_ms: 1000
receive_buffer_bytes: 65536         # sized to max response: FFT = 6,404 bytes
frame_terminator: "magic_bytes"
protocols: [sdfc_drs, scd_drs]
protocol_version: "ICD-COMM-DF-v1"
group_ids: [100, 101, 111]
store_fft: true                     # set false to drop FFT data before Kafka
```

---

### 20.8 What the Generator Does NOT Produce
[↑ Table of Contents](#table-of-contents)

| Not generated | Reason |
|---|---|
| Field-decode bodies inside `parse_group_NNN()` | Requires human judgement: bit-packing, multi-field conditions, enumeration semantics not fully expressible in Excel |
| Checksum / CRC validation | CRC algorithm and polynomial are not present in the ICD Excel; must be confirmed from hardware spec |
| Unit tests for the parser | Test cases need real captured frames; cannot be synthesised from Excel alone |
| TimescaleDB migration SQL | Column-level schema decisions (what to index, what goes in JSONB vs scalars) require DBA review |
| Kafka topic names beyond the default convention | Topic naming policy is a system-level decision, not ICD-driven |

The generator is a skeleton writer, not a complete solution. A new hardware variant goes from "nothing" to "compiling C++ file with all constants and dispatch structure in place, three fields per `TODO` comment, and a matching YAML profile" — the C++ developer then fills in the field decode bodies. This is the majority of the boilerplate eliminated.

---

### 20.9 Offline Vendoring
[↑ Table of Contents](#table-of-contents)

Both runtime dependencies are small and vendorable:

```
openpyxl==3.1.5        # ~250 KB wheel
jinja2==3.1.4          # ~175 KB wheel
markupsafe==2.1.5      # jinja2 dependency, ~25 KB
```

Add to `tools/icd_codegen/packages/` and install with:
```bash
pip install --no-index --find-links=packages -r requirements.txt
```

No internet access required on the development machine.

---

## 21. Low Level Design — Layered Architecture
[↑ Table of Contents](#table-of-contents)

This section defines the internal layer model for each service. Layers enforce a strict dependency rule: each layer may only call the layer directly below it. No layer may reach across a peer or skip a level. Cross-cutting concerns (logging, health, auth) are handled via middleware or injected collaborators, not by layers calling each other sideways.

---

### 21.1 Design Validation Summary
[↑ Table of Contents](#table-of-contents)

Before proceeding to the LLD, the design choices from Section 2 were formally reviewed against the codebase analysis findings from Section 7.7 and the anti-patterns documented in `docs/ewtss/legacy/ANTI_PATTERNS_AND_PERFORMANCE.md` (curated summary at `docs/ewtss/legacy-system-audit.md`).

| Design choice | Verdict | Key evidence |
|---|---|---|
| C++ shared libs for parsing | Valid | 4+ copies of `parse_command()` in existing Python; AP-13 (no stream reassembly) |
| asyncio TCP server | Valid | TS-4 (GIL contention); AP-13 (partial frame loss); AP-12 (`=+1` bugs) |
| YAML hardware profiles | Valid* | 6 duplicated directory trees for 6 variants confirmed |
| aiokafka single library | Valid | TS-2 (confluent-kafka + dead kafka-python coexist); AP-9 (producer thread-safety) |
| TimescaleDB | Valid | AP-11/AP-5/P-1 (full-table scans, per-row commits, no indexes) |
| ICD codegen utility | Valid | AP-15 (CSV with ast.literal_eval; silent failures; diverged copies) |
| Three-service SOA | Valid | Boundary already present but with leaky interfaces |
| Kafka KRaft | Valid | Single-node; ZooKeeper adds zero value here |
| Electron + Angular + CesiumJS + CZML | Valid* | AP-10/TS-6 (polling replaced by WebSocket push) |
| drs-bridge bidirectionality | **Gap fixed** | Sections 7.3 and 7.4 updated — `format_response()` added to C API; `handle_client` shows full request→response→Kafka cycle |

*YAML profiles: "new variant = new YAML only" is true for Python; a variant with a genuinely new protocol format still requires a new C++ parser file and a CMake entry.  
*Electron: verify DVD capacity is dual-layer (8.5 GB) — CesiumJS (~120 MB) + Electron (~200 MB) + vendored packages may exceed single-layer (4.7 GB).

---

### 21.2 System-Level Layered Model
[↑ Table of Contents](#table-of-contents)

Six horizontal layers span the entire system. Each layer communicates only with its adjacent neighbors; no layer skips levels.

```
┌──────────────────────────────────────────────────────────────────────────────┐
│  L6  PRESENTATION                                                            │
│      Electron shell + Angular app + CesiumJS viewer                         │
│      Communicates with: HTTP/REST (sg-service, drs-server) +                │
│                         WebSocket push (drs-server) +                        │
│                         CZML file load (local, from sg-service)             │
├──────────────────────────────────────────────────────────────────────────────┤
│  L5  API GATEWAY                                                             │
│      sg-service FastAPI  │  drs-server FastAPI                               │
│      — auth, routing, schema validation, WebSocket upgrade                  │
│      Communicates with: L4 (service layer below), L6 (frontend above)       │
├──────────────────────────────────────────────────────────────────────────────┤
│  L4  BUSINESS LOGIC                                                          │
│      sg-service: scenario lifecycle, STK orchestration, RBAC, PDF           │
│      drs-server: query/aggregation service, WebSocket broadcaster           │
│      Communicates with: L3 (Kafka + DB), L5 (API above)                     │
├──────────────────────────────────────────────────────────────────────────────┤
│  L3  MESSAGE BUS + PERSISTENCE                                               │
│      Kafka KRaft  │  TimescaleDB (shared between sg-service + drs-server)   │
│      — durable event log, hypertable storage, async decoupling              │
│      Communicates with: L2 (bridge below), L4 (services above)              │
├──────────────────────────────────────────────────────────────────────────────┤
│  L2  PROTOCOL BRIDGE                                                         │
│      drs-bridge: asyncio TCP + C++ parsers + ResponseRouter                 │
│      — bidirectional: parses commands from SDFC, emits responses to SDFC,   │
│        publishes both sides to Kafka                                         │
│      Communicates with: L1 (TCP below), L3 (Kafka above)                    │
├──────────────────────────────────────────────────────────────────────────────┤
│  L1  TRANSPORT                                                               │
│      TCP/IP LAN — 1 Gbps, point-to-point between SDFC and drs-bridge        │
├──────────────────────────────────────────────────────────────────────────────┤
│  L0  HARDWARE / EXTERNAL SYSTEMS                                             │
│      DRS devices (real hardware, integrated mode only)  │  SDFC app          │
│      STK 12 (COM, on SG workstation)                                         │
└──────────────────────────────────────────────────────────────────────────────┘
```

Layer rules:
- L6 may call L5 only (never directly query the DB or Kafka)
- L5 may call L4 only (never call C++ parsers or Kafka directly)
- L4 may call L3 only (never open TCP sockets)
- L2 may call L3 (Kafka produce) and L1 (TCP read/write) only
- No layer may instantiate a layer above it

---

### 21.3 drs-bridge — Internal Layers
[↑ Table of Contents](#table-of-contents)

drs-bridge is the most structurally complex service because it is bidirectional and mode-sensitive.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  LAYER 4 — LIFECYCLE & SUPERVISION                                          │
│  supervisor.py, health_reporter.py, config.py                               │
│  Responsibility: Start/stop profile listeners; monitor health; reload       │
│                  profiles without process restart                            │
│  Input:  YAML profile files on disk                                         │
│  Output: Running asyncio TCP servers (one per profile); health metrics      │
├─────────────────────────────────────────────────────────────────────────────┤
│  LAYER 3 — RESPONSE ROUTING (mode-aware)                                    │
│  response_router.py: ResponseRouter, SessionRegistry, RandomGenerator,      │
│                       SgClient (HTTP to sg-service), HardwareRelay          │
│  Responsibility: Given a parsed command dict + profile, return a response   │
│                  JSON string appropriate to the current operating mode       │
│  Input:  Parsed command dict (from layer 2) + active session mode           │
│  Output: Response JSON string → layer 2 (for encoding back to SDFC)         │
│  Modes:  RANDOM → RandomGenerator.generate()                                │
│          SCENARIO → SgClient.get_scenario_response()  (HTTP to sg-service) │
│          INTEGRATED → HardwareRelay.forward()  (TCP to real hardware)       │
│  Rule:   No frame bytes enter this layer. No Kafka calls in this layer.     │
├─────────────────────────────────────────────────────────────────────────────┤
│  LAYER 2 — PARSE / ENCODE (C++ via ctypes)                                  │
│  frame_dispatcher.py: ParserHandle (wraps ctypes calls)                     │
│  C++ shared libs: extract_frame(), parse_message(), format_response()       │
│  Responsibility: frame detection, type tagging, JSON decode, JSON encode    │
│  Input:  Raw bytes buffer (from layer 1)                                    │
│  Output: (frame_bytes, frame_type) tuples upstream; binary response frames  │
│           downstream                                                         │
│  Rule:   No network I/O. No Kafka. Pure byte ↔ JSON transformation.        │
├─────────────────────────────────────────────────────────────────────────────┤
│  LAYER 1 — TRANSPORT (asyncio TCP)                                          │
│  tcp_server.py: handle_client() coroutine, asyncio.start_server()           │
│  kafka_producer.py: AIOKafkaProducer (shared, one per bridge process)       │
│  Responsibility: Accept TCP connections; accumulate bytes into buffer;      │
│                  feed layer 2; receive response bytes from layer 2;          │
│                  write back to SDFC; publish both sides to Kafka            │
│  Input:  asyncio.StreamReader bytes + response frames from layer 2         │
│  Output: Kafka produce calls (cmd + response records); TCP writes to SDFC  │
│  Rule:   No business logic. No mode switching. Only I/O orchestration.      │
└─────────────────────────────────────────────────────────────────────────────┘
```

#### Key module responsibilities

| Module | Layer | Does | Does not |
|---|---|---|---|
| `supervisor.py` | 4 | Loads YAML profiles, starts one TCP server per profile, restarts crashed servers with exponential backoff | Parse frames, make Kafka calls |
| `health_reporter.py` | 4 | Publishes health metrics to `{hw}.system.health` Kafka topic at `health_interval_ms` | Manage connections |
| `response_router.py` | 3 | Dispatches to correct generator based on active session mode | Touch binary frames |
| `random_gen.py` | 3 | Generates plausible random values within `min`/`max` bounds from YAML profile | Know about Kafka or TCP |
| `sg_client.py` | 3 | Async HTTP GET to sg-service `/exercises/{id}/responses` | Cache responses locally |
| `frame_dispatcher.py` | 2 | ctypes wrapper for all three C++ functions | Any I/O |
| `tcp_server.py` | 1 | Buffer accumulation, frame loop, Kafka produce, TCP write | Any logic beyond I/O |
| `kafka_producer.py` | 1 | Single shared `AIOKafkaProducer` with `linger_ms=5`, `compression_type="lz4"` | Consume |

#### Error handling contract per layer

| Layer | On C++ parse error (-1) | On Kafka produce error | On TCP write error |
|---|---|---|---|
| 1 (Transport) | Log + drain buffer to next header | Log + retry with backoff; close connection after N failures | Log + close connection cleanly |
| 2 (Parse) | Return -1 to layer 1 (corrupt frame) | N/A | N/A |
| 3 (Routing) | N/A | N/A (Kafka not called here) | N/A |
| 4 (Lifecycle) | N/A | Alert health reporter | Supervisor restarts listener |

---

### 21.4 drs-server — Internal Layers
[↑ Table of Contents](#table-of-contents)

drs-server is a pure consumer: it has no TCP connections, no STK, no command routing. It receives from Kafka and serves queries and WebSocket pushes.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  LAYER 3 — API                                                              │
│  routers/: measurement_router.py, session_router.py, health_router.py      │
│  ws/: websocket_manager.py                                                  │
│  Responsibility: FastAPI route handlers (async def), WebSocket accept/      │
│                  broadcast, request validation, response serialisation       │
│  Input:  HTTP requests, WebSocket upgrade requests                          │
│  Output: JSON responses, WebSocket push events                              │
│  Rule:   No DB queries. No Kafka. Delegates entirely to layer 2.            │
├─────────────────────────────────────────────────────────────────────────────┤
│  LAYER 2 — SERVICE                                                          │
│  services/: measurement_service.py, session_service.py,                     │
│             report_service.py, broadcast_service.py                         │
│  Responsibility: Query composition, pagination, aggregation, fan-out to    │
│                  WebSocket subscribers on new Kafka messages                 │
│  Input:  Validated query params from layer 3; Kafka messages from layer 1  │
│  Output: Pydantic response models; WebSocket broadcast calls to layer 3    │
│  Rule:   No direct asyncpg/SQL. All DB access through layer 1 repository.  │
├─────────────────────────────────────────────────────────────────────────────┤
│  LAYER 1 — REPOSITORY + INGEST                                              │
│  repos/: measurement_repo.py, session_repo.py                               │
│  ingest/: consumer_manager.py, kafka_consumer.py (one per topic group)      │
│  Responsibility: Async SQLAlchemy models, hypertable INSERT, SELECT with    │
│                  chunk exclusion; Kafka consumer lifecycle; manual offset    │
│                  commit after successful DB write (fixes AP-4)               │
│  Input:  Kafka messages (bytes); SQL query specs from layer 2              │
│  Output: ORM model instances; Kafka offset commits                          │
│  Rule:   No business logic. No HTTP. Pure DB + Kafka I/O.                  │
├─────────────────────────────────────────────────────────────────────────────┤
│  LAYER 0 — INFRASTRUCTURE                                                   │
│  configs/: database.py (asyncpg engine, TimescaleDB), kafka_config.py      │
│  Responsibility: Connection pool init, engine creation, consumer group      │
│                  registration, lifespan startup/shutdown                    │
│  Rule:   No business logic. Constructed once; shared via dependency inject. │
└─────────────────────────────────────────────────────────────────────────────┘
```

#### Kafka consumer → WebSocket push path

The ingest layer is the only place that bridges Kafka and WebSocket. The pattern avoids threading by using asyncio:

```python
# ingest/kafka_consumer.py
async def consume_loop(topic: str, service: MeasurementService):
    consumer = AIOKafkaConsumer(topic, ...)
    async for msg in consumer:
        try:
            data = json.loads(msg.value)
            await service.ingest(data)           # write to DB (layer 1→repo)
            await service.broadcast(data)        # fan-out to WebSocket clients (layer 1→layer 2→layer 3)
            await consumer.commit()              # manual offset commit only after success
        except Exception:
            logger.exception("ingest failed, offset not committed")
```

#### Key design rules

- All route handlers are `async def` — no sync SQLAlchemy (fixes TS-1)
- Kafka consumers started in FastAPI lifespan via `consumer_manager.start_all()` (fixes AP-2)
- Session scoped per message, not per thread (fixes AP-1, AP-3)
- No `query.all()` — all queries use `.offset()` + `.limit()` with index hints (fixes AP-11)

---

### 21.5 sg-service — Internal Layers
[↑ Table of Contents](#table-of-contents)

sg-service is the scenario brain. It owns STK computation, user management, and the session/exercise lifecycle. It is the only service that writes to sg-specific tables; drs-server has read-only access to `drs_sessions` and `computed_links`.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  LAYER 3 — API                                                              │
│  routers/: exercise_router.py, session_router.py, user_router.py,           │
│            scenario_router.py, report_router.py                              │
│  middleware/: auth_middleware.py (JWT decode, RBAC enforcement)              │
│  Responsibility: FastAPI routes, auth enforcement, CZML file serve,         │
│                  PDF download endpoint                                       │
│  Rule:   Auth check before every mutating route. No STK calls here.        │
├─────────────────────────────────────────────────────────────────────────────┤
│  LAYER 2 — SERVICE / ORCHESTRATION                                          │
│  services/: exercise_service.py, stk_orchestrator.py, rbac_service.py,      │
│             report_service.py (WeasyPrint), scenario_response_service.py    │
│  Responsibility: STK job submission (ProcessPoolExecutor), scenario         │
│                  response lookup for drs-bridge, PDF generation,            │
│                  CZML export lifecycle                                       │
│  Key contract: scenario_response_service answers drs-bridge HTTP queries   │
│                GET /exercises/{id}/responses?group_id=X&unit_id=Y           │
│  Rule:   All STK calls isolated to stk_orchestrator.py in a separate        │
│          process (ProcessPoolExecutor) — STK COM blocks; it must never      │
│          run on the FastAPI event loop thread.                               │
├─────────────────────────────────────────────────────────────────────────────┤
│  LAYER 1 — DOMAIN + REPOSITORY                                              │
│  domain/: Exercise, Scenario, GamingArea, Emitter, ComputedLink, User       │
│  repos/: exercise_repo.py, computed_link_repo.py, user_repo.py              │
│  stk/: stk_service.py (IStkService), stk_com_service.py, mock_stk_service  │
│  Responsibility: SQLAlchemy ORM models, CRUD, computed_links hypertable     │
│                  writes, STK service abstraction (real vs mock)             │
│  Rule:   Domain objects have no FastAPI imports. STK service is injected    │
│          (never imported directly), enabling MockStkService substitution.  │
├─────────────────────────────────────────────────────────────────────────────┤
│  LAYER 0 — INFRASTRUCTURE                                                   │
│  configs/: database.py, kafka_config.py, stk_config.py                      │
│  Responsibility: TimescaleDB pool, Kafka producer (for session events),     │
│                  STK COM initialisation (in worker process), ProcessPool    │
└─────────────────────────────────────────────────────────────────────────────┘
```

#### STK isolation rule

STK COM (`agi.stk12`) is single-threaded and blocking. It must never run on the FastAPI event loop thread. Two viable isolation strategies:

**Option A — ProcessPoolExecutor with pool_size=1 (recommended for STKEngine)**

STKEngine permits only one instance per process. A `ProcessPoolExecutor(max_workers=1)` gives a single dedicated worker process where `get_stk_service()` initialises the singleton on first use and reuses it for all subsequent jobs. The main process never holds a STK reference.

```python
# services/stk_orchestrator.py
_worker_pool = ProcessPoolExecutor(max_workers=1)

class StkOrchestrator:
    async def run_exercise(self, exercise: Exercise) -> list[ComputedLink]:
        loop = asyncio.get_running_loop()
        return await loop.run_in_executor(
            _worker_pool,
            _compute_in_worker,   # module-level function (picklable)
            exercise
        )

def _compute_in_worker(exercise: Exercise) -> list[ComputedLink]:
    # get_stk_service() singleton lives here in the worker process
    return get_stk_service().compute_links(exercise)
```

**Option B — ThreadPoolExecutor (acceptable for STKDesktop on dev machines)**

STKDesktop is also effectively single-threaded per session. A `ThreadPoolExecutor(max_workers=1)` with a process-level singleton works for development but is less crash-isolated than a subprocess.

`MockStkService` (Friis + geometric LOS) runs in the same process — no isolation needed, no COM dependency.

#### Scenario response endpoint

drs-bridge layer 3 calls sg-service synchronously (HTTP GET) when in SCENARIO mode to retrieve the pre-computed response for a given command. This endpoint must be low-latency (< 5 ms) because it is on the request/response path:

```
GET /exercises/{exercise_id}/responses?group_id=101&unit_id=40&tick={session_tick}

Response: {"group_id": 101, "unit_id": 40, "status": 0, "hopper_count": 3, "hoppers": [...]}
```

`computed_links` hypertable has a composite index on `(exercise_id, tick_sec, group_id, unit_id)` so this is an index point-lookup — O(1) regardless of table size.

---

### 21.6 Frontend — Internal Layers
[↑ Table of Contents](#table-of-contents)

The frontend runs inside Electron. The Angular app is unaware of Electron — it communicates with the outside world only via HTTP/WebSocket through a preload-proxied API. This separation means the Angular app can run in a browser without code changes.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  LAYER 3 — ELECTRON SHELL                                                   │
│  electron/main.ts, electron/preload.ts                                      │
│  Responsibility: Window lifecycle, contextBridge (exposes safe API to       │
│                  renderer), CSP enforcement, local file access for CZML     │
│  Rule:   No Angular imports. No business logic. Minimal surface area.       │
├─────────────────────────────────────────────────────────────────────────────┤
│  LAYER 2 — ANGULAR APPLICATION SHELL                                        │
│  app.routes.ts, app.config.ts                                               │
│  core/: auth.service.ts, api.service.ts, ws.service.ts, error-handler.ts   │
│  Responsibility: Route guards (auth), HTTP client wrapper, WebSocket        │
│                  service (RxJS WebSocketSubject), global error handling     │
│  Rule:   No CesiumJS imports here. Components import from feature modules. │
├─────────────────────────────────────────────────────────────────────────────┤
│  LAYER 1 — FEATURE MODULES                                                  │
│  features/                                                                  │
│    scenario-planner/   — exercise CRUD, emitter/antenna config, STK trigger│
│    live-monitor/       — per-hardware WebSocket subscriptions, live tables  │
│    report-viewer/      — paginated query results, export to PDF             │
│    user-admin/         — RBAC management (admin role only)                  │
│  Responsibility: Reactive Forms (scenario planning), RxJS data streams     │
│                  (live monitor), Angular Material tables (reports)          │
│  Rule:   Feature modules are lazy-loaded. No cross-feature imports.        │
├─────────────────────────────────────────────────────────────────────────────┤
│  LAYER 0 — VISUALIZATION                                                    │
│  shared/cesium/: cesium-viewer.component.ts, czml-loader.service.ts        │
│  Responsibility: CesiumJS Viewer initialisation, CZML DataSource load,      │
│                  timeline control, camera bookmarks                          │
│  Rule:   CesiumJS is only ever imported in this layer. Feature modules     │
│          interact with the Cesium layer through typed Angular @Input/@Output│
└─────────────────────────────────────────────────────────────────────────────┘
```

#### WebSocket subscription pattern (live-monitor)

All live data replaces `setInterval` polling (AP-10 / TS-6):

```typescript
// features/live-monitor/services/hardware-stream.service.ts
@Injectable({ providedIn: 'root' })
export class HardwareStreamService {
  stream$(hwType: string): Observable<DrsMessage> {
    return webSocket<DrsMessage>(
      `ws://localhost:8001/ws/measurements/${hwType}`
    ).pipe(
      retry({ delay: 2000 }),
      share()            // one WS connection shared across all subscribers
    );
  }
}
```

---

### 21.7 Cross-Layer Data Flows
[↑ Table of Contents](#table-of-contents)

#### Flow A — Command/Response (SDFC ↔ drs-bridge)

This is the primary real-time path. Latency target: < 50 ms end-to-end.

```
SDFC
  │  TCP bytes (SDFC→DRS command frame)
  ▼
drs-bridge L1 (Transport)
  │  accumulate buffer → extract_frame()
  ▼
drs-bridge L2 (Parse)    ← C++ extract_frame + parse_message → JSON
  │  command dict
  ▼
drs-bridge L1 (Transport)  ← Kafka produce: {hw}.cmd (for recording)
  │  command dict
  ▼
drs-bridge L3 (ResponseRouter)
  │  mode = RANDOM → random_gen.generate()       → response JSON
  │  mode = SCENARIO → sg_client.get_response()  → response JSON  (HTTP to sg-service L3)
  │  mode = INTEGRATED → hardware_relay.forward()→ response JSON  (TCP to real hardware)
  ▼
drs-bridge L2 (Encode)   ← C++ format_response → binary response frame
  │  binary bytes
  ▼
drs-bridge L1 (Transport)
  ├─► TCP write → SDFC (synchronous response on same connection)
  └─► Kafka produce: {hw}.drs.ui (for recording by drs-server)
```

#### Flow B — Telemetry Recording + UI Push

Runs concurrently with Flow A. Decoupled through Kafka.

```
Kafka topic: {hw}.drs.ui
  │  JSON message
  ▼
drs-server L1 (Ingest / aiokafka consumer)
  │  parse JSON
  ▼
drs-server L1 (Repository)
  │  INSERT into measurements hypertable + measurement_scalars
  │  manual offset commit after successful INSERT
  ▼
drs-server L2 (Service / BroadcastService)
  │  fan-out to all subscribed WebSocket clients
  ▼
drs-server L3 (API / WebSocketManager)
  │  WebSocket push (JSON)
  ▼
Frontend L1 (live-monitor / HardwareStreamService)
  │  RxJS stream update
  ▼
Frontend L1 (live-monitor component)
  └─► Angular table / chart update (< 100 ms from Kafka publish)
```

#### Flow C — Scenario Computation + CZML Load

This flow is asynchronous and session-scoped, not per-command.

```
Frontend L1 (scenario-planner)
  │  POST /exercises (HTTP to sg-service)
  ▼
sg-service L3 (API)
  ▼
sg-service L2 (StkOrchestrator)
  │  ProcessPoolExecutor.submit(stk_service.compute_links)
  ▼
sg-service L1 (StkComService / MockStkService)  ← runs in worker process
  │  STK COM computation → ComputedLink rows
  ▼
sg-service L1 (computed_link_repo)
  │  INSERT into computed_links hypertable
  ▼
sg-service L2 (StkOrchestrator)
  │  stk_service.export_czml() → czml/{exercise_id}.czml (local file)
  ▼
sg-service L3 (API)  ← returns 201 with exercise_id
  │  exercise_id
  ▼
Frontend L0 (CesiumLoader)
  │  GET /exercises/{id}/czml (streams file from sg-service)
  ▼
CesiumJS CzmlDataSource.load()
  └─► 3D timeline animation rendered in CesiumJS viewer
```

---

### 21.8 Operating Mode Switching
[↑ Table of Contents](#table-of-contents)

The three operating modes (Random / Scenario / Integrated) are session-scoped, not process-global. This fixes the existing `isRandom` MySQL flag which is a process-wide switch.

```python
# drs-bridge: response_router.py
@dataclass
class DrsSession:
    session_id: str
    exercise_id: str | None      # set when mode = SCENARIO
    mode: OperatingMode          # RANDOM | SCENARIO | INTEGRATED
    started_at: datetime

class SessionRegistry:
    """Thread-safe (asyncio-safe) registry of active sessions per hardware type."""
    _sessions: dict[str, DrsSession] = {}

    def activate(self, hw_name: str, session: DrsSession) -> None:
        self._sessions[hw_name] = session

    def active(self, hw_name: str) -> DrsSession | None:
        return self._sessions.get(hw_name)

    def deactivate(self, hw_name: str) -> None:
        self._sessions.pop(hw_name, None)
```

Session lifecycle is controlled by sg-service via a Kafka control topic (`drs.session.control`), consumed by drs-bridge's supervisor layer:

```
sg-service:  POST /sessions/{id}/start  →  Kafka: drs.session.control
                                              {"action": "start", "hw": "comm_df",
                                               "mode": "SCENARIO", "exercise_id": "ex-42"}
drs-bridge:  consumer reads event  →  SessionRegistry.activate("comm_df", session)
             ResponseRouter now routes comm_df commands to sg-service scenario endpoint
```

This makes mode switching atomic and observable — it appears in the Kafka log and can be replayed for debugging.

---

### 21.9 Cross-Cutting Concerns
[↑ Table of Contents](#table-of-contents)

These concerns span all layers and services. They are implemented via middleware, dependency injection, or shared libraries — not by layers calling each other sideways.

#### Structured Logging

All services use the same log format. Log level, service name, and trace context are injected at the middleware layer so individual modules never format their own prefixes:

```python
# shared/logging/log_config.py
LOG_FORMAT = {
    "time":    "%(asctime)s",
    "level":   "%(levelname)s",
    "service": "%(service)s",        # injected by middleware
    "hw":      "%(hw_name)s",        # injected per-connection
    "msg":     "%(message)s"
}
```

#### Health Endpoints

Every service exposes `GET /health` returning `{"status": "ok", "checks": {...}}`. drs-bridge additionally exposes per-hardware connection state. A single health-check script can poll all three services before declaring the system ready.

#### Auth (sg-service and drs-server)

JWT tokens issued by sg-service auth endpoint. drs-server validates the same JWT (shared secret, not inter-service HTTP call). drs-bridge has no auth — it is on the LAN segment accessible only to SDFC; the network perimeter is the auth boundary.

| Service | Auth mechanism |
|---|---|
| sg-service | JWT issued + validated; RBAC via `user_roles` + `role_features` tables |
| drs-server | JWT validated (read-only token accepted); no RBAC (read only) |
| drs-bridge | None — LAN-only, SDFC is trusted peer |
| Frontend | JWT stored in Electron session (not localStorage); sent on every request |

#### Offline Vendoring Checklist

All dependency wheels are committed to `packages/` directories:

| Service | Key packages | Approximate size |
|---|---|---|
| drs-bridge | aiokafka, pyyaml | ~2 MB |
| drs-server | aiokafka, asyncpg, sqlalchemy, fastapi | ~15 MB |
| sg-service | fastapi, sqlalchemy, asyncpg, weasyprint, jinja2, aiokafka | ~80 MB |
| tools/icd_codegen | openpyxl, jinja2 | ~1 MB |
| Frontend | CesiumJS (bundled) | ~120 MB |
| Electron | Chromium runtime | ~200 MB |

Verify total against delivery medium capacity before Milestone 2 packaging.

---

## 22. Visualization Platform Analysis — CesiumJS vs STK Display vs ArcGIS
[↑ Table of Contents](#table-of-contents)

This section documents the comparative analysis that validated CesiumJS (Option E) as the visualization platform. Three candidates were evaluated across four dimensions: implementation complexity, air-gapped deployment constraints, rendering performance at scale, and 2D/3D capability.

---

### 22.1 Candidate Definitions
[↑ Table of Contents](#table-of-contents)

| Candidate | What it is | How it integrates |
|---|---|---|
| **CesiumJS** (Option E) | Open-source WebGL 3D globe library | Angular component inside Electron renderer |
| **STK native display** (Options D1/D2) | STK's own Windows rendering engine (WPF/ActiveX COM) | Hosted in WPF `WindowsFormsHost`; C# controls it |
| **ArcGIS Maps SDK for JavaScript** | Esri's commercial WebGL mapping SDK | Angular component inside Electron renderer (same integration path as CesiumJS) |

ArcGIS Earth and ArcGIS Pro were evaluated and **immediately eliminated**: neither is embeddable in an Electron renderer process. They are standalone desktop applications, not SDKs.

---

### 22.2 Implementation Complexity
[↑ Table of Contents](#table-of-contents)

#### CesiumJS — Medium (≈ 2 weeks)

The CesiumJS + Angular integration is well-documented. The complexity is front-loaded in setup (build pipeline, NgZone, Electron CSP — see Sections 8.5–8.8) and pays off immediately: once the viewer component exists, adding features is incremental. The decisive advantage is **native CZML support** — STK's computation output loads directly into CesiumJS with one API call. No translation layer.

The `MockStkService` CZML generator requires writing CZML packets by hand (additional ≈ 3 days), but this is a developer productivity tool, not a deployment dependency.

#### STK native display — Low for scenario display, Medium for live overlays

STK's COM API (`IAgStkObjectRoot`) renders scenario entities natively. For a static scenario display, the C# developer writes ~200 lines of COM initialisation and gets a fully rendered 3D globe with entity tracks and coverage cones.

**The fundamental limitation:** STK's COM API is synchronous and designed for batch computation, not real-time streaming. It has no mechanism to receive WebSocket messages and update rendered entities in real time. Consequence: live DRS status markers and real-time signal detections — a core EWTSS use case — **cannot** be rendered by STK's native viewer. They must be drawn as WPF canvas overlays that float above the STK window and manually track its camera projection. This WPF overlay work is estimated at 1–2 additional weeks and is fragile (breaks if the STK window resizes or the user undocks it).

#### ArcGIS Maps SDK — High (≈ 3+ weeks)

The integration complexity with Angular is comparable to CesiumJS. The disqualifying factor is **no native CZML support**. Every STK export must be translated to ArcGIS `Graphic` objects. CZML encodes time-sampled positions as compact arrays:

```json
"cartesian": [0, x0, y0, z0, 60, x1, y1, z1, 120, x2, y2, z2, ...]
```

ArcGIS has no equivalent format. A custom translator would need to parse the CZML time-sample arrays, convert from INERTIAL to geographic coordinates, and create ArcGIS `Track` graphics for each entity — an estimated 1–2 week translation layer that does not exist today and must be maintained whenever STK changes its CZML output format.

**Complexity verdict:**

| | Scenario display | Live data overlays | CZML path | Total effort |
|---|---|---|---|---|
| CesiumJS | 1 week | 2 days (Primitive API) | Native | **~2 weeks** |
| STK native display | 1 week | 1–2 weeks (WPF overlay) | Not applicable | **~2–3 weeks** |
| ArcGIS Maps SDK | 1 week | 2 days | Translation layer (1–2 weeks) | **~3–4 weeks** |

---

### 22.3 Air-Gapped Deployment Constraints
[↑ Table of Contents](#table-of-contents)

#### CesiumJS

Three external services are attempted on startup. All are suppressed with explicit constructor arguments (see Section 8.7). After suppression, CesiumJS is fully self-contained.

**Offline imagery options:**
- **Flat ellipsoid + local raster tiles** (2–3 days): Reuses tile infrastructure already in the system. Gives textured 2D/3D globe without terrain elevation.
- **3D terrain from DTED** (1–2 weeks + GIS specialist): DTED → quantized-mesh conversion. Required only if the customer needs elevation-accurate terrain display in the CesiumJS view. Note: STK always uses DTED for physics computation regardless of what CesiumJS displays; LOS accuracy is not affected by the CesiumJS terrain choice.

#### STK native display

STK ships with a built-in terrain and imagery database populated from its DTED import. **Works offline without any additional configuration.** This is the strongest argument for Options D1/D2 in air-gapped deployments — the terrain problem is solved by the STK installation itself.

#### ArcGIS Maps SDK

Requires pre-packaged tile caches in `.vtpk` or `.tpkx` format, created using ArcGIS Pro or the ArcGIS Online tile export tool. Requires an ArcGIS Pro license to create the packages. The packages must be prepared for the specific geographic coverage area before delivery. Higher operational overhead than either other option.

**Air-gapped verdict:**

| | Offline readiness | Additional effort | Specialist required |
|---|---|---|---|
| CesiumJS (flat imagery) | Configurable | 2–3 days | No |
| CesiumJS (3D terrain) | Configurable | 1–2 weeks | GIS specialist |
| STK native display | Built-in | Zero | No |
| ArcGIS Maps SDK | Pre-packaged tiles | Medium | ArcGIS Pro license |

---

### 22.4 Rendering Performance at Scale
[↑ Table of Contents](#table-of-contents)

#### EWTSS entity count baseline

A realistic worst-case EWTSS scenario contains:
- 5–50 emitter entities (animated position tracks from STK)
- 50 sensor coverage cones/polygons
- 100 DRS hardware status markers
- Up to several hundred real-time signal detection events (per active scan session)

**Peak count: ~300–500 rendered objects.** This is comfortably within all three platforms' 3D rendering limits without optimisation.

The "many entities" concern arises in specific edge cases:
- Dense radar track playback (hundreds of track points per emitter over a long session)
- Signal detection heat maps (thousands of detection events shown simultaneously)
- Historical session replay with all detection events visible

#### CesiumJS performance tiers

| Rendering API | Entity range | 60 fps sustained | Use case in EWTSS |
|---|---|---|---|
| Entity API (default) | ≤ 500 entities | Yes | CZML scenario entities, coverage cones |
| PointPrimitiveCollection | ≤ 50,000 points | Yes | Real-time detection markers from WebSocket |
| BillboardCollection | ≤ 10,000 icons | Yes | DRS hardware status icons |
| Custom GLSL | 100,000+ | Yes | Not needed for EWTSS scale |

2D mode (`morphTo2D()`) reduces WebGL draw calls by ~60% — no atmosphere, no depth sort, no terrain tessellation. All Entity API and Primitive API data remains loaded and functional in 2D. Switching is seamless and reversible within the same viewer instance.

#### STK native display performance

STK is a native C++ application. It handles orbital conjunction scenarios with tens of thousands of satellite objects. At EWTSS scale (< 500 entities), performance is never a concern.

**However**, the performance advantage is partially irrelevant: live data overlays (real-time DRS status, signal detections) are drawn as WPF canvas elements **above** the STK window, not inside it. Those overlays are subject to WPF's rendering limits, not STK's. WPF is capable at this scale but the manual camera-sync code introduces latency proportional to scene complexity.

#### ArcGIS Maps SDK performance

Uses the same WebGL pipeline as CesiumJS. Performance is comparable in 3D SceneView mode. ArcGIS has stronger built-in clustering for dense static feature sets (`FeatureReductionCluster`) — better out-of-the-box for showing 10,000+ static detection events as a summarised layer. For real-time streaming at 100 DRS instances × 10 Hz, ArcGIS requires manual `graphic.setAttribute()` batching (1,000 calls/sec), which is manageable but untested at this scale on this platform.

**Performance verdict:**

| | 3D comfortable range | 2D comfortable range | Auto-2D available | Live data at 100×10Hz |
|---|---|---|---|---|
| CesiumJS Entity API | ≤ 500 | ≤ 5,000 | Native `morphTo2D()` | Yes — separate Primitive API |
| CesiumJS Primitive API | ≤ 50,000 | N/A (not scene-mode-aware) | N/A | Yes — designed for this |
| STK native display | ≤ 10,000+ | ≤ 10,000+ (STK 2D map) | Native (STK built-in) | No — WPF overlay only |
| ArcGIS Maps SDK 3D | ≤ 500 | — | SceneView → MapView (two instances) | Possible, needs batching |
| ArcGIS Maps SDK 2D | — | ≤ 5,000 | N/A | Possible, needs batching |

---

### 22.5 2D / 3D Capability
[↑ Table of Contents](#table-of-contents)

| | 2D flat map | 3D globe | Switch mechanism | Same data in both modes |
|---|---|---|---|---|
| CesiumJS | Native (`SceneMode.SCENE2D`) | Native (default) | `morphTo2D()` / `morphTo3D()` — same viewer, runtime switch | Yes — CZML data persists |
| STK native display | Native (STK 2D map) | Native (STK 3D globe) | STK toolbar button | Yes — same STK scenario |
| ArcGIS Maps SDK | `MapView` class | `SceneView` class | Requires creating both instances and swapping the DOM element | Partial — must sync state manually |

CesiumJS's single-viewer architecture (one `Viewer` instance serves all three modes: 2D, 3D, Columbus View) is the most operationally simple. There is also a **Columbus View** (2.5D tilted perspective) that gives geographic context without full 3D overhead — useful as an intermediate between 2D and 3D when entity density is moderate.

ArcGIS's two-class model (`MapView` for 2D, `SceneView` for 3D) requires instantiating both views and managing which is active. Camera state, selected entities, and data layers must be synchronised manually when switching. This adds ~1 week of extra Angular component complexity relative to CesiumJS.

---

### 22.6 Summary Decision Matrix
[↑ Table of Contents](#table-of-contents)

| Dimension | CesiumJS (Option E) | STK display (D1/D2) | ArcGIS Maps SDK |
|---|---|---|---|
| **Implementation effort** | Medium (2 weeks) | Low+Medium (2–3 weeks with overlays) | High (3–4 weeks with translation) |
| **Air-gapped — flat imagery** | 2–3 days | Built-in (zero) | Medium + ArcGIS Pro |
| **Air-gapped — 3D terrain** | 1–2 weeks + specialist | Built-in (zero) | Medium + ArcGIS Pro |
| **3D entity performance** | ≤ 500 Entity API; ≤ 50k Primitive | 10,000+ native | ≤ 500 SceneView |
| **2D entity performance** | ≤ 5,000 (native switch) | 10,000+ native | ≤ 5,000 MapView |
| **2D/3D switch** | Seamless — same viewer | Seamless — same STK | Two instances, manual sync |
| **Live data integration** | Native WebSocket → Primitive API | Not in STK — WPF overlay only | Possible but needs batching |
| **STK physics accuracy** | Full — CZML is STK's native export | Full — same application | Lossy — requires CZML translation |
| **Additional license cost** | None | STK Components (same as Option B) | ArcGIS Developer license |
| **Browser portability** | Yes — remove Electron shell | No — Windows-only forever | Yes — remove Electron shell |
| **CZML support** | Native — first class | Not applicable | None — custom translation required |
| **Team skill gap** | CesiumJS API (1–2 weeks to learn) | C# WPF + STK COM | CesiumJS + ArcGIS SDK (dual learning) |

---

### 22.7 Decision and Fallback
[↑ Table of Contents](#table-of-contents)

**CesiumJS (Option E) is confirmed as the visualization platform.** It is the only candidate with native CZML support, making it the direct counterpart to STK's Python COM automation — both speak the same data format natively. The offline terrain and imagery concern is resolved by the Martin tile server serving pre-converted MBTiles files bundled in the installer (Section 8.7). 3D terrain with elevation accuracy is confirmed as a requirement (Section 22.8).

**ArcGIS Maps SDK** is eliminated. The CZML translation layer cost is unjustifiable when CesiumJS loads the same data natively.

**STK native display** (Options D1/D2) remains the documented fallback if the STK Components deployment license question resolves unfavourably (Section 18.5). In that case, D1 is preferred over D2 because D1 keeps Python + STK computation on the server and only uses C# for display — partial reuse of the Option E backend is possible.

### 22.8 Terrain Decision — Resolved
[↑ Table of Contents](#table-of-contents)

**3D terrain elevation is a confirmed requirement.** RLOS and LOS coverage cone rendering must display accurately against real-world terrain in the CesiumJS view.

The confirmed approach is the Martin tile server with quantized-mesh MBTiles (see Section 8.7 for full architecture). This decision affects:

- **Installer content**: `data/terrain/terrain.mbtiles` + `data/imagery/imagery.mbtiles` bundled via DVD
- **Startup scripts**: Martin process started before sg-service in `start_drs_services_all.bat`
- **Team composition**: GIS specialist required for one-time DTED → MBTiles conversion pipeline (Section 23)
- **CesiumJS Viewer config**: `CesiumTerrainProvider` pointing to `localhost:8090`, not `EllipsoidTerrainProvider`

No further customer confirmation is needed on this point. The flat-ellipsoid fallback (former Option A) is retired.

---

## 23. Team Composition and Development Staffing
[↑ Table of Contents](#table-of-contents)

### 23.1 Overview
[↑ Table of Contents](#table-of-contents)

The v2 architecture spans five technical domains that do not overlap cleanly: Python async infrastructure, C++ binary protocol parsing, STK COM automation, Angular/Electron/CesiumJS frontend, and GIS data preparation. A single developer cannot carry more than one of these without creating a schedule bottleneck. The recommended team is **six people** — five engineers plus one part-time GIS specialist.

| # | Role | Type | Primary module(s) |
|---|---|---|---|
| 1 | Python Developer 1 (Senior) | Full-time | sg-service |
| 2 | Python Developer 2 | Full-time | drs-server |
| 3 | Python Developer 3 | Full-time | drs-bridge Python layer, infrastructure, installer |
| 4 | C++ Developer | Full-time | drs-bridge C++ parser library |
| 5 | Frontend Developer (Senior) | Full-time | Angular, Electron, CesiumJS |
| 6 | GIS Specialist | Part-time (~6 weeks) | Tile data pipeline |

### 23.2 Per-Role Module Ownership
[↑ Table of Contents](#table-of-contents)

#### Python Developer 1 (Senior) — sg-service

- FastAPI service: scenario CRUD, gaming area → emitter → antenna → modulation profile endpoints
- STK 12 COM automation via `agi.stk12`: scenario lifecycle, access/LOS/RLOS computation, CZML export
- STK ProcessPoolExecutor worker pool: job queuing, result serialisation, error isolation
- RBAC: role/feature/permission enforcement middleware
- Kafka producer: scenario commands dispatched to `drs.session.control` topic
- PDF report generation (scenario summary, access interval tables)
- HTTP endpoint consumed by ResponseRouter in drs-bridge (`GET /scenario/{id}/response`)

#### Python Developer 2 — drs-server

- FastAPI service: query endpoints (historical DRS data), WebSocket push endpoint
- aiokafka consumers for all hardware variants (SRX, MRX, GNSS, JSVUSHF × command + response topics)
- TimescaleDB ORM (SQLAlchemy): `measurements`, `computed_links`, `system_logs` hypertables
- WebSocket broadcast service: fan-out to connected frontend clients on new Kafka messages
- Auth: JWT issue/verify endpoints, token refresh
- Health and metrics endpoints

#### Python Developer 3 — drs-bridge Python layer, infrastructure, installer

- asyncio TCP server: `handle_client` coroutine, stream reassembly buffer (AP-13 fix)
- ResponseRouter: mode switching (RANDOM / SCENARIO / INTEGRATED), `drs.session.control` Kafka consumer
- SessionRegistry: per-hardware session state, replacing global `isRandom` DB flag
- ctypes bridge to C++ parser shared library: `ParserHandle` wrapper class
- ICD codegen tool (`tools/icd_codegen/`): Python + openpyxl + Jinja2 pipeline (Section 20)
- Infrastructure init scripts: Kafka topic creation, TimescaleDB hypertable setup, KRaft initialisation
- Windows batch startup scripts (`start_*.bat`) and Martin tile server process management
- NSIS/WiX installer packaging: bundling Python packages, Martin binary, tile MBTiles, Electron dist

#### C++ Developer — drs-bridge parser library

- `extract_frame(buf, buf_len, out_frame, out_len)` — multi-format frame detector (SDFC / SCD / response headers, AP-14 fix: constants from header, not inline literals)
- `parse_message(frame, frame_len, frame_type)` → JSON string — field decode for all 12+ hardware variants (SRX, MRX, GNSS, JSVUSHF variants, DC, Servo)
- `format_response(json_response, out_buf)` → byte count — binary response serialisation for RANDOM and SCENARIO modes
- CMake build: shared library (`.dll` on Windows) + Python ctypes bindings verification
- AP-13/14/15 remediation in the parser layer: stream accumulation logic, named byte constants, replacing CSV `ast.literal_eval` schemas with generated C++ dispatch tables (from ICD codegen output)
- Unit test suite: golden-frame corpus per hardware variant, fuzz harness for `extract_frame`

#### Frontend Developer (Senior) — Angular, Electron, CesiumJS

- Electron main process: window management, IPC, CSP configuration (`worker-src blob:`)
- Angular application shell: routing, SCSS theming, auth guards
- CesiumJS integration: `CesiumViewerComponent` with NgZone isolation, `ChangeDetectionStrategy.OnPush`, offline terrain + imagery from Martin tile server
- Scenario planner UI: gaming area editor, emitter placement on CesiumJS globe, modulation profile form
- Live monitor view: WebSocket consumer → `PointPrimitiveCollection` for real-time detection markers, `RenderModeService` auto-2D switch at 300/500 entity thresholds
- CZML loader: `CzmlDataSource.load()` for STK-exported coverage cones and access intervals
- 3D/2D toggle control, scene.morphTo2D integration
- Electron packager + ASAR: bundling Angular build, CesiumJS assets, Electron shell

#### GIS Specialist — Tile data preparation (part-time, ~6 weeks)

- Obtain DTED Level 1/2 source data for the area of interest from DISA/NGA
- Run `ctb-tile` to convert DTED → quantized-mesh tile directory (zoom 0–14)
- Package terrain tiles into `terrain.mbtiles` using `mb-util`
- Prepare raster imagery source (GeoTIFF / existing tile cache) → `imagery.mbtiles`
- Configure and validate Martin tile server (`martin.yaml`, content-type headers, tile URL pattern)
- Validate CesiumJS terrain display at representative zoom levels and geographic coordinates
- Hand off MBTiles files to installer packaging (Python Dev 3)
- Document tile preparation procedure and DTED source provenance for project records

### 23.3 Parallel Workstreams
[↑ Table of Contents](#table-of-contents)

The architecture enables four independent workstreams from day one:

```
Week 1–2
├── [Dev1]  sg-service scaffold, STK COM hello-world, first CZML export
├── [Dev2]  drs-server scaffold, TimescaleDB hypertable creation, first aiokafka consumer
├── [Dev3]  drs-bridge asyncio TCP echo server + Kafka infrastructure init scripts
├── [Dev4]  C++ parser library: CMake skeleton + extract_frame golden-frame tests
└── [Dev5]  Electron + Angular scaffold, CesiumJS Viewer with Martin terrain (mock tile data)

Week 3–4
├── [Dev1]  STK scenario lifecycle, RBAC middleware, scenario response endpoint
├── [Dev2]  All Kafka consumer services, WebSocket broadcast service
├── [Dev3]  ResponseRouter + SessionRegistry, ctypes ParserHandle wrapper
├── [Dev4]  parse_message for SRX/MRX/GNSS variants (3 of 12)
└── [Dev5]  Scenario planner UI (CesiumJS entity placement), live monitor skeleton

Week 5–8
├── [Dev1]  Full CZML export, PDF reports, complete RBAC feature set
├── [Dev2]  Auth JWT, query API, WebSocket fan-out optimisation
├── [Dev3]  ICD codegen tool, installer packaging, batch scripts
├── [Dev4]  Remaining variants (JSVUSHF, DC, Servo, MSC), format_response, fuzz harness
└── [Dev5]  CZML loader, PointPrimitive live overlay, 2D/3D auto-switch, Electron packager

[GIS]  Weeks 1–6 (parallel): DTED acquisition, tile conversion, Martin validation
```

### 23.4 Critical Path
[↑ Table of Contents](#table-of-contents)

The following sequence is the minimum chain that must complete before end-to-end testing can begin:

1. **Infrastructure** (Dev 3, Week 1): Kafka topics created, TimescaleDB hypertables up, Martin running
2. **C++ parser API contract** (Dev 4, Week 1–2): `extract_frame` + `parse_message` signatures frozen — all other services depend on this interface
3. **First end-to-end message flow** (Dev 3 + Dev 4, Week 3): SDFC simulator → asyncio TCP → `extract_frame` → `parse_message` → Kafka → drs-server consumer → TimescaleDB row
4. **ResponseRouter round-trip** (Dev 3, Week 4): `format_response` output written back to SDFC TCP connection, both sides in Kafka
5. **STK CZML in CesiumJS** (Dev 1 + Dev 5, Week 5): sg-service exports CZML → frontend loads into viewer over confirmed Martin terrain
6. **Full integration test** (all, Week 8): live SDFC session with scenario mode active, frontend showing real-time detections and CZML coverage cones simultaneously

### 23.5 Specialist Risks and Mitigations
[↑ Table of Contents](#table-of-contents)

| Risk | Owner | Mitigation |
|---|---|---|
| DTED data access delayed (controlled dataset) | GIS Specialist | Start procurement Week 1; Martin can serve a synthetic DEM for dev/test |
| STK COM API undocumented edge cases | Dev 1 | Prototype STK access patterns before committing to API design; use AGI support channel |
| C++ parser variants exceed 12 hardware types | Dev 4 | ICD codegen (Section 20) produces skeletons — only field decode bodies are manual; each new ICD = hours not days |
| CesiumJS terrain artefacts at AoI boundaries | GIS Specialist + Dev 5 | Tile overlap margins in `ctb-tile`; CesiumJS `skirtHeight` parameter |
| Installer size exceeds DVD capacity | Dev 3 + GIS | Bound tile MBTiles to confirmed AoI coordinates before final packaging; measure early |

---

## 24. STK Globe Visualization in a Web Browser
[↑ Table of Contents](#table-of-contents)

A recurring question during architecture review: *can the STK globe renderer itself be embedded in a web browser or web application, with Python as the backend?*

The short answer is **no — not natively** — but the reasons illuminate why Option E is the correct architecture choice, and provide a clear answer when the customer asks about "STK in the browser."

### 24.1 Why STK's Renderer Cannot Run in a Browser
[↑ Table of Contents](#table-of-contents)

STK's 3D globe is a Windows desktop renderer built on DirectX/OpenGL. It has no WebGL or WebAssembly port and cannot be embedded in a browser the way CesiumJS can. The STK Desktop application is a native Windows process; its display is not a web component.

### 24.2 Available Approaches — Comparison
[↑ Table of Contents](#table-of-contents)

| Approach | Feasibility | Detail |
|---|---|---|
| **CesiumJS + STK CZML (Option E)** | ✅ Recommended — already the plan | CesiumJS was created by AGI (the same team that built STK). It shares the same 3D globe physics, CZML animation format, and time-scrubbing model. Python backend computes with STK Engine; STK exports CZML; browser renders with CesiumJS. This is effectively STK-quality visualization in the browser. |
| **STK Data Federate / STK Enterprise Web** | ⚠️ Viable but expensive | Ansys sells server/cloud STK products with built-in web interfaces. Requires enterprise licensing layered on top of the base STK license. Almost certainly out of scope for this project's budget and air-gapped deployment constraints. |
| **VNC / Remote Desktop Stream** | ❌ Not recommended | Run STK Desktop on a server, stream the display to a browser via noVNC or similar. Technically works but produces a brittle, high-latency experience with no proper web integration — the browser becomes a dumb display terminal. |
| **STK Connect over WebSocket Proxy** | ⚠️ Partial capability only | A Python backend relays STK Connect commands to the browser via WebSocket. The browser can query STK state and trigger computations, but still needs its own renderer — which brings the architecture back to CesiumJS. |

### 24.3 Why Option E Is the Right Answer to This Question
[↑ Table of Contents](#table-of-contents)

Option E already is "STK in a browser" in every meaningful operational sense:

- **Same physics engine lineage** — CesiumJS was built by AGI and uses the same orbital mechanics, terrain model, and sensor-geometry primitives as STK's own display.
- **Same data format** — CZML is STK's native export format; CesiumJS was designed to consume it without translation.
- **Same time animation semantics** — CZML's clock packet drives CesiumJS's timeline in exactly the same way the STK Desktop timeline works.
- **Python backend drives STK computation** — sg-service runs STK Engine headless, computes access intervals and coverage, and exports CZML. The browser never needs to talk to STK directly.
- **Browser portability is built in** — removing the Electron shell leaves an Angular app that runs unchanged in any browser.

The only capabilities absent compared to the full STK Desktop are real-time sensor-footprint overlays and live access-interval cones rendered during simulation — both of which are present in the CZML export once the native ExportCZML command (or plugin equivalent) is available.

If the customer or stakeholder asks for "STK visualization in a browser", Option E is the direct and complete answer.

---

## 25. Post-Implementation Findings — MVP1 through MVP4
[↑ Table of Contents](#table-of-contents)

> **Status: written 2026-04-27 after MVP4's structural completion.** This section captures concrete learnings from building four MVPs in sequence, each validating a different architectural choice from §10/§14/§17. The recommendation in §22.7 stands; this section refines it with evidence rather than overturning it.

### 25.1 MVP roadmap recap
[↑ Table of Contents](#table-of-contents)

| MVP | Validates | Approach | Status |
|---|---|---|---|
| MVP1 | Option E baseline | `ExportCZML` plugin chain → Angular + CesiumJS frontend; Python `agi.stk12` backend on port 8001 | Shipped |
| MVP2 | Option E + draw-first authoring | Browser-side scenario authoring via Cesium primitives; Python service computes after submit. STK 13 (`agi.stk13`) on port 8002 | Shipped |
| MVP3 | Option E refined | DataProvider-driven CZML (no `ExportCZML` plugin); Cesium Ion SDK 1.135 for native sensor primitives; Gaussian-lobe antenna visualisation; horizontal/nadir aiming via per-sample quaternions; STK 12 on port 8003 | Shipped |
| MVP4 | Option D2 | C# WPF + STK ActiveX globe; map-first click-to-place authoring; native `StartObjectEditing` drag handles; in-process COM only — no HTTP backend | Shipped |

Both Option E (validated by MVP1/2/3) and Option D2 (MVP4) are now proven viable. The §22.7 recommendation — Cesium primary, Option D2 fallback — stands, but Option D2 is no longer hypothetical.

### 25.2 What MVP3 confirmed about Cesium-based approach (Option E)
[↑ Table of Contents](#table-of-contents)

Findings that materially refine the §22 analysis:

- **DataProviders > `ExportCZML` for clean integration.** MVP1 used the `ExportCZML` Connect command which requires the CZML Exporter plugin MSI and forced JSON post-processing (`_patch_czml_sensors`) to convert STK's extension packets into native Cesium primitives. MVP3 generates CZML directly from STK DataProvider reads. No plugin dependency, no JSON surgery — the CZML pipeline is now ~250 lines of focused Python in `czml_builder_mvp3.py`.
- **Cesium Ion SDK 1.135 is essential for proper sensor rendering.** Stock CesiumJS treats `agi_conicSensor` / `agi_customPatternSensor` / `agi_rectangularSensor` as unknown extensions. The `@cesiumgs/ion-sdk-sensors` package adds the visualizers via `initializeSensors(viewer)`. Without it, sensor cones must be approximated as cylinders (the MVP1 approach via JSON surgery).
- **Cesium widget version pinning is required.** `@cesium/widgets@14.5.0` bundles `@cesium/engine@24` while MVP3 uses engine `^22.x`; both end up in `node_modules` and Cesium's static `ContextLimits` cache gets the wrong values. Pin widgets to `>=14.3.0 <14.4` to match engine 22. Bundle drops 18.21 MB → 10.58 MB after the pin.
- **ANGLE/D3D11 driver quirk requires a workaround.** Some AMD Radeon drivers (observed on the AMD 890M iGPU via ANGLE D3D11) report `0` for several GL caps that Cesium's Context constructor captures into the static `ContextLimits` singleton. We patch each `_maximum*` field to a conservative default (`MAX_TEXTURE_SIZE = 4096`, etc.) at viewer construction. Without this patch, `RenderState.fromCache()` rejects the context with "Width must be less than or equal to the maximum texture size (0)".
- **Local file-based imagery + ellipsoid terrain is sufficient for offline.** MVP3 disables Cesium Ion access (`Ion.defaultAccessToken = ''`) and uses ArcGIS World Imagery via `ArcGisMapServerImageryProvider.fromUrl(...)`. Terrain is `EllipsoidTerrainProvider` (no DTED). Suppression of Ion + offline imagery is ~5 lines of viewer config; no Martin tile server needed for the MVP-scale demo.
- **3D model textures crash on the same driver path.** Even with the `ContextLimits` patch, glTF texture upload failed on the AMD Radeon 890M. MVP3 falls back to a `point` primitive (coloured dot) instead of the AGI aircraft glTF. This is a known Cesium issue, not a bug in our code; revisit on better-tested GPUs.
- **`MockStkService` has been worth the maintenance cost.** Synthesises a Kashmir-pattern scenario without STK. Lets frontend developers iterate without a license. MVP3 keeps it — three weeks of work but pays back every dev who can't get STK installed locally.

### 25.3 What MVP4 + MVP4.5 confirmed about STK-native approach (Option D2)
[↑ Table of Contents](#table-of-contents)

The core finding: **Option D2 is achievable with sample-quality smoothness once a handful of non-obvious gotchas are addressed.** STK's own C# samples (`3DObjectEditing/Form1.cs`, `PolygonDrawing/Form1.cs`, `STKProTutorial/Form1.cs`, `Tutorial/Form1.cs`, `Events/Form1.cs`, `PlaceFinder/Form1.cs`, `RT3EngineApplication/MainForm.cs`, `MarsProbe/Form1.cs`, distributed with the STK 12 install at `bin/CodeSamples/CustomApplications/CSharp/`) embody the canonical patterns; deviations from them caused every performance and correctness issue MVP4 hit. The **Constelli `EWTSS_CSP_POC` reference repo** (branch `scenario-tree-emitters`, also WPF + WindowsFormsHost) was used as a second canonical reference during MVP4.5 — its explicit choices (no permanent COM event subscriptions, `Position.AssignGeodetic` directly, `RefreshStkViews()` after specific operations only) corrected several patterns we'd carried forward from MVP4.

#### 25.3.1 Bootstrap and threading

- **`AgSTKXApplication` must be created before any `AgStkObjectRoot`.** Direct construction of `AgStkObjectRoot` without first creating the STK X application returns `HRESULT 0x80040204` (license / engine subsystem not initialised). Every sample's `Main()` does `new AgSTKXApplication()` before any other STK call. MVP4 achieves this via a static lazy field in `StkRootService.Root`.
- **STA apartment threading is mandatory.** STK COM is `ThreadingModel = Apartment`. NUnit on .NET defaults to MTA; integration tests must be marked `[Apartment(ApartmentState.STA)]` or every COM call fails with the same `0x80040204`.
- **`Application.EnableVisualStyles()` and `SetCompatibleTextRenderingDefault(false)` are required even for a pure-WPF entry point.** `WindowsFormsHost` relies on the WinForms message-pump being themed. Skipping these (because "we're WPF, we don't need them") leaves hosted ActiveX controls in a slow legacy GDI paint path.
- **Don't construct `AgSTKXApplication` twice.** It's a process singleton, so a second `new` returns the same instance — but the call still goes through `CoCreateInstance`, which on first call triggers full engine init. Doing it twice (once in the COM service for tests, once in the display host) approximately doubles cold-start time. Centralise the bootstrap in one place.

#### 25.3.2 Rendering performance

- **GPU preference must be set explicitly for the .NET app.** Windows defaults new .NET WPF executables to the integrated GPU. STK's globe is DirectX-rendered; on iGPU the interaction is sluggish regardless of code. Manual fix: `Settings → System → Display → Graphics → Browse → Sg.App.exe → Options → High performance`. Programmatic fix (deferred for MVP4): write `"GpuPreference=2;"` to `HKCU\Software\Microsoft\DirectX\UserGpuPreferences\<exe-path>` on first launch and prompt the user to relaunch.
- **WPF `WindowsFormsHost` adds ~1–3 ms input-pipeline latency** vs pure WinForms hosting per `MouseMove`. The marshalling chain ActiveX → COM event sink → WinForms message pump → `HwndSource` → WPF dispatcher is structural to WPF airspace and cannot be eliminated short of moving the globe to a top-level WinForms window. STK's own samples are pure WinForms and feel snappier as a direct consequence. For most users the gap is acceptable; for an Insight-fidelity feel, a top-level WinForms host is the only option.
- **Skip `PickInfo(x, y)` calls outside placement modes.** Pan/zoom fires `MouseMoveEvent` 60–120 ×/sec; calling `PickInfo` on every move (even with a 16 ms throttle) is enough to make pan/zoom feel laggy. MVP4 early-returns from `MouseMove` when `_controller.Mode is not Placing*`.
- **Use the `AGI.STKX.Controls.Interop` PIA, not `AxAGI.STKX.Interop`.** STK ships two PIA wrappers around the same ActiveX controls. Every sample uses the Controls PIA. The Ax PIA is the older auto-generated wrapper; the Controls PIA has cleaner connection-point plumbing. Both are referenced (the Ax PIA still supplies `IAgUiAx*Events_*EventHandler` delegate types), but consumed code uses `using AGI.STKX.Controls;`.
- **Call `manager.Scenes[0].Render()` after every primitive update.** STK doesn't auto-render on `SetCartographic` mutation; it renders on its internal animation tick. For interactive primitives (rubber-band line during placement), explicit `Render()` after each update is required for the preview to feel real-time.

#### 25.3.3 STK COM API gotchas

A non-trivial fraction of MVP4's bugs were API-shape mismatches between assumed and actual STK 12 PIA exposure. The samples are authoritative; reflection (`Assembly.LoadFrom + GetExportedTypes`) is the fallback when sample coverage is sparse. Two cross-reference review passes — one mid-implementation, one after Tasks 8–17 shipped — found **seven correctness bugs** between them. The complete list:

| Wrong assumption | Reality | Symptom if missed |
|---|---|---|
| `IAgDisplayColor` exists for typed color access | Doesn't exist. Use `IAgVeGfxAttributesBasic` accessed via `_obj.Graphics.Attributes` | Compile error or silent fallback to default color |
| Aircraft `Graphics.Attributes` is `IAgVeGfxAttributesBasic` directly | Must call `_obj.Graphics.SetAttributesType(AgEVeGfxAttributes.eAttributesBasic)` first; without it the cast fails silently | Color edits silently dropped |
| Aircraft graphics has `.Show` for path visibility | Property is `IsObjectGraphicsVisible` | Compile error |
| Facility position uses `eGeodetic` / `IAgGeodetic` | Use `ePlanetodetic` / `IAgPlanetodetic` (samples uniformly use the planetodetic interface for facilities) | Compile error or wrong-position behaviour |
| `IAgPosition.AssignGeodetic(lat, lon, alt)` writes back to STK | The mutation is on a snapshot; must call `_obj.Position.Assign(geo)` to write back | Position changes silently discarded |
| `Children.Unload(type, name)` while a managed RCW still references the object is safe | Race: STK may close the object while the RCW still resolves. Don't call `Marshal.ReleaseComObject` on the RCW before `Unload` — let GC handle it (samples do this) | Sporadic COM `0x80020009` errors |
| `OnObjectEditingApply` event means "user committed; exit edit mode" | It fires when STK's Apply button commits an in-progress edit. Edit mode stays active. Treat as a no-op (or "refresh-from-COM" trigger) | Edit mode prematurely exits after first Apply |
| `OnObjectEditingStop` is a cancel signal | It's the commit signal — STK's OK button. Treat as `ApplyEdit()` in the controller | Confirmed edits silently rolled back |
| `StopObjectEditing(false)` cancels the edit | `(false)` commits, `(true)` cancels — the boolean argument is `revert` not `apply`. **Critical bug** because users pressing Esc to discard changes were unwittingly committing them | Cancel/Esc commits the edits the user wanted to discard |
| `StartObjectEditing("*/Aircraft/Aircraft1")` works | Use full registry path `/Application/STK/Scenario/{name}/Aircraft/Aircraft1` per `3DObjectEditing/Form1.cs:10` | Edit mode silently fails to start |
| `_IAgCoverageDefinition` is interchangeable with `IAgCoverageDefinition` | The underscore-prefixed interface is the hidden dispatch interface; use the public `IAgCoverageDefinition` for casts and field declarations | Inconsistent runtime behaviour vs `StkRootService` |
| `IAgVeWaypointsElement.Latitude` / `.Longitude` are `double` | Typed as `object` (COM VARIANT) — cast to `double` explicitly | `InvalidCastException` at runtime |
| Time format `"dd MMM yyyy HH:mm:ss.fff"` is culture-stable | `MMM` resolves to current culture's month abbreviation. STK expects English. Pass `CultureInfo.InvariantCulture` for both `ToString` and `Parse` | Scenario creation fails on non-English Windows |
| `IAgStkGraphicsPolylinePrimitive.SetCartographic` accepts degrees | Expects **radians** (per `PolygonDrawing/Form1.cs` which feeds `WindowToCartographic` output without conversion). Convert from degrees | Preview primitives render near 0,0 instead of where the user clicked |

This list is exhaustive for what MVP4 hit. None of these were obvious from documentation alone — every fix came from comparing our code against the samples. **Plan for two cross-reference passes when integrating STK COM**; a single pass under-samples (mid-implementation pass found 3 bugs; second pass found 4 more).

#### 25.3.4 Map-first interaction (addendum)

The original MVP4 design called for form-based authoring (right-pane property panel with Apply button). MVP4 pivoted mid-execution to **STK-Insight-style map interaction**: click-to-place new entities with rubber-band preview, double-click to finalize, drag handles on existing entities via `StartObjectEditing`. This was viable because the ActiveX controls expose all four required capabilities natively:

| Capability | API | Sample |
|---|---|---|
| Click → lat/lon | `control.PickInfo(x, y)` → `IAgPickInfoData.Lat/Lon/Alt + IsLatLonAltValid` | `Events/Form1.cs:163-185` |
| Pick entity identity | Same `PickInfo` → `.ObjPath + IsObjPathValid`; resolve via `root.GetObjectFromPath(path)` | `PlaceFinder/Form1.cs:250-258` |
| Temporary placement visuals | `IAgStkGraphicsPolylinePrimitive` + `IAgStkGraphicsPointBatchPrimitive` added to `manager.Primitives`; updated via `SetCartographic` (radians) | `PolygonDrawing/Form1.cs:22-52, 169-210` |
| Drag handles | `control.StartObjectEditing(fullStkPath)` + lifecycle events `OnObjectEditingStart/Apply/Stop/Cancel`. STK renders handles itself | `3DObjectEditing/Form1.cs:66-122` |

No WPF overlay was needed (the §22.2 fragility warning didn't apply once we used `IAgStkGraphicsPrimitives` for previews). Three structural choices made the pivot work:

1. The form-based property panels stayed — they handle non-spatial fields (color, name, sensor cone angle, FOM type) AND act as fallback editors for spatial fields when the user prefers exact numeric input over map clicks.
2. The interaction state machine lives in a domain-layer `IInteractionController` interface, not wired directly into mouse handlers. This kept the controller unit-testable with a fake `IStkRootService`.
3. Drag editing uses STK's native `StartObjectEditing` rather than custom hit-testing on graphics primitives. STK provides the visible drag handles, the live coordinate readout, and the apply/revert UX for free.

The map-first pivot is documented in `mvp4-map-first-interaction-addendum.md` and added ~1.5 weeks to MVP4's timeline (5 → 6.5 weeks). The result is the user-facing fidelity matches STK Insight closely.

#### 25.3.5 MVP4.5 corrections and additional STK COM realities

MVP4.5 (DTO boundary refactor + perf and interaction polish, on branch `feat/mvp4.5-dto-boundary`) ran an extended smoke-test cycle against real STK 12 with the Constelli `EWTSS_CSP_POC` reference repo as a second canonical example. Six findings emerged that weren't visible during the original MVP4 implementation. Several supersede entries in §25.3.3 — flagged inline below. Pattern source for each: empirical testing against STK 12 + cross-check against the reference repo's `Infrastructure/Services/StkEngineService.cs` and `Presentation/Views/Scenario/ScenarioEditorPage.Stk.cs`.

**Permanent COM event subscriptions on STK ActiveX controls cost more than the handler body.** §25.3.2 noted that calling `PickInfo` on every `MouseMoveEvent` is expensive. The deeper finding: **even subscribing to the events at all** (the `+= ...` itself, with no work inside the handler) forces COM connection-point marshalling per render tick. On this STK 12 build with WPF + `WindowsFormsHost`, every permanent subscription added a measurable ~1–2 second stall after every pan-release gesture. The handler's early-return doesn't help — the dispatch cost is in the bridge, not the body.

The fix: subscribe **on-demand only**. Mouse events (`MouseDownEvent`, `MouseMoveEvent`) only while in placement modes (`PlacingFacility/Aircraft/AreaTarget`); editing events (`OnObjectEditingStart/Apply/Stop/Cancel` + a `MouseUpEvent` bridge — see below) only while in `EditingEntity`. Idle pan has zero subscriptions. Reference repo follows this pattern (their `MouseDownEvent` subscription is commented out at `ScenarioEditorPage.Stk.cs:85`). MVP4.5 dispatched a "WinForms rewrite" diagnostic branch (`feat/mvp4.6-winforms-host`) before isolating this — pure WinForms hosting did not fix pan; removing the subscriptions did. **Architecture (WPF vs WinForms) is not the cause of sticky pan; permanent subscriptions are.**

**STK fires `DblClick` on every single click, not just real double-clicks.** Verified by diagnostic logging: each `MouseDownEvent` is followed ~12 ms later by a synthetic `DblClick` at the same screen coords, regardless of click cadence. STK appears to use `DblClick` as a "click completed" notification rather than the standard meaning. Subscribing to it produces a spurious finalize event on every click. Don't subscribe to STK's `DblClick`. Detect real double-clicks from `MouseDownEvent` timestamp deltas using `System.Windows.Forms.SystemInformation.DoubleClickTime` and `DoubleClickSize` if you need them at all (we ended up not relying on DC; see Enter-key finalize below).

**Right-click on the 3D globe is consumed by STK's camera operations and never raises `MouseDownEvent` with `button == 2`.** STK's default mouse mode binds right-drag to camera operations and consumes the input. The COM event isn't propagated to subscribers. A `right-click-to-finalize` gesture is therefore unreachable on this control. MVP4.5 routes finalize via **keyboard `Enter`** at the WPF Window's `OnPreviewKeyDown` (window-level so focus inside the `WindowsFormsHost` doesn't swallow it). `Esc` cancels. Status hints updated to advertise the keys.

**Drag handles do NOT auto-commit edits to the entity COM state.** This was the most expensive miss. `_globe3D.StartObjectEditing(stkPath)` shows handles. User drags. User releases. The visual handle is at the new position — but `IAgFacility.Position` / `IAgVePropagatorGreatArc.Waypoints` etc. still hold the **pre-drag values**. Until something explicitly commits, `GetXxx` returns OLD state and any panel-driven `Apply` pushes OLD values back, undoing the drag. Symptom: drag a waypoint, click Apply, watch the path snap back unchanged.

The commit signal is `_globe3D.ApplyObjectEditing()`. It commits the in-progress drag to COM and fires `OnObjectEditingApply` with the new state already synced. Per §25.3.3 those events fire only from programmatic methods, never from user mouse interaction directly. MVP4.5 bridges user drag-and-release to `ApplyObjectEditing` via a `MouseUpEvent` subscription that's only active during `EditingEntity` mode. Once committed, the panel's `Refresh()` reads the new state correctly and `MarkDirty()` enables Apply.

**Explicit `Refresh()` after `StartObjectEditing` is required for handles to paint.** STK has the handles internally active immediately after `StartObjectEditing` returns OK, but the WindowsFormsHost doesn't repaint until the next pan/zoom — handles stay invisible until the user happens to interact. The reference repo follows `StartObjectEditing` with `RefreshStkViews()`. We do the same in `_onModeChanged`'s `EditingEntity` branch.

**SUPERSEDES §25.3.3 row 5 — facility position uses `Position.AssignGeodetic(lat, lon, alt)` directly.** The original §25.3.3 entry stated:
> `IAgPosition.AssignGeodetic(lat, lon, alt)` writes back to STK / The mutation is on a snapshot; must call `_obj.Position.Assign(geo)` to write back

MVP4.5 testing on real STK 12 found the `ConvertTo(ePlanetodetic) + Assign(geo)` pattern **silently no-ops** for facility placement. The facility stays at STK's default position (lat 40.04 °N, lon 75.60 °W — AGI's HQ in Exton, PA) regardless of what's written to the snapshot. The reference repo's `StkEngineService.cs:255,263` uses `Position.AssignGeodetic(lat, lon, alt)` directly (single call) and works. The MVP4 pattern was carried forward from the deleted `ComFacilityBackend.AssignPlanetodetic` setter, which used the per-property-write idiom from MVP4 — the placement flow likely never actually exercised it under conditions that exposed the silent failure. **Use `Position.AssignGeodetic` directly. The `ConvertTo + Assign` snapshot-and-writeback pattern documented in §25.3.3 should be considered untrusted on this STK build.**

**Tree rebuild on every `ScenarioChanged` blanks the property panel during edits.** Subtle interaction-with-state-management bug. ObservableCollection-backed tree views typically rebuild on every backend mutation event. If the user is in `EditingEntity` mode and a drag-edit fires `ScenarioChanged` (via the MouseUp → `ApplyObjectEditing` → `OnObjectEditingApply` chain), a naive `RebuildFromService` clears `SelectedNode` → `SelectionChanged` event fires → the property panel sets `Current = EmptySelectionViewModel` → **the active edit panel disappears mid-edit**, and the gate that's supposed to mark dirty after refresh fails because Current is now empty. Visible to the user as: "I clicked the globe and the right pane went blank."

Fix: short-circuit `RebuildFromService` when the path **set** is unchanged. Drag-edits change entity *content* but never the tree's *structure*, so the tree should not rebuild for those events. Add/remove entity (path set differs) still triggers a real rebuild and re-applies the prior selection if the path still exists.

**Refresh-on-every-ScenarioChanged caused a placement freeze under load.** An attempt to mirror the reference repo by subscribing `ScenarioChanged → _globe3D.Refresh(); _map2D.Refresh()` deadlocked the app after a multi-waypoint placement. `FinalizeAircraft` fires `ScenarioChanged` twice (`AddEntity` + `UpdateAircraft`), each triggering 2 forced-synchronous `Refresh` calls, plus per-click `Render()` in the placement-preview path. STK's ActiveX render pipeline does not reliably reenter under that load. The reference repo calls `Refresh` **explicitly** after specific operations (`LoadVDF`, `CreateOrUpdate*`) — not via a generic subscription. Match that pattern: explicit `Refresh` only where it's genuinely needed (e.g. after `StartObjectEditing` for handles); STK auto-renders the rest of the time.

**Diagnostic instrumentation pays for itself.** Several of the findings above turned multi-day investigations into single-iteration fixes once `Debug.WriteLine` + file-append logs were added at: `MouseDown` entry (before `PickInfo`), mode transitions, editing-event firings, and `ScenarioChanged` subscriber gates. Without the trace, every guess was speculative; with it, each step's actual behaviour was visible. Recommended pattern: build a diag-log helper from day one, gated by an env variable (e.g. `MVP4_DIAG=1`), so verbose tracing is off in production but instantly available when investigating. MVP4.5 logs to `Desktop\stk-debug.log` for trivial sharing.

### 25.4 Updated recommendation vs §22.7
[↑ Table of Contents](#table-of-contents)

§22.7 confirmed CesiumJS as the visualization platform with Option D2 as the documented fallback. After building both:

| Choice driver | Favoured option |
|---|---|
| Customer needs browser delivery (non-Windows endpoints, web-app deployment) | **Option E (Cesium)** — the only candidate. MVP3 proves it. |
| Customer wants STK-Insight-fidelity interaction (drag-handle waypoint editing, native FOM grid colouring, exact STK timeline behaviour, keyboard-accelerated authoring) | **Option D2 (STK native)** — MVP4 proves the experience matches Insight closely. |
| Customer wants both: web delivery AND Insight-fidelity editing for power users | **Both, deployed in parallel.** Same Python sg-service backs both; Cesium for the web UI, MVP4-style WPF app for the operator workstation. The licensing analysis in §18 already covers two STK Runtime seats. |
| Performance is the dominant concern on a known-good GPU workstation | **Option D2** is faster per-frame in raw rendering when the dedicated GPU is wired up; CesiumJS is faster to load and zero-install. |
| Cross-team development velocity is the dominant concern | **Option E**. The Python developer plus Angular developer can each iterate independently. Option D2 collapses much of the work onto a single C# developer who must understand WPF + COM + STK simultaneously. |

The §22.7 recommendation (Option E primary, Option D2 fallback if licensing forces it) is unchanged — but the MVP4 evidence raises Option D2 from "documented fallback" to "validated alternative we could ship". For air-gapped Windows-only deployments where the operator doesn't need a browser, Option D2 is now the higher-fidelity choice.

**Option D2 + Option E as a hybrid deliverable** is specced separately in [`hybrid-frontend-design.md`](hybrid-frontend-design.md). That design takes MVP4.5's C# WPF as the primary deliverable today, and adds an ASP.NET Core server (`Sg.Server`) plus an Angular + CesiumJS SPA (`Sg.Web`) as future additive deliverables — sharing the same `Sg.Domain` (DTOs, `IScenarioBackend`, `StkScenarioBackend`) without inserting any HTTP layer between the WPF process and STK. Performance of the desktop deliverable is unchanged; the browser path is a separate process with its own in-process STK, gated by the same one-engine licence rule. Phased delivery (read-only browser viewer first, full authoring browser later) lets partial value ship without committing to the full SPA upfront.

### 25.5 Code complexity — Hybrid (D2 + future E) vs Option E head-to-head
[↑ Table of Contents](#table-of-contents)

The Hybrid design ([`hybrid-frontend-design.md`](hybrid-frontend-design.md)) keeps MVP4.5's C# WPF as the primary deliverable today and adds an ASP.NET Core server + Angular/CesiumJS SPA later as opt-in second deliverable, sharing the same `Sg.Domain`. The natural question is: *if I'm going to end up with browser support either way, isn't the hybrid just Option E with extra steps?*

The honest answer is **it depends on whether Mode B is ever actually built**. The two phases of the hybrid have different complexity profiles, and conflating them obscures the real choice.

#### Code surface comparison

| Area | Option E (Cesium primary) | Hybrid (Mode A only — today) | Hybrid (Mode A + Mode B fully built) |
|---|---|---|---|
| Frontend projects to build & maintain | 1 — Electron + Angular + CesiumJS SPA | 1 — `Sg.App` (WPF) | **2** — `Sg.App` + `Sg.Web` |
| Backend projects for scenario authoring | 1 — Python `sg-service` (FastAPI + `agi.stk12`) | 1 — `Sg.App` (in-process STK) | **2** — `Sg.App` + `Sg.Server` |
| Languages in the scenario / authoring stack | Python + TypeScript | C# only | C# + TypeScript |
| Cross-process serialisation in the data path | Always present (Python ↔ browser via REST + WebSocket) | None (in-process method calls) | Same as Option E (HTTP for Mode B; Mode A still in-process) |
| STK integration shape | Python `agi.stk12`, out-of-process, `ProcessPoolExecutor` for parallel compute, COM thread juggling | C# COM in-process, native typing, no process pools — but ~15 documented STK COM gotchas (§25.3.3 + §25.3.5) | Same as Mode A for the desktop and the server; double the surface that has to be regression-tested |

(`drs-server`, `drs-bridge`, and the C++ parser libs are unchanged across all three columns. They're concerned with hardware telemetry, not scenario authoring.)

#### Where each option pays its complexity tax

**Option E pays once, evenly distributed.** Python `sg-service` is medium-effort. STK in Python has known patterns (the `agi.stk12` package wraps the COM model). MVP3 already validated the Cesium-Angular integration. Browser is the only authoring UI — no duplication, one bug-fix lands in one place, one test surface, one mental model for new developers.

**The hybrid pays in two distinct phases:**

- **Today (Mode A only):** simpler than Option E in raw line count and conceptual surface. C# is one language for both UI and STK. No Electron, no Python service for scenario authoring, no out-of-process STK calls, no JSON serialisation in the per-frame path. MVP4.5 already shipped this. **For just the desktop deliverable, this is genuinely cheaper than Option E.**
- **Tomorrow (when Mode B ships):** the cost re-emerges. You now have **two authoring UIs to keep behaviourally consistent forever after.** Place-Aircraft logic exists in WPF MVVM and again in Angular components. Drag-edit semantics exist as STK ActiveX integration and again as Cesium primitive hit-testing. A bug fixed on the desktop side has to be re-fixed on the browser side. The test matrix doubles. Defence-grade audit narrative widens (two UIs to certify).

#### Debuggability

| | Option E | Hybrid Mode A | Hybrid Mode A + Mode B |
|---|---|---|---|
| Stack | Python tracebacks + browser DevTools | C# debugger across WPF + STK COM | All three above + ASP.NET Core endpoints + SignalR |
| Familiar to defence-team developers | Mostly yes | Less so (WPF + COM are specialist) | Specialist for desktop side, familiar for server-browser side |
| Hard bug class | Python COM threading edge cases | STK silent failures (§25.3.5: `Position.Assign` no-op, drag handles not auto-committing, `OnObjectEditingApply` not firing on user mouse) — already paid in MVP4.5 | Both above plus contract-drift between C# DTOs and TS mirror types |

#### Performance

| | Option E | Hybrid Mode A | Hybrid Mode B |
|---|---|---|---|
| Per-interaction latency | REST round-trip per scenario edit | In-process method call (microseconds) | Same as Option E |
| Per-frame rendering | CesiumJS WebGL via Electron Chromium | STK ActiveX direct DirectX | CesiumJS via browser |
| Scrub responsiveness | Browser-grade | Native (matches STK Insight) | Browser-grade |
| Smooth pan validated | MVP3 did pan well; CesiumJS is mature | MVP4.5 validates only after the on-demand event subscription discipline (§25.3.5) | Inherits Option E's profile |

#### Honest summary

| Question | Answer |
|---|---|
| Is the Hybrid simpler than Option E for the current C# WPF deliverable alone? | **Yes** — one language for UI + STK + compute; no Electron, no Python service for scenarios, no out-of-process STK. |
| Is the Hybrid simpler than Option E if Mode B is fully built out? | **No.** Mode A + Mode B is **more** complex than Option E. The shared `Sg.Domain` saves the contract tax but not the UI tax — two authoring UIs is two authoring UIs. |
| Where does the Hybrid's complexity advantage come from? | **Optionality.** Mode A ships today (cheaper than Option E for desktop-only). You only pay Mode B's complexity if a customer actually requests browser delivery and is willing to fund it. If Mode B is never built, the Hybrid stays simpler than Option E forever. |
| Where is the Hybrid's worst case? | If both modes are built AND maintained AND in production. Strictly larger codebase than Option E for the same user-visible feature set. |

**Practical framing for stakeholders:** the Hybrid is a *bet that Mode B might never be built* — and that's fine, because Mode A on its own is genuinely cheaper than Option E for desktop-only delivery. If Mode B does get built, the Hybrid pays a duplicate-frontend tax that Option E avoids entirely. The decision criterion is whether STK-Insight-grade fidelity for power users on Windows workstations is worth potentially paying that tax later. For a customer who explicitly wants both desktop power-user authoring and broad browser viewing, the Hybrid wins on capability and ties (or loses) on complexity. For a customer comfortable with browser-only, Option E is the simpler, faster delivery.

### 25.6 Lessons that apply regardless of frontend choice
[↑ Table of Contents](#table-of-contents)

- **STK's own C# samples are the authoritative reference.** Documentation and IntelliSense are necessary but not sufficient — every gotcha in §25.3.3 came from comparing code against samples after a bug surfaced. Make `bin/CodeSamples/CustomApplications/CSharp/` the first place to look when an STK COM call doesn't behave as expected. Reflect (`Assembly.LoadFrom`) when sample coverage is sparse for a specific interface.
- **Cold-start times are real.** STK Engine takes 15–60 s on first construction (`new AgSTKXApplication()` followed by `new AgStkObjectRoot()`). Both Option E (Python `agi.stk12`) and Option D2 (C# COM) hit this. A splash window with a status text is not optional UX polish — it's required to prevent users from thinking the app froze.
- **STA threading + COM teardown deserve a watchdog.** STK's COM teardown can hang on an in-flight calculation. MVP4's `App.OnExit` includes a 5-second daemon thread that calls `Environment.Exit(0)` if graceful disposal hasn't completed. MVP1/3 had a similar pattern in Python (forcing `os._exit` from a daemon thread on uvicorn shutdown). Plan for this from day one.
- **Time format must be culture-invariant.** `dd MMM yyyy HH:mm:ss.fff` with `CultureInfo.InvariantCulture` is the safe pattern. STK refuses anything else; non-English Windows installs will fail without `InvariantCulture`. Apply this both ways: `ToString` for writes, `Parse` for reads (with `DateTimeStyles.AssumeUniversal`).
- **Pin the engine's `DateFormat` unit preference.** Call `Root.UnitPreferences.SetCurrentUnit("DateFormat", "UTCG")` after `NewScenario` so `IAgScenario.StartTime`/`StopTime` returns parseable strings. Without this, a previously-loaded scenario can leave the engine in a different time format and read-back fails.
- **Mocking matters even for headless services.** MVP3's `MockStkService` and MVP4's `FakeStkRootService` both let development continue when the STK license server is down. Build an `IStkService` interface from day one; concrete COM implementation behind one impl, in-memory fake behind another for tests + offline dev. **Don't expose the mock as a runtime fallback** (we did in MVP1 and the team got burned when mocks drifted from real STK behaviour); use it strictly for tests.
- **Validate against samples twice.** A single review pass under-samples; budget two. MVP4's first cross-check pass found 3 bugs (color `SetAttributesType`, `OnObjectEditing` event direction, missing `Render`). The second cross-check pass found 4 more (facility `Position.Assign` writeback, `eGeodetic` vs `ePlanetodetic`, `_IAgCoverageDefinition` vs public interface, `StopObjectEditing` argument inverted). Two of those four were Critical-severity correctness bugs that the first pass had clearly missed.
- **GPU preference is part of deployment, not just dev environment.** Whatever option is shipped, the `.exe` must be associated with the dedicated GPU in Windows Graphics Settings (or via the `HKCU\...\UserGpuPreferences` registry write). This applies equally to Option E (Cesium uses WebGL → DirectX via the Electron renderer) and Option D2 (STK uses DirectX directly). Plan an installer step.
- **Don't subscribe to STK ActiveX events you don't need (Option D2 only).** Per §25.3.5: every permanent subscription on `MouseDownEvent` / `MouseMoveEvent` / `OnObjectEditing*` / `DblClick` adds COM marshalling overhead per render tick, manifesting as a 1–2 s post-pan stall regardless of what the handler does. Subscribe per-mode and unsubscribe on return to Idle. Reference repo (`EWTSS_CSP_POC`) demonstrates the zero-permanent-subscription pattern.
- **Build a diagnostic-log flag from day one (Option D2 only).** Per §25.3.5: an `MVP4_DIAG=1`-gated trace helper that logs mode transitions, mouse events, and editing-event firings to a known location turns multi-day investigations into single-iteration fixes. Default off, zero overhead when disabled, trivial to flip on when investigating.
- **STK COM event semantics deserve their own integration test.** Per §25.3.5: `DblClick` fires per single-click; `OnObjectEditingApply/Stop` fire only from programmatic `ApplyObjectEditing()`/`StopObjectEditing()` (not user drag); right-click is consumed by camera ops; drag handles don't auto-commit to entity COM state. None of these are documented; all of them caused user-visible bugs in MVP4.5. Plan an integration smoke that exercises each event source and asserts the actual firing pattern before relying on it for app logic.
- **Validate against the reference repo, not just samples.** STK's own `bin/CodeSamples/CustomApplications/CSharp/*` are the canonical source for STK API usage. The Constelli `EWTSS_CSP_POC` reference repo (branch `scenario-tree-emitters`) is the canonical source for the "running EWTSS-style C# WPF + STK app" pattern — it gets architectural choices right (no permanent subs, `RefreshStkViews()` after specific operations only, `Position.AssignGeodetic` for facilities) that the samples don't directly exhibit. Use both as references; they catch different classes of issue.
