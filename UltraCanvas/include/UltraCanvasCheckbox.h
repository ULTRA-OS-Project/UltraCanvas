// UltraCanvasCheckbox.h
// Interactive checkbox component with multiple states and customizable appearance.
// Visual variants only (Standard/Rounded/Material). Radio and Switch are now
// separate classes — see UltraCanvasRadio.h and UltraCanvasSwitch.h.
// Version: 2.0.0
// Last Modified: 2026-05-07
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasLabeledToggleBase.h"
#include <memory>

namespace UltraCanvas {

// ===== CHECKBOX VISUAL VARIANT =====
    enum class CheckboxStyle {
        Standard,   // Default square checkbox
        Rounded,    // Rounded corners
        Material    // Material Design (rounded with material colors)
    };

// ===== CHECKBOX VISUAL STYLE =====
    struct CheckboxVisualStyle {
        LabeledToggleVisualStyle base;

        // Box appearance
        Color boxColor = Colors::ButtonFace;
        Color boxBorderColor = Colors::ButtonShadow;
        Color boxHoverColor = Colors::SelectionHover;
        Color boxPressedColor = Color(204, 228, 247, 255);
        Color boxDisabledColor = Colors::LightGray;

        // Checkmark appearance
        Color checkmarkColor = Colors::TextDefault;
        Color checkmarkHoverColor = Colors::TextDefault;
        Color checkmarkDisabledColor = Colors::TextDisabled;

        // Layout
        float boxSize = 16.0f;
        float borderWidth = 1.0f;
        float cornerRadius = 2.0f;
        float checkmarkThickness = 2.0f;
    };

// ===== CHECKBOX =====
    class UltraCanvasCheckbox : public UltraCanvasLabeledToggleBase {
    private:
        CheckboxStyle style = CheckboxStyle::Standard;
        CheckboxVisualStyle visualStyle;
        bool allowIndeterminate = false;

        Color GetCurrentBoxColor() const;
        Color GetCurrentCheckmarkColor() const;
        float GetEffectiveCornerRadius() const;

        void DrawCheckmark(IRenderContext* ctx);
        void DrawIndeterminateMark(IRenderContext* ctx);

    protected:
        void DrawIndicator(IRenderContext* ctx) override;
        Size2Df GetIndicatorSize() const override { return {visualStyle.boxSize, visualStyle.boxSize}; }
        void OnActivate() override { Toggle(); }
        const LabeledToggleVisualStyle& GetBaseVisualStyle() const override { return visualStyle.base; }

    public:
        UltraCanvasCheckbox(const std::string& identifier = "", long id = 0,
                            long x = 0, long y = 0, long w = 150, long h = 24,
                            const std::string& labelText = "");
        ~UltraCanvasCheckbox() override = default;

        // ===== STATE =====
        // Three-state cycle when allowIndeterminate is enabled.
        void Toggle() override;

        void SetIndeterminate(bool indeterminate);
        bool IsIndeterminate() const { return checkState == CheckedState::Indeterminate; }

        void SetAllowIndeterminate(bool allow) { allowIndeterminate = allow; }
        bool GetAllowIndeterminate() const { return allowIndeterminate; }

        // ===== APPEARANCE =====
        void SetStyle(CheckboxStyle newStyle) { style = newStyle; layoutDirty = true; }
        CheckboxStyle GetStyle() const { return style; }

        void SetVisualStyle(const CheckboxVisualStyle& s) { visualStyle = s; layoutDirty = true; }
        CheckboxVisualStyle& GetVisualStyle() { return visualStyle; }
        const CheckboxVisualStyle& GetVisualStyle() const { return visualStyle; }

        void SetBoxSize(float size) { visualStyle.boxSize = size; layoutDirty = true; }
        float GetBoxSize() const { return visualStyle.boxSize; }

        void SetColors(const Color& box, const Color& checkmark, const Color& text);
        void SetFont(const std::string& family, float size, FontWeight weight = FontWeight::Normal);
        void SetFontSize(float size);

        // ===== FACTORY =====
        static std::shared_ptr<UltraCanvasCheckbox> CreateCheckbox(
                const std::string& identifier, long id,
                long x, long y, long w, long h,
                const std::string& text = "",
                bool checked = false);
    };

} // namespace UltraCanvas
