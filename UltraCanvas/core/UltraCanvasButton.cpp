// core/UltraCanvasButton.cpp
// Interactive button component with icon and text display support
// Version: 2.1.0
// Last Modified: 2025-01-11
// Author: UltraCanvas Framework

#include "UltraCanvasButton.h"
#include "UltraCanvasApplication.h"
#include <string>
#include <functional>
#include <algorithm>

namespace UltraCanvas {

    UltraCanvasButton::UltraCanvasButton(const std::string &identifier, long id, long x, long y, long w, long h,
                                         const std::string &buttonText)
            : UltraCanvasUIElement(identifier, id, x, y, w, h), text(buttonText) {

        currentState = ButtonState::Normal;
        pressed = false;
        layoutDirty = true;
    }

    void UltraCanvasButton::SetColors(const Color &normal, const Color &hover, const Color &pressed, const Color &disabled) {
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

    void UltraCanvasButton::SetIconColors(const Color &normal, const Color &hover, const Color &pressed,
                                          const Color &disabled) {
        style.normalIconColor = normal;
        style.hoverIconColor = hover;
        style.pressedIconColor = pressed;
        style.disabledIconColor = disabled;
    }

    void UltraCanvasButton::SetFont(const std::string &fontFamily, float fontSize, FontWeight weight) {
        style.fontFamily = fontFamily;
        style.fontSize = fontSize;
        style.fontWeight = weight;
        layoutDirty = true;
        AutoResize();
    }

    void UltraCanvasButton::SetPadding(int left, int right, int top, int bottom) {
        style.paddingLeft = left;
        style.paddingRight = right;
        style.paddingTop = top;
        style.paddingBottom = bottom;
        layoutDirty = true;
        AutoResize();
    }

    void UltraCanvasButton::SetIconSpacing(int spacing) {
        style.iconSpacing = spacing;
        layoutDirty = true;
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
        if (!IsEnabled() || !IsVisible()) return;

        if (onClick) {
            onClick();
        }
    }

    void UltraCanvasButton::AutoResize() {
        if (!autoresize || text.empty()) return;

        auto ctx = GetRenderContext();
        if (!ctx) return;

        // Calculate text size
        ctx->SetFontFace(style.fontFamily, style.fontWeight, FontSlant::Normal);
        ctx->SetFontSize(style.fontSize);
        int textWidth, textHeight;
        ctx->GetTextDimension(text, textWidth, textHeight);

        // Calculate total width including icon
        int totalWidth = textWidth + style.paddingLeft + style.paddingRight;
        int totalHeight = textHeight + style.paddingTop + style.paddingBottom;

        if (HasIcon()) {
            if (iconPosition == IconPosition::Left || iconPosition == IconPosition::Right) {
                totalWidth += iconWidth + style.iconSpacing;
                totalHeight = std::max(totalHeight, iconHeight + style.paddingTop + style.paddingBottom);
            } else if (iconPosition == IconPosition::Top || iconPosition == IconPosition::Bottom) {
                totalHeight += iconHeight + style.iconSpacing;
                totalWidth = std::max(totalWidth, iconWidth + style.paddingLeft + style.paddingRight);
            } else if (iconPosition == IconPosition::Center) {
                totalWidth = iconWidth + style.paddingLeft + style.paddingRight;
                totalHeight = iconHeight + style.paddingTop + style.paddingBottom;
            }
        }

        SetWidth(totalWidth);
        SetHeight(totalHeight);
        layoutDirty = true;
    }

    void UltraCanvasButton::CalculateLayout() {
        int contentWidth = GetWidth() - style.paddingLeft - style.paddingRight;
        int contentHeight = GetHeight() - style.paddingTop - style.paddingBottom;

        int contentX = GetX() + style.paddingLeft;
        int contentY = GetY() + style.paddingTop;

        // Reset rects
        iconRect = Rect2Di(0, 0, 0, 0);
        textRect = Rect2Di(0, 0, 0, 0);

        if (iconPosition == IconPosition::Center && HasIcon()) {
            // Icon only, centered
            int iconX = contentX + (contentWidth - iconWidth) / 2;
            int iconY = contentY + (contentHeight - iconHeight) / 2;
            iconRect = Rect2Di(iconX, iconY, iconWidth, iconHeight);
        } else if (HasIcon() && !text.empty()) {
            // Both icon and text
            auto ctx = GetRenderContext();
            if (ctx) {
                ctx->SetFontFace(style.fontFamily, style.fontWeight, FontSlant::Normal);
                ctx->SetFontSize(style.fontSize);
                int textWidth, textHeight;
                ctx->GetTextDimension(text, textWidth, textHeight);

                switch (iconPosition) {
                    case IconPosition::Left: {
                        int totalContentWidth = iconWidth + style.iconSpacing + textWidth;
                        int startX = contentX + (contentWidth - totalContentWidth) / 2;

                        iconRect = Rect2Di(
                                startX,
                                contentY + (contentHeight - iconHeight) / 2,
                                iconWidth,
                                iconHeight
                        );

                        textRect = Rect2Di(
                                startX + iconWidth + style.iconSpacing,
                                contentY + (contentHeight - textHeight) / 2,
                                textWidth,
                                textHeight
                        );
                        break;
                    }
                    case IconPosition::Right: {
                        int totalContentWidth = textWidth + style.iconSpacing + iconWidth;
                        int startX = contentX + (contentWidth - totalContentWidth) / 2;

                        textRect = Rect2Di(
                                startX,
                                contentY + (contentHeight - textHeight) / 2,
                                textWidth,
                                textHeight
                        );

                        iconRect = Rect2Di(
                                startX + textWidth + style.iconSpacing,
                                contentY + (contentHeight - iconHeight) / 2,
                                iconWidth,
                                iconHeight
                        );
                        break;
                    }
                    case IconPosition::Top: {
                        int totalContentHeight = iconHeight + style.iconSpacing + textHeight;
                        int startY = contentY + (contentHeight - totalContentHeight) / 2;

                        iconRect = Rect2Di(
                                contentX + (contentWidth - iconWidth) / 2,
                                startY,
                                iconWidth,
                                iconHeight
                        );

                        textRect = Rect2Di(
                                contentX + (contentWidth - textWidth) / 2,
                                startY + iconHeight + style.iconSpacing,
                                textWidth,
                                textHeight
                        );
                        break;
                    }
                    case IconPosition::Bottom: {
                        int totalContentHeight = textHeight + style.iconSpacing + iconHeight;
                        int startY = contentY + (contentHeight - totalContentHeight) / 2;

                        textRect = Rect2Di(
                                contentX + (contentWidth - textWidth) / 2,
                                startY,
                                textWidth,
                                textHeight
                        );

                        iconRect = Rect2Di(
                                contentX + (contentWidth - iconWidth) / 2,
                                startY + textHeight + style.iconSpacing,
                                iconWidth,
                                iconHeight
                        );
                        break;
                    }
                    default:
                        break;
                }
            }
        } else if (HasIcon()) {
            // Icon only
            iconRect = Rect2Di(
                    contentX + (contentWidth - iconWidth) / 2,
                    contentY + (contentHeight - iconHeight) / 2,
                    iconWidth,
                    iconHeight
            );
        } else if (!text.empty()) {
            // Text only
            auto ctx = GetRenderContext();
            if (ctx) {
                ctx->SetFontFace(style.fontFamily, style.fontWeight, FontSlant::Normal);
                ctx->SetFontSize(style.fontSize);
                int textWidth, textHeight;
                ctx->GetTextDimension(text, textWidth, textHeight);

                textRect = Rect2Di(
                        contentX + (contentWidth - textWidth) / 2,
                        contentY + (contentHeight - textHeight) / 2,
                        textWidth,
                        textHeight
                );
            }
        }

        layoutDirty = false;
    }

    void UltraCanvasButton::DrawIcon(IRenderContext* ctx) {
        if (!HasIcon() || iconRect.width <= 0) return;

        // Get current icon color for tinting
        Color bgColor, textColor, iconColor;
        GetCurrentColors(bgColor, textColor, iconColor);

        // Draw the icon
        if (scaleIconToFit) {
            // Scale to fit the icon rectangle
            ctx->DrawImage(iconPath, iconRect);
        } else {
            // Draw at original size, centered in icon rectangle
            ctx->DrawImage(iconPath, iconRect);
        }
    }

    void UltraCanvasButton::DrawText(IRenderContext* ctx) {
        if (text.empty() || textRect.width <= 0) return;

        Color bgColor, textColor, iconColor;
        GetCurrentColors(bgColor, textColor, iconColor);

        ctx->SetTextPaint(textColor);
        ctx->SetFontFace(style.fontFamily, style.fontWeight, FontSlant::Normal);
        ctx->SetFontSize(style.fontSize);

        // Draw text centered in text rectangle
        Point2Df textPos(textRect.x, textRect.y);
        ctx->DrawText(text, textPos);
    }

    void UltraCanvasButton::Render() {
        if (!IsVisible()) return;

        auto ctx = GetRenderContext();
        if (!ctx) return;

        UpdateButtonState();

        // Update layout if needed
        if (layoutDirty) {
            CalculateLayout();
        }
        ctx->PushState();
        // Get current colors based on state
        Color bgColor, textColor, iconColor;
        GetCurrentColors(bgColor, textColor, iconColor);

        Rect2Di bounds = GetBounds();

        // Draw shadow if enabled and not pressed
        if (style.hasShadow && currentState != ButtonState::Pressed) {
            Rect2Di shadowBounds = bounds;
            shadowBounds.x += style.shadowOffset.x;
            shadowBounds.y += style.shadowOffset.y;

            ctx->DrawFilledRectangle(shadowBounds, style.shadowColor, 0, Colors::Transparent, style.cornerRadius);
        }

        // Adjust bounds for pressed effect
        if (currentState == ButtonState::Pressed) {
            bounds.x += 1;
            bounds.y += 1;
        }

        // Draw button background
        ctx->DrawFilledRectangle(bounds, bgColor, style.borderWidth, style.borderColor, style.cornerRadius);

        // Draw icon
        DrawIcon(ctx);

        // Draw text
        DrawText(ctx);

        // Draw focus indicator if focused
        if (IsFocused()) {
            ctx->SetStrokePaint(style.focusedColor);
            ctx->DrawRoundedRectangle(bounds, style.cornerRadius);
        }
        ctx->PopState();
    }

    bool UltraCanvasButton::OnEvent(const UCEvent &event) {
        if (!IsEnabled() || !IsVisible()) return false;

        switch (event.type) {
            case UCEventType::MouseDown:
                if (Contains(event.x, event.y)) {
                    pressed = true;
                    SetFocus();
                    if (onPress) onPress();
                    RequestRedraw();
                    return true;
                }
                break;

            case UCEventType::MouseUp:
                if (pressed) {
                    bool wasInside = Contains(event.x, event.y);
                    pressed = false;
                    if (onRelease) onRelease();
                    if (wasInside) {
                        Click(event);
                    }
                    RequestRedraw();
                    return true;
                }
                break;

            case UCEventType::MouseMove:
                if (pressed) {
                    bool inside = Contains(event.x, event.y);
                    SetHovered(inside);
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
                        return true;
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

    void UltraCanvasButton::GetCurrentColors(Color &bgColor, Color &textColor, Color &iconColor) {
        switch (currentState) {
            case ButtonState::Normal:
                bgColor = style.normalColor;
                textColor = style.normalTextColor;
                iconColor = style.normalIconColor;
                break;
            case ButtonState::Hovered:
                bgColor = style.hoverColor;
                textColor = style.hoverTextColor;
                iconColor = style.hoverIconColor;
                break;
            case ButtonState::Pressed:
                bgColor = style.pressedColor;
                textColor = style.pressedTextColor;
                iconColor = style.pressedIconColor;
                break;
            case ButtonState::Disabled:
                bgColor = style.disabledColor;
                textColor = style.disabledTextColor;
                iconColor = style.disabledIconColor;
                break;
        }
    }
}