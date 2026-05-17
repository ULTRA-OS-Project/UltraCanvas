// UltraCanvasArcDiagram.cpp
// Arc diagram — nodes on a baseline, edges as cubic Bezier arcs above/below
// Version: 1.0.2
// Last Modified: 2026-05-17
// Author: UltraCanvas Framework
// Changes: P1 degree sizing, P2 vertical labels, P3 opacity weight, P4 semicircle mode,
//          P5 axis arrow, P6 mid-arc arrowhead, P7 self-loops, P8 parallel bundles,
//          P9 span z-order, P10 color legend,
//          P11 connected-edge highlight on node hover/select,
//          P12 auto value/percentage label at arc apex (zenit)

#include "Plugins/Diagrams/UltraCanvasArcDiagram.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <iomanip>

namespace UltraCanvas {

// ─────────────────────────────────────────────
// CONSTRUCTION
// ─────────────────────────────────────────────

    UltraCanvasArcDiagram::UltraCanvasArcDiagram(
            const std::string& id,
            long x, long y, long w, long h)
            : UltraCanvasUIElement(id, x, y, w, h)
    {}

// ─────────────────────────────────────────────
// NODE API
// ─────────────────────────────────────────────

    int UltraCanvasArcDiagram::AddNode(const ArcNode& node) {
        if (LookupNode(node.id) >= 0) return LookupNode(node.id);  // reject duplicate
        int idx = static_cast<int>(nodes.size());
        nodes.push_back(node);
        needsLayout = true;
        RequestRedraw();
        return idx;
    }

    int UltraCanvasArcDiagram::AddNode(const std::string& id, const std::string& label) {
        ArcNode n;
        n.id    = id;
        n.label = label;
        return AddNode(n);
    }

    bool UltraCanvasArcDiagram::RemoveNode(const std::string& id) {
        int idx = LookupNode(id);
        if (idx < 0) return false;

        nodes.erase(nodes.begin() + idx);

        // Remove all edges referencing this node
        edges.erase(
                std::remove_if(edges.begin(), edges.end(),
                               [&id](const ArcEdge& e) {
                                   return e.sourceId == id || e.targetId == id;
                               }),
                edges.end());

        needsLayout = true;
        RequestRedraw();
        return true;
    }

    const ArcNode* UltraCanvasArcDiagram::GetNode(int index) const {
        if (index < 0 || index >= static_cast<int>(nodes.size())) return nullptr;
        return &nodes[index];
    }

    const ArcNode* UltraCanvasArcDiagram::GetNodeById(const std::string& id) const {
        int idx = LookupNode(id);
        return idx >= 0 ? &nodes[idx] : nullptr;
    }

    void UltraCanvasArcDiagram::UpdateNode(const std::string& id, const ArcNode& updated) {
        int idx = LookupNode(id);
        if (idx < 0) return;
        nodes[idx] = updated;
        nodes[idx].id = id; // ensure ID is preserved
        RequestRedraw();
    }

// ─────────────────────────────────────────────
// EDGE API
// ─────────────────────────────────────────────

    int UltraCanvasArcDiagram::AddEdge(const ArcEdge& edge) {
        int idx = static_cast<int>(edges.size());
        edges.push_back(edge);
        maxWeight = std::max(maxWeight, edge.weight);
        needsLayout = true;     // P1 — degree counts change
        RequestRedraw();
        return idx;
    }

    int UltraCanvasArcDiagram::AddEdge(
            const std::string& sourceId,
            const std::string& targetId,
            float weight)
    {
        ArcEdge e;
        e.sourceId = sourceId;
        e.targetId = targetId;
        e.weight   = weight;
        return AddEdge(e);
    }

    void UltraCanvasArcDiagram::RemoveEdge(
            const std::string& sourceId,
            const std::string& targetId)
    {
        edges.erase(
                std::remove_if(edges.begin(), edges.end(),
                               [&](const ArcEdge& e) {
                                   return (e.sourceId == sourceId && e.targetId == targetId)
                                          || (e.sourceId == targetId && e.targetId == sourceId);
                               }),
                edges.end());
        needsLayout = true;     // refresh degree + weight caches
        RequestRedraw();
    }

    void UltraCanvasArcDiagram::ClearEdges() {
        edges.clear();
        maxWeight = 1.0f;
        needsLayout = true;     // refresh degree + weight caches
        RequestRedraw();
    }

    void UltraCanvasArcDiagram::Clear() {
        nodes.clear();
        edges.clear();
        nodePositions.clear();
        maxWeight = 1.0f;
        hoveredNodeIdx = hoveredEdgeIdx = -1;
        selectedNodeIdx = selectedEdgeIdx = -1;
        needsLayout = true;
        RequestRedraw();
    }

// ─────────────────────────────────────────────
// LAYOUT
// ─────────────────────────────────────────────

    void UltraCanvasArcDiagram::ComputeLayout() {
        int n = static_cast<int>(nodes.size());
        nodePositions.resize(n);

        bool horiz = (style.orientation == ArcOrientation::Horizontal);
        float totalLen = horiz
                         ? static_cast<float>(GetWidth())  - style.marginH * 2.0f
                         : static_cast<float>(GetHeight()) - style.marginV * 2.0f;

        // Evenly space nodes across available span
        // If nodePadding is set, use that; otherwise distribute evenly
        float spacing = (n > 1)
                        ? std::min(style.nodePadding, totalLen / static_cast<float>(n - 1))
                        : 0.0f;

        float usedSpan = spacing * static_cast<float>(n - 1);
        float startPos = (horiz ? style.marginH : style.marginV)
                         + (totalLen - usedSpan) * 0.5f;

        for (int i = 0; i < n; ++i) {
            nodePositions[i] = startPos + i * spacing;
        }
        availableSpan = usedSpan;

        // Baseline sits in the middle vertically (horiz) or middle horizontally (vert)
        if (horiz) {
            float labelH = style.labelFontSize + style.labelMargin + style.nodeBaseSize;
            baselinePos = static_cast<float>(GetHeight()) * 0.5f;
            // Shift baseline down if labels are below to give arcs more space above
            if (style.labelSide == ArcLabelSide::Below) {
                baselinePos = static_cast<float>(GetHeight()) * 0.4f;
            } else if (style.labelSide == ArcLabelSide::Above) {
                baselinePos = static_cast<float>(GetHeight()) * 0.6f;
            }
        } else {
            baselinePos = static_cast<float>(GetWidth()) * 0.5f;
        }

        needsLayout = false;
    }

    // P1 — count edges per node into degreeCache
    void UltraCanvasArcDiagram::ComputeNodeDegrees() {
        int n = static_cast<int>(nodes.size());
        degreeCache.assign(n, 0);
        for (const auto& edge : edges) {
            int si = LookupNode(edge.sourceId);
            int ti = LookupNode(edge.targetId);
            if (si >= 0 && si < n) ++degreeCache[si];
            if (ti >= 0 && ti < n && ti != si) ++degreeCache[ti];
        }
    }

    // P12 — sum of all edge weights, used as denominator for percentage display
    void UltraCanvasArcDiagram::ComputeTotalEdgeWeight() {
        totalEdgeWeight = 0.0f;
        for (const auto& e : edges) totalEdgeWeight += e.weight;
    }

    // P12 — format weight as "value" or "percent%" per style.arcValueDisplay
    std::string UltraCanvasArcDiagram::FormatArcValueLabel(float weight) const {
        std::ostringstream ss;
        int decimals = std::max(0, style.arcValueDecimals);
        ss << std::fixed << std::setprecision(decimals);
        switch (style.arcValueDisplay) {
            case ArcValueDisplay::Value:
                ss << weight;
                break;
            case ArcValueDisplay::Percentage: {
                float pct = (totalEdgeWeight > 0.0f)
                            ? (weight / totalEdgeWeight) * 100.0f : 0.0f;
                ss << pct << "%";
                break;
            }
            default:
                return std::string();
        }
        return ss.str();
    }

    // P1 — unified node radius computation respecting nodeSizeMode
    float UltraCanvasArcDiagram::NodeRadius(int idx) const {
        if (idx < 0 || idx >= static_cast<int>(nodes.size())) return style.nodeBaseSize;
        const ArcNode& node = nodes[idx];

        switch (style.nodeSizeMode) {
            case ArcNodeSizeMode::ByValue:
                return std::max(2.0f, node.size * std::sqrt(std::max(0.1f, node.value)));
            case ArcNodeSizeMode::ByDegree: {
                float deg = (idx < static_cast<int>(degreeCache.size()))
                            ? static_cast<float>(degreeCache[idx]) : 1.0f;
                return std::max(2.0f, style.nodeBaseSize * std::sqrt(std::max(1.0f, deg)));
            }
            default: // Fixed
                return std::max(2.0f, node.size);
        }
    }

// ─────────────────────────────────────────────
// COORDINATE HELPERS
// ─────────────────────────────────────────────

    float UltraCanvasArcDiagram::NodeScreenPos(int idx) const {
        if (idx < 0 || idx >= static_cast<int>(nodePositions.size())) return 0.0f;
        return nodePositions[idx];
    }

    void UltraCanvasArcDiagram::BaselinePoint(float pos, float& outX, float& outY) const {
        if (style.orientation == ArcOrientation::Horizontal) {
            outX = pos;
            outY = baselinePos;
        } else {
            outX = baselinePos;
            outY = pos;
        }
    }

    float UltraCanvasArcDiagram::ArcHeight(int srcIdx, int tgtIdx, int bundleOffset) const {
        float posA = NodeScreenPos(srcIdx);
        float posB = NodeScreenPos(tgtIdx);
        float dist = std::fabs(posB - posA);

        float base = 0.0f;
        switch (style.arcHeightMode) {
            case ArcHeightMode::Fixed:
                base = style.arcFixedHeight;
                break;
            case ArcHeightMode::Sqrt:
                base = std::sqrt(dist) * style.arcHeightScale * 10.0f;
                break;
            case ArcHeightMode::Semicircle:
                base = dist * 0.5f;     // P4 — perfect D-shape
                break;
            default: // Proportional
                base = dist * style.arcHeightScale;
                break;
        }

        // P8 — each parallel edge in a bundle adds a fixed step
        return base + static_cast<float>(bundleOffset) * style.bundleHeightStep;
    }

    float UltraCanvasArcDiagram::ArcWidth(float weight) const {
        if (maxWeight <= 0.0f) return style.arcMinWidth;
        float t = std::max(0.0f, std::min(1.0f, weight / maxWeight));
        return style.arcMinWidth + t * (style.arcMaxWidth - style.arcMinWidth);
    }

    // P3 — map weight to arc alpha (0–255)
    float UltraCanvasArcDiagram::ArcOpacity(float weight) const {
        if (!style.arcEncodeOpacity) return 255.0f;
        if (maxWeight <= 0.0f) return static_cast<float>(style.arcMaxOpacity);
        float t = std::max(0.0f, std::min(1.0f, weight / maxWeight));
        return style.arcMinOpacity + t * (style.arcMaxOpacity - style.arcMinOpacity);
    }

    bool UltraCanvasArcDiagram::EdgeSideIsAbove(
            const ArcEdge& edge, int srcIdx, int tgtIdx) const
    {
        switch (edge.side) {
            case ArcEdgeType::Above: return true;
            case ArcEdgeType::Below: return false;
            default: // Auto
                return srcIdx < tgtIdx; // forward = above, back = below
        }
    }

    // Compute the two cubic Bezier control points for an arc
    void UltraCanvasArcDiagram::ArcControlPoints(
            int srcIdx, int tgtIdx, bool above,
            float heightOverride,
            float& cp1x, float& cp1y,
            float& cp2x, float& cp2y) const
    {
        float posS = NodeScreenPos(srcIdx);
        float posT = NodeScreenPos(tgtIdx);
        float h    = heightOverride;
        float sign = above ? -1.0f : 1.0f;

        bool horiz = (style.orientation == ArcOrientation::Horizontal);

        if (horiz) {
            cp1x = posS + (posT - posS) / 3.0f;
            cp1y = baselinePos + sign * h;
            cp2x = posS + (posT - posS) * 2.0f / 3.0f;
            cp2y = baselinePos + sign * h;
        } else {
            cp1x = baselinePos + sign * h;
            cp1y = posS + (posT - posS) / 3.0f;
            cp2x = baselinePos + sign * h;
            cp2y = posS + (posT - posS) * 2.0f / 3.0f;
        }
    }

// ─────────────────────────────────────────────
// DRAW HELPERS
// ─────────────────────────────────────────────

    void UltraCanvasArcDiagram::DrawBaseline(IRenderContext* ctx) const {
        if (!style.showBaseline) return;
        int n = static_cast<int>(nodes.size());
        if (n == 0) return;

        float startPos = nodePositions.front();
        float endPos   = nodePositions.back();

        // P5 — extend end past last node when axis arrow is enabled
        float extendedEnd = style.showAxisArrow
                            ? endPos + style.axisArrowOverhang
                            : endPos;

        float x1, y1, x2, y2;
        BaselinePoint(startPos,   x1, y1);
        BaselinePoint(extendedEnd, x2, y2);

        ctx->SetStrokePaint(style.baselineColor);
        ctx->SetStrokeWidth(style.baselineWidth);
        ctx->DrawLine(Point2Df(x1, y1), Point2Df(x2, y2));

        // P5 — draw axis arrowhead at the extended tip
        if (style.showAxisArrow) {
            bool horiz = (style.orientation == ArcOrientation::Horizontal);
            float angle = horiz ? 0.0f : static_cast<float>(M_PI) * 0.5f;
            DrawArrowhead(ctx, x2, y2, angle, style.baselineColor);
        }
    }

    void UltraCanvasArcDiagram::DrawArrowhead(
            IRenderContext* ctx,
            float tipX, float tipY,
            float angle, const Color& col) const
    {
        float sz = style.arrowSize;
        float spread = 0.4f;  // half-angle of arrowhead

        float ax = tipX - sz * std::cos(angle - spread);
        float ay = tipY - sz * std::sin(angle - spread);
        float bx = tipX - sz * std::cos(angle + spread);
        float by = tipY - sz * std::sin(angle + spread);

        ctx->SetFillPaint(col);
        ctx->ClearPath();
        ctx->MoveTo(tipX, tipY);
        ctx->LineTo(ax, ay);
        ctx->LineTo(bx, by);
        ctx->ClosePath();
        ctx->Fill();
    }

    void UltraCanvasArcDiagram::DrawArc(
            IRenderContext* ctx,
            const ArcEdge& edge,
            int srcIdx, int tgtIdx,
            bool hovered, bool selected,
            bool connectedHighlight,
            int bundleOffset) const
    {
        bool above = EdgeSideIsAbove(edge, srcIdx, tgtIdx);

        // P8 — compute height with bundle offset applied
        float h = ArcHeight(srcIdx, tgtIdx, bundleOffset);

        float cp1x, cp1y, cp2x, cp2y;
        ArcControlPoints(srcIdx, tgtIdx, above, h, cp1x, cp1y, cp2x, cp2y);

        float posS = NodeScreenPos(srcIdx);
        float posT = NodeScreenPos(tgtIdx);
        float sx, sy, tx, ty;
        BaselinePoint(posS, sx, sy);
        BaselinePoint(posT, tx, ty);

        // P3 — compute arc color with optional opacity encoding
        Color col = edge.color;
        if (style.arcEncodeOpacity) {
            col.a = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, ArcOpacity(edge.weight))));
        }
        // P11 — brighten when hovered, selected, or attached to active node
        if (hovered || selected || connectedHighlight) {
            col.r = static_cast<uint8_t>(std::min(255, col.r + 40));
            col.g = static_cast<uint8_t>(std::min(255, col.g + 40));
            col.b = static_cast<uint8_t>(std::min(255, col.b + 40));
            col.a = 255;
        }

        float lw = ArcWidth(edge.weight);
        if (selected) lw += 1.5f;
        else if (connectedHighlight) lw += 0.75f;

        ctx->SetStrokePaint(col);
        ctx->SetStrokeWidth(lw);

        ctx->ClearPath();
        ctx->MoveTo(sx, sy);
        ctx->BezierCurveTo(cp1x, cp1y, cp2x, cp2y, tx, ty);
        ctx->Stroke();

        // Arrowhead
        if (edge.directed && style.showArrows) {
            if (style.arrowPosition == ArcArrowPosition::AtMidArc) {
                // P6 — arrowhead at arc apex (t=0.5)
                float mx = 0.125f*sx + 0.375f*cp1x + 0.375f*cp2x + 0.125f*tx;
                float my = 0.125f*sy + 0.375f*cp1y + 0.375f*cp2y + 0.125f*ty;
                // Tangent at t=0.5: derivative of cubic bezier
                float dtx = 3.0f*(0.25f*(cp1x-sx) + 0.5f*(cp2x-cp1x) + 0.25f*(tx-cp2x));
                float dty = 3.0f*(0.25f*(cp1y-sy) + 0.5f*(cp2y-cp1y) + 0.25f*(ty-cp2y));
                float angle = std::atan2(dty, dtx);
                DrawArrowhead(ctx, mx, my, angle, col);
            } else {
                // AtTarget — tangent at t=1: end - cp2
                float dx    = tx - cp2x;
                float dy    = ty - cp2y;
                float angle = std::atan2(dy, dx);
                float nodeR = NodeRadius(tgtIdx);
                float tipX  = tx - nodeR * std::cos(angle);
                float tipY  = ty - nodeR * std::sin(angle);
                DrawArrowhead(ctx, tipX, tipY, angle, col);
            }
        }

        // Arc apex (zenit) label — P12 auto value/% takes precedence over edge.label
        std::string apexText;
        Color       apexColor   = style.arcLabelColor;
        float       apexFont    = style.arcLabelFontSize;
        bool        apexOutside = false;
        if (style.arcValueDisplay != ArcValueDisplay::None) {
            apexText    = FormatArcValueLabel(edge.weight);
            apexColor   = style.arcValueLabelColor;
            apexFont    = style.arcValueFontSize;
            apexOutside = true;
        } else if (!edge.label.empty()) {
            apexText = edge.label;
        }

        if (!apexText.empty()) {
            float apexX = 0.125f*sx + 0.375f*cp1x + 0.375f*cp2x + 0.125f*tx;
            float apexY = 0.125f*sy + 0.375f*cp1y + 0.375f*cp2y + 0.125f*ty;

            ctx->SetFontSize(apexFont);
            ctx->SetFontFace("Sans", FontWeight::Normal, FontSlant::Normal);
            ctx->SetTextPaint(apexColor);

            auto dims = ctx->GetTextLineDimensions(apexText);
            double tw = dims.width, th = dims.height;

            // Auto-generated value labels sit OUTSIDE the arc (above for above-arcs,
            // below for below-arcs) so they don't overlap the curve. Explicit
            // edge.label keeps the legacy centered-at-apex placement.
            float ly;
            if (apexOutside) {
                ly = above
                     ? static_cast<float>(apexY - th - style.arcValueLabelOffset)
                     : static_cast<float>(apexY + style.arcValueLabelOffset);
            } else {
                ly = static_cast<float>(apexY - th * 0.5);
            }
            ctx->DrawText(apexText,
                          Point2Df(static_cast<float>(apexX - tw * 0.5), ly));
        }
    }

    // P7 — self-loop: draw a small circle above (or below) the node
    void UltraCanvasArcDiagram::DrawSelfLoop(
            IRenderContext* ctx,
            const ArcEdge& edge, int nodeIdx,
            bool hovered, bool selected,
            bool connectedHighlight) const
    {
        float pos = NodeScreenPos(nodeIdx);
        float cx, cy;
        BaselinePoint(pos, cx, cy);

        float nodeR  = NodeRadius(nodeIdx);
        float loopR  = nodeR * 1.4f + 6.0f;
        bool  above  = (edge.side != ArcEdgeType::Below);
        float sign   = above ? -1.0f : 1.0f;
        float loopCY = cy + sign * (nodeR + loopR);

        Color col = edge.color;
        if (style.arcEncodeOpacity)
            col.a = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, ArcOpacity(edge.weight))));
        // P11 — also brighten self-loops when their node is the active one
        if (hovered || selected || connectedHighlight) {
            col.r = static_cast<uint8_t>(std::min(255, col.r + 40));
            col.g = static_cast<uint8_t>(std::min(255, col.g + 40));
            col.b = static_cast<uint8_t>(std::min(255, col.b + 40));
            col.a = 255;
        }

        float lw = ArcWidth(edge.weight);
        if (selected) lw += 1.5f;
        else if (connectedHighlight) lw += 0.75f;

        ctx->SetStrokePaint(col);
        ctx->SetStrokeWidth(lw);
        ctx->ClearPath();
        ctx->Circle(cx, loopCY, loopR);
        ctx->Stroke();

        // Loop apex label — P12 value/% takes precedence over edge.label
        std::string apexText;
        Color       apexColor = style.arcLabelColor;
        float       apexFont  = style.arcLabelFontSize;
        if (style.arcValueDisplay != ArcValueDisplay::None) {
            apexText  = FormatArcValueLabel(edge.weight);
            apexColor = style.arcValueLabelColor;
            apexFont  = style.arcValueFontSize;
        } else if (!edge.label.empty()) {
            apexText = edge.label;
        }

        if (!apexText.empty()) {
            float apexY = loopCY + sign * loopR;
            ctx->SetFontSize(apexFont);
            ctx->SetFontFace("Sans", FontWeight::Normal, FontSlant::Normal);
            ctx->SetTextPaint(apexColor);
            auto dims = ctx->GetTextLineDimensions(apexText);
            int tw = dims.width, th = dims.height;
            float gap = (style.arcValueDisplay != ArcValueDisplay::None)
                        ? style.arcValueLabelOffset : 2.0f;
            float ly = above ? (apexY - th - gap) : (apexY + gap);
            ctx->DrawText(apexText, Point2Df(cx - tw * 0.5f, ly));
        }
    }

    void UltraCanvasArcDiagram::DrawEdges(IRenderContext* ctx) const {
        // P11 — selected node wins over hovered for "active" highlight target
        int activeNodeIdx = (selectedNodeIdx >= 0) ? selectedNodeIdx : hoveredNodeIdx;

        // P9 — sort edges by span (descending = longest first, shortest on top)
        // Build a sorted index list without modifying the original edge vector
        std::vector<int> order(edges.size());
        for (int i = 0; i < static_cast<int>(edges.size()); ++i) order[i] = i;

        if (style.sortBySpan) {
            std::sort(order.begin(), order.end(), [this](int a, int b) {
                int asrc = LookupNode(edges[a].sourceId);
                int atgt = LookupNode(edges[a].targetId);
                int bsrc = LookupNode(edges[b].sourceId);
                int btgt = LookupNode(edges[b].targetId);
                float spanA = (asrc >= 0 && atgt >= 0)
                              ? std::fabs(NodeScreenPos(asrc) - NodeScreenPos(atgt)) : 0.0f;
                float spanB = (bsrc >= 0 && btgt >= 0)
                              ? std::fabs(NodeScreenPos(bsrc) - NodeScreenPos(btgt)) : 0.0f;
                return spanA > spanB;   // longest first
            });
        }

        // P8 — track parallel edges between same pair to assign bundle offsets
        // Key: canonical pair (min,max) + side
        std::map<std::tuple<int,int,bool>, int> bundleCount;

        for (int oi : order) {
            const ArcEdge& edge = edges[oi];
            int srcIdx = LookupNode(edge.sourceId);
            int tgtIdx = LookupNode(edge.targetId);

            bool hovered  = (oi == hoveredEdgeIdx);
            bool selected = (oi == selectedEdgeIdx);

            // P11 — does this edge touch the active (selected/hovered) node?
            bool connectedHighlight = style.highlightConnectedEdges
                                      && activeNodeIdx >= 0
                                      && (srcIdx == activeNodeIdx || tgtIdx == activeNodeIdx);

            // P7 — self-loop
            if (srcIdx >= 0 && srcIdx == tgtIdx) {
                DrawSelfLoop(ctx, edge, srcIdx, hovered, selected, connectedHighlight);
                continue;
            }

            if (srcIdx < 0 || tgtIdx < 0) continue;

            bool above = EdgeSideIsAbove(edge, srcIdx, tgtIdx);

            // P8 — bundle offset for parallel edges on same side
            int loKey = std::min(srcIdx, tgtIdx);
            int hiKey = std::max(srcIdx, tgtIdx);
            auto bKey = std::make_tuple(loKey, hiKey, above);
            int offset = bundleCount[bKey]++;

            DrawArc(ctx, edge, srcIdx, tgtIdx, hovered, selected, connectedHighlight, offset);
        }
    }

    void UltraCanvasArcDiagram::DrawNodes(IRenderContext* ctx) const {
        int n = static_cast<int>(nodes.size());
        for (int i = 0; i < n; ++i) {
            const ArcNode& node = nodes[i];
            float pos = NodeScreenPos(i);
            float cx, cy;
            BaselinePoint(pos, cx, cy);

            float r = NodeRadius(i);   // P1 — unified radius

            Color fillCol = node.color;
            if (i == selectedNodeIdx) fillCol = style.nodeSelectedColor;
            else if (i == hoveredNodeIdx) fillCol = style.nodeHoverColor;

            ctx->SetFillPaint(fillCol);
            ctx->ClearPath();
            ctx->Circle(cx, cy, r);
            ctx->Fill();

            ctx->SetStrokePaint(style.nodeStrokeColor);
            ctx->SetStrokeWidth(style.nodeStrokeWidth);
            ctx->ClearPath();
            ctx->Circle(cx, cy, r);
            ctx->Stroke();
        }
    }

    void UltraCanvasArcDiagram::DrawLabels(IRenderContext* ctx) const {
        int n = static_cast<int>(nodes.size());
        if (n == 0) return;

        ctx->SetFontSize(style.labelFontSize);
        ctx->SetFontFace("Sans", FontWeight::Normal, FontSlant::Normal);
        ctx->SetTextPaint(style.labelColor);

        bool horiz = (style.orientation == ArcOrientation::Horizontal);

        for (int i = 0; i < n; ++i) {
            const ArcNode& node = nodes[i];
            float pos = NodeScreenPos(i);
            float cx, cy;
            BaselinePoint(pos, cx, cy);

            float r = NodeRadius(i);   // P1

            auto dims = ctx->GetTextLineDimensions(node.label);
            int tw = dims.width, th = dims.height;

            if (style.labelSide == ArcLabelSide::Vertical && horiz) {
                // P2 — rotated 90° label descending below baseline
                ctx->PushState();
                ctx->Translate(cx, cy + r + style.labelMargin);
                ctx->Rotate(1.5707963f);    // +90° (text runs downward)
                ctx->DrawText(node.label, Point2Df(0.0f, 0.0f));
                ctx->PopState();
                continue;
            }

            // Determine label side for this node
            bool labelBelow = true;
            switch (style.labelSide) {
                case ArcLabelSide::Above:       labelBelow = false; break;
                case ArcLabelSide::Alternating: labelBelow = (i % 2 == 0); break;
                default:                        labelBelow = true; break;
            }

            if (horiz) {
                float lx = cx - tw * 0.5f;
                float ly = labelBelow
                           ? cy + r + style.labelMargin
                           : cy - r - style.labelMargin - th;
                ctx->DrawText(node.label, Point2Df(lx, ly));
            } else {
                float lx = labelBelow
                           ? cx + r + style.labelMargin
                           : cx - r - style.labelMargin - tw;
                float ly = pos - th * 0.5f;
                ctx->DrawText(node.label, Point2Df(lx, ly));
            }
        }
    }

    // P10 — color legend: colored swatches + labels
    void UltraCanvasArcDiagram::DrawLegend(IRenderContext* ctx) const {
        if (!style.showLegend || style.legendEntries.empty()) return;

        float swatchW = 14.0f;
        float swatchH = 14.0f;
        float gap     = 4.0f;
        float rowH    = swatchH + gap;
        float x       = style.legendX;
        float y       = style.legendY;

        ctx->SetFontSize(style.legendFontSize);
        ctx->SetFontFace("Sans", FontWeight::Normal, FontSlant::Normal);

        for (const auto& entry : style.legendEntries) {
            // Swatch
            ctx->SetFillPaint(entry.color);
            ctx->FillRectangle(Rect2Df(x, y, swatchW, swatchH));
            ctx->SetStrokePaint(Color(120, 120, 120, 180));
            ctx->SetStrokeWidth(0.5f);
            ctx->DrawRectangle(Rect2Df(x, y, swatchW, swatchH));

            // Label
            ctx->SetTextPaint(style.legendTextColor);
            auto dims = ctx->GetTextLineDimensions(entry.label);
            double tw = dims.width, th = dims.height;
            ctx->DrawText(entry.label,
                          Point2Df(x + swatchW + gap, y + swatchH * 0.5f - th * 0.5f));

            y += rowH;
        }
    }

    void UltraCanvasArcDiagram::DrawTooltip(IRenderContext* ctx) const {
        if (!showingTooltip || tooltipText.empty()) return;

        ctx->SetFontSize(style.tooltipFontSize);
        ctx->SetFontFace("Sans", FontWeight::Normal, FontSlant::Normal);

        auto dims = ctx->GetTextLineDimensions(tooltipText);
        int tw = dims.width, th = dims.height;

        float pad  = 6.0f;
        float boxW = tw + pad * 2.0f;
        float boxH = th + pad * 2.0f;

        float bx = tooltipX + 14.0f;
        float by = tooltipY - boxH - 4.0f;
        if (bx + boxW > GetWidth())  bx = tooltipX - boxW - 4.0f;
        if (by < 0)                  by = tooltipY + 14.0f;

        ctx->SetFillPaint(style.tooltipBackground);
        ctx->FillRectangle(Rect2Df(bx, by, boxW, boxH));

        ctx->SetTextPaint(style.tooltipText);
        ctx->DrawText(tooltipText, Point2Df(bx + pad, by + pad));
    }

// ─────────────────────────────────────────────
// RENDER
// ─────────────────────────────────────────────

    void UltraCanvasArcDiagram::Render(IRenderContext* ctx, const Rect2Di& dirtyRect) {
        if (nodes.empty()) return;
        if (needsLayout) {
            ComputeLayout();
            ComputeNodeDegrees();       // P1 — build degree cache after layout
            ComputeTotalEdgeWeight();   // P12 — sum weights for percentage display
        }

        ctx->PushState();

        DrawBaseline(ctx);
        DrawEdges(ctx);
        DrawNodes(ctx);
        DrawLabels(ctx);
        DrawLegend(ctx);            // P10
        DrawTooltip(ctx);

        ctx->PopState();
    }

// ─────────────────────────────────────────────
// HIT TESTING
// ─────────────────────────────────────────────

    int UltraCanvasArcDiagram::HitTestNode(float localX, float localY) const {
        int n = static_cast<int>(nodes.size());
        for (int i = 0; i < n; ++i) {
            float pos = NodeScreenPos(i);
            float cx, cy;
            BaselinePoint(pos, cx, cy);

            float r = NodeRadius(i) + 4.0f;   // P1 + expand hit area
            float dx = localX - cx;
            float dy = localY - cy;
            if (dx * dx + dy * dy <= r * r) return i;
        }
        return -1;
    }

    int UltraCanvasArcDiagram::HitTestEdge(float localX, float localY) const {
        // Test proximity to the arc apex region (±20px around the midpoint of each arc)
        for (int ei = 0; ei < static_cast<int>(edges.size()); ++ei) {
            const ArcEdge& edge = edges[ei];
            int srcIdx = LookupNode(edge.sourceId);
            int tgtIdx = LookupNode(edge.targetId);
            if (srcIdx < 0 || tgtIdx < 0 || srcIdx == tgtIdx) continue;

            bool above = EdgeSideIsAbove(edge, srcIdx, tgtIdx);
            float h = ArcHeight(srcIdx, tgtIdx, 0);
            float cp1x, cp1y, cp2x, cp2y;
            ArcControlPoints(srcIdx, tgtIdx, above, h, cp1x, cp1y, cp2x, cp2y);

            float posS = NodeScreenPos(srcIdx);
            float posT = NodeScreenPos(tgtIdx);
            float sx, sy, tx, ty;
            BaselinePoint(posS, sx, sy);
            BaselinePoint(posT, tx, ty);

            // Sample several points along the bezier curve for hit testing
            const int samples = 12;
            for (int s = 1; s < samples; ++s) {
                float t  = static_cast<float>(s) / static_cast<float>(samples);
                float mt = 1.0f - t;

                // Cubic bezier point
                float bx = mt*mt*mt*sx + 3*mt*mt*t*cp1x + 3*mt*t*t*cp2x + t*t*t*tx;
                float by = mt*mt*mt*sy + 3*mt*mt*t*cp1y + 3*mt*t*t*cp2y + t*t*t*ty;

                float dx = localX - bx;
                float dy = localY - by;
                float hitRadius = ArcWidth(edge.weight) * 0.5f + 6.0f;

                if (dx*dx + dy*dy <= hitRadius*hitRadius) return ei;
            }
        }
        return -1;
    }

// ─────────────────────────────────────────────
// EVENT HANDLING
// ─────────────────────────────────────────────

    bool UltraCanvasArcDiagram::OnEvent(const UCEvent& event) {
        if (nodes.empty()) return false;

        auto bounds = GetLocalBounds();
        float localX = static_cast<float>(event.pointer.x - bounds.x);
        float localY = static_cast<float>(event.pointer.y - bounds.y);

        switch (event.type) {

            case UCEventType::MouseMove: {
                int nodeIdx = HitTestNode(localX, localY);
                int edgeIdx = (nodeIdx < 0) ? HitTestEdge(localX, localY) : -1;

                bool changed = (nodeIdx != hoveredNodeIdx || edgeIdx != hoveredEdgeIdx);
                hoveredNodeIdx = nodeIdx;
                hoveredEdgeIdx = edgeIdx;

                // Tooltip
                showingTooltip = false;
                tooltipText.clear();

                if (style.showTooltip) {
                    if (nodeIdx >= 0) {
                        const ArcNode& n = nodes[nodeIdx];
                        std::ostringstream ss;
                        ss << n.label;
                        if (n.value != 1.0f)
                            ss << " (" << std::fixed << std::setprecision(1) << n.value << ")";
                        tooltipText    = ss.str();
                        tooltipX       = localX;
                        tooltipY       = localY;
                        showingTooltip = true;

                        if (onNodeHover && changed)
                            onNodeHover(nodeIdx, n);

                    } else if (edgeIdx >= 0) {
                        const ArcEdge& e = edges[edgeIdx];
                        int si = LookupNode(e.sourceId);
                        int ti = LookupNode(e.targetId);
                        std::ostringstream ss;
                        ss << (si >= 0 ? nodes[si].label : e.sourceId)
                           << (e.directed ? " \u2192 " : " \u2014 ")
                           << (ti >= 0 ? nodes[ti].label : e.targetId);
                        if (e.weight != 1.0f)
                            ss << " : " << std::fixed << std::setprecision(2) << e.weight;
                        tooltipText    = ss.str();
                        tooltipX       = localX;
                        tooltipY       = localY;
                        showingTooltip = true;

                        if (onEdgeHover && changed)
                            onEdgeHover(edgeIdx, e);
                    }
                }

                if (changed) RequestRedraw();
                return true;
            }

            case UCEventType::MouseLeave: {
                hoveredNodeIdx = hoveredEdgeIdx = -1;
                showingTooltip = false;
                tooltipText.clear();
                RequestRedraw();
                return true;
            }

            case UCEventType::MouseUp: {
                if (event.button != UCMouseButton::Left) return false;

                int nodeIdx = HitTestNode(localX, localY);
                if (nodeIdx >= 0) {
                    selectedNodeIdx = nodeIdx;
                    selectedEdgeIdx = -1;
                    if (onNodeClick) onNodeClick(nodeIdx, nodes[nodeIdx]);
                    RequestRedraw();
                    return true;
                }

                int edgeIdx = HitTestEdge(localX, localY);
                if (edgeIdx >= 0) {
                    selectedEdgeIdx = edgeIdx;
                    selectedNodeIdx = -1;
                    if (onEdgeClick) onEdgeClick(edgeIdx, edges[edgeIdx]);
                    RequestRedraw();
                    return true;
                }

                // Click on empty area — deselect
                if (selectedNodeIdx >= 0 || selectedEdgeIdx >= 0) {
                    selectedNodeIdx = selectedEdgeIdx = -1;
                    RequestRedraw();
                }
                return false;
            }

            default:
                return false;
        }
    }

// ─────────────────────────────────────────────
// UTILITY
// ─────────────────────────────────────────────

    int UltraCanvasArcDiagram::LookupNode(const std::string& id) const {
        for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
            if (nodes[i].id == id) return i;
        }
        return -1;
    }

// ─────────────────────────────────────────────
// FACTORY
// ─────────────────────────────────────────────

    std::shared_ptr<UltraCanvasArcDiagram> CreateArcDiagram(
            const std::string& id,
            long x, long y, long width, long height)
    {
        return std::make_shared<UltraCanvasArcDiagram>(id, x, y, width, height);
    }

} // namespace UltraCanvas