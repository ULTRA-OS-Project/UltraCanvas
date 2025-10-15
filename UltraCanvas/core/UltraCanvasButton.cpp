// core/UltraCanvasButton.cpp
// Interactive button component implementation with secondary icon support
// Version: 2.3.1
// Last Modified: 2025-01-15
// Author: UltraCanvas Framework

#include "UltraCanvasButton.h"
#include <algorithm>

namespace UltraCanvas {

// ===== CONSTRUCTOR =====
    UltraCanvasButton::UltraCanvasButton(const std::string& identifier, long id,
                                         long x, long y, long w, long h,
                                         const std::string& buttonText)
            : UltraCanvasUIElement(identifier, id, x, y, w, h), text(buttonText) {
    }

// ===== SPLIT BUTTON METHODS =====
    void UltraCanvasButton::SetSplitEnabled(bool enabled) {
        style.splitStyle.enabled = enabled;
        layoutDirty = true;
        RequestRedraw();
    }

    void UltraCanvasButton::SetSplitRatio(float primaryRatio) {
        style.splitStyle.primaryRatio = primaryRatio;
        layoutDirty = true;
        RequestRedraw();
    }

    void UltraCanvasButton::SetSplitHorizontal(bool horizontal) {
        style.splitStyle.horizontal = horizontal;
        layoutDirty = true;
        RequestRedraw();
    }

    void UltraCanvasButton::SetSplitSecondaryText(const std::string& secondaryText) {
        style.splitStyle.secondaryText = secondaryText;
        layoutDirty = true;
        AutoResize();
        RequestRedraw();
    }

    void UltraCanvasButton::SetSplitSecondaryIcon(const std::string& iconPath,
                                                  ButtonSecondaryIconPosition position) {
        style.splitStyle.secondaryIconPath = iconPath;
        style.splitStyle.secondaryIconPosition = position;
        layoutDirty = true;
        AutoResize();
        RequestRedraw();
    }

    void UltraCanvasButton::SetSplitSecondaryIconSize(int width, int height) {
        style.splitStyle.secondaryIconWidth = width;
        style.splitStyle.secondaryIconHeight = height;
        layoutDirty = true;
        AutoResize();
        RequestRedraw();
    }

    void UltraCanvasButton::SetSplitSecondaryIconSpacing(int spacing) {
        style.splitStyle.secondaryIconSpacing = spacing;
        layoutDirty = true;
        AutoResize();
        RequestRedraw();
    }

    void UltraCanvasButton::SetSplitSecondaryIconPosition(ButtonSecondaryIconPosition position) {
        style.splitStyle.secondaryIconPosition = position;
        layoutDirty = true;
        RequestRedraw();
    }

    void UltraCanvasButton::SetSplitSecondaryIconColors(const Color& normal, const Color& hover,
                                                        const Color& pressed, const Color& disabled) {
        style.splitStyle.secondaryNormalIconColor = normal;
        style.splitStyle.secondaryHoverIconColor = hover;
        style.splitStyle.secondaryPressedIconColor = pressed;
        style.splitStyle.secondaryDisabledIconColor = disabled;
        RequestRedraw();
    }

    void UltraCanvasButton::SetSplitColors(const Color& secBg, const Color& secText,
                                           const Color& secHover, const Color& secPressed) {
        style.splitStyle.secondaryBackgroundColor = secBg;
        style.splitStyle.secondaryTextColor = secText;
        style.splitStyle.secondaryHoverColor = secHover;
        style.splitStyle.secondaryPressedColor = secPressed;
        RequestRedraw();
    }

    void UltraCanvasButton::SetSplitSeparator(bool show, const Color& color, float width) {
        style.splitStyle.showSeparator = show;
        style.splitStyle.separatorColor = color;
        style.splitStyle.separatorWidth = width;
        layoutDirty = true;
        RequestRedraw();
    }

// ===== TEXT & ICON METHODS =====
    void UltraCanvasButton::SetText(const std::string& buttonText) {
        text = buttonText;
        layoutDirty = true;
        AutoResize();
        RequestRedraw();
    }

    void UltraCanvasButton::SetIcon(const std::string& path) {
        iconPath = path;
        layoutDirty = true;
        AutoResize();
        RequestRedraw();
    }

    void UltraCanvasButton::SetIconPosition(ButtonIconPosition position) {
        iconPosition = position;
        layoutDirty = true;
        RequestRedraw();
    }

    void UltraCanvasButton::SetIconSize(int width, int height) {
        iconWidth = width;
        iconHeight = height;
        layoutDirty = true;
        AutoResize();
        RequestRedraw();
    }

// ===== STYLING METHODS =====
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

    void UltraCanvasButton::SetBorder(float width, const Color& color) {
        style.borderWidth = width;
        style.borderColor = color;
        RequestRedraw();
    }

    void UltraCanvasButton::SetFont(const std::string& family, float size, FontWeight weight) {
        style.fontFamily = family;
        style.fontSize = size;
        style.fontWeight = weight;
        layoutDirty = true;
        AutoResize();
        RequestRedraw();
    }

    void UltraCanvasButton::SetTextAlign(TextAlignment align) {
        style.textAlign = align;
        RequestRedraw();
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

    void UltraCanvasButton::SetStyle(const ButtonStyle& newStyle) {
        style = newStyle;
        layoutDirty = true;
        AutoResize();
        RequestRedraw();
    }

// ===== STATE MANAGEMENT =====
    void UltraCanvasButton::SetEnabled(bool enabled) {
        if (enabled) {
            if (currentState == ButtonState::Disabled) {
                currentState = ButtonState::Normal;
            }
        } else {
            currentState = ButtonState::Disabled;
        }
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

        // Add split button secondary section dimensions
        if (style.splitStyle.enabled) {
            const SplitButtonStyle& split = style.splitStyle;
            int secWidth = 0, secHeight = 0;

            // Calculate secondary text dimensions
            if (!split.secondaryText.empty()) {
                int secTextWidth, secTextHeight;
                ctx->GetTextDimension(split.secondaryText, secTextWidth, secTextHeight);
                secWidth += secTextWidth;
                secHeight = std::max(secHeight, secTextHeight);
            }

            // Calculate secondary icon dimensions
            if (HasSecondaryIcon()) {
                secWidth += split.secondaryIconWidth;
                secHeight = std::max(secHeight, split.secondaryIconHeight);

                // Add spacing between icon and text if both exist
                if (!split.secondaryText.empty()) {
                    secWidth += split.secondaryIconSpacing;
                }
            }

            if (split.horizontal) {
                newWidth += secWidth + 10; // Add padding for secondary section
                if (split.showSeparator) {
                    newWidth += split.separatorWidth;
                }
            } else {
                newHeight += secHeight + 10;
                if (split.showSeparator) {
                    newHeight += split.separatorWidth;
                }
            }
        }

        // Add icon dimensions for primary section
        if (HasIcon()) {
            newWidth += iconWidth;
            newHeight = std::max(newHeight, iconHeight + style.paddingTop + style.paddingBottom);

            if (!text.empty() && (iconPosition == ButtonIconPosition::Left || iconPosition == ButtonIconPosition::Right)) {
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
        secondaryIconRect = Rect2Di(0, 0, 0, 0);
        primarySectionRect = Rect2Di(0, 0, 0, 0);
        secondarySectionRect = Rect2Di(0, 0, 0, 0);

        if (HasIcon() && !text.empty()) {
            // Both icon and text
            if (iconPosition == ButtonIconPosition::Left) {
                iconRect = Rect2Di(contentX, contentY + (contentHeight - iconHeight) / 2,
                                   iconWidth, iconHeight);
                textRect = Rect2Di(contentX + iconWidth + style.iconSpacing, contentY,
                                   contentWidth - iconWidth - style.iconSpacing, contentHeight);
            } else if (iconPosition == ButtonIconPosition::Right) {
                textRect = Rect2Di(contentX, contentY,
                                   contentWidth - iconWidth - style.iconSpacing, contentHeight);
                iconRect = Rect2Di(contentX + contentWidth - iconWidth,
                                   contentY + (contentHeight - iconHeight) / 2,
                                   iconWidth, iconHeight);
            } else if (iconPosition == ButtonIconPosition::Top) {
                iconRect = Rect2Di(contentX + (contentWidth - iconWidth) / 2, contentY,
                                   iconWidth, iconHeight);
                textRect = Rect2Di(contentX, contentY + iconHeight + style.iconSpacing,
                                   contentWidth, contentHeight - iconHeight - style.iconSpacing);
            } else if (iconPosition == ButtonIconPosition::Bottom) {
                textRect = Rect2Di(contentX, contentY,
                                   contentWidth, contentHeight - iconHeight - style.iconSpacing);
                iconRect = Rect2Di(contentX + (contentWidth - iconWidth) / 2,
                                   contentY + contentHeight - iconHeight,
                                   iconWidth, iconHeight);
            } else if (iconPosition == ButtonIconPosition::Center) {
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
                // Auto-size secondary section based on content
                int contentWidth = 0;
                auto ctx = GetRenderContext();

                if (!split.secondaryText.empty()) {
                    int textWidth, textHeight;
                    ctx->SetFontFace(style.fontFamily, style.fontWeight, FontSlant::Normal);
                    ctx->GetTextDimension(split.secondaryText, textWidth, textHeight);
                    contentWidth += textWidth;
                }

                if (HasSecondaryIcon()) {
                    contentWidth += split.secondaryIconWidth;
                    if (!split.secondaryText.empty()) {
                        contentWidth += split.secondaryIconSpacing;
                    }
                }

                secondaryWidth = contentWidth + style.paddingLeft + style.paddingRight;
                primaryWidth = bounds.width - secondaryWidth;
            } else {
                primaryWidth = bounds.width * split.primaryRatio;
                secondaryWidth = bounds.width - primaryWidth;
            }

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

            // Secondary section layout with icon support
            int secContentX = secondarySectionRect.x + style.paddingLeft;
            int secContentY = secondarySectionRect.y + style.paddingTop;
            int secContentWidth = secondarySectionRect.width - style.paddingLeft - style.paddingRight;
            int secContentHeight = secondarySectionRect.height - style.paddingTop - style.paddingBottom;

            if (HasSecondaryIcon() && !split.secondaryText.empty()) {
                // Both icon and text in secondary section
                if (split.secondaryIconPosition == ButtonSecondaryIconPosition::Left) {
                    secondaryIconRect = Rect2Di(secContentX,
                                                secContentY + (secContentHeight - split.secondaryIconHeight) / 2,
                                                split.secondaryIconWidth, split.secondaryIconHeight);
                    secondaryTextRect = Rect2Di(secContentX + split.secondaryIconWidth + split.secondaryIconSpacing,
                                                secContentY,
                                                secContentWidth - split.secondaryIconWidth - split.secondaryIconSpacing,
                                                secContentHeight);
                } else {
                    // Icon on right
                    secondaryTextRect = Rect2Di(secContentX, secContentY,
                                                secContentWidth - split.secondaryIconWidth - split.secondaryIconSpacing,
                                                secContentHeight);
                    secondaryIconRect = Rect2Di(secContentX + secContentWidth - split.secondaryIconWidth,
                                                secContentY + (secContentHeight - split.secondaryIconHeight) / 2,
                                                split.secondaryIconWidth, split.secondaryIconHeight);
                }
            } else if (HasSecondaryIcon()) {
                // Icon only in secondary section
                secondaryIconRect = Rect2Di(secContentX + (secContentWidth - split.secondaryIconWidth) / 2,
                                            secContentY + (secContentHeight - split.secondaryIconHeight) / 2,
                                            split.secondaryIconWidth, split.secondaryIconHeight);
                secondaryTextRect = Rect2Di(0, 0, 0, 0);
            } else {
                // Text only in secondary section
                secondaryTextRect = Rect2Di(secContentX, secContentY, secContentWidth, secContentHeight);
                secondaryIconRect = Rect2Di(0, 0, 0, 0);
            }

            // Icon positioning for primary section
            if (HasIcon()) {
                if (iconPosition == ButtonIconPosition::Left) {
                    iconRect = Rect2Di(textRect.x, textRect.y + (textRect.height - iconHeight) / 2,
                                       iconWidth, iconHeight);
                    textRect.x += iconWidth + style.iconSpacing;
                    textRect.width -= iconWidth + style.iconSpacing;
                } else if (iconPosition == ButtonIconPosition::Right) {
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

            // Secondary section layout for vertical split
            int secContentX = secondarySectionRect.x + style.paddingLeft;
            int secContentY = secondarySectionRect.y + 2;
            int secContentWidth = secondarySectionRect.width - style.paddingLeft - style.paddingRight;
            int secContentHeight = secondarySectionRect.height - 4;

            // For vertical split, always stack icon above text
            if (HasSecondaryIcon() && !split.secondaryText.empty()) {
                // Icon above text
                secondaryIconRect = Rect2Di(secContentX + (secContentWidth - split.secondaryIconWidth) / 2,
                                            secContentY, split.secondaryIconWidth, split.secondaryIconHeight);
                secondaryTextRect = Rect2Di(secContentX,
                                            secContentY + split.secondaryIconHeight + split.secondaryIconSpacing,
                                            secContentWidth,
                                            secContentHeight - split.secondaryIconHeight - split.secondaryIconSpacing);
            } else if (HasSecondaryIcon()) {
                secondaryIconRect = Rect2Di(secContentX + (secContentWidth - split.secondaryIconWidth) / 2,
                                            secContentY + (secContentHeight - split.secondaryIconHeight) / 2,
                                            split.secondaryIconWidth, split.secondaryIconHeight);
                secondaryTextRect = Rect2Di(0, 0, 0, 0);
            } else {
                secondaryTextRect = Rect2Di(secContentX, secContentY, secContentWidth, secContentHeight);
                secondaryIconRect = Rect2Di(0, 0, 0, 0);
            }
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

// ===== RENDERING HELPERS =====
    void UltraCanvasButton::UpdateButtonState() {
        // State management is handled in OnEvent
    }

    void UltraCanvasButton::GetCurrentColors(Color& bgColor, Color& textColor, Color& iconColor) const {
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

    void UltraCanvasButton::GetSplitColors(Color& primaryBg, Color& primaryText,
                                           Color& secondaryBg, Color& secondaryText)  {
        // Primary section uses normal button colors
        Color picon;
        GetCurrentColors(primaryBg, primaryText, picon);

        // Secondary section colors based on state
        const SplitButtonStyle& split = style.splitStyle;

        switch (currentState) {
            case ButtonState::Normal:
                secondaryBg = split.secondaryBackgroundColor;
                secondaryText = split.secondaryTextColor;
                break;
            case ButtonState::Hovered:
                secondaryBg = split.secondaryHoverColor;
                secondaryText = split.secondaryTextColor;
                break;
            case ButtonState::Pressed:
                secondaryBg = split.secondaryPressedColor;
                secondaryText = split.secondaryTextColor;
                break;
            case ButtonState::Disabled:
                secondaryBg = style.disabledColor;
                secondaryText = style.disabledTextColor;
                break;
        }
    }

    void UltraCanvasButton::GetSecondaryIconColor(Color& iconColor) const {
        const SplitButtonStyle& split = style.splitStyle;
        switch (currentState) {
            case ButtonState::Normal:
                iconColor = split.secondaryNormalIconColor;
                break;
            case ButtonState::Hovered:
                iconColor = split.secondaryHoverIconColor;
                break;
            case ButtonState::Pressed:
                iconColor = split.secondaryPressedIconColor;
                break;
            case ButtonState::Disabled:
                iconColor = split.secondaryDisabledIconColor;
                break;
        }
    }

    void UltraCanvasButton::DrawIcon(IRenderContext* ctx) {
        if (!HasIcon() || iconRect.width <= 0) return;

        Color bgColor, textColor, iconColor;
        GetCurrentColors(bgColor, textColor, iconColor);

        // Apply icon tinting if needed
        if (iconColor != Colors::White) {
            // TODO: Apply color tinting to icon
        }

        // Draw the icon
        ctx->DrawImage(iconPath, iconRect);
    }

    void UltraCanvasButton::DrawSecondaryIcon(IRenderContext* ctx) {
        if (!HasSecondaryIcon() || secondaryIconRect.width <= 0) return;

        Color iconColor;
        GetSecondaryIconColor(iconColor);

        // Apply icon tinting if needed
        if (iconColor != Colors::White) {
            // TODO: Apply color tinting to icon
        }

        // Draw the secondary icon
        ctx->DrawImage(style.splitStyle.secondaryIconPath, secondaryIconRect);
    }

    void UltraCanvasButton::DrawText(IRenderContext* ctx) {
        if (text.empty() || textRect.width <= 0) return;

        Color bgColor, textColor, iconColor;
        GetCurrentColors(bgColor, textColor, iconColor);

        ctx->SetTextPaint(textColor);
        ctx->SetFontFace(style.fontFamily, style.fontWeight, FontSlant::Normal);
        ctx->SetFontSize(style.fontSize);

        // Get text dimensions
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

        // Draw primary and secondary sections
        if (split.horizontal) {
            // Draw primary section
            Rect2Di primaryDraw = primarySectionRect;
            if (currentState == ButtonState::Pressed) {
                primaryDraw.x += 1;
                primaryDraw.y += 1;
            }
            ctx->DrawFilledRectangle(primaryDraw, primaryBg, 0, Colors::Transparent, 0);

            // Draw secondary section
            Rect2Di secondaryDraw = secondarySectionRect;
            if (currentState == ButtonState::Pressed) {
                secondaryDraw.x += 1;
                secondaryDraw.y += 1;
            }
            ctx->DrawFilledRectangle(secondaryDraw, secondaryBg, 0, Colors::Transparent, 0);
        } else {
            // Vertical split
            Rect2Di primaryDraw = primarySectionRect;
            if (currentState == ButtonState::Pressed) {
                primaryDraw.x += 1;
                primaryDraw.y += 1;
            }
            ctx->DrawFilledRectangle(primaryDraw, primaryBg, 0, Colors::Transparent,
                                     style.cornerRadius);

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

        // Draw primary section icon
        DrawIcon(ctx);

        // Draw primary text
        if (!text.empty()) {
            ctx->SetTextPaint(primaryText);
            ctx->SetFontFace(style.fontFamily, style.fontWeight, FontSlant::Normal);
            ctx->SetFontSize(style.fontSize);

            int textWidth, textHeight;
            ctx->GetTextDimension(text, textWidth, textHeight);

            Point2Df primaryTextPos;
            if (HasIcon() && (iconPosition == ButtonIconPosition::Left || iconPosition == ButtonIconPosition::Right)) {
                primaryTextPos.x = textRect.x;
                primaryTextPos.y = textRect.y + (textRect.height - textHeight) / 2;
            } else {
                primaryTextPos.x = textRect.x + (textRect.width - textWidth) / 2;
                primaryTextPos.y = textRect.y + (textRect.height - textHeight) / 2;
            }

            if (currentState == ButtonState::Pressed) {
                primaryTextPos.x += 1;
                primaryTextPos.y += 1;
            }

            ctx->DrawText(text, primaryTextPos);
        }

        // Draw secondary section icon
        DrawSecondaryIcon(ctx);

        // Draw secondary text
        if (!split.secondaryText.empty()) {
            ctx->SetTextPaint(secondaryText);
            ctx->SetFontFace(style.fontFamily, style.fontWeight, FontSlant::Normal);
            ctx->SetFontSize(style.fontSize * 0.9f);  // Slightly smaller font

            int textWidth, textHeight;
            ctx->GetTextDimension(split.secondaryText, textWidth, textHeight);

            Point2Df secondaryTextPos;

            // Position text based on whether there's an icon
            if (HasSecondaryIcon() && split.horizontal) {
                // Text position already calculated in layout
                secondaryTextPos.x = secondaryTextRect.x;
                secondaryTextPos.y = secondaryTextRect.y + (secondaryTextRect.height - textHeight) / 2;
            } else {
                // Center text if no icon
                secondaryTextPos.x = secondaryTextRect.x + (secondaryTextRect.width - textWidth) / 2;
                secondaryTextPos.y = secondaryTextRect.y + (secondaryTextRect.height - textHeight) / 2;
            }

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

// ===== MAIN RENDER METHOD =====
    void UltraCanvasButton::Render() {
        if (!IsVisible()) return;

        auto ctx = GetRenderContext();
        if (!ctx) return;

        // Update layout if needed
        if (layoutDirty) {
            CalculateLayout();
        }

        ctx->PushState();

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

            // Draw icon and text
            DrawIcon(ctx);
            DrawText(ctx);

            // Draw focus indicator if focused
            if (IsFocused()) {
                ctx->SetStrokePaint(style.focusedColor);
                ctx->DrawRoundedRectangle(bounds, style.cornerRadius);
            }
        }

        ctx->PopState();
    }

// ===== CLICK HELPER =====
    void UltraCanvasButton::Click(const UCEvent& event) {
        if (style.splitStyle.enabled) {
            // Determine which section was clicked
            if (IsPointInSecondarySection(event.x, event.y)) {
                if (onSecondaryClick) onSecondaryClick();
            } else {
                if (onClick) onClick();
            }
        } else {
            // Regular button click
            if (onClick) onClick();
        }
    }

// ===== EVENT HANDLING =====
    bool UltraCanvasButton::OnEvent(const UCEvent& event) {
        if (!IsEnabled() || !IsVisible()) return false;

        switch (event.type) {
            case UCEventType::MouseDown:
                if (Contains(event.x, event.y)) {
                    pressed = true;
                    currentState = ButtonState::Pressed;
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
                    if (wasInside) {
                        currentState = ButtonState::Hovered;
                        Click(event);
                    } else {
                        currentState = ButtonState::Normal;
                    }
                    if (onRelease) onRelease();
                    RequestRedraw();
                    return true;
                }
                break;

            case UCEventType::MouseMove:
                if (!pressed) {
                    bool inside = Contains(event.x, event.y);
                    ButtonState newState = inside ? ButtonState::Hovered : ButtonState::Normal;
                    if (newState != currentState) {
                        currentState = newState;
                        RequestRedraw();
                    }
                }
                return false;
                break;

            case UCEventType::MouseEnter:
                if (!pressed) {
                    currentState = ButtonState::Hovered;
                    SetHovered(true);
                    if (onHoverEnter) onHoverEnter();
                    RequestRedraw();
                }
                return true;

            case UCEventType::MouseLeave:
                if (!pressed) {
                    currentState = ButtonState::Normal;
                }
                SetHovered(false);
                if (onHoverLeave) onHoverLeave();
                RequestRedraw();
                return true;

            case UCEventType::KeyDown:
                if (IsFocused() && (event.virtualKey == UCKeys::Space || event.virtualKey == UCKeys::Return)) {
                    pressed = true;
                    currentState = ButtonState::Pressed;
                    if (onPress) onPress();
                    RequestRedraw();
                    return true;
                }
                break;

            case UCEventType::KeyUp:
                if (IsFocused() && pressed && (event.virtualKey == UCKeys::Space || event.virtualKey == UCKeys::Return)) {
                    pressed = false;
                    currentState = ButtonState::Normal;
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