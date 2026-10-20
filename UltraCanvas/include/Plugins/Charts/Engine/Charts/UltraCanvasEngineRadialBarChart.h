// include/Plugins/Charts/Engine/Charts/UltraCanvasEngineRadialBarChart.h
// Radial bar ("ray") chart on the chart engine - the port of
// UltraCanvasRadialBarChart.
//
// Rays radiate from a base ring, their length proportional to the value; each
// series fills a contiguous angular sector, separated by a gap. Under the
// engine's Polar projection the domain (u) is the position around the fan and
// the value (v) the ray's length from the inner ring: SetPolarAngles gives the
// fan's start and sweep, SetPolarInnerRadius the base ring. The series names
// are the domain axis's tick labels, so the engine rings the fan with them;
// the ring guides are the value grid. The content draws the rays (stroked
// along the radius with butt, round, square or arrow caps, optionally fading
// toward the tip), the inner disc and the centre text. Peak labels ride the
// solved label plan; hover, tooltips and the grow-out animation are engine
// services.
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

enum class EngineRadialBarStyle {
    Bars,       // thick rays, thickness derived from the angular slot
    Lines       // thin uniform rays (the spoke / "radial line" look)
};

enum class EngineRadialBarCap { Butt, Round, Square, Arrow };

enum class EngineRadialBarNormalization {
    Global,     // one range across every series (comparable lengths)
    PerSeries   // each series scales to its own maximum
};

struct EngineRadialBarValue {
    std::string label;
    double value = 0.0;
    Color color = Color(0, 0, 0, 0);    // alpha 0 = the series colour

    EngineRadialBarValue() = default;
    EngineRadialBarValue(const std::string& valueLabel, double v,
                         const Color& valueColor = Color(0, 0, 0, 0))
        : label(valueLabel), value(v), color(valueColor) {}
};

// One angular sector: a named group of consecutive rays.
struct EngineRadialBarSeries {
    std::string name;
    Color color = Color(0, 0, 0, 0);    // alpha 0 = the theme palette's colour
    std::vector<EngineRadialBarValue> values;
};

class UltraCanvasEngineRadialBarChart : public UltraCanvasChartEngineElement {
public:
    UltraCanvasEngineRadialBarChart(const std::string& id, int x = 0, int y = 0,
                                    int w = 0, int h = 0);

    // ---- data --------------------------------------------------------------
    void AddSeries(const EngineRadialBarSeries& series);
    void AddSeries(const std::string& name,
                   const std::vector<std::pair<std::string, double>>& values,
                   const Color& color = Color(0, 0, 0, 0));
    void ClearSeries();
    size_t GetSeriesCount() const { return seriesList.size(); }

    // ---- geometry ----------------------------------------------------------
    void SetStartAngle(double degrees);            // -90 = 12 o'clock (default)
    void SetSweepAngle(double degrees);            // 10..360
    void SetInnerRadiusFraction(float fraction);   // the base ring, 0.05..0.9
    void SetSeriesGapAngle(double degrees);        // between neighbouring sectors
    void SetBarWidth(float pixels);                // 0 = from the angular slot

    // ---- value scaling -----------------------------------------------------
    void SetNormalization(EngineRadialBarNormalization mode);
    void SetValueRange(double minValue, double maxValue);
    void ClearValueRange();
    void SetMinBarLengthFraction(float fraction);  // keeps the smallest rays visible

    // ---- style -------------------------------------------------------------
    void SetBarStyle(EngineRadialBarStyle style);
    void SetCapStyle(EngineRadialBarCap cap);
    void SetGradientFade(bool on);                 // fade toward the tip
    // true: series take the theme palette, ignoring their own colours.
    void SetUseThemePalette(bool use);
    void SetShowRingGuides(bool show);
    void SetRingGuideCount(int count);
    // Inner disc; alpha 0 = the theme's plot-area colour / grid colour.
    void SetInnerCircleColor(const Color& fill, const Color& border);
    void SetCenterText(const std::string& text);

    // ---- labels and interaction --------------------------------------------
    void SetShowSeriesLabels(bool show);           // the series names round the fan
    void SetShowPeakLabels(bool show);             // "Max <v>" at each series' peak
    void SetValueFormatter(std::function<std::string(double)> formatter);
    void SetHoverHighlightEnabled(bool on);

    // ---- engine contract ---------------------------------------------------
    void DescribeAxes(ChartAxisSet& axes) override;
    void RenderChartContent(IRenderContext* ctx, const ChartEngineFrame& frame) override;
    void CollectChartLabels(IRenderContext* ctx, ChartLabelBroker& broker) override;
    void RenderInteractionOverlay(IRenderContext* ctx, const ChartEngineFrame& frame) override;
    void OnThemeChanged() override;

private:
    // One ray resolved into normalised space.
    struct Ray {
        size_t seriesIndex = 0;
        size_t valueIndex = 0;
        double u = 0.0;           // the ray's angle position
        double slot = 0.0;        // the angular slot it owns, in u
        double length = 0.0;      // 0..1 from the inner ring to the rim
    };
    struct Sector {
        size_t seriesIndex = 0;
        double u0 = 0.0, u1 = 0.0;
        size_t peakRay = SIZE_MAX;
    };

    std::vector<EngineRadialBarSeries> seriesList;
    double startAngle = -90.0;
    double sweepAngle = 360.0;
    double seriesGap = 3.0;
    float barWidth = 0.0f;
    EngineRadialBarNormalization normalization = EngineRadialBarNormalization::Global;
    bool manualRange = false;
    double manualMin = 0.0, manualMax = 1.0;
    float minBarLength = 0.06f;
    EngineRadialBarStyle barStyle = EngineRadialBarStyle::Bars;
    EngineRadialBarCap capStyle = EngineRadialBarCap::Round;
    bool gradientFade = true;
    bool useThemePalette = false;
    bool showRingGuides = false;
    int ringGuideCount = 3;
    Color innerFill = Color(0, 0, 0, 0);
    Color innerBorder = Color(0, 0, 0, 0);
    std::string centerText;
    bool showSeriesLabels = true;
    bool showPeakLabels = false;
    bool hoverHighlight = true;
    std::function<std::string(double)> valueFormatter;

    void Layout(std::vector<Ray>& rays, std::vector<Sector>& sectors) const;
    Color SeriesColor(size_t seriesIndex) const;
    Color RayColor(const Ray& ray) const;
    std::string FormatValue(double value) const;
    void RefreshLegend();
    static int64_t RegionId(size_t seriesIndex, size_t valueIndex) {
        return static_cast<int64_t>(seriesIndex) * 65536 + static_cast<int64_t>(valueIndex);
    }
};

inline std::shared_ptr<UltraCanvasEngineRadialBarChart> CreateEngineRadialBarChart(
        const std::string& id, int x = 0, int y = 0, int w = 0, int h = 0) {
    return std::make_shared<UltraCanvasEngineRadialBarChart>(id, x, y, w, h);
}

} // namespace UltraCanvas
