// Plugins/Charts/Engine/Charts/UltraCanvasEngineLineChart.cpp
// Line, area, stacked area and cumulative flow charts on the chart engine.
// Version: 1.1.0 - SetValueAtLeast
// Version: 1.0.0
// Last Modified: 2026-10-24
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineLineChart.h"
#include <algorithm>
#include <limits>
#include <sstream>
#include <cmath>
#include <cstdlib>

namespace UltraCanvas {

namespace {

bool Finite(double v) { return std::isfinite(v); }

// Where a rising (cumulative) edge first reaches `value`, as a fractional
// index; NaN when it never does.
double CrossingIndex(const std::vector<double>& edge, double value) {
    for (size_t i = 0; i < edge.size(); ++i) {
        if (!Finite(edge[i]) || edge[i] < value) continue;
        if (i == 0 || !Finite(edge[i - 1])) return static_cast<double>(i);
        const double a = edge[i - 1], b = edge[i];
        const double t = (std::abs(b - a) < 1e-12) ? 0.0 : (value - a) / (b - a);
        return static_cast<double>(i - 1) + std::clamp(t, 0.0, 1.0);
    }
    return std::nan("");
}

double Interpolate(const std::vector<double>& edge, double at) {
    if (edge.empty()) return std::nan("");
    const double clamped = std::clamp(at, 0.0, static_cast<double>(edge.size() - 1));
    const size_t i0 = static_cast<size_t>(std::floor(clamped));
    const size_t i1 = std::min(i0 + 1, edge.size() - 1);
    const double t = clamped - static_cast<double>(i0);
    return edge[i0] * (1.0 - t) + edge[i1] * t;
}

} // namespace

UltraCanvasEngineLineChart::UltraCanvasEngineLineChart(const std::string& id, int x, int y,
                                                       int w, int h)
    : UltraCanvasChartEngineElement(id, x, y, w, h) {
    SetAnimateOnDataChange(true, 0.7f);    // lines rise from the base on every data change
    SetLegendPosition(ChartLegendPosition::TopCenter);
}

// =============================================================================
// DATA
// =============================================================================

void UltraCanvasEngineLineChart::SetCategories(const std::vector<std::string>& names) {
    categories = names;
    xValues.clear();
    RebuildSpanAnnotations();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineLineChart::SetXValues(const std::vector<double>& values) {
    xValues = values;
    categories.clear();
    RebuildSpanAnnotations();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineLineChart::SetSeries(const std::vector<EngineLineSeries>& seriesList) {
    series = seriesList;
    hidden.assign(series.size(), false);
    SetShowLegend(series.size() > 1);
    RefreshLegend();
    RebuildSpanAnnotations();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineLineChart::AddSeries(const EngineLineSeries& s) {
    series.push_back(s);
    hidden.push_back(false);
    SetShowLegend(series.size() > 1);
    RefreshLegend();
    RebuildSpanAnnotations();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineLineChart::ClearSeries() {
    series.clear();
    hidden.clear();
    RefreshLegend();
    RebuildSpanAnnotations();
    MarkEngineDirty(ChartDirty::Data);
}

// =============================================================================
// PRESENTATION
// =============================================================================

void UltraCanvasEngineLineChart::SetMode(EngineLineChartMode m) {
    mode = m;
    RefreshLegend();                    // swatches mirror the mark
    RebuildSpanAnnotations();
    MarkEngineDirty(ChartDirty::Data);  // the value range follows the mode
}

void UltraCanvasEngineLineChart::SetLineShape(ChartLineShape shape) {
    lineOptions.shape = shape;
    RequestRedraw();                    // geometry only - nothing moves in the layout
}

void UltraCanvasEngineLineChart::SetBridgeGaps(bool bridge) {
    lineOptions.bridgeGaps = bridge;
    RequestRedraw();
}

void UltraCanvasEngineLineChart::SetLineWidth(float width) {
    lineWidth = std::max(0.5f, width);
    RequestRedraw();
}

void UltraCanvasEngineLineChart::SetShowMarkers(bool show) {
    showMarkers = show;
    RequestRedraw();
}

void UltraCanvasEngineLineChart::SetMarkerRadius(float radius) {
    markerRadius = std::max(1.0f, radius);
    RequestRedraw();
}

void UltraCanvasEngineLineChart::SetMarkerColor(const Color& color) {
    markerColor = color;
    RequestRedraw();
}

void UltraCanvasEngineLineChart::SetAreaOpacity(float opacity) {
    areaOpacity = std::clamp(opacity, 0.0f, 1.0f);
    RequestRedraw();
}

void UltraCanvasEngineLineChart::SetGradientFill(bool gradient) {
    gradientFill = gradient;
    RefreshLegend();
    RequestRedraw();
}

void UltraCanvasEngineLineChart::SetShowEdgeLines(bool show) {
    showEdgeLines = show;
    RequestRedraw();
}

void UltraCanvasEngineLineChart::SetShowValueLabels(bool show) {
    showValueLabels = show;
    MarkEngineDirty(ChartDirty::Style);     // the label plan changes
}

void UltraCanvasEngineLineChart::SetValueFormat(const std::string& prefix,
                                                const std::string& suffix, bool compact,
                                                int decimals) {
    valuePrefix = prefix;
    valueSuffix = suffix;
    compactValues = compact;
    valueDecimals = decimals;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineLineChart::SetIncludeZero(bool include) {
    includeZero = include;
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineLineChart::SetValueAtLeast(double value) {
    valueAtLeast = value;
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineLineChart::SetCategoryPadding(double slots) {
    categoryPadding = std::max(0.0, slots);
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineLineChart::SetAxisTitles(const std::string& domain,
                                               const std::string& value) {
    domainTitle = domain;
    valueTitle = value;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineLineChart::SetReverseStackOrder(bool reverse) {
    reverseStack = reverse;
    RebuildSpanAnnotations();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineLineChart::AddSeriesSpan(const EngineLineSpan& span) {
    spans.push_back(span);
    RebuildSpanAnnotations();
}

void UltraCanvasEngineLineChart::ClearSeriesSpans() {
    spans.clear();
    RebuildSpanAnnotations();
}

void UltraCanvasEngineLineChart::AddCumulativeFlowAnnotations(const std::string& leadLabel,
                                                              const std::string& cycleLabel,
                                                              const std::string& wipLabel) {
    if (series.size() < 2) return;
    const size_t done = series.size() - 1;
    const size_t started = std::min<size_t>(1, done);
    // The legacy chart's auto placement: lead time at half the final total,
    // cycle time a little below it, WIP a third of the way along.
    spans.push_back({EngineLineSpanKind::AcrossDomain, 0, done, std::nan(""), leadLabel});
    spans.push_back({EngineLineSpanKind::AcrossDomain, started, done, -0.38, cycleLabel});
    spans.push_back({EngineLineSpanKind::AcrossValue, started, done, std::nan(""), wipLabel});
    RebuildSpanAnnotations();
}

// =============================================================================
// HELPERS
// =============================================================================

bool UltraCanvasEngineLineChart::Stacked() const {
    return mode == EngineLineChartMode::StackedArea ||
           mode == EngineLineChartMode::PercentStackedArea;
}

bool UltraCanvasEngineLineChart::Filled() const {
    return mode != EngineLineChartMode::Line;
}

size_t UltraCanvasEngineLineChart::PointCount() const {
    size_t count = categories.empty() ? xValues.size() : categories.size();
    if (count == 0) {
        for (const EngineLineSeries& s : series) count = std::max(count, s.values.size());
    }
    return count;
}

Color UltraCanvasEngineLineChart::SeriesColor(size_t index) const {
    if (index < series.size() && series[index].color.a > 0) return series[index].color;
    return Palette().ColorAt(index, series.size());
}

double UltraCanvasEngineLineChart::DomainValue(size_t index) const {
    if (categories.empty() && index < xValues.size()) return xValues[index];
    return static_cast<double>(index);
}

ChartStackedValues UltraCanvasEngineLineChart::Bands() const {
    const size_t count = PointCount();
    std::vector<std::vector<double>> values(series.size());
    for (size_t s = 0; s < series.size(); ++s) {
        values[s].assign(count, std::nan(""));
        if (s < hidden.size() && hidden[s]) continue;     // drops out of the stack
        for (size_t i = 0; i < count && i < series[s].values.size(); ++i) {
            values[s][i] = series[s].values[i];
        }
    }
    const ChartBarArrangement arrangement =
        mode == EngineLineChartMode::StackedArea ? ChartBarArrangement::Stacked
        : mode == EngineLineChartMode::PercentStackedArea ? ChartBarArrangement::PercentStacked
                                                          : ChartBarArrangement::Grouped;
    if (!reverseStack || arrangement == ChartBarArrangement::Grouped) {
        return StackSeriesValues(values, arrangement);
    }
    std::vector<std::vector<double>> reversed(values.rbegin(), values.rend());
    ChartStackedValues stacked = StackSeriesValues(reversed, arrangement);
    std::reverse(stacked.base.begin(), stacked.base.end());
    std::reverse(stacked.top.begin(), stacked.top.end());
    return stacked;
}

std::string UltraCanvasEngineLineChart::FormatPlotted(const ChartAxis& valueAxis,
                                                      double value) const {
    return valueAxis.FormatValue(value);
}

void UltraCanvasEngineLineChart::RefreshLegend() {
    std::vector<ChartLegendEntry> entries;
    for (size_t s = 0; s < series.size(); ++s) {
        LegendSwatch swatch = LegendSwatch::Line;
        if (Filled()) swatch = gradientFill ? LegendSwatch::Gradient : LegendSwatch::Square;
        else if (series[s].dashed) swatch = LegendSwatch::DashedLine;
        ChartLegendEntry entry(series[s].name, SeriesColor(s), swatch);
        entry.enabled = !(s < hidden.size() && hidden[s]);
        entries.push_back(entry);
    }
    SetLegendEntries(entries);
}

void UltraCanvasEngineLineChart::RebuildSpanAnnotations() {
    ClearSpanAnnotations();
    if (spans.empty() || series.empty() || !Stacked()) return;
    const ChartStackedValues bands = Bands();
    const size_t count = PointCount();
    if (count < 2) return;

    // The final total (the top of the whole stack at the last position)
    // scales the automatic placements.
    double finalTotal = 0.0;
    for (const std::vector<double>& top : bands.top) {
        if (!top.empty() && Finite(top.back())) finalTotal = std::max(finalTotal, top.back());
    }

    for (const EngineLineSpan& span : spans) {
        if (span.fromSeries >= series.size() || span.toSeries >= series.size()) continue;
        const std::vector<double>& a = bands.top[span.fromSeries];
        const std::vector<double>& b = bands.top[span.toSeries];
        ChartSpanAnnotation annotation;
        annotation.xAxisIndex = 1;
        annotation.yAxisIndex = 0;
        annotation.label = span.label;
        if (span.kind == EngineLineSpanKind::AcrossDomain) {
            // A negative `at` is a fraction of the final total.
            double value = span.at;
            if (!Finite(value)) value = finalTotal * 0.5;
            else if (value < 0.0) value = finalTotal * -value;
            double i0 = CrossingIndex(a, value), i1 = CrossingIndex(b, value);
            if (!Finite(i0) || !Finite(i1)) continue;
            if (i1 < i0) std::swap(i0, i1);
            // Fractional indices map to the domain linearly between points.
            auto domainAt = [&](double index) {
                const size_t k = static_cast<size_t>(std::floor(index));
                const double t = index - static_cast<double>(k);
                const double x0 = DomainValue(k), x1 = DomainValue(std::min(k + 1, count - 1));
                return x0 + (x1 - x0) * t;
            };
            annotation.from = Point2Dd(domainAt(i0), value);
            annotation.to = Point2Dd(domainAt(i1), value);
        } else {
            const double index = Finite(span.at) ? span.at
                                                 : static_cast<double>(count - 1) / 3.0;
            const double upper = Interpolate(a, index), lower = Interpolate(b, index);
            if (!Finite(upper) || !Finite(lower)) continue;
            const size_t k = static_cast<size_t>(std::floor(std::clamp(index, 0.0,
                                                 static_cast<double>(count - 1))));
            const double t = index - static_cast<double>(k);
            const double x = DomainValue(k) +
                             (DomainValue(std::min(k + 1, count - 1)) - DomainValue(k)) * t;
            annotation.from = Point2Dd(x, std::max(upper, lower));
            annotation.to = Point2Dd(x, std::min(upper, lower));
        }
        AddSpanAnnotation(annotation);
    }
}

std::vector<ChartNormalizedPoint> UltraCanvasEngineLineChart::TopPoints(
        size_t s, const ChartStackedValues& bands, const ChartAxis& value,
        const ChartAxis& domain, double progress) const {
    const double zero = std::clamp(value.Normalize(0.0), 0.0, 1.0);
    std::vector<ChartNormalizedPoint> points;
    const size_t count = PointCount();
    points.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const double top = (i < bands.top[s].size()) ? bands.top[s][i] : std::nan("");
        const double v = value.Normalize(top);
        points.emplace_back(domain.Normalize(DomainValue(i)), zero + (v - zero) * progress);
    }
    return points;
}

// =============================================================================
// ENGINE CONTRACT
// =============================================================================

void UltraCanvasEngineLineChart::DescribeAxes(ChartAxisSet& axes) {
    const bool horizontal = GetProjectionKind() == ChartProjectionKind::Horizontal;
    const bool polar = GetProjectionKind() == ChartProjectionKind::Polar;

    ChartAxis value("value");
    value.side = horizontal ? ChartAxisSide::Bottom : ChartAxisSide::Left;
    value.title = polar ? std::string() : valueTitle;
    if (mode == EngineLineChartMode::PercentStackedArea) {
        value.unitSuffix = "%";
        value.decimals = 0;
    } else {
        value.unitPrefix = valuePrefix;
        value.unitSuffix = valueSuffix;
        value.compactNumbers = compactValues;
        value.decimals = valueDecimals;
    }

    std::vector<std::vector<double>> visible;
    for (size_t s = 0; s < series.size(); ++s) {
        if (s < hidden.size() && hidden[s]) continue;
        visible.push_back(series[s].values);
    }
    if (Stacked()) {
        ChartBarLayoutOptions options;
        options.arrangement = mode == EngineLineChartMode::StackedArea
                                  ? ChartBarArrangement::Stacked
                                  : ChartBarArrangement::PercentStacked;
        ObserveBarSeries(value, visible, options);
    } else {
        for (const std::vector<double>& values : visible) value.Observe(values);
        if (Filled() || includeZero || polar) value.Observe(0.0);
        if (std::isfinite(valueAtLeast)) value.Observe(valueAtLeast);
        // Keeps the extreme points and their markers clear of the plot edge;
        // a range that starts at zero keeps its baseline.
        value.rangePadding = 0.05;
    }
    axes.Add(value);

    ChartAxis domain("domain");
    domain.side = horizontal ? ChartAxisSide::Left : ChartAxisSide::Bottom;
    domain.title = polar ? std::string() : domainTitle;
    if (!categories.empty() || xValues.empty()) {
        domain.scale = ChartScale::Category;
        domain.categories = categories;
        if (domain.categories.empty()) {
            for (size_t i = 0; i < PointCount(); ++i) {
                domain.categories.push_back(std::to_string(i + 1));
            }
        }
        // Round the circle the categories spread evenly instead of the last
        // landing on the first.
        if (polar) domain.cyclic = true;
        else domain.categoryPadding = categoryPadding;
    } else {
        domain.Observe(xValues);
    }
    axes.Add(domain);

    SetGridAxis(0);
    SetDomainGridAxis(polar ? 1 : static_cast<size_t>(-1));
}

void UltraCanvasEngineLineChart::RenderChartContent(IRenderContext* ctx,
                                                    const ChartEngineFrame& frame) {
    if (series.empty() || !frame.axes || frame.axes->Count() < 2 || PointCount() == 0) return;
    const ChartAxis& value = frame.axes->At(0);
    const ChartAxis& domain = frame.axes->At(1);
    const IChartProjection& projection = *frame.projection;
    const bool orthogonal = GetProjectionKind() != ChartProjectionKind::Polar;
    const double progress = frame.animationProgress;
    const ChartStackedValues bands = Bands();
    const double zero = std::clamp(value.Normalize(0.0), 0.0, 1.0);
    const int64_t hovered = HoveredRegionId();
    const size_t hoveredSeries = hovered >= 0 ? static_cast<size_t>(hovered / 1000000) : SIZE_MAX;
    // Around a full circle of categories the series is a loop: December
    // joins back to January.
    ChartLineOptions shapeOptions = lineOptions;
    shapeOptions.closed = !orthogonal && domain.scale == ChartScale::Category && domain.cyclic;

    std::vector<std::vector<ChartNormalizedPoint>> tops(series.size());
    for (size_t s = 0; s < series.size(); ++s) {
        if (s < hidden.size() && hidden[s]) continue;
        tops[s] = TopPoints(s, bands, value, domain, progress);
    }

    // ---- fills (area modes): bottom of the stack first ----------------------
    if (Filled()) {
        std::vector<size_t> order;
        for (size_t s = 0; s < series.size(); ++s) order.push_back(s);
        if (reverseStack) std::reverse(order.begin(), order.end());
        for (size_t s : order) {
            if (tops[s].empty()) continue;
            std::vector<ChartNormalizedPoint> base;
            base.reserve(tops[s].size());
            for (size_t i = 0; i < tops[s].size(); ++i) {
                const double b = (i < bands.base[s].size()) ? bands.base[s][i] : std::nan("");
                const double v = Finite(b) ? value.Normalize(b) : std::nan("");
                base.emplace_back(tops[s][i].u, zero + (v - zero) * progress);
            }
            const Color color = SeriesColor(s);
            const float alpha = (hoveredSeries == s) ? std::min(1.0f, areaOpacity + 0.2f)
                                                     : areaOpacity;
            for (const std::vector<Point2Dd>& outline :
                 BuildAreaOutlines(projection, tops[s], base, shapeOptions)) {
                if (outline.size() < 3) continue;
                if (gradientFill && orthogonal) {
                    const Point2Dd from = projection.ToScreen(ChartNormalizedPoint(0.5, 1.0));
                    const Point2Dd to = projection.ToScreen(ChartNormalizedPoint(0.5, zero));
                    ctx->SetFillPaint(ctx->CreateLinearGradientPattern(
                        from.x, from.y, to.x, to.y,
                        {GradientStop(0.0, color.WithAlpha(static_cast<uint8_t>(255 * std::min(1.0f, alpha + 0.3f)))),
                         GradientStop(1.0, color.WithAlpha(static_cast<uint8_t>(255 * alpha * 0.2f)))}));
                } else {
                    ctx->SetFillPaint(color.WithAlpha(static_cast<uint8_t>(255 * alpha)));
                }
                ctx->FillLinePath(outline);
            }
        }
    }

    // ---- lines ----------------------------------------------------------------
    std::vector<std::vector<ChartLineRun>> runs(series.size());
    ctx->PushState();
    ctx->SetLineJoin(LineJoin::Round);
    ctx->SetLineCap(LineCap::Round);
    for (size_t s = 0; s < series.size(); ++s) {
        if (tops[s].empty()) continue;
        runs[s] = BuildLineRuns(projection, tops[s], shapeOptions);
        if (Filled() && !showEdgeLines) continue;
        const Color color = SeriesColor(s);
        float width = series[s].lineWidth > 0.0f ? series[s].lineWidth : lineWidth;
        if (Filled()) width = std::max(1.0f, width * 0.7f);
        if (hoveredSeries == s) width += 1.5f;
        ctx->SetStrokePaint(Filled() ? color.Darken(0.2f) : color);
        ctx->SetStrokeWidth(width);
        if (series[s].dashed) ctx->SetLineDash(UCDashPattern({7.0, 4.0}));
        for (const ChartLineRun& run : runs[s]) {
            if (run.points.size() >= 2) ctx->DrawLinePath(run.points, false);
        }
        if (series[s].dashed) ctx->SetLineDash(UCDashPattern());
    }
    ctx->PopState();

    // ---- markers and hit regions ----------------------------------------------
    // Tooltips show the datum as supplied, in the chart's own units - under
    // 100% stacking the axis reads percent, the data does not.
    ChartAxis datum = value;
    if (mode == EngineLineChartMode::PercentStackedArea) {
        datum.unitPrefix = valuePrefix;
        datum.unitSuffix = valueSuffix;
        datum.compactNumbers = compactValues;
        datum.decimals = valueDecimals;
    }
    for (size_t s = 0; s < series.size(); ++s) {
        const Color color = SeriesColor(s);
        const Color fill = markerColor.a > 0 ? markerColor : color;
        for (const ChartLineRun& run : runs[s]) {
            for (size_t k = 0; k < run.knots.size() && k < run.indices.size(); ++k) {
                const size_t i = run.indices[k];
                const Point2Dd& p = run.knots[k];
                const int64_t id = RegionId(s, i);
                const bool isHovered = (hovered == id);
                if (showMarkers || isHovered) {
                    DrawChartMarker(ctx, series[s].marker, p,
                                    isHovered ? markerRadius + 2.0 : markerRadius, fill,
                                    GetTheme().plotAreaColor, 1.5f);
                }
                const double raw = (i < series[s].values.size()) ? series[s].values[i]
                                                                 : std::nan("");
                std::ostringstream tip;
                tip << series[s].name << "\n"
                    << (categories.empty() ? domain.FormatValue(DomainValue(i))
                                           : categories[std::min(i, categories.size() - 1)])
                    << ": " << datum.FormatValue(raw);
                if (mode == EngineLineChartMode::PercentStackedArea &&
                    i < bands.top[s].size() && i < bands.base[s].size()) {
                    tip << " (" << value.FormatValue(bands.top[s][i] - bands.base[s][i]) << ")";
                }
                const double r = std::max(6.0, static_cast<double>(markerRadius) + 3.0);
                AddHitRegion(Rect2Dd(p.x - r, p.y - r, r * 2.0, r * 2.0), id, tip.str());
            }
        }
    }
}

void UltraCanvasEngineLineChart::MeasureContent(IRenderContext* ctx,
                                                ChartLayoutRequest& request) {
    // A label band past the value end, so the label of a point at the axis
    // maximum stays just outside the plot instead of being pushed onto it.
    if (!showValueLabels) return;
    ctx->SetFontSize(axisFontSize);
    if (GetProjectionKind() == ChartProjectionKind::Vertical) {
        request.Reserve(ChartAxisEdge::Top, ctx->GetTextLineHeight("Ag") + 8.0);
    } else if (GetProjectionKind() == ChartProjectionKind::Horizontal) {
        request.Reserve(ChartAxisEdge::Right, ctx->GetTextLineWidth("000.0K") + 10.0);
    }
}

void UltraCanvasEngineLineChart::CollectChartLabels(IRenderContext* ctx,
                                                    ChartLabelBroker& broker) {
    if (!Frame().axes || Frame().axes->Count() < 2 || !Frame().projection) return;
    const ChartAxis& value = Frame().axes->At(0);
    const ChartAxis& domain = Frame().axes->At(1);
    const ChartStackedValues bands = Bands();
    ctx->SetFontSize(axisFontSize);

    // The lines are obstacles: a label must not sit on its own series.
    for (size_t s = 0; s < series.size(); ++s) {
        if (s < hidden.size() && hidden[s]) continue;
        for (const ChartLineRun& run :
             BuildLineRuns(*Frame().projection, TopPoints(s, bands, value, domain, 1.0),
                           lineOptions)) {
            if (run.points.size() >= 2) broker.AddObstaclePolyline(run.points, lineWidth * 0.5 + 1.0);
        }
    }
    if (!showValueLabels) return;

    for (size_t s = 0; s < series.size(); ++s) {
        if (s < hidden.size() && hidden[s]) continue;
        // The series' extreme survives any declutter.
        size_t champion = SIZE_MAX;
        double best = -std::numeric_limits<double>::max();
        for (size_t i = 0; i < series[s].values.size(); ++i) {
            if (Finite(series[s].values[i]) && series[s].values[i] > best) {
                best = series[s].values[i];
                champion = i;
            }
        }
        const std::vector<ChartNormalizedPoint> tops = TopPoints(s, bands, value, domain, 1.0);
        for (size_t i = 0; i < tops.size(); ++i) {
            if (!Finite(tops[i].v) || i >= series[s].values.size()) continue;
            ChartLabelRequest r;
            const double shown = (mode == EngineLineChartMode::PercentStackedArea)
                                     ? bands.top[s][i] - bands.base[s][i]
                                     : series[s].values[i];
            r.text = FormatPlotted(value, shown);
            r.klass = ChartLabelClass::ValueLabel;
            r.anchor = Frame().projection->ToScreen(tops[i]);
            r.anchorRadius = markerRadius + 1.0;
            const Size2Di size = ctx->GetTextLineDimensions(r.text);
            r.textSize = Size2Dd(size.width, size.height);
            r.preferredSide = GetProjectionKind() == ChartProjectionKind::Horizontal
                                  ? LabelSide::Right : LabelSide::Top;
            r.allowSuppress = true;
            r.priority = (i == champion) ? 10 : 0;
            broker.Add(r);
        }
    }
}

void UltraCanvasEngineLineChart::OnChartThemeChanged() {
    RefreshLegend();                    // entries store their colour
}

void UltraCanvasEngineLineChart::OnLegendEntryToggled(size_t entryIndex, bool enabled) {
    if (entryIndex >= hidden.size()) return;
    hidden[entryIndex] = !enabled;
    RebuildSpanAnnotations();
    MarkEngineDirty(ChartDirty::Data);  // the range and the stack change
}

} // namespace UltraCanvas
