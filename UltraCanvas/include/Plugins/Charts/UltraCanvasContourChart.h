// include/Plugins/Charts/UltraCanvasContourChart.h
// 2D contour chart element: isolines, filled contour bands, or both, over a
// scalar field. The field can be supplied as a regular grid or derived from a
// scattered point cloud (kernel density, binning, IDW, nearest neighbour).
//
// Derives from UltraCanvasHeatmapChartElement so it inherits the grid data
// model, value range / scaling, the colour maps and the colour bar; the
// contour geometry and its rendering are added on top. That also makes the
// combined "heatmap cells + isolines" presentation a single render mode.
//
// Version: 1.0.0
// Last Modified: 2026-07-29
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/UltraCanvasHeatmapChart.h"
#include "Plugins/Charts/UltraCanvasContourGrid.h"
#include "Plugins/Charts/UltraCanvasMarchingSquares.h"
#include <functional>
#include <string>
#include <vector>

namespace UltraCanvas {

// =============================================================================
// ENUMS
// =============================================================================

// How the field is painted.
    enum class ContourRenderMode {
        Lines,                // isolines only, no fill
        Filled,               // filled bands only
        FilledWithLines,      // filled bands with isolines stroked on top
        Smooth,               // continuous (unbanded) colour fill plus isolines
        HeatmapWithContours   // discrete heatmap cells with isolines on top
    };

// Legend presentation. Named NoLegend rather than None to dodge the X11 macro.
    enum class ContourLegendMode {
        NoLegend,
        ColorBar,                 // continuous vertical ramp with ticks
        DiscreteBands,            // vertical list of swatch + interval
        DiscreteBandsHorizontal   // same, laid out along the bottom
    };

// How an isoline is coloured.
    enum class ContourLineColorMode {
        Fixed,        // one colour for every contour
        FromLevel     // each contour takes its own colour from the colour map
    };

// Text used for a band entry in the discrete legend.
    enum class ContourIntervalFormat {
        Brackets,     // [0.00, 0.02]
        Dash,         // 0.00 - 0.02
        To            // 0.00 to 0.02
    };

// =============================================================================
// CONTOUR CHART ELEMENT
// =============================================================================

// Everything a click on the field can tell the application.
    struct ContourClickInfo {
        double x = 0.0;        // data-space position
        double y = 0.0;
        double value = 0.0;    // bilinear field value at (x, y)
        size_t bandIndex = 0;  // 0 = below the first level .. levels = above the last
    };

    class UltraCanvasContourChartElement : public UltraCanvasHeatmapChartElement {
    public:
        using ValueFormatter = std::function<std::string(double)>;
        using ContourClickHandler = std::function<void(const ContourClickInfo&)>;

    private:
        // ---- contour configuration ----
        ContourRenderMode contourMode = ContourRenderMode::FilledWithLines;
        ContourLevelMode levelMode = ContourLevelMode::Count;
        int levelCount = 10;
        double levelStep = 1.0;
        double levelOrigin = 0.0;
        std::vector<double> explicitLevels;
        bool niceLevels = true;

        // ---- field preparation ----
        double gridSmoothing = 0.0;     // Gaussian sigma in cells applied to the field
        int lineSmoothing = 0;          // Chaikin iterations applied to the polylines
        double minContourLength = 0.0;  // cull speckle contours shorter than this
        int upsampleFactor = 1;         // bilinear grid refinement before contouring

        // ---- isoline styling ----
        ContourLineColorMode lineColorMode = ContourLineColorMode::Fixed;
        Color lineColor = Color(30, 30, 30, 255);
        float lineWidth = 1.0f;
        bool negativeLevelsDashed = false;
        UCDashPattern negativeDash{{4.0, 3.0}, 0.0};
        int majorLineEvery = 0;         // 0 = off; otherwise every n-th level
        float majorLineWidth = 2.0f;
        bool highlightZeroLevel = false;
        Color zeroLevelColor = Color(0, 0, 0, 255);
        float zeroLevelWidth = 2.0f;
        double fillAlpha = 1.0;         // multiplies the band fill alpha

        // ---- inline labels ----
        bool showLineLabels = false;
        int labelDecimals = 2;
        float labelFontSize = 10.0f;
        Color labelTextColor = Color(20, 20, 20, 255);
        bool labelBreaksLine = true;
        bool labelHalo = false;
        Color labelHaloColor = Color(255, 255, 255, 220);
        double labelSpacing = 0.0;      // px between repeats; 0 = one per contour
        double minLabelLength = 40.0;   // px of contour needed before labelling
        bool labelMajorOnly = false;
        ValueFormatter labelFormatter;

        // ---- legend ----
        ContourLegendMode legendMode = ContourLegendMode::ColorBar;
        std::string legendTitle;
        ContourIntervalFormat intervalFormat = ContourIntervalFormat::Brackets;
        int legendDecimals = 2;
        int legendSwatchSize = 14;
        int legendGap = 12;

        // ---- axes ----
        bool showAxisTicks = true;
        int xTickCount = 6;
        int yTickCount = 6;
        ValueFormatter xTickFormatter;
        ValueFormatter yTickFormatter;

        // ---- scattered source points ----
        std::vector<ContourPoint> sourcePoints;
        ContourGriddingOptions griddingOptions;
        bool showSourcePoints = false;
        Color sourcePointColor = Color(255, 255, 255, 90);
        float sourcePointRadius = 1.5f;

        // ---- derived state (rebuilt on demand) ----
        mutable ContourGrid field;                      // grid mirrored from the base
        mutable std::vector<double> levels;
        mutable std::vector<ContourPolyline> polylines; // fractional grid coords
        mutable bool contourValid = false;

        // ---- cached raster fill ----
        std::shared_ptr<UCPixmap> fillPixmap;
        bool fillPixmapValid = false;
        int fillPixmapW = 0, fillPixmapH = 0;

        // Data-space extents of the field (used for axis ticks and tooltips).
        double dataXMin = 0.0, dataXMax = 1.0;
        double dataYMin = 0.0, dataYMax = 1.0;
        bool dataRangeSet = false;

        // ---- zoomed/panned view window (a sub-rectangle of the data extents) ----
        double viewXMin = 0.0, viewXMax = 1.0;
        double viewYMin = 0.0, viewYMax = 1.0;
        bool viewActive = false;          // false = the full field is shown
        double maxZoomFactor = 1000.0;    // smallest allowed span = full span / this

        // ---- crosshair ----
        bool showCrosshair = false;
        Color crosshairColor = Color(90, 95, 110, 200);
        bool crosshairLive = false;       // cursor currently inside the plot
        Point2Di crosshairPos;

        // ---- interaction state ----
        ContourClickHandler onContourClick;
        bool panDragging = false;
        Point2Di panLastPos;
        Point2Di clickDownPos;
        bool clickCandidate = false;

        // Reserved by the last layout pass for the legend.
        mutable double legendReservedW = 0.0;
        mutable double legendReservedH = 0.0;

    public:
        UltraCanvasContourChartElement(const std::string& id, int x, int y, int width, int height);

        // =====================================================================
        // DATA
        // =====================================================================

        // The x/y extents the node lattice spans. Drives the axis ticks and the
        // tooltip read-out; purely cosmetic for the contour geometry itself.
        void SetDataRange(double xMin, double xMax, double yMin, double yMax);
        void ClearDataRange();

        // Replace the field with a grid derived from a scattered point cloud.
        // Also sets the data range from the resulting grid extents.
        void SetPoints(const std::vector<ContourPoint>& points);
        void SetPoints(const std::vector<Point2Dd>& points);   // unit weights
        const std::vector<ContourPoint>& GetPoints() const { return sourcePoints; }
        void SetGriddingOptions(const ContourGriddingOptions& options);
        const ContourGriddingOptions& GetGriddingOptions() const { return griddingOptions; }
        void SetGriddingMethod(ContourGridding method);
        void SetGridResolution(int cols, int rows);
        void SetKDEBandwidth(double bandwidth);   // <= 0 selects Scott's rule

        // Sample an analytic function onto a grid of the given resolution.
        void SetFunction(const std::function<double(double, double)>& fn,
                         int cols, int rows,
                         double xMin, double xMax, double yMin, double yMax);

        // =====================================================================
        // LEVELS
        // =====================================================================

        void SetLevelCount(int count);
        void SetLevelStep(double step, double origin = 0.0);
        void SetLevels(const std::vector<double>& values);
        void SetQuantileLevels(int count);
        void SetNiceLevels(bool on);
        // The levels currently in use (computed lazily; forces a rebuild).
        const std::vector<double>& GetLevels() const;

        // =====================================================================
        // FIELD PREPARATION
        // =====================================================================

        void SetGridSmoothing(double sigmaCells);
        void SetLineSmoothing(int chaikinIterations);
        void SetMinContourLength(double gridUnits);
        void SetUpsampleFactor(int factor);   // 1 = off

        // =====================================================================
        // RENDER MODE / STYLE
        // =====================================================================

        void SetRenderMode(ContourRenderMode mode);
        ContourRenderMode GetRenderMode() const { return contourMode; }

        void SetLineColor(const Color& c);
        void SetLineColorMode(ContourLineColorMode mode);
        void SetLineWidth(float width);
        void SetNegativeLevelsDashed(bool on);
        void SetNegativeDashPattern(const UCDashPattern& pattern);
        void SetMajorLineEvery(int nth, float width = 2.0f);
        void SetHighlightZeroLevel(bool on, const Color& c = Color(0, 0, 0, 255), float width = 2.0f);
        void SetFillAlpha(double alpha);

        // =====================================================================
        // INLINE LABELS
        // =====================================================================

        void SetShowLineLabels(bool on);
        void SetLabelDecimals(int decimals);
        void SetLabelFontSize(float size);
        void SetLabelTextColor(const Color& c);
        void SetLabelBreaksLine(bool on);
        void SetLabelHalo(bool on, const Color& c = Color(255, 255, 255, 220));
        void SetLabelSpacing(double pixels);      // 0 = a single label per contour
        void SetMinLabelLength(double pixels);
        void SetLabelMajorOnly(bool on);
        void SetLabelFormatter(ValueFormatter fn);

        // =====================================================================
        // LEGEND
        // =====================================================================

        void SetLegendMode(ContourLegendMode mode);
        void SetLegendTitle(const std::string& title);
        void SetIntervalFormat(ContourIntervalFormat fmt);
        void SetLegendDecimals(int decimals);

        // =====================================================================
        // AXES
        // =====================================================================

        void SetShowAxisTicks(bool on);
        void SetTickCounts(int xTicks, int yTicks);
        void SetXTickFormatter(ValueFormatter fn);
        void SetYTickFormatter(ValueFormatter fn);
        // Categorical labels replace the numeric ticks on that axis.
        void SetCategoryLabelsX(const std::vector<std::string>& labels);
        void SetCategoryLabelsY(const std::vector<std::string>& labels);

        // =====================================================================
        // SOURCE POINT OVERLAY
        // =====================================================================

        void SetShowSourcePoints(bool on);
        void SetSourcePointStyle(const Color& c, float radius);

        // =====================================================================
        // VIEW WINDOW (ZOOM / PAN)
        // =====================================================================

        // Show only this data-space sub-rectangle of the field. The window is
        // clamped to the data extents. The same window is what the mouse wheel
        // (SetEnableZoom) and drag panning (SetEnablePan) manipulate; both are
        // ignored in HeatmapWithContours mode, which keeps the base heatmap's
        // whole-matrix layout.
        void SetViewRange(double xMin, double xMax, double yMin, double yMax);
        // Back to the full field. A double-click does the same when zoom is on.
        void ResetView();
        // The window currently shown; returns false when the view is the full
        // field (the outputs then hold the full extents).
        bool GetViewRange(double& xMin, double& xMax, double& yMin, double& yMax) const;

        // =====================================================================
        // INTERACTION
        // =====================================================================

        // Crosshair that follows the cursor with an x/y read-out on the axes.
        void SetShowCrosshair(bool on);
        void SetCrosshairColor(const Color& c);
        // Called on a left click (a press-and-release without dragging) inside
        // the plot with the data position, field value and band index.
        void SetOnContourClick(ContourClickHandler handler);

        // =====================================================================
        // QUERIES
        // =====================================================================

        // Bilinear value of the field at a data-space position (NaN if outside).
        double ValueAt(double x, double y) const;
        // Index of the band containing `value`: 0 is below the first level,
        // levels.size() is above the last.
        size_t BandIndexOf(double value) const;
        // The extracted contours, in fractional grid coordinates.
        const std::vector<ContourPolyline>& GetContours() const;
        // The extracted contours converted to data-space coordinates - the
        // GIS/CAD hand-off format. Levels and closed flags are preserved.
        std::vector<ContourPolyline> ExportContourPolylines() const;

        // =====================================================================
        // OVERRIDES
        // =====================================================================

        void RenderChart(IRenderContext* ctx) override;
        bool HandleChartMouseMove(const Point2Di& mousePos) override;
        bool OnEvent(const UCEvent& event) override;

    protected:
        ChartPlotArea ComputeHeatmapArea(IRenderContext* ctx) override;
        void RenderGridLabels(IRenderContext* ctx) override;
        void InvalidateRaster() override;

    private:
        // ---- pipeline ----
        void RebuildContours() const;
        void InvalidateContours();
        void RebuildFromPoints();
        ContourGrid BuildFieldFromBase() const;

        // ---- geometry mapping ----
        bool UsesCellAlignedGrid() const;
        Point2Dd GridToScreen(double col, double row) const;
        Point2Dd ScreenToGrid(double sx, double sy) const;
        // The data-space window currently mapped onto the plot area: the view
        // window when one is active (and the mode honours it), otherwise the
        // full field extents.
        void EffectiveViewRange(double& xMin, double& xMax,
                                double& yMin, double& yMax) const;
        bool ViewZoomable() const;   // view/zoom/pan apply in this render mode
        void ClampViewToData();
        void ApplyZoom(double factor, double focusX, double focusY);
        void ApplyPan(double dxData, double dyData);

        // ---- bands ----
        size_t BandCount() const;
        void BandRange(size_t bandIndex, double& lo, double& hi) const;
        Color BandColor(size_t bandIndex) const;
        std::string BandIntervalText(size_t bandIndex) const;

        // ---- rendering stages ----
        void RenderFillRaster(IRenderContext* ctx, bool banded);
        void RenderIsolines(IRenderContext* ctx);
        void RenderOneContour(IRenderContext* ctx, const ContourPolyline& poly,
                              const std::vector<Point2Dd>& screenPts);
        void RenderSourcePoints(IRenderContext* ctx);
        void RenderDiscreteLegend(IRenderContext* ctx);
        void RenderAxisTicks(IRenderContext* ctx);
        void RenderCrosshair(IRenderContext* ctx);

        // ---- helpers ----
        int LevelIndexOf(double level) const;
        bool IsMajorLevel(int levelIndex) const;
        Color ResolveLineColor(double level) const;
        float ResolveLineWidth(int levelIndex, double level) const;
        bool ResolveLineDashed(double level) const;
        std::string FormatLabelValue(double v) const;
        std::string FormatLegendValue(double v) const;
        std::string FormatTick(double v, const ValueFormatter& fn) const;
        void MeasureLegend(IRenderContext* ctx, double& outW, double& outH) const;
    };

// =============================================================================
// FACTORY
// =============================================================================

    inline std::shared_ptr<UltraCanvasContourChartElement> CreateContourChartElement(
            const std::string& id, int x, int y, int width, int height) {
        return std::make_shared<UltraCanvasContourChartElement>(id, x, y, width, height);
    }

} // namespace UltraCanvas
