// core/UltraCanvasButton.cpp
// Interactive button component implementation with secondary icon support
// Version: 2.5.1
// Last Modified: 2026-06-03
// Author: UltraCanvas Framework

#include "UltraCanvasButton.h"
#include "UltraCanvasImage.h"
#include "CSSLayout/LayoutUtils.h"
#include <algorithm>

namespace UltraCanvas {

// ===== CONSTRUCTOR =====
    UltraCanvasButton::UltraCanvasButton(const std::string& identifier,
                                         float x, float y, float w, float h,
                                         const std::string& buttonText)
            : UltraCanvasUIElement(identifier, x, y, w, h), text(buttonText) {
        SetMouseCursor(UCMouseCursor::Hand);
        SetPadding(4,8,4,8);
    }

// ===== SPLIT BUTTON METHODS =====
    void UltraCanvasButton::SetSplitEnabled(bool enabled) {
        style.splitStyle.enabled = enabled;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasButton::SetSplitRatio(float primaryRatio) {
        style.splitStyle.primaryRatio = primaryRatio;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasButton::SetSplitHorizontal(bool horizontal) {
        style.splitStyle.horizontal = horizontal;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasButton::SetSplitSecondaryText(const std::string& secondaryText) {
        style.splitStyle.secondaryText = secondaryText;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasButton::SetSplitSecondaryIcon(const std::string& iconPath,
                                                  ButtonSecondaryIconPosition position) {
        style.splitStyle.secondaryIcon = UCImage::Get(iconPath);
        style.splitStyle.secondaryIconPosition = position;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasButton::SetSplitSecondaryIconSize(int width, int height) {
        style.splitStyle.secondaryIconWidth = width;
        style.splitStyle.secondaryIconHeight = height;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasButton::SetSplitSecondaryIconSpacing(int spacing) {
        style.splitStyle.secondaryIconSpacing = spacing;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasButton::SetSplitSecondaryIconPosition(ButtonSecondaryIconPosition position) {
        style.splitStyle.secondaryIconPosition = position;
        InvalidateLayout();
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
        InvalidateLayout();
        RequestRedraw();
    }

// ===== TEXT & ICON METHODS =====
    void UltraCanvasButton::SetText(const std::string& buttonText) {
        text = buttonText;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasButton::SetIcon(const std::string& path) {
        icon = UCImage::Get(path);
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasButton::SetIconPosition(ButtonIconPosition position) {
        iconPosition = position;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasButton::SetIconSize(int width, int height) {
        iconWidth = width;
        iconHeight = height;
        InvalidateLayout();
        RequestRedraw();
    }
    
    void UltraCanvasButton::SetUseIconAsMask(bool useAsMask) {
        style.useIconAsMask = useAsMask;
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

    void UltraCanvasButton::SetColors(const Color& normal, const Color& hover) {
        style.normalColor = normal;
        style.hoverColor = hover;
        style.pressedColor = normal.Darken(0.1);
        style.disabledColor = normal.Lighten(0.3);
        RequestRedraw();
    }

    void UltraCanvasButton::SetColors(const Color& normal) {
        style.normalColor = normal;
        style.hoverColor = normal.Darken(0.1);
        style.pressedColor = normal.Darken(0.2);
        style.disabledColor = normal.Lighten(0.3);
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

    void UltraCanvasButton::SetTextColors(const Color& normal, const Color& hover) {
        style.normalTextColor = normal;
        style.hoverTextColor = hover;
        style.pressedTextColor = normal.Darken(0.1);
        style.disabledTextColor = normal.Lighten(0.3);
        RequestRedraw();
    }

    void UltraCanvasButton::SetTextColors(const Color& normal) {
        style.normalTextColor = normal;
        style.hoverTextColor = normal.Darken(0.1);
        style.pressedTextColor = normal.Darken(0.2);
        style.disabledTextColor = normal.Lighten(0.3);
        RequestRedraw();
    }

    void UltraCanvasButton::SetBorder(float width, const Color& color) {
        style.borderWidth = width;
        style.borderColor = color;
        SetBorders(static_cast<int>(width), color, style.cornerRadius);
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasButton::SetFont(const std::string& family, float size, FontWeight weight) {
        style.fontFamily = family;
        style.fontSize = size;
        style.fontWeight = weight;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasButton::SetFontSize(float size) {
        style.fontSize = size;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasButton::SetTextAlign(TextAlignment align) {
        style.textAlign = align;
        RequestRedraw();
    }

    void UltraCanvasButton::SetIconSpacing(int spacing) {
        style.iconSpacing = spacing;
        InvalidateLayout();
        RequestRedraw();
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
        InvalidateLayout();
        RequestRedraw();
    }

// ===== LAYOUT CALCULATION =====
    Size2Df UltraCanvasButton::MeasureContentSize(IRenderContext* rc) const {
        // Returns the CONTENT-box size (no padding/border). Primary section =
        // text + icon; for a split button, add the secondary section's extent
        // plus the separator on the split axis.
        float contentW = 0.f, contentH = 0.f;

        rc->SetFontFace(style.fontFamily, style.fontWeight, FontSlant::Normal);
        rc->SetFontSize(style.fontSize);

        if (!text.empty()) {
            Size2Di textSize = rc->GetTextLineDimensions(text);
            contentW += textSize.width;
            contentH = std::max(contentH, (float)textSize.height);
        }

        // Primary icon
        if (HasIcon()) {
            contentW += iconWidth;
            contentH = std::max(contentH, (float)iconHeight);
            if (!text.empty() &&
                (iconPosition == ButtonIconPosition::Left || iconPosition == ButtonIconPosition::Right)) {
                contentW += style.iconSpacing;
            }
        }

        // Split secondary section
        if (style.splitStyle.enabled) {
            const SplitButtonStyle& split = style.splitStyle;
            float secWidth = 0.f, secHeight = 0.f;

            if (!split.secondaryText.empty()) {
                Size2Di secTextSize = rc->GetTextLineDimensions(split.secondaryText);
                secWidth += secTextSize.width;
                secHeight = std::max(secHeight, (float)secTextSize.height);
            }
            if (HasSecondaryIcon()) {
                secWidth += split.secondaryIconWidth;
                secHeight = std::max(secHeight, (float)split.secondaryIconHeight);
                if (!split.secondaryText.empty()) {
                    secWidth += split.secondaryIconSpacing;
                }
            }

            // Reserve the secondary section's own horizontal padding (the section
            // is a self-contained box inside the button).
            if (split.horizontal) {
                contentW += secWidth + GetPaddingLeft() + GetPaddingRight();
                if (split.showSeparator) contentW += split.separatorWidth;
                contentH = std::max(contentH, secHeight);
            } else {
                contentH += secHeight + GetPaddingTop() + GetPaddingBottom();
                if (split.showSeparator) contentH += split.separatorWidth;
                contentW = std::max(contentW, secWidth);
            }
        }

        return Size2Df(contentW, contentH);
    }

    Size2Df UltraCanvasButton::MeasureOwnContent(std::optional<float> /*definiteContentWidth*/,
                                                 const CSSLayout::LayoutContext& /*ctx*/) {
        IRenderContext* rc = GetRenderContext();
        if (!rc) {
            // No surface to measure text against yet — report no own content;
            // the block path resolves from size.width/height or the constraints.
            return Size2Df(0.f, 0.f);
        }
        // A button doesn't wrap, so the content size is width-independent.
        rc->PushState();
        Size2Df content = MeasureContentSize(rc);
        rc->PopState();
        return content;
    }

    void UltraCanvasButton::ComputeIntrinsicSizes(const CSSLayout::LayoutContext& /*ctx*/) {
        IRenderContext* rc = GetRenderContext();
        if (!rc) {
            intrinsic.minContentWidth = intrinsic.maxContentWidth = 0;
            intrinsic.minContentHeight = intrinsic.maxContentHeight = 0;
            return;
        }

        const float padH = GetTotalPaddingHorizontal() + GetTotalBorderHorizontal();
        const float padV = GetTotalPaddingVertical()   + GetTotalBorderVertical();

        rc->PushState();
        Size2Df content = MeasureContentSize(rc);
        rc->PopState();

        // A button doesn't wrap, so min-content == max-content. Publish in
        // border-box units so Flex/Grid can use them directly.
        intrinsic.valid = true;
        intrinsic.maxContentWidth  = content.width  + padH;
        intrinsic.minContentWidth  = content.width  + padH;
        intrinsic.maxContentHeight = content.height + padV;
        intrinsic.minContentHeight = content.height + padV;
    }

    void UltraCanvasButton::CalculateLayout() {
        if (style.splitStyle.enabled) {
            CalculateSplitLayout();
            return;
        }

        // Regular button layout calculation
        Rect2Df bounds = GetLocalBounds();
        float contentX = GetPaddingLeft();
        float contentY = GetPaddingTop();
        float contentWidth = finalBounds.width - GetPaddingLeft() - GetPaddingRight();
        float contentHeight = finalBounds.height - GetPaddingTop() - GetPaddingBottom();

        // Reset rectangles
        iconRect = Rect2Df(0, 0, 0, 0);
        textRect = Rect2Df(0, 0, 0, 0);
        secondaryTextRect = Rect2Df(0, 0, 0, 0);
        secondaryIconRect = Rect2Df(0, 0, 0, 0);
        primarySectionRect = Rect2Df(0, 0, 0, 0);
        secondarySectionRect = Rect2Df(0, 0, 0, 0);

        if (HasIcon() && !text.empty()) {
            // Both icon and text
            if (iconPosition == ButtonIconPosition::Left) {
                iconRect = Rect2Df(contentX, contentY + (contentHeight - iconHeight) / 2,
                                   iconWidth, iconHeight);
                textRect = Rect2Df(contentX + iconWidth + style.iconSpacing, contentY,
                                   contentWidth - iconWidth - style.iconSpacing, contentHeight);
            } else if (iconPosition == ButtonIconPosition::Right) {
                textRect = Rect2Df(contentX, contentY,
                                   contentWidth - iconWidth - style.iconSpacing, contentHeight);
                iconRect = Rect2Df(contentX + contentWidth - iconWidth,
                                   contentY + (contentHeight - iconHeight) / 2,
                                   iconWidth, iconHeight);
            } else if (iconPosition == ButtonIconPosition::Top) {
                iconRect = Rect2Df(contentX + (contentWidth - iconWidth) / 2, contentY,
                                   iconWidth, iconHeight);
                textRect = Rect2Df(contentX, contentY + iconHeight + style.iconSpacing,
                                   contentWidth, contentHeight - iconHeight - style.iconSpacing);
            } else if (iconPosition == ButtonIconPosition::Bottom) {
                textRect = Rect2Df(contentX, contentY,
                                   contentWidth, contentHeight - iconHeight - style.iconSpacing);
                iconRect = Rect2Df(contentX + (contentWidth - iconWidth) / 2,
                                   contentY + contentHeight - iconHeight,
                                   iconWidth, iconHeight);
            } else if (iconPosition == ButtonIconPosition::Center) {
                iconRect = Rect2Df(contentX + (contentWidth - iconWidth) / 2,
                                   contentY + (contentHeight - iconHeight) / 2,
                                   iconWidth, iconHeight);
            }
        } else if (HasIcon()) {
            // Icon only
            iconRect = Rect2Df(contentX + (contentWidth - iconWidth) / 2,
                               contentY + (contentHeight - iconHeight) / 2,
                               iconWidth, iconHeight);
        } else if (!text.empty()) {
            // Text only
            textRect = Rect2Df(contentX, contentY, contentWidth, contentHeight);
        }
    }

    void UltraCanvasButton::CalculateSplitLayout() {
        Rect2Df bounds = GetLocalBounds();
        const SplitButtonStyle& split = style.splitStyle;

        if (split.horizontal) {
            // Horizontal split layout
            int primaryWidth, secondaryWidth;

            if (split.primaryRatio == 0) {
                // Auto-size secondary section based on content
                int contentWidth = 0;
                auto ctx = GetRenderContext();

                if (ctx && !split.secondaryText.empty()) {
                    // Measure inside a saved state: SetFontFace mutates the shared
                    // context's persistent font state, and this runs in the
                    // (unbracketed) layout pass — without Push/PopState the weight
                    // would leak to later-rendered elements (e.g. the nav TreeView).
                    ctx->PushState();
                    ctx->SetFontFace(style.fontFamily, style.fontWeight, FontSlant::Normal);
                    Size2Di textSize = ctx->GetTextLineDimensions(split.secondaryText);
                    ctx->PopState();
                    contentWidth += textSize.width;
                }

                if (HasSecondaryIcon()) {
                    contentWidth += split.secondaryIconWidth;
                    if (!split.secondaryText.empty()) {
                        contentWidth += split.secondaryIconSpacing;
                    }
                }

                secondaryWidth = contentWidth + GetPaddingLeft() + GetPaddingRight();
                primaryWidth = finalBounds.width - secondaryWidth;
            } else {
                primaryWidth = finalBounds.width * split.primaryRatio;
                secondaryWidth = finalBounds.width - primaryWidth;
            }

            if (split.showSeparator) {
                primaryWidth -= split.separatorWidth / 2;
                secondaryWidth -= split.separatorWidth / 2;
            }

            // Section rects are in ELEMENT-LOCAL coordinates (origin 0,0): the
            // render context is translated to the element origin, and hit-testing
            // (Contains/event.pointer) is element-local too.
            primarySectionRect = Rect2Df(0, 0, primaryWidth, finalBounds.height);
            secondarySectionRect = Rect2Df(primaryWidth +
                                           (split.showSeparator ? split.separatorWidth : 0),
                                           0, secondaryWidth, finalBounds.height);

            // Text rectangles within sections
            textRect = Rect2Df(primarySectionRect.x + GetPaddingLeft(),
                               primarySectionRect.y + GetPaddingTop(),
                               primarySectionRect.width - GetPaddingLeft() - GetPaddingRight(),
                               primarySectionRect.height - GetPaddingTop() - GetPaddingBottom());

            // Secondary section layout with icon support
            int secContentX = secondarySectionRect.x + GetPaddingLeft();
            int secContentY = secondarySectionRect.y + GetPaddingTop();
            int secContentWidth = secondarySectionRect.width - GetPaddingLeft() - GetPaddingRight();
            int secContentHeight = secondarySectionRect.height - GetPaddingTop() - GetPaddingBottom();

            if (HasSecondaryIcon() && !split.secondaryText.empty()) {
                // Both icon and text in secondary section
                if (split.secondaryIconPosition == ButtonSecondaryIconPosition::Left) {
                    secondaryIconRect = Rect2Df(secContentX,
                                                secContentY + (secContentHeight - split.secondaryIconHeight) / 2,
                                                split.secondaryIconWidth, split.secondaryIconHeight);
                    secondaryTextRect = Rect2Df(secContentX + split.secondaryIconWidth + split.secondaryIconSpacing,
                                                secContentY,
                                                secContentWidth - split.secondaryIconWidth - split.secondaryIconSpacing,
                                                secContentHeight);
                } else {
                    // Icon on right
                    secondaryTextRect = Rect2Df(secContentX, secContentY,
                                                secContentWidth - split.secondaryIconWidth - split.secondaryIconSpacing,
                                                secContentHeight);
                    secondaryIconRect = Rect2Df(secContentX + secContentWidth - split.secondaryIconWidth,
                                                secContentY + (secContentHeight - split.secondaryIconHeight) / 2,
                                                split.secondaryIconWidth, split.secondaryIconHeight);
                }
            } else if (HasSecondaryIcon()) {
                // Icon only in secondary section
                secondaryIconRect = Rect2Df(secContentX + (secContentWidth - split.secondaryIconWidth) / 2,
                                            secContentY + (secContentHeight - split.secondaryIconHeight) / 2,
                                            split.secondaryIconWidth, split.secondaryIconHeight);
                secondaryTextRect = Rect2Df(0, 0, 0, 0);
            } else {
                // Text only in secondary section
                secondaryTextRect = Rect2Df(secContentX, secContentY, secContentWidth, secContentHeight);
                secondaryIconRect = Rect2Df(0, 0, 0, 0);
            }

            // Icon positioning for primary section
            if (HasIcon()) {
                if (iconPosition == ButtonIconPosition::Left) {
                    iconRect = Rect2Df(textRect.x, textRect.y + (textRect.height - iconHeight) / 2,
                                       iconWidth, iconHeight);
                    textRect.x += iconWidth + style.iconSpacing;
                    textRect.width -= iconWidth + style.iconSpacing;
                } else if (iconPosition == ButtonIconPosition::Right) {
                    iconRect = Rect2Df(textRect.x + textRect.width - iconWidth,
                                       textRect.y + (textRect.height - iconHeight) / 2,
                                       iconWidth, iconHeight);
                    textRect.width -= iconWidth + style.iconSpacing;
                }
            }
        } else {
            // Vertical split layout
            int primaryHeight = finalBounds.height * split.primaryRatio;
            int secondaryHeight = finalBounds.height - primaryHeight;

            if (split.showSeparator) {
                primaryHeight -= split.separatorWidth / 2;
                secondaryHeight -= split.separatorWidth / 2;
            }

            // Element-local coordinates (origin 0,0); see horizontal branch.
            primarySectionRect = Rect2Df(0, 0, finalBounds.width, primaryHeight);
            secondarySectionRect = Rect2Df(0, primaryHeight +
                                                     (split.showSeparator ? split.separatorWidth : 0),
                                           finalBounds.width, secondaryHeight);

            // Text rectangles within sections
            textRect = Rect2Df(primarySectionRect.x + GetPaddingLeft(),
                               primarySectionRect.y + GetPaddingTop(),
                               primarySectionRect.width - GetPaddingLeft() - GetPaddingRight(),
                               primarySectionRect.height - GetPaddingTop() - GetPaddingBottom());

            // Secondary section layout for vertical split
            int secContentX = secondarySectionRect.x + GetPaddingLeft();
            int secContentY = secondarySectionRect.y + 2;
            int secContentWidth = secondarySectionRect.width - GetPaddingLeft() - GetPaddingRight();
            int secContentHeight = secondarySectionRect.height - 4;

            // For vertical split, always stack icon above text
            if (HasSecondaryIcon() && !split.secondaryText.empty()) {
                // Icon above text
                secondaryIconRect = Rect2Df(secContentX + (secContentWidth - split.secondaryIconWidth) / 2,
                                            secContentY, split.secondaryIconWidth, split.secondaryIconHeight);
                secondaryTextRect = Rect2Df(secContentX,
                                            secContentY + split.secondaryIconHeight + split.secondaryIconSpacing,
                                            secContentWidth,
                                            secContentHeight - split.secondaryIconHeight - split.secondaryIconSpacing);
            } else if (HasSecondaryIcon()) {
                secondaryIconRect = Rect2Df(secContentX + (secContentWidth - split.secondaryIconWidth) / 2,
                                            secContentY + (secContentHeight - split.secondaryIconHeight) / 2,
                                            split.secondaryIconWidth, split.secondaryIconHeight);
                secondaryTextRect = Rect2Df(0, 0, 0, 0);
            } else {
                secondaryTextRect = Rect2Df(secContentX, secContentY, secContentWidth, secContentHeight);
                secondaryIconRect = Rect2Df(0, 0, 0, 0);
            }
        }

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
    void UltraCanvasButton::GetCurrentColors(Color& bgColor, Color& textColor) const {
        switch (GetPrimaryState()) {
            case ElementState::Hovered:
                bgColor = style.hoverColor;
                textColor = style.hoverTextColor;
                break;
            case ElementState::Pressed:
                bgColor = style.pressedColor;
                textColor = style.pressedTextColor;
                break;
            case ElementState::Disabled:
                bgColor = style.disabledColor;
                textColor = style.disabledTextColor;
                break;
            default:
                bgColor = style.normalColor;
                textColor = style.normalTextColor;
                break;
        }
    }

    void UltraCanvasButton::GetSplitColors(Color& primaryBg, Color& primaryText,
                                           Color& secondaryBg, Color& secondaryText)  {
        // Primary section uses normal button colors
        GetCurrentColors(primaryBg, primaryText);

        // Secondary section colors based on state
        const SplitButtonStyle& split = style.splitStyle;

        switch (GetPrimaryState()) {
            case ElementState::Hovered:
                secondaryBg = split.secondaryHoverColor;
                secondaryText = split.secondaryTextColor;
                break;
            case ElementState::Pressed:
                secondaryBg = split.secondaryPressedColor;
                secondaryText = split.secondaryTextColor;
                break;
            case ElementState::Disabled:
                secondaryBg = style.disabledColor;
                secondaryText = style.disabledTextColor;
                break;
            default:
                secondaryBg = split.secondaryBackgroundColor;
                secondaryText = split.secondaryTextColor;
                break;
        }
    }

    void UltraCanvasButton::GetSecondaryIconColor(Color& iconColor) const {
        const SplitButtonStyle& split = style.splitStyle;
        switch (GetPrimaryState()) {
            case ElementState::Hovered:
                iconColor = split.secondaryHoverIconColor;
                break;
            case ElementState::Pressed:
                iconColor = split.secondaryPressedIconColor;
                break;
            case ElementState::Disabled:
                iconColor = split.secondaryDisabledIconColor;
                break;
            default:
                iconColor = split.secondaryNormalIconColor;
                break;
        }
    }

    void UltraCanvasButton::DrawIcon(IRenderContext* ctx) {
        if (!HasIcon() || iconRect.width <= 0) return;

        Color bgColor, textColor;
        GetCurrentColors(bgColor, textColor);

        // Dim icon when disabled
        if (GetPrimaryState() == ElementState::Disabled) {
            ctx->PushState();
            ctx->SetAlpha(0.35f);
            if (style.useIconAsMask) {
                ctx->DrawMask(textColor, *icon.get(), iconRect, ImageFitMode::Contain);
            } else {
                ctx->DrawImage(*icon.get(), iconRect, ImageFitMode::Contain);
            }
            ctx->PopState();
        } else {
            if (style.useIconAsMask) {
                ctx->DrawMask(textColor, *icon.get(), iconRect, ImageFitMode::Contain);
            } else {
                ctx->DrawImage(*icon.get(), iconRect, ImageFitMode::Contain);
            }
        }
    }

    void UltraCanvasButton::DrawSecondaryIcon(IRenderContext* ctx) {
        if (!HasSecondaryIcon() || secondaryIconRect.width <= 0) return;

        Color iconColor;
        GetSecondaryIconColor(iconColor);

        // Apply icon tinting if needed
        if (iconColor != Colors::White) {
            // TODO: Apply color tinting to icon
        }

        // Dim icon when disabled
        if (GetPrimaryState() == ElementState::Disabled) {
            ctx->PushState();
            ctx->SetAlpha(0.35f);
            if (style.useIconAsMask) {
                ctx->DrawMask(iconColor, *style.splitStyle.secondaryIcon.get(), secondaryIconRect, ImageFitMode::Contain);
            } else {
                ctx->DrawImage(*style.splitStyle.secondaryIcon.get(), secondaryIconRect, ImageFitMode::Contain);
            }
            ctx->PopState();
        } else {
            if (style.useIconAsMask) {
                ctx->DrawMask(iconColor, *style.splitStyle.secondaryIcon.get(), secondaryIconRect, ImageFitMode::Contain);
            } else {
                ctx->DrawImage(*style.splitStyle.secondaryIcon.get(), secondaryIconRect, ImageFitMode::Contain);
            }
        }
    }

    void UltraCanvasButton::DrawText(IRenderContext* ctx) {
        if (text.empty() || textRect.width <= 0) return;

        Color bgColor, textColor;
        GetCurrentColors(bgColor, textColor);

        ctx->SetTextPaint(textColor);
        ctx->SetFontFace(style.fontFamily, style.fontWeight, FontSlant::Normal);
        ctx->SetFontSize(style.fontSize);
        ctx->SetTextIsMarkup(true);
        ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
        ctx->SetTextAlignment(TextAlignment::Center);

        //debugOutput << "btn draw text=" << text << " rect=" << textRect.x << "," << textRect.y << " " << textRect.width << "x" << textRect.height << std::endl;
        if (GetPrimaryState() == ElementState::Pressed) {
            ctx->DrawTextInRect(text, Rect2Dd(textRect.x+1, textRect.y+1, textRect.width, textRect.height));
        } else {
            ctx->DrawTextInRect(text, textRect);
        }
    }

    void UltraCanvasButton::DrawSplitButton(IRenderContext* ctx) {
        const SplitButtonStyle& split = style.splitStyle;
        // Local draw box, inset by the shadow offset so the button + its shadow
        // fit within finalBounds. Never mutate finalBounds here (the engine owns it).
        Rect2Df bounds = GetLocalBounds();
        bounds.width  -= style.shadowOffset.x;
        bounds.height -= style.shadowOffset.y;

        // Get colors for both sections
        Color primaryBg, primaryText, secondaryBg, secondaryText;
        GetSplitColors(primaryBg, primaryText, secondaryBg, secondaryText);

        // Draw shadow if enabled and not pressed
        if (style.hasShadow && GetPrimaryState() != ElementState::Pressed) {
            Rect2Df shadowBounds = bounds;
            shadowBounds.x += style.shadowOffset.x;
            shadowBounds.y += style.shadowOffset.y;

            ctx->DrawFilledRectangle(shadowBounds, style.shadowColor, 0,
                                     Colors::Transparent, style.cornerRadius);
        }

        Rect2Df drawBounds = bounds;

        // Draw primary and secondary sections
        if (split.horizontal) {
            // Draw primary section
            Rect2Df primaryDraw = primarySectionRect;
            ctx->DrawFilledRectangle(primaryDraw, primaryBg, 0, Colors::Transparent, 0);

            // Draw secondary section
            Rect2Df secondaryDraw = secondarySectionRect;
            ctx->DrawFilledRectangle(secondaryDraw, secondaryBg, 0, Colors::Transparent, 0);
        } else {
            // Vertical split
            Rect2Df primaryDraw = primarySectionRect;
            if (GetPrimaryState() == ElementState::Pressed) {
                primaryDraw.x += 1;
                primaryDraw.y += 1;
            }
            ctx->DrawFilledRectangle(primaryDraw, primaryBg, 0, Colors::Transparent,
                                     style.cornerRadius);

            Rect2Df secondaryDraw = secondarySectionRect;
            ctx->DrawFilledRectangle(secondaryDraw, secondaryBg, 0, Colors::Transparent,
                                     style.cornerRadius);
        }

        // Draw separator if enabled
        if (split.showSeparator) {
            ctx->SetStrokePaint(split.separatorColor);
            ctx->SetStrokeWidth(split.separatorWidth);

            if (split.horizontal) {
                int separatorX = primarySectionRect.x + primarySectionRect.width;
                if (GetPrimaryState() == ElementState::Pressed) separatorX += 1;
                ctx->DrawLine(Point2Dd(separatorX, drawBounds.y),
                              Point2Dd(separatorX, drawBounds.y + drawBounds.height));
            } else {
                int separatorY = primarySectionRect.y + primarySectionRect.height;
                if (GetPrimaryState() == ElementState::Pressed) separatorY += 1;
                ctx->DrawLine(Point2Dd(drawBounds.x, separatorY),
                              Point2Dd(drawBounds.x + drawBounds.width, separatorY));
            }
        }

        // Draw primary section icon
        DrawIcon(ctx);

        // Draw primary text
        if (!text.empty()) {
            ctx->SetTextPaint(primaryText);
            ctx->SetFontFace(style.fontFamily, style.fontWeight, FontSlant::Normal);
            ctx->SetFontSize(style.fontSize);

            Size2Di textSize = ctx->GetTextLineDimensions(text);

            Point2Dd primaryTextPos;
            if (HasIcon() && (iconPosition == ButtonIconPosition::Left || iconPosition == ButtonIconPosition::Right)) {
                primaryTextPos.x = textRect.x;
                primaryTextPos.y = textRect.y + (textRect.height - textSize.height) / 2;
            } else {
                primaryTextPos.x = textRect.x + (textRect.width - textSize.width) / 2;
                primaryTextPos.y = textRect.y + (textRect.height - textSize.height) / 2;
            }

            if (GetPrimaryState() == ElementState::Pressed) {
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

            Size2Di textSize = ctx->GetTextLineDimensions(split.secondaryText);

            Point2Dd secondaryTextPos;

            // Position text based on whether there's an icon
            if (HasSecondaryIcon() && split.horizontal) {
                // Text position already calculated in layout
                secondaryTextPos.x = secondaryTextRect.x;
                secondaryTextPos.y = secondaryTextRect.y + (secondaryTextRect.height - textSize.height) / 2;
            } else {
                // Center text if no icon
                secondaryTextPos.x = secondaryTextRect.x + (secondaryTextRect.width - textSize.width) / 2;
                secondaryTextPos.y = secondaryTextRect.y + (secondaryTextRect.height - textSize.height) / 2;
            }

            if (GetPrimaryState() == ElementState::Pressed) {
                secondaryTextPos.x += 1;
                secondaryTextPos.y += 1;
            }

            ctx->DrawText(split.secondaryText, secondaryTextPos);
        }

        // Draw border around entire button
        if (style.borderWidth > 0) {
            ctx->SetStrokePaint(IsFocused() ? style.focusedColor : style.borderColor);
            ctx->SetStrokeWidth(IsFocused() ? style.borderWidth + 1 : style.borderWidth);
            ctx->DrawRoundedRectangle(drawBounds, style.cornerRadius);
        }

    }

    void UltraCanvasButton::Arrange(const Rect2Df& finalRect, const CSSLayout::LayoutContext& ctx) {
        // The engine sizes/places us (finalBounds) from the measure pass; we just
        // lay out the internal sub-rects (text/icon/split sections) in local
        // coordinates against the final size.
        UltraCanvasUIElement::Arrange(finalRect, ctx);
        CalculateLayout();
    }

// ===== MAIN RENDER METHOD =====
    void UltraCanvasButton::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        if (style.splitStyle.enabled) {
            DrawSplitButton(ctx);
        } else {
            // Regular button rendering
            Color bgColor, textColor;
            GetCurrentColors(bgColor, textColor);

            // Local draw box, inset by the shadow offset. Do not mutate finalBounds.
            Rect2Df bounds = GetLocalBounds();
            bounds.width  -= style.shadowOffset.x;
            bounds.height -= style.shadowOffset.y;

            // Draw shadow if enabled and not pressed
            if (style.hasShadow && GetPrimaryState() != ElementState::Pressed) {
                Rect2Df shadowBounds = bounds;
                shadowBounds.x += style.shadowOffset.x;
                shadowBounds.y += style.shadowOffset.y;

                ctx->DrawFilledRectangle(shadowBounds, style.shadowColor, 0,
                                         Colors::Transparent, style.cornerRadius);
            }

            // Draw button background
            ctx->DrawFilledRectangle(bounds, bgColor, IsFocused() ? style.borderWidth + 1 : style.borderWidth,
                                     (IsFocused() ? style.focusedColor : style.borderColor), style.cornerRadius);

            // Optional background gradient overlay (e.g. "unsaved" indicator)
            if (!style.backgroundGradient.empty()) {
                ctx->PushState();
                auto pattern = ctx->CreateLinearGradientPattern(
                        bounds.x, bounds.y, bounds.x, bounds.y + bounds.height,
                        style.backgroundGradient);
                if (style.cornerRadius > 0) {
                    ctx->RoundedRect(bounds.x, bounds.y, bounds.width, bounds.height, style.cornerRadius);
                } else {
                    ctx->Rect(bounds.x, bounds.y, bounds.width, bounds.height);
                }
                ctx->SetFillPaint(pattern);
                ctx->FillPathPreserve();
                ctx->ClearPath();
                ctx->PopState();
            }

            // Draw icon and text
            DrawIcon(ctx);
            DrawText(ctx);
        }
    }

// ===== CLICK HELPER =====
    void UltraCanvasButton::Click(const UCEvent& event) {
        if (style.splitStyle.enabled) {
            // Determine which section was clicked
            if (IsPointInSecondarySection(event.pointer.x, event.pointer.y)) {
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
        if (IsDisabled() || !IsVisible()) return false;

        switch (event.type) {
            case UCEventType::MouseDown:
            // A rapid second click arrives as a double-click instead of a
            // MouseDown; without treating it as a press, the MouseUp that
            // follows would not fire the click.
            case UCEventType::MouseDoubleClick:
                if (Contains(event.pointer)) {
                    if (IsPressed() && canToggled) {
                        SetPressed(false);
                    } else {
                        SetPressed(true);
                        SetFocus();
                        if (onPress) onPress();
                    }
                    if (canToggled && onToggle) {
                        onToggle(IsPressed());
                    }
                    RequestRedraw();
                    return true;
                }
                break;

            case UCEventType::MouseUp:
                if (IsPressed() && !canToggled) {
                    bool wasInside = Contains(event.pointer);
                    SetPressed(false);
                    if (onRelease) onRelease();
                    if (wasInside) {
                        Click(event);
                    }
                    RequestRedraw();
                    return true;
                }
                break;

            case UCEventType::MouseMove:
                if (!IsPressed()) {
                    bool inside = Contains(event.pointer);
                    SetHovered(inside);
                }
                return false;
                break;

            case UCEventType::MouseEnter:
                if (!IsPressed()) {
                    SetHovered(true);
                    if (onHoverEnter) onHoverEnter();
                }
                return true;

            case UCEventType::MouseLeave:
                SetHovered(false);
                if (onHoverLeave) onHoverLeave();
                return true;

            case UCEventType::KeyDown:
                if (IsFocused() && (event.virtualKey == UCKeys::Space || event.virtualKey == UCKeys::Return)) {
                    SetPressed(true);
                    if (onPress) onPress();
                    return true;
                }
                break;

            case UCEventType::KeyUp:
                if (IsFocused() && IsPressed() && (event.virtualKey == UCKeys::Space || event.virtualKey == UCKeys::Return)) {
                    SetPressed(false);
                    if (onRelease) onRelease();
                    Click(event);
                    return true;
                }
                break;
        }

        return false;
    }

} // namespace UltraCanvas