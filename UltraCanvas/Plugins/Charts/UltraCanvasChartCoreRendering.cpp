// UltraCanvasChartCoreRendering.cpp
// Core rendering engine for UltraCanvas charts
// Version: 1.0.1
// Last Modified: 2025-09-10
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasChartRenderer.h"
#include "Plugins/Charts/UltraCanvasChartStructures.h"
#include "UltraCanvasRenderContext.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

// =============================================================================
// CORE CALCULATION FUNCTIONS
// =============================================================================

    PlotArea CalculatePlotArea(const ChartConfiguration& config, int width, int height) {
        PlotArea area;

        // Reserve space for titles
        int topMargin = 20;
        if (!config.title.empty()) topMargin += 30;
        if (!config.subtitle.empty()) topMargin += 20;

        // Reserve space for axes labels and ticks
        int leftMargin = 80;   // Y-axis labels and title
        int rightMargin = 20;  // General margin
        int bottomMargin = 60; // X-axis labels and title

        // Adjust for legend
        if (config.legend.position == LegendSettings::Position::Right) {
            rightMargin += 150;
        } else if (config.legend.position == LegendSettings::Position::Left) {
            leftMargin += 150;
        } else if (config.legend.position == LegendSettings::Position::Bottom) {
            bottomMargin += 40;
        } else if (config.legend.position == LegendSettings::Position::Top) {
            topMargin += 40;
        }

        // Calculate final plot area
        area.x = leftMargin;
        area.y = topMargin;
        area.width = std::max(100, width - leftMargin - rightMargin);
        area.height = std::max(100, height - topMargin - bottomMargin);

        return area;
    }

    DataBounds CalculateDataBounds(IChartDataSource* dataSource, const ChartConfiguration& config) {
        if (!dataSource || dataSource->GetPointCount() == 0) {
            return DataBounds(0, 1, 0, 1);
        }

        DataBounds bounds;

        // Initialize with first point
        auto firstPoint = dataSource->GetPoint(0);
        bounds.Expand(firstPoint.x, firstPoint.y, firstPoint.z);

        // Scan all points to find bounds
        size_t pointCount = dataSource->GetPointCount();
        for (size_t i = 1; i < pointCount; ++i) {
            auto point = dataSource->GetPoint(i);
            bounds.Expand(point.x, point.y, point.z);
        }

        // Override with manual axis settings if specified
        if (!config.xAxis.autoScale) {
            bounds.minX = config.xAxis.minValue;
            bounds.maxX = config.xAxis.maxValue;
        }

        if (!config.yAxis.autoScale) {
            bounds.minY = config.yAxis.minValue;
            bounds.maxY = config.yAxis.maxValue;
        }

        // Add margin for better visualization
        if (config.xAxis.autoScale) {
            bounds.AddMargin(0.05); // 5% margin
        }

        return bounds;
    }

// =============================================================================
// BACKGROUND AND GRID RENDERING
// =============================================================================

    void DrawChartBackground(const ChartConfiguration& config, const PlotArea& plotArea, IRenderContext* ctx) {
        if (!ctx) return;

        // Draw overall background
        ctx->SetFillColor(Color::FromARGB(config.backgroundColor));
        ctx->FillRectangle(0, 0, plotArea.GetRight() + 50, plotArea.GetBottom() + 50);

        // Draw plot area background
        ctx->SetFillColor(Color::FromARGB(config.plotAreaColor));
        ctx->FillRectangle(plotArea.ToRect2D());

        // Draw plot area border
        if (config.plotAreaBorderWidth > 0) {
            ctx->SetStrokeColor(Color::FromARGB(config.plotAreaBorderColor));
            ctx->SetStrokeWidth(config.plotAreaBorderWidth);
            ctx->DrawRectangle(plotArea.ToRect2D());
        }
    }

    void DrawGrid(const ChartConfiguration& config, const PlotArea& plotArea, const DataBounds& bounds, IRenderContext* ctx) {
        if (!ctx) return;

        ChartCoordinateTransform transform(plotArea, bounds);

        // Draw X-axis grid
        if (config.xAxis.showGrid) {
            ctx->SetStrokeColor(Color::FromARGB(config.xAxis.gridColor));
            ctx->SetStrokeWidth(config.xAxis.gridLineWidth);

            auto xTicks = ChartRenderingHelpers::CalculateAxisTicks(bounds.minX, bounds.maxX, 8);
            for (double tick : xTicks) {
                float x = static_cast<float>(transform.DataToScreenX(tick));
                ctx->DrawLine(x, static_cast<float>(plotArea.y),
                              x, static_cast<float>(plotArea.GetBottom()));
            }
        }

        // Draw Y-axis grid
        if (config.yAxis.showGrid) {
            ctx->SetStrokeColor(Color::FromARGB(config.yAxis.gridColor));
            ctx->SetStrokeWidth(config.yAxis.gridLineWidth);

            auto yTicks = ChartRenderingHelpers::CalculateAxisTicks(bounds.minY, bounds.maxY, 6);
            for (double tick : yTicks) {
                float y = static_cast<float>(transform.DataToScreenY(tick));
                ctx->DrawLine(static_cast<float>(plotArea.x), y,
                              static_cast<float>(plotArea.GetRight()), y);
            }
        }
    }

    void DrawAxes(const ChartConfiguration& config, const PlotArea& plotArea, const DataBounds& bounds, IRenderContext* ctx) {
        if (!ctx) return;

        ChartCoordinateTransform transform(plotArea, bounds);

        // Draw X-axis
        ctx->SetStrokeColor(Color::FromARGB(config.xAxis.axisColor));
        ctx->SetStrokeWidth(config.xAxis.axisLineWidth);
        ctx->DrawLine(static_cast<float>(plotArea.x), static_cast<float>(plotArea.GetBottom()),
                      static_cast<float>(plotArea.GetRight()), static_cast<float>(plotArea.GetBottom()));

        // Draw Y-axis
        ctx->SetStrokeColor(Color::FromARGB(config.yAxis.axisColor));
        ctx->SetStrokeWidth(config.yAxis.axisLineWidth);
        ctx->DrawLine(static_cast<float>(plotArea.x), static_cast<float>(plotArea.y),
                      static_cast<float>(plotArea.x), static_cast<float>(plotArea.GetBottom()));

        // Draw X-axis ticks and labels
        if (config.xAxis.showTicks) {
            ctx->SetFont(config.xAxis.labelStyle.fontFamily, config.xAxis.labelStyle.fontSize);
            ctx->SetTextColor(Color::FromARGB(config.xAxis.labelStyle.color));

            auto xTicks = ChartRenderingHelpers::CalculateAxisTicks(bounds.minX, bounds.maxX, 8);
            for (double tick : xTicks) {
                float x = static_cast<float>(transform.DataToScreenX(tick));

                // Draw tick mark
                ctx->DrawLine(x, static_cast<float>(plotArea.GetBottom()),
                              x, static_cast<float>(plotArea.GetBottom() + 5));

                // Draw label
                std::string label = ChartRenderingHelpers::FormatAxisLabel(tick);
                auto textSize = ChartRenderingHelpers::MeasureText(ctx, label,
                                                                   config.xAxis.labelStyle.fontFamily, config.xAxis.labelStyle.fontSize);
                ctx->DrawText(label, x - textSize.x / 2, static_cast<float>(plotArea.GetBottom() + 20));
            }

            // Draw X-axis title
            if (!config.xAxis.title.empty()) {
                ctx->SetFont(config.xAxis.titleStyle.fontFamily, config.xAxis.titleStyle.fontSize);
                ctx->SetTextColor(Color::FromARGB(config.xAxis.titleStyle.color));

                auto titleSize = ChartRenderingHelpers::MeasureText(ctx, config.xAxis.title,
                                                                    config.xAxis.titleStyle.fontFamily, config.xAxis.titleStyle.fontSize);
                float titleX = plotArea.x + plotArea.width / 2.0f - titleSize.x / 2.0f;
                float titleY = plotArea.GetBottom() + 45;
                ctx->DrawText(config.xAxis.title, titleX, titleY);
            }
        }

        // Draw Y-axis ticks and labels
        if (config.yAxis.showTicks) {
            ctx->SetFont(config.yAxis.labelStyle.fontFamily, config.yAxis.labelStyle.fontSize);
            ctx->SetTextColor(Color::FromARGB(config.yAxis.labelStyle.color));

            auto yTicks = ChartRenderingHelpers::CalculateAxisTicks(bounds.minY, bounds.maxY, 6);
            for (double tick : yTicks) {
                float y = transform.DataToScreenY(tick);

                // Draw tick mark
                ctx->DrawLine(static_cast<float>(plotArea.x - 5), y,
                              static_cast<float>(plotArea.x), y);

                // Draw label
                std::string label = ChartRenderingHelpers::FormatAxisLabel(tick);
                auto textSize = ChartRenderingHelpers::MeasureText(ctx, label,
                                                                   config.yAxis.labelStyle.fontFamily, config.yAxis.labelStyle.fontSize);
                ctx->DrawText(label, plotArea.x - textSize.x - 10, y + textSize.y / 2);
            }

            // Draw Y-axis title (rotated)
            if (!config.yAxis.title.empty()) {
                ctx->SetFont(config.yAxis.titleStyle.fontFamily, config.yAxis.titleStyle.fontSize);
                ctx->SetTextColor(Color::FromARGB(config.yAxis.titleStyle.color));

                // Note: Text rotation would need to be implemented in IRenderContext
                // For now, draw vertically positioned
                float titleX = 15;
                float titleY = plotArea.y + plotArea.height / 2.0f;
                ctx->DrawText(config.yAxis.title, titleX, titleY);
            }
        }
    }

    void DrawAxisHighlights(const ChartConfiguration& config, const PlotArea& plotArea, const DataBounds& bounds, IRenderContext* ctx) {
        if (!ctx) return;

        ChartCoordinateTransform transform(plotArea, bounds);

        // Draw X-axis highlights
        for (const auto& highlight : config.xAxis.highlights) {
            float x = transform.DataToScreenX(highlight.position);

            // Set line style
            ctx->SetStrokeColor(Color::FromARGB(highlight.color));
            ctx->SetStrokeWidth(highlight.lineWidth);

            // Draw highlight line
            ctx->DrawLine(x, static_cast<float>(plotArea.y),
                          x, static_cast<float>(plotArea.GetBottom()));

            // Draw label if provided
            if (!highlight.label.empty()) {
                ctx->SetTextColor(Color::FromARGB(highlight.color));
                ctx->SetFont("Arial", 10.0f);
                ctx->DrawText(highlight.label, x + 5, static_cast<float>(plotArea.y + 15));
            }
        }

        // Draw Y-axis highlights
        for (const auto& highlight : config.yAxis.highlights) {
            float y = transform.DataToScreenY(highlight.position);

            // Set line style
            ctx->SetStrokeColor(Color::FromARGB(highlight.color));
            ctx->SetStrokeWidth(highlight.lineWidth);

            // Draw highlight line
            ctx->DrawLine(static_cast<float>(plotArea.x), y,
                          static_cast<float>(plotArea.GetRight()), y);

            // Draw label if provided
            if (!highlight.label.empty()) {
                ctx->SetTextColor(Color::FromARGB(highlight.color));
                ctx->SetFont("Arial", 10.0f);
                auto textSize = ChartRenderingHelpers::MeasureText(ctx, highlight.label, "Arial", 10.0f);
                ctx->DrawText(highlight.label, plotArea.GetRight() - textSize.x - 5, y - 5);
            }
        }
    }

    void DrawTitles(const ChartConfiguration& config, int width, int height, IRenderContext* ctx) {
        if (!ctx) return;

        float currentY = 10;

        // Draw main title
        if (!config.title.empty()) {
            ctx->SetFont(config.titleStyle.fontFamily, config.titleStyle.fontSize);
            ctx->SetTextColor(Color::FromARGB(config.titleStyle.color));

            auto titleSize = ChartRenderingHelpers::MeasureText(ctx, config.title,
                                                                config.titleStyle.fontFamily, config.titleStyle.fontSize);

            float titleX = width / 2.0f - titleSize.x / 2.0f;
            if (config.titleStyle.alignment == TextSettings::Alignment::Left) {
                titleX = 20;
            } else if (config.titleStyle.alignment == TextSettings::Alignment::Right) {
                titleX = width - titleSize.x - 20;
            }

            ctx->DrawText(config.title, titleX, currentY + titleSize.y);
            currentY += titleSize.y + 5;
        }

        // Draw subtitle
        if (!config.subtitle.empty()) {
            ctx->SetFont(config.subtitleStyle.fontFamily, config.subtitleStyle.fontSize);
            ctx->SetTextColor(Color::FromARGB(config.subtitleStyle.color));

            auto subtitleSize = ChartRenderingHelpers::MeasureText(ctx, config.subtitle,
                                                                   config.subtitleStyle.fontFamily, config.subtitleStyle.fontSize);

            float subtitleX = width / 2.0f - subtitleSize.x / 2.0f;
            if (config.subtitleStyle.alignment == TextSettings::Alignment::Left) {
                subtitleX = 20;
            } else if (config.subtitleStyle.alignment == TextSettings::Alignment::Right) {
                subtitleX = width - subtitleSize.x - 20;
            }

            ctx->DrawText(config.subtitle, subtitleX, currentY + subtitleSize.y);
            currentY += subtitleSize.y + 5;
        }

        // Draw comment text
        if (!config.commentText.empty()) {
            ctx->SetFont(config.commentStyle.fontFamily, config.commentStyle.fontSize);
            ctx->SetTextColor(Color::FromARGB(config.commentStyle.color));

            auto commentSize = ChartRenderingHelpers::MeasureText(ctx, config.commentText,
                                                                  config.commentStyle.fontFamily, config.commentStyle.fontSize);

            float commentX = width - commentSize.x - 20;
            float commentY = height - 10;

            if (config.commentStyle.alignment == TextSettings::Alignment::Left) {
                commentX = 20;
            } else if (config.commentStyle.alignment == TextSettings::Alignment::Center) {
                commentX = width / 2.0f - commentSize.x / 2.0f;
            }

            ctx->DrawText(config.commentText, commentX, commentY);
        }
    }

    void DrawLegend(const ChartConfiguration& config, const PlotArea& plotArea, IRenderInterface* ctx) {
        if (!ctx || !config.legend.showBackground) return;

        // Calculate legend size and position
        std::vector<std::string> legendItems;

        // Add data series to legend
        if (config.dataSource && config.dataSource->GetPointCount() > 0) {
            legendItems.push_back("Data Series");
        }

        // Add trend lines to legend
        for (const auto& trendLine : config.trendLines) {
            if (!trendLine.label.empty()) {
                legendItems.push_back(trendLine.label);
            }
        }

        if (legendItems.empty()) return;

        // Calculate legend dimensions
        float maxItemWidth = 0;
        float itemHeight = config.legend.fontSize + 4;

        ctx->SetFont(config.legend.fontFamily, config.legend.fontSize);
        for (const auto& item : legendItems) {
            auto textSize = ChartRenderingHelpers::MeasureText(ctx, item,
                                                               config.legend.fontFamily, config.legend.fontSize);
            maxItemWidth = std::max(maxItemWidth, textSize.x);
        }

        float legendWidth = maxItemWidth + 30; // 20 for color box + 10 padding
        float legendHeight = legendItems.size() * itemHeight + 10;

        // Calculate legend position
        float legendX, legendY;

        switch (config.legend.position) {
            case LegendSettings::Position::TopLeft:
                legendX = plotArea.x + config.legend.marginX;
                legendY = plotArea.y + config.legend.marginY;
                break;
            case LegendSettings::Position::TopRight:
                legendX = plotArea.GetRight() - legendWidth - config.legend.marginX;
                legendY = plotArea.y + config.legend.marginY;
                break;
            case LegendSettings::Position::BottomLeft:
                legendX = plotArea.x + config.legend.marginX;
                legendY = plotArea.GetBottom() - legendHeight - config.legend.marginY;
                break;
            case LegendSettings::Position::BottomRight:
                legendX = plotArea.GetRight() - legendWidth - config.legend.marginX;
                legendY = plotArea.GetBottom() - legendHeight - config.legend.marginY;
                break;
            default:
                legendX = plotArea.GetRight() - legendWidth - config.legend.marginX;
                legendY = plotArea.y + config.legend.marginY;
                break;
        }

        // Draw legend background
        if (config.legend.showBackground) {
            ctx->SetFillColor(Color::FromARGB(config.legend.backgroundColor));

            if (config.legend.style == LegendSettings::Style::Rounded) {
                ctx->FillRoundedRectangle(legendX, legendY, legendWidth, legendHeight, 5.0f);
            } else {
                ctx->FillRectangle(legendX, legendY, legendWidth, legendHeight);
            }
        }

        // Draw legend border
        if (config.legend.showBorder) {
            ctx->SetStrokeColor(Color::FromARGB(config.legend.borderColor));
            ctx->SetStrokeWidth(config.legend.borderWidth);

            if (config.legend.style == LegendSettings::Style::Rounded) {
                ctx->DrawRoundedRectangle(legendX, legendY, legendWidth, legendHeight, 5.0f);
            } else {
                ctx->DrawRectangle(legendX, legendY, legendWidth, legendHeight);
            }
        }

        // Draw legend items
        ctx->SetTextColor(Color::FromARGB(config.legend.textColor));
        ctx->SetFont(config.legend.fontFamily, config.legend.fontSize);

        auto colors = ChartRenderingHelpers::GenerateColorPalette(static_cast<int>(legendItems.size()));

        for (size_t i = 0; i < legendItems.size(); ++i) {
            float itemY = legendY + 5 + i * itemHeight;

            // Draw color box
            ctx->SetFillColor(colors[i]);
            ctx->FillRectangle(legendX + 5, itemY + 2, 15, itemHeight - 4);

            // Draw item text
            ctx->DrawText(legendItems[i], legendX + 25, itemY + itemHeight - 2);
        }
    }

// =============================================================================
// TREND LINE RENDERING
// =============================================================================

    void DrawTrendLine(const ChartConfiguration& config, const TrendLine& trendLine,
                       const PlotArea& plotArea, const DataBounds& bounds, IRenderInterface* ctx) {
        if (!ctx || !config.dataSource || config.dataSource->GetPointCount() < 2) return;

        ChartCoordinateTransform transform(plotArea, bounds);

        ctx->SetStrokeColor(Color::FromARGB(trendLine.color));
        ctx->SetStrokeWidth(trendLine.lineWidth);

        switch (trendLine.type) {
            case TrendLine::Type::Linear: {
                // Calculate linear regression
                double sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;
                size_t n = config.dataSource->GetPointCount();

                for (size_t i = 0; i < n; ++i) {
                    auto point = config.dataSource->GetPoint(i);
                    sumX += point.x;
                    sumY += point.y;
                    sumXY += point.x * point.y;
                    sumX2 += point.x * point.x;
                }

                double slope = (n * sumXY - sumX * sumY) / (n * sumX2 - sumX * sumX);
                double intercept = (sumY - slope * sumX) / n;

                // Draw trend line
                float startX = transform.DataToScreenX(bounds.minX);
                float startY = transform.DataToScreenY(slope * bounds.minX + intercept);
                float endX = transform.DataToScreenX(bounds.maxX);
                float endY = transform.DataToScreenY(slope * bounds.maxX + intercept);

                ctx->DrawLine(startX, startY, endX, endY);
                break;
            }

            case TrendLine::Type::MovingAverage: {
                int period = trendLine.movingAveragePeriod;
                if (period <= 1) period = 10;

                std::vector<Point2D> maPoints;

                for (size_t i = period - 1; i < config.dataSource->GetPointCount(); ++i) {
                    double sum = 0;
                    for (int j = 0; j < period; ++j) {
                        sum += config.dataSource->GetPoint(i - j).y;
                    }
                    double average = sum / period;

                    auto point = config.dataSource->GetPoint(i);
                    Point2D screenPoint = transform.DataToScreen(point.x, average);
                    maPoints.push_back(screenPoint);
                }

                // Draw moving average line
                for (size_t i = 1; i < maPoints.size(); ++i) {
                    ctx->DrawLine(maPoints[i-1], maPoints[i]);
                }
                break;
            }

            default:
                // Other trend line types would be implemented here
                break;
        }
    }

} // namespace UltraCanvas