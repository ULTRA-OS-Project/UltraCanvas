// include/Plugins/Charts/Engine/Charts/UltraCanvasRidgelineChart.h
// A ridgeline chart (a joyplot) on the chart engine: one row per category,
// each with a filled curve per series on its own baseline that may rise over
// the rows above it, read down the rows to compare where and how each is
// spread.
//
// The engine supplies the x axis, the vertical grid, limiters, the legend,
// tooltips and themes; this class draws the rows back to front (the lowest
// row in front), the row names, a value column beside them ("Average score")
// and its header, marks and quantile bands inside the curves. The curves,
// statistics and scaling come from UltraCanvasRidgelineModel.
//
// Axis indices: 0 = rows (hidden, 0..1), 1 = x.
// Design: Docs/UltraCanvas/UltraCanvasRidgelineChartProposal.md
// Guide:  Docs/UltraCanvas/UltraCanvasRidgelineChart.md
//
// Version: 1.1.0 - Phase 2: several series per row with a legend, marks
//                  (mean, median, quartiles), quantile bands, highlight on
//                  hover, onRowClick
// Version: 1.0.0 - Phase 1
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/Engine/UltraCanvasChartEngineElement.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasRidgelineModel.h"
#include "Plugins/Charts/UltraCanvasColormap.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// How the rows are coloured. A row's own colour wins; with two or more
// series every curve takes its series' colour instead.
enum class RidgelineColoring {
    Single,        // one colour (SetFillColor)
    Alternating,   // two colours, row by row (SetAlternateColors)
    ByRow,         // a colour map down the rows (SetColormap)
    ByRowValue     // a colour map by the value column (the mean when it is off)
};

// What the value column beside the row names shows.
// (Hidden, not None: X11's Xlib.h #defines None.)
enum class RidgelineValue { Hidden, Mean, Median, Count, Peak };

// Marks drawn inside each curve, as a rule from the baseline up to the curve
// (combine with |).
enum RidgelineMark : unsigned {
    RidgelineMarkMean = 1u,
    RidgelineMarkMedian = 2u,
    RidgelineMarkQuartiles = 4u
};

class UltraCanvasRidgelineChart : public UltraCanvasChartEngineElement {
public:
    UltraCanvasRidgelineChart(const std::string& id, int x = 0, int y = 0, int w = 0, int h = 0);

    // ---- data -----------------------------------------------------------
    // Takes the model's title as the chart title and draws its subtitle.
    void SetModel(const UltraCanvasRidgelineModel& model);
    const UltraCanvasRidgelineModel& GetModel() const { return model; }

    // ---- layout ---------------------------------------------------------
    // How many row heights the tallest curve spans (0.5..5): at 1 it just
    // reaches the next baseline up.
    void SetOverlap(double rows);
    double GetOverlap() const { return overlap; }
    // A thin line along each row's baseline.
    void SetShowRowLines(bool show);
    // The x axis: tick positions (empty = automatic) and their text.
    void SetXTicks(const std::vector<double>& ticks);
    void SetXFormatter(ValueFormatter formatter);

    // ---- colour ---------------------------------------------------------
    void SetColoring(RidgelineColoring coloring);
    RidgelineColoring GetColoring() const { return coloring; }
    void SetColormap(HeatmapColormap colormap, bool reversed = false);
    void SetFillColor(const Color& color);                   // alpha 0 = the palette's first
    void SetAlternateColors(const Color& first, const Color& second);
    void SetFillOpacity(float opacity);                      // 0..1
    // Fills fade from the row's colour at the curve's top towards its baseline.
    void SetGradientFill(bool gradient);
    // The curve's outline: alpha 0 = a darker shade of the fill; width 0 = none.
    void SetOutline(const Color& color, float width);
    // The colour a row's series is filled with.
    Color RowColor(size_t row, size_t series = 0) const;
    // A series' colour: its own, or the theme palette's.
    Color SeriesColor(size_t series) const;

    // ---- marks and bands --------------------------------------------------
    // RidgelineMarkMean | RidgelineMarkMedian | RidgelineMarkQuartiles; 0 = none.
    void SetMarks(unsigned marks);
    unsigned GetMarks() const { return marks; }
    // The middle half of each curve (first to third quartile) shaded darker.
    void SetQuantileBands(bool show);

    // ---- series -------------------------------------------------------------
    // A series switched off (as its legend entry does) is not drawn.
    void SetSeriesVisible(size_t series, bool visible);
    bool IsSeriesVisible(size_t series) const;

    // ---- value column -----------------------------------------------------
    // `title` is the column's header. Mean, median and peak are x values:
    // written by the x formatter when there is one, otherwise to `decimals`.
    // The column describes each row's first series.
    void SetValueColumn(const std::string& title, RidgelineValue value, int decimals = 2);
    double RowValue(size_t row) const;
    std::string RowValueText(size_t row) const;

    // ---- reading ----------------------------------------------------------
    // While a row is hovered, the other rows fade.
    void SetHighlightOnHover(bool highlight);
    // The row under the pointer (-1 when none), and the x there.
    int GetHoveredRow() const { return hoveredRow; }
    double GetHoveredX() const { return hoveredX; }
    // The tooltip text for a row at an x: every shown series' height there
    // and statistics.
    std::string DescribeRow(size_t row, double x) const;
    // A row's baseline in normalised plot height (0 = bottom, 1 = top).
    double RowBaseline(size_t row) const;
    // The drawn height of a row's curve at x, in normalised plot height above
    // its baseline (0 outside the curve).
    double RowHeightAt(size_t row, double x, size_t series = 0) const;
    // The pointer entered a row, moved along it (GetHoveredX changed) or
    // left every row (-1).
    std::function<void(int row)> onRowHover;
    // A row was clicked (pressed and released on it).
    std::function<void(int row)> onRowClick;

    // ---- engine contract --------------------------------------------------
    void DescribeAxes(ChartAxisSet& axes) override;
    void MeasureContent(IRenderContext* ctx, ChartLayoutRequest& request) override;
    void RenderChartContent(IRenderContext* ctx, const ChartEngineFrame& frame) override;
    void RenderInteractionOverlay(IRenderContext* ctx, const ChartEngineFrame& frame) override;
    void OnHitRegionHoverChanged(int64_t regionId) override;
    void OnLegendEntryToggled(size_t entryIndex, bool enabled) override;
    void OnChartThemeChanged() override;
    bool OnEvent(const UCEvent& event) override;

protected:
    void RenderEngineTitle(IRenderContext* ctx) override;

private:
    UltraCanvasRidgelineModel model;
    // [row][series]: scaled 0..1, in data units, and their statistics.
    std::vector<std::vector<std::vector<RidgelineCurvePoint>>> curves;
    std::vector<std::vector<std::vector<RidgelineCurvePoint>>> rawCurves;
    std::vector<std::vector<RidgelineRowStatistics>> statistics;
    std::pair<double, double> xRange{0.0, 1.0};
    std::vector<bool> hiddenSeries;

    double overlap = 1.8;
    bool showRowLines = false;
    std::vector<double> xTicks;
    ValueFormatter xFormatter;
    RidgelineColoring coloring = RidgelineColoring::Single;
    HeatmapColormap colormap = HeatmapColormap::Viridis;
    bool colormapReversed = false;
    Color fillColor = Color(0, 0, 0, 0);
    Color alternate[2] = {Color(0, 0, 0, 0), Color(0, 0, 0, 0)};
    float fillOpacity = 0.85f;
    bool gradientFill = false;
    Color outlineColor = Color(0, 0, 0, 0);
    float outlineWidth = 1.2f;
    unsigned marks = 0;
    bool quantileBands = false;
    bool highlightOnHover = false;
    std::string valueTitle;
    RidgelineValue valueColumn = RidgelineValue::Hidden;
    int valueDecimals = 2;
    int hoveredRow = -1;
    double hoveredX = 0.0;
    int pressedRow = -1;

    static constexpr int kStrips = 64;      // hit strips per row
    static constexpr double kBottomPad = 0.3;

    void Recompute();
    void RefreshLegend();
    size_t SeriesCount() const { return model.SeriesCount(); }
    // The series a row's value column and sorting describe: its first with data.
    size_t FirstSeries(size_t row) const;
    double RowPitch() const;
    std::string FormatX(double x) const;
    double SubtitleHeight(IRenderContext* ctx) const;
};

inline std::shared_ptr<UltraCanvasRidgelineChart> CreateRidgelineChart(
        const std::string& id, int x = 0, int y = 0, int w = 0, int h = 0) {
    return std::make_shared<UltraCanvasRidgelineChart>(id, x, y, w, h);
}

} // namespace UltraCanvas
