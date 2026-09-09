// include/Plugins/Diagrams/UltraCanvasDiagramViewport.h
// Shared pan/zoom/minimap/controls viewport for canvas-style diagram elements
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework
//
// CHANGELOG 1.0.0 (initial):
//  - Extracted from UltraCanvasNodeDiagram 2.0.6 and UltraCanvasCompositorDiagram,
//    which each carried their own copy of the same viewport machinery.
//  - Owns: zoom/pan state + clamping, screen<->world transforms, zoom-at-cursor,
//    FitView/CenterOn, the snap grid, the minimap overlay (config, geometry,
//    render, hit test, viewport drag) and the controls overlay (config, geometry,
//    render, hit test, button roles).
//  - COORDINATE CONVENTION: everything here is ELEMENT-LOCAL. The framework
//    already translates pointer coordinates into element-local space before
//    dispatch (see UltraCanvasContainer::HandleScrollbarEvents), and
//    UltraCanvasUIElement::Contains() tests against GetLocalBounds() which is
//    always (0, 0, w, h). Overlays are therefore positioned from the viewport
//    SIZE alone and never from the element's absolute origin.
//
//    This fixes a latent bug inherited from UltraCanvasNodeDiagram, whose
//    PointInMinimap(), FindControlButtonAt() and HandleMouseWheel() added or
//    subtracted finalBounds.x/y against what are already element-local event
//    coordinates. That double-counted the element origin, so minimap/controls
//    hit-testing and zoom-at-cursor were offset by exactly the element's
//    position whenever the diagram was not placed at (0, 0).
//    UltraCanvasCompositorDiagram had it right; this component adopts that
//    convention for both.

#pragma once

#include "UltraCanvasRenderContext.h"
#include "UltraCanvasSmoothScroll.h"
#include "UltraCanvasCommonTypes.h"
#include <functional>
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

// =============================================================================
// OVERLAY PLACEMENT
// =============================================================================

enum class DiagramPanelPosition {
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight
};

// =============================================================================
// CONFIGURATION
// =============================================================================

struct DiagramSnapGrid {
    bool enabled = false;
    double snapX = 25.0;
    double snapY = 25.0;
};

struct DiagramMinimapConfig {
    bool visible = false;
    DiagramPanelPosition position = DiagramPanelPosition::BottomRight;
    double width = 180.0;
    double height = 130.0;
    double padding = 10.0;
    Color backgroundColor = Color(245, 245, 245, 230);
    Color borderColor = Color(180, 180, 180, 255);
    Color nodeColor = Color(140, 160, 200, 255);
    Color viewportFill = Color(0, 120, 215, 40);
    Color viewportStroke = Color(0, 120, 215, 200);
    bool pannable = true;
    double innerPadding = 6.0;   // Gap between panel edge and projected content
    double contentMargin = 20.0; // World-space margin added around content bounds
};

struct DiagramControlsConfig {
    bool visible = false;
    DiagramPanelPosition position = DiagramPanelPosition::BottomLeft;
    double buttonSize = 28.0;
    double padding = 10.0;
    double gap = 4.0;
    Color backgroundColor = Color(255, 255, 255, 230);
    Color borderColor = Color(200, 200, 200, 255);
    Color iconColor = Color(80, 80, 80, 255);
    Color hoverColor = Color(230, 230, 230, 255);
    bool showZoom = true;
    bool showFit = true;
    bool showLock = true;
};

// Which action a control-panel button index maps to. The panel only shows the
// groups enabled in DiagramControlsConfig, so indices are not fixed - always
// resolve them through GetControlButtonRole().
// NOTE: no enumerator may be named "None" - X11's Xlib.h defines None as a
// macro, and this header is reachable from app code that includes X11.
enum class DiagramControlButton {
    NoAction,
    ZoomIn,
    ZoomOut,
    FitView,
    ToggleLock
};

// =============================================================================
// CONTENT BOUNDS
// =============================================================================

// World-space extent of whatever the host element draws. The viewport never
// inspects host data structures; the host reports its extent through this.
struct DiagramContentBounds {
    double minX = 0.0;
    double minY = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;
    bool valid = false;

    DiagramContentBounds() = default;
    DiagramContentBounds(double x0, double y0, double x1, double y1)
        : minX(x0), minY(y0), maxX(x1), maxY(y1), valid(true) {}

    double GetWidth() const { return maxX - minX; }
    double GetHeight() const { return maxY - minY; }
    bool IsUsable() const { return valid && GetWidth() > 0.0 && GetHeight() > 0.0; }

    void Include(double x, double y) {
        if (!valid) {
            minX = maxX = x;
            minY = maxY = y;
            valid = true;
            return;
        }
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        maxX = std::max(maxX, x);
        maxY = std::max(maxY, y);
    }

    void Include(const Rect2Dd& rect) {
        Include(rect.x, rect.y);
        Include(rect.x + rect.width, rect.y + rect.height);
    }
};

// Projection from world space into the minimap panel, handed to the host so it
// can draw its own content into the minimap without knowing the panel geometry.
struct DiagramMinimapProjection {
    double scale = 1.0;
    double originX = 0.0;   // Panel-local pixel origin of the projected content
    double originY = 0.0;
    double worldMinX = 0.0;
    double worldMinY = 0.0;

    Point2Dd WorldToMinimap(double worldX, double worldY) const {
        return Point2Dd(originX + (worldX - worldMinX) * scale,
                        originY + (worldY - worldMinY) * scale);
    }
};

// =============================================================================
// VIEWPORT
// =============================================================================

class UltraCanvasDiagramViewport {
public:
    UltraCanvasDiagramViewport() = default;

    // -------------------------------------------------------------------------
    // VIEWPORT SIZE - the host pushes its element size here (local space, so the
    // element's absolute position is deliberately not part of this API).
    // -------------------------------------------------------------------------

    void SetViewportSize(double width, double height) {
        viewportWidth = width;
        viewportHeight = height;
    }
    double GetViewportWidth() const { return viewportWidth; }
    double GetViewportHeight() const { return viewportHeight; }

    // -------------------------------------------------------------------------
    // ZOOM & PAN
    // -------------------------------------------------------------------------

    double GetZoomLevel() const { return zoomLevel; }
    void SetZoomLevel(double zoom) {
        zoomAnim.Cancel();   // a set zoom is a position, not a gesture
        zoomLevel = zoom;
        ClampZoom();
    }

    Point2Dd GetPanOffset() const { return panOffset; }
    void SetPanOffset(double x, double y) {
        panOffset.x = x;
        panOffset.y = y;
    }
    void PanBy(double dx, double dy) {
        panOffset.x += dx;
        panOffset.y += dy;
    }

    void SetZoomRange(double minZ, double maxZ) {
        minZoom = minZ;
        maxZoom = maxZ;
        ClampZoom();
    }
    double GetMinZoom() const { return minZoom; }
    double GetMaxZoom() const { return maxZoom; }

    void ZoomIn(double factor = 1.2) { SetZoomLevel(zoomLevel * factor); }
    void ZoomOut(double factor = 1.2) { SetZoomLevel(zoomLevel / factor); }

    // Zoom about a cursor position so the world point under the cursor stays
    // put. localCursor is in element-local pixels.
    // Returns false when the zoom was already clamped and nothing changed.
    //
    // With a repaint hook set (SetRepaintCallback) the zoom eases towards its
    // target over the app-wide smooth-scroll duration instead of stepping, so a
    // wheel spin reads as one continuous move; the anchor world point is held
    // under the cursor for the whole glide. Without a hook the viewport has no
    // way to ask its element to repaint the intermediate steps, so the zoom is
    // applied in one go exactly as it always was.
    bool ZoomAtPoint(const Point2Di& localCursor, double factor);

    // How the viewport asks its owning element to repaint. Elements that set it
    // get animated wheel zoom; those that do not keep the instant behaviour.
    void SetRepaintCallback(std::function<void()> repaint) {
        requestRepaint = std::move(repaint);
    }
    // Drops an in-flight zoom glide — for anything that positions the view
    // outright (FitView, a programmatic zoom, loading another diagram).
    void CancelZoomAnimation() { zoomAnim.Cancel(); }

    // Fit the given world content into the viewport. Returns false when the
    // content or the viewport is degenerate and nothing was changed.
    bool FitView(const DiagramContentBounds& content, double padding = 40.0);

    void CenterOn(double worldX, double worldY) {
        zoomAnim.Cancel();
        panOffset.x = viewportWidth / 2.0 - worldX * zoomLevel;
        panOffset.y = viewportHeight / 2.0 - worldY * zoomLevel;
    }

    // -------------------------------------------------------------------------
    // COORDINATE TRANSFORMS (element-local pixels <-> diagram world units)
    // -------------------------------------------------------------------------

    Point2Dd ScreenToWorld(const Point2Di& localPos) const {
        double z = (std::abs(zoomLevel) < 1e-9) ? 1e-9 : zoomLevel;
        return Point2Dd((localPos.x - panOffset.x) / z,
                        (localPos.y - panOffset.y) / z);
    }

    Point2Dd ScreenToWorld(double localX, double localY) const {
        double z = (std::abs(zoomLevel) < 1e-9) ? 1e-9 : zoomLevel;
        return Point2Dd((localX - panOffset.x) / z, (localY - panOffset.y) / z);
    }

    Point2Di WorldToScreen(const Point2Dd& worldPos) const {
        return Point2Di(static_cast<int>(worldPos.x * zoomLevel + panOffset.x),
                        static_cast<int>(worldPos.y * zoomLevel + panOffset.y));
    }

    Point2Dd WorldToScreenD(const Point2Dd& worldPos) const {
        return Point2Dd(worldPos.x * zoomLevel + panOffset.x,
                        worldPos.y * zoomLevel + panOffset.y);
    }

    // World-space rectangle currently visible in the viewport.
    Rect2Dd GetVisibleWorldRect() const {
        Point2Dd tl = ScreenToWorld(0.0, 0.0);
        Point2Dd br = ScreenToWorld(viewportWidth, viewportHeight);
        return Rect2Dd(tl.x, tl.y, br.x - tl.x, br.y - tl.y);
    }

    // -------------------------------------------------------------------------
    // SNAP GRID
    // -------------------------------------------------------------------------

    DiagramSnapGrid& SnapGridConfig() { return snapGrid; }
    const DiagramSnapGrid& SnapGridConfig() const { return snapGrid; }
    void SetSnapToGrid(bool enabled) { snapGrid.enabled = enabled; }
    void SetSnapGrid(double snapX, double snapY) {
        snapGrid.snapX = snapX;
        snapGrid.snapY = snapY;
    }
    bool IsSnapToGridEnabled() const { return snapGrid.enabled; }

    Point2Dd SnapPoint(const Point2Dd& p) const {
        if (!snapGrid.enabled || snapGrid.snapX <= 0.0 || snapGrid.snapY <= 0.0) return p;
        return Point2Dd(std::round(p.x / snapGrid.snapX) * snapGrid.snapX,
                        std::round(p.y / snapGrid.snapY) * snapGrid.snapY);
    }

    // -------------------------------------------------------------------------
    // MINIMAP OVERLAY
    // -------------------------------------------------------------------------

    DiagramMinimapConfig& MinimapConfig() { return minimapConfig; }
    const DiagramMinimapConfig& MinimapConfig() const { return minimapConfig; }
    void SetMinimapConfig(const DiagramMinimapConfig& cfg) { minimapConfig = cfg; }
    void SetMinimapVisible(bool visible) { minimapConfig.visible = visible; }
    void SetMinimapPosition(DiagramPanelPosition pos) { minimapConfig.position = pos; }
    bool IsMinimapVisible() const { return minimapConfig.visible; }

    // Panel rectangle in element-local pixels.
    Rect2Dd GetMinimapRect() const;
    bool PointInMinimap(const Point2Di& localPos) const;

    // Computes the world->minimap projection for the given content. Returns
    // false when nothing can be projected (empty content or degenerate panel).
    bool GetMinimapProjection(const DiagramContentBounds& content,
                              DiagramMinimapProjection& outProjection) const;

    // Draws the panel, then calls drawContent (if set) so the host can plot its
    // own items, then draws the viewport indicator on top.
    void RenderMinimap(IRenderContext* ctx,
                       const DiagramContentBounds& content,
                       const std::function<void(const DiagramMinimapProjection&)>& drawContent);

    // Centres the view on the world point the given panel position maps to.
    void HandleMinimapDrag(const Point2Di& localPos, const DiagramContentBounds& content);

    // -------------------------------------------------------------------------
    // CONTROLS OVERLAY
    // -------------------------------------------------------------------------

    DiagramControlsConfig& ControlsConfig() { return controlsConfig; }
    const DiagramControlsConfig& ControlsConfig() const { return controlsConfig; }
    void SetControlsConfig(const DiagramControlsConfig& cfg) { controlsConfig = cfg; }
    void SetControlsVisible(bool visible) { controlsConfig.visible = visible; }
    void SetControlsPosition(DiagramPanelPosition pos) { controlsConfig.position = pos; }
    bool AreControlsVisible() const { return controlsConfig.visible; }

    int GetControlButtonCount() const;
    DiagramControlButton GetControlButtonRole(int index) const;
    Rect2Dd GetControlsRect() const;
    int FindControlButtonAt(const Point2Di& localPos) const;

    void RenderControls(IRenderContext* ctx, int hoveredButton, bool locked);

    // Applies the standard action for a button index. Returns the role that was
    // applied so the host can react (e.g. toggle its own interaction lock).
    DiagramControlButton ApplyControlButton(int index,
                                            const DiagramContentBounds& content,
                                            double fitPadding = 40.0);

private:
    void ClampZoom() {
        if (zoomLevel < minZoom) zoomLevel = minZoom;
        if (zoomLevel > maxZoom) zoomLevel = maxZoom;
    }

    // Shared panel placement for both overlays.
    Rect2Dd PlacePanel(DiagramPanelPosition position,
                       double panelWidth, double panelHeight, double padding) const;

    double viewportWidth = 0.0;
    double viewportHeight = 0.0;

    double zoomLevel = 1.0;
    Point2Dd panOffset;
    // Wheel zoom animation: the world point that must stay under the cursor for
    // the whole glide, the cursor pixel it stays under, and the animator easing
    // zoomLevel towards its target (see UltraCanvasSmoothScroll.h).
    Point2Dd zoomAnchorWorld;
    Point2Di zoomAnchorLocal;
    UltraCanvasSmoothScroll zoomAnim;
    std::function<void()> requestRepaint;
    // Applies one eased zoom step: sets the level and re-solves the pan that
    // holds the anchor under the cursor.
    void ApplyZoomAtAnchor(double zoom);
    double minZoom = 0.1;
    double maxZoom = 5.0;

    DiagramSnapGrid snapGrid;
    DiagramMinimapConfig minimapConfig;
    DiagramControlsConfig controlsConfig;
};

} // namespace UltraCanvas
