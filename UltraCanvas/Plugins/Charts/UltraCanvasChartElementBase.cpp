// Plugins/Charts/UltraCanvasChartElementBase.cpp
// Base class for all chart elements with common functionality
// Version: 1.2.2 - a left press or release is taken only when it starts or ends a
//                  pan; any other goes on to the parent
// Version: 1.2.1 - the hover ring sits on the point in DataLabel mode
// Version: 1.2.0 - x-axis zoom and pan that work (charts opt in); the wheel and
//                  drags are left to the parent when nothing zooms; plot area
//                  recomputed on every resize
// Version: 1.1.0
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasChartElementBase.h"
#include "UltraCanvasApplication.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace UltraCanvas {

    void UltraCanvasChartElementBase::SetDataSource(std::shared_ptr<IChartDataSource> data) {
        dataSource = data;
        // New data is shown whole: a zoom into the old data means nothing here.
        xViewStart = 0.0;
        xViewEnd = 1.0;
        InvalidateCache();
        StartAnimation();
        RequestRedraw();
    }

    void UltraCanvasChartElementBase::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        // Check if we have data
        if (!dataSource || dataSource->GetPointCount() == 0) {
            DrawEmptyState(ctx);
            return;
        }

        // Update cache if needed
        UpdateRenderingCache();

        // Apply animation if active
        if (animationEnabled && !animationComplete) {
            UpdateAnimation();
        }

        // Draw common background
        RenderCommonBackground(ctx);

        // Call derived class to render specific chart type
        RenderChart(ctx);

        // Draw selection indicators if any using existing drawing functions
        if (enableSelection) {
            DrawSelectionIndicators(ctx);
        }

    }

    void UltraCanvasChartElementBase::SetBounds(const Rect2Df& b) {
        Size2Df before = GetSize();
        UltraCanvasUIElement::SetBounds(b);
        if (GetSize() != before) InvalidateCache();
    }

    void UltraCanvasChartElementBase::Arrange(const Rect2Df& finalRect, const CSSLayout::LayoutContext& ctx) {
        Size2Df before = GetSize();
        UltraCanvasUIElement::Arrange(finalRect, ctx);
        if (GetSize() != before) InvalidateCache();
    }

    void UltraCanvasChartElementBase::SetXAxisView(double start, double span) {
        span = std::clamp(span, 1.0 / kMaxXAxisZoom, 1.0);
        start = std::clamp(start, 0.0, 1.0 - span);
        if (span >= 1.0 - 1e-9) {
            start = 0.0;
            span = 1.0;
        }
        if (start == xViewStart && start + span == xViewEnd) return;
        xViewStart = start;
        xViewEnd = start + span;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasChartElementBase::EndPan() {
        if (!isPanning) return;
        isPanning = false;
        if (auto* app = UltraCanvasApplication::GetInstance()) {
            app->ReleaseMouse();
        }
    }

    bool UltraCanvasChartElementBase::OnEvent(const UCEvent& event) {
        if (IsDisabled() || !IsVisible()) return false;

        switch (event.type) {
            case UCEventType::MouseDown:
                return HandleMouseDown(event);
            case UCEventType::MouseUp:
                return HandleMouseUp(event);
            case UCEventType::MouseMove:
                return HandleMouseMove(event);
            case UCEventType::MouseWheel:
                return HandleMouseWheel(event);
            default:
                return false;
        }
    }

    ChartDataBounds UltraCanvasChartElementBase::CalculateDataBounds() {
        ChartDataBounds bounds = CalculateRawDataBounds();
        if (!bounds.hasData) return bounds;
        PadDataBounds(bounds, true, true, true);
        return bounds;
    }

    void UltraCanvasChartElementBase::PadDataBounds(ChartDataBounds& bounds, bool padX, bool padMinY, bool padMaxY) {
        const double rangeX = bounds.maxX - bounds.minX;
        const double rangeY = bounds.maxY - bounds.minY;
        if (padX && rangeX > 0) {
            bounds.minX -= rangeX * 0.05;
            bounds.maxX += rangeX * 0.05;
        }
        if (rangeY > 0) {
            if (padMinY) bounds.minY -= rangeY * 0.05;
            if (padMaxY) bounds.maxY += rangeY * 0.05;
        }
    }

    ChartDataBounds UltraCanvasChartElementBase::CalculateRawDataBounds() const {
        ChartDataBounds bounds;
        if (!dataSource || dataSource->GetPointCount() == 0) {
            return bounds;
        }
        bounds.hasData = true;

        // Initialize with first point
        auto firstPoint = dataSource->GetPoint(0);
        bounds.minX = bounds.maxX = firstPoint.x;
        bounds.minY = bounds.maxY = firstPoint.y;

        // Find min/max values
        for (size_t i = 1; i < dataSource->GetPointCount(); ++i) {
            auto point = dataSource->GetPoint(i);
            bounds.minX = std::min(bounds.minX, point.x);
            bounds.maxX = std::max(bounds.maxX, point.x);
            bounds.minY = std::min(bounds.minY, point.y);
            bounds.maxY = std::max(bounds.maxY, point.y);
        }
        return bounds;
    }

    void UltraCanvasChartElementBase::RenderCommonBackground(IRenderContext* ctx) {
        if (!ctx) return;

        // Draw overall background using existing functions
        if (showBackground) {
            ctx->DrawFilledRectangle(GetLocalBounds(), backgroundColor);

            // Draw plot area background using existing functions
            ctx->SetFillPaint(plotAreaColor);
            ctx->FillRectangle(cachedPlotArea.ToRect2D());

            // Draw plot area border using existing functions
            ctx->SetStrokePaint(Color(180, 180, 180, 255));
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawRectangle(cachedPlotArea.ToRect2D());
        }

        // Draw grid if enabled using existing functions
        if (showGrid) {
            RenderGrid(ctx);
        }

        if (showAxes) {
            // Draw axes using existing functions
            RenderAxes(ctx);
        }

        // Draw title using existing functions
        if (!chartTitle.empty()) {
            ctx->SetTextPaint(Color(0, 0, 0, 255));
            ctx->SetFontSize(16.0f);

            // Calculate center position (simplified)
            double titleX = static_cast<double>(GetWidth()) / 2 - chartTitle.length() * 5;
            ctx->DrawText(chartTitle, Point2Dd(titleX, 0));
        }
    }

    void UltraCanvasChartElementBase::RenderGrid(IRenderContext* ctx) {
        ctx->SetStrokePaint(gridColor);
        ctx->SetStrokeWidth(1.0f);
        ChartCoordinateTransform transform(cachedPlotArea, cachedDataBounds);
        const double top = cachedPlotArea.y, bottom = cachedPlotArea.GetBottom();
        const double left = cachedPlotArea.x, right = cachedPlotArea.GetRight();

        // The grid lies on the ticks the axes label, so every line reads as
        // a value. Lines on the plot's own border are left to the border.
        if (useIndexBasedPositioning && dataSource) {
            const size_t count = dataSource->GetPointCount();
            for (size_t i = 0; i < count; i += GetXAxisLabelStep()) {
                const double x = GetXAxisLabelPosition(i, count);
                if (x <= left + 0.5 || x >= right - 0.5) continue;
                ctx->DrawLine({x, top}, {x, bottom});
            }
        } else {
            for (double value : GetXAxisTicks().values) {
                const double x = transform.DataToScreenX(value);
                if (x <= left + 0.5 || x >= right - 0.5) continue;
                ctx->DrawLine({x, top}, {x, bottom});
            }
        }
        for (double value : GetYAxisTicks().values) {
            const double y = transform.DataToScreenY(value);
            if (y <= top + 0.5 || y >= bottom - 0.5) continue;
            ctx->DrawLine({left, y}, {right, y});
        }
    }

    size_t UltraCanvasChartElementBase::GetXAxisLabelStep() const {
        // Labels for at most a dozen of the points in view.
        if (!dataSource) return 1;
        const int maxLabels = 12;
        const double pointsInView = static_cast<double>(dataSource->GetPointCount()) * (xViewEnd - xViewStart);
        return static_cast<size_t>(std::max(1, static_cast<int>(pointsInView) / maxLabels));
    }

    void UltraCanvasChartElementBase::RenderAxes(IRenderContext* ctx) {
        // Set axis style using existing functions
        ctx->SetStrokePaint(Color(0, 0, 0, 255));
        ctx->SetStrokeWidth(1.0f);

        // Draw X-axis using existing DrawLine
        ctx->DrawLine({cachedPlotArea.x, cachedPlotArea.y + cachedPlotArea.height}, {
                      cachedPlotArea.x + cachedPlotArea.width, cachedPlotArea.y + cachedPlotArea.height});

        // Draw Y-axis using existing DrawLine
        ctx->DrawLine({cachedPlotArea.x, cachedPlotArea.y}, {
                      cachedPlotArea.x, cachedPlotArea.y + cachedPlotArea.height});

        // Draw basic tick marks and labels
        RenderAxisLabels(ctx);
    }

//    void UltraCanvasChartElementBase::RenderAxisLabels(IRenderContext* ctx) {
//        if (!ctx) return;
//
//        ctx->SetStrokePaint(Color(0, 0, 0, 255));
//        ctx->SetTextPaint(Color(0, 0, 0, 255));
//        ctx->SetFontSize(10.0f);
//
//        // Use new method for X-axis labels that supports label mode
//        RenderXAxisLabelsWithMode(ctx);
//
//        // Y-axis labels
//        int numYTicks = 6, txtW, txtH;
//        for (int i = 0; i <= numYTicks; ++i) {
//            float y = cachedPlotArea.y + cachedPlotArea.height - (i * cachedPlotArea.height / numYTicks);
//            float tickX = cachedPlotArea.x;
//
//            // Draw tick mark using existing DrawLine
//            ctx->DrawLine({tickX - 5, y}, { tickX, y});
//
//            // Draw label using existing DrawText
//            double labelValue = cachedDataBounds.minY + (i * (cachedDataBounds.maxY - cachedDataBounds.minY) / numYTicks);
//            std::string label = FormatAxisLabel(labelValue);
//            ctx->GetTextLineDimensions(label, txtW, txtH);
//            ctx->DrawText(label, tickX - txtW - 8, y - (txtH / 2));
//        }
//    }

    double UltraCanvasChartElementBase::GetXAxisLabelPosition(size_t dataIndex, size_t totalPoints) {
        // Default implementation for line, scatter, area charts: under the
        // point, as GetDataPointScreenPosition places it
        return IndexToScreenX(dataIndex, totalPoints);
    }

    void UltraCanvasChartElementBase::RenderAxisLabels(IRenderContext* ctx) {
        if (!ctx || !dataSource) return;

        size_t dataPointCount = dataSource->GetPointCount();
        if (dataPointCount == 0) return;

        ctx->SetStrokePaint(Color(0, 0, 0, 255));
        ctx->SetTextPaint(Color(0, 0, 0, 255));
        ctx->SetFontSize(10.0f);

        if (useIndexBasedPositioning) {
            // When using index-based positioning (DataLabel mode),
            // draw labels at evenly spaced positions matching the data points

            // Determine number of labels to show based on available space
            // (the points in view, when zoomed)
            const size_t labelStep = GetXAxisLabelStep();

            for (size_t i = 0; i < dataPointCount; i += labelStep) {
                // Calculate X position - must match GetDataPointScreenPosition logic
                double x = GetXAxisLabelPosition(i, dataPointCount);
                if (!IsScreenXInView(x)) continue;

                auto point = dataSource->GetPoint(i);

                double tickY = cachedPlotArea.y + cachedPlotArea.height;

                // Draw tick mark
                ctx->DrawLine({x, tickY}, { x, tickY + 5});

                // Draw label (use label if available, otherwise fall back to x value)
                std::string label = point.label;
                if (label.empty()) {
                    label = FormatAxisLabel(point.x);
                }

                // Handle rotation if enabled
                if (rotateXAxisLabels) {
                    ctx->PushState();
                    ctx->Translate(x, tickY + 8);
                    ctx->Rotate(xAxisLabelRotation * M_PI / 180.0f);
                    ctx->DrawText(label, {0, 0});
                    ctx->PopState();
                } else {
                    Size2Di txtSize = ctx->GetTextLineDimensions(label);
                    ctx->DrawText(label, {x - txtSize.width / 2, tickY + 8});
                }
            }
        } else {
            // Numeric x: a tick at every whole step inside the visible range
            ChartCoordinateTransform transform(cachedPlotArea, cachedDataBounds);
            const ChartAxisTicks xTicks = GetXAxisTicks();
            for (double labelValue : xTicks.values) {
                double x = transform.DataToScreenX(labelValue);
                double tickY = cachedPlotArea.y + cachedPlotArea.height;

                // Draw tick mark
                ctx->DrawLine({x, tickY}, { x, tickY + 5});

                // Draw label
                std::string label = xTicks.Format(labelValue);

                if (rotateXAxisLabels) {
                    ctx->PushState();
                    ctx->Translate(x, tickY + 8);
                    ctx->Rotate(xAxisLabelRotation * M_PI / 180.0f);
                    ctx->DrawText(label, {0, 0});
                    ctx->PopState();
                } else {
                    Size2Di txtSize = ctx->GetTextLineDimensions(label);
                    ctx->DrawText(label, Point2Dd(x - txtSize.width / 2.0, tickY + 8));
                }
            }
        }

        // Y: a tick at every whole step inside the value range
        ChartCoordinateTransform transform(cachedPlotArea, cachedDataBounds);
        const ChartAxisTicks yTicks = GetYAxisTicks();
        for (double labelValue : yTicks.values) {
            double y = transform.DataToScreenY(labelValue);
            double tickX = cachedPlotArea.x;

            // Draw tick mark using existing DrawLine
            ctx->DrawLine({tickX - 5, y}, { tickX, y});

            std::string label = yTicks.Format(labelValue);
            Size2Di txtSize = ctx->GetTextLineDimensions(label);
            ctx->DrawText(label, Point2Dd (tickX - txtSize.width - 8, y - (static_cast<double>(txtSize.height) / 2)));
        }
    }

    void UltraCanvasChartElementBase::RenderValueLabels(IRenderContext* ctx, const std::vector<Point2Dd>& screenPositions) {
        if (!dataSource) return;

        ctx->SetTextPaint(valueLabelColor);
        ctx->SetFontSize(valueLabelFontSize);

        // Labels are placed greedily in point order: each is drawn only if its
        // bounds do not overlap a label already drawn, so dense series degrade
        // to a readable subset instead of painting every value over its
        // neighbors.
        std::vector<Rect2Dd> placedLabels;
        const double labelPadding = 2.0;

        for (size_t i = 0; i < dataSource->GetPointCount() && i < screenPositions.size(); ++i) {
            auto point = dataSource->GetPoint(i);
            Point2Dd labelPos = CalculateValueLabelPosition(screenPositions[i], i, dataSource->GetPointCount());

            std::string valueText = FormatAxisLabel(point.y);
            Size2Di txtSize = ctx->GetTextLineDimensions(valueText);

            double rotation = 0.0;
            if (valueLabelAutoRotate && dataSource->GetPointCount() > 10) {
                rotation = -45.0;
            } else if (valueLabelRotation != 0.0f) {
                rotation = valueLabelRotation;
            }

            Rect2Dd bounds;
            if (rotation != 0.0) {
                // Axis-aligned bounds of the text rect rotated around labelPos.
                double rad = rotation * M_PI / 180.0;
                double c = std::cos(rad), s = std::sin(rad);
                double minX = 0.0, maxX = 0.0, minY = 0.0, maxY = 0.0;
                const double cx[3] = {static_cast<double>(txtSize.width), 0.0,
                                      static_cast<double>(txtSize.width)};
                const double cy[3] = {0.0, static_cast<double>(txtSize.height),
                                      static_cast<double>(txtSize.height)};
                for (int k = 0; k < 3; ++k) {
                    double rx = cx[k] * c - cy[k] * s;
                    double ry = cx[k] * s + cy[k] * c;
                    minX = std::min(minX, rx); maxX = std::max(maxX, rx);
                    minY = std::min(minY, ry); maxY = std::max(maxY, ry);
                }
                bounds = Rect2Dd(labelPos.x + minX - labelPadding, labelPos.y + minY - labelPadding,
                                 maxX - minX + 2 * labelPadding, maxY - minY + 2 * labelPadding);
            } else {
                bounds = Rect2Dd(labelPos.x - txtSize.width / 2.0 - labelPadding, labelPos.y - labelPadding,
                                 txtSize.width + 2 * labelPadding, txtSize.height + 2 * labelPadding);
            }

            bool overlaps = false;
            for (const auto& placed : placedLabels) {
                if (bounds.Intersects(placed)) { overlaps = true; break; }
            }
            if (overlaps) continue;
            placedLabels.push_back(bounds);

            if (rotation != 0.0) {
                ctx->PushState();
                ctx->Translate(labelPos.x, labelPos.y);
                ctx->Rotate(rotation * M_PI / 180.0);
                ctx->DrawText(valueText, {0, 0});
                ctx->PopState();
            } else {
                // No rotation - center the text
                ctx->DrawText(valueText, {labelPos.x - txtSize.width/2, labelPos.y});
            }
        }
    }

    Point2Dd UltraCanvasChartElementBase::CalculateValueLabelPosition(const Point2Dd& pointPos, size_t index, size_t totalPoints) {
        Point2Dd labelPos = pointPos;

        switch (valueLabelPosition) {
            case ValueLabelPosition::LabelAbove:
                labelPos.y -= (pointRadius + valueLabelOffset);
                break;
            case ValueLabelPosition::LabelBelow:
                labelPos.y += (pointRadius + valueLabelOffset);
                break;
            case ValueLabelPosition::LabelLeft:
                labelPos.x -= (pointRadius + valueLabelOffset);
                break;
            case ValueLabelPosition::LabelRight:
                labelPos.x += (pointRadius + valueLabelOffset);
                break;
            case ValueLabelPosition::LabelAuto:
                // Auto position - alternate above/below for crowded charts
                if (totalPoints > 10 && index % 2 == 1) {
                    labelPos.y += (pointRadius + valueLabelOffset);
                } else {
                    labelPos.y -= (pointRadius + valueLabelOffset);
                }
                break;
        }

        return labelPos;
    }

    namespace {
        // `value` with at most `maxDecimals` decimals, trailing zeros dropped.
        std::string TrimmedNumber(double value, int maxDecimals) {
            char text[64];
            std::snprintf(text, sizeof text, "%.*f", maxDecimals, value);   // locale-ok: a label the person reads
            std::string s = text;
            const size_t mark = s.find_first_of(".,");
            if (mark != std::string::npos) {
                while (!s.empty() && s.back() == '0') s.pop_back();
                if (!s.empty() && (s.back() == '.' || s.back() == ',')) s.pop_back();
            }
            if (s == "-0") s = "0";
            return s;
        }
    }

    std::string UltraCanvasChartElementBase::FormatAxisLabel(double value) {
        // A single value (a value label, a tooltip): K and M from a thousand
        // and a million, with one decimal when it says something - 1500 is
        // "1.5K", not "1K" - and up to two decimals below that.
        if (std::abs(value) >= 1e6) return TrimmedNumber(value / 1e6, 1) + "M";
        if (std::abs(value) >= 1e3) return TrimmedNumber(value / 1e3, 1) + "K";
        return TrimmedNumber(value, 2);
    }

    // ===== AXIS TICKS =====

    double ChartNiceStep(double range, int maxTicks) {
        if (!(range > 0.0) || !std::isfinite(range)) return 0.0;
        maxTicks = std::max(1, maxTicks);
        const double rough = range / maxTicks;
        const double magnitude = std::pow(10.0, std::floor(std::log10(rough)));
        for (double factor : {1.0, 2.0, 2.5, 5.0, 10.0}) {
            const double step = factor * magnitude;
            // A hair of tolerance, so 100 / 5 is a step of 20, not 50.
            if (step >= rough * (1.0 - 1e-9)) return step;
        }
        return 10.0 * magnitude;
    }

    ChartAxisTicks ComputeChartAxisTicks(double lo, double hi, int maxTicks) {
        ChartAxisTicks ticks;
        if (!std::isfinite(lo) || !std::isfinite(hi)) return ticks;
        if (hi < lo) std::swap(lo, hi);
        ticks.step = ChartNiceStep(hi - lo, maxTicks);
        if (ticks.step <= 0.0) {
            ticks.values.push_back(lo);   // a single value: one tick on it
            return ticks;
        }
        const double eps = ticks.step * 1e-6;
        const long long first = static_cast<long long>(std::ceil((lo - eps) / ticks.step));
        const long long last = static_cast<long long>(std::floor((hi + eps) / ticks.step));
        // Below 1 the step is 1/n of a whole (0.1, 0.25, 0.002): k / n is the
        // double nearest the true value, where k * step is not (3 * 0.1 is
        // 0.30000000000000004). Each tick is computed, never summed.
        const double perUnit = ticks.step < 1.0 ? std::round(1.0 / ticks.step) : 0.0;
        for (long long k = first; k <= last && ticks.values.size() < 1000; ++k) {
            double v = perUnit > 0.0 ? static_cast<double>(k) / perUnit : static_cast<double>(k) * ticks.step;
            if (std::abs(v) < eps) v = 0.0;
            ticks.values.push_back(v);
        }

        double maxAbs = 0.0;
        for (double v : ticks.values) maxAbs = std::max(maxAbs, std::abs(v));
        if (maxAbs >= 1e7) { ticks.unitDivisor = 1e6; ticks.unitSuffix = "M"; }
        else if (maxAbs >= 1e4) { ticks.unitDivisor = 1e3; ticks.unitSuffix = "K"; }
        // As many decimals as the step has: 0.25 needs two, 0.5 one, 20 none.
        const double stepInUnit = ticks.step / ticks.unitDivisor;
        while (ticks.decimals < 9) {
            const double scaled = stepInUnit * std::pow(10.0, ticks.decimals);
            if (std::abs(scaled - std::round(scaled)) < 1e-6 * std::max(1.0, scaled)) break;
            ++ticks.decimals;
        }
        return ticks;
    }

    std::string ChartAxisTicks::Format(double value) const {
        char text[64];
        std::snprintf(text, sizeof text, "%.*f", decimals, value / unitDivisor);   // locale-ok: a label the person reads
        std::string s = text;
        // A value that rounds to zero is zero, not "-0".
        if (!s.empty() && s[0] == '-' && s.find_first_not_of("-0.,") == std::string::npos) s.erase(0, 1);
        return s + unitSuffix;
    }

    void UltraCanvasChartElementBase::DrawSelectionIndicators(IRenderContext* ctx) {
        if (hoveredPointIndex == SIZE_MAX || !dataSource) return;

        // Where the point is drawn: by its index in DataLabel mode, by its x
        // value otherwise. Not drawn for a point the zoom has scrolled away.
        auto point = dataSource->GetPoint(hoveredPointIndex);
        Point2Dd screenPos = GetDataPointScreenPosition(hoveredPointIndex, point);
        if (!IsScreenXInView(screenPos.x)) return;
        float indicatorSize = 8.0f;

        // Use existing IRenderContext drawing functions
        ctx->SetStrokePaint(Color(255, 0, 0, 255)); // Red selection indicator
        ctx->SetStrokeWidth(2.0f);
        ctx->DrawCircle(screenPos, indicatorSize);
    }

    void UltraCanvasChartElementBase::DrawEmptyState(IRenderContext* ctx) {
        if (!ctx) return;
        const Rect2Di local = GetLocalBounds();
        // The chart's own background, as with data (it was a fixed grey).
        if (showBackground) ctx->DrawFilledRectangle(local, backgroundColor);
        const bool hasIcon = !emptyIconPath.empty();
        if (emptyMessage.empty() && emptyDetail.empty() && !hasIcon) return;

        ctx->PushState();
        // The icon, the message and the detail under it, centred as one block.
        constexpr double kMessageHeight = 20.0, kDetailHeight = 18.0, kGap = 2.0, kIconGap = 10.0;
        constexpr double kBadge = 54.0, kBadgeIcon = 28.0, kBareIcon = 32.0;
        const bool badge = hasIcon && emptyIconBadgeColor.a > 0;
        const double iconBox = !hasIcon ? 0.0 : (badge ? kBadge : kBareIcon);
        double text = 0.0;
        if (!emptyMessage.empty()) text += kMessageHeight;
        if (!emptyDetail.empty()) text += (text > 0 ? kGap : 0.0) + kDetailHeight;
        const double block = iconBox + (iconBox > 0 && text > 0 ? kIconGap : 0.0) + text;
        double y = local.y + (local.height - block) / 2.0;
        const double centreX = local.x + local.width / 2.0;

        if (hasIcon) {
            if (badge) {
                ctx->SetFillPaint(emptyIconBadgeColor);
                ctx->FillRoundedRectangle(Rect2Dd(centreX - kBadge / 2.0, y, kBadge, kBadge), 14.0);
            }
            const double size = badge ? kBadgeIcon : kBareIcon;
            const Rect2Dd iconRect(centreX - size / 2.0, y + (iconBox - size) / 2.0, size, size);
            if (emptyIconColor.a > 0) {
                ctx->DrawMask(emptyIconColor, emptyIconPath, iconRect, ImageFitMode::Contain);
            } else {
                ctx->DrawImage(emptyIconPath, iconRect, ImageFitMode::Contain);
            }
            y += iconBox + (text > 0 ? kIconGap : 0.0);
        }

        ctx->SetTextAlignment(TextAlignment::Center);
        ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
        ctx->SetTextPaint(emptyMessageColor);
        const double x = local.x + 8.0, width = std::max(0.0, local.width - 16.0);
        if (!emptyMessage.empty()) {
            ctx->SetFontSize(14.0f);
            ctx->DrawTextInRect(emptyMessage, Rect2Dd(x, y, width, kMessageHeight));
            y += kMessageHeight + kGap;
        }
        if (!emptyDetail.empty()) {
            ctx->SetFontSize(12.0f);
            ctx->DrawTextInRect(emptyDetail, Rect2Dd(x, y, width, kDetailHeight));
        }
        ctx->PopState();
    }

    bool UltraCanvasChartElementBase::HandleMouseMove(const UCEvent& event) {
        Point2Di mousePos(event.pointer.x, event.pointer.y);
        lastMousePos = mousePos;

        // A drag that started on a zoomed plot moves the view, by as many
        // data units as the pointer moved across the plot.
        if (isPanning && isDragging && cachedPlotArea.width > 0) {
            double span = xViewEnd - xViewStart;
            double shift = (mousePos.x - panStartPointerX) / cachedPlotArea.width * span;
            SetXAxisView(panStartViewStart - shift, span);
            return true;
        }

        // Call derived class for chart-specific handling. A drag that pans
        // nothing is not taken: it is the parent's to use.
        return HandleChartMouseMove(mousePos);
    }

    bool UltraCanvasChartElementBase::HandleMouseDown(const UCEvent& event) {
        if (event.button == UCMouseButton::Left) { // Left mouse button
            isDragging = true;
            lastMousePos = Point2Di(event.pointer.x, event.pointer.y);
            if (enablePan && SupportsXAxisZoom() && IsZoomed() &&
                cachedPlotArea.Contains(event.pointer.x, event.pointer.y)) {
                isPanning = true;
                panStartPointerX = event.pointer.x;
                panStartViewStart = xViewStart;
                // The drag goes on reaching the chart when it leaves it
                if (auto* app = UltraCanvasApplication::GetInstance()) {
                    app->CaptureMouse(this);
                }
                return true;
            }
        }
        // A press that starts no pan is not the chart's: it goes on to the
        // parent (a scrolling or draggable container), as an unused wheel
        // turn does. A subclass that reacts to clicks handles them first.
        return false;
    }

    bool UltraCanvasChartElementBase::HandleMouseUp(const UCEvent& event) {
        if (event.button != UCMouseButton::Left) return false;
        const bool endsPan = isPanning;
        isDragging = false;
        EndPan();
        return endsPan;
    }

    bool UltraCanvasChartElementBase::HandleMouseWheel(const UCEvent& event) {
        // A wheel turn the chart does nothing with is left to the parent, so
        // a scrolling container around the chart still scrolls.
        if (!enableZoom || !SupportsXAxisZoom() || !dataSource || event.wheelDelta == 0) return false;

        UpdateRenderingCache();
        if (cachedPlotArea.width <= 0 ||
            !cachedPlotArea.Contains(event.pointer.x, event.pointer.y)) return false;
        bool zoomable = useIndexBasedPositioning ? dataSource->GetPointCount() >= 2
                                                 : cachedDataBounds.GetXRange() > 0;
        if (!zoomable) return false;

        // Wheel up zooms in, down out, keeping the value under the pointer
        // under the pointer.
        double span = xViewEnd - xViewStart;
        double factor = event.wheelDelta > 0 ? 1.0 / 1.25 : 1.25;
        double newSpan = std::clamp(span * factor, 1.0 / kMaxXAxisZoom, 1.0);
        if (std::abs(newSpan - span) < 1e-12) return false;   // fully in or fully out already

        double anchor = xViewStart + (event.pointer.x - cachedPlotArea.x) / cachedPlotArea.width * span;
        SetXAxisView(anchor - (anchor - xViewStart) * newSpan / span, newSpan);
        return true;
    }

    void UltraCanvasChartElementBase::HideTooltip() {
        if (isTooltipActive) {
            UltraCanvasTooltipManager::HideTooltip();
            isTooltipActive = false;
            hoveredPointIndex = SIZE_MAX;
        }
    }

} // namespace UltraCanvas