// include/Plugins/Charts/Engine/Charts/UltraCanvasEnginePolarChart.h
// Polar chart on the chart engine - the port of UltraCanvasPolarChart:
// scatter, line, spline, area, spline-area and column series on an angle /
// radius coordinate system.
//
// The angle is the engine's domain axis (numeric degrees, or one slot per
// category) and the radius its value axis; the Polar projection turns them
// into the rim labels, the radial rule, the rings (circles or polygons) and
// the spokes. Lines and areas are the engine's line runs and area outlines
// (Engine/UltraCanvasChartSeries.h), columns its bar spans and outlines, the
// markers DrawChartMarker, radial and angular bands its ValueBand highlights
// (rings and wedges under Polar), stacking StackSeriesValues /
// BuildBarSpans. Legend toggling, tooltips, value labels and the grow-out
// animation are engine services.
//
// Version: 1.0.0
// Last Modified: 2026-10-20
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/Engine/UltraCanvasChartEngineElement.h"
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace UltraCanvas {

enum class EnginePolarSeriesType { Line, Spline, Area, SplineArea, Scatter, Column };

enum class EnginePolarAngleMode {
    Numeric,       // points carry their own angle
    Categorical    // points are spread one per category slot
};

enum class EnginePolarCategoryPlacement {
    OnSpokes,      // a category on a spoke (the radar look)
    BetweenSpokes  // categories centred in slots (the column look)
};

enum class EnginePolarRadialScale { Linear, Logarithmic, SquareRoot };

enum class EnginePolarStackMode { NoStacking, Stacked, PercentStacked };

struct EnginePolarPoint {
    double angle = 0.0;                  // Numeric mode only
    double radius = 0.0;
    std::string label;
    Color color = Color(0, 0, 0, 0);     // alpha 0 = the series colour

    EnginePolarPoint() = default;
    EnginePolarPoint(double a, double r, const std::string& pointLabel = std::string(),
                     const Color& pointColor = Color(0, 0, 0, 0))
        : angle(a), radius(r), label(pointLabel), color(pointColor) {}
};

struct EnginePolarSeries {
    std::string name;
    EnginePolarSeriesType type = EnginePolarSeriesType::Line;
    std::vector<EnginePolarPoint> points;
    Color color = Color(0, 0, 0, 0);     // alpha 0 = the theme palette's colour
    float fillOpacity = 0.35f;
    float lineWidth = 2.0f;
    bool dashed = false;
    ChartMarkerShape marker = ChartMarkerShape::Circle;
    float markerSize = 5.0f;
    bool closed = true;                  // join the last point back to the first
    bool visible = true;
    bool showValueLabels = false;
};

// A shaded ring (radial band) or wedge (angular band) behind the data.
struct EnginePolarBand {
    double from = 0.0;
    double to = 0.0;
    Color color = Color(200, 200, 200, 90);
    std::string label;
};

class UltraCanvasEnginePolarChart : public UltraCanvasChartEngineElement {
public:
    UltraCanvasEnginePolarChart(const std::string& id, int x = 0, int y = 0, int w = 0, int h = 0);

    // ---- series ------------------------------------------------------------
    void AddSeries(const EnginePolarSeries& series);
    void AddSeries(const std::string& name, EnginePolarSeriesType type,
                   const std::vector<std::pair<double, double>>& angleRadiusPairs,
                   const Color& color = Color(0, 0, 0, 0));
    void AddCategorySeries(const std::string& name, EnginePolarSeriesType type,
                           const std::vector<double>& values,
                           const Color& color = Color(0, 0, 0, 0));
    void ClearSeries();
    size_t GetSeriesCount() const { return seriesList.size(); }
    void SetSeriesType(size_t index, EnginePolarSeriesType type);
    void SetSeriesVisible(size_t index, bool visible);

    // ---- angular axis ------------------------------------------------------
    void SetAngleMode(EnginePolarAngleMode mode);
    void SetCategories(const std::vector<std::string>& categories);   // switches to Categorical
    void SetCategoryPlacement(EnginePolarCategoryPlacement placement);
    void SetAngleRange(double minAngle, double maxAngle);   // Numeric; default 0..360
    void SetZeroAngle(double degrees);                      // -90 = 12 o'clock (default)
    void SetClockwise(bool clockwise);
    void SetSweepAngle(double degrees);                     // 360 = full circle
    void SetAngleTickInterval(double degrees);              // 0 = automatic
    void SetAngleLabelFormatter(std::function<std::string(double)> formatter);
    void SetShowAngleLabels(bool show);

    // ---- radial axis -------------------------------------------------------
    void SetRadialScale(EnginePolarRadialScale scale);
    void SetRadialRange(double minValue, double maxValue);
    void ClearRadialRange();
    void SetRadialTickInterval(double interval);            // 0 = automatic
    void SetRadialTickCount(int count);
    void SetRadialAxisReversed(bool reversed);              // values grow inward
    void SetInnerRadiusFraction(float fraction);
    void SetShowRadialLabels(bool show);
    void SetRadialLabelUnit(const std::string& unit);       // suffix, e.g. " ppm"

    // ---- grid and bands ----------------------------------------------------
    // Ring shape: SetPolarGridShape (Circle / Polygon) on the engine.
    void SetShowRadialGrid(bool show);                      // the rings
    void SetShowAngularGrid(bool show);                     // the spokes
    void SetAlternatingBands(bool enabled,
                             const Color& colorA = Color(250, 250, 252, 255),
                             const Color& colorB = Color(240, 242, 246, 255));
    void AddRadialBand(double from, double to, const Color& color,
                       const std::string& label = std::string());
    void ClearRadialBands();
    void AddAngularBand(double fromAngle, double toAngle, const Color& color,
                        const std::string& label = std::string());
    void ClearAngularBands();

    // ---- series layout and style -------------------------------------------
    // Categorical mode: column and area series accumulate outward.
    void SetStackMode(EnginePolarStackMode mode);
    void SetColumnWidthFraction(float fraction);            // of the angular slot
    void SetColumnBorder(const Color& color, float width);
    // true: series take the theme palette, ignoring their own colours.
    void SetUseThemePalette(bool use);

    // ---- engine contract ---------------------------------------------------
    void DescribeAxes(ChartAxisSet& axes) override;
    void RenderChartContent(IRenderContext* ctx, const ChartEngineFrame& frame) override;
    void CollectChartLabels(IRenderContext* ctx, ChartLabelBroker& broker) override;
    void OnChartThemeChanged() override;
    void OnLegendEntryToggled(size_t entryIndex, bool enabled) override;

private:
    // One point of one series resolved into normalised space.
    struct PlacedPoint {
        size_t pointIndex = 0;
        double u = 0.0;
        double v = 0.0;          // the value edge
        double base = 0.0;       // the band's base (areas, columns)
        double radius = 0.0;     // the datum, for labels and tooltips
    };

    std::vector<EnginePolarSeries> seriesList;
    std::vector<std::string> categories;
    EnginePolarAngleMode angleMode = EnginePolarAngleMode::Numeric;
    EnginePolarCategoryPlacement categoryPlacement = EnginePolarCategoryPlacement::OnSpokes;
    double angleMin = 0.0, angleMax = 360.0;
    double zeroAngle = -90.0;
    bool clockwiseSweep = true;
    double sweepAngle = 360.0;
    double angleTickInterval = 0.0;
    std::function<std::string(double)> angleFormatter;
    bool showAngleLabels = true;

    EnginePolarRadialScale radialScale = EnginePolarRadialScale::Linear;
    bool manualRadialRange = false;
    double radialMin = 0.0, radialMax = 1.0;
    double radialTickInterval = 0.0;
    int radialTickCount = 5;
    bool radialReversed = false;
    bool showRadialLabels = true;
    std::string radialUnit;

    bool showRadialGrid = true;
    bool showAngularGrid = true;
    bool alternatingBands = false;
    Color alternatingA = Color(250, 250, 252, 255);
    Color alternatingB = Color(240, 242, 246, 255);
    std::vector<EnginePolarBand> radialBands;
    std::vector<EnginePolarBand> angularBands;

    EnginePolarStackMode stackMode = EnginePolarStackMode::NoStacking;
    float columnWidthFraction = 0.85f;
    Color columnBorderColor = Color(255, 255, 255, 200);
    float columnBorderWidth = 1.0f;
    bool useThemePalette = false;

    bool Categorical() const { return angleMode == EnginePolarAngleMode::Categorical; }
    bool FullCircle() const { return sweepAngle >= 359.9; }
    size_t CategoryCount() const;
    bool Stacks(const EnginePolarSeries& series) const;
    ChartAxis BuildRadialAxis() const;
    ChartAxis BuildAngleAxis() const;
    void RebuildHighlights();
    void RefreshLegend();
    Color SeriesColor(size_t index) const;
    std::string FormatAngle(const EnginePolarSeries& series, size_t pointIndex) const;
    // Every series' points in normalised space (before the animation).
    std::vector<std::vector<PlacedPoint>> PlaceSeries(const ChartAxis& radial,
                                                      const ChartAxis& angle) const;
    static int64_t RegionId(size_t seriesIndex, size_t pointIndex) {
        return static_cast<int64_t>(seriesIndex) * 65536 + static_cast<int64_t>(pointIndex);
    }
};

inline std::shared_ptr<UltraCanvasEnginePolarChart> CreateEnginePolarChart(
        const std::string& id, int x = 0, int y = 0, int w = 0, int h = 0) {
    return std::make_shared<UltraCanvasEnginePolarChart>(id, x, y, w, h);
}

} // namespace UltraCanvas
