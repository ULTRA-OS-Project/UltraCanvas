// include/Plugins/Charts/UltraCanvasMekkoChart.h
// Mekko (Marimekko / mosaic) chart element with variable-width 100% stacked columns
// Version: 1.0.0
// Last Modified: 2026-07-28
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasChartElementBase.h"
#include "UltraCanvasChartDataStructures.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// =============================================================================
// MEKKO CHART DATA STRUCTURES
// =============================================================================

    // A series is one horizontal band of the chart (one legend entry).
    struct MekkoChartSeries {
        std::string name;
        Color color;

        MekkoChartSeries(const std::string& seriesName, const Color& seriesColor)
                : name(seriesName), color(seriesColor) {}
    };

    // A column is one variable-width vertical bar; it holds one value per series.
    struct MekkoChartColumn {
        std::string label;
        std::vector<double> values;     // Aligned with the series list; missing = 0
        std::string category;           // Optional grouping/annotation

        MekkoChartColumn(const std::string& columnLabel) : label(columnLabel) {}
        MekkoChartColumn(const std::string& columnLabel, const std::vector<double>& columnValues)
                : label(columnLabel), values(columnValues) {}

        double GetTotal() const {
            double total = 0.0;
            for (double v : values) {
                if (v > 0) total += v;
            }
            return total;
        }
    };

    class MekkoChartDataVector : public IChartDataSource {
    private:
        std::vector<MekkoChartSeries> series;
        std::vector<MekkoChartColumn> columns;

    public:
        // ===== Series management =====
        // Returns the index of the new series. Pass Colors::Transparent to let
        // the chart element assign a palette color automatically.
        size_t AddSeries(const std::string& name, const Color& color = Colors::Transparent);
        size_t GetSeriesCount() const { return series.size(); }
        const MekkoChartSeries& GetSeries(size_t index) const;
        void SetSeriesColor(size_t index, const Color& color);
        int FindSeries(const std::string& name) const;

        // ===== Column management =====
        // Values are given in series order; missing trailing values default to 0.
        size_t AddColumn(const std::string& label, const std::vector<double>& values = {});
        size_t GetColumnCount() const { return columns.size(); }
        const MekkoChartColumn& GetColumn(size_t index) const;
        int FindColumn(const std::string& label) const;

        // Set one cell (column x series)
        void SetValue(size_t columnIndex, size_t seriesIndex, double value);
        void SetValue(const std::string& columnLabel, const std::string& seriesName, double value);
        double GetValue(size_t columnIndex, size_t seriesIndex) const;

        void SetColumnCategory(size_t columnIndex, const std::string& category);

        // ===== Aggregates =====
        double GetColumnTotal(size_t columnIndex) const;
        double GetSeriesTotal(size_t seriesIndex) const;
        double GetGrandTotal() const;

        void ClearData() {
            series.clear();
            columns.clear();
        }

        // ===== IChartDataSource implementation =====
        // Each point is one column: x = column index, y = column total.
        size_t GetPointCount() const override { return columns.size(); }
        ChartDataPoint GetPoint(size_t index) override;
        bool SupportsStreaming() const override { return false; }
        // CSV format: header "Column,Series1,Series2,..." then one row per column.
        void LoadFromCSV(const std::string& filePath) override;
        void LoadFromArray(const std::vector<ChartDataPoint>& data) override {}
    };

// =============================================================================
// MEKKO CHART ELEMENT
// =============================================================================

    class UltraCanvasMekkoChartElement : public UltraCanvasChartElementBase {
    public:
        enum class MekkoMode {
            Marimekko,      // 100% stacked: column height normalized, Y axis in percent
            BarMekko        // Absolute stacked: column height = column total, Y axis in values
        };

        enum class ColumnSortMode {
            DataOrder,          // Keep insertion order
            TotalDescending,    // Largest column first
            TotalAscending,     // Smallest column first
            Alphabetical        // Sort by column label
        };

        enum class SegmentLabelMode {
            NoLabels,           // No text inside segments
            Value,              // Absolute value
            Percent,            // Percent of column
            ValueAndPercent,    // "value (percent)"
            SeriesName,         // Series name (useful when legend is hidden)
            SeriesAndValue      // "series: value"
        };

        enum class ColumnHeaderMode {
            NoHeader,           // Nothing above the columns
            WidthPercent,       // Column share of grand total, e.g. "20%"
            Total,              // Column total, e.g. "$10.4T"
            TotalAndPercent     // "total (percent)"
        };

        enum class LegendPosition {
            Hidden,
            Right,
            Bottom,
            Top
        };

        // Callback fired on left click over a segment: (columnIndex, seriesIndex)
        using SegmentClickCallback = std::function<void(size_t, size_t)>;
        // Custom value formatter, e.g. money formatting
        using ValueFormatter = std::function<std::string(double)>;

    private:
        // ===== Configuration =====
        MekkoMode mekkoMode = MekkoMode::Marimekko;
        ColumnSortMode columnSortMode = ColumnSortMode::DataOrder;
        SegmentLabelMode segmentLabelMode = SegmentLabelMode::Value;
        ColumnHeaderMode columnHeaderMode = ColumnHeaderMode::WidthPercent;
        LegendPosition legendPosition = LegendPosition::Right;

        float columnGap = 2.0f;                 // Pixels between columns
        Color segmentBorderColor = Colors::White;
        float segmentBorderWidth = 1.0f;
        float segmentCornerRadius = 0.0f;       // >0 draws rounded segments

        bool showColumnLabels = true;           // Category labels under the columns
        bool showYAxisLabels = true;            // Percent/value scale at the left
        bool showCumulativeXAxis = false;       // Cumulative percent ticks under columns

        Color labelTextColor = Color(33, 33, 33, 255);
        float labelFontSize = 11.0f;
        float segmentLabelFontSize = 10.0f;
        float headerFontSize = 11.0f;
        float minSegmentLabelHeight = 14.0f;    // Segments shorter than this get no label
        bool autoContrastSegmentLabels = true;  // Pick black/white text per segment color
        Color segmentLabelColor = Colors::White; // Used when autoContrast is off

        float legendSwatchSize = 12.0f;
        float legendItemSpacing = 6.0f;
        float legendFontSize = 11.0f;
        int legendWidth = 130;                  // Reserved width for LegendPosition::Right

        bool highlightHoveredSegment = true;
        Color hoverBorderColor = Color(33, 33, 33, 255);
        float hoverBorderWidth = 2.0f;

        ValueFormatter valueFormatter;          // Optional; falls back to FormatValue()
        std::string valueSuffix;                // e.g. "%", "T", "K" appended by FormatValue
        std::string valuePrefix;                // e.g. "$"

        SegmentClickCallback onSegmentClick;

        // Default categorical palette used when a series has no explicit color
        static const std::vector<Color>& DefaultPalette();

        // ===== Cached layout =====
        struct SegmentRect {
            size_t columnIndex;
            size_t seriesIndex;
            Rect2Dd rect;
            Color color;
            double value;
            double percentOfColumn;   // 0..100
        };

        struct MekkoRenderCache {
            std::vector<size_t> columnOrder;      // Display order -> data column index
            std::vector<double> columnX;          // Left edge per displayed column
            std::vector<double> columnWidth;      // Width per displayed column
            std::vector<SegmentRect> segments;
            std::vector<Rect2Dd> legendItemRects; // For legend hit testing
            double grandTotal = 0.0;
            double maxColumnTotal = 0.0;
            bool isValid = false;
        } renderCache;

        size_t hoveredSegment = SIZE_MAX;         // Index into renderCache.segments
        size_t hoveredLegendSeries = SIZE_MAX;    // Series index dimming others

    public:
        UltraCanvasMekkoChartElement(const std::string& id, int x, int y, int width, int height)
                : UltraCanvasChartElementBase(id, x, y, width, height) {
            enableTooltips = true;
            enableZoom = false;
            enablePan = false;
            showGrid = false;   // Mekko draws its own percent gridlines
            showAxes = false;   // ...and its own axis labels
        }

        // =============================================================================
        // DATA SOURCE
        // =============================================================================

        void SetMekkoDataSource(std::shared_ptr<MekkoChartDataVector> ds) {
            dataSource = ds;
            InvalidateMekkoCache();
            RequestRedraw();
        }

        std::shared_ptr<MekkoChartDataVector> GetMekkoDataSource() const {
            return std::dynamic_pointer_cast<MekkoChartDataVector>(dataSource);
        }

        // =============================================================================
        // CONFIGURATION METHODS
        // =============================================================================

        void SetMekkoMode(MekkoMode mode) {
            mekkoMode = mode;
            InvalidateMekkoCache();
            RequestRedraw();
        }
        MekkoMode GetMekkoMode() const { return mekkoMode; }

        void SetColumnSortMode(ColumnSortMode mode) {
            columnSortMode = mode;
            InvalidateMekkoCache();
            RequestRedraw();
        }
        ColumnSortMode GetColumnSortMode() const { return columnSortMode; }

        void SetSegmentLabelMode(SegmentLabelMode mode) {
            segmentLabelMode = mode;
            RequestRedraw();
        }
        SegmentLabelMode GetSegmentLabelMode() const { return segmentLabelMode; }

        void SetColumnHeaderMode(ColumnHeaderMode mode) {
            columnHeaderMode = mode;
            RequestRedraw();
        }
        ColumnHeaderMode GetColumnHeaderMode() const { return columnHeaderMode; }

        void SetLegendPosition(LegendPosition position) {
            legendPosition = position;
            InvalidateMekkoCache();
            RequestRedraw();
        }
        LegendPosition GetLegendPosition() const { return legendPosition; }

        void SetLegendWidth(int width) {
            legendWidth = std::max(40, width);
            InvalidateMekkoCache();
            RequestRedraw();
        }

        void SetColumnGap(float gap) {
            columnGap = std::max(0.0f, gap);
            InvalidateMekkoCache();
            RequestRedraw();
        }

        void SetSegmentBorder(const Color& color, float width) {
            segmentBorderColor = color;
            segmentBorderWidth = width;
            RequestRedraw();
        }

        void SetSegmentCornerRadius(float radius) {
            segmentCornerRadius = std::max(0.0f, radius);
            RequestRedraw();
        }

        void SetShowColumnLabels(bool show) {
            showColumnLabels = show;
            InvalidateMekkoCache();
            RequestRedraw();
        }

        void SetShowYAxisLabels(bool show) {
            showYAxisLabels = show;
            InvalidateMekkoCache();
            RequestRedraw();
        }

        void SetShowCumulativeXAxis(bool show) {
            showCumulativeXAxis = show;
            InvalidateMekkoCache();
            RequestRedraw();
        }

        void SetLabelStyle(const Color& textColor, float fontSize) {
            labelTextColor = textColor;
            labelFontSize = fontSize;
            RequestRedraw();
        }

        void SetSegmentLabelStyle(float fontSize, float minLabelHeight,
                                  bool autoContrast = true,
                                  const Color& fixedColor = Colors::White) {
            segmentLabelFontSize = fontSize;
            minSegmentLabelHeight = minLabelHeight;
            autoContrastSegmentLabels = autoContrast;
            segmentLabelColor = fixedColor;
            RequestRedraw();
        }

        void SetHighlightHoveredSegment(bool enable) {
            highlightHoveredSegment = enable;
            RequestRedraw();
        }

        void SetValueFormatter(ValueFormatter formatter) {
            valueFormatter = formatter;
            RequestRedraw();
        }

        void SetValueUnits(const std::string& prefix, const std::string& suffix) {
            valuePrefix = prefix;
            valueSuffix = suffix;
            RequestRedraw();
        }

        void SetOnSegmentClick(SegmentClickCallback callback) {
            onSegmentClick = callback;
        }

        // Returns (columnIndex, seriesIndex) of the hovered segment or (SIZE_MAX, SIZE_MAX)
        std::pair<size_t, size_t> GetHoveredSegment() const;

        // =============================================================================
        // CHART RENDERING IMPLEMENTATION
        // =============================================================================

        void RenderChart(IRenderContext* ctx) override;
        bool HandleChartMouseMove(const Point2Di& mousePos) override;
        bool OnEvent(const UCEvent& event) override;

    protected:
        ChartPlotArea CalculatePlotArea() override;
        ChartDataBounds CalculateDataBounds() override;
        void UpdateRenderingCache() override;

    private:
        void InvalidateMekkoCache() {
            renderCache.isValid = false;
            InvalidateCache();
        }

        void BuildColumnOrder(std::shared_ptr<MekkoChartDataVector>& data);
        void BuildSegmentRects(std::shared_ptr<MekkoChartDataVector>& data);

        void DrawPercentGrid(IRenderContext* ctx);
        void DrawSegments(IRenderContext* ctx);
        void DrawSegmentLabels(IRenderContext* ctx);
        void DrawColumnHeaders(IRenderContext* ctx);
        void DrawColumnLabels(IRenderContext* ctx);
        void DrawYAxisLabels(IRenderContext* ctx);
        void DrawCumulativeXAxis(IRenderContext* ctx);
        void DrawLegend(IRenderContext* ctx);

        Color GetSeriesColor(size_t seriesIndex) const;
        Color GetContrastTextColor(const Color& background) const;
        std::string FormatValue(double value) const;
        std::string FormatPercent(double percent) const;

        size_t GetSegmentIndexAtPosition(const Point2Di& mousePos) const;
        size_t GetLegendSeriesAtPosition(const Point2Di& mousePos) const;
        std::string GenerateSegmentTooltip(size_t segmentIndex) const;
    };

// =============================================================================
// FACTORY FUNCTIONS
// =============================================================================

    inline std::shared_ptr<UltraCanvasMekkoChartElement> CreateMekkoChartElement(
            const std::string& id, int x, int y, int width, int height) {
        return std::make_shared<UltraCanvasMekkoChartElement>(id, x, y, width, height);
    }

    inline std::shared_ptr<UltraCanvasMekkoChartElement> CreateMekkoChartWithData(
            const std::string& id, int x, int y, int width, int height,
            std::shared_ptr<MekkoChartDataVector> data, const std::string& title = "") {

        auto chart = CreateMekkoChartElement(id, x, y, width, height);
        chart->SetMekkoDataSource(data);
        if (!title.empty()) {
            chart->SetChartTitle(title);
        }
        return chart;
    }

} // namespace UltraCanvas
