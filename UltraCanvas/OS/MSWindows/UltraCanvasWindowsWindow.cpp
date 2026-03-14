// OS/MSWindows/UltraCanvasWindowsWindow.cpp
// Complete Windows window implementation with Cairo rendering
// Version: 1.0.0
// Last Modified: 2026-03-06
// Author: UltraCanvas Framework

#include "../../include/UltraCanvasWindow.h"
#include "../../include/UltraCanvasImage.h"
#include "UltraCanvasWindowsApplication.h"
#include <iostream>
#include "UltraCanvasDebug.h"

namespace UltraCanvas {

// ===== CONSTRUCTOR =====
    UltraCanvasWindowsWindow::UltraCanvasWindowsWindow()
            : hwnd(nullptr)
            , hdc(nullptr)
            , cairoSurface(nullptr)
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
        if (!CreateCairoSurface()) {
            debugOutput << "UltraCanvas Windows: Failed to create Cairo surface" << std::endl;
            DestroyWindow(hwnd);
            hwnd = nullptr;
            return false;
        }

        // STEP 3: Create render context (same RenderContextCairo as Linux)
        try {
            renderContext = std::make_unique<RenderContextCairo>(
                cairoSurface, config_.width, config_.height, true);
        } catch (const std::exception& e) {
            debugOutput << "UltraCanvas Windows: Failed to create render context: "
                      << e.what() << std::endl;
            DestroyCairoSurface();
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
            event.targetWindow = this;
            event.nativeWindowHandle = hwnd;
            event.x = event.windowX = x;
            event.y = event.windowY = y;
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
            event.targetWindow = this;
            event.nativeWindowHandle = hwnd;
            event.x = event.windowX = x;
            event.y = event.windowY = y;
            UltraCanvasWindowsApplication::GetInstance()->PushEvent(event);
        };

        dropTarget->onDragLeave = [this](int x, int y) {
            UCEvent event;
            event.type = UCEventType::DragLeave;
            event.targetWindow = this;
            event.nativeWindowHandle = hwnd;
            event.x = event.windowX = x;
            event.y = event.windowY = y;
            UltraCanvasWindowsApplication::GetInstance()->PushEvent(event);
        };

        dropTarget->onDragOver = [this](int x, int y) {
            UCEvent event;
            event.type = UCEventType::DragOver;
            event.targetWindow = this;
            event.nativeWindowHandle = hwnd;
            event.x = event.windowX = x;
            event.y = event.windowY = y;
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

        // Calculate window rect to get correct client area size
        RECT rect = {0, 0, config_.width, config_.height};
        AdjustWindowRectEx(&rect, style, FALSE, exStyle);

        int winWidth = rect.right - rect.left;
        int winHeight = rect.bottom - rect.top;
        int winX = (config_.x >= 0) ? config_.x : CW_USEDEFAULT;
        int winY = (config_.y >= 0) ? config_.y : CW_USEDEFAULT;

        std::wstring wTitle = UltraCanvasWindowsApplication::Utf8ToUtf16(config_.title);

        HWND parentHwnd = nullptr;
        if (config_.parentWindow) {
            parentHwnd = reinterpret_cast<HWND>(config_.parentWindow->GetNativeHandle());
        }

        hwnd = CreateWindowExW(
            exStyle,
            UltraCanvasWindowsApplication::GetWindowClassName(),
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

    bool UltraCanvasWindowsWindow::CreateCairoSurface() {
        if (!hwnd) return false;

        // Use a Cairo image surface instead of cairo_win32_surface.
        // This avoids DWM composition issues where rendering to a persistent
        // window DC outside of BeginPaint/EndPaint is not reliably displayed.
        // The image surface data is blitted to the window DC in WM_PAINT.
        cairoSurface = cairo_image_surface_create(CAIRO_FORMAT_RGB24,
                                                   config_.width, config_.height);
        if (!cairoSurface) {
            debugOutput << "UltraCanvas Windows: cairo_image_surface_create failed" << std::endl;
            return false;
        }

        cairo_status_t status = cairo_surface_status(cairoSurface);
        if (status != CAIRO_STATUS_SUCCESS) {
            debugOutput << "UltraCanvas Windows: Cairo surface error: "
                      << cairo_status_to_string(status) << std::endl;
            cairo_surface_destroy(cairoSurface);
            cairoSurface = nullptr;
            return false;
        }

        debugOutput << "UltraCanvas Windows: Cairo image surface created ("
                  << config_.width << "x" << config_.height << ")" << std::endl;
        return true;
    }

    void UltraCanvasWindowsWindow::DestroyCairoSurface() {
        if (cairoSurface) {
            cairo_surface_destroy(cairoSurface);
            cairoSurface = nullptr;
        }
    }

    void UltraCanvasWindowsWindow::HandleResizeEventWindows(int w, int h) {
        HandleResizeEvent(w, h);
        DoResize();
        Render(GetRenderContext());
        Flush();
        ClearRequestRedraw();

        debugOutput << "UltraCanvasWindowsWindow::HandleResizeEventWindows: nativeh=" << GetNativeHandle() << " updated successfully" << std::endl;
    }

    void UltraCanvasWindowsWindow::DoResizeNative() {
        std::lock_guard<std::mutex> lock(cairoMutex);
        int w = config_.width;
        int h = config_.height;

        if (cairoSurface) {
            cairo_surface_destroy(cairoSurface);
            cairoSurface = nullptr;
        }

        cairoSurface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
        if (!cairoSurface) {
            debugOutput << "UltraCanvas Windows: Failed to recreate Cairo surface on resize" << std::endl;
            return;
        }

        if (renderContext) {
            renderContext->SetTargetSurface(cairoSurface, w, h);
            renderContext->ResizeStagingSurface(w, h);
        }

        debugOutput << "UltraCanvasWindowsWindow::DoResizeNative: nativeh=" << GetNativeHandle() << " Cairo surface=" << cairoSurface << " updated successfully" << std::endl;
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
        DestroyCairoSurface();

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
                int w_new = LOWORD(lParam);
                int h_new = HIWORD(lParam);
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
                    RECT r = {0, 0, config_.minWidth, config_.minHeight};
                    AdjustWindowRectEx(&r, style, FALSE, exStyle);
                    mmi->ptMinTrackSize.x = r.right - r.left;
                    mmi->ptMinTrackSize.y = r.bottom - r.top;
                }
                if (config_.maxWidth > 0 && config_.maxHeight > 0) {
                    RECT r = {0, 0, config_.maxWidth, config_.maxHeight};
                    AdjustWindowRectEx(&r, style, FALSE, exStyle);
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
        if (!_created || visible) return;
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);

        visible = true;

        if (onWindowShow) onWindowShow();
    }

    void UltraCanvasWindowsWindow::Hide() {
        if (!_created || !visible) return;
        ShowWindow(hwnd, SW_HIDE);

        visible = false;

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
            // Calculate window size from desired client area
            DWORD style = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_STYLE));
            DWORD exStyle = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_EXSTYLE));
            RECT rect = {0, 0, width, height};
            AdjustWindowRectEx(&rect, style, FALSE, exStyle);

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

    void UltraCanvasWindowsWindow::Flush() {
        if (!renderContext || !cairoSurface || !hwnd) return;
        std::lock_guard<std::mutex> lock(cairoMutex);
        renderContext->SwapBuffers();
        cairo_surface_flush(cairoSurface);
        // Trigger a synchronous WM_PAINT to blit the image surface to the window
        InvalidateRect(hwnd, NULL, FALSE);
        UpdateWindow(hwnd);
    }

    void UltraCanvasWindowsWindow::BlitSurfaceToHDC(HDC targetDC) {
        if (!cairoSurface || !targetDC) return;
        if (cairo_surface_get_type(cairoSurface) != CAIRO_SURFACE_TYPE_IMAGE) return;

        cairo_surface_flush(cairoSurface);

        int width = cairo_image_surface_get_width(cairoSurface);
        int height = cairo_image_surface_get_height(cairoSurface);
        unsigned char* data = cairo_image_surface_get_data(cairoSurface);
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

    void UltraCanvasWindowsWindow::GetScreenPosition(int& x, int& y) const {
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
} // namespace UltraCanvas
