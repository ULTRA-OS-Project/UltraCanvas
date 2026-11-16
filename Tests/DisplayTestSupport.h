// Tests/DisplayTestSupport.h
// What a test that opens a real window under Xvfb needs and would otherwise
// copy from the last such test: activating the window, focusing an element,
// driving a frame without an event loop, waiting for the shared caret, and
// reading back whether a part of the window shows a colour (a selection's
// fill, a focus ring along a row's edges).
//
// Under Xvfb there is no window manager, so nothing ever activates the
// window - and a window that is never activated draws no caret and reports no
// focused element. That is correct behaviour, not a bug in the test: the test
// hands the application the activation event the backend would have
// delivered. There is no event loop either unless the test runs one, so
// frames are driven by hand.
//
// Header-only. Include it from a test that links UltraCanvas.
// Version: 1.1.0 - RowShows / RingSides, from TreeViewKeyboardTest, for every focus-ring test
// Version: 1.0.0
// Last Modified: 2026-11-15
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasApplication.h"
#include "UltraCanvasCaret.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasWindow.h"

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <vector>

namespace UltraCanvas {
namespace DisplayTest {

// Deliver the activation the window manager would have given the window.
inline void ActivateWindow(UltraCanvasApplication& app,
                           const std::shared_ptr<UltraCanvasWindow>& window) {
    UCEvent activate;
    activate.type = UCEventType::WindowFocus;
    activate.targetWindow = window;
    activate.nativeWindowHandle = window->GetNativeHandle();
    app.DispatchEvent(activate);
}

// Activate the window and give `element` the keyboard focus. True when the
// element reports itself focused afterwards; false means the window could not
// be activated, which a test treats as a reason to skip.
inline bool FocusElement(UltraCanvasApplication& app,
                         const std::shared_ptr<UltraCanvasWindow>& window,
                         const std::shared_ptr<UltraCanvasUIElement>& element) {
    ActivateWindow(app, window);
    element->SetFocus(true);
    return element->IsFocused();
}

// One frame, without the event loop: mark the elements dirty and render. A
// test marks its own elements because what it exercises is the drawing, not
// the dirty-rect plumbing.
inline void Frame(const std::shared_ptr<UltraCanvasWindow>& window,
                  const std::vector<std::shared_ptr<UltraCanvasUIElement>>& elements) {
    for (const auto& element : elements) element->RequestRedraw();
    window->UpdateAndRender();
}

// Drive frames until the shared caret is claimed on this window. False when it
// never is within `maxFrames`: the focused element draws no caret in its
// current state (a text input, for one, shows none while a selection exists).
inline bool WaitForCaret(const std::shared_ptr<UltraCanvasWindow>& window,
                         const std::vector<std::shared_ptr<UltraCanvasUIElement>>& elements,
                         int maxFrames = 60) {
    auto& caret = UltraCanvasCaret::GetInstance();
    for (int frame = 0; frame < maxFrames && !caret.IsOnWindow(window.get()); ++frame) {
        Frame(window, elements);
    }
    return caret.IsOnWindow(window.get());
}

// What a check looks for in a row: any pixel of the colour (the focus ring,
// a dashed line one pixel wide), or an area of it (a selection's fill).
enum class Look { AnyPixel, Area };

// Is the row's rectangle showing `colour`? The row is read from the
// composited window. Looking for an Area, a pixel counts only when a
// neighbour beside it and one above or below it show the colour too: a fill
// has those, while the coloured fringe that subpixel antialiasing leaves on
// black text - a pixel wide, beside each stem - does not. Counting the fringe
// made the plain row look selected wherever text renders in subpixel (a
// desktop with an RGB fontconfig).
inline bool RowShows(const std::shared_ptr<UltraCanvasWindow>& window, const Rect2Di& row,
              const Color& colour, Look look = Look::AnyPixel) {
    auto close = [](uint8_t l, uint8_t r) {
        return std::abs(static_cast<int>(l) - static_cast<int>(r)) <= 24;
    };
    const int w = row.width, h = row.height;
    std::vector<char> match(static_cast<size_t>(w) * h, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            Color px;
            if (!window->GetPixelColor(row.x + x, row.y + y, px)) continue;
            match[static_cast<size_t>(y) * w + x] = close(px.r, colour.r) && close(px.g, colour.g) && close(px.b, colour.b);
        }
    }
    auto at = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < w && y < h && match[static_cast<size_t>(y) * w + x];
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!at(x, y)) continue;
            if (look == Look::AnyPixel) return true;
            if ((at(x - 1, y) || at(x + 1, y)) && (at(x, y - 1) || at(x, y + 1))) return true;
        }
    }
    return false;
}

// On how many of its four sides does a rectangle show `colour` along its
// edge: a two-pixel band inside each side, which is where the focus ring
// runs. The label - white on a selected row too, in the selection text colour
// - sits in the middle of the row and never reaches a band, so it cannot pass
// for the ring; looking for the colour anywhere in the row let it.
inline int RingSides(const std::shared_ptr<UltraCanvasWindow>& window, const Rect2Di& ring, const Color& colour) {
    const int band = 2;
    const Rect2Di sides[] = {
        Rect2Di(ring.x, ring.y, ring.width, band),                        // top
        Rect2Di(ring.x, ring.y + ring.height - band, ring.width, band),   // bottom
        Rect2Di(ring.x, ring.y, band, ring.height),                       // left
        Rect2Di(ring.x + ring.width - band, ring.y, band, ring.height),   // right
    };
    int shown = 0;
    for (const Rect2Di& side : sides) {
        if (RowShows(window, side, colour)) ++shown;
    }
    return shown;
}

} // namespace DisplayTest
} // namespace UltraCanvas
