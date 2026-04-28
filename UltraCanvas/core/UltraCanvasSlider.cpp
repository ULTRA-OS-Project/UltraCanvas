// core/UltraCanvasScrollbar.cpp
// Platform-independent scrollbar component implementation
// Version: 1.0.0
// Last Modified: 2025-08-15
// Author: UltraCanvas Framework

#include "UltraCanvasSlider.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasCommonTypes.h"
#include <string>
#include <functional>
#include <cmath>
#include <memory>

namespace UltraCanvas {

    UltraCanvasSlider::UltraCanvasSlider(const std::string &identifier, long id, long x, long y, long w, long h)
            : UltraCanvasUIElement(identifier, id, x, y, w, h) {

        // Initialize standard properties
        mouseCursor = UCMouseCursor::Hand;

        // Initialize range values
        lowerValue = minValue;
        upperValue = maxValue;

        UpdateSliderState();
    }

    void UltraCanvasSlider::SetRange(float min, float max) {
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

    void UltraCanvasSlider::SetValue(float value) {
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

    void UltraCanvasSlider::SetRangeMode(bool enabled) {
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

    void UltraCanvasSlider::SetLowerValue(float value) {
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

    void UltraCanvasSlider::SetUpperValue(float value) {
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

    void UltraCanvasSlider::SetRangeValues(float lower, float upper) {
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

    void UltraCanvasSlider::SetSliderStyle(SliderStyle newStyle) {
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

    void UltraCanvasSlider::SetOrientation(SliderOrientation orient) {
        orientation = orient;
        // Auto-adjust style based on orientation
        if (orient == SliderOrientation::Vertical && sliderStyle == SliderStyle::Horizontal) {
            sliderStyle = SliderStyle::Vertical;
        } else if (orient == SliderOrientation::Horizontal && sliderStyle == SliderStyle::Vertical) {
            sliderStyle = SliderStyle::Horizontal;
        }
    }

    void UltraCanvasSlider::SetColors(const Color &track, const Color &activeTrack, const Color &handle) {
        style.trackColor = track;
        style.activeTrackColor = activeTrack;
        style.handleColor = handle;
    }

    void UltraCanvasSlider::Render(IRenderContext *ctx, const Rect2Di &dirtyRect) {
        ctx->PushState();

        UpdateSliderState();
        Rect2Di bounds = GetLocalBounds();

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

    bool UltraCanvasSlider::OnEvent(const UCEvent &event) {
        if (!IsVisible() || IsDisabled()) return false;

        if (UltraCanvasUIElement::OnEvent(event)) {
            return true;
        }

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

        return false;
    }

    float UltraCanvasSlider::ClampAndStep(float value) const {
        float newValue = std::max(minValue, std::min(maxValue, value));

        // Apply step if specified
        if (step > 0) {
            float steps = std::round((newValue - minValue) / step);
            newValue = minValue + steps * step;
            newValue = std::max(minValue, std::min(maxValue, newValue));
        }

        return newValue;
    }

    RangeHandle UltraCanvasSlider::GetHandleAt(const Point2Di &pos, const Rect2Di &bounds) {
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

    void UltraCanvasSlider::UpdateSliderState() {
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

    void UltraCanvasSlider::RenderLinearSlider(const Rect2Di &bounds, IRenderContext *ctx) {
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

    void UltraCanvasSlider::RenderRangeSlider(const Rect2Di &bounds, IRenderContext *ctx) {
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

    void UltraCanvasSlider::RenderCircularSlider(const Rect2Di &bounds, IRenderContext *ctx) {
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

    void UltraCanvasSlider::RenderProgressSlider(const Rect2Di &bounds, IRenderContext *ctx) {
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

    void UltraCanvasSlider::RenderHandle(const Point2Di &position, IRenderContext *ctx, bool highlighted) {
        float handleRadius = style.handleSize / 2;
//            Rect2Di handleRect(
//                    position.x - handleRadius,
//                    position.y - handleRadius,
//                    style.handleSize,
//                    style.handleSize
//            );

        Color handleColor = highlighted ? style.handleHoverColor : GetCurrentHandleColor();

        ctx->SetFillPaint(handleColor);
        ctx->SetStrokePaint(style.handleBorderColor);
        ctx->SetStrokeWidth(style.borderWidth);

        // Fill handle
        switch (style.handleShape) {
            case SliderHandleShape::Circle: {
                // Draw filled circle
                ctx->FillCircle(position, handleRadius);
                // Draw border
                ctx->DrawCircle(position, handleRadius);
                break;
            }

            case SliderHandleShape::Square: {
                // Calculate square bounds
                Rect2Di handleRect(
                        position.x - handleRadius,
                        position.y - handleRadius,
                        style.handleSize,
                        style.handleSize
                );
                // Draw filled square
                ctx->FillRectangle(handleRect);
                // Draw border
                ctx->DrawRectangle(handleRect);
                break;
            }

            case SliderHandleShape::Triangle: {
                // Create triangle points (pointing up)
                std::vector<Point2Df> triangle = {
                        Point2Df(position.x, position.y - handleRadius),                    // Top
                        Point2Df(position.x - handleRadius, position.y + handleRadius),    // Bottom left
                        Point2Df(position.x + handleRadius, position.y + handleRadius)     // Bottom right
                };
                // Draw filled triangle
                ctx->FillLinePath(triangle);
                // Draw border
                ctx->DrawLinePath(triangle, true);
                break;
            }

            case SliderHandleShape::Diamond: {
                // Create diamond points
                std::vector<Point2Df> diamond = {
                        Point2Df(position.x, position.y - handleRadius),                   // Top
                        Point2Df(position.x + handleRadius, position.y),                   // Right
                        Point2Df(position.x, position.y + handleRadius),                   // Bottom
                        Point2Df(position.x - handleRadius, position.y)                    // Left
                };
                // Draw filled diamond
                ctx->FillLinePath(diamond);
                // Draw border
                ctx->DrawLinePath(diamond, true);
                break;
            }

            default:
                // Fallback to circle
                ctx->FillCircle(position, handleRadius);
                ctx->DrawCircle(position, handleRadius);
                break;
        }
        //ctx->DrawFilledRectangle(handleRect, handleColor, style.borderWidth, style.handleBorderColor, handleRadius);
    }

    void UltraCanvasSlider::RenderValueDisplay(const Rect2Di &bounds, IRenderContext *ctx) {
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

            ctx->DrawText(text, textPos);
        } else {
            // Range mode - display both values
            std::string lowerText = FormatValue(lowerValue);
            std::string upperText = FormatValue(upperValue);

            ctx->SetTextPaint(IsDisabled() ? style.disabledTextColor : style.textColor);

            bool isVertical = (orientation == SliderOrientation::Vertical);
            Point2Df lowerHandlePos = GetHandlePosition(bounds, isVertical, lowerValue);
            Point2Df upperHandlePos = GetHandlePosition(bounds, isVertical, upperValue);

            // Draw lower value
            ctx->SetFontStyle(style.fontStyle);
            Point2Df lowerTextSize = ctx->GetTextDimension(lowerText);
            Point2Df lowerTextPos;
            if (isVertical) {
                lowerTextPos = Point2Df(bounds.x + bounds.width + 8, lowerHandlePos.y - lowerTextSize.y / 2);
            } else {
                lowerTextPos = Point2Df(lowerHandlePos.x - lowerTextSize.x / 2, bounds.y - lowerTextSize.y / 2 - 4);
            }
            ctx->DrawText(lowerText, lowerTextPos);

            // Draw upper value
            Point2Df upperTextSize = ctx->GetTextDimension(upperText);
            Point2Df upperTextPos;
            if (isVertical) {
                upperTextPos = Point2Df(bounds.x + bounds.width + 8, upperHandlePos.y - upperTextSize.y / 2);
            } else {
                upperTextPos = Point2Df(upperHandlePos.x - upperTextSize.x / 2, bounds.y - upperTextSize.y / 2 - 4);
            }
            ctx->DrawText(upperText, upperTextPos);
        }
    }

    std::string UltraCanvasSlider::FormatValue(float value) const {
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

    Rect2Di UltraCanvasSlider::GetTrackRect(const Rect2Di &bounds, bool isVertical) const {
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

    Rect2Di UltraCanvasSlider::GetActiveTrackRect(const Rect2Di &trackRect, bool isVertical) const {
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

    Rect2Di UltraCanvasSlider::GetRangeTrackRect(const Rect2Di &trackRect, bool isVertical) const {
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

    Point2Di UltraCanvasSlider::GetHandlePosition(const Rect2Di &bounds, bool isVertical, float value) const {
        float percentage = (value - minValue) / (maxValue - minValue);

        if (isVertical) {
            int y = bounds.y + bounds.height - (bounds.height - style.handleSize) * percentage - style.handleSize / 2;
            return Point2Di(bounds.x + bounds.width / 2, y);
        } else {
            int x = bounds.x + (bounds.width - style.handleSize) * percentage + style.handleSize / 2;
            return Point2Di(x, bounds.y + bounds.height / 2);
        }
    }

    Color UltraCanvasSlider::GetCurrentTrackColor() const {
        return IsDisabled() ? style.disabledTrackColor : style.trackColor;
    }

    Color UltraCanvasSlider::GetCurrentHandleColor() const {
        switch(GetPrimaryState()) {
            case ElementState::Disabled: return style.handleDisabledColor;
            case ElementState::Pressed: return style.handlePressedColor;
            case ElementState::Hovered: return style.handleHoverColor;
            default: return style.handleColor;
        }
    }

    Point2Di UltraCanvasSlider::CalculateTextPosition(const Rect2Di &bounds, const Point2Di &textSize) const {
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

    bool UltraCanvasSlider::HandleMouseDown(const UCEvent &event) {
        Point2Di mousePos(event.pointer.x, event.pointer.y);

        if (!Contains(mousePos)) return false;

        Rect2Di bounds = GetLocalBounds();

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

    bool UltraCanvasSlider::HandleMouseMove(const UCEvent &event) {
        Point2Di mousePos(event.pointer.x, event.pointer.y);

        if (isDragging) {
            UpdateValueFromPosition(mousePos);
            return true;
        }

        // Update hover state for handles in range mode
        if (isRangeMode && Contains(mousePos)) {
            Rect2Di bounds = GetLocalBounds();
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

    bool UltraCanvasSlider::HandleMouseUp(const UCEvent &event) {
        if (isDragging) {
            isDragging = false;
            activeHandle = RangeHandle::NoneRange;

            if (onRelease) onRelease(event);
            if (onClick) onClick(event);

            return true;
        }

        return false;
    }

    bool UltraCanvasSlider::HandleKeyDown(const UCEvent &event) {
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

    void UltraCanvasSlider::UpdateValueFromPosition(const Point2Di &pos) {
        Rect2Di bounds = GetLocalBounds();
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
} // namespace UltraCanvas