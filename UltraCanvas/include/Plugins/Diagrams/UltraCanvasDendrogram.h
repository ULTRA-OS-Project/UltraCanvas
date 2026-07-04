// UltraCanvasDendrogram.h
// Interactive dendrogram / phylogenetic tree diagram element
// Version: 1.4.2
// Last Modified: 2026-06-05
// Author: UltraCanvas Framework
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

        // =====================================================================
        // LAYOUT
        // =====================================================================

        void RebuildLayout();
        Rect2Dd GetTreeBounds() const; // Inner area after margins
        void BuildLeafGroupMap();

        // =====================================================================
        // RENDERING PASSES  (called in order inside Render())
        // =====================================================================

        void RenderBackground(IRenderContext* ctx);
        void RenderGroupFills(IRenderContext* ctx);       // Strategy 2: sector/band fills
        void RenderAxis(IRenderContext* ctx);              // Distance axis + grid
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
