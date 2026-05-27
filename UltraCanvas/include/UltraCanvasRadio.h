// UltraCanvasRadio.h
// Radio button: circular indicator with center dot, exclusive selection via UltraCanvasRadioGroup.
// Version: 1.0.0
// Last Modified: 2026-05-07
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
        Color outerDisabledColor = Colors::LightGray;

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
        Size2Dd GetIndicatorSize() const override { return {visualStyle.boxSize, visualStyle.boxSize}; }
        void OnActivate() override { SetChecked(true); }  // Standard UX: clicking selected radio is no-op.
        const LabeledToggleVisualStyle& GetBaseVisualStyle() const override { return visualStyle.base; }
        void DrawFocusRingShape(IRenderContext* ctx) override;

    public:
        UltraCanvasRadio(const std::string& identifier = "",
                         float x = 0, float y = 0, float w = 150, float h = 24,
                         const std::string& labelText = "");
        ~UltraCanvasRadio() override = default;

        // Indeterminate is invalid for radios; clamp to Unchecked.
        void SetCheckState(CheckedState state) override;

        // ===== APPEARANCE =====
        void SetVisualStyle(const RadioVisualStyle& s) { visualStyle = s; layoutDirty = true; }
        RadioVisualStyle& GetVisualStyle() { return visualStyle; }
        const RadioVisualStyle& GetVisualStyle() const { return visualStyle; }

        void SetBoxSize(float size) { visualStyle.boxSize = size; layoutDirty = true; }
        float GetBoxSize() const { return visualStyle.boxSize; }

        // ===== FACTORY =====
        static std::shared_ptr<UltraCanvasRadio> Create(
                const std::string& identifier,
                float x, float y,
                const std::string& text = "",
                bool checked = false);
    };

// ===== EXCLUSIVE-SELECTION GROUP =====
    class UltraCanvasRadioGroup {
    private:
        std::vector<std::shared_ptr<UltraCanvasRadio>> radioButtons;
        std::shared_ptr<UltraCanvasRadio> selectedButton;

    public:
        void AddRadioButton(std::shared_ptr<UltraCanvasRadio> button);
        void RemoveRadioButton(std::shared_ptr<UltraCanvasRadio> button);
        void SelectButton(std::shared_ptr<UltraCanvasRadio> button);
        std::shared_ptr<UltraCanvasRadio> GetSelectedButton() const { return selectedButton; }
        void ClearSelection();

        std::function<void(std::shared_ptr<UltraCanvasRadio>)> onSelectionChanged;
    };

} // namespace UltraCanvas
