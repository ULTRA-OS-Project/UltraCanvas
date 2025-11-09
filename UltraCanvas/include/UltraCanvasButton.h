// include/UltraCanvasButton.h
// Interactive button component with styling options
// Version: 2.3.1
// Last Modified: 2025-01-15
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
    enum class ButtonIconPosition {
        Left,       // Icon on the left of text
        Right,      // Icon on the right of text
        Top,        // Icon above text
        Bottom,     // Icon below text
        Center      // Icon only (no text displayed when Center)
    };

// ===== SECONDARY ICON POSITION =====
    enum class ButtonSecondaryIconPosition {
        Left,       // Icon on the left of secondary text
        Right       // Icon on the right of secondary text
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

        // Secondary section icon (new feature)
        std::shared_ptr<UCImage> secondaryIcon;
        ButtonSecondaryIconPosition secondaryIconPosition = ButtonSecondaryIconPosition::Left;
        int secondaryIconWidth = 16;
        int secondaryIconHeight = 16;
        int secondaryIconSpacing = 4;  // Space between secondary icon and text

        // Colors for secondary section
        Color secondaryBackgroundColor = Color(240, 240, 240, 255);
        Color secondaryTextColor = Color(128, 128, 128, 255);
        Color secondaryHoverColor = Color(230, 230, 230, 255);
        Color secondaryPressedColor = Color(200, 200, 200, 255);

        // Icon colors for secondary section
        Color secondaryNormalIconColor = Colors::White;  // White = no tinting
        Color secondaryHoverIconColor = Colors::White;
        Color secondaryPressedIconColor = Colors::White;
        Color secondaryDisabledIconColor = Color(255, 255, 255, 128);

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
        std::shared_ptr<UCImage> icon;
        ButtonStyle style;
        ButtonIconPosition iconPosition = ButtonIconPosition::Left;

        // Icon properties
        int iconWidth = 24;
        int iconHeight = 24;
        bool scaleIconToFit = false;
        bool maintainIconAspectRatio = true;

        // State
        bool autoresize = false;
        bool isNeedAutoresize = false;

        // Cached layout calculations
        Rect2Di iconRect;
        Rect2Di textRect;
        Rect2Di secondaryTextRect;  // For split button secondary section
        Rect2Di secondaryIconRect;  // For split button secondary icon (new)
        Rect2Di primarySectionRect;  // Primary section bounds
        Rect2Di secondarySectionRect;  // Secondary section bounds
        bool layoutDirty = true;

    public:
        // ===== CONSTRUCTOR =====
        UltraCanvasButton(const std::string& identifier = "Button", long id = 0,
                          long x = 0, long y = 0, long w = 100, long h = 30,
                          const std::string& buttonText = "Button");

        // ===== SPLIT BUTTON METHODS =====
        void SetSplitEnabled(bool enabled);
        void SetSplitRatio(float primaryRatio = 0.75f);
        void SetSplitHorizontal(bool horizontal);
        void SetSplitSecondaryText(const std::string& secondaryText);

        // New methods for secondary icon support
        void SetSplitSecondaryIcon(const std::string& iconPath,
                                   ButtonSecondaryIconPosition position = ButtonSecondaryIconPosition::Left);
        void SetSplitSecondaryIconSize(int width, int height);
        void SetSplitSecondaryIconSpacing(int spacing);
        void SetSplitSecondaryIconPosition(ButtonSecondaryIconPosition position);
        void SetSplitSecondaryIconColors(const Color& normal, const Color& hover,
                                         const Color& pressed, const Color& disabled);

        void SetSplitColors(const Color& secBg, const Color& secText,
                            const Color& secHover, const Color& secPressed);
        void SetSplitSeparator(bool show, const Color& color, float width);
        SplitButtonStyle& GetSplitStyle() { return style.splitStyle; }
        bool IsSplitButton() const { return style.splitStyle.enabled; }

        // Check if secondary icon exists
        bool HasSecondaryIcon() const {
            return style.splitStyle.enabled && style.splitStyle.secondaryIcon != nullptr;
        }
        // ===== TEXT & ICON METHODS =====
        void SetText(const std::string& buttonText);
        std::string GetText() const { return text; }
        void SetIcon(const std::string& iconPath);
        void SetIconPosition(ButtonIconPosition position);
        void SetIconSize(int width, int height);
        void SetIconScaleToFit(bool scale) { scaleIconToFit = scale; }
        void SetMaintainAspectRatio(bool maintain) { maintainIconAspectRatio = maintain; }
        bool HasIcon() const { return icon != nullptr; }

        // ===== STYLING METHODS =====
        void SetColors(const Color& normal, const Color& hover,
                       const Color& pressed, const Color& disabled);
        void SetTextColors(const Color& normal, const Color& hover,
                           const Color& pressed, const Color& disabled);
        void SetIconColors(const Color& normal, const Color& hover,
                           const Color& pressed, const Color& disabled);
        void SetBorder(float width, const Color& color);
        void SetFont(const std::string& family, float size,
                     FontWeight weight = FontWeight::Normal);
        void SetTextAlign(TextAlignment align);
        void SetPadding(int left, int right, int top, int bottom);
        void SetIconSpacing(int spacing);
        void SetCornerRadius(float radius);
        void SetShadow(bool enabled, const Color& color = Color(0, 0, 0, 64),
                       const Point2Di& offset = Point2Di(1, 1));
        void SetStyle(const ButtonStyle& newStyle);
        ButtonStyle& GetStyle() { return style; }

        // ===== AUTO-RESIZE =====
        void SetAutoResize(bool enable) { autoresize = enable; }
        bool IsAutoResize() const { return autoresize; }
        void AutoResize();

        void SetOnClick(std::function<void()> onClick_) {
            onClick = onClick_;
        };

        // ===== CALLBACKS =====
        std::function<void()> onClick;
        std::function<void()> onPress;      // When button is pressed down
        std::function<void()> onRelease;    // When button is released
        std::function<void()> onSecondaryClick;  // For secondary section click
        std::function<void()> onHoverEnter;
        std::function<void()> onHoverLeave;

        // ===== OVERRIDES =====
        void Render(IRenderContext* ctx) override;
        bool OnEvent(const UCEvent& event) override;
        bool AcceptsFocus() const override { return true; }

    protected:
        // ===== LAYOUT HELPERS =====
        void CalculateLayout();
        void CalculateSplitLayout();
        bool IsPointInPrimarySection(int x, int y) const;
        bool IsPointInSecondarySection(int x, int y) const;

        // ===== RENDERING HELPERS =====
        void UpdateButtonState();
        void DrawIcon(IRenderContext* ctx);
        void DrawSecondaryIcon(IRenderContext* ctx);  // New method
        void DrawText(IRenderContext* ctx);
        void DrawSplitButton(IRenderContext* ctx);
        void GetCurrentColors(Color& bgColor, Color& textColor, Color& iconColor) const;
        void GetSplitColors(Color& primaryBg, Color& primaryText, Color& secondaryBg, Color& secondaryText);
        void GetSecondaryIconColor(Color& iconColor) const;  // New method
        void Click(const UCEvent& event);  // Helper for click handling
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

        ButtonBuilder& SetSplitEnabled(bool enable = true) {
            button->SetSplitEnabled(enable);
            return *this;
        }

        ButtonBuilder& SetSplitSecondaryText(const std::string& text) {
            button->SetSplitSecondaryText(text);
            return *this;
        }

        ButtonBuilder& SetSplitSecondaryIcon(const std::string& icon) {
            button->SetSplitSecondaryIcon(icon);
            return *this;
        }

        ButtonBuilder& SetSplitRatio(float ratio) {
            button->SetSplitRatio(ratio);
            return *this;
        }

        ButtonBuilder& SetSplitColors(const Color& secBg, const Color& secText,
                                      const Color& secHover, const Color& secPressed) {
            button->SetSplitColors(secBg, secText, secHover, secPressed);
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

        ButtonBuilder& SetIconPosition(ButtonIconPosition position) {
            button->SetIconPosition(position);
            return *this;
        }

        ButtonBuilder& SetIconSize(int width, int height) {
            button->SetIconSize(width, height);
            return *this;
        }

        ButtonBuilder& SetSplitIconSize(int width, int height) {
            button->SetSplitSecondaryIconSize(width, height);
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
        ButtonBuilder& SetIconSpacing(int spacing) {
            button->SetIconSpacing(spacing);
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
            style.splitStyle.secondaryHoverColor = Colors::LightGray;
            style.splitStyle.secondaryPressedColor = Colors::Gray;
            style.cornerRadius = 5.0f;
            return style;
        }

        inline ButtonStyle CounterButtonStyle() {
            ButtonStyle style;
            style.splitStyle.enabled = true;
            style.splitStyle.primaryRatio = 0.70f;
            style.splitStyle.secondaryBackgroundColor = Color(100, 150, 255, 255);
            style.splitStyle.secondaryTextColor = Colors::White;
            style.splitStyle.secondaryHoverColor = Colors::LightGray;
            style.splitStyle.secondaryPressedColor = Colors::Gray;
            style.splitStyle.separatorColor = Colors::White;
            style.splitStyle.separatorWidth = 2.0f;
            return style;
        }
    }

} // namespace UltraCanvas