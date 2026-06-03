// UltraCanvasDendrogramLayout.cpp
// Reingold-Tilford layout engine for dendrogram tree positioning
// Version: 1.3.0
// Last Modified: 2026-04-05
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasDendrogramLayout.h"
#include <stdexcept>
#include <limits>
#include <cassert>

namespace UltraCanvas {

// =============================================================================
// PUBLIC ENTRY POINT
// =============================================================================

    DendrogramLayout DendrogramLayoutEngine::Compute(
            const IDendrogramDataSource* data,
            const Rect2Dd& bounds,
            DendrogramOrientation orientation,
            DendrogramScaleMode scaleMode,
            float leafSpacing)
    {
        DendrogramLayout layout;
        if (!data || data->GetNodeCount() == 0) return layout;

        std::string rootId = data->GetRootId();
        if (rootId.empty()) return layout;

        // --- Step 1: Find maximum merge distance ---
        layout.maxDistance = FindMaxDistance(data, rootId);
        if (layout.maxDistance <= 0.0) layout.maxDistance = 1.0;

        // --- Step 2: Build RT tree ---
        int leafCount = 0;
        auto rtRoot = BuildRTTree(data, rootId, nullptr, leafCount);
        if (!rtRoot) return layout;

        // --- Step 3: First walk (assign preliminary positions) ---
        // FirstWalk assigns prelim in pixel units: each adjacent leaf is leafSpacing apart.
        // We need to know the actual span BEFORE calling SecondWalk so we can fit-to-bounds.

        // Dry-run: count max prelim by simulating leaf positions
        // Simpler: after FirstWalk the root's prelim ≈ midpoint of tree.
        // The rightmost leaf has prelim = (leafCount-1) * leafSpacing.
        // We clamp leafSpacing so the full tree fits inside bounds.

        float effectiveLeafSpacing = leafSpacing;
        if (orientation == DendrogramOrientation::TopDown ||
            orientation == DendrogramOrientation::BottomUp) {
            // Primary axis = width. Fit (leafCount-1)*spacing inside bounds.width
            if (leafCount > 1) {
                float maxSpacing = bounds.width / static_cast<float>(leafCount - 1);
                effectiveLeafSpacing = std::min(leafSpacing, maxSpacing);
            }
        } else if (orientation == DendrogramOrientation::LeftRight ||
                   orientation == DendrogramOrientation::RightLeft) {
            // Primary axis = height. Fit inside bounds.height
            if (leafCount > 1) {
                float maxSpacing = bounds.height / static_cast<float>(leafCount - 1);
                effectiveLeafSpacing = std::min(leafSpacing, maxSpacing);
            }
        }

        FirstWalk(rtRoot.get(), effectiveLeafSpacing);

        // Assign correct pixel positions to every node using the simple Walker approach:
        // leaves get sequential positions, internal nodes get the midpoint of their children.
        int leafCounter = 0;
        AssignLeafPositions(rtRoot.get(), effectiveLeafSpacing, leafCounter);

        // --- Step 4: Second walk ---
        // primaryScale = 1.0 because prelim is already in pixel units from FirstWalk.
        // depthScale = available pixels on the depth axis.
        float depthScale   = 0.0f;
        float primaryScale = 1.0f; // prelim already in pixel space — no extra scaling

        if (orientation == DendrogramOrientation::TopDown ||
            orientation == DendrogramOrientation::BottomUp) {
            depthScale = bounds.height;
        } else if (orientation == DendrogramOrientation::LeftRight ||
                   orientation == DendrogramOrientation::RightLeft) {
            depthScale = bounds.width;
        } else {
            // Radial — ApplyRadialLayout handles scaling
            float r    = std::min(bounds.width, bounds.height) * 0.5f;
            depthScale = r;
        }

        layout.primaryScale = primaryScale;
        layout.depthScale   = depthScale;
        layout.originX      = bounds.x;
        layout.originY      = bounds.y;
        layout.totalWidth   = bounds.width;
        layout.totalHeight  = bounds.height;

        float minX = std::numeric_limits<float>::max();
        SecondWalk(rtRoot.get(), 0.0f, minX, layout,
                   layout.maxDistance, depthScale,
                   bounds.x, bounds.y,
                   orientation, scaleMode, leafCount, primaryScale,
                   0);

        // Center the tree within the available bounds on the primary axis.
        // minX holds the actual minimum primary coordinate from SecondWalk.
        // Guard: if no nodes were added, minX will still be max()
        if (minX == std::numeric_limits<float>::max()) minX = 0.0f;

        float treeSpan = (leafCount > 1)
            ? static_cast<float>(leafCount - 1) * effectiveLeafSpacing
            : 0.0f;

        if (orientation == DendrogramOrientation::TopDown ||
            orientation == DendrogramOrientation::BottomUp) {
            // Primary axis = X. minX = leftmost px. Center within bounds.width.
            float margin = (bounds.width - treeSpan) * 0.5f;
            float target = bounds.x + std::max(0.0f, margin);
            float shift  = target - minX;
            if (std::abs(shift) > 0.5f) {
                for (auto& n : layout.nodes) n.px += shift;
            }
        } else if (orientation == DendrogramOrientation::LeftRight ||
                   orientation == DendrogramOrientation::RightLeft) {
            // Primary axis = Y. minX = topmost py. Center within bounds.height.
            float margin = (bounds.height - treeSpan) * 0.5f;
            float target = bounds.y + std::max(0.0f, margin);
            float shift  = target - minX;
            if (std::abs(shift) > 0.5f) {
                for (auto& n : layout.nodes) n.py += shift;
            }
        }

        // --- Step 5: Radial conversion ---
        if (orientation == DendrogramOrientation::Radial) {
            ApplyRadialLayout(layout, bounds);
        }

        // --- Step 6: Build index ---
        for (size_t i = 0; i < layout.nodes.size(); ++i) {
            layout.nodeIndex[layout.nodes[i].id] = i;
        }

        return layout;
    }

// =============================================================================
// BUILD RT TREE
// =============================================================================

    std::unique_ptr<DendrogramLayoutEngine::RTNode> DendrogramLayoutEngine::BuildRTTree(
            const IDendrogramDataSource* data,
            const std::string& nodeId,
            RTNode* parent,
            int& leafCount)
    {
        if (!data) return nullptr;
        
        const DendrogramNode* src = data->GetNodeById(nodeId);
        if (!src) return nullptr;

        auto node = std::make_unique<RTNode>();
        node->id             = src->id;
        node->mergeDistance  = src->mergeDistance;
        node->parent         = parent;
        node->ancestor       = node.get();
        node->isLeaf         = src->IsLeaf();

        if (src->IsLeaf()) {
            ++leafCount;
        } else {
            RTNode* prevSib = nullptr;
            for (size_t i = 0; i < src->childIds.size(); ++i) {
                auto child = BuildRTTree(data, src->childIds[i], node.get(), leafCount);
                if (child) {
                    child->number  = static_cast<int>(i);
                    child->leftSib = prevSib;
                    prevSib        = child.get();
                    node->children.push_back(std::move(child));
                }
            }
        }
        return node;
    }

// =============================================================================
// WALKER LAYOUT — correct two-pass algorithm using a global leaf position counter
// Pass 1 (bottom-up): assign each node a preliminary position based on its leaves
// Pass 2 (top-down):  apply accumulated modifiers to get final positions
// This is simpler and more robust than full Reingold-Tilford for dendrograms.
// =============================================================================

    // Pass 1: assign prelim (pixel position on primary axis).
    // Uses a mutable leaf counter that increments for each leaf visited left-to-right.
    void DendrogramLayoutEngine::FirstWalk(RTNode* node, float leafSpacing)
    {
        if (!node) return;

        if (node->isLeaf) {
            // Prelim is set in Pass 2 from the global leaf counter.
            // Nothing to do here for leaves — just mark them.
            node->prelim = 0.0f;
            node->mod    = 0.0f;
        } else {
            // Recurse into all children first (post-order)
            for (auto& child : node->children) {
                FirstWalk(child.get(), leafSpacing);
            }
            // Internal node: prelim = midpoint of leftmost and rightmost child prelims.
            // (This will be updated after the global leaf numbering in SecondWalk.)
            node->prelim = 0.0f;
            node->mod    = 0.0f;
        }
    }

    void DendrogramLayoutEngine::AssignLeafPositions(RTNode* node,
                                    float leafSpacing,
                                    int& leafCounter)
    {
        if (!node) return;

        if (node->isLeaf) {
            node->prelim = static_cast<float>(leafCounter) * leafSpacing;
            ++leafCounter;
        } else {
            // Recurse children left to right
            for (auto& child : node->children) {
                AssignLeafPositions(child.get(), leafSpacing, leafCounter);
            }
            // Internal node: midpoint of first and last child
            if (!node->children.empty()) {
                float firstChildPrelim = node->children.front()->prelim;
                float lastChildPrelim  = node->children.back()->prelim;
                node->prelim = (firstChildPrelim + lastChildPrelim) * 0.5f;
            }
        }
    }

// =============================================================================
// REINGOLD-TILFORD helpers — kept as stubs (Walker algorithm used instead)
// =============================================================================

    void DendrogramLayoutEngine::Apportion(RTNode* node, RTNode*& defaultAncestor, float leafSpacing)
    {
        // Not used — Walker layout in AssignLeafPositions handles positioning
        (void)node; (void)defaultAncestor; (void)leafSpacing;
    }

    void DendrogramLayoutEngine::ExecuteShifts(RTNode* node)
    {
        (void)node;
    }

    void DendrogramLayoutEngine::MoveSubtree(RTNode* wm, RTNode* wp, float shift)
    {
        (void)wm; (void)wp; (void)shift;
    }

    DendrogramLayoutEngine::RTNode* DendrogramLayoutEngine::Ancestor(
            RTNode* vil, RTNode* node, RTNode* defaultAncestor)
    {
        if (!vil || !node) return defaultAncestor;
        if (vil->ancestor && vil->ancestor->parent == node->parent)
            return vil->ancestor;
        return defaultAncestor;
    }

    DendrogramLayoutEngine::RTNode* DendrogramLayoutEngine::NextLeft(RTNode* node)
    {
        if (!node || node->children.empty()) return nullptr;
        return node->children.front().get();
    }

    DendrogramLayoutEngine::RTNode* DendrogramLayoutEngine::NextRight(RTNode* node)
    {
        if (!node || node->children.empty()) return nullptr;
        return node->children.back().get();
    }

// =============================================================================
// SECOND WALK — convert preliminary positions to pixel layout nodes
// =============================================================================

    void DendrogramLayoutEngine::SecondWalk(
            RTNode* node, float mod, float& minX,
            DendrogramLayout& layout,
            double maxDist, float depthScale,
            float originX, float originY,
            DendrogramOrientation orient,
            DendrogramScaleMode scaleMode,
            int totalLeaves, float leafSpacingPx,
            int depth)
    {
        float primaryPos = node->prelim + mod;

        // Depth position: 0 = root, 1 = leaves (normalized merge distance)
        float normalizedDepth = (maxDist > 0.0)
            ? static_cast<float>(node->mergeDistance / maxDist)
            : 0.0f;

        // Cladogram: leaves all at 1.0, internals spaced evenly by integer depth
        float depthPos = 0.0f;
        if (scaleMode == DendrogramScaleMode::Cladogram) {
            depthPos = node->isLeaf
                ? 1.0f
                : static_cast<float>(depth) / static_cast<float>(std::max(1, depth + 1));
        } else {
            depthPos = normalizedDepth;
        }

        DendrogramLayoutNode out;
        out.id         = node->id;
        out.isLeaf     = node->isLeaf;
        out.depth      = depth;  // Integer depth from root (0 = root)

        for (const auto& child : node->children)
            out.childIds.push_back(child->id);

        float px = 0.0f, py = 0.0f;

        switch (orient) {
            case DendrogramOrientation::TopDown:
                px = originX + primaryPos;
                py = originY + (1.0f - depthPos) * depthScale;
                break;
            case DendrogramOrientation::BottomUp:
                px = originX + primaryPos;
                py = originY + depthPos * depthScale;
                break;
            case DendrogramOrientation::LeftRight:
                py = originY + primaryPos;
                px = originX + (1.0f - depthPos) * depthScale;
                break;
            case DendrogramOrientation::RightLeft:
                py = originY + primaryPos;
                px = originX + depthPos * depthScale;
                break;
            case DendrogramOrientation::Radial:
                // ApplyRadialLayout() overwrites px/py after second walk
                px = primaryPos;
                py = depthPos;
                break;
        }

        out.px         = px;
        out.py         = py;
        out.primaryPos = primaryPos;
        out.depthPos   = depthPos;

        if (orient == DendrogramOrientation::TopDown || orient == DendrogramOrientation::BottomUp) {
            minX = std::min(minX, px);
        } else {
            minX = std::min(minX, py);
        }

        layout.nodes.push_back(out);

        for (const auto& child : node->children) {
            SecondWalk(child.get(), mod + node->mod, minX, layout,
                       maxDist, depthScale, originX, originY,
                       orient, scaleMode, totalLeaves, leafSpacingPx,
                       depth + 1);
        }
    }

// =============================================================================
// RADIAL LAYOUT CONVERSION
// =============================================================================

    void DendrogramLayoutEngine::ApplyRadialLayout(DendrogramLayout& layout, const Rect2Dd& bounds)
    {
        float cx = bounds.x + bounds.width  * 0.5f;
        float cy = bounds.y + bounds.height * 0.5f;
        float maxR = std::min(bounds.width, bounds.height) * 0.5f * 0.85f;

        // primaryPos in [0, leafCount * leafSpacing] — map to angle
        float maxPrimary = 0.0f;
        for (auto& n : layout.nodes)
            maxPrimary = std::max(maxPrimary, n.primaryPos);
        if (maxPrimary <= 0.0f) maxPrimary = 1.0f;

        for (auto& n : layout.nodes) {
            float angle  = (n.primaryPos / maxPrimary) * 2.0f * 3.14159265f;
            float radius = (1.0f - n.depthPos) * maxR; // root at centre
            n.angle  = angle;
            n.radius = radius;
            n.px = cx + std::cos(angle) * radius;
            n.py = cy + std::sin(angle) * radius;
        }
    }

// =============================================================================
// UTILITY
// =============================================================================

    double DendrogramLayoutEngine::FindMaxDistance(
            const IDendrogramDataSource* data, const std::string& nodeId)
    {
        const DendrogramNode* node = data->GetNodeById(nodeId);
        if (!node) return 0.0;
        double maxDist = node->mergeDistance;
        for (const auto& childId : node->childIds) {
            maxDist = std::max(maxDist, FindMaxDistance(data, childId));
        }
        return maxDist;
    }

} // namespace UltraCanvas
