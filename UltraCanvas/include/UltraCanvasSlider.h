// include/UltraCanvasSlider.h
// Interactive slider control with multiple styles, value display options, and dual-handle range support
// Version: 3.0.0
// Last Modified: 2025-11-16
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

    enum class RangeHandle {
        NoneRange,           // No handle selected
        Lower,          // Lower/left handle
        Upper,          // Upper/right handle
        Both            // Both handles (for special operations)
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
                          long id = 0, long x = 0, long y = 0, long w = 200, long h = 30)
                : UltraCanvasUIElement(identifier, id, x, y, w, h) {

            // Initialize standard properties
            mousePtr = MousePointer::Hand;

            // Initialize range values
            lowerValue = minValue;
            upperValue = maxValue;

            UpdateSliderState();
        }

        // ===== VALUE MANAGEMENT =====
        void SetRange(float min, float max) {
            if (min <= max) {
                minValue = min;
                maxValue = max;
                SetValue(currentValue); // Clamp current value to new range

                // Also clamp range values if in range mode
                if (isRangeMode) {
                    SetLowerValue(lowerValue);
                    SetUpperValue(upperValue);
                }
            }
        }

        void SetValue(float value) {
            if (isRangeMode) {
                // In range mode, SetValue sets both handles to same position (degenerate range)
                float clampedValue = ClampAndStep(value);
                SetRangeValues(clampedValue, clampedValue);
                return;
            }

            float newValue = ClampAndStep(value);

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

        // ===== RANGE MODE MANAGEMENT =====
        void SetRangeMode(bool enabled) {
            if (isRangeMode == enabled) return;

            isRangeMode = enabled;

            if (enabled) {
                // Initialize range values from current single value
                float midPoint = currentValue;
                float spread = (maxValue - minValue) * 0.2f; // 20% spread
                lowerValue = std::max(minValue, midPoint - spread / 2);
                upperValue = std::min(maxValue, midPoint + spread / 2);

                // Ensure values are clamped and stepped
                lowerValue = ClampAndStep(lowerValue);
                upperValue = ClampAndStep(upperValue);
            } else {
                // Set current value to midpoint of range
                currentValue = (lowerValue + upperValue) / 2.0f;
                currentValue = ClampAndStep(currentValue);
            }

            RequestRedraw();
        }

        bool IsRangeMode() const { return isRangeMode; }

        void SetLowerValue(float value) {
            if (!isRangeMode) return;

            float newValue = ClampAndStep(value);

            // Ensure lower value doesn't exceed upper value minus margin
            float maxAllowed = upperValue - handleCollisionMargin;
            newValue = std::min(newValue, maxAllowed);

            if (std::abs(newValue - lowerValue) > 0.001f) {
                float oldValue = lowerValue;
                lowerValue = newValue;

                // Trigger callbacks
                if (onLowerValueChanged) {
                    onLowerValueChanged(lowerValue);
                }
                if (onRangeChanged) {
                    onRangeChanged(lowerValue, upperValue);
                }
                RequestRedraw();
            }
        }

        void SetUpperValue(float value) {
            if (!isRangeMode) return;

            float newValue = ClampAndStep(value);

            // Ensure upper value doesn't go below lower value plus margin
            float minAllowed = lowerValue + handleCollisionMargin;
            newValue = std::max(newValue, minAllowed);

            if (std::abs(newValue - upperValue) > 0.001f) {
                float oldValue = upperValue;
                upperValue = newValue;

                // Trigger callbacks
                if (onUpperValueChanged) {
                    onUpperValueChanged(upperValue);
                }
                if (onRangeChanged) {
                    onRangeChanged(lowerValue, upperValue);
                }
                RequestRedraw();
            }
        }

        void SetRangeValues(float lower, float upper) {
            if (!isRangeMode) return;

            // Ensure proper ordering
            if (lower > upper) {
                std::swap(lower, upper);
            }

            float oldLower = lowerValue;
            float oldUpper = upperValue;

            lowerValue = ClampAndStep(lower);
            upperValue = ClampAndStep(upper);

            // Enforce minimum separation
            if (upperValue - lowerValue < handleCollisionMargin) {
                float midPoint = (lowerValue + upperValue) / 2.0f;
                lowerValue = midPoint - handleCollisionMargin / 2.0f;
                upperValue = midPoint + handleCollisionMargin / 2.0f;

                // Re-clamp after adjustment
                lowerValue = std::max(minValue, lowerValue);
                upperValue = std::min(maxValue, upperValue);
            }

            bool changed = (std::abs(oldLower - lowerValue) > 0.001f) ||
                           (std::abs(oldUpper - upperValue) > 0.001f);

            if (changed) {
                if (onLowerValueChanged && std::abs(oldLower - lowerValue) > 0.001f) {
                    onLowerValueChanged(lowerValue);
                }
                if (onUpperValueChanged && std::abs(oldUpper - upperValue) > 0.001f) {
                    onUpperValueChanged(upperValue);
                }
                if (onRangeChanged) {
                    onRangeChanged(lowerValue, upperValue);
                }
                RequestRedraw();
            }
        }

        float GetLowerValue() const { return lowerValue; }
        float GetUpperValue() const { return upperValue; }

        void SetHandleCollisionMargin(float margin) {
            handleCollisionMargin = std::max(0.0f, margin);
        }

        float GetHandleCollisionMargin() const { return handleCollisionMargin; }

        // ===== STYLE MANAGEMENT =====
        void SetSliderStyle(SliderStyle newStyle) {
            sliderStyle = newStyle;

            // Auto-enable range mode for Range style
            if (newStyle == SliderStyle::Range && !isRangeMode) {
                SetRangeMode(true);
            }

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
        void Render(IRenderContext* ctx) override {
            if (!IsVisible()) return;
            ctx->PushState();

            UpdateSliderState();
            Rect2Di bounds = GetBounds();

            // ===== RENDER BASED ON STYLE =====
            if (isRangeMode || sliderStyle == SliderStyle::Range) {
                RenderRangeSlider(bounds, ctx);
            } else {
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

                    default:
                        RenderLinearSlider(bounds, ctx);
                        break;
                }
            }

            // ===== RENDER VALUE DISPLAY =====
            if (ShouldShowValueText()) {
                RenderValueDisplay(bounds, ctx);
            }
            ctx->PopState();
        }

        // ===== EVENT HANDLING (REQUIRED OVERRIDE) =====
        bool OnEvent(const UCEvent& event) override {
            if (!IsVisible() || IsDisabled()) return false;

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
                    hoveredHandle = RangeHandle::NoneRange;
                    return true;

                default:
                    break;
            }

            return UltraCanvasUIElement::OnEvent(event);
        }

    private:
        // ===== HELPER METHODS =====
        float ClampAndStep(float value) const {
            float newValue = std::max(minValue, std::min(maxValue, value));

            // Apply step if specified
            if (step > 0) {
                float steps = std::round((newValue - minValue) / step);
                newValue = minValue + steps * step;
                newValue = std::max(minValue, std::min(maxValue, newValue));
            }

            return newValue;
        }

        RangeHandle GetHandleAt(const Point2Di& pos, const Rect2Di& bounds) {
            if (!isRangeMode) {
                // Single handle mode
                Point2Di handlePos = GetHandlePosition(bounds, orientation == SliderOrientation::Vertical, currentValue);
                float distance = std::sqrt(std::pow(pos.x - handlePos.x, 2) + std::pow(pos.y - handlePos.y, 2));
                return (distance <= style.handleSize) ? RangeHandle::Lower : RangeHandle::NoneRange;
            }

            // Range mode - check both handles
            Point2Di lowerHandlePos = GetHandlePosition(bounds, orientation == SliderOrientation::Vertical, lowerValue);
            Point2Di upperHandlePos = GetHandlePosition(bounds, orientation == SliderOrientation::Vertical, upperValue);

            float lowerDist = std::sqrt(std::pow(pos.x - lowerHandlePos.x, 2) + std::pow(pos.y - lowerHandlePos.y, 2));
            float upperDist = std::sqrt(std::pow(pos.x - upperHandlePos.x, 2) + std::pow(pos.y - upperHandlePos.y, 2));

            // Return closest handle if within click range
            if (lowerDist <= style.handleSize && upperDist <= style.handleSize) {
                // Both handles are close, choose nearest
                return (lowerDist < upperDist) ? RangeHandle::Lower : RangeHandle::Upper;
            } else if (lowerDist <= style.handleSize) {
                return RangeHandle::Lower;
            } else if (upperDist <= style.handleSize) {
                return RangeHandle::Upper;
            }

            return RangeHandle::NoneRange;
        }

        // ===== STATE MANAGEMENT =====
        void UpdateSliderState() {
            if (IsDisabled()) {
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
            ctx->DrawFilledRectangle(trackRect, GetCurrentTrackColor(), 1.0, style.handleBorderColor);

            // Calculate and draw active track
            Rect2Di activeRect = GetActiveTrackRect(trackRect, isVertical);
            if ((isVertical && activeRect.height > 0) || (!isVertical && activeRect.width > 0)) {
                ctx->SetFillPaint(style.activeTrackColor);
                ctx->FillRectangle(activeRect);
            }

            // Draw handle
            Point2Di handlePos = GetHandlePosition(bounds, isVertical, currentValue);
            RenderHandle(handlePos, ctx);
        }

        void RenderRangeSlider(const Rect2Di& bounds, IRenderContext* ctx) {
            bool isVertical = (orientation == SliderOrientation::Vertical);

            // Calculate track rectangle
            Rect2Di trackRect = GetTrackRect(bounds, isVertical);

            // Draw full track background
            ctx->DrawFilledRectangle(trackRect, GetCurrentTrackColor(), 1.0, style.handleBorderColor);

            // Draw range region (between handles)
            Rect2Di rangeRect = GetRangeTrackRect(trackRect, isVertical);
            if ((isVertical && rangeRect.height > 0) || (!isVertical && rangeRect.width > 0)) {
                ctx->SetFillPaint(style.rangeTrackColor);
                ctx->FillRectangle(rangeRect);
            }

            // Draw both handles
            Point2Di lowerHandlePos = GetHandlePosition(bounds, isVertical, lowerValue);
            Point2Di upperHandlePos = GetHandlePosition(bounds, isVertical, upperValue);

            // Draw lower handle
            RenderHandle(lowerHandlePos, ctx, (activeHandle == RangeHandle::Lower) || (hoveredHandle == RangeHandle::Lower));

            // Draw upper handle
            RenderHandle(upperHandlePos, ctx, (activeHandle == RangeHandle::Upper) || (hoveredHandle == RangeHandle::Upper));
        }

        void RenderRoundedSlider(const Rect2Di& bounds, IRenderContext* ctx) {
            // Similar to linear but with rounded corners
            bool isVertical = (orientation == SliderOrientation::Vertical);
            Rect2Di trackRect = GetTrackRect(bounds, isVertical);

            // Draw track with rounded corners
            ctx->DrawFilledRectangle(trackRect, GetCurrentTrackColor(), 1.0, style.handleBorderColor, style.cornerRadius);

            // Draw active track with rounded corners
            Rect2Di activeRect = GetActiveTrackRect(trackRect, isVertical);
            if ((isVertical && activeRect.height > 0) || (!isVertical && activeRect.width > 0)) {
                ctx->SetFillPaint(style.activeTrackColor);
                ctx->FillRoundedRectangle(activeRect, style.cornerRadius);
            }

            // Draw handle
            Point2Di handlePos = GetHandlePosition(bounds, isVertical, currentValue);
            RenderRoundedHandle(handlePos, ctx);
        }

        void RenderCircularSlider(const Rect2Di& bounds, IRenderContext* ctx) {
            // Circular/knob style slider
            int centerX = bounds.x + bounds.width / 2;
            int centerY = bounds.y + bounds.height / 2;
            float radius = std::min(bounds.width, bounds.height) / 2.0f - 10.0f;

            // Draw outer circle (track)
            ctx->SetStrokePaint(style.trackColor);
            ctx->SetStrokeWidth(style.trackHeight);
            ctx->DrawCircle(Point2Di(centerX, centerY), radius);

            // Draw active arc
            float percentage = GetPercentage();
            float startAngle = -90.0f; // Start from top
            float sweepAngle = percentage * 360.0f;

            ctx->SetStrokePaint(style.activeTrackColor);
            ctx->SetStrokeWidth(style.trackHeight);
            // Note: DrawArc would be needed here - simplified for now

            // Draw handle at arc end
            float angle = (startAngle + sweepAngle) * 3.14159f / 180.0f;
            Point2Di handlePos(
                    centerX + radius * std::cos(angle),
                    centerY + radius * std::sin(angle)
            );
            RenderHandle(handlePos, ctx);
        }

        void RenderProgressSlider(const Rect2Di& bounds, IRenderContext* ctx) {
            // Similar to linear but without handle
            bool isVertical = (orientation == SliderOrientation::Vertical);

            // Draw background
            ctx->SetFillPaint(GetCurrentTrackColor());
            ctx->FillRectangle(bounds);

            // Draw progress
            Rect2Di progressRect = GetActiveTrackRect(bounds, isVertical);
            if ((isVertical && progressRect.height > 0) || (!isVertical && progressRect.width > 0)) {
                ctx->SetFillPaint(style.activeTrackColor);
                ctx->FillRectangle(progressRect);
            }

            // Draw border
            ctx->SetStrokePaint(style.handleBorderColor);
            ctx->SetStrokeWidth(style.borderWidth);
            ctx->DrawRectangle(bounds);
        }

        void RenderHandle(const Point2Di& position, IRenderContext* ctx, bool highlighted = false) {
            float handleRadius = style.handleSize / 2;
            Rect2Di handleRect(
                    position.x - handleRadius,
                    position.y - handleRadius,
                    style.handleSize,
                    style.handleSize
            );

            Color handleColor = highlighted ? style.handleHoverColor : GetCurrentHandleColor();

            // Fill handle
            ctx->DrawFilledRectangle(handleRect, handleColor, style.borderWidth, style.handleBorderColor, handleRadius);
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
            ctx->SetFillPaint(GetCurrentHandleColor());
            ctx->FillRoundedRectangle(handleRect, handleRadius);

            // Draw handle border
            ctx->SetStrokePaint(style.handleBorderColor);
            ctx->SetStrokeWidth(style.borderWidth);
            ctx->DrawRoundedRectangle(handleRect, handleRadius);
        }

        void RenderValueDisplay(const Rect2Di& bounds, IRenderContext* ctx) {
            ctx->SetFontStyle(style.fontStyle);
            if (!isRangeMode) {
                // Single value display
                std::string text = GetDisplayText();
                if (text.empty()) return;

                ctx->SetTextPaint(IsDisabled() ? style.disabledTextColor : style.textColor);
                Point2Di textSize = ctx->GetTextDimension(text);
                Point2Di textPos = CalculateTextPosition(bounds, textSize);

                // Draw tooltip background if needed
                if (valueDisplay == SliderValueDisplay::Tooltip && showTooltip) {
                    Rect2Di tooltipRect(textPos.x - 4, textPos.y - textSize.y - 4, textSize.x + 8, textSize.y + 8);
                    ctx->DrawFilledRectangle(tooltipRect, Color(255, 255, 220, 230), 1.0, Color(128, 128, 128));
                }

                ctx->DrawText(text, textPos.x, textPos.y);
            } else {
                // Range mode - display both values
                std::string lowerText = FormatValue(lowerValue);
                std::string upperText = FormatValue(upperValue);

                ctx->SetTextPaint(IsDisabled() ? style.disabledTextColor : style.textColor);

                bool isVertical = (orientation == SliderOrientation::Vertical);
                Point2Di lowerHandlePos = GetHandlePosition(bounds, isVertical, lowerValue);
                Point2Di upperHandlePos = GetHandlePosition(bounds, isVertical, upperValue);

                // Draw lower value
                ctx->SetFontStyle(style.fontStyle);
                Point2Di lowerTextSize = ctx->GetTextDimension(lowerText);
                Point2Di lowerTextPos;
                if (isVertical) {
                    lowerTextPos = Point2Di(bounds.x + bounds.width + 8, lowerHandlePos.y - lowerTextSize.y / 2);
                } else {
                    lowerTextPos = Point2Di(lowerHandlePos.x - lowerTextSize.x / 2, bounds.y - lowerTextSize.y / 2 - 4);
                }
                ctx->DrawText(lowerText, lowerTextPos.x, lowerTextPos.y);

                // Draw upper value
                Point2Di upperTextSize = ctx->GetTextDimension(upperText);
                Point2Di upperTextPos;
                if (isVertical) {
                    upperTextPos = Point2Di(bounds.x + bounds.width + 8, upperHandlePos.y - upperTextSize.y / 2);
                } else {
                    upperTextPos = Point2Di(upperHandlePos.x - upperTextSize.x / 2, bounds.y - upperTextSize.y / 2 - 4);
                }
                ctx->DrawText(upperText, upperTextPos.x, upperTextPos.y);
            }
        }

        std::string FormatValue(float value) const {
            if (!customText.empty()) {
                return customText;
            }

            switch (valueDisplay) {
                case SliderValueDisplay::Percentage: {
                    float percentage = ((value - minValue) / (maxValue - minValue)) * 100.0f;
                    return std::to_string(static_cast<int>(percentage)) + "%";
                }

                case SliderValueDisplay::Number:
                case SliderValueDisplay::AlwaysVisible:
                case SliderValueDisplay::Tooltip: {
                    char buffer[64];
                    std::snprintf(buffer, sizeof(buffer), valueFormat.c_str(), value);
                    return std::string(buffer);
                }

                default:
                    return "";
            }
        }

        Rect2Di GetTrackRect(const Rect2Di& bounds, bool isVertical) const {
            if (isVertical) {
                int trackX = bounds.x + (bounds.width - style.trackHeight) / 2;
                return Rect2Di(trackX, bounds.y + style.handleSize / 2,
                               style.trackHeight, bounds.height - style.handleSize);
            } else {
                int trackY = bounds.y + (bounds.height - style.trackHeight) / 2;
                return Rect2Di(bounds.x + style.handleSize / 2, trackY,
                               bounds.width - style.handleSize, style.trackHeight);
            }
        }

        Rect2Di GetActiveTrackRect(const Rect2Di& trackRect, bool isVertical) const {
            float percentage = GetPercentage();

            if (isVertical) {
                int activeHeight = trackRect.height * (1.0f - percentage);
                return Rect2Di(trackRect.x, trackRect.y + activeHeight,
                               trackRect.width, trackRect.height - activeHeight);
            } else {
                int activeWidth = trackRect.width * percentage;
                return Rect2Di(trackRect.x, trackRect.y, activeWidth, trackRect.height);
            }
        }

        Rect2Di GetRangeTrackRect(const Rect2Di& trackRect, bool isVertical) const {
            if (!isRangeMode) return Rect2Di(0, 0, 0, 0);

            float lowerPercentage = (lowerValue - minValue) / (maxValue - minValue);
            float upperPercentage = (upperValue - minValue) / (maxValue - minValue);

            if (isVertical) {
                int lowerY = trackRect.y + trackRect.height * (1.0f - lowerPercentage);
                int upperY = trackRect.y + trackRect.height * (1.0f - upperPercentage);
                int rangeHeight = lowerY - upperY;
                return Rect2Di(trackRect.x, upperY, trackRect.width, rangeHeight);
            } else {
                int lowerX = trackRect.x + trackRect.width * lowerPercentage;
                int upperX = trackRect.x + trackRect.width * upperPercentage;
                int rangeWidth = upperX - lowerX;
                return Rect2Di(lowerX, trackRect.y, rangeWidth, trackRect.height);
            }
        }

        Point2Di GetHandlePosition(const Rect2Di& bounds, bool isVertical, float value) const {
            float percentage = (value - minValue) / (maxValue - minValue);

            if (isVertical) {
                int y = bounds.y + bounds.height - (bounds.height - style.handleSize) * percentage - style.handleSize / 2;
                return Point2Di(bounds.x + bounds.width / 2, y);
            } else {
                int x = bounds.x + (bounds.width - style.handleSize) * percentage + style.handleSize / 2;
                return Point2Di(x, bounds.y + bounds.height / 2);
            }
        }

        Color GetCurrentTrackColor() const {
            return IsDisabled() ? style.disabledTrackColor : style.trackColor;
        }

        Color GetCurrentHandleColor() const {
            switch(GetPrimaryState()) {
                case ElementState::Disabled: return style.handleDisabledColor;
                case ElementState::Pressed: return style.handlePressedColor;
                case ElementState::Hovered: return style.handleHoverColor;
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
            return FormatValue(currentValue);
        }

        Point2Di CalculateTextPosition(const Rect2Di& bounds, const Point2Di& textSize) const {
            if (valueDisplay == SliderValueDisplay::Tooltip) {
                Point2Di handlePos = GetHandlePosition(bounds, orientation == SliderOrientation::Vertical, currentValue);
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

            Rect2Di bounds = GetBounds();

            if (isRangeMode) {
                // Determine which handle was clicked
                activeHandle = GetHandleAt(mousePos, bounds);

                if (activeHandle == RangeHandle::NoneRange) {
                    // Clicked on track - move nearest handle
                    Point2Di lowerHandlePos = GetHandlePosition(bounds, orientation == SliderOrientation::Vertical, lowerValue);
                    Point2Di upperHandlePos = GetHandlePosition(bounds, orientation == SliderOrientation::Vertical, upperValue);

                    float lowerDist = std::abs((orientation == SliderOrientation::Vertical) ?
                                               (mousePos.y - lowerHandlePos.y) :
                                               (mousePos.x - lowerHandlePos.x));
                    float upperDist = std::abs((orientation == SliderOrientation::Vertical) ?
                                               (mousePos.y - upperHandlePos.y) :
                                               (mousePos.x - upperHandlePos.x));

                    activeHandle = (lowerDist < upperDist) ? RangeHandle::Lower : RangeHandle::Upper;
                }

                isDragging = true;
                dragStartPos = mousePos;
                dragStartValue = (activeHandle == RangeHandle::Lower) ? lowerValue : upperValue;
            } else {
                // Single handle mode
                isDragging = true;
                dragStartPos = mousePos;
                dragStartValue = currentValue;
            }

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

            // Update hover state for handles in range mode
            if (isRangeMode && Contains(mousePos)) {
                Rect2Di bounds = GetBounds();
                hoveredHandle = GetHandleAt(mousePos, bounds);
                RequestRedraw();
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
                activeHandle = RangeHandle::NoneRange;

                if (onRelease) onRelease(event);
                if (onClick) onClick(event);

                return true;
            }

            return false;
        }

        bool HandleKeyDown(const UCEvent& event) {
            if (!IsFocused()) return false;

            float increment = step > 0 ? step : (maxValue - minValue) / 100.0f;

            if (isRangeMode) {
                // In range mode, arrows move the active handle (or both)
                RangeHandle targetHandle = (activeHandle != RangeHandle::NoneRange) ? activeHandle : RangeHandle::Both;

                switch (event.virtualKey) {
                    case UCKeys::Left:
                    case UCKeys::Down:
                        if (targetHandle == RangeHandle::Lower || targetHandle == RangeHandle::Both) {
                            SetLowerValue(lowerValue - increment);
                        }
                        if (targetHandle == RangeHandle::Upper || targetHandle == RangeHandle::Both) {
                            SetUpperValue(upperValue - increment);
                        }
                        return true;

                    case UCKeys::Right:
                    case UCKeys::Up:
                        if (targetHandle == RangeHandle::Lower || targetHandle == RangeHandle::Both) {
                            SetLowerValue(lowerValue + increment);
                        }
                        if (targetHandle == RangeHandle::Upper || targetHandle == RangeHandle::Both) {
                            SetUpperValue(upperValue + increment);
                        }
                        return true;

                    case UCKeys::Home:
                        if (targetHandle == RangeHandle::Lower || targetHandle == RangeHandle::Both) {
                            SetLowerValue(minValue);
                        }
                        return true;

                    case UCKeys::End:
                        if (targetHandle == RangeHandle::Upper || targetHandle == RangeHandle::Both) {
                            SetUpperValue(maxValue);
                        }
                        return true;

                    case UCKeys::Tab:
                        // Switch active handle
                        activeHandle = (activeHandle == RangeHandle::Lower) ? RangeHandle::Upper : RangeHandle::Lower;
                        RequestRedraw();
                        return true;
                }
            } else {
                // Single handle mode - standard behavior
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

            if (isRangeMode) {
                if (activeHandle == RangeHandle::Lower) {
                    SetLowerValue(newValue);
                } else if (activeHandle == RangeHandle::Upper) {
                    SetUpperValue(newValue);
                }
            } else {
                SetValue(newValue);
            }
        }

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

    inline std::shared_ptr<UltraCanvasSlider> CreateRangeSlider(
            const std::string& identifier, long id, long x, long y, long width, long height,
            float min = 0.0f, float max = 100.0f, float lower = 25.0f, float upper = 75.0f) {
        auto slider = std::make_shared<UltraCanvasSlider>(identifier, id, x, y, width, height);
        slider->SetSliderStyle(SliderStyle::Range);
        slider->SetRange(min, max);
        slider->SetRangeMode(true);
        slider->SetRangeValues(lower, upper);
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

    inline void SetSliderRangeValues(UltraCanvasSlider* slider, float lower, float upper) {
        if (slider && slider->IsRangeMode()) slider->SetRangeValues(lower, upper);
    }

} // namespace UltraCanvas
