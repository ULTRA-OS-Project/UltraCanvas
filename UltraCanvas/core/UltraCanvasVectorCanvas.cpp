// core/UltraCanvasVectorCanvas.cpp
// Editable vector drawing view: pasteboard, page, grid, rulers, guides,
// snapping, the selection's handles, and pointer events forwarded to the
// active tool in document coordinates. See the header.
// Version: 1.0.0
// Last Modified: 2026-09-15
// Author: UltraCanvas Framework

#include "UltraCanvasVectorCanvas.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasRenderContext.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace UltraCanvas {

namespace {
    const double kZoomSteps[] = { 0.05, 0.1, 0.15, 0.25, 0.33, 0.5, 0.67, 0.75, 1.0, 1.25, 1.5, 2.0,
                                  3.0, 4.0, 6.0, 8.0, 12.0, 16.0, 24.0, 32.0, 64.0 };
    constexpr double kMinZoom = 0.01;
    constexpr double kMaxZoom = 256.0;

    bool EmptyBox(const Rect2Dd& b) { return b.width <= 0 && b.height <= 0 && b.x == 0 && b.y == 0; }

    // The nicest tick step (1, 2, 5 x 10^n in ruler units) whose major
    // spacing is at least `minPixels` on screen.
    double NiceStep(double pixelsPerUnit, double minPixels) {
        const double raw = minPixels / std::max(pixelsPerUnit, 1e-9);
        const double mag = std::pow(10.0, std::floor(std::log10(std::max(raw, 1e-9))));
        for (double k : {1.0, 2.0, 5.0, 10.0}) if (k * mag >= raw) return k * mag;
        return 10.0 * mag;
    }

    std::string FormatTick(double v) {
        char buf[32];
        if (std::fabs(v - std::round(v)) < 1e-6) std::snprintf(buf, sizeof buf, "%.0f", v);
        else std::snprintf(buf, sizeof buf, "%.2g", v);
        return buf;
    }
}

UltraCanvasVectorCanvas::UltraCanvasVectorCanvas(const std::string& elemId)
    : UltraCanvasVectorCanvas(elemId, 0.0f, 0.0f, 0.0f, 0.0f) {}

UltraCanvasVectorCanvas::UltraCanvasVectorCanvas(const std::string& elemId,
                                                 float x, float y, float w, float h)
    : UltraCanvasUIElement(elemId, x, y, w, h),
      renderer(std::make_unique<VectorRenderer>()) {
    SetMouseCursor(UCMouseCursor::Arrow);
    selection = std::make_shared<VectorEdit::VectorSelection>();
    HookSelection();
}

UltraCanvasVectorCanvas::~UltraCanvasVectorCanvas() {
    UnhookSelection();
}

// ===========================================================================
// DOCUMENT AND SELECTION
// ===========================================================================

void UltraCanvasVectorCanvas::SetDocument(std::shared_ptr<VectorStorage::VectorDocument> doc) {
    document = std::move(doc);
    if (selection) selection->Clear();
    rotationCenter.reset();
    fitPending = true;
    RequestRedraw();
}

void UltraCanvasVectorCanvas::HookSelection() {
    if (!selection) return;
    selectionListener = selection->AddListener([this]() {
        rotationCenter.reset();
        RequestRedraw();
    });
}

void UltraCanvasVectorCanvas::UnhookSelection() {
    if (selection && selectionListener) selection->RemoveListener(selectionListener);
    selectionListener = 0;
}

void UltraCanvasVectorCanvas::SetSelection(std::shared_ptr<VectorEdit::VectorSelection> sel) {
    UnhookSelection();
    selection = sel ? std::move(sel) : std::make_shared<VectorEdit::VectorSelection>();
    HookSelection();
    RequestRedraw();
}

// ===========================================================================
// VIEW
// ===========================================================================

Rect2Dd UltraCanvasVectorCanvas::CanvasArea() const {
    const Rect2Df b = GetLocalBounds();
    const double r = showRulers ? rulerSize : 0.0;
    return Rect2Dd(r, r, std::max(0.0, b.width - r), std::max(0.0, b.height - r));
}

Rect2Dd UltraCanvasVectorCanvas::VisibleDocRect() const {
    return view.ViewToDoc(CanvasArea());
}

void UltraCanvasVectorCanvas::ClampView() {
    // The page (or the drawing) may never leave the canvas entirely.
    Rect2Dd content = document ? Rect2Dd(0, 0, document->Size.width, document->Size.height) : Rect2Dd(0, 0, 0, 0);
    if (document && (content.width <= 0 || content.height <= 0)) content = document->GetBoundingBox();
    if (EmptyBox(content)) return;
    const Rect2Dd area = CanvasArea();
    const double w = content.width * view.zoom, h = content.height * view.zoom;
    const double margin = 32.0;
    const double minX = area.x + margin - (content.x * view.zoom + w);
    const double maxX = area.x + area.width - margin - content.x * view.zoom;
    const double minY = area.y + margin - (content.y * view.zoom + h);
    const double maxY = area.y + area.height - margin - content.y * view.zoom;
    view.originX = std::clamp(view.originX, std::min(minX, maxX), std::max(minX, maxX));
    view.originY = std::clamp(view.originY, std::min(minY, maxY), std::max(minY, maxY));
}

void UltraCanvasVectorCanvas::SetZoomAt(double zoom, const Point2Di& at) {
    zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
    const Point2Dd doc = view.ViewToDoc(Point2Dd(at.x, at.y));
    view.zoom = zoom;
    view.originX = at.x - doc.x * zoom;
    view.originY = at.y - doc.y * zoom;
    ClampView();
    fitPending = false;
    if (onViewChanged) onViewChanged();
    RequestRedraw();
}

void UltraCanvasVectorCanvas::SetZoom(double zoom) {
    const Rect2Dd a = CanvasArea();
    SetZoomAt(zoom, Point2Di(static_cast<int>(a.x + a.width * 0.5), static_cast<int>(a.y + a.height * 0.5)));
}

void UltraCanvasVectorCanvas::ZoomStep(int direction) {
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
    if (hasHover && CanvasArea().Contains(Point2Dd(hoverPointer.x, hoverPointer.y))) SetZoomAt(next, hoverPointer);
    else SetZoom(next);
}

void UltraCanvasVectorCanvas::ZoomToRect(const Rect2Dd& docRect, double margin) {
    const Rect2Dd area = CanvasArea();
    if (EmptyBox(docRect) || docRect.width <= 0 || docRect.height <= 0 || area.width <= 0 || area.height <= 0) return;
    const double zx = (area.width - 2 * margin) / docRect.width;
    const double zy = (area.height - 2 * margin) / docRect.height;
    view.zoom = std::clamp(std::min(zx, zy), kMinZoom, kMaxZoom);
    view.originX = area.x + (area.width - docRect.width * view.zoom) / 2 - docRect.x * view.zoom;
    view.originY = area.y + (area.height - docRect.height * view.zoom) / 2 - docRect.y * view.zoom;
    fitPending = false;
    if (onViewChanged) onViewChanged();
    RequestRedraw();
}

void UltraCanvasVectorCanvas::ZoomToPage() {
    if (!document) return;
    ZoomToRect(Rect2Dd(0, 0, document->Size.width, document->Size.height), 24.0);
}

void UltraCanvasVectorCanvas::ZoomToDrawing() {
    if (!document) return;
    Rect2Dd b = document->GetBoundingBox();
    if (EmptyBox(b) || b.width <= 0 || b.height <= 0) { ZoomToPage(); return; }
    ZoomToRect(b, 24.0);
}

void UltraCanvasVectorCanvas::ZoomToSelection() {
    const Rect2Dd b = SelectionBounds();
    if (EmptyBox(b) || b.width <= 0 || b.height <= 0) return;
    ZoomToRect(b, 40.0);
}

void UltraCanvasVectorCanvas::CenterOn(const Point2Dd& docPoint) {
    const Rect2Dd area = CanvasArea();
    view.originX = area.x + area.width / 2 - docPoint.x * view.zoom;
    view.originY = area.y + area.height / 2 - docPoint.y * view.zoom;
    ClampView();
    if (onViewChanged) onViewChanged();
    RequestRedraw();
}

void UltraCanvasVectorCanvas::PanBy(double dx, double dy) {
    view.originX += dx;
    view.originY += dy;
    ClampView();
    fitPending = false;
    if (onViewChanged) onViewChanged();
    RequestRedraw();
}

void UltraCanvasVectorCanvas::InvalidateDoc(const Rect2Dd& docRect) {
    if (EmptyBox(docRect)) { RequestRedraw(); return; }
    const Rect2Dd v = view.DocToView(docRect);
    const double pad = handleSize + 4;
    InvalidateRect(Rect2Df(static_cast<float>(std::floor(v.x - pad)), static_cast<float>(std::floor(v.y - pad)),
                           static_cast<float>(std::ceil(v.width + 2 * pad)), static_cast<float>(std::ceil(v.height + 2 * pad))));
}

// ===========================================================================
// WORKSPACE
// ===========================================================================

void UltraCanvasVectorCanvas::SetShowRulers(bool show) {
    if (showRulers == show) return;
    showRulers = show;
    ClampView();
    RequestRedraw();
}

void UltraCanvasVectorCanvas::SetRulerUnit(double pointsPerUnit, const std::string& symbol) {
    rulerPointsPerUnit = pointsPerUnit > 0 ? pointsPerUnit : 1.0;
    rulerUnitSymbol = symbol;
    RequestRedraw();
}

void UltraCanvasVectorCanvas::SetGrid(const VectorGridSpec& spec) {
    grid = spec;
    if (grid.spacing <= 0) grid.spacing = 10.0;
    if (grid.subdivisions < 1) grid.subdivisions = 1;
    RequestRedraw();
}

void UltraCanvasVectorCanvas::SetShowGuides(bool show) { showGuides = show; RequestRedraw(); }

void UltraCanvasVectorCanvas::SetGuides(const std::vector<VectorGuide>& list) {
    guides = list;
    if (onGuidesChanged) onGuidesChanged();
    RequestRedraw();
}

int UltraCanvasVectorCanvas::AddGuide(const VectorGuide& guide) {
    guides.push_back(guide);
    if (onGuidesChanged) onGuidesChanged();
    RequestRedraw();
    return static_cast<int>(guides.size()) - 1;
}

void UltraCanvasVectorCanvas::RemoveGuide(int index) {
    if (index < 0 || index >= static_cast<int>(guides.size())) return;
    guides.erase(guides.begin() + index);
    if (onGuidesChanged) onGuidesChanged();
    RequestRedraw();
}

void UltraCanvasVectorCanvas::ClearGuides() {
    if (guides.empty()) return;
    guides.clear();
    if (onGuidesChanged) onGuidesChanged();
    RequestRedraw();
}

Point2Dd UltraCanvasVectorCanvas::Snap(const Point2Dd& doc, VectorSnapResult* result) const {
    VectorSnapResult res;
    Point2Dd out = doc;
    const double radius = view.ViewToDocLength(snapOptions.radiusPixels);
    double bestX = radius, bestY = radius;

    auto tryX = [&](double x, VectorSnapResult::Kind kind) {
        const double d = std::fabs(x - doc.x);
        if (d <= bestX) { bestX = d; out.x = x; res.x = kind; }
    };
    auto tryY = [&](double y, VectorSnapResult::Kind kind) {
        const double d = std::fabs(y - doc.y);
        if (d <= bestY) { bestY = d; out.y = y; res.y = kind; }
    };

    // Precedence: guides, then page, then objects, then grid - later
    // candidates win only when strictly closer, so a guide beats a grid
    // line at the same distance.
    if (snapOptions.toGuides && showGuides) {
        for (const auto& g : guides) {
            if (g.horizontal) tryY(g.position, VectorSnapResult::Kind::Guide);
            else tryX(g.position, VectorSnapResult::Kind::Guide);
        }
    }
    if (snapOptions.toPage && document) {
        const double w = document->Size.width, h = document->Size.height;
        for (double x : {0.0, w / 2, w}) tryX(x, VectorSnapResult::Kind::Page);
        for (double y : {0.0, h / 2, h}) tryY(y, VectorSnapResult::Kind::Page);
    }
    if (snapOptions.toObjects && document) {
        for (const auto& layer : document->Layers) {
            if (!layer || !layer->Visible) continue;
            for (const auto& child : layer->Children) {
                if (!child || (selection && selection->Contains(child))) continue;
                const Rect2Dd b = VectorEdit::DocumentBounds(child);
                if (EmptyBox(b)) continue;
                for (double x : {b.x, b.x + b.width / 2, b.x + b.width}) tryX(x, VectorSnapResult::Kind::Object);
                for (double y : {b.y, b.y + b.height / 2, b.y + b.height}) tryY(y, VectorSnapResult::Kind::Object);
            }
        }
    }
    if (snapOptions.toGrid && grid.spacing > 0) {
        const double step = grid.spacing / std::max(1, grid.subdivisions);
        tryX(std::round(doc.x / step) * step, VectorSnapResult::Kind::Grid);
        tryY(std::round(doc.y / step) * step, VectorSnapResult::Kind::Grid);
    }
    if (result) *result = res;
    return out;
}

// ===========================================================================
// SELECTION DISPLAY
// ===========================================================================

Rect2Dd UltraCanvasVectorCanvas::SelectionBounds() const {
    return selection ? selection->Bounds() : Rect2Dd(0, 0, 0, 0);
}

void UltraCanvasVectorCanvas::SetHandleMode(VectorHandleMode mode) {
    if (handleMode == mode) return;
    handleMode = mode;
    RequestRedraw();
}

void UltraCanvasVectorCanvas::SetRotationCenter(const std::optional<Point2Dd>& docPoint) {
    rotationCenter = docPoint;
    RequestRedraw();
}

Point2Dd UltraCanvasVectorCanvas::GetRotationCenter() const {
    if (rotationCenter) return *rotationCenter;
    const Rect2Dd b = SelectionBounds();
    return Point2Dd(b.x + b.width / 2, b.y + b.height / 2);
}

Point2Dd UltraCanvasVectorCanvas::HandlePoint(VectorHandle h) const {
    const Rect2Dd b = SelectionBounds();
    const double x0 = b.x, x1 = b.x + b.width, xm = b.x + b.width / 2;
    const double y0 = b.y, y1 = b.y + b.height, ym = b.y + b.height / 2;
    switch (h) {
        case VectorHandle::TopLeft: return {x0, y0};
        case VectorHandle::Top: return {xm, y0};
        case VectorHandle::TopRight: return {x1, y0};
        case VectorHandle::Right: return {x1, ym};
        case VectorHandle::BottomRight: return {x1, y1};
        case VectorHandle::Bottom: return {xm, y1};
        case VectorHandle::BottomLeft: return {x0, y1};
        case VectorHandle::Left: return {x0, ym};
        case VectorHandle::Center: return GetRotationCenter();
        default: return {xm, ym};
    }
}

Rect2Dd UltraCanvasVectorCanvas::HandleRect(VectorHandle h) const {
    const Point2Dd p = view.DocToView(HandlePoint(h));
    const double s = handleSize;
    return Rect2Dd(p.x - s / 2, p.y - s / 2, s, s);
}

VectorHandle UltraCanvasVectorCanvas::HitTestHandle(const Point2Di& vp) const {
    if (!showSelection || !selection || selection->Empty()) return VectorHandle::NoHandle;
    const Rect2Dd b = SelectionBounds();
    if (EmptyBox(b)) return VectorHandle::NoHandle;
    const Point2Dd p(vp.x, vp.y);
    const double slop = 2.0;
    auto within = [&](VectorHandle h) {
        const Rect2Dd r = HandleRect(h);
        return p.x >= r.x - slop && p.x <= r.x + r.width + slop && p.y >= r.y - slop && p.y <= r.y + r.height + slop;
    };
    if (handleMode == VectorHandleMode::Rotate && within(VectorHandle::Center)) return VectorHandle::Center;
    for (VectorHandle h : {VectorHandle::TopLeft, VectorHandle::TopRight, VectorHandle::BottomRight, VectorHandle::BottomLeft,
                           VectorHandle::Top, VectorHandle::Right, VectorHandle::Bottom, VectorHandle::Left})
        if (within(h)) return h;
    const Rect2Dd vb = view.DocToView(b);
    if (p.x >= vb.x && p.x <= vb.x + vb.width && p.y >= vb.y && p.y <= vb.y + vb.height) return VectorHandle::Body;
    return VectorHandle::NoHandle;
}

// ===========================================================================
// HIT TESTING
// ===========================================================================

std::optional<VectorEdit::VectorHit> UltraCanvasVectorCanvas::HitTest(const Point2Dd& doc, double tolerancePixels) const {
    if (!document) return std::nullopt;
    return hitTester.HitTest(*document, doc, view.ViewToDocLength(tolerancePixels));
}

std::vector<VectorEdit::ElementPtr> UltraCanvasVectorCanvas::ElementsIn(const Rect2Dd& docRect, bool fullyInside) const {
    if (!document) return {};
    return hitTester.ElementsIn(*document, docRect, fullyInside);
}

// ===========================================================================
// RENDER
// ===========================================================================

void UltraCanvasVectorCanvas::DrawPage(IRenderContext* ctx) {
    if (!document || !showPage) return;
    const Rect2Dd page = view.DocToView(Rect2Dd(0, 0, document->Size.width, document->Size.height));
    if (page.width <= 0 || page.height <= 0) return;
    ctx->SetFillPaint(Color(0, 0, 0, 40));
    ctx->FillRectangle(Rect2Dd(page.x + 3, page.y + 3, page.width, page.height));
    ctx->SetFillPaint(document->BackgroundColor.value_or(pageColor));
    ctx->FillRectangle(page);
    ctx->SetStrokePaint(Color(0, 0, 0, 90));
    ctx->SetStrokeWidth(1.0);
    ctx->SetLineDash(UCDashPattern());
    ctx->DrawRectangle(Rect2Dd(page.x + 0.5, page.y + 0.5, page.width, page.height));
}

void UltraCanvasVectorCanvas::DrawGrid(IRenderContext* ctx, const Rect2Dd& area) {
    if (!grid.visible || grid.spacing <= 0) return;
    const double majorPx = grid.spacing * view.zoom;
    if (majorPx < 2.0) return;
    const Rect2Dd docRect = view.ViewToDoc(area);
    ctx->SetStrokeWidth(1.0);
    ctx->SetLineDash(UCDashPattern());
    auto drawLines = [&](double step, const Color& color, bool skipMajor) {
        ctx->SetStrokePaint(color);
        const double x0 = std::floor(docRect.x / step) * step;
        const double y0 = std::floor(docRect.y / step) * step;
        for (double x = x0; x <= docRect.x + docRect.width; x += step) {
            if (skipMajor && std::fabs(std::remainder(x, grid.spacing)) < 1e-9) continue;
            const double vx = std::round(view.DocToView(Point2Dd(x, 0)).x) + 0.5;
            ctx->DrawLine(Point2Dd(vx, area.y), Point2Dd(vx, area.y + area.height));
        }
        for (double y = y0; y <= docRect.y + docRect.height; y += step) {
            if (skipMajor && std::fabs(std::remainder(y, grid.spacing)) < 1e-9) continue;
            const double vy = std::round(view.DocToView(Point2Dd(0, y)).y) + 0.5;
            ctx->DrawLine(Point2Dd(area.x, vy), Point2Dd(area.x + area.width, vy));
        }
    };
    if (grid.subdivisions > 1 && majorPx / grid.subdivisions >= grid.minPixelSpacing)
        drawLines(grid.spacing / grid.subdivisions, grid.minorColor, true);
    drawLines(grid.spacing, grid.majorColor, false);
}

void UltraCanvasVectorCanvas::DrawDocument(IRenderContext* ctx, const Rect2Dd& area) {
    if (!document) return;
    ctx->PushState();
    ctx->Translate(view.originX, view.originY);
    ctx->Scale(view.zoom, view.zoom);
    VectorRenderOptions opts = renderer->GetOptions();
    opts.ViewportBounds = view.ViewToDoc(area);
    opts.ClipToViewport = true;
    opts.EnableCulling = true;
    renderer->SetOptions(opts);
    // The renderer maps the document's ViewBox onto its viewport; we have
    // already placed the document, so render layers directly.
    for (const auto& layer : document->Layers)
        if (layer && layer->Visible) renderer->RenderLayer(ctx, *layer);
    ctx->PopState();
}

void UltraCanvasVectorCanvas::DrawGuides(IRenderContext* ctx, const Rect2Dd& area) {
    if (!showGuides || guides.empty()) return;
    ctx->SetStrokePaint(guideColor);
    ctx->SetStrokeWidth(1.0);
    ctx->SetLineDash(UCDashPattern({4.0, 3.0}, 0.0));
    for (const auto& g : guides) {
        if (g.horizontal) {
            const double vy = std::round(view.DocToView(Point2Dd(0, g.position)).y) + 0.5;
            if (vy < area.y || vy > area.y + area.height) continue;
            ctx->DrawLine(Point2Dd(area.x, vy), Point2Dd(area.x + area.width, vy));
        } else {
            const double vx = std::round(view.DocToView(Point2Dd(g.position, 0)).x) + 0.5;
            if (vx < area.x || vx > area.x + area.width) continue;
            ctx->DrawLine(Point2Dd(vx, area.y), Point2Dd(vx, area.y + area.height));
        }
    }
    ctx->SetLineDash(UCDashPattern());
}

void UltraCanvasVectorCanvas::DrawSelectionHandles(IRenderContext* ctx) {
    if (!showSelection || !selection || selection->Empty()) return;
    const Rect2Dd b = SelectionBounds();
    if (EmptyBox(b)) return;
    const Rect2Dd vb = view.DocToView(b);
    // The bounds, as a thin blue rectangle.
    ctx->SetStrokePaint(Color(30, 100, 220, 160));
    ctx->SetStrokeWidth(1.0);
    ctx->SetLineDash(UCDashPattern());
    ctx->DrawRectangle(Rect2Dd(std::round(vb.x) + 0.5, std::round(vb.y) + 0.5, std::round(vb.width), std::round(vb.height)));
    // Per-element outlines when several are selected.
    if (selection->Count() > 1) {
        ctx->SetStrokePaint(Color(30, 100, 220, 90));
        for (const auto& e : selection->Elements()) {
            const Rect2Dd eb = view.DocToView(VectorEdit::DocumentBounds(e));
            if (EmptyBox(eb)) continue;
            ctx->DrawRectangle(Rect2Dd(std::round(eb.x) + 0.5, std::round(eb.y) + 0.5, std::round(eb.width), std::round(eb.height)));
        }
    }
    const VectorHandle corners[] = {VectorHandle::TopLeft, VectorHandle::TopRight, VectorHandle::BottomRight, VectorHandle::BottomLeft};
    const VectorHandle edges[] = {VectorHandle::Top, VectorHandle::Right, VectorHandle::Bottom, VectorHandle::Left};
    ctx->SetStrokePaint(handleBorder);
    ctx->SetFillPaint(handleColor);
    if (handleMode == VectorHandleMode::Scale) {
        for (VectorHandle h : corners) { const Rect2Dd r = HandleRect(h); ctx->FillRectangle(r); ctx->DrawRectangle(r); }
        for (VectorHandle h : edges) { const Rect2Dd r = HandleRect(h); ctx->FillRectangle(r); ctx->DrawRectangle(r); }
    } else {
        // Rotate: round corner handles; skew: diamond edge handles; and
        // the rotation centre as a cross-hair circle.
        for (VectorHandle h : corners) {
            const Rect2Dd r = HandleRect(h);
            const Point2Dd c(r.x + r.width / 2, r.y + r.height / 2);
            ctx->FillCircle(c, r.width / 2);
            ctx->DrawCircle(c, r.width / 2);
        }
        for (VectorHandle h : edges) {
            const Rect2Dd r = HandleRect(h);
            const Point2Dd c(r.x + r.width / 2, r.y + r.height / 2);
            ctx->ClearPath();
            ctx->MoveTo(c.x, r.y); ctx->LineTo(r.x + r.width, c.y); ctx->LineTo(c.x, r.y + r.height); ctx->LineTo(r.x, c.y); ctx->ClosePath();
            ctx->FillPathPreserve();
            ctx->StrokePathPreserve();
            ctx->ClearPath();
        }
        const Point2Dd c = view.DocToView(GetRotationCenter());
        const double r = handleSize * 0.6;
        ctx->DrawCircle(c, r);
        ctx->DrawLine(Point2Dd(c.x - r * 1.6, c.y), Point2Dd(c.x + r * 1.6, c.y));
        ctx->DrawLine(Point2Dd(c.x, c.y - r * 1.6), Point2Dd(c.x, c.y + r * 1.6));
    }
}

void UltraCanvasVectorCanvas::DrawRulers(IRenderContext* ctx) {
    if (!showRulers) return;
    const Rect2Df b = GetLocalBounds();
    const double rs = rulerSize;
    const Color bg(245, 245, 247, 255), line(150, 150, 155, 255), tick(90, 90, 95, 255), text(60, 60, 65, 255);
    ctx->SetFillPaint(bg);
    ctx->FillRectangle(Rect2Dd(0, 0, b.width, rs));
    ctx->FillRectangle(Rect2Dd(0, 0, rs, b.height));
    ctx->SetStrokePaint(line);
    ctx->SetStrokeWidth(1.0);
    ctx->SetLineDash(UCDashPattern());
    ctx->DrawLine(Point2Dd(0, rs - 0.5), Point2Dd(b.width, rs - 0.5));
    ctx->DrawLine(Point2Dd(rs - 0.5, 0), Point2Dd(rs - 0.5, b.height));

    const double pixelsPerUnit = view.zoom * rulerPointsPerUnit;
    const double major = NiceStep(pixelsPerUnit, 60.0);
    const double minor = major / (major / std::pow(10.0, std::floor(std::log10(major))) == 2.0 ? 4.0 : 5.0);
    ctx->SetFontSize(9.0);
    ctx->SetTextPaint(text);
    ctx->SetStrokePaint(tick);

    // Horizontal ruler.
    {
        const double docLeft = (rs - view.originX) / view.zoom / rulerPointsPerUnit;
        const double docRight = (b.width - view.originX) / view.zoom / rulerPointsPerUnit;
        const double start = std::floor(docLeft / minor) * minor;
        for (double u = start; u <= docRight; u += minor) {
            const double vx = std::round(view.originX + u * rulerPointsPerUnit * view.zoom) + 0.5;
            if (vx < rs) continue;
            const bool isMajor = std::fabs(std::remainder(u, major)) < minor * 0.01;
            const double h = isMajor ? rs * 0.55 : rs * 0.25;
            ctx->DrawLine(Point2Dd(vx, rs - h), Point2Dd(vx, rs));
            if (isMajor) ctx->DrawText(FormatTick(u), Point2Dd(vx + 2, 1));
        }
    }
    // Vertical ruler.
    {
        const double docTop = (rs - view.originY) / view.zoom / rulerPointsPerUnit;
        const double docBottom = (b.height - view.originY) / view.zoom / rulerPointsPerUnit;
        const double start = std::floor(docTop / minor) * minor;
        for (double u = start; u <= docBottom; u += minor) {
            const double vy = std::round(view.originY + u * rulerPointsPerUnit * view.zoom) + 0.5;
            if (vy < rs) continue;
            const bool isMajor = std::fabs(std::remainder(u, major)) < minor * 0.01;
            const double w = isMajor ? rs * 0.55 : rs * 0.25;
            ctx->DrawLine(Point2Dd(rs - w, vy), Point2Dd(rs, vy));
            if (isMajor) ctx->DrawText(FormatTick(u), Point2Dd(1, vy + 1));
        }
    }
    // Corner: the unit.
    ctx->SetFillPaint(bg);
    ctx->FillRectangle(Rect2Dd(0, 0, rs - 1, rs - 1));
    ctx->DrawText(rulerUnitSymbol, Point2Dd(2, 4));
    // Pointer position marks.
    if (hasHover) {
        ctx->SetStrokePaint(Color(30, 100, 220, 255));
        ctx->DrawLine(Point2Dd(hoverPointer.x + 0.5, 0), Point2Dd(hoverPointer.x + 0.5, rs));
        ctx->DrawLine(Point2Dd(0, hoverPointer.y + 0.5), Point2Dd(rs, hoverPointer.y + 0.5));
    }
}

void UltraCanvasVectorCanvas::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    (void)dirtyRect;
    if (!IsVisible()) return;
    const Rect2Df b = GetLocalBounds();
    if (fitPending && document && b.width > 0 && b.height > 0) ZoomToPage();
    const Rect2Dd area = CanvasArea();

    ctx->PushState();
    ctx->SetFillPaint(pasteboardColor);
    ctx->FillRectangle(Rect2Dd(0, 0, b.width, b.height));
    ctx->ClipRect(area);
    DrawPage(ctx);
    DrawGrid(ctx, area);
    DrawDocument(ctx, area);
    DrawGuides(ctx, area);
    DrawSelectionHandles(ctx);
    if (onDrawOverlay) {
        ctx->PushState();
        onDrawOverlay(ctx, view);
        ctx->PopState();
    }
    ctx->PopState();
    ctx->PushState();
    DrawRulers(ctx);
    ctx->PopState();
}

// ===========================================================================
// EVENTS
// ===========================================================================

VectorPointerEvent UltraCanvasVectorCanvas::MakePointerEvent(const UCEvent& e) const {
    VectorPointerEvent p;
    p.view = e.pointer;
    p.doc = view.ViewToDoc(Point2Dd(e.pointer.x, e.pointer.y));
    p.snapped = Snap(p.doc, &p.snap);
    p.button = e.button;
    p.ctrl = e.ctrl;
    p.shift = e.shift;
    p.alt = e.alt;
    p.pressure = e.pressure;
    p.insidePage = document && p.doc.x >= 0 && p.doc.y >= 0 &&
                   p.doc.x <= document->Size.width && p.doc.y <= document->Size.height;
    return p;
}

bool UltraCanvasVectorCanvas::PointInRuler(const Point2Di& p, bool& horizontalRuler) const {
    if (!showRulers) return false;
    if (p.y >= 0 && p.y < rulerSize && p.x >= rulerSize) { horizontalRuler = true; return true; }
    if (p.x >= 0 && p.x < rulerSize && p.y >= rulerSize) { horizontalRuler = false; return true; }
    return false;
}

int UltraCanvasVectorCanvas::GuideNear(const Point2Di& vp, double tolerancePx) const {
    if (!showGuides) return -1;
    int best = -1;
    double bestD = tolerancePx;
    for (size_t i = 0; i < guides.size(); ++i) {
        const auto& g = guides[i];
        const double d = g.horizontal ? std::fabs(view.DocToView(Point2Dd(0, g.position)).y - vp.y)
                                      : std::fabs(view.DocToView(Point2Dd(g.position, 0)).x - vp.x);
        if (d <= bestD) { bestD = d; best = static_cast<int>(i); }
    }
    return best;
}

void UltraCanvasVectorCanvas::SetPanMode(bool enabled) {
    alwaysPan = enabled;
    SetMouseCursor(enabled ? UCMouseCursor::Hand : toolCursor);
}

void UltraCanvasVectorCanvas::SetToolCursor(UCMouseCursor cursor) {
    toolCursor = cursor;
    if (!alwaysPan && !spaceDown) SetMouseCursor(cursor);
}

bool UltraCanvasVectorCanvas::OnEvent(const UCEvent& event) {
    switch (event.type) {
        case UCEventType::MouseWheel: {
            if (!Contains(event.pointer)) return false;
            if (event.ctrl || !event.shift) {
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
            if (auto* app = UltraCanvasApplication::GetInstance()) app->CaptureMouse(this);
            bool horizontalRuler = false;
            if (guidesDraggable && event.button == UCMouseButton::Left && PointInRuler(event.pointer, horizontalRuler)) {
                // Pull a new guide out of the ruler.
                VectorGuide g;
                g.horizontal = horizontalRuler;
                g.position = horizontalRuler ? view.ViewToDoc(Point2Dd(0, event.pointer.y)).y
                                             : view.ViewToDoc(Point2Dd(event.pointer.x, 0)).x;
                guides.push_back(g);
                draggingGuide = static_cast<int>(guides.size()) - 1;
                creatingGuide = true;
                RequestRedraw();
                return true;
            }
            const bool wantPan = alwaysPan || spaceDown || event.button == UCMouseButton::Middle;
            if (wantPan) { panning = true; return true; }
            if (guidesDraggable && event.button == UCMouseButton::Left && event.alt) {
                // Alt-drag moves an existing guide.
                const int g = GuideNear(event.pointer, 5.0);
                if (g >= 0) { draggingGuide = g; creatingGuide = false; return true; }
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
            hasHover = Contains(event.pointer) || panning || toolDragging || draggingGuide >= 0;
            if (showRulers) InvalidateRect(Rect2Df(0, 0, GetLocalBounds().width, static_cast<float>(rulerSize)));
            if (showRulers) InvalidateRect(Rect2Df(0, 0, static_cast<float>(rulerSize), GetLocalBounds().height));
            if (panning) {
                PanBy(event.pointer.x - lastPointer.x, event.pointer.y - lastPointer.y);
                lastPointer = event.pointer;
                return true;
            }
            lastPointer = event.pointer;
            if (draggingGuide >= 0 && draggingGuide < static_cast<int>(guides.size())) {
                VectorGuide& g = guides[draggingGuide];
                g.position = g.horizontal ? view.ViewToDoc(Point2Dd(0, event.pointer.y)).y
                                          : view.ViewToDoc(Point2Dd(event.pointer.x, 0)).x;
                RequestRedraw();
                return true;
            }
            if (toolDragging) {
                if (onToolDrag) onToolDrag(MakePointerEvent(event));
                return true;
            }
            if (hasHover && onToolHover) onToolHover(MakePointerEvent(event));
            return false;
        }
        case UCEventType::MouseLeave:
            hasHover = false;
            RequestRedraw();
            return false;

        case UCEventType::MouseUp: {
            if (panning) {
                panning = false;
                if (auto* app = UltraCanvasApplication::GetInstance()) app->ReleaseMouse();
                return true;
            }
            if (draggingGuide >= 0) {
                // Dropped back on a ruler: the guide goes away.
                bool hr = false;
                const bool onRuler = PointInRuler(event.pointer, hr) ||
                                     event.pointer.x < rulerSize || event.pointer.y < rulerSize;
                if (onRuler && showRulers && draggingGuide < static_cast<int>(guides.size()))
                    guides.erase(guides.begin() + draggingGuide);
                draggingGuide = -1;
                creatingGuide = false;
                if (auto* app = UltraCanvasApplication::GetInstance()) app->ReleaseMouse();
                if (onGuidesChanged) onGuidesChanged();
                RequestRedraw();
                return true;
            }
            if (toolDragging) {
                toolDragging = false;
                if (auto* app = UltraCanvasApplication::GetInstance()) app->ReleaseMouse();
                VectorPointerEvent p = MakePointerEvent(event);
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
            if (event.character == '+' || event.character == '=' || event.virtualKey == UCKeys::Plus) { ZoomIn(); return true; }
            if (event.character == '-' || event.virtualKey == UCKeys::Minus) { ZoomOut(); return true; }
            if (event.ctrl && event.virtualKey == UCKeys::Key0) { ZoomToPage(); return true; }
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
