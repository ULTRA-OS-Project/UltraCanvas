// OS/Linux/UltraCanvasLinuxWindow.cpp
// Complete Linux window implementation with all methods
// Version: 1.2.0 - per-monitor HiDPI: physical surface/window, hybrid DPI detect
// Last Modified: 2026-07-03
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasLinuxWindow.h"
#include "UltraCanvasImage.h"
#include "../libspecific/Cairo/RenderContextCairo.h"
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <X11/Xresource.h>
#include <X11/extensions/Xrandr.h>
#include "UltraCanvasDebug.h"

namespace UltraCanvas {

    // Snap a noisy XRandR-derived scale to the nearest common step so tiny
    // EDID/mm errors don't produce jittery fractional factors. Env/Xft.dpi
    // values are precise and honored as-is (not snapped).
    static float SnapDeviceScale(float s) {
        static const float steps[] = {1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f, 3.0f};
        float best = 1.0f, bestErr = 1e9f;
        for (float st : steps) {
            float e = std::fabs(st - s);
            if (e < bestErr) { bestErr = e; best = st; }
        }
        return best;
    }

// ===== CONSTRUCTOR & DESTRUCTOR =====
    UltraCanvasLinuxWindow::UltraCanvasLinuxWindow()
            : xWindow(0) {

        debugOutput << "UltraCanvas Linux: Window constructor completed successfully" << std::endl;
    }

// ===== WINDOW CREATION =====
    bool UltraCanvasLinuxWindow::CreateNative() {
        if (_created) {
            debugOutput << "UltraCanvas Linux: Window already created" << std::endl;
            return true;
        }
        auto application = UltraCanvasApplication::GetInstance();
        if (!application || !application->IsInitialized()) {
            debugOutput << "UltraCanvas Linux: Cannot create window - application not ready" << std::endl;
            return false;
        }

        debugOutput << "UltraCanvas Linux: Creating X11 window..." << std::endl;

        if (!CreateXWindow()) {
            debugOutput << "UltraCanvas Linux: Failed to create X11 window" << std::endl;
            return false;
        }

        // Apply window icon
        std::string iconToUse = config_.iconPath;
        if (iconToUse.empty()) {
            iconToUse = application->GetDefaultWindowIcon();
        }
        if (!iconToUse.empty()) {
            SetWindowIcon(iconToUse);
        }

        CreateXIC();

        if (!CreateNativeCairoSurface()) {
            debugOutput << "UltraCanvas Linux: Failed to create Cairo surface" << std::endl;
            XDestroyWindow(application->GetDisplay(), xWindow);
            xWindow = 0;
            return false;
        }

//        try {
//            renderContext = std::make_unique<RenderContextCairo>(cairoSurface, config_.width, config_.height, true);
//            debugOutput << "UltraCanvas Linux: Render context created successfully" << std::endl;
//        } catch (const std::exception& e) {
//            debugOutput << "UltraCanvas Linux: Failed to create render context: " << e.what() << std::endl;
//            DestroyNativeCairoSurface();
//            XDestroyWindow(UltraCanvasApplication::GetInstance()->GetDisplay(), xWindow);
//            xWindow = 0;
//            return false;
//        }

        debugOutput << "UltraCanvas Linux: Window created successfully!" << std::endl;
        return true;
    }

    bool UltraCanvasLinuxWindow::CreateXWindow() {
        auto application = UltraCanvasApplication::GetInstance();
        if (!application || !application->GetDisplay()) {
            debugOutput << "UltraCanvas Linux: Invalid application or display" << std::endl;
            return false;
        }

        Display* display = application->GetDisplay();
        int screen = application->GetScreen();
        Window rootWindow = application->GetRootWindow();
        Visual* visual = application->GetVisual();
        Colormap colormap = application->GetColormap();

        // Validate resources
        if (!display || rootWindow == 0 || !visual) {
            debugOutput << "UltraCanvas Linux: Invalid X11 resources" << std::endl;
            return false;
        }

        // Validate dimensions
        if (config_.width <= 0 || config_.height <= 0 || config_.width > 4096 || config_.height > 4096) {
            debugOutput << "UltraCanvas Linux: Invalid window dimensions: "
                      << config_.width << "x" << config_.height << std::endl;
            return false;
        }

        // Set window attributes
        XSetWindowAttributes attrs;
        memset(&attrs, 0, sizeof(attrs));

//        attrs.background_pixel = BlackPixel(display, screen);
//        attrs.border_pixel = BlackPixel(display, screen);
        attrs.backing_store = WhenMapped;
        attrs.colormap = colormap;
        attrs.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask |
                           ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                           StructureNotifyMask | FocusChangeMask | PropertyChangeMask |
                           EnterWindowMask | LeaveWindowMask;

//        unsigned long valueMask = CWBackPixel | CWBorderPixel | CWColormap | CWEventMask | CWBackingStore;
        unsigned long valueMask = CWColormap | CWEventMask | CWBackingStore;

        // Seed the device scale for the monitor this window opens on. X11 window
        // sizes/positions are in PHYSICAL px, so the client area must be created
        // at logical × deviceScale to appear the correct size on a HiDPI display.
        RefreshDeviceScale();
        const int physW = LogicalToPhysical(config_.width);
        const int physH = LogicalToPhysical(config_.height);

        debugOutput << "UltraCanvas Linux: Creating X11 window with dimensions: "
                  << config_.width << "x" << config_.height << " (logical) -> "
                  << physW << "x" << physH << " px @ " << deviceScale << "x"
                  << " at position: " << config_.x << "," << config_.y << std::endl;

        xWindow = XCreateWindow(
                display, rootWindow,
                config_.x, config_.y,
                physW, physH,
                0, // border width
                application->GetDepth(),
                InputOutput,
                visual,
                valueMask,
                &attrs
        );

        XSync(display, False);

        if (xWindow == 0) {
            debugOutput << "UltraCanvas Linux: XCreateWindow failed" << std::endl;
            return false;
        }

        debugOutput << "UltraCanvas Linux: X11 window created with ID: " << xWindow << std::endl;

        // An explicit initial position from the config counts as programmatic
        // placement and must be honored by the WM (USPosition in SetWindowHints).
        if (config_.x >= 0 && config_.y >= 0) {
            hasExplicitPosition = true;
        }

        SetWindowTitle(config_.title);
        SetWindowHints();

        // Tell the WM this is a dialog so it stacks and places it like one.
        if (config_.type == WindowType::Dialog) {
            Atom windowTypeAtom = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
            Atom dialogTypeAtom = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DIALOG", False);
            XChangeProperty(display, xWindow, windowTypeAtom, XA_ATOM, 32,
                            PropModeReplace,
                            reinterpret_cast<unsigned char*>(&dialogTypeAtom), 1);
        }
        if (config_.parentWindow) {
            SetTransientParent(config_.parentWindow);
        }

        // Apply borderless style if requested (remove window decorations)
        if (config_.type == WindowType::Borderless) {
            struct {
                unsigned long flags;
                unsigned long functions;
                unsigned long decorations;
                long inputMode;
                unsigned long status;
            } motifHints = {2, 0, 0, 0, 0};  // flags=MWM_HINTS_DECORATIONS, decorations=0

            Atom motifWmHints = XInternAtom(display, "_MOTIF_WM_HINTS", False);
            XChangeProperty(display, xWindow, motifWmHints, motifWmHints, 32,
                            PropModeReplace,
                            reinterpret_cast<unsigned char*>(&motifHints), 5);
        }

        // Set WM protocols
        Atom wmDeleteWindow = XInternAtom(display, "WM_DELETE_WINDOW", False);
        if (wmDeleteWindow != None) {
            XSetWMProtocols(display, xWindow, &wmDeleteWindow, 1);
        }

        // Initialize XDnD drag-and-drop support
        dragDropHandler.Initialize(display, xWindow);

        // Wire XDnD callbacks to dispatch UCEvents
        dragDropHandler.onFileDrop = [this](const std::vector<std::string>& paths, int x, int y) {
            UCEvent event;
            event.type = UCEventType::Drop;
            event.targetWindow = GetWindowWeakPtr();
            event.nativeWindowHandle = xWindow;
            // XDnD coords arrive in PHYSICAL px; convert to LOGICAL.
            event.pointerWindow = PhysicalToLogical(Point2Di{ x, y });
            event.pointer = event.pointerWindow;
            event.droppedFiles = paths;
            event.dragMimeType = "text/uri-list";
            // Join paths for legacy dragData compatibility
            std::string joined;
            for (const auto& p : paths) {
                if (!joined.empty()) joined += "\n";
                joined += p;
            }
            event.dragData = joined;
            UltraCanvasApplication::GetInstance()->PushEvent(event);
        };

        dragDropHandler.onDragEnter = [this](int x, int y) {
            UCEvent event;
            event.type = UCEventType::DragEnter;
            event.targetWindow = GetWindowWeakPtr();
            event.nativeWindowHandle = xWindow;
            // XDnD coords arrive in PHYSICAL px; convert to LOGICAL.
            event.pointerWindow = PhysicalToLogical(Point2Di{ x, y });
            event.pointer = event.pointerWindow;
            UltraCanvasApplication::GetInstance()->PushEvent(event);
        };

        dragDropHandler.onDragLeave = [this](int x, int y) {
            UCEvent event;
            event.type = UCEventType::DragLeave;
            event.targetWindow = GetWindowWeakPtr();
            event.nativeWindowHandle = xWindow;
            // XDnD coords arrive in PHYSICAL px; convert to LOGICAL.
            event.pointerWindow = PhysicalToLogical(Point2Di{ x, y });
            event.pointer = event.pointerWindow;
            UltraCanvasApplication::GetInstance()->PushEvent(event);
        };

        dragDropHandler.onDragOver = [this](int x, int y) {
            UCEvent event;
            event.type = UCEventType::DragOver;
            event.targetWindow = GetWindowWeakPtr();
            event.nativeWindowHandle = xWindow;
            // XDnD coords arrive in PHYSICAL px; convert to LOGICAL.
            event.pointerWindow = PhysicalToLogical(Point2Di{ x, y });
            event.pointer = event.pointerWindow;
            UltraCanvasApplication::GetInstance()->PushEvent(event);
        };        
        XSync(display, False);
        return true;
    }

    bool UltraCanvasLinuxWindow::CreateNativeCairoSurface() {
        auto application = UltraCanvasApplication::GetInstance();
        if (!application || !application->GetDisplay() || xWindow == 0) {
            debugOutput << "UltraCanvas Linux: Cannot create Cairo surface - invalid state" << std::endl;
            return false;
        }

        Display* display = application->GetDisplay();
        Visual* visual = application->GetVisual();

        // The X window is sized in PHYSICAL px (logical × deviceScale); the xlib
        // surface must match the drawable.
        const int physW = LogicalToPhysical(config_.width);
        const int physH = LogicalToPhysical(config_.height);

        nativeSurface = cairo_xlib_surface_create(
                display, xWindow, visual,
                physW, physH
        );

        if (!nativeSurface) {
            debugOutput << "UltraCanvas Linux: cairo_xlib_surface_create failed" << std::endl;
            return false;
        }

        // Tell Cairo 1 user unit = deviceScale device px; UI keeps drawing in
        // logical coordinates while Cairo rasterizes onto the larger pixel grid.
        cairo_surface_set_device_scale(static_cast<cairo_surface_t *>(nativeSurface),
                                       deviceScale, deviceScale);

        // Pin the surface fallback DPI to 96 so Pango's draw-time
        // pango_cairo_update_layout() doesn't pull in the X server's reported
        // DPI (often != 96 from xdpyinfo's physical-mm calculation) and
        // resync our text layout to a different scale than Windows.
        cairo_surface_set_fallback_resolution(static_cast<cairo_surface_t *>(nativeSurface), 96.0, 96.0);

        cairo_status_t status = cairo_surface_status(static_cast<cairo_surface_t *>(nativeSurface));
        if (status != CAIRO_STATUS_SUCCESS) {
            debugOutput << "UltraCanvas Linux: Cairo surface creation failed: "
                      << cairo_status_to_string(status) << std::endl;
            cairo_surface_destroy(static_cast<cairo_surface_t *>(nativeSurface));
            nativeSurface = nullptr;
            return false;
        }

        debugOutput << "UltraCanvas Linux: Cairo surface and context created successfully" << std::endl;
        return true;
    }

    void UltraCanvasLinuxWindow::GetWindowCenterInRootCoords(Display* display, int& cx, int& cy) const {
        // Determine the window's current position/size in PHYSICAL root coords.
        // Prefer live geometry (robust when the window has moved between monitors);
        // fall back to config when the window doesn't exist yet.
        int wx = config_.x, wy = config_.y;
        int pw = LogicalToPhysical(config_.width);
        int ph = LogicalToPhysical(config_.height);
        if (xWindow != 0) {
            Window child;
            XTranslateCoordinates(display, xWindow, DefaultRootWindow(display),
                                  0, 0, &wx, &wy, &child);
            XWindowAttributes attrs;
            if (XGetWindowAttributes(display, xWindow, &attrs)) {
                pw = attrs.width;
                ph = attrs.height;
            }
        }
        cx = wx + pw / 2;
        cy = wy + ph / 2;
    }

    float UltraCanvasLinuxWindow::QueryXRandRScaleForWindow(Display* display) const {
        if (!display) return 0.0f;
        Window root = DefaultRootWindow(display);

        int cx = 0, cy = 0;
        GetWindowCenterInRootCoords(display, cx, cy);

        int nmon = 0;
        XRRMonitorInfo* mons = XRRGetMonitors(display, root, True, &nmon);
        if (!mons || nmon <= 0) {
            if (mons) XRRFreeMonitors(mons);
            return 0.0f;
        }
        float scale = 0.0f;
        for (int i = 0; i < nmon; ++i) {
            const XRRMonitorInfo& m = mons[i];
            if (cx >= m.x && cx < m.x + m.width && cy >= m.y && cy < m.y + m.height) {
                if (m.mwidth > 0) {
                    double dpi = static_cast<double>(m.width) * 25.4 / static_cast<double>(m.mwidth);
                    if (dpi > 0.0) scale = SnapDeviceScale(static_cast<float>(dpi / 96.0));
                }
                break;
            }
        }
        XRRFreeMonitors(mons);
        return scale;
    }

    float UltraCanvasLinuxWindow::QueryNativeDeviceScale() const {
        // 1. Explicit desktop/toolkit env overrides (honored as-is, fractional OK).
        if (const char* s = getenv("GDK_SCALE")) {
            float v = static_cast<float>(atof(s));
            if (v > 0.0f) return v;
        }
        if (const char* s = getenv("QT_SCALE_FACTOR")) {
            float v = static_cast<float>(atof(s));
            if (v > 0.0f) return v;
        }

        auto* app = UltraCanvasApplication::GetInstance();
        Display* display = app ? app->GetDisplay() : nullptr;
        if (!display) return 1.0f;

        // 2. Xft.dpi from the X resource DB (what most desktops set, precise).
        if (char* rms = XResourceManagerString(display)) {
            if (XrmDatabase db = XrmGetStringDatabase(rms)) {
                char* type = nullptr;
                XrmValue val;
                float scale = 0.0f;
                if (XrmGetResource(db, "Xft.dpi", "Xft.Dpi", &type, &val) && val.addr) {
                    double dpi = atof(val.addr);
                    if (dpi > 0.0) scale = static_cast<float>(dpi / 96.0);
                }
                XrmDestroyDatabase(db);
                if (scale > 0.0f) return scale;
            }
        }

        // 3. XRandR per-monitor physical DPI for the monitor under the window.
        float randrScale = QueryXRandRScaleForWindow(display);
        if (randrScale > 0.0f) return randrScale;

        // 4. Default.
        return 1.0f;
    }

    bool UltraCanvasLinuxWindow::RecreateNativeSurface() {
        std::lock_guard<std::mutex> lock(cairoMutex);

        auto old = nativeSurface;
        if (!CreateNativeCairoSurface()) {
            return false;
        }
        if (old) {
            cairo_surface_destroy(static_cast<cairo_surface_t *>(old));
        }
        debugOutput << "UltraCanvasLinuxWindow::RecreateNativeSurface: rebuilt @ "
                    << deviceScale << "x" << std::endl;
        return true;
    }

    bool UltraCanvasLinuxWindow::CreateXIC() {
        xic = nullptr;
        auto application = dynamic_cast<UltraCanvasLinuxApplication*>(UltraCanvasApplication::GetInstance());
        auto xim = application->GetXIM();
        if (!xim) {
            debugOutput << "UltraCanvas: Cannot create XIC - XIM not initialized" << std::endl;
            return false;
        }

        // Query supported input styles
        XIMStyles* styles = nullptr;
        if (XGetIMValues(xim, XNQueryInputStyle, &styles, nullptr) != nullptr || !styles) {
            debugOutput << "UltraCanvas: Failed to query XIM input styles" << std::endl;
            return false;
        }

        // Find a suitable input style
        // Prefer: XIMPreeditNothing | XIMStatusNothing (simple, no on-the-spot editing)
        XIMStyle bestStyle = 0;
        XIMStyle preferredStyles[] = {
            XIMPreeditNothing | XIMStatusNothing,
            XIMPreeditNone | XIMStatusNone,
            0
        };

        for (int p = 0; preferredStyles[p] != 0; p++) {
            for (unsigned short i = 0; i < styles->count_styles; i++) {
                if (styles->supported_styles[i] == preferredStyles[p]) {
                    bestStyle = preferredStyles[p];
                    break;
                }
            }
            if (bestStyle != 0) break;
        }

        // Fallback: use the first available style
        if (bestStyle == 0 && styles->count_styles > 0) {
            bestStyle = styles->supported_styles[0];
        }

        XFree(styles);

        if (bestStyle == 0) {
            debugOutput << "UltraCanvas: No suitable XIM input style found" << std::endl;
            return false;
        }

        // Create the Input Context
        xic = XCreateIC(xim,
                           XNInputStyle, bestStyle,
                           XNClientWindow, xWindow,
                           XNFocusWindow, xWindow,
                           nullptr);

        if (!xic) {
            debugOutput << "UltraCanvas: XCreateIC() failed" << std::endl;
            return false;
        }

        debugOutput << "UltraCanvas: XIC created for window " << xWindow << std::endl;
        return true;
    }

    void UltraCanvasLinuxWindow::DestroyXIC() {
        if (xic) {
            XDestroyIC(xic);
            xic = nullptr;
        }
    }

    void UltraCanvasLinuxWindow::DestroyNativeCairoSurface() {
        if (nativeSurface) {
            debugOutput << "UltraCanvas Linux: Destroying Cairo surface..." << std::endl;
            cairo_surface_destroy(static_cast<cairo_surface_t *>(nativeSurface));
            nativeSurface = nullptr;
        }
    }

    void UltraCanvasLinuxWindow::DestroyNative() {
        debugOutput << "UltraCanvas Linux: Destroying window..." << std::endl;

        renderContext.reset();
        DestroyNativeCairoSurface();
        dragDropHandler.Shutdown();
        auto application = UltraCanvasApplication::GetInstance();
        if (xWindow && application && application->GetDisplay()) {        
            DestroyXIC();
            debugOutput << "UltraCanvas Linux: Destroying X11 window..." << std::endl;
            XDestroyWindow(application->GetDisplay(), xWindow);
            XSync(application->GetDisplay(), False);
            xWindow = 0;
        }
        debugOutput << "UltraCanvas Linux: Window destroyed successfully" << std::endl;
    }


    void UltraCanvasLinuxWindow::SetWindowTitle(const std::string& title) {
        config_.title = title;

        if (xWindow != 0) {
            auto application = UltraCanvasApplication::GetInstance();
            if (!application) {
                debugOutput << "UltraCanvas Linux: Application instance not available" << std::endl;
                return;
            }

            Display* display = application->GetDisplay();
            if (!display) {
                debugOutput << "UltraCanvas Linux: Display not available" << std::endl;
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

            debugOutput << "UltraCanvas Linux: Window title set to: \"" << title << "\"" << std::endl;
        }
    }

    void UltraCanvasLinuxWindow::SetWindowIcon(const std::string& iconPath) {
        if (xWindow == 0 || iconPath.empty()) {
            return;
        }

        auto application = UltraCanvasApplication::GetInstance();
        if (!application || !application->GetDisplay()) {
            return;
        }

        // Load the icon image
        auto img = UCImageRaster::Load(iconPath, false);
        if (!img || !img->IsValid()) {
            debugOutput << "UltraCanvas Linux: Failed to load icon: " << iconPath << std::endl;
            return;
        }

        // Get pixmap at original size
        auto pixmap = img->GetPixmap();
        if (!pixmap || !pixmap->IsValid()) {
            debugOutput << "UltraCanvas Linux: Failed to create pixmap for icon" << std::endl;
            return;
        }

        int w = pixmap->GetWidth();
        int h = pixmap->GetHeight();
        uint32_t* pixels = pixmap->GetPixelData();
        if (!pixels || w <= 0 || h <= 0) {
            return;
        }

        // Build _NET_WM_ICON data: [width, height, pixel0, pixel1, ...]
        // Each element must be unsigned long (8 bytes on 64-bit systems)
        size_t dataSize = 2 + (size_t)w * h;
        std::vector<unsigned long> iconData(dataSize);
        iconData[0] = (unsigned long)w;
        iconData[1] = (unsigned long)h;

        // Cairo uses premultiplied ARGB32, _NET_WM_ICON expects non-premultiplied ARGB.
        // For icon purposes, premultiplied is close enough and widely accepted by WMs.
        for (int i = 0; i < w * h; i++) {
            iconData[2 + i] = (unsigned long)pixels[i];
        }

        Display* display = application->GetDisplay();
        Atom netWmIcon = XInternAtom(display, "_NET_WM_ICON", False);

        XChangeProperty(display, xWindow, netWmIcon, XA_CARDINAL, 32,
                        PropModeReplace,
                        reinterpret_cast<const unsigned char*>(iconData.data()),
                        dataSize);

        XFlush(display);
        debugOutput << "UltraCanvas Linux: Window icon set (" << w << "x" << h << ") from: " << iconPath << std::endl;
    }

    void UltraCanvasLinuxWindow::SetWindowSize(int width, int height) {
        config_.width = width;
        config_.height = height;

        if (_created) {
            auto application = UltraCanvasApplication::GetInstance();
            // X11 window sizes are PHYSICAL px; config_ is LOGICAL.
            XResizeWindow(application->GetDisplay(), xWindow,
                          LogicalToPhysical(width), LogicalToPhysical(height));
            _needsResize = true;
//            UpdateCairoSurface(width, height);
        } else {
            UltraCanvasWindowBase::SetSize(width, height);
        }
    }

    void UltraCanvasLinuxWindow::SetWindowPosition(int x, int y) {
        config_.x = x;
        config_.y = y;
        hasExplicitPosition = true;

        if (_created) {
            auto application = UltraCanvasApplication::GetInstance();
            XMoveWindow(application->GetDisplay(), xWindow, x, y);
            // Refresh WM_NORMAL_HINTS while the window is still unmapped so the
            // WM honors this position at map time (dialogs are positioned
            // between creation and Show()).
            if (!_windowVisible) {
                SetWindowHints();
            }
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
        if (!_created || _windowVisible) {
            return;
        }

        debugOutput << "UltraCanvas Linux: Showing window..." << std::endl;
        auto application = UltraCanvasApplication::GetInstance();

        XMapWindow(application->GetDisplay(), xWindow);
        XFlush(application->GetDisplay());

        if (config_.type == WindowType::Fullscreen) {
            SetFullscreen(true);
        }

        _windowVisible = true;

        if (onWindowShow) {
            onWindowShow();
        }
        RequestRedraw();
    }

    void UltraCanvasLinuxWindow::Hide() {
        if (!_created || ! _windowVisible) {
            return;
        }

        debugOutput << "UltraCanvas Linux: Hiding window..." << std::endl;
        auto application = UltraCanvasApplication::GetInstance();
        XUnmapWindow(application->GetDisplay(), xWindow);
        XFlush(application->GetDisplay());

        _windowVisible = false;

        if (onWindowHide) {
            onWindowHide();
        }
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

        // A programmatically chosen position (e.g. a dialog centered on its
        // parent's monitor) must be exported via USPosition, otherwise the WM
        // is free to ignore it at map time and apply its own placement policy
        // (typically dropping the window on a different monitor).
        if (hasExplicitPosition) {
            hints.flags |= USPosition;
            hints.x = config_.x;
            hints.y = config_.y;
        }

        // WM size hints are enforced in PHYSICAL px; config limits are LOGICAL.
        if (config_.resizable) {
            hints.min_width  = LogicalToPhysical(config_.minWidth  > 0 ? config_.minWidth  : 100);
            hints.min_height = LogicalToPhysical(config_.minHeight > 0 ? config_.minHeight : 100);
            hints.max_width  = LogicalToPhysical(config_.maxWidth  > 0 ? config_.maxWidth  : 4096);
            hints.max_height = LogicalToPhysical(config_.maxHeight > 0 ? config_.maxHeight : 4096);
        } else {
            hints.min_width = hints.max_width = LogicalToPhysical(config_.width);
            hints.min_height = hints.max_height = LogicalToPhysical(config_.height);
        }

        XSetWMNormalHints(display, xWindow, &hints);
    }

    void UltraCanvasLinuxWindow::RaiseAndFocus() {
        auto application = UltraCanvasApplication::GetInstance();
        Display* display = application->GetDisplay();
        if (xWindow == 0 || !display) {
            return;
        }

        Atom net_active = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);

        XEvent event;
        std::memset(&event, 0, sizeof(event));

        event.xclient.type = ClientMessage;
        event.xclient.message_type = net_active;
        event.xclient.display = display;
        event.xclient.window = xWindow;
        event.xclient.format = 32;
        event.xclient.data.l[0] = 1; // source indication: application
        event.xclient.data.l[1] = CurrentTime;

        Window root = DefaultRootWindow(display);

        XSendEvent(
                display,
                root,
                False,
                SubstructureRedirectMask | SubstructureNotifyMask,
                &event
        );

        XFlush(display);
//        XSetInputFocus(display, xWindow, RevertToParent, CurrentTime);
    }

    void UltraCanvasLinuxWindow::DoResizeNative() {
        // Rebuild the xlib surface at the new physical size (logical × deviceScale)
        // with the device scale re-asserted. RecreateNativeSurface() takes
        // cairoMutex internally, so do not hold it here.
        RecreateNativeSurface();

        InvalidateWindowNative();
        XFlush(UltraCanvasApplication::GetInstance()->GetDisplay());

        debugOutput << "UltraCanvasLinuxWindow::DoResizeNative: Cairo surface updated successfully" << std::endl;
    }

    void UltraCanvasLinuxWindow::InvalidateWindowNative() {
//        cairo_surface_t *ctxSurface = static_cast<cairo_surface_t *>(renderContext->GetSurface());
//        cairo_surface_flush(stagingSurface);
//        // Copy staging surface to window surface
//        cairo_set_source_surface(targetContext, stagingSurface, 0, 0);
//        cairo_set_operator(targetContext, CAIRO_OPERATOR_SOURCE);
//        cairo_paint(targetContext);
//
//        cairo_surface_flush(cairoSurface);
//            XFlush(application->GetDisplay());
    }

// ===== EVENT HANDLING =====
    bool UltraCanvasLinuxWindow::HandleXEvent(const XEvent& event) {
        // Let the XDnD handler process drag-and-drop events first
        if (event.type == ClientMessage || event.type == SelectionNotify) {
            if (dragDropHandler.HandleXEvent(event)) {
                return true;
            }
        }
        return false;
    }

// ===== ACCESSORS =====
    NativeWindowHandle UltraCanvasLinuxWindow::GetNativeHandle() const {
        return xWindow;
    }

    void UltraCanvasLinuxWindow::GetWindowPosition(int& outX, int& outY) const {
        if (!_created) {
            outX = config_.x;
            outY = config_.y;
            return;
        }
        auto* app = UltraCanvasApplication::GetInstance();
        if (!app) {
            outX = config_.x;
            outY = config_.y;
            return;
        }
        Window child;
        XTranslateCoordinates(app->GetDisplay(), xWindow,
                              DefaultRootWindow(app->GetDisplay()),
                              0, 0, &outX, &outY, &child);
    }

    void UltraCanvasLinuxWindow::GetScreenSize(int& width, int& height) const {
        auto* app = UltraCanvasApplication::GetInstance();
        if (app && app->GetDisplay()) {
            Display* display = app->GetDisplay();
            int screen = app->GetScreen();
            width = XDisplayWidth(display, screen);
            height = XDisplayHeight(display, screen);
        } else {
            width = 0;
            height = 0;
        }
    }

    void UltraCanvasLinuxWindow::GetScreenBounds(int& x, int& y, int& width, int& height) const {
        // Fallback: the whole X screen. On multi-monitor X11 this spans ALL
        // monitors, so it's only used when XRandR gives us nothing better.
        x = 0;
        y = 0;
        GetScreenSize(width, height);

        auto* app = UltraCanvasApplication::GetInstance();
        if (!app || !app->GetDisplay()) return;
        Display* display = app->GetDisplay();
        Window root = DefaultRootWindow(display);

        int nmon = 0;
        XRRMonitorInfo* mons = XRRGetMonitors(display, root, True, &nmon);
        if (!mons || nmon <= 0) {
            if (mons) XRRFreeMonitors(mons);
            return;
        }

        int cx = 0, cy = 0;
        GetWindowCenterInRootCoords(display, cx, cy);

        // The monitor containing the window's center; the primary monitor when
        // the window isn't on any (not created yet or moved off-screen).
        int chosen = -1;
        int primary = 0;
        for (int i = 0; i < nmon; ++i) {
            const XRRMonitorInfo& m = mons[i];
            if (m.primary) primary = i;
            if (cx >= m.x && cx < m.x + m.width && cy >= m.y && cy < m.y + m.height) {
                chosen = i;
                break;
            }
        }
        if (chosen < 0) chosen = primary;

        x = mons[chosen].x;
        y = mons[chosen].y;
        width = mons[chosen].width;
        height = mons[chosen].height;
        XRRFreeMonitors(mons);
    }

    void UltraCanvasLinuxWindow::SetTransientParent(UltraCanvasWindowBase* parent) {
        if (!parent || parent == this) return;
        UltraCanvasWindowBase::SetTransientParent(parent);

        auto* app = UltraCanvasApplication::GetInstance();
        if (xWindow == 0 || !app || !app->GetDisplay()) return;
        Window parentXWindow = static_cast<Window>(parent->GetNativeHandle());
        if (parentXWindow != 0) {
            XSetTransientForHint(app->GetDisplay(), xWindow, parentXWindow);
        }
    }

// ===== MOUSE POINTER CONTROL =====
} // namespace UltraCanvas