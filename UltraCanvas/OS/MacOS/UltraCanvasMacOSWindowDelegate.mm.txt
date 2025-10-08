// OS/MacOS/UltraCanvasMacOSWindowDelegate.mm
// Window delegate implementation for UltraCanvas Framework
// Version: 1.0.0
// Last Modified: 2024-12-19
// Author: UltraCanvas Framework

#import <Cocoa/Cocoa.h>
#include "UltraCanvasMacOSWindow.h"

// ===== WINDOW DELEGATE CLASS =====
@interface UltraCanvasWindowDelegate : NSObject <NSWindowDelegate> {
    UltraCanvas::UltraCanvasMacOSWindow* ultraCanvasWindow;
}

@property (nonatomic, assign) UltraCanvas::UltraCanvasMacOSWindow* ultraCanvasWindow;

- (instancetype)initWithUltraCanvasWindow:(UltraCanvas::UltraCanvasMacOSWindow*)window;

// Window lifecycle
- (BOOL)windowShouldClose:(NSWindow*)sender;
- (void)windowWillClose:(NSNotification*)notification;
- (void)windowDidBecomeKey:(NSNotification*)notification;
- (void)windowDidResignKey:(NSNotification*)notification;
- (void)windowDidBecomeMain:(NSNotification*)notification;
- (void)windowDidResignMain:(NSNotification*)notification;

// Window state changes
- (void)windowDidResize:(NSNotification*)notification;
- (void)windowDidMove:(NSNotification*)notification;
- (void)windowDidMiniaturize:(NSNotification*)notification;
- (void)windowDidDeminiaturize:(NSNotification*)notification;
- (void)windowWillEnterFullScreen:(NSNotification*)notification;
- (void)windowDidEnterFullScreen:(NSNotification*)notification;
- (void)windowWillExitFullScreen:(NSNotification*)notification;
- (void)windowDidExitFullScreen:(NSNotification*)notification;

// Live resize
- (void)windowWillStartLiveResize:(NSNotification*)notification;
- (void)windowDidEndLiveResize:(NSNotification*)notification;

// Zoom (maximize)
- (BOOL)windowShouldZoom:(NSWindow*)window toFrame:(NSRect)newFrame;
- (void)windowWillZoom:(NSWindow*)window toFrame:(NSRect)newFrame;

@end

// ===== IMPLEMENTATION =====
@implementation UltraCanvasWindowDelegate

@synthesize ultraCanvasWindow;

- (instancetype)initWithUltraCanvasWindow:(UltraCanvas::UltraCanvasMacOSWindow*)window {
    self = [super init];
    if (self) {
        ultraCanvasWindow = window;
        NSLog(@"UltraCanvasWindowDelegate: Initialized for window: %p", window);
    }
    return self;
}

- (void)dealloc {
    NSLog(@"UltraCanvasWindowDelegate: Deallocating delegate");
    ultraCanvasWindow = nullptr;
    [super dealloc];
}

// ===== WINDOW LIFECYCLE =====
- (BOOL)windowShouldClose:(NSWindow*)sender {
    NSLog(@"UltraCanvasWindowDelegate: Window should close requested");
    
    if (ultraCanvasWindow) {
        // Call the UltraCanvas window's close handler
        ultraCanvasWindow->OnWindowWillClose();
        return YES;
    }
    
    return YES;
}

- (void)windowWillClose:(NSNotification*)notification {
    NSLog(@"UltraCanvasWindowDelegate: Window will close");
    
    if (ultraCanvasWindow) {
        ultraCanvasWindow->OnWindowWillClose();
    }
}

- (void)windowDidBecomeKey:(NSNotification*)notification {
    NSLog(@"UltraCanvasWindowDelegate: Window became key");
    
    if (ultraCanvasWindow) {
        ultraCanvasWindow->OnWindowDidBecomeKey();
    }
}

- (void)windowDidResignKey:(NSNotification*)notification {
    NSLog(@"UltraCanvasWindowDelegate: Window resigned key");
    
    if (ultraCanvasWindow) {
        ultraCanvasWindow->OnWindowDidResignKey();
    }
}

- (void)windowDidBecomeMain:(NSNotification*)notification {
    NSLog(@"UltraCanvasWindowDelegate: Window became main");
    
    if (ultraCanvasWindow) {
        ultraCanvasWindow->OnWindowDidBecomeKey();
    }
}

- (void)windowDidResignMain:(NSNotification*)notification {
    NSLog(@"UltraCanvasWindowDelegate: Window resigned main");
    
    if (ultraCanvasWindow) {
        ultraCanvasWindow->OnWindowDidResignKey();
    }
}

// ===== WINDOW STATE CHANGES =====
- (void)windowDidResize:(NSNotification*)notification {
    NSWindow* window = [notification object];
    NSRect frame = [window frame];
    
    NSLog(@"UltraCanvasWindowDelegate: Window resized to: %.0fx%.0f", 
          frame.size.width, frame.size.height);
    
    if (ultraCanvasWindow) {
        ultraCanvasWindow->OnWindowDidResize();
    }
}

- (void)windowDidMove:(NSNotification*)notification {
    NSWindow* window = [notification object];
    NSRect frame = [window frame];
    
    NSLog(@"UltraCanvasWindowDelegate: Window moved to: (%.0f, %.0f)", 
          frame.origin.x, frame.origin.y);
    
    if (ultraCanvasWindow) {
        ultraCanvasWindow->OnWindowDidMove();
    }
}

- (void)windowDidMiniaturize:(NSNotification*)notification {
    NSLog(@"UltraCanvasWindowDelegate: Window minimized");
    
    if (ultraCanvasWindow) {
        ultraCanvasWindow->OnWindowDidMiniaturize();
    }
}

- (void)windowDidDeminiaturize:(NSNotification*)notification {
    NSLog(@"UltraCanvasWindowDelegate: Window deminimized");
    
    if (ultraCanvasWindow) {
        ultraCanvasWindow->OnWindowDidDeminiaturize();
    }
}

- (void)windowWillEnterFullScreen:(NSNotification*)notification {
    NSLog(@"UltraCanvasWindowDelegate: Window will enter fullscreen");
}

- (void)windowDidEnterFullScreen:(NSNotification*)notification {
    NSLog(@"UltraCanvasWindowDelegate: Window entered fullscreen");
}

- (void)windowWillExitFullScreen:(NSNotification*)notification {
    NSLog(@"UltraCanvasWindowDelegate: Window will exit fullscreen");
}

- (void)windowDidExitFullScreen:(NSNotification*)notification {
    NSLog(@"UltraCanvasWindowDelegate: Window exited fullscreen");
}

// ===== LIVE RESIZE =====
- (void)windowWillStartLiveResize:(NSNotification*)notification {
    NSLog(@"UltraCanvasWindowDelegate: Live resize started");
}

- (void)windowDidEndLiveResize:(NSNotification*)notification {
    NSLog(@"UltraCanvasWindowDelegate: Live resize ended");
    
    if (ultraCanvasWindow) {
        ultraCanvasWindow->OnWindowDidResize();
    }
}

// ===== ZOOM (MAXIMIZE) =====
- (BOOL)windowShouldZoom:(NSWindow*)window toFrame:(NSRect)newFrame {
    NSLog(@"UltraCanvasWindowDelegate: Window should zoom to: (%.0f, %.0f, %.0fx%.0f)", 
          newFrame.origin.x, newFrame.origin.y, newFrame.size.width, newFrame.size.height);
    return YES;
}

- (void)windowWillZoom:(NSWindow*)window toFrame:(NSRect)newFrame {
    NSLog(@"UltraCanvasWindowDelegate: Window will zoom");
}

@end

// ===== C++ BRIDGE FUNCTIONS =====
namespace UltraCanvas {

void* CreateWindowDelegate(UltraCanvasMacOSWindow* window) {
    UltraCanvasWindowDelegate* delegate = [[UltraCanvasWindowDelegate alloc] 
                                         initWithUltraCanvasWindow:window];
    return delegate;
}

void DestroyWindowDelegate(void* delegate) {
    if (delegate) {
        UltraCanvasWindowDelegate* nsDelegate = (UltraCanvasWindowDelegate*)delegate;
        [nsDelegate release];
    }
}

void SetWindowDelegate(void* nsWindow, void* delegate) {
    if (nsWindow && delegate) {
        NSWindow* window = (NSWindow*)nsWindow;
        UltraCanvasWindowDelegate* windowDelegate = (UltraCanvasWindowDelegate*)delegate;
        [window setDelegate:windowDelegate];
    }
}

} // namespace UltraCanvas