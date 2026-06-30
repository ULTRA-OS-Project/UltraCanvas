// UltraCanvasRadio.cpp
// Radio button rendering and exclusive-selection group.
// Version: 1.1.0
// Last Modified: 2026-06-02
// Author: UltraCanvas Framework

#include "UltraCanvasRadio.h"
#include <algorithm>

namespace UltraCanvas {

    UltraCanvasRadio::UltraCanvasRadio(const std::string& identifier,
                                       float x, float y, float w, float h,
                                       const std::string& labelText)
            : UltraCanvasLabeledToggleBase(identifier, x, y, w, h, labelText) {}

    void UltraCanvasRadio::SetCheckState(CheckedState state) {
        if (state == CheckedState::Indeterminate) state = CheckedState::Unchecked;
        UltraCanvasLabeledToggleBase::SetCheckState(state);
    }

    Color UltraCanvasRadio::GetCurrentOuterColor() const {
        switch (GetPrimaryState()) {
            case ElementState::Disabled: return visualStyle.outerDisabledColor;
            case ElementState::Pressed:  return visualStyle.outerPressedColor;
            case ElementState::Hovered:  return visualStyle.outerHoverColor;
            default:                     return visualStyle.outerColor;
        }
    }

    Color UltraCanvasRadio::GetCurrentInnerDotColor() const {
        return IsDisabled() ? visualStyle.innerDotDisabledColor : visualStyle.innerDotColor;
    }

    void UltraCanvasRadio::DrawIndicator(IRenderContext* ctx) {
        Point2Df center(indicatorRect.x + indicatorRect.width / 2.0f,
                        indicatorRect.y + indicatorRect.height / 2.0f);
        float radius = indicatorRect.width / 2.0f;

        ctx->DrawFilledCircle(center, radius,
                              GetCurrentOuterColor(),
                              visualStyle.outerBorderColor,
                              visualStyle.borderWidth);

        if (checkState == CheckedState::Checked) {
            float dotRadius = radius * (1.0f - 2.0f * visualStyle.dotInsetRatio);
            ctx->DrawFilledCircle(center, dotRadius, GetCurrentInnerDotColor());
        }
    }

    void UltraCanvasRadio::DrawFocusRingShape(IRenderContext* ctx) {
        const auto& base = visualStyle.base;
        Point2Df center(indicatorRect.x + indicatorRect.width / 2.0f,
                        indicatorRect.y + indicatorRect.height / 2.0f);
        float radius = indicatorRect.width / 2.0f + base.focusRingWidth;
        ctx->DrawFilledCircle(center, radius, base.focusRingColor,
                              base.focusRingColor, base.focusRingWidth);
    }

    std::shared_ptr<UltraCanvasRadio> UltraCanvasRadio::Create(
            const std::string& identifier,
            float x, float y,
            const std::string& text, bool checked) {
        auto radio = std::make_shared<UltraCanvasRadio>(identifier, x, y, 150, 24, text);
        radio->SetChecked(checked);
        // Content-size to indicator + label: clear the ctor-stamped pixel size.
        radio->size.width  = CSSLayout::Dimension::Auto();
        radio->size.height = CSSLayout::Dimension::Auto();
        return radio;
    }

// ===== RADIO GROUP =====
    void UltraCanvasRadioGroup::AddRadioButton(std::shared_ptr<UltraCanvasRadio> button) {
        if (!button) return;
        radioButtons.push_back(button);
        button->onChecked = [this, button]() { SelectButton(button); };
    }

    void UltraCanvasRadioGroup::RemoveRadioButton(std::shared_ptr<UltraCanvasRadio> button) {
        radioButtons.erase(std::remove(radioButtons.begin(), radioButtons.end(), button),
                           radioButtons.end());
        if (selectedButton == button) selectedButton = nullptr;
    }

    void UltraCanvasRadioGroup::SelectButton(std::shared_ptr<UltraCanvasRadio> button) {
        if (!button) return;
        if (std::find(radioButtons.begin(), radioButtons.end(), button) == radioButtons.end()) return;

        for (auto& other : radioButtons) {
            if (other != button && other->IsChecked()) other->SetChecked(false);
        }
        selectedButton = button;
        if (onSelectionChanged) onSelectionChanged(button);
    }

    void UltraCanvasRadioGroup::ClearSelection() {
        for (auto& btn : radioButtons) btn->SetChecked(false);
        selectedButton = nullptr;
        if (onSelectionChanged) onSelectionChanged(nullptr);
    }

} // namespace UltraCanvas
