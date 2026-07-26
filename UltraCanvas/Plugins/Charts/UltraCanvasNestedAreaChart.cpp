// Plugins/Charts/UltraCanvasNestedAreaChart.cpp
// Nested Proportional Area Chart - Implementation
// Version: 1.1.0
// Last Modified: 2026-07-25
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasNestedAreaChart.h"
#include "UltraCanvasTooltipManager.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace UltraCanvas {

// =============================================================================
// PLOT AREA
// =============================================================================

    ChartPlotArea UltraCanvasNestedAreaChart::CalculatePlotArea() {
        float titleReserve = chartTitle.empty() ? 0.0f : 24.0f;
        float legendReserve = showLegend ? legendHeight : 0.0f;
        float w = std::max(0.0f, GetWidth() - chartPadding * 2);
        float h = std::max(0.0f, GetHeight() - chartPadding * 2 - titleReserve - legendReserve);
        return ChartPlotArea(chartPadding, chartPadding + titleReserve, w, h);
    }

// =============================================================================
// LAYOUT CACHE
// =============================================================================

    std::vector<size_t> UltraCanvasNestedAreaChart::GetSortedIndices(bool descending) const {
        const auto& pts = nestedDataSource->GetPoints();
        std::vector<size_t> indices(pts.size());
        for (size_t i = 0; i < indices.size(); ++i) indices[i] = i;
        std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
            return descending ? pts[a].value > pts[b].value
                              : pts[a].value < pts[b].value;
        });
        return indices;
    }

    void UltraCanvasNestedAreaChart::CalculateShapeCache() {
        shapeCache.shapeBounds.clear();
        shapeCache.shapeRadii.clear();
        shapeCache.labelPositions.clear();

        const auto& pts = nestedDataSource->GetPoints();
        if (pts.empty() || maxDataValue <= 0) {
            shapeCache.isValid = true;
            return;
        }

        float areaX = static_cast<float>(cachedPlotArea.x);
        float areaY = static_cast<float>(cachedPlotArea.y);
        float areaW = static_cast<float>(cachedPlotArea.width);
        float areaH = static_cast<float>(cachedPlotArea.height);
        float maxDimension = std::min(areaW, areaH);

        // Anchor point based on alignment mode
        Point2Df anchor;
        switch (alignmentMode) {
            case NestedAreaAlignmentMode::BottomLeft:
                anchor = Point2Df(areaX, areaY + areaH);
                break;
            case NestedAreaAlignmentMode::BottomRight:
                anchor = Point2Df(areaX + areaW, areaY + areaH);
                break;
            case NestedAreaAlignmentMode::TopLeft:
                anchor = Point2Df(areaX, areaY);
                break;
            case NestedAreaAlignmentMode::TopRight:
                anchor = Point2Df(areaX + areaW, areaY);
                break;
            case NestedAreaAlignmentMode::Center:
                anchor = Point2Df(areaX + areaW / 2, areaY + areaH / 2);
                break;
            case NestedAreaAlignmentMode::BottomCenter:
                anchor = Point2Df(areaX + areaW / 2, areaY + areaH);
                break;
            case NestedAreaAlignmentMode::TopCenter:
                anchor = Point2Df(areaX + areaW / 2, areaY);
                break;
        }

        shapeCache.shapeBounds.resize(pts.size());
        shapeCache.shapeRadii.resize(pts.size());
        shapeCache.labelPositions.resize(pts.size());

        for (size_t dataIdx = 0; dataIdx < pts.size(); ++dataIdx) {
            const auto& point = pts[dataIdx];

            if (shapeMode == NestedAreaShapeMode::Circle) {
                float radius = CalculateRadiusFromValue(point.value, maxDataValue, maxDimension / 2);
                shapeCache.shapeRadii[dataIdx] = radius;

                Rect2Df bounds;
                switch (alignmentMode) {
                    case NestedAreaAlignmentMode::BottomLeft:
                        bounds = Rect2Df(anchor.x, anchor.y - radius * 2, radius * 2, radius * 2);
                        break;
                    case NestedAreaAlignmentMode::BottomRight:
                        bounds = Rect2Df(anchor.x - radius * 2, anchor.y - radius * 2, radius * 2, radius * 2);
                        break;
                    case NestedAreaAlignmentMode::TopLeft:
                        bounds = Rect2Df(anchor.x, anchor.y, radius * 2, radius * 2);
                        break;
                    case NestedAreaAlignmentMode::TopRight:
                        bounds = Rect2Df(anchor.x - radius * 2, anchor.y, radius * 2, radius * 2);
                        break;
                    case NestedAreaAlignmentMode::Center:
                        bounds = Rect2Df(anchor.x - radius, anchor.y - radius, radius * 2, radius * 2);
                        break;
                    case NestedAreaAlignmentMode::BottomCenter:
                        bounds = Rect2Df(anchor.x - radius, anchor.y - radius * 2, radius * 2, radius * 2);
                        break;
                    case NestedAreaAlignmentMode::TopCenter:
                        bounds = Rect2Df(anchor.x - radius, anchor.y, radius * 2, radius * 2);
                        break;
                }
                shapeCache.shapeBounds[dataIdx] = bounds;

                // Label at circle center
                shapeCache.labelPositions[dataIdx] = Point2Df(
                        bounds.x + bounds.width / 2,
                        bounds.y + bounds.height / 2
                );
            } else {
                // Rectangle or RoundedRect mode
                float side = CalculateSideFromValue(point.value, maxDataValue, maxDimension);

                Rect2Df bounds;
                switch (alignmentMode) {
                    case NestedAreaAlignmentMode::BottomLeft:
                        bounds = Rect2Df(anchor.x, anchor.y - side, side, side);
                        break;
                    case NestedAreaAlignmentMode::BottomRight:
                        bounds = Rect2Df(anchor.x - side, anchor.y - side, side, side);
                        break;
                    case NestedAreaAlignmentMode::TopLeft:
                        bounds = Rect2Df(anchor.x, anchor.y, side, side);
                        break;
                    case NestedAreaAlignmentMode::TopRight:
                        bounds = Rect2Df(anchor.x - side, anchor.y, side, side);
                        break;
                    case NestedAreaAlignmentMode::Center:
                        bounds = Rect2Df(anchor.x - side / 2, anchor.y - side / 2, side, side);
                        break;
                    case NestedAreaAlignmentMode::BottomCenter:
                        bounds = Rect2Df(anchor.x - side / 2, anchor.y - side, side, side);
                        break;
                    case NestedAreaAlignmentMode::TopCenter:
                        bounds = Rect2Df(anchor.x - side / 2, anchor.y, side, side);
                        break;
                }
                shapeCache.shapeBounds[dataIdx] = bounds;
                shapeCache.shapeRadii[dataIdx] = side / 2;

                // Label position - near the shape's outer corner so the labels of
                // nested shapes don't overlap each other
                float labelOffsetX = bounds.width * 0.1f;
                float labelOffsetY = bounds.height * 0.1f;

                switch (alignmentMode) {
                    case NestedAreaAlignmentMode::BottomLeft:
                    case NestedAreaAlignmentMode::TopLeft:
                        shapeCache.labelPositions[dataIdx] = Point2Df(
                                bounds.x + bounds.width - labelOffsetX,
                                bounds.y + labelOffsetY
                        );
                        break;
                    case NestedAreaAlignmentMode::BottomRight:
                    case NestedAreaAlignmentMode::TopRight:
                        shapeCache.labelPositions[dataIdx] = Point2Df(
                                bounds.x + labelOffsetX,
                                bounds.y + labelOffsetY
                        );
                        break;
                    default:
                        shapeCache.labelPositions[dataIdx] = Point2Df(
                                bounds.x + bounds.width / 2,
                                bounds.y + bounds.height / 2
                        );
                        break;
                }
            }
        }

        shapeCache.isValid = true;
    }

// =============================================================================
// COLOR MANAGEMENT
// =============================================================================

    Color UltraCanvasNestedAreaChart::GetColorForIndex(size_t index) const {
        const auto& pts = nestedDataSource->GetPoints();
        if (index >= pts.size()) return Color(100, 100, 100, 255);

        // Explicit per-point color override
        const auto& point = pts[index];
        if (point.color.a > 0) {
            return point.color;
        }

        // Custom colors if set
        if (colorTheme == NestedAreaColorTheme::Custom && !customColors.empty()) {
            return customColors[index % customColors.size()];
        }

        // Palette colors follow size rank so the gradient runs from the
        // largest (outer) shape to the smallest (inner) one
        auto palette = NestedAreaColorPalettes::GetPalette(colorTheme);
        if (palette.empty()) {
            return Color(100, 100, 100, 255);
        }
        auto sorted = GetSortedIndices(true);
        size_t rank = 0;
        for (size_t i = 0; i < sorted.size(); ++i) {
            if (sorted[i] == index) { rank = i; break; }
        }
        return palette[rank % palette.size()];
    }

// =============================================================================
// MAIN RENDER METHOD
// =============================================================================

    void UltraCanvasNestedAreaChart::RenderChart(IRenderContext* ctx) {
        if (!ctx) return;

        if (!shapeCache.isValid) {
            CalculateShapeCache();
        }

        // Reference grid (background layer)
        if (showGridLines) {
            RenderGridLines(ctx);
        }

        RenderShapes(ctx);

        if ((showLabels || showValues) &&
            labelPosition != NestedAreaLabelPosition::None &&
            labelPosition != NestedAreaLabelPosition::Tooltip) {
            RenderShapeLabels(ctx);
        }

        if (showLegend) {
            RenderLegend(ctx);
        }
    }

// =============================================================================
// SHAPE RENDERING
// =============================================================================

    static Color ApplyHighlight(const Color& c, float factor) {
        return Color(
                static_cast<uint8_t>(std::min(255, static_cast<int>(c.r * factor))),
                static_cast<uint8_t>(std::min(255, static_cast<int>(c.g * factor))),
                static_cast<uint8_t>(std::min(255, static_cast<int>(c.b * factor))),
                c.a
        );
    }

    void UltraCanvasNestedAreaChart::RenderShapes(IRenderContext* ctx) {
        const auto& pts = nestedDataSource->GetPoints();
        if (pts.empty()) return;

        // Render largest first (back to front)
        for (size_t dataIdx : GetSortedIndices(true)) {
            const Rect2Df& bounds = shapeCache.shapeBounds[dataIdx];
            if (bounds.width <= 0 || bounds.height <= 0) continue;

            Color fillColor = GetColorForIndex(dataIdx);
            if (static_cast<int>(dataIdx) == hoveredIndex) {
                fillColor = ApplyHighlight(fillColor, 1.2f);   // Brighten on hover
            }
            if (static_cast<int>(dataIdx) == selectedIndex) {
                fillColor = ApplyHighlight(fillColor, 0.8f);   // Darken when selected
            }

            Rect2Dd rect(bounds);

            switch (shapeMode) {
                case NestedAreaShapeMode::Rectangle: {
                    ctx->SetFillPaint(fillColor);
                    ctx->FillRectangle(rect);
                    if (showBorders && borderWidth > 0) {
                        ctx->SetStrokePaint(borderColor);
                        ctx->SetStrokeWidth(borderWidth);
                        ctx->DrawRectangle(rect);
                    }
                    if (static_cast<int>(dataIdx) == selectedIndex) {
                        ctx->SetStrokePaint(Colors::Black);
                        ctx->SetStrokeWidth(2.0f);
                        ctx->DrawRectangle(Rect2Dd(rect.x - 2, rect.y - 2,
                                                   rect.width + 4, rect.height + 4));
                    }
                    break;
                }
                case NestedAreaShapeMode::Circle: {
                    float radius = shapeCache.shapeRadii[dataIdx];
                    if (radius <= 0) continue;
                    Point2Dd center(rect.x + rect.width / 2, rect.y + rect.height / 2);
                    ctx->SetFillPaint(fillColor);
                    ctx->FillCircle(center, radius);
                    if (showBorders && borderWidth > 0) {
                        ctx->SetStrokePaint(borderColor);
                        ctx->SetStrokeWidth(borderWidth);
                        ctx->DrawCircle(center, radius);
                    }
                    if (static_cast<int>(dataIdx) == selectedIndex) {
                        ctx->SetStrokePaint(Colors::Black);
                        ctx->SetStrokeWidth(2.0f);
                        ctx->DrawCircle(center, radius + 3);
                    }
                    break;
                }
                case NestedAreaShapeMode::RoundedRect: {
                    double effectiveRadius = std::min(static_cast<double>(cornerRadius),
                                                      std::min(rect.width, rect.height) / 4);
                    ctx->SetFillPaint(fillColor);
                    ctx->FillRoundedRectangle(rect, effectiveRadius);
                    if (showBorders && borderWidth > 0) {
                        ctx->SetStrokePaint(borderColor);
                        ctx->SetStrokeWidth(borderWidth);
                        ctx->DrawRoundedRectangle(rect, effectiveRadius);
                    }
                    if (static_cast<int>(dataIdx) == selectedIndex) {
                        ctx->SetStrokePaint(Colors::Black);
                        ctx->SetStrokeWidth(2.0f);
                        ctx->DrawRoundedRectangle(Rect2Dd(rect.x - 2, rect.y - 2,
                                                          rect.width + 4, rect.height + 4),
                                                  effectiveRadius);
                    }
                    break;
                }
            }
        }
    }

// =============================================================================
// LABELS RENDERING
// =============================================================================

    void UltraCanvasNestedAreaChart::RenderShapeLabels(IRenderContext* ctx) {
        const auto& pts = nestedDataSource->GetPoints();
        ctx->SetFontSize(11.0f);

        // Smallest labels last so they stay readable on top
        for (size_t dataIdx : GetSortedIndices(false)) {
            const auto& point = pts[dataIdx];
            const Rect2Df& bounds = shapeCache.shapeBounds[dataIdx];

            // Skip if shape is too small for labels
            if (bounds.width < 40 || bounds.height < 30) continue;

            std::string nameLine = showLabels ? point.label : std::string();
            std::string valueLine = showValues ? FormatNestedValue(point.value, point.unit)
                                               : std::string();
            if (nameLine.empty() && valueLine.empty()) continue;

            Point2Df labelPos = shapeCache.labelPositions[dataIdx];
            if (labelPosition == NestedAreaLabelPosition::Outside) {
                // Move label outside the shape
                if (alignmentMode == NestedAreaAlignmentMode::BottomLeft ||
                    alignmentMode == NestedAreaAlignmentMode::TopLeft) {
                    labelPos.x = bounds.x + bounds.width + 5;
                } else if (alignmentMode == NestedAreaAlignmentMode::BottomRight ||
                           alignmentMode == NestedAreaAlignmentMode::TopRight) {
                    labelPos.x = bounds.x - 5;
                }
            }

            // Text color based on background brightness
            Color bgColor = GetColorForIndex(dataIdx);
            float brightness = (bgColor.r * 0.299f + bgColor.g * 0.587f + bgColor.b * 0.114f) / 255.0f;
            Color textColor = (labelPosition == NestedAreaLabelPosition::Outside || brightness > 0.5f)
                              ? Colors::Black : Colors::White;
            ctx->SetTextPaint(textColor);

            Size2Di nameSize = nameLine.empty() ? Size2Di(0, 0) : ctx->GetTextLineDimensions(nameLine);
            Size2Di valueSize = valueLine.empty() ? Size2Di(0, 0) : ctx->GetTextLineDimensions(valueLine);
            int totalHeight = nameSize.height + valueSize.height;
            int maxWidth = std::max(nameSize.width, valueSize.width);

            float textY = labelPos.y - totalHeight / 2.0f;

            // Keep inside labels within the shape bounds
            if (labelPosition == NestedAreaLabelPosition::Inside) {
                float minX = bounds.x + 2, maxX = bounds.x + bounds.width - maxWidth - 2;
                float minY = bounds.y + 2, maxY = bounds.y + bounds.height - totalHeight - 2;
                labelPos.x = std::max(minX + maxWidth / 2.0f,
                                      std::min(labelPos.x, maxX + maxWidth / 2.0f));
                textY = std::max(minY, std::min(textY, maxY));
            }

            if (!nameLine.empty()) {
                ctx->DrawText(nameLine, Point2Dd(labelPos.x - nameSize.width / 2.0f, textY));
                textY += nameSize.height;
            }
            if (!valueLine.empty()) {
                ctx->DrawText(valueLine, Point2Dd(labelPos.x - valueSize.width / 2.0f, textY));
            }
        }
    }

// =============================================================================
// LEGEND RENDERING
// =============================================================================

    void UltraCanvasNestedAreaChart::RenderLegend(IRenderContext* ctx) {
        const auto& pts = nestedDataSource->GetPoints();
        if (pts.empty()) return;

        float legendX = chartPadding;
        float legendY = GetHeight() - legendHeight + 5.0f;
        float swatchSize = 12.0f;
        float spacing = 6.0f;
        float itemSpacing = 16.0f;

        ctx->SetFontSize(10.0f);

        float currentX = legendX;

        // Legend follows size order, largest first
        for (size_t dataIdx : GetSortedIndices(true)) {
            const auto& point = pts[dataIdx];
            Color color = GetColorForIndex(dataIdx);

            std::string legendText = point.label;
            if (showValues) {
                legendText += " (" + FormatNestedValue(point.value, point.unit) + ")";
            }
            Size2Di txtSize = ctx->GetTextLineDimensions(legendText);

            // Wrap to next line if this item won't fit
            if (currentX > legendX &&
                currentX + swatchSize + spacing + txtSize.width > GetWidth() - chartPadding) {
                currentX = legendX;
                legendY += swatchSize + 5;
            }

            ctx->SetFillPaint(color);
            ctx->FillRectangle(Rect2Dd(currentX, legendY, swatchSize, swatchSize));
            if (showBorders) {
                ctx->SetStrokePaint(borderColor);
                ctx->SetStrokeWidth(1.0f);
                ctx->DrawRectangle(Rect2Dd(currentX, legendY, swatchSize, swatchSize));
            }

            ctx->SetTextPaint(Color(60, 60, 60, 255));
            ctx->DrawText(legendText, Point2Dd(currentX + swatchSize + spacing, legendY));

            currentX += swatchSize + spacing + txtSize.width + itemSpacing;
        }
    }

// =============================================================================
// GRID LINES RENDERING
// =============================================================================

    void UltraCanvasNestedAreaChart::RenderGridLines(IRenderContext* ctx) {
        float areaX = static_cast<float>(cachedPlotArea.x);
        float areaY = static_cast<float>(cachedPlotArea.y);
        float areaW = static_cast<float>(cachedPlotArea.width);
        float areaH = static_cast<float>(cachedPlotArea.height);
        float maxDimension = std::min(areaW, areaH);

        ctx->SetStrokePaint(gridLineColor);
        ctx->SetStrokeWidth(0.5f);

        // Reference squares/circles showing the area scale
        for (int i = 1; i <= gridLineCount; ++i) {
            float fraction = static_cast<float>(i) / gridLineCount;
            float size = maxDimension * std::sqrt(fraction);  // sqrt for area scaling

            float gridX, gridY;
            switch (alignmentMode) {
                case NestedAreaAlignmentMode::BottomLeft:
                    gridX = areaX;
                    gridY = areaY + areaH - size;
                    break;
                case NestedAreaAlignmentMode::BottomRight:
                    gridX = areaX + areaW - size;
                    gridY = areaY + areaH - size;
                    break;
                case NestedAreaAlignmentMode::TopLeft:
                    gridX = areaX;
                    gridY = areaY;
                    break;
                case NestedAreaAlignmentMode::TopRight:
                    gridX = areaX + areaW - size;
                    gridY = areaY;
                    break;
                default:
                    gridX = areaX + (areaW - size) / 2;
                    gridY = areaY + (areaH - size) / 2;
                    break;
            }

            if (shapeMode == NestedAreaShapeMode::Circle) {
                float radius = size / 2;
                ctx->DrawCircle(Point2Dd(gridX + radius, gridY + radius), radius);
            } else {
                ctx->DrawRectangle(Rect2Dd(gridX, gridY, size, size));
            }
        }
    }

// =============================================================================
// HIT TESTING
// =============================================================================

    int UltraCanvasNestedAreaChart::HitTest(const Point2Di& pos) const {
        if (!shapeCache.isValid) return -1;
        const auto& pts = nestedDataSource->GetPoints();
        if (pts.empty() || shapeCache.shapeBounds.size() != pts.size()) return -1;

        // Test smallest shapes first, as they're rendered on top
        for (size_t dataIdx : GetSortedIndices(false)) {
            const Rect2Df& bounds = shapeCache.shapeBounds[dataIdx];

            if (shapeMode == NestedAreaShapeMode::Circle) {
                float centerX = bounds.x + bounds.width / 2;
                float centerY = bounds.y + bounds.height / 2;
                float radius = shapeCache.shapeRadii[dataIdx];
                float dx = pos.x - centerX;
                float dy = pos.y - centerY;
                if (dx * dx + dy * dy <= radius * radius) {
                    return static_cast<int>(dataIdx);
                }
            } else {
                if (pos.x >= bounds.x && pos.x <= bounds.x + bounds.width &&
                    pos.y >= bounds.y && pos.y <= bounds.y + bounds.height) {
                    return static_cast<int>(dataIdx);
                }
            }
        }
        return -1;
    }

// =============================================================================
// EVENT HANDLING
// =============================================================================

    bool UltraCanvasNestedAreaChart::HandleChartMouseMove(const Point2Di& mousePos) {
        int newHovered = HitTest(mousePos);

        if (newHovered != hoveredIndex) {
            hoveredIndex = newHovered;

            if (hoveredIndex >= 0 && onShapeHover) {
                onShapeHover(static_cast<size_t>(hoveredIndex));
            }

            if (enableTooltips && hoveredIndex >= 0) {
                UpdateNestedTooltip(mousePos);
            } else {
                UltraCanvasTooltipManager::HideTooltip();
            }

            RequestRedraw();
            return true;
        }

        return false;
    }

    bool UltraCanvasNestedAreaChart::OnEvent(const UCEvent& event) {
        if (IsDisabled() || !IsVisible()) return false;

        switch (event.type) {
            case UCEventType::MouseDown: {
                if (event.button == UCMouseButton::Left) {
                    int clicked = HitTest(Point2Di(event.pointer.x, event.pointer.y));
                    if (clicked >= 0) {
                        if (onShapeClick) {
                            onShapeClick(static_cast<size_t>(clicked));
                        }
                        // Toggle selection
                        if (selectedIndex == clicked) {
                            ClearSelection();
                        } else {
                            SetSelectedIndex(clicked);
                        }
                        return true;
                    }
                }
                break;
            }
            case UCEventType::MouseLeave: {
                if (hoveredIndex >= 0) {
                    hoveredIndex = -1;
                    UltraCanvasTooltipManager::HideTooltip();
                    RequestRedraw();
                }
                break;
            }
            default:
                break;
        }

        return UltraCanvasChartElementBase::OnEvent(event);
    }

    void UltraCanvasNestedAreaChart::UpdateNestedTooltip(const Point2Di& mousePos) {
        if (hoveredIndex < 0) return;
        std::string tooltipText = GenerateNestedTooltip(static_cast<size_t>(hoveredIndex));
        if (tooltipText.empty()) return;

        auto mouseGlobalPos = MapFromLocal(mousePos, nullptr);
        UltraCanvasTooltipManager::UpdateAndShowTooltip(GetWindow(), tooltipText, mouseGlobalPos);
    }

// =============================================================================
// UTILITY METHODS
// =============================================================================

    std::string UltraCanvasNestedAreaChart::FormatNestedValue(double value, const std::string& unit) const {
        std::ostringstream oss;

        if (value >= 1000000000) {
            oss << std::fixed << std::setprecision(1) << (value / 1000000000.0) << "B";
        } else if (value >= 1000000) {
            oss << std::fixed << std::setprecision(1) << (value / 1000000.0) << "M";
        } else if (value >= 1000) {
            oss << std::fixed << std::setprecision(1) << (value / 1000.0) << "K";
        } else if (value >= 100) {
            oss << std::fixed << std::setprecision(0) << value;
        } else {
            oss << std::fixed << std::setprecision(2) << value;
        }

        if (!unit.empty()) {
            oss << " " << unit;
        }

        return oss.str();
    }

    std::string UltraCanvasNestedAreaChart::GenerateNestedTooltip(size_t index) const {
        const auto& pts = nestedDataSource->GetPoints();
        if (index >= pts.size()) return "";

        const auto& point = pts[index];

        // Use custom tooltip if provided
        if (!point.tooltip.empty()) {
            return point.tooltip;
        }

        std::ostringstream oss;
        if (!point.label.empty()) {
            oss << point.label << "\n";
        }
        if (!point.category.empty()) {
            oss << "Category: " << point.category << "\n";
        }
        oss << "Value: " << FormatNestedValue(point.value, point.unit);

        if (maxDataValue > 0) {
            double percentage = (point.value / maxDataValue) * 100.0;
            oss << "\n(" << std::fixed << std::setprecision(1) << percentage << "% of max)";
        }

        return oss.str();
    }

} // namespace UltraCanvas
