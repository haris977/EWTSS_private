# EWTSS v2 — Software Architecture Document (SAD)

This directory holds the **consolidated Software Architecture Document** for EWTSS v2 — a
single Word file produced as the **RFQ Milestone-1 "Software Architecture for SG" deliverable**
(client-facing). It synthesises the architectural material under [`docs/ewtss/`](../ewtss/)
into one reference.

> **Who uses what.** This `.docx` is the client deliverable only. **The internal team always
> works from the markdown doc set under [`docs/ewtss/`](../ewtss/)** — those are the canonical,
> authoritative, continuously-maintained sources. This generated document is a point-in-time
> consolidation; where it and a `docs/ewtss/` source disagree, the source wins. It is also
> deliberately client-scoped: internal-only material (e.g. the future browser-frontend option)
> is omitted here and lives in the markdown set. Regenerate it for a milestone hand-off; do not
> treat it as a working document.

| File | What it is |
|---|---|
| [`Software_Architecture_Document.docx`](Software_Architecture_Document.docx) | The deliverable. Title page, auto-updating TOC, 8 numbered sections + 2 appendices, 9 numbered figures, 12 numbered tables. **Generated — do not hand-edit;** edit `build_sad.py` and regenerate. |
| [`build_sad.py`](build_sad.py) | python-docx generator that assembles the .docx and embeds the rendered figures. |
| [`diagrams/`](diagrams/) | Figure sources (`*.mmd`, Mermaid) + rendered `*.png`, the `render.sh` script, and `mermaid-config.json`. Edit the `.mmd`, re-render, regenerate the doc. |

## What the document covers

Document purpose / scope / audience; system overview and context; architectural drivers
(requirements, quality attributes, constraints); architecture overview (components + the
seven-layer model); the 19 ADRs; the major workflows (operator, data, control, compute,
exercise); the deployment/runtime view; and a consolidated **open-items, gaps and conflicts**
section. Because the system is mid-development, every capability is tagged
*Implemented / Designed / Planned / Open / Deferred*, and documented inter-source conflicts
(e.g. the STK-Components licence question) are flagged rather than resolved by invention.

## Figures

All figures are authored as code (Mermaid) and rendered to PNG so they stay editable and
version-controlled. Figure 2 reuses the repo's canonical `architecture-diagram.md`; Figure 9
reuses `command-flows.md` §3.3 verbatim; the rest are derived from the design docs (see
Appendix B of the document for per-figure provenance).

## Regenerating

Toolchain (one-time):

```bash
# Diagram renderer (Node ≥ 18; bundles its own Chromium):
npm install @mermaid-js/mermaid-cli            # creates ./node_modules (git-ignored)
# Document generator:
python -m pip install python-docx pillow
```

Then:

```bash
bash docs/architecture/diagrams/render.sh      # *.mmd  -> *.png
python docs/architecture/build_sad.py          # -> Software_Architecture_Document.docx
```

The generated `.docx` embeds a Word TOC field; it populates/refreshes when the file is opened
in Word (the document is flagged to update fields on open), or via right-click → Update Field.

> **Note (air-gapped target):** the diagram renderer (`@mermaid-js/mermaid-cli` + bundled
> Chromium) and the Python packages must be vendored/installed on the build host before going
> offline. The rendered PNGs are committed alongside the sources, so regenerating the `.docx`
> from existing PNGs needs only `python-docx`.
