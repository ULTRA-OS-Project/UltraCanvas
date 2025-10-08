// OS/MacOS/UltraCanvasMacOSWindow.h
// macOS platform window implementation for UltraCanvas Framework
// Version: 1.0.1
// Last Modified: 2024-12-19
// Author: UltraCanvas Framework

#pragma once

#ifndef ULTRACANVAS_MACOS_WINDOW_H
#define ULTRACANVAS_MACOS_WINDOW_H

// ===== CORE INCLUDES =====
#include "../../include/UltraCanvasBaseWindow.h"
#include "../../include/UltraCanvasEvent.h"
#include "../../include/UltraCanvasCommonTypes.h"
#include "UltraCanvasMacOSRenderContext.h"

// ===== MACOS PLATFORM INCLUDES =====
#ifdef __OBJC__
    #import <Cocoa/Cocoa.h>
    #import <CoreGraphics/CoreGraphics.h>
    #import <QuartzCore/QuartzCore.h>
#else
    // Forward declarations for C++ only files
    typedef struct objc_object NSWindow;
    typedef struct objc_object NSView;
    typedef struct objc_object NSEvent;
    typedef struct CGContext* CGContextRef;
    typedef struct CGLayer* CGLayerRef;
    typedef unsigned long NSWindowStyleMask;
#endif

// ===== STANDARD INCLUDES =====
#include <memory>
#include <functional>
#include <thread>
#include <mutex>
#include <string>

namespace UltraCanvas {

// Forward declarations
class UltraCanvasMacOSApplication;

// ===== MACOS WINDOW CLASS =====
class UltraCanvasMacOSWindow : public UltraCanvasBaseWindow {
private:
    // ===== COCOA WINDOW SYSTEM =====
    NSWindow* nsWindow;
    NSView* contentView;
    NSView* customView;
    void* windowDelegate;  // NSWindowDelegate*
    
    // ===== CORE GRAPHICS SYSTEM =====
    CGContextRef cgContext;
    CGLayerRef backBuffer;
    std::unique_ptr<MacOSRenderContext> renderContext;
    
    // ===== STATE MANAGEMENT =====
    bool isCustomViewInstalled;
    bool needsDisplay;
    
    // ===== THREAD SAFETY =====
    mutable std::mutex cocoaMutex;
    std::thread::id owningThread;

public:
    // ===== CONSTRUCTOR & DESTRUCTOR =====
    UltraCanvasMacOSWindow(const WindowConfig& config);
    virtual ~UltraCanvasMacOSWindow();

    // ===== INHERITED FROM BASE WINDOW =====
    void Destroy() override;
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

    // ===== MACOS-SPECIFIC METHODS =====
    NSWindow* GetNSWindow() const { return nsWindow; }
    NSView* GetCustomView() const { return customView; }
    CGContextRef GetCGContext() const { return cgContext; }
    void SetCGContext(CGContextRef context) { cgContext = context; }
    
    // ===== EVENT HANDLING =====
    bool HandleNSEvent(NSEvent* nsEvent);
    bool HandleKeyEvent(NSEvent* nsEvent);
    bool HandleMouseEvent(NSEvent* nsEvent);
    bool HandleWindowEvent(NSEvent* nsEvent);

    // ===== DRAWING SYSTEM =====
    void InvalidateRect(const Rect2D& rect);
    void SetNeedsDisplay(bool needsDisplay = true);
    bool GetNeedsDisplay() const { return needsDisplay; }

    // ===== GRAPHICS CONTEXT MANAGEMENT =====
    MacOSRenderContext* GetRenderContext() const { return renderContext.get(); }
    void UpdateGraphicsContext();

    // ===== WINDOW DELEGATE CALLBACKS =====
    void OnWindowWillClose();
    void OnWindowDidResize();
    void OnWindowDidMove();
    void OnWindowDidBecomeKey();
    void OnWindowDidResignKey();
    void OnWindowDidMiniaturize();
    void OnWindowDidDeminiaturize();

    // ===== THREAD SAFETY =====
    template<typename Func>
    auto SafeExecute(Func&& func) -> decltype(func());

private:
    // ===== WINDOW CREATION =====
    bool CreateNative(const WindowConfig& config);
    bool CreateNSWindow(const WindowConfig& config);
    bool CreateCustomView();
    bool CreateCoreGraphicsContext();
    void SetupWindowDelegate();
    void ApplyWindowConfiguration(const WindowConfig& config);
    NSWindowStyleMask GetNSWindowStyleMask(const WindowConfig& config);

    // ===== HELPER METHODS =====
    void UpdateContentViewSize();
    void UpdateBackBuffer();
    CGRect GetWindowContentRect();
    CGRect CocoaRectToCGRect(const Rect2D& rect);
    Rect2D CGRectToUltraCanvasRect(CGRect cgRect);

    // ===== BACK BUFFER MANAGEMENT =====
    void CreateBackBuffer();
    void DestroyBackBuffer();

    // ===== CLEANUP =====
    void CleanupCoreGraphics();
    void CleanupCocoa();
};

// ===== THREAD-SAFE EXECUTION TEMPLATE =====
template<typename Func>
auto UltraCanvasMacOSWindow::SafeExecute(Func&& func) -> decltype(func()) {
    std::lock_guard<std::mutex> lock(cocoaMutex);
    
    if (owningThread == std::thread::id() || owningThread == std::this_thread::get_id()) {
        return func();
    }
    
    // Execute on main thread for thread safety
    __block decltype(func()) result;
    dispatch_sync(dispatch_get_main_queue(), ^{
        result = func();
    });
    
    return result;
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_MACOS_WINDOW_H