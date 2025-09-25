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
    class UltraCanvasButton : public UltraCanvasUIElement {
    private:
        std::string text = "Button";
        ButtonStyle style;
        ButtonState currentState = ButtonState::Normal;
        bool pressed = false;

    public:
        // ===== CONSTRUCTOR =====
        UltraCanvasButton(const std::string& identifier = "Button", long id = 0,
                          long x = 0, long y = 0, long w = 100, long h = 30,
                          const std::string& buttonText = "Button");

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
        void SetColors(const Color& normal, const Color& hover, const Color& pressed, const Color& disabled);

        void SetTextColors(const Color& normal, const Color& hover, const Color& pressed, const Color& disabled);

        void SetFont(const std::string& fontFamily, float fontSize, FontWeight weight = FontWeight::Normal);

        void SetPadding(int left, int right, int top, int bottom);

        void SetCornerRadius(float radius);

        void SetShadow(bool enabled, const Color& color = Color(0, 0, 0, 64),
                       const Point2Di& offset = Point2Di(1, 1));

        void Click(const UCEvent& ev);

        void AutoResize();

        // ===== RENDERING =====
        void Render() override;

        // ===== EVENT HANDLING =====
        bool OnEvent(const UCEvent& event) override;

        // ===== EVENT CALLBACKS =====
        std::function<void(const UCEvent&)> onClick;
        std::function<void(const UCEvent&)> onPress;
        std::function<void(const UCEvent&)> onRelease;
        std::function<void(const UCEvent&)> onHoverEnter;
        std::function<void(const UCEvent&)> onHoverLeave;

    private:
        // ===== HELPER METHODS =====
        void UpdateButtonState();

        void GetCurrentColors(Color& bgColor, Color& textColor);
    };

// ===== FACTORY FUNCTIONS (AFTER BUTTON CLASS) =====
    inline std::shared_ptr<UltraCanvasButton> CreateButton(
            const std::string& identifier, long id, long x, long y, long w, long h,
            const std::string& text = "Button") {

        return UltraCanvasUIElementFactory::CreateWithID<UltraCanvasButton>(
                id, identifier, id, x, y, w, h, text);
    }

    inline std::shared_ptr<UltraCanvasButton> CreateAutoButton(
            const std::string& identifier, long x, long y, const std::string& text = "Button") {

        auto button = UltraCanvasUIElementFactory::Create<UltraCanvasButton>(
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
