// include/UltraCanvasSlider.h
// Interactive slider control with multiple styles and value display options
// Version: 2.0.0
// Last Modified: 2025-08-17
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
        Rounded         // Rounded corners
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

// ===== SLIDER VISUAL STYLE =====
    struct SliderVisualStyle {
        // Track colors
        Color trackColor = Color(200, 200, 200);
        Color activeTrackColor = Color(0, 120, 215);
        Color disabledTrackColor = Color(180, 180, 180);

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

        // Font
        FontStyle fontStyle;
    };

// ===== MAIN SLIDER COMPONENT =====
    class UltraCanvasSlider : public UltraCanvasUIElement {
    private:
        // ===== STANDARD PROPERTIES (REQUIRED) =====
        StandardProperties properties;

        // ===== SLIDER PROPERTIES =====
        float minValue = 0.0f;
        float maxValue = 100.0f;
        float currentValue = 0.0f;
        float step = 1.0f;
        SliderStyle sliderStyle = SliderStyle::Horizontal;
        SliderValueDisplay valueDisplay = SliderValueDisplay::NoDisplay;
        SliderOrientation orientation = SliderOrientation::Horizontal;

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
                          long id = 0, long x = 0, long y = 0, long w = 200, long h = 30)
                : UltraCanvasUIElement(identifier, id, x, y, w, h) {

            // Initialize standard properties
            properties = StandardProperties(identifier, id, x, y, w, h);
            properties.MousePtr = MousePointer::Hand;
            properties.MouseCtrl = MouseControls::Object2D;

            UpdateSliderState();
        }

        // ===== VALUE MANAGEMENT =====
        void SetRange(float min, float max) {
            if (min <= max) {
                minValue = min;
                maxValue = max;
                SetValue(currentValue); // Clamp current value to new range
            }
        }

        void SetValue(float value) {
            float newValue = std::max(minValue, std::min(maxValue, value));

            // Apply step if specified
            if (step > 0) {
                float steps = std::round((newValue - minValue) / step);
                newValue = minValue + steps * step;
                newValue = std::max(minValue, std::min(maxValue, newValue));
            }

            if (std::abs(newValue - currentValue) > 0.001f) {
                float oldValue = currentValue;
                currentValue = newValue;

                // Trigger callbacks
                if (onValueChanging && isDragging) {
                    onValueChanging(currentValue);
                } else if (onValueChanged) {
                    onValueChanged(currentValue);
                }
                RequestRedraw();
            }
        }

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

        // ===== STYLE MANAGEMENT =====
        void SetSliderStyle(SliderStyle newStyle) {
            sliderStyle = newStyle;

            // Auto-adjust orientation for vertical style
            if (newStyle == SliderStyle::Vertical) {
                orientation = SliderOrientation::Vertical;
            } else if (newStyle == SliderStyle::Horizontal) {
                orientation = SliderOrientation::Horizontal;
            }
        }

        SliderStyle GetSliderStyle() const { return sliderStyle; }

        void SetValueDisplay(SliderValueDisplay mode) { valueDisplay = mode; }
        SliderValueDisplay GetValueDisplay() const { return valueDisplay; }

        void SetOrientation(SliderOrientation orient) {
            orientation = orient;
            // Auto-adjust style based on orientation
            if (orient == SliderOrientation::Vertical && sliderStyle == SliderStyle::Horizontal) {
                sliderStyle = SliderStyle::Vertical;
            } else if (orient == SliderOrientation::Horizontal && sliderStyle == SliderStyle::Vertical) {
                sliderStyle = SliderStyle::Horizontal;
            }
        }

        SliderOrientation GetOrientation() const { return orientation; }

        // ===== APPEARANCE CUSTOMIZATION =====
        void SetColors(const Color& track, const Color& activeTrack, const Color& handle) {
            style.trackColor = track;
            style.activeTrackColor = activeTrack;
            style.handleColor = handle;
        }

        void SetTrackHeight(float height) {
            style.trackHeight = std::max(1.0f, height);
        }

        void SetHandleSize(float size) {
            style.handleSize = std::max(8.0f, size);
        }

        void SetValueFormat(const std::string& format) { valueFormat = format; }
        void SetCustomText(const std::string& text) { customText = text; }

        SliderVisualStyle& GetStyle() { return style; }
        const SliderVisualStyle& GetStyle() const { return style; }

        // ===== RENDERING (REQUIRED OVERRIDE) =====
        void Render() override {
            IRenderContext* ctx = GetRenderContext();
            if (!IsVisible() || !ctx) return;
            ctx->PushState();

            UpdateSliderState();
            Rect2Di bounds = GetBounds();

            // ===== RENDER BASED ON STYLE =====
            switch (sliderStyle) {
                case SliderStyle::Horizontal:
                case SliderStyle::Vertical:
                    RenderLinearSlider(bounds, ctx);
                    break;

                case SliderStyle::Circular:
                    RenderCircularSlider(bounds, ctx);
                    break;

                case SliderStyle::Progress:
                    RenderProgressSlider(bounds, ctx);
                    break;

                case SliderStyle::Rounded:
                    RenderRoundedSlider(bounds, ctx);
                    break;
            }

            // ===== RENDER VALUE DISPLAY =====
            if (ShouldShowValueText()) {
                RenderValueDisplay(bounds, ctx);
            }
            ctx->PopState();
        }

        // ===== EVENT HANDLING (REQUIRED OVERRIDE) =====
        bool OnEvent(const UCEvent& event) override {
            switch (event.type) {
                case UCEventType::MouseDown:
                    return HandleMouseDown(event);

                case UCEventType::MouseMove:
                    return HandleMouseMove(event);

                case UCEventType::MouseUp:
                    return HandleMouseUp(event);

                case UCEventType::KeyDown:
                    return HandleKeyDown(event);

                case UCEventType::MouseEnter:
                    SetHovered(true);
                    showTooltip = (valueDisplay == SliderValueDisplay::Tooltip);
                    return true;

                case UCEventType::MouseLeave:
                    SetHovered(false);
                    showTooltip = false;
                    return true;

                default:
                    break;
            }

            return UltraCanvasUIElement::OnEvent(event);
        }

    private:
        // ===== STATE MANAGEMENT =====
        void UpdateSliderState() {
            if (!IsEnabled()) {
                currentState = SliderState::Disabled;
            } else if (isDragging) {
                currentState = SliderState::Pressed;
            } else if (IsFocused()) {
                currentState = SliderState::Focused;
            } else if (IsHovered()) {
                currentState = SliderState::Hovered;
            } else {
                currentState = SliderState::Normal;
            }
        }

        // ===== RENDERING METHODS =====
        void RenderLinearSlider(const Rect2Di& bounds, IRenderContext* ctx) {
            bool isVertical = (orientation == SliderOrientation::Vertical);

            // Calculate track rectangle
            Rect2Di trackRect = GetTrackRect(bounds, isVertical);

            // Draw track background
            ctx->PaintWithColor(GetCurrentTrackColor());
            ctx->FillRectangle(trackRect);

            // Draw track border
            ctx->PaintWithColor(style.handleBorderColor);
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawRectangle(trackRect);

            // Calculate and draw active track
            Rect2Di activeRect = GetActiveTrackRect(trackRect, isVertical);
            if ((isVertical && activeRect.height > 0) || (!isVertical && activeRect.width > 0)) {
                ctx->PaintWithColor(style.activeTrackColor);
                ctx->FillRectangle(activeRect);
            }

            // Draw handle
            Point2Di handlePos = GetHandlePosition(bounds, isVertical);
            RenderHandle(handlePos, ctx);
        }

        void RenderRoundedSlider(const Rect2Di& bounds, IRenderContext* ctx) {
            bool isVertical = (orientation == SliderOrientation::Vertical);

            // Calculate track rectangle
            Rect2Di trackRect = GetTrackRect(bounds, isVertical);

            // Draw track background
            ctx->PaintWithColor(GetCurrentTrackColor());
            ctx->FillRoundedRectangle(trackRect, style.cornerRadius);

            // Draw track border
            ctx->PaintWithColor(style.handleBorderColor);
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawRoundedRectangle(trackRect, style.cornerRadius);

            // Calculate and draw active track
            Rect2Di activeRect = GetActiveTrackRect(trackRect, isVertical);
            if ((isVertical && activeRect.height > 0) || (!isVertical && activeRect.width > 0)) {
                ctx->PaintWithColor(style.activeTrackColor);
                ctx->FillRoundedRectangle(activeRect, style.cornerRadius);
            }

            // Draw handle
            Point2Di handlePos = GetHandlePosition(bounds, isVertical);
            RenderRoundedHandle(handlePos, ctx);
        }

        void RenderCircularSlider(const Rect2Di& bounds, IRenderContext* ctx) {
            Point2Di center(bounds.x + bounds.width / 2, bounds.y + bounds.height / 2);
            float radius = std::min(bounds.width, bounds.height) / 2 - style.handleSize / 2 - 2;

            // Draw track circle
            ctx->PaintWithColor(GetCurrentTrackColor());
            ctx->SetStrokeWidth(style.trackHeight);
            ctx->DrawCircle(center, radius);

            // Draw active arc
            float percentage = GetPercentage();
            float startAngle = -M_PI / 2; // Start from top
            float endAngle = startAngle + percentage * 2 * M_PI;

            if (percentage > 0) {
                ctx->PaintWithColor(style.activeTrackColor);
                ctx->SetStrokeWidth(style.trackHeight);
                ctx->DrawArc(center.x, center.y, radius, startAngle, endAngle);
            }

            // Draw handle
            float handleAngle = startAngle + percentage * 2 * M_PI;
            Point2Di handlePos(
                    center.x + std::cos(handleAngle) * radius,
                    center.y + std::sin(handleAngle) * radius
            );
            RenderHandle(handlePos, ctx);
        }

        void RenderProgressSlider(const Rect2Di& bounds, IRenderContext* ctx) {
            // Similar to linear but without handle
            bool isVertical = (orientation == SliderOrientation::Vertical);

            // Draw background
            ctx->PaintWithColor(GetCurrentTrackColor());
            ctx->FillRectangle(bounds);

            // Draw progress
            Rect2Di progressRect = GetActiveTrackRect(bounds, isVertical);
            if ((isVertical && progressRect.height > 0) || (!isVertical && progressRect.width > 0)) {
                ctx->PaintWithColor(style.activeTrackColor);
                ctx->FillRectangle(progressRect);
            }

            // Draw border
            ctx->PaintWithColor(style.handleBorderColor);
            ctx->SetStrokeWidth(style.borderWidth);
            ctx->DrawRectangle(bounds);
        }

        void RenderHandle(const Point2Di& position, IRenderContext* ctx) {
            float handleRadius = style.handleSize / 2;
            Rect2Di handleRect(
                    position.x - handleRadius,
                    position.y - handleRadius,
                    style.handleSize,
                    style.handleSize
            );

            // Fill handle
            ctx->PaintWithColor(GetCurrentHandleColor());
            ctx->FillEllipse(handleRect.x, handleRect.y, handleRect.width, handleRect.height);

            // Draw handle border
            ctx->PaintWithColor(style.handleBorderColor);
            ctx->SetStrokeWidth(style.borderWidth);
            ctx->DrawEllipse(handleRect.x, handleRect.y, handleRect.width, handleRect.height);
        }

        void RenderRoundedHandle(const Point2Di& position, IRenderContext* ctx) {
            float handleRadius = style.handleSize / 2;
            Rect2Di handleRect(
                    position.x - handleRadius,
                    position.y - handleRadius,
                    style.handleSize,
                    style.handleSize
            );

            // Fill handle
            ctx->PaintWithColor(GetCurrentHandleColor());
            ctx->FillRoundedRectangle(handleRect, handleRadius);

            // Draw handle border
            ctx->PaintWithColor(style.handleBorderColor);
            ctx->SetStrokeWidth(style.borderWidth);
            ctx->DrawRoundedRectangle(handleRect, handleRadius);
        }

        void RenderValueDisplay(const Rect2Di& bounds, IRenderContext* ctx) {
            std::string text = GetDisplayText();
            if (text.empty()) return;

            ctx->PaintWithColor(IsEnabled() ? style.textColor : style.disabledTextColor);
            ctx->SetFontStyle(style.fontStyle);

            Point2Di textSize = ctx->MeasureText(text);
            Point2Di textPos = CalculateTextPosition(bounds, textSize);

            // Draw background for tooltip
            if (valueDisplay == SliderValueDisplay::Tooltip && showTooltip) {
                Rect2Di tooltipBg(textPos.x - 4, textPos.y - textSize.y - 2,
                                 textSize.x + 8, textSize.y + 4);
                ctx->PaintWithColor(Color(255, 255, 200, 240));
                ctx->FillRoundedRectangle(tooltipBg, 3.0f);
                ctx->PaintWithColor(Color(180, 180, 180));
                ctx->SetStrokeWidth(1.0f);
                ctx->DrawRoundedRectangle(tooltipBg, 3.0f);
            }

            ctx->DrawText(text, textPos);
        }

        // ===== HELPER METHODS =====
        Rect2Di GetTrackRect(const Rect2Di& bounds, bool isVertical) const {
            if (isVertical) {
                float trackX = bounds.x + (bounds.width - style.trackHeight) / 2;
                return Rect2Di(trackX, bounds.y + style.handleSize / 2,
                               style.trackHeight, bounds.height - style.handleSize);
            } else {
                float trackY = bounds.y + (bounds.height - style.trackHeight) / 2;
                return Rect2Di(bounds.x + style.handleSize / 2, trackY,
                              bounds.width - style.handleSize, style.trackHeight);
            }
        }

        Rect2Di GetActiveTrackRect(const Rect2Di& trackRect, bool isVertical) const {
            float percentage = GetPercentage();

            if (isVertical) {
                float activeHeight = trackRect.height * percentage;
                return Rect2Di(trackRect.x, trackRect.y + trackRect.height - activeHeight,
                               trackRect.width, activeHeight);
            } else {
                float activeWidth = trackRect.width * percentage;
                return Rect2Di(trackRect.x, trackRect.y, activeWidth, trackRect.height);
            }
        }

        Point2Di GetHandlePosition(const Rect2Di& bounds, bool isVertical) const {
            float percentage = GetPercentage();

            if (isVertical) {
                float handleY = bounds.y + bounds.height - style.handleSize / 2 -
                                percentage * (bounds.height - style.handleSize);
                return Point2Di(bounds.x + bounds.width / 2, handleY);
            } else {
                float handleX = bounds.x + style.handleSize / 2 +
                                percentage * (bounds.width - style.handleSize);
                return Point2Di(handleX, bounds.y + bounds.height / 2);
            }
        }

        Color GetCurrentTrackColor() const {
            return IsEnabled() ? style.trackColor : style.disabledTrackColor;
        }

        Color GetCurrentHandleColor() const {
            if (!IsEnabled()) return style.handleDisabledColor;

            switch (currentState) {
                case SliderState::Pressed: return style.handlePressedColor;
                case SliderState::Hovered: return style.handleHoverColor;
                default: return style.handleColor;
            }
        }

        bool ShouldShowValueText() const {
            return (valueDisplay == SliderValueDisplay::AlwaysVisible) ||
                   (valueDisplay == SliderValueDisplay::Tooltip && showTooltip) ||
                   (valueDisplay == SliderValueDisplay::Number) ||
                   (valueDisplay == SliderValueDisplay::Percentage);
        }

        std::string GetDisplayText() const {
            if (!customText.empty()) {
                return customText;
            }

            switch (valueDisplay) {
                case SliderValueDisplay::Percentage: {
                    float percentage = GetPercentage() * 100.0f;
                    return std::to_string(static_cast<int>(percentage)) + "%";
                }

                case SliderValueDisplay::Number:
                case SliderValueDisplay::AlwaysVisible:
                case SliderValueDisplay::Tooltip: {
                    char buffer[64];
                    std::snprintf(buffer, sizeof(buffer), valueFormat.c_str(), currentValue);
                    return std::string(buffer);
                }

                default:
                    return "";
            }
        }

        Point2Di CalculateTextPosition(const Rect2Di& bounds, const Point2Di& textSize) const {
            if (valueDisplay == SliderValueDisplay::Tooltip) {
                Point2Di handlePos = GetHandlePosition(bounds, orientation == SliderOrientation::Vertical);
                return Point2Di(handlePos.x - textSize.x / 2, handlePos.y - style.handleSize / 2 - 8);
            }

            if (orientation == SliderOrientation::Vertical) {
                return Point2Di(bounds.x + bounds.width + 8, bounds.y + bounds.height / 2 + textSize.y / 2);
            } else {
                return Point2Di(bounds.x + bounds.width / 2 - textSize.x / 2, bounds.y - 8);
            }
        }

        // ===== EVENT HANDLERS =====
        bool HandleMouseDown(const UCEvent& event) {
            Point2Di mousePos(event.x, event.y);

            if (!Contains(mousePos)) return false;
            if (!IsEnabled()) return false;

            isDragging = true;
            dragStartPos = mousePos;
            dragStartValue = currentValue;

            SetFocus(true);
            UpdateValueFromPosition(mousePos);

            if (onPress) onPress(event);

            return true;
        }

        bool HandleMouseMove(const UCEvent& event) {
            Point2Di mousePos(event.x, event.y);

            if (isDragging) {
                UpdateValueFromPosition(mousePos);
                return true;
            }

            // Update tooltip visibility
            if (Contains(mousePos)) {
                showTooltip = (valueDisplay == SliderValueDisplay::Tooltip);
            } else {
                showTooltip = false;
            }

            return false;
        }

        bool HandleMouseUp(const UCEvent& event) {
            if (isDragging) {
                isDragging = false;

                if (onRelease) onRelease(event);
                if (onClick) onClick(event);

                return true;
            }

            return false;
        }

        bool HandleKeyDown(const UCEvent& event) {
            if (!IsFocused() || !IsEnabled()) return false;

            float increment = step > 0 ? step : (maxValue - minValue) / 100.0f;

            switch (event.virtualKey) {
                case UCKeys::Left:
                case UCKeys::Down:
                    SetValue(currentValue - increment);
                    return true;

                case UCKeys::Right:
                case UCKeys::Up:
                    SetValue(currentValue + increment);
                    return true;

                case UCKeys::Home:
                    SetValue(minValue);
                    return true;

                case UCKeys::End:
                    SetValue(maxValue);
                    return true;

                case UCKeys::PageUp:
                    SetValue(currentValue + increment * 10);
                    return true;

                case UCKeys::PageDown:
                    SetValue(currentValue - increment * 10);
                    return true;
            }

            return false;
        }

        void UpdateValueFromPosition(const Point2Di& pos) {
            Rect2Di bounds = GetBounds();
            float newValue;

            if (orientation == SliderOrientation::Vertical) {
                float ratio = 1.0f - (pos.y - bounds.y - style.handleSize / 2) /
                                     (bounds.height - style.handleSize);
                newValue = minValue + ratio * (maxValue - minValue);
            } else {
                float ratio = (pos.x - bounds.x - style.handleSize / 2) /
                              (bounds.width - style.handleSize);
                newValue = minValue + ratio * (maxValue - minValue);
            }

            SetValue(newValue);
        }

    public:
        // ===== CALLBACK EVENTS =====
        std::function<void(float)> onValueChanged;
        std::function<void(float)> onValueChanging; // Called during drag
        std::function<void(const UCEvent&)> onPress;
        std::function<void(const UCEvent&)> onRelease;
        std::function<void(const UCEvent&)> onClick;
    };

// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasSlider> CreateSlider(
            const std::string& identifier, long id, long x, long y, long width, long height) {
        return std::make_shared<UltraCanvasSlider>(identifier, id, x, y, width, height);
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

    inline std::shared_ptr<UltraCanvasSlider> CreateRoundedSlider(
            const std::string& identifier, long id, long x, long y, long width, long height,
            float min = 0.0f, float max = 100.0f) {
        auto slider = std::make_shared<UltraCanvasSlider>(identifier, id, x, y, width, height);
        slider->SetSliderStyle(SliderStyle::Rounded);
        slider->SetRange(min, max);
        return slider;
    }

// ===== CONVENIENCE FUNCTIONS =====
    inline void SetSliderValue(UltraCanvasSlider* slider, float value) {
        if (slider) slider->SetValue(value);
    }

    inline float GetSliderValue(const UltraCanvasSlider* slider) {
        return slider ? slider->GetValue() : 0.0f;
    }

    inline void SetSliderRange(UltraCanvasSlider* slider, float min, float max) {
        if (slider) slider->SetRange(min, max);
    }

} // namespace UltraCanvas