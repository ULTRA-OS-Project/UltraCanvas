// UltraCanvasLabeledToggleBase.cpp
// Shared layout/event/label/focus plumbing for checkbox, radio, and switch.
// Version: 1.1.0
// Last Modified: 2026-06-02
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

        if (!text.empty()) {
            textRect.x = indicatorRect.x + indicatorRect.width + GetBaseVisualStyle().textSpacing;
            textRect.y = 0;
            textRect.width = GetWidth() - textRect.x - padding;
            textRect.height = GetHeight();
        }

        totalBounds.x = 0;
        totalBounds.y = 0;
        totalBounds.width = GetWidth();
        totalBounds.height = GetHeight();

        layoutDirty = false;
    }

    Size2Df UltraCanvasLabeledToggleBase::MeasureContentSize(IRenderContext* ctx) const {
        const auto& base = GetBaseVisualStyle();
        Size2Df indicatorSize = GetIndicatorSize();

        Size2Di textSize{0, 0};
        if (!text.empty() && ctx) {
            ctx->SetFontFace(base.fontFamily, base.fontWeight, FontSlant::Normal);
            ctx->SetFontSize(base.fontSize);
            textSize = ctx->GetTextLineDimensions(text);
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

        ctx->SetFontFace(base.fontFamily, base.fontWeight, FontSlant::Normal);
        ctx->SetFontSize(base.fontSize);
        ctx->SetTextPaint(color);

        float textY = textRect.y + (textRect.height - ctx->GetTextLineHeight(text)) / 2.0f;
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

    Size2Df UltraCanvasLabeledToggleBase::MeasureOwnContent(std::optional<float> /*definiteContentWidth*/,
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
        Size2Df content = MeasureContentSize(rc);
        rc->PopState();
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
        rc->PopState();

        // No wrapping — min-content == max-content. Publish in border-box units.
        intrinsic.valid = true;
        intrinsic.maxContentWidth  = (float)content.width  + padH;
        intrinsic.minContentWidth  = (float)content.width  + padH;
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
                if (totalBounds.Contains(event.pointer)) {
                    SetPressed(true);
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
                if (IsFocused()
                    && (event.virtualKey == UCKeys::Space || event.virtualKey == UCKeys::Enter)) {
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
