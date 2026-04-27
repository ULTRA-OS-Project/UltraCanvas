// OS/MSWindows/UltraCanvasWindowsWindow.h
// Windows platform window implementation for UltraCanvas Framework
// Version: 1.0.1
// Last Modified: 2026-04-05
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_WINDOWS_WINDOW_H
#define ULTRACANVAS_WINDOWS_WINDOW_H

// ===== CORE INCLUDES =====
// NOTE: Do NOT include UltraCanvasWindow.h here - it creates a circular dependency.
// UltraCanvasWindow.h includes this header after defining UltraCanvasWindowBase.
#include "../../libspecific/Cairo/RenderContextCairo.h"
#include "UltraCanvasWindowsDragDrop.h"

// ===== WINDOWS PLATFORM INCLUDES =====
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cairo/cairo.h>
#include <cairo/cairo-win32.h>

// Undefine Windows macros that conflict with our method names
#ifdef DrawText
#undef DrawText
#endif
#ifdef CreateWindow
#undef CreateWindow
#endif
#ifdef CreateDialog
#undef CreateDialog
#endif
#ifdef RGB
#undef RGB
#endif

// ===== STANDARD INCLUDES =====
#include <memory>
#include <mutex>

namespace UltraCanvas {

    // Forward declarations
    class UltraCanvasWindowsApplication;

// ===== WINDOWS WINDOW CLASS =====
    class UltraCanvasWindowsWindow : public UltraCanvasWindowBase {
    protected:
        HWND hwnd;
        HDC hdc;
        HICON hIconBig = nullptr;
        HICON hIconSmall = nullptr;

        cairo_surface_t* cairoSurface;
        std::unique_ptr<RenderContextCairo> renderContext;
        UltraCanvasWindowsDropTarget* dropTarget;

        bool trackingMouseLeave;

        bool CreateNative() override;
        void DestroyNative() override;
        std::mutex cairoMutex;

        // Saved state for fullscreen toggle
        WINDOWPLACEMENT savedPlacement;
        LONG savedStyle;
        LONG savedExStyle;

    public:
        UltraCanvasWindowsWindow();

        // ===== INHERITED FROM BASE WINDOW =====
        void Show() override;
        void Hide() override;
        void RaiseAndFocus() override;
        void SetWindowTitle(const std::string& title) override;
        void SetWindowIcon(const std::string& iconPath) override;
        void SetWindowSize(int width, int height) override;
        void SetWindowPosition(int x, int y) override;
        void SetResizable(bool resizable) override;
        void Minimize() override;
        void Maximize() override;
        void Restore() override;
        void SetFullscreen(bool fullscreen) override;
        void FlushNative() override;
        NativeWindowHandle GetNativeHandle() const override;
        void GetScreenPosition(int& x, int& y) const override;
        void GetScreenSize(int& width, int& height) const override;
        void GetScreenBounds(int& x, int& y, int& width, int& height) const override;
        IRenderContext* GetRenderContext() const override { return renderContext.get(); }

        // ===== WINDOWS-SPECIFIC METHODS =====
        HWND GetHWND() const { return hwnd; }

        /// WndProc message handler called from StaticWndProc
        LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    private:
        // ===== INTERNAL SETUP =====
        bool CreateHWND();
        bool CreateNativeCairoSurface();
        void DestroyNativeCairoSurface();
        void BlitSurfaceToHDC(HDC targetDC);
        void SetWindowStyle();

        void HandleResizeEventWindows(int w, int h);

    protected:
        void DoResizeNative() override;
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_WINDOWS_WINDOW_H
