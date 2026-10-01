// Tests/DisplayTestSupport.h
// What a test that opens a real window under Xvfb needs and would otherwise
// copy from the last such test: activating the window, focusing an element,
// driving a frame without an event loop, waiting for the shared caret.
//
// Under Xvfb there is no window manager, so nothing ever activates the
// window - and a window that is never activated draws no caret and reports no
// focused element. That is correct behaviour, not a bug in the test: the test
// hands the application the activation event the backend would have
// delivered. There is no event loop either unless the test runs one, so
// frames are driven by hand.
//
// Header-only. Include it from a test that links UltraCanvas.
// Version: 1.0.0
// Last Modified: 2026-09-30
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasApplication.h"
#include "UltraCanvasCaret.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasWindow.h"

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

} // namespace DisplayTest
} // namespace UltraCanvas
