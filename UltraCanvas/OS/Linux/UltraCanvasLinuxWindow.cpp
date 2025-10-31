// OS/Linux/UltraCanvasLinuxWindow.cpp
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
    UltraCanvasLinuxWindow::UltraCanvasLinuxWindow()
            : xWindow(0)
            , cairoSurface(nullptr)
            , cairoContext(nullptr) {

        std::cout << "UltraCanvas Linux: Window constructor completed successfully" << std::endl;
    }

    UltraCanvasLinuxWindow::UltraCanvasLinuxWindow(const WindowConfig& config)
            : xWindow(0)
            , cairoSurface(nullptr)
            , cairoContext(nullptr)
            {
        if (!Create(config)) {
            throw std::runtime_error("UltraCanvasWindow Create failed");
        }
        std::cout << "UltraCanvas Linux: Window constructor completed successfully" << std::endl;
    }

    UltraCanvasLinuxWindow::~UltraCanvasLinuxWindow() {
        if (_created) {
            DestroyNative();
        }
    }

// ===== WINDOW CREATION =====
    bool UltraCanvasLinuxWindow::CreateNative(const WindowConfig& config) {
        if (_created) {
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
            renderContext = std::make_unique<LinuxRenderContext>(cairoContext, cairoSurface, config_.width, config_.height, true);
            std::cout << "UltraCanvas Linux: Render context created successfully" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "UltraCanvas Linux: Failed to create render context: " << e.what() << std::endl;
            DestroyCairoSurface();
            XDestroyWindow(UltraCanvasApplication::GetInstance()->GetDisplay(), xWindow);
            xWindow = 0;
            return false;
        }

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

    void UltraCanvasLinuxWindow::DestroyNative() {
        std::cout << "UltraCanvas Linux: Destroying window..." << std::endl;

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

        if (xWindow != 0) {
            auto application = UltraCanvasApplication::GetInstance();
            if (!application) {
                std::cerr << "UltraCanvas Linux: Application instance not available" << std::endl;
                return;
            }

            Display* display = application->GetDisplay();
            if (!display) {
                std::cerr << "UltraCanvas Linux: Display not available" << std::endl;
                return;
            }

            // Set the window title using XStoreName
            XStoreName(display, xWindow, title.c_str());

            // Also set the _NET_WM_NAME property for modern window managers
            Atom netWmName = XInternAtom(display, "_NET_WM_NAME", False);
            Atom utf8String = XInternAtom(display, "UTF8_STRING", False);

            if (netWmName != None && utf8String != None) {
                XChangeProperty(display, xWindow, netWmName, utf8String, 8,
                                PropModeReplace,
                                reinterpret_cast<const unsigned char*>(title.c_str()),
                                title.length());
            }

            // Also set WM_NAME for compatibility with older window managers
            XTextProperty textProp;
            char *title_str = const_cast<char *>(title.c_str());
            if (XStringListToTextProperty(&title_str, 1, &textProp) != 0) {
                XSetWMName(display, xWindow, &textProp);
                XFree(textProp.value);
            }

            // CRITICAL: Flush the X11 display to ensure changes are sent to the X server
            XFlush(display);

            std::cout << "UltraCanvas Linux: Window title set to: \"" << title << "\"" << std::endl;
        }
    }

    void UltraCanvasLinuxWindow::SetWindowSize(int width, int height) {
        config_.width = width;
        config_.height = height;

        if (_created) {
            auto application = UltraCanvasApplication::GetInstance();
            XResizeWindow(application->GetDisplay(), xWindow, width, height);
            UpdateCairoSurface(width, height);
        }
        UltraCanvasWindowBase::SetSize(width, height);
    }

    void UltraCanvasLinuxWindow::SetWindowPosition(int x, int y) {
        config_.x = x;
        config_.y = y;

        if (_created) {
            auto application = UltraCanvasApplication::GetInstance();
            XMoveWindow(application->GetDisplay(), xWindow, x, y);
        }
        //UltraCanvasWindowBase::SetPosition(x, y);
    }

    void UltraCanvasLinuxWindow::SetResizable(bool resizable) {
        config_.resizable = resizable;

        if (_created) {
            SetWindowHints();
        }
    }

    // ===== WINDOW STATE MANAGEMENT =====
    void UltraCanvasLinuxWindow::Show() {
        if (!_created || _visible) {
            return;
        }

        std::cout << "UltraCanvas Linux: Showing window..." << std::endl;
        auto application = UltraCanvasApplication::GetInstance();

        XMapWindow(application->GetDisplay(), xWindow);
        XFlush(application->GetDisplay());

        if (config_.type == WindowType::Fullscreen) {
            SetFullscreen(true);
        }

        _visible = true;

        if (onWindowShow) {
            onWindowShow();
        }
    }

    void UltraCanvasLinuxWindow::Hide() {
        if (!_created || !_visible) {
            return;
        }

        std::cout << "UltraCanvas Linux: Hiding window..." << std::endl;
        auto application = UltraCanvasApplication::GetInstance();
        XUnmapWindow(application->GetDisplay(), xWindow);
        XFlush(application->GetDisplay());

        _visible = false;

        if (onWindowHide) {
            onWindowHide();
        }
    }

    void UltraCanvasLinuxWindow::Close() {
        if (!_created || _state == WindowState::Closing) {
            return;
        }

        _state = WindowState::Closing;

        if (onWindowClose) {
            onWindowClose();
        }

        Hide();

        DestroyNative();
        Destroy();
        std::cout << "UltraCanvas: Window close completed" << std::endl;
    }

    void UltraCanvasLinuxWindow::Minimize() {
        if (!_created) {
            return;
        }
        auto application = UltraCanvasApplication::GetInstance();

        Display* display = application->GetDisplay();
        XIconifyWindow(display, xWindow, application->GetScreen());
        _state = WindowState::Minimized;
    }

    void UltraCanvasLinuxWindow::Maximize() {
        if (!_created) {
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

        _state = WindowState::Maximized;
    }

    void UltraCanvasLinuxWindow::Restore() {
        if (!_created) {
            return;
        }
        auto application = UltraCanvasApplication::GetInstance();

        Display* display = application->GetDisplay();

        if (_state == WindowState::Maximized) {
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
        } else if (_state == WindowState::Minimized) {
            XMapWindow(display, xWindow);
        }

        _state = WindowState::Normal;
    }

    void UltraCanvasLinuxWindow::SetFullscreen(bool fullscreen) {
        if (xWindow == 0) {
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

        _state = fullscreen ? WindowState::Fullscreen : WindowState::Normal;
    }

    void UltraCanvasLinuxWindow::SetWindowHints() {
        if (xWindow == 0) {
            return;
        }
        auto application = UltraCanvasApplication::GetInstance();
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

    void UltraCanvasLinuxWindow::UpdateCairoSurface(int w, int h) {
        std::lock_guard<std::mutex> lock(cairoMutex);  // Add this
        if (cairoSurface) {
            cairo_xlib_surface_set_size(cairoSurface, w, h);
        }

        if (renderContext) {
            renderContext->OnWindowResize(w, h);
        }

        std::cout << "UltraCanvas Linux: Cairo surface updated successfully" << std::endl;
    }

    void UltraCanvasLinuxWindow::HandleResizeEvent(int w, int h) {
        if (config_.width != w || config_.height != h) {
            UpdateCairoSurface(w, h);
            UltraCanvasWindowBase::HandleResizeEvent(w, h);
            Flush();
        }
    }

    void UltraCanvasLinuxWindow::Flush() {
        renderContext->Flush();
//            XFlush(application->GetDisplay());
    }

// ===== EVENT HANDLING =====
    bool UltraCanvasLinuxWindow::HandleXEvent(const XEvent& event) {
        return false;
    }

// ===== ACCESSORS =====
    unsigned long UltraCanvasLinuxWindow::GetNativeHandle() const {
        return xWindow;
    }

} // namespace UltraCanvas