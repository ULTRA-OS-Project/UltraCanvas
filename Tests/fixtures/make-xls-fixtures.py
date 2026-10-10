#!/usr/bin/env python3
"""Rebuilds the legacy Excel fixtures SpreadsheetXlsFileTest reads.

    python3 Tests/fixtures/make-xls-fixtures.py

Needs openpyxl (pip install openpyxl) and LibreOffice (`soffice` on PATH).
Two fixtures:

    xls-biff8-sample.xls   Excel 97-2003 (BIFF8, "Workbook" stream, UTF-16).
                           Written as .xlsx with openpyxl and converted by
                           LibreOffice, a real BIFF8 writer.
    xls-biff5-sample.xls   Excel 5.0/95 (BIFF5, "Book" stream, code page 1252,
                           1904 date system). LibreOffice no longer exports
                           BIFF5, so this one is written record by record below
                           (write_biff5); xlrd, an independent reader, opens it
                           with formatting_info=True when it is installed.
    xls-biff5-encrypted.xls  the same with a FILEPASS record: password-protected.

Two more fixtures are written by hand and checked in as they are, because they
are text: xls-html-export.xls (an HTML table under the .xls name) and
xls-xml2003-sample.xls (an Excel 2003 XML Spreadsheet under the name).

What the BIFF8 workbook holds is what the test checks: typed values, the number
formats that make a number a date, time, percentage or currency, formulas
(within a sheet, across sheets, through a defined name, a filled-down run
LibreOffice writes as one shared formula, and functions the engine lacks),
merged cells, column widths, row heights, hidden rows and columns, fonts,
fills, borders and alignment, a hidden sheet, and enough distinct strings that
the shared string table spills over several CONTINUE records - with non-Latin
text at the boundaries, where BIFF8 restates each string's character width.
"""

import os
import shutil
import struct
import subprocess
import sys
import tempfile
from datetime import date, time

from openpyxl import Workbook
from openpyxl.styles import Alignment, Border, Font, PatternFill, Side
from openpyxl.workbook.defined_name import DefinedName

HERE = os.path.dirname(os.path.abspath(__file__))


def build_workbook(path):
    wb = Workbook()
    data = wb.active
    data.title = "Data"

    header_font = Font(name="Arial", size=12, bold=True)
    header_fill = PatternFill(fill_type="solid", fgColor="FFFF00")
    header_border = Border(bottom=Side(style="thin", color="000000"))
    for col, title in enumerate(["Name", "Amount", "Date", "Share", "Double"], start=1):
        cell = data.cell(row=1, column=col, value=title)
        cell.font = header_font
        cell.fill = header_fill
        cell.border = header_border

    data["A2"] = "Apple"
    data["B2"] = 12.5
    data["C2"] = date(2024, 3, 15)
    data["C2"].number_format = "yyyy-mm-dd"
    data["D2"] = 0.25
    data["D2"].number_format = "0%"

    data["A3"] = "ขาย"                      # Thai
    data["B3"] = 1000
    data["C3"] = time(13, 45, 30)
    data["C3"].number_format = "hh:mm:ss"
    data["D3"] = 0.5
    data["D3"].number_format = "0.00%"

    data["A4"] = "東京 \U0001F4CA"                 # CJK and an emoji
    data["B4"] = -3.75
    data["B4"].number_format = '"$"#,##0.00'

    data["A5"] = "Total"
    data["B5"] = "=SUM(B2:B4)"
    data["C5"] = '=IF(B5>100,"big","small")'
    data["D5"] = '=A2&" pie"'

    data["A6"] = True
    data["B6"] = "=1/0"
    data["C6"] = 123456789
    data["D6"] = 1e-5

    # A run of one formula filled down: LibreOffice writes it as a shared
    # formula (SHRFMLA) referenced from each cell.
    for row in range(2, 11):
        data.cell(row=row, column=5, value=f"=B{row}*2")
    for row in range(7, 11):
        data.cell(row=row, column=2, value=row * 10)

    data["A8"] = "Merged title"
    data["A8"].alignment = Alignment(horizontal="center", vertical="center")
    data.merge_cells("A8:C8")
    data.row_dimensions[8].height = 30

    data["A9"] = "Italic red"
    data["A9"].font = Font(name="Arial", size=10, italic=True, color="FF0000")
    data["B9"].font = Font(name="Arial", size=10, strike=True)
    data["C9"] = "Underlined"
    data["C9"].font = Font(name="Arial", size=10, underline="single")
    data["A10"] = "A long sentence that wraps inside its cell"
    data["A10"].alignment = Alignment(wrap_text=True, horizontal="right")

    data.column_dimensions["A"].width = 20
    data.column_dimensions["B"].width = 12
    data["G1"] = "hidden column"
    data.column_dimensions["G"].hidden = True
    data["A12"] = "hidden row"
    data.row_dimensions[12].hidden = True

    second = wb.create_sheet("Second Sheet")
    second["A1"] = "=Data!B5"
    second["A2"] = "=SUM(Data!B2:B4)"
    second["A3"] = '=VLOOKUP("Apple",Data!A2:B4,2,FALSE())'
    second["A4"] = "=ROUND(PI(),2)"
    second["A5"] = '=LEN("hello")'
    second["A6"] = "=Rate*2"
    second["A7"] = "=-B1+3^2"
    second["A8"] = "=AVERAGE(Data!B2:B3)%"
    second["A9"] = "=ISPMT(0.1,1,3,1000)"                 # not in the engine's library
    second["B1"] = 2
    wb.defined_names["Rate"] = DefinedName("Rate", attr_text="Data!$B$2")

    # Distinct strings until the shared string table spills over several
    # CONTINUE records; every seventh is Thai, so some boundary falls inside
    # or next to a two-byte string.
    strings = wb.create_sheet("Strings")
    for i in range(400):
        text = f"String number {i:04d} of the table"
        if i % 7 == 0:
            text = f"สตริง {i:04d} ของตาราง"
        strings.cell(row=i + 1, column=1, value=text)

    hidden = wb.create_sheet("Hidden")
    hidden["A1"] = "secret"
    hidden.sheet_state = "hidden"

    wb.save(path)


# ===== BIFF5 (Excel 5.0/95), record by record =====

def rec(rtype, body):
    return struct.pack("<HH", rtype, len(body)) + body


def biff5_string(text, lenlen=1):
    raw = text.encode("cp1252")
    return struct.pack("<B" if lenlen == 1 else "<H", len(raw)) + raw


def biff5_font(height_twips, bold=False, italic=False, colour=0x7FFF, name="Arial",
               underline=0):
    grbit = (0x02 if italic else 0)
    return rec(0x0031, struct.pack("<HHHHHBBBB", height_twips, grbit, colour,
                                   700 if bold else 400, 0, underline, 0, 0, 0)
               + biff5_string(name))


def biff5_xf(font, fmt, style=False, halign=0, wrap=False, valign=2,
             fill=0, fore=64, back=65, bottom=0, bottom_colour=64):
    type_par = 0x0001 | (0xFFF4 if style else 0)  # locked; a style XF has no parent
    align = halign | (0x08 if wrap else 0) | (valign << 4)
    used = 0 if style else 0xFC                     # cell XF: all attribute groups set
    brdbkg1 = fore | (back << 7) | (fill << 16) | (bottom << 22) | (bottom_colour << 25)
    brdbkg2 = (64 << 9) | (64 << 16) | (64 << 23)  # top/left/right: none, automatic
    return rec(0x00E0, struct.pack("<HHHBBII", font, fmt, type_par, align, used,
                                   brdbkg1, brdbkg2))


def rk_int(value):
    return ((value << 2) | 0x02) & 0xFFFFFFFF      # a 30-bit two's complement integer


def rk_x100(value_times_100):
    return ((value_times_100 << 2) | 0x03) & 0xFFFFFFFF


def cell_head(row, col, xf):
    return struct.pack("<HHH", row, col, xf)


def biff5_formula(row, col, xf, cached, tokens):
    """cached: float, or ("str", text) / ("bool", b) / ("err", code)."""
    if isinstance(cached, tuple):
        kind, val = cached
        code = {"str": 0, "bool": 1, "err": 2}[kind]
        value = struct.pack("<BBBBBBH", code, 0, int(val) if kind != "str" else 0, 0, 0, 0, 0xFFFF)
    else:
        value = struct.pack("<d", cached)
    body = cell_head(row, col, xf) + value + struct.pack("<HI", 0, 0)
    body += struct.pack("<H", len(tokens)) + tokens
    out = rec(0x0006, body)
    if isinstance(cached, tuple) and cached[0] == "str":
        out += rec(0x0207, biff5_string(cached[1], lenlen=2))
    return out


def ref5(row, col, row_rel=True, col_rel=True):
    return struct.pack("<HB", row | (0x4000 if col_rel else 0) | (0x8000 if row_rel else 0), col)


def area5(r1, r2, c1, c2):
    return struct.pack("<HHBB", r1 | 0xC000, r2 | 0xC000, c1, c2)


def sheet5(cells_records, dims, extra=b""):
    body = rec(0x0809, struct.pack("<HHHH", 0x0500, 0x0010, 0x0DBB, 0x07CC))
    body += rec(0x0055, struct.pack("<H", 8))
    body += extra
    body += rec(0x0200, struct.pack("<HHHHH", *dims, 0))
    body += cells_records
    body += rec(0x000A, b"")
    return body


def write_biff5(path, encrypted=False):
    # Globals, with the two BOUNDSHEET offsets patched once the layout is known.
    fonts = (biff5_font(200) + biff5_font(200, bold=True) + biff5_font(200, italic=True)
             + biff5_font(200)                       # index 3; index 4 is never used
             + biff5_font(280, bold=True, colour=10, name="Times New Roman", underline=1))
    formats = rec(0x041E, struct.pack("<H", 164) + biff5_string("dd.mm.yyyy"))
    xfs = b"".join(biff5_xf(0, 0, style=True) for _ in range(15))
    xfs += biff5_xf(0, 0)                                    # 15 default cell
    xfs += biff5_xf(1, 0)                                    # 16 bold
    xfs += biff5_xf(0, 14)                                   # 17 built-in date
    xfs += biff5_xf(0, 10)                                   # 18 0.00%
    xfs += biff5_xf(0, 164)                                  # 19 custom date
    xfs += biff5_xf(5, 0, halign=2, wrap=True, fill=1, fore=13, bottom=1, bottom_colour=8)  # 20

    def globals_with(offsets):
        g = rec(0x0809, struct.pack("<HHHH", 0x0500, 0x0005, 0x0DBB, 0x07CC))
        if encrypted:
            # FILEPASS: XOR obfuscation (key, hash). The reader must refuse the
            # workbook rather than read scrambled records.
            g += rec(0x002F, struct.pack("<HH", 0x1234, 0x5678))
        g += rec(0x0042, struct.pack("<H", 1252))
        g += rec(0x0022, struct.pack("<H", 1))              # 1904 date system
        g += fonts + formats + xfs
        g += rec(0x0085, struct.pack("<IBB", offsets[0], 0, 0) + biff5_string("Données"))
        g += rec(0x0085, struct.pack("<IBB", offsets[1], 0, 0) + biff5_string("Strings"))
        g += rec(0x000A, b"")
        return g

    cells = b""
    cells += rec(0x0204, cell_head(0, 0, 16) + biff5_string("Café crème", lenlen=2))
    cells += rec(0x0203, cell_head(0, 1, 15) + struct.pack("<d", 3.14159))
    cells += rec(0x027E, cell_head(1, 1, 15) + struct.pack("<I", rk_int(42)))
    cells += rec(0x027E, cell_head(2, 1, 15) + struct.pack("<I", rk_x100(1234)))
    cells += rec(0x00BD, struct.pack("<HH", 3, 1) + struct.pack("<HI", 15, rk_int(-7))
                 + struct.pack("<HI", 18, rk_x100(25)) + struct.pack("<H", 2))
    cells += rec(0x0205, cell_head(4, 0, 15) + struct.pack("<BB", 1, 0))   # TRUE
    cells += rec(0x0205, cell_head(4, 1, 15) + struct.pack("<BB", 0x2A, 1))  # #N/A
    # 2024-03-15 in the 1904 system: 45366 - 1462.
    cells += rec(0x0203, cell_head(5, 0, 17) + struct.pack("<d", 43904.0))
    cells += rec(0x0203, cell_head(5, 1, 19) + struct.pack("<d", 43904.0))
    cells += rec(0x0204, cell_head(6, 0, 20) + biff5_string("Styled", lenlen=2))
    # =SUM(B1:B3)  ->  PtgArea(value class), PtgFuncVar(argc 1, SUM)
    cells += biff5_formula(7, 1, 15, 3.14159 + 42 + 12.34,
                           b"\x45" + area5(0, 2, 1, 1) + b"\x42\x01\x04\x00")
    # =A1&"!"  ->  PtgRef, PtgStr, PtgConcat; the text result follows in STRING
    cells += biff5_formula(8, 1, 15, ("str", "Café crème!"),
                           b"\x44" + ref5(0, 0) + b"\x17" + biff5_string("!") + b"\x08")
    # =IF(B2>40,"big","small")  ->  tAttrIf / tAttrGoto jumps around the branches
    if_tokens = (b"\x44" + ref5(1, 1) + b"\x1E" + struct.pack("<H", 40) + b"\x0D"
                 + b"\x19\x02" + struct.pack("<H", 9)
                 + b"\x17" + biff5_string("big")
                 + b"\x19\x08" + struct.pack("<H", 10)
                 + b"\x17" + biff5_string("small")
                 + b"\x19\x08" + struct.pack("<H", 3)
                 + b"\x42\x03\x01\x00")
    cells += biff5_formula(9, 1, 15, ("str", "big"), if_tokens)
    # =ROUND($B$1*2,1)  ->  absolute PtgRef, PtgInt, PtgMul, PtgInt, PtgFunc ROUND
    cells += biff5_formula(10, 1, 15, 6.3,
                           b"\x44" + ref5(0, 1, False, False) + b"\x1E\x02\x00\x05"
                           + b"\x1E\x01\x00" + b"\x41\x1B\x00")
    # =B2>40  ->  a Boolean result
    cells += biff5_formula(11, 1, 15, ("bool", 1),
                           b"\x44" + ref5(1, 1) + b"\x1E" + struct.pack("<H", 40) + b"\x0D")

    colinfo = rec(0x007D, struct.pack("<HHHHHH", 0, 0, 20 * 256, 15, 0x0002, 0))
    colinfo += rec(0x007D, struct.pack("<HHHHHH", 3, 3, 10 * 256, 15, 0x0001, 0))
    rows = rec(0x0208, struct.pack("<HHHHHHHH", 0, 0, 2, 400 | 0, 0, 0, 0x0140, 15))
    rows += rec(0x0208, struct.pack("<HHHHHHHH", 12, 0, 1, 255, 0, 0, 0x0120, 15))
    cells += rec(0x0204, cell_head(12, 0, 15) + biff5_string("hidden row", lenlen=2))
    sheet1 = sheet5(cells, (0, 13, 0, 2), colinfo + rows)

    strings = b"".join(rec(0x0204, cell_head(i, 0, 15)
                           + biff5_string(f"Latin-1 line {i:03d} à la carte", lenlen=2))
                       for i in range(200))
    sheet2 = sheet5(strings, (0, 200, 0, 1))

    g = globals_with((0, 0))
    book = globals_with((len(g), len(g) + len(sheet1))) + sheet1 + sheet2
    write_compound_file(path, "Book", book)


def write_compound_file(path, stream_name, stream):
    """A version 3 compound file holding one root stream (>= 4096 bytes, so it
    lives in regular sectors and no mini stream is needed)."""
    assert len(stream) >= 4096
    sector = 512
    padded = stream + b"\0" * (-len(stream) % sector)
    n_stream = len(padded) // sector
    dir_sector = n_stream
    n_fat = 1
    while (n_stream + 1 + n_fat) > n_fat * (sector // 4):
        n_fat += 1
    fat_first = dir_sector + 1
    fat = []
    for i in range(n_stream):
        fat.append(i + 1 if i + 1 < n_stream else 0xFFFFFFFE)
    fat.append(0xFFFFFFFE)                            # the directory
    fat += [0xFFFFFFFD] * n_fat                      # the FAT itself
    fat += [0xFFFFFFFF] * (n_fat * (sector // 4) - len(fat))

    def dir_entry(name, kind, child, start, size):
        raw = name.encode("utf-16-le") + b"\0\0" if name else b""
        return (raw.ljust(64, b"\0") + struct.pack("<HBB", len(raw), kind, 1)
                + struct.pack("<III", 0xFFFFFFFF, 0xFFFFFFFF, child)
                + b"\0" * 16 + struct.pack("<I", 0) + b"\0" * 16
                + struct.pack("<III", start, size, 0))

    directory = (dir_entry("Root Entry", 5, 1, 0xFFFFFFFE, 0)
                 + dir_entry(stream_name, 2, 0xFFFFFFFF, 0, len(stream))
                 + dir_entry("", 0, 0xFFFFFFFF, 0, 0) * 2)
    difat = [fat_first + i for i in range(n_fat)] + [0xFFFFFFFF] * (109 - n_fat)
    header = (b"\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1" + b"\0" * 16
              + struct.pack("<HHHHH", 0x003E, 0x0003, 0xFFFE, 9, 6) + b"\0" * 6
              + struct.pack("<IIIIIIIII", 0, n_fat, dir_sector, 0, 4096,
                            0xFFFFFFFE, 0, 0xFFFFFFFE, 0)
              + struct.pack("<109I", *difat))
    assert len(header) == 512
    with open(path, "wb") as f:
        f.write(header + padded + directory + struct.pack(f"<{len(fat)}I", *fat))


def check_with_xlrd(path):
    try:
        import xlrd
    except ImportError:
        print("  (xlrd not installed - skipped the independent check of", path, ")")
        return
    book = xlrd.open_workbook(path, formatting_info=True)
    assert book.biff_version in (50, 70) and book.datemode == 1   # Excel 95 is "BIFF7" to xlrd
    sheet = book.sheet_by_index(0)
    assert sheet.name == "Données"
    assert sheet.cell_value(0, 0) == "Café crème"
    assert sheet.cell_value(2, 1) == 12.34 and sheet.cell_value(3, 2) == 0.25
    assert sheet.cell_value(8, 1) == "Café crème!"
    assert book.sheet_by_index(1).nrows == 200
    print("  xlrd reads", os.path.basename(path), "as intended")


def convert(soffice, source, filter_name, target):
    outdir = tempfile.mkdtemp()
    try:
        subprocess.run([soffice, "--headless", "--convert-to", f"xls:{filter_name}",
                        "--outdir", outdir, source],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        produced = os.path.join(outdir, os.path.splitext(os.path.basename(source))[0] + ".xls")
        shutil.move(produced, target)
    finally:
        shutil.rmtree(outdir, ignore_errors=True)


def main():
    soffice = shutil.which("soffice") or shutil.which("libreoffice")
    if not soffice:
        sys.exit("LibreOffice (soffice) is needed to write the binary Excel formats")
    work = tempfile.mkdtemp()
    try:
        source = os.path.join(work, "xls-sample.xlsx")
        build_workbook(source)
        convert(soffice, source, "MS Excel 97", os.path.join(HERE, "xls-biff8-sample.xls"))
    finally:
        shutil.rmtree(work, ignore_errors=True)
    biff5 = os.path.join(HERE, "xls-biff5-sample.xls")
    write_biff5(biff5)
    check_with_xlrd(biff5)
    write_biff5(os.path.join(HERE, "xls-biff5-encrypted.xls"), encrypted=True)
    print("wrote xls-biff8-sample.xls, xls-biff5-sample.xls and xls-biff5-encrypted.xls")


if __name__ == "__main__":
    main()
