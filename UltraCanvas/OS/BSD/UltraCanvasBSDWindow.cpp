// OS/BSD/UltraCanvasBSDWindow.cpp
// Complete BSD window implementation with all methods
// Version: 1.0.0
// Last Modified: 2025-10-08
// Author: UltraCanvas Framework

#include "UltraCanvasBSDWindow.h"
#include "UltraCanvasBSDApplication.h"
#include "../Linux/UltraCanvasLinuxRenderContext.h"  // Reuse Linux render context
#include "UltraCanvasApplication.h"
#include <iostream>
#include <cstring>

namespace UltraCanvas {

// ===== CONSTRUCTOR & DESTRUCTOR =====
UltraCanvasBSDWindow::UltraCanvasBSDWindow(const WindowConfig& config)
    : UltraCanvasBaseWindow(config)
    , xWindow(0)
    , cairoSurface(nullptr)
    , cairoContext(nullptr) {
    
    std::cout << "UltraCanvas BSD: Window constructor completed successfully" << std::endl;
}

UltraCanvasBSDWindow::~UltraCanvasBSDWindow() {
    std::cout << "UltraCanvas BSD: Window destructor called" << std::endl;
    
    if (_created) {
        Close();
    }
}

// ===== WINDOW CREATION =====
bool UltraCanvasBSDWindow::CreateNative(const WindowConfig& config) {
    if (_created) {
        std::cout << "UltraCanvas BSD: Window already created" << std::endl;
        return true;
    }
    
    auto application = UltraCanvasApplication::GetInstance();
    if (!application || !application->IsInitialized()) {
        std::cerr << "UltraCanvas BSD: Cannot create window - application not ready" << std::endl;
        return false;
    }
    
    std::cout << "UltraCanvas BSD: Creating X11 window..." << std::endl;
    
    if (!CreateXWindow()) {
        std::cerr << "UltraCanvas BSD: Failed to create X11 window" << std::endl;
        return false;
    }
    
    if (!CreateCairoSurface()) {
        std::cerr << "UltraCanvas BSD: Failed to create Cairo surface" << std::endl;
        auto bsdApp = static_cast<UltraCanvasBSDApplication*>(application);
        XDestroyWindow(bsdApp->GetDisplay(), xWindow);
        xWindow = 0;
        return false;
    }
    
    try {
        renderContext = std::make_unique<LinuxRenderContext>(
            cairoContext, 
            cairoSurface, 
            config_.width, 
            config_.height, 
            true
        );
        std::cout << "UltraCanvas BSD: Render context created successfully" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UltraCanvas BSD: Failed to create render context: " << e.what() << std::endl;
        DestroyCairoSurface();
        auto bsdApp = static_cast<UltraCanvasBSDApplication*>(application);
        XDestroyWindow(bsdApp->GetDisplay(), xWindow);
        xWindow = 0;
        return false;
    }
    
    _created = true;
    
    std::cout << "UltraCanvas BSD: Window created successfully!" << std::endl;
    return true;
}

bool UltraCanvasBSDWindow::CreateXWindow() {
    auto application = UltraCanvasApplication::GetInstance();
    if (!application) {
        std::cerr << "UltraCanvas BSD: Invalid application" << std::endl;
        return false;
    }
    
    auto bsdApp = static_cast<UltraCanvasBSDApplication*>(application);
    Display* display = bsdApp->GetDisplay();
    int screen = bsdApp->GetScreen();
    Window rootWindow = bsdApp->GetRootWindow();
    Visual* visual = bsdApp->GetVisual();
    Colormap colormap = bsdApp->GetColormap();
    
    // Validate resources
    if (!display || rootWindow == 0 || !visual) {
        std::cerr << "UltraCanvas BSD: Invalid X11 resources" << std::endl;
        return false;
    }
    
    // Validate dimensions
    if (config_.width <= 0 || config_.height <= 0 || 
        config_.width > 4096 || config_.height > 4096) {
        std::cerr << "UltraCanvas BSD: Invalid window dimensions: "
                  << config_.width << "x" << config_.height << std::endl;
        return false;
    }
    
    // Set window attributes
    XSetWindowAttributes attrs;
    memset(&attrs, 0, sizeof(attrs));
    
    attrs.background_pixel = BlackPixel(display, screen);
    attrs.border_pixel = BlackPixel(display, screen);
    attrs.colormap = colormap;
    attrs.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask |
                       ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                       StructureNotifyMask | FocusChangeMask | PropertyChangeMask |
                       EnterWindowMask | LeaveWindowMask;
    
    unsigned long valueMask = CWBackPixel | CWBorderPixel | CWColormap | CWEventMask;
    
    std::cout << "UltraCanvas BSD: Creating X11 window with dimensions: "
              << config_.width << "x" << config_.height
              << " at position: " << config_.x << "," << config_.y << std::endl;
    
    xWindow = XCreateWindow(
        display,
        rootWindow,
        config_.x, config_.y,
        config_.width, config_.height,
        0,  // border width
        CopyFromParent,  // depth
        InputOutput,  // class
        visual,
        valueMask,
        &attrs
    );
    
    if (xWindow == 0) {
        std::cerr << "UltraCanvas BSD: XCreateWindow() failed" << std::endl;
        return false;
    }
    
    // Set window title
    XStoreName(display, xWindow, config_.title.c_str());
    
    // Set WM_DELETE_WINDOW protocol
    Atom wmDeleteWindow = bsdApp->GetWMDeleteWindow();
    XSetWMProtocols(display, xWindow, &wmDeleteWindow, 1);
    
    // Set window size hints
    XSizeHints sizeHints;
    memset(&sizeHints, 0, sizeof(sizeHints));
    sizeHints.flags = PPosition | PSize;
    sizeHints.x = config_.x;
    sizeHints.y = config_.y;
    sizeHints.width = config_.width;
    sizeHints.height = config_.height;
    
    if (!config_.resizable) {
        sizeHints.flags |= PMinSize | PMaxSize;
        sizeHints.min_width = sizeHints.max_width = config_.width;
        sizeHints.min_height = sizeHints.max_height = config_.height;
    }
    
    XSetWMNormalHints(display, xWindow, &sizeHints);
    
    // Register window with application
    bsdApp->RegisterWindow(xWindow, this);
    
    std::cout << "UltraCanvas BSD: X11 window created successfully (ID: " 
              << xWindow << ")" << std::endl;
    
    return true;
}

bool UltraCanvasBSDWindow::CreateCairoSurface() {
    auto application = UltraCanvasApplication::GetInstance();
    if (!application) {
        return false;
    }
    
    auto bsdApp = static_cast<UltraCanvasBSDApplication*>(application);
    Display* display = bsdApp->GetDisplay();
    Visual* visual = bsdApp->GetVisual();
    
    if (!display || !visual || xWindow == 0) {
        std::cerr << "UltraCanvas BSD: Invalid parameters for Cairo surface" << std::endl;
        return false;
    }
    
    // Create Cairo surface for X11
    cairoSurface = cairo_xlib_surface_create(
        display,
        xWindow,
        visual,
        config_.width,
        config_.height
    );
    
    if (!cairoSurface || cairo_surface_status(cairoSurface) != CAIRO_STATUS_SUCCESS) {
        std::cerr << "UltraCanvas BSD: Failed to create Cairo surface" << std::endl;
        if (cairoSurface) {
            cairo_surface_destroy(cairoSurface);
            cairoSurface = nullptr;
        }
        return false;
    }
    
    // Create Cairo context
    cairoContext = cairo_create(cairoSurface);
    
    if (!cairoContext || cairo_status(cairoContext) != CAIRO_STATUS_SUCCESS) {
        std::cerr << "UltraCanvas BSD: Failed to create Cairo context" << std::endl;
        if (cairoContext) {
            cairo_destroy(cairoContext);
            cairoContext = nullptr;
        }
        cairo_surface_destroy(cairoSurface);
        cairoSurface = nullptr;
        return false;
    }
    
    std::cout << "UltraCanvas BSD: Cairo surface and context created successfully" << std::endl;
    return true;
}

void UltraCanvasBSDWindow::DestroyCairoSurface() {
    if (cairoContext) {
        cairo_destroy(cairoContext);
        cairoContext = nullptr;
    }
    
    if (cairoSurface) {
        cairo_surface_destroy(cairoSurface);
        cairoSurface = nullptr;
    }
}

void UltraCanvasBSDWindow::ResizeCairoSurface(int width, int height) {
    if (cairoSurface) {
        cairo_xlib_surface_set_size(cairoSurface, width, height);
    }
}

// ===== WINDOW MANAGEMENT =====
void UltraCanvasBSDWindow::Show() {
    if (!_created || xWindow == 0) {
        return;
    }
    
    auto application = UltraCanvasApplication::GetInstance();
    if (!application) {
        return;
    }
    
    auto bsdApp = static_cast<UltraCanvasBSDApplication*>(application);
    Display* display = bsdApp->GetDisplay();
    
    XMapWindow(display, xWindow);
    XFlush(display);
    
    _visible = true;
    std::cout << "UltraCanvas BSD: Window shown" << std::endl;
}

void UltraCanvasBSDWindow::Hide() {
    if (!_created || xWindow == 0) {
        return;
    }
    
    auto application = UltraCanvasApplication::GetInstance();
    if (!application) {
        return;
    }
    
    auto bsdApp = static_cast<UltraCanvasBSDApplication*>(application);
    Display* display = bsdApp->GetDisplay();
    
    XUnmapWindow(display, xWindow);
    XFlush(display);
    
    _visible = false;
    std::cout << "UltraCanvas BSD: Window hidden" << std::endl;
}

void UltraCanvasBSDWindow::Close() {
    if (!_created) {
        return;
    }
    
    std::cout << "UltraCanvas BSD: Closing window..." << std::endl;
    
    auto application = UltraCanvasApplication::GetInstance();
    if (application) {
        auto bsdApp = static_cast<UltraCanvasBSDApplication*>(application);
        Display* display = bsdApp->GetDisplay();
        
        if (xWindow && display) {
            bsdApp->UnregisterWindow(xWindow);
            
            DestroyCairoSurface();
            
            XDestroyWindow(display, xWindow);
            XFlush(display);
            xWindow = 0;
        }
    }
    
    _created = false;
    _visible = false;
    
    std::cout << "UltraCanvas BSD: Window closed" << std::endl;
}

void UltraCanvasBSDWindow::Minimize() {
    if (!_created || xWindow == 0) {
        return;
    }
    
    auto application = UltraCanvasApplication::GetInstance();
    if (!application) {
        return;
    }
    
    auto bsdApp = static_cast<UltraCanvasBSDApplication*>(application);
    Display* display = bsdApp->GetDisplay();
    int screen = bsdApp->GetScreen();
    
    XIconifyWindow(display, xWindow, screen);
    XFlush(display);
    
    std::cout << "UltraCanvas BSD: Window minimized" << std::endl;
}

void UltraCanvasBSDWindow::Maximize() {
    // TODO: Implement maximize using _NET_WM_STATE
    std::cout << "UltraCanvas BSD: Maximize not yet implemented" << std::endl;
}

void UltraCanvasBSDWindow::Restore() {
    // TODO: Implement restore
    std::cout << "UltraCanvas BSD: Restore not yet implemented" << std::endl;
}

void UltraCanvasBSDWindow::Focus() {
    if (!_created || xWindow == 0) {
        return;
    }
    
    auto application = UltraCanvasApplication::GetInstance();
    if (!application) {
        return;
    }
    
    auto bsdApp = static_cast<UltraCanvasBSDApplication*>(application);
    Display* display = bsdApp->GetDisplay();
    
    XRaiseWindow(display, xWindow);
    XSetInputFocus(display, xWindow, RevertToParent, CurrentTime);
    XFlush(display);
}

// ===== WINDOW PROPERTIES =====
void UltraCanvasBSDWindow::SetTitle(const std::string& title) {
    config_.title = title;
    
    if (!_created || xWindow == 0) {
        return;
    }
    
    auto application = UltraCanvasApplication::GetInstance();
    if (!application) {
        return;
    }
    
    auto bsdApp = static_cast<UltraCanvasBSDApplication*>(application);
    Display* display = bsdApp->GetDisplay();
    
    XStoreName(display, xWindow, title.c_str());
    XFlush(display);
}

void UltraCanvasBSDWindow::SetPosition(int x, int y) {
    config_.x = x;
    config_.y = y;
    
    if (!_created || xWindow == 0) {
        return;
    }
    
    auto application = UltraCanvasApplication::GetInstance();
    if (!application) {
        return;
    }
    
    auto bsdApp = static_cast<UltraCanvasBSDApplication*>(application);
    Display* display = bsdApp->GetDisplay();
    
    XMoveWindow(display, xWindow, x, y);
    XFlush(display);
}

void UltraCanvasBSDWindow::SetSize(int width, int height) {
    config_.width = width;
    config_.height = height;
    
    if (!_created || xWindow == 0) {
        return;
    }
    
    auto application = UltraCanvasApplication::GetInstance();
    if (!application) {
        return;
    }
    
    auto bsdApp = static_cast<UltraCanvasBSDApplication*>(application);
    Display* display = bsdApp->GetDisplay();
    
    XResizeWindow(display, xWindow, width, height);
    ResizeCairoSurface(width, height);
    XFlush(display);
    
    // Notify render context of resize
    if (renderContext) {
        // Render context will handle resize internally
    }
}

void UltraCanvasBSDWindow::SetFullscreen(bool fullscreen) {
    // TODO: Implement fullscreen using _NET_WM_STATE_FULLSCREEN
    std::cout << "UltraCanvas BSD: Fullscreen not yet implemented" << std::endl;
}

// ===== RENDERING =====
void UltraCanvasBSDWindow::Invalidate() {
    if (!_created || xWindow == 0) {
        return;
    }
    
    auto application = UltraCanvasApplication::GetInstance();
    if (!application) {
        return;
    }
    
    auto bsdApp = static_cast<UltraCanvasBSDApplication*>(application);
    Display* display = bsdApp->GetDisplay();
    
    // Send expose event to trigger repaint
    XEvent event;
    memset(&event, 0, sizeof(event));
    event.type = Expose;
    event.xexpose.window = xWindow;
    event.xexpose.x = 0;
    event.xexpose.y = 0;
    event.xexpose.width = config_.width;
    event.xexpose.height = config_.height;
    event.xexpose.count = 0;
    
    XSendEvent(display, xWindow, False, ExposureMask, &event);
    XFlush(display);
}

void UltraCanvasBSDWindow::SwapBuffers() {
    if (cairoSurface) {
        cairo_surface_flush(cairoSurface);
    }
    
    auto application = UltraCanvasApplication::GetInstance();
    if (application) {
        auto bsdApp = static_cast<UltraCanvasBSDApplication*>(application);
        Display* display = bsdApp->GetDisplay();
        if (display) {
            XFlush(display);
        }
    }
}

IRenderContext* UltraCanvasBSDWindow::GetRenderContext() {
    return renderContext.get();
}

// ===== EVENT HANDLING =====
void UltraCanvasBSDWindow::DispatchEvent(const UCEvent& event) {
    // Handle resize events specially
    if (event.type == UCEventType::WindowResized) {
        config_.width = event.size.width;
        config_.height = event.size.height;
        ResizeCairoSurface(event.size.width, event.size.height);
    }
    
    // Call base class event dispatch
    HandleEvent(event);
}

} // namespace UltraCanvas