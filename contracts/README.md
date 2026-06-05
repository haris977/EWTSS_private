# EWTSS cross-repo contracts

**This directory is the versioned source of truth for every interface that crosses a repo boundary.**
Per [repository-and-release-strategy §3.2](../docs/ewtss/specs/repository-and-release-strategy.md). After the polyrepo split this directory lives at `ewtss-release/contracts/`; today it lives at the monorepo root that becomes `ewtss-release`.

The rule: a producer changes the artifact here **first**, the consumer builds against the artifact — not against the other repo's source. A breaking change to a contract is a major bump on that contract and triggers coordinated consumer bumps (tracked in the release manifest, §4.2 of the strategy).

## Status

| Contract | File | Producer → Consumers | Status |
|---|---|---|---|
| drs-server REST/WS API | [`drs-server-openapi.yaml`](drs-server-openapi.yaml) | drs-server → sg-app, drs-webapp, drs-bridge | **partial** — `/health` + `/time/status` baselined from the live FastAPI app; the rest planned (below) |
| Kafka `drs.control` | [`kafka/drs.control.schema.json`](kafka/drs.control.schema.json) | drs-bridge → drs-server | **baselined** (`variant.registered`; open for forward-compatible events) |
| Kafka `system.timesync` | [`kafka/system.timesync.schema.json`](kafka/system.timesync.schema.json) | drs-server → sg-app, drs-webapp | **baselined** |
| Kafka `system.health` | [`kafka/system.health.schema.json`](kafka/system.health.schema.json) | drs-bridge, drs-server → consumers | **baselined** (envelope; payload open pending data-plane spec) |
| Kafka `drs.roster` | [`kafka/drs.roster.schema.json`](kafka/drs.roster.schema.json) | drs-server → drs-bridge | **baselined** (compacted; full-snapshot active roster, key `active`) — consumer shipped in [B1.43](../docs/ewtss/design-backlog.md) drs-bridge plan; producer in Plan 2 |
| Kafka `hw.<variant>.<kind>` (data-plane) | — | drs-bridge → drs-server | **planned** — deferred to first-IRS work ([kafka-infra-layer-design §3.3](../docs/ewtss/specs/kafka-infra-layer-design.md)) |
| Scenario `content_json` | [`scenario-content-json.schema.json`](scenario-content-json.schema.json) | sg-app → drs-server (reports) | **partial** — partition structure pinned; inner entity/emitter shapes await the typed-entity DTO spec + EW Library (B1.7) |
| Scenario publisher API | — | sg-app → drs-bridge ResponseRouter | **planned** — `GET /exercises/{id}/responses` ([command-flows §5.2](../docs/ewtss/command-flows.md)) |

## Planned drs-server surface (add to the OpenAPI as it lands)

These are designed in [command-flows.md](../docs/ewtss/command-flows.md) but not yet implemented; consumers should treat them as **not frozen** until they appear in `drs-server-openapi.yaml`:

- WebSocket telemetry fan-out — `/ws/variants/{id}/monitor` (command-flows §4.2, §5.1)
- Scenario publisher — `GET /exercises/{id}/responses?group_id=&unit_id=&tick=` (§5.2)
- `/measurements`, `/reports`, `/messages` query + report endpoints (§Reports)
- `/auth/*` — login, logout, refresh, lockout, revocation (command-flows §1 + backlog B1.17)

## Ownership & gate

Per the pre-mortem, the OpenAPI + the typed-entity DTO shapes are a **pre-build freeze item** (§2.1): owner **F** (drs-server OpenAPI), **B** (scenario/DTO shapes), **D** (Kafka schemas + parser ABI). Target: frozen by **week 2** of the hardening phase, each accepted in writing by the consuming repo's owner.

## How to use

- **Kafka schemas** are JSON Schema (draft 2020-12). Producers should validate outgoing payloads against them in a unit test; consumers validate incoming. The schemas are the wire contract — the publisher source in `drs-bridge`/`drs-server` must match.
- **OpenAPI** is generated from the FastAPI app for the implemented surface (`drs-server/.venv/Scripts/python.exe -c "import json; from drs_server.main import app; print(json.dumps(app.openapi()))"`), then curated (e.g. the `status` enum is pinned here even though FastAPI emits a bare string). Regenerate + re-curate when endpoints land.
- **Scenario schema** mirrors [scenario-management-design §4.1](../docs/ewtss/specs/scenario-management-design.md). `compute_inputs` is the hashed partition (edit-invalidation); `metadata` is not.
