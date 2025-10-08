// OS/MacOS/UltraCanvasMacOSWindow.cpp
// Complete macOS window implementation with all methods
// Version: 1.0.1
// Last Modified: 2024-12-19
// Author: UltraCanvas Framework

#include "UltraCanvasMacOSWindow.h"
#include "UltraCanvasMacOSApplication.h"
#include "UltraCanvasMacOSEventBridge.h"
#include "../../include/UltraCanvasApplication.h"

#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>
#import <QuartzCore/QuartzCore.h>

#include <iostream>
#include <cstring>

// Forward declaration for C++ bridge functions
namespace UltraCanvas {
    void* CreateCustomNSView(const Rect2D& frame, UltraCanvasMacOSWindow* window);
    void DestroyCustomNSView(void* view);
    void SetNSViewFrame(void* view, const Rect2D& frame);
    void SetNSViewNeedsDisplay(void* view, bool needsDisplay);
    void SetNSViewNeedsDisplayInRect(void* view, const Rect2D& rect);
    
    void* CreateWindowDelegate(UltraCanvasMacOSWindow* window);
    void DestroyWindowDelegate(void* delegate);
    void SetWindowDelegate(void* nsWindow, void* delegate);
}

namespace UltraCanvas {

// ===== CONSTRUCTOR & DESTRUCTOR =====
UltraCanvasMacOSWindow::UltraCanvasMacOSWindow(const WindowConfig& config)
    : UltraCanvasBaseWindow(config)
    , nsWindow(nullptr)
    , contentView(nullptr)
    , customView(nullptr)
    , cgContext(nullptr)
    , backBuffer(nullptr)
    , windowDelegate(nullptr)
    , isCustomViewInstalled(false)
    , needsDisplay(true)
    , owningThread(std::this_thread::get_id()) {

    std::cout << "UltraCanvas macOS: Window constructor started" << std::endl;
    
    if (!CreateNative(config)) {
        std::cerr << "UltraCanvas macOS: Failed to create native window" << std::endl;
    }
    
    std::cout << "UltraCanvas macOS: Window constructor completed successfully" << std::endl;
}

UltraCanvasMacOSWindow::~UltraCanvasMacOSWindow() {
    std::cout << "UltraCanvas macOS: Window destructor called" << std::endl;
    Destroy();
}

// ===== WINDOW CREATION =====
bool UltraCanvasMacOSWindow::CreateNative(const WindowConfig& config) {
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
        if (!CreateNSWindow(config)) {
            std::cerr << "UltraCanvas macOS: Failed to create NSWindow" << std::endl;
            return false;
        }

        if (!CreateCustomView()) {
            std::cerr << "UltraCanvas macOS: Failed to create custom view" << std::endl;
            [nsWindow release];
            nsWindow = nullptr;
            return false;
        }

        if (!CreateCoreGraphicsContext()) {
            std::cerr << "UltraCanvas macOS: Failed to create Core Graphics context" << std::endl;
            DestroyCustomNSView(customView);
            [nsWindow release];
            nsWindow = nullptr;
            return false;
        }

        try {
            renderContext = std::make_unique<MacOSRenderContext>(
                cgContext, config_.width, config_.height, true);
            std::cout << "UltraCanvas macOS: Render context created successfully" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "UltraCanvas macOS: Failed to create render context: " << e.what() << std::endl;
            CleanupCoreGraphics();
            DestroyCustomNSView(customView);
            [nsWindow release];
            nsWindow = nullptr;
            return false;
        }

        // Register window with application
        auto macOSApp = static_cast<UltraCanvasMacOSApplication*>(application);
        macOSApp->RegisterWindow(this, nsWindow);

        _created = true;
        std::cout << "UltraCanvas macOS: Window created successfully!" << std::endl;
        return true;
    }
}

bool UltraCanvasMacOSWindow::CreateNSWindow(const WindowConfig& config) {
    @autoreleasepool {
        // Calculate window frame
        NSRect windowFrame = NSMakeRect(
            config.x >= 0 ? config.x : 100,
            config.y >= 0 ? config.y : 100,
            config.width,
            config.height
        );

        // Get window style mask
        NSWindowStyleMask styleMask = GetNSWindowStyleMask(config);

        // Create NSWindow
        nsWindow = [[NSWindow alloc] initWithContentRect:windowFrame
                                               styleMask:styleMask
                                                 backing:NSBackingStoreBuffered
                                                   defer:NO];

        if (!nsWindow) {
            std::cerr << "UltraCanvas macOS: Failed to create NSWindow instance" << std::endl;
            return false;
        }

        // Configure window properties
        ApplyWindowConfiguration(config);

        // Setup window delegate
        SetupWindowDelegate();

        // Get content view
        contentView = [nsWindow contentView];

        std::cout << "UltraCanvas macOS: NSWindow created successfully" << std::endl;
        return true;
    }
}

bool UltraCanvasMacOSWindow::CreateCustomView() {
    Rect2D frame(0, 0, config_.width, config_.height);
    customView = static_cast<NSView*>(CreateCustomNSView(frame, this));

    if (!customView) {
        std::cerr << "UltraCanvas macOS: Failed to create custom view" << std::endl;
        return false;
    }

    // Add custom view to content view
    [contentView addSubview:customView];
    isCustomViewInstalled = true;

    std::cout << "UltraCanvas macOS: Custom view created and installed" << std::endl;
    return true;
}

bool UltraCanvasMacOSWindow::CreateCoreGraphicsContext() {
    // The actual CGContext will be provided during drawing
    // For now, we'll set it to nullptr and update it during rendering
    cgContext = nullptr;
    
    // Create back buffer for double buffering
    CreateBackBuffer();
    
    return true;
}

void UltraCanvasMacOSWindow::SetupWindowDelegate() {
    // Create window delegate
    windowDelegate = CreateWindowDelegate(this);
    
    if (windowDelegate) {
        SetWindowDelegate(nsWindow, windowDelegate);
        std::cout << "UltraCanvas macOS: Window delegate created and set" << std::endl;
    }
}

void UltraCanvasMacOSWindow::ApplyWindowConfiguration(const WindowConfig& config) {
    if (!nsWindow) return;

    @autoreleasepool {
        // Set window title
        NSString* title = [NSString stringWithUTF8String:config.title.c_str()];
        [nsWindow setTitle:title];

        // Set minimum size
        if (config.minWidth > 0 && config.minHeight > 0) {
            NSSize minSize = NSMakeSize(config.minWidth, config.minHeight);
            [nsWindow setMinSize:minSize];
        }

        // Set maximum size
        if (config.maxWidth > 0 && config.maxHeight > 0) {
            NSSize maxSize = NSMakeSize(config.maxWidth, config.maxHeight);
            [nsWindow setMaxSize:maxSize];
        }

        // Set background color
        NSColor* bgColor = [NSColor colorWithRed:config.backgroundColor.r
                                           green:config.backgroundColor.g
                                            blue:config.backgroundColor.b
                                           alpha:config.backgroundColor.a];
        [nsWindow setBackgroundColor:bgColor];

        // Set opacity
        [nsWindow setAlphaValue:config.opacity];

        // Set always on top
        if (config.alwaysOnTop) {
            [nsWindow setLevel:NSFloatingWindowLevel];
        }

        // Set modal
        if (config.modal && config.parentWindow) {
            // Handle modal windows if needed
        }
    }
}

NSWindowStyleMask UltraCanvasMacOSWindow::GetNSWindowStyleMask(const WindowConfig& config) {
    NSWindowStyleMask styleMask = 0;

    switch (config.type) {
        case WindowType::Standard:
            styleMask = NSWindowStyleMaskTitled;
            if (config.closable) styleMask |= NSWindowStyleMaskClosable;
            if (config.minimizable) styleMask |= NSWindowStyleMaskMiniaturizable;
            if (config.resizable) styleMask |= NSWindowStyleMaskResizable;
            break;

        case WindowType::Dialog:
            styleMask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable;
            break;

        case WindowType::Popup:
            styleMask = NSWindowStyleMaskBorderless;
            break;

        case WindowType::Tool:
            styleMask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | 
                       NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskUtilityWindow;
            break;

        case WindowType::Splash:
            styleMask = NSWindowStyleMaskBorderless;
            break;

        case WindowType::Fullscreen:
            styleMask = NSWindowStyleMaskBorderless | NSWindowStyleMaskFullScreen;
            break;

        case WindowType::Borderless:
            styleMask = NSWindowStyleMaskBorderless;
            break;

        case WindowType::Overlay:
            styleMask = NSWindowStyleMaskBorderless;
            break;
    }

    return styleMask;
}

// ===== INHERITED FROM BASE WINDOW =====
void UltraCanvasMacOSWindow::Destroy() {
    if (!_created) return;

    std::cout << "UltraCanvas macOS: Destroying window..." << std::endl;

    SafeExecute([this]() {
        // Unregister from application
        auto application = UltraCanvasApplication::GetInstance();
        if (application) {
            auto macOSApp = static_cast<UltraCanvasMacOSApplication*>(application);
            macOSApp->UnregisterWindow(nsWindow);
        }

        // Cleanup Core Graphics resources
        CleanupCoreGraphics();

        // Cleanup custom view
        if (customView) {
            DestroyCustomNSView(customView);
            customView = nullptr;
            isCustomViewInstalled = false;
        }

        // Cleanup Cocoa resources
        CleanupCocoa();

        _created = false;
        _visible = false;
    });

    std::cout << "UltraCanvas macOS: Window destroyed" << std::endl;
}

void UltraCanvasMacOSWindow::Show() {
    if (!_created || _visible) return;

    std::cout << "UltraCanvas macOS: Showing window..." << std::endl;

    SafeExecute([this]() {
        [nsWindow makeKeyAndOrderFront:nil];
        [nsWindow orderFrontRegardless];
        _visible = true;
    });

    if (onWindowShow) {
        onWindowShow();
    }
}

void UltraCanvasMacOSWindow::Hide() {
    if (!_created || !_visible) return;

    std::cout << "UltraCanvas macOS: Hiding window..." << std::endl;

    SafeExecute([this]() {
        [nsWindow orderOut:nil];
        _visible = false;
    });

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

    SafeExecute([this]() {
        [nsWindow close];
    });
}

void UltraCanvasMacOSWindow::SetWindowTitle(const std::string& title) {
    config_.title = title;

    if (_created) {
        SafeExecute([this, title]() {
            NSString* nsTitle = [NSString stringWithUTF8String:title.c_str()];
            [nsWindow setTitle:nsTitle];
        });
    }
}

void UltraCanvasMacOSWindow::SetWindowSize(int width, int height) {
    config_.width = width;
    config_.height = height;

    if (_created) {
        SafeExecute([this, width, height]() {
            NSSize newSize = NSMakeSize(width, height);
            [nsWindow setContentSize:newSize];
            UpdateContentViewSize();
            UpdateBackBuffer();
        });
    }

    UltraCanvasBaseWindow::SetSize(width, height);
}

void UltraCanvasMacOSWindow::SetWindowPosition(int x, int y) {
    config_.x = x;
    config_.y = y;

    if (_created) {
        SafeExecute([this, x, y]() {
            // Convert coordinates (macOS has origin at bottom-left)
            NSScreen* screen = [nsWindow screen] ?: [NSScreen mainScreen];
            CGFloat screenHeight = [screen frame].size.height;
            CGFloat windowHeight = [nsWindow frame].size.height;
            CGFloat adjustedY = screenHeight - y - windowHeight;
            
            NSPoint newOrigin = NSMakePoint(x, adjustedY);
            [nsWindow setFrameOrigin:newOrigin];
        });
    }
}

void UltraCanvasMacOSWindow::SetResizable(bool resizable) {
    config_.resizable = resizable;

    if (_created) {
        SafeExecute([this, resizable]() {
            NSWindowStyleMask currentMask = [nsWindow styleMask];
            if (resizable) {
                currentMask |= NSWindowStyleMaskResizable;
            } else {
                currentMask &= ~NSWindowStyleMaskResizable;
            }
            [nsWindow setStyleMask:currentMask];
        });
    }
}

void UltraCanvasMacOSWindow::Minimize() {
    if (!_created) return;

    SafeExecute([this]() {
        [nsWindow miniaturize:nil];
    });

    if (onWindowMinimize) {
        onWindowMinimize();
    }
}

void UltraCanvasMacOSWindow::Maximize() {
    if (!_created) return;

    SafeExecute([this]() {
        if (!([nsWindow styleMask] & NSWindowStyleMaskFullScreen)) {
            [nsWindow zoom:nil];
        }
    });

    if (onWindowMaximize) {
        onWindowMaximize();
    }
}

void UltraCanvasMacOSWindow::Restore() {
    if (!_created) return;

    SafeExecute([this]() {
        if ([nsWindow isMiniaturized]) {
            [nsWindow deminiaturize:nil];
        } else if ([nsWindow isZoomed]) {
            [nsWindow zoom:nil];
        }
    });

    if (onWindowRestore) {
        onWindowRestore();
    }
}

void UltraCanvasMacOSWindow::SetFullscreen(bool fullscreen) {
    if (!_created) return;

    SafeExecute([this, fullscreen]() {
        BOOL isCurrentlyFullscreen = ([nsWindow styleMask] & NSWindowStyleMaskFullScreen) != 0;
        
        if (fullscreen != isCurrentlyFullscreen) {
            [nsWindow toggleFullScreen:nil];
        }
    });
}

void UltraCanvasMacOSWindow::Flush() {
    if (!_created) return;

    SafeExecute([this]() {
        [nsWindow flushWindow];
        
        if (renderContext && renderContext->IsDoubleBufferingEnabled()) {
            renderContext->Flush();
        }
    });
}

unsigned long UltraCanvasMacOSWindow::GetNativeHandle() const {
    return reinterpret_cast<unsigned long>(nsWindow);
}

// ===== EVENT HANDLING =====
bool UltraCanvasMacOSWindow::HandleNSEvent(NSEvent* nsEvent) {
    if (!nsEvent) return false;

    NSEventType eventType = [nsEvent type];

    // Route to specific handlers
    switch (eventType) {
        case NSEventTypeKeyDown:
        case NSEventTypeKeyUp:
        case NSEventTypeFlagsChanged:
            return HandleKeyEvent(nsEvent);

        case NSEventTypeLeftMouseDown:
        case NSEventTypeLeftMouseUp:
        case NSEventTypeRightMouseDown:
        case NSEventTypeRightMouseUp:
        case NSEventTypeOtherMouseDown:
        case NSEventTypeOtherMouseUp:
        case NSEventTypeLeftMouseDragged:
        case NSEventTypeRightMouseDragged:
        case NSEventTypeOtherMouseDragged:
        case NSEventTypeMouseMoved:
        case NSEventTypeMouseEntered:
        case NSEventTypeMouseExited:
        case NSEventTypeScrollWheel:
            return HandleMouseEvent(nsEvent);

        default:
            return HandleWindowEvent(nsEvent);
    }
}

bool UltraCanvasMacOSWindow::HandleKeyEvent(NSEvent* nsEvent) {
    // Convert to UCEvent and dispatch
    UCEvent ucEvent = MacOSEventBridge::ConvertNSEventToUCEvent(nsEvent, this);
    
    if (ucEvent.type != UCEventType::Unknown) {
        return HandleEvent(ucEvent);
    }
    
    return false;
}

bool UltraCanvasMacOSWindow::HandleMouseEvent(NSEvent* nsEvent) {
    // Convert to UCEvent and dispatch
    UCEvent ucEvent = MacOSEventBridge::ConvertNSEventToUCEvent(nsEvent, this);
    
    if (ucEvent.type != UCEventType::Unknown) {
        return HandleEvent(ucEvent);
    }
    
    return false;
}

bool UltraCanvasMacOSWindow::HandleWindowEvent(NSEvent* nsEvent) {
    // Handle window-specific events
    return false;
}

// ===== DRAWING SYSTEM =====
void UltraCanvasMacOSWindow::OnPaint() {
    if (!renderContext) return;
    
    try {
        // Clear the window
        renderContext->Clear(config_.backgroundColor);
        
        // Call the user-defined paint handler
        if (onWindowPaint) {
            onWindowPaint();
        }
        
        // Draw all child elements
        if (HasChildren()) {
            for (auto& child : GetChildren()) {
                if (child && child->IsVisible()) {
                    child->Render();
                }
            }
        }
        
        // Flush the rendering
        renderContext->Flush();
        
    } catch (const std::exception& e) {
        std::cerr << "UltraCanvas macOS: Exception during paint: " << e.what() << std::endl;
    }
}

void UltraCanvasMacOSWindow::InvalidateRect(const Rect2D& rect) {
    if (customView) {
        SetNSViewNeedsDisplayInRect(customView, rect);
    }
}

void UltraCanvasMacOSWindow::SetNeedsDisplay(bool needsDisplay) {
    this->needsDisplay = needsDisplay;
    
    if (customView) {
        SetNSViewNeedsDisplay(customView, needsDisplay);
    }
}

// ===== GRAPHICS CONTEXT MANAGEMENT =====
void UltraCanvasMacOSWindow::UpdateGraphicsContext() {
    // The CGContext will be updated during the draw cycle
    if (renderContext) {
        renderContext->SetCGContext(cgContext);
    }
}

void UltraCanvasMacOSWindow::CreateBackBuffer() {
    if (!cgContext) return;

    SafeExecute([this]() {
        CGSize layerSize = CGSizeMake(config_.width, config_.height);
        backBuffer = CGLayerCreateWithContext(cgContext, layerSize, nullptr);
    });
}

void UltraCanvasMacOSWindow::DestroyBackBuffer() {
    if (backBuffer) {
        CGLayerRelease(backBuffer);
        backBuffer = nullptr;
    }
}

// ===== WINDOW DELEGATE CALLBACKS =====
void UltraCanvasMacOSWindow::OnWindowWillClose() {
    if (onWindowClosing) {
        onWindowClosing();
    }
}

void UltraCanvasMacOSWindow::OnWindowDidResize() {
    NSRect contentRect = [nsWindow contentRectForFrameRect:[nsWindow frame]];
    int newWidth = static_cast<int>(contentRect.size.width);
    int newHeight = static_cast<int>(contentRect.size.height);

    config_.width = newWidth;
    config_.height = newHeight;

    UpdateContentViewSize();
    UpdateBackBuffer();

    if (onWindowResize) {
        onWindowResize(newWidth, newHeight);
    }

    SetSize(newWidth, newHeight);
    SetNeedsDisplay();
}

void UltraCanvasMacOSWindow::OnWindowDidMove() {
    NSRect frame = [nsWindow frame];
    NSScreen* screen = [nsWindow screen] ?: [NSScreen mainScreen];
    CGFloat screenHeight = [screen frame].size.height;
    
    // Convert from Cocoa coordinates to UltraCanvas coordinates
    int newX = static_cast<int>(frame.origin.x);
    int newY = static_cast<int>(screenHeight - frame.origin.y - frame.size.height);

    config_.x = newX;
    config_.y = newY;

    if (onWindowMove) {
        onWindowMove(newX, newY);
    }
}

void UltraCanvasMacOSWindow::OnWindowDidBecomeKey() {
    _focused = true;
    
    auto application = UltraCanvasApplication::GetInstance();
    if (application) {
        auto macOSApp = static_cast<UltraCanvasMacOSApplication*>(application);
        macOSApp->SetKeyWindow(this);
    }

    if (onWindowFocus) {
        onWindowFocus();
    }
}

void UltraCanvasMacOSWindow::OnWindowDidResignKey() {
    _focused = false;

    if (onWindowBlur) {
        onWindowBlur();
    }
}

void UltraCanvasMacOSWindow::OnWindowDidMiniaturize() {
    if (onWindowMinimize) {
        onWindowMinimize();
    }
}

void UltraCanvasMacOSWindow::OnWindowDidDeminiaturize() {
    if (onWindowRestore) {
        onWindowRestore();
    }
}

// ===== HELPER METHODS =====
void UltraCanvasMacOSWindow::UpdateContentViewSize() {
    if (customView) {
        Rect2D newFrame(0, 0, config_.width, config_.height);
        SetNSViewFrame(customView, newFrame);
    }
}

void UltraCanvasMacOSWindow::UpdateBackBuffer() {
    DestroyBackBuffer();
    CreateBackBuffer();
    
    if (renderContext) {
        renderContext->EnableDoubleBuffering(true);
    }
}

CGRect UltraCanvasMacOSWindow::GetWindowContentRect() {
    if (!nsWindow) return CGRectZero;
    
    NSRect contentRect = [nsWindow contentRectForFrameRect:[nsWindow frame]];
    return NSRectToCGRect(contentRect);
}

CGRect UltraCanvasMacOSWindow::CocoaRectToCGRect(const Rect2D& rect) {
    return CGRectMake(rect.x, rect.y, rect.width, rect.height);
}

Rect2D UltraCanvasMacOSWindow::CGRectToUltraCanvasRect(CGRect cgRect) {
    return Rect2D(cgRect.origin.x, cgRect.origin.y, cgRect.size.width, cgRect.size.height);
}

// ===== CLEANUP =====
void UltraCanvasMacOSWindow::CleanupCoreGraphics() {
    DestroyBackBuffer();
    
    if (renderContext) {
        renderContext.reset();
    }
    
    cgContext = nullptr;
}

void UltraCanvasMacOSWindow::CleanupCocoa() {
    if (windowDelegate) {
        DestroyWindowDelegate(windowDelegate);
        windowDelegate = nullptr;
    }
    
    if (nsWindow) {
        [nsWindow release];
        nsWindow = nullptr;
    }
    
    contentView = nullptr;
}

} // namespace UltraCanvas