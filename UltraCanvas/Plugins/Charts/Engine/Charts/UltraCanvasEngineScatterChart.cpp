// Plugins/Charts/Engine/Charts/UltraCanvasEngineScatterChart.cpp
// Scatter and bubble charts on the chart engine.
// Version: 1.0.0
// Last Modified: 2026-10-20
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineScatterChart.h"
#include "Plugins/Charts/Engine/UltraCanvasChartAnalysis.h"
#include <algorithm>
#include <limits>
#include <numeric>
#include <sstream>

namespace UltraCanvas {

namespace {

bool Finite(double v) { return std::isfinite(v); }

// Dark text on a light mark, light text on a dark one.
Color ContrastText(const Color& background) {
    const double luminance = 0.299 * background.r + 0.587 * background.g + 0.114 * background.b;
    return luminance > 150.0 ? Color(40, 40, 40, 255) : Color(255, 255, 255, 255);
}

LegendSwatch SwatchFor(ChartMarkerShape shape) {
    switch (shape) {
        case ChartMarkerShape::Circle: return LegendSwatch::Circle;
        case ChartMarkerShape::Square: return LegendSwatch::Square;
        default:                       return LegendSwatch::Marker;
    }
}

} // namespace

UltraCanvasEngineScatterChart::UltraCanvasEngineScatterChart(const std::string& id, int x,
                                                             int y, int w, int h)
    : UltraCanvasChartEngineElement(id, x, y, w, h) {
    SetAnimateOnDataChange(true, 0.7f);    // marks grow in on every data change
    SetLegendPosition(ChartLegendPosition::RightStart);
}

// =============================================================================
// DATA
// =============================================================================

void UltraCanvasEngineScatterChart::DataChanged() {
    RefreshLegend();
    RebuildHighlights();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineScatterChart::SetGroups(const std::vector<EngineScatterGroup>& list) {
    groups = list;
    hidden.assign(groups.size(), false);
    DataChanged();
}

void UltraCanvasEngineScatterChart::AddGroup(const EngineScatterGroup& group) {
    groups.push_back(group);
    hidden.push_back(false);
    DataChanged();
}

void UltraCanvasEngineScatterChart::AddPoint(size_t groupIndex, const EngineScatterPoint& point) {
    while (groups.size() <= groupIndex) {
        groups.emplace_back();
        hidden.push_back(false);
    }
    groups[groupIndex].points.push_back(point);
    DataChanged();
}

void UltraCanvasEngineScatterChart::ClearGroups() {
    groups.clear();
    hidden.clear();
    DataChanged();
}

// =============================================================================
// AXES
// =============================================================================

void UltraCanvasEngineScatterChart::SetXCategories(const std::vector<std::string>& names) {
    xFormat.categories = names;
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineScatterChart::SetYCategories(const std::vector<std::string>& names) {
    yFormat.categories = names;
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineScatterChart::SetXRange(double lo, double hi) {
    xFormat.explicitRange = true;
    xFormat.lo = lo;
    xFormat.hi = hi;
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineScatterChart::SetYRange(double lo, double hi) {
    yFormat.explicitRange = true;
    yFormat.lo = lo;
    yFormat.hi = hi;
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineScatterChart::SetAutoRanges() {
    xFormat.explicitRange = false;
    yFormat.explicitRange = false;
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineScatterChart::SetAxisTitles(const std::string& xTitle,
                                                  const std::string& yTitle) {
    xFormat.title = xTitle;
    yFormat.title = yTitle;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineScatterChart::SetXFormat(const std::string& prefix,
                                               const std::string& suffix, bool compact,
                                               int decimals) {
    xFormat.prefix = prefix;
    xFormat.suffix = suffix;
    xFormat.compact = compact;
    xFormat.decimals = decimals;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineScatterChart::SetYFormat(const std::string& prefix,
                                               const std::string& suffix, bool compact,
                                               int decimals) {
    yFormat.prefix = prefix;
    yFormat.suffix = suffix;
    yFormat.compact = compact;
    yFormat.decimals = decimals;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineScatterChart::SetXFormatter(ValueFormatter formatter) {
    xFormat.formatter = std::move(formatter);
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineScatterChart::SetYFormatter(ValueFormatter formatter) {
    yFormat.formatter = std::move(formatter);
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineScatterChart::SetShowXGrid(bool show) {
    showXGrid = show;
    MarkEngineDirty(ChartDirty::Style);
}

// =============================================================================
// MARKS AND CHANNELS
// =============================================================================

void UltraCanvasEngineScatterChart::SetMarkerRadius(float radius) {
    markerRadius = std::max(1.0f, radius);
    MarkEngineDirty(ChartDirty::Style);     // label clearances follow the marks
}

void UltraCanvasEngineScatterChart::SetMarkerOpacity(float opacity) {
    markerOpacity = std::clamp(opacity, 0.05f, 1.0f);
    RequestRedraw();
}

void UltraCanvasEngineScatterChart::SetMarkerOutline(const Color& color, float width) {
    outlineColor = color;
    outlineWidth = std::max(0.0f, width);
    RequestRedraw();
}

void UltraCanvasEngineScatterChart::SetRadiusRange(float minR, float maxR) {
    minRadius = std::max(1.0f, std::min(minR, maxR));
    maxRadius = std::max(minRadius, std::max(minR, maxR));
    RefreshLegend();
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineScatterChart::SetSizeScale(EngineBubbleScale scale) {
    sizeScale = scale;
    RefreshLegend();
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineScatterChart::SetSizeDomain(double minValue, double maxValue) {
    autoSizeDomain = false;
    sizeMin = std::min(minValue, maxValue);
    sizeMax = std::max(minValue, maxValue);
    RefreshLegend();
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineScatterChart::SetAutoSizeDomain() {
    autoSizeDomain = true;
    RefreshLegend();
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineScatterChart::SetShowSizeLegend(bool show, const std::string& title,
                                                      const std::vector<double>& values) {
    showSizeLegend = show;
    sizeLegendTitle = title;
    sizeLegendValues = values;
    RefreshLegend();
}

void UltraCanvasEngineScatterChart::SetColormap(HeatmapColormap map, bool reverse) {
    colormap = map;
    colormapReversed = reverse;
    RefreshLegend();
    RequestRedraw();
}

void UltraCanvasEngineScatterChart::SetCustomColormap(const std::vector<Color>& anchors) {
    customColormap = anchors;
    colormap = HeatmapColormap::Custom;
    RefreshLegend();
    RequestRedraw();
}

void UltraCanvasEngineScatterChart::SetColorDomain(double minValue, double maxValue) {
    autoColorDomain = false;
    colorMin = std::min(minValue, maxValue);
    colorMax = std::max(minValue, maxValue);
    RefreshLegend();
    RequestRedraw();
}

void UltraCanvasEngineScatterChart::SetAutoColorDomain() {
    autoColorDomain = true;
    RefreshLegend();
    RequestRedraw();
}

void UltraCanvasEngineScatterChart::SetShowColorBar(bool show, const std::string& title) {
    showColorBar = show;
    colorBarTitle = title;
    RefreshLegend();
}

void UltraCanvasEngineScatterChart::SetShowTrendLine(bool show) {
    showTrendLine = show;
    MarkEngineDirty(ChartDirty::Style);     // the correlation readout is a label
}

void UltraCanvasEngineScatterChart::SetTrendLineStyle(const Color& color, float width,
                                                      bool dashed) {
    trendColor = color;
    trendWidth = std::max(0.5f, width);
    trendDashed = dashed;
    RequestRedraw();
}

void UltraCanvasEngineScatterChart::SetShowCorrelationInfo(bool show) {
    showCorrelationInfo = show;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineScatterChart::SetShowConfidenceEllipses(bool show, double confidence) {
    showEllipses = show;
    ellipseConfidence = std::clamp(confidence, 0.05, 0.999);
    RebuildHighlights();
}

void UltraCanvasEngineScatterChart::SetPointLabelMode(EngineScatterLabelMode mode) {
    labelMode = mode;
    MarkEngineDirty(ChartDirty::Style);
}

// =============================================================================
// HELPERS
// =============================================================================

bool UltraCanvasEngineScatterChart::HasSizeChannel() const {
    for (const EngineScatterGroup& g : groups) {
        for (const EngineScatterPoint& p : g.points) if (Finite(p.size)) return true;
    }
    return false;
}

bool UltraCanvasEngineScatterChart::HasColorChannel() const {
    for (const EngineScatterGroup& g : groups) {
        for (const EngineScatterPoint& p : g.points) if (Finite(p.colorValue)) return true;
    }
    return false;
}

void UltraCanvasEngineScatterChart::SizeDomain(double& lo, double& hi) const {
    if (!autoSizeDomain) { lo = sizeMin; hi = sizeMax; return; }
    lo = std::numeric_limits<double>::max();
    hi = std::numeric_limits<double>::lowest();
    for (const EngineScatterGroup& g : groups) {
        for (const EngineScatterPoint& p : g.points) {
            if (!Finite(p.size)) continue;
            lo = std::min(lo, p.size);
            hi = std::max(hi, p.size);
        }
    }
    if (lo > hi) { lo = 0.0; hi = 1.0; }
}

void UltraCanvasEngineScatterChart::ColorDomain(double& lo, double& hi) const {
    if (!autoColorDomain) { lo = colorMin; hi = colorMax; return; }
    lo = std::numeric_limits<double>::max();
    hi = std::numeric_limits<double>::lowest();
    for (const EngineScatterGroup& g : groups) {
        for (const EngineScatterPoint& p : g.points) {
            if (!Finite(p.colorValue)) continue;
            lo = std::min(lo, p.colorValue);
            hi = std::max(hi, p.colorValue);
        }
    }
    if (lo > hi) { lo = 0.0; hi = 1.0; }
}

double UltraCanvasEngineScatterChart::RadiusOf(const EngineScatterPoint& point) const {
    if (!Finite(point.size)) return markerRadius;
    double lo, hi;
    SizeDomain(lo, hi);
    const double t = (hi - lo) < 1e-12 ? 1.0 : std::clamp((point.size - lo) / (hi - lo), 0.0, 1.0);
    const double r0 = minRadius, r1 = maxRadius;
    // Area scaling: the circle's area, not its radius, follows the value.
    return sizeScale == EngineBubbleScale::Area ? std::sqrt(r0 * r0 + (r1 * r1 - r0 * r0) * t)
                                                : r0 + (r1 - r0) * t;
}

Color UltraCanvasEngineScatterChart::GroupColor(size_t group) const {
    if (group < groups.size() && groups[group].color.a > 0) return groups[group].color;
    return Palette().ColorAt(group, groups.size());
}

Color UltraCanvasEngineScatterChart::PointColor(size_t group,
                                                const EngineScatterPoint& point) const {
    if (point.color.a > 0) return point.color;
    if (Finite(point.colorValue)) {
        double lo, hi;
        ColorDomain(lo, hi);
        const double t = (hi - lo) < 1e-12 ? 0.5
                                           : std::clamp((point.colorValue - lo) / (hi - lo), 0.0, 1.0);
        return SampleColormap(colormap, customColormap, t, colormapReversed);
    }
    return GroupColor(group);
}

void UltraCanvasEngineScatterChart::ApplyFormat(ChartAxis& axis, const AxisFormat& format) const {
    axis.title = (GetProjectionKind() == ChartProjectionKind::Polar) ? std::string() : format.title;
    if (!format.categories.empty()) {
        axis.scale = ChartScale::Category;
        axis.categories = format.categories;
        axis.categoryPadding = 0.5;          // marks on the outer slots keep their room
        return;
    }
    axis.unitPrefix = format.prefix;
    axis.unitSuffix = format.suffix;
    axis.compactNumbers = format.compact;
    axis.decimals = format.decimals;
    axis.formatter = format.formatter;
    if (format.explicitRange) axis.SetRange(format.lo, format.hi);
}

void UltraCanvasEngineScatterChart::RefreshLegend() {
    // The size key, then the colour bar, then the group list - one legend,
    // the most informative key for what the chart shows.
    if (showSizeLegend && HasSizeChannel()) {
        double lo, hi;
        SizeDomain(lo, hi);
        LegendSizeScale scale;
        scale.minValue = lo;
        scale.maxValue = hi;
        scale.minRadius = minRadius;
        scale.maxRadius = maxRadius;
        scale.areaScaled = (sizeScale == EngineBubbleScale::Area);
        scale.values = sizeLegendValues;
        scale.sampleCount = 3;
        scale.fillColor = GetTheme().axisLabelColor.WithAlpha(60);
        scale.strokeColor = GetTheme().axisLabelColor;
        Legend().SetSizeScale(scale);
        SetLegendMode(ChartLegendMode::SizeLegend);
        SetLegendTitle(sizeLegendTitle);
        SetShowLegend(true);
        return;
    }
    if (showColorBar && HasColorChannel()) {
        double lo, hi;
        ColorDomain(lo, hi);
        LegendColorBar bar;
        bar.colormap = colormap;
        bar.customColormap = customColormap;
        bar.reverse = colormapReversed;
        bar.minValue = lo;
        bar.maxValue = hi;
        Legend().SetColorBar(bar);
        SetLegendMode(ChartLegendMode::ColorBar);
        SetLegendTitle(colorBarTitle);
        SetShowLegend(true);
        return;
    }
    std::vector<ChartLegendEntry> entries;
    for (size_t g = 0; g < groups.size(); ++g) {
        ChartLegendEntry entry(groups[g].name, GroupColor(g), SwatchFor(groups[g].marker));
        entry.enabled = Visible(g);
        entries.push_back(entry);
    }
    SetLegendMode(ChartLegendMode::Discrete);
    SetLegendTitle(std::string());
    SetLegendEntries(entries);
    SetShowLegend(groups.size() > 1);
}

void UltraCanvasEngineScatterChart::RebuildHighlights() {
    ClearHighlights();
    if (!showEllipses) return;
    for (size_t g = 0; g < groups.size(); ++g) {
        if (!Visible(g) || groups[g].points.size() < 3) continue;
        ChartHighlight h;
        h.shape = ChartHighlightShape::ConfidenceEllipse;
        h.confidence = ellipseConfidence;
        h.xAxisIndex = 1;
        h.yAxisIndex = 0;
        for (const EngineScatterPoint& p : groups[g].points) h.members.emplace_back(p.x, p.y);
        const Color color = GroupColor(g);
        h.fill = color.WithAlpha(38);
        h.stroke = color.WithAlpha(170);
        h.dashed = true;
        AddHighlight(h);
    }
}

std::string UltraCanvasEngineScatterChart::Tooltip(size_t group, const EngineScatterPoint& point,
                                                   const ChartAxis& x, const ChartAxis& y) const {
    std::ostringstream out;
    if (!point.label.empty()) out << point.label << "\n";
    if (groups.size() > 1 && !groups[group].name.empty()) out << groups[group].name << "\n";
    out << (xFormat.title.empty() ? "x" : xFormat.title) << ": " << x.FormatValue(point.x) << "\n"
        << (yFormat.title.empty() ? "y" : yFormat.title) << ": " << y.FormatValue(point.y);
    if (Finite(point.size)) {
        out << "\n" << (sizeLegendTitle.empty() ? "size" : sizeLegendTitle) << ": "
            << point.size;   // locale-ok: tooltip text a person reads
    }
    if (Finite(point.colorValue)) {
        out << "\n" << (colorBarTitle.empty() ? "colour" : colorBarTitle) << ": "
            << point.colorValue;   // locale-ok: tooltip text a person reads
    }
    return out.str();
}

// =============================================================================
// ENGINE CONTRACT
// =============================================================================

void UltraCanvasEngineScatterChart::DescribeAxes(ChartAxisSet& axes) {
    const bool horizontal = GetProjectionKind() == ChartProjectionKind::Horizontal;

    ChartAxis y("y");
    y.side = horizontal ? ChartAxisSide::Bottom : ChartAxisSide::Left;
    ApplyFormat(y, yFormat);
    ChartAxis x("x");
    x.side = horizontal ? ChartAxisSide::Left : ChartAxisSide::Bottom;
    ApplyFormat(x, xFormat);

    for (size_t g = 0; g < groups.size(); ++g) {
        if (!Visible(g)) continue;
        for (const EngineScatterPoint& p : groups[g].points) {
            if (xFormat.categories.empty() && !xFormat.explicitRange) x.Observe(p.x);
            if (yFormat.categories.empty() && !yFormat.explicitRange) y.Observe(p.y);
        }
    }
    // Outliers keep their whole marker inside the plot; bubbles need room for
    // the largest radius (a fraction of the plot, as the plot size is not
    // known while the axes are described).
    const double padding = HasSizeChannel() ? 0.12 : 0.05;
    x.rangePadding = padding;
    y.rangePadding = padding;
    axes.Add(y);    // 0
    axes.Add(x);    // 1

    SetGridAxis(0);
    SetDomainGridAxis(showXGrid ? 1 : static_cast<size_t>(-1));
}

void UltraCanvasEngineScatterChart::RenderChartContent(IRenderContext* ctx,
                                                       const ChartEngineFrame& frame) {
    if (groups.empty() || !frame.axes || frame.axes->Count() < 2) return;
    const ChartAxis& y = frame.axes->At(0);
    const ChartAxis& x = frame.axes->At(1);
    const IChartProjection& projection = *frame.projection;
    const double progress = frame.animationProgress;
    const int64_t hovered = HoveredRegionId();

    // ---- trend line (slot 700 analysis, drawn under the marks it summarises)
    if (showTrendLine && xFormat.categories.empty() && yFormat.categories.empty()) {
        std::vector<double> xs, ys;
        for (size_t g = 0; g < groups.size(); ++g) {
            if (!Visible(g)) continue;
            for (const EngineScatterPoint& p : groups[g].points) {
                xs.push_back(p.x);
                ys.push_back(p.y);
            }
        }
        const ChartLinearFit fit = FitLinearTrend(xs, ys);
        if (fit.valid) {
            const double x0 = x.Min(), x1 = x.Min() + (x.Max() - x.Min()) * progress;
            ChartLineOptions options;
            options.samplesPerSegment = 32;     // follows the rings under Polar
            const std::vector<ChartLineRun> runs = BuildLineRuns(
                projection,
                {ChartNormalizedPoint(x.Normalize(x0), y.Normalize(fit.Evaluate(x0))),
                 ChartNormalizedPoint(x.Normalize(x1), y.Normalize(fit.Evaluate(x1)))},
                options);
            ctx->PushState();
            ctx->SetStrokePaint(trendColor);
            ctx->SetStrokeWidth(trendWidth);
            if (trendDashed) ctx->SetLineDash(UCDashPattern({8.0, 5.0}));
            for (const ChartLineRun& run : runs) {
                if (run.points.size() >= 2) ctx->DrawLinePath(run.points, false);
            }
            ctx->PopState();
        }
    }

    // ---- marks: the largest first, so small bubbles stay on top -------------
    struct Mark { size_t group; size_t index; double radius; };
    std::vector<Mark> marks;
    for (size_t g = 0; g < groups.size(); ++g) {
        if (!Visible(g)) continue;
        for (size_t i = 0; i < groups[g].points.size(); ++i) {
            marks.push_back({g, i, RadiusOf(groups[g].points[i])});
        }
    }
    std::stable_sort(marks.begin(), marks.end(),
                     [](const Mark& a, const Mark& b) { return a.radius > b.radius; });

    const bool insideLabels = labelMode == EngineScatterLabelMode::Inside;
    ctx->SetFontSize(axisFontSize);
    for (const Mark& mark : marks) {
        const EngineScatterPoint& point = groups[mark.group].points[mark.index];
        if (!Finite(point.x) || !Finite(point.y)) continue;
        const Point2Dd p = projection.ToScreen(
            ChartNormalizedPoint(x.Normalize(point.x), y.Normalize(point.y)));
        const int64_t id = RegionId(mark.group, mark.index);
        const bool isHovered = (hovered == id);
        const double radius = std::max(0.5, mark.radius * progress) + (isHovered ? 2.0 : 0.0);
        const Color base = PointColor(mark.group, point);
        const Color fill = isHovered ? base : base.WithAlpha(static_cast<uint8_t>(255 * markerOpacity));
        const Color outline = outlineColor.a > 0 ? outlineColor
                                                 : (isHovered ? base.Darken(0.4f) : Color(0, 0, 0, 0));
        DrawChartMarker(ctx, groups[mark.group].marker, p, radius, fill, outline,
                        isHovered ? 2.0f : outlineWidth);

        if (insideLabels && !point.label.empty() && progress >= 1.0) {
            const Size2Di size = ctx->GetTextLineDimensions(point.label);
            if (size.width <= radius * 2.0 - 6.0 && size.height <= radius * 2.0 - 2.0) {
                ctx->SetTextPaint(ContrastText(base));
                ctx->DrawText(point.label, Point2Dd(p.x - size.width * 0.5, p.y - size.height * 0.5));
            }
        }
        const double r = std::max(5.0, radius);
        AddHitRegion(Rect2Dd(p.x - r, p.y - r, r * 2.0, r * 2.0), id, Tooltip(mark.group, point, x, y));
    }
}

void UltraCanvasEngineScatterChart::CollectChartLabels(IRenderContext* ctx,
                                                       ChartLabelBroker& broker) {
    if (!Frame().axes || Frame().axes->Count() < 2 || !Frame().projection) return;
    const ChartAxis& y = Frame().axes->At(0);
    const ChartAxis& x = Frame().axes->At(1);
    const IChartProjection& projection = *Frame().projection;
    ctx->SetFontSize(axisFontSize);

    // Every mark is an obstacle, so labels never cover another point. A mark
    // that carries its own label is already a shape in the solver (its
    // anchorRadius); boxing it as an obstacle as well pushed its label away
    // from its own corners and off to wherever the cluster had room.
    const bool labelled = labelMode != EngineScatterLabelMode::Hidden;
    for (size_t g = 0; g < groups.size(); ++g) {
        if (!Visible(g)) continue;
        for (const EngineScatterPoint& point : groups[g].points) {
            if (!Finite(point.x) || !Finite(point.y)) continue;
            if (labelled && !point.label.empty()) continue;
            const Point2Dd p = projection.ToScreen(
                ChartNormalizedPoint(x.Normalize(point.x), y.Normalize(point.y)));
            const double r = RadiusOf(point);
            broker.AddObstacleRect(Rect2Dd(p.x - r, p.y - r, r * 2.0, r * 2.0));
        }
    }

    if (labelMode != EngineScatterLabelMode::Hidden) {
        for (size_t g = 0; g < groups.size(); ++g) {
            if (!Visible(g)) continue;
            for (const EngineScatterPoint& point : groups[g].points) {
                if (point.label.empty() || !Finite(point.x) || !Finite(point.y)) continue;
                const double r = RadiusOf(point);
                const Size2Di size = ctx->GetTextLineDimensions(point.label);
                // Inside labels that fit are drawn in the mark by the content.
                if (labelMode == EngineScatterLabelMode::Inside &&
                    size.width <= r * 2.0 - 6.0 && size.height <= r * 2.0 - 2.0) continue;
                ChartLabelRequest request;
                request.text = point.label;
                request.klass = ChartLabelClass::ValueLabel;
                request.anchor = projection.ToScreen(
                    ChartNormalizedPoint(x.Normalize(point.x), y.Normalize(point.y)));
                request.anchorRadius = r + 1.0;
                request.textSize = Size2Dd(size.width, size.height);
                request.preferredSide = LabelSide::Auto;
                request.allowSuppress = true;
                request.wantLeaderLine = true;   // a name pushed off its bubble points back to it
                request.priority = static_cast<int>(r);      // big marks keep their names
                broker.Add(request);
            }
        }
    }

    // The correlation readout rides the plan at the trend line's high end.
    if (showTrendLine && showCorrelationInfo && xFormat.categories.empty() &&
        yFormat.categories.empty()) {
        std::vector<double> xs, ys;
        for (size_t g = 0; g < groups.size(); ++g) {
            if (!Visible(g)) continue;
            for (const EngineScatterPoint& p : groups[g].points) {
                xs.push_back(p.x);
                ys.push_back(p.y);
            }
        }
        const ChartLinearFit fit = FitLinearTrend(xs, ys);
        if (fit.valid) {
            std::ostringstream text;
            text.setf(std::ios::fixed);
            text.precision(2);
            // locale-ok: a readout a person reads, not a file format
            text << "y = " << fit.slope << "x " << (fit.intercept < 0 ? "- " : "+ ")
                 << std::abs(fit.intercept) << "   r = " << fit.correlation
                 << "   r\xC2\xB2 = " << fit.r2;
            ChartLabelRequest request;
            request.text = text.str();
            request.klass = ChartLabelClass::Annotation;
            const double xEnd = x.Min() + (x.Max() - x.Min()) * 0.75;
            request.anchor = projection.ToScreen(
                ChartNormalizedPoint(x.Normalize(xEnd), y.Normalize(fit.Evaluate(xEnd))));
            const Size2Di size = ctx->GetTextLineDimensions(request.text);
            request.textSize = Size2Dd(size.width, size.height);
            request.preferredSide = LabelSide::Top;
            request.priority = 20;
            broker.Add(request);
        }
    }
}

void UltraCanvasEngineScatterChart::OnThemeChanged() {
    RefreshLegend();
    RebuildHighlights();                 // ellipse washes take the group colours
}

void UltraCanvasEngineScatterChart::OnLegendEntryToggled(size_t entryIndex, bool enabled) {
    if (entryIndex >= hidden.size()) return;
    hidden[entryIndex] = !enabled;
    RebuildHighlights();
    MarkEngineDirty(ChartDirty::Data);   // the ranges follow the visible groups
}

} // namespace UltraCanvas
