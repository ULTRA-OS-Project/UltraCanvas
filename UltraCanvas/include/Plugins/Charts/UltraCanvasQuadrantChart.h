// include/Plugins/Charts/UltraCanvasQuadrantChart.h
// Quadrant chart element for strategic analysis (SWOT, BCG, Eisenhower, risk & priority matrices)
// Version: 1.0.0
// Last Modified: 2026-07-25
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "Plugins/Charts/UltraCanvasChartElementBase.h"
#include <vector>
#include <string>
#include <functional>
#include <memory>

namespace UltraCanvas {

// =============================================================================
// QUADRANT CHART ENUMERATIONS
// =============================================================================

    enum class QuadrantType {
        SWOT,           // Strengths, Weaknesses, Opportunities, Threats
        BCG,            // Boston Consulting Group matrix (Stars, Cash Cows, Question Marks, Dogs)
        Ansoff,         // Ansoff growth matrix (Market Penetration/Development, Product Development, Diversification)
        Eisenhower,     // Eisenhower decision matrix (Do First, Schedule, Delegate, Eliminate)
        MagicQuadrant,  // Gartner-style magic quadrant (Leaders, Challengers, Visionaries, Niche Players)
        RiskMatrix,     // Risk assessment matrix (probability vs impact)
        Priority,       // Priority matrix (impact vs effort)
        Custom          // User-defined quadrant labels
    };

    enum class QuadrantPosition {
        TopLeft = 0,     // Quadrant II
        TopRight = 1,    // Quadrant I
        BottomLeft = 2,  // Quadrant III
        BottomRight = 3  // Quadrant IV
    };

    enum class QuadrantPointShape {
        Circle,
        Square,
        Triangle,
        Diamond
    };

    enum class QuadrantStyle {
        Clean,          // Minimal design
        Business,       // Professional business style
        Scientific,     // Academic/research style with grid
        Modern          // Contemporary flat design
    };

// =============================================================================
// QUADRANT CHART DATA STRUCTURES
// =============================================================================

    struct QuadrantDataPoint {
        std::string label;
        float xValue = 0.0f;    // In axis units (default axis range is 0.0 .. 1.0)
        float yValue = 0.0f;    // In axis units (default axis range is 0.0 .. 1.0)
        Color pointColor = Color(100, 150, 255, 255);
        float radius = 6.0f;
        QuadrantPointShape shape = QuadrantPointShape::Circle;
        Color strokeColor = Color(50, 50, 50, 255);
        float strokeWidth = 1.0f;

        QuadrantDataPoint() = default;
        QuadrantDataPoint(const std::string& lbl, float x, float y)
            : label(lbl), xValue(x), yValue(y) {}
        QuadrantDataPoint(const std::string& lbl, float x, float y, const Color& color)
            : label(lbl), xValue(x), yValue(y), pointColor(color) {}
    };

    struct QuadrantDefinition {
        std::string topLeftLabel = "High Y / Low X";
        std::string topRightLabel = "High Y / High X";
        std::string bottomLeftLabel = "Low Y / Low X";
        std::string bottomRightLabel = "Low Y / High X";

        Color topLeftColor = Color(255, 200, 200, 100);      // Light red
        Color topRightColor = Color(200, 255, 200, 100);     // Light green
        Color bottomLeftColor = Color(200, 200, 255, 100);   // Light blue
        Color bottomRightColor = Color(255, 255, 200, 100);  // Light yellow

        // Predefined configurations
        static QuadrantDefinition SWOT();
        static QuadrantDefinition BCG();
        static QuadrantDefinition Ansoff();
        static QuadrantDefinition Eisenhower();
        static QuadrantDefinition MagicQuadrant();
        static QuadrantDefinition RiskMatrix();
        static QuadrantDefinition Priority();
    };

    struct QuadrantAxisConfiguration {
        std::string xAxisLabel = "X Axis";
        std::string yAxisLabel = "Y Axis";
        std::string xAxisLeftLabel = "Low";
        std::string xAxisRightLabel = "High";
        std::string yAxisBottomLabel = "Low";
        std::string yAxisTopLabel = "High";

        bool showAxisLabels = true;      // The two main axis captions
        bool showAxisEndLabels = true;   // Low/High captions at the axis ends
        bool showCenterLines = true;     // The two dividing lines through the center

        Color axisColor = Color(100, 100, 100, 255);
        Color centerLineColor = Color(150, 150, 150, 200);
        float centerLineWidth = 1.5f;

        // Data range and quadrant division point (in data units)
        float xMin = 0.0f;
        float xMax = 1.0f;
        float yMin = 0.0f;
        float yMax = 1.0f;
        float xCenter = 0.5f;
        float yCenter = 0.5f;
    };

// =============================================================================
// QUADRANT CHART ELEMENT CLASS
// =============================================================================

    class UltraCanvasQuadrantChart : public UltraCanvasChartElementBase {
    private:
        // ===== DATA =====
        std::vector<QuadrantDataPoint> dataPoints;
        QuadrantDefinition quadrantDef;
        QuadrantAxisConfiguration axisConfig;
        QuadrantType chartType = QuadrantType::Custom;
        QuadrantStyle visualStyle = QuadrantStyle::Business;

        // ===== SELECTION & INTERACTION =====
        std::vector<size_t> selectedPoints;
        int hoveredPoint = -1;
        bool enableMultiSelection = true;

        // ===== VISUAL CUSTOMIZATION =====
        bool showQuadrantLabels = true;
        bool showQuadrantColors = true;
        bool showDataPointLabels = false;
        Color plotBorderColor = Color(120, 120, 120, 255);
        float plotBorderWidth = 1.0f;

        // ===== FONT SETTINGS =====
        float titleFontSize = 16.0f;
        float quadrantLabelFontSize = 12.0f;
        float axisLabelFontSize = 10.0f;
        float dataPointLabelFontSize = 9.0f;

        // ===== LAYOUT CACHE (local coordinates) =====
        struct QuadrantLayout {
            Rect2Dd plotArea;
            Rect2Dd topLeft, topRight, bottomLeft, bottomRight;
            Point2Dd centerPoint;
            double xScale = 1.0, yScale = 1.0;
            bool valid = false;
        };
        QuadrantLayout layout;

    public:
        // ===== CONSTRUCTOR =====
        UltraCanvasQuadrantChart(const std::string& id, int x, int y, int w, int h);

        // ===== CHART TYPE & CONFIGURATION =====
        void SetQuadrantType(QuadrantType type);
        QuadrantType GetQuadrantType() const { return chartType; }

        void SetQuadrantDefinition(const QuadrantDefinition& def);
        const QuadrantDefinition& GetQuadrantDefinition() const { return quadrantDef; }

        void SetAxisConfiguration(const QuadrantAxisConfiguration& config);
        const QuadrantAxisConfiguration& GetAxisConfiguration() const { return axisConfig; }

        void SetQuadrantStyle(QuadrantStyle style);
        QuadrantStyle GetQuadrantStyle() const { return visualStyle; }

        // ===== DATA MANAGEMENT =====
        void AddDataPoint(const QuadrantDataPoint& point);
        void AddDataPoint(const std::string& label, float x, float y,
                          const Color& color = Color(100, 150, 255, 255));
        void SetDataPoints(const std::vector<QuadrantDataPoint>& points);
        const std::vector<QuadrantDataPoint>& GetDataPoints() const { return dataPoints; }
        void ClearDataPoints();
        void RemoveDataPoint(size_t index);

        // Loads a small built-in sample data set matching the current chart type
        void LoadSampleData();

        // ===== VISUAL TOGGLES =====
        void SetShowQuadrantLabels(bool show);
        bool GetShowQuadrantLabels() const { return showQuadrantLabels; }

        void SetShowQuadrantColors(bool show);
        bool GetShowQuadrantColors() const { return showQuadrantColors; }

        void SetShowDataPointLabels(bool show);
        bool GetShowDataPointLabels() const { return showDataPointLabels; }

        // ===== INTERACTION SETTINGS =====
        // Selection and tooltips are controlled by the base class:
        // SetEnableSelection(bool) / SetEnableTooltips(bool)
        void SetEnableMultiSelection(bool enable) { enableMultiSelection = enable; }
        bool GetEnableMultiSelection() const { return enableMultiSelection; }

        // ===== SELECTION MANAGEMENT =====
        void SelectDataPoint(size_t index);
        void DeselectDataPoint(size_t index);
        void ClearSelection();
        const std::vector<size_t>& GetSelectedPoints() const { return selectedPoints; }
        bool IsPointSelected(size_t index) const;

        // ===== QUADRANT UTILITIES =====
        QuadrantPosition GetPointQuadrant(const QuadrantDataPoint& point) const;
        std::vector<size_t> GetPointsInQuadrant(QuadrantPosition quadrant) const;
        size_t CountPointsInQuadrant(QuadrantPosition quadrant) const;
        const std::string& GetQuadrantLabel(QuadrantPosition quadrant) const;

        // ===== CALLBACKS =====
        std::function<void(size_t, const QuadrantDataPoint&)> onPointSelect;
        std::function<void(size_t, const QuadrantDataPoint&)> onPointDeselect;
        std::function<void(size_t, const QuadrantDataPoint&)> onPointHover;
        std::function<void(size_t, const QuadrantDataPoint&)> onPointDoubleClick;
        std::function<void()> onSelectionChange;

        // ===== OVERRIDES =====
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        void RenderChart(IRenderContext* ctx) override;
        bool HandleChartMouseMove(const Point2Di& mousePos) override;
        bool OnEvent(const UCEvent& event) override;

    private:
        // ===== DEFAULTS =====
        void ApplyQuadrantTypeDefaults();
        void ApplyStyleDefaults();

        // ===== LAYOUT & COORDINATES =====
        void UpdateQuadrantLayout();
        void InvalidateQuadrantLayout() { layout.valid = false; }
        Point2Dd DataToScreen(float x, float y) const;
        int FindDataPointAt(const Point2Di& screenPos) const;

        // ===== EVENT HELPERS =====
        bool HandlePointClick(const UCEvent& event);
        bool HandlePointDoubleClick(const UCEvent& event);
        std::string BuildPointTooltip(size_t index) const;

        // ===== DRAWING =====
        void DrawQuadrantBackgrounds(IRenderContext* ctx);
        void DrawQuadrantGrid(IRenderContext* ctx);
        void DrawCenterLinesAndBorder(IRenderContext* ctx);
        void DrawQuadrantAxisLabels(IRenderContext* ctx);
        void DrawQuadrantLabels(IRenderContext* ctx);
        void DrawDataPoints(IRenderContext* ctx);
        void DrawDataPointLabels(IRenderContext* ctx);
        void DrawSelectionRings(IRenderContext* ctx);
        void DrawPointShape(IRenderContext* ctx, const QuadrantDataPoint& point,
                            const Point2Dd& pos, float radius);
        void DrawMultilineTextCentered(IRenderContext* ctx, const std::string& text,
                                       const Point2Dd& center, float lineHeight);
    };

// =============================================================================
// SAMPLE DATA GENERATORS
// =============================================================================

    namespace QuadrantChartSamples {
        std::vector<QuadrantDataPoint> SWOTData();
        std::vector<QuadrantDataPoint> BCGData();
        std::vector<QuadrantDataPoint> EisenhowerData();
        std::vector<QuadrantDataPoint> RiskData();
        std::vector<QuadrantDataPoint> PriorityData();
    }

// =============================================================================
// FACTORY FUNCTIONS - FOLLOW EXISTING ULTRACANVAS PATTERNS
// =============================================================================

    std::shared_ptr<UltraCanvasQuadrantChart> CreateQuadrantChartElement(
            const std::string& id, int x, int y, int width, int height);

    std::shared_ptr<UltraCanvasQuadrantChart> CreateSWOTAnalysisChart(
            const std::string& id, int x, int y, int width, int height);

    std::shared_ptr<UltraCanvasQuadrantChart> CreateBCGMatrixChart(
            const std::string& id, int x, int y, int width, int height);

    std::shared_ptr<UltraCanvasQuadrantChart> CreateEisenhowerMatrixChart(
            const std::string& id, int x, int y, int width, int height);

    std::shared_ptr<UltraCanvasQuadrantChart> CreateRiskMatrixChart(
            const std::string& id, int x, int y, int width, int height);

    std::shared_ptr<UltraCanvasQuadrantChart> CreatePriorityMatrixChart(
            const std::string& id, int x, int y, int width, int height);

    std::shared_ptr<UltraCanvasQuadrantChart> CreateCustomQuadrantChart(
            const std::string& id, int x, int y, int width, int height,
            const std::string& title, const QuadrantDefinition& definition);

} // namespace UltraCanvas
