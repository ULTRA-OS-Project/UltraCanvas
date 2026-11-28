// Tests/PresentationPptxTest.cpp
// What the PowerPoint reader and the slide renderer make of a deck's look:
//   * Tests/fixtures/pptx-features.pptx (python-pptx, see
//     make-pptx-features-fixture.py): typefaces (the theme's too), strike,
//     superscript, capitals, highlight, a hyperlink, a Wingdings bullet with
//     its colour and size, an alphabetic numbering starting at 3, spacing,
//     margins and hanging indents, insets, vertical text; a star turned,
//     mirrored, filled with a gradient and outlined in dashes; connectors
//     with an arrowhead and a bend; a freeform outline; a table with a merged
//     cell, a cell fill and border and its style's header row; a chart read
//     through UltraCanvasDrawingML; a cropped picture;
//   * a deck rebuilt in memory with a SmartArt diagram, read from the drawing
//     PowerPoint saves beside it, a hyperlink to another slide, and a table
//     style from ppt/tableStyles.xml;
//   * PptxNumberLabel's schemes;
//   * each slide drawn (RenderPptxSlide) and looked at where the shapes are;
//   * the writer: the fixture, the python-pptx sample and a protected copy
//     written and read back, field by field and drawn side by side; the
//     package kinds; a document laid out as a deck and a deck's outline read
//     back as its slides; UltraCanvasFileLoader saving a document as .pptx;
//   * the OpenDocument writer: the same decks as .odp, .fodp and .otp, read
//     back by the .odp reader with their look and drawn as the originals; a
//     document saved as .odp; a password refused;
//   * the .odp reader's look: Tests/fixtures/pptx-features.odp, LibreOffice's
//     conversion of the fixture (make-odp-fixtures.py), read field by field
//     against the .pptx reader and drawn as the .pptx is.
// UC_PPTX_TEST_DUMP=<dir> writes the drawn slides there as .ppm, and the
// written decks as .pptx, to look at (or open in PowerPoint / LibreOffice).
// Version: 1.3.0 - the .odp reader's look: LibreOffice's pptx-features.odp against the .pptx; the
//                  writer's .odp read back with its look and drawn as the original
// Version: 1.2.0 - the OpenDocument writer: .odp, .fodp and .otp read back, a document saved as .odp
// Version: 1.1.0 - the writer: round trips, a password, the package kinds, a document as a deck
// Version: 1.0.0
// Last Modified: 2026-11-22
// Author: UltraCanvas Framework

#include "UltraCanvasPptxSlideRender.h"
#include "UltraCanvasPresentationPptx.h"
#include "UltraCanvasPresentationOdp.h"
#include "UltraCanvasZipPackage.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasOfficeEncryption.h"
#include "UltraCanvasPathUtf8.h"

#include <filesystem>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

bool Contains(const std::string& haystack, const std::string& needle) { return haystack.find(needle) != std::string::npos; }

std::string Fixture(const char* name) { return std::string(PPTXTEST_FIXTURE_DIR) + "/" + name; }

const PptxShape* Named(const PptxSlide& slide, const std::string& name) {
    for (const PptxShape& shape : slide.shapes)
        if (shape.name == name) return &shape;
    return nullptr;
}

const PptxShape* OfKind(const PptxSlide& slide, PptxShapeKind kind) {
    for (const PptxShape& shape : slide.shapes)
        if (shape.kind == kind) return &shape;
    return nullptr;
}

struct Rgb {
    int r = 0, g = 0, b = 0;
};

Rgb PixelAt(const UCPixmap& pixmap, int x, int y) {
    const uint32_t argb = pixmap.GetPixel(x, y);
    return Rgb{static_cast<int>((argb >> 16) & 0xFF), static_cast<int>((argb >> 8) & 0xFF), static_cast<int>(argb & 0xFF)};
}

bool Near(const Rgb& c, int r, int g, int b, int tolerance = 40) {
    return std::abs(c.r - r) <= tolerance && std::abs(c.g - g) <= tolerance && std::abs(c.b - b) <= tolerance;
}

// How many pixels in the rectangle are not the background (white).
int Inked(const UCPixmap& pixmap, int x0, int y0, int x1, int y1) {
    int count = 0;
    for (int y = std::max(0, y0); y < std::min(y1, pixmap.GetRawHeight()); ++y)
        for (int x = std::max(0, x0); x < std::min(x1, pixmap.GetRawWidth()); ++x) {
            const Rgb c = PixelAt(pixmap, x, y);
            if (c.r < 235 || c.g < 235 || c.b < 235) ++count;
        }
    return count;
}

// Counts pixels close to a colour in the rectangle.
int Coloured(const UCPixmap& pixmap, int x0, int y0, int x1, int y1, int r, int g, int b, int tolerance = 50) {
    int count = 0;
    for (int y = std::max(0, y0); y < std::min(y1, pixmap.GetRawHeight()); ++y)
        for (int x = std::max(0, x0); x < std::min(x1, pixmap.GetRawWidth()); ++x)
            if (Near(PixelAt(pixmap, x, y), r, g, b, tolerance)) ++count;
    return count;
}

void Dump(const UCPixmap& pixmap, const std::string& name) {
    const char* dir = std::getenv("UC_PPTX_TEST_DUMP");
    if (!dir || !*dir) return;
    std::ofstream out(std::string(dir) + "/" + name + ".ppm", std::ios::binary);
    out << "P6\n" << pixmap.GetRawWidth() << " " << pixmap.GetRawHeight() << "\n255\n";
    for (int y = 0; y < pixmap.GetRawHeight(); ++y)
        for (int x = 0; x < pixmap.GetRawWidth(); ++x) {
            const Rgb c = PixelAt(pixmap, x, y);
            out.put(static_cast<char>(c.r)).put(static_cast<char>(c.g)).put(static_cast<char>(c.b));
        }
}

void TestNumberLabels() {
    std::cout << "\n=== Numbering schemes ===\n";
    Check(PptxNumberLabel("arabicPeriod", 3) == "3." && PptxNumberLabel("arabicParenR", 2) == "2)" &&
              PptxNumberLabel("arabicParenBoth", 1) == "(1)" && PptxNumberLabel("arabicPlain", 7) == "7",
          "arabic: period, parenthesis, both, plain");
    Check(PptxNumberLabel("alphaLcParenR", 3) == "c)" && PptxNumberLabel("alphaUcPeriod", 28) == "BB.",
          "letters, and past z doubled as PowerPoint counts");
    Check(PptxNumberLabel("romanLcPeriod", 4) == "iv." && PptxNumberLabel("romanUcParenBoth", 1999) == "(MCMXCIX)",
          "roman numerals");
    Check(PptxNumberLabel("thaiAlphaPeriod", 2) == "2." && PptxNumberLabel("", 0) == "1.",
          "an unknown scheme, and a number below one, as PowerPoint's plain arabic");
}

void TestFixture() {
    std::cout << "\n=== pptx-features.pptx ===\n";
    PptxPresentation deck;
    std::string error;
    const bool ok = ReadPptxPresentation(Fixture("pptx-features.pptx"), deck, error, "", true);
    Check(ok && deck.slides.size() == 3, "the deck reads, three slides");
    if (!ok || deck.slides.size() != 3) {
        std::cout << "         " << error << "\n";
        return;
    }
    Check(deck.majorFont == "Calibri" && deck.minorFont == "Calibri", "the theme's heading and body fonts");

    // ----- slide 1: text -----
    const PptxSlide& text = deck.slides[0];
    const PptxShape* box = Named(text, "TextBox 2");
    Check(box && box->paragraphs.size() == 4, "the text box and its four paragraphs");
    if (box && box->paragraphs.size() == 4) {
        const auto& runs = box->paragraphs[0].runs;
        Check(runs.size() == 6, "six runs in the first paragraph");
        if (runs.size() == 6) {
            Check(runs[0].font == "DejaVu Serif" && runs[1].font == "Calibri",
                  "a run's typeface; the others the theme's body font (+mn-lt)");
            Check(runs[1].strike && !runs[0].strike, "strike");
            Check(runs[2].baseline == 30, "superscript: 30 % up");
            Check(runs[3].allCaps, "capitals");
            Check(runs[4].highlight == "FFFF00", "a highlight");
            Check(runs[5].hyperlink == "https://example.org/", "a hyperlink, from the slide's external relationship");
        }
        const PptxParagraph& square = box->paragraphs[1];
        Check(square.bullet && !square.numbered && square.bulletChar == "\xC2\xA7" && square.bulletFont == "Wingdings" &&
                  square.bulletColor == "00B050" && std::abs(square.bulletSizePercent - 80.0f) < 0.01f,
              "a Wingdings bullet, its colour and size");
        Check(square.marginLeft == 457200 && square.indent == -228600, "the margin and the hanging indent");
        Check(std::abs(square.spaceBefore.points - 12.0f) < 0.01f && std::abs(square.lineSpacing.percent - 1.5f) < 0.001f,
              "12 pt before, 150 % line spacing");
        const PptxParagraph& third = box->paragraphs[2];
        Check(third.numbered && third.numberScheme == "alphaLcParenR" && third.numberStartAt == 3,
              "numbered alphaLcParenR from 3");
        Check(box->insetLeft == 182880 && box->insetBottom == 182880, "the box's insets");
    }
    const PptxShape* vertical = Named(text, "TextBox 3");
    Check(vertical && vertical->textRotation == 270 && !vertical->wrap, "vertical text, one line");
    const UCRichDocument outline = PptxPresentationToRichDocument(deck);
    bool linked = false, struck = false, raised = false;
    for (const RichDocBlock& block : outline.blocks)
        for (const RichTextRun& run : block.runs) {
            linked = linked || run.linkTarget == "https://example.org/";
            struck = struck || (run.strikethrough && run.text == "struck ");
            raised = raised || (run.superscript && run.text == "2");
        }
    Check(linked && struck && raised, "the outline keeps the link, the strike and the superscript");

    // ----- slide 2: shapes -----
    const PptxSlide& shapes = deck.slides[1];
    const PptxShape* star = nullptr;
    const PptxShape* arrow = nullptr;
    const PptxShape* bent = nullptr;
    const PptxShape* freeform = nullptr;
    for (const PptxShape& shape : shapes.shapes) {
        if (shape.geometry == "star5") star = &shape;
        if (shape.kind == PptxShapeKind::Connector && shape.geometry == "line") arrow = &shape;
        if (shape.kind == PptxShapeKind::Connector && shape.geometry.rfind("bentConnector", 0) == 0) bent = &shape;
        if (!shape.customPath.empty()) freeform = &shape;
    }
    Check(star && std::abs(star->rotationDeg - 30.0) < 0.01 && star->flipH && !star->flipV, "a star turned 30 degrees, mirrored");
    Check(star && star->gradientStops.size() == 2 && star->gradientStops[0].color == "FF0000" &&
              star->gradientStops[1].color == "0000FF" && std::abs(star->gradientAngleDeg - 270.0) < 0.01 &&
              star->fill == "FF0000",
          "its gradient: red to blue, bottom to top (270 degrees)");
    Check(star && star->dash == RichShape::Dash::Dashed && star->line == "202020" && std::abs(star->lineWidthPt - 2.0) < 0.01,
          "its dashed 2 pt outline");
    Check(arrow && arrow->kind == PptxShapeKind::Connector && arrow->arrowEnd && !arrow->arrowStart && arrow->line == "0070C0" && std::abs(arrow->lineWidthPt - 3.0) < 0.01,
          "a straight connector (prst line) with an arrowhead at its end");
    Check(bent != nullptr, "a bent connector");
    Check(freeform && freeform->customPath.size() >= 4 && freeform->fill == "00B050", "a freeform outline (custom geometry)");

    // ----- slide 3: table, chart, picture -----
    const PptxSlide& third = deck.slides[2];
    const PptxShape* table = OfKind(third, PptxShapeKind::Table);
    Check(table && table->rows.size() == 3 && table->cellStyles.size() == 3, "the table, with a look for each cell");
    if (table && table->cellStyles.size() == 3 && table->cellStyles[1].size() == 3 && table->cellStyles[2].size() == 3) {
        Check(table->cellStyles[1][0].gridSpan == 2 && table->cellStyles[1][1].mergedAway, "row 2: two cells merged");
        Check(table->cellStyles[2][2].fill == "FF0000" && table->cellStyles[2][2].bottom.color == "0000FF" &&
                  std::abs(table->cellStyles[2][2].bottom.widthPt - 2.0) < 0.01,
              "C3: red, a 2 pt blue border below");
        Check(table->cellStyles[0][0].bold && !table->cellStyles[0][0].fill.empty() &&
                  table->cellStyles[0][0].fill != table->cellStyles[1][0].fill,
              "the style's header row: its own fill, bold");
    }
    const PptxShape* chart = OfKind(third, PptxShapeKind::Chart);
    Check(chart && chart->chart.kind == RichChart::Kind::Column && chart->chart.title == "Sales" &&
              chart->chart.series.size() == 2 && chart->chart.categories.size() == 3 && chart->chart.series[1].name == "South" &&
              chart->chart.series[1].values.size() == 3 && chart->chart.series[1].values[2] == 4.0,
          "the chart: columns, its title, two series over three quarters");
    const PptxShape* picture = OfKind(third, PptxShapeKind::Picture);
    Check(picture && std::abs(picture->cropLeft - 0.25) < 0.001 && picture->cropRight == 0 &&
              deck.pictures.count(picture->pictureTarget) == 1,
          "the picture, 25 % cropped from its left, its bytes kept");
    Check(Contains(outline.ToPlainText(), "[Chart: Sales]"), "the outline names the chart");

    // ----- drawn -----
    // 1000 pixels for 10 inches: 100 pixels an inch.
    for (size_t i = 0; i < deck.slides.size(); ++i) {
        const auto pixmap = RenderPptxSlide(deck, i, 1000);
        Check(pixmap && pixmap->GetRawWidth() == 1000 && pixmap->GetRawHeight() == 750, "slide " + std::to_string(i + 1) + " drawn");
        if (pixmap) Dump(*pixmap, "pptx-features-" + std::to_string(i + 1));
    }
    const auto drawnText = RenderPptxSlide(deck, 0, 1000);
    if (drawnText) {
        // The square bullet in green: 0.5" in, its paragraph below the first.
        Check(Coloured(*drawnText, 60, 150, 120, 450, 0x00, 0xB0, 0x50) > 5, "the bullet drawn in its green");
        Check(Coloured(*drawnText, 50, 150, 650, 450, 0xFF, 0xFF, 0x00, 30) > 20, "the highlight drawn");
        Check(Inked(*drawnText, 700, 150, 800, 550) > 50, "the vertical text drawn in its box");
    }
    const auto drawnShapes = RenderPptxSlide(deck, 1, 1000);
    if (drawnShapes) {
        // The star's gradient turns with it: red toward its lower left, blue
        // toward its upper right.
        Check(Coloured(*drawnShapes, 60, 330, 220, 460, 0xFF, 0x00, 0x00, 90) > 50, "the star's red end");
        Check(Coloured(*drawnShapes, 200, 170, 340, 280, 0x00, 0x00, 0xFF, 90) > 50, "the star's blue end");
        // The connector runs from (4", 2") to (7", 4"); its arrowhead at the end.
        Check(Coloured(*drawnShapes, 540, 290, 560, 310, 0x00, 0x70, 0xC0) > 5, "the connector drawn along its line");
        Check(Coloured(*drawnShapes, 680, 385, 702, 402, 0x00, 0x70, 0xC0) > 30, "its arrowhead at the end");
        Check(Coloured(*drawnShapes, 830, 300, 870, 340, 0x00, 0xB0, 0x50) > 100, "the freeform triangle filled");
    }
    const auto drawnTable = RenderPptxSlide(deck, 2, 1000);
    if (drawnTable) {
        // C3 spans 3.5" .. 5" x 2.5" .. 3": red.
        Check(Coloured(*drawnTable, 360, 260, 490, 290, 0xFF, 0x00, 0x00) > 500, "the red cell");
        // The chart: 5.5" .. 9.5" x 1.5" .. 4.5", drawn (not left white).
        Check(Inked(*drawnTable, 560, 160, 940, 440) > 2000, "the chart drawn");
        // The cropped picture (one colour) fills its box: 0.5" .. 3.5" x 4" .. 6".
        Check(Coloured(*drawnTable, 60, 410, 340, 590, 0x33, 0x66, 0x99, 20) > 40000, "the cropped picture fills its box");
    }
}

// A deck rebuilt in memory: SmartArt (its data model naming its drawing), a
// hyperlink to another slide, and a table style the package defines.
void TestRebuilt() {
    std::cout << "\n=== SmartArt, a link to a slide, a table style ===\n";
    UCZipPackageReader source;
    if (!source.Open(Fixture("pptx-features.pptx"))) {
        Check(false, "the fixture opens");
        return;
    }
    const std::string ns =
        "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
        "xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\"";
    const std::string frame =
        "<p:graphicFrame><p:nvGraphicFramePr><p:cNvPr id=\"40\" name=\"Diagram 39\"/><p:cNvGraphicFramePr/><p:nvPr/>"
        "</p:nvGraphicFramePr><p:xfrm><a:off x=\"914400\" y=\"914400\"/><a:ext cx=\"3657600\" cy=\"914400\"/></p:xfrm>"
        "<a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/diagram\">"
        "<dgm:relIds xmlns:dgm=\"http://schemas.openxmlformats.org/drawingml/2006/diagram\" r:dm=\"rId90\" r:lo=\"rId91\" "
        "r:qs=\"rId92\" r:cs=\"rId93\"/></a:graphicData></a:graphic></p:graphicFrame>"
        "<p:sp><p:nvSpPr><p:cNvPr id=\"41\" name=\"Jump\"/><p:cNvSpPr txBox=\"1\"/><p:nvPr/></p:nvSpPr><p:spPr>"
        "<a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"914400\" cy=\"457200\"/></a:xfrm></p:spPr><p:txBody><a:bodyPr/>"
        "<a:p><a:r><a:rPr><a:hlinkClick r:id=\"rId94\"/></a:rPr><a:t>Go to the shapes</a:t></a:r></a:p></p:txBody></p:sp>";
    const std::string model =
        "<dgm:dataModel xmlns:dgm=\"http://schemas.openxmlformats.org/drawingml/2006/diagram\" " + ns + "><dgm:ptLst/>"
        "<dgm:extLst><a:ext uri=\"http://schemas.microsoft.com/office/drawing/2008/diagram\">"
        "<dsp:dataModelExt xmlns:dsp=\"http://schemas.microsoft.com/office/drawing/2008/diagram\" relId=\"rId95\"/>"
        "</a:ext></dgm:extLst></dgm:dataModel>";
    auto box = [](int id, const char* text, int x, const char* colour) {
        return "<dsp:sp modelId=\"{" + std::to_string(id) + "}\"><dsp:nvSpPr><dsp:cNvPr id=\"0\" name=\"\"/><dsp:cNvSpPr/>"
               "</dsp:nvSpPr><dsp:spPr><a:xfrm><a:off x=\"" + std::to_string(x) + "\" y=\"0\"/><a:ext cx=\"1097280\" "
               "cy=\"914400\"/></a:xfrm><a:prstGeom prst=\"roundRect\"><a:avLst/></a:prstGeom><a:solidFill><a:srgbClr val=\"" +
               colour + "\"/></a:solidFill></dsp:spPr><dsp:txBody><a:bodyPr/><a:p><a:r><a:t>" + text +
               "</a:t></a:r></a:p></dsp:txBody></dsp:sp>";
    };
    const std::string drawing =
        "<dsp:drawing xmlns:dsp=\"http://schemas.microsoft.com/office/drawing/2008/diagram\" " + ns + "><dsp:spTree>"
        "<dsp:nvGrpSpPr><dsp:cNvPr id=\"0\" name=\"\"/><dsp:cNvGrpSpPr/></dsp:nvGrpSpPr><dsp:grpSpPr/>" +
        box(1, "Plan", 0, "C00000") + box(2, "Build", 1280160, "00B050") + box(3, "Ship", 2560320, "0070C0") +
        "</dsp:spTree></dsp:drawing>";
    const std::string rels =
        "<Relationship Id=\"rId90\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/diagramData\" "
        "Target=\"../diagrams/data1.xml\"/>"
        "<Relationship Id=\"rId94\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slide\" "
        "Target=\"slide2.xml\"/>"
        "<Relationship Id=\"rId95\" Type=\"http://schemas.microsoft.com/office/2007/relationships/diagramDrawing\" "
        "Target=\"../diagrams/drawing1.xml\"/>";
    const std::string styles =
        "<a:tblStyleLst " + ns + " def=\"{AAAA0000-0000-0000-0000-000000000001}\">"
        "<a:tblStyle styleId=\"{AAAA0000-0000-0000-0000-000000000001}\" styleName=\"Green\">"
        "<a:wholeTbl><a:tcTxStyle><a:srgbClr val=\"003300\"/></a:tcTxStyle><a:tcStyle><a:tcBdr/><a:fill><a:solidFill>"
        "<a:srgbClr val=\"E2F0D9\"/></a:solidFill></a:fill></a:tcStyle></a:wholeTbl>"
        "<a:firstRow><a:tcTxStyle b=\"on\"><a:srgbClr val=\"FFFFFF\"/></a:tcTxStyle><a:tcStyle><a:tcBdr/><a:fill>"
        "<a:solidFill><a:srgbClr val=\"385723\"/></a:solidFill></a:fill></a:tcStyle></a:firstRow>"
        "</a:tblStyle></a:tblStyleLst>";

    UCZipPackageWriter rebuilt;
    rebuilt.OpenInMemory();
    for (const std::string& name : source.EntryNames()) {
        std::string data;
        if (!source.ReadEntry(name, data)) continue;
        if (name == "ppt/slides/slide1.xml") {
            const size_t end = data.rfind("</p:spTree>");
            if (end != std::string::npos) data.insert(end, frame);
        } else if (name == "ppt/slides/_rels/slide1.xml.rels") {
            const size_t end = data.rfind("</Relationships>");
            if (end != std::string::npos) data.insert(end, rels);
        } else if (name == "ppt/slides/slide3.xml") {
            // The table names the package's own style.
            const size_t id = data.find("<a:tableStyleId>");
            const size_t idEnd = data.find("</a:tableStyleId>", id);
            if (id != std::string::npos && idEnd != std::string::npos)
                data.replace(id + 16, idEnd - id - 16, "{AAAA0000-0000-0000-0000-000000000001}");
        } else if (name == "ppt/tableStyles.xml") {
            data = styles;
        }
        rebuilt.AddEntry(name, data);
    }
    rebuilt.AddEntry("ppt/diagrams/data1.xml", model);
    rebuilt.AddEntry("ppt/diagrams/drawing1.xml", drawing);
    rebuilt.Finalize();

    PptxPresentation deck;
    std::string error;
    const bool ok = ReadPptxPresentationFromPackage(rebuilt.TakeBytes(), deck, error, true);
    Check(ok && deck.slides.size() == 3, "the rebuilt deck reads");
    if (!ok || deck.slides.size() != 3) {
        std::cout << "         " << error << "\n";
        return;
    }
    std::vector<const PptxShape*> boxes;
    for (const PptxShape& shape : deck.slides[0].shapes)
        if (shape.geometry == "roundRect" && !shape.paragraphs.empty()) boxes.push_back(&shape);
    Check(boxes.size() == 3 && boxes[0]->paragraphs[0].Text() == "Plan" && boxes[2]->paragraphs[0].Text() == "Ship",
          "SmartArt: its three boxes, from the drawing beside it");
    Check(boxes.size() == 3 && boxes[0]->x == 914400 && boxes[1]->x == 914400 + 1280160 && boxes[0]->y == 914400 &&
              boxes[2]->fill == "0070C0",
          "...placed in the frame, in their fills");
    const PptxShape* jump = Named(deck.slides[0], "Jump");
    Check(jump && !jump->paragraphs.empty() && !jump->paragraphs[0].runs.empty() &&
              jump->paragraphs[0].runs[0].hyperlink == "#slide2",
          "a link to another slide names its number");
    const PptxShape* table = OfKind(deck.slides[2], PptxShapeKind::Table);
    Check(table && table->cellStyles.size() == 3 && table->cellStyles[0][1].fill == "385723" &&
              table->cellStyles[0][1].bold && table->cellStyles[1][2].fill == "E2F0D9" &&
              table->rows[1][2][0].runs[0].color == "003300" && table->rows[0][1][0].runs[0].color == "FFFFFF",
          "the package's table style: header row and body, fills and text colours");
    Check(Contains(PptxPresentationToRichDocument(deck).ToPlainText(), "Build"), "the outline has the diagram's words");
    const auto pixmap = RenderPptxSlide(deck, 0, 1000);
    if (pixmap) {
        Dump(*pixmap, "pptx-smartart");
        // "Build" stands at 1" + 1.4" .. 1" + 2.6" across, 1" .. 2" down: green.
        Check(Coloured(*pixmap, 250, 110, 350, 190, 0x00, 0xB0, 0x50) > 1000, "the diagram's middle box drawn in green");
    }
}


// ----- The writer -----

std::string TempDir() {
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::temp_directory_path(ec) / "uc-pptx-writer-test";
    std::filesystem::create_directories(dir, ec);
    return PathToUtf8(dir);
}

void DumpDeck(const std::vector<uint8_t>& package, const std::string& name) {
    const char* dir = std::getenv("UC_PPTX_TEST_DUMP");
    if (!dir || !*dir) return;
    std::ofstream out(std::string(dir) + "/" + name, std::ios::binary);
    out.write(reinterpret_cast<const char*>(package.data()), static_cast<std::streamsize>(package.size()));
}

// How much two drawings of a slide differ: the mean absolute channel difference.
double Difference(const UCPixmap& a, const UCPixmap& b) {
    if (a.GetRawWidth() != b.GetRawWidth() || a.GetRawHeight() != b.GetRawHeight()) return 255;
    double total = 0;
    for (int y = 0; y < a.GetRawHeight(); ++y)
        for (int x = 0; x < a.GetRawWidth(); ++x) {
            const Rgb p = PixelAt(a, x, y), q = PixelAt(b, x, y);
            total += std::abs(p.r - q.r) + std::abs(p.g - q.g) + std::abs(p.b - q.b);
        }
    return total / (3.0 * a.GetRawWidth() * a.GetRawHeight());
}

bool RoundTrip(const PptxPresentation& deck, PptxPresentation& back, const std::string& dumpName,
               const std::string& extension = "pptx") {
    std::vector<uint8_t> package;
    std::string error;
    if (!WritePptxPresentationToPackage(deck, package, error, extension)) {
        std::cout << "         write: " << error << "\n";
        return false;
    }
    DumpDeck(package, dumpName);
    if (!ReadPptxPresentationFromPackage(package, back, error, true)) {
        std::cout << "         read back: " << error << "\n";
        return false;
    }
    return true;
}

void TestWriterFeatures() {
    std::cout << "\n=== Writer: pptx-features.pptx written and read back ===\n";
    PptxPresentation deck, back;
    std::string error;
    if (!ReadPptxPresentation(Fixture("pptx-features.pptx"), deck, error, "", true)) {
        Check(false, "the fixture reads (" + error + ")");
        return;
    }
    Check(RoundTrip(deck, back, "written-features.pptx") && back.slides.size() == deck.slides.size(),
          "written, and read back: the same three slides");
    if (back.slides.size() != 3) return;
    Check(back.slideWidth == deck.slideWidth && back.slideHeight == deck.slideHeight, "the slide size");
    Check(back.majorFont == deck.majorFont && back.minorFont == deck.minorFont &&
              back.themeColors["accent1"] == deck.themeColors["accent1"],
          "the theme's fonts and colours");
    for (size_t i = 0; i < 3; ++i)
        Check(back.slides[i].title == deck.slides[i].title && back.slides[i].shapes.size() == deck.slides[i].shapes.size(),
              "slide " + std::to_string(i + 1) + ": its title and its " + std::to_string(deck.slides[i].shapes.size()) +
                  " shapes (" + std::to_string(back.slides[i].shapes.size()) + ")");

    const PptxShape* box = Named(back.slides[0], "TextBox 2");
    if (box && box->paragraphs.size() == 4 && box->paragraphs[0].runs.size() == 6) {
        const auto& runs = box->paragraphs[0].runs;
        Check(runs[0].font == "DejaVu Serif" && runs[1].strike && runs[2].baseline == 30 && runs[3].allCaps &&
                  runs[4].highlight == "FFFF00" && runs[5].hyperlink == "https://example.org/",
              "runs: typeface, strike, superscript, capitals, highlight, hyperlink");
        const PptxParagraph& square = box->paragraphs[1];
        Check(square.bullet && square.bulletChar == "\xC2\xA7" && square.bulletFont == "Wingdings" &&
                  square.bulletColor == "00B050" && std::abs(square.bulletSizePercent - 80.0f) < 0.01f &&
                  square.marginLeft == 457200 && square.indent == -228600 &&
                  std::abs(square.spaceBefore.points - 12.0f) < 0.01f &&
                  std::abs(square.lineSpacing.percent - 1.5f) < 0.001f,
              "a paragraph's bullet, margin, indent and spacing");
        Check(box->paragraphs[2].numbered && box->paragraphs[2].numberScheme == "alphaLcParenR" &&
                  box->paragraphs[2].numberStartAt == 3,
              "the numbering scheme and its start");
        Check(box->insetLeft == 182880 && box->insetBottom == 182880, "the insets");
    } else {
        Check(false, "the text box and its paragraphs");
    }
    const PptxShape* vertical = Named(back.slides[0], "TextBox 3");
    Check(vertical && vertical->textRotation == 270 && !vertical->wrap, "vertical text on one line");

    const PptxShape* star = nullptr;
    const PptxShape* arrow = nullptr;
    const PptxShape* freeform = nullptr;
    for (const PptxShape& shape : back.slides[1].shapes) {
        if (shape.geometry == "star5") star = &shape;
        if (shape.kind == PptxShapeKind::Connector && shape.arrowEnd) arrow = &shape;
        if (!shape.customPath.empty()) freeform = &shape;
    }
    Check(star && std::abs(star->rotationDeg - 30.0) < 0.01 && star->flipH && star->gradientStops.size() == 2 &&
              star->gradientStops[1].color == "0000FF" && std::abs(star->gradientAngleDeg - 270.0) < 0.01 &&
              star->dash == RichShape::Dash::Dashed && std::abs(star->lineWidthPt - 2.0) < 0.01,
          "the star: turned, mirrored, its gradient and dashed outline");
    Check(arrow && arrow->line == "0070C0" && !arrow->arrowStart, "the connector and its arrowhead");
    Check(freeform && freeform->customPath.size() >= 4 && freeform->fill == "00B050", "the freeform's outline");

    const PptxShape* table = OfKind(back.slides[2], PptxShapeKind::Table);
    Check(table && table->cellStyles.size() == 3 && table->cellStyles[1][0].gridSpan == 2 &&
              table->cellStyles[1][1].mergedAway && table->cellStyles[2][2].fill == "FF0000" &&
              table->cellStyles[2][2].bottom.color == "0000FF",
          "the table: the merge, the red cell and its blue border");
    const PptxShape* chart = OfKind(back.slides[2], PptxShapeKind::Chart);
    Check(chart && chart->chart.kind == RichChart::Kind::Column && chart->chart.title == "Sales" &&
              chart->chart.series.size() == 2 && chart->chart.series[1].values.size() == 3 &&
              chart->chart.series[1].values[2] == 4.0 && chart->chart.categories.size() == 3,
          "the chart: its kind, title, series and categories");
    const PptxShape* picture = OfKind(back.slides[2], PptxShapeKind::Picture);
    const PptxShape* original = OfKind(deck.slides[2], PptxShapeKind::Picture);
    Check(picture && original && std::abs(picture->cropLeft - 0.25) < 0.001 &&
              back.pictures[picture->pictureTarget] == deck.pictures.at(original->pictureTarget),
          "the picture: its crop and the same bytes");

    // Drawn, the written deck looks as the one it came from.
    for (size_t i = 0; i < 3; ++i) {
        const auto before = RenderPptxSlide(deck, i, 500), after = RenderPptxSlide(back, i, 500);
        const double diff = before && after ? Difference(*before, *after) : 255;
        if (before && after) {
            Dump(*before, "roundtrip-" + std::to_string(i + 1) + "-before");
            Dump(*after, "roundtrip-" + std::to_string(i + 1) + "-after");
        }
        Check(diff < 1.0, "slide " + std::to_string(i + 1) + " drawn from the written deck looks the same (" +
                              std::to_string(diff) + ")");
    }
}

void TestWriterSample() {
    std::cout << "\n=== Writer: the python-pptx sample, notes, a hidden slide, a password ===\n";
    PptxPresentation deck, back;
    std::string error;
    if (!ReadPptxPresentation(Fixture("office-pptx-sample.pptx"), deck, error, "", true)) {
        Check(false, "the sample reads (" + error + ")");
        return;
    }
    Check(RoundTrip(deck, back, "written-sample.pptx") && back.slides.size() == deck.slides.size(),
          "written and read back: its slides");
    if (back.slides.size() != deck.slides.size()) return;
    bool titles = true, notes = true, hidden = true, text = true;
    for (size_t i = 0; i < deck.slides.size(); ++i) {
        titles = titles && back.slides[i].title == deck.slides[i].title;
        hidden = hidden && back.slides[i].hidden == deck.slides[i].hidden;
        notes = notes && back.slides[i].notes.size() == deck.slides[i].notes.size();
        text = text && PptxPresentationToRichDocument(back).ToPlainText() == PptxPresentationToRichDocument(deck).ToPlainText();
    }
    Check(titles && hidden && notes, "titles, the hidden slide and the speaker notes");
    Check(text, "the outline of the written deck is the original's");

    // With a password: encrypted, refused without it.
    const std::string path = TempDir() + "/protected.pptx";
    Check(WritePptxPresentation(deck, path, error, "s3cret \xC3\xBC") && IsEncryptedOfficePackage(path),
          "written with a password: an encrypted package (" + error + ")");
    PptxPresentation locked;
    Check(!ReadPptxPresentation(path, locked, error) && Contains(error, "password"), "without it, refused");
    Check(ReadPptxPresentation(path, locked, error, "s3cret \xC3\xBC") && locked.slides.size() == deck.slides.size(),
          "with it, read");

    // The package kinds.
    std::vector<uint8_t> package;
    Check(WritePptxPresentationToPackage(deck, package, error, "potx"), "written as a template");
    UCZipPackageReader zip;
    std::string types;
    Check(zip.OpenFromMemory(package) && zip.ReadEntry("[Content_Types].xml", types) &&
              Contains(types, "presentationml.template.main+xml"),
          "a template names itself one");
    PptxPresentation empty;
    Check(WritePptxPresentationToPackage(empty, package, error) && ReadPptxPresentationFromPackage(package, back, error) &&
              back.slides.empty(),
          "a deck of no slides is written and read");
}

void TestDocumentAsDeck() {
    std::cout << "\n=== A document laid out as a deck ===\n";
    UCRichDocument document;
    document.metadata.title = "Plan";
    auto block = [](RichBlockType type, const std::string& text) {
        RichDocBlock b;
        b.type = type;
        RichTextRun run;
        run.text = text;
        b.runs.push_back(run);
        return b;
    };
    document.blocks.push_back(block(RichBlockType::Paragraph, "Opening words"));
    RichDocBlock h1 = block(RichBlockType::Heading, "Agenda");
    h1.headingLevel = 1;
    document.blocks.push_back(h1);
    RichDocBlock item = block(RichBlockType::ListItem, "First");
    document.blocks.push_back(item);
    item = block(RichBlockType::ListItem, "Second, deeper");
    item.listLevel = 1;
    document.blocks.push_back(item);
    item = block(RichBlockType::ListItem, "Third");
    item.orderedList = true;
    item.numberFormat = RichNumberFormat::LowerRoman;
    item.listStartNumber = 2;
    document.blocks.push_back(item);
    RichDocBlock table;
    table.type = RichBlockType::Table;
    for (int r = 0; r < 2; ++r) {
        RichTableRow row;
        row.header = r == 0;
        for (int c = 0; c < 2; ++c) {
            RichTableCell cell;
            RichTextRun run;
            run.text = "R" + std::to_string(r) + "C" + std::to_string(c);
            cell.runs.push_back(run);
            row.cells.push_back(cell);
        }
        table.tableRows.push_back(row);
    }
    document.blocks.push_back(table);
    RichDocBlock h2 = block(RichBlockType::Heading, "Numbers");
    h2.headingLevel = 1;
    document.blocks.push_back(h2);
    RichDocBlock code = block(RichBlockType::CodeBlock, "x = 1");
    document.blocks.push_back(code);
    RichDocBlock rule;
    rule.type = RichBlockType::HorizontalRule;
    document.blocks.push_back(rule);
    document.blocks.push_back(block(RichBlockType::Paragraph, "After the rule"));

    const PptxPresentation deck = PptxPresentationFromRichDocument(document);
    Check(deck.slides.size() == 4, "four slides: the opening, two headings, after the rule (" +
                                       std::to_string(deck.slides.size()) + ")");
    if (deck.slides.size() != 4) return;
    Check(deck.slides[0].title == "Plan" && deck.slides[1].title == "Agenda" && deck.slides[2].title == "Numbers" &&
              deck.slides[3].title.empty(),
          "the document's title, then the headings as titles");
    const PptxShape* body = OfKind(deck.slides[1], PptxShapeKind::Body);
    Check(body && body->paragraphs.size() == 3 && body->paragraphs[0].bullet && body->paragraphs[1].level == 1 &&
              body->paragraphs[2].numbered && body->paragraphs[2].numberScheme == "romanLcPeriod" &&
              body->paragraphs[2].numberStartAt == 2,
          "the list: bullets, a deeper level, a roman numbering from ii");
    const PptxShape* table2 = OfKind(deck.slides[1], PptxShapeKind::Table);
    Check(table2 && body && table2->rows.size() == 2 && table2->y >= body->y + body->height &&
              table2->rows[0][0][0].runs[0].bold,
          "the table below the text, its header row bold");
    const PptxShape* code2 = OfKind(deck.slides[2], PptxShapeKind::Body);
    Check(code2 && !code2->paragraphs.empty() && code2->paragraphs[0].runs[0].font == "Courier New",
          "code in Courier New");

    PptxPresentation back;
    Check(RoundTrip(deck, back, "written-document.pptx") && back.slides.size() == 4 && back.slides[1].title == "Agenda",
          "the deck written and read back");

    // A deck's outline reads back as its slides.
    PptxPresentation sample;
    std::string error;
    if (ReadPptxPresentation(Fixture("office-pptx-sample.pptx"), sample, error)) {
        const PptxPresentation again = PptxPresentationFromRichDocument(PptxPresentationToRichDocument(sample));
        bool same = again.slides.size() == sample.slides.size();
        for (size_t i = 0; same && i < sample.slides.size(); ++i)
            same = again.slides[i].title == sample.slides[i].title && again.slides[i].hidden == sample.slides[i].hidden &&
                   again.slides[i].notes.size() == sample.slides[i].notes.size();
        Check(same, "the sample's outline laid out again: its titles, hidden slide and notes");
    }

    // Through the file loader: a document saved as .pptx, opened again.
    const std::string path = TempDir() + "/document.pptx";
    Check(UltraCanvasFileLoader::SaveTextDocument(document, path, error), "SaveTextDocument writes a .pptx (" + error + ")");
    auto opened = UltraCanvasFileLoader::LoadTextDocument(path, error);
    Check(opened && Contains(opened->ToPlainText(), "2. Agenda") && Contains(opened->ToPlainText(), "Second, deeper"),
          "LoadTextDocument opens it as its slides' outline");
}

void TestOdpWriter() {
    std::cout << "\n=== The OpenDocument writer ===\n";
    PptxPresentation features, sample;
    std::string error;
    if (!ReadPptxPresentation(Fixture("pptx-features.pptx"), features, error, "", true) ||
        !ReadPptxPresentation(Fixture("office-pptx-sample.pptx"), sample, error, "", true)) {
        Check(false, "the fixtures read (" + error + ")");
        return;
    }
    // The features deck: what the .odp reader reads of it.
    std::vector<uint8_t> bytes;
    PptxPresentation back;
    Check(WriteOdpPresentationToMemory(features, bytes, error) && ReadOdpPresentationFromMemory(bytes, back, error) &&
              back.slides.size() == 3,
          "the features deck written as .odp and read back (" + error + ")");
    DumpDeck(bytes, "written-features.odp");
    if (back.slides.size() == 3) {
        Check(back.slideWidth == features.slideWidth && back.slideHeight == features.slideHeight, "the slide size");
        Check(back.slides[0].title == "Text" && back.slides[1].title == "Shapes" && back.slides[2].title == "Table and chart",
              "the titles");
        const PptxShape* box = Named(back.slides[0], "TextBox 2");
        Check(box && box->paragraphs.size() == 4 && box->paragraphs[0].Text() == "Serif struck 2 caps marked link" &&
                  box->paragraphs[1].bullet && !box->paragraphs[1].numbered && box->paragraphs[2].numbered &&
                  box->paragraphs[3].Text() == "Fourth",
              "the text box: its runs' text with the spaces between them, a bullet, a numbered item");
        const PptxShape* table = OfKind(back.slides[2], PptxShapeKind::Table);
        Check(table && table->rows.size() == 3 && table->rows[0].size() == 3 && table->rows[2][2][0].Text() == "R3C3" &&
                  table->rows[1][1].empty(),
              "the table: its cells, a merged one covered");
        const PptxShape* chart = OfKind(back.slides[2], PptxShapeKind::Chart);
        Check(chart && chart->chart.kind == RichChart::Kind::Column && chart->chart.title == "Sales" &&
                  chart->chart.series.size() == 2 && chart->chart.series[1].values.size() == 3 &&
                  chart->chart.series[1].values[2] == 4.0,
              "the chart, as a chart object read back");
        const PptxShape* picture = OfKind(back.slides[2], PptxShapeKind::Picture);
        UCZipPackageReader zip;
        std::string mimetype, manifest;
        Check(picture && zip.OpenFromMemory(bytes) && zip.ReadEntry("mimetype", mimetype) &&
                  mimetype == "application/vnd.oasis.opendocument.presentation" &&
                  zip.ReadEntry("META-INF/manifest.xml", manifest) && Contains(manifest, picture->pictureTarget) &&
                  Contains(manifest, "Object 1/content.xml"),
              "the package: its media type, the picture and the chart object in the manifest");
        // Read back with its look: drawn as the original.
        PptxPresentation drawn;
        Check(ReadOdpPresentationFromMemory(bytes, drawn, error, "", true) && drawn.pictures.size() == 1,
              "read back withPictures: the picture's bytes");
        const PptxShape* star = Named(back.slides[1], "5-Point Star 2");
        Check(star && std::abs(star->rotationDeg - 30.0) < 0.01 && !star->customPath.empty() &&
                  star->gradientStops.size() == 2 && star->line == "202020" && star->dash == RichShape::Dash::Dashed,
              "the star: turned, its mirrored outline, its gradient and dashed outline");
        const PptxShape* box2 = Named(back.slides[0], "TextBox 2");
        Check(box2 && box2->paragraphs.size() == 4 && box2->paragraphs[0].runs.size() == 6 &&
                  box2->paragraphs[0].runs[0].font == "DejaVu Serif" && box2->paragraphs[0].runs[1].strike &&
                  box2->paragraphs[0].runs[2].baseline > 0 && box2->paragraphs[0].runs[5].hyperlink == "https://example.org/" &&
                  box2->paragraphs[1].bulletColor == "00B050" &&
                  PptxNumberLabel(box2->paragraphs[2].numberScheme, box2->paragraphs[2].numberStartAt) == "c)",
              "the text: typefaces, strike, superscript, the link, the bullet's colour, numbering from c)");
        for (size_t i = 0; i < 3 && drawn.slides.size() == 3; ++i) {
            auto a = RenderPptxSlide(features, i, 640), b = RenderPptxSlide(drawn, i, 640);
            const double difference = a && b ? Difference(*a, *b) : 255;
            if (b) Dump(*b, "written-features-odp-" + std::to_string(i + 1));
            Check(difference < 1.5, "written and read back, slide " + std::to_string(i + 1) +
                                        " is drawn as the original (mean difference " + std::to_string(difference) + ")");
        }
    }

    // The sample: titles, the hidden slide, the notes, the outline.
    PptxPresentation sampleBack;
    Check(WriteOdpPresentationToMemory(sample, bytes, error) && ReadOdpPresentationFromMemory(bytes, sampleBack, error) &&
              sampleBack.slides.size() == sample.slides.size(),
          "the python-pptx sample written as .odp and read back");
    DumpDeck(bytes, "written-sample.odp");
    if (sampleBack.slides.size() == sample.slides.size()) {
        bool same = true;
        for (size_t i = 0; i < sample.slides.size(); ++i)
            same = same && sampleBack.slides[i].title == sample.slides[i].title &&
                   sampleBack.slides[i].hidden == sample.slides[i].hidden &&
                   sampleBack.slides[i].notes.size() == sample.slides[i].notes.size();
        Check(same, "titles, the hidden slide and the speaker notes");
        const std::string before = PptxPresentationToRichDocument(sample).ToPlainText();
        const std::string after = PptxPresentationToRichDocument(sampleBack).ToPlainText();
        Check(before == after, "the outline of the written deck is the original's");
        if (before != after) std::cout << "--- before\n" << before << "\n--- after\n" << after << "\n";
    }

    // A mirrored shape: mirrored in its own outline (and its gradient), as
    // PowerPoint mirrors it before turning it - not with ODF's mirror, which
    // comes after the turn and leaves the fill alone.
    {
        PptxPresentation flipped;
        PptxSlide slide;
        PptxShape arrow;
        arrow.geometry = "rightArrow";
        arrow.x = arrow.y = 914400;
        arrow.width = 1828800;
        arrow.height = 914400;
        arrow.rotationDeg = 30;
        arrow.flipH = true;
        arrow.fill = "FF0000";
        arrow.gradientStops = {{0.0f, "FF0000"}, {1.0f, "0000FF"}};
        arrow.gradientAngleDeg = 0;
        slide.shapes.push_back(arrow);
        flipped.slides.push_back(slide);
        std::string content, common;
        UCZipPackageReader package;
        Check(WriteOdpPresentationToMemory(flipped, bytes, error) && package.OpenFromMemory(bytes) &&
                  package.ReadEntry("content.xml", content) && package.ReadEntry("styles.xml", common) &&
                  !Contains(content, "draw:mirror-horizontal") &&
                  Contains(content, "draw:enhanced-path=\"M 21600 5400") && Contains(common, "draw:angle=\"240deg\"") &&
                  Contains(content, "draw:transform=\"rotate (-0.523599)"),
              "a mirrored arrow: its outline mirrored, its gradient turned the other way and with the shape, "
              "then the shape turned");
    }

    // Flat, and a template.
    Check(WriteOdpPresentationToMemory(features, bytes, error, "fodp") && bytes.size() > 5 && bytes[0] == '<' &&
              ReadOdpPresentationFromMemory(bytes, back, error) && back.slides.size() == 3 &&
              OfKind(back.slides[2], PptxShapeKind::Chart) != nullptr,
          "a flat .fodp: one XML document, read back with its chart");
    DumpDeck(bytes, "written-features.fodp");
    UCZipPackageReader templ;
    std::string mimetype;
    Check(WriteOdpPresentationToMemory(sample, bytes, error, "otp") && templ.OpenFromMemory(bytes) &&
              templ.ReadEntry("mimetype", mimetype) && mimetype == "application/vnd.oasis.opendocument.presentation-template",
          "a template names itself one");

    // Through the file loader.
    const std::string dir = TempDir();
    UCRichDocument document = UCRichDocument::FromMarkdown("# Agenda\n\n- First\n- Second\n\n# Numbers\n\n1. One\n2. Two\n");
    Check(UltraCanvasFileLoader::SaveTextDocument(document, dir + "/document.odp", error),
          "SaveTextDocument writes an .odp (" + error + ")");
    auto opened = UltraCanvasFileLoader::LoadTextDocument(dir + "/document.odp", error);
    Check(opened && Contains(opened->ToPlainText(), "1. Agenda") && Contains(opened->ToPlainText(), "Second") &&
              Contains(opened->ToPlainText(), "2. Numbers"),
          "LoadTextDocument opens it as its slides' outline");
    Check(UltraCanvasFileLoader::SavePresentation(sample, dir + "/sample.fodp", error) &&
              ReadOdpPresentation(dir + "/sample.fodp", back, error) && back.slides.size() == sample.slides.size(),
          "SavePresentation writes a .fodp");
    Check(!UltraCanvasFileLoader::SavePresentation(sample, dir + "/locked.odp", error, "secret") &&
              Contains(error, "password"),
          "an OpenDocument presentation with a password is refused, not saved unprotected");
}

} // namespace

// The .odp reader's look: LibreOffice's conversion of the features deck read
// as the .pptx reader reads the original, and drawn the same.
void TestOdpLook() {
    std::cout << "\n=== The .odp reader's look: pptx-features.odp (LibreOffice) ===\n";
    PptxPresentation pptx, odp;
    std::string error;
    if (!ReadPptxPresentation(Fixture("pptx-features.pptx"), pptx, error, "", true) ||
        !ReadOdpPresentation(Fixture("pptx-features.odp"), odp, error, "", true) || odp.slides.size() != 3) {
        Check(false, "the fixtures read (" + error + ")");
        return;
    }
    Check(odp.slideWidth == pptx.slideWidth && odp.slideHeight == pptx.slideHeight && odp.slides[0].background == "FFFFFF",
          "the slide size, and the background from the master page's style");
    Check(odp.themeColors.size() == 12 && odp.themeColors["accent1"] == "4F81BD" && odp.themeColors["hlink"] == "0000FF",
          "the theme's colours (loext:theme)");

    const PptxShape* title = OfKind(odp.slides[0], PptxShapeKind::Title);
    Check(title && title->placeholder == "title" && title->anchor == PptxAnchor::Middle && !title->paragraphs.empty() &&
              title->paragraphs[0].align == PptxAlign::Center && !title->paragraphs[0].runs.empty() &&
              title->paragraphs[0].runs[0].sizePt == 44.0f && title->paragraphs[0].runs[0].font == "Calibri",
          "the title: a placeholder, centred, anchored in the middle, 44 pt Calibri");

    const PptxShape* box = Named(odp.slides[0], "TextBox 2");
    if (!box || box->paragraphs.size() != 4 || box->paragraphs[0].runs.size() != 6) {
        Check(false, "the text box: four paragraphs, six runs in the first");
    } else {
        const std::vector<PptxRun>& runs = box->paragraphs[0].runs;
        Check(runs[0].font == "DejaVu Serif" && runs[0].sizePt == 18.0f && runs[1].strike && !runs[0].strike &&
                  runs[2].baseline == 30 && runs[3].allCaps && runs[4].highlight == "FFFF00" &&
                  runs[5].hyperlink == "https://example.org/" && runs[5].underline && runs[5].color == "0000FF",
              "runs: a typeface by its font face, strike, superscript, capitals, a highlight, a link");
        Check(box->insetLeft == 182880 && box->insetTop == 182880 && box->anchor == PptxAnchor::Top && box->wrap,
              "the text area: its padding, anchor and wrap");
        const PptxParagraph& square = box->paragraphs[1];
        const PptxParagraph& expected = Named(pptx.slides[0], "TextBox 2")->paragraphs[1];
        Check(square.bullet && !square.numbered && square.bulletChar == expected.bulletChar &&
                  square.bulletFont == "Wingdings" && square.bulletColor == "00B050" && square.bulletSizePercent == 80.0f &&
                  square.marginLeft == expected.marginLeft && square.indent == expected.indent &&
                  std::abs(square.spaceBefore.points - 12.0f) < 0.1f && std::abs(square.lineSpacing.percent - 1.5f) < 0.01f,
              "a Wingdings bullet: its code from the private-use character, colour, size, hanging indent, spacing");
        const PptxParagraph& third = box->paragraphs[2];
        const PptxParagraph& fourth = box->paragraphs[3];
        Check(third.numbered && third.numberScheme == "alphaLcParenR" && fourth.numbered &&
                  PptxNumberLabel(third.numberScheme, third.numberStartAt) == "c)" &&
                  PptxNumberLabel(fourth.numberScheme, fourth.numberStartAt + 1) == "d)",
              "numbering: the scheme, from c) on, counted on as the renderer counts");
    }
    const PptxShape* vertical = Named(odp.slides[0], "TextBox 3");
    Check(vertical && vertical->textRotation == 270 && !vertical->wrap, "vertical text (bt-lr), not wrapped");

    const PptxShape* star = Named(odp.slides[1], "5-Point Star 2");
    const PptxShape* pptxStar = Named(pptx.slides[1], "5-Point Star 2");
    Check(star && pptxStar && star->geometry == "star5" && std::abs(star->rotationDeg - 30.0) < 0.01 && star->flipH &&
              !star->flipV && std::llabs(star->x - pptxStar->x) < 1000 && std::llabs(star->y - pptxStar->y) < 1000,
          "the star: its Office preset, turned and mirrored (ODF's mirror after the turn undone), in place");
    Check(star && star->gradientStops.size() == 2 && star->gradientStops[0].color == "FF0000" &&
              star->gradientStops[1].color == "0000FF" && std::abs(star->gradientAngleDeg - pptxStar->gradientAngleDeg) < 0.5,
          "... its gradient: the stops, the angle back in the shape (LibreOffice's turn taken out)");
    Check(star && star->line == "202020" && std::abs(star->lineWidthPt - 2.0) < 0.05 &&
              star->dash == RichShape::Dash::Dashed && star->anchor == PptxAnchor::Middle,
          "... its outline: colour, width, a long dash");
    const PptxShape* arrowed = Named(odp.slides[1], "Connector 3");
    Check(arrowed && arrowed->kind == PptxShapeKind::Connector && arrowed->line == "0070C0" && arrowed->arrowEnd &&
              !arrowed->arrowStart && arrowed->width == 2743200 && arrowed->height == 1828800,
          "a straight connector: its ends, colour and arrowhead (a marker)");
    const PptxShape* bent = Named(odp.slides[1], "Connector 4");
    Check(bent && bent->geometry == "bentConnector3" && bent->line == "7030A0",
          "a standard connector with only its ends: routed with elbows");
    const PptxShape* freeform = Named(odp.slides[1], "Freeform 5");
    Check(freeform && freeform->customPath.size() == 4 && freeform->fill == "00B050" &&
              std::abs(freeform->customPath[1].x - 1.0f) < 0.01f && std::abs(freeform->customPath[1].y - 1.0f) < 0.01f,
          "a freeform: its enhanced path in the sub-view's units, filled");

    const PptxShape* table = OfKind(odp.slides[2], PptxShapeKind::Table);
    Check(table && table->cellStyles.size() == 3 && table->columnWidths.size() == 3 &&
              table->columnWidths[0] == 1371600 && table->rowHeights.size() == 3 && table->rowHeights[0] == 457200,
          "the table: column widths and row heights from their styles");
    if (table && table->cellStyles.size() == 3 && table->cellStyles[2].size() == 3) {
        const PptxCellStyle& header = table->cellStyles[0][0];
        const PptxCellStyle& red = table->cellStyles[2][2];
        Check(header.fill == "4F81BD" && header.top.color == "FFFFFF" && header.top.widthPt > 0.4 &&
                  table->rows[0][0][0].runs[0].color == "FFFFFF",
              "a header cell: its fill (loext:graphic-properties), borders and white text");
        Check(red.fill == "FF0000" && red.bottom.color == "0000FF" && red.bottom.widthPt > red.top.widthPt,
              "a cell of its own: its fill and a heavier blue bottom border");
        Check(table->cellStyles[1][0].gridSpan == 2 && table->cellStyles[1][1].mergedAway,
              "a merged cell: its span, the cell it covers");
    }
    const PptxShape* picture = OfKind(odp.slides[2], PptxShapeKind::Picture);
    Check(picture && odp.pictures.count(picture->pictureTarget) && !odp.pictures[picture->pictureTarget].empty(),
          "the picture's bytes, read withPictures");

    // Drawn: each slide as the .pptx's drawing of it.
    for (size_t i = 0; i < 3; ++i) {
        auto a = RenderPptxSlide(pptx, i, 640), b = RenderPptxSlide(odp, i, 640);
        const double difference = a && b ? Difference(*a, *b) : 255;
        if (b) Dump(*b, "pptx-features-odp-" + std::to_string(i + 1));
        // The table's and the chart's colours are LibreOffice's own on slide 3.
        Check(difference < (i == 2 ? 2.5 : 1.0), "slide " + std::to_string(i + 1) + " drawn as the .pptx's (mean difference " +
                                                     std::to_string(difference) + ")");
    }
}

int main() {
    TestNumberLabels();
    TestFixture();
    TestRebuilt();
    TestWriterFeatures();
    TestWriterSample();
    TestDocumentAsDeck();
    TestOdpWriter();
    TestOdpLook();
    std::cout << "\n" << (g_failures == 0 ? "All checks passed" : "FAILURES: " + std::to_string(g_failures)) << "\n";
    return g_failures == 0 ? 0 : 1;
}
