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
#include <vector>
#include <string>
#include <memory>
#include <cstdint>

namespace UltraCanvas {

// =============================================================================
// ENUMS
// =============================================================================

// Built-in perceptual / classic colour maps. Custom uses SetCustomColormap().
    enum class HeatmapColormap {
        Grayscale,
        Viridis,
        Inferno,   // good default for audio spectrograms
        Magma,
        Plasma,
        Jet,
        Hot,
        Cool,
        Custom
    };

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

        // Colour mapping
        HeatmapColormap colormap = HeatmapColormap::Viridis;
        std::vector<Color> customColormap;
        bool reverseColormap = false;
        Color nanColor = Colors::Transparent;

        // Orientation / render strategy
        HeatmapRowOrder rowOrder = HeatmapRowOrder::TopDown;
        HeatmapRenderMode renderMode = HeatmapRenderMode::Auto;

        // Cell decoration (Cells mode only)
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

        // ---- Colour map ----
        void SetColormap(HeatmapColormap c);
        void SetCustomColormap(const std::vector<Color>& colors); // >=2 evenly spaced anchors
        void SetReverseColormap(bool on);
        void SetNaNColor(const Color& c);
        Color MapValueToColor(double value) const;

        // ---- Orientation / render strategy ----
        void SetRowOrder(HeatmapRowOrder o);
        void SetRenderMode(HeatmapRenderMode m);

        // ---- Cell decoration ----
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

        static std::vector<Color> BuiltinColormapAnchors(HeatmapColormap c);
        static Color InterpolateAnchors(const std::vector<Color>& anchors, double t);
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
