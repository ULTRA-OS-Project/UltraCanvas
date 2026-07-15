// include/Plugins/Documents/UltraCanvasPDFView.h
// UI element that displays a PDF document with a thumbnail strip,
// scrollable page render, and search-hit overlay.
// Version: 1.6.0
// Last Modified: 2026-07-15
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
    // Large translucent page number drawn over each thumbnail (Overlay style).
    Color thumbOverlayNumberColor = Color(20, 20, 20, 90);
    float thumbOverlayNumberHeight = 0.30f;   // fraction of thumb height
    Color hitFill          = Color(255, 235, 59, 120);  // translucent yellow
    Color hitFillActive    = Color(255, 152, 0,   180);
    Color selectionFill    = Color(70, 130, 220, 90);   // text selection overlay
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
    // preserves the original image format, so prefer the extension matching
    // PDFImageRef::mimeType (the extraction context menu does this for you).
    bool ExtractImageToFile(int indexOnPage, const std::string& path);

    // ----- Text selection & export -----
    // Left-drag behaviour: Pan (default, scrolls the page) or SelectText
    // (character-level selection from the caret under the drag start to the
    // caret under the drag end).
    enum class MouseMode { Pan, SelectText };
    void      SetMouseMode(MouseMode m);
    MouseMode GetMouseMode() const { return mouseMode_; }

    bool        HasTextSelection() const { return hasSelection_; }
    // Selected text; a newline is inserted between characters on different lines.
    std::string GetSelectedText();
    void        ClearTextSelection();
    void        SelectAllText();            // selects all text on the current page
    bool        CopySelectionToClipboard(); // copies GetSelectedText(); false if empty
    std::string GetCurrentPageText();       // full text of the current page
    // Write text to `path`: the selection if selectionOnly, else the whole
    // current page. Returns true on success.
    bool        ExportTextToFile(const std::string& path, bool selectionOnly);

    // ----- Layout toggles -----
    void SetShowThumbnailStrip(bool show);
    bool GetShowThumbnailStrip() const { return showThumbs_; }

    // How the page number is shown on each thumbnail:
    //   Caption  - small label beneath the thumbnail (default)
    //   Overlay  - large translucent number centred over the page
    enum class ThumbnailNumberStyle { Caption, Overlay };
    void SetThumbnailNumberStyle(ThumbnailNumberStyle s);
    ThumbnailNumberStyle GetThumbnailNumberStyle() const { return thumbNumberStyle_; }

    // ----- Style -----
    void SetStyle(const PDFViewStyle& s);
    const PDFViewStyle& GetStyle() const { return style_; }

    // ----- Mutation passthroughs (delegate to IPDFDocument; redraw after) -----
    bool DeleteCurrentPage();
    bool MovePage(int fromPage, int toPage);
    bool InsertBlankPageAt(int at, float widthPt, float heightPt);
    bool SaveAs(const std::string& path, const PDFSaveOptions& opts = {});

    // Merge pages [srcStart..srcEnd] (1-based, inclusive) from `other` into the
    // current document so the first merged page lands at position `insertAt`.
    // srcEnd <= 0 means "through the last source page"; insertAt <= 0 means
    // "append after the last page". `other` is not modified or consumed. The
    // view navigates to the first merged page on success.
    bool MergeFromDocument(IPDFDocument& other, int srcStart = 1, int srcEnd = 0,
                           int insertAt = 0);
    // Convenience: open the PDF at `path` and merge a page range from it. The
    // opened document is released once its pages have been grafted in. Returns
    // false if the file cannot be opened or the merge fails.
    bool MergeFromFile(const std::string& path, int srcStart = 1, int srcEnd = 0,
                       int insertAt = 0, const std::string& password = "");

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
    // Fired as the text selection changes (number of selected characters).
    std::function<void(int selectedChars)>                onSelectionChanged;
    // Fired after a text export ("Export … Text") completes.
    std::function<void(const std::string& path, bool ok)> onTextExported;

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
    void   FireSelectionChanged();
    // Text-selection helpers (page units == PDF user units, top-left origin).
    void    EnsurePageChars();                                 // cache chars+lines of current page
    bool    LocalToPage(const Point2Di& local, Point2Df& outPage) const;
    // Caret position (0..N) nearest to a point in page units.
    int     CaretAtPage(const Point2Df& pagePt) const;
    void    PromptExportText(bool selectionOnly);              // save-dialog + write
    // Effective scale (1.0 == actual size) that fits the page into contentW/H.
    // widthOnly fits to width; otherwise fits the whole page.
    float  ComputeFitScale(int contentW, int contentH, bool widthOnly) const;
    std::shared_ptr<UCPixmapCairo> EnsurePageRendered(int page, float dpi);
    std::shared_ptr<UCPixmapCairo> EnsureThumbnail(int page);
    std::shared_ptr<UCPixmapCairo> MakePixmapFromRGBA(const PDFRenderedPage&);
    void   DrawThumbStrip(IRenderContext* ctx, const Rect2Di& strip);
    void   DrawPageWithOverlays(IRenderContext* ctx, const Rect2Di& contentArea);
    int    HitTestThumb(const Point2Di& p) const;  // returns 1-based page or 0
    // Build + open the right-click context menu. imageIndex >= 0 adds an
    // "Extract Image" item; text actions are added based on the selection.
    void   ShowContextMenu(int imageIndex, const Point2Di& windowPos);
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
    ThumbnailNumberStyle thumbNumberStyle_ = ThumbnailNumberStyle::Caption;

    std::string                                            query_;
    std::vector<PDFTextRun>                                hits_;
    int                                                    activeHit_ = -1;

    // Page rect (element-local) of the page drawn in the last frame; used to map
    // a click back to PDF user units for image hit-testing.
    Rect2Df                                                pageRect_{};
    // Held while the right-click context menu popup is open.
    std::shared_ptr<UltraCanvasMenu>                       imageMenu_;

    // ----- Text selection (caret-level) -----
    // A run of characters on one line; [first, last] are inclusive indices into
    // pageChars_, and [y0, y1] is the line's vertical extent in page units.
    struct CharLine { int first; int last; float y0; float y1; };

    MouseMode mouseMode_    = MouseMode::Pan;
    bool      selecting_    = false;       // a drag-select is in progress
    bool      hasSelection_ = false;
    int       selPage_      = 0;           // page the selection belongs to
    int       selAnchorChar_ = 0;          // caret where the drag started (0..N)
    int       selCaretChar_  = 0;          // caret at the drag's current point
    std::vector<PDFTextChar> pageChars_;   // cached chars for pageCharsPage_
    std::vector<CharLine>    pageLines_;   // line ranges over pageChars_
    int       pageCharsPage_ = -1;

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
