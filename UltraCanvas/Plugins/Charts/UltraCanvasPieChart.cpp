// Plugins/Charts/UltraCanvasPieChart.cpp
// Comprehensive pie / donut / 3D chart element for UltraCanvas
// Version: 1.0.0
// Last Modified: 2026-05-14
// Author: UltraCanvas Framework
#include "Plugins/Charts/UltraCanvasPieChart.h"
#include "../../libspecific/Cairo/RenderContextCairo.h"

#include <cairo/cairo.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace UltraCanvas {

    // ===== DEFAULTS =====

    std::vector<Color> UltraCanvasPieChartElement::DefaultPalette() {
        return {
            Color( 54, 162, 235, 255),
            Color(255,  99, 132, 255),
            Color(255, 205,  86, 255),
            Color( 75, 192, 192, 255),
            Color(153, 102, 255, 255),
            Color(255, 159,  64, 255),
            Color(199, 199, 199, 255),
            Color( 83, 102, 255, 255),
            Color(255,  99, 255, 255),
            Color( 99, 255, 132, 255)
        };
    }

    // ===== CONSTRUCTOR =====

    UltraCanvasPieChartElement::UltraCanvasPieChartElement(
        const std::string& id, int x, int y, int w, int h)
        : UltraCanvasChartElementBase(id, x, y, w, h)
    {
        colorPalette = DefaultPalette();
        showGrid = false;
        showAxes = false;
        showBackground = false;
    }

    // ===== SETTERS =====

    void UltraCanvasPieChartElement::SetColorPalette(const std::vector<Color>& p) {
        if (p.empty()) {
            colorPalette = DefaultPalette();
        } else {
            colorPalette = p;
        }
        InvalidateSlices();
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPieChartElement::SetBorderColor(const Color& c) { borderColor = c; RequestRedraw(); }
    void UltraCanvasPieChartElement::SetBorderWidth(float w)        { borderWidth = std::max(0.0f, w); RequestRedraw(); }

    void UltraCanvasPieChartElement::SetDonutMode(bool on) {
        donutMode = on;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPieChartElement::SetInnerRadius(float fraction) {
        innerRadiusFraction = std::max(0.0f, std::min(0.95f, fraction));
        RequestRedraw();
    }

    void UltraCanvasPieChartElement::SetGlobalExplosion(float fraction) {
        globalExplosion = std::max(0.0f, std::min(0.5f, fraction));
        InvalidateSlices();
        RequestRedraw();
    }

    void UltraCanvasPieChartElement::SetSliceExplosion(size_t index, float fraction) {
        sliceExplosionOverrides[index] = std::max(0.0f, std::min(0.5f, fraction));
        InvalidateSlices();
        RequestRedraw();
    }

    void UltraCanvasPieChartElement::ClearSliceExplosion(size_t index) {
        sliceExplosionOverrides.erase(index);
        InvalidateSlices();
        RequestRedraw();
    }

    void UltraCanvasPieChartElement::ClearAllSliceExplosions() {
        sliceExplosionOverrides.clear();
        InvalidateSlices();
        RequestRedraw();
    }

    void UltraCanvasPieChartElement::SetLabelPosition(LabelPosition p) {
        labelPosition = p;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPieChartElement::SetLabelContent(LabelContent c)        { labelContent = c; RequestRedraw(); }
    void UltraCanvasPieChartElement::SetLeaderLinesEnabled(bool on)         { leaderLinesEnabled = on; RequestRedraw(); }
    void UltraCanvasPieChartElement::SetLeaderLineStyle(const Color& c, float w) {
        leaderLineColor = c; leaderLineWidth = w; RequestRedraw();
    }
    void UltraCanvasPieChartElement::SetLabelFont(const std::string& family, float size, FontWeight weight) {
        labelFontFamily = family; labelFontSize = size; labelFontWeight = weight; RequestRedraw();
    }
    void UltraCanvasPieChartElement::SetLabelColor(const Color& c)              { labelColor = c; RequestRedraw(); }
    void UltraCanvasPieChartElement::SetLabelBackgroundEnabled(bool on)         { labelBackgroundEnabled = on; RequestRedraw(); }
    void UltraCanvasPieChartElement::SetLabelBackgroundColor(const Color& c)    { labelBackgroundColor = c; RequestRedraw(); }
    void UltraCanvasPieChartElement::SetPercentageFormatter(std::function<std::string(double)> f) { percentFormatter = std::move(f); RequestRedraw(); }
    void UltraCanvasPieChartElement::SetValueFormatter(std::function<std::string(double)> f)      { valueFormatter = std::move(f); RequestRedraw(); }

    void UltraCanvasPieChartElement::SetSliceGradient(size_t index, const std::vector<GradientStop>& stops) {
        sliceGradients[index] = stops;
        autoGradients = false;
        RequestRedraw();
    }

    void UltraCanvasPieChartElement::SetAutoRadialGradients() {
        autoGradients = true;
        sliceGradients.clear();
        RequestRedraw();
    }

    void UltraCanvasPieChartElement::ClearSliceGradients() {
        sliceGradients.clear();
        autoGradients = false;
        RequestRedraw();
    }

    void UltraCanvasPieChartElement::SetSlicePattern(size_t index, std::shared_ptr<IPaintPattern> pattern) {
        slicePatterns[index] = std::move(pattern);
        RequestRedraw();
    }

    void UltraCanvasPieChartElement::ClearSlicePatterns() {
        slicePatterns.clear();
        RequestRedraw();
    }

    void UltraCanvasPieChartElement::Enable3DMode(float depth, float perspectiveDeg) {
        enable3D = true;
        depthHeight = std::max(0.0f, depth);
        perspectiveAngleDeg = std::max(0.0f, std::min(85.0f, perspectiveDeg));
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPieChartElement::Disable3DMode() {
        enable3D = false;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasPieChartElement::SetLightDirection(Point2Df dir) {
        double len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (len > 1e-6) {
            lightDirection = Point2Df(dir.x / len, dir.y / len);
        }
        RequestRedraw();
    }
    void UltraCanvasPieChartElement::SetAmbientLight(float a) { ambientLight = std::max(0.0f, std::min(1.0f, a)); RequestRedraw(); }
    void UltraCanvasPieChartElement::SetDiffuseLight(float d) { diffuseLight = std::max(0.0f, std::min(1.0f, d)); RequestRedraw(); }

    // ===== PLOT AREA =====

    ChartPlotArea UltraCanvasPieChartElement::CalculatePlotArea() {
        int marginSide   = 10;
        int marginTop    = chartTitle.empty() ? 10 : 32;
        int marginBottom = 10;

        bool needsLabelRoom =
            (labelPosition == LabelPosition::Outside || labelPosition == LabelPosition::Auto);
        if (needsLabelRoom) {
            // Reserve enough room on the sides for outside labels + leader-line gap.
            // We estimate label width before having a render context, then cap at
            // ~40% of the element width so we never starve the plot itself.
            int estLabelW  = EstimateLongestLabelWidth();
            int leaderGap  = 14;                                  // elbow + gap to text
            marginSide     = std::max(40, estLabelW + leaderGap);
            marginSide     = std::min(marginSide, std::max(40, GetWidth() * 4 / 10));

            // Vertical room: budget for the tallest possible label block (could be 2-line).
            int lineH      = EstimateLabelLineHeight();
            int lineCount  = EstimateLabelLineCount();
            int vRoom      = lineH * lineCount + 10;
            marginTop      = chartTitle.empty() ? std::max(20, vRoom)
                                                : std::max(40, 24 + vRoom);
            marginBottom   = std::max(20, vRoom);
            marginBottom   = std::min(marginBottom, std::max(20, GetHeight() * 3 / 10));
            marginTop      = std::min(marginTop,    std::max(20, GetHeight() * 4 / 10));
        }

        ChartPlotArea area;
        area.x      = marginSide;
        area.y      = marginTop;
        area.width  = std::max(20, GetWidth()  - 2 * marginSide);
        area.height = std::max(20, GetHeight() - marginTop - marginBottom);
        return area;
    }

    // Rough label-width estimate without a render context. Uses ~0.6 * font size
    // as average glyph width — close enough for margin budgeting in proportional
    // sans-serif fonts. Always overestimates slightly, which is what we want.
    int UltraCanvasPieChartElement::EstimateLongestLabelWidth() const {
        const float glyphW = labelFontSize * 0.6f;
        if (!dataSource) return static_cast<int>(glyphW * 10);

        size_t count = dataSource->GetPointCount();
        if (count == 0) return static_cast<int>(glyphW * 10);

        size_t maxNameLen = 0;
        double total = 0.0, maxValue = 0.0;
        for (size_t i = 0; i < count; ++i) {
            ChartDataPoint p = dataSource->GetPoint(i);
            double v = p.value != 0.0 ? p.value : p.y;
            if (v <= 0.0) continue;
            total += v;
            if (v > maxValue) maxValue = v;
            if (p.label.size() > maxNameLen) maxNameLen = p.label.size();
        }
        if (total <= 0.0) return static_cast<int>(glyphW * 10);

        std::string valStr = FormatValue(maxValue);
        std::string pctStr = FormatPercent(maxValue / total);

        size_t composed = maxNameLen;
        switch (labelContent) {
            case LabelContent::Name:            composed = maxNameLen; break;
            case LabelContent::Value:           composed = valStr.size(); break;
            case LabelContent::Percentage:      composed = pctStr.size(); break;
            case LabelContent::NameValue:       composed = maxNameLen + 2 + valStr.size(); break;
            case LabelContent::NamePercentage:  composed = maxNameLen + 2 + pctStr.size(); break;
            case LabelContent::ValuePercentage: composed = valStr.size() + 3 + pctStr.size(); break;
            case LabelContent::All:
                composed = std::max(maxNameLen, valStr.size() + 3 + pctStr.size());
                break;
        }
        return static_cast<int>(composed * glyphW);
    }

    int UltraCanvasPieChartElement::EstimateLabelLineHeight() const {
        return static_cast<int>(labelFontSize * 1.25f + 0.5f);
    }

    int UltraCanvasPieChartElement::EstimateLabelLineCount() const {
        return (labelContent == LabelContent::All) ? 2 : 1;
    }

    // ===== SLICE BUILDING =====

    void UltraCanvasPieChartElement::InvalidateSlices() {
        slicesValid = false;
    }

    Color UltraCanvasPieChartElement::ResolveSliceColor(size_t index, const Color& dataColor) const {
        if (dataColor.a > 0) return dataColor;
        if (colorPalette.empty()) return Color(128, 128, 128, 255);
        return colorPalette[index % colorPalette.size()];
    }

    float UltraCanvasPieChartElement::GetSliceExplosion(size_t index) const {
        auto it = sliceExplosionOverrides.find(index);
        if (it != sliceExplosionOverrides.end()) return it->second;
        return globalExplosion;
    }

    void UltraCanvasPieChartElement::RebuildSlices() {
        cachedSlices.clear();
        slicesValid = true;

        if (!dataSource) return;
        size_t count = dataSource->GetPointCount();
        if (count == 0) return;

        double total = 0.0;
        std::vector<std::pair<size_t, ChartDataPoint>> raw;
        raw.reserve(count);

        for (size_t i = 0; i < count; ++i) {
            ChartDataPoint p = dataSource->GetPoint(i);
            double v = p.value != 0.0 ? p.value : p.y;
            if (v > 0.0) {
                raw.emplace_back(i, p);
                total += v;
            }
        }

        if (total <= 0.0) return;

        double startAngle = -M_PI / 2.0;
        for (auto& kv : raw) {
            const ChartDataPoint& p = kv.second;
            double v = p.value != 0.0 ? p.value : p.y;
            double pct = v / total;
            double sweep = pct * 2.0 * M_PI;
            double endAngle = startAngle + sweep;

            Slice s;
            s.index      = kv.first;
            s.name       = p.label;
            s.value      = v;
            s.percentage = pct;
            s.startAngle = startAngle;
            s.endAngle   = endAngle;
            s.midAngle   = startAngle + sweep / 2.0;
            s.baseColor  = ResolveSliceColor(s.index, p.color);
            s.explosion  = GetSliceExplosion(s.index);
            cachedSlices.push_back(s);

            startAngle = endAngle;
        }
    }

    void UltraCanvasPieChartElement::ComputeCenterAndRadius(const Rect2Df& chartArea) {
        cachedCenter = Point2Df(chartArea.x + chartArea.width / 2.0,
                                chartArea.y + chartArea.height / 2.0);
        float availH = chartArea.height;
        if (enable3D) {
            // Reserve space for the depth strip below the ellipse and for the
            // vertical squash from the perspective tilt.
            availH = std::max<float>(20.0f, availH - depthHeight);
        }
        float baseR = std::min<float>(chartArea.width, availH) * 0.5f - 4.0f;
        // Account for explosion outward push so slices stay on-canvas.
        float maxExplosion = globalExplosion;
        for (auto& kv : sliceExplosionOverrides) maxExplosion = std::max(maxExplosion, kv.second);
        baseR /= (1.0f + maxExplosion);
        cachedOuterRadius = std::max(10.0f, baseR);
    }

    // ===== FORMATTERS =====

    std::string UltraCanvasPieChartElement::FormatPercent(double pct) const {
        if (percentFormatter) return percentFormatter(pct);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f%%", pct * 100.0);
        return std::string(buf);
    }

    std::string UltraCanvasPieChartElement::FormatValue(double v) const {
        if (valueFormatter) return valueFormatter(v);
        char buf[32];
        if (std::floor(v) == v && std::fabs(v) < 1e12) {
            std::snprintf(buf, sizeof(buf), "%.0f", v);
        } else {
            std::snprintf(buf, sizeof(buf), "%g", v);
        }
        return std::string(buf);
    }

    std::string UltraCanvasPieChartElement::BuildLabelText(const Slice& s) const {
        switch (labelContent) {
            case LabelContent::Name:           return s.name;
            case LabelContent::Value:          return FormatValue(s.value);
            case LabelContent::Percentage:     return FormatPercent(s.percentage);
            case LabelContent::NameValue:      return s.name + ": " + FormatValue(s.value);
            case LabelContent::NamePercentage: return s.name + ": " + FormatPercent(s.percentage);
            case LabelContent::ValuePercentage:return FormatValue(s.value) + " (" + FormatPercent(s.percentage) + ")";
            case LabelContent::All: {
                std::string text = s.name + "\n" + FormatValue(s.value) + " (" + FormatPercent(s.percentage) + ")";
                return text;
            }
        }
        return s.name;
    }

    // Compact, line-broken variant used for inside placement. Splitting the name
    // and the value/percentage onto separate lines roughly halves the width of
    // each line, which makes the label far more likely to fit within a slice.
    std::string UltraCanvasPieChartElement::BuildInsideLabelText(const Slice& s) const {
        switch (labelContent) {
            case LabelContent::Name:            return s.name;
            case LabelContent::Value:           return FormatValue(s.value);
            case LabelContent::Percentage:      return FormatPercent(s.percentage);
            case LabelContent::NameValue:       return s.name + "\n" + FormatValue(s.value);
            case LabelContent::NamePercentage:  return s.name + "\n" + FormatPercent(s.percentage);
            case LabelContent::ValuePercentage: return FormatValue(s.value) + "\n" + FormatPercent(s.percentage);
            case LabelContent::All:
                return s.name + "\n" + FormatValue(s.value) + "\n" + FormatPercent(s.percentage);
        }
        return s.name;
    }

    // ===== RENDER ENTRY POINT =====

    void UltraCanvasPieChartElement::RenderChart(IRenderContext* ctx) {
        if (!ctx) return;
        UpdateRenderingCache();
        if (!slicesValid) RebuildSlices();

        if (cachedSlices.empty()) {
            DrawEmptyState(ctx);
            return;
        }

        Rect2Df chartArea(cachedPlotArea.x, cachedPlotArea.y,
                          cachedPlotArea.width, cachedPlotArea.height);
        RenderChartImpl(ctx, chartArea);
    }

    void UltraCanvasPieChartElement::RenderChartImpl(IRenderContext* ctx, const Rect2Df& chartArea) {
        ComputeCenterAndRadius(chartArea);

        if (!chartTitle.empty()) {
            ctx->PushState();
            ctx->SetFontFamily(labelFontFamily);
            ctx->SetFontSize(labelFontSize + 2.0f);
            ctx->SetFontWeight(FontWeight::Bold);
            ctx->SetTextPaint(labelColor);
            Size2Di ts = ctx->GetTextLineDimensions(chartTitle);
            ctx->DrawText(chartTitle,
                          Point2Df(chartArea.x + (chartArea.width - ts.width) / 2.0,
                                   chartArea.y - ts.height - 4.0));
            ctx->PopState();
        }

        if (enable3D) {
            Render3D(ctx, chartArea);
        } else {
            Render2D(ctx, chartArea);
        }

        RenderLabels(ctx, cachedCenter, cachedOuterRadius,
                     enable3D ? std::cos(perspectiveAngleDeg * M_PI / 180.0) : 1.0);
    }

    // ===== 2D RENDERING =====

    void UltraCanvasPieChartElement::Render2D(IRenderContext* ctx, const Rect2Df& /*chartArea*/) {
        float outerR = cachedOuterRadius;
        float innerR = donutMode ? outerR * innerRadiusFraction : 0.0f;

        for (const Slice& s : cachedSlices) {
            DrawSlice2D(ctx, cachedCenter, outerR, innerR, s);
        }
    }

    void UltraCanvasPieChartElement::DrawSlice2D(IRenderContext* ctx,
                                                 const Point2Df& center,
                                                 float outerR,
                                                 float innerR,
                                                 const Slice& s) {
        double ex = std::cos(s.midAngle) * s.explosion * outerR;
        double ey = std::sin(s.midAngle) * s.explosion * outerR;
        Point2Df sliceCenter(center.x + ex, center.y + ey);

        ctx->PushState();
        ctx->Translate(sliceCenter.x, sliceCenter.y);

        // Resolve fill paint
        auto patternIt = slicePatterns.find(s.index);
        bool usedPattern = false;
        if (patternIt != slicePatterns.end() && patternIt->second) {
            ctx->SetFillPaint(patternIt->second);
            usedPattern = true;
        } else if (autoGradients || sliceGradients.count(s.index)) {
            auto grad = CreateSliceGradient(ctx, outerR, s);
            if (grad) { ctx->SetFillPaint(grad); usedPattern = true; }
        }
        if (!usedPattern) {
            ctx->SetFillPaint(s.baseColor);
        }

        if (donutMode && innerR > 0.0f) {
            // Build annular sector polygon
            double sweep = s.endAngle - s.startAngle;
            int segments = std::max(4, (int)std::ceil(std::fabs(sweep) / (M_PI / 36.0)));
            std::vector<Point2Df> pts;
            pts.reserve(2 * (segments + 1));

            for (int i = 0; i <= segments; ++i) {
                double a = s.startAngle + sweep * (double)i / segments;
                pts.emplace_back(std::cos(a) * outerR, std::sin(a) * outerR);
            }
            for (int i = segments; i >= 0; --i) {
                double a = s.startAngle + sweep * (double)i / segments;
                pts.emplace_back(std::cos(a) * innerR, std::sin(a) * innerR);
            }
            ctx->FillLinePath(pts);

            if (borderWidth > 0.0f && borderColor.a > 0) {
                ctx->SetStrokePaint(borderColor);
                ctx->SetStrokeWidth(borderWidth);
                ctx->DrawLinePath(pts, true);
            }
        } else {
            // Full pie sector
            ctx->ClearPath();
            ctx->MoveTo(0.0, 0.0);
            ctx->Arc(0.0, 0.0, outerR, s.startAngle, s.endAngle);
            ctx->LineTo(0.0, 0.0);
            ctx->ClosePath();
            ctx->FillPathPreserve();
            if (borderWidth > 0.0f && borderColor.a > 0) {
                ctx->SetStrokePaint(borderColor);
                ctx->SetStrokeWidth(borderWidth);
                ctx->StrokePathPreserve();
            }
            ctx->ClearPath();
        }

        ctx->PopState();
    }

    std::shared_ptr<IPaintPattern> UltraCanvasPieChartElement::CreateSliceGradient(
        IRenderContext* ctx, float outerR, const Slice& s)
    {
        std::vector<GradientStop> stops;
        auto explicitIt = sliceGradients.find(s.index);
        if (explicitIt != sliceGradients.end() && !explicitIt->second.empty()) {
            stops = explicitIt->second;
        } else if (autoGradients) {
            stops.emplace_back(0.0,  s.baseColor.Lighten(0.45f));
            stops.emplace_back(0.85, s.baseColor);
            stops.emplace_back(1.0,  s.baseColor.Darken(0.20f));
        } else {
            return nullptr;
        }
        return ctx->CreateRadialGradientPattern(0.0, 0.0, 0.0, 0.0, 0.0, outerR, stops);
    }

    // ===== 3D RENDERING =====

    static Color ShadeColor(const Color& base, float ambient, float diffuse,
                            float dotLight) {
        float shade = ambient + diffuse * std::max(0.0f, dotLight);
        if (shade > 1.0f) shade = 1.0f;
        if (shade < 0.15f) shade = 0.15f;
        // Convert "shade" (brightness) into Darken factor: amount removed from full brightness.
        float darken = 1.0f - shade;
        return base.Darken(darken);
    }

    void UltraCanvasPieChartElement::Render3D(IRenderContext* ctx, const Rect2Df& /*chartArea*/) {
        float outerR = cachedOuterRadius;
        float innerR = donutMode ? outerR * innerRadiusFraction : 0.0f;
        double tilt = perspectiveAngleDeg * M_PI / 180.0;
        double vScale = std::cos(tilt);

        // Sort slices for painter's algorithm: back-most (smallest projected
        // mid-y on top face) first. After tilt, projected y at angle a is sin(a) * vScale.
        std::vector<Slice> sorted = cachedSlices;
        std::sort(sorted.begin(), sorted.end(),
                  [vScale](const Slice& a, const Slice& b) {
                      return std::sin(a.midAngle) * vScale < std::sin(b.midAngle) * vScale;
                  });

        // Side strips (back-to-front)
        for (const Slice& s : sorted) {
            DrawSlice3DSides(ctx, cachedCenter, outerR, innerR, vScale, depthHeight, s);
        }

        // Top faces
        for (const Slice& s : sorted) {
            DrawSlice3DTop(ctx, cachedCenter, outerR, innerR, vScale, s);
        }
    }

    void UltraCanvasPieChartElement::DrawSlice3DSides(IRenderContext* ctx,
                                                     const Point2Df& center,
                                                     float outerR,
                                                     float innerR,
                                                     double vScale,
                                                     float depth,
                                                     const Slice& s)
    {
        double ex = std::cos(s.midAngle) * s.explosion * outerR;
        double ey = std::sin(s.midAngle) * s.explosion * outerR * vScale;

        auto project = [&](double a, double r) -> Point2Df {
            return Point2Df(center.x + ex + r * std::cos(a),
                            center.y + ey + r * std::sin(a) * vScale);
        };

        double sweep = s.endAngle - s.startAngle;
        int segments = std::max(4, (int)std::ceil(std::fabs(sweep) / (M_PI / 36.0)));

        // ---- Outer cylinder strip — only front-facing portion ----
        // Front-facing on the projected ellipse corresponds to sin(a) > 0
        // (angles in lower half after our coordinate convention).
        double aFirst = std::max(s.startAngle, 0.0);
        double aLast  = std::min(s.endAngle, M_PI);
        if (aFirst < aLast) {
            int segs = std::max(4, (int)std::ceil((aLast - aFirst) / (M_PI / 36.0)));
            std::vector<Point2Df> pts;
            pts.reserve(2 * (segs + 1));
            for (int i = 0; i <= segs; ++i) {
                double a = aFirst + (aLast - aFirst) * (double)i / segs;
                pts.push_back(project(a, outerR));
            }
            for (int i = segs; i >= 0; --i) {
                double a = aFirst + (aLast - aFirst) * (double)i / segs;
                Point2Df top = project(a, outerR);
                pts.emplace_back(top.x, top.y + depth);
            }
            // Lighting: average normal across the visible arc — use mid angle
            double aMid = (aFirst + aLast) * 0.5;
            float nx = (float)std::cos(aMid);
            float ny = (float)std::sin(aMid);
            float dotL = nx * (float)lightDirection.x + ny * (float)lightDirection.y;
            Color c = ShadeColor(s.baseColor, ambientLight, diffuseLight, -dotL);
            ctx->SetFillPaint(c);
            ctx->FillLinePath(pts);
            if (borderWidth > 0.0f && borderColor.a > 0) {
                ctx->SetStrokePaint(borderColor);
                ctx->SetStrokeWidth(std::max(0.5f, borderWidth * 0.5f));
                ctx->DrawLinePath(pts, true);
            }
        }

        // ---- Donut: inner cylinder — back-facing portion of inner circle is visible ----
        if (donutMode && innerR > 0.0f) {
            double innerFirst = std::max(s.startAngle, -M_PI);
            double innerLast  = std::min(s.endAngle, 0.0);
            // The inner wall is visible where the outer is not — i.e. top arc of inner ellipse
            if (innerFirst < innerLast) {
                int segs = std::max(4, (int)std::ceil((innerLast - innerFirst) / (M_PI / 36.0)));
                std::vector<Point2Df> pts;
                pts.reserve(2 * (segs + 1));
                for (int i = 0; i <= segs; ++i) {
                    double a = innerFirst + (innerLast - innerFirst) * (double)i / segs;
                    pts.push_back(project(a, innerR));
                }
                for (int i = segs; i >= 0; --i) {
                    double a = innerFirst + (innerLast - innerFirst) * (double)i / segs;
                    Point2Df top = project(a, innerR);
                    pts.emplace_back(top.x, top.y + depth);
                }
                double aMid = (innerFirst + innerLast) * 0.5;
                float nx = -(float)std::cos(aMid); // inner wall normal points inward
                float ny = -(float)std::sin(aMid);
                float dotL = nx * (float)lightDirection.x + ny * (float)lightDirection.y;
                Color c = ShadeColor(s.baseColor.Darken(0.15f), ambientLight, diffuseLight, -dotL);
                ctx->SetFillPaint(c);
                ctx->FillLinePath(pts);
            }
        }

        // ---- Radial side faces (start / end) ----
        auto drawRadialFace = [&](double a, bool isStartFace) {
            // Visible only if its normal faces the viewer (negative y in screen space).
            // Normal to the radial wall at angle a is perpendicular to the radial direction:
            //   nx = -sin(a) for start face (rotates -90deg from radial),
            //   ny =  cos(a)
            // For the end face, flip sign.
            float sign = isStartFace ? 1.0f : -1.0f;
            float nx = -sign * (float)std::sin(a);
            float ny =  sign * (float)std::cos(a);
            // Viewer's "down" in the screen after tilt is +y; a face faces viewer if ny > 0.
            if (ny <= 0.0f) return;

            Point2Df topOuter = project(a, outerR);
            Point2Df topInner = project(a, innerR);
            Point2Df botOuter(topOuter.x, topOuter.y + depth);
            Point2Df botInner(topInner.x, topInner.y + depth);

            std::vector<Point2Df> quad;
            if (donutMode && innerR > 0.0f) {
                quad = {topInner, topOuter, botOuter, botInner};
            } else {
                Point2Df topCenter(center.x + ex, center.y + ey);
                Point2Df botCenter(topCenter.x, topCenter.y + depth);
                quad = {topCenter, topOuter, botOuter, botCenter};
            }
            float dotL = nx * (float)lightDirection.x + ny * (float)lightDirection.y;
            Color c = ShadeColor(s.baseColor.Darken(0.10f), ambientLight, diffuseLight, -dotL);
            ctx->SetFillPaint(c);
            ctx->FillLinePath(quad);
            if (borderWidth > 0.0f && borderColor.a > 0) {
                ctx->SetStrokePaint(borderColor);
                ctx->SetStrokeWidth(std::max(0.5f, borderWidth * 0.5f));
                ctx->DrawLinePath(quad, true);
            }
        };
        drawRadialFace(s.startAngle, true);
        drawRadialFace(s.endAngle,  false);
    }

    void UltraCanvasPieChartElement::DrawSlice3DTop(IRenderContext* ctx,
                                                    const Point2Df& center,
                                                    float outerR,
                                                    float innerR,
                                                    double vScale,
                                                    const Slice& s)
    {
        double ex = std::cos(s.midAngle) * s.explosion * outerR;
        double ey = std::sin(s.midAngle) * s.explosion * outerR * vScale;

        ctx->PushState();
        ctx->Translate(center.x + ex, center.y + ey);
        ctx->Scale(1.0, vScale);

        // Resolve fill (gradient/pattern/base color shaded by top-face normal).
        // Top face normal in 3D is (0, 0, 1) — after our 2D projection the
        // diffuse term is simply how much it tilts toward / away from the viewer.
        // Apply a mild brighten so the top reads as the lit surface.
        Color topShade = ShadeColor(s.baseColor, ambientLight,
                                    diffuseLight, 0.5f);

        auto patternIt = slicePatterns.find(s.index);
        bool usedPattern = false;
        if (patternIt != slicePatterns.end() && patternIt->second) {
            ctx->SetFillPaint(patternIt->second);
            usedPattern = true;
        } else if (autoGradients || sliceGradients.count(s.index)) {
            auto grad = CreateSliceGradient(ctx, outerR, s);
            if (grad) { ctx->SetFillPaint(grad); usedPattern = true; }
        }
        if (!usedPattern) {
            ctx->SetFillPaint(topShade);
        }

        if (donutMode && innerR > 0.0f) {
            double sweep = s.endAngle - s.startAngle;
            int segments = std::max(4, (int)std::ceil(std::fabs(sweep) / (M_PI / 36.0)));
            std::vector<Point2Df> pts;
            pts.reserve(2 * (segments + 1));
            for (int i = 0; i <= segments; ++i) {
                double a = s.startAngle + sweep * (double)i / segments;
                pts.emplace_back(std::cos(a) * outerR, std::sin(a) * outerR);
            }
            for (int i = segments; i >= 0; --i) {
                double a = s.startAngle + sweep * (double)i / segments;
                pts.emplace_back(std::cos(a) * innerR, std::sin(a) * innerR);
            }
            ctx->FillLinePath(pts);
            if (borderWidth > 0.0f && borderColor.a > 0) {
                ctx->SetStrokePaint(borderColor);
                ctx->SetStrokeWidth(borderWidth);
                ctx->DrawLinePath(pts, true);
            }
        } else {
            ctx->ClearPath();
            ctx->MoveTo(0.0, 0.0);
            ctx->Arc(0.0, 0.0, outerR, s.startAngle, s.endAngle);
            ctx->LineTo(0.0, 0.0);
            ctx->ClosePath();
            ctx->FillPathPreserve();
            if (borderWidth > 0.0f && borderColor.a > 0) {
                ctx->SetStrokePaint(borderColor);
                ctx->SetStrokeWidth(borderWidth);
                ctx->StrokePathPreserve();
            }
            ctx->ClearPath();
        }

        ctx->PopState();
    }

    // ===== LABELS =====

    void UltraCanvasPieChartElement::RenderLabels(IRenderContext* ctx,
                                                  const Point2Df& center,
                                                  float outerR,
                                                  double vScale)
    {
        if (labelPosition == LabelPosition::None) return;
        if (cachedSlices.empty()) return;

        ctx->PushState();
        ctx->SetFontFamily(labelFontFamily);
        ctx->SetFontSize(labelFontSize);
        ctx->SetFontWeight(labelFontWeight);
        ctx->SetTextPaint(labelColor);

        struct LayoutItem {
            const Slice* slice;
            std::string text;
            LabelPosition resolved;
            Point2Df arcAttach;   // point on outer arc
            Point2Df anchor;      // base anchor before adjustment
            Point2Df textPos;     // top-left of text after adjustment
            Size2Di  textSize;
            bool     rightSide;
            float    fontScale;   // 1.0 = native size; <1.0 = shrunk to fit inside
        };

        std::vector<LayoutItem> items;
        items.reserve(cachedSlices.size());

        // Inside auto-scale tuning. Anything below kMinInsideScale just goes
        // outside instead — readability beats squeezing every label in.
        const float kAnchorRadiusFactor = 0.65f;
        const float kChordSafety        = 0.92f;
        const float kRadialFraction     = 0.55f;
        const float kMinInsideScale     = 0.72f;

        // Pass 1a — pick provisional position per slice, build the right text for it,
        // measure at native size and remember each slice's required shrink factor.
        for (const Slice& s : cachedSlices) {
            LayoutItem it;
            it.slice = &s;
            it.fontScale = 1.0f;

            LabelPosition resolved = labelPosition;
            double sweep = s.endAngle - s.startAngle;
            if (resolved == LabelPosition::Auto) {
                resolved = (sweep > (25.0 * M_PI / 180.0))
                           ? LabelPosition::Inside
                           : LabelPosition::Outside;
            }

            // Inside placement uses the compact multi-line variant so the slice
            // chord only has to fit one word/number per line.
            it.text = (resolved == LabelPosition::Inside)
                      ? BuildInsideLabelText(s)
                      : BuildLabelText(s);
            if (it.text.empty()) continue;

            ctx->SetFontSize(labelFontSize);
            it.textSize = ctx->GetTextLineDimensions(it.text);

            if (resolved == LabelPosition::Inside) {
                const float labelR = outerR * kAnchorRadiusFactor;
                const float chord  = 2.0f * labelR * static_cast<float>(std::sin(sweep * 0.5));
                const float innerLim = donutMode ? outerR * innerRadiusFraction : 0.0f;
                const float availW = chord * kChordSafety;
                const float availH = (outerR - innerLim) * kRadialFraction;

                if (availW > 4.0f && availH > 4.0f &&
                    it.textSize.width > 0 && it.textSize.height > 0) {
                    float scaleW = availW / static_cast<float>(it.textSize.width);
                    float scaleH = availH / static_cast<float>(it.textSize.height);
                    float scale  = std::min({1.0f, scaleW, scaleH});

                    if (scale >= kMinInsideScale) {
                        it.fontScale = scale;
                    } else {
                        // Slice is too narrow even with shrunk text. Fall back to
                        // the outside leader-line variant for this slice.
                        resolved = LabelPosition::Outside;
                        it.text = BuildLabelText(s);
                        ctx->SetFontSize(labelFontSize);
                        it.textSize = ctx->GetTextLineDimensions(it.text);
                    }
                } else {
                    resolved = LabelPosition::Outside;
                    it.text = BuildLabelText(s);
                    ctx->SetFontSize(labelFontSize);
                    it.textSize = ctx->GetTextLineDimensions(it.text);
                }
            }

            it.resolved = resolved;
            items.push_back(it);
        }

        // Pass 1b — pick a single, uniform inside font scale so every label
        // that sits inside the pie renders at the same typographic size.
        float uniformInsideScale = 1.0f;
        for (const auto& it : items) {
            if (it.resolved == LabelPosition::Inside && it.fontScale < uniformInsideScale) {
                uniformInsideScale = it.fontScale;
            }
        }
        if (uniformInsideScale < 1.0f) {
            for (auto& it : items) {
                if (it.resolved != LabelPosition::Inside) continue;
                it.fontScale = uniformInsideScale;
                it.textSize.width  = static_cast<int>(it.textSize.width  * uniformInsideScale + 0.5f);
                it.textSize.height = static_cast<int>(it.textSize.height * uniformInsideScale + 0.5f);
            }
        }

        // Pass 1c — position labels now that text sizes are final.
        for (auto& it : items) {
            const Slice& s = *it.slice;
            LabelPosition resolved = it.resolved;

            double ex = std::cos(s.midAngle) * s.explosion * outerR;
            double ey = std::sin(s.midAngle) * s.explosion * outerR * vScale;
            it.arcAttach = Point2Df(center.x + ex + outerR * std::cos(s.midAngle),
                                    center.y + ey + outerR * std::sin(s.midAngle) * vScale);

            float radiusFactor = 0.65f;
            if (resolved == LabelPosition::Inside)       radiusFactor = 0.65f;
            else if (resolved == LabelPosition::Edge)    radiusFactor = 1.0f;
            else if (resolved == LabelPosition::Outside) radiusFactor = 1.18f;

            it.anchor = Point2Df(center.x + ex + outerR * radiusFactor * std::cos(s.midAngle),
                                 center.y + ey + outerR * radiusFactor * std::sin(s.midAngle) * vScale);
            it.rightSide = std::cos(s.midAngle) >= 0.0;

            if (resolved == LabelPosition::Inside) {
                it.textPos = Point2Df(it.anchor.x - it.textSize.width / 2.0,
                                      it.anchor.y - it.textSize.height / 2.0);
            } else if (resolved == LabelPosition::Edge) {
                if (it.rightSide) {
                    it.textPos = Point2Df(it.anchor.x + 2.0,
                                          it.anchor.y - it.textSize.height / 2.0);
                } else {
                    it.textPos = Point2Df(it.anchor.x - 2.0 - it.textSize.width,
                                          it.anchor.y - it.textSize.height / 2.0);
                }
            } else { // Outside
                double elbowX = it.rightSide
                                ? it.anchor.x + 6.0
                                : it.anchor.x - 6.0 - it.textSize.width;
                it.textPos = Point2Df(elbowX, it.anchor.y - it.textSize.height / 2.0);

                // Keep the label fully inside the chart element. The leader line
                // in pass 3 will follow whatever x we end up with.
                const float minX = 2.0f;
                const float maxX = static_cast<float>(GetWidth()) - it.textSize.width - 2.0f;
                if (it.textPos.x < minX) it.textPos.x = minX;
                if (it.textPos.x > maxX) it.textPos.x = maxX;
            }

            // Vertical clamp for every label position so they never escape the element.
            const float minY = 2.0f;
            const float maxY = static_cast<float>(GetHeight()) - it.textSize.height - 2.0f;
            if (it.textPos.y < minY) it.textPos.y = minY;
            if (it.textPos.y > maxY) it.textPos.y = maxY;
        }

        // Pass 2 — anti-overlap for outside labels (per hemisphere, sorted by Y)
        auto resolveOverlaps = [&](bool rightHemisphere) {
            std::vector<size_t> indices;
            for (size_t i = 0; i < items.size(); ++i) {
                if (items[i].resolved == LabelPosition::Outside &&
                    items[i].rightSide == rightHemisphere) {
                    indices.push_back(i);
                }
            }
            std::sort(indices.begin(), indices.end(),
                      [&](size_t a, size_t b) { return items[a].textPos.y < items[b].textPos.y; });

            const double gap = 2.0;
            // Top-down pass
            for (size_t k = 1; k < indices.size(); ++k) {
                auto& prev = items[indices[k - 1]];
                auto& cur  = items[indices[k]];
                double prevBottom = prev.textPos.y + prev.textSize.height;
                if (cur.textPos.y < prevBottom + gap) {
                    cur.textPos.y = prevBottom + gap;
                }
            }
            // Bottom-up corrective pass
            for (size_t k = indices.size(); k-- > 1;) {
                auto& cur  = items[indices[k - 1]];
                auto& next = items[indices[k]];
                double curBottom = cur.textPos.y + cur.textSize.height;
                if (curBottom + gap > next.textPos.y) {
                    cur.textPos.y = next.textPos.y - gap - cur.textSize.height;
                }
            }
        };
        resolveOverlaps(true);
        resolveOverlaps(false);

        // Pass 3 — draw
        for (const LayoutItem& it : items) {
            if (it.resolved == LabelPosition::Outside && leaderLinesEnabled) {
                ctx->SetStrokePaint(leaderLineColor);
                ctx->SetStrokeWidth(leaderLineWidth);
                Point2Df elbow;
                if (it.rightSide) {
                    elbow = Point2Df(it.textPos.x - 4.0, it.textPos.y + it.textSize.height / 2.0);
                } else {
                    elbow = Point2Df(it.textPos.x + it.textSize.width + 4.0,
                                     it.textPos.y + it.textSize.height / 2.0);
                }
                ctx->DrawLine(it.arcAttach, elbow);
                Point2Df labelEdge(it.rightSide ? it.textPos.x : it.textPos.x + it.textSize.width,
                                   it.textPos.y + it.textSize.height / 2.0);
                ctx->DrawLine(elbow, labelEdge);
            }

            if (labelBackgroundEnabled) {
                ctx->SetFillPaint(labelBackgroundColor);
                ctx->FillRoundedRectangle(
                    Rect2Df(it.textPos.x - 3.0, it.textPos.y - 1.0,
                            it.textSize.width + 6.0, it.textSize.height + 2.0),
                    3.0);
            }

            ctx->SetFontSize(labelFontSize * it.fontScale);
            ctx->SetTextPaint(labelColor);
            ctx->DrawText(it.text, it.textPos);
        }

        ctx->PopState();
    }

    // ===== HIT TESTING =====

    size_t UltraCanvasPieChartElement::HitTestSlice(const Point2Df& localPos) const {
        if (cachedSlices.empty()) return SIZE_MAX;

        float outerR = cachedOuterRadius;
        float innerR = donutMode ? outerR * innerRadiusFraction : 0.0f;
        double vScale = enable3D ? std::cos(perspectiveAngleDeg * M_PI / 180.0) : 1.0;
        if (vScale < 1e-3) vScale = 1e-3;

        // Walk slices front-to-back: any with non-zero explosion can extend outward,
        // so each slice gets its own local frame.
        for (auto it = cachedSlices.rbegin(); it != cachedSlices.rend(); ++it) {
            const Slice& s = *it;
            double ex = std::cos(s.midAngle) * s.explosion * outerR;
            double ey = std::sin(s.midAngle) * s.explosion * outerR * vScale;
            double dx = localPos.x - (cachedCenter.x + ex);
            double dy = (localPos.y - (cachedCenter.y + ey)) / vScale;
            double r = std::sqrt(dx * dx + dy * dy);
            if (r > outerR) continue;
            if (donutMode && r < innerR) continue;

            double angle = std::atan2(dy, dx);
            // Normalize angle into the slice's angle range.
            double a = angle;
            // Shift `a` to be within [startAngle, startAngle + 2*PI)
            while (a < s.startAngle) a += 2.0 * M_PI;
            while (a >= s.startAngle + 2.0 * M_PI) a -= 2.0 * M_PI;
            if (a >= s.startAngle && a <= s.endAngle) {
                return s.index;
            }
        }
        return SIZE_MAX;
    }

    bool UltraCanvasPieChartElement::HandleChartMouseMove(const Point2Di& mousePos) {
        if (cachedSlices.empty()) return false;

        Point2Df local((double)mousePos.x, (double)mousePos.y);
        size_t hit = HitTestSlice(local);

        if (hit != hoveredSliceIndex) {
            hoveredSliceIndex = hit;
            RequestRedraw();
        }

        if (hit != SIZE_MAX) {
            if (enableTooltips && dataSource) {
                ChartDataPoint p = dataSource->GetPoint(hit);
                ShowChartPointTooltip(mousePos, p, hit);
            }
            return true;
        } else if (isTooltipActive) {
            HideTooltip();
        }
        return false;
    }

    // ===== EXPORT =====

    namespace {
        std::string ToLowerExt(const std::string& filename) {
            auto dotPos = filename.find_last_of('.');
            if (dotPos == std::string::npos) return "";
            std::string ext = filename.substr(dotPos);
            for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
            return ext;
        }

        bool ExtensionToFormat(const std::string& ext, UCImageSaveFormat& outFmt) {
            if (ext == ".png")  { outFmt = UCImageSaveFormat::PNG;  return true; }
            if (ext == ".jpg" || ext == ".jpeg") { outFmt = UCImageSaveFormat::JPEG; return true; }
            if (ext == ".jp2") { outFmt = UCImageSaveFormat::JPEG2000; return true; }
            if (ext == ".jxl") { outFmt = UCImageSaveFormat::JXL;   return true; }
            if (ext == ".bmp") { outFmt = UCImageSaveFormat::BMP;   return true; }
            if (ext == ".gif") { outFmt = UCImageSaveFormat::GIF;   return true; }
            if (ext == ".tif" || ext == ".tiff") { outFmt = UCImageSaveFormat::TIFF; return true; }
            if (ext == ".webp"){ outFmt = UCImageSaveFormat::WEBP;  return true; }
            if (ext == ".heif" || ext == ".heic") { outFmt = UCImageSaveFormat::HEIF; return true; }
            if (ext == ".avif"){ outFmt = UCImageSaveFormat::AVIF;  return true; }
            if (ext == ".qoi") { outFmt = UCImageSaveFormat::QOI;   return true; }
            if (ext == ".tga") { outFmt = UCImageSaveFormat::TGA;   return true; }
            if (ext == ".ppm" || ext == ".pgm" || ext == ".pbm") {
                outFmt = UCImageSaveFormat::PPM;
                return true;
            }
            return false;
        }
    }

    bool UltraCanvasPieChartElement::QuickExport(const std::string& filename) {
        return ExportToFile(filename, GetWidth(), GetHeight());
    }

    bool UltraCanvasPieChartElement::ExportToFile(const std::string& filename, int w, int h) {
        if (w <= 0 || h <= 0) return false;

        std::string ext = ToLowerExt(filename);
        UCImageSaveFormat fmt = UCImageSaveFormat::PNG;
        if (!ext.empty()) ExtensionToFormat(ext, fmt);

        // Create offscreen render context
        auto exportCtx = std::make_unique<RenderContextCairo>();
        if (!exportCtx->CreateSurface(Size2Di(w, h), nullptr)) {
            std::cerr << "UltraCanvasPieChartElement::ExportToFile: failed to create surface" << std::endl;
            return false;
        }
        // Fill background
        exportCtx->Clear(backgroundColor.a > 0 ? backgroundColor : Color(255, 255, 255, 255));

        // Render into the offscreen ctx using export-area geometry.
        Rect2Df chartArea(0.0, 0.0, (double)w, (double)h);

        // Ensure slices reflect current dataSource state.
        InvalidateCache();
        if (!slicesValid) RebuildSlices();

        // Apply margins similar to CalculatePlotArea.
        int marginSide   = 10;
        int marginTop    = chartTitle.empty() ? 10 : 32;
        int marginBottom = 10;
        bool needsLabelRoom =
            (labelPosition == LabelPosition::Outside || labelPosition == LabelPosition::Auto);
        if (needsLabelRoom) {
            marginSide   = 70;
            marginTop    = chartTitle.empty() ? 30 : 50;
            marginBottom = 30;
        }
        chartArea.x = marginSide;
        chartArea.y = marginTop;
        chartArea.width  = std::max(20.0, (double)w - 2 * marginSide);
        chartArea.height = std::max(20.0, (double)h - marginTop - marginBottom);

        RenderChartImpl(exportCtx.get(), chartArea);

        // Mark base-class cache as stale so the next on-screen render rebuilds
        // against the live bounds rather than the export dimensions.
        InvalidateCache();
        RequestRedraw();

        cairo_surface_t* surf = exportCtx->GetCairoSurface();
        if (!surf) return false;
        cairo_surface_flush(surf);

        if (fmt == UCImageSaveFormat::PNG || ext.empty()) {
            cairo_status_t st = cairo_surface_write_to_png(surf, filename.c_str());
            return st == CAIRO_STATUS_SUCCESS;
        }

#ifdef HAS_LIBVIPS
        // Round-trip through a temporary PNG so we reuse the framework's tested
        // VIPS pipeline for JPEG/WEBP/TIFF/QOI/JXL/etc. — at the cost of one extra
        // disk write + re-encode. Acceptable for an explicit "export" action.
        namespace fs = std::filesystem;
        fs::path tmpDir = fs::temp_directory_path();
        fs::path tmpFile = tmpDir / ("ultracanvas_piechart_export.png");
        cairo_status_t st = cairo_surface_write_to_png(surf, tmpFile.string().c_str());
        if (st != CAIRO_STATUS_SUCCESS) return false;

        auto raster = UCImageRaster::Load(tmpFile.string(), false);
        if (!raster || !raster->IsValid()) {
            fs::remove(tmpFile);
            return false;
        }
        UCImageSave::ImageExportOptions opts;
        opts.format = fmt;
        std::string err = raster->Save(filename, opts);
        fs::remove(tmpFile);
        return err.empty();
#else
        // Without libvips, fall back to PNG regardless of extension.
        cairo_status_t st = cairo_surface_write_to_png(surf, filename.c_str());
        return st == CAIRO_STATUS_SUCCESS;
#endif
    }

    // ===== FACTORY =====

    std::shared_ptr<UltraCanvasPieChartElement> CreatePieChartElement(
        const std::string& id, int x, int y, int w, int h)
    {
        return std::make_shared<UltraCanvasPieChartElement>(id, x, y, w, h);
    }

} // namespace UltraCanvas
