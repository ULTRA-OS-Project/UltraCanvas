// OS/MacOS/UltraCanvasMacOSWindow.h
// macOS window implementation with Cocoa/Cairo support
// Version: 2.1.0
// Last Modified: 2026-05-03
// Author: UltraCanvas Framework

#pragma once

#ifndef ULTRACANVAS_MACOS_WINDOW_H
#define ULTRACANVAS_MACOS_WINDOW_H

// ===== CORE INCLUDES =====
#include "../../libspecific/Cairo/RenderContextCairo.h"
#include "../../include/UltraCanvasWindow.h"

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
    class UltraCanvasMacOSApplication;

// ===== MACOS WINDOW CLASS =====
    class UltraCanvasMacOSWindow : public UltraCanvasWindowBase {
    friend UltraCanvasMacOSApplication;
    private:
        // ===== COCOA WINDOW SYSTEM =====
        NSWindow* nsWindow;
        NSView* contentView;
        void* windowDelegate;  // NSWindowDelegate*

        bool pendingShow = false;

        // Backing-store scale of the screen the window is currently on.
        // 1.0 = standard, 2.0 = Retina, 3.0 = some external HiDPI panels.
        // Updated when the window is created and when the system reports a
        // change (display switched, window dragged between monitors).
        float backingScaleFactor = 1.0f;

        // ===== THREAD SAFETY =====
        mutable std::mutex cairoMutex;

        // ===== INTERNAL METHODS =====
        bool CreateNSWindow();
        bool CreateNativeCairoSurface();
        void DestroyNativeCairoSurface();
        // Re-reads [nsWindow backingScaleFactor]. Returns true if the value
        // changed since the last refresh.
        bool RefreshBackingScaleFactor();

    protected:
        bool CreateNative() override;
        void DestroyNative() override;
        void DoResizeNative() override;

    public:
        // ===== CONSTRUCTOR & DESTRUCTOR =====
        UltraCanvasMacOSWindow();

        // ===== INHERITED FROM BASE WINDOW =====
        void Show() override;
        void Hide() override;
        void SetWindowTitle(const std::string& title) override;
        void SetWindowIcon(const std::string& iconPath) override;
        void SetWindowSize(int width, int height) override;
        void SetWindowPosition(int x, int y) override;
        void SetResizable(bool resizable) override;
        void Minimize() override;
        void Maximize() override;
        void Restore() override;
        void SetFullscreen(bool fullscreen) override;
        void InvalidateWindowNative() override;
        NativeWindowHandle GetNativeHandle() const override;
        void GetScreenSize(int& width, int& height) const override;
        void GetScreenBounds(int& x, int& y, int& width, int& height) const override;
        NSWindow* GetNSWindowHandle() const;
        void RaiseAndFocus() override;

        void Focus();  // Not virtual in base class

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
        // Fired when the window's backing scale changes (e.g. dragged onto a
        // different display). Recreates the Cairo surface at the new pixel
        // size so rendering stays crisp on the new screen.
        void OnWindowDidChangeBackingProperties();

        // Currently observed device scale; used by the renderer to pick
        // pixel-resolution rasterization sizes for fonts and SVG icons.
        float GetBackingScaleFactor() const { return backingScaleFactor; }
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_MACOS_WINDOW_H