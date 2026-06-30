// OS/MacOS/UltraCanvasMacOSWindow.mm
// Complete macOS window implementation with Cocoa and Cairo
// Version: 2.1.1
// Last Modified: 2026-05-07
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasMacOSWindow.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasUtils.h"

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

#include <iostream>
#include <cstring>
#include "UltraCanvasDebug.h"

@interface UltraCanvasView : NSView {
    UltraCanvas::UltraCanvasMacOSWindow* ultraCanvasWindow;
    cairo_surface_t* nativeSurface;
}

@property (nonatomic, assign) UltraCanvas::UltraCanvasMacOSWindow* ultraCanvasWindow;
@property (nonatomic, assign) cairo_surface_t* nativeSurface;

@end

@implementation UltraCanvasView

@synthesize ultraCanvasWindow;
@synthesize nativeSurface;

- (instancetype)initWithFrame:(NSRect)frameRect window:(UltraCanvas::UltraCanvasMacOSWindow*)window {
    self = [super initWithFrame:frameRect];
    if (self) {
        ultraCanvasWindow = window;
        nativeSurface = nullptr;
        [self setWantsLayer:YES];
        // Make the layer hold backing-resolution pixels so the offscreen Cairo
        // bitmap (which we render at backing dims) maps 1:1 to screen pixels.
        // The actual scale is applied per-window in CreateNativeCairoSurface
        // and OnWindowDidChangeBackingProperties; this just primes the layer
        // before the view is attached to a window.
        if (self.layer) {
            self.layer.contentsScale = self.window ? [self.window backingScaleFactor] : 1.0;
        }
    }
    return self;
}

- (void)drawRect:(NSRect)dirtyRect {
    if (!nativeSurface) return;

    // Get view's CGContext
    CGContextRef viewCtx = [[NSGraphicsContext currentContext] CGContext];
    if (!viewCtx) return;

    // Flush pending Cairo operations
    cairo_surface_flush(nativeSurface);

    // Bypass Cairo for the final blit: the view's CGContext already has a
    // device-scale CTM applied by Cocoa, and wrapping it as a Cairo surface
    // would force Cairo to rasterize at point grain (losing Retina detail).
    // Pulling the underlying CGBitmapContext from the offscreen Cairo Quartz
    // surface and CGContextDrawImage'ing it gives a 1:1 backing-pixel blit.
    CGContextRef offCtx = cairo_quartz_surface_get_cg_context(nativeSurface);
    if (!offCtx) return;

    CGImageRef img = CGBitmapContextCreateImage(offCtx);
    if (!img) return;

    NSRect bounds = [self bounds]; // logical points
    CGContextSaveGState(viewCtx);
    // No interpolation — source pixels and destination backing pixels match
    // exactly when the offscreen is created at backingScaleFactor.
    CGContextSetInterpolationQuality(viewCtx, kCGInterpolationNone);
    // The view is flipped (isFlipped == YES) so AppKit gave us a CGContext
    // whose Y-axis grows downward. CGContextDrawImage assumes Y-up, which
    // would render the bitmap upside down. Locally invert before drawing.
    CGContextTranslateCTM(viewCtx, NSMinX(bounds), NSMaxY(bounds));
    CGContextScaleCTM(viewCtx, 1.0, -1.0);
    CGContextDrawImage(viewCtx, CGRectMake(0, 0, NSWidth(bounds), NSHeight(bounds)), img);
    CGContextRestoreGState(viewCtx);
    CGImageRelease(img);
}

- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)isFlipped { return YES; }

// Override to prevent default NSView behavior (system beep) for unhandled keys.
// Key events are handled through the UltraCanvas event system at the application level.
- (void)keyDown:(NSEvent *)event {}

// Sent by AppKit when the view's backing scale changes (window dragged to a
// different display, or the system reports a scale change). Forward to the
// C++ window so the offscreen Cairo surface is recreated at the new size.
- (void)viewDidChangeBackingProperties {
    [super viewDidChangeBackingProperties];
    if (self.layer && self.window) {
        self.layer.contentsScale = [self.window backingScaleFactor];
    }
    if (ultraCanvasWindow) {
        ultraCanvasWindow->OnWindowDidChangeBackingProperties();
    }
}


@end


// ===== WINDOW DELEGATE =====
@interface UltraCanvasWindowDelegate : NSObject <NSWindowDelegate> {
    UltraCanvas::UltraCanvasMacOSWindow* ultraCanvasWindow;
}

@property (nonatomic, assign) UltraCanvas::UltraCanvasMacOSWindow* ultraCanvasWindow;

- (instancetype)initWithWindow:(UltraCanvas::UltraCanvasMacOSWindow*)window;
- (BOOL)windowShouldClose:(NSWindow*)sender;
- (void)windowWillClose:(NSNotification*)notification;
- (void)windowDidResize:(NSNotification*)notification;
- (void)windowDidMove:(NSNotification*)notification;
- (void)windowDidBecomeKey:(NSNotification*)notification;
- (void)windowDidResignKey:(NSNotification*)notification;
- (void)windowDidMiniaturize:(NSNotification*)notification;
- (void)windowDidDeminiaturize:(NSNotification*)notification;
- (void)windowDidChangeBackingProperties:(NSNotification*)notification;

@end

@implementation UltraCanvasWindowDelegate

@synthesize ultraCanvasWindow;

- (instancetype)initWithWindow:(UltraCanvas::UltraCanvasMacOSWindow*)window {
    self = [super init];
    if (self) {
        ultraCanvasWindow = window;
    }
    return self;
}

- (BOOL)windowShouldClose:(NSWindow*)sender {
    if (ultraCanvasWindow) {
        ultraCanvasWindow->OnWindowWillClose();
    }
    return NO; // We handle closing ourselves via Close() -> DestroyNative()
}

- (void)windowWillClose:(NSNotification*)notification {
    // Do NOT call OnWindowWillClose() here - it is already called in windowShouldClose:.
    // This also fires when DestroyNative() calls [nsWindow close], at which point
    // the window is already being destroyed.
}

- (void)windowDidResize:(NSNotification*)notification {
    if (ultraCanvasWindow) {
        ultraCanvasWindow->OnWindowDidResize();
    }
}

- (void)windowDidMove:(NSNotification*)notification {
    if (ultraCanvasWindow) {
        ultraCanvasWindow->OnWindowDidMove();
    }
}

- (void)windowDidBecomeKey:(NSNotification*)notification {
    if (ultraCanvasWindow) {
        ultraCanvasWindow->OnWindowDidBecomeKey();
    }
}

- (void)windowDidResignKey:(NSNotification*)notification {
    if (ultraCanvasWindow) {
        ultraCanvasWindow->OnWindowDidResignKey();
    }
}

- (void)windowDidMiniaturize:(NSNotification*)notification {
    if (ultraCanvasWindow) {
        ultraCanvasWindow->OnWindowDidMiniaturize();
    }
}

- (void)windowDidDeminiaturize:(NSNotification*)notification {
    if (ultraCanvasWindow) {
        ultraCanvasWindow->OnWindowDidDeminiaturize();
    }
}

- (void)windowDidChangeBackingProperties:(NSNotification*)notification {
    if (ultraCanvasWindow) {
        ultraCanvasWindow->OnWindowDidChangeBackingProperties();
    }
}

- (void)dealloc {
    ultraCanvasWindow = nullptr;
}

@end

namespace UltraCanvas {

// ===== CONSTRUCTOR & DESTRUCTOR =====
    UltraCanvasMacOSWindow::UltraCanvasMacOSWindow()
            :  nsWindow(nullptr)
            , contentView(nullptr)
            , windowDelegate(nullptr) {
        debugOutput << "UltraCanvas macOS: Window constructor started" << std::endl;
    }


    void UltraCanvasMacOSWindow::DestroyNative() {
        if (!_created) {
            return;
        }
        @autoreleasepool {
            // Cleanup render context
            renderContext.reset();

            // Destroy Cairo surface
            DestroyNativeCairoSurface();

            // Detach delegate from window BEFORE closing or releasing it,
            // otherwise [nsWindow close] fires windowWillClose: on a freed delegate
            if (nsWindow) {
                [nsWindow setDelegate:nil];
            }

            // Release delegate
            if (windowDelegate) {
                (void)CFBridgingRelease(windowDelegate);
                windowDelegate = nullptr;
            }

            // Close and release window
            if (nsWindow) {
                [nsWindow close];
                nsWindow = nullptr;
            }
            debugOutput << "UltraCanvas macOS: Native Window destroyed" << std::endl;
        }
    }

    // ===== WINDOW CREATION =====
    bool UltraCanvasMacOSWindow::CreateNative() {
        if (_created) {
            debugOutput << "UltraCanvas macOS: Window already created" << std::endl;
            return true;
        }

        auto application = UltraCanvasApplication::GetInstance();
        if (!application || !application->IsInitialized()) {
            debugOutput << "UltraCanvas macOS: Cannot create window - application not ready" << std::endl;
            return false;
        }

        debugOutput << "UltraCanvas macOS: Creating NSWindow..." << std::endl;

        @autoreleasepool {
            if (!CreateNSWindow()) {
                debugOutput << "UltraCanvas macOS: Failed to create NSWindow" << std::endl;
                return false;
            }

            if (!CreateNativeCairoSurface()) {
                debugOutput << "UltraCanvas macOS: Failed to create Cairo surface" << std::endl;
                nsWindow = nullptr;
                return false;
            }

//            try {
//                renderContext = std::make_unique<RenderContextCairo>(
//                    cairoSurface, config_.width, config_.height, false);
//                debugOutput << "UltraCanvas macOS: Render context created successfully" << std::endl;
//            } catch (const std::exception& e) {
//                debugOutput << "UltraCanvas macOS: Failed to create render context: " << e.what() << std::endl;
//                DestroyCairoSurface();
//                nsWindow = nullptr;
//                return false;
//            }

            // Apply window icon
            std::string iconToUse = config_.iconPath;
            if (iconToUse.empty()) {
                iconToUse = application->GetDefaultWindowIcon();
            }
            if (!iconToUse.empty()) {
                SetWindowIcon(iconToUse);
            }

            debugOutput << "UltraCanvas macOS: CreateNative Native Window created successfully!" << std::endl;
            return true;
        }
    }

    bool UltraCanvasMacOSWindow::CreateNSWindow() {
        @autoreleasepool {
            // Calculate window frame
            NSRect windowFrame = NSMakeRect(
                config_.x >= 0 ? config_.x : 100,
                config_.y >= 0 ? config_.y : 100,
                config_.width > 0 ? config_.width : 800,
                config_.height > 0 ? config_.height : 600
            );

            // Window style mask
            NSWindowStyleMask styleMask;
            if (config_.type == WindowType::Borderless) {
                styleMask = NSWindowStyleMaskBorderless;
            } else {
                styleMask = NSWindowStyleMaskTitled |
                            NSWindowStyleMaskClosable |
                            NSWindowStyleMaskMiniaturizable;
                if (config_.resizable) {
                    styleMask |= NSWindowStyleMaskResizable;
                }
            }

            // Create window
            nsWindow = [[NSWindow alloc] initWithContentRect:windowFrame
                                                   styleMask:styleMask
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];

            if (!nsWindow) {
                debugOutput << "UltraCanvas macOS: Failed to create NSWindow" << std::endl;
                return false;
            }

            // Set window properties
            [nsWindow setTitle:[NSString stringWithUTF8String:config_.title.c_str()]];
            [nsWindow setReleasedWhenClosed:NO];
            [nsWindow setAcceptsMouseMovedEvents:YES];

            // Opt out of AppKit's automatic window state restoration. The framework
            // owns its own window/UI state, and AppKit's NSPersistentUIManager
            // periodically archives restorable windows via NSKeyedArchiver — that
            // path crashes on macOS 26.x while encoding our custom Cairo NSView
            // (CFRelease alignment fault inside __CFBinaryPlistWriteOrPresize).
            [nsWindow setRestorable:NO];

            // Create custom view for Cairo rendering
            NSRect contentFrame = [[nsWindow contentView] frame];
            contentView = [[UltraCanvasView alloc] initWithFrame:contentFrame window:this];
            [nsWindow setContentView:contentView];

            // Create and set delegate
            UltraCanvasWindowDelegate* delegate = [[UltraCanvasWindowDelegate alloc] initWithWindow:this];
            windowDelegate = (__bridge_retained void*)delegate;
            [nsWindow setDelegate:delegate];

            debugOutput << "UltraCanvas macOS: NSWindow created successfully" << std::endl;
            return true;
        }
    }

    bool UltraCanvasMacOSWindow::RefreshBackingScaleFactor() {
        float newScale = 1.0f;
        @autoreleasepool {
            if (nsWindow) {
                newScale = static_cast<float>([nsWindow backingScaleFactor]);
            } else {
                NSScreen* screen = [NSScreen mainScreen];
                if (screen) newScale = static_cast<float>([screen backingScaleFactor]);
            }
        }
        if (newScale <= 0.0f) newScale = 1.0f;
        bool changed = (newScale != backingScaleFactor);
        backingScaleFactor = newScale;
        return changed;
    }

    bool UltraCanvasMacOSWindow::CreateNativeCairoSurface() {
        //std::lock_guard<std::mutex> lock(cairoMutex);

        debugOutput << "UltraCanvas macOS: Creating Cairo surface..." << std::endl;

        // Get logical (point) window dimensions
        int width = config_.width;
        int height = config_.height;

        if (width <= 0 || height <= 0) {
            debugOutput << "UltraCanvas macOS: Invalid surface dimensions" << std::endl;
            return false;
        }

        // Pull current per-window backing scale (queries [nsWindow backingScaleFactor]).
        // 1.0 on standard displays, 2.0 on Retina, etc.
        RefreshBackingScaleFactor();
        const float scale = backingScaleFactor;
        const int pixW = static_cast<int>(width * scale);
        const int pixH = static_cast<int>(height * scale);

        // Create Quartz surface at *backing pixel* dimensions so font/vector
        // rasterization runs at full Retina detail.
        nativeSurface = cairo_quartz_surface_create(CAIRO_FORMAT_ARGB32, pixW, pixH);

        if (!nativeSurface) {
            debugOutput << "UltraCanvas macOS: Failed to create Cairo Quartz surface" << std::endl;
            return false;
        }

        cairo_status_t status = cairo_surface_status(static_cast<cairo_surface_t*>(nativeSurface));
        if (status != CAIRO_STATUS_SUCCESS) {
            debugOutput << "UltraCanvas macOS: Cairo surface error: "
                      << cairo_status_to_string(status) << std::endl;
            cairo_surface_destroy(static_cast<cairo_surface_t*>(nativeSurface));
            nativeSurface = nullptr;
            return false;
        }

        // Tell Cairo that 1 user-space unit = `scale` device pixels on this
        // surface. All higher-level UI drawing keeps using logical coordinates;
        // Cairo internally rasterizes onto the larger pixel grid.
        cairo_surface_set_device_scale(static_cast<cairo_surface_t*>(nativeSurface), scale, scale);

        // Update custom view
        if (contentView) {
            ((UltraCanvasView*)contentView).nativeSurface = (cairo_surface_t*)nativeSurface;
            // Keep the view's CALayer at backing resolution so the final blit
            // is presented without additional Cocoa scaling.
            if (((NSView*)contentView).layer) {
                ((NSView*)contentView).layer.contentsScale = scale;
            }
        }

        debugOutput << "UltraCanvas macOS: Cairo surface created successfully ("
                    << pixW << "x" << pixH << " px @ " << scale << "x)" << std::endl;
        return true;
    }

    void UltraCanvasMacOSWindow::DestroyNativeCairoSurface() {
        std::lock_guard<std::mutex> lock(cairoMutex);

        if (nativeSurface) {
            debugOutput << "UltraCanvas macOS: Destroying Cairo surface..." << std::endl;
            cairo_surface_destroy(static_cast<cairo_surface_t*>(nativeSurface));
            nativeSurface = nullptr;
        }

        //cgContext = nullptr;
    }

    void UltraCanvasMacOSWindow::DoResizeNative() {
        std::lock_guard<std::mutex> lock(cairoMutex);
        int w = config_.width;
        int h = config_.height;

        debugOutput << "UltraCanvasMacOSWindow::DoResizeNative: Resizing Cairo surface to " << w << "x" << h << std::endl;

        auto oldCairoSurface = nativeSurface;

        if (!CreateNativeCairoSurface()) {
            return;
        }

        // Destroy old surface
        if (oldCairoSurface) {
            cairo_surface_destroy(static_cast<cairo_surface_t*>(oldCairoSurface));
        }
    }

    // ===== WINDOW MANAGEMENT =====
    void UltraCanvasMacOSWindow::Show() {
        if (!_created || _windowVisible) return;

        debugOutput << "UltraCanvas macOS: Showing window..." << std::endl;
        if (!UltraCanvasApplication::GetInstance()->IsRunning()) {
            debugOutput << "UltraCanvas Application is not running yet, delaying window show..." << std::endl;
            pendingShow = true;
            return;
        }

        pendingShow = false;
        @autoreleasepool {
            [nsWindow makeKeyAndOrderFront:nil];

            // Ensure the window is not miniaturized
            if ([nsWindow isMiniaturized]) {
                [nsWindow deminiaturize:nil];
            }
            _windowVisible = true;
        }

        if (onWindowShow) {
            onWindowShow();
        }
        RequestRedraw();
    }

    void UltraCanvasMacOSWindow::Hide() {
        if (!_created || !_windowVisible) return;

        debugOutput << "UltraCanvas macOS: Hiding window..." << std::endl;

        @autoreleasepool {
            [nsWindow orderOut:nil];
            _windowVisible = false;
        }

        if (onWindowHide) {
            onWindowHide();
        }
    }

    void UltraCanvasMacOSWindow::Minimize() {
        if (!_created) return;

        @autoreleasepool {
            [nsWindow miniaturize:nil];
        }
    }

    void UltraCanvasMacOSWindow::Maximize() {
        if (!_created) return;

        @autoreleasepool {
            [nsWindow zoom:nil];
        }
    }

    void UltraCanvasMacOSWindow::Restore() {
        if (!_created) return;

        @autoreleasepool {
            if ([nsWindow isMiniaturized]) {
                [nsWindow deminiaturize:nil];
            }
        }
    }

    void UltraCanvasMacOSWindow::Focus() {
        if (!_created) return;

        @autoreleasepool {
            [nsWindow makeKeyAndOrderFront:nil];
        }
    }

    void UltraCanvasMacOSWindow::RaiseAndFocus() {
        if (!_created) return;

        @autoreleasepool {
            [nsWindow makeKeyAndOrderFront:nil];
            [NSApp activateIgnoringOtherApps:YES];
        }
    }

    // ===== WINDOW PROPERTIES =====
    void UltraCanvasMacOSWindow::SetWindowTitle(const std::string& title) {
        config_.title = title;

        if (_created) {
            @autoreleasepool {
                [nsWindow setTitle:[NSString stringWithUTF8String:title.c_str()]];
            }
        }
    }

    void UltraCanvasMacOSWindow::SetWindowIcon(const std::string& iconPath) {
        if (iconPath.empty()) return;

        // Load the icon image
        auto img = UCImageRaster::Load(iconPath, false);
        if (!img || !img->IsValid()) {
            debugOutput << "UltraCanvas macOS: Failed to load icon: " << iconPath << std::endl;
            return;
        }

        auto pixmap = img->GetPixmap();
        if (!pixmap || !pixmap->IsValid()) {
            debugOutput << "UltraCanvas macOS: Failed to create pixmap for icon" << std::endl;
            return;
        }

        int w = pixmap->GetWidth();
        int h = pixmap->GetHeight();
        uint32_t* pixels = pixmap->GetPixelData();
        if (!pixels || w <= 0 || h <= 0) return;

        @autoreleasepool {
            // Create CGImage from ARGB pixel data
            CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
            CGContextRef ctx = CGBitmapContextCreate(
                pixels, w, h, 8, w * 4,
                colorSpace,
                kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host
            );

            if (ctx) {
                CGImageRef cgImage = CGBitmapContextCreateImage(ctx);
                if (cgImage) {
                    NSImage* nsImage = [[NSImage alloc] initWithCGImage:cgImage
                                                                  size:NSMakeSize(w, h)];
                    if (nsImage) {
                        // macOS sets icon at the application level (Dock icon)
                        [NSApp setApplicationIconImage:nsImage];
                        debugOutput << "UltraCanvas macOS: Application icon set (" << w << "x" << h
                                  << ") from: " << iconPath << std::endl;
                    }
                    CGImageRelease(cgImage);
                }
                CGContextRelease(ctx);
            }
            CGColorSpaceRelease(colorSpace);
        }
    }

    void UltraCanvasMacOSWindow::SetWindowPosition(int x, int y) {
        config_.x = x;
        config_.y = y;

        if (_created) {
            @autoreleasepool {
                NSPoint origin = NSMakePoint(x, y);
                [nsWindow setFrameOrigin:origin];
            }
        }
    }

    void UltraCanvasMacOSWindow::SetResizable(bool resizable) {

    }

    void UltraCanvasMacOSWindow::SetWindowSize(int width, int height) {
        config_.width = width;
        config_.height = height;

        if (_created) {
            @autoreleasepool {
                NSSize size = NSMakeSize(width, height);
                [nsWindow setContentSize:size];
                _needsResize = true;
            }
        } else {
            UltraCanvasWindowBase::SetSize(width, height);
        }
    }

    void UltraCanvasMacOSWindow::SetFullscreen(bool fullscreen) {
        if (!_created) return;

        @autoreleasepool {
            BOOL isFullscreen = ([nsWindow styleMask] & NSWindowStyleMaskFullScreen) != 0;

            if (fullscreen && !isFullscreen) {
                [nsWindow toggleFullScreen:nil];
            } else if (!fullscreen && isFullscreen) {
                [nsWindow toggleFullScreen:nil];
            }
        }
    }

    // ===== RENDERING =====
    void UltraCanvasMacOSWindow::InvalidateWindowNative() {
        if (!_created || !contentView) return;

        @autoreleasepool {
            [(NSView*)contentView setNeedsDisplay:YES];
        }
    }

    NativeWindowHandle UltraCanvasMacOSWindow::GetNativeHandle() const  {
        return (NativeWindowHandle)(__bridge void*)nsWindow;
    };

    NSWindow* UltraCanvasMacOSWindow::GetNSWindowHandle() const {
        return (NSWindow*)nsWindow;
    };

    void UltraCanvasMacOSWindow::GetScreenSize(int& width, int& height) const {
        @autoreleasepool {
            NSRect screenFrame = [[NSScreen mainScreen] frame];
            width = static_cast<int>(screenFrame.size.width);
            height = static_cast<int>(screenFrame.size.height);
        }
    }

    void UltraCanvasMacOSWindow::GetScreenBounds(int& x, int& y, int& width, int& height) const {
        @autoreleasepool {
            NSScreen* screen = nil;
            if (nsWindow) {
                screen = [nsWindow screen];  // Screen the window currently resides on
            }
            if (!screen) {
                screen = [NSScreen mainScreen];
            }
            NSRect frame = [screen frame];
            // NSScreen frames are already in the unified multi-monitor coordinate
            // space used by NSWindow's setFrameOrigin (which SetWindowPosition calls
            // directly), so no y-flip conversion is needed here.
            x = static_cast<int>(frame.origin.x);
            y = static_cast<int>(frame.origin.y);
            width = static_cast<int>(frame.size.width);
            height = static_cast<int>(frame.size.height);
        }
    }

    // ===== WINDOW DELEGATE CALLBACKS =====
    void UltraCanvasMacOSWindow::OnWindowWillClose() {
        debugOutput << "UltraCanvas macOS: Window will close callback" << std::endl;
        Close();
    }

    void UltraCanvasMacOSWindow::OnWindowDidResize() {
        if (!_created) return;

        @autoreleasepool {
            NSSize size = [[nsWindow contentView] frame].size;
            int newWidth = static_cast<int>(size.width);
            int newHeight = static_cast<int>(size.height);

            if (newWidth != config_.width || newHeight != config_.height) {
                config_.width = newWidth;
                config_.height = newHeight;

                DoResize();
                RequestWindowComposition();
                UpdateAndRender();
                InvalidateWindowNative();
            }
        }
    }

    void UltraCanvasMacOSWindow::OnWindowDidMove() {
        if (!_created) return;

        @autoreleasepool {
            NSPoint origin = [nsWindow frame].origin;
            config_.x = static_cast<int>(origin.x);
            config_.y = static_cast<int>(origin.y);

            if (onWindowMove) {
                onWindowMove(config_.x, config_.y);
            }
        }
    }

    void UltraCanvasMacOSWindow::OnWindowDidBecomeKey() {
        UCEvent event;
        event.type = UCEventType::WindowFocus;
        event.targetWindow = this;
        event.nativeWindowHandle = GetNativeHandle();
        UltraCanvasApplication::GetInstance()->PushEvent(event);
    }

    void UltraCanvasMacOSWindow::OnWindowDidResignKey() {
        UCEvent event;
        event.type = UCEventType::WindowBlur;
        event.targetWindow = this;
        event.nativeWindowHandle = GetNativeHandle();
        UltraCanvasApplication::GetInstance()->PushEvent(event);
    }

    void UltraCanvasMacOSWindow::OnWindowDidMiniaturize() {
        debugOutput << "UltraCanvas macOS: Window minimized" << std::endl;
    }

    void UltraCanvasMacOSWindow::OnWindowDidDeminiaturize() {
        debugOutput << "UltraCanvas macOS: Window restored from minimized" << std::endl;
    }

    void UltraCanvasMacOSWindow::OnWindowDidChangeBackingProperties() {
        if (!_created) return;

        const float oldScale = backingScaleFactor;
        if (!RefreshBackingScaleFactor()) {
            // No actual change (e.g. a redundant notification).
            return;
        }
        debugOutput << "UltraCanvas macOS: Backing scale changed " << oldScale
                    << " -> " << backingScaleFactor << ", recreating Cairo surface" << std::endl;

        // Recreate the offscreen Cairo surface at the new backing dimensions.
        // Logical width/height stay the same; only the pixel resolution shifts.
        {
            std::lock_guard<std::mutex> lock(cairoMutex);
            auto oldCairoSurface = nativeSurface;

            if (!CreateNativeCairoSurface()) {
                return;
            }

            if (oldCairoSurface) {
                cairo_surface_destroy(static_cast<cairo_surface_t*>(oldCairoSurface));
            }
        }

        // Force a full re-layout / repaint at the new resolution.
        DoResize();
        RequestWindowComposition();
        UpdateAndRender();
        InvalidateWindowNative();
    }
} // namespace UltraCanvas

