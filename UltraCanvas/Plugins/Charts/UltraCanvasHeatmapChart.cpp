// Plugins/Charts/UltraCanvasHeatmapChart.cpp
// 2D heatmap / matrix chart element for UltraCanvas.
// Version: 1.0.0
// Last Modified: 2026-06-18
// Author: UltraCanvas Framework
#include "Plugins/Charts/UltraCanvasHeatmapChart.h"
#include "UltraCanvasTooltipManager.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace UltraCanvas {

// =============================================================================
// CONSTRUCTION
// =============================================================================

    UltraCanvasHeatmapChartElement::UltraCanvasHeatmapChartElement(
            const std::string& id, int x, int y, int width, int height)
            : UltraCanvasChartElementBase(id, x, y, width, height) {
        // The base grid/axes machinery is built around a 1D point data source;
        // the heatmap manages its own layout, so disable them here.
        showGrid = false;
        showAxes = false;
        enableTooltips = true;
    }

// =============================================================================
// DATA
// =============================================================================

    void UltraCanvasHeatmapChartElement::SetData(const std::vector<double>& flat, int columns, int rowsCount) {
        if (columns < 0) columns = 0;
        if (rowsCount < 0) rowsCount = 0;
        cols = columns;
        rows = rowsCount;
        values = flat;
        values.resize(static_cast<size_t>(cols) * static_cast<size_t>(rows), 0.0);
        rangeDirty = true;
        InvalidateRaster();
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasHeatmapChartElement::SetData(const std::vector<std::vector<double>>& matrix) {
        rows = static_cast<int>(matrix.size());
        cols = 0;
        for (const auto& row : matrix) {
            cols = std::max(cols, static_cast<int>(row.size()));
        }
        values.assign(static_cast<size_t>(cols) * static_cast<size_t>(rows), 0.0);
        for (int r = 0; r < rows; ++r) {
            const auto& row = matrix[r];
            for (int c = 0; c < static_cast<int>(row.size()); ++c) {
                values[static_cast<size_t>(r) * cols + c] = row[c];
            }
        }
        rangeDirty = true;
        InvalidateRaster();
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasHeatmapChartElement::Resize(int columns, int rowsCount, double fill) {
        cols = std::max(0, columns);
        rows = std::max(0, rowsCount);
        values.assign(static_cast<size_t>(cols) * static_cast<size_t>(rows), fill);
        rangeDirty = true;
        InvalidateRaster();
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasHeatmapChartElement::SetValue(int col, int row, double v) {
        if (col < 0 || col >= cols || row < 0 || row >= rows) return;
        values[static_cast<size_t>(row) * cols + col] = v;
        rangeDirty = true;
        InvalidateRaster();
        RequestRedraw();
    }

    double UltraCanvasHeatmapChartElement::GetValue(int col, int row) const {
        if (col < 0 || col >= cols || row < 0 || row >= rows) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        return values[static_cast<size_t>(row) * cols + col];
    }

    void UltraCanvasHeatmapChartElement::ClearData() {
        values.clear();
        cols = rows = 0;
        rangeDirty = true;
        InvalidateRaster();
        InvalidateCache();
        RequestRedraw();
    }

// =============================================================================
// VALUE RANGE / SCALE
// =============================================================================

    void UltraCanvasHeatmapChartElement::SetAutoValueRange(bool on) {
        autoRange = on;
        if (on) rangeDirty = true;
        InvalidateRaster();
        RequestRedraw();
    }

    void UltraCanvasHeatmapChartElement::SetValueRange(double minVal, double maxVal) {
        autoRange = false;
        valueMin = minVal;
        valueMax = maxVal;
        InvalidateRaster();
        RequestRedraw();
    }

    void UltraCanvasHeatmapChartElement::SetScale(HeatmapScale s) {
        scaleMode = s;
        InvalidateRaster();
        RequestRedraw();
    }

    void UltraCanvasHeatmapChartElement::SetDiverging(bool on, double midpoint) {
        divergingEnabled = on;
        divergingMidpoint = midpoint;
        InvalidateRaster();
        RequestRedraw();
    }

// =============================================================================
// COLOUR MAP
// =============================================================================

    void UltraCanvasHeatmapChartElement::SetColormap(HeatmapColormap c) {
        colormap = c;
        InvalidateRaster();
        RequestRedraw();
    }

    void UltraCanvasHeatmapChartElement::SetCustomColormap(const std::vector<Color>& colors) {
        customColormap = colors;
        colormap = HeatmapColormap::Custom;
        InvalidateRaster();
        RequestRedraw();
    }

    void UltraCanvasHeatmapChartElement::SetReverseColormap(bool on) {
        reverseColormap = on;
        InvalidateRaster();
        RequestRedraw();
    }

    void UltraCanvasHeatmapChartElement::SetColorLevels(int levels) {
        colorLevels = (levels < 2) ? 0 : levels;
        InvalidateRaster();
        RequestRedraw();
    }

    void UltraCanvasHeatmapChartElement::SetNaNColor(const Color& c) {
        nanColor = c;
        InvalidateRaster();
        RequestRedraw();
    }

    void UltraCanvasHeatmapChartElement::SetTriangularMask(HeatmapTriangularMask mask) {
        triangularMask = mask;
        InvalidateRaster();
        RequestRedraw();
    }

// =============================================================================
// ORIENTATION / RENDER STRATEGY
// =============================================================================

    void UltraCanvasHeatmapChartElement::SetRowOrder(HeatmapRowOrder o) {
        rowOrder = o;
        InvalidateRaster();
        RequestRedraw();
    }

    void UltraCanvasHeatmapChartElement::SetRenderMode(HeatmapRenderMode m) {
        renderMode = m;
        RequestRedraw();
    }

// =============================================================================
// CELL DECORATION
// =============================================================================

    void UltraCanvasHeatmapChartElement::SetCellShape(HeatmapCellShape shape) {
        cellShape = shape;
        RequestRedraw();
    }

    void UltraCanvasHeatmapChartElement::SetCellGap(double fraction) {
        cellGap = std::clamp(fraction, 0.0, 0.9);
        RequestRedraw();
    }

    void UltraCanvasHeatmapChartElement::SetCellCornerRadius(double radiusPx) {
        cellCornerRadius = std::max(0.0, radiusPx);
        RequestRedraw();
    }

    void UltraCanvasHeatmapChartElement::SetShowCellBorders(bool on, const Color& color, float width) {
        showCellBorders = on;
        cellBorderColor = color;
        cellBorderWidth = std::max(0.0f, width);
        RequestRedraw();
    }

    void UltraCanvasHeatmapChartElement::SetShowCellValues(bool on) {
        showCellValues = on;
        RequestRedraw();
    }

    void UltraCanvasHeatmapChartElement::SetCellValueStyle(const Color& color, float fontSize) {
        cellValueColor = color;
        cellValueFontSize = std::max(1.0f, fontSize);
        RequestRedraw();
    }

// =============================================================================
// LABELS / TITLES
// =============================================================================

    void UltraCanvasHeatmapChartElement::SetColumnLabels(const std::vector<std::string>& labels) {
        columnLabels = labels;
        RequestRedraw();
    }

    void UltraCanvasHeatmapChartElement::SetRowLabels(const std::vector<std::string>& labels) {
        rowLabels = labels;
        RequestRedraw();
    }

    void UltraCanvasHeatmapChartElement::SetShowColumnLabels(bool on) {
        showColumnLabels = on;
        RequestRedraw();
    }

    void UltraCanvasHeatmapChartElement::SetShowRowLabels(bool on) {
        showRowLabels = on;
        RequestRedraw();
    }

    void UltraCanvasHeatmapChartElement::SetColumnLabelsOnTop(bool on) {
        columnLabelsOnTop = on;
        RequestRedraw();
    }

    void UltraCanvasHeatmapChartElement::SetAxisTitles(const std::string& xTitle, const std::string& yTitle) {
        xAxisTitle = xTitle;
        yAxisTitle = yTitle;
        RequestRedraw();
    }

    void UltraCanvasHeatmapChartElement::SetShowColorBar(bool on) {
        showColorBar = on;
        RequestRedraw();
    }

// =============================================================================
// INTERNAL HELPERS
// =============================================================================

    void UltraCanvasHeatmapChartElement::InvalidateRaster() {
        pixmapValid = false;
    }

    void UltraCanvasHeatmapChartElement::RecomputeAutoRange() {
        double lo = std::numeric_limits<double>::infinity();
        double hi = -std::numeric_limits<double>::infinity();
        for (double v : values) {
            if (std::isnan(v)) continue;
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        if (!std::isfinite(lo) || !std::isfinite(hi)) {
            lo = 0.0;
            hi = 1.0;
        } else if (lo == hi) {
            // Degenerate range - expand so everything maps to mid colour.
            lo -= 0.5;
            hi += 0.5;
        }
        valueMin = lo;
        valueMax = hi;
        rangeDirty = false;
    }

    double UltraCanvasHeatmapChartElement::NormalizeValue(double v) const {
        if (std::isnan(v)) return std::numeric_limits<double>::quiet_NaN();

        if (divergingEnabled) {
            return DivergingNorm(v, valueMin, divergingMidpoint, valueMax);
        }

        double lo = valueMin;
        double hi = valueMax;
        double t;

        if (scaleMode == HeatmapScale::Logarithmic) {
            double eps = (hi > 0.0 ? hi : 1.0) * 1e-6;
            double lv = (v <= 0.0) ? eps : v;
            double llo = (lo <= 0.0) ? eps : lo;
            double lhi = (hi <= 0.0) ? eps : hi;
            if (lhi <= llo) return 0.0;
            t = (std::log10(lv) - std::log10(llo)) / (std::log10(lhi) - std::log10(llo));
        } else {
            if (hi <= lo) return 0.0;
            t = (v - lo) / (hi - lo);
        }
        return std::clamp(t, 0.0, 1.0);
    }

    Color UltraCanvasHeatmapChartElement::ColorAtT(double t) const {
        t = QuantizeNorm(std::clamp(t, 0.0, 1.0), colorLevels);
        return SampleColormap(colormap, customColormap, t, reverseColormap);
    }

    Color UltraCanvasHeatmapChartElement::MapValueToColor(double value) const {
        double t = NormalizeValue(value);
        if (std::isnan(t)) return nanColor;
        return ColorAtT(t);
    }

    uint32_t UltraCanvasHeatmapChartElement::ColorToPixel(const Color& c) {
        // Cairo ARGB32 is premultiplied, native-endian (0xAARRGGBB on LE).
        uint32_t a = c.a;
        uint32_t r = static_cast<uint32_t>(c.r) * a / 255;
        uint32_t g = static_cast<uint32_t>(c.g) * a / 255;
        uint32_t b = static_cast<uint32_t>(c.b) * a / 255;
        return (a << 24) | (r << 16) | (g << 8) | b;
    }

    std::string UltraCanvasHeatmapChartElement::ColumnLabel(int col) const {
        if (col >= 0 && col < static_cast<int>(columnLabels.size())) return columnLabels[col];
        return std::to_string(col);
    }

    std::string UltraCanvasHeatmapChartElement::RowLabel(int row) const {
        if (row >= 0 && row < static_cast<int>(rowLabels.size())) return rowLabels[row];
        return std::to_string(row);
    }

    std::string UltraCanvasHeatmapChartElement::FormatValue(double v) const {
        if (std::isnan(v)) return "NaN";
        char buf[32];
        if (std::fabs(v - std::round(v)) < 1e-9 && std::fabs(v) < 1e7) {
            std::snprintf(buf, sizeof(buf), "%.0f", v);
        } else {
            std::snprintf(buf, sizeof(buf), "%.3g", v);
        }
        return std::string(buf);
    }

    bool UltraCanvasHeatmapChartElement::IsCellVisible(int col, int row) const {
        switch (triangularMask) {
            case HeatmapTriangularMask::Lower:           return col <= row;
            case HeatmapTriangularMask::LowerNoDiagonal: return col <  row;
            case HeatmapTriangularMask::Upper:           return col >= row;
            case HeatmapTriangularMask::UpperNoDiagonal: return col >  row;
            case HeatmapTriangularMask::NoMask:
            default:                                     return true;
        }
    }

// =============================================================================
// LAYOUT
// =============================================================================

    ChartPlotArea UltraCanvasHeatmapChartElement::ComputeHeatmapArea(IRenderContext* ctx) {
        double left = 10.0, right = 10.0, top = 10.0, bottom = 10.0;

        ctx->SetFontSize(11.0);
        int lineH = ctx->GetTextLineHeight("0");

        if (!chartTitle.empty()) top += lineH + 12;

        if (showRowLabels && rows > 0) {
            double maxW = 0.0;
            if (!rowLabels.empty()) {
                for (int r = 0; r < rows; ++r) {
                    maxW = std::max(maxW, static_cast<double>(ctx->GetTextLineWidth(RowLabel(r))));
                }
            } else {
                // Numeric fallback: the largest index produces the widest label.
                maxW = ctx->GetTextLineWidth(std::to_string(std::max(0, rows - 1)));
            }
            left += maxW + 10.0;
        }
        if (!yAxisTitle.empty()) left += lineH + 4.0;

        if (showColumnLabels && cols > 0) {
            if (columnLabelsOnTop) top += lineH + 6.0;
            else                   bottom += lineH + 8.0;
        }
        if (!xAxisTitle.empty()) bottom += lineH + 4.0;

        if (showColorBar) {
            double w = std::max(ctx->GetTextLineWidth(FormatValue(valueMax)),
                                ctx->GetTextLineWidth(FormatValue(valueMin)));
            right += colorBarGap + colorBarWidth + 6.0 + w + 4.0;
        }

        double w = std::max(1.0, static_cast<double>(GetWidth()) - left - right);
        double h = std::max(1.0, static_cast<double>(GetHeight()) - top - bottom);
        return ChartPlotArea(left, top, w, h);
    }

    bool UltraCanvasHeatmapChartElement::EffectiveUseImageMode() const {
        if (renderMode == HeatmapRenderMode::Image) return true;
        if (renderMode == HeatmapRenderMode::Cells) return false;
        if (cols <= 0 || rows <= 0) return false;
        double cellW = heatmapArea.width / cols;
        double cellH = heatmapArea.height / rows;
        return (static_cast<long long>(cols) * rows > 2500) || cellW < 4.0 || cellH < 4.0;
    }

// =============================================================================
// RENDERING
// =============================================================================

    void UltraCanvasHeatmapChartElement::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        if (!ctx) return;

        if (showBackground) {
            ctx->DrawFilledRectangle(Rect2Dd(0, 0, GetWidth(), GetHeight()), backgroundColor);
        }

        if (cols <= 0 || rows <= 0 || values.empty()) {
            DrawEmptyState(ctx);
            return;
        }

        if (autoRange && rangeDirty) {
            RecomputeAutoRange();
        }

        heatmapArea = ComputeHeatmapArea(ctx);

        // Title
        if (!chartTitle.empty()) {
            ctx->SetTextPaint(Color(0, 0, 0, 255));
            ctx->SetFontSize(15.0);
            Size2Di sz = ctx->GetTextLineDimensions(chartTitle);
            ctx->DrawText(chartTitle, Point2Dd(GetWidth() / 2.0 - sz.width / 2.0, 6));
        }

        RenderChart(ctx);
    }

    void UltraCanvasHeatmapChartElement::RenderChart(IRenderContext* ctx) {
        if (!ctx || cols <= 0 || rows <= 0) return;

        if (EffectiveUseImageMode()) {
            RenderImage(ctx);
        } else {
            RenderCells(ctx);
        }

        // Plot-area border
        ctx->SetStrokePaint(Color(120, 120, 120, 255));
        ctx->SetStrokeWidth(1.0);
        ctx->DrawRectangle(heatmapArea.ToRect2D());

        RenderGridLabels(ctx);
        RenderColorBar(ctx);
    }

    void UltraCanvasHeatmapChartElement::RenderCells(IRenderContext* ctx) {
        double cellW = heatmapArea.width / cols;
        double cellH = heatmapArea.height / rows;

        double gx = cellW * cellGap * 0.5;
        double gy = cellH * cellGap * 0.5;
        // Overlap adjacent rectangles by 1px to hide hairline seams, but only
        // when cells are truly contiguous (no gap, no borders, plain rects).
        bool seamless = (cellGap <= 0.0 && cellShape == HeatmapCellShape::Rectangle && !showCellBorders);

        ctx->SetFontSize(cellValueFontSize);

        for (int r = 0; r < rows; ++r) {
            int displayRow = (rowOrder == HeatmapRowOrder::TopDown) ? r : (rows - 1 - r);
            for (int c = 0; c < cols; ++c) {
                if (!IsCellVisible(c, r)) continue;

                double v = values[static_cast<size_t>(r) * cols + c];
                Color color = MapValueToColor(v);

                double x = heatmapArea.x + c * cellW;
                double y = heatmapArea.y + displayRow * cellH;

                double innerX = x + gx;
                double innerY = y + gy;
                double innerW = cellW - 2.0 * gx + (seamless ? 1.0 : 0.0);
                double innerH = cellH - 2.0 * gy + (seamless ? 1.0 : 0.0);
                if (innerW <= 0.0 || innerH <= 0.0) continue;
                Rect2Dd inner(innerX, innerY, innerW, innerH);

                Color border = (showCellBorders && cellBorderWidth > 0.0f)
                               ? cellBorderColor : Colors::Transparent;
                float bw = (border.a > 0) ? cellBorderWidth : 0.0f;

                switch (cellShape) {
                    case HeatmapCellShape::Circle: {
                        double rad = std::min(innerW, innerH) * 0.5;
                        Point2Dd center(innerX + innerW * 0.5, innerY + innerH * 0.5);
                        if (color.a > 0 || bw > 0) {
                            ctx->DrawFilledCircle(center, static_cast<float>(rad), color, border, bw);
                        }
                        break;
                    }
                    case HeatmapCellShape::RoundedRectangle: {
                        double rad = (cellCornerRadius > 0.0) ? cellCornerRadius
                                                              : std::min(innerW, innerH) * 0.25;
                        rad = std::min(rad, std::min(innerW, innerH) * 0.5);
                        ctx->DrawFilledRectangle(inner, color, bw, border, static_cast<float>(rad));
                        break;
                    }
                    case HeatmapCellShape::Rectangle:
                    default:
                        ctx->DrawFilledRectangle(inner, color, bw, border);
                        break;
                }

                if (showCellValues && !std::isnan(v) && innerW > 14.0 && innerH > 10.0) {
                    std::string text = FormatValue(v);
                    Size2Di sz = ctx->GetTextLineDimensions(text);
                    ctx->SetTextPaint(cellValueColor);
                    ctx->DrawText(text, Point2Dd(innerX + (innerW - sz.width) / 2.0,
                                                 innerY + (innerH - sz.height) / 2.0));
                }
            }
        }
    }

    void UltraCanvasHeatmapChartElement::BuildPixmap() {
        if (!cellPixmap) {
            cellPixmap = std::make_shared<UCPixmap>();
        }
        if (!cellPixmap->Init(cols, rows)) return;

        uint32_t* px = cellPixmap->GetPixelData();
        if (!px) return;

        for (int r = 0; r < rows; ++r) {
            int pixRow = (rowOrder == HeatmapRowOrder::TopDown) ? r : (rows - 1 - r);
            uint32_t* line = px + static_cast<size_t>(pixRow) * cols;
            for (int c = 0; c < cols; ++c) {
                Color color = IsCellVisible(c, r)
                              ? MapValueToColor(values[static_cast<size_t>(r) * cols + c])
                              : nanColor;
                line[c] = ColorToPixel(color);
            }
        }
        cellPixmap->MarkDirty();
        pixmapValid = true;
    }

    void UltraCanvasHeatmapChartElement::RenderImage(IRenderContext* ctx) {
        if (!pixmapValid) {
            BuildPixmap();
        }
        if (!cellPixmap || !cellPixmap->IsValid()) return;

        ctx->DrawPartOfPixmap(*cellPixmap,
                              Rect2Dd(0, 0, cols, rows),
                              heatmapArea.ToRect2D());
    }

    void UltraCanvasHeatmapChartElement::RenderGridLabels(IRenderContext* ctx) {
        ctx->SetFontSize(11.0);
        ctx->SetTextPaint(Color(40, 40, 40, 255));
        int lineH = ctx->GetTextLineHeight("0");

        double cellW = heatmapArea.width / cols;
        double cellH = heatmapArea.height / rows;

        // Column labels. Draw every non-empty label that does not overlap the
        // previous one. This thins dense numeric axes automatically while still
        // honouring sparse labels (e.g. month names on a calendar heatmap).
        if (showColumnLabels) {
            double y = columnLabelsOnTop ? (heatmapArea.y - lineH - 2.0)
                                         : (heatmapArea.GetBottom() + 4.0);
            double lastRight = -1e9;
            for (int c = 0; c < cols; ++c) {
                std::string label = ColumnLabel(c);
                if (label.empty()) continue;
                Size2Di sz = ctx->GetTextLineDimensions(label);
                double cx = heatmapArea.x + (c + 0.5) * cellW;
                double x = cx - sz.width / 2.0;
                if (x < lastRight + 4.0) continue;
                ctx->DrawText(label, Point2Dd(x, y));
                lastRight = x + sz.width;
            }
        }

        // Row labels (left), with the same overlap avoidance vertically.
        if (showRowLabels) {
            double lastBottom = -1e9;
            for (int r = 0; r < rows; ++r) {
                std::string label = RowLabel(r);
                if (label.empty()) continue;
                int displayRow = (rowOrder == HeatmapRowOrder::TopDown) ? r : (rows - 1 - r);
                Size2Di sz = ctx->GetTextLineDimensions(label);
                double cy = heatmapArea.y + (displayRow + 0.5) * cellH;
                double y = cy - sz.height / 2.0;
                if (y < lastBottom + 2.0) continue;
                ctx->DrawText(label, Point2Dd(heatmapArea.x - sz.width - 6.0, y));
                lastBottom = y + sz.height;
            }
        }

        // Axis titles
        if (!xAxisTitle.empty()) {
            Size2Di sz = ctx->GetTextLineDimensions(xAxisTitle);
            double y = heatmapArea.GetBottom() + (showColumnLabels ? lineH + 8.0 : 4.0);
            ctx->DrawText(xAxisTitle, Point2Dd(heatmapArea.GetCenter().x - sz.width / 2.0, y));
        }
        if (!yAxisTitle.empty()) {
            Size2Di sz = ctx->GetTextLineDimensions(yAxisTitle);
            ctx->PushState();
            ctx->Translate(8.0, heatmapArea.GetCenter().y + sz.width / 2.0);
            ctx->Rotate(-M_PI / 2.0);
            ctx->DrawText(yAxisTitle, Point2Dd(0, 0));
            ctx->PopState();
        }
    }

    void UltraCanvasHeatmapChartElement::RenderColorBar(IRenderContext* ctx) {
        if (!showColorBar) return;
        double barX = heatmapArea.GetRight() + colorBarGap;
        double barY = heatmapArea.y;
        double barW = colorBarWidth;
        double barH = heatmapArea.height;
        if (barH < 1.0) return;

        int steps = static_cast<int>(barH);
        for (int i = 0; i < steps; ++i) {
            // Top of the bar is the maximum value.
            double t = 1.0 - static_cast<double>(i) / std::max(1, steps - 1);
            Color color = ColorAtT(t);
            ctx->DrawFilledRectangle(Rect2Dd(barX, barY + i, barW, 1.0), color);
        }

        ctx->SetStrokePaint(Color(120, 120, 120, 255));
        ctx->SetStrokeWidth(1.0);
        ctx->DrawRectangle(Rect2Dd(barX, barY, barW, barH));

        ctx->SetFontSize(10.0);
        ctx->SetTextPaint(Color(40, 40, 40, 255));
        std::string maxLabel = FormatValue(valueMax);
        std::string minLabel = FormatValue(valueMin);
        double midValue;
        if (divergingEnabled) {
            midValue = divergingMidpoint;
        } else if (scaleMode == HeatmapScale::Logarithmic) {
            midValue = std::sqrt(std::max(valueMin, 0.0) * std::max(valueMax, 0.0));
        } else {
            midValue = (valueMin + valueMax) / 2.0;
        }
        std::string midLabel = FormatValue(midValue);
        double labelX = barX + barW + 5.0;
        Size2Di sz = ctx->GetTextLineDimensions(maxLabel);
        ctx->DrawText(maxLabel, Point2Dd(labelX, barY - sz.height / 2.0));
        ctx->DrawText(midLabel, Point2Dd(labelX, barY + barH / 2.0 - sz.height / 2.0));
        ctx->DrawText(minLabel, Point2Dd(labelX, barY + barH - sz.height / 2.0));
    }

// =============================================================================
// INTERACTION
// =============================================================================

    bool UltraCanvasHeatmapChartElement::CellAtScreen(const Point2Di& pos, int& outCol, int& outRow) const {
        if (cols <= 0 || rows <= 0) return false;
        if (!heatmapArea.Contains(pos.x, pos.y)) return false;

        double cellW = heatmapArea.width / cols;
        double cellH = heatmapArea.height / rows;
        if (cellW <= 0.0 || cellH <= 0.0) return false;

        int c = static_cast<int>((pos.x - heatmapArea.x) / cellW);
        int displayRow = static_cast<int>((pos.y - heatmapArea.y) / cellH);
        c = std::clamp(c, 0, cols - 1);
        displayRow = std::clamp(displayRow, 0, rows - 1);

        outCol = c;
        outRow = (rowOrder == HeatmapRowOrder::TopDown) ? displayRow : (rows - 1 - displayRow);
        return true;
    }

    bool UltraCanvasHeatmapChartElement::HandleChartMouseMove(const Point2Di& mousePos) {
        if (!enableTooltips || cols <= 0 || rows <= 0) {
            HideTooltip();
            return false;
        }

        int c, r;
        if (CellAtScreen(mousePos, c, r)) {
            double v = GetValue(c, r);
            std::string content = RowLabel(r) + " / " + ColumnLabel(c) + "\nValue: " + FormatValue(v);
            auto windowMousePos = MapFromLocal(Point2Df(mousePos.x, mousePos.y), nullptr);
            UltraCanvasTooltipManager::UpdateAndShowTooltip(this->window, content,
                                                            Point2Di(windowMousePos.x, windowMousePos.y));
            isTooltipActive = true;
            return true;
        }

        HideTooltip();
        return false;
    }

} // namespace UltraCanvas
