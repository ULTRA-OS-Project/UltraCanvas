// OS/BSD/UltraCanvasBSDWindow.h
// BSD window implementation with X11/Cairo support
// Version: 1.0.0
// Last Modified: 2025-10-08
// Author: UltraCanvas Framework

#pragma once

#ifndef ULTRACANVAS_BSD_WINDOW_H
#define ULTRACANVAS_BSD_WINDOW_H

// ===== CORE INCLUDES =====
#include "../../include/UltraCanvasBaseWindow.h"
#include "../../include/UltraCanvasRenderContext.h"
#include "../../include/UltraCanvasEvent.h"

// ===== BSD PLATFORM INCLUDES =====
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>

// ===== STANDARD INCLUDES =====
#include <memory>
#include <string>

namespace UltraCanvas {

// Forward declarations
class LinuxRenderContext;  // Reuse Linux render context

// ===== BSD WINDOW CLASS =====
class UltraCanvasBSDWindow : public UltraCanvasBaseWindow {
private:
    // ===== X11 WINDOW =====
    Window xWindow;
    
    // ===== CAIRO RENDERING =====
    cairo_surface_t* cairoSurface;
    cairo_t* cairoContext;
    
    // ===== RENDER CONTEXT =====
    std::unique_ptr<LinuxRenderContext> renderContext;
    
    // ===== INTERNAL METHODS =====
    bool CreateXWindow();
    bool CreateCairoSurface();
    void DestroyCairoSurface();
    void ResizeCairoSurface(int width, int height);

public:
    // ===== CONSTRUCTOR & DESTRUCTOR =====
    explicit UltraCanvasBSDWindow(const WindowConfig& config);
    virtual ~UltraCanvasBSDWindow() override;

    // ===== WINDOW CREATION =====
    bool CreateNative(const WindowConfig& config) override;
    
    // ===== WINDOW MANAGEMENT =====
    void Show() override;
    void Hide() override;
    void Close() override;
    void Minimize() override;
    void Maximize() override;
    void Restore() override;
    void Focus() override;
    
    // ===== WINDOW PROPERTIES =====
    void SetTitle(const std::string& title) override;
    void SetPosition(int x, int y) override;
    void SetSize(int width, int height) override;
    void SetFullscreen(bool fullscreen) override;
    
    // ===== RENDERING =====
    void Invalidate() override;
    void SwapBuffers() override;
    IRenderContext* GetRenderContext() override;
    
    // ===== GETTERS =====
    Window GetXWindow() const { return xWindow; }
    cairo_surface_t* GetCairoSurface() const { return cairoSurface; }
    cairo_t* GetCairoContext() const { return cairoContext; }
    
    // ===== EVENT HANDLING =====
    void DispatchEvent(const UCEvent& event);
};

} // namespace UltraCanvas

#endif // ULTRACANVAS_BSD_WINDOW_H