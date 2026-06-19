// Plugins/Charts/UltraCanvasHexbinChart.cpp
// Hexagonal heatmap / hexbin chart.
// Version: 1.0.0
// Last Modified: 2026-06-18
// Author: UltraCanvas Framework
#include "Plugins/Charts/UltraCanvasHexbinChart.h"
#include "UltraCanvasTooltipManager.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace UltraCanvas {

    UltraCanvasHexbinChartElement::UltraCanvasHexbinChartElement(
            const std::string& id, int x, int y, int width, int height)
            : UltraCanvasHeatmapChartElement(id, x, y, width, height) {
        SetColormap(HeatmapColormap::Turbo);
        SetShowColorBar(true);
        SetShowRowLabels(false);
        SetShowColumnLabels(false);
        SetNaNColor(Colors::Transparent);
    }

    void UltraCanvasHexbinChartElement::SetHexBorders(bool on, const Color& color, float width) {
        showHexBorders = on;
        hexBorderColor = color;
        hexBorderWidth = std::max(0.0f, width);
        RequestRedraw();
    }

    void UltraCanvasHexbinChartElement::SetBinnedData(const std::vector<Point2Dd>& points,
                                                      int cols, int rows,
                                                      double xMin, double xMax,
                                                      double yMin, double yMax) {
        if (cols <= 0 || rows <= 0 || xMax <= xMin || yMax <= yMin) return;

        std::vector<double> vals(static_cast<size_t>(cols) * rows, 0.0);
        HexLayout dataLayout = MakeHexLayout(xMin, yMin, xMax - xMin, yMax - yMin, cols, rows);
        for (const auto& p : points) {
            int c, r;
            if (HexNearestCell(dataLayout, p.x, p.y, c, r)) {
                vals[static_cast<size_t>(r) * cols + c] += 1.0;
            }
        }
        SetData(vals, cols, rows);
        SetAutoValueRange(true);
    }

    void UltraCanvasHexbinChartElement::DrawHexagon(IRenderContext* ctx, double cx, double cy,
                                                    double size, const Color& fill) {
        std::vector<Point2Dd> pts;
        pts.reserve(6);
        for (int i = 0; i < 6; ++i) {
            double a = (60.0 * i - 30.0) * M_PI / 180.0; // pointy-top
            pts.push_back(Point2Dd(cx + size * std::cos(a), cy + size * std::sin(a)));
        }
        if (fill.a > 0) {
            ctx->SetFillPaint(fill);
            ctx->FillLinePath(pts);
        }
        if (showHexBorders && hexBorderWidth > 0.0f) {
            ctx->SetStrokePaint(hexBorderColor);
            ctx->SetStrokeWidth(hexBorderWidth);
            ctx->DrawLinePath(pts, true);
        }
    }

    void UltraCanvasHexbinChartElement::RenderChart(IRenderContext* ctx) {
        int cols = GetColumns();
        int rows = GetRows();
        if (!ctx || cols <= 0 || rows <= 0) return;

        const ChartPlotArea& area = GetHeatmapArea();
        layout = MakeHexLayout(area.x, area.y, area.width, area.height, cols, rows);

        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                double v = GetValue(c, r);
                Color color = MapValueToColor(v);
                if (color.a == 0) continue; // NaN / fully transparent
                double cx, cy;
                HexCenter(layout, c, r, cx, cy);
                DrawHexagon(ctx, cx, cy, layout.size, color);
            }
        }

        RenderGridLabels(ctx);
        RenderColorBar(ctx);
    }

    bool UltraCanvasHexbinChartElement::HandleChartMouseMove(const Point2Di& mousePos) {
        if (!enableTooltips || GetColumns() <= 0 || GetRows() <= 0) {
            HideTooltip();
            return false;
        }

        int c, r;
        if (HexNearestCell(layout, mousePos.x, mousePos.y, c, r)) {
            double v = GetValue(c, r);
            if (std::isnan(v)) {
                HideTooltip();
                return false;
            }
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%.4g", v);
            std::string content = "Cell (" + std::to_string(c) + ", " + std::to_string(r) + ")"
                                  + "\nValue: " + buf;
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
