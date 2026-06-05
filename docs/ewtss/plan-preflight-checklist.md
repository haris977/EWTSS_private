# Plan pre-flight checklist

> **Audience:** anyone writing an implementation plan with `superpowers:writing-plans` (or its equivalent in another harness). **Why:** the B1.3 spec-validation pass on `feat/b1-3-time-sync` exercised 19 distinct plan-quality findings (F1–F19, recorded in [`plans/time-sync-corrigenda.md`](plans/time-sync-corrigenda.md)). Each one would have cost the implementing engineer (or subagent) a wrong turn, a re-write, or a wasted commit. This checklist codifies the additions that the existing `writing-plans` skill — which covers placeholders, spec coverage, type consistency — does not yet enforce.
>
> **How to use:** treat this as a supplement to `superpowers:writing-plans` §Self-Review. Run all three passes below before saving the plan. Each item is short, mechanical, and grep-able.

---

## Pass 1 — environment and scope (before writing the first task)

Run once before drafting tasks. Anything that fails here means the plan can't be executed even if the tasks are perfect.

- [ ] **Decide which application(s) the plan targets when the repo has multiple candidates.** State it in the plan header in one sentence: *"This plan modifies X (path: <path>); siblings Y and Z are reference/legacy and are not touched."* Path strings are a weak signal — readers default to whichever directory exists first.
  *Anchors F18 (Phase 5 paths drifted to mvp4 vs sg-app) and F19 (drs-bridge `Bridge` class missing).*

- [ ] **Pin the language / runtime version to what the target environment can actually install.** Don't reference 3.12 if the lab box has 3.9 and 3.11. If the version isn't installable, add an explicit "install X.Y" task — don't assume.
  *Anchors F6.*

- [ ] **List every non-language CLI the plan touches** (e.g. `ng`, `dotnet`, `docker`, `aws`). For each, state whether it's already installed on the target box or needs a Phase-0 setup task.
  *Anchors F7.*

- [ ] **Reconcile every new top-level directory with the project's `.gitignore`.** If you add `packages/` and `packages/` is wholesale ignored, plan an explicit `.gitignore` adjustment task — text files inside the new dir won't track otherwise.
  *Anchors F8.*

- [ ] **Use commands that work from anywhere in the repo.** Either prefix `cd <service-root> && <cmd>` or `git -C <repo-root> <cmd>` in every step. PowerShell and Bash treat CWD differently across tool invocations.
  *Anchors F9.*

## Pass 2 — skeleton mapping (before writing wiring tasks)

Cross-phase wiring tasks frequently assume infrastructure that earlier tasks should have built. F18 and F19 are the same finding in two phases; treat this as a category.

- [ ] **Grep your draft for `self._<attr>` references inside code blocks.** Every attribute name must be initialised in a class declaration somewhere in the plan (or already exist in the repo).
- [ ] **Grep your draft for `from <module> import <name>`** in tests. Every name must be defined by the time that test runs — either earlier in the same task or in an earlier task. A symbol defined in Task M and imported in Task N (M > N) is a deferred ImportError.
  *Anchors F2.*
- [ ] **If your plan wires Feature X into App Y, verify that App Y exists in the form the plan assumes.** If Y has the right name but the wrong layout (e.g. `mvp4/Sg.App` exists but doesn't have `Services/TimeSync/` or `ViewModels/` or `IConfiguration`), schedule a scaffolding task at the top of the relevant phase — don't bury it inside the wiring task.
  *Anchors F18, F19.*

## Pass 3 — content sanity (while writing each task)

Specific traps that bit in B1.3.

- [ ] **Don't reference deprecated stdlib APIs.** `datetime.utcnow()` deprecated in 3.12. `asyncio.get_event_loop()` deprecated in newer-3.x. Grep your draft for the canonical traps at the version you pinned in Pass 1.
  *Anchors F3.*
- [ ] **State where breadth lives.** In the plan header or the phase intro, write one sentence: *"Unit tests in each task cover only the path required to wire the next task; phase-level breadth lives in Tasks N, M, P."* This calibrates reviewers so they don't flag every narrowness as a gap.
  *Anchors F11.*
- [ ] **When a test mocks a serialize-then-send API** (Kafka producer, HTTP client, gRPC stub), the test's assertion must apply the same encoding the producer applies. If production does `producer.send(topic, json.dumps(body).encode("utf-8"))`, the test must decode bytes before asserting fields. Hand-write a one-line `_assert_kafka_message_eq` helper if you use this pattern more than once.
  *Anchors F12.*
- [ ] **ISO 8601 + manual `"Z"` suffix is a bug.** Pythons `datetime.now(timezone.utc).isoformat()` produces `+00:00` not `Z`; appending `"Z"` makes `+00:00Z` which downstream parsers reject. If you need `Z`, use `.isoformat(timespec="microseconds").replace("+00:00", "Z")`. Pin formatting once in a shared helper.
  *Anchors F13.*
- [ ] **Buffer sizes derived from configurable thresholds must be parameterised.** `deque(maxlen=10)` next to thresholds whose product is 11 will quietly drop the oldest sample whenever a threshold changes. Compute the size from the thresholds.
  *Anchors F14.*
- [ ] **Methods that look like getters must be side-effect free, or carry an unmistakable hint.** A method called `current_status()` that mutates and fires callbacks under some condition will mis-read on first encounter. Either rename, or include a docstring sentence that begins *"Note: this method has a side effect …"*.
  *Anchors F15.*
- [ ] **Callbacks firing from multiple contexts must carry the source.** If a single callback list fires for both *global* and *per-variant* state changes, the listener can't tell them apart. Pass `scope: str` as the first callback parameter from day one — adding it later means rewriting every registration site.
  *Anchors F16.*
- [ ] **Mark hardware-dependent steps `[manual]` or `[lab-only]`.** A subagent can author `ntp-smoke.ps1`, but running it requires WS1 + WS2 hardware. Conflating the two leads project trackers into thinking the gate was exercised.
  *Anchors F4, F5.*
- [ ] **Strip authoring artefacts from code blocks before publishing.** Comments like `// wait — switch from X to Y` or `# remove this` or leftover `import Moq` from a discarded approach are not TODOs; they are typos of intent. Grep your plan for `wait`, `TBD`, `XXX`, `remove this`.
  *Anchors F17.*

## Pass 4 — reviewer-handoff hygiene

The author runs the plan, but a reviewer (human or subagent) will critique it. F10 noted that reviewers themselves can hallucinate.

- [ ] **Mark which reviewer issues are blocking vs scope-creep.** When the plan deliberately omits something (no `__init__.py` validation, no retry-on-Kafka-failure), say so in a "Things this plan does NOT do" section near the top of each task. Reviewers calibrate against that list.
- [ ] **Categorise auto-fixable issues separately from judgment calls.** Deprecation warnings + outright bugs are safe to auto-fix. Over-engineering recommendations (TypedDict over loose dict, type-narrowing on `scope: str`, defensive `isinstance` guards) are not — they need plan-author judgment.

## Quick reference

If you only have time for one grep pass before saving, run these three:

```bash
# Authoring artefacts
grep -nE 'wait|TBD|XXX|remove this|fix this' docs/ewtss/plans/<your-plan>.md
# Deprecated stdlib
grep -nE 'datetime\.utcnow\b|asyncio\.get_event_loop\b' docs/ewtss/plans/<your-plan>.md
# Manual-step ambiguity (anything that lab-tests but isn't labelled)
grep -niE 'ntp|hardware|smoke test|lab.*run|workstation' docs/ewtss/plans/<your-plan>.md
```

If any return hits in code blocks, audit them before publishing.

---

## Mapping back to the findings

Every check above is anchored to one or more findings from the B1.3 corrigenda. The corrigenda is the authoritative source for what each finding cost; this checklist is the prescription. If you encounter a new failure mode while running this checklist, add it here *and* file a new corrigenda finding so the next reader has both the prescription and the case.
