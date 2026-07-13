// OS/MSWindows/UltraCanvasWindowsWindow.cpp
// Complete Windows window implementation with Cairo rendering
// Version: 1.1.0 - Per-Monitor HiDPI: physical surface/window, WM_DPICHANGED
// Last Modified: 2026-07-03
// Author: UltraCanvas Framework

#include "../../include/UltraCanvasWindow.h"
#include "../../include/UltraCanvasImage.h"
#include "UltraCanvasWindowsApplication.h"
#include <iostream>
#include "UltraCanvasDebug.h"

// Per-Monitor-V2 DPI message; may be absent from older SDK headers under
// WIN32_LEAN_AND_MEAN. All DPI entry points are resolved dynamically via
// GetProcAddress so no shcore/user32 import lib is required.
#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif
#ifndef MDT_EFFECTIVE_DPI
#define MDT_EFFECTIVE_DPI 0
#endif

namespace UltraCanvas {

// ===== CONSTRUCTOR =====
    UltraCanvasWindowsWindow::UltraCanvasWindowsWindow()
            : hwnd(nullptr)
            , hdc(nullptr)
            , dropTarget(nullptr)
            , trackingMouseLeave(false)
            , savedStyle(0)
            , savedExStyle(0) {
        std::memset(&savedPlacement, 0, sizeof(savedPlacement));
        savedPlacement.length = sizeof(WINDOWPLACEMENT);
    }

// ===== WINDOW CREATION =====

    bool UltraCanvasWindowsWindow::CreateNative() {
        if (_created) return true;

        auto* app = UltraCanvasWindowsApplication::GetInstance();
        if (!app || !app->IsInitialized()) {
            debugOutput << "UltraCanvas Windows: Application not initialized" << std::endl;
            return false;
        }

        debugOutput << "UltraCanvas Windows: Creating window..." << std::endl;

        // STEP 1: Create HWND
        if (!CreateHWND()) {
            debugOutput << "UltraCanvas Windows: Failed to create HWND" << std::endl;
            return false;
        }

        // STEP 2: Create Cairo surface from HWND's DC
        if (!CreateNativeCairoSurface()) {
            debugOutput << "UltraCanvas Windows: Failed to create Cairo surface" << std::endl;
            DestroyWindow(hwnd);
            hwnd = nullptr;
            return false;
        }

        // STEP 4: Initialize OLE drag-drop
        dropTarget = new UltraCanvasWindowsDropTarget(this);

        // Wire drag-drop callbacks to push UCEvents
        dropTarget->onFileDrop = [this](const std::vector<std::string>& paths, int x, int y) {
            UCEvent event;
            event.type = UCEventType::Drop;
            event.targetWindow = GetWindowWeakPtr();
            event.nativeWindowHandle = hwnd;
            // Drop coords arrive in PHYSICAL px; convert to LOGICAL.
            event.pointerWindow = PhysicalToLogical(Point2Di{ x, y });
            event.pointer = event.pointerWindow;
            event.droppedFiles = paths;
            event.dragMimeType = "text/uri-list";
            std::string joined;
            for (const auto& p : paths) {
                if (!joined.empty()) joined += "\n";
                joined += p;
            }
            event.dragData = joined;
            UltraCanvasWindowsApplication::GetInstance()->PushEvent(event);
        };

        dropTarget->onDragEnter = [this](int x, int y) {
            UCEvent event;
            event.type = UCEventType::DragEnter;
            event.targetWindow = GetWindowWeakPtr();
            event.nativeWindowHandle = hwnd;
            // Drop coords arrive in PHYSICAL px; convert to LOGICAL.
            event.pointerWindow = PhysicalToLogical(Point2Di{ x, y });
            event.pointer = event.pointerWindow;
            UltraCanvasWindowsApplication::GetInstance()->PushEvent(event);
        };

        dropTarget->onDragLeave = [this](int x, int y) {
            UCEvent event;
            event.type = UCEventType::DragLeave;
            event.targetWindow = GetWindowWeakPtr();
            event.nativeWindowHandle = hwnd;
            // Drop coords arrive in PHYSICAL px; convert to LOGICAL.
            event.pointerWindow = PhysicalToLogical(Point2Di{ x, y });
            event.pointer = event.pointerWindow;
            UltraCanvasWindowsApplication::GetInstance()->PushEvent(event);
        };

        dropTarget->onDragOver = [this](int x, int y) {
            UCEvent event;
            event.type = UCEventType::DragOver;
            event.targetWindow = GetWindowWeakPtr();
            event.nativeWindowHandle = hwnd;
            // Drop coords arrive in PHYSICAL px; convert to LOGICAL.
            event.pointerWindow = PhysicalToLogical(Point2Di{ x, y });
            event.pointer = event.pointerWindow;
            UltraCanvasWindowsApplication::GetInstance()->PushEvent(event);
        };

        RegisterDragDrop(hwnd, dropTarget);

        // Apply window icon
        std::string iconToUse = config_.iconPath;
        if (iconToUse.empty()) {
            iconToUse = app->GetDefaultWindowIcon();
        }
        if (!iconToUse.empty()) {
            SetWindowIcon(iconToUse);
        }

        _created = true;
        debugOutput << "UltraCanvas Windows: Window created successfully (HWND="
                  << hwnd << ")" << std::endl;
        return true;
    }

    bool UltraCanvasWindowsWindow::CreateHWND() {
        auto* app = UltraCanvasWindowsApplication::GetInstance();

        DWORD style = WS_OVERLAPPEDWINDOW;
        DWORD exStyle = WS_EX_APPWINDOW;

        // Adjust style based on WindowType
        switch (config_.type) {
            case WindowType::Dialog:
                style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
                exStyle |= WS_EX_DLGMODALFRAME;
                break;
            case WindowType::Popup:
                style = WS_POPUP | WS_BORDER;
                exStyle |= WS_EX_TOPMOST;
                break;
            case WindowType::Tool:
                style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME;
                exStyle |= WS_EX_TOOLWINDOW;
                break;
            case WindowType::Borderless:
                style = WS_POPUP;
                break;
            case WindowType::Fullscreen:
                style = WS_POPUP;
                break;
            default: // Standard
                break;
        }

        // Apply config flags
        if (!config_.resizable)   style &= ~WS_THICKFRAME;
        if (!config_.minimizable) style &= ~WS_MINIMIZEBOX;
        if (!config_.maximizable) style &= ~WS_MAXIMIZEBOX;
        if (config_.alwaysOnTop)  exStyle |= WS_EX_TOPMOST;

        // Seed the device scale from the monitor the window will open on. hwnd
        // doesn't exist yet, so QueryNativeDeviceScale() uses the monitor under
        // the configured position (GetDpiForMonitor / GetDpiForSystem). The
        // process is Per-Monitor-V2 aware, so CreateWindowExW client sizes are in
        // PHYSICAL px = logical × deviceScale.
        RefreshDeviceScale();

        // Calculate window rect to get correct client area size (physical px)
        RECT rect = {0, 0, LogicalToPhysical(config_.width), LogicalToPhysical(config_.height)};
        AdjustWindowRectForDpi(&rect, style, exStyle);

        int winWidth = rect.right - rect.left;
        int winHeight = rect.bottom - rect.top;
        int winX = (config_.x >= 0) ? config_.x : CW_USEDEFAULT;
        int winY = (config_.y >= 0) ? config_.y : CW_USEDEFAULT;

        std::wstring wTitle = UltraCanvasWindowsApplication::Utf8ToUtf16(config_.title);

        HWND parentHwnd = nullptr;
        if (config_.parentWindow) {
            parentHwnd = reinterpret_cast<HWND>(config_.parentWindow->GetNativeHandle());
        }

        // Determine window class to use
        const wchar_t* windowClass;
        if (!config_.windowClassSuffix.empty()) {
            windowClass = app->GetWindowClassName(config_.windowClassSuffix);
        } else {
            windowClass = app->GetWindowClassName();
        }

        hwnd = CreateWindowExW(
            exStyle,
            windowClass,
            wTitle.c_str(),
            style,
            winX, winY, winWidth, winHeight,
            parentHwnd,
            nullptr,               // No menu
            app->GetHInstance(),
            this                   // Pass 'this' to WM_NCCREATE via CREATESTRUCT::lpCreateParams
        );

        if (!hwnd) {
            debugOutput << "UltraCanvas Windows: CreateWindowExW failed: "
                      << GetLastError() << std::endl;
            return false;
        }

        hdc = GetDC(hwnd);
        if (!hdc) {
            debugOutput << "UltraCanvas Windows: GetDC failed" << std::endl;
            DestroyWindow(hwnd);
            hwnd = nullptr;
            return false;
        }

        trackingMouseLeave = false;
        return true;
    }

    bool UltraCanvasWindowsWindow::CreateNativeCairoSurface() {
        if (!hwnd) return false;

        // Now that the window exists, GetDpiForWindow() is authoritative — pull
        // the live scale so the surface is built at the correct physical size.
        RefreshDeviceScale();

        const int pixW = LogicalToPhysical(config_.width);
        const int pixH = LogicalToPhysical(config_.height);

        // Use a Cairo image surface instead of cairo_win32_surface.
        // This avoids DWM composition issues where rendering to a persistent
        // window DC outside of BeginPaint/EndPaint is not reliably displayed.
        // The image surface data is blitted to the window DC in WM_PAINT.
        // Allocate at PHYSICAL px so fonts/vectors rasterize at full DPI detail.
        nativeSurface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, pixW, pixH);
        if (!nativeSurface) {
            debugOutput << "UltraCanvas Windows: cairo_image_surface_create failed" << std::endl;
            return false;
        }

        // Tell Cairo 1 user unit = deviceScale device px; UI keeps drawing in
        // logical coordinates while Cairo rasterizes onto the larger pixel grid.
        cairo_surface_set_device_scale(static_cast<cairo_surface_t *>(nativeSurface),
                                       deviceScale, deviceScale);

        // Pin the surface fallback DPI to 96. Cairo image surfaces default to
        // 72 DPI, which would let Pango's draw-time pango_cairo_update_layout()
        // resync to a different scale than the cairo-xlib surface on Linux.
        cairo_surface_set_fallback_resolution(static_cast<cairo_surface_t *>(nativeSurface), 96.0, 96.0);

        cairo_status_t status = cairo_surface_status(static_cast<cairo_surface_t *>(nativeSurface));
        if (status != CAIRO_STATUS_SUCCESS) {
            debugOutput << "UltraCanvas Windows: Cairo surface error: "
                      << cairo_status_to_string(status) << std::endl;
            cairo_surface_destroy(static_cast<cairo_surface_t *>(nativeSurface));
            nativeSurface = nullptr;
            return false;
        }

        debugOutput << "UltraCanvas Windows: Cairo image surface created ("
                  << pixW << "x" << pixH << " px @ " << deviceScale << "x)" << std::endl;
        return true;
    }

    float UltraCanvasWindowsWindow::QueryNativeDeviceScale() const {
        UINT dpi = 96;
        HMODULE user32 = GetModuleHandleW(L"user32.dll");

        if (hwnd) {
            using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
            static auto pGetDpiForWindow = user32
                ? reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(user32, "GetDpiForWindow"))
                : nullptr;
            if (pGetDpiForWindow) {
                dpi = pGetDpiForWindow(hwnd);
            } else {
                HDC dc = GetDC(hwnd);
                if (dc) {
                    int d = GetDeviceCaps(dc, LOGPIXELSX);
                    if (d > 0) dpi = static_cast<UINT>(d);
                    ReleaseDC(hwnd, dc);
                }
            }
        } else {
            // No window yet: use the monitor under the configured position.
            POINT pt = { config_.x >= 0 ? config_.x : 0, config_.y >= 0 ? config_.y : 0 };
            HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
            bool got = false;
            if (HMODULE shcore = LoadLibraryW(L"shcore.dll")) {
                using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
                if (auto p = reinterpret_cast<GetDpiForMonitorFn>(GetProcAddress(shcore, "GetDpiForMonitor"))) {
                    UINT dx = 96, dy = 96;
                    if (SUCCEEDED(p(mon, MDT_EFFECTIVE_DPI, &dx, &dy)) && dx > 0) { dpi = dx; got = true; }
                }
                FreeLibrary(shcore);
            }
            if (!got && user32) {
                using GetDpiForSystemFn = UINT(WINAPI*)();
                if (auto p = reinterpret_cast<GetDpiForSystemFn>(GetProcAddress(user32, "GetDpiForSystem"))) {
                    UINT d = p();
                    if (d > 0) dpi = d;
                }
            }
        }
        return dpi > 0 ? dpi / 96.0f : 1.0f;
    }

    void UltraCanvasWindowsWindow::AdjustWindowRectForDpi(RECT* rect, DWORD style, DWORD exStyle) const {
        UINT dpi = static_cast<UINT>(std::lround(deviceScale * 96.0f));
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        using AdjustForDpiFn = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
        static auto pAdjust = user32
            ? reinterpret_cast<AdjustForDpiFn>(GetProcAddress(user32, "AdjustWindowRectExForDpi"))
            : nullptr;
        if (pAdjust) {
            pAdjust(rect, style, FALSE, exStyle, dpi);
        } else {
            AdjustWindowRectEx(rect, style, FALSE, exStyle);
        }
    }

    bool UltraCanvasWindowsWindow::RecreateNativeSurface() {
        std::lock_guard<std::mutex> lock(cairoMutex);

        if (nativeSurface) {
            cairo_surface_destroy(static_cast<cairo_surface_t *>(nativeSurface));
            nativeSurface = nullptr;
        }
        if (!CreateNativeCairoSurface()) {
            debugOutput << "UltraCanvas Windows: Failed to recreate Cairo surface" << std::endl;
            return false;
        }
        debugOutput << "UltraCanvasWindowsWindow::RecreateNativeSurface: nativeh=" << GetNativeHandle()
                    << " surface=" << nativeSurface << " @ " << deviceScale << "x" << std::endl;
        return true;
    }

    void UltraCanvasWindowsWindow::DestroyNativeCairoSurface() {
        if (nativeSurface) {
            cairo_surface_destroy(static_cast<cairo_surface_t *>(nativeSurface));
            nativeSurface = nullptr;
        }
    }

    void UltraCanvasWindowsWindow::HandleResizeEventWindows(int w, int h) {
        HandleResizeEvent(w, h);
        DoResize();
        RequestWindowComposition();
        UpdateAndRender();

        debugOutput << "UltraCanvasWindowsWindow::HandleResizeEventWindows: nativeh=" << GetNativeHandle() << " updated successfully" << std::endl;
    }

    void UltraCanvasWindowsWindow::DoResizeNative() {
        // Rebuild the image surface at the current logical size × deviceScale.
        RecreateNativeSurface();
    }

    void UltraCanvasWindowsWindow::DestroyNative() {
        if (!_created) return;

        // Revoke drag-drop registration
        if (hwnd) {
            RevokeDragDrop(hwnd);
        }

        // Release drop target
        if (dropTarget) {
            dropTarget->Release();
            dropTarget = nullptr;
        }

        // Destroy render context first (uses cairo surface)
        renderContext.reset();

        // Destroy Cairo surface
        DestroyNativeCairoSurface();

        // Release DC
        if (hdc && hwnd) {
            ReleaseDC(hwnd, hdc);
            hdc = nullptr;
        }

        // Destroy icons
        if (hIconBig) { DestroyIcon(hIconBig); hIconBig = nullptr; }
        if (hIconSmall) { DestroyIcon(hIconSmall); hIconSmall = nullptr; }

        // Destroy window
        if (hwnd) {
            DestroyWindow(hwnd);
            hwnd = nullptr;
        }

        _created = false;
        debugOutput << "UltraCanvas Windows: Window destroyed" << std::endl;
    }

// ===== WNDPROC MESSAGE HANDLER =====

    LRESULT UltraCanvasWindowsWindow::HandleMessage(
            HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {

        switch (msg) {
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC paintDC = BeginPaint(h, &ps);
                // Blit the Cairo image surface to the window
                BlitSurfaceToHDC(paintDC);
                EndPaint(h, &ps);
                return 0;
            }

            case WM_SIZE: {
                // LOWORD/HIWORD are PHYSICAL client px (Per-Monitor-V2 aware).
                // Convert to LOGICAL before feeding the framework.
                int w_new = PhysicalToLogical(LOWORD(lParam));
                int h_new = PhysicalToLogical(HIWORD(lParam));
                // Track window state from SIZE message
                if (wParam == SIZE_MINIMIZED)
                    _state = WindowState::Minimized;
                else if (wParam == SIZE_MAXIMIZED)
                    _state = WindowState::Maximized;
                else if (wParam == SIZE_RESTORED)
                    _state = WindowState::Normal;

                if (w_new > 0 && h_new > 0) {
                    HandleResizeEventWindows(w_new, h_new);
                }
                return 0;
            }

            case WM_DPICHANGED: {
                // The window was moved to a display with a different DPI (or the
                // display scale changed). wParam LOWORD = new DPI; lParam = the
                // OS-suggested window RECT (physical px, in the new DPI).
                deviceScale = LOWORD(wParam) / 96.0f;
                if (deviceScale <= 0.0f) deviceScale = 1.0f;

                const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
                if (suggested) {
                    // Reposition/resize to the suggested rect (required by the
                    // WM_DPICHANGED contract). This fires a nested WM_SIZE, now
                    // computed against the already-updated deviceScale.
                    SetWindowPos(h, nullptr,
                                 suggested->left, suggested->top,
                                 suggested->right - suggested->left,
                                 suggested->bottom - suggested->top,
                                 SWP_NOZORDER | SWP_NOACTIVATE);
                }
                // Rebuild nativeSurface + renderContext at the new scale, relayout
                // and repaint (authoritative final state after the nested WM_SIZE).
                HandleDeviceScaleChange();
                return 0;
            }

            case WM_CLOSE: {
                // Don't call DestroyWindow here - let the framework handle it
                // via WindowCloseRequest event (pushed by ProcessWindowMessage)
                return 0;
            }

            case WM_DESTROY: {
                return 0;
            }

            case WM_ERASEBKGND: {
                // Return 1 to prevent GDI from erasing the background
                // We paint everything ourselves via Cairo
                return 1;
            }

            case WM_MOUSEMOVE: {
                // Track mouse for WM_MOUSELEAVE notifications
                if (!trackingMouseLeave) {
                    TRACKMOUSEEVENT tme = {};
                    tme.cbSize = sizeof(tme);
                    tme.dwFlags = TME_LEAVE;
                    tme.hwndTrack = h;
                    TrackMouseEvent(&tme);
                    trackingMouseLeave = true;
                }
                break;  // Let ProcessWindowMessage handle the event
            }

            case WM_MOUSELEAVE: {
                trackingMouseLeave = false;
                break;  // Let ProcessWindowMessage handle the event
            }

            case WM_GETMINMAXINFO: {
                auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
                DWORD style = static_cast<DWORD>(GetWindowLongW(h, GWL_STYLE));
                DWORD exStyle = static_cast<DWORD>(GetWindowLongW(h, GWL_EXSTYLE));

                if (config_.minWidth > 0 && config_.minHeight > 0) {
                    // Track sizes are PHYSICAL px; config limits are LOGICAL.
                    RECT r = {0, 0, LogicalToPhysical(config_.minWidth), LogicalToPhysical(config_.minHeight)};
                    AdjustWindowRectForDpi(&r, style, exStyle);
                    mmi->ptMinTrackSize.x = r.right - r.left;
                    mmi->ptMinTrackSize.y = r.bottom - r.top;
                }
                if (config_.maxWidth > 0 && config_.maxHeight > 0) {
                    RECT r = {0, 0, LogicalToPhysical(config_.maxWidth), LogicalToPhysical(config_.maxHeight)};
                    AdjustWindowRectForDpi(&r, style, exStyle);
                    mmi->ptMaxTrackSize.x = r.right - r.left;
                    mmi->ptMaxTrackSize.y = r.bottom - r.top;
                }
                return 0;
            }

            case WM_SETCURSOR: {
                // Handle cursor in client area - prevent Windows from resetting it
                if (LOWORD(lParam) == HTCLIENT) {
                    auto* app = UltraCanvasWindowsApplication::GetInstance();
                    if (app) {
                        app->SelectMouseCursorNative(this, currentMouseCursor);
                        return TRUE;
                    }
                }
                break;
            }
        }

        return DefWindowProcW(h, msg, wParam, lParam);
    }

// ===== WINDOW OPERATIONS =====

    void UltraCanvasWindowsWindow::Show() {
        if (!_created || _windowVisible) return;
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);

        _windowVisible = true;

        if (onWindowShow) onWindowShow();
    }

    void UltraCanvasWindowsWindow::Hide() {
        if (!_created || !_windowVisible) return;
        ShowWindow(hwnd, SW_HIDE);

        _windowVisible = false;

        if (onWindowHide) onWindowHide();
    }

    void UltraCanvasWindowsWindow::RaiseAndFocus() {
        if (!_created) return;

        SetForegroundWindow(hwnd);
        SetFocus(hwnd);
    }

    void UltraCanvasWindowsWindow::SetWindowTitle(const std::string& title) {
        config_.title = title;
        if (hwnd) {
            std::wstring wTitle = UltraCanvasWindowsApplication::Utf8ToUtf16(title);
            SetWindowTextW(hwnd, wTitle.c_str());
        }
    }

    void UltraCanvasWindowsWindow::SetWindowIcon(const std::string& iconPath) {
        if (!hwnd || iconPath.empty()) return;

        // Load the icon image
        auto img = UCImageRaster::Load(iconPath, false);
        if (!img || !img->IsValid()) {
            debugOutput << "UltraCanvas Windows: Failed to load icon: " << iconPath << std::endl;
            return;
        }

        auto pixmap = img->GetPixmap();
        if (!pixmap || !pixmap->IsValid()) {
            debugOutput << "UltraCanvas Windows: Failed to create pixmap for icon" << std::endl;
            return;
        }

        int w = pixmap->GetWidth();
        int h = pixmap->GetHeight();
        uint32_t* pixels = pixmap->GetPixelData();
        if (!pixels || w <= 0 || h <= 0) return;

        // Helper lambda to create HICON from ARGB pixel data at a given size
        auto createIcon = [&](int targetW, int targetH) -> HICON {
            // Get pixmap at target size
            auto sizedPixmap = img->GetPixmap(targetW, targetH);
            if (!sizedPixmap || !sizedPixmap->IsValid()) return nullptr;

            int pw = sizedPixmap->GetWidth();
            int ph = sizedPixmap->GetHeight();
            uint32_t* px = sizedPixmap->GetPixelData();
            if (!px) return nullptr;

            // Create a 32-bit ARGB DIB section
            BITMAPV5HEADER bi = {};
            bi.bV5Size = sizeof(BITMAPV5HEADER);
            bi.bV5Width = pw;
            bi.bV5Height = -ph; // top-down
            bi.bV5Planes = 1;
            bi.bV5BitCount = 32;
            bi.bV5Compression = BI_BITFIELDS;
            bi.bV5RedMask   = 0x00FF0000;
            bi.bV5GreenMask = 0x0000FF00;
            bi.bV5BlueMask  = 0x000000FF;
            bi.bV5AlphaMask = 0xFF000000;

            void* dibBits = nullptr;
            HDC screenDC = GetDC(nullptr);
            HBITMAP hBitmap = CreateDIBSection(screenDC,
                reinterpret_cast<BITMAPINFO*>(&bi),
                DIB_RGB_COLORS, &dibBits, nullptr, 0);
            ReleaseDC(nullptr, screenDC);

            if (!hBitmap || !dibBits) return nullptr;

            // Copy pixel data — Cairo ARGB32 premultiplied to Windows ARGB
            // Both use the same byte layout (BGRA in memory on little-endian)
            memcpy(dibBits, px, pw * ph * 4);

            // Create mask bitmap (all zeros = fully opaque, alpha is in the color bitmap)
            HBITMAP hMask = CreateBitmap(pw, ph, 1, 1, nullptr);

            ICONINFO iconInfo = {};
            iconInfo.fIcon = TRUE;
            iconInfo.hbmColor = hBitmap;
            iconInfo.hbmMask = hMask;

            HICON icon = CreateIconIndirect(&iconInfo);

            DeleteObject(hBitmap);
            DeleteObject(hMask);

            return icon;
        };

        // Clean up previous icons
        if (hIconBig) { DestroyIcon(hIconBig); hIconBig = nullptr; }
        if (hIconSmall) { DestroyIcon(hIconSmall); hIconSmall = nullptr; }

        // Create icons at standard sizes
        hIconBig = createIcon(48, 48);    // Taskbar / Alt-Tab
        hIconSmall = createIcon(16, 16);  // Title bar

        if (hIconBig) {
            SendMessage(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIconBig));
        }
        if (hIconSmall) {
            SendMessage(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIconSmall));
        }

        debugOutput << "UltraCanvas Windows: Window icon set from: " << iconPath << std::endl;
    }

    void UltraCanvasWindowsWindow::SetWindowSize(int width, int height) {
        config_.width = width;
        config_.height = height;

        if (hwnd) {
            // Calculate window size from desired client area. width/height are
            // LOGICAL; the native client area is PHYSICAL = logical × deviceScale.
            DWORD style = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_STYLE));
            DWORD exStyle = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_EXSTYLE));
            RECT rect = {0, 0, LogicalToPhysical(width), LogicalToPhysical(height)};
            AdjustWindowRectForDpi(&rect, style, exStyle);

            SetWindowPos(hwnd, nullptr, 0, 0,
                         rect.right - rect.left, rect.bottom - rect.top,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            debugOutput << "UltraCanvasWindowsWindow::SetWindowSize nativeh=" << GetNativeHandle() << " (" << width << "x" << height << ")" << std::endl;
        }
    }

    void UltraCanvasWindowsWindow::SetWindowPosition(int x, int y) {
        config_.x = x;
        config_.y = y;

        if (hwnd) {
            SetWindowPos(hwnd, nullptr, x, y, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    void UltraCanvasWindowsWindow::SetResizable(bool resizable) {
        config_.resizable = resizable;

        if (hwnd) {
            LONG style = GetWindowLongW(hwnd, GWL_STYLE);
            if (resizable) {
                style |= WS_THICKFRAME;
            } else {
                style &= ~WS_THICKFRAME;
            }
            SetWindowLongW(hwnd, GWL_STYLE, style);
            // Apply style change
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        }
    }

    void UltraCanvasWindowsWindow::Minimize() {
        if (!_created) return;
        ShowWindow(hwnd, SW_MINIMIZE);
        _state = WindowState::Minimized;
        if (onWindowMinimize) onWindowMinimize();
        debugOutput << "UltraCanvasWindowsWindow::Minimize nativeh=" << GetNativeHandle() << std::endl;
    }

    void UltraCanvasWindowsWindow::Maximize() {
        if (!_created) return;
        ShowWindow(hwnd, SW_MAXIMIZE);
        _state = WindowState::Maximized;
        if (onWindowMaximize) onWindowMaximize();
        debugOutput << "UltraCanvasWindowsWindow::Maximize nativeh=" << GetNativeHandle() << std::endl;
    }

    void UltraCanvasWindowsWindow::Restore() {
        if (!_created) return;
        ShowWindow(hwnd, SW_RESTORE);
        _state = WindowState::Normal;
        if (onWindowRestore) onWindowRestore();
        debugOutput << "UltraCanvasWindowsWindow::Restore nativeh=" << GetNativeHandle() << std::endl;
    }

    void UltraCanvasWindowsWindow::SetFullscreen(bool fullscreen) {
        if (!_created) return;

        if (fullscreen) {
            // Save current window state
            GetWindowPlacement(hwnd, &savedPlacement);
            savedStyle = GetWindowLongW(hwnd, GWL_STYLE);
            savedExStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);

            // Remove window borders and title bar
            SetWindowLongW(hwnd, GWL_STYLE,
                           savedStyle & ~(WS_CAPTION | WS_THICKFRAME));
            SetWindowLongW(hwnd, GWL_EXSTYLE,
                           savedExStyle & ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE |
                                            WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));

            // Get the monitor that contains the window
            MONITORINFO mi = {sizeof(mi)};
            GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);

            // Resize to fill the entire monitor
            SetWindowPos(hwnd, HWND_TOP,
                         mi.rcMonitor.left, mi.rcMonitor.top,
                         mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

            _state = WindowState::Fullscreen;
            debugOutput << "UltraCanvasWindowsWindow::SetFullscreen(true) nativeh=" << GetNativeHandle() << std::endl;
        } else {
            // Restore saved window state
            SetWindowLongW(hwnd, GWL_STYLE, savedStyle);
            SetWindowLongW(hwnd, GWL_EXSTYLE, savedExStyle);
            SetWindowPlacement(hwnd, &savedPlacement);
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            debugOutput << "UltraCanvasWindowsWindow::SetFullscreen(false) nativeh=" << GetNativeHandle() << std::endl;
            _state = WindowState::Normal;
        }
    }

    void UltraCanvasWindowsWindow::InvalidateWindowNative() {
        if (!renderContext || !nativeSurface || !hwnd) return;
        // Trigger a synchronous WM_PAINT to blit the image surface to the window
        ::InvalidateRect(hwnd, NULL, FALSE);
        UpdateWindow(hwnd);
    }

    void UltraCanvasWindowsWindow::BlitSurfaceToHDC(HDC targetDC) {
        if (!nativeSurface || !targetDC) return;

        cairo_surface_flush(static_cast<cairo_surface_t*>(nativeSurface));

        int width = cairo_image_surface_get_width(static_cast<cairo_surface_t*>(nativeSurface));
        int height = cairo_image_surface_get_height(static_cast<cairo_surface_t*>(nativeSurface));
        unsigned char* data = cairo_image_surface_get_data(static_cast<cairo_surface_t*>(nativeSurface));
        if (!data) return;

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = width;
        bmi.bmiHeader.biHeight = -height;  // Negative = top-down DIB
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        SetDIBitsToDevice(targetDC, 0, 0, width, height,
                          0, 0, 0, height,
                          data, &bmi, DIB_RGB_COLORS);
        //debugOutput << "UltraCanvasWindowsWindow::BlitSurfaceToHDC hativeh=" << GetNativeHandle() << " surface=" << cairoSurface << std::endl;
    }

    NativeWindowHandle UltraCanvasWindowsWindow::GetNativeHandle() const {
        return reinterpret_cast<NativeWindowHandle>(hwnd);
    }

    void UltraCanvasWindowsWindow::GetWindowPosition(int& x, int& y) const {
        if (hwnd) {
            RECT r;
            GetWindowRect(hwnd, &r);
            x = r.left;
            y = r.top;
        } else {
            x = config_.x;
            y = config_.y;
        }
    }

    void UltraCanvasWindowsWindow::GetScreenSize(int& width, int& height) const {
        if (hwnd) {
            MONITORINFO mi = {sizeof(mi)};
            GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
            width = mi.rcMonitor.right - mi.rcMonitor.left;
            height = mi.rcMonitor.bottom - mi.rcMonitor.top;
        } else {
            width = GetSystemMetrics(SM_CXSCREEN);
            height = GetSystemMetrics(SM_CYSCREEN);
        }
    }

    void UltraCanvasWindowsWindow::GetScreenBounds(int& x, int& y, int& width, int& height) const {
        MONITORINFO mi = {sizeof(mi)};
        HMONITOR monitor = hwnd
            ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)
            : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
        if (GetMonitorInfoW(monitor, &mi)) {
            x = mi.rcMonitor.left;
            y = mi.rcMonitor.top;
            width = mi.rcMonitor.right - mi.rcMonitor.left;
            height = mi.rcMonitor.bottom - mi.rcMonitor.top;
        } else {
            x = 0;
            y = 0;
            width = GetSystemMetrics(SM_CXSCREEN);
            height = GetSystemMetrics(SM_CYSCREEN);
        }
    }
} // namespace UltraCanvas
