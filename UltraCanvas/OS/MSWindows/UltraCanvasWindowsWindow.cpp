// OS/MSWindows/UltraCanvasWindowsWindow.cpp
// Complete Windows window implementation with Cairo rendering
// Version: 1.1.0 - Per-Monitor V2 HiDPI: physical-pixel surfaces + Cairo device
//                  scale, WM_DPICHANGED handling, logical/physical coord mapping
// Last Modified: 2026-07-01
// Author: UltraCanvas Framework

#include "../../include/UltraCanvasWindow.h"
#include "../../include/UltraCanvasImage.h"
#include "UltraCanvasWindowsApplication.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include "UltraCanvasDebug.h"

namespace UltraCanvas {

namespace {
    // ===== DPI HELPERS =====
    // The per-monitor DPI APIs only exist on Windows 8.1 / 10+. Resolve them
    // dynamically so the framework still links and runs on older systems,
    // falling back to the system-wide DPI (GetDeviceCaps) when unavailable.

    UINT QueryDpiForWindow(HWND hwnd) {
        using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
        static auto fn = reinterpret_cast<GetDpiForWindowFn>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
        if (fn && hwnd) {
            UINT dpi = fn(hwnd);
            if (dpi > 0) return dpi;
        }
        // Fallback: system DPI (correct on single-DPI setups)
        HDC dc = GetDC(nullptr);
        UINT dpi = dc ? static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX)) : 96;
        if (dc) ReleaseDC(nullptr, dc);
        return dpi > 0 ? dpi : 96;
    }

    UINT QueryDpiForPoint(POINT pt) {
        // GetDpiForMonitor lives in shcore.dll (Windows 8.1+). MDT_EFFECTIVE_DPI = 0.
        using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
        static auto fn = []() -> GetDpiForMonitorFn {
            HMODULE shcore = LoadLibraryW(L"shcore.dll");
            return shcore ? reinterpret_cast<GetDpiForMonitorFn>(
                GetProcAddress(shcore, "GetDpiForMonitor")) : nullptr;
        }();
        HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        if (fn && mon) {
            UINT dx = 96, dy = 96;
            if (fn(mon, 0 /*MDT_EFFECTIVE_DPI*/, &dx, &dy) == S_OK && dx > 0) {
                return dx;
            }
        }
        HDC dc = GetDC(nullptr);
        UINT dpi = dc ? static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX)) : 96;
        if (dc) ReleaseDC(nullptr, dc);
        return dpi > 0 ? dpi : 96;
    }

    // AdjustWindowRectEx that accounts for per-DPI non-client metrics when the
    // API is present (Windows 10 1607+); otherwise the classic system-DPI form.
    void AdjustWindowRectForDpi(LPRECT rect, DWORD style, DWORD exStyle, UINT dpi) {
        using AdjustFn = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
        static auto fn = reinterpret_cast<AdjustFn>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));
        if (fn) {
            fn(rect, style, FALSE, exStyle, dpi);
        } else {
            AdjustWindowRectEx(rect, style, FALSE, exStyle);
        }
    }
} // anonymous namespace

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

        // STEP 1b: Now that the HWND exists we know the exact monitor DPI. Size
        // the client area to config_.width/height *logical* units at that scale
        // (dpiScale was seeded from the monitor in CreateHWND and refined here).
        ApplyClientSizePhysical();

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
            event.targetWindow = this;
            event.nativeWindowHandle = hwnd;
            // x,y are physical client pixels; convert to logical UI units.
            double s = dpiScale > 0.0 ? dpiScale : 1.0;
            event.pointerWindow = { static_cast<int>(std::lround(x / s)),
                                    static_cast<int>(std::lround(y / s)) };
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
            event.targetWindow = this;
            event.nativeWindowHandle = hwnd;
            // x,y are physical client pixels; convert to logical UI units.
            double s = dpiScale > 0.0 ? dpiScale : 1.0;
            event.pointerWindow = { static_cast<int>(std::lround(x / s)),
                                    static_cast<int>(std::lround(y / s)) };
            event.pointer = event.pointerWindow;
            UltraCanvasWindowsApplication::GetInstance()->PushEvent(event);
        };

        dropTarget->onDragLeave = [this](int x, int y) {
            UCEvent event;
            event.type = UCEventType::DragLeave;
            event.targetWindow = this;
            event.nativeWindowHandle = hwnd;
            // x,y are physical client pixels; convert to logical UI units.
            double s = dpiScale > 0.0 ? dpiScale : 1.0;
            event.pointerWindow = { static_cast<int>(std::lround(x / s)),
                                    static_cast<int>(std::lround(y / s)) };
            event.pointer = event.pointerWindow;
            UltraCanvasWindowsApplication::GetInstance()->PushEvent(event);
        };

        dropTarget->onDragOver = [this](int x, int y) {
            UCEvent event;
            event.type = UCEventType::DragOver;
            event.targetWindow = this;
            event.nativeWindowHandle = hwnd;
            // x,y are physical client pixels; convert to logical UI units.
            double s = dpiScale > 0.0 ? dpiScale : 1.0;
            event.pointerWindow = { static_cast<int>(std::lround(x / s)),
                                    static_cast<int>(std::lround(y / s)) };
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

        // Seed dpiScale from the monitor the window will open on, so the initial
        // outer size is close to correct (avoids a visible resize on show). The
        // real per-window DPI is re-queried after creation below.
        POINT target = { config_.x >= 0 ? config_.x : 0,
                         config_.y >= 0 ? config_.y : 0 };
        UINT dpi = QueryDpiForPoint(target);
        dpiScale = dpi / 96.0;

        // Calculate window rect to get correct client area size. Client area is
        // sized in physical pixels (logical * dpiScale) since the process is
        // DPI-aware and Windows will not scale us.
        int physW = std::max(1, static_cast<int>(std::lround(config_.width * dpiScale)));
        int physH = std::max(1, static_cast<int>(std::lround(config_.height * dpiScale)));
        RECT rect = {0, 0, physW, physH};
        AdjustWindowRectForDpi(&rect, style, exStyle, dpi);

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

        // Refine dpiScale from the actual window (it may have landed on a
        // different monitor than the seed point above).
        dpiScale = QueryDpiForWindow(hwnd) / 96.0;
        if (dpiScale <= 0.0) dpiScale = 1.0;

        trackingMouseLeave = false;
        return true;
    }

    // Resize the HWND so its client area is config_.width/height logical units
    // expressed in physical pixels at the current dpiScale.
    void UltraCanvasWindowsWindow::ApplyClientSizePhysical() {
        if (!hwnd) return;
        DWORD style = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_STYLE));
        DWORD exStyle = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_EXSTYLE));
        int physW = std::max(1, static_cast<int>(std::lround(config_.width * dpiScale)));
        int physH = std::max(1, static_cast<int>(std::lround(config_.height * dpiScale)));
        RECT rect = {0, 0, physW, physH};
        AdjustWindowRectForDpi(&rect, style, exStyle,
                               static_cast<UINT>(std::lround(dpiScale * 96.0)));

        // Suppress the WM_SIZE that this SetWindowPos synthesizes: the native
        // surface is (re)built explicitly by the caller, and config_ already
        // holds the intended logical size.
        inDpiChange = true;
        SetWindowPos(hwnd, nullptr, 0, 0,
                     rect.right - rect.left, rect.bottom - rect.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        inDpiChange = false;
    }

    bool UltraCanvasWindowsWindow::CreateNativeCairoSurface() {
        if (!hwnd) return false;

        // Use a Cairo image surface instead of cairo_win32_surface.
        // This avoids DWM composition issues where rendering to a persistent
        // window DC outside of BeginPaint/EndPaint is not reliably displayed.
        // The image surface data is blitted to the window DC in WM_PAINT.
        //
        // The backing store is allocated in PHYSICAL pixels (logical * dpiScale)
        // and tagged with a Cairo device scale, so all logical-coordinate drawing
        // rasterizes at full monitor resolution (crisp text/vectors on HiDPI).
        int physW = std::max(1, static_cast<int>(std::lround(config_.width * dpiScale)));
        int physH = std::max(1, static_cast<int>(std::lround(config_.height * dpiScale)));
        nativeSurface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, physW, physH);
        if (!nativeSurface) {
            debugOutput << "UltraCanvas Windows: cairo_image_surface_create failed" << std::endl;
            return false;
        }

        // Device scale maps logical user units -> physical pixels. Render contexts
        // created "similar to" this surface (main + popups) inherit the scale.
        cairo_surface_set_device_scale(static_cast<cairo_surface_t *>(nativeSurface),
                                       dpiScale, dpiScale);

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
                  << config_.width << "x" << config_.height << ")" << std::endl;
        return true;
    }

    void UltraCanvasWindowsWindow::DestroyNativeCairoSurface() {
        if (nativeSurface) {
            cairo_surface_destroy(static_cast<cairo_surface_t *>(nativeSurface));
            nativeSurface = nullptr;
        }
    }

    void UltraCanvasWindowsWindow::HandleResizeEventWindows(int w, int h) {
        // Ignore WM_SIZE synthesized by our own geometry adjustments during
        // creation / DPI handling, and any that arrive before the render context
        // exists (the surface is built explicitly in those paths).
        if (inDpiChange || !_created) {
            return;
        }
        HandleResizeEvent(w, h);
        DoResize();
        RequestWindowComposition();
        UpdateAndRender();

        debugOutput << "UltraCanvasWindowsWindow::HandleResizeEventWindows: nativeh=" << GetNativeHandle() << " updated successfully" << std::endl;
    }

    // Rebuild everything for a new display scale after WM_DPICHANGED. The window
    // has already been moved/resized to Windows' suggested rect, so we read the
    // actual client size and keep the logical size stable across the change.
    void UltraCanvasWindowsWindow::ApplyDpiScaleChange() {
        if (!hwnd) return;
        RECT cr;
        if (!GetClientRect(hwnd, &cr)) return;
        int physW = std::max(1, static_cast<int>(cr.right - cr.left));
        int physH = std::max(1, static_cast<int>(cr.bottom - cr.top));
        int logW = std::max(1, static_cast<int>(std::lround(physW / dpiScale)));
        int logH = std::max(1, static_cast<int>(std::lround(physH / dpiScale)));

        config_.width = logW;
        config_.height = logH;

        // Rebuild the native surface at the new physical size + device scale.
        {
            std::lock_guard<std::mutex> lock(cairoMutex);
            if (nativeSurface) {
                cairo_surface_destroy(static_cast<cairo_surface_t *>(nativeSurface));
                nativeSurface = nullptr;
            }
            nativeSurface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, physW, physH);
            if (!nativeSurface) {
                debugOutput << "UltraCanvas Windows: DPI-change surface alloc failed" << std::endl;
                return;
            }
            cairo_surface_set_device_scale(static_cast<cairo_surface_t *>(nativeSurface),
                                           dpiScale, dpiScale);
            cairo_surface_set_fallback_resolution(static_cast<cairo_surface_t *>(nativeSurface),
                                                  96.0, 96.0);
        }

        // Recreate the staging render context "similar to" the new native surface
        // so it inherits the new device scale (ResizeSurface would inherit the
        // stale scale from the previous staging surface).
        renderContext = CreateRenderContext(Size2Di(logW, logH), nativeSurface);
        SetElementSize({config_.width, config_.height});
        _needsResize = false;

        if (onWindowResize) onWindowResize(logW, logH);

        AddDirtyRectangle(Rect2Di(0, 0, logW, logH));
        RequestWindowComposition();
        UpdateAndRender();

        debugOutput << "UltraCanvasWindowsWindow::ApplyDpiScaleChange nativeh="
                    << GetNativeHandle() << " scale=" << dpiScale
                    << " logical=(" << logW << "x" << logH << ")"
                    << " physical=(" << physW << "x" << physH << ")" << std::endl;
    }

    void UltraCanvasWindowsWindow::DoResizeNative() {
        std::lock_guard<std::mutex> lock(cairoMutex);
        // config_ is in logical units; the surface backing store is physical.
        int w = std::max(1, static_cast<int>(std::lround(config_.width * dpiScale)));
        int h = std::max(1, static_cast<int>(std::lround(config_.height * dpiScale)));

        if (nativeSurface) {
            cairo_surface_destroy(static_cast<cairo_surface_t *>(nativeSurface));
            nativeSurface = nullptr;
        }

        nativeSurface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
        if (!nativeSurface) {
            debugOutput << "UltraCanvas Windows: Failed to recreate Cairo surface on resize" << std::endl;
            return;
        }
        cairo_surface_set_device_scale(static_cast<cairo_surface_t *>(nativeSurface),
                                       dpiScale, dpiScale);
        cairo_surface_set_fallback_resolution(static_cast<cairo_surface_t *>(nativeSurface), 96.0, 96.0);

//        if (renderContext) {
//            renderContext->SetTargetSurface(cairoSurface, w, h);
//            renderContext->ResizeStagingSurface(w, h);
//        }

        debugOutput << "UltraCanvasWindowsWindow::DoResizeNative: nativeh=" << GetNativeHandle() << " Native surface=" << nativeSurface << " updated successfully" << std::endl;
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
                // WM_SIZE reports the client area in PHYSICAL pixels (the process
                // is DPI-aware). Convert to logical units for the framework.
                int physW = LOWORD(lParam);
                int physH = HIWORD(lParam);
                // Track window state from SIZE message
                if (wParam == SIZE_MINIMIZED)
                    _state = WindowState::Minimized;
                else if (wParam == SIZE_MAXIMIZED)
                    _state = WindowState::Maximized;
                else if (wParam == SIZE_RESTORED)
                    _state = WindowState::Normal;

                if (physW > 0 && physH > 0) {
                    double s = dpiScale > 0.0 ? dpiScale : 1.0;
                    int logW = std::max(1, static_cast<int>(std::lround(physW / s)));
                    int logH = std::max(1, static_cast<int>(std::lround(physH / s)));
                    HandleResizeEventWindows(logW, logH);
                }
                return 0;
            }

            case WM_DPICHANGED: {
                // The window moved to a monitor with a different scale (or the
                // user changed the scale). HIWORD(wParam) is the new DPI; lParam
                // is Windows' suggested window rect (physical screen coords) that
                // preserves the logical size.
                UINT newDpi = HIWORD(wParam);
                dpiScale = (newDpi > 0 ? newDpi : 96) / 96.0;

                const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
                if (suggested) {
                    inDpiChange = true;
                    SetWindowPos(h, nullptr,
                                 suggested->left, suggested->top,
                                 suggested->right - suggested->left,
                                 suggested->bottom - suggested->top,
                                 SWP_NOZORDER | SWP_NOACTIVATE);
                    inDpiChange = false;
                }

                ApplyDpiScaleChange();
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
                // config_ min/max are logical; track sizes are physical.
                UINT dpi = static_cast<UINT>(std::lround(dpiScale * 96.0));

                if (config_.minWidth > 0 && config_.minHeight > 0) {
                    RECT r = {0, 0,
                              static_cast<int>(std::lround(config_.minWidth * dpiScale)),
                              static_cast<int>(std::lround(config_.minHeight * dpiScale))};
                    AdjustWindowRectForDpi(&r, style, exStyle, dpi);
                    mmi->ptMinTrackSize.x = r.right - r.left;
                    mmi->ptMinTrackSize.y = r.bottom - r.top;
                }
                if (config_.maxWidth > 0 && config_.maxHeight > 0) {
                    RECT r = {0, 0,
                              static_cast<int>(std::lround(config_.maxWidth * dpiScale)),
                              static_cast<int>(std::lround(config_.maxHeight * dpiScale))};
                    AdjustWindowRectForDpi(&r, style, exStyle, dpi);
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
        // width/height are logical units; store them and let the shared resize
        // path (WM_SIZE -> HandleResizeEventWindows) rebuild the surfaces once the
        // physical client area lands. ApplyClientSizePhysical converts to physical.
        config_.width = width;
        config_.height = height;

        if (hwnd) {
            ApplyClientSizePhysical();
            // ApplyClientSizePhysical suppresses WM_SIZE, so drive the resize
            // through the framework explicitly to rebuild surfaces + relayout.
            DoResize();
            RequestWindowComposition();
            UpdateAndRender();
            debugOutput << "UltraCanvasWindowsWindow::SetWindowSize nativeh=" << GetNativeHandle() << " (" << width << "x" << height << " logical)" << std::endl;
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
