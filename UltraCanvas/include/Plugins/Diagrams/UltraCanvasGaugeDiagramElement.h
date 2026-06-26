// Plugins/Gauges/UltraCanvasGaugeDiagramElement.h
// Comprehensive gauge element supporting analog, digital, progress, and specialized gauge modes
// Version: 2.0.0
// Last Modified: 2026-05-17
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasApplication.h"
#include <vector>
#include <string>
#include <functional>
#include <cmath>

namespace UltraCanvas {

// =============================================================================
// GAUGE TYPE ENUMERATIONS
// =============================================================================

enum class GaugeMode {
    // ===== ANALOG GAUGES =====
    Speedometer,        // 270-degree circular gauge with needle (e.g., car speedometer)
    Semicircular,       // 180-degree arc gauge with needle (e.g., temperature, sales)
    Quadrant,           // 90-degree arc gauge
    Compass,            // 360-degree compass with cardinal directions
    AnalogClock,        // Analog clock with hour/minute/second hands
    Stopwatch,          // Stopwatch with main display and optional sub-dial

    // ===== PROGRESS / BAR GAUGES =====
    LinearBar,          // Horizontal or vertical bar (e.g., download progress)
    LinearLED,          // Segmented LED-style bar (e.g., VU meter, signal strength)
    LinearSegmented,    // Brick-style segmented bar (e.g., business metrics)
    LinearMultiPointer, // Bar with multiple external pointers (e.g., recipe layers)
    LinearWithArrow,    // Bar with external arrow pointer and value label (e.g., blood test)
    LinearScale,        // Linear scale with multiple markers (e.g., radio tuner)
    CircularRing,       // 360-degree progress ring
    Battery,            // Battery shape with terminal
    Thermometer,        // Vertical thermometer with bulb
    Cylinder,           // 3D-style cylindrical bar

    // ===== DIGITAL GAUGES =====
    Digital             // LED/LCD numeric display
};

enum class GaugeNeedleStyle {
    Classic,
    Thin,
    Arrow,
    Triangle,
    NoNeedle        // 'None' avoided — X11 #defines None as a macro
};

enum class GaugeTickPosition {
    NoTicks,        // 'None' avoided — X11 #defines None as a macro
    Inside,
    Outside,
    Both
};

enum class GaugeOrientation {
    Horizontal,
    Vertical
};

enum class GaugeBatteryStyle {
    BarPointer,
    LedPointer
};

// ===== ROUND-GAUGE (CircularRing) STYLE ENUMS =====

// How the progress indicator around a circular ring is drawn.
enum class GaugeRingStyle {
    SolidArc,       // one smooth continuous arc (the classic ring)
    Segmented,      // a series of discrete chunks separated by small gaps
    Dashed,         // many fine ticks/dashes around the circle (tachymeter look)
    SegmentedRing,  // a few chunky arc segments around the circle with a large
                    // centre value and an optional icon/text label (battery look).
                    // Honours ringSegmentCount, ringSegmentRounded, ringBorder and
                    // the ring centre-content options below.
    Spectrum        // a smooth fade through up to 100 user colours (see
                    // SetRingGradientColors). The whole ring shows the colour
                    // scale faintly; the arc up to the value is drawn at full
                    // opacity, so the leading-edge colour pinpoints the value
                    // among up to 100 distinct levels (maximum-indication option).
};

// Content shown in the centre of a round gauge, beneath the value.
enum class GaugeRingCenterContent {
    NoContent,      // value only ('None' avoided — X11 #defines None as a macro)
    TextLabel,      // a short text label (e.g. "Battery")
    Icon            // a small icon glyph (e.g. a battery or bolt)
};

// Built-in icon glyphs usable as round-gauge centre content.
enum class GaugeRingIcon {
    Battery,        // horizontal battery body with a terminal and a level fill
    Bolt            // lightning bolt
};

// Shape of an individual segment when GaugeRingStyle is Segmented or Dashed.
enum class GaugeRingSegmentStyle {
    Bars,           // thin radial bars / ticks (e.g. the "62% ON TRACK" reference)
    Dots,           // rounded dots spaced around the ring
    Blocks          // chunky rounded arc blocks (e.g. the activity-ring look)
};

// Optional liquid-style fill that covers the disc surface inside the ring.
enum class GaugeFillStyle {
    NoFill,         // empty centre (classic ring)
    StraightLevel,  // flat liquid surface rising with the value
    WavedLevel      // wavy liquid surface (e.g. the "18 %" battery reference)
};

// =============================================================================
// GAUGE DATA STRUCTURES
// =============================================================================

struct GaugeRangeSegment {
    double startValue;
    double endValue;
    Color color;
    std::string label;

    GaugeRangeSegment() : startValue(0.0), endValue(0.0), color(128, 128, 128, 255) {}
    GaugeRangeSegment(double s, double e, const Color& c, const std::string& l = "")
        : startValue(s), endValue(e), color(c), label(l) {}
};

struct GaugeThreshold {
    double value;
    Color color;
    std::string label;

    GaugeThreshold() : value(0.0), color(255, 0, 0, 255) {}
    GaugeThreshold(double v, const Color& c, const std::string& l = "")
        : value(v), color(c), label(l) {}
};

struct GaugeExternalPointer {
    double value;
    Color color;
    std::string label;
    bool showOnLeft;

    GaugeExternalPointer() : value(0.0), color(80, 80, 90, 255), showOnLeft(false) {}
    GaugeExternalPointer(double v, const Color& c, const std::string& l, bool left = false)
        : value(v), color(c), label(l), showOnLeft(left) {}
};

struct GaugeSubDial {
    bool enabled = false;
    GaugeMode mode = GaugeMode::Semicircular;
    double currentValue = 0.0;
    double minValue = 0.0;
    double maxValue = 100.0;
    std::string title;
    std::string unit;
    double relativeSize = 0.3;
    double offsetXRatio = 0.0;
    double offsetYRatio = 0.2;
};

// =============================================================================
// MAIN GAUGE ELEMENT CLASS
// =============================================================================

class UltraCanvasGaugeDiagramElement : public UltraCanvasUIElement {
public:
    UltraCanvasGaugeDiagramElement(const std::string& id, float x, float y, float width, float height);
    ~UltraCanvasGaugeDiagramElement() override;

    bool AcceptsFocus() const override { return true; }
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;

    // ===== MODE & VALUE =====
    void SetMode(GaugeMode m);
    GaugeMode GetMode() const { return mode; }

    void SetValue(double val);
    double GetValue() const { return currentValue; }
    void SetMinValue(double v);
    double GetMinValue() const { return minValue; }
    void SetMaxValue(double v);
    double GetMaxValue() const { return maxValue; }

    // ===== TITLE & UNIT =====
    void SetTitle(const std::string& t);
    const std::string& GetTitle() const { return title; }
    void SetUnit(const std::string& u);
    const std::string& GetUnit() const { return unit; }

    // ===== VISUAL STYLE =====
    void SetNeedleStyle(GaugeNeedleStyle s);
    GaugeNeedleStyle GetNeedleStyle() const { return needleStyle; }
    void SetTickPosition(GaugeTickPosition p);
    GaugeTickPosition GetTickPosition() const { return tickPos; }
    void SetOrientation(GaugeOrientation o);
    GaugeOrientation GetOrientation() const { return orientation; }
    void SetBatteryStyle(GaugeBatteryStyle s);
    GaugeBatteryStyle GetBatteryStyle() const { return batteryStyle; }

    // ===== ROUND-GAUGE (CircularRing) STYLE =====
    void SetRingThickness(float t);
    float GetRingThickness() const { return ringThickness; }
    void SetRingStyle(GaugeRingStyle s);
    GaugeRingStyle GetRingStyle() const { return ringStyle; }
    void SetRingSegmentStyle(GaugeRingSegmentStyle s);
    GaugeRingSegmentStyle GetRingSegmentStyle() const { return ringSegmentStyle; }
    void SetRingSegmentCount(int count);
    int GetRingSegmentCount() const { return ringSegmentCount; }
    void SetRingSegmentRounded(bool rounded);
    bool GetRingSegmentRounded() const { return ringSegmentRounded; }
    void SetRingBorder(bool enabled);
    bool GetRingBorder() const { return ringBorder; }
    void SetRingBorderColor(const Color& c);
    const Color& GetRingBorderColor() const { return ringBorderColor; }
    void SetFillStyle(GaugeFillStyle s);
    GaugeFillStyle GetFillStyle() const { return fillStyle; }
    void SetTrackColor(const Color& c);
    const Color& GetTrackColor() const { return trackColor; }
    // Faded (soft, lightened) colours for the indicator ring and the centre fill.
    void SetRingFaded(bool faded);
    bool GetRingFaded() const { return ringFaded; }
    void SetFillFaded(bool faded);
    bool GetFillFaded() const { return fillFaded; }
    // Colour stops for the Spectrum ring style — set up to 100 colours that the
    // ring fades through (evenly spaced across the value range). More colours give
    // finer value indication. Lists longer than 100 are truncated.
    void SetRingGradientColors(const std::vector<Color>& colors);
    const std::vector<Color>& GetRingGradientColors() const { return ringGradientColors; }

    // Centre content for round gauges (value-only, text label or icon glyph).
    void SetRingCenterContent(GaugeRingCenterContent c);
    GaugeRingCenterContent GetRingCenterContent() const { return ringCenterContent; }
    void SetRingCenterLabel(const std::string& label);
    const std::string& GetRingCenterLabel() const { return ringCenterLabel; }
    void SetRingCenterIcon(GaugeRingIcon icon);
    GaugeRingIcon GetRingCenterIcon() const { return ringCenterIcon; }

    // Start angle of the round-gauge ring, in degrees, screen convention
    // (0 = 3 o'clock, +90 = 6 o'clock / bottom, -90 = 12 o'clock / top). The
    // value-zero ("null") position sits here and the indicator fills clockwise
    // from it. Defaults to -90 (top) to match the classic round gauge.
    void SetRingStartAngleDeg(float deg);
    float GetRingStartAngleDeg() const { return ringStartAngleDeg; }
    // When enabled, the lit indicator colour is derived from the current value
    // as a health band (green high -> yellow -> orange -> red low) instead of
    // the flat gauge colour. Useful for battery / dot-ring style indicators.
    void SetRingValueColorBands(bool enabled);
    bool GetRingValueColorBands() const { return ringValueColorBands; }

    // ===== ARC ANGLES =====
    void SetArcAngles(double startDeg, double endDeg);
    double GetArcStartAngle() const { return arcStartDeg; }
    double GetArcEndAngle() const { return arcEndDeg; }

    // ===== COLORS =====
    void SetGaugeColor(const Color& c);
    const Color& GetGaugeColor() const { return gaugeColor; }
    void SetBackgroundColor(const Color& c);
    const Color& GetBackgroundColor() const { return bgColor; }
    void SetTextColor(const Color& c);
    const Color& GetTextColor() const { return textColor; }

    // ===== RANGES & THRESHOLDS =====
    void AddRange(const GaugeRangeSegment& range);
    void ClearRanges();
    const std::vector<GaugeRangeSegment>& GetRanges() const { return ranges; }

    void AddThreshold(const GaugeThreshold& t);
    void ClearThresholds();
    const std::vector<GaugeThreshold>& GetThresholds() const { return thresholds; }

    // ===== EXTERNAL POINTERS =====
    void AddExternalPointer(const GaugeExternalPointer& p);
    void ClearExternalPointers();
    const std::vector<GaugeExternalPointer>& GetExternalPointers() const { return externalPointers; }

    // ===== SUB-DIAL =====
    void SetSubDial(const GaugeSubDial& sub);
    const GaugeSubDial& GetSubDial() const { return subDial; }
    void SetSubDialValue(double val);
    void SetSubDialEnabled(bool en);

    // ===== DISPLAY OPTIONS =====
    void SetDecimalPlaces(int places);
    int GetDecimalPlaces() const { return decimalPlaces; }
    void SetShowGlow(bool glow);
    bool GetShowGlow() const { return showGlow; }
    void SetShowBolt(bool show);
    bool GetShowBolt() const { return showBolt; }
    void SetShowLabels(bool show);
    bool GetShowLabels() const { return showLabels; }

    // ===== DIGITAL DISPLAY =====
    // When enabled (Digital mode), the panel shows the live wall-clock time
    // (HH:MM:SS) instead of the value, ticking once a second.
    void SetDigitalClock(bool en);
    bool GetDigitalClock() const { return digitalClock; }
    // Font family used by the Digital panel. Accepts a comma-separated fallback
    // list (e.g. "DSEG7 Classic,Monospace") so an LED-style font is used when
    // installed and a guaranteed family is used otherwise.
    void SetDigitalFontFamily(const std::string& family);
    const std::string& GetDigitalFontFamily() const { return digitalFontFamily; }

    // ===== STOPWATCH CONTROLS =====
    void StopwatchStart();
    void StopwatchStop();
    void StopwatchReset();
    bool IsStopwatchRunning() const { return stopwatchRunning; }

    // ===== TICK CONFIGURATION =====
    void SetMajorTickCount(int count);
    int GetMajorTickCount() const { return majorTickCount; }
    void SetMinorTickCount(int count);
    int GetMinorTickCount() const { return minorTickCount; }

    // ===== LED SEGMENTS =====
    void SetSegmentCount(int count);
    int GetSegmentCount() const { return segmentCount; }

    // ===== CALLBACKS =====
    std::function<void(double)> onGaugeValueChange;

private:
    GaugeMode mode = GaugeMode::Speedometer;
    double currentValue = 0.0;
    double minValue = 0.0;
    double maxValue = 100.0;

    GaugeNeedleStyle needleStyle = GaugeNeedleStyle::Classic;
    GaugeTickPosition tickPos = GaugeTickPosition::Both;
    GaugeOrientation orientation = GaugeOrientation::Horizontal;
    GaugeBatteryStyle batteryStyle = GaugeBatteryStyle::BarPointer;

    // Round-gauge (CircularRing) styling
    float ringThickness = 6.0f;
    GaugeRingStyle ringStyle = GaugeRingStyle::SolidArc;
    GaugeRingSegmentStyle ringSegmentStyle = GaugeRingSegmentStyle::Blocks;
    int ringSegmentCount = 36;
    bool ringSegmentRounded = true;                  // rounded vs sharp segment ends
    bool ringBorder = false;                         // draw an outline around segments
    Color ringBorderColor = Color(40, 40, 50, 255);  // colour of that outline
    GaugeFillStyle fillStyle = GaugeFillStyle::NoFill;
    Color trackColor = Color(220, 221, 230, 255);
    bool ringFaded = false;   // draw the indicator with a soft lightened gradient
    bool fillFaded = false;   // draw the centre fill with a pale lightened tint
    float ringStartAngleDeg = -90.0f;  // ring zero/null position (-90 = top)
    bool ringValueColorBands = false;  // colour the indicator by value (green->red)
    std::vector<Color> ringGradientColors;  // Spectrum-style colour stops (<=100)

    // Round-gauge centre content (drawn beneath the centre value).
    GaugeRingCenterContent ringCenterContent = GaugeRingCenterContent::NoContent;
    std::string ringCenterLabel;
    GaugeRingIcon ringCenterIcon = GaugeRingIcon::Battery;

    std::string title;
    std::string unit;

    double arcStartDeg = 135.0;
    double arcEndDeg = 405.0;

    Color gaugeColor = Color(0, 122, 255, 255);
    Color bgColor = Color(238, 239, 245, 255);
    Color textColor = Color(55, 55, 70, 255);

    std::vector<GaugeRangeSegment> ranges;
    std::vector<GaugeThreshold> thresholds;
    std::vector<GaugeExternalPointer> externalPointers;
    GaugeSubDial subDial;

    int decimalPlaces = 0;
    bool digitalClock = false;
    std::string digitalFontFamily = "Sans";
    bool showGlow = true;
    bool showBolt = false;
    bool showLabels = true;
    int majorTickCount = 6;
    int minorTickCount = 4;
    int segmentCount = 10;
    uint32_t clockTimerId = 0;
    bool stopwatchRunning = false;
    float stopwatchAccumulated = 0.0f;
    uint32_t stopwatchTimerId = 0;

    // ===== HELPERS =====
    double ValueToAngle(double val) const;
    double ValueToRatio(double val) const;
    Point2Df AngleToPoint(double angleDeg, double radius, const Point2Df& center) const;
    Point2Df GetGaugeCenter() const;
    float GetGaugeRadius() const;
    Color GetColorForValue(double val) const;

    // ===== MODE-SPECIFIC RENDERERS =====
    void RenderSpeedometer(IRenderContext* ctx, const Point2Df& center, float radius);
    void RenderSemicircular(IRenderContext* ctx, const Point2Df& center, float radius);
    void RenderQuadrant(IRenderContext* ctx, const Point2Df& center, float radius);
    void RenderCompass(IRenderContext* ctx, const Point2Df& center, float radius);
    void RenderAnalogClock(IRenderContext* ctx, const Point2Df& center, float radius);
    void RenderStopwatch(IRenderContext* ctx, const Point2Df& center, float radius);
    void RenderLinearBar(IRenderContext* ctx);
    void RenderLinearLED(IRenderContext* ctx);
    void RenderLinearSegmented(IRenderContext* ctx);
    void RenderLinearMultiPointer(IRenderContext* ctx);
    void RenderLinearWithArrow(IRenderContext* ctx);
    void RenderLinearScale(IRenderContext* ctx);
    void RenderCircularRing(IRenderContext* ctx, const Point2Df& center, float radius);
    // CircularRing sub-renderers (round-gauge style system)
    void DrawRingTrackAndValue(IRenderContext* ctx, const Point2Df& center, float radius);
    void DrawRingLiquidFill(IRenderContext* ctx, const Point2Df& center, float innerRadius);
    void DrawRingCenterIcon(IRenderContext* ctx, const Point2Df& center, float size);
    // Builds a soft "faded" two-tone linear-gradient paint (light tint -> darker
    // shade) spanning the gauge circle, used when ringFaded / fillFaded is enabled.
    // The gradient is sized to the circle (center +/- radius) so the full light->dark
    // sweep is visible across the ring/disc rather than washed out over the whole card.
    std::shared_ptr<IPaintPattern> MakeFadedPaint(IRenderContext* ctx, const Color& base,
                                                  const Point2Df& center, float radius) const;
    // Samples the Spectrum colour list at t in [0,1] (linear interpolation between
    // adjacent stops). Falls back to gaugeColor when no colours are configured.
    Color SampleGradientColor(double t) const;
    void RenderBattery(IRenderContext* ctx);
    void RenderThermometer(IRenderContext* ctx);
    void RenderCylinder(IRenderContext* ctx);
    void RenderDigital(IRenderContext* ctx);

    // ===== DRAWING PRIMITIVES =====
    void DrawArcSection(IRenderContext* ctx, const Point2Df& center, float radius,
                        float startA, float endA, const Color& color, float thickness);
    void DrawNeedle(IRenderContext* ctx, const Point2Df& center, double angleDeg, float length);
    void DrawArcTicks(IRenderContext* ctx, const Point2Df& center, float radius,
                      double startAngle, double endAngle);
    void DrawValueText(IRenderContext* ctx, const std::string& text, const Point2Df& pos,
                       float fontSize, const Color& color, bool centered = true);
    void DrawTitleText(IRenderContext* ctx, const Point2Df& pos);
    void DrawUnitText(IRenderContext* ctx, const Point2Df& pos);
    void DrawSubDial(IRenderContext* ctx, const Point2Df& mainCenter, float mainRadius);
    void DrawExternalPointers(IRenderContext* ctx, float barX, float barY, float barW, float barH);
    void DrawThresholdMarkers(IRenderContext* ctx, const Point2Df& center, float radius);

    std::string FormatValue(double val) const;

    // Starts/stops the 1-second redraw timer needed by live displays
    // (AnalogClock, and Digital when in clock mode) based on current state.
    void UpdateClockTimer();
};

// =============================================================================
// FACTORY FUNCTION
// =============================================================================

std::shared_ptr<UltraCanvasGaugeDiagramElement> CreateGaugeDiagramElement(
    const std::string& id, long x, long y, long width, long height);

} // namespace UltraCanvas
