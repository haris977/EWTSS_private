# EWTSS System Architecture

## Overview

EWTSS is a distributed, event-driven platform composed of three Python FastAPI services, a DRS hardware bridge, and an Angular frontend. Apache Kafka is the central integration bus — every hardware data path flows through it before reaching persistent storage.

---

## Component Map

```
┌────────────────────────────────────────────────────────────────────┐
│                        Operator Browser                            │
│                   Angular SPA  (port 4200)                         │
│  OpenLayers map · scenario builder · report viewer · user mgmt     │
└──────────────────────────┬─────────────────────────────────────────┘
                           │ HTTP / WebSocket (JWT bearer)
                           ▼
┌──────────────────────────────────────────────────────────────────┐
│               ewtss-backend  (FastAPI · port 9000)               │
│  • Scenario management  (gaming areas → emitters → antennas)     │
│  • User / Role / Feature / Permission RBAC                       │
│  • Kafka producer control  (start_stream / stop_stream APIs)     │
│  • Map tile server  (local GeoTIFF tiles)                        │
│  • DB backup / restore                                           │
│  • WebSocket log streaming                                       │
└──────┬──────────────────────────────────────┬────────────────────┘
       │ Produces messages                    │ MySQL (scenarios,
       ▼                                      │  users, roles)
┌──────────────────────────────────────┐      │
│       Apache Kafka Broker            │      │
│                                      │      │
│  Topics (per hardware variant):      │      │
│   srx_ui_data      mrx_ui_data       │      │
│   gnss_ui_data     jsvushf_srx_data  │      │
│   jsvushf_mrx_data jsvushf_gnss_data │      │
│   dc_data          servo_data        │      │
└──────┬───────────────────────────────┘      │
       │ Consumes messages                    │
       ▼                                      │
┌──────────────────────────────────────┐      │
│       drs_server  (FastAPI · 8000)   │      │
│  • 18 Kafka consumer threads         │      │
│  • Persists FF/FH/Burst/Health rows  │      │
│  • REST API for frontend queries     │◄─────┘
│  • JWT auth                          │
└──────────────────────────────────────┘
       ▲
       │ Produces messages (TCP → Kafka)
┌──────┴───────────────────────────────┐
│        drs_bridge  (standalone)      │
│  • TCP servers per hardware variant  │
│  • Parses binary device protocol     │
│  • Two modes per variant:            │
│    random_* — synthetic data gen     │
│    sg_* / msc_* — real hardware      │
└──────────────────────────────────────┘
       ▲
       │ Binary TCP (proprietary protocol)
┌──────┴───────────────────────────────┐
│  Physical Hardware Devices           │
│  SRX · MRX · GNSS · JSVUSHF variants│
└──────────────────────────────────────┘
```

---

## Data Flow — Ingest Path

```
Hardware device
  → TCP connection to drs_bridge TCP server
  → Protocol bytes parsed against device constants
  → Structured Python dict serialised to JSON
  → Produced onto Kafka topic  (e.g. srx_ui_data)
  → drs_server Kafka consumer thread picks up message
  → Message deserialised and validated
  → SQLAlchemy ORM writes row to MySQL  (FF / FH / Burst / Health)
  → drs_server REST endpoint serves rows to Angular frontend
```

## Data Flow — Scenario / Control Path

```
Operator creates scenario in Angular UI
  → POST /scenario → ewtss-backend
  → Scenario tree persisted to MySQL
  → Operator starts stream  →  POST /start_stream → ewtss-backend
  → ewtss-backend Kafka producer starts emitting control messages
  → drs_bridge consumes control messages, configures hardware mode
```

---

## Service Details

### ewtss-backend

**Entry point:** `ewtss-backend/main.py`

| Module | Responsibility |
|--------|---------------|
| `usermanagement/` | RBAC: user CRUD, role/feature/permission tables, JWT issuance |
| `scenario/` | Scenario tree CRUD — gaming areas, emitters, antennas, modulation profiles |
| `producer/` | Kafka producers for SRX, GNSS, JSVUSHF; activated via `/start_stream` API |
| `database_backup/` | MySQL dump/restore endpoints |

Startup initialises master data (default users/roles) and optionally starts Kafka producers if streams were active before restart.

### drs_server

**Entry point:** `drs_server/main.py`

| Module | Responsibility |
|--------|---------------|
| `services/` (18 files) | One Kafka consumer per hardware variant × message type; writes to MySQL |
| `routers/` (12 files) | REST endpoints; one per hardware variant |
| `models/models.py` | SQLAlchemy ORM: FF, FH, Burst, HealthStatus, SystemVersion, Configuration, DrsRequest |
| `ui_constants/` | Hardware constants injected into ORM rows at initialisation |
| `auth/` | JWT decode, token table lookup |

All Kafka consumers run as daemon threads launched during `@app.on_event("startup")`.

### drs_bridge

**Entry point:** `drs_bridge/<variant>/start_<variant>_server.py`

Each hardware variant spawns a TCP server (blocking, one thread per client connection). Two operational modes exist for each variant:

| Mode | Directory suffix | Data source |
|------|-----------------|-------------|
| Random | `random_*` | Generates synthetic device data |
| SG / MSC | `sg_*` / `msc_*` | Reads from real hardware over TCP |

The bridge reads command/structure definitions from CSV files at startup. Protocol parsing is implemented inline in `tcp_server.py` per variant (no shared parser library).

### Angular Frontend

**Entry point:** `src/app/`

Key structural points:
- Standalone component architecture (Angular 17+)
- `ApiService` centralises all HTTP calls; base URL is currently hardcoded
- `AuthInterceptor` attaches JWT from `localStorage` to every request
- OpenLayers 9.2.4 renders the geospatial map with locally-served map tiles
- Route guards enforce authentication

---

## Database Schema (Key Tables)

| Table | Purpose | Notable columns |
|-------|---------|----------------|
| `ff` | Fixed-frequency detection reports | frequency, power, AOA/DOA, wideband_fft_data (JSON) |
| `fh` | Frequency-hopper reports | hopper details, pulse counts |
| `burst` | Burst detection data | timing, power levels |
| `srx_health_status` / `mrx_health_status` / … | Per-hardware health | initialised from constants on startup |
| `drs_request` | Command/group IDs with JSON payloads | group_id, command_id, payload |
| `configuration` | Mode and random flag | isRandom, mode |
| `users` / `roles` / `features` / `permissions` | RBAC | — |
| `scenario` / `gaming_area` / `emitter` / `antenna` | Scenario tree | hierarchical foreign keys |

---

## Inter-Service Communication

| From | To | Protocol | Auth |
|------|----|---------|------|
| Angular | ewtss-backend | HTTP + WebSocket | JWT (Bearer) |
| Angular | drs_server | HTTP | JWT (Bearer) |
| ewtss-backend | Kafka | Confluent Kafka client | None |
| drs_bridge | Kafka | Confluent Kafka client | None |
| drs_server | Kafka | Confluent Kafka client | None (consumer) |
| drs_server | MySQL | SQLAlchemy / pymysql | DB credentials |
| ewtss-backend | MySQL | SQLAlchemy / pymysql | DB credentials |
| Hardware devices | drs_bridge | Raw TCP (binary) | None |

---

## Thread Model

`drs_server` runs 18+ background threads at startup — one Kafka consumer per topic. Each consumer holds an open SQLAlchemy session. These are daemon threads; no health monitoring exists.

`drs_bridge` spawns one thread per connected hardware device using `threading.Thread`. Threads are also daemon threads.

`ewtss-backend` spawns producer threads on demand when `/start_stream` is called.

All three services run under the same Python process using uvicorn's asyncio event loop, with synchronous blocking I/O pushed to threads. There is no thread pool limiting or backpressure.

---

## Deployment Topology

The system is designed for a single-host, LAN-connected deployment:

```
[Operator Laptop / Workstation]
  ├── MySQL 8
  ├── Apache Kafka + Zookeeper
  ├── drs_server  (port 8000)
  ├── ewtss-backend  (port 9000)
  ├── drs_bridge  (multiple TCP ports)
  └── Angular dev server or nginx  (port 4200)

[Hardware devices on same LAN]
  └── connect via TCP to drs_bridge ports
```

Offline / air-gapped deployment is supported — all Python packages are vendored in `packages/` directories.
