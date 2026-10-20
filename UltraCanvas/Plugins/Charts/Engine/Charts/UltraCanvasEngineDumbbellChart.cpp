// Plugins/Charts/Engine/Charts/UltraCanvasEngineDumbbellChart.cpp
// Dumbbell (connected dot) chart on the chart engine
// Version: 1.0.0
// Last Modified: 2026-10-20
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineDumbbellChart.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace UltraCanvas {

namespace {

constexpr int64_t kRowStride = 64;
constexpr int64_t kConnectorSlot = 63;   // the connector's region inside a row

int64_t DotRegionId(size_t row, size_t group) {
    return static_cast<int64_t>(row) * kRowStride + static_cast<int64_t>(group);
}

int64_t ConnectorRegionId(size_t row) {
    return static_cast<int64_t>(row) * kRowStride + kConnectorSlot;
}

size_t RowOfRegion(int64_t id) {
    return id < 0 ? SIZE_MAX : static_cast<size_t>(id / kRowStride);
}

} // namespace

UltraCanvasEngineDumbbellChart::UltraCanvasEngineDumbbellChart(const std::string& id,
                                                               int x, int y, int w, int h)
    : UltraCanvasChartEngineElement(id, x, y, w, h) {
    SetProjectionKind(ChartProjectionKind::Horizontal);
    SetAnimateOnDataChange(true, 0.6f);
    SetLegendPosition(ChartLegendPosition::TopCenter);
}

// =============================================================================
// DATA
// =============================================================================

void UltraCanvasEngineDumbbellChart::SetGroups(const std::vector<DumbbellGroup>& newGroups) {
    groups = newGroups;
    groupEnabled.assign(groups.size(), true);
    RefreshLegend();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineDumbbellChart::SetRows(const std::vector<DumbbellRow>& newRows) {
    rows = newRows;
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineDumbbellChart::AddRow(const std::string& label,
                                            const std::vector<double>& values) {
    rows.emplace_back(label, values);
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineDumbbellChart::AddRow(const std::string& label, double first,
                                            double second) {
    AddRow(label, std::vector<double>{first, second});
}

void UltraCanvasEngineDumbbellChart::ClearRows() {
    rows.clear();
    MarkEngineDirty(ChartDirty::Data);
}

// =============================================================================
// PRESENTATION
// =============================================================================

void UltraCanvasEngineDumbbellChart::SetSortOrder(DumbbellRowSort order) {
    sortOrder = order;
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineDumbbellChart::SetDotRadius(float radius) {
    dotRadius = std::clamp(radius, 1.0f, 30.0f);
    MarkEngineDirty(ChartDirty::Style);     // labels clear the dots
}

void UltraCanvasEngineDumbbellChart::SetConnectorWidth(float width) {
    connectorWidth = std::max(0.5f, width);
    RequestRedraw();
}

void UltraCanvasEngineDumbbellChart::SetConnectorColor(const Color& color) {
    connectorColor = color;
    RequestRedraw();
}

void UltraCanvasEngineDumbbellChart::SetValueRange(double minValue, double maxValue) {
    manualRange = maxValue > minValue;
    rangeMin = minValue;
    rangeMax = maxValue;
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineDumbbellChart::ClearValueRange() {
    manualRange = false;
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineDumbbellChart::SetIncludeZero(bool include) {
    includeZero = include;
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineDumbbellChart::SetValueFormat(const std::string& prefix,
                                                    const std::string& suffix,
                                                    bool compact, int decimals) {
    valuePrefix = prefix;
    valueSuffix = suffix;
    compactValues = compact;
    valueDecimals = decimals;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineDumbbellChart::SetValueAxisTitle(const std::string& title) {
    valueAxisTitle = title;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineDumbbellChart::SetShowValueLabels(bool show) {
    showValues = show;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineDumbbellChart::SetShowDifferenceLabels(bool show) {
    showDifferences = show;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineDumbbellChart::SetShowRowHighlight(bool show) {
    showRowHighlight = show;
    RequestRedraw();
}

void UltraCanvasEngineDumbbellChart::OnThemeChanged() {
    RefreshLegend();
}

void UltraCanvasEngineDumbbellChart::OnLegendEntryToggled(size_t entryIndex, bool enabled) {
    if (entryIndex >= groupEnabled.size()) return;
    groupEnabled[entryIndex] = enabled;
    MarkEngineDirty(ChartDirty::Data);      // ranges, connectors and labels change
}

// =============================================================================
// HELPERS
// =============================================================================

Color UltraCanvasEngineDumbbellChart::GroupColor(size_t group) const {
    if (group < groups.size() && groups[group].color.a > 0) return groups[group].color;
    return Palette().ColorAt(group, std::max<size_t>(groups.size(), 2));
}

bool UltraCanvasEngineDumbbellChart::GroupActive(size_t group) const {
    return group < groupEnabled.size() ? groupEnabled[group] : true;
}

bool UltraCanvasEngineDumbbellChart::RowExtent(const DumbbellRow& row, double& lo,
                                               double& hi) const {
    bool any = false;
    for (size_t g = 0; g < row.values.size(); ++g) {
        const double v = row.values[g];
        if (!GroupActive(g) || !std::isfinite(v)) continue;
        lo = any ? std::min(lo, v) : v;
        hi = any ? std::max(hi, v) : v;
        any = true;
    }
    return any;
}

void UltraCanvasEngineDumbbellChart::RefreshLegend() {
    std::vector<ChartLegendEntry> entries;
    for (size_t g = 0; g < groups.size(); ++g) {
        ChartLegendEntry e(groups[g].name, GroupColor(g),
                           groups[g].marker == ChartMarkerShape::Circle ? LegendSwatch::Circle
                                                                        : LegendSwatch::Marker);
        e.enabled = GroupActive(g);
        entries.push_back(e);
    }
    SetLegendEntries(entries);
    SetShowLegend(!entries.empty());
}

std::vector<size_t> UltraCanvasEngineDumbbellChart::DisplayOrder() const {
    std::vector<size_t> order(rows.size());
    std::iota(order.begin(), order.end(), 0);
    if (sortOrder == DumbbellRowSort::InsertionOrder) return order;

    auto key = [this](size_t r) {
        const DumbbellRow& row = rows[r];
        double lo = 0.0, hi = 0.0;
        const bool any = RowExtent(row, lo, hi);
        switch (sortOrder) {
            case DumbbellRowSort::FirstAscending:
            case DumbbellRowSort::FirstDescending:
                return row.values.empty() ? 0.0 : row.values.front();
            case DumbbellRowSort::LastAscending:
            case DumbbellRowSort::LastDescending:
                return row.values.empty() ? 0.0 : row.values.back();
            case DumbbellRowSort::RangeAscending:
            case DumbbellRowSort::RangeDescending:
                return any ? hi - lo : 0.0;
            default:
                return 0.0;
        }
    };
    const bool byLabel = sortOrder == DumbbellRowSort::LabelAscending ||
                         sortOrder == DumbbellRowSort::LabelDescending;
    const bool descending = sortOrder == DumbbellRowSort::LabelDescending ||
                            sortOrder == DumbbellRowSort::FirstDescending ||
                            sortOrder == DumbbellRowSort::LastDescending ||
                            sortOrder == DumbbellRowSort::RangeDescending;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        if (byLabel) {
            return descending ? rows[a].label > rows[b].label : rows[a].label < rows[b].label;
        }
        return descending ? key(a) > key(b) : key(a) < key(b);
    });
    return order;
}

std::string UltraCanvasEngineDumbbellChart::FormatValue(double value) const {
    if (Axes().Count() > 0) return Axes().At(0).FormatValue(value);
    ChartAxis format("value");
    format.unitPrefix = valuePrefix;
    format.unitSuffix = valueSuffix;
    format.compactNumbers = compactValues;
    format.decimals = valueDecimals;
    return format.FormatValue(value);
}

std::string UltraCanvasEngineDumbbellChart::TooltipFor(size_t row, size_t group) const {
    const DumbbellRow& r = rows[row];
    std::string text = r.label;
    for (size_t g = 0; g < r.values.size() && g < groups.size(); ++g) {
        if (!GroupActive(g) || !std::isfinite(r.values[g])) continue;
        text += "\n" + std::string(g == group ? "> " : "  ") + groups[g].name + ": " +
                FormatValue(r.values[g]);
    }
    double lo = 0.0, hi = 0.0;
    if (RowExtent(r, lo, hi)) {
        if (r.values.size() == 2 && std::isfinite(r.values[0]) && std::isfinite(r.values[1]) &&
            GroupActive(0) && GroupActive(1)) {
            const double delta = r.values[1] - r.values[0];
            text += "\nDifference: " + std::string(delta >= 0.0 ? "+" : "-") +
                    FormatValue(std::abs(delta));
        } else {
            text += "\nRange: " + FormatValue(hi - lo);
        }
    }
    return text;
}

// =============================================================================
// ENGINE CONTRACT
// =============================================================================

void UltraCanvasEngineDumbbellChart::DescribeAxes(ChartAxisSet& axes) {
    const bool horizontal = GetProjectionKind() == ChartProjectionKind::Horizontal;

    ChartAxis value("value");
    value.side = horizontal ? ChartAxisSide::Bottom : ChartAxisSide::Left;
    value.unitPrefix = valuePrefix;
    value.unitSuffix = valueSuffix;
    value.compactNumbers = compactValues;
    value.decimals = valueDecimals;
    value.title = valueAxisTitle;
    if (manualRange) {
        value.SetRange(rangeMin, rangeMax);
    } else {
        if (includeZero) value.Observe(0.0);
        for (const DumbbellRow& row : rows) {
            for (size_t g = 0; g < row.values.size(); ++g) {
                if (GroupActive(g)) value.Observe(row.values[g]);
            }
        }
    }
    axes.Add(value);

    ChartAxis rowAxis("rows");
    rowAxis.scale = ChartScale::Category;
    rowAxis.side = horizontal ? ChartAxisSide::Left : ChartAxisSide::Bottom;
    for (size_t r : DisplayOrder()) rowAxis.categories.push_back(rows[r].label);
    rowAxis.categoryPadding = 0.5;
    axes.Add(rowAxis);

    SetGridAxis(0);
}

void UltraCanvasEngineDumbbellChart::RenderChartContent(IRenderContext* ctx,
                                                        const ChartEngineFrame& frame) {
    if (rows.empty() || !frame.axes || frame.axes->Count() < 2) return;
    const ChartAxis& value = frame.axes->At(0);
    const ChartAxis& rowAxis = frame.axes->At(1);
    const IChartProjection& projection = *frame.projection;
    const double progress = frame.animationProgress;
    const std::vector<size_t> order = DisplayOrder();
    const double slot = std::abs(rowAxis.Normalize(1.0) - rowAxis.Normalize(0.0));
    const size_t hoveredRow = RowOfRegion(HoveredRegionId());

    for (size_t s = 0; s < order.size(); ++s) {
        const size_t r = order[s];
        const DumbbellRow& row = rows[r];
        const double u = rowAxis.Normalize(static_cast<double>(s));
        double lo = 0.0, hi = 0.0;
        if (!RowExtent(row, lo, hi)) continue;

        // The hovered row's band, under its marks.
        if (showRowHighlight && hoveredRow == r) {
            const std::vector<Point2Dd> band =
                BuildBarOutline(projection, u - slot * 0.5, 0.0, u + slot * 0.5, 1.0, 1, 0.0);
            ctx->SetFillPaint(GetTheme().gridColor.WithAlpha(110));
            ctx->FillLinePath(band);
        }

        // Dots slide outward from the row's middle as the animation runs.
        const double middle = (lo + hi) * 0.5;
        auto animated = [&](double v) { return value.Normalize(middle + (v - middle) * progress); };

        const Point2Dd a = projection.ToScreen(ChartNormalizedPoint(u, animated(lo)));
        const Point2Dd b = projection.ToScreen(ChartNormalizedPoint(u, animated(hi)));
        ctx->PushState();
        ctx->SetLineCap(LineCap::Round);
        ctx->SetStrokePaint(row.connectorColor.a > 0 ? row.connectorColor : connectorColor);
        ctx->SetStrokeWidth(hoveredRow == r ? connectorWidth + 1.5f : connectorWidth);
        ctx->DrawLine(a, b);
        ctx->PopState();

        // The connector is hoverable too; the dots, added after it, win.
        const double pad = std::max<double>(dotRadius, connectorWidth) * 0.5 + 2.0;
        AddHitRegion(Rect2Dd(std::min(a.x, b.x) - pad, std::min(a.y, b.y) - pad,
                             std::abs(b.x - a.x) + pad * 2.0, std::abs(b.y - a.y) + pad * 2.0),
                     ConnectorRegionId(r), TooltipFor(r, SIZE_MAX));

        for (size_t g = 0; g < row.values.size(); ++g) {
            const double v = row.values[g];
            if (!GroupActive(g) || !std::isfinite(v)) continue;
            const Point2Dd p = projection.ToScreen(ChartNormalizedPoint(u, animated(v)));
            const int64_t id = DotRegionId(r, g);
            const bool hovered = HoveredRegionId() == id;
            const double radius = hovered ? dotRadius * 1.25 : dotRadius;
            const ChartMarkerShape marker = g < groups.size() ? groups[g].marker
                                                              : ChartMarkerShape::Circle;
            DrawChartMarker(ctx, marker, p, radius, GroupColor(g), GetTheme().plotAreaColor, 1.5f);
            AddHitRegion(Rect2Dd(p.x - radius, p.y - radius, radius * 2.0, radius * 2.0), id,
                         TooltipFor(r, g));
        }
    }
}

void UltraCanvasEngineDumbbellChart::MeasureContent(IRenderContext* ctx,
                                                    ChartLayoutRequest& request) {
    if (!showValues) return;
    // Room past both ends of the value range for the outer dots' labels.
    ctx->SetFontSize(axisFontSize);
    double widest = 0.0;
    for (const DumbbellRow& row : rows) {
        for (double v : row.values) {
            if (std::isfinite(v)) {
                widest = std::max(widest, static_cast<double>(ctx->GetTextLineWidth(FormatValue(v))));
            }
        }
    }
    if (GetProjectionKind() == ChartProjectionKind::Horizontal) {
        request.Reserve(ChartAxisEdge::Right, widest + dotRadius + 8.0);
    } else {
        request.Reserve(ChartAxisEdge::Top, ctx->GetTextLineHeight("Ag") + dotRadius + 6.0);
    }
}

void UltraCanvasEngineDumbbellChart::CollectChartLabels(IRenderContext* ctx,
                                                        ChartLabelBroker& broker) {
    if ((!showValues && !showDifferences) || rows.empty() || !Frame().axes ||
        Frame().axes->Count() < 2 || !Frame().projection) return;
    const ChartAxis& value = Frame().axes->At(0);
    const ChartAxis& rowAxis = Frame().axes->At(1);
    const IChartProjection& projection = *Frame().projection;
    const bool horizontal = GetProjectionKind() == ChartProjectionKind::Horizontal;
    const std::vector<size_t> order = DisplayOrder();
    ctx->SetFontSize(axisFontSize);

    for (size_t s = 0; s < order.size(); ++s) {
        const DumbbellRow& row = rows[order[s]];
        const double u = rowAxis.Normalize(static_cast<double>(s));
        double lo = 0.0, hi = 0.0;
        if (!RowExtent(row, lo, hi)) continue;

        if (showValues) {
            for (size_t g = 0; g < row.values.size(); ++g) {
                const double v = row.values[g];
                if (!GroupActive(g) || !std::isfinite(v)) continue;
                ChartLabelRequest r;
                r.text = FormatValue(v);
                r.klass = ChartLabelClass::ValueLabel;
                r.anchor = projection.ToScreen(ChartNormalizedPoint(u, value.Normalize(v)));
                r.anchorRadius = dotRadius + 2.0;
                const Size2Di size = ctx->GetTextLineDimensions(r.text);
                r.textSize = Size2Dd(size.width, size.height);
                // The low dot labels outward on the low side, every other dot
                // on the high side - never across the connector.
                const bool low = (v == lo && hi > lo);
                r.preferredSide = horizontal ? (low ? LabelSide::Left : LabelSide::Right)
                                             : (low ? LabelSide::Bottom : LabelSide::Top);
                r.allowSuppress = true;
                broker.Add(r);
            }
        }
        if (showDifferences && hi > lo) {
            ChartLabelRequest r;
            r.text = "+" + FormatValue(hi - lo);
            r.klass = ChartLabelClass::Annotation;
            r.anchor = projection.ToScreen(ChartNormalizedPoint(u, value.Normalize((lo + hi) * 0.5)));
            r.anchorRadius = connectorWidth;
            const Size2Di size = ctx->GetTextLineDimensions(r.text);
            r.textSize = Size2Dd(size.width, size.height);
            r.preferredSide = horizontal ? LabelSide::Top : LabelSide::Right;
            r.allowSuppress = true;
            broker.Add(r);
        }
    }
}

} // namespace UltraCanvas
