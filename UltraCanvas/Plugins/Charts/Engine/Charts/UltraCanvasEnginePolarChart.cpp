// Plugins/Charts/Engine/Charts/UltraCanvasEnginePolarChart.cpp
// Polar chart on the chart engine.
// Version: 1.0.0
// Last Modified: 2026-10-20
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasEnginePolarChart.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace UltraCanvas {

namespace {

bool IsColumn(const EnginePolarSeries& s) { return s.type == EnginePolarSeriesType::Column; }
bool IsArea(const EnginePolarSeries& s) {
    return s.type == EnginePolarSeriesType::Area || s.type == EnginePolarSeriesType::SplineArea;
}

} // namespace

UltraCanvasEnginePolarChart::UltraCanvasEnginePolarChart(const std::string& id, int x, int y,
                                                         int w, int h)
    : UltraCanvasChartEngineElement(id, x, y, w, h) {
    SetProjectionKind(ChartProjectionKind::Polar);
    SetPolarAngles(zeroAngle, sweepAngle, clockwiseSweep);
    SetAnimateOnDataChange(true, 0.8f);     // the series grow out from the centre
    SetEnableTooltips(true);
    SetLegendPosition(ChartLegendPosition::TopCenter);
}

// =============================================================================
// SERIES
// =============================================================================

void UltraCanvasEnginePolarChart::AddSeries(const EnginePolarSeries& series) {
    seriesList.push_back(series);
    RefreshLegend();
    RebuildHighlights();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEnginePolarChart::AddSeries(
        const std::string& name, EnginePolarSeriesType type,
        const std::vector<std::pair<double, double>>& angleRadiusPairs, const Color& color) {
    EnginePolarSeries series;
    series.name = name;
    series.type = type;
    series.color = color;
    for (const auto& [angle, radius] : angleRadiusPairs) series.points.emplace_back(angle, radius);
    AddSeries(series);
}

void UltraCanvasEnginePolarChart::AddCategorySeries(const std::string& name,
                                                    EnginePolarSeriesType type,
                                                    const std::vector<double>& values,
                                                    const Color& color) {
    EnginePolarSeries series;
    series.name = name;
    series.type = type;
    series.color = color;
    for (double v : values) series.points.emplace_back(0.0, v);
    angleMode = EnginePolarAngleMode::Categorical;
    AddSeries(series);
}

void UltraCanvasEnginePolarChart::ClearSeries() {
    seriesList.clear();
    RefreshLegend();
    RebuildHighlights();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEnginePolarChart::SetSeriesType(size_t index, EnginePolarSeriesType type) {
    if (index >= seriesList.size()) return;
    seriesList[index].type = type;
    RefreshLegend();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEnginePolarChart::SetSeriesVisible(size_t index, bool visible) {
    if (index >= seriesList.size()) return;
    seriesList[index].visible = visible;
    RefreshLegend();
    RebuildHighlights();
    MarkEngineDirty(ChartDirty::Data);
}

// =============================================================================
// AXES AND GRID CONFIGURATION
// =============================================================================

void UltraCanvasEnginePolarChart::SetAngleMode(EnginePolarAngleMode mode) {
    angleMode = mode;
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEnginePolarChart::SetCategories(const std::vector<std::string>& newCategories) {
    categories = newCategories;
    angleMode = EnginePolarAngleMode::Categorical;
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEnginePolarChart::SetCategoryPlacement(EnginePolarCategoryPlacement placement) {
    categoryPlacement = placement;
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEnginePolarChart::SetAngleRange(double minAngle, double maxAngle) {
    if (!(maxAngle > minAngle)) return;
    angleMin = minAngle;
    angleMax = maxAngle;
    RebuildHighlights();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEnginePolarChart::SetZeroAngle(double degrees) {
    zeroAngle = degrees;
    SetPolarAngles(zeroAngle, sweepAngle, clockwiseSweep);
}

void UltraCanvasEnginePolarChart::SetClockwise(bool clockwise) {
    clockwiseSweep = clockwise;
    SetPolarAngles(zeroAngle, sweepAngle, clockwiseSweep);
}

void UltraCanvasEnginePolarChart::SetSweepAngle(double degrees) {
    sweepAngle = std::clamp(degrees, 10.0, 360.0);
    SetPolarAngles(zeroAngle, sweepAngle, clockwiseSweep);
    MarkEngineDirty(ChartDirty::Data);      // closing and cyclic slots depend on it
}

void UltraCanvasEnginePolarChart::SetAngleTickInterval(double degrees) {
    angleTickInterval = std::max(0.0, degrees);
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEnginePolarChart::SetAngleLabelFormatter(
        std::function<std::string(double)> formatter) {
    angleFormatter = std::move(formatter);
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEnginePolarChart::SetShowAngleLabels(bool show) {
    showAngleLabels = show;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEnginePolarChart::SetRadialScale(EnginePolarRadialScale scale) {
    radialScale = scale;
    RebuildHighlights();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEnginePolarChart::SetRadialRange(double minValue, double maxValue) {
    if (!(maxValue > minValue)) return;
    manualRadialRange = true;
    radialMin = minValue;
    radialMax = maxValue;
    RebuildHighlights();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEnginePolarChart::ClearRadialRange() {
    manualRadialRange = false;
    RebuildHighlights();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEnginePolarChart::SetRadialTickInterval(double interval) {
    radialTickInterval = std::max(0.0, interval);
    RebuildHighlights();
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEnginePolarChart::SetRadialTickCount(int count) {
    radialTickCount = std::clamp(count, 2, 20);
    targetTickCount = radialTickCount;      // the engine's ticks follow it
    RebuildHighlights();
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEnginePolarChart::SetRadialAxisReversed(bool reversed) {
    radialReversed = reversed;
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEnginePolarChart::SetInnerRadiusFraction(float fraction) {
    SetPolarInnerRadius(std::clamp(fraction, 0.0f, 0.9f));
}

void UltraCanvasEnginePolarChart::SetShowRadialLabels(bool show) {
    showRadialLabels = show;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEnginePolarChart::SetRadialLabelUnit(const std::string& unit) {
    radialUnit = unit;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEnginePolarChart::SetShowRadialGrid(bool show) {
    showRadialGrid = show;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEnginePolarChart::SetShowAngularGrid(bool show) {
    showAngularGrid = show;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEnginePolarChart::SetAlternatingBands(bool enabled, const Color& colorA,
                                                      const Color& colorB) {
    alternatingBands = enabled;
    alternatingA = colorA;
    alternatingB = colorB;
    RebuildHighlights();
}

void UltraCanvasEnginePolarChart::AddRadialBand(double from, double to, const Color& color,
                                                const std::string& label) {
    radialBands.push_back({from, to, color, label});
    RebuildHighlights();
}

void UltraCanvasEnginePolarChart::ClearRadialBands() {
    radialBands.clear();
    RebuildHighlights();
}

void UltraCanvasEnginePolarChart::AddAngularBand(double fromAngle, double toAngle,
                                                 const Color& color, const std::string& label) {
    angularBands.push_back({fromAngle, toAngle, color, label});
    RebuildHighlights();
}

void UltraCanvasEnginePolarChart::ClearAngularBands() {
    angularBands.clear();
    RebuildHighlights();
}

void UltraCanvasEnginePolarChart::SetStackMode(EnginePolarStackMode mode) {
    stackMode = mode;
    RebuildHighlights();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEnginePolarChart::SetColumnWidthFraction(float fraction) {
    columnWidthFraction = std::clamp(fraction, 0.05f, 1.0f);
    RequestRedraw();
}

void UltraCanvasEnginePolarChart::SetColumnBorder(const Color& color, float width) {
    columnBorderColor = color;
    columnBorderWidth = std::max(0.0f, width);
    RequestRedraw();
}

void UltraCanvasEnginePolarChart::SetUseThemePalette(bool use) {
    useThemePalette = use;
    RefreshLegend();
    RequestRedraw();
}

// =============================================================================
// HELPERS
// =============================================================================

size_t UltraCanvasEnginePolarChart::CategoryCount() const {
    size_t count = categories.size();
    for (const EnginePolarSeries& s : seriesList) count = std::max(count, s.points.size());
    return count;
}

bool UltraCanvasEnginePolarChart::Stacks(const EnginePolarSeries& series) const {
    return Categorical() && stackMode != EnginePolarStackMode::NoStacking && series.visible &&
           (IsColumn(series) || IsArea(series));
}

Color UltraCanvasEnginePolarChart::SeriesColor(size_t index) const {
    const Color& own = seriesList[index].color;
    if (!useThemePalette && own.a > 0) return own.WithAlpha(255);
    return Palette().ColorAt(index, seriesList.size());
}

std::string UltraCanvasEnginePolarChart::FormatAngle(const EnginePolarSeries& series,
                                                     size_t pointIndex) const {
    if (Categorical()) {
        if (pointIndex < categories.size()) return categories[pointIndex];
        return std::to_string(pointIndex + 1);
    }
    const ChartAxis angle = BuildAngleAxis();
    return angle.FormatValue(series.points[pointIndex].angle);
}

ChartAxis UltraCanvasEnginePolarChart::BuildRadialAxis() const {
    ChartAxis radial("radius");
    radial.side = ChartAxisSide::Left;
    radial.visible = showRadialLabels;
    radial.inverted = radialReversed;
    radial.unitSuffix = radialUnit;
    switch (radialScale) {
        case EnginePolarRadialScale::Logarithmic: radial.scale = ChartScale::Log; break;
        case EnginePolarRadialScale::SquareRoot:  radial.scale = ChartScale::Sqrt; break;
        case EnginePolarRadialScale::Linear:      radial.scale = ChartScale::Linear; break;
    }

    if (manualRadialRange) {
        double lo = radialMin, hi = radialMax;
        if (radial.scale == ChartScale::Log) {
            lo = std::max(lo, std::max(hi * 1e-4, 1e-9));
        }
        radial.SetRange(lo, hi);
    } else if (Categorical() && stackMode == EnginePolarStackMode::PercentStacked) {
        radial.SetRange(0.0, 100.0);
    } else {
        // Every plotted radius, the stack totals for stacked series, and zero
        // (radii grow from it) on the linear scales.
        std::vector<std::vector<double>> stacked;
        double minPositive = std::numeric_limits<double>::max(), most = 0.0;
        for (const EnginePolarSeries& s : seriesList) {
            if (!s.visible) continue;
            std::vector<double> values;
            for (const EnginePolarPoint& p : s.points) {
                values.push_back(p.radius);
                if (p.radius > 0.0) minPositive = std::min(minPositive, p.radius);
                most = std::max(most, p.radius);
            }
            if (Stacks(s)) stacked.push_back(values);
            else radial.Observe(values);
        }
        if (!stacked.empty()) {
            ChartBarLayoutOptions options;
            options.arrangement = ChartBarArrangement::Stacked;
            ObserveBarSeries(radial, stacked, options);
            const ChartStackedValues totals = StackSeriesValues(stacked, ChartBarArrangement::Stacked);
            for (const auto& top : totals.top) {
                for (double v : top) if (std::isfinite(v)) most = std::max(most, v);
            }
        }
        if (radial.scale == ChartScale::Log) {
            // Whole decades: a log axis has no zero to grow from.
            if (minPositive > most) minPositive = 1.0;
            radial.SetRange(std::pow(10.0, std::floor(std::log10(minPositive))),
                            std::pow(10.0, std::ceil(std::log10(std::max(most, minPositive * 10.0)))));
        } else {
            radial.Observe(0.0);
        }
    }

    if (radialTickInterval > 0.0 && radial.scale != ChartScale::Log) {
        radial.Finalize();
        const double lo = radial.Min(), hi = radial.Max();
        for (double v = std::ceil(lo / radialTickInterval - 1e-9) * radialTickInterval;
             v <= hi + radialTickInterval * 1e-6; v += radialTickInterval) {
            radial.tickValues.push_back(std::abs(v) < radialTickInterval * 1e-9 ? 0.0 : v);
        }
    }
    return radial;
}

ChartAxis UltraCanvasEnginePolarChart::BuildAngleAxis() const {
    ChartAxis angle("angle");
    angle.side = ChartAxisSide::Bottom;
    angle.visible = showAngleLabels;
    if (Categorical()) {
        angle.scale = ChartScale::Category;
        angle.categories = categories;
        for (size_t i = angle.categories.size(); i < CategoryCount(); ++i) {
            angle.categories.push_back(std::to_string(i + 1));
        }
        if (categoryPlacement == EnginePolarCategoryPlacement::BetweenSpokes) {
            angle.categoryPadding = 0.5;           // categories centred in their slots
        } else {
            angle.cyclic = FullCircle();           // the last spoke stops short of the first
        }
        return angle;
    }

    angle.SetRange(angleMin, angleMax);
    double interval = angleTickInterval;
    if (interval <= 0.0) {
        // About a dozen spokes, on a round number of degrees.
        const double raw = (angleMax - angleMin) / 12.0;
        for (double nice : {1.0, 2.0, 5.0, 10.0, 15.0, 20.0, 30.0, 45.0, 60.0, 90.0}) {
            interval = nice;
            if (nice >= raw) break;
        }
    }
    for (double a = angleMin; a <= angleMax + interval * 1e-6; a += interval) {
        angle.tickValues.push_back(a);
    }
    std::function<std::string(double)> custom = angleFormatter;
    angle.formatter = [custom](double degrees) {
        if (custom) return custom(degrees);
        ChartAxis plain;
        plain.decimals = (std::abs(degrees - std::round(degrees)) < 1e-9) ? 0 : 1;
        return plain.FormatValue(degrees) + "°";
    };
    return angle;
}

void UltraCanvasEnginePolarChart::RebuildHighlights() {
    ClearHighlights();
    if (alternatingBands) {
        // Shade every other ring between the radial ticks.
        ChartAxis radial = BuildRadialAxis();
        radial.Finalize();
        const std::vector<ChartTick> ticks = radial.GenerateTicks(radialTickCount);
        for (size_t i = 0; i + 1 < ticks.size(); ++i) {
            ChartHighlight band;
            band.shape = ChartHighlightShape::ValueBand;
            band.axisIndex = 0;
            band.bandLow = ticks[i].value;
            band.bandHigh = ticks[i + 1].value;
            band.fill = (i % 2 == 0) ? alternatingA : alternatingB;
            band.strokeWidth = 0.0f;
            AddHighlight(band);
        }
    }
    for (const EnginePolarBand& b : radialBands) {
        ChartHighlight band;
        band.shape = ChartHighlightShape::ValueBand;
        band.axisIndex = 0;                         // a ring under Polar
        band.bandLow = b.from;
        band.bandHigh = b.to;
        band.fill = b.color;
        band.strokeWidth = 0.0f;
        band.label = b.label;
        AddHighlight(band);
    }
    for (const EnginePolarBand& b : angularBands) {
        ChartHighlight band;
        band.shape = ChartHighlightShape::ValueBand;
        band.axisIndex = 1;                         // a wedge under Polar
        band.bandLow = b.from;
        band.bandHigh = b.to;
        band.fill = b.color;
        band.strokeWidth = 0.0f;
        band.label = b.label;
        AddHighlight(band);
    }
}

void UltraCanvasEnginePolarChart::RefreshLegend() {
    std::vector<ChartLegendEntry> entries;
    for (size_t i = 0; i < seriesList.size(); ++i) {
        const EnginePolarSeries& s = seriesList[i];
        LegendSwatch swatch = LegendSwatch::Line;
        if (s.type == EnginePolarSeriesType::Scatter) swatch = LegendSwatch::Circle;
        else if (IsColumn(s) || IsArea(s)) swatch = LegendSwatch::Square;
        ChartLegendEntry entry(s.name, SeriesColor(i), swatch);
        entry.enabled = s.visible;
        entries.push_back(entry);
    }
    SetLegendEntries(entries);
    SetShowLegend(seriesList.size() > 1);
}

void UltraCanvasEnginePolarChart::OnChartThemeChanged() { RefreshLegend(); }

void UltraCanvasEnginePolarChart::OnLegendEntryToggled(size_t entryIndex, bool enabled) {
    if (entryIndex >= seriesList.size()) return;
    seriesList[entryIndex].visible = enabled;
    MarkEngineDirty(ChartDirty::Data);      // the range and the stacks change
}

std::vector<std::vector<UltraCanvasEnginePolarChart::PlacedPoint>>
UltraCanvasEnginePolarChart::PlaceSeries(const ChartAxis& radial, const ChartAxis& angle) const {
    std::vector<std::vector<PlacedPoint>> placed(seriesList.size());

    // The floor the radii grow from: zero when it is on the axis.
    double floorValue = radial.Min();
    if (radial.scale != ChartScale::Log && radial.Min() <= 0.0 && radial.Max() >= 0.0) {
        floorValue = 0.0;
    }
    const double floorV = std::clamp(radial.Normalize(floorValue), 0.0, 1.0);

    // Stacked series accumulate per category, in series order.
    std::vector<size_t> members;
    std::vector<std::vector<double>> stackInput;
    for (size_t s = 0; s < seriesList.size(); ++s) {
        if (!Stacks(seriesList[s])) continue;
        members.push_back(s);
        std::vector<double> values;
        for (const EnginePolarPoint& p : seriesList[s].points) values.push_back(p.radius);
        stackInput.push_back(values);
    }
    const ChartBarArrangement arrangement = stackMode == EnginePolarStackMode::PercentStacked
                                                ? ChartBarArrangement::PercentStacked
                                                : ChartBarArrangement::Stacked;
    const ChartStackedValues stacked = StackSeriesValues(stackInput, arrangement);

    for (size_t s = 0; s < seriesList.size(); ++s) {
        const EnginePolarSeries& series = seriesList[s];
        if (!series.visible) continue;
        const auto member = std::find(members.begin(), members.end(), s);
        const size_t stackRow = static_cast<size_t>(member - members.begin());
        for (size_t i = 0; i < series.points.size(); ++i) {
            const EnginePolarPoint& p = series.points[i];
            if (!std::isfinite(p.radius)) continue;
            PlacedPoint point;
            point.pointIndex = i;
            point.radius = p.radius;
            point.u = angle.Normalize(Categorical() ? static_cast<double>(i) : p.angle);
            if (member != members.end() && i < stacked.top[stackRow].size()) {
                point.base = radial.Normalize(stacked.base[stackRow][i]);
                point.v = radial.Normalize(stacked.top[stackRow][i]);
            } else {
                point.base = floorV;
                point.v = radial.Normalize(p.radius);
            }
            placed[s].push_back(point);
        }
    }
    return placed;
}

// =============================================================================
// ENGINE CONTRACT
// =============================================================================

void UltraCanvasEnginePolarChart::DescribeAxes(ChartAxisSet& axes) {
    axes.Add(BuildRadialAxis());   // index 0: the radius - the radial rule
    const ChartAxis angle = BuildAngleAxis();
    axes.Add(angle);               // index 1: the angle - the rim labels
    size_t spokes = 1;
    if (Categorical() && categoryPlacement == EnginePolarCategoryPlacement::BetweenSpokes) {
        // Index 2: the slot boundaries. Not drawn; the spokes (and a polygon
        // web) follow its ticks, so the categories sit between the spokes.
        ChartAxis boundaries = angle;
        boundaries.name = "slots";
        boundaries.visible = false;
        for (size_t i = 0; i <= CategoryCount(); ++i) {
            boundaries.tickValues.push_back(static_cast<double>(i) - 0.5);
        }
        spokes = axes.Add(boundaries);
    }
    SetGridAxis(showRadialGrid ? 0 : static_cast<size_t>(-1));
    SetDomainGridAxis(showAngularGrid ? spokes : static_cast<size_t>(-1));
}

void UltraCanvasEnginePolarChart::RenderChartContent(IRenderContext* ctx,
                                                     const ChartEngineFrame& frame) {
    if (!frame.axes || frame.axes->Count() < 2) return;
    const ChartAxis& radial = frame.axes->At(0);
    const ChartAxis& angle = frame.axes->At(1);
    const std::vector<std::vector<PlacedPoint>> placed = PlaceSeries(radial, angle);
    const double progress = frame.animationProgress;
    const Color plotColor = frame.theme ? frame.theme->plotAreaColor : Colors::White;
    const int64_t hovered = HoveredRegionId();

    // Column slot widths: the category slot, or the tightest angular spacing.
    double slot = 1.0 / 12.0;
    if (Categorical() && CategoryCount() > 1) {
        slot = std::abs(angle.Normalize(1.0) - angle.Normalize(0.0));
    }
    size_t columnSeries = 0;
    for (const EnginePolarSeries& s : seriesList) {
        if (s.visible && IsColumn(s) && !Stacks(s)) ++columnSeries;
    }
    size_t columnOrdinal = 0;

    ctx->PushState();
    ctx->SetLineJoin(LineJoin::Round);
    ctx->SetLineCap(LineCap::Round);

    for (size_t s = 0; s < seriesList.size(); ++s) {
        const EnginePolarSeries& series = seriesList[s];
        if (!series.visible || placed[s].empty()) continue;
        const Color color = SeriesColor(s);
        const std::vector<PlacedPoint>& points = placed[s];
        auto grow = [&](double base, double v) { return base + (v - base) * progress; };

        // ---- columns (polar columns / the Nightingale rose) ----------------
        if (IsColumn(series)) {
            double width = slot * columnWidthFraction;
            double offset = 0.0;
            if (!Categorical() && points.size() > 1) {
                double tightest = 1.0;
                for (size_t i = 1; i < points.size(); ++i) {
                    const double gap = std::abs(points[i].u - points[i - 1].u);
                    if (gap > 1e-9) tightest = std::min(tightest, gap);
                }
                width = tightest * columnWidthFraction;
            }
            if (!Stacks(series) && columnSeries > 1) {
                // Grouped: the slot is shared side by side.
                const double each = width / static_cast<double>(columnSeries);
                offset = -width * 0.5 + each * (static_cast<double>(columnOrdinal) + 0.5);
                width = each;
                ++columnOrdinal;
            }
            for (const PlacedPoint& p : points) {
                const double u = p.u + offset;
                const std::vector<Point2Dd> outline = BuildBarOutline(
                    *frame.projection, u - width * 0.5, p.base, u + width * 0.5,
                    grow(p.base, p.v), 8);
                if (outline.size() < 3) continue;
                const int64_t id = RegionId(s, p.pointIndex);
                const Color& own = series.points[p.pointIndex].color;
                Color fill = (!useThemePalette && own.a > 0) ? own : color;
                if (hovered == id) fill = fill.Lighten(0.2f);
                ctx->SetFillPaint(fill);
                ctx->FillLinePath(outline);
                if (columnBorderWidth > 0.0f && columnBorderColor.a > 0) {
                    ctx->SetStrokePaint(columnBorderColor);
                    ctx->SetStrokeWidth(columnBorderWidth);
                    ctx->DrawLinePath(outline, true);
                }
                AddHitRegion(outline, id, series.name + "\n" + FormatAngle(series, p.pointIndex) +
                                              ": " + radial.FormatValue(p.radius));
            }
            continue;
        }

        // ---- lines, splines, areas, scatter --------------------------------
        std::vector<ChartNormalizedPoint> top, base;
        for (const PlacedPoint& p : points) {
            top.emplace_back(p.u, grow(p.base, p.v));
            base.emplace_back(p.u, p.base);
        }

        const bool spline = series.type == EnginePolarSeriesType::Spline ||
                            series.type == EnginePolarSeriesType::SplineArea;
        ChartLineOptions options;
        options.shape = spline ? ChartLineShape::Curved : ChartLineShape::Straight;
        // Straight segments are chords between the projected points; splines
        // are sampled in normalised space so they curve with the rings.
        options.samplesPerSegment = spline ? 24 : 1;
        // A closed series on a full circle joins its last point back to the
        // first, one turn on (the engine's closed loops).
        options.closed = series.closed && FullCircle();

        if (IsArea(series)) {
            ctx->SetFillPaint(color.WithAlpha(static_cast<uint8_t>(
                std::clamp(series.fillOpacity, 0.0f, 1.0f) * 255.0f)));
            for (const std::vector<Point2Dd>& outline :
                 BuildAreaOutlines(*frame.projection, top, base, options)) {
                ctx->FillLinePath(outline);
            }
        }

        if (series.type != EnginePolarSeriesType::Scatter && series.lineWidth > 0.0f) {
            const bool emphasised = hovered >= 0 && static_cast<size_t>(hovered / 65536) == s;
            ctx->SetStrokePaint(color);
            ctx->SetStrokeWidth(series.lineWidth + (emphasised ? 1.0f : 0.0f));
            if (series.dashed) ctx->SetLineDash(UCDashPattern({6.0, 4.0}));
            for (const ChartLineRun& run : BuildLineRuns(*frame.projection, top, options)) {
                if (run.points.size() >= 2) ctx->DrawLinePath(run.points, false);
            }
            if (series.dashed) ctx->SetLineDash(UCDashPattern());
        }

        // Markers and the hit regions sit on the data points themselves.
        const bool markers = series.marker != ChartMarkerShape::NoMarker &&
                             (series.type == EnginePolarSeriesType::Scatter || series.markerSize > 0.0f);
        for (size_t k = 0; k < points.size(); ++k) {
            const PlacedPoint& p = points[k];
            const Point2Dd at = frame.projection->ToScreen(ChartNormalizedPoint(p.u, top[k].v));
            const int64_t id = RegionId(s, p.pointIndex);
            if (markers) {
                const Color& own = series.points[p.pointIndex].color;
                const Color fill = (!useThemePalette && own.a > 0) ? own : color;
                DrawChartMarker(ctx, series.marker, at,
                                series.markerSize * (hovered == id ? 1.4 : 1.0) * 0.5 + 1.0, fill,
                                plotColor, 1.0f);
            }
            const double r = std::max(5.0, series.markerSize * 0.5 + 3.0);
            AddHitRegion(Rect2Dd(at.x - r, at.y - r, r * 2.0, r * 2.0), id,
                         series.name + "\n" + FormatAngle(series, p.pointIndex) + ": " +
                             radial.FormatValue(p.radius));
        }
    }
    ctx->PopState();
}

void UltraCanvasEnginePolarChart::CollectChartLabels(IRenderContext* ctx,
                                                     ChartLabelBroker& broker) {
    if (!Frame().axes || Frame().axes->Count() < 2 || !Frame().projection) return;
    const std::vector<std::vector<PlacedPoint>> placed =
        PlaceSeries(Frame().axes->At(0), Frame().axes->At(1));
    ctx->SetFontSize(axisFontSize);
    for (size_t s = 0; s < seriesList.size(); ++s) {
        if (!seriesList[s].visible || !seriesList[s].showValueLabels) continue;
        for (const PlacedPoint& p : placed[s]) {
            ChartLabelRequest label;
            label.text = Frame().axes->At(0).FormatValue(p.radius);
            label.klass = ChartLabelClass::ValueLabel;
            label.anchor = Frame().projection->ToScreen(ChartNormalizedPoint(p.u, p.v));
            label.anchorRadius = seriesList[s].markerSize * 0.5 + 1.0;
            const Size2Di size = ctx->GetTextLineDimensions(label.text);
            label.textSize = Size2Dd(size.width, size.height);
            label.allowSuppress = true;
            label.userData = static_cast<size_t>(RegionId(s, p.pointIndex));
            broker.Add(label);
        }
    }
}

} // namespace UltraCanvas
