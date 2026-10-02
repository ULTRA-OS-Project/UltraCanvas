// Tests/RichTextPrintTest.cpp
// A word-processing document's pages, for the printers that cannot take its
// PDF: drawn straight into a render context (GutenPrint, IPP's PWG raster) or
// drawn off screen and placed as an image (a Windows printer DC). Every path
// is checked here without a printer: the pages are drawn into an off-screen
// page and into a recording target, and a job carrying the PDF and its pages
// is handed to the renderers that take each.
// Version: 1.0.0
// Author: UltraCanvas Framework

#include "UltraCanvasRichTextPrint.h"
#include "UltraCanvasRenderContext.h"
#include "IODeviceManager/UltraCanvasIODevicePrinter.h"
#include "IODeviceManager/UltraCanvasIODevicePrinterJobSource.h"
#include "IODeviceManager/UltraCanvasIODevicePrinterRasterTarget.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

RichDocBlock Paragraph(const std::string& text, bool bold = false) {
    RichDocBlock block;
    block.type = RichBlockType::Paragraph;
    RichTextRun run;
    run.text = text;
    run.bold = bold;
    block.runs.push_back(run);
    return block;
}

RichDocBlock PageBreak() {
    RichDocBlock block;
    block.type = RichBlockType::PageBreak;
    return block;
}

// Two pages: a bold first line and a paragraph, a page break, a paragraph.
// A4 with 2 cm margins, which is also what a document stating no page gets.
UCRichDocument TwoPageDocument() {
    UCRichDocument document;
    document.blocks.push_back(Paragraph("Quarterly report", true));
    document.blocks.push_back(Paragraph("The first page carries the summary."));
    document.blocks.push_back(PageBreak());
    document.blocks.push_back(Paragraph("The second page carries the detail."));
    return document;
}

// The ink on a page, as the rectangle holding every non-white pixel.
struct InkBox {
    int left = -1, top = -1, right = -1, bottom = -1;
    long pixels = 0;
    bool selectionTint = false;        // any pixel of the editor's selection colour
    bool Any() const { return pixels > 0; }
};

InkBox MeasureInk(const RasterPageTarget& page) {
    InkBox box;
    int y = 0;
    page.ForEachRow([&](const uint8_t* rgba, int width) {
        for (int x = 0; x < width; ++x) {
            const uint8_t* p = rgba + x * 4;
            if (p[0] > 245 && p[1] > 245 && p[2] > 245) continue;
            ++box.pixels;
            if (box.left < 0 || x < box.left) box.left = x;
            if (x > box.right) box.right = x;
            if (box.top < 0) box.top = y;
            box.bottom = y;
            // RichTextEditStyle::selectionColor, (180, 212, 253).
            if (std::abs(p[0] - 180) < 6 && std::abs(p[1] - 212) < 6 && std::abs(p[2] - 253) < 6) {
                box.selectionTint = true;
            }
        }
        ++y;
        return true;
    });
    return box;
}

// A target like a Windows printer DC: pages reach it only as images, and it
// has an unprintable margin the page must reach past to line up with the
// sheet.
class RecordingTarget : public IPrintPageTarget {
public:
    RecordingTarget(int width, int height, int dpi, int offset)
        : widthDots(width), heightDots(height), dpiValue(dpi), offsetDots(offset) {}

    IOPrintPageMetrics GetMetrics() const override {
        IOPrintPageMetrics m;
        m.widthDots = widthDots;
        m.heightDots = heightDots;
        m.dpiX = m.dpiY = dpiValue;
        m.offsetXDots = m.offsetYDots = offsetDots;
        return m;
    }
    bool DrawImage(const uint8_t* pixels, int width, int height, const IOPrintRect& dest) override {
        images++;
        lastWidth = width;
        lastHeight = height;
        lastDest = dest;
        inkPixels = 0;
        for (long i = 0; i < static_cast<long>(width) * height; ++i) {
            const uint8_t* p = pixels + i * 4;
            if (p[0] < 128 && p[1] < 128 && p[2] < 128) ++inkPixels;
        }
        return true;
    }
    bool DrawTextLine(const std::string&, int, int, int) override { return true; }
    int MeasureTextWidth(const std::string& utf8, int pixelHeight) const override {
        return static_cast<int>(utf8.size()) * pixelHeight / 2;
    }
    int GetLineHeight(int pixelHeight) const override { return pixelHeight + pixelHeight / 5; }
    int GetAscent(int pixelHeight) const override { return pixelHeight * 4 / 5; }

    int images = 0;
    int lastWidth = 0;
    int lastHeight = 0;
    IOPrintRect lastDest;
    long inkPixels = 0;

private:
    int widthDots, heightDots, dpiValue, offsetDots;
};

// A4 at 100 dpi: 827 x 1169 dots, and a 2 cm margin is 79 of them.
constexpr int kA4Width100 = 827;
constexpr int kA4Height100 = 1169;
constexpr int kMargin100 = 79;

void TestDrawnIntoARenderContext() {
    std::cout << "\nPages drawn into a render context (GutenPrint's path)\n";
    auto pages = std::make_shared<RichDocumentPrintPages>(TwoPageDocument(), RichTextEditStyle());
    RasterPageTarget page(kA4Width100, kA4Height100, 100, 100);
    Check(page.IsValid(), "an off-screen page can be made");
    if (!page.IsValid()) return;

    page.BeginPage();
    IODeviceResult prepared = pages->Prepare(page);
    Check(prepared.success, "the document is laid out on the device");
    Check(pages->GetPageCount() == 2, "into two pages, as its page break says");

    const RichDocumentPrintPages::Placement& at = pages->GetPlacement();
    Check(std::abs(at.x) < 0.5 && std::abs(at.y) < 0.5, "an A4 page on an A4 sheet sits on the sheet's corner");
    Check(std::abs(at.scaleX - 100.0 / 96.0) < 1e-6, "at 1:1 - a view pixel is 100/96 of a dot at 100 dpi");

    for (int i = 0; i < 2; ++i) {
        page.BeginPage();
        Check(pages->DrawPage(i, page).success, "page " + std::to_string(i + 1) + " draws");
        const InkBox ink = MeasureInk(page);
        Check(ink.Any(), "page " + std::to_string(i + 1) + " has text on it");
        Check(ink.left >= kMargin100 - 3 && ink.top >= kMargin100 - 3,
              "page " + std::to_string(i + 1) + "'s text starts inside the 2 cm margin (at "
              + std::to_string(ink.left) + ", " + std::to_string(ink.top) + ")");
        Check(ink.top < kMargin100 + 40, "and at the top of the text area, not further down");
    }
    Check(!pages->DrawPage(2, page).success, "a page past the end is refused");
}

void TestEditorStateDoesNotPrint() {
    std::cout << "\nWhat the screen shows around the document\n";
    auto editor = CreateRichTextEdit("Screen", 0, 0, 600, 400);
    editor->SetDocument(std::make_shared<UCRichDocument>(TwoPageDocument()));
    editor->SelectAll();
    auto pages = CreateRichDocumentPrintPages(*editor);
    Check(pages != nullptr, "the pages of an editor's document can be made");
    if (!pages) return;

    RasterPageTarget page(kA4Width100, kA4Height100, 100, 100);
    page.BeginPage();
    pages->Prepare(page);
    page.BeginPage();
    pages->DrawPage(0, page);
    const InkBox ink = MeasureInk(page);
    Check(ink.Any(), "prints the document");
    Check(!ink.selectionTint, "without the editor's selection");
    Check(editor->HasSelection(), "and leaves the selection on screen alone");

    // The copy is taken when the pages are made: a later edit is not printed.
    editor->GetDocument()->blocks.push_back(PageBreak());
    editor->GetDocument()->blocks.push_back(Paragraph("Typed while spooling"));
    page.BeginPage();
    pages->Prepare(page);
    Check(pages->GetPageCount() == 2, "an edit after the pages were made does not reach them");
}

void TestDrawnAsAnImage() {
    std::cout << "\nPages placed as images (a Windows printer DC)\n";
    // 600 dpi, a 1/12 inch unprintable margin all round: the printable area
    // of an A4 sheet.
    const int offset = 50;
    RecordingTarget printer(4961 - 2 * offset, 7016 - 2 * offset, 600, offset);
    auto pages = std::make_shared<RichDocumentPrintPages>(TwoPageDocument(), RichTextEditStyle());
    Check(pages->Prepare(printer).success, "a target with no render context is prepared");
    Check(pages->GetPageCount() == 2, "into the same two pages");
    Check(pages->DrawPage(1, printer).success, "a page draws");
    Check(printer.images == 1, "as one image");
    Check(printer.lastWidth <= 2481 && printer.lastWidth >= 2470,
          "drawn at 300 dpi, not the printer's 600 (" + std::to_string(printer.lastWidth) + " px wide)");
    Check(printer.lastDest.x == -offset && printer.lastDest.y == -offset,
          "placed past the unprintable margin, on the sheet's corner");
    Check(std::abs(printer.lastDest.width - 4961) <= 2, "and as wide as the sheet");
    Check(printer.inkPixels > 0, "with the text on it");
}

void TestLargerPageIsFitted() {
    std::cout << "\nA page larger than the sheet\n";
    UCRichDocument a3 = TwoPageDocument();
    a3.page.widthPt = 841.89f;
    a3.page.heightPt = 1190.55f;
    a3.page.marginTopPt = a3.page.marginBottomPt = a3.page.marginLeftPt = a3.page.marginRightPt = 56.69f;
    auto pages = std::make_shared<RichDocumentPrintPages>(a3, RichTextEditStyle());
    RasterPageTarget page(kA4Width100, kA4Height100, 100, 100);
    page.BeginPage();
    Check(pages->Prepare(page).success, "an A3 document is prepared for A4 paper");
    const RichDocumentPrintPages::Placement& at = pages->GetPlacement();
    Check(at.width <= kA4Width100 + 0.5 && at.height <= kA4Height100 + 0.5,
          "scaled down to fit the sheet");
    Check(at.width > kA4Width100 * 0.99, "and no further than it has to be");
}

void TestPrintedBesideItsPdf() {
    std::cout << "\nA job carrying the PDF and its pages\n";
    auto editor = CreateRichTextEdit("Screen", 0, 0, 600, 400);
    editor->SetDocument(std::make_shared<UCRichDocument>(TwoPageDocument()));
    editor->SelectAll();
    std::vector<uint8_t> pdf;
    std::string error;
    Check(editor->ExportToPdf(pdf, error), "the editor writes its PDF");
    Check(pdf.size() > 5 && std::string(pdf.begin(), pdf.begin() + 5) == "%PDF-", "which is a PDF");
    Check(!editor->IsPageView() && editor->HasSelection(),
          "and its view is put back afterwards, selection and all");

    IOPrintJob job;
    job.jobName = "Report";
    job.data = pdf;
    job.mimeType = "application/pdf";
    job.pages = CreateRichDocumentPrintPages(*editor);

    // CUPS's renderer passes the PDF through, as it did before pages existed.
    NativePrintRenderer native;
    IOPrintPayload payload;
    Check(native.Render(IODeviceInfo(), job, IOPrinterCapabilities(), payload).success &&
              payload.contentType == "application/pdf" && payload.data == pdf,
          "a document-taking renderer queues the PDF");

    // The renderers that draw get the pages, not a PDF they cannot lay out.
    IPrintPageSourcePtr source;
    std::string contentType;
    Check(MakePageSourceForJob(job, source, contentType).success && source == job.pages,
          "a page-drawing renderer gets the pages instead");
    IOPrintJob pdfOnly = job;
    pdfOnly.pages = nullptr;
    IPrintPageSourcePtr none;
    Check(!MakePageSourceForJob(pdfOnly, none, contentType).success,
          "  where the PDF alone is still refused, by name");

    IOPrintJob pagesOnly = job;
    pagesOnly.data.clear();
    pagesOnly.mimeType.clear();
    Check(pagesOnly.IsValid(), "pages alone make a valid job");
    IOPrintPayload empty;
    const IODeviceResult refused = native.Render(IODeviceInfo(), pagesOnly, IOPrinterCapabilities(), empty);
    Check(!refused.success && refused.code == IODeviceResultCode::NotSupported && empty.data.empty(),
          "  which a document-taking renderer refuses rather than queuing nothing");
}

} // namespace

int main() {
    std::cout << "RichTextPrintTest\n";
    TestDrawnIntoARenderContext();
    TestEditorStateDoesNotPrint();
    TestDrawnAsAnImage();
    TestLargerPageIsFitted();
    TestPrintedBesideItsPdf();
    std::cout << "\n" << (g_failures == 0 ? "All checks passed" : std::to_string(g_failures) + " check(s) failed")
              << "\n";
    return g_failures == 0 ? 0 : 1;
}
