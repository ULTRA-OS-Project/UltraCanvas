// UltraCanvasSwitch.h
// Toggle switch: pill-shaped track with a circular thumb that snaps between sides.
// Version: 1.0.0
// Last Modified: 2026-05-07
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasLabeledToggleBase.h"
#include <memory>

namespace UltraCanvas {

// ===== SWITCH VISUAL STYLE =====
    struct SwitchVisualStyle {
        LabeledToggleVisualStyle base;

        // Track
        Color trackOffColor = Colors::LightGray;
        Color trackOnColor = Color(76, 175, 80, 255);   // Material green-ish
        Color trackBorderColor = Colors::ButtonShadow;
        Color trackDisabledColor = Color(220, 220, 220, 255);

        // Thumb
        Color thumbColor = Color(255, 255, 255, 255);
        Color thumbBorderColor = Colors::ButtonShadow;
        Color thumbDisabledColor = Color(240, 240, 240, 255);

        // Layout
        float trackWidth = 36.0f;
        float trackHeight = 18.0f;
        float thumbInset = 2.0f;     // Gap between thumb edge and track inside edge
        float borderWidth = 1.0f;
    };

// ===== TOGGLE SWITCH =====
    class UltraCanvasSwitch : public UltraCanvasLabeledToggleBase {
    private:
        SwitchVisualStyle visualStyle;

        Color GetCurrentTrackColor() const;
        Color GetCurrentThumbColor() const;

    protected:
        void DrawIndicator(IRenderContext* ctx) override;
        Size2Df GetIndicatorSize() const override { return {visualStyle.trackWidth, visualStyle.trackHeight}; }
        void OnActivate() override { Toggle(); }
        const LabeledToggleVisualStyle& GetBaseVisualStyle() const override { return visualStyle.base; }
        void DrawFocusRingShape(IRenderContext* ctx) override;

    public:
        UltraCanvasSwitch(const std::string& identifier = "", long id = 0,
                          long x = 0, long y = 0, long w = 200, long h = 30,
                          const std::string& labelText = "");
        ~UltraCanvasSwitch() override = default;

        // Indeterminate is invalid for switches; clamp to Unchecked.
        void SetCheckState(CheckedState state) override;

        // ===== APPEARANCE =====
        void SetVisualStyle(const SwitchVisualStyle& s) { visualStyle = s; layoutDirty = true; }
        SwitchVisualStyle& GetVisualStyle() { return visualStyle; }
        const SwitchVisualStyle& GetVisualStyle() const { return visualStyle; }

        void SetTrackSize(float width, float height) {
            visualStyle.trackWidth = width;
            visualStyle.trackHeight = height;
            layoutDirty = true;
        }

        // ===== FACTORY =====
        static std::shared_ptr<UltraCanvasSwitch> Create(
                const std::string& identifier, long id,
                long x, long y,
                const std::string& text = "",
                bool checked = false);
    };

} // namespace UltraCanvas
