// include/Plugins/Charts/Engine/Charts/UltraCanvasEngineScatterChart.h
// Scatter and bubble charts on the chart engine: point groups on two axes,
// with an optional third dimension (bubble size, area- or
// diameter-proportional, keyed by the engine's size legend) and fourth
// (colour through a colormap, keyed by a colour bar), a least-squares trend
// line with its correlation readout, per-group confidence ellipses, and
// point labels through the solved label plan.
//
// One content class replaces UltraCanvasScatterPlotElement and the axis-based
// modes of UltraCanvasBubbleChartElement: ScatterBubbles (two numeric axes),
// BubbleMatrix (two categorical axes) and TimelineBubbles (categorical rows
// over a numeric x). Either axis is numeric or categorical
// (SetXCategories / SetYCategories). The packed and hierarchical bubble
// layouts have no axes and stay with the legacy element.
//
// Axis indices: 0 = y (the value), 1 = x (the projection's domain) - the
// engine's convention, shared with the highlight and span defaults.
//
// Version: 1.1.0 - AddChartHighlights() / RefreshHighlights(): a derived chart's own highlights
//   survive the rebuild every data, theme and legend change does
// Version: 1.0.0
// Last Modified: 2026-10-24
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/Engine/UltraCanvasChartEngineElement.h"
#include "Plugins/Charts/UltraCanvasColormap.h"
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

struct EngineScatterPoint {
    double x = 0.0;                    // a value, or a category index
    double y = 0.0;
    double size = std::nan("");        // bubble size channel; NaN = the plain marker radius
    double colorValue = std::nan("");  // colour channel; NaN = the group colour
    std::string label;                 // the point's name: tooltip and point label
    Color color = Color(0, 0, 0, 0);   // per-point override; alpha 0 = none

    EngineScatterPoint() = default;
    EngineScatterPoint(double xx, double yy, double sizeValue = std::nan(""),
                       double colorChannel = std::nan(""), const std::string& name = std::string())
        : x(xx), y(yy), size(sizeValue), colorValue(colorChannel), label(name) {}
};

struct EngineScatterGroup {
    std::string name;
    std::vector<EngineScatterPoint> points;
    Color color = Color(0, 0, 0, 0);   // alpha 0 = take it from the theme palette
    ChartMarkerShape marker = ChartMarkerShape::Circle;
};

enum class EngineBubbleScale {
    Area,       // value proportional to the circle's area (the honest default)
    Diameter    // value proportional to the diameter (exaggerates differences)
};

enum class EngineScatterLabelMode {
    Hidden,
    Outside,    // beside the mark, solved by the label plan (may be suppressed)
    Inside,     // centred in the mark when it fits, otherwise outside
};

class UltraCanvasEngineScatterChart : public UltraCanvasChartEngineElement {
public:
    UltraCanvasEngineScatterChart(const std::string& id, int x = 0, int y = 0, int w = 0, int h = 0);

    // ---- data ---------------------------------------------------------------
    void SetGroups(const std::vector<EngineScatterGroup>& groupList);
    void AddGroup(const EngineScatterGroup& group);
    // Appends a point to a group, creating groups up to the index as needed.
    void AddPoint(size_t groupIndex, const EngineScatterPoint& point);
    void ClearGroups();
    const std::vector<EngineScatterGroup>& GetGroups() const { return groups; }

    // ---- axes ----------------------------------------------------------------
    // Categorical axes: a point's x / y is then the category index.
    void SetXCategories(const std::vector<std::string>& names);
    void SetYCategories(const std::vector<std::string>& names);
    void SetXRange(double lo, double hi);
    void SetYRange(double lo, double hi);
    void SetAutoRanges();
    void SetAxisTitles(const std::string& xTitle, const std::string& yTitle);
    void SetXFormat(const std::string& prefix, const std::string& suffix,
                    bool compact = false, int decimals = -1);
    void SetYFormat(const std::string& prefix, const std::string& suffix,
                    bool compact = false, int decimals = -1);
    void SetXFormatter(ValueFormatter formatter);
    void SetYFormatter(ValueFormatter formatter);
    // Rule both directions (default) or the y grid only.
    void SetShowXGrid(bool show);

    // ---- marks ---------------------------------------------------------------
    void SetMarkerRadius(float radius);           // the plain (no size channel) radius
    void SetMarkerOpacity(float opacity);         // 0..1
    void SetMarkerOutline(const Color& color, float width);

    // ---- size channel (bubbles) ----------------------------------------------
    void SetRadiusRange(float minRadius, float maxRadius);
    void SetSizeScale(EngineBubbleScale scale);
    void SetSizeDomain(double minValue, double maxValue);
    void SetAutoSizeDomain();
    // Keys the size channel with the engine's nested size legend (it then
    // replaces the group legend). Empty values = min / mid / max.
    void SetShowSizeLegend(bool show, const std::string& title = std::string(),
                           const std::vector<double>& values = {});

    // ---- colour channel ------------------------------------------------------
    void SetColormap(HeatmapColormap colormap, bool reverse = false);
    void SetCustomColormap(const std::vector<Color>& anchors);
    void SetColorDomain(double minValue, double maxValue);
    void SetAutoColorDomain();
    // Keys the colour channel with a colour bar legend (replaces the group
    // legend; the size legend wins when both are asked for).
    void SetShowColorBar(bool show, const std::string& title = std::string());

    // ---- analysis ------------------------------------------------------------
    // Least-squares line through every visible point (numeric axes only).
    void SetShowTrendLine(bool show);
    bool GetShowTrendLine() const { return showTrendLine; }
    void SetTrendLineStyle(const Color& color, float width, bool dashed);
    // "y = ax + b   r = .., r² = .." at the end of the trend line.
    void SetShowCorrelationInfo(bool show);
    // A confidence ellipse around each group (engine highlight, slot 200).
    void SetShowConfidenceEllipses(bool show, double confidence = 0.95);

    // ---- labels --------------------------------------------------------------
    void SetPointLabelMode(EngineScatterLabelMode mode);

    // ---- engine contract -----------------------------------------------------
    void DescribeAxes(ChartAxisSet& axes) override;
    void RenderChartContent(IRenderContext* ctx, const ChartEngineFrame& frame) override;
    void CollectChartLabels(IRenderContext* ctx, ChartLabelBroker& broker) override;
    void OnChartThemeChanged() override;
    void OnLegendEntryToggled(size_t entryIndex, bool enabled) override;

protected:
    // Every data, theme and legend change clears the highlights and rebuilds
    // the chart's own (the confidence ellipses). A derived chart adds its
    // highlights here, so they are rebuilt with them instead of wiped -
    // drawn first, under the ellipses. RefreshHighlights() reruns the
    // rebuild when only the derived chart's highlights changed.
    virtual void AddChartHighlights() {}
    void RefreshHighlights() { RebuildHighlights(); }

private:
    struct AxisFormat {
        std::string prefix, suffix;
        bool compact = false;
        int decimals = -1;
        ValueFormatter formatter;
        std::vector<std::string> categories;
        bool explicitRange = false;
        double lo = 0.0, hi = 1.0;
        std::string title;
    };

    std::vector<EngineScatterGroup> groups;
    std::vector<bool> hidden;
    AxisFormat xFormat, yFormat;
    bool showXGrid = true;

    float markerRadius = 5.0f;
    float markerOpacity = 0.85f;
    Color outlineColor = Color(0, 0, 0, 0);
    float outlineWidth = 1.0f;

    float minRadius = 6.0f, maxRadius = 36.0f;
    EngineBubbleScale sizeScale = EngineBubbleScale::Area;
    bool autoSizeDomain = true;
    double sizeMin = 0.0, sizeMax = 1.0;
    bool showSizeLegend = false;
    std::string sizeLegendTitle;
    std::vector<double> sizeLegendValues;

    HeatmapColormap colormap = HeatmapColormap::Viridis;
    std::vector<Color> customColormap;
    bool colormapReversed = false;
    bool autoColorDomain = true;
    double colorMin = 0.0, colorMax = 1.0;
    bool showColorBar = false;
    std::string colorBarTitle;

    bool showTrendLine = false;
    Color trendColor = Color(220, 60, 60, 255);
    float trendWidth = 2.0f;
    bool trendDashed = true;
    bool showCorrelationInfo = false;
    bool showEllipses = false;
    double ellipseConfidence = 0.95;

    EngineScatterLabelMode labelMode = EngineScatterLabelMode::Hidden;

    bool Visible(size_t group) const { return !(group < hidden.size() && hidden[group]); }
    bool HasSizeChannel() const;
    bool HasColorChannel() const;
    void SizeDomain(double& lo, double& hi) const;
    void ColorDomain(double& lo, double& hi) const;
    double RadiusOf(const EngineScatterPoint& point) const;
    Color GroupColor(size_t group) const;
    Color PointColor(size_t group, const EngineScatterPoint& point) const;
    void ApplyFormat(ChartAxis& axis, const AxisFormat& format) const;
    void RefreshLegend();
    void RebuildHighlights();
    void DataChanged();
    std::string Tooltip(size_t group, const EngineScatterPoint& point,
                        const ChartAxis& x, const ChartAxis& y) const;
    static int64_t RegionId(size_t group, size_t index) {
        return static_cast<int64_t>(group) * 1000000 + static_cast<int64_t>(index);
    }
};

inline std::shared_ptr<UltraCanvasEngineScatterChart> CreateEngineScatterChart(
        const std::string& id, int x = 0, int y = 0, int w = 0, int h = 0) {
    return std::make_shared<UltraCanvasEngineScatterChart>(id, x, y, w, h);
}

} // namespace UltraCanvas
