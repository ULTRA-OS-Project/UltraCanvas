// OS/MacOS/UltraCanvasMacOSAccessibility.h
// The NSAccessibility bridge: VoiceOver and other macOS assistive technology
// read a window's elements through the window's content view.
// Version: 1.0.0
// Last Modified: 2026-10-08
// Author: UltraCanvas Framework
#pragma once

#ifdef __OBJC__
#import <Cocoa/Cocoa.h>
#endif

namespace UltraCanvas {

class UltraCanvasMacOSWindow;

namespace MacOSAccessibility {

#ifdef __OBJC__
// Called by the window's content view (UltraCanvasView). Each returns nil
// when there is nothing to say, so the view falls back to NSView's answer.
// The first call switches the accessibility events on.

// The window's top-level elements.
NSArray* WindowChildren(UltraCanvasMacOSWindow* window);
// The deepest element under a screen point (AppKit screen coordinates).
id HitTest(UltraCanvasMacOSWindow* window, NSPoint screenPoint);
// The window's focused element.
id FocusedElement(UltraCanvasMacOSWindow* window);
#endif

// Application shutdown: stops listening and drops every element object.
void Shutdown();

} // namespace MacOSAccessibility
} // namespace UltraCanvas
