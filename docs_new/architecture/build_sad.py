# -*- coding: utf-8 -*-
"""
build_sad.py — Generate the EWTSS Software Architecture Document (.docx).

Consolidates the EWTSS architecture doc set (docs/ewtss/*) into a single
Word reference. Diagrams are authored as Mermaid under docs/architecture/diagrams/
and rendered to PNG by render.sh; this script embeds those PNGs.

Run:  python docs/architecture/build_sad.py
Requires: python-docx (pip install python-docx) and the rendered PNGs.

The prose is derived from the repo's own architecture documents; where the
sources are silent, incomplete, or conflict, the document says so explicitly
rather than inventing detail.
"""
import os
from docx import Document
from docx.shared import Pt, Inches, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH, WD_BREAK
from docx.enum.section import WD_ORIENT, WD_SECTION
from docx.enum.table import WD_TABLE_ALIGNMENT
from docx.oxml.ns import qn
from docx.oxml import OxmlElement
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
DIAG = os.path.join(HERE, "diagrams")
OUT = os.path.join(HERE, "Software_Architecture_Document.docx")

ACCENT = RGBColor(0x2A, 0x4B, 0x8D)
GREY = RGBColor(0x55, 0x55, 0x55)

fig_n = 0
tbl_n = 0


# ----------------------------------------------------------------------------
# low-level helpers
# ----------------------------------------------------------------------------
def set_cell_bg(cell, hexcolor):
    tcPr = cell._tc.get_or_add_tcPr()
    shd = OxmlElement('w:shd')
    shd.set(qn('w:val'), 'clear')
    shd.set(qn('w:fill'), hexcolor)
    tcPr.append(shd)


def shade_header(row):
    for c in row.cells:
        set_cell_bg(c, "2A4B8D")
        for p in c.paragraphs:
            for r in p.runs:
                r.font.bold = True
                r.font.color.rgb = RGBColor(0xFF, 0xFF, 0xFF)
                r.font.size = Pt(9)


def add_field(paragraph, instr):
    """Insert a Word field (e.g. TOC, PAGE) into a paragraph."""
    r = paragraph.add_run()
    fb = OxmlElement('w:fldChar'); fb.set(qn('w:fldCharType'), 'begin')
    it = OxmlElement('w:instrText'); it.set(qn('xml:space'), 'preserve'); it.text = instr
    sep = OxmlElement('w:fldChar'); sep.set(qn('w:fldCharType'), 'separate')
    t = OxmlElement('w:t'); t.text = "Update this field (right-click > Update Field)."
    end = OxmlElement('w:fldChar'); end.set(qn('w:fldCharType'), 'end')
    r._r.append(fb); r._r.append(it); r._r.append(sep); r._r.append(t); r._r.append(end)


_bm_id = [3000]  # unique bookmark ids for figure captions


def _field_run(par, instr, placeholder="1", bold=True, size=9, color=ACCENT, italic=False):
    """Append a run carrying a single Word field (SEQ / REF / …) to a paragraph."""
    run = par.add_run()
    run.bold = bold; run.italic = italic; run.font.size = Pt(size)
    if color is not None:
        run.font.color.rgb = color
    fb = OxmlElement('w:fldChar'); fb.set(qn('w:fldCharType'), 'begin')
    it = OxmlElement('w:instrText'); it.set(qn('xml:space'), 'preserve'); it.text = instr
    sep = OxmlElement('w:fldChar'); sep.set(qn('w:fldCharType'), 'separate')
    t = OxmlElement('w:t'); t.text = placeholder
    end = OxmlElement('w:fldChar'); end.set(qn('w:fldCharType'), 'end')
    for el in (fb, it, sep, t, end):
        run._r.append(el)
    return run


def add_seq_caption(par, bookmark, caption_text):
    """Caption = 'Figure ' + bookmarked SEQ Figure field + '. ' + text.
    The SEQ field auto-numbers; the bookmark lets REF fields cross-reference the number;
    a TOC field with \\c "Figure" collects these into the Figures list."""
    r = par.add_run("Figure "); r.bold = True; r.font.color.rgb = ACCENT; r.font.size = Pt(9)
    bid = str(_bm_id[0]); _bm_id[0] += 1
    bs = OxmlElement('w:bookmarkStart'); bs.set(qn('w:id'), bid); bs.set(qn('w:name'), bookmark)
    par._p.append(bs)
    _field_run(par, ' SEQ Figure \\* ARABIC ')
    be = OxmlElement('w:bookmarkEnd'); be.set(qn('w:id'), bid)
    par._p.append(be)
    r2 = par.add_run(". "); r2.bold = True; r2.font.color.rgb = ACCENT; r2.font.size = Pt(9)
    r3 = par.add_run(caption_text); r3.italic = True; r3.font.size = Pt(9)


def add_ref(par, bookmark):
    """Insert 'Figure ' + a REF field that resolves to the bookmarked caption number."""
    par.add_run("Figure ")
    _field_run(par, ' REF %s \\h ' % bookmark, placeholder="#", bold=False, color=None)


def para_with_refs(doc, text, space_after=6):
    """Like para(), but [[FIG:key]] tokens become live 'Figure N' cross-references
    (REF fields to the fig_<key> caption bookmark)."""
    import re
    p = doc.add_paragraph()
    p.paragraph_format.space_after = Pt(space_after)
    for part in re.split(r'(\[\[FIG:[a-z0-9_-]+\]\])', text):
        m = re.match(r'\[\[FIG:([a-z0-9_-]+)\]\]$', part)
        if m:
            add_ref(p, "fig_" + m.group(1))
        elif part:
            p.add_run(part)
    return p


def force_update_fields(doc):
    se = doc.settings.element
    uf = OxmlElement('w:updateFields'); uf.set(qn('w:val'), 'true')
    se.append(uf)


def portrait(doc):
    s = doc.add_section(WD_SECTION.NEW_PAGE)
    s.orientation = WD_ORIENT.PORTRAIT
    if s.page_width > s.page_height:
        s.page_width, s.page_height = s.page_height, s.page_width
    s.left_margin = s.right_margin = Inches(1.0)
    s.top_margin = s.bottom_margin = Inches(1.0)
    return s


def landscape(doc):
    s = doc.add_section(WD_SECTION.NEW_PAGE)
    s.orientation = WD_ORIENT.LANDSCAPE
    if s.page_width < s.page_height:
        s.page_width, s.page_height = s.page_height, s.page_width
    s.left_margin = s.right_margin = Inches(0.7)
    s.top_margin = s.bottom_margin = Inches(0.7)
    return s


# ----------------------------------------------------------------------------
# content helpers
# ----------------------------------------------------------------------------
def h(doc, text, level=1):
    p = doc.add_heading(text, level=level)
    return p


def para(doc, text, italic=False, size=None, space_after=6):
    p = doc.add_paragraph()
    r = p.add_run(text)
    r.italic = italic
    if size:
        r.font.size = Pt(size)
    p.paragraph_format.space_after = Pt(space_after)
    return p


def bullets(doc, items, style="List Bullet"):
    for it in items:
        p = doc.add_paragraph(style=style)
        if isinstance(it, tuple):
            lead, rest = it
            r = p.add_run(lead); r.bold = True
            p.add_run(rest)
        else:
            p.add_run(it)
        p.paragraph_format.space_after = Pt(2)


def note_box(doc, label, text):
    p = doc.add_paragraph()
    p.paragraph_format.space_before = Pt(4)
    p.paragraph_format.space_after = Pt(8)
    rl = p.add_run(label + "  ")
    rl.bold = True
    rl.font.color.rgb = RGBColor(0xB0, 0x30, 0x30)
    p.add_run(text)
    # light border
    pPr = p._p.get_or_add_pPr()
    pbdr = OxmlElement('w:pBdr')
    for edge in ('top', 'left', 'bottom', 'right'):
        e = OxmlElement('w:' + edge)
        e.set(qn('w:val'), 'single'); e.set(qn('w:sz'), '6')
        e.set(qn('w:space'), '6'); e.set(qn('w:color'), 'B03030')
        pbdr.append(e)
    pPr.append(pbdr)


def table(doc, headers, rows, widths=None, font=8.5):
    global tbl_n
    t = doc.add_table(rows=1, cols=len(headers))
    t.style = 'Table Grid'
    t.alignment = WD_TABLE_ALIGNMENT.CENTER
    hdr = t.rows[0].cells
    for i, htext in enumerate(headers):
        hdr[i].paragraphs[0].add_run(htext)
    shade_header(t.rows[0])
    for row in rows:
        cells = t.add_row().cells
        for i, val in enumerate(row):
            par = cells[i].paragraphs[0]
            run = par.add_run(str(val))
            run.font.size = Pt(font)
    if widths:
        for r_ in t.rows:
            for i, w in enumerate(widths):
                r_.cells[i].width = Inches(w)
    for r_ in t.rows[1:]:
        for c in r_.cells:
            for p in c.paragraphs:
                p.paragraph_format.space_after = Pt(1)
    doc.add_paragraph().paragraph_format.space_after = Pt(2)
    return t


def table_caption(doc, text):
    global tbl_n
    tbl_n += 1
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.LEFT
    r = p.add_run("Table %d. " % tbl_n); r.bold = True; r.font.size = Pt(9)
    r.font.color.rgb = ACCENT
    r2 = p.add_run(text); r2.font.size = Pt(9); r2.italic = True
    p.paragraph_format.space_after = Pt(4)
    return tbl_n


def figure(doc, filename, key, caption, orient="portrait"):
    """Embed a rendered PNG with a SEQ-numbered, cross-referenceable caption.

    `key` names the caption bookmark (fig_<key>) that in-text [[FIG:key]] refs target.
    Numbering is a Word SEQ field, so it (and every REF to it) auto-maintains on update.
    Portrait figures flow inline (no section break) so pages fill naturally;
    only landscape figures switch section orientation and then resume portrait.
    """
    global fig_n
    fig_n += 1
    path = os.path.join(DIAG, filename)
    with Image.open(path) as im:
        w, hgt = im.size
    aspect = w / hgt
    if orient == "landscape":
        landscape(doc)
        maxw, maxh = 9.4, 6.4
    else:
        maxw, maxh = 6.4, 8.3
    disp_w = min(maxw, maxh * aspect)
    cap = doc.add_paragraph()
    cap.alignment = WD_ALIGN_PARAGRAPH.CENTER
    cap.paragraph_format.keep_with_next = True
    cap.paragraph_format.space_before = Pt(6)
    add_seq_caption(cap, "fig_" + key, caption)
    pic = doc.add_paragraph(); pic.alignment = WD_ALIGN_PARAGRAPH.CENTER
    pic.paragraph_format.keep_with_next = True
    pic.add_run().add_picture(path, width=Inches(disp_w))
    spacer = doc.add_paragraph(); spacer.paragraph_format.space_after = Pt(8)
    if orient == "landscape":
        portrait(doc)  # resume portrait flow after a landscape figure
    return fig_n


# ----------------------------------------------------------------------------
# build
# ----------------------------------------------------------------------------
doc = Document()

# base styles
normal = doc.styles['Normal']
normal.font.name = 'Calibri'
normal.font.size = Pt(10.5)
normal.paragraph_format.space_after = Pt(6)
for lvl, sz in [('Heading 1', 16), ('Heading 2', 13), ('Heading 3', 11.5)]:
    st = doc.styles[lvl]
    st.font.color.rgb = ACCENT
    st.font.size = Pt(sz)
    st.font.name = 'Calibri'

# first section margins + footer
sec0 = doc.sections[0]
sec0.left_margin = sec0.right_margin = Inches(1.0)
sec0.top_margin = sec0.bottom_margin = Inches(1.0)

# ---- Title page ----
for _ in range(3):
    doc.add_paragraph()
t = doc.add_paragraph(); t.alignment = WD_ALIGN_PARAGRAPH.CENTER
rt = t.add_run("Software Architecture Document"); rt.bold = True
rt.font.size = Pt(30); rt.font.color.rgb = ACCENT
st = doc.add_paragraph(); st.alignment = WD_ALIGN_PARAGRAPH.CENTER
rst = st.add_run("Development of Scenario Generator with DRS")
rst.font.size = Pt(15); rst.font.color.rgb = GREY
sub = doc.add_paragraph(); sub.alignment = WD_ALIGN_PARAGRAPH.CENTER
rsub = sub.add_run("Architecture reference — RFQ Milestone 1")
rsub.italic = True; rsub.font.size = Pt(11)
for _ in range(2):
    doc.add_paragraph()

meta = doc.add_table(rows=0, cols=2); meta.alignment = WD_TABLE_ALIGNMENT.CENTER
for k, v in [
    ("Document type", "Software Architecture Document (SAD)"),
    ("System", "Development of Scenario Generator with DRS (referred to herein as EWTSS)"),
    ("Version", "1.0"),
    ("Date", "30 June 2026"),
    ("Status", "Issued for RFQ Milestone 1 (Software Architecture for SG)"),
    ("Prepared by", "Architecture team"),
    ("Classification", "Commercial-in-Confidence — issued under RFQ Milestone 1"),
]:
    cells = meta.add_row().cells
    rk = cells[0].paragraphs[0].add_run(k); rk.bold = True; rk.font.size = Pt(10)
    cells[0].width = Inches(2.0)
    cells[1].paragraphs[0].add_run(v).font.size = Pt(10)
    cells[1].width = Inches(4.0)

note = doc.add_paragraph(); note.alignment = WD_ALIGN_PARAGRAPH.CENTER
rn = note.add_run(
    "This document describes the EWTSS software architecture for the development phase. "
    "Capabilities already validated in the current codebase are distinguished from those "
    "planned for the build.")
rn.italic = True; rn.font.size = Pt(8.5); rn.font.color.rgb = GREY

# footer: confidential + page number
footer = sec0.footer
fp = footer.paragraphs[0]; fp.alignment = WD_ALIGN_PARAGRAPH.CENTER
fr = fp.add_run("EWTSS — Software Architecture Document   |   Commercial-in-Confidence   |   Page ")
fr.font.size = Pt(8); fr.font.color.rgb = GREY
add_field(fp, "PAGE")

# ---- TOC ----
portrait(doc)
htoc = doc.add_heading("Contents", level=1)
add_field(doc.add_paragraph(), 'TOC \\o "1-3" \\h \\z \\u')
hfig = doc.add_heading("Figures", level=2)
add_field(doc.add_paragraph(), 'TOC \\h \\z \\c "Figure"')

# ============================================================================
# 1. Introduction
# ============================================================================
portrait(doc)
h(doc, "1. Introduction", 1)

h(doc, "1.1 Purpose", 2)
para(doc,
     "This Software Architecture Document (SAD) is the architecture reference for the "
     "Development of Scenario Generator with DRS (the RFQ project title), referred to throughout "
     "this document by the short name EWTSS. It is prepared as the RFQ Milestone-1 "
     "\"Software Architecture for SG\" deliverable. The system is engineered to meet the "
     "required telemetry throughput and concurrency at scale. This document describes the "
     "architecture, the decisions behind it, the major workflows it supports, the deployment "
     "and runtime view, and the phased path to delivery.")

h(doc, "1.2 Scope", 2)
para(doc,
     "The document covers the system context and boundaries, architectural drivers, the "
     "component and layered architecture, the key architectural decisions (ADRs), the major "
     "operator and runtime workflows, the deployment/runtime view, and the delivery scope. It "
     "covers the two workstations inside the development scope (WS1 — Scenario Generator; "
     "WS2 — DRS) and treats the client-owned Control Center and per-variant Entity Controller "
     "Applications as external integration boundaries. Implementation-level UI gestures and "
     "exhaustive alternatives analysis are out of scope for this document.")

h(doc, "1.3 Intended audience", 2)
bullets(doc, [
    ("Architects & technical reviewers — ", "the architecture and the decision record."),
    ("Engineering staff — ", "component responsibilities and how the system fits together."),
    ("Customer architects & design-review participants — ", "feasibility assessment and RFQ alignment."),
    ("Ops / integration engineers — ", "the deployment and runtime view."),
])

h(doc, "1.4 How to read this document", 2)
para(doc,
     "This document describes the target architecture for the development phase. A capability "
     "already validated in the current codebase is identified as Established; a capability "
     "scheduled for the build is marked Planned; an integration point with a client-owned "
     "system is marked External.")
table(doc, ["Marker", "Meaning"], [
    ["Established", "Validated in the current codebase."],
    ["Planned", "Scheduled for the development phase."],
    ["External", "Boundary with a client-owned / third-party system; EWTSS exposes an interface but does not own the consumer."],
], widths=[1.6, 4.8])
table_caption(doc, "Capability markers used in this document.")

h(doc, "1.5 Terminology", 2)
bullets(doc, [
    ("EWTSS — ", "the short name used throughout this document for the system delivered under the RFQ title 'Development of Scenario Generator with DRS' (expands as Electronic Warfare Training Software System)."),
    ("DRS = Device Replacement Software — ", "software that replaces real EW hardware in test scenarios (per the RFQ). It is NOT 'Data Recording Subsystem' or 'Digital Receiver System' — both are incorrect for this system."),
    ("WS1 (SG) — ", "Scenario Generator workstation: the only STK-bearing host."),
    ("WS2 (DRS) — ", "hosts the drs-server / drs-bridge services, Kafka, and TimescaleDB; serves the DRS Engineer webapp."),
    ("SG Operator / DRS Engineer — ", "the two operator personas in scope (WS1 and WS2 respectively)."),
    ("Scenario Generator application — ", "the Sg.App C# WPF desktop application on WS1 with STK 12 embedded in-process; the SG Operator's primary surface."),
    ("Operating modes — ", "two independent dimensions: deployment scope (Standalone | Integrated) and data source (Random | Scenario). Distinct from any application packaging."),
])

# ============================================================================
# 2. System overview & context
# ============================================================================
h(doc, "2. System overview and context", 1)

h(doc, "2.1 What EWTSS is", 2)
para(doc,
     "EWTSS supports authoring, computation, telemetry capture, and analysis of GNSS / DRS "
     "hardware test scenarios. It is a Windows-based defence simulation system run on isolated "
     "workstations or a LAN-connected workstation pair. Operators author scenarios in STK terms "
     "(vehicles, sensors, transmitters, coverage definitions, figures of merit), run pre-computed "
     "STK link analysis, and capture live telemetry from up to ~100 DRS instances during exercise "
     "execution. Scenario computation is a pre-computed batch (not real-time); the computed link "
     "analysis is replayed tick-by-tick during execution.")

h(doc, "2.2 System context and boundaries", 2)
para_with_refs(doc,
     "[[FIG:context]] shows the system boundary and its external interfaces. The two operator personas "
     "drive the two workstations inside scope; STK 12 is a third-party engine embedded in-process "
     "on WS1; the Control Center, the per-variant Entity Controller Applications, and the DRS "
     "hardware devices are external. The whole deployment runs on an air-gapped LAN with no "
     "runtime internet.")
figure(doc, "01-system-context.png", "context",
       "EWTSS system context — personas, the WS1+WS2 system boundary, and external interfaces.",
       "portrait")

h(doc, "2.3 External interfaces", 2)
bullets(doc, [
    ("STK 12 Engine (external, third-party) — ", "Ansys/AGI engine accessed via in-process COM on WS1; one licence seat per deployment. Governed by the AGI COM interface contract."),
    ("Control Center (CC) (external, client-owned) — ", "EWTSS exposes integration APIs from SG and/or DRS; the specific surface (endpoints, topics, schemas) is Open and defined once CC integration requirements arrive. Anticipated touchpoints: mission-guidelines delivery, exercise-lifecycle events, aggregate health push, CC-initiated queries."),
    ("Entity Controller Applications (external, client-owned, per-variant) — ", "drs-bridge exchanges IRS-compliant frames over LAN TCP/UDP in Integrated mode."),
    ("DRS hardware devices (external) — ", "real hardware speaking per-variant binary protocols over TCP/UDP."),
])

h(doc, "2.4 Operating modes", 2)
para(doc, "The system runs in one of four mode combinations along two independent axes. All four "
          "reuse the same components; only the active topics, the data source, and the presence of "
          "entity controllers change.")
table(doc, ["Mode", "Deployment", "Entity controllers", "DRS data source", "STK at runtime"], [
    ["Standalone + Random", "WS1 + WS2 only", "None", "C++ random generator", "No"],
    ["Standalone + Scenario", "WS1 + WS2 only", "None", "computed link-analysis (STK pre-computed)", "Pre-compute only"],
    ["Integrated + Random", "+ entity workstations", "Yes", "C++ random generator", "No"],
    ["Integrated + Scenario", "+ entity workstations", "Yes", "computed link-analysis", "Pre-compute only"],
], widths=[1.7, 1.5, 1.1, 1.6, 1.0])
table_caption(doc, "The four operating-mode combinations.")

# ============================================================================
# 3. Architectural drivers
# ============================================================================
h(doc, "3. Architectural drivers", 1)

h(doc, "3.1 Key functional requirements and quantitative scope", 2)
bullets(doc, [
    "Author scenarios against STK 12 and run pre-computed link analysis (range, azimuth, elevation, Doppler, signal strength, signal offsets, channel attenuation) per (sensor, emitter) pair across the exercise window.",
    "Capture and persist live telemetry from up to ~100 DRS instances; display it live with filter/search/sort.",
    "Support 12+ hardware variants through one configuration system: a new variant is one YAML profile + one C++ parser, not a refactor.",
    "Two operator surfaces by persona: SG-side scenario authoring/exercise control (WS1) and a DRS-side hardware health/monitor/config webapp (WS2).",
    "Full RBAC + JWT auth, template-based PDF reports, scenario library lifecycle, and time synchronisation (SG as Time Server) are in scope for the build.",
])
table(doc, ["Dimension", "Value"], [
    ["Hardware variants in scope", "12+ (RDFS, JV/UHF, JHF, SJRR, JLB, JMB, JHB×4, AUS, PADS)"],
    ["Concurrent DRS instances", "Up to 100"],
    ["Per-instance message rate", "~10–20 Hz"],
    ["Sustained throughput target", "~2,000 msg/s, indefinite duration"],
    ["Deployment", "Air-gapped LAN, two Windows workstations (WS1 + WS2)"],
    ["Delivery", "DVD with vendored dependencies + source code"],
    ["Build phase", "Phased build with integration-test checkpoints"],
], widths=[2.6, 3.8])
table_caption(doc, "Quantitative scope of the delivered system.")

h(doc, "3.2 Quality attributes", 2)
para(doc,
     "Performance and scalability are the dominant drivers. EWTSS is engineered to sustain the "
     "required telemetry load profile indefinitely; the design achieves this structurally "
     "rather than by tuning. Table 4 lists the targets and the design approach behind each.")
table(doc, ["Dimension", "Target", "Design approach"], [
    ["Concurrent DRS instances", "100+", "asyncio single event loop; one shared producer per bridge"],
    ["Hardware variants", "12+ (linear in files)", "One YAML profile + one C++ parser per variant"],
    ["Sustained throughput", "2,000+ msg/s, indefinite", "Batched writes (100 msg / 500 ms); async SQLAlchemy + asyncpg"],
    ["Query latency", "Near-constant under growth", "TimescaleDB chunk exclusion + composite index"],
    ["Concurrent UI operators", "Sustained without degradation", "All handlers async; WebSocket fan-out replaces polling"],
    ["Cost of a new DRS type", "One YAML + one C++ file", "Generic bridge supervisor + ICD codegen"],
], widths=[1.9, 1.5, 3.0])
table_caption(doc, "Quality-attribute targets and the design approach behind each.")
bullets(doc, [
    ("Reliability — ", "Kafka durability for replay; manual offset commit only after a successful DB write (no message loss across restart). Telemetry services run as Windows Services."),
    ("Security — ", "air-gapped LAN, no runtime internet; RBAC + JWT issued by drs-server; PostgreSQL inbound restricted to WS1. A security baseline (threat model, data classification, auth lifecycle) is defined, with security review and penetration testing performed during the build."),
    ("Maintainability — ", "adding a hardware variant is one YAML profile + one C++ parser, with no Python, server, or UI changes (accelerated by the ICD codegen tool); a new typed STK entity is localised, additive work (a deliberate choice over a metadata-driven editor, ADR-007)."),
    ("Usability — ", "STK-Insight-grade desktop fidelity (smooth pan/zoom, native drag-handle editing). Load-bearing rule: STK COM event subscriptions are made on-demand (only while placing or editing), never permanently, to preserve interaction latency (ADR-013)."),
    ("Latency budget — ", "tens of milliseconds end-to-end; explicitly NOT sub-millisecond / hardware-in-the-loop. The cross-LAN scenario-publisher path carries a p99 ≤ 30 ms target, validated during integration testing."),
])

h(doc, "3.3 Constraints", 2)
table(doc, ["Constraint", "Rationale"], [
    ["Air-gapped LAN, no runtime internet; all dependencies vendored", "Target deployment environment"],
    ["DVD delivery with vendored deps + pre-built parser .dlls", "Offline installation on the secure segment"],
    ["Windows-only", "Forced by in-process single-threaded-STA STK COM"],
    ["One STK Engine seat per deployment, on WS1 only; WS2 has zero STK", "Per-process STK licensing (ADR-005 / ADR-012)"],
    ["STK one-process invariant (single AgSTKXApplication lifecycle)", "STK COM constraint; one engine lifecycle per process"],
    ["STK version pinned per deployment", "STK COM behaviour is version-specific; no forward-compat promise"],
    ["Two-workstation topology (WS1 + WS2)", "RFQ deployment shape"],
    ["Pre-computed (not real-time) link analysis", "Author → compute → execute workflow"],
    ["Single-user per workstation (no multi-user concurrency)", "Out of scope"],
    ["Cloud / multi-tenant out of scope", "Air-gapped LAN is load-bearing"],
], widths=[3.5, 2.9])
table_caption(doc, "Constraints the architecture must honour.")

h(doc, "3.4 RFQ traceability", 2)
para(doc,
     "Functional scope traces to RFQ Annexure A.1 (Scope of Development) and A.2 (deliverables "
     "and milestones). A full requirement-by-requirement traceability matrix — every numbered "
     "RFQ requirement mapped to where it is satisfied in EWTSS — accompanies the SRS deliverable; "
     "this document satisfies the Milestone-1 \"Software Architecture for SG\" deliverable.")

# ============================================================================
# 4. Architecture overview
# ============================================================================
h(doc, "4. Architecture overview", 1)

h(doc, "4.1 Component structure", 2)
para_with_refs(doc,
     "EWTSS is a partial service-oriented architecture (ADR-002). [[FIG:component]] is the "
     "canonical high-level diagram: WS1 — the Scenario Generator — runs the Sg.App desktop "
     "application (C# WPF) with STK 12 embedded in-process; WS2 hosts drs-bridge, drs-server, "
     "Kafka, TimescaleDB, and the DRS Engineer webapp. Solid edges are present in every "
     "deployment; dashed orange boxes are out-of-scope, client-owned external systems.")
figure(doc, "02-component-overview.png", "component",
       "Component / deployment-component overview.",
       "portrait")

h(doc, "4.2 Layered model", 2)
para_with_refs(doc,
     "The same components factor into seven horizontal layers ([[FIG:layers]]). Each layer talks only "
     "to its immediate neighbour; no layer skips levels. The split point is L3 (Kafka + DB on "
     "WS2): L4–L6 live on WS1, L0–L3 on WS2. The only cross-LAN traffic is L4(WS1)↔L3(WS2) "
     "(computed link-analysis writes) and L5(WS2)↔L6(WS1) (drs-server REST/WebSocket). These layer "
     "rules are load-bearing: violations of them — a layer reaching past its neighbour or "
     "calling synchronously across services — are the class of issue that degrades a system "
     "of this kind under sustained load.")
figure(doc, "03-layered-model.png", "layers",
       "Seven-layer system model with the L3 split point and cross-LAN rules.",
       "portrait")

h(doc, "4.3 Component responsibilities", 2)
bullets(doc, [
    ("Shared domain core — ", "DTO records, the scenario-backend interface (the single boundary to STK), the STK COM adapter, view-models, and the interaction (placement/edit) controller. No COM types leak into the data contracts, enforced by a build-time check. Established."),
    ("Sg.App (the Scenario Generator desktop application) — ", "C# WPF host over the shared domain core: DI wiring, STK ActiveX hosting via WindowsFormsHost, object tree, property panel, keyboard finalize routing, file dialogs, splash. Established."),
    ("drs-server — ", "Python 3.12 FastAPI: Kafka consumers, TimescaleDB batched writes, WebSocket hub to WS1, REST reports/queries, RBAC + JWT, and serves the DRS webapp. Planned."),
    ("drs-bridge — ", "Python asyncio TCP servers + per-variant C++ parser .dlls + a response router. One YAML profile and one C++ parser per variant. A reference parser template is established; per-variant parsers are Planned."),
    ("TimescaleDB — ", "PostgreSQL 16 + Timescale 2.x: hypertables for telemetry; regular tables for users/RBAC, scenarios, library, computed link-analysis. Planned."),
    ("Kafka KRaft — ", "single-broker 3.x (no ZooKeeper); hw.<variant>.<kind> topics + control-plane topics. Single-broker infrastructure established; per-variant data-plane topics added with each variant interface."),
    ("DRS webapp — ", "React (Vite + JS) DRS Engineer surface on WS2: dashboard, per-variant monitor-scan, IP/network config, message logs, per-variant control, consumer-lag health. Planned."),
])

# ============================================================================
# 5. Key architectural decisions
# ============================================================================
h(doc, "5. Key architectural decisions", 1)
para(doc,
     "Table 6 summarises the architectural decisions that govern the delivered Scenario "
     "Generator and telemetry system, each stating what was chosen and the rationale. Status "
     "convention: Accepted = current canon; revisions are noted where a decision superseded "
     "earlier guidance.")
adr_rows = [
    ["001", "Frontend: C# WPF desktop with in-process STK", "STK-Insight-grade fidelity for power users on Windows; validated during design validation", "Accepted"],
    ["002", "Telemetry stack: three-service partial SOA", "drs-server + drs-bridge (Python/C++); scenario authoring + STK in C#", "Accepted"],
    ["003", "Database: TimescaleDB (PostgreSQL 16 + Timescale 2.x)", "Hypertables for telemetry; regular tables for RBAC/scenarios", "Accepted"],
    ["004", "Message bus: Kafka KRaft single-node", "Replay + persistence; KRaft drops ZooKeeper", "Accepted"],
    ["005", "STK access: C# COM in-process", "STK API is C#-first: native typing, microsecond method-call latency", "Accepted"],
    ["006", "STK contract boundary: a backend interface + DTOs", "Single boundary; no COM types in the data contracts; JSON round-trip safe", "Accepted"],
    ["007", "Typed entities, not metadata-driven editor", "Small fixed catalogue favours compile-time safety over a generic framework", "Accepted"],
    ["008", "Desktop GUI: WPF + WindowsFormsHost", "Hosts the STK ActiveX control with validated interaction performance", "Accepted"],
    ["011", "Scenario file format: STK native (.sc/.vdf)", "Round-trip with STK Desktop/Insight is a customer requirement", "Accepted"],
    ["012", "STK licensing: one Engine seat per deployment", "Per-process licensing; only WS1 hosts STK, so the count is unambiguous (1)", "Accepted"],
    ["013", "STK COM events: on-demand only, never permanent", "Permanent COM event subscriptions degrade interaction latency; subscribe only while placing/editing", "Accepted (load-bearing)"],
    ["014", "Facility position via direct geodetic assignment", "Direct geodetic assignment is the reliable position-write path on the target STK build", "Accepted"],
    ["015", "Drag-edit changes committed explicitly on release", "A user drag updates the visual only; an explicit commit persists the change", "Accepted"],
    ["016", "Placement/edit UX: match STK Desktop where possible", "Principle load-bearing; specific keystrokes open for revision", "Accepted; revised"],
    ["017", "STK Mock: test-only, never runtime fallback", "Fail-fast at startup; runtime mocks drift and hide bugs", "Accepted"],
    ["018", "WS2 DRS webapp, served by drs-server (React)", "RFQ-required DRS Engineer surface; React + JavaScript (Vite)", "Accepted"],
]
table(doc, ["ADR", "Decision", "Rationale (short)", "Status"],
      adr_rows, widths=[0.5, 2.2, 2.5, 1.2], font=8)
table_caption(doc, "Architectural decisions governing the delivered system.")
para(doc,
     "Dependency notes: ADR-001 drives ADR-005/006/008/012; ADR-013/014/015 are implementation "
     "invariants that must hold for ADR-008 to deliver its performance/correctness goals "
     "(each was validated during design validation); ADR-018 is the second-persona (DRS Engineer) "
     "commitment and is independent of the Scenario Generator application.")

# ============================================================================
# 6. Workflow & behaviour
# ============================================================================
h(doc, "6. Workflow and behaviour", 1)
para(doc,
     "This section illustrates the major workflows with diagrams. The figures below capture the "
     "load-bearing workflows; additional sequence detail (authentication, library operations, "
     "every execution mode, DRS Engineer flows, error/recovery, and external integration) is "
     "carried in the accompanying interface and test specifications.")

h(doc, "6.1 Operator workflow (end to end)", 2)
para_with_refs(doc,
     "[[FIG:operator]] traces a complete operational cycle: the SG Operator logs in, checks time sync, "
     "manages the EW library, authors a scenario on the GIS map, saves it (edits to compute-"
     "inputs flip it to STALE), computes link analysis when needed, selects an exercise mode, "
     "executes and monitors live, controls playback, and generates a report. The DRS Engineer "
     "works in parallel on WS2. Green nodes are established in the current codebase; amber nodes "
     "are planned for the build.")
figure(doc, "04-operator-workflow.png", "operator",
       "End-to-end operator workflow / operational cycle.",
       "portrait")

h(doc, "6.2 Telemetry data flow", 2)
para_with_refs(doc,
     "[[FIG:dataflow]] shows how telemetry moves through the system — the hot path the design is "
     "built to keep stable. Hardware (or the C++ random generator) feeds drs-bridge, which parses "
     "to JSON and publishes to Kafka; drs-server consumes, writes to TimescaleDB in batches "
     "(100 msg / 500 ms) committing the Kafka offset only after the DB write, and fans out over "
     "WebSocket to the Sg.App panels and the DRS webapp. The low-volume computed link-analysis write from "
     "WS1 is the only other cross-LAN data path.")
figure(doc, "05-data-flow.png", "dataflow",
       "Telemetry data flow and the batched-write hot path.",
       "landscape")

h(doc, "6.3 Control flow", 2)
para_with_refs(doc,
     "[[FIG:control]] shows how commands propagate. The SG Operator's exercise control (start/pause/"
     "resume/stop) is published to Kafka control topics and consumed by drs-bridge's "
     "response router; in Scenario mode the bridge pulls pre-computed values from Sg.App's "
     "scenario-publisher endpoint per tick and emits IRS frames to the entity controllers. The "
     "DRS Engineer's config/control actions flow through drs-server to Kafka control topics. A "
     "SYNC_LOST time-sync event auto-pauses a running exercise.")
figure(doc, "06-control-flow.png", "control",
       "Control / command propagation across WS1, the Kafka control plane, WS2, and entity controllers.",
       "landscape")

h(doc, "6.4 Scenario compute lifecycle", 2)
para_with_refs(doc,
     "[[FIG:compute]] is the scenario compute state machine. A scenario is NOT_COMPUTED on creation/"
     "import/duplicate, becomes COMPUTED after a successful compute, and flips to STALE when an "
     "edit changes compute-inputs (which invalidates the prior computed link-analysis on save). Compute runs as "
     "three transactions so the multi-minute STK loop never holds the cross-LAN write lock, and "
     "is idempotent at every failure point.")
figure(doc, "07-compute-state.png", "compute",
       "Scenario compute lifecycle and the three-transaction write-back.",
       "portrait")

h(doc, "6.5 Exercise execution (Integrated + Scenario)", 2)
para_with_refs(doc,
     "[[FIG:exercise]] is the runtime sequence for the primary mission-execution mode. Each tick: Sg.App "
     "publishes the tick, drs-bridge fetches the pre-computed response from Sg.App, formats IRS "
     "bytes to the entity controllers, captures their responses, and mirrors both entity-response "
     "and telemetry traffic through Kafka into TimescaleDB and back to the operator UI over WebSocket.")
figure(doc, "09-exercise-sequence.png", "exercise",
       "Integrated + Scenario exercise execution sequence.",
       "landscape")

# ============================================================================
# 7. Deployment & runtime view
# ============================================================================
h(doc, "7. Deployment and runtime view", 1)
para_with_refs(doc,
     "[[FIG:deployment]] is the deployment view: two Windows nodes on an air-gapped LAN with their deployed "
     "artifacts, the labelled ports between them, and the external client-owned nodes.")
figure(doc, "08-deployment-view.png", "deployment",
       "Deployment / runtime view — nodes, artifacts, ports, and licences.",
       "landscape")

h(doc, "7.1 What runs where", 2)
table(doc, ["", "WS1 — Scenario Generator", "WS2 — DRS"], [
    ["Role", "Scenario authoring, STK compute, exercise control, reports, admin", "Telemetry pipeline, TimescaleDB, Kafka, DRS Engineer webapp"],
    ["Artifacts", "Sg.App.exe (WPF, .NET 8 Desktop Runtime) + STK 12 + PIAs", "drs-server + drs-bridge (+ C++ .dlls) + Kafka + PostgreSQL/Timescale"],
    ["STK seats", "1 (Engine)", "0"],
    ["GPU", "Dedicated strongly recommended", "Not required"],
], widths=[1.3, 2.6, 2.5])
table_caption(doc, "Topology — what runs where.")

h(doc, "7.2 Hardware and software", 2)
table(doc, ["Resource", "WS1", "WS2"], [
    ["OS", "Windows 11", "Windows 11"],
    ["CPU", "x64, 4 cores min / 8 rec", "8 cores min / 16 rec"],
    ["RAM", "16 GB min / 32 GB (>50 entities)", "32 GB min / 64 GB for 100-DRS load"],
    ["GPU", "GTX 1650 / RTX 3050+ recommended", "Not required"],
    ["Disk", "SSD; ~5 GB STK + app + scenarios", "SSD; ~1 TB recommended (retention)"],
    ["Key software", "STK 12, .NET 8 Desktop Runtime, VC++ redist", "Python 3.12, PostgreSQL 16 + Timescale 2.x, Kafka 3.x KRaft, NSSM"],
], widths=[1.2, 2.6, 2.6])
table_caption(doc, "Hardware and software requirements.")

h(doc, "7.3 Network and ports", 2)
bullets(doc, [
    ("PostgreSQL 5432 — ", "WS1 → WS2 (computed link-analysis writes); inbound restricted to WS1."),
    ("drs-server REST/WebSocket 8000 — ", "WS2 → WS1 (telemetry reads)."),
    ("Kafka 9092 — ", "loopback on WS2."),
    ("NTP UDP 123 — ", "WS2 → WS1 (WS1 is Stratum-1 time server; < 10 ms offset gate)."),
    ("DRS hardware / entity controllers — ", "TCP/UDP per protocol / IRS spec."),
])

h(doc, "7.4 Critical setup considerations", 2)
bullets(doc, [
    ("GPU preference (critical) — ", "Sg.App.exe must be set to High Performance on dual-GPU machines or globe interaction is sluggish regardless of code quality."),
    ("Windows Time Service must be Disabled on WS2 — ", "Meinberg NTP owns the clock; a GPO can re-enable it and break the sync smoke test."),
    ("DVD ships pre-built parser .dlls — ", "the C++ toolchain is only needed if rebuilding on-site."),
])

# ============================================================================
# 8. Delivery approach & scope boundaries
# ============================================================================
h(doc, "8. Delivery approach and scope boundaries", 1)

h(doc, "8.1 Delivery approach", 2)
para(doc,
     "The architecture in this document is the target end-state for the delivered system. It is "
     "delivered through a phased build with integration-test checkpoints, so that the telemetry "
     "pipeline, the Scenario Generator capabilities, and the DRS Engineer webapp are each "
     "exercised against acceptance criteria as they come together. Capabilities already validated "
     "in the current codebase (the scenario-authoring core and its STK integration, the "
     "time-synchronisation service, the message-bus infrastructure, and the reference hardware-"
     "parser template) are marked Established in this document; the remainder are Planned for "
     "the build.")

h(doc, "8.2 Out of scope", 2)
bullets(doc, [
    "Multi-user authoring concurrency; real-time (mid-execution) scenario compute; and cloud / multi-tenant deployment — out of scope (the air-gapped, single-site LAN is load-bearing for the architecture).",
    "Cross-deployment scenario portability — out of scope (single-site delivery; the STK .sc/.vdf export exists for STK Desktop interoperability and backup, not for moving scenarios between deployments).",
])

force_update_fields(doc)
doc.save(OUT)
print("WROTE", OUT)
print("Figures embedded:", fig_n, "| Tables:", tbl_n)
