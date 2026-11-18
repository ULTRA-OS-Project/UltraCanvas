// core/UltraCanvasRichDocumentChart.cpp
// A document's chart as a chart of the chart engine - see the header.
// Version: 1.4.0 - radar, bubble and stock charts; smoothed lines
// Version: 1.3.0 - a scatter chart is always the engine's scatter chart (joined groups)
// Version: 1.2.0 - a chart's stated axis ranges (bars, scatter)
// Version: 1.1.0 - `ink`: the chart's furniture on a dark page
// Version: 1.0.0
// Last Modified: 2026-11-18
// Author: UltraCanvas Framework

#include "UltraCanvasRichDocumentChart.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineBarChart.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineLineChart.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasEnginePieChart.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineScatterChart.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineRadarChart.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineFinancialChart.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace UltraCanvas {

namespace {

// "#RRGGBB" -> the colour; alpha 0 ("from the palette") for anything else.
Color ChartColor(const std::string& hex) {
    if (hex.size() != 7 || hex[0] != '#') return Color(0, 0, 0, 0);
    char* end = nullptr;
    const unsigned long value = std::strtoul(hex.c_str() + 1, &end, 16);
    if (end != hex.c_str() + 7) return Color(0, 0, 0, 0);
    return Color(static_cast<uint8_t>((value >> 16) & 0xFF), static_cast<uint8_t>((value >> 8) & 0xFF),
                 static_cast<uint8_t>(value & 0xFF), 255);
}

// A chart on a page: the page shows through, the series take the office
// palette where the file states no colour.
ChartTheme PageTheme(const Color& ink) {
    ChartTheme theme = ChartThemes::Light();
    theme.name = "Document";
    theme.backgroundColor = Color(0, 0, 0, 0);
    theme.plotAreaColor = Color(0, 0, 0, 0);
    theme.legendBackground = Color(0, 0, 0, 0);
    theme.axisLineColor = Color(128, 128, 128, 255);
    theme.axisTickColor = Color(128, 128, 128, 255);
    theme.gridColor = Color(208, 208, 208, 255);
    if (ink.a > 0) {
        // A dark page: the words in the view's ink, the rules fainter.
        theme.axisLabelColor = theme.titleTextColor = theme.legendTextColor = ink;
        theme.axisLineColor = theme.axisTickColor = ink.WithAlpha(150);
        theme.gridColor = ink.WithAlpha(50);
    }
    theme.palette = ChartPalette("Office", {Color(0x44, 0x72, 0xC4, 255), Color(0xED, 0x7D, 0x31, 255),
                                            Color(0xA5, 0xA5, 0xA5, 255), Color(0xFF, 0xC0, 0x00, 255),
                                            Color(0x5B, 0x9B, 0xD5, 255), Color(0x70, 0xAD, 0x47, 255)});
    return theme;
}

} // namespace

std::shared_ptr<UltraCanvasChartEngineElement> CreateRichDocumentChart(const RichChart& chart, int width, int height,
                                                                       const Color& ink) {
    if (!chart.IsChart() || chart.series.empty()) return nullptr;
    std::shared_ptr<UltraCanvasChartEngineElement> element;
    bool anyMarked = false;
    for (const RichChartSeries& one : chart.series) {
        anyMarked = anyMarked || one.marked;
    }

    switch (chart.kind) {
        case RichChart::Kind::Column:
        case RichChart::Kind::Bar: {
            auto bars = CreateEngineBarChart("DocumentChart", 0, 0, width, height);
            bars->SetAnimateOnDataChange(false);     // a page shows the chart as it is
            bars->SetCategories(chart.categories);
            std::vector<EngineBarSeries> list;
            for (const RichChartSeries& one : chart.series) {
                EngineBarSeries series;
                series.name = one.name;
                series.values = one.values;
                series.color = ChartColor(one.color);
                list.push_back(std::move(series));
            }
            bars->SetSeries(list);
            bars->SetArrangement(chart.percent ? ChartBarArrangement::PercentStacked
                                 : chart.stacked ? ChartBarArrangement::Stacked : ChartBarArrangement::Grouped);
            bars->SetHorizontal(chart.kind == RichChart::Kind::Bar);
            if (std::isfinite(chart.valueMin) && std::isfinite(chart.valueMax)) bars->SetValueRange(chart.valueMin, chart.valueMax);
            element = bars;
            break;
        }
        case RichChart::Kind::Pie:
        case RichChart::Kind::Doughnut: {
            // The first series' values as slices, named by the categories.
            auto pie = CreateEnginePieChart("DocumentChart", 0, 0, width, height);
            pie->SetAnimateOnDataChange(false);     // a page shows the chart as it is
            const RichChartSeries& one = chart.series.front();
            std::vector<EnginePieSlice> slices;
            for (size_t i = 0; i < one.values.size(); i++) {
                EnginePieSlice slice;
                slice.name = i < chart.categories.size() ? chart.categories[i] : one.name;
                slice.value = std::max(0.0, one.values[i]);
                if (i < chart.pointColors.size()) slice.color = ChartColor(chart.pointColors[i]);
                slices.push_back(std::move(slice));
            }
            pie->SetSlices(slices);
            pie->SetDonutMode(chart.kind == RichChart::Kind::Doughnut);
            pie->SetLabelPosition(EnginePieLabelPosition::Hidden);
            pie->SetHoverExplosion(0.0f);
            pie->SetBorder(Color(255, 255, 255, 255), 1.0f);
            element = pie;
            break;
        }
        case RichChart::Kind::Bubble:
        case RichChart::Kind::Scatter: {
            // Points over two value axes, each series over its own x; a
            // series with a line is joined in the order of its points.
            auto scatter = CreateEngineScatterChart("DocumentChart", 0, 0, width, height);
            scatter->SetAnimateOnDataChange(false);     // a page shows the chart as it is
            std::vector<EngineScatterGroup> groups;
            for (const RichChartSeries& one : chart.series) {
                EngineScatterGroup group;
                group.name = one.name;
                group.color = ChartColor(one.color);
                const bool bubbles = chart.kind == RichChart::Kind::Bubble;
                group.joined = one.joined && !bubbles;
                group.smooth = one.smooth;
                group.showMarkers = one.marked || !group.joined;
                for (size_t i = 0; i < one.values.size(); i++) {
                    EngineScatterPoint point(i < one.xValues.size() ? one.xValues[i] : static_cast<double>(i + 1), one.values[i]);
                    // (A bubble's size is its area's; one without a size is the smallest.)
                    if (bubbles) point.size = i < one.sizes.size() ? std::max(0.0, one.sizes[i]) : 0.0;
                    group.points.push_back(point);
                }
                groups.push_back(std::move(group));
            }
            scatter->SetGroups(groups);
            scatter->SetMarkerRadius(3.0f);
            scatter->SetMarkerOpacity(chart.kind == RichChart::Kind::Bubble ? 0.75f : 1.0f);
            scatter->SetJoinLineWidth(2.0f);
            if (chart.kind == RichChart::Kind::Bubble) scatter->SetRadiusRange(3.0f, std::max(8.0f, std::min(width, height) * 0.09f));
            if (std::isfinite(chart.xMin) && std::isfinite(chart.xMax)) scatter->SetXRange(chart.xMin, chart.xMax);
            if (std::isfinite(chart.valueMin) && std::isfinite(chart.valueMax)) scatter->SetYRange(chart.valueMin, chart.valueMax);
            element = scatter;
            break;
        }
        case RichChart::Kind::Line:
        case RichChart::Kind::Area: {
            auto lines = CreateEngineLineChart("DocumentChart", 0, 0, width, height);
            lines->SetAnimateOnDataChange(false);     // a page shows the chart as it is
            lines->SetCategories(chart.categories);
            std::vector<EngineLineSeries> list;
            for (const RichChartSeries& one : chart.series) {
                EngineLineSeries series;
                series.name = one.name;
                series.values = one.values;
                series.color = ChartColor(one.color);
                series.showMarkers = one.marked;
                if (one.smooth) series.shape = ChartLineShape::Curved;
                series.drawLine = one.joined;
                list.push_back(std::move(series));
            }
            lines->SetSeries(list);
            lines->SetShowMarkers(anyMarked);
            lines->SetMarkerRadius(3.0f);
            lines->SetLineWidth(2.0f);
            if (chart.kind == RichChart::Kind::Area) {
                lines->SetMode(chart.percent ? EngineLineChartMode::PercentStackedArea
                               : chart.stacked ? EngineLineChartMode::StackedArea : EngineLineChartMode::Area);
            } else {
                lines->SetMode(EngineLineChartMode::Line);
                lines->SetIncludeZero(true);
            }
            element = lines;
            break;
        }
        case RichChart::Kind::Radar: {
            // The categories are the spokes, on one range from nought to a
            // round number over the highest value.
            auto radar = CreateEngineRadarChart("DocumentChart", 0, 0, width, height);
            radar->SetAnimateOnDataChange(false);     // a page shows the chart as it is
            size_t spokes = chart.categories.size();
            double lowest = 0.0, highest = 0.0;
            for (const RichChartSeries& one : chart.series) {
                spokes = std::max(spokes, one.values.size());
                for (double value : one.values) {
                    if (!std::isfinite(value)) continue;
                    lowest = std::min(lowest, value);
                    highest = std::max(highest, value);
                }
            }
            if (spokes < 3) return nullptr;
            if (std::isfinite(chart.valueMin)) lowest = chart.valueMin;
            if (std::isfinite(chart.valueMax)) highest = chart.valueMax;
            else if (highest > lowest) {
                const double step = NiceNumber((highest - lowest) / 5.0, true);
                highest = std::ceil(highest / step - 1e-9) * step;
            }
            if (highest <= lowest) highest = lowest + 1.0;
            for (size_t i = 0; i < spokes; i++) {
                radar->AddAxis(i < chart.categories.size() ? chart.categories[i] : std::to_string(i + 1), lowest, highest);
            }
            for (const RichChartSeries& one : chart.series) {
                EngineRadarSeries series;
                series.name = one.name;
                series.values = one.values;
                series.color = ChartColor(one.color);
                series.fillOpacity = chart.filled ? 0.35f : 0.0f;
                series.showPoints = one.marked;
                series.pointRadius = 3.0f;
                radar->AddSeries(series);
            }
            element = radar;
            break;
        }
        case RichChart::Kind::Stock: {
            // A bar for each category from its low to its high, with its
            // close - and its open, as a candle, where the chart has one.
            const size_t count = chart.series.size();
            if (count < 3) return nullptr;
            const bool opened = count >= 4;
            const RichChartSeries& high = chart.series[opened ? 1 : 0];
            const RichChartSeries& low = chart.series[opened ? 2 : 1];
            const RichChartSeries& close = chart.series[opened ? 3 : 2];
            std::vector<ChartOHLCBar> bars;
            for (size_t i = 0; i < close.values.size() && i < high.values.size() && i < low.values.size(); i++) {
                const double open = opened && i < chart.series[0].values.size() ? chart.series[0].values[i] : close.values[i];
                bars.emplace_back(i < chart.categories.size() ? chart.categories[i] : std::to_string(i + 1),
                                  open, high.values[i], low.values[i], close.values[i]);
            }
            if (bars.empty()) return nullptr;
            auto stock = CreateEngineFinancialChart("DocumentChart", 0, 0, width, height);
            stock->SetAnimateOnDataChange(false);     // a page shows the chart as it is
            stock->SetCandleStyle(opened ? ChartCandleStyle::Candlestick : ChartCandleStyle::OHLCBars);
            stock->SetShowVolumePane(false);
            stock->SetShowMovingAverage(false);
            stock->SetShowCrosshair(false);
            stock->SetPriceFormat("");
            stock->SetData(bars);
            element = stock;
            break;
        }
        case RichChart::Kind::NoChart:
            return nullptr;
    }
    if (!element) return nullptr;
    element->SetTheme(PageTheme(ink));
    element->SetEnableTooltips(false);
    element->SetShowLegend(chart.legend);
    element->SetLegendPosition(ChartLegendPosition::RightCenter);
    if (!chart.title.empty()) element->SetChartTitle(chart.title);
    return element;
}

bool DrawRichDocumentChart(IRenderContext* ctx, const RichChart& chart, const Rect2Dd& target, const Size2Di& natural,
                           std::shared_ptr<UltraCanvasChartEngineElement>* cached, const Color& ink) {
    if (!ctx || natural.width < 8 || natural.height < 8 || target.width <= 0.0 || target.height <= 0.0) return false;
    std::shared_ptr<UltraCanvasChartEngineElement> local;
    std::shared_ptr<UltraCanvasChartEngineElement>& element = cached ? *cached : local;
    if (!element || element->GetWidth() != natural.width || element->GetHeight() != natural.height
        || element->GetTheme().axisLabelColor != PageTheme(ink).axisLabelColor) {
        element = CreateRichDocumentChart(chart, natural.width, natural.height, ink);
    }
    if (!element) return false;
    ctx->PushState();
    ctx->Translate(target.x, target.y);
    const double scaleX = target.width / natural.width, scaleY = target.height / natural.height;
    if (std::abs(scaleX - 1.0) > 1e-6 || std::abs(scaleY - 1.0) > 1e-6) ctx->Scale(scaleX, scaleY);
    element->Render(ctx, Rect2Df(0.0f, 0.0f, static_cast<float>(natural.width), static_cast<float>(natural.height)));
    ctx->PopState();
    return true;
}

} // namespace UltraCanvas
