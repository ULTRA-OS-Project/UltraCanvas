// include/Plugins/Charts/Engine/Charts/UltraCanvasEngineMekkoChart.h
// Mekko (Marimekko / mosaic) chart on the chart engine: variable-width
// columns whose width is their share of the grand total, each split into
// stacked series segments - 100% stacked (Marimekko) or at absolute height
// (Bar-Mekko).
//
// The engine port of UltraCanvasMekkoChartElement. The domain is a Linear
// axis over 0..100% of the grand total, so a column is a span [cumulative
// share, cumulative share + own share] and every segment is a BuildBarOutline
// span - the mosaic also renders transposed under the Horizontal projection.
// Column names and the optional cumulative-percent ticks are two edge axes on
// the same side, which the engine stacks outward; the value axis, the grid,
// the legend (hover dims the other series, a click hides one and the widths
// re-share), the column headers in the solved label plan, the segment hit
// regions and the grow-in animation are engine services.
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

// One horizontal band of the mosaic (one legend entry). Alpha 0 = palette colour.
struct ChartMekkoSeries {
    std::string name;
    Color color = Color(0, 0, 0, 0);

    ChartMekkoSeries() = default;
    ChartMekkoSeries(const std::string& n, const Color& c = Color(0, 0, 0, 0))
        : name(n), color(c) {}
};

// One variable-width column: one value per series (missing = 0, negatives ignored).
struct ChartMekkoColumn {
    std::string label;
    std::vector<double> values;

    ChartMekkoColumn() = default;
    ChartMekkoColumn(const std::string& l, const std::vector<double>& v = {})
        : label(l), values(v) {}
};

enum class ChartMekkoMode {
    Marimekko,   // every column full height, the value axis in percent
    BarMekko     // column height = column total, the value axis in values
};

enum class ChartMekkoColumnSort { DataOrder, TotalDescending, TotalAscending, Alphabetical };

enum class ChartMekkoSegmentLabel { NoLabels, Value, Percent, ValueAndPercent, SeriesName, SeriesAndValue };

enum class ChartMekkoHeader { NoHeader, WidthPercent, Total, TotalAndPercent };

class UltraCanvasEngineMekkoChart : public UltraCanvasChartEngineElement {
public:
    // (columnIndex, seriesIndex) of a clicked segment, in data order.
    using SegmentClickCallback = std::function<void(size_t, size_t)>;

    UltraCanvasEngineMekkoChart(const std::string& id, int x = 0, int y = 0,
                                int w = 0, int h = 0);

    // ---- data -----------------------------------------------------------
    void SetData(const std::vector<ChartMekkoSeries>& series,
                 const std::vector<ChartMekkoColumn>& columns);
    size_t AddSeries(const std::string& name, const Color& color = Color(0, 0, 0, 0));
    size_t AddColumn(const std::string& label, const std::vector<double>& values = {});
    void ClearData();
    size_t GetSeriesCount() const { return series.size(); }
    size_t GetColumnCount() const { return columns.size(); }

    // ---- configuration --------------------------------------------------
    void SetMekkoMode(ChartMekkoMode mode);
    ChartMekkoMode GetMekkoMode() const { return mode; }
    void SetColumnSort(ChartMekkoColumnSort sort);
    void SetSegmentLabelMode(ChartMekkoSegmentLabel labelMode);
    ChartMekkoSegmentLabel GetSegmentLabelMode() const { return segmentLabels; }
    void SetColumnHeaderMode(ChartMekkoHeader headerMode);
    // Pixels between neighbouring columns.
    void SetColumnGap(double gapPx);
    void SetSegmentBorder(const Color& color, float width);
    void SetSegmentCornerRadius(double radiusPx);
    void SetShowColumnLabels(bool show);
    void SetShowValueAxis(bool show);
    // Cumulative percent ticks at the column boundaries.
    void SetShowCumulativeAxis(bool show);
    // Value text around the number, e.g. ("$", "B").
    void SetValueUnits(const std::string& prefix, const std::string& suffix);
    void SetValueFormatter(ValueFormatter formatter);
    void SetOnSegmentClick(SegmentClickCallback callback) { onSegmentClick = std::move(callback); }
    // (columnIndex, seriesIndex) under the pointer, or (SIZE_MAX, SIZE_MAX).
    std::pair<size_t, size_t> GetHoveredSegment() const;

    // ---- engine contract ------------------------------------------------
    void DescribeAxes(ChartAxisSet& axes) override;
    void RenderChartContent(IRenderContext* ctx, const ChartEngineFrame& frame) override;
    void MeasureContent(IRenderContext* ctx, ChartLayoutRequest& request) override;
    void CollectChartLabels(IRenderContext* ctx, ChartLabelBroker& broker) override;
    void OnThemeChanged() override;
    void OnLegendEntryToggled(size_t entryIndex, bool enabled) override;
    bool OnEvent(const UCEvent& event) override;

private:
    std::vector<ChartMekkoSeries> series;
    std::vector<ChartMekkoColumn> columns;
    std::vector<bool> seriesHidden;

    ChartMekkoMode mode = ChartMekkoMode::Marimekko;
    ChartMekkoColumnSort sort = ChartMekkoColumnSort::DataOrder;
    ChartMekkoSegmentLabel segmentLabels = ChartMekkoSegmentLabel::Value;
    ChartMekkoHeader header = ChartMekkoHeader::WidthPercent;
    double columnGap = 2.0;
    Color segmentBorderColor = Color(255, 255, 255, 255);
    float segmentBorderWidth = 1.0f;
    double segmentCornerRadius = 0.0;
    bool showColumnLabels = true;
    bool showValueAxis = true;
    bool showCumulativeAxis = false;
    std::string valuePrefix, valueSuffix;
    ValueFormatter valueFormatter;
    SegmentClickCallback onSegmentClick;

    // The resolved mosaic, rebuilt in DescribeAxes (on Data / Style only).
    struct Segment {
        size_t column = 0, series = 0;
        double u0 = 0.0, u1 = 0.0;   // domain extent, percent of the grand total
        double v0 = 0.0, v1 = 0.0;   // value extent, in value-axis units
        double value = 0.0;
        double percentOfColumn = 0.0;
    };
    struct ColumnSpan {
        size_t column = 0;
        double u0 = 0.0, u1 = 0.0;   // percent of the grand total
        double total = 0.0;
    };
    std::vector<ColumnSpan> spans;    // in display order
    std::vector<Segment> segments;
    double grandTotal = 0.0;

    static constexpr size_t kValueAxis = 0;
    static constexpr size_t kCumulativeAxis = 1;
    static constexpr size_t kColumnAxis = 2;

    double ColumnTotal(size_t column) const;
    void BuildMosaic();
    void RefreshLegend();
    Color SeriesColor(size_t index) const;
    std::string FormatValue(double value) const;
    static std::string FormatPercent(double percent);
    std::string SegmentText(const Segment& segment) const;
    std::string HeaderText(const ColumnSpan& span) const;
    // A gap of columnGap pixels in normalised domain units, via the projection.
    double GapInDomainUnits(const IChartProjection& projection) const;
};

inline std::shared_ptr<UltraCanvasEngineMekkoChart> CreateEngineMekkoChart(
        const std::string& id, int x = 0, int y = 0, int w = 0, int h = 0) {
    return std::make_shared<UltraCanvasEngineMekkoChart>(id, x, y, w, h);
}

} // namespace UltraCanvas
