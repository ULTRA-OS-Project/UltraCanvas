// core/UltraCanvasRichDocumentChart.cpp
// A document's chart as a chart of the chart engine - see the header.
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

// Every series' x values alike (and stated): the points can ride one domain.
bool SharedX(const RichChart& chart) {
    for (const RichChartSeries& one : chart.series) {
        if (one.xValues.size() != one.values.size() || one.xValues != chart.series.front().xValues) return false;
    }
    return !chart.series.empty();
}

} // namespace

std::shared_ptr<UltraCanvasChartEngineElement> CreateRichDocumentChart(const RichChart& chart, int width, int height,
                                                                       const Color& ink) {
    if (!chart.IsChart() || chart.series.empty()) return nullptr;
    std::shared_ptr<UltraCanvasChartEngineElement> element;
    bool anyJoined = false, anyMarked = false;
    for (const RichChartSeries& one : chart.series) {
        anyJoined = anyJoined || one.joined;
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
        case RichChart::Kind::Scatter:
            if (!(anyJoined && SharedX(chart))) {
                // Points over two value axes.
                auto scatter = CreateEngineScatterChart("DocumentChart", 0, 0, width, height);
                scatter->SetAnimateOnDataChange(false);     // a page shows the chart as it is
                std::vector<EngineScatterGroup> groups;
                for (const RichChartSeries& one : chart.series) {
                    EngineScatterGroup group;
                    group.name = one.name;
                    group.color = ChartColor(one.color);
                    for (size_t i = 0; i < one.values.size(); i++) {
                        group.points.emplace_back(i < one.xValues.size() ? one.xValues[i] : static_cast<double>(i + 1),
                                                  one.values[i]);
                    }
                    groups.push_back(std::move(group));
                }
                scatter->SetGroups(groups);
                scatter->SetMarkerRadius(3.0f);
                scatter->SetMarkerOpacity(1.0f);
                if (std::isfinite(chart.xMin) && std::isfinite(chart.xMax)) scatter->SetXRange(chart.xMin, chart.xMax);
                if (std::isfinite(chart.valueMin) && std::isfinite(chart.valueMax)) scatter->SetYRange(chart.valueMin, chart.valueMax);
                element = scatter;
                break;
            }
            [[fallthrough]];        // joined points sharing their x: lines over a numeric domain
        case RichChart::Kind::Line:
        case RichChart::Kind::Area: {
            auto lines = CreateEngineLineChart("DocumentChart", 0, 0, width, height);
            lines->SetAnimateOnDataChange(false);     // a page shows the chart as it is
            if (chart.kind == RichChart::Kind::Scatter) lines->SetXValues(chart.series.front().xValues);
            else lines->SetCategories(chart.categories);
            std::vector<EngineLineSeries> list;
            for (const RichChartSeries& one : chart.series) {
                EngineLineSeries series;
                series.name = one.name;
                series.values = one.values;
                series.color = ChartColor(one.color);
                series.showMarkers = one.marked;
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
