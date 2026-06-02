// UltraCanvasLabeledToggleBase.h
// Abstract base for labeled toggle controls (checkbox, radio, switch).
// Owns label/layout/event/state plumbing; subclasses provide indicator drawing.
// Version: 1.0.0
// Last Modified: 2026-05-07
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include <string>
#include <functional>

namespace UltraCanvas {

// ===== TOGGLE STATE =====
    enum class CheckedState {
        Unchecked,
        Checked,
        Indeterminate  // Only checkbox uses this; radio/switch clamp to Unchecked.
    };

// ===== SHARED LABEL/FOCUS/FONT VISUAL FIELDS =====
    struct LabeledToggleVisualStyle {
        // Text appearance
        Color textColor = Colors::TextDefault;
        Color textHoverColor = Colors::TextDefault;
        Color textDisabledColor = Colors::TextDisabled;

        // Layout
        int textSpacing = 6;  // Space between indicator and label

        // Text styling
        std::string fontFamily = "Arial";
        float fontSize = 12.0f;
        FontWeight fontWeight = FontWeight::Normal;

        // Focus ring
        bool hasFocusRing = true;
        Color focusRingColor = Color(0, 120, 215, 128);
        float focusRingWidth = 2.0f;
    };

// ===== ABSTRACT BASE =====
    class UltraCanvasLabeledToggleBase : public UltraCanvasUIElement {
    protected:
        // Core state
        std::string text;
        CheckedState checkState = CheckedState::Unchecked;

        // Layout
        bool layoutDirty = true;
        Rect2Df indicatorRect;  // Position+size of the visual indicator (box, circle, or track)
        Rect2Df textRect;
        Rect2Df totalBounds;

        // Helpers
        void CalculateLayout(IRenderContext* ctx);
        // Measures the natural content size (indicator + spacing + label). Measure
        // only — must not mutate this element's size (the block measure owns that).
        Size2Df MeasureContentSize(IRenderContext* ctx) const;
        void DrawLabel(IRenderContext* ctx);

        // Subclass hooks
        virtual void DrawIndicator(IRenderContext* ctx) = 0;
        virtual Size2Df GetIndicatorSize() const = 0;
        virtual void OnActivate() = 0;
        virtual const LabeledToggleVisualStyle& GetBaseVisualStyle() const = 0;
        virtual void DrawFocusRingShape(IRenderContext* ctx);  // Default: rectangle around indicator.

    public:
        UltraCanvasLabeledToggleBase(const std::string& identifier,
                                     float x, float y, float w, float h,
                                     const std::string& labelText);
        virtual ~UltraCanvasLabeledToggleBase() = default;

        // ===== STATE =====
        void SetChecked(bool checked);
        bool IsChecked() const { return checkState == CheckedState::Checked; }

        virtual void SetCheckState(CheckedState state);
        CheckedState GetCheckState() const { return checkState; }

        virtual void Toggle();

        // ===== TEXT/LAYOUT =====
        void SetText(const std::string& labelText) { text = labelText; layoutDirty = true; InvalidateLayout(); RequestRedraw(); }
        std::string GetText() const { return text; }

        // ===== LAYOUT (CSS Measure/Arrange) =====
        // Toggles have intrinsic size (indicator + spacing + label); we report it
        // as a content box via MeasureOwnContent (Button pattern) and position our
        // sub-rects in Arrange. For content sizing, leave size.width/height = Auto
        // (the default); an explicit size wins per normal CSS.
        Size2Df MeasureOwnContent(std::optional<float> definiteContentWidth,
                                  const CSSLayout::LayoutContext& ctx) override;
        void ComputeIntrinsicSizes(const CSSLayout::LayoutContext& ctx) override;
        void Arrange(const Rect2Df& finalRect, const CSSLayout::LayoutContext& ctx) override;

        // ===== RENDER/EVENT =====
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;

        // ===== CALLBACKS =====
        std::function<void(CheckedState oldState, CheckedState newState)> onStateChanged;
        std::function<void()> onChecked;
        std::function<void()> onUnchecked;
        std::function<void()> onIndeterminate;
    };

} // namespace UltraCanvas
