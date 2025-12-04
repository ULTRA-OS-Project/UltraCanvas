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
//    struct StandardProperties {
//        // Core identification
//        std::string Identifier = "";
//        long IdentifierID = 0;
//
//        // State properties
//        bool Active = true;
//        bool Visible = true;
//
//        // Mouse interaction
//        MousePointer MousePtr = MousePointer::Default;
//        MouseControls MouseCtrl = MouseControls::NoMouse;
//
//        // Hierarchy
////        long ParentObject = 0;
//        int z_index = 0;
//
//        std::string tooltip = "";
//
//        // Constructors
//        StandardProperties() = default;
//
//        StandardProperties(const std::string& identifier, long id)
//                : Identifier(identifier), IdentifierID(id) {}
//
//        // Utility methods
////        Rect2Di GetBounds() const {
////            return Rect2Di(x_pos, y_pos, width_size, height_size);
////        }
////
////        Point2Di GetPosition() const {
////            return Point2Di(x_pos, y_pos);
////        }
////
////        Point2Di GetSize() const {
////            return Point2Di(width_size, height_size);
////        }
////
////        bool Contains(const Point2Di& point) const {
////            return GetBounds().Contains(point);
////        }
////
////        bool Contains(int px, int py) const {
////            return Contains(Point2Di(px, py));
////        }
//    };

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

    struct ElementBorder {
        int width = 0;
        Color color = Color(0, 0, 0, 255);
        int radius = 0;
        UCDashPattern dashPattern;
    };

    struct ElementStateFlags {
        bool isHovered = false;
        bool isPressed = false;
        bool isDisabled = false;
        bool isSelected = false;
//        bool isDragging = false;
//        bool isResizing = false;

        void Reset() {
            isDisabled = isHovered = isPressed = isSelected = false;
        }
    };

// ===== LEAF UI ELEMENT CLASS (NO CHILDREN) =====
    class UltraCanvasUIElement {
    private:
        // Event handling
        std::function<bool(const UCEvent&)> eventCallback;

    protected:
        std::string identifier = "";

        // State properties
        bool visible = true;

        // Mouse interaction
        MousePointer mousePtr = MousePointer::Default;

        // Hierarchy
//        long ParentObject = 0;
        int zIndex = 0;

        std::string tooltip = "";

        UltraCanvasWindowBase* window = nullptr;
        UltraCanvasContainer* parentContainer = nullptr; // Parent container (not element)
        ElementStateFlags stateFlags;
        UCMargins margin;
        UCMargins padding;
        std::unique_ptr<ElementBorder> borderLeft = nullptr;
        std::unique_ptr<ElementBorder> borderRight = nullptr;
        std::unique_ptr<ElementBorder> borderTop = nullptr;
        std::unique_ptr<ElementBorder> borderBottom = nullptr;
        Color backgroundColor = Colors::Transparent;

        Rect2Di bounds;
        Size2Di originalSize;

    public:
        // ===== CONSTRUCTOR AND DESTRUCTOR =====
        UltraCanvasUIElement(const std::string& idstr, long id,
                             int x, int y, int w, int h)
                : identifier(idstr),
                  bounds(x, y, w, h),
                  originalSize(w, h) {
            stateFlags.Reset();
        }

        explicit UltraCanvasUIElement(const std::string& idstr = "",
                             int w = 0, int h = 0)
                : identifier(idstr),
                  bounds(0, 0, w, h),
                  originalSize(w, h) {
            stateFlags.Reset();
        }

        virtual ~UltraCanvasUIElement() {}

        // ===== INCLUDE PROPERTY ACCESSORS =====
// ===== STANDARD PROPERTY ACCESSORS (including relative coordinate support) =====
        const std::string& GetIdentifier() const { return identifier; }
        void SetIdentifier(const std::string& id) { identifier = id; }
//        long GetIdentifierID() const { return properties.IdentifierID; }
//        void SetIdentifierID(long id) { properties.IdentifierID = id; }

        const Rect2Di& GetBounds() const {
            return bounds;
        }

        Point2Di GetPosition() {
            return Point2Di(bounds.x, bounds.y);
        }

        Size2Di GetSize() {
            return Size2Di(bounds.width, bounds.height);
        }

        Size2Di GetOriginalSize() {
            return Size2Di(originalSize);
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
        virtual int GetPreferredWidth() { return originalSize.width; }
        virtual int GetMinWidth() const { return 0; }
        virtual int GetMaxWidth() const { return 10000; }
        void SetWidth(int w) { SetBounds(bounds.x, bounds.y, w, bounds.height); }

        int GetHeight() const { return bounds.height; }
        virtual int GetPreferredHeight() { return originalSize.height; }
        virtual int GetMinHeight() const { return 0; }
        virtual int GetMaxHeight() const { return 10000; }
        void SetHeight(int h) { SetBounds(bounds.x, bounds.y, bounds.width, h); }

        int GetX() const { return bounds.x; }
        void SetX(int x) { bounds.x = x; }
        int GetY() const { return bounds.y; }
        void SetY(int y) { bounds.y = y; }

//        void SetAbsolutePosition(int x, int y) { SetAbsoluteX(x); SetAbsoluteY(y); }
        void SetPosition(int x, int y) { bounds.x = x, bounds.y = y; }
        void SetSize(int w, int h) { SetBounds(bounds.x, bounds.y, w, h); }
        virtual void SetOriginalSize(int w, int h);
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

        // ===== CONVENIENCE SETTERS - MARGIN =====
        void SetMargin(int all) {
            margin.left = margin.right = margin.top = margin.bottom = all;
        }

        void SetMargin(int vertical, int horizontal) {
            margin.left = margin.right = horizontal;
            margin.top = margin.bottom = vertical;
        }

        void SetMargin(int top, int right, int bottom, int left) {
            margin.left = left;
            margin.top = top;
            margin.right = right;
            margin.bottom = bottom;
        }

        // ===== CONVENIENCE SETTERS - PADDING =====
        void SetPadding(int all) {
            padding.left = padding.right = padding.top = padding.bottom = all;
        }

        void SetPadding(int vertical, int horizontal) {
            padding.left = padding.right = horizontal;
            padding.top = padding.bottom = vertical;
        }

        void SetPadding(int top, int right, int bottom, int left) {
            padding.left = left;
            padding.top = top;
            padding.right = right;
            padding.bottom = bottom;
        }

        // ===== CONVENIENCE SETTERS - BORDER WIDTH (all sides same) =====
        void SetBorders(int width, const Color& color = Colors::Black, int borderRadius = 0, const UCDashPattern& dash = UCDashPattern()) {
            SetBorderLeft(width, color, borderRadius, dash);
            SetBorderRight(width, color, borderRadius, dash);
            SetBorderTop(width, color, borderRadius, dash);
            SetBorderBottom(width, color, borderRadius, dash);
        }

        // ===== CONVENIENCE SETTERS - BORDER COLOR (all sides same) =====
        void SetBordersColor(const Color& color) {
            if (borderLeft) {
                borderLeft->color = color;
            }
            if (borderRight) {
                borderRight->color = color;
            }
            if (borderTop) {
                borderTop->color = color;
            }
            if (borderBottom) {
                borderBottom->color = color;
            }
            RequestRedraw();
        }

        // ===== INDIVIDUAL BORDER SETTERS =====
        void SetBorderLeft(int width, const Color& color = Colors::Black, int borderRadius = 0, const UCDashPattern& dash = UCDashPattern()) {
            if (borderLeft) {
                borderLeft->width = width;
                borderLeft->color = color;
                borderLeft->radius = borderRadius;
                borderLeft->dashPattern = dash;
            } else {
                borderLeft = std::make_unique<ElementBorder>(width, color, borderRadius, dash);
            }
        }

        void SetBorderRight(int width, const Color& color = Colors::Black, int borderRadius = 0, const UCDashPattern& dash = UCDashPattern()) {
            if (borderRight) {
                borderRight->width = width;
                borderRight->color = color;
                borderRight->radius = borderRadius;
                borderRight->dashPattern = dash;
            } else {
                borderRight = std::make_unique<ElementBorder>(width, color, borderRadius, dash);
            }
        }

        void SetBorderTop(int width, const Color& color = Colors::Black, int borderRadius = 0, const UCDashPattern& dash = UCDashPattern()) {
            if (borderTop) {
                borderTop->width = width;
                borderTop->color = color;
                borderTop->radius = borderRadius;
                borderTop->dashPattern = dash;

            } else {
                borderTop = std::make_unique<ElementBorder>(width, color, borderRadius, dash);
            }
        }

        void SetBorderBottom(int width, const Color& color = Colors::Black, int borderRadius = 0, const UCDashPattern& dash = UCDashPattern()) {
            if (borderBottom) {
                borderBottom->width = width;
                borderBottom->color = color;
                borderBottom->radius = borderRadius;
                borderBottom->dashPattern = dash;
            } else {
                borderBottom = std::make_unique<ElementBorder>(width, color, borderRadius, dash);
            }
        }

        void SetBackgroundColor(const Color& color) {
            backgroundColor = color;
        }

        const Color& GetBackgroundColor() {
            return backgroundColor;
        }

        // ===== TOTAL CALCULATIONS =====
        int GetTotalMarginHorizontal() const {
            return margin.left + margin.right;
        }

        int GetTotalMarginVertical() const {
            return margin.top + margin.bottom;
        }

        int GetTotalPaddingHorizontal() const {
            return padding.left + padding.right;
        }

        int GetTotalPaddingVertical() const {
            return padding.top + padding.bottom;
        }

        int GetTotalBorderHorizontal() const {
            return GetBorderLeftWidth() + GetBorderRightWidth();
        }

        int GetTotalBorderVertical() const {
            return GetBorderTopWidth() + GetBorderBottomWidth();
        }

        // ===== CONTENT AREA CALCULATIONS =====
        // client area = bounds minus (border + padding)
        virtual Rect2Di GetContentRect() const {
            return Rect2Di(
                    bounds.x + GetBorderLeftWidth() + padding.left,
                    bounds.y + GetBorderTopWidth() + padding.top,
                    bounds.width - (GetTotalBorderHorizontal() + GetTotalPaddingHorizontal()),
                    bounds.height - (GetTotalBorderVertical() + GetTotalPaddingVertical())
            );
        }

        // Padding box = bounds minus border
        Rect2Di GetPaddingRect() {
            return Rect2Di(
                    bounds.x + GetBorderLeftWidth(),
                    bounds.y + GetBorderTopWidth(),
                    bounds.width - GetTotalBorderHorizontal(),
                    bounds.height - GetTotalBorderVertical()
            );
        }

        // Margin box = bounds plus margins
        Rect2Di GetMarginRect() {
            return Rect2Di(
                    bounds.x - margin.left,
                    bounds.y - margin.top,
                    bounds.width + margin.left + margin.right,
                    bounds.height + margin.top + margin.bottom
            );
        }

        int GetMarginLeft() const { return margin.left; }
        int GetMarginRight() const { return margin.right; }
        int GetMarginTop() const { return margin.top; }
        int GetMarginBottom() const { return margin.bottom; }

        int GetPaddingLeft() const { return padding.left; }
        int GetPaddingRight() const { return padding.right; }
        int GetPaddingTop() const { return padding.top; }
        int GetPaddingBottom() const { return padding.bottom; }

        int GetBorderLeftWidth() const { return borderLeft ? borderLeft->width : 0; }
        int GetBorderRightWidth() const { return borderRight ? borderRight->width : 0; }
        int GetBorderTopWidth() const { return borderTop ? borderTop->width : 0; }
        int GetBorderBottomWidth() const { return borderBottom ? borderBottom->width : 0; }

        // ===== CHECK IF ANY BORDER EXISTS =====
        bool HasBorder() const {
            return GetBorderLeftWidth() > 0 || GetBorderRightWidth() > 0 ||
                   GetBorderTopWidth() > 0 || GetBorderBottomWidth() > 0;
        }

        bool HasPadding() const {
            return padding.left > 0 || padding.right > 0 ||
                   padding.top > 0 || padding.bottom > 0;
        }

        bool HasMargin() const {
            return margin.left > 0 || margin.right > 0 ||
                   margin.top > 0 || margin.bottom > 0;
        }

        Point2Di ConvertWindowToParentContainerCoordinates(const Point2Di &globalPos);
        virtual void ConvertWindowToParentContainerCoordinates(int &x, int &y);

        Point2Di ConvertContainerToWindowCoordinates(const Point2Di &globalPos);
        virtual void ConvertContainerToWindowCoordinates(int &x, int &y);

        MousePointer GetMousePointer() const { return mousePtr; }
        void SetMousePointer(MousePointer pointer) { mousePtr = pointer; }

        int GetZIndex() const { return zIndex; }
        void SetZIndex(int index) { zIndex = index; }

        const std::string& GetTooltip() const { return tooltip; }
        void SetTooltip(const std::string& tooltipStr) { tooltip = tooltipStr; }

        // ===== HIERARCHY MANAGEMENT =====
//        UltraCanvasContainer* GetParent() const { return parent; }
//        void SetParent(UltraCanvasContainer* newParent) { parent = newParent; }

        bool IsVisible() const { return visible; }
        void SetVisible(bool visible);

        bool IsDisabled() const { return stateFlags.isDisabled; }
        void SetDisabled(bool disabled) { stateFlags.isDisabled = disabled; RequestRedraw(); }

        // ===== STATE MANAGEMENT =====
        bool IsHovered() const { return stateFlags.isHovered; }
        void SetHovered(bool hovered) { stateFlags.isHovered = hovered; RequestRedraw(); }

        bool IsPressed() const { return stateFlags.isPressed; }
        void SetPressed(bool pressed) { stateFlags.isPressed = pressed; RequestRedraw(); }

        bool IsFocused() const;
        virtual bool SetFocus(bool focus = true);
        virtual bool AcceptsFocus() const { return false; }
        bool CanReceiveFocus() const { return IsVisible() && !IsDisabled() && AcceptsFocus(); }


        bool IsSelected() const { return stateFlags.isSelected; }
        void SetSelected(bool selected) { stateFlags.isSelected = selected; RequestRedraw(); }

//        bool IsDragging() const { return stateFlags.isDragging; }
//        void SetDragging(bool dragging) { stateFlags.isDragging = dragging; }

        ElementState GetPrimaryState() const {
            if (stateFlags.isDisabled) return ElementState::Disabled;
            if (stateFlags.isPressed) return ElementState::Pressed;
            if (stateFlags.isSelected) return ElementState::Selected;
            if (stateFlags.isHovered) return ElementState::Hovered;
            if (IsFocused()) return ElementState::Focused;
            return ElementState::Normal;
        }
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
        virtual void RenderPopupContent(IRenderContext* ctx) {};

        void AddThisPopupElementToWindow();
        void RemoveThisPopupElementFromWindow();

        void RequestRedraw();
//        void RequestFullRedraw();

        // ===== CORE VIRTUAL METHODS =====
        IRenderContext* GetRenderContext() const;
        virtual void Render(IRenderContext* ctx);

        virtual bool OnEvent(const UCEvent& event) {
            if (eventCallback) {
                return eventCallback(event);
            }
            return false;
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
    };

} // namespace UltraCanvas