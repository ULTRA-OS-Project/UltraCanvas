// include/UltraCanvasUIElement.h
// Modern C++ base class system for all UI components
// Version: 3.0.1
// Last Modified: 2025-01-17
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasConfig.h"
#include <iostream>
#include <string>
#include <vector>
#include <list>
#include <unordered_map>
#include <memory>
#include <functional>
#include <chrono>
#include <algorithm>

namespace UltraCanvas {

// Forward declarations
    class UltraCanvasContainer;
    class UltraCanvasUIElement;
    class UltraCanvasWindowBase;
    class UltraCanvasApplicationBase;
    class IRenderContext;

// ===== ELEMENT STATE MANAGEMENT =====
    namespace OverlayZOrder {
        constexpr int Background = -1000;    // Background elements
        constexpr int Content = 0;           // Main content areas
        constexpr int Controls = 100;        // Standard UI controls
        constexpr int Overlays = 200;        // Overlays, tooltips
        constexpr int Menus = 1000;          // Menu bars
        constexpr int Popups = 2000;         // Context menus, popups
        constexpr int Tooltips = 3000;       // Tooltips
    }

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

        ElementBorder(int w, const Color& c, int r, UCDashPattern p) : width(w), color(c), radius(r), dashPattern(p) {}
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

// ===== OverlayElementSetting =====
    struct PopupElementSettings {
        bool closeByEscapeKey = true;
        bool closeByClickOutside = true;
        std::weak_ptr<UltraCanvasUIElement> popupOwner;
    };

    enum class ClosePopupReason {
        Manual,
        EscapeKey,
        ClickOutside
    };

// ===== LEAF UI ELEMENT CLASS (NO CHILDREN) =====
    class UltraCanvasUIElement : public std::enable_shared_from_this<UltraCanvasUIElement>  {
    friend UltraCanvasWindowBase;
    protected:
        std::string identifier = "";
        bool needsUpdateGeometry = true;
        bool needsRedraw = true;

        // State properties
        bool visible = true;
        bool isPopup = false;

        int zOrder = 0;

        // Mouse interaction
        UCMouseCursor mouseCursor = UCMouseCursor::Default;

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
        std::function<bool(const UCEvent&)> eventCallback;
        std::function<bool(ClosePopupReason)> onPopupAboutToClose;
        std::function<void(ClosePopupReason)> onPopupClosed;
        std::function<void()> onPopupOpened;

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

        virtual ~UltraCanvasUIElement();

        // ===== INCLUDE PROPERTY ACCESSORS =====
        const std::string& GetIdentifier() const { return identifier; }
        void SetIdentifier(const std::string& id) { identifier = id; }

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

        int GetXInWindow();
        int GetYInWindow();

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
        void SetX(int x) { SetBounds(x, bounds.y, bounds.width, bounds.height); }
        int GetY() const { return bounds.y; }
        void SetY(int y) { SetBounds(bounds.x, y, bounds.width, bounds.height); }

        void SetPosition(int x, int y) { SetBounds(x, y, bounds.width, bounds.height); }
        void SetPosition(const Point2Di& pos) { SetBounds(pos.x, pos.y, bounds.width, bounds.height); }
        void SetSize(int w, int h) { SetBounds(bounds.x, bounds.y, w, h); }
        void SetSize(const Size2Di& sz) { SetBounds(bounds.x, bounds.y, sz.width, sz.height); }
        virtual void SetOriginalSize(int w, int h);
        void SetBounds(int x, int y, int w, int h) {
            SetBounds(Rect2Di(x, y, w, h));
        }
        void SetBounds(float x, float y, float w, float h) {
            SetBounds(Rect2Di(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w), static_cast<int>(h)));
        }
        virtual void SetBounds(const Rect2Di& b) {
            if (bounds != b) {
                bounds = b;
                RequestUpdateGeometry();
            }
        }

        Point2Di GetPositionInWindow() const;
        Rect2Di GetBoundsInWindow() const {
            return GetBounds().SetPosition(GetPositionInWindow());
        }

        // ===== CONVENIENCE SETTERS - MARGIN =====
        void SetMargin(int all) {
            margin.left = margin.right = margin.top = margin.bottom = all;
            RequestUpdateGeometry();
        }

        void SetMargin(int vertical, int horizontal) {
            margin.left = margin.right = horizontal;
            margin.top = margin.bottom = vertical;
            RequestUpdateGeometry();
        }

        void SetMargin(int top, int right, int bottom, int left) {
            margin.left = left;
            margin.top = top;
            margin.right = right;
            margin.bottom = bottom;
            RequestUpdateGeometry();
        }

        // ===== CONVENIENCE SETTERS - PADDING =====
        void SetPadding(int all) {
            padding.left = padding.right = padding.top = padding.bottom = all;
            RequestUpdateGeometry();
        }

        void SetPadding(int horizontal, int vertical) {
            padding.left = padding.right = horizontal;
            padding.top = padding.bottom = vertical;
            RequestUpdateGeometry();
        }

        void SetPadding(int top, int right, int bottom, int left) {
            padding.left = left;
            padding.top = top;
            padding.right = right;
            padding.bottom = bottom;
            RequestUpdateGeometry();
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
                RequestUpdateGeometry();
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
                RequestUpdateGeometry();
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
                RequestUpdateGeometry();
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
                RequestUpdateGeometry();
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

        UCMouseCursor GetMouseCursor() const { return mouseCursor; }
        void SetMouseCursor(UCMouseCursor cur) { mouseCursor = cur; }

        int GetZOrder() const { return zOrder; }
        void SetZIndex(int index) { zOrder = index; }

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


        // ===== CORE VIRTUAL METHODS =====
        IRenderContext* GetRenderContext() const;
        virtual void Render(IRenderContext* ctx);
        virtual void UpdateGeometry(IRenderContext* ctx);

        // ===== EVENT HANDLING =====
        virtual bool OnEvent(const UCEvent& event);
        virtual bool OnEventFilter(const UCEvent& event) { return false; };

        void SetEventCallback(std::function<bool(const UCEvent&)> callback);

        bool IsNeedsRedraw() const { return needsRedraw; }
        bool IsNeedsUpdateGeometry() const { return needsUpdateGeometry; }

        void RequestRedraw();
        void RequestUpdateGeometry();

        // ===== UTILITY METHODS =====
        UltraCanvasContainer* GetRootContainer();

        bool IsDescendantOf(const UltraCanvasContainer* container) const;

        std::string GetDebugInfo() const {
            return "Element{id='" + GetIdentifier() +
                   "', bounds=(" + std::to_string(GetX()) + "," + std::to_string(GetY()) +
                   "," + std::to_string(GetWidth()) + "," + std::to_string(GetHeight()) +
                   "), visible=" + (IsVisible() ? "true" : "false") + "}";
        }

    protected:
        virtual bool OnPopupAboutToClose(ClosePopupReason reason) {
            if (onPopupAboutToClose) return onPopupAboutToClose(reason);
            return true;
        }

        virtual void OnPopupClosed(ClosePopupReason reason) {
            if (onPopupClosed) onPopupClosed(reason);
        }

        virtual void OnPopupOpened() {
            if (onPopupOpened) onPopupOpened();
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