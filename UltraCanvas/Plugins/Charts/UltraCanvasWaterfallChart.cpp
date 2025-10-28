// UltraCanvasWaterfallChart.cpp
// Waterfall chart element implementation with cumulative flow visualization
// Version: 1.0.1
// Last Modified: 2025-09-29
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasWaterfallChart.h"
#include "UltraCanvasTooltipManager.h"
#include <algorithm>
#include <limits>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace UltraCanvas {

// =============================================================================
// COMMON BACKGROUND AND GRID METHODS (OVERRIDES FROM BASE)
// =============================================================================

//    void UltraCanvasWaterfallChartElement::DrawCommonBackground(IRenderContext* ctx) {
//        if (!ctx) return;
//
//        // Draw overall background
//        ctx->PaintWithColor(backgroundColor);
//        ctx->FillRectangle(GetX(), GetY(), GetWidth(), GetHeight());
//
//        // Draw plot area background
//        ctx->PaintWithColor(plotAreaColor);
//        ctx->FillRectangle(cachedPlotArea.x, cachedPlotArea.y,
//                           cachedPlotArea.width, cachedPlotArea.height);
//
//        // Draw plot area border
//        ctx->PaintWithColor(Color(180, 180, 180, 255));
//        ctx->SetStrokeWidth(1.0f);
//        ctx->DrawRectangle(cachedPlotArea.x, cachedPlotArea.y,
//                           cachedPlotArea.width, cachedPlotArea.height);
//
//        // Draw title if present
//        if (!chartTitle.empty()) {
//            ctx->SetTextColor(Color(0, 0, 0, 255));
//            ctx->SetFontSize(16.0f);
//
//            int textWidth, textHeight;
//            ctx->GetTextLineDimensions(chartTitle, textWidth, textHeight);
//            int titleX = GetX() + (GetWidth() - textWidth) / 2;
//            int titleY = GetY() + 10;
//            ctx->DrawText(chartTitle, titleX, titleY);
//        }
//    }

//    void UltraCanvasWaterfallChartElement::DrawGrid(IRenderContext* ctx) {
//        if (!ctx || !showGrid) return;
//
//        ctx->PaintWithColor(gridColor);
//        ctx->SetStrokeWidth(1.0f);
//
//        // Horizontal grid lines
//        int numHorizontalLines = 8;
//        for (int i = 1; i < numHorizontalLines; ++i) {
//            float y = cachedPlotArea.y + (i * cachedPlotArea.height / numHorizontalLines);
//            ctx->DrawLine(cachedPlotArea.x, y,
//                          cachedPlotArea.x + cachedPlotArea.width, y);
//        }
//
//        // Vertical grid lines (light)
//        auto waterfallData = GetWaterfallDataSource();
//        if (waterfallData && renderCache.barX.size() > 0) {
//            ctx->PaintWithColor(Color(gridColor.r, gridColor.g, gridColor.b, 100)); // Lighter
//            for (size_t i = 0; i < renderCache.barX.size(); ++i) {
//                float x = renderCache.barX[i] + renderCache.barWidth / 2;
//                ctx->DrawLine(x, cachedPlotArea.y, x, cachedPlotArea.GetBottom());
//            }
//        }
//    }

//    void UltraCanvasWaterfallChartElement::DrawAxes(IRenderContext* ctx) {
//        if (!ctx) return;
//
//        ctx->PaintWithColor(Color(0, 0, 0, 255));
//        ctx->SetStrokeWidth(2.0f);
//
//        // Draw X-axis
//        ctx->DrawLine(cachedPlotArea.x, cachedPlotArea.GetBottom(),
//                      cachedPlotArea.x + cachedPlotArea.width, cachedPlotArea.GetBottom());
//
//        // Draw Y-axis
//        ctx->DrawLine(cachedPlotArea.x, cachedPlotArea.y,
//                      cachedPlotArea.x, cachedPlotArea.GetBottom());
//
//        // Draw axis labels
//        DrawAxisLabels(ctx);
//    }

    void UltraCanvasWaterfallChartElement::RenderAxisLabels(UltraCanvas::IRenderContext *ctx) {
        if (!ctx) return;

        ctx->SetStrokePaint(Color(0, 0, 0, 255));
        ctx->SetTextPaint(Color(0, 0, 0, 255));
        ctx->SetFontSize(10.0f);

        // Y-axis labels (values)
        int numYTicks = 6;
        for (int i = 0; i <= numYTicks; ++i) {
            double value = cachedDataBounds.minY +
                           (i * (cachedDataBounds.maxY - cachedDataBounds.minY) / numYTicks);
            float y = cachedPlotArea.GetBottom() -
                      (i * cachedPlotArea.height / numYTicks);

            // Draw tick mark
            ctx->DrawLine(cachedPlotArea.x - 5, y, cachedPlotArea.x, y);

            // Draw label
            std::string label = FormatValue(value);
            int textWidth, textHeight;
            ctx->GetTextLineDimensions(label, textWidth, textHeight);
            ctx->DrawText(label, cachedPlotArea.x - textWidth - 8, y - textHeight/2);
        }

        // X-axis labels are drawn in DrawValueLabels() for each bar
    }

// =============================================================================
// MAIN RENDERING METHOD
// =============================================================================

    void UltraCanvasWaterfallChartElement::RenderChart(IRenderContext* ctx) {
//        if (!ctx || !dataSource || dataSource->GetPointCount() == 0) {
//            return;
//        }
//
//        auto waterfallData = GetWaterfallDataSource();
//        if (!waterfallData) return;
//
//        // Update rendering cache if needed
//        if (!renderCache.isValid) {
//            UpdateRenderingCache();
//        }
//
//        // Draw chart background and grid
//        DrawCommonBackground(ctx);
//        if (showGrid) {
//            DrawGrid(ctx);
//        }
//        DrawAxes(ctx);

        // Draw main waterfall bars
        DrawWaterfallBars(ctx);

        // Draw connecting lines between bars
        if (enableConnectorLines && connectionStyle != ConnectionStyle::NoneStyle) {
            DrawConnectionLines(ctx);
        }

        // Draw value labels
        if (showValueLabels || showCumulativeLabels) {
            DrawValueLabels(ctx);
        }
    }

// =============================================================================
// CACHE AND CALCULATION METHODS
// =============================================================================

    void UltraCanvasWaterfallChartElement::UpdateRenderingCache() {
        UltraCanvasChartElementBase::UpdateRenderingCache();
        CalculateBarDimensions();
        renderCache.isValid = true;
    }

    void UltraCanvasWaterfallChartElement::CalculateBarDimensions() {
        auto waterfallData = GetWaterfallDataSource();
        if (!waterfallData) return;

        size_t pointCount = waterfallData->GetPointCount();
        if (pointCount == 0) return;

        size_t totalBars = pointCount;

        // Calculate bar dimensions
        float totalWidth = cachedPlotArea.width;
        float totalSpacing = totalWidth * barSpacing;
        float availableWidth = totalWidth - totalSpacing;
        renderCache.barWidth = availableWidth / static_cast<float>(totalBars);
        renderCache.barSpacing = totalSpacing / static_cast<float>(totalBars + 1);

        // Clear and resize vectors
        renderCache.barX.clear();
        renderCache.barY.clear();
        renderCache.barHeight.clear();
        renderCache.barColors.clear();

        renderCache.barX.reserve(pointCount);
        renderCache.barY.reserve(pointCount);
        renderCache.barHeight.reserve(pointCount);
        renderCache.barColors.reserve(pointCount);

        // Calculate positions for each bar
        float currentX = cachedPlotArea.x + renderCache.barSpacing;

        // Ensure we have valid data bounds
        if (cachedDataBounds.maxY <= cachedDataBounds.minY) {
            return; // Invalid bounds, avoid division by zero
        }

        for (size_t i = 0; i < pointCount; ++i) {
            const auto& point = waterfallData->GetWaterfallPoint(i);

            // Calculate bar position and height
            renderCache.barX.push_back(currentX);

            // Convert data values to screen coordinates
            auto dataToScreenY = [this](double value) -> float {
                double normalized = (value - cachedDataBounds.minY) / (cachedDataBounds.maxY - cachedDataBounds.minY);
                // Clamp to [0,1] to prevent drawing outside bounds
                normalized = std::max(0.0, std::min(1.0, normalized));
                return cachedPlotArea.GetBottom() - static_cast<float>(normalized * cachedPlotArea.height);
            };

            float barTop, barBottom;

            if (point.isTotal || point.isSubtotal) {
                // Total/subtotal bars go from 0 to cumulative value
                float zeroY = dataToScreenY(0.0);
                float valueY = dataToScreenY(point.cumulativeValue);

                barTop = std::min(zeroY, valueY);
                barBottom = std::max(zeroY, valueY);
            } else {
                // Regular bars show the change
                double prevCumulative = (i > 0) ?
                                        waterfallData->GetWaterfallPoint(i-1).cumulativeValue :
                                        0.0;

                float prevY = dataToScreenY(prevCumulative);
                float currentY = dataToScreenY(point.cumulativeValue);

                barTop = std::min(prevY, currentY);
                barBottom = std::max(prevY, currentY);
            }

            // Store the top position and height
            renderCache.barY.push_back(barTop);
            renderCache.barHeight.push_back(barBottom - barTop);
            renderCache.barColors.push_back(GetBarColor(point));

            currentX += renderCache.barWidth + renderCache.barSpacing;
        }
    }
    ChartDataBounds UltraCanvasWaterfallChartElement::CalculateDataBounds() {
        auto ds = GetWaterfallDataSource();
        if (!ds || ds->GetPointCount() == 0) {
            return ChartDataBounds();
        }

        size_t pointCount = ds->GetPointCount();

        double minY = 0.0;
        double maxY = 0.0;
        // Track cumulative value as we iterate through points
        double currentCumulative = 0.0;

        // Also consider zero as a potential bound (for axis display)
        minY = std::min(minY, 0.0);
        maxY = std::max(maxY, 0.0);

        for (size_t i = 0; i < pointCount; ++i) {
            const auto& point = ds->GetWaterfallPoint(i);

            // For regular bars, we need to consider both the start and end of the bar
            if (!point.isTotal && !point.isSubtotal) {
                // The bar starts at currentCumulative
                minY = std::min(minY, currentCumulative);
                maxY = std::max(maxY, currentCumulative);

                // The bar ends at the new cumulative value
                minY = std::min(minY, point.cumulativeValue);
                maxY = std::max(maxY, point.cumulativeValue);

                // Update current cumulative for next iteration
                currentCumulative = point.cumulativeValue;
            } else {
                // For totals and subtotals, bar goes from 0 to cumulative value
                minY = std::min(minY, 0.0);
                minY = std::min(minY, point.cumulativeValue);
                maxY = std::max(maxY, 0.0);
                maxY = std::max(maxY, point.cumulativeValue);

                // Reset cumulative tracking after total/subtotal
                currentCumulative = point.cumulativeValue;
            }
        }

        // Add padding to prevent bars from touching the edges
        double range = maxY - minY;
        if (range > 0) {
//            double padding = range; // 10% padding
//            minY -= padding;
            maxY += range * 0.1;
        } else if (range == 0) {
            // If all values are the same, add some default padding
            minY -= 1.0;
            maxY += 1.0;
        }

        return ChartDataBounds(0, static_cast<double>(pointCount - 1), minY, maxY);
    }
// =============================================================================
// BAR RENDERING METHODS
// =============================================================================

    void UltraCanvasWaterfallChartElement::DrawWaterfallBars(IRenderContext* ctx) {
        auto waterfallData = GetWaterfallDataSource();
        if (!waterfallData) return;

        size_t pointCount = waterfallData->GetPointCount();

        for (size_t i = 0; i < pointCount; ++i) {
            if (i < renderCache.barX.size()) {
                DrawSingleBar(ctx,
                              renderCache.barX[i],
                              renderCache.barY[i],
                              renderCache.barWidth,
                              renderCache.barHeight[i],
                              renderCache.barColors[i]);
            }
        }
    }

    void UltraCanvasWaterfallChartElement::DrawSingleBar(IRenderContext* ctx, float x, float y,
                                                         float width, float height, const Color& fillColor, bool hasBorder) {
        if (!ctx || height <= 0) return;

        switch (barStyle) {
            case BarStyle::Standard:
                ctx->SetFillPaint(fillColor);
                ctx->FillRectangle(x, y, width, height);
                break;

            case BarStyle::Rounded:
                ctx->SetFillPaint(fillColor);
                ctx->FillRoundedRectangle(x, y, width, height, 4.0f);
                break;

            case BarStyle::Gradient:
                // Create a subtle gradient effect
                Color lighterColor = Color(
                        std::min(255, static_cast<int>(fillColor.r * 1.2f)),
                        std::min(255, static_cast<int>(fillColor.g * 1.2f)),
                        std::min(255, static_cast<int>(fillColor.b * 1.2f)),
                        fillColor.a
                );
                ctx->SetFillPaint(ctx->CreateLinearGradientPattern(x,y, x, x + height, {
                        GradientStop(0, lighterColor),
                        GradientStop(1, fillColor),
                }));
                ctx->FillRectangle(x, y, width, height);
                break;
        }

        // Draw border if enabled
        if (hasBorder && barBorderWidth > 0) {
            ctx->SetStrokePaint(barBorderColor);
            ctx->SetStrokeWidth(barBorderWidth);

            if (barStyle == BarStyle::Rounded) {
                ctx->DrawRoundedRectangle(x, y, width, height, 4.0f);
            } else {
                ctx->DrawRectangle(x, y, width, height);
            }
        }
    }

// =============================================================================
// CONNECTION LINE RENDERING
// =============================================================================

    void UltraCanvasWaterfallChartElement::DrawConnectionLines(IRenderContext* ctx) {
        auto waterfallData = GetWaterfallDataSource();
        if (!waterfallData) return;

        ctx->SetStrokePaint(connectionLineColor);
        ctx->SetStrokeWidth(connectionLineWidth);

        size_t pointCount = waterfallData->GetPointCount();

        // Ensure we have valid data bounds
        if (cachedDataBounds.maxY <= cachedDataBounds.minY) {
            return; // Invalid bounds, avoid division by zero
        }

        // Convert data values to screen coordinates
        auto dataToScreenY = [this](double value) -> float {
            double normalized = (value - cachedDataBounds.minY) / (cachedDataBounds.maxY - cachedDataBounds.minY);
            // Clamp to [0,1] to prevent drawing outside bounds
            normalized = std::max(0.0, std::min(1.0, normalized));
            return cachedPlotArea.GetBottom() - static_cast<float>(normalized * cachedPlotArea.height);
        };

        for (size_t i = 1; i < pointCount; ++i) {
            const auto& currentPoint = waterfallData->GetWaterfallPoint(i);
            const auto& prevPoint = waterfallData->GetWaterfallPoint(i-1);

            // Skip connection lines for totals and subtotals
            //if (currentPoint.isTotal || currentPoint.isSubtotal) continue;

            // Calculate connection line endpoints
            float prevBarRight = renderCache.barX[i-1] + renderCache.barWidth;
            float currentBarLeft = renderCache.barX[i];

            // Y position is at the level where the previous bar ended
            float lineY = dataToScreenY(prevPoint.cumulativeValue);

            DrawConnectionLine(ctx, prevBarRight, lineY, currentBarLeft, lineY);
        }
    }

    void UltraCanvasWaterfallChartElement::DrawConnectionLine(IRenderContext* ctx, float x1, float y1, float x2, float y2) {
        switch (connectionStyle) {
            case ConnectionStyle::Solid:
                ctx->DrawLine(x1, y1, x2, y2);
                break;

            case ConnectionStyle::Dotted:
            {
                float dashLength = 3.0f;
                float gapLength = 3.0f;
                float totalLength = std::sqrt((x2-x1)*(x2-x1) + (y2-y1)*(y2-y1));
                int numDashes = static_cast<int>(totalLength / (dashLength + gapLength));

                for (int i = 0; i < numDashes; ++i) {
                    float t1 = (i * (dashLength + gapLength)) / totalLength;
                    float t2 = ((i * (dashLength + gapLength)) + dashLength) / totalLength;

                    float startX = x1 + t1 * (x2 - x1);
                    float startY = y1 + t1 * (y2 - y1);
                    float endX = x1 + t2 * (x2 - x1);
                    float endY = y1 + t2 * (y2 - y1);

                    ctx->DrawLine(startX, startY, endX, endY);
                }
            }
                break;

            case ConnectionStyle::Dashed:
            {
                float dashLength = 8.0f;
                float gapLength = 4.0f;
                float totalLength = std::sqrt((x2-x1)*(x2-x1) + (y2-y1)*(y2-y1));
                int numDashes = static_cast<int>(totalLength / (dashLength + gapLength));

                for (int i = 0; i < numDashes; ++i) {
                    float t1 = (i * (dashLength + gapLength)) / totalLength;
                    float t2 = ((i * (dashLength + gapLength)) + dashLength) / totalLength;

                    float startX = x1 + t1 * (x2 - x1);
                    float startY = y1 + t1 * (y2 - y1);
                    float endX = x1 + t2 * (x2 - x1);
                    float endY = y1 + t2 * (y2 - y1);

                    ctx->DrawLine(startX, startY, endX, endY);
                }
            }
                break;

            case ConnectionStyle::NoneStyle:
            default:
                // No connecting lines
                break;
        }
    }

// =============================================================================
// LABEL RENDERING
// =============================================================================

    void UltraCanvasWaterfallChartElement::DrawValueLabels(IRenderContext* ctx) {
        auto waterfallData = GetWaterfallDataSource();
        if (!waterfallData) return;

        ctx->SetTextPaint(labelTextColor);
        ctx->SetFontSize(labelFontSize);

        size_t pointCount = waterfallData->GetPointCount();

        for (size_t i = 0; i < pointCount && i < renderCache.barX.size(); ++i) {
            const auto& point = waterfallData->GetWaterfallPoint(i);

            float barCenterX = renderCache.barX[i] + renderCache.barWidth / 2;

            if (showValueLabels) {
                // Show the change value above/below the bar
                std::string valueText;
                if (point.isSubtotal || point.isTotal) {
                    valueText = FormatValue(point.cumulativeValue);
                } else {
                    valueText = FormatValue(point.value);
                }
                int textWidth, textHeight;
                ctx->GetTextLineDimensions(valueText, textWidth, textHeight);

                float labelY = (point.value >= 0) ?
                               renderCache.barY[i] - (textHeight + 3):
                               renderCache.barY[i] + renderCache.barHeight[i] + 1;

                ctx->DrawText(valueText, barCenterX - textWidth/2, labelY);
            }

            if (showCumulativeLabels) {
                // Show the cumulative value
                std::string cumulativeText = FormatValue(point.cumulativeValue);
                int textWidth, textHeight;
                ctx->GetTextLineDimensions(cumulativeText, textWidth, textHeight);

                float labelY = renderCache.barY[i] + renderCache.barHeight[i]/2 - textHeight/2;
                ctx->DrawText(cumulativeText, barCenterX - textWidth/2, labelY);
            }

            // Draw category label on X-axis
            if (!point.label.empty()) {
                int textWidth, textHeight;
                ctx->GetTextLineDimensions(point.label, textWidth, textHeight);
                float labelY = cachedPlotArea.GetBottom() + 5;
                ctx->DrawText(point.label, barCenterX - textWidth/2, labelY);
            }
        }
    }

// =============================================================================
// HELPER METHODS
// =============================================================================

    Color UltraCanvasWaterfallChartElement::GetBarColor(const WaterfallChartDataPoint& point) const {
        // Check for custom color override
        if (point.customColor.a > 0) {
            return point.customColor;
        }

        // Use type-based coloring
        if (point.isTotal) {
            return totalBarColor;
        } else if (point.isSubtotal) {
            return subtotalBarColor;
        } else if (point.value >= 0) {
            return positiveBarColor;
        } else {
            return negativeBarColor;
        }
    }

    std::string UltraCanvasWaterfallChartElement::FormatValue(double value) const {
        std::ostringstream oss;
        if (std::abs(value) >= 1e6) {
            oss << std::fixed << std::setprecision(1) << (value / 1e6) << "M";
        } else if (std::abs(value) >= 1e3) {
            oss << std::fixed << std::setprecision(1) << (value / 1e3) << "K";
        } else if (std::abs(value - std::round(value)) < 0.01) {
            oss << static_cast<int>(std::round(value));
        } else {
            oss << std::fixed << std::setprecision(1) << value;
        }
        return oss.str();
    }

// =============================================================================
// INTERACTION HANDLING
// =============================================================================

    bool UltraCanvasWaterfallChartElement::HandleChartMouseMove(const Point2Di& mousePos) {
        if (!enableTooltips) return false;

        size_t barIndex = GetBarIndexAtPosition(mousePos);

        if (barIndex != SIZE_MAX && barIndex != hoveredPointIndex) {
            hoveredPointIndex = barIndex;
            auto mpos = mousePos;
            mpos.x += 10;
            mpos.y -= 30;
            ConvertContainerToWindowCoordinates(mpos.x, mpos.y);
            // Generate and show tooltip
            std::string tooltipContent = GenerateWaterfallTooltip(barIndex);
            if (!tooltipContent.empty()) {
                UltraCanvasTooltipManager::UpdateAndShowTooltip(GetWindow(), tooltipContent, mpos);
                isTooltipActive = true;
            }

            RequestRedraw();
            return true;
        } else if (barIndex == SIZE_MAX && isTooltipActive) {
            UltraCanvasTooltipManager::HideTooltip();
            hoveredPointIndex = SIZE_MAX;
            isTooltipActive = false;
            RequestRedraw();
            return true;
        }

        return false;
    }

    size_t UltraCanvasWaterfallChartElement::GetBarIndexAtPosition(const Point2Di& mousePos) const {
        if (renderCache.barX.empty()) return SIZE_MAX;

        // Check if mouse is within plot area vertically
        if (mousePos.y < cachedPlotArea.y || mousePos.y > cachedPlotArea.GetBottom()) {
            return SIZE_MAX;
        }

        // Find which bar the mouse is over
        for (size_t i = 0; i < renderCache.barX.size(); ++i) {
            float barLeft = renderCache.barX[i];
            float barRight = barLeft + renderCache.barWidth;

            if (mousePos.x >= barLeft && mousePos.x <= barRight) {
                // Check if mouse is within the bar height
                float barTop = renderCache.barY[i];
                float barBottom = barTop + renderCache.barHeight[i];

                if (mousePos.y >= barTop && mousePos.y <= barBottom) {
                    return i;
                }
            }
        }

        return SIZE_MAX;
    }

    std::string UltraCanvasWaterfallChartElement::GenerateWaterfallTooltip(size_t index) const {
        auto waterfallData = GetWaterfallDataSource();
        if (!waterfallData || index >= waterfallData->GetPointCount()) {
            return "";
        }

        const auto& point = waterfallData->GetWaterfallPoint(index);

        std::ostringstream tooltip;
        tooltip << point.label << "\n";

        if (point.isTotal) {
            tooltip << "Total: " << FormatValue(point.cumulativeValue);
        } else if (point.isSubtotal) {
            tooltip << "Subtotal: " << FormatValue(point.cumulativeValue);
        } else {
            tooltip << "Change: " << (point.value >= 0 ? "+" : "") << FormatValue(point.value) << "\n";
            tooltip << "Cumulative: " << FormatValue(point.cumulativeValue);
        }

        if (!point.category.empty()) {
            tooltip << "\nCategory: " << point.category;
        }

        return tooltip.str();
    }

} // namespace UltraCanvas