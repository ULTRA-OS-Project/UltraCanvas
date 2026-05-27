// include/UltraCanvasUIElement.h
// Modern C++ base class system for all UI components.
// Inherits from UltraCanvas::CSSLayout::Element so the CSS layout engine
// can drive geometry. UI-only concerns (visibility, hover/press state,
// border *visual* properties, render context, window, tooltip) stay on
// this class; geometry, box model, identifier, parent link, z-index live
// on the engine base.
// Version: 4.0.0
// Last Modified: 2026-05-27
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasConfig.h"
#include "CSSLayout/CSSLayout.h"
#include <iostream>
#include <string>
#include <vector>
#include <list>
#include <unordered_map>
#include <memory>
#include <functional>
#include <chrono>
#include <algorithm>
#include <optional>

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

    // Per-side visual properties of a border. Width lives in
    // CSSLayout::BoxModel::border (Dimension-typed) on the engine base.
    struct BorderSideVisual {
        Color color = Color(0, 0, 0, 255);
        int radius = 0;                  // legacy per-side radius (CSS border-radius is per-corner)
        UCDashPattern dashPattern;
    };

    struct BordersVisual {
        BorderSideVisual left, right, top, bottom;
    };

    struct ElementStateFlags {
        bool isHovered = false;
        bool isPressed = false;
        bool isDisabled = false;
        bool isSelected = false;

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

    // Internal helper: resolve a Dimension to integer pixels for the UI
    // back-compat getters (which return int). Non-Px units (auto/%/fr/etc.)
    // collapse to 0 — these are box-model widths set by the UI layer, which
    // historically only used integer pixels.
    inline int dimPx(const CSSLayout::Dimension& d) {
        return d.unit == CSSLayout::DimensionUnit::Pixels ? (int)d.value : 0;
    }

// ===== UI BASE CLASS =====
    class UltraCanvasUIElement
            : public CSSLayout::Element,
              public std::enable_shared_from_this<UltraCanvasUIElement> {
    friend UltraCanvasWindowBase;
    friend UltraCanvasContainer;
    protected:
        bool needsUpdateGeometry = true;
        bool visible = true;
        bool isPopup = false;

        std::unique_ptr<IRenderContext> renderContext = nullptr;
        UCMouseCursor mouseCursor = UCMouseCursor::Default;
        std::string tooltip;
        UltraCanvasWindowBase* window = nullptr;
        ElementStateFlags stateFlags;
        Color backgroundColor = Colors::Transparent;

        // Visual properties of borders (color/radius/dash per side). Widths
        // live in the inherited CSSLayout::BoxModel::border. Lazily
        // initialised by the first border setter call.
        std::optional<BordersVisual> bordersVisual;

    public:
        std::function<bool(const UCEvent&)> eventCallback;
        std::function<bool(ClosePopupReason)> onPopupAboutToClose;
        std::function<void(ClosePopupReason)> onPopupClosed;
        std::function<void()> onPopupOpened;

        // ===== CONSTRUCTOR AND DESTRUCTOR =====
        UltraCanvasUIElement(const std::string& idstr,
                             int x, int y, int w, int h) {
            id = idstr;
            finalBounds = CSSLayout::LayoutRect{(float)x, (float)y, (float)w, (float)h};
            if (w > 0) size.width  = CSSLayout::Dimension::Px((float)w);
            if (h > 0) size.height = CSSLayout::Dimension::Px((float)h);
            stateFlags.Reset();
        }

        explicit UltraCanvasUIElement(const std::string& idstr = "",
                             int w = 0, int h = 0) {
            id = idstr;
            finalBounds = CSSLayout::LayoutRect{0.f, 0.f, (float)w, (float)h};
            if (w > 0) size.width  = CSSLayout::Dimension::Px((float)w);
            if (h > 0) size.height = CSSLayout::Dimension::Px((float)h);
            stateFlags.Reset();
        }

        ~UltraCanvasUIElement() override;

        // ===== IDENTIFIER (wraps inherited Element::id) =====
        const std::string& GetIdentifier() const { return id; }
        void SetIdentifier(const std::string& newId) { id = newId; }

        // ===== BOUNDS (wraps inherited Element::finalBounds) =====
        Rect2Di GetBounds() const {
            return Rect2Di((int)finalBounds.x, (int)finalBounds.y,
                           (int)finalBounds.width, (int)finalBounds.height);
        }

        Rect2Di GetLocalBounds() const {
            return Rect2Di(0, 0, (int)finalBounds.width, (int)finalBounds.height);
        }

        Point2Di GetPosition() {
            return Point2Di((int)finalBounds.x, (int)finalBounds.y);
        }

        Size2Di GetSize() {
            return Size2Di((int)finalBounds.width, (int)finalBounds.height);
        }

        Size2Di GetOriginalSize() {
            return Size2Di(dimPx(size.width), dimPx(size.height));
        }

        virtual bool Contains(const Point2Di& point) {
            return GetLocalBounds().Contains(point);
        }

        virtual bool ContainsInWindow(const Point2Di& point) {
            return GetBoundsInWindow().Contains(point);
        }

        int GetXInWindow();
        int GetYInWindow();

        int GetWidth() const { return (int)finalBounds.width; }
        virtual int GetPreferredWidth() { return dimPx(size.width); }
        virtual int GetMinWidth() const { return 0; }
        virtual int GetMaxWidth() const { return 10000; }
        void SetWidth(int w) { SetBounds((int)finalBounds.x, (int)finalBounds.y, w, (int)finalBounds.height); }

        int GetHeight() const { return (int)finalBounds.height; }
        virtual int GetPreferredHeight() { return dimPx(size.height); }
        virtual int GetMinHeight() const { return 0; }
        virtual int GetMaxHeight() const { return 10000; }
        void SetHeight(int h) { SetBounds((int)finalBounds.x, (int)finalBounds.y, (int)finalBounds.width, h); }

        int GetX() const { return (int)finalBounds.x; }
        void SetX(int x) { SetBounds(x, (int)finalBounds.y, (int)finalBounds.width, (int)finalBounds.height); }
        int GetY() const { return (int)finalBounds.y; }
        void SetY(int y) { SetBounds((int)finalBounds.x, y, (int)finalBounds.width, (int)finalBounds.height); }

        void SetPosition(int x, int y) { SetBounds(x, y, (int)finalBounds.width, (int)finalBounds.height); }
        void SetPosition(const Point2Di& pos) { SetBounds(pos.x, pos.y, (int)finalBounds.width, (int)finalBounds.height); }
        void SetSize(int w, int h) { SetBounds((int)finalBounds.x, (int)finalBounds.y, w, h); }
        void SetSize(const Size2Di& sz) { SetBounds((int)finalBounds.x, (int)finalBounds.y, sz.width, sz.height); }
        virtual void SetOriginalSize(int w, int h);
        void SetBounds(int x, int y, int w, int h) {
            SetBounds(Rect2Di(x, y, w, h));
        }
        void SetBounds(float x, float y, float w, float h) {
            SetBounds(Rect2Di(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w), static_cast<int>(h)));
        }
        virtual void SetBounds(const Rect2Di& b);

        Point2Di GetPositionInWindow() const;
        Rect2Di GetBoundsInWindow() const {
            return GetBounds().SetPosition(GetPositionInWindow());
        }

        // ===== MARGIN SETTERS (write through to BoxModel::margin) =====
        void SetMargin(int all) {
            auto px = CSSLayout::Dimension::Px((float)all);
            box.margin.left = box.margin.right = box.margin.top = box.margin.bottom = px;
            RequestUpdateGeometry();
        }

        void SetMargin(int vertical, int horizontal) {
            box.margin.left   = box.margin.right  = CSSLayout::Dimension::Px((float)horizontal);
            box.margin.top    = box.margin.bottom = CSSLayout::Dimension::Px((float)vertical);
            RequestUpdateGeometry();
        }

        void SetMargin(int top, int right, int bottom, int left) {
            box.margin.left   = CSSLayout::Dimension::Px((float)left);
            box.margin.top    = CSSLayout::Dimension::Px((float)top);
            box.margin.right  = CSSLayout::Dimension::Px((float)right);
            box.margin.bottom = CSSLayout::Dimension::Px((float)bottom);
            RequestUpdateGeometry();
        }

        // ===== PADDING SETTERS (write through to BoxModel::padding) =====
        void SetPadding(int all) {
            auto px = CSSLayout::Dimension::Px((float)all);
            box.padding.left = box.padding.right = box.padding.top = box.padding.bottom = px;
            RequestUpdateGeometry();
        }

        void SetPadding(int horizontal, int vertical) {
            box.padding.left   = box.padding.right  = CSSLayout::Dimension::Px((float)horizontal);
            box.padding.top    = box.padding.bottom = CSSLayout::Dimension::Px((float)vertical);
            RequestUpdateGeometry();
        }

        void SetPadding(int top, int right, int bottom, int left) {
            box.padding.left   = CSSLayout::Dimension::Px((float)left);
            box.padding.top    = CSSLayout::Dimension::Px((float)top);
            box.padding.right  = CSSLayout::Dimension::Px((float)right);
            box.padding.bottom = CSSLayout::Dimension::Px((float)bottom);
            RequestUpdateGeometry();
        }

        // ===== BORDER SETTERS =====
        void SetBorders(int width, const Color& color = Colors::Black, int borderRadius = 0, const UCDashPattern& dash = UCDashPattern()) {
            SetBorderLeft(width, color, borderRadius, dash);
            SetBorderRight(width, color, borderRadius, dash);
            SetBorderTop(width, color, borderRadius, dash);
            SetBorderBottom(width, color, borderRadius, dash);
        }

        void SetBordersColor(const Color& color) {
            if (!bordersVisual) bordersVisual.emplace();
            bordersVisual->left.color = color;
            bordersVisual->right.color = color;
            bordersVisual->top.color = color;
            bordersVisual->bottom.color = color;
            RequestRedraw();
        }

        void SetBorderLeft(int width, const Color& color = Colors::Black, int borderRadius = 0, const UCDashPattern& dash = UCDashPattern()) {
            box.border.left = CSSLayout::Dimension::Px((float)width);
            if (!bordersVisual) bordersVisual.emplace();
            bordersVisual->left.color = color;
            bordersVisual->left.radius = borderRadius;
            bordersVisual->left.dashPattern = dash;
            RequestUpdateGeometry();
        }

        void SetBorderRight(int width, const Color& color = Colors::Black, int borderRadius = 0, const UCDashPattern& dash = UCDashPattern()) {
            box.border.right = CSSLayout::Dimension::Px((float)width);
            if (!bordersVisual) bordersVisual.emplace();
            bordersVisual->right.color = color;
            bordersVisual->right.radius = borderRadius;
            bordersVisual->right.dashPattern = dash;
            RequestUpdateGeometry();
        }

        void SetBorderTop(int width, const Color& color = Colors::Black, int borderRadius = 0, const UCDashPattern& dash = UCDashPattern()) {
            box.border.top = CSSLayout::Dimension::Px((float)width);
            if (!bordersVisual) bordersVisual.emplace();
            bordersVisual->top.color = color;
            bordersVisual->top.radius = borderRadius;
            bordersVisual->top.dashPattern = dash;
            RequestUpdateGeometry();
        }

        void SetBorderBottom(int width, const Color& color = Colors::Black, int borderRadius = 0, const UCDashPattern& dash = UCDashPattern()) {
            box.border.bottom = CSSLayout::Dimension::Px((float)width);
            if (!bordersVisual) bordersVisual.emplace();
            bordersVisual->bottom.color = color;
            bordersVisual->bottom.radius = borderRadius;
            bordersVisual->bottom.dashPattern = dash;
            RequestUpdateGeometry();
        }

        void SetBackgroundColor(const Color& color) {
            backgroundColor = color;
        }

        const Color& GetBackgroundColor() {
            return backgroundColor;
        }

        // ===== TOTAL CALCULATIONS =====
        int GetTotalMarginHorizontal() const {
            return dimPx(box.margin.left) + dimPx(box.margin.right);
        }

        int GetTotalMarginVertical() const {
            return dimPx(box.margin.top) + dimPx(box.margin.bottom);
        }

        int GetTotalPaddingHorizontal() const {
            return dimPx(box.padding.left) + dimPx(box.padding.right);
        }

        int GetTotalPaddingVertical() const {
            return dimPx(box.padding.top) + dimPx(box.padding.bottom);
        }

        int GetTotalBorderHorizontal() const {
            return GetBorderLeftWidth() + GetBorderRightWidth();
        }

        int GetTotalBorderVertical() const {
            return GetBorderTopWidth() + GetBorderBottomWidth();
        }

        // ===== CONTENT AREA CALCULATIONS =====
        // client area = bounds minus (border + padding), in parent coords
        Rect2Di GetContentRect() const {
            const int x = (int)finalBounds.x;
            const int y = (int)finalBounds.y;
            const int w = (int)finalBounds.width;
            const int h = (int)finalBounds.height;
            return Rect2Di(
                    x + GetBorderLeftWidth() + dimPx(box.padding.left),
                    y + GetBorderTopWidth()  + dimPx(box.padding.top),
                    w - (GetTotalBorderHorizontal() + GetTotalPaddingHorizontal()),
                    h - (GetTotalBorderVertical()   + GetTotalPaddingVertical())
            );
        }

        // local client area = bounds in element coords (0,0 origin) minus (border + padding)
        Rect2Di GetLocalContentRect() const {
            const int w = (int)finalBounds.width;
            const int h = (int)finalBounds.height;
            return Rect2Di(
                    GetBorderLeftWidth() + dimPx(box.padding.left),
                    GetBorderTopWidth()  + dimPx(box.padding.top),
                    w - (GetTotalBorderHorizontal() + GetTotalPaddingHorizontal()),
                    h - (GetTotalBorderVertical()   + GetTotalPaddingVertical())
            );
        }

        // Padding box = bounds minus border
        Rect2Di GetPaddingRect() {
            const int x = (int)finalBounds.x;
            const int y = (int)finalBounds.y;
            const int w = (int)finalBounds.width;
            const int h = (int)finalBounds.height;
            return Rect2Di(
                    x + GetBorderLeftWidth(),
                    y + GetBorderTopWidth(),
                    w - GetTotalBorderHorizontal(),
                    h - GetTotalBorderVertical()
            );
        }

        // Margin box = bounds plus margins
        Rect2Di GetMarginRect() {
            const int x = (int)finalBounds.x;
            const int y = (int)finalBounds.y;
            const int w = (int)finalBounds.width;
            const int h = (int)finalBounds.height;
            return Rect2Di(
                    x - dimPx(box.margin.left),
                    y - dimPx(box.margin.top),
                    w + dimPx(box.margin.left) + dimPx(box.margin.right),
                    h + dimPx(box.margin.top)  + dimPx(box.margin.bottom)
            );
        }

        int GetMarginLeft() const   { return dimPx(box.margin.left); }
        int GetMarginRight() const  { return dimPx(box.margin.right); }
        int GetMarginTop() const    { return dimPx(box.margin.top); }
        int GetMarginBottom() const { return dimPx(box.margin.bottom); }

        int GetPaddingLeft() const   { return dimPx(box.padding.left); }
        int GetPaddingRight() const  { return dimPx(box.padding.right); }
        int GetPaddingTop() const    { return dimPx(box.padding.top); }
        int GetPaddingBottom() const { return dimPx(box.padding.bottom); }

        int GetBorderLeftWidth() const   { return dimPx(box.border.left); }
        int GetBorderRightWidth() const  { return dimPx(box.border.right); }
        int GetBorderTopWidth() const    { return dimPx(box.border.top); }
        int GetBorderBottomWidth() const { return dimPx(box.border.bottom); }

        bool HasBorder() const {
            return GetBorderLeftWidth() > 0 || GetBorderRightWidth() > 0 ||
                   GetBorderTopWidth() > 0  || GetBorderBottomWidth() > 0;
        }

        bool HasPadding() const {
            return GetPaddingLeft() > 0 || GetPaddingRight() > 0 ||
                   GetPaddingTop() > 0  || GetPaddingBottom() > 0;
        }

        bool HasMargin() const {
            return GetMarginLeft() > 0 || GetMarginRight() > 0 ||
                   GetMarginTop() > 0  || GetMarginBottom() > 0;
        }

        // Coordinate mapping (unchanged signatures; implementation uses GetParentContainer()).
        Point2Di MapFromLocal(const Point2Di &localPos, UltraCanvasContainer* mapToParent = nullptr);
        Point2Di MapToLocal(const Point2Di &globalPos, UltraCanvasContainer* mapFromParent = nullptr);

        UCMouseCursor GetMouseCursor() const { return mouseCursor; }
        void SetMouseCursor(UCMouseCursor cur) { mouseCursor = cur; }

        // ===== Z-INDEX (wraps inherited Element::zIndex) =====
        int GetZOrder() const { return zIndex; }
        int GetZIndex() const { return zIndex; }
        void SetZIndex(int index) { zIndex = index; }

        const std::string& GetTooltip() const { return tooltip; }
        void SetTooltip(const std::string& tooltipStr) { tooltip = tooltipStr; }

        bool IsVisible() const { return visible; }
        void SetVisible(bool visible);

        bool IsDisabled() const { return stateFlags.isDisabled; }
        void SetDisabled(bool disabled) { stateFlags.isDisabled = disabled; RequestRedraw(); }

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

        ElementState GetPrimaryState() const {
            if (stateFlags.isDisabled) return ElementState::Disabled;
            if (stateFlags.isPressed) return ElementState::Pressed;
            if (stateFlags.isSelected) return ElementState::Selected;
            if (stateFlags.isHovered) return ElementState::Hovered;
            if (IsFocused()) return ElementState::Focused;
            return ElementState::Normal;
        }
        const ElementStateFlags& GetStateFlags() const { return stateFlags; }

        // ===== PARENT CONTAINER (wraps inherited Element::Parent()) =====
        UltraCanvasContainer* GetParentContainer() const;

        UltraCanvasWindowBase* GetWindow() const {
            return window;
        }

        virtual void SetWindow(UltraCanvasWindowBase* win);

        // ===== CORE VIRTUAL METHODS =====
        IRenderContext* GetRenderContext() const;
        // dirtyRect is in element-local coordinates (matches the translated ctx).
        virtual void Render(IRenderContext* ctx, const Rect2Di& dirtyRect);
        virtual void UpdateGeometry(IRenderContext* ctx) {};

        // ===== EVENT HANDLING =====
        virtual bool OnEvent(const UCEvent& event);
        virtual bool OnEventFilter(const UCEvent& event) { return false; };

        void SetEventCallback(std::function<bool(const UCEvent&)> callback);

        bool IsNeedsUpdateGeometry() const { return needsUpdateGeometry; }

        // Adds localRect (in this element's local coords) to the appropriate
        // dirty-rect manager: the containing popup's, or the window's.
        virtual void InvalidateRect(const Rect2Di& localRect);
        void RequestRedraw();
        // TODO: when the CSSLayout engine takes over the UpdateGeometry/Render
        // pipeline, this should also call InvalidateLayout() so the engine
        // caches drop.
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
