// Plugins/Charts/UltraCanvasCircularProgressChart.cpp
// Circular progress chart element: concentric progress rings ("activity
// rings"), single progress ring and progress pie, all angle-encoded.
// Version: 1.0.0
// Last Modified: 2026-07-29
// Author: UltraCanvas Framework
#include "Plugins/Charts/UltraCanvasCircularProgressChart.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace UltraCanvas {

    // =========================================================================
    // CONSTRUCTION / DEFAULTS
    // =========================================================================

    std::vector<Color> UltraCanvasCircularProgressChart::DefaultPalette() {
        return {
            Color( 26, 188, 156, 255),   // Teal
            Color( 66, 133, 244, 255),   // Blue
            Color(243, 156,  18, 255),   // Orange
            Color(241, 196,  15, 255),   // Yellow
            Color(255,  62, 133, 255),   // Magenta
            Color(120,  90, 200, 255),   // Purple
            Color(150, 200,  60, 255),   // Green
            Color(225,  30,  40, 255)    // Red
        };
    }

    UltraCanvasCircularProgressChart::UltraCanvasCircularProgressChart(
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

    size_t UltraCanvasCircularProgressChart::AddRing(const ProgressRing& ring) {
        rings.push_back(ring);
        InvalidateLayout();
        RequestRedraw();
        return rings.size() - 1;
    }

    size_t UltraCanvasCircularProgressChart::AddRing(const std::string& label,
                                                     double value,
                                                     const Color& color) {
        return AddRing(ProgressRing(label, value, color));
    }

    void UltraCanvasCircularProgressChart::ClearRings() {
        rings.clear();
        hoveredRing = SIZE_MAX;
        InvalidateLayout();
        RequestRedraw();
    }

    const ProgressRing* UltraCanvasCircularProgressChart::GetRing(size_t index) const {
        if (index >= rings.size()) return nullptr;
        return &rings[index];
    }

    bool UltraCanvasCircularProgressChart::SetRingValue(size_t index, double value) {
        if (index >= rings.size()) return false;
        rings[index].value = value;
        InvalidateLayout();
        RequestRedraw();
        return true;
    }

    bool UltraCanvasCircularProgressChart::SetRingColor(size_t index, const Color& color) {
        if (index >= rings.size()) return false;
        rings[index].color = color;
        InvalidateLayout();
        RequestRedraw();
        return true;
    }

    bool UltraCanvasCircularProgressChart::SetRingRange(size_t index,
                                                        double minValue,
                                                        double maxValue) {
        if (index >= rings.size()) return false;
        rings[index].minValue = std::min(minValue, maxValue);
        rings[index].maxValue = std::max(minValue, maxValue);
        InvalidateLayout();
        RequestRedraw();
        return true;
    }

    // =========================================================================
    // CONFIGURATION SETTERS
    // =========================================================================

    void UltraCanvasCircularProgressChart::SetSubStyle(CircularProgressStyle style) {
        subStyle = style;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasCircularProgressChart::SetStartAngle(float degrees) {
        startAngleDeg = degrees;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasCircularProgressChart::SetClockwise(bool on) {
        clockwise = on;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasCircularProgressChart::SetInnerRadiusFraction(float fraction) {
        innerRadiusFraction = std::max(0.0f, std::min(0.9f, fraction));
        RequestRedraw();
    }

    void UltraCanvasCircularProgressChart::SetRingSpacing(float pixels) {
        ringSpacing = std::max(0.0f, pixels);
        RequestRedraw();
    }

    void UltraCanvasCircularProgressChart::SetRingThickness(float pixels) {
        ringThickness = std::max(0.0f, pixels);
        RequestRedraw();
    }

    void UltraCanvasCircularProgressChart::SetCapStyle(RingCapStyle cap) {
        capStyle = cap;
        RequestRedraw();
    }

    void UltraCanvasCircularProgressChart::SetTrackMode(RingTrackMode mode) {
        trackMode = mode;
        RequestRedraw();
    }

    void UltraCanvasCircularProgressChart::SetTrackColor(const Color& c) {
        trackColor = c;
        RequestRedraw();
    }

    void UltraCanvasCircularProgressChart::SetTrackTintAlpha(uint8_t alpha) {
        trackTintAlpha = alpha;
        RequestRedraw();
    }

    void UltraCanvasCircularProgressChart::SetColorPalette(const std::vector<Color>& palette) {
        colorPalette = palette.empty() ? DefaultPalette() : palette;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasCircularProgressChart::SetTipLabelContent(RingTipLabel content) {
        tipLabelContent = content;
        RequestRedraw();
    }

    void UltraCanvasCircularProgressChart::SetTipLabelBubble(bool on) {
        tipLabelBubble = on;
        RequestRedraw();
    }

    void UltraCanvasCircularProgressChart::SetTipLabelFont(const std::string& family,
                                                           float size, FontWeight weight) {
        tipFontFamily = family;
        tipFontSize = size;
        tipFontWeight = weight;
        RequestRedraw();
    }

    void UltraCanvasCircularProgressChart::SetTipLabelColor(const Color& c) {
        tipLabelColor = c;
        RequestRedraw();
    }

    void UltraCanvasCircularProgressChart::SetRingNameLabels(RingNamePosition position) {
        ringNamePosition = position;
        RequestRedraw();
    }

    void UltraCanvasCircularProgressChart::SetNameLabelFont(const std::string& family,
                                                            float size, FontWeight weight) {
        nameFontFamily = family;
        nameFontSize = size;
        nameFontWeight = weight;
        RequestRedraw();
    }

    void UltraCanvasCircularProgressChart::SetNameLabelColor(const Color& c) {
        nameLabelColor = c;
        RequestRedraw();
    }

    void UltraCanvasCircularProgressChart::SetShowCenterDisc(bool show) {
        showCenterDisc = show;
        RequestRedraw();
    }

    void UltraCanvasCircularProgressChart::SetCenterDisc(const Color& fill, const Color& border) {
        showCenterDisc = true;
        centerFillColor = fill;
        centerBorderColor = border;
        RequestRedraw();
    }

    void UltraCanvasCircularProgressChart::SetCenterText(const std::string& title,
                                                         const std::string& subtitle) {
        centerTitle = title;
        centerSubtitle = subtitle;
        RequestRedraw();
    }

    void UltraCanvasCircularProgressChart::SetCenterTextColor(const Color& c) {
        centerTextColor = c;
        RequestRedraw();
    }

    void UltraCanvasCircularProgressChart::SetCenterFont(const std::string& family,
                                                         float size, FontWeight weight) {
        centerFontFamily = family;
        centerFontSize = size;
        centerFontWeight = weight;
        RequestRedraw();
    }

    void UltraCanvasCircularProgressChart::SetLegendStyle(CircularLegendStyle style,
                                                          CircularLegendPosition position) {
        legendStyle = style;
        legendPosition = position;
        RequestRedraw();
    }

    void UltraCanvasCircularProgressChart::SetLegendFont(const std::string& family, float size) {
        legendFontFamily = family;
        legendFontSize = size;
        RequestRedraw();
    }

    void UltraCanvasCircularProgressChart::SetLegendTextColor(const Color& c) {
        legendTextColor = c;
        RequestRedraw();
    }

    void UltraCanvasCircularProgressChart::SetValueFormatter(std::function<std::string(double)> f) {
        valueFormatter = std::move(f);
        RequestRedraw();
    }

    void UltraCanvasCircularProgressChart::SetPercentFormatter(std::function<std::string(double)> f) {
        percentFormatter = std::move(f);
        RequestRedraw();
    }

    void UltraCanvasCircularProgressChart::SetHoverHighlightEnabled(bool on) {
        hoverHighlightEnabled = on;
        if (!on && hoveredRing != SIZE_MAX) {
            hoveredRing = SIZE_MAX;
            RequestRedraw();
        }
    }

    void UltraCanvasCircularProgressChart::SetRingTooltipFormatter(
        std::function<std::string(const ProgressRing&)> f) {
        ringTooltipFormatter = std::move(f);
    }

    // =========================================================================
    // LAYOUT
    // =========================================================================

    ChartPlotArea UltraCanvasCircularProgressChart::CalculatePlotArea() {
        int marginSide   = 8;
        int marginTop    = chartTitle.empty() ? 8 : 34;
        int marginBottom = 8;
        ChartPlotArea area;
        area.x      = marginSide;
        area.y      = marginTop;
        area.width  = std::max(20.0f, static_cast<float>(GetWidth())  - 2.0f * marginSide);
        area.height = std::max(20.0f, static_cast<float>(GetHeight()) - marginTop - marginBottom);
        return area;
    }

    void UltraCanvasCircularProgressChart::InvalidateLayout() {
        layoutValid = false;
    }

    size_t UltraCanvasCircularProgressChart::VisibleRingCount() const {
        if (rings.empty()) return 0;
        if (subStyle == CircularProgressStyle::ConcentricRings) return rings.size();
        return 1;
    }

    double UltraCanvasCircularProgressChart::RingFraction(const ProgressRing& ring) const {
        double span = ring.maxValue - ring.minValue;
        if (span <= 0.0) return 0.0;
        double f = (ring.value - ring.minValue) / span;
        return std::max(0.0, std::min(1.0, f));
    }

    Color UltraCanvasCircularProgressChart::ResolveRingColor(size_t index) const {
        if (index < rings.size() && rings[index].color.a > 0) {
            return rings[index].color;
        }
        if (colorPalette.empty()) return Color(128, 128, 128, 255);
        return colorPalette[index % colorPalette.size()];
    }

    Color UltraCanvasCircularProgressChart::TrackColorFor(const RingLayout& layout) const {
        switch (trackMode) {
            case RingTrackMode::AutoTint: return layout.color.WithAlpha(trackTintAlpha);
            case RingTrackMode::Explicit: return trackColor;
            default:                      return Colors::Transparent;
        }
    }

    // Builds the angular layout for every visible ring. Radii are filled in by
    // RenderChart, which knows the pixel geometry of the current frame.
    void UltraCanvasCircularProgressChart::RebuildLayout() {
        ringLayouts.clear();
        layoutValid = true;

        double base = startAngleDeg * M_PI / 180.0;
        size_t count = VisibleRingCount();
        for (size_t i = 0; i < count; ++i) {
            const ProgressRing& ring = rings[i];

            RingLayout layout;
            layout.ringIndex = i;
            layout.fraction  = RingFraction(ring);
            layout.color     = ResolveRingColor(i);

            double sweep = layout.fraction * 2.0 * M_PI;
            // Keep startAngle <= endAngle regardless of direction so the
            // renderer and hit tester never deal with reversed ranges.
            if (clockwise) {
                layout.startAngle = base;
                layout.endAngle   = base + sweep;
            } else {
                layout.startAngle = base - sweep;
                layout.endAngle   = base;
            }
            ringLayouts.push_back(layout);
        }
    }

    // t = 0 at the arc base (the configured start angle), t = 1 at the moving tip.
    double UltraCanvasCircularProgressChart::ScreenAngle(const RingLayout& layout,
                                                         double fractionAlong) const {
        double baseAngle = clockwise ? layout.startAngle : layout.endAngle;
        double tipAngle  = clockwise ? layout.endAngle   : layout.startAngle;
        return baseAngle + (tipAngle - baseAngle) * fractionAlong;
    }

    Point2Dd UltraCanvasCircularProgressChart::ArcTipPoint(const RingLayout& layout) const {
        double a = ScreenAngle(layout, 1.0);
        return Point2Dd(cachedCenter.x + layout.radius * std::cos(a),
                        cachedCenter.y + layout.radius * std::sin(a));
    }

    // =========================================================================
    // FORMATTERS
    // =========================================================================

    std::string UltraCanvasCircularProgressChart::FormatValue(double v) const {
        if (valueFormatter) return valueFormatter(v);
        char buf[32];
        if (std::floor(v) == v && std::fabs(v) < 1e12) {
            std::snprintf(buf, sizeof(buf), "%.0f", v);
        } else {
            std::snprintf(buf, sizeof(buf), "%.1f", v);
        }
        return std::string(buf);
    }

    std::string UltraCanvasCircularProgressChart::FormatPercent(double fraction) const {
        double pct = fraction * 100.0;
        if (percentFormatter) return percentFormatter(pct);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f%%", pct);
        return std::string(buf);
    }

    std::string UltraCanvasCircularProgressChart::TipLabelText(const RingLayout& layout) const {
        switch (tipLabelContent) {
            case RingTipLabel::Percentage: return FormatPercent(layout.fraction);
            case RingTipLabel::Value:      return FormatValue(rings[layout.ringIndex].value);
            default:                       return "";
        }
    }

    std::string UltraCanvasCircularProgressChart::BuildTooltipText(size_t ringIndex) const {
        const ProgressRing& ring = rings[ringIndex];
        if (ringTooltipFormatter) return ringTooltipFormatter(ring);

        std::string text = ring.label;
        if (!text.empty()) text += "\n";
        text += FormatValue(ring.value) + " (" + FormatPercent(RingFraction(ring)) + ")";
        return text;
    }

    // =========================================================================
    // RENDERING
    // =========================================================================

    void UltraCanvasCircularProgressChart::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
        if (!ctx) return;

        UpdateRenderingCache();
        if (!layoutValid) RebuildLayout();

        RenderCommonBackground(ctx);

        if (ringLayouts.empty()) {
            DrawEmptyState(ctx);
            return;
        }

        RenderChart(ctx);
    }

    void UltraCanvasCircularProgressChart::RenderChart(IRenderContext* ctx) {
        if (!ctx) return;
        UpdateRenderingCache();
        if (!layoutValid) RebuildLayout();
        if (ringLayouts.empty()) {
            DrawEmptyState(ctx);
            return;
        }

        // ---- Reserve a band for the legend ----
        legendBandSize = 0.0;
        if (legendStyle != CircularLegendStyle::NoLegend) {
            ctx->PushState();
            ctx->SetFontFamily(legendFontFamily);
            ctx->SetFontSize(legendFontSize);
            double maxRow = 0.0;
            for (const RingLayout& layout : ringLayouts) {
                Size2Di ts = ctx->GetTextLineDimensions(rings[layout.ringIndex].label);
                double row = ts.width + 34.0;                          // chip + gaps
                if (!rings[layout.ringIndex].iconPath.empty()) row += 22.0;
                maxRow = std::max(maxRow, row);
            }
            ctx->PopState();
            if (legendPosition == CircularLegendPosition::Left ||
                legendPosition == CircularLegendPosition::Right) {
                legendBandSize = maxRow + 12.0;
            } else {
                legendBandSize = std::max(24.0, legendFontSize + 14.0);
            }
        }

        // ---- Circle geometry ----
        double areaX = cachedPlotArea.x;
        double areaY = cachedPlotArea.y;
        double areaW = cachedPlotArea.width;
        double areaH = cachedPlotArea.height;
        switch (legendPosition) {
            case CircularLegendPosition::Left:   areaX += legendBandSize; areaW -= legendBandSize; break;
            case CircularLegendPosition::Right:  areaW -= legendBandSize; break;
            case CircularLegendPosition::Top:    areaY += legendBandSize; areaH -= legendBandSize; break;
            case CircularLegendPosition::Bottom: areaH -= legendBandSize; break;
        }
        if (legendStyle == CircularLegendStyle::NoLegend) legendBandSize = 0.0;

        double tipMargin = tipLabelContent != RingTipLabel::NoLabel ? tipFontSize + 6.0 : 0.0;
        cachedCenter = Point2Dd(areaX + areaW / 2.0, areaY + areaH / 2.0);
        float maxR = static_cast<float>(std::min(areaW, areaH) * 0.5 - 4.0 - tipMargin);
        cachedOuterRadius = std::max(10.0f, maxR);
        cachedInnerRadius = cachedOuterRadius * innerRadiusFraction;

        // ---- Per-ring radii & thickness ----
        size_t count = ringLayouts.size();
        float band = cachedOuterRadius - cachedInnerRadius;
        float step = band / static_cast<float>(count);
        for (RingLayout& layout : ringLayouts) {
            float autoThickness = std::max(2.0f, step - ringSpacing);
            layout.thickness = ringThickness > 0.0f
                             ? std::min(ringThickness, autoThickness)
                             : autoThickness;
            layout.radius = cachedInnerRadius
                          + step * (static_cast<float>(layout.ringIndex) + 0.5f);
        }

        RenderTitle(ctx);

        if (subStyle == CircularProgressStyle::ProgressPie) {
            RenderProgressPie(ctx);
        } else {
            RenderRings(ctx);
            RenderCenterDisc(ctx);
        }

        RenderTipLabels(ctx);
        RenderNameLabels(ctx);
        RenderLegend(ctx);
    }

    void UltraCanvasCircularProgressChart::RenderTitle(IRenderContext* ctx) {
        if (chartTitle.empty()) return;
        ctx->PushState();
        ctx->SetFontFamily(nameFontFamily);
        ctx->SetFontSize(nameFontSize + 5.0f);
        ctx->SetFontWeight(FontWeight::Bold);
        ctx->SetTextPaint(nameLabelColor);
        Size2Di ts = ctx->GetTextLineDimensions(chartTitle);
        ctx->DrawText(chartTitle,
                      Point2Dd(cachedPlotArea.x + (cachedPlotArea.width - ts.width) / 2.0,
                               cachedPlotArea.y - ts.height - 6.0));
        ctx->PopState();
    }

    void UltraCanvasCircularProgressChart::RenderRings(IRenderContext* ctx) {
        for (const RingLayout& layout : ringLayouts) {
            bool highlighted = hoverHighlightEnabled && layout.ringIndex == hoveredRing;
            DrawRingArc(ctx, layout, highlighted);
        }
    }

    void UltraCanvasCircularProgressChart::DrawRingArc(IRenderContext* ctx,
                                                       const RingLayout& layout,
                                                       bool highlighted) {
        ctx->PushState();

        // Background track — a full circle under the value arc.
        Color track = TrackColorFor(layout);
        if (track.a > 0) {
            ctx->SetStrokePaint(track);
            ctx->SetStrokeWidth(layout.thickness);
            ctx->SetLineCap(LineCap::Butt);
            ctx->DrawCircle(cachedCenter, layout.radius);
        }

        // Value arc.
        double sweep = layout.endAngle - layout.startAngle;
        if (sweep * layout.radius > 0.5) {
            Color color = highlighted ? layout.color.Lighten(0.2f) : layout.color;
            ctx->SetStrokePaint(color);
            ctx->SetStrokeWidth(highlighted ? layout.thickness * 1.12f : layout.thickness);
            ctx->SetLineCap(capStyle == RingCapStyle::Round ? LineCap::Round : LineCap::Butt);
            ctx->DrawArc(cachedCenter.x, cachedCenter.y, layout.radius,
                         layout.startAngle, layout.endAngle);
        }

        ctx->PopState();
    }

    void UltraCanvasCircularProgressChart::RenderProgressPie(IRenderContext* ctx) {
        const RingLayout& layout = ringLayouts.front();
        float radius = cachedOuterRadius;
        bool highlighted = hoverHighlightEnabled && layout.ringIndex == hoveredRing;

        ctx->PushState();

        Color track = TrackColorFor(layout);
        if (track.a > 0) {
            ctx->SetFillPaint(track);
            ctx->FillCircle(cachedCenter, radius);
        }

        if (layout.endAngle > layout.startAngle) {
            ctx->SetFillPaint(highlighted ? layout.color.Lighten(0.2f) : layout.color);
            ctx->ClearPath();
            ctx->MoveTo(cachedCenter.x, cachedCenter.y);
            ctx->Arc(cachedCenter.x, cachedCenter.y, radius,
                     layout.startAngle, layout.endAngle);
            ctx->LineTo(cachedCenter.x, cachedCenter.y);
            ctx->ClosePath();
            ctx->FillPathPreserve();
            ctx->ClearPath();
        }

        ctx->PopState();
    }

    void UltraCanvasCircularProgressChart::RenderTipLabels(IRenderContext* ctx) {
        if (tipLabelContent == RingTipLabel::NoLabel) return;
        if (subStyle == CircularProgressStyle::ProgressPie) return;

        ctx->PushState();
        ctx->SetFontFamily(tipFontFamily);
        ctx->SetFontSize(tipFontSize);
        ctx->SetFontWeight(tipFontWeight);

        for (const RingLayout& layout : ringLayouts) {
            std::string text = TipLabelText(layout);
            if (text.empty()) continue;

            Size2Di ts = ctx->GetTextLineDimensions(text);
            Point2Dd tip = ArcTipPoint(layout);

            // Nudge the label past the rounded cap, along the sweep direction.
            double tipAngle = ScreenAngle(layout, 1.0);
            double tangent = tipAngle + (clockwise ? 1.0 : -1.0) * M_PI / 2.0;
            double push = layout.thickness * 0.5 + ts.width * 0.5 + 4.0;
            Point2Dd pos(tip.x + push * std::cos(tangent) - ts.width / 2.0,
                         tip.y + push * std::sin(tangent) - ts.height / 2.0);

            if (tipLabelBubble) {
                ctx->SetFillPaint(Color(255, 255, 255, 225));
                ctx->FillRoundedRectangle(
                    Rect2Dd(pos.x - 5.0, pos.y - 2.0, ts.width + 10.0, ts.height + 4.0),
                    (ts.height + 4.0) / 2.0);
            }
            ctx->SetTextPaint(tipLabelColor);
            ctx->DrawText(text, pos);
        }
        ctx->PopState();
    }

    void UltraCanvasCircularProgressChart::RenderNameLabels(IRenderContext* ctx) {
        if (ringNamePosition == RingNamePosition::Hidden) return;
        if (subStyle == CircularProgressStyle::ProgressPie) return;

        if (ringNamePosition == RingNamePosition::StackedStart) {
            RenderStackedStartLabels(ctx);
            return;
        }

        // InsideStart: horizontal text inside the band, just past the arc base.
        ctx->PushState();
        ctx->SetFontFamily(nameFontFamily);
        ctx->SetFontSize(nameFontSize);
        ctx->SetFontWeight(nameFontWeight);
        ctx->SetTextPaint(nameLabelColor);

        for (const RingLayout& layout : ringLayouts) {
            const std::string& name = rings[layout.ringIndex].label;
            if (name.empty()) continue;

            Size2Di ts = ctx->GetTextLineDimensions(name);
            if (ts.height > layout.thickness + 2.0f) continue;   // band too thin

            // Angular offset that clears half the text width plus the cap.
            double offset = (ts.width * 0.5 + layout.thickness * 0.5 + 6.0)
                          / std::max(1.0, layout.radius);
            double sweep = layout.endAngle - layout.startAngle;
            if (offset * 2.0 > sweep) continue;                  // arc too short

            double a = clockwise ? (ScreenAngle(layout, 0.0) + offset)
                                 : (ScreenAngle(layout, 0.0) - offset);
            Point2Dd pos(cachedCenter.x + layout.radius * std::cos(a) - ts.width / 2.0,
                         cachedCenter.y + layout.radius * std::sin(a) - ts.height / 2.0);
            ctx->DrawText(name, pos);
        }
        ctx->PopState();
    }

    void UltraCanvasCircularProgressChart::RenderStackedStartLabels(IRenderContext* ctx) {
        ctx->PushState();
        ctx->SetFontFamily(nameFontFamily);
        ctx->SetFontSize(nameFontSize);

        double baseAngle = startAngleDeg * M_PI / 180.0;
        double bx = std::cos(baseAngle);
        double by = std::sin(baseAngle);

        for (const RingLayout& layout : ringLayouts) {
            const ProgressRing& ring = rings[layout.ringIndex];
            std::string valueText = tipLabelContent == RingTipLabel::Value
                                  ? FormatValue(ring.value)
                                  : FormatPercent(layout.fraction);
            std::string nameText = ring.label.empty() ? "" : ring.label + " ";

            ctx->SetFontWeight(nameFontWeight);
            Size2Di ns = ctx->GetTextLineDimensions(nameText);
            ctx->SetFontWeight(FontWeight::Bold);
            Size2Di vs = ctx->GetTextLineDimensions(valueText);

            // Anchor at the ring's start point, offset against the row
            // direction so the text ends just short of the arc base.
            Point2Dd anchor(cachedCenter.x + layout.radius * bx,
                            cachedCenter.y + layout.radius * by);
            double rowW = ns.width + vs.width;
            double rowH = std::max(ns.height, vs.height);
            Point2Dd pos(anchor.x - rowW - layout.thickness * 0.5 - 8.0,
                         anchor.y - rowH / 2.0);

            if (!nameText.empty()) {
                ctx->SetFontWeight(nameFontWeight);
                ctx->SetTextPaint(nameLabelColor);
                ctx->DrawText(nameText, pos);
            }
            ctx->SetFontWeight(FontWeight::Bold);
            ctx->SetTextPaint(tipLabelColor);
            ctx->DrawText(valueText, Point2Dd(pos.x + ns.width, pos.y));
        }
        ctx->PopState();
    }

    void UltraCanvasCircularProgressChart::RenderCenterDisc(IRenderContext* ctx) {
        float discRadius = std::max(0.0f, cachedInnerRadius - std::max(4.0f, ringSpacing));
        if (subStyle == CircularProgressStyle::SingleRing) {
            const RingLayout& layout = ringLayouts.front();
            discRadius = std::max(0.0f,
                static_cast<float>(layout.radius) - layout.thickness * 0.5f
                - std::max(4.0f, ringSpacing));
        }

        if (showCenterDisc && discRadius > 2.0f) {
            if (centerFillColor.a > 0) {
                ctx->SetFillPaint(centerFillColor);
                ctx->FillCircle(cachedCenter, discRadius);
            }
            if (centerBorderColor.a > 0) {
                ctx->SetStrokePaint(centerBorderColor);
                ctx->SetStrokeWidth(1.5f);
                ctx->DrawCircle(cachedCenter, discRadius);
            }
        }

        if (centerTitle.empty() && centerSubtitle.empty()) return;

        ctx->PushState();
        ctx->SetFontFamily(centerFontFamily);
        ctx->SetTextPaint(centerTextColor);

        ctx->SetFontSize(centerFontSize);
        ctx->SetFontWeight(centerFontWeight);
        Size2Di titleSize = centerTitle.empty() ? Size2Di{0, 0}
                          : ctx->GetTextLineDimensions(centerTitle);

        float subFontSize = std::max(8.0f, centerFontSize * 0.72f);
        ctx->SetFontSize(subFontSize);
        ctx->SetFontWeight(FontWeight::Normal);
        Size2Di subSize = centerSubtitle.empty() ? Size2Di{0, 0}
                        : ctx->GetTextLineDimensions(centerSubtitle);

        double gap = (titleSize.height > 0 && subSize.height > 0) ? 3.0 : 0.0;
        double totalH = subSize.height + gap + titleSize.height;
        double top = cachedCenter.y - totalH / 2.0;

        // Subtitle above the title, matching the "Completed / 72%" convention.
        if (!centerSubtitle.empty()) {
            ctx->SetFontSize(subFontSize);
            ctx->SetFontWeight(FontWeight::Normal);
            ctx->DrawText(centerSubtitle,
                          Point2Dd(cachedCenter.x - subSize.width / 2.0, top));
            top += subSize.height + gap;
        }
        if (!centerTitle.empty()) {
            ctx->SetFontSize(centerFontSize);
            ctx->SetFontWeight(centerFontWeight);
            ctx->DrawText(centerTitle,
                          Point2Dd(cachedCenter.x - titleSize.width / 2.0, top));
        }
        ctx->PopState();
    }

    void UltraCanvasCircularProgressChart::RenderLegend(IRenderContext* ctx) {
        if (legendStyle == CircularLegendStyle::NoLegend) return;

        ctx->PushState();
        ctx->SetFontFamily(legendFontFamily);
        ctx->SetFontSize(legendFontSize);
        ctx->SetFontWeight(FontWeight::Normal);

        bool vertical = legendPosition == CircularLegendPosition::Left ||
                        legendPosition == CircularLegendPosition::Right;
        double chipW = legendStyle == CircularLegendStyle::NumberedChips ? 26.0 : 14.0;
        double chipH = legendStyle == CircularLegendStyle::NumberedChips ? 18.0 : 14.0;
        double rowH  = std::max(chipH + 6.0, legendFontSize + 8.0);

        // Row extents, needed to centre the block.
        std::vector<double> rowWidths;
        double maxRow = 0.0;
        for (const RingLayout& layout : ringLayouts) {
            const ProgressRing& ring = rings[layout.ringIndex];
            Size2Di ts = ctx->GetTextLineDimensions(ring.label);
            double w = chipW + 8.0 + ts.width;
            if (!ring.iconPath.empty()) w += 22.0;
            rowWidths.push_back(w);
            maxRow = std::max(maxRow, w);
        }

        double x = 0.0, y = 0.0;
        size_t count = ringLayouts.size();
        switch (legendPosition) {
            case CircularLegendPosition::Left:
                x = cachedPlotArea.x;
                y = cachedCenter.y - rowH * count / 2.0;
                break;
            case CircularLegendPosition::Right:
                x = cachedPlotArea.x + cachedPlotArea.width - maxRow;
                y = cachedCenter.y - rowH * count / 2.0;
                break;
            case CircularLegendPosition::Top:
                y = cachedPlotArea.y;
                break;
            case CircularLegendPosition::Bottom:
                y = cachedPlotArea.y + cachedPlotArea.height - rowH;
                break;
        }

        double totalRowW = 0.0;
        for (double w : rowWidths) totalRowW += w + 16.0;
        if (!vertical) x = cachedCenter.x - (totalRowW - 16.0) / 2.0;

        for (size_t i = 0; i < count; ++i) {
            const RingLayout& layout = ringLayouts[i];
            const ProgressRing& ring = rings[layout.ringIndex];
            double cx = x;

            if (legendStyle == CircularLegendStyle::NumberedChips) {
                ctx->SetFillPaint(layout.color);
                ctx->FillRoundedRectangle(Rect2Dd(cx, y + (rowH - chipH) / 2.0, chipW, chipH), 4.0);

                char num[8];
                std::snprintf(num, sizeof(num), "%02zu", i + 1);
                ctx->SetFontWeight(FontWeight::Bold);
                ctx->SetTextPaint(Colors::White);
                Size2Di ns = ctx->GetTextLineDimensions(num);
                ctx->DrawText(num, Point2Dd(cx + (chipW - ns.width) / 2.0,
                                            y + (rowH - ns.height) / 2.0));
            } else {
                ctx->SetFillPaint(layout.color);
                ctx->FillRoundedRectangle(Rect2Dd(cx, y + (rowH - chipH) / 2.0, chipW, chipH), 3.0);
            }
            cx += chipW + 8.0;

            if (!ring.iconPath.empty()) {
                ctx->DrawImage(ring.iconPath,
                               Rect2Dd(cx, y + (rowH - 16.0) / 2.0, 16.0, 16.0),
                               ImageFitMode::Contain);
                cx += 22.0;
            }

            ctx->SetFontWeight(FontWeight::Normal);
            ctx->SetTextPaint(legendTextColor);
            Size2Di ts = ctx->GetTextLineDimensions(ring.label);
            ctx->DrawText(ring.label, Point2Dd(cx, y + (rowH - ts.height) / 2.0));

            if (vertical) {
                y += rowH;
            } else {
                x += rowWidths[i] + 16.0;
            }
        }
        ctx->PopState();
    }

    // =========================================================================
    // HIT TESTING & EVENTS
    // =========================================================================

    size_t UltraCanvasCircularProgressChart::HitTestRing(const Point2Dd& localPos) const {
        if (ringLayouts.empty()) return SIZE_MAX;

        double dx = localPos.x - cachedCenter.x;
        double dy = localPos.y - cachedCenter.y;
        double r = std::sqrt(dx * dx + dy * dy);
        double angle = std::atan2(dy, dx);

        if (subStyle == CircularProgressStyle::ProgressPie) {
            const RingLayout& layout = ringLayouts.front();
            if (r > cachedOuterRadius) return SIZE_MAX;
            double a = angle;
            while (a < layout.startAngle) a += 2.0 * M_PI;
            while (a >= layout.startAngle + 2.0 * M_PI) a -= 2.0 * M_PI;
            if (a <= layout.endAngle) return layout.ringIndex;
            return SIZE_MAX;
        }

        for (const RingLayout& layout : ringLayouts) {
            if (std::fabs(r - layout.radius) > layout.thickness * 0.5 + 2.0) continue;
            double a = angle;
            while (a < layout.startAngle) a += 2.0 * M_PI;
            while (a >= layout.startAngle + 2.0 * M_PI) a -= 2.0 * M_PI;
            if (a <= layout.endAngle) return layout.ringIndex;
        }
        return SIZE_MAX;
    }

    bool UltraCanvasCircularProgressChart::HandleChartMouseMove(const Point2Di& mousePos) {
        if (ringLayouts.empty()) return false;

        Point2Dd local(static_cast<double>(mousePos.x), static_cast<double>(mousePos.y));
        size_t hit = HitTestRing(local);

        if (hit != hoveredRing) {
            hoveredRing = hit;
            if (hit != SIZE_MAX && onRingHover) onRingHover(hit);
            RequestRedraw();
        }

        if (hit != SIZE_MAX) {
            if (enableTooltips) {
                std::string tooltip = BuildTooltipText(hit);
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

    bool UltraCanvasCircularProgressChart::HandleClick(const UCEvent& event) {
        Point2Dd local(static_cast<double>(event.pointer.x),
                       static_cast<double>(event.pointer.y));
        size_t hit = HitTestRing(local);
        if (hit != SIZE_MAX) {
            if (onRingClick) onRingClick(hit);
            return true;
        }
        return false;
    }

    bool UltraCanvasCircularProgressChart::OnEvent(const UCEvent& event) {
        if (IsDisabled() || !IsVisible()) return false;

        switch (event.type) {
            case UCEventType::MouseDown:
                if (event.button == UCMouseButton::Left && HandleClick(event)) {
                    return true;
                }
                break;
            case UCEventType::MouseLeave:
                if (hoveredRing != SIZE_MAX) {
                    hoveredRing = SIZE_MAX;
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

    std::shared_ptr<UltraCanvasCircularProgressChart> CreateCircularProgressChart(
        const std::string& id, int x, int y, int w, int h)
    {
        return std::make_shared<UltraCanvasCircularProgressChart>(id, x, y, w, h);
    }

    std::shared_ptr<UltraCanvasCircularProgressChart> CreateConcentricRingChart(
        const std::string& id, int x, int y, int w, int h,
        const std::vector<std::pair<std::string, double>>& labeledPercentages)
    {
        auto chart = std::make_shared<UltraCanvasCircularProgressChart>(id, x, y, w, h);
        for (const auto& entry : labeledPercentages) {
            chart->AddRing(entry.first, entry.second);
        }
        return chart;
    }

    std::shared_ptr<UltraCanvasCircularProgressChart> CreateProgressPieChart(
        const std::string& id, int x, int y, int w, int h,
        const std::string& label, double percent, const Color& color)
    {
        auto chart = std::make_shared<UltraCanvasCircularProgressChart>(id, x, y, w, h);
        chart->SetSubStyle(CircularProgressStyle::ProgressPie);
        chart->AddRing(label, percent, color);
        chart->SetTipLabelContent(RingTipLabel::NoLabel);
        return chart;
    }

} // namespace UltraCanvas
