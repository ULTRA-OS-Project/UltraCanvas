// include/UltraCanvasUIElement.h
// Modern C++ base class system for all UI components.
// Inherits from UltraCanvas::CSSLayout::Element so the CSS layout engine
// can drive geometry. UI-only concerns (visibility, hover/press state,
// border *visual* properties, render context, window, tooltip) stay on
// this class; geometry, box model, identifier, parent link, z-index live
// on the engine base.
// Version: 4.4.0 - GetPopupShadow: a soft drop shadow the window paints under a popup
// Version: 4.3.1 - default bodies mark unused parameters (void): no -Wunused-parameter
// Version: 4.3.0 - accessible name/description overrides, toggle state, range value, value text and default action
// Version: 4.2.0 - ArrangeOwnBox: take a box without laying the children out
// Version: 4.1.0 - SetBorderRadius: rounded corners without a border
// Last Modified: 2026-10-10
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasTooltipTypes.h"
#include "UltraCanvasAccessibility.h"
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
        float radius = 0.0f;             // legacy per-side radius (CSS border-radius is per-corner)
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
        bool isInteractive = true;

        void Reset() {
            isDisabled = isHovered = isPressed = isSelected = false;
            isInteractive  = true;
        }
    };

// ===== OverlayElementSetting =====
    struct PopupElementSettings {
        bool closeByEscapeKey = true;
        bool closeByClickOutside = true;
        std::weak_ptr<UltraCanvasUIElement> popupOwner;
    };

    // A soft drop shadow under a popup (see UltraCanvasUIElement::
    // GetPopupShadow). The window paints it outside the popup's own surface,
    // beneath it, so the popup keeps its bounds and the shadow catches no
    // clicks. Its shape is the popup's outline - a rounded rectangle of
    // `cornerRadius` - moved by `offset`; `blur` is the width of the fade
    // from `color` to nothing, centred on that outline as a CSS box-shadow's
    // blur is, so the shadow reaches blur / 2 past it at half strength.
    struct PopupShadow {
        Color color = Colors::Transparent;   // alpha 0: no shadow
        int blur = 0;
        Point2Di offset = Point2Di(0, 0);
        float cornerRadius = 0.0f;

        bool IsVisible() const { return color.a > 0; }
        bool operator==(const PopupShadow& o) const {
            return color == o.color && blur == o.blur && offset == o.offset &&
                   cornerRadius == o.cornerRadius;
        }
        bool operator!=(const PopupShadow& o) const { return !(*this == o); }
        // How far the shadow reaches past the popup's bounds on each side.
        int Reach() const { return (std::max(0, blur) + 1) / 2; }
        int MarginLeft() const { return IsVisible() ? std::max(0, Reach() - offset.x) : 0; }
        int MarginTop() const { return IsVisible() ? std::max(0, Reach() - offset.y) : 0; }
        int MarginRight() const { return IsVisible() ? std::max(0, Reach() + offset.x) : 0; }
        int MarginBottom() const { return IsVisible() ? std::max(0, Reach() + offset.y) : 0; }
    };

    enum class ClosePopupReason {
        Manual,
        EscapeKey,
        ClickOutside
    };

    // Internal helper: resolve a Dimension to float pixels for the UI
    // coordinate getters. Non-Px units (auto/%/fr/etc.) collapse to 0.
    inline float dimPx(const CSSLayout::Dimension& d) {
        return d.unit == CSSLayout::DimensionUnit::Pixels ? d.value : 0.0f;
    }

// ===== UI BASE CLASS =====
    class UltraCanvasUIElement
            : public CSSLayout::Element,
              public std::enable_shared_from_this<UltraCanvasUIElement> {
    friend UltraCanvasWindowBase;
    friend UltraCanvasContainer;
    protected:
        CSSLayout::DisplayType prevDisplayType = CSSLayout::DisplayType::Block;
        bool isPopup = false;

        std::unique_ptr<IRenderContext> renderContext = nullptr;
        UCMouseCursor mouseCursor = UCMouseCursor::Default;
        std::string tooltip;
        std::shared_ptr<TooltipContent> tooltipContent;  // structured tooltip; wins over `tooltip`
        std::string accessibleName;          // SetAccessibleName(); empty = the element's own
        std::string accessibleDescription;   // SetAccessibleDescription(); empty = the tooltip
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
        // NOTE: Passing non-zero w/h here stamps a CSS `size.width/height`
        // on the element. An explicit width/height OVERRIDES the parent's
        // stretch (align-items / justify-items: Stretch), as in CSS; the
        // element's own align-self / justify-self: Stretch still stretches it,
        // and flex-grow still grows the main axis. For widgets you want the
        // engine to size — children of a flex/grid container, or any widget
        // that should stretch to its parent — use the no-size constructor
        // (or pass 0, 0) and let the parent decide; the parent stretches
        // only when its layout asks for it. Pass non-zero w/h only when you
        // genuinely want a fixed-size box.
        UltraCanvasUIElement(const std::string& idstr,
                             float x, float y, float w, float h) {
            id = idstr;
            finalBounds = Rect2Df{x, y, w, h};
            box.boxSizing = CSSLayout::BoxSizing::BorderBox;
            if (w > 0) size.width  = CSSLayout::Dimension::Px(w);
            if (h > 0) size.height = CSSLayout::Dimension::Px(h);
            if (x > 0 || y > 0) {
                // A widget given an explicit non-zero origin is a legacy
                // absolutely-placed element: it must keep that (x, y) when its
                // parent runs the layout engine, instead of being re-stacked as an
                // in-flow child. We only do this for a real offset — flex/grid
                // children are conventionally built with (0, 0, w, h) and must stay
                // in flow so the parent's algorithm can place them.
                layoutItem.position = CSSLayout::Position();
                layoutItem.position->left = CSSLayout::Dimension::Px(x);
                layoutItem.position->top = CSSLayout::Dimension::Px(y);
                layoutItem.SetPositionType(CSSLayout::PositionType::AbsoluteUI);
            }
            stateFlags.Reset();
        }

        explicit UltraCanvasUIElement(const std::string& idstr = "",
                             float w = 0, float h = 0) {
            id = idstr;
            finalBounds = Rect2Df{0.f, 0.f, w, h};
            if (w > 0) size.width  = CSSLayout::Dimension::Px(w);
            if (h > 0) size.height = CSSLayout::Dimension::Px(h);
            stateFlags.Reset();
        }

        ~UltraCanvasUIElement() override;

        // ===== IDENTIFIER (wraps inherited Element::id) =====
        const std::string& GetIdentifier() const { return id; }
        void SetIdentifier(const std::string& newId) { id = newId; }

        // ===== BOUNDS (wraps inherited Element::finalBounds) =====
        Rect2Df GetBounds() const {
            return Rect2Df(finalBounds.x, finalBounds.y,
                           finalBounds.width, finalBounds.height);
        }

        Rect2Df GetLocalBounds() const {
            return Rect2Df(0.0f, 0.0f, finalBounds.width, finalBounds.height);
        }

        Point2Df GetPosition() {
            return Point2Df(finalBounds.x, finalBounds.y);
        }

        Size2Df GetSize() {
            return Size2Df(finalBounds.width, finalBounds.height);
        }

        Size2Df GetOriginalSize() {
            return Size2Df(dimPx(size.width), dimPx(size.height));
        }

        virtual bool Contains(const Point2Df& point) {
            return GetLocalBounds().Contains(point);
        }

        virtual bool ContainsInWindow(const Point2Df& point) {
            return GetBoundsInWindow().Contains(point);
        }

        float GetXInWindow();
        float GetYInWindow();

        float GetWidth() const { return finalBounds.width; }
        void SetWidth(float w) { SetBounds(finalBounds.x, finalBounds.y, w, finalBounds.height); }

        float GetHeight() const { return finalBounds.height; }
        void SetHeight(float h) { SetBounds(finalBounds.x, finalBounds.y, finalBounds.width, h); }

        float GetX() const { return finalBounds.x; }
        void SetX(float x) { SetBounds(x, finalBounds.y, finalBounds.width, finalBounds.height); }
        float GetY() const { return finalBounds.y; }
        void SetY(float y) { SetBounds(finalBounds.x, y, finalBounds.width, finalBounds.height); }

        // if possible dont use these SetSize/SetPosition as it may break layout, use SetElementSize/SetElementAbsolutePosition
        void SetPosition(float x, float y) { SetBounds(x, y, finalBounds.width, finalBounds.height); }
        void SetPosition(const Point2Df& pos) { SetBounds(pos.x, pos.y, finalBounds.width, finalBounds.height); }
        void SetSize(float w, float h) { SetBounds(finalBounds.x, finalBounds.y, w, h); }
        void SetSize(const Size2Df& sz) { SetBounds(finalBounds.x, finalBounds.y, sz.width, sz.height); }
        void SetBounds(float x, float y, float w, float h) {
            SetBounds(Rect2Df(x, y, w, h));
        }
        virtual void SetBounds(const Rect2Df& b);

        void SetElementAbsolutePosition(const Point2Df& pos);
        void SetElementSize(const Size2Df & sz);
        void SetElementSize(const CSSLayout::Dimension &w, const CSSLayout::Dimension &h);

        // True while this element is open as a popup on its window (menu,
        // dropdown list, date-picker calendar, ...). Popups are rendered into
        // their own surface and composited above the window content, so the
        // compositor uses this to work out what stacks above what.
        bool IsPopupElement() const { return isPopup; }

        // The drop shadow the window paints under this element while it is
        // open as a popup. None by default; a menu returns its style's.
        // Popups are composited over the window with their alpha, so a popup
        // that leaves its corners transparent shows them rounded.
        virtual PopupShadow GetPopupShadow() const { return PopupShadow(); }

        virtual Point2Df GetPositionInWindow() const;
        Rect2Df GetBoundsInWindow() const {
            return GetBounds().SetPosition(GetPositionInWindow());
        }

        // ===== MARGIN SETTERS (write through to BoxModel::margin) =====
        void SetMargin(float all) {
            auto px = CSSLayout::Dimension::Px(all);
            box.margin.left = box.margin.right = box.margin.top = box.margin.bottom = px;
            InvalidateLayout();
        }

        void SetMargin(float vertical, float horizontal) {
            box.margin.left   = box.margin.right  = CSSLayout::Dimension::Px(horizontal);
            box.margin.top    = box.margin.bottom = CSSLayout::Dimension::Px(vertical);
            InvalidateLayout();
        }

        void SetMargin(float top, float right, float bottom, float left) {
            box.margin.left   = CSSLayout::Dimension::Px(left);
            box.margin.top    = CSSLayout::Dimension::Px(top);
            box.margin.right  = CSSLayout::Dimension::Px(right);
            box.margin.bottom = CSSLayout::Dimension::Px(bottom);
            InvalidateLayout();
        }

        // ===== PADDING SETTERS (write through to BoxModel::padding) =====
        void SetPadding(float all) {
            auto px = CSSLayout::Dimension::Px(all);
            box.padding.left = box.padding.right = box.padding.top = box.padding.bottom = px;
            InvalidateLayout();
        }

        void SetPadding(float vertical, float horizontal) {
            box.padding.left   = box.padding.right  = CSSLayout::Dimension::Px(horizontal);
            box.padding.top    = box.padding.bottom = CSSLayout::Dimension::Px(vertical);
            InvalidateLayout();
        }

        void SetPadding(float top, float right, float bottom, float left) {
            box.padding.top    = CSSLayout::Dimension::Px(top);
            box.padding.right  = CSSLayout::Dimension::Px(right);
            box.padding.bottom = CSSLayout::Dimension::Px(bottom);
            box.padding.left   = CSSLayout::Dimension::Px(left);
            InvalidateLayout();
        }

        // ===== BORDER SETTERS =====
        void SetBorders(float width, const Color& color = Colors::Black, float borderRadius = 0.0f, const UCDashPattern& dash = UCDashPattern()) {
            SetBorderLeft(width, color, borderRadius, dash);
            SetBorderRight(width, color, borderRadius, dash);
            SetBorderTop(width, color, borderRadius, dash);
            SetBorderBottom(width, color, borderRadius, dash);
        }

        // Rounded corners with or without a border: the background (and any
        // border) is drawn with this radius. Border widths are left as they
        // are - a box can be rounded and borderless (a mail button).
        void SetBorderRadius(float radius) {
            if (!bordersVisual) bordersVisual.emplace();
            bordersVisual->left.radius = radius;
            bordersVisual->right.radius = radius;
            bordersVisual->top.radius = radius;
            bordersVisual->bottom.radius = radius;
            RequestRedraw();
        }

        void SetBordersColor(const Color& color) {
            if (!bordersVisual) bordersVisual.emplace();
            bordersVisual->left.color = color;
            bordersVisual->right.color = color;
            bordersVisual->top.color = color;
            bordersVisual->bottom.color = color;
            RequestRedraw();
        }

        void SetBorderLeft(float width, const Color& color = Colors::Black, float borderRadius = 0.0f, const UCDashPattern& dash = UCDashPattern()) {
            box.border.left = CSSLayout::Dimension::Px(width);
            if (!bordersVisual) bordersVisual.emplace();
            bordersVisual->left.color = color;
            bordersVisual->left.radius = borderRadius;
            bordersVisual->left.dashPattern = dash;
            InvalidateLayout();
        }

        void SetBorderRight(float width, const Color& color = Colors::Black, float borderRadius = 0.0f, const UCDashPattern& dash = UCDashPattern()) {
            box.border.right = CSSLayout::Dimension::Px(width);
            if (!bordersVisual) bordersVisual.emplace();
            bordersVisual->right.color = color;
            bordersVisual->right.radius = borderRadius;
            bordersVisual->right.dashPattern = dash;
            InvalidateLayout();
        }

        void SetBorderTop(float width, const Color& color = Colors::Black, float borderRadius = 0.0f, const UCDashPattern& dash = UCDashPattern()) {
            box.border.top = CSSLayout::Dimension::Px(width);
            if (!bordersVisual) bordersVisual.emplace();
            bordersVisual->top.color = color;
            bordersVisual->top.radius = borderRadius;
            bordersVisual->top.dashPattern = dash;
            InvalidateLayout();
        }

        void SetBorderBottom(float width, const Color& color = Colors::Black, float borderRadius = 0.0f, const UCDashPattern& dash = UCDashPattern()) {
            box.border.bottom = CSSLayout::Dimension::Px(width);
            if (!bordersVisual) bordersVisual.emplace();
            bordersVisual->bottom.color = color;
            bordersVisual->bottom.radius = borderRadius;
            bordersVisual->bottom.dashPattern = dash;
            InvalidateLayout();
        }

        void SetBackgroundColor(const Color& color) {
            backgroundColor = color;
        }

        const Color& GetBackgroundColor() {
            return backgroundColor;
        }

        // ===== TOTAL CALCULATIONS =====
        float GetTotalMarginHorizontal() const {
            return dimPx(box.margin.left) + dimPx(box.margin.right);
        }

        float GetTotalMarginVertical() const {
            return dimPx(box.margin.top) + dimPx(box.margin.bottom);
        }

        float GetTotalPaddingHorizontal() const {
            return dimPx(box.padding.left) + dimPx(box.padding.right);
        }

        float GetTotalPaddingVertical() const {
            return dimPx(box.padding.top) + dimPx(box.padding.bottom);
        }

        float GetTotalBorderHorizontal() const {
            return GetBorderLeftWidth() + GetBorderRightWidth();
        }

        float GetTotalBorderVertical() const {
            return GetBorderTopWidth() + GetBorderBottomWidth();
        }

        // ===== CONTENT AREA CALCULATIONS =====
        // client area = bounds minus (border + padding), in parent coords
        Rect2Df GetContentRect() const {
            return Rect2Df(
                    finalBounds.x + GetBorderLeftWidth() + dimPx(box.padding.left),
                    finalBounds.y + GetBorderTopWidth()  + dimPx(box.padding.top),
                    finalBounds.width  - (GetTotalBorderHorizontal() + GetTotalPaddingHorizontal()),
                    finalBounds.height - (GetTotalBorderVertical()   + GetTotalPaddingVertical())
            );
        }

        // local client area = bounds in element coords (0,0 origin) minus (border + padding)
        Rect2Df GetLocalContentRect() const {
            return Rect2Df(
                    GetBorderLeftWidth() + dimPx(box.padding.left),
                    GetBorderTopWidth()  + dimPx(box.padding.top),
                    finalBounds.width  - (GetTotalBorderHorizontal() + GetTotalPaddingHorizontal()),
                    finalBounds.height - (GetTotalBorderVertical()   + GetTotalPaddingVertical())
            );
        }

        // Padding box = bounds minus border
        Rect2Df GetPaddingRect() {
            return Rect2Df(
                    finalBounds.x + GetBorderLeftWidth(),
                    finalBounds.y + GetBorderTopWidth(),
                    finalBounds.width  - GetTotalBorderHorizontal(),
                    finalBounds.height - GetTotalBorderVertical()
            );
        }

        // Margin box = bounds plus margins
        Rect2Df GetMarginRect() {
            return Rect2Df(
                    finalBounds.x - dimPx(box.margin.left),
                    finalBounds.y - dimPx(box.margin.top),
                    finalBounds.width  + dimPx(box.margin.left) + dimPx(box.margin.right),
                    finalBounds.height + dimPx(box.margin.top)  + dimPx(box.margin.bottom)
            );
        }

        float GetMarginLeft() const   { return dimPx(box.margin.left); }
        float GetMarginRight() const  { return dimPx(box.margin.right); }
        float GetMarginTop() const    { return dimPx(box.margin.top); }
        float GetMarginBottom() const { return dimPx(box.margin.bottom); }

        float GetPaddingLeft() const   { return dimPx(box.padding.left); }
        float GetPaddingRight() const  { return dimPx(box.padding.right); }
        float GetPaddingTop() const    { return dimPx(box.padding.top); }
        float GetPaddingBottom() const { return dimPx(box.padding.bottom); }

        float GetBorderLeftWidth() const   { return dimPx(box.border.left); }
        float GetBorderRightWidth() const  { return dimPx(box.border.right); }
        float GetBorderTopWidth() const    { return dimPx(box.border.top); }
        float GetBorderBottomWidth() const { return dimPx(box.border.bottom); }

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

        // Coordinate mapping (implementation uses GetParentContainer()).
        Point2Df MapFromLocal(const Point2Df &localPos, UltraCanvasContainer* mapToParent = nullptr);
        Point2Df MapToLocal(const Point2Df &globalPos, UltraCanvasContainer* mapFromParent = nullptr);

        // Virtual so interactive widgets (e.g. link labels) can resolve a
        // context-dependent cursor without the caller setting one explicitly.
        virtual UCMouseCursor GetMouseCursor() const { return mouseCursor; }
        void SetMouseCursor(UCMouseCursor cur) { mouseCursor = cur; }

        // ===== Z-INDEX (wraps inherited Element::zIndex) =====
        int GetZOrder() const { return zIndex; }
        int GetZIndex() const { return zIndex; }
        void SetZIndex(int index) { zIndex = index; }

        const std::string& GetTooltip() const { return tooltip; }
        void SetTooltip(const std::string& tooltipStr) { tooltip = tooltipStr; }

        // Structured tooltip (title, table rows, bullets, …). When set,
        // it takes precedence over the plain-text tooltip on hover.
        const std::shared_ptr<TooltipContent>& GetTooltipContent() const { return tooltipContent; }
        void SetTooltipContent(const TooltipContent& content) {
            tooltipContent = std::make_shared<TooltipContent>(content);
        }
        void ClearTooltipContent() { tooltipContent.reset(); }
        bool HasTooltip() const { return (tooltipContent && !tooltipContent->Empty()) || !tooltip.empty(); }

        bool IsVisible() const { return layout.display != CSSLayout::DisplayType::NoDisplay; }
        void SetVisible(bool visible);

        bool IsDisabled() const { return stateFlags.isDisabled; }
        void SetDisabled(bool disabled) { stateFlags.isDisabled = disabled; RequestRedraw(); }

        bool IsHovered() const { return stateFlags.isHovered; }
        void SetHovered(bool hovered) { stateFlags.isHovered = hovered; RequestRedraw(); }

        bool IsPressed() const { return stateFlags.isPressed; }
        void SetPressed(bool pressed) { stateFlags.isPressed = pressed; RequestRedraw(); }

        bool IsInteractive() const { return stateFlags.isInteractive; }
        void SetInteractive(bool interactive) { stateFlags.isInteractive = interactive; RequestRedraw(); }

        bool IsFocused() const;
        virtual bool SetFocus(bool focus);
        virtual bool AcceptsFocus() const { return false; }
        // ===== ACCESSIBILITY =====
        // What a screen reader is told about the element (see
        // UltraCanvasAccessibility.h): its role, its name, and for text its
        // text interface (null for elements without text to navigate).
        virtual AccessibleRole GetAccessibleRole() const { return AccessibleRole::Unknown; }
        // An element's own name comes from what it shows (a button's text, a
        // checkbox's label); SetAccessibleName() replaces it - for an icon
        // button, a field whose label is a separate element, or a panel.
        // Overrides return GetAccessibleNameOverride() when it is set.
        virtual std::string GetAccessibleName() const { return accessibleName; }
        void SetAccessibleName(const std::string& name);
        const std::string& GetAccessibleNameOverride() const { return accessibleName; }
        // Longer help, read after the name: SetAccessibleDescription(), else
        // the tooltip.
        virtual std::string GetAccessibleDescription() const;
        void SetAccessibleDescription(const std::string& description) { accessibleDescription = description; }
        virtual IAccessibleText* GetAccessibleTextInterface() { return nullptr; }
        // Checked, pressed or switched on; NotToggleable for anything else.
        virtual AccessibleToggleState GetAccessibleToggleState() const { return AccessibleToggleState::NotToggleable; }
        // A slider's, spin button's or progress bar's value; false without one.
        virtual bool GetAccessibleRange(AccessibleRange& range) const { (void)range; return false; }
        // Sets that value as the user would (callbacks fire); false when it
        // cannot be set.
        virtual bool SetAccessibleValue(double value) { (void)value; return false; }
        // The current value as text, where it is not a number: a combo box's
        // item, a text field's content (never a password field's). Empty for none.
        virtual std::string GetAccessibleValueText() const { return ""; }
        // Replaces that text as the user would (a text field's content);
        // false where it cannot be set.
        virtual bool SetAccessibleValueText(const std::string& text) { (void)text; return false; }
        // The element's default action, as a screen reader offers it: "press"
        // for a button, "toggle" for a checkbox, "select" for a radio button.
        // Empty for none. DoAccessibleAction() performs it as a click would.
        virtual std::string GetAccessibleActionName() const { return ""; }
        virtual bool DoAccessibleAction() { return false; }
        // Tells the platform bridges about a change (ValueChanged,
        // StateChanged, NameChanged, ...). Cheap when nobody listens.
        void NotifyAccessibility(AccessibilityEventType type);
        // True for a field whose content is a secret. The bridges report it
        // as a password field (UI Automation's IsPassword, AT-SPI's password
        // text role), so a screen reader says "password" and echoes stars
        // rather than the characters typed, and other assistive tools leave
        // the content alone. Such an element must not hand out its text
        // through GetAccessibleTextInterface().
        virtual bool IsAccessiblePassword() const { return false; }

        // True for an element that shows an input method's pre-edit text in
        // place (TextComposition events). While one has focus the platform
        // input method leaves the composing to it; any other element gets the
        // input method's own composition window.
        virtual bool DrawsTextComposition() const { return false; }
        bool CanReceiveFocus() const { return IsVisible() && !IsDisabled() && IsInteractive() && AcceptsFocus(); }

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
        virtual void Render(IRenderContext* ctx, const Rect2Df& dirtyRect);

        void Arrange(const Rect2Df& newFinalRect, const CSSLayout::LayoutContext& ctx) override;
        // Arrange() for an element that places its children itself: takes the
        // box and redraws what it covered, but runs no layout of the children.
        void ArrangeOwnBox(const Rect2Df& newFinalRect);

        // ===== EVENT HANDLING =====
        virtual bool OnEvent(const UCEvent& event);
        virtual bool OnEventFilter(const UCEvent& event) { (void)event; return false; };

        void SetEventCallback(std::function<bool(const UCEvent&)> callback);

        // Adds localRect (in this element's local coords) to the appropriate
        // dirty-rect manager: the containing popup's, or the window's.
        virtual void InvalidateRect(const Rect2Df& localRect);
        void RequestRedraw();

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
        // Redraws the union of oldBounds and the new finalBounds (Arrange's tail).
        void InvalidateArrangeDamage(const Rect2Df& oldBounds);

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
