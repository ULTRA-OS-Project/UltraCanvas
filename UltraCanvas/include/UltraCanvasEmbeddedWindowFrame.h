// include/UltraCanvasEmbeddedWindowFrame.h
// The title bar UltraCanvasEmbeddedWindowManager puts above a window: a
// borderless window of its own holding the title (UltraCanvasLabel) and the
// minimize, maximize and close buttons (UltraCanvasButton). A press on the
// title moves the window, a double click maximizes or restores it. Made by a
// backend that runs the embedded window manager (the Android desktop mode),
// which composites it like any other window.
// Version: 1.0.0
// Last Modified: 2026-11-24
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_EMBEDDED_WINDOW_FRAME_H
#define ULTRACANVAS_EMBEDDED_WINDOW_FRAME_H

#include <functional>
#include <memory>
#include <string>

namespace UltraCanvas {

    class UltraCanvasWindowBase;
    class UltraCanvasLabel;
    class UltraCanvasButton;
    struct UCEvent;

    class UltraCanvasEmbeddedWindowFrame {
    public:
        // What the title bar asks of the window manager.
        struct Actions {
            std::function<void()> beginMove;        // the title was taken with the button down
            std::function<void()> minimize;
            std::function<void()> toggleMaximize;   // the button, or a double click on the title
            std::function<void()> close;            // the window's own close request
        };

        // The title bar's window, `width` x `height` logical pixels, hidden.
        // Null when no window could be made.
        static std::shared_ptr<UltraCanvasEmbeddedWindowFrame> Create(const std::string& title,
                                                                      int width, int height,
                                                                      bool minimizable, bool maximizable,
                                                                      bool closable, Actions actions);
        // True while Create makes its window: the backend sees a frame being
        // made and must not decorate it in turn.
        static bool IsCreatingFrame();

        ~UltraCanvasEmbeddedWindowFrame();

        UltraCanvasWindowBase* GetWindow() const;
        void SetTitle(const std::string& title);
        // The window it decorates has the keyboard (a stronger bar) or not.
        void SetActive(bool active);
        // The maximize button shows restore while the window is maximized.
        void SetMaximized(bool maximized);
        // Closes the frame's window (the window it decorates went).
        void Close();

    private:
        UltraCanvasEmbeddedWindowFrame() = default;
        void Build(bool minimizable, bool maximizable, bool closable);
        void ApplyColours();
        bool OnPointer(const UCEvent& event);

        std::shared_ptr<UltraCanvasWindowBase> window;
        std::shared_ptr<UltraCanvasLabel> titleLabel;
        std::shared_ptr<UltraCanvasButton> minimizeButton, maximizeButton, closeButton;
        Actions actions;
        std::string title;
        bool active = false;
        bool maximized = false;
        bool closed = false;
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_EMBEDDED_WINDOW_FRAME_H
