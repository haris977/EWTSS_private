# -*- coding: utf-8 -*-
"""Build a Word (.docx) version of the EWTSS v2 WBS from the Zoho CSV.
Stdlib only (csv, zipfile) -- no third-party deps, air-gap safe.
"""
import csv, zipfile, sys, io

CSV_PATH = r"e:\GitHub\ewtss-v2-pub\docs\ewtss\wbs\ewtss-v2-wbs-zoho.csv"
OUT_PATH = r"e:\GitHub\ewtss-v2-pub\docs\ewtss\wbs\ewtss-v2-wbs.docx"

W = 'xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"'

def esc(t):
    return (t or "").replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")

def run(text, bold=False, sz=18):
    rpr = "<w:rPr>" + ("<w:b/>" if bold else "") + ('<w:sz w:val="%d"/>' % sz) + "</w:rPr>"
    return '<w:r>%s<w:t xml:space="preserve">%s</w:t></w:r>' % (rpr, esc(text))

def para(text="", style=None, bold=False, sz=18, bullet=False, after=60):
    ppr = "<w:pPr>"
    if style:
        ppr += '<w:pStyle w:val="%s"/>' % style
    if bullet:
        ppr += '<w:numPr><w:ilvl w:val="0"/><w:numId w:val="1"/></w:numPr>'
    ppr += '<w:spacing w:after="%d"/>' % after
    ppr += "</w:pPr>"
    body = run(text, bold=bold, sz=sz) if text else ""
    return "<w:p>%s%s</w:p>" % (ppr, body)

def cell(text, width, bold=False, shade=None, sz=16):
    tcpr = '<w:tcPr><w:tcW w:w="%d" w:type="dxa"/>' % width
    if shade:
        tcpr += '<w:shd w:val="clear" w:color="auto" w:fill="%s"/>' % shade
    tcpr += '<w:tcMar><w:top w:w="20" w:type="dxa"/><w:bottom w:w="20" w:type="dxa"/>' \
            '<w:left w:w="60" w:type="dxa"/><w:right w:w="60" w:type="dxa"/></w:tcMar></w:tcPr>'
    p = '<w:p><w:pPr><w:spacing w:after="0" w:line="200" w:lineRule="atLeast"/></w:pPr>%s</w:p>' % run(text, bold=bold, sz=sz)
    return "<w:tc>%s%s</w:tc>" % (tcpr, p)

def table(headers, rows, widths, header_shade="1F3864", header_color_white=True):
    borders = ('<w:tblBorders>'
               '<w:top w:val="single" w:sz="4" w:color="BFBFBF"/>'
               '<w:left w:val="single" w:sz="4" w:color="BFBFBF"/>'
               '<w:bottom w:val="single" w:sz="4" w:color="BFBFBF"/>'
               '<w:right w:val="single" w:sz="4" w:color="BFBFBF"/>'
               '<w:insideH w:val="single" w:sz="4" w:color="BFBFBF"/>'
               '<w:insideV w:val="single" w:sz="4" w:color="BFBFBF"/>'
               '</w:tblBorders>')
    tblpr = '<w:tblPr><w:tblW w:w="%d" w:type="dxa"/><w:tblLayout w:type="fixed"/>%s</w:tblPr>' % (sum(widths), borders)
    grid = "<w:tblGrid>" + "".join('<w:gridCol w:w="%d"/>' % w for w in widths) + "</w:tblGrid>"
    # header row (repeats on each page)
    hcells = "".join(cell_header(h, w, header_shade, header_color_white) for h, w in zip(headers, widths))
    hrow = '<w:tr><w:trPr><w:tblHeader/></w:trPr>%s</w:tr>' % hcells
    body_rows = []
    for i, r in enumerate(rows):
        shade = "EAF1FB" if i % 2 else None
        cells = "".join(cell(v, w, shade=shade) for v, w in zip(r, widths))
        body_rows.append("<w:tr>%s</w:tr>" % cells)
    return "<w:tbl>%s%s%s%s</w:tbl>" % (tblpr, grid, hrow, "".join(body_rows))

def cell_header(text, width, shade, white):
    color = '<w:color w:val="FFFFFF"/>' if white else ""
    tcpr = ('<w:tcPr><w:tcW w:w="%d" w:type="dxa"/>'
            '<w:shd w:val="clear" w:color="auto" w:fill="%s"/>'
            '<w:tcMar><w:top w:w="30" w:type="dxa"/><w:bottom w:w="30" w:type="dxa"/>'
            '<w:left w:w="60" w:type="dxa"/><w:right w:w="60" w:type="dxa"/></w:tcMar></w:tcPr>') % (width, shade)
    rpr = '<w:rPr><w:b/>%s<w:sz w:val="16"/></w:rPr>' % color
    p = '<w:p><w:pPr><w:spacing w:after="0"/></w:pPr><w:r>%s<w:t xml:space="preserve">%s</w:t></w:r></w:p>' % (rpr, esc(text))
    return "<w:tc>%s%s</w:tc>" % (tcpr, p)

# ---- read CSV ----
with io.open(CSV_PATH, "r", encoding="utf-8-sig", newline="") as f:
    reader = csv.reader(f)
    all_rows = list(reader)
headers = all_rows[0]
data = all_rows[1:]
col_widths = [1600, 2500, 1800, 1000, 800, 2700, 4000]  # = 14400 twips

# ---- prose content ----
legend = [
    ("F", "drs-server lead (Senior Python)"),
    ("A", "drs-bridge Python + C# scenario-publisher integration"),
    ("B", "SG (C# specialist)"),
    ("C", "C++ parser libraries"),
    ("D", "cross-stack lead - architect, integration-test owner, infra/packaging"),
    ("E", "drs-server REST/auth/reports (React dev, cross-trained to Python)"),
    ("G", "DRS webapp lead (React)"),
    ("PL / AL", "project lead / architecture lead"),
    ("Ops / Sec", "ops-lab lead / security tester"),
]
decisions = [
    "B1.4 - CC integration API surface: externally blocked on the client's Control Center requirements. Scope only the v2-decidable surface until the client responds.",
    "B1.44 - JVM licence for the Kafka runtime: OpenJDK/Temurin is GPLv2+Classpath-Exception vs the handbook's \"GPL (all versions)\" block-list. PL+D ruling needed before air-gap packaging.",
    "B1.45 - DRS webapp checkJs gate: editor-only vs enforced tsc vs status-quo. PL call before Phase-6 webapp surfaces.",
    "B1.15 - Blue/Red Line to entity-type mapping: needs an ADR before entity property panels.",
    "Variant count/priority list: parser & monitor-scan tasks are batched (first / 2-3 / 4-6 / 7-9 / 10-12); the actual list and ordering must come from the customer before per-variant durations are firm.",
    "STK dev-seat / licence count: tasks assume working STK 12.9 seats (procured separately) - confirm covered.",
]
crosscutting = [
    "CI bring-up harness - org-hosted CI runners are currently paused (.github/disabled/ci.yml); integration suite runs locally for now.",
    "Polyrepo migration + release manifests - repository-and-release-strategy mandates the six-repo split + ewtss-release manifests.",
    "Air-gap dependency vendoring + WiX/DVD packaging - gate the Phase-6 install gate; vendoring becomes load-bearing after week 8.",
    "Build toolchain on the secure segment - build-from-source offline (vendored Node 22 + offline npm cache).",
]
caveat = ("Several rows are already partly/fully implemented in-repo and are listed for tracking, not fresh build: "
          "Time sync (B1.3), DRS roster store/publisher/reconcile/per-instance transport (B1.43), webapp scaffold, "
          "Kafka infra layer, reference parser template, security design + contracts scaffold. Mark these % complete "
          "or Closed on import rather than scheduling them anew.")
import_steps = [
    "Projects > your project > Tasks > More (...) menu > Import > choose ewtss-v2-wbs-zoho.csv.",
    "Map columns: Task List > Task List; Task Name > Task Name; Parent Task > Parent Task (for subtasks); Owner > Owner/Assignee; Duration (days) > Duration; Dependency > Predecessor/Dependency; Description > Description.",
    "The file is UTF-8 and fully quoted. Hierarchy is expressed only via Task List + Parent Task; dependencies reference exact Task Names so they resolve on import.",
    "Apply the team-name mapping (owner legend above) before importing the Owner column.",
]

# ---- assemble body ----
parts = []
parts.append(para("EWTSS v2 - Work Breakdown Structure", style="Title"))
parts.append(para("For Zoho Projects import. Prepared 2026-06-29 - draft for PM review. PROPRIETARY & CONFIDENTIAL - internal distribution only.", sz=18))
parts.append(para("Derived from the design docs: v2-execution-plan.md (team, 17-week / 7-phase structure, per-person ownership, critical path, integration gates) and design-backlog.md (Milestone-1 design items B1.x + documentation deliverables). One task per row.", sz=18, after=120))

parts.append(para("Owner legend", style="Heading2"))
parts.append(para("The repo names people by role ID, not by name. Map A-G and the leads to real people before scheduling.", sz=18))
parts.append(table(["ID", "Role"], legend, [1600, 12800]))
parts.append(para(after=80))

parts.append(para("Work breakdown structure", style="Heading2"))
parts.append(table(headers, data, col_widths))
parts.append(para(after=80))

parts.append(para("Open decisions to resolve before / early in the build", style="Heading2"))
for d in decisions:
    parts.append(para(d, bullet=True, sz=18))

parts.append(para("Cross-cutting work surfaced as explicit tasks", style="Heading2"))
for c in crosscutting:
    parts.append(para(c, bullet=True, sz=18))

parts.append(para("Status caveat", style="Heading2"))
parts.append(para(caveat, sz=18, after=120))

parts.append(para("Zoho Projects import steps", style="Heading2"))
for s in import_steps:
    parts.append(para(s, bullet=True, sz=18))

sectpr = ('<w:sectPr><w:pgSz w:w="15840" w:h="12240" w:orient="landscape"/>'
          '<w:pgMar w:top="720" w:right="720" w:bottom="720" w:left="720" w:header="360" w:footer="360" w:gutter="0"/></w:sectPr>')

document = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
            '<w:document %s><w:body>%s%s</w:body></w:document>') % (W, "".join(parts), sectpr)

styles = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
          '<w:styles %s>'
          '<w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii="Calibri" w:hAnsi="Calibri"/><w:sz w:val="18"/></w:rPr></w:rPrDefault></w:docDefaults>'
          '<w:style w:type="paragraph" w:default="1" w:styleId="Normal"><w:name w:val="Normal"/></w:style>'
          '<w:style w:type="paragraph" w:styleId="Title"><w:name w:val="Title"/><w:pPr><w:spacing w:after="120"/></w:pPr><w:rPr><w:b/><w:sz w:val="40"/><w:color w:val="1F3864"/></w:rPr></w:style>'
          '<w:style w:type="paragraph" w:styleId="Heading2"><w:name w:val="heading 2"/><w:basedOn w:val="Normal"/><w:next w:val="Normal"/><w:pPr><w:spacing w:before="200" w:after="80"/><w:outlineLvl w:val="1"/></w:pPr><w:rPr><w:b/><w:sz w:val="26"/><w:color w:val="2E74B5"/></w:rPr></w:style>'
          '</w:styles>') % W

numbering = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
             '<w:numbering %s>'
             '<w:abstractNum w:abstractNumId="0"><w:lvl w:ilvl="0"><w:start w:val="1"/><w:numFmt w:val="bullet"/><w:lvlText w:val="•"/><w:lvlJc w:val="left"/>'
             '<w:pPr><w:ind w:left="360" w:hanging="360"/></w:pPr></w:lvl></w:abstractNum>'
             '<w:num w:numId="1"><w:abstractNumId w:val="0"/></w:num>'
             '</w:numbering>') % W

content_types = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
                 '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
                 '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
                 '<Default Extension="xml" ContentType="application/xml"/>'
                 '<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>'
                 '<Override PartName="/word/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml"/>'
                 '<Override PartName="/word/numbering.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml"/>'
                 '</Types>')

rels = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
        '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>'
        '</Relationships>')

doc_rels = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
            '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
            '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>'
            '<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/numbering" Target="numbering.xml"/>'
            '</Relationships>')

with zipfile.ZipFile(OUT_PATH, "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("[Content_Types].xml", content_types)
    z.writestr("_rels/.rels", rels)
    z.writestr("word/document.xml", document)
    z.writestr("word/styles.xml", styles)
    z.writestr("word/numbering.xml", numbering)
    z.writestr("word/_rels/document.xml.rels", doc_rels)

print("wrote", OUT_PATH, "rows:", len(data))
