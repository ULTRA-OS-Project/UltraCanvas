// include/Plugins/Charts/Engine/Charts/UltraCanvasEngineLineChart.h
// The line family on the chart engine: line, area, stacked area and 100%
// stacked area charts - and the cumulative flow diagram, which is a stacked
// area over periods with lead time / cycle time / WIP spans.
//
// One content class replaces UltraCanvasLineChartElement,
// UltraCanvasAreaChartElement and UltraCanvasCumulativeFlowChartElement. The
// engine supplies the axes, the grid, the measured layout, the legend (click
// an entry to hide its series), the solved value-label plan, per-point hover
// tooltips, themes and the entrance animation; this class adds the series
// geometry, which comes from the engine's line-family helpers
// (BuildLineRuns, BuildAreaOutlines, StackSeriesValues). Everything is drawn
// through the projection, so the same chart renders vertically, transposed
// (Horizontal) and as a polar line chart (Polar).
//
// Axis indices: 0 = value axis, 1 = domain axis (categories or numeric x).
//
// Version: 1.0.0
// Last Modified: 2026-10-20
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/Engine/UltraCanvasChartEngineElement.h"
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

enum class EngineLineChartMode {
    Line,                 // strokes only
    Area,                 // each series filled down to zero, overlapping
    StackedArea,          // bands piled up per domain position
    PercentStackedArea    // bands sharing out 100% per domain position
};

struct EngineLineSeries {
    std::string name;
    std::vector<double> values;               // one per category / x value; NaN = gap
    Color color = Color(0, 0, 0, 0);          // alpha 0 = take it from the theme palette
    ChartMarkerShape marker = ChartMarkerShape::Circle;
    float lineWidth = 0.0f;                   // 0 = the chart's line width
    bool dashed = false;
};

// A dimension line measured between the TOP edges of two series' bands - the
// cumulative flow diagram's annotations. AcrossDomain runs at value `at` from
// where the first series' edge reaches it to where the second's does (lead /
// cycle time); AcrossValue stands at domain position `at` (an index, or an x
// value) between the two edges (WIP). NaN `at` = automatic placement (half
// the final stack total / a third of the way along); a negative `at` on an
// AcrossDomain span is that fraction of the final total (-0.38 = 38%).
enum class EngineLineSpanKind { AcrossDomain, AcrossValue };

struct EngineLineSpan {
    EngineLineSpanKind kind = EngineLineSpanKind::AcrossDomain;
    size_t fromSeries = 0;
    size_t toSeries = 0;
    double at = std::nan("");
    std::string label;
};

class UltraCanvasEngineLineChart : public UltraCanvasChartEngineElement {
public:
    UltraCanvasEngineLineChart(const std::string& id, int x = 0, int y = 0, int w = 0, int h = 0);

    // ---- data -------------------------------------------------------------
    // A categorical domain (one slot per name) ...
    void SetCategories(const std::vector<std::string>& categories);
    // ... or a numeric one (one x per value index); clears the categories.
    void SetXValues(const std::vector<double>& xValues);
    void SetSeries(const std::vector<EngineLineSeries>& seriesList);
    void AddSeries(const EngineLineSeries& series);
    void ClearSeries();
    const std::vector<EngineLineSeries>& GetSeries() const { return series; }
    const std::vector<std::string>& GetCategories() const { return categories; }

    // ---- presentation ---------------------------------------------------
    void SetMode(EngineLineChartMode mode);
    EngineLineChartMode GetMode() const { return mode; }
    void SetLineShape(ChartLineShape shape);
    ChartLineShape GetLineShape() const { return lineOptions.shape; }
    // Join across missing values instead of breaking the line there.
    void SetBridgeGaps(bool bridge);
    void SetLineWidth(float width);
    void SetShowMarkers(bool show);
    bool GetShowMarkers() const { return showMarkers; }
    void SetMarkerRadius(float radius);
    // One marker colour for every series (alpha 0 = each series' own colour).
    void SetMarkerColor(const Color& color);
    // Fill opacity of the area modes, 0..1.
    void SetAreaOpacity(float opacity);
    // Area fills fade from the series colour at the top of the plot towards
    // the base (orthogonal projections; Polar keeps the flat fill).
    void SetGradientFill(bool gradient);
    // Stroke the top edge of each band in the area modes.
    void SetShowEdgeLines(bool show);
    void SetShowValueLabels(bool show);
    bool GetShowValueLabels() const { return showValueLabels; }
    // Value axis formatting; decimals -1 = from the range.
    void SetValueFormat(const std::string& prefix, const std::string& suffix,
                        bool compact = false, int decimals = -1);
    // Line mode only: keep zero on the value axis (area modes always do).
    void SetIncludeZero(bool include);
    // Slot padding of a categorical domain, in slots (0 = the first and last
    // points sit on the plot edges). Ignored under Polar, which spreads the
    // categories evenly round the circle.
    void SetCategoryPadding(double slots);
    void SetAxisTitles(const std::string& domainTitle, const std::string& valueTitle);
    // Stack the series from the last to the first, so series 0 is the TOP
    // band - the cumulative flow convention (first stage on top, Done at the
    // bottom). The legend keeps the given order.
    void SetReverseStackOrder(bool reverse);

    // ---- span annotations (stacked modes) ---------------------------------
    void AddSeriesSpan(const EngineLineSpan& span);
    void ClearSeriesSpans();
    // The cumulative flow trio for stages given top band first: lead time
    // (arrivals to Done), cycle time (start of work to Done) and WIP.
    void AddCumulativeFlowAnnotations(const std::string& leadLabel = "Lead Time",
                                      const std::string& cycleLabel = "Cycle Time",
                                      const std::string& wipLabel = "WIP");

    // ---- engine contract --------------------------------------------------
    void DescribeAxes(ChartAxisSet& axes) override;
    void RenderChartContent(IRenderContext* ctx, const ChartEngineFrame& frame) override;
    void MeasureContent(IRenderContext* ctx, ChartLayoutRequest& request) override;
    void CollectChartLabels(IRenderContext* ctx, ChartLabelBroker& broker) override;
    void OnChartThemeChanged() override;
    void OnLegendEntryToggled(size_t entryIndex, bool enabled) override;

private:
    std::vector<std::string> categories;
    std::vector<double> xValues;
    std::vector<EngineLineSeries> series;
    std::vector<bool> hidden;
    std::vector<EngineLineSpan> spans;

    EngineLineChartMode mode = EngineLineChartMode::Line;
    ChartLineOptions lineOptions;
    float lineWidth = 2.5f;
    bool showMarkers = true;
    float markerRadius = 4.0f;
    Color markerColor = Color(0, 0, 0, 0);
    float areaOpacity = 0.45f;
    bool gradientFill = false;
    bool showEdgeLines = true;
    bool showValueLabels = false;
    bool includeZero = false;
    bool reverseStack = false;
    double categoryPadding = 0.0;
    std::string valuePrefix, valueSuffix;
    bool compactValues = false;
    int valueDecimals = -1;
    std::string domainTitle, valueTitle;

    bool Stacked() const;
    bool Filled() const;
    size_t PointCount() const;
    Color SeriesColor(size_t index) const;
    double DomainValue(size_t index) const;
    // Value-space band per series per index (base, top) for the current mode
    // with hidden series removed from the stack.
    ChartStackedValues Bands() const;
    // The plotted value of series s at index i (percent share when 100%).
    std::string FormatPlotted(const ChartAxis& valueAxis, double value) const;
    void RefreshLegend();
    void RebuildSpanAnnotations();
    std::vector<ChartNormalizedPoint> TopPoints(size_t seriesIndex, const ChartStackedValues& bands,
                                                const ChartAxis& value, const ChartAxis& domain,
                                                double progress) const;
    static int64_t RegionId(size_t seriesIndex, size_t pointIndex) {
        return static_cast<int64_t>(seriesIndex) * 1000000 + static_cast<int64_t>(pointIndex);
    }
};

inline std::shared_ptr<UltraCanvasEngineLineChart> CreateEngineLineChart(
        const std::string& id, int x = 0, int y = 0, int w = 0, int h = 0) {
    return std::make_shared<UltraCanvasEngineLineChart>(id, x, y, w, h);
}

} // namespace UltraCanvas
