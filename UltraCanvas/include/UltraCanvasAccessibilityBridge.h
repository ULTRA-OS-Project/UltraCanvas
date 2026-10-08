// include/UltraCanvasAccessibilityBridge.h
// What the platform accessibility bridges (AT-SPI on Linux, UI Automation on
// Windows) share: the element tree as a screen reader walks it, stable ids for
// its nodes, screen geometry, and the difference between two versions of a
// text. Applications do not use this header; elements describe themselves
// through UltraCanvasAccessibility.h.
// Version: 1.1.0
// Last Modified: 2026-10-08
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasAccessibility.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace UltraCanvas {

class UltraCanvasUIElement;
class UltraCanvasWindowBase;

namespace AccessibilityBridge {

// ===== TREE =====
// The application's windows, in registration order. Windows not created yet
// are left out.
std::vector<UltraCanvasWindowBase*> Windows();

// An element's children (a container's children; none otherwise).
std::vector<UltraCanvasUIElement*> Children(UltraCanvasUIElement* element);

// An element's parent in the tree: its container, else its window; null for
// a window (whose parent is the application).
UltraCanvasUIElement* Parent(UltraCanvasUIElement* element);

// The element as a window, or null.
UltraCanvasWindowBase* AsWindow(UltraCanvasUIElement* element);

// The window an element is shown in (the element itself for a window).
UltraCanvasWindowBase* WindowOf(UltraCanvasUIElement* element);

// Position among its parent's children (among the windows for a window), or -1.
int IndexInParent(UltraCanvasUIElement* element);

// The name to announce: the element's accessible name, a window's title.
std::string Name(UltraCanvasUIElement* element);

// True when the element is in a live window's tree. Used to refuse a stale
// id that outlived its element by a moment.
bool IsLive(UltraCanvasUIElement* element);

// ===== GEOMETRY =====
// Screen rectangles are in the platform's native screen pixels.
struct ScreenRect {
    int x = 0, y = 0, width = 0, height = 0;
    bool Contains(int px, int py) const { return px >= x && py >= y && px < x + width && py < y + height; }
};

// The element's rectangle on the screen.
ScreenRect ScreenBounds(UltraCanvasUIElement* element);

// A rectangle in a window's logical coordinates, on the screen.
ScreenRect WindowRectToScreen(UltraCanvasWindowBase* window, const Rect2Df& rect);

// A screen point in a window's logical coordinates.
Point2Df ScreenToWindow(UltraCanvasWindowBase* window, int x, int y);

// The deepest visible element under a screen point, starting at `root`
// (itself when no child is under the point), or null when the point is
// outside it.
UltraCanvasUIElement* HitTest(UltraCanvasUIElement* root, int x, int y);

// ===== IDS =====
// Small integer ids for elements, stable while the element lives. Ids are
// never reused, so a screen reader holding an old one gets "gone", not a
// different element.
class IdMap {
public:
    uint32_t IdOf(UltraCanvasUIElement* element);
    // The element with this id, or null when it is gone.
    UltraCanvasUIElement* ElementOf(uint32_t id) const;
    void Forget(UltraCanvasUIElement* element);
    void Clear();

private:
    std::unordered_map<UltraCanvasUIElement*, uint32_t> ids;
    std::unordered_map<uint32_t, UltraCanvasUIElement*> elements;
    uint32_t next = 1;
};

// ===== TEXT =====
// The text a screen reader reads from an element: its own IAccessibleText,
// else - for a text field or combo box that only reports a value text - a
// read-only view of that value (caret at its end). Null for a password field
// and for elements without text. The view is shared: valid until the next
// call, which is all a bridge's single request needs.
IAccessibleText* TextInterface(UltraCanvasUIElement* element);

// The text in [start, end) (characters; end < 0 = to the end).
std::string Substring(const std::string& utf8, int start, int end);

// The Unicode code point of the character at `offset`, or 0.
uint32_t CodePointAt(const std::string& utf8, int offset);

// What turned `before` into `after`: at character `start`, `removed` was
// replaced by `inserted`. False when they are equal.
bool Difference(const std::string& before, const std::string& after,
                int& start, std::string& removed, std::string& inserted);

// The character under a point in window coordinates, or -1. Elements report
// the nearest caret position, which over the right half of a character is
// the gap after it; a screen reader asks for the character itself.
int CharacterAtPoint(const IAccessibleText* text, const Point2Df& windowPoint);

// The rectangle covering characters [start, end) of a text, in window
// coordinates (empty when none of them is laid out). Looks at no more than
// `limit` characters.
Rect2Df RangeBounds(const IAccessibleText* text, int start, int end, int limit = 4096);

} // namespace AccessibilityBridge
} // namespace UltraCanvas
