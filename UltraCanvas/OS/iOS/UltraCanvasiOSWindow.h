// OS/iOS/UltraCanvasiOSWindow.h
// iOS window: a Cairo image surface presented as the contents of the root
// view's CALayer - the Windows / Android model (an image surface plus an
// explicit present), not the macOS one: cairo's Quartz surface needs
// ApplicationServices, which iOS lacks
// (Docs/UltraCanvas/iOSPortInvestigation.md 3.3). The window always covers
// the whole view; desktop window-management calls are accepted no-ops.
// Version: 1.1.0 - SetiOSKeyboardWanted
// Version: 1.0.0
// Last Modified: 2026-11-24
// Author: UltraCanvas Framework

#pragma once

#ifndef ULTRACANVAS_IOS_WINDOW_H
#define ULTRACANVAS_IOS_WINDOW_H

// ===== CORE INCLUDES =====
#include "UltraCanvasRenderContext.h"

#include <cairo/cairo.h>
#include <pango/pangocairo.h>

namespace UltraCanvas {

    class UltraCanvasiOSApplication;

    // The root view the backend's main (OS/iOS/UltraCanvasiOSMain.mm) puts in
    // the key window before the application's main body runs; a UIView*, kept
    // opaque so this header compiles as plain C++. Null before launch.
    void* GetiOSRootView();
    void SetiOSRootView(void* view);
    // Raise (true) or lower (false) the software keyboard: the root view
    // becomes or stops being the first responder (OS/iOS/UltraCanvasiOSMain.mm).
    void SetiOSKeyboardWanted(bool wanted);

    // ===== iOS WINDOW CLASS =====
    class UltraCanvasiOSWindow : public UltraCanvasWindowBase {
    protected:
        void* rootView = nullptr;   // UIView*, borrowed: owned by the view controller

        bool CreateNative() override;
        void DestroyNative() override;
        void DoResizeNative() override;
        bool RecreateNativeSurface() override;
        float QueryNativeDeviceScale() const override;

    public:
        UltraCanvasiOSWindow();
        // Releases the native half however the window dies (as the Android
        // window does): ~UltraCanvasWindowBase cannot reach an override.
        ~UltraCanvasiOSWindow() override;

        // ===== INHERITED FROM BASE WINDOW =====
        void Show() override;
        void Hide() override;
        void RaiseAndFocus() override;
        void SetWindowTitle(const std::string& title) override;
        void SetWindowIcon(const std::string& iconPath) override;
        void SetWindowPosition(int x, int y) override;
        void SetWindowSize(int width, int height) override;
        void Minimize() override;
        void Maximize() override;
        void Restore() override;
        void SetFullscreen(bool fullscreen) override;
        void SetResizable(bool resizable) override;
        void GetScreenSize(int& width, int& height) const override;
        NativeWindowHandle GetNativeHandle() const override;
        void InvalidateWindowNative() override;

        // ===== FROM THE VIEW (main thread) =====
        // The view's bounds changed (rotation, split view, Stage Manager):
        // the size in points.
        void HandleViewResized(int widthPoints, int heightPoints);
        // Backgrounded / back: no rendering while the app cannot be seen.
        void HandleEnteredBackground();
        void HandleEnteredForeground();

    private:
        bool autoHiddenInBackground = false;
        bool CreateNativeCairoSurface();
        void DestroyNativeCairoSurface();
        // Hand the image surface's pixels to the view's layer.
        void PresentSurface();
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_IOS_WINDOW_H
