// core/UltraCanvasRichTextPrint.cpp
// A rich document's pages, drawn for a printer that cannot take its PDF.
// Version: 1.0.0
// Author: UltraCanvas Framework

#include "UltraCanvasRichTextPrint.h"

#include "IODeviceManager/UltraCanvasIODevicePrinterRasterTarget.h"
#include "UltraCanvasRenderContext.h"

#include <string>
#include <vector>

#include <algorithm>
#include <cmath>

namespace UltraCanvas {

namespace {

// The element draws at 96 pixels to the inch; documents measure in points.
constexpr double kPixelsPerPoint = 96.0 / 72.0;
constexpr double kViewDpi = 96.0;

// A page drawn off screen for a device that cannot be drawn on directly is
// sharp on paper at 300 dpi, and a 600 dpi A4 page would be 140 MB of pixels
// for no visible gain in text that the driver halftones anyway.
constexpr int kMaxOffscreenDpi = 300;

// How much larger than the sheet a page may be and still print 1:1: a page
// and a sheet of the "same" size differ by rounding, and scaling an A4
// document by 0.998 for that would blur every line of it.
constexpr double kFitTolerance = 1.02;

} // namespace

RichDocumentPrintPages::RichDocumentPrintPages(const UCRichDocument& document,
                                               const RichTextEditStyle& style) {
    layout = std::make_shared<UltraCanvasRichTextEdit>("RichDocumentPrintPages", 0, 0, 1, 1);
    layout->SetStyle(style);
    layout->SetDocument(std::make_shared<UCRichDocument>(document));
}

RichDocumentPrintPages::~RichDocumentPrintPages() = default;

// ===== ON A DEVICE =====

IODeviceResult RichDocumentPrintPages::Prepare(IPrintPageTarget& target) {
    pageCount = 0;
    offscreen.reset();

    const IOPrintPageMetrics metrics = target.GetMetrics();
    if (!metrics.IsValid()) {
        return IODeviceResult::Error(IODeviceResultCode::InvalidArgument,
                                     "The printer reported no usable page size");
    }

    // Where the document's page goes on the sheet. The sheet is the printable
    // area plus its unprintable margin, taken as the same on both sides - the
    // only thing a device context reports.
    const RichPageSetup page = layout->GetEffectivePageSetup();
    const double pageWidthPx = page.widthPt * kPixelsPerPoint;
    const double pageHeightPx = page.heightPt * kPixelsPerPoint;
    const double dotsPerPxX = metrics.dpiX / kViewDpi;
    const double dotsPerPxY = metrics.dpiY / kViewDpi;
    const double sheetWidth = metrics.widthDots + 2.0 * metrics.offsetXDots;
    const double sheetHeight = metrics.heightDots + 2.0 * metrics.offsetYDots;
    double fit = 1.0;
    if (pageWidthPx * dotsPerPxX > sheetWidth * kFitTolerance ||
        pageHeightPx * dotsPerPxY > sheetHeight * kFitTolerance) {
        fit = std::min(sheetWidth / (pageWidthPx * dotsPerPxX),
                       sheetHeight / (pageHeightPx * dotsPerPxY));
    }
    placement.scaleX = dotsPerPxX * fit;
    placement.scaleY = dotsPerPxY * fit;
    placement.width = pageWidthPx * placement.scaleX;
    placement.height = pageHeightPx * placement.scaleY;
    // Centred on the sheet, which for a page the sheet's size is exactly its
    // edges; never pushed off the sheet's top-left by a page that overhangs
    // it within the tolerance.
    placement.x = -metrics.offsetXDots + std::max(0.0, (sheetWidth - placement.width) * 0.5);
    placement.y = -metrics.offsetYDots + std::max(0.0, (sheetHeight - placement.height) * 0.5);

    // Laid out on the context the pages will be drawn on, so the fonts that
    // measured the lines are the ones that draw them.
    IRenderContext* ctx = target.GetRenderContext();
    if (!ctx) {
        const double dpiX = std::min(metrics.dpiX, kMaxOffscreenDpi);
        const double dpiY = std::min(metrics.dpiY, kMaxOffscreenDpi);
        offscreenScale = std::min(dpiX, dpiY) / kViewDpi;
        const int width = std::max(1, static_cast<int>(std::ceil(pageWidthPx * offscreenScale)));
        const int height = std::max(1, static_cast<int>(std::ceil(pageHeightPx * offscreenScale)));
        const int dpi = static_cast<int>(std::lround(offscreenScale * kViewDpi));
        offscreen = std::make_unique<RasterPageTarget>(width, height, dpi, dpi);
        if (!offscreen->IsValid()) {
            offscreen.reset();
            return IODeviceResult::Error(IODeviceResultCode::BackendError,
                                         "Could not create a page to draw the document on");
        }
        ctx = offscreen->GetRenderContext();
    }

    pageCount = layout->BeginPrintLayout(ctx);
    if (pageCount <= 0) {
        return IODeviceResult::Error(IODeviceResultCode::InvalidArgument,
                                     "The document has no pages to print");
    }
    return IODeviceResult::Ok();
}

IODeviceResult RichDocumentPrintPages::DrawPage(int pageIndex, IPrintPageTarget& target) {
    if (pageIndex < 0 || pageIndex >= pageCount) {
        return IODeviceResult::Error(IODeviceResultCode::InvalidArgument,
                                     "There is no page " + std::to_string(pageIndex + 1) +
                                     " in this document");
    }
    if (offscreen) return DrawPageAsImage(pageIndex, target);

    IRenderContext* ctx = target.GetRenderContext();
    if (!ctx) {
        return IODeviceResult::Error(IODeviceResultCode::InvalidArgument,
                                     "The page was prepared for another target");
    }
    ctx->PushState();
    ctx->Translate(placement.x, placement.y);
    ctx->Scale(placement.scaleX, placement.scaleY);
    layout->RenderPrintPage(ctx, pageIndex);
    ctx->PopState();
    return IODeviceResult::Ok();
}

IODeviceResult RichDocumentPrintPages::DrawPageAsImage(int pageIndex, IPrintPageTarget& target) {
    offscreen->BeginPage();
    IRenderContext* ctx = offscreen->GetRenderContext();
    ctx->PushState();
    ctx->Scale(offscreenScale, offscreenScale);
    layout->RenderPrintPage(ctx, pageIndex);
    ctx->PopState();

    const IOPrintPageMetrics drawn = offscreen->GetMetrics();
    std::vector<uint8_t> pixels;
    pixels.reserve(static_cast<size_t>(drawn.widthDots) * static_cast<size_t>(drawn.heightDots) * 4u);
    offscreen->ForEachRow([&pixels](const uint8_t* rgba, int width) {
        pixels.insert(pixels.end(), rgba, rgba + static_cast<size_t>(width) * 4u);
        return true;
    });

    IOPrintRect dest;
    dest.x = static_cast<int>(std::lround(placement.x));
    dest.y = static_cast<int>(std::lround(placement.y));
    dest.width = static_cast<int>(std::lround(placement.width));
    dest.height = static_cast<int>(std::lround(placement.height));
    if (!target.DrawImage(pixels.data(), drawn.widthDots, drawn.heightDots, dest)) {
        return IODeviceResult::Error(IODeviceResultCode::IOError,
                                     "The printer did not take page " + std::to_string(pageIndex + 1));
    }
    return IODeviceResult::Ok();
}

// ===== FACTORY =====

std::shared_ptr<IPrintPageSource> CreateRichDocumentPrintPages(
        const UltraCanvasRichTextEdit& editor) {
    const std::shared_ptr<UCRichDocument>& document = editor.GetDocument();
    if (!document) return nullptr;
    return std::make_shared<RichDocumentPrintPages>(*document, editor.GetStyle());
}

} // namespace UltraCanvas
