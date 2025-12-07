// OS/MacOS/UltraCanvasMacOSWindow.mm
// Complete macOS window implementation with Cocoa and Cairo
// Version: 2.0.1
// Last Modified: 2025-12-05
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasMacOSWindow.h"

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

#include <iostream>
#include <cstring>

// ===== CUSTOM NSVIEW FOR CAIRO RENDERING =====
@interface UltraCanvasView : NSView {
    UltraCanvas::UltraCanvasMacOSWindow* ultraCanvasWindow;
    cairo_surface_t* cairoSurface;
    cairo_t* cairoContext;
}

@property (nonatomic, assign) UltraCanvas::UltraCanvasMacOSWindow* ultraCanvasWindow;
@property (nonatomic, assign) cairo_surface_t* cairoSurface;
@property (nonatomic, assign) cairo_t* cairoContext;

- (instancetype)initWithFrame:(NSRect)frameRect window:(UltraCanvas::UltraCanvasMacOSWindow*)window;
- (void)drawRect:(NSRect)dirtyRect;
- (BOOL)acceptsFirstResponder;
- (BOOL)isFlipped;

@end

@implementation UltraCanvasView

@synthesize ultraCanvasWindow;
@synthesize cairoSurface;
@synthesize cairoContext;

- (instancetype)initWithFrame:(NSRect)frameRect window:(UltraCanvas::UltraCanvasMacOSWindow*)window {
    self = [super initWithFrame:frameRect];
    if (self) {
        ultraCanvasWindow = window;
        cairoSurface = nullptr;
        cairoContext = nullptr;
        
        // Enable layer backing for better performance
        [self setWantsLayer:YES];
    }
    return self;
}

- (void)drawRect:(NSRect)dirtyRect {
    if (!ultraCanvasWindow || !cairoSurface) return;

    @autoreleasepool {
        // Get the current CGContext
        CGContextRef cgContext = [[NSGraphicsContext currentContext] CGContext];
        if (!cgContext) return;

        // Flush Cairo surface
        cairo_surface_flush(cairoSurface);

        // Get the Cairo surface's CGImage
//        CGImageRef cgImage = cairo_quartz_surface_get_image(cairoSurface);
//        if (cgImage) {
//            // Draw the image
//            CGRect bounds = NSRectToCGRect([self bounds]);
//            CGContextDrawImage(cgContext, bounds, cgImage);
//        }
    }
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (BOOL)isFlipped {
    return YES;  // Use top-left origin like other platforms
}

- (void)dealloc {
    ultraCanvasWindow = nullptr;
    cairoSurface = nullptr;
    cairoContext = nullptr;
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
    return YES;
}

- (void)windowWillClose:(NSNotification*)notification {
    if (ultraCanvasWindow) {
        ultraCanvasWindow->OnWindowWillClose();
    }
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

- (void)dealloc {
    ultraCanvasWindow = nullptr;
}

@end

namespace UltraCanvas {

// ===== CONSTRUCTOR & DESTRUCTOR =====
    UltraCanvasMacOSWindow::UltraCanvasMacOSWindow()
            :  nsWindow(nullptr)
            , contentView(nullptr)
            , windowDelegate(nullptr)
            , cairoSurface(nullptr)
            , cgContext(nullptr) {

        std::cout << "UltraCanvas macOS: Window constructor started" << std::endl;

    }

    UltraCanvasMacOSWindow::UltraCanvasMacOSWindow(const WindowConfig& config)
    : nsWindow(nullptr)
    , contentView(nullptr)
    , windowDelegate(nullptr)
    , cairoSurface(nullptr)
    , cgContext(nullptr) {

    std::cout << "UltraCanvas macOS: Window constructor started" << std::endl;

    if (!CreateNative()) {
        std::cerr << "UltraCanvas macOS: Failed to create native window" << std::endl;
        throw std::runtime_error("Failed to create macOS window");
    }

    std::cout << "UltraCanvas macOS: Window constructor completed successfully" << std::endl;
}

UltraCanvasMacOSWindow::~UltraCanvasMacOSWindow() {
    std::cout << "UltraCanvas macOS: Window destructor called" << std::endl;

    if (_created) {
        @autoreleasepool {
            // Cleanup render context
            renderContext.reset();

            // Destroy Cairo surface
            DestroyCairoSurface();

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

            _created = false;
        }
    }

    std::cout << "UltraCanvas macOS: Window destroyed" << std::endl;
}

// ===== WINDOW CREATION =====
bool UltraCanvasMacOSWindow::CreateNative() {
    if (_created) {
        std::cout << "UltraCanvas macOS: Window already created" << std::endl;
        return true;
    }

    auto application = UltraCanvasApplication::GetInstance();
    if (!application || !application->IsInitialized()) {
        std::cerr << "UltraCanvas macOS: Cannot create window - application not ready" << std::endl;
        return false;
    }

    std::cout << "UltraCanvas macOS: Creating NSWindow..." << std::endl;

    @autoreleasepool {
        if (!CreateNSWindow()) {
            std::cerr << "UltraCanvas macOS: Failed to create NSWindow" << std::endl;
            return false;
        }

        if (!CreateCairoSurface()) {
            std::cerr << "UltraCanvas macOS: Failed to create Cairo surface" << std::endl;
            nsWindow = nullptr;
            return false;
        }

        try {
            renderContext = std::make_unique<RenderContextCairo>(
                cairoSurface, config_.width, config_.height, true);
            std::cout << "UltraCanvas macOS: Render context created successfully" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "UltraCanvas macOS: Failed to create render context: " << e.what() << std::endl;
            DestroyCairoSurface();
            nsWindow = nullptr;
            return false;
        }

        std::cout << "UltraCanvas macOS: CreateNative Native Window created successfully!" << std::endl;
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
        NSWindowStyleMask styleMask = NSWindowStyleMaskTitled |
                                      NSWindowStyleMaskClosable |
                                      NSWindowStyleMaskMiniaturizable;

        if (config_.resizable) {
            styleMask |= NSWindowStyleMaskResizable;
        }

        // Create window
        nsWindow = [[NSWindow alloc] initWithContentRect:windowFrame
                                               styleMask:styleMask
                                                 backing:NSBackingStoreBuffered
                                                   defer:NO];

        if (!nsWindow) {
            std::cerr << "UltraCanvas macOS: Failed to create NSWindow" << std::endl;
            return false;
        }

        // Set window properties
        [nsWindow setTitle:[NSString stringWithUTF8String:config_.title.c_str()]];
        [nsWindow setReleasedWhenClosed:NO];
        [nsWindow setAcceptsMouseMovedEvents:YES];

        // Create custom view for Cairo rendering
        NSRect contentFrame = [[nsWindow contentView] frame];
        contentView = [[UltraCanvasView alloc] initWithFrame:contentFrame window:this];
        [nsWindow setContentView:contentView];

        // Create and set delegate
        UltraCanvasWindowDelegate* delegate = [[UltraCanvasWindowDelegate alloc] initWithWindow:this];
        windowDelegate = (__bridge_retained void*)delegate;
        [nsWindow setDelegate:delegate];

        std::cout << "UltraCanvas macOS: NSWindow created successfully" << std::endl;
        return true;
    }
}

bool UltraCanvasMacOSWindow::CreateCairoSurface() {
    //std::lock_guard<std::mutex> lock(cairoMutex);

    std::cout << "UltraCanvas macOS: Creating Cairo surface..." << std::endl;

    // Get window dimensions
    int width = config_.width;
    int height = config_.height;

    if (width <= 0 || height <= 0) {
        std::cerr << "UltraCanvas macOS: Invalid surface dimensions" << std::endl;
        return false;
    }

    // Create Quartz surface for Cairo
    cairoSurface = cairo_quartz_surface_create(CAIRO_FORMAT_ARGB32, width, height);

    if (!cairoSurface) {
        std::cerr << "UltraCanvas macOS: Failed to create Cairo Quartz surface" << std::endl;
        return false;
    }

    cairo_status_t status = cairo_surface_status(cairoSurface);
    if (status != CAIRO_STATUS_SUCCESS) {
        std::cerr << "UltraCanvas macOS: Cairo surface error: " 
                  << cairo_status_to_string(status) << std::endl;
        cairo_surface_destroy(cairoSurface);
        cairoSurface = nullptr;
        return false;
    }

    // Get CGContext from Cairo surface
    cgContext = cairo_quartz_surface_get_cg_context(cairoSurface);

    // Update custom view
    if (contentView) {
        [(UltraCanvasView*)contentView setCairoSurface:cairoSurface];
    }

    std::cout << "UltraCanvas macOS: Cairo surface created successfully" << std::endl;
    return true;
}

void UltraCanvasMacOSWindow::DestroyCairoSurface() {
    std::lock_guard<std::mutex> lock(cairoMutex);

    if (cairoSurface) {
        std::cout << "UltraCanvas macOS: Destroying Cairo surface..." << std::endl;
        cairo_surface_destroy(cairoSurface);
        cairoSurface = nullptr;
    }

    cgContext = nullptr;
}

void UltraCanvasMacOSWindow::ResizeCairoSurface(int width, int height) {
    std::lock_guard<std::mutex> lock(cairoMutex);

    std::cout << "UltraCanvas macOS: Resizing Cairo surface to " << width << "x" << height << std::endl;

    auto oldCairoSurface = cairoSurface;

    if (!CreateCairoSurface()) {
        return;
    }

    // Destroy old surface
    if (oldCairoSurface) {
        cairo_surface_destroy(oldCairoSurface);
    }

        // Update render context
    if (renderContext) {
        renderContext->SetTargetSurface(cairoSurface, width, height);
        renderContext->ResizeStagingSurface(width, height);
    }
}

void UltraCanvasMacOSWindow::DestroyNative() {
    std::cout << "UltraCanvas MacOS: Destroying window..." << std::endl;
    renderContext.reset();
    DestroyCairoSurface();
}

// ===== WINDOW MANAGEMENT =====
void UltraCanvasMacOSWindow::Show() {
    if (!_created || _visible) return;

    std::cout << "UltraCanvas macOS: Showing window..." << std::endl;
    if (!UltraCanvasApplication::GetInstance()->IsRunning()) {
        std::cout << "UltraCanvas Application is not running yet, delaying window show..." << std::endl;
        pendingShow = true;
        return;
    }

    pendingShow = false;
    @autoreleasepool {
        [nsWindow makeKeyAndOrderFront:nil];

//        [nsWindow makeKey];

        [[nsWindow contentView] setNeedsDisplay:YES];

        // Ensure the window is not miniaturized
        if ([nsWindow isMiniaturized]) {
            [nsWindow deminiaturize:nil];
        }
        _visible = true;
    }

    if (onWindowShow) {
        onWindowShow();
    }
}

void UltraCanvasMacOSWindow::Hide() {
    if (!_created || !_visible) return;

    std::cout << "UltraCanvas macOS: Hiding window..." << std::endl;

    @autoreleasepool {
        [nsWindow orderOut:nil];
        _visible = false;
    }

    if (onWindowHide) {
        onWindowHide();
    }
}

void UltraCanvasMacOSWindow::Close() {
    if (!_created) return;

    std::cout << "UltraCanvas macOS: Closing window..." << std::endl;

    if (onWindowClose) {
        onWindowClose();
    }

    @autoreleasepool {
        [nsWindow close];
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

// ===== WINDOW PROPERTIES =====
void UltraCanvasMacOSWindow::SetWindowTitle(const std::string& title) {
    config_.title = title;

    if (_created) {
        @autoreleasepool {
            [nsWindow setTitle:[NSString stringWithUTF8String:title.c_str()]];
        }
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
            ResizeCairoSurface(width, height);
        }
    }

    UltraCanvasWindowBase::SetSize(width, height);
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
void UltraCanvasMacOSWindow::Invalidate() {
    if (!_created || !contentView) return;

    @autoreleasepool {
        [(NSView*)contentView setNeedsDisplay:YES];
    }
}

void UltraCanvasMacOSWindow::Flush() {
    if (!_created || !renderContext) return;

    std::lock_guard<std::mutex> lock(cairoMutex);
    renderContext->SwapBuffers();

    // Flush Cairo surface
    if (cairoSurface) {
        cairo_surface_flush(cairoSurface);
    }

    // Trigger redraw
    Invalidate();
}

unsigned long UltraCanvasMacOSWindow::GetNativeHandle() const {
    return (unsigned long)(__bridge_retained void*)nsWindow;
}

// ===== WINDOW DELEGATE CALLBACKS =====
void UltraCanvasMacOSWindow::OnWindowWillClose() {
    std::cout << "UltraCanvas macOS: Window will close callback" << std::endl;

    if (onWindowClose) {
        onWindowClose();
    }
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

            ResizeCairoSurface(newWidth, newHeight);

            if (onWindowResize) {
                onWindowResize(newWidth, newHeight);
            }
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
    auto application = UltraCanvasApplication::GetInstance();
    if (application) {
        application->HandleFocusedWindowChange(this);
    }
}

void UltraCanvasMacOSWindow::OnWindowDidResignKey() {
    auto application = UltraCanvasApplication::GetInstance();
    if (application && application->GetFocusedWindow() == this) {
        application->HandleFocusedWindowChange(nullptr);
    }
}

void UltraCanvasMacOSWindow::OnWindowDidMiniaturize() {
    std::cout << "UltraCanvas macOS: Window minimized" << std::endl;
}

void UltraCanvasMacOSWindow::OnWindowDidDeminiaturize() {
    std::cout << "UltraCanvas macOS: Window restored from minimized" << std::endl;
}

} // namespace UltraCanvas

