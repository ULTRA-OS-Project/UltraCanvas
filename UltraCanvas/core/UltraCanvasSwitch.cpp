// UltraCanvasSwitch.cpp
// Toggle switch rendering.
// Version: 1.0.0
// Last Modified: 2026-05-07
// Author: UltraCanvas Framework

#include "UltraCanvasSwitch.h"

namespace UltraCanvas {

    UltraCanvasSwitch::UltraCanvasSwitch(const std::string& identifier, long id,
                                         long x, long y, long w, long h,
                                         const std::string& labelText)
            : UltraCanvasLabeledToggleBase(identifier, id, x, y, w, h, labelText) {}

    void UltraCanvasSwitch::SetCheckState(CheckedState state) {
        if (state == CheckedState::Indeterminate) state = CheckedState::Unchecked;
        UltraCanvasLabeledToggleBase::SetCheckState(state);
    }

    Color UltraCanvasSwitch::GetCurrentTrackColor() const {
        if (IsDisabled()) return visualStyle.trackDisabledColor;
        return IsChecked() ? visualStyle.trackOnColor : visualStyle.trackOffColor;
    }

    Color UltraCanvasSwitch::GetCurrentThumbColor() const {
        return IsDisabled() ? visualStyle.thumbDisabledColor : visualStyle.thumbColor;
    }

    void UltraCanvasSwitch::DrawIndicator(IRenderContext* ctx) {
        // Pill-shaped track
        ctx->DrawFilledRectangle(indicatorRect,
                                 GetCurrentTrackColor(),
                                 visualStyle.borderWidth,
                                 visualStyle.trackBorderColor,
                                 indicatorRect.height / 2.0f);

        // Circular thumb, snapped to side matching state
        float thumbRadius = (indicatorRect.height / 2.0f) - visualStyle.thumbInset;
        float cy = indicatorRect.y + indicatorRect.height / 2.0f;
        float cx = IsChecked()
                ? (indicatorRect.x + indicatorRect.width - thumbRadius - visualStyle.thumbInset)
                : (indicatorRect.x + thumbRadius + visualStyle.thumbInset);

        ctx->DrawFilledCircle(Point2Df(cx, cy), thumbRadius,
                              GetCurrentThumbColor(),
                              visualStyle.thumbBorderColor,
                              visualStyle.borderWidth);
    }

    void UltraCanvasSwitch::DrawFocusRingShape(IRenderContext* ctx) {
        const auto& base = visualStyle.base;
        Rect2Df focusRect(
                indicatorRect.x - base.focusRingWidth,
                indicatorRect.y - base.focusRingWidth,
                indicatorRect.width + 2 * base.focusRingWidth,
                indicatorRect.height + 2 * base.focusRingWidth);
        ctx->DrawFilledRectangle(focusRect, base.focusRingColor,
                                 base.focusRingWidth, base.focusRingColor,
                                 focusRect.height / 2.0f);
    }

    std::shared_ptr<UltraCanvasSwitch> UltraCanvasSwitch::Create(
            const std::string& identifier, long id,
            long x, long y,
            const std::string& text, bool checked) {
        auto sw = std::make_shared<UltraCanvasSwitch>(identifier, id, x, y, 200, 30, text);
        sw->SetChecked(checked);
        sw->SetAutoSize(true);
        return sw;
    }

} // namespace UltraCanvas
