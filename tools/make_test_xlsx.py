#!/usr/bin/env python3
"""Generate the .xlsx fixtures the hardpoint reader tests run against.

Standard library only -- `zipfile` writes real DEFLATE members, so the fixtures
exercise the same decompression path a workbook out of Excel does. Committing
binary blobs instead would hide what each fixture is actually testing.

Usage: make_test_xlsx.py <output-directory>
"""

import sys
import zipfile
from pathlib import Path

CONTENT_TYPES = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
<Default Extension="xml" ContentType="application/xml"/>
<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>
{sheet_overrides}
<Override PartName="/xl/sharedStrings.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml"/>
<Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/>
</Types>"""

ROOT_RELS = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>
</Relationships>"""

STYLES = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">
<fonts count="1"><font><sz val="11"/><name val="Calibri"/></font></fonts>
<fills count="1"><fill><patternFill patternType="none"/></fill></fills>
<borders count="1"><border/></borders>
<cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs>
<cellXfs count="2"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/>
<xf numFmtId="2" fontId="0" fillId="0" borderId="0" xfId="0" applyNumberFormat="1"/></cellXfs>
</styleSheet>"""

# A part no spreadsheet library understands, present to prove that saving copies
# through everything it was not asked to change.
CUSTOM_PART = """<?xml version="1.0" encoding="UTF-8"?>
<vaultMetadata><revision>C</revision><checkedOutBy>nobody</checkedOutBy></vaultMetadata>"""


def escape(text):
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


class SharedStrings:
    def __init__(self):
        self.items = []
        self.index = {}

    def add(self, text):
        if text not in self.index:
            self.index[text] = len(self.items)
            self.items.append(text)
        return self.index[text]

    def xml(self):
        items = "".join(f"<si><t>{escape(t)}</t></si>" for t in self.items)
        return (
            '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
            '<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" '
            f'count="{len(self.items)}" uniqueCount="{len(self.items)}">{items}</sst>'
        )


def column_name(index):
    letters = ""
    while index > 0:
        index, remainder = divmod(index - 1, 26)
        letters = chr(ord("A") + remainder) + letters
    return letters


def sheet_xml(rows, shared, inline_strings=False, name_column=1, value_column=2, cell_refs=True):
    """rows: list of (name, value_text_or_None). A name of None leaves the row blank.

    With cell_refs=False the cells carry no r attribute and are positional, which
    the format allows and some non-Excel writers do.
    """
    body = []
    for number, (name, value) in enumerate(rows, start=1):
        cells = []
        if name is not None:
            ref = f' r="{column_name(name_column)}{number}"' if cell_refs else ""
            if inline_strings:
                cells.append(f'<c{ref} t="inlineStr"><is><t>{escape(name)}</t></is></c>')
            else:
                cells.append(f'<c{ref} t="s"><v>{shared.add(name)}</v></c>')
        elif not cell_refs and value is not None:
            cells.append("<c/>")  # hold the column open for the positional value
        if value is not None:
            ref = f' r="{column_name(value_column)}{number}"' if cell_refs else ""
            # s="1" is a number format; it has to survive the round trip.
            cells.append(f'<c{ref} s="1"><v>{value}</v></c>')
        if cells:
            body.append(f'<row r="{number}">{"".join(cells)}</row>')

    return (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
        '<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">'
        f'<dimension ref="A1:{column_name(value_column)}{len(rows)}"/>'
        '<sheetViews><sheetView workbookViewId="0"/></sheetViews>'
        '<cols><col min="1" max="1" width="22" customWidth="1"/></cols>'
        f'<sheetData>{"".join(body)}</sheetData>'
        "</worksheet>"
    )


def write_workbook(path, sheets, shared, extra_parts=None):
    """sheets: list of (display name, sheet xml)."""
    overrides = "\n".join(
        f'<Override PartName="/xl/worksheets/sheet{i}.xml" '
        'ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>'
        for i in range(1, len(sheets) + 1)
    )
    sheet_tags = "".join(
        f'<sheet name="{escape(name)}" sheetId="{i}" r:id="rId{i}"/>'
        for i, (name, _) in enumerate(sheets, start=1)
    )
    workbook = (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
        '<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" '
        'xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">'
        f"<sheets>{sheet_tags}</sheets>"
        '<calcPr calcId="191029"/>'
        "</workbook>"
    )
    relationships = "".join(
        f'<Relationship Id="rId{i}" '
        'Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" '
        f'Target="worksheets/sheet{i}.xml"/>'
        for i in range(1, len(sheets) + 1)
    )
    next_id = len(sheets) + 1
    relationships += (
        f'<Relationship Id="rId{next_id}" '
        'Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" '
        'Target="sharedStrings.xml"/>'
        f'<Relationship Id="rId{next_id + 1}" '
        'Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" '
        'Target="styles.xml"/>'
    )
    workbook_rels = (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
        '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
        f"{relationships}</Relationships>"
    )

    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("[Content_Types].xml", CONTENT_TYPES.format(sheet_overrides=overrides))
        archive.writestr("_rels/.rels", ROOT_RELS)
        archive.writestr("xl/workbook.xml", workbook)
        archive.writestr("xl/_rels/workbook.xml.rels", workbook_rels)
        for i, (_, xml) in enumerate(sheets, start=1):
            archive.writestr(f"xl/worksheets/sheet{i}.xml", xml)
        archive.writestr("xl/sharedStrings.xml", shared.xml())
        archive.writestr("xl/styles.xml", STYLES)
        for name, content in (extra_parts or {}).items():
            archive.writestr(name, content)


# The value text is written verbatim, so a fixture can carry the full-precision
# digits Excel actually emits and the round-trip test can look for them again.
HARDPOINT_ROWS = [
    ("Name", None),
    ("F_LCA_O_x", "-544.26"),
    ("F_LCA_O_y", "554.41200000000003"),
    ("F_LCA_O_z", "138.408"),
    ("F_UCA_O_x", "-556.894"),
    ("F_UCA_O_y", "548.23699999999997"),
    ("F_UCA_O_z", "318.38799999999998"),
    (None, None),
    ("Wheelbase", "1530"),
    ("R_LCA_O_x", "-2068.62"),
    ("R_LCA_O_y", "554.78399999999999"),
    ("R_LCA_O_z", "138.409"),
    ("F_Damper_I_x", "-343.80632000000003"),
    ("F_Damper_I_y", "139.1987"),
    ("F_Damper_I_z", "601.01023999999995"),
    ("R_Damper_I_x", "-2215.93204"),
    ("R_Damper_I_y", "231.07736"),
]


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)

    # 1. The ordinary case: a header row, shared-string names, one row that is
    #    not a coordinate at all, and one point missing its z.
    shared = SharedStrings()
    shared.add("Value")  # so the header row's second cell is a shared string too
    rows = list(HARDPOINT_ROWS)
    rows[0] = ("Name", None)
    sheet = sheet_xml(rows, shared)
    # sheet_xml() only writes numbers into the value column, so the "Value"
    # header cell is spliced in after the "Name" one it wrote.
    name_cell = '<c r="A1" t="s"><v>%d</v></c>' % shared.index["Name"]
    value_cell = '<c r="B1" t="s"><v>%d</v></c>' % shared.index["Value"]
    assert name_cell in sheet
    sheet = sheet.replace(name_cell, name_cell + value_cell, 1)
    write_workbook(
        out / "hardpoints.xlsx",
        [("Geometry", sheet)],
        shared,
        extra_parts={"customXml/item1.xml": CUSTOM_PART},
    )

    # 2. The table on the second sheet, behind a cover sheet with no hardpoints.
    shared2 = SharedStrings()
    cover = sheet_xml([("Bremergy 26", None), ("Revision", "3")], shared2)
    table = sheet_xml(HARDPOINT_ROWS, shared2)
    write_workbook(out / "second_sheet.xlsx", [("Cover", cover), ("Points", table)], shared2)

    # 3. No header row, inline strings, and the table shifted to columns C/D --
    #    which together mean the reader cannot fall back on any convention.
    shared3 = SharedStrings()
    inline = sheet_xml(
        [("Name", None)] + HARDPOINT_ROWS[1:],
        shared3,
        inline_strings=True,
        name_column=3,
        value_column=4,
    )
    write_workbook(out / "inline_strings.xlsx", [("Sheet1", inline)], shared3)

    # 4. Cells with no r attribute, so both the reader and the writer have to
    #    track the position themselves.
    shared4 = SharedStrings()
    positional = sheet_xml(HARDPOINT_ROWS, shared4, cell_refs=False)
    write_workbook(out / "no_cell_refs.xlsx", [("Sheet1", positional)], shared4)

    # 5. Not a workbook at all.
    (out / "not_a_zip.xlsx").write_bytes(b"PK\x03\x04 this is not really a zip file")

    # 6. A table with more to it than names and numbers: a unit and a note in
    #    the columns beside every coordinate, and a formatted row with nothing in
    #    it below the table. Deleting a point has to blank its two cells and
    #    leave the unit and the note standing; appending has to go below the
    #    empty formatted row rather than be numbered the same as it.
    shared6 = SharedStrings()
    header = "".join(
        f'<c r="{column_name(i)}1" t="s"><v>{shared6.add(text)}</v></c>'
        for i, text in enumerate(["Name", "Value", "Unit", "Note"], start=1)
    )
    body = [f'<row r="1">{header}</row>']
    extra_rows = [
        ("F_LCA_O_x", "-544.26", "measured"),
        ("F_LCA_O_y", "554.412", "measured"),
        ("F_LCA_O_z", "138.408", "measured"),
        ("F_UCA_O_x", "-556.894", "from CAD"),
        ("F_UCA_O_y", "548.237", "from CAD"),
        ("F_UCA_O_z", "318.388", "from CAD"),
    ]
    for number, (name, value, note) in enumerate(extra_rows, start=2):
        body.append(
            f'<row r="{number}">'
            f'<c r="A{number}" t="s"><v>{shared6.add(name)}</v></c>'
            f'<c r="B{number}" s="1"><v>{value}</v></c>'
            f'<c r="C{number}" t="s"><v>{shared6.add("mm")}</v></c>'
            f'<c r="D{number}" t="s"><v>{shared6.add(note)}</v></c>'
            "</row>"
        )
    # Row 10: styled and empty, two rows below the table.
    body.append('<row r="10"><c r="A10" s="1"/><c r="B10" s="1"/></row>')
    extra = (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
        '<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">'
        '<dimension ref="A1:D10"/>'
        f'<sheetData>{"".join(body)}</sheetData>'
        "</worksheet>"
    )
    write_workbook(out / "extra_columns.xlsx", [("Geometry", extra)], shared6)

    print(f"xlsx fixtures written to {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
