#!/usr/bin/env python3
"""Rebuilds the password-protected Office fixtures OfficeEncryptionTest reads.

    python3 Tests/fixtures/make-office-encrypted-fixtures.py

Needs msoffcrypto-tool (pip install msoffcrypto-tool), an encryptor written
independently of UltraCanvas, so the test does not only check the reader
against its own idea of the format. It writes ECMA-376 agile encryption, which
is what Excel and Word 2010 and later write: AES-256-CBC, SHA-512, a spin
count of 100000, and the dataIntegrity HMAC.

    office-agile-encrypted.xlsx  a workbook: "Name" / "Total" in row 1, the
                                 text "Widget" and the number 42 in row 2, and
                                 =B2*2 in B3, then A4:A403 = 7 * the row
                                 number - stored uncompressed, so the
                                 encrypted package spans several 4096-byte
                                 segments, each with its own IV
    office-agile-encrypted.docx  a document: "Secret text", then "Paragraph 2"
                                 to "Paragraph 201" - stored uncompressed too

Both packages are kept above 4096 bytes on purpose. A smaller EncryptedPackage
stream goes into the compound file's mini stream, and msoffcrypto-tool 6.0.0
writes that case wrongly: its StreamSize field comes out as garbage and its
own integrity check rejects the file.

Both are encrypted with the password PASSWORD below - not ASCII, so the
UTF-16LE conversion the key derivation starts from is covered too. The plain
packages are written here by hand, as the smallest parts each format needs.
"""

import io
import os
import sys
import zipfile

try:
    from msoffcrypto.format.ooxml import OOXMLFile
except ImportError:
    sys.exit("msoffcrypto-tool is not installed: pip install msoffcrypto-tool")

PASSWORD = "Ültra-Pässwört 1"
HERE = os.path.dirname(os.path.abspath(__file__))

CONTENT_TYPES_XLSX = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
<Default Extension="xml" ContentType="application/xml"/>
<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>
<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>
<Override PartName="/xl/sharedStrings.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml"/>
</Types>"""

ROOT_RELS_XLSX = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>
</Relationships>"""

WORKBOOK = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">
<sheets><sheet name="Secret" sheetId="1" r:id="rId1"/></sheets>
</workbook>"""

WORKBOOK_RELS = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>
<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" Target="sharedStrings.xml"/>
</Relationships>"""

SHARED_STRINGS = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="3" uniqueCount="3">
<si><t>Name</t></si><si><t>Total</t></si><si><t>Widget</t></si>
</sst>"""

SHEET = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">
<sheetData>
<row r="1"><c r="A1" t="s"><v>0</v></c><c r="B1" t="s"><v>1</v></c></row>
<row r="2"><c r="A2" t="s"><v>2</v></c><c r="B2"><v>42</v></c></row>
<row r="3"><c r="B3"><f>B2*2</f><v>84</v></c></row>
%s
</sheetData>
</worksheet>""" % "\n".join(
    '<row r="%d"><c r="A%d"><v>%d</v></c></row>' % (r, r, r * 7) for r in range(4, 404))

CONTENT_TYPES_DOCX = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
<Default Extension="xml" ContentType="application/xml"/>
<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>
</Types>"""

ROOT_RELS_DOCX = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>
</Relationships>"""

DOCUMENT = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
<w:body><w:p><w:r><w:t>Secret text</w:t></w:r></w:p>
%s</w:body>
</w:document>""" % "\n".join(
    "<w:p><w:r><w:t>Paragraph %d</w:t></w:r></w:p>" % n for n in range(2, 202))


def package(parts, compression=zipfile.ZIP_DEFLATED):
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w", compression) as z:
        for name, text in parts:
            z.writestr(name, text)
    return buffer.getvalue()


def encrypt(plain, name):
    out = io.BytesIO()
    OOXMLFile(io.BytesIO(plain)).encrypt(PASSWORD, out)
    path = os.path.join(HERE, name)
    with open(path, "wb") as f:
        f.write(out.getvalue())
    print("wrote", path, len(out.getvalue()), "bytes")


encrypt(package([
    ("[Content_Types].xml", CONTENT_TYPES_XLSX),
    ("_rels/.rels", ROOT_RELS_XLSX),
    ("xl/workbook.xml", WORKBOOK),
    ("xl/_rels/workbook.xml.rels", WORKBOOK_RELS),
    ("xl/sharedStrings.xml", SHARED_STRINGS),
    ("xl/worksheets/sheet1.xml", SHEET),
], zipfile.ZIP_STORED), "office-agile-encrypted.xlsx")

encrypt(package([
    ("[Content_Types].xml", CONTENT_TYPES_DOCX),
    ("_rels/.rels", ROOT_RELS_DOCX),
    ("word/document.xml", DOCUMENT),
], zipfile.ZIP_STORED), "office-agile-encrypted.docx")
