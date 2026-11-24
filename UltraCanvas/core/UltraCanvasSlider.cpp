// core/UltraCanvasSlider.cpp
// Platform-independent slider component implementation
// Version: 1.6.0 - ticks and labels, the new handle shapes and styles, the bubble, the
//                  fill origin, rounded track ends, the circular value arc, relative drag
//                  and the wheel for knobs and the ruler, double-click reset
// Version: 1.5.1 - the two key switches say the keys they ignore (default; -Wswitch)
// Version: 1.5.0 - slider.handle, slider.handle-border
// Version: 1.4.0 - SliderVisualStyle::FromTheme
// Version: 1.3.0
// Last Modified: 2026-11-24
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

    UltraCanvasSlider::UltraCanvasSlider(const std::string &identifier, float x, float y, float w, float h)
            : UltraCanvasUIElement(identifier, x, y, w, h) {

        // Initialize standard properties
        mouseCursor = UCMouseCursor::Hand;

        // Initialize range values
        lowerValue = minValue;
        upperValue = maxValue;

        UpdateSliderState();
        ApplyThemeIfFollowing();
    }

    void UltraCanvasSlider::SetRange(float min, float max) {
        if (min <= max) {
            minValue = min;
            maxValue = max;
            // A caller that never stated a step gets one that suits the range:
            // whole units where they make a usable slider, continuous below
            // that. Without this a fractional range (gamma 0.2..3.0, a 0..1
            // ratio) would keep the whole-unit default and snap to a handful of
            // positions.
            if (!stepExplicit) step = DefaultStepForRange(minValue, maxValue);
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
            NotifyAccessibility(AccessibilityEventType::ValueChanged);
            RequestRedraw();
        }
    }

    bool UltraCanvasSlider::GetAccessibleRange(AccessibleRange& range) const {
        if (isRangeMode) return false;
        range.value = currentValue;
        range.minimum = minValue;
        range.maximum = maxValue;
        range.step = step;
        range.readOnly = IsDisabled();
        return true;
    }

    bool UltraCanvasSlider::SetAccessibleValue(double value) {
        if (isRangeMode || IsDisabled()) return false;
        SetValue(static_cast<float>(value));
        return true;
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

    void UltraCanvasSlider::Render(IRenderContext *ctx, const Rect2Df&dirtyRect) {
        (void)dirtyRect;
        UpdateSliderState();
        // Externally sized for now (explicit size or parent stretch); the base block
        // block measure sizes us. TODO: implement MeasureOwnContent for intrinsic sizing.
        Rect2Di bounds = GetLocalBounds();

        // The labels' size decides how much room the layout leaves them, so
        // measure them before anything is placed.
        MeasureTickLabels(ctx);

        // ===== RENDER BASED ON STYLE =====
        bool drawsOwnValue = false;
        if (isRangeMode || sliderStyle == SliderStyle::Range) {
            RenderHistogram(bounds, ctx);
            RenderRangeSlider(bounds, ctx);
        } else {
            switch (sliderStyle) {
                case SliderStyle::Circular:
                    RenderCircularSlider(bounds, ctx);
                    drawsOwnValue = true;
                    break;

                case SliderStyle::Knob:
                    RenderKnobSlider(bounds, ctx);
                    drawsOwnValue = true;
                    break;

                case SliderStyle::Progress:
                    RenderProgressSlider(bounds, ctx);
                    break;

                case SliderStyle::Field:
                    RenderFieldSlider(bounds, ctx);
                    drawsOwnValue = true;
                    break;

                case SliderStyle::Seek:
                    RenderSeekSlider(bounds, ctx);
                    break;

                case SliderStyle::Ruler:
                    RenderRulerSlider(bounds, ctx);
                    drawsOwnValue = true;
                    break;

                case SliderStyle::Segmented:
                    RenderHistogram(bounds, ctx);
                    RenderSegmentedSlider(bounds, ctx);
                    break;

                case SliderStyle::Horizontal:
                case SliderStyle::Vertical:
                default:
                    RenderHistogram(bounds, ctx);
                    RenderLinearSlider(bounds, ctx);
                    break;
            }
        }

        // ===== RENDER VALUE DISPLAY =====
        if (!drawsOwnValue && ShouldShowValueText()) {
            RenderValueDisplay(bounds, ctx);
        }
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

            case UCEventType::MouseDoubleClick:
                // A double-click puts a slider that has a reset value back to it
                // (the centre of a balance control, unity gain on a fader).
                if (hasResetValue && !isRangeMode) {
                    SetValue(resetValue);
                    return true;
                }
                return false;

            case UCEventType::MouseWheel:
                return HandleWheel(event);

            case UCEventType::KeyDown:
                return HandleKeyDown(event);

            case UCEventType::MouseEnter:
                SetHovered(true);
                showTooltip = (valueDisplay == SliderValueDisplay::Tooltip);
                RequestRedraw();
                return true;

            case UCEventType::MouseLeave:
                SetHovered(false);
                showTooltip = false;
                hoveredHandle = RangeHandle::NoneRange;
                hoverPosition = -1;
                RequestRedraw();
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


    namespace {
        constexpr double SliderPi = 3.14159265358979323846;

        double DegreesToRadians(double degrees) { return degrees * SliderPi / 180.0; }

        // The radius that rounds a bar `thickness` thick: the style's corner
        // radius, but never more than half the thickness (a pill).
        double BarRadius(float cornerRadius, double thickness) {
            return std::max(0.0, std::min<double>(cornerRadius, thickness / 2.0));
        }

        void FillBar(IRenderContext* ctx, const Rect2Dd& rect, double radius) {
            if (rect.width <= 0 || rect.height <= 0) return;
            if (radius > 0.0) ctx->FillRoundedRectangle(rect, radius);
            else ctx->FillRectangle(rect);
        }
    }

    // ===== GEOMETRY =====

    int UltraCanvasSlider::TickExtent(bool beforeSide) const {
        if (tickPosition == SliderTickPosition::NoTicks) return 0;
        const bool isVertical = (orientation == SliderOrientation::Vertical);
        const bool hasLabels = showTickLabels || !stopLabels.empty();

        const bool ticksHere = beforeSide
            ? (tickPosition == SliderTickPosition::Before || tickPosition == SliderTickPosition::BothSides)
            : (tickPosition == SliderTickPosition::After || tickPosition == SliderTickPosition::BothSides);

        int extent = 0;
        if (ticksHere) {
            extent += (int)std::ceil(style.tickGap + std::max(style.tickLength, style.minorTickLength));
        }
        // Labels go on the far side of ticks drawn after (or on) the track,
        // and before it only when that is where the ticks are.
        const bool labelsBefore = (tickPosition == SliderTickPosition::Before);
        if (hasLabels && beforeSide == labelsBefore) {
            int label = isVertical ? labelWidth : labelHeight;
            if (label <= 0) {
                // Not measured yet (no frame drawn): a fair guess from the font.
                label = isVertical ? (int)std::ceil(style.fontStyle.fontSize * 3.0f)
                                   : (int)std::ceil(style.fontStyle.fontSize * 1.4f);
            }
            extent += 2 + label;
            if (!ticksHere) extent += (int)std::ceil(style.tickGap);
        }
        return extent;
    }

    int UltraCanvasSlider::LabelOverhang() const {
        if (tickPosition == SliderTickPosition::NoTicks) return 0;
        if (!showTickLabels && stopLabels.empty()) return 0;
        const bool isVertical = (orientation == SliderOrientation::Vertical);
        int half = (isVertical ? labelHeight : labelWidth) / 2;
        return std::max(0, half - (int)style.handleSize / 2);
    }

    void UltraCanvasSlider::MeasureTickLabels(IRenderContext* ctx) const {
        if (tickPosition == SliderTickPosition::NoTicks || (!showTickLabels && stopLabels.empty())) {
            labelWidth = labelHeight = 0;
            return;
        }
        ctx->SetFontStyle(style.fontStyle);
        int widest = 0, tallest = 0;
        for (const auto& tick : GetTickValues()) {
            if (!tick.second) continue;
            Point2Di size = ctx->GetTextDimension(GetTickLabel(tick.first));
            widest = std::max(widest, size.x);
            tallest = std::max(tallest, size.y);
        }
        labelWidth = widest;
        labelHeight = tallest;
    }

    Rect2Di UltraCanvasSlider::GetSliderInteriorRect(const Rect2Di &bounds, bool isVertical) const {
        // Element-local coordinates: the ctx is translated to our origin and
        // event.pointer is element-local, so build the interior from the passed-in
        // local bounds (origin 0,0), not finalBounds.x/y (parent-space).
        // The cross-axis thickness must fit whichever is larger — the handle or
        // the bar plus its border — so a palette-style bar thicker than the
        // handle is not clipped at the element edge.
        // A value the slider shows is drawn above a horizontal bar and right
        // of a vertical one, so the bar keeps to the far edge to leave it the
        // room; with no value to show it is centred across the box, which puts
        // it on the line of the labels a row centres beside it.
        // The ticks and their labels are laid out either side of that band
        // (the band itself is what this returns), and a label wider than the
        // handle pulls both ends in so the end labels are not cut off.
        // How far the handle reaches across the bar (see RenderHandle's shapes)
        float handleCross = style.handleSize;
        if (style.handleShape == SliderHandleShape::FaderCap) handleCross = style.handleSize * 2.0f;
        else if (style.handleShape == SliderHandleShape::Bar) handleCross = style.handleSize * 1.6f;
        else if (style.handleShape == SliderHandleShape::Pointer) handleCross = style.handleSize * 1.2f;
        int core = (int)std::ceil(std::max(handleCross, style.trackHeight + 2.0f * style.borderWidth));
        int before = TickExtent(true);
        int after = TickExtent(false);
        int thickness = core + before + after;
        int inset = LabelOverhang();
        bool centred = (valueDisplay == SliderValueDisplay::NoDisplay) && !hoverPreview && histogram.empty();
        if (isVertical) {
            int x = centred ? bounds.x + (bounds.width - thickness) / 2 : bounds.x;
            return Rect2Di(x + before, bounds.y + inset, core, std::max(0, bounds.height - 2 * inset));
        } else {
            int y = centred ? bounds.y + (bounds.height - thickness) / 2
                            : bounds.y + bounds.height - thickness;
            return Rect2Di(bounds.x + inset, y + before, std::max(0, bounds.width - 2 * inset), core);
        }
    }

    Rect2Di UltraCanvasSlider::GetTrackRect(const Rect2Di &bounds, bool isVertical) const {
        Rect2Di interior = GetSliderInteriorRect(bounds, isVertical);
        if (isVertical) {
            int trackX = interior.x + (interior.width - (int)style.trackHeight) / 2;
            return Rect2Di(trackX, interior.y + (int)style.handleSize / 2,
                           (int)style.trackHeight, interior.height - (int)style.handleSize);
        } else {
            int trackY = interior.y + (interior.height - (int)style.trackHeight) / 2;
            return Rect2Di(interior.x + (int)style.handleSize / 2, trackY,
                           interior.width - (int)style.handleSize, (int)style.trackHeight);
        }
    }

    Rect2Di UltraCanvasSlider::GetActiveTrackRect(const Rect2Di &trackRect, bool isVertical) const {
        // From the fill origin (the minimum unless SetFillOrigin moved it) to
        // the value, whichever side of the origin the value is on.
        float span = maxValue - minValue;
        float valueFraction = GetPercentage();
        float originFraction = 0.0f;
        if (hasFillOrigin && span > 0.0f) {
            originFraction = std::max(0.0f, std::min(1.0f, (fillOrigin - minValue) / span));
        }
        float lo = std::min(originFraction, valueFraction);
        float hi = std::max(originFraction, valueFraction);

        if (isVertical) {
            int top = trackRect.y + (int)std::lround(trackRect.height * (1.0f - hi));
            int bottom = trackRect.y + (int)std::lround(trackRect.height * (1.0f - lo));
            return Rect2Di(trackRect.x, top, trackRect.width, bottom - top);
        } else {
            int left = trackRect.x + (int)std::lround(trackRect.width * lo);
            int right = trackRect.x + (int)std::lround(trackRect.width * hi);
            return Rect2Di(left, trackRect.y, right - left, trackRect.height);
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
        Rect2Di interior = GetSliderInteriorRect(bounds, isVertical);
        float percentage = (maxValue > minValue) ? (value - minValue) / (maxValue - minValue) : 0.0f;

        if (isVertical) {
            int y = interior.y + interior.height - (interior.height - (int)style.handleSize) * percentage - (int)style.handleSize / 2;
            return Point2Di(interior.x + interior.width / 2, y);
        } else {
            int x = interior.x + (interior.width - (int)style.handleSize) * percentage + (int)style.handleSize / 2;
            return Point2Di(x, interior.y + interior.height / 2);
        }
    }

    float UltraCanvasSlider::ValueAtPosition(const Point2Di& pos) const {
        Rect2Di bounds = GetLocalBounds();
        float ratio;
        if (sliderStyle == SliderStyle::Field && !isRangeMode) {
            // A field fills edge to edge: no handle to keep inside.
            ratio = bounds.width > 0 ? float(pos.x - bounds.x) / float(bounds.width) : 0.0f;
        } else if (orientation == SliderOrientation::Vertical) {
            Rect2Di interior = GetSliderInteriorRect(bounds, true);
            float travel = std::max(1.0f, interior.height - style.handleSize);
            ratio = 1.0f - (pos.y - interior.y - style.handleSize / 2) / travel;
        } else {
            Rect2Di interior = GetSliderInteriorRect(bounds, false);
            float travel = std::max(1.0f, interior.width - style.handleSize);
            ratio = (pos.x - interior.x - style.handleSize / 2) / travel;
        }
        ratio = std::max(0.0f, std::min(1.0f, ratio));
        return minValue + ratio * (maxValue - minValue);
    }

    // ===== TICKS =====

    std::vector<std::pair<float, bool>> UltraCanvasSlider::GetTickValues() const {
        std::vector<std::pair<float, bool>> ticks;
        const float span = maxValue - minValue;
        if (span <= 0.0f) return ticks;
        const float epsilon = span * 1e-5f;
        const int limit = 2000;

        if (!customTickValues.empty()) {
            for (float v : customTickValues) {
                if (v >= minValue - epsilon && v <= maxValue + epsilon) ticks.emplace_back(v, true);
            }
            return ticks;
        }

        if (!stopLabels.empty() && step > 0.0f && span / step <= limit) {
            int count = (int)std::floor(span / step + 1e-4f);
            for (int i = 0; i <= count; i++) ticks.emplace_back(minValue + i * step, true);
            return ticks;
        }

        float major = majorTickInterval;
        if (major <= 0.0f) {
            major = (step > 0.0f && span / step <= 20.0f) ? step : span / 10.0f;
        }
        if (span / major > limit) return ticks;

        float minor = minorTickInterval;
        if (minor > 0.0f && minor < major && span / minor <= limit) {
            int count = (int)std::floor(span / minor + 1e-4f);
            for (int i = 0; i <= count; i++) {
                float offset = i * minor;
                float rem = std::fmod(offset, major);
                bool isMajor = rem < minor * 0.01f || (major - rem) < minor * 0.01f;
                ticks.emplace_back(minValue + offset, isMajor);
            }
        } else {
            int count = (int)std::floor(span / major + 1e-4f);
            for (int i = 0; i <= count; i++) ticks.emplace_back(minValue + i * major, true);
        }
        return ticks;
    }

    std::string UltraCanvasSlider::FormatNumber(float value) const {
        if (std::abs(value - std::round(value)) < 1e-4f) {
            return std::to_string((long long)std::llround(value));
        }
        char buffer[32];
        // locale-ok: a number a person reads on screen, in their own notation
        std::snprintf(buffer, sizeof(buffer), "%.2f", value);
        std::string text(buffer);
        while (!text.empty() && text.back() == '0') text.pop_back();
        if (!text.empty() && (text.back() == '.' || text.back() == ',')) text.pop_back();
        return text;
    }

    std::string UltraCanvasSlider::GetTickLabel(float value) const {
        if (tickLabelFormatter) return tickLabelFormatter(value);
        if (!stopLabels.empty() && step > 0.0f) {
            long index = std::lround((value - minValue) / step);
            if (index >= 0 && index < (long)stopLabels.size()) return stopLabels[(size_t)index];
        }
        return FormatNumber(value);
    }

    void UltraCanvasSlider::RenderTicks(const Rect2Di &bounds, IRenderContext *ctx) {
        if (tickPosition == SliderTickPosition::NoTicks) return;
        const bool isVertical = (orientation == SliderOrientation::Vertical);
        const auto ticks = GetTickValues();
        if (ticks.empty()) return;

        const Rect2Di interior = GetSliderInteriorRect(bounds, isVertical);
        const Rect2Di trackRect = GetTrackRect(bounds, isVertical);
        const float span = maxValue - minValue;
        const Color tickColor = IsDisabled() ? style.disabledTextColor : style.tickColor;
        const Color onFillColor = style.handleColor.WithAlpha(220);
        const Rect2Di fillRect = GetActiveTrackRect(trackRect, isVertical);

        const bool drawBefore = tickPosition == SliderTickPosition::Before || tickPosition == SliderTickPosition::BothSides;
        const bool drawAfter = tickPosition == SliderTickPosition::After || tickPosition == SliderTickPosition::BothSides;
        const bool onTrack = tickPosition == SliderTickPosition::OnTrack;
        const bool hasLabels = showTickLabels || !stopLabels.empty();
        const float rampMin = std::max(1.0f, std::min(2.0f, style.minorTickLength));

        auto lengthOf = [&](float value, bool major) -> float {
            if (tickStyle == SliderTickStyle::Ramp) {
                float f = span > 0.0f ? (value - minValue) / span : 0.0f;
                return rampMin + (style.tickLength - rampMin) * f;
            }
            return major ? style.tickLength : style.minorTickLength;
        };

        // One mark, from `edge` outward by `length` (sign = -1 before the
        // track, +1 after it), at main-axis position `pos`.
        auto drawMark = [&](int pos, float edge, float sign, float length, bool major) {
            if (tickStyle == SliderTickStyle::Dot) {
                double radius = major ? 2.0 : 1.5;
                double centre = edge + sign * radius;
                ctx->SetFillPaint(tickColor);
                if (isVertical) ctx->FillCircle(Point2Dd(centre, pos), radius);
                else ctx->FillCircle(Point2Dd(pos, centre), radius);
                return;
            }
            double width = (tickStyle == SliderTickStyle::Ramp) ? 2.0 : 1.0;
            double start = sign < 0 ? edge - length : edge;
            ctx->SetFillPaint(tickColor);
            if (isVertical) ctx->FillRectangle(Rect2Dd(start, pos - width / 2.0, length, width));
            else ctx->FillRectangle(Rect2Dd(pos - width / 2.0, start, width, length));
        };

        for (const auto& tick : ticks) {
            const float value = tick.first;
            const bool major = tick.second;
            Point2Di at = GetHandlePosition(bounds, isVertical, value);
            const int pos = isVertical ? at.y : at.x;
            const float length = lengthOf(value, major);

            if (drawBefore) {
                float edge = (isVertical ? interior.x : interior.y) - style.tickGap;
                drawMark(pos, edge, -1.0f, length, major);
            }
            if (drawAfter) {
                float edge = (isVertical ? interior.x + interior.width : interior.y + interior.height) + style.tickGap;
                drawMark(pos, edge, 1.0f, length, major);
            }
            if (onTrack) {
                // Inside the track: a dot (or a short line) per stop, light on
                // the filled part so it still shows.
                bool onFill = isVertical ? (pos >= fillRect.y && pos <= fillRect.y + fillRect.height && fillRect.height > 0)
                                         : (pos >= fillRect.x && pos <= fillRect.x + fillRect.width && fillRect.width > 0);
                Color c = onFill ? onFillColor : tickColor;
                double thickness = isVertical ? trackRect.width : trackRect.height;
                ctx->SetFillPaint(c);
                if (tickStyle == SliderTickStyle::Line) {
                    if (isVertical) ctx->FillRectangle(Rect2Dd(trackRect.x + 1, pos - 0.5, thickness - 2, 1));
                    else ctx->FillRectangle(Rect2Dd(pos - 0.5, trackRect.y + 1, 1, thickness - 2));
                } else {
                    double radius = std::max(1.0, std::min(2.0, thickness / 4.0));
                    Point2Dd centre = isVertical ? Point2Dd(trackRect.x + thickness / 2.0, pos)
                                                 : Point2Dd(pos, trackRect.y + thickness / 2.0);
                    ctx->FillCircle(centre, radius);
                }
            }

            if (hasLabels && major) {
                std::string text = GetTickLabel(value);
                if (text.empty()) continue;
                ctx->SetFontStyle(style.fontStyle);
                bool current = !stopLabels.empty() && !isRangeMode && std::abs(value - currentValue) < 1e-4f;
                ctx->SetTextPaint(IsDisabled() ? style.disabledTextColor
                                               : (current ? style.textColor : style.tickLabelColor));
                Point2Di size = ctx->GetTextDimension(text);
                const bool before = (tickPosition == SliderTickPosition::Before);
                const bool ticksOnLabelSide = before ? drawBefore : drawAfter;
                float offset = style.tickGap + (ticksOnLabelSide ? std::max(style.tickLength, style.minorTickLength) : 0.0f) + 2.0f;
                if (isVertical) {
                    int x = before ? interior.x - (int)offset - size.x
                                   : interior.x + interior.width + (int)offset;
                    ctx->DrawText(text, Point2Dd(x, pos - size.y / 2.0));
                } else {
                    int y = before ? interior.y - (int)offset - size.y
                                   : interior.y + interior.height + (int)offset;
                    ctx->DrawText(text, Point2Dd(pos - size.x / 2.0, y));
                }
            }
        }
    }

    void UltraCanvasSlider::RenderHistogram(const Rect2Di &bounds, IRenderContext *ctx) {
        if (histogram.empty() || orientation == SliderOrientation::Vertical) return;
        const Rect2Di interior = GetSliderInteriorRect(bounds, false);
        const Rect2Di trackRect = GetTrackRect(bounds, false);
        int bottom = interior.y - TickExtent(true) - 4;
        int top = bounds.y + 2;
        int height = bottom - top;
        if (height < 6 || trackRect.width <= 0) return;

        float tallest = 0.0f;
        for (float h : histogram) tallest = std::max(tallest, h);
        if (tallest <= 0.0f) return;

        // The selected span in value units: between the handles, or the fill.
        float span = maxValue - minValue;
        float selLo, selHi;
        if (isRangeMode) {
            selLo = lowerValue; selHi = upperValue;
        } else {
            float origin = hasFillOrigin ? fillOrigin : minValue;
            selLo = std::min(origin, currentValue); selHi = std::max(origin, currentValue);
        }

        const size_t n = histogram.size();
        const double bucket = double(trackRect.width) / double(n);
        for (size_t i = 0; i < n; i++) {
            double h = histogram[i] / tallest * height;
            if (h <= 0.0) continue;
            double x = trackRect.x + i * bucket;
            float centreValue = minValue + float((i + 0.5) / double(n)) * span;
            bool selected = centreValue >= selLo && centreValue <= selHi;
            ctx->SetFillPaint(IsDisabled() ? style.disabledTrackColor
                                           : (selected ? style.histogramActiveColor : style.histogramColor));
            double gap = bucket > 3.0 ? 1.0 : 0.0;
            ctx->FillRectangle(Rect2Dd(x + gap, bottom - h, std::max(1.0, bucket - 2.0 * gap), h));
        }
    }

    // ===== TRACK =====

    Color UltraCanvasSlider::ZoneColor(float fraction) const {
        if (IsDisabled()) return style.disabledActiveTrackColor;
        Color c = style.activeTrackColor;
        float best = -1.0f;
        for (const auto& zone : colorZones) {
            if (fraction + 1e-5f >= zone.fromFraction && zone.fromFraction >= best) {
                c = zone.color;
                best = zone.fromFraction;
            }
        }
        return c;
    }

    void UltraCanvasSlider::RenderTrackBar(IRenderContext *ctx, const Rect2Di &trackRect, bool isVertical) {
        const double thickness = isVertical ? trackRect.width : trackRect.height;
        const double radius = BarRadius(style.cornerRadius, thickness);
        const Color track = GetCurrentTrackColor();

        if (style.trackInset) {
            // A groove: a dark rim along the top (left) edge, the track
            // colour inside it, and a light line along the bottom (right) one.
            ctx->SetFillPaint(track.Darken(0.45f));
            FillBar(ctx, trackRect, radius);
            Rect2Dd inner = isVertical
                ? Rect2Dd(trackRect.x + 2, trackRect.y + 1, trackRect.width - 2, trackRect.height - 1)
                : Rect2Dd(trackRect.x + 1, trackRect.y + 2, trackRect.width - 1, trackRect.height - 2);
            ctx->SetFillPaint(track);
            FillBar(ctx, inner, std::max(0.0, radius - 1.0));
            ctx->SetFillPaint(track.Lighten(0.35f));
            if (isVertical) ctx->FillRectangle(Rect2Dd(trackRect.x + trackRect.width - 1, trackRect.y + radius, 1, trackRect.height - 2 * radius));
            else ctx->FillRectangle(Rect2Dd(trackRect.x + radius, trackRect.y + trackRect.height - 1, trackRect.width - 2 * radius, 1));
        } else {
            ctx->SetFillPaint(track);
            FillBar(ctx, trackRect, radius);
            if (style.borderWidth > 0.0f) {
                ctx->SetStrokePaint(GetCurrentHandleBorderColor());
                ctx->SetStrokeWidth(style.borderWidth);
                if (radius > 0.0) ctx->DrawRoundedRectangle(trackRect, radius);
                else ctx->DrawRectangle(trackRect);
            }
        }
    }

    void UltraCanvasSlider::RenderLinearSlider(const Rect2Di &bounds, IRenderContext *ctx) {
        bool isVertical = (orientation == SliderOrientation::Vertical);

        // Calculate track rectangle
        Rect2Di trackRect = GetTrackRect(bounds, isVertical);
        Point2Di handlePos = GetHandlePosition(bounds, isVertical, currentValue);
        const double thickness = isVertical ? trackRect.width : trackRect.height;
        const double radius = BarRadius(style.cornerRadius, thickness);

        if (!trackGradientStops.empty()) {
            // Paint the whole bar with the supplied colour gradient (e.g. a full
            // hue palette). The bar spans the full slider length with fully
            // rounded end caps so the handle appears to sit inside the bar at
            // both extremes. The active-track overlay is skipped so the palette
            // stays visible across the entire bar.
            Rect2Di interior = GetSliderInteriorRect(bounds, isVertical);
            Rect2Di barRect = trackRect;
            double capRadius;
            if (isVertical) {
                barRect.y = interior.y;
                barRect.height = interior.height;
                capRadius = barRect.width / 2.0;
            } else {
                barRect.x = interior.x;
                barRect.width = interior.width;
                capRadius = barRect.height / 2.0;
            }

            // Gradient coordinates span the handle-travel range (trackRect) so
            // the colour under the handle matches the value; the rounded caps
            // continue the first/last stop colours (CAIRO_EXTEND_PAD).
            std::shared_ptr<IPaintPattern> grad = isVertical
                ? ctx->CreateLinearGradientPattern(trackRect.x, trackRect.y + trackRect.height,
                                                   trackRect.x, trackRect.y, trackGradientStops)
                : ctx->CreateLinearGradientPattern(trackRect.x, trackRect.y,
                                                   trackRect.x + trackRect.width, trackRect.y,
                                                   trackGradientStops);
            ctx->SetFillPaint(grad);
            ctx->FillRoundedRectangle(barRect, capRadius);

            // Outline so the bar reads as a discrete control.
            ctx->SetStrokePaint(style.handleBorderColor);
            ctx->SetStrokeWidth(style.borderWidth);
            ctx->DrawRoundedRectangle(barRect, capRadius);
        } else if (style.handleShape == SliderHandleShape::Bar && style.handleGap > 0.0f && !hasFillOrigin) {
            // A bar handle with clear space either side: the filled part and
            // the rest of the track are two separate pills.
            const double half = std::max(1.5, style.handleSize / 8.0) + style.handleGap;
            const Color fill = colorZones.empty() ? GetCurrentActiveTrackColor() : ZoneColor(GetPercentage());
            const Color rest = GetCurrentTrackColor();
            if (isVertical) {
                double bottom = trackRect.y + trackRect.height;
                ctx->SetFillPaint(fill);
                FillBar(ctx, Rect2Dd(trackRect.x, handlePos.y + half, trackRect.width, bottom - handlePos.y - half), radius);
                ctx->SetFillPaint(rest);
                FillBar(ctx, Rect2Dd(trackRect.x, trackRect.y, trackRect.width, handlePos.y - half - trackRect.y), radius);
            } else {
                double right = trackRect.x + trackRect.width;
                ctx->SetFillPaint(fill);
                FillBar(ctx, Rect2Dd(trackRect.x, trackRect.y, handlePos.x - half - trackRect.x, trackRect.height), radius);
                ctx->SetFillPaint(rest);
                FillBar(ctx, Rect2Dd(handlePos.x + half, trackRect.y, right - handlePos.x - half, trackRect.height), radius);
            }
        } else {
            RenderTrackBar(ctx, trackRect, isVertical);

            // The filled part, clipped to the rounded track so its ends follow
            // the track's wherever the fill starts.
            Rect2Di activeRect = GetActiveTrackRect(trackRect, isVertical);
            if ((isVertical && activeRect.height > 0) || (!isVertical && activeRect.width > 0)) {
                ctx->PushState();
                ctx->ClipRoundedRectangle(trackRect, radius, radius, radius, radius);
                ctx->SetFillPaint(colorZones.empty() ? GetCurrentActiveTrackColor() : ZoneColor(GetPercentage()));
                ctx->FillRectangle(activeRect);
                ctx->PopState();
            }
            if (hasFillOrigin) {
                // A short mark at the origin, so the neutral point is visible
                // when the value sits on it.
                Point2Di at = GetHandlePosition(bounds, isVertical, fillOrigin);
                ctx->SetFillPaint(IsDisabled() ? style.disabledTextColor : style.tickColor);
                if (isVertical) ctx->FillRectangle(Rect2Dd(trackRect.x - 3, at.y - 1, trackRect.width + 6, 2));
                else ctx->FillRectangle(Rect2Dd(at.x - 1, trackRect.y - 3, 2, trackRect.height + 6));
            }
        }

        RenderTicks(bounds, ctx);
        RenderHandle(handlePos, ctx);
    }

    void UltraCanvasSlider::RenderRangeSlider(const Rect2Di &bounds, IRenderContext *ctx) {
        bool isVertical = (orientation == SliderOrientation::Vertical);

        // Calculate track rectangle
        Rect2Di trackRect = GetTrackRect(bounds, isVertical);
        const double thickness = isVertical ? trackRect.width : trackRect.height;
        const double radius = BarRadius(style.cornerRadius, thickness);

        // Draw full track background
        RenderTrackBar(ctx, trackRect, isVertical);

        // Draw range region (between handles)
        Rect2Di rangeRect = GetRangeTrackRect(trackRect, isVertical);
        if ((isVertical && rangeRect.height > 0) || (!isVertical && rangeRect.width > 0)) {
            ctx->PushState();
            ctx->ClipRoundedRectangle(trackRect, radius, radius, radius, radius);
            ctx->SetFillPaint(IsDisabled() ? style.disabledActiveTrackColor : style.rangeTrackColor);
            ctx->FillRectangle(rangeRect);
            ctx->PopState();
        }

        RenderTicks(bounds, ctx);

        // Draw both handles
        Point2Di lowerHandlePos = GetHandlePosition(bounds, isVertical, lowerValue);
        Point2Di upperHandlePos = GetHandlePosition(bounds, isVertical, upperValue);

        // Draw lower handle
        RenderHandle(lowerHandlePos, ctx, (activeHandle == RangeHandle::Lower) || (hoveredHandle == RangeHandle::Lower));

        // Draw upper handle
        RenderHandle(upperHandlePos, ctx, (activeHandle == RangeHandle::Upper) || (hoveredHandle == RangeHandle::Upper));
    }

    void UltraCanvasSlider::RenderProgressSlider(const Rect2Di &bounds, IRenderContext *ctx) {
        // Similar to linear but without handle
        bool isVertical = (orientation == SliderOrientation::Vertical);
        Rect2Di interior = GetSliderInteriorRect(bounds, isVertical);
        const double thickness = isVertical ? interior.width : interior.height;
        const double radius = BarRadius(style.cornerRadius, thickness);

        // Draw background
        ctx->SetFillPaint(GetCurrentTrackColor());
        FillBar(ctx, interior, radius);

        // Draw progress, clipped to the rounded bar
        Rect2Di progressRect = GetActiveTrackRect(interior, isVertical);
        if ((isVertical && progressRect.height > 0) || (!isVertical && progressRect.width > 0)) {
            ctx->PushState();
            ctx->ClipRoundedRectangle(interior, radius, radius, radius, radius);
            ctx->SetFillPaint(colorZones.empty() ? GetCurrentActiveTrackColor() : ZoneColor(GetPercentage()));
            ctx->FillRectangle(progressRect);
            ctx->PopState();
        }

        // Draw border
        ctx->SetStrokePaint(GetCurrentHandleBorderColor());
        ctx->SetStrokeWidth(style.borderWidth);
        if (radius > 0.0) ctx->DrawRoundedRectangle(interior, radius);
        else ctx->DrawRectangle(interior);

        RenderTicks(bounds, ctx);
    }

    void UltraCanvasSlider::RenderSegmentedSlider(const Rect2Di &bounds, IRenderContext *ctx) {
        bool isVertical = (orientation == SliderOrientation::Vertical);
        Rect2Di trackRect = GetTrackRect(bounds, isVertical);
        const int n = std::max(2, segmentCount);
        const double gap = 2.0;
        const double length = isVertical ? trackRect.height : trackRect.width;
        const double segment = (length - gap * (n - 1)) / n;
        if (segment <= 0.5) return;
        const double radius = BarRadius(style.cornerRadius, std::min(segment, isVertical ? (double)trackRect.width : (double)trackRect.height));
        const float fraction = GetPercentage();

        for (int i = 0; i < n; i++) {
            const float from = float(i) / float(n);
            const bool lit = float(i + 1) / float(n) <= fraction + 1e-4f;
            Color c = ZoneColor(from);
            if (!lit) c = IsDisabled() ? style.disabledTrackColor : c.WithAlpha(50);
            ctx->SetFillPaint(c);
            double offset = i * (segment + gap);
            Rect2Dd r = isVertical
                ? Rect2Dd(trackRect.x, trackRect.y + trackRect.height - offset - segment, trackRect.width, segment)
                : Rect2Dd(trackRect.x + offset, trackRect.y, segment, trackRect.height);
            FillBar(ctx, r, radius);
        }

        RenderTicks(bounds, ctx);
        RenderHandle(GetHandlePosition(bounds, isVertical, currentValue), ctx);
    }

    void UltraCanvasSlider::RenderSeekSlider(const Rect2Di &bounds, IRenderContext *ctx) {
        bool isVertical = (orientation == SliderOrientation::Vertical);
        Rect2Di trackRect = GetTrackRect(bounds, isVertical);
        const bool active = IsHovered() || isDragging || IsFocused();
        // The line thickens while the pointer is over it.
        const int grow = active ? 2 : 0;
        if (isVertical) { trackRect.x -= grow / 2; trackRect.width += grow; }
        else { trackRect.y -= grow / 2; trackRect.height += grow; }
        const double thickness = isVertical ? trackRect.width : trackRect.height;
        const double radius = BarRadius(style.cornerRadius, thickness);

        ctx->SetFillPaint(GetCurrentTrackColor());
        FillBar(ctx, trackRect, radius);

        ctx->PushState();
        ctx->ClipRoundedRectangle(trackRect, radius, radius, radius, radius);
        float span = maxValue - minValue;
        if (hasSecondaryValue && span > 0.0f) {
            float f = std::max(0.0f, std::min(1.0f, (secondaryValue - minValue) / span));
            ctx->SetFillPaint(IsDisabled() ? style.disabledTrackColor : style.secondaryTrackColor);
            if (isVertical) ctx->FillRectangle(Rect2Dd(trackRect.x, trackRect.y + trackRect.height * (1.0f - f), trackRect.width, trackRect.height * f));
            else ctx->FillRectangle(Rect2Dd(trackRect.x, trackRect.y, trackRect.width * f, trackRect.height));
        }
        ctx->SetFillPaint(GetCurrentActiveTrackColor());
        ctx->FillRectangle(GetActiveTrackRect(trackRect, isVertical));
        ctx->PopState();

        // The thumb only while the pointer is over the line, or it is held.
        if (active && style.handleShape != SliderHandleShape::NoHandle) {
            Point2Di at = GetHandlePosition(bounds, isVertical, currentValue);
            ctx->SetFillPaint(GetCurrentActiveTrackColor());
            ctx->FillCircle(at, style.handleSize / 2.0);
        }
    }

    void UltraCanvasSlider::RenderCircularSlider(const Rect2Di &bounds, IRenderContext *ctx) {
        // Circular/knob style slider — element-local coordinates (ctx is translated
        // to our origin), so use the local bounds, not finalBounds.x/y.
        const double centerX = bounds.x + bounds.width / 2.0;
        const double centerY = bounds.y + bounds.height / 2.0;
        const double margin = std::max(style.handleSize / 2.0, style.trackHeight / 2.0) + 1.0;
        const double radius = std::min(bounds.width, bounds.height) / 2.0 - margin;
        if (radius <= 2.0) return;

        const double start = DegreesToRadians(arcStartAngle);
        const double sweep = DegreesToRadians(arcSweepAngle);
        const float span = maxValue - minValue;
        auto angleOf = [&](float value) {
            float f = span > 0.0f ? std::max(0.0f, std::min(1.0f, (value - minValue) / span)) : 0.0f;
            return start + sweep * f;
        };

        ctx->SetLineCap(LineCap::Round);
        ctx->SetStrokeWidth(style.trackHeight);

        // The track: the whole arc
        ctx->SetStrokePaint(GetCurrentTrackColor());
        ctx->DrawArc(centerX, centerY, radius, start, start + sweep);

        // The value: from the start (or the fill origin) to the value
        double a0 = hasFillOrigin ? angleOf(fillOrigin) : start;
        double a1 = angleOf(currentValue);
        if (std::abs(a1 - a0) > 1e-3) {
            ctx->SetStrokePaint(colorZones.empty() ? GetCurrentActiveTrackColor() : ZoneColor(GetPercentage()));
            ctx->DrawArc(centerX, centerY, radius, std::min(a0, a1), std::max(a0, a1));
        }
        ctx->SetLineCap(LineCap::Butt);

        // Draw handle at arc end
        Point2Di handlePos((int)std::lround(centerX + radius * std::cos(a1)),
                           (int)std::lround(centerY + radius * std::sin(a1)));
        RenderHandle(handlePos, ctx);

        if (ShouldShowValueText()) {
            std::string text = GetDisplayText();
            if (!text.empty()) {
                ctx->SetFontStyle(style.fontStyle);
                ctx->SetTextPaint(IsDisabled() ? style.disabledTextColor : style.textColor);
                Point2Di size = ctx->GetTextDimension(text);
                ctx->DrawText(text, Point2Dd(centerX - size.x / 2.0, centerY - size.y / 2.0));
            }
        }
    }

    void UltraCanvasSlider::RenderKnobSlider(const Rect2Di &bounds, IRenderContext *ctx) {
        const double centerX = bounds.x + bounds.width / 2.0;
        const double centerY = bounds.y + bounds.height / 2.0;
        const double outer = std::min(bounds.width, bounds.height) / 2.0 - 4.0;
        if (outer <= 8.0) return;

        const double start = DegreesToRadians(arcStartAngle);
        const double sweep = DegreesToRadians(arcSweepAngle);
        const float span = maxValue - minValue;
        const float valueFraction = GetPercentage();
        float originFraction = 0.0f;
        if (hasFillOrigin && span > 0.0f) originFraction = std::max(0.0f, std::min(1.0f, (fillOrigin - minValue) / span));
        const float lo = std::min(originFraction, valueFraction), hi = std::max(originFraction, valueFraction);

        // The ring of dots: the ticks when there are any, else one every tenth.
        std::vector<std::pair<float, bool>> dots;
        if (tickPosition != SliderTickPosition::NoTicks) {
            for (const auto& tick : GetTickValues()) {
                dots.emplace_back(span > 0.0f ? (tick.first - minValue) / span : 0.0f, tick.second);
            }
        } else {
            for (int i = 0; i <= 10; i++) dots.emplace_back(i / 10.0f, i % 5 == 0);
        }
        const double ringRadius = outer - 3.0;
        for (const auto& dot : dots) {
            double a = start + sweep * dot.first;
            bool lit = dot.first >= lo - 1e-4f && dot.first <= hi + 1e-4f;
            ctx->SetFillPaint(lit ? (colorZones.empty() ? GetCurrentActiveTrackColor() : ZoneColor(dot.first))
                                  : GetCurrentTrackColor());
            ctx->FillCircle(Point2Dd(centerX + ringRadius * std::cos(a), centerY + ringRadius * std::sin(a)),
                            dot.second ? 3.0 : 2.0);
        }

        // The cap: a raised disc with a soft shadow
        const double cap = std::max(6.0, outer - 14.0);
        ctx->SetFillPaint(Color(0, 0, 0, 40));
        ctx->FillCircle(Point2Dd(centerX, centerY + 3.0), cap + 1.0);
        ctx->SetFillPaint(Color(0, 0, 0, 30));
        ctx->FillCircle(Point2Dd(centerX, centerY + 1.5), cap);
        const Color top = GetCurrentHandleColor();
        std::vector<GradientStop> stops = { GradientStop(0.0, top), GradientStop(1.0, top.Darken(0.18f)) };
        ctx->SetFillPaint(ctx->CreateLinearGradientPattern(centerX, centerY - cap, centerX, centerY + cap, stops));
        ctx->FillCircle(Point2Dd(centerX, centerY), cap);
        ctx->SetStrokePaint(GetCurrentHandleBorderColor());
        ctx->SetStrokeWidth(style.borderWidth);
        ctx->DrawCircle(Point2Dd(centerX, centerY), cap);

        // The pointer line, from inside the cap to near its rim
        double a = start + sweep * valueFraction;
        ctx->SetLineCap(LineCap::Round);
        ctx->SetStrokeWidth(std::max(2.0, cap / 10.0));
        ctx->SetStrokePaint(IsFocused() || isDragging ? GetCurrentActiveTrackColor()
                                                      : GetCurrentActiveTrackColor().Darken(0.1f));
        ctx->DrawLine(Point2Dd(centerX + cap * 0.45 * std::cos(a), centerY + cap * 0.45 * std::sin(a)),
                      Point2Dd(centerX + cap * 0.85 * std::cos(a), centerY + cap * 0.85 * std::sin(a)));
        ctx->SetLineCap(LineCap::Butt);

        if (ShouldShowValueText()) {
            std::string text = GetDisplayText();
            if (!text.empty()) {
                ctx->SetFontStyle(style.fontStyle);
                ctx->SetTextPaint(IsDisabled() ? style.disabledTextColor : style.textColor);
                Point2Di size = ctx->GetTextDimension(text);
                ctx->DrawText(text, Point2Dd(centerX - size.x / 2.0, centerY - size.y / 2.0));
            }
        }
    }

    void UltraCanvasSlider::RenderFieldSlider(const Rect2Di &bounds, IRenderContext *ctx) {
        // A field the full size of the element: its background fills with the
        // value, the name (SetCustomText) sits on the left, the number right.
        Rect2Di field(bounds.x, bounds.y, bounds.width, bounds.height);
        const double radius = BarRadius(style.cornerRadius, field.height);

        ctx->SetFillPaint(GetCurrentTrackColor());
        FillBar(ctx, field, radius);

        float span = maxValue - minValue;
        float valueFraction = GetPercentage();
        float originFraction = 0.0f;
        if (hasFillOrigin && span > 0.0f) originFraction = std::max(0.0f, std::min(1.0f, (fillOrigin - minValue) / span));
        float lo = std::min(originFraction, valueFraction), hi = std::max(originFraction, valueFraction);
        if (hi > lo) {
            ctx->PushState();
            ctx->ClipRoundedRectangle(field, radius, radius, radius, radius);
            Color fill = colorZones.empty() ? GetCurrentActiveTrackColor() : ZoneColor(valueFraction);
            ctx->SetFillPaint(fill.WithAlpha((uint8_t)std::min<int>(fill.a, 150)));
            ctx->FillRectangle(Rect2Dd(field.x + field.width * lo, field.y, field.width * (hi - lo), field.height));
            ctx->PopState();
        }

        ctx->SetStrokePaint((IsFocused() || isDragging) && !IsDisabled() ? GetCurrentActiveTrackColor()
                                                                          : GetCurrentHandleBorderColor());
        ctx->SetStrokeWidth(style.borderWidth);
        Rect2Dd outline(field.x + 0.5, field.y + 0.5, field.width - 1.0, field.height - 1.0);
        if (radius > 0.0) ctx->DrawRoundedRectangle(outline, radius);
        else ctx->DrawRectangle(outline);

        ctx->SetFontStyle(style.fontStyle);
        ctx->SetTextPaint(IsDisabled() ? style.disabledTextColor : style.textColor);
        const int padding = 8;
        std::string value = FormatValue(currentValue);
        Point2Di valueSize = ctx->GetTextDimension(value);
        ctx->DrawText(value, Point2Dd(field.x + field.width - padding - valueSize.x,
                                      field.y + (field.height - valueSize.y) / 2.0));
        if (!customText.empty()) {
            Point2Di nameSize = ctx->GetTextDimension(customText);
            ctx->PushState();
            ctx->ClipRect(Rect2Dd(field.x + padding, field.y, std::max(0, field.width - 3 * padding - valueSize.x), field.height));
            ctx->DrawText(customText, Point2Dd(field.x + padding, field.y + (field.height - nameSize.y) / 2.0));
            ctx->PopState();
        }
    }

    void UltraCanvasSlider::RenderRulerSlider(const Rect2Di &bounds, IRenderContext *ctx) {
        // The marker stays in the middle; the scale slides under it, one unit
        // of value every pixelsPerUnit pixels, and fades towards both ends.
        const double centerX = bounds.x + bounds.width / 2.0;
        ctx->SetFontStyle(style.fontStyle);
        const int textHeight = ctx->GetTextDimension("0").y;
        const bool showValue = (valueDisplay != SliderValueDisplay::NoDisplay);
        const double markerTop = bounds.y + (showValue ? textHeight + 4 : 2);
        const double tallest = std::max(style.tickLength, style.minorTickLength);
        const double baseline = markerTop + 7.0 + tallest;
        const Color tickColor = IsDisabled() ? style.disabledTextColor : style.tickColor;

        const double halfWidth = std::max(1.0, bounds.width / 2.0);
        for (const auto& tick : GetTickValues()) {
            double x = centerX + (tick.first - currentValue) * pixelsPerUnit;
            if (x < bounds.x || x > bounds.x + bounds.width) continue;
            double fade = std::max(0.0, std::min(1.0, 1.0 - std::abs(x - centerX) / halfWidth));
            if (fade <= 0.02) continue;
            ctx->PushState();
            ctx->SetAlpha(fade);
            double length = tick.second ? style.tickLength : style.minorTickLength;
            ctx->SetFillPaint(tickColor);
            ctx->FillRectangle(Rect2Dd(std::round(x) - 0.5, baseline - length, 1.0, length));
            if (tick.second && showTickLabels) {
                std::string text = GetTickLabel(tick.first);
                Point2Di size = ctx->GetTextDimension(text);
                ctx->SetTextPaint(IsDisabled() ? style.disabledTextColor : style.tickLabelColor);
                ctx->DrawText(text, Point2Dd(x - size.x / 2.0, baseline + 3.0));
            }
            ctx->PopState();
        }

        // The fixed marker: a small triangle and a line down to the scale
        const Color marker = GetCurrentActiveTrackColor();
        ctx->SetFillPaint(marker);
        ctx->FillLinePath({ Point2Dd(centerX - 5.0, markerTop), Point2Dd(centerX + 5.0, markerTop),
                            Point2Dd(centerX, markerTop + 6.0) });
        ctx->FillRectangle(Rect2Dd(centerX - 1.0, markerTop + 4.0, 2.0, baseline - markerTop - 2.0));

        if (showValue) {
            std::string text = GetDisplayText();
            Point2Di size = ctx->GetTextDimension(text);
            ctx->SetTextPaint(IsDisabled() ? style.disabledTextColor : style.textColor);
            ctx->DrawText(text, Point2Dd(centerX - size.x / 2.0, bounds.y));
        }
    }

    // ===== HANDLE =====

    void UltraCanvasSlider::RenderHandle(const Point2Di &position, IRenderContext *ctx, bool highlighted) {
        if (style.handleShape == SliderHandleShape::NoHandle) return;
        const bool isVertical = (orientation == SliderOrientation::Vertical);
        const double size = style.handleSize;
        const double handleRadius = size / 2.0;
        const double x = position.x, y = position.y;

        Color handleColor = (highlighted && !IsDisabled()) ? style.handleHoverColor
                                                           : GetCurrentHandleColor();

        // Which way a Pointer points: at the ticks, or down / right without any.
        const bool pointsBefore = (tickPosition == SliderTickPosition::Before);

        // The outline of the shape, moved by (dx, dy): for the fill, the
        // border and the shadow alike.
        auto outline = [&](double dx, double dy) -> std::vector<Point2Dd> {
            const double cx = x + dx, cy = y + dy;
            switch (style.handleShape) {
                case SliderHandleShape::Triangle:
                    return { Point2Dd(cx, cy - handleRadius), Point2Dd(cx - handleRadius, cy + handleRadius),
                             Point2Dd(cx + handleRadius, cy + handleRadius) };
                case SliderHandleShape::Diamond:
                    return { Point2Dd(cx, cy - handleRadius), Point2Dd(cx + handleRadius, cy),
                             Point2Dd(cx, cy + handleRadius), Point2Dd(cx - handleRadius, cy) };
                case SliderHandleShape::Pointer: {
                    // A body as wide as 0.7 of the handle, ending in a point.
                    const double w = size * 0.35;           // half the body's width
                    const double h = size * 0.6;            // half its length
                    std::vector<Point2Dd> p;
                    if (!isVertical) {
                        double s = pointsBefore ? -1.0 : 1.0;
                        p = { Point2Dd(cx - w, cy - s * h), Point2Dd(cx + w, cy - s * h),
                              Point2Dd(cx + w, cy + s * (h - w)), Point2Dd(cx, cy + s * h),
                              Point2Dd(cx - w, cy + s * (h - w)) };
                    } else {
                        double s = pointsBefore ? -1.0 : 1.0;
                        p = { Point2Dd(cx - s * h, cy - w), Point2Dd(cx - s * h, cy + w),
                              Point2Dd(cx + s * (h - w), cy + w), Point2Dd(cx + s * h, cy),
                              Point2Dd(cx + s * (h - w), cy - w) };
                    }
                    return p;
                }
                default:
                    return {};
            }
        };

        // A rectangle for the box-like shapes: Square, Block, Bar, FaderCap
        auto boxOf = [&](double dx, double dy) -> Rect2Dd {
            double w = size, h = size;
            switch (style.handleShape) {
                case SliderHandleShape::Block:    w = std::max(6.0, size * 0.5); break;
                case SliderHandleShape::Bar:      w = std::max(3.0, size / 4.0); h = size * 1.6; break;
                case SliderHandleShape::FaderCap: w = size; h = size * 2.0; break;
                default: break;
            }
            if (isVertical) std::swap(w, h);
            return Rect2Dd(x + dx - w / 2.0, y + dy - h / 2.0, w, h);
        };

        const SliderHandleShape shape = style.handleShape;
        const bool isBox = shape == SliderHandleShape::Square || shape == SliderHandleShape::Block ||
                           shape == SliderHandleShape::Bar || shape == SliderHandleShape::FaderCap;
        const bool isPolygon = shape == SliderHandleShape::Triangle || shape == SliderHandleShape::Diamond ||
                               shape == SliderHandleShape::Pointer;
        const double boxRadius = shape == SliderHandleShape::Square ? 0.0
                               : shape == SliderHandleShape::Bar ? std::max(1.5, size / 8.0)
                               : shape == SliderHandleShape::FaderCap ? 3.0 : 1.5;

        // The shadow: the shape twice, a little lower, in translucent black
        if (style.handleShadow && !IsDisabled()) {
            const Color shades[2] = { Color(0, 0, 0, 28), Color(0, 0, 0, 45) };
            const double drops[2] = { 2.5, 1.2 };
            for (int i = 0; i < 2; i++) {
                ctx->SetFillPaint(shades[i]);
                if (isBox) FillBar(ctx, boxOf(0.0, drops[i]), boxRadius + 1.0);
                else if (isPolygon) ctx->FillLinePath(outline(0.0, drops[i]));
                else ctx->FillCircle(Point2Dd(x, y + drops[i]), handleRadius + 0.5);
            }
        }

        ctx->SetFillPaint(handleColor);
        ctx->SetStrokePaint(GetCurrentHandleBorderColor());
        ctx->SetStrokeWidth(style.borderWidth);

        switch (shape) {
            case SliderHandleShape::Bar: {
                // A bar in the accent colour, the handle of the pill style
                ctx->SetFillPaint(GetCurrentActiveTrackColor());
                FillBar(ctx, boxOf(0.0, 0.0), boxRadius);
                break;
            }

            case SliderHandleShape::FaderCap: {
                Rect2Dd box = boxOf(0.0, 0.0);
                std::vector<GradientStop> stops = {
                    GradientStop(0.0, handleColor), GradientStop(0.5, handleColor.Darken(0.15f)),
                    GradientStop(1.0, handleColor) };
                auto grad = isVertical
                    ? ctx->CreateLinearGradientPattern(box.x, box.y, box.x, box.y + box.height, stops)
                    : ctx->CreateLinearGradientPattern(box.x, box.y, box.x + box.width, box.y, stops);
                ctx->SetFillPaint(grad);
                ctx->FillRoundedRectangle(box, boxRadius);
                ctx->DrawRoundedRectangle(box, boxRadius);
                // Ribs across the cap, and a line in the accent at its centre
                ctx->SetFillPaint(Color(0, 0, 0, 60));
                for (double o : { -6.0, -3.0, 3.0, 6.0 }) {
                    if (isVertical) ctx->FillRectangle(Rect2Dd(box.x + 4, std::round(y + o), box.width - 8, 1));
                    else ctx->FillRectangle(Rect2Dd(std::round(x + o), box.y + 4, 1, box.height - 8));
                }
                ctx->SetFillPaint(GetCurrentActiveTrackColor());
                if (isVertical) ctx->FillRectangle(Rect2Dd(box.x + 2, y - 1, box.width - 4, 2));
                else ctx->FillRectangle(Rect2Dd(x - 1, box.y + 2, 2, box.height - 4));
                break;
            }

            case SliderHandleShape::Square:
            case SliderHandleShape::Block: {
                Rect2Dd box = boxOf(0.0, 0.0);
                FillBar(ctx, box, boxRadius);
                if (boxRadius > 0.0) ctx->DrawRoundedRectangle(box, boxRadius);
                else ctx->DrawRectangle(box);
                break;
            }

            case SliderHandleShape::Triangle:
            case SliderHandleShape::Diamond:
            case SliderHandleShape::Pointer: {
                std::vector<Point2Dd> points = outline(0.0, 0.0);
                ctx->FillLinePath(points);
                ctx->DrawLinePath(points, true);
                break;
            }

            case SliderHandleShape::Circle:
            default:
                ctx->FillCircle(position, handleRadius);
                ctx->DrawCircle(position, handleRadius);
                break;
        }
    }

    // ===== VALUE DISPLAY =====

    void UltraCanvasSlider::RenderBubble(IRenderContext *ctx, const std::string &text, const Point2Di &anchor, bool vertical) {
        if (text.empty()) return;
        ctx->SetFontStyle(style.fontStyle);
        Point2Di size = ctx->GetTextDimension(text);
        const double padX = 6.0, padY = 3.0, tail = 5.0;
        const double w = size.x + 2 * padX, h = size.y + 2 * padY;
        Rect2Di bounds = GetLocalBounds();

        Rect2Dd box;
        std::vector<Point2Dd> pointer;
        if (vertical) {
            // To the right of the anchor, pointing back at it
            box = Rect2Dd(anchor.x + tail, anchor.y - h / 2.0, w, h);
            box.y = std::max<double>(bounds.y, std::min<double>(box.y, bounds.y + bounds.height - h));
            pointer = { Point2Dd(anchor.x, anchor.y), Point2Dd(anchor.x + tail + 1, anchor.y - tail),
                        Point2Dd(anchor.x + tail + 1, anchor.y + tail) };
        } else {
            // Above the anchor, pointing down at it, kept inside the element
            box = Rect2Dd(anchor.x - w / 2.0, anchor.y - tail - h, w, h);
            box.x = std::max<double>(bounds.x, std::min<double>(box.x, bounds.x + bounds.width - w));
            box.y = std::max<double>(bounds.y, box.y);
            pointer = { Point2Dd(anchor.x - tail, box.y + h - 1), Point2Dd(anchor.x + tail, box.y + h - 1),
                        Point2Dd(anchor.x, box.y + h + tail - 1) };
        }
        ctx->SetFillPaint(style.bubbleColor);
        ctx->FillRoundedRectangle(box, 4.0);
        ctx->FillLinePath(pointer);
        ctx->SetTextPaint(style.bubbleTextColor);
        ctx->DrawText(text, Point2Dd(box.x + padX, box.y + padY));
    }

    void UltraCanvasSlider::RenderValueDisplay(const Rect2Di &bounds, IRenderContext *ctx) {
        const bool isVertical = (orientation == SliderOrientation::Vertical);
        ctx->SetFontStyle(style.fontStyle);
        ctx->SetTextPaint(IsDisabled() ? style.disabledTextColor : style.textColor);

        // The hover preview: a hairline at the pointer and the value under it
        if (hoverPreview && hoverPosition >= 0 && !isDragging) {
            Rect2Di interior = GetSliderInteriorRect(bounds, isVertical);
            Point2Di at = isVertical ? Point2Di(interior.x + interior.width / 2, hoverPosition)
                                     : Point2Di(hoverPosition, interior.y + interior.height / 2);
            float value = ValueAtPosition(at);
            std::string text = hoverText ? hoverText(value) : FormatValue(value);
            ctx->SetFillPaint(style.bubbleColor);
            if (isVertical) ctx->FillRectangle(Rect2Dd(interior.x, hoverPosition - 0.5, interior.width, 1));
            else ctx->FillRectangle(Rect2Dd(hoverPosition - 0.5, interior.y, 1, interior.height));
            Point2Di anchor = isVertical ? Point2Di(interior.x + interior.width, hoverPosition)
                                         : Point2Di(hoverPosition, interior.y);
            RenderBubble(ctx, text, anchor, isVertical);
        }

        const bool bubble = valueDisplay == SliderValueDisplay::Bubble ||
                            (valueDisplay == SliderValueDisplay::BubbleWhileDragging && isDragging);
        if (bubble) {
            auto bubbleAt = [&](float value) {
                Point2Di at = GetHandlePosition(bounds, isVertical, value);
                int reach = (int)std::ceil(style.handleSize / 2.0f);
                if (style.handleShape == SliderHandleShape::Bar) reach = (int)std::ceil(style.handleSize * 0.8f);
                Point2Di anchor = isVertical ? Point2Di(at.x + reach, at.y) : Point2Di(at.x, at.y - reach);
                RenderBubble(ctx, FormatValue(value), anchor, isVertical);
            };
            if (isRangeMode) { bubbleAt(lowerValue); bubbleAt(upperValue); }
            else bubbleAt(currentValue);
            return;
        }
        if (valueDisplay == SliderValueDisplay::NoDisplay || valueDisplay == SliderValueDisplay::BubbleWhileDragging) return;
        if (valueDisplay == SliderValueDisplay::Tooltip && !showTooltip) return;

        ctx->SetFontStyle(style.fontStyle);
        ctx->SetTextPaint(IsDisabled() ? style.disabledTextColor : style.textColor);
        if (!isRangeMode) {
            std::string text = GetDisplayText();
            if (text.empty()) return;

            Point2Di textSize = ctx->GetTextDimension(text);
            Point2Di textPos = CalculateTextPosition(bounds, textSize, currentValue);

            if (valueDisplay == SliderValueDisplay::Tooltip && showTooltip) {
                Rect2Di tooltipRect(textPos.x - 4, textPos.y - 4, textSize.x + 8, textSize.y + 8);
                ctx->DrawFilledRectangle(tooltipRect, style.tooltipColor, 1.0, style.tooltipBorderColor);
                ctx->SetTextPaint(IsDisabled() ? style.disabledTextColor : style.textColor);
            }

            ctx->DrawText(text, textPos);
        } else {
            std::string lowerText = FormatValue(lowerValue);
            std::string upperText = FormatValue(upperValue);

            Point2Di lowerTextSize = ctx->GetTextDimension(lowerText);
            Point2Di lowerTextPos = CalculateTextPosition(bounds, lowerTextSize, lowerValue);
            ctx->DrawText(lowerText, lowerTextPos);

            Point2Di upperTextSize = ctx->GetTextDimension(upperText);
            Point2Di upperTextPos = CalculateTextPosition(bounds, upperTextSize, upperValue);
            ctx->DrawText(upperText, upperTextPos);
        }
    }

    std::string UltraCanvasSlider::FormatValue(float value) const {
        // A Field shows its name (the custom text) beside the number, so the
        // custom text replaces the value everywhere but there.
        if (!customText.empty() && sliderStyle != SliderStyle::Field) {
            return customText;
        }
        if (!stopLabels.empty() && step > 0.0f) {
            long index = std::lround((value - minValue) / step);
            if (index >= 0 && index < (long)stopLabels.size()) return stopLabels[(size_t)index];
        }

        if (valueDisplay == SliderValueDisplay::Percentage) {
            float percentage = ((value - minValue) / (maxValue - minValue)) * 100.0f;
            return std::to_string(static_cast<int>(percentage)) + "%";
        }
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), valueFormat.c_str(), value);
        return std::string(buffer);
    }

    Color UltraCanvasSlider::GetCurrentTrackColor() const {
        return IsDisabled() ? style.disabledTrackColor : style.trackColor;
    }

    Color UltraCanvasSlider::GetCurrentActiveTrackColor() const {
        return IsDisabled() ? style.disabledActiveTrackColor : style.activeTrackColor;
    }

    Color UltraCanvasSlider::GetCurrentHandleBorderColor() const {
        // A washed-out border so a disabled handle reads as grey, not crisp.
        return IsDisabled() ? Color(170, 170, 175) : style.handleBorderColor;
    }

    Color UltraCanvasSlider::GetCurrentHandleColor() const {
        switch(GetPrimaryState()) {
            case ElementState::Disabled: return style.handleDisabledColor;
            case ElementState::Pressed: return style.handlePressedColor;
            case ElementState::Hovered: return style.handleHoverColor;
            default: return style.handleColor;
        }
    }

    Point2Di UltraCanvasSlider::CalculateTextPosition(const Rect2Di &bounds, const Point2Di &textSize, float value) const {
        bool isVertical = (orientation == SliderOrientation::Vertical);

        if (valueDisplay == SliderValueDisplay::Tooltip) {
            Point2Di handlePos = GetHandlePosition(bounds, isVertical, value);
            if (isVertical) {
                return Point2Di(handlePos.x + (int)style.handleSize / 2 + 8,
                                handlePos.y - textSize.y / 2);
            } else {
                return Point2Di(handlePos.x - textSize.x / 2,
                                handlePos.y - (int)style.handleSize / 2 - textSize.y - 2);
            }
        }

        Rect2Di interior = GetSliderInteriorRect(bounds, isVertical);
        Point2Di handlePos = GetHandlePosition(bounds, isVertical, value);
        if (isVertical) {
            return Point2Di(interior.x + interior.width + 2 + TickExtent(false), handlePos.y - textSize.y / 2);
        } else {
            return Point2Di(handlePos.x - textSize.x / 2, interior.y - TickExtent(true) - textSize.y - 2);
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

            dragStartValue = (activeHandle == RangeHandle::Lower) ? lowerValue : upperValue;
        } else {
            // Single handle mode
            dragStartValue = currentValue;
        }

        dragStartPos = mousePos;
        isDragging = true;
        hoverPosition = -1;
        SetFocus(true);
        // A knob or a ruler is turned by dragging, not set by where the
        // press lands: the value moves with the pointer from where it was.
        if (isRangeMode || !UsesRelativeDrag()) {
            UpdateValueFromPosition(mousePos);
        }
        RequestRedraw();

        if (onPress) onPress(event);

        if (auto* app = UltraCanvasApplication::GetInstance()) {
            app->CaptureMouse(this);
        }

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

        // The hover preview follows the pointer along the track
        if (hoverPreview) {
            int position = -1;
            if (Contains(mousePos)) {
                position = (orientation == SliderOrientation::Vertical) ? mousePos.y : mousePos.x;
            }
            if (position != hoverPosition) {
                hoverPosition = position;
                RequestRedraw();
            }
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
            RequestRedraw();

            // Report the value the drag settled on. SetValue() hands the
            // intermediate values to onValueChanging while the drag is live and
            // fires onValueChanged only when it is not dragging, so a handler
            // that acts on the committed value - a filter preview that is too
            // costly to re-run per pointer move, say - would otherwise never
            // hear the result of a drag at all. Range mode has no such gap: its
            // callbacks fire from SetLowerValue / SetUpperValue either way.
            if (!isRangeMode && onValueChanged && std::abs(currentValue - dragStartValue) > 0.001f) {
                onValueChanged(currentValue);
            }

            if (onRelease) onRelease(event);
            if (onClick) onClick(event);

            return true;
        }

        return false;
    }

    bool UltraCanvasSlider::HandleWheel(const UCEvent &event) {
        // Only the controls that are turned rather than slid take the wheel:
        // a linear slider in a scrolling page would otherwise catch the
        // page's scroll whenever the pointer passed over it.
        if (isRangeMode || !UsesRelativeDrag()) return false;
        if (!IsHovered() && !IsFocused()) return false;
        if (event.wheelDelta == 0) return false;
        float increment = step > 0 ? step : (maxValue - minValue) / 100.0f;
        SetValue(currentValue + (event.wheelDelta > 0 ? increment : -increment));
        return true;
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

                default:
                    break;
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

                default:
                    break;
            }
        }

        return false;
    }


    void UltraCanvasSlider::UpdateValueFromPosition(const Point2Di &pos) {
        // pos is element-local, so map against local bounds (origin 0,0), not finalBounds.
        float newValue;

        if (!isRangeMode && IsRotary()) {
            // Dragging up turns a knob up: the whole range over 200 pixels.
            float delta = float(dragStartPos.y - pos.y);
            newValue = dragStartValue + delta * (maxValue - minValue) / 200.0f;
        } else if (!isRangeMode && sliderStyle == SliderStyle::Ruler) {
            // The scale follows the pointer, so the value moves the other way.
            newValue = dragStartValue - float(pos.x - dragStartPos.x) / pixelsPerUnit;
        } else {
            newValue = ValueAtPosition(pos);
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

    SliderVisualStyle SliderVisualStyle::FromTheme(const UltraCanvasTheme& theme, const SliderVisualStyle& base) {
        SliderVisualStyle s = base;
        const ThemeRoles& r = theme.roles;
        // the value's text at the caption size
        const FontStyle caption = theme.Font(ThemeTextRole::Caption);
        s.fontStyle.fontFamily = caption.fontFamily;
        s.fontStyle.fontSize = caption.fontSize;
        s.trackColor = r.pressed;
        s.activeTrackColor = r.accent;
        s.disabledTrackColor = r.line;
        s.disabledActiveTrackColor = r.inkFaint;
        s.rangeTrackColor = r.accent.WithAlpha(180);
        // the element's own roles, for a design whose handle is not a raised surface
        s.handleColor = theme.Role("slider.handle", r.raised);
        s.handleBorderColor = theme.Role("slider.handle-border", r.lineStrong);
        s.handleHoverColor = r.hover;
        s.handlePressedColor = r.pressed;
        s.handleDisabledColor = r.panel;
        s.textColor = r.ink;
        s.disabledTextColor = r.inkFaint;
        // ticks and their labels in muted ink; the bubble in ink with the
        // ground's colour for its text, so it stands out in either look
        s.tickColor = r.inkMuted;
        s.tickLabelColor = r.inkMuted;
        s.bubbleColor = r.ink;
        s.bubbleTextColor = r.ground;
        s.tooltipColor = r.raised;
        s.tooltipBorderColor = r.lineStrong;
        s.secondaryTrackColor = r.inkFaint;
        s.histogramColor = r.line;
        s.histogramActiveColor = r.accent.WithAlpha(200);
        return s;
    }
} // namespace UltraCanvas
