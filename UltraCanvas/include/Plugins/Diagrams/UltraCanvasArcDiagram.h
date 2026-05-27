// UltraCanvasArcDiagram.h
// Arc diagram — nodes on a baseline, edges as cubic Bezier arcs above/below
// Version: 1.0.2
// Last Modified: 2026-05-17
// Author: UltraCanvas Framework
// Changes: P1 degree sizing, P2 vertical labels, P3 opacity weight, P4 semicircle mode,
//          P5 axis arrow, P6 mid-arc arrowhead, P7 self-loops, P8 parallel bundles,
//          P9 span z-order, P10 color legend,
//          P11 connected-edge highlight on node hover/select,
//          P12 auto value/percentage label at arc apex (zenit),
//          P13 focus mode — dim non-involved nodes/arcs/labels to a user-defined gray

#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasCommonTypes.h"
#include <vector>
#include <string>
#include <functional>
#include <algorithm>
#include <cmath>
#include <limits>

#ifdef Below
#undef Below
#endif
#ifdef Above
#undef Above
#endif
#ifdef None
#undef None   // X11 defines `None` as 0L — collides with ArcValueDisplay::None
#endif

namespace UltraCanvas {

// ===== ENUMERATIONS =====

    /// Which side of the baseline arcs are drawn on
    enum class ArcEdgeType {
        Above,          ///< Arc curves above the baseline (forward links, default)
        Below,          ///< Arc curves below the baseline (back links, secondary)
        Auto            ///< Above when source < target index, below otherwise
    };

    /// Where node labels are positioned relative to the node dot
    enum class ArcLabelSide {
        Below,          ///< Labels below the baseline (default)
        Above,          ///< Labels above the baseline
        Alternating,    ///< Alternates above/below by index — reduces overlap for dense graphs
        Vertical        ///< P2 — labels rotated 90° descending below baseline (image 1 style)
    };

    /// Controls how arc height (curvature) scales with edge span
    enum class ArcHeightMode {
        Proportional,   ///< Arc height proportional to distance between nodes (default)
        Fixed,          ///< All arcs same fixed height regardless of span
        Sqrt,           ///< Arc height = sqrt(distance) × scale — compresses long arcs
        Semicircle      ///< P4 — perfect D-shape: height = span/2 (images 1, 2, 4 style)
    };

    /// Axis orientation of the node baseline
    enum class ArcOrientation {
        Horizontal,     ///< Nodes laid out left-to-right (default)
        Vertical        ///< Nodes laid out top-to-bottom
    };

    /// P6 — where arrowheads are placed on directed edges
    enum class ArcArrowPosition {
        AtTarget,       ///< Arrowhead at the target node end (default)
        AtMidArc        ///< Arrowhead at the arc apex midpoint (image 3 style)
    };

    /// P1 — how node radius is determined
    enum class ArcNodeSizeMode {
        Fixed,          ///< All nodes use ArcNode.size directly (default)
        ByValue,        ///< size = baseSize * sqrt(node.value)
        ByDegree        ///< P1 — size = baseSize * sqrt(connectionCount) — image 1 style
    };

    /// P12 — what to render automatically at the arc apex (zenit)
    enum class ArcValueDisplay {
        None,           ///< No auto-generated apex label (default; honors edge.label)
        Value,          ///< Show edge weight as numeric value
        Percentage      ///< Show edge weight as percentage of total edge weight
    };

// ===== DATA STRUCTURES =====

    /// P10 — one entry in the color legend
    struct ArcLegendEntry {
        std::string label;                              ///< Legend item label
        Color       color = Color(100, 149, 237, 200);  ///< Swatch color
    };

    /// A single node on the arc diagram baseline
    struct ArcNode {
        std::string id;                                 ///< Unique identifier
        std::string label;                              ///< Display label
        float       size        = 6.0f;                ///< Dot radius in pixels
        Color       color       = Color(70, 130, 180, 255); ///< Node fill color
        float       value       = 1.0f;                ///< Optional value (drives size scaling)
        bool        scaleByValue = false;              ///< If true, size = baseSize * sqrt(value)
    };

    /// A directed edge between two nodes
    struct ArcEdge {
        std::string sourceId;                           ///< Source node ID
        std::string targetId;                           ///< Target node ID
        float       weight      = 1.0f;                ///< Edge weight (drives stroke width)
        Color       color       = Color(100, 149, 237, 180); ///< Arc stroke color
        ArcEdgeType side        = ArcEdgeType::Auto;   ///< Which side of baseline
        std::string label;                             ///< Optional arc label (shown at apex)
        bool        directed    = false;               ///< Show arrowhead at target
    };

    /// Visual styling for the arc diagram
    struct ArcDiagramStyle {
        // Baseline
        Color   baselineColor       = Color(180, 180, 180, 255);    ///< Baseline stroke color
        float   baselineWidth       = 1.0f;                         ///< Baseline stroke width
        bool    showBaseline        = true;                         ///< Draw the node baseline

        // Nodes
        float   nodeBaseSize        = 6.0f;     ///< Default node dot radius
        float   nodeStrokeWidth     = 1.0f;     ///< Node outline width
        Color   nodeStrokeColor     = Color(255, 255, 255, 200);    ///< Node outline color
        Color   nodeHoverColor      = Color(255, 200, 50, 255);     ///< Node hover highlight
        Color   nodeSelectedColor   = Color(255, 120, 0, 255);      ///< Selected node color

        // Labels
        float   labelFontSize       = 11.0f;    ///< Node label font size
        Color   labelColor          = Color(40, 40, 40, 255);       ///< Node label text color
        float   labelMargin         = 6.0f;     ///< Gap between node dot and label
        ArcLabelSide labelSide      = ArcLabelSide::Below;

        // Arcs
        float   arcMinWidth         = 0.5f;     ///< Minimum arc stroke width
        float   arcMaxWidth         = 6.0f;     ///< Maximum arc stroke width (at max weight)
        float   arcHeightScale      = 0.5f;     ///< Arc height as fraction of baseline span
        float   arcFixedHeight      = 60.0f;    ///< Height used when ArcHeightMode::Fixed
        ArcHeightMode arcHeightMode = ArcHeightMode::Proportional;
        bool    showArrows          = true;     ///< Draw arrowheads on directed edges
        float   arrowSize           = 8.0f;     ///< Arrowhead size in pixels

        // P1 — node sizing mode
        ArcNodeSizeMode nodeSizeMode = ArcNodeSizeMode::Fixed;

        // P3 — opacity weight encoding (applied alongside width)
        bool    arcEncodeOpacity    = false;    ///< If true, alpha also scales with weight
        uint8_t arcMinOpacity       = 40;       ///< Alpha at minimum weight (0–255)
        uint8_t arcMaxOpacity       = 220;      ///< Alpha at maximum weight (0–255)

        // P5 — axis arrow extending past last node
        bool    showAxisArrow       = false;    ///< Extend baseline with an arrowhead past last node
        float   axisArrowOverhang   = 20.0f;   ///< How far past the last node the axis extends

        // P6 — arrowhead placement
        ArcArrowPosition arrowPosition = ArcArrowPosition::AtTarget;

        // P8 — parallel edge bundle offset
        float   bundleHeightStep    = 8.0f;    ///< Extra height added per parallel edge in a bundle

        // P9 — z-order by span (short arcs on top)
        bool    sortBySpan          = false;    ///< If true, draw longer arcs first (shorter on top)

        // P10 — color legend
        bool    showLegend          = false;    ///< Draw a color legend
        std::vector<ArcLegendEntry> legendEntries; ///< Legend items (top-to-bottom)
        float   legendX             = 20.0f;   ///< Legend top-left X (element-local)
        float   legendY             = 20.0f;   ///< Legend top-left Y (element-local)
        float   legendFontSize      = 11.0f;   ///< Legend text font size
        Color   legendTextColor     = Color(40, 40, 40, 255);

        // Arc label
        float   arcLabelFontSize    = 10.0f;    ///< Arc apex label font size
        Color   arcLabelColor       = Color(80, 80, 80, 220);       ///< Arc label color

        // P11 — highlight edges connected to hovered/selected node
        bool    highlightConnectedEdges = true; ///< Brighten arcs touching the active node

        // P13 — focus mode: gray out everything not involved with the active node
        bool    dimUnconnected      = true;     ///< When a node is active, fade non-involved geometry
        Color   dimmedColor         = Color(200, 200, 200, 140);    ///< Replacement color for dimmed arcs/nodes/labels
        bool    emphasizeActiveLabel = true;    ///< Draw active node's label in bold weight

        // P12 — auto value/percentage label drawn at arc apex (zenit)
        ArcValueDisplay arcValueDisplay  = ArcValueDisplay::None;
        int     arcValueDecimals    = 1;        ///< Decimal places for value/percentage text
        float   arcValueFontSize    = 10.0f;    ///< Font size for apex value label
        Color   arcValueLabelColor  = Color(40, 40, 40, 230);   ///< Apex value label color
        float   arcValueLabelOffset = 6.0f;     ///< Gap between arc apex and value label

        // Tooltip
        bool    showTooltip         = true;     ///< Show tooltip on hover
        float   tooltipFontSize     = 11.0f;    ///< Tooltip font size
        Color   tooltipBackground   = Color(50, 50, 50, 230);       ///< Tooltip bg
        Color   tooltipText         = Color(255, 255, 255, 255);    ///< Tooltip text

        // Spacing
        float   nodePadding         = 40.0f;    ///< Pixel spacing between adjacent nodes
        float   marginH             = 40.0f;    ///< Horizontal margin (left/right)
        float   marginV             = 40.0f;    ///< Vertical margin (top/bottom for labels)

        ArcOrientation orientation  = ArcOrientation::Horizontal;
    };

// ===== MAIN CLASS =====

    /**
     * @brief Arc diagram element
     *
     * Renders nodes on a straight baseline with edges drawn as cubic Bezier
     * arcs above and/or below. Arc height encodes distance or is fixed.
     * Arc width (and optionally opacity) encodes edge weight.
     *
     * Features:
     * - Nodes on horizontal or vertical baseline
     * - Cubic Bezier arcs above/below baseline
     * - Arc height: proportional, fixed, sqrt-scaled, or strict semicircle (P4)
     * - Arc width + optional opacity scales with edge weight (P3)
     * - Node sizing: fixed, by value, or by degree count (P1)
     * - Directed arrowheads at target or arc midpoint (P6)
     * - Self-loop arcs (P7)
     * - Parallel edge bundle height-offset stacking (P8)
     * - Z-order by span — shorter arcs drawn on top (P9)
     * - Rotated 90° vertical labels (P2)
     * - Axis arrowhead extending past last node (P5)
     * - Color legend panel (P10)
     * - Hover and click interaction on nodes and edges
     * - Tooltip showing node/edge details
     */
    class UltraCanvasArcDiagram : public UltraCanvasUIElement {
    public:
        // ===== CONSTRUCTION =====

        UltraCanvasArcDiagram(const std::string& id,
                              float x, float y, float w, float h);

        // ===== NODE API =====

        /// Add a node. Returns assigned insertion index. Duplicate IDs are rejected.
        int  AddNode(const ArcNode& node);

        /// Add a node by ID and label (convenience)
        int  AddNode(const std::string& id, const std::string& label);

        /// Remove a node by ID and all its connected edges. Returns false if not found.
        bool RemoveNode(const std::string& id);

        /// Returns node count
        int  GetNodeCount() const { return static_cast<int>(nodes.size()); }

        /// Returns node by index (insertion order)
        const ArcNode* GetNode(int index) const;

        /// Returns node by ID, nullptr if not found
        const ArcNode* GetNodeById(const std::string& id) const;

        /// Update node properties in-place
        void UpdateNode(const std::string& id, const ArcNode& updated);

        // ===== EDGE API =====

        /// Add a directed or undirected arc edge. Returns edge index.
        int  AddEdge(const ArcEdge& edge);

        /// Add an edge by source/target IDs (convenience, weight=1, auto side)
        int  AddEdge(const std::string& sourceId,
                     const std::string& targetId,
                     float weight = 1.0f);

        /// Remove all edges between two nodes (both directions)
        void RemoveEdge(const std::string& sourceId, const std::string& targetId);

        /// Remove all edges
        void ClearEdges();

        /// Remove all nodes and edges
        void Clear();

        /// Returns edge count
        int  GetEdgeCount() const { return static_cast<int>(edges.size()); }

        // ===== STYLE & BEHAVIOUR =====

        void SetStyle(const ArcDiagramStyle& s)     { style = s; needsLayout = true; RequestRedraw(); }
        const ArcDiagramStyle& GetStyle() const     { return style; }

        // ===== SELECTION =====

        /// Returns index of currently selected node (-1 if none)
        int  GetSelectedNodeIndex() const           { return selectedNodeIdx; }

        /// Returns index of currently selected edge (-1 if none)
        int  GetSelectedEdgeIndex() const           { return selectedEdgeIdx; }

        void ClearSelection()                       { selectedNodeIdx = selectedEdgeIdx = -1; RequestRedraw(); }

        // ===== CALLBACKS =====

        /// Fired when a node is clicked
        /// @param int — node index; const ArcNode& — node data
        std::function<void(int, const ArcNode&)> onNodeClick;

        /// Fired when an edge is clicked
        /// @param int — edge index; const ArcEdge& — edge data
        std::function<void(int, const ArcEdge&)> onEdgeClick;

        /// Fired when mouse enters a node
        std::function<void(int, const ArcNode&)> onNodeHover;

        /// Fired when mouse enters an arc edge (at the apex region)
        std::function<void(int, const ArcEdge&)> onEdgeHover;

        // ===== RENDER & EVENTS =====

        void Render(IRenderContext* ctx, const Rect2Df& dirtyrect) override;
        bool OnEvent(const UCEvent& event) override;

    private:
        // ===== DATA =====
        std::vector<ArcNode>    nodes;          ///< Insertion-order node list
        std::vector<ArcEdge>    edges;          ///< Edge list
        float                   maxWeight       = 1.0f;
        float                   totalEdgeWeight = 0.0f;  ///< P12 — sum of weights for % display

        // ===== STATE =====
        ArcDiagramStyle style;
        bool            needsLayout     = true;

        // Layout cache
        std::vector<float>  nodePositions;  ///< Screen position along baseline per node
        float               baselinePos    = 0.0f; ///< Screen coordinate of the baseline
        float               availableSpan  = 0.0f; ///< Total usable baseline length

        // Hover / selection
        int     hoveredNodeIdx  = -1;
        int     hoveredEdgeIdx  = -1;
        int     selectedNodeIdx = -1;
        int     selectedEdgeIdx = -1;

        // Tooltip
        bool        showingTooltip  = false;
        float       tooltipX        = 0.0f;
        float       tooltipY        = 0.0f;
        std::string tooltipText;

        // ===== INTERNAL HELPERS =====
        void    ComputeLayout();
        void    ComputeNodeDegrees();                           ///< P1 — build degreeCache
        void    ComputeTotalEdgeWeight();                       ///< P12 — refresh sum cache
        std::string FormatArcValueLabel(float weight) const;    ///< P12 — value/% formatter
        int     LookupNode(const std::string& id) const;
        float   ArcHeight(int srcIdx, int tgtIdx,
                          int bundleOffset = 0) const;         ///< P8 — bundle offset param
        float   ArcWidth(float weight) const;
        float   ArcOpacity(float weight) const;                ///< P3
        bool    EdgeSideIsAbove(const ArcEdge& edge,
                                int srcIdx, int tgtIdx) const;
        float   NodeRadius(int idx) const;                     ///< P1 — unified radius lookup

        // Rendering
        void    DrawBaseline(IRenderContext* ctx) const;
        void    DrawEdges(IRenderContext* ctx) const;
        void    DrawNodes(IRenderContext* ctx) const;
        void    DrawLabels(IRenderContext* ctx) const;
        void    DrawLegend(IRenderContext* ctx) const;          ///< P10
        void    DrawTooltip(IRenderContext* ctx) const;

        // Edge drawing helpers
        void    DrawArc(IRenderContext* ctx, const ArcEdge& edge,
                        int srcIdx, int tgtIdx,
                        bool hovered, bool selected,
                        bool connectedHighlight,                ///< P11
                        bool dimmed,                            ///< P13 — render in dimmedColor
                        int bundleOffset = 0) const;           ///< P8
        void    DrawSelfLoop(IRenderContext* ctx,
                             const ArcEdge& edge, int nodeIdx,
                             bool hovered, bool selected,
                             bool connectedHighlight,
                             bool dimmed) const;               ///< P7 + P11 + P13
        void    DrawArrowhead(IRenderContext* ctx,
                              float tipX, float tipY,
                              float angle, const Color& col) const;

        // Hit testing
        int     HitTestNode(float localX, float localY) const;
        int     HitTestEdge(float localX, float localY) const;

        // Coordinate helpers (handles both orientations)
        float   NodeScreenPos(int idx) const;
        void    BaselinePoint(float pos, float& outX, float& outY) const;
        void    ArcControlPoints(int srcIdx, int tgtIdx, bool above,
                                 float heightOverride,
                                 float& cp1x, float& cp1y,
                                 float& cp2x, float& cp2y) const; ///< P8 heightOverride

        // ===== PRIVATE DATA =====
        std::vector<int>    degreeCache;    ///< P1 — edge count per node index

        // P13 — per-render focus state, refreshed each Render() call
        mutable int                 activeForDraw = -1;
        mutable std::vector<bool>   connectedNodeMask;

        void    RefreshFocusState() const;  ///< P13 — build activeForDraw + connectedNodeMask
    };

// ===== FACTORY FUNCTION =====

    std::shared_ptr<UltraCanvasArcDiagram> CreateArcDiagram(
            const std::string& id,
            float x, float y, float width, float height);

} // namespace UltraCanvas