// include/Plugins/Documents/UltraCanvasPDFView.h
// UI element that displays a PDF document with a thumbnail strip,
// scrollable page render, and search-hit overlay.
// Version: 1.2.0
// Last Modified: 2026-06-19
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

class UltraCanvasMenu;   // for the built-in image context menu (popup)

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
                       float x = 0, float y = 0, float w = 600, float h = 800);
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
    // userZoom_ / effective zoom is an absolute scale where 1.0 == actual size
    // (100%). Fit modes recompute the effective scale from the viewport on every
    // render so the page keeps fitting when the view is resized.
    enum class ZoomMode { FitPage, FitWidth, Custom };

    void     SetZoom(float scale);            // absolute scale; switches to Custom
    float    GetZoom() const { return effectiveZoom_; }
    void     SetZoomMode(ZoomMode mode);
    ZoomMode GetZoomMode() const { return zoomMode_; }
    void     ZoomIn();
    void     ZoomOut();
    void     ZoomToFit()      { SetZoomMode(ZoomMode::FitPage); }   // fit whole page
    void     ZoomToWidth()    { SetZoomMode(ZoomMode::FitWidth); }  // fit page width
    void     ZoomActualSize() { SetZoom(1.0f); }                    // 100%
    // Effective on-screen scale as a percentage of actual size (valid after the
    // first render; fit modes resolve their scale during rendering).
    float    GetZoomPercent() const { return effectiveZoom_ * 100.0f; }

    // ----- Search -----
    int  SetSearchQuery(const std::string& query);  // returns hit count
    void ClearSearch();
    int  GetSearchHitCount() const { return static_cast<int>(hits_.size()); }
    void NextHit();
    void PrevHit();
    const std::string& GetSearchQuery() const { return query_; }

    // ----- Images -----
    // Images on the current page, in document order (delegates to the engine).
    std::vector<PDFImageRef> ImagesOnCurrentPage();
    // Index of the image under a point given in element-LOCAL coordinates, or
    // -1 if none. Topmost (last-drawn) image wins on overlap.
    int  ImageIndexAt(const Point2Di& localPt);
    // Extract image #indexOnPage of the current page to `path`. The engine
    // encodes extracted images as PNG, so `path` should end in ".png".
    bool ExtractImageToFile(int indexOnPage, const std::string& path);

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
    std::function<void(float zoomPercent)>               onZoomChanged;
    // Fired whenever the active search hit changes (1-based index, total hits).
    std::function<void(int activeHit, int totalHits)>    onActiveHitChanged;
    // Fired after the built-in "Extract Image" context-menu action completes.
    std::function<void(const std::string& path, bool ok)> onImageExtracted;

    // ----- UltraCanvasUIElement overrides -----
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;
    void SetBounds(const Rect2Df& b) override;

private:
    // ----- internal -----
    Rect2Di PageContentArea() const;
    Rect2Di ThumbStripArea()  const;
    Rect2Df ComputePageDrawRect(int pageW, int pageH,
                                const Rect2Di& contentArea) const;
    void   InvalidateCaches();
    void   FireDocumentChanged();
    void   FirePageChanged();
    void   FireZoomChanged();
    void   FireActiveHitChanged();
    // Effective scale (1.0 == actual size) that fits the page into contentW/H.
    // widthOnly fits to width; otherwise fits the whole page.
    float  ComputeFitScale(int contentW, int contentH, bool widthOnly) const;
    std::shared_ptr<UCPixmapCairo> EnsurePageRendered(int page, float dpi);
    std::shared_ptr<UCPixmapCairo> EnsureThumbnail(int page);
    std::shared_ptr<UCPixmapCairo> MakePixmapFromRGBA(const PDFRenderedPage&);
    void   DrawThumbStrip(IRenderContext* ctx, const Rect2Di& strip);
    void   DrawPageWithOverlays(IRenderContext* ctx, const Rect2Di& contentArea);
    int    HitTestThumb(const Point2Di& p) const;  // returns 1-based page or 0
    // Build + open the right-click context menu for the image at imageIndex.
    void   ShowImageContextMenu(int imageIndex, const Point2Di& windowPos);
    void   ScrollBy(int deltaX, int deltaY);
    void   ScrollThumbsBy(int delta);
    void   Repaint();

private:
    std::unique_ptr<IPDFDocument> doc_;
    PDFViewStyle style_;

    int      currentPage_ = 1;
    ZoomMode zoomMode_     = ZoomMode::FitPage;
    float    userZoom_     = 1.0f;      // custom scale (used when zoomMode_ == Custom)
    float    effectiveZoom_ = 1.0f;     // last applied scale (renderDpi / defaultDpi)
    int     scrollX_     = 0;
    int     scrollY_     = 0;
    int     thumbScroll_ = 0;

    bool    showThumbs_  = true;

    std::string                                            query_;
    std::vector<PDFTextRun>                                hits_;
    int                                                    activeHit_ = -1;

    // Page rect (element-local) of the page drawn in the last frame; used to map
    // a click back to PDF user units for image hit-testing.
    Rect2Df                                                pageRect_{};
    // Held while the image context menu popup is open.
    std::shared_ptr<UltraCanvasMenu>                       imageMenu_;

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
