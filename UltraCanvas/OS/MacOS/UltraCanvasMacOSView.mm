// OS/MacOS/UltraCanvasMacOSView.mm
// Custom NSView implementation for UltraCanvas Framework
// Version: 1.0.0
// Last Modified: 2024-12-19
// Author: UltraCanvas Framework

#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>
#import <QuartzCore/QuartzCore.h>

#include "UltraCanvasMacOSWindow.h"
#include "UltraCanvasMacOSApplication.h"

// ===== CUSTOM NSVIEW FOR ULTRACANVAS =====
@interface UltraCanvasNSView : NSView {
    UltraCanvas::UltraCanvasMacOSWindow* ultraCanvasWindow;
    BOOL trackingAreaInstalled;
    NSTrackingArea* mouseTrackingArea;
    BOOL acceptsFirstResponder;
}

@property (nonatomic, assign) UltraCanvas::UltraCanvasMacOSWindow* ultraCanvasWindow;

- (instancetype)initWithFrame:(NSRect)frameRect ultraCanvasWindow:(UltraCanvas::UltraCanvasMacOSWindow*)window;
- (void)setupTrackingArea;
- (void)removeTrackingArea;

// Drawing
- (void)drawRect:(NSRect)dirtyRect;
- (BOOL)isOpaque;
- (BOOL)wantsLayer;
- (CALayer*)makeBackingLayer;

// Event handling
- (BOOL)acceptsFirstResponder;
- (BOOL)becomeFirstResponder;
- (BOOL)resignFirstResponder;

// Mouse events
- (void)mouseDown:(NSEvent*)event;
- (void)mouseUp:(NSEvent*)event;
- (void)mouseMoved:(NSEvent*)event;
- (void)mouseDragged:(NSEvent*)event;
- (void)rightMouseDown:(NSEvent*)event;
- (void)rightMouseUp:(NSEvent*)event;
- (void)rightMouseDragged:(NSEvent*)event;
- (void)otherMouseDown:(NSEvent*)event;
- (void)otherMouseUp:(NSEvent*)event;
- (void)otherMouseDragged:(NSEvent*)event;
- (void)scrollWheel:(NSEvent*)event;
- (void)mouseEntered:(NSEvent*)event;
- (void)mouseExited:(NSEvent*)event;

// Keyboard events
- (void)keyDown:(NSEvent*)event;
- (void)keyUp:(NSEvent*)event;
- (void)flagsChanged:(NSEvent*)event;

// Window events
- (void)viewDidMoveToWindow;
- (void)viewWillMoveToWindow:(NSWindow*)newWindow;
- (void)viewDidEndLiveResize;

@end

// ===== IMPLEMENTATION =====
@implementation UltraCanvasNSView

@synthesize ultraCanvasWindow;

- (instancetype)initWithFrame:(NSRect)frameRect ultraCanvasWindow:(UltraCanvas::UltraCanvasMacOSWindow*)window {
    self = [super initWithFrame:frameRect];
    if (self) {
        ultraCanvasWindow = window;
        trackingAreaInstalled = NO;
        mouseTrackingArea = nil;
        acceptsFirstResponder = YES;
        
        // Enable layer-backed view for better performance
        [self setWantsLayer:YES];
        
        NSLog(@"UltraCanvasNSView: Initialized with frame (%.0f, %.0f, %.0f, %.0f)", 
              frameRect.origin.x, frameRect.origin.y, frameRect.size.width, frameRect.size.height);
    }
    return self;
}

- (void)dealloc {
    NSLog(@"UltraCanvasNSView: Deallocating view");
    [self removeTrackingArea];
    [super dealloc];
}

// ===== TRACKING AREA MANAGEMENT =====
- (void)setupTrackingArea {
    if (trackingAreaInstalled) {
        [self removeTrackingArea];
    }
    
    NSTrackingAreaOptions trackingOptions = NSTrackingMouseEnteredAndExited | 
                                          NSTrackingMouseMoved | 
                                          NSTrackingActiveInKeyWindow |
                                          NSTrackingInVisibleRect;
    
    mouseTrackingArea = [[NSTrackingArea alloc] initWithRect:[self bounds] 
                                                    options:trackingOptions 
                                                      owner:self 
                                                   userInfo:nil];
    
    [self addTrackingArea:mouseTrackingArea];
    trackingAreaInstalled = YES;
    
    NSLog(@"UltraCanvasNSView: Tracking area installed");
}

- (void)removeTrackingArea {
    if (trackingAreaInstalled && mouseTrackingArea) {
        [self removeTrackingArea:mouseTrackingArea];
        [mouseTrackingArea release];
        mouseTrackingArea = nil;
        trackingAreaInstalled = NO;
        
        NSLog(@"UltraCanvasNSView: Tracking area removed");
    }
}

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    [self setupTrackingArea];
}

// ===== DRAWING METHODS =====
- (void)drawRect:(NSRect)dirtyRect {
    if (!ultraCanvasWindow) {
        // Fill with default background if no UltraCanvas window
        [[NSColor windowBackgroundColor] setFill];
        NSRectFill(dirtyRect);
        return;
    }
    
    // Get the Core Graphics context
    CGContextRef context = [[NSGraphicsContext currentContext] CGContext];
    
    if (!context) {
        NSLog(@"UltraCanvasNSView: Failed to get CGContext");
        return;
    }
    
    // Clear the dirty rect
    CGContextClearRect(context, NSRectToCGRect(dirtyRect));
    
    // Flip coordinate system to match UltraCanvas (Y-axis up)
    CGContextSaveGState(context);
    CGContextTranslateCTM(context, 0, [self bounds].size.height);
    CGContextScaleCTM(context, 1.0, -1.0);
    
    try {
        // Update the UltraCanvas window's graphics context
        ultraCanvasWindow->SetCGContext(context);
        
        // Trigger UltraCanvas rendering
        UltraCanvas::Rect2D updateRect(
            dirtyRect.origin.x,
            [self bounds].size.height - dirtyRect.origin.y - dirtyRect.size.height,
            dirtyRect.size.width,
            dirtyRect.size.height
        );
        
        // Call UltraCanvas window's render method
        ultraCanvasWindow->OnPaint();
        
    } catch (const std::exception& e) {
        NSLog(@"UltraCanvasNSView: Exception during rendering: %s", e.what());
    }
    
    CGContextRestoreGState(context);
}

- (BOOL)isOpaque {
    return YES;  // For better performance
}

- (BOOL)wantsLayer {
    return YES;  // Enable layer-backed view
}

- (CALayer*)makeBackingLayer {
    CALayer* layer = [CALayer layer];
    layer.opaque = YES;
    return layer;
}

// ===== RESPONDER METHODS =====
- (BOOL)acceptsFirstResponder {
    return acceptsFirstResponder;
}

- (BOOL)becomeFirstResponder {
    NSLog(@"UltraCanvasNSView: Became first responder");
    return [super becomeFirstResponder];
}

- (BOOL)resignFirstResponder {
    NSLog(@"UltraCanvasNSView: Resigned first responder");
    return [super resignFirstResponder];
}

// ===== MOUSE EVENT HANDLING =====
- (void)mouseDown:(NSEvent*)event {
    if (ultraCanvasWindow) {
        ultraCanvasWindow->HandleNSEvent(event);
    }
}

- (void)mouseUp:(NSEvent*)event {
    if (ultraCanvasWindow) {
        ultraCanvasWindow->HandleNSEvent(event);
    }
}

- (void)mouseMoved:(NSEvent*)event {
    if (ultraCanvasWindow) {
        ultraCanvasWindow->HandleNSEvent(event);
    }
}

- (void)mouseDragged:(NSEvent*)event {
    if (ultraCanvasWindow) {
        ultraCanvasWindow->HandleNSEvent(event);
    }
}

- (void)rightMouseDown:(NSEvent*)event {
    if (ultraCanvasWindow) {
        ultraCanvasWindow->HandleNSEvent(event);
    }
}

- (void)rightMouseUp:(NSEvent*)event {
    if (ultraCanvasWindow) {
        ultraCanvasWindow->HandleNSEvent(event);
    }
}

- (void)rightMouseDragged:(NSEvent*)event {
    if (ultraCanvasWindow) {
        ultraCanvasWindow->HandleNSEvent(event);
    }
}

- (void)otherMouseDown:(NSEvent*)event {
    if (ultraCanvasWindow) {
        ultraCanvasWindow->HandleNSEvent(event);
    }
}

- (void)otherMouseUp:(NSEvent*)event {
    if (ultraCanvasWindow) {
        ultraCanvasWindow->HandleNSEvent(event);
    }
}

- (void)otherMouseDragged:(NSEvent*)event {
    if (ultraCanvasWindow) {
        ultraCanvasWindow->HandleNSEvent(event);
    }
}

- (void)scrollWheel:(NSEvent*)event {
    if (ultraCanvasWindow) {
        ultraCanvasWindow->HandleNSEvent(event);
    }
}

- (void)mouseEntered:(NSEvent*)event {
    if (ultraCanvasWindow) {
        ultraCanvasWindow->HandleNSEvent(event);
    }
}

- (void)mouseExited:(NSEvent*)event {
    if (ultraCanvasWindow) {
        ultraCanvasWindow->HandleNSEvent(event);
    }
}

// ===== KEYBOARD EVENT HANDLING =====
- (void)keyDown:(NSEvent*)event {
    if (ultraCanvasWindow) {
        ultraCanvasWindow->HandleNSEvent(event);
    }
}

- (void)keyUp:(NSEvent*)event {
    if (ultraCanvasWindow) {
        ultraCanvasWindow->HandleNSEvent(event);
    }
}

- (void)flagsChanged:(NSEvent*)event {
    if (ultraCanvasWindow) {
        ultraCanvasWindow->HandleNSEvent(event);
    }
}

// ===== WINDOW EVENT HANDLING =====
- (void)viewDidMoveToWindow {
    [super viewDidMoveToWindow];
    
    if ([self window]) {
        [self setupTrackingArea];
        NSLog(@"UltraCanvasNSView: Moved to window");
    } else {
        [self removeTrackingArea];
        NSLog(@"UltraCanvasNSView: Removed from window");
    }
}

- (void)viewWillMoveToWindow:(NSWindow*)newWindow {
    [super viewWillMoveToWindow:newWindow];
    
    if (!newWindow) {
        [self removeTrackingArea];
    }
}

- (void)viewDidEndLiveResize {
    [super viewDidEndLiveResize];
    
    if (ultraCanvasWindow) {
        NSRect bounds = [self bounds];
        ultraCanvasWindow->OnWindowDidResize();
        
        NSLog(@"UltraCanvasNSView: Live resize ended - new size: (%.0f, %.0f)", 
              bounds.size.width, bounds.size.height);
    }
}

@end

// ===== C++ BRIDGE FUNCTIONS =====
namespace UltraCanvas {

void* CreateCustomNSView(const Rect2D& frame, UltraCanvasMacOSWindow* window) {
    NSRect nsFrame = NSMakeRect(frame.x, frame.y, frame.width, frame.height);
    UltraCanvasNSView* view = [[UltraCanvasNSView alloc] initWithFrame:nsFrame 
                                                      ultraCanvasWindow:window];
    return view;
}

void DestroyCustomNSView(void* view) {
    if (view) {
        UltraCanvasNSView* nsView = (UltraCanvasNSView*)view;
        [nsView release];
    }
}

void SetNSViewFrame(void* view, const Rect2D& frame) {
    if (view) {
        UltraCanvasNSView* nsView = (UltraCanvasNSView*)view;
        NSRect nsFrame = NSMakeRect(frame.x, frame.y, frame.width, frame.height);
        [nsView setFrame:nsFrame];
    }
}

void SetNSViewNeedsDisplay(void* view, bool needsDisplay) {
    if (view) {
        UltraCanvasNSView* nsView = (UltraCanvasNSView*)view;
        [nsView setNeedsDisplay:needsDisplay];
    }
}

void SetNSViewNeedsDisplayInRect(void* view, const Rect2D& rect) {
    if (view) {
        UltraCanvasNSView* nsView = (UltraCanvasNSView*)view;
        NSRect nsRect = NSMakeRect(rect.x, rect.y, rect.width, rect.height);
        [nsView setNeedsDisplayInRect:nsRect];
    }
}

} // namespace UltraCanvas