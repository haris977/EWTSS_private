# Disabled GitHub Actions workflows

This directory holds workflow YAML files that are temporarily inactive.
GitHub does not look for workflows under `.github/disabled/` — only under
`.github/workflows/` — so files here have no effect on the repo until
moved back.

## Current state (2026-05-21)

`ci.yml` is parked here because **GitHub Actions hosted runners are
disabled at the GitHub Enterprise org level** for this repository. Every
push and PR triggered the four CI jobs (`Python — drs-server`, `Python —
drs-bridge`, `Angular — drs-webapp`, `.NET — sg-app`), each immediately
failed with:

> GitHub Actions hosted runners are disabled for this repository. For
> more information please contact your GitHub Enterprise Administrator.

So the workflow itself is fine — there's nothing to fix in the YAML. The
runtime is just not provisioned.

## Why park instead of delete

The YAML is a valid description of how the four suites *should* be
exercised on CI. Keeping it under version control means:

- The day hosted runners are enabled, revival is a one-line `git mv`
  away.
- New contributors see the intended test matrix as soon as they land in
  `.github/`.
- The reference parser integration test
  ([`drs-bridge/tests/test_reference_parser_integration.py`](../../drs-bridge/tests/test_reference_parser_integration.py))
  and the Kafka integration test
  ([`drs-server/tests/test_kafka_broker_integration.py`](../../drs-server/tests/test_kafka_broker_integration.py))
  both depend on a CI-style environment to exercise their "broker up" /
  "cmake available" paths. The YAML documents how those paths get
  exercised once runners are back.

## To revive

```bash
git mv .github/disabled/ci.yml .github/workflows/ci.yml
git commit -m "ci: re-enable workflow (hosted runners now provisioned)"
git push
```

That's it. The first push afterwards will trigger the matrix on whichever
runners the org has enabled (hosted or self-hosted).

## If hosted runners stay off long-term

Two options worth considering:

1. **Self-hosted runner** on a team workstation that already has the
   relevant toolchains (Python 3.11, Node 22, .NET 8 SDK,
   STK 12 for the mvp4 suite that's currently CI-excluded). The
   `runs-on:` lines in `ci.yml` change from `ubuntu-latest` /
   `windows-latest` to `[self-hosted, linux]` / `[self-hosted, windows]`.

2. **Local pre-commit gate** — a `pre-commit` or `lefthook` config that
   runs the same `pytest` / `ng test` / `dotnet test` commands the CI
   YAML would. Slower feedback than push-time CI, but no infrastructure
   dependency.

Both are larger lifts than a one-line revival, so they only make sense
if the hosted-runner policy is durable.
