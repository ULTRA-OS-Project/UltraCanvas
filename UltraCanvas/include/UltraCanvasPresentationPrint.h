// include/UltraCanvasPresentationPrint.h
// A deck's slides as pages, for a PDF and for printers: one page per slide,
// the slide's own size, drawn by DrawPptxSlide (UltraCanvasPptxSlideRender.h)
// - the same drawing as the editor, the thumbnails and the show. Hidden
// slides are left out unless asked for.
//
// It is an IPrintPageSource, like RichDocumentPrintPages and
// SpreadsheetPrintPages: a printer that draws gets each slide drawn on its own
// context, one that cannot an image of it.
//
// doc-check: PptxPresentation deck;
// doc-check: example
//     PresentationPrintPages pages(deck);
//     std::string error;
//     pages.ExportPdf("Talk.pdf", error);
//
// Version: 1.0.0
// Author: UltraCanvas Framework
#pragma once

#include "IODeviceManager/UltraCanvasIODevicePrinterPage.h"
#include "UltraCanvasPresentationPptx.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

class IRenderContext;
class RasterPageTarget;

class PresentationPrintPages : public IPrintPageSource {
public:
    // The deck is copied, so editing it while a job spools cannot change
    // what prints. Read it withPictures for the pictures.
    explicit PresentationPrintPages(const PptxPresentation& deck, bool includeHidden = false);
    ~PresentationPrintPages() override;

    PresentationPrintPages(const PresentationPrintPages&) = delete;
    PresentationPrintPages& operator=(const PresentationPrintPages&) = delete;

    int GetPageCount() const override { return static_cast<int>(slides_.size()); }
    // The slide (index into the deck) page `page` shows.
    int GetSlideOfPage(int page) const;
    // The page in points (the slide's size).
    double GetPageWidthPt() const { return widthPt_; }
    double GetPageHeightPt() const { return heightPt_; }

    // Page `index` drawn on `ctx` in points, from its top-left.
    void RenderPage(IRenderContext* ctx, int index) const;

    bool ExportPdf(const std::string& utf8Path, std::string& error);
    bool ExportPdf(std::vector<uint8_t>& out, std::string& error);

    // ===== IPrintPageSource =====
    IODeviceResult Prepare(IPrintPageTarget& target) override;
    IODeviceResult DrawPage(int pageIndex, IPrintPageTarget& target) override;

private:
    PptxPresentation deck_;
    std::vector<int> slides_;
    double widthPt_ = 0, heightPt_ = 0;
    double placeX_ = 0, placeY_ = 0, dotsPerPoint_ = 1;
    std::unique_ptr<RasterPageTarget> offscreen_;
    double offscreenScale_ = 1;
};

} // namespace UltraCanvas
