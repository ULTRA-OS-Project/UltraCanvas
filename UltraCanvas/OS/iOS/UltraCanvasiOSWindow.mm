// OS/iOS/UltraCanvasiOSWindow.mm
// iOS window implementation for UltraCanvas Framework.
// Version: 1.0.1 - the bitmap info's two enums combined without a mixed-enum warning
// Version: 1.0.0
// Last Modified: 2026-11-24
// Author: UltraCanvas Framework

// The public headers first: they define the base classes and then pull in the
// OS headers through their platform alias chain (as the other backends do).
#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasiOSWindow.h"
#include "UltraCanvasiOSApplication.h"
#include "UltraCanvasDebug.h"

#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import <CoreGraphics/CoreGraphics.h>

#include <algorithm>
#include <cmath>

// The image as the layer's contents. A function of its own, outside the
// window's methods: in there `id` is the element's identifier member, not
// Objective-C's object type.
static void SetLayerContents(CALayer* layer, CGImageRef image) {
    layer.contents = (__bridge id)image;
}

namespace UltraCanvas {

    namespace {
        // The root view, retained here (__strong under ARC) for as long as
        // the backend's main keeps it in the key window.
        UIView* g_rootView = nil;

        UIView* RootView(void* opaque) {
            return (__bridge UIView*)opaque;
        }
    }

    void* GetiOSRootView() { return (__bridge void*)g_rootView; }
    void SetiOSRootView(void* view) { g_rootView = (__bridge UIView*)view; }

    UltraCanvasiOSWindow::UltraCanvasiOSWindow() = default;

    UltraCanvasiOSWindow::~UltraCanvasiOSWindow() {
        if (_created) DestroyNative();
    }

    // ===== CREATION / DESTRUCTION =====

    bool UltraCanvasiOSWindow::CreateNative() {
        rootView = GetiOSRootView();
        UIView* view = RootView(rootView);
        if (!view) {
            debugOutput << "UltraCanvas iOS: no root view - windows must be created "
                           "from the application's main body, which the backend runs "
                           "after launch (OS/iOS/UltraCanvasiOSMain.mm)" << std::endl;
            return false;
        }

        // The window always covers the whole view: replace the requested size
        // with the view's (points are the framework's logical units) before
        // building the surface, and re-anchor the bounds Create() set.
        RefreshDeviceScale();
        const CGSize size = view.bounds.size;
        const int w = static_cast<int>(std::lround(size.width));
        const int h = static_cast<int>(std::lround(size.height));
        if (w > 0 && h > 0) {
            config_.x = 0;
            config_.y = 0;
            config_.width = w;
            config_.height = h;
            SetBounds(Rect2Di(0, 0, w, h));
        }
        view.layer.contentsScale = deviceScale;
        view.layer.contentsGravity = kCAGravityTopLeft;

        if (!CreateNativeCairoSurface()) {
            rootView = nullptr;
            return false;
        }
        return true;
    }

    void UltraCanvasiOSWindow::DestroyNative() {
        DestroyNativeCairoSurface();
        if (UIView* view = RootView(rootView)) {
            view.layer.contents = nil;
        }
        rootView = nullptr;   // borrowed from the view controller, never released here
    }

    // ===== SURFACE MANAGEMENT =====

    bool UltraCanvasiOSWindow::CreateNativeCairoSurface() {
        const int pixW = std::max(1, LogicalToPhysical(config_.width));
        const int pixH = std::max(1, LogicalToPhysical(config_.height));

        // Image surface at PHYSICAL px, presented by PresentSurface().
        nativeSurface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, pixW, pixH);
        if (!nativeSurface ||
            cairo_surface_status(static_cast<cairo_surface_t*>(nativeSurface)) != CAIRO_STATUS_SUCCESS) {
            debugOutput << "UltraCanvas iOS: cairo_image_surface_create failed ("
                        << pixW << "x" << pixH << ")" << std::endl;
            nativeSurface = nullptr;
            return false;
        }
        // 1 user unit = deviceScale device px: the UI draws in points while
        // Cairo rasterises onto the screen's pixel grid.
        cairo_surface_set_device_scale(static_cast<cairo_surface_t*>(nativeSurface),
                                       deviceScale, deviceScale);
        cairo_surface_set_fallback_resolution(static_cast<cairo_surface_t*>(nativeSurface),
                                              96.0 * deviceScale, 96.0 * deviceScale);
        return true;
    }

    void UltraCanvasiOSWindow::DestroyNativeCairoSurface() {
        if (nativeSurface) {
            cairo_surface_destroy(static_cast<cairo_surface_t*>(nativeSurface));
            nativeSurface = nullptr;
        }
    }

    bool UltraCanvasiOSWindow::RecreateNativeSurface() {
        DestroyNativeCairoSurface();
        if (!CreateNativeCairoSurface()) {
            debugOutput << "UltraCanvas iOS: failed to recreate Cairo surface" << std::endl;
            return false;
        }
        return true;
    }

    void UltraCanvasiOSWindow::DoResizeNative() {
        // Rebuild only: the core lays out and repaints after this, and the
        // composition the dirty window gets presents the frame (presenting
        // here would show the new, empty surface - the Android lesson).
        RecreateNativeSurface();
        RequestWindowComposition();
    }

    float UltraCanvasiOSWindow::QueryNativeDeviceScale() const {
        UIView* view = RootView(rootView ? rootView : GetiOSRootView());
        CGFloat scale = 0;
        if (view) scale = view.traitCollection.displayScale;
        if (scale <= 0) scale = [UIScreen mainScreen].scale;
        return scale > 0 ? static_cast<float>(scale) : 1.0f;
    }

    // ===== PRESENTATION =====

    void UltraCanvasiOSWindow::InvalidateWindowNative() {
        PresentSurface();
    }

    void UltraCanvasiOSWindow::PresentSurface() {
        UIView* view = RootView(rootView);
        if (!nativeSurface || !view) return;

        auto* surface = static_cast<cairo_surface_t*>(nativeSurface);
        cairo_surface_flush(surface);
        unsigned char* data = cairo_image_surface_get_data(surface);
        if (!data) return;
        const int w = cairo_image_surface_get_width(surface);
        const int h = cairo_image_surface_get_height(surface);
        const int stride = cairo_image_surface_get_stride(surface);

        @autoreleasepool {
            // Cairo's RGB24 is a native-endian 32-bit xRGB word: B,G,R,X bytes
            // on arm64, which CoreGraphics reads as 32-bit little-endian with
            // the first (high) component skipped. CGBitmapContextCreateImage
            // copies (copy-on-write), so the surface can be drawn into again
            // while CoreAnimation still shows this frame.
            CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
            CGContextRef ctx = CGBitmapContextCreate(
                    data, static_cast<size_t>(w), static_cast<size_t>(h), 8,
                    static_cast<size_t>(stride), space,
                    static_cast<CGBitmapInfo>(kCGImageAlphaNoneSkipFirst) | kCGBitmapByteOrder32Little);
            CGColorSpaceRelease(space);
            if (!ctx) {
                debugOutput << "UltraCanvas iOS: CGBitmapContextCreate failed ("
                            << w << "x" << h << ")" << std::endl;
                return;
            }
            CGImageRef image = CGBitmapContextCreateImage(ctx);
            CGContextRelease(ctx);
            if (!image) return;

            [CATransaction begin];
            [CATransaction setDisableActions:YES];   // no cross-fade between frames
            view.layer.contentsScale = deviceScale;
            SetLayerContents(view.layer, image);
            [CATransaction commit];
            CGImageRelease(image);
        }
    }

    // ===== FROM THE VIEW =====

    void UltraCanvasiOSWindow::HandleViewResized(int widthPoints, int heightPoints) {
        if (!_created || widthPoints <= 0 || heightPoints <= 0) return;
        const bool scaleChanged = RefreshDeviceScale();
        if (widthPoints != config_.width || heightPoints != config_.height) {
            HandleResizeEvent(widthPoints, heightPoints);   // rebuilds the surface
        } else if (scaleChanged) {
            RecreateNativeSurface();
            AddDirtyRectangle(Rect2Di(0, 0, config_.width, config_.height));
            RequestWindowComposition();
        }
    }

    void UltraCanvasiOSWindow::HandleEnteredBackground() {
        // iOS ends an app that keeps the GPU busy in the background; nothing
        // is drawn while it cannot be seen. Only a window hidden HERE is shown
        // again on the way back.
        if (_windowVisible) {
            autoHiddenInBackground = true;
            Hide();
        }
    }

    void UltraCanvasiOSWindow::HandleEnteredForeground() {
        if (autoHiddenInBackground) {
            autoHiddenInBackground = false;
            Show();
        }
    }

    // ===== VISIBILITY / WINDOW MANAGEMENT =====
    // UIKit manages actual visibility; these keep the framework's bookkeeping
    // (and focus notifications) consistent.

    void UltraCanvasiOSWindow::Show() {
        if (!_created || _windowVisible) return;
        _windowVisible = true;
        HandleWindowShown();
        if (onWindowShow) onWindowShow();
        AddDirtyRectangle(Rect2Di(0, 0, config_.width, config_.height));
        RequestWindowComposition();
    }

    void UltraCanvasiOSWindow::Hide() {
        if (!_created || !_windowVisible) return;
        _windowVisible = false;
        HandleWindowHidden();
        if (onWindowHide) onWindowHide();
    }

    void UltraCanvasiOSWindow::RaiseAndFocus() {}
    void UltraCanvasiOSWindow::SetWindowTitle(const std::string& title) {
        config_.title = title;   // no title bar; kept for GetWindowTitle()
    }
    void UltraCanvasiOSWindow::SetWindowIcon(const std::string&) {}
    void UltraCanvasiOSWindow::SetWindowPosition(int, int) {}
    void UltraCanvasiOSWindow::SetWindowSize(int, int) {}
    void UltraCanvasiOSWindow::Minimize() {}
    void UltraCanvasiOSWindow::Maximize() {}
    void UltraCanvasiOSWindow::Restore() {}
    void UltraCanvasiOSWindow::SetFullscreen(bool) {}
    void UltraCanvasiOSWindow::SetResizable(bool) {}

    void UltraCanvasiOSWindow::GetScreenSize(int& width, int& height) const {
        // Physical px, the desktop backends' convention.
        const CGRect native = [UIScreen mainScreen].nativeBounds;
        width = static_cast<int>(native.size.width);
        height = static_cast<int>(native.size.height);
    }

    NativeWindowHandle UltraCanvasiOSWindow::GetNativeHandle() const {
        return static_cast<NativeWindowHandle>(rootView);
    }

} // namespace UltraCanvas
