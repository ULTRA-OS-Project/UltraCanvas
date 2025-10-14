// include/UltraCanvasButton.h
// Interactive button component with styling options
// Version: 2.2.0
// Last Modified: 2025-01-11
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include <string>
#include <functional>

namespace UltraCanvas {

// ===== FORWARD DECLARATIONS =====
    class UltraCanvasButton;

// ===== ICON POSITION =====
    enum class IconPosition {
        Left,       // Icon on the left of text
        Right,      // Icon on the right of text
        Top,        // Icon above text
        Bottom,     // Icon below text
        Center      // Icon only (no text displayed when Center)
    };

// ===== BUTTON STATES =====
    enum class ButtonState {
        Normal,
        Hovered,
        Pressed,
        Disabled
    };

// ===== SPLIT BUTTON STYLE =====
    struct SplitButtonStyle {
        // Enable split button mode
        bool enabled = false;

        // Split orientation (true = horizontal, false = vertical)
        bool horizontal = true;

        // Section proportions (0.0 to 1.0)
        float primaryRatio = 0.75f;

        // Secondary section text
        std::string secondaryText;

        // Colors for secondary section
        Color secondaryBackgroundColor = Color(240, 240, 240, 255);
        Color secondaryTextColor = Color(128, 128, 128, 255);
        Color secondaryHoverColor = Color(230, 230, 230, 255);
        Color secondaryPressedColor = Color(200, 200, 200, 255);

        // Separator styling
        bool showSeparator = true;
        Color separatorColor = Color(200, 200, 200, 255);
        float separatorWidth = 1.0f;
    };

// ===== BUTTON STYLE =====
    struct ButtonStyle {
        // Colors for different states
        Color normalColor = Colors::ButtonFace;
        Color hoverColor = Colors::SelectionHover;
        Color pressedColor = Color(204, 228, 247, 255);
        Color disabledColor = Colors::LightGray;
        Color focusedColor = Color(80, 80, 80, 255);

        Color normalTextColor = Colors::TextDefault;
        Color hoverTextColor = Colors::TextDefault;
        Color pressedTextColor = Colors::TextDefault;
        Color disabledTextColor = Colors::TextDisabled;

        Color borderColor = Colors::ButtonShadow;
        float borderWidth = 1.0f;

        // Icon colors (tinting)
        Color normalIconColor = Colors::White;  // White = no tinting
        Color hoverIconColor = Colors::White;
        Color pressedIconColor = Colors::White;
        Color disabledIconColor = Color(255, 255, 255, 128);  // Semi-transparent

        // Text styling
        std::string fontFamily = "Arial";
        float fontSize = 12.0f;
        FontWeight fontWeight = FontWeight::Normal;
        TextAlignment textAlign = TextAlignment::Center;

        // Layout
        int paddingLeft = 8;
        int paddingRight = 8;
        int paddingTop = 4;
        int paddingBottom = 4;
        int iconSpacing = 4;  // Space between icon and text
        float cornerRadius = 3.0f;

        // Effects
        bool hasShadow = false;
        Color shadowColor = Color(0, 0, 0, 64);
        Point2Di shadowOffset = Point2Di(1, 1);

        // Split button style
        SplitButtonStyle splitStyle;
    };

// ===== MAIN BUTTON CLASS =====
    class UltraCanvasButton : public UltraCanvasUIElement {
    private:
        std::string text = "Button";
        std::string iconPath;
        ButtonStyle style;
        ButtonState currentState = ButtonState::Normal;
        IconPosition iconPosition = IconPosition::Left;

        // Icon properties
        int iconWidth = 24;
        int iconHeight = 24;
        bool scaleIconToFit = false;
        bool maintainIconAspectRatio = true;

        // State
        bool pressed = false;
        bool autoresize = false;
        bool isNeedAutoresize = false;

        // Cached layout calculations
        Rect2Di iconRect;
        Rect2Di textRect;
        Rect2Di secondaryTextRect;  // For split button secondary section
        Rect2Di primarySectionRect;  // Primary section bounds
        Rect2Di secondarySectionRect;  // Secondary section bounds
        bool layoutDirty = true;

    public:
        // ===== CONSTRUCTOR =====
        UltraCanvasButton(const std::string& identifier = "Button", long id = 0,
                          long x = 0, long y = 0, long w = 100, long h = 30,
                          const std::string& buttonText = "Button");

        // ===== SPLIT BUTTON METHODS =====
        void EnableSplitButton(bool enable = true) {
            style.splitStyle.enabled = enable;
            layoutDirty = true;
            RequestRedraw();
        }

        bool IsSplitButtonEnabled() const { return style.splitStyle.enabled; }

        void SetSplitButtonSecondaryText(const std::string& text) {
            style.splitStyle.secondaryText = text;
            layoutDirty = true;
            RequestRedraw();
        }

        const std::string& GetSplitButtonSecondaryText() const {
            return style.splitStyle.secondaryText;
        }

        void SetSplitButtonRatio(float ratio) {
            style.splitStyle.primaryRatio = std::min(0.9f, ratio);
            layoutDirty = true;
            RequestRedraw();
        }

        void SetSplitButtonColors(const Color& primaryBg, const Color& primaryText,
                                  const Color& secondaryBg, const Color& secondaryText) {
            style.normalColor = primaryBg;
            style.normalTextColor = primaryText;
            style.splitStyle.secondaryBackgroundColor = secondaryBg;
            style.splitStyle.secondaryTextColor = secondaryText;
            RequestRedraw();
        }

        void SetSplitButtonOrientation(bool horizontal = true) {
            style.splitStyle.horizontal = horizontal;
            layoutDirty = true;
            RequestRedraw();
        }

        // ===== TEXT METHODS =====
        const std::string& GetText() const { return text; }
        void SetText(const std::string& newText) {
            text = newText;
            layoutDirty = true;
            AutoResize();
        }

        // ===== ICON METHODS =====
        const std::string& GetIcon() const { return iconPath; }
        void SetIcon(const std::string& path) {
            iconPath = path;
            layoutDirty = true;
            AutoResize();
        }

        void ClearIcon() {
            iconPath.clear();
            layoutDirty = true;
            AutoResize();
        }

        bool HasIcon() const { return !iconPath.empty(); }

        void SetIconPosition(IconPosition position) {
            iconPosition = position;
            layoutDirty = true;
            AutoResize();
        }

        IconPosition GetIconPosition() const { return iconPosition; }

        void SetIconSize(int width, int height) {
            iconWidth = width;
            iconHeight = height;
            layoutDirty = true;
            AutoResize();
        }

        void GetIconSize(int& width, int& height) const {
            width = iconWidth;
            height = iconHeight;
        }

        void SetScaleIconToFit(bool scale) {
            scaleIconToFit = scale;
            layoutDirty = true;
        }

        bool GetScaleIconToFit() const { return scaleIconToFit; }

        void SetMaintainIconAspectRatio(bool maintain) {
            maintainIconAspectRatio = maintain;
            layoutDirty = true;
        }

        bool GetMaintainIconAspectRatio() const { return maintainIconAspectRatio; }

        // ===== STYLE METHODS =====
        const ButtonStyle& GetStyle() const { return style; }
        void SetStyle(const ButtonStyle& newStyle) {
            style = newStyle;
            layoutDirty = true;
        }

        ButtonState GetButtonState() const { return currentState; }
        bool IsPressed() const { return pressed; }

        bool AcceptsFocus() const override { return true; }
        void SetAutoresize(bool value) {
            autoresize = value;
            AutoResize();
        }
        bool GetAutoresize() const { return autoresize; }

        // ===== STYLE CONVENIENCE METHODS =====
        void SetColors(const Color& normal, const Color& hover, const Color& pressed, const Color& disabled);
        void SetTextColors(const Color& normal, const Color& hover, const Color& pressed, const Color& disabled);
        void SetIconColors(const Color& normal, const Color& hover, const Color& pressed, const Color& disabled);
        void SetFont(const std::string& fontFamily, float fontSize, FontWeight weight = FontWeight::Normal);
        void SetPadding(int left, int right, int top, int bottom);
        void SetIconSpacing(int spacing);
        void SetCornerRadius(float radius);
        void SetShadow(bool enabled, const Color& color = Color(0, 0, 0, 64),
                       const Point2Di& offset = Point2Di(1, 1));

        // ===== TOOLTIP SUPPORT =====
        void SetTooltip(const std::string& tooltip) {
            properties.tooltip = tooltip;
        }

        const std::string& GetTooltip() const {
            return properties.tooltip;
        }

        // ===== EVENT METHODS =====
        void Click(const UCEvent& ev);
        void SetOnClick(std::function<void()> _onClick) {
            onClick = _onClick;
        }

        void AutoResize();

        // ===== RENDERING =====
        void Render() override;

        // ===== EVENT HANDLING =====
        bool OnEvent(const UCEvent& event) override;

        // ===== PUBLIC CALLBACKS =====
        std::function<void()> onClick;
        std::function<void()> onSecondaryClick;  // For split button secondary section
        std::function<void()> onPress;
        std::function<void()> onRelease;
        std::function<void()> onHoverEnter;
        std::function<void()> onHoverLeave;

    protected:
        // ===== INTERNAL METHODS =====
        void UpdateButtonState();
        void GetCurrentColors(Color& bgColor, Color& textColor, Color& iconColor);
        void GetSplitColors(Color& primaryBg, Color& primaryText,
                            Color& secondaryBg, Color& secondaryText);
        void CalculateLayout();
        void CalculateSplitLayout();
        void DrawIcon(IRenderContext* ctx);
        void DrawText(IRenderContext* ctx);
        void DrawSplitButton(IRenderContext* ctx);
        bool IsPointInPrimarySection(int x, int y) const;
        bool IsPointInSecondarySection(int x, int y) const;
    };

// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasButton> CreateButton(
            const std::string& identifier, long id, long x, long y, long w, long h,
            const std::string& text = "Button") {
        return std::make_shared<UltraCanvasButton>(identifier, id, x, y, w, h, text);
    }

    inline std::shared_ptr<UltraCanvasButton> CreateIconButton(
            const std::string& identifier, long id, long x, long y, long w, long h,
            const std::string& iconPath, const std::string& text = "") {
        auto button = CreateButton(identifier, id, x, y, w, h, text);
        button->SetIcon(iconPath);
        return button;
    }

// ===== BUTTON BUILDER (FLUENT INTERFACE) =====
    class ButtonBuilder {
    private:
        std::shared_ptr<UltraCanvasButton> button;

    public:
        ButtonBuilder(const std::string& identifier = "Button", long id = 0) {
            button = std::make_shared<UltraCanvasButton>(identifier, id);
        }

        ButtonBuilder& SetPosition(long x, long y) {
            button->SetX(x);
            button->SetY(y);
            return *this;
        }

        ButtonBuilder& SetText(const std::string& text) {
            button->SetText(text);
            return *this;
        }

        ButtonBuilder& EnableSplitButton(bool enable = true) {
            button->EnableSplitButton(enable);
            return *this;
        }

        ButtonBuilder& SetSplitSecondaryText(const std::string& text) {
            button->SetSplitButtonSecondaryText(text);
            return *this;
        }

        ButtonBuilder& SetSplitRatio(float ratio) {
            button->SetSplitButtonRatio(ratio);
            return *this;
        }

        ButtonBuilder& SetSplitColors(const Color& primaryBg, const Color& primaryText,
                                      const Color& secondaryBg, const Color& secondaryText) {
            button->SetSplitButtonColors(primaryBg, primaryText, secondaryBg, secondaryText);
            return *this;
        }

        ButtonBuilder& OnSecondaryClick(std::function<void()> callback) {
            button->onSecondaryClick = callback;
            return *this;
        }

        ButtonBuilder& SetIcon(const std::string& iconPath) {
            button->SetIcon(iconPath);
            return *this;
        }

        ButtonBuilder& SetIconPosition(IconPosition position) {
            button->SetIconPosition(position);
            return *this;
        }

        ButtonBuilder& SetIconSize(int width, int height) {
            button->SetIconSize(width, height);
            return *this;
        }

        ButtonBuilder& SetStyle(const ButtonStyle& style) {
            button->SetStyle(style);
            return *this;
        }

        ButtonBuilder& SetFont(const std::string& family, float size, FontWeight weight = FontWeight::Normal) {
            button->SetFont(family, size, weight);
            return *this;
        }

        ButtonBuilder& SetPadding(int padding) {
            button->SetPadding(padding, padding, padding/2, padding/2);
            return *this;
        }

        ButtonBuilder& SetCornerRadius(float radius) {
            button->SetCornerRadius(radius);
            return *this;
        }

        ButtonBuilder& SetShadow(bool enabled = true) {
            button->SetShadow(enabled);
            return *this;
        }

        ButtonBuilder& SetTooltip(const std::string& tooltip) {
            button->SetTooltip(tooltip);
            return *this;
        }

        ButtonBuilder& OnClick(std::function<void()> callback) {
            button->onClick = callback;
            return *this;
        }

        ButtonBuilder& OnHover(std::function<void()> enter, std::function<void()> leave = nullptr) {
            button->onHoverEnter = enter;
            if (leave) button->onHoverLeave = leave;
            return *this;
        }

        ButtonBuilder& SetSize(long w, long h) {
            button->SetWidth(w);
            button->SetHeight(h);
            return *this;
        }

        std::shared_ptr<UltraCanvasButton> Build() {
            return button;
        }
    };

// ===== PREDEFINED STYLES =====
    namespace ButtonStyles {
        inline ButtonStyle Default() {
            return ButtonStyle();
        }

        inline ButtonStyle PrimaryStyle() {
            ButtonStyle style;
            style.normalColor = Colors::Selection;
            style.hoverColor = Color(0, 90, 180, 255);
            style.pressedColor = Color(0, 60, 120, 255);
            style.normalTextColor = Colors::White;
            style.hoverTextColor = Colors::White;
            style.pressedTextColor = Colors::White;
            style.fontWeight = FontWeight::Bold;
            return style;
        }

        inline ButtonStyle SecondaryStyle() {
            ButtonStyle style;
            style.normalColor = Colors::ButtonFace;
            style.borderWidth = 2.0f;
            style.borderColor = Colors::Selection;
            style.hoverColor = Color(240, 240, 250, 255);
            return style;
        }

        inline ButtonStyle DangerStyle() {
            ButtonStyle style;
            style.normalColor = Color(220, 53, 69, 255);
            style.hoverColor = Color(200, 35, 51, 255);
            style.pressedColor = Color(180, 20, 36, 255);
            style.normalTextColor = Colors::White;
            style.hoverTextColor = Colors::White;
            style.pressedTextColor = Colors::White;
            return style;
        }

        inline ButtonStyle SuccessStyle() {
            ButtonStyle style;
            style.normalColor = Color(40, 167, 69, 255);
            style.hoverColor = Color(34, 142, 59, 255);
            style.pressedColor = Color(28, 117, 49, 255);
            style.normalTextColor = Colors::White;
            style.hoverTextColor = Colors::White;
            style.pressedTextColor = Colors::White;
            return style;
        }

        inline ButtonStyle FlatStyle() {
            ButtonStyle style;
            style.normalColor = Colors::Transparent;
            style.hoverColor = Color(240, 240, 240, 128);
            style.pressedColor = Color(220, 220, 220, 180);
            style.borderWidth = 0.0f;
            style.hasShadow = false;
            return style;
        }

        inline ButtonStyle IconOnlyStyle() {
            ButtonStyle style = FlatStyle();
            style.paddingLeft = style.paddingRight = 4;
            style.paddingTop = style.paddingBottom = 4;
            return style;
        }

        inline ButtonStyle SplitButtonStyle() {
            ButtonStyle style;
            style.splitStyle.enabled = true;
            style.splitStyle.showSeparator = true;
            style.splitStyle.primaryRatio = 0.75f;
            return style;
        }

        inline ButtonStyle BadgeButtonStyle() {
            ButtonStyle style;
            style.splitStyle.enabled = true;
            style.splitStyle.showSeparator = false;
            style.splitStyle.primaryRatio = 0.8f;
            style.splitStyle.secondaryBackgroundColor = Color(255, 100, 100, 255);
            style.splitStyle.secondaryTextColor = Colors::White;
            style.cornerRadius = 5.0f;
            return style;
        }

        inline ButtonStyle CounterButtonStyle() {
            ButtonStyle style;
            style.splitStyle.enabled = true;
            style.splitStyle.primaryRatio = 0.70f;
            style.splitStyle.secondaryBackgroundColor = Color(100, 150, 255, 255);
            style.splitStyle.secondaryTextColor = Colors::White;
            style.splitStyle.separatorColor = Colors::White;
            style.splitStyle.separatorWidth = 2.0f;
            return style;
        }
    }

} // namespace UltraCanvas