// core/UltraCanvasPaintSurface.cpp
// Editable raster view: zoom / pan, checkerboard, pixel grid, marching-ants
// selection, brush cursor, and pointer events forwarded to the active tool
// in image coordinates.
// Version: 1.0.0
// Last Modified: 2026-09-06
// Author: UltraCanvas Framework

#include "UltraCanvasPaintSurface.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasRenderContext.h"

#include <algorithm>
#include <cmath>

namespace UltraCanvas {

namespace {
    // Zoom ladder for ZoomStep(): the usual editor steps.
    const double kZoomSteps[] = { 0.03125, 0.0625, 0.125, 0.25, 0.333, 0.5, 0.667, 1.0, 1.5, 2.0,
                                  3.0, 4.0, 6.0, 8.0, 12.0, 16.0, 24.0, 32.0, 48.0, 64.0 };
    constexpr double kMinZoom = 0.01;
    constexpr double kMaxZoom = 128.0;
}

UltraCanvasPaintSurface::UltraCanvasPaintSurface(const std::string& elemId)
    : UltraCanvasUIElement(elemId, 0.0f, 0.0f, 0.0f, 0.0f) {
    SetMouseCursor(UCMouseCursor::Cross);
}

UltraCanvasPaintSurface::~UltraCanvasPaintSurface() {
    StopAntsTimer();
    UnhookDocument();
}

// ===========================================================================
// DOCUMENT
// ===========================================================================

void UltraCanvasPaintSurface::HookDocument() {
    if (!document) return;
    document->onPixelsChanged = [this](const Rect2Di& r) {
        const Rect2Dd v = view.ImageToView(Rect2Dd(r.x, r.y, r.width, r.height));
        InvalidateRect(Rect2Df(static_cast<float>(std::floor(v.x)) - 2.0f, static_cast<float>(std::floor(v.y)) - 2.0f,
                               static_cast<float>(std::ceil(v.width)) + 4.0f, static_cast<float>(std::ceil(v.height)) + 4.0f));
    };
    document->onStructureChanged = [this]() { ClampView(); RequestRedraw(); };
    document->onSelectionChanged = [this]() {
        if (document->GetSelection().IsActive()) StartAntsTimer(); else StopAntsTimer();
        RequestRedraw();
    };
}

void UltraCanvasPaintSurface::UnhookDocument() {
    if (!document) return;
    document->onPixelsChanged = nullptr;
    document->onStructureChanged = nullptr;
    document->onSelectionChanged = nullptr;
}

void UltraCanvasPaintSurface::SetDocument(std::shared_ptr<UCRasterDocument> doc) {
    UnhookDocument();
    document = std::move(doc);
    HookDocument();
    fitPending = true;
    if (document && document->GetSelection().IsActive()) StartAntsTimer(); else StopAntsTimer();
    RequestRedraw();
}

// ===========================================================================
// VIEW
// ===========================================================================

void UltraCanvasPaintSurface::ClampView() {
    if (!document) return;
    const Rect2Df b = GetLocalBounds();
    const double imgW = document->GetWidth() * view.zoom;
    const double imgH = document->GetHeight() * view.zoom;
    // Keep at least a margin of the image on screen so it cannot be lost.
    const double margin = 32.0;
    view.originX = std::clamp(view.originX, margin - imgW, static_cast<double>(b.width) - margin);
    view.originY = std::clamp(view.originY, margin - imgH, static_cast<double>(b.height) - margin);
    // A small image is nicer centred.
    if (imgW <= b.width) view.originX = (b.width - imgW) * 0.5;
    if (imgH <= b.height) view.originY = (b.height - imgH) * 0.5;
}

void UltraCanvasPaintSurface::SetZoomAt(double zoom, const Point2Di& at) {
    zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
    if (!document) { view.zoom = zoom; return; }
    const Point2Dd img = view.ViewToImage(at.x, at.y);
    view.zoom = zoom;
    view.originX = at.x - img.x * zoom;
    view.originY = at.y - img.y * zoom;
    ClampView();
    fitPending = false;
    if (onViewChanged) onViewChanged();
    RequestRedraw();
}

void UltraCanvasPaintSurface::SetZoom(double zoom) {
    const Rect2Df b = GetLocalBounds();
    SetZoomAt(zoom, Point2Di(static_cast<int>(b.width * 0.5f), static_cast<int>(b.height * 0.5f)));
}

void UltraCanvasPaintSurface::ZoomStep(int direction) {
    const double cur = view.zoom;
    double next = cur;
    if (direction > 0) {
        next = kMaxZoom;
        for (double z : kZoomSteps) if (z > cur * 1.001) { next = z; break; }
    } else {
        next = kMinZoom;
        for (int i = static_cast<int>(sizeof(kZoomSteps) / sizeof(kZoomSteps[0])) - 1; i >= 0; --i)
            if (kZoomSteps[i] < cur * 0.999) { next = kZoomSteps[i]; break; }
    }
    if (hasHover) SetZoomAt(next, hoverPointer); else SetZoom(next);
}

void UltraCanvasPaintSurface::ZoomToFit() {
    if (!document) return;
    const Rect2Df b = GetLocalBounds();
    if (b.width <= 0 || b.height <= 0) { fitPending = true; return; }
    const double pad = 24.0;
    const double zx = (b.width - pad) / std::max(1, document->GetWidth());
    const double zy = (b.height - pad) / std::max(1, document->GetHeight());
    view.zoom = std::clamp(std::min(zx, zy), kMinZoom, kMaxZoom);
    CenterImage();
    fitPending = false;
}

void UltraCanvasPaintSurface::CenterImage() {
    if (!document) return;
    const Rect2Df b = GetLocalBounds();
    view.originX = (b.width - document->GetWidth() * view.zoom) * 0.5;
    view.originY = (b.height - document->GetHeight() * view.zoom) * 0.5;
    if (onViewChanged) onViewChanged();
    RequestRedraw();
}

void UltraCanvasPaintSurface::PanBy(double dx, double dy) {
    view.originX += dx;
    view.originY += dy;
    ClampView();
    if (onViewChanged) onViewChanged();
    RequestRedraw();
}

void UltraCanvasPaintSurface::ScrollToImagePoint(double ix, double iy) {
    const Rect2Df b = GetLocalBounds();
    view.originX = b.width * 0.5 - ix * view.zoom;
    view.originY = b.height * 0.5 - iy * view.zoom;
    ClampView();
    if (onViewChanged) onViewChanged();
    RequestRedraw();
}

// ===========================================================================
// MARCHING ANTS
// ===========================================================================

void UltraCanvasPaintSurface::StartAntsTimer() {
    if (antsTimer) return;
    auto* app = UltraCanvasApplication::GetInstance();
    if (!app) return;
    antsTimer = app->StartTimer(120, true, [this](TimerId) {
        antsOffset = std::fmod(antsOffset + 1.0, 8.0);
        if (IsVisible() && showSelection) RequestRedraw();
    });
}

void UltraCanvasPaintSurface::StopAntsTimer() {
    if (!antsTimer) return;
    if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(antsTimer);
    antsTimer = 0;
}

// ===========================================================================
// RENDER
// ===========================================================================

void UltraCanvasPaintSurface::DrawCheckerboard(IRenderContext* ctx, const Rect2Dd& area) {
    // Squares in view pixels so they do not scale with the zoom.
    const double sq = 8.0;
    ctx->DrawFilledRectangle(area, checkerLight, 0.0f);
    ctx->SetFillPaint(checkerDark);
    const int x0 = static_cast<int>(std::floor(area.x / sq));
    const int y0 = static_cast<int>(std::floor(area.y / sq));
    const int x1 = static_cast<int>(std::ceil((area.x + area.width) / sq));
    const int y1 = static_cast<int>(std::ceil((area.y + area.height) / sq));
    ctx->ClearPath();
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            if (((x + y) & 1) == 0) continue;
            const double rx = std::max(area.x, x * sq), ry = std::max(area.y, y * sq);
            const double rw = std::min(area.x + area.width, (x + 1) * sq) - rx;
            const double rh = std::min(area.y + area.height, (y + 1) * sq) - ry;
            if (rw > 0 && rh > 0) ctx->Rect(rx, ry, rw, rh);
        }
    }
    ctx->Fill();
}

void UltraCanvasPaintSurface::DrawPixelGrid(IRenderContext* ctx, const Rect2Dd& imageArea) {
    const Rect2Df b = GetLocalBounds();
    ctx->SetStrokePaint(Color(0, 0, 0, 70));
    ctx->SetStrokeWidth(1.0);
    ctx->SetLineDash(UCDashPattern());
    const double left = std::max(0.0, imageArea.x), top = std::max(0.0, imageArea.y);
    const double right = std::min(static_cast<double>(b.width), imageArea.x + imageArea.width);
    const double bottom = std::min(static_cast<double>(b.height), imageArea.y + imageArea.height);
    const int ix0 = static_cast<int>(std::ceil((left - view.originX) / view.zoom));
    const int ix1 = static_cast<int>(std::floor((right - view.originX) / view.zoom));
    const int iy0 = static_cast<int>(std::ceil((top - view.originY) / view.zoom));
    const int iy1 = static_cast<int>(std::floor((bottom - view.originY) / view.zoom));
    ctx->ClearPath();
    for (int ix = ix0; ix <= ix1; ++ix) {
        const double x = std::floor(view.originX + ix * view.zoom) + 0.5;
        ctx->MoveTo(x, top); ctx->LineTo(x, bottom);
    }
    for (int iy = iy0; iy <= iy1; ++iy) {
        const double y = std::floor(view.originY + iy * view.zoom) + 0.5;
        ctx->MoveTo(left, y); ctx->LineTo(right, y);
    }
    ctx->Stroke();
}

void UltraCanvasPaintSurface::DrawSelection(IRenderContext* ctx) {
    const UCRasterSelection& sel = document->GetSelection();
    if (!sel.IsActive()) return;
    const auto& segs = sel.GetOutline();
    if (segs.empty()) return;
    const Rect2Df b = GetLocalBounds();
    ctx->ClearPath();
    for (const auto& s : segs) {
        const Point2Dd a = view.ImageToView(s.x0, s.y0);
        const Point2Dd c = view.ImageToView(s.x1, s.y1);
        // skip segments entirely off-screen
        if ((a.x < 0 && c.x < 0) || (a.y < 0 && c.y < 0) ||
            (a.x > b.width && c.x > b.width) || (a.y > b.height && c.y > b.height)) continue;
        ctx->MoveTo(std::floor(a.x) + 0.5, std::floor(a.y) + 0.5);
        ctx->LineTo(std::floor(c.x) + 0.5, std::floor(c.y) + 0.5);
    }
    ctx->SetStrokeWidth(1.0);
    ctx->SetStrokePaint(Colors::White);
    ctx->SetLineDash(UCDashPattern());
    ctx->StrokePathPreserve();
    ctx->SetStrokePaint(Colors::Black);
    ctx->SetLineDash(UCDashPattern({4.0, 4.0}, antsOffset));
    ctx->Stroke();
    ctx->SetLineDash(UCDashPattern());
}

void UltraCanvasPaintSurface::DrawBrushCursor(IRenderContext* ctx) {
    if (cursorRadius <= 0.0 || !hasHover || panning || alwaysPan) return;
    const double r = std::max(1.5, cursorRadius * view.zoom);
    const Point2Dd c(hoverPointer.x, hoverPointer.y);
    ctx->SetStrokeWidth(1.0);
    ctx->SetLineDash(UCDashPattern());
    for (int pass = 0; pass < 2; ++pass) {
        ctx->SetStrokePaint(pass == 0 ? Color(255, 255, 255, 200) : Color(0, 0, 0, 200));
        const double rr = pass == 0 ? r + 1.0 : r;
        ctx->ClearPath();
        if (cursorSquare) ctx->Rect(c.x - rr, c.y - rr, rr * 2, rr * 2);
        else ctx->Circle(c.x, c.y, rr);
        ctx->Stroke();
    }
}

void UltraCanvasPaintSurface::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
    if (!IsVisible()) return;
    const Rect2Df b = GetLocalBounds();
    if (b.width <= 0 || b.height <= 0) return;

    ctx->PushState();
    ctx->ClipRect(Rect2Dd(b.x, b.y, b.width, b.height));
    ctx->DrawFilledRectangle(Rect2Dd(b.x, b.y, b.width, b.height), canvasColor, 0.0f);

    if (document && document->IsValid()) {
        if (fitPending) ZoomToFit();
        const double iw = document->GetWidth(), ih = document->GetHeight();
        const Rect2Dd imageArea = view.ImageToView(Rect2Dd(0, 0, iw, ih));
        // visible part of the image, in image pixels (whole pixels)
        const double vx0 = std::max(0.0, std::floor((0 - view.originX) / view.zoom));
        const double vy0 = std::max(0.0, std::floor((0 - view.originY) / view.zoom));
        const double vx1 = std::min(iw, std::ceil((b.width - view.originX) / view.zoom));
        const double vy1 = std::min(ih, std::ceil((b.height - view.originY) / view.zoom));
        if (vx1 > vx0 && vy1 > vy0) {
            const Rect2Dd src(vx0, vy0, vx1 - vx0, vy1 - vy0);
            const Rect2Dd dst = view.ImageToView(src);
            // drop shadow + checkerboard behind the image
            ctx->DrawFilledRectangle(Rect2Dd(imageArea.x + 3, imageArea.y + 3, imageArea.width, imageArea.height),
                                     Color(0, 0, 0, 90), 0.0f);
            DrawCheckerboard(ctx, dst);
            if (auto pm = document->GetCompositePixmap()) {
                // Nearest-neighbour above 200 % so pixels read as pixels;
                // smooth below so downscaled views do not shimmer.
                ctx->SetImageSmoothing(view.zoom < 2.0);
                ctx->DrawPartOfPixmap(*pm, src, dst);
                ctx->SetImageSmoothing(true);
            }
            if (showPixelGrid && view.zoom >= pixelGridThreshold) DrawPixelGrid(ctx, imageArea);
        }
        // image border
        ctx->SetStrokePaint(Color(0, 0, 0, 160));
        ctx->SetStrokeWidth(1.0);
        ctx->SetLineDash(UCDashPattern());
        ctx->DrawRectangle(Rect2Dd(std::floor(imageArea.x) - 0.5, std::floor(imageArea.y) - 0.5,
                                   std::ceil(imageArea.width) + 1.0, std::ceil(imageArea.height) + 1.0));
        if (showSelection) DrawSelection(ctx);
    }

    if (onDrawOverlay) onDrawOverlay(ctx, view);
    DrawBrushCursor(ctx);
    ctx->PopState();
}

// ===========================================================================
// EVENTS
// ===========================================================================

PaintPointerEvent UltraCanvasPaintSurface::MakePointerEvent(const UCEvent& e) const {
    PaintPointerEvent p;
    p.view = e.pointer;
    const Point2Dd img = view.ViewToImage(e.pointer.x, e.pointer.y);
    p.x = img.x; p.y = img.y;
    p.button = e.button;
    p.ctrl = e.ctrl; p.shift = e.shift; p.alt = e.alt;
    p.pressure = e.pressure > 0.0f ? e.pressure : 1.0f;
    p.insideImage = document && img.x >= 0 && img.y >= 0 &&
                    img.x < document->GetWidth() && img.y < document->GetHeight();
    return p;
}

bool UltraCanvasPaintSurface::OnEvent(const UCEvent& event) {
    switch (event.type) {
        case UCEventType::MouseWheel: {
            if (!Contains(event.pointer)) return false;
            if (event.ctrl || !event.shift) {
                // wheel zooms (the common editor convention); shift+wheel pans
                if (event.wheelDelta == 0) return true;
                const double factor = event.wheelDelta > 0 ? 1.15 : 1.0 / 1.15;
                hoverPointer = event.pointer; hasHover = true;
                SetZoomAt(view.zoom * factor, event.pointer);
            } else {
                PanBy(0.0, event.wheelDelta > 0 ? 40.0 : -40.0);
            }
            return true;
        }
        case UCEventType::MouseWheelHorizontal:
            if (!Contains(event.pointer)) return false;
            PanBy(event.wheelDelta > 0 ? 40.0 : -40.0, 0.0);
            return true;

        case UCEventType::MouseDown: {
            if (!Contains(event.pointer)) return false;
            SetFocus(true);
            lastPointer = event.pointer;
            const bool wantPan = alwaysPan || spaceDown || event.button == UCMouseButton::Middle;
            if (auto* app = UltraCanvasApplication::GetInstance()) app->CaptureMouse(this);
            if (wantPan) {
                panning = true;
                return true;
            }
            toolDragging = true;
            dragButton = event.button;
            if (onToolPress) onToolPress(MakePointerEvent(event));
            return true;
        }
        case UCEventType::MouseDoubleClick:
            if (!Contains(event.pointer)) return false;
            if (onToolDoubleClick) onToolDoubleClick(MakePointerEvent(event));
            return true;

        case UCEventType::MouseMove: {
            hoverPointer = event.pointer;
            hasHover = Contains(event.pointer) || panning || toolDragging;
            if (panning) {
                PanBy(event.pointer.x - lastPointer.x, event.pointer.y - lastPointer.y);
                lastPointer = event.pointer;
                return true;
            }
            lastPointer = event.pointer;
            if (toolDragging) {
                if (onToolDrag) onToolDrag(MakePointerEvent(event));
                return true;
            }
            if (hasHover) {
                if (onToolHover) onToolHover(MakePointerEvent(event));
                if (cursorRadius > 0.0) RequestRedraw();
            }
            return false;
        }
        case UCEventType::MouseLeave:
            hasHover = false;
            if (cursorRadius > 0.0) RequestRedraw();
            return false;

        case UCEventType::MouseUp: {
            if (panning) {
                panning = false;
                if (auto* app = UltraCanvasApplication::GetInstance()) app->ReleaseMouse();
                return true;
            }
            if (toolDragging) {
                toolDragging = false;
                if (auto* app = UltraCanvasApplication::GetInstance()) app->ReleaseMouse();
                PaintPointerEvent p = MakePointerEvent(event);
                if (p.button == UCMouseButton::NoneButton) p.button = dragButton;
                if (onToolRelease) onToolRelease(p);
                dragButton = UCMouseButton::NoneButton;
                return true;
            }
            return false;
        }

        case UCEventType::KeyDown: {
            if (event.virtualKey == UCKeys::Space && !event.ctrl) {
                if (!spaceDown) { spaceDown = true; if (!alwaysPan) SetMouseCursor(UCMouseCursor::Hand); }
                return true;
            }
            if (onToolKey && onToolKey(event)) return true;
            // Zoom keys the tool did not want.
            if (event.character == '+' || event.character == '=' || event.virtualKey == UCKeys::Plus) { ZoomIn(); return true; }
            if (event.character == '-' || event.virtualKey == UCKeys::Minus) { ZoomOut(); return true; }
            if (event.ctrl && event.virtualKey == UCKeys::Key0) { ZoomToFit(); return true; }
            if (event.ctrl && event.virtualKey == UCKeys::Key1) { ZoomToActual(); return true; }
            return false;
        }
        case UCEventType::KeyUp:
            if (event.virtualKey == UCKeys::Space) {
                spaceDown = false;
                if (!alwaysPan) SetMouseCursor(toolCursor);
                return true;
            }
            return false;

        case UCEventType::Drop:
            if (onFilesDropped && !event.droppedFiles.empty()) { onFilesDropped(event.droppedFiles); return true; }
            return false;

        case UCEventType::WindowResize:
            ClampView();
            return false;

        default:
            break;
    }
    return UltraCanvasUIElement::OnEvent(event);
}

} // namespace UltraCanvas
