#!/usr/bin/env python3
"""Generate the blank hardpoint workbook the application ships.

A project that has points and no workbook of its own -- one started from
Hardpoints > New Hardpoint Table, or from a generated corner -- gets a copy of
this, filled through the same writer that adds mirrored points to an imported
workbook. It is one sheet with a Name/Value header row and nothing else.

Standard library only, like the test fixtures, and generated at configure time
rather than committed: CLAUDE.md keeps binaries out of git. Every timestamp in
the archive is fixed, so the resource compiled into the binary is the same bytes
on every build.

Usage: make_blank_hardpoints_xlsx.py <output-file>
"""

import sys
import zipfile
from pathlib import Path

# The earliest date a ZIP can say. Anything later would make the archive, and
# the binary it is compiled into, differ from one build to the next.
FIXED_TIME = (1980, 1, 1, 0, 0, 0)

SHEET_NAME = "Hardpoints"

CONTENT_TYPES = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
<Default Extension="xml" ContentType="application/xml"/>
<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>
<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>
<Override PartName="/xl/sharedStrings.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml"/>
<Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/>
</Types>"""

ROOT_RELS = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>
</Relationships>"""

WORKBOOK = f"""<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">
<sheets><sheet name="{SHEET_NAME}" sheetId="1" r:id="rId1"/></sheets>
<calcPr calcId="191029"/>
</workbook>"""

WORKBOOK_RELS = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>
<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" Target="sharedStrings.xml"/>
<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>
</Relationships>"""

# Two cell formats: the default, and a bold one for the header row.
STYLES = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">
<fonts count="2"><font><sz val="11"/><name val="Calibri"/></font><font><b/><sz val="11"/><name val="Calibri"/></font></fonts>
<fills count="2"><fill><patternFill patternType="none"/></fill><fill><patternFill patternType="gray125"/></fill></fills>
<borders count="1"><border><left/><right/><top/><bottom/><diagonal/></border></borders>
<cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs>
<cellXfs count="2"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/><xf numFmtId="0" fontId="1" fillId="0" borderId="0" xfId="0" applyFont="1"/></cellXfs>
<cellStyles count="1"><cellStyle name="Normal" xfId="0" builtinId="0"/></cellStyles>
</styleSheet>"""

SHARED_STRINGS = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="2" uniqueCount="2"><si><t>Name</t></si><si><t>Value</t></si></sst>"""

# The header row is the whole of the content. The reader finds the table by
# these two words, and the writer appends every point below them.
SHEET = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">
<dimension ref="A1:B1"/>
<sheetViews><sheetView workbookViewId="0"><pane ySplit="1" topLeftCell="A2" activePane="bottomLeft" state="frozen"/></sheetView></sheetViews>
<sheetFormatPr defaultRowHeight="15"/>
<cols><col min="1" max="1" width="28" customWidth="1"/><col min="2" max="2" width="16" customWidth="1"/></cols>
<sheetData><row r="1"><c r="A1" s="1" t="s"><v>0</v></c><c r="B1" s="1" t="s"><v>1</v></c></row></sheetData>
</worksheet>"""

PARTS = [
    ("[Content_Types].xml", CONTENT_TYPES),
    ("_rels/.rels", ROOT_RELS),
    ("xl/workbook.xml", WORKBOOK),
    ("xl/_rels/workbook.xml.rels", WORKBOOK_RELS),
    ("xl/worksheets/sheet1.xml", SHEET),
    ("xl/sharedStrings.xml", SHARED_STRINGS),
    ("xl/styles.xml", STYLES),
]


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    out = Path(sys.argv[1])
    out.parent.mkdir(parents=True, exist_ok=True)

    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as archive:
        for name, content in PARTS:
            info = zipfile.ZipInfo(name, date_time=FIXED_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            # Regular file, rw-r--r--, so the external attributes do not vary
            # with the umask of whoever configured the build.
            info.external_attr = 0o100644 << 16
            archive.writestr(info, content)

    print(f"blank hardpoint workbook written to {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
