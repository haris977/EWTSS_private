# EWTSS v2 — Security Design

**Date:** 2026-05-31
**Status:** Baseline (first cut). Establishes the security posture the build and code review validate against; several items are explicitly deferred to tracked backlog work or to customer clarification.
**Audience:** architecture lead, project lead, all developers (code review uses this as the security bar), customer security reviewer.
**Authority:** RFQ Annexure A.1 §F (User Management & Access Rights), the air-gapped deployment model, and standard secure-system expectations for a defence/EW system. Architectural commitments live in the [Decision Record](../decision-record.md); auth flows in [command-flows §1](../command-flows.md).

> **Why this doc exists.** The [pre-mortem](../design-postmortem.md) (§3.8, §4.12) flagged that the doc set had no holistic security artifact — auth was sketched but threat model, data classification, audit-log integrity, and a security-test plan were absent. For a defence/EW system the customer's own security review can block acceptance. This baseline closes the *documentation* gap (tracked as [B1.27](../design-backlog.md)); execution items (pen test, RBAC audit) are [B1.28](../design-backlog.md).

## 1. Scope and trust model

EWTSS v2 runs on an **air-gapped LAN** with two workstations (per [Deployment Guide §1](../deployment-guide.md)):

- **WS1** — SG (Scenario Generator): `Sg.App` (WPF) + STK 12, the SG Operator's workstation; also the NTP Stratum-1 time server.
- **WS2** — DRS: `drs-server`, `drs-bridge`, the DRS webapp (browser), talking to DRS hardware instances on the LAN.

**Trust boundaries** (where untrusted/less-trusted data crosses into a component):

| Boundary | Crossing data | Primary control |
|---|---|---|
| DRS hardware → drs-bridge (TCP/UDP, per IRS) | Raw device frames | C++ parser input validation (§9) |
| Browser (DRS webapp) → drs-server (REST/WS) | Operator input, queries | AuthN/AuthZ (§4–§5), input validation |
| Sg.App (WS1) → PostgreSQL / drs-server (WS2, cross-LAN) | Compute writes, queries | LAN trust + AuthN; resilience [B1.37](../design-backlog.md) |
| Removable media / DVD (install + hotfix) | Software + vendored deps | Supply-chain integrity (§8) |
| Client-owned CC application ↔ SG/DRS | Mission guidelines, events | CC integration API ([B1.4](../design-backlog.md)) — out of v2 control |

The LAN itself is treated as a **trusted but not unconditionally trusted** segment: it is physically controlled and air-gapped, but the design still authenticates inter-actor traffic and does not assume the network is hostile-free (an insider or a compromised workstation is in scope).

## 2. Assets and data classification

| Class | Examples | Handling expectation |
|---|---|---|
| **Mission-sensitive** | EW Library emitter parameters, Apriori known own/enemy info base, scenario `content_json`, computed link analysis | Highest. Access via RBAC; in scope for the customer IP transfer; never leaves the air-gap; redact before any issue-report export ([B1.41](../design-backlog.md)). |
| **Operational telemetry** | `hw.<variant>.<kind>` feeds, `computed_links`, measurements | Internal. Retention-bounded ([B1.34](../design-backlog.md)); time-stamped UTC. |
| **Audit / activity** | User log, Sent/Receive Message logs, System log (RFQ §F 4-class) | Integrity-critical — see §6. |
| **Credentials / secrets** | Operator password hashes, JWT signing key, STK licence file, DB credentials | Secret. See §8. Never logged, never committed (enforced by the repo secret-deny rules + [strategy §9](repository-and-release-strategy.md)). |

A formal classification label scheme (if the customer mandates one) is a clarification item — see [B1.42](../design-backlog.md).

## 3. Threat model (summary)

Actors considered: **unauthorised local user** (physical access to a workstation), **malicious/careless insider** (valid low-privilege account), **compromised workstation** (malware on WS1 or WS2), **supply-chain tampering** (a vendored dependency or DVD altered before install). Out of scope by deployment model: remote internet attackers (air-gapped), DDoS.

Top risks and where they're addressed:

| Threat | Vector | Mitigation (section / backlog) |
|---|---|---|
| Credential theft / session hijack | Stolen token, shared workstation | Short-lived JWT + refresh, token revocation, session timeout (§4, [B1.17](../design-backlog.md)) |
| Privilege escalation | Missing per-endpoint permission check | RBAC enforced server-side on every endpoint (§5, [B1.16](../design-backlog.md)); audited ([B1.28](../design-backlog.md)) |
| Audit-log tampering | Insider edits logs to hide actions | Append-only / tamper-evident logs (§6, [B1.42](../design-backlog.md) for customer requirement) |
| Malicious device frames | Crafted hardware input crashing/exploiting the parser | Bounds-checked C++ parsers + fuzzing (§9, [B1.28](../design-backlog.md)) |
| SQL injection | Dynamic report/query construction in drs-server | Parameterised queries only; SQLi check in the security pass (§9, [B1.28](../design-backlog.md)) |
| Tampered install media / dependency | Altered wheel/DLL/MSI on the DVD | Signature verification + SHA-256 manifest (§8, [B1.39](../design-backlog.md)) |

## 4. Authentication and session management

- **Pattern:** JWT issued by the SG side, validated by `drs-server` via a shared secret — no inter-service HTTP round-trip on the auth hot path ([command-flows §1](../command-flows.md)).
- **First-login flows** (SG Operator, DRS Engineer) are designed in command-flows §1.
- **Lifecycle flows beyond first login** — logout, JWT refresh / rotation, failed-login lockout, password change/reset, session timeout (must not silently interrupt a running exercise), re-auth on destructive admin actions, token revocation on role/disable change, concurrent-session policy — are specified in [B1.17](../design-backlog.md) and each gets a command-flows §1 sequence diagram. **These are required before auth is acceptance-ready and are the security-critical near-term work (weeks 1–4).**

## 5. Authorization (RBAC)

- Roles: SG Operator, DRS Engineer, Admin (catalogue + per-role permission grants tracked in [B1.16](../design-backlog.md)). Shared RBAC store on WS2 PostgreSQL.
- **Rule:** every drs-server endpoint and every privileged Sg.App action checks the caller's permission server-side; the UI hiding a control is never the only gate. A per-endpoint permission audit is part of the security pass ([B1.28](../design-backlog.md)).
- Destructive actions (DB Purge [B1.14], user-disable, role-elevation, hotfix install) require re-authentication (generalised from the DB-purge pattern; [B1.17](../design-backlog.md)).

## 6. Audit logging and integrity

The RFQ §F mandates a 4-class log (User / Sent Message / Receive Message / System). Defence systems typically require **append-only, tamper-evident** audit logs. Options if the customer mandates it ([B1.42](../design-backlog.md) confirms the requirement): an append-only PostgreSQL role for the audit table, per-row hash chaining, or off-workstation log forwarding. Auth events (login, logout, failed-login, lockout, role change) feed the User-log class ([B1.13](../design-backlog.md)).

## 7. Transport and network security

- **DRS device links** are plain TCP/UDP per the IRS — transport security there is a deployment concern, not added by v2 (the IRS dictates the wire format). Documented in the [drs-bridge runtime skeleton plan](../plans/drs-bridge-runtime-skeleton.md).
- **WS1↔WS2 traffic** (PostgreSQL writes, drs-server REST/WS) runs on the trusted LAN. TLS/mTLS on these links is **deferred** as a deployment hardening option (low priority given the air-gap), to be revisited if the customer's security review requires encrypted intra-site transport. Network-partition behaviour is [B1.37](../design-backlog.md).
- The DRS webapp is served by drs-server over the LAN; standard browser-side protections (no secrets in localStorage beyond the short-lived token, CSRF/SameSite posture) apply and are detailed alongside the B1.17 auth flows.

## 8. Secrets, keys, and supply-chain integrity

- **No hardcoded secrets.** Credentials and the JWT signing key are provided via environment / secure config, never committed — enforced by the repo `deny` rules and the confidentiality policy ([strategy §9](repository-and-release-strategy.md)). JWT signing-key rotation procedure is part of [B1.28](../design-backlog.md).
- **Air-gap supply chain.** Vendored wheels / DLLs / npm packages / the Meinberg MSI are verified by publisher signature where available and pinned by SHA-256 in `packages/THIRD-PARTY-LICENCES.md` (the vendoring process is [B1.18](../design-backlog.md); the IP-clean licence allow-list — block GPL/AGPL/LGPL — is [B1.39](../design-backlog.md)). DVD install media integrity is checked offline against the committed hashes.

## 9. Input validation

- **C++ parsers** bounds-check every device frame (length/magic/field ranges) and never trust device input; fuzzing of the parser ABI is part of the security pass ([B1.28](../design-backlog.md)). The reference parser demonstrates the defensive pattern.
- **drs-server** uses parameterised queries exclusively; the dynamic report/query builders get an explicit SQLi review ([B1.28](../design-backlog.md)).
- **Sg.App** validates operator geometry input (lat/lon/alt: reject NaN/±Inf/out-of-range) before pushing DTOs into STK.

## 10. Security testing and acceptance

A dedicated security pass — penetration testing, per-endpoint RBAC audit, input fuzzing (parsers + Sg.App geometry), SQLi check, JWT/secret-handling review — is scheduled for **weeks 13–15** so findings have remediation time before customer acceptance. Tracked as [B1.28](../design-backlog.md). The customer's own security review at delivery is anticipated; this baseline + B1.28 exist so its findings are minimised, not discovered cold.

## 11. Open items / to confirm with the customer

- Data-classification label scheme; audit-log integrity requirement (append-only vs hash-chain vs forwarding); intra-site TLS requirement; localisation/accessibility security implications — all bundled in [B1.42](../design-backlog.md), to raise at the design review.
- Auth lifecycle flows ([B1.17](../design-backlog.md)) — the largest near-term security deliverable.

## 12. References

- [Command Flows §1](../command-flows.md) — authentication sequence diagrams.
- [Design Backlog](../design-backlog.md) — B1.13 (logs), B1.14 (DB purge), B1.16 (RBAC), B1.17 (auth lifecycle), B1.18 (vendoring), B1.27 (this doc), B1.28 (security testing), B1.39 (licence allow-list), B1.42 (customer clarifications).
- [Design Post-Mortem §3.8, §4.12](../design-postmortem.md) — the gaps this doc answers.
- [Repository + Release Strategy §9](repository-and-release-strategy.md) — secrets/confidentiality governance.
- [Deployment Guide](../deployment-guide.md) — workstation roles, licensing, install.
