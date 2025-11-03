// include/UltraCanvasUIElement.h
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
    class UltraCanvasUIElement;
    class UltraCanvasWindowBase;
    class IRenderContext;

// ===== MODERN PROPERTIES SYSTEM =====
    struct StandardProperties {
        // Core identification
        std::string Identifier = "";
        long IdentifierID = 0;

        // State properties
        bool Active = true;
        bool Visible = true;

        // Mouse interaction
        MousePointer MousePtr = MousePointer::Default;
        MouseControls MouseCtrl = MouseControls::NoMouse;

        // Hierarchy
//        long ParentObject = 0;
        int z_index = 0;

        std::string tooltip = "";

        // Extended properties
        std::string Script = "";
        std::vector<uint8_t> Cache;

        // Constructors
        StandardProperties() = default;

        StandardProperties(const std::string& identifier, long id)
                : Identifier(identifier), IdentifierID(id) {}

        // Utility methods
//        Rect2Di GetBounds() const {
//            return Rect2Di(x_pos, y_pos, width_size, height_size);
//        }
//
//        Point2Di GetPosition() const {
//            return Point2Di(x_pos, y_pos);
//        }
//
//        Point2Di GetSize() const {
//            return Point2Di(width_size, height_size);
//        }
//
//        bool Contains(const Point2Di& point) const {
//            return GetBounds().Contains(point);
//        }
//
//        bool Contains(int px, int py) const {
//            return Contains(Point2Di(px, py));
//        }
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
        bool isEnabled = true;
        bool isSelected = false;
        bool isDragging = false;
        bool isResizing = false;

        ElementState GetPrimaryState() const {
            if (!isEnabled) return ElementState::Disabled;
            if (isPressed) return ElementState::Pressed;
            if (isSelected) return ElementState::Selected;
            if (isHovered) return ElementState::Hovered;
            return ElementState::Normal;
        }

        void Reset() {
            isHovered = isPressed = isSelected = isDragging = isResizing = false;
            isEnabled = true;
        }
    };

// ===== LEAF UI ELEMENT CLASS (NO CHILDREN) =====
    class UltraCanvasUIElement {
    private:
        // Timing and animation
        std::chrono::steady_clock::time_point lastUpdateTime;
        std::chrono::steady_clock::time_point creationTime;

        // Event handling
        std::function<bool(const UCEvent&)> eventCallback;

    protected:
        UltraCanvasWindowBase* window = nullptr;
        UltraCanvasContainer* parentContainer = nullptr; // Parent container (not element)
        StandardProperties properties;
        ElementStateFlags stateFlags;
        Rect2Di bounds;

    public:
        // ===== CONSTRUCTOR AND DESTRUCTOR =====
        UltraCanvasUIElement(const std::string& identifier = "", long id = 0,
                             int x = 0, int y = 0, int w = 100, int h = 30)
                : properties(identifier, id),
                  creationTime(std::chrono::steady_clock::now()),
                  bounds(x, y, w, h),
                  lastUpdateTime(creationTime) {
            stateFlags.Reset();
        }

        virtual ~UltraCanvasUIElement() {
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

        const Rect2Di& GetBounds() const {
            return bounds;
        }

        Point2Di GetPosition() {
            return Point2Di(bounds.x, bounds.y);
        }

        Size2Di GetSize() {
            return Size2Di(bounds.width, bounds.height);
        }

        bool Contains(const Point2Di& point) {
            return bounds.Contains(point);
        }

        virtual bool Contains(int px, int py) {
            return bounds.Contains(px, py);
        }

        virtual int GetXInWindow();
//        void SetAbsoluteX(int x) {
//            if (parent) {
//                properties.x_pos = x - parent->GetAbsoluteX();
//            } else {
//                properties.x_pos = x;
//            }
//        }
        virtual int GetYInWindow();
//        void SetAbsoluteY(int y) {
//            if (parent) {
//                properties.y_pos = y - parent->GetAbsoluteY();
//            } else {
//                properties.y_pos = y;
//            }
//        }

        int GetWidth() const { return bounds.width; }
        void SetWidth(int w) { bounds.width = w; }
        int GetHeight() const { return bounds.height; }
        void SetHeight(int h) { bounds.height = h; }

        int GetX() const { return bounds.x; }
        void SetX(int x) { bounds.x = x; }
        int GetY() const { return bounds.y; }
        void SetY(int y) { bounds.y = y; }

//        void SetAbsolutePosition(int x, int y) { SetAbsoluteX(x); SetAbsoluteY(y); }
        virtual void SetPosition(int x, int y) { bounds.x = x; bounds.y = y; }
        virtual void SetSize(int w, int h) { bounds.width = w; bounds.height = h; }
        void SetBounds(int x, int y, int w, int h) {
            SetBounds(Rect2Di(x, y, w, h));
        }
        void SetBounds(float x, float y, float w, float h) {
            SetBounds(Rect2Di(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w), static_cast<int>(h)));
        }
        virtual void SetBounds(const Rect2Di& b) {
            bounds = b;
        }

//        Rect2Di GetAbsoluteBounds() const {
//            return Rect2Di(static_cast<float>(GetAbsoluteX()), static_cast<float>(GetAbsoluteY()),
//                          static_cast<float>(properties.width_size), static_cast<float>(properties.height_size));
//        }
        // actual bounds is for variable-sized elements like dropdowns, menus, popups
        virtual Rect2Di GetActualBounds() { return GetBounds(); }

//        Rect2Di GetActualBoundsInWindow() {
//            Rect2Di bounds = GetActualBounds();
//            bounds.x += (GetXInWindow() - properties.x_pos);
//            bounds.y += (GetYInWindow() - properties.y_pos);
//            return bounds;
//        }
        Rect2Di GetActualBoundsInWindow() {
            Rect2Di bounds = GetActualBounds();

            // Transform bounds to window coordinates
            Point2Di windowPos = GetPositionInWindow();
            bounds.x = windowPos.x;
            bounds.y = windowPos.y;

            return bounds;
        }

        Point2Di GetPositionInWindow() {
            return Point2Di(GetXInWindow(), GetYInWindow());
        }

        bool IsActive() const { return properties.Active; }
        void SetActive(bool active) { properties.Active = active; }
        bool IsVisible() const { return properties.Visible; }
        void SetVisible(bool visible);

        Point2Di ConvertWindowToParentContainerCoordinates(const Point2Di &globalPos);
        virtual void ConvertWindowToParentContainerCoordinates(int &x, int &y);

        Point2Di ConvertContainerToWindowCoordinates(const Point2Di &globalPos);
        virtual void ConvertContainerToWindowCoordinates(int &x, int &y);

        MousePointer GetMousePointer() const { return properties.MousePtr; }
        void SetMousePointer(MousePointer pointer) { properties.MousePtr = pointer; }
        MouseControls GetMouseControls() const { return properties.MouseCtrl; }
        void SetMouseControls(MouseControls controls) { properties.MouseCtrl = controls; }

        int GetZIndex() const { return properties.z_index; }
        void SetZIndex(int index) { properties.z_index = index; }

        const std::string& GetScript() const { return properties.Script; }
        void SetScript(const std::string& script) { properties.Script = script; }

        const std::string& GetTooltip() const { return properties.tooltip; }
        void SetTooltip(const std::string& tooltip) { properties.tooltip = tooltip; }

        // ===== HIERARCHY MANAGEMENT =====
//        UltraCanvasContainer* GetParent() const { return parent; }
//        void SetParent(UltraCanvasContainer* newParent) { parent = newParent; }

        // ===== STATE MANAGEMENT =====
        bool IsHovered() const { return stateFlags.isHovered; }
        void SetHovered(bool hovered) { stateFlags.isHovered = hovered; }

        bool IsPressed() const { return stateFlags.isPressed; }
        void SetPressed(bool pressed) { stateFlags.isPressed = pressed; }

        bool IsFocused() const;
        virtual bool SetFocus(bool focus = true);
        virtual bool AcceptsFocus() const { return false; }
        bool CanReceiveFocus() const { return IsVisible() && IsEnabled() && AcceptsFocus(); }

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

        UltraCanvasWindowBase* GetWindow() const {
            return window;
        }

        virtual void SetWindow(UltraCanvasWindowBase* win);

        //virtual bool IsInPopupState() { return false; }
        virtual void RenderPopupContent() {};

        void AddThisPopupElementToWindow();
        void RemoveThisPopupElementFromWindow();

        void RequestRedraw();
//        void RequestFullRedraw();

        // ===== CORE VIRTUAL METHODS =====
        IRenderContext* GetRenderContext() const;
        virtual void Render() {}

        virtual bool OnEvent(const UCEvent& event) {
            if (eventCallback) {
                if (eventCallback(event)) {
                    return true;
                }
            }
            return false;
        }

        // ===== SPATIAL QUERIES =====
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
    class UltraCanvasUIElementFactory {
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