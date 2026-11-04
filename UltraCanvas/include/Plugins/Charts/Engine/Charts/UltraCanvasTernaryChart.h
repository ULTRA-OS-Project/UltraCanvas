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
//     cannot sit near its point is left to the tooltip);
//   * the model's regions under the grid, each named at its widest inside
//     point, with hover, a tooltip and onRegionClick; a point's tooltip says
//     which region it is in;
//   * paths (a composition changing over time) with arrowheads, bubbles
//     sized by a fourth value, and reference curves such as the
//     Hardy-Weinberg parabola, in the legend with the series;
//   * on request, a reading guide: the three grid lines through the
//     pointer, drawn to the edges that carry their numbers, with the shares;
//   * a zoom to a sub-triangle (the axes' minimums), chosen by dragging
//     across the triangle and undone by a double click;
//   * on request, density: the triangle cut into small triangles coloured
//     by how many points fall in each, with contour lines and a colour bar.
//
// Design: Docs/UltraCanvas/UltraCanvasTernaryChartProposal.md
// Guide:  Docs/UltraCanvas/UltraCanvasTernaryChart.md
//
// Version: 1.2.0 - Phase 3: the sub-triangle zoom with drag-to-zoom and a
//   double-click reset, density by triangular binning, contour lines
// Version: 1.1.0 - Phase 2: regions, paths, bubbles, curves, the reading guide,
//   region hover and clicks
// Version: 1.0.0
// Last Modified: 2026-11-05
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/Engine/UltraCanvasChartEngineElement.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasTernaryModel.h"
#include "Plugins/Charts/UltraCanvasColormap.h"
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
    void SetCurveVisible(size_t curveIndex, bool visible);
    bool IsCurveVisible(size_t curveIndex) const;
    // Each region's name inside it, where it fits (it is always in the
    // region's tooltip).
    void SetShowRegionLabels(bool show);
    // The three grid lines through the pointer, to the edges that carry their
    // numbers, with the pointer's shares (and region) beside it.
    void SetReadingGuide(bool show);
    bool GetReadingGuide() const { return readingGuide; }
    // The bubble radii, in pixels, for the smallest and the largest point
    // size; points without a size keep their series' marker radius.
    void SetBubbleRadiusRange(double smallest, double largest);

    // ---- zoom -------------------------------------------------------------------
    // Show only the sub-triangle where each part is at least its minimum
    // (shares, 0..1). Refused - false, nothing changed - when a minimum is
    // negative or the three leave less than 2 % of the triangle.
    bool SetZoom(double minimumA, double minimumB, double minimumC);
    void ResetZoom() { SetZoom(0.0, 0.0, 0.0); }
    bool IsZoomed() const { return model.IsZoomed(); }
    // Dragging across the triangle zooms to the smallest sub-triangle holding
    // both ends of the drag; a double click shows the whole triangle again.
    // On by default.
    void SetZoomByDrag(bool enabled);
    // After every zoom change, by code, drag or double click.
    std::function<void()> onZoomChanged;

    // ---- density ------------------------------------------------------------------
    // Cut the (zoomed) triangle into subdivisions^2 small triangles and colour
    // each by how many visible points fall in it; 0 turns it off. For
    // thousands of points, where markers would hide one another.
    void SetDensity(int subdivisions);
    int GetDensity() const { return densitySubdivisions; }
    void SetDensityColormap(HeatmapColormap map);
    // Contour lines over the density at this many evenly spaced levels; 0 = none.
    void SetDensityContours(int levels);
    // The markers themselves; turn them off over a dense density view.
    void SetShowPoints(bool show);
    // Counted at the last draw: all binned points, the fullest cell, the
    // cell a composition falls in, and the contour segments drawn.
    int GetDensityTotal() const;
    int GetDensityMax() const { return densityMax; }
    int DensityCountAt(double a, double b, double c) const;
    size_t GetContourSegmentCount() const { return contourSegments.size(); }

    // A click on a point: its series and its index in that series.
    std::function<void(size_t seriesIndex, size_t pointIndex)> onPointClick;
    // A click on a region (not on a point in it): its index in the model.
    std::function<void(int regionIndex)> onRegionClick;

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
    // The hovered region (not counting a point in it), -1 for none.
    int GetHoveredRegion() const;
    std::string BuildPointTooltip(size_t seriesIndex, size_t pointIndex) const;
    std::string BuildRegionTooltip(int regionIndex) const;
    // A point's marker radius: its bubble size, or its series' radius.
    double PointRadius(size_t seriesIndex, size_t pointIndex) const;
    // Where a region's name is drawn: its widest inside point. Invalid (NaN)
    // when the name does not fit or the region has no area.
    Point2Dd RegionLabelCentre(int regionIndex) const;
    // The shares under the pointer while the reading guide is on.
    bool GetGuideShares(double& a, double& b, double& c) const;

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
    void RenderInteractionOverlay(IRenderContext* ctx, const ChartEngineFrame& frame) override;

private:
    TernaryModel model;
    std::vector<bool> seriesHidden;
    // Zoom by drag: the press, and whether it has become a drag.
    bool zoomByDrag = true;
    bool pressActive = false;
    bool selecting = false;
    Point2Dd pressScreen, dragScreen;
    double pressShares[3] = {0.0, 0.0, 0.0};
    double dragShares[3] = {0.0, 0.0, 0.0};
    // Density: counts per small triangle (upward and downward, indexed
    // i * n + j on the lattice of the zoomed triangle) and the contours.
    int densitySubdivisions = 0;
    int densityContourLevels = 0;
    HeatmapColormap densityMap = HeatmapColormap::Blues;
    bool showPoints = true;
    std::vector<int> densityUp, densityDown;
    int densityMax = 0;
    std::vector<std::pair<Point2Dd, Point2Dd>> contourSegments;   // zoomed (u, v)
    std::vector<bool> curveHidden;
    bool showGridLines = true;
    bool showRegionLabels = true;
    bool readingGuide = false;
    bool guideActive = false;
    double guideShares[3] = {0.0, 0.0, 0.0};
    double bubbleSmallest = 4.0, bubbleLargest = 18.0;
    float regionFontSize = 10.5f;
    // A region's name as drawn: on one line or wrapped at its spaces, at
    // the region font size or a step smaller - whatever fits inside.
    struct RegionLabel {
        Rect2Dd box;                         // empty = the name does not fit
        std::vector<std::string> lines;
        float size = 10.5f;
        double lineHeight = 0.0;
    };
    std::vector<RegionLabel> regionLabels;   // from the last draw
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
    // Hit-region ids of the model's regions start here, above every point's.
    static constexpr int64_t kRegionIdBase = int64_t(1) << 40;
    // A region's outline on screen; empty when it has no valid corners.
    std::vector<Point2Dd> RegionScreenOutline(int regionIndex) const;
    // A region's name laid out where it fits; an empty box otherwise.
    RegionLabel LayoutRegionLabel(IRenderContext* ctx, int regionIndex) const;
    void DrawRegions(IRenderContext* ctx);
    void DrawCurves(IRenderContext* ctx);
    // Shares of the whole triangle to the projection's (u, v) under the zoom.
    ChartNormalizedPoint ZoomedNormal(double sa, double sb, double sc) const;
    // Shares at a screen point, clamped into the zoomed triangle.
    void ClampedShares(const Point2Dd& screen, double shares[3]) const;
    // Clips later drawing to the triangle.
    void ClipToTriangle(IRenderContext* ctx) const;
    void BinDensity();
    void BuildContours();
    void DrawDensity(IRenderContext* ctx);
    // The colour bar under the triangle while density is on.
    Rect2Dd ColourBarRect() const;
    // The minimums a drag from one composition to another selects.
    void SelectionMinimums(double mins[3]) const;
};

inline std::shared_ptr<UltraCanvasTernaryChart> CreateTernaryChart(
        const std::string& id, int x = 0, int y = 0, int w = 0, int h = 0) {
    return std::make_shared<UltraCanvasTernaryChart>(id, x, y, w, h);
}

} // namespace UltraCanvas
