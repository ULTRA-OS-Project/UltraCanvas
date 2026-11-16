// include/Plugins/Charts/Engine/Charts/UltraCanvasRidgelineChart.h
// A ridgeline chart (a joyplot) on the chart engine: one row per category,
// each with a filled curve on its own baseline that may rise over the rows
// above it, read down the rows to compare where and how each is spread.
//
// The engine supplies the x axis, the vertical grid, limiters, tooltips and
// themes; this class draws the rows back to front (the lowest row in front),
// the row names, a value column beside them ("Average score") and its header.
// The curves, statistics and scaling come from UltraCanvasRidgelineModel.
//
// Axis indices: 0 = rows (hidden, 0..1), 1 = x.
// Design: Docs/UltraCanvas/UltraCanvasRidgelineChartProposal.md
// Guide:  Docs/UltraCanvas/UltraCanvasRidgelineChart.md
//
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

// How the rows are coloured (a row's own colour always wins).
enum class RidgelineColoring {
    Single,        // one colour (SetFillColor)
    Alternating,   // two colours, row by row (SetAlternateColors)
    ByRow,         // a colour map down the rows (SetColormap)
    ByRowValue     // a colour map by the value column (the mean when it is off)
};

// What the value column beside the row names shows.
// (Hidden, not None: X11's Xlib.h #defines None.)
enum class RidgelineValue { Hidden, Mean, Median, Count, Peak };

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
    // The colour a row is filled with.
    Color RowColor(size_t row) const;

    // ---- value column -----------------------------------------------------
    // `title` is the column's header; `decimals` for Mean, Median and Peak.
    void SetValueColumn(const std::string& title, RidgelineValue value, int decimals = 2);
    double RowValue(size_t row) const;
    std::string RowValueText(size_t row) const;

    // ---- reading ----------------------------------------------------------
    // The row under the pointer (-1 when none), and the x there.
    int GetHoveredRow() const { return hoveredRow; }
    double GetHoveredX() const { return hoveredX; }
    // The tooltip text for a row at an x.
    std::string DescribeRow(size_t row, double x) const;
    // A row's baseline in normalised plot height (0 = bottom, 1 = top).
    double RowBaseline(size_t row) const;
    // The drawn height of a row's curve at x, in normalised plot height above
    // its baseline (0 outside the curve).
    double RowHeightAt(size_t row, double x) const;
    // The pointer entered a row, moved along it (GetHoveredX changed) or
    // left every row (-1).
    std::function<void(int row)> onRowHover;

    // ---- engine contract --------------------------------------------------
    void DescribeAxes(ChartAxisSet& axes) override;
    void MeasureContent(IRenderContext* ctx, ChartLayoutRequest& request) override;
    void RenderChartContent(IRenderContext* ctx, const ChartEngineFrame& frame) override;
    void RenderInteractionOverlay(IRenderContext* ctx, const ChartEngineFrame& frame) override;
    void OnHitRegionHoverChanged(int64_t regionId) override;

protected:
    void RenderEngineTitle(IRenderContext* ctx) override;

private:
    UltraCanvasRidgelineModel model;
    std::vector<std::vector<RidgelineCurvePoint>> curves;      // scaled 0..1
    std::vector<std::vector<RidgelineCurvePoint>> rawCurves;   // data units
    std::vector<RidgelineRowStatistics> statistics;
    std::pair<double, double> xRange{0.0, 1.0};

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
    std::string valueTitle;
    RidgelineValue valueColumn = RidgelineValue::Hidden;
    int valueDecimals = 2;
    int hoveredRow = -1;
    double hoveredX = 0.0;

    static constexpr int kStrips = 64;      // hit strips per row
    static constexpr double kBottomPad = 0.3;

    void Recompute();
    double RowPitch() const;
    std::string FormatX(double x) const;
    double SubtitleHeight(IRenderContext* ctx) const;
};

inline std::shared_ptr<UltraCanvasRidgelineChart> CreateRidgelineChart(
        const std::string& id, int x = 0, int y = 0, int w = 0, int h = 0) {
    return std::make_shared<UltraCanvasRidgelineChart>(id, x, y, w, h);
}

} // namespace UltraCanvas
