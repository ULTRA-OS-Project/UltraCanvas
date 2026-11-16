// Plugins/Charts/Engine/Charts/UltraCanvasRidgelineChart.cpp
// A ridgeline chart (a joyplot) on the chart engine.
// Version: 1.0.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasRidgelineChart.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace UltraCanvas {

namespace {

std::string Fixed(double v, int decimals) {
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%.*f", std::clamp(decimals, 0, 6), v);   // locale-ok: display text
    return buffer;
}

std::string Short(double v) {
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%.3g", v);   // locale-ok: display text
    return buffer;
}

// The curve's height at x, by straight interpolation; 0 outside it.
double HeightAt(const std::vector<RidgelineCurvePoint>& curve, double x) {
    if (curve.size() < 2 || x < curve.front().x || x > curve.back().x) return 0.0;
    auto it = std::lower_bound(curve.begin(), curve.end(), x,
                               [](const RidgelineCurvePoint& p, double v) { return p.x < v; });
    if (it == curve.begin()) return it->y;
    if (it == curve.end()) return curve.back().y;
    const RidgelineCurvePoint& b = *it;
    const RidgelineCurvePoint& a = *(it - 1);
    const double span = b.x - a.x;
    return span > 0.0 ? a.y + (b.y - a.y) * (x - a.x) / span : b.y;
}

} // namespace

UltraCanvasRidgelineChart::UltraCanvasRidgelineChart(const std::string& id, int x, int y, int w, int h)
    : UltraCanvasChartEngineElement(id, x, y, w, h) {
    SetShowLegend(false);
}

// =============================================================================
// DATA AND SETTINGS
// =============================================================================

void UltraCanvasRidgelineChart::SetModel(const UltraCanvasRidgelineModel& m) {
    model = m;
    SetChartTitle(model.title);
    hoveredRow = -1;
    Recompute();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasRidgelineChart::Recompute() {
    curves = model.Curves();
    rawCurves.assign(model.rows.size(), {});
    statistics.assign(model.rows.size(), RidgelineRowStatistics());
    for (size_t i = 0; i < model.rows.size(); ++i) {
        rawCurves[i] = model.RawCurve(i);
        statistics[i] = model.Statistics(i);
    }
    xRange = model.XRange();
}

void UltraCanvasRidgelineChart::SetOverlap(double rows) {
    overlap = std::clamp(rows, 0.5, 5.0);
    MarkEngineDirty(ChartDirty::Geometry);
}

void UltraCanvasRidgelineChart::SetShowRowLines(bool show) {
    showRowLines = show;
    RequestRedraw();
}

void UltraCanvasRidgelineChart::SetXTicks(const std::vector<double>& ticks) {
    xTicks = ticks;
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasRidgelineChart::SetXFormatter(ValueFormatter formatter) {
    xFormatter = std::move(formatter);
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasRidgelineChart::SetColoring(RidgelineColoring c) {
    coloring = c;
    RequestRedraw();
}

void UltraCanvasRidgelineChart::SetColormap(HeatmapColormap map, bool reversed) {
    colormap = map;
    colormapReversed = reversed;
    RequestRedraw();
}

void UltraCanvasRidgelineChart::SetFillColor(const Color& color) {
    fillColor = color;
    RequestRedraw();
}

void UltraCanvasRidgelineChart::SetAlternateColors(const Color& first, const Color& second) {
    alternate[0] = first;
    alternate[1] = second;
    RequestRedraw();
}

void UltraCanvasRidgelineChart::SetFillOpacity(float opacity) {
    fillOpacity = std::clamp(opacity, 0.0f, 1.0f);
    RequestRedraw();
}

void UltraCanvasRidgelineChart::SetGradientFill(bool gradient) {
    gradientFill = gradient;
    RequestRedraw();
}

void UltraCanvasRidgelineChart::SetOutline(const Color& color, float width) {
    outlineColor = color;
    outlineWidth = std::max(0.0f, width);
    RequestRedraw();
}

void UltraCanvasRidgelineChart::SetValueColumn(const std::string& title, RidgelineValue value, int decimals) {
    valueTitle = title;
    valueColumn = value;
    valueDecimals = decimals;
    MarkEngineDirty(ChartDirty::Geometry);     // the column's width
}

// =============================================================================
// READING
// =============================================================================

Color UltraCanvasRidgelineChart::RowColor(size_t row) const {
    if (row < model.rows.size() && model.rows[row].color.a > 0) return model.rows[row].color.WithAlpha(255);
    const Color first = Palette().ColorAt(0).WithAlpha(255);
    const size_t n = model.rows.size();
    switch (coloring) {
        case RidgelineColoring::Alternating: {
            const Color& own = alternate[row % 2];
            if (own.a > 0) return own.WithAlpha(255);
            return row % 2 == 0 ? first : first.Blend(Color(255, 255, 255, 255), 0.35f);
        }
        case RidgelineColoring::ByRow: {
            const double t = n > 1 ? static_cast<double>(row) / static_cast<double>(n - 1) : 0.5;
            return SampleColormap(colormap, {}, t, colormapReversed).WithAlpha(255);
        }
        case RidgelineColoring::ByRowValue: {
            double lo = 0.0, hi = 0.0;
            bool any = false;
            for (size_t i = 0; i < n; ++i) {
                if (i >= statistics.size() || !statistics[i].valid) continue;
                const double v = RowValue(i);
                if (!any) { lo = hi = v; any = true; }
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
            const double t = (any && hi > lo) ? (RowValue(row) - lo) / (hi - lo) : 0.5;
            return SampleColormap(colormap, {}, std::clamp(t, 0.0, 1.0), colormapReversed).WithAlpha(255);
        }
        default:
            return fillColor.a > 0 ? fillColor.WithAlpha(255) : first;
    }
}

double UltraCanvasRidgelineChart::RowValue(size_t row) const {
    if (row >= statistics.size()) return 0.0;
    const RidgelineRowStatistics& s = statistics[row];
    switch (valueColumn) {
        case RidgelineValue::Median: return s.median;
        case RidgelineValue::Count: return static_cast<double>(s.count);
        case RidgelineValue::Peak: return s.peakAt;
        default: return s.mean;
    }
}

std::string UltraCanvasRidgelineChart::RowValueText(size_t row) const {
    if (row >= statistics.size() || !statistics[row].valid || valueColumn == RidgelineValue::Hidden) return std::string();
    switch (valueColumn) {
        case RidgelineValue::Count: return std::to_string(statistics[row].count);
        case RidgelineValue::Peak: return FormatX(statistics[row].peakAt);
        default: return Fixed(RowValue(row), valueDecimals);
    }
}

std::string UltraCanvasRidgelineChart::FormatX(double x) const {
    if (xFormatter) return xFormatter(x);
    const double span = xRange.second - xRange.first;
    return Fixed(x, span < 20.0 ? 2 : (span < 200.0 ? 1 : 0));
}

std::string UltraCanvasRidgelineChart::DescribeRow(size_t row, double x) const {
    if (row >= model.rows.size()) return std::string();
    const RidgelineRowStatistics& s = statistics[row];
    std::string text = model.rows[row].name;
    text += "\n" + FormatX(x) + ": " + model.curveName + " " + Short(row < rawCurves.size() ? HeightAt(rawCurves[row], x) : 0.0);
    if (s.valid && s.count > 0) {
        text += "\n" + std::to_string(s.count) + " values, mean " + Fixed(s.mean, 2) + ", median " + Fixed(s.median, 2);
    } else if (s.valid) {
        text += "\nPeak at " + FormatX(s.peakAt) + ", middle half " + FormatX(s.quartile1) + " to " + FormatX(s.quartile3);
    }
    if (!model.rows[row].note.empty()) text += "\n" + model.rows[row].note;
    return text;
}

double UltraCanvasRidgelineChart::RowPitch() const {
    const double n = static_cast<double>(model.rows.size());
    return 1.0 / std::max(1.0, n - 1.0 + overlap + kBottomPad);
}

double UltraCanvasRidgelineChart::RowBaseline(size_t row) const {
    const double n = static_cast<double>(model.rows.size());
    return (n - 1.0 - static_cast<double>(row) + kBottomPad) * RowPitch();
}

double UltraCanvasRidgelineChart::RowHeightAt(size_t row, double x) const {
    if (row >= curves.size()) return 0.0;
    return HeightAt(curves[row], x) * overlap * RowPitch();
}

void UltraCanvasRidgelineChart::OnHitRegionHoverChanged(int64_t regionId) {
    // Every strip is its own region, so this runs as the pointer moves along
    // a row as well as between rows.
    const int row = regionId >= 0 ? static_cast<int>(regionId / kStrips) : -1;
    if (row >= 0) {
        const int strip = static_cast<int>(regionId % kStrips);
        hoveredX = xRange.first + (xRange.second - xRange.first) * (strip + 0.5) / kStrips;
    }
    hoveredRow = row;
    if (onRowHover) onRowHover(row);
}

// =============================================================================
// ENGINE CONTRACT
// =============================================================================

void UltraCanvasRidgelineChart::DescribeAxes(ChartAxisSet& axes) {
    // The rows: a hidden 0..1 axis. Their baselines are placed by the chart,
    // and their names are drawn beside the plot with the value column.
    ChartAxis rows("rows");
    rows.side = ChartAxisSide::Left;
    rows.visible = false;
    rows.SetRange(0.0, 1.0);
    axes.Add(rows);

    ChartAxis x("x");
    x.side = ChartAxisSide::Bottom;
    x.title = model.xTitle;
    x.SetRange(xRange.first, xRange.second);
    x.tickValues = xTicks;
    x.formatter = xFormatter;
    axes.Add(x);

    SetGridAxis(static_cast<size_t>(-1));
    SetDomainGridAxis(1);
}

double UltraCanvasRidgelineChart::SubtitleHeight(IRenderContext* ctx) const {
    if (model.subtitle.empty()) return 0.0;
    ctx->SetFontSize(axisFontSize);
    return ctx->GetTextLineHeight(model.subtitle) + 4.0;
}

void UltraCanvasRidgelineChart::MeasureContent(IRenderContext* ctx, ChartLayoutRequest& request) {
    ctx->SetFontSize(axisFontSize);
    ctx->SetFontWeight(FontWeight::Normal);
    double names = 0.0;
    for (const RidgelineRow& row : model.rows) {
        names = std::max(names, static_cast<double>(ctx->GetTextLineDimensions(row.name).width));
    }
    double values = 0.0;
    if (valueColumn != RidgelineValue::Hidden) {
        values = static_cast<double>(ctx->GetTextLineDimensions(valueTitle).width);
        ctx->SetFontWeight(FontWeight::Bold);
        for (size_t i = 0; i < model.rows.size(); ++i) {
            values = std::max(values, static_cast<double>(ctx->GetTextLineDimensions(RowValueText(i)).width));
        }
        ctx->SetFontWeight(FontWeight::Normal);
    }
    const double gap = 8.0;
    request.Reserve(ChartAxisEdge::Left, names + gap + (values > 0.0 ? values + 12.0 : 0.0) + 4.0);
    // The column's header sits above the top row's name, inside the plot
    // while the top curve leaves room there; a small overlap does not.
    const double line = ctx->GetTextLineHeight("Ag");
    const bool headerAbove = valueColumn != RidgelineValue::Hidden && !valueTitle.empty() && overlap < 1.6;
    request.Reserve(ChartAxisEdge::Top, SubtitleHeight(ctx) + (headerAbove ? line + 6.0 : 0.0) + 4.0);
}

void UltraCanvasRidgelineChart::RenderEngineTitle(IRenderContext* ctx) {
    UltraCanvasChartEngineElement::RenderEngineTitle(ctx);
    if (model.subtitle.empty()) return;
    double top = 4.0;
    if (!chartTitle.empty()) {
        ctx->SetFontSize(titleFontSize);
        top += ctx->GetTextLineHeight(chartTitle) + 2.0;
    }
    ctx->SetFontSize(axisFontSize);
    ctx->SetFontWeight(FontWeight::Normal);
    ctx->SetTextPaint(axisLabelColor);
    const Size2Di size = ctx->GetTextLineDimensions(model.subtitle);
    ctx->DrawText(model.subtitle, Point2Dd((GetWidth() - size.width) * 0.5, top));
}

void UltraCanvasRidgelineChart::RenderChartContent(IRenderContext* ctx, const ChartEngineFrame& frame) {
    if (!frame.axes || frame.axes->Count() < 2 || !frame.projection) return;
    const ChartAxis& x = frame.axes->At(1);
    const IChartProjection& projection = *frame.projection;
    const double amplitude = overlap * RowPitch() * frame.animationProgress;
    const double x0 = xRange.first, x1 = xRange.second;

    // From the top row down: each row's fill covers the outline of the row
    // behind it, and its hit strips are added after (last added wins).
    for (size_t i = 0; i < curves.size(); ++i) {
        const std::vector<RidgelineCurvePoint>& curve = curves[i];
        const double base = RowBaseline(i);
        if (showRowLines) {
            ctx->SetStrokePaint(GetTheme().gridColor);
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawLine(projection.ToScreen(ChartNormalizedPoint(0.0, base)),
                          projection.ToScreen(ChartNormalizedPoint(1.0, base)));
        }
        if (curve.size() < 2) continue;

        std::vector<ChartNormalizedPoint> top, bottom;
        top.reserve(curve.size());
        bottom.reserve(curve.size());
        for (const RidgelineCurvePoint& p : curve) {
            const double u = x.Normalize(p.x);
            top.emplace_back(u, base + amplitude * p.y);
            bottom.emplace_back(u, base);
        }
        ChartLineOptions options;
        // A density is evaluated densely already; a supplied curve may be
        // coarse, so it is joined smoothly (monotone: never above its points).
        const bool supplied = !model.rows[i].HasValues();
        options.shape = supplied ? ChartLineShape::Curved : ChartLineShape::Straight;
        options.samplesPerSegment = supplied ? 6 : 1;

        const Color color = RowColor(i);
        double peak = 0.0;
        for (const RidgelineCurvePoint& p : curve) peak = std::max(peak, p.y);
        for (const std::vector<Point2Dd>& outline : BuildAreaOutlines(projection, top, bottom, options)) {
            if (outline.size() < 3) continue;
            if (gradientFill) {
                const Point2Dd from = projection.ToScreen(ChartNormalizedPoint(0.5, base + amplitude * peak));
                const Point2Dd to = projection.ToScreen(ChartNormalizedPoint(0.5, base));
                ctx->SetFillPaint(ctx->CreateLinearGradientPattern(
                    from.x, from.y, to.x, to.y,
                    {GradientStop(0.0, color.WithAlpha(static_cast<uint8_t>(255.0f * fillOpacity))),
                     GradientStop(1.0, color.WithAlpha(static_cast<uint8_t>(255.0f * fillOpacity * 0.35f)))}));
            } else {
                ctx->SetFillPaint(color.WithAlpha(static_cast<uint8_t>(255.0f * fillOpacity)));
            }
            ctx->FillLinePath(outline);
        }
        if (outlineWidth > 0.0f) {
            const Color stroke = outlineColor.a > 0 ? outlineColor : color.Blend(Color(0, 0, 0, 255), 0.35f);
            ctx->SetStrokePaint(stroke);
            ctx->SetStrokeWidth(static_cast<int>(i) == hoveredRow ? outlineWidth + 1.0f : outlineWidth);
            for (const ChartLineRun& run : BuildLineRuns(projection, top, options)) {
                if (run.points.size() >= 2) ctx->DrawLinePath(run.points, false);
            }
        }

        // Hit strips: the area under the curve, cut into vertical strips so
        // the tooltip can say where along the row the pointer is.
        for (int s = 0; s < kStrips; ++s) {
            const double a = x0 + (x1 - x0) * s / kStrips, b = x0 + (x1 - x0) * (s + 1) / kStrips;
            std::vector<Point2Dd> polygon;
            double tallest = 0.0;
            auto add = [&](double at) {
                const double h = HeightAt(curve, at);
                tallest = std::max(tallest, h);
                polygon.push_back(projection.ToScreen(ChartNormalizedPoint(x.Normalize(at), base + amplitude * h)));
            };
            add(a);
            for (const RidgelineCurvePoint& p : curve) if (p.x > a && p.x < b) add(p.x);
            add(b);
            if (tallest < 0.01) continue;
            polygon.push_back(projection.ToScreen(ChartNormalizedPoint(x.Normalize(b), base)));
            polygon.push_back(projection.ToScreen(ChartNormalizedPoint(x.Normalize(a), base)));
            AddHitRegion(polygon, static_cast<int64_t>(i) * kStrips + s, DescribeRow(i, (a + b) * 0.5));
        }
    }
}

void UltraCanvasRidgelineChart::RenderInteractionOverlay(IRenderContext* ctx, const ChartEngineFrame& frame) {
    if (!frame.projection || !frame.axes || frame.axes->Count() < 2 || model.rows.empty()) return;
    const IChartProjection& projection = *frame.projection;
    const Rect2Dd& plot = frame.plotArea;

    // The pointer's x on the hovered row: a rule from the baseline to the curve.
    if (hoveredRow >= 0 && static_cast<size_t>(hoveredRow) < curves.size()) {
        const double u = frame.axes->At(1).Normalize(hoveredX);
        const double base = RowBaseline(static_cast<size_t>(hoveredRow));
        ctx->SetStrokePaint(GetTheme().axisLineColor.WithAlpha(170));
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawLine(projection.ToScreen(ChartNormalizedPoint(u, base)),
                      projection.ToScreen(ChartNormalizedPoint(u, base + RowHeightAt(static_cast<size_t>(hoveredRow), hoveredX))));
    }

    // The row names, right-aligned left of the value column, each sitting on
    // its baseline. Rows closer than a line of text show every n-th name.
    ctx->SetFontSize(axisFontSize);
    ctx->SetFontWeight(FontWeight::Normal);
    const double line = ctx->GetTextLineHeight("Ag");
    double valuesWidth = 0.0;
    if (valueColumn != RidgelineValue::Hidden) {
        valuesWidth = static_cast<double>(ctx->GetTextLineDimensions(valueTitle).width);
        ctx->SetFontWeight(FontWeight::Bold);
        for (size_t i = 0; i < model.rows.size(); ++i) {
            valuesWidth = std::max(valuesWidth, static_cast<double>(ctx->GetTextLineDimensions(RowValueText(i)).width));
        }
    }
    const double valuesRight = plot.x - 8.0;
    const double namesRight = valuesWidth > 0.0 ? valuesRight - valuesWidth - 12.0 : valuesRight;
    const double pitchPixels = RowPitch() * plot.height;
    const size_t step = pitchPixels > 0.0 ? static_cast<size_t>(std::max(1.0, std::ceil(line * 1.05 / pitchPixels))) : 1;

    double firstTop = plot.y;
    for (size_t i = 0; i < model.rows.size(); ++i) {
        if (i % step != 0) continue;
        const double baseY = projection.ToScreen(ChartNormalizedPoint(0.0, RowBaseline(i))).y;
        const double top = baseY - line * 0.95;
        if (i == 0) firstTop = top;
        const bool hovered = static_cast<int>(i) == hoveredRow;
        ctx->SetFontWeight(hovered ? FontWeight::Bold : FontWeight::Normal);
        ctx->SetTextPaint(axisLabelColor);
        const std::string& name = model.rows[i].name;
        ctx->DrawText(name, Point2Dd(namesRight - ctx->GetTextLineDimensions(name).width, top));
        if (valuesWidth > 0.0) {
            const std::string value = RowValueText(i);
            ctx->SetFontWeight(FontWeight::Bold);
            ctx->DrawText(value, Point2Dd(valuesRight - ctx->GetTextLineDimensions(value).width, top));
        }
    }
    if (valuesWidth > 0.0 && !valueTitle.empty()) {
        ctx->SetFontWeight(FontWeight::Normal);
        ctx->SetTextPaint(axisLabelColor);
        const double width = ctx->GetTextLineDimensions(valueTitle).width;
        ctx->DrawText(valueTitle, Point2Dd(valuesRight - width, firstTop - line - 2.0));
    }
    ctx->SetFontWeight(FontWeight::Normal);
}

} // namespace UltraCanvas
