// UltraCanvasDendrogram.cpp
// Interactive dendrogram / phylogenetic tree diagram element
// Version: 1.4.2
// Last Modified: 2026-06-05
// Author: UltraCanvas Framework
//
// Changelog:
//   v1.4.2 (2026-06-05):
//     - Re-fit the tree layout when the element is resized. Render() now compares
//       the current width/height to the size used by the last RebuildLayout() and
//       flips layoutDirty on a change, so a flex/stretch parent (which resizes us
//       via SetBounds without touching layoutDirty) makes the diagram reflow.

#include "Plugins/Diagrams/UltraCanvasDendrogram.h"
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace UltraCanvas {

static constexpr float kPi = 3.14159265f;

// =============================================================================
// CONSTRUCTOR
// =============================================================================

    UltraCanvasDendrogram::UltraCanvasDendrogram(
            const std::string& id,
            int x, int y, int width, int height)
        : UltraCanvasUIElement(id, x, y, width, height)
    {
        vertScrollbar = std::make_shared<UltraCanvasScrollbar>(
            id + "_vsb", 0, 0, 12, 200, ScrollbarOrientation::Vertical);
        horzScrollbar = std::make_shared<UltraCanvasScrollbar>(
            id + "_hsb", 0, 0, 200, 12, ScrollbarOrientation::Horizontal);
    }

// =============================================================================
// DATA
// =============================================================================

    void UltraCanvasDendrogram::SetDataSource(std::shared_ptr<IDendrogramDataSource> data)
    {
        dataSource  = data;
        layoutDirty = true;
        selectedLeafIds.clear();
        hoveredNodeId.clear();
        hoveredGroupId.clear();
        scrollX = scrollY = 0;
        zoom = 1.0f;
        panOffset = {0.0f, 0.0f};

        BuildLeafGroupMap();
        RequestRedraw();
    }

    void UltraCanvasDendrogram::BuildLeafGroupMap()
    {
        leafGroupMap.clear();
        if (!dataSource) return;

        for (size_t gi = 0; gi < dataSource->GetGroupCount(); ++gi) {
            const DendrogramGroup* g = dataSource->GetGroup(gi);
            if (!g) continue;
            for (const auto& leafId : g->leafIds) {
                leafGroupMap[leafId] = g->groupId;
            }
        }
    }

// =============================================================================
// LAYOUT CONTROL
// =============================================================================

    void UltraCanvasDendrogram::SetOrientation(DendrogramOrientation o)
    {
        orientation = o;
        layoutDirty = true;
        RequestRedraw();
    }

    void UltraCanvasDendrogram::SetScaleMode(DendrogramScaleMode m)
    {
        scaleMode   = m;
        layoutDirty = true;
        RequestRedraw();
    }

    void UltraCanvasDendrogram::SetLeafSpacing(double px)
    {
        style.leafSpacing = px;
        layoutDirty = true;
        RequestRedraw();
    }

// =============================================================================
// STYLE
// =============================================================================

    void UltraCanvasDendrogram::SetStyle(const DendrogramStyle& s)
    {
        style       = s;
        layoutDirty = true;
        RequestRedraw();
    }

    void UltraCanvasDendrogram::SetBranchColorMode(BranchColorMode m)
    {
        style.branchColorMode = m;
        RequestRedraw();
    }

    void UltraCanvasDendrogram::SetLinkStyle(DendrogramLinkStyle l)
    {
        style.linkStyle = l;
        RequestRedraw();
    }

    void UltraCanvasDendrogram::SetConfidenceMode(ConfidenceDisplayMode m)
    {
        style.confidenceMode = m;
        RequestRedraw();
    }

// =============================================================================
// SELECTION
// =============================================================================

    void UltraCanvasDendrogram::SelectNode(const std::string& nodeId, bool addToSelection)
    {
        if (!addToSelection) selectedLeafIds.clear();

        const DendrogramLayoutNode* n = layout.FindNode(nodeId);
        if (!n) return;

        if (n->isLeaf) {
            auto it = std::find(selectedLeafIds.begin(), selectedLeafIds.end(), nodeId);
            if (it == selectedLeafIds.end())
                selectedLeafIds.push_back(nodeId);
            else if (addToSelection)
                selectedLeafIds.erase(it);
        } else {
            // Select all leaf descendants
            std::function<void(const std::string&)> selectLeaves = [&](const std::string& id) {
                const DendrogramLayoutNode* ln = layout.FindNode(id);
                if (!ln) return;
                if (ln->isLeaf) {
                    auto it = std::find(selectedLeafIds.begin(), selectedLeafIds.end(), id);
                    if (it == selectedLeafIds.end())
                        selectedLeafIds.push_back(id);
                } else {
                    for (const auto& cid : ln->childIds) selectLeaves(cid);
                }
            };
            selectLeaves(nodeId);
        }

        if (onSelectionChanged) onSelectionChanged(selectedLeafIds);
        RequestRedraw();
    }

    void UltraCanvasDendrogram::ClearSelection()
    {
        selectedLeafIds.clear();
        if (onSelectionChanged) onSelectionChanged(selectedLeafIds);
        RequestRedraw();
    }

// =============================================================================
// COLLAPSE / EXPAND
// =============================================================================

    void UltraCanvasDendrogram::ToggleCollapse(const std::string& nodeId)
    {
        // Only internal nodes can be collapsed
        const DendrogramLayoutNode* n = layout.FindNode(nodeId);
        if (!n || n->isLeaf) return;

        auto it = collapsedNodes.find(nodeId);
        bool nowCollapsed = (it == collapsedNodes.end());
        if (nowCollapsed)
            collapsedNodes.insert(nodeId);
        else
            collapsedNodes.erase(it);

        if (onNodeCollapsed) onNodeCollapsed(nodeId, nowCollapsed);
        layoutDirty = true; // Collapsed nodes affect visible leaf set
        RequestRedraw();
    }

    bool UltraCanvasDendrogram::IsCollapsed(const std::string& nodeId) const
    {
        return collapsedNodes.count(nodeId) > 0;
    }

    void UltraCanvasDendrogram::ExpandAll()
    {
        collapsedNodes.clear();
        layoutDirty = true;
        RequestRedraw();
    }

    void UltraCanvasDendrogram::CollapseAll()
    {
        // Collapse all internal nodes that have children (everything except root's direct children)
        for (const auto& node : layout.nodes) {
            if (!node.isLeaf && node.depth > 0)
                collapsedNodes.insert(node.id);
        }
        layoutDirty = true;
        RequestRedraw();
    }

    bool UltraCanvasDendrogram::IsSubtreeVisible(const std::string& nodeId) const
    {
        // Walk up the tree — if any ancestor is collapsed, this node is hidden
        const DendrogramLayoutNode* n = layout.FindNode(nodeId);
        if (!n) return false;

        // Find parent by scanning all nodes' childIds
        std::function<const DendrogramLayoutNode*(const std::string&)> findParent;
        findParent = [&](const std::string& id) -> const DendrogramLayoutNode* {
            for (const auto& candidate : layout.nodes) {
                for (const auto& cid : candidate.childIds) {
                    if (cid == id) return &candidate;
                }
            }
            return nullptr;
        };

        const DendrogramLayoutNode* current = n;
        while (current) {
            const DendrogramLayoutNode* parent = findParent(current->id);
            if (!parent) break; // Reached root
            if (collapsedNodes.count(parent->id)) return false;
            current = parent;
        }
        return true;
    }

// =============================================================================
// ZOOM / PAN
// =============================================================================

    void UltraCanvasDendrogram::SetZoom(double z)
    {
        zoom = std::clamp(z, 0.1, 10.0);
        RequestRedraw();
    }

    void UltraCanvasDendrogram::ResetView()
    {
        zoom = 1.0;
        panOffset = {0.0, 0.0};
        scrollX = scrollY = 0;
        RequestRedraw();
    }

// =============================================================================
// LAYOUT REBUILD
// =============================================================================

    void UltraCanvasDendrogram::RebuildLayout()
    {
        if (!dataSource || dataSource->GetNodeCount() == 0) {
            layout = DendrogramLayout();
            return;
        }
        
        // Guard against invalid bounds before layout
        if (GetWidth() <= 0 || GetHeight() <= 0) {
            return;
        }
        
        Rect2Dd bounds = GetTreeBounds();
        if (finalBounds.width <= 0 || finalBounds.height <= 0) {
            return;
        }
        
        layout = layoutEngine.Compute(dataSource.get(), bounds, orientation, scaleMode, style.leafSpacing);
        
        // Apply custom positions if any exist
        for (const auto& [nodeId, pos] : customNodePositions) {
            DendrogramLayoutNode* n = layout.FindNode(nodeId);
            if (n) {
                n->px = pos.x;
                n->py = pos.y;
            }
        }

        lastLayoutWidth  = GetWidth();
        lastLayoutHeight = GetHeight();
        layoutDirty = false;
    }

    void UltraCanvasDendrogram::ClearCustomPositions()
    {
        customNodePositions.clear();
        layoutDirty = true;
        RequestRedraw();
    }

    void UltraCanvasDendrogram::ResetNodePositions()
    {
        customNodePositions.clear();
        layoutDirty = true;
        RequestRedraw();
    }

    Rect2Dd UltraCanvasDendrogram::GetTreeBounds() const
    {
        float x = static_cast<float>(GetX() + style.marginLeft);
        float y = static_cast<float>(GetY() + style.marginTop);
        float w = static_cast<float>(GetWidth()  - style.marginLeft - style.marginRight);
        float h = static_cast<float>(GetHeight() - style.marginTop  - style.marginBottom);

        if (w <= 0 || h <= 0) return Rect2Dd(x, y, 10.0f, 10.0f);

        switch (orientation) {
            case DendrogramOrientation::LeftRight:
            case DendrogramOrientation::RightLeft:
                // Reserve right column for leaf labels
                w -= 130.0f;
                // Reserve sidebar bracket area
                if (style.showGroupLabels && dataSource && dataSource->GetGroupCount() > 0)
                    w -= static_cast<float>(style.sidebarBracketWidth + style.sidebarBracketGap) + 90.0f;
                break;

            case DendrogramOrientation::TopDown:
            case DendrogramOrientation::BottomUp:
                // Reserve bottom for 45° rotated leaf labels (need ~80px at 45°)
                h -= 85.0f;
                // Reserve left column for Y-axis labels
                if (style.showDistanceAxis) {
                    x += 50.0f;
                    w -= 50.0f;
                }
                break;

            case DendrogramOrientation::Radial: {
                // Square centred region — no label reservation needed here (labels outside radius)
                float side = std::min(w, h);
                float cx   = x + w * 0.5f;
                float cy   = y + h * 0.5f;
                x = cx - side * 0.5f;
                y = cy - side * 0.5f;
                w = side;
                h = side;
                break;
            }
        }

        return Rect2Dd(x, y, std::max(10.0f, w), std::max(10.0f, h));
    }

// =============================================================================
// RENDER
// =============================================================================

    void UltraCanvasDendrogram::Render(IRenderContext* ctx, const Rect2Df& dirtyRect)
    {
        if (!IsVisible()) return;

        try {
            // Flex/stretch parents resize us via SetBounds without flipping
            // layoutDirty; re-fit the tree when our size actually changed.
            if (GetWidth() != lastLayoutWidth || GetHeight() != lastLayoutHeight)
                layoutDirty = true;
            if (layoutDirty) RebuildLayout();

            if (!dataSource || dataSource->GetNodeCount() == 0) {
                RenderEmptyState(ctx);
                return;
            }

            ctx->PushState();
            Rect2Di bounds = GetBounds();
            ctx->ClipRect(bounds);

            RenderBackground(ctx);

            ctx->PushState();
            ctx->Translate(panOffset.x - scrollX, panOffset.y - scrollY);
            ctx->Scale(zoom, zoom);

            RenderGroupFills(ctx);
            RenderAxis(ctx);
            RenderBranches(ctx);
            RenderNodeDots(ctx);
            RenderLeafLabels(ctx);
            RenderInternalNodeLabels(ctx);
            RenderNodeAnnotations(ctx);

            ctx->PopState(); // zoom/pan

            RenderGroupLabels(ctx); // After zoom pop — labels are in screen space
            RenderScrollbars(ctx);
            ctx->PopState(); // clip
        } catch (const std::exception& e) {
            // Silently ignore render errors to prevent crashes
        } catch (...) {
            // Silently ignore any errors
        }
    }

// =============================================================================
// RENDER — BACKGROUND
// =============================================================================

    void UltraCanvasDendrogram::RenderBackground(IRenderContext* ctx)
    {
        Rect2Di b = GetLocalBounds();
        ctx->SetFillPaint(style.backgroundColor);
        ctx->FillRectangle(Rect2Dd(b.x, b.y, b.width, b.height));
    }

    void UltraCanvasDendrogram::RenderEmptyState(IRenderContext* ctx)
    {
        Rect2Di b = GetLocalBounds();
        ctx->SetTextPaint(Color(160, 160, 160, 255));
        ctx->SetFontFace("sans-serif", FontWeight::Normal, FontSlant::Normal);
        ctx->SetFontSize(13.0f);
        int tw = ctx->GetTextLineWidth("No data — call SetDataSource()");
        ctx->DrawText("No data — call SetDataSource()",
                      {static_cast<double>(b.x + (b.width - tw) / 2),
                       static_cast<double>(b.y + b.height / 2)});
    }

// =============================================================================
// RENDER — GROUP FILLS (Strategy 2)
// =============================================================================

    void UltraCanvasDendrogram::RenderGroupFills(IRenderContext* ctx)
    {
        if (!dataSource || !style.showGroupLabels) return;

        for (size_t gi = 0; gi < dataSource->GetGroupCount(); ++gi) {
            const DendrogramGroup* g = dataSource->GetGroup(gi);
            if (!g || g->fillMode == GroupFillMode::None) continue;

            // Collect pixel positions of all leaves in this group
            float minPrimary = 1e9f, maxPrimary = -1e9f;
            float leafDepth  = 0.0f; // All leaves share same depth in proportional

            bool any = false;
            for (const auto& leafId : g->leafIds) {
                const DendrogramLayoutNode* n = layout.FindNode(leafId);
                if (!n || !n->isLeaf) continue;
                any = true;
                if (orientation == DendrogramOrientation::LeftRight ||
                    orientation == DendrogramOrientation::RightLeft) {
                    minPrimary = std::min(minPrimary, n->py);
                    maxPrimary = std::max(maxPrimary, n->py);
                    leafDepth  = n->px;
                } else {
                    minPrimary = std::min(minPrimary, n->px);
                    maxPrimary = std::max(maxPrimary, n->px);
                    leafDepth  = n->py;
                }
            }
            if (!any) continue;

            Color fill = g->fillColor;
            fill.a = static_cast<uint8_t>(g->fillOpacity * 255.0f);
            ctx->SetFillPaint(fill);

            if (g->fillMode == GroupFillMode::BandFill) {
                float pad = 3.0f;
                if (orientation == DendrogramOrientation::LeftRight) {
                    Rect2Dd treeBounds = GetTreeBounds();
                    ctx->FillRectangle(Rect2Dd(
                        treeBounds.x,
                        minPrimary - pad,
                        leafDepth - treeBounds.x + 120,
                        maxPrimary - minPrimary + pad * 2.0f));
                } else if (orientation == DendrogramOrientation::TopDown) {
                    Rect2Dd treeBounds = GetTreeBounds();
                    ctx->FillRectangle(Rect2Dd(
                        minPrimary - pad,
                        treeBounds.y,
                        maxPrimary - minPrimary + pad * 2.0f,
                        leafDepth - treeBounds.y + 20));
                }
            } else if (g->fillMode == GroupFillMode::SectorFill &&
                       orientation == DendrogramOrientation::Radial) {
                // Find angle range of group leaves
                float minAngle = 1e9f, maxAngle = -1e9f;
                for (const auto& leafId : g->leafIds) {
                    const DendrogramLayoutNode* n = layout.FindNode(leafId);
                    if (!n) continue;
                    minAngle = std::min(minAngle, n->angle);
                    maxAngle = std::max(maxAngle, n->angle);
                }
                if (minAngle > maxAngle) continue;

                Rect2Dd treeBounds = GetTreeBounds();
                float cx = treeBounds.x + treeBounds.width  * 0.5f;
                float cy = treeBounds.y + treeBounds.height * 0.5f;
                float maxR = std::min(treeBounds.width, treeBounds.height) * 0.5f;

                // Draw sector as path
                ctx->MoveTo(cx, cy);
                int steps = std::max(12, static_cast<int>((maxAngle - minAngle) / (kPi / 36.0f)));
                float step = (maxAngle - minAngle) / steps;
                for (int s = 0; s <= steps; ++s) {
                    float a = minAngle + s * step;
                    ctx->LineTo(cx + std::cos(a) * maxR * 1.05f,
                                cy + std::sin(a) * maxR * 1.05f);
                }
                ctx->LineTo(cx, cy);
                ctx->FillPathPreserve();
                ctx->ClearPath();
            }
        }
    }

// =============================================================================
// RENDER — DISTANCE AXIS
// =============================================================================

    void UltraCanvasDendrogram::RenderAxis(IRenderContext* ctx)
    {
        if (!style.showDistanceAxis) return;
        if (layout.maxDistance <= 0.0f) return;
        if (orientation == DendrogramOrientation::Radial) return;

        Rect2Dd tb = GetTreeBounds();

        ctx->SetFontFace("sans-serif", FontWeight::Normal, FontSlant::Normal);
        ctx->SetFontSize(style.axisFontSize);

        int ticks = style.axisTickCount;

        // In LeftRight: depthPos=0 → px = tb.x + (1-0)*depthScale = tb.x+tb.width (root, left)
        //               depthPos=1 → px = tb.x + (1-1)*depthScale = tb.x         (leaves, right)
        //   Wait — actually SecondWalk: px = originX + (1-depthPos)*depthScale
        //   depthPos = mergeDistance/maxDistance (0 for root, 1 for leaves conceptually)
        //   But leaves have mergeDistance=0 so depthPos=0 too → px = originX + depthScale (RIGHT)
        //   Root has highest mergeDistance → depthPos→1 → px = originX + 0 = LEFT
        // So for LeftRight: left = root (high dist), right = leaves (dist=0)
        // Tick at t: tickX = tb.x + tb.width*(1-t), dist = maxDistance*t (t=0→right,dist=0)

        for (int i = 0; i <= ticks; ++i) {
            float t    = static_cast<float>(i) / static_cast<float>(ticks);
            double dist = layout.maxDistance * t; // t=0 → 0 (leaves), t=1 → maxDist (root)

            std::ostringstream ss;
            ss << std::fixed << std::setprecision(2) << dist;
            std::string label = ss.str();

            if (orientation == DendrogramOrientation::LeftRight) {
                // depthPos = dist/maxDist; px = tb.x + (1-depthPos)*tb.width
                float tickX = tb.x + (1.0f - t) * tb.width;
                float tickY = tb.y + tb.height + 8.0f;

                if (style.showGrid) {
                    ctx->SetStrokePaint(style.gridColor);
                    ctx->SetStrokeWidth(0.5f);
                    ctx->DrawLine({tickX, tb.y}, {tickX, tb.y + tb.height});
                }
                // Axis baseline
                ctx->SetStrokePaint(style.axisColor);
                ctx->SetStrokeWidth(0.5f);
                ctx->DrawLine({tickX, tb.y + tb.height}, {tickX, tb.y + tb.height + 5.0f});

                ctx->SetTextPaint(style.axisColor);
                int tw = ctx->GetTextLineWidth(label);
                ctx->DrawText(label, {tickX - tw * 0.5f, tickY + style.axisFontSize});

            } else if (orientation == DendrogramOrientation::TopDown) {
                // In TopDown: root at top (depthPos=1 → py=top), leaves at bottom (depthPos=0)
                // depthPos = mergeDistance/maxDistance; py = originY + (1-depthPos)*depthScale
                // t=0 → dist=0, depthPos=0 → py = originY+depthScale = BOTTOM
                // t=1 → dist=maxDist, depthPos=1 → py = originY = TOP
                float tickY = tb.y + (1.0f - t) * tb.height;
                float tickX = tb.x - 42.0f;

                if (style.showGrid) {
                    ctx->SetStrokePaint(style.gridColor);
                    ctx->SetStrokeWidth(0.5f);
                    ctx->DrawLine({tb.x, tickY}, {tb.x + tb.width, tickY});
                }
                ctx->SetStrokePaint(style.axisColor);
                ctx->SetStrokeWidth(0.5f);
                ctx->DrawLine({tb.x - 5.0f, tickY}, {tb.x, tickY});

                ctx->SetTextPaint(style.axisColor);
                int tw = ctx->GetTextLineWidth(label);
                ctx->DrawText(label, {tickX - tw, tickY + style.axisFontSize * 0.4f});
            }
        }

        // Draw axis baseline line
        ctx->SetStrokePaint(style.axisColor);
        ctx->SetStrokeWidth(0.8f);
        if (orientation == DendrogramOrientation::LeftRight) {
            ctx->DrawLine({tb.x, tb.y + tb.height}, {tb.x + tb.width, tb.y + tb.height});
        } else if (orientation == DendrogramOrientation::TopDown) {
            ctx->DrawLine({tb.x - 5.0f, tb.y}, {tb.x - 5.0f, tb.y + tb.height});
        }
    }

// =============================================================================
// RENDER — BRANCHES
// =============================================================================

    void UltraCanvasDendrogram::RenderBranches(IRenderContext* ctx)
    {
        for (const auto& node : layout.nodes) {
            if (node.isLeaf) continue;

            // Skip collapsed subtrees (the parent node itself is still drawn)
            if (!IsSubtreeVisible(node.id) && node.depth > 0) continue;

            const DendrogramLayoutNode* parent = &node;

            for (const auto& childId : node.childIds) {
                // If this node is collapsed, don't draw its children's branches
                if (collapsedNodes.count(node.id)) continue;

                const DendrogramLayoutNode* child = layout.FindNode(childId);
                if (!child) continue;

                Color branchColor = ResolveBranchColor(*child);
                float branchWidth = DefaultBranchWidth();
                bool  dashed      = false;

                float conf = 1.0f;
                if (dataSource) {
                    const DendrogramNode* src = dataSource->GetNodeById(child->id);
                    if (src) conf = src->confidence;
                }
                branchWidth = ComputeBranchWidth(conf);
                dashed      = ComputeBranchDash(conf);

                // Non-radial hover highlight
                bool isHovered = (!hoveredBranchParentId.empty() &&
                                  parent->id == hoveredBranchParentId &&
                                  orientation != DendrogramOrientation::Radial);
                if (isHovered) {
                    branchWidth *= 2.2f;
                    branchColor = Color(
                        std::min(255, static_cast<int>(branchColor.r) + 60),
                        std::min(255, static_cast<int>(branchColor.g) + 60),
                        std::min(255, static_cast<int>(branchColor.b) + 60),
                        branchColor.a);
                }

                DrawBranch(ctx, *parent, *child, branchColor, branchWidth, dashed);
            }
        }
    }

    // Wrapper resolving per-orientation crossbar
    void UltraCanvasDendrogram::DrawBranch(IRenderContext* ctx,
            const DendrogramLayoutNode& parent,
            const DendrogramLayoutNode& child,
            const Color& color, float width, bool dashed)
    {
        if (style.linkStyle == DendrogramLinkStyle::Curved) {
            if (orientation == DendrogramOrientation::Radial) {
                DrawBranchCurvedRadial(ctx, parent, child, color, width);
            } else {
                DrawBranchCurved(ctx, parent.px, parent.py, child.px, child.py, color, width);
            }
        } else if (style.linkStyle == DendrogramLinkStyle::Diagonal) {
            DrawBranchDiagonal(ctx, parent.px, parent.py, child.px, child.py, color, width);
        } else {
            if (orientation == DendrogramOrientation::LeftRight ||
                orientation == DendrogramOrientation::RightLeft) {
                DrawBranchRectangular(ctx,
                    parent.px, parent.py,
                    child.px,  child.py,
                    parent.py,
                    color, width, dashed);
            } else if (orientation == DendrogramOrientation::Radial) {
                // Radial rectangular: straight line from parent to child
                DrawBranchDiagonal(ctx, parent.px, parent.py, child.px, child.py, color, width);
            } else {
                DrawBranchRectangular(ctx,
                    parent.px, parent.py,
                    child.px,  child.py,
                    parent.px,
                    color, width, dashed);
            }
        }
    }

    void UltraCanvasDendrogram::DrawBranchRectangular(IRenderContext* ctx,
            float px, float py, float cx, float cy,
            float crossbar,
            const Color& color, float width, bool dashed)
    {
        ctx->SetStrokePaint(color);
        ctx->SetStrokeWidth(width);

        if (dashed) {
            // Dashed line: draw short segments manually
            auto drawDashedLine = [&](float x1, float y1, float x2, float y2) {
                float dx = x2 - x1, dy = y2 - y1;
                float len = std::sqrt(dx * dx + dy * dy);
                if (len < 1.0f) return;
                float nx = dx / len, ny = dy / len;
                float dashLen = 4.0f, gapLen = 3.0f;
                float t = 0.0f;
                bool drawing = true;
                while (t < len) {
                    float segLen = drawing ? dashLen : gapLen;
                    float tend   = std::min(t + segLen, len);
                    if (drawing) {
                        ctx->DrawLine({x1 + nx * t, y1 + ny * t},
                                      {x1 + nx * tend, y1 + ny * tend});
                    }
                    t += segLen;
                    drawing = !drawing;
                }
            };

            if (orientation == DendrogramOrientation::LeftRight ||
                orientation == DendrogramOrientation::RightLeft) {
                // Horizontal to child Y, then vertical to child X
                drawDashedLine(px, crossbar, cx, crossbar);
                drawDashedLine(cx, crossbar, cx, cy);
            } else {
                drawDashedLine(crossbar, py, crossbar, cy);
                drawDashedLine(crossbar, cy, cx, cy);
            }
        } else {
            if (orientation == DendrogramOrientation::LeftRight ||
                orientation == DendrogramOrientation::RightLeft) {
                ctx->DrawLine({px, crossbar}, {cx, crossbar}); // horizontal crossbar
                ctx->DrawLine({cx, crossbar}, {cx, cy});       // vertical stem to child
            } else {
                ctx->DrawLine({crossbar, py}, {crossbar, cy}); // vertical crossbar
                ctx->DrawLine({crossbar, cy}, {cx, cy});       // horizontal stem to child
            }
        }
    }

    void UltraCanvasDendrogram::DrawBranchCurved(IRenderContext* ctx,
            float px, float py, float cx, float cy,
            const Color& color, float width)
    {
        ctx->SetStrokePaint(color);
        ctx->SetStrokeWidth(width);
        ctx->ClearPath();

        float midX = (px + cx) * 0.5f;
        float midY = (py + cy) * 0.5f;

        ctx->MoveTo(px, py);

        if (orientation == DendrogramOrientation::LeftRight ||
            orientation == DendrogramOrientation::RightLeft) {
            // S-curve: control points pull horizontally from each end
            ctx->BezierCurveTo(midX, py, midX, cy, cx, cy);
        } else {
            // TopDown / BottomUp: control points pull vertically
            ctx->BezierCurveTo(px, midY, cx, midY, cx, cy);
        }

        ctx->StrokePathPreserve();
        ctx->ClearPath();
    }

    void UltraCanvasDendrogram::DrawBranchCurvedRadial(IRenderContext* ctx,
            const DendrogramLayoutNode& parent,
            const DendrogramLayoutNode& child,
            const Color& color, float width)
    {
        ctx->SetStrokeWidth(width);
        ctx->ClearPath();

        // Use layout origin + half dimensions — these are in pre-zoom layout space,
        // which is correct because we're already inside the zoom/pan PushState.
        float centreX = layout.originX + layout.totalWidth  * 0.5f;
        float centreY = layout.originY + layout.totalHeight * 0.5f;

        float px = parent.px;
        float py = parent.py;
        float cx = child.px;
        float cy = child.py;

        // Radii from layout centre
        float parentR = std::sqrt((px - centreX) * (px - centreX) +
                                   (py - centreY) * (py - centreY));
        float childR  = std::sqrt((cx - centreX) * (cx - centreX) +
                                   (cy - centreY) * (cy - centreY));

        float parentAngle = std::atan2(py - centreY, px - centreX);
        float childAngle  = std::atan2(cy - centreY, cx - centreX);

        // Wrap angle delta to [-pi, pi] to avoid long-way-round sweeps
        float dA = childAngle - parentAngle;
        while (dA >  kPi) dA -= 2.0f * kPi;
        while (dA < -kPi) dA += 2.0f * kPi;

        float cp1Angle = parentAngle + dA * 0.33f;
        float cp2Angle = parentAngle + dA * 0.67f;

        float cp1x = centreX + std::cos(cp1Angle) * parentR;
        float cp1y = centreY + std::sin(cp1Angle) * parentR;
        float cp2x = centreX + std::cos(cp2Angle) * childR;
        float cp2y = centreY + std::sin(cp2Angle) * childR;

        // Hover highlight: widen and brighten the hovered branch
        bool isHovered = (!hoveredBranchParentId.empty() && parent.id == hoveredBranchParentId);
        Color drawColor = color;
        float drawWidth = width;
        if (isHovered) {
            drawWidth  = width * 2.2f;
            drawColor  = Color(
                std::min(255, static_cast<int>(color.r) + 60),
                std::min(255, static_cast<int>(color.g) + 60),
                std::min(255, static_cast<int>(color.b) + 60),
                color.a);
        }
        ctx->SetStrokePaint(drawColor);
        ctx->SetStrokeWidth(drawWidth);

        ctx->MoveTo(px, py);
        ctx->BezierCurveTo(cp1x, cp1y, cp2x, cp2y, cx, cy);
        ctx->StrokePathPreserve();
        ctx->ClearPath();
    }

    void UltraCanvasDendrogram::DrawBranchDiagonal(IRenderContext* ctx,
            float px, float py, float cx, float cy,
            const Color& color, float width)
    {
        ctx->SetStrokePaint(color);
        ctx->SetStrokeWidth(width);
        ctx->DrawLine({px, py}, {cx, cy});
    }

// =============================================================================
// RENDER — NODE DOTS
// =============================================================================

    void UltraCanvasDendrogram::RenderNodeDots(IRenderContext* ctx)
    {
        for (const auto& node : layout.nodes) {
            // Skip entirely hidden nodes
            if (!IsSubtreeVisible(node.id) && node.depth > 0) continue;

            if (node.isLeaf) {
                if (!style.showLeafNodes) continue;

                Color c = style.colorLeafNodesByGroup
                    ? ResolveNodeColor(node)
                    : ResolveBranchColor(node);

                // Hover ring on hovered leaf
                if (node.id == hoveredNodeId) {
                    ctx->DrawFilledCircle(Point2Dd(node.px, node.py),
                        style.leafNodeRadius + 2.5f,
                        Color(c.r, c.g, c.b, 80));
                }
                ctx->DrawFilledCircle(Point2Dd(node.px, node.py),
                                      style.leafNodeRadius, c);

            } else {
                if (!style.showInternalNodes) continue;

                float conf = 1.0f;
                if (dataSource) {
                    const DendrogramNode* src = dataSource->GetNodeById(node.id);
                    if (src) conf = src->confidence;
                }

                float radius = ResolveNodeRadius(node);
                Color color  = ResolveNodeColor(node);

                // Hover ring on hovered internal node
                if (node.id == hoveredNodeId) {
                    ctx->DrawFilledCircle(Point2Dd(node.px, node.py),
                        radius + 3.0f,
                        Color(color.r, color.g, color.b, 80));
                }

                // Collapsed indicator: draw a filled diamond instead of circle
                if (collapsedNodes.count(node.id)) {
                    float d = radius * 1.6f;
                    Color collapseColor = Color(
                        std::min(255, static_cast<int>(color.r) + 40),
                        std::min(255, static_cast<int>(color.g) + 40),
                        std::min(255, static_cast<int>(color.b) + 40),
                        255);
                    ctx->ClearPath();
                    ctx->MoveTo(node.px,     node.py - d);  // top
                    ctx->LineTo(node.px + d, node.py);       // right
                    ctx->LineTo(node.px,     node.py + d);   // bottom
                    ctx->LineTo(node.px - d, node.py);       // left
                    ctx->ClosePath();
                    ctx->SetFillPaint(collapseColor);
                    ctx->FillPathPreserve();
                    ctx->ClearPath();
                    continue; // Skip normal dot
                }

                bool showDot = (style.confidenceMode == ConfidenceDisplayMode::NodeDot ||
                                style.confidenceMode == ConfidenceDisplayMode::LineThicknessAndDot);

                if (showDot) {
                    bool filled = (conf >= style.confidenceHighThreshold);
                    if (filled) {
                        ctx->DrawFilledCircle(Point2Dd(node.px, node.py), radius, color);
                    } else {
                        ctx->DrawFilledCircle(Point2Dd(node.px, node.py),
                                              radius, Colors::Transparent, color, 1.0f);
                    }
                } else {
                    ctx->DrawFilledCircle(Point2Dd(node.px, node.py), radius, color);
                }
            }
        }
    }

// =============================================================================
// RENDER — LEAF LABELS
// =============================================================================

    void UltraCanvasDendrogram::RenderLeafLabels(IRenderContext* ctx)
    {
        ctx->SetFontFace("sans-serif", FontWeight::Normal, FontSlant::Normal);
        ctx->SetFontSize(style.leafLabelFontSize);

        for (const auto& node : layout.nodes) {
            if (!node.isLeaf) continue;

            // Resolve label color
            Color labelColor = style.defaultLeafColor;
            if (style.colorLeafLabels) {
                const std::string* gid = nullptr;
                auto it = leafGroupMap.find(node.id);
                if (it != leafGroupMap.end()) gid = &it->second;

                if (gid && dataSource) {
                    const DendrogramGroup* g = dataSource->GetGroupById(*gid);
                    if (g) labelColor = g->labelColor;
                }
            }

            // Highlight selected leaves
            bool selected = std::find(selectedLeafIds.begin(), selectedLeafIds.end(), node.id)
                            != selectedLeafIds.end();
            if (selected) {
                labelColor = Color(
                    std::min(255, static_cast<int>(labelColor.r) + 40),
                    std::min(255, static_cast<int>(labelColor.g) + 40),
                    static_cast<uint8_t>(labelColor.b),
                    labelColor.a);
                ctx->SetFontFace("sans-serif", FontWeight::Bold, FontSlant::Normal);
            } else {
                ctx->SetFontFace("sans-serif", FontWeight::Normal, FontSlant::Normal);
            }

            ctx->SetTextPaint(labelColor);

            // Fetch label from data source
            std::string label = node.id;
            if (dataSource) {
                const DendrogramNode* src = dataSource->GetNodeById(node.id);
                if (src && !src->label.empty()) label = src->label;
            }

            // Skip nodes hidden by collapse
            if (!IsSubtreeVisible(node.id)) continue;

            float lx = node.px, ly = node.py;

            if (orientation == DendrogramOrientation::LeftRight) {
                lx = node.px + style.leafLabelPadding;
                ly = node.py + style.leafLabelFontSize * 0.35f;
            } else if (orientation == DendrogramOrientation::RightLeft) {
                int tw = ctx->GetTextLineWidth(label);
                lx = node.px - style.leafLabelPadding - tw;
                ly = node.py + style.leafLabelFontSize * 0.35f;
            } else if (orientation == DendrogramOrientation::TopDown) {
                // Rotate labels 45° to prevent overlap — standard in all dendrogram tools
                ctx->PushState();
                ctx->Translate(node.px, node.py + style.leafLabelPadding + 2.0f);
                ctx->Rotate(kPi * 0.25f); // 45 degrees clockwise
                ctx->DrawText(label, {0.0, style.leafLabelFontSize * 0.35});
                ctx->PopState();
                continue;
            } else if (orientation == DendrogramOrientation::BottomUp) {
                // Rotate labels -45° (upward-right)
                ctx->PushState();
                ctx->Translate(node.px, node.py - style.leafLabelPadding - 2.0f);
                ctx->Rotate(-kPi * 0.25f); // 45 degrees counter-clockwise
                int tw = ctx->GetTextLineWidth(label);
                ctx->DrawText(label, {static_cast<double>(-tw), style.leafLabelFontSize * 0.35});
                ctx->PopState();
                continue;
            } else if (orientation == DendrogramOrientation::Radial) {
                // Radial label WITH CONNECTING LINE to node (colored branch extension)
                float angle = node.angle;
                bool rightHalf = (angle > -kPi * 0.5f && angle < kPi * 0.5f);
                
                int tw = ctx->GetTextLineWidth(label);
                int th = static_cast<int>(style.leafLabelFontSize);
                float baseOffset = static_cast<float>(style.leafLabelPadding + 2);
                float connectLen = 12.0f; // Length of connecting line
                
                // Check for label collision
                bool hasCollision = false;
                for (const auto& other : layout.nodes) {
                    if (!other.isLeaf || other.id == node.id) continue;
                    float dist = std::sqrt((other.px - node.px)*(other.px - node.px) + 
                                           (other.py - node.py)*(other.py - node.py));
                    if (dist < 15.0f) {
                        hasCollision = true;
                        break;
                    }
                }
                
                // Hide when zoomed too far out
                if (zoom < 0.25f) {
                    continue;
                }
                
                // Calculate label position with connector
                ctx->PushState();
                ctx->Translate(node.px, node.py);
                
                float rot = rightHalf ? angle : (angle + kPi);
                ctx->Rotate(rot);
                
                // Draw connecting line (branch extension) in group color
                float lineEndX = rightHalf ? connectLen : -connectLen;
                ctx->SetStrokePaint(labelColor);
                ctx->SetStrokeWidth(1.2f);
                ctx->DrawLine({0.0, style.leafLabelFontSize * 0.3}, {lineEndX, style.leafLabelFontSize * 0.3});
                
                // Position text at end of connector
                float textX = rightHalf ? connectLen + 3 : -(tw + connectLen + 3);
                if (hasCollision && rightHalf) {
                    textX = -baseOffset - tw - 5;
                }
                
                // Dim label based on zoom level
                float labelOpacity = (zoom > 0.5f) ? 1.0f : (zoom > 0.3f ? 0.8f : 0.5f);
                Color adjustedColor = labelColor;
                adjustedColor.a = static_cast<uint8_t>(255 * labelOpacity);
                ctx->SetTextPaint(adjustedColor);
                
                ctx->DrawText(label, {textX, style.leafLabelFontSize * 0.35});
                ctx->PopState();
                continue;
            }

            ctx->DrawText(label, {static_cast<double>(lx), static_cast<double>(ly)});
        }
    }

// =============================================================================
// RENDER — INTERNAL NODE CLADE LABELS
// Modern smart label placement with visual enhancements
// =============================================================================

    void UltraCanvasDendrogram::RenderInternalNodeLabels(IRenderContext* ctx)
    {
        if (!style.showInternalNodeLabels || !dataSource) return;
        if (layout.nodes.empty()) return;

        ctx->SetFontFace("sans-serif", FontWeight::Normal, FontSlant::Normal);
        ctx->SetFontSize(style.internalNodeLabelFontSize);

        int pad = style.labelBackgroundPadding;
        int cornerRadius = static_cast<int>(style.labelBackgroundRadius);

        for (const auto& node : layout.nodes) {
            if (node.isLeaf) continue;
            if (!IsSubtreeVisible(node.id) && node.depth > 0) continue;
            if (node.depth == 0) continue; // Skip root

            // Skip if label should only show on click and this node isn't selected
            if (style.showLabelsOnClick && visibleBranchLabels.count(node.id) == 0) continue;

            // Get the label from data source
            std::string label = node.id;
            if (dataSource) {
                const DendrogramNode* src = dataSource->GetNodeById(node.id);
                if (src && !src->label.empty()) label = src->label;
            }
            if (label.empty()) continue;

            int tw = ctx->GetTextLineWidth(label);
            int th = static_cast<int>(style.internalNodeLabelFontSize);
            int off = style.internalNodeLabelOffset;

            // Find parent node position to determine branch direction
            float parentX = node.px;
            float parentY = node.py;
            for (const auto& candidate : layout.nodes) {
                for (const auto& childId : candidate.childIds) {
                    if (childId == node.id) {
                        parentX = candidate.px;
                        parentY = candidate.py;
                        break;
                    }
                }
            }

            // Smart label positioning with multiple candidates
            // Evaluate ALL positions and pick the best one that avoids overlap
            std::vector<std::array<float, 3>> candidates; // {x, y, priority}
            
            switch (orientation) {
                case DendrogramOrientation::TopDown:
                case DendrogramOrientation::BottomUp:
                    // Vertical branches: place labels horizontally (left side preferred)
                    candidates = {
                        {static_cast<float>(node.px - tw - off - 8), static_cast<float>(node.py - th * 0.3f), 1.0f},  // left, preferred
                        {static_cast<float>(node.px + off + 8), static_cast<float>(node.py - th * 0.3f), 0.8f},       // right, fallback
                        {static_cast<float>(node.px - tw - off - 8), static_cast<float>(node.py + th * 0.5f), 0.6f},  // left-down
                    };
                    break;
                case DendrogramOrientation::LeftRight:
                    // Horizontal branches: place labels above, below, OR on the left
                    candidates = {
                        {static_cast<float>(node.px - tw - off - 12), static_cast<float>(node.py - th * 0.3f), 1.0f}, // left, best for readability
                        {static_cast<float>(node.px + off + 8), static_cast<float>(node.py - th - off - 10), 0.9f},   // right-above
                        {static_cast<float>(node.px + off + 8), static_cast<float>(node.py + th * 0.5f + off), 0.7f}, // right-below
                        {static_cast<float>(node.px - tw * 0.5f - off), static_cast<float>(node.py - th - off - 8), 0.5f}, // center-above
                    };
                    break;
                case DendrogramOrientation::RightLeft:
                    candidates = {
                        {static_cast<float>(node.px + off + 8), static_cast<float>(node.py - th * 0.3f), 1.0f},
                        {static_cast<float>(node.px - tw - off - 12), static_cast<float>(node.py - th - off - 10), 0.8f},
                        {static_cast<float>(node.px - tw - off - 12), static_cast<float>(node.py + th * 0.5f + off), 0.6f},
                    };
                    break;
                case DendrogramOrientation::Radial:
                    candidates = {
                        {static_cast<float>(node.px + off + 5), static_cast<float>(node.py + th * 0.3f), 1.0f},
                        {static_cast<float>(node.px - tw - off - 8), static_cast<float>(node.py + th * 0.3f), 0.9f},
                        {static_cast<float>(node.px + off + 5), static_cast<float>(node.py - th * 0.5f), 0.7f},
                        {static_cast<float>(node.px - tw - off - 8), static_cast<float>(node.py - th * 0.5f), 0.6f},
                    };
                    break;
            }

            float lx = node.px;
            float ly = node.py;
            float bestScore = -1e9f;

            // First pass: check which candidates are far enough from branches
            for (size_t i = 0; i < candidates.size(); ++i) {
                float testX = candidates[i][0];
                float testY = candidates[i][1];
                float priority = candidates[i][2];
                
                // Calculate minimum distance to all branches
                float minBranchDist = 1e9f;
                for (const auto& otherNode : layout.nodes) {
                    if (otherNode.id == node.id) continue;
                    
                    for (const auto& childId : otherNode.childIds) {
                        const DendrogramLayoutNode* childNode = layout.FindNode(childId);
                        if (!childNode) continue;
                        
                        float px1 = otherNode.px, py1 = otherNode.py;
                        float px2 = childNode->px, py2 = childNode->py;
                        
                        float segLen = std::sqrt((px2-px1)*(px2-px1) + (py2-py1)*(py2-py1));
                        if (segLen < 0.001f) continue;
                        
                        float t = std::max(0.0f, std::min(1.0f, 
                            ((testX-px1)*(px2-px1) + (testY-py1)*(py2-py1)) / (segLen * segLen)));
                        float projX = px1 + t * (px2 - px1);
                        float projY = py1 + t * (py2 - py1);
                        float dist = std::sqrt((testX - projX)*(testX - projX) + (testY - projY)*(testY - projY));
                        minBranchDist = std::min(minBranchDist, dist);
                    }
                }
                
                // Score: prefer positions with good branch distance AND high priority
                // Minimum 8px from branches, otherwise reject
                float score = (minBranchDist > 8.0f ? minBranchDist : minBranchDist * 0.1f) + priority * 20.0f;
                
                if (score > bestScore) {
                    bestScore = score;
                    lx = testX;
                    ly = testY;
                }
            }

            // Draw connecting line from node to label (subtle line)
            if (std::abs(lx - node.px) > 5.0f || std::abs(ly - node.py) > 5.0f) {
                ctx->SetStrokePaint(Color(150, 150, 160, 80));
                ctx->SetStrokeWidth(0.8f);
                ctx->DrawLine({node.px, node.py}, {lx + tw * 0.3f, ly});
            }

            // Draw label text with hover highlight
            if (node.id == hoveredNodeId) {
                ctx->SetTextPaint(Color(
                    std::min(255, static_cast<int>(style.internalNodeLabelColor.r + 30)),
                    std::min(255, static_cast<int>(style.internalNodeLabelColor.g + 30)),
                    std::min(255, static_cast<int>(style.internalNodeLabelColor.b + 30)),
                    255));
            } else {
                ctx->SetTextPaint(style.internalNodeLabelColor);
            }
            
            ctx->DrawText(label, {static_cast<double>(lx), static_cast<double>(ly)});
        }
    }

// =============================================================================
// RENDER — NODE ANNOTATIONS
// =============================================================================

    void UltraCanvasDendrogram::RenderNodeAnnotations(IRenderContext* ctx)
    {
        if (!style.showNodeAnnotations || !dataSource) return;

        ctx->SetFontFace("sans-serif", FontWeight::Normal, FontSlant::Normal);
        ctx->SetFontSize(style.annotationFontSize);

        for (const auto& node : layout.nodes) {
            if (node.isLeaf) continue;

            const DendrogramNode* src = dataSource->GetNodeById(node.id);
            if (!src || src->nodeAnnotation.empty()) continue;

            Size2Di annoDim = ctx->GetTextLineDimensions(src->nodeAnnotation);
            int tw = annoDim.width;
            int th = annoDim.height;

            float bx = node.px + 3.0f;
            float by = node.py - th - 4.0f;
            float bw = tw + 8.0f;
            float bh = th + 4.0f;

            ctx->DrawFilledRectangle(
                Rect2Dd(bx, by, bw, bh),
                style.annotationBgColor,
                0.5f, style.annotationBorderColor, 2.0f);

            ctx->SetTextPaint(style.annotationTextColor);
            ctx->DrawText(src->nodeAnnotation, {static_cast<double>(bx + 4.0f), static_cast<double>(by + th)});
        }
    }

// =============================================================================
// RENDER — GROUP LABELS (Strategy 3)
// =============================================================================

    void UltraCanvasDendrogram::RenderGroupLabels(IRenderContext* ctx)
    {
        if (!style.showGroupLabels || !dataSource) return;

        for (size_t gi = 0; gi < dataSource->GetGroupCount(); ++gi) {
            const DendrogramGroup* g = dataSource->GetGroup(gi);
            if (!g || !g->showGroupLabel) continue;

            switch (g->labelPlacement) {
                case GroupLabelPlacement::SidebarBracket:
                    RenderGroupSidebarBrackets(ctx);
                    return; // Renders all groups in one pass
                case GroupLabelPlacement::FloatingBadge:
                    RenderGroupFloatingBadges(ctx);
                    return;
                case GroupLabelPlacement::ArcText:
                    RenderGroupArcLabels(ctx);
                    return;
                default:
                    break;
            }
        }
    }

    void UltraCanvasDendrogram::RenderGroupSidebarBrackets(IRenderContext* ctx)
    {
        if (!dataSource) return;

        // Sidebar sits to the right of the leaf labels column
        Rect2Dd tb     = GetTreeBounds();
        float   sideX  = GetX() + GetWidth() - style.marginRight - 80.0f;

        ctx->SetFontFace("sans-serif", FontWeight::Normal, FontSlant::Normal);

        for (size_t gi = 0; gi < dataSource->GetGroupCount(); ++gi) {
            const DendrogramGroup* g = dataSource->GetGroup(gi);
            if (!g || !g->showGroupLabel) continue;

            float minY = 1e9f, maxY = -1e9f;
            for (const auto& leafId : g->leafIds) {
                const DendrogramLayoutNode* n = layout.FindNode(leafId);
                if (!n || !n->isLeaf) continue;
                if (orientation == DendrogramOrientation::LeftRight) {
                    minY = std::min(minY, n->py);
                    maxY = std::max(maxY, n->py);
                }
            }
            if (minY > maxY) continue;

            float pad = 2.0f;
            float bx  = sideX;
            float by  = minY - pad;
            float bh  = maxY - minY + pad * 2.0f;

            // Colored bracket bar
            ctx->SetFillPaint(g->branchColor);
            ctx->FillRectangle(Rect2Dd(bx, by, static_cast<float>(style.sidebarBracketWidth), bh));

            // Lines from bracket ends to label anchor
            float labelX = bx + style.sidebarBracketWidth + style.sidebarBracketGap;
            float labelY = (minY + maxY) * 0.5f + g->groupLabelFontSize * 0.35f;

            ctx->SetStrokePaint(g->branchColor);
            ctx->SetStrokeWidth(0.5f);
            ctx->DrawLine({bx + style.sidebarBracketWidth, by},
                          {labelX - 2.0f, by});
            ctx->DrawLine({bx + style.sidebarBracketWidth, by + bh},
                          {labelX - 2.0f, by + bh});

            // Label
            ctx->SetFontSize(g->groupLabelFontSize);
            ctx->SetFontFace("sans-serif", g->groupLabelWeight, FontSlant::Normal);
            ctx->SetTextPaint(g->branchColor);
            ctx->DrawText(g->label, {static_cast<double>(labelX), static_cast<double>(labelY)});
        }
    }

    void UltraCanvasDendrogram::RenderGroupFloatingBadges(IRenderContext* ctx)
    {
        if (!dataSource) return;
        ctx->SetFontFace("sans-serif", FontWeight::Bold, FontSlant::Normal);

        for (size_t gi = 0; gi < dataSource->GetGroupCount(); ++gi) {
            const DendrogramGroup* g = dataSource->GetGroup(gi);
            if (!g || !g->showGroupLabel) continue;

            float cx = 0.0f, cy = 0.0f;
            int   count = 0;
            for (const auto& leafId : g->leafIds) {
                const DendrogramLayoutNode* n = layout.FindNode(leafId);
                if (!n) continue;
                cx += n->px; cy += n->py; ++count;
            }
            if (count == 0) continue;
            cx /= count; cy /= count;

            ctx->SetFontSize(g->groupLabelFontSize);
            Size2Di gDim = ctx->GetTextLineDimensions(g->label);
            int tw = gDim.width;
            int th = gDim.height;

            float bx = cx - tw * 0.5f - style.floatingBadgePad;
            float by = cy - th * 0.5f - style.floatingBadgePad;
            float bw = tw + style.floatingBadgePad * 2;
            float bh = th + style.floatingBadgePad * 2;

            Color fill = g->fillColor;
            fill.a = 200;
            ctx->DrawFilledRectangle(Rect2Dd(bx, by, bw, bh), fill, 1.0f, g->branchColor, 4.0f);
            ctx->SetTextPaint(g->branchColor);
            ctx->DrawText(g->label, {static_cast<double>(bx + style.floatingBadgePad),
                                      static_cast<double>(by + th + style.floatingBadgePad)});
        }
    }

    void UltraCanvasDendrogram::RenderGroupArcLabels(IRenderContext* ctx)
    {
        if (!dataSource || orientation != DendrogramOrientation::Radial) return;

        Rect2Dd tb = GetTreeBounds();
        float   cx = tb.x + tb.width  * 0.5f;
        float   cy = tb.y + tb.height * 0.5f;
        float   maxR = std::min(tb.width, tb.height) * 0.5f * style.arcLabelRadiusMul;

        ctx->SetFontFace("sans-serif", FontWeight::Bold, FontSlant::Normal);

        for (size_t gi = 0; gi < dataSource->GetGroupCount(); ++gi) {
            const DendrogramGroup* g = dataSource->GetGroup(gi);
            if (!g || !g->showGroupLabel) continue;

            float minAngle = 1e9f, maxAngle = -1e9f;
            for (const auto& leafId : g->leafIds) {
                const DendrogramLayoutNode* n = layout.FindNode(leafId);
                if (!n) continue;
                minAngle = std::min(minAngle, n->angle);
                maxAngle = std::max(maxAngle, n->angle);
            }
            if (minAngle > maxAngle) continue;

            float midAngle = (minAngle + maxAngle) * 0.5f;
            float lx       = cx + std::cos(midAngle) * maxR;
            float ly       = cy + std::sin(midAngle) * maxR;

            ctx->SetFontSize(g->groupLabelFontSize);
            int tw = ctx->GetTextLineWidth(g->label);

            ctx->PushState();
            ctx->Translate(lx, ly);
            ctx->Rotate(midAngle + (midAngle > kPi * 0.5f && midAngle < kPi * 1.5f ? kPi : 0.0f));
            ctx->SetTextPaint(g->branchColor);
            ctx->DrawText(g->label, {-tw * 0.5, g->groupLabelFontSize * 0.35});
            ctx->PopState();
        }
    }

// =============================================================================
// RENDER — SCROLLBARS
// =============================================================================

    void UltraCanvasDendrogram::RenderScrollbars(IRenderContext* ctx)
    {
        Rect2Di local = GetLocalBounds();
        if (vertScrollbar && vertScrollbar->IsVisible()) vertScrollbar->Render(ctx, local);
        if (horzScrollbar && horzScrollbar->IsVisible()) horzScrollbar->Render(ctx, local);
    }

// =============================================================================
// COLOR / WIDTH HELPERS
// =============================================================================

    Color UltraCanvasDendrogram::ResolveBranchColor(const DendrogramLayoutNode& node) const
    {
        switch (style.branchColorMode) {
            case BranchColorMode::Uniform:
                return style.defaultBranchColor;

            case BranchColorMode::ByGroup: {
                auto it = leafGroupMap.find(node.id);
                if (it != leafGroupMap.end() && dataSource) {
                    const DendrogramGroup* g = dataSource->GetGroupById(it->second);
                    if (g) return g->branchColor;
                }
                return style.defaultBranchColor;
            }

            case BranchColorMode::ByDistance: {
                float t = (layout.maxDistance > 0.0f)
                    ? static_cast<float>(node.depthPos)
                    : 0.0f;
                return InterpolateGradient(t);
            }

            case BranchColorMode::ByValue: {
                if (dataSource) {
                    const DendrogramNode* src = dataSource->GetNodeById(node.id);
                    if (src && src->branchValue >= 0.0f)
                        return InterpolateGradient(src->branchValue);
                }
                return style.defaultBranchColor;
            }

            case BranchColorMode::Custom: {
                if (dataSource) {
                    const DendrogramNode* src = dataSource->GetNodeById(node.id);
                    if (src && src->customBranchColor.a > 0)
                        return src->customBranchColor;
                }
                return style.defaultBranchColor;
            }
        }
        return style.defaultBranchColor;
    }

    Color UltraCanvasDendrogram::InterpolateGradient(float t) const
    {
        t = std::clamp(t, 0.0f, 1.0f);
        auto lerp = [&](uint8_t a, uint8_t b) -> uint8_t {
            return static_cast<uint8_t>(a + (b - a) * t);
        };
        return Color(
            lerp(style.gradientLow.r, style.gradientHigh.r),
            lerp(style.gradientLow.g, style.gradientHigh.g),
            lerp(style.gradientLow.b, style.gradientHigh.b),
            255);
    }

    // Resolve the dot color for any node (leaf or internal).
    // Priority:
    //   1. colorNodesByDepth  → palette[depth % size]  (internal nodes only)
    //   2. colorLeafNodesByGroup → group branchColor    (leaf nodes only)
    //   3. internalNodeColor / branchColor as fallback
    Color UltraCanvasDendrogram::ResolveNodeColor(const DendrogramLayoutNode& node) const
    {
        if (node.isLeaf) {
            // Leaf: use group color when enabled
            if (style.colorLeafNodesByGroup) {
                auto it = leafGroupMap.find(node.id);
                if (it != leafGroupMap.end() && dataSource) {
                    const DendrogramGroup* g = dataSource->GetGroupById(it->second);
                    if (g) return g->branchColor;
                }
            }
            return ResolveBranchColor(node);
        }

        // Internal node: depth-based palette first
        if (style.colorNodesByDepth && !style.nodeDepthPalette.empty()) {
            size_t idx = static_cast<size_t>(node.depth) % style.nodeDepthPalette.size();
            return style.nodeDepthPalette[idx];
        }

        return style.internalNodeColor;
    }

    // Resolve dot radius for a node.
    // Leaf nodes always use leafNodeRadius.
    // Internal nodes scale by depth when scaleNodesByDepth is true:
    //   radius = internalNodeRadius * (1 + scale * (1 - normalizedDepth))
    // where normalizedDepth=0 is the root (largest) and 1 is near the leaves (smallest).
    float UltraCanvasDendrogram::ResolveNodeRadius(const DendrogramLayoutNode& node) const
    {
        if (node.isLeaf) return style.leafNodeRadius;

        if (!style.scaleNodesByDepth) return style.internalNodeRadius;

        // depthPos: 0 = root, 1 = max depth (leaves)
        float normalized = std::clamp(node.depthPos, 0.0f, 1.0f);
        float r = style.internalNodeRadius *
                  (1.0f + style.nodeRadiusDepthScale * (1.0f - normalized));
        return std::min(static_cast<double>(r), style.nodeRadiusMax);
    }

    float UltraCanvasDendrogram::ComputeBranchWidth(float confidence) const
    {
        if (style.confidenceMode == ConfidenceDisplayMode::None) return style.defaultBranchWidth;
        if (style.confidenceMode == ConfidenceDisplayMode::LineDash ||
            style.confidenceMode == ConfidenceDisplayMode::NodeDot)  return style.defaultBranchWidth;

        float t = std::clamp(confidence, 0.0f, 1.0f);
        return style.branchWidthMin + t * (style.branchWidthMax - style.branchWidthMin);
    }

    bool UltraCanvasDendrogram::ComputeBranchDash(float confidence) const
    {
        if (style.confidenceMode == ConfidenceDisplayMode::LineDash ||
            style.confidenceMode == ConfidenceDisplayMode::LineThicknessAndDot) {
            return (confidence < style.confidenceLowThreshold);
        }
        return false;
    }

// =============================================================================
// HIT TESTING
// =============================================================================

    std::string UltraCanvasDendrogram::HitTestNode(int mx, int my) const
    {
        // Convert screen position to layout space (undo zoom+pan)
        float lx = (mx - GetX() - panOffset.x) / zoom;
        float ly = (my - GetY() - panOffset.y) / zoom;

        // Adjust for element origin since layout px/py include GetX/GetY offset
        lx += GetX();
        ly += GetY();

        float hitRadius = 10.0f / zoom; // Scale hit radius with zoom
        std::string bestId;
        float bestDist = hitRadius;

        for (const auto& node : layout.nodes) {
            // Skip nodes hidden by collapse
            if (!IsSubtreeVisible(node.id)) continue;

            float dx   = lx - node.px;
            float dy   = ly - node.py;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < bestDist) {
                bestDist = dist;
                bestId   = node.id;
            }
        }
        return bestId;
    }

    // Hit-test branches: find which parent node's branch the mouse is closest to.
    // Returns the parent nodeId whose subtree branches pass near (mx, my).
    std::string UltraCanvasDendrogram::HitTestBranch(int mx, int my) const
    {
        // Convert to layout space
        float lx = (mx - GetX() - panOffset.x) / zoom + GetX();
        float ly = (my - GetY() - panOffset.y) / zoom + GetY();

        float bestDist = 12.0f / zoom;
        std::string bestParentId;

        for (const auto& node : layout.nodes) {
            if (node.isLeaf) continue;
            if (!IsSubtreeVisible(node.id) && node.depth > 0) continue;
            if (collapsedNodes.count(node.id)) continue;

            for (const auto& childId : node.childIds) {
                const DendrogramLayoutNode* child = layout.FindNode(childId);
                if (!child) continue;

                // Approximate branch as line segment parent → child; compute point-to-segment dist
                float ax = node.px,  ay = node.py;
                float bx = child->px, by = child->py;
                float dx = bx - ax,   dy = by - ay;
                float lenSq = dx * dx + dy * dy;
                float dist = 1e9f;

                if (lenSq < 1.0f) {
                    dist = std::sqrt((lx - ax) * (lx - ax) + (ly - ay) * (ly - ay));
                } else {
                    float t = std::clamp(((lx - ax) * dx + (ly - ay) * dy) / lenSq, 0.0f, 1.0f);
                    float projX = ax + t * dx;
                    float projY = ay + t * dy;
                    dist = std::sqrt((lx - projX) * (lx - projX) + (ly - projY) * (ly - projY));
                }

                if (dist < bestDist) {
                    bestDist     = dist;
                    bestParentId = node.id;
                }
            }
        }
        return bestParentId;
    }

    std::string UltraCanvasDendrogram::HitTestGroup(int mx, int my) const
    {
        if (!dataSource) return {};

        for (size_t gi = 0; gi < dataSource->GetGroupCount(); ++gi) {
            const DendrogramGroup* g = dataSource->GetGroup(gi);
            if (!g) continue;

            float minY = 1e9f, maxY = -1e9f;
            for (const auto& leafId : g->leafIds) {
                const DendrogramLayoutNode* n = layout.FindNode(leafId);
                if (!n) continue;
                minY = std::min(minY, n->py);
                maxY = std::max(maxY, n->py);
            }
            if (minY > maxY) continue;

            // Check sidebar bracket area
            Rect2Dd tb    = GetTreeBounds();
            float   sideX = GetX() + GetWidth() - style.marginRight - 80.0f;
            if (mx >= sideX && mx <= sideX + 80 && my >= minY - 4 && my <= maxY + 4)
                return g->groupId;
        }
        return {};
    }

// =============================================================================
// EVENT HANDLING
// =============================================================================

    bool UltraCanvasDendrogram::OnEvent(const UCEvent& event)
    {
        if (!IsVisible() || IsDisabled()) return false;

        try {
            Rect2Di bounds = GetBounds();
            bool inside = finalBounds.Contains(Point2Di(event.pointer.x, event.pointer.y));
            if (!inside && event.type != UCEventType::MouseUp) return false;

            switch (event.type) {
                case UCEventType::MouseDown:   return HandleMouseDown(event);
                case UCEventType::MouseUp:     return HandleMouseUp(event);
                case UCEventType::MouseMove:   return HandleMouseMove(event);
                case UCEventType::MouseWheel:  return HandleMouseWheel(event);
                default: break;
            }
        } catch (const std::exception& e) {
            return false;
        } catch (...) {
            return false;
        }
        return false;
    }

    bool UltraCanvasDendrogram::HandleMouseDown(const UCEvent& event)
    {
        // Middle-button or Alt+Left: pan
        if (event.button == UCMouseButton::Middle ||
           (event.button == UCMouseButton::Left && event.alt)) {
            isPanning    = true;
            lastPanMouse = {event.pointer.x, event.pointer.y};
            return true;
        }

        if (event.button == UCMouseButton::Left) {
            std::string nodeId = HitTestNode(event.pointer.x, event.pointer.y);
            if (!nodeId.empty()) {
                const DendrogramLayoutNode* n = layout.FindNode(nodeId);
                bool additive = event.shift || event.ctrl;

                // Start dragging if Ctrl is held
                if (event.ctrl && n) {
                    draggingNodeId = nodeId;
                    dragStartPos = {event.pointer.x, event.pointer.y};
                    DendrogramLayoutNode* node = layout.FindNode(nodeId);
                    if (node) {
                        originalNodePos = {node->px, node->py};
                    }
                    SetMouseCursor(UCMouseCursor::SizeAll);
                    return true;
                }

                // Double-click detection using timestamps
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - lastClickTime).count();
                bool isDoubleClick = (elapsed <= 300 && 
                    std::abs(event.pointer.x - lastClickX) <= 10 && 
                    std::abs(event.pointer.y - lastClickY) <= 10);
                
                if (isDoubleClick && n && !n->isLeaf) {
                    ToggleCollapse(nodeId);
                    RequestRedraw();
                    return true;
                }
                
                // Only update timestamp on single clicks
                if (!isDoubleClick) {
                    lastClickTime = now;
                    lastClickX = event.pointer.x;
                    lastClickY = event.pointer.y;
                }

                if (n && n->isLeaf) {
                    SelectNode(nodeId, additive);
                    if (onLeafClicked) onLeafClicked(nodeId);
                } else if (n) {
                    SelectNode(nodeId, additive);
                    if (onInternalNodeClicked) onInternalNodeClicked(nodeId);
                }

                RequestRedraw();
                return true;
            }

            // Toggle branch label visibility on click (for showLabelsOnClick mode)
            if (style.showLabelsOnClick) {
                std::string branchParent = HitTestBranch(event.pointer.x, event.pointer.y);
                if (!branchParent.empty()) {
                    if (visibleBranchLabels.count(branchParent)) {
                        visibleBranchLabels.erase(branchParent);
                    } else {
                        visibleBranchLabels.insert(branchParent);
                    }
                    RequestRedraw();
                    return true;
                }
            }

            std::string groupId = HitTestGroup(event.pointer.x, event.pointer.y);
            if (!groupId.empty()) {
                if (onGroupClicked) onGroupClicked(groupId);
                return true;
            }
        }
        return false;
    }

    bool UltraCanvasDendrogram::HandleMouseUp(const UCEvent& event)
    {
        if (isPanning) {
            isPanning = false;
            return true;
        }

        // End node drag - save position
        if (!draggingNodeId.empty()) {
            customNodePositions[draggingNodeId] = {
                (event.pointer.x - GetX() - panOffset.x) / zoom,
                (event.pointer.y - GetY() - panOffset.y) / zoom
            };
            draggingNodeId.clear();
            SetMouseCursor(UCMouseCursor::Default);
            RebuildLayout();
            RequestRedraw();
            return true;
        }

        return false;
    }

    bool UltraCanvasDendrogram::HandleMouseMove(const UCEvent& event)
    {
        if (isPanning) {
            float dx = static_cast<float>(event.pointer.x - lastPanMouse.x);
            float dy = static_cast<float>(event.pointer.y - lastPanMouse.y);
            panOffset.x += dx;
            panOffset.y += dy;
            lastPanMouse = {event.pointer.x, event.pointer.y};
            RequestRedraw();
            return true;
        }

        // Handle node dragging
        if (!draggingNodeId.empty()) {
            float lx = (event.pointer.x - GetX() - panOffset.x) / zoom;
            float ly = (event.pointer.y - GetY() - panOffset.y) / zoom;
            
            // Update node position in layout
            DendrogramLayoutNode* n = layout.FindNode(draggingNodeId);
            if (n) {
                n->px = lx;
                n->py = ly;
            }
            RequestRedraw();
            return true;
        }

        bool redrawNeeded = false;

        // Node hover (for tooltip)
        std::string hitId = HitTestNode(event.pointer.x, event.pointer.y);
        if (hitId != hoveredNodeId) {
            hoveredNodeId = hitId;
            if (onNodeHovered) onNodeHovered(hoveredNodeId);

            if (!hoveredNodeId.empty() && dataSource) {
                const DendrogramNode* src = dataSource->GetNodeById(hoveredNodeId);
                if (src && !src->tooltip.empty()) {
                    auto globalPos = MapFromLocal({event.pointer.x, event.pointer.y}, nullptr);
                    UltraCanvasTooltipManager::UpdateAndShowTooltip(
                        GetWindow(), src->tooltip, globalPos);
                } else {
                    UltraCanvasTooltipManager::HideTooltip();
                }
            } else {
                UltraCanvasTooltipManager::HideTooltip();
            }
            redrawNeeded = true;
        }

        // Branch hover (for highlight)
        std::string branchParent = HitTestBranch(event.pointer.x, event.pointer.y);
        if (branchParent != hoveredBranchParentId) {
            hoveredBranchParentId = branchParent;
            redrawNeeded = true;
        }

        // Group hover
        std::string gid = HitTestGroup(event.pointer.x, event.pointer.y);
        if (gid != hoveredGroupId) {
            if (!hoveredGroupId.empty() && onGroupHighlighted)
                onGroupHighlighted(hoveredGroupId, false);
            hoveredGroupId = gid;
            if (!hoveredGroupId.empty() && onGroupHighlighted)
                onGroupHighlighted(hoveredGroupId, true);
            redrawNeeded = true;
        }

        if (redrawNeeded) RequestRedraw();
        return !hoveredNodeId.empty() || !hoveredBranchParentId.empty();
    }

    bool UltraCanvasDendrogram::HandleMouseWheel(const UCEvent& event)
    {
        float delta = event.wheelDelta > 0 ? 1.12f : (1.0f / 1.12f);
        double newZoom = std::clamp(zoom * delta, 0.05, 20.0);

        // Zoom toward mouse position in element-local space
        float mx = static_cast<float>(event.pointer.x - GetX());
        float my = static_cast<float>(event.pointer.y - GetY());
        float ratio = newZoom / zoom;
        panOffset.x = mx - (mx - panOffset.x) * ratio;
        panOffset.y = my - (my - panOffset.y) * ratio;
        zoom = newZoom;

        RequestRedraw();
        return true;
    }

} // namespace UltraCanvas