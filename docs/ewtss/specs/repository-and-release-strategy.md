# Repository and Release Strategy — Design Spec

**Date:** 2026-05-26
**Status:** Approved (user-led brainstorm 2026-05-26 settled all six foundational decisions; cross-account migration approach refined same day after the team confirmed the target org is `Constelli-Projects` with **internal** visibility for all new repos).
**Author:** architecture review (incorporating two engineer proposals from 2026-05-25 + 2026-05-26)
**Source proposals:** the polyrepo case (developer 1) and the SemVer + tag + compat-matrix case (developer 2).

## 1. Goal

Define the repository structure, branching workflow, versioning conventions, integration-testing model, and release process for the EWTSS programme. Address the four concerns the team raised:

1. Each engineer / pair can work and test changes independently.
2. Integration testing across services happens at predictable cadences.
3. The whole system is packageable as a versioned deliverable.
4. The EWTSS overall release version maps cleanly to the per-service repository versions.

## 2. Context

The two engineer proposals (one for polyrepo, one for SemVer + namespaced tags + compatibility matrix) are sound at the per-repo level. Their shared blind spot is **cross-cutting coordination**: contracts that span repos (Kafka schemas, OpenAPI for drs-server, scenario JSON shape), integration tests that span services, the EWTSS overall version concept, and packaging logic for the DVD deliverable. The original proposal placed the compatibility matrix in `drs-server`'s README; this spec moves it into a dedicated coordination repo that also owns docs, infrastructure scripts, integration tests, release manifests, and the installer.

## 3. Repository structure

**Six repositories.** Four service repos + one coordination repo + one references repo.

| Repo | Contents | Tech stack | Primary owners |
|---|---|---|---|
| `sg-app` | Production v2 WPF (`Sg.App/` + `Sg.App.Tests/` + `sg-app.sln`) | .NET 8 + WPF + STK 12 (at runtime; not at build) | B (WPF) + STK developer |
| `drs-server` | FastAPI server, Kafka consumers + producer, REST + WebSocket | Python 3.11 + FastAPI + aiokafka + asyncpg | F |
| `drs-bridge` | Per-variant adapter, Python + C++ parser libs | Python 3.11 + C++17 + CMake | A (Python+Kafka) + C (C++ parsers) |
| `drs-webapp` | DRS Engineer browser SPA | Angular 18 + TypeScript | G |
| **`ewtss-release`** | Canonical docs, infra scripts, integration tests, release manifests, DVD installer, compatibility matrix | Markdown + shell + Python + Docker compose | D (technical) + project lead (release approval) |
| **`ewtss-references`** | Reference codebases — mvp4 (STK reference) + mvp/mvp2/mvp3 (browser-MVP archive) | .NET 8 + STK 12 (mvp4); historical Cesium MVPs (mvp1–3, not actively maintained) | D |

**Repo leads for the two-developer repos.** `sg-app` (B + STK dev) and `drs-bridge` (A + C) each carry two owners on coupled-but-distinct concerns. To keep tag-cutting and release-readiness unambiguous (§5.2), designate one owner per repo as **repo lead** — suggested: **B** for `sg-app` (owns the app shell + composition root) and **A** for `drs-bridge` (owns the Python runtime the C++ parsers plug into). The second owner (STK developer; C) drives their subsystem. The team can swap these assignments; the point is that exactly one name is accountable per repo.

### 3.1 What each repo CI does

Each service repo runs its own CI matrix independently. Repos are decoupled at the CI level — no cross-repo build dependencies.

| Repo | CI workflow |
|---|---|
| `sg-app` | `dotnet build` + `dotnet test --filter Category=Unit` (Windows runner) |
| `drs-server` | `pip install -e .[dev]` + `pytest` (29 tests; 1 skips without broker) on `ubuntu-latest` |
| `drs-bridge` | `pip install -e .[dev]` + build reference C++ parser via CMake + `pytest` (32 tests; 2 skip without cmake) on `ubuntu-latest` |
| `drs-webapp` | `npm ci` + `ng build` + `ng test --browsers=ChromeHeadless` on `ubuntu-latest` |
| `ewtss-release` | Markdown link checker + (when integration test rigs exist) cross-repo integration suite on phase gates |
| `ewtss-references` | `dotnet test --filter Category=Unit` for mvp4 (Windows runner; STK-required tests skipped in CI) |

### 3.2 Cross-repo contracts (the polyrepo gap the proposals didn't cover)

These contracts cross repo boundaries and need versioned definitions:

| Contract | Producer | Consumers | Where the contract lives |
|---|---|---|---|
| Kafka topic + payload schemas (`drs.control`, `system.timesync`, `system.health`, future `hw.<variant>.<kind>`) | drs-bridge, drs-server | drs-server, sg-app, drs-webapp | `ewtss-release/contracts/kafka/*.json` (JSON Schema per topic) |
| OpenAPI for drs-server REST + WebSocket | drs-server | sg-app, drs-webapp | `ewtss-release/contracts/drs-server-openapi.yaml` (generated from FastAPI, committed) |
| `scenarios.content_json` shape (per [scenario-management spec](scenario-management-design.md)) | sg-app | drs-server (reports) | `ewtss-release/contracts/scenario-content-json-schema.json` |
| Scenario publisher endpoint exposed by sg-app | sg-app | drs-bridge ResponseRouter | `ewtss-release/contracts/scenario-publisher-openapi.yaml` |

Contract version (semver-style) is independent of repo version. A contract change that breaks consumers is a major bump on the contract and triggers coordinated major bumps on consumer repos (tracked via the release manifest in §5).

**Intra-repo seams (the two-developer repos).** Where two developers share a repo, they work in parallel by relying on a stable *internal* contract — the same idea as the cross-repo contracts above, one level down:

| Repo | Internal seam | Between |
|---|---|---|
| `sg-app` | `IScenarioBackend` + the DTO boundary (MVP4.5) | WPF UI (B) ↔ STK integration (STK dev) |
| `drs-bridge` | the 4-symbol C parser ABI (ctypes) | Python runtime (A) ↔ C++ parsers (C) |

Keeping each seam stable is what lets the pair avoid blocking each other; churning it forces tight coordination. Keeping the seam *inside* one repo — rather than splitting the pair into two repos — is deliberate: it stays a plain internal interface instead of becoming a versioned cross-repo contract with its own release-manifest entry.

## 4. Versioning

### 4.1 Per-repo SemVer with namespaced tags

Each repo uses Semantic Versioning (MAJOR.MINOR.PATCH), tagged on `main` after a stable build. **Tags are namespaced with the repo name as prefix** so they're self-describing in cross-repo contexts (CI dashboards, release manifests, git log across multiple worktrees):

| Repo | Tag format | Example |
|---|---|---|
| `sg-app` | `sg-app-vMAJOR.MINOR.PATCH` | `sg-app-v1.4.2` |
| `drs-server` | `drs-server-vMAJOR.MINOR.PATCH` | `drs-server-v1.5.0` |
| `drs-bridge` | `drs-bridge-vMAJOR.MINOR.PATCH` | `drs-bridge-v1.3.1` |
| `drs-webapp` | `drs-webapp-vMAJOR.MINOR.PATCH` | `drs-webapp-v1.2.0` |
| `ewtss-release` | `ewtss-vMAJOR.MINOR.PATCH` | `ewtss-v1.0.0` |
| `ewtss-references` | `ewtss-references-vMAJOR.MINOR.PATCH` | rarely bumped; only when reference code changes substantially |

**SemVer rules per repo:**

- **PATCH** — bug fix, no behaviour change visible to consumers (other services, operator, customer).
- **MINOR** — new feature, backward-compatible.
- **MAJOR** — breaking change. Anything that changes a cross-repo contract (Kafka schema, OpenAPI surface, scenario JSON shape) is a major bump even within one service repo.

### 4.2 EWTSS overall version (the linchpin)

An EWTSS release version (e.g., `ewtss-v1.0.0`) is **not** a renamed per-service version. It identifies a specific tuple of per-service tags that integration-test together cleanly. The mapping lives in a versioned release manifest in `ewtss-release/releases/`:

```yaml
# ewtss-release/releases/ewtss-v1.0.0.yaml
ewtss_version: "1.0.0"
released_at: 2026-09-15
released_by: D + project lead
phase_gate: "Phase 7 — Milestone 2 customer acceptance"
notes: |
  First customer-acceptance release. All 7 phase-gate integration
  test rigs passed. Delivered on DVD per RFQ Milestone 2 #1.

services:
  sg-app:      { repo: "sg-app",      tag: "sg-app-v1.4.2" }
  drs-server:  { repo: "drs-server",  tag: "drs-server-v1.5.0" }
  drs-bridge:  { repo: "drs-bridge",  tag: "drs-bridge-v1.3.1" }
  drs-webapp:  { repo: "drs-webapp",  tag: "drs-webapp-v1.2.0" }

contracts:
  kafka_schemas_version:        "1.0"
  drs_server_openapi_version:   "1.0"
  scenario_content_json_version: "1.0"
  scenario_publisher_openapi_version: "1.0"

infrastructure:
  ntp_msi:         "ntp-4.2.8p18-win-x64-setup.msi (sha256: ...)"
  kafka_image:     "confluentinc/cp-kafka:7.6.0"
  postgresql:      "16.x + TimescaleDB 2.x"
  dotnet_runtime:  "8.0.x"
  python_runtime:  "3.11.x"

stk:
  version_pinned:  "12.9.x"

docs_snapshot:
  ewtss_release_tag: "ewtss-v1.0.0"  # this manifest's repo is itself tagged
```

The manifest is **machine-readable** so the DVD installer script (§6) and "restore EWTSS as of v0.4.0 for a demo" workflows can consume it directly. Beats the original proposal's compat-matrix-in-README pattern because it's atomically committed alongside the binary it describes.

### 4.3 Compatibility matrix (auto-derivable)

The compatibility matrix lives at `ewtss-release/releases/compatibility.md`. It's a flat table derived from the release manifests in the same directory — when a new manifest is added, the matrix gets a new row:

```
| EWTSS  | sg-app           | drs-server          | drs-bridge          | drs-webapp           | Notes                |
|--------|------------------|---------------------|---------------------|----------------------|----------------------|
| v0.1.0 | sg-app-v0.1.0    | drs-server-v0.1.0   | drs-bridge-v0.1.0   | drs-webapp-v0.0.1    | Wk 2 — Phase 1 gate  |
| v0.2.0 | sg-app-v0.2.0    | drs-server-v0.3.0   | drs-bridge-v0.2.0   | drs-webapp-v0.1.0    | Wk 5 — first e2e     |
| v0.3.0 | sg-app-v0.3.0    | drs-server-v0.5.0   | drs-bridge-v0.4.0   | drs-webapp-v0.2.0    | Wk 8 — Random+4 var  |
| v0.4.0 | sg-app-v0.5.0    | drs-server-v0.6.0   | drs-bridge-v0.5.0   | drs-webapp-v0.3.0    | Wk 11 — Scenario     |
| v0.5.0 | sg-app-v0.6.0    | drs-server-v0.7.0   | drs-bridge-v0.6.0   | drs-webapp-v0.4.0    | Wk 13 — Integrated   |
| v0.6.0 | sg-app-v0.7.0    | drs-server-v0.8.0   | drs-bridge-v0.7.0   | drs-webapp-v0.5.0    | Wk 15 — hardening    |
| v1.0.0 | sg-app-v1.0.0    | drs-server-v1.0.0   | drs-bridge-v1.0.0   | drs-webapp-v1.0.0    | Wk 17 — Milestone 2  |
```

A trivial Python script in `ewtss-release/tools/` can regenerate this table from the YAML manifests so the file is never hand-edited.

## 5. Release process

### 5.1 Cadence

- **During v2 hardening (17-week build):** EWTSS releases are cut at each of the 7 phase gates (weeks 2, 5, 8, 11, 13, 15, 17). Phase gates already run the cumulative integration test rig per [v2 Execution Plan §6](../v2-execution-plan.md#6-integration-testing-checkpoints); a passing gate becomes a release.
- **During warranty (12 months post-acceptance):** releases are on-demand — hotfixes, scope-expansion batches, breaking changes. No fixed cadence.

### 5.2 Roles

- **D (cross-stack lead)** — owns commit access to `ewtss-release` + `ewtss-references`; drives the integration test rig; authors the release manifest; runs the DVD installer build.
- **Project lead** — countersigns the release tag. A release is not "cut" until both D and the project lead have approved the manifest PR. Adds one approval beat to the release cycle in exchange for cross-checked accountability on customer-visible releases.
- **Per-service owners** — cut tags on their own repos independently (between EWTSS releases) and ensure their service is ready when D opens the release PR. For the two-owner repos (`sg-app`, `drs-bridge`), the designated **repo lead** (§3) cuts the tag, so release-readiness has a single accountable owner rather than "either of us."

### 5.3 Release workflow (per cut)

```
1. Per-service owners tag their own repos as they finish work.
   (Tags accumulate independently. EWTSS release timing is decoupled.)

2. D opens a "release PR" on ewtss-release:
     - new releases/ewtss-vX.Y.Z.yaml manifest
     - updated releases/compatibility.md (auto-generated)
     - draft release notes in releases/notes/ewtss-vX.Y.Z.md

3. D runs the cross-service integration test rig pinned to the
   manifest's tagged versions. Pass = release candidate; fail =
   investigate and rerun.

4. Project lead reviews the manifest + release notes + integration
   test results; approves the PR.

5. PR merged. D tags ewtss-release with ewtss-vX.Y.Z.

6. DVD installer build CI fires on the tag, produces the deliverable.
```

### 5.4 Restoring an older EWTSS release (for a demo or hotfix)

The release manifest is the only file you need to read:

```bash
# 1. From ewtss-release, read the manifest:
cat releases/ewtss-v0.4.0.yaml

# 2. Check out the listed tags in each repo:
git -C sg-app     checkout sg-app-v0.5.0
git -C drs-server checkout drs-server-v0.6.0
git -C drs-bridge checkout drs-bridge-v0.5.0
git -C drs-webapp checkout drs-webapp-v0.3.0

# 3. Use the manifest's infrastructure section to verify
#    matching .NET / Python / Kafka / NTP versions.
```

No compatibility-matrix-in-a-README archaeology needed.

## 6. Integration testing

Cross-service integration tests live in `ewtss-release/integration-tests/`. The rigs span all four services (and `drs-bridge` parsers; and Kafka + TimescaleDB). They run:

- **At each phase gate** — manually triggered by D against the about-to-be-released manifest's pinned tags. Mandatory for cutting an EWTSS release.
- **Nightly** (eventually) — automated, against `main` of each service repo. Surfaces integration regressions early. Blocked today by the CI-hosted-runners pause; revisit when runners are re-enabled.

Per-service unit tests stay in each service repo's own CI — never duplicated to `ewtss-release`.

### 6.1 Per-repo testing convention

Inside each service repo (and every new variant repo) tests follow the same shape — a unit-heavy pyramid plus a single real-boundary check:

1. **Mock-by-default unit tests** — the bulk of the suite: fast, no external systems. Heavy externals (Kafka, the DB, STK, NTP, the drs-server HTTP API) are faked and injected through the same DI seam the code already exposes, so no broker, licence, network, or C++ toolchain is needed to run them.
2. **Exactly one graceful-skip integration test per real boundary** — exercises the *actual* external and **skips cleanly when that dependency is absent**, implemented as a **capability probe (not an env flag)** so CI and developer machines without it stay green:
   - `drs-server` — real Kafka broker test; skips if `localhost:9092` is unreachable (socket probe).
   - `drs-bridge` — C++ parser test; a session fixture builds the parser with CMake and loads the `.dll`/`.so` through ctypes, skipping if `cmake` is absent, the build fails, or the library isn't found.
3. **No cross-service end-to-end in the repo** — that lives in `ewtss-release/integration-tests/` (above), runs at phase gates, and is never duplicated down into a service repo.

Per-stack realisation:

| Stack | Runner | Test doubles |
|---|---|---|
| Python (`drs-server`, `drs-bridge`) | pytest + pytest-asyncio (`asyncio_mode=auto`) | `AsyncMock` + injected factories; in-process sockets for transport tests |
| Angular (`drs-webapp`) | Karma + Jasmine (`ng test`, headless Chrome on CI) | `HttpTestingController` + `jasmine.createSpyObj` services; HTTP fully mocked |
| .NET (`sg-app`) | NUnit + FluentAssertions, `[Category("Unit")]` | hand-written interface doubles (no mocking library); STK is exercised only in the `mvp4` reference, never the product |

**New variant parsers / future services inherit this**: a mock-by-default unit suite plus one capability-probed, graceful-skip integration test for their real boundary. Keep the skip a *probe of the dependency* so the suite is always runnable; don't gate it behind an env var, and don't pull cross-service e2e into the repo.

## 7. Branching strategy (per repo)

Adopt the proposal's GitFlow-lite for all service repos:

```
feature/* or bugfix/*  (short-lived; one ticket / one engineer)
    └──► develop   (integration & local validation)
            └──► main   (stable, deployable)  ◄── per-repo tag here
```

**Rule:** tags only on `main`. Every release on `main` gets a tag (no untagged releases). Develop is the staging branch; main is the protected production-equivalent.

For `ewtss-release`, the workflow is the same with one addition: **a release PR is the only path to a new EWTSS version tag.** Direct pushes to main on `ewtss-release` are restricted (D + project lead are the only mergers).

### 7.1 Code review

`develop` and `main` are protected: every change lands via PR with **at least one approving review from someone other than the author**, enforced by branch protection. This is deliberate — v1's defects (including the cumulative-resource-leak performance regression) trace in part to code shipped without rigorous review, and with most repos single-developer the polyrepo split must not recreate "solo dev self-merges to main."

Reviewer assignment follows the team's ownership split:

| Repo | Reviewer |
|---|---|
| `sg-app` (B + STK dev) | the pair review each other |
| `drs-bridge` (A + C) | the pair review each other |
| `drs-server` (F) | cross-reviewed by A (shared Python / Kafka competency) |
| `drs-webapp` (G) | sole web developer — assign D (or another nominated reviewer) as standing reviewer; lean on tests + a thorough PR description |
| `ewtss-references` (D) | an STK-literate peer (STK dev or B) on the rare occasions mvp4 is touched |
| `ewtss-release` (D + project lead) | release PRs already gated by the §5.3 dual review; routine docs/infra PRs reviewed by a service owner familiar with the area |

The cross-repo pairings double as cross-training, which mitigates the bus-factor risk of single-developer repos and keeps review load from funnelling entirely onto D.

## 8. Migration plan

**Cross-account move:** the current repo lives at `mohit-m_constell/ewtss-v2-design` (personal account). The new repos all land under the org at `https://github.com/Constelli-Projects/` with **internal** visibility. GitHub's rename feature is within-account-only, so the migration uses a **clone-and-push** approach (preserves full git history identically) rather than a transfer + rename.

Seven steps. ~1–2 days of mechanical work + coordination overhead with the team.

### 8.1 Step 1: stand up `Constelli-Projects/ewtss-release` with full history

```bash
# 1. Create the empty org repo (internal visibility):
gh repo create Constelli-Projects/ewtss-release \
    --internal \
    --description "EWTSS — release coordination, docs, integration tests, installer"

# 2. Clone the personal source, then re-point origin at the new org repo
#    and push the full history + tags:
git clone https://github.com/mohit-m_constell/ewtss-v2-design.git ewtss-release
cd ewtss-release
git remote set-url origin https://github.com/Constelli-Projects/ewtss-release.git
git push -u origin main
git push origin --tags    # if any tags exist (none today)
```

The org repo now carries the full git history of the design phase — every doc, ADR, B1.3 corrigenda, scenario-management spec, the recent Kafka infra + reference-parser work, etc. — identical to the source repo.

### 8.2 Step 2: remove service dirs from `Constelli-Projects/ewtss-release`

```bash
# Still in the same clone, on main:
git rm -r sg-app/ drs-server/ drs-bridge/ drs-webapp/ mvp4/ mvp/ mvp2/ mvp3/
git commit -m "split: remove service + reference dirs (moved to per-service repos)

Service dirs and reference codebases have moved to their own repos
per the polyrepo split. This commit removes them from ewtss-release
which now retains only docs, infrastructure scripts, packages, and
release-coordination artefacts.

Source for the split: <commit SHA before this removal>
Per-repo destinations:
  sg-app/    → github.com/Constelli-Projects/sg-app
  drs-server/    → github.com/Constelli-Projects/drs-server
  drs-bridge/    → github.com/Constelli-Projects/drs-bridge
  drs-webapp/    → github.com/Constelli-Projects/drs-webapp
  mvp4/, mvp/, mvp2/, mvp3/  → github.com/Constelli-Projects/ewtss-references"
git push origin main
```

After this commit, `Constelli-Projects/ewtss-release` retains:
- `docs/ewtss/` (the canonical doc set)
- `docs/customer-facing/`
- `docs/superpowers/` (MVP1–MVP4.5 historical archive)
- `infrastructure/` (NTP scripts + docker-compose + kafka topic provisioner)
- `packages/` (vendored installers)
- `.github/disabled/ci.yml` (parked CI workflow)

It also gains (initially empty; populated in subsequent commits):
- `releases/` — release manifests + auto-generated compatibility.md
- `contracts/` — Kafka schemas + drs-server OpenAPI + scenario JSON schema + scenario publisher OpenAPI (initially seeded from current ad-hoc locations)
- `integration-tests/` — cross-service test rigs (populated as Phase 4+ gates approach)
- `installer/` — DVD packaging scripts (built out for Milestone 2)

### 8.3 Step 3: create the 4 service repos

For each of `sg-app`, `drs-server`, `drs-bridge`, `drs-webapp`:

```bash
# 1. Create the empty org repo (internal visibility):
gh repo create Constelli-Projects/<service-name> \
    --internal \
    --description "EWTSS — <one-line per service>"

# 2. From a working copy that still has the service dir
#    (e.g., the original personal clone from before Step 2's removal),
#    copy the contents into a fresh local dir:
mkdir <service-name>
cp -r <pre-removal-clone>/<service-name>/. <service-name>/
cd <service-name>
git init -b main

# 3. Single commit citing the source SHA:
git add .
git commit -m "Initial import from mohit-m_constell/ewtss-v2-design @ <pre-removal-SHA>

Service dir was previously at github.com/mohit-m_constell/ewtss-v2-design
under <service-name>/. Full pre-split history available in
github.com/Constelli-Projects/ewtss-release prior to its split commit
<post-removal SHA in Constelli-Projects/ewtss-release>."

# 4. Push:
git remote add origin https://github.com/Constelli-Projects/<service-name>.git
git push -u origin main

# 5. Cut the initial tag (namespaced per §4.1):
git tag -a <service-name>-v0.1.0 -m "Initial extraction from ewtss-v2-design"
git push origin <service-name>-v0.1.0

# 6. Enforce the §7.1 review policy: protect main so changes land only via PR
#    with >=1 non-author approval. (main exists now; apply the same to develop
#    once it is branched.) Without this step the review policy is advisory only.
gh api --method PUT repos/Constelli-Projects/<service-name>/branches/main/protection \
    --input - <<'JSON'
{
  "required_pull_request_reviews": { "required_approving_review_count": 1 },
  "required_status_checks": null,
  "enforce_admins": false,
  "restrictions": null
}
JSON
```

**Before the `git add .` in step 3**, seed the repo with the Claude Code standard
(committed `.claude/settings.json` + `.gitignore` stanza + a per-repo `CLAUDE.md`,
hook adjusted to the repo's stack) per [§9.4](#94-applying-the-standard-to-a-new-repo).
That way the standard lands in the repo's initial commit rather than as a bolt-on.

Step 6 (branch protection) applies to **every** repo, not just the four service
repos: `ewtss-references` gets the same `main` protection, and `ewtss-release`
additionally restricts who can merge to `main` to D + the project lead (the
release-PR rule in §7). Configuring it at creation time is what turns the §7.1
review matrix from policy into enforcement.

### 8.4 Step 4: create `Constelli-Projects/ewtss-references`

Same shape as Step 3 but the dir contents are `mvp4/` + `mvp/` + `mvp2/` + `mvp3/`:

```bash
gh repo create Constelli-Projects/ewtss-references \
    --internal \
    --description "EWTSS — reference codebases (mvp4 STK reference + mvp1-3 historical browser MVPs)"
```

README at the root explains the archive nature + cross-links each MVP's design spec in `Constelli-Projects/ewtss-release/docs/superpowers/specs/`.

### 8.5 Step 5: settle CI per repo

Each service repo gets its own `.github/workflows/ci.yml` extracted from the current parked workflow (`Constelli-Projects/ewtss-release/.github/disabled/ci.yml`) and scoped to just that service. The current workflow's matrix structure makes this clean — each matrix entry was already self-contained.

The `Constelli-Projects/ewtss-release/.github/disabled/ci.yml` workflow stays disabled until hosted runners are re-enabled (see [.github/disabled/README.md](../../../.github/disabled/README.md)). Per-service CI also stays disabled at the service repo level for the same reason; revival is a per-repo `git mv` once policy changes.

### 8.6 Step 6: update docs

- `Constelli-Projects/ewtss-release/docs/ewtss/README.md` — add a new section "Repository structure" pointing at this spec.
- `Constelli-Projects/ewtss-release/README.md` (top-level) — restructure to describe what `ewtss-release` is now (coordination repo) and link out to the 5 sibling org repos.
- Top-level READMEs in each new service repo describe the service in isolation + cross-link back to `ewtss-release` for shared docs + release manifests.

A doc-propagation pass after the split completes the migration.

### 8.7 Step 7: archive the personal source repo

Once all six org repos are stood up + verified working + the team has cloned them locally + at least one round-trip commit has landed in each:

1. **Update the personal repo's README** with a top-of-page banner pointing at the org:

   ```markdown
   > **Archived 2026-MM-DD.** This was the personal-account artefact of the
   > EWTSS v2 design phase. Active development has moved to the
   > `Constelli-Projects` GitHub organisation:
   >
   > - [Constelli-Projects/ewtss-release](https://github.com/Constelli-Projects/ewtss-release) — docs, infra, integration tests, releases
   > - [Constelli-Projects/sg-app](https://github.com/Constelli-Projects/sg-app)
   > - [Constelli-Projects/drs-server](https://github.com/Constelli-Projects/drs-server)
   > - [Constelli-Projects/drs-bridge](https://github.com/Constelli-Projects/drs-bridge)
   > - [Constelli-Projects/drs-webapp](https://github.com/Constelli-Projects/drs-webapp)
   > - [Constelli-Projects/ewtss-references](https://github.com/Constelli-Projects/ewtss-references) — mvp4 + historical MVPs
   >
   > This repo is preserved as a read-only design-phase archive.
   ```

2. **Mark Archived in GitHub** — Settings → "Archive this repository" → confirm. This sets the repo to read-only with a visible banner; no new commits, issues, or PRs can be made.

   The archive can be undone later if needed (Settings → Unarchive), but the intent is permanent — this is the design-phase artefact, not active code.

3. **(Optional, after a cooling-off period)** — delete the personal repo entirely. Recommended only if the team has been working on the org repos for ≥1 month with no need to reference the personal one. The Archived state is sufficient for most use cases; full deletion is irreversible.

## 9. Developer environment standards (Claude Code)

Every repo in the programme ships a **committed, version-controlled Claude Code
configuration** so that any engineer (or AI agent) working in any repo gets the
same permissions, the same auto-formatting, the same plugin set, and the same
confidentiality guardrails. This is part of the repo baseline, not a personal
choice — the canonical copy lives in `ewtss-release/.claude/` and new repos copy
from it (§9.4).

### 9.1 Committed vs personal

| File | Scope | Git | Contents |
|---|---|---|---|
| `.claude/settings.json` | Team baseline | **committed** | Permission allowlist, secret-deny rules, format-on-edit hook, standard plugins + marketplaces |
| `CLAUDE.md` | Team baseline | **committed** | Per-repo agent operating guide (conventions, gotchas, terminology) — complements `README.md`, does not duplicate it |
| `.claude/settings.local.json` | Personal | **gitignored** | Machine-specific paths, `additionalDirectories`, personal allow rules — never committed |

The `.gitignore` stanza that enforces this (identical in every repo):

```gitignore
# Claude Code config — track the team-shared settings + agent guide,
# ignore personal overrides and all runtime/session artefacts.
.claude/*
!.claude/settings.json
.claude/settings.local.json
```

`CLAUDE.md` sits at the repo root and is tracked normally (not under `.claude/`).

### 9.2 What the baseline `settings.json` contains

- **Permissions (`allow`)** — portable dev-tool wildcards only, no machine paths:
  `git`, `gh`, `dotnet`, `npm`/`npx`/`ng`/`node`, `cmake`, `docker`, the `py`
  launchers, `python -m pytest`/`pip`/`uvicorn`, venv `pytest`/`python`, plus
  `WebSearch` and a short `WebFetch` domain allowlist. Anything machine-specific
  (user-home reads, `additionalDirectories`) belongs in `settings.local.json`.
- **Permissions (`deny`)** — secrets are never readable or editable by the agent:
  `**/.env*`, `**/*.pem`, `**/*.key`, `**/*.pfx`, `**/credentials*.json`,
  `**/secrets*.json`. This is the agent-level complement to the `.gitignore`
  secret rules and is mandatory given the proprietary classification (§9.3).
- **Format-on-edit hook** — a single, **byte-identical** `PostToolUse` hook on
  `Write|Edit`, committed in every repo. The shell is **`powershell`**
  (universally present on the Windows dev boxes — no Git Bash dependency). It
  **dispatches by file extension** and runs the stack's standard single-file
  formatter, walking up from the edited file to find that stack's tool. If the
  tool isn't found it is a **silent no-op** — which is why the same hook is safe
  to ship in every repo regardless of stack:
  - **Python** (`.py`) → `ruff format`, resolved at the nearest
    `.venv/Scripts/ruff.exe`, so each repo uses its own ruff + `pyproject.toml`.
  - **Web** (`.ts .tsx .js .jsx .mjs .cjs .json .css .scss .html`) →
    `prettier --write`, resolved at the nearest `node_modules/.bin/prettier`.
  - **.NET** (`.cs`) → `dotnet csharpier format`, run from the dir whose
    `.config/dotnet-tools.json` declares CSharpier (a fast single-file formatter —
    chosen over `dotnet format`, which is whole-project and too slow per-edit).
  - **C/C++** (`.c .cc .cpp .cxx .h .hpp .hxx`) → `clang-format -i`, **gated on a
    `.clang-format` existing up-tree**. Without that style file the branch is a
    no-op (it does *not* fall back to clang-format's LLVM default, which would
    reformat existing code to a style nobody chose). clang-format is resolved from
    PATH, else from the VS 2022 bundled LLVM (`…/VC/Tools/Llvm/bin/clang-format.exe`).
  - Markdown and YAML are **deliberately not** auto-formatted — the doc set and
    config files are hand-tuned and prettier would reflow them. Opt in per repo if
    a team wants it.

  **Per-repo prerequisite to make a branch active** (the hook itself never
  changes): Python repos already ship `ruff` as a `[dev]` dependency — works out
  of the box. Web repos add `prettier` + a `.prettierrc` to `devDependencies` and
  run `npm ci`. .NET repos run `dotnet new tool-manifest` + `dotnet tool install
  csharpier`. `drs-bridge` (C/C++ parsers) does **not** ship a style file, by choice: a
  single developer owns the parsers, so C++ auto-format is left off until that
  developer chooses to enable it (clang-format reflows the deliberately wrapped layouts — long ABI signatures,
  multi-term bitwise expressions — that the parser authors want preserved; see the
  C++ formatter open item in §10). Until a repo provides its style
  file / formatter, the hook simply no-ops for that stack — no errors, no
  per-engineer config.
- **Plugins** — the standard toolbox, enabled identically for everyone:
  `superpowers`, `code-review`, `claude-code-setup`, `github`, `feature-dev`,
  `code-simplifier`, `frontend-design`, and `typescript-lsp` (all from
  `claude-plugins-official`). Stack-specific plugins or extra marketplaces
  (declared via `extraKnownMarketplaces`) are added per repo only when that
  repo's stack needs them — this programme uses no such extras.

### 9.3 Confidentiality policy for MCP servers and external tools

EWTSS code is **proprietary and internal** (RFQ terms; see each repo's
`CLAUDE.md`). Therefore:

- **No MCP server or hook may transmit repository contents to an external
  service without security sign-off.** This includes "helpful" integrations that
  upload code for analysis, indexing, or documentation.
- External **documentation-lookup** MCPs (e.g. `context7`, which resolves public
  library names → public docs and does not need your code) are permissible **only
  after sign-off**, and only via a **committed `.mcp.json`** so the whole team
  runs an identical, reviewed configuration. Do not enable such servers ad-hoc in
  personal config and call it a team standard.
- When in doubt, keep it local. The agent's `deny` rules (§9.2) already block the
  obvious secret-leak paths; this policy covers the subtler exfiltration-by-tool
  path.

**Current state:** the programme ships **no MCP servers**. Library/API documentation
lookups use the `WebFetch` domain allowlist in §9.2 (`pypi.org`, `github.com`,
`help.agi.com`, `stk.docs.pyansys.com`, `deepwiki.com`). The `context7` doc-lookup
MCP was evaluated (2026-05) and **declined** — its marginal benefit over the
existing allowlist did not justify routing per-developer queries through an
external service on a proprietary project. Revisit only with a specific need and
fresh sign-off.

### 9.4 Applying the standard to a new repo

When a repo is created during the migration (§8.3 / §8.4) — or any time later —
seed it from the canonical copy **before the initial commit**:

1. Copy `ewtss-release/.claude/settings.json` into the new repo's `.claude/`.
2. Copy the Claude Code `.gitignore` stanza from §9.1.
3. Add a root `CLAUDE.md` describing *that* repo (stack, how to run, conventions,
   gotchas) — use `ewtss-release/CLAUDE.md` as the structural template.
4. Leave the format-on-edit hook **as-is** — it is byte-identical in every repo
   and self-dispatches by file type (§9.2). Just install the repo's formatter so
   its branch activates: `ruff` (Python — already a `[dev]` dependency),
   `prettier` + `.prettierrc` (web), CSharpier as a local dotnet tool (.NET), or a
   `.clang-format` at the repo root (C/C++ — clang-format ships with VS 2022).

Prerequisite per machine: Windows PowerShell (built in) — the hook's only hard
dependency. The per-stack formatters are repo-local (venv / `node_modules` /
dotnet tool manifest), so there is nothing to install globally per engineer.

## 10. Open items + future considerations

- **Contract migration timing**: the four cross-repo contracts (§3.2) need to be moved out of their current ad-hoc locations into `ewtss-release/contracts/` as part of the split. Kafka schemas don't exist as standalone JSON Schema files today — they're implicit in `ControlPublisher`/`HealthPublisher`/etc. Should be made explicit as part of B1.x scope.
- **DVD installer**: not yet built. Lives in `ewtss-release/installer/`. Per [Deployment Guide §5](../deployment-guide.md), this is a Milestone-2 deliverable; design + implementation are part of Phase 6.
- **Contract version drift across repos**: when a contract bumps (e.g., scenario JSON adds a field), each consumer repo's compatibility needs to be tracked. Recommended convention: include `contracts.*_version` keys in the release manifest (§4.2 example) so the manifest is the audit trail of "as of this EWTSS release, consumers expect contract version X".
- **`ewtss-references` activity level**: largely write-once (mvp1–3 are frozen; mvp4 is read-mostly). Tag bumps are rare. CI runs on push only; no scheduled CI.
- **Release-manifest validation**: the YAML manifest should be schema-validated in CI to prevent typos (e.g., a tag that doesn't exist in the named repo). A small Python script in `ewtss-release/tools/validate-manifest.py` can run as part of the release-PR CI.
- **Engineer onboarding**: a new engineer joining a service team clones only their service repo + `ewtss-release` for the docs + `ewtss-references` if they need STK reference. Three repos max. Documented in each service repo's README. The committed `.claude/settings.json` + `CLAUDE.md` (§9) mean the agent toolchain is configured the moment they clone — no per-engineer Claude Code setup step.
- **STK dev-licence allocation**: a clarification thread with the customer is already open. Scoped against this team split, demand is small: only the **`sg-app` STK-integration developer** needs an STK *dev* seat. The WPF developer (B) works against `FakeScenarioBackend` (the MVP4.5 test double), so needs none; `ewtss-references`/mvp4 is reference-only and **no longer executed**, so needs none either. Net dev-seat demand from this team: **one**. (Runtime / deployment licensing per ADR-005 / ADR-006 is a separate line item.)
- **C++ formatter selection (§9.2 C/C++ branch)**: decided (2026-05) — **left to the parser's single owner; C++ auto-format stays off for now.** Rationale captured for whenever it's revisited: clang-format (the obvious default, bundled with VS 2022) re-derives every line break from its column limit, so it cannot preserve the deliberate hand-wrapping (one-per-line ABI signatures, one-operand-per-line bitwise expressions) — `ColumnLimit: 0` saves the signatures but then packs free expressions onto one line. Because a single developer owns the parsers, `drs-bridge` ships no style file and the hook's C/C++ branch stays dormant; that developer can later enable one if they wish — clang-format + `// clang-format off` guards, or an alternative such as uncrustify tuned to preserve existing newlines. The parser layout is hand-maintained meanwhile.
- **Test-coverage gaps against the §6.1 convention** (each wants a graceful-skip integration test added):
  - **Persistence layer is untested** — `drs-server`'s sync engine is in-memory; the TimescaleDB path (hypertable inserts, chunk-exclusion queries — the structural fix for v1's query-degradation-with-table-growth) has no automated coverage. Add a Postgres/Timescale integration test (skips without a local DB) **before the Phase 7 load test**, since this is precisely where v1 degraded.
  - **`sg-app` has no Integration-category tests** — the drs-server REST contract is verified only against hand-written `ITimeSyncClient` fakes, never a live or contract-pinned server. As the REST surface stabilises, add a contract test against the committed OpenAPI in `ewtss-release/contracts/` (or a spun-up drs-server).
  - **`drs-webapp` coverage is unenforced and there is no browser e2e** — `karma-coverage` is present but has no thresholds, and there is no Cypress/Playwright smoke. Decide on a coverage gate plus a thin "dashboard renders against a mocked drs-server" e2e for the Phase 6 surfaces.

## 11. References

- Developer proposal 1 (polyrepo strategy) — 2026-05-25, captured in user prompt 2026-05-26.
- Developer proposal 2 (SemVer + tag conventions + compat matrix) — 2026-05-26, same source.
- [v2 Execution Plan §6 — Integration testing checkpoints](../v2-execution-plan.md#6-integration-testing-checkpoints)
- [Scenario Management design spec](scenario-management-design.md) — contract example for `scenarios.content_json`
- [Kafka Infrastructure Layer design spec](kafka-infra-layer-design.md) — contract example for the `drs.control` + `system.timesync` + `system.health` topics
- [Reference C++ parser template plan](../plans/reference-parser-template.md) — example of the per-variant artefact that lives in `drs-bridge/parsers/`
- [Design Backlog](../design-backlog.md) — existing B1.x items; this spec doesn't introduce a new B1.x entry but cross-references the contract migrations
