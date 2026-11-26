#!/usr/bin/env python3
"""Rebuilds the PowerPoint 97-2003 (.ppt) fixtures PresentationPptTest reads.

    python3 Tests/fixtures/make-ppt-fixtures.py

    ppt-features.ppt         a deck python-pptx builds here, as LibreOffice
                             Impress saves it as "MS PowerPoint 97": a 4:3
                             slide; a title slide ("Binary Deck", subtitle)
                             coming on with a fade; "Agenda" with a bold run,
                             an italic second-level bullet, a 32 pt run, a
                             numbered pair and speaker notes, pushed on after
                             three seconds; "Shapes" with a red rectangle at
                             (1", 2") 3" x 1", a turned and mirrored star, a
                             described picture and a 2 x 2 table; a hidden
                             slide
    office-pptx-sample.ppt   office-pptx-sample.pptx (make-office-encrypted-
                             fixtures.py) the same way: five slides on 16:9 -
                             titles, bullets on two levels, a text box, notes,
                             a table, a picture, a hidden slide, sixty bullets

Both are written by producers independent of UltraCanvas: the deck by
python-pptx (pip install python-pptx pillow), the .ppt by LibreOffice
(soffice on the PATH; the checked-in files were written by LibreOffice 24.2).
LibreOffice puts a 400 KB thumbnail into the SummaryInformation stream, which
no reader of the slides needs: the compound file is written again here with
the streams the deck is made of (read with olefile, pip install olefile) and
without that one. A rebuilt file differs in its timestamps, not in what the
test checks.
"""
import io
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
KEEP = ("PowerPoint Document", "Current User", "Pictures")


def build_features_deck(path):
    from pptx import Presentation
    from pptx.dml.color import RGBColor
    from pptx.enum.shapes import MSO_SHAPE
    from pptx.oxml.ns import qn
    from pptx.util import Inches, Pt
    from lxml import etree
    from PIL import Image

    deck = Presentation()          # 10" x 7.5", PowerPoint 97's slide

    def transition(slide, xml):
        slide._element.append(etree.fromstring(
            '<p:transition xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main" ' + xml))

    title = deck.slides.add_slide(deck.slide_layouts[0])
    title.shapes.title.text = "Binary Deck"
    title.placeholders[1].text = "PowerPoint 97-2003"
    transition(title, 'spd="slow"><p:fade/></p:transition>')

    agenda = deck.slides.add_slide(deck.slide_layouts[1])
    agenda.shapes.title.text = "Agenda"
    body = agenda.placeholders[1].text_frame
    p = body.paragraphs[0]
    bold = p.add_run()
    bold.text, bold.font.bold = "First", True
    p.add_run().text = " point"
    detail = body.add_paragraph()
    detail.level = 1
    run = detail.add_run()
    run.text, run.font.italic = "Detail in italic", True
    big = body.add_paragraph()
    run = big.add_run()
    run.text, run.font.size = "Big words", Pt(32)
    for text in ("One", "Two"):
        numbered = body.add_paragraph()
        numbered.text = text
        pPr = numbered._p.get_or_add_pPr()
        pPr.append(pPr.makeelement(qn("a:buAutoNum"), {"type": "arabicPeriod"}))
    agenda.notes_slide.notes_text_frame.text = "Speaker notes here"
    transition(agenda, 'spd="fast" advTm="3000"><p:push dir="u"/></p:transition>')

    shapes = deck.slides.add_slide(deck.slide_layouts[5])
    shapes.shapes.title.text = "Shapes"
    box = shapes.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(1), Inches(2), Inches(3), Inches(1))
    box.fill.solid()
    box.fill.fore_color.rgb = RGBColor(0xFF, 0x00, 0x00)
    box.line.color.rgb = RGBColor(0x00, 0x00, 0x80)
    box.text_frame.text = "Red box"
    star = shapes.shapes.add_shape(MSO_SHAPE.STAR_5_POINT, Inches(1), Inches(4), Inches(2), Inches(2))
    star.rotation = 30
    star._element.spPr.find(qn("a:xfrm")).set("flipH", "1")
    png = io.BytesIO()
    Image.new("RGB", (8, 8), (40, 90, 160)).save(png, "PNG")
    png.seek(0)
    picture = shapes.shapes.add_picture(png, Inches(6), Inches(2), Inches(2), Inches(2))
    picture._element.nvPicPr.cNvPr.set("descr", "Blue square")
    table = shapes.shapes.add_table(2, 2, Inches(4.5), Inches(5), Inches(4), Inches(1)).table
    for (r, c), text in {(0, 0): "Name", (0, 1): "Value", (1, 0): "Alpha", (1, 1): "42"}.items():
        table.cell(r, c).text = text

    hidden = deck.slides.add_slide(deck.slide_layouts[1])
    hidden.shapes.title.text = "Hidden"
    hidden.placeholders[1].text_frame.text = "Not in the show"
    hidden._element.set("show", "0")

    deck.save(path)


# ----- A compound file, written again without the thumbnail -----
# Version 3 (512-byte sectors); streams under 4096 bytes in the mini stream.

def write_compound_file(streams, out_path):
    ENDOFCHAIN, FREESECT, FATSECT = 0xFFFFFFFE, 0xFFFFFFFF, 0xFFFFFFFD
    sectors = []          # 512-byte sectors after the header
    fat = []

    def add_chain(data, size=512):
        chain_start = len(sectors)
        count = max(1, (len(data) + size - 1) // size)
        for i in range(count):
            sectors.append(data[i * size:(i + 1) * size].ljust(size, b"\0"))
            fat.append(chain_start + i + 1 if i + 1 < count else ENDOFCHAIN)
        return chain_start

    mini, minifat, entries = b"", [], []
    for name, data in streams:
        if len(data) < 4096:
            start = len(mini) // 64
            count = max(1, (len(data) + 63) // 64)
            for i in range(count):
                minifat.append(start + i + 1 if i + 1 < count else ENDOFCHAIN)
            mini += data.ljust(count * 64, b"\0")
            entries.append((name, start, len(data)))
        else:
            entries.append((name, add_chain(data), len(data)))
    mini_start = add_chain(mini) if mini else ENDOFCHAIN
    minifat_bytes = b"".join(struct.pack("<I", v) for v in minifat)
    minifat_start = add_chain(minifat_bytes) if minifat else ENDOFCHAIN

    def entry(name, kind, start, size, left=FREESECT, right=FREESECT, child=FREESECT):
        encoded = (name + "\0").encode("utf-16-le")
        return (encoded.ljust(64, b"\0") + struct.pack("<HBB", len(encoded), kind, 1) +
                struct.pack("<III", left, right, child) + b"\0" * 16 + b"\0" * 4 + b"\0" * 16 +
                struct.pack("<IIi", start, size, 0))

    # The root's children as a tree ordered by (length, upper-case name): a
    # chain to the right, all black, which every reader accepts.
    order = sorted(range(len(entries)), key=lambda i: (len(entries[i][0]), entries[i][0].upper()))
    directory = [entry("Root Entry", 5, mini_start, len(mini), child=order[0] + 1)]
    for position, i in enumerate(order):
        name, start, size = entries[i]
        right = order[position + 1] + 1 if position + 1 < len(order) else FREESECT
        directory.append((i, entry(name, 2, start, size, right=right)))
    root = directory[0]
    rest = [e for _, e in sorted(directory[1:])]
    dir_bytes = root + b"".join(rest)
    dir_start = add_chain(dir_bytes.ljust(((len(dir_bytes) + 511) // 512) * 512, b"\0"))

    # The FAT's own sectors, marked in it.
    fat_sectors = 1
    while (len(fat) + fat_sectors) > fat_sectors * 128:
        fat_sectors += 1
    if fat_sectors > 109:
        raise SystemExit("too large for a header-only DIFAT")
    fat_first = len(sectors)
    fat += [FATSECT] * fat_sectors
    fat += [FREESECT] * (fat_sectors * 128 - len(fat))
    fat_bytes = b"".join(struct.pack("<I", v) for v in fat)
    for i in range(fat_sectors):
        sectors.append(fat_bytes[i * 512:(i + 1) * 512])

    header = (b"\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1" + b"\0" * 16 + struct.pack("<HHHHH", 0x3E, 3, 0xFFFE, 9, 6) +
              b"\0" * 6 + struct.pack("<IIIIIIIII", 0, fat_sectors, dir_start, 0, 4096, minifat_start,
                                      len(minifat_bytes) // 512 + (1 if len(minifat_bytes) % 512 else 0)
                                      if minifat else 0, ENDOFCHAIN, 0))
    difat = [fat_first + i for i in range(fat_sectors)] + [FREESECT] * (109 - fat_sectors)
    header += b"".join(struct.pack("<I", v) for v in difat)
    assert len(header) == 512
    with open(out_path, "wb") as f:
        f.write(header + b"".join(sectors))


def strip_thumbnail(ppt_path, out_path):
    import olefile
    with olefile.OleFileIO(ppt_path) as ole:
        streams = [(name, ole.openstream(name).read()) for name in KEEP if ole.exists(name)]
    write_compound_file(streams, out_path)


def main() -> int:
    soffice = shutil.which("soffice") or shutil.which("libreoffice")
    if not soffice:
        print("soffice (LibreOffice) is not on the PATH", file=sys.stderr)
        return 1
    with tempfile.TemporaryDirectory() as work:
        build_features_deck(os.path.join(work, "ppt-features.pptx"))
        shutil.copyfile(os.path.join(HERE, "office-pptx-sample.pptx"), os.path.join(work, "office-pptx-sample.pptx"))
        profile = "file://" + os.path.join(work, "profile")
        for name in ("ppt-features", "office-pptx-sample"):
            subprocess.run([soffice, "-env:UserInstallation=" + profile, "--headless", "--convert-to", "ppt",
                            "--outdir", work, os.path.join(work, name + ".pptx")],
                           check=True, stdout=subprocess.DEVNULL)
            made = os.path.join(work, name + ".ppt")
            if not os.path.exists(made):
                print("LibreOffice wrote no .ppt for " + name, file=sys.stderr)
                return 1
            strip_thumbnail(made, os.path.join(HERE, name + ".ppt"))
            print("wrote " + name + ".ppt")
    return 0


if __name__ == "__main__":
    sys.exit(main())
