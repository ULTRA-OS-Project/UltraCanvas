// core/UltraCanvasMediaViewer.cpp
// Implementation of the comprehensive media / photo / document viewer widget.
// See UltraCanvasMediaViewer.h for the feature overview.
// Version: 1.0.0
// Last Modified: 2026-06-26
// Author: UltraCanvas Framework

#include "UltraCanvasMediaViewer.h"
#include "UltraCanvasToolbar.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasFileLoader.h"   // FileDialogOptions, DialogResult, FileFilter
#ifdef ULTRACANVAS_PLUGIN_PDF
#include "Plugins/Documents/UltraCanvasPDFView.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace UltraCanvas {

static constexpr double kHalfPi = 1.5707963267948966;

// ===========================================================================
// FILE-LOCAL HELPERS
// ===========================================================================

static std::string LowerExt(const std::string& path) {
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return "";
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return ext;
}

static std::string BaseName(const std::string& path) {
    std::error_code ec;
    fs::path p(path);
    return p.filename().string();
}

static std::string HumanSize(uintmax_t bytes) {
    const char* units[] = { "B", "KB", "MB", "GB", "TB" };
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
    char buf[48];
    if (u == 0) snprintf(buf, sizeof(buf), "%llu %s", (unsigned long long)bytes, units[u]);
    else        snprintf(buf, sizeof(buf), "%.1f %s", v, units[u]);
    return std::string(buf);
}

#ifdef HAS_LIBVIPS
// Apply the tone/colour adjustments to a PixelFX image. Pure colour work — no
// geometry. Wrapped per-op in try/catch so an unsupported colourspace for one
// operation (e.g. sharpen on an exotic format) doesn't abort the whole chain.
static PixelFX::PFXImage ApplyColourAdjustments(PixelFX::PFXImage p,
                                                const MediaAdjustments& a) {
    if (a.autoOptimize) {
        try { p = PixelFX::Colour::HistEqual(p); } catch (...) {}
    }
    if (a.gamma != 1.0) {
        try { p = PixelFX::Colour::Gamma(p, a.gamma); } catch (...) {}
    }
    if (a.brightness != 1.0) {
        try { p = PixelFX::Colour::Brightness(p, a.brightness); } catch (...) {}
    }
    if (a.red != 1.0 || a.green != 1.0 || a.blue != 1.0) {
        try {
            int bands = p.bands();
            std::vector<double> m;
            if (bands >= 3) {
                m = { a.red, a.green, a.blue };
                while (static_cast<int>(m.size()) < bands) m.push_back(1.0);
            } else {
                double avg = (a.red + a.green + a.blue) / 3.0;
                m.assign(bands > 0 ? bands : 1, avg);
            }
            p = PixelFX::Arithmetic::Multiply(p, m);
        } catch (...) {}
    }
    if (a.sharpen > 0.0) {
        try { p = PixelFX::Convolution::Sharpen(p, a.sharpen); } catch (...) {}
    }
    return p;
}
#endif // HAS_LIBVIPS

// ===========================================================================
// MEDIA SURFACE
// ===========================================================================

UltraCanvasMediaSurface::UltraCanvasMediaSurface(const std::string& elemId)
    : UltraCanvasUIElement(elemId, 0.0f, 0.0f, 0.0f, 0.0f) {
    SetMouseCursor(UCMouseCursor::Default);
}

UltraCanvasMediaSurface::~UltraCanvasMediaSurface() {
    StopTransitionTimer();
}

double UltraCanvasMediaSurface::FitScale(double iw, double ih, int rotQ) const {
    Rect2Df b = GetLocalBounds();
    if (b.width <= 0 || b.height <= 0 || iw <= 0 || ih <= 0) return 0.0;
    bool swap = ((((rotQ % 4) + 4) % 4) % 2) == 1;   // 90° / 270° swap the box
    double effW = swap ? ih : iw;
    double effH = swap ? iw : ih;
    return std::min(b.width / effW, b.height / effH);
}

void UltraCanvasMediaSurface::RebuildProcessed() {
    processed.reset();
    if (!image || !image->IsValid()) return;
    if (adjust.IsIdentity()) return;   // nothing to do — draw the source directly
#ifdef HAS_LIBVIPS
    try {
        vips::VImage v = image->GetVImage();
        PixelFX::PFXImage p(v);
        p = ApplyColourAdjustments(p, adjust);
        processed = CreatePixmapFromVImage(p);
    } catch (...) {
        processed.reset();
    }
#endif
}

void UltraCanvasMediaSurface::SetAdjustments(const MediaAdjustments& adj) {
    adjust = adj;
    RebuildProcessed();
    RequestRedraw();
}

void UltraCanvasMediaSurface::ShowImage(std::shared_ptr<UCImage> img,
                                        MediaTransition transition,
                                        int durationMs, bool animated) {
    bool animate = animated && transition != MediaTransition::NoTransition &&
                   durationMs > 0 && image && image->IsValid();
    if (animate) {
        prevImage = image;
        prevProcessed = processed;
        prevRotQ = rotationQuarters;
        prevFlipH = flipH;
        prevFlipV = flipV;
        transitionStyle = transition;
        StartTransitionTimer(durationMs);
    } else {
        StopTransitionTimer();
        transitionActive = false;
        prevImage.reset();
        prevProcessed.reset();
    }

    image = std::move(img);
    // Each new image starts fit-to-window with no rotation/mirror.
    zoom = 1.0;
    panX = panY = 0.0;
    rotationQuarters = 0;
    flipH = flipV = false;
    RebuildProcessed();
    RequestRedraw();
    if (onViewChanged) onViewChanged();
}

void UltraCanvasMediaSurface::ResetView() {
    zoom = 1.0;
    panX = panY = 0.0;
    RequestRedraw();
    if (onViewChanged) onViewChanged();
}

double UltraCanvasMediaSurface::GetZoomPercent() const {
    if (!image || !image->IsValid()) return 100.0;
    double fit = FitScale(image->GetWidth(), image->GetHeight(), rotationQuarters);
    return fit * zoom * 100.0;
}

bool UltraCanvasMediaSurface::IsAutoFit() const {
    return std::fabs(zoom - 1.0) < 0.001;
}

void UltraCanvasMediaSurface::SetZoomPercent(double percent) {
    if (!image || !image->IsValid()) return;
    double fit = FitScale(image->GetWidth(), image->GetHeight(), rotationQuarters);
    if (fit <= 0.0) return;
    zoom = std::max(0.02, std::min((percent / 100.0) / fit, 64.0));
    RequestRedraw();
    if (onViewChanged) onViewChanged();
}

void UltraCanvasMediaSurface::ZoomBy(double factor) {
    zoom = std::max(0.02, std::min(zoom * factor, 64.0));
    RequestRedraw();
    if (onViewChanged) onViewChanged();
}

void UltraCanvasMediaSurface::RotateBy(int quarters) {
    rotationQuarters = (((rotationQuarters + quarters) % 4) + 4) % 4;
    RequestRedraw();
    if (onViewChanged) onViewChanged();
}

void UltraCanvasMediaSurface::SetRotationQuarters(int q) {
    rotationQuarters = ((q % 4) + 4) % 4;
    RequestRedraw();
    if (onViewChanged) onViewChanged();
}

void UltraCanvasMediaSurface::ToggleFlipHorizontal() {
    flipH = !flipH;
    RequestRedraw();
    if (onViewChanged) onViewChanged();
}

void UltraCanvasMediaSurface::ToggleFlipVertical() {
    flipV = !flipV;
    RequestRedraw();
    if (onViewChanged) onViewChanged();
}

void UltraCanvasMediaSurface::ResetTransforms() {
    rotationQuarters = 0;
    flipH = flipV = false;
    RequestRedraw();
    if (onViewChanged) onViewChanged();
}

void UltraCanvasMediaSurface::StartTransitionTimer(int durationMs) {
    StopTransitionTimer();
    transitionActive = true;
    transitionProgress = 0.0;
    transitionDurationMs = std::max(1, durationMs);
    transitionStart = std::chrono::steady_clock::now();
    auto* app = UltraCanvasApplication::GetInstance();
    if (!app) { transitionActive = false; return; }
    transitionTimer = app->StartTimer(16, true, [this](TimerId) {
        auto now = std::chrono::steady_clock::now();
        double el = std::chrono::duration<double, std::milli>(now - transitionStart).count();
        transitionProgress = el / static_cast<double>(transitionDurationMs);
        if (transitionProgress >= 1.0) {
            transitionProgress = 1.0;
            transitionActive = false;
            prevImage.reset();
            prevProcessed.reset();
            StopTransitionTimer();
        }
        RequestRedraw();
    });
}

void UltraCanvasMediaSurface::StopTransitionTimer() {
    if (transitionTimer) {
        if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(transitionTimer);
        transitionTimer = 0;
    }
}

void UltraCanvasMediaSurface::Blit(IRenderContext* ctx,
                                   const std::shared_ptr<UCImage>& img,
                                   const std::shared_ptr<UCPixmap>& pm,
                                   double iw, double ih, double scale,
                                   double cx, double cy, int rotQ,
                                   bool fH, bool fV, double alpha) {
    if (iw <= 0 || ih <= 0 || scale <= 0) return;
    double dispW = iw * scale;
    double dispH = ih * scale;
    ctx->PushState();
    if (alpha < 0.999) ctx->SetAlpha(alpha < 0.0 ? 0.0 : alpha);
    ctx->Translate(cx, cy);
    int q = ((rotQ % 4) + 4) % 4;
    if (q != 0) ctx->Rotate(q * kHalfPi);
    if (fH || fV) ctx->Scale(fH ? -1.0 : 1.0, fV ? -1.0 : 1.0);
    Rect2Dd dst(-dispW / 2.0, -dispH / 2.0, dispW, dispH);
    if (pm) {
        ctx->DrawPixmap(*pm, dst, ImageFitMode::Fill);
    } else if (img) {
        ctx->DrawImage(*img, dst, ImageFitMode::Fill);
    }
    ctx->PopState();
}

void UltraCanvasMediaSurface::DrawCurrent(IRenderContext* ctx, const Rect2Df& b) {
    if (!image || !image->IsValid()) {
        ctx->SetFontSize(16);
        ctx->SetTextPaint(Color(130, 130, 140, 255));
        Point2Dd p = ctx->CalculateCenteredTextPosition(
                "No media", Rect2Dd(b.x, b.y, b.width, b.height));
        ctx->DrawText("No media", p);
        return;
    }
    double iw = image->GetWidth();
    double ih = image->GetHeight();
    double fit = FitScale(iw, ih, rotationQuarters);
    double s = fit * zoom;
    double cx = b.width * 0.5 + panX;
    double cy = b.height * 0.5 + panY;
    Blit(ctx, image, processed, iw, ih, s, cx, cy, rotationQuarters, flipH, flipV, 1.0);
}

void UltraCanvasMediaSurface::DrawInfoOverlay(IRenderContext* ctx, const Rect2Df& b) {
    if (infoText.empty()) return;
    double boxW = std::min(440.0, static_cast<double>(b.width) - 24.0);
    double boxH = std::min(280.0, static_cast<double>(b.height) - 24.0);
    if (boxW <= 40 || boxH <= 40) return;
    Rect2Dd box(b.x + 12, b.y + 12, boxW, boxH);
    ctx->DrawFilledRectangle(box, Color(0, 0, 0, 190), 1.0f, Color(255, 255, 255, 45), 8.0f);
    double pad = 14;
    ctx->SetFontSize(12);
    ctx->SetTextPaint(Color(235, 235, 240, 255));
    ctx->SetTextAlignment(TextAlignment::Left);
    ctx->SetTextWrap(TextWrap::WrapWord);
    ctx->DrawTextInRect(infoText,
            Rect2Dd(box.x + pad, box.y + pad, box.width - 2 * pad, box.height - 2 * pad));
}

void UltraCanvasMediaSurface::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
    if (!IsVisible()) return;
    Rect2Df b = GetLocalBounds();
    if (b.width <= 0 || b.height <= 0) return;

    ctx->PushState();
    ctx->ClipRect(Rect2Dd(b.x, b.y, b.width, b.height));
    ctx->DrawFilledRectangle(Rect2Dd(b.x, b.y, b.width, b.height), canvasColor, 0.0f);

    if (transitionActive && (prevImage || image)) {
        double t = std::max(0.0, std::min(transitionProgress, 1.0));
        double cx = b.width * 0.5;
        double cy = b.height * 0.5;

        double iwC = image ? image->GetWidth() : 0;
        double ihC = image ? image->GetHeight() : 0;
        double fitC = FitScale(iwC, ihC, rotationQuarters);

        double iwP = prevImage ? prevImage->GetWidth() : 0;
        double ihP = prevImage ? prevImage->GetHeight() : 0;
        double fitP = prevImage ? FitScale(iwP, ihP, prevRotQ) : 0;

        // Per-style opacity / offset / scale for the outgoing (prev) and
        // incoming (current) layers.
        double prevAlpha = 1.0 - t, curAlpha = t;
        double prevOffX = 0, prevOffY = 0, curOffX = 0, curOffY = 0;
        double prevScaleMul = 1.0, curScaleMul = 1.0;

        switch (transitionStyle) {
            case MediaTransition::CrossFade:
                break;
            case MediaTransition::FadeOutIn:
                if (t < 0.5) { prevAlpha = 1.0 - 2.0 * t; curAlpha = 0.0; }
                else         { prevAlpha = 0.0; curAlpha = 2.0 * t - 1.0; }
                break;
            case MediaTransition::SlideHorizontal:
                prevAlpha = curAlpha = 1.0;
                prevOffX = -b.width * t;
                curOffX  =  b.width * (1.0 - t);
                break;
            case MediaTransition::SlideVertical:
                prevAlpha = curAlpha = 1.0;
                prevOffY = -b.height * t;
                curOffY  =  b.height * (1.0 - t);
                break;
            case MediaTransition::ZoomFade:
                curScaleMul = 1.18 - 0.18 * t;   // settle from 1.18x to 1.0x
                break;
            default:
                prevAlpha = 0.0; curAlpha = 1.0;
                break;
        }

        if (prevImage && prevAlpha > 0.001) {
            Blit(ctx, prevImage, prevProcessed, iwP, ihP, fitP * prevScaleMul,
                 cx + prevOffX, cy + prevOffY, prevRotQ, prevFlipH, prevFlipV, prevAlpha);
        }
        if (image && curAlpha > 0.001) {
            Blit(ctx, image, processed, iwC, ihC, fitC * curScaleMul,
                 cx + curOffX, cy + curOffY, rotationQuarters, flipH, flipV, curAlpha);
        }
    } else {
        DrawCurrent(ctx, b);
    }

    if (showInfoPopup) DrawInfoOverlay(ctx, b);
    ctx->PopState();
}

bool UltraCanvasMediaSurface::HandleWheelZoom(const UCEvent& event) {
    if (!image || !image->IsValid()) return false;
    Rect2Df b = GetLocalBounds();
    double iw = std::max(1, image->GetWidth());
    double ih = std::max(1, image->GetHeight());
    double fit = FitScale(iw, ih, rotationQuarters);
    if (fit <= 0.0) return false;

    double step = (event.wheelDelta > 0) ? 1.15 : (1.0 / 1.15);
    double newZoom = std::max(0.05, std::min(zoom * step, 64.0));
    if (newZoom == zoom) return true;

    // Anchor the zoom under the cursor only when the image is un-rotated /
    // un-mirrored (the simple screen<->image mapping holds). Otherwise zoom
    // about the centre.
    if (rotationQuarters == 0 && !flipH && !flipV) {
        double sOld = fit * zoom;
        double leftOld = b.width * 0.5 + panX - iw * sOld * 0.5;
        double topOld  = b.height * 0.5 + panY - ih * sOld * 0.5;
        double imgX = (event.pointer.x - leftOld) / sOld;
        double imgY = (event.pointer.y - topOld)  / sOld;
        zoom = newZoom;
        double sNew = fit * zoom;
        double cxNew = event.pointer.x - imgX * sNew + iw * sNew * 0.5;
        double cyNew = event.pointer.y - imgY * sNew + ih * sNew * 0.5;
        panX = cxNew - b.width * 0.5;
        panY = cyNew - b.height * 0.5;
    } else {
        zoom = newZoom;
    }
    RequestRedraw();
    if (onViewChanged) onViewChanged();
    return true;
}

bool UltraCanvasMediaSurface::OnEvent(const UCEvent& event) {
    auto* app = UltraCanvasApplication::GetInstance();
    switch (event.type) {
        case UCEventType::MouseWheel:
            return HandleWheelZoom(event);

        case UCEventType::MouseDown:
            if (Contains(event.pointer)) {
                pressing = true;
                dragging = false;
                pressX = lastX = event.pointer.x;
                pressY = lastY = event.pointer.y;
                pressButton = event.button;
                SetFocus(true);
                if (app) app->CaptureMouse(this);
                if (showInfoPopup) { showInfoPopup = false; RequestRedraw(); }
                return true;
            }
            return false;

        case UCEventType::MouseMove:
            if (pressing) {
                double dx = event.pointer.x - pressX;
                double dy = event.pointer.y - pressY;
                if (!dragging && (dx * dx + dy * dy) > 16) dragging = true;
                if (dragging) {
                    panX += event.pointer.x - lastX;
                    panY += event.pointer.y - lastY;
                    lastX = event.pointer.x;
                    lastY = event.pointer.y;
                    RequestRedraw();
                }
                return true;
            }
            return false;

        case UCEventType::MouseUp:
            if (pressing) {
                pressing = false;
                if (app) app->ReleaseMouse();
                bool wasDrag = dragging;
                dragging = false;
                // A click (no drag) navigates: left = next, right = previous —
                // works whether or not the image is zoomed in.
                if (!wasDrag && onNavigate) {
                    if (pressButton == UCMouseButton::Left)       onNavigate(1);
                    else if (pressButton == UCMouseButton::Right)  onNavigate(-1);
                }
                return true;
            }
            return false;

        case UCEventType::MouseDoubleClick:
            ResetView();   // back to fit-to-window
            return true;

        case UCEventType::Drop:
            if (onFilesDropped && !event.droppedFiles.empty()) {
                onFilesDropped(event.droppedFiles);
                return true;
            }
            return false;

        case UCEventType::KeyDown: {
            UCKeys k = event.virtualKey;
            if (k == UCKeys::Left)  { if (onNavigate) onNavigate(-1); return true; }
            if (k == UCKeys::Right) { if (onNavigate) onNavigate(1);  return true; }
            if (k == UCKeys::Up)    { ZoomBy(1.15);       return true; }
            if (k == UCKeys::Down)  { ZoomBy(1.0 / 1.15); return true; }
            if (event.character == '+' || event.character == '=') { ZoomBy(1.15); return true; }
            if (event.character == '-') { ZoomBy(1.0 / 1.15); return true; }
            if (event.character == '0') { ResetView(); return true; }
            if (event.character == 'r' || event.character == 'R') { RotateBy(1); return true; }
            return false;
        }

        default:
            return false;
    }
}

bool UltraCanvasMediaSurface::SaveProcessed(const std::string& path, std::string& error) {
    if (!image || !image->IsValid()) { error = "No image to save"; return false; }
#ifdef HAS_LIBVIPS
    try {
        vips::VImage v = image->GetVImage();
        PixelFX::PFXImage p(v);
        p = ApplyColourAdjustments(p, adjust);
        int q = ((rotationQuarters % 4) + 4) % 4;
        if      (q == 1) p = PixelFX::Resample::Rot(p, PixelFX::Angle::D90);
        else if (q == 2) p = PixelFX::Resample::Rot(p, PixelFX::Angle::D180);
        else if (q == 3) p = PixelFX::Resample::Rot(p, PixelFX::Angle::D270);
        if (flipH) p = PixelFX::Resample::FlipHorizontal(p);
        if (flipV) p = PixelFX::Resample::FlipVertical(p);
        if (!PixelFX::FileIO::Save(p, path)) {
            error = PixelFX::GetLastError();
            if (error.empty()) error = "Save failed";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    } catch (...) {
        error = "Unknown error during save";
        return false;
    }
#else
    error = "Saving requires libvips support";
    return false;
#endif
}

// ===========================================================================
// MEDIA VIEWER
// ===========================================================================

UltraCanvasMediaViewer::UltraCanvasMediaViewer(const std::string& identifier,
                                               float x, float y, float w, float h)
    : UltraCanvasContainer(identifier, x, y, w, h) {
    BuildUI(w, h);
}

UltraCanvasMediaViewer::~UltraCanvasMediaViewer() {
    if (slideshowTimer) {
        if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(slideshowTimer);
        slideshowTimer = 0;
    }
}

std::shared_ptr<UltraCanvasUIElement> UltraCanvasMediaViewer::BuildAdjustSlider(
        const std::string& id, const std::string& caption,
        float minV, float maxV, float value, std::function<void(float)> onChange) {
    auto box = std::make_shared<UltraCanvasContainer>(id + "_box", 0, 0, 124, 64);
    box->layout.SetFlexColumn().SetFlexGap(2)
               .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    box->layoutItem.SetFlexGrow(0).SetFlexShrink(0);

    auto lbl = std::make_shared<UltraCanvasLabel>(id + "_lbl", 0, 0, 124, 16, caption);
    lbl->SetFontSize(11);
    lbl->SetTextColor(Color(210, 210, 216, 255));
    lbl->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    box->AddChild(lbl);

    auto sld = std::make_shared<UltraCanvasSlider>(id, 0, 0, 124, 22);
    sld->SetRange(minV, maxV);
    sld->SetValue(value);
    sld->onValueChanged = [onChange](float v) { if (onChange) onChange(v); };
    sld->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    box->AddChild(sld);

    return box;
}

void UltraCanvasMediaViewer::BuildUI(float w, float h) {
    SetBackgroundColor(Color(24, 24, 28, 255));
    layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    // ----- TOP TOOLBAR ROW 1: navigation + slideshow -----
    // Two rows keep every control visible at modest widths (the toolbar widget
    // does not yet implement overflow handling).
    toolbar = std::make_shared<UltraCanvasToolbar>("MV_Toolbar", 0, 0, 0, 40);
    toolbar->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                       .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    toolbar->AddButton("mv_open", "Open", "", [this] { ShowOpenDialog(); });
    toolbar->AddSeparator("mv_sep0");
    toolbar->AddButton("mv_prev", "Prev", "", [this] { Previous(); });
    toolbar->AddButton("mv_next", "Next", "", [this] { Next(); });
    toolbar->AddSeparator("mv_sep1");
    playButton = toolbar->AddToggleButton("mv_play", "Slideshow", "",
            [this](bool on) { if (on) PlaySlideshow(); else PauseSlideshow(); });
    toolbar->AddDropdownButton("mv_interval", "Interval",
            { "3 s", "5 s", "7 s", "10 s" },
            [this](const std::string& s) {
                double sec = std::atof(s.c_str());
                if (sec > 0) SetSlideshowIntervalSeconds(sec);
            });
    toolbar->AddDropdownButton("mv_trans", "Transition",
            { "None", "Cross fade", "Fade out/in", "Slide H", "Slide V", "Zoom" },
            [this](const std::string& s) {
                MediaTransition t = MediaTransition::CrossFade;
                if      (s == "None")        t = MediaTransition::NoTransition;
                else if (s == "Cross fade")  t = MediaTransition::CrossFade;
                else if (s == "Fade out/in") t = MediaTransition::FadeOutIn;
                else if (s == "Slide H")     t = MediaTransition::SlideHorizontal;
                else if (s == "Slide V")     t = MediaTransition::SlideVertical;
                else if (s == "Zoom")        t = MediaTransition::ZoomFade;
                SetTransition(t);
            });
    AddChild(toolbar);

    // ----- TOP TOOLBAR ROW 2: view + edit -----
    toolbar2 = std::make_shared<UltraCanvasToolbar>("MV_Toolbar2", 0, 0, 0, 40);
    toolbar2->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                        .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    toolbar2->AddButton("mv_zoomout", "Zoom -", "", [this] { ZoomOutAction(); });
    toolbar2->AddDropdownButton("mv_zoom", "Zoom",
            { "Fit", "25%", "50%", "75%", "100%", "150%", "200%", "400%" },
            [this](const std::string& s) {
                if (s == "Fit") ZoomFitAction();
                else            ZoomPercentAction(std::atof(s.c_str()));
            });
    toolbar2->AddButton("mv_zoomin", "Zoom +", "", [this] { ZoomInAction(); });
    toolbar2->AddButton("mv_fit", "Fit", "", [this] { ZoomFitAction(); });
    toolbar2->AddSeparator("mv_sep3");
    toolbar2->AddButton("mv_rotl", "Rotate L", "", [this] { if (surface) surface->RotateBy(-1); });
    toolbar2->AddButton("mv_rotr", "Rotate R", "", [this] { if (surface) surface->RotateBy(1); });
    toolbar2->AddButton("mv_mirh", "Mirror H", "", [this] { if (surface) surface->ToggleFlipHorizontal(); });
    toolbar2->AddButton("mv_mirv", "Mirror V", "", [this] { if (surface) surface->ToggleFlipVertical(); });
    toolbar2->AddSeparator("mv_sep4");
    toolbar2->AddToggleButton("mv_adjust", "Adjust", "",
            [this](bool on) { if (adjustPanel) adjustPanel->SetVisible(on); });
    toolbar2->AddButton("mv_save", "Save as", "", [this] { ShowSaveDialog(); });
    toolbar2->AddButton("mv_info", "Info", "", [this] { if (surface) surface->ToggleInfoPopup(); });
    AddChild(toolbar2);

    // ----- ADJUSTMENTS PANEL (hidden until "Adjust" is toggled) -----
    adjustPanel = std::make_shared<UltraCanvasContainer>("MV_Adjust", 0, 0, 0, 84);
    adjustPanel->SetBackgroundColor(Color(34, 34, 40, 255));
    adjustPanel->layout.SetFlexRow().SetFlexGap(10)
                       .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    adjustPanel->SetPadding(8, 12, 8, 12);
    adjustPanel->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                           .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    adjustPanel->SetVisible(false);

    adjustPanel->AddChild(BuildAdjustSlider("adj_gamma", "Gamma", 0.2f, 3.0f, 1.0f,
            [this](float v) { adjustments.gamma = v; ApplyAdjustments(); }));
    adjustPanel->AddChild(BuildAdjustSlider("adj_bright", "Brightness", 0.2f, 2.5f, 1.0f,
            [this](float v) { adjustments.brightness = v; ApplyAdjustments(); }));
    adjustPanel->AddChild(BuildAdjustSlider("adj_red", "Red", 0.0f, 2.0f, 1.0f,
            [this](float v) { adjustments.red = v; ApplyAdjustments(); }));
    adjustPanel->AddChild(BuildAdjustSlider("adj_green", "Green", 0.0f, 2.0f, 1.0f,
            [this](float v) { adjustments.green = v; ApplyAdjustments(); }));
    adjustPanel->AddChild(BuildAdjustSlider("adj_blue", "Blue", 0.0f, 2.0f, 1.0f,
            [this](float v) { adjustments.blue = v; ApplyAdjustments(); }));
    adjustPanel->AddChild(BuildAdjustSlider("adj_sharp", "Sharpen", 0.0f, 3.0f, 0.0f,
            [this](float v) { adjustments.sharpen = v; ApplyAdjustments(); }));

    auto autoBtn = std::make_shared<UltraCanvasButton>("adj_auto", 0, 0, 96, 28, "Auto");
    autoBtn->onClick = [this] {
        adjustments.autoOptimize = !adjustments.autoOptimize;
        ApplyAdjustments();
    };
    autoBtn->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    adjustPanel->AddChild(autoBtn);

    auto resetBtn = std::make_shared<UltraCanvasButton>("adj_reset", 0, 0, 96, 28, "Reset");
    resetBtn->onClick = [this] {
        adjustments = MediaAdjustments();
        ApplyAdjustments();
    };
    resetBtn->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    adjustPanel->AddChild(resetBtn);
    AddChild(adjustPanel);

    // ----- CENTRAL IMAGE SURFACE -----
    surface = std::make_shared<UltraCanvasMediaSurface>("MV_Surface");
    surface->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                       .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    surface->onNavigate = [this](int d) { if (d < 0) Previous(); else Next(); };
    surface->onViewChanged = [this] { UpdateInfoBar(); };
    surface->onFilesDropped = [this](const std::vector<std::string>& files) {
        HandleDroppedFiles(files);
    };
    surface->SetAdjustments(adjustments);
    AddChild(surface);

#ifdef ULTRACANVAS_PLUGIN_PDF
    // ----- PDF DOCUMENT VIEW (shown instead of the image surface for PDFs) -----
    // Built with a zero size so the flex layout (not a fixed CSS size) drives it.
    {
        auto pv = std::make_shared<UltraCanvasPDFView>("MV_PDFView", 0, 0, 0, 0);
        pv->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                      .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        pv->onPageChanged = [this](int, int) { UpdateInfoBar(); };
        pv->SetVisible(false);
        pdfView = pv;
        AddChild(pdfView);
    }
#endif

    // ----- BOTTOM INFO BAR -----
    bottomBar = std::make_shared<UltraCanvasContainer>("MV_Bottom", 0, 0, 0, 26);
    bottomBar->SetBackgroundColor(Color(18, 18, 22, 255));
    bottomBar->layout.SetFlexRow().SetFlexGap(8)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    bottomBar->SetPadding(2, 10, 2, 10);
    bottomBar->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                         .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    infoLabel = std::make_shared<UltraCanvasLabel>("MV_InfoLabel", 0, 0, 0, 20, "No media");
    infoLabel->SetFontSize(11);
    infoLabel->SetTextColor(Color(200, 200, 206, 255));
    infoLabel->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    bottomBar->AddChild(infoLabel);

    auto detailsBtn = std::make_shared<UltraCanvasButton>("MV_Details", 0, 0, 72, 20, "Details");
    detailsBtn->onClick = [this] { if (surface) surface->ToggleInfoPopup(); };
    detailsBtn->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    bottomBar->AddChild(detailsBtn);
    AddChild(bottomBar);

    (void)w; (void)h;
}

bool UltraCanvasMediaViewer::IsDocumentFile(const std::string& path) {
    // Documents rendered through a dedicated document element rather than the
    // image pipeline. PDF is handled by UltraCanvasPDFView (MuPDF) and is only
    // advertised as supported when that plugin is compiled in.
#ifdef ULTRACANVAS_PLUGIN_PDF
    return LowerExt(path) == "pdf";
#else
    (void)path;
    return false;
#endif
}

bool UltraCanvasMediaViewer::IsSupportedMedia(const std::string& path) {
    static const std::vector<std::string> exts = {
        "jpg", "jpeg", "jpe", "png", "gif", "bmp", "webp", "tif", "tiff",
        "svg", "ico", "cur", "heic", "heif", "avif", "jxl", "jp2", "j2k",
        "ppm", "pgm", "pbm", "pnm", "pfm", "tga", "psd", "qoi", "hdr", "exr",
        "xpm", "xbm", "pcx", "sgi", "dds", "fits"
    };
    std::string e = LowerExt(path);
    if (e.empty()) return false;
    if (std::find(exts.begin(), exts.end(), e) != exts.end()) return true;
    return IsDocumentFile(path);   // PDF when the document plugin is present
}

std::vector<std::string> UltraCanvasMediaViewer::EnumerateFolder(const std::string& folder) {
    std::vector<std::string> out;
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) return out;
    for (fs::directory_iterator it(folder, ec), end; it != end && !ec; it.increment(ec)) {
        std::error_code fec;
        if (it->is_regular_file(fec)) {
            std::string p = it->path().string();
            if (IsSupportedMedia(p)) out.push_back(p);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string UltraCanvasMediaViewer::GetCurrentPath() const {
    if (currentIndex < playlist.size()) return playlist[currentIndex];
    return "";
}

void UltraCanvasMediaViewer::OpenFolder(const std::string& folderPath,
                                        const std::string& selectFile) {
    playlist = EnumerateFolder(folderPath);
    currentIndex = 0;
    if (!selectFile.empty()) {
        std::string target = BaseName(selectFile);
        for (size_t i = 0; i < playlist.size(); ++i) {
            if (playlist[i] == selectFile || BaseName(playlist[i]) == target) {
                currentIndex = i;
                break;
            }
        }
    }
    LoadCurrent(false);
    if (slideshowPlaying) { PauseSlideshow(); PlaySlideshow(); }
}

void UltraCanvasMediaViewer::SetFiles(const std::vector<std::string>& files, size_t startIndex) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& f : files) {
        if (fs::is_directory(f, ec)) {
            auto sub = EnumerateFolder(f);
            out.insert(out.end(), sub.begin(), sub.end());
        } else if (IsSupportedMedia(f)) {
            out.push_back(f);
        }
    }
    if (out.empty()) return;
    playlist = std::move(out);
    currentIndex = std::min(startIndex, playlist.size() - 1);
    LoadCurrent(false);
    if (slideshowPlaying) { PauseSlideshow(); PlaySlideshow(); }
}

void UltraCanvasMediaViewer::OpenFile(const std::string& filePath) {
    std::error_code ec;
    fs::path p(filePath);
    std::string folder = p.parent_path().string();
    if (folder.empty()) folder = ".";
    OpenFolder(folder, filePath);
}

void UltraCanvasMediaViewer::ShowOpenDialog() {
    FileDialogOptions opts;
    opts.SetTitle("Open media")
        .AddFilter("Images", std::vector<std::string>{
            "png", "jpg", "jpeg", "gif", "bmp", "webp", "tiff", "tif",
            "svg", "ico", "heic", "heif", "avif", "jxl", "tga", "ppm", "qoi" })
        .AddFilter("All files", std::vector<std::string>{ "*" })
        .SetParentWindow(GetWindow());
    UltraCanvasFileLoader::OpenMultipleFilesDialog(opts,
            [this](DialogResult r, const std::vector<std::string>& files) {
                if (r == DialogResult::OK && !files.empty()) SetFiles(files, 0);
            });
}

void UltraCanvasMediaViewer::ShowSaveDialog() {
    if (!surface || !surface->GetImage() || !surface->GetImage()->IsValid()) return;
    std::string current = GetCurrentPath();
    std::string defName = current.empty() ? "image.png" : BaseName(current);

    FileDialogOptions opts;
    opts.SetTitle("Save image as")
        .SetDefaultFileName(defName)
        .AddFilter("PNG image",  std::vector<std::string>{ "png" })
        .AddFilter("JPEG image", std::vector<std::string>{ "jpg", "jpeg" })
        .AddFilter("WebP image", std::vector<std::string>{ "webp" })
        .AddFilter("TIFF image", std::vector<std::string>{ "tiff", "tif" })
        .AddFilter("AVIF image", std::vector<std::string>{ "avif" })
        .AddFilter("BMP image",  std::vector<std::string>{ "bmp" })
        .SetParentWindow(GetWindow());
    UltraCanvasFileLoader::SaveFileDialog(opts,
            [this](DialogResult r, const std::string& path) {
                if (r != DialogResult::OK || path.empty() || !surface) return;
                std::string err;
                if (surface->SaveProcessed(path, err)) {
                    if (infoLabel) infoLabel->SetText("Saved: " + BaseName(path));
                } else if (infoLabel) {
                    infoLabel->SetText("Save failed: " + err);
                }
            });
}

void UltraCanvasMediaViewer::Next() {
    if (playlist.empty()) return;
    currentIndex = (currentIndex + 1) % playlist.size();
    LoadCurrent(true);
}

void UltraCanvasMediaViewer::Previous() {
    if (playlist.empty()) return;
    currentIndex = (currentIndex + playlist.size() - 1) % playlist.size();
    LoadCurrent(true);
}

void UltraCanvasMediaViewer::GoTo(size_t index, bool animated) {
    if (index >= playlist.size()) return;
    currentIndex = index;
    LoadCurrent(animated);
}

void UltraCanvasMediaViewer::LoadCurrent(bool animated) {
    if (!surface) return;
    if (playlist.empty()) {
        if (pdfView) pdfView->SetVisible(false);
        pdfActive = false;
        surface->SetVisible(true);
        surface->ShowImage(nullptr, MediaTransition::NoTransition, 0, false);
        UpdateInfoBar();
        return;
    }
    if (currentIndex >= playlist.size()) currentIndex = playlist.size() - 1;
    const std::string& path = playlist[currentIndex];

#ifdef ULTRACANVAS_PLUGIN_PDF
    if (IsDocumentFile(path) && pdfView) {
        // Render the document through the dedicated PDF element (MuPDF), not the
        // image/libvips path. Drop the now-hidden image so it frees memory.
        surface->SetVisible(false);
        surface->ShowImage(nullptr, MediaTransition::NoTransition, 0, false);
        pdfView->SetVisible(true);
        pdfActive = true;
        auto* pv = static_cast<UltraCanvasPDFView*>(pdfView.get());
        if (!pv->LoadFromPath(path) && infoLabel) {
            infoLabel->SetText("Failed to open document: " + BaseName(path));
        }
        UpdateInfoBar();
        UpdateDetailedInfo();
        return;
    }
#endif

    // Image path.
    if (pdfView) pdfView->SetVisible(false);
    pdfActive = false;
    surface->SetVisible(true);
    auto img = UCImage::Get(path);
    surface->ShowImage(img, transition, transitionDurationMs, animated);
    UpdateInfoBar();
    UpdateDetailedInfo();
}

void UltraCanvasMediaViewer::ApplyAdjustments() {
    if (surface) surface->SetAdjustments(adjustments);
}

// ===== ZOOM ACTIONS (routed to whichever view is live) =====

void UltraCanvasMediaViewer::ZoomInAction() {
#ifdef ULTRACANVAS_PLUGIN_PDF
    if (pdfActive && pdfView) { static_cast<UltraCanvasPDFView*>(pdfView.get())->ZoomIn(); return; }
#endif
    if (surface) surface->ZoomBy(1.25);
}

void UltraCanvasMediaViewer::ZoomOutAction() {
#ifdef ULTRACANVAS_PLUGIN_PDF
    if (pdfActive && pdfView) { static_cast<UltraCanvasPDFView*>(pdfView.get())->ZoomOut(); return; }
#endif
    if (surface) surface->ZoomBy(1.0 / 1.25);
}

void UltraCanvasMediaViewer::ZoomFitAction() {
#ifdef ULTRACANVAS_PLUGIN_PDF
    if (pdfActive && pdfView) { static_cast<UltraCanvasPDFView*>(pdfView.get())->ZoomToFit(); return; }
#endif
    if (surface) surface->ResetView();
}

void UltraCanvasMediaViewer::ZoomPercentAction(double percent) {
#ifdef ULTRACANVAS_PLUGIN_PDF
    if (pdfActive && pdfView) {
        static_cast<UltraCanvasPDFView*>(pdfView.get())->SetZoom(static_cast<float>(percent / 100.0));
        return;
    }
#endif
    if (surface) surface->SetZoomPercent(percent);
}

void UltraCanvasMediaViewer::UpdateInfoBar() {
    if (!infoLabel) return;
    if (playlist.empty()) {
        infoLabel->SetText("No media");
        return;
    }
    const std::string& path = playlist[currentIndex];

#ifdef ULTRACANVAS_PLUGIN_PDF
    if (pdfActive && pdfView) {
        auto* pv = static_cast<UltraCanvasPDFView*>(pdfView.get());
        std::ostringstream os;
        os << BaseName(path) << "   \xC2\xB7   PDF";
        if (pv->HasDocument()) {
            os << "   \xC2\xB7   page " << pv->GetCurrentPage()
               << " / " << pv->GetPageCount();
        }
        std::error_code ec;
        auto sz = fs::file_size(path, ec);
        if (!ec) os << "   \xC2\xB7   " << HumanSize(sz);
        os << "   \xC2\xB7   " << (currentIndex + 1) << " / " << playlist.size();
        infoLabel->SetText(os.str());
        return;
    }
#endif

    if (!surface || !surface->GetImage() || !surface->GetImage()->IsValid()) {
        infoLabel->SetText("No media");
        return;
    }
    auto img = surface->GetImage();
    std::ostringstream os;
    os << BaseName(path)
       << "   \xC2\xB7   " << img->GetWidth() << " x " << img->GetHeight();

    std::error_code ec;
    auto sz = fs::file_size(path, ec);
    if (!ec) os << "   \xC2\xB7   " << HumanSize(sz);

    std::string ext = LowerExt(path);
    if (!ext.empty()) {
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return (char)std::toupper(c); });
        os << "   \xC2\xB7   " << ext;
    }

    os << "   \xC2\xB7   " << (currentIndex + 1) << " / " << playlist.size();

    char zbuf[32];
    snprintf(zbuf, sizeof(zbuf), "%.0f%%", surface->GetZoomPercent());
    os << "   \xC2\xB7   " << zbuf;

    int rot = surface->GetRotationQuarters();
    if (rot != 0) os << "   \xC2\xB7   " << (rot * 90) << "\xC2\xB0";

    infoLabel->SetText(os.str());
}

void UltraCanvasMediaViewer::UpdateDetailedInfo() {
    if (!surface || playlist.empty()) return;
    const std::string& path = playlist[currentIndex];

#ifdef ULTRACANVAS_PLUGIN_PDF
    if (pdfActive && pdfView) {
        // Document metadata comes from the PDF element, never from libvips.
        auto* pv = static_cast<UltraCanvasPDFView*>(pdfView.get());
        std::ostringstream dos;
        dos << "Document information\n\n";
        dos << "File: " << BaseName(path) << "\n";
        dos << "Path: " << path << "\n";
        std::error_code dec;
        auto dsz = fs::file_size(path, dec);
        if (!dec) dos << "Size: " << HumanSize(dsz) << "\n";
        dos << "Type: PDF document\n";
        if (pv->HasDocument()) dos << "Pages: " << pv->GetPageCount() << "\n";
        surface->SetInfoText(dos.str());
        return;
    }
#endif

    std::ostringstream os;
    os << "Image information\n\n";
    os << "File: " << BaseName(path) << "\n";
    os << "Path: " << path << "\n";

    std::error_code ec;
    auto sz = fs::file_size(path, ec);
    if (!ec) os << "Size: " << HumanSize(sz) << "\n";

    if (auto img = surface->GetImage()) {
        os << "Dimensions: " << img->GetWidth() << " x " << img->GetHeight() << " px\n";
    }

#ifdef HAS_LIBVIPS
    try {
        PixelFX::PXImageFileInfo fi = PixelFX::ExtractImageInfo(path);
        os << "Channels: " << fi.channels << "\n";
        os << "Bits/channel: " << fi.bitsPerChannel << "\n";
        if (!fi.colorSpace.empty()) os << "Colour space: " << fi.colorSpace << "\n";
        os << "Alpha: " << (fi.hasAlpha ? "yes" : "no") << "\n";
        if (fi.dpiX > 0 || fi.dpiY > 0)
            os << "Resolution: " << fi.dpiX << " x " << fi.dpiY << " dpi\n";
        if (!fi.loader.empty()) os << "Loader: " << fi.loader << "\n";
    } catch (...) {
        // Metadata extraction is best-effort.
    }
#endif

    surface->SetInfoText(os.str());
}

// ===== SLIDESHOW =====

void UltraCanvasMediaViewer::PlaySlideshow() {
    slideshowPlaying = true;
    auto* app = UltraCanvasApplication::GetInstance();
    if (!app) return;
    if (slideshowTimer) app->StopTimer(slideshowTimer);
    unsigned ms = static_cast<unsigned>(std::max(0.5, slideshowIntervalSec) * 1000.0);
    slideshowTimer = app->StartTimer(ms, true, [this](TimerId) { Next(); });
}

void UltraCanvasMediaViewer::PauseSlideshow() {
    slideshowPlaying = false;
    if (slideshowTimer) {
        if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(slideshowTimer);
        slideshowTimer = 0;
    }
}

void UltraCanvasMediaViewer::ToggleSlideshow() {
    if (slideshowPlaying) PauseSlideshow();
    else                  PlaySlideshow();
}

void UltraCanvasMediaViewer::SetSlideshowIntervalSeconds(double sec) {
    slideshowIntervalSec = std::max(0.5, sec);
    if (slideshowPlaying) { PauseSlideshow(); slideshowPlaying = true; PlaySlideshow(); }
}

// ===== EVENTS =====

void UltraCanvasMediaViewer::HandleDroppedFiles(const std::vector<std::string>& files) {
    if (files.empty()) return;
    std::error_code ec;
    // A single dropped folder browses that folder; anything else is treated as
    // an explicit file list (with any folders in it expanded).
    if (files.size() == 1 && fs::is_directory(files[0], ec)) {
        OpenFolder(files[0]);
        return;
    }
    SetFiles(files, 0);
}

bool UltraCanvasMediaViewer::OnEvent(const UCEvent& event) {
    if (event.type == UCEventType::Drop) {
        HandleDroppedFiles(event.droppedFiles);
        return true;
    }
    if (UltraCanvasContainer::OnEvent(event)) return true;

    if (event.type == UCEventType::KeyDown) {
        switch (event.virtualKey) {
            case UCKeys::Left:  Previous();        return true;
            case UCKeys::Right: Next();            return true;
            case UCKeys::Space: ToggleSlideshow(); return true;
            default: break;
        }
    }
    return false;
}

} // namespace UltraCanvas
