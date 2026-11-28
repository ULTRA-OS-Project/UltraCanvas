// include/Plugins/Charts/Engine/Charts/UltraCanvasEngineBarChart.h
// Column and bar chart on the chart engine: series of values over categories
// as columns side by side, one on the other, or as shares of 100% - lying
// down as bars under the Horizontal projection, as ring sectors under Polar.
//
// The chart describes its two axes and draws its bars; the arithmetic
// (group offsets, stacking, the value range) is the engine's bar series
// geometry (Engine/UltraCanvasChartSeries.h), and the grid, axis furniture,
// legend, tooltips and themes are engine services. The first client is the
// chart of a word-processing document (UltraCanvasRichDocumentChart.h).
//
// Version: 1.0.0
// Last Modified: 2026-11-18
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/Engine/UltraCanvasChartEngineElement.h"
#include "Plugins/Charts/Engine/UltraCanvasChartSeries.h"
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

struct EngineBarSeries {
    std::string name;
    std::vector<double> values;               // one per category
    Color color = Color(0, 0, 0, 0);          // alpha 0 = take it from the theme palette
};

class UltraCanvasEngineBarChart : public UltraCanvasChartEngineElement {
public:
    UltraCanvasEngineBarChart(const std::string& id, int x = 0, int y = 0, int w = 0, int h = 0);

    // ---- data -----------------------------------------------------------
    void SetCategories(const std::vector<std::string>& categories);
    void SetSeries(const std::vector<EngineBarSeries>& seriesList);
    void AddSeries(const EngineBarSeries& series);
    void ClearSeries();
    const std::vector<EngineBarSeries>& GetSeries() const { return series; }

    // ---- look -----------------------------------------------------------
    // Side by side (Grouped), one on the other (Stacked), or as shares of
    // their category's total (PercentStacked: the value axis reads 0..100%).
    void SetArrangement(ChartBarArrangement arrangement);
    ChartBarArrangement GetArrangement() const { return layout.arrangement; }
    // Bars lying down: the Horizontal projection, the first category at the
    // foot as office charts have it (the projection alone puts it on top).
    void SetHorizontal(bool horizontal);
    bool IsHorizontal() const { return GetProjectionKind() == ChartProjectionKind::Horizontal; }
    // The share of a category's slot its bars fill (0.7), and the gap
    // between the bars of a group as a fraction of a bar (0).
    void SetSlotFill(double fraction, double groupGap = 0.0);
    void SetCornerRadius(double pixels);
    void SetValueFormat(const std::string& prefix, const std::string& suffix,
                        bool compact = false, int decimals = -1);
    // A stated value range (an office chart's c:scaling); SetAutoValueRange
    // returns to the one fitted to the data.
    void SetValueRange(double lo, double hi);
    void SetAutoValueRange();

    // ---- the content contract ---------------------------------------------
    void DescribeAxes(ChartAxisSet& axes) override;
    void RenderChartContent(IRenderContext* ctx, const ChartEngineFrame& frame) override;
    void OnChartThemeChanged() override;
    void OnLegendEntryToggled(size_t entryIndex, bool enabled) override;

    // Every bar the chart draws, in normalised chart space (tests, and
    // charts derived from this one).
    std::vector<ChartBarSpan> BarSpans() const;
    Color SeriesColor(size_t index) const;

private:
    std::vector<std::string> categoryNames;
    std::vector<EngineBarSeries> series;
    std::vector<bool> hidden;
    ChartBarLayoutOptions layout;
    double cornerRadius = 0.0;
    std::string valuePrefix, valueSuffix;
    bool compactValues = false;
    int valueDecimals = -1;
    bool statedRange = false;
    double rangeLow = 0.0, rangeHigh = 1.0;

    size_t CategoryCount() const;
    std::vector<std::vector<double>> SeriesValues() const;
    void RefreshLegend();
    void MirrorGroups(std::vector<ChartBarSpan>& spans, const ChartAxis& category) const;
};

inline std::shared_ptr<UltraCanvasEngineBarChart> CreateEngineBarChart(
        const std::string& id, int x = 0, int y = 0, int w = 0, int h = 0) {
    return std::make_shared<UltraCanvasEngineBarChart>(id, x, y, w, h);
}

} // namespace UltraCanvas
