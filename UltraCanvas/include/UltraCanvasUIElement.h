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
    class UltraCanvasContainer;
    class UltraCanvasElement;
    class UltraCanvasWindow;
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

// ===== ELEMENT STATE MANAGEMENT =====
// Forward declarations

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

// ===== LEAF UI ELEMENT CLASS (NO CHILDREN) =====
    class UltraCanvasElement {
    private:
        // Timing and animation
        std::chrono::steady_clock::time_point lastUpdateTime;
        std::chrono::steady_clock::time_point creationTime;

        // Event handling
        std::function<bool(const UCEvent&)> eventCallback;

    protected:
        UltraCanvasWindow* window = nullptr;
        UltraCanvasContainer* parentContainer = nullptr; // Parent container (not element)
        StandardProperties properties;
        ElementStateFlags stateFlags;

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
            // Remove from parent container if attached
            if (parentContainer) {
                // Container will handle removal
            }
        }

        // ===== INCLUDE PROPERTY ACCESSORS =====
// ===== STANDARD PROPERTY ACCESSORS (including relative coordinate support) =====
        const std::string& GetIdentifier() const { return properties.Identifier; }
        void SetIdentifier(const std::string& id) { properties.Identifier = id; }
        long GetIdentifierID() const { return properties.IdentifierID; }
        void SetIdentifierID(long id) { properties.IdentifierID = id; }

        virtual long GetXInWindow();
//        void SetAbsoluteX(long x) {
//            if (parent) {
//                properties.x_pos = x - parent->GetAbsoluteX();
//            } else {
//                properties.x_pos = x;
//            }
//        }
        virtual long GetYInWindow();
//        void SetAbsoluteY(long y) {
//            if (parent) {
//                properties.y_pos = y - parent->GetAbsoluteY();
//            } else {
//                properties.y_pos = y;
//            }
//        }

        long GetWidth() const { return properties.width_size; }
        void SetWidth(long w) { properties.width_size = w; }
        long GetHeight() const { return properties.height_size; }
        void SetHeight(long h) { properties.height_size = h; }

        long GetX() const { return properties.x_pos; }
        void SetX(long x) { properties.x_pos = x; }
        long GetY() const { return properties.y_pos; }
        void SetY(long y) { properties.y_pos = y; }

//        void SetAbsolutePosition(long x, long y) { SetAbsoluteX(x); SetAbsoluteY(y); }
        virtual void SetPosition(long x, long y) { properties.x_pos = x; properties.y_pos = y; }
        virtual void SetSize(long w, long h) { properties.width_size = w; properties.height_size = h; }
        void SetBounds(long x, long y, long w, long h) {
            SetPosition(x, y);
            SetSize(w, h);
        }

//        Rect2D GetAbsoluteBounds() const {
//            return Rect2D(static_cast<float>(GetAbsoluteX()), static_cast<float>(GetAbsoluteY()),
//                          static_cast<float>(properties.width_size), static_cast<float>(properties.height_size));
//        }
        Rect2D GetBounds() const {
            return Rect2D(static_cast<float>(properties.x_pos), static_cast<float>(properties.y_pos),
                          static_cast<float>(properties.width_size), static_cast<float>(properties.height_size));
        }
        Point2D GetPositionInWindow() {
            return Point2D(static_cast<float>(GetXInWindow()), static_cast<float>(GetYInWindow()));
        }
        Point2D GetPosition() const {
            return Point2D(static_cast<float>(properties.x_pos), static_cast<float>(properties.y_pos));
        }
        Point2D GetElementSize() const { return properties.GetSize(); }

        bool IsActive() const { return properties.Active; }
        void SetActive(bool active) { properties.Active = active; }
        bool IsVisible() const { return properties.Visible; }
        void SetVisible(bool visible) { properties.Visible = visible; }

        Point2D ConvertWindowToLocalCoordinates(const Point2D &globalPos);
        MousePointer GetMousePointer() const { return properties.MousePtr; }
        void SetMousePointer(MousePointer pointer) { properties.MousePtr = pointer; }
        MouseControls GetMouseControls() const { return properties.MouseCtrl; }
        void SetMouseControls(MouseControls controls) { properties.MouseCtrl = controls; }

        long GetZIndex() const { return properties.z_index; }
        void SetZIndex(long index) { properties.z_index = index; }

        const std::string& GetScript() const { return properties.Script; }
        void SetScript(const std::string& script) { properties.Script = script; }

        // ===== HIERARCHY MANAGEMENT =====
//        UltraCanvasContainer* GetParent() const { return parent; }
//        void SetParent(UltraCanvasContainer* newParent) { parent = newParent; }

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

        ElementState GetState() const { return stateFlags.GetPrimaryState(); }
        const ElementStateFlags& GetStateFlags() const { return stateFlags; }

        // ===== PARENT CONTAINER MANAGEMENT =====
        UltraCanvasContainer* GetParentContainer() const {
            return parentContainer;
        }

        void SetParentContainer(UltraCanvasContainer* container) {
            parentContainer = container;
        }

        UltraCanvasWindow* GetWindow() const {
            return window;
        }

        virtual void SetWindow(UltraCanvasWindow* win) {
            window = win;
        }

        void SetThisAsActivePopupElement();
        void ClearThisAsActivePopupElement();
        void RequestRedraw();

        // ===== CORE VIRTUAL METHODS =====
        virtual void Render() {}

        virtual bool OnEvent(const UCEvent& event) {
            if (eventCallback) {
                return eventCallback(event);
            }
            return false;
        }

        // ===== SPATIAL QUERIES =====
        virtual bool Contains(const Point2D& point) const {
            return properties.Contains(point);
        }

        virtual bool Contains(float px, float py) const {
            return properties.Contains(px, py);
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
        UltraCanvasContainer* GetRootContainer();

        bool IsDescendantOf(const UltraCanvasContainer* container) const;

        std::string GetDebugInfo() const {
            return "Element{id='" + GetIdentifier() +
                   "', bounds=(" + std::to_string(GetX()) + "," + std::to_string(GetY()) +
                   "," + std::to_string(GetWidth()) + "," + std::to_string(GetHeight()) +
                   "), visible=" + (IsVisible() ? "true" : "false") + "}";
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

} // namespace UltraCanvas