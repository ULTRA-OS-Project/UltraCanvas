// Plugins/Charts/UltraCanvasMekkoChart.cpp
// Mekko (Marimekko / mosaic) chart element implementation
// Version: 1.0.0
// Last Modified: 2026-07-28
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasMekkoChart.h"
#include "UltraCanvasTooltipManager.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace UltraCanvas {

// =============================================================================
// MEKKO CHART DATA VECTOR
// =============================================================================

    size_t MekkoChartDataVector::AddSeries(const std::string& name, const Color& color) {
        series.emplace_back(name, color);
        size_t seriesIndex = series.size() - 1;
        for (auto& column : columns) {
            column.values.resize(series.size(), 0.0);
        }
        return seriesIndex;
    }

    const MekkoChartSeries& MekkoChartDataVector::GetSeries(size_t index) const {
        static MekkoChartSeries dummy("", Colors::Transparent);
        return (index < series.size()) ? series[index] : dummy;
    }

    void MekkoChartDataVector::SetSeriesColor(size_t index, const Color& color) {
        if (index < series.size()) {
            series[index].color = color;
        }
    }

    int MekkoChartDataVector::FindSeries(const std::string& name) const {
        for (size_t i = 0; i < series.size(); ++i) {
            if (series[i].name == name) return static_cast<int>(i);
        }
        return -1;
    }

    size_t MekkoChartDataVector::AddColumn(const std::string& label, const std::vector<double>& values) {
        columns.emplace_back(label, values);
        columns.back().values.resize(series.size(), 0.0);
        return columns.size() - 1;
    }

    const MekkoChartColumn& MekkoChartDataVector::GetColumn(size_t index) const {
        static MekkoChartColumn dummy("");
        return (index < columns.size()) ? columns[index] : dummy;
    }

    int MekkoChartDataVector::FindColumn(const std::string& label) const {
        for (size_t i = 0; i < columns.size(); ++i) {
            if (columns[i].label == label) return static_cast<int>(i);
        }
        return -1;
    }

    void MekkoChartDataVector::SetValue(size_t columnIndex, size_t seriesIndex, double value) {
        if (columnIndex >= columns.size() || seriesIndex >= series.size()) return;
        columns[columnIndex].values.resize(series.size(), 0.0);
        columns[columnIndex].values[seriesIndex] = value;
    }

    void MekkoChartDataVector::SetValue(const std::string& columnLabel, const std::string& seriesName, double value) {
        int columnIndex = FindColumn(columnLabel);
        int seriesIndex = FindSeries(seriesName);
        if (columnIndex >= 0 && seriesIndex >= 0) {
            SetValue(static_cast<size_t>(columnIndex), static_cast<size_t>(seriesIndex), value);
        }
    }

    double MekkoChartDataVector::GetValue(size_t columnIndex, size_t seriesIndex) const {
        if (columnIndex >= columns.size()) return 0.0;
        const auto& values = columns[columnIndex].values;
        return (seriesIndex < values.size()) ? values[seriesIndex] : 0.0;
    }

    void MekkoChartDataVector::SetColumnCategory(size_t columnIndex, const std::string& category) {
        if (columnIndex < columns.size()) {
            columns[columnIndex].category = category;
        }
    }

    double MekkoChartDataVector::GetColumnTotal(size_t columnIndex) const {
        return (columnIndex < columns.size()) ? columns[columnIndex].GetTotal() : 0.0;
    }

    double MekkoChartDataVector::GetSeriesTotal(size_t seriesIndex) const {
        double total = 0.0;
        for (const auto& column : columns) {
            if (seriesIndex < column.values.size() && column.values[seriesIndex] > 0) {
                total += column.values[seriesIndex];
            }
        }
        return total;
    }

    double MekkoChartDataVector::GetGrandTotal() const {
        double total = 0.0;
        for (const auto& column : columns) {
            total += column.GetTotal();
        }
        return total;
    }

    ChartDataPoint MekkoChartDataVector::GetPoint(size_t index) {
        if (index >= columns.size()) return ChartDataPoint(0, 0);
        const auto& column = columns[index];
        double total = column.GetTotal();
        return ChartDataPoint(static_cast<double>(index), total, 0.0, column.label, total);
    }

    void MekkoChartDataVector::LoadFromCSV(const std::string& filePath) {
        std::ifstream file(filePath);
        if (!file.is_open()) return;

        ClearData();

        std::string line;
        bool isHeader = true;
        while (std::getline(file, line)) {
            if (line.empty()) continue;

            std::stringstream ss(line);
            std::string cell;
            std::vector<std::string> cells;
            while (std::getline(ss, cell, ',')) {
                cells.push_back(cell);
            }
            if (cells.empty()) continue;

            if (isHeader) {
                // First cell is the column-label header, the rest are series names
                for (size_t i = 1; i < cells.size(); ++i) {
                    AddSeries(cells[i]);
                }
                isHeader = false;
            } else {
                std::vector<double> values;
                for (size_t i = 1; i < cells.size(); ++i) {
                    try {
                        values.push_back(std::stod(cells[i]));
                    } catch (...) {
                        values.push_back(0.0);
                    }
                }
                AddColumn(cells[0], values);
            }
        }
    }

// =============================================================================
// DEFAULT PALETTE
// =============================================================================

    const std::vector<Color>& UltraCanvasMekkoChartElement::DefaultPalette() {
        static const std::vector<Color> palette = {
                Color(25, 60, 130, 255),     // Deep blue
                Color(235, 75, 105, 255),    // Rose red
                Color(65, 145, 225, 255),    // Sky blue
                Color(250, 190, 40, 255),    // Amber
                Color(45, 165, 130, 255),    // Teal green
                Color(155, 100, 220, 255),   // Purple
                Color(240, 130, 55, 255),    // Orange
                Color(110, 190, 70, 255),    // Green
                Color(200, 60, 170, 255),    // Magenta
                Color(90, 110, 130, 255),    // Slate gray
                Color(0, 170, 200, 255),     // Cyan
                Color(160, 120, 80, 255)     // Brown
        };
        return palette;
    }

// =============================================================================
// LAYOUT CALCULATION
// =============================================================================

    ChartPlotArea UltraCanvasMekkoChartElement::CalculatePlotArea() {
        int marginLeft = showYAxisLabels ? 48 : 12;
        int marginRight = 15;
        int marginTop = chartTitle.empty() ? 12 : 30;
        int marginBottom = 12;

        if (columnHeaderMode != ColumnHeaderMode::NoHeader) {
            marginTop += 18;
        }
        if (showColumnLabels) {
            marginBottom += 18;
        }
        if (showCumulativeXAxis) {
            marginBottom += 16;
        }

        switch (legendPosition) {
            case LegendPosition::Right:
                marginRight += legendWidth;
                break;
            case LegendPosition::Bottom:
                marginBottom += 26;
                break;
            case LegendPosition::Top:
                marginTop += 26;
                break;
            case LegendPosition::Hidden:
            default:
                break;
        }

        return ChartPlotArea(
                marginLeft,
                marginTop,
                std::max(10.0f, static_cast<float>(GetWidth() - marginLeft - marginRight)),
                std::max(10.0f, static_cast<float>(GetHeight() - marginTop - marginBottom))
        );
    }

    ChartDataBounds UltraCanvasMekkoChartElement::CalculateDataBounds() {
        auto data = GetMekkoDataSource();
        if (!data || data->GetColumnCount() == 0) {
            return ChartDataBounds();
        }

        double maxY;
        if (mekkoMode == MekkoMode::Marimekko) {
            maxY = 100.0;
        } else {
            maxY = 0.0;
            for (size_t i = 0; i < data->GetColumnCount(); ++i) {
                maxY = std::max(maxY, data->GetColumnTotal(i));
            }
            if (maxY <= 0.0) maxY = 1.0;
        }

        return ChartDataBounds(0.0, data->GetGrandTotal(), 0.0, maxY);
    }

    void UltraCanvasMekkoChartElement::UpdateRenderingCache() {
        bool needsRebuild = !cacheValid || !renderCache.isValid;
        UltraCanvasChartElementBase::UpdateRenderingCache();

        if (needsRebuild) {
            auto data = GetMekkoDataSource();
            renderCache.columnOrder.clear();
            renderCache.columnX.clear();
            renderCache.columnWidth.clear();
            renderCache.segments.clear();
            renderCache.legendItemRects.clear();
            renderCache.grandTotal = 0.0;
            renderCache.maxColumnTotal = 0.0;

            if (data && data->GetColumnCount() > 0) {
                BuildColumnOrder(data);
                BuildSegmentRects(data);
            }
            renderCache.isValid = true;
        }
    }

    void UltraCanvasMekkoChartElement::BuildColumnOrder(std::shared_ptr<MekkoChartDataVector>& data) {
        size_t columnCount = data->GetColumnCount();
        renderCache.columnOrder.resize(columnCount);
        for (size_t i = 0; i < columnCount; ++i) {
            renderCache.columnOrder[i] = i;
        }

        switch (columnSortMode) {
            case ColumnSortMode::TotalDescending:
                std::stable_sort(renderCache.columnOrder.begin(), renderCache.columnOrder.end(),
                                 [&data](size_t a, size_t b) {
                                     return data->GetColumnTotal(a) > data->GetColumnTotal(b);
                                 });
                break;
            case ColumnSortMode::TotalAscending:
                std::stable_sort(renderCache.columnOrder.begin(), renderCache.columnOrder.end(),
                                 [&data](size_t a, size_t b) {
                                     return data->GetColumnTotal(a) < data->GetColumnTotal(b);
                                 });
                break;
            case ColumnSortMode::Alphabetical:
                std::stable_sort(renderCache.columnOrder.begin(), renderCache.columnOrder.end(),
                                 [&data](size_t a, size_t b) {
                                     return data->GetColumn(a).label < data->GetColumn(b).label;
                                 });
                break;
            case ColumnSortMode::DataOrder:
            default:
                break;
        }
    }

    void UltraCanvasMekkoChartElement::BuildSegmentRects(std::shared_ptr<MekkoChartDataVector>& data) {
        size_t columnCount = data->GetColumnCount();
        size_t seriesCount = data->GetSeriesCount();

        renderCache.grandTotal = data->GetGrandTotal();
        for (size_t i = 0; i < columnCount; ++i) {
            renderCache.maxColumnTotal = std::max(renderCache.maxColumnTotal, data->GetColumnTotal(i));
        }
        if (renderCache.grandTotal <= 0.0 || renderCache.maxColumnTotal <= 0.0) return;

        double totalGap = columnGap * static_cast<double>(columnCount > 1 ? columnCount - 1 : 0);
        double availableWidth = std::max(1.0, cachedPlotArea.width - totalGap);

        renderCache.columnX.resize(columnCount);
        renderCache.columnWidth.resize(columnCount);

        double currentX = cachedPlotArea.x;
        for (size_t displayIndex = 0; displayIndex < columnCount; ++displayIndex) {
            size_t columnIndex = renderCache.columnOrder[displayIndex];
            double columnTotal = data->GetColumnTotal(columnIndex);
            double width = (columnTotal / renderCache.grandTotal) * availableWidth;

            renderCache.columnX[displayIndex] = currentX;
            renderCache.columnWidth[displayIndex] = width;

            // Column height: full plot height for Marimekko, proportional for BarMekko
            double columnHeight;
            if (mekkoMode == MekkoMode::Marimekko) {
                columnHeight = cachedPlotArea.height;
            } else {
                columnHeight = (columnTotal / renderCache.maxColumnTotal) * cachedPlotArea.height;
            }

            // Stack segments top-down so visual order matches the legend
            double currentY = cachedPlotArea.GetBottom() - columnHeight;
            for (size_t seriesIndex = 0; seriesIndex < seriesCount; ++seriesIndex) {
                double value = data->GetValue(columnIndex, seriesIndex);
                if (value <= 0.0 || columnTotal <= 0.0) continue;

                double segmentHeight = (value / columnTotal) * columnHeight;

                SegmentRect segment;
                segment.columnIndex = columnIndex;
                segment.seriesIndex = seriesIndex;
                segment.rect = Rect2Dd(currentX, currentY, width, segmentHeight);
                segment.color = GetSeriesColor(seriesIndex);
                segment.value = value;
                segment.percentOfColumn = (value / columnTotal) * 100.0;
                renderCache.segments.push_back(segment);

                currentY += segmentHeight;
            }

            currentX += width + columnGap;
        }
    }

// =============================================================================
// MAIN RENDERING
// =============================================================================

    void UltraCanvasMekkoChartElement::RenderChart(IRenderContext* ctx) {
        if (!ctx) return;

        auto data = GetMekkoDataSource();
        if (!data || data->GetColumnCount() == 0 || data->GetSeriesCount() == 0) return;

        if (!renderCache.isValid) {
            UpdateRenderingCache();
        }

        if (mekkoMode == MekkoMode::Marimekko) {
            DrawPercentGrid(ctx);
        }
        DrawSegments(ctx);
        if (segmentLabelMode != SegmentLabelMode::NoLabels) {
            DrawSegmentLabels(ctx);
        }
        if (columnHeaderMode != ColumnHeaderMode::NoHeader) {
            DrawColumnHeaders(ctx);
        }
        if (showColumnLabels) {
            DrawColumnLabels(ctx);
        }
        if (showYAxisLabels) {
            DrawYAxisLabels(ctx);
        }
        if (showCumulativeXAxis) {
            DrawCumulativeXAxis(ctx);
        }
        if (legendPosition != LegendPosition::Hidden) {
            DrawLegend(ctx);
        }
    }

    void UltraCanvasMekkoChartElement::DrawPercentGrid(IRenderContext* ctx) {
        ctx->SetStrokePaint(gridColor);
        ctx->SetStrokeWidth(1.0f);

        for (int percent = 0; percent <= 100; percent += 20) {
            double y = cachedPlotArea.GetBottom() - (percent / 100.0) * cachedPlotArea.height;
            ctx->DrawLine({cachedPlotArea.x, y}, {cachedPlotArea.GetRight(), y});
        }
    }

    void UltraCanvasMekkoChartElement::DrawSegments(IRenderContext* ctx) {
        for (size_t i = 0; i < renderCache.segments.size(); ++i) {
            const auto& segment = renderCache.segments[i];

            Color fillColor = segment.color;
            // Dim non-hovered series while hovering a legend entry
            if (hoveredLegendSeries != SIZE_MAX && segment.seriesIndex != hoveredLegendSeries) {
                fillColor.a = 70;
            }

            ctx->SetFillPaint(fillColor);
            if (segmentCornerRadius > 0.0f) {
                ctx->FillRoundedRectangle(segment.rect, segmentCornerRadius);
            } else {
                ctx->FillRectangle(segment.rect);
            }

            if (segmentBorderWidth > 0.0f) {
                ctx->SetStrokePaint(segmentBorderColor);
                ctx->SetStrokeWidth(segmentBorderWidth);
                if (segmentCornerRadius > 0.0f) {
                    ctx->DrawRoundedRectangle(segment.rect, segmentCornerRadius);
                } else {
                    ctx->DrawRectangle(segment.rect);
                }
            }
        }

        // Highlight border on top of everything else
        if (highlightHoveredSegment && hoveredSegment < renderCache.segments.size()) {
            const auto& segment = renderCache.segments[hoveredSegment];
            ctx->SetStrokePaint(hoverBorderColor);
            ctx->SetStrokeWidth(hoverBorderWidth);
            if (segmentCornerRadius > 0.0f) {
                ctx->DrawRoundedRectangle(segment.rect, segmentCornerRadius);
            } else {
                ctx->DrawRectangle(segment.rect);
            }
        }
    }

    void UltraCanvasMekkoChartElement::DrawSegmentLabels(IRenderContext* ctx) {
        auto data = GetMekkoDataSource();
        if (!data) return;

        ctx->SetFontSize(segmentLabelFontSize);

        for (const auto& segment : renderCache.segments) {
            if (segment.rect.height < minSegmentLabelHeight) continue;

            std::string text;
            switch (segmentLabelMode) {
                case SegmentLabelMode::Value:
                    text = FormatValue(segment.value);
                    break;
                case SegmentLabelMode::Percent:
                    text = FormatPercent(segment.percentOfColumn);
                    break;
                case SegmentLabelMode::ValueAndPercent:
                    text = FormatValue(segment.value) + " (" + FormatPercent(segment.percentOfColumn) + ")";
                    break;
                case SegmentLabelMode::SeriesName:
                    text = data->GetSeries(segment.seriesIndex).name;
                    break;
                case SegmentLabelMode::SeriesAndValue:
                    text = data->GetSeries(segment.seriesIndex).name + ": " + FormatValue(segment.value);
                    break;
                case SegmentLabelMode::NoLabels:
                default:
                    continue;
            }

            Size2Di textSize = ctx->GetTextLineDimensions(text);
            if (textSize.width > segment.rect.width - 4) {
                // Fall back to a shorter label before giving up entirely
                if (segmentLabelMode == SegmentLabelMode::ValueAndPercent ||
                    segmentLabelMode == SegmentLabelMode::SeriesAndValue) {
                    text = FormatValue(segment.value);
                    textSize = ctx->GetTextLineDimensions(text);
                }
                if (textSize.width > segment.rect.width - 4) continue;
            }

            Color textColor = autoContrastSegmentLabels
                              ? GetContrastTextColor(segment.color)
                              : segmentLabelColor;
            if (hoveredLegendSeries != SIZE_MAX && segment.seriesIndex != hoveredLegendSeries) {
                textColor.a = 90;
            }
            ctx->SetTextPaint(textColor);

            ctx->DrawText(text, {
                    segment.rect.x + (segment.rect.width - textSize.width) / 2.0,
                    segment.rect.y + (segment.rect.height - textSize.height) / 2.0
            });
        }
    }

    void UltraCanvasMekkoChartElement::DrawColumnHeaders(IRenderContext* ctx) {
        auto data = GetMekkoDataSource();
        if (!data || renderCache.grandTotal <= 0.0) return;

        ctx->SetTextPaint(labelTextColor);
        ctx->SetFontSize(headerFontSize);

        for (size_t displayIndex = 0; displayIndex < renderCache.columnOrder.size(); ++displayIndex) {
            size_t columnIndex = renderCache.columnOrder[displayIndex];
            double columnTotal = data->GetColumnTotal(columnIndex);
            double percent = (columnTotal / renderCache.grandTotal) * 100.0;

            std::string text;
            switch (columnHeaderMode) {
                case ColumnHeaderMode::WidthPercent:
                    text = FormatPercent(percent);
                    break;
                case ColumnHeaderMode::Total:
                    text = FormatValue(columnTotal);
                    break;
                case ColumnHeaderMode::TotalAndPercent:
                    text = FormatValue(columnTotal) + " (" + FormatPercent(percent) + ")";
                    break;
                case ColumnHeaderMode::NoHeader:
                default:
                    continue;
            }

            Size2Di textSize = ctx->GetTextLineDimensions(text);
            if (textSize.width > renderCache.columnWidth[displayIndex] + columnGap) continue;

            double centerX = renderCache.columnX[displayIndex] + renderCache.columnWidth[displayIndex] / 2.0;

            // For BarMekko place the header just above the column top
            double y;
            if (mekkoMode == MekkoMode::BarMekko) {
                double columnHeight = (columnTotal / renderCache.maxColumnTotal) * cachedPlotArea.height;
                y = cachedPlotArea.GetBottom() - columnHeight - textSize.height - 3;
            } else {
                y = cachedPlotArea.y - textSize.height - 3;
            }

            ctx->DrawText(text, {centerX - textSize.width / 2.0, y});
        }
    }

    void UltraCanvasMekkoChartElement::DrawColumnLabels(IRenderContext* ctx) {
        auto data = GetMekkoDataSource();
        if (!data) return;

        ctx->SetTextPaint(labelTextColor);
        ctx->SetFontSize(labelFontSize);

        for (size_t displayIndex = 0; displayIndex < renderCache.columnOrder.size(); ++displayIndex) {
            size_t columnIndex = renderCache.columnOrder[displayIndex];
            const std::string& label = data->GetColumn(columnIndex).label;
            if (label.empty()) continue;

            Size2Di textSize = ctx->GetTextLineDimensions(label);
            double centerX = renderCache.columnX[displayIndex] + renderCache.columnWidth[displayIndex] / 2.0;
            double y = cachedPlotArea.GetBottom() + (showCumulativeXAxis ? 20.0 : 4.0);

            if (rotateXAxisLabels) {
                ctx->PushState();
                ctx->Translate(centerX, y);
                ctx->Rotate(xAxisLabelRotation * M_PI / 180.0f);
                ctx->DrawText(label, {0, 0});
                ctx->PopState();
            } else {
                if (textSize.width > renderCache.columnWidth[displayIndex] + columnGap) continue;
                ctx->DrawText(label, {centerX - textSize.width / 2.0, y});
            }
        }
    }

    void UltraCanvasMekkoChartElement::DrawYAxisLabels(IRenderContext* ctx) {
        ctx->SetTextPaint(labelTextColor);
        ctx->SetStrokePaint(labelTextColor);
        ctx->SetFontSize(labelFontSize - 1);

        if (mekkoMode == MekkoMode::Marimekko) {
            for (int percent = 0; percent <= 100; percent += 20) {
                double y = cachedPlotArea.GetBottom() - (percent / 100.0) * cachedPlotArea.height;
                std::string label = std::to_string(percent) + "%";
                Size2Di textSize = ctx->GetTextLineDimensions(label);
                ctx->DrawText(label, {cachedPlotArea.x - textSize.width - 8,
                                      y - textSize.height / 2.0});
                ctx->DrawLine({cachedPlotArea.x - 5, y}, {cachedPlotArea.x, y});
            }
        } else {
            int numTicks = 5;
            for (int i = 0; i <= numTicks; ++i) {
                double value = (renderCache.maxColumnTotal * i) / numTicks;
                double y = cachedPlotArea.GetBottom() - (static_cast<double>(i) / numTicks) * cachedPlotArea.height;
                std::string label = FormatValue(value);
                Size2Di textSize = ctx->GetTextLineDimensions(label);
                ctx->DrawText(label, {cachedPlotArea.x - textSize.width - 8,
                                      y - textSize.height / 2.0});
                ctx->DrawLine({cachedPlotArea.x - 5, y}, {cachedPlotArea.x, y});
            }
        }
    }

    void UltraCanvasMekkoChartElement::DrawCumulativeXAxis(IRenderContext* ctx) {
        if (renderCache.grandTotal <= 0.0) return;

        auto data = GetMekkoDataSource();
        if (!data) return;

        ctx->SetTextPaint(labelTextColor);
        ctx->SetStrokePaint(labelTextColor);
        ctx->SetFontSize(labelFontSize - 2);

        double axisY = cachedPlotArea.GetBottom();
        double cumulative = 0.0;

        // Tick at 0%
        std::string zeroLabel = "0%";
        Size2Di zeroSize = ctx->GetTextLineDimensions(zeroLabel);
        ctx->DrawText(zeroLabel, {cachedPlotArea.x - zeroSize.width / 2.0, axisY + 4});

        for (size_t displayIndex = 0; displayIndex < renderCache.columnOrder.size(); ++displayIndex) {
            size_t columnIndex = renderCache.columnOrder[displayIndex];
            cumulative += data->GetColumnTotal(columnIndex);
            double percent = (cumulative / renderCache.grandTotal) * 100.0;

            double x = renderCache.columnX[displayIndex] + renderCache.columnWidth[displayIndex];
            ctx->DrawLine({x, axisY}, {x, axisY + 3});

            std::string label = FormatPercent(percent);
            Size2Di textSize = ctx->GetTextLineDimensions(label);
            ctx->DrawText(label, {x - textSize.width / 2.0, axisY + 4});
        }
    }

    void UltraCanvasMekkoChartElement::DrawLegend(IRenderContext* ctx) {
        auto data = GetMekkoDataSource();
        if (!data || data->GetSeriesCount() == 0) return;

        renderCache.legendItemRects.clear();
        ctx->SetFontSize(legendFontSize);

        size_t seriesCount = data->GetSeriesCount();

        if (legendPosition == LegendPosition::Right) {
            double x = GetWidth() - legendWidth + 8;
            double y = cachedPlotArea.y;
            double rowHeight = legendSwatchSize + legendItemSpacing;

            for (size_t i = 0; i < seriesCount; ++i) {
                Color color = GetSeriesColor(i);
                if (hoveredLegendSeries != SIZE_MAX && i != hoveredLegendSeries) {
                    color.a = 70;
                }
                ctx->SetFillPaint(color);
                ctx->FillRectangle(Rect2Dd(x, y, legendSwatchSize, legendSwatchSize));

                ctx->SetTextPaint(labelTextColor);
                const std::string& name = data->GetSeries(i).name;
                Size2Di textSize = ctx->GetTextLineDimensions(name);
                ctx->DrawText(name, {x + legendSwatchSize + 6,
                                     y + (legendSwatchSize - textSize.height) / 2.0});

                renderCache.legendItemRects.push_back(
                        Rect2Dd(x, y, legendSwatchSize + 6 + textSize.width, legendSwatchSize));
                y += rowHeight;
            }
        } else {
            // Horizontal legend (Top or Bottom): items flow left to right
            double y = (legendPosition == LegendPosition::Top)
                       ? 4.0
                       : GetHeight() - legendSwatchSize - 8.0;
            double x = cachedPlotArea.x;

            for (size_t i = 0; i < seriesCount; ++i) {
                const std::string& name = data->GetSeries(i).name;
                Size2Di textSize = ctx->GetTextLineDimensions(name);
                double itemWidth = legendSwatchSize + 6 + textSize.width + 16;

                if (x + itemWidth > GetWidth() - 8 && x > cachedPlotArea.x) break;

                Color color = GetSeriesColor(i);
                if (hoveredLegendSeries != SIZE_MAX && i != hoveredLegendSeries) {
                    color.a = 70;
                }
                ctx->SetFillPaint(color);
                ctx->FillRectangle(Rect2Dd(x, y, legendSwatchSize, legendSwatchSize));

                ctx->SetTextPaint(labelTextColor);
                ctx->DrawText(name, {x + legendSwatchSize + 6,
                                     y + (legendSwatchSize - textSize.height) / 2.0});

                renderCache.legendItemRects.push_back(
                        Rect2Dd(x, y, legendSwatchSize + 6 + textSize.width, legendSwatchSize));
                x += itemWidth;
            }
        }
    }

// =============================================================================
// HELPERS
// =============================================================================

    Color UltraCanvasMekkoChartElement::GetSeriesColor(size_t seriesIndex) const {
        auto data = std::dynamic_pointer_cast<MekkoChartDataVector>(dataSource);
        if (data && seriesIndex < data->GetSeriesCount()) {
            const Color& explicitColor = data->GetSeries(seriesIndex).color;
            if (explicitColor.a > 0) {
                return explicitColor;
            }
        }
        const auto& palette = DefaultPalette();
        return palette[seriesIndex % palette.size()];
    }

    Color UltraCanvasMekkoChartElement::GetContrastTextColor(const Color& background) const {
        // Relative luminance approximation (ITU-R BT.709)
        double luminance = (0.2126 * background.r + 0.7152 * background.g + 0.0722 * background.b) / 255.0;
        return (luminance > 0.55) ? Color(33, 33, 33, 255) : Colors::White;
    }

    std::string UltraCanvasMekkoChartElement::FormatValue(double value) const {
        if (valueFormatter) {
            return valueFormatter(value);
        }

        std::ostringstream oss;
        oss << valuePrefix;
        if (std::abs(value) >= 1e9) {
            oss << std::fixed << std::setprecision(1) << (value / 1e9) << "B";
        } else if (std::abs(value) >= 1e6) {
            oss << std::fixed << std::setprecision(1) << (value / 1e6) << "M";
        } else if (std::abs(value) >= 1e4) {
            oss << std::fixed << std::setprecision(1) << (value / 1e3) << "K";
        } else if (std::abs(value - std::round(value)) < 0.01) {
            oss << static_cast<long long>(std::llround(value));
        } else {
            oss << std::fixed << std::setprecision(1) << value;
        }
        oss << valueSuffix;
        return oss.str();
    }

    std::string UltraCanvasMekkoChartElement::FormatPercent(double percent) const {
        std::ostringstream oss;
        if (std::abs(percent - std::round(percent)) < 0.05) {
            oss << static_cast<int>(std::round(percent));
        } else {
            oss << std::fixed << std::setprecision(1) << percent;
        }
        oss << "%";
        return oss.str();
    }

// =============================================================================
// INTERACTION HANDLING
// =============================================================================

    std::pair<size_t, size_t> UltraCanvasMekkoChartElement::GetHoveredSegment() const {
        if (hoveredSegment < renderCache.segments.size()) {
            const auto& segment = renderCache.segments[hoveredSegment];
            return {segment.columnIndex, segment.seriesIndex};
        }
        return {SIZE_MAX, SIZE_MAX};
    }

    size_t UltraCanvasMekkoChartElement::GetSegmentIndexAtPosition(const Point2Di& mousePos) const {
        for (size_t i = 0; i < renderCache.segments.size(); ++i) {
            const auto& rect = renderCache.segments[i].rect;
            if (mousePos.x >= rect.x && mousePos.x <= rect.x + rect.width &&
                mousePos.y >= rect.y && mousePos.y <= rect.y + rect.height) {
                return i;
            }
        }
        return SIZE_MAX;
    }

    size_t UltraCanvasMekkoChartElement::GetLegendSeriesAtPosition(const Point2Di& mousePos) const {
        for (size_t i = 0; i < renderCache.legendItemRects.size(); ++i) {
            const auto& rect = renderCache.legendItemRects[i];
            if (mousePos.x >= rect.x && mousePos.x <= rect.x + rect.width &&
                mousePos.y >= rect.y && mousePos.y <= rect.y + rect.height) {
                return i;
            }
        }
        return SIZE_MAX;
    }

    std::string UltraCanvasMekkoChartElement::GenerateSegmentTooltip(size_t segmentIndex) const {
        auto data = std::dynamic_pointer_cast<MekkoChartDataVector>(dataSource);
        if (!data || segmentIndex >= renderCache.segments.size()) return "";

        const auto& segment = renderCache.segments[segmentIndex];
        const auto& column = data->GetColumn(segment.columnIndex);

        std::ostringstream tooltip;
        tooltip << column.label << " / " << data->GetSeries(segment.seriesIndex).name << "\n";
        tooltip << "Value: " << FormatValue(segment.value) << "\n";
        tooltip << "Of column: " << FormatPercent(segment.percentOfColumn);
        if (renderCache.grandTotal > 0.0) {
            double percentOfTotal = (segment.value / renderCache.grandTotal) * 100.0;
            tooltip << "\nOf total: " << FormatPercent(percentOfTotal);
        }
        if (!column.category.empty()) {
            tooltip << "\nCategory: " << column.category;
        }
        return tooltip.str();
    }

    bool UltraCanvasMekkoChartElement::HandleChartMouseMove(const Point2Di& mousePos) {
        bool changed = false;

        size_t legendSeries = GetLegendSeriesAtPosition(mousePos);
        if (legendSeries != hoveredLegendSeries) {
            hoveredLegendSeries = legendSeries;
            changed = true;
        }

        size_t segmentIndex = GetSegmentIndexAtPosition(mousePos);
        if (segmentIndex != hoveredSegment) {
            hoveredSegment = segmentIndex;
            changed = true;

            if (enableTooltips) {
                if (segmentIndex != SIZE_MAX) {
                    std::string tooltipContent = GenerateSegmentTooltip(segmentIndex);
                    if (!tooltipContent.empty()) {
                        auto tooltipPos = mousePos;
                        tooltipPos.x += 10;
                        tooltipPos.y -= 30;
                        tooltipPos = MapFromLocal(tooltipPos, nullptr);
                        UltraCanvasTooltipManager::UpdateAndShowTooltip(GetWindow(), tooltipContent, tooltipPos);
                        isTooltipActive = true;
                    }
                } else if (isTooltipActive) {
                    UltraCanvasTooltipManager::HideTooltip();
                    isTooltipActive = false;
                }
            }
        }

        if (changed) {
            RequestRedraw();
        }
        return changed;
    }

    bool UltraCanvasMekkoChartElement::OnEvent(const UCEvent& event) {
        switch (event.type) {
            case UCEventType::MouseDown:
                if (event.button == UCMouseButton::Left && onSegmentClick) {
                    size_t segmentIndex = GetSegmentIndexAtPosition(Point2Di(event.pointer.x, event.pointer.y));
                    if (segmentIndex != SIZE_MAX) {
                        const auto& segment = renderCache.segments[segmentIndex];
                        onSegmentClick(segment.columnIndex, segment.seriesIndex);
                    }
                }
                break;
            case UCEventType::MouseLeave:
                if (hoveredSegment != SIZE_MAX || hoveredLegendSeries != SIZE_MAX) {
                    hoveredSegment = SIZE_MAX;
                    hoveredLegendSeries = SIZE_MAX;
                    HideTooltip();
                    RequestRedraw();
                }
                break;
            default:
                break;
        }
        return UltraCanvasChartElementBase::OnEvent(event);
    }

} // namespace UltraCanvas
