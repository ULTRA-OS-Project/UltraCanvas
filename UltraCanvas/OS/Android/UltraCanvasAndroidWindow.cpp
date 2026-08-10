// OS/Android/UltraCanvasAndroidWindow.cpp
// Android window implementation for UltraCanvas Framework.
// Version: 1.0.0
// Last Modified: 2026-08-10
// Author: UltraCanvas Framework

// The public headers first: they define the base classes and then pull in the
// OS headers through their platform alias chain (same pattern as Linux).
#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasAndroidWindow.h"
#include "UltraCanvasAndroidApplication.h"
#include "UltraCanvasDebug.h"

#include <android/configuration.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace UltraCanvas {

    UltraCanvasAndroidWindow::UltraCanvasAndroidWindow() = default;

    // ===== CREATION / DESTRUCTION =====

    bool UltraCanvasAndroidWindow::CreateNative() {
        auto* application = UltraCanvasAndroidApplication::GetInstance();
        if (!application) {
            debugOutput << "UltraCanvas Android: no application instance" << std::endl;
            return false;
        }

        nativeWindow = application->GetNativeWindow();
        if (!nativeWindow) {
            debugOutput << "UltraCanvas Android: no ANativeWindow yet - windows must "
                           "be created after the android_main glue has delivered "
                           "APP_CMD_INIT_WINDOW" << std::endl;
            return false;
        }

        // The window always covers the whole surface: replace the requested
        // config size with the real one (logical units) before building the
        // surface, and re-anchor the container bounds that Create() already
        // set from the requested size.
        RefreshDeviceScale();
        const int physW = ANativeWindow_getWidth(nativeWindow);
        const int physH = ANativeWindow_getHeight(nativeWindow);
        if (physW > 0 && physH > 0) {
            config_.x = 0;
            config_.y = 0;
            config_.width = PhysicalToLogical(physW);
            config_.height = PhysicalToLogical(physH);
            SetBounds(Rect2Di(0, 0, config_.width, config_.height));
        }

        if (!CreateNativeCairoSurface()) {
            nativeWindow = nullptr;
            return false;
        }
        return true;
    }

    void UltraCanvasAndroidWindow::DestroyNative() {
        DestroyNativeCairoSurface();
        nativeWindow = nullptr;   // borrowed from the activity, never released here
    }

    // ===== SURFACE MANAGEMENT =====

    bool UltraCanvasAndroidWindow::CreateNativeCairoSurface() {
        if (!nativeWindow) return false;

        const int pixW = LogicalToPhysical(config_.width);
        const int pixH = LogicalToPhysical(config_.height);

        // Fix the producer buffer geometry so PresentSurface's row copy and
        // the lock buffer agree on size and format.
        ANativeWindow_setBuffersGeometry(nativeWindow, pixW, pixH,
                                         WINDOW_FORMAT_RGBX_8888);

        // Image surface at PHYSICAL px, presented by PresentSurface() - the
        // same model as the Windows backend (which blits via SetDIBitsToDevice).
        nativeSurface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, pixW, pixH);
        if (!nativeSurface ||
            cairo_surface_status(static_cast<cairo_surface_t*>(nativeSurface)) != CAIRO_STATUS_SUCCESS) {
            debugOutput << "UltraCanvas Android: cairo_image_surface_create failed ("
                        << pixW << "x" << pixH << ")" << std::endl;
            nativeSurface = nullptr;
            return false;
        }

        // 1 user unit = deviceScale device px; UI keeps drawing in logical
        // coordinates while Cairo rasterizes onto the larger pixel grid.
        cairo_surface_set_device_scale(static_cast<cairo_surface_t*>(nativeSurface),
                                       deviceScale, deviceScale);
        // Pin the fallback DPI like the Windows backend so vector fallbacks
        // don't rasterize at a different density than text.
        cairo_surface_set_fallback_resolution(static_cast<cairo_surface_t*>(nativeSurface),
                                              96.0 * deviceScale, 96.0 * deviceScale);
        return true;
    }

    void UltraCanvasAndroidWindow::DestroyNativeCairoSurface() {
        if (nativeSurface) {
            cairo_surface_destroy(static_cast<cairo_surface_t*>(nativeSurface));
            nativeSurface = nullptr;
        }
    }

    bool UltraCanvasAndroidWindow::RecreateNativeSurface() {
        DestroyNativeCairoSurface();
        if (!CreateNativeCairoSurface()) {
            debugOutput << "UltraCanvas Android: failed to recreate Cairo surface"
                        << std::endl;
            return false;
        }
        return true;
    }

    void UltraCanvasAndroidWindow::DoResizeNative() {
        RecreateNativeSurface();
        InvalidateWindowNative();
    }

    float UltraCanvasAndroidWindow::QueryNativeDeviceScale() const {
        android_app* app = UltraCanvasAndroidApplication::GetAndroidApp();
        if (app && app->config) {
            const int32_t density = AConfiguration_getDensity(app->config);
            // DENSITY_ANY / DENSITY_NONE are sentinel values, not dpi.
            if (density >= 60 && density <= 1000) {
                return static_cast<float>(density) / 160.0f;   // mdpi baseline
            }
        }
        return 1.0f;
    }

    // ===== PRESENTATION =====

    void UltraCanvasAndroidWindow::InvalidateWindowNative() {
        PresentSurface();
    }

    void UltraCanvasAndroidWindow::PresentSurface() {
        if (!nativeSurface || !nativeWindow) return;

        auto* surface = static_cast<cairo_surface_t*>(nativeSurface);
        cairo_surface_flush(surface);

        const unsigned char* srcData = cairo_image_surface_get_data(surface);
        if (!srcData) return;
        const int srcW = cairo_image_surface_get_width(surface);
        const int srcH = cairo_image_surface_get_height(surface);
        const int srcStride = cairo_image_surface_get_stride(surface);

        ANativeWindow_Buffer buffer;
        if (ANativeWindow_lock(nativeWindow, &buffer, nullptr) != 0) {
            debugOutput << "UltraCanvas Android: ANativeWindow_lock failed" << std::endl;
            return;
        }

        const int copyW = std::min(srcW, buffer.width);
        const int copyH = std::min(srcH, buffer.height);
        auto* dstBase = static_cast<uint32_t*>(buffer.bits);

        for (int y = 0; y < copyH; ++y) {
            const auto* srcRow = reinterpret_cast<const uint32_t*>(srcData + y * srcStride);
            uint32_t* dstRow = dstBase + y * buffer.stride;   // stride is in pixels
            for (int x = 0; x < copyW; ++x) {
                // Cairo xRGB words (B,G,R,X bytes on little-endian) -> the
                // buffer's RGBX byte order: swap the R and B channels.
                const uint32_t px = srcRow[x];
                dstRow[x] = (px & 0xFF00FF00u)
                          | ((px & 0x00FF0000u) >> 16)
                          | ((px & 0x000000FFu) << 16);
            }
        }

        ANativeWindow_unlockAndPost(nativeWindow);
    }

    // ===== SURFACE LIFECYCLE (activity commands) =====

    void UltraCanvasAndroidWindow::HandleNativeSurfaceCreated(ANativeWindow* window) {
        nativeWindow = window;
        if (!_created) return;   // CreateNative will pick it up

        RefreshDeviceScale();
        const int physW = ANativeWindow_getWidth(nativeWindow);
        const int physH = ANativeWindow_getHeight(nativeWindow);
        const int logW = PhysicalToLogical(physW);
        const int logH = PhysicalToLogical(physH);

        if (logW > 0 && logH > 0 && (logW != config_.width || logH != config_.height)) {
            // Rotation while the surface was gone: run the normal resize path
            // (rebuilds surface + render contexts + layout).
            HandleResizeEvent(logW, logH);
        } else {
            RecreateNativeSurface();
            AddDirtyRectangle(Rect2Di(0, 0, config_.width, config_.height));
            RequestWindowComposition();
        }
    }

    void UltraCanvasAndroidWindow::HandleNativeSurfaceDestroyed() {
        // The buffer queue dies with the surface; every reference must be gone
        // before the command handler returns. The offscreen render context
        // survives, so the next INIT_WINDOW only recomposites.
        DestroyNativeCairoSurface();
        nativeWindow = nullptr;
    }

    void UltraCanvasAndroidWindow::HandleNativeSurfaceResized() {
        if (!nativeWindow) return;
        RefreshDeviceScale();
        const int logW = PhysicalToLogical(ANativeWindow_getWidth(nativeWindow));
        const int logH = PhysicalToLogical(ANativeWindow_getHeight(nativeWindow));
        if (logW > 0 && logH > 0 && (logW != config_.width || logH != config_.height)) {
            HandleResizeEvent(logW, logH);
        }
    }

    // ===== VISIBILITY / WINDOW MANAGEMENT =====
    // The activity manages actual visibility; these keep the framework's
    // bookkeeping (and focus notifications) consistent.

    void UltraCanvasAndroidWindow::Show() {
        if (!_created || _windowVisible) return;
        _windowVisible = true;
        HandleWindowShown();
        if (onWindowShow) onWindowShow();
        AddDirtyRectangle(Rect2Di(0, 0, config_.width, config_.height));
        RequestWindowComposition();
    }

    void UltraCanvasAndroidWindow::Hide() {
        if (!_created || !_windowVisible) return;
        _windowVisible = false;
        HandleWindowHidden();
        if (onWindowHide) onWindowHide();
    }

    void UltraCanvasAndroidWindow::RaiseAndFocus() {}
    void UltraCanvasAndroidWindow::SetWindowTitle(const std::string& title) {
        config_.title = title;   // no title bar; kept for GetWindowTitle()
    }
    void UltraCanvasAndroidWindow::SetWindowIcon(const std::string&) {}
    void UltraCanvasAndroidWindow::SetWindowPosition(int, int) {}
    void UltraCanvasAndroidWindow::SetWindowSize(int, int) {}
    void UltraCanvasAndroidWindow::Minimize() {}
    void UltraCanvasAndroidWindow::Maximize() {}
    void UltraCanvasAndroidWindow::Restore() {}
    void UltraCanvasAndroidWindow::SetFullscreen(bool) {}
    void UltraCanvasAndroidWindow::SetResizable(bool) {}

    void UltraCanvasAndroidWindow::GetScreenSize(int& width, int& height) const {
        // Physical px, matching the desktop backends' convention.
        if (nativeWindow) {
            width = ANativeWindow_getWidth(nativeWindow);
            height = ANativeWindow_getHeight(nativeWindow);
        } else {
            width = LogicalToPhysical(config_.width);
            height = LogicalToPhysical(config_.height);
        }
    }

    NativeWindowHandle UltraCanvasAndroidWindow::GetNativeHandle() const {
        return static_cast<NativeWindowHandle>(nativeWindow);
    }

} // namespace UltraCanvas
