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
};

// =============================================================================
// FACTORY FUNCTION
// =============================================================================

std::shared_ptr<UltraCanvasGaugeDiagramElement> CreateGaugeDiagramElement(
    const std::string& id, long x, long y, long width, long height);

} // namespace UltraCanvas
