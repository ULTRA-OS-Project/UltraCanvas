// Plugins/Charts/Engine/Charts/UltraCanvasEngineBarChart.cpp
// Column and bar chart on the chart engine - see the header.
// Version: 1.0.0
// Last Modified: 2026-11-18
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineBarChart.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

UltraCanvasEngineBarChart::UltraCanvasEngineBarChart(const std::string& id, int x, int y, int w, int h)
    : UltraCanvasChartEngineElement(id, x, y, w, h) {
}

void UltraCanvasEngineBarChart::SetCategories(const std::vector<std::string>& categories) {
    categoryNames = categories;
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineBarChart::SetSeries(const std::vector<EngineBarSeries>& seriesList) {
    series = seriesList;
    hidden.assign(series.size(), false);
    RefreshLegend();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineBarChart::AddSeries(const EngineBarSeries& one) {
    series.push_back(one);
    hidden.push_back(false);
    RefreshLegend();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineBarChart::ClearSeries() {
    series.clear();
    hidden.clear();
    RefreshLegend();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineBarChart::SetArrangement(ChartBarArrangement arrangement) {
    layout.arrangement = arrangement;
    MarkEngineDirty(ChartDirty::Data);          // the value range changes with it
}

void UltraCanvasEngineBarChart::SetHorizontal(bool horizontal) {
    SetProjectionKind(horizontal ? ChartProjectionKind::Horizontal : ChartProjectionKind::Vertical);
    MarkEngineDirty(ChartDirty::Data);          // the axes change sides
}

void UltraCanvasEngineBarChart::SetSlotFill(double fraction, double groupGap) {
    layout.slotFill = std::clamp(fraction, 0.05, 1.0);
    layout.groupGap = std::max(0.0, groupGap);
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineBarChart::SetCornerRadius(double pixels) {
    cornerRadius = std::max(0.0, pixels);
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineBarChart::SetValueFormat(const std::string& prefix, const std::string& suffix,
                                               bool compact, int decimals) {
    valuePrefix = prefix;
    valueSuffix = suffix;
    compactValues = compact;
    valueDecimals = decimals;
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineBarChart::SetValueRange(double lo, double hi) {
    statedRange = hi > lo;
    rangeLow = lo;
    rangeHigh = hi;
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineBarChart::SetAutoValueRange() {
    statedRange = false;
    MarkEngineDirty(ChartDirty::Data);
}

size_t UltraCanvasEngineBarChart::CategoryCount() const {
    size_t count = categoryNames.size();
    for (const EngineBarSeries& one : series) count = std::max(count, one.values.size());
    return count;
}

// One value vector per series; a series switched off in the legend keeps its
// place in the group and has no bars.
std::vector<std::vector<double>> UltraCanvasEngineBarChart::SeriesValues() const {
    std::vector<std::vector<double>> values;
    values.reserve(series.size());
    for (size_t s = 0; s < series.size(); ++s) {
        values.push_back(s < hidden.size() && hidden[s] ? std::vector<double>() : series[s].values);
    }
    return values;
}

Color UltraCanvasEngineBarChart::SeriesColor(size_t index) const {
    if (index < series.size() && series[index].color.a > 0) return series[index].color;
    return Palette().ColorAt(index, series.size());
}

void UltraCanvasEngineBarChart::RefreshLegend() {
    std::vector<ChartLegendEntry> entries;
    for (size_t s = 0; s < series.size(); ++s) {
        ChartLegendEntry entry(series[s].name, SeriesColor(s), LegendSwatch::Square);
        entry.enabled = !(s < hidden.size() && hidden[s]);
        entries.push_back(entry);
    }
    SetLegendEntries(entries);
}

void UltraCanvasEngineBarChart::OnChartThemeChanged() {
    RefreshLegend();                             // entries store their colour
}

void UltraCanvasEngineBarChart::OnLegendEntryToggled(size_t entryIndex, bool enabled) {
    if (entryIndex >= hidden.size()) return;
    hidden[entryIndex] = !enabled;
    MarkEngineDirty(ChartDirty::Data);           // the range and the stacks change
}

void UltraCanvasEngineBarChart::DescribeAxes(ChartAxisSet& axes) {
    const bool horizontal = IsHorizontal();

    ChartAxis value("value");
    value.side = horizontal ? ChartAxisSide::Bottom : ChartAxisSide::Left;
    if (layout.arrangement == ChartBarArrangement::PercentStacked) {
        value.unitSuffix = "%";
    } else {
        value.unitPrefix = valuePrefix;
        value.unitSuffix = valueSuffix;
        value.compactNumbers = compactValues;
        value.decimals = valueDecimals;
    }
    if (statedRange && layout.arrangement != ChartBarArrangement::PercentStacked) value.SetRange(rangeLow, rangeHigh);
    else ObserveBarSeries(value, SeriesValues(), layout);
    axes.Add(value);

    // One slot per category, the outer slots as wide as the inner ones.
    ChartAxis category("category");
    category.scale = ChartScale::Category;
    category.side = horizontal ? ChartAxisSide::Left : ChartAxisSide::Bottom;
    category.categories = categoryNames;
    for (size_t c = category.categories.size(); c < CategoryCount(); ++c) category.categories.push_back(std::to_string(c + 1));
    category.categoryPadding = 0.5;
    // Lying down, the projection runs the domain downward; the first
    // category belongs at the foot.
    category.inverted = horizontal;
    axes.Add(category);

    SetGridAxis(0);
}

std::vector<ChartBarSpan> UltraCanvasEngineBarChart::BarSpans() const {
    const ChartAxisSet& axes = Axes();
    if (series.empty() || axes.Count() < 2) return {};
    std::vector<ChartBarSpan> spans = BuildBarSpans(axes.At(0), axes.At(1), CategoryCount(), SeriesValues(), layout);
    MirrorGroups(spans, axes.At(1));
    return spans;
}

// Lying down, a group reads upward like its categories do: the first series
// lowest. Each bar is mirrored about its slot's centre.
void UltraCanvasEngineBarChart::MirrorGroups(std::vector<ChartBarSpan>& spans, const ChartAxis& category) const {
    if (!IsHorizontal() || layout.arrangement != ChartBarArrangement::Grouped) return;
    for (ChartBarSpan& span : spans) {
        const double centre = category.Normalize(static_cast<double>(span.categoryIndex));
        const double u0 = 2.0 * centre - span.u1, u1 = 2.0 * centre - span.u0;
        span.u0 = u0;
        span.u1 = u1;
    }
}

void UltraCanvasEngineBarChart::RenderChartContent(IRenderContext* ctx, const ChartEngineFrame& frame) {
    if (series.empty() || frame.axes->Count() < 2) return;
    const ChartAxis& value = frame.axes->At(0);
    const ChartAxis& category = frame.axes->At(1);
    const bool orthogonal = GetProjectionKind() != ChartProjectionKind::Polar;
    const double zero = std::clamp(value.Normalize(0.0), 0.0, 1.0);
    const double progress = frame.animationProgress;

    std::vector<ChartBarSpan> spans = BuildBarSpans(value, category, CategoryCount(), SeriesValues(), layout);
    MirrorGroups(spans, category);
    for (const ChartBarSpan& span : spans) {
        // The bars grow out of the zero line together.
        const double v0 = zero + (span.v0 - zero) * progress;
        const double v1 = zero + (span.v1 - zero) * progress;
        const std::vector<Point2Dd> outline = BuildBarOutline(*frame.projection, span.u0, v0, span.u1, v1,
                                                              orthogonal ? 1 : 8, cornerRadius);
        if (outline.size() < 3) continue;
        const int64_t regionId = static_cast<int64_t>(span.seriesIndex) * 100000 + static_cast<int64_t>(span.categoryIndex);
        Color color = SeriesColor(span.seriesIndex);
        if (HoveredRegionId() == regionId) color = color.WithAlpha(210);
        ctx->SetFillPaint(color);
        ctx->FillLinePath(outline);

        std::string tooltip = span.seriesIndex < series.size() ? series[span.seriesIndex].name : std::string();
        if (span.categoryIndex < category.categories.size()) {
            tooltip += (tooltip.empty() ? "" : ", ") + category.categories[span.categoryIndex];
        }
        tooltip += (tooltip.empty() ? "" : ": ") + value.FormatValue(span.plotted);
        if (orthogonal) {
            const Point2Dd a = frame.projection->ToScreen(ChartNormalizedPoint(span.u0, v0));
            const Point2Dd b = frame.projection->ToScreen(ChartNormalizedPoint(span.u1, v1));
            AddHitRegion(Rect2Dd(std::min(a.x, b.x), std::min(a.y, b.y), std::abs(b.x - a.x), std::abs(b.y - a.y)),
                         regionId, tooltip);
        } else {
            AddHitRegion(outline, regionId, tooltip);
        }
    }
}

} // namespace UltraCanvas
