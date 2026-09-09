// Plugins/Diagrams/UltraCanvasDiagramViewport.cpp
// Shared pan/zoom/minimap/controls viewport for canvas-style diagram elements
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasDiagramViewport.h"

namespace UltraCanvas {

// =============================================================================
// ZOOM & PAN
// =============================================================================

void UltraCanvasDiagramViewport::ApplyZoomAtAnchor(double zoom) {
    zoomLevel = zoom;
    // Solve pan so the anchor world point lands back under the same local pixel:
    //   local = world * zoom + pan   =>   pan = local - world * zoom
    panOffset.x = zoomAnchorLocal.x - zoomAnchorWorld.x * zoomLevel;
    panOffset.y = zoomAnchorLocal.y - zoomAnchorWorld.y * zoomLevel;
}

bool UltraCanvasDiagramViewport::ZoomAtPoint(const Point2Di& localCursor, double factor) {
    // Chain onto where a running glide is heading, so a fast wheel spin
    // multiplies up into one long zoom rather than restarting each notch.
    const double base = zoomAnim.IsAnimating() ? zoomAnim.PendingValue() : zoomLevel;
    const double newZoom = std::clamp(base * factor, minZoom, maxZoom);
    if (std::abs(newZoom - base) < 1e-6) return false;

    // The world point under the cursor is captured once and held under it for
    // the whole glide, so the diagram grows around the cursor rather than
    // drifting as the intermediate steps re-read a moving transform.
    zoomAnchorWorld = ScreenToWorld(localCursor);
    zoomAnchorLocal = localCursor;

    // Without a repaint hook the viewport cannot show intermediate steps, so
    // the zoom is applied in one go — the behaviour every caller had before.
    if (!requestRepaint) {
        ApplyZoomAtAnchor(newZoom);
        return true;
    }
    if (!zoomAnim.IsBound()) {
        zoomAnim.Bind([this] { return zoomLevel; },
                      [this](double z) {
                          ApplyZoomAtAnchor(z);
                          if (requestRepaint) requestRepaint();
                      });
    }
    zoomAnim.AnimateTo(newZoom, minZoom, maxZoom);
    return true;
}

bool UltraCanvasDiagramViewport::FitView(const DiagramContentBounds& content, double padding) {
    if (!content.IsUsable()) return false;
    zoomAnim.Cancel();   // fitting positions the view; it is not a zoom gesture

    double availW = viewportWidth - padding * 2.0;
    double availH = viewportHeight - padding * 2.0;
    if (availW <= 0.0 || availH <= 0.0) return false;

    double newZoom = std::min(availW / content.GetWidth(), availH / content.GetHeight());
    zoomLevel = std::clamp(newZoom, minZoom, maxZoom);

    double centerX = (content.minX + content.maxX) / 2.0;
    double centerY = (content.minY + content.maxY) / 2.0;
    panOffset.x = viewportWidth / 2.0 - centerX * zoomLevel;
    panOffset.y = viewportHeight / 2.0 - centerY * zoomLevel;
    return true;
}

// =============================================================================
// PANEL PLACEMENT
// =============================================================================

Rect2Dd UltraCanvasDiagramViewport::PlacePanel(DiagramPanelPosition position,
                                               double panelWidth, double panelHeight,
                                               double padding) const {
    double px = padding;
    double py = padding;
    switch (position) {
        case DiagramPanelPosition::TopLeft:
            px = padding;
            py = padding;
            break;
        case DiagramPanelPosition::TopRight:
            px = viewportWidth - panelWidth - padding;
            py = padding;
            break;
        case DiagramPanelPosition::BottomLeft:
            px = padding;
            py = viewportHeight - panelHeight - padding;
            break;
        case DiagramPanelPosition::BottomRight:
            px = viewportWidth - panelWidth - padding;
            py = viewportHeight - panelHeight - padding;
            break;
    }
    return Rect2Dd(px, py, panelWidth, panelHeight);
}

// =============================================================================
// MINIMAP
// =============================================================================

Rect2Dd UltraCanvasDiagramViewport::GetMinimapRect() const {
    return PlacePanel(minimapConfig.position,
                      minimapConfig.width, minimapConfig.height,
                      minimapConfig.padding);
}

bool UltraCanvasDiagramViewport::PointInMinimap(const Point2Di& localPos) const {
    if (!minimapConfig.visible) return false;
    Rect2Dd r = GetMinimapRect();
    return localPos.x >= r.x && localPos.x <= r.x + r.width &&
           localPos.y >= r.y && localPos.y <= r.y + r.height;
}

bool UltraCanvasDiagramViewport::GetMinimapProjection(
        const DiagramContentBounds& content,
        DiagramMinimapProjection& outProjection) const {
    if (!content.IsUsable()) return false;

    Rect2Dd panel = GetMinimapRect();

    // Add a world-space margin so items don't sit flush against the panel edge.
    double margin = minimapConfig.contentMargin;
    double worldMinX = content.minX - margin;
    double worldMinY = content.minY - margin;
    double worldW = content.GetWidth() + margin * 2.0;
    double worldH = content.GetHeight() + margin * 2.0;
    if (worldW <= 0.0 || worldH <= 0.0) return false;

    double innerPad = minimapConfig.innerPadding;
    double availW = panel.width - 2.0 * innerPad;
    double availH = panel.height - 2.0 * innerPad;
    if (availW <= 0.0 || availH <= 0.0) return false;

    double scale = std::min(availW / worldW, availH / worldH);

    outProjection.scale = scale;
    outProjection.worldMinX = worldMinX;
    outProjection.worldMinY = worldMinY;
    outProjection.originX = panel.x + innerPad + (availW - worldW * scale) * 0.5;
    outProjection.originY = panel.y + innerPad + (availH - worldH * scale) * 0.5;
    return true;
}

void UltraCanvasDiagramViewport::RenderMinimap(
        IRenderContext* ctx,
        const DiagramContentBounds& content,
        const std::function<void(const DiagramMinimapProjection&)>& drawContent) {
    if (!ctx || !minimapConfig.visible) return;

    Rect2Dd panel = GetMinimapRect();

    ctx->SetFillPaint(minimapConfig.backgroundColor);
    ctx->FillRoundedRectangle(panel, 4.0);
    ctx->SetStrokePaint(minimapConfig.borderColor);
    ctx->SetStrokeWidth(1.0);
    ctx->DrawRoundedRectangle(panel, 4.0);

    DiagramMinimapProjection projection;
    if (!GetMinimapProjection(content, projection)) return;

    if (drawContent) {
        ctx->SetFillPaint(minimapConfig.nodeColor);
        drawContent(projection);
    }

    // Viewport indicator: the world rect currently on screen.
    Rect2Dd visible = GetVisibleWorldRect();
    Point2Dd tl = projection.WorldToMinimap(visible.x, visible.y);
    Point2Dd br = projection.WorldToMinimap(visible.x + visible.width,
                                            visible.y + visible.height);
    Rect2Dd indicator(tl.x, tl.y, br.x - tl.x, br.y - tl.y);

    ctx->SetFillPaint(minimapConfig.viewportFill);
    ctx->FillRectangle(indicator);
    ctx->SetStrokePaint(minimapConfig.viewportStroke);
    ctx->SetStrokeWidth(1.5);
    ctx->DrawRectangle(indicator);
}

void UltraCanvasDiagramViewport::HandleMinimapDrag(const Point2Di& localPos,
                                                   const DiagramContentBounds& content) {
    if (!minimapConfig.pannable) return;

    DiagramMinimapProjection projection;
    if (!GetMinimapProjection(content, projection)) return;
    if (std::abs(projection.scale) < 1e-9) return;

    // Invert the minimap projection to get the world point that was clicked.
    double worldX = projection.worldMinX + (localPos.x - projection.originX) / projection.scale;
    double worldY = projection.worldMinY + (localPos.y - projection.originY) / projection.scale;
    CenterOn(worldX, worldY);
}

// =============================================================================
// CONTROLS
// =============================================================================

int UltraCanvasDiagramViewport::GetControlButtonCount() const {
    int count = 0;
    if (controlsConfig.showZoom) count += 2;
    if (controlsConfig.showFit) count += 1;
    if (controlsConfig.showLock) count += 1;
    return count;
}

DiagramControlButton UltraCanvasDiagramViewport::GetControlButtonRole(int index) const {
    if (index < 0) return DiagramControlButton::NoAction;
    int cursor = 0;
    if (controlsConfig.showZoom) {
        if (index == cursor) return DiagramControlButton::ZoomIn;
        if (index == cursor + 1) return DiagramControlButton::ZoomOut;
        cursor += 2;
    }
    if (controlsConfig.showFit) {
        if (index == cursor) return DiagramControlButton::FitView;
        cursor += 1;
    }
    if (controlsConfig.showLock) {
        if (index == cursor) return DiagramControlButton::ToggleLock;
    }
    return DiagramControlButton::NoAction;
}

Rect2Dd UltraCanvasDiagramViewport::GetControlsRect() const {
    int btnCount = GetControlButtonCount();
    if (btnCount == 0) return Rect2Dd(0, 0, 0, 0);

    double bs = controlsConfig.buttonSize;
    double totalH = btnCount * bs + (btnCount - 1) * controlsConfig.gap;
    double panelW = bs + 2.0 * controlsConfig.padding;
    double panelH = totalH + 2.0 * controlsConfig.padding;
    return PlacePanel(controlsConfig.position, panelW, panelH, controlsConfig.padding);
}

int UltraCanvasDiagramViewport::FindControlButtonAt(const Point2Di& localPos) const {
    if (!controlsConfig.visible) return -1;
    int btnCount = GetControlButtonCount();
    if (btnCount == 0) return -1;

    Rect2Dd panel = GetControlsRect();
    if (localPos.x < panel.x || localPos.x > panel.x + panel.width ||
        localPos.y < panel.y || localPos.y > panel.y + panel.height) {
        return -1;
    }

    double bs = controlsConfig.buttonSize;
    double bx = panel.x + controlsConfig.padding;
    double by = panel.y + controlsConfig.padding;
    for (int i = 0; i < btnCount; ++i) {
        double btnY = by + i * (bs + controlsConfig.gap);
        if (localPos.x >= bx && localPos.x <= bx + bs &&
            localPos.y >= btnY && localPos.y <= btnY + bs) {
            return i;
        }
    }
    return -1;
}

void UltraCanvasDiagramViewport::RenderControls(IRenderContext* ctx, int hoveredButton, bool locked) {
    if (!ctx || !controlsConfig.visible) return;
    int btnCount = GetControlButtonCount();
    if (btnCount == 0) return;

    Rect2Dd panel = GetControlsRect();
    double bs = controlsConfig.buttonSize;

    ctx->SetFillPaint(controlsConfig.backgroundColor);
    ctx->FillRoundedRectangle(panel, 4.0);
    ctx->SetStrokePaint(controlsConfig.borderColor);
    ctx->SetStrokeWidth(1.0);
    ctx->DrawRoundedRectangle(panel, 4.0);

    double bx = panel.x + controlsConfig.padding;
    double by = panel.y + controlsConfig.padding;

    // Glyphs are drawn as vector primitives rather than text: text glyphs at
    // 14pt rendered too small and off-centre inside a 28px square (see
    // UltraCanvasNodeDiagram 2.0.1 changelog).
    auto drawButtonBackground = [&](int idx) -> Point2Dd {
        double btnY = by + idx * (bs + controlsConfig.gap);
        if (hoveredButton == idx) {
            ctx->SetFillPaint(controlsConfig.hoverColor);
            ctx->FillRoundedRectangle(Rect2Dd(bx, btnY, bs, bs), 3.0);
        }
        return Point2Dd(bx + bs * 0.5, btnY + bs * 0.5);
    };

    auto drawPlus = [&](double cx, double cy, double armLen, double thickness) {
        ctx->SetStrokeWidth(thickness);
        ctx->DrawLine({cx - armLen, cy}, {cx + armLen, cy});
        ctx->DrawLine({cx, cy - armLen}, {cx, cy + armLen});
    };

    auto drawMinus = [&](double cx, double cy, double armLen, double thickness) {
        ctx->SetStrokeWidth(thickness);
        ctx->DrawLine({cx - armLen, cy}, {cx + armLen, cy});
    };

    // Fit-to-view glyph: 4 corner brackets pointing inward (a viewfinder).
    auto drawFitGlyph = [&](double cx, double cy, double halfSize, double thickness) {
        ctx->SetStrokeWidth(thickness);
        double bracket = halfSize * 0.45;
        ctx->DrawLine({cx - halfSize, cy - halfSize}, {cx - halfSize + bracket, cy - halfSize});
        ctx->DrawLine({cx - halfSize, cy - halfSize}, {cx - halfSize, cy - halfSize + bracket});
        ctx->DrawLine({cx + halfSize - bracket, cy - halfSize}, {cx + halfSize, cy - halfSize});
        ctx->DrawLine({cx + halfSize, cy - halfSize}, {cx + halfSize, cy - halfSize + bracket});
        ctx->DrawLine({cx - halfSize, cy + halfSize - bracket}, {cx - halfSize, cy + halfSize});
        ctx->DrawLine({cx - halfSize, cy + halfSize}, {cx - halfSize + bracket, cy + halfSize});
        ctx->DrawLine({cx + halfSize, cy + halfSize - bracket}, {cx + halfSize, cy + halfSize});
        ctx->DrawLine({cx + halfSize - bracket, cy + halfSize}, {cx + halfSize, cy + halfSize});
    };

    // Padlock glyph: rectangular body plus a U-shaped shackle.
    auto drawLockGlyph = [&](double cx, double cy, bool isLocked, double thickness) {
        ctx->SetStrokeWidth(thickness);
        double bodyW = 9.0;
        double bodyH = 7.0;
        double bodyY = cy + 1.0;
        ctx->FillRectangle(Rect2Dd(cx - bodyW * 0.5, bodyY, bodyW, bodyH));
        double shackleW = 7.0;
        double shackleH = 5.5;
        double shackleTop = bodyY - shackleH;
        if (isLocked) {
            ctx->DrawLine({cx - shackleW * 0.5, bodyY}, {cx - shackleW * 0.5, shackleTop});
            ctx->DrawLine({cx - shackleW * 0.5, shackleTop}, {cx + shackleW * 0.5, shackleTop});
            ctx->DrawLine({cx + shackleW * 0.5, shackleTop}, {cx + shackleW * 0.5, bodyY});
        } else {
            // Open shackle: right side detached and swung clear.
            ctx->DrawLine({cx - shackleW * 0.5, bodyY}, {cx - shackleW * 0.5, shackleTop});
            ctx->DrawLine({cx - shackleW * 0.5, shackleTop}, {cx + shackleW * 0.6, shackleTop});
            ctx->DrawLine({cx + shackleW * 0.6, shackleTop}, {cx + shackleW * 0.6, shackleTop + 2.5});
        }
    };

    for (int i = 0; i < btnCount; ++i) {
        Point2Dd center = drawButtonBackground(i);
        ctx->SetStrokePaint(controlsConfig.iconColor);
        ctx->SetFillPaint(controlsConfig.iconColor);
        switch (GetControlButtonRole(i)) {
            case DiagramControlButton::ZoomIn:
                drawPlus(center.x, center.y, bs * 0.25, 2.0);
                break;
            case DiagramControlButton::ZoomOut:
                drawMinus(center.x, center.y, bs * 0.25, 2.0);
                break;
            case DiagramControlButton::FitView:
                drawFitGlyph(center.x, center.y, bs * 0.28, 1.8);
                break;
            case DiagramControlButton::ToggleLock:
                drawLockGlyph(center.x, center.y, locked, 1.8);
                break;
            case DiagramControlButton::NoAction:
                break;
        }
    }
}

DiagramControlButton UltraCanvasDiagramViewport::ApplyControlButton(
        int index, const DiagramContentBounds& content, double fitPadding) {
    DiagramControlButton role = GetControlButtonRole(index);
    switch (role) {
        case DiagramControlButton::ZoomIn:  ZoomIn();  break;
        case DiagramControlButton::ZoomOut: ZoomOut(); break;
        case DiagramControlButton::FitView: FitView(content, fitPadding); break;
        case DiagramControlButton::ToggleLock:
        case DiagramControlButton::NoAction:
            // Lock state belongs to the host element, not the viewport.
            break;
    }
    return role;
}

} // namespace UltraCanvas
