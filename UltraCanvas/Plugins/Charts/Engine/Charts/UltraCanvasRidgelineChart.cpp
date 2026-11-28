// Plugins/Charts/Engine/Charts/UltraCanvasRidgelineChart.cpp
// A ridgeline chart (a joyplot) on the chart engine.
// Version: 1.1.0 - Phase 2: series and legend, marks, quantile bands,
//                  highlight on hover, onRowClick
// Version: 1.0.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasRidgelineChart.h"
#include "UltraCanvasApplication.h"
#include <algorithm>
#include <cctype>
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

// The curve's height at x, by straight interpolation; 0 outside it. Where a
// stepped (binned) curve has two points at one x, the later step wins.
double HeightAt(const std::vector<RidgelineCurvePoint>& curve, double x) {
    if (curve.size() < 2 || x < curve.front().x || x > curve.back().x) return 0.0;
    auto it = std::upper_bound(curve.begin(), curve.end(), x,
                               [](double v, const RidgelineCurvePoint& p) { return v < p.x; });
    if (it == curve.begin()) return it->y;
    if (it == curve.end()) return curve.back().y;
    const RidgelineCurvePoint& b = *it;
    const RidgelineCurvePoint& a = *(it - 1);
    const double span = b.x - a.x;
    return span > 0.0 ? a.y + (b.y - a.y) * (x - a.x) / span : a.y;
}

} // namespace

UltraCanvasRidgelineChart::UltraCanvasRidgelineChart(const std::string& id, int x, int y, int w, int h)
    : UltraCanvasChartEngineElement(id, x, y, w, h) {
    // Right of the plot: the top is the title's and the subtitle's, the
    // bottom the x axis's, and the left the row names'.
    SetLegendPosition(ChartLegendPosition::RightStart);
    SetShowLegend(false);
}

// =============================================================================
// DATA AND SETTINGS
// =============================================================================

void UltraCanvasRidgelineChart::SetModel(const UltraCanvasRidgelineModel& m) {
    model = m;
    SetChartTitle(model.title);
    hoveredRow = -1;
    hiddenSeries.assign(SeriesCount(), false);
    Recompute();
    RefreshLegend();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasRidgelineChart::Recompute() {
    const size_t seriesCount = SeriesCount();
    curves = model.AllCurves();
    rawCurves.assign(model.rows.size(), {});
    statistics.assign(model.rows.size(), {});
    for (size_t i = 0; i < model.rows.size(); ++i) {
        rawCurves[i].resize(seriesCount);
        statistics[i].resize(seriesCount);
        for (size_t s = 0; s < seriesCount; ++s) {
            rawCurves[i][s] = model.RawCurve(i, s);
            statistics[i][s] = model.Statistics(i, s);
        }
    }
    xRange = model.XRange();
}

void UltraCanvasRidgelineChart::RefreshLegend() {
    std::vector<ChartLegendEntry> entries;
    const size_t seriesCount = SeriesCount();
    if (seriesCount > 1) {
        for (size_t s = 0; s < seriesCount; ++s) {
            const std::string name = s < model.series.size() && !model.series[s].name.empty()
                                         ? model.series[s].name
                                         : "Series " + std::to_string(s + 1);
            ChartLegendEntry entry(name, SeriesColor(s), LegendSwatch::Square);
            entry.enabled = IsSeriesVisible(s);
            entries.push_back(entry);
        }
    }
    SetLegendEntries(entries);
    SetShowLegend(!entries.empty());
}

void UltraCanvasRidgelineChart::OnChartThemeChanged() {
    RefreshLegend();
}

void UltraCanvasRidgelineChart::OnLegendEntryToggled(size_t entryIndex, bool enabled) {
    SetSeriesVisible(entryIndex, enabled);
}

void UltraCanvasRidgelineChart::SetSeriesVisible(size_t series, bool visible) {
    if (hiddenSeries.size() < SeriesCount()) hiddenSeries.resize(SeriesCount(), false);
    if (series >= hiddenSeries.size()) return;
    hiddenSeries[series] = !visible;
    RefreshLegend();
    MarkEngineDirty(ChartDirty::Data);
}

bool UltraCanvasRidgelineChart::IsSeriesVisible(size_t series) const {
    return series >= hiddenSeries.size() || !hiddenSeries[series];
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

void UltraCanvasRidgelineChart::SetMarks(unsigned m) {
    marks = m;
    RequestRedraw();
}

void UltraCanvasRidgelineChart::SetQuantileBands(bool show) {
    quantileBands = show;
    RequestRedraw();
}

void UltraCanvasRidgelineChart::SetHighlightOnHover(bool highlight) {
    highlightOnHover = highlight;
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

Color UltraCanvasRidgelineChart::SeriesColor(size_t series) const {
    if (series < model.series.size() && model.series[series].color.a > 0) return model.series[series].color.WithAlpha(255);
    return Palette().ColorAt(series, std::max<size_t>(SeriesCount(), 2)).WithAlpha(255);
}

Color UltraCanvasRidgelineChart::RowColor(size_t row, size_t series) const {
    if (SeriesCount() > 1) return SeriesColor(series);
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
                if (i >= statistics.size() || statistics[i].empty() || !statistics[i][FirstSeries(i)].valid) continue;
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

size_t UltraCanvasRidgelineChart::FirstSeries(size_t row) const {
    if (row >= model.rows.size()) return 0;
    for (size_t s = 0; s < SeriesCount(); ++s) if (model.rows[row].HasDataOf(s)) return s;
    return 0;
}

double UltraCanvasRidgelineChart::RowValue(size_t row) const {
    if (row >= statistics.size() || statistics[row].empty()) return 0.0;
    const RidgelineRowStatistics& s = statistics[row][FirstSeries(row)];
    switch (valueColumn) {
        case RidgelineValue::Median: return s.median;
        case RidgelineValue::Count: return static_cast<double>(s.count);
        case RidgelineValue::Peak: return s.peakAt;
        default: return s.mean;
    }
}

std::string UltraCanvasRidgelineChart::RowValueText(size_t row) const {
    if (row >= statistics.size() || statistics[row].empty() || valueColumn == RidgelineValue::Hidden) return std::string();
    const RidgelineRowStatistics& s = statistics[row][FirstSeries(row)];
    if (!s.valid) return std::string();
    // Mean, median and peak are x values: through the x formatter when the
    // axis has one (a time of day), otherwise to `valueDecimals`.
    switch (valueColumn) {
        case RidgelineValue::Count: return std::to_string(s.count);
        case RidgelineValue::Peak: return FormatX(s.peakAt);
        default: return xFormatter ? xFormatter(RowValue(row)) : Fixed(RowValue(row), valueDecimals);
    }
}

std::string UltraCanvasRidgelineChart::FormatX(double x) const {
    if (xFormatter) return xFormatter(x);
    const double span = xRange.second - xRange.first;
    return Fixed(x, span < 20.0 ? 2 : (span < 200.0 ? 1 : 0));
}

std::string UltraCanvasRidgelineChart::DescribeRow(size_t row, double x) const {
    if (row >= model.rows.size()) return std::string();
    const size_t seriesCount = SeriesCount();
    std::string text = model.rows[row].name + "\n" + FormatX(x) + ":";
    if (seriesCount == 1) {
        text += " " + model.curveName + " " + Short(rawCurves[row].empty() ? 0.0 : HeightAt(rawCurves[row][0], x));
    }
    for (size_t s = 0; s < seriesCount; ++s) {
        if (!IsSeriesVisible(s) || !model.rows[row].HasDataOf(s)) continue;
        const RidgelineRowStatistics& st = statistics[row][s];
        std::string line = "\n";
        if (seriesCount > 1) {
            line += (s < model.series.size() && !model.series[s].name.empty() ? model.series[s].name
                                                                             : "Series " + std::to_string(s + 1)) +
                    ": " + model.curveName + " " + Short(HeightAt(rawCurves[row][s], x)) + "; ";
        }
        if (st.valid && st.count > 0) {
            line += std::to_string(st.count) + " values, mean " + FormatX(st.mean) + ", median " + FormatX(st.median);
        } else if (st.valid) {
            line += "peak at " + FormatX(st.peakAt) + ", middle half " + FormatX(st.quartile1) + " to " + FormatX(st.quartile3);
        }
        if (seriesCount == 1 && line.size() > 1) line[1] = static_cast<char>(std::toupper(static_cast<unsigned char>(line[1])));
        text += line;
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

double UltraCanvasRidgelineChart::RowHeightAt(size_t row, double x, size_t series) const {
    if (row >= curves.size() || series >= curves[row].size()) return 0.0;
    return HeightAt(curves[row][series], x) * overlap * RowPitch();
}

void UltraCanvasRidgelineChart::OnHitRegionHoverChanged(int64_t regionId) {
    // Every strip is its own region, so this runs as the pointer moves along
    // a row as well as between rows.
    const int row = regionId >= 0 ? static_cast<int>(regionId / kStrips) : -1;
    if (row >= 0) {
        const int strip = static_cast<int>(regionId % kStrips);
        hoveredX = xRange.first + (xRange.second - xRange.first) * (strip + 0.5) / kStrips;
    }
    const bool rowChanged = row != hoveredRow;
    hoveredRow = row;
    if (rowChanged && highlightOnHover) RequestRedraw();
    if (onRowHover) onRowHover(row);
}

bool UltraCanvasRidgelineChart::OnEvent(const UCEvent& event) {
    // A click: pressed and released on the same row.
    if (event.type == UCEventType::MouseDown && event.button == UCMouseButton::Left) {
        pressedRow = hoveredRow;
    } else if (event.type == UCEventType::MouseUp && event.button == UCMouseButton::Left) {
        const int row = pressedRow;
        pressedRow = -1;
        if (row >= 0 && row == hoveredRow && onRowClick) {
            onRowClick(row);
            return true;
        }
    }
    return UltraCanvasChartEngineElement::OnEvent(event);
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
    const size_t seriesCount = SeriesCount();
    auto at = [&](double xv, double v) { return projection.ToScreen(ChartNormalizedPoint(x.Normalize(xv), v)); };

    // From the top row down: each row's fill covers the outline of the row
    // behind it, and its hit strips are added after (last added wins).
    for (size_t i = 0; i < curves.size(); ++i) {
        const double base = RowBaseline(i);
        // While a row is hovered, the others fade.
        const float fade = (highlightOnHover && hoveredRow >= 0 && static_cast<int>(i) != hoveredRow) ? 0.3f : 1.0f;
        if (showRowLines) {
            ctx->SetStrokePaint(GetTheme().gridColor);
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawLine(projection.ToScreen(ChartNormalizedPoint(0.0, base)),
                          projection.ToScreen(ChartNormalizedPoint(1.0, base)));
        }

        for (size_t s = 0; s < seriesCount; ++s) {
            if (s >= curves[i].size() || !IsSeriesVisible(s)) continue;
            const std::vector<RidgelineCurvePoint>& curve = curves[i][s];
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
            // A density is evaluated densely and a histogram is steps already;
            // a supplied curve may be coarse, so it is joined smoothly
            // (monotone: never above its points).
            const bool supplied = !model.rows[i].HasValuesOf(s);
            options.shape = supplied ? ChartLineShape::Curved : ChartLineShape::Straight;
            options.samplesPerSegment = supplied ? 6 : 1;

            const Color color = RowColor(i, s);
            const float alpha = fillOpacity * fade;
            double peak = 0.0;
            for (const RidgelineCurvePoint& p : curve) peak = std::max(peak, p.y);
            for (const std::vector<Point2Dd>& outline : BuildAreaOutlines(projection, top, bottom, options)) {
                if (outline.size() < 3) continue;
                if (gradientFill) {
                    const Point2Dd from = projection.ToScreen(ChartNormalizedPoint(0.5, base + amplitude * peak));
                    const Point2Dd to = projection.ToScreen(ChartNormalizedPoint(0.5, base));
                    ctx->SetFillPaint(ctx->CreateLinearGradientPattern(
                        from.x, from.y, to.x, to.y,
                        {GradientStop(0.0, color.WithAlpha(static_cast<uint8_t>(255.0f * alpha))),
                         GradientStop(1.0, color.WithAlpha(static_cast<uint8_t>(255.0f * alpha * 0.35f)))}));
                } else {
                    ctx->SetFillPaint(color.WithAlpha(static_cast<uint8_t>(255.0f * alpha)));
                }
                ctx->FillLinePath(outline);
            }

            const RidgelineRowStatistics& st = statistics[i][s];
            const Color dark = color.Blend(Color(0, 0, 0, 255), 0.35f);

            // The middle half (first to third quartile), shaded darker.
            if (quantileBands && st.valid && st.quartile3 > st.quartile1) {
                std::vector<Point2Dd> band;
                band.push_back(at(st.quartile1, base));
                band.push_back(at(st.quartile1, base + amplitude * HeightAt(curve, st.quartile1)));
                for (const RidgelineCurvePoint& p : curve) {
                    if (p.x > st.quartile1 && p.x < st.quartile3) band.push_back(at(p.x, base + amplitude * p.y));
                }
                band.push_back(at(st.quartile3, base + amplitude * HeightAt(curve, st.quartile3)));
                band.push_back(at(st.quartile3, base));
                ctx->SetFillPaint(dark.WithAlpha(static_cast<uint8_t>(255.0f * alpha * 0.45f)));
                ctx->FillLinePath(band);
            }

            if (outlineWidth > 0.0f) {
                const Color stroke = outlineColor.a > 0 ? outlineColor : dark;
                ctx->SetStrokePaint(stroke.WithAlpha(static_cast<uint8_t>(stroke.a * fade)));
                ctx->SetStrokeWidth(static_cast<int>(i) == hoveredRow ? outlineWidth + 1.0f : outlineWidth);
                for (const ChartLineRun& run : BuildLineRuns(projection, top, options)) {
                    if (run.points.size() >= 2) ctx->DrawLinePath(run.points, false);
                }
            }

            // Marks: a rule from the baseline up to the curve.
            if (marks != 0 && st.valid) {
                auto mark = [&](double xv, float width) {
                    if (xv < x0 || xv > x1) return;
                    ctx->SetStrokePaint(dark.WithAlpha(static_cast<uint8_t>(230.0f * fade)));
                    ctx->SetStrokeWidth(width);
                    ctx->DrawLine(at(xv, base), at(xv, base + amplitude * HeightAt(curve, xv)));
                };
                if (marks & RidgelineMarkMean) mark(st.mean, 1.6f);
                if (marks & RidgelineMarkMedian) mark(st.median, 1.6f);
                if (marks & RidgelineMarkQuartiles) {
                    mark(st.quartile1, 1.0f);
                    mark(st.quartile3, 1.0f);
                }
            }
        }

        // Hit strips: the area under the row's tallest shown curve, cut into
        // vertical strips so the tooltip can say where along the row the
        // pointer is.
        for (int strip = 0; strip < kStrips; ++strip) {
            const double a = x0 + (x1 - x0) * strip / kStrips, b = x0 + (x1 - x0) * (strip + 1) / kStrips;
            auto height = [&](double xv) {
                double h = 0.0;
                for (size_t s = 0; s < seriesCount && s < curves[i].size(); ++s) {
                    if (IsSeriesVisible(s)) h = std::max(h, HeightAt(curves[i][s], xv));
                }
                return h;
            };
            std::vector<double> xs = {a};
            for (size_t s = 0; s < seriesCount && s < curves[i].size(); ++s) {
                if (!IsSeriesVisible(s)) continue;
                for (const RidgelineCurvePoint& p : curves[i][s]) if (p.x > a && p.x < b) xs.push_back(p.x);
            }
            xs.push_back(b);
            std::sort(xs.begin(), xs.end());
            std::vector<Point2Dd> polygon;
            double tallest = 0.0;
            for (double xv : xs) {
                const double h = height(xv);
                tallest = std::max(tallest, h);
                polygon.push_back(at(xv, base + amplitude * h));
            }
            if (tallest < 0.01) continue;
            polygon.push_back(at(b, base));
            polygon.push_back(at(a, base));
            AddHitRegion(polygon, static_cast<int64_t>(i) * kStrips + strip, DescribeRow(i, (a + b) * 0.5));
        }
    }
}

void UltraCanvasRidgelineChart::RenderInteractionOverlay(IRenderContext* ctx, const ChartEngineFrame& frame) {
    if (!frame.projection || !frame.axes || frame.axes->Count() < 2 || model.rows.empty()) return;
    const IChartProjection& projection = *frame.projection;
    const Rect2Dd& plot = frame.plotArea;

    // The pointer's x on the hovered row: a rule from the baseline to the
    // tallest shown curve.
    if (hoveredRow >= 0 && static_cast<size_t>(hoveredRow) < curves.size()) {
        const double u = frame.axes->At(1).Normalize(hoveredX);
        const double base = RowBaseline(static_cast<size_t>(hoveredRow));
        double h = 0.0;
        for (size_t s = 0; s < SeriesCount(); ++s) {
            if (IsSeriesVisible(s)) h = std::max(h, RowHeightAt(static_cast<size_t>(hoveredRow), hoveredX, s));
        }
        ctx->SetStrokePaint(GetTheme().axisLineColor.WithAlpha(170));
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawLine(projection.ToScreen(ChartNormalizedPoint(u, base)),
                      projection.ToScreen(ChartNormalizedPoint(u, base + h)));
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
