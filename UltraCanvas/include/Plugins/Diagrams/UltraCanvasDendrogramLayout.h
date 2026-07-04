// UltraCanvasDendrogramLayout.h
// Reingold-Tilford layout engine for dendrogram tree positioning
// Version: 1.1.0
// Last Modified: 2026-04-02
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasRenderContext.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <cmath>
#include <algorithm>
#include <memory>

#undef None
namespace UltraCanvas {

// =============================================================================
// ENUMS
// =============================================================================

    enum class DendrogramOrientation {
        TopDown,    // Root at top, leaves at bottom
        BottomUp,   // Root at bottom, leaves at top
        LeftRight,  // Root at left, leaves at right
        RightLeft,  // Root at right, leaves at left
        Radial      // Root at centre, leaves on circumference
    };

    enum class DendrogramScaleMode {
        Proportional, // Branch heights proportional to mergeDistance
        Cladogram     // All leaves equidistant — topology only
    };

    enum class DendrogramLinkStyle {
        Rectangular,  // Right-angle elbow connectors
        Curved,       // Smooth cubic Bezier curves
        Diagonal      // Straight diagonal lines
    };

    enum class BranchColorMode {
        Uniform,    // All branches one color (DendrogramStyle::defaultBranchColor)
        ByGroup,    // Color from DendrogramGroup assignment
        ByDistance, // Gradient mapped to merge distance
        ByValue,    // Gradient mapped to DendrogramNode::branchValue
        Custom      // Per-node override via DendrogramNode::customBranchColor
    };

    enum class GroupLabelPlacement {
        None,           // Group labels not shown
        SidebarBracket, // Vertical bracket beside tree (horizontal layouts)
        FloatingBadge,  // Text badge centered over clade bounding box
        ArcText,        // Rotated text on circular layout perimeter
        LeaderCallout   // External label with leader line
    };

    enum class GroupFillMode {
        None,       // No background fill
        BandFill,   // Semi-transparent rect band (horizontal/vertical layouts)
        SectorFill, // Pie-slice sector (radial/circular layouts)
        ConvexHull  // Convex hull polygon around all group nodes
    };

    enum class ConfidenceDisplayMode {
        None,                // Not shown
        LineThickness,       // Stroke width mapped from confidence value
        LineDash,            // Dashed line below threshold
        NodeDot,             // Filled/hollow dot on internal nodes
        LineThicknessAndDot  // Both combined
    };

// =============================================================================
// DATA STRUCTURES
// =============================================================================

    // Input node — provided by user
    struct DendrogramNode {
        std::string id;
        std::string label;
        double mergeDistance = 0.0;   // 0 for leaves
        std::string groupId;          // Assigns leaf to a DendrogramGroup

        float confidence   = 1.0f;   // 0–1; controls branch thickness/dash/dot
        float branchValue  = -1.0f;  // -1 = unset; 0–1 for gradient encoding
        Color customBranchColor = Colors::Transparent; // Only used in Custom mode

        std::string nodeAnnotation;   // Short annotation shown on internal nodes
        std::string tooltip;
        void* userData = nullptr;

        std::vector<std::string> childIds;

        bool IsLeaf() const { return childIds.empty(); }
    };

    // Group definition — defines a named clade and its visual treatment
    struct DendrogramGroup {
        std::string groupId;
        std::string label;
        std::vector<std::string> leafIds; // Leaf nodeIds that belong to this group

        Color branchColor = Color(80, 80, 200, 255);
        Color labelColor  = Color(60, 60, 180, 255);
        Color fillColor   = Color(80, 80, 200, 40);

        GroupLabelPlacement labelPlacement = GroupLabelPlacement::SidebarBracket;
        GroupFillMode       fillMode       = GroupFillMode::BandFill;
        float fillOpacity = 0.15f;

        bool  showGroupLabel     = true;
        float groupLabelFontSize = 12.0f;
        FontWeight groupLabelWeight = FontWeight::Bold;
    };

    // Computed layout node — output of the layout engine
    struct DendrogramLayoutNode {
        std::string id;
        float primaryPos  = 0.0f; // Position along leaf axis (X for TopDown)
        float depthPos    = 0.0f; // Position along depth axis (Y for TopDown)
        float mergeDepth  = 0.0f; // Depth of the crossbar for internal nodes
        int   depth       = 0;
        bool  isLeaf      = false;
        std::string groupId;
        std::vector<std::string> childIds;

        // Pixel-space coordinates (computed from primaryPos/depthPos after scaling)
        float px = 0.0f;
        float py = 0.0f;

        // For radial layout
        float angle  = 0.0f; // Angle in radians
        float radius = 0.0f; // Radius from centre
    };

    // Result of a full layout pass
    struct DendrogramLayout {
        std::vector<DendrogramLayoutNode> nodes;
        std::unordered_map<std::string, size_t> nodeIndex; // id → index in nodes

        float totalWidth  = 0.0f;
        float totalHeight = 0.0f;
        float maxDistance = 0.0f; // Maximum merge distance found in tree

        // Pixel mapping helpers (set by layout engine based on bounds + orientation)
        float primaryScale = 1.0f; // pixels per primaryPos unit
        float depthScale   = 1.0f; // pixels per depthPos unit
        float originX      = 0.0f;
        float originY      = 0.0f;

        const DendrogramLayoutNode* FindNode(const std::string& id) const {
            auto it = nodeIndex.find(id);
            if (it == nodeIndex.end()) return nullptr;
            return &nodes[it->second];
        }

        DendrogramLayoutNode* FindNode(const std::string& id) {
            auto it = nodeIndex.find(id);
            if (it == nodeIndex.end()) return nullptr;
            return &nodes[it->second];
        }
    };

// =============================================================================
// DATA SOURCE INTERFACE
// =============================================================================

    class IDendrogramDataSource {
    public:
        virtual ~IDendrogramDataSource() = default;

        virtual size_t GetNodeCount() const = 0;
        virtual const DendrogramNode* GetNode(size_t index) const = 0;
        virtual const DendrogramNode* GetNodeById(const std::string& id) const = 0;
        virtual std::string GetRootId() const = 0;

        // Optional: enumerate groups
        virtual size_t GetGroupCount() const { return 0; }
        virtual const DendrogramGroup* GetGroup(size_t index) const { return nullptr; }
        virtual const DendrogramGroup* GetGroupById(const std::string& id) const { return nullptr; }
    };

    // Concrete vector-based data source
    class DendrogramDataVector : public IDendrogramDataSource {
    public:
        void AddNode(const DendrogramNode& node) {
            nodeIndex[node.id] = nodes.size();
            nodes.push_back(node);
        }

        void AddGroup(const DendrogramGroup& group) {
            groupIndex[group.groupId] = groups.size();
            groups.push_back(group);
        }

        void SetRootId(const std::string& id) { rootId = id; }

        size_t GetNodeCount() const override { return nodes.size(); }

        const DendrogramNode* GetNode(size_t index) const override {
            if (index >= nodes.size()) return nullptr;
            return &nodes[index];
        }

        const DendrogramNode* GetNodeById(const std::string& id) const override {
            auto it = nodeIndex.find(id);
            if (it == nodeIndex.end()) return nullptr;
            return &nodes[it->second];
        }

        std::string GetRootId() const override { return rootId; }

        size_t GetGroupCount() const override { return groups.size(); }

        const DendrogramGroup* GetGroup(size_t index) const override {
            if (index >= groups.size()) return nullptr;
            return &groups[index];
        }

        const DendrogramGroup* GetGroupById(const std::string& id) const override {
            auto it = groupIndex.find(id);
            if (it == groupIndex.end()) return nullptr;
            return &groups[it->second];
        }

    private:
        std::vector<DendrogramNode> nodes;
        std::unordered_map<std::string, size_t> nodeIndex;
        std::vector<DendrogramGroup> groups;
        std::unordered_map<std::string, size_t> groupIndex;
        std::string rootId;
    };

// =============================================================================
// LAYOUT ENGINE
// =============================================================================

    class DendrogramLayoutEngine {
    public:
        // Compute layout for all nodes in the data source.
        // bounds: pixel area available for the tree (excluding margin for labels).
        // orientation / scaleMode control the layout algorithm variant.
        DendrogramLayout Compute(
            const IDendrogramDataSource* data,
            const Rect2Dd& bounds,
            DendrogramOrientation orientation,
            DendrogramScaleMode scaleMode,
            float leafSpacing = 18.0f);  // Min pixels between adjacent leaves

    private:
        // --- Reingold-Tilford internal structures ---
        struct RTNode {
            std::string id;
            float prelim  = 0.0f;  // Preliminary x position
            float mod     = 0.0f;  // Modifier for subtree shift
            float shift   = 0.0f;
            float change  = 0.0f;
            int   number  = 0;     // Index among siblings
            RTNode* parent    = nullptr;
            RTNode* leftSib   = nullptr;
            RTNode* ancestor  = nullptr;
            std::vector<std::unique_ptr<RTNode>> children;
            double mergeDistance = 0.0;
            bool   isLeaf = false;

            RTNode* LeftmostChild()  const { return children.empty() ? nullptr : children.front().get(); }
            RTNode* RightmostChild() const { return children.empty() ? nullptr : children.back().get(); }
        };

        // Build RT tree from data source
        std::unique_ptr<RTNode> BuildRTTree(
            const IDendrogramDataSource* data,
            const std::string& nodeId,
            RTNode* parent,
            int& leafCount);

        // Reingold-Tilford passes
        void FirstWalk(RTNode* node, float leafSpacing);
        void AssignLeafPositions(RTNode* node, float leafSpacing, int& leafCounter);
        void SecondWalk(RTNode* node, float mod, float& minX, DendrogramLayout& layout, double maxDist, float depthScale, float originX, float originY, DendrogramOrientation orient, DendrogramScaleMode scaleMode, int totalLeaves, float leafSpacingPx, int depth);
        void Apportion(RTNode* node, RTNode*& defaultAncestor, float leafSpacing);
        void ExecuteShifts(RTNode* node);
        void MoveSubtree(RTNode* wm, RTNode* wp, float shift);

        RTNode* Ancestor(RTNode* vil, RTNode* node, RTNode* defaultAncestor);
        RTNode* NextLeft(RTNode* node);
        RTNode* NextRight(RTNode* node);

        // Compute max merge distance in tree
        double FindMaxDistance(const IDendrogramDataSource* data, const std::string& nodeId);

        // Convert Cartesian layout to radial
        void ApplyRadialLayout(DendrogramLayout& layout, const Rect2Dd& bounds);
    };

} // namespace UltraCanvas
