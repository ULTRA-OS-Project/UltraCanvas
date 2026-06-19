// include/Plugins/Charts/UltraCanvasHexbinChart.h
// Hexagonal heatmap / hexbin chart: a grid of pointy-top hexagons whose colour
// encodes a value. Reuses the heatmap element's data, colour-mapping, value
// range and colour bar; only the cell geometry differs. Also supports binning a
// set of 2D points into hex counts (hexbin density plot).
//
// Version: 1.0.0
// Last Modified: 2026-06-18
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/UltraCanvasHeatmapChart.h"
#include "Plugins/Charts/UltraCanvasHexLayout.h"
#include <vector>

namespace UltraCanvas {

    class UltraCanvasHexbinChartElement : public UltraCanvasHeatmapChartElement {
    private:
        bool showHexBorders = true;
        Color hexBorderColor = Color(255, 255, 255, 200);
        float hexBorderWidth = 1.0f;
        HexLayout layout;   // cached from the last render (for hit-testing)

    public:
        UltraCanvasHexbinChartElement(const std::string& id, int x, int y, int width, int height);

        void SetHexBorders(bool on, const Color& color = Color(255, 255, 255, 200), float width = 1.0f);

        // Bin 2D points into a cols x rows hex grid (counts per hexagon) over the
        // given data bounds, and use the counts as the heatmap values.
        void SetBinnedData(const std::vector<Point2Dd>& points, int cols, int rows,
                           double xMin, double xMax, double yMin, double yMax);

        // ---- Overrides ----
        void RenderChart(IRenderContext* ctx) override;
        bool HandleChartMouseMove(const Point2Di& mousePos) override;

    private:
        void DrawHexagon(IRenderContext* ctx, double cx, double cy, double size, const Color& fill);
    };

    inline std::shared_ptr<UltraCanvasHexbinChartElement> CreateHexbinChartElement(
            const std::string& id, int x, int y, int width, int height) {
        return std::make_shared<UltraCanvasHexbinChartElement>(id, x, y, width, height);
    }

} // namespace UltraCanvas
