// include/UltraCanvasPaintSurface.h
// The editable image view of a bitmap editor: shows a UCRasterDocument's
// composite at any zoom, pans it, draws the checkerboard behind transparent
// pixels, the pixel grid at high zoom, the marching-ants selection outline
// and a brush-size cursor, and hands pointer events to the active tool in
// IMAGE coordinates. It owns no tool logic: the host sets the callbacks
// (onToolPress / onToolDrag / onToolRelease / onToolHover) and draws its
// rubber-band previews through onDrawOverlay.
//
// Distinct from UltraCanvasZoomPanImage (a read-only viewer of a UCImage)
// and UltraCanvasMediaSurface (the media viewer's display): this one edits.
// Version: 1.0.0
// Last Modified: 2026-09-06
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRasterDocument.h"
#include "UltraCanvasTimer.h"

#include <functional>
#include <memory>

namespace UltraCanvas {

// ===== VIEW TRANSFORM =====
// Image <-> view mapping: view = origin + image * zoom (view coordinates are
// local to the element).
struct PaintViewTransform {
    double zoom = 1.0;
    double originX = 0.0;
    double originY = 0.0;
    Point2Dd ImageToView(double ix, double iy) const { return Point2Dd(originX + ix * zoom, originY + iy * zoom); }
    Point2Dd ViewToImage(double vx, double vy) const { return Point2Dd((vx - originX) / zoom, (vy - originY) / zoom); }
    Rect2Dd ImageToView(const Rect2Dd& r) const {
        return Rect2Dd(originX + r.x * zoom, originY + r.y * zoom, r.width * zoom, r.height * zoom);
    }
};

// ===== A POINTER EVENT IN IMAGE SPACE =====
struct PaintPointerEvent {
    double x = 0.0, y = 0.0;         // image coordinates (sub-pixel)
    Point2Di view;                   // the same in element-local pixels
    UCMouseButton button = UCMouseButton::NoneButton;
    bool ctrl = false, shift = false, alt = false;
    float pressure = 1.0f;
    // True when the pointer is over the canvas rectangle (tools may still
    // want events outside it, e.g. a marquee dragged past the edge).
    bool insideImage = false;
};

class UltraCanvasPaintSurface : public UltraCanvasUIElement {
public:
    explicit UltraCanvasPaintSurface(const std::string& elemId = "PaintSurface");
    ~UltraCanvasPaintSurface() override;

    // ===== DOCUMENT =====
    void SetDocument(std::shared_ptr<UCRasterDocument> doc);
    std::shared_ptr<UCRasterDocument> GetDocument() const { return document; }

    // ===== VIEW =====
    const PaintViewTransform& GetTransform() const { return view; }
    double GetZoom() const { return view.zoom; }
    // 1.0 == 100 %. Zooms about the centre of the view.
    void SetZoom(double zoom);
    // Zoom about a point given in element-local coordinates (the wheel).
    void SetZoomAt(double zoom, const Point2Di& viewPoint);
    void ZoomIn()  { ZoomStep(1); }
    void ZoomOut() { ZoomStep(-1); }
    void ZoomStep(int direction);           // +1 / -1 through the preset ladder
    void ZoomToFit();                       // whole image visible, centred
    void ZoomToActual() { SetZoom(1.0); }   // 100 %
    void CenterImage();
    void PanBy(double dx, double dy);       // in view pixels
    void ScrollToImagePoint(double ix, double iy);   // centre the view on it
    Point2Dd ViewToImage(const Point2Di& p) const { return view.ViewToImage(p.x, p.y); }
    Point2Dd ImageToView(double ix, double iy) const { return view.ImageToView(ix, iy); }

    // ===== DISPLAY OPTIONS =====
    void SetShowPixelGrid(bool show) { showPixelGrid = show; RequestRedraw(); }
    bool GetShowPixelGrid() const { return showPixelGrid; }
    // Zoom at which the pixel grid appears (default 8x).
    void SetPixelGridThreshold(double zoom) { pixelGridThreshold = zoom; RequestRedraw(); }
    void SetShowSelection(bool show) { showSelection = show; RequestRedraw(); }
    bool GetShowSelection() const { return showSelection; }
    void SetCanvasColor(const Color& c) { canvasColor = c; RequestRedraw(); }
    // Checkerboard squares behind transparent pixels (in view pixels).
    void SetCheckerColors(const Color& light, const Color& dark) { checkerLight = light; checkerDark = dark; RequestRedraw(); }
    // Brush outline drawn at the pointer; 0 hides it. Radius in image pixels.
    void SetCursorRadius(double imageRadius) { cursorRadius = imageRadius; RequestRedraw(); }
    void SetCursorSquare(bool square) { cursorSquare = square; }
    // Space-bar / middle-button panning is built in; a tool that wants the
    // drag itself (the Pan tool) can turn it on permanently.
    void SetPanMode(bool alwaysPan) { alwaysPan = alwaysPan; SetMouseCursor(alwaysPan ? UCMouseCursor::Hand : toolCursor); }
    void SetToolCursor(UCMouseCursor cursor) { toolCursor = cursor; if (!alwaysPan) SetMouseCursor(cursor); }

    // ===== TOOL CALLBACKS (image coordinates) =====
    std::function<void(const PaintPointerEvent&)> onToolPress;
    std::function<void(const PaintPointerEvent&)> onToolDrag;
    std::function<void(const PaintPointerEvent&)> onToolRelease;
    std::function<void(const PaintPointerEvent&)> onToolHover;     // move without a button
    std::function<void(const PaintPointerEvent&)> onToolDoubleClick;
    // Keys reach the tool first (Escape cancels a marquee, Enter commits
    // a crop); return true to consume.
    std::function<bool(const UCEvent&)> onToolKey;
    // Draw tool previews in VIEW coordinates after the image and selection.
    std::function<void(IRenderContext*, const PaintViewTransform&)> onDrawOverlay;
    // Zoom / pan changed (status bar).
    std::function<void()> onViewChanged;
    // Files dropped onto the surface.
    std::function<void(const std::vector<std::string>&)> onFilesDropped;

    // ===== ELEMENT =====
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;
    bool AcceptsFocus() const override { return true; }
    // Refresh (the document notifies through its callbacks; a host that
    // edits the document elsewhere calls this).
    void Refresh() { RequestRedraw(); }

private:
    PaintPointerEvent MakePointerEvent(const UCEvent& e) const;
    void ClampView();
    void StartAntsTimer();
    void StopAntsTimer();
    void DrawCheckerboard(IRenderContext* ctx, const Rect2Dd& area);
    void DrawPixelGrid(IRenderContext* ctx, const Rect2Dd& imageArea);
    void DrawSelection(IRenderContext* ctx);
    void DrawBrushCursor(IRenderContext* ctx);
    void HookDocument();
    void UnhookDocument();

    std::shared_ptr<UCRasterDocument> document;
    PaintViewTransform view;
    bool fitPending = true;

    bool showPixelGrid = true;
    double pixelGridThreshold = 8.0;
    bool showSelection = true;
    Color canvasColor = Color(64, 64, 68, 255);
    Color checkerLight = Color(255, 255, 255, 255);
    Color checkerDark  = Color(204, 204, 204, 255);
    double cursorRadius = 0.0;
    bool cursorSquare = false;
    UCMouseCursor toolCursor = UCMouseCursor::Cross;

    // interaction
    bool alwaysPan = false;
    bool spaceDown = false;
    bool panning = false;
    bool toolDragging = false;
    UCMouseButton dragButton = UCMouseButton::NoneButton;
    Point2Di lastPointer;
    Point2Di hoverPointer;
    bool hasHover = false;

    // marching ants
    TimerId antsTimer = 0;
    double antsOffset = 0.0;
};

inline std::shared_ptr<UltraCanvasPaintSurface> CreatePaintSurface(const std::string& id) {
    return std::make_shared<UltraCanvasPaintSurface>(id);
}

} // namespace UltraCanvas
