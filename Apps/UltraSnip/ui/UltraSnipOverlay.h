// Apps/UltraSnip/ui/UltraSnipOverlay.h
// The selection over the frozen screen: a borderless window the size of the
// screen showing the capture (UltraCanvasImageElement), and over it the
// marquee element - the dimmed surround, the rectangle being dragged with
// its size, the crosshair, the window under the pointer in Window mode and
// the hint line. Escape or a right click cancels; a drag, a click on a
// window, or Enter on the current choice selects.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraSnipCapture.h"
#include "UltraSnipTypes.h"

#include "UltraCanvasUIElement.h"
#include "UltraCanvasWindow.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas { class UltraCanvasImageElement; }

namespace UltraSnip {

// A window the overlay can pick, in the overlay's own (logical) coordinates
struct OverlayWindow {
    UltraCanvas::Rect2Df bounds;
    std::string title;
};

// ui-reuse-exempt: the marquee itself - a self-rendered view of the dimmed
// screen, the rectangle being dragged, its size and the hint; the surround
// is the image element beneath it.
class UltraSnipSelectionElement : public UltraCanvas::UltraCanvasUIElement {
public:
    explicit UltraSnipSelectionElement(const std::string& identifier);

    void SetMode(SnipMode mode) { mode_ = mode; RequestRedraw(); }
    void SetWindows(std::vector<OverlayWindow> windows) { windows_ = std::move(windows); }
    // Logical to physical pixels, for the size label
    void SetScale(float scale) { scale_ = scale; }

    // A selection: the rectangle in the element's (logical) coordinates
    std::function<void(const UltraCanvas::Rect2Df&)> onSelected;
    std::function<void()> onCancelled;

    // Enter: the window under the pointer (Window mode) or the whole screen
    void SelectCurrent();

    void Render(UltraCanvas::IRenderContext* ctx, const UltraCanvas::Rect2Df& dirtyRect) override;
    bool OnEvent(const UltraCanvas::UCEvent& event) override;

private:
    void DrawDim(UltraCanvas::IRenderContext* ctx, const UltraCanvas::Rect2Df& hole);
    void DrawPill(UltraCanvas::IRenderContext* ctx, const std::string& text, float centreX, float y, bool below);
    int WindowUnder(const UltraCanvas::Point2Di& point) const;
    bool Finish(const UltraCanvas::Rect2Df& rect);

    SnipMode mode_ = SnipMode::Rectangle;
    std::vector<OverlayWindow> windows_;
    float scale_ = 1.0f;
    bool dragging_ = false;
    bool pointerKnown_ = false;
    UltraCanvas::Point2Di anchor_;
    UltraCanvas::Point2Di current_;
    int hoverWindow_ = -1;
    bool done_ = false;
};

class UltraSnipOverlay {
public:
    UltraSnipOverlay();
    ~UltraSnipOverlay();

    // Shows `shot` the size of the screen and lets the user choose. The
    // windows (screen pixels, bottom first) are what Window mode offers.
    // False with the reason when the picture could not be shown.
    bool Open(const UltraCanvas::DesktopScreenImage& shot, SnipMode mode,
              const std::vector<SnipWindowBounds>& windows, std::string* error = nullptr);
    void Close();
    bool IsOpen() const { return window_ != nullptr && open_; }

    // The chosen rectangle in screen pixels (as ListScreens counts them)
    std::function<void(const UltraCanvas::Rect2Di&)> onSelected;
    std::function<void()> onCancelled;

private:
    void Finish(bool selected, const UltraCanvas::Rect2Di& area);

    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    std::shared_ptr<UltraCanvas::UltraCanvasImageElement> picture_;
    std::shared_ptr<UltraSnipSelectionElement> marquee_;
    bool open_ = false;
    int shotX_ = 0, shotY_ = 0;
    float scale_ = 1.0f;
};

} // namespace UltraSnip
