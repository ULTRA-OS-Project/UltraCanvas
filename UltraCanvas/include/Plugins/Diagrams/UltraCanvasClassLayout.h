// include/Plugins/Diagrams/UltraCanvasClassLayout.h
// Automatic layout for class diagrams
// Version: 1.0.0
// Last Modified: 2026-08-02
// Author: UltraCanvas Framework
//
// UI-free, like the rest of the class-diagram foundation: it depends only on
// UltraCanvasCommonTypes.h. It does not measure text and it does not know what
// a classifier is — the caller measures its boxes, hands over sizes and edges,
// and gets positions back. That is what makes it unit-testable without a
// window (Tests/ClassLayoutTest.cpp).
//
// The layouts implement proposal items L2-L4 and L8-L11:
//   Layered  - Sugiyama-style ranking driven by the generalization edges, so
//              parents sit above children and inheritance arrows read upward.
//              Ordering within a rank is barycentre-based to cut crossings.
//   Tree     - Layered plus a parent-centring pass, for pure hierarchies.
//   Grid     - Deterministic rows and columns; the fallback that never
//              surprises anyone.
//   Manual   - Positions untouched; only the bounding box is computed.
//
// CHANGELOG 1.0.0:
//  - Initial implementation.

#pragma once

#include "UltraCanvasCommonTypes.h"

#include <string>
#include <vector>

namespace UltraCanvas {

// =============================================================================
// TYPES
// =============================================================================

enum class ClassLayoutKind {
    Manual,
    Layered,
    Tree,
    Grid
};

enum class ClassLayoutDirection {
    TopToBottom,
    BottomToTop,
    LeftToRight,
    RightToLeft
};

// One box to place. `width`/`height` come from the caller's measurement pass;
// `x`/`y` are written by the layout (and read as input when `pinned`).
struct ClassLayoutNode {
    std::string id;
    double width = 0.0;
    double height = 0.0;
    double x = 0.0;
    double y = 0.0;
    // Pinned nodes keep the x/y they came in with. Everything else is placed
    // around them (proposal L10: re-layout must not throw away the positions a
    // user dragged into place).
    bool pinned = false;

    ClassLayoutNode() = default;
    ClassLayoutNode(const std::string& nodeId, double w, double h)
        : id(nodeId), width(w), height(h) {}
};

struct ClassLayoutEdge {
    std::string sourceId;
    std::string targetId;
    // True for generalization/realization. Hierarchical edges define the
    // ranking; every edge influences ordering within a rank.
    bool hierarchical = false;

    ClassLayoutEdge() = default;
    ClassLayoutEdge(const std::string& source, const std::string& target, bool isHierarchical)
        : sourceId(source), targetId(target), hierarchical(isHierarchical) {}
};

struct ClassLayoutOptions {
    ClassLayoutKind kind = ClassLayoutKind::Layered;
    ClassLayoutDirection direction = ClassLayoutDirection::TopToBottom;

    // Gap between two boxes in the same rank (proposal L9).
    double nodeSeparation = 50.0;
    // Gap between one rank and the next.
    double rankSeparation = 80.0;
    // Offset of the whole drawing from the origin.
    double marginX = 40.0;
    double marginY = 40.0;

    // Barycentre sweeps used to reduce edge crossings. 0 keeps insertion order.
    int crossingReductionPasses = 4;

    // Grid layout only. 0 = pick a roughly square arrangement.
    int gridColumns = 0;
};

// =============================================================================
// LAYOUT
// =============================================================================

class UltraCanvasClassLayout {
public:
    // Places every non-pinned node and returns the bounding box of the result
    // (including pinned nodes). Node order in the vector is preserved; only
    // x/y are modified.
    //
    // Edges naming an id that is not in `nodes` are ignored, so a partially
    // resolved model still lays out.
    static Rect2Dd Apply(std::vector<ClassLayoutNode>& nodes,
                         const std::vector<ClassLayoutEdge>& edges,
                         const ClassLayoutOptions& options);

    // Bounding box of the nodes as they currently stand. Empty rect for an
    // empty input.
    static Rect2Dd ComputeBounds(const std::vector<ClassLayoutNode>& nodes);

    // Rank of every node under the Layered/Tree rules: 0 for a node with no
    // hierarchical parent, otherwise one more than its deepest parent. Exposed
    // because it is the interesting thing to assert in a test, and because a
    // caller may want to colour or group by depth.
    //
    // Cycles are broken deterministically: a node already on the current path
    // does not contribute to its own rank.
    static std::vector<int> ComputeRanks(const std::vector<ClassLayoutNode>& nodes,
                                         const std::vector<ClassLayoutEdge>& edges);
};

} // namespace UltraCanvas
