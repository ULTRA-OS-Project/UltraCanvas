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
// Axis indices: 0 = value axis (pane 0), 1 = domain axis (categories or
// numeric x), 2.. = the value axes of panes 1.. (ValueAxisOfPane).
//
// Version: 1.3.0 - bands between two series, onPointClick, a series without a line;
//                  ShowControlChart draws varying limits, stages, excluded points,
//                  notes and specification limits
// Version: 1.2.0 - per-series line shape, marker, legend and tooltip switches;
//                  per-point marker styles and tooltip notes; stacked panes
// Version: 1.1.0 - SetValueAtLeast
// Version: 1.0.0
// Last Modified: 2026-11-06
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/Engine/UltraCanvasChartEngineElement.h"
#include "Plugins/Charts/Engine/UltraCanvasControlLimits.h"
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace UltraCanvas {

enum class EngineLineChartMode {
    Line,                 // strokes only
    Area,                 // each series filled down to zero, overlapping
    StackedArea,          // bands piled up per domain position
    PercentStackedArea    // bands sharing out 100% per domain position
};

// One point's own look, overriding its series': an outlier, the latest
// value, a point that breaks a control chart rule. Fields left at their
// defaults keep the series' look.
struct EngineLinePointStyle {
    Color color = Color(0, 0, 0, 0);          // alpha 0 = the series' marker colour
    std::optional<ChartMarkerShape> marker;   // unset = the series' marker
    bool hollow = false;                      // outline only, in the colour above
    float radiusScale = 1.0f;
    bool alwaysShown = false;                 // drawn even when the series hides markers
};

struct EngineLineSeries {
    std::string name;
    std::vector<double> values;               // one per category / x value; NaN = gap
    Color color = Color(0, 0, 0, 0);          // alpha 0 = take it from the theme palette
    ChartMarkerShape marker = ChartMarkerShape::Circle;
    float lineWidth = 0.0f;                   // 0 = the chart's line width
    bool dashed = false;
    // The line shape of this series alone (a stepped limit beside a straight
    // data line); unset = the chart's SetLineShape.
    std::optional<ChartLineShape> shape;
    // Markers for this series alone; unset = the chart's SetShowMarkers.
    std::optional<bool> showMarkers;
    // false = no legend entry (a reference or limit line that is not data).
    bool inLegend = true;
    // false = no hit regions, tooltips or value labels: a line to read
    // against, not data to inspect.
    bool interactive = true;
    // false = no line is stroked: a series that is only the edge of a band
    // (AddSeriesBand). Its markers still follow showMarkers.
    bool drawLine = true;
    // The pane the series is drawn in (SetPanes); out of range = the last.
    size_t pane = 0;
    // Per-point looks and extra tooltip lines, by value index; shorter than
    // `values` (or empty) = the rest keep the series' look / have no note.
    std::vector<EngineLinePointStyle> pointStyles;
    std::vector<std::string> pointNotes;
};

// A filled band between two series, point by point - a confidence
// interval, a min / max envelope, the zones under control limits that change
// from point to point. Drawn under the lines, through the pane of the lower
// series; where either series has a gap the band has one too.
struct EngineLineBand {
    size_t lowerSeries = 0;
    size_t upperSeries = 0;
    Color color = Color(0, 0, 0, 0);          // alpha 0 = the upper series' colour
    float opacity = 0.15f;
    std::optional<ChartLineShape> shape;      // unset = the upper series' shape
};

// One of several stacked panes sharing the domain axis (SetPanes): each has
// its own value axis, grid and title in a sub-band of the plot - a range
// chart under its averages, an indicator under a price.
struct EngineLinePane {
    double weight = 1.0;                      // share of the plot, relative to the others
    std::string valueTitle;                   // empty = the chart's value title (pane 0) / none
    bool includeZero = false;
    // Values the pane's axis always reaches, data or not: control limits, a
    // target. A limiter outside its axis's range is not drawn.
    std::vector<double> rangeIncludes;
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
    // The value axis reaches at least `value` whatever the data (NaN: no
    // floor). A live chart of small counts or rates - a server monitor at
    // rest - keeps a readable axis instead of one fitted to a flat zero.
    void SetValueAtLeast(double value);
    // Slot padding of a categorical domain, in slots (0 = the first and last
    // points sit on the plot edges). Ignored under Polar, which spreads the
    // categories evenly round the circle.
    void SetCategoryPadding(double slots);
    void SetAxisTitles(const std::string& domainTitle, const std::string& valueTitle);
    // Stack the series from the last to the first, so series 0 is the TOP
    // band - the cumulative flow convention (first stage on top, Done at the
    // bottom). The legend keeps the given order.
    void SetReverseStackOrder(bool reverse);

    // ---- bands -----------------------------------------------------------
    size_t AddSeriesBand(const EngineLineBand& band);
    void ClearSeriesBands();
    const std::vector<EngineLineBand>& GetSeriesBands() const { return seriesBands; }

    // ---- interaction ------------------------------------------------------
    // A click on a point of an interactive series: its series and value index.
    std::function<void(size_t series, size_t index)> onPointClick;

    // ---- panes -----------------------------------------------------------
    // Stacked panes from the top (pane 0) down, under the vertical
    // projection; side by side from the left when Horizontal. Each series
    // names its pane (EngineLineSeries::pane). Empty = one pane, as before.
    // The stacked area modes and Polar draw every series in pane 0.
    void SetPanes(const std::vector<EngineLinePane>& panes);
    const std::vector<EngineLinePane>& GetPanes() const { return panes; }
    // The gap between neighbouring panes, as a fraction of the plot (0..0.3).
    void SetPaneGap(double fraction);
    // The value axis a pane plots against - the axis index limiters
    // (ChartLimiter::axisIndex) and highlights (ChartHighlight::axisIndex)
    // take to land in that pane.
    static size_t ValueAxisOfPane(size_t pane) { return pane == 0 ? 0 : pane + 1; }
    // The pane series `index` is drawn in after the rules above.
    size_t PaneOfSeries(size_t index) const;

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
    bool OnEvent(const UCEvent& event) override;

private:
    std::vector<std::string> categories;
    std::vector<double> xValues;
    std::vector<EngineLineSeries> series;
    std::vector<bool> hidden;
    std::vector<EngineLineSpan> spans;
    std::vector<EngineLinePane> panes;
    std::vector<EngineLineBand> seriesBands;
    double paneGap = 0.06;
    // Legend entry -> series, for the series that have one.
    std::vector<size_t> legendSeries;

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
    double valueAtLeast = std::nan("");
    bool reverseStack = false;
    double categoryPadding = 0.0;
    std::string valuePrefix, valueSuffix;
    bool compactValues = false;
    int valueDecimals = -1;
    std::string domainTitle, valueTitle;

    bool Stacked() const;
    bool Filled() const;
    size_t PointCount() const;
    size_t PaneCount() const;
    // The frame's value axis for series s.
    const ChartAxis& SeriesValueAxis(const ChartAxisSet& axes, size_t s) const;
    ChartLineOptions SeriesLineOptions(size_t s) const;
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

// =============================================================================
// CONTROL CHARTS
// =============================================================================

// How ShowControlChart draws a control chart. Colours with alpha 0 come from
// the chart's theme, so a dark theme keeps working.
struct ControlChartStyle {
    bool showDispersion = true;               // the R / MR pane under the values
    double dispersionWeight = 0.55;           // its height relative to the values pane
    bool showZones = false;                   // A / B / C shading either side of the centre
    bool showSigmaLines = false;              // dotted 1 and 2 sigma lines
    bool showLimitCaptions = true;            // "UCL = 10.032" at the right end
    Color dataColor = Color(0, 0, 0, 0);      // the plotted statistic; alpha 0 = palette
    Color limitColor = Color(0, 0, 0, 0);     // UCL / LCL; alpha 0 = the axis line colour
    Color centreColor = Color(0, 0, 0, 0);    // CL; alpha 0 = a lighter axis colour
    Color flagColor = Color(200, 40, 40, 255);   // a point that breaks a rule
    bool showSpecLimits = true;               // USL / LSL / target from the result
    Color specColor = Color(128, 70, 190, 255);
    bool showStageNames = true;               // the stage's name at its divider
};

// Puts a control chart result (UltraCanvasControlLimits.h) on a line chart,
// replacing its categories, series, bands, panes, limiters and highlights: the
// statistic in pane 0 and its range in pane 1; constant limits as limiters with
// their values in a column past the plot's right edge
// (ChartLimiterCaptionPlacement::BeyondEnd), limits that change from point to
// point (p and u charts, unequal subgroups, stages) as stepped lines with the
// last point's values in that column; a lower limit of zero left out; points
// that break a rule as larger diamonds in flagColor with the rules in their
// tooltip; excluded points hollow; each point's note in its tooltip; a divider
// at each stage; specification limits; and optional zones and sigma lines. It
// also pads the domain by half a slot, so the first and last points clear the
// plot edges. Recompute and call again after the data changes.
void ShowControlChart(UltraCanvasEngineLineChart& chart, const ControlResult& result,
                      const ControlChartStyle& style = {});

inline std::shared_ptr<UltraCanvasEngineLineChart> CreateEngineLineChart(
        const std::string& id, int x = 0, int y = 0, int w = 0, int h = 0) {
    return std::make_shared<UltraCanvasEngineLineChart>(id, x, y, w, h);
}

} // namespace UltraCanvas
