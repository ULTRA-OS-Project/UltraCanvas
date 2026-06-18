// include/Plugins/Charts/UltraCanvasHeatmapChart.h
// 2D heatmap / matrix chart element for UltraCanvas.
// Renders a grid of cells whose colour encodes a scalar value, using a
// configurable colour map. Designed as a reusable building block: feed it any
// 2D matrix (correlation matrices, confusion matrices, density grids, audio
// spectrograms produced by an STFT layer, ...).
// Version: 1.0.0
// Last Modified: 2026-06-18
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasChartElementBase.h"
#include "UltraCanvasImage.h"
#include "Plugins/Charts/UltraCanvasColormap.h"
#include <vector>
#include <string>
#include <memory>
#include <cstdint>

namespace UltraCanvas {

// =============================================================================
// ENUMS
// =============================================================================

// HeatmapColormap (the available colour maps) is defined in UltraCanvasColormap.h.

// How a raw value is mapped onto the normalized [0,1] colour-map position.
    enum class HeatmapScale {
        Linear,
        Logarithmic   // useful for magnitudes spanning many orders (e.g. dB-less spectra)
    };

// Where data row 0 is displayed. TopDown is the natural matrix orientation;
// BottomUp puts row 0 at the bottom (e.g. lowest frequency at the bottom of a
// spectrogram).
    enum class HeatmapRowOrder {
        TopDown,
        BottomUp
    };

// Cell rendering strategy.
//   Cells - draw one filled rectangle per cell (supports borders + value text,
//           crisp edges; best for small/medium matrices).
//   Image - rasterize the matrix into a pixmap and blit it scaled (smooth,
//           cheap for very large grids such as spectrograms).
//   Auto  - pick Cells for small grids, Image for large ones.
    enum class HeatmapRenderMode {
        Auto,
        Cells,
        Image
    };

// Shape drawn for each cell in Cells render mode.
    enum class HeatmapCellShape {
        Rectangle,
        RoundedRectangle,
        Circle           // dot heatmap
    };

// Show only part of a (square) matrix, e.g. for correlation matrices.
    enum class HeatmapTriangularMask {
        NoMask,          // show all cells (named to avoid the X11 'None' macro)
        Lower,           // col <= row
        LowerNoDiagonal, // col <  row
        Upper,           // col >= row
        UpperNoDiagonal  // col >  row
    };

// =============================================================================
// HEATMAP CHART ELEMENT
// =============================================================================

    class UltraCanvasHeatmapChartElement : public UltraCanvasChartElementBase {
    private:
        // Matrix data, row-major: values[row * cols + col]
        std::vector<double> values;
        int cols = 0;
        int rows = 0;

        // Value range / scaling
        bool autoRange = true;
        bool rangeDirty = true;
        double valueMin = 0.0;
        double valueMax = 1.0;
        HeatmapScale scaleMode = HeatmapScale::Linear;

        // Diverging normalization (centre a midpoint on the colour-map middle)
        bool divergingEnabled = false;
        double divergingMidpoint = 0.0;

        // Colour mapping
        HeatmapColormap colormap = HeatmapColormap::Viridis;
        std::vector<Color> customColormap;
        bool reverseColormap = false;
        int colorLevels = 0;            // 0 = continuous; >= 2 = quantized bands
        Color nanColor = Colors::Transparent;

        // Masking (square matrices, e.g. correlation)
        HeatmapTriangularMask triangularMask = HeatmapTriangularMask::NoMask;

        // Orientation / render strategy
        HeatmapRowOrder rowOrder = HeatmapRowOrder::TopDown;
        HeatmapRenderMode renderMode = HeatmapRenderMode::Auto;

        // Cell decoration / geometry (Cells mode only)
        HeatmapCellShape cellShape = HeatmapCellShape::Rectangle;
        double cellGap = 0.0;           // padding as a fraction [0, 0.9] of the cell
        double cellCornerRadius = 0.0;  // px for RoundedRectangle (<=0 => auto)
        bool showCellBorders = false;
        Color cellBorderColor = Color(255, 255, 255, 255);
        float cellBorderWidth = 1.0f;
        bool showCellValues = false;
        float cellValueFontSize = 9.0f;
        Color cellValueColor = Color(0, 0, 0, 255);

        // Labels / titles
        std::vector<std::string> columnLabels;
        std::vector<std::string> rowLabels;
        bool showColumnLabels = true;
        bool showRowLabels = true;
        std::string xAxisTitle;
        std::string yAxisTitle;

        // Colour bar legend
        bool showColorBar = true;
        int colorBarWidth = 18;
        int colorBarGap = 12;

        // Cached layout / raster
        ChartPlotArea heatmapArea;
        std::shared_ptr<UCPixmap> cellPixmap;
        bool pixmapValid = false;

    public:
        UltraCanvasHeatmapChartElement(const std::string& id, int x, int y, int width, int height);

        // ---- Data ----
        // Flat row-major buffer (size must equal columns * rowsCount).
        void SetData(const std::vector<double>& flat, int columns, int rowsCount);
        // 2D matrix as a vector of rows (each row a vector of column values).
        void SetData(const std::vector<std::vector<double>>& matrix);
        // Allocate an empty grid filled with a constant value.
        void Resize(int columns, int rowsCount, double fill = 0.0);
        void SetValue(int col, int row, double v);
        double GetValue(int col, int row) const;
        int GetColumns() const { return cols; }
        int GetRows() const { return rows; }
        void ClearData();

        // ---- Value range / scale ----
        void SetAutoValueRange(bool on);
        void SetValueRange(double minVal, double maxVal);   // disables auto range
        void SetScale(HeatmapScale s);
        double GetValueMin() const { return valueMin; }
        double GetValueMax() const { return valueMax; }

        // Centre `midpoint` on the colour-map middle (great for correlation /
        // deviation data; pair with a diverging colour map). Linear scale only.
        void SetDiverging(bool on, double midpoint = 0.0);

        // ---- Colour map ----
        void SetColormap(HeatmapColormap c);
        void SetCustomColormap(const std::vector<Color>& colors); // >=2 evenly spaced anchors
        void SetReverseColormap(bool on);
        void SetColorLevels(int levels);   // 0 = continuous; >= 2 = discrete bands
        void SetNaNColor(const Color& c);
        Color MapValueToColor(double value) const;

        // ---- Masking ----
        void SetTriangularMask(HeatmapTriangularMask mask);

        // ---- Orientation / render strategy ----
        void SetRowOrder(HeatmapRowOrder o);
        void SetRenderMode(HeatmapRenderMode m);

        // ---- Cell decoration / geometry ----
        void SetCellShape(HeatmapCellShape shape);
        void SetCellGap(double fraction);          // [0, 0.9] padding between cells
        void SetCellCornerRadius(double radiusPx); // for RoundedRectangle (<=0 => auto)
        void SetShowCellBorders(bool on, const Color& color = Color(255, 255, 255, 255), float width = 1.0f);
        void SetShowCellValues(bool on);
        void SetCellValueStyle(const Color& color, float fontSize);

        // ---- Labels / titles ----
        void SetColumnLabels(const std::vector<std::string>& labels);
        void SetRowLabels(const std::vector<std::string>& labels);
        void SetShowColumnLabels(bool on);
        void SetShowRowLabels(bool on);
        void SetAxisTitles(const std::string& xTitle, const std::string& yTitle);

        // ---- Colour bar ----
        void SetShowColorBar(bool on);

        // ---- Overrides ----
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        void RenderChart(IRenderContext* ctx) override;
        bool HandleChartMouseMove(const Point2Di& mousePos) override;

    private:
        void RecomputeAutoRange();
        void InvalidateRaster();
        double NormalizeValue(double v) const;         // -> [0,1], NaN passthrough
        Color ColorAtT(double t) const;                // colour-map lookup (handles reverse)
        ChartPlotArea ComputeHeatmapArea(IRenderContext* ctx);
        bool EffectiveUseImageMode() const;
        void BuildPixmap();
        void RenderCells(IRenderContext* ctx);
        void RenderImage(IRenderContext* ctx);
        void RenderGridLabels(IRenderContext* ctx);
        void RenderColorBar(IRenderContext* ctx);
        bool CellAtScreen(const Point2Di& pos, int& outCol, int& outRow) const;

        std::string ColumnLabel(int col) const;
        std::string RowLabel(int row) const;
        std::string FormatValue(double v) const;
        bool IsCellVisible(int col, int row) const;    // triangular mask test

        static uint32_t ColorToPixel(const Color& c);  // -> premultiplied ARGB32
    };

// =============================================================================
// FACTORY
// =============================================================================

    inline std::shared_ptr<UltraCanvasHeatmapChartElement> CreateHeatmapChartElement(
            const std::string& id, int x, int y, int width, int height) {
        return std::make_shared<UltraCanvasHeatmapChartElement>(id, x, y, width, height);
    }

} // namespace UltraCanvas
