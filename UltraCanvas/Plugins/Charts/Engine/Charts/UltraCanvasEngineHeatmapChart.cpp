// Plugins/Charts/Engine/Charts/UltraCanvasEngineHeatmapChart.cpp
// Heatmap / matrix chart on the chart engine.
// Version: 1.0.0
// Last Modified: 2026-10-20
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineHeatmapChart.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace UltraCanvas {

namespace {

// Black or white, whichever reads better on the cell colour.
Color ContrastText(const Color& background) {
    const double luminance = 0.299 * background.r + 0.587 * background.g + 0.114 * background.b;
    return luminance > 150.0 ? Color(20, 20, 20, 255) : Color(255, 255, 255, 255);
}

} // namespace

UltraCanvasEngineHeatmapChart::UltraCanvasEngineHeatmapChart(
        const std::string& id, int x, int y, int w, int h)
    : UltraCanvasChartEngineElement(id, x, y, w, h) {
    enableTooltips = true;
    // Row and column labels sit against the cell grid.
    SetLabelPolicy(ChartLabelPolicy::Heatmap());
    SetGridAxis(static_cast<size_t>(-1));     // the cells are the grid
    SetLegendMode(ChartLegendMode::ColorBar);
    SetLegendPosition(ChartLegendPosition::RightCenter);
    SetShowLegend(true);
    RefreshColorBar();
}

// =============================================================================
// DATA
// =============================================================================

void UltraCanvasEngineHeatmapChart::SetData(const std::vector<double>& flat, int columns,
                                            int rowCount) {
    cols = std::max(0, columns);
    rows = std::max(0, rowCount);
    values = flat;
    values.resize(static_cast<size_t>(cols) * static_cast<size_t>(rows),
                  std::numeric_limits<double>::quiet_NaN());
    RecomputeRange();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineHeatmapChart::SetData(const std::vector<std::vector<double>>& matrix) {
    int width = 0;
    for (const auto& row : matrix) width = std::max(width, static_cast<int>(row.size()));
    std::vector<double> flat(static_cast<size_t>(width) * matrix.size(),
                             std::numeric_limits<double>::quiet_NaN());
    for (size_t r = 0; r < matrix.size(); ++r) {
        for (size_t c = 0; c < matrix[r].size(); ++c) {
            flat[r * static_cast<size_t>(width) + c] = matrix[r][c];
        }
    }
    SetData(flat, width, static_cast<int>(matrix.size()));
}

void UltraCanvasEngineHeatmapChart::SetValue(int col, int row, double value) {
    if (col < 0 || row < 0 || col >= cols || row >= rows) return;
    values[static_cast<size_t>(row) * cols + col] = value;
    RecomputeRange();
    RequestRedraw();
}

double UltraCanvasEngineHeatmapChart::GetValue(int col, int row) const {
    if (col < 0 || row < 0 || col >= cols || row >= rows) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return values[static_cast<size_t>(row) * cols + col];
}

void UltraCanvasEngineHeatmapChart::ClearData() {
    values.clear();
    cols = rows = 0;
    MarkEngineDirty(ChartDirty::Data);
}

// =============================================================================
// VALUE MAPPING
// =============================================================================

void UltraCanvasEngineHeatmapChart::SetAutoValueRange(bool on) {
    autoRange = on;
    RecomputeRange();
    RequestRedraw();
}

void UltraCanvasEngineHeatmapChart::SetValueRange(double minValue, double maxValue) {
    autoRange = false;
    valueMin = std::min(minValue, maxValue);
    valueMax = std::max(minValue, maxValue);
    RefreshColorBar();
    RequestRedraw();
}

void UltraCanvasEngineHeatmapChart::RecomputeRange() {
    if (autoRange) {
        double lo = std::numeric_limits<double>::max();
        double hi = std::numeric_limits<double>::lowest();
        for (double v : values) {
            if (!std::isfinite(v)) continue;
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        if (lo > hi) { lo = 0.0; hi = 1.0; }
        valueMin = lo;
        valueMax = hi;
    }
    RefreshColorBar();
}

void UltraCanvasEngineHeatmapChart::SetScale(ChartHeatmapScale s) {
    scale = s;
    RefreshColorBar();
    RequestRedraw();
}

void UltraCanvasEngineHeatmapChart::SetDiverging(bool on, double midpoint) {
    diverging = on;
    divergingMidpoint = midpoint;
    RefreshColorBar();
    RequestRedraw();
}

void UltraCanvasEngineHeatmapChart::SetColormap(HeatmapColormap c) {
    colormap = c;
    RefreshColorBar();
    RequestRedraw();
}

void UltraCanvasEngineHeatmapChart::SetCustomColormap(const std::vector<Color>& anchors) {
    customColormap = anchors;
    if (anchors.size() >= 2) colormap = HeatmapColormap::Custom;
    RefreshColorBar();
    RequestRedraw();
}

void UltraCanvasEngineHeatmapChart::SetReverseColormap(bool on) {
    reverseColormap = on;
    RefreshColorBar();
    RequestRedraw();
}

void UltraCanvasEngineHeatmapChart::SetColorLevels(int levels) {
    colorLevels = std::max(0, levels);
    RefreshColorBar();
    RequestRedraw();
}

void UltraCanvasEngineHeatmapChart::SetNaNColor(const Color& color) {
    nanColor = color;
    RequestRedraw();
}

double UltraCanvasEngineHeatmapChart::NormalizeValue(double v) const {
    if (std::isnan(v)) return v;
    if (diverging) return DivergingNorm(v, valueMin, divergingMidpoint, valueMax);
    double t;
    if (scale == ChartHeatmapScale::Logarithmic) {
        const double eps = (valueMax > 0.0 ? valueMax : 1.0) * 1e-6;
        const double lv = std::log10(v <= 0.0 ? eps : v);
        const double llo = std::log10(valueMin <= 0.0 ? eps : valueMin);
        const double lhi = std::log10(valueMax <= 0.0 ? eps : valueMax);
        if (lhi <= llo) return 0.0;
        t = (lv - llo) / (lhi - llo);
    } else {
        if (valueMax <= valueMin) return 0.0;
        t = (v - valueMin) / (valueMax - valueMin);
    }
    return std::clamp(t, 0.0, 1.0);
}

double UltraCanvasEngineHeatmapChart::DenormalizeValue(double t) const {
    if (diverging) {
        // DivergingNorm's two linear halves, inverted.
        return (t <= 0.5) ? valueMin + (divergingMidpoint - valueMin) * (t / 0.5)
                          : divergingMidpoint + (valueMax - divergingMidpoint) * ((t - 0.5) / 0.5);
    }
    if (scale == ChartHeatmapScale::Logarithmic) {
        const double eps = (valueMax > 0.0 ? valueMax : 1.0) * 1e-6;
        const double llo = std::log10(valueMin <= 0.0 ? eps : valueMin);
        const double lhi = std::log10(valueMax <= 0.0 ? eps : valueMax);
        return std::pow(10.0, llo + (lhi - llo) * t);
    }
    return valueMin + (valueMax - valueMin) * t;
}

Color UltraCanvasEngineHeatmapChart::MapValueToColor(double value) const {
    const double t = NormalizeValue(value);
    if (std::isnan(t)) return nanColor;
    return SampleColormap(colormap, customColormap,
                          QuantizeNorm(std::clamp(t, 0.0, 1.0), colorLevels), reverseColormap);
}

void UltraCanvasEngineHeatmapChart::RefreshColorBar() {
    LegendColorBar bar;
    bar.colormap = colormap;
    bar.customColormap = customColormap;
    bar.reverse = reverseColormap;
    bar.quantizeLevels = colorLevels;
    bar.tickCount = 5;
    bar.barLength = 220.0;
    if (scale == ChartHeatmapScale::Linear && !diverging) {
        bar.minValue = valueMin;
        bar.maxValue = valueMax;
        bar.formatter = [this](double v) { return FormatCell(v); };
    } else {
        // The bar runs over the colour-map position itself; its ticks are
        // read back through the inverse of the cells' normalisation, so a
        // log or diverging key labels exactly the values the cells show.
        bar.minValue = 0.0;
        bar.maxValue = 1.0;
        bar.formatter = [this](double t) { return FormatCell(DenormalizeValue(t)); };
    }
    Legend().SetColorBar(bar);
    MarkEngineDirty(ChartDirty::Geometry);    // tick labels may change width
}

// =============================================================================
// LAYOUT AND CELLS
// =============================================================================

void UltraCanvasEngineHeatmapChart::SetTriangularMask(ChartHeatmapMask m) {
    mask = m;
    RequestRedraw();
}

void UltraCanvasEngineHeatmapChart::SetRowOrder(ChartHeatmapRowOrder order) {
    rowOrder = order;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineHeatmapChart::SetColumnLabels(const std::vector<std::string>& labels) {
    columnLabels = labels;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineHeatmapChart::SetRowLabels(const std::vector<std::string>& labels) {
    rowLabels = labels;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineHeatmapChart::SetShowColumnLabels(bool show) {
    showColumnLabels = show;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineHeatmapChart::SetShowRowLabels(bool show) {
    showRowLabels = show;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineHeatmapChart::SetColumnLabelsOnTop(bool onTop) {
    columnLabelsOnTop = onTop;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineHeatmapChart::SetAxisTitles(const std::string& x, const std::string& y) {
    xTitle = x;
    yTitle = y;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineHeatmapChart::SetCellShape(ChartHeatmapCellShape shape) {
    cellShape = shape;
    RequestRedraw();
}

void UltraCanvasEngineHeatmapChart::SetCellGap(double fraction) {
    cellGap = std::clamp(fraction, 0.0, 0.9);
    RequestRedraw();
}

void UltraCanvasEngineHeatmapChart::SetCellCornerRadius(double radiusPx) {
    cellCornerRadius = radiusPx;
    RequestRedraw();
}

void UltraCanvasEngineHeatmapChart::SetShowCellBorders(bool on, const Color& color, float width) {
    showCellBorders = on;
    cellBorderColor = color;
    cellBorderWidth = width;
    RequestRedraw();
}

void UltraCanvasEngineHeatmapChart::SetShowCellValues(bool on) {
    showCellValues = on;
    RequestRedraw();
}

void UltraCanvasEngineHeatmapChart::SetCellValueStyle(const Color& color, float fontSize) {
    cellValueColor = color;
    cellValueFontSize = fontSize;
    RequestRedraw();
}

void UltraCanvasEngineHeatmapChart::SetValueFormatter(ValueFormatter formatter) {
    valueFormatter = std::move(formatter);
    RefreshColorBar();
    RequestRedraw();
}

void UltraCanvasEngineHeatmapChart::SetShowColorBar(bool show) {
    showColorBar = show;
    SetShowLegend(show);
}

bool UltraCanvasEngineHeatmapChart::IsCellVisible(int col, int row) const {
    switch (mask) {
        case ChartHeatmapMask::Lower:           return col <= row;
        case ChartHeatmapMask::LowerNoDiagonal: return col < row;
        case ChartHeatmapMask::Upper:           return col >= row;
        case ChartHeatmapMask::UpperNoDiagonal: return col > row;
        case ChartHeatmapMask::NoMask:
        default:                                return true;
    }
}

std::string UltraCanvasEngineHeatmapChart::FormatCell(double v) const {
    if (std::isnan(v)) return "NaN";
    if (valueFormatter) return valueFormatter(v);
    std::ostringstream out;
    if (std::abs(v) < 1e-12) v = 0.0;        // no "-0.000" from a round trip
    if (v != 0.0 && std::abs(v) < 0.01) {
        // A log key reaches far below what fixed decimals can show: three
        // significant digits (2.59e-06), as the legacy colour bar printed.
        out << std::setprecision(3) << v;
        return out.str();
    }
    if (std::abs(v - std::round(v)) < 1e-9 && std::abs(v) < 1e7) {
        out << std::fixed << std::setprecision(0) << v;
    } else {
        const double magnitude = std::abs(v);
        out << std::fixed << std::setprecision(magnitude >= 100.0 ? 1 : (magnitude >= 1.0 ? 2 : 3))
            << v;
    }
    return out.str();
}

std::string UltraCanvasEngineHeatmapChart::ColumnLabel(int col) const {
    if (col >= 0 && col < static_cast<int>(columnLabels.size())) return columnLabels[col];
    return std::to_string(col);
}

std::string UltraCanvasEngineHeatmapChart::RowLabel(int row) const {
    if (row >= 0 && row < static_cast<int>(rowLabels.size())) return rowLabels[row];
    return std::to_string(row);
}

// =============================================================================
// ENGINE CONTRACT
// =============================================================================

void UltraCanvasEngineHeatmapChart::DescribeAxes(ChartAxisSet& axes) {
    // One slot per column and per row, padded so the outer cells keep their
    // full size; the engine declutters the labels of a dense matrix.
    ChartAxis columns("columns");
    columns.scale = ChartScale::Category;
    columns.side = columnLabelsOnTop ? ChartAxisSide::Top : ChartAxisSide::Bottom;
    columns.categoryPadding = 0.5;
    for (int c = 0; c < cols; ++c) columns.categories.push_back(ColumnLabel(c));
    columns.visible = showColumnLabels || !xTitle.empty();
    columns.title = xTitle;
    if (!showColumnLabels) columns.formatter = [](double) { return std::string(); };
    axes.Add(columns);                        // kColumnAxis

    ChartAxis rowAxis("rows");
    rowAxis.scale = ChartScale::Category;
    rowAxis.side = ChartAxisSide::Left;
    rowAxis.categoryPadding = 0.5;
    // Matrix order puts row 0 at the top, where the value direction ends.
    rowAxis.inverted = (rowOrder == ChartHeatmapRowOrder::TopDown);
    for (int r = 0; r < rows; ++r) rowAxis.categories.push_back(RowLabel(r));
    rowAxis.visible = showRowLabels || !yTitle.empty();
    rowAxis.title = yTitle;
    axes.Add(rowAxis);                        // kRowAxis
}

void UltraCanvasEngineHeatmapChart::RenderChartContent(IRenderContext* ctx,
                                                       const ChartEngineFrame& frame) {
    if (cols <= 0 || rows <= 0 || !frame.axes || frame.axes->Count() < 2) return;
    const ChartAxis& columns = frame.axes->At(kColumnAxis);
    const ChartAxis& rowAxis = frame.axes->At(kRowAxis);
    const IChartProjection& projection = *frame.projection;
    const double inset = cellGap * 0.5;
    const int64_t hovered = HoveredRegionId();
    // Entrance: the colours fade in from the plot background.
    const double progress = frame.animationProgress;

    // One cell's screen size decides the auto corner radius and whether a
    // value fits - measured through the projection, not from the plot rect.
    const Point2Dd p0 = projection.ToScreen(ChartNormalizedPoint(columns.Normalize(0.0),
                                                                 rowAxis.Normalize(0.0)));
    const Point2Dd p1 = projection.ToScreen(ChartNormalizedPoint(columns.Normalize(1.0),
                                                                 rowAxis.Normalize(1.0)));
    const double cellW = std::abs(p1.x - p0.x) * (1.0 - cellGap);
    const double cellH = std::abs(p1.y - p0.y) * (1.0 - cellGap);
    const double radius = (cellShape == ChartHeatmapCellShape::RoundedRectangle)
                              ? (cellCornerRadius > 0.0 ? cellCornerRadius
                                                        : std::min(cellW, cellH) * 0.22)
                              : 0.0;

    if (showCellValues) ctx->SetFontSize(cellValueFontSize);
    const bool drawValues = showCellValues && cellW >= 14.0 && cellH >= cellValueFontSize + 2.0;

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            if (!IsCellVisible(c, r)) continue;
            const double value = values[static_cast<size_t>(r) * cols + c];
            Color color = MapValueToColor(value);
            if (color.a == 0) continue;            // transparent NaN cells vanish
            if (progress < 1.0) color = color.WithAlpha(static_cast<uint8_t>(color.a * progress));

            const double u0 = columns.Normalize(c - 0.5 + inset);
            const double u1 = columns.Normalize(c + 0.5 - inset);
            const double v0 = rowAxis.Normalize(r - 0.5 + inset);
            const double v1 = rowAxis.Normalize(r + 0.5 - inset);
            const int64_t id = static_cast<int64_t>(r) * cols + c;

            std::vector<Point2Dd> outline;
            Point2Dd center;
            double circleRadius = 0.0;
            if (cellShape == ChartHeatmapCellShape::Circle) {
                const Point2Dd a = projection.ToScreen(ChartNormalizedPoint(u0, v0));
                const Point2Dd b = projection.ToScreen(ChartNormalizedPoint(u1, v1));
                center = Point2Dd((a.x + b.x) * 0.5, (a.y + b.y) * 0.5);
                circleRadius = std::min(std::abs(b.x - a.x), std::abs(b.y - a.y)) * 0.5;
                ctx->SetFillPaint(color);
                ctx->FillCircle(center, circleRadius);
                if (showCellBorders) {
                    ctx->SetStrokePaint(cellBorderColor);
                    ctx->SetStrokeWidth(cellBorderWidth);
                    ctx->DrawCircle(center, circleRadius);
                }
                outline = {Point2Dd(center.x - circleRadius, center.y - circleRadius),
                           Point2Dd(center.x + circleRadius, center.y - circleRadius),
                           Point2Dd(center.x + circleRadius, center.y + circleRadius),
                           Point2Dd(center.x - circleRadius, center.y + circleRadius)};
            } else {
                outline = BuildBarOutline(projection, u0, v0, u1, v1, 2, radius);
                if (outline.size() < 3) continue;
                ctx->SetFillPaint(color);
                ctx->FillLinePath(outline);
                // Contiguous plain cells hide hairline seams with a stroke in
                // their own colour; real borders replace it.
                if (showCellBorders) {
                    ctx->SetStrokePaint(cellBorderColor);
                    ctx->SetStrokeWidth(cellBorderWidth);
                    ctx->DrawLinePath(outline, true);
                } else if (cellGap <= 0.0 && radius <= 0.0) {
                    ctx->SetStrokePaint(color);
                    ctx->SetStrokeWidth(1.0f);
                    ctx->DrawLinePath(outline, true);
                }
                double cx = 0.0, cy = 0.0;
                for (const Point2Dd& p : outline) { cx += p.x; cy += p.y; }
                center = Point2Dd(cx / outline.size(), cy / outline.size());
            }

            if (hovered == id) {
                ctx->SetStrokePaint(GetTheme().axisLineColor);
                ctx->SetStrokeWidth(2.0f);
                if (cellShape == ChartHeatmapCellShape::Circle) {
                    ctx->DrawCircle(center, circleRadius + 1.0);
                } else {
                    ctx->DrawLinePath(outline, true);
                }
            }

            if (drawValues && std::isfinite(value)) {
                const std::string text = FormatCell(value);
                const Size2Di size = ctx->GetTextLineDimensions(text);
                if (size.width <= cellW - 2.0) {
                    ctx->SetTextPaint(cellValueColor.a > 0 ? cellValueColor : ContrastText(color));
                    ctx->DrawText(text, Point2Dd(center.x - size.width * 0.5,
                                                 center.y - size.height * 0.5));
                }
            }

            AddHitRegion(outline, id,
                         RowLabel(r) + " / " + ColumnLabel(c) + ": " + FormatCell(value));
        }
    }
}

} // namespace UltraCanvas
