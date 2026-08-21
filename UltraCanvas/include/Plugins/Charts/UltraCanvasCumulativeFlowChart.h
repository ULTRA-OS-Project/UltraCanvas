// include/Plugins/Charts/UltraCanvasCumulativeFlowChart.h
// Cumulative flow diagram (CFD) element: stacked per-stage bands over time —
// the standard Kanban analytics chart. Binds directly to a KanbanDataSource
// (deriving daily stage counts from its move history) or takes manual series,
// and draws the classic Lead Time / Cycle Time / WIP span annotations.
// Version: 1.1.0
// Last Modified: 2026-08-20
// V1.1.0: legend: migrated to the shared ChartLegend component
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasChartElementBase.h"
#include "UltraCanvasKanbanBoard.h"
#include "Plugins/Charts/UltraCanvasChartLegend.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// =============================================================================
// DATA MODEL
// =============================================================================

    // One workflow stage band. `values[i]` is the item count in this stage at
    // period i. Stage order follows the workflow: stages[0] is the first
    // stage (To Do — drawn as the TOP band), stages.back() is the final stage
    // (Done — drawn as the BOTTOM band), per CFD convention.
    struct CFDStage {
        std::string name;
        std::vector<double> values;
        Color color = Colors::Transparent;   // Transparent = palette color
    };

    // A measurement annotation drawn onto the bands. Band *boundaries* are
    // indexed by stage: boundary k is the top edge of stage k (the cumulative
    // count of stages k..last). Boundary 0 is the overall top (arrival)
    // curve; boundary stages-1 is the top of Done (departure curve).
    struct CFDAnnotation {
        enum class Kind {
            HorizontalSpan,   // Time between two boundaries at an item count
            VerticalSpan      // Item count between two boundaries at a time
        };
        Kind kind = Kind::HorizontalSpan;
        std::string label;
        int fromBoundary = 0;     // Left/upper boundary index
        int toBoundary = -1;      // -1 = last boundary (top of Done)
        double value = -1;        // Horizontal: item count; -1 = auto (half of
                                  // the final total)
        double position = -1;     // Vertical: period position (0-based, may be
                                  // fractional); -1 = auto (one third of range)
    };

// =============================================================================
// STYLE SYSTEM
// =============================================================================

    // Kept for API compatibility; mapped internally onto the shared
    // ChartLegendPosition (Right -> RightCenter, Hidden -> legend hidden).
    enum class CFDLegendPosition {
        Hidden,
        Right             // Boxed side panel with a dot per stage
    };

    struct CumulativeFlowChartStyle {
        // ----- Layout -----
        float paddingLeft = 46.0f;      // Room for y tick labels
        float paddingRight = 14.0f;
        float paddingTop = 16.0f;
        float paddingBottom = 34.0f;    // Room for x labels
        float legendWidth = 150.0f;     // Right legend panel width

        // ----- Bands -----
        float bandAlpha = 1.0f;         // Fill alpha multiplier (0..1)
        bool showEdgeLines = true;      // Stroke each boundary curve
        float edgeLineWidth = 1.6f;
        float edgeDarken = 0.22f;       // Edge color = band color darkened
        bool showMarkers = true;        // Dot per period on each boundary
        float markerRadius = 3.4f;
        Color markerFill = Colors::White;

        // ----- Axes & grid -----
        Color background = Colors::White;
        Color plotBackground = Colors::Transparent;
        Color axisColor = Color(150, 152, 158);
        Color gridColor = Color(232, 233, 236);
        bool showHorizontalGrid = true;
        Color tickTextColor = Color(95, 99, 108);
        float tickFontSize = 10.5f;
        float xLabelFontSize = 10.5f;
        bool boldXLabels = true;
        double yTickStep = 0;           // 0 = automatic "nice" step

        // ----- Legend -----
        // These fields are mapped onto the shared ChartLegendStyle whenever a
        // style is applied (SetStyle/StyleChanged); the legend itself is a
        // shared ChartLegend that measures its own width, so legendWidth is
        // only a historical upper bound and no longer enforced.
        CFDLegendPosition legendPosition = CFDLegendPosition::Right;
        Color legendPanelColor = Color(246, 246, 248);
        Color legendBorderColor = Color(225, 226, 230);
        Color legendTextColor = Color(60, 63, 70);
        float legendFontSize = 11.5f;
        float legendDotRadius = 5.5f;

        // ----- Annotations -----
        Color annotationColor = Color(120, 122, 128);
        Color annotationBoxColor = Color(235, 235, 238, 235);
        Color annotationTextColor = Color(70, 72, 78);
        float annotationFontSize = 11.0f;
        float annotationLineWidth = 2.4f;
        float annotationArrowSize = 7.0f;

        // ----- Fonts / palette -----
        std::string fontFamily;         // Empty = system default
        std::vector<Color> palette;     // Stage color cycle; empty = default
    };

    namespace CumulativeFlowStyles {
        CumulativeFlowChartStyle CreateModern();  // Light, vibrant bands
        CumulativeFlowChartStyle CreateOlive();   // Olive/amber report look
                                                  // (reference image style)
        CumulativeFlowChartStyle CreateDark();    // Dark background variant
    }

// =============================================================================
// CUMULATIVE FLOW CHART ELEMENT
// =============================================================================

    class UltraCanvasCumulativeFlowChartElement : public UltraCanvasChartElementBase {
    public:
        UltraCanvasCumulativeFlowChartElement(const std::string& id, int x, int y,
                                              int width, int height);

        // ===== MANUAL SERIES INPUT =====
        // Stage order: first = first workflow stage (top band), last = Done
        // (bottom band). All value vectors should share one length; shorter
        // ones read as 0.
        void SetStages(const std::vector<CFDStage>& stages);
        void AddStage(const std::string& name, const std::vector<double>& values,
                      const Color& color = Colors::Transparent);
        void ClearStages();
        const std::vector<CFDStage>& GetStages() const { return stages; }

        // Captions under the x axis ("Month 1", "W27", dates...). Missing
        // entries render as 1-based period numbers.
        void SetPeriodLabels(const std::vector<std::string>& labels);

        // ===== KANBAN BINDING =====
        // Derives one stage per board column (board order; first column =
        // top band) with one period every `stepDays` from `from` to `to`
        // inclusive, replaying the board's move history. Period labels become
        // ISO dates when stepDays >= 7, else day numbers. The data is copied:
        // call again to refresh after further board changes.
        void LoadFromKanban(const std::shared_ptr<KanbanDataSource>& board,
                            const GanttDate& from, const GanttDate& to,
                            int stepDays = 1);

        // ===== ANNOTATIONS =====
        // The classic three, auto-placed (F5/F6); span boundaries follow the
        // CFD reading: lead time = arrival (top) to Done, cycle time = start
        // of active work (boundary 1) to Done, WIP = thickness between
        // boundary 1 and Done top.
        void AddLeadTimeAnnotation(const std::string& label = "Lead Time");
        void AddCycleTimeAnnotation(const std::string& label = "Cycle Time");
        void AddWipAnnotation(const std::string& label = "WIP Limit");
        void AddAnnotation(const CFDAnnotation& annotation);
        void ClearAnnotations();

        // ===== STYLE =====
        void SetStyle(const CumulativeFlowChartStyle& newStyle);
        const CumulativeFlowChartStyle& GetStyle() const { return style; }
        CumulativeFlowChartStyle& EditStyle() { return style; }
        void StyleChanged();

        // ===== CALLBACKS =====
        // Hover moved to another period (-1 = left the plot).
        std::function<void(int periodIndex)> onHoveredPeriodChanged;

        // ===== FRAMEWORK OVERRIDES =====
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        void RenderChart(IRenderContext* ctx) override;
        bool HandleChartMouseMove(const Point2Di& mousePos) override;
        bool OnEvent(const UCEvent& event) override;

    private:
        std::vector<CFDStage> stages;
        std::vector<std::string> periodLabels;
        std::vector<CFDAnnotation> annotations;
        CumulativeFlowChartStyle style;
        int hoveredPeriod = -1;

        // Shared legend component (stage name + stage color per entry).
        ChartLegend legend;

        // Resolved per-frame plot geometry.
        Rect2Dd plotRect;
        Rect2Dd chartArea;      // Padded content area the legend carves from
        size_t periodCount = 0;
        double yMax = 0;

        size_t PeriodCount() const;
        double StageValue(size_t stageIndex, size_t period) const;
        // Cumulative count of stages boundaryIndex..last at `period`.
        double BoundaryValue(size_t boundaryIndex, size_t period) const;
        Color StageColor(size_t stageIndex) const;
        double XForPeriod(double period) const;
        double YForValue(double value) const;
        // First x (period units) where the boundary curve crosses `value`;
        // clamps to the range ends when it never does.
        double CrossingPeriod(size_t boundaryIndex, double value) const;
        int ResolveBoundary(int boundary) const;

        void ApplyLegendStyle();
        void ComputePlot(IRenderContext* ctx);
        void RenderGridAndAxes(IRenderContext* ctx);
        void RenderBands(IRenderContext* ctx);
        void RenderMarkers(IRenderContext* ctx);
        void RenderAnnotations(IRenderContext* ctx);
        void DrawArrowHead(IRenderContext* ctx, const Point2Dd& tip,
                           double angle, const Color& color);
        std::string PeriodLabel(size_t period) const;
        std::string BuildPeriodTooltip(size_t period) const;
    };

// =============================================================================
// FACTORY FUNCTIONS
// =============================================================================

    inline std::shared_ptr<UltraCanvasCumulativeFlowChartElement>
    CreateCumulativeFlowChartElement(const std::string& id, int x, int y,
                                     int width, int height) {
        return std::make_shared<UltraCanvasCumulativeFlowChartElement>(
                id, x, y, width, height);
    }

    // Bound to a Kanban board's history with the three classic annotations.
    inline std::shared_ptr<UltraCanvasCumulativeFlowChartElement>
    CreateCumulativeFlowChartFromKanban(
            const std::string& id, int x, int y, int width, int height,
            const std::shared_ptr<KanbanDataSource>& board,
            const GanttDate& from, const GanttDate& to, int stepDays = 1,
            const std::string& title = "") {
        auto chart = CreateCumulativeFlowChartElement(id, x, y, width, height);
        chart->LoadFromKanban(board, from, to, stepDays);
        chart->AddLeadTimeAnnotation();
        chart->AddCycleTimeAnnotation();
        chart->AddWipAnnotation();
        if (!title.empty()) chart->SetChartTitle(title);
        return chart;
    }

} // namespace UltraCanvas
