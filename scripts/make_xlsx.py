#!/usr/bin/env python3
"""Minimal stdlib-only .xlsx writer (no openpyxl dependency needed/available offline).

Usage: called programmatically via build_workbook(sheets, out_path)
where sheets = [(sheet_name, header_row, [data_rows...]), ...]
"""
import zipfile
import xml.sax.saxutils as sax
import re


def esc(v):
    return sax.escape(str(v))


def col_letter(n):
    s = ""
    n += 1
    while n > 0:
        n, r = divmod(n - 1, 26)
        s = chr(65 + r) + s
    return s


def sheet_xml(header, rows, freeze_header=True, col_widths=None, tab_selected=False):
    all_rows = [header] + rows
    ncols = len(header)
    nrows = len(all_rows)
    out = []
    out.append('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>')
    out.append('<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">')
    # CT_Worksheet requires this element order: dimension, sheetViews, cols, sheetData, autoFilter, ...
    out.append(f'<dimension ref="A1:{col_letter(ncols-1)}{nrows}"/>')
    if freeze_header:
        tab_attr = ' tabSelected="1"' if tab_selected else ''
        out.append(f'<sheetViews><sheetView{tab_attr} workbookViewId="0">'
                    '<pane ySplit="1" topLeftCell="A2" activePane="bottomLeft" state="frozen"/>'
                    '<selection pane="bottomLeft" activeCell="A2" sqref="A2"/>'
                    '</sheetView></sheetViews>')
    if col_widths:
        out.append('<cols>')
        for i, w in enumerate(col_widths):
            out.append(f'<col min="{i+1}" max="{i+1}" width="{w}" customWidth="1"/>')
        out.append('</cols>')
    out.append('<sheetData>')
    for r_idx, row in enumerate(all_rows, start=1):
        out.append(f'<row r="{r_idx}">')
        style = ' s="1"' if r_idx == 1 else ''
        for c_idx, val in enumerate(row):
            ref = f"{col_letter(c_idx)}{r_idx}"
            if val is None or val == "":
                out.append(f'<c r="{ref}"{style}/>')
            elif r_idx > 1 and isinstance(val, (int, float)) and not isinstance(val, bool):
                out.append(f'<c r="{ref}"{style}><v>{val}</v></c>')
            else:
                out.append(
                    f'<c r="{ref}" t="inlineStr"{style}><is><t xml:space="preserve">{esc(val)}</t></is></c>'
                )
        out.append('</row>')
    out.append('</sheetData>')
    out.append(f'<autoFilter ref="A1:{col_letter(ncols-1)}{nrows}"/>')
    out.append('</worksheet>')
    return "\n".join(out)


CONTENT_TYPES = '''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
<Default Extension="xml" ContentType="application/xml"/>
<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>
<Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/>
{sheet_overrides}
</Types>'''

ROOT_RELS = '''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>
</Relationships>'''

STYLES = '''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">
<fonts count="2">
<font><sz val="10"/><name val="Calibri"/></font>
<font><b/><sz val="10"/><color rgb="FFFFFFFF"/><name val="Calibri"/></font>
</fonts>
<fills count="3">
<fill><patternFill patternType="none"/></fill>
<fill><patternFill patternType="gray125"/></fill>
<fill><patternFill patternType="solid"><fgColor rgb="FF1F4E78"/><bgColor indexed="64"/></patternFill></fill>
</fills>
<borders count="1"><border><left/><right/><top/><bottom/><diagonal/></border></borders>
<cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs>
<cellXfs count="2">
<xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/>
<xf numFmtId="0" fontId="1" fillId="2" borderId="0" xfId="0" applyFont="1" applyFill="1"><alignment vertical="center"/></xf>
</cellXfs>
</styleSheet>'''


def build_workbook(sheets, out_path):
    """sheets: list of (name, header, rows, col_widths_or_None)"""
    n = len(sheets)
    sheet_overrides = "\n".join(
        f'<Override PartName="/xl/worksheets/sheet{i+1}.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>'
        for i in range(n)
    )
    content_types = CONTENT_TYPES.format(sheet_overrides=sheet_overrides)

    wb_sheets_xml = "\n".join(
        f'<sheet name="{esc(name)}" sheetId="{i+1}" r:id="rId{i+1}"/>'
        for i, (name, *_rest) in enumerate(sheets)
    )
    workbook_xml = f'''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">
<sheets>{wb_sheets_xml}</sheets>
</workbook>'''

    wb_rels_xml_parts = "\n".join(
        f'<Relationship Id="rId{i+1}" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet{i+1}.xml"/>'
        for i in range(n)
    )
    wb_rels_xml_parts += f'\n<Relationship Id="rId{n+1}" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>'
    workbook_rels_xml = f'''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
{wb_rels_xml_parts}
</Relationships>'''

    with zipfile.ZipFile(out_path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", content_types)
        z.writestr("_rels/.rels", ROOT_RELS)
        z.writestr("xl/workbook.xml", workbook_xml)
        z.writestr("xl/_rels/workbook.xml.rels", workbook_rels_xml)
        z.writestr("xl/styles.xml", STYLES)
        for i, sheet in enumerate(sheets):
            name, header, rows = sheet[0], sheet[1], sheet[2]
            col_widths = sheet[3] if len(sheet) > 3 else None
            xml_content = sheet_xml(header, rows, col_widths=col_widths, tab_selected=(i == 0))
            z.writestr(f"xl/worksheets/sheet{i+1}.xml", xml_content)

    print(f"wrote {out_path}")
