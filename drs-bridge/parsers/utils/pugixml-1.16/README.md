# pugixml v1.16 — reference copy, not the vendoring location

This is a **reference copy** for evaluating the pugixml swap discussed for the
CA120 XML control-channel parser (`drs-bridge/parsers/ca120/src/ca120_parser.cpp`).
It is **not** the final vendoring location — per the repo split, `packages/`
(where third-party C++ dependencies are actually vendored, alongside
`packages/THIRD-PARTY-LICENCES.md`) belongs to the separate parent deployment
repo, not this one. These files need to be copied there and committed in that
repo before they're load-bearing for any build.

## Provenance

| Field | Value |
|---|---|
| Source | https://github.com/zeux/pugixml/releases/tag/v1.16 |
| Asset | `pugixml-1.16.tar.gz` |
| Version | 1.16 (`PUGIXML_VERSION` = 1160) |
| License | MIT — Copyright (c) 2006-2026 Arseny Kapoulkine (see `LICENSE.md`) |
| SHA-256 (tarball) | `4cee1ca4aad395170f4c7a07824f3bdd41f28316c6e1e1090a1425b278ec0b4b` — verified against GitHub's own published release-asset digest before extraction |

## Files

- `pugixml.hpp` / `pugixml.cpp` — the library (not header-only as extracted; either
  compile `pugixml.cpp` as an extra source per parser target, or define
  `PUGIXML_HEADER_ONLY` before including `pugixml.hpp` in a single translation
  unit to avoid a second compiled object).
- `pugiconfig.hpp` — user-configuration header (untouched from upstream).
- `LICENSE.md` — upstream MIT license text, unmodified.

## Next steps (once the parent-repo path is confirmed)

1. Copy these four files to `packages/cpp/pugixml/` in the parent deployment repo.
2. Add a row to that repo's `packages/THIRD-PARTY-LICENCES.md`:
   `pugixml | 1.16 | MIT | https://github.com/zeux/pugixml/releases/tag/v1.16 | 4cee1ca4aad395170f4c7a07824f3bdd41f28316c6e1e1090a1425b278ec0b4b`
3. Point `ca120/CMakeLists.txt`'s include path at that location and add `pugixml.cpp`
   (or `PUGIXML_HEADER_ONLY`) to the build.
