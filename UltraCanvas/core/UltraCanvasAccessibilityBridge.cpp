// core/UltraCanvasAccessibilityBridge.cpp
// The tree, ids, geometry and text helpers the platform accessibility bridges
// share (see UltraCanvasAccessibilityBridge.h).
// Version: 1.0.0
// Last Modified: 2026-10-01
// Author: UltraCanvas Framework

#include "UltraCanvasAccessibilityBridge.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasWindow.h"

#include <algorithm>
#include <cmath>

namespace UltraCanvas {
namespace AccessibilityBridge {

// ===== TREE =====

std::vector<UltraCanvasWindowBase*> Windows() {
    std::vector<UltraCanvasWindowBase*> out;
    auto* app = UltraCanvasApplication::GetInstance();
    if (!app) return out;
    for (const auto& window : app->GetWindows()) {
        if (window && window->IsCreated()) out.push_back(window.get());
    }
    return out;
}

std::vector<UltraCanvasUIElement*> Children(UltraCanvasUIElement* element) {
    std::vector<UltraCanvasUIElement*> out;
    if (auto* container = dynamic_cast<UltraCanvasContainer*>(element)) {
        for (const auto& child : container->GetChildren()) {
            if (child) out.push_back(child.get());
        }
    }
    return out;
}

UltraCanvasWindowBase* AsWindow(UltraCanvasUIElement* element) {
    return dynamic_cast<UltraCanvasWindowBase*>(element);
}

UltraCanvasUIElement* Parent(UltraCanvasUIElement* element) {
    if (!element || AsWindow(element)) return nullptr;
    if (UltraCanvasUIElement* container = element->GetParentContainer()) return container;
    return element->GetWindow();
}

UltraCanvasWindowBase* WindowOf(UltraCanvasUIElement* element) {
    if (!element) return nullptr;
    if (auto* window = AsWindow(element)) return window;
    return element->GetWindow();
}

int IndexInParent(UltraCanvasUIElement* element) {
    if (!element) return -1;
    if (AsWindow(element)) {
        const auto windows = Windows();
        for (size_t i = 0; i < windows.size(); i++) {
            if (windows[i] == element) return static_cast<int>(i);
        }
        return -1;
    }
    const auto siblings = Children(Parent(element));
    for (size_t i = 0; i < siblings.size(); i++) {
        if (siblings[i] == element) return static_cast<int>(i);
    }
    return -1;
}

std::string Name(UltraCanvasUIElement* element) {
    if (!element) return "";
    std::string name = element->GetAccessibleName();
    if (name.empty()) {
        if (auto* window = AsWindow(element)) name = window->GetWindowTitle();
    }
    return name;
}

bool IsLive(UltraCanvasUIElement* element) {
    if (!element) return false;
    UltraCanvasUIElement* top = element;
    for (int depth = 0; depth < 512; depth++) {
        UltraCanvasUIElement* up = Parent(top);
        if (!up) break;
        top = up;
    }
    for (UltraCanvasWindowBase* window : Windows()) {
        if (window == top) return true;
    }
    return false;
}

// ===== GEOMETRY =====

namespace {

float ScaleOf(UltraCanvasWindowBase* window) {
    const float scale = window ? window->GetDeviceScale() : 1.0f;
    return scale > 0.0f ? scale : 1.0f;
}

} // namespace

ScreenRect WindowRectToScreen(UltraCanvasWindowBase* window, const Rect2Df& rect) {
    ScreenRect out;
    if (!window) return out;
    int wx = 0, wy = 0;
    window->GetContentScreenOrigin(wx, wy);
    const float scale = ScaleOf(window);
    out.x = wx + static_cast<int>(std::lround(rect.x * scale));
    out.y = wy + static_cast<int>(std::lround(rect.y * scale));
    out.width = static_cast<int>(std::lround(rect.width * scale));
    out.height = static_cast<int>(std::lround(rect.height * scale));
    return out;
}

Point2Df ScreenToWindow(UltraCanvasWindowBase* window, int x, int y) {
    if (!window) return Point2Df(0, 0);
    int wx = 0, wy = 0;
    window->GetContentScreenOrigin(wx, wy);
    const float scale = ScaleOf(window);
    return Point2Df(static_cast<float>(x - wx) / scale, static_cast<float>(y - wy) / scale);
}

ScreenRect ScreenBounds(UltraCanvasUIElement* element) {
    ScreenRect out;
    if (!element) return out;
    if (auto* window = AsWindow(element)) {
        // The window's content area: what its elements are positioned in.
        window->GetContentScreenOrigin(out.x, out.y);
        window->GetNativeWindowSize(out.width, out.height);
        return out;
    }
    return WindowRectToScreen(element->GetWindow(), element->GetBoundsInWindow());
}

UltraCanvasUIElement* HitTest(UltraCanvasUIElement* root, int x, int y) {
    if (!root || !root->IsVisible() || !ScreenBounds(root).Contains(x, y)) return nullptr;
    const auto children = Children(root);
    // The last child is drawn on top.
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        if (UltraCanvasUIElement* hit = HitTest(*it, x, y)) return hit;
    }
    return root;
}

// ===== IDS =====

uint32_t IdMap::IdOf(UltraCanvasUIElement* element) {
    if (!element) return 0;
    auto it = ids.find(element);
    if (it != ids.end()) return it->second;
    const uint32_t id = next++;
    ids[element] = id;
    elements[id] = element;
    return id;
}

UltraCanvasUIElement* IdMap::ElementOf(uint32_t id) const {
    auto it = elements.find(id);
    return it == elements.end() ? nullptr : it->second;
}

void IdMap::Forget(UltraCanvasUIElement* element) {
    auto it = ids.find(element);
    if (it == ids.end()) return;
    elements.erase(it->second);
    ids.erase(it);
}

void IdMap::Clear() {
    ids.clear();
    elements.clear();
}

// ===== TEXT =====

std::string Substring(const std::string& utf8, int start, int end) {
    const int count = UltraCanvasAccessibility::CharacterCount(utf8);
    if (end < 0 || end > count) end = count;
    start = std::clamp(start, 0, end);
    const size_t from = UltraCanvasAccessibility::ByteOffsetOfCharacter(utf8, start);
    const size_t to = UltraCanvasAccessibility::ByteOffsetOfCharacter(utf8, end);
    return utf8.substr(from, to - from);
}

uint32_t CodePointAt(const std::string& utf8, int offset) {
    if (offset < 0) return 0;
    const size_t at = UltraCanvasAccessibility::ByteOffsetOfCharacter(utf8, offset);
    if (at >= utf8.size()) return 0;
    const auto byte = [&](size_t i) { return static_cast<unsigned char>(i < utf8.size() ? utf8[i] : 0); };
    const unsigned char lead = byte(at);
    if (lead < 0x80) return lead;
    if ((lead & 0xE0) == 0xC0) return ((lead & 0x1Fu) << 6) | (byte(at + 1) & 0x3Fu);
    if ((lead & 0xF0) == 0xE0) return ((lead & 0x0Fu) << 12) | ((byte(at + 1) & 0x3Fu) << 6) | (byte(at + 2) & 0x3Fu);
    return ((lead & 0x07u) << 18) | ((byte(at + 1) & 0x3Fu) << 12) | ((byte(at + 2) & 0x3Fu) << 6) | (byte(at + 3) & 0x3Fu);
}

bool Difference(const std::string& before, const std::string& after,
                int& start, std::string& removed, std::string& inserted) {
    if (before == after) return false;
    const auto isContinuation = [](unsigned char c) { return (c & 0xC0) == 0x80; };
    size_t prefix = 0;
    const size_t shorter = std::min(before.size(), after.size());
    while (prefix < shorter && before[prefix] == after[prefix]) prefix++;
    // Never split a character.
    while (prefix > 0 && prefix < before.size() && isContinuation(static_cast<unsigned char>(before[prefix]))) prefix--;
    size_t suffix = 0;
    while (suffix < shorter - prefix &&
           before[before.size() - 1 - suffix] == after[after.size() - 1 - suffix]) suffix++;
    while (suffix > 0 && isContinuation(static_cast<unsigned char>(before[before.size() - suffix]))) suffix--;
    start = UltraCanvasAccessibility::CharacterOffsetOfByte(before, prefix);
    removed = before.substr(prefix, before.size() - prefix - suffix);
    inserted = after.substr(prefix, after.size() - prefix - suffix);
    return true;
}

int CharacterAtPoint(const IAccessibleText* text, const Point2Df& windowPoint) {
    if (!text) return -1;
    const int offset = text->GetOffsetAtPoint(windowPoint);
    if (offset > 0) {
        Rect2Df before = text->GetCharacterBounds(offset - 1);
        if (before.width > 0 && before.height > 0 && before.Contains(windowPoint)) return offset - 1;
    }
    return offset;
}

Rect2Df RangeBounds(const IAccessibleText* text, int start, int end, int limit) {
    Rect2Df bounds(0, 0, 0, 0);
    if (!text) return bounds;
    const int count = text->GetCharacterCount();
    if (end < 0 || end > count) end = count;
    start = std::max(0, start);
    end = std::min(end, start + std::max(1, limit));
    for (int offset = start; offset < end; offset++) {
        const Rect2Df box = text->GetCharacterBounds(offset);
        if (box.width <= 0 && box.height <= 0) continue;
        bounds = bounds.Union(box);
    }
    return bounds;
}

} // namespace AccessibilityBridge
} // namespace UltraCanvas
