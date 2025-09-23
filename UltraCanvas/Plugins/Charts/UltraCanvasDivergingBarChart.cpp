// UltraCanvasDivergingBarChart.cpp
// Diverging bar chart component for multi-valued categorical data (population pyramid, likert scales, etc.)
// Version: 1.1.0
// Last Modified: 2025-09-23
// Author: UltraCanvas Framework
#include "Plugins/Charts/UltraCanvasDivergingBarChart.h"

namespace UltraCanvas {
    UltraCanvasDivergingBarChart::UltraCanvasDivergingBarChart(const std::string &id, long uid, int x,
                                                                            int y, int width, int height)
            : UltraCanvasChartElementBase(id, uid, x, y, width, height) {
        enableTooltips = true;

        // Initialize diverging data source
        divergingDataSource = CreateDivergingDataSource();
        dataSource = divergingDataSource; // Set base class data source
    }

    void UltraCanvasDivergingBarChart::SetCategories(const std::vector<DivergingCategory> &cats) {
        categories = cats;

        // Update data source categories
        if (divergingDataSource) {
            for (const auto& cat : categories) {
                divergingDataSource->AddCategory(cat.name, cat.isPositive);
            }
        }

        needsRecalculation = true;
        RequestRedraw();
    }

    void UltraCanvasDivergingBarChart::AddDataRow(const std::string &rowLabel,
                                                               const std::map<std::string, float> &values) {
        if (divergingDataSource) {
            divergingDataSource->AddDataRow(rowLabel, values);
            needsRecalculation = true;
            RequestRedraw();
        }
    }

    void UltraCanvasDivergingBarChart::LoadDataMatrix(const std::vector<std::string> &rowLabels,
                                                                   const std::vector<std::vector<float>> &matrix) {
        if (divergingDataSource) {
            divergingDataSource->AddDataMatrix(rowLabels, matrix);
            needsRecalculation = true;
            RequestRedraw();
        }
    }

    void UltraCanvasDivergingBarChart::ClearData() {
        if (divergingDataSource) {
            divergingDataSource->Clear();
            needsRecalculation = true;
            RequestRedraw();
        }
    }

    void UltraCanvasDivergingBarChart::SetChartStyle(DivergingChartStyle style) {
        chartStyle = style;
        RequestRedraw();
    }

    void UltraCanvasDivergingBarChart::SetBarHeight(float height) {
        barHeight = std::clamp(height, 0.1f, 1.0f);
        RequestRedraw();
    }

    void UltraCanvasDivergingBarChart::SetCenterGap(float gap) {
        centerGap = std::max(0.0f, gap);
        RequestRedraw();
    }

    void UltraCanvasDivergingBarChart::SetShowCenterLine(bool show) {
        showCenterLine = show;
        RequestRedraw();
    }

    void UltraCanvasDivergingBarChart::SetShowRowLabels(bool show) {
        showRowLabels = show;
        RequestRedraw();
    }

    void UltraCanvasDivergingBarChart::SetShowValueLabels(bool show) {
        showValueLabels = show;
        RequestRedraw();
    }

    void UltraCanvasDivergingBarChart::RenderChart(IRenderContext *ctx) {
        if (!ctx || !divergingDataSource || divergingDataSource->GetPointCount() == 0) return;

        // Recalculate bounds if needed
        if (needsRecalculation) {
            cachedDataBounds = CalculateDataBounds();
        }

        // Apply animation
        float animScale = enableAnimation ? GetAnimationScale() : 1.0f;

        // Draw grid first
        if (showGrid) {
            RenderGrid(ctx);
        }

        // Draw center line
        if (showCenterLine) {
            RenderCenterLine(ctx);
        }

        // Render bars based on style
        switch (chartStyle) {
            case DivergingChartStyle::PopulationPyramid:
                RenderPopulationPyramid(ctx, animScale);
                break;
            case DivergingChartStyle::LikertScale:
                RenderLikertScale(ctx, animScale);
                break;
            case DivergingChartStyle::TornadoChart:
                RenderTornadoChart(ctx, animScale);
                break;
            case DivergingChartStyle::OpposingBars:
                RenderOpposingBars(ctx, animScale);
                break;
        }

        // Draw row labels
        if (showRowLabels) {
            RenderRowLabels(ctx);
        }

        // Draw axis labels
//            RenderAxisLabels(ctx);
    }

    bool UltraCanvasDivergingBarChart::HandleChartMouseMove(const Point2Di &mousePos) {
        int prevHoveredRow = hoveredRowIndex;
        std::string prevHoveredCategory = hoveredCategory;

        FindHoveredBar(mousePos);

        if (prevHoveredRow != hoveredRowIndex || prevHoveredCategory != hoveredCategory) {
            if (enableTooltips && hoveredRowIndex >= 0 && !hoveredCategory.empty()) {
                UpdateTooltip(mousePos);
            }
            RequestRedraw();
            return true;
        }

        return false;
    }

    bool UltraCanvasDivergingBarChart::HandleMouseClick(const Point2Di &mousePos) {
        FindHoveredBar(mousePos);

        if (hoveredRowIndex >= 0 && !hoveredCategory.empty()) {
            if (onBarClick) {
                onBarClick(hoveredRowIndex, hoveredCategory);
            }
            return true;
        }

        return false;
    }

    ChartDataBounds UltraCanvasDivergingBarChart::CalculateDataBounds() {
        ChartDataBounds bounds;
        maxNegativeValue = 0.0f;
        maxPositiveValue = 0.0f;

        if (divergingDataSource) {
            divergingDataSource->GetDataBounds(maxNegativeValue, maxPositiveValue);

            // Add to bounds for base class compatibility
            bounds.Expand(-maxNegativeValue, 0, 0);
            bounds.Expand(maxPositiveValue, 0, 0);
        }

        // Add 10% margin
        maxNegativeValue *= 1.1f;
        maxPositiveValue *= 1.1f;
        bounds.AddMargin(0.1);

        needsRecalculation = false;
        return bounds;
    }

    void UltraCanvasDivergingBarChart::RenderGrid(IRenderContext *ctx) {
        ctx->SetStrokeColor(gridColor);
        ctx->SetStrokeWidth(1.0);

        float centerX = cachedPlotArea.x + cachedPlotArea.width / 2;

        // Calculate nice intervals for grid lines
        float maxValue = std::max(maxNegativeValue, maxPositiveValue);
        float gridInterval = GetNiceRoundNumber(maxValue / 5.0f); // Aim for about 5 grid lines

        // Grid lines on negative side
        float currentValue = gridInterval;
        while (currentValue < maxNegativeValue) {
            float x = centerX - (currentValue / maxNegativeValue) * (cachedPlotArea.width / 2 - centerGap/2);
            ctx->DrawLine(x, cachedPlotArea.y, x, cachedPlotArea.y + cachedPlotArea.height);
            currentValue += gridInterval;
        }

        // Grid lines on positive side
        currentValue = gridInterval;
        while (currentValue < maxPositiveValue) {
            float x = centerX + (currentValue / maxPositiveValue) * (cachedPlotArea.width / 2 - centerGap/2);
            ctx->DrawLine(x, cachedPlotArea.y, x, cachedPlotArea.y + cachedPlotArea.height);
            currentValue += gridInterval;
        }

        // Draw horizontal grid lines for rows (optional)
        if (divergingDataSource && divergingDataSource->GetPointCount() > 0) {
            float rowHeight = cachedPlotArea.height / divergingDataSource->GetPointCount();
            ctx->SetStrokeColor(Color(gridColor.r, gridColor.g, gridColor.b, gridColor.a / 2)); // Lighter horizontal lines
            for (size_t i = 1; i < divergingDataSource->GetPointCount(); ++i) {
                float y = cachedPlotArea.y + i * rowHeight;
                ctx->DrawLine(cachedPlotArea.x, y, cachedPlotArea.x + cachedPlotArea.width, y);
            }
        }
    }

    void UltraCanvasDivergingBarChart::RenderCenterLine(IRenderContext *ctx) {
        float centerX = cachedPlotArea.x + cachedPlotArea.width / 2;

        ctx->SetStrokeColor(centerLineColor);
        ctx->SetStrokeWidth(centerLineWidth);
        ctx->DrawLine(centerX, cachedPlotArea.y, centerX, cachedPlotArea.y + cachedPlotArea.height);
    }

    void UltraCanvasDivergingBarChart::RenderPopulationPyramid(IRenderContext *ctx, float animScale) {
        if (!divergingDataSource) return;

        size_t pointCount = divergingDataSource->GetPointCount();
        if (pointCount == 0) return;

        float rowHeight = cachedPlotArea.height / pointCount;
        float actualBarHeight = rowHeight * barHeight;
        float centerX = cachedPlotArea.x + cachedPlotArea.width / 2;
        float availableWidth = (cachedPlotArea.width - centerGap) / 2;

        for (size_t rowIdx = 0; rowIdx < pointCount; ++rowIdx) {
            const auto& point = divergingDataSource->GetDivergingPoint(rowIdx);
            float y = cachedPlotArea.y + rowIdx * rowHeight + (rowHeight - actualBarHeight) / 2;

            // Render negative side (left) - stacked segments
            float negativeOffset = 0;
            for (const auto& category : categories) {
                if (!category.isPositive && point.categoryValues.count(category.name)) {
                    float value = point.categoryValues.at(category.name);
                    float barWidth = (value / maxNegativeValue) * availableWidth * animScale;
                    float x = centerX - centerGap/2 - negativeOffset - barWidth;

                    // Apply hover effect
                    Color fillColor = category.categoryColor;
                    if (hoveredRowIndex == static_cast<int>(rowIdx) && hoveredCategory == category.name) {
                        fillColor.r = std::min(255, fillColor.r + 30);
                        fillColor.g = std::min(255, fillColor.g + 30);
                        fillColor.b = std::min(255, fillColor.b + 30);
                    }

                    ctx->SetFillColor(fillColor);
                    ctx->FillRectangle(x, y, barWidth, actualBarHeight);

                    negativeOffset += barWidth;
                }
            }

            // Render positive side (right) - stacked segments
            float positiveOffset = centerGap/2;
            for (const auto& category : categories) {
                if (category.isPositive && point.categoryValues.count(category.name)) {
                    float value = point.categoryValues.at(category.name);
                    float barWidth = (value / maxPositiveValue) * availableWidth * animScale;
                    float x = centerX + positiveOffset;

                    // Apply hover effect
                    Color fillColor = category.categoryColor;
                    if (hoveredRowIndex == static_cast<int>(rowIdx) && hoveredCategory == category.name) {
                        fillColor.r = std::min(255, fillColor.r + 30);
                        fillColor.g = std::min(255, fillColor.g + 30);
                        fillColor.b = std::min(255, fillColor.b + 30);
                    }

                    ctx->SetFillColor(fillColor);
                    ctx->FillRectangle(x, y, barWidth, actualBarHeight);

                    positiveOffset += barWidth;
                }
            }
        }
    }

    void UltraCanvasDivergingBarChart::RenderLikertScale(IRenderContext *ctx, float animScale) {
        if (!divergingDataSource) return;

        size_t pointCount = divergingDataSource->GetPointCount();
        if (pointCount == 0) return;

        // Similar to population pyramid but with all categories shown as stacked segments
        float rowHeight = cachedPlotArea.height / pointCount;
        float actualBarHeight = rowHeight * barHeight;
        float centerX = cachedPlotArea.x + cachedPlotArea.width / 2;
        float availableWidth = (cachedPlotArea.width - centerGap) / 2;

        for (size_t rowIdx = 0; rowIdx < pointCount; ++rowIdx) {
            const auto& point = divergingDataSource->GetDivergingPoint(rowIdx);
            float y = cachedPlotArea.y + rowIdx * rowHeight + (rowHeight - actualBarHeight) / 2;
            float barWidth, value;

            // Calculate total for percentage
            float totalPos = 0;
            float totalNeg = 0;
            for (const auto& category : categories) {
                if (point.categoryValues.count(category.name)) {
                    value = std::abs(point.categoryValues.at(category.name));
                    if (category.isPositive) {
                        totalPos += value;
                    } else {
                        totalNeg += value;
                    }
                }
            }
            if (totalPos == 0 && totalNeg == 0) continue;

            // Render from center outward
            float leftOffset = centerX - centerGap/2;
            float rightOffset = centerX + centerGap/2;
            for (const auto& category : categories) {
                if (point.categoryValues.count(category.name)) {
                    value = std::abs(point.categoryValues.at(category.name));

                    Color fillColor = category.categoryColor;
                    if (hoveredRowIndex == static_cast<int>(rowIdx) && hoveredCategory == category.name) {
                        fillColor.r = std::min(255, fillColor.r + 30);
                        fillColor.g = std::min(255, fillColor.g + 30);
                        fillColor.b = std::min(255, fillColor.b + 30);
                    }

                    ctx->SetFillColor(fillColor);
                    if (category.isPositive) {
                        barWidth = (value / totalPos) * availableWidth * animScale;
                        // Draw on right side
                        ctx->FillRectangle(rightOffset, y, barWidth, actualBarHeight);
                        rightOffset += barWidth;
                    } else {
                        barWidth = (value / totalNeg) * availableWidth * animScale;
                        // Draw on left side
                        leftOffset -= barWidth;
                        ctx->FillRectangle(leftOffset, y, barWidth, actualBarHeight);
                    }
                }
            }
        }
    }

    void UltraCanvasDivergingBarChart::RenderTornadoChart(IRenderContext *ctx, float animScale) {
        if (!divergingDataSource) return;

        size_t pointCount = divergingDataSource->GetPointCount();
        if (pointCount == 0) return;

        // Single bars extending left or right from center
        float rowHeight = cachedPlotArea.height / pointCount;
        float actualBarHeight = rowHeight * barHeight;
        float centerX = cachedPlotArea.x + cachedPlotArea.width / 2;
        float maxWidth = (cachedPlotArea.width - centerGap) / 2;

        for (size_t rowIdx = 0; rowIdx < pointCount; ++rowIdx) {
            const auto& point = divergingDataSource->GetDivergingPoint(rowIdx);
            float y = cachedPlotArea.y + rowIdx * rowHeight + (rowHeight - actualBarHeight) / 2;

            // Calculate net value
            float posTotal = 0, negTotal = 0;
            for (const auto& [cat, val] : point.categoryValues) {
                bool isPositive = false;
                for (const auto& category : categories) {
                    if (category.name == cat) {
                        isPositive = category.isPositive;
                        break;
                    }
                }

                if (isPositive) {
                    posTotal += std::abs(val);
                } else {
                    negTotal += std::abs(val);
                }
            }

            float netValue = posTotal - negTotal;
            Color barColor = (netValue >= 0) ? Color(100, 150, 200, 255) : Color(200, 100, 100, 255);

            if (hoveredRowIndex == static_cast<int>(rowIdx)) {
                barColor.r = std::min(255, barColor.r + 30);
                barColor.g = std::min(255, barColor.g + 30);
                barColor.b = std::min(255, barColor.b + 30);
            }

            float barWidth = (std::abs(netValue) / std::max(maxPositiveValue, maxNegativeValue)) * maxWidth * animScale;

            ctx->SetFillColor(barColor);

            if (netValue < 0) {
                // Draw on left side
                ctx->FillRectangle(centerX - centerGap/2 - barWidth, y, barWidth, actualBarHeight);
            } else {
                // Draw on right side
                ctx->FillRectangle(centerX + centerGap/2, y, barWidth, actualBarHeight);
            }
        }
    }

    void UltraCanvasDivergingBarChart::RenderOpposingBars(IRenderContext *ctx, float animScale) {
        if (!divergingDataSource) return;

        size_t pointCount = divergingDataSource->GetPointCount();
        if (pointCount == 0) return;

        // Two separate bars meeting at center
        float rowHeight = cachedPlotArea.height / pointCount;
        float actualBarHeight = rowHeight * barHeight / 2; // Half height for each bar
        float centerX = cachedPlotArea.x + cachedPlotArea.width / 2;
        float maxWidth = (cachedPlotArea.width - centerGap) / 2;

        for (size_t rowIdx = 0; rowIdx < pointCount; ++rowIdx) {
            const auto& point = divergingDataSource->GetDivergingPoint(rowIdx);
            float y = cachedPlotArea.y + rowIdx * rowHeight + (rowHeight - actualBarHeight * 2) / 2;

            // Calculate totals
            float posTotal = 0, negTotal = 0;
            for (const auto& [cat, val] : point.categoryValues) {
                bool isPositive = false;
                for (const auto& category : categories) {
                    if (category.name == cat) {
                        isPositive = category.isPositive;
                        break;
                    }
                }

                if (isPositive) {
                    posTotal += std::abs(val);
                } else {
                    negTotal += std::abs(val);
                }
            }

            // Left bar (negative values)
            if (negTotal > 0) {
                float barWidth = (negTotal / maxNegativeValue) * maxWidth * animScale;
                Color leftColor = Color(200, 100, 100, 255);

                if (hoveredRowIndex == static_cast<int>(rowIdx) && hoveredCategory == "negative") {
                    leftColor.r = std::min(255, leftColor.r + 30);
                    leftColor.g = std::min(255, leftColor.g + 30);
                    leftColor.b = std::min(255, leftColor.b + 30);
                }

                ctx->SetFillColor(leftColor);
                ctx->FillRectangle(centerX - centerGap/2 - barWidth, y, barWidth, actualBarHeight);
            }

            // Right bar (positive values)
            if (posTotal > 0) {
                float barWidth = (posTotal / maxPositiveValue) * maxWidth * animScale;
                Color rightColor = Color(100, 150, 200, 255);

                if (hoveredRowIndex == static_cast<int>(rowIdx) && hoveredCategory == "positive") {
                    rightColor.r = std::min(255, rightColor.r + 30);
                    rightColor.g = std::min(255, rightColor.g + 30);
                    rightColor.b = std::min(255, rightColor.b + 30);
                }

                ctx->SetFillColor(rightColor);
                ctx->FillRectangle(centerX + centerGap/2, y + actualBarHeight, barWidth, actualBarHeight);
            }
        }
    }

    void UltraCanvasDivergingBarChart::RenderRowLabels(IRenderContext *ctx) {
        if (!divergingDataSource) return;

        TextStyle st = ctx->GetTextStyle();
        st.alignment = TextAlignment::Left;
        st.fontFamily = "Arial";
        st.fontSize = labelFontSize;
        st.textColor = labelColor;
        ctx->SetTextStyle(st);

        size_t pointCount = divergingDataSource->GetPointCount();
        if (pointCount == 0) return;

        float rowHeight = cachedPlotArea.height / pointCount;

        for (size_t rowIdx = 0; rowIdx < pointCount; ++rowIdx) {
            float y = cachedPlotArea.y + rowIdx * rowHeight - rowHeight / 2 + 2;
            const auto& point = divergingDataSource->GetDivergingPoint(rowIdx);

            // Draw label at center
            ctx->DrawText(point.rowLabel, cachedPlotArea.x + cachedPlotArea.width + 2, y);
        }
    }

    void UltraCanvasDivergingBarChart::RenderAxisLabels(IRenderContext *ctx) {
        ctx->SetFont("Arial", labelFontSize - 1);
        ctx->SetFillColor(Color(100, 100, 100, 255));

        float centerX = cachedPlotArea.x + cachedPlotArea.width / 2;
        float y = cachedPlotArea.y + cachedPlotArea.height + 2;

        // Calculate nice round numbers for axis labels
        float maxNegativeRounded = GetNiceRoundNumber(maxNegativeValue);
        float maxPositiveRounded = GetNiceRoundNumber(maxPositiveValue);

        // Draw scale labels based on actual data range
        char leftLabel[32], rightLabel[32];

        // Format labels based on magnitude
        if (maxNegativeRounded >= 1000) {
            snprintf(leftLabel, sizeof(leftLabel), "-%.0fk", maxNegativeRounded / 1000);
        } else if (maxNegativeRounded >= 10) {
            snprintf(leftLabel, sizeof(leftLabel), "-%.0f", maxNegativeRounded);
        } else {
            snprintf(leftLabel, sizeof(leftLabel), "-%.1f", maxNegativeRounded);
        }

        if (maxPositiveRounded >= 1000) {
            snprintf(rightLabel, sizeof(rightLabel), "%.0fk", maxPositiveRounded / 1000);
        } else if (maxPositiveRounded >= 10) {
            snprintf(rightLabel, sizeof(rightLabel), "%.0f", maxPositiveRounded);
        } else {
            snprintf(rightLabel, sizeof(rightLabel), "%.1f", maxPositiveRounded);
        }

        // Draw the labels
        TextStyle st = ctx->GetTextStyle();
        st.alignment = TextAlignment::Left;
        ctx->SetTextStyle(st);
        ctx->DrawTextInRect(leftLabel, cachedPlotArea.x, y, cachedPlotArea.width / 2 - 10, 20);
        st.alignment = TextAlignment::Center;
        ctx->SetTextStyle(st);
        ctx->DrawTextInRect("0", centerX - 10, y, 20, 20);
        st.alignment = TextAlignment::Right;
        ctx->SetTextStyle(st);
        ctx->DrawTextInRect(rightLabel, centerX + 10, y, cachedPlotArea.width / 2 - 10, 20);

        // Draw intermediate labels for better scale understanding
        int numIntermediateLabels = 2;
        for (int i = 1; i <= numIntermediateLabels; ++i) {
            float fraction = i / (float)(numIntermediateLabels + 1);

            // Left side intermediate labels
            float leftValue = maxNegativeRounded * fraction;
            float leftX = centerX - (cachedPlotArea.width / 2) * fraction;
            char leftIntermediate[32];
            if (leftValue >= 1000) {
                snprintf(leftIntermediate, sizeof(leftIntermediate), "-%.0fk", leftValue / 1000);
            } else {
                snprintf(leftIntermediate, sizeof(leftIntermediate), "-%.0f", leftValue);
            }
            ctx->DrawTextInRect(leftIntermediate, leftX, y, cachedPlotArea.width / 2, 20);

            // Right side intermediate labels
            float rightValue = maxPositiveRounded * fraction;
            float rightX = centerX + (cachedPlotArea.width / 2) * fraction;
            char rightIntermediate[32];
            if (rightValue >= 1000) {
                snprintf(rightIntermediate, sizeof(rightIntermediate), "%.0fk", rightValue / 1000);
            } else {
                snprintf(rightIntermediate, sizeof(rightIntermediate), "%.0f", rightValue);
            }
            ctx->DrawTextInRect(rightIntermediate, rightX, y, cachedPlotArea.width / 2, 20);
        }

        // Draw axis title
        ctx->DrawTextInRect("Frequency", centerX, y + 20, cachedPlotArea.width / 2, 20);
    }

    float UltraCanvasDivergingBarChart::GetNiceRoundNumber(float value) {
        if (value == 0) return 0;

        // Find the order of magnitude
        float magnitude = std::pow(10, std::floor(std::log10(value)));
        float normalized = value / magnitude;

        // Round to nice numbers (1, 2, 2.5, 5, 10)
        float rounded;
        if (normalized <= 1.0f) {
            rounded = 1.0f;
        } else if (normalized <= 2.0f) {
            rounded = 2.0f;
        } else if (normalized <= 2.5f) {
            rounded = 2.5f;
        } else if (normalized <= 5.0f) {
            rounded = 5.0f;
        } else {
            rounded = 10.0f;
        }

        return rounded * magnitude;
    }

    void UltraCanvasDivergingBarChart::FindHoveredBar(const Point2Di &mousePos) {
        hoveredRowIndex = -1;
        hoveredCategory = "";

        if (!cachedPlotArea.Contains(mousePos.x, mousePos.y) || !divergingDataSource) {
            return;
        }

        size_t pointCount = divergingDataSource->GetPointCount();
        if (pointCount == 0) return;

        float rowHeight = cachedPlotArea.height / pointCount;
        hoveredRowIndex = (mousePos.y - cachedPlotArea.y) / rowHeight;

        if (hoveredRowIndex < 0 || hoveredRowIndex >= static_cast<int>(pointCount)) {
            hoveredRowIndex = -1;
            return;
        }

        // Determine which category based on x position
        float centerX = cachedPlotArea.x + cachedPlotArea.width / 2;
        float relativeX = mousePos.x - centerX;

        // Simple detection - could be improved for stacked segments
        if (std::abs(relativeX) < centerGap / 2) {
            // In center gap
            hoveredCategory = "";
        } else if (relativeX < 0) {
            // On negative side
            for (const auto& cat : categories) {
                if (!cat.isPositive) {
                    hoveredCategory = cat.name;
                    break;
                }
            }
        } else {
            // On positive side
            for (const auto& cat : categories) {
                if (cat.isPositive) {
                    hoveredCategory = cat.name;
                    break;
                }
            }
        }
    }

    void UltraCanvasDivergingBarChart::UpdateTooltip(const Point2Di &mousePos) {
        if (hoveredRowIndex < 0 || hoveredCategory.empty() || !divergingDataSource) return;

        const auto& point = divergingDataSource->GetDivergingPoint(hoveredRowIndex);
        std::string tooltipText = point.rowLabel + "\n" + hoveredCategory + ": ";

        if (point.categoryValues.count(hoveredCategory)) {
            float value = point.categoryValues.at(hoveredCategory);
            tooltipText += std::to_string(static_cast<int>(value));
        }

        // Show tooltip using tooltip manager
        UltraCanvasTooltipManager::UpdateAndShowTooltip(GetWindow(), tooltipText, mousePos);
    }

    float UltraCanvasDivergingBarChart::GetAnimationScale() {
        if (!enableAnimation) return 1.0f;

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - animationStartTime).count();

        const float animationDuration = 600.0f;
        float progress = std::min(1.0f, elapsed / animationDuration);

        // Ease-out cubic
        return 1.0f - std::pow(1.0f - progress, 3.0f);
    }
}