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
//   * each slide drawn (RenderPptxSlide) and looked at where the shapes are.
// UC_PPTX_TEST_DUMP=<dir> writes the drawn slides there as .ppm, to look at.
// Version: 1.0.0
// Last Modified: 2026-11-20
// Author: UltraCanvas Framework

#include "UltraCanvasPptxSlideRender.h"
#include "UltraCanvasPresentationPptx.h"
#include "UltraCanvasZipPackage.h"

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

} // namespace

int main() {
    TestNumberLabels();
    TestFixture();
    TestRebuilt();
    std::cout << "\n" << (g_failures == 0 ? "All checks passed" : "FAILURES: " + std::to_string(g_failures)) << "\n";
    return g_failures == 0 ? 0 : 1;
}
