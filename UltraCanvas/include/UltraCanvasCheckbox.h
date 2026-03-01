// UltraCanvasCheckbox.h
// Interactive checkbox component with multiple states and customizable appearance
// Version: 1.1.0
// Last Modified: 2024-12-19
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include <string>
#include <functional>

namespace UltraCanvas {

// ===== CHECKBOX STATES =====
    enum class CheckboxState {
        Unchecked,
        Checked,
        Indeterminate  // Partially checked state for tree views
    };

// ===== CHECKBOX APPEARANCE STYLES =====
    enum class CheckboxStyle {
        Standard,       // Default square checkbox
        Rounded,        // Rounded corners
        Switch,         // Toggle switch style
        Radio,          // Radio button style (circular)
        Material        // Material Design style
    };

// ===== CHECKBOX VISUAL STYLE =====
    struct CheckboxVisualStyle {
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

        // Text appearance
        Color textColor = Colors::TextDefault;
        Color textHoverColor = Colors::TextDefault;
        Color textDisabledColor = Colors::TextDisabled;

        // Layout
        float boxSize = 16.0f;
        float borderWidth = 1.0f;
        float cornerRadius = 2.0f;
        float checkmarkThickness = 2.0f;
        int textSpacing = 6;  // Space between checkbox and label

        // Text styling
        std::string fontFamily = "Arial";
        float fontSize = 12.0f;
        FontWeight fontWeight = FontWeight::Normal;

        // Effects
        bool hasFocusRing = true;
        Color focusRingColor = Color(0, 120, 215, 128);
        float focusRingWidth = 2.0f;
    };

// ===== MAIN CHECKBOX CLASS =====
    class UltraCanvasCheckbox : public UltraCanvasUIElement {
    private:
        // Core properties
        std::string text;
        CheckboxState checkState = CheckboxState::Unchecked;
        CheckboxStyle style = CheckboxStyle::Standard;
        CheckboxVisualStyle visualStyle;

        // State tracking
        bool allowIndeterminate = false;
        bool layoutDirty = true;
        bool autoSize = false;

        // Layout calculations
        Rect2Df boxRect;
        Rect2Df textRect;
        Rect2Df totalBounds;

        // Helper methods
        void CalculateLayout();
        void DrawCheckbox(IRenderContext* ctx);
        void DrawCheckmark(IRenderContext* ctx);
        void DrawIndeterminateMark(IRenderContext* ctx);
        void DrawLabel(IRenderContext* ctx);
        void DrawFocusRing(IRenderContext* ctx);

        Color GetCurrentBoxColor() const;
        Color GetCurrentCheckmarkColor() const;
        void CalculateAutoSize();

    public:
        // ===== CONSTRUCTORS =====
        UltraCanvasCheckbox(const std::string& identifier = "", long id = 0,
                            long x = 0, long y = 0, long w = 150, long h = 24,
                            const std::string& labelText = "");

        virtual ~UltraCanvasCheckbox() = default;

        // ===== STATE MANAGEMENT =====
        void SetChecked(bool checked);
        bool IsChecked() const { return checkState == CheckboxState::Checked; }

        void SetCheckState(CheckboxState state);
        CheckboxState GetCheckState() const { return checkState; }

        void SetIndeterminate(bool indeterminate);
        bool IsIndeterminate() const { return checkState == CheckboxState::Indeterminate; }

        void SetAllowIndeterminate(bool allow) { allowIndeterminate = allow; }
        bool GetAllowIndeterminate() const { return allowIndeterminate; }

        void SetAutoSize(bool val) { autoSize = val; }
        void Toggle();

        // ===== APPEARANCE =====
        void SetText(const std::string& labelText) { text = labelText; CalculateLayout(); }
        std::string GetText() const { return text; }

        void SetStyle(CheckboxStyle newStyle) { style = newStyle; CalculateLayout(); }
        CheckboxStyle GetStyle() const { return style; }

        void SetVisualStyle(const CheckboxVisualStyle& newStyle) { visualStyle = newStyle; CalculateLayout(); }
        CheckboxVisualStyle& GetVisualStyle() { return visualStyle; }
        const CheckboxVisualStyle& GetVisualStyle() const { return visualStyle; }

        void SetBoxSize(float size) { visualStyle.boxSize = size; CalculateLayout(); }
        float GetBoxSize() const { return visualStyle.boxSize; }

        void SetColors(const Color& box, const Color& checkmark, const Color& text);
        void SetFont(const std::string& family, float size, FontWeight weight = FontWeight::Normal);
        void SetFontSize(float size);

        // ===== RENDERING =====
        void Render(IRenderContext* ctx) override;

        // ===== EVENT HANDLING =====
        bool OnEvent(const UCEvent& event) override;

        // ===== CALLBACKS =====
        std::function<void(CheckboxState oldState, CheckboxState newState)> onStateChanged;
        std::function<void()> onChecked;
        std::function<void()> onUnchecked;
        std::function<void()> onIndeterminate;

        // ===== AUTO-SIZING =====

        // ===== FACTORY METHODS =====
        static std::shared_ptr<UltraCanvasCheckbox> CreateCheckbox(
                const std::string& identifier, long id,
                long x, long y, long w, long h,
                const std::string& text = "",
                bool checked = false);

        static std::shared_ptr<UltraCanvasCheckbox> CreateSwitch(
                const std::string& identifier, long id,
                long x, long y,
                const std::string& text = "",
                bool checked = false);

        static std::shared_ptr<UltraCanvasCheckbox> CreateRadioButton(
                const std::string& identifier, long id,
                long x, long y,
                const std::string& text = "",
                bool checked = false);
    };

// ===== RADIO BUTTON GROUP MANAGER =====
    class UltraCanvasRadioGroup {
    private:
        std::vector<std::shared_ptr<UltraCanvasCheckbox>> radioButtons;
        std::shared_ptr<UltraCanvasCheckbox> selectedButton;

    public:
        void AddRadioButton(std::shared_ptr<UltraCanvasCheckbox> button);
        void RemoveRadioButton(std::shared_ptr<UltraCanvasCheckbox> button);
        void SelectButton(std::shared_ptr<UltraCanvasCheckbox> button);
        std::shared_ptr<UltraCanvasCheckbox> GetSelectedButton() const { return selectedButton; }
        void ClearSelection();

        std::function<void(std::shared_ptr<UltraCanvasCheckbox>)> onSelectionChanged;
    };

} // namespace UltraCanvas