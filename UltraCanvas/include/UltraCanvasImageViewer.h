// include/UltraCanvasImageViewer.h
// Reusable lightbox image viewer: a zoomable / pannable image above a dark
// info panel, opened in its own window. Shared by the markdown renderer's
// image-click action and the Album photo viewer.
// Version: 1.1.0
// Last Modified: 2026-07-12
// V1.1.0: Animated images (GIF / animated WebP) now play in the lightbox — the
//   zoom / pan surface steps them with a UCImageAnimationController, matching
//   UltraCanvasImageElement.
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasImageAnimation.h"
#include <string>
#include <memory>

namespace UltraCanvas {

// ===== ZOOMABLE / PANNABLE IMAGE SURFACE =====
// Starts fit-to-view; the mouse wheel zooms about the cursor, dragging pans
// (with mouse capture so the drag survives leaving the element) and a
// double-click resets to fit. The image is re-rasterized at the displayed size
// each frame, so vector sources (SVG) stay crisp at any zoom. A solid canvas is
// painted behind the image first, so artwork that uses transparency composites
// over a known colour instead of whatever sits behind the element.
class UltraCanvasZoomPanImage : public UltraCanvasUIElement {
public:
    explicit UltraCanvasZoomPanImage(const std::string& elemId = "ZoomPanImage");

    void SetImage(std::shared_ptr<UCImage> img);
    void LoadFromFile(const std::string& path) { SetImage(UCImage::Get(path)); }
    // Backdrop painted behind the image (default white) so transparent artwork
    // reads correctly.
    void SetCanvasColor(const Color& c) { canvasColor = c; RequestRedraw(); }
    void ResetView() { needsFit = true; RequestRedraw(); }

    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;

private:
    bool HandleZoom(const UCEvent& event);

    std::shared_ptr<UCImage> image;
    // Steps animated images (GIF / animated WebP) on the app timer; zoom and
    // pan apply to the running animation. Holds no animation for stills.
    UCImageAnimationController animator;
    Color  canvasColor = Color(255, 255, 255, 255);
    bool   needsFit = true;
    double zoom = 1.0;            // multiple of the fit scale (1.0 == fit-to-view)
    double panX = 0.0, panY = 0.0;
    bool   dragging = false;
    double lastX = 0.0, lastY = 0.0;
};

// ===== LIGHTBOX INFO PANEL TEXT =====
// Empty fields are omitted from the panel (except the hint, which has a
// default). subtitle is typically a source, author or file path.
struct ImageViewerInfo {
    std::string title;
    std::string subtitle;
    std::string description;
    std::string hint = "Scroll to zoom \xC2\xB7 drag to pan \xC2\xB7 double-click to reset \xC2\xB7 Esc to close";
};

// ===== LIGHTBOX VIEWER WINDOW =====
// Opens an image at full size in its own window: the image fills the top (zoom +
// pan) above a dark info panel. A single window instance is reused across calls.
// Closes on Esc.
class UltraCanvasImageViewer {
public:
    // Show imagePath in the lightbox. When host is given the window opens over it
    // (same monitor / position) and is owned by it; otherwise it is centred at a
    // default size. imageBackground is the canvas painted behind the image.
    void Show(const std::string& imagePath, const ImageViewerInfo& info,
              UltraCanvasWindowBase* host = nullptr,
              const Color& imageBackground = Color(255, 255, 255, 255));
    void Close();
    bool IsOpen() const { return window != nullptr; }

private:
    std::shared_ptr<UltraCanvasWindow> window;
};

} // namespace UltraCanvas
