// Plugins/Charts/UltraCanvasSpecificChartElements.cpp
// Specific chart element implementations with aligned X-axis positioning
// Version: 1.3.0 - bars: the value axis reaches 0 before it is padded; a
//                  horizontal orientation; a fixed value range; value labels
// Version: 1.2.0 - zoomed line/area/scatter clip to the plot and hover only
//                  points in view; bars rise from the zero line
// Version: 1.1.0
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasSpecificChartElements.h"
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace UltraCanvas {

// =============================================================================
// LINE CHART IMPLEMENTATION
// =============================================================================

    void UltraCanvasLineChartElement::RenderChart(IRenderContext* ctx) {
        if (!ctx || !dataSource || dataSource->GetPointCount() == 0) return;

        // Set line style
        ctx->SetStrokePaint(lineColor);
        ctx->SetStrokeWidth(lineWidth);

        std::vector<Point2Dd> linePoints;

        for (size_t i = 0; i < dataSource->GetPointCount(); ++i) {
            auto point = dataSource->GetPoint(i);

            // Use the new positioning method that respects label mode
            Point2Dd screenPos = GetDataPointScreenPosition(i, point);
            linePoints.push_back(screenPos);
        }

        bool clipped = PushXAxisViewClip(ctx);

        // Draw the line
        if (enableSmoothing && linePoints.size() > 2) {
            DrawSmoothLine(ctx, linePoints);
        } else {
            // Draw straight line segments
            for (size_t i = 1; i < linePoints.size(); ++i) {
                ctx->DrawLine({linePoints[i-1].x, linePoints[i-1].y},
                              {linePoints[i].x, linePoints[i].y});
            }
        }

        // Draw data points if enabled
        if (showDataPoints) {
            ctx->SetFillPaint(pointColor);
            for (const auto& screenPos : linePoints) {
                ctx->FillCircle(screenPos, pointRadius);
            }
        }

        if (showValueLabels) {
            RenderValueLabels(ctx, linePoints);
        }

        if (clipped) ctx->PopState();
    }

    void UltraCanvasLineChartElement::DrawSmoothLine(IRenderContext* ctx, const std::vector<Point2Dd>& points) {
        if (points.size() < 3) {
            // Not enough points for smoothing
            for (size_t i = 1; i < points.size(); ++i) {
                ctx->DrawLine(points[i-1], points[i]);
            }
            return;
        }

        // Simple Catmull-Rom spline interpolation
        for (size_t i = 0; i < points.size() - 1; ++i) {
            Point2Dd p0 = (i > 0) ? points[i-1] : points[i];
            Point2Dd p1 = points[i];
            Point2Dd p2 = points[i+1];
            Point2Dd p3 = (i < points.size() - 2) ? points[i+2] : points[i+1];

            // Draw interpolated curve segment
            int steps = 20;
            Point2Dd prevPoint = p1;

            for (int step = 1; step <= steps; ++step) {
                double t = step / static_cast<double>(steps);
                double t2 = t * t;
                double t3 = t2 * t;

                double x = 0.5f * ((2 * p1.x) +
                                  (-p0.x + p2.x) * t +
                                  (2*p0.x - 5*p1.x + 4*p2.x - p3.x) * t2 +
                                  (-p0.x + 3*p1.x - 3*p2.x + p3.x) * t3);

                double y = 0.5f * ((2 * p1.y) +
                                  (-p0.y + p2.y) * t +
                                  (2*p0.y - 5*p1.y + 4*p2.y - p3.y) * t2 +
                                  (-p0.y + 3*p1.y - 3*p2.y + p3.y) * t3);

                Point2Dd currentPoint(x, y);
                ctx->DrawLine(prevPoint, currentPoint);
                prevPoint = currentPoint;
            }
        }
    }

    bool UltraCanvasLineChartElement::HandleChartMouseMove(const Point2Di& mousePos) {
        if (!dataSource || !enableTooltips) return false;

        // Find nearest data point
        double minDistance = 20.0f; // Threshold distance in pixels
        size_t nearestIndex = SIZE_MAX;

        for (size_t i = 0; i < dataSource->GetPointCount(); ++i) {
            auto point = dataSource->GetPoint(i);
            Point2Dd screenPos = GetDataPointScreenPosition(i, point);
            if (!IsScreenXInView(screenPos.x)) continue;   // zoomed out of the plot

            double dx = mousePos.x - screenPos.x;
            double dy = mousePos.y - screenPos.y;
            double distance = std::sqrt(dx * dx + dy * dy);

            if (distance < minDistance) {
                minDistance = distance;
                nearestIndex = i;
            }
        }

        if (nearestIndex != SIZE_MAX) {
            auto point = dataSource->GetPoint(nearestIndex);
            ShowChartPointTooltip(mousePos, point, nearestIndex);
            return true;
        } else if (isTooltipActive) {
            HideTooltip();
        }

        return false;
    }

// =============================================================================
// BAR CHART IMPLEMENTATION
// =============================================================================

    double UltraCanvasBarChartElement::ValueToScreen(double value) const {
        const double lo = cachedDataBounds.minY, hi = cachedDataBounds.maxY;
        const double t = hi > lo ? (value - lo) / (hi - lo) : 0.5;
        if (orientation == BarChartOrientation::Horizontal) {
            return cachedPlotArea.x + t * cachedPlotArea.width;
        }
        return cachedPlotArea.GetBottom() - t * cachedPlotArea.height;
    }

    Rect2Dd UltraCanvasBarChartElement::GetBarRect(size_t index) {
        UpdateRenderingCache();
        if (!dataSource || index >= dataSource->GetPointCount()) return Rect2Dd();
        const size_t count = dataSource->GetPointCount();
        const double value = dataSource->GetPoint(index).y;
        // The zero line, kept inside the plot when a value range leaves 0 out.
        const double lo = cachedDataBounds.minY, hi = cachedDataBounds.maxY;
        const double base = ValueToScreen(std::clamp(0.0, lo, hi));
        const double end = ValueToScreen(std::clamp(value, lo, hi));

        if (orientation == BarChartOrientation::Horizontal) {
            // One row per category, first at the top.
            const double row = cachedPlotArea.height / static_cast<double>(count);
            const double gap = row * barSpacing;
            const double y = cachedPlotArea.y + index * row + gap / 2;
            return Rect2Dd(std::min(base, end), y, std::abs(end - base), row - gap);
        }
        const double slot = cachedPlotArea.width / static_cast<double>(count);
        const double gap = slot * barSpacing;
        const double x = cachedPlotArea.x + index * slot + gap / 2;
        return Rect2Dd(x, std::min(base, end), slot - gap, std::abs(end - base));
    }

    void UltraCanvasBarChartElement::RenderChart(IRenderContext* ctx) {
        if (!ctx || !dataSource || dataSource->GetPointCount() == 0) return;

        const size_t pointCount = dataSource->GetPointCount();
        for (size_t i = 0; i < pointCount; ++i) {
            const Rect2Dd bar = GetBarRect(i);
            if (bar.width <= 0 && bar.height <= 0) continue;

            // A point's own colour, where it has one, wins over the series'.
            const Color fill = dataSource->GetPoint(i).color.a > 0 ? dataSource->GetPoint(i).color : barColor;
            ctx->SetFillPaint(fill);
            ctx->FillRectangle(bar);

            if (barBorderWidth > 0) {
                ctx->SetStrokePaint(barBorderColor);
                ctx->SetStrokeWidth(barBorderWidth);
                ctx->DrawRectangle(bar);
            }
        }

        if (showValueLabels) {
            ctx->SetTextPaint(valueLabelColor);
            ctx->SetFontSize(valueLabelFontSize);
            for (size_t i = 0; i < pointCount; ++i) {
                DrawBarValueLabel(ctx, GetBarRect(i), dataSource->GetPoint(i).y);
            }
        }
    }

    void UltraCanvasBarChartElement::DrawBarValueLabel(IRenderContext* ctx, const Rect2Dd& bar, double value) {
        // Past the bar's end: above a rising bar, below a hanging one, right
        // of a bar running right, left of one running left.
        const std::string text = FormatAxisLabel(value);
        const Size2Di size = ctx->GetTextLineDimensions(text);
        const double gap = 4.0;
        Point2Dd at;
        if (orientation == BarChartOrientation::Horizontal) {
            const double y = bar.y + (bar.height - size.height) / 2.0;
            at = value >= 0 ? Point2Dd(bar.x + bar.width + gap, y)
                            : Point2Dd(bar.x - gap - size.width, y);
        } else {
            const double x = bar.x + (bar.width - size.width) / 2.0;
            at = value >= 0 ? Point2Dd(x, bar.y - gap - size.height)
                            : Point2Dd(x, bar.y + bar.height + gap);
        }
        ctx->DrawText(text, at);
    }

    bool UltraCanvasBarChartElement::HandleChartMouseMove(const Point2Di& mousePos) {
        if (!dataSource || !enableTooltips) return false;

        const size_t pointCount = dataSource->GetPointCount();
        if (pointCount == 0) return false;

        // The whole slot (column or row) of a bar answers, not just the bar:
        // a short bar is still easy to point at.
        for (size_t i = 0; i < pointCount; ++i) {
            bool inSlot;
            if (orientation == BarChartOrientation::Horizontal) {
                const double row = cachedPlotArea.height / static_cast<double>(pointCount);
                const double y = cachedPlotArea.y + i * row;
                inSlot = mousePos.y >= y && mousePos.y <= y + row &&
                         mousePos.x >= cachedPlotArea.x && mousePos.x <= cachedPlotArea.GetRight();
            } else {
                const double slot = cachedPlotArea.width / static_cast<double>(pointCount);
                const double x = cachedPlotArea.x + i * slot;
                inSlot = mousePos.x >= x && mousePos.x <= x + slot;
            }
            if (inSlot) {
                auto point = dataSource->GetPoint(i);
                ShowChartPointTooltip(mousePos, point, i);
                return true;
            }
        }

        if (isTooltipActive) {
            HideTooltip();
        }

        return false;
    }

    ChartDataBounds UltraCanvasBarChartElement::CalculateDataBounds() {
        ChartDataBounds bounds = CalculateRawDataBounds();
        if (!bounds.hasData) return bounds;
        PadDataBounds(bounds, true, false, false);   // x as every chart; the value axis below

        if (hasValueRange) {
            bounds.minY = valueRangeMin;
            bounds.maxY = valueRangeMax;
            return bounds;
        }

        // A bar's length is its value, so the value axis must reach 0. That
        // has to happen before the padding: padded first, a series whose
        // smallest value is 0 had its axis start at -5 % of the range, and
        // the zero line floated above the bottom of the plot.
        double lo = std::min(bounds.minY, 0.0);
        double hi = std::max(bounds.maxY, 0.0);
        if (hi == lo) hi = lo + 1.0;   // every value 0
        const double range = hi - lo;
        // Padding only away from zero, then out to the next whole tick, so
        // the last grid line is the plot's edge.
        if (hi > 0.0) hi += range * 0.05;
        if (lo < 0.0) lo -= range * 0.05;
        const double step = ChartNiceStep(hi - lo, GetYAxisTickTarget());
        if (step > 0.0) {
            if (hi > 0.0) hi = std::ceil(hi / step - 1e-9) * step;
            if (lo < 0.0) lo = std::floor(lo / step + 1e-9) * step;
        }
        bounds.minY = lo;
        bounds.maxY = hi;
        return bounds;
    }

    ChartPlotArea UltraCanvasBarChartElement::CalculatePlotArea() {
        if (orientation != BarChartOrientation::Horizontal) {
            return UltraCanvasChartElementBase::CalculatePlotArea();
        }
        // Room for the longest category name, estimated from its length (the
        // plot is worked out before anything is measured): about 6.5 px a
        // character at the 10 px label size, within a fifth to two fifths of
        // the width.
        size_t longest = 0;
        if (dataSource) {
            for (size_t i = 0; i < dataSource->GetPointCount(); ++i) {
                longest = std::max(longest, dataSource->GetPoint(i).label.size());
            }
        }
        const double width = GetWidth();
        const double left = std::clamp(16.0 + 6.5 * static_cast<double>(longest), width * 0.2, width * 0.4);
        const double right = showValueLabels ? 56.0 : 20.0;   // value labels past the longest bar
        const double top = chartTitle.empty() ? 16.0 : 40.0;
        const double bottom = 34.0;
        return ChartPlotArea(static_cast<float>(left), static_cast<float>(top),
                             static_cast<float>(std::max(10.0, width - left - right)),
                             static_cast<float>(std::max(10.0, GetHeight() - top - bottom)));
    }

    void UltraCanvasBarChartElement::RenderGrid(IRenderContext* ctx) {
        ctx->SetStrokePaint(gridColor);
        ctx->SetStrokeWidth(1.0f);
        for (double value : GetYAxisTicks().values) {
            const double at = ValueToScreen(value);
            if (orientation == BarChartOrientation::Horizontal) {
                if (at <= cachedPlotArea.x + 0.5 || at >= cachedPlotArea.GetRight() - 0.5) continue;
                ctx->DrawLine({at, cachedPlotArea.y}, {at, cachedPlotArea.GetBottom()});
            } else {
                if (at <= cachedPlotArea.y + 0.5 || at >= cachedPlotArea.GetBottom() - 0.5) continue;
                ctx->DrawLine({cachedPlotArea.x, at}, {cachedPlotArea.GetRight(), at});
            }
        }
    }

    void UltraCanvasBarChartElement::RenderAxisLabels(IRenderContext* ctx) {
        if (orientation != BarChartOrientation::Horizontal) {
            UltraCanvasChartElementBase::RenderAxisLabels(ctx);
            return;
        }
        if (!ctx || !dataSource || dataSource->GetPointCount() == 0) return;

        ctx->SetStrokePaint(Color(0, 0, 0, 255));
        ctx->SetTextPaint(Color(0, 0, 0, 255));
        ctx->SetFontSize(10.0f);

        // The categories down the left, each centred on its row and ending
        // just short of the axis.
        const size_t count = dataSource->GetPointCount();
        const double row = cachedPlotArea.height / static_cast<double>(count);
        for (size_t i = 0; i < count; ++i) {
            const auto point = dataSource->GetPoint(i);
            const std::string label = point.label.empty() ? FormatAxisLabel(point.x) : point.label;
            const Size2Di size = ctx->GetTextLineDimensions(label);
            const double y = cachedPlotArea.y + (i + 0.5) * row;
            ctx->DrawLine({cachedPlotArea.x - 5, y}, {cachedPlotArea.x, y});
            ctx->DrawText(label, Point2Dd(cachedPlotArea.x - 8 - size.width, y - size.height / 2.0));
        }

        // The values along the bottom, at whole steps.
        const ChartAxisTicks ticks = GetYAxisTicks();
        const double bottom = cachedPlotArea.GetBottom();
        for (double value : ticks.values) {
            const double x = ValueToScreen(value);
            ctx->DrawLine({x, bottom}, {x, bottom + 5});
            const std::string label = ticks.Format(value);
            const Size2Di size = ctx->GetTextLineDimensions(label);
            ctx->DrawText(label, Point2Dd(x - size.width / 2.0, bottom + 8));
        }
    }

    double UltraCanvasBarChartElement::GetXAxisLabelPosition(size_t dataIndex, size_t totalPoints) {
        // For bar charts, center the label under each bar
        double barWidth = cachedPlotArea.width / totalPoints;
        return cachedPlotArea.x + (dataIndex * barWidth) + (barWidth / 2);
    }

// =============================================================================
// SCATTER PLOT IMPLEMENTATION
// =============================================================================

    void UltraCanvasScatterPlotElement::RenderChart(IRenderContext* ctx) {
        if (!ctx || !dataSource || dataSource->GetPointCount() == 0) return;

        bool clipped = PushXAxisViewClip(ctx);

        if (showTrendLine) {
            RenderTrendLine(ctx);
        }

        ctx->SetFillPaint(pointColor);
        ctx->SetStrokePaint(pointColor);
        ctx->SetStrokeWidth(1.5f);

        for (size_t i = 0; i < dataSource->GetPointCount(); ++i) {
            auto point = dataSource->GetPoint(i);

            // A point may carry its own colour (e.g. to mark outliers or
            // distinguish series); fall back to the element colour otherwise.
            ctx->SetFillPaint(point.color.a > 0 ? point.color : pointColor);

            // Use the new positioning method
            Point2Dd screenPos = GetDataPointScreenPosition(i, point);

            // Draw point based on shape
            switch (pointShape) {
                case PointShape::Circle:
                    ctx->FillCircle(screenPos, pointSize);
                    break;

                case PointShape::Square: {
                    double halfSize = pointSize;
                    ctx->FillRectangle(Rect2Dd(screenPos.x - halfSize, screenPos.y - halfSize,
                                       halfSize * 2, halfSize * 2));
                    break;
                }

                case PointShape::Triangle: {
                    std::vector<Point2Dd> triangle = {
                            Point2Dd(screenPos.x, screenPos.y - pointSize),
                            Point2Dd(screenPos.x - pointSize, screenPos.y + pointSize),
                            Point2Dd(screenPos.x + pointSize, screenPos.y + pointSize)
                    };
                    ctx->FillLinePath(triangle);
                    break;
                }

                case PointShape::Diamond: {
                    std::vector<Point2Dd> diamond = {
                            Point2Dd(screenPos.x, screenPos.y - pointSize),
                            Point2Dd(screenPos.x + pointSize, screenPos.y),
                            Point2Dd(screenPos.x, screenPos.y + pointSize),
                            Point2Dd(screenPos.x - pointSize, screenPos.y)
                    };
                    ctx->FillLinePath(diamond);
                    break;
                }
            }
        }

        if (clipped) ctx->PopState();
    }

    bool UltraCanvasScatterPlotElement::ComputeLinearRegression(double& slope, double& intercept) const {
        slope = 0.0;
        intercept = 0.0;
        if (!dataSource) return false;

        size_t n = dataSource->GetPointCount();
        if (n < 2) return false;

        double sumX = 0.0, sumY = 0.0;
        for (size_t i = 0; i < n; ++i) {
            auto point = dataSource->GetPoint(i);
            sumX += point.x;
            sumY += point.y;
        }
        double meanX = sumX / n;
        double meanY = sumY / n;

        double covXY = 0.0, varX = 0.0;
        for (size_t i = 0; i < n; ++i) {
            auto point = dataSource->GetPoint(i);
            double dx = point.x - meanX;
            covXY += dx * (point.y - meanY);
            varX += dx * dx;
        }

        if (varX <= 0.0) return false;   // vertical column of points - no fit

        slope = covXY / varX;
        intercept = meanY - slope * meanX;
        return true;
    }

    double UltraCanvasScatterPlotElement::GetCorrelationCoefficient() const {
        if (!dataSource) return 0.0;

        size_t n = dataSource->GetPointCount();
        if (n < 2) return 0.0;

        double sumX = 0.0, sumY = 0.0;
        for (size_t i = 0; i < n; ++i) {
            auto point = dataSource->GetPoint(i);
            sumX += point.x;
            sumY += point.y;
        }
        double meanX = sumX / n;
        double meanY = sumY / n;

        double covXY = 0.0, varX = 0.0, varY = 0.0;
        for (size_t i = 0; i < n; ++i) {
            auto point = dataSource->GetPoint(i);
            double dx = point.x - meanX;
            double dy = point.y - meanY;
            covXY += dx * dy;
            varX += dx * dx;
            varY += dy * dy;
        }

        double denom = std::sqrt(varX * varY);
        if (denom <= 0.0) return 0.0;
        return covXY / denom;
    }

    void UltraCanvasScatterPlotElement::RenderTrendLine(IRenderContext* ctx) {
        // The fit is over numeric x values; with index-based (categorical)
        // positioning the x axis carries no metric, so a fit is meaningless.
        if (IsUsingIndexBasedPositioning()) return;

        double slope, intercept;
        if (!ComputeLinearRegression(slope, intercept)) return;

        // Span the full visible x range so the line reads as a reference line
        // rather than a segment between the extreme points.
        ChartCoordinateTransform transform(cachedPlotArea, cachedDataBounds);
        Point2Dd p0 = transform.DataToScreen(cachedDataBounds.minX,
                                             slope * cachedDataBounds.minX + intercept);
        Point2Dd p1 = transform.DataToScreen(cachedDataBounds.maxX,
                                             slope * cachedDataBounds.maxX + intercept);

        ctx->PushState();
        ctx->ClipRect(cachedPlotArea.ToRect2D());
        ctx->SetStrokePaint(trendLineColor);
        ctx->SetStrokeWidth(trendLineWidth);
        switch (trendLineStyle) {
            case TrendLineStyle::Dashed:
                ctx->SetLineDash(UCDashPattern({6.0, 4.0}));
                break;
            case TrendLineStyle::Dotted:
                ctx->SetLineDash(UCDashPattern({1.5, 3.5}));
                break;
            case TrendLineStyle::Solid:
                break;
        }
        ctx->DrawLine(p0, p1);
        ctx->SetLineDash(UCDashPattern::EMPTY);
        ctx->PopState();

        if (showCorrelationInfo) {
            RenderCorrelationInfo(ctx, slope, intercept);
        }
    }

    void UltraCanvasScatterPlotElement::RenderCorrelationInfo(IRenderContext* ctx,
                                                              double slope, double intercept) {
        double r = GetCorrelationCoefficient();

        char equation[96];
        std::snprintf(equation, sizeof(equation), "y = %.4gx %c %.4g",
                      slope, intercept < 0.0 ? '-' : '+', std::fabs(intercept));
        char correlation[64];
        std::snprintf(correlation, sizeof(correlation), "r = %.3f   r² = %.3f", r, r * r);

        ctx->SetFontSize(correlationInfoFontSize);
        ctx->SetTextPaint(correlationInfoColor);

        Size2Di eqSize = ctx->GetTextLineDimensions(equation);
        Size2Di corrSize = ctx->GetTextLineDimensions(correlation);
        double width = std::max(eqSize.width, corrSize.width);
        double x = cachedPlotArea.GetRight() - width - 8.0;
        double y = cachedPlotArea.y + 6.0;

        ctx->DrawText(equation, Point2Dd(x, y));
        ctx->DrawText(correlation, Point2Dd(x, y + eqSize.height + 2.0));
    }

    bool UltraCanvasScatterPlotElement::HandleChartMouseMove(const Point2Di& mousePos) {
        if (!dataSource || !enableTooltips) return false;

        // Find nearest point
        double minDistance = pointSize + 5.0f; // Threshold based on point size
        size_t nearestIndex = SIZE_MAX;

        for (size_t i = 0; i < dataSource->GetPointCount(); ++i) {
            auto point = dataSource->GetPoint(i);
            Point2Dd screenPos = GetDataPointScreenPosition(i, point);
            if (!IsScreenXInView(screenPos.x)) continue;   // zoomed out of the plot

            double dx = mousePos.x - screenPos.x;
            double dy = mousePos.y - screenPos.y;
            double distance = std::sqrt(dx * dx + dy * dy);

            if (distance < minDistance) {
                minDistance = distance;
                nearestIndex = i;
            }
        }

        if (nearestIndex != SIZE_MAX) {
            auto point = dataSource->GetPoint(nearestIndex);
            ShowChartPointTooltip(mousePos, point, nearestIndex);
            return true;
        } else if (isTooltipActive) {
            HideTooltip();
        }

        return false;
    }

// =============================================================================
// AREA CHART IMPLEMENTATION
// =============================================================================

    std::vector<Point2Dd> CalculateSmoothPath(const std::vector<Point2Dd>& points) {
        std::vector<Point2Dd> newpoints;
        if (points.size() < 3) {
            newpoints = points;
            return newpoints;
        }

        newpoints.push_back(points[0]);

        // Simple Catmull-Rom spline interpolation
        for (size_t i = 0; i < points.size() - 1; ++i) {
            Point2Dd p0 = (i > 0) ? points[i-1] : points[i];
            Point2Dd p1 = points[i];
            Point2Dd p2 = points[i+1];
            Point2Dd p3 = (i < points.size() - 2) ? points[i+2] : points[i+1];

            // Draw interpolated curve segment
            int steps = 20;
//            Point2Dd prevPoint = p1;

            for (int step = 1; step <= steps; ++step) {
                double t = step / static_cast<double>(steps);
                double t2 = t * t;
                double t3 = t2 * t;

                double x = 0.5f * ((2 * p1.x) +
                                  (-p0.x + p2.x) * t +
                                  (2*p0.x - 5*p1.x + 4*p2.x - p3.x) * t2 +
                                  (-p0.x + 3*p1.x - 3*p2.x + p3.x) * t3);

                double y = 0.5f * ((2 * p1.y) +
                                  (-p0.y + p2.y) * t +
                                  (2*p0.y - 5*p1.y + 4*p2.y - p3.y) * t2 +
                                  (-p0.y + 3*p1.y - 3*p2.y + p3.y) * t3);

                newpoints.emplace_back(x, y);
            }
        }
        return newpoints;
    }


    void UltraCanvasAreaChartElement::RenderChart(IRenderContext* ctx) {
        if (!ctx || !dataSource || dataSource->GetPointCount() == 0) return;

        std::vector<Point2Dd> areaPoints;
        std::vector<Point2Dd> smoothedAreaPoints;

        // Build the area polygon
        for (size_t i = 0; i < dataSource->GetPointCount(); ++i) {
            auto point = dataSource->GetPoint(i);
            Point2Dd screenPos = GetDataPointScreenPosition(i, point);
            areaPoints.push_back(screenPos);
        }

        // Add bottom points to close the area
        ChartCoordinateTransform transform(cachedPlotArea, cachedDataBounds);
        auto bottomY = transform.DataToScreen(0, cachedDataBounds.minY).y;

        if (enableSmoothing) {
            smoothedAreaPoints = CalculateSmoothPath(areaPoints);
        } else {
            smoothedAreaPoints = areaPoints;
        }
        // Add bottom-right corner
        smoothedAreaPoints.push_back(Point2Dd(areaPoints.back().x, bottomY));

        // Add bottom-left corner
        smoothedAreaPoints.push_back(Point2Dd(areaPoints.front().x, bottomY));

        bool clipped = PushXAxisViewClip(ctx);

        // Fill the area
        if (enableGradientFill) {
            // Create gradient
            double minY = areaPoints[0].y;
            double maxY = areaPoints[0].y;

            for (const auto& point : areaPoints) {
                minY = std::min(minY, point.y);
                maxY = std::max(maxY, point.y);
            }

            auto gradient = ctx->CreateLinearGradientPattern(
                    0, minY, 0, maxY,
                    {GradientStop(0, gradientStartColor),
                     GradientStop(1, gradientEndColor)}
            );
            ctx->SetFillPaint(gradient);
        } else {
            ctx->SetFillPaint(fillColor);
        }

        ctx->FillLinePath(smoothedAreaPoints);

        // Draw the line on top
        ctx->SetStrokePaint(lineColor);
        ctx->SetStrokeWidth(lineWidth);

        for (size_t i = 1; i < smoothedAreaPoints.size() - 2; ++i) {
            ctx->DrawLine(smoothedAreaPoints[i-1],
                          smoothedAreaPoints[i]);
        }

        // Draw data points if enabled
        if (showDataPoints) {
            ctx->SetFillPaint(pointColor);
            for (size_t i = 0; i < dataSource->GetPointCount(); ++i) {
                ctx->FillCircle(areaPoints[i], pointRadius);
            }
        }

        if (showValueLabels) {
            RenderValueLabels(ctx, areaPoints);
        }

        if (clipped) ctx->PopState();
    }

    bool UltraCanvasAreaChartElement::HandleChartMouseMove(const Point2Di& mousePos) {
        if (!dataSource || !enableTooltips) return false;

        // Find nearest data point along the x-axis
        double minXDistance = 30.0f; // Threshold distance in pixels for X
        double maxYDistance = 50.0f; // Threshold distance in pixels for Y
        size_t nearestIndex = SIZE_MAX;

        for (size_t i = 0; i < dataSource->GetPointCount(); ++i) {
            auto point = dataSource->GetPoint(i);
            Point2Dd screenPos = GetDataPointScreenPosition(i, point);
            if (!IsScreenXInView(screenPos.x)) continue;   // zoomed out of the plot

            double dx = std::abs(mousePos.x - screenPos.x);
            double dy = std::abs(mousePos.y - screenPos.y);

            // Check if mouse is close enough in X direction and within reasonable Y range
            if (dx < minXDistance && dy < maxYDistance) {
                minXDistance = dx;
                nearestIndex = i;
            }
        }

        if (nearestIndex != SIZE_MAX) {
            auto point = dataSource->GetPoint(nearestIndex);
            ShowChartPointTooltip(mousePos, point, nearestIndex);
            return true;
        } else if (isTooltipActive) {
            HideTooltip();
        }

        return false;
    }

} // namespace UltraCanvas