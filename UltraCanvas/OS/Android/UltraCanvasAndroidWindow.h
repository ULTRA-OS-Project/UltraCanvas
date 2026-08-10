// OS/Android/UltraCanvasAndroidWindow.h
// Android window: a Cairo image surface presented onto the activity's
// ANativeWindow (lock → row copy → unlockAndPost), following the Windows
// backend's image-surface + blit model. The "window" always covers the whole
// surface; desktop window-management calls are accepted no-ops.
// Version: 1.0.0
// Last Modified: 2026-08-10
// Author: UltraCanvas Framework

#pragma once

#ifndef ULTRACANVAS_ANDROID_WINDOW_H
#define ULTRACANVAS_ANDROID_WINDOW_H

// ===== CORE INCLUDES =====
#include "UltraCanvasRenderContext.h"

#include <cairo/cairo.h>
#include <pango/pangocairo.h>

struct ANativeWindow;   // <android/native_window.h>

namespace UltraCanvas {

    class UltraCanvasAndroidApplication;

    // ===== ANDROID WINDOW CLASS =====
    class UltraCanvasAndroidWindow : public UltraCanvasWindowBase {
    protected:
        // Borrowed from android_app; owned by the activity. Null between
        // APP_CMD_TERM_WINDOW and the next APP_CMD_INIT_WINDOW.
        ANativeWindow* nativeWindow = nullptr;

        bool CreateNative() override;
        void DestroyNative() override;
        void DoResizeNative() override;
        bool RecreateNativeSurface() override;
        float QueryNativeDeviceScale() const override;

    public:
        UltraCanvasAndroidWindow();

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

        // ===== SURFACE LIFECYCLE (driven by activity commands) =====
        // APP_CMD_INIT_WINDOW: adopt the (re)created ANativeWindow.
        void HandleNativeSurfaceCreated(ANativeWindow* window);
        // APP_CMD_TERM_WINDOW: the surface is going away; drop every
        // reference before returning to the system.
        void HandleNativeSurfaceDestroyed();
        // APP_CMD_WINDOW_RESIZED / APP_CMD_CONFIG_CHANGED.
        void HandleNativeSurfaceResized();

    private:
        bool CreateNativeCairoSurface();
        void DestroyNativeCairoSurface();
        // Copy the image surface into the ANativeWindow buffer, converting
        // Cairo's native-endian xRGB words to the buffer's RGBX byte order.
        void PresentSurface();
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_ANDROID_WINDOW_H
