# EWTSS v2 — Work Breakdown Structure (Zoho Projects import)

**Audience:** project lead / project manager. **Created:** 2026-06-29. **Status:** draft for PM review.

A work breakdown structure for the EWTSS v2 hardening phase, derived from the design docs —
chiefly [v2-execution-plan.md](../v2-execution-plan.md) (team, 17-week / 7-phase structure,
per-person ownership, critical path, integration gates) and
[design-backlog.md](../design-backlog.md) (Milestone-1 design items B1.x + documentation
deliverables). One task per row, formatted for Zoho's Task List → Task → Subtask hierarchy.

This is a point-in-time PM hand-off artifact, not a maintained design doc — it is not kept in
sync with the docs as they evolve. Regenerate from the design set if the plan changes materially.

## Files

| File | What it is |
|---|---|
| [`ewtss-v2-wbs-zoho.csv`](ewtss-v2-wbs-zoho.csv) | The WBS, one task per row, ready for Zoho Projects CSV import. Columns: Task List, Task Name, Parent Task, Owner, Duration (days), Dependency, Description. |
| [`ewtss-v2-wbs.docx`](ewtss-v2-wbs.docx) | Word-formatted equivalent of the WBS (landscape, repeating header row) plus the owner legend, open decisions, cross-cutting notes, and import steps — for reading/sharing. Generated from the CSV by [`build-wbs-docx.py`](build-wbs-docx.py); regenerate if the CSV changes. |
| [`build-wbs-docx.py`](build-wbs-docx.py) | Stdlib-only (csv + zipfile) generator that builds the `.docx` from the CSV. No third-party deps — air-gap safe. |
| `README.md` | This cover note: owner legend, open decisions, cross-cutting notes, import steps, and the generating prompt. |

## Owner legend (repo uses role IDs, not names — map to real people before scheduling)

| ID | Role |
|---|---|
| F | drs-server lead (Senior Python) |
| A | drs-bridge Python + C# scenario-publisher integration |
| B | SG (C# specialist) |
| C | C++ parser libraries |
| D | cross-stack lead — architect, integration-test owner, infra/packaging |
| E | drs-server REST/auth/reports (React dev, cross-trained to Python) |
| G | DRS webapp lead (React) |
| PL / AL | project lead / architecture lead |
| Ops / Sec | ops-lab lead / security tester |

## Open decisions to resolve before/early in the build

- **B1.4 — CC integration API surface:** externally blocked on the client's Control Center requirements. Scope only the v2-decidable surface until the client responds.
- **B1.44 — JVM licence for the Kafka runtime:** OpenJDK/Temurin is GPLv2+Classpath-Exception vs the handbook's "GPL (all versions)" block-list. PL+D ruling needed before air-gap packaging.
- **B1.45 — DRS webapp `checkJs` gate:** editor-only vs enforced `tsc` vs status-quo. PL call before Phase-6 webapp surfaces.
- **B1.15 — Blue/Red Line → entity-type mapping:** needs an ADR before entity property panels.
- **Variant count/priority list:** parser & monitor-scan tasks are batched (first / 2-3 / 4-6 / 7-9 / 10-12); the actual list and ordering must come from the customer before per-variant durations are firm.
- **STK dev-seat / licence count:** tasks assume working STK 12.9 seats (procured separately) — confirm covered.

## Cross-cutting work surfaced as explicit tasks

- **CI bring-up harness** — note: org-hosted CI runners are currently paused (`.github/disabled/ci.yml`); integration suite runs locally for now.
- **Polyrepo migration + release manifests** — [repository-and-release-strategy](../specs/repository-and-release-strategy.md) mandates the six-repo split + `ewtss-release` manifests.
- **Air-gap dependency vendoring + WiX/DVD packaging** — gate the Phase-6 install gate; vendoring becomes load-bearing after week 8.
- **Build toolchain on the secure segment** — build-from-source offline (vendored Node 22 + offline npm cache).

## Status caveat

Several rows are already partly/fully implemented in-repo and are listed for tracking, not fresh build:
Time sync (B1.3), DRS roster store/publisher/reconcile/per-instance transport (B1.43), webapp scaffold,
Kafka infra layer, reference parser template, security design + contracts scaffold. Mark these % complete
or Closed on import rather than scheduling them anew.

## Zoho Projects import steps

1. Projects → your project → **Tasks** → ⋯ menu → **Import** → choose `ewtss-v2-wbs-zoho.csv`.
2. Map columns: **Task List** → Task List; **Task Name** → Task Name; **Parent Task** → Parent Task (for subtasks); **Owner** → Owner/Assignee; **Duration (days)** → Duration; **Dependency** → Predecessor/Dependency; **Description** → Description.
3. The file is UTF-8 and fully quoted. Hierarchy is expressed only via Task List + Parent Task; dependencies reference exact Task Names so they resolve on import.
4. Apply the team-name mapping above before importing the Owner column (Zoho matches assignees to real users).

## Generating prompt (verbatim, for reproducibility)

> You are an experienced technical project manager producing a work breakdown structure formatted for import into Zoho Projects. You have the project folder open in VS Code — read the design and constraint docs in the repo directly to ground the WBS in the actual design.
>
> Context:
> - The project is design-complete; the detailed design docs are in this repo — locate and read them.
> - The team is already allocated: [each person + role/skill area].
> - Project: [one-line description + domain, if not clear from the docs].
> - Honor the constraints documented in the repo (timeline, environment, milestones).
>
> Task:
> - Break the project down into a work breakdown structure derived from the design docs, organized to map onto Zoho's Task List → Task → Subtask hierarchy. Use the major design components or phases as Task Lists, and decompose each into reasonably sized, assignable tasks — enough detail to plan and track, without splitting hairs.
> - Output a single flat table ready for Zoho XLS/CSV import, with these columns: Task List | Task Name | Parent Task (blank for top-level; parent's exact Task Name for a subtask) | Owner | Duration (days) | Dependency (exact Task Name(s), blank if none) | Description (the design item it maps to)
> - One task per row; express hierarchy only through the Task List and Parent Task columns, not indentation or numbering. Keep parent and dependency references as exact, consistent Task Names so they resolve on import.
> - Below the table, briefly note any design items that need a decision before work starts, and any cross-cutting work the design implies but doesn't name (build, CI, integration, packaging).
> - Derive tasks from the actual design — don't pad with boilerplate. If the design docs are unclear or silent on something needed to scope a task, flag it rather than guessing.

**Note on the prompt's bracketed placeholders:** `[each person + role/skill area]` and `[one-line description + domain]` were left as template placeholders and not filled in. The team allocation and project description were taken from the repo's `v2-execution-plan.md` §2–3 instead (hence role IDs A–G rather than names).
