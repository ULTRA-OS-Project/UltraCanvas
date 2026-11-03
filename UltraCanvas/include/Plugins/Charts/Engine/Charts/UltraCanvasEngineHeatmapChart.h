// include/Plugins/Charts/Engine/Charts/UltraCanvasEngineHeatmapChart.h
// Heatmap / matrix chart on the chart engine: a grid of cells whose colour
// encodes a scalar through a colour map, keyed by the engine's colour-bar
// legend.
//
// The engine port of UltraCanvasHeatmapChartElement. Rows and columns are two
// Category axes, so their labels are edge-axis furniture the engine measures
// and declutters (dense matrices thin their labels instead of overprinting);
// cells are BuildBarOutline spans, so the matrix also renders transposed
// under the Horizontal projection; the colour bar is the shared legend in
// ColorBar mode - for a logarithmic or diverging mapping its ticks are read
// back through the same normalisation the cells use, so the key always
// matches the cells. Hover outlines a cell and shows its row, column and
// value through the engine's hit regions.
//
// Version: 1.0.0
// Last Modified: 2026-10-20
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/Engine/UltraCanvasChartEngineElement.h"
#include "Plugins/Charts/UltraCanvasColormap.h"
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// How a value maps onto the colour map.
enum class ChartHeatmapScale { Linear, Logarithmic };
// Where data row 0 is shown: at the top (matrix order) or the bottom.
enum class ChartHeatmapRowOrder { TopDown, BottomUp };
enum class ChartHeatmapCellShape { Rectangle, RoundedRectangle, Circle };
// Show only part of a square matrix (correlation matrices).
enum class ChartHeatmapMask { NoMask, Lower, LowerNoDiagonal, Upper, UpperNoDiagonal };

class UltraCanvasEngineHeatmapChart : public UltraCanvasChartEngineElement {
public:
    UltraCanvasEngineHeatmapChart(const std::string& id, int x = 0, int y = 0,
                                  int w = 0, int h = 0);

    // ---- data -----------------------------------------------------------
    // Row-major: flat[row * columns + col]. NaN cells take the NaN colour.
    void SetData(const std::vector<double>& flat, int columns, int rowCount);
    void SetData(const std::vector<std::vector<double>>& matrix);
    void SetValue(int col, int row, double value);
    double GetValue(int col, int row) const;
    int GetColumns() const { return cols; }
    int GetRows() const { return rows; }
    void ClearData();

    // ---- value range and mapping ---------------------------------------
    void SetAutoValueRange(bool on);
    void SetValueRange(double minValue, double maxValue);     // turns auto off
    double GetValueMin() const { return valueMin; }
    double GetValueMax() const { return valueMax; }
    void SetScale(ChartHeatmapScale scale);
    // Centre `midpoint` on the colour map's middle (pair with a diverging map).
    void SetDiverging(bool on, double midpoint = 0.0);
    void SetColormap(HeatmapColormap colormap);
    void SetCustomColormap(const std::vector<Color>& anchors);   // >= 2 anchors
    void SetReverseColormap(bool on);
    // 0 = continuous; >= 2 = that many discrete bands.
    void SetColorLevels(int levels);
    void SetNaNColor(const Color& color);
    Color MapValueToColor(double value) const;

    // ---- layout ---------------------------------------------------------
    void SetTriangularMask(ChartHeatmapMask mask);
    void SetRowOrder(ChartHeatmapRowOrder order);
    void SetColumnLabels(const std::vector<std::string>& labels);
    void SetRowLabels(const std::vector<std::string>& labels);
    void SetShowColumnLabels(bool show);
    void SetShowRowLabels(bool show);
    void SetColumnLabelsOnTop(bool onTop);
    void SetAxisTitles(const std::string& xTitle, const std::string& yTitle);

    // ---- cells ----------------------------------------------------------
    void SetCellShape(ChartHeatmapCellShape shape);
    // Padding between cells as a fraction of a cell, 0..0.9.
    void SetCellGap(double fraction);
    // Corner radius in px for RoundedRectangle; <= 0 picks one from the cell size.
    void SetCellCornerRadius(double radiusPx);
    void SetShowCellBorders(bool on, const Color& color = Color(255, 255, 255, 255),
                            float width = 1.0f);
    void SetShowCellValues(bool on);
    // Cell value text; alpha 0 (the default) picks black or white per cell.
    void SetCellValueStyle(const Color& color, float fontSize);
    // Formats the cell values, the tooltips and the colour-bar ticks.
    void SetValueFormatter(ValueFormatter formatter);

    // ---- colour bar -----------------------------------------------------
    void SetShowColorBar(bool show);

    // ---- engine contract ------------------------------------------------
    void DescribeAxes(ChartAxisSet& axes) override;
    void RenderChartContent(IRenderContext* ctx, const ChartEngineFrame& frame) override;

private:
    std::vector<double> values;
    int cols = 0, rows = 0;

    bool autoRange = true;
    double valueMin = 0.0, valueMax = 1.0;
    ChartHeatmapScale scale = ChartHeatmapScale::Linear;
    bool diverging = false;
    double divergingMidpoint = 0.0;
    HeatmapColormap colormap = HeatmapColormap::Viridis;
    std::vector<Color> customColormap;
    bool reverseColormap = false;
    int colorLevels = 0;
    Color nanColor = Color(0, 0, 0, 0);

    ChartHeatmapMask mask = ChartHeatmapMask::NoMask;
    ChartHeatmapRowOrder rowOrder = ChartHeatmapRowOrder::TopDown;
    std::vector<std::string> columnLabels, rowLabels;
    bool showColumnLabels = true, showRowLabels = true, columnLabelsOnTop = false;
    std::string xTitle, yTitle;

    ChartHeatmapCellShape cellShape = ChartHeatmapCellShape::Rectangle;
    double cellGap = 0.0;
    double cellCornerRadius = 0.0;
    bool showCellBorders = false;
    Color cellBorderColor = Color(255, 255, 255, 255);
    float cellBorderWidth = 1.0f;
    bool showCellValues = false;
    Color cellValueColor = Color(0, 0, 0, 0);
    float cellValueFontSize = 9.0f;
    ValueFormatter valueFormatter;

    bool showColorBar = true;

    static constexpr size_t kColumnAxis = 0;
    static constexpr size_t kRowAxis = 1;

    void RecomputeRange();
    void RefreshColorBar();
    // Value -> [0,1] colour-map position (NaN passes through), and back.
    double NormalizeValue(double v) const;
    double DenormalizeValue(double t) const;
    bool IsCellVisible(int col, int row) const;
    std::string FormatCell(double v) const;
    std::string ColumnLabel(int col) const;
    std::string RowLabel(int row) const;
};

inline std::shared_ptr<UltraCanvasEngineHeatmapChart> CreateEngineHeatmapChart(
        const std::string& id, int x = 0, int y = 0, int w = 0, int h = 0) {
    return std::make_shared<UltraCanvasEngineHeatmapChart>(id, x, y, w, h);
}

} // namespace UltraCanvas
