// Tests/PresentationPrintTest.cpp
// A deck's slides drawn on any render context and as pages:
//  - DrawPptxSlide paints a slide into a rectangle and nothing around it,
//    with grey prompt text in an empty placeholder;
//  - PresentationPrintPages has a page per shown slide (hidden ones left out
//    unless asked for), at the slide's size, and writes them as a PDF.
//
// Usage: PresentationPrintTest [pdf-output-path]
// Exit code is the number of failed checks.

#include "IODeviceManager/UltraCanvasIODevicePrinterRasterTarget.h"
#include "UltraCanvasPptxSlideRender.h"
#include "UltraCanvasPresentationPrint.h"
#include "UltraCanvasPresentationPptx.h"
#include "UltraCanvasRenderContext.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) ++failures;
    std::printf("%s: %s\n", ok ? "  ok" : "FAIL", what.c_str());
}

struct Pixel { int r, g, b; };

// The pixels of a raster page, row by row.
std::vector<std::vector<Pixel>> Pixels(RasterPageTarget& page) {
    std::vector<std::vector<Pixel>> rows;
    page.ForEachRow([&rows](const uint8_t* rgba, int width) {
        std::vector<Pixel> row;
        for (int x = 0; x < width; ++x) row.push_back({rgba[x * 4], rgba[x * 4 + 1], rgba[x * 4 + 2]});
        rows.push_back(std::move(row));
        return true;
    });
    return rows;
}

} // namespace

int main(int argc, char** argv) {
    std::printf("=== Slides on a render context, and as pages ===\n");
    PptxPresentation deck;
    deck.slideWidth = 12192000;
    deck.slideHeight = 6858000;
    PptxSlide first = PptxSlideFromLayout(deck, -1);   // Title and Content, empty
    first.background = "1F4E79";
    deck.slides.push_back(first);
    PptxSlide second = PptxSlideFromLayout(deck, -1);
    second.shapes[0].paragraphs[0].runs[0].text = "Second";
    second.hidden = true;
    deck.slides.push_back(second);
    PptxSlide third = PptxSlideFromLayout(deck, -1);
    third.shapes[0].paragraphs[0].runs[0].text = "Third";
    deck.slides.push_back(third);
    Check(first.shapes.size() == 2 && first.shapes[0].placeholder == "title" && first.shapes[1].placeholder == "body",
          "a new slide without layouts is Title and Content");

    std::printf("\n-- into a rectangle, nothing around it --\n");
    RasterPageTarget page(400, 300, 96, 96);
    Check(page.IsValid(), "an off-screen page");
    page.BeginPage();
    IRenderContext* ctx = page.GetRenderContext();
    ctx->Clear(Color(255, 0, 0));
    DrawPptxSlide(ctx, deck, deck.slides[0], Rect2Dd(40, 40, 320, 180));
    auto rows = Pixels(page);
    const Pixel outside = rows[10][10], inside = rows[200][60], below = rows[260][200];
    Check(outside.r == 255 && outside.g == 0 && below.r == 255 && below.g == 0, "the context around the slide is untouched");
    Check(inside.r == 0x1F && inside.g == 0x4E && inside.b == 0x79, "the slide's background fills its rectangle");

    std::printf("\n-- prompt text in an empty placeholder --\n");
    PptxSlideDrawOptions options;
    options.promptText = [](const PptxShape& shape, size_t) {
        return shape.placeholder == "title" ? std::string("Click to add title") : std::string();
    };
    PptxSlide white = deck.slides[0];
    white.background = "FFFFFF";
    auto greyIn = [&](const PptxSlideDrawOptions& drawOptions) {
        page.BeginPage();
        page.GetRenderContext()->Clear(Color(255, 255, 255));
        DrawPptxSlide(page.GetRenderContext(), deck, white, Rect2Dd(0, 0, 400, 225), drawOptions);
        int grey = 0;
        for (const auto& row : Pixels(page))
            for (const Pixel& p : row)
                if (p.r < 200 && std::abs(p.r - p.g) < 8 && std::abs(p.g - p.b) < 8) ++grey;
        return grey;
    };
    const int withPrompt = greyIn(options);
    const int without = greyIn(PptxSlideDrawOptions());
    Check(withPrompt > 50 && without == 0,
          "grey text in the empty title, none without a prompt [" + std::to_string(withPrompt) + " / " + std::to_string(without) + "]");

    std::printf("\n-- pages --\n");
    PresentationPrintPages pages(deck);
    Check(pages.GetPageCount() == 2 && pages.GetSlideOfPage(1) == 2, "the hidden slide is left out");
    Check(std::fabs(pages.GetPageWidthPt() - 960.0) < 0.01 && std::fabs(pages.GetPageHeightPt() - 540.0) < 0.01,
          "a page is the slide's size, 960 x 540 pt");
    PresentationPrintPages all(deck, true);
    Check(all.GetPageCount() == 3, "or kept when asked for");
    std::string error;
    const std::string path = argc > 1 ? argv[1] : "presentation-print-test.pdf";
    Check(pages.ExportPdf(path, error), "the PDF " + path + " [" + error + "]");
    std::vector<uint8_t> pdf;
    Check(pages.ExportPdf(pdf, error) && pdf.size() > 4 && std::string(pdf.begin(), pdf.begin() + 4) == "%PDF",
          "and in memory");

    std::printf("\n%s (%d failed)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
