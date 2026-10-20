// Plugins/Charts/Engine/Charts/UltraCanvasEngineMekkoChart.cpp
// Mekko (Marimekko / mosaic) chart on the chart engine.
// Version: 1.0.0
// Last Modified: 2026-10-20
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineMekkoChart.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>

namespace UltraCanvas {

namespace {

Color ContrastText(const Color& background) {
    const double luminance = 0.299 * background.r + 0.587 * background.g + 0.114 * background.b;
    return luminance > 150.0 ? Color(20, 20, 20, 255) : Color(255, 255, 255, 255);
}

} // namespace

UltraCanvasEngineMekkoChart::UltraCanvasEngineMekkoChart(const std::string& id, int x, int y,
                                                         int w, int h)
    : UltraCanvasChartEngineElement(id, x, y, w, h) {
    enableTooltips = true;
    SetAnimateOnDataChange(true, 0.6f);
    SetLegendPosition(ChartLegendPosition::RightStart);
    SetShowLegend(true);
}

// =============================================================================
// DATA AND CONFIGURATION
// =============================================================================

void UltraCanvasEngineMekkoChart::SetData(const std::vector<ChartMekkoSeries>& s,
                                          const std::vector<ChartMekkoColumn>& c) {
    series = s;
    columns = c;
    seriesHidden.assign(series.size(), false);
    RefreshLegend();
    MarkEngineDirty(ChartDirty::Data);
}

size_t UltraCanvasEngineMekkoChart::AddSeries(const std::string& name, const Color& color) {
    series.emplace_back(name, color);
    seriesHidden.push_back(false);
    RefreshLegend();
    MarkEngineDirty(ChartDirty::Data);
    return series.size() - 1;
}

size_t UltraCanvasEngineMekkoChart::AddColumn(const std::string& label,
                                              const std::vector<double>& values) {
    columns.emplace_back(label, values);
    MarkEngineDirty(ChartDirty::Data);
    return columns.size() - 1;
}

void UltraCanvasEngineMekkoChart::ClearData() {
    series.clear();
    columns.clear();
    seriesHidden.clear();
    RefreshLegend();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineMekkoChart::SetMekkoMode(ChartMekkoMode m) {
    mode = m;
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineMekkoChart::SetColumnSort(ChartMekkoColumnSort s) {
    sort = s;
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineMekkoChart::SetSegmentLabelMode(ChartMekkoSegmentLabel labelMode) {
    segmentLabels = labelMode;
    RequestRedraw();                          // drawn inside the segments
}

void UltraCanvasEngineMekkoChart::SetColumnHeaderMode(ChartMekkoHeader headerMode) {
    header = headerMode;
    MarkEngineDirty(ChartDirty::Style);       // headers ride the label plan
}

void UltraCanvasEngineMekkoChart::SetColumnGap(double gapPx) {
    columnGap = std::max(0.0, gapPx);
    RequestRedraw();
}

void UltraCanvasEngineMekkoChart::SetSegmentBorder(const Color& color, float width) {
    segmentBorderColor = color;
    segmentBorderWidth = width;
    RequestRedraw();
}

void UltraCanvasEngineMekkoChart::SetSegmentCornerRadius(double radiusPx) {
    segmentCornerRadius = std::max(0.0, radiusPx);
    RequestRedraw();
}

void UltraCanvasEngineMekkoChart::SetShowColumnLabels(bool show) {
    showColumnLabels = show;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineMekkoChart::SetShowValueAxis(bool show) {
    showValueAxis = show;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineMekkoChart::SetShowCumulativeAxis(bool show) {
    showCumulativeAxis = show;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineMekkoChart::SetValueUnits(const std::string& prefix,
                                                const std::string& suffix) {
    valuePrefix = prefix;
    valueSuffix = suffix;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineMekkoChart::SetValueFormatter(ValueFormatter formatter) {
    valueFormatter = std::move(formatter);
    MarkEngineDirty(ChartDirty::Style);
}

std::pair<size_t, size_t> UltraCanvasEngineMekkoChart::GetHoveredSegment() const {
    const int64_t id = HoveredRegionId();
    if (id < 0 || static_cast<size_t>(id) >= segments.size()) return {SIZE_MAX, SIZE_MAX};
    return {segments[static_cast<size_t>(id)].column, segments[static_cast<size_t>(id)].series};
}

// =============================================================================
// HELPERS
// =============================================================================

Color UltraCanvasEngineMekkoChart::SeriesColor(size_t index) const {
    if (index < series.size() && series[index].color.a > 0) return series[index].color;
    return Palette().ColorAt(index, series.size());
}

double UltraCanvasEngineMekkoChart::ColumnTotal(size_t column) const {
    double total = 0.0;
    const std::vector<double>& values = columns[column].values;
    for (size_t s = 0; s < series.size() && s < values.size(); ++s) {
        if (s < seriesHidden.size() && seriesHidden[s]) continue;
        if (values[s] > 0.0) total += values[s];
    }
    return total;
}

std::string UltraCanvasEngineMekkoChart::FormatValue(double value) const {
    if (valueFormatter) return valueFormatter(value);
    std::ostringstream out;
    const bool whole = std::abs(value - std::round(value)) < 1e-9;
    out << std::fixed << std::setprecision(whole ? 0 : 1) << value;
    return valuePrefix + out.str() + valueSuffix;
}

std::string UltraCanvasEngineMekkoChart::FormatPercent(double percent) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(percent < 10.0 ? 1 : 0) << percent << "%";
    return out.str();
}

std::string UltraCanvasEngineMekkoChart::SegmentText(const Segment& segment) const {
    const std::string& name = series[segment.series].name;
    switch (segmentLabels) {
        case ChartMekkoSegmentLabel::Value:           return FormatValue(segment.value);
        case ChartMekkoSegmentLabel::Percent:         return FormatPercent(segment.percentOfColumn);
        case ChartMekkoSegmentLabel::ValueAndPercent:
            return FormatValue(segment.value) + " (" + FormatPercent(segment.percentOfColumn) + ")";
        case ChartMekkoSegmentLabel::SeriesName:      return name;
        case ChartMekkoSegmentLabel::SeriesAndValue:  return name + ": " + FormatValue(segment.value);
        case ChartMekkoSegmentLabel::NoLabels:
        default:                                      return std::string();
    }
}

std::string UltraCanvasEngineMekkoChart::HeaderText(const ColumnSpan& span) const {
    const double percent = grandTotal > 0.0 ? span.total / grandTotal * 100.0 : 0.0;
    switch (header) {
        case ChartMekkoHeader::WidthPercent:    return FormatPercent(percent);
        case ChartMekkoHeader::Total:           return FormatValue(span.total);
        case ChartMekkoHeader::TotalAndPercent:
            return FormatValue(span.total) + " (" + FormatPercent(percent) + ")";
        case ChartMekkoHeader::NoHeader:
        default:                                return std::string();
    }
}

double UltraCanvasEngineMekkoChart::GapInDomainUnits(const IChartProjection& projection) const {
    // The screen length of the whole domain, whichever way it runs.
    const Point2Dd a = projection.ToScreen(ChartNormalizedPoint(0.0, 0.0));
    const Point2Dd b = projection.ToScreen(ChartNormalizedPoint(1.0, 0.0));
    const double length = std::hypot(b.x - a.x, b.y - a.y);
    return length > 1.0 ? columnGap / length : 0.0;
}

void UltraCanvasEngineMekkoChart::BuildMosaic() {
    spans.clear();
    segments.clear();
    grandTotal = 0.0;
    std::vector<size_t> order(columns.size());
    std::iota(order.begin(), order.end(), 0);
    switch (sort) {
        case ChartMekkoColumnSort::TotalDescending:
            std::stable_sort(order.begin(), order.end(),
                             [this](size_t a, size_t b) { return ColumnTotal(a) > ColumnTotal(b); });
            break;
        case ChartMekkoColumnSort::TotalAscending:
            std::stable_sort(order.begin(), order.end(),
                             [this](size_t a, size_t b) { return ColumnTotal(a) < ColumnTotal(b); });
            break;
        case ChartMekkoColumnSort::Alphabetical:
            std::stable_sort(order.begin(), order.end(), [this](size_t a, size_t b) {
                return columns[a].label < columns[b].label;
            });
            break;
        case ChartMekkoColumnSort::DataOrder:
        default:
            break;
    }
    for (size_t c : order) grandTotal += ColumnTotal(c);
    if (grandTotal <= 0.0) return;

    double cumulative = 0.0;
    for (size_t c : order) {
        const double total = ColumnTotal(c);
        ColumnSpan span;
        span.column = c;
        span.total = total;
        span.u0 = cumulative / grandTotal * 100.0;
        cumulative += total;
        span.u1 = cumulative / grandTotal * 100.0;
        spans.push_back(span);
        if (total <= 0.0) continue;

        // Stack top-down, so the first series sits on top as in the legend.
        const double height = (mode == ChartMekkoMode::Marimekko) ? 100.0 : total;
        double top = height;
        const std::vector<double>& values = columns[c].values;
        for (size_t s = 0; s < series.size() && s < values.size(); ++s) {
            if (seriesHidden[s] || values[s] <= 0.0) continue;
            const double share = values[s] / total;
            Segment segment;
            segment.column = c;
            segment.series = s;
            segment.u0 = span.u0;
            segment.u1 = span.u1;
            segment.v1 = top;
            segment.v0 = top - share * height;
            top = segment.v0;
            segment.value = values[s];
            segment.percentOfColumn = share * 100.0;
            segments.push_back(segment);
        }
    }
}

void UltraCanvasEngineMekkoChart::RefreshLegend() {
    std::vector<ChartLegendEntry> entries;
    for (size_t s = 0; s < series.size(); ++s) {
        ChartLegendEntry entry(series[s].name, SeriesColor(s));
        entry.enabled = !(s < seriesHidden.size() && seriesHidden[s]);
        entries.push_back(entry);
    }
    SetLegendEntries(entries);
}

void UltraCanvasEngineMekkoChart::OnThemeChanged() {
    RefreshLegend();
}

void UltraCanvasEngineMekkoChart::OnLegendEntryToggled(size_t entryIndex, bool enabled) {
    if (entryIndex >= seriesHidden.size()) return;
    seriesHidden[entryIndex] = !enabled;
    // The column totals - and so every width - re-share without the series.
    MarkEngineDirty(ChartDirty::Data);
}

bool UltraCanvasEngineMekkoChart::OnEvent(const UCEvent& event) {
    const bool handled = UltraCanvasChartEngineElement::OnEvent(event);
    if (!handled && event.type == UCEventType::MouseDown &&
        event.button == UCMouseButton::Left && onSegmentClick) {
        const std::pair<size_t, size_t> hit = GetHoveredSegment();
        if (hit.first != SIZE_MAX) {
            onSegmentClick(hit.first, hit.second);
            return true;
        }
    }
    return handled;
}

// =============================================================================
// ENGINE CONTRACT
// =============================================================================

void UltraCanvasEngineMekkoChart::DescribeAxes(ChartAxisSet& axes) {
    BuildMosaic();

    ChartAxis value("value");
    value.side = ChartAxisSide::Left;
    value.visible = showValueAxis;
    if (mode == ChartMekkoMode::Marimekko) {
        value.SetRange(0.0, 100.0);
        value.unitSuffix = "%";
        value.decimals = 0;
    } else {
        value.Observe(0.0);
        for (const ColumnSpan& span : spans) value.Observe(span.total);
        value.unitPrefix = valuePrefix;
        value.unitSuffix = valueSuffix;
        if (valueFormatter) value.formatter = valueFormatter;
    }
    axes.Add(value);                          // kValueAxis

    // Cumulative share at every column boundary, right under the columns.
    ChartAxis cumulative("cumulative");
    cumulative.side = ChartAxisSide::Bottom;
    cumulative.SetRange(0.0, 100.0);
    cumulative.unitSuffix = "%";
    cumulative.decimals = 0;
    cumulative.visible = showCumulativeAxis;
    cumulative.tickValues.push_back(0.0);
    for (const ColumnSpan& span : spans) cumulative.tickValues.push_back(span.u1);
    axes.Add(cumulative);                     // kCumulativeAxis

    // Column names under each column's centre. Same side as the cumulative
    // axis: the engine stacks it outward, below the percent ticks.
    ChartAxis names("columns");
    names.side = ChartAxisSide::Bottom;
    names.SetRange(0.0, 100.0);
    names.visible = showColumnLabels;
    std::vector<std::pair<double, std::string>> centres;
    for (const ColumnSpan& span : spans) {
        const double centre = (span.u0 + span.u1) * 0.5;
        names.tickValues.push_back(centre);
        centres.emplace_back(centre, columns[span.column].label);
    }
    names.formatter = [centres](double at) {
        for (const auto& [centre, label] : centres) {
            if (std::abs(centre - at) < 1e-9) return label;
        }
        return std::string();
    };
    axes.Add(names);                          // kColumnAxis

    SetGridAxis(kValueAxis);
}

void UltraCanvasEngineMekkoChart::MeasureContent(IRenderContext* ctx, ChartLayoutRequest& request) {
    if (header == ChartMekkoHeader::NoHeader) return;
    ctx->SetFontSize(axisFontSize);
    const double band = ctx->GetTextLineHeight("Ag") + 8.0;
    // The headers sit past the value end of the columns.
    request.Reserve(GetProjectionKind() == ChartProjectionKind::Horizontal
                        ? ChartAxisEdge::Right : ChartAxisEdge::Top,
                    GetProjectionKind() == ChartProjectionKind::Horizontal ? band * 4.0 : band);
}

void UltraCanvasEngineMekkoChart::CollectChartLabels(IRenderContext* ctx, ChartLabelBroker& broker) {
    if (header == ChartMekkoHeader::NoHeader || !Frame().projection || !Frame().axes ||
        Frame().axes->Count() <= kColumnAxis) return;
    const IChartProjection& projection = *Frame().projection;
    const ChartAxis& value = Frame().axes->At(kValueAxis);
    const ChartAxis& domain = Frame().axes->At(kColumnAxis);
    ctx->SetFontSize(axisFontSize);

    for (const ColumnSpan& span : spans) {
        if (span.total <= 0.0) continue;
        const double u0 = domain.Normalize(span.u0), u1 = domain.Normalize(span.u1);
        const double top = value.Normalize(mode == ChartMekkoMode::Marimekko ? 100.0 : span.total);
        const Point2Dd a = projection.ToScreen(ChartNormalizedPoint(u0, top));
        const Point2Dd b = projection.ToScreen(ChartNormalizedPoint(u1, top));
        const double room = std::hypot(b.x - a.x, b.y - a.y) + columnGap + 12.0;

        std::string text = HeaderText(span);
        Size2Di size = ctx->GetTextLineDimensions(text);
        if (size.width > room && header == ChartMekkoHeader::TotalAndPercent) {
            // Progressively shorter forms on narrow columns.
            text = FormatValue(span.total);
            size = ctx->GetTextLineDimensions(text);
            if (size.width > room) {
                text = FormatPercent(grandTotal > 0.0 ? span.total / grandTotal * 100.0 : 0.0);
                size = ctx->GetTextLineDimensions(text);
            }
        }

        ChartLabelRequest r;
        r.text = text;
        r.klass = ChartLabelClass::ValueLabel;
        r.anchor = projection.ToScreen(ChartNormalizedPoint((u0 + u1) * 0.5, top));
        r.textSize = Size2Dd(size.width, size.height);
        r.preferredSide = GetProjectionKind() == ChartProjectionKind::Horizontal ? LabelSide::Right
                                                                                 : LabelSide::Top;
        r.allowSuppress = true;
        r.priority = static_cast<int>(span.total / std::max(grandTotal, 1e-12) * 10.0);
        broker.Add(r);
    }
}

void UltraCanvasEngineMekkoChart::RenderChartContent(IRenderContext* ctx,
                                                     const ChartEngineFrame& frame) {
    if (segments.empty() || !frame.axes || frame.axes->Count() <= kColumnAxis) return;
    const IChartProjection& projection = *frame.projection;
    const ChartAxis& value = frame.axes->At(kValueAxis);
    const ChartAxis& domain = frame.axes->At(kColumnAxis);
    const double halfGap = GapInDomainUnits(projection) * 0.5;
    const double base = value.Normalize(0.0);
    const double progress = frame.animationProgress;
    const int64_t hovered = HoveredRegionId();
    const size_t legendHover = GetLegend().GetHighlightedEntry();

    ctx->SetFontSize(axisFontSize - 1.0f);
    for (size_t i = 0; i < segments.size(); ++i) {
        const Segment& segment = segments[i];
        double u0 = domain.Normalize(segment.u0) + halfGap;
        double u1 = domain.Normalize(segment.u1) - halfGap;
        if (u1 <= u0) { u0 = domain.Normalize(segment.u0); u1 = domain.Normalize(segment.u1); }
        // Entrance: every column grows up from the baseline.
        const double v0 = base + (value.Normalize(segment.v0) - base) * progress;
        const double v1 = base + (value.Normalize(segment.v1) - base) * progress;

        const std::vector<Point2Dd> outline =
            BuildBarOutline(projection, u0, v0, u1, v1, 2, segmentCornerRadius);
        if (outline.size() < 3) continue;

        Color fill = SeriesColor(segment.series);
        const bool dimmed = legendHover != SIZE_MAX && legendHover != segment.series;
        if (dimmed) fill = fill.WithAlpha(70);
        if (hovered == static_cast<int64_t>(i)) fill = fill.Lighten(0.2f);
        ctx->SetFillPaint(fill);
        ctx->FillLinePath(outline);
        if (segmentBorderWidth > 0.0f && segmentBorderColor.a > 0) {
            ctx->SetStrokePaint(segmentBorderColor);
            ctx->SetStrokeWidth(segmentBorderWidth);
            ctx->DrawLinePath(outline, true);
        }
        if (hovered == static_cast<int64_t>(i)) {
            ctx->SetStrokePaint(GetTheme().axisLineColor);
            ctx->SetStrokeWidth(2.0f);
            ctx->DrawLinePath(outline, true);
        }

        // Segment labels sit inside their segment - nothing to solve, but a
        // label that does not fit falls back to the value, then to nothing.
        if (segmentLabels != ChartMekkoSegmentLabel::NoLabels && progress >= 1.0) {
            const Point2Dd a = projection.ToScreen(ChartNormalizedPoint(u0, v0));
            const Point2Dd b = projection.ToScreen(ChartNormalizedPoint(u1, v1));
            const double w = std::abs(b.x - a.x), h = std::abs(b.y - a.y);
            std::string text = SegmentText(segment);
            Size2Di size = ctx->GetTextLineDimensions(text);
            if (size.width > w - 4.0 &&
                (segmentLabels == ChartMekkoSegmentLabel::ValueAndPercent ||
                 segmentLabels == ChartMekkoSegmentLabel::SeriesAndValue)) {
                text = FormatValue(segment.value);
                size = ctx->GetTextLineDimensions(text);
            }
            if (h >= 14.0 && size.width <= w - 4.0 && size.height <= h) {
                Color textColor = ContrastText(fill);
                if (dimmed) textColor = textColor.WithAlpha(110);
                ctx->SetTextPaint(textColor);
                ctx->DrawText(text, Point2Dd((a.x + b.x - size.width) * 0.5,
                                             (a.y + b.y - size.height) * 0.5));
            }
        }

        std::ostringstream tip;
        tip << columns[segment.column].label << " / " << series[segment.series].name << "\n"
            << FormatValue(segment.value) << " (" << FormatPercent(segment.percentOfColumn)
            << " of column)";
        AddHitRegion(outline, static_cast<int64_t>(i), tip.str());
    }
}

} // namespace UltraCanvas
