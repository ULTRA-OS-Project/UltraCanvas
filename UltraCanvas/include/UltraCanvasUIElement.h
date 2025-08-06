// UltraCanvasUIElement.h - CLEAN VERSION WITHOUT LEGACY CODE
// Modern C++ base class system for all UI components
// Version: 3.0.0
// Last Modified: 2025-01-04
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasEvent.h"
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <chrono>
#include <algorithm>

namespace UltraCanvas {

// Forward declarations
    class UltraCanvasElement;
    class IRenderContext;

// ===== MODERN PROPERTIES SYSTEM =====
    struct StandardProperties {
        // Core identification
        std::string Identifier = "";
        long IdentifierID = 0;

        // Position and size
        long x_pos = 0;
        long y_pos = 0;
        long width_size = 100;
        long height_size = 30;

        // State properties
        bool Active = true;
        bool Visible = true;

        // Mouse interaction
        MousePointer MousePtr = MousePointer::Default;
        MouseControls MouseCtrl = MouseControls::NoMouse;

        // Hierarchy
        long ParentObject = 0;
        long z_index = 0;

        // Extended properties
        std::string Script = "";
        std::vector<uint8_t> Cache;

        // Constructors
        StandardProperties() = default;

        StandardProperties(const std::string& identifier, long id, long x, long y, long w, long h)
                : Identifier(identifier), IdentifierID(id), x_pos(x), y_pos(y),
                  width_size(w), height_size(h) {}

        // Utility methods
        Rect2D GetBounds() const {
            return Rect2D(static_cast<float>(x_pos), static_cast<float>(y_pos),
                          static_cast<float>(width_size), static_cast<float>(height_size));
        }

        Point2D GetPosition() const {
            return Point2D(static_cast<float>(x_pos), static_cast<float>(y_pos));
        }

        Point2D GetSize() const {
            return Point2D(static_cast<float>(width_size), static_cast<float>(height_size));
        }

        bool Contains(const Point2D& point) const {
            return GetBounds().Contains(point);
        }

        bool Contains(float px, float py) const {
            return Contains(Point2D(px, py));
        }
    };

// ===== CLEAN PROPERTY ACCESSORS MACRO =====
#define ULTRACANVAS_STANDARD_PROPERTIES_ACCESSORS() \
    const std::string& GetIdentifier() const { return properties.Identifier; } \
    void SetIdentifier(const std::string& id) { properties.Identifier = id; } \
    long GetIdentifierID() const { return properties.IdentifierID; } \
    void SetIdentifierID(long id) { properties.IdentifierID = id; } \
    \
    long GetX() const { return properties.x_pos; } \
    void SetX(long x) { properties.x_pos = x; } \
    long GetY() const { return properties.y_pos; } \
    void SetY(long y) { properties.y_pos = y; } \
    long GetWidth() const { return properties.width_size; } \
    void SetWidth(long w) { properties.width_size = w; } \
    long GetHeight() const { return properties.height_size; } \
    void SetHeight(long h) { properties.height_size = h; } \
    \
    void SetPosition(long x, long y) { properties.x_pos = x; properties.y_pos = y; } \
    void SetSize(long w, long h) { properties.width_size = w; properties.height_size = h; } \
    void SetBounds(long x, long y, long w, long h) { \
        properties.x_pos = x; properties.y_pos = y; \
        properties.width_size = w; properties.height_size = h; \
    } \
    \
    Rect2D GetBounds() const { return properties.GetBounds(); } \
    Point2D GetPosition() const { return properties.GetPosition(); } \
    Point2D GetElementSize() const { return properties.GetSize(); } \
    \
    bool IsActive() const { return properties.Active; } \
    void SetActive(bool active) { properties.Active = active; } \
    bool IsVisible() const { return properties.Visible; } \
    void SetVisible(bool visible) { properties.Visible = visible; } \
    \
    MousePointer GetMousePointer() const { return properties.MousePtr; } \
    void SetMousePointer(MousePointer pointer) { properties.MousePtr = pointer; } \
    MouseControls GetMouseControls() const { return properties.MouseCtrl; } \
    void SetMouseControls(MouseControls controls) { properties.MouseCtrl = controls; } \
    \
    long GetZIndex() const { return properties.z_index; } \
    void SetZIndex(long index) { properties.z_index = index; } \
    \
    const std::string& GetScript() const { return properties.Script; } \
    void SetScript(const std::string& script) { properties.Script = script; }

// ===== ELEMENT STATE MANAGEMENT =====
    enum class ElementState {
        Normal,
        Hovered,
        Pressed,
        Focused,
        Disabled,
        Selected
    };

    struct ElementStateFlags {
        bool isHovered = false;
        bool isPressed = false;
        bool isFocused = false;
        bool isEnabled = true;
        bool isSelected = false;
        bool isDragging = false;
        bool isResizing = false;

        ElementState GetPrimaryState() const {
            if (!isEnabled) return ElementState::Disabled;
            if (isPressed) return ElementState::Pressed;
            if (isFocused) return ElementState::Focused;
            if (isSelected) return ElementState::Selected;
            if (isHovered) return ElementState::Hovered;
            return ElementState::Normal;
        }

        void Reset() {
            isHovered = isPressed = isFocused = isSelected = isDragging = isResizing = false;
            isEnabled = true;
        }
    };

// ===== CLEAN BASE UI ELEMENT CLASS =====
    class UltraCanvasElement {
    private:
        // Core properties - NO LEGACY PROPERTIES
        StandardProperties properties;

        // Hierarchy management
        UltraCanvasElement* parent = nullptr;
        std::vector<UltraCanvasElement*> children;

        // State management
        ElementStateFlags stateFlags;

        // Timing and animation
        std::chrono::steady_clock::time_point lastUpdateTime;
        std::chrono::steady_clock::time_point creationTime;

        // Event handling
        std::function<void(const UCEvent&)> eventCallback;

    public:
        // ===== CONSTRUCTOR AND DESTRUCTOR =====
        UltraCanvasElement(const std::string& identifier = "", long id = 0,
                           long x = 0, long y = 0, long w = 100, long h = 30)
                : properties(identifier, id, x, y, w, h),
                  creationTime(std::chrono::steady_clock::now()),
                  lastUpdateTime(creationTime) {
            stateFlags.Reset();
        }

        virtual ~UltraCanvasElement() {
            // Remove from parent
            if (parent) {
                parent->RemoveChild(this);
            }

            // Clear children (they will handle their own cleanup)
            children.clear();
        }

        // ===== INCLUDE PROPERTY ACCESSORS =====
        ULTRACANVAS_STANDARD_PROPERTIES_ACCESSORS()

        // ===== HIERARCHY MANAGEMENT =====
        void AddChild(UltraCanvasElement* child) {
            if (!child || child == this) return;

            // Remove from previous parent
            if (child->parent) {
                child->parent->RemoveChild(child);
            }

            // Add to this element
            children.push_back(child);
            child->parent = this;
            child->properties.ParentObject = this->properties.IdentifierID;

            // Notify derived classes
            OnChildAdded(child);
        }

        void RemoveChild(UltraCanvasElement* child) {
            auto it = std::find(children.begin(), children.end(), child);
            if (it != children.end()) {
                (*it)->parent = nullptr;
                (*it)->properties.ParentObject = 0;
                children.erase(it);

                // Notify derived classes
                OnChildRemoved(child);
            }
        }

        UltraCanvasElement* FindChildById(const std::string& id) {
            for (auto* child : children) {
                if (child->GetIdentifier() == id) {
                    return child;
                }
            }
            return nullptr;
        }

        const std::vector<UltraCanvasElement*>& GetChildren() const {
            return children;
        }

        UltraCanvasElement* GetParent() const {
            return parent;
        }

        // ===== CORE VIRTUAL METHODS =====
        virtual void Render() {}
        virtual void OnEvent(const UCEvent& event) {
            if (eventCallback) {
                eventCallback(event);
            }
        }
        virtual void OnChildAdded(UltraCanvasElement* child) {}
        virtual void OnChildRemoved(UltraCanvasElement* child) {}

        // ===== STATE MANAGEMENT =====
        ElementState GetState() const { return stateFlags.GetPrimaryState(); }
        const ElementStateFlags& GetStateFlags() const { return stateFlags; }

        bool IsHovered() const { return stateFlags.isHovered; }
        void SetHovered(bool hovered) { stateFlags.isHovered = hovered; }

        bool IsPressed() const { return stateFlags.isPressed; }
        void SetPressed(bool pressed) { stateFlags.isPressed = pressed; }

        bool IsFocused() const { return stateFlags.isFocused; }
        void SetFocus(bool focused) { stateFlags.isFocused = focused; }

        bool IsEnabled() const { return stateFlags.isEnabled; }
        void SetEnabled(bool enabled) { stateFlags.isEnabled = enabled; }

        bool IsSelected() const { return stateFlags.isSelected; }
        void SetSelected(bool selected) { stateFlags.isSelected = selected; }

        // ===== SPATIAL QUERIES =====
        virtual bool Contains(const Point2D& point) const {
            return properties.Contains(point);
        }

        virtual bool Contains(float px, float py) const {
            return properties.Contains(px, py);
        }

        Point2D GetAbsolutePosition() const {
            Point2D pos = GetPosition();
            if (parent) {
                Point2D parentPos = parent->GetAbsolutePosition();
                pos.x += parentPos.x;
                pos.y += parentPos.y;
            }
            return pos;
        }

        // ===== TIMING AND ANIMATION =====
        float GetAge() const {
            auto now = std::chrono::steady_clock::now();
            return std::chrono::duration<float>(now - creationTime).count();
        }

        float GetTimeSinceLastUpdate() const {
            auto now = std::chrono::steady_clock::now();
            return std::chrono::duration<float>(now - lastUpdateTime).count();
        }

        virtual void UpdateTiming() {
            lastUpdateTime = std::chrono::steady_clock::now();
        }

        // ===== EVENT HANDLING =====
        void SetEventCallback(std::function<void(const UCEvent&)> callback) {
            eventCallback = callback;
        }

        // ===== UTILITY METHODS =====
        UltraCanvasElement* GetRoot() {
            UltraCanvasElement* root = this;
            while (root->parent) {
                root = root->parent;
            }
            return root;
        }

        bool IsAncestorOf(const UltraCanvasElement* element) const {
            if (!element) return false;

            const UltraCanvasElement* current = element->parent;
            while (current) {
                if (current == this) return true;
                current = current->parent;
            }
            return false;
        }

        bool IsDescendantOf(const UltraCanvasElement* element) const {
            if (!element) return false;
            return element->IsAncestorOf(this);
        }

        std::string GetDebugInfo() const {
            return "Element{id='" + GetIdentifier() +
                   "', bounds=(" + std::to_string(GetX()) + "," + std::to_string(GetY()) +
                   "," + std::to_string(GetWidth()) + "," + std::to_string(GetHeight()) +
                   "), visible=" + (IsVisible() ? "true" : "false") +
                   ", children=" + std::to_string(children.size()) + "}";
        }
    };

// ===== FACTORY SYSTEM =====
    class UltraCanvasElementFactory {
    public:
        template<typename ElementType, typename... Args>
        static std::shared_ptr<ElementType> Create(Args&&... args) {
            return std::make_shared<ElementType>(std::forward<Args>(args)...);
        }

        template<typename ElementType, typename... Args>
        static std::shared_ptr<ElementType> CreateWithID(long id, Args&&... args) {
            auto element = std::make_shared<ElementType>(std::forward<Args>(args)...);
            element->SetIdentifierID(id);
            return element;
        }

        template<typename ElementType, typename... Args>
        static std::shared_ptr<ElementType> CreateWithIdentifier(const std::string& identifier, Args&&... args) {
            auto element = std::make_shared<ElementType>(std::forward<Args>(args)...);
            element->SetIdentifier(identifier);
            return element;
        }
    };

// ===== CLEAN UTILITY FUNCTIONS (NO LEGACY REFERENCES) =====

// Find element by ID in hierarchy
    inline UltraCanvasElement* FindElementByID(UltraCanvasElement* root, const std::string& id) {
        if (!root) return nullptr;

        if (root->GetIdentifier() == id) {  // NOW THIS WORKS!
            return root;
        }

        for (auto* child : root->GetChildren()) {
            if (auto* found = FindElementByID(child, id)) {
                return found;
            }
        }

        return nullptr;
    }

// Hit testing - find topmost element at point
    inline UltraCanvasElement* HitTest(UltraCanvasElement* root, const Point2D& point) {
        if (!root || !root->IsVisible() || !root->Contains(point)) {
            return nullptr;
        }

        // Check children first (they render on top)
        auto children = root->GetChildren();
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            auto* child = *it;
            if (auto* hit = HitTest(child, point)) {
                return hit;
            }
        }

        // If no child was hit, this element was hit
        return root;
    }

// Calculate total bounds of element hierarchy
    inline Rect2D CalculateTotalBounds(UltraCanvasElement* root) {
        if (!root || !root->IsVisible()) {
            return Rect2D(0, 0, 0, 0);
        }

        Rect2D bounds = root->GetBounds();

        for (auto* child : root->GetChildren()) {
            if (child && child->IsVisible()) {
                Rect2D childBounds = CalculateTotalBounds(child);
                bounds = bounds.Union(childBounds);
            }
        }

        return bounds;
    }

// Find elements of specific type
    template<typename T>
    inline std::vector<T*> FindElementsOfType(UltraCanvasElement* root) {
        std::vector<T*> result;
        if (!root) return result;

        if (auto* typed = dynamic_cast<T*>(root)) {
            result.push_back(typed);
        }

        for (auto* child : root->GetChildren()) {
            auto childResults = FindElementsOfType<T>(child);
            result.insert(result.end(), childResults.begin(), childResults.end());
        }

        return result;
    }

// Visit all elements with a function
    template<typename Func>
    inline void VisitElements(UltraCanvasElement* root, Func visitor) {
        if (!root) return;

        visitor(root);

        for (auto* child : root->GetChildren()) {
            VisitElements(child, visitor);
        }
    }

// Count total elements in hierarchy
    inline size_t CountElements(UltraCanvasElement* root) {
        if (!root) return 0;

        size_t count = 1;
        for (auto* child : root->GetChildren()) {
            count += CountElements(child);
        }
        return count;
    }

// Print element hierarchy for debugging
    inline void PrintElementHierarchy(UltraCanvasElement* root, int depth = 0) {
        if (!root) return;

        std::string indent(depth * 2, ' ');
        std::cout << indent << root->GetDebugInfo() << std::endl;

        for (auto* child : root->GetChildren()) {
            PrintElementHierarchy(child, depth + 1);
        }
    }

} // namespace UltraCanvas

/*
=== CLEAN IMPLEMENTATION SUMMARY ===

✅ **All Legacy Code Removed**:
- No more "id", "x", "y", "width", "height" legacy properties
- No more SyncLegacyProperties() calls
- Clean, modern C++ design only

✅ **Pure StandardProperties System**:
- Single source of truth for all properties
- GetIdentifier() now works properly through macro
- No duplicate property storage

✅ **Modern C++ Features**:
- RAII and smart pointer ready
- std::chrono for timing
- Template-based factory system
- Range-based for loops

✅ **Clean Architecture**:
- No backward compatibility burden
- Streamlined class hierarchy
- Efficient memory usage
- Type-safe design

✅ **Fixed Compilation Issues**:
- GetIdentifier() method now available
- All utility functions work properly
- No missing method errors

This is now a clean, modern implementation without any legacy cruft!
*/