// Plugins/Documents/UltraCanvasPDFView.cpp
// UI element rendering a PDF document via the IPDFDocument backend.
// Version: 1.2.0
// Last Modified: 2026-06-19
// Author: UltraCanvas Framework

#include "Plugins/Documents/UltraCanvasPDFView.h"

#ifdef ULTRACANVAS_PLUGIN_PDF

#include "../libspecific//Cairo/ImageCairo.h"  // UCPixmapCairo
#include "UltraCanvasMenu.h"                    // built-in image context menu
#include "UltraCanvasFileLoader.h"             // SaveFileDialog for "Extract Image"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace UltraCanvas {

namespace {
// File extension + dialog filter description for an image MIME type. The engine
// preserves the original format when it can, so the save dialog should offer the
// matching extension; anything unrecognized defaults to PNG (the fallback).
struct ImageFormat { const char* ext; const char* desc; };
ImageFormat FormatForMime(const std::string& mime) {
    if (mime == "image/jpeg")                return {"jpg",  "JPEG image"};
    if (mime == "image/jp2")                 return {"jp2",  "JPEG 2000 image"};
    if (mime == "image/bmp")                 return {"bmp",  "BMP image"};
    if (mime == "image/gif")                 return {"gif",  "GIF image"};
    if (mime == "image/tiff")                return {"tiff", "TIFF image"};
    if (mime == "image/vnd.ms-photo")        return {"jxr",  "JPEG XR image"};
    if (mime == "image/x-portable-anymap")   return {"pnm",  "PNM image"};
    if (mime == "image/vnd.adobe.photoshop") return {"psd",  "Photoshop image"};
    return {"png", "PNG image"};
}
} // namespace

// ===== ctor / dtor =====

UltraCanvasPDFView::UltraCanvasPDFView(const std::string& id,
                                       float x, float y, float w, float h)
    : UltraCanvasUIElement(id, x, y, w, h) {
    backgroundColor = style_.background;
}

UltraCanvasPDFView::~UltraCanvasPDFView() = default;

// ===== Document =====

void UltraCanvasPDFView::SetDocument(std::unique_ptr<IPDFDocument> doc) {
    doc_ = std::move(doc);
    currentPage_ = 1;
    scrollX_ = scrollY_ = thumbScroll_ = 0;
    zoomMode_ = ZoomMode::FitPage;
    userZoom_ = 1.0f;
    effectiveZoom_ = 1.0f;
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

void UltraCanvasPDFView::SetZoom(float scale) {
    scale = std::clamp(scale, 0.1f, 16.0f);
    zoomMode_ = ZoomMode::Custom;
    userZoom_ = scale;
    InvalidateCaches();      // page cache keyed by dpi; zoom changes dpi.
    Repaint();
}

void UltraCanvasPDFView::SetZoomMode(ZoomMode mode) {
    // Re-selecting Custom is a no-op; fit modes are re-resolved at render time.
    if (mode == zoomMode_) return;
    zoomMode_ = mode;
    InvalidateCaches();
    Repaint();
}

// Zoom in/out around the current effective scale so the step is continuous even
// when leaving a fit mode.
void UltraCanvasPDFView::ZoomIn()  { SetZoom(effectiveZoom_ * 1.25f); }
void UltraCanvasPDFView::ZoomOut() { SetZoom(effectiveZoom_ / 1.25f); }

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
    FireActiveHitChanged();
    Repaint();
    return static_cast<int>(hits_.size());
}

void UltraCanvasPDFView::ClearSearch() {
    query_.clear();
    hits_.clear();
    activeHit_ = -1;
    if (onSearchResults) onSearchResults(0);
    FireActiveHitChanged();
    Repaint();
}

void UltraCanvasPDFView::NextHit() {
    if (hits_.empty()) return;
    activeHit_ = (activeHit_ + 1) % static_cast<int>(hits_.size());
    GoToPage(hits_[activeHit_].pageNumber);
    FireActiveHitChanged();
    Repaint();
}

void UltraCanvasPDFView::PrevHit() {
    if (hits_.empty()) return;
    activeHit_ = (activeHit_ - 1 + static_cast<int>(hits_.size()))
               % static_cast<int>(hits_.size());
    GoToPage(hits_[activeHit_].pageNumber);
    FireActiveHitChanged();
    Repaint();
}

// ===== Images =====

std::vector<PDFImageRef> UltraCanvasPDFView::ImagesOnCurrentPage() {
    if (!doc_) return {};
    return doc_->ListImages(currentPage_);
}

int UltraCanvasPDFView::ImageIndexAt(const Point2Di& p) {
    if (!doc_ || pageRect_.width <= 0 || pageRect_.height <= 0) return -1;
    PDFPageInfo pi = doc_->GetPageInfo(currentPage_);
    if (pi.widthPt <= 0 || pi.heightPt <= 0) return -1;

    // Image bboxes are in PDF user units (top-left origin), the same space as
    // search hits, so they map to the page rect with the same scale factors.
    const float sx = pageRect_.width  / pi.widthPt;
    const float sy = pageRect_.height / pi.heightPt;

    auto images = doc_->ListImages(currentPage_);
    int found = -1;
    for (int i = 0; i < static_cast<int>(images.size()); ++i) {
        const Rect2Df& bb = images[i].bboxOnPage;
        const Rect2Df r(pageRect_.x + bb.x * sx,
                        pageRect_.y + bb.y * sy,
                        bb.width  * sx,
                        bb.height * sy);
        if (r.Contains(static_cast<float>(p.x), static_cast<float>(p.y))) {
            found = i;   // keep last match → topmost (drawn last) wins
        }
    }
    return found;
}

bool UltraCanvasPDFView::ExtractImageToFile(int indexOnPage,
                                            const std::string& path) {
    if (!doc_ || path.empty()) return false;
    auto images = doc_->ListImages(currentPage_);
    if (indexOnPage < 0 || indexOnPage >= static_cast<int>(images.size()))
        return false;
    std::vector<uint8_t> bytes = doc_->ExtractImageBytes(images[indexOnPage]);
    if (bytes.empty()) return false;

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    return f.good();
}

void UltraCanvasPDFView::ShowImageContextMenu(int imageIndex,
                                              const Point2Di& windowPos) {
    auto win = GetWindow();
    if (!win || imageIndex < 0) return;

    imageMenu_ = std::make_shared<UltraCanvasMenu>("PDFImageContextMenu",
                                                   0, 0, 220, 0);
    imageMenu_->SetMenuType(MenuType::PopupMenu);

    auto self = std::static_pointer_cast<UltraCanvasPDFView>(shared_from_this());
    std::weak_ptr<UltraCanvasPDFView> weakSelf = self;
    const int page = currentPage_;

    // Pick the default extension from the image's actual (preserved) format.
    auto images = doc_ ? doc_->ListImages(currentPage_) : std::vector<PDFImageRef>{};
    const std::string mime =
        (imageIndex < static_cast<int>(images.size())) ? images[imageIndex].mimeType
                                                       : std::string("image/png");
    const ImageFormat fmt = FormatForMime(mime);
    const std::string ext = fmt.ext, desc = fmt.desc;

    imageMenu_->AddItem(MenuItemData::Action("Extract Image\xE2\x80\xA6",
        [weakSelf, imageIndex, page, win, ext, desc]() {
            auto v = weakSelf.lock();
            if (!v) return;
            FileDialogOptions opts;
            opts.SetTitle("Extract Image")
                .SetDefaultFileName("image_p" + std::to_string(page) + "_" +
                                    std::to_string(imageIndex + 1) + "." + ext)
                .AddFilter(desc, ext)
                .SetParentWindow(win);
            UltraCanvasFileLoader::SaveFileDialog(opts,
                [weakSelf, imageIndex](DialogResult res, const std::string& chosen) {
                    auto v = weakSelf.lock();
                    if (!v || res != DialogResult::OK || chosen.empty()) return;
                    const bool ok = v->ExtractImageToFile(imageIndex, chosen);
                    if (v->onImageExtracted) v->onImageExtracted(chosen, ok);
                });
        }));

    imageMenu_->OpenMenu(windowPos, *win, PopupElementSettings());
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

bool UltraCanvasPDFView::ReplaceTextAt(const Rect2Df& bboxPt,
                                       const std::string& newText) {
    if (!doc_) return false;
    PDFTextRun run;
    run.pageNumber = currentPage_;
    run.bbox       = bboxPt;
    if (!doc_->ReplaceText(run, newText)) return false;
    InvalidateCaches();
    Repaint();
    return true;
}

bool UltraCanvasPDFView::ApplyPendingRedactions() {
    if (!doc_) return false;
    if (!doc_->ApplyPendingRedactions(currentPage_)) return false;
    InvalidateCaches();
    Repaint();
    return true;
}

std::vector<PDFAnnotation> UltraCanvasPDFView::ListAnnotationsOnCurrentPage() {
    return doc_ ? doc_->ListAnnotations(currentPage_)
                : std::vector<PDFAnnotation>{};
}

bool UltraCanvasPDFView::DeleteAnnotation(int indexOnCurrentPage) {
    if (!doc_) return false;
    if (!doc_->DeleteAnnotation(currentPage_, indexOnCurrentPage)) return false;
    InvalidateCaches();
    Repaint();
    return true;
}

// ===== Geometry =====

// Geometry is expressed in element-LOCAL coordinates (origin 0,0 == the view's
// top-left). The render context is already translated to the element origin
// before Render() runs, and incoming event.pointer values are mapped to local
// space, so both rendering and hit-testing share this frame.
Rect2Di UltraCanvasPDFView::ThumbStripArea() const {
    if (!showThumbs_) return Rect2Di(0, 0, 0, 0);
    return Rect2Di(0, 0, style_.thumbStripWidth,
                   static_cast<int>(GetHeight()));
}

Rect2Di UltraCanvasPDFView::PageContentArea() const {
    const int left = showThumbs_ ? style_.thumbStripWidth : 0;
    return Rect2Di(left, 0,
                   static_cast<int>(GetWidth()) - left,
                   static_cast<int>(GetHeight()));
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

float UltraCanvasPDFView::ComputeFitScale(int contentW, int contentH,
                                          bool widthOnly) const {
    if (!doc_) return 1.0f;
    PDFPageInfo pi = doc_->GetPageInfo(currentPage_);
    if (pi.widthPt <= 0 || pi.heightPt <= 0) return 1.0f;
    const float availW = std::max(1, contentW - 2 * style_.pageMargin);
    const float availH = std::max(1, contentH - 2 * style_.pageMargin);
    // Page size in pixels at actual size (100% == defaultDpi).
    const float pageW = pi.widthPt  * style_.defaultDpi / 72.0f;
    const float pageH = pi.heightPt * style_.defaultDpi / 72.0f;
    const float scaleW = availW / pageW;
    const float scaleH = availH / pageH;
    return widthOnly ? scaleW : std::min(scaleW, scaleH);
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

void UltraCanvasPDFView::Render(IRenderContext* ctx, const Rect2Df& /*dirty*/) {
    if (!IsVisible()) return;
    // Local frame: the render context is already translated to this element's
    // top-left, so draw from the origin rather than from GetBounds().
    const Rect2Di b(0, 0, static_cast<int>(GetWidth()),
                    static_cast<int>(GetHeight()));

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

    // Resolve the effective scale for the current zoom mode. Fit modes depend on
    // the live viewport, so they are recomputed here every frame.
    float ez;
    switch (zoomMode_) {
        case ZoomMode::FitPage:  ez = ComputeFitScale(area.width, area.height, false); break;
        case ZoomMode::FitWidth: ez = ComputeFitScale(area.width, area.height, true);  break;
        default:                 ez = userZoom_; break;
    }
    ez = std::clamp(ez, 0.1f, 16.0f);
    if (std::abs(ez - effectiveZoom_) > 0.001f) {
        effectiveZoom_ = ez;
        FireZoomChanged();   // only fires on change, so this settles in one frame
    }
    const float renderDpi = std::max(8.0f, style_.defaultDpi * ez);
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
    pageRect_ = pageRect;   // remembered for image hit-testing in OnEvent

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

    // Page number indicator, pinned to the top-right of the page content area.
    // The pill is sized to the label and the text is centered inside it so the
    // two always stay aligned (DrawText positions text by its top-left).
    ctx->SetFontSize(12);
    const std::string pageStr =
        std::to_string(currentPage_) + " / " + std::to_string(total);
    const Size2Di textSz = ctx->GetTextLineDimensions(pageStr);
    const float padX = 12.0f, badgeH = 24.0f, badgeMargin = 12.0f;
    const float badgeW = std::max(48.0f, textSz.width + 2 * padX);
    const Rect2Df badge(area.x + area.width - badgeW - badgeMargin,
                        area.y + badgeMargin, badgeW, badgeH);
    ctx->SetFillPaint(Color(0, 0, 0, 160));
    ctx->FillRoundedRectangle(badge, 4.0);
    ctx->SetFillPaint(Color(255, 255, 255, 255));
    ctx->DrawText(pageStr, ctx->CalculateCenteredTextPosition(pageStr, badge));

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
            // event.pointer is already in element-local coordinates.
            const bool inThumbs = showThumbs_ &&
                                  event.pointer.x < style_.thumbStripWidth;
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
            if (event.button == UCMouseButton::Right) {
                // Right-click over an image offers to extract it.
                int img = ImageIndexAt(event.pointer);
                if (img >= 0) {
                    ShowImageContextMenu(img, event.pointerWindow);
                    return true;
                }
                break;
            }
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

void UltraCanvasPDFView::SetBounds(const Rect2Df& b) {
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

void UltraCanvasPDFView::FireZoomChanged() {
    if (onZoomChanged) onZoomChanged(effectiveZoom_ * 100.0f);
}

void UltraCanvasPDFView::FireActiveHitChanged() {
    if (onActiveHitChanged) {
        onActiveHitChanged(activeHit_ >= 0 ? activeHit_ + 1 : 0,
                           static_cast<int>(hits_.size()));
    }
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_PLUGIN_PDF
