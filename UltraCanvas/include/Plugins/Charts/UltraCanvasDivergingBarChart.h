// UltraCanvasDivergingBarChart.h
// Diverging bar chart component for multi-valued categorical data (population pyramid, likert scales, etc.)
// Version: 1.1.0
// Last Modified: 2025-09-23
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasChartElementBase.h"
#include "UltraCanvasChartDataStructures.h"
#include "UltraCanvasDivergingDataSource.h"
#include "UltraCanvasTooltipManager.h"
#include <vector>
#include <map>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <cstdio>

namespace UltraCanvas {

// ===== DIVERGING CHART STYLES =====
    enum class DivergingChartStyle {
        PopulationPyramid,   // Symmetric bars extending from center (like the image)
        TornadoChart,        // Single bars with positive/negative values
        LikertScale,         // Stacked segments diverging from center
        OpposingBars         // Two separate bars meeting at center
    };

// ===== VALUE CATEGORIES FOR DIVERGING DATA =====
    struct DivergingCategory {
        std::string name;        // Category name (e.g., "Strongly Agree", "Disagree")
        Color categoryColor;     // Color for this category
        bool isPositive;        // True if on right/positive side, false if on left/negative side

        DivergingCategory(const std::string& name, const Color& color, bool positive)
                : name(name), categoryColor(color), isPositive(positive) {}
    };

// ===== DIVERGING BAR CHART ELEMENT =====
    class UltraCanvasDivergingBarChart : public UltraCanvasChartElementBase {
    private:
        // Chart configuration
        DivergingChartStyle chartStyle = DivergingChartStyle::PopulationPyramid;

        // Use diverging data source
        std::shared_ptr<DivergingDataSource> divergingDataSource;
        std::vector<DivergingCategory> categories;

        // Visual properties
        float barHeight = 0.8f;           // Height of bars as percentage of row height
        float barSpacing = 0.2f;          // Vertical spacing between bars
        float centerGap = 10.0f;          // Gap at the center axis in pixels
        bool showCenterLine = true;       // Show vertical line at center
        Color centerLineColor = Color(128, 128, 128, 255);
        float centerLineWidth = 1.0f;

        // Labels and text
        bool showRowLabels = true;
        bool showValueLabels = false;
        float labelFontSize = 10.0f;
        Color labelColor = Color(60, 60, 60, 255);

        // Interaction state
        int hoveredRowIndex = -1;
        std::string hoveredCategory = "";

        // Animation
        bool enableAnimation = true;
        float animationProgress = 0.0f;
        std::chrono::steady_clock::time_point animationStartTime;

        // Cached calculations
        float maxNegativeValue = 0.0f;
        float maxPositiveValue = 0.0f;
        bool needsRecalculation = true;

    protected:
        ChartPlotArea CalculatePlotArea() override;
        ChartDataBounds CalculateDataBounds() override;

    public:
        // Constructor
        UltraCanvasDivergingBarChart(const std::string& id, long uid, int x, int y, int width, int height);

        // ===== DATA MANAGEMENT =====
        // Set custom categories
        void SetCategories(const std::vector<DivergingCategory>& cats);

        // Add a data row
        void AddDataRow(const std::string& rowLabel, const std::map<std::string, float>& values);

        // Load data matrix (rows x categories)
        void LoadDataMatrix(const std::vector<std::string>& rowLabels,
                            const std::vector<std::vector<float>>& matrix);

        // Clear all data
        void ClearData();

        // ===== CONFIGURATION =====

        void SetChartStyle(DivergingChartStyle style);
        void SetBarHeight(float height);
        void SetCenterGap(float gap);
        void SetShowCenterLine(bool show);
        void SetShowRowLabels(bool show);
        void SetShowValueLabels(bool show);

        // ===== RENDERING =====

        void RenderChart(IRenderContext* ctx) override;

        // ===== INTERACTION =====

        bool HandleChartMouseMove(const Point2Di& mousePos) override;

        bool HandleMouseClick(const Point2Di& mousePos);

        // ===== CALLBACKS =====

        std::function<void(int rowIndex, const std::string& category)> onBarClick;
        std::function<void(int rowIndex, const std::string& category)> onBarHover;

    private:
        // ===== INTERNAL RENDERING METHODS =====
        void RenderGrid(IRenderContext* ctx) override;
        void RenderCenterLine(IRenderContext* ctx);
        void RenderPopulationPyramid(IRenderContext* ctx, float animScale);
        void RenderLikertScale(IRenderContext* ctx, float animScale);
        void RenderTornadoChart(IRenderContext* ctx, float animScale);
        void RenderOpposingBars(IRenderContext* ctx, float animScale);
        void RenderRowLabels(IRenderContext* ctx);
        void RenderAxisLabels(IRenderContext* ctx) override;

        // Helper function to get nice round numbers for axis labels
        float GetNiceRoundNumber(float value);
        void FindHoveredBar(const Point2Di& mousePos);
        void UpdateTooltip(const Point2Di& mousePos);
        float GetAnimationScale();
    };

// ===== FACTORY FUNCTIONS =====

    inline std::shared_ptr<UltraCanvasDivergingBarChart> CreateDivergingBarChart(
            const std::string& id, long uid, int x, int y, int width, int height) {
        return std::make_shared<UltraCanvasDivergingBarChart>(id, uid, x, y, width, height);
    }

    inline std::shared_ptr<UltraCanvasDivergingBarChart> CreatePopulationPyramid(
            const std::string& id, long uid, int x, int y, int width, int height,
            const std::vector<std::string>& rowLabels,
            const std::vector<DivergingCategory>& categories) {

        auto chart = CreateDivergingBarChart(id, uid, x, y, width, height);
        chart->SetChartStyle(DivergingChartStyle::PopulationPyramid);
        chart->SetCategories(categories);
        return chart;
    }

    inline std::shared_ptr<UltraCanvasDivergingBarChart> CreateLikertChart(
            const std::string& id, long uid, int x, int y, int width, int height) {

        auto chart = CreateDivergingBarChart(id, uid, x, y, width, height);
        chart->SetChartStyle(DivergingChartStyle::LikertScale);
        return chart;
    }

} // namespace UltraCanvas