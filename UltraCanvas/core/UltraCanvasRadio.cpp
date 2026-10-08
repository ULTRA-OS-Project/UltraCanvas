// UltraCanvasRadio.cpp
// Radio button rendering and exclusive-selection group.
// Version: 1.3.0 - a radio added already checked becomes the group's selection
// Version: 1.2.0 - the group's onChecked handler no longer owns its radio, and the group
//                 takes it back when it is destroyed or the radio is removed
// Version: 1.1.1
// Last Modified: 2026-10-07
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

        // The ring greys with the face - see UltraCanvasCheckbox::DrawIndicator.
        ctx->DrawFilledCircle(center, radius,
                              GetCurrentOuterColor(),
                              IsDisabled() ? visualStyle.outerBorderDisabledColor
                                           : visualStyle.outerBorderColor,
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
    // A click checks the radio, which lands here: select it by the shared_ptr
    // the group holds.
    void UltraCanvasRadioGroup::CheckedHandler::operator()() const {
        for (const auto& member : group->radioButtons) {
            if (member.get() == radio) {
                group->SelectButton(member);
                return;
            }
        }
    }

    void UltraCanvasRadioGroup::Detach(UltraCanvasRadio& radio) {
        const auto* handler = radio.onChecked.target<CheckedHandler>();
        if (handler && handler->group == this) radio.onChecked = nullptr;
    }

    void UltraCanvasRadioGroup::DetachAll() {
        for (auto& radio : radioButtons) {
            if (radio) Detach(*radio);
        }
    }

    // After a move: the handlers still name the group the radios came from.
    void UltraCanvasRadioGroup::TakeOverHandlersFrom(const UltraCanvasRadioGroup* previous) {
        for (auto& radio : radioButtons) {
            auto* handler = radio ? radio->onChecked.target<CheckedHandler>() : nullptr;
            if (handler && handler->group == previous) handler->group = this;
        }
    }

    UltraCanvasRadioGroup::~UltraCanvasRadioGroup() {
        DetachAll();
    }

    UltraCanvasRadioGroup& UltraCanvasRadioGroup::operator=(const UltraCanvasRadioGroup& other) {
        if (this != &other) {
            DetachAll();   // the radios this group had would otherwise keep a handler naming it
            radioButtons = other.radioButtons;
            selectedButton = other.selectedButton;
            onSelectionChanged = other.onSelectionChanged;
        }
        return *this;
    }

    UltraCanvasRadioGroup::UltraCanvasRadioGroup(UltraCanvasRadioGroup&& other) noexcept
            : radioButtons(std::move(other.radioButtons)),
              selectedButton(std::move(other.selectedButton)),
              onSelectionChanged(std::move(other.onSelectionChanged)) {
        other.radioButtons.clear();
        TakeOverHandlersFrom(&other);
    }

    UltraCanvasRadioGroup& UltraCanvasRadioGroup::operator=(UltraCanvasRadioGroup&& other) noexcept {
        if (this != &other) {
            DetachAll();
            radioButtons = std::move(other.radioButtons);
            selectedButton = std::move(other.selectedButton);
            onSelectionChanged = std::move(other.onSelectionChanged);
            other.radioButtons.clear();
            TakeOverHandlersFrom(&other);
        }
        return *this;
    }

    void UltraCanvasRadioGroup::AddRadioButton(std::shared_ptr<UltraCanvasRadio> button) {
        if (!button) return;
        radioButtons.push_back(button);
        button->onChecked = CheckedHandler{ this, button.get() };
        // A radio that arrives checked is the group's choice - the last one
        // added wins, as the last checked radio of an HTML group does - and
        // the one chosen before is cleared. Building a group is not a choice
        // the user made, so onSelectionChanged is not called.
        if (button->IsChecked()) {
            selectedButton = button;
            for (auto& other : radioButtons) {
                if (other != button && other->IsChecked()) other->SetChecked(false);
            }
        }
    }

    void UltraCanvasRadioGroup::RemoveRadioButton(std::shared_ptr<UltraCanvasRadio> button) {
        if (button) Detach(*button);
        radioButtons.erase(std::remove(radioButtons.begin(), radioButtons.end(), button),
                           radioButtons.end());
        if (selectedButton == button) selectedButton = nullptr;
    }

    void UltraCanvasRadioGroup::SelectButton(std::shared_ptr<UltraCanvasRadio> button) {
        if (!button) return;
        if (std::find(radioButtons.begin(), radioButtons.end(), button) == radioButtons.end()) return;
        // Already the selection: nothing to change, and no callback to repeat.
        if (selectedButton == button && button->IsChecked()) return;

        // Check the target too. This used to be left to the caller, which is
        // true only of the click path (AddRadioButton routes onChecked here,
        // so the button has already checked itself). A programmatic
        // SelectButton() unchecked the others and checked nothing, leaving the
        // group with no dot at all.
        //
        // selectedButton is assigned FIRST because SetChecked fires onChecked,
        // which re-enters this function; by then the guard above sees a
        // consistent group and returns, so the work below - and the
        // notification - happen exactly once.
        selectedButton = button;
        button->SetChecked(true);
        for (auto& other : radioButtons) {
            if (other != button && other->IsChecked()) other->SetChecked(false);
        }
        if (onSelectionChanged) onSelectionChanged(button);
    }

    void UltraCanvasRadioGroup::ClearSelection() {
        for (auto& btn : radioButtons) btn->SetChecked(false);
        selectedButton = nullptr;
        if (onSelectionChanged) onSelectionChanged(nullptr);
    }

} // namespace UltraCanvas
