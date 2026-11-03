// include/Plugins/Charts/Engine/Charts/UltraCanvasTernaryChart.h
// The ternary chart: compositions of three parts as points in an equilateral
// triangle, on the chart engine's ternary projection.
//
// The data is a TernaryModel (UltraCanvasTernaryModel.h). Each point's raw
// amounts are divided by their sum, so the three shares always add up to 1.
// The chart draws:
//
//   * the triangle, its grid (lines of equal share, parallel to the edge
//     opposite each corner) and three edge axes with ticks every 10 %;
//   * tick labels on the edge each axis reads along, clockwise or
//     anticlockwise round the triangle (TernaryReading), with each tick drawn
//     along the grid line it belongs to, so the eye follows the right line;
//   * a title at each corner and along each labelled edge, with an arrow
//     showing which way the share grows;
//   * the points of each series as markers, with a legend that hides a
//     series, tooltips giving all three shares and the raw amounts, and
//     optional point labels through the engine's label plan (a label that
//     cannot sit near its point is left to the tooltip).
//
// Design: Docs/UltraCanvas/UltraCanvasTernaryChartProposal.md
// Guide:  Docs/UltraCanvas/UltraCanvasTernaryChart.md
//
// Version: 1.0.0
// Last Modified: 2026-11-03
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/Engine/UltraCanvasChartEngineElement.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasTernaryModel.h"
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace UltraCanvas {

class UltraCanvasTernaryChart : public UltraCanvasChartEngineElement {
public:
    UltraCanvasTernaryChart(const std::string& id, int x = 0, int y = 0, int w = 0, int h = 0);

    // ---- data ----------------------------------------------------------------
    void SetModel(const TernaryModel& model);
    const TernaryModel& GetModel() const { return model; }

    // ---- presentation --------------------------------------------------------
    void SetReading(TernaryReading reading);       // also in the model
    void SetShowGrid(bool show);
    // The axis title along each labelled edge, with its direction arrow. The
    // corner titles are always drawn.
    void SetShowEdgeTitles(bool show);
    // Each point's label next to it, through the label plan (crowded labels
    // are dropped, never overlapped).
    void SetShowPointLabels(bool show);
    void SetSeriesVisible(size_t seriesIndex, bool visible);
    bool IsSeriesVisible(size_t seriesIndex) const;

    // A click on a point: its series and its index in that series.
    std::function<void(size_t seriesIndex, size_t pointIndex)> onPointClick;

    // ---- geometry (valid after the first render) -------------------------------
    // A corner: 0 = A (apex), 1 = B (bottom left), 2 = C (bottom right).
    Point2Dd CornerPoint(int corner) const;
    // Where a composition is drawn. The shares need not be normalised.
    Point2Dd SharesToScreen(double a, double b, double c) const;
    // The shares at a screen point; false outside the triangle.
    bool ScreenToShares(const Point2Dd& screen, double& a, double& b, double& c) const;
    // Where the tick for `share` of a corner's part sits on its labelled edge.
    Point2Dd TickPosition(int corner, double share) const;
    // A point's centre on screen; NaN when it has no shares or its series is hidden.
    Point2Dd PointPosition(size_t seriesIndex, size_t pointIndex) const;
    // The hovered point, {SIZE_MAX, SIZE_MAX} for none.
    std::pair<size_t, size_t> GetHoveredPoint() const;
    std::string BuildPointTooltip(size_t seriesIndex, size_t pointIndex) const;

    // ---- engine contract -------------------------------------------------------
    void DescribeAxes(ChartAxisSet& axes) override;
    void MeasureContent(IRenderContext* ctx, ChartLayoutRequest& request) override;
    void RenderChartContent(IRenderContext* ctx, const ChartEngineFrame& frame) override;
    void CollectChartLabels(IRenderContext* ctx, ChartLabelBroker& broker) override;
    void OnChartThemeChanged() override;
    void OnLegendEntryToggled(size_t entryIndex, bool enabled) override;
    bool OnEvent(const UCEvent& event) override;

protected:
    void RenderEngineGrid(IRenderContext* ctx) override;
    void RenderEngineAxes(IRenderContext* ctx) override;
    void RenderEngineTitle(IRenderContext* ctx) override;
    // Point labels the solver had to park far from their point are left
    // out: across a crowded triangle a long leader reads worse than none,
    // and the name is still in the point's tooltip.
    void RenderPlannedLabels(IRenderContext* ctx) override;

private:
    TernaryModel model;
    std::vector<bool> seriesHidden;
    bool showGridLines = true;
    bool showEdgeTitles = true;
    bool showPointLabels = false;
    float cornerFontSize = 12.5f;

    void RefreshLegend();
    Color SeriesColor(size_t seriesIndex) const;
    const ChartTernaryProjection* Projection() const;
    // Shares moved towards the centroid while the entrance animation runs.
    ChartNormalizedPoint Normalized(double sa, double sb, double sc, double progress) const;
    // The outward direction of a corner's ticks: along its grid lines, away
    // from the triangle across its labelled edge.
    Point2Dd TickDirection(int corner) const;
    std::string EdgeTitle(int corner) const;
    // Where a corner's title goes, for a title of the given size.
    Rect2Dd CornerTitleBox(int corner, const Size2Di& size, double labelBand) const;
    // The furniture around the triangle, laid out once and shared by the
    // draw pass and the label plan, which steers point labels around it.
    struct TickLabel { std::string text; Point2Dd tick, tickEnd; Rect2Dd box; };
    struct EdgeTitleLayout { std::string text; Point2Dd centre; double angle = 0.0; Size2Di size; Rect2Dd bounds; };
    struct Furniture {
        std::vector<TickLabel> ticks;
        std::vector<EdgeTitleLayout> edgeTitles;
        std::vector<std::pair<std::string, Rect2Dd>> cornerTitles;
        double band = 0.0;                 // how far the tick labels reach out
    };
    Furniture LayoutFurniture(IRenderContext* ctx) const;
    double SubtitleHeight(IRenderContext* ctx) const;
    static int64_t RegionId(size_t seriesIndex, size_t pointIndex) {
        return static_cast<int64_t>(seriesIndex) * 65536 + static_cast<int64_t>(pointIndex);
    }
};

inline std::shared_ptr<UltraCanvasTernaryChart> CreateTernaryChart(
        const std::string& id, int x = 0, int y = 0, int w = 0, int h = 0) {
    return std::make_shared<UltraCanvasTernaryChart>(id, x, y, w, h);
}

} // namespace UltraCanvas
