// OS/Linux/UltraCanvasX11Window.h
// Linux platform implementation for UltraCanvas Framework
// Version: 1.1.0 - per-monitor HiDPI (deviceScale) support
// Last Modified: 2026-07-03
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_X11_WINDOW_H
#define ULTRACANVAS_X11_WINDOW_H

// ===== CORE INCLUDES =====
#include "UltraCanvasRenderContext.h"

// ===== LINUX PLATFORM INCLUDES =====
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlocale.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <pango/pangocairo.h>

#include "UltraCanvasLinuxDragDrop.h"

// ===== STANDARD INCLUDES =====
#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>
#include <thread>
#include <chrono>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace UltraCanvas {

// Forward declarations
    class UltraCanvasLinuxApplication;
// ===== LINUX WINDOW CLASS =====
    class UltraCanvasLinuxWindow : public UltraCanvasWindowBase {
    protected:
        Window xWindow;
        XIC xic;                    // X Input Context for this window

        UltraCanvasLinuxDragDrop dragDropHandler;

        bool CreateNative() override;
        void DestroyNative() override;
        std::mutex cairoMutex;  // Add this

    public:
        UltraCanvasLinuxWindow();

        // ===== INHERITED FROM BASE WINDOW =====
        virtual void Show() override;
        virtual void Hide() override;
        virtual void RaiseAndFocus() override;
        virtual void SetWindowTitle(const std::string& title) override;
        virtual void SetWindowIcon(const std::string& iconPath) override;
        virtual void SetWindowSize(int width, int height) override;
        virtual void SetWindowPosition(int x, int y) override;
        virtual void SetResizable(bool resizable) override;
        virtual void Minimize() override;
        virtual void Maximize() override;
        virtual void Restore() override;
        virtual void SetFullscreen(bool fullscreen) override;
        virtual void InvalidateWindowNative() override;
        virtual NativeWindowHandle GetNativeHandle() const override;
        virtual void GetWindowPosition(int& x, int& y) const override;
        void GetScreenSize(int& width, int& height) const override;
        // Bounds of the XRandR monitor this window is on (physical px, root
        // coords); primary monitor when the window isn't on any, whole X screen
        // when XRandR is unavailable.
        void GetScreenBounds(int& x, int& y, int& width, int& height) const override;
        // Record the parent and set WM_TRANSIENT_FOR so the WM keeps this
        // window above its parent and places it on the parent's monitor.
        void SetTransientParent(UltraCanvasWindowBase* parent) override;
        UltraCanvasLinuxDragDrop& GetDragDropHandler() { return dragDropHandler; }

        // Native XDnD source: drag files out of this window into other
        // applications. Called with a mouse button held down.
        bool StartNativeFileDrag(const std::vector<std::string>& filePaths,
                                 std::function<void(bool accepted, bool moved)> onFinished = nullptr) override;
//        virtual void ProcessEvents() override;
//        virtual bool OnEvent(const UCEvent&) override;

        // ===== LINUX-SPECIFIC METHODS =====
        Window GetXWindow() const { return xWindow; }
        XIC GetXIC() const { return xic; }


        bool HandleXEvent(const XEvent& event);

    private:

        // Event handling
//        void OnResize(int width, int height);
//        void OnMove(int x, int y);
//        void OnFocusChanged(bool focused);
//        void OnMapStateChanged(bool mapped);

        // ===== INTERNAL SETUP =====
        bool CreateXWindow();
        bool CreateXIC();
        void DestroyXIC();
        bool CreateNativeCairoSurface();
        void DestroyNativeCairoSurface();
        void SetWindowHints();

        // Per-monitor physical DPI (mm-based) for the monitor the window is on;
        // returns 0 when XRandR has no usable data. Helper for QueryNativeDeviceScale.
        float QueryXRandRScaleForWindow(Display* display) const;

        // Window center in root coordinates (physical px): live X geometry when
        // the window exists, config geometry otherwise. Used to pick the
        // XRandR monitor the window is on.
        void GetWindowCenterInRootCoords(Display* display, int& cx, int& cy) const;

        // True once a position has been chosen programmatically (dialog
        // centering, explicit config x/y). Exported to the WM via the
        // USPosition size hint so it honors the position instead of applying
        // its own placement policy.
        bool hasExplicitPosition = false;
//        void SetWindowDecorations();

        // ===== STATE MANAGEMENT =====
//        void UpdateWindowState();
//        void ApplyWindowState();
//        void SaveWindowGeometry();
//        void RestoreWindowGeometry();
    protected:
        void DoResizeNative() override;
        // Hybrid DPI detection: GDK_SCALE / QT_SCALE_FACTOR env -> Xft.dpi ->
        // XRandR per-monitor physical DPI -> 1.0.
        float QueryNativeDeviceScale() const override;
        // Rebuild the xlib surface at physical px + device scale.
        bool RecreateNativeSurface() override;
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_LINUX_APPLICATION_H