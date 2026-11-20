#!/usr/bin/env python3
"""Rebuilds Tests/fixtures/pptx-features.pptx, the deck PresentationPptxTest reads.

    python3 Tests/fixtures/make-pptx-features-fixture.py

Needs python-pptx (pip install python-pptx), a writer independent of
UltraCanvas. What python-pptx has no call for is written into the slide XML
directly, as PowerPoint writes it.

    slide 1  "Text": a text box whose paragraphs carry a typeface, strike,
             superscript, capitals, a highlight and a hyperlink; a Wingdings
             bullet ("§", a square) in green at 80 %; a numbered list
             "alphaLcParenR" starting at 3; 12 pt before, 150 % line
             spacing, a 0.5" margin and a -0.25" hanging indent; insets of
             0.2"; vertical text in a second box
    slide 2  "Shapes": a five-pointed star turned 30 degrees and mirrored,
             with a two-stop linear gradient (python-pptx's 90 degrees,
             counter-clockwise, is DrawingML's 270: red at the foot, blue at
             the top) and a dashed outline; a
             straight connector with an arrowhead at its end; a freeform
             triangle (custom geometry); a bent connector
    slide 3  "Table and chart": a 3 x 3 table, header row on, the first two
             cells of row 2 merged, cell C3 filled red with a 2 pt blue
             bottom border; a clustered column chart "Sales" with two series
             over three quarters; a picture with 25 % cropped from its left
"""
import os
import struct
import sys
import zlib

from lxml import etree
from pptx import Presentation
from pptx.chart.data import CategoryChartData
from pptx.dml.color import RGBColor
from pptx.enum.chart import XL_CHART_TYPE
from pptx.enum.shapes import MSO_CONNECTOR, MSO_SHAPE
from pptx.oxml.ns import qn
from pptx.util import Inches, Pt

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "pptx-features.pptx")


def png(width, height, rgb):
    """A plain PNG of one colour, so the fixture needs no image library."""
    raw = b"".join(b"\x00" + bytes(rgb) * width for _ in range(height))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")


def rpr(run):
    return run._r.get_or_add_rPr()


def ppr(paragraph):
    return paragraph._p.get_or_add_pPr()


def sub(parent, tag, **attrs):
    element = etree.SubElement(parent, qn(tag))
    for key, value in attrs.items():
        element.set(key, str(value))
    return element


def main():
    deck = Presentation()
    title_only = deck.slide_layouts[5]

    # ----- slide 1: text -----
    slide = deck.slides.add_slide(title_only)
    slide.shapes.title.text = "Text"
    box = slide.shapes.add_textbox(Inches(0.5), Inches(1.5), Inches(6), Inches(4))
    body = box.text_frame
    body.word_wrap = True
    bodyPr = body._txBody.find(qn("a:bodyPr"))
    for side in ("lIns", "tIns", "rIns", "bIns"):
        bodyPr.set(side, str(Inches(0.2)))

    p = body.paragraphs[0]
    r = p.add_run()
    r.text = "Serif "
    r.font.name = "DejaVu Serif"
    r = p.add_run()
    r.text = "struck "
    rpr(r).set("strike", "sngStrike")
    r = p.add_run()
    r.text = "2"
    rpr(r).set("baseline", "30000")
    r = p.add_run()
    r.text = " caps "
    rpr(r).set("cap", "all")
    r = p.add_run()
    r.text = "marked "
    highlight = sub(rpr(r), "a:highlight")
    sub(highlight, "a:srgbClr", val="FFFF00")
    r = p.add_run()
    r.text = "link"
    r.hyperlink.address = "https://example.org/"

    p = body.add_paragraph()
    p.text = "Square bullet"
    pPr = ppr(p)
    pPr.set("marL", str(Inches(0.5)))
    pPr.set("indent", str(-Inches(0.25)))
    spcBef = sub(pPr, "a:spcBef")
    sub(spcBef, "a:spcPts", val="1200")
    lnSpc = etree.Element(qn("a:lnSpc"))
    sub(lnSpc, "a:spcPct", val="150000")
    pPr.insert(0, lnSpc)
    buClr = sub(pPr, "a:buClr")
    sub(buClr, "a:srgbClr", val="00B050")
    sub(pPr, "a:buSzPct", val="80000")
    sub(pPr, "a:buFont", typeface="Wingdings")
    sub(pPr, "a:buChar", char="§")

    for text in ("Third", "Fourth"):
        p = body.add_paragraph()
        p.text = text
        pPr = ppr(p)
        pPr.set("marL", str(Inches(0.5)))
        pPr.set("indent", str(-Inches(0.25)))
        sub(pPr, "a:buAutoNum", type="alphaLcParenR", startAt="3")

    vertical = slide.shapes.add_textbox(Inches(7), Inches(1.5), Inches(1), Inches(4))
    vertical.text_frame.text = "Vertical"
    vertical.text_frame._txBody.find(qn("a:bodyPr")).set("vert", "vert270")

    # ----- slide 2: shapes -----
    slide = deck.slides.add_slide(title_only)
    slide.shapes.title.text = "Shapes"
    star = slide.shapes.add_shape(MSO_SHAPE.STAR_5_POINT, Inches(0.5), Inches(1.5), Inches(3), Inches(3))
    star.rotation = 30
    star._element.spPr.find(qn("a:xfrm")).set("flipH", "1")
    star.fill.gradient()
    star.fill.gradient_angle = 90
    stops = star.fill.gradient_stops
    stops[0].color.rgb = RGBColor(0xFF, 0x00, 0x00)
    stops[0].position = 0
    stops[1].color.rgb = RGBColor(0x00, 0x00, 0xFF)
    stops[1].position = 1.0
    star.line.color.rgb = RGBColor(0x20, 0x20, 0x20)
    star.line.width = Pt(2)
    ln = star._element.spPr.find(qn("a:ln"))
    sub(ln, "a:prstDash", val="dash")

    line = slide.shapes.add_connector(MSO_CONNECTOR.STRAIGHT, Inches(4), Inches(2), Inches(7), Inches(4))
    line.line.color.rgb = RGBColor(0x00, 0x70, 0xC0)
    line.line.width = Pt(3)
    sub(line.line._get_or_add_ln(), "a:tailEnd", type="triangle")

    bent = slide.shapes.add_connector(MSO_CONNECTOR.ELBOW, Inches(4), Inches(5), Inches(8), Inches(6))
    bent.line.color.rgb = RGBColor(0x70, 0x30, 0xA0)

    # Start and points in inches (the scale): a triangle (8, 1.5), (9, 3.5), (8, 3.5).
    builder = slide.shapes.build_freeform(8, 1.5, scale=Inches(1))
    builder.add_line_segments([(9, 3.5), (8, 3.5)], close=True)
    triangle = builder.convert_to_shape()
    triangle.fill.solid()
    triangle.fill.fore_color.rgb = RGBColor(0x00, 0xB0, 0x50)

    # ----- slide 3: table, chart, picture -----
    slide = deck.slides.add_slide(title_only)
    slide.shapes.title.text = "Table and chart"
    table = slide.shapes.add_table(3, 3, Inches(0.5), Inches(1.5), Inches(4.5), Inches(1.5)).table
    for r in range(3):
        for c in range(3):
            table.cell(r, c).text = "R%dC%d" % (r + 1, c + 1)
    table.cell(1, 0).merge(table.cell(1, 1))
    corner = table.cell(2, 2)
    corner.fill.solid()
    corner.fill.fore_color.rgb = RGBColor(0xFF, 0x00, 0x00)
    tcPr = corner._tc.get_or_add_tcPr()
    lnB = etree.Element(qn("a:lnB"), w=str(Pt(2)))
    solid = sub(lnB, "a:solidFill")
    sub(solid, "a:srgbClr", val="0000FF")
    tcPr.insert(0, lnB)

    data = CategoryChartData()
    data.categories = ["Q1", "Q2", "Q3"]
    data.add_series("North", (1.5, 2.5, 3.5))
    data.add_series("South", (2.0, 1.0, 4.0))
    chart = slide.shapes.add_chart(XL_CHART_TYPE.COLUMN_CLUSTERED, Inches(5.5), Inches(1.5), Inches(4), Inches(3),
                                   data).chart
    chart.has_title = True
    chart.chart_title.text_frame.text = "Sales"
    chart.has_legend = True

    picture_path = os.path.join(HERE, "pptx-features-picture.tmp.png")
    with open(picture_path, "wb") as f:
        f.write(png(40, 20, (0x33, 0x66, 0x99)))
    try:
        picture = slide.shapes.add_picture(picture_path, Inches(0.5), Inches(4), Inches(3), Inches(2))
    finally:
        os.remove(picture_path)
    picture.crop_left = 0.25

    deck.save(OUT)
    print("wrote", OUT)


if __name__ == "__main__":
    sys.exit(main())
