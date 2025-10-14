// core/UltraCanvasButton.cpp
// Interactive button component implementation with split button support
// Version: 2.2.0
// Last Modified: 2025-01-11
// Author: UltraCanvas Framework

#include "UltraCanvasButton.h"
#include "UltraCanvasWindow.h"
#include <algorithm>

namespace UltraCanvas {

// ===== CONSTRUCTOR =====
    UltraCanvasButton::UltraCanvasButton(const std::string& identifier, long id,
                                         long x, long y, long w, long h,
                                         const std::string& buttonText)
            : UltraCanvasUIElement(identifier, id, x, y, w, h) {
        SetText(buttonText);
    }

// ===== STATE MANAGEMENT =====
    void UltraCanvasButton::UpdateButtonState() {
        ButtonState oldState = currentState;

        if (!IsEnabled()) {
            currentState = ButtonState::Disabled;
        } else if (pressed) {
            currentState = ButtonState::Pressed;
        } else if (IsHovered()) {
            currentState = ButtonState::Hovered;
        } else {
            currentState = ButtonState::Normal;
        }

        if (oldState != currentState) {
            RequestRedraw();
        }
    }

    void UltraCanvasButton::GetCurrentColors(Color& bgColor, Color& textColor, Color& iconColor) {
        switch (currentState) {
            case ButtonState::Disabled:
                bgColor = style.disabledColor;
                textColor = style.disabledTextColor;
                iconColor = style.disabledIconColor;
                break;
            case ButtonState::Pressed:
                bgColor = style.pressedColor;
                textColor = style.pressedTextColor;
                iconColor = style.pressedIconColor;
                break;
            case ButtonState::Hovered:
                bgColor = style.hoverColor;
                textColor = style.hoverTextColor;
                iconColor = style.hoverIconColor;
                break;
            default:
                bgColor = style.normalColor;
                textColor = style.normalTextColor;
                iconColor = style.normalIconColor;
                break;
        }
    }

    void UltraCanvasButton::GetSplitColors(Color& primaryBg, Color& primaryText,
                                           Color& secondaryBg, Color& secondaryText) {
        // Get base colors for primary section
        Color picon;
        GetCurrentColors(primaryBg, primaryText, picon); // Reusing primaryBg for icon placeholder

        // Secondary section colors based on state
        const SplitButtonStyle& split = style.splitStyle;

        if (currentState == ButtonState::Pressed) {
            secondaryBg = split.secondaryPressedColor;
        } else if (currentState == ButtonState::Hovered) {
            secondaryBg = split.secondaryHoverColor;
        } else {
            secondaryBg = split.secondaryBackgroundColor;
        }
        secondaryText = split.secondaryTextColor;
    }

// ===== STYLE CONVENIENCE METHODS =====
    void UltraCanvasButton::SetColors(const Color& normal, const Color& hover,
                                      const Color& pressed, const Color& disabled) {
        style.normalColor = normal;
        style.hoverColor = hover;
        style.pressedColor = pressed;
        style.disabledColor = disabled;
        RequestRedraw();
    }

    void UltraCanvasButton::SetTextColors(const Color& normal, const Color& hover,
                                          const Color& pressed, const Color& disabled) {
        style.normalTextColor = normal;
        style.hoverTextColor = hover;
        style.pressedTextColor = pressed;
        style.disabledTextColor = disabled;
        RequestRedraw();
    }

    void UltraCanvasButton::SetIconColors(const Color& normal, const Color& hover,
                                          const Color& pressed, const Color& disabled) {
        style.normalIconColor = normal;
        style.hoverIconColor = hover;
        style.pressedIconColor = pressed;
        style.disabledIconColor = disabled;
        RequestRedraw();
    }

    void UltraCanvasButton::SetFont(const std::string& fontFamily, float fontSize, FontWeight weight) {
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
        RequestRedraw();
    }

    void UltraCanvasButton::SetShadow(bool enabled, const Color& color, const Point2Di& offset) {
        style.hasShadow = enabled;
        style.shadowColor = color;
        style.shadowOffset = offset;
        RequestRedraw();
    }

// ===== LAYOUT CALCULATION =====
    void UltraCanvasButton::AutoResize() {
        if (!autoresize) return;

        auto ctx = GetRenderContext();
        if (!ctx) return;

        int newWidth = style.paddingLeft + style.paddingRight;
        int newHeight = style.paddingTop + style.paddingBottom;

        // Calculate text dimensions
        if (!text.empty()) {
            ctx->SetFontFace(style.fontFamily, style.fontWeight, FontSlant::Normal);
            ctx->SetFontSize(style.fontSize);
            int textWidth, textHeight;
            ctx->GetTextDimension(text, textWidth, textHeight);
            newWidth += textWidth;
            newHeight = std::max(newHeight, textHeight + style.paddingTop + style.paddingBottom);
        }

        // Add split button secondary text dimensions
        if (style.splitStyle.enabled && !style.splitStyle.secondaryText.empty()) {
            int secTextWidth, secTextHeight;
            ctx->GetTextDimension(style.splitStyle.secondaryText, secTextWidth, secTextHeight);

            if (style.splitStyle.horizontal) {
                newWidth += secTextWidth + 10; // Add padding for secondary section
                if (style.splitStyle.showSeparator) {
                    newWidth += style.splitStyle.separatorWidth;
                }
            } else {
                newHeight += secTextHeight + 10;
                if (style.splitStyle.showSeparator) {
                    newHeight += style.splitStyle.separatorWidth;
                }
            }
        }

        // Add icon dimensions
        if (HasIcon()) {
            newWidth += iconWidth;
            newHeight = std::max(newHeight, iconHeight + style.paddingTop + style.paddingBottom);

            if (!text.empty() && (iconPosition == IconPosition::Left || iconPosition == IconPosition::Right)) {
                newWidth += style.iconSpacing;
            }
        }

        SetSize(newWidth, newHeight);
    }

    void UltraCanvasButton::CalculateLayout() {
        if (style.splitStyle.enabled) {
            CalculateSplitLayout();
            return;
        }

        // Regular button layout calculation
        Rect2Di bounds = GetBounds();
        int contentX = bounds.x + style.paddingLeft;
        int contentY = bounds.y + style.paddingTop;
        int contentWidth = bounds.width - style.paddingLeft - style.paddingRight;
        int contentHeight = bounds.height - style.paddingTop - style.paddingBottom;

        // Reset rectangles
        iconRect = Rect2Di(0, 0, 0, 0);
        textRect = Rect2Di(0, 0, 0, 0);
        secondaryTextRect = Rect2Di(0, 0, 0, 0);
        primarySectionRect = Rect2Di(0, 0, 0, 0);
        secondarySectionRect = Rect2Di(0, 0, 0, 0);

        if (HasIcon() && !text.empty()) {
            // Both icon and text
            if (iconPosition == IconPosition::Left) {
                iconRect = Rect2Di(contentX, contentY + (contentHeight - iconHeight) / 2,
                                   iconWidth, iconHeight);
                textRect = Rect2Di(contentX + iconWidth + style.iconSpacing, contentY,
                                   contentWidth - iconWidth - style.iconSpacing, contentHeight);
            } else if (iconPosition == IconPosition::Right) {
                textRect = Rect2Di(contentX, contentY,
                                   contentWidth - iconWidth - style.iconSpacing, contentHeight);
                iconRect = Rect2Di(contentX + contentWidth - iconWidth,
                                   contentY + (contentHeight - iconHeight) / 2,
                                   iconWidth, iconHeight);
            } else if (iconPosition == IconPosition::Top) {
                iconRect = Rect2Di(contentX + (contentWidth - iconWidth) / 2, contentY,
                                   iconWidth, iconHeight);
                textRect = Rect2Di(contentX, contentY + iconHeight + style.iconSpacing,
                                   contentWidth, contentHeight - iconHeight - style.iconSpacing);
            } else if (iconPosition == IconPosition::Bottom) {
                textRect = Rect2Di(contentX, contentY,
                                   contentWidth, contentHeight - iconHeight - style.iconSpacing);
                iconRect = Rect2Di(contentX + (contentWidth - iconWidth) / 2,
                                   contentY + contentHeight - iconHeight - style.iconSpacing,
                                   iconWidth, iconHeight);
            } else if (iconPosition == IconPosition::Center) {
                iconRect = Rect2Di(contentX + (contentWidth - iconWidth) / 2,
                                   contentY + (contentHeight - iconHeight) / 2,
                                   iconWidth, iconHeight);
            }
        } else if (HasIcon()) {
            // Icon only
            iconRect = Rect2Di(contentX + (contentWidth - iconWidth) / 2,
                               contentY + (contentHeight - iconHeight) / 2,
                               iconWidth, iconHeight);
        } else if (!text.empty()) {
            // Text only
            textRect = Rect2Di(contentX, contentY, contentWidth, contentHeight);
        }

        layoutDirty = false;
    }

    void UltraCanvasButton::CalculateSplitLayout() {
        Rect2Di bounds = GetBounds();
        const SplitButtonStyle& split = style.splitStyle;

        if (split.horizontal) {
            // Horizontal split layout
            int primaryWidth, secondaryWidth;

            if (split.primaryRatio == 0) {
                int textWidth, textHeight;
                auto ctx  = GetRenderContext();
                ctx->SetFontFace(style.fontFamily, style.fontWeight, FontSlant::Normal);
                ctx->GetTextDimension(split.secondaryText, textWidth, textHeight);
                primaryWidth = bounds.width - (textWidth + style.paddingLeft + style.paddingRight + split.separatorWidth);
            } else {
                primaryWidth = bounds.width * split.primaryRatio;
            }
            secondaryWidth = bounds.width - primaryWidth;

            if (split.showSeparator) {
                primaryWidth -= split.separatorWidth / 2;
                secondaryWidth -= split.separatorWidth / 2;
            }

            primarySectionRect = Rect2Di(bounds.x, bounds.y, primaryWidth, bounds.height);
            secondarySectionRect = Rect2Di(bounds.x + primaryWidth +
                                           (split.showSeparator ? split.separatorWidth : 0),
                                           bounds.y, secondaryWidth, bounds.height);

            // Text rectangles within sections
            textRect = Rect2Di(primarySectionRect.x + style.paddingLeft,
                               primarySectionRect.y + style.paddingTop,
                               primarySectionRect.width - style.paddingLeft - style.paddingRight,
                               primarySectionRect.height - style.paddingTop - style.paddingBottom);

            secondaryTextRect = Rect2Di(secondarySectionRect.x + 4,
                                        secondarySectionRect.y + style.paddingTop,
                                        secondarySectionRect.width - 8,
                                        secondarySectionRect.height - style.paddingTop - style.paddingBottom);

            // Icon positioning for split button (always in primary section)
            if (HasIcon()) {
                if (iconPosition == IconPosition::Left) {
                    iconRect = Rect2Di(textRect.x, textRect.y + (textRect.height - iconHeight) / 2,
                                       iconWidth, iconHeight);
                    textRect.x += iconWidth + style.iconSpacing;
                    textRect.width -= iconWidth + style.iconSpacing;
                } else if (iconPosition == IconPosition::Right) {
                    iconRect = Rect2Di(textRect.x + textRect.width - iconWidth,
                                       textRect.y + (textRect.height - iconHeight) / 2,
                                       iconWidth, iconHeight);
                    textRect.width -= iconWidth + style.iconSpacing;
                }
            }
        } else {
            // Vertical split layout
            int primaryHeight = bounds.height * split.primaryRatio;
            int secondaryHeight = bounds.height - primaryHeight;

            if (split.showSeparator) {
                primaryHeight -= split.separatorWidth / 2;
                secondaryHeight -= split.separatorWidth / 2;
            }

            primarySectionRect = Rect2Di(bounds.x, bounds.y, bounds.width, primaryHeight);
            secondarySectionRect = Rect2Di(bounds.x, bounds.y + primaryHeight +
                                                     (split.showSeparator ? split.separatorWidth : 0),
                                           bounds.width, secondaryHeight);

            // Text rectangles within sections
            textRect = Rect2Di(primarySectionRect.x + style.paddingLeft,
                               primarySectionRect.y + style.paddingTop,
                               primarySectionRect.width - style.paddingLeft - style.paddingRight,
                               primarySectionRect.height - style.paddingTop - style.paddingBottom);

            secondaryTextRect = Rect2Di(secondarySectionRect.x + style.paddingLeft,
                                        secondarySectionRect.y + 2,
                                        secondarySectionRect.width - style.paddingLeft - style.paddingRight,
                                        secondarySectionRect.height - 4);
        }

        layoutDirty = false;
    }

    bool UltraCanvasButton::IsPointInPrimarySection(int x, int y) const {
        if (!style.splitStyle.enabled) return true;
        return primarySectionRect.Contains(x, y);
    }

    bool UltraCanvasButton::IsPointInSecondarySection(int x, int y) const {
        if (!style.splitStyle.enabled) return false;
        return secondarySectionRect.Contains(x, y);
    }

// ===== RENDERING =====
    void UltraCanvasButton::DrawIcon(IRenderContext* ctx) {
        if (!HasIcon() || iconRect.width <= 0) return;

        Color bgColor, textColor, iconColor;
        GetCurrentColors(bgColor, textColor, iconColor);

        // Apply icon tinting if needed
        if (iconColor != Colors::White) {
            // TODO: Apply color tinting to icon
        }

        // Draw the icon
        if (scaleIconToFit) {
            ctx->DrawImage(iconPath, iconRect);
        } else {
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

        // Center text vertically in rectangle
        int textWidth, textHeight;
        ctx->GetTextDimension(text, textWidth, textHeight);

        Point2Df textPos;
        if (style.textAlign == TextAlignment::Center) {
            textPos.x = textRect.x + (textRect.width - textWidth) / 2;
        } else if (style.textAlign == TextAlignment::Right) {
            textPos.x = textRect.x + textRect.width - textWidth;
        } else {
            textPos.x = textRect.x;
        }
        textPos.y = textRect.y + (textRect.height - textHeight) / 2;

        ctx->DrawText(text, textPos);
    }

    void UltraCanvasButton::DrawSplitButton(IRenderContext* ctx) {
        const SplitButtonStyle& split = style.splitStyle;
        Rect2Di bounds = GetBounds();

        // Get colors for both sections
        Color primaryBg, primaryText, secondaryBg, secondaryText;
        GetSplitColors(primaryBg, primaryText, secondaryBg, secondaryText);

        // Draw shadow if enabled and not pressed
        if (style.hasShadow && currentState != ButtonState::Pressed) {
            Rect2Di shadowBounds = bounds;
            shadowBounds.x += style.shadowOffset.x;
            shadowBounds.y += style.shadowOffset.y;

            ctx->DrawFilledRectangle(shadowBounds, style.shadowColor, 0,
                                     Colors::Transparent, style.cornerRadius);
        }

        // Adjust bounds for pressed effect
        Rect2Di drawBounds = bounds;
        if (currentState == ButtonState::Pressed) {
            drawBounds.x += 1;
            drawBounds.y += 1;
        }

        // Draw primary section background
        if (split.horizontal) {
            // Left side with left corners rounded
            Rect2Di primaryDraw = primarySectionRect;
            if (currentState == ButtonState::Pressed) {
                primaryDraw.x += 1;
                primaryDraw.y += 1;
            }
            ctx->DrawFilledRectangle(primaryDraw, primaryBg, 0, Colors::Transparent,
                                     0);

            // Right side with right corners rounded
            Rect2Di secondaryDraw = secondarySectionRect;
            if (currentState == ButtonState::Pressed) {
                secondaryDraw.x += 1;
                secondaryDraw.y += 1;
            }
            ctx->DrawFilledRectangle(secondaryDraw, secondaryBg, 0, Colors::Transparent,
                                     0);
        } else {
            // Top section with top corners rounded
            Rect2Di primaryDraw = primarySectionRect;
            if (currentState == ButtonState::Pressed) {
                primaryDraw.x += 1;
                primaryDraw.y += 1;
            }
            ctx->DrawFilledRectangle(primaryDraw, primaryBg, 0, Colors::Transparent,
                                     style.cornerRadius);

            // Bottom section with bottom corners rounded
            Rect2Di secondaryDraw = secondarySectionRect;
            if (currentState == ButtonState::Pressed) {
                secondaryDraw.x += 1;
                secondaryDraw.y += 1;
            }
            ctx->DrawFilledRectangle(secondaryDraw, secondaryBg, 0, Colors::Transparent,
                                     style.cornerRadius);
        }

        // Draw separator if enabled
        if (split.showSeparator) {
            ctx->SetStrokePaint(split.separatorColor);
            ctx->SetStrokeWidth(split.separatorWidth);

            if (split.horizontal) {
                int separatorX = primarySectionRect.x + primarySectionRect.width;
                if (currentState == ButtonState::Pressed) separatorX += 1;
                ctx->DrawLine(Point2Di(separatorX, drawBounds.y),
                              Point2Di(separatorX, drawBounds.y + drawBounds.height));
            } else {
                int separatorY = primarySectionRect.y + primarySectionRect.height;
                if (currentState == ButtonState::Pressed) separatorY += 1;
                ctx->DrawLine(Point2Di(drawBounds.x, separatorY),
                              Point2Di(drawBounds.x + drawBounds.width, separatorY));
            }
        }

        // Draw icon in primary section
        DrawIcon(ctx);

        // Draw primary text
        if (!text.empty()) {
            ctx->SetTextPaint(primaryText);
            ctx->SetFontFace(style.fontFamily, style.fontWeight, FontSlant::Normal);
            ctx->SetFontSize(style.fontSize);

            int textWidth, textHeight;
            ctx->GetTextDimension(text, textWidth, textHeight);

            Point2Df primaryTextPos;
            if (HasIcon() && (iconPosition == IconPosition::Left || iconPosition == IconPosition::Right)) {
                // Text already positioned by icon layout
                primaryTextPos.x = textRect.x;
                primaryTextPos.y = textRect.y + (textRect.height - textHeight) / 2;
            } else {
                // Center text in primary section
                primaryTextPos.x = textRect.x + (textRect.width - textWidth) / 2;
                primaryTextPos.y = textRect.y + (textRect.height - textHeight) / 2;
            }

            if (currentState == ButtonState::Pressed) {
                primaryTextPos.x += 1;
                primaryTextPos.y += 1;
            }

            ctx->DrawText(text, primaryTextPos);
        }

        // Draw secondary text
        if (!split.secondaryText.empty()) {
            ctx->SetTextPaint(secondaryText);
            ctx->SetFontFace(style.fontFamily, style.fontWeight, FontSlant::Normal);
            ctx->SetFontSize(style.fontSize * 0.9f);  // Slightly smaller font

            int textWidth, textHeight;
            ctx->GetTextDimension(split.secondaryText, textWidth, textHeight);

            Point2Df secondaryTextPos(
                    secondaryTextRect.x + (secondaryTextRect.width - textWidth) / 2,
                    secondaryTextRect.y + (secondaryTextRect.height - textHeight) / 2
            );

            if (currentState == ButtonState::Pressed) {
                secondaryTextPos.x += 1;
                secondaryTextPos.y += 1;
            }

            ctx->DrawText(split.secondaryText, secondaryTextPos);
        }

        // Draw border around entire button
        if (style.borderWidth > 0) {
            ctx->SetStrokePaint(style.borderColor);
            ctx->SetStrokeWidth(style.borderWidth);
            ctx->DrawRoundedRectangle(drawBounds, style.cornerRadius);
        }

        // Draw focus indicator if focused
        if (IsFocused()) {
            ctx->SetStrokePaint(style.focusedColor);
            ctx->DrawRoundedRectangle(drawBounds, style.cornerRadius);
        }
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

        // Handle split button rendering
        if (style.splitStyle.enabled) {
            DrawSplitButton(ctx);
        } else {
            // Regular button rendering
            Color bgColor, textColor, iconColor;
            GetCurrentColors(bgColor, textColor, iconColor);

            Rect2Di bounds = GetBounds();

            // Draw shadow if enabled and not pressed
            if (style.hasShadow && currentState != ButtonState::Pressed) {
                Rect2Di shadowBounds = bounds;
                shadowBounds.x += style.shadowOffset.x;
                shadowBounds.y += style.shadowOffset.y;

                ctx->DrawFilledRectangle(shadowBounds, style.shadowColor, 0,
                                         Colors::Transparent, style.cornerRadius);
            }

            // Adjust bounds for pressed effect
            if (currentState == ButtonState::Pressed) {
                bounds.x += 1;
                bounds.y += 1;
            }

            // Draw button background
            ctx->DrawFilledRectangle(bounds, bgColor, style.borderWidth,
                                     style.borderColor, style.cornerRadius);

            // Draw icon
            DrawIcon(ctx);

            // Draw text
            DrawText(ctx);

            // Draw focus indicator if focused
            if (IsFocused()) {
                ctx->SetStrokePaint(style.focusedColor);
                ctx->DrawRoundedRectangle(bounds, style.cornerRadius);
            }
        }

        ctx->PopState();
    }

// ===== EVENT HANDLING =====
    void UltraCanvasButton::Click(const UCEvent& event) {
        if (style.splitStyle.enabled) {
            // Determine which section was clicked
            if (IsPointInPrimarySection(event.x, event.y)) {
                if (onClick) onClick();
            } else if (IsPointInSecondarySection(event.x, event.y)) {
                if (onSecondaryClick) onSecondaryClick();
            }
        } else {
            // Regular button click
            if (onClick) onClick();
        }
    }

    bool UltraCanvasButton::OnEvent(const UCEvent& event) {
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
                    return true;
                }
                break;

            case UCEventType::MouseEnter:
                SetHovered(true);
                if (onHoverEnter) onHoverEnter();
                RequestRedraw();
                return true;

            case UCEventType::MouseLeave:
                SetHovered(false);
                if (onHoverLeave) onHoverLeave();
                RequestRedraw();
                return true;

            case UCEventType::KeyDown:
                if (IsFocused() && (event.virtualKey == UCKeys::Space || event.virtualKey == UCKeys::Return)) {
                    pressed = true;
                    if (onPress) onPress();
                    RequestRedraw();
                    return true;
                }
                break;

            case UCEventType::KeyUp:
                if (IsFocused() && pressed && (event.virtualKey == UCKeys::Space || event.virtualKey == UCKeys::Return)) {
                    pressed = false;
                    if (onRelease) onRelease();
                    Click(event);
                    RequestRedraw();
                    return true;
                }
                break;
        }

        return false;
    }

} // namespace UltraCanvas