// Plugins/Charts/UltraCanvasDumbbellChart.cpp
// Dumbbell (DNA / connected dot) chart implementation
// Version: 1.0.0
// Last Modified: 2026-07-29
// Author: UltraCanvas Framework
#include "Plugins/Charts/UltraCanvasDumbbellChart.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>

namespace UltraCanvas {

// =============================================================================
// DATA SOURCE - CSV LOADING
// =============================================================================

    void DumbbellDataSource::LoadFromCSV(const std::string& filePath) {
        std::ifstream file(filePath);
        if (!file.is_open()) return;

        dumbbellData.clear();

        std::string line;
        while (std::getline(file, line)) {
            if (line.empty()) continue;

            std::vector<std::string> fields;
            std::stringstream ss(line);
            std::string field;
            while (std::getline(ss, field, ',')) {
                // Trim surrounding whitespace and quotes
                size_t begin = field.find_first_not_of(" \t\r\n\"");
                size_t end = field.find_last_not_of(" \t\r\n\"");
                fields.push_back(begin == std::string::npos ? std::string()
                                                            : field.substr(begin, end - begin + 1));
            }

            if (fields.size() < 3) continue;

            // Rows whose value columns do not parse are skipped - this also swallows
            // a header line without needing to special-case it.
            try {
                double v1 = std::stod(fields[1]);
                double v2 = std::stod(fields[2]);
                std::string lbl1 = fields.size() > 3 && !fields[3].empty() ? fields[3] : "Group 1";
                std::string lbl2 = fields.size() > 4 && !fields[4].empty() ? fields[4] : "Group 2";
                dumbbellData.emplace_back(fields[0], v1, v2, lbl1, lbl2);
            } catch (const std::exception&) {
                continue;
            }
        }
    }

// =============================================================================
// CONSTRUCTION
// =============================================================================

    UltraCanvasDumbbellChart::UltraCanvasDumbbellChart(const std::string& id, int x, int y,
                                                       int width, int height)
            : UltraCanvasChartElementBase(id, x, y, width, height) {
        dumbbellDataSource = CreateDumbbellDataSource();
        dataSource = dumbbellDataSource;   // Base class renders empty state from this

        enableTooltips = true;
        showGrid = true;
        showAxes = true;
        showValueLabels = true;
        valueLabelFontSize = 9.0f;
    }

// =============================================================================
// DATA
// =============================================================================

    void UltraCanvasDumbbellChart::SetDumbbellDataSource(std::shared_ptr<DumbbellDataSource> data) {
        dumbbellDataSource = data ? data : CreateDumbbellDataSource();
        dataSource = dumbbellDataSource;
        orderValid = false;
        hoveredRowIndex = -1;
        hoveredDotIndex = -1;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasDumbbellChart::AddDataPoint(const std::string& category, double value1, double value2,
                                                 const std::string& label1, const std::string& label2) {
        if (!dumbbellDataSource) return;
        dumbbellDataSource->AddDumbbellPoint(category, value1, value2, label1, label2);
        orderValid = false;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasDumbbellChart::AddDataPoint(const DumbbellDataPoint& point) {
        if (!dumbbellDataSource) return;
        dumbbellDataSource->AddDumbbellPoint(point);
        orderValid = false;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasDumbbellChart::ClearData() {
        if (!dumbbellDataSource) return;
        dumbbellDataSource->Clear();
        displayOrder.clear();
        orderValid = true;
        hoveredRowIndex = -1;
        hoveredDotIndex = -1;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasDumbbellChart::SetSortOrder(DumbbellSortOrder order) {
        if (sortOrder == order) return;
        sortOrder = order;
        orderValid = false;
        hoveredRowIndex = -1;
        hoveredDotIndex = -1;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasDumbbellChart::RebuildDisplayOrder() {
        displayOrder.clear();
        if (!dumbbellDataSource) {
            orderValid = true;
            return;
        }

        size_t count = dumbbellDataSource->GetPointCount();
        displayOrder.reserve(count);
        for (size_t i = 0; i < count; ++i) displayOrder.push_back(i);

        const auto& points = dumbbellDataSource->GetPoints();
        auto by = [&points](DumbbellSortOrder order) {
            return [&points, order](size_t a, size_t b) {
                const auto& pa = points[a];
                const auto& pb = points[b];
                switch (order) {
                    case DumbbellSortOrder::CategoryAscending:  return pa.categoryLabel < pb.categoryLabel;
                    case DumbbellSortOrder::CategoryDescending: return pb.categoryLabel < pa.categoryLabel;
                    case DumbbellSortOrder::Value1Ascending:    return pa.value1 < pb.value1;
                    case DumbbellSortOrder::Value1Descending:   return pb.value1 < pa.value1;
                    case DumbbellSortOrder::Value2Ascending:    return pa.value2 < pb.value2;
                    case DumbbellSortOrder::Value2Descending:   return pb.value2 < pa.value2;
                    case DumbbellSortOrder::RangeAscending:     return pa.GetRange() < pb.GetRange();
                    case DumbbellSortOrder::RangeDescending:    return pb.GetRange() < pa.GetRange();
                    default:                                    return a < b;
                }
            };
        };

        if (sortOrder != DumbbellSortOrder::InsertionOrder) {
            std::stable_sort(displayOrder.begin(), displayOrder.end(), by(sortOrder));
        }

        orderValid = true;
    }

// =============================================================================
// GEOMETRY CONFIGURATION
// =============================================================================

    void UltraCanvasDumbbellChart::SetDotRadius(float radius) {
        dotRadius = std::max(2.0f, std::min(24.0f, radius));
        RequestRedraw();
    }

    void UltraCanvasDumbbellChart::SetLineWidth(float width) {
        lineWidth = std::max(0.5f, std::min(12.0f, width));
        RequestRedraw();
    }

    void UltraCanvasDumbbellChart::SetRowHeight(float height) {
        rowHeight = std::max(6.0f, std::min(120.0f, height));
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasDumbbellChart::SetRowSpacing(float spacing) {
        rowSpacing = std::max(0.0f, std::min(60.0f, spacing));
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasDumbbellChart::SetAutoFitRows(bool autoFit) {
        autoFitRows = autoFit;
        InvalidateCache();
        RequestRedraw();
    }

    float UltraCanvasDumbbellChart::GetPreferredHeight() const {
        size_t count = GetDataPointCount();
        if (count == 0) return GetMarginTop() + GetMarginBottom() + 40.0f;
        return GetMarginTop() + GetMarginBottom() +
               static_cast<float>(count) * (rowHeight + rowSpacing) - rowSpacing;
    }

// =============================================================================
// COLOURS
// =============================================================================

    void UltraCanvasDumbbellChart::SetDefaultColors(const Color& color1, const Color& color2,
                                                     const Color& lineCol) {
        defaultColor1 = color1;
        defaultColor2 = color2;
        defaultLineColor = lineCol;
        RequestRedraw();
    }

    void UltraCanvasDumbbellChart::SetGroupColors(const Color& color1, const Color& color2) {
        defaultColor1 = color1;
        defaultColor2 = color2;
        RequestRedraw();
    }

    void UltraCanvasDumbbellChart::SetConnectorColor(const Color& lineCol) {
        defaultLineColor = lineCol;
        RequestRedraw();
    }

// =============================================================================
// LABELS
// =============================================================================

    void UltraCanvasDumbbellChart::SetShowCategoryLabels(bool show) {
        showCategoryLabels = show;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasDumbbellChart::SetCategoryLabelWidth(float width) {
        categoryLabelWidth = std::max(20.0f, std::min(400.0f, width));
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasDumbbellChart::SetCategoryLabelFontSize(float size) {
        categoryLabelFontSize = std::max(6.0f, std::min(24.0f, size));
        RequestRedraw();
    }

    void UltraCanvasDumbbellChart::SetCategoryLabelColor(const Color& color) {
        categoryLabelColor = color;
        RequestRedraw();
    }

    void UltraCanvasDumbbellChart::SetValueFormat(const std::string& format) {
        valueFormat = format;
        RequestRedraw();
    }

    void UltraCanvasDumbbellChart::SetValueLabelPlacement(DumbbellValueLabelPlacement placement) {
        valueLabelPlacement = placement;
        InvalidateCache();
        RequestRedraw();
    }

// =============================================================================
// LEGEND
// =============================================================================

    void UltraCanvasDumbbellChart::SetShowLegend(bool show) {
        showLegend = show;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasDumbbellChart::SetLegendLabels(const std::string& label1, const std::string& label2) {
        legendLabel1 = label1;
        legendLabel2 = label2;
        RequestRedraw();
    }

    void UltraCanvasDumbbellChart::SetLegendPosition(DumbbellLegendPosition position) {
        legendPosition = position;
        InvalidateCache();
        RequestRedraw();
    }

// =============================================================================
// AXIS
// =============================================================================

    void UltraCanvasDumbbellChart::SetShowAxisValues(bool show) {
        showAxisValues = show;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasDumbbellChart::SetAxisTickCount(int ticks) {
        numAxisTicks = std::max(1, std::min(20, ticks));
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasDumbbellChart::SetAxisLabel(const std::string& label) {
        axisLabel = label;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasDumbbellChart::SetIncludeZeroInRange(bool include) {
        includeZeroInRange = include;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasDumbbellChart::SetUseNiceAxisBounds(bool useNice) {
        useNiceAxisBounds = useNice;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasDumbbellChart::SetValueRange(double minValue, double maxValue) {
        if (maxValue <= minValue) return;
        useManualRange = true;
        manualMinValue = minValue;
        manualMaxValue = maxValue;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasDumbbellChart::ClearValueRange() {
        useManualRange = false;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasDumbbellChart::SetShowRowHighlight(bool show) {
        showRowHighlight = show;
        RequestRedraw();
    }

// =============================================================================
// LAYOUT
// =============================================================================

    float UltraCanvasDumbbellChart::GetMarginTop() const {
        float margin = chartTitle.empty() ? 10.0f : 34.0f;
        if (showLegend && (legendPosition == DumbbellLegendPosition::TopLeft ||
                           legendPosition == DumbbellLegendPosition::TopRight)) {
            margin += 22.0f;
        }
        return margin;
    }

    float UltraCanvasDumbbellChart::GetMarginBottom() const {
        float margin = showAxisValues ? 26.0f : 10.0f;
        if (!axisLabel.empty()) margin += 18.0f;
        if (showLegend && (legendPosition == DumbbellLegendPosition::BottomLeft ||
                           legendPosition == DumbbellLegendPosition::BottomRight)) {
            margin += 22.0f;
        }
        return margin;
    }

    float UltraCanvasDumbbellChart::GetMarginLeft() const {
        return showCategoryLabels ? categoryLabelWidth + categoryLabelGap : 14.0f;
    }

    ChartPlotArea UltraCanvasDumbbellChart::CalculatePlotArea() {
        float left = GetMarginLeft();
        float top = GetMarginTop();
        float width = static_cast<float>(GetWidth()) - left - rightMargin;
        float height = static_cast<float>(GetHeight()) - top - GetMarginBottom();

        // Never hand back a degenerate rectangle - callers divide by width/height
        return ChartPlotArea(left, top, std::max(1.0f, width), std::max(1.0f, height));
    }

    ChartDataBounds UltraCanvasDumbbellChart::CalculateDataBounds() {
        ChartDataBounds bounds;
        size_t count = GetDataPointCount();
        if (count == 0) return bounds;

        double minValue = 0.0, maxValue = 1.0;
        dumbbellDataSource->GetValueBounds(minValue, maxValue);
        return ChartDataBounds(minValue, maxValue, 0.0, static_cast<double>(count - 1));
    }

    void UltraCanvasDumbbellChart::UpdateRenderingCache() {
        if (!orderValid) RebuildDisplayOrder();
        if (cacheValid) return;

        UltraCanvasChartElementBase::UpdateRenderingCache();
        UpdateAxisRange();
    }

// Round a number up to 1, 2, 2.5 or 5 times a power of ten
    static double DumbbellNiceNumber(double value, bool round) {
        if (value <= 0.0) return 1.0;

        double exponent = std::floor(std::log10(value));
        double fraction = value / std::pow(10.0, exponent);
        double niceFraction;

        if (round) {
            if (fraction < 1.5)      niceFraction = 1.0;
            else if (fraction < 3.0) niceFraction = 2.0;
            else if (fraction < 7.0) niceFraction = 5.0;
            else                     niceFraction = 10.0;
        } else {
            if (fraction <= 1.0)      niceFraction = 1.0;
            else if (fraction <= 2.0) niceFraction = 2.0;
            else if (fraction <= 2.5) niceFraction = 2.5;
            else if (fraction <= 5.0) niceFraction = 5.0;
            else                      niceFraction = 10.0;
        }

        return niceFraction * std::pow(10.0, exponent);
    }

    void UltraCanvasDumbbellChart::UpdateAxisRange() {
        if (useManualRange) {
            axisMin = manualMinValue;
            axisMax = manualMaxValue;
            axisStep = (axisMax - axisMin) / std::max(1, numAxisTicks);
            return;
        }

        double minValue = 0.0, maxValue = 1.0;
        bool hasData = dumbbellDataSource && dumbbellDataSource->GetValueBounds(minValue, maxValue);
        if (!hasData) {
            axisMin = 0.0;
            axisMax = 1.0;
            axisStep = 0.25;
            return;
        }

        if (includeZeroInRange) {
            minValue = std::min(minValue, 0.0);
            maxValue = std::max(maxValue, 0.0);
        }

        double range = maxValue - minValue;
        if (range <= 0.0) {
            // Every value identical - open up a symmetric window so the dots are not on the edge
            double pad = std::abs(maxValue) > 0.0 ? std::abs(maxValue) * 0.1 : 1.0;
            minValue -= pad;
            maxValue += pad;
            range = maxValue - minValue;
        } else {
            minValue -= range * 0.08;
            maxValue += range * 0.08;
        }

        if (useNiceAxisBounds) {
            double niceRange = DumbbellNiceNumber(maxValue - minValue, false);
            axisStep = DumbbellNiceNumber(niceRange / std::max(1, numAxisTicks), true);
            axisMin = std::floor(minValue / axisStep) * axisStep;
            axisMax = std::ceil(maxValue / axisStep) * axisStep;
        } else {
            axisMin = minValue;
            axisMax = maxValue;
            axisStep = (axisMax - axisMin) / std::max(1, numAxisTicks);
        }

        if (axisMax <= axisMin) {
            axisMax = axisMin + 1.0;
            axisStep = 0.25;
        }
    }

    double UltraCanvasDumbbellChart::GetRowPitch() const {
        size_t count = displayOrder.size();
        if (count == 0) return static_cast<double>(rowHeight + rowSpacing);

        if (autoFitRows) {
            return cachedPlotArea.height / static_cast<double>(count);
        }
        return static_cast<double>(rowHeight + rowSpacing);
    }

    double UltraCanvasDumbbellChart::GetEffectiveDotRadius() const {
        double pitch = GetRowPitch();
        // Leave a hair of breathing room so neighbouring rows never touch
        double maxRadius = std::max(1.5, pitch / 2.0 - 1.0);
        return std::min(static_cast<double>(dotRadius), maxRadius);
    }

    double UltraCanvasDumbbellChart::GetRowCenterY(size_t displayIndex) const {
        double pitch = GetRowPitch();
        return cachedPlotArea.y + (static_cast<double>(displayIndex) + 0.5) * pitch;
    }

    double UltraCanvasDumbbellChart::ValueToX(double value) const {
        double range = axisMax - axisMin;
        if (range <= 0.0) return cachedPlotArea.x;
        double normalized = (value - axisMin) / range;
        return cachedPlotArea.x + normalized * cachedPlotArea.width;
    }

// =============================================================================
// TEXT HELPERS
// =============================================================================

    std::string UltraCanvasDumbbellChart::FormatValue(double value) const {
        char buffer[128];
        // valueFormat is supplied by the caller; guard the buffer and fall back on a
        // plain rendering when the format produces nothing usable.
        int written = std::snprintf(buffer, sizeof(buffer), valueFormat.c_str(), value);
        if (written <= 0) {
            std::snprintf(buffer, sizeof(buffer), "%.1f", value);
        }
        return std::string(buffer);
    }

    std::string UltraCanvasDumbbellChart::TruncateToWidth(IRenderContext* ctx, const std::string& text,
                                                          double maxWidth) const {
        if (text.empty() || maxWidth <= 0.0) return std::string();
        if (ctx->GetTextLineDimensions(text).width <= maxWidth) return text;

        const std::string ellipsis = "...";
        std::string result = text;
        while (!result.empty()) {
            result.pop_back();
            if (ctx->GetTextLineDimensions(result + ellipsis).width <= maxWidth) {
                return result + ellipsis;
            }
        }
        return std::string();
    }

// =============================================================================
// RENDERING
// =============================================================================

    void UltraCanvasDumbbellChart::RenderCommonBackground(IRenderContext* ctx) {
        if (!ctx) return;

        if (showBackground) {
            ctx->DrawFilledRectangle(GetLocalBounds(), backgroundColor);

            ctx->SetFillPaint(plotAreaColor);
            ctx->FillRectangle(cachedPlotArea.ToRect2D());

            ctx->SetStrokePaint(Color(200, 200, 200, 255));
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawRectangle(cachedPlotArea.ToRect2D());
        }

        if (showGrid) RenderGrid(ctx);
        if (showAxes) RenderAxes(ctx);

        if (!chartTitle.empty()) {
            ctx->PushState();
            ctx->SetTextPaint(Color(30, 30, 30, 255));
            ctx->SetFontSize(14.0f);
            ctx->SetFontWeight(FontWeight::Bold);
            Size2Di titleSize = ctx->GetTextLineDimensions(chartTitle);
            double titleX = (static_cast<double>(GetWidth()) - titleSize.width) / 2.0;
            ctx->DrawText(chartTitle, Point2Dd(std::max(4.0, titleX), 8.0));
            ctx->PopState();
        }
    }

    void UltraCanvasDumbbellChart::RenderGrid(IRenderContext* ctx) {
        if (!ctx || axisMax <= axisMin) return;

        ctx->PushState();
        ctx->SetStrokePaint(gridColor);
        ctx->SetStrokeWidth(1.0f);

        double top = cachedPlotArea.y;
        double bottom = cachedPlotArea.y + cachedPlotArea.height;

        for (double value = axisMin; value <= axisMax + axisStep * 0.5; value += axisStep) {
            double x = ValueToX(value);
            if (x < cachedPlotArea.x - 0.5 || x > cachedPlotArea.GetRight() + 0.5) continue;
            ctx->DrawLine({x, top}, {x, bottom});
        }

        ctx->PopState();
    }

    void UltraCanvasDumbbellChart::RenderAxes(IRenderContext* ctx) {
        if (!ctx) return;

        ctx->PushState();
        ctx->SetStrokePaint(Color(120, 120, 120, 255));
        ctx->SetStrokeWidth(1.0f);

        double baseline = cachedPlotArea.y + cachedPlotArea.height;
        ctx->DrawLine({cachedPlotArea.x, baseline}, {cachedPlotArea.GetRight(), baseline});
        ctx->PopState();

        RenderAxisLabels(ctx);
    }

    void UltraCanvasDumbbellChart::RenderAxisLabels(IRenderContext* ctx) {
        if (!ctx || axisMax <= axisMin) return;

        double baseline = cachedPlotArea.y + cachedPlotArea.height;

        if (showAxisValues) {
            ctx->PushState();
            ctx->SetFontSize(9.0f);
            ctx->SetTextPaint(Color(80, 80, 80, 255));
            ctx->SetStrokePaint(Color(120, 120, 120, 255));
            ctx->SetStrokeWidth(1.0f);

            for (double value = axisMin; value <= axisMax + axisStep * 0.5; value += axisStep) {
                double x = ValueToX(value);
                if (x < cachedPlotArea.x - 0.5 || x > cachedPlotArea.GetRight() + 0.5) continue;

                ctx->DrawLine({x, baseline}, {x, baseline + 4.0});

                std::string label = FormatValue(value);
                Size2Di size = ctx->GetTextLineDimensions(label);
                ctx->DrawText(label, Point2Dd(x - size.width / 2.0, baseline + 6.0));
            }
            ctx->PopState();
        }

        if (!axisLabel.empty()) {
            ctx->PushState();
            ctx->SetFontSize(11.0f);
            ctx->SetTextPaint(Color(60, 60, 60, 255));
            Size2Di size = ctx->GetTextLineDimensions(axisLabel);
            double labelX = cachedPlotArea.x + (cachedPlotArea.width - size.width) / 2.0;
            double labelY = baseline + (showAxisValues ? 24.0 : 8.0);
            ctx->DrawText(axisLabel, Point2Dd(labelX, labelY));
            ctx->PopState();
        }
    }

    void UltraCanvasDumbbellChart::RenderChart(IRenderContext* ctx) {
        if (!ctx || displayOrder.empty()) return;

        RenderRowHighlight(ctx);
        RenderCategoryLabels(ctx);
        RenderDumbbellRows(ctx);

        if (showLegend) RenderLegend(ctx);
    }

    void UltraCanvasDumbbellChart::RenderRowHighlight(IRenderContext* ctx) {
        if (!showRowHighlight || hoveredRowIndex < 0) return;
        if (static_cast<size_t>(hoveredRowIndex) >= displayOrder.size()) return;

        double pitch = GetRowPitch();
        double y = cachedPlotArea.y + hoveredRowIndex * pitch;

        ctx->SetFillPaint(rowHighlightColor);
        ctx->FillRectangle(Rect2Dd(cachedPlotArea.x, y, cachedPlotArea.width, pitch));
    }

    void UltraCanvasDumbbellChart::RenderCategoryLabels(IRenderContext* ctx) {
        if (!showCategoryLabels) return;

        ctx->PushState();
        ctx->SetFontSize(categoryLabelFontSize);

        double pitch = GetRowPitch();
        double labelRight = cachedPlotArea.x - categoryLabelGap;

        for (size_t i = 0; i < displayOrder.size(); ++i) {
            double yCenter = GetRowCenterY(i);
            // Skip rows scrolled/squeezed outside the plot area
            if (yCenter < cachedPlotArea.y - pitch || yCenter > cachedPlotArea.GetBottom() + pitch) continue;

            const auto& point = dumbbellDataSource->GetDumbbellPoint(displayOrder[i]);

            bool isHovered = (static_cast<int>(i) == hoveredRowIndex);
            ctx->SetTextPaint(isHovered ? Color(20, 20, 20, 255) : categoryLabelColor);

            std::string label = TruncateToWidth(ctx, point.categoryLabel, categoryLabelWidth);
            if (label.empty()) continue;

            Size2Di size = ctx->GetTextLineDimensions(label);
            // Right-align against the plot area so the rows read as a column
            double labelX = labelRight - size.width;
            ctx->DrawText(label, Point2Dd(labelX, yCenter - size.height / 2.0));
        }

        ctx->PopState();
    }

    void UltraCanvasDumbbellChart::RenderDumbbellRows(IRenderContext* ctx) {
        double pitch = GetRowPitch();
        double effRadius = GetEffectiveDotRadius();

        // Keep dots and connectors inside the plot area even when rows overflow
        ctx->PushState();
        ctx->ClipRect(Rect2Dd(cachedPlotArea.x - effRadius - 60.0, cachedPlotArea.y,
                              cachedPlotArea.width + 2.0 * effRadius + 120.0, cachedPlotArea.height));

        for (size_t i = 0; i < displayOrder.size(); ++i) {
            double yCenter = GetRowCenterY(i);
            if (yCenter < cachedPlotArea.y - pitch || yCenter > cachedPlotArea.GetBottom() + pitch) continue;

            const auto& point = dumbbellDataSource->GetDumbbellPoint(displayOrder[i]);
            DrawDumbbellRow(ctx, point, static_cast<int>(i), yCenter, effRadius);
        }

        ctx->PopState();
    }

    void UltraCanvasDumbbellChart::DrawDumbbellRow(IRenderContext* ctx, const DumbbellDataPoint& point,
                                                    int displayIndex, double yCenter, double effRadius) {
        double x1 = ValueToX(point.value1);
        double x2 = ValueToX(point.value2);

        // Connector - drawn first so the dots sit on top of it
        ctx->SetStrokePaint(ResolveLineColor(point));
        ctx->SetStrokeWidth(lineWidth);
        ctx->DrawLine({std::min(x1, x2), yCenter}, {std::max(x1, x2), yCenter});

        bool dot1Hovered = (displayIndex == hoveredRowIndex && hoveredDotIndex == 0);
        bool dot2Hovered = (displayIndex == hoveredRowIndex && hoveredDotIndex == 1);

        DrawDot(ctx, x1, yCenter, effRadius, ResolveColor1(point), dot1Hovered);
        DrawDot(ctx, x2, yCenter, effRadius, ResolveColor2(point), dot2Hovered);

        if (showValueLabels) {
            DrawValueLabels(ctx, point, x1, x2, yCenter, effRadius);
        }
    }

    void UltraCanvasDumbbellChart::DrawDot(IRenderContext* ctx, double x, double y, double radius,
                                            const Color& fillColor, bool highlighted) {
        Color borderColor = highlighted ? Color(20, 20, 20, 255) : Colors::White;
        float borderWidth = highlighted ? 2.0f : 1.0f;
        // A halo would eat the dot itself once the radius drops below a few pixels
        if (radius < 3.0) borderWidth = highlighted ? 1.5f : 0.0f;

        ctx->DrawFilledCircle(Point2Dd(x, y), static_cast<float>(radius), fillColor,
                              borderColor, borderWidth);
    }

    void UltraCanvasDumbbellChart::DrawValueLabels(IRenderContext* ctx, const DumbbellDataPoint& point,
                                                    double x1, double x2, double yCenter, double effRadius) {
        std::string label1 = FormatValue(point.value1);
        std::string label2 = FormatValue(point.value2);

        ctx->PushState();
        ctx->SetFontSize(valueLabelFontSize);

        Size2Di size1 = ctx->GetTextLineDimensions(label1);
        Size2Di size2 = ctx->GetTextLineDimensions(label2);

        bool inside;
        switch (valueLabelPlacement) {
            case DumbbellValueLabelPlacement::Inside:  inside = true; break;
            case DumbbellValueLabelPlacement::Outside: inside = false; break;
            default: {
                double widest = std::max(size1.width, size2.width);
                double tallest = std::max(size1.height, size2.height);
                bool fitsInDot = (2.0 * effRadius >= widest + 3.0) && (2.0 * effRadius >= tallest);
                // Two nearly equal values put the dots on top of each other, and the
                // centred labels with them - push those outside instead.
                double needed = (size1.width + size2.width) / 2.0 + 2.0;
                bool labelsWouldCollide = std::abs(x2 - x1) < needed;
                inside = fitsInDot && !labelsWouldCollide;
                break;
            }
        }

        if (inside) {
            ctx->SetTextPaint(Colors::White);
            ctx->DrawText(label1, Point2Dd(x1 - size1.width / 2.0, yCenter - size1.height / 2.0));
            ctx->DrawText(label2, Point2Dd(x2 - size2.width / 2.0, yCenter - size2.height / 2.0));
            ctx->PopState();
            return;
        }

        // Outside: each label sits beyond the outer edge of its own dot, so the two
        // never collide no matter which value is larger.
        const double gap = 4.0;
        bool oneIsLeft = x1 <= x2;

        double leftX = oneIsLeft ? x1 : x2;
        double rightX = oneIsLeft ? x2 : x1;
        const std::string& leftLabel = oneIsLeft ? label1 : label2;
        const std::string& rightLabel = oneIsLeft ? label2 : label1;
        const Size2Di& leftSize = oneIsLeft ? size1 : size2;
        const Size2Di& rightSize = oneIsLeft ? size2 : size1;
        Color leftColor = oneIsLeft ? ResolveColor1(point) : ResolveColor2(point);
        Color rightColor = oneIsLeft ? ResolveColor2(point) : ResolveColor1(point);

        ctx->SetTextPaint(leftColor);
        ctx->DrawText(leftLabel, Point2Dd(leftX - effRadius - gap - leftSize.width,
                                          yCenter - leftSize.height / 2.0));

        ctx->SetTextPaint(rightColor);
        ctx->DrawText(rightLabel, Point2Dd(rightX + effRadius + gap,
                                           yCenter - rightSize.height / 2.0));

        ctx->PopState();
    }

    void UltraCanvasDumbbellChart::RenderLegend(IRenderContext* ctx) {
        ctx->PushState();
        ctx->SetFontSize(10.0f);

        const double dotR = 5.0;
        const double textGap = 6.0;
        const double itemGap = 18.0;

        Size2Di size1 = ctx->GetTextLineDimensions(legendLabel1);
        Size2Di size2 = ctx->GetTextLineDimensions(legendLabel2);

        double totalWidth = dotR * 2 + textGap + size1.width + itemGap +
                            dotR * 2 + textGap + size2.width;
        double rowHeightPx = std::max<double>(dotR * 2, std::max(size1.height, size2.height));

        double x, y;
        switch (legendPosition) {
            case DumbbellLegendPosition::TopLeft:
                x = cachedPlotArea.x;
                y = cachedPlotArea.y - rowHeightPx - 6.0;
                break;
            case DumbbellLegendPosition::BottomLeft:
                x = cachedPlotArea.x;
                y = static_cast<double>(GetHeight()) - rowHeightPx - 6.0;
                break;
            case DumbbellLegendPosition::BottomRight:
                x = cachedPlotArea.GetRight() - totalWidth;
                y = static_cast<double>(GetHeight()) - rowHeightPx - 6.0;
                break;
            case DumbbellLegendPosition::TopRight:
            default:
                x = cachedPlotArea.GetRight() - totalWidth;
                y = cachedPlotArea.y - rowHeightPx - 6.0;
                break;
        }
        x = std::max(4.0, x);
        y = std::max(2.0, y);

        double centerY = y + rowHeightPx / 2.0;

        ctx->DrawFilledCircle(Point2Dd(x + dotR, centerY), static_cast<float>(dotR),
                              defaultColor1, Colors::White, 1.0f);
        ctx->SetTextPaint(Color(50, 50, 50, 255));
        ctx->DrawText(legendLabel1, Point2Dd(x + dotR * 2 + textGap, centerY - size1.height / 2.0));

        double x2 = x + dotR * 2 + textGap + size1.width + itemGap;
        ctx->DrawFilledCircle(Point2Dd(x2 + dotR, centerY), static_cast<float>(dotR),
                              defaultColor2, Colors::White, 1.0f);
        ctx->SetTextPaint(Color(50, 50, 50, 255));
        ctx->DrawText(legendLabel2, Point2Dd(x2 + dotR * 2 + textGap, centerY - size2.height / 2.0));

        ctx->PopState();
    }

// =============================================================================
// INTERACTION
// =============================================================================

    int UltraCanvasDumbbellChart::FindHoveredRow(const Point2Di& mousePos) const {
        if (displayOrder.empty()) return -1;
        if (mousePos.x < cachedPlotArea.x - GetMarginLeft() || mousePos.x > cachedPlotArea.GetRight() + 40) {
            return -1;
        }
        if (mousePos.y < cachedPlotArea.y || mousePos.y > cachedPlotArea.GetBottom()) return -1;

        double pitch = GetRowPitch();
        if (pitch <= 0.0) return -1;

        int index = static_cast<int>((mousePos.y - cachedPlotArea.y) / pitch);
        if (index < 0 || static_cast<size_t>(index) >= displayOrder.size()) return -1;
        return index;
    }

    int UltraCanvasDumbbellChart::FindHoveredDot(const Point2Di& mousePos, int displayIndex) const {
        if (displayIndex < 0 || static_cast<size_t>(displayIndex) >= displayOrder.size()) return -1;

        const auto& point = dumbbellDataSource->GetDumbbellPoint(displayOrder[displayIndex]);
        double yCenter = GetRowCenterY(displayIndex);

        double x1 = ValueToX(point.value1);
        double x2 = ValueToX(point.value2);

        double dy = mousePos.y - yCenter;
        double dx1 = mousePos.x - x1;
        double dx2 = mousePos.x - x2;
        double dist1 = std::sqrt(dx1 * dx1 + dy * dy);
        double dist2 = std::sqrt(dx2 * dx2 + dy * dy);

        // Small dots still need a usable hit target
        double hitRadius = std::max(6.0, GetEffectiveDotRadius() + 4.0);

        if (dist1 > hitRadius && dist2 > hitRadius) return -1;
        return dist1 <= dist2 ? 0 : 1;
    }

// The tooltip manager renders its text as Pango markup, so anything coming from
// the data has to be XML-escaped or a stray '&' silently produces an empty tooltip.
    static std::string DumbbellEscapeMarkup(const std::string& text) {
        std::string escaped;
        escaped.reserve(text.size());
        for (char c : text) {
            switch (c) {
                case '&': escaped += "&amp;"; break;
                case '<': escaped += "&lt;"; break;
                case '>': escaped += "&gt;"; break;
                default:  escaped += c; break;
            }
        }
        return escaped;
    }

    std::string UltraCanvasDumbbellChart::GenerateDumbbellTooltip(const DumbbellDataPoint& point,
                                                                  int dotIndex) const {
        // A caller-supplied tooltip is passed through untouched so it can use markup
        if (!point.tooltipText.empty()) return point.tooltipText;

        std::ostringstream oss;
        oss << DumbbellEscapeMarkup(point.categoryLabel) << "\n"
            << DumbbellEscapeMarkup(point.label1) << ": "
            << DumbbellEscapeMarkup(FormatValue(point.value1)) << "\n"
            << DumbbellEscapeMarkup(point.label2) << ": "
            << DumbbellEscapeMarkup(FormatValue(point.value2)) << "\n"
            << "Difference: " << DumbbellEscapeMarkup(FormatValue(point.GetRange()));

        if (dotIndex == 0 || dotIndex == 1) {
            const std::string& focus = (dotIndex == 0) ? point.label1 : point.label2;
            oss << "\n(" << DumbbellEscapeMarkup(focus) << ")";
        }

        return oss.str();
    }

    bool UltraCanvasDumbbellChart::HandleChartMouseMove(const Point2Di& mousePos) {
        if (!dumbbellDataSource) return false;

        // Hit testing reads the cached plot area, which Render() refreshes. A mouse
        // move can arrive before the first paint, so make sure it is current.
        UpdateRenderingCache();

        int previousRow = hoveredRowIndex;
        int previousDot = hoveredDotIndex;

        hoveredRowIndex = FindHoveredRow(mousePos);
        hoveredDotIndex = (hoveredRowIndex >= 0) ? FindHoveredDot(mousePos, hoveredRowIndex) : -1;

        bool changed = (previousRow != hoveredRowIndex || previousDot != hoveredDotIndex);

        if (enableTooltips) {
            if (hoveredRowIndex >= 0 && hoveredDotIndex >= 0) {
                const auto& point = dumbbellDataSource->GetDumbbellPoint(displayOrder[hoveredRowIndex]);
                std::string tooltipText = GenerateDumbbellTooltip(point, hoveredDotIndex);
                Point2Di windowPos = MapFromLocal(Point2Df(mousePos.x, mousePos.y), nullptr);
                UltraCanvasTooltipManager::UpdateAndShowTooltip(GetWindow(), tooltipText, windowPos);
                isTooltipActive = true;
            } else if (isTooltipActive) {
                UltraCanvasTooltipManager::HideTooltip();
                isTooltipActive = false;
            }
        }

        if (changed) RequestRedraw();
        return changed;
    }

    bool UltraCanvasDumbbellChart::OnEvent(const UCEvent& event) {
        if (IsDisabled() || !IsVisible()) return false;

        if (event.type == UCEventType::MouseLeave) {
            if (hoveredRowIndex != -1 || hoveredDotIndex != -1) {
                hoveredRowIndex = -1;
                hoveredDotIndex = -1;
                RequestRedraw();
            }
            if (isTooltipActive) {
                UltraCanvasTooltipManager::HideTooltip();
                isTooltipActive = false;
            }
            return false;
        }

        if (event.type == UCEventType::MouseDown && event.button == UCMouseButton::Left) {
            UpdateRenderingCache();
            Point2Di mousePos(event.pointer.x, event.pointer.y);
            int row = FindHoveredRow(mousePos);
            if (row >= 0 && static_cast<size_t>(row) < displayOrder.size()) {
                size_t dataIndex = displayOrder[row];
                int dot = FindHoveredDot(mousePos, row);
                if (dot >= 0 && onDotClick) onDotClick(dataIndex, dot);
                if (onRowClick) onRowClick(dataIndex);
            }
        }

        return UltraCanvasChartElementBase::OnEvent(event);
    }

} // namespace UltraCanvas
