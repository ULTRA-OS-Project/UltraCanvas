// include/Plugins/Charts/UltraCanvasDumbbellChart.h
// Dumbbell (DNA / connected dot) chart for comparing paired values across categories
// Version: 1.1.0
// Last Modified: 2026-08-20
// V1.1.0: legend: migrated to the shared ChartLegend component
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasChartElementBase.h"
#include "UltraCanvasChartDataStructures.h"
#include "Plugins/Charts/UltraCanvasChartLegend.h"
#include "UltraCanvasTooltipManager.h"
#include <vector>
#include <string>
#include <functional>
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

// =============================================================================
// DUMBBELL DATA POINT
// =============================================================================

    struct DumbbellDataPoint : public ChartDataPoint {
        std::string categoryLabel;   // Row label, e.g. "Instagram 16-20"
        double value1;               // First value, e.g. men 68
        double value2;               // Second value, e.g. women 83
        std::string label1;          // Name of the first group
        std::string label2;          // Name of the second group
        Color color1;                // Dot colour for value1 (transparent = use chart default)
        Color color2;                // Dot colour for value2 (transparent = use chart default)
        Color lineColor;             // Connector colour (transparent = use chart default)
        // Optional custom tooltip replacing the generated one. Tooltips are rendered
        // as Pango markup: the generated text escapes &, < and > for you, but text
        // supplied here is passed through verbatim so it can carry markup.
        std::string tooltipText;

        DumbbellDataPoint(const std::string& category, double val1, double val2,
                          const std::string& lbl1 = "Group 1", const std::string& lbl2 = "Group 2")
                : ChartDataPoint(val1, val2, 0.0, category, std::abs(val2 - val1)),
                  categoryLabel(category), value1(val1), value2(val2),
                  label1(lbl1), label2(lbl2),
                  color1(Colors::Transparent), color2(Colors::Transparent),
                  lineColor(Colors::Transparent) {}

        // Absolute gap between the two values
        double GetRange() const { return std::abs(value2 - value1); }

        // Signed gap (value2 - value1) - positive when the second group leads
        double GetDelta() const { return value2 - value1; }

        double GetMinValue() const { return std::min(value1, value2); }
        double GetMaxValue() const { return std::max(value1, value2); }
    };

// =============================================================================
// DUMBBELL DATA SOURCE
// =============================================================================

    class DumbbellDataSource : public IChartDataSource {
    private:
        std::vector<DumbbellDataPoint> dumbbellData;

    public:
        // ===== IChartDataSource =====
        size_t GetPointCount() const override { return dumbbellData.size(); }

        ChartDataPoint GetPoint(size_t index) override {
            if (index < dumbbellData.size()) return dumbbellData[index];
            return ChartDataPoint(0, 0);
        }

        bool SupportsStreaming() const override { return false; }

        // CSV layout: categoryLabel,value1,value2[,label1,label2]
        void LoadFromCSV(const std::string& filePath) override;

        // Consecutive pairs of standard points become one dumbbell row
        void LoadFromArray(const std::vector<ChartDataPoint>& data) override {
            dumbbellData.clear();
            for (size_t i = 0; i + 1 < data.size(); i += 2) {
                dumbbellData.emplace_back(data[i].label, data[i].y, data[i + 1].y);
            }
        }

        // ===== DUMBBELL SPECIFIC =====
        void AddDumbbellPoint(const DumbbellDataPoint& point) {
            dumbbellData.push_back(point);
        }

        void AddDumbbellPoint(const std::string& category, double val1, double val2,
                              const std::string& lbl1 = "Group 1", const std::string& lbl2 = "Group 2") {
            dumbbellData.emplace_back(category, val1, val2, lbl1, lbl2);
        }

        // Reference access - the chart renders straight from the vector, no copies
        const DumbbellDataPoint& GetDumbbellPoint(size_t index) const {
            static const DumbbellDataPoint empty("", 0, 0);
            if (index < dumbbellData.size()) return dumbbellData[index];
            return empty;
        }

        DumbbellDataPoint* GetMutableDumbbellPoint(size_t index) {
            if (index < dumbbellData.size()) return &dumbbellData[index];
            return nullptr;
        }

        const std::vector<DumbbellDataPoint>& GetPoints() const { return dumbbellData; }

        void Clear() { dumbbellData.clear(); }

        void LoadDumbbellData(const std::vector<DumbbellDataPoint>& data) { dumbbellData = data; }

        // Min/max across both values of every row. Returns false when there is no data,
        // in which case the caller keeps its own fallback range.
        bool GetValueBounds(double& minValue, double& maxValue) const {
            if (dumbbellData.empty()) {
                minValue = 0.0;
                maxValue = 1.0;
                return false;
            }

            minValue = dumbbellData[0].GetMinValue();
            maxValue = dumbbellData[0].GetMaxValue();
            for (const auto& point : dumbbellData) {
                minValue = std::min(minValue, point.GetMinValue());
                maxValue = std::max(maxValue, point.GetMaxValue());
            }
            return true;
        }
    };

// =============================================================================
// CONFIGURATION ENUMS
// =============================================================================

    enum class DumbbellValueLabelPlacement {
        Auto,      // Inside the dots when they are big enough, otherwise outside
        Inside,    // Always centred on the dots
        Outside    // Always beyond the outer edge of each dot
    };

    enum class DumbbellLegendPosition {
        TopRight,
        TopLeft,
        BottomRight,
        BottomLeft
    };

    enum class DumbbellSortOrder {
        InsertionOrder,     // As added (X11 macros rule out None and Unsorted as names here)
        CategoryAscending,
        CategoryDescending,
        Value1Ascending,
        Value1Descending,
        Value2Ascending,
        Value2Descending,
        RangeAscending,     // Smallest gap first
        RangeDescending     // Largest gap first
    };

// =============================================================================
// DUMBBELL CHART ELEMENT
// =============================================================================

    class UltraCanvasDumbbellChart : public UltraCanvasChartElementBase {
    private:
        std::shared_ptr<DumbbellDataSource> dumbbellDataSource;

        // Row order actually drawn (indices into the data source)
        std::vector<size_t> displayOrder;
        DumbbellSortOrder sortOrder = DumbbellSortOrder::InsertionOrder;
        bool orderValid = false;

        // Dot / connector geometry
        float dotRadius = 7.0f;
        float lineWidth = 2.0f;
        float rowHeight = 26.0f;      // Used when autoFitRows is off
        float rowSpacing = 8.0f;      // Gap between rows
        bool autoFitRows = true;      // Squeeze every row into the plot area

        // Category labels
        bool showCategoryLabels = true;
        float categoryLabelWidth = 170.0f;
        float categoryLabelFontSize = 11.0f;
        Color categoryLabelColor = Color(60, 60, 60, 255);

        // Value labels (base class owns showValueLabels / valueLabelFontSize / valueLabelColor)
        DumbbellValueLabelPlacement valueLabelPlacement = DumbbellValueLabelPlacement::Auto;
        std::string valueFormat = "%.0f%%";

        // Group colours
        Color defaultColor1 = Color(70, 130, 190, 255);
        Color defaultColor2 = Color(214, 96, 130, 255);
        Color defaultLineColor = Color(190, 190, 190, 255);

        // Legend - drawn by the shared ChartLegend component; the labels feed
        // its entry list (see RefreshLegendEntries)
        ChartLegend legend;
        std::string legendLabel1 = "Group 1";
        std::string legendLabel2 = "Group 2";

        // Axis
        bool showAxisValues = true;
        int numAxisTicks = 5;
        std::string axisLabel;
        bool includeZeroInRange = false;
        bool useNiceAxisBounds = true;
        bool useManualRange = false;
        double manualMinValue = 0.0;
        double manualMaxValue = 100.0;

        // Row banding / highlight
        bool showRowHighlight = true;
        Color rowHighlightColor = Color(0, 0, 0, 18);

        // Layout constants
        float categoryLabelGap = 12.0f;
        float rightMargin = 24.0f;

        // Cached axis range recomputed with the rendering cache
        double axisMin = 0.0;
        double axisMax = 1.0;
        double axisStep = 1.0;

        // Interaction state
        int hoveredRowIndex = -1;   // Index into displayOrder
        int hoveredDotIndex = -1;   // 0 = value1 dot, 1 = value2 dot

    public:
        UltraCanvasDumbbellChart(const std::string& id, int x, int y, int width, int height);

        // ===== DATA =====
        void SetDumbbellDataSource(std::shared_ptr<DumbbellDataSource> data);
        std::shared_ptr<DumbbellDataSource> GetDumbbellDataSource() const { return dumbbellDataSource; }

        void AddDataPoint(const std::string& category, double value1, double value2,
                          const std::string& label1 = "Group 1", const std::string& label2 = "Group 2");
        void AddDataPoint(const DumbbellDataPoint& point);
        void ClearData();

        size_t GetDataPointCount() const {
            return dumbbellDataSource ? dumbbellDataSource->GetPointCount() : 0;
        }

        void SetSortOrder(DumbbellSortOrder order);
        DumbbellSortOrder GetSortOrder() const { return sortOrder; }

        // ===== GEOMETRY =====
        void SetDotRadius(float radius);
        float GetDotRadius() const { return dotRadius; }

        void SetLineWidth(float width);
        float GetLineWidth() const { return lineWidth; }

        void SetRowHeight(float height);
        float GetRowHeight() const { return rowHeight; }

        void SetRowSpacing(float spacing);
        float GetRowSpacing() const { return rowSpacing; }

        void SetAutoFitRows(bool autoFit);
        bool GetAutoFitRows() const { return autoFitRows; }

        // Height needed to draw every row at the configured pitch without squeezing
        float GetPreferredHeight() const;

        // ===== COLOURS =====
        void SetDefaultColors(const Color& color1, const Color& color2, const Color& lineCol);
        void SetGroupColors(const Color& color1, const Color& color2);
        void SetConnectorColor(const Color& lineCol);

        // ===== LABELS =====
        void SetShowCategoryLabels(bool show);
        void SetCategoryLabelWidth(float width);
        void SetCategoryLabelFontSize(float size);
        void SetCategoryLabelColor(const Color& color);

        void SetValueFormat(const std::string& format);
        const std::string& GetValueFormat() const { return valueFormat; }
        void SetValueLabelPlacement(DumbbellValueLabelPlacement placement);

        // ===== LEGEND =====
        void SetShowLegend(bool show);
        bool GetShowLegend() const { return legend.IsVisible(); }
        void SetLegendLabels(const std::string& label1, const std::string& label2);
        void SetLegendPosition(DumbbellLegendPosition position);

        // ===== AXIS =====
        void SetShowAxisValues(bool show);
        void SetAxisTickCount(int ticks);
        void SetAxisLabel(const std::string& label);
        void SetIncludeZeroInRange(bool include);
        void SetUseNiceAxisBounds(bool useNice);
        void SetValueRange(double minValue, double maxValue);   // Pin the axis
        void ClearValueRange();                                 // Back to automatic

        void SetShowRowHighlight(bool show);

        // ===== CALLBACKS =====
        // rowIndex is the index into the data source, dotIndex is 0 for value1 and 1 for value2
        std::function<void(size_t rowIndex, int dotIndex)> onDotClick;
        std::function<void(size_t rowIndex)> onRowClick;

        // ===== UIELEMENT / CHART BASE OVERRIDES =====
        void RenderChart(IRenderContext* ctx) override;
        bool HandleChartMouseMove(const Point2Di& mousePos) override;
        bool OnEvent(const UCEvent& event) override;

    protected:
        ChartPlotArea CalculatePlotArea() override;
        ChartDataBounds CalculateDataBounds() override;
        void UpdateRenderingCache() override;
        void RenderCommonBackground(IRenderContext* ctx) override;
        void RenderGrid(IRenderContext* ctx) override;
        void RenderAxes(IRenderContext* ctx) override;
        void RenderAxisLabels(IRenderContext* ctx) override;

    private:
        // Rendering helpers
        void RenderRowHighlight(IRenderContext* ctx);
        void RenderCategoryLabels(IRenderContext* ctx);
        void RenderDumbbellRows(IRenderContext* ctx);

        // Shared-legend plumbing: entry list, measure pass and the band the
        // legend may occupy (above or below the plot columns).
        void UpdateLegendLayout(IRenderContext* ctx);
        void RefreshLegendEntries();
        Rect2Dd GetLegendContentArea() const;
        void DrawDumbbellRow(IRenderContext* ctx, const DumbbellDataPoint& point,
                             int displayIndex, double yCenter, double effRadius);
        void DrawDot(IRenderContext* ctx, double x, double y, double radius,
                     const Color& fillColor, bool highlighted);
        void DrawValueLabels(IRenderContext* ctx, const DumbbellDataPoint& point,
                             double x1, double x2, double yCenter, double effRadius);

        // Layout helpers
        float GetMarginTop() const;
        float GetMarginBottom() const;
        float GetMarginLeft() const;
        double GetRowPitch() const;
        double GetEffectiveDotRadius() const;
        double GetRowCenterY(size_t displayIndex) const;
        double ValueToX(double value) const;

        void RebuildDisplayOrder();
        void UpdateAxisRange();

        std::string FormatValue(double value) const;
        std::string TruncateToWidth(IRenderContext* ctx, const std::string& text, double maxWidth) const;

        Color ResolveColor1(const DumbbellDataPoint& point) const {
            return point.color1.a > 0 ? point.color1 : defaultColor1;
        }
        Color ResolveColor2(const DumbbellDataPoint& point) const {
            return point.color2.a > 0 ? point.color2 : defaultColor2;
        }
        Color ResolveLineColor(const DumbbellDataPoint& point) const {
            return point.lineColor.a > 0 ? point.lineColor : defaultLineColor;
        }

        // Hit testing - both take element local coordinates
        int FindHoveredRow(const Point2Di& mousePos) const;
        int FindHoveredDot(const Point2Di& mousePos, int displayIndex) const;

        std::string GenerateDumbbellTooltip(const DumbbellDataPoint& point, int dotIndex) const;
    };

// =============================================================================
// FACTORY FUNCTIONS
// =============================================================================

    inline std::shared_ptr<DumbbellDataSource> CreateDumbbellDataSource() {
        return std::make_shared<DumbbellDataSource>();
    }

    inline std::shared_ptr<UltraCanvasDumbbellChart> CreateDumbbellChart(
            const std::string& id, int x, int y, int width, int height) {
        return std::make_shared<UltraCanvasDumbbellChart>(id, x, y, width, height);
    }

    inline std::shared_ptr<UltraCanvasDumbbellChart> CreateDumbbellChartWithData(
            const std::string& id, int x, int y, int width, int height,
            std::shared_ptr<DumbbellDataSource> data, const std::string& title = "") {

        auto chart = CreateDumbbellChart(id, x, y, width, height);
        chart->SetDumbbellDataSource(data);
        if (!title.empty()) {
            chart->SetTitle(title);
        }
        return chart;
    }

} // namespace UltraCanvas
