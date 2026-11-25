// core/UltraCanvasPresentationPrint.cpp
// A deck's slides as pages (include/UltraCanvasPresentationPrint.h).
//
// Version: 1.0.0
// Author: UltraCanvas Framework
#include "UltraCanvasPresentationPrint.h"

#include "IODeviceManager/UltraCanvasIODevicePrinterRasterTarget.h"
#include "UltraCanvasPdfSurface.h"
#include "UltraCanvasPptxSlideRender.h"
#include "UltraCanvasRenderContext.h"

#include <algorithm>
#include <cmath>

namespace UltraCanvas {

namespace {

constexpr double kEmuPerPoint = 12700.0;
constexpr double kPointsPerInch = 72.0;
constexpr double kMaxOffscreenDpi = 300.0;
constexpr int64_t kDefaultSlideWidth = 12192000;
constexpr int64_t kDefaultSlideHeight = 6858000;

} // namespace

PresentationPrintPages::PresentationPrintPages(const PptxPresentation& deck, bool includeHidden) : deck_(deck) {
    widthPt_ = static_cast<double>(deck_.slideWidth > 0 ? deck_.slideWidth : kDefaultSlideWidth) / kEmuPerPoint;
    heightPt_ = static_cast<double>(deck_.slideHeight > 0 ? deck_.slideHeight : kDefaultSlideHeight) / kEmuPerPoint;
    for (size_t i = 0; i < deck_.slides.size(); ++i)
        if (includeHidden || !deck_.slides[i].hidden) slides_.push_back(static_cast<int>(i));
}

PresentationPrintPages::~PresentationPrintPages() = default;

int PresentationPrintPages::GetSlideOfPage(int page) const {
    return page >= 0 && page < GetPageCount() ? slides_[static_cast<size_t>(page)] : -1;
}

void PresentationPrintPages::RenderPage(IRenderContext* ctx, int index) const {
    const int slide = GetSlideOfPage(index);
    if (!ctx || slide < 0) return;
    DrawPptxSlide(ctx, deck_, deck_.slides[static_cast<size_t>(slide)], Rect2Dd(0, 0, widthPt_, heightPt_));
}

bool PresentationPrintPages::ExportPdf(const std::string& utf8Path, std::string& error) {
    if (GetPageCount() == 0) { error = "The presentation has no slides to print."; return false; }
    auto pdf = UltraCanvasPdfSurface::CreateForFile(utf8Path, widthPt_, heightPt_, error);
    if (!pdf) return false;
    for (int i = 0; i < GetPageCount(); ++i) {
        if (i > 0) pdf->NextPage();
        RenderPage(pdf->GetContext(), i);
    }
    return pdf->Finish(error);
}

bool PresentationPrintPages::ExportPdf(std::vector<uint8_t>& out, std::string& error) {
    if (GetPageCount() == 0) { error = "The presentation has no slides to print."; return false; }
    auto pdf = UltraCanvasPdfSurface::CreateInMemory(widthPt_, heightPt_, error);
    if (!pdf) return false;
    for (int i = 0; i < GetPageCount(); ++i) {
        if (i > 0) pdf->NextPage();
        RenderPage(pdf->GetContext(), i);
    }
    if (!pdf->Finish(error)) return false;
    out = pdf->GetBytes();
    return true;
}

IODeviceResult PresentationPrintPages::Prepare(IPrintPageTarget& target) {
    offscreen_.reset();
    const IOPrintPageMetrics metrics = target.GetMetrics();
    if (!metrics.IsValid())
        return IODeviceResult::Error(IODeviceResultCode::InvalidArgument, "The printer reported no usable page size");
    if (GetPageCount() == 0)
        return IODeviceResult::Error(IODeviceResultCode::InvalidArgument, "The presentation has no slides to print");
    // The slide as large as the sheet of paper takes, centred on it.
    const double sheetWidth = metrics.widthDots + 2.0 * metrics.offsetXDots;
    const double sheetHeight = metrics.heightDots + 2.0 * metrics.offsetYDots;
    dotsPerPoint_ = std::min(sheetWidth / widthPt_, sheetHeight / heightPt_);
    placeX_ = -metrics.offsetXDots + (sheetWidth - widthPt_ * dotsPerPoint_) / 2.0;
    placeY_ = -metrics.offsetYDots + (sheetHeight - heightPt_ * dotsPerPoint_) / 2.0;
    if (!target.GetRenderContext()) {
        const double dpi = std::min(std::min(metrics.dpiX, metrics.dpiY) * 1.0, kMaxOffscreenDpi);
        offscreenScale_ = dpi / kPointsPerInch;
        const int width = std::max(1, static_cast<int>(std::ceil(widthPt_ * offscreenScale_)));
        const int height = std::max(1, static_cast<int>(std::ceil(heightPt_ * offscreenScale_)));
        offscreen_ = std::make_unique<RasterPageTarget>(width, height, static_cast<int>(dpi), static_cast<int>(dpi));
        if (!offscreen_->IsValid()) {
            offscreen_.reset();
            return IODeviceResult::Error(IODeviceResultCode::BackendError, "Could not create a page to draw the slide on");
        }
    }
    return IODeviceResult::Ok();
}

IODeviceResult PresentationPrintPages::DrawPage(int pageIndex, IPrintPageTarget& target) {
    if (GetSlideOfPage(pageIndex) < 0)
        return IODeviceResult::Error(IODeviceResultCode::InvalidArgument, "There is no such page in this presentation");
    if (offscreen_) {
        offscreen_->BeginPage();
        IRenderContext* ctx = offscreen_->GetRenderContext();
        ctx->PushState();
        ctx->Scale(offscreenScale_, offscreenScale_);
        RenderPage(ctx, pageIndex);
        ctx->PopState();
        const IOPrintPageMetrics drawn = offscreen_->GetMetrics();
        std::vector<uint8_t> pixels;
        pixels.reserve(static_cast<size_t>(drawn.widthDots) * static_cast<size_t>(drawn.heightDots) * 4u);
        offscreen_->ForEachRow([&pixels](const uint8_t* rgba, int width) {
            pixels.insert(pixels.end(), rgba, rgba + static_cast<size_t>(width) * 4u);
            return true;
        });
        IOPrintRect dest;
        dest.x = static_cast<int>(std::lround(placeX_));
        dest.y = static_cast<int>(std::lround(placeY_));
        dest.width = static_cast<int>(std::lround(widthPt_ * dotsPerPoint_));
        dest.height = static_cast<int>(std::lround(heightPt_ * dotsPerPoint_));
        if (!target.DrawImage(pixels.data(), drawn.widthDots, drawn.heightDots, dest))
            return IODeviceResult::Error(IODeviceResultCode::IOError, "The printer did not take a page");
        return IODeviceResult::Ok();
    }
    IRenderContext* ctx = target.GetRenderContext();
    if (!ctx) return IODeviceResult::Error(IODeviceResultCode::InvalidArgument, "The page was prepared for another target");
    ctx->PushState();
    ctx->Translate(placeX_, placeY_);
    ctx->Scale(dotsPerPoint_, dotsPerPoint_);
    RenderPage(ctx, pageIndex);
    ctx->PopState();
    return IODeviceResult::Ok();
}

} // namespace UltraCanvas
