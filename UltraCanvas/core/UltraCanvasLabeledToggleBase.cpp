// UltraCanvasLabeledToggleBase.cpp
// Shared layout/event/label/focus plumbing for checkbox, radio, and switch.
// Version: 1.0.0
// Last Modified: 2026-05-07
// Author: UltraCanvas Framework

#include "UltraCanvasLabeledToggleBase.h"
#include "UltraCanvasApplication.h"
#include <algorithm>

namespace UltraCanvas {

    UltraCanvasLabeledToggleBase::UltraCanvasLabeledToggleBase(
            const std::string& identifier, long id,
            long x, long y, long w, long h,
            const std::string& labelText)
            : UltraCanvasUIElement(identifier, id, x, y, w, h), text(labelText) {}

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
        Size2Df indicatorSize = GetIndicatorSize();

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

    void UltraCanvasLabeledToggleBase::CalculateAutoSize(IRenderContext* ctx) {
        const auto& base = GetBaseVisualStyle();
        Size2Df indicatorSize = GetIndicatorSize();

        Size2Di textSize{0, 0};
        if (!text.empty()) {
            ctx->SetFontFace(base.fontFamily, base.fontWeight, FontSlant::Normal);
            ctx->SetFontSize(base.fontSize);
            textSize = ctx->GetTextLineDimensions(text);
        }

        double totalWidth = 8.0 + indicatorSize.width + (text.empty() ? 0 : base.textSpacing + textSize.width) + 8.0;
        double totalHeight = std::max(indicatorSize.height + 8.0, static_cast<double>(textSize.height + 8));

        SetSize(static_cast<int>(totalWidth), static_cast<int>(totalHeight));
    }

    void UltraCanvasLabeledToggleBase::DrawLabel(IRenderContext* ctx) {
        const auto& base = GetBaseVisualStyle();
        Color color = !IsDisabled()
                ? (IsHovered() ? base.textHoverColor : base.textColor)
                : base.textDisabledColor;

        ctx->SetFontFace(base.fontFamily, base.fontWeight, FontSlant::Normal);
        ctx->SetFontSize(base.fontSize);
        ctx->SetTextPaint(color);

        double textY = textRect.y + (textRect.height - ctx->GetTextLineHeight(text)) / 2.0f;
        ctx->DrawText(text, Point2Df(textRect.x, textY));
    }

    void UltraCanvasLabeledToggleBase::DrawFocusRingShape(IRenderContext* ctx) {
        const auto& base = GetBaseVisualStyle();
        Rect2Df focusRect(
                indicatorRect.x - base.focusRingWidth,
                indicatorRect.y - base.focusRingWidth,
                indicatorRect.width + 2 * base.focusRingWidth,
                indicatorRect.height + 2 * base.focusRingWidth);
        ctx->DrawFilledRectangle(focusRect, base.focusRingColor,
                                 base.focusRingWidth, base.focusRingColor, 0.0f);
    }

    void UltraCanvasLabeledToggleBase::UpdateGeometry(IRenderContext* ctx) {
        if (layoutDirty) {
            ctx->PushState();
            if (autoSize) CalculateAutoSize(ctx);
            CalculateLayout(ctx);
            ctx->PopState();
        }
    }

    void UltraCanvasLabeledToggleBase::Render(IRenderContext* ctx, const Rect2Di& dirtyRect) {
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
