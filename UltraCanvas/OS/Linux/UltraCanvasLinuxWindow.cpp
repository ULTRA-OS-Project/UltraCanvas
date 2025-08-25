// UltraCanvasLinuxWindow.cpp
// Complete Linux window implementation with all methods
// Version: 1.1.0 - Complete implementation
// Last Modified: 2025-07-16
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasLinuxWindow.h"
#include <iostream>
#include <cstring>

namespace UltraCanvas {

// ===== CONSTRUCTOR & DESTRUCTOR =====
    UltraCanvasLinuxWindow::UltraCanvasLinuxWindow(const WindowConfig& config)
            : UltraCanvasBaseWindow(config)
            , xWindow(0)
            , cairoSurface(nullptr)
            , cairoContext(nullptr) {

        std::cout << "UltraCanvas Linux: Window constructor completed successfully" << std::endl;
    }

// ===== WINDOW CREATION =====
    bool UltraCanvasLinuxWindow::CreateNative(const WindowConfig& config) {
        if (created_) {
            std::cout << "UltraCanvas Linux: Window already created" << std::endl;
            return true;
        }
        auto application = UltraCanvasApplication::GetInstance();
        if (!application || !application->IsInitialized()) {
            std::cerr << "UltraCanvas Linux: Cannot create window - application not ready" << std::endl;
            return false;
        }

        std::cout << "UltraCanvas Linux: Creating X11 window..." << std::endl;

        if (!CreateXWindow()) {
            std::cerr << "UltraCanvas Linux: Failed to create X11 window" << std::endl;
            return false;
        }

        if (!CreateCairoSurface()) {
            std::cerr << "UltraCanvas Linux: Failed to create Cairo surface" << std::endl;
            XDestroyWindow(application->GetDisplay(), xWindow);
            xWindow = 0;
            return false;
        }

        try {
            renderContext = std::make_unique<LinuxRenderContext>(cairoContext);
            std::cout << "UltraCanvas Linux: Render context created successfully" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "UltraCanvas Linux: Failed to create render context: " << e.what() << std::endl;
            DestroyCairoSurface();
            XDestroyWindow(UltraCanvasApplication::GetInstance()->GetDisplay(), xWindow);
            xWindow = 0;
            return false;
        }

        RenderContextManager::RegisterWindowContext(this, renderContext.get());

        created_ = true;

        std::cout << "UltraCanvas Linux: Window created successfully!" << std::endl;
        return true;
    }

    bool UltraCanvasLinuxWindow::CreateXWindow() {
        auto application = UltraCanvasApplication::GetInstance();
        if (!application || !application->GetDisplay()) {
            std::cerr << "UltraCanvas Linux: Invalid application or display" << std::endl;
            return false;
        }

        Display* display = application->GetDisplay();
        int screen = application->GetScreen();
        Window rootWindow = application->GetRootWindow();
        Visual* visual = application->GetVisual();
        Colormap colormap = application->GetColormap();

        // Validate resources
        if (!display || rootWindow == 0 || !visual) {
            std::cerr << "UltraCanvas Linux: Invalid X11 resources" << std::endl;
            return false;
        }

        // Validate dimensions
        if (config_.width <= 0 || config_.height <= 0 || config_.width > 4096 || config_.height > 4096) {
            std::cerr << "UltraCanvas Linux: Invalid window dimensions: "
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

        std::cout << "UltraCanvas Linux: Creating X11 window with dimensions: "
                  << config_.width << "x" << config_.height
                  << " at position: " << config_.x << "," << config_.y << std::endl;

        xWindow = XCreateWindow(
                display, rootWindow,
                config_.x, config_.y,
                config_.width, config_.height,
                0, // border width
                application->GetDepth(),
                InputOutput,
                visual,
                valueMask,
                &attrs
        );

        XSync(display, False);

        if (xWindow == 0) {
            std::cerr << "UltraCanvas Linux: XCreateWindow failed" << std::endl;
            return false;
        }

        std::cout << "UltraCanvas Linux: X11 window created with ID: " << xWindow << std::endl;

        SetWindowTitle(config_.title);
        SetWindowHints();

        // Set WM protocols
        Atom wmDeleteWindow = XInternAtom(display, "WM_DELETE_WINDOW", False);
        if (wmDeleteWindow != None) {
            XSetWMProtocols(display, xWindow, &wmDeleteWindow, 1);
        }

        XSync(display, False);
        return true;
    }

    bool UltraCanvasLinuxWindow::CreateCairoSurface() {
        auto application = UltraCanvasApplication::GetInstance();
        if (!application || !application->GetDisplay() || xWindow == 0) {
            std::cerr << "UltraCanvas Linux: Cannot create Cairo surface - invalid state" << std::endl;
            return false;
        }

        Display* display = application->GetDisplay();
        Visual* visual = application->GetVisual();

        cairoSurface = cairo_xlib_surface_create(
                display, xWindow, visual,
                config_.width, config_.height
        );

        if (!cairoSurface) {
            std::cerr << "UltraCanvas Linux: cairo_xlib_surface_create failed" << std::endl;
            return false;
        }

        cairo_status_t status = cairo_surface_status(cairoSurface);
        if (status != CAIRO_STATUS_SUCCESS) {
            std::cerr << "UltraCanvas Linux: Cairo surface creation failed: "
                      << cairo_status_to_string(status) << std::endl;
            cairo_surface_destroy(cairoSurface);
            cairoSurface = nullptr;
            return false;
        }

        cairoContext = cairo_create(cairoSurface);
        if (!cairoContext) {
            std::cerr << "UltraCanvas Linux: cairo_create failed" << std::endl;
            cairo_surface_destroy(cairoSurface);
            cairoSurface = nullptr;
            return false;
        }

        status = cairo_status(cairoContext);
        if (status != CAIRO_STATUS_SUCCESS) {
            std::cerr << "UltraCanvas Linux: Cairo context creation failed: "
                      << cairo_status_to_string(status) << std::endl;
            cairo_destroy(cairoContext);
            cairo_surface_destroy(cairoSurface);
            cairoContext = nullptr;
            cairoSurface = nullptr;
            return false;
        }

        std::cout << "UltraCanvas Linux: Cairo surface and context created successfully" << std::endl;
        return true;
    }

    void UltraCanvasLinuxWindow::DestroyCairoSurface() {
        if (cairoContext) {
            std::cout << "UltraCanvas Linux: Destroying Cairo context..." << std::endl;
            cairo_destroy(cairoContext);
            cairoContext = nullptr;
        }

        if (cairoSurface) {
            std::cout << "UltraCanvas Linux: Destroying Cairo surface..." << std::endl;
            cairo_surface_destroy(cairoSurface);
            cairoSurface = nullptr;
        }
    }

    void UltraCanvasLinuxWindow::Destroy() {
        std::cout << "UltraCanvas Linux: Destroying window..." << std::endl;

        RenderContextManager::UnregisterWindowContext(this);

        renderContext.reset();
        DestroyCairoSurface();
        auto application = UltraCanvasApplication::GetInstance();
        if (xWindow && application && application->GetDisplay()) {
            std::cout << "UltraCanvas Linux: Destroying X11 window..." << std::endl;
            XDestroyWindow(application->GetDisplay(), xWindow);
            XSync(application->GetDisplay(), False);
            xWindow = 0;
        }

        std::cout << "UltraCanvas Linux: Window destroyed successfully" << std::endl;
    }


    void UltraCanvasLinuxWindow::SetWindowTitle(const std::string& title) {
        config_.title = title;

        auto application = UltraCanvasApplication::GetInstance();
        if (created_) {
            XStoreName(application->GetDisplay(), xWindow, title.c_str());

            Atom netWmName = XInternAtom(application->GetDisplay(), "_NET_WM_NAME", False);
            Atom utf8String = XInternAtom(application->GetDisplay(), "UTF8_STRING", False);
            XChangeProperty(application->GetDisplay(), xWindow, netWmName, utf8String, 8,
                            PropModeReplace, (unsigned char*)title.c_str(), title.length());
        }
    }

    void UltraCanvasLinuxWindow::SetWindowSize(int width, int height) {
        config_.width = width;
        config_.height = height;

        if (created_) {
            auto application = UltraCanvasApplication::GetInstance();
            XResizeWindow(application->GetDisplay(), xWindow, width, height);
            UpdateCairoSurface();
        }
        UltraCanvasBaseWindow::SetSize(width, height);
    }

    void UltraCanvasLinuxWindow::SetWindowPosition(int x, int y) {
        config_.x = x;
        config_.y = y;

        if (created_) {
            auto application = UltraCanvasApplication::GetInstance();
            XMoveWindow(application->GetDisplay(), xWindow, x, y);
        }
        //UltraCanvasBaseWindow::SetPosition(x, y);
    }

    void UltraCanvasLinuxWindow::SetResizable(bool resizable) {
        config_.resizable = resizable;

        if (created_) {
            SetWindowHints();
        }
    }

    // ===== WINDOW STATE MANAGEMENT =====
    void UltraCanvasLinuxWindow::Show() {
        if (!created_ || visible_) {
            return;
        }

        std::cout << "UltraCanvas Linux: Showing window..." << std::endl;
        auto application = UltraCanvasApplication::GetInstance();

        XMapWindow(application->GetDisplay(), xWindow);
        XFlush(application->GetDisplay());

        visible_ = true;

        if (onWindowShow) {
            onWindowShow();
        }
    }

    void UltraCanvasLinuxWindow::Hide() {
        if (!created_ || !visible_) {
            return;
        }

        std::cout << "UltraCanvas Linux: Hiding window..." << std::endl;
        auto application = UltraCanvasApplication::GetInstance();
        XUnmapWindow(application->GetDisplay(), xWindow);
        XFlush(application->GetDisplay());

        visible_ = false;

        if (onWindowHide) {
            onWindowHide();
        }
    }

    void UltraCanvasLinuxWindow::Close() {
        if (!created_ || state_ == WindowState::Closing) {
            return;
        }

        state_ = WindowState::Closing;

        if (onWindowClose) {
            onWindowClose();
        }

        Hide();
        Destroy();

        std::cout << "UltraCanvas: Window close completed" << std::endl;
    }

    void UltraCanvasLinuxWindow::Minimize() {
        if (!created_) {
            return;
        }
        auto application = UltraCanvasApplication::GetInstance();

        Display* display = application->GetDisplay();
        XIconifyWindow(display, xWindow, application->GetScreen());
        state_ = WindowState::Minimized;
    }

    void UltraCanvasLinuxWindow::Maximize() {
        if (!created_) {
            return;
        }
        auto application = UltraCanvasApplication::GetInstance();

        Display* display = application->GetDisplay();
        Atom wmState = XInternAtom(display, "_NET_WM_STATE", False);
        Atom wmStateMaxHorz = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
        Atom wmStateMaxVert = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_VERT", False);

        XEvent event;
        memset(&event, 0, sizeof(event));
        event.type = ClientMessage;
        event.xclient.window = xWindow;
        event.xclient.message_type = wmState;
        event.xclient.format = 32;
        event.xclient.data.l[0] = 1; // _NET_WM_STATE_ADD
        event.xclient.data.l[1] = wmStateMaxHorz;
        event.xclient.data.l[2] = wmStateMaxVert;

        XSendEvent(display, application->GetRootWindow(), False,
                   SubstructureNotifyMask | SubstructureRedirectMask, &event);

        state_ = WindowState::Maximized;
    }

    void UltraCanvasLinuxWindow::Restore() {
        if (!created_) {
            return;
        }
        auto application = UltraCanvasApplication::GetInstance();

        Display* display = application->GetDisplay();

        if (state_ == WindowState::Maximized) {
            Atom wmState = XInternAtom(display, "_NET_WM_STATE", False);
            Atom wmStateMaxHorz = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
            Atom wmStateMaxVert = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_VERT", False);

            XEvent event;
            memset(&event, 0, sizeof(event));
            event.type = ClientMessage;
            event.xclient.window = xWindow;
            event.xclient.message_type = wmState;
            event.xclient.format = 32;
            event.xclient.data.l[0] = 0; // _NET_WM_STATE_REMOVE
            event.xclient.data.l[1] = wmStateMaxHorz;
            event.xclient.data.l[2] = wmStateMaxVert;

            XSendEvent(display, application->GetRootWindow(), False,
                       SubstructureNotifyMask | SubstructureRedirectMask, &event);
        } else if (state_ == WindowState::Minimized) {
            XMapWindow(display, xWindow);
        }

        state_ = WindowState::Normal;
    }

    void UltraCanvasLinuxWindow::SetFullscreen(bool fullscreen) {
        if (!created_) {
            return;
        }
        auto application = UltraCanvasApplication::GetInstance();

        Display* display = application->GetDisplay();
        Atom wmState = XInternAtom(display, "_NET_WM_STATE", False);
        Atom wmStateFullscreen = XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", False);

        XEvent event;
        memset(&event, 0, sizeof(event));
        event.type = ClientMessage;
        event.xclient.window = xWindow;
        event.xclient.message_type = wmState;
        event.xclient.format = 32;
        event.xclient.data.l[0] = fullscreen ? 1 : 0;
        event.xclient.data.l[1] = wmStateFullscreen;

        XSendEvent(display, application->GetRootWindow(), False,
                   SubstructureNotifyMask | SubstructureRedirectMask, &event);

        state_ = fullscreen ? WindowState::Fullscreen : WindowState::Normal;
    }

    void UltraCanvasLinuxWindow::SetWindowHints() {
        auto application = UltraCanvasApplication::GetInstance();
        if (!created_ || !application) {
            return;
        }
        Display* display = application->GetDisplay();

        XSizeHints hints;
        memset(&hints, 0, sizeof(hints));

        hints.flags = PMinSize | PMaxSize;

        if (config_.resizable) {
            hints.min_width = config_.minWidth > 0 ? config_.minWidth : 100;
            hints.min_height = config_.minHeight > 0 ? config_.minHeight : 100;
            hints.max_width = config_.maxWidth > 0 ? config_.maxWidth : 4096;
            hints.max_height = config_.maxHeight > 0 ? config_.maxHeight : 4096;
        } else {
            hints.min_width = hints.max_width = config_.width;
            hints.min_height = hints.max_height = config_.height;
        }

        XSetWMNormalHints(display, xWindow, &hints);
    }

    void UltraCanvasLinuxWindow::UpdateCairoSurface() {
        std::lock_guard<std::mutex> lock(cairoMutex);  // Add this
        if (cairoSurface) {
            cairo_xlib_surface_set_size(cairoSurface, config_.width, config_.height);
        }

        // Reset state without invalidating context
        if (renderContext) {
            renderContext->ResetState();
        }

        std::cout << "UltraCanvas Linux: Cairo surface updated successfully" << std::endl;
    }

// ===== RENDERING =====
    void UltraCanvasLinuxWindow::Render() {
        if (!created_ || !visible_ || !cairoContext) {
            return;
        }

//        // Validate Cairo context before use
//        cairo_status_t status = cairo_status(cairoContext);
//        if (status != CAIRO_STATUS_SUCCESS) {
//            std::cerr << "UltraCanvas Linux: Cairo context invalid during render: "
//                      << cairo_status_to_string(status) << std::endl;
//            return;
//        }
//
//        // Validate Cairo surface
//        if (cairoSurface) {
//            status = cairo_surface_status(cairoSurface);
//            if (status != CAIRO_STATUS_SUCCESS) {
//                std::cerr << "UltraCanvas Linux: Cairo surface invalid during render: "
//                          << cairo_status_to_string(status) << std::endl;
//                return;
//            }
//        }

        // Set up rendering context
//        int width, height;
//        GetSize(width, height);
//        SetFillColor(Color(245, 248, 255, 255)); // Light blue background
//        FillRectangle(Rect2D(0, 0, width, height));

        // Call base class render
        UltraCanvasBaseWindow::Render();

    }

    void UltraCanvasLinuxWindow::SwapBuffers() {
        if (cairoSurface) {
            auto application = UltraCanvasApplication::GetInstance();
            // Validate surface before flushing
//            cairo_status_t status = cairo_surface_status(cairoSurface);
//            if (status == CAIRO_STATUS_SUCCESS) {
            cairo_surface_flush(cairoSurface);
            XFlush(application->GetDisplay());
//            } else {
//                std::cerr << "UltraCanvas Linux: Cannot flush invalid Cairo surface: "
//                          << cairo_status_to_string(status) << std::endl;
//            }
        }
    }

// ===== EVENT HANDLING =====
    void UltraCanvasLinuxWindow::HandleXEvent(const XEvent& event) {
        switch (event.type) {
            case ConfigureNotify:
                if (event.xconfigure.width != config_.width ||
                    event.xconfigure.height != config_.height) {
                    OnResize(event.xconfigure.width, event.xconfigure.height);
                }
                if (event.xconfigure.x != config_.x ||
                    event.xconfigure.y != config_.y) {
                    OnMove(event.xconfigure.x, event.xconfigure.y);
                }
                break;

            case FocusIn:
                OnFocusChanged(true);
                break;

            case FocusOut:
                OnFocusChanged(false);
                break;

            case MapNotify:
                OnMapStateChanged(true);
                break;

            case UnmapNotify:
                OnMapStateChanged(false);
                break;

            case Expose:
                if (event.xexpose.count == 0) {
                    // Only render on the last expose event
                    Render();
                }
                break;

            default:
                // Other events are handled by the application
                break;
        }
    }

    bool UltraCanvasLinuxWindow::OnEvent(const UCEvent& event) {
        ULTRACANVAS_WINDOW_RENDER_SCOPE(this);

        // Handle window-level events first
        switch (event.type) {
            case UCEventType::WindowClose:
                Close();
                return true;

            case UCEventType::WindowResize:
                // Resize is handled in HandleXEvent
                break;

            case UCEventType::WindowRepaint:
                Render();
                break;

            case UCEventType::WindowFocus:
//                application->SetFocusedWindow(this);
                break;

            case UCEventType::WindowBlur:
//                if (application->GetFocusedWindow() == this) {
//                    application->SetFocusedWindow(nullptr);
//                }
                break;

            default:
                // Pass other events to base class
                return UltraCanvasBaseWindow::OnEvent(event);
                break;
        }
        return false;
    }

// ===== INTERNAL EVENT HANDLERS =====
    void UltraCanvasLinuxWindow::OnResize(int width, int height) {
        std::cout << "UltraCanvas Linux: Window resize event to " << width << "x" << height << std::endl;
        config_.width = width;
        config_.height = height;

        UpdateCairoSurface();

        if (onWindowResize) {
            onWindowResize(width, height);
        }

        needsRedraw_ = true;

        std::cout << "UltraCanvas Linux: Window resized to " << width << "x" << height << std::endl;
    }

    void UltraCanvasLinuxWindow::OnMove(int x, int y) {
        config_.x = x;
        config_.y = y;

        if (onWindowMove) {
            onWindowMove(x, y);
        }

        std::cout << "UltraCanvas Linux: Window moved to " << x << "," << y << std::endl;
    }

    void UltraCanvasLinuxWindow::OnFocusChanged(bool focused) {
        auto application = UltraCanvasApplication::GetInstance();
        if (focused) {
            application->SetFocusedWindow(this);
            if (onWindowFocus) {
                onWindowFocus();
            }
        } else {
            if (application->GetFocusedWindow() == this) {
                application->SetFocusedWindow(nullptr);
            }
            if (onWindowBlur) {
                onWindowBlur();
            }
        }
    }

    void UltraCanvasLinuxWindow::OnMapStateChanged(bool mapped) {
        visible_ = mapped;

        if (mapped) {
            if (onWindowShow) {
                onWindowShow();
            }
        } else {
            if (onWindowHide) {
                onWindowHide();
            }
        }
    }

// ===== ACCESSORS =====
    void* UltraCanvasLinuxWindow::GetNativeHandle() const {
        return reinterpret_cast<void*>(xWindow);
    }

} // namespace UltraCanvas