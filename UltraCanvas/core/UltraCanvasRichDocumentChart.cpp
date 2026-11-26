// core/UltraCanvasRichDocumentChart.cpp
// A document's chart as a chart of the chart engine - see the header.
// Version: 1.5.0 - every kind: CreateRichChartElement / DrawRichChart build the framework's chart element
//                  of each RichChart::Kind from its categories and series
// Version: 1.4.0 - radar, bubble and stock charts; smoothed lines
// Version: 1.3.0 - a scatter chart is always the engine's scatter chart (joined groups)
// Version: 1.2.0 - a chart's stated axis ranges (bars, scatter)
// Version: 1.1.0 - `ink`: the chart's furniture on a dark page
// Version: 1.0.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework

#include "UltraCanvasRichDocumentChart.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineBarChart.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineLineChart.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasEnginePieChart.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineScatterChart.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineRadarChart.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineFinancialChart.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineWaterfallChart.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineDivergingBarChart.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineDumbbellChart.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineMekkoChart.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasEnginePolarChart.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineRadialBarChart.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineHeatmapChart.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasImportancePerformanceChart.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasTernaryChart.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasRidgelineChart.h"
#include "Plugins/Charts/UltraCanvasCalendarDate.h"
#include "Plugins/Charts/UltraCanvasCalendarHeatmap.h"
#include "Plugins/Charts/UltraCanvasChordChart.h"
#include "Plugins/Charts/UltraCanvasCircularProgressChart.h"
#include "Plugins/Charts/UltraCanvasContourChart.h"
#include "Plugins/Charts/UltraCanvasContourSurface3D.h"
#include "Plugins/Charts/UltraCanvasCumulativeFlowChart.h"
#include "Plugins/Charts/UltraCanvasFunnelChart.h"
#include "Plugins/Charts/UltraCanvasGanttChart.h"
#include "Plugins/Charts/UltraCanvasHexbinChart.h"
#include "Plugins/Charts/UltraCanvasJitterPlotElement.h"
#include "Plugins/Charts/UltraCanvasNestedAreaChart.h"
#include "Plugins/Charts/UltraCanvasPyramidChart.h"
#include "Plugins/Charts/UltraCanvasQuadrantChart.h"
#include "Plugins/Charts/UltraCanvasScatterPlot3D.h"
#include "Plugins/Charts/UltraCanvasSunburstChart.h"
#include "Plugins/Charts/UltraCanvasTimelineChart.h"
#include "Plugins/Diagrams/UltraCanvasGaugeDiagramElement.h"
#include "Plugins/Diagrams/UltraCanvasParliamentDiagram.h"
#include "Plugins/Diagrams/UltraCanvasSankey.h"
#include "Plugins/Diagrams/UltraCanvasTreeMapElement.h"
#include "Plugins/Diagrams/UltraCanvasWordCloudDiagram.h"
#include <map>
#include <set>
#include <sstream>
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

// ----- what the other kinds read -----------------------------------------------

const Color kOffice[] = {Color(0x44, 0x72, 0xC4, 255), Color(0xED, 0x7D, 0x31, 255), Color(0xA5, 0xA5, 0xA5, 255),
                         Color(0xFF, 0xC0, 0x00, 255), Color(0x5B, 0x9B, 0xD5, 255), Color(0x70, 0xAD, 0x47, 255),
                         Color(0x26, 0x44, 0x78, 255), Color(0x9E, 0x48, 0x0E, 255), Color(0x63, 0x63, 0x63, 255),
                         Color(0x99, 0x73, 0x00, 255)};

// Series `index`'s colour: its own, else the office palette's.
Color SeriesColor(const RichChart& chart, size_t index) {
    const Color own = index < chart.series.size() ? ChartColor(chart.series[index].color) : Color(0, 0, 0, 0);
    return own.a > 0 ? own : kOffice[index % (sizeof(kOffice) / sizeof(kOffice[0]))];
}

// Point `index`'s colour (a pie's slice colours, else the palette by place).
Color PointColor(const RichChart& chart, size_t index) {
    const Color own = index < chart.pointColors.size() ? ChartColor(chart.pointColors[index]) : Color(0, 0, 0, 0);
    return own.a > 0 ? own : kOffice[index % (sizeof(kOffice) / sizeof(kOffice[0]))];
}

// The name of point `index`: its category, else its number.
std::string PointName(const RichChart& chart, size_t index) {
    if (index < chart.categories.size() && !chart.categories[index].empty()) return chart.categories[index];
    return std::to_string(index + 1);
}

// The series that hold numbers (not a column of words), in order.
std::vector<const RichChartSeries*> NumberSeries(const RichChart& chart) {
    std::vector<const RichChartSeries*> out;
    for (const RichChartSeries& one : chart.series) {
        if (one.labels.empty() || !one.values.empty()) {
            bool words = false;
            for (const std::string& label : one.labels) words = words || !label.empty();
            if (!words) out.push_back(&one);
        }
    }
    return out;
}

// The series that hold words, in order.
std::vector<const RichChartSeries*> WordSeries(const RichChart& chart) {
    std::vector<const RichChartSeries*> out;
    for (const RichChartSeries& one : chart.series) {
        bool words = false;
        for (const std::string& label : one.labels) words = words || !label.empty();
        if (words) out.push_back(&one);
    }
    return out;
}

// The series a one-series kind draws: the first that holds a number other
// than nought (a column of words read as numbers holds none).
const RichChartSeries* FirstWithValues(const std::vector<const RichChartSeries*>& numbers) {
    for (const RichChartSeries* one : numbers) {
        for (double value : one->values) {
            if (std::isfinite(value) && value != 0.0) return one;
        }
    }
    return numbers.empty() ? nullptr : numbers.front();
}

double ValueAt(const RichChartSeries* series, size_t index) {
    if (!series || index >= series->values.size() || !std::isfinite(series->values[index])) return 0.0;
    return series->values[index];
}

// How many points the chart has: its categories, or its longest series.
size_t PointCount(const RichChart& chart) {
    size_t count = chart.categories.size();
    for (const RichChartSeries& one : chart.series) count = std::max(count, std::max(one.values.size(), one.labels.size()));
    return count;
}

// A number as a person reads it on an axis label: three significant digits.
std::string ShortNumber(double value) {
    std::ostringstream out;
    out.precision(3);
    out << value;
    return out.str();
}

// The groups of raw values a statistical kind draws: one per series, or -
// one series beside a column of names - one per distinct name.
std::vector<std::pair<std::string, std::vector<double>>> ValueGroups(const RichChart& chart) {
    std::vector<std::pair<std::string, std::vector<double>>> groups;
    const auto numbers = NumberSeries(chart);
    if (numbers.size() == 1 && !chart.categories.empty()) {
        std::map<std::string, size_t> at;
        const RichChartSeries* one = numbers.front();
        for (size_t i = 0; i < one->values.size(); ++i) {
            if (!std::isfinite(one->values[i])) continue;
            const std::string name = PointName(chart, i);
            auto found = at.find(name);
            if (found == at.end()) {
                found = at.emplace(name, groups.size()).first;
                groups.emplace_back(name, std::vector<double>());
            }
            groups[found->second].second.push_back(one->values[i]);
        }
        // Every point its own name: one group after all.
        if (groups.size() == one->values.size() && groups.size() > 1) {
            groups.clear();
            groups.emplace_back(one->name, std::vector<double>());
            for (double value : one->values) if (std::isfinite(value)) groups.back().second.push_back(value);
        }
        return groups;
    }
    for (const RichChartSeries* one : numbers) {
        std::vector<double> values;
        for (double value : one->values) if (std::isfinite(value)) values.push_back(value);
        if (!values.empty()) groups.emplace_back(one->name.empty() ? std::to_string(groups.size() + 1) : one->name, values);
    }
    return groups;
}

// The matrix a heatmap, contour, surface or chord diagram draws: a row per
// category, a column per series of numbers.
std::vector<std::vector<double>> Matrix(const RichChart& chart) {
    const auto numbers = NumberSeries(chart);
    std::vector<std::vector<double>> rows;
    size_t count = 0;
    for (const RichChartSeries* one : numbers) count = std::max(count, one->values.size());
    for (size_t r = 0; r < count; ++r) {
        std::vector<double> row;
        for (const RichChartSeries* one : numbers) row.push_back(ValueAt(one, r));
        rows.push_back(std::move(row));
    }
    return rows;
}

// A spreadsheet's date serial (days since 1899-12-30) as days since 1970.
long UnixDays(double serial) {
    return static_cast<long>(std::floor(serial)) - 25569L;
}

// An engine chart that shows its data as it is (a page has no entrance).
template <class T>
std::shared_ptr<T> Still(std::shared_ptr<T> element) {
    element->SetAnimateOnDataChange(false);
    return element;
}

// The engine's charts beyond the office kinds.
std::shared_ptr<UltraCanvasChartEngineElement> CreateOtherEngineChart(const RichChart& chart, int width, int height) {
    using Kind = RichChart::Kind;
    const auto numbers = NumberSeries(chart);
    const size_t points = PointCount(chart);
    switch (chart.kind) {
        case Kind::Waterfall: {
            // The changes; a step named "Total" or "Subtotal" stands on zero.
            if (numbers.empty()) return nullptr;
            std::vector<WaterfallStep> steps;
            for (size_t i = 0; i < FirstWithValues(numbers)->values.size(); ++i) {
                std::string lower = PointName(chart, i);
                std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
                const bool total = lower.rfind("total", 0) == 0 || lower.rfind("subtotal", 0) == 0;
                steps.emplace_back(PointName(chart, i), ValueAt(FirstWithValues(numbers), i),
                                   total ? WaterfallStepKind::Subtotal : WaterfallStepKind::Change);
            }
            if (steps.empty()) return nullptr;
            auto waterfall = Still(CreateEngineWaterfallChart("DocumentChart", 0, 0, width, height));
            waterfall->SetSteps(steps);
            return waterfall;
        }
        case Kind::DivergingBar: {
            // A row per name; the answers' series run from the most negative
            // to the most positive, a middle one neutral.
            if (numbers.empty()) return nullptr;
            const size_t count = numbers.size();
            std::vector<DivergingBarCategory> categories;
            for (size_t c = 0; c < count; ++c) {
                const DivergingSide side = count % 2 == 1 && c == count / 2 ? DivergingSide::Neutral
                                         : c < count / 2 ? DivergingSide::Negative : DivergingSide::Positive;
                categories.emplace_back(numbers[c]->name.empty() ? std::to_string(c + 1) : numbers[c]->name,
                                        SeriesColor(chart, c), side);
            }
            std::vector<DivergingBarRow> rows;
            for (size_t r = 0; r < points; ++r) {
                std::vector<double> values;
                for (const RichChartSeries* one : numbers) values.push_back(std::fabs(ValueAt(one, r)));
                rows.emplace_back(PointName(chart, r), values);
            }
            auto diverging = Still(CreateEngineDivergingBarChart("DocumentChart", 0, 0, width, height));
            diverging->SetCategories(categories);
            diverging->SetRows(rows);
            diverging->SetStyle(DivergingBarStyle::Likert);
            diverging->SetPercentOfRow(chart.percent);
            return diverging;
        }
        case Kind::PopulationPyramid: {
            if (numbers.size() < 2) return nullptr;
            std::vector<DivergingPopulationGroup> groups;
            for (size_t r = 0; r < points; ++r) {
                DivergingPopulationGroup group;
                group.label = PointName(chart, r);
                group.left = std::fabs(ValueAt(numbers[0], r));
                group.right = std::fabs(ValueAt(numbers[1], r));
                groups.push_back(group);
            }
            auto pyramid = Still(CreateEngineDivergingBarChart("DocumentChart", 0, 0, width, height));
            pyramid->SetPopulationData(groups, numbers[0]->name, SeriesColor(chart, 0), numbers[1]->name, SeriesColor(chart, 1));
            return pyramid;
        }
        case Kind::Dumbbell: {
            if (numbers.size() < 2) return nullptr;
            std::vector<DumbbellGroup> groups;
            for (size_t g = 0; g < numbers.size(); ++g)
                groups.emplace_back(numbers[g]->name.empty() ? std::to_string(g + 1) : numbers[g]->name, SeriesColor(chart, g));
            std::vector<DumbbellRow> rows;
            for (size_t r = 0; r < points; ++r) {
                std::vector<double> values;
                for (const RichChartSeries* one : numbers) values.push_back(ValueAt(one, r));
                rows.emplace_back(PointName(chart, r), values);
            }
            auto dumbbell = Still(CreateEngineDumbbellChart("DocumentChart", 0, 0, width, height));
            dumbbell->SetGroups(groups);
            dumbbell->SetRows(rows);
            return dumbbell;
        }
        case Kind::Mekko: {
            if (numbers.empty()) return nullptr;
            std::vector<ChartMekkoSeries> series;
            for (size_t g = 0; g < numbers.size(); ++g)
                series.emplace_back(numbers[g]->name.empty() ? std::to_string(g + 1) : numbers[g]->name, SeriesColor(chart, g));
            std::vector<ChartMekkoColumn> columns;
            for (size_t r = 0; r < points; ++r) {
                std::vector<double> values;
                for (const RichChartSeries* one : numbers) values.push_back(std::max(0.0, ValueAt(one, r)));
                columns.emplace_back(PointName(chart, r), values);
            }
            auto mekko = Still(CreateEngineMekkoChart("DocumentChart", 0, 0, width, height));
            mekko->SetData(series, columns);
            return mekko;
        }
        case Kind::Polar: {
            if (numbers.empty() || points < 3) return nullptr;
            auto polar = Still(CreateEnginePolarChart("DocumentChart", 0, 0, width, height));
            std::vector<std::string> names;
            for (size_t i = 0; i < points; ++i) names.push_back(PointName(chart, i));
            polar->SetCategories(names);
            for (size_t g = 0; g < numbers.size(); ++g)
                polar->AddCategorySeries(numbers[g]->name, chart.filled ? EnginePolarSeriesType::Area : EnginePolarSeriesType::Line,
                                         numbers[g]->values, SeriesColor(chart, g));
            return polar;
        }
        case Kind::RadialBar: {
            if (numbers.empty()) return nullptr;
            auto radial = Still(CreateEngineRadialBarChart("DocumentChart", 0, 0, width, height));
            for (size_t g = 0; g < numbers.size(); ++g) {
                std::vector<std::pair<std::string, double>> values;
                for (size_t i = 0; i < numbers[g]->values.size(); ++i) values.emplace_back(PointName(chart, i), ValueAt(numbers[g], i));
                radial->AddSeries(numbers[g]->name, values, SeriesColor(chart, g));
            }
            return radial;
        }
        case Kind::ImportancePerformance: {
            // Slack's matrix: importance and performance on nine-point scales.
            if (numbers.size() < 2) return nullptr;
            ImportancePerformanceModel model = ImportancePerformanceSamples::SlackZones(true);
            model.title.clear();
            for (size_t i = 0; i < points; ++i) {
                ImportancePerformanceFactor factor(PointName(chart, i), std::string(), ValueAt(numbers[0], i), ValueAt(numbers[1], i));
                model.factors.push_back(factor);
            }
            auto ipa = Still(CreateImportancePerformanceChart("DocumentChart", 0, 0, width, height));
            ipa->SetModel(model);
            return ipa;
        }
        case Kind::Ternary: {
            if (numbers.size() < 3) return nullptr;
            TernaryModel model;
            for (int corner = 0; corner < 3; ++corner) model.axes[corner] = TernaryAxis(numbers[static_cast<size_t>(corner)]->name);
            TernarySeries series(chart.title);
            series.color = SeriesColor(chart, 0);
            for (size_t i = 0; i < points; ++i)
                series.points.emplace_back(ValueAt(numbers[0], i), ValueAt(numbers[1], i), ValueAt(numbers[2], i), PointName(chart, i));
            model.series.push_back(series);
            auto ternary = Still(CreateTernaryChart("DocumentChart", 0, 0, width, height));
            ternary->SetModel(model);
            ternary->SetZoomByDrag(false);
            return ternary;
        }
        case Kind::Ridgeline: {
            const auto groups = ValueGroups(chart);
            if (groups.empty()) return nullptr;
            UltraCanvasRidgelineModel model;
            for (size_t g = 0; g < groups.size(); ++g) {
                RidgelineRow row;
                row.name = groups[g].first;
                row.values = groups[g].second;
                model.rows.push_back(row);
            }
            auto ridgeline = Still(CreateRidgelineChart("DocumentChart", 0, 0, width, height));
            ridgeline->SetModel(model);
            return ridgeline;
        }
        case Kind::Heatmap: {
            const auto matrix = Matrix(chart);
            if (matrix.empty() || matrix.front().empty()) return nullptr;
            auto heatmap = Still(CreateEngineHeatmapChart("DocumentChart", 0, 0, width, height));
            heatmap->SetData(matrix);
            std::vector<std::string> rows, columns;
            for (size_t r = 0; r < matrix.size(); ++r) rows.push_back(PointName(chart, r));
            for (const RichChartSeries* one : numbers) columns.push_back(one->name);
            heatmap->SetRowLabels(rows);
            heatmap->SetColumnLabels(columns);
            heatmap->SetShowCellValues(matrix.size() * matrix.front().size() <= 64);
            return heatmap;
        }
        case Kind::Histogram: {
            // Each group's values counted into the same bins, as columns.
            const auto groups = ValueGroups(chart);
            double lowest = std::numeric_limits<double>::infinity(), highest = -lowest;
            size_t total = 0;
            for (const auto& group : groups) {
                for (double value : group.second) {
                    lowest = std::min(lowest, value);
                    highest = std::max(highest, value);
                }
                total += group.second.size();
            }
            if (total == 0) return nullptr;
            int bins = chart.bins > 0 ? chart.bins
                                      : static_cast<int>(std::ceil(std::log2(static_cast<double>(total)) + 1.0));   // Sturges
            bins = std::clamp(bins, 1, 60);
            if (highest <= lowest) highest = lowest + 1.0;
            const double step = (highest - lowest) / bins;
            std::vector<std::string> names;
            for (int b = 0; b < bins; ++b) names.push_back(ShortNumber(lowest + b * step) + "-" + ShortNumber(lowest + (b + 1) * step));
            std::vector<EngineBarSeries> list;
            for (size_t g = 0; g < groups.size(); ++g) {
                EngineBarSeries series;
                series.name = groups[g].first;
                series.color = SeriesColor(chart, g);
                series.values.assign(static_cast<size_t>(bins), 0.0);
                for (double value : groups[g].second) {
                    const int at = std::clamp(static_cast<int>((value - lowest) / step), 0, bins - 1);
                    series.values[static_cast<size_t>(at)] += 1.0;
                }
                list.push_back(std::move(series));
            }
            auto bars = Still(CreateEngineBarChart("DocumentChart", 0, 0, width, height));
            bars->SetCategories(names);
            bars->SetSeries(list);
            bars->SetSlotFill(0.98);
            return bars;
        }
        default:
            return nullptr;
    }
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
        default:
            // The engine's other charts; every other kind is drawn by an
            // element of its own (CreateRichChartElement).
            element = CreateOtherEngineChart(chart, width, height);
            break;
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
    if (chart.IsChart() && !IsRichChartEngineKind(chart.kind)) {
        // Another chart element: drawn afresh (DrawRichChart keeps it).
        if (cached) cached->reset();
        return DrawRichChart(ctx, chart, target, natural, nullptr, ink);
    }
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

// ============================================================================
// EVERY KIND
// ============================================================================

bool IsRichChartEngineKind(RichChart::Kind kind) {
    using Kind = RichChart::Kind;
    switch (kind) {
        case Kind::Column: case Kind::Bar: case Kind::Line: case Kind::Area: case Kind::Pie: case Kind::Doughnut:
        case Kind::Scatter: case Kind::Radar: case Kind::Bubble: case Kind::Stock: case Kind::Waterfall:
        case Kind::DivergingBar: case Kind::PopulationPyramid: case Kind::Dumbbell: case Kind::Mekko:
        case Kind::Polar: case Kind::RadialBar: case Kind::ImportancePerformance: case Kind::Ternary:
        case Kind::Ridgeline: case Kind::Heatmap: case Kind::Histogram:
            return true;
        default:
            return false;
    }
}

namespace {

// A chart element of the framework's older family, shown as a page shows
// it: complete from the first frame, no tooltips, the chart's title.
void AsOnPage(UltraCanvasChartElementBase& element, const RichChart& chart) {
    element.SetEntranceAnimation(false);
    element.SetEnableTooltips(false);
    if (!chart.title.empty()) element.SetChartTitle(chart.title);
}

std::shared_ptr<UltraCanvasUIElement> CreateOtherChartElement(const RichChart& chart, int width, int height) {
    using Kind = RichChart::Kind;
    const auto numbers = NumberSeries(chart);
    const size_t points = PointCount(chart);
    const std::string id = "DocumentChart";
    switch (chart.kind) {
        case Kind::CumulativeFlow: {
            if (numbers.empty()) return nullptr;
            auto flow = CreateCumulativeFlowChartElement(id, 0, 0, width, height);
            for (size_t g = 0; g < numbers.size(); ++g) flow->AddStage(numbers[g]->name, numbers[g]->values, SeriesColor(chart, g));
            flow->SetPeriodLabels(chart.categories);
            AsOnPage(*flow, chart);
            return flow;
        }
        case Kind::NestedArea: {
            // Only the positive values: an area of nothing has no shape.
            const RichChartSeries* values = FirstWithValues(numbers);
            if (!values) return nullptr;
            auto nested = CreateNestedAreaChartElement(id, 0, 0, width, height);
            size_t shapes = 0;
            for (size_t i = 0; i < values->values.size(); ++i) {
                if (ValueAt(values, i) <= 0.0) continue;
                nested->AddDataPoint(PointName(chart, i), ValueAt(values, i), PointColor(chart, i));
                ++shapes;
            }
            if (shapes == 0) return nullptr;
            AsOnPage(*nested, chart);
            return nested;
        }
        case Kind::CircularProgress: {
            // A ring per name, its value out of the second series' (or 100).
            if (numbers.empty()) return nullptr;
            auto rings = CreateCircularProgressChart(id, 0, 0, width, height);
            double highest = 100.0;
            for (double value : FirstWithValues(numbers)->values) if (std::isfinite(value)) highest = std::max(highest, value);
            for (size_t i = 0; i < FirstWithValues(numbers)->values.size() && i < 12; ++i) {
                const size_t ring = rings->AddRing(PointName(chart, i), ValueAt(FirstWithValues(numbers), i), PointColor(chart, i));
                const double most = numbers.size() > 1 && ValueAt(numbers[1], i) > 0 ? ValueAt(numbers[1], i) : highest;
                rings->SetRingRange(ring, 0.0, most);
            }
            // (Its title is the series' name, which the rings draw twice.)
            RichChart untitled = chart;
            untitled.title.clear();
            AsOnPage(*rings, untitled);
            return rings;
        }
        case Kind::Gauge: {
            if (numbers.empty() || FirstWithValues(numbers)->values.empty()) return nullptr;
            const double value = ValueAt(FirstWithValues(numbers), 0);
            double most = numbers.size() > 1 ? ValueAt(numbers[1], 0) : 0.0;
            if (most <= 0.0) {
                most = value <= 100.0 ? 100.0 : NiceNumber(value * 1.2, false);
            }
            auto gauge = CreateGaugeDiagramElement(id, 0, 0, width, height);
            gauge->SetMode(GaugeMode::Semicircular);
            gauge->SetMinValue(0.0);
            gauge->SetMaxValue(most);
            gauge->SetValue(value);
            gauge->SetTitle(!chart.title.empty() ? chart.title : !chart.categories.empty() ? chart.categories.front()
                                                                                          : FirstWithValues(numbers)->name);
            return gauge;
        }
        case Kind::Quadrant: {
            if (numbers.size() < 2) return nullptr;
            auto quadrant = CreateQuadrantChartElement(id, 0, 0, width, height);
            QuadrantAxisConfiguration axes;
            double xLow = std::numeric_limits<double>::infinity(), xHigh = -xLow, yLow = xLow, yHigh = -xLow;
            for (size_t i = 0; i < points; ++i) {
                xLow = std::min(xLow, ValueAt(numbers[0], i));
                xHigh = std::max(xHigh, ValueAt(numbers[0], i));
                yLow = std::min(yLow, ValueAt(numbers[1], i));
                yHigh = std::max(yHigh, ValueAt(numbers[1], i));
            }
            if (!(xHigh > xLow)) { xLow -= 1.0; xHigh += 1.0; }
            if (!(yHigh > yLow)) { yLow -= 1.0; yHigh += 1.0; }
            const double xPad = (xHigh - xLow) * 0.1, yPad = (yHigh - yLow) * 0.1;
            axes.xMin = static_cast<float>(xLow - xPad);
            axes.xMax = static_cast<float>(xHigh + xPad);
            axes.yMin = static_cast<float>(yLow - yPad);
            axes.yMax = static_cast<float>(yHigh + yPad);
            axes.xCenter = static_cast<float>((xLow + xHigh) / 2.0);
            axes.yCenter = static_cast<float>((yLow + yHigh) / 2.0);
            axes.xAxisLabel = numbers[0]->name;
            axes.yAxisLabel = numbers[1]->name;
            quadrant->SetAxisConfiguration(axes);
            for (size_t i = 0; i < points; ++i)
                quadrant->AddDataPoint(PointName(chart, i), static_cast<float>(ValueAt(numbers[0], i)),
                                       static_cast<float>(ValueAt(numbers[1], i)), SeriesColor(chart, 0));
            AsOnPage(*quadrant, chart);
            return quadrant;
        }
        case Kind::Hexbin: {
            if (numbers.size() < 2) return nullptr;
            std::vector<Point2Dd> cloud;
            double xLow = std::numeric_limits<double>::infinity(), xHigh = -xLow, yLow = xLow, yHigh = -xLow;
            for (size_t i = 0; i < numbers[0]->values.size() && i < numbers[1]->values.size(); ++i) {
                const double x = numbers[0]->values[i], y = numbers[1]->values[i];
                if (!std::isfinite(x) || !std::isfinite(y)) continue;
                cloud.emplace_back(x, y);
                xLow = std::min(xLow, x); xHigh = std::max(xHigh, x);
                yLow = std::min(yLow, y); yHigh = std::max(yHigh, y);
            }
            if (cloud.empty()) return nullptr;
            if (!(xHigh > xLow)) { xLow -= 1.0; xHigh += 1.0; }
            if (!(yHigh > yLow)) { yLow -= 1.0; yHigh += 1.0; }
            auto hexbin = CreateHexbinChartElement(id, 0, 0, width, height);
            const int columns = std::clamp(static_cast<int>(std::sqrt(static_cast<double>(cloud.size())) * 1.5), 4, 24);
            hexbin->SetBinnedData(cloud, columns, std::max(3, columns * 3 / 4), xLow, xHigh, yLow, yHigh);
            AsOnPage(*hexbin, chart);
            return hexbin;
        }
        case Kind::Scatter3D: {
            if (numbers.size() < 3) return nullptr;
            auto data = std::make_shared<ChartDataVector>();
            for (size_t i = 0; i < points; ++i)
                data->AddPoint(ChartDataPoint(ValueAt(numbers[0], i), ValueAt(numbers[1], i), ValueAt(numbers[2], i), PointName(chart, i)));
            auto cloud = CreateScatterPlot3DElement(id, 0, 0, width, height);
            cloud->SetDataSource(data);
            cloud->SetAxisTitles(numbers[0]->name, numbers[1]->name, numbers[2]->name);
            AsOnPage(*cloud, chart);
            return cloud;
        }
        case Kind::BoxWhisker:
        case Kind::Jitter: {
            const auto groups = ValueGroups(chart);
            if (groups.empty()) return nullptr;
            std::vector<std::string> names;
            for (const auto& group : groups) names.push_back(group.first);
            auto jitter = CreateJitterPlotWithCategories(id, 0, 0, width, height, names, chart.title);
            for (const auto& group : groups) jitter->AddCategoryData(group.first, group.second);
            if (chart.kind == Kind::BoxWhisker) jitter->SetHybridMode(JitterHybridMode::JitterBoxPlot);
            AsOnPage(*jitter, chart);
            return jitter;
        }
        case Kind::Funnel: {
            if (numbers.empty()) return nullptr;
            auto funnel = CreateFunnelChart(id, 0, 0, width, height);
            for (size_t i = 0; i < FirstWithValues(numbers)->values.size(); ++i) funnel->AddStage(PointName(chart, i), ValueAt(FirstWithValues(numbers), i));
            funnel->SetAnimationEnabled(false);
            AsOnPage(*funnel, chart);
            return funnel;
        }
        case Kind::Pyramid: {
            if (numbers.empty()) return nullptr;
            auto pyramid = CreatePyramidChart(id, 0, 0, width, height);
            for (size_t i = 0; i < FirstWithValues(numbers)->values.size(); ++i) pyramid->AddLevel(PointName(chart, i), ValueAt(FirstWithValues(numbers), i));
            pyramid->SetAnimationEnabled(false);
            AsOnPage(*pyramid, chart);
            return pyramid;
        }
        case Kind::Sankey: {
            // Sources (the names), targets (the first column of words) and
            // the amounts (the last column of numbers).
            const auto words = WordSeries(chart);
            if (words.empty() || numbers.empty()) return nullptr;
            auto sankey = CreateSankeyRenderer(id, 0, 0, static_cast<float>(width), static_cast<float>(height));
            std::set<std::string> nodes;
            const RichChartSeries* targets = words.front();
            const RichChartSeries* amounts = numbers.back();
            size_t links = 0;
            for (size_t i = 0; i < targets->labels.size(); ++i) {
                const std::string from = PointName(chart, i), to = targets->labels[i];
                const double amount = ValueAt(amounts, i);
                if (to.empty() || from == to || amount <= 0.0) continue;
                for (const std::string& node : {from, to}) {
                    if (nodes.insert(node).second) sankey->AddNode(node, node, PointColor(chart, nodes.size() - 1));
                }
                sankey->AddLink(from, to, static_cast<float>(amount));
                ++links;
            }
            if (links == 0) return nullptr;
            sankey->PerformLayout();
            return sankey;
        }
        case Kind::Chord: {
            // A square matrix: the names down, the same across.
            auto matrix = Matrix(chart);
            const size_t size = std::min(matrix.size(), numbers.size());
            if (size < 2) return nullptr;
            matrix.resize(size);
            for (auto& row : matrix) row.resize(size);
            std::vector<std::string> names;
            std::vector<Color> colours;
            for (size_t i = 0; i < size; ++i) {
                names.push_back(PointName(chart, i));
                colours.push_back(PointColor(chart, i));
            }
            auto chord = CreateChordChart(id, 0, 0, width, height);
            chord->ImportFromMatrix(matrix, names, colours);
            AsOnPage(*chord, chart);
            return chord;
        }
        case Kind::Treemap:
        case Kind::Sunburst: {
            // A path per row: the name, then the deeper levels' words; the
            // value from the last column of numbers.
            if (numbers.empty()) return nullptr;
            const auto words = WordSeries(chart);
            std::vector<std::vector<std::string>> paths;
            std::vector<double> values;
            for (size_t i = 0; i < points; ++i) {
                std::vector<std::string> path{PointName(chart, i)};
                for (const RichChartSeries* level : words) {
                    if (i < level->labels.size() && !level->labels[i].empty()) path.push_back(level->labels[i]);
                }
                const double value = ValueAt(numbers.back(), i);
                if (value <= 0.0) continue;
                paths.push_back(std::move(path));
                values.push_back(value);
            }
            if (paths.empty()) return nullptr;
            if (chart.kind == Kind::Sunburst) {
                auto sunburst = CreateSunburstChart(id, 0, 0, width, height);
                for (size_t i = 0; i < paths.size(); ++i) sunburst->AddNode(paths[i], values[i]);
                AsOnPage(*sunburst, chart);
                return sunburst;
            }
            std::vector<std::tuple<std::string, std::string, double, double>> rows;
            for (size_t i = 0; i < paths.size(); ++i) {
                std::string joined;
                for (const std::string& part : paths[i]) joined += (joined.empty() ? "" : "/") + part;
                rows.emplace_back(joined, paths[i].back(), values[i], 0.0);
            }
            auto treemap = CreateTreeMap(id, 0, 0, width, height);
            treemap->SetAnimationEnabled(false);
            treemap->SetDataFromHierarchy(rows);
            // Each branch in the office palette (a node is white until coloured).
            treemap->SetColorPalette(std::vector<Color>(std::begin(kOffice), std::end(kOffice)));
            AsOnPage(*treemap, chart);
            return treemap;
        }
        case Kind::Gantt: {
            // Tasks, starts and ends (an end before its start: a duration).
            if (numbers.size() < 2) return nullptr;
            auto plan = std::make_shared<GanttDataSource>();
            size_t tasks = 0;
            for (size_t i = 0; i < points; ++i) {
                const double start = ValueAt(numbers[0], i);
                double end = ValueAt(numbers[1], i);
                if (start <= 0.0) continue;
                if (end < start) end = start + std::max(0.0, end - 1.0);
                if (end <= start) plan->AddMilestone(PointName(chart, i), GanttDate(UnixDays(start)));
                else plan->AddTask(PointName(chart, i), GanttDate(UnixDays(start)), GanttDate(UnixDays(end)));
                ++tasks;
            }
            if (tasks == 0) return nullptr;
            auto gantt = CreateGanttChartElement(id, 0, 0, width, height);
            gantt->SetGanttDataSource(plan);
            gantt->SetShowTable(width >= 420);
            gantt->FitToRange();
            AsOnPage(*gantt, chart);
            return gantt;
        }
        case Kind::Timeline: {
            if (numbers.empty()) return nullptr;
            auto events = std::make_shared<TimelineChartDataSource>();
            for (size_t i = 0; i < points; ++i) {
                const double start = ValueAt(numbers[0], i);
                if (start <= 0.0) continue;
                const double end = numbers.size() > 1 ? ValueAt(numbers[1], i) : 0.0;
                const double from = static_cast<double>(UnixDays(start));
                if (end > start) events->AddSpan(PointName(chart, i), from, static_cast<double>(UnixDays(end)));
                else events->AddMilestone(PointName(chart, i), from);
            }
            if (events->Count() == 0) return nullptr;
            auto timeline = CreateTimelineChartElement(id, 0, 0, width, height);
            timeline->SetData(events);
            AsOnPage(*timeline, chart);
            return timeline;
        }
        case Kind::CalendarHeatmap: {
            // Dates and their values.
            if (numbers.size() < 2) return nullptr;
            auto calendar = CreateCalendarHeatmapElement(id, 0, 0, width, height);
            long first = std::numeric_limits<long>::max(), last = std::numeric_limits<long>::min();
            for (size_t i = 0; i < numbers[0]->values.size(); ++i) {
                const double serial = ValueAt(numbers[0], i);
                if (serial <= 0.0) continue;
                const long day = UnixDays(serial);
                first = std::min(first, day);
                last = std::max(last, day);
            }
            if (first > last) return nullptr;
            int y1 = 0, m1 = 0, d1 = 0, y2 = 0, m2 = 0, d2 = 0;
            CalendarCivilFromDays(first, y1, m1, d1);
            CalendarCivilFromDays(last, y2, m2, d2);
            calendar->SetDateRange(y1, m1, d1, y2, m2, d2);
            for (size_t i = 0; i < numbers[0]->values.size(); ++i) {
                const double serial = ValueAt(numbers[0], i);
                if (serial <= 0.0) continue;
                int y = 0, m = 0, d = 0;
                CalendarCivilFromDays(UnixDays(serial), y, m, d);
                calendar->AddValue(y, m, d, ValueAt(numbers[1], i));
            }
            AsOnPage(*calendar, chart);
            return calendar;
        }
        case Kind::Contour: {
            const auto matrix = Matrix(chart);
            if (matrix.size() < 2 || matrix.front().size() < 2) return nullptr;
            auto contour = CreateContourChartElement(id, 0, 0, width, height);
            contour->SetData(matrix);
            AsOnPage(*contour, chart);
            return contour;
        }
        case Kind::Surface: {
            const auto matrix = Matrix(chart);
            if (matrix.size() < 2 || matrix.front().size() < 2) return nullptr;
            auto surface = CreateContourSurface3DElement(id, 0, 0, width, height);
            surface->SetData(matrix);
            AsOnPage(*surface, chart);
            return surface;
        }
        case Kind::WordCloud: {
            if (numbers.empty()) return nullptr;
            auto cloud = CreateWordCloud(id, 0, 0, width, height);
            for (size_t i = 0; i < FirstWithValues(numbers)->values.size(); ++i) {
                const double weight = ValueAt(FirstWithValues(numbers), i);
                if (weight > 0.0) cloud->AddWord(PointName(chart, i), weight);
            }
            AsOnPage(*cloud, chart);
            return cloud;
        }
        case Kind::Parliament: {
            if (numbers.empty()) return nullptr;
            auto chamber = CreateParliamentDiagram(id, 0, 0, width, height);
            int seats = 0;
            for (size_t i = 0; i < FirstWithValues(numbers)->values.size(); ++i) {
                const int count = static_cast<int>(std::lround(std::max(0.0, ValueAt(FirstWithValues(numbers), i))));
                if (count <= 0) continue;
                chamber->AddParty(PointName(chart, i), count, PointColor(chart, i));
                seats += count;
            }
            if (seats == 0 || seats > 5000) return nullptr;
            AsOnPage(*chamber, chart);
            return chamber;
        }
        default:
            return nullptr;
    }
}

} // namespace

std::shared_ptr<UltraCanvasUIElement> CreateRichChartElement(const RichChart& chart, int width, int height, const Color& ink) {
    if (!chart.IsChart() || chart.series.empty() || width < 8 || height < 8) return nullptr;
    if (IsRichChartEngineKind(chart.kind)) return CreateRichDocumentChart(chart, width, height, ink);
    auto element = CreateOtherChartElement(chart, width, height);
    if (element) element->SetBounds(Rect2Df(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)));
    return element;
}

bool DrawRichChart(IRenderContext* ctx, const RichChart& chart, const Rect2Dd& target, const Size2Di& natural,
                   std::shared_ptr<UltraCanvasUIElement>* cached, const Color& ink) {
    if (!ctx || natural.width < 8 || natural.height < 8 || target.width <= 0.0 || target.height <= 0.0) return false;
    std::shared_ptr<UltraCanvasUIElement> local;
    std::shared_ptr<UltraCanvasUIElement>& element = cached ? *cached : local;
    if (!element || element->GetWidth() != natural.width || element->GetHeight() != natural.height) {
        element = CreateRichChartElement(chart, natural.width, natural.height, ink);
    }
    if (!element) return false;
    ctx->PushState();
    ctx->Translate(target.x, target.y);
    const double scaleX = target.width / natural.width, scaleY = target.height / natural.height;
    if (std::abs(scaleX - 1.0) > 1e-6 || std::abs(scaleY - 1.0) > 1e-6) ctx->Scale(scaleX, scaleY);
    ctx->ClipRect(Rect2Dd(0.0, 0.0, natural.width, natural.height));
    element->Render(ctx, Rect2Df(0.0f, 0.0f, static_cast<float>(natural.width), static_cast<float>(natural.height)));
    ctx->PopState();
    return true;
}

} // namespace UltraCanvas
