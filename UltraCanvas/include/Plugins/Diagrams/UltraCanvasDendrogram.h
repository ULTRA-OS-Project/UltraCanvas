// UltraCanvasDendrogram.h
// Interactive dendrogram / phylogenetic tree diagram element
// Version: 1.5.0
// Last Modified: 2026-07-31
// Author: UltraCanvas Framework
//
// CHANGELOG 1.5.0 (minor):
//  - NEW: Hierarchical edge bundling (Holten 2006). AddRelation() registers a
//         leaf-to-leaf association; each one is drawn as a spline routed through
//         the tree path source -> lowest common ancestor -> target, then relaxed
//         toward a straight chord by the bundling strength (beta). Relations are
//         a separate layer from the tree's own branches, so a radial dendrogram
//         can now show both its hierarchy and the cross-links between its leaves
//         without turning into a hairball.
//  - NEW: Area-proportional node dots. DendrogramNodeSizeMode::ByValue sizes
//         each dot from DendrogramNode::nodeValue, normalised against the
//         largest value in the tree and mapped through sqrt so AREA - not
//         radius - tracks the quantity. Replaces the previous behaviour where
//         style.leafNodeRadius was the only leaf size available.
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasTooltipManager.h"
#include "UltraCanvasScrollbar.h"
#include "Plugins/Diagrams/UltraCanvasDendrogramLayout.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <cmath>

namespace UltraCanvas {

// =============================================================================
// STYLE CONFIGURATION
// =============================================================================

    struct DendrogramStyle {
        // --- Branch rendering ---
        BranchColorMode branchColorMode = BranchColorMode::ByGroup;
        Color defaultBranchColor        = Color(120, 120, 120, 255); // Backbone / unassigned
        double defaultBranchWidth        = 1.5f;
        DendrogramLinkStyle linkStyle   = DendrogramLinkStyle::Rectangular;

        // --- Confidence display ---
        ConfidenceDisplayMode confidenceMode  = ConfidenceDisplayMode::None;
        double confidenceHighThreshold         = 0.80f; // Above: thick solid line
        double confidenceLowThreshold          = 0.40f; // Below: dashed line
        double branchWidthMin                  = 0.5f;
        double branchWidthMax                  = 3.5f;

        // --- Continuous value gradient (ByDistance / ByValue modes) ---
        Color gradientLow  = Color(24,  95, 165, 255); // Blue  (early / low)
        Color gradientHigh = Color(163, 45,  45, 255); // Red   (late  / high)

        // --- Leaf labels ---
        double  leafLabelFontSize  = 11.0f;
        bool   colorLeafLabels    = true;  // Apply group color to leaf text
        int    leafLabelPadding   = 5;     // Gap between line terminus and label
        Color  defaultLeafColor   = Color(40, 40, 40, 255);

        // --- Internal node clade labels (drawn beside node dots, clear of branch lines) ---
        // When true, draws the node's label string next to each internal node.
        // Position is chosen automatically to avoid overlapping the branch that connects
        // this node to its parent (pushed to the side that has no incoming branch).
        bool  showInternalNodeLabels    = false;
        double internalNodeLabelFontSize = 10.0f;
        Color internalNodeLabelColor    = Color(60, 60, 70, 255);
        int   internalNodeLabelOffset   = 6;    // Pixels from node dot to label edge

        // --- Label visibility mode ---
        // false = show all labels (if showInternalNodeLabels is true)
        // true = show label only when branch is clicked (interactive mode)
        bool  showLabelsOnClick        = true;

        // --- Label background (pill/rect behind all text for readability) ---
        // Applies to leaf labels, internal node labels, and clade annotations.
        bool  showLabelBackground    = true;
        Color labelBackgroundColor   = Color(255, 255, 255, 210); // Semi-opaque white
        Color labelBackgroundBorder  = Color(200, 200, 210, 120);
        int   labelBackgroundPadding = 3;   // px padding around text in background rect
        double labelBackgroundRadius  = 3.0f; // Corner radius

        // --- Node dots ---
        double internalNodeRadius = 3.0f;
        double leafNodeRadius     = 2.0f;
        bool  showInternalNodes  = true;
        bool  showLeafNodes      = true;
        Color internalNodeColor  = Color(100, 100, 100, 255);

        // --- Value-driven node radius (NEW in 1.5.0) ---
        // In ByValue mode a node's dot area is proportional to its
        // DendrogramNode::nodeValue, normalised against the largest value found
        // in the tree, then mapped onto [nodeRadiusMin, nodeRadiusMaxValue].
        // Nodes with nodeValue < 0 (unset) keep their fixed radius, so a tree
        // can mix sized and unsized nodes.
        DendrogramNodeSizeMode nodeSizeMode = DendrogramNodeSizeMode::Fixed;
        double nodeRadiusMin      = 2.0f;   // Radius at value 0
        double nodeRadiusMaxValue = 22.0f;  // Radius at the largest value in the tree
        bool   sizeLeafNodesByValue     = true;  // Apply ByValue sizing to leaves
        bool   sizeInternalNodesByValue = true;  // Apply ByValue sizing to internal nodes

        // --- Hierarchical edge bundling (NEW in 1.5.0) ---
        bool  showRelations = true;
        // Bundling strength (Holten's beta). 1.0 = follow the tree path exactly
        // (maximum bundling); 0.0 = a straight chord between the two leaves (no
        // bundling at all). 0.85 is the value Holten recommends and reads well
        // on radial layouts.
        double bundlingStrength = 0.85f;
        // Samples per spline. Higher is smoother and slower; 32 is plenty for
        // on-screen work, drop to 16 for very large relation sets.
        int    relationSegments = 32;
        double relationWidth    = 1.0f;  // Fallback width when relation.width <= 0
        // Draw relations under the tree branches (true, matches the classic
        // look where trunks sit on top of the wispy bundles) or over them.
        bool   relationsBelowBranches = true;
        // Extra alpha multiplier applied to every relation, 0-1. Lets an app dim
        // the whole bundle layer without touching per-relation colors.
        double relationOpacity = 1.0f;
        // When a leaf is hovered or selected, dim every relation that does not
        // touch it to this alpha multiplier. 1.0 disables the effect.
        double relationDimOpacity = 1.0f;

        // --- Node radius scaling by depth ---
        bool  scaleNodesByDepth      = false;
        double nodeRadiusDepthScale   = 1.2f;
        double nodeRadiusMax          = 10.0f;

        // --- Per-depth node color palette ---
        bool                colorNodesByDepth = false;
        std::vector<Color>  nodeDepthPalette  = {
            Color(30,  30,  30,  255),
            Color(100, 80,  160, 255),
            Color(200, 80,  130, 255),
            Color(230, 130, 80,  255),
        };

        // --- Per-group node color ---
        bool colorLeafNodesByGroup = true;

        // --- Internal node annotation box ---
        bool  showNodeAnnotations   = false;
        double annotationFontSize    = 9.0f;
        Color annotationBgColor     = Color(255, 255, 220, 230);
        Color annotationTextColor   = Color(40, 40, 40, 255);
        Color annotationBorderColor = Color(180, 180, 100, 255);

        // --- Axis ---
        bool  showDistanceAxis = true;
        int   axisTickCount    = 5;
        double axisFontSize     = 10.0f;
        Color axisColor        = Color(160, 160, 160, 255);
        Color gridColor        = Color(225, 225, 225, 255);
        bool  showGrid         = true;

        // --- Group label sidebar ---
        bool showGroupLabels     = true;
        int  sidebarBracketWidth = 5;    // Width of the colored bracket bar (px)
        int  sidebarBracketGap   = 6;    // Gap from bracket to leaf label column
        int  floatingBadgePad    = 5;    // Padding inside floating badge rects
        double arcLabelRadiusMul  = 1.10f; // Multiplier of tree radius for arc labels

        // --- Background ---
        Color backgroundColor = Color(255, 255, 255, 255);

        // --- Margins (pixels) ---
        int marginTop    = 20;
        int marginBottom = 40; // Space for distance axis
        int marginLeft   = 20;
        int marginRight  = 20;

        // --- Layout ---
        double leafSpacing = 18.0f; // Minimum pixels between adjacent leaves
    };

// =============================================================================
// MAIN ELEMENT CLASS
// =============================================================================

    class UltraCanvasDendrogram : public UltraCanvasUIElement {
    public:
        // =====================================================================
        // CALLBACKS
        // =====================================================================

        std::function<void(const std::string&)> onLeafClicked;
            // @param nodeId — clicked leaf node

        std::function<void(const std::string&)> onInternalNodeClicked;
            // @param nodeId — clicked internal (merge) node

        std::function<void(const std::string&)> onNodeHovered;
            // @param nodeId — hovered node (any type), empty string = no hover

        std::function<void(const std::string&)> onGroupClicked;
            // @param groupId — user clicked a group region or label

        std::function<void(const std::string&, bool)> onGroupHighlighted;
            // @param groupId, isEntering — hover enter/leave on group

        std::function<void(const std::vector<std::string>&)> onSelectionChanged;
            // @param selectedLeafIds — set changed after click with shift/ctrl

        std::function<void(const std::string&, bool)> onNodeCollapsed;
            // @param nodeId — internal node collapsed (true) or expanded (false)

    public:
        // =====================================================================
        // CONSTRUCTOR / DESTRUCTOR
        // =====================================================================

        UltraCanvasDendrogram(const std::string& id,
                              int x, int y, int width, int height);
        ~UltraCanvasDendrogram() override = default;

        // =====================================================================
        // DATA
        // =====================================================================

        void SetDataSource(std::shared_ptr<IDendrogramDataSource> data);
        std::shared_ptr<IDendrogramDataSource> GetDataSource() const { return dataSource; }

        // =====================================================================
        // LAYOUT CONTROL
        // =====================================================================

        void SetOrientation(DendrogramOrientation o);
        DendrogramOrientation GetOrientation() const { return orientation; }

        void SetScaleMode(DendrogramScaleMode m);
        DendrogramScaleMode GetScaleMode() const { return scaleMode; }

        void SetLeafSpacing(double px);

        // =====================================================================
        // STYLE
        // =====================================================================

        void SetStyle(const DendrogramStyle& s);
        const DendrogramStyle& GetStyle() const { return style; }

        void SetBranchColorMode(BranchColorMode m);
        void SetLinkStyle(DendrogramLinkStyle l);
        void SetConfidenceMode(ConfidenceDisplayMode m);

        // =====================================================================
        // NODE SIZING (NEW in 1.5.0)
        // =====================================================================

        void SetNodeSizeMode(DendrogramNodeSizeMode m);
        DendrogramNodeSizeMode GetNodeSizeMode() const { return style.nodeSizeMode; }

        // Radius range used by DendrogramNodeSizeMode::ByValue.
        void SetNodeRadiusRange(double minRadius, double maxRadius);

        // Largest nodeValue found in the current data source, or 0 when no node
        // carries a value. Exposed so apps can label a size legend.
        double GetMaxNodeValue() const { return maxNodeValue; }

        // =====================================================================
        // HIERARCHICAL EDGE BUNDLING (NEW in 1.5.0)
        // =====================================================================

        void AddRelation(const DendrogramRelation& relation);
        void AddRelation(const std::string& sourceLeafId, const std::string& targetLeafId);
        void SetRelations(const std::vector<DendrogramRelation>& relations);
        void ClearRelations();
        size_t GetRelationCount() const { return relations.size(); }
        const std::vector<DendrogramRelation>& GetRelations() const { return relations; }

        void SetRelationsVisible(bool visible);
        bool AreRelationsVisible() const { return style.showRelations; }

        // Holten's beta: 1.0 hugs the tree (maximum bundling), 0.0 draws straight
        // chords. Values outside 0-1 are clamped.
        void SetBundlingStrength(double beta);
        double GetBundlingStrength() const { return style.bundlingStrength; }

        // =====================================================================
        // SELECTION
        // =====================================================================

        void SelectNode(const std::string& nodeId, bool addToSelection = false);
        void ClearSelection();
        const std::vector<std::string>& GetSelectedLeafIds() const { return selectedLeafIds; }

        // =====================================================================
        // ZOOM / PAN
        // =====================================================================

        void SetZoom(double z);
        double GetZoom() const { return zoom; }
        void ResetView();

        // Custom node positions (for drag & drop repositioning)
        void ClearCustomPositions();
        void ResetNodePositions();

        // Collapse / expand an internal node (double-click toggles)
        void ToggleCollapse(const std::string& nodeId);
        bool IsCollapsed(const std::string& nodeId) const;
        void ExpandAll();
        void CollapseAll();

        // =====================================================================
        // RENDERING & EVENTS (UltraCanvasUIElement overrides)
        // =====================================================================

        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;

    private:
        // =====================================================================
        // INTERNAL STATE
        // =====================================================================

        std::shared_ptr<IDendrogramDataSource> dataSource;
        DendrogramLayoutEngine layoutEngine;
        DendrogramLayout       layout;
        DendrogramStyle        style;

        DendrogramOrientation orientation = DendrogramOrientation::LeftRight;
        DendrogramScaleMode   scaleMode   = DendrogramScaleMode::Proportional;

        bool layoutDirty = true;
        // Size used by the last RebuildLayout(); lets Render() detect a resize
        // from a flex/stretch parent (which never flips layoutDirty) and re-fit.
        float lastLayoutWidth  = -1.0f;
        float lastLayoutHeight = -1.0f;

        // Zoom / pan
        double   zoom      = 1.0f;
        Point2Dd panOffset = {0.0f, 0.0f};
        bool     isPanning = false;
        Point2Di lastPanMouse;

        // Interaction state
        std::string hoveredNodeId;
        std::string hoveredGroupId;
        std::string hoveredBranchParentId;  // For branch hover highlight
        std::vector<std::string> selectedLeafIds;
        std::unordered_set<std::string> collapsedNodes; // Internal nodes that are collapsed
        std::unordered_set<std::string> visibleBranchLabels; // Internal node labels visible on click
        std::chrono::steady_clock::time_point lastClickTime;
        int lastClickX = 0, lastClickY = 0;

        // Node drag state
        std::string draggingNodeId;
        Point2Di dragStartPos;
        Point2Dd originalNodePos;
        std::unordered_map<std::string, Point2Dd> customNodePositions; // Manual node positions

        // Scrollbars (for large trees)
        std::shared_ptr<UltraCanvasScrollbar> vertScrollbar;
        std::shared_ptr<UltraCanvasScrollbar> horzScrollbar;
        int scrollX = 0;
        int scrollY = 0;

        // Cached group map: leafId → groupId (built from data source on SetDataSource)
        std::unordered_map<std::string, std::string> leafGroupMap;

        // --- Edge bundling (1.5.0) ---
        std::vector<DendrogramRelation> relations;
        // childId → parentId, rebuilt with the layout. Needed to walk a leaf up
        // to the root when resolving the lowest common ancestor.
        std::unordered_map<std::string, std::string> parentMap;

        // --- Value sizing (1.5.0) ---
        // Largest DendrogramNode::nodeValue in the data source, cached on
        // SetDataSource so ResolveNodeRadius stays O(1) per node.
        double maxNodeValue = 0.0;

        // =====================================================================
        // LAYOUT
        // =====================================================================

        void RebuildLayout();
        Rect2Dd GetTreeBounds() const; // Inner area after margins
        void BuildLeafGroupMap();
        void BuildParentMap();      // NEW in 1.5.0 — childId → parentId
        void ComputeMaxNodeValue(); // NEW in 1.5.0 — cache for ByValue sizing

        // =====================================================================
        // RENDERING PASSES  (called in order inside Render())
        // =====================================================================

        void RenderBackground(IRenderContext* ctx);
        void RenderGroupFills(IRenderContext* ctx);       // Strategy 2: sector/band fills
        void RenderAxis(IRenderContext* ctx);              // Distance axis + grid
        void RenderRelations(IRenderContext* ctx);         // 1.5.0: bundled leaf-to-leaf splines
        void RenderBranches(IRenderContext* ctx);          // All branch lines
        void RenderNodeDots(IRenderContext* ctx);          // Node circles
        void RenderLeafLabels(IRenderContext* ctx);        // Leaf text
        void RenderNodeAnnotations(IRenderContext* ctx);   // Internal node annotation boxes
        void RenderInternalNodeLabels(IRenderContext* ctx); // Clade name labels beside node dots
        void RenderGroupLabels(IRenderContext* ctx);       // Strategy 3: group name labels
        void RenderGroupSidebarBrackets(IRenderContext* ctx);
        void RenderGroupFloatingBadges(IRenderContext* ctx);
        void RenderGroupArcLabels(IRenderContext* ctx);
        void RenderScrollbars(IRenderContext* ctx);
        void RenderEmptyState(IRenderContext* ctx);

        // =====================================================================
        // BRANCH DRAWING HELPERS
        // =====================================================================

        void DrawBranch(IRenderContext* ctx,
                        const DendrogramLayoutNode& parent,
                        const DendrogramLayoutNode& child,
                        const Color& branchColor,
                        float branchWidth,
                        bool dashed);

        void DrawBranchRectangular(IRenderContext* ctx,
                                   float px, float py,
                                   float cx, float cy,
                                   float crossbarY,
                                   const Color& color,
                                   float width, bool dashed);

        void DrawBranchCurved(IRenderContext* ctx,
                              float px, float py,
                              float cx, float cy,
                              const Color& color,
                              float width);

        // Radial-specific curved branch: angular arc sweep from parent to child
        void DrawBranchCurvedRadial(IRenderContext* ctx,
                                    const DendrogramLayoutNode& parent,
                                    const DendrogramLayoutNode& child,
                                    const Color& color,
                                    float width);

        void DrawBranchDiagonal(IRenderContext* ctx,
                                float px, float py,
                                float cx, float cy,
                                const Color& color, float width);

        // =====================================================================
        // EDGE BUNDLING HELPERS (NEW in 1.5.0)
        // =====================================================================

        // Node ids from `leafId` up to the root, inclusive of both ends.
        std::vector<std::string> PathToRoot(const std::string& leafId) const;

        // Full tree path source -> lowest common ancestor -> target, as pixel
        // control points. Empty when either endpoint is missing or hidden.
        std::vector<Point2Dd> BuildRelationControlPoints(const DendrogramRelation& relation) const;

        // Holten's straightening: pull each interior control point toward the
        // straight line between the endpoints by (1 - beta).
        void ApplyBundlingStrength(std::vector<Point2Dd>& controlPoints, double beta) const;

        // Sample a clamped uniform cubic B-spline through the control points.
        static std::vector<Point2Dd> SampleBSpline(const std::vector<Point2Dd>& controlPoints,
                                                    int segments);

        // Returns style.defaultBranchWidth — needed internally before confidence lookup
        double DefaultBranchWidth() const { return style.defaultBranchWidth; }

        // =====================================================================
        // COLOR HELPERS
        // =====================================================================

        Color ResolveBranchColor(const DendrogramLayoutNode& node) const;
        Color ResolveNodeColor(const DendrogramLayoutNode& node) const;
        float ResolveNodeRadius(const DendrogramLayoutNode& node) const;
        Color InterpolateGradient(float t) const;
        float ComputeBranchWidth(float confidence) const;
        bool  ComputeBranchDash(float confidence) const;

        // =====================================================================
        // COORDINATE HELPERS
        // =====================================================================

        // Convert layout coordinates to pixel coordinates (with pan/zoom applied)
        Point2Dd ToPixel(double primary, double depth) const;
        Point2Dd ToPixelRadial(double angle, double radius) const;

        // Hit-test: find node at pixel position; returns empty string if none
        std::string HitTestNode(int px, int py) const;
        std::string HitTestGroup(int px, int py) const;
        std::string HitTestBranch(int mx, int my) const; // Returns parent nodeId of nearest branch

        // Returns false if node or any ancestor is collapsed
        bool IsSubtreeVisible(const std::string& nodeId) const;

        // =====================================================================
        // EVENT HELPERS
        // =====================================================================

        bool HandleMouseDown(const UCEvent& event);
        bool HandleMouseUp(const UCEvent& event);
        bool HandleMouseMove(const UCEvent& event);
        bool HandleMouseWheel(const UCEvent& event);
    };

} // namespace UltraCanvas
