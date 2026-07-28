// Plugins/Charts/UltraCanvasSunburstChart.cpp
// Hierarchical sunburst / radial partition chart element for UltraCanvas
// Version: 1.0.0
// Last Modified: 2026-07-28
// Author: UltraCanvas Framework
#include "Plugins/Charts/UltraCanvasSunburstChart.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace UltraCanvas {

    // =========================================================================
    // SUNBURST NODE
    // =========================================================================

    std::shared_ptr<SunburstNode> SunburstNode::AddChild(const std::string& lbl,
                                                         double val,
                                                         const Color& clr) {
        auto child = std::make_shared<SunburstNode>(lbl, val, clr);
        child->parent = shared_from_this();
        children.push_back(child);
        return child;
    }

    std::shared_ptr<SunburstNode> SunburstNode::FindChild(const std::string& lbl) const {
        for (const auto& c : children) {
            if (c->label == lbl) return c;
        }
        return nullptr;
    }

    double SunburstNode::AggregatedValue() const {
        if (children.empty()) return std::max(0.0, value);
        double sum = 0.0;
        for (const auto& c : children) sum += c->AggregatedValue();
        return sum;
    }

    int SunburstNode::SubtreeDepth() const {
        int deepest = 0;
        for (const auto& c : children) deepest = std::max(deepest, c->SubtreeDepth());
        return deepest + 1;
    }

    // =========================================================================
    // CONSTRUCTION / DEFAULTS
    // =========================================================================

    std::vector<Color> UltraCanvasSunburstChart::DefaultPalette() {
        return {
            Color( 66, 133, 244, 255),   // Blue
            Color(219,  68,  55, 255),   // Red
            Color(244, 180,   0, 255),   // Amber
            Color( 15, 157,  88, 255),   // Green
            Color(171,  71, 188, 255),   // Purple
            Color(  0, 172, 193, 255),   // Cyan
            Color(255, 112,  67, 255),   // Deep orange
            Color(158, 157,  36, 255),   // Olive
            Color( 92, 107, 192, 255),   // Indigo
            Color(240,  98, 146, 255)    // Pink
        };
    }

    UltraCanvasSunburstChart::UltraCanvasSunburstChart(
        const std::string& id, int x, int y, int w, int h)
        : UltraCanvasChartElementBase(id, x, y, w, h)
    {
        colorPalette = DefaultPalette();
        rootNode = std::make_shared<SunburstNode>("root");
        showGrid = false;
        showAxes = false;
        showBackground = false;
    }

    // =========================================================================
    // DATA MANAGEMENT
    // =========================================================================

    void UltraCanvasSunburstChart::SetRootNode(std::shared_ptr<SunburstNode> root) {
        rootNode = root ? std::move(root) : std::make_shared<SunburstNode>("root");
        drillRoot.reset();
        hoveredSegment = SIZE_MAX;
        InvalidateSegments();
        InvalidateCache();
        RequestRedraw();
    }

    std::shared_ptr<SunburstNode> UltraCanvasSunburstChart::AddNode(
        const std::vector<std::string>& path, double value, const Color& color)
    {
        if (path.empty()) return nullptr;
        if (!rootNode) rootNode = std::make_shared<SunburstNode>("root");

        std::shared_ptr<SunburstNode> node = rootNode;
        for (const auto& label : path) {
            auto child = node->FindChild(label);
            if (!child) child = node->AddChild(label);
            node = child;
        }
        node->value = value;
        if (color.a > 0) node->color = color;

        InvalidateSegments();
        RequestRedraw();
        return node;
    }

    void UltraCanvasSunburstChart::ClearData() {
        rootNode = std::make_shared<SunburstNode>("root");
        drillRoot.reset();
        hoveredSegment = SIZE_MAX;
        InvalidateSegments();
        RequestRedraw();
    }

    // =========================================================================
    // CONFIGURATION SETTERS
    // =========================================================================

    void UltraCanvasSunburstChart::SetStartAngle(float degrees) {
        startAngleDeg = degrees;
        InvalidateSegments();
        RequestRedraw();
    }

    void UltraCanvasSunburstChart::SetSweepAngle(float degrees) {
        sweepAngleDeg = std::max(10.0f, std::min(360.0f, degrees));
        InvalidateSegments();
        RequestRedraw();
    }

    void UltraCanvasSunburstChart::SetInnerRadiusFraction(float fraction) {
        innerRadiusFraction = std::max(0.0f, std::min(0.9f, fraction));
        InvalidateSegments();
        RequestRedraw();
    }

    void UltraCanvasSunburstChart::SetRingSpacing(float pixels) {
        ringSpacing = std::max(0.0f, pixels);
        InvalidateSegments();
        RequestRedraw();
    }

    void UltraCanvasSunburstChart::SetSegmentSpacingAngle(float degrees) {
        segmentSpacingDeg = std::max(0.0f, std::min(10.0f, degrees));
        InvalidateSegments();
        RequestRedraw();
    }

    void UltraCanvasSunburstChart::SetMaxVisibleDepth(int depth) {
        maxVisibleDepth = std::max(0, depth);
        InvalidateSegments();
        RequestRedraw();
    }

    void UltraCanvasSunburstChart::SetColorPalette(const std::vector<Color>& palette) {
        colorPalette = palette.empty() ? DefaultPalette() : palette;
        InvalidateSegments();
        RequestRedraw();
    }

    void UltraCanvasSunburstChart::SetShadeMode(SunburstShadeMode mode) {
        shadeMode = mode;
        InvalidateSegments();
        RequestRedraw();
    }

    void UltraCanvasSunburstChart::SetShadeStep(float step) {
        shadeStep = std::max(0.0f, std::min(1.0f, step));
        InvalidateSegments();
        RequestRedraw();
    }

    void UltraCanvasSunburstChart::SetBorderColor(const Color& c) { borderColor = c; RequestRedraw(); }
    void UltraCanvasSunburstChart::SetBorderWidth(float w)        { borderWidth = std::max(0.0f, w); RequestRedraw(); }

    void UltraCanvasSunburstChart::SetCenterText(const std::string& text) {
        centerText = text;
        RequestRedraw();
    }

    void UltraCanvasSunburstChart::SetShowCenterTotal(bool show) {
        showCenterTotal = show;
        RequestRedraw();
    }

    void UltraCanvasSunburstChart::SetCenterTextColor(const Color& c) {
        centerTextColor = c;
        RequestRedraw();
    }

    void UltraCanvasSunburstChart::SetCenterFont(const std::string& family, float size, FontWeight weight) {
        centerFontFamily = family;
        centerFontSize = size;
        centerFontWeight = weight;
        RequestRedraw();
    }

    void UltraCanvasSunburstChart::SetLabelContent(SunburstLabelContent content) {
        labelContent = content;
        RequestRedraw();
    }

    void UltraCanvasSunburstChart::SetLabelOrientation(SunburstLabelOrientation orientation) {
        labelOrientation = orientation;
        RequestRedraw();
    }

    void UltraCanvasSunburstChart::SetLabelFont(const std::string& family, float size, FontWeight weight) {
        labelFontFamily = family;
        labelFontSize = size;
        labelFontWeight = weight;
        RequestRedraw();
    }

    void UltraCanvasSunburstChart::SetLabelColor(const Color& c) { labelColor = c; RequestRedraw(); }

    void UltraCanvasSunburstChart::SetMinLabelAngle(float degrees) {
        minLabelAngleDeg = std::max(0.0f, degrees);
        RequestRedraw();
    }

    void UltraCanvasSunburstChart::SetValueFormatter(std::function<std::string(double)> f) {
        valueFormatter = std::move(f);
        RequestRedraw();
    }

    void UltraCanvasSunburstChart::SetPercentageFormatter(std::function<std::string(double)> f) {
        percentFormatter = std::move(f);
        RequestRedraw();
    }

    void UltraCanvasSunburstChart::SetHoverHighlightEnabled(bool on) {
        hoverHighlightEnabled = on;
        if (!on && hoveredSegment != SIZE_MAX) {
            hoveredSegment = SIZE_MAX;
            RequestRedraw();
        }
    }

    void UltraCanvasSunburstChart::SetSunburstTooltipFormatter(
        std::function<std::string(const SunburstNode&)> f) {
        sunburstTooltipFormatter = std::move(f);
    }

    void UltraCanvasSunburstChart::SetDrillDownEnabled(bool on) {
        drillDownEnabled = on;
        if (!on) ResetDrill();
    }

    // =========================================================================
    // DRILL-DOWN NAVIGATION
    // =========================================================================

    std::shared_ptr<SunburstNode> UltraCanvasSunburstChart::GetCurrentRoot() const {
        auto locked = drillRoot.lock();
        return locked ? locked : rootNode;
    }

    void UltraCanvasSunburstChart::DrillTo(const std::shared_ptr<SunburstNode>& node) {
        if (!node || node->IsLeaf()) return;
        // Only accept nodes that actually belong to this chart's hierarchy.
        auto walker = node;
        while (auto p = walker->parent.lock()) walker = p;
        if (walker != rootNode && node != rootNode) return;

        drillRoot = (node == rootNode) ? std::weak_ptr<SunburstNode>() :
                    std::weak_ptr<SunburstNode>(node);
        hoveredSegment = SIZE_MAX;
        InvalidateSegments();
        RequestRedraw();
        if (onDrillChange) onDrillChange(GetCurrentRoot());
    }

    void UltraCanvasSunburstChart::DrillUp() {
        auto current = drillRoot.lock();
        if (!current) return;
        auto parent = current->parent.lock();
        drillRoot = (parent && parent != rootNode) ? std::weak_ptr<SunburstNode>(parent)
                                                   : std::weak_ptr<SunburstNode>();
        hoveredSegment = SIZE_MAX;
        InvalidateSegments();
        RequestRedraw();
        if (onDrillChange) onDrillChange(GetCurrentRoot());
    }

    void UltraCanvasSunburstChart::ResetDrill() {
        if (drillRoot.expired()) return;
        drillRoot.reset();
        hoveredSegment = SIZE_MAX;
        InvalidateSegments();
        RequestRedraw();
        if (onDrillChange) onDrillChange(GetCurrentRoot());
    }

    // =========================================================================
    // LAYOUT
    // =========================================================================

    ChartPlotArea UltraCanvasSunburstChart::CalculatePlotArea() {
        int marginSide   = 10;
        int marginTop    = chartTitle.empty() ? 10 : 34;
        int marginBottom = 10;
        ChartPlotArea area;
        area.x      = marginSide;
        area.y      = marginTop;
        area.width  = std::max(20.0f, static_cast<float>(GetWidth())  - 2.0f * marginSide);
        area.height = std::max(20.0f, static_cast<float>(GetHeight()) - marginTop - marginBottom);
        return area;
    }

    void UltraCanvasSunburstChart::InvalidateSegments() {
        segmentsValid = false;
    }

    Color UltraCanvasSunburstChart::ResolveBranchColor(
        size_t branchIndex, const std::shared_ptr<SunburstNode>& node) const
    {
        if (node->color.a > 0) return node->color;
        if (colorPalette.empty()) return Color(128, 128, 128, 255);
        return colorPalette[branchIndex % colorPalette.size()];
    }

    Color UltraCanvasSunburstChart::ShadeForDepth(const Color& base, int depth) const {
        if (depth <= 1) return base;
        float amount = shadeStep * static_cast<float>(depth - 1);
        amount = std::min(0.85f, amount);
        switch (shadeMode) {
            case SunburstShadeMode::LightenByDepth: return base.Lighten(amount);
            case SunburstShadeMode::DarkenByDepth:  return base.Darken(amount);
            case SunburstShadeMode::None:           break;
        }
        return base;
    }

    void UltraCanvasSunburstChart::LayoutNode(
        const std::shared_ptr<SunburstNode>& node, int depth,
        double a0, double a1, const Color& branchColor)
    {
        if (cachedVisibleDepth > 0 && depth > cachedVisibleDepth) return;

        double nodeValue = node->AggregatedValue();
        if (nodeValue <= 0.0 || a1 <= a0) return;

        Segment seg;
        seg.node       = node;
        seg.depth      = depth;
        seg.startAngle = a0;
        seg.endAngle   = a1;
        seg.value      = nodeValue;
        seg.percentage = cachedTotal > 0.0 ? nodeValue / cachedTotal : 0.0;
        seg.color      = ShadeForDepth(
            node->color.a > 0 ? node->color : branchColor, depth);
        segments.push_back(seg);

        // Distribute the node's angular span among its children.
        Color childBase = node->color.a > 0 ? node->color : branchColor;
        double cursor = a0;
        for (const auto& child : node->children) {
            double childValue = child->AggregatedValue();
            if (childValue <= 0.0) continue;
            double span = (a1 - a0) * (childValue / nodeValue);
            LayoutNode(child, depth + 1, cursor, cursor + span, childBase);
            cursor += span;
        }
    }

    void UltraCanvasSunburstChart::RebuildSegments() {
        segments.clear();
        segmentsValid = true;
        cachedTotal = 0.0;
        cachedVisibleDepth = 0;

        auto current = GetCurrentRoot();
        if (!current) return;

        cachedTotal = current->AggregatedValue();
        if (cachedTotal <= 0.0) return;

        int dataDepth = current->SubtreeDepth() - 1;  // root itself is not drawn
        if (dataDepth <= 0) return;
        cachedVisibleDepth = (maxVisibleDepth > 0)
            ? std::min(maxVisibleDepth, dataDepth) : dataDepth;

        double startRad = startAngleDeg * M_PI / 180.0;
        double sweepRad = sweepAngleDeg * M_PI / 180.0;

        double cursor = startRad;
        size_t branchIndex = 0;
        for (const auto& child : current->children) {
            double childValue = child->AggregatedValue();
            if (childValue <= 0.0) { ++branchIndex; continue; }
            double span = sweepRad * (childValue / cachedTotal);
            LayoutNode(child, 1, cursor, cursor + span,
                       ResolveBranchColor(branchIndex, child));
            cursor += span;
            ++branchIndex;
        }
    }

    void UltraCanvasSunburstChart::ComputeRadii() {
        cachedCenter = Point2Dd(cachedPlotArea.x + cachedPlotArea.width / 2.0,
                                cachedPlotArea.y + cachedPlotArea.height / 2.0);
        float maxR = std::min(cachedPlotArea.width, cachedPlotArea.height) * 0.5f - 4.0f;
        cachedOuterRadius = std::max(10.0f, maxR);
        cachedInnerRadius = cachedOuterRadius * innerRadiusFraction;

        int ringCount = std::max(1, cachedVisibleDepth);
        float radialSpace = cachedOuterRadius - cachedInnerRadius
                          - ringSpacing * static_cast<float>(ringCount - 1);
        cachedRingWidth = std::max(2.0f, radialSpace / static_cast<float>(ringCount));
    }

    // =========================================================================
    // FORMATTERS
    // =========================================================================

    std::string UltraCanvasSunburstChart::FormatValue(double v) const {
        if (valueFormatter) return valueFormatter(v);
        char buf[32];
        if (std::floor(v) == v && std::fabs(v) < 1e12) {
            std::snprintf(buf, sizeof(buf), "%.0f", v);
        } else {
            std::snprintf(buf, sizeof(buf), "%g", v);
        }
        return std::string(buf);
    }

    std::string UltraCanvasSunburstChart::FormatPercent(double pct) const {
        if (percentFormatter) return percentFormatter(pct);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f%%", pct * 100.0);
        return std::string(buf);
    }

    std::string UltraCanvasSunburstChart::BuildLabelText(const Segment& seg) const {
        switch (labelContent) {
            case SunburstLabelContent::None:          return "";
            case SunburstLabelContent::Name:          return seg.node->label;
            case SunburstLabelContent::Value:         return FormatValue(seg.value);
            case SunburstLabelContent::Percentage:    return FormatPercent(seg.percentage);
            case SunburstLabelContent::NameValue:
                return seg.node->label + " (" + FormatValue(seg.value) + ")";
            case SunburstLabelContent::NamePercentage:
                return seg.node->label + " (" + FormatPercent(seg.percentage) + ")";
        }
        return seg.node->label;
    }

    std::string UltraCanvasSunburstChart::BuildTooltipText(const Segment& seg) const {
        if (sunburstTooltipFormatter) return sunburstTooltipFormatter(*seg.node);

        std::string text;
        // Breadcrumb path from the current root down to this node.
        std::vector<std::string> chain;
        auto current = GetCurrentRoot();
        for (auto n = seg.node; n && n != current; n = n->parent.lock()) {
            chain.push_back(n->label);
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            if (!text.empty()) text += " › ";
            text += *it;
        }
        text += "\nValue: " + FormatValue(seg.value);
        text += "\nShare: " + FormatPercent(seg.percentage);
        auto parent = seg.node->parent.lock();
        if (parent && parent != current) {
            double parentValue = parent->AggregatedValue();
            if (parentValue > 0.0) {
                text += "\nOf " + parent->label + ": "
                     + FormatPercent(seg.value / parentValue);
            }
        }
        return text;
    }

    // =========================================================================
    // RENDERING
    // =========================================================================

    void UltraCanvasSunburstChart::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
        if (!ctx) return;

        UpdateRenderingCache();
        if (!segmentsValid) RebuildSegments();

        RenderCommonBackground(ctx);

        if (segments.empty()) {
            DrawEmptyState(ctx);
            return;
        }

        RenderChart(ctx);
    }

    void UltraCanvasSunburstChart::RenderTitle(IRenderContext* ctx) {
        if (chartTitle.empty()) return;
        ctx->PushState();
        ctx->SetFontFamily(labelFontFamily);
        ctx->SetFontSize(labelFontSize + 4.0f);
        ctx->SetFontWeight(FontWeight::Bold);
        ctx->SetTextPaint(labelColor);
        Size2Di ts = ctx->GetTextLineDimensions(chartTitle);
        ctx->DrawText(chartTitle,
                      Point2Dd(cachedPlotArea.x + (cachedPlotArea.width - ts.width) / 2.0,
                               cachedPlotArea.y - ts.height - 6.0));
        ctx->PopState();
    }

    void UltraCanvasSunburstChart::RenderChart(IRenderContext* ctx) {
        if (!ctx) return;
        UpdateRenderingCache();
        if (!segmentsValid) RebuildSegments();
        if (segments.empty()) {
            DrawEmptyState(ctx);
            return;
        }

        ComputeRadii();
        RenderTitle(ctx);

        for (size_t i = 0; i < segments.size(); ++i) {
            bool highlighted = hoverHighlightEnabled && i == hoveredSegment;
            DrawSegment(ctx, segments[i], highlighted);
        }

        if (labelContent != SunburstLabelContent::None) {
            RenderSegmentLabels(ctx);
        }

        RenderCenter(ctx);
    }

    void UltraCanvasSunburstChart::DrawSegment(IRenderContext* ctx,
                                               const Segment& seg,
                                               bool highlighted)
    {
        float rIn  = cachedInnerRadius
                   + (seg.depth - 1) * (cachedRingWidth + ringSpacing);
        float rOut = rIn + cachedRingWidth;
        if (highlighted) rOut += std::min(6.0f, ringSpacing + 4.0f);

        // Angular padding shrinks the segment on both sides; never collapse a
        // segment entirely because of padding.
        double pad = segmentSpacingDeg * M_PI / 180.0 * 0.5;
        double span = seg.endAngle - seg.startAngle;
        if (span <= 2.0 * pad) pad = span * 0.25;
        double a0 = seg.startAngle + pad;
        double a1 = seg.endAngle - pad;
        if (a1 <= a0) return;

        // Build the annular sector as a polyline (outer arc + reversed inner arc).
        int steps = std::max(4, static_cast<int>(std::ceil((a1 - a0) / (M_PI / 72.0))));
        std::vector<Point2Dd> pts;
        pts.reserve(2 * (steps + 1));
        for (int i = 0; i <= steps; ++i) {
            double a = a0 + (a1 - a0) * static_cast<double>(i) / steps;
            pts.emplace_back(cachedCenter.x + rOut * std::cos(a),
                             cachedCenter.y + rOut * std::sin(a));
        }
        for (int i = steps; i >= 0; --i) {
            double a = a0 + (a1 - a0) * static_cast<double>(i) / steps;
            pts.emplace_back(cachedCenter.x + rIn * std::cos(a),
                             cachedCenter.y + rIn * std::sin(a));
        }

        Color fill = highlighted ? seg.color.Lighten(0.18f) : seg.color;
        ctx->SetFillPaint(fill);
        ctx->FillLinePath(pts);

        if (borderWidth > 0.0f && borderColor.a > 0) {
            ctx->SetStrokePaint(borderColor);
            ctx->SetStrokeWidth(borderWidth);
            ctx->DrawLinePath(pts, true);
        }
    }

    void UltraCanvasSunburstChart::RenderSegmentLabels(IRenderContext* ctx) {
        double minAngle = minLabelAngleDeg * M_PI / 180.0;

        ctx->PushState();
        ctx->SetFontFamily(labelFontFamily);
        ctx->SetFontSize(labelFontSize);
        ctx->SetFontWeight(labelFontWeight);
        ctx->SetTextPaint(labelColor);

        for (const Segment& seg : segments) {
            double span = seg.endAngle - seg.startAngle;
            if (span < minAngle) continue;

            std::string text = BuildLabelText(seg);
            if (text.empty()) continue;

            float rIn  = cachedInnerRadius
                       + (seg.depth - 1) * (cachedRingWidth + ringSpacing);
            float rMid = rIn + cachedRingWidth * 0.5f;
            double aMid = (seg.startAngle + seg.endAngle) * 0.5;

            Size2Di ts = ctx->GetTextLineDimensions(text);
            double arcLen = span * rMid;

            bool fitsHorizontal = ts.width <= arcLen - 4.0 &&
                                  ts.width <= 2.0 * rMid &&
                                  ts.height <= cachedRingWidth - 2.0f;
            bool fitsRadial = ts.width <= cachedRingWidth - 4.0f &&
                              ts.height <= arcLen - 2.0;

            SunburstLabelOrientation mode = labelOrientation;
            if (mode == SunburstLabelOrientation::Auto) {
                if (fitsHorizontal)      mode = SunburstLabelOrientation::Horizontal;
                else if (fitsRadial)     mode = SunburstLabelOrientation::Radial;
                else                     continue;
            } else if (mode == SunburstLabelOrientation::Horizontal && !fitsHorizontal) {
                continue;
            } else if (mode == SunburstLabelOrientation::Radial && !fitsRadial) {
                continue;
            }

            if (mode == SunburstLabelOrientation::Horizontal) {
                Point2Dd pos(cachedCenter.x + rMid * std::cos(aMid) - ts.width / 2.0,
                             cachedCenter.y + rMid * std::sin(aMid) - ts.height / 2.0);
                ctx->DrawText(text, pos);
            } else {
                // Radial: text runs outward along the mid-angle; on the left
                // half it is flipped so it never renders upside down.
                double norm = std::fmod(aMid + 2.0 * M_PI, 2.0 * M_PI);
                bool leftSide = norm > M_PI / 2.0 && norm < 3.0 * M_PI / 2.0;

                ctx->PushState();
                ctx->Translate(cachedCenter.x + rMid * std::cos(aMid),
                               cachedCenter.y + rMid * std::sin(aMid));
                ctx->Rotate(leftSide ? aMid + M_PI : aMid);
                ctx->DrawText(text, Point2Dd(-ts.width / 2.0, -ts.height / 2.0));
                ctx->PopState();
            }
        }

        ctx->PopState();
    }

    void UltraCanvasSunburstChart::RenderCenter(IRenderContext* ctx) {
        if (cachedInnerRadius < 12.0f) return;

        auto current = GetCurrentRoot();
        bool drilled = !drillRoot.expired();

        std::vector<std::string> lines;
        if (!centerText.empty()) {
            lines.push_back(centerText);
        } else if (drilled) {
            lines.push_back(current->label);
        }
        if (showCenterTotal) {
            lines.push_back(FormatValue(cachedTotal));
        }
        if (drilled && drillDownEnabled) {
            lines.push_back("↑ back");
        }
        if (lines.empty()) return;

        ctx->PushState();
        ctx->SetFontFamily(centerFontFamily);
        ctx->SetFontSize(centerFontSize);
        ctx->SetFontWeight(centerFontWeight);
        ctx->SetTextPaint(centerTextColor);

        float lineHeight = centerFontSize * 1.3f;
        float blockHeight = lineHeight * static_cast<float>(lines.size());
        float y = static_cast<float>(cachedCenter.y) - blockHeight / 2.0f;

        for (size_t i = 0; i < lines.size(); ++i) {
            // Secondary lines (total / back hint) are rendered smaller.
            if (i == 1) {
                ctx->SetFontSize(centerFontSize * 0.85f);
                ctx->SetFontWeight(FontWeight::Normal);
            }
            Size2Di ts = ctx->GetTextLineDimensions(lines[i]);
            // Keep the text inside the hole.
            if (ts.width > cachedInnerRadius * 1.8f) { y += lineHeight; continue; }
            ctx->DrawText(lines[i],
                          Point2Dd(cachedCenter.x - ts.width / 2.0, y));
            y += lineHeight;
        }
        ctx->PopState();
    }

    // =========================================================================
    // HIT TESTING & EVENTS
    // =========================================================================

    size_t UltraCanvasSunburstChart::HitTestSegment(const Point2Dd& localPos) const {
        if (segments.empty()) return SIZE_MAX;

        double dx = localPos.x - cachedCenter.x;
        double dy = localPos.y - cachedCenter.y;
        double r = std::sqrt(dx * dx + dy * dy);
        if (r < cachedInnerRadius || r > cachedOuterRadius) return SIZE_MAX;

        // Radius -> ring depth
        double band = cachedRingWidth + ringSpacing;
        int depth = band > 0.0
            ? 1 + static_cast<int>((r - cachedInnerRadius) / band) : 1;
        // Reject clicks landing in the spacing gap between rings.
        double innerEdge = cachedInnerRadius + (depth - 1) * band;
        if (r > innerEdge + cachedRingWidth) return SIZE_MAX;

        double angle = std::atan2(dy, dx);
        for (size_t i = 0; i < segments.size(); ++i) {
            const Segment& seg = segments[i];
            if (seg.depth != depth) continue;
            double a = angle;
            while (a < seg.startAngle) a += 2.0 * M_PI;
            while (a >= seg.startAngle + 2.0 * M_PI) a -= 2.0 * M_PI;
            if (a >= seg.startAngle && a <= seg.endAngle) return i;
        }
        return SIZE_MAX;
    }

    bool UltraCanvasSunburstChart::HitTestCenter(const Point2Dd& localPos) const {
        double dx = localPos.x - cachedCenter.x;
        double dy = localPos.y - cachedCenter.y;
        return std::sqrt(dx * dx + dy * dy) < cachedInnerRadius;
    }

    bool UltraCanvasSunburstChart::HandleChartMouseMove(const Point2Di& mousePos) {
        if (segments.empty()) return false;

        Point2Dd local(static_cast<double>(mousePos.x), static_cast<double>(mousePos.y));
        size_t hit = HitTestSegment(local);

        if (hit != hoveredSegment) {
            hoveredSegment = hit;
            if (hit != SIZE_MAX && onSegmentHover) {
                onSegmentHover(segments[hit].node, segments[hit].depth);
            }
            RequestRedraw();
        }

        if (hit != SIZE_MAX) {
            if (enableTooltips) {
                const Segment& seg = segments[hit];
                std::string tooltip = BuildTooltipText(seg);
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

    bool UltraCanvasSunburstChart::HandleClick(const UCEvent& event) {
        Point2Dd local(static_cast<double>(event.pointer.x),
                       static_cast<double>(event.pointer.y));

        size_t hit = HitTestSegment(local);
        if (hit != SIZE_MAX) {
            const Segment& seg = segments[hit];
            if (onSegmentClick) onSegmentClick(seg.node, seg.depth);
            if (drillDownEnabled && !seg.node->IsLeaf()) {
                DrillTo(seg.node);
            }
            return true;
        }

        if (drillDownEnabled && !drillRoot.expired() && HitTestCenter(local)) {
            DrillUp();
            return true;
        }
        return false;
    }

    bool UltraCanvasSunburstChart::OnEvent(const UCEvent& event) {
        if (IsDisabled() || !IsVisible()) return false;

        switch (event.type) {
            case UCEventType::MouseDown:
                if (event.button == UCMouseButton::Left && HandleClick(event)) {
                    return true;
                }
                break;
            case UCEventType::MouseLeave:
                if (hoveredSegment != SIZE_MAX) {
                    hoveredSegment = SIZE_MAX;
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

    std::shared_ptr<UltraCanvasSunburstChart> CreateSunburstChart(
        const std::string& id, int x, int y, int w, int h)
    {
        return std::make_shared<UltraCanvasSunburstChart>(id, x, y, w, h);
    }

    std::shared_ptr<UltraCanvasSunburstChart> CreateSunburstChartFromGroups(
        const std::string& id, int x, int y, int w, int h,
        const std::vector<std::pair<std::string,
              std::vector<std::pair<std::string, double>>>>& groupedData)
    {
        auto chart = std::make_shared<UltraCanvasSunburstChart>(id, x, y, w, h);
        for (const auto& group : groupedData) {
            for (const auto& entry : group.second) {
                chart->AddNode({group.first, entry.first}, entry.second);
            }
        }
        return chart;
    }

} // namespace UltraCanvas
