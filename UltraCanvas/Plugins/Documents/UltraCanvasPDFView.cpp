// Plugins/Documents/UltraCanvasPDFView.cpp
// UI element rendering a PDF document via the IPDFDocument backend.
// Version: 1.0.0
// Last Modified: 2026-05-18
// Author: UltraCanvas Framework

#include "Plugins/Documents/UltraCanvasPDFView.h"

#ifdef ULTRACANVAS_PLUGIN_PDF

#include "libspecific/Cairo/ImageCairo.h"  // UCPixmapCairo

#include <algorithm>
#include <cstring>

namespace UltraCanvas {

// ===== ctor / dtor =====

UltraCanvasPDFView::UltraCanvasPDFView(const std::string& id,
                                       int x, int y, int w, int h)
    : UltraCanvasUIElement(id, x, y, w, h) {
    backgroundColor = style_.background;
}

UltraCanvasPDFView::~UltraCanvasPDFView() = default;

// ===== Document =====

void UltraCanvasPDFView::SetDocument(std::unique_ptr<IPDFDocument> doc) {
    doc_ = std::move(doc);
    currentPage_ = 1;
    scrollX_ = scrollY_ = thumbScroll_ = 0;
    userZoom_ = 1.0f;
    query_.clear();
    hits_.clear();
    activeHit_ = -1;
    InvalidateCaches();
    FireDocumentChanged();
    FirePageChanged();
    Repaint();
}

bool UltraCanvasPDFView::LoadFromPath(const std::string& path,
                                      const std::string& password) {
    auto d = PDFEngineFactory::Create(PDFEngineKind::Auto);
    if (!d) {
        if (onError) onError("No PDF engine available");
        return false;
    }
    if (!d->Open(path, password)) {
        if (onError) onError("Failed to open PDF: " + path);
        return false;
    }
    SetDocument(std::move(d));
    return true;
}

int UltraCanvasPDFView::GetPageCount() const {
    return doc_ ? doc_->GetPageCount() : 0;
}

// ===== Navigation =====

void UltraCanvasPDFView::GoToPage(int page) {
    if (!doc_) return;
    const int total = doc_->GetPageCount();
    if (total <= 0) return;
    page = std::clamp(page, 1, total);
    if (page == currentPage_) return;
    currentPage_ = page;
    scrollX_ = scrollY_ = 0;
    FirePageChanged();
    Repaint();
}

void UltraCanvasPDFView::GoToNextPage() { GoToPage(currentPage_ + 1); }
void UltraCanvasPDFView::GoToPrevPage() { GoToPage(currentPage_ - 1); }
void UltraCanvasPDFView::GoToLastPage() {
    if (doc_) GoToPage(doc_->GetPageCount());
}

// ===== Zoom =====

void UltraCanvasPDFView::SetZoom(float zoom) {
    zoom = std::clamp(zoom, 0.1f, 16.0f);
    if (std::abs(zoom - userZoom_) < 0.001f) return;
    userZoom_ = zoom;
    InvalidateCaches();      // page cache keyed by dpi; zoom changes dpi.
    Repaint();
}

// ===== Search =====

int UltraCanvasPDFView::SetSearchQuery(const std::string& query) {
    query_ = query;
    hits_.clear();
    activeHit_ = -1;
    if (doc_ && !query.empty()) {
        PDFSearchOptions opts;
        opts.maxHits = 5000;
        hits_ = doc_->Search(query, opts);
        if (!hits_.empty()) {
            activeHit_ = 0;
            GoToPage(hits_[0].pageNumber);
        }
    }
    if (onSearchResults) onSearchResults(static_cast<int>(hits_.size()));
    Repaint();
    return static_cast<int>(hits_.size());
}

void UltraCanvasPDFView::ClearSearch() {
    query_.clear();
    hits_.clear();
    activeHit_ = -1;
    if (onSearchResults) onSearchResults(0);
    Repaint();
}

void UltraCanvasPDFView::NextHit() {
    if (hits_.empty()) return;
    activeHit_ = (activeHit_ + 1) % static_cast<int>(hits_.size());
    GoToPage(hits_[activeHit_].pageNumber);
    Repaint();
}

void UltraCanvasPDFView::PrevHit() {
    if (hits_.empty()) return;
    activeHit_ = (activeHit_ - 1 + static_cast<int>(hits_.size()))
               % static_cast<int>(hits_.size());
    GoToPage(hits_[activeHit_].pageNumber);
    Repaint();
}

// ===== Layout toggles =====

void UltraCanvasPDFView::SetShowThumbnailStrip(bool show) {
    if (show == showThumbs_) return;
    showThumbs_ = show;
    InvalidateCaches();   // page area changed → fit dpi changed
    Repaint();
}

void UltraCanvasPDFView::SetStyle(const PDFViewStyle& s) {
    style_ = s;
    backgroundColor = style_.background;
    InvalidateCaches();
    Repaint();
}

// ===== Mutation passthroughs =====

bool UltraCanvasPDFView::DeleteCurrentPage() {
    if (!doc_) return false;
    const int total = doc_->GetPageCount();
    if (total <= 1) return false;
    if (!doc_->DeletePage(currentPage_)) return false;
    if (currentPage_ > doc_->GetPageCount()) currentPage_ = doc_->GetPageCount();
    InvalidateCaches();
    FirePageChanged();
    Repaint();
    return true;
}

bool UltraCanvasPDFView::MovePage(int fromPage, int toPage) {
    if (!doc_) return false;
    if (!doc_->MovePage(fromPage, toPage)) return false;
    if (currentPage_ == fromPage) currentPage_ = toPage;
    InvalidateCaches();
    FirePageChanged();
    Repaint();
    return true;
}

bool UltraCanvasPDFView::InsertBlankPageAt(int at, float wpt, float hpt) {
    if (!doc_) return false;
    if (!doc_->InsertBlankPage(at, wpt, hpt)) return false;
    InvalidateCaches();
    GoToPage(at);
    return true;
}

bool UltraCanvasPDFView::SaveAs(const std::string& path,
                                const PDFSaveOptions& opts) {
    if (!doc_) return false;
    return doc_->Save(path, opts);
}

// ===== Geometry =====

Rect2Di UltraCanvasPDFView::ThumbStripArea() const {
    Rect2Di b = GetBounds();
    if (!showThumbs_) return Rect2Di(b.x, b.y, 0, 0);
    return Rect2Di(b.x, b.y, style_.thumbStripWidth, b.height);
}

Rect2Di UltraCanvasPDFView::PageContentArea() const {
    Rect2Di b = GetBounds();
    int left = b.x + (showThumbs_ ? style_.thumbStripWidth : 0);
    return Rect2Di(left, b.y,
                   b.width - (showThumbs_ ? style_.thumbStripWidth : 0),
                   b.height);
}

Rect2Df UltraCanvasPDFView::ComputePageDrawRect(int pageW, int pageH,
                                                const Rect2Di& contentArea) const {
    // Centered, with style_.pageMargin padding.
    const int innerW = std::max(1, contentArea.width  - 2 * style_.pageMargin);
    const int innerH = std::max(1, contentArea.height - 2 * style_.pageMargin);
    const float x = contentArea.x + style_.pageMargin +
                    (innerW - pageW) * 0.5f - scrollX_;
    const float y = contentArea.y + style_.pageMargin +
                    (innerH - pageH) * 0.5f - scrollY_;
    return Rect2Df(x, y, pageW, pageH);
}

void UltraCanvasPDFView::RecomputeFitDpi(int contentW, int contentH) {
    if (!doc_) { fitDpi_ = style_.defaultDpi; return; }
    PDFPageInfo pi = doc_->GetPageInfo(currentPage_);
    if (pi.widthPt <= 0 || pi.heightPt <= 0) {
        fitDpi_ = style_.defaultDpi;
        return;
    }
    const float availW = std::max(1, contentW - 2 * style_.pageMargin);
    const float availH = std::max(1, contentH - 2 * style_.pageMargin);
    const float dpiW = (availW * 72.0f) / pi.widthPt;
    const float dpiH = (availH * 72.0f) / pi.heightPt;
    fitDpi_ = std::min(dpiW, dpiH);
}

// ===== Caching =====

void UltraCanvasPDFView::InvalidateCaches() {
    pageCache_.clear();
    pageCacheDpiKey_ = 0;
    // Thumbnails are page-only (fixed size), so they survive zoom changes.
}

std::shared_ptr<UCPixmapCairo>
UltraCanvasPDFView::MakePixmapFromRGBA(const PDFRenderedPage& rp) {
    if (!rp.IsValid()) return {};
    auto pm = std::make_shared<UCPixmapCairo>();
    if (!pm->Init(rp.width, rp.height)) return {};

    // MuPDF gave us non-premul RGBA, byte order R,G,B,A.
    // Cairo ARGB32 little-endian = byte order B,G,R,A premultiplied.
    uint32_t* dst = pm->GetPixelData();
    if (!dst) return {};
    const uint8_t* src = rp.pixels.data();
    const int dstStride = rp.width;   // pixels per row
    for (int y = 0; y < rp.height; ++y) {
        for (int x = 0; x < rp.width; ++x) {
            const uint8_t r = src[0];
            const uint8_t g = src[1];
            const uint8_t b = src[2];
            const uint8_t a = src[3];
            const uint16_t pr = (uint16_t(r) * a + 127) / 255;
            const uint16_t pg = (uint16_t(g) * a + 127) / 255;
            const uint16_t pb = (uint16_t(b) * a + 127) / 255;
            dst[y * dstStride + x] =
                (uint32_t(a)  << 24) |
                (uint32_t(pr) << 16) |
                (uint32_t(pg) << 8 ) |
                (uint32_t(pb)      );
            src += 4;
        }
        // Skip any per-row stride padding from the source.
        src += (rp.stride - rp.width * 4);
    }
    pm->MarkDirty();
    return pm;
}

std::shared_ptr<UCPixmapCairo>
UltraCanvasPDFView::EnsurePageRendered(int page, float dpi) {
    if (!doc_) return {};
    const int dpiKey = static_cast<int>(dpi * 100.0f + 0.5f);
    if (pageCacheDpiKey_ != dpiKey) {
        pageCache_.clear();
        pageCacheDpiKey_ = dpiKey;
    }
    auto it = pageCache_.find(page);
    if (it != pageCache_.end()) return it->second;

    PDFRenderSettings s;
    s.dpi = dpi;
    s.zoom = 1.0f;
    s.antialias = true;
    s.colorMode = PDFColorMode::RGBA;
    PDFRenderedPage rp = doc_->RenderPage(page, s);
    auto pm = MakePixmapFromRGBA(rp);
    if (pm) pageCache_[page] = pm;
    return pm;
}

std::shared_ptr<UCPixmapCairo>
UltraCanvasPDFView::EnsureThumbnail(int page) {
    if (!doc_) return {};
    auto it = thumbCache_.find(page);
    if (it != thumbCache_.end()) return it->second;
    PDFRenderedPage rp = doc_->RenderThumbnail(page, style_.thumbHeight);
    auto pm = MakePixmapFromRGBA(rp);
    if (pm) thumbCache_[page] = pm;
    return pm;
}

// ===== Render =====

void UltraCanvasPDFView::Render(IRenderContext* ctx, const Rect2Di& /*dirty*/) {
    if (!IsVisible()) return;
    const Rect2Di b = GetBounds();

    ctx->PushState();
    ctx->SetFillPaint(style_.background);
    ctx->FillRectangle(b);

    if (showThumbs_) {
        DrawThumbStrip(ctx, ThumbStripArea());
    }
    DrawPageWithOverlays(ctx, PageContentArea());
    ctx->PopState();
}

void UltraCanvasPDFView::DrawThumbStrip(IRenderContext* ctx,
                                        const Rect2Di& strip) {
    ctx->PushState();
    ctx->ClipRect(strip);
    ctx->SetFillPaint(style_.thumbStripBg);
    ctx->FillRectangle(strip);

    if (!doc_) { ctx->PopState(); return; }
    const int total = doc_->GetPageCount();
    const int innerW = strip.width - 16;
    int y = strip.y + style_.thumbSpacing - thumbScroll_;

    ctx->SetFontSize(11.0f);
    for (int p = 1; p <= total; ++p) {
        Rect2Df slot(strip.x + 8, y, innerW, style_.thumbHeight);

        // Skip thumbs that are entirely outside the visible strip.
        if (slot.y + slot.height + 20 < strip.y ||
            slot.y > strip.y + strip.height) {
            y += style_.thumbHeight + style_.thumbSpacing + 16;
            continue;
        }

        // Slot background
        const bool active = (p == currentPage_);
        ctx->SetFillPaint(style_.pageBackground);
        ctx->FillRectangle(slot);

        // Page thumbnail
        auto pm = EnsureThumbnail(p);
        if (pm && pm->IsValid()) {
            ctx->DrawPixmap(*pm, slot, ImageFitMode::Contain);
        }

        // Border (active or normal)
        ctx->SetStrokePaint(active ? style_.thumbBorderActive
                                   : style_.thumbBorder);
        ctx->SetStrokeWidth(active ? 2.0 : 1.0);
        ctx->DrawRectangle(slot);

        // Page number label
        ctx->SetFillPaint(style_.thumbLabelColor);
        ctx->DrawText(std::to_string(p),
                      Point2Df(slot.x + 4, slot.y + slot.height + 12));

        y += style_.thumbHeight + style_.thumbSpacing + 16;
    }
    ctx->PopState();
}

void UltraCanvasPDFView::DrawPageWithOverlays(IRenderContext* ctx,
                                              const Rect2Di& area) {
    ctx->PushState();
    ctx->ClipRect(area);

    if (!doc_) {
        ctx->SetFillPaint(Color(180, 180, 180, 255));
        ctx->DrawText("No document loaded",
                      Point2Df(area.x + 24, area.y + 24));
        ctx->PopState();
        return;
    }
    const int total = doc_->GetPageCount();
    if (total <= 0) {
        ctx->SetFillPaint(Color(180, 180, 180, 255));
        ctx->DrawText("Empty document", Point2Df(area.x + 24, area.y + 24));
        ctx->PopState();
        return;
    }

    RecomputeFitDpi(area.width, area.height);
    const float renderDpi = std::max(8.0f, fitDpi_ * userZoom_);
    auto pm = EnsurePageRendered(currentPage_, renderDpi);
    if (!pm || !pm->IsValid()) {
        ctx->SetFillPaint(Color(220, 80, 80, 255));
        ctx->DrawText("Failed to render page " + std::to_string(currentPage_),
                      Point2Df(area.x + 24, area.y + 24));
        ctx->PopState();
        return;
    }

    const Rect2Df pageRect = ComputePageDrawRect(pm->GetWidth(),
                                                 pm->GetHeight(), area);

    // Drop shadow
    ctx->SetFillPaint(style_.pageShadowColor);
    ctx->FillRectangle(Rect2Df(pageRect.x + style_.pageShadowSize,
                               pageRect.y + style_.pageShadowSize,
                               pageRect.width, pageRect.height));
    // White page underlay (in case the rendered pixmap has transparency)
    ctx->SetFillPaint(style_.pageBackground);
    ctx->FillRectangle(pageRect);
    // The page
    ctx->DrawPixmap(*pm, pageRect, ImageFitMode::Fill);

    // Search hits
    if (!hits_.empty()) {
        PDFPageInfo pi = doc_->GetPageInfo(currentPage_);
        if (pi.widthPt > 0 && pi.heightPt > 0) {
            const float sx = pageRect.width  / pi.widthPt;
            const float sy = pageRect.height / pi.heightPt;
            int idx = 0;
            for (const auto& h : hits_) {
                if (h.pageNumber != currentPage_) { ++idx; continue; }
                const Rect2Df r(
                    pageRect.x + h.bbox.x * sx,
                    pageRect.y + h.bbox.y * sy,
                    h.bbox.width  * sx,
                    h.bbox.height * sy);
                ctx->SetFillPaint(idx == activeHit_ ? style_.hitFillActive
                                                    : style_.hitFill);
                ctx->FillRectangle(r);
                ++idx;
            }
        }
    }

    // Page number indicator
    ctx->SetFillPaint(Color(0, 0, 0, 160));
    Rect2Df badge(area.x + area.width - 90, area.y + 12, 80, 24);
    ctx->FillRoundedRectangle(badge, 4.0);
    ctx->SetFillPaint(Color(255, 255, 255, 255));
    ctx->SetFontSize(12);
    ctx->DrawText(std::to_string(currentPage_) + " / " + std::to_string(total),
                  Point2Df(badge.x + 16, badge.y + 16));

    ctx->PopState();
}

// ===== Events =====

int UltraCanvasPDFView::HitTestThumb(const Point2Di& p) const {
    if (!showThumbs_ || !doc_) return 0;
    Rect2Di strip = ThumbStripArea();
    if (!strip.Contains(p)) return 0;
    const int total = doc_->GetPageCount();
    int y = strip.y + style_.thumbSpacing - thumbScroll_;
    for (int i = 1; i <= total; ++i) {
        Rect2Di slot(strip.x + 8, y, strip.width - 16, style_.thumbHeight);
        if (slot.Contains(p)) return i;
        y += style_.thumbHeight + style_.thumbSpacing + 16;
    }
    return 0;
}

void UltraCanvasPDFView::ScrollBy(int dx, int dy) {
    scrollX_ += dx;
    scrollY_ += dy;
    Repaint();
}

void UltraCanvasPDFView::ScrollThumbsBy(int delta) {
    thumbScroll_ = std::max(0, thumbScroll_ + delta);
    Repaint();
}

bool UltraCanvasPDFView::OnEvent(const UCEvent& event) {
    switch (event.type) {
        case UCEventType::MouseWheel: {
            const Point2Di local(event.pointer.x - GetX(),
                                 event.pointer.y - GetY());
            const bool inThumbs = showThumbs_ &&
                                  local.x < style_.thumbStripWidth;
            if (event.ctrl && !inThumbs) {
                if (event.wheelDelta > 0) ZoomIn(); else ZoomOut();
                return true;
            }
            if (inThumbs) {
                ScrollThumbsBy(event.wheelDelta > 0 ? -40 : 40);
            } else {
                ScrollBy(0, event.wheelDelta > 0 ? -40 : 40);
            }
            return true;
        }

        case UCEventType::MouseDown: {
            if (event.button == UCMouseButton::Left) {
                int p = HitTestThumb(event.pointer);
                if (p > 0) { GoToPage(p); return true; }
                panning_   = true;
                panAnchor_ = event.pointer;
                panScrollX_ = scrollX_;
                panScrollY_ = scrollY_;
                return true;
            }
            break;
        }

        case UCEventType::MouseMove: {
            if (panning_) {
                scrollX_ = panScrollX_ - (event.pointer.x - panAnchor_.x);
                scrollY_ = panScrollY_ - (event.pointer.y - panAnchor_.y);
                Repaint();
                return true;
            }
            break;
        }

        case UCEventType::MouseUp: {
            if (panning_) { panning_ = false; return true; }
            break;
        }

        case UCEventType::KeyDown: {
            switch (event.virtualKey) {
                case UCKeys::PageDown: case UCKeys::Down:
                    GoToNextPage(); return true;
                case UCKeys::PageUp: case UCKeys::Up:
                    GoToPrevPage(); return true;
                case UCKeys::Home:
                    GoToFirstPage(); return true;
                case UCKeys::End:
                    GoToLastPage(); return true;
                case UCKeys::F3:
                    if (event.shift) PrevHit(); else NextHit();
                    return true;
                default: break;
            }
            break;
        }
        default: break;
    }
    return UltraCanvasUIElement::OnEvent(event);
}

void UltraCanvasPDFView::SetBounds(const Rect2Di& b) {
    UltraCanvasUIElement::SetBounds(b);
    InvalidateCaches();
}

// ===== Helpers =====

void UltraCanvasPDFView::Repaint() { RequestRedraw(); }

void UltraCanvasPDFView::FireDocumentChanged() {
    if (onDocumentChanged) onDocumentChanged();
}

void UltraCanvasPDFView::FirePageChanged() {
    if (onPageChanged) {
        onPageChanged(currentPage_, doc_ ? doc_->GetPageCount() : 0);
    }
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_PLUGIN_PDF
