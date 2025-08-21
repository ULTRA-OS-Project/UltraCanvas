// UltraCanvasUIElement.h - CLEAN VERSION WITHOUT LEGACY CODE
// Modern C++ base class system for all UI components
// Version: 3.0.1
// Last Modified: 2025-01-17
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
    class UltraCanvasBaseWindow;
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
    long GetAbsoluteX() const { \
        if (parent) { \
            return parent->GetAbsoluteX() + properties.x_pos; \
        } \
        return properties.x_pos; \
    } \
    void SetAbsoluteX(long x) { \
        if (parent) { \
            properties.x_pos = x - parent->GetAbsoluteX(); \
        } else { \
            properties.x_pos = x; \
        } \
    } \
    long GetAbsoluteY() const { \
        if (parent) { \
            return parent->GetAbsoluteY() + properties.y_pos; \
        } \
        return properties.y_pos; \
    } \
    void SetAbsoluteY(long y) { \
        if (parent) { \
            properties.y_pos = y - parent->GetAbsoluteY(); \
        } else { \
            properties.y_pos = y; \
        } \
    } \
    long GetWidth() const { return properties.width_size; } \
    void SetWidth(long w) { properties.width_size = w; } \
    long GetHeight() const { return properties.height_size; } \
    void SetHeight(long h) { properties.height_size = h; } \
    \
    long GetX() const { return properties.x_pos; } \
    void SetX(long x) { properties.x_pos = x; } \
    long GetY() const { return properties.y_pos; } \
    void SetY(long y) { properties.y_pos = y; } \
    \
    void SetAbsolutePosition(long x, long y) { SetAbsoluteX(x); SetAbsoluteY(y); } \
    void SetPosition(long x, long y) { properties.x_pos = x; properties.y_pos = y; } \
    void SetSize(long w, long h) { properties.width_size = w; properties.height_size = h; } \
    void SetBounds(long x, long y, long w, long h) { \
        SetPosition(x, y); \
        SetSize(w, h); \
    } \
    \
    Rect2D GetAbsoluteBounds() const { \
        return Rect2D(static_cast<float>(GetAbsoluteX()), static_cast<float>(GetAbsoluteY()), \
                     static_cast<float>(properties.width_size), static_cast<float>(properties.height_size)); \
    } \
    Rect2D GetBounds() const { \
        return Rect2D(static_cast<float>(properties.x_pos), static_cast<float>(properties.y_pos), \
                     static_cast<float>(properties.width_size), static_cast<float>(properties.height_size)); \
    } \
    Point2D GetAbsolutePosition() const { \
        return Point2D(static_cast<float>(GetAbsoluteX()), static_cast<float>(GetAbsoluteY())); \
    } \
    Point2D GetPosition() const { \
        return Point2D(static_cast<float>(properties.x_pos), static_cast<float>(properties.y_pos)); \
    } \
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

        // Timing and animation
        std::chrono::steady_clock::time_point lastUpdateTime;
        std::chrono::steady_clock::time_point creationTime;

        // Event handling
        std::function<bool(const UCEvent&)> eventCallback;

    protected:
        UltraCanvasBaseWindow* window = nullptr; // parent window
        StandardProperties properties;
        // State management
        ElementStateFlags stateFlags;

        // Hierarchy management
        UltraCanvasElement* parent = nullptr;
        std::vector<UltraCanvasElement*> children;

    public:
        // ===== CONSTRUCTOR AND DESTRUCTOR =====
        UltraCanvasElement(const std::string& identifier = "", long id = 0,
                           long x = 0, long y = 0, long w = 100, long h = 30)
                : properties(identifier, id, x, y, w, h),
                  creationTime(std::chrono::steady_clock::now()),
                  lastUpdateTime(creationTime) {
            stateFlags.Reset();
        }

        virtual ~UltraCanvasElement() = default;

        // ===== STANDARD PROPERTY ACCESSORS (including relative coordinate support) =====
        ULTRACANVAS_STANDARD_PROPERTIES_ACCESSORS()

        // ===== HIERARCHY MANAGEMENT =====
        UltraCanvasElement* GetParent() const { return parent; }
        void SetParent(UltraCanvasElement* newParent) { parent = newParent; }

        const std::vector<UltraCanvasElement*>& GetChildren() const { return children; }

        void AddChild(UltraCanvasElement* child);
        void RemoveChild(UltraCanvasElement* child);
        UltraCanvasElement* FindChildById(const std::string& id);

        // ===== STATE MANAGEMENT =====
        bool IsHovered() const { return stateFlags.isHovered; }
        void SetHovered(bool hovered) { stateFlags.isHovered = hovered; }

        bool IsPressed() const { return stateFlags.isPressed; }
        void SetPressed(bool pressed) { stateFlags.isPressed = pressed; }

        bool IsFocused() const { return stateFlags.isFocused; }
        void SetFocus(bool focused) { stateFlags.isFocused = focused; }

        bool IsEnabled() const { return stateFlags.isEnabled && properties.Active; }
        void SetEnabled(bool enabled) { stateFlags.isEnabled = enabled; }

        bool IsSelected() const { return stateFlags.isSelected; }
        void SetSelected(bool selected) { stateFlags.isSelected = selected; }

        bool IsDragging() const { return stateFlags.isDragging; }
        void SetDragging(bool dragging) { stateFlags.isDragging = dragging; }

        bool IsResizing() const { return stateFlags.isResizing; }
        void SetResizing(bool resizing) { stateFlags.isResizing = resizing; }

        ElementState GetElementState() const { return stateFlags.GetPrimaryState(); }

        bool IsHandleOutsideClicks() const { return false; } // Default implementation

        // ===== WINDOW MANAGEMENT =====
        UltraCanvasBaseWindow* GetWindow() const { return window; }
        void SetWindow(UltraCanvasBaseWindow* w) { window = w; }

        // ===== SPATIAL QUERIES =====
        virtual bool Contains(const Point2D& point) const {
            return GetBounds().Contains(point);
        }

        virtual bool ContainsAbsolute(const Point2D& point) const {
            return GetAbsoluteBounds().Contains(point);
        }

        virtual bool Contains(float px, float py) const {
            return Contains(Point2D(px, py));
        }

        virtual bool ContainsAbsolute(float px, float py) const {
            return ContainsAbsolute(Point2D(px, py));
        }

        virtual bool Contains(int px, int py) const {
            return Contains(static_cast<float>(px), static_cast<float>(py));
        }

        virtual bool ContainsAbsolute(int px, int py) const {
            return ContainsAbsolute(static_cast<float>(px), static_cast<float>(py));
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
        void SetEventCallback(std::function<bool(const UCEvent&)> callback) {
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

        virtual bool IsHandleOutsideClicks() { return false; }

        bool IsAncestorOf(const UltraCanvasElement* element) const;

        bool IsDescendantOf(const UltraCanvasElement* element) const {
            if (!element) return false;
            return element->IsAncestorOf(this);
        }

        std::string GetDebugInfo() const {
            return "Element{id='" + GetIdentifier() +
                   "', bounds=(" + std::to_string(GetX()) + "," + std::to_string(GetY()) +
                   "," + std::to_string(GetWidth()) + "," + std::to_string(GetHeight()) +
                   "), relative=(" + std::to_string(properties.x_pos) + "," + std::to_string(properties.y_pos) +
                   "), visible=" + (IsVisible() ? "true" : "false") +
                   ", enabled=" + (IsEnabled() ? "true" : "false") + "}";
        }

        void RequestRedraw();

        // ===== VIRTUAL INTERFACE =====
        virtual void Render() = 0;
        virtual bool OnEvent(const UCEvent& event) { return false; }
        virtual void PerformLayout() {}

        // ===== CHILD MANAGEMENT CALLBACKS =====
        virtual void OnChildAdded(UltraCanvasElement* child) {}
        virtual void OnChildRemoved(UltraCanvasElement* child) {}
    };

// ===== ELEMENT FACTORY SYSTEM =====
    class UltraCanvasElementFactory {
    public:
        template<typename T, typename... Args>
        static std::shared_ptr<T> Create(Args&&... args) {
            return std::make_shared<T>(std::forward<Args>(args)...);
        }

        template<typename T, typename... Args>
        static std::shared_ptr<T> CreateWithID(long id, Args&&... args) {
            auto element = std::make_shared<T>(std::forward<Args>(args)...);
            element->SetIdentifierID(id);
            return element;
        }

        template<typename T>
        static std::shared_ptr<T> CreateAt(const std::string& identifier, long x, long y, long w, long h) {
            return std::make_shared<T>(identifier, 0, x, y, w, h);
        }
    };

// ===== UTILITY FUNCTIONS =====

// Calculate total bounds of all elements
    inline Rect2D CalculateTotalBounds(const std::vector<UltraCanvasElement*>& elements) {
        if (elements.empty()) {
            return Rect2D(0, 0, 0, 0);
        }

        Rect2D bounds = elements[0]->GetBounds();

        for (size_t i = 1; i < elements.size(); ++i) {
            if (elements[i]) {
                Rect2D childBounds = elements[i]->GetBounds();
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