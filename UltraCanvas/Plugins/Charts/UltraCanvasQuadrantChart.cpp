// Plugins/Charts/UltraCanvasQuadrantChart.cpp
// Quadrant chart element for strategic analysis (SWOT, BCG, Eisenhower, risk & priority matrices)
// Version: 1.1.0
// Last Modified: 2026-07-29
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasQuadrantChart.h"
#include "UltraCanvasLabelPlacement.h"
#include "UltraCanvasTooltipManager.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace UltraCanvas {

// =============================================================================
// PREDEFINED QUADRANT DEFINITIONS
// =============================================================================

    QuadrantDefinition QuadrantDefinition::SWOT() {
        QuadrantDefinition def;
        def.topLeftLabel = "Strengths";
        def.topRightLabel = "Opportunities";
        def.bottomLeftLabel = "Weaknesses";
        def.bottomRightLabel = "Threats";
        def.topLeftColor = Color(144, 238, 144, 100);     // Light green
        def.topRightColor = Color(255, 215, 0, 100);      // Gold
        def.bottomLeftColor = Color(255, 182, 193, 100);  // Light pink
        def.bottomRightColor = Color(255, 160, 122, 100); // Light salmon
        return def;
    }

    QuadrantDefinition QuadrantDefinition::BCG() {
        QuadrantDefinition def;
        def.topLeftLabel = "Question Marks";
        def.topRightLabel = "Stars";
        def.bottomLeftLabel = "Dogs";
        def.bottomRightLabel = "Cash Cows";
        def.topLeftColor = Color(255, 255, 150, 100);
        def.topRightColor = Color(150, 255, 150, 100);
        def.bottomLeftColor = Color(255, 150, 150, 100);
        def.bottomRightColor = Color(150, 200, 255, 100);
        return def;
    }

    QuadrantDefinition QuadrantDefinition::Ansoff() {
        QuadrantDefinition def;
        def.topLeftLabel = "Market\nDevelopment";
        def.topRightLabel = "Diversification";
        def.bottomLeftLabel = "Market\nPenetration";
        def.bottomRightLabel = "Product\nDevelopment";
        def.topLeftColor = Color(173, 216, 230, 100);
        def.topRightColor = Color(221, 160, 221, 100);
        def.bottomLeftColor = Color(144, 238, 144, 100);
        def.bottomRightColor = Color(255, 215, 0, 100);
        return def;
    }

    QuadrantDefinition QuadrantDefinition::Eisenhower() {
        QuadrantDefinition def;
        def.topLeftLabel = "Schedule";
        def.topRightLabel = "Do First";
        def.bottomLeftLabel = "Eliminate";
        def.bottomRightLabel = "Delegate";
        def.topLeftColor = Color(255, 215, 0, 100);       // Gold
        def.topRightColor = Color(255, 120, 120, 100);    // Red
        def.bottomLeftColor = Color(192, 192, 192, 100);  // Silver
        def.bottomRightColor = Color(135, 206, 250, 100); // Sky blue
        return def;
    }

    QuadrantDefinition QuadrantDefinition::MagicQuadrant() {
        QuadrantDefinition def;
        def.topLeftLabel = "Challengers";
        def.topRightLabel = "Leaders";
        def.bottomLeftLabel = "Niche Players";
        def.bottomRightLabel = "Visionaries";
        def.topLeftColor = Color(200, 200, 255, 100);
        def.topRightColor = Color(144, 238, 144, 100);
        def.bottomLeftColor = Color(220, 220, 220, 100);
        def.bottomRightColor = Color(255, 228, 181, 100);
        return def;
    }

    QuadrantDefinition QuadrantDefinition::RiskMatrix() {
        QuadrantDefinition def;
        def.topLeftLabel = "Low Probability\nHigh Impact";
        def.topRightLabel = "High Probability\nHigh Impact";
        def.bottomLeftLabel = "Low Probability\nLow Impact";
        def.bottomRightLabel = "High Probability\nLow Impact";
        def.topLeftColor = Color(255, 165, 0, 100);       // Orange
        def.topRightColor = Color(255, 100, 100, 100);    // Red
        def.bottomLeftColor = Color(150, 255, 150, 100);  // Green
        def.bottomRightColor = Color(255, 255, 150, 100); // Yellow
        return def;
    }

    QuadrantDefinition QuadrantDefinition::Priority() {
        QuadrantDefinition def;
        def.topLeftLabel = "Quick Wins";
        def.topRightLabel = "Major Projects";
        def.bottomLeftLabel = "Fill-ins";
        def.bottomRightLabel = "Thankless Tasks";
        def.topLeftColor = Color(144, 238, 144, 100);
        def.topRightColor = Color(255, 215, 0, 100);
        def.bottomLeftColor = Color(220, 220, 220, 100);
        def.bottomRightColor = Color(255, 182, 193, 100);
        return def;
    }

// =============================================================================
// CONSTRUCTION & DEFAULTS
// =============================================================================

    UltraCanvasQuadrantChart::UltraCanvasQuadrantChart(const std::string& id, int x, int y, int w, int h)
            : UltraCanvasChartElementBase(id, x, y, w, h) {
        enableSelection = true;
        enableTooltips = true;
        showGrid = false;
        backgroundColor = Color(255, 255, 255, 255);
        ApplyQuadrantTypeDefaults();
        ApplyStyleDefaults();
    }

    void UltraCanvasQuadrantChart::SetQuadrantType(QuadrantType type) {
        if (chartType == type) return;
        chartType = type;
        ApplyQuadrantTypeDefaults();
        InvalidateQuadrantLayout();
        RequestRedraw();
    }

    void UltraCanvasQuadrantChart::SetQuadrantDefinition(const QuadrantDefinition& def) {
        quadrantDef = def;
        RequestRedraw();
    }

    void UltraCanvasQuadrantChart::SetAxisConfiguration(const QuadrantAxisConfiguration& config) {
        axisConfig = config;
        InvalidateQuadrantLayout();
        RequestRedraw();
    }

    void UltraCanvasQuadrantChart::SetQuadrantStyle(QuadrantStyle style) {
        if (visualStyle == style) return;
        visualStyle = style;
        ApplyStyleDefaults();
        RequestRedraw();
    }

    void UltraCanvasQuadrantChart::ApplyQuadrantTypeDefaults() {
        switch (chartType) {
            case QuadrantType::SWOT:
                quadrantDef = QuadrantDefinition::SWOT();
                axisConfig.xAxisLabel = "Internal vs External";
                axisConfig.yAxisLabel = "Positive vs Negative";
                axisConfig.xAxisLeftLabel = "Internal";
                axisConfig.xAxisRightLabel = "External";
                axisConfig.yAxisBottomLabel = "Negative";
                axisConfig.yAxisTopLabel = "Positive";
                break;

            case QuadrantType::BCG:
                quadrantDef = QuadrantDefinition::BCG();
                axisConfig.xAxisLabel = "Relative Market Share";
                axisConfig.yAxisLabel = "Market Growth Rate";
                axisConfig.xAxisLeftLabel = "Low";
                axisConfig.xAxisRightLabel = "High";
                axisConfig.yAxisBottomLabel = "Low";
                axisConfig.yAxisTopLabel = "High";
                break;

            case QuadrantType::Ansoff:
                quadrantDef = QuadrantDefinition::Ansoff();
                axisConfig.xAxisLabel = "Products";
                axisConfig.yAxisLabel = "Markets";
                axisConfig.xAxisLeftLabel = "Existing";
                axisConfig.xAxisRightLabel = "New";
                axisConfig.yAxisBottomLabel = "Existing";
                axisConfig.yAxisTopLabel = "New";
                break;

            case QuadrantType::Eisenhower:
                quadrantDef = QuadrantDefinition::Eisenhower();
                axisConfig.xAxisLabel = "Urgency";
                axisConfig.yAxisLabel = "Importance";
                axisConfig.xAxisLeftLabel = "Not Urgent";
                axisConfig.xAxisRightLabel = "Urgent";
                axisConfig.yAxisBottomLabel = "Not Important";
                axisConfig.yAxisTopLabel = "Important";
                break;

            case QuadrantType::MagicQuadrant:
                quadrantDef = QuadrantDefinition::MagicQuadrant();
                axisConfig.xAxisLabel = "Completeness of Vision";
                axisConfig.yAxisLabel = "Ability to Execute";
                axisConfig.xAxisLeftLabel = "Narrow";
                axisConfig.xAxisRightLabel = "Complete";
                axisConfig.yAxisBottomLabel = "Low";
                axisConfig.yAxisTopLabel = "High";
                break;

            case QuadrantType::RiskMatrix:
                quadrantDef = QuadrantDefinition::RiskMatrix();
                axisConfig.xAxisLabel = "Probability of Occurrence";
                axisConfig.yAxisLabel = "Impact Severity";
                axisConfig.xAxisLeftLabel = "Low Probability";
                axisConfig.xAxisRightLabel = "High Probability";
                axisConfig.yAxisBottomLabel = "Low Impact";
                axisConfig.yAxisTopLabel = "High Impact";
                break;

            case QuadrantType::Priority:
                quadrantDef = QuadrantDefinition::Priority();
                axisConfig.xAxisLabel = "Effort Required";
                axisConfig.yAxisLabel = "Impact / Value";
                axisConfig.xAxisLeftLabel = "Low Effort";
                axisConfig.xAxisRightLabel = "High Effort";
                axisConfig.yAxisBottomLabel = "Low Impact";
                axisConfig.yAxisTopLabel = "High Impact";
                break;

            case QuadrantType::Custom:
            default:
                // Keep whatever the caller configured
                break;
        }
    }

    void UltraCanvasQuadrantChart::ApplyStyleDefaults() {
        switch (visualStyle) {
            case QuadrantStyle::Clean:
                backgroundColor = Color(255, 255, 255, 255);
                plotBorderColor = Color(210, 210, 210, 255);
                axisConfig.axisColor = Color(150, 150, 150, 255);
                showGrid = false;
                break;

            case QuadrantStyle::Business:
                backgroundColor = Color(248, 248, 248, 255);
                plotBorderColor = Color(100, 100, 100, 255);
                axisConfig.axisColor = Color(80, 80, 80, 255);
                showGrid = false;
                break;

            case QuadrantStyle::Scientific:
                backgroundColor = Color(255, 255, 255, 255);
                plotBorderColor = Color(0, 0, 0, 255);
                axisConfig.axisColor = Color(0, 0, 0, 255);
                showGrid = true;
                break;

            case QuadrantStyle::Modern:
                backgroundColor = Color(250, 250, 250, 255);
                plotBorderColor = Color(160, 160, 160, 255);
                axisConfig.axisColor = Color(100, 100, 100, 255);
                showGrid = false;
                break;
        }
    }

// =============================================================================
// DATA MANAGEMENT
// =============================================================================

    void UltraCanvasQuadrantChart::AddDataPoint(const QuadrantDataPoint& point) {
        dataPoints.push_back(point);
        RequestRedraw();
    }

    void UltraCanvasQuadrantChart::AddDataPoint(const std::string& label, float x, float y, const Color& color) {
        dataPoints.emplace_back(label, x, y, color);
        RequestRedraw();
    }

    void UltraCanvasQuadrantChart::SetDataPoints(const std::vector<QuadrantDataPoint>& points) {
        dataPoints = points;
        selectedPoints.clear();
        hoveredPoint = -1;
        RequestRedraw();
    }

    void UltraCanvasQuadrantChart::ClearDataPoints() {
        dataPoints.clear();
        selectedPoints.clear();
        hoveredPoint = -1;
        RequestRedraw();
    }

    void UltraCanvasQuadrantChart::RemoveDataPoint(size_t index) {
        if (index >= dataPoints.size()) return;

        dataPoints.erase(dataPoints.begin() + index);

        // Drop the removed index from the selection and shift the ones behind it
        std::vector<size_t> updated;
        updated.reserve(selectedPoints.size());
        for (size_t sel : selectedPoints) {
            if (sel < index) updated.push_back(sel);
            else if (sel > index) updated.push_back(sel - 1);
        }
        selectedPoints = std::move(updated);

        hoveredPoint = -1;
        RequestRedraw();
    }

    void UltraCanvasQuadrantChart::LoadSampleData() {
        switch (chartType) {
            case QuadrantType::SWOT:
                SetDataPoints(QuadrantChartSamples::SWOTData());
                break;
            case QuadrantType::BCG:
                SetDataPoints(QuadrantChartSamples::BCGData());
                break;
            case QuadrantType::Eisenhower:
                SetDataPoints(QuadrantChartSamples::EisenhowerData());
                break;
            case QuadrantType::RiskMatrix:
                SetDataPoints(QuadrantChartSamples::RiskData());
                break;
            case QuadrantType::Priority:
                SetDataPoints(QuadrantChartSamples::PriorityData());
                break;
            default:
                // No built-in sample data for the other types
                break;
        }
    }

// =============================================================================
// VISUAL TOGGLES
// =============================================================================

    void UltraCanvasQuadrantChart::SetShowQuadrantLabels(bool show) {
        if (showQuadrantLabels == show) return;
        showQuadrantLabels = show;
        RequestRedraw();
    }

    void UltraCanvasQuadrantChart::SetShowQuadrantColors(bool show) {
        if (showQuadrantColors == show) return;
        showQuadrantColors = show;
        RequestRedraw();
    }

    void UltraCanvasQuadrantChart::SetShowDataPointLabels(bool show) {
        if (showDataPointLabels == show) return;
        showDataPointLabels = show;
        RequestRedraw();
    }

// =============================================================================
// SELECTION MANAGEMENT
// =============================================================================

    void UltraCanvasQuadrantChart::SelectDataPoint(size_t index) {
        if (index >= dataPoints.size() || !enableSelection) return;

        if (!enableMultiSelection) selectedPoints.clear();

        if (std::find(selectedPoints.begin(), selectedPoints.end(), index) == selectedPoints.end()) {
            selectedPoints.push_back(index);
            if (onPointSelect) onPointSelect(index, dataPoints[index]);
        }
        RequestRedraw();
    }

    void UltraCanvasQuadrantChart::DeselectDataPoint(size_t index) {
        auto it = std::find(selectedPoints.begin(), selectedPoints.end(), index);
        if (it == selectedPoints.end()) return;

        selectedPoints.erase(it);
        if (index < dataPoints.size() && onPointDeselect) {
            onPointDeselect(index, dataPoints[index]);
        }
        RequestRedraw();
    }

    void UltraCanvasQuadrantChart::ClearSelection() {
        if (selectedPoints.empty()) return;
        selectedPoints.clear();
        RequestRedraw();
    }

    bool UltraCanvasQuadrantChart::IsPointSelected(size_t index) const {
        return std::find(selectedPoints.begin(), selectedPoints.end(), index) != selectedPoints.end();
    }

// =============================================================================
// QUADRANT UTILITIES
// =============================================================================

    QuadrantPosition UltraCanvasQuadrantChart::GetPointQuadrant(const QuadrantDataPoint& point) const {
        bool rightSide = point.xValue >= axisConfig.xCenter;
        bool topSide = point.yValue >= axisConfig.yCenter;

        if (topSide && rightSide) return QuadrantPosition::TopRight;
        if (topSide) return QuadrantPosition::TopLeft;
        if (rightSide) return QuadrantPosition::BottomRight;
        return QuadrantPosition::BottomLeft;
    }

    std::vector<size_t> UltraCanvasQuadrantChart::GetPointsInQuadrant(QuadrantPosition quadrant) const {
        std::vector<size_t> result;
        for (size_t i = 0; i < dataPoints.size(); ++i) {
            if (GetPointQuadrant(dataPoints[i]) == quadrant) {
                result.push_back(i);
            }
        }
        return result;
    }

    size_t UltraCanvasQuadrantChart::CountPointsInQuadrant(QuadrantPosition quadrant) const {
        return GetPointsInQuadrant(quadrant).size();
    }

    const std::string& UltraCanvasQuadrantChart::GetQuadrantLabel(QuadrantPosition quadrant) const {
        switch (quadrant) {
            case QuadrantPosition::TopLeft: return quadrantDef.topLeftLabel;
            case QuadrantPosition::TopRight: return quadrantDef.topRightLabel;
            case QuadrantPosition::BottomLeft: return quadrantDef.bottomLeftLabel;
            case QuadrantPosition::BottomRight:
            default: return quadrantDef.bottomRightLabel;
        }
    }

// =============================================================================
// LAYOUT & COORDINATE CONVERSION (local coordinates)
// =============================================================================

    void UltraCanvasQuadrantChart::UpdateQuadrantLayout() {
        double leftMargin = (axisConfig.showAxisLabels || axisConfig.showAxisEndLabels) ? 74.0 : 24.0;
        double rightMargin = 24.0;
        double topMargin = chartTitle.empty() ? 20.0 : 44.0;
        double bottomMargin = (axisConfig.showAxisLabels || axisConfig.showAxisEndLabels) ? 58.0 : 24.0;

        layout.plotArea = Rect2Dd(
                leftMargin,
                topMargin,
                std::max(10.0, GetWidth() - leftMargin - rightMargin),
                std::max(10.0, GetHeight() - topMargin - bottomMargin));

        double xRange = std::max(1e-6f, axisConfig.xMax - axisConfig.xMin);
        double yRange = std::max(1e-6f, axisConfig.yMax - axisConfig.yMin);

        layout.xScale = layout.plotArea.width / xRange;
        layout.yScale = layout.plotArea.height / yRange;

        layout.centerPoint = Point2Dd(
                layout.plotArea.x + (axisConfig.xCenter - axisConfig.xMin) * layout.xScale,
                layout.plotArea.y + layout.plotArea.height - (axisConfig.yCenter - axisConfig.yMin) * layout.yScale);

        double left = layout.plotArea.x;
        double top = layout.plotArea.y;
        double right = layout.plotArea.x + layout.plotArea.width;
        double bottom = layout.plotArea.y + layout.plotArea.height;
        double cx = layout.centerPoint.x;
        double cy = layout.centerPoint.y;

        layout.topLeft = Rect2Dd(left, top, cx - left, cy - top);
        layout.topRight = Rect2Dd(cx, top, right - cx, cy - top);
        layout.bottomLeft = Rect2Dd(left, cy, cx - left, bottom - cy);
        layout.bottomRight = Rect2Dd(cx, cy, right - cx, bottom - cy);

        layout.valid = true;
    }

    Point2Dd UltraCanvasQuadrantChart::DataToScreen(float x, float y) const {
        return Point2Dd(
                layout.plotArea.x + (x - axisConfig.xMin) * layout.xScale,
                layout.plotArea.y + layout.plotArea.height - (y - axisConfig.yMin) * layout.yScale);
    }

    int UltraCanvasQuadrantChart::FindDataPointAt(const Point2Di& screenPos) const {
        // Iterate backwards so the point drawn on top wins
        for (int i = static_cast<int>(dataPoints.size()) - 1; i >= 0; --i) {
            const auto& point = dataPoints[i];
            Point2Dd pos = DataToScreen(point.xValue, point.yValue);
            double dx = screenPos.x - pos.x;
            double dy = screenPos.y - pos.y;
            double hitRadius = point.radius + 3.0;  // 3px tolerance
            if (dx * dx + dy * dy <= hitRadius * hitRadius) {
                return i;
            }
        }
        return -1;
    }

// =============================================================================
// RENDERING
// =============================================================================

    void UltraCanvasQuadrantChart::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        if (!ctx) return;

        UpdateQuadrantLayout();

        // Full element background
        ctx->DrawFilledRectangle(GetLocalBounds(), backgroundColor);

        // Title
        if (!chartTitle.empty()) {
            ctx->SetTextPaint(Color(40, 40, 40, 255));
            ctx->SetFontSize(titleFontSize);
            Size2Di txtSize = ctx->GetTextLineDimensions(chartTitle);
            ctx->DrawText(chartTitle, Point2Dd(
                    layout.plotArea.x + layout.plotArea.width / 2.0 - txtSize.width / 2.0,
                    (44.0 - txtSize.height) / 2.0));
        }

        RenderChart(ctx);
    }

    void UltraCanvasQuadrantChart::RenderChart(IRenderContext* ctx) {
        if (!ctx) return;
        if (!layout.valid) UpdateQuadrantLayout();

        if (showQuadrantColors) DrawQuadrantBackgrounds(ctx);
        if (showGrid) DrawQuadrantGrid(ctx);
        DrawCenterLinesAndBorder(ctx);
        DrawQuadrantAxisLabels(ctx);
        quadrantLabelRects.clear();
        if (showQuadrantLabels) DrawQuadrantLabels(ctx);
        DrawDataPoints(ctx);
        if (showDataPointLabels) DrawDataPointLabels(ctx);
        DrawSelectionRings(ctx);
    }

    void UltraCanvasQuadrantChart::DrawQuadrantBackgrounds(IRenderContext* ctx) {
        ctx->SetFillPaint(quadrantDef.topLeftColor);
        ctx->FillRectangle(layout.topLeft);

        ctx->SetFillPaint(quadrantDef.topRightColor);
        ctx->FillRectangle(layout.topRight);

        ctx->SetFillPaint(quadrantDef.bottomLeftColor);
        ctx->FillRectangle(layout.bottomLeft);

        ctx->SetFillPaint(quadrantDef.bottomRightColor);
        ctx->FillRectangle(layout.bottomRight);
    }

    void UltraCanvasQuadrantChart::DrawQuadrantGrid(IRenderContext* ctx) {
        ctx->SetStrokePaint(gridColor);
        ctx->SetStrokeWidth(1.0f);

        const int divisions = 10;
        for (int i = 1; i < divisions; ++i) {
            double x = layout.plotArea.x + layout.plotArea.width * i / divisions;
            ctx->DrawLine({x, layout.plotArea.y}, {x, layout.plotArea.y + layout.plotArea.height});

            double y = layout.plotArea.y + layout.plotArea.height * i / divisions;
            ctx->DrawLine({layout.plotArea.x, y}, {layout.plotArea.x + layout.plotArea.width, y});
        }
    }

    void UltraCanvasQuadrantChart::DrawCenterLinesAndBorder(IRenderContext* ctx) {
        if (axisConfig.showCenterLines) {
            ctx->SetStrokePaint(axisConfig.centerLineColor);
            ctx->SetStrokeWidth(axisConfig.centerLineWidth);

            ctx->DrawLine({layout.plotArea.x, layout.centerPoint.y},
                          {layout.plotArea.x + layout.plotArea.width, layout.centerPoint.y});
            ctx->DrawLine({layout.centerPoint.x, layout.plotArea.y},
                          {layout.centerPoint.x, layout.plotArea.y + layout.plotArea.height});
        }

        ctx->SetStrokePaint(plotBorderColor);
        ctx->SetStrokeWidth(plotBorderWidth);
        ctx->DrawRectangle(layout.plotArea);
    }

    void UltraCanvasQuadrantChart::DrawQuadrantAxisLabels(IRenderContext* ctx) {
        ctx->SetFontSize(axisLabelFontSize);
        ctx->SetTextPaint(Color(80, 80, 80, 255));

        double plotBottom = layout.plotArea.y + layout.plotArea.height;

        if (axisConfig.showAxisEndLabels) {
            // X-axis end labels, centered under each half of the plot
            if (!axisConfig.xAxisLeftLabel.empty()) {
                Size2Di s = ctx->GetTextLineDimensions(axisConfig.xAxisLeftLabel);
                double cx = (layout.plotArea.x + layout.centerPoint.x) / 2.0;
                ctx->DrawText(axisConfig.xAxisLeftLabel, Point2Dd(cx - s.width / 2.0, plotBottom + 6));
            }
            if (!axisConfig.xAxisRightLabel.empty()) {
                Size2Di s = ctx->GetTextLineDimensions(axisConfig.xAxisRightLabel);
                double cx = (layout.centerPoint.x + layout.plotArea.x + layout.plotArea.width) / 2.0;
                ctx->DrawText(axisConfig.xAxisRightLabel, Point2Dd(cx - s.width / 2.0, plotBottom + 6));
            }

            // Y-axis end labels, rotated alongside each half of the Y axis
            if (!axisConfig.yAxisTopLabel.empty()) {
                Size2Di s = ctx->GetTextLineDimensions(axisConfig.yAxisTopLabel);
                double cy = (layout.plotArea.y + layout.centerPoint.y) / 2.0;
                ctx->PushState();
                ctx->Translate(layout.plotArea.x - 8, cy);
                ctx->Rotate(-M_PI / 2.0);
                ctx->DrawText(axisConfig.yAxisTopLabel, Point2Dd(-s.width / 2.0, -s.height));
                ctx->PopState();
            }
            if (!axisConfig.yAxisBottomLabel.empty()) {
                Size2Di s = ctx->GetTextLineDimensions(axisConfig.yAxisBottomLabel);
                double cy = (layout.centerPoint.y + plotBottom) / 2.0;
                ctx->PushState();
                ctx->Translate(layout.plotArea.x - 8, cy);
                ctx->Rotate(-M_PI / 2.0);
                ctx->DrawText(axisConfig.yAxisBottomLabel, Point2Dd(-s.width / 2.0, -s.height));
                ctx->PopState();
            }
        }

        if (axisConfig.showAxisLabels) {
            ctx->SetTextPaint(Color(50, 50, 50, 255));

            if (!axisConfig.xAxisLabel.empty()) {
                Size2Di s = ctx->GetTextLineDimensions(axisConfig.xAxisLabel);
                ctx->DrawText(axisConfig.xAxisLabel, Point2Dd(
                        layout.plotArea.x + layout.plotArea.width / 2.0 - s.width / 2.0,
                        plotBottom + 6 + (axisConfig.showAxisEndLabels ? s.height + 8 : 0)));
            }

            if (!axisConfig.yAxisLabel.empty()) {
                Size2Di s = ctx->GetTextLineDimensions(axisConfig.yAxisLabel);
                double labelX = layout.plotArea.x - 8 -
                        (axisConfig.showAxisEndLabels ? s.height + 10 : 0);
                ctx->PushState();
                ctx->Translate(labelX, layout.plotArea.y + layout.plotArea.height / 2.0);
                ctx->Rotate(-M_PI / 2.0);
                ctx->DrawText(axisConfig.yAxisLabel, Point2Dd(-s.width / 2.0, -s.height));
                ctx->PopState();
            }
        }
    }

    Rect2Dd UltraCanvasQuadrantChart::DrawMultilineTextCentered(IRenderContext* ctx, const std::string& text,
                                                                const Point2Dd& center, float lineHeight) {
        std::vector<std::string> lines;
        std::stringstream ss(text);
        std::string line;
        while (std::getline(ss, line, '\n')) lines.push_back(line);
        if (lines.empty()) return Rect2Dd(center.x, center.y, 0.0, 0.0);

        double totalHeight = lines.size() * lineHeight;
        double y = center.y - totalHeight / 2.0;
        double maxWidth = 0.0;
        for (const auto& l : lines) {
            Size2Di s = ctx->GetTextLineDimensions(l);
            ctx->DrawText(l, Point2Dd(center.x - s.width / 2.0, y));
            maxWidth = std::max(maxWidth, static_cast<double>(s.width));
            y += lineHeight;
        }
        return Rect2Dd(center.x - maxWidth / 2.0, center.y - totalHeight / 2.0,
                       maxWidth, totalHeight);
    }

    void UltraCanvasQuadrantChart::DrawQuadrantLabels(IRenderContext* ctx) {
        ctx->SetFontSize(quadrantLabelFontSize);
        ctx->SetTextPaint(Color(70, 70, 70, 255));

        float lineHeight = quadrantLabelFontSize * 1.3f;

        // Labels sit near the outer corner of each quadrant so they do not
        // collide with data points clustered around the center. Their rects
        // are kept as obstacles for the data point label placement.
        quadrantLabelRects.push_back(DrawMultilineTextCentered(ctx, quadrantDef.topLeftLabel,
                Point2Dd(layout.topLeft.x + layout.topLeft.width / 2.0,
                         layout.topLeft.y + std::min(24.0, layout.topLeft.height / 2.0)), lineHeight));

        quadrantLabelRects.push_back(DrawMultilineTextCentered(ctx, quadrantDef.topRightLabel,
                Point2Dd(layout.topRight.x + layout.topRight.width / 2.0,
                         layout.topRight.y + std::min(24.0, layout.topRight.height / 2.0)), lineHeight));

        quadrantLabelRects.push_back(DrawMultilineTextCentered(ctx, quadrantDef.bottomLeftLabel,
                Point2Dd(layout.bottomLeft.x + layout.bottomLeft.width / 2.0,
                         layout.bottomLeft.y + layout.bottomLeft.height -
                                 std::min(24.0, layout.bottomLeft.height / 2.0)), lineHeight));

        quadrantLabelRects.push_back(DrawMultilineTextCentered(ctx, quadrantDef.bottomRightLabel,
                Point2Dd(layout.bottomRight.x + layout.bottomRight.width / 2.0,
                         layout.bottomRight.y + layout.bottomRight.height -
                                 std::min(24.0, layout.bottomRight.height / 2.0)), lineHeight));
    }

    void UltraCanvasQuadrantChart::DrawPointShape(IRenderContext* ctx, const QuadrantDataPoint& point,
                                                  const Point2Dd& pos, float radius) {
        ctx->SetFillPaint(point.pointColor);
        ctx->SetStrokePaint(point.strokeColor);
        ctx->SetStrokeWidth(point.strokeWidth);

        switch (point.shape) {
            case QuadrantPointShape::Square: {
                Rect2Dd rect(pos.x - radius, pos.y - radius, radius * 2.0, radius * 2.0);
                ctx->FillRectangle(rect);
                if (point.strokeWidth > 0) ctx->DrawRectangle(rect);
                break;
            }

            case QuadrantPointShape::Triangle: {
                std::vector<Point2Dd> tri = {
                        Point2Dd(pos.x, pos.y - radius),
                        Point2Dd(pos.x + radius, pos.y + radius),
                        Point2Dd(pos.x - radius, pos.y + radius)};
                ctx->FillLinePath(tri);
                if (point.strokeWidth > 0) ctx->DrawLinePath(tri, true);
                break;
            }

            case QuadrantPointShape::Diamond: {
                std::vector<Point2Dd> diamond = {
                        Point2Dd(pos.x, pos.y - radius),
                        Point2Dd(pos.x + radius, pos.y),
                        Point2Dd(pos.x, pos.y + radius),
                        Point2Dd(pos.x - radius, pos.y)};
                ctx->FillLinePath(diamond);
                if (point.strokeWidth > 0) ctx->DrawLinePath(diamond, true);
                break;
            }

            case QuadrantPointShape::Circle:
            default:
                ctx->FillCircle(pos, radius);
                if (point.strokeWidth > 0) ctx->DrawCircle(pos, radius);
                break;
        }
    }

    void UltraCanvasQuadrantChart::DrawDataPoints(IRenderContext* ctx) {
        for (size_t i = 0; i < dataPoints.size(); ++i) {
            const auto& point = dataPoints[i];
            Point2Dd pos = DataToScreen(point.xValue, point.yValue);

            bool isHovered = (hoveredPoint == static_cast<int>(i));
            float radius = point.radius * (isHovered ? 1.2f : 1.0f);

            if (isHovered) {
                ctx->SetStrokePaint(Color(255, 200, 0, 180));
                ctx->SetStrokeWidth(2.0f);
                ctx->DrawCircle(pos, radius + 3.0);
            }

            DrawPointShape(ctx, point, pos, radius);
        }
    }

    void UltraCanvasQuadrantChart::DrawDataPointLabels(IRenderContext* ctx) {
        ctx->SetFontSize(dataPointLabelFontSize);
        ctx->SetTextPaint(Color(40, 40, 40, 255));

        // Collision-free placement via the shared solver: every point is a
        // shape so labels do not cover neighbouring markers, the quadrant
        // titles are keep-out obstacles, and each label prefers its classic
        // spot above the point with the other sides as fallback.
        std::vector<LabelShape> shapes(dataPoints.size());
        for (size_t i = 0; i < dataPoints.size(); ++i) {
            Point2Dd pos = DataToScreen(dataPoints[i].xValue, dataPoints[i].yValue);
            shapes[i].type = LabelShapeType::Circle;
            shapes[i].center = pos;
            shapes[i].radius = dataPoints[i].radius;
            shapes[i].keepLabelInside = false;
        }

        std::vector<ShapeLabel> labels;
        labels.reserve(dataPoints.size());
        for (size_t i = 0; i < dataPoints.size(); ++i) {
            if (dataPoints[i].label.empty()) continue;
            ShapeLabel l;
            l.text = dataPoints[i].label;
            l.shapeIndex = i;
            l.preferredSide = LabelSide::Top;
            Size2Di s = ctx->GetTextLineDimensions(dataPoints[i].label);
            l.textSize = Size2Dd(s.width, s.height);
            labels.push_back(l);
        }
        if (labels.empty()) return;

        LabelPlacementOptions opts;
        opts.bounds = layout.plotArea;
        opts.shapeMargin = 3.0;
        opts.labelMargin = 3.0;
        opts.obstacles = quadrantLabelRects;

        std::vector<PlacedShapeLabel> placed = PlaceShapeLabels(shapes, labels, opts);
        for (size_t i = 0; i < placed.size(); ++i) {
            ctx->DrawText(labels[i].text, placed[i].bounds.TopLeft());
        }
    }

    void UltraCanvasQuadrantChart::DrawSelectionRings(IRenderContext* ctx) {
        if (selectedPoints.empty()) return;

        ctx->SetStrokePaint(Color(255, 140, 0, 255));  // Orange
        ctx->SetStrokeWidth(2.5f);

        for (size_t index : selectedPoints) {
            if (index >= dataPoints.size()) continue;
            const auto& point = dataPoints[index];
            Point2Dd pos = DataToScreen(point.xValue, point.yValue);
            ctx->DrawCircle(pos, point.radius + 4.0);
        }
    }

// =============================================================================
// EVENT HANDLING
// =============================================================================

    std::string UltraCanvasQuadrantChart::BuildPointTooltip(size_t index) const {
        const auto& point = dataPoints[index];

        std::string quadrantLabel = GetQuadrantLabel(GetPointQuadrant(point));
        std::replace(quadrantLabel.begin(), quadrantLabel.end(), '\n', ' ');

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        if (!point.label.empty()) oss << point.label << "\n";
        oss << "X: " << point.xValue << "\n";
        oss << "Y: " << point.yValue << "\n";
        oss << "Quadrant: " << quadrantLabel;
        return oss.str();
    }

    bool UltraCanvasQuadrantChart::HandleChartMouseMove(const Point2Di& mousePos) {
        if (!layout.valid) UpdateQuadrantLayout();

        int found = -1;
        if (layout.plotArea.Contains(Point2Dd(mousePos.x, mousePos.y))) {
            found = FindDataPointAt(mousePos);
        }

        if (found != hoveredPoint) {
            hoveredPoint = found;
            if (hoveredPoint >= 0 && onPointHover) {
                onPointHover(static_cast<size_t>(hoveredPoint), dataPoints[hoveredPoint]);
            }
            RequestRedraw();
        }

        if (enableTooltips) {
            if (hoveredPoint >= 0) {
                Point2Di tipPos(mousePos.x + 15, mousePos.y - 20);
                auto windowPos = MapFromLocal(tipPos, nullptr);
                UltraCanvasTooltipManager::UpdateAndShowTooltip(
                        GetWindow(), BuildPointTooltip(hoveredPoint), windowPos);
            } else {
                UltraCanvasTooltipManager::HideTooltip();
            }
        }

        return hoveredPoint >= 0;
    }

    bool UltraCanvasQuadrantChart::HandlePointClick(const UCEvent& event) {
        if (!enableSelection) return false;
        if (!layout.valid) UpdateQuadrantLayout();

        Point2Di mousePos(event.pointer.x, event.pointer.y);
        if (!layout.plotArea.Contains(Point2Dd(mousePos.x, mousePos.y))) return false;

        int clicked = FindDataPointAt(mousePos);

        if (clicked >= 0) {
            if (IsPointSelected(clicked)) {
                DeselectDataPoint(clicked);
            } else {
                SelectDataPoint(clicked);
            }
            if (onSelectionChange) onSelectionChange();
            return true;
        }

        // Click on empty plot area clears the selection
        if (!selectedPoints.empty()) {
            ClearSelection();
            if (onSelectionChange) onSelectionChange();
            return true;
        }

        return false;
    }

    bool UltraCanvasQuadrantChart::HandlePointDoubleClick(const UCEvent& event) {
        if (!layout.valid) UpdateQuadrantLayout();

        int clicked = FindDataPointAt(Point2Di(event.pointer.x, event.pointer.y));
        if (clicked >= 0 && onPointDoubleClick) {
            onPointDoubleClick(static_cast<size_t>(clicked), dataPoints[clicked]);
            return true;
        }
        return false;
    }

    bool UltraCanvasQuadrantChart::OnEvent(const UCEvent& event) {
        if (IsDisabled() || !IsVisible()) return false;

        switch (event.type) {
            case UCEventType::MouseDown:
                if (event.button == UCMouseButton::Left && HandlePointClick(event)) {
                    return true;
                }
                return UltraCanvasChartElementBase::OnEvent(event);

            case UCEventType::MouseDoubleClick:
                return HandlePointDoubleClick(event);

            case UCEventType::MouseLeave:
                if (hoveredPoint != -1) {
                    hoveredPoint = -1;
                    RequestRedraw();
                }
                UltraCanvasTooltipManager::HideTooltip();
                return false;

            default:
                return UltraCanvasChartElementBase::OnEvent(event);
        }
    }

// =============================================================================
// SAMPLE DATA GENERATORS
// =============================================================================

    namespace QuadrantChartSamples {

        std::vector<QuadrantDataPoint> SWOTData() {
            return {
                QuadrantDataPoint("Strong Brand", 0.2f, 0.8f, Color(60, 179, 113, 255)),
                QuadrantDataPoint("Experienced Team", 0.3f, 0.7f, Color(60, 179, 113, 255)),
                QuadrantDataPoint("Market Growth", 0.8f, 0.9f, Color(218, 165, 32, 255)),
                QuadrantDataPoint("Technology Trends", 0.7f, 0.8f, Color(218, 165, 32, 255)),
                QuadrantDataPoint("High Costs", 0.2f, 0.3f, Color(219, 112, 147, 255)),
                QuadrantDataPoint("Limited Resources", 0.3f, 0.2f, Color(219, 112, 147, 255)),
                QuadrantDataPoint("Economic Uncertainty", 0.8f, 0.2f, Color(205, 92, 92, 255)),
                QuadrantDataPoint("Competition", 0.7f, 0.3f, Color(205, 92, 92, 255))
            };
        }

        std::vector<QuadrantDataPoint> BCGData() {
            std::vector<QuadrantDataPoint> data = {
                QuadrantDataPoint("Product Alpha", 0.8f, 0.9f, Color(46, 160, 67, 220)),
                QuadrantDataPoint("Product Beta", 0.9f, 0.2f, Color(9, 105, 218, 220)),
                QuadrantDataPoint("Product Gamma", 0.2f, 0.8f, Color(212, 167, 44, 220)),
                QuadrantDataPoint("Product Delta", 0.1f, 0.1f, Color(207, 34, 46, 220)),
                QuadrantDataPoint("Product Epsilon", 0.7f, 0.7f, Color(46, 160, 67, 220))
            };
            // Bubble size encodes revenue in a BCG matrix
            data[0].radius = 12.0f;
            data[1].radius = 10.0f;
            data[2].radius = 8.0f;
            data[3].radius = 6.0f;
            data[4].radius = 14.0f;
            return data;
        }

        std::vector<QuadrantDataPoint> EisenhowerData() {
            return {
                QuadrantDataPoint("Crisis Response", 0.9f, 0.9f, Color(207, 34, 46, 255)),
                QuadrantDataPoint("Strategic Planning", 0.2f, 0.8f, Color(212, 167, 44, 255)),
                QuadrantDataPoint("Email Responses", 0.8f, 0.3f, Color(9, 105, 218, 255)),
                QuadrantDataPoint("Social Media", 0.2f, 0.2f, Color(140, 140, 140, 255)),
                QuadrantDataPoint("Client Meeting", 0.7f, 0.8f, Color(207, 34, 46, 255)),
                QuadrantDataPoint("Skill Development", 0.3f, 0.7f, Color(212, 167, 44, 255))
            };
        }

        std::vector<QuadrantDataPoint> RiskData() {
            return {
                QuadrantDataPoint("Market Crash", 0.3f, 0.9f, Color(230, 126, 34, 255)),
                QuadrantDataPoint("Data Breach", 0.7f, 0.8f, Color(207, 34, 46, 255)),
                QuadrantDataPoint("Equipment Failure", 0.2f, 0.3f, Color(46, 160, 67, 255)),
                QuadrantDataPoint("Minor Delays", 0.8f, 0.2f, Color(212, 167, 44, 255)),
                QuadrantDataPoint("Regulatory Change", 0.6f, 0.7f, Color(207, 34, 46, 255))
            };
        }

        std::vector<QuadrantDataPoint> PriorityData() {
            return {
                QuadrantDataPoint("Bug Fixes", 0.2f, 0.8f, Color(46, 160, 67, 255)),
                QuadrantDataPoint("New Platform", 0.9f, 0.9f, Color(212, 167, 44, 255)),
                QuadrantDataPoint("Documentation", 0.3f, 0.3f, Color(140, 140, 140, 255)),
                QuadrantDataPoint("Legacy Migration", 0.8f, 0.2f, Color(219, 112, 147, 255)),
                QuadrantDataPoint("UI Polish", 0.4f, 0.7f, Color(46, 160, 67, 255))
            };
        }

    } // namespace QuadrantChartSamples

// =============================================================================
// FACTORY FUNCTIONS
// =============================================================================

    std::shared_ptr<UltraCanvasQuadrantChart> CreateQuadrantChartElement(
            const std::string& id, int x, int y, int width, int height) {
        return std::make_shared<UltraCanvasQuadrantChart>(id, x, y, width, height);
    }

    std::shared_ptr<UltraCanvasQuadrantChart> CreateSWOTAnalysisChart(
            const std::string& id, int x, int y, int width, int height) {
        auto chart = CreateQuadrantChartElement(id, x, y, width, height);
        chart->SetQuadrantType(QuadrantType::SWOT);
        chart->SetTitle("SWOT Analysis");
        return chart;
    }

    std::shared_ptr<UltraCanvasQuadrantChart> CreateBCGMatrixChart(
            const std::string& id, int x, int y, int width, int height) {
        auto chart = CreateQuadrantChartElement(id, x, y, width, height);
        chart->SetQuadrantType(QuadrantType::BCG);
        chart->SetTitle("BCG Matrix");
        return chart;
    }

    std::shared_ptr<UltraCanvasQuadrantChart> CreateEisenhowerMatrixChart(
            const std::string& id, int x, int y, int width, int height) {
        auto chart = CreateQuadrantChartElement(id, x, y, width, height);
        chart->SetQuadrantType(QuadrantType::Eisenhower);
        chart->SetTitle("Eisenhower Decision Matrix");
        return chart;
    }

    std::shared_ptr<UltraCanvasQuadrantChart> CreateRiskMatrixChart(
            const std::string& id, int x, int y, int width, int height) {
        auto chart = CreateQuadrantChartElement(id, x, y, width, height);
        chart->SetQuadrantType(QuadrantType::RiskMatrix);
        chart->SetTitle("Risk Assessment Matrix");
        return chart;
    }

    std::shared_ptr<UltraCanvasQuadrantChart> CreatePriorityMatrixChart(
            const std::string& id, int x, int y, int width, int height) {
        auto chart = CreateQuadrantChartElement(id, x, y, width, height);
        chart->SetQuadrantType(QuadrantType::Priority);
        chart->SetTitle("Priority Matrix");
        return chart;
    }

    std::shared_ptr<UltraCanvasQuadrantChart> CreateCustomQuadrantChart(
            const std::string& id, int x, int y, int width, int height,
            const std::string& title, const QuadrantDefinition& definition) {
        auto chart = CreateQuadrantChartElement(id, x, y, width, height);
        chart->SetQuadrantType(QuadrantType::Custom);
        chart->SetTitle(title);
        chart->SetQuadrantDefinition(definition);
        return chart;
    }

} // namespace UltraCanvas
