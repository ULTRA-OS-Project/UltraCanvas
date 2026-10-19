// UltraCanvasRadio.h
// Radio button: circular indicator with center dot, exclusive selection via UltraCanvasRadioGroup.
// Version: 1.5.0 - AddRadioButton adopts a radio that is already checked
// Version: 1.4.0 - a radio button to screen readers, selected by its action
// Version: 1.3.0 - the group's onChecked handler holds its radio raw (it kept the radio
//                 alive forever) and is taken back when the group goes or the radio leaves it
// Version: 1.2.0
// Last Modified: 2026-10-08
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasLabeledToggleBase.h"
#include <memory>
#include <vector>

namespace UltraCanvas {

// ===== RADIO VISUAL STYLE =====
    struct RadioVisualStyle {
        LabeledToggleVisualStyle base;

        // Outer circle (the ring)
        Color outerColor = Colors::ButtonFace;
        Color outerBorderColor = Colors::ButtonShadow;
        Color outerHoverColor = Colors::SelectionHover;
        Color outerPressedColor = Color(204, 228, 247, 255);
        // Lighter than outerColor, and the border lighter than
        // outerBorderColor - see the note on Colors::ControlDisabled.
        Color outerDisabledColor = Colors::ControlDisabled;
        Color outerBorderDisabledColor = Colors::ControlDisabledBorder;

        // Inner dot (visible when checked)
        Color innerDotColor = Colors::TextDefault;
        Color innerDotDisabledColor = Colors::TextDisabled;

        // Layout
        float boxSize = 16.0f;       // Diameter of outer circle
        float borderWidth = 1.0f;
        float dotInsetRatio = 0.3f;  // Inner dot radius = (boxSize/2) * (1 - 2*dotInsetRatio)
    };

// ===== RADIO BUTTON =====
    class UltraCanvasRadio : public UltraCanvasLabeledToggleBase {
    private:
        RadioVisualStyle visualStyle;

        Color GetCurrentOuterColor() const;
        Color GetCurrentInnerDotColor() const;

    protected:
        void DrawIndicator(IRenderContext* ctx) override;
        Size2Df GetIndicatorSize() const override { return {visualStyle.boxSize, visualStyle.boxSize}; }
        void OnActivate() override { SetChecked(true); }  // Standard UX: clicking selected radio is no-op.
    public:
        AccessibleRole GetAccessibleRole() const override { return AccessibleRole::RadioButton; }
        std::string GetAccessibleActionName() const override { return "select"; }
    protected:
        const LabeledToggleVisualStyle& GetBaseVisualStyle() const override { return visualStyle.base; }
        void DrawFocusRingShape(IRenderContext* ctx) override;

    public:
        // ===== CONSTRUCTORS =====
        UltraCanvasRadio(const std::string& identifier,
                         float x, float y, float w, float h,
                         const std::string& labelText = "");

        UltraCanvasRadio(const std::string& identifier,
                         float w, float h,
                         const std::string& labelText = "")
            : UltraCanvasRadio(identifier, -1, -1, w, h, labelText) {}

        UltraCanvasRadio(const std::string& identifier, const std::string& labelText)
            : UltraCanvasRadio(identifier, -1, -1, -1, -1, labelText) {}

        explicit UltraCanvasRadio(const std::string& labelText = "")
            : UltraCanvasRadio("", -1, -1, -1, -1, labelText) {}

        ~UltraCanvasRadio() override = default;

        // Indeterminate is invalid for radios; clamp to Unchecked.
        void SetCheckState(CheckedState state) override;

        // ===== APPEARANCE =====
        void SetVisualStyle(const RadioVisualStyle& s) { visualStyle = s; layoutDirty = true; InvalidateLayout(); RequestRedraw(); }
        RadioVisualStyle& GetVisualStyle() { return visualStyle; }
        const RadioVisualStyle& GetVisualStyle() const { return visualStyle; }

        void SetBoxSize(float size) { visualStyle.boxSize = size; layoutDirty = true; InvalidateLayout(); RequestRedraw(); }
        float GetBoxSize() const { return visualStyle.boxSize; }

        // ===== FACTORY =====
        static std::shared_ptr<UltraCanvasRadio> Create(
                const std::string& identifier,
                float x, float y,
                const std::string& text = "",
                bool checked = false);
    };

// ===== EXCLUSIVE-SELECTION GROUP =====
// Not an element, and not owned by its radios: keep the group alive for as
// long as the selection should work (a member of the window or dialog). It may
// still go first - it then takes back the onChecked handler it installed on
// each radio, so a radio clicked afterwards only checks itself. A radio that
// leaves the group (RemoveRadioButton) loses the handler the same way. Only the
// group's own handler is taken back: one the application assigned since stays.
// Moving a group hands its radios' clicks to the new object; a copy lists the
// same radios but their clicks stay with the original.
    class UltraCanvasRadioGroup {
    private:
        std::vector<std::shared_ptr<UltraCanvasRadio>> radioButtons;
        std::shared_ptr<UltraCanvasRadio> selectedButton;

        // The onChecked handler installed on each radio. A named type, not a
        // lambda, so the group can recognise its own handler. Both pointers
        // are raw: the radio owns the handler (a shared_ptr back to the radio
        // would keep it alive forever), and the group takes the handler back
        // before it goes.
        struct CheckedHandler {
            UltraCanvasRadioGroup* group;
            UltraCanvasRadio* radio;
            void operator()() const;
        };
        void Detach(UltraCanvasRadio& radio);
        void DetachAll();
        void TakeOverHandlersFrom(const UltraCanvasRadioGroup* previous);

    public:
        UltraCanvasRadioGroup() = default;
        ~UltraCanvasRadioGroup();
        UltraCanvasRadioGroup(const UltraCanvasRadioGroup& other) = default;
        UltraCanvasRadioGroup& operator=(const UltraCanvasRadioGroup& other);
        UltraCanvasRadioGroup(UltraCanvasRadioGroup&& other) noexcept;
        UltraCanvasRadioGroup& operator=(UltraCanvasRadioGroup&& other) noexcept;

        // A radio added already checked becomes the selection (the last such
        // radio wins; the others are cleared), without onSelectionChanged.
        void AddRadioButton(std::shared_ptr<UltraCanvasRadio> button);
        void RemoveRadioButton(std::shared_ptr<UltraCanvasRadio> button);
        void SelectButton(std::shared_ptr<UltraCanvasRadio> button);
        std::shared_ptr<UltraCanvasRadio> GetSelectedButton() const { return selectedButton; }
        void ClearSelection();

        std::function<void(std::shared_ptr<UltraCanvasRadio>)> onSelectionChanged;
    };

} // namespace UltraCanvas
