// include/UltraCanvasButton.h
// Refactored button component using unified base system
// Version: 2.0.0
// Last Modified: 2024-12-19
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

// ===== BUTTON STATES =====
    enum class ButtonState {
        Normal,
        Hovered,
        Pressed,
        Disabled
    };

// ===== BUTTON STYLE =====
    struct ButtonStyle {
        // Colors for different states
        Color normalColor = Colors::ButtonFace;
        Color hoverColor = Colors::SelectionHover;
        Color pressedColor = Color(204, 228, 247, 255);
        Color disabledColor = Colors::LightGray;

        Color normalTextColor = Colors::TextDefault;
        Color hoverTextColor = Colors::TextDefault;
        Color pressedTextColor = Colors::TextDefault;
        Color disabledTextColor = Colors::TextDisabled;

        Color borderColor = Colors::ButtonShadow;
        float borderWidth = 1.0f;

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
        float cornerRadius = 0.0f;

        // Effects
        bool hasShadow = false;
        Color shadowColor = Color(0, 0, 0, 64);
        Point2Di shadowOffset = Point2Di(1, 1);
    };

// ===== MAIN BUTTON CLASS (DEFINE FIRST) =====
    class UltraCanvasButton : public UltraCanvasElement {
    private:
        std::string text = "Button";
        ButtonStyle style;
        ButtonState currentState = ButtonState::Normal;
        bool pressed = false;

    public:
        // ===== CONSTRUCTOR =====
        UltraCanvasButton(const std::string& identifier = "Button", long id = 0,
                          long x = 0, long y = 0, long w = 100, long h = 30,
                          const std::string& buttonText = "Button")
                : UltraCanvasElement(identifier, id, x, y, w, h), text(buttonText) {

            currentState = ButtonState::Normal;
            pressed = false;
        }

        // ===== BUTTON-SPECIFIC METHODS =====
        const std::string& GetText() const { return text; }
        void SetText(const std::string& newText) {
            text = newText;
            AutoResize();
        }

        const ButtonStyle& GetStyle() const { return style; }
        void SetStyle(const ButtonStyle& newStyle) { style = newStyle; }

        ButtonState GetButtonState() const { return currentState; }
        bool IsPressed() const { return pressed; }

        bool AcceptsFocus() const override { return true; }

        // ===== STYLE CONVENIENCE METHODS (THESE NEED TO BE DEFINED FIRST) =====
        void SetColors(const Color& normal, const Color& hover, const Color& pressed, const Color& disabled) {
            style.normalColor = normal;
            style.hoverColor = hover;
            style.pressedColor = pressed;
            style.disabledColor = disabled;
        }

        void SetTextColors(const Color& normal, const Color& hover, const Color& pressed, const Color& disabled) {
            style.normalTextColor = normal;
            style.hoverTextColor = hover;
            style.pressedTextColor = pressed;
            style.disabledTextColor = disabled;
        }

        void SetFont(const std::string& fontFamily, float fontSize, FontWeight weight = FontWeight::Normal) {
            style.fontFamily = fontFamily;
            style.fontSize = fontSize;
            style.fontWeight = weight;
            AutoResize();
        }

        void SetPadding(int left, int right, int top, int bottom) {
            style.paddingLeft = left;
            style.paddingRight = right;
            style.paddingTop = top;
            style.paddingBottom = bottom;
            AutoResize();
        }

        void SetCornerRadius(float radius) {
            style.cornerRadius = radius;
        }

        void SetShadow(bool enabled, const Color& color = Color(0, 0, 0, 64),
                       const Point2Di& offset = Point2Di(1, 1)) {
            style.hasShadow = enabled;
            style.shadowColor = color;
            style.shadowOffset = offset;
        }

        void Click(const UCEvent& ev) {
            if (IsEnabled() && onClick) {
                onClick(ev);
            }
        }

        void AutoResize() {
            if (text.empty()) return;

            // Simple auto-resize logic
            int newWidth = static_cast<int>(text.length() * style.fontSize * 0.6f) +
                           style.paddingLeft + style.paddingRight;
            int newHeight = static_cast<int>(style.fontSize * 1.5f) +
                            style.paddingTop + style.paddingBottom;

            SetWidth(newWidth);
            SetHeight(newHeight);
        }

        // ===== RENDERING =====
        void Render() override {
            IRenderContext *ctx = GetRenderContext();
            if (!IsVisible() || !ctx) return;

            ctx->PushState();

            UpdateButtonState();

            Color bgColor, textColor;
            GetCurrentColors(bgColor, textColor);

            Rect2Di bounds = GetBounds();

            // Draw background
            if (style.cornerRadius > 0) {
                ctx->DrawFilledRectangle(bounds, bgColor, Colors::Transparent, 0, style.cornerRadius);
            } else {
                ctx->DrawFilledRectangle(bounds, bgColor);
            }

            // Draw border
            if (style.borderWidth > 0) {
                // Simple border using lines
                ctx->DrawLine(bounds.x, bounds.y,
                         bounds.x + bounds.width, bounds.y, style.borderColor);
                ctx->DrawLine(bounds.x + bounds.width, bounds.y,
                         bounds.x + bounds.width, bounds.y + bounds.height, style.borderColor);
                ctx->DrawLine(bounds.x + bounds.width, bounds.y + bounds.height,
                         bounds.x, bounds.y + bounds.height, style.borderColor);
                ctx->DrawLine(bounds.x, bounds.y + bounds.height,
                         bounds.x, bounds.y, style.borderColor);
            }

            // Draw text (FIXED CENTERING)
            if (!text.empty()) {
                // Set text style with middle baseline
                TextStyle centeredTextStyle;
                centeredTextStyle.fontFamily = style.fontFamily;
                centeredTextStyle.fontSize = style.fontSize;
                centeredTextStyle.textColor = textColor;
                centeredTextStyle.alignment = TextAlignment::Center;
                centeredTextStyle.baseline = TextBaseline::Middle;  // This ensures vertical centering
                ctx->SetTextStyle(centeredTextStyle);

                ctx->DrawTextInRect(text, bounds);
            }

            // Draw focus indicator
            if (IsFocused()) {
                // Simple focus rectangle
                Rect2Di focusRect(bounds.x + 2, bounds.y + 2, bounds.width - 4, bounds.height - 4);
                ctx->DrawLine(focusRect.x, focusRect.y,
                         focusRect.x + focusRect.width, focusRect.y, Colors::Blue);
                ctx->DrawLine(focusRect.x + focusRect.width, focusRect.y,
                         focusRect.x + focusRect.width, focusRect.y + focusRect.height, Colors::Blue);
                ctx->DrawLine(focusRect.x + focusRect.width, focusRect.y + focusRect.height,
                         focusRect.x, focusRect.y + focusRect.height, Colors::Blue);
                ctx->DrawLine(focusRect.x, focusRect.y + focusRect.height,
                         focusRect.x, focusRect.y, Colors::Blue);
            }

            ctx->PopState();
        }

        // ===== EVENT HANDLING =====
        bool OnEvent(const UCEvent& event) override {
            if (!IsActive() || !IsVisible()) return false;

            switch (event.type) {
                case UCEventType::MouseDown:
                    if (Contains(event.x, event.y) && IsEnabled()) {
                        pressed = true;
                        SetFocus(true);
                        if (onPress) onPress(event);
                    }
                    break;

                case UCEventType::MouseUp:
                    if (IsEnabled()) {
                        bool wasPressed = pressed;
                        pressed = false;

                        if (onRelease) onRelease(event);

                        if (wasPressed && Contains(event.x, event.y)) {
                            Click(event);
                        }
                        RequestRedraw();
                    }
                    break;

                case UCEventType::MouseEnter:
                    SetHovered(true);
                    if (onHoverEnter) onHoverEnter(event);
                    RequestRedraw();
                    break;

                case UCEventType::MouseLeave:
                    SetHovered(false);
                    if (pressed) {
                        pressed = false;
                        if (onRelease) onRelease(event);
                    }
                    if (onHoverLeave) onHoverLeave(event);
                    RequestRedraw();
                    break;

                case UCEventType::KeyDown:
                    if (IsFocused() && IsEnabled()) {
                        if (event.virtualKey == UCKeys::Return || event.virtualKey == UCKeys::Space) {
                            Click(event);
                        }
                    }
                    break;
            }
            return false;
        }

        // ===== EVENT CALLBACKS =====
        std::function<void(const UCEvent&)> onClick;
        std::function<void(const UCEvent&)> onPress;
        std::function<void(const UCEvent&)> onRelease;
        std::function<void(const UCEvent&)> onHoverEnter;
        std::function<void(const UCEvent&)> onHoverLeave;

    private:
        // ===== HELPER METHODS =====
        void UpdateButtonState() {
            if (!IsEnabled()) {
                currentState = ButtonState::Disabled;
            } else if (pressed) {
                currentState = ButtonState::Pressed;
            } else if (IsHovered()) {
                currentState = ButtonState::Hovered;
            } else {
                currentState = ButtonState::Normal;
            }
        }

        void GetCurrentColors(Color& bgColor, Color& textColor) {
            switch (currentState) {
                case ButtonState::Normal:
                    bgColor = style.normalColor;
                    textColor = style.normalTextColor;
                    break;
                case ButtonState::Hovered:
                    bgColor = style.hoverColor;
                    textColor = style.hoverTextColor;
                    break;
                case ButtonState::Pressed:
                    bgColor = style.pressedColor;
                    textColor = style.pressedTextColor;
                    break;
                case ButtonState::Disabled:
                    bgColor = style.disabledColor;
                    textColor = style.disabledTextColor;
                    break;
            }
        }
    };

// ===== FACTORY FUNCTIONS (AFTER BUTTON CLASS) =====
    inline std::shared_ptr<UltraCanvasButton> CreateButton(
            const std::string& identifier, long id, long x, long y, long w, long h,
            const std::string& text = "Button") {

        return UltraCanvasElementFactory::CreateWithID<UltraCanvasButton>(
                id, identifier, id, x, y, w, h, text);
    }

    inline std::shared_ptr<UltraCanvasButton> CreateAutoButton(
            const std::string& identifier, long x, long y, const std::string& text = "Button") {

        auto button = UltraCanvasElementFactory::Create<UltraCanvasButton>(
                identifier, 0, x, y, 100, 30, text);
        button->AutoResize();
        return button;
    }

// ===== BUTTON BUILDER (DEFINE AFTER BUTTON CLASS) =====
    class ButtonBuilder {
    private:
        std::shared_ptr<UltraCanvasButton> button;

    public:
        ButtonBuilder(const std::string& identifier, long x, long y, const std::string& text) {
            button = CreateAutoButton(identifier, x, y, text);
        }

        ButtonBuilder(const std::string& identifier, long id, int x, int y,  int w, int h, const std::string& text) {
            button = CreateButton(identifier, id, x, y, w, h, text);
        }

        // NOW these methods can call UltraCanvasButton methods because they're defined above
        ButtonBuilder& SetColors(const Color& normal, const Color& hover,
                                 const Color& pressed, const Color& disabled) {
            button->SetColors(normal, hover, pressed, disabled);
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

        ButtonBuilder& OnClick(std::function<void(const UCEvent&)> callback) {
            button->onClick = callback;
            return *this;
        }

        ButtonBuilder& OnHover(std::function<void(const UCEvent&)> enter, std::function<void(const UCEvent&)> leave = nullptr) {
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

// ===== PREDEFINED STYLES (AFTER EVERYTHING ELSE) =====
    namespace ButtonStyles {
        inline ButtonStyle Default() {
            return ButtonStyle();
        }

        inline ButtonStyle Primary() {
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

        // ... other styles
    }

} // namespace UltraCanvas
