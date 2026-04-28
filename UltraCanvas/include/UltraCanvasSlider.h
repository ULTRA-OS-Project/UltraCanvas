// include/UltraCanvasSlider.h
// Interactive slider control with multiple styles, value display options, and dual-handle range support
// Version: 3.1.0
// Last Modified: 2026-04-28
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasCommonTypes.h"
#include <string>
#include <functional>
#include <cmath>
#include <memory>

namespace UltraCanvas {

// ===== SLIDER STYLE DEFINITIONS =====
    enum class SliderStyle {
        Horizontal,     // Classic horizontal bar
        Vertical,       // Classic vertical bar
        Circular,       // Circular/knob style
        Progress,       // Progress bar style
        Range,          // Range slider with two handles
    };

    enum class SliderValueDisplay {
        NoDisplay,         // No value display
        Number,           // Numeric value
        Percentage,       // Percentage display
        Tooltip,          // Show on hover
        AlwaysVisible     // Always visible (fixed X11 conflict)
    };

    enum class SliderOrientation {
        Horizontal,
        Vertical
    };

    enum class SliderState {
        Normal,
        Hovered,
        Pressed,
        Focused,
        Disabled
    };

    enum class RangeHandle {
        NoneRange,           // No handle selected
        Lower,          // Lower/left handle
        Upper,          // Upper/right handle
        Both            // Both handles (for special operations)
    };

    enum SliderHandleShape {
        Circle,
        Square,
        Triangle,
        Diamond
    };

// ===== SLIDER VISUAL STYLE =====
    struct SliderVisualStyle {
        // Track colors
        Color trackColor = Color(200, 200, 200);
        Color activeTrackColor = Color(0, 120, 215);
        Color disabledTrackColor = Color(180, 180, 180);
        Color rangeTrackColor = Color(0, 120, 215, 180);  // Color for range between handles

        // Handle colors
        Color handleColor = Colors::White;
        Color handleBorderColor = Color(100, 100, 100);
        Color handleHoverColor = Color(240, 240, 240);
        Color handlePressedColor = Color(200, 200, 200);
        Color handleDisabledColor = Color(220, 220, 220);

        // Text colors
        Color textColor = Colors::Black;
        Color disabledTextColor = Color(150, 150, 150);

        // Dimensions
        float trackHeight = 6.0f;
        float handleSize = 16.0f;
        float borderWidth = 1.0f;
        float cornerRadius = 3.0f;
        SliderHandleShape handleShape = SliderHandleShape::Circle;

        // Font
        FontStyle fontStyle;
    };

// ===== MAIN SLIDER COMPONENT =====
    class UltraCanvasSlider : public UltraCanvasUIElement {
    private:
        // ===== SLIDER PROPERTIES =====
        float minValue = 0.0f;
        float maxValue = 100.0f;
        float currentValue = 0.0f;
        float step = 1.0f;
        SliderStyle sliderStyle = SliderStyle::Horizontal;
        SliderValueDisplay valueDisplay = SliderValueDisplay::NoDisplay;
        SliderOrientation orientation = SliderOrientation::Horizontal;

        // ===== RANGE SLIDER PROPERTIES =====
        bool isRangeMode = false;
        float lowerValue = 0.0f;
        float upperValue = 100.0f;
        RangeHandle activeHandle = RangeHandle::NoneRange;
        RangeHandle hoveredHandle = RangeHandle::NoneRange;
        float handleCollisionMargin = 0.0f;  // Minimum distance between handles

        // ===== VISUAL STYLE =====
        SliderVisualStyle style;

        // ===== STATE MANAGEMENT =====
        SliderState currentState = SliderState::Normal;
        bool isDragging = false;
        bool showTooltip = false;
        Point2Di dragStartPos;
        float dragStartValue = 0.0f;

        // ===== TEXT FORMATTING =====
        std::string valueFormat = "%.1f";
        std::string customText = "";

    public:
        // ===== CONSTRUCTOR (REQUIRED PATTERN) =====
        UltraCanvasSlider(const std::string& identifier = "Slider",
                          long id = 0, long x = 0, long y = 0, long w = 200, long h = 30);

        // ===== VALUE MANAGEMENT =====
        void SetRange(float min, float max);
        void SetValue(float value);

        float GetValue() const { return currentValue; }
        float GetMinValue() const { return minValue; }
        float GetMaxValue() const { return maxValue; }
        float GetPercentage() const {
            if (maxValue == minValue) return 0.0f;
            return (currentValue - minValue) / (maxValue - minValue);
        }
        void SetPercentage(float percentage) {
            float value = minValue + percentage * (maxValue - minValue);
            SetValue(value);
        }

        void SetStep(float stepValue) { step = std::max(0.0f, stepValue); }
        float GetStep() const { return step; }

        // ===== RANGE MODE MANAGEMENT =====
        void SetRangeMode(bool enabled);

        bool IsRangeMode() const { return isRangeMode; }

        void SetLowerValue(float value);

        void SetUpperValue(float value);

        void SetRangeValues(float lower, float upper);

        float GetLowerValue() const { return lowerValue; }
        float GetUpperValue() const { return upperValue; }

        void SetHandleCollisionMargin(float margin) {
            handleCollisionMargin = std::max(0.0f, margin);
        }

        float GetHandleCollisionMargin() const { return handleCollisionMargin; }

        // ===== STYLE MANAGEMENT =====
        void SetSliderStyle(SliderStyle newStyle);

        SliderStyle GetSliderStyle() const { return sliderStyle; }

        void SetValueDisplay(SliderValueDisplay mode) { valueDisplay = mode; }
        SliderValueDisplay GetValueDisplay() const { return valueDisplay; }

        void SetOrientation(SliderOrientation orient);

        SliderOrientation GetOrientation() const { return orientation; }

        // ===== APPEARANCE CUSTOMIZATION =====
        void SetColors(const Color& track, const Color& activeTrack, const Color& handle);

        void SetTrackHeight(float height) {
            style.trackHeight = std::max(1.0f, height);
        }

        void SetHandleSize(float size) {
            style.handleSize = std::max(8.0f, size);
        }

        void SetHandleShape(SliderHandleShape shape) {
            style.handleShape = shape;
        }

        void SetValueFormat(const std::string& format) { valueFormat = format; }
        void SetCustomText(const std::string& text) { customText = text; }

        SliderVisualStyle& GetStyle() { return style; }
        const SliderVisualStyle& GetStyle() const { return style; }
        void SetStyle(const SliderVisualStyle& st) {
            style = st;
        }

        // ===== RENDERING (REQUIRED OVERRIDE) =====
        void Render(IRenderContext* ctx, const Rect2Di& dirtyRect) override;

        // ===== EVENT HANDLING (REQUIRED OVERRIDE) =====
        bool OnEvent(const UCEvent& event) override;

    private:
        // ===== HELPER METHODS =====
        float ClampAndStep(float value) const;

        RangeHandle GetHandleAt(const Point2Di& pos, const Rect2Di& bounds);

        // ===== STATE MANAGEMENT =====
        void UpdateSliderState();

        // ===== RENDERING METHODS =====
        void RenderLinearSlider(const Rect2Di& bounds, IRenderContext* ctx);
        void RenderRangeSlider(const Rect2Di& bounds, IRenderContext* ctx);
        void RenderCircularSlider(const Rect2Di& bounds, IRenderContext* ctx);
        void RenderProgressSlider(const Rect2Di& bounds, IRenderContext* ctx);
        void RenderHandle(const Point2Di& position, IRenderContext* ctx, bool highlighted = false);

        void RenderValueDisplay(const Rect2Di& bounds, IRenderContext* ctx);

        std::string FormatValue(float value) const;

        Rect2Di GetSliderInteriorRect(const Rect2Di& bounds, bool isVertical) const;
        Rect2Di GetTrackRect(const Rect2Di& bounds, bool isVertical) const;
        Rect2Di GetActiveTrackRect(const Rect2Di& trackRect, bool isVertical) const;
        Rect2Di GetRangeTrackRect(const Rect2Di& trackRect, bool isVertical) const;
        Point2Di GetHandlePosition(const Rect2Di& bounds, bool isVertical, float value) const;
        Color GetCurrentTrackColor() const;
        Color GetCurrentHandleColor() const;

        bool ShouldShowValueText() const {
            return (valueDisplay == SliderValueDisplay::AlwaysVisible) ||
                   (valueDisplay == SliderValueDisplay::Tooltip && showTooltip) ||
                   (valueDisplay == SliderValueDisplay::Number) ||
                   (valueDisplay == SliderValueDisplay::Percentage);
        }

        std::string GetDisplayText() const {
            return FormatValue(currentValue);
        }

        Point2Di CalculateTextPosition(const Rect2Di& bounds, const Point2Di& textSize, float value) const;

        // ===== EVENT HANDLERS =====
        bool HandleMouseDown(const UCEvent& event);

        bool HandleMouseMove(const UCEvent& event);

        bool HandleMouseUp(const UCEvent& event);

        bool HandleKeyDown(const UCEvent& event);

        void UpdateValueFromPosition(const Point2Di& pos);

    public:
        // ===== CALLBACK EVENTS =====
        std::function<void(float)> onValueChanged;
        std::function<void(float)> onValueChanging; // Called during drag
        std::function<void(const UCEvent&)> onPress;
        std::function<void(const UCEvent&)> onRelease;
        std::function<void(const UCEvent&)> onClick;

        // ===== RANGE MODE CALLBACKS =====
        std::function<void(float)> onLowerValueChanged;    // Lower handle value changed
        std::function<void(float)> onUpperValueChanged;    // Upper handle value changed
        std::function<void(float, float)> onRangeChanged;  // Range changed (both values)
    };

// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasSlider> CreateSlider(
            const std::string& identifier, long id, long x, long y, long width, long height) {
        return std::make_shared<UltraCanvasSlider>(identifier, id, x, y, width, height);
    }

    inline std::shared_ptr<UltraCanvasSlider> CreateSlider(
            const std::string& identifier, long x, long y, long width, long height) {
        return std::make_shared<UltraCanvasSlider>(identifier, 0, x, y, width, height);
    }

    inline std::shared_ptr<UltraCanvasSlider> CreateHorizontalSlider(
            const std::string& identifier, long id, long x, long y, long width, long height,
            float min = 0.0f, float max = 100.0f) {
        auto slider = std::make_shared<UltraCanvasSlider>(identifier, id, x, y, width, height);
        slider->SetSliderStyle(SliderStyle::Horizontal);
        slider->SetRange(min, max);
        return slider;
    }

    inline std::shared_ptr<UltraCanvasSlider> CreateVerticalSlider(
            const std::string& identifier, long id, long x, long y, long width, long height,
            float min = 0.0f, float max = 100.0f) {
        auto slider = std::make_shared<UltraCanvasSlider>(identifier, id, x, y, width, height);
        slider->SetSliderStyle(SliderStyle::Vertical);
        slider->SetRange(min, max);
        return slider;
    }

    inline std::shared_ptr<UltraCanvasSlider> CreateCircularSlider(
            const std::string& identifier, long id, long x, long y, long size,
            float min = 0.0f, float max = 100.0f) {
        auto slider = std::make_shared<UltraCanvasSlider>(identifier, id, x, y, size, size);
        slider->SetSliderStyle(SliderStyle::Circular);
        slider->SetRange(min, max);
        return slider;
    }

    inline std::shared_ptr<UltraCanvasSlider> CreateRangeSlider(
            const std::string& identifier, long x, long y, long width, long height,
            float min = 0.0f, float max = 100.0f, float lower = 25.0f, float upper = 75.0f) {
        auto slider = std::make_shared<UltraCanvasSlider>(identifier, 0, x, y, width, height);
        slider->SetSliderStyle(SliderStyle::Range);
        slider->SetRange(min, max);
        slider->SetRangeMode(true);
        slider->SetRangeValues(lower, upper);
        return slider;
    }

} // namespace UltraCanvas
