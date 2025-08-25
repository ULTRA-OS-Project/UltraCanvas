// UltraCanvasX11Window.h
// Linux platform implementation for UltraCanvas Framework
// Version: 1.0.0
// Last Modified: 2025-07-11
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_X11_WINDOW_H
#define ULTRACANVAS_X11_WINDOW_H

// ===== CORE INCLUDES =====
#include "../../include/UltraCanvasBaseWindow.h"
#include "../../include/UltraCanvasEvent.h"
#include "../../include/UltraCanvasCommonTypes.h"
#include "../../include/UltraCanvasRenderInterface.h"
#include "UltraCanvasLinuxRenderContext.h"

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
    class LinuxRenderContext;
    class UltraCanvasLinuxApplication;
// ===== LINUX WINDOW CLASS =====
    class UltraCanvasLinuxWindow : public UltraCanvasBaseWindow {
    protected:
        Window xWindow;
        cairo_surface_t* cairoSurface;
        cairo_t* cairoContext;
        std::unique_ptr<LinuxRenderContext> renderContext;

        bool CreateNative(const WindowConfig& config);
        std::mutex cairoMutex;  // Add this

    public:
        UltraCanvasLinuxWindow(const WindowConfig& config = WindowConfig());

        // ===== INHERITED FROM BASE WINDOW =====
        virtual void Destroy() override;
        virtual void Show() override;
        virtual void Hide() override;
        virtual void Close() override;
        virtual void SetWindowTitle(const std::string& title) override;
        virtual void SetWindowSize(int width, int height) override;
        virtual void SetWindowPosition(int x, int y) override;
        virtual void SetResizable(bool resizable) override;
        virtual void Minimize() override;
        virtual void Maximize() override;
        virtual void Restore() override;
        virtual void SetFullscreen(bool fullscreen) override;
        virtual void Render() override;
        virtual void SwapBuffers() override;
        virtual void* GetNativeHandle() const override;
//        virtual void ProcessEvents() override;
        virtual bool OnEvent(const UCEvent&) override;


        // ===== LINUX-SPECIFIC METHODS =====
        Window GetXWindow() const { return xWindow; }
        cairo_t* GetCairoContext() const { return cairoContext; }
        LinuxRenderContext* GetRenderContext() const { return renderContext.get(); }
        void HandleXEvent(const XEvent& event);

    private:

        // Event handling
        void OnResize(int width, int height);
        void OnMove(int x, int y);
        void OnFocusChanged(bool focused);
        void OnMapStateChanged(bool mapped);

        // ===== INTERNAL SETUP =====
        bool CreateXWindow();
        bool CreateCairoSurface();
        void DestroyCairoSurface();
        void UpdateCairoSurface();
        void SetWindowHints();
//        void SetWindowDecorations();

        // ===== STATE MANAGEMENT =====
//        void UpdateWindowState();
//        void ApplyWindowState();
//        void SaveWindowGeometry();
//        void RestoreWindowGeometry();
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_LINUX_APPLICATION_H