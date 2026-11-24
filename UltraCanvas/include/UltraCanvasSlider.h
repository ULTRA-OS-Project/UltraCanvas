// include/UltraCanvasSlider.h
// Interactive slider control with multiple styles, value display options, and dual-handle range support
// Version: 3.9.0 - tick marks and tick labels, stop labels, more handle shapes (Pointer, Bar,
//                  Block, FaderCap, NoHandle), a value bubble, a fill origin, a reset value,
//                  the Knob / Field / Seek / Ruler / Segmented styles, a buffered (secondary)
//                  value, a histogram, colour zones; the circular style draws its value arc
//                  over SetArcAngles; the track and fill round their ends with cornerRadius
// Version: 3.8.0 - FromTheme sets the value's font at the caption size; the SetStyle rule and the constructor tail
// Version: 3.7.1 - a slider that shows no value centres its bar across its box
// Version: 3.7.0 - the handle as element roles (slider.handle, slider.handle-border)
// Version: 3.6.0 - SliderVisualStyle::FromTheme, ApplyTheme
// Version: 3.5.0 - a slider to screen readers, with a value they can read and set
// Version: 3.3.1
// Last Modified: 2026-11-24
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
#include <vector>

namespace UltraCanvas {

// ===== SLIDER STYLE DEFINITIONS =====
    enum class SliderStyle {
        Horizontal,     // Classic horizontal bar
        Vertical,       // Classic vertical bar
        Circular,       // Circular/knob style
        Progress,       // Progress bar style
        Range,          // Range slider with two handles
        Knob,           // Rotary cap with a pointer line inside a ring of dots
        Field,          // A field that fills with the value: a name on the left, the number on the right
        Seek,           // Media timeline: thin line, buffered part, thumb shown on hover
        Ruler,          // Fixed centre marker; the scale slides under it
        Segmented       // A row (or column) of blocks lit up to the value, like a level meter
    };

    enum class SliderValueDisplay {
        NoDisplay,         // No value display
        Number,           // Numeric value
        Percentage,       // Percentage display
        Tooltip,          // Show on hover
        AlwaysVisible,    // Always visible (fixed X11 conflict)
        Bubble,           // In a balloon over the handle, always
        BubbleWhileDragging // In a balloon over the handle while it is dragged
    };

    // Where tick marks are drawn. Before is above a horizontal track and left
    // of a vertical one; After is below / right. OnTrack draws them inside the
    // track itself (with Dot, one dot per stop).
    enum class SliderTickPosition {
        NoTicks,
        Before,
        After,
        BothSides,
        OnTrack
    };

    enum class SliderTickStyle {
        Line,           // Straight marks; major ones longer than minor ones
        Ramp,           // Marks that grow from the minimum to the maximum, so the scale says "more"
        Dot             // Round dots
    };

    // A colour for the part of a Segmented slider (or its active fill) at and
    // above a fraction of the range, e.g. {0.75f, amber}, {0.9f, red}.
    struct SliderColorZone {
        float fromFraction = 0.0f;
        Color color;
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

    enum class SliderHandleShape {
        Circle,
        Square,
        Triangle,
        Diamond,
        Pointer,        // Five-sided thumb that points at the ticks
        Bar,            // Slim bar across the track (see SliderVisualStyle::handleGap)
        Block,          // Small upright block, as on a compact desktop trackbar
        FaderCap,       // Ribbed rectangular cap of a mixing-desk fader
        NoHandle        // No handle at all (a meter, or a Seek bar at rest)
    };

// ===== SLIDER VISUAL STYLE =====
    struct SliderVisualStyle {
        // Track colors
        Color trackColor = Color(200, 200, 200);
        Color activeTrackColor = Color(0, 120, 215);
        Color disabledTrackColor = Color(210, 210, 214);
        Color disabledActiveTrackColor = Color(175, 175, 180);  // replaces activeTrackColor when disabled
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

        // Ticks, their labels, the value bubble, the tooltip, the buffered
        // (secondary) part of a Seek track and the histogram
        Color tickColor = Color(110, 110, 110);
        Color tickLabelColor = Color(90, 90, 90);
        Color bubbleColor = Color(40, 40, 40);
        Color bubbleTextColor = Colors::White;
        Color tooltipColor = Color(255, 255, 220, 230);
        Color tooltipBorderColor = Color(128, 128, 128);
        Color secondaryTrackColor = Color(160, 160, 160);
        Color histogramColor = Color(205, 205, 210);
        Color histogramActiveColor = Color(0, 120, 215, 200);

        // The theme's roles: the pressed fill for the track, the accent for
        // the active part, the raised surface for the handle, ink (muted ink
        // for ticks, ink for the bubble), the line for the histogram; the
        // element roles "slider.handle" and "slider.handle-border" where a
        // theme names them
        // over a base's own shape (sizes, fonts, flags), or a default one
        static SliderVisualStyle FromTheme(const UltraCanvasTheme& theme, const SliderVisualStyle& base);
        static SliderVisualStyle FromTheme(const UltraCanvasTheme& theme) { return FromTheme(theme, SliderVisualStyle()); }
        static SliderVisualStyle FromTheme() { return FromTheme(UltraCanvasTheme::Current()); }

        // Dimensions
        float trackHeight = 6.0f;
        float handleSize = 16.0f;
        float borderWidth = 1.0f;
        // Rounds the track, its fill and the Progress / Field bar (capped at
        // half the thickness, so 3 on a 6 px track gives round ends); 0 is square.
        float cornerRadius = 3.0f;
        SliderHandleShape handleShape = SliderHandleShape::Circle;
        // A soft shadow under the handle
        bool handleShadow = false;
        // Space left clear of the track either side of a Bar handle
        float handleGap = 0.0f;
        // A sunken, bevelled track: a dark top edge and a light bottom one
        bool trackInset = false;
        // Tick lengths (a Ramp grows up to tickLength) and the space between
        // the track's edge and the ticks
        float tickLength = 6.0f;
        float minorTickLength = 3.0f;
        float tickGap = 3.0f;

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
        // Snap increment. 0 == continuous (every position the track offers).
        // Left alone it follows the range (see SetRange / DefaultStepForRange):
        // whole units for a range wide enough for them, continuous otherwise —
        // a fractional range like 0..2 would otherwise snap to three positions.
        float step = 1.0f;
        // Set by SetStep(): the caller stated the increment, so SetRange() must
        // not derive one over it.
        bool stepExplicit = false;
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

        // ===== TICKS AND LABELS =====
        SliderTickPosition tickPosition = SliderTickPosition::NoTicks;
        SliderTickStyle tickStyle = SliderTickStyle::Line;
        float majorTickInterval = 0.0f;   // 0: derived from the step or the range
        float minorTickInterval = 0.0f;   // 0: no minor ticks
        std::vector<float> customTickValues;
        bool showTickLabels = false;
        std::vector<std::string> stopLabels;
        // Measured in Render (the font is only known with a context): the
        // widest and tallest tick label, so the layout leaves them room.
        mutable int labelWidth = 0;
        mutable int labelHeight = 0;

        // ===== FILL ORIGIN, RESET =====
        bool hasFillOrigin = false;
        float fillOrigin = 0.0f;
        bool hasResetValue = false;
        float resetValue = 0.0f;

        // ===== ROTARY =====
        float arcStartAngle = 135.0f;     // degrees, clockwise from 3 o'clock
        float arcSweepAngle = 270.0f;

        // ===== SEEK, HISTOGRAM, SEGMENTS, RULER =====
        bool hasSecondaryValue = false;
        float secondaryValue = 0.0f;
        bool hoverPreview = false;
        int hoverPosition = -1;           // element-local main-axis position, -1 when not hovering
        std::vector<float> histogram;
        int segmentCount = 16;
        std::vector<SliderColorZone> colorZones;
        float pixelsPerUnit = 6.0f;

        // ===== OPTIONAL TRACK GRADIENT =====
        // When non-empty the linear track is painted with this colour gradient
        // (e.g. a full hue palette) instead of the solid track/active-track fill.
        std::vector<GradientStop> trackGradientStops;

    public:
        // A range narrower than this many whole units gets a continuous
        // default step: below ~20 positions a whole-unit slider is too coarse
        // to be a slider at all (0..2 would offer three).
        static constexpr float MinAutoStepSpan = 20.0f;

        // ===== CONSTRUCTORS (REQUIRED PATTERN) =====
        UltraCanvasSlider(const std::string& identifier, float x, float y, float w, float h);

        UltraCanvasSlider(const std::string& identifier, float w, float h)
            : UltraCanvasSlider(identifier, -1, -1, w, h) {}

        explicit UltraCanvasSlider(const std::string& identifier)
            : UltraCanvasSlider(identifier, -1, -1, -1, -1) {}

        // ===== VALUE MANAGEMENT =====
        void SetRange(float min, float max);
        void SetValue(float value);

        // ===== ACCESSIBILITY =====
        // A slider with its value, range and step; a screen reader can set the
        // value (callbacks fire as for a user's change). A range slider (two
        // handles) reports no single value.
        AccessibleRole GetAccessibleRole() const override { return AccessibleRole::Slider; }
        bool GetAccessibleRange(AccessibleRange& range) const override;
        bool SetAccessibleValue(double value) override;

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

        // Snap increment; 0 makes the slider continuous. Setting it also stops
        // SetRange() from deriving one, so an integer control keeps whole steps
        // whatever range it is later given.
        void SetStep(float stepValue) {
            step = std::max(0.0f, stepValue);
            stepExplicit = true;
        }
        float GetStep() const { return step; }
        // The increment used when the caller sets none: whole units once the
        // range spans at least MinAutoStepSpan of them, continuous below that.
        static float DefaultStepForRange(float min, float max) {
            return (max - min) >= MinAutoStepSpan ? 1.0f : 0.0f;
        }

        // True while the user is actively dragging the handle. Lets owners
        // suppress programmatic SetValue() feedback (e.g. a media element pushing
        // playback position) from fighting the user's drag.
        bool IsDragging() const { return isDragging; }

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

        // ===== TICK MARKS =====
        // Where ticks go (NoTicks by default) and how they look.
        void SetTickPosition(SliderTickPosition position) { tickPosition = position; RequestRedraw(); }
        SliderTickPosition GetTickPosition() const { return tickPosition; }
        void SetTickStyle(SliderTickStyle tickStyleValue) { tickStyle = tickStyleValue; RequestRedraw(); }
        SliderTickStyle GetTickStyle() const { return tickStyle; }
        // A major tick every `major` units, and a minor one every `minor`
        // (0: none). Left at 0, the major interval is the step when that gives
        // at most 20 ticks, a tenth of the range otherwise.
        void SetTickInterval(float major, float minor = 0.0f) {
            majorTickInterval = std::max(0.0f, major);
            minorTickInterval = std::max(0.0f, minor);
            RequestRedraw();
        }
        float GetMajorTickInterval() const { return majorTickInterval; }
        float GetMinorTickInterval() const { return minorTickInterval; }
        // Ticks at these values only (all major), instead of an interval: the
        // 0 dB mark of a fader, or -100 / 0 / +100 of a balance control.
        void SetTickMarks(const std::vector<float>& values) { customTickValues = values; RequestRedraw(); }
        const std::vector<float>& GetTickMarks() const { return customTickValues; }
        // A label under (or beside) each major tick: the number, or what
        // tickLabelFormatter makes of it.
        void SetTickLabels(bool show) { showTickLabels = show; RequestRedraw(); }
        bool GetTickLabels() const { return showTickLabels; }
        // One name per step (min, min + step, ...): a tick and a label each,
        // and the value text names the stop. SetRange(0, 6) with SetStep(1)
        // and seven names makes a size picker.
        void SetStopLabels(const std::vector<std::string>& labels) { stopLabels = labels; RequestRedraw(); }
        const std::vector<std::string>& GetStopLabels() const { return stopLabels; }
        // The value a tick at `value` is labelled with: the formatter, the
        // stop's name, or the number.
        std::string GetTickLabel(float value) const;
        // The tick positions, in value units, with whether each is major.
        std::vector<std::pair<float, bool>> GetTickValues() const;

        // ===== FILL ORIGIN AND RESET =====
        // Fill the track from this value to the handle, not from the minimum:
        // 0 on a -100..100 balance control fills outward from the middle.
        void SetFillOrigin(float value) { fillOrigin = value; hasFillOrigin = true; RequestRedraw(); }
        void ClearFillOrigin() { hasFillOrigin = false; RequestRedraw(); }
        bool HasFillOrigin() const { return hasFillOrigin; }
        float GetFillOrigin() const { return fillOrigin; }
        // A double-click puts the value back to this one.
        void SetResetValue(float value) { resetValue = value; hasResetValue = true; }
        void ClearResetValue() { hasResetValue = false; }
        bool HasResetValue() const { return hasResetValue; }
        float GetResetValue() const { return resetValue; }

        // ===== ROTARY (Circular, Knob) =====
        // Where the arc starts, in degrees clockwise from 3 o'clock, and how
        // far it sweeps. The default, 135 and 270, leaves a gap at the bottom.
        void SetArcAngles(float startDegrees, float sweepDegrees) {
            arcStartAngle = startDegrees;
            arcSweepAngle = std::max(1.0f, std::min(360.0f, sweepDegrees));
            RequestRedraw();
        }
        float GetArcStartAngle() const { return arcStartAngle; }
        float GetArcSweepAngle() const { return arcSweepAngle; }

        // ===== SEEK =====
        // A second, lighter fill from the minimum: what a player has loaded.
        void SetSecondaryValue(float value) { secondaryValue = value; hasSecondaryValue = true; RequestRedraw(); }
        void ClearSecondaryValue() { hasSecondaryValue = false; RequestRedraw(); }
        bool HasSecondaryValue() const { return hasSecondaryValue; }
        float GetSecondaryValue() const { return secondaryValue; }
        // While the pointer is over the slider, a bubble shows the value
        // under it (hoverText, or the formatted value) and a hairline marks it.
        void SetHoverPreview(bool enabled) { hoverPreview = enabled; RequestRedraw(); }
        bool GetHoverPreview() const { return hoverPreview; }

        // ===== HISTOGRAM =====
        // Bars drawn behind the track, one per bucket across the range, in
        // the free space above a horizontal slider: where the data bunches up.
        // The bars under the selection (or the fill) take histogramActiveColor.
        void SetHistogram(const std::vector<float>& bucketCounts) { histogram = bucketCounts; RequestRedraw(); }
        void ClearHistogram() { histogram.clear(); RequestRedraw(); }
        const std::vector<float>& GetHistogram() const { return histogram; }

        // ===== SEGMENTED =====
        void SetSegmentCount(int count) { segmentCount = std::max(2, count); RequestRedraw(); }
        int GetSegmentCount() const { return segmentCount; }
        // Colours by fraction of the range; below the first zone the active
        // track colour is used. Also colours the fill of a linear slider.
        void SetColorZones(const std::vector<SliderColorZone>& zones) { colorZones = zones; RequestRedraw(); }
        const std::vector<SliderColorZone>& GetColorZones() const { return colorZones; }

        // ===== RULER =====
        // How far apart one unit of value is drawn on the moving scale.
        void SetPixelsPerUnit(float pixels) { pixelsPerUnit = std::max(0.5f, pixels); RequestRedraw(); }
        float GetPixelsPerUnit() const { return pixelsPerUnit; }

        // ===== TRACK GRADIENT (e.g. colour palette inside the bar) =====
        void SetTrackGradient(const std::vector<GradientStop>& stops) {
            trackGradientStops = stops;
            RequestRedraw();
        }
        void ClearTrackGradient() {
            trackGradientStops.clear();
            RequestRedraw();
        }
        bool HasTrackGradient() const { return !trackGradientStops.empty(); }

        SliderVisualStyle& GetStyle() { return style; }
        const SliderVisualStyle& GetStyle() const { return style; }
        void SetStyle(const SliderVisualStyle& st) {
            StyleSetByHand();
            style = st;
        }

        // ===== RENDERING (REQUIRED OVERRIDE) =====
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;

        // ===== EVENT HANDLING (REQUIRED OVERRIDE) =====
        bool OnEvent(const UCEvent& event) override;
        void ApplyTheme(const UltraCanvasTheme& theme) override { SetStyle(SliderVisualStyle::FromTheme(theme, GetStyle())); }

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
        void RenderKnobSlider(const Rect2Di& bounds, IRenderContext* ctx);
        void RenderFieldSlider(const Rect2Di& bounds, IRenderContext* ctx);
        void RenderSeekSlider(const Rect2Di& bounds, IRenderContext* ctx);
        void RenderRulerSlider(const Rect2Di& bounds, IRenderContext* ctx);
        void RenderSegmentedSlider(const Rect2Di& bounds, IRenderContext* ctx);
        void RenderTicks(const Rect2Di& bounds, IRenderContext* ctx);
        void RenderHistogram(const Rect2Di& bounds, IRenderContext* ctx);
        void RenderBubble(IRenderContext* ctx, const std::string& text, const Point2Di& anchor, bool vertical);
        void RenderTrackBar(IRenderContext* ctx, const Rect2Di& trackRect, bool isVertical);
        void MeasureTickLabels(IRenderContext* ctx) const;
        // The cross-axis room the ticks and labels need on one side of the bar
        int TickExtent(bool beforeSide) const;
        // The room a label sticks out past the handle at either end
        int LabelOverhang() const;
        bool IsRotary() const { return sliderStyle == SliderStyle::Circular || sliderStyle == SliderStyle::Knob; }
        bool UsesRelativeDrag() const { return IsRotary() || sliderStyle == SliderStyle::Ruler; }
        float ValueAtPosition(const Point2Di& pos) const;
        Color ZoneColor(float fraction) const;
        std::string FormatNumber(float value) const;

        std::string FormatValue(float value) const;

        Rect2Di GetSliderInteriorRect(const Rect2Di& bounds, bool isVertical) const;
        Rect2Di GetTrackRect(const Rect2Di& bounds, bool isVertical) const;
        Rect2Di GetActiveTrackRect(const Rect2Di& trackRect, bool isVertical) const;
        Rect2Di GetRangeTrackRect(const Rect2Di& trackRect, bool isVertical) const;
        Point2Di GetHandlePosition(const Rect2Di& bounds, bool isVertical, float value) const;
        Color GetCurrentTrackColor() const;
        Color GetCurrentActiveTrackColor() const;
        Color GetCurrentHandleColor() const;
        Color GetCurrentHandleBorderColor() const;

        bool ShouldShowValueText() const {
            return (valueDisplay == SliderValueDisplay::AlwaysVisible) ||
                   (valueDisplay == SliderValueDisplay::Tooltip && showTooltip) ||
                   (valueDisplay == SliderValueDisplay::Bubble) ||
                   (valueDisplay == SliderValueDisplay::BubbleWhileDragging && isDragging) ||
                   (hoverPreview && hoverPosition >= 0) ||
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

        bool HandleWheel(const UCEvent& event);

        void UpdateValueFromPosition(const Point2Di& pos);

    public:
        // ===== CALLBACK EVENTS =====
        std::function<void(float)> onValueChanged;
        std::function<void(float)> onValueChanging; // Called during drag
        std::function<void(const UCEvent&)> onPress;
        std::function<void(const UCEvent&)> onRelease;
        std::function<void(const UCEvent&)> onClick;

        // ===== TEXT HOOKS =====
        // The label of a tick at a value (SetTickLabels); unset, the number.
        std::function<std::string(float)> tickLabelFormatter;
        // The text of the hover preview (SetHoverPreview); unset, the value
        // formatted as the value display formats it.
        std::function<std::string(float)> hoverText;

        // ===== RANGE MODE CALLBACKS =====
        std::function<void(float)> onLowerValueChanged;    // Lower handle value changed
        std::function<void(float)> onUpperValueChanged;    // Upper handle value changed
        std::function<void(float, float)> onRangeChanged;  // Range changed (both values)
    };

// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasSlider> CreateSlider(
            const std::string& identifier, float x, float y, float width, float height) {
        return std::make_shared<UltraCanvasSlider>(identifier, x, y, width, height);
    }

    inline std::shared_ptr<UltraCanvasSlider> CreateHorizontalSlider(
            const std::string& identifier, float x, float y, float width, float height,
            float min = 0.0f, float max = 100.0f) {
        auto slider = std::make_shared<UltraCanvasSlider>(identifier, x, y, width, height);
        slider->SetSliderStyle(SliderStyle::Horizontal);
        slider->SetRange(min, max);
        return slider;
    }

    inline std::shared_ptr<UltraCanvasSlider> CreateVerticalSlider(
            const std::string& identifier, float x, float y, float width, float height,
            float min = 0.0f, float max = 100.0f) {
        auto slider = std::make_shared<UltraCanvasSlider>(identifier, x, y, width, height);
        slider->SetSliderStyle(SliderStyle::Vertical);
        slider->SetRange(min, max);
        return slider;
    }

    inline std::shared_ptr<UltraCanvasSlider> CreateCircularSlider(
            const std::string& identifier, float x, float y, float size,
            float min = 0.0f, float max = 100.0f) {
        auto slider = std::make_shared<UltraCanvasSlider>(identifier, x, y, size, size);
        slider->SetSliderStyle(SliderStyle::Circular);
        slider->SetRange(min, max);
        return slider;
    }

    inline std::shared_ptr<UltraCanvasSlider> CreateRangeSlider(
            const std::string& identifier, float x, float y, float width, float height,
            float min = 0.0f, float max = 100.0f, float lower = 25.0f, float upper = 75.0f) {
        auto slider = std::make_shared<UltraCanvasSlider>(identifier, x, y, width, height);
        slider->SetSliderStyle(SliderStyle::Range);
        slider->SetRange(min, max);
        slider->SetRangeMode(true);
        slider->SetRangeValues(lower, upper);
        return slider;
    }

} // namespace UltraCanvas
