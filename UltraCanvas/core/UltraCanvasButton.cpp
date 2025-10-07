// core/UltraCanvasButton.cpp
// Refactored button component using unified base system
// Version: 2.0.0
// Last Modified: 2024-12-19
// Author: UltraCanvas Framework

#include "UltraCanvasButton.h"
#include "UltraCanvasApplication.h"
#include <string>
#include <functional>

namespace UltraCanvas {

    UltraCanvasButton::UltraCanvasButton(const std::string &identifier, long id, long x, long y, long w, long h,
                                         const std::string &buttonText)
            : UltraCanvasUIElement(identifier, id, x, y, w, h), text(buttonText) {

        currentState = ButtonState::Normal;
        pressed = false;
    }

    void
    UltraCanvasButton::SetColors(const Color &normal, const Color &hover, const Color &pressed, const Color &disabled) {
        style.normalColor = normal;
        style.hoverColor = hover;
        style.pressedColor = pressed;
        style.disabledColor = disabled;
    }

    void UltraCanvasButton::SetTextColors(const Color &normal, const Color &hover, const Color &pressed,
                                          const Color &disabled) {
        style.normalTextColor = normal;
        style.hoverTextColor = hover;
        style.pressedTextColor = pressed;
        style.disabledTextColor = disabled;
    }

    void UltraCanvasButton::SetFont(const std::string &fontFamily, float fontSize, FontWeight weight) {
        style.fontFamily = fontFamily;
        style.fontSize = fontSize;
        style.fontWeight = weight;
        AutoResize();
    }

    void UltraCanvasButton::SetPadding(int left, int right, int top, int bottom) {
        style.paddingLeft = left;
        style.paddingRight = right;
        style.paddingTop = top;
        style.paddingBottom = bottom;
        AutoResize();
    }

    void UltraCanvasButton::SetCornerRadius(float radius) {
        style.cornerRadius = radius;
    }

    void UltraCanvasButton::SetShadow(bool enabled, const Color &color, const Point2Di &offset) {
        style.hasShadow = enabled;
        style.shadowColor = color;
        style.shadowOffset = offset;
    }

    void UltraCanvasButton::Click(const UCEvent &ev) {
        if (IsEnabled()) {
            if (onClick) {
                onClick();
            }
            UCEvent evclick = ev;
            evclick.type = UCEventType::ButtonClick;
            evclick.targetElement = this;
            UltraCanvasApplication::GetInstance()->PushEvent(ev);
        }
    }

    void UltraCanvasButton::AutoResize() {
        if (!autoresize || text.empty()) return;

        // Simple auto-resize logic
        auto ctx = GetRenderContext();
        if (ctx) {
            int txtw, txth;
            ctx->GetTextDimension(text, txtw, txth);
            int newWidth = txtw +
                           style.paddingLeft + style.paddingRight;
            int newHeight = txth +
                            style.paddingTop + style.paddingBottom;

            SetWidth(newWidth);
            SetHeight(newHeight);
            isNeedAutoresize = false;
        }
        isNeedAutoresize = true;
    }

    void UltraCanvasButton::Render() {
        IRenderContext *ctx = GetRenderContext();
        if (!IsVisible() || !ctx) return;

        if (isNeedAutoresize) {
            AutoResize();
        }

        ctx->PushState();

        UpdateButtonState();

        Color bgColor, textColor;
        GetCurrentColors(bgColor, textColor);

        Rect2Di bounds = GetBounds();

        // Draw background
        ctx->DrawFilledRectangle(bounds, bgColor, style.borderWidth, style.borderColor, style.cornerRadius);

        // Draw text
        if (!text.empty()) {
            // Set text style with middle baseline
            TextStyle centeredTextStyle;
            centeredTextStyle.textColor = textColor;
            centeredTextStyle.alignment = TextAlignment::Center;
            centeredTextStyle.verticalAlignement = TextVerticalAlignement::Middle;  // This ensures vertical centering
            ctx->SetTextStyle(centeredTextStyle);
            ctx->SetFontStyle({.fontFamily=style.fontFamily, .fontSize=style.fontSize});
            ctx->DrawTextInRect(text, bounds);
        }

        // Draw focus indicator
        if (IsFocused()) {
            // Simple focus rectangle
            Rect2Di focusRect(bounds.x + 2, bounds.y + 2, bounds.width - 4, bounds.height - 4);
            ctx->DrawFilledRectangle(focusRect, Colors::Transparent, 1, Colors::Blue);
        }

        ctx->PopState();
    }

    bool UltraCanvasButton::OnEvent(const UCEvent &event) {
        if (!IsActive() || !IsVisible()) return false;

        switch (event.type) {
            case UCEventType::MouseDown:
                if (Contains(event.x, event.y) && IsEnabled()) {
                    pressed = true;
                    SetFocus(true);
                    if (onPress) onPress();
                }
                break;

            case UCEventType::MouseUp:
                if (IsEnabled()) {
                    bool wasPressed = pressed;
                    pressed = false;
                    if (onRelease) onRelease();

                    if (wasPressed && Contains(event.x, event.y)) {
                        Click(event);
                    }
                    RequestRedraw();
                }
                break;

            case UCEventType::MouseEnter:
                SetHovered(true);
                if (onHoverEnter) onHoverEnter();
                RequestRedraw();
                break;

            case UCEventType::MouseLeave:
                SetHovered(false);
                if (pressed) {
                    pressed = false;
                    if (onRelease) onRelease();
                }
                if (onHoverLeave) onHoverLeave();
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

    void UltraCanvasButton::UpdateButtonState() {
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

    void UltraCanvasButton::GetCurrentColors(Color &bgColor, Color &textColor) {
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
}