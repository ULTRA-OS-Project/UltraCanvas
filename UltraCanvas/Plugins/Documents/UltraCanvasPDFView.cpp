// Plugins/Documents/UltraCanvasPDFView.cpp
// UI element rendering a PDF document via the IPDFDocument backend.
// Version: 1.9.0
// Last Modified: 2026-08-25
// Author: UltraCanvas Framework

#include "Plugins/Documents/UltraCanvasPDFView.h"

#ifdef ULTRACANVAS_PLUGIN_PDF

#include "../libspecific//Cairo/ImageCairo.h"  // UCPixmapCairo
#include "UltraCanvasMenu.h"                    // built-in context menu
#include "UltraCanvasFileLoader.h"             // SaveFileDialog for extract/export
#include "UltraCanvasClipboard.h"              // SetClipboardText for "Copy"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <system_error>

namespace UltraCanvas {

namespace {
// File extension + dialog filter description for an image MIME type. The engine
// preserves the original format when it can, so the save dialog should offer the
// matching extension; anything unrecognized defaults to PNG (the fallback).
// Scroll sentinels: far beyond any real limit. The per-frame clamp in
// DrawPageWithOverlays resolves them to the page's exact top/bottom scroll
// position once the page's effective zoom (and with it the real limit) is
// known — page sizes can differ, so the limit is only exact at render time.
constexpr int kScrollPageTop    = std::numeric_limits<int>::min() / 4;
constexpr int kScrollPageBottom = std::numeric_limits<int>::max() / 4;

// Thumbnail-strip layout floors. A thumbnail is sized from the strip width and
// the page's own aspect ratio; these keep a degenerate page (or an extremely
// narrow strip) from collapsing the slot to nothing.
constexpr int   kMinThumbWidth     = 24;
constexpr int   kMinThumbHeight    = 24;

// Zoom limits and steps. The wheel steps finer than the buttons and the
// keyboard: a wheel notch is a small nudge, a click of "Zoom +" a deliberate one.
constexpr float kMinZoom      = 0.1f;
constexpr float kMaxZoom      = 16.0f;
constexpr float kZoomStep     = 1.25f;
constexpr float kWheelZoomStep = 1.1f;
constexpr float kDefaultPageAspect = 1.4142f;   // A4 portrait, when unknown
constexpr float kMinCaptionFont    = 8.0f;
constexpr float kMaxCaptionFont    = 13.0f;

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
    scrollX_ = thumbScroll_ = 0;
    scrollY_ = kScrollPageTop;   // open at the top of the first page
    zoomMode_ = ZoomMode::FitPage;
    userZoom_ = 1.0f;
    effectiveZoom_ = 1.0f;
    selecting_ = false;
    hasSelection_ = false;
    selAnchorChar_ = selCaretChar_ = 0;
    pageChars_.clear();
    pageLines_.clear();
    pageCharsPage_ = -1;
    query_.clear();
    hits_.clear();
    activeHit_ = -1;
    InvalidateAllCaches();
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
    // Read the document into memory when it fits: the engine streams pages from
    // an Open()ed file and keeps it open for as long as the document lives,
    // which blocks moving, renaming or deleting the very file being viewed
    // (on Windows outright). A file too big to hold is streamed instead — the
    // handle is the lesser cost there.
    std::error_code ec;
    const auto fileSize = std::filesystem::file_size(path, ec);
    const bool inMemory = !ec && maxInMemoryBytes_ > 0 &&
                          static_cast<uintmax_t>(fileSize) <= maxInMemoryBytes_;

    bool opened = inMemory && d->OpenInMemory(path, password);
    // Streaming is the fallback: the read may have failed on a file the engine
    // can still open (no memory for the buffer, a racing writer).
    if (!opened) opened = d->Open(path, password);
    if (!opened) {
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
    scrollX_ = 0;
    scrollY_ = kScrollPageTop;   // a page opens at its top, not centered
    // The selection and cached text belong to the previous page.
    selecting_ = false;
    hasSelection_ = false;
    pageCharsPage_ = -1;
    EnsureThumbVisible(currentPage_);   // keep the active thumb on-screen
    FireSelectionChanged();
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
    scale = std::clamp(scale, kMinZoom, kMaxZoom);
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
void UltraCanvasPDFView::ZoomIn()  { SetZoom(effectiveZoom_ * kZoomStep); }
void UltraCanvasPDFView::ZoomOut() { SetZoom(effectiveZoom_ / kZoomStep); }

void UltraCanvasPDFView::ZoomInAt(const Point2Di& local) {
    SetZoomAt(effectiveZoom_ * kWheelZoomStep, local);
}

void UltraCanvasPDFView::ZoomOutAt(const Point2Di& local) {
    SetZoomAt(effectiveZoom_ / kWheelZoomStep, local);
}

// Zoom so the page point under `local` stays under `local`. The page is drawn
// centred in the content area, shifted by the scroll offset (ComputePageDrawRect),
// so holding a point still is a matter of solving that placement for the new
// page size and turning the result back into a scroll offset.
void UltraCanvasPDFView::SetZoomAt(float scale, const Point2Di& local) {
    const float oldZoom = effectiveZoom_;
    const float newZoom = std::clamp(scale, kMinZoom, kMaxZoom);
    if (std::abs(newZoom - oldZoom) < 0.0001f) return;

    const Rect2Di area = PageContentArea();
    float oldW = 0.0f, oldH = 0.0f, newW = 0.0f, newH = 0.0f;
    const bool haveSizes = PageSizeAtZoom(oldZoom, oldW, oldH) &&
                           PageSizeAtZoom(newZoom, newW, newH);

    // Where the anchor sits on the page, as a fraction of the page rectangle.
    // A pointer outside the page (the margins around it, or the strip) anchors
    // the zoom on the page centre instead of dragging the page towards itself.
    float fx = 0.5f, fy = 0.5f;
    Point2Df anchor(area.x + area.width * 0.5f, area.y + area.height * 0.5f);
    if (haveSizes && pageRect_.width > 0.0f && pageRect_.height > 0.0f &&
        pageRect_.Contains(static_cast<float>(local.x),
                           static_cast<float>(local.y))) {
        fx = (local.x - pageRect_.x) / pageRect_.width;
        fy = (local.y - pageRect_.y) / pageRect_.height;
        anchor = Point2Df(static_cast<float>(local.x), static_cast<float>(local.y));
    }

    SetZoom(newZoom);
    if (!haveSizes) return;

    // ComputePageDrawRect: x = area.x + margin + (innerW - pageW) / 2 - scrollX.
    // Solve it for the scroll that puts the anchor fraction back under `anchor`.
    const int innerW = std::max(1, area.width  - 2 * style_.pageMargin);
    const int innerH = std::max(1, area.height - 2 * style_.pageMargin);
    const float wantX = anchor.x - fx * newW;
    const float wantY = anchor.y - fy * newH;
    int maxX = 0, maxY = 0;
    ComputeScrollLimitsAt(newZoom, maxX, maxY);
    scrollX_ = std::clamp(static_cast<int>(
            area.x + style_.pageMargin + (innerW - newW) * 0.5f - wantX + 0.5f),
            -maxX, maxX);
    scrollY_ = std::clamp(static_cast<int>(
            area.y + style_.pageMargin + (innerH - newH) * 0.5f - wantY + 0.5f),
            -maxY, maxY);
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

// ===== Text selection & export =====

void UltraCanvasPDFView::SetMouseMode(MouseMode m) {
    if (m == mouseMode_) return;
    mouseMode_ = m;
    selecting_ = false;
    Repaint();
}

void UltraCanvasPDFView::EnsurePageChars() {
    if (!doc_) { pageChars_.clear(); pageLines_.clear(); pageCharsPage_ = -1; return; }
    if (pageCharsPage_ == currentPage_) return;
    pageChars_ = doc_->ExtractTextChars(currentPage_);
    pageCharsPage_ = currentPage_;

    // Group consecutive chars by line index, recording each line's index range
    // and vertical extent for caret hit-testing.
    pageLines_.clear();
    for (int i = 0; i < static_cast<int>(pageChars_.size()); ++i) {
        const PDFTextChar& c = pageChars_[i];
        if (pageLines_.empty() || pageChars_[pageLines_.back().last].lineIndex != c.lineIndex) {
            pageLines_.push_back(CharLine{i, i, c.bbox.y, c.bbox.y + c.bbox.height});
        } else {
            CharLine& L = pageLines_.back();
            L.last = i;
            L.y0 = std::min(L.y0, c.bbox.y);
            L.y1 = std::max(L.y1, c.bbox.y + c.bbox.height);
        }
    }
}

bool UltraCanvasPDFView::LocalToPage(const Point2Di& local,
                                     Point2Df& outPage) const {
    if (!doc_ || pageRect_.width <= 0 || pageRect_.height <= 0) return false;
    PDFPageInfo pi = doc_->GetPageInfo(currentPage_);
    if (pi.widthPt <= 0 || pi.heightPt <= 0) return false;
    const float sx = pageRect_.width  / pi.widthPt;
    const float sy = pageRect_.height / pi.heightPt;
    outPage.x = (local.x - pageRect_.x) / sx;
    outPage.y = (local.y - pageRect_.y) / sy;
    return true;
}

int UltraCanvasPDFView::CaretAtPage(const Point2Df& p) const {
    if (pageChars_.empty() || pageLines_.empty()) return 0;

    // Pick the line containing p.y, else the one whose centre is nearest.
    int li = 0;
    float bestDist = std::numeric_limits<float>::max();
    for (int i = 0; i < static_cast<int>(pageLines_.size()); ++i) {
        const CharLine& L = pageLines_[i];
        if (p.y >= L.y0 && p.y <= L.y1) { li = i; bestDist = -1.0f; break; }
        const float d = std::abs(p.y - 0.5f * (L.y0 + L.y1));
        if (d < bestDist) { bestDist = d; li = i; }
    }

    // Within the line, the caret falls before the first char whose horizontal
    // midpoint is to the right of p.x; otherwise after the last char.
    const CharLine& L = pageLines_[li];
    for (int i = L.first; i <= L.last; ++i) {
        const Rect2Df& bb = pageChars_[i].bbox;
        if (p.x < bb.x + bb.width * 0.5f) return i;
    }
    return L.last + 1;
}

std::string UltraCanvasPDFView::GetSelectedText() {
    if (!hasSelection_ || selPage_ != currentPage_) return {};
    EnsurePageChars();
    const int n  = static_cast<int>(pageChars_.size());
    const int lo = std::clamp(std::min(selAnchorChar_, selCaretChar_), 0, n);
    const int hi = std::clamp(std::max(selAnchorChar_, selCaretChar_), 0, n);
    std::string out;
    for (int i = lo; i < hi; ++i) {
        if (i > lo && pageChars_[i].lineIndex != pageChars_[i - 1].lineIndex) {
            out.push_back('\n');
        }
        out += pageChars_[i].text;
    }
    return out;
}

void UltraCanvasPDFView::ClearTextSelection() {
    if (!hasSelection_ && !selecting_) return;
    selecting_ = false;
    hasSelection_ = false;
    FireSelectionChanged();
    Repaint();
}

void UltraCanvasPDFView::SelectAllText() {
    if (!doc_) return;
    EnsurePageChars();
    if (pageChars_.empty()) return;
    selAnchorChar_ = 0;
    selCaretChar_  = static_cast<int>(pageChars_.size());
    selPage_       = currentPage_;
    hasSelection_  = true;
    selecting_     = false;
    FireSelectionChanged();
    Repaint();
}

bool UltraCanvasPDFView::CopySelectionToClipboard() {
    const std::string text = GetSelectedText();
    if (text.empty()) return false;
    return SetClipboardText(text);
}

std::string UltraCanvasPDFView::GetCurrentPageText() {
    return doc_ ? doc_->GetPageText(currentPage_) : std::string();
}

bool UltraCanvasPDFView::ExportTextToFile(const std::string& path,
                                          bool selectionOnly) {
    if (path.empty()) return false;
    const std::string text = selectionOnly ? GetSelectedText()
                                            : GetCurrentPageText();
    if (text.empty()) return false;
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    return f.good();
}

void UltraCanvasPDFView::PromptExportText(bool selectionOnly) {
    auto win = GetWindow();
    FileDialogOptions opts;
    opts.SetTitle(selectionOnly ? "Export Selected Text" : "Export Page Text")
        .SetDefaultFileName(selectionOnly
                                ? "selection.txt"
                                : ("page_" + std::to_string(currentPage_) + ".txt"))
        .AddFilter("Text file", "txt")
        .SetParentWindow(win);
    std::weak_ptr<UltraCanvasPDFView> weakSelf =
        std::static_pointer_cast<UltraCanvasPDFView>(shared_from_this());
    UltraCanvasFileLoader::SaveFileDialog(opts,
        [weakSelf, selectionOnly](DialogResult res, const std::string& chosen) {
            auto v = weakSelf.lock();
            if (!v || res != DialogResult::OK || chosen.empty()) return;
            const bool ok = v->ExportTextToFile(chosen, selectionOnly);
            if (v->onTextExported) v->onTextExported(chosen, ok);
        });
}

// ===== Context menu =====

void UltraCanvasPDFView::ShowContextMenu(int imageIndex,
                                         const Point2Di& windowPos) {
    auto win = GetWindow();
    if (!win) return;

    imageMenu_ = std::make_shared<UltraCanvasMenu>("PDFContextMenu", 0, 0, 240, 0);
    imageMenu_->SetMenuType(MenuType::PopupMenu);

    std::weak_ptr<UltraCanvasPDFView> weakSelf =
        std::static_pointer_cast<UltraCanvasPDFView>(shared_from_this());
    const int page = currentPage_;
    bool needSeparator = false;

    // --- Image item (only when right-clicking over an image) ---
    if (imageIndex >= 0 && doc_) {
        auto images = doc_->ListImages(currentPage_);
        const std::string mime =
            (imageIndex < static_cast<int>(images.size()))
                ? images[imageIndex].mimeType : std::string("image/png");
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
        needSeparator = true;
    }

    // --- Selection-dependent text items ---
    if (!GetSelectedText().empty()) {
        if (needSeparator) imageMenu_->AddItem(MenuItemData::Separator());
        imageMenu_->AddItem(MenuItemData::Action("Copy",
            [weakSelf]() { if (auto v = weakSelf.lock()) v->CopySelectionToClipboard(); }));
        imageMenu_->AddItem(MenuItemData::Action("Export Selected Text\xE2\x80\xA6",
            [weakSelf]() { if (auto v = weakSelf.lock()) v->PromptExportText(true); }));
        needSeparator = true;
    }

    // --- Always-available text items ---
    if (needSeparator) imageMenu_->AddItem(MenuItemData::Separator());
    imageMenu_->AddItem(MenuItemData::Action("Select All Text",
        [weakSelf]() { if (auto v = weakSelf.lock()) v->SelectAllText(); }));
    imageMenu_->AddItem(MenuItemData::Action("Export Page Text\xE2\x80\xA6",
        [weakSelf]() { if (auto v = weakSelf.lock()) v->PromptExportText(false); }));

    imageMenu_->OpenMenu(windowPos, *win, PopupElementSettings());
}

// ===== Layout toggles =====

void UltraCanvasPDFView::SetShowThumbnailStrip(bool show) {
    if (show == showThumbs_) return;
    showThumbs_ = show;
    InvalidateCaches();   // page area changed → fit dpi changed
    Repaint();
}

void UltraCanvasPDFView::SetThumbnailNumberStyle(ThumbnailNumberStyle s) {
    if (s == thumbNumberStyle_) return;
    thumbNumberStyle_ = s;
    Repaint();   // overlay is drawn on top of cached thumbnails; no cache wipe
}

void UltraCanvasPDFView::SetThumbnailWidthMode(ThumbnailWidthMode m) {
    if (m == thumbWidthMode_) return;
    thumbWidthMode_ = m;
    // The strip's width changes, and with it the page area: the thumbnails are
    // re-laid out (and re-rendered at their new size) and the fit zoom of the
    // page is resolved against the new content area on the next frame.
    InvalidateThumbLayout();
    InvalidateCaches();
    Repaint();
}

void UltraCanvasPDFView::SetThumbnailWidth(int pixels) {
    const int w = std::max(kMinThumbWidth, pixels);
    if (w != style_.thumbWidth) {
        style_.thumbWidth = w;
        InvalidateThumbLayout();
        InvalidateCaches();
        Repaint();
    }
    SetThumbnailWidthMode(ThumbnailWidthMode::Absolute);
}

void UltraCanvasPDFView::SetThumbnailWidthFraction(float fraction) {
    const float f = std::clamp(fraction, 0.05f, 0.5f);
    if (std::abs(f - style_.thumbStripWidthFraction) > 0.0001f) {
        style_.thumbStripWidthFraction = f;
        InvalidateThumbLayout();
        InvalidateCaches();
        Repaint();
    }
    SetThumbnailWidthMode(ThumbnailWidthMode::Relative);
}

void UltraCanvasPDFView::SetStyle(const PDFViewStyle& s) {
    style_ = s;
    InvalidateThumbLayout();   // strip metrics are baked into the layout
    backgroundColor = style_.background;
    InvalidateAllCaches();
    Repaint();
}

// ===== Mutation passthroughs =====

bool UltraCanvasPDFView::DeleteCurrentPage() {
    if (!doc_) return false;
    const int total = doc_->GetPageCount();
    if (total <= 1) return false;
    if (!doc_->DeletePage(currentPage_)) return false;
    if (currentPage_ > doc_->GetPageCount()) currentPage_ = doc_->GetPageCount();
    InvalidateAllCaches();
    FirePageChanged();
    Repaint();
    return true;
}

bool UltraCanvasPDFView::MovePage(int fromPage, int toPage) {
    if (!doc_) return false;
    if (!doc_->MovePage(fromPage, toPage)) return false;
    if (currentPage_ == fromPage) currentPage_ = toPage;
    InvalidateAllCaches();
    FirePageChanged();
    Repaint();
    return true;
}

bool UltraCanvasPDFView::InsertBlankPageAt(int at, float wpt, float hpt) {
    if (!doc_) return false;
    if (!doc_->InsertBlankPage(at, wpt, hpt)) return false;
    InvalidateAllCaches();
    GoToPage(at);
    return true;
}

bool UltraCanvasPDFView::SaveAs(const std::string& path,
                                const PDFSaveOptions& opts) {
    if (!doc_) return false;
    return doc_->Save(path, opts);
}

bool UltraCanvasPDFView::MergeFromDocument(IPDFDocument& other, int srcStart,
                                           int srcEnd, int insertAt) {
    if (!doc_) return false;
    // insertAt <= 0 means "append after the last page". The engine clamps the
    // range end when srcEnd <= 0, so pass it through unchanged.
    const int at = (insertAt <= 0) ? doc_->GetPageCount() + 1 : insertAt;
    if (!doc_->MergeFrom(other, srcStart, srcEnd, at)) return false;
    InvalidateAllCaches();
    GoToPage(at);
    return true;
}

bool UltraCanvasPDFView::MergeFromFile(const std::string& path, int srcStart,
                                       int srcEnd, int insertAt,
                                       const std::string& password) {
    if (!doc_) return false;
    std::unique_ptr<IPDFDocument> other = OpenPDF(path, password);
    if (!other || !other->IsOpen()) {
        if (onError) onError("Failed to open PDF for merge: " + path);
        return false;
    }
    // `other`'s pages are grafted (copied) into doc_, so it can be released as
    // soon as the merge returns.
    return MergeFromDocument(*other, srcStart, srcEnd, insertAt);
}

bool UltraCanvasPDFView::ReplaceTextAt(const Rect2Df& bboxPt,
                                       const std::string& newText) {
    if (!doc_) return false;
    PDFTextRun run;
    run.pageNumber = currentPage_;
    run.bbox       = bboxPt;
    if (!doc_->ReplaceText(run, newText)) return false;
    InvalidateAllCaches();
    Repaint();
    return true;
}

bool UltraCanvasPDFView::ApplyPendingRedactions() {
    if (!doc_) return false;
    if (!doc_->ApplyPendingRedactions(currentPage_)) return false;
    InvalidateAllCaches();
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
    InvalidateAllCaches();
    Repaint();
    return true;
}

// ===== Geometry =====

// Geometry is expressed in element-LOCAL coordinates (origin 0,0 == the view's
// top-left). The render context is already translated to the element origin
// before Render() runs, and incoming event.pointer values are mapped to local
// space, so both rendering and hit-testing share this frame.
bool UltraCanvasPDFView::ThumbStripVisible() const {
    // A single-page document needs no page inventory, whatever the toggle says.
    return showThumbs_ && doc_ && doc_->GetPageCount() > 1;
}

int UltraCanvasPDFView::EffectiveThumbStripWidth() const {
    if (!ThumbStripVisible()) return 0;
    const int viewW = static_cast<int>(GetWidth());
    if (thumbWidthMode_ == ThumbnailWidthMode::Absolute) {
        // The requested thumbnail width plus the margins around it. The pixel
        // width is honoured as asked for; only a pane too narrow to leave the
        // page any room at all pulls it back, and then to half the view — the
        // alternative is an inventory with no document next to it.
        const int want = style_.thumbWidth + 2 * style_.thumbMargin;
        const int cap  = std::max(kMinThumbWidth + 2 * style_.thumbMargin,
                                  viewW / 2);
        return std::max(0, std::min(want, cap));
    }
    // Relative: a share of the view's width, and never wider than the style's
    // strip width. The default share is a quarter, which keeps the page area at
    // least 3x the strip so a narrow view never ends up mostly inventory.
    const int share = static_cast<int>(viewW * style_.thumbStripWidthFraction);
    return std::max(0, std::min(style_.thumbStripWidth, share));
}

int UltraCanvasPDFView::ThumbContentWidth() const {
    const int w = EffectiveThumbStripWidth();
    if (w <= 0) return 0;
    return std::max(kMinThumbWidth, w - 2 * style_.thumbMargin);
}

float UltraCanvasPDFView::ThumbNumberFontSize(int thumbH) const {
    // Both numbering styles size the number from the thumbnail it belongs to,
    // so a small page never gets an oversized number.
    if (thumbNumberStyle_ == ThumbnailNumberStyle::Overlay) {
        return std::max(8.0f, thumbH * style_.thumbOverlayNumberHeight);
    }
    return std::clamp(thumbH * style_.thumbLabelHeight,
                      kMinCaptionFont, kMaxCaptionFont);
}

void UltraCanvasPDFView::InvalidateThumbLayout() {
    thumbLayoutWidth_ = -1;   // forces EnsureThumbLayout() to rebuild
}

void UltraCanvasPDFView::EnsurePageAspects() const {
    const int pages = doc_ ? doc_->GetPageCount() : 0;
    if (pageAspectsPages_ == pages) return;
    pageAspectsPages_ = pages;
    pageAspects_.clear();
    pageAspects_.reserve(static_cast<size_t>(std::max(0, pages)));
    for (int p = 1; p <= pages; ++p) {
        const PDFPageInfo pi = doc_->GetPageInfo(p);
        pageAspects_.push_back((pi.widthPt > 0.0f && pi.heightPt > 0.0f)
                               ? pi.heightPt / pi.widthPt
                               : kDefaultPageAspect);
    }
}

void UltraCanvasPDFView::EnsureThumbLayout() const {
    const int width   = ThumbContentWidth();
    const int pages   = doc_ ? doc_->GetPageCount() : 0;
    const bool caption = (thumbNumberStyle_ == ThumbnailNumberStyle::Caption);
    if (thumbLayoutWidth_ == width && thumbLayoutPages_ == pages &&
        thumbLayoutCaption_ == caption) {
        return;
    }
    thumbLayoutWidth_   = width;
    thumbLayoutPages_   = pages;
    thumbLayoutCaption_ = caption;
    thumbLayout_.clear();
    thumbContentHeight_ = 0;
    thumbRenderDim_     = 0;
    if (width <= 0 || pages <= 0) return;

    EnsurePageAspects();
    thumbLayout_.reserve(static_cast<size_t>(pages));
    int y = style_.thumbSpacing;
    for (int p = 1; p <= pages; ++p) {
        // Each thumbnail is as tall as its own page needs at the strip's
        // width, so the inventory never pads a slot with empty space.
        const float aspect = (p <= static_cast<int>(pageAspects_.size()))
                             ? pageAspects_[p - 1] : kDefaultPageAspect;
        ThumbSlot slot;
        slot.w = width;
        slot.h = std::max(kMinThumbHeight,
                          static_cast<int>(width * aspect + 0.5f));
        if (slot.h > style_.thumbMaxHeight) {
            // A page too tall for the cap keeps its proportions by narrowing.
            slot.h = std::max(kMinThumbHeight, style_.thumbMaxHeight);
            slot.w = std::clamp(static_cast<int>(slot.h / aspect + 0.5f),
                                kMinThumbWidth, width);
        }
        slot.captionH = caption
            ? static_cast<int>(ThumbNumberFontSize(slot.h) + 0.5f) + 4
            : 0;
        slot.y = y;
        y += slot.h + slot.captionH + style_.thumbSpacing;
        thumbRenderDim_ = std::max(thumbRenderDim_, std::max(slot.w, slot.h));
        thumbLayout_.push_back(slot);
    }
    thumbContentHeight_ = y;
}

const std::vector<UltraCanvasPDFView::ThumbSlot>&
UltraCanvasPDFView::ThumbLayout() const {
    EnsureThumbLayout();
    return thumbLayout_;
}

int UltraCanvasPDFView::ThumbContentHeight() const {
    EnsureThumbLayout();
    return thumbContentHeight_;
}

Rect2Di UltraCanvasPDFView::ThumbStripArea() const {
    const int w = EffectiveThumbStripWidth();
    if (w <= 0) return Rect2Di(0, 0, 0, 0);
    return Rect2Di(0, 0, w, static_cast<int>(GetHeight()));
}

Rect2Di UltraCanvasPDFView::PageContentArea() const {
    const int left = EffectiveThumbStripWidth();
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

void UltraCanvasPDFView::InvalidateAllCaches() {
    // Document (or page content/order) changed: cached thumbnails belong to
    // the old state and must go too, or the strip keeps showing it.
    InvalidateCaches();
    thumbCache_.clear();
    thumbCacheMaxDim_ = 0;
    pageAspectsPages_ = -1;    // page sizes/order may have changed with it
    InvalidateThumbLayout();
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
UltraCanvasPDFView::EnsureThumbnail(int page, int maxDim) {
    if (!doc_ || maxDim <= 0) return {};
    if (thumbCacheMaxDim_ != maxDim) {
        // The strip was resized: every cached thumbnail is now the wrong size.
        thumbCache_.clear();
        thumbCacheMaxDim_ = maxDim;
    }
    auto it = thumbCache_.find(page);
    if (it != thumbCache_.end()) return it->second;
    PDFRenderedPage rp = doc_->RenderThumbnail(page, maxDim);
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

    if (ThumbStripVisible()) {
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
    const std::vector<ThumbSlot>& slots = ThumbLayout();
    const int total = static_cast<int>(slots.size());
    const int renderDim = thumbRenderDim_;

    for (int p = 1; p <= total; ++p) {
        const ThumbSlot& ts = slots[p - 1];
        const int top = strip.y + ts.y - thumbScroll_;

        // Skip thumbs that are entirely outside the visible strip.
        if (top + ts.h + ts.captionH < strip.y ||
            top > strip.y + strip.height) {
            continue;
        }

        // The slot matches the page's aspect ratio, so the page fills it: the
        // only spare room in the strip is the margin around the thumbnail.
        const Rect2Df slot(strip.x + (strip.width - ts.w) * 0.5f,
                           static_cast<float>(top),
                           static_cast<float>(ts.w),
                           static_cast<float>(ts.h));

        // Slot background
        const bool active = (p == currentPage_);
        ctx->SetFillPaint(style_.pageBackground);
        ctx->FillRectangle(slot);

        // Page thumbnail
        auto pm = EnsureThumbnail(p, renderDim);
        if (pm && pm->IsValid()) {
            ctx->DrawPixmap(*pm, slot, ImageFitMode::Fill);
        }

        // Border (active or normal)
        ctx->SetStrokePaint(active ? style_.thumbBorderActive
                                   : style_.thumbBorder);
        ctx->SetStrokeWidth(active ? 2.0 : 1.0);
        ctx->DrawRectangle(slot);

        // Page number — either a small caption beneath, or a large translucent
        // number overlaid on the page. Both are sized from this thumbnail's
        // height, so they shrink with it.
        const std::string num = std::to_string(p);
        const float fontPx = ThumbNumberFontSize(ts.h);
        ctx->SetFontSize(fontPx);
        if (thumbNumberStyle_ == ThumbnailNumberStyle::Overlay) {
            ctx->SetFillPaint(style_.thumbOverlayNumberColor);
            ctx->DrawText(num, ctx->CalculateCenteredTextPosition(num, slot));
        } else {
            // DrawText positions by the text's top-left, so the label is
            // centered inside the row reserved for it beneath the thumbnail.
            const Rect2Df caption(slot.x, slot.y + slot.height,
                                  slot.width, static_cast<float>(ts.captionH));
            ctx->SetFillPaint(style_.thumbLabelColor);
            ctx->DrawText(num, ctx->CalculateCenteredTextPosition(num, caption));
        }
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
    // Scroll limits depend on the zoom just resolved; re-clamp so a page turn
    // or resize never leaves the page stranded outside the viewport.
    {
        int maxSX = 0, maxSY = 0;
        ComputeScrollLimits(maxSX, maxSY);
        scrollX_ = std::clamp(scrollX_, -maxSX, maxSX);
        scrollY_ = std::clamp(scrollY_, -maxSY, maxSY);
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

    // Text selection overlay: one highlight band per spanned line, covering the
    // selected characters on that line.
    if (hasSelection_ && selPage_ == currentPage_) {
        EnsurePageChars();
        PDFPageInfo pi = doc_->GetPageInfo(currentPage_);
        if (pi.widthPt > 0 && pi.heightPt > 0 && !pageChars_.empty()) {
            const float sx = pageRect.width  / pi.widthPt;
            const float sy = pageRect.height / pi.heightPt;
            const int n  = static_cast<int>(pageChars_.size());
            const int lo = std::clamp(std::min(selAnchorChar_, selCaretChar_), 0, n);
            const int hi = std::clamp(std::max(selAnchorChar_, selCaretChar_), 0, n);
            ctx->SetFillPaint(style_.selectionFill);
            int i = lo;
            while (i < hi) {
                // Union the bboxes of the run of selected chars sharing a line.
                const int line = pageChars_[i].lineIndex;
                Rect2Df band = pageChars_[i].bbox;
                int j = i + 1;
                for (; j < hi && pageChars_[j].lineIndex == line; ++j) {
                    const Rect2Df& b = pageChars_[j].bbox;
                    const float x0 = std::min(band.x, b.x);
                    const float y0 = std::min(band.y, b.y);
                    const float x1 = std::max(band.x + band.width,  b.x + b.width);
                    const float y1 = std::max(band.y + band.height, b.y + b.height);
                    band = Rect2Df(x0, y0, x1 - x0, y1 - y0);
                }
                ctx->FillRectangle(Rect2Df(pageRect.x + band.x * sx,
                                           pageRect.y + band.y * sy,
                                           band.width  * sx,
                                           band.height * sy));
                i = j;
            }
        }
    }

    // No page-number badge is drawn over the page: the thumbnail strip already
    // marks the current page, and hosts (the media viewer, UltraFiler) show
    // "page N / M" in their own status bar, so a floating pill on the page
    // would only be a non-interactive duplicate.

    ctx->PopState();
}

// ===== Events =====

int UltraCanvasPDFView::HitTestThumb(const Point2Di& p) const {
    if (!ThumbStripVisible()) return 0;
    Rect2Di strip = ThumbStripArea();
    if (!strip.Contains(p)) return 0;
    const std::vector<ThumbSlot>& slots = ThumbLayout();
    const int total = static_cast<int>(slots.size());
    for (int i = 1; i <= total; ++i) {
        const ThumbSlot& ts = slots[i - 1];
        const Rect2Di slot(strip.x + (strip.width - ts.w) / 2,
                           strip.y + ts.y - thumbScroll_, ts.w, ts.h);
        if (slot.Contains(p)) return i;
    }
    return 0;
}

bool UltraCanvasPDFView::PageSizeAtZoom(float zoom, float& outW,
                                       float& outH) const {
    outW = outH = 0.0f;
    if (!doc_) return false;
    const PDFPageInfo pi = doc_->GetPageInfo(currentPage_);
    if (pi.widthPt <= 0 || pi.heightPt <= 0) return false;
    outW = pi.widthPt  * style_.defaultDpi / 72.0f * zoom;
    outH = pi.heightPt * style_.defaultDpi / 72.0f * zoom;
    return true;
}

bool UltraCanvasPDFView::ComputeScrollLimits(int& maxX, int& maxY) const {
    return ComputeScrollLimitsAt(effectiveZoom_, maxX, maxY);
}

bool UltraCanvasPDFView::ComputeScrollLimitsAt(float zoom, int& maxX,
                                               int& maxY) const {
    maxX = maxY = 0;
    // Page size on screen at `zoom`. The page is centered, so the scroll range
    // is symmetric: half the overflow in each direction.
    float pageW = 0.0f, pageH = 0.0f;
    if (!PageSizeAtZoom(zoom, pageW, pageH)) return false;
    const Rect2Di area = PageContentArea();
    const int innerW = std::max(1, area.width  - 2 * style_.pageMargin);
    const int innerH = std::max(1, area.height - 2 * style_.pageMargin);
    maxX = std::max(0, static_cast<int>((pageW - innerW) * 0.5f + 0.5f));
    maxY = std::max(0, static_cast<int>((pageH - innerH) * 0.5f + 0.5f));
    return true;
}

void UltraCanvasPDFView::ScrollBy(int dx, int dy) {
    int maxX = 0, maxY = 0;
    ComputeScrollLimits(maxX, maxY);

    // Only once the view already rests at the page edge does a further wheel
    // step continue into the neighbouring page, so a multi-page document can
    // be read straight through with the wheel.
    if (dy > 0 && scrollY_ >= maxY && currentPage_ < GetPageCount()) {
        GoToPage(currentPage_ + 1);        // opens at the top of the next page
        return;
    }
    if (dy < 0 && scrollY_ <= -maxY && currentPage_ > 1) {
        GoToPage(currentPage_ - 1);
        scrollY_ = kScrollPageBottom;      // arrive at the previous page's bottom
        return;
    }

    // Within the page the scroll is hard-limited: it stops once the page edge
    // sits style_.pageMargin inside the viewport — on the last/first page that
    // is the end of the line.
    scrollX_ = std::clamp(scrollX_ + dx, -maxX, maxX);
    scrollY_ = std::clamp(scrollY_ + dy, -maxY, maxY);
    Repaint();
}

void UltraCanvasPDFView::ScrollThumbsBy(int delta) {
    const Rect2Di strip = ThumbStripArea();
    const int maxScroll = std::max(0, ThumbContentHeight() - strip.height);
    thumbScroll_ = std::clamp(thumbScroll_ + delta, 0, maxScroll);
    Repaint();
}

void UltraCanvasPDFView::EnsureThumbVisible(int page) {
    if (!ThumbStripVisible()) return;
    const Rect2Di strip = ThumbStripArea();
    if (strip.height <= 0) return;
    const std::vector<ThumbSlot>& slots = ThumbLayout();
    if (page < 1 || page > static_cast<int>(slots.size())) return;
    const ThumbSlot& ts = slots[page - 1];
    const int top    = ts.y - thumbScroll_;
    const int bottom = top + ts.h + ts.captionH + style_.thumbSpacing;
    if (top < 0)                    thumbScroll_ += top;
    else if (bottom > strip.height) thumbScroll_ += bottom - strip.height;
    const int maxScroll = std::max(0, ThumbContentHeight() - strip.height);
    thumbScroll_ = std::clamp(thumbScroll_, 0, maxScroll);
}

bool UltraCanvasPDFView::OnEvent(const UCEvent& event) {
    switch (event.type) {
        case UCEventType::MouseWheel: {
            // event.pointer is already in element-local coordinates.
            const bool inThumbs = ThumbStripVisible() &&
                                  event.pointer.x < EffectiveThumbStripWidth();
            if (inThumbs) {   // the strip scrolls, whatever the wheel does elsewhere
                ScrollThumbsBy(event.wheelDelta > 0 ? -40 : 40);
                return true;
            }
            // Over the page: the configured action, with Ctrl selecting the
            // other one, so zooming and scrolling are both always reachable.
            const bool zooming = (wheelAction_ == WheelAction::Zoom) != event.ctrl;
            if (zooming) {
                if (event.wheelDelta > 0) ZoomInAt(event.pointer);
                else                      ZoomOutAt(event.pointer);
            } else {
                ScrollBy(0, event.wheelDelta > 0 ? -40 : 40);
            }
            return true;
        }

        case UCEventType::MouseDown: {
            // Clicking the view is what hands it the keyboard: without this the
            // page, zoom and selection keys only reach it in a host that focused
            // it itself.
            SetFocus(true);
            if (event.button == UCMouseButton::Right) {
                // Context menu: image extraction (if over an image) + text actions.
                ShowContextMenu(ImageIndexAt(event.pointer), event.pointerWindow);
                return true;
            }
            if (event.button == UCMouseButton::Left) {
                int p = HitTestThumb(event.pointer);
                if (p > 0) { GoToPage(p); return true; }

                // In select-text mode, a left-drag over the page selects text.
                Point2Df pg;
                const bool overPage =
                    event.pointer.x >= EffectiveThumbStripWidth();
                if (mouseMode_ == MouseMode::SelectText && overPage &&
                    LocalToPage(event.pointer, pg)) {
                    EnsurePageChars();
                    const int caret = CaretAtPage(pg);
                    selecting_     = true;
                    selAnchorChar_ = caret;
                    selCaretChar_  = caret;
                    selPage_       = currentPage_;
                    if (hasSelection_) { hasSelection_ = false; FireSelectionChanged(); }
                    Repaint();
                    return true;
                }

                panning_   = true;
                panAnchor_ = event.pointer;
                panScrollX_ = scrollX_;
                panScrollY_ = scrollY_;
                return true;
            }
            break;
        }

        case UCEventType::MouseMove: {
            if (selecting_) {
                Point2Df pg;
                if (LocalToPage(event.pointer, pg)) {
                    const int caret = CaretAtPage(pg);
                    if (caret != selCaretChar_) {
                        selCaretChar_ = caret;
                        hasSelection_ = (selCaretChar_ != selAnchorChar_);
                        FireSelectionChanged();
                        Repaint();
                    }
                }
                return true;
            }
            if (panning_) {
                // A pan drag stays within the current page (no page turning);
                // clamp so the page cannot be dragged out of the viewport.
                int maxX = 0, maxY = 0;
                ComputeScrollLimits(maxX, maxY);
                scrollX_ = std::clamp(
                    panScrollX_ - (event.pointer.x - panAnchor_.x), -maxX, maxX);
                scrollY_ = std::clamp(
                    panScrollY_ - (event.pointer.y - panAnchor_.y), -maxY, maxY);
                Repaint();
                return true;
            }
            break;
        }

        case UCEventType::MouseUp: {
            if (selecting_) {
                selecting_ = false;
                // A click with no drag clears the selection.
                if (GetSelectedText().empty()) {
                    hasSelection_ = false;
                    FireSelectionChanged();
                }
                Repaint();
                return true;
            }
            if (panning_) { panning_ = false; return true; }
            break;
        }

        case UCEventType::KeyDown: {
            // Zoom keys first: they are the same on every keyboard row (the
            // number row, the numeric keypad) and with or without Ctrl, which
            // is what users try. Both the key code and the character are
            // checked because platforms differ on which of the two carries a
            // shifted symbol such as '+'.
            switch (event.virtualKey) {
                case UCKeys::Plus: case UCKeys::Equal: case UCKeys::NumPadPlus:
                    ZoomIn(); return true;
                case UCKeys::Minus: case UCKeys::Underscore:
                case UCKeys::NumPadMinus:
                    ZoomOut(); return true;
                case UCKeys::Key0: case UCKeys::NumPad0:
                    ZoomToFit(); return true;         // whole page in view
                case UCKeys::Key1: case UCKeys::NumPad1:
                    ZoomActualSize(); return true;    // 100 %
                case UCKeys::W:
                    if (!event.ctrl) { ZoomToWidth(); return true; }
                    break;
                default: break;
            }
            switch (event.character) {
                case '+': case '=': ZoomIn();        return true;
                case '-': case '_': ZoomOut();       return true;
                case '0':           ZoomToFit();     return true;
                case '1':           ZoomActualSize(); return true;
                default: break;
            }

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
                case UCKeys::C:
                    if (event.ctrl) { CopySelectionToClipboard(); return true; }
                    break;
                case UCKeys::A:
                    if (event.ctrl) { SelectAllText(); return true; }
                    break;
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

void UltraCanvasPDFView::FireSelectionChanged() {
    if (onSelectionChanged) {
        onSelectionChanged(static_cast<int>(GetSelectedText().size()));
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
