// OS/MacOS/UltraCanvasMacOSWindow.mm
// Complete macOS window implementation with Cocoa and Cairo
// Version: 2.0.1
// Last Modified: 2025-12-05
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasMacOSWindow.h"
#include "UltraCanvasUtils.h"

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

#include <iostream>
#include <cstring>

@interface UltraCanvasView : NSView {
    UltraCanvas::UltraCanvasMacOSWindow* ultraCanvasWindow;
    cairo_surface_t* cairoSurface;
}

@property (nonatomic, assign) UltraCanvas::UltraCanvasMacOSWindow* ultraCanvasWindow;
@property (nonatomic, assign) cairo_surface_t* cairoSurface;

@end

@implementation UltraCanvasView

@synthesize ultraCanvasWindow;
@synthesize cairoSurface;

- (instancetype)initWithFrame:(NSRect)frameRect window:(UltraCanvas::UltraCanvasMacOSWindow*)window {
    self = [super initWithFrame:frameRect];
    if (self) {
        ultraCanvasWindow = window;
        cairoSurface = nullptr;
        [self setWantsLayer:YES];
    }
    return self;
}

void _drawScreen(int w, int h, CGContextRef viewCtx, cairo_surface_t* cairoSurface) {
    cairo_surface_t* viewSurface = cairo_quartz_surface_create_for_cg_context(viewCtx, w, h);
    cairo_t* cr = cairo_create(viewSurface);

    // Single operation: paint source surface to view
    cairo_set_source_surface(cr, cairoSurface, 0, 0);
    cairo_paint(cr);

    cairo_destroy(cr);
    cairo_surface_destroy(viewSurface);
}

- (void)drawRect:(NSRect)dirtyRect {
    if (!cairoSurface) return;

    // Get view's CGContext
    CGContextRef viewCtx = [[NSGraphicsContext currentContext] CGContext];
    if (!viewCtx) return;

    // Flush pending Cairo operations
    cairo_surface_flush(cairoSurface);

    // Create a temporary Cairo surface wrapping the VIEW's context
    NSRect bounds = [self bounds];
    int w = (int)bounds.size.width;
    int h = (int)bounds.size.height;

    // Wrap the view's CGContext in a Cairo surface
//    measureExecutionTime("dirtyRect", _drawScreen, w, h, viewCtx, cairoSurface);
//    return;

    cairo_surface_t* viewSurface = cairo_quartz_surface_create_for_cg_context(viewCtx, w, h);
    cairo_t* cr = cairo_create(viewSurface);

    // Handle coordinate flip (NSView with isFlipped:YES)
    // Cairo and Quartz both use bottom-left origin, but isFlipped changes this
//    cairo_translate(cr, 0, h);
//    cairo_scale(cr, 1.0, -1.0);

    // Single operation: paint source surface to view
    cairo_set_source_surface(cr, cairoSurface, 0, 0);
    cairo_paint(cr);

    cairo_destroy(cr);
    cairo_surface_destroy(viewSurface);
}

- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)isFlipped { return YES; }

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
            , cairoSurface(nullptr) {
        std::cout << "UltraCanvas macOS: Window constructor started" << std::endl;
    }


    void UltraCanvasMacOSWindow::DestroyNative() {
        if (!_created) {
            return;
        }
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
            std::cout << "UltraCanvas macOS: Native Window destroyed" << std::endl;
        }
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
                    cairoSurface, config_.width, config_.height, false);
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
        //cgContext = cairo_quartz_surface_get_cg_context(cairoSurface);

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

        //cgContext = nullptr;
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
        }
    }

    // ===== WINDOW MANAGEMENT =====
    void UltraCanvasMacOSWindow::Show() {
        if (!_created || visible) return;

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
            visible = true;
        }

        if (onWindowShow) {
            onWindowShow();
        }
    }

    void UltraCanvasMacOSWindow::Hide() {
        if (!_created || !visible) return;

        std::cout << "UltraCanvas macOS: Hiding window..." << std::endl;

        @autoreleasepool {
            [nsWindow orderOut:nil];
            visible = false;
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

    // ===== MOUSE POINTER CONTROL =====
    void UltraCanvasMacOSWindow::SelectMouseCursorNative(UCMouseCursor cur) {
        if (!_created) {
            return;
        }

        @autoreleasepool {
            NSCursor* cursor = nil;

            // Map MousePointer enum to NSCursor
            switch (cur) {
                case UCMouseCursor::Default:
                    cursor = [NSCursor arrowCursor];
                    break;

                case UCMouseCursor::NoCursor:
                    // Hide cursor - use an invisible cursor
                    // Note: NSCursor doesn't have a built-in invisible cursor
                    // We use hideCursor but need to be careful with show/hide balance
                    [NSCursor hide];
                    currentMousePointer = ptr;
                    return;

                case UCMouseCursor::Hand:
                    cursor = [NSCursor pointingHandCursor];
                    break;

                case UCMouseCursor::Text:
                    cursor = [NSCursor IBeamCursor];
                    break;

                case UCMouseCursor::Wait:
                    // macOS doesn't have a standard wait cursor
                    // Use the spinning beach ball effect is system-managed
                    // Fall back to arrow cursor
                    cursor = [NSCursor arrowCursor];
                    break;

                case UCMouseCursor::Cross:
                    cursor = [NSCursor crosshairCursor];
                    break;

                case UCMouseCursor::Help:
                    // macOS 10.15+ has contextualMenuCursor but no help cursor
                    // Fall back to arrow cursor
                    cursor = [NSCursor arrowCursor];
                    break;

                case UCMouseCursor::NotAllowed:
                    cursor = [NSCursor operationNotAllowedCursor];
                    break;

                case UCMouseCursor::SizeAll:
                    // macOS doesn't have a size-all cursor
                    // Use openHandCursor as closest equivalent
                    cursor = [NSCursor openHandCursor];
                    break;

                case UCMouseCursor::SizeNS:
                    cursor = [NSCursor resizeUpDownCursor];
                    break;

                case UCMouseCursor::SizeWE:
                    cursor = [NSCursor resizeLeftRightCursor];
                    break;

                case UCMouseCursor::SizeNWSE:
                    // macOS doesn't have diagonal resize cursors by default
                    // Use a generic resize cursor if available, otherwise arrow
                    // On macOS 11+, we could use _windowResizeNorthWestSouthEastCursor
                    // but it's private API. Fall back to arrow.
                    cursor = [NSCursor arrowCursor];
                    break;

                case UCMouseCursor::SizeNESW:
                    // Same as above - no diagonal resize cursor available
                    cursor = [NSCursor arrowCursor];
                    break;

                case UCMouseCursor::Custom:
                    // Custom cursor not implemented - use arrow
                    cursor = [NSCursor arrowCursor];
                    break;

                default:
                    cursor = [NSCursor arrowCursor];
                    break;
            }

            // If transitioning from NoCursor, ensure cursor is visible
            if (currentMousePointer == UCMouseCursor::NoCursor) {
                [NSCursor unhide];
            }

            if (cursor) {
                [cursor set];
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

        // Trigger redraw
        Invalidate();
    }

    unsigned long UltraCanvasMacOSWindow::GetNativeHandle() const  {
        return (unsigned long)(__bridge_retained void*)nsWindow;
    };

    // ===== WINDOW DELEGATE CALLBACKS =====
    void UltraCanvasMacOSWindow::OnWindowWillClose() {
        std::cout << "UltraCanvas macOS: Window will close callback" << std::endl;
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

