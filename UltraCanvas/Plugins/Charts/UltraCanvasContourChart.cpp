// Plugins/Charts/UltraCanvasContourChart.cpp
// 2D contour chart element.
// Version: 1.0.0
// Last Modified: 2026-07-29
// Author: UltraCanvas Framework
#include "Plugins/Charts/UltraCanvasContourChart.h"
#include "UltraCanvasTooltipManager.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace UltraCanvas {

    namespace {

        // Cairo ARGB32 is premultiplied and native-endian.
        uint32_t ContourColorToPixel(const Color& c) {
            uint32_t a = c.a;
            uint32_t r = static_cast<uint32_t>(c.r) * a / 255;
            uint32_t g = static_cast<uint32_t>(c.g) * a / 255;
            uint32_t b = static_cast<uint32_t>(c.b) * a / 255;
            return (a << 24) | (r << 16) | (g << 8) | b;
        }

        std::string FormatFixed(double v, int decimals) {
            if (std::isnan(v)) return "NaN";
            if (v == std::numeric_limits<double>::infinity())  return "inf";
            if (v == -std::numeric_limits<double>::infinity()) return "-inf";
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.*f", std::clamp(decimals, 0, 12), v);
            // Avoid "-0.00" for values that round to zero.
            std::string s(buf);
            if (s.find_first_not_of("-0.") == std::string::npos && s[0] == '-') {
                s.erase(s.begin());
            }
            return s;
        }

        // Cumulative arc length along a screen polyline.
        std::vector<double> ArcLengths(const std::vector<Point2Dd>& pts) {
            std::vector<double> cum(pts.size(), 0.0);
            for (size_t i = 1; i < pts.size(); ++i) {
                double dx = pts[i].x - pts[i - 1].x;
                double dy = pts[i].y - pts[i - 1].y;
                cum[i] = cum[i - 1] + std::sqrt(dx * dx + dy * dy);
            }
            return cum;
        }

        Point2Dd PointAtArc(const std::vector<Point2Dd>& pts,
                            const std::vector<double>& cum, double d) {
            if (pts.empty()) return Point2Dd(0, 0);
            if (d <= 0.0) return pts.front();
            if (d >= cum.back()) return pts.back();
            size_t i = static_cast<size_t>(
                    std::lower_bound(cum.begin(), cum.end(), d) - cum.begin());
            if (i == 0) return pts.front();
            double seg = cum[i] - cum[i - 1];
            double f = (seg > 0.0) ? (d - cum[i - 1]) / seg : 0.0;
            return Point2Dd(pts[i - 1].x + (pts[i].x - pts[i - 1].x) * f,
                            pts[i - 1].y + (pts[i].y - pts[i - 1].y) * f);
        }

        // The piece of the polyline between two arc distances, with the ends
        // interpolated so the sub-path starts and stops exactly on the cut.
        std::vector<Point2Dd> SubPath(const std::vector<Point2Dd>& pts,
                                      const std::vector<double>& cum,
                                      double dStart, double dEnd) {
            std::vector<Point2Dd> out;
            if (pts.size() < 2 || dEnd <= dStart) return out;
            dStart = std::max(0.0, dStart);
            dEnd = std::min(cum.back(), dEnd);
            if (dEnd <= dStart) return out;

            out.push_back(PointAtArc(pts, cum, dStart));
            for (size_t i = 0; i < pts.size(); ++i) {
                if (cum[i] > dStart && cum[i] < dEnd) out.push_back(pts[i]);
            }
            out.push_back(PointAtArc(pts, cum, dEnd));
            return out;
        }

    } // namespace

// =============================================================================
// CONSTRUCTION
// =============================================================================

    UltraCanvasContourChartElement::UltraCanvasContourChartElement(
            const std::string& id, int x, int y, int width, int height)
            : UltraCanvasHeatmapChartElement(id, x, y, width, height) {
        // A contour plot is read like a graph, not like a matrix: row 0 holds
        // the lowest y and belongs at the bottom.
        SetRowOrder(HeatmapRowOrder::BottomUp);
        // The base grid labels are replaced by numeric axis ticks.
        showColumnLabels = false;
        showRowLabels = false;
        // The legend is drawn by this class so it can also render bands.
        showColorBar = false;
        enableTooltips = true;
    }

// =============================================================================
// DATA
// =============================================================================

    void UltraCanvasContourChartElement::SetDataRange(double xMin, double xMax,
                                                      double yMin, double yMax) {
        dataXMin = xMin; dataXMax = xMax;
        dataYMin = yMin; dataYMax = yMax;
        dataRangeSet = true;
        InvalidateContours();
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::ClearDataRange() {
        dataRangeSet = false;
        InvalidateContours();
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetPoints(const std::vector<ContourPoint>& points) {
        sourcePoints = points;
        RebuildFromPoints();
    }

    void UltraCanvasContourChartElement::SetPoints(const std::vector<Point2Dd>& points) {
        sourcePoints.clear();
        sourcePoints.reserve(points.size());
        for (const auto& p : points) sourcePoints.emplace_back(p.x, p.y, 1.0);
        RebuildFromPoints();
    }

    void UltraCanvasContourChartElement::RebuildFromPoints() {
        if (sourcePoints.empty()) {
            ClearData();
            return;
        }
        ContourGrid g = BuildContourGridFromPoints(sourcePoints, griddingOptions);
        // Feed the base so the value range, colour map and colour bar all see
        // the same numbers the contours are computed from.
        SetData(g.values, g.cols, g.rows);
        SetDataRange(g.xMin, g.xMax, g.yMin, g.yMax);
    }

    void UltraCanvasContourChartElement::SetGriddingOptions(const ContourGriddingOptions& options) {
        griddingOptions = options;
        if (!sourcePoints.empty()) RebuildFromPoints();
    }

    void UltraCanvasContourChartElement::SetGriddingMethod(ContourGridding method) {
        griddingOptions.method = method;
        if (!sourcePoints.empty()) RebuildFromPoints();
    }

    void UltraCanvasContourChartElement::SetGridResolution(int cols, int rows) {
        griddingOptions.cols = std::max(2, cols);
        griddingOptions.rows = std::max(2, rows);
        if (!sourcePoints.empty()) RebuildFromPoints();
    }

    void UltraCanvasContourChartElement::SetKDEBandwidth(double bandwidth) {
        griddingOptions.bandwidth = bandwidth;
        if (!sourcePoints.empty()) RebuildFromPoints();
    }

    void UltraCanvasContourChartElement::SetFunction(
            const std::function<double(double, double)>& fn,
            int cols, int rows, double xMin, double xMax, double yMin, double yMax) {
        if (!fn || cols < 2 || rows < 2) return;
        std::vector<double> v(static_cast<size_t>(cols) * rows, 0.0);
        for (int r = 0; r < rows; ++r) {
            double y = yMin + (yMax - yMin) * r / (rows - 1);
            for (int c = 0; c < cols; ++c) {
                double x = xMin + (xMax - xMin) * c / (cols - 1);
                v[static_cast<size_t>(r) * cols + c] = fn(x, y);
            }
        }
        sourcePoints.clear();
        SetData(v, cols, rows);
        SetDataRange(xMin, xMax, yMin, yMax);
    }

// =============================================================================
// LEVELS
// =============================================================================

    void UltraCanvasContourChartElement::SetLevelCount(int count) {
        levelMode = ContourLevelMode::Count;
        levelCount = std::max(1, count);
        InvalidateContours();
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetLevelStep(double step, double origin) {
        levelMode = ContourLevelMode::Step;
        levelStep = step;
        levelOrigin = origin;
        InvalidateContours();
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetLevels(const std::vector<double>& values) {
        levelMode = ContourLevelMode::Explicit;
        explicitLevels = values;
        InvalidateContours();
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetQuantileLevels(int count) {
        levelMode = ContourLevelMode::Quantile;
        levelCount = std::max(1, count);
        InvalidateContours();
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetNiceLevels(bool on) {
        niceLevels = on;
        InvalidateContours();
        RequestRedraw();
    }

    const std::vector<double>& UltraCanvasContourChartElement::GetLevels() const {
        RebuildContours();
        return levels;
    }

    const std::vector<ContourPolyline>& UltraCanvasContourChartElement::GetContours() const {
        RebuildContours();
        return polylines;
    }

// =============================================================================
// FIELD PREPARATION
// =============================================================================

    void UltraCanvasContourChartElement::SetGridSmoothing(double sigmaCells) {
        gridSmoothing = std::max(0.0, sigmaCells);
        InvalidateContours();
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetLineSmoothing(int chaikinIterations) {
        lineSmoothing = std::clamp(chaikinIterations, 0, 5);
        InvalidateContours();
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetMinContourLength(double gridUnits) {
        minContourLength = std::max(0.0, gridUnits);
        InvalidateContours();
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetUpsampleFactor(int factor) {
        upsampleFactor = std::clamp(factor, 1, 8);
        InvalidateContours();
        RequestRedraw();
    }

// =============================================================================
// RENDER MODE / STYLE
// =============================================================================

    void UltraCanvasContourChartElement::SetRenderMode(ContourRenderMode mode) {
        contourMode = mode;
        fillPixmapValid = false;
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetLineColor(const Color& c) {
        lineColor = c;
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetLineColorMode(ContourLineColorMode mode) {
        lineColorMode = mode;
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetLineWidth(float width) {
        lineWidth = std::max(0.1f, width);
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetNegativeLevelsDashed(bool on) {
        negativeLevelsDashed = on;
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetNegativeDashPattern(const UCDashPattern& pattern) {
        negativeDash = pattern;
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetMajorLineEvery(int nth, float width) {
        majorLineEvery = std::max(0, nth);
        majorLineWidth = std::max(0.1f, width);
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetHighlightZeroLevel(bool on, const Color& c, float width) {
        highlightZeroLevel = on;
        zeroLevelColor = c;
        zeroLevelWidth = std::max(0.1f, width);
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetFillAlpha(double alpha) {
        fillAlpha = std::clamp(alpha, 0.0, 1.0);
        fillPixmapValid = false;
        RequestRedraw();
    }

// =============================================================================
// INLINE LABELS
// =============================================================================

    void UltraCanvasContourChartElement::SetShowLineLabels(bool on) {
        showLineLabels = on;
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetLabelDecimals(int decimals) {
        labelDecimals = std::clamp(decimals, 0, 12);
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetLabelFontSize(float size) {
        labelFontSize = std::max(1.0f, size);
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetLabelTextColor(const Color& c) {
        labelTextColor = c;
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetLabelBreaksLine(bool on) {
        labelBreaksLine = on;
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetLabelHalo(bool on, const Color& c) {
        labelHalo = on;
        labelHaloColor = c;
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetLabelSpacing(double pixels) {
        labelSpacing = std::max(0.0, pixels);
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetMinLabelLength(double pixels) {
        minLabelLength = std::max(0.0, pixels);
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetLabelMajorOnly(bool on) {
        labelMajorOnly = on;
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetLabelFormatter(ValueFormatter fn) {
        labelFormatter = std::move(fn);
        RequestRedraw();
    }

// =============================================================================
// LEGEND / AXES / POINTS
// =============================================================================

    void UltraCanvasContourChartElement::SetLegendMode(ContourLegendMode mode) {
        legendMode = mode;
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetLegendTitle(const std::string& title) {
        legendTitle = title;
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetIntervalFormat(ContourIntervalFormat fmt) {
        intervalFormat = fmt;
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetLegendDecimals(int decimals) {
        legendDecimals = std::clamp(decimals, 0, 12);
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetShowAxisTicks(bool on) {
        showAxisTicks = on;
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetTickCounts(int xTicks, int yTicks) {
        xTickCount = std::max(2, xTicks);
        yTickCount = std::max(2, yTicks);
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetXTickFormatter(ValueFormatter fn) {
        xTickFormatter = std::move(fn);
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetYTickFormatter(ValueFormatter fn) {
        yTickFormatter = std::move(fn);
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetCategoryLabelsX(const std::vector<std::string>& labels) {
        columnLabels = labels;
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetCategoryLabelsY(const std::vector<std::string>& labels) {
        rowLabels = labels;
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetShowSourcePoints(bool on) {
        showSourcePoints = on;
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetSourcePointStyle(const Color& c, float radius) {
        sourcePointColor = c;
        sourcePointRadius = std::max(0.1f, radius);
        RequestRedraw();
    }

// =============================================================================
// VIEW WINDOW (ZOOM / PAN)
// =============================================================================

    void UltraCanvasContourChartElement::SetViewRange(double xMin, double xMax,
                                                      double yMin, double yMax) {
        if (!(xMax > xMin) || !(yMax > yMin)) return;
        viewXMin = xMin; viewXMax = xMax;
        viewYMin = yMin; viewYMax = yMax;
        viewActive = true;
        ClampViewToData();
        fillPixmapValid = false;
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::ResetView() {
        if (!viewActive) return;
        viewActive = false;
        fillPixmapValid = false;
        RequestRedraw();
    }

    bool UltraCanvasContourChartElement::GetViewRange(double& xMin, double& xMax,
                                                      double& yMin, double& yMax) const {
        EffectiveViewRange(xMin, xMax, yMin, yMax);
        return viewActive;
    }

    void UltraCanvasContourChartElement::ClampViewToData() {
        RebuildContours();
        if (!field.Valid()) return;

        double fullX = field.xMax - field.xMin;
        double fullY = field.yMax - field.yMin;
        double minSpanX = fullX / maxZoomFactor;
        double minSpanY = fullY / maxZoomFactor;

        double spanX = std::clamp(viewXMax - viewXMin, minSpanX, fullX);
        double spanY = std::clamp(viewYMax - viewYMin, minSpanY, fullY);

        // Keep the centre, then slide the window back inside the field.
        double cx = (viewXMin + viewXMax) * 0.5;
        double cy = (viewYMin + viewYMax) * 0.5;
        viewXMin = std::clamp(cx - spanX * 0.5, field.xMin, field.xMax - spanX);
        viewXMax = viewXMin + spanX;
        viewYMin = std::clamp(cy - spanY * 0.5, field.yMin, field.yMax - spanY);
        viewYMax = viewYMin + spanY;

        // Fully zoomed out is the same as no view at all.
        if (spanX >= fullX * 0.9999 && spanY >= fullY * 0.9999) {
            viewActive = false;
        }
    }

    void UltraCanvasContourChartElement::ApplyZoom(double factor, double focusX, double focusY) {
        RebuildContours();
        if (!field.Valid()) return;

        double vx0, vx1, vy0, vy1;
        EffectiveViewRange(vx0, vx1, vy0, vy1);

        // Scale the window about the focus point so the data under the cursor
        // stays under the cursor.
        viewXMin = focusX - (focusX - vx0) * factor;
        viewXMax = focusX + (vx1 - focusX) * factor;
        viewYMin = focusY - (focusY - vy0) * factor;
        viewYMax = focusY + (vy1 - focusY) * factor;
        viewActive = true;
        ClampViewToData();
        fillPixmapValid = false;
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::ApplyPan(double dxData, double dyData) {
        RebuildContours();
        if (!field.Valid() || !viewActive) return;

        double spanX = viewXMax - viewXMin;
        double spanY = viewYMax - viewYMin;
        viewXMin = std::clamp(viewXMin + dxData, field.xMin, field.xMax - spanX);
        viewXMax = viewXMin + spanX;
        viewYMin = std::clamp(viewYMin + dyData, field.yMin, field.yMax - spanY);
        viewYMax = viewYMin + spanY;
        fillPixmapValid = false;
        RequestRedraw();
    }

// =============================================================================
// INTERACTION SETTERS
// =============================================================================

    void UltraCanvasContourChartElement::SetShowCrosshair(bool on) {
        showCrosshair = on;
        if (!on) crosshairLive = false;
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetCrosshairColor(const Color& c) {
        crosshairColor = c;
        RequestRedraw();
    }

    void UltraCanvasContourChartElement::SetOnContourClick(ContourClickHandler handler) {
        onContourClick = std::move(handler);
    }

// =============================================================================
// PIPELINE
// =============================================================================

    void UltraCanvasContourChartElement::InvalidateRaster() {
        UltraCanvasHeatmapChartElement::InvalidateRaster();
        InvalidateContours();
    }

    void UltraCanvasContourChartElement::InvalidateContours() {
        contourValid = false;
        fillPixmapValid = false;
    }

    ContourGrid UltraCanvasContourChartElement::BuildFieldFromBase() const {
        ContourGrid g;
        int cols = GetColumns();
        int rows = GetRows();
        if (cols < 2 || rows < 2) return g;

        g.Resize(cols, rows);
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                g.Set(c, r, GetValue(c, r));
            }
        }
        if (dataRangeSet) {
            g.SetExtents(dataXMin, dataXMax, dataYMin, dataYMax);
        } else {
            g.SetExtents(0.0, cols - 1.0, 0.0, rows - 1.0);
        }
        return g;
    }

    void UltraCanvasContourChartElement::RebuildContours() const {
        if (contourValid) return;
        contourValid = true;

        field = BuildFieldFromBase();
        levels.clear();
        polylines.clear();
        if (!field.Valid()) return;

        if (upsampleFactor > 1) {
            int nc = (field.cols - 1) * upsampleFactor + 1;
            int nr = (field.rows - 1) * upsampleFactor + 1;
            // Keep the refined lattice within a sane working size.
            if (static_cast<long long>(nc) * nr <= 4000000LL) {
                field = ResampleContourGrid(field, nc, nr);
            }
        }
        if (gridSmoothing > 0.0) {
            SmoothContourGrid(field, gridSmoothing);
        }

        levels = GenerateContourLevels(field, levelMode, levelCount, levelStep,
                                       levelOrigin, explicitLevels, niceLevels);

        polylines = ExtractContourPolylines(field, levels);
        if (minContourLength > 0.0) {
            CullShortPolylines(polylines, minContourLength);
        }
        if (lineSmoothing > 0) {
            for (auto& p : polylines) SmoothContourPolyline(p, lineSmoothing);
        }
    }

// =============================================================================
// GEOMETRY MAPPING
// =============================================================================

    bool UltraCanvasContourChartElement::UsesCellAlignedGrid() const {
        // Only the heatmap-cell presentation puts samples at cell centres; the
        // pure contour modes stretch the node lattice over the whole plot area.
        return contourMode == ContourRenderMode::HeatmapWithContours;
    }

    bool UltraCanvasContourChartElement::ViewZoomable() const {
        // The heatmap-cell presentation is laid out by the base class over the
        // whole matrix, so the view window (and with it zoom/pan) only applies
        // to the lattice-mapped contour modes.
        return !UsesCellAlignedGrid();
    }

    void UltraCanvasContourChartElement::EffectiveViewRange(double& xMin, double& xMax,
                                                            double& yMin, double& yMax) const {
        if (field.Valid()) {
            xMin = field.xMin; xMax = field.xMax;
            yMin = field.yMin; yMax = field.yMax;
        } else {
            xMin = dataXMin; xMax = dataXMax;
            yMin = dataYMin; yMax = dataYMax;
        }
        if (viewActive && ViewZoomable()) {
            xMin = viewXMin; xMax = viewXMax;
            yMin = viewYMin; yMax = viewYMax;
        }
    }

    Point2Dd UltraCanvasContourChartElement::GridToScreen(double col, double row) const {
        const ChartPlotArea& area = GetHeatmapArea();
        if (field.cols < 2 || field.rows < 2) {
            return Point2Dd(area.x, area.y);
        }

        if (UsesCellAlignedGrid()) {
            double u = col / (field.cols - 1);
            double displayRow = (GetRowOrder() == HeatmapRowOrder::TopDown)
                                ? row : (field.rows - 1 - row);
            double v = displayRow / (field.rows - 1);
            int baseCols = std::max(1, GetColumns());
            int baseRows = std::max(1, GetRows());
            double cw = area.width / baseCols;
            double ch = area.height / baseRows;
            return Point2Dd(area.x + (0.5 + u * (baseCols - 1)) * cw,
                            area.y + (0.5 + v * (baseRows - 1)) * ch);
        }

        double vx0, vx1, vy0, vy1;
        EffectiveViewRange(vx0, vx1, vy0, vy1);
        double colMin = field.ColAtX(vx0), colMax = field.ColAtX(vx1);
        double rowMin = field.RowAtY(vy0), rowMax = field.RowAtY(vy1);

        double u = (colMax > colMin) ? (col - colMin) / (colMax - colMin) : 0.0;
        double rowFrac = (rowMax > rowMin) ? (row - rowMin) / (rowMax - rowMin) : 0.0;
        double v = (GetRowOrder() == HeatmapRowOrder::TopDown) ? rowFrac : 1.0 - rowFrac;
        return Point2Dd(area.x + u * area.width, area.y + v * area.height);
    }

    Point2Dd UltraCanvasContourChartElement::ScreenToGrid(double sx, double sy) const {
        const ChartPlotArea& area = GetHeatmapArea();
        if (field.cols < 2 || field.rows < 2 || area.width <= 0 || area.height <= 0) {
            return Point2Dd(0, 0);
        }

        if (UsesCellAlignedGrid()) {
            int baseCols = std::max(2, GetColumns());
            int baseRows = std::max(2, GetRows());
            double cw = area.width / baseCols;
            double ch = area.height / baseRows;
            double u = ((sx - area.x) / cw - 0.5) / (baseCols - 1);
            double v = ((sy - area.y) / ch - 0.5) / (baseRows - 1);
            double col = u * (field.cols - 1);
            double displayRow = v * (field.rows - 1);
            double row = (GetRowOrder() == HeatmapRowOrder::TopDown)
                         ? displayRow : (field.rows - 1 - displayRow);
            return Point2Dd(col, row);
        }

        double vx0, vx1, vy0, vy1;
        EffectiveViewRange(vx0, vx1, vy0, vy1);
        double colMin = field.ColAtX(vx0), colMax = field.ColAtX(vx1);
        double rowMin = field.RowAtY(vy0), rowMax = field.RowAtY(vy1);

        double u = (sx - area.x) / area.width;
        double v = (sy - area.y) / area.height;
        double rowFrac = (GetRowOrder() == HeatmapRowOrder::TopDown) ? v : 1.0 - v;
        return Point2Dd(colMin + u * (colMax - colMin),
                        rowMin + rowFrac * (rowMax - rowMin));
    }

// =============================================================================
// BANDS
// =============================================================================

    size_t UltraCanvasContourChartElement::BandCount() const {
        return levels.size() + 1;
    }

    void UltraCanvasContourChartElement::BandRange(size_t bandIndex, double& lo, double& hi) const {
        double vmin = GetValueMin();
        double vmax = GetValueMax();
        lo = (bandIndex == 0) ? vmin : levels[bandIndex - 1];
        hi = (bandIndex >= levels.size()) ? vmax : levels[bandIndex];
    }

    Color UltraCanvasContourChartElement::BandColor(size_t bandIndex) const {
        double lo, hi;
        BandRange(bandIndex, lo, hi);
        Color c = MapValueToColor((lo + hi) * 0.5);
        if (fillAlpha < 1.0) {
            c.a = static_cast<uint8_t>(std::clamp(c.a * fillAlpha, 0.0, 255.0));
        }
        return c;
    }

    size_t UltraCanvasContourChartElement::BandIndexOf(double value) const {
        RebuildContours();
        if (std::isnan(value)) return 0;
        return static_cast<size_t>(
                std::upper_bound(levels.begin(), levels.end(), value) - levels.begin());
    }

    std::string UltraCanvasContourChartElement::BandIntervalText(size_t bandIndex) const {
        double lo, hi;
        BandRange(bandIndex, lo, hi);
        std::string a = FormatLegendValue(lo);
        std::string b = FormatLegendValue(hi);
        switch (intervalFormat) {
            case ContourIntervalFormat::Dash: return a + " - " + b;
            case ContourIntervalFormat::To:   return a + " to " + b;
            case ContourIntervalFormat::Brackets:
            default:                          return "[" + a + ", " + b + "]";
        }
    }

// =============================================================================
// FORMATTING
// =============================================================================

    std::string UltraCanvasContourChartElement::FormatLabelValue(double v) const {
        if (labelFormatter) return labelFormatter(v);
        return FormatFixed(v, labelDecimals);
    }

    std::string UltraCanvasContourChartElement::FormatLegendValue(double v) const {
        return FormatFixed(v, legendDecimals);
    }

    std::string UltraCanvasContourChartElement::FormatTick(double v, const ValueFormatter& fn) const {
        if (fn) return fn(v);
        double mag = std::max(std::fabs(v), 1e-12);
        int decimals = (mag >= 1000.0) ? 0 : (mag >= 10.0 ? 1 : 2);
        return FormatFixed(v, decimals);
    }

// =============================================================================
// LEVEL STYLE RESOLUTION
// =============================================================================

    int UltraCanvasContourChartElement::LevelIndexOf(double level) const {
        for (size_t i = 0; i < levels.size(); ++i) {
            if (levels[i] == level) return static_cast<int>(i);
        }
        return -1;
    }

    bool UltraCanvasContourChartElement::IsMajorLevel(int levelIndex) const {
        if (majorLineEvery <= 0 || levelIndex < 0) return false;
        return (levelIndex % majorLineEvery) == 0;
    }

    Color UltraCanvasContourChartElement::ResolveLineColor(double level) const {
        if (highlightZeroLevel && level == 0.0) return zeroLevelColor;
        if (lineColorMode == ContourLineColorMode::FromLevel) {
            return MapValueToColor(level);
        }
        return lineColor;
    }

    float UltraCanvasContourChartElement::ResolveLineWidth(int levelIndex, double level) const {
        if (highlightZeroLevel && level == 0.0) return zeroLevelWidth;
        if (IsMajorLevel(levelIndex)) return majorLineWidth;
        return lineWidth;
    }

    bool UltraCanvasContourChartElement::ResolveLineDashed(double level) const {
        return negativeLevelsDashed && level < 0.0;
    }

// =============================================================================
// LAYOUT
// =============================================================================

    void UltraCanvasContourChartElement::MeasureLegend(IRenderContext* ctx,
                                                       double& outW, double& outH) const {
        outW = 0.0;
        outH = 0.0;
        if (legendMode == ContourLegendMode::NoLegend) return;

        ctx->SetFontSize(10.0);
        int lineH = ctx->GetTextLineHeight("0");

        if (legendMode == ContourLegendMode::ColorBar) {
            double w = std::max(ctx->GetTextLineWidth(FormatLegendValue(GetValueMax())),
                                ctx->GetTextLineWidth(FormatLegendValue(GetValueMin())));
            outW = legendGap + colorBarWidth + 6.0 + w + 4.0;
            return;
        }

        double textW = 0.0;
        for (size_t b = 0; b < BandCount(); ++b) {
            textW = std::max(textW, static_cast<double>(ctx->GetTextLineWidth(BandIntervalText(b))));
        }
        if (!legendTitle.empty()) {
            textW = std::max(textW, static_cast<double>(ctx->GetTextLineWidth(legendTitle)));
        }

        if (legendMode == ContourLegendMode::DiscreteBands) {
            outW = legendGap + legendSwatchSize + 6.0 + textW + 4.0;
        } else {
            // Horizontal: one row of swatches, plus a title line above it.
            outH = legendGap + lineH + 6.0 + (legendTitle.empty() ? 0.0 : lineH + 2.0);
        }
    }

    ChartPlotArea UltraCanvasContourChartElement::ComputeHeatmapArea(IRenderContext* ctx) {
        // The contour chart lays itself out rather than reusing the heatmap's
        // matrix-style margins: it needs room for numeric ticks and its own
        // legend variants.
        RebuildContours();

        double left = 12.0, right = 12.0, top = 10.0, bottom = 10.0;

        ctx->SetFontSize(11.0);
        int lineH = ctx->GetTextLineHeight("0");

        if (!chartTitle.empty()) top += lineH + 12.0;

        bool categoricalY = !rowLabels.empty();
        bool categoricalX = !columnLabels.empty();

        double vx0, vx1, vy0, vy1;
        EffectiveViewRange(vx0, vx1, vy0, vy1);

        if (showAxisTicks || categoricalY) {
            double w = 0.0;
            if (categoricalY) {
                for (const auto& s : rowLabels) {
                    w = std::max(w, static_cast<double>(ctx->GetTextLineWidth(s)));
                }
            } else {
                w = std::max(ctx->GetTextLineWidth(FormatTick(vy0, yTickFormatter)),
                             ctx->GetTextLineWidth(FormatTick(vy1, yTickFormatter)));
            }
            left += w + 10.0;
        }
        if (!yAxisTitle.empty()) left += lineH + 6.0;

        if (showAxisTicks || categoricalX) bottom += lineH + 8.0;
        if (!xAxisTitle.empty()) bottom += lineH + 4.0;

        MeasureLegend(ctx, legendReservedW, legendReservedH);
        right += legendReservedW;
        bottom += legendReservedH;

        double w = std::max(1.0, static_cast<double>(GetWidth()) - left - right);
        double h = std::max(1.0, static_cast<double>(GetHeight()) - top - bottom);
        return ChartPlotArea(left, top, w, h);
    }

// =============================================================================
// RENDERING
// =============================================================================

    void UltraCanvasContourChartElement::RenderChart(IRenderContext* ctx) {
        if (!ctx || GetColumns() <= 0 || GetRows() <= 0) return;
        RebuildContours();

        const ChartPlotArea& area = GetHeatmapArea();

        if (contourMode == ContourRenderMode::HeatmapWithContours) {
            // Reuse the base cell renderer; it also draws the plot border and
            // calls RenderGridLabels (overridden below for numeric ticks).
            UltraCanvasHeatmapChartElement::RenderChart(ctx);
        } else {
            if (contourMode != ContourRenderMode::Lines) {
                RenderFillRaster(ctx, contourMode != ContourRenderMode::Smooth);
            }
            ctx->SetStrokePaint(frameColor);
            ctx->SetStrokeWidth(1.0);
            ctx->DrawRectangle(area.ToRect2D());
            RenderGridLabels(ctx);
        }

        if (showSourcePoints) RenderSourcePoints(ctx);

        if (contourMode != ContourRenderMode::Filled) {
            RenderIsolines(ctx);
        }

        if (showCrosshair && crosshairLive) RenderCrosshair(ctx);

        switch (legendMode) {
            case ContourLegendMode::ColorBar: {
                // The base colour bar honours its own flag; enable it just for
                // this call so the reserved gutter is filled.
                bool saved = showColorBar;
                showColorBar = true;
                int savedGap = colorBarGap;
                colorBarGap = legendGap;
                RenderColorBar(ctx);
                colorBarGap = savedGap;
                showColorBar = saved;
                break;
            }
            case ContourLegendMode::DiscreteBands:
            case ContourLegendMode::DiscreteBandsHorizontal:
                RenderDiscreteLegend(ctx);
                break;
            case ContourLegendMode::NoLegend:
            default:
                break;
        }
    }

    void UltraCanvasContourChartElement::RenderFillRaster(IRenderContext* ctx, bool banded) {
        const ChartPlotArea& area = GetHeatmapArea();
        if (!field.Valid() || area.width < 1.0 || area.height < 1.0) return;

        // Rasterising the fill instead of emitting one polygon per cell keeps
        // adjacent bands seam-free and makes the continuous (Smooth) mode a
        // one-line change. The buffer is capped so a huge element does not turn
        // into a huge allocation.
        const int maxDim = 2048;
        int w = std::clamp(static_cast<int>(std::ceil(area.width)), 1, maxDim);
        int h = std::clamp(static_cast<int>(std::ceil(area.height)), 1, maxDim);

        if (!fillPixmapValid || fillPixmapW != w || fillPixmapH != h) {
            if (!fillPixmap) fillPixmap = std::make_shared<UCPixmap>();
            if (!fillPixmap->Init(w, h)) return;

            uint32_t* px = fillPixmap->GetPixelData();
            if (!px) return;

            // Pre-resolve the band colours; the inner loop then only needs a
            // binary search over the levels.
            std::vector<uint32_t> bandPixels;
            if (banded) {
                bandPixels.reserve(BandCount());
                for (size_t b = 0; b < BandCount(); ++b) {
                    bandPixels.push_back(ContourColorToPixel(BandColor(b)));
                }
            }
            uint32_t nanPixel = ContourColorToPixel(GetNaNColor());

            bool bottomUp = (GetRowOrder() != HeatmapRowOrder::TopDown);
            double vx0, vx1, vy0, vy1;
            EffectiveViewRange(vx0, vx1, vy0, vy1);
            double colMin = field.ColAtX(vx0);
            double rowMin = field.RowAtY(vy0);
            double colScale = (field.ColAtX(vx1) - colMin) / std::max(1, w - 1);
            double rowScale = (field.RowAtY(vy1) - rowMin) / std::max(1, h - 1);

            for (int py = 0; py < h; ++py) {
                double rowFrac = bottomUp ? (h - 1 - py) : py;
                double row = rowMin + rowFrac * rowScale;
                uint32_t* line = px + static_cast<size_t>(py) * w;
                for (int pxi = 0; pxi < w; ++pxi) {
                    double v = field.SampleBilinear(colMin + pxi * colScale, row);
                    if (std::isnan(v)) {
                        line[pxi] = nanPixel;
                        continue;
                    }
                    if (banded) {
                        size_t b = static_cast<size_t>(
                                std::upper_bound(levels.begin(), levels.end(), v) - levels.begin());
                        line[pxi] = bandPixels[std::min(b, bandPixels.size() - 1)];
                    } else {
                        Color c = MapValueToColor(v);
                        if (fillAlpha < 1.0) {
                            c.a = static_cast<uint8_t>(std::clamp(c.a * fillAlpha, 0.0, 255.0));
                        }
                        line[pxi] = ContourColorToPixel(c);
                    }
                }
            }
            fillPixmap->MarkDirty();
            fillPixmapValid = true;
            fillPixmapW = w;
            fillPixmapH = h;
        }

        if (fillPixmap && fillPixmap->IsValid()) {
            ctx->DrawPartOfPixmap(*fillPixmap, Rect2Dd(0, 0, w, h), area.ToRect2D());
        }
    }

    void UltraCanvasContourChartElement::RenderIsolines(IRenderContext* ctx) {
        if (polylines.empty()) return;

        const ChartPlotArea& area = GetHeatmapArea();
        ctx->PushState();
        ctx->ClipRect(area.ToRect2D());

        std::vector<Point2Dd> screenPts;
        for (const auto& poly : polylines) {
            if (poly.points.size() < 2) continue;
            screenPts.clear();
            screenPts.reserve(poly.points.size() + 1);
            for (const auto& p : poly.points) {
                screenPts.push_back(GridToScreen(p.x, p.y));
            }
            // Treat a closed contour as an open path that returns to its start,
            // so gap carving and label placement need only one code path.
            if (poly.closed) screenPts.push_back(screenPts.front());

            RenderOneContour(ctx, poly, screenPts);
        }

        ctx->PopState();
    }

    void UltraCanvasContourChartElement::RenderOneContour(IRenderContext* ctx,
                                                          const ContourPolyline& poly,
                                                          const std::vector<Point2Dd>& screenPts) {
        if (screenPts.size() < 2) return;

        int levelIndex = LevelIndexOf(poly.level);
        Color color = ResolveLineColor(poly.level);
        float width = ResolveLineWidth(levelIndex, poly.level);
        bool dashed = ResolveLineDashed(poly.level);

        std::vector<double> cum = ArcLengths(screenPts);
        double total = cum.back();

        // ---- decide where labels go ----
        struct LabelSpot { double d; Point2Dd pos; double angle; std::string text; double halfW; };
        std::vector<LabelSpot> spots;

        bool wantLabel = showLineLabels && total >= minLabelLength &&
                         (!labelMajorOnly || IsMajorLevel(levelIndex) ||
                          (highlightZeroLevel && poly.level == 0.0));
        if (wantLabel) {
            std::string text = FormatLabelValue(poly.level);
            ctx->SetFontSize(labelFontSize);
            Size2Di sz = ctx->GetTextLineDimensions(text);

            std::vector<double> anchors;
            if (labelSpacing > 0.0) {
                for (double d = labelSpacing * 0.5; d < total; d += labelSpacing) {
                    anchors.push_back(d);
                }
            } else {
                anchors.push_back(total * 0.5);
            }

            for (double d : anchors) {
                double half = sz.width * 0.5 + 3.0;
                // Skip a label that would hang off either end of the contour.
                if (d - half < 0.0 || d + half > total) continue;

                Point2Dd at = PointAtArc(screenPts, cum, d);
                Point2Dd before = PointAtArc(screenPts, cum, std::max(0.0, d - 4.0));
                Point2Dd after = PointAtArc(screenPts, cum, std::min(total, d + 4.0));
                double angle = std::atan2(after.y - before.y, after.x - before.x);
                // Keep text upright rather than mirrored.
                if (angle > M_PI / 2.0) angle -= M_PI;
                else if (angle < -M_PI / 2.0) angle += M_PI;

                spots.push_back({d, at, angle, text, half});
            }
        }

        // ---- stroke the line, skipping the label gaps ----
        ctx->SetStrokePaint(color);
        ctx->SetStrokeWidth(width);
        if (dashed) ctx->SetLineDash(negativeDash);
        else        ctx->SetLineDash(UCDashPattern());

        if (spots.empty() || !labelBreaksLine) {
            ctx->DrawLinePath(screenPts, false);
        } else {
            double cursor = 0.0;
            for (const auto& s : spots) {
                std::vector<Point2Dd> piece = SubPath(screenPts, cum, cursor, s.d - s.halfW);
                if (piece.size() >= 2) ctx->DrawLinePath(piece, false);
                cursor = s.d + s.halfW;
            }
            std::vector<Point2Dd> tail = SubPath(screenPts, cum, cursor, total);
            if (tail.size() >= 2) ctx->DrawLinePath(tail, false);
        }
        ctx->SetLineDash(UCDashPattern());

        // ---- draw the labels ----
        if (!spots.empty()) {
            ctx->SetFontSize(labelFontSize);
            for (const auto& s : spots) {
                Size2Di sz = ctx->GetTextLineDimensions(s.text);
                ctx->PushState();
                ctx->Translate(s.pos.x, s.pos.y);
                ctx->Rotate(s.angle);
                if (labelHalo) {
                    // A filled plate behind the text keeps it legible over a
                    // busy fill without needing text outlining.
                    ctx->DrawFilledRectangle(
                            Rect2Dd(-sz.width / 2.0 - 2.0, -sz.height / 2.0 - 1.0,
                                    sz.width + 4.0, sz.height + 2.0),
                            labelHaloColor);
                }
                ctx->SetTextPaint(labelTextColor);
                ctx->DrawText(s.text, Point2Dd(-sz.width / 2.0, -sz.height / 2.0));
                ctx->PopState();
            }
        }
    }

    void UltraCanvasContourChartElement::RenderSourcePoints(IRenderContext* ctx) {
        if (sourcePoints.empty() || !field.Valid()) return;
        const ChartPlotArea& area = GetHeatmapArea();

        ctx->PushState();
        ctx->ClipRect(area.ToRect2D());
        for (const auto& p : sourcePoints) {
            double col = field.ColAtX(p.x);
            double row = field.RowAtY(p.y);
            if (col < 0 || row < 0 || col > field.cols - 1 || row > field.rows - 1) continue;
            Point2Dd s = GridToScreen(col, row);
            ctx->DrawFilledCircle(s, sourcePointRadius, sourcePointColor, Colors::Transparent, 0.0f);
        }
        ctx->PopState();
    }

    void UltraCanvasContourChartElement::RenderGridLabels(IRenderContext* ctx) {
        RenderAxisTicks(ctx);
    }

    void UltraCanvasContourChartElement::RenderAxisTicks(IRenderContext* ctx) {
        const ChartPlotArea& area = GetHeatmapArea();
        ctx->SetFontSize(11.0);
        ctx->SetTextPaint(labelColor);
        int lineH = ctx->GetTextLineHeight("0");

        bool categoricalX = !columnLabels.empty();
        bool categoricalY = !rowLabels.empty();

        ctx->SetStrokePaint(frameColor);
        ctx->SetStrokeWidth(1.0);

        // ---- X ----
        if (categoricalX) {
            size_t n = columnLabels.size();
            for (size_t i = 0; i < n; ++i) {
                Size2Di sz = ctx->GetTextLineDimensions(columnLabels[i]);
                double cx = area.x + (i + 0.5) * area.width / n;
                ctx->DrawText(columnLabels[i],
                              Point2Dd(cx - sz.width / 2.0, area.GetBottom() + 5.0));
            }
        } else if (showAxisTicks && field.Valid()) {
            double vx0, vx1, vyUnused0, vyUnused1;
            EffectiveViewRange(vx0, vx1, vyUnused0, vyUnused1);
            double lastRight = -1e9;
            for (int i = 0; i < xTickCount; ++i) {
                double t = static_cast<double>(i) / (xTickCount - 1);
                double value = vx0 + (vx1 - vx0) * t;
                Point2Dd s = GridToScreen(field.ColAtX(value), 0.0);
                std::string text = FormatTick(value, xTickFormatter);
                Size2Di sz = ctx->GetTextLineDimensions(text);
                double x = s.x - sz.width / 2.0;
                if (x < lastRight + 6.0) continue;
                ctx->DrawLine(Point2Dd(s.x, area.GetBottom()),
                              Point2Dd(s.x, area.GetBottom() + 4.0));
                ctx->DrawText(text, Point2Dd(x, area.GetBottom() + 5.0));
                lastRight = x + sz.width;
            }
        }

        // ---- Y ----
        if (categoricalY) {
            size_t n = rowLabels.size();
            for (size_t i = 0; i < n; ++i) {
                Size2Di sz = ctx->GetTextLineDimensions(rowLabels[i]);
                double cy = area.GetBottom() - (i + 0.5) * area.height / n;
                ctx->DrawText(rowLabels[i],
                              Point2Dd(area.x - sz.width - 8.0, cy - sz.height / 2.0));
            }
        } else if (showAxisTicks && field.Valid()) {
            double vxUnused0, vxUnused1, vy0, vy1;
            EffectiveViewRange(vxUnused0, vxUnused1, vy0, vy1);
            double lastTop = 1e9;
            for (int i = 0; i < yTickCount; ++i) {
                double t = static_cast<double>(i) / (yTickCount - 1);
                double value = vy0 + (vy1 - vy0) * t;
                Point2Dd s = GridToScreen(0.0, field.RowAtY(value));
                std::string text = FormatTick(value, yTickFormatter);
                Size2Di sz = ctx->GetTextLineDimensions(text);
                double y = s.y - sz.height / 2.0;
                if (y + sz.height > lastTop - 2.0) continue;
                ctx->DrawLine(Point2Dd(area.x - 4.0, s.y), Point2Dd(area.x, s.y));
                ctx->DrawText(text, Point2Dd(area.x - sz.width - 8.0, y));
                lastTop = y;
            }
        }

        // ---- axis titles ----
        if (!xAxisTitle.empty()) {
            Size2Di sz = ctx->GetTextLineDimensions(xAxisTitle);
            ctx->DrawText(xAxisTitle,
                          Point2Dd(area.GetCenter().x - sz.width / 2.0,
                                   area.GetBottom() + lineH + 8.0));
        }
        if (!yAxisTitle.empty()) {
            Size2Di sz = ctx->GetTextLineDimensions(yAxisTitle);
            ctx->PushState();
            ctx->Translate(10.0, area.GetCenter().y + sz.width / 2.0);
            ctx->Rotate(-M_PI / 2.0);
            ctx->SetTextPaint(labelColor);
            ctx->DrawText(yAxisTitle, Point2Dd(0, 0));
            ctx->PopState();
        }
    }

    void UltraCanvasContourChartElement::RenderDiscreteLegend(IRenderContext* ctx) {
        const ChartPlotArea& area = GetHeatmapArea();
        size_t bands = BandCount();
        if (bands == 0) return;

        ctx->SetFontSize(10.0);
        int lineH = ctx->GetTextLineHeight("0");

        if (legendMode == ContourLegendMode::DiscreteBandsHorizontal) {
            double y = area.GetBottom() + legendReservedH - lineH - 2.0;
            double totalW = 0.0;
            std::vector<double> widths(bands);
            for (size_t b = 0; b < bands; ++b) {
                widths[b] = legendSwatchSize + 4.0 + ctx->GetTextLineWidth(BandIntervalText(b)) + 12.0;
                totalW += widths[b];
            }
            double x = area.GetCenter().x - totalW / 2.0;

            if (!legendTitle.empty()) {
                Size2Di sz = ctx->GetTextLineDimensions(legendTitle);
                ctx->SetTextPaint(labelColor);
                ctx->DrawText(legendTitle,
                              Point2Dd(area.GetCenter().x - sz.width / 2.0, y - lineH - 2.0));
            }

            for (size_t b = 0; b < bands; ++b) {
                ctx->DrawFilledRectangle(
                        Rect2Dd(x, y, legendSwatchSize, lineH),
                        BandColor(b), 1.0f, frameColor);
                ctx->SetTextPaint(labelColor);
                ctx->DrawText(BandIntervalText(b), Point2Dd(x + legendSwatchSize + 4.0, y));
                x += widths[b];
            }
            return;
        }

        // Vertical list on the right, lowest band at the top - the reading
        // order of a stepped density legend.
        double x = area.GetRight() + legendGap;
        double y = area.y;

        if (!legendTitle.empty()) {
            ctx->SetTextPaint(labelColor);
            ctx->DrawText(legendTitle, Point2Dd(x, y));
            y += lineH + 4.0;
        }

        double rowH = std::max<double>(legendSwatchSize, lineH) + 3.0;
        for (size_t b = 0; b < bands; ++b) {
            if (y + rowH > area.GetBottom() + legendReservedH) break;
            ctx->DrawFilledRectangle(
                    Rect2Dd(x, y, legendSwatchSize, legendSwatchSize),
                    BandColor(b), 1.0f, frameColor);
            ctx->SetTextPaint(labelColor);
            ctx->DrawText(BandIntervalText(b),
                          Point2Dd(x + legendSwatchSize + 6.0,
                                   y + (legendSwatchSize - lineH) / 2.0));
            y += rowH;
        }
    }

// =============================================================================
// QUERIES / INTERACTION
// =============================================================================

    double UltraCanvasContourChartElement::ValueAt(double x, double y) const {
        RebuildContours();
        if (!field.Valid()) return std::numeric_limits<double>::quiet_NaN();
        return field.SampleBilinear(field.ColAtX(x), field.RowAtY(y));
    }

    std::vector<ContourPolyline> UltraCanvasContourChartElement::ExportContourPolylines() const {
        RebuildContours();
        std::vector<ContourPolyline> out;
        if (!field.Valid()) return out;

        out.reserve(polylines.size());
        for (const auto& poly : polylines) {
            ContourPolyline exported;
            exported.level = poly.level;
            exported.closed = poly.closed;
            exported.points.reserve(poly.points.size());
            for (const auto& p : poly.points) {
                exported.points.emplace_back(field.XAt(p.x), field.YAt(p.y));
            }
            out.push_back(std::move(exported));
        }
        return out;
    }

    void UltraCanvasContourChartElement::RenderCrosshair(IRenderContext* ctx) {
        const ChartPlotArea& area = GetHeatmapArea();
        if (!field.Valid() ||
            !area.Contains(static_cast<float>(crosshairPos.x),
                           static_cast<float>(crosshairPos.y))) {
            return;
        }

        double sx = crosshairPos.x;
        double sy = crosshairPos.y;

        ctx->PushState();
        ctx->ClipRect(area.ToRect2D());
        ctx->SetStrokePaint(crosshairColor);
        ctx->SetStrokeWidth(1.0);
        ctx->SetLineDash(UCDashPattern{{4.0, 3.0}, 0.0});
        ctx->DrawLine(Point2Dd(area.x, sy), Point2Dd(area.GetRight(), sy));
        ctx->DrawLine(Point2Dd(sx, area.y), Point2Dd(sx, area.GetBottom()));
        ctx->SetLineDash(UCDashPattern());
        ctx->PopState();

        // Value read-outs pinned to the axes, on small plates so they stay
        // legible over the fill and the frame.
        Point2Dd g = ScreenToGrid(sx, sy);
        std::string xText = FormatTick(field.XAt(g.x), xTickFormatter);
        std::string yText = FormatTick(field.YAt(g.y), yTickFormatter);

        ctx->SetFontSize(10.0);
        Color plate(crosshairColor.r, crosshairColor.g, crosshairColor.b, 255);
        Color plateText(255, 255, 255, 255);

        Size2Di xSz = ctx->GetTextLineDimensions(xText);
        double bx = std::clamp(sx - xSz.width / 2.0,
                               static_cast<double>(area.x),
                               area.GetRight() - xSz.width);
        ctx->DrawFilledRectangle(Rect2Dd(bx - 3.0, area.GetBottom() + 1.0,
                                         xSz.width + 6.0, xSz.height + 2.0), plate);
        ctx->SetTextPaint(plateText);
        ctx->DrawText(xText, Point2Dd(bx, area.GetBottom() + 2.0));

        Size2Di ySz = ctx->GetTextLineDimensions(yText);
        double by = std::clamp(sy - ySz.height / 2.0,
                               static_cast<double>(area.y),
                               area.GetBottom() - ySz.height);
        ctx->DrawFilledRectangle(Rect2Dd(area.x - ySz.width - 9.0, by - 1.0,
                                         ySz.width + 6.0, ySz.height + 2.0), plate);
        ctx->SetTextPaint(plateText);
        ctx->DrawText(yText, Point2Dd(area.x - ySz.width - 6.0, by));
    }

    bool UltraCanvasContourChartElement::HandleChartMouseMove(const Point2Di& mousePos) {
        RebuildContours();

        const ChartPlotArea& area = GetHeatmapArea();
        bool inside = field.Valid() && area.Contains(static_cast<float>(mousePos.x),
                                                     static_cast<float>(mousePos.y));
        if (showCrosshair) {
            crosshairPos = mousePos;
            if (inside != crosshairLive || inside) RequestRedraw();
            crosshairLive = inside;
        }

        if (!enableTooltips) {
            HideTooltip();
            return false;
        }
        if (!inside) {
            HideTooltip();
            return false;
        }

        Point2Dd g = ScreenToGrid(mousePos.x, mousePos.y);
        double value = field.SampleBilinear(g.x, g.y);
        if (std::isnan(value)) {
            HideTooltip();
            return false;
        }

        std::string content;
        content += "X: " + FormatTick(field.XAt(g.x), xTickFormatter) + "\n";
        content += "Y: " + FormatTick(field.YAt(g.y), yTickFormatter) + "\n";
        content += "Value: " + FormatLabelValue(value);
        if (!levels.empty()) {
            size_t band = static_cast<size_t>(
                    std::upper_bound(levels.begin(), levels.end(), value) - levels.begin());
            content += "\nBand: " + BandIntervalText(band);
        }

        auto windowMousePos = MapFromLocal(Point2Df(mousePos.x, mousePos.y), nullptr);
        UltraCanvasTooltipManager::UpdateAndShowTooltip(
                this->window, content,
                Point2Di(static_cast<int>(windowMousePos.x), static_cast<int>(windowMousePos.y)));
        isTooltipActive = true;
        return true;
    }

    bool UltraCanvasContourChartElement::OnEvent(const UCEvent& event) {
        switch (event.type) {
            case UCEventType::MouseDown:
                if (event.button == UCMouseButton::Left) {
                    clickDownPos = Point2Di(event.pointer.x, event.pointer.y);
                    clickCandidate = true;
                    if (enablePan && ViewZoomable()) {
                        panDragging = true;
                        panLastPos = clickDownPos;
                        return true;
                    }
                }
                break;

            case UCEventType::MouseMove:
                if (panDragging) {
                    Point2Di pos(event.pointer.x, event.pointer.y);
                    if (std::abs(pos.x - clickDownPos.x) > 3 ||
                        std::abs(pos.y - clickDownPos.y) > 3) {
                        clickCandidate = false;
                    }
                    RebuildContours();
                    if (field.Valid() && viewActive) {
                        // Move the window so the data under the cursor follows
                        // the drag; ScreenToGrid keeps this correct for either
                        // row order.
                        Point2Dd g0 = ScreenToGrid(panLastPos.x, panLastPos.y);
                        Point2Dd g1 = ScreenToGrid(pos.x, pos.y);
                        ApplyPan(field.XAt(g0.x) - field.XAt(g1.x),
                                 field.YAt(g0.y) - field.YAt(g1.y));
                    }
                    panLastPos = pos;
                    return true;
                }
                break;

            case UCEventType::MouseUp:
                if (event.button == UCMouseButton::Left) {
                    bool wasPanning = panDragging;
                    panDragging = false;

                    if (clickCandidate && onContourClick) {
                        Point2Di pos(event.pointer.x, event.pointer.y);
                        if (std::abs(pos.x - clickDownPos.x) <= 3 &&
                            std::abs(pos.y - clickDownPos.y) <= 3) {
                            RebuildContours();
                            const ChartPlotArea& area = GetHeatmapArea();
                            if (field.Valid() &&
                                area.Contains(static_cast<float>(pos.x),
                                              static_cast<float>(pos.y))) {
                                Point2Dd g = ScreenToGrid(pos.x, pos.y);
                                double value = field.SampleBilinear(g.x, g.y);
                                if (!std::isnan(value)) {
                                    ContourClickInfo info;
                                    info.x = field.XAt(g.x);
                                    info.y = field.YAt(g.y);
                                    info.value = value;
                                    info.bandIndex = BandIndexOf(value);
                                    onContourClick(info);
                                }
                            }
                        }
                    }
                    clickCandidate = false;
                    if (wasPanning) return true;
                }
                break;

            case UCEventType::MouseWheel:
                if (enableZoom && ViewZoomable()) {
                    RebuildContours();
                    const ChartPlotArea& area = GetHeatmapArea();
                    if (field.Valid() &&
                        area.Contains(static_cast<float>(event.pointer.x),
                                      static_cast<float>(event.pointer.y))) {
                        Point2Dd g = ScreenToGrid(event.pointer.x, event.pointer.y);
                        double factor = (event.wheelDelta > 0) ? (1.0 / 1.25) : 1.25;
                        ApplyZoom(factor, field.XAt(g.x), field.YAt(g.y));
                        return true;
                    }
                }
                break;

            case UCEventType::MouseDoubleClick:
                if (enableZoom && viewActive) {
                    ResetView();
                    return true;
                }
                break;

            case UCEventType::MouseLeave:
                panDragging = false;
                clickCandidate = false;
                if (crosshairLive) {
                    crosshairLive = false;
                    RequestRedraw();
                }
                break;

            default:
                break;
        }
        return UltraCanvasHeatmapChartElement::OnEvent(event);
    }

} // namespace UltraCanvas
