// Plugins/Charts/UltraCanvasChordChart.cpp
// Chord diagram element: circular category arcs joined by proportional ribbons.
// Version: 1.0.1
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework
//
// Changelog:
//   v1.0.1 (2026-07-30):
//     - Rebuild the cached layout when the layout engine resizes the element.
//       The plot area was only computed once, so a chart sized by flex/grid kept
//       its original ring after a window resize, and the ribbon outlines were
//       never rebuilt in the new geometry.
//     - Reserve the full label room above and below the ring for radial labels.
//       They run straight up and down there, so half the room clipped them.
#include "Plugins/Charts/UltraCanvasChordChart.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace UltraCanvas {

// =============================================================================
// CHORD MATRIX
// =============================================================================

    void ChordMatrix::AddCategory(const ChordCategory& category) {
        categories.push_back(category);
        ResizeMatrix();
    }

    void ChordMatrix::ResizeMatrix() {
        size_t n = categories.size();
        flows.resize(n);
        for (auto& row : flows) row.resize(n, 0.0);
    }

    bool ChordMatrix::SetFlow(size_t from, size_t to, double value) {
        if (from >= categories.size() || to >= categories.size()) return false;
        ResizeMatrix();
        flows[from][to] = std::max(0.0, value);
        RecalculateTotals();
        return true;
    }

    double ChordMatrix::GetFlow(size_t from, size_t to) const {
        if (from >= flows.size() || to >= flows[from].size()) return 0.0;
        return flows[from][to];
    }

    void ChordMatrix::Clear() {
        flows.clear();
        categories.clear();
    }

    void ChordMatrix::RecalculateTotals() {
        size_t n = categories.size();
        for (size_t i = 0; i < n; ++i) {
            double outgoing = 0.0, incoming = 0.0;
            for (size_t j = 0; j < n; ++j) {
                if (i < flows.size() && j < flows[i].size()) outgoing += flows[i][j];
                if (j < flows.size() && i < flows[j].size()) incoming += flows[j][i];
            }
            // Throughput. A self-flow legitimately contributes twice because it
            // occupies both an outgoing and an incoming sub-arc on the ring.
            categories[i].totalValue = outgoing + incoming;
        }
    }

    double ChordMatrix::GrandTotal() const {
        double sum = 0.0;
        for (const auto& c : categories) sum += c.totalValue;
        return sum;
    }

// =============================================================================
// CONSTRUCTION
// =============================================================================

    std::vector<Color> UltraCanvasChordChart::DefaultPalette() {
        return {
            Color( 66, 133, 244, 255), Color(219,  68,  55, 255),
            Color(244, 180,   0, 255), Color( 15, 157,  88, 255),
            Color(171,  71, 188, 255), Color(  0, 172, 193, 255),
            Color(255, 112,  67, 255), Color(158, 157,  36, 255),
            Color( 92, 107, 192, 255), Color(240,  98, 146, 255)
        };
    }

    UltraCanvasChordChart::UltraCanvasChordChart(
        const std::string& id, int x, int y, int w, int h)
        : UltraCanvasChartElementBase(id, x, y, w, h)
    {
        showGrid = false;
        showAxes = false;
        showBackground = false;
    }

// =============================================================================
// DATA MANAGEMENT
// =============================================================================

    void UltraCanvasChordChart::SetChordMatrix(const ChordMatrix& matrix) {
        chordMatrix = matrix;
        chordMatrix.ResizeMatrix();
        chordMatrix.RecalculateTotals();
        hoveredCategory = SIZE_MAX;
        hoveredRibbon = SIZE_MAX;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasChordChart::AddCategory(const std::string& name, const Color& color) {
        Color resolved = color;
        if (resolved.a == 0) {
            auto palette = DefaultPalette();
            resolved = palette[chordMatrix.categories.size() % palette.size()];
        }
        chordMatrix.AddCategory(ChordCategory(name, resolved));
        chordMatrix.RecalculateTotals();
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasChordChart::SetFlow(size_t from, size_t to, double value) {
        if (!chordMatrix.SetFlow(from, to, value)) return;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasChordChart::ClearData() {
        chordMatrix.Clear();
        flows.clear();
        ribbonOutlines.clear();
        hoveredCategory = SIZE_MAX;
        hoveredRibbon = SIZE_MAX;
        InvalidateLayout();
        RequestRedraw();
    }

    bool UltraCanvasChordChart::ImportFromMatrix(
        const std::vector<std::vector<double>>& matrix,
        const std::vector<std::string>& categoryNames,
        const std::vector<Color>& colors)
    {
        if (matrix.size() != categoryNames.size()) return false;
        for (const auto& row : matrix) {
            if (row.size() != categoryNames.size()) return false;
        }

        auto palette = DefaultPalette();
        chordMatrix.Clear();
        for (size_t i = 0; i < categoryNames.size(); ++i) {
            Color c = (i < colors.size() && colors[i].a > 0)
                    ? colors[i] : palette[i % palette.size()];
            chordMatrix.categories.emplace_back(categoryNames[i], c);
        }
        chordMatrix.flows = matrix;
        for (auto& row : chordMatrix.flows) {
            for (auto& v : row) v = std::max(0.0, v);
        }
        chordMatrix.ResizeMatrix();
        chordMatrix.RecalculateTotals();

        hoveredCategory = SIZE_MAX;
        hoveredRibbon = SIZE_MAX;
        InvalidateLayout();
        RequestRedraw();
        return true;
    }

// =============================================================================
// CONFIGURATION
// =============================================================================

    void UltraCanvasChordChart::SetRadius(float r) {
        if (r <= 0.0f) {
            autoFitRadius = true;
            explicitRadius = 0.0f;
        } else {
            autoFitRadius = false;
            explicitRadius = r;
        }
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasChordChart::SetAutoFitRadius(bool on) {
        autoFitRadius = on;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasChordChart::SetRingThickness(float pixels) {
        ringThickness = std::max(2.0f, pixels);
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasChordChart::SetStartAngle(float degrees) {
        startAngleDeg = degrees;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasChordChart::SetCategoryPadding(float degrees) {
        categoryPaddingDeg = std::max(0.0f, std::min(30.0f, degrees));
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasChordChart::SetRibbonOpacity(float opacity) {
        ribbonOpacity = std::max(0.0f, std::min(1.0f, opacity));
        RequestRedraw();
    }

    void UltraCanvasChordChart::SetRibbonCurvature(float curvature) {
        ribbonCurvature = std::max(0.0f, std::min(1.0f, curvature));
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasChordChart::SetRibbonColorMode(ChordRibbonColorMode mode) {
        ribbonColorMode = mode;
        RequestRedraw();
    }

    void UltraCanvasChordChart::SetRibbonBorder(const Color& color, float width) {
        ribbonBorderColor = color;
        ribbonBorderWidth = std::max(0.0f, width);
        RequestRedraw();
    }

    void UltraCanvasChordChart::SetMinFlowValue(double minValue) {
        minFlowValue = std::max(0.0, minValue);
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasChordChart::SetLabelPlacement(ChordLabelPlacement placement) {
        labelPlacement = placement;
        InvalidateCache();
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasChordChart::SetShowLabels(bool show) {
        SetLabelPlacement(show ? ChordLabelPlacement::Outside
                               : ChordLabelPlacement::None);
    }

    void UltraCanvasChordChart::SetLabelDistance(float pixels) {
        labelDistance = std::max(0.0f, pixels);
        InvalidateCache();
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasChordChart::SetLabelFont(const std::string& family, float size,
                                             FontWeight weight) {
        labelFontFamily = family;
        labelFontSize = std::max(4.0f, size);
        labelFontWeight = weight;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasChordChart::SetLabelColor(const Color& c) {
        labelColor = c;
        RequestRedraw();
    }

    void UltraCanvasChordChart::SetValueFormatter(std::function<std::string(double)> f) {
        valueFormatter = std::move(f);
        RequestRedraw();
    }

    void UltraCanvasChordChart::SetHoverHighlightEnabled(bool on) {
        hoverHighlightEnabled = on;
        if (!on && AnythingHovered()) {
            hoveredCategory = SIZE_MAX;
            hoveredRibbon = SIZE_MAX;
            RequestRedraw();
        }
    }

    void UltraCanvasChordChart::SetDimmedOpacity(float factor) {
        dimmedOpacity = std::max(0.0f, std::min(1.0f, factor));
        RequestRedraw();
    }

// =============================================================================
// LAYOUT
// =============================================================================

    void UltraCanvasChordChart::InvalidateLayout() {
        layoutValid = false;
    }

    void UltraCanvasChordChart::SyncLayoutToElementSize() {
        // The layout engine can resize the element without any setter running —
        // it happens on every window resize when the chart is a flex/grid item.
        // Both the plot area and everything derived from it are cached, so drop
        // both caches when the size we last built for is stale.
        if (lastLayoutWidth == GetWidth() && lastLayoutHeight == GetHeight()) return;
        lastLayoutWidth  = GetWidth();
        lastLayoutHeight = GetHeight();
        InvalidateCache();
        InvalidateLayout();
    }

    ChartPlotArea UltraCanvasChordChart::CalculatePlotArea() {
        // Reserve room for outboard labels so neither the ring nor the longest
        // label is clipped. Text cannot be measured here (no render context),
        // so the widest name is estimated from its character count.
        float labelRoom = 0.0f;
        if (labelPlacement != ChordLabelPlacement::None) {
            size_t longest = 0;
            for (const auto& c : chordMatrix.categories) {
                longest = std::max(longest, c.name.size());
            }
            labelRoom = labelDistance + static_cast<float>(longest) * labelFontSize * 0.62f + 6.0f;
        }
        // A radial label at the top or bottom of the ring runs straight up or
        // down, so it needs as much vertical room as a side label needs
        // horizontal room. An outside label is horizontal wherever it sits, so
        // there it is only the line height that has to fit.
        float verticalLabelRoom = (labelPlacement == ChordLabelPlacement::Radial)
                                ? labelRoom : labelRoom * 0.5f;
        float marginSide   = 8.0f + labelRoom;
        float marginTop    = (chartTitle.empty() ? 8.0f : 32.0f) + verticalLabelRoom;
        float marginBottom = 8.0f + verticalLabelRoom;

        ChartPlotArea area;
        area.x = marginSide;
        area.y = marginTop;
        area.width  = std::max(40.0, static_cast<double>(GetWidth())  - 2.0 * marginSide);
        area.height = std::max(40.0, static_cast<double>(GetHeight()) - marginTop - marginBottom);
        return area;
    }

    void UltraCanvasChordChart::RebuildFlows() {
        flows.clear();
        size_t n = chordMatrix.categories.size();
        if (n == 0) return;

        for (size_t i = 0; i < n; ++i) {
            for (size_t j = 0; j < n; ++j) {
                double v = chordMatrix.GetFlow(i, j);
                if (v <= minFlowValue || v <= 0.0) continue;
                flows.emplace_back(i, j, v);
            }
        }
    }

    void UltraCanvasChordChart::ComputeGeometry() {
        cachedCenter = Point2Dd(cachedPlotArea.x + cachedPlotArea.width / 2.0,
                                cachedPlotArea.y + cachedPlotArea.height / 2.0);

        float fitted = static_cast<float>(
            std::min(cachedPlotArea.width, cachedPlotArea.height) * 0.5 - 2.0);
        float outer = autoFitRadius ? fitted : explicitRadius;
        // Even an explicit radius is clamped so the ring cannot spill outside
        // the element and get clipped away.
        cachedOuterRadius = std::max(12.0f, std::min(outer, std::max(12.0f, fitted)));
        cachedInnerRadius = std::max(6.0f, cachedOuterRadius -
                                          std::min(ringThickness, cachedOuterRadius * 0.5f));
    }

    void UltraCanvasChordChart::AssignArcs() {
        size_t n = chordMatrix.categories.size();
        if (n == 0) return;

        // Totals are derived from the *drawn* flows so the arcs account for
        // exactly what is on screen once minFlowValue has filtered the set.
        std::vector<double> totals(n, 0.0);
        for (const auto& f : flows) {
            totals[f.fromIndex] += f.value;
            totals[f.toIndex]   += f.value;
        }
        for (size_t i = 0; i < n; ++i) {
            chordMatrix.categories[i].totalValue = totals[i];
        }

        cachedGrandTotal = std::accumulate(totals.begin(), totals.end(), 0.0);
        if (cachedGrandTotal <= 0.0) {
            for (auto& c : chordMatrix.categories) { c.startAngle = c.endAngle = 0.0; }
            return;
        }

        // Categories with no drawn flow get no arc and no padding slot.
        size_t activeCount = 0;
        for (size_t i = 0; i < n; ++i) if (totals[i] > 0.0) ++activeCount;

        double padding = categoryPaddingDeg * M_PI / 180.0;
        double available = 2.0 * M_PI - padding * static_cast<double>(activeCount);
        if (available < 0.1) {
            // Padding would consume the whole circle; fall back to no padding.
            padding = 0.0;
            available = 2.0 * M_PI;
        }

        double cursor = startAngleDeg * M_PI / 180.0;
        for (size_t i = 0; i < n; ++i) {
            auto& cat = chordMatrix.categories[i];
            if (totals[i] <= 0.0) {
                cat.startAngle = cat.endAngle = cursor;
                continue;
            }
            double span = available * (totals[i] / cachedGrandTotal);
            cat.startAngle = cursor;
            cat.endAngle   = cursor + span;
            cursor = cat.endAngle + padding;
        }

        // Dense (from, to) -> index into `flows`, for sub-arc allocation.
        std::vector<size_t> flowIndex(n * n, SIZE_MAX);
        for (size_t k = 0; k < flows.size(); ++k) {
            flowIndex[flows[k].fromIndex * n + flows[k].toIndex] = k;
        }

        // Within each category, lay out sub-arcs partner by partner, placing
        // the outgoing slice next to the matching incoming slice so both
        // directions of a relationship sit together on the ring.
        for (size_t i = 0; i < n; ++i) {
            if (totals[i] <= 0.0) continue;
            const auto& cat = chordMatrix.categories[i];
            double span = cat.endAngle - cat.startAngle;
            double sub = cat.startAngle;

            for (size_t j = 0; j < n; ++j) {
                size_t outIdx = flowIndex[i * n + j];
                if (outIdx != SIZE_MAX) {
                    double w = span * (flows[outIdx].value / totals[i]);
                    flows[outIdx].sourceStartAngle = sub;
                    flows[outIdx].sourceEndAngle   = sub + w;
                    sub += w;
                }
                size_t inIdx = flowIndex[j * n + i];
                if (inIdx != SIZE_MAX) {
                    double w = span * (flows[inIdx].value / totals[i]);
                    flows[inIdx].targetStartAngle = sub;
                    flows[inIdx].targetEndAngle   = sub + w;
                    sub += w;
                }
            }
        }

        for (auto& f : flows) {
            f.flowColor = ResolveRibbonColor(f);
        }
    }

    void UltraCanvasChordChart::BuildRibbonOutlines() {
        ribbonOutlines.assign(flows.size(), {});
        for (size_t i = 0; i < flows.size(); ++i) {
            const ChordFlow& f = flows[i];
            ConnectionRenderer::BuildArcRibbonOutline(
                cachedCenter, cachedInnerRadius,
                f.sourceStartAngle, f.sourceEndAngle,
                f.targetStartAngle, f.targetEndAngle,
                ribbonCurvature, ribbonOutlines[i]);
        }
    }

// =============================================================================
// HELPERS
// =============================================================================

    Color UltraCanvasChordChart::ResolveRibbonColor(const ChordFlow& flow) const {
        const auto& cats = chordMatrix.categories;
        if (flow.fromIndex >= cats.size() || flow.toIndex >= cats.size()) {
            return Color(128, 128, 128, 255);
        }
        const Color& src = cats[flow.fromIndex].color;
        const Color& tgt = cats[flow.toIndex].color;

        switch (ribbonColorMode) {
            case ChordRibbonColorMode::Source: return src;
            case ChordRibbonColorMode::Target: return tgt;
            case ChordRibbonColorMode::Blend:  return src.Blend(tgt, 0.5f);
            case ChordRibbonColorMode::Larger:
            default:
                return cats[flow.fromIndex].totalValue >= cats[flow.toIndex].totalValue
                     ? src : tgt;
        }
    }

    bool UltraCanvasChordChart::IsRibbonActive(size_t ribbonIndex) const {
        if (!hoverHighlightEnabled || !AnythingHovered()) return true;
        if (hoveredRibbon != SIZE_MAX) return ribbonIndex == hoveredRibbon;
        if (ribbonIndex >= flows.size()) return false;
        const ChordFlow& f = flows[ribbonIndex];
        return f.fromIndex == hoveredCategory || f.toIndex == hoveredCategory;
    }

    bool UltraCanvasChordChart::IsCategoryActive(size_t categoryIndex) const {
        if (!hoverHighlightEnabled || !AnythingHovered()) return true;
        if (hoveredCategory != SIZE_MAX) return categoryIndex == hoveredCategory;
        if (hoveredRibbon >= flows.size()) return true;
        const ChordFlow& f = flows[hoveredRibbon];
        return categoryIndex == f.fromIndex || categoryIndex == f.toIndex;
    }

    std::string UltraCanvasChordChart::FormatValue(double v) const {
        if (valueFormatter) return valueFormatter(v);
        char buf[48];
        if (std::floor(v) == v && std::fabs(v) < 1e12) {
            std::snprintf(buf, sizeof(buf), "%.0f", v);
        } else {
            std::snprintf(buf, sizeof(buf), "%g", v);
        }
        return std::string(buf);
    }

    std::string UltraCanvasChordChart::BuildCategoryTooltip(size_t index) const {
        if (index >= chordMatrix.categories.size()) return "";
        const ChordCategory& cat = chordMatrix.categories[index];
        if (customCategoryTooltip) return customCategoryTooltip(cat);

        double outgoing = 0.0, incoming = 0.0;
        for (const auto& f : flows) {
            if (f.fromIndex == index) outgoing += f.value;
            if (f.toIndex   == index) incoming += f.value;
        }

        std::string text = cat.name;
        text += "\nTotal: "    + FormatValue(cat.totalValue);
        text += "\nOutgoing: " + FormatValue(outgoing);
        text += "\nIncoming: " + FormatValue(incoming);
        if (cachedGrandTotal > 0.0) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1f%%",
                          cat.totalValue / cachedGrandTotal * 100.0);
            text += "\nShare: " + std::string(buf);
        }
        return text;
    }

    std::string UltraCanvasChordChart::BuildFlowTooltip(const ChordFlow& flow) const {
        if (customFlowTooltip) return customFlowTooltip(flow);
        const auto& cats = chordMatrix.categories;
        if (flow.fromIndex >= cats.size() || flow.toIndex >= cats.size()) return "";

        std::string text = cats[flow.fromIndex].name + " \xE2\x86\x92 " +
                           cats[flow.toIndex].name;
        text += "\nFlow: " + FormatValue(flow.value);

        double reverse = chordMatrix.GetFlow(flow.toIndex, flow.fromIndex);
        if (reverse > 0.0) {
            text += "\nReturn: " + FormatValue(reverse);
            text += "\nNet: " + FormatValue(flow.value - reverse);
        }
        return text;
    }

// =============================================================================
// RENDERING
// =============================================================================

    void UltraCanvasChordChart::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
        if (!ctx) return;

        SyncLayoutToElementSize();
        UpdateRenderingCache();
        // RenderCommonBackground() is deliberately not called: grid, axes and
        // the plot-area rectangle are meaningless for a radial chart, and its
        // title pass would draw a second copy over RenderTitle()'s.

        if (chordMatrix.categories.empty()) {
            DrawEmptyState(ctx);
            return;
        }
        RenderChart(ctx);
    }

    void UltraCanvasChordChart::RenderChart(IRenderContext* ctx) {
        if (!ctx) return;
        SyncLayoutToElementSize();
        UpdateRenderingCache();

        if (!layoutValid) {
            RebuildFlows();
            ComputeGeometry();
            AssignArcs();
            BuildRibbonOutlines();
            layoutValid = true;
        } else {
            // The plot area can also change without the size changing (a label
            // font or placement only invalidates the cache), and the ribbon
            // outlines are built in the resulting geometry, so they have to
            // follow it whenever it actually moves.
            Point2Dd previousCenter = cachedCenter;
            float previousInnerRadius = cachedInnerRadius;
            ComputeGeometry();
            if (cachedCenter != previousCenter || cachedInnerRadius != previousInnerRadius) {
                BuildRibbonOutlines();
            }
        }

        if (flows.empty() && chordMatrix.categories.empty()) {
            DrawEmptyState(ctx);
            return;
        }

        RenderTitle(ctx);
        DrawRibbons(ctx);
        DrawCategories(ctx);
        if (labelPlacement != ChordLabelPlacement::None) {
            DrawLabels(ctx);
        }
    }

    void UltraCanvasChordChart::RenderTitle(IRenderContext* ctx) {
        if (chartTitle.empty()) return;
        ctx->PushState();
        ctx->SetFontFamily(labelFontFamily);
        ctx->SetFontSize(labelFontSize + 4.0f);
        ctx->SetFontWeight(FontWeight::Bold);
        ctx->SetTextPaint(labelColor);
        Size2Di ts = ctx->GetTextLineDimensions(chartTitle);
        ctx->DrawText(chartTitle,
                      Point2Dd(cachedPlotArea.x + (cachedPlotArea.width - ts.width) / 2.0,
                               std::max(2.0, cachedPlotArea.y - ts.height - 6.0)));
        ctx->PopState();
    }

    void UltraCanvasChordChart::DrawRibbons(IRenderContext* ctx) {
        if (flows.empty() || ribbonOutlines.size() != flows.size()) return;

        // Thick ribbons first, so slender ones stay visible on top of them.
        std::vector<size_t> order(flows.size());
        std::iota(order.begin(), order.end(), size_t{0});
        std::sort(order.begin(), order.end(), [this](size_t a, size_t b) {
            return flows[a].value > flows[b].value;
        });

        for (size_t idx : order) {
            const ChordFlow& f = flows[idx];
            if (!f.isVisible) continue;
            const auto& outline = ribbonOutlines[idx];
            if (outline.size() < 3) continue;

            bool active = IsRibbonActive(idx);
            float alphaScale = ribbonOpacity * (active ? 1.0f : dimmedOpacity);
            if (alphaScale <= 0.0f) continue;

            Color fill = f.flowColor;
            if (hoveredRibbon == idx) fill = fill.Lighten(0.15f);
            fill = fill.WithAlpha(static_cast<uint8_t>(
                std::min(255.0f, static_cast<float>(fill.a) * alphaScale)));

            ctx->SetFillPaint(fill);
            ctx->FillLinePath(outline);

            if (ribbonBorderWidth > 0.0f && ribbonBorderColor.a > 0) {
                Color border = ribbonBorderColor.WithAlpha(static_cast<uint8_t>(
                    std::min(255.0f, static_cast<float>(ribbonBorderColor.a) * alphaScale)));
                ctx->SetStrokePaint(border);
                ctx->SetStrokeWidth(hoveredRibbon == idx ? ribbonBorderWidth + 1.0f
                                                         : ribbonBorderWidth);
                ctx->DrawLinePath(outline, true);
            }
        }
    }

    void UltraCanvasChordChart::DrawCategories(IRenderContext* ctx) {
        for (size_t i = 0; i < chordMatrix.categories.size(); ++i) {
            const ChordCategory& cat = chordMatrix.categories[i];
            double span = cat.endAngle - cat.startAngle;
            if (span <= 1e-6) continue;

            bool active = IsCategoryActive(i);
            bool hovered = hoverHighlightEnabled && hoveredCategory == i;

            float rIn  = cachedInnerRadius;
            float rOut = cachedOuterRadius;
            if (hovered) rOut += 3.0f;

            int steps = std::max(3, static_cast<int>(std::ceil(span / (M_PI / 90.0))));
            std::vector<Point2Dd> pts;
            pts.reserve(2 * (steps + 1));
            ConnectionGeometry::SampleArc(cachedCenter, rOut,
                                          cat.startAngle, cat.endAngle, steps, pts);
            std::vector<Point2Dd> inner;
            ConnectionGeometry::SampleArc(cachedCenter, rIn,
                                          cat.startAngle, cat.endAngle, steps, inner);
            pts.insert(pts.end(), inner.rbegin(), inner.rend());

            Color fill = hovered ? cat.color.Lighten(0.2f) : cat.color;
            if (!active) {
                fill = fill.WithAlpha(static_cast<uint8_t>(
                    static_cast<float>(fill.a) * dimmedOpacity));
            }
            ctx->SetFillPaint(fill);
            ctx->FillLinePath(pts);

            ctx->SetStrokePaint(Color(255, 255, 255, active ? 220 : 90));
            ctx->SetStrokeWidth(1.5f);
            ctx->DrawLinePath(pts, true);
        }
    }

    void UltraCanvasChordChart::DrawLabels(IRenderContext* ctx) {
        ctx->PushState();
        ctx->SetFontFamily(labelFontFamily);
        ctx->SetFontSize(labelFontSize);
        ctx->SetFontWeight(labelFontWeight);

        double labelRadius = cachedOuterRadius + labelDistance;

        for (size_t i = 0; i < chordMatrix.categories.size(); ++i) {
            const ChordCategory& cat = chordMatrix.categories[i];
            double span = cat.endAngle - cat.startAngle;
            if (span <= 1e-6 || cat.name.empty()) continue;

            bool active = IsCategoryActive(i);
            Color c = active ? labelColor
                             : labelColor.WithAlpha(static_cast<uint8_t>(
                                   static_cast<float>(labelColor.a) * dimmedOpacity));
            ctx->SetTextPaint(c);

            double mid = (cat.startAngle + cat.endAngle) * 0.5;
            Size2Di ts = ctx->GetTextLineDimensions(cat.name);

            // Normalised angle decides which half of the circle we are on, so
            // text can be anchored (and flipped) to read outward.
            double norm = std::fmod(mid + 4.0 * M_PI, 2.0 * M_PI);
            bool leftSide = norm > M_PI / 2.0 && norm < 3.0 * M_PI / 2.0;

            if (labelPlacement == ChordLabelPlacement::Radial) {
                ctx->PushState();
                ctx->Translate(cachedCenter.x + labelRadius * std::cos(mid),
                               cachedCenter.y + labelRadius * std::sin(mid));
                ctx->Rotate(leftSide ? mid + M_PI : mid);
                ctx->DrawText(cat.name,
                              Point2Dd(leftSide ? -ts.width : 0.0, -ts.height / 2.0));
                ctx->PopState();
            } else {
                Point2Dd pos(cachedCenter.x + labelRadius * std::cos(mid),
                             cachedCenter.y + labelRadius * std::sin(mid));
                // Right-align on the left half so labels grow away from the ring.
                if (leftSide) pos.x -= ts.width;
                pos.y -= ts.height / 2.0;
                ctx->DrawText(cat.name, pos);
            }
        }
        ctx->PopState();
    }

// =============================================================================
// HIT TESTING & EVENTS
// =============================================================================

    size_t UltraCanvasChordChart::HitTestCategory(const Point2Dd& localPos) const {
        double dx = localPos.x - cachedCenter.x;
        double dy = localPos.y - cachedCenter.y;
        double r = std::sqrt(dx * dx + dy * dy);
        if (r < cachedInnerRadius || r > cachedOuterRadius + 3.0) return SIZE_MAX;

        double angle = std::atan2(dy, dx);
        for (size_t i = 0; i < chordMatrix.categories.size(); ++i) {
            const ChordCategory& cat = chordMatrix.categories[i];
            if (cat.endAngle - cat.startAngle <= 1e-6) continue;
            // Rotate the probe into the arc's own turn rather than normalising
            // both ends, which would break arcs that wrap past 2*pi.
            double a = angle;
            while (a < cat.startAngle) a += 2.0 * M_PI;
            while (a >= cat.startAngle + 2.0 * M_PI) a -= 2.0 * M_PI;
            if (a <= cat.endAngle) return i;
        }
        return SIZE_MAX;
    }

    size_t UltraCanvasChordChart::HitTestRibbon(const Point2Dd& localPos) const {
        double dx = localPos.x - cachedCenter.x;
        double dy = localPos.y - cachedCenter.y;
        if (std::sqrt(dx * dx + dy * dy) > cachedInnerRadius) return SIZE_MAX;

        // Walk thin-to-thick, mirroring the paint order so the ribbon actually
        // drawn on top is the one that answers the hit.
        std::vector<size_t> order(flows.size());
        std::iota(order.begin(), order.end(), size_t{0});
        std::sort(order.begin(), order.end(), [this](size_t a, size_t b) {
            return flows[a].value < flows[b].value;
        });

        for (size_t idx : order) {
            if (!flows[idx].isVisible) continue;
            if (idx >= ribbonOutlines.size()) continue;
            if (ConnectionGeometry::PolygonContains(ribbonOutlines[idx], localPos)) {
                return idx;
            }
        }
        return SIZE_MAX;
    }

    bool UltraCanvasChordChart::HandleChartMouseMove(const Point2Di& mousePos) {
        if (chordMatrix.categories.empty()) return false;

        Point2Dd local(static_cast<double>(mousePos.x), static_cast<double>(mousePos.y));

        size_t cat = HitTestCategory(local);
        size_t rib = (cat == SIZE_MAX) ? HitTestRibbon(local) : SIZE_MAX;

        if (cat != hoveredCategory || rib != hoveredRibbon) {
            hoveredCategory = cat;
            hoveredRibbon = rib;
            if (cat != SIZE_MAX && onCategoryHover) onCategoryHover(cat);
            RequestRedraw();
        }

        if (cat != SIZE_MAX || rib != SIZE_MAX) {
            if (enableTooltips) {
                std::string text = (cat != SIZE_MAX)
                                 ? BuildCategoryTooltip(cat)
                                 : BuildFlowTooltip(flows[rib]);
                if (!text.empty()) {
                    auto windowMousePos = MapFromLocal(mousePos, nullptr);
                    UltraCanvasTooltipManager::UpdateAndShowTooltip(
                        this->window, text, windowMousePos);
                    isTooltipActive = true;
                }
            }
            return true;
        }

        if (isTooltipActive) HideTooltip();
        return false;
    }

    bool UltraCanvasChordChart::HandleClick(const UCEvent& event) {
        Point2Dd local(static_cast<double>(event.pointer.x),
                       static_cast<double>(event.pointer.y));

        size_t cat = HitTestCategory(local);
        if (cat != SIZE_MAX) {
            if (onCategoryClick) onCategoryClick(cat);
            return true;
        }

        size_t rib = HitTestRibbon(local);
        if (rib != SIZE_MAX) {
            const ChordFlow& f = flows[rib];
            if (onFlowClick) onFlowClick(f.fromIndex, f.toIndex, f.value);
            return true;
        }
        return false;
    }

    bool UltraCanvasChordChart::OnEvent(const UCEvent& event) {
        if (IsDisabled() || !IsVisible()) return false;

        switch (event.type) {
            case UCEventType::MouseDown:
                if (event.button == UCMouseButton::Left && HandleClick(event)) {
                    return true;
                }
                break;
            case UCEventType::MouseLeave:
                if (AnythingHovered()) {
                    hoveredCategory = SIZE_MAX;
                    hoveredRibbon = SIZE_MAX;
                    RequestRedraw();
                }
                HideTooltip();
                return true;
            default:
                break;
        }
        return UltraCanvasChartElementBase::OnEvent(event);
    }

// =============================================================================
// FACTORY FUNCTIONS
// =============================================================================

    std::shared_ptr<UltraCanvasChordChart> CreateChordChart(
        const std::string& id, int x, int y, int w, int h) {
        return std::make_shared<UltraCanvasChordChart>(id, x, y, w, h);
    }

    std::shared_ptr<UltraCanvasChordChart> CreateChordChartFromMatrix(
        const std::string& id, int x, int y, int w, int h,
        const std::vector<std::vector<double>>& matrix,
        const std::vector<std::string>& categoryNames,
        const std::vector<Color>& colors) {
        auto chart = std::make_shared<UltraCanvasChordChart>(id, x, y, w, h);
        chart->ImportFromMatrix(matrix, categoryNames, colors);
        return chart;
    }

} // namespace UltraCanvas
