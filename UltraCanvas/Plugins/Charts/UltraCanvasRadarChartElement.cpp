// Plugins/Charts/UltraCanvasRadarChartElement.cpp
// Comprehensive radar chart implementation with multi-axis visualization
// Version: 2.0.0
// Last Modified: 2026-06-19
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasRadarChartElement.h"
#include "UltraCanvasTooltipManager.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <sstream>
#include <iomanip>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace UltraCanvas {

// =============================================================================
// RENDER ENTRY POINTS
// =============================================================================

    void UltraCanvasRadarChartElement::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
        if (!ctx) return;

        // Background
        if (showBackground) {
            ctx->DrawFilledRectangle(GetLocalBounds(), backgroundColor);
        }

        // Nothing to draw yet
        if (axes.empty() || series.empty()) {
            DrawEmptyState(ctx);
            return;
        }

        RenderChart(ctx);

        if (enableSelection) {
            DrawSelectionIndicators(ctx);
        }
    }

    void UltraCanvasRadarChartElement::RenderChart(IRenderContext* ctx) {
        if (!ctx || axes.empty() || series.empty()) {
            return;
        }

        // Update animation progress
        UpdateAnimationProgress();

        // Recalculate layout
        RecalculateLayout();

        ctx->PushState();

        // Draw components in proper order
        if (showGrid) {
            DrawRadarGrid(ctx);
        }

        if (showAxisLabels) {
            DrawAxisLabels(ctx);
        }

        DrawRadarSeries(ctx);

        if (showValueLabels) {
            DrawValueLabels(ctx);
        }

        if (showLegend) {
            DrawRadarLegend(ctx);
        }

        ctx->PopState();
    }

// =============================================================================
// LAYOUT CALCULATION
// =============================================================================

    void UltraCanvasRadarChartElement::RecalculateLayout() {
        if (axes.empty()) return;

        // Work in element-local coordinates (origin at 0,0)
        Rect2Df bounds = GetLocalBounds();

        // Reserve space for labels and legend
        float labelMargin = showAxisLabels ? axisLabelFontSize * 2.0f : 10.0f;
        float legendWidth = showLegend ? 150.0f : 0.0f;

        float availableWidth = bounds.width - labelMargin * 2 - legendWidth;
        float availableHeight = bounds.height - labelMargin * 2;

        // Use the smaller dimension to keep the chart circular
        maxRadius = std::min(availableWidth, availableHeight) * 0.4f;
        if (maxRadius < 0.0f) maxRadius = 0.0f;

        centerPoint.x = bounds.x + labelMargin + maxRadius;
        centerPoint.y = bounds.y + labelMargin + maxRadius;

        // Auto-position legend if no explicit position was given
        if (showLegend && legendPosition.x == 0 && legendPosition.y == 0) {
            legendPosition.x = centerPoint.x + maxRadius + 20.0f;
            legendPosition.y = centerPoint.y - (series.size() * 20.0f) * 0.5f;
        }
    }

    void UltraCanvasRadarChartElement::UpdateAnimationProgress() {
        if (!enableAnimation) {
            animationProgress = 1.0f;
            return;
        }

        auto currentTime = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                currentTime - radarAnimationStart).count();

        if (elapsed >= animationDurationMs) {
            animationProgress = 1.0f;
        } else {
            // Smoothstep easing
            float t = static_cast<float>(elapsed) / static_cast<float>(animationDurationMs);
            animationProgress = t * t * (3.0f - 2.0f * t);
            RequestRedraw(); // Continue animation
        }
    }

// =============================================================================
// COORDINATE TRANSFORMATION
// =============================================================================

    Point2Df UltraCanvasRadarChartElement::ValueToPolarCoordinate(size_t axisIndex, double value) const {
        if (axisIndex >= axes.size()) return Point2Df(0, 0);

        float angleStep = 360.0f / static_cast<float>(axes.size());
        float angle = startAngle + (clockwiseRotation ? 1.0f : -1.0f) * angleStep * static_cast<float>(axisIndex);

        double normalizedValue = NormalizeValue(axisIndex, value);
        normalizedValue = std::clamp(normalizedValue, 0.0, 1.0);

        float radius = static_cast<float>(normalizedValue) * maxRadius * animationProgress;

        return Point2Df(angle, radius);
    }

    Point2Df UltraCanvasRadarChartElement::PolarToScreen(float angle, float radius) const {
        float radians = angle * static_cast<float>(M_PI) / 180.0f;
        float x = centerPoint.x + radius * std::cos(radians);
        float y = centerPoint.y + radius * std::sin(radians);
        return Point2Df(x, y);
    }

    double UltraCanvasRadarChartElement::NormalizeValue(size_t axisIndex, double value) const {
        if (axisIndex >= axes.size()) return 0.0;

        const auto& axis = axes[axisIndex];
        double range = axis.maxValue - axis.minValue;
        if (range <= 0.0) return 0.0;

        return (value - axis.minValue) / range;
    }

// =============================================================================
// GRID RENDERING
// =============================================================================

    void UltraCanvasRadarChartElement::DrawRadarGrid(IRenderContext* ctx) {
        if (axes.empty()) return;

        ctx->SetStrokePaint(gridColor);
        ctx->SetStrokeWidth(gridLineWidth);

        // Determine number of grid levels
        int maxGridLevels = 5;
        for (const auto& axis : axes) {
            if (axis.showGrid) {
                maxGridLevels = std::max(maxGridLevels, axis.gridLevels);
            }
        }
        if (maxGridLevels < 1) maxGridLevels = 1;

        // Concentric grid rings
        for (int level = 1; level <= maxGridLevels; ++level) {
            float radius = (static_cast<float>(level) / static_cast<float>(maxGridLevels)) * maxRadius * animationProgress;
            ctx->DrawCircle(Point2Dd(centerPoint.x, centerPoint.y), radius);
        }

        // Radial spokes (one per axis)
        float angleStep = 360.0f / static_cast<float>(axes.size());
        for (size_t i = 0; i < axes.size(); ++i) {
            float angle = startAngle + (clockwiseRotation ? 1.0f : -1.0f) * angleStep * static_cast<float>(i);
            Point2Df outerPoint = PolarToScreen(angle, maxRadius * animationProgress);
            ctx->DrawLine(Point2Dd(centerPoint.x, centerPoint.y), Point2Dd(outerPoint.x, outerPoint.y));
        }
    }

// =============================================================================
// AXIS LABEL RENDERING
// =============================================================================

    void UltraCanvasRadarChartElement::DrawAxisLabels(IRenderContext* ctx) {
        if (axes.empty()) return;

        ctx->SetFontFamily(axisLabelFont);
        ctx->SetFontSize(axisLabelFontSize);
        ctx->SetFontWeight(FontWeight::Normal);
        ctx->SetTextPaint(axisLabelColor);

        float angleStep = 360.0f / static_cast<float>(axes.size());

        for (size_t i = 0; i < axes.size(); ++i) {
            const auto& axis = axes[i];
            float angle = startAngle + (clockwiseRotation ? 1.0f : -1.0f) * angleStep * static_cast<float>(i);

            float labelRadius = maxRadius + axisLabelFontSize;
            Point2Df labelPos = PolarToScreen(angle, labelRadius);

            Size2Di textSize = ctx->GetTextLineDimensions(axis.name);

            float radians = angle * static_cast<float>(M_PI) / 180.0f;

            // Horizontal alignment based on angle
            if (std::cos(radians) < -0.5f) {
                labelPos.x -= textSize.width;
            } else if (std::cos(radians) > 0.5f) {
                // keep
            } else {
                labelPos.x -= textSize.width * 0.5f;
            }

            // Vertical alignment based on angle
            if (std::sin(radians) < -0.5f) {
                labelPos.y -= textSize.height;
            } else if (std::sin(radians) > 0.5f) {
                // keep
            } else {
                labelPos.y -= textSize.height * 0.5f;
            }

            ctx->DrawText(axis.name, Point2Dd(labelPos.x, labelPos.y));
        }
    }

// =============================================================================
// SERIES RENDERING
// =============================================================================

    void UltraCanvasRadarChartElement::DrawRadarSeries(IRenderContext* ctx) {
        if (axes.empty() || series.empty()) return;

        for (size_t seriesIndex = 0; seriesIndex < series.size(); ++seriesIndex) {
            const auto& currentSeries = series[seriesIndex];

            if (currentSeries.values.size() != axes.size()) continue;

            std::vector<Point2Dd> polygonPoints;
            polygonPoints.reserve(axes.size());

            for (size_t axisIndex = 0; axisIndex < axes.size(); ++axisIndex) {
                Point2Df polar = ValueToPolarCoordinate(axisIndex, currentSeries.values[axisIndex]);
                Point2Df screenPos = PolarToScreen(polar.x, polar.y);
                polygonPoints.emplace_back(screenPos.x, screenPos.y);
            }

            // Filled polygon
            if (currentSeries.fillOpacity > 0.0f && polygonPoints.size() >= 3) {
                Color fillColor = currentSeries.fillColor;
                fillColor.a = static_cast<uint8_t>(fillColor.a * currentSeries.fillOpacity);
                ctx->SetFillPaint(fillColor);
                ctx->FillLinePath(polygonPoints);
            }

            // Outline (closed polygon)
            if (currentSeries.strokeWidth > 0.0f && polygonPoints.size() >= 2) {
                ctx->SetStrokePaint(currentSeries.strokeColor);
                ctx->SetStrokeWidth(currentSeries.strokeWidth);
                ctx->DrawLinePath(polygonPoints, true);
            }

            // Points
            if (currentSeries.showPoints) {
                DrawSeriesPoints(ctx, seriesIndex);
            }
        }
    }

    void UltraCanvasRadarChartElement::DrawSeriesPoints(IRenderContext* ctx, size_t seriesIndex) {
        if (seriesIndex >= series.size() || axes.empty()) return;

        const auto& currentSeries = series[seriesIndex];

        for (size_t axisIndex = 0; axisIndex < axes.size(); ++axisIndex) {
            if (axisIndex >= currentSeries.values.size()) continue;

            Point2Df polar = ValueToPolarCoordinate(axisIndex, currentSeries.values[axisIndex]);
            Point2Df screenPos = PolarToScreen(polar.x, polar.y);

            // Filled point with a white outline for contrast
            ctx->DrawFilledCircle(Point2Dd(screenPos.x, screenPos.y), currentSeries.pointRadius,
                                  currentSeries.strokeColor, Color(255, 255, 255, 255), 1.0f);
        }
    }

// =============================================================================
// VALUE LABEL RENDERING
// =============================================================================

    void UltraCanvasRadarChartElement::DrawValueLabels(IRenderContext* ctx) {
        if (!showValueLabels || axes.empty() || series.empty()) return;

        ctx->SetFontFamily(axisLabelFont);
        ctx->SetFontSize(valueLabelFontSize);
        ctx->SetFontWeight(FontWeight::Normal);
        ctx->SetTextPaint(valueLabelColor);

        // Only label the first series to avoid clutter
        const auto& firstSeries = series[0];

        for (size_t axisIndex = 0; axisIndex < axes.size() && axisIndex < firstSeries.values.size(); ++axisIndex) {
            double value = firstSeries.values[axisIndex];

            Point2Df polar = ValueToPolarCoordinate(axisIndex, value);
            Point2Df screenPos = PolarToScreen(polar.x, polar.y);

            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1) << value;
            std::string valueText = oss.str();

            Size2Di textSize = ctx->GetTextLineDimensions(valueText);

            ctx->DrawText(valueText,
                          Point2Dd(screenPos.x - textSize.width * 0.5,
                                   screenPos.y - textSize.height * 0.5));
        }
    }

// =============================================================================
// LEGEND RENDERING
// =============================================================================

    void UltraCanvasRadarChartElement::DrawRadarLegend(IRenderContext* ctx) {
        if (!showLegend || series.empty()) return;

        float legendItemHeight = 20.0f;
        float legendPadding = 10.0f;
        float legendWidth = 120.0f;
        float legendHeight = series.size() * legendItemHeight + legendPadding * 2;

        // Legend background + border
        ctx->DrawFilledRectangle(Rect2Dd(legendPosition.x, legendPosition.y, legendWidth, legendHeight),
                                 legendBackgroundColor, 1.0f, Color(160, 160, 160, 255));

        ctx->SetFontFamily(axisLabelFont);
        ctx->SetFontSize(11.0f);
        ctx->SetFontWeight(FontWeight::Normal);

        for (size_t i = 0; i < series.size(); ++i) {
            const auto& currentSeries = series[i];

            float itemY = legendPosition.y + legendPadding + i * legendItemHeight;
            float colorBoxSize = 12.0f;

            // Color swatch
            ctx->SetFillPaint(currentSeries.strokeColor);
            ctx->FillRectangle(Rect2Dd(legendPosition.x + legendPadding, itemY + 2, colorBoxSize, colorBoxSize));

            // Series name
            ctx->SetTextPaint(legendTextColor);
            ctx->DrawText(currentSeries.name,
                          Point2Dd(legendPosition.x + legendPadding + colorBoxSize + 5,
                                   itemY + colorBoxSize * 0.5));
        }
    }

// =============================================================================
// INTERACTION HANDLING
// =============================================================================

    bool UltraCanvasRadarChartElement::HandleChartMouseMove(const Point2Di& mousePos) {
        if (axes.empty() || series.empty() || !enableTooltips) return false;

        auto closestPoint = FindClosestSeriesPoint(mousePos);
        size_t seriesIndex = closestPoint.first;
        size_t axisIndex = closestPoint.second;

        if (seriesIndex < series.size() && axisIndex < axes.size() &&
            axisIndex < series[seriesIndex].values.size()) {
            Point2Df polar = ValueToPolarCoordinate(axisIndex, series[seriesIndex].values[axisIndex]);
            Point2Df screenPos = PolarToScreen(polar.x, polar.y);

            float dx = static_cast<float>(mousePos.x) - screenPos.x;
            float dy = static_cast<float>(mousePos.y) - screenPos.y;
            float distance = std::sqrt(dx * dx + dy * dy);

            if (distance <= 15.0f) {
                ShowRadarTooltip(seriesIndex, axisIndex, mousePos);
                hoveredPointIndex = axisIndex;
                return true;
            }
        }

        if (isTooltipActive) {
            HideTooltip();
        }
        return false;
    }

    std::pair<size_t, size_t> UltraCanvasRadarChartElement::FindClosestSeriesPoint(const Point2Di& mousePos) const {
        if (axes.empty() || series.empty()) {
            return std::make_pair(SIZE_MAX, SIZE_MAX);
        }

        size_t closestSeries = 0;
        size_t closestAxis = 0;
        float minDistance = std::numeric_limits<float>::max();

        for (size_t seriesIndex = 0; seriesIndex < series.size(); ++seriesIndex) {
            const auto& currentSeries = series[seriesIndex];

            for (size_t axisIndex = 0; axisIndex < axes.size() && axisIndex < currentSeries.values.size(); ++axisIndex) {
                Point2Df polar = ValueToPolarCoordinate(axisIndex, currentSeries.values[axisIndex]);
                Point2Df screenPos = PolarToScreen(polar.x, polar.y);

                float dx = static_cast<float>(mousePos.x) - screenPos.x;
                float dy = static_cast<float>(mousePos.y) - screenPos.y;
                float distance = std::sqrt(dx * dx + dy * dy);

                if (distance < minDistance) {
                    minDistance = distance;
                    closestSeries = seriesIndex;
                    closestAxis = axisIndex;
                }
            }
        }

        return std::make_pair(closestSeries, closestAxis);
    }

    void UltraCanvasRadarChartElement::ShowRadarTooltip(size_t seriesIndex, size_t axisIndex, const Point2Di& mousePos) {
        if (seriesIndex >= series.size() || axisIndex >= axes.size()) return;

        const auto& currentSeries = series[seriesIndex];
        const auto& axis = axes[axisIndex];

        if (axisIndex >= currentSeries.values.size()) return;

        std::ostringstream tooltip;
        tooltip << currentSeries.name << "\n";
        tooltip << axis.name << ": " << std::fixed << std::setprecision(2) << currentSeries.values[axisIndex];

        Point2Df windowMousePos = MapFromLocal(Point2Df(mousePos.x, mousePos.y), nullptr);
        UltraCanvasTooltipManager::UpdateAndShowTooltip(
                GetWindow(), tooltip.str(),
                Point2Di(static_cast<int>(windowMousePos.x), static_cast<int>(windowMousePos.y)));
        isTooltipActive = true;
        hoveredPointIndex = axisIndex;
    }

} // namespace UltraCanvas
