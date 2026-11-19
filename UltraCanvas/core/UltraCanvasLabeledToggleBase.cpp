// UltraCanvasLabeledToggleBase.cpp
// Shared layout/event/label/focus plumbing for checkbox, radio, and switch.
// Version: 1.4.0 - SetLabelWrap: the label wraps at the toggle's width, the indicator level with
//                 its first line; min-content is the longest word
// Version: 1.3.1 - the label is centred on its capitals, level with the box or dot
// Version: 1.3.0 - a press focuses the toggle; Space activates it and Enter is left
//                 to the window (a dialog's default button)
// Version: 1.2.0 - accessible name, toggle state and action
// Last Modified: 2026-10-24
// Author: UltraCanvas Framework

#include "UltraCanvasLabeledToggleBase.h"
#include "UltraCanvasApplication.h"
#include "CSSLayout/LayoutUtils.h"
#include <algorithm>
#include <optional>

namespace UltraCanvas {

    UltraCanvasLabeledToggleBase::UltraCanvasLabeledToggleBase(
            const std::string& identifier,
            float x, float y, float w, float h,
            const std::string& labelText)
            : UltraCanvasUIElement(identifier, x, y, w, h), text(labelText) {}

    void UltraCanvasLabeledToggleBase::SetText(const std::string& labelText) {
        const bool renamed = text != labelText;
        text = labelText;
        layoutDirty = true;
        InvalidateLayout();
        RequestRedraw();
        if (renamed && GetAccessibleNameOverride().empty()) NotifyAccessibility(AccessibilityEventType::NameChanged);
    }

    void UltraCanvasLabeledToggleBase::SetLabelWrap(bool wrap) {
        if (labelWrap == wrap) return;
        labelWrap = wrap;
        layoutDirty = true;
        InvalidateLayout();
        RequestRedraw();
    }

    std::string UltraCanvasLabeledToggleBase::GetAccessibleName() const {
        return GetAccessibleNameOverride().empty() ? text : GetAccessibleNameOverride();
    }

    AccessibleToggleState UltraCanvasLabeledToggleBase::GetAccessibleToggleState() const {
        switch (checkState) {
            case CheckedState::Checked:       return AccessibleToggleState::On;
            case CheckedState::Indeterminate: return AccessibleToggleState::Mixed;
            default:                          return AccessibleToggleState::Off;
        }
    }

    bool UltraCanvasLabeledToggleBase::DoAccessibleAction() {
        if (IsDisabled() || !IsVisible()) return false;
        OnActivate();
        return true;
    }

    void UltraCanvasLabeledToggleBase::SetChecked(bool checked) {
        SetCheckState(checked ? CheckedState::Checked : CheckedState::Unchecked);
    }

    void UltraCanvasLabeledToggleBase::SetCheckState(CheckedState state) {
        if (checkState == state) return;

        CheckedState oldState = checkState;
        checkState = state;

        if (onStateChanged) onStateChanged(oldState, state);
        switch (state) {
            case CheckedState::Checked:       if (onChecked) onChecked(); break;
            case CheckedState::Unchecked:     if (onUnchecked) onUnchecked(); break;
            case CheckedState::Indeterminate: if (onIndeterminate) onIndeterminate(); break;
        }
        NotifyAccessibility(AccessibilityEventType::StateChanged);
        RequestRedraw();
    }

    void UltraCanvasLabeledToggleBase::Toggle() {
        SetCheckState(IsChecked() ? CheckedState::Unchecked : CheckedState::Checked);
    }

    void UltraCanvasLabeledToggleBase::CalculateLayout(IRenderContext* ctx) {
        const float padding = 4.0f;
        Size2Dd indicatorSize = GetIndicatorSize();

        indicatorRect.x = padding;
        indicatorRect.y = (GetHeight() - indicatorSize.height) / 2.0f;
        indicatorRect.width = indicatorSize.width;
        indicatorRect.height = indicatorSize.height;

        labelWrapped = false;
        if (!text.empty()) {
            textRect.x = indicatorRect.x + indicatorRect.width + GetBaseVisualStyle().textSpacing;
            textRect.y = 0;
            textRect.width = GetWidth() - textRect.x - padding;
            textRect.height = GetHeight();
            if (labelWrap && ctx && textRect.width > 1.0f) {
                const auto& base = GetBaseVisualStyle();
                ctx->PushState();
                ctx->SetFontFace(base.fontFamily, base.fontWeight, FontSlant::Normal);
                ctx->SetFontSize(base.fontSize);
                const Size2Di oneLine = ctx->GetTextLineDimensions(text);
                if (oneLine.width > textRect.width) {
                    labelWrapped = true;
                    // The indicator beside the first line, the lines from the top
                    // inset the measure gave them
                    indicatorRect.y = padding + (oneLine.height - indicatorSize.height) / 2.0f;
                    textRect.y = padding;
                    textRect.height = GetHeight() - 2.0f * padding;
                }
                ctx->PopState();
            }
        }

        totalBounds.x = 0;
        totalBounds.y = 0;
        totalBounds.width = GetWidth();
        totalBounds.height = GetHeight();

        layoutDirty = false;
    }

    Size2Df UltraCanvasLabeledToggleBase::MeasureContentSize(IRenderContext* ctx, float availableWidth) const {
        const auto& base = GetBaseVisualStyle();
        Size2Df indicatorSize = GetIndicatorSize();

        Size2Di textSize{0, 0};
        if (!text.empty() && ctx) {
            ctx->SetFontFace(base.fontFamily, base.fontWeight, FontSlant::Normal);
            ctx->SetFontSize(base.fontSize);
            textSize = ctx->GetTextLineDimensions(text);
            // Wrapping: as tall as the lines the label takes at the width it has
            const float room = availableWidth - 16.0f - indicatorSize.width - base.textSpacing;
            if (labelWrap && availableWidth > 0 && textSize.width > room && room > 1.0f) {
                ctx->SetTextWrap(TextWrap::WrapWord);
                textSize = ctx->GetTextDimensions(text, Size2Di(static_cast<int>(room), 0));
            }
        }

        // Natural box = 8px inset each side + indicator + (spacing + label) when present.
        float totalWidth = 8.0 + indicatorSize.width + (text.empty() ? 0 : base.textSpacing + textSize.width) + 8.0;
        float totalHeight = std::max(indicatorSize.height + 8, static_cast<float>(textSize.height + 8));

        return Size2Df(totalWidth, totalHeight);
    }

    void UltraCanvasLabeledToggleBase::DrawLabel(IRenderContext* ctx) {
        const auto& base = GetBaseVisualStyle();
        Color color = !IsDisabled()
                ? (IsHovered() ? base.textHoverColor : base.textColor)
                : base.textDisabledColor;

        FontStyle font;
        font.fontFamily = base.fontFamily;
        font.fontSize = base.fontSize;
        font.fontWeight = base.fontWeight;
        ctx->SetFontStyle(font);
        ctx->SetTextPaint(color);

        if (labelWrapped) {
            // Several lines, from the top, broken at the label's width
            ctx->SetTextWrap(TextWrap::WrapWord);
            if (auto layout = ctx->GetOrCreateTextLayout(text, Size2Di(static_cast<int>(textRect.width), 0), false)) {
                ctx->DrawTextLayout(*layout, Point2Dd(textRect.x, textRect.y));
            }
            return;
        }
        // Centred on its capitals - level with the box or dot centred beside
        // it - not on its line box, which sits high or low with the font.
        float textY = static_cast<float>(ctx->TextTopCentredOnCaps(textRect, font));
        ctx->DrawText(text, Point2Dd(textRect.x, textY));
    }

    void UltraCanvasLabeledToggleBase::DrawFocusRingShape(IRenderContext* ctx) {
        const auto& base = GetBaseVisualStyle();
        Rect2Dd focusRect(
                indicatorRect.x - base.focusRingWidth,
                indicatorRect.y - base.focusRingWidth,
                indicatorRect.width + 2 * base.focusRingWidth,
                indicatorRect.height + 2 * base.focusRingWidth);
        ctx->DrawFilledRectangle(focusRect, base.focusRingColor,
                                 base.focusRingWidth, base.focusRingColor, 0.0f);
    }

    Size2Df UltraCanvasLabeledToggleBase::MeasureOwnContent(std::optional<float> definiteContentWidth,
                                                            const CSSLayout::LayoutContext& /*ctx*/) {
        IRenderContext* rc = GetRenderContext();
        if (!rc) {
            // No surface to measure the label against yet — report no own content;
            // the block path resolves from size.width/height or the constraints.
            return Size2Df(0.f, 0.f);
        }
        // Content box: indicator + spacing + label (their insets are baked in;
        // these widgets carry no CSS padding/border). The block layout applies
        // size.*/constraints — leave size = Auto for content sizing.
        rc->PushState();
        Size2Df content = MeasureContentSize(rc, definiteContentWidth ? *definiteContentWidth : -1.0f);
        rc->PopState();
        // Wrapped, it fills the width it was given rather than reporting the
        // one-line width it does not have
        if (labelWrap && definiteContentWidth && content.width > *definiteContentWidth)
            content.width = *definiteContentWidth;
        return Size2Df((float)content.width, (float)content.height);
    }

    void UltraCanvasLabeledToggleBase::ComputeIntrinsicSizes(const CSSLayout::LayoutContext& /*ctx*/) {
        IRenderContext* rc = GetRenderContext();
        if (!rc) {
            intrinsic.minContentWidth = intrinsic.maxContentWidth = 0;
            intrinsic.minContentHeight = intrinsic.maxContentHeight = 0;
            return;
        }

        const float padH = GetTotalPaddingHorizontal() + GetTotalBorderHorizontal();
        const float padV = GetTotalPaddingVertical()   + GetTotalBorderVertical();

        rc->PushState();
        Size2Dd content = MeasureContentSize(rc);
        // A wrapping label can narrow to its longest word; one that does not
        // wrap has min-content == max-content.
        float minWidth = (float)content.width;
        if (labelWrap && !text.empty()) {
            const auto& base = GetBaseVisualStyle();
            float longest = 0.0f;
            size_t start = 0;
            while (start <= text.size()) {
                size_t end = text.find(' ', start);
                if (end == std::string::npos) end = text.size();
                if (end > start)
                    longest = std::max(longest, (float)rc->GetTextLineDimensions(text.substr(start, end - start)).width);
                start = end + 1;
            }
            minWidth = std::min(minWidth, 16.0f + (float)GetIndicatorSize().width + base.textSpacing + longest);
        }
        rc->PopState();

        // Publish in border-box units.
        intrinsic.valid = true;
        intrinsic.maxContentWidth  = (float)content.width  + padH;
        intrinsic.minContentWidth  = minWidth              + padH;
        intrinsic.maxContentHeight = (float)content.height + padV;
        intrinsic.minContentHeight = (float)content.height + padV;
    }

    void UltraCanvasLabeledToggleBase::Arrange(const Rect2Df& finalRect, const CSSLayout::LayoutContext& ctx) {
        UltraCanvasUIElement::Arrange(finalRect, ctx);   // sets finalBounds + damage
        // Position indicator/label/totalBounds within the resolved bounds (local coords).
        CalculateLayout(GetRenderContext());
        layoutDirty = false;
    }

    void UltraCanvasLabeledToggleBase::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        ctx->PushState();
        DrawIndicator(ctx);
        if (!text.empty()) DrawLabel(ctx);
        if (IsFocused() && GetBaseVisualStyle().hasFocusRing) DrawFocusRingShape(ctx);
        ctx->PopState();
    }

    bool UltraCanvasLabeledToggleBase::OnEvent(const UCEvent& event) {
        if (IsDisabled() || !IsVisible()) return false;
        if (UltraCanvasUIElement::OnEvent(event)) return true;

        bool handled = false;
        switch (event.type) {
            case UCEventType::MouseDown:
            // A rapid second click arrives as a double-click instead of a
            // MouseDown; without treating it as a press, the MouseUp that
            // follows would not toggle.
            case UCEventType::MouseDoubleClick:
                if (totalBounds.Contains(event.pointer)) {
                    SetPressed(true);
                    SetFocus(true);   // no-op when focus is refused
                    handled = true;
                }
                break;

            case UCEventType::MouseUp:
                if (IsPressed() && totalBounds.Contains(event.pointer)) {
                    OnActivate();
                    handled = true;
                }
                SetPressed(false);
                break;

            case UCEventType::MouseMove:
                SetHovered(totalBounds.Contains(event.pointer));
                break;

            case UCEventType::MouseEnter:
                SetHovered(true);
                break;

            case UCEventType::MouseLeave:
                SetHovered(false);
                break;

            case UCEventType::KeyDown:
                // Space only, as in HTML and the native toolkits: Enter goes on
                // to the window, where a dialog presses its default button.
                if (IsFocused() && event.virtualKey == UCKeys::Space &&
                    !event.ctrl && !event.alt && !event.meta) {
                    OnActivate();
                    handled = true;
                }
                break;

            case UCEventType::FocusGained:
            case UCEventType::FocusLost:
                RequestRedraw();
                handled = true;
                break;

            default:
                break;
        }
        return handled;
    }

} // namespace UltraCanvas
