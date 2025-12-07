// OS/MacOS/UltraCanvasMacOSWindow.h
// macOS window implementation with Cocoa/Cairo support
// Version: 2.0.0
// Last Modified: 2025-01-18
// Author: UltraCanvas Framework

#pragma once

#ifndef ULTRACANVAS_MACOS_WINDOW_H
#define ULTRACANVAS_MACOS_WINDOW_H

// ===== CORE INCLUDES =====
#include "../../libspecific/Cairo/RenderContextCairo.h"

// ===== MACOS PLATFORM INCLUDES =====
#ifdef __OBJC__
#import <Cocoa/Cocoa.h>
    #import <QuartzCore/QuartzCore.h>
#else
// Forward declarations for C++ only files
typedef struct objc_object NSWindow;
typedef struct objc_object NSView;
typedef struct objc_object NSEvent;
typedef struct CGContext* CGContextRef;
#endif

// ===== CAIRO INCLUDES =====
#include <cairo/cairo.h>
#include <cairo/cairo-quartz.h>

// ===== STANDARD INCLUDES =====
#include <memory>
#include <string>
#include <mutex>

namespace UltraCanvas {

// Forward declarations
    class LinuxRenderContext;  // Reuse Linux/Cairo render context
    class UltraCanvasMacOSApplication;

// ===== MACOS WINDOW CLASS =====
    class UltraCanvasMacOSWindow : public UltraCanvasWindowBase {
    friend UltraCanvasMacOSApplication;
    private:
        // ===== COCOA WINDOW SYSTEM =====
        NSWindow* nsWindow;
        NSView* contentView;
        void* windowDelegate;  // NSWindowDelegate*

        // ===== CAIRO RENDERING =====
        cairo_surface_t* cairoSurface;
        CGContextRef cgContext;  // Quartz context for Cairo surface

        bool pendingShow = false;

        // ===== RENDER CONTEXT =====
        std::unique_ptr<RenderContextCairo> renderContext;

        // ===== THREAD SAFETY =====
        mutable std::mutex cairoMutex;

        // ===== INTERNAL METHODS =====
        bool CreateNSWindow();
        bool CreateCairoSurface();
        void DestroyCairoSurface();
        void ResizeCairoSurface(int width, int height);

    protected:
        bool CreateNative() override;
        void DestroyNative() override;

    public:
        // ===== CONSTRUCTOR & DESTRUCTOR =====
        UltraCanvasMacOSWindow();
        UltraCanvasMacOSWindow(const WindowConfig& config);
        ~UltraCanvasMacOSWindow() override;

        // ===== WINDOW CREATION =====

        // ===== WINDOW MANAGEMENT =====
        // ===== INHERITED FROM BASE WINDOW =====
        void Show() override;
        void Hide() override;
        void Close() override;
        void SetWindowTitle(const std::string& title) override;
        void SetWindowSize(int width, int height) override;
        void SetWindowPosition(int x, int y) override;
        void SetResizable(bool resizable) override;
        void Minimize() override;
        void Maximize() override;
        void Restore() override;
        void SetFullscreen(bool fullscreen) override;
        void Flush() override;
        unsigned long GetNativeHandle() const override;
        IRenderContext* GetRenderContext() const override { return renderContext.get(); }

        void Focus();  // Not virtual in base class

        // ===== RENDERING =====
        void Invalidate();  // Not virtual in base class

        // ===== GETTERS =====
        NSWindow* GetNSWindow() const { return nsWindow; }

        // ===== WINDOW DELEGATE CALLBACKS =====
        void OnWindowWillClose();
        void OnWindowDidResize();
        void OnWindowDidMove();
        void OnWindowDidBecomeKey();
        void OnWindowDidResignKey();
        void OnWindowDidMiniaturize();
        void OnWindowDidDeminiaturize();
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_MACOS_WINDOW_H