// OS/Linux/UltraCanvasX11Window.h
// Linux platform implementation for UltraCanvas Framework
// Version: 1.0.0
// Last Modified: 2025-07-11
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_X11_WINDOW_H
#define ULTRACANVAS_X11_WINDOW_H

// ===== CORE INCLUDES =====
#include "../../libspecific/Cairo/RenderContextCairo.h"

// ===== LINUX PLATFORM INCLUDES =====
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <pango/pangocairo.h>

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

        cairo_surface_t* cairoSurface;
        std::unique_ptr<RenderContextCairo> renderContext;

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
        virtual void SetWindowSize(int width, int height) override;
        virtual void SetWindowPosition(int x, int y) override;
        virtual void SetResizable(bool resizable) override;
        virtual void Minimize() override;
        virtual void Maximize() override;
        virtual void Restore() override;
        virtual void SetFullscreen(bool fullscreen) override;
        virtual void Flush() override;
        virtual unsigned long GetNativeHandle() const override;
        IRenderContext* GetRenderContext() const override { return renderContext.get(); }
//        virtual void ProcessEvents() override;
//        virtual bool OnEvent(const UCEvent&) override;

        // ===== LINUX-SPECIFIC METHODS =====
        Window GetXWindow() const { return xWindow; }
        bool HandleXEvent(const XEvent& event);

    private:

        // Event handling
//        void OnResize(int width, int height);
//        void OnMove(int x, int y);
//        void OnFocusChanged(bool focused);
//        void OnMapStateChanged(bool mapped);

        // ===== INTERNAL SETUP =====
        bool CreateXWindow();
        bool CreateCairoSurface();
        void DestroyCairoSurface();
        void UpdateCairoSurface(int w, int h);
        void SetWindowHints();
//        void SetWindowDecorations();

        // ===== STATE MANAGEMENT =====
//        void UpdateWindowState();
//        void ApplyWindowState();
//        void SaveWindowGeometry();
//        void RestoreWindowGeometry();
    protected:
        void HandleResizeEvent(int w, int h) override;
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_LINUX_APPLICATION_H