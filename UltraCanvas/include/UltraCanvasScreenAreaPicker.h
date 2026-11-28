// include/UltraCanvasScreenAreaPicker.h
// Choosing an area of the screen: the screen frozen in a borderless window
// the size of the screen, dimmed, with a rectangle dragged over it, or the
// window under the pointer lit and taken with a click. Escape or a right
// click gives up; Enter takes the window under the pointer (Window mode) or
// the whole screen. The answer is a rectangle in screen pixels; the pixels
// of it come from the capture the picker was opened on (ScreenArea::CropToRgba).
//
//   DesktopScreenImage shot;
//   if (UltraCanvasDesktopShell::CaptureScreenImage(shot, &error)) {
//       picker = std::make_unique<UltraCanvasScreenAreaPicker>();
//       picker->onSelected = [&](const Rect2Di& area) {
//           ClipboardDib::RgbaImage image;
//           ScreenArea::CropToRgba(shot, area, image);   // then EncodePng, ...
//       };
//       picker->Open(shot, ScreenAreaMode::Rectangle, ScreenArea::ListPickableWindows(), &error);
//   }
//
// Hide the program's own windows before the capture if they are not to be in
// it (UltraSnip and ChatCube wait a moment after hiding: the window system
// takes the window off the screen after the call returns).
//
// Screen pixels are the pixels of UltraCanvasDesktopShell::ListScreens and
// of the capture. On macOS windows are placed in points; the picker turns
// one into the other with the window's backing scale and opens above the
// menu bar and the Dock.
//
// Moved here from UltraSnip (Apps/UltraSnip/ui/UltraSnipOverlay.*,
// UltraSnipCapture.*), which it serves along with ChatCube's screenshot
// button. Docs: Docs/UltraCanvas/UltraCanvasScreenAreaPicker.md
// Version: 1.0.0 - from UltraSnip's overlay 0.4.0 and its capture helpers
// Last Modified: 2026-11-23
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVASSCREENAREAPICKER_H
#define ULTRACANVASSCREENAREAPICKER_H

#include "UltraCanvasClipboardDib.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasDesktopShell.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasWindow.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

class UltraCanvasImageElement;

// What a press of Enter takes, and what a click takes: a dragged rectangle
// (Enter: the whole screen), a window (a click on it; Enter: the one under
// the pointer), or the whole screen (the caller takes it without the picker)
enum class ScreenAreaMode { Rectangle, Window, FullScreen };

// A window the picker can light and take: its outer rectangle in screen pixels
struct ScreenAreaWindow {
    uint64_t id = 0;
    std::string title;
    Rect2Di bounds;
};

namespace ScreenArea {

// The rectangle between two corners of a drag, either way round, both
// corners inside it (a click without a drag is a 1 x 1 rectangle)
Rect2Di RectBetween(const Point2Di& a, const Point2Di& b);

// `rect` cut down to `bounds`; empty (width or height 0) when they do not meet
Rect2Di ClipRect(const Rect2Di& rect, const Rect2Di& bounds);

// The screen area `area` (screen pixels) out of `shot` - whose own x / y say
// where it lies - as straight RGBA, opaque: what ClipboardDib::EncodePng,
// the clipboard and the image element take. The area is clipped to the
// shot; false when nothing of it is in the shot.
bool CropToRgba(const DesktopScreenImage& shot, const Rect2Di& area, ClipboardDib::RgbaImage& out,
                Rect2Di* taken = nullptr);

// The index in `windows` (bottom first, as ListWindows lists them) of the
// topmost window under `point`; -1 when none is
int TopmostWindowAt(const std::vector<ScreenAreaWindow>& windows, const Point2Di& point);

// The windows Window mode offers: every window of
// UltraCanvasDesktopShell::ListWindows that is not minimized and not kept
// out of the taskbar, with its bounds, bottom first. Empty where the
// platform lists no windows (the picker then offers rectangles only).
std::vector<ScreenAreaWindow> ListPickableWindows();

} // namespace ScreenArea

// ui-reuse-exempt: the marquee itself - a self-rendered view of the dimmed
// screen, the rectangle being dragged, its size and the hint; the frozen
// screen is the image element beneath it.
class UltraCanvasScreenAreaSelection : public UltraCanvasUIElement {
public:
    // A window the marquee can light, in the element's (logical) coordinates
    struct Window {
        Rect2Df bounds;
        std::string title;
    };

    explicit UltraCanvasScreenAreaSelection(const std::string& identifier);

    // Changed while open: switched to Window, the window under the pointer
    // is lit at once
    void SetMode(ScreenAreaMode mode);
    void SetWindows(std::vector<Window> windows) { windows_ = std::move(windows); }
    // Logical to physical pixels, for the size label
    void SetScale(float scale) { scale_ = scale; }
    // Where the hint line starts from the top (logical)
    void SetHintTop(float top) { hintTop_ = top; RequestRedraw(); }

    // A selection: the rectangle in the element's (logical) coordinates
    std::function<void(const Rect2Df&)> onSelected;
    std::function<void()> onCancelled;

    // Enter: the window under the pointer (Window mode) or the whole screen
    void SelectCurrent();
    // Where the pointer is, in the element's coordinates, before it has
    // moved: the picker opens under a resting pointer, and the window under
    // it is lit at once rather than at the first movement
    void SetPointer(const Point2Di& point);

    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;

private:
    void DrawDim(IRenderContext* ctx, const Rect2Df& hole);
    void DrawPill(IRenderContext* ctx, const std::string& text, float centreX, float y, bool below);
    int WindowUnder(const Point2Di& point) const;
    bool Finish(const Rect2Df& rect);

    ScreenAreaMode mode_ = ScreenAreaMode::Rectangle;
    std::vector<Window> windows_;
    float scale_ = 1.0f;
    float hintTop_ = 16.0f;
    bool dragging_ = false;
    bool pointerKnown_ = false;
    Point2Di anchor_;
    Point2Di current_;
    int hoverWindow_ = -1;
    bool done_ = false;
};

class UltraCanvasScreenAreaPicker {
public:
    UltraCanvasScreenAreaPicker();
    ~UltraCanvasScreenAreaPicker();
    UltraCanvasScreenAreaPicker(const UltraCanvasScreenAreaPicker&) = delete;
    UltraCanvasScreenAreaPicker& operator=(const UltraCanvasScreenAreaPicker&) = delete;

    // The picker window's title, where the window system shows one (a task
    // switcher): the program's name. Before Open.
    void SetTitle(const std::string& title) { title_ = title; }

    // Shows `shot` the size of the screen and lets the user choose. The
    // windows (screen pixels, bottom first) are what Window mode offers.
    // False with the reason when the picture could not be shown.
    bool Open(const DesktopScreenImage& shot, ScreenAreaMode mode, const std::vector<ScreenAreaWindow>& windows,
              std::string* error = nullptr);
    void Close();
    // The shape the marquee takes while it is open
    void SetMode(ScreenAreaMode mode);
    void SetHintTop(float top);
    bool IsOpen() const { return window_ != nullptr && open_; }

    // The chosen rectangle in screen pixels (as ListScreens counts them).
    // Called after the picker window has closed; the picker may be destroyed
    // from a task posted to the UI thread, not from inside the callback.
    std::function<void(const Rect2Di&)> onSelected;
    std::function<void()> onCancelled;
    // A button went up over the picker and it is still open: a window kept
    // above it (UltraSnip's bar) raises itself again, as the click raised
    // the picker
    std::function<void()> onReleased;

private:
    void Finish(bool selected, const Rect2Di& area);

    std::string title_ = "Screenshot";
    std::shared_ptr<UltraCanvasWindow> window_;
    std::shared_ptr<UltraCanvasImageElement> picture_;
    std::shared_ptr<UltraCanvasScreenAreaSelection> marquee_;
    bool open_ = false;
    int shotX_ = 0, shotY_ = 0;
    float scale_ = 1.0f;
};

} // namespace UltraCanvas

#endif // ULTRACANVASSCREENAREAPICKER_H
