// include/Plugins/Documents/UltraCanvasPDFView.h
// UI element that displays a PDF document with a thumbnail strip,
// scrollable page render, and search-hit overlay.
// Version: 1.0.0
// Last Modified: 2026-05-18
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVAS_PDF_VIEW_H
#define ULTRACANVAS_PDF_VIEW_H

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasImage.h"
#include "Plugins/Documents/UltraCanvasPDF.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace UltraCanvas {

struct PDFViewStyle {
    Color background       = Color(48, 48, 48, 255);
    Color pageBackground   = Colors::White;
    Color pageShadowColor  = Color(0, 0, 0, 100);
    Color thumbStripBg     = Color(32, 32, 32, 255);
    Color thumbBorder      = Color(80, 80, 80, 255);
    Color thumbBorderActive = Color(70, 140, 220, 255);
    Color thumbLabelColor  = Color(200, 200, 200, 255);
    Color hitFill          = Color(255, 235, 59, 120);  // translucent yellow
    Color hitFillActive    = Color(255, 152, 0,   180);
    Color scrollbarTrack   = Color(40, 40, 40, 255);
    Color scrollbarThumb   = Color(140, 140, 140, 255);
    Color toolbarText      = Color(220, 220, 220, 255);

    int   thumbStripWidth   = 160;
    int   thumbHeight       = 180;
    int   thumbSpacing      = 8;
    int   pageMargin        = 24;
    int   pageShadowSize    = 4;
    int   scrollbarWidth    = 12;
    float defaultDpi        = 96.0f;
};

class UltraCanvasPDFView : public UltraCanvasUIElement {
public:
    UltraCanvasPDFView(const std::string& id = "PDFView",
                       int x = 0, int y = 0, int w = 600, int h = 800);
    ~UltraCanvasPDFView() override;

    // ----- Document -----
    void SetDocument(std::unique_ptr<IPDFDocument> doc);
    IPDFDocument* GetDocument() const { return doc_.get(); }
    bool LoadFromPath(const std::string& path, const std::string& password = "");
    bool HasDocument() const { return doc_ && doc_->IsOpen(); }

    // ----- Navigation -----
    void GoToPage(int page);     // 1-based
    void GoToNextPage();
    void GoToPrevPage();
    void GoToFirstPage() { GoToPage(1); }
    void GoToLastPage();
    int  GetCurrentPage() const { return currentPage_; }
    int  GetPageCount()   const;

    // ----- Zoom -----
    void  SetZoom(float zoom);   // 1.0 = fit-page
    float GetZoom() const { return userZoom_; }
    void  ZoomIn()   { SetZoom(userZoom_ * 1.25f); }
    void  ZoomOut()  { SetZoom(userZoom_ / 1.25f); }
    void  ZoomToFit() { SetZoom(1.0f); }

    // ----- Search -----
    int  SetSearchQuery(const std::string& query);  // returns hit count
    void ClearSearch();
    int  GetSearchHitCount() const { return static_cast<int>(hits_.size()); }
    void NextHit();
    void PrevHit();
    const std::string& GetSearchQuery() const { return query_; }

    // ----- Layout toggles -----
    void SetShowThumbnailStrip(bool show);
    bool GetShowThumbnailStrip() const { return showThumbs_; }

    // ----- Style -----
    void SetStyle(const PDFViewStyle& s);
    const PDFViewStyle& GetStyle() const { return style_; }

    // ----- Mutation passthroughs (delegate to IPDFDocument; redraw after) -----
    bool DeleteCurrentPage();
    bool MovePage(int fromPage, int toPage);
    bool InsertBlankPageAt(int at, float widthPt, float heightPt);
    bool SaveAs(const std::string& path, const PDFSaveOptions& opts = {});

    // ----- Content editing (M3) -----
    // Replace the text under `bboxPt` on the current page with `newText`.
    // bbox is in PDF user units (same coordinate space as PDFTextRun::bbox
    // returned by GetDocument()->ExtractTextRuns / Search).
    bool ReplaceTextAt(const Rect2Df& bboxPt, const std::string& newText);
    bool ApplyPendingRedactions();   // applies on the current page
    std::vector<PDFAnnotation> ListAnnotationsOnCurrentPage();
    bool DeleteAnnotation(int indexOnCurrentPage);

    // ----- Callbacks -----
    std::function<void(int currentPage, int totalPages)> onPageChanged;
    std::function<void(int hitCount)>                    onSearchResults;
    std::function<void(const std::string&)>              onError;
    std::function<void()>                                onDocumentChanged;

    // ----- UltraCanvasUIElement overrides -----
    void Render(IRenderContext* ctx, const Rect2Di& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;
    void SetBounds(const Rect2Di& b) override;

private:
    // ----- internal -----
    Rect2Di PageContentArea() const;
    Rect2Di ThumbStripArea()  const;
    Rect2Df ComputePageDrawRect(int pageW, int pageH,
                                const Rect2Di& contentArea) const;
    void   InvalidateCaches();
    void   FireDocumentChanged();
    void   FirePageChanged();
    std::shared_ptr<UCPixmapCairo> EnsurePageRendered(int page, float dpi);
    std::shared_ptr<UCPixmapCairo> EnsureThumbnail(int page);
    std::shared_ptr<UCPixmapCairo> MakePixmapFromRGBA(const PDFRenderedPage&);
    void   RecomputeFitDpi(int contentW, int contentH);
    void   DrawThumbStrip(IRenderContext* ctx, const Rect2Di& strip);
    void   DrawPageWithOverlays(IRenderContext* ctx, const Rect2Di& contentArea);
    int    HitTestThumb(const Point2Di& p) const;  // returns 1-based page or 0
    void   ScrollBy(int deltaX, int deltaY);
    void   ScrollThumbsBy(int delta);
    void   Repaint();

private:
    std::unique_ptr<IPDFDocument> doc_;
    PDFViewStyle style_;

    int     currentPage_ = 1;
    float   userZoom_    = 1.0f;
    float   fitDpi_      = 96.0f;       // dpi that fits current page in viewport
    int     scrollX_     = 0;
    int     scrollY_     = 0;
    int     thumbScroll_ = 0;

    bool    showThumbs_  = true;

    std::string                                            query_;
    std::vector<PDFTextRun>                                hits_;
    int                                                    activeHit_ = -1;

    // pageNumber → rendered pixmap. Keyed plain by page; on zoom/size change
    // we wipe the cache.
    std::unordered_map<int, std::shared_ptr<UCPixmapCairo>> pageCache_;
    std::unordered_map<int, std::shared_ptr<UCPixmapCairo>> thumbCache_;
    int     pageCacheDpiKey_ = 0;  // 0 = invalid

    // pan / drag-thumb state
    bool    panning_ = false;
    Point2Di panAnchor_;
    int     panScrollX_ = 0;
    int     panScrollY_ = 0;
};

inline std::shared_ptr<UltraCanvasPDFView> CreatePDFView(
        const std::string& id = "PDFView",
        int x = 0, int y = 0, int w = 600, int h = 800) {
    return std::make_shared<UltraCanvasPDFView>(id, x, y, w, h);
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_PDF_VIEW_H
