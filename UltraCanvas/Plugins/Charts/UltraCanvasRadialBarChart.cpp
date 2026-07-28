// Plugins/Charts/UltraCanvasRadialBarChart.cpp
// Radial bar / radial line ("ray") chart element for UltraCanvas
// Version: 1.0.0
// Last Modified: 2026-07-28
// Author: UltraCanvas Framework
#include "Plugins/Charts/UltraCanvasRadialBarChart.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace UltraCanvas {

    // =========================================================================
    // CONSTRUCTION / DEFAULTS
    // =========================================================================

    std::vector<Color> UltraCanvasRadialBarChart::DefaultPalette() {
        return {
            Color(139,   0,   0, 255),   // Dark red
            Color(178,  34,  34, 255),   // Fire brick
            Color(220,  20,  60, 255),   // Crimson
            Color(255, 105, 180, 255),   // Hot pink
            Color(255, 152,   0, 255),   // Orange
            Color( 66, 133, 244, 255),   // Blue
            Color( 15, 157,  88, 255),   // Green
            Color(171,  71, 188, 255)    // Purple
        };
    }

    UltraCanvasRadialBarChart::UltraCanvasRadialBarChart(
        const std::string& id, int x, int y, int w, int h)
        : UltraCanvasChartElementBase(id, x, y, w, h)
    {
        colorPalette = DefaultPalette();
        showGrid = false;
        showAxes = false;
        showBackground = false;
    }

    // =========================================================================
    // DATA MANAGEMENT
    // =========================================================================

    void UltraCanvasRadialBarChart::AddSeries(const RadialBarSeries& series) {
        seriesList.push_back(series);
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasRadialBarChart::AddSeries(
        const std::string& name,
        const std::vector<std::pair<std::string, double>>& values,
        const Color& color)
    {
        RadialBarSeries series(name, color);
        series.values.reserve(values.size());
        for (const auto& v : values) {
            series.values.emplace_back(v.first, v.second);
        }
        AddSeries(series);
    }

    void UltraCanvasRadialBarChart::ClearSeries() {
        seriesList.clear();
        hoveredBar = SIZE_MAX;
        InvalidateLayout();
        RequestRedraw();
    }

    RadialBarSeries* UltraCanvasRadialBarChart::GetSeries(size_t index) {
        if (index >= seriesList.size()) return nullptr;
        // Callers may mutate values; relayout on next render.
        InvalidateLayout();
        return &seriesList[index];
    }

    void UltraCanvasRadialBarChart::SetSeriesColor(size_t index, const Color& color) {
        if (index >= seriesList.size()) return;
        seriesList[index].color = color;
        InvalidateLayout();
        RequestRedraw();
    }

    // =========================================================================
    // CONFIGURATION SETTERS
    // =========================================================================

    void UltraCanvasRadialBarChart::SetStartAngle(float degrees) {
        startAngleDeg = degrees;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasRadialBarChart::SetSweepAngle(float degrees) {
        sweepAngleDeg = std::max(10.0f, std::min(360.0f, degrees));
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasRadialBarChart::SetInnerRadiusFraction(float fraction) {
        innerRadiusFraction = std::max(0.05f, std::min(0.9f, fraction));
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasRadialBarChart::SetSeriesGapAngle(float degrees) {
        seriesGapDeg = std::max(0.0f, std::min(30.0f, degrees));
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasRadialBarChart::SetBarWidth(float pixels) {
        barWidth = std::max(0.0f, pixels);
        RequestRedraw();
    }

    void UltraCanvasRadialBarChart::SetNormalization(RadialBarNormalization mode) {
        normalization = mode;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasRadialBarChart::SetValueRange(double minValue, double maxValue) {
        manualRange = true;
        manualMin = std::min(minValue, maxValue);
        manualMax = std::max(minValue, maxValue);
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasRadialBarChart::ClearValueRange() {
        manualRange = false;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasRadialBarChart::SetMinBarLengthFraction(float fraction) {
        minBarLengthFraction = std::max(0.0f, std::min(0.5f, fraction));
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasRadialBarChart::SetBarStyle(RadialBarStyle style) {
        barStyle = style;
        RequestRedraw();
    }

    void UltraCanvasRadialBarChart::SetCapStyle(RadialBarCapStyle cap) {
        capStyle = cap;
        RequestRedraw();
    }

    void UltraCanvasRadialBarChart::SetGradientFade(bool on) {
        gradientFade = on;
        RequestRedraw();
    }

    void UltraCanvasRadialBarChart::SetColorPalette(const std::vector<Color>& palette) {
        colorPalette = palette.empty() ? DefaultPalette() : palette;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasRadialBarChart::SetShowRingGuides(bool show) {
        showRingGuides = show;
        RequestRedraw();
    }

    void UltraCanvasRadialBarChart::SetRingGuideCount(int count) {
        ringGuideCount = std::max(1, std::min(10, count));
        RequestRedraw();
    }

    void UltraCanvasRadialBarChart::SetRingGuideColor(const Color& c) {
        ringGuideColor = c;
        RequestRedraw();
    }

    void UltraCanvasRadialBarChart::SetInnerCircleColor(const Color& fill, const Color& border) {
        innerCircleFill = fill;
        innerCircleBorder = border;
        RequestRedraw();
    }

    void UltraCanvasRadialBarChart::SetCenterText(const std::string& text) {
        centerText = text;
        RequestRedraw();
    }

    void UltraCanvasRadialBarChart::SetCenterTextColor(const Color& c) {
        centerTextColor = c;
        RequestRedraw();
    }

    void UltraCanvasRadialBarChart::SetCenterFont(const std::string& family, float size, FontWeight weight) {
        centerFontFamily = family;
        centerFontSize = size;
        centerFontWeight = weight;
        RequestRedraw();
    }

    void UltraCanvasRadialBarChart::SetShowSeriesLabels(bool show) {
        showSeriesLabels = show;
        RequestRedraw();
    }

    void UltraCanvasRadialBarChart::SetSeriesLabelFont(const std::string& family, float size, FontWeight weight) {
        seriesLabelFontFamily = family;
        seriesLabelFontSize = size;
        seriesLabelFontWeight = weight;
        RequestRedraw();
    }

    void UltraCanvasRadialBarChart::SetShowPeakLabels(bool show) {
        showPeakLabels = show;
        RequestRedraw();
    }

    void UltraCanvasRadialBarChart::SetLabelColor(const Color& c) {
        labelColor = c;
        RequestRedraw();
    }

    void UltraCanvasRadialBarChart::SetValueFormatter(std::function<std::string(double)> f) {
        valueFormatter = std::move(f);
        RequestRedraw();
    }

    void UltraCanvasRadialBarChart::SetHoverHighlightEnabled(bool on) {
        hoverHighlightEnabled = on;
        if (!on && hoveredBar != SIZE_MAX) {
            hoveredBar = SIZE_MAX;
            RequestRedraw();
        }
    }

    void UltraCanvasRadialBarChart::SetRadialTooltipFormatter(
        std::function<std::string(const RadialBarSeries&, const RadialBarValue&)> f) {
        radialTooltipFormatter = std::move(f);
    }

    // =========================================================================
    // LAYOUT
    // =========================================================================

    ChartPlotArea UltraCanvasRadialBarChart::CalculatePlotArea() {
        int marginSide   = showSeriesLabels ? 30 : 10;
        int marginTop    = chartTitle.empty() ? (showSeriesLabels ? 30 : 10) : 34;
        int marginBottom = showSeriesLabels ? 30 : 10;
        ChartPlotArea area;
        area.x      = marginSide;
        area.y      = marginTop;
        area.width  = std::max(20.0f, static_cast<float>(GetWidth())  - 2.0f * marginSide);
        area.height = std::max(20.0f, static_cast<float>(GetHeight()) - marginTop - marginBottom);
        return area;
    }

    void UltraCanvasRadialBarChart::InvalidateLayout() {
        layoutValid = false;
    }

    Color UltraCanvasRadialBarChart::ResolveSeriesColor(size_t index) const {
        if (index < seriesList.size() && seriesList[index].color.a > 0) {
            return seriesList[index].color;
        }
        if (colorPalette.empty()) return Color(128, 128, 128, 255);
        return colorPalette[index % colorPalette.size()];
    }

    void UltraCanvasRadialBarChart::RebuildLayout() {
        bars.clear();
        sectors.clear();
        layoutValid = true;

        size_t totalValues = 0;
        for (const auto& s : seriesList) totalValues += s.values.size();
        if (totalValues == 0) return;

        // ---- Normalization ranges ----
        double globalMin = std::numeric_limits<double>::max();
        double globalMax = std::numeric_limits<double>::lowest();
        std::vector<std::pair<double, double>> seriesRange(seriesList.size(),
            {std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest()});

        for (size_t si = 0; si < seriesList.size(); ++si) {
            for (const auto& v : seriesList[si].values) {
                globalMin = std::min(globalMin, v.value);
                globalMax = std::max(globalMax, v.value);
                seriesRange[si].first  = std::min(seriesRange[si].first,  v.value);
                seriesRange[si].second = std::max(seriesRange[si].second, v.value);
            }
        }

        auto normalize = [&](size_t si, double v) -> double {
            double lo, hi;
            if (manualRange) {
                lo = manualMin; hi = manualMax;
            } else if (normalization == RadialBarNormalization::PerSeries) {
                // Scale from zero (not the series minimum) so relative bar
                // lengths within a series stay proportional to their values.
                lo = std::min(0.0, seriesRange[si].first);
                hi = seriesRange[si].second;
            } else {
                lo = std::min(0.0, globalMin);
                hi = globalMax;
            }
            if (hi <= lo) return 1.0;
            double n = (v - lo) / (hi - lo);
            n = std::max(0.0, std::min(1.0, n));
            return minBarLengthFraction + (1.0 - minBarLengthFraction) * n;
        };

        // ---- Angular layout ----
        double startRad = startAngleDeg * M_PI / 180.0;
        double sweepRad = sweepAngleDeg * M_PI / 180.0;
        bool fullCircle = sweepAngleDeg >= 359.9f;
        size_t gapCount = fullCircle ? seriesList.size()
                                     : (seriesList.size() > 0 ? seriesList.size() - 1 : 0);
        double gapRad = seriesGapDeg * M_PI / 180.0;
        double usable = sweepRad - gapRad * static_cast<double>(gapCount);
        if (usable <= 0.0) {
            gapRad = 0.0;
            usable = sweepRad;
        }
        double slotSpan = usable / static_cast<double>(totalValues);

        double cursor = startRad;
        for (size_t si = 0; si < seriesList.size(); ++si) {
            const RadialBarSeries& series = seriesList[si];
            if (series.values.empty()) continue;

            SeriesSector sector;
            sector.seriesIndex = si;
            sector.startAngle = cursor;
            sector.color = ResolveSeriesColor(si);

            double peakValue = std::numeric_limits<double>::lowest();
            for (size_t vi = 0; vi < series.values.size(); ++vi) {
                const RadialBarValue& v = series.values[vi];

                Bar bar;
                bar.seriesIndex = si;
                bar.valueIndex  = vi;
                bar.slotSpan    = slotSpan;
                bar.angle       = cursor + slotSpan * 0.5;
                bar.normalized  = normalize(si, v.value);
                bar.color       = v.color.a > 0 ? v.color : sector.color;
                bars.push_back(bar);

                if (v.value > peakValue) {
                    peakValue = v.value;
                    sector.peakBarIndex = bars.size() - 1;
                }
                cursor += slotSpan;
            }

            sector.endAngle = cursor;
            sectors.push_back(sector);
            cursor += gapRad;
        }
    }

    float UltraCanvasRadialBarChart::BarLength(const Bar& bar) const {
        return static_cast<float>(bar.normalized)
             * (cachedOuterRadius - cachedInnerRadius);
    }

    float UltraCanvasRadialBarChart::BarThickness(const Bar& bar) const {
        if (barStyle == RadialBarStyle::Lines) {
            return barWidth > 0.0f ? barWidth : 1.5f;
        }
        if (barWidth > 0.0f) return barWidth;
        // Automatic: fill ~70% of the angular slot measured at the inner ring.
        float t = static_cast<float>(bar.slotSpan) * cachedInnerRadius * 0.7f;
        return std::max(1.5f, std::min(24.0f, t));
    }

    // =========================================================================
    // FORMATTERS
    // =========================================================================

    std::string UltraCanvasRadialBarChart::FormatValue(double v) const {
        if (valueFormatter) return valueFormatter(v);
        char buf[32];
        if (std::floor(v) == v && std::fabs(v) < 1e12) {
            std::snprintf(buf, sizeof(buf), "%.0f", v);
        } else {
            std::snprintf(buf, sizeof(buf), "%.1f", v);
        }
        return std::string(buf);
    }

    std::string UltraCanvasRadialBarChart::BuildTooltipText(const Bar& bar) const {
        const RadialBarSeries& series = seriesList[bar.seriesIndex];
        const RadialBarValue& value = series.values[bar.valueIndex];
        if (radialTooltipFormatter) return radialTooltipFormatter(series, value);

        std::string text = series.name;
        if (!value.label.empty()) text += "\n" + value.label;
        text += "\nValue: " + FormatValue(value.value);
        return text;
    }

    // =========================================================================
    // RENDERING
    // =========================================================================

    void UltraCanvasRadialBarChart::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
        if (!ctx) return;

        UpdateRenderingCache();
        if (!layoutValid) RebuildLayout();

        RenderCommonBackground(ctx);

        if (bars.empty()) {
            DrawEmptyState(ctx);
            return;
        }

        RenderChart(ctx);
    }

    void UltraCanvasRadialBarChart::RenderTitle(IRenderContext* ctx) {
        if (chartTitle.empty()) return;
        ctx->PushState();
        ctx->SetFontFamily(seriesLabelFontFamily);
        ctx->SetFontSize(seriesLabelFontSize + 4.0f);
        ctx->SetFontWeight(FontWeight::Bold);
        ctx->SetTextPaint(labelColor);
        Size2Di ts = ctx->GetTextLineDimensions(chartTitle);
        ctx->DrawText(chartTitle,
                      Point2Dd(cachedPlotArea.x + (cachedPlotArea.width - ts.width) / 2.0,
                               cachedPlotArea.y - ts.height - 6.0));
        ctx->PopState();
    }

    void UltraCanvasRadialBarChart::RenderChart(IRenderContext* ctx) {
        if (!ctx) return;
        UpdateRenderingCache();
        if (!layoutValid) RebuildLayout();
        if (bars.empty()) {
            DrawEmptyState(ctx);
            return;
        }

        cachedCenter = Point2Dd(cachedPlotArea.x + cachedPlotArea.width / 2.0,
                                cachedPlotArea.y + cachedPlotArea.height / 2.0);
        float maxR = std::min(cachedPlotArea.width, cachedPlotArea.height) * 0.5f - 4.0f;
        cachedOuterRadius = std::max(10.0f, maxR);
        cachedInnerRadius = cachedOuterRadius * innerRadiusFraction;

        RenderTitle(ctx);
        RenderInnerCircle(ctx);
        if (showRingGuides) RenderRingGuides(ctx);

        for (size_t i = 0; i < bars.size(); ++i) {
            bool highlighted = hoverHighlightEnabled && i == hoveredBar;
            DrawBar(ctx, bars[i], highlighted);
        }

        if (showSeriesLabels) RenderSeriesLabels(ctx);
        if (showPeakLabels)   RenderPeakLabels(ctx);
    }

    void UltraCanvasRadialBarChart::RenderInnerCircle(IRenderContext* ctx) {
        if (innerCircleFill.a > 0) {
            ctx->SetFillPaint(innerCircleFill);
            ctx->FillCircle(cachedCenter, cachedInnerRadius);
        }
        if (innerCircleBorder.a > 0) {
            ctx->SetStrokePaint(innerCircleBorder);
            ctx->SetStrokeWidth(1.5f);
            ctx->DrawCircle(cachedCenter, cachedInnerRadius);
        }

        if (!centerText.empty() && cachedInnerRadius >= 12.0f) {
            ctx->PushState();
            ctx->SetFontFamily(centerFontFamily);
            ctx->SetFontSize(centerFontSize);
            ctx->SetFontWeight(centerFontWeight);
            ctx->SetTextPaint(centerTextColor);
            Size2Di ts = ctx->GetTextLineDimensions(centerText);
            if (ts.width <= cachedInnerRadius * 1.8f) {
                ctx->DrawText(centerText,
                              Point2Dd(cachedCenter.x - ts.width / 2.0,
                                       cachedCenter.y - ts.height / 2.0));
            }
            ctx->PopState();
        }
    }

    void UltraCanvasRadialBarChart::RenderRingGuides(IRenderContext* ctx) {
        ctx->SetStrokePaint(ringGuideColor);
        ctx->SetStrokeWidth(1.0f);
        for (int i = 1; i <= ringGuideCount; ++i) {
            float r = cachedInnerRadius
                    + (cachedOuterRadius - cachedInnerRadius)
                    * static_cast<float>(i) / static_cast<float>(ringGuideCount);
            ctx->DrawCircle(cachedCenter, r);
        }
    }

    void UltraCanvasRadialBarChart::DrawBar(IRenderContext* ctx,
                                            const Bar& bar,
                                            bool highlighted)
    {
        float length = BarLength(bar);
        if (length <= 0.5f) return;

        double cosA = std::cos(bar.angle);
        double sinA = std::sin(bar.angle);
        Point2Dd inner(cachedCenter.x + cachedInnerRadius * cosA,
                       cachedCenter.y + cachedInnerRadius * sinA);
        Point2Dd tip(cachedCenter.x + (cachedInnerRadius + length) * cosA,
                     cachedCenter.y + (cachedInnerRadius + length) * sinA);

        Color color = highlighted ? bar.color.Lighten(0.25f) : bar.color;
        float thickness = BarThickness(bar);
        if (highlighted) thickness *= 1.5f;

        ctx->PushState();

        if (gradientFade && barStyle == RadialBarStyle::Bars) {
            std::vector<GradientStop> stops;
            stops.emplace_back(0.0, color);
            stops.emplace_back(1.0, color.WithAlpha(
                static_cast<uint8_t>(color.a * 0.35f)));
            auto grad = ctx->CreateLinearGradientPattern(
                inner.x, inner.y, tip.x, tip.y, stops);
            if (grad) ctx->SetStrokePaint(grad);
            else      ctx->SetStrokePaint(color);
        } else {
            ctx->SetStrokePaint(color);
        }

        ctx->SetStrokeWidth(thickness);
        switch (capStyle) {
            case RadialBarCapStyle::Round:  ctx->SetLineCap(LineCap::Round);  break;
            case RadialBarCapStyle::Square: ctx->SetLineCap(LineCap::Square); break;
            default:                        ctx->SetLineCap(LineCap::Butt);   break;
        }
        ctx->DrawLine(inner, tip);

        if (capStyle == RadialBarCapStyle::Arrow) {
            DrawArrowCap(ctx, tip, bar.angle, thickness, color);
        }

        ctx->PopState();
    }

    void UltraCanvasRadialBarChart::DrawArrowCap(IRenderContext* ctx,
                                                 const Point2Dd& tip,
                                                 double angle,
                                                 float thickness,
                                                 const Color& color)
    {
        double arrowLength = std::max(6.0f, thickness * 2.0f);
        double arrowAngle = M_PI / 6.0;   // 30 degrees

        Point2Dd wing1(tip.x - arrowLength * std::cos(angle - arrowAngle),
                       tip.y - arrowLength * std::sin(angle - arrowAngle));
        Point2Dd wing2(tip.x - arrowLength * std::cos(angle + arrowAngle),
                       tip.y - arrowLength * std::sin(angle + arrowAngle));

        ctx->SetStrokePaint(color);
        ctx->SetStrokeWidth(std::max(1.0f, thickness * 0.6f));
        ctx->SetLineCap(LineCap::Round);
        ctx->DrawLine(tip, wing1);
        ctx->DrawLine(tip, wing2);
    }

    void UltraCanvasRadialBarChart::RenderSeriesLabels(IRenderContext* ctx) {
        ctx->PushState();
        ctx->SetFontFamily(seriesLabelFontFamily);
        ctx->SetFontSize(seriesLabelFontSize);
        ctx->SetFontWeight(seriesLabelFontWeight);

        float labelRadius = cachedOuterRadius + 6.0f;

        for (const SeriesSector& sector : sectors) {
            const std::string& name = seriesList[sector.seriesIndex].name;
            if (name.empty()) continue;

            double aMid = (sector.startAngle + sector.endAngle) * 0.5;
            Size2Di ts = ctx->GetTextLineDimensions(name);

            // Push the anchor outward so the label box clears the outer radius.
            Point2Dd anchor(cachedCenter.x + labelRadius * std::cos(aMid),
                            cachedCenter.y + labelRadius * std::sin(aMid));
            // Shift the box away from the circle based on which side it sits on.
            double dx = std::cos(aMid);
            double dy = std::sin(aMid);
            Point2Dd pos(anchor.x + dx * (ts.width / 2.0) - ts.width / 2.0,
                         anchor.y + dy * (ts.height / 2.0) - ts.height / 2.0);

            ctx->SetFillPaint(Color(255, 255, 255, 210));
            ctx->FillRoundedRectangle(
                Rect2Dd(pos.x - 4.0, pos.y - 2.0, ts.width + 8.0, ts.height + 4.0), 4.0);

            ctx->SetTextPaint(sector.color);
            ctx->DrawText(name, pos);
        }
        ctx->PopState();
    }

    void UltraCanvasRadialBarChart::RenderPeakLabels(IRenderContext* ctx) {
        ctx->PushState();
        ctx->SetFontFamily(seriesLabelFontFamily);
        ctx->SetFontSize(std::max(8.0f, seriesLabelFontSize - 2.0f));
        ctx->SetFontWeight(FontWeight::Normal);
        ctx->SetTextPaint(labelColor);

        for (const SeriesSector& sector : sectors) {
            if (sector.peakBarIndex >= bars.size()) continue;
            const Bar& bar = bars[sector.peakBarIndex];
            const RadialBarValue& value =
                seriesList[bar.seriesIndex].values[bar.valueIndex];

            std::string text = "Max " + FormatValue(value.value);
            Size2Di ts = ctx->GetTextLineDimensions(text);

            float r = cachedInnerRadius + BarLength(bar) + 6.0f;
            Point2Dd pos(cachedCenter.x + r * std::cos(bar.angle),
                         cachedCenter.y + r * std::sin(bar.angle));
            // Anchor the text box on the side facing away from the center.
            double norm = std::fmod(bar.angle + 4.0 * M_PI, 2.0 * M_PI);
            bool leftSide = norm > M_PI / 2.0 && norm < 3.0 * M_PI / 2.0;
            pos.x -= leftSide ? ts.width : 0.0;
            pos.y -= ts.height / 2.0;

            ctx->DrawText(text, pos);
        }
        ctx->PopState();
    }

    // =========================================================================
    // HIT TESTING & EVENTS
    // =========================================================================

    size_t UltraCanvasRadialBarChart::HitTestBar(const Point2Dd& localPos) const {
        if (bars.empty()) return SIZE_MAX;

        double dx = localPos.x - cachedCenter.x;
        double dy = localPos.y - cachedCenter.y;
        double r = std::sqrt(dx * dx + dy * dy);
        if (r < cachedInnerRadius || r > cachedOuterRadius + 4.0) return SIZE_MAX;

        double angle = std::atan2(dy, dx);

        for (size_t i = 0; i < bars.size(); ++i) {
            const Bar& bar = bars[i];
            // Angular distance from the bar's center line.
            double da = angle - bar.angle;
            while (da >  M_PI) da -= 2.0 * M_PI;
            while (da < -M_PI) da += 2.0 * M_PI;
            if (std::fabs(da) > bar.slotSpan * 0.5) continue;

            float length = BarLength(bar);
            if (r <= cachedInnerRadius + length + 4.0f) return i;
        }
        return SIZE_MAX;
    }

    bool UltraCanvasRadialBarChart::HandleChartMouseMove(const Point2Di& mousePos) {
        if (bars.empty()) return false;

        Point2Dd local(static_cast<double>(mousePos.x), static_cast<double>(mousePos.y));
        size_t hit = HitTestBar(local);

        if (hit != hoveredBar) {
            hoveredBar = hit;
            if (hit != SIZE_MAX && onBarHover) {
                onBarHover(bars[hit].seriesIndex, bars[hit].valueIndex);
            }
            RequestRedraw();
        }

        if (hit != SIZE_MAX) {
            if (enableTooltips) {
                std::string tooltip = BuildTooltipText(bars[hit]);
                auto windowMousePos = MapFromLocal(mousePos, nullptr);
                UltraCanvasTooltipManager::UpdateAndShowTooltip(
                    this->window, tooltip, windowMousePos);
                isTooltipActive = true;
            }
            return true;
        }
        if (isTooltipActive) HideTooltip();
        return false;
    }

    bool UltraCanvasRadialBarChart::HandleClick(const UCEvent& event) {
        Point2Dd local(static_cast<double>(event.pointer.x),
                       static_cast<double>(event.pointer.y));
        size_t hit = HitTestBar(local);
        if (hit != SIZE_MAX) {
            if (onBarClick) onBarClick(bars[hit].seriesIndex, bars[hit].valueIndex);
            return true;
        }
        return false;
    }

    bool UltraCanvasRadialBarChart::OnEvent(const UCEvent& event) {
        if (IsDisabled() || !IsVisible()) return false;

        switch (event.type) {
            case UCEventType::MouseDown:
                if (event.button == UCMouseButton::Left && HandleClick(event)) {
                    return true;
                }
                break;
            case UCEventType::MouseLeave:
                if (hoveredBar != SIZE_MAX) {
                    hoveredBar = SIZE_MAX;
                    RequestRedraw();
                }
                HideTooltip();
                return true;
            default:
                break;
        }
        return UltraCanvasChartElementBase::OnEvent(event);
    }

    // =========================================================================
    // FACTORY FUNCTIONS
    // =========================================================================

    std::shared_ptr<UltraCanvasRadialBarChart> CreateRadialBarChart(
        const std::string& id, int x, int y, int w, int h)
    {
        return std::make_shared<UltraCanvasRadialBarChart>(id, x, y, w, h);
    }

    std::shared_ptr<UltraCanvasRadialBarChart> CreateRadialBarChartFromSeries(
        const std::string& id, int x, int y, int w, int h,
        const std::vector<std::pair<std::string, std::vector<double>>>& seriesData)
    {
        auto chart = std::make_shared<UltraCanvasRadialBarChart>(id, x, y, w, h);
        for (const auto& entry : seriesData) {
            RadialBarSeries series(entry.first);
            series.values.reserve(entry.second.size());
            for (size_t i = 0; i < entry.second.size(); ++i) {
                series.values.emplace_back("T" + std::to_string(i + 1),
                                           entry.second[i]);
            }
            chart->AddSeries(series);
        }
        return chart;
    }

} // namespace UltraCanvas
