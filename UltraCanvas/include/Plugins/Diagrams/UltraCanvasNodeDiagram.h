// include/Plugins/Diagrams/UltraCanvasNodeDiagram.h
// Interactive Node Diagram component for graph/network visualization & flow editing
// Version: 2.2.0
// Last Modified: 2026-07-31
// Author: UltraCanvas Framework
//
// CHANGELOG 2.2.0 (minor - organizational network analysis features):
//  - NEW: Data-driven node sizing. NodeSizeMode (Fixed / ByDegree / ByValue)
//         plus NodeDiagramSizing config. In ByDegree mode a node's size is
//         baseSize * sqrt(degree) clamped to [minSize, maxSize], so hub nodes
//         read as hubs instead of rendering the same size as leaves. Degree can
//         count Total / Incoming / Outgoing connections. NodeDiagramNode gained
//         a `value` field driving ByValue mode.
//  - NEW: Cluster containers. NodeDiagramGroup wraps a set of member nodes in
//         an auto-fitted boundary box (solid or dashed, optional fill, optional
//         corner radius, title in any corner). Bounds are recomputed from the
//         member nodes so the box follows dragging and re-layout. Groups are
//         drawn behind links, included in ComputeContentBounds (so FitView and
//         the minimap account for them) and serialized in ToJson/FromJson.
//  - NEW: Group cohesion force. SetGroupCohesion() adds a per-group centroid
//         attraction to the force-directed simulation, so members of a cluster
//         settle together instead of scattering - the layout that makes a
//         grouped network diagram readable.
//  - NEW: Color legend overlay. NodeDiagramLegendConfig renders swatch+label
//         rows in any corner, with optional panel background and title.
//         BuildLegendFromGroups() derives the entries from the groups.
//
// CHANGELOG 2.0.6 (patch - critical text + layout fixes):
//  - FIXED: RenderNodeLabel - was offsetting text Y by textHeight/3.0f from
//           centerY, which assumed DrawText's Y was the baseline. The actual
//           Cairo+Pango backend treats Y as the TOP of the text bounding box
//           (cairo_move_to + pango_cairo_show_layout). Aligned with the
//           canonical framework pattern used by UltraCanvasButton:
//             textY = nodeY + (nodeHeight - textHeight) / 2
//           Labels are now correctly centered vertically inside nodes.
//  - FIXED: RenderLinkLabel had the same baseline-vs-top bug. Same correction.
//  - FIXED: RunForceDirectedLayout - added a final overlap-resolution pass
//           that physically pushes any pair of overlapping nodes apart based
//           on their bounding-box radii. Force-directed alone with discrete
//           timesteps can leave residual overlaps; this guarantees zero
//           overlap on every Reset (fixes the recurring Friends overlap
//           reported across multiple screenshots).
//
// CHANGELOG 2.0.5 (patch):
//  - FIXED: Colorful theme - linkDistance 80->130 and chargeStrength -250->-500
//           so force-directed layouts converge with proper spacing instead of
//           overlapping (the old values were tuned for 40px nodes; 60px+ nodes
//           need stronger separation).
//  - FIXED: SuggestNodeSizeForLabel - heuristic factor 0.58->0.70 and
//           horizontal padding 24->32. The old factor underestimated bold
//           glyph width; "Superficial" overflowed despite the auto-sizing.
//  - FIXED: RenderNodeLabel - if the measured text width exceeds the node
//           interior, the label is shifted right by the overflow so it stays
//           visually centered inside the node bounds (previously it just
//           overflowed to the right edge).
//
// CHANGELOG 2.0.4 (patch):
//  - FIXED: RunForceDirectedLayout - replaced random initial placement with a
//           deterministic circular layout, raised default iterations to 250,
//           and added a center-pulling weak force. Reset now produces a clean
//           non-overlapping arrangement on every call (previous behavior gave
//           inconsistent results because srand() was never seeded and the
//           algorithm sometimes terminated in a local minimum).
//  - NEW: GetTextLineDimensions exposed indirectly via MeasureLabel(label,
//         outWidth, outHeight) - lets callers ask the component what space a
//         given label needs at the current font, so they can size nodes to
//         fit.
//
// CHANGELOG 2.0.3 (patch):
//  - NEW: onCanvasRightClick(worldX, worldY) callback - fires when user
//         right-clicks on empty canvas. Enables apps to implement context
//         menus or quick-create-node interactions without touching the
//         component internals.
//
// CHANGELOG 2.0.2 (patch - revert and cleanup):
//  - REMOVED: Integrated title bar feature (NodeDiagramTitleConfig, SetTitle*,
//             RenderTitle). Demo wrappers already provide titles externally,
//             and the integrated title clashed visually with demo wrappers.
//             Use the wrapping container/Label widget if you want a title.
//
// CHANGELOG 2.0.1 (patch - cosmetic & UX fixes from real demo screenshot):
//  - NEW: SetAutoFitOnLayout() - RunLayout() now auto-calls FitView() by default
//  - FIXED: AddNode default size 40 -> 60 (40px circles cropped labels >5 chars)
//  - FIXED: RunForceDirectedLayout uses bounds fallback (800x600) when bounds invalid
//  - FIXED: Controls glyphs now drawn as vector primitives (lines/rects) instead of
//           text characters - the old "+ - [ ] L" text glyphs were tiny and
//           off-center due to a vertical-centering math bug
//  - FIXED: Default & Colorful theme grid colors slightly darker so they're visible
//
// CHANGELOG 2.0.0 (mayor):
//  - Hybrid component: graph visualization + interactive flow editor
//  - NEW: NodeHandle system (input/output ports) with drag-to-connect
//  - NEW: LinkStyle enum (Straight, Bezier, SmoothStep, Step) per-link selectable
//  - NEW: Multi-selection with Shift+click and selection box (drag in empty area)
//  - NEW: Interactive panning (middle-click or empty-area drag)
//  - NEW: Keyboard shortcuts (Delete, Ctrl+A select all, Escape cancel)
//  - NEW: Snap-to-grid for node positioning
//  - NEW: Minimap overlay (configurable position, draggable viewport indicator)
//  - NEW: Controls overlay (zoom in/out, fit view, lock interaction)
//  - NEW: JSON save/load (ToJson / FromJson)
//  - NEW: Verbose API (AddNode(NodeDiagramNode&), AddLink(NodeDiagramLink&)) alongside simple API
//  - NEW: Hit testing per shape (no longer assumes circular)
//  - NEW: Arrow direction uses last segment's local vector (lesson from BlockDiagram v2.3)
//  - FIXED: Zoom-at-cursor formula (was net-zero before)
//  - FIXED: Mouse cursor feedback (Hand on handle, SizeAll on draggable node, etc.)

// CHANGELOG 2.1.0 (minor - shared viewport):
//  - CHANGED: pan/zoom, the snap grid, the minimap and the controls overlay now
//    live in the shared UltraCanvasDiagramViewport instead of being duplicated
//    here and in UltraCanvasCompositorDiagram. The public API is unchanged;
//    NodeDiagramPanelPosition / NodeDiagramMinimapConfig /
//    NodeDiagramControlsConfig / NodeDiagramSnapGrid are now aliases of the
//    shared types, so existing app code keeps compiling.
//  - FIXED: PointInMinimap(), FindControlButtonAt() and HandleMouseWheel()
//    added/subtracted finalBounds.x/y against event coordinates that are
//    already element-local, double-counting the element origin. Minimap and
//    controls hit-testing, and zoom-at-cursor, were offset by the element's
//    position whenever the diagram was not placed at (0, 0). The shared
//    viewport is local-space throughout.

#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasCommonTypes.h"
#include "Plugins/Diagrams/UltraCanvasDiagramViewport.h"
#include <vector>
#include <map>
#include <set>
#include <string>
#include <memory>
#include <functional>
#include <optional>
#include <cmath>

namespace UltraCanvas {

// =============================================================================
// NODE DIAGRAM ENUMERATIONS
// =============================================================================

enum class NodeShape {
    Circle,
    Square,
    Rectangle,
    RoundedSquare,
    Diamond,
    Hexagon,
    Triangle,
    Star,
    Cloud
};

enum class NodeDiagramLayout {
    Manual,
    ForceDirected,
    Circular,
    Hierarchical,
    Grid
};

enum class NodeDiagramTheme {
    Default,
    Professional,
    Colorful,
    Minimal,
    Dark
};

// NEW in 2.0.0: Link routing style
enum class LinkStyle {
    Straight,    // Direct line A->B
    Bezier,      // Smooth cubic bezier
    SmoothStep,  // Orthogonal with rounded corners
    Step         // Sharp orthogonal
};

// NEW in 2.0.0: Handle position on node perimeter
enum class HandlePosition {
    Top,
    Right,
    Bottom,
    Left
};

// NEW in 2.0.0: Handle role (output = source of links, input = target)
enum class HandleType {
    Source,
    Target
};

// NEW in 2.0.0: Panel positions for overlays
// CHANGED in 2.1.0: now an alias of the shared DiagramPanelPosition
using NodeDiagramPanelPosition = DiagramPanelPosition;

// NEW in 2.2.0: how a node's rendered size is derived.
enum class NodeSizeMode {
    Fixed,      // node.width / node.height are used verbatim (2.0 behaviour)
    ByDegree,   // size = baseSize * sqrt(degree), clamped to [minSize, maxSize]
    ByValue     // size = baseSize * sqrt(node.value), clamped the same way
};

// NEW in 2.2.0: which connections count toward a node's degree.
enum class NodeDegreeMode {
    Total,      // Incoming + outgoing (default - undirected reading)
    Incoming,   // Links where the node is the target
    Outgoing    // Links where the node is the source
};

// NEW in 2.2.0: where a cluster container draws its title.
// NOTE: no enumerator may be named "None" - X11's Xlib.h defines None as a
// macro and this header is reachable from app code that includes X11.
enum class GroupLabelPosition {
    TopLeft,
    TopCenter,
    TopRight,
    BottomLeft,
    BottomCenter,
    NoLabel
};

// =============================================================================
// NODE DIAGRAM DATA STRUCTURES
// =============================================================================

// NEW in 2.0.0: Connection handle (port) on a node
struct NodeHandle {
    std::string id;
    HandleType type = HandleType::Source;
    HandlePosition position = HandlePosition::Right;
    
    // Computed position offset (from node center, in node-local coords)
    double offsetX = 0.0f;
    double offsetY = 0.0f;
    
    // Visual
    double radius = 5.0f;
    Color color = Color(85, 85, 85, 255);
    Color hoverColor = Color(0, 120, 215, 255);
    Color connectedColor = Color(0, 180, 100, 255);
    
    // Behaviour
    bool connectable = true;
    int currentConnections = 0;
    
    // State
    bool isHovered = false;
    
    NodeHandle() = default;
    NodeHandle(const std::string& handleId, HandleType t, HandlePosition pos)
        : id(handleId), type(t), position(pos) {}
};

struct NodeDiagramNode {
    std::string id;
    std::string label;
    NodeShape shape = NodeShape::Circle;
    
    double x = 0.0f;
    double y = 0.0f;
    double width = 40.0f;
    double height = 40.0f;
    
    Color fillColor = Color(100, 150, 220, 255);
    Color borderColor = Color(50, 80, 140, 255);
    double borderWidth = 2.0f;
    
    Color textColor = Color(255, 255, 255, 255);
    double fontSize = 10.0f;
    
    bool isSelected = false;
    bool isDragging = false;
    bool isPinned = false;
    
    // Force-directed layout velocity
    double vx = 0.0f;
    double vy = 0.0f;
    
    // NEW in 2.0.0: Behaviour flags
    bool draggable = true;
    bool selectable = true;
    bool deletable = true;
    
    // NEW in 2.0.0: Connection handles. Empty = no handles, links connect node-to-node
    std::vector<NodeHandle> handles;
    
    // NEW in 2.0.0: Z-order (higher = on top)
    int zIndex = 0;

    // NEW in 2.2.0: magnitude behind this node. Drives NodeSizeMode::ByValue.
    // Ignored in Fixed and ByDegree modes.
    double value = 1.0;

    // NEW in 2.2.0: size the sizing pass scales from. Captured the first time
    // sizing runs so switching back to NodeSizeMode::Fixed restores the size
    // the node was created with instead of whatever the last mode computed.
    double baseWidth = 0.0;
    double baseHeight = 0.0;

    NodeDiagramNode() = default;
    NodeDiagramNode(const std::string& nodeId, const std::string& nodeLabel)
        : id(nodeId), label(nodeLabel) {}
};

struct NodeDiagramLink {
    std::string id;
    std::string sourceNodeId;
    std::string targetNodeId;
    
    // NEW in 2.0.0: Specific handles to connect to (empty = node center)
    std::string sourceHandleId;
    std::string targetHandleId;
    
    Color lineColor = Color(120, 120, 120, 255);
    double lineWidth = 2.0f;
    bool directed = true;
    
    std::string label;
    
    bool isSelected = false;
    
    // NEW in 2.0.0: Routing style per link
    LinkStyle style = LinkStyle::Straight;
    
    // NEW in 2.0.0: Behaviour
    bool selectable = true;
    bool deletable = true;
    
    NodeDiagramLink() = default;
    NodeDiagramLink(const std::string& linkId, const std::string& source, const std::string& target)
        : id(linkId), sourceNodeId(source), targetNodeId(target) {}
};

// NEW in 2.2.0: data-driven node sizing configuration.
//
// Both scaling modes use a square-root transfer so the node AREA - not its
// diameter - is proportional to the underlying quantity. That is the perceptually
// correct mapping for circular/square marks; scaling the diameter linearly makes
// a degree-9 hub look 9x more important than a degree-1 leaf instead of 3x.
struct NodeDiagramSizing {
    NodeSizeMode mode = NodeSizeMode::Fixed;
    NodeDegreeMode degreeMode = NodeDegreeMode::Total;

    double baseSize = 26.0;   // Size of a degree-1 (or value-1) node
    double minSize  = 18.0;   // Floor - keeps isolated nodes clickable
    double maxSize  = 96.0;   // Ceiling - keeps a mega-hub from eating the canvas

    // When true (default) width and height are scaled together, so circles stay
    // circular and squares stay square. When false only the width scales and the
    // height keeps whatever the node was created with - useful for label-bearing
    // rectangles that must stay one text line tall.
    bool keepAspect = true;
};

// NEW in 2.2.0: a cluster container - a boundary box drawn around a set of
// member nodes, with an optional title. Bounds are recomputed from the members
// on every layout/drag, so the box tracks its contents automatically.
struct NodeDiagramGroup {
    std::string id;
    std::string label;
    std::vector<std::string> nodeIds;

    Color fillColor = Color(0, 0, 0, 0);              // Transparent by default
    Color borderColor = Color(170, 170, 170, 255);
    double borderWidth = 1.0;
    bool dashed = false;
    double dashLength = 6.0;
    double dashGap = 4.0;

    double padding = 24.0;        // World-space gap between members and the box
    double cornerRadius = 0.0;    // 0 = sharp corners

    GroupLabelPosition labelPosition = GroupLabelPosition::TopLeft;
    Color labelColor = Color(70, 70, 70, 255);
    double labelFontSize = 12.0;
    double labelMargin = 6.0;

    bool visible = true;

    NodeDiagramGroup() = default;
    NodeDiagramGroup(const std::string& groupId, const std::string& groupLabel)
        : id(groupId), label(groupLabel) {}
};

// NEW in 2.2.0: one row of the color legend overlay.
struct NodeDiagramLegendEntry {
    std::string label;
    Color color = Color(100, 150, 220, 255);

    NodeDiagramLegendEntry() = default;
    NodeDiagramLegendEntry(const std::string& entryLabel, const Color& entryColor)
        : label(entryLabel), color(entryColor) {}
};

// NEW in 2.2.0: color legend overlay. Drawn in SCREEN space like the minimap
// and controls overlays, so it does not pan or zoom with the diagram.
struct NodeDiagramLegendConfig {
    bool visible = false;
    std::vector<NodeDiagramLegendEntry> entries;

    DiagramPanelPosition position = DiagramPanelPosition::TopLeft;
    double padding = 10.0;        // Distance from the element edge
    double innerPadding = 8.0;    // Panel border to content
    double swatchWidth = 14.0;
    double swatchHeight = 14.0;
    double rowGap = 5.0;
    double swatchGap = 7.0;       // Swatch to its label
    double fontSize = 11.0;

    std::string title;            // Optional heading above the rows
    double titleFontSize = 12.0;
    double titleGap = 6.0;

    Color textColor = Color(50, 50, 50, 255);
    Color backgroundColor = Color(255, 255, 255, 225);
    Color borderColor = Color(200, 200, 200, 255);
    bool showBackground = true;
    bool showSwatchBorder = true;
    Color swatchBorderColor = Color(120, 120, 120, 180);
};

struct NodeDiagramStyle {
    std::string fontFamily = "Arial";
    double baseFontSize = 11.0f;
    
    Color backgroundColor = Color(248, 248, 248, 255);
    Color gridColor = Color(230, 230, 230, 255);
    bool showGrid = false;
    double gridSpacing = 25.0f;
    
    Color selectionColor = Color(255, 100, 0, 255);
    double selectionWidth = 3.0f;
    
    // NEW in 2.0.0: selection box
    Color selectionBoxFill = Color(0, 120, 215, 40);
    Color selectionBoxStroke = Color(0, 120, 215, 200);
    
    // NEW in 2.0.0: in-progress connection line
    Color connectionLineColor = Color(100, 100, 100, 200);
    double connectionLineWidth = 2.0f;
    
    // Force-directed layout parameters
    double linkDistance = 100.0f;
    double linkStrength = 0.1f;
    double chargeStrength = -300.0f;
    int iterations = 100;

    // NEW in 2.2.0: how strongly members of the same group are pulled toward
    // their group centroid during force-directed layout. 0 disables clustering
    // (2.1 behaviour); ~0.05-0.15 gives visually distinct clusters that still
    // respect link and repulsion forces.
    double groupCohesion = 0.0f;
};

// NEW in 2.0.0: Snap grid / minimap / controls configuration
// CHANGED in 2.1.0: now aliases of the shared viewport's configuration types
using NodeDiagramSnapGrid = DiagramSnapGrid;
using NodeDiagramMinimapConfig = DiagramMinimapConfig;
using NodeDiagramControlsConfig = DiagramControlsConfig;

// =============================================================================
// NODE DIAGRAM COMPONENT CLASS
// =============================================================================

class UltraCanvasNodeDiagram : public UltraCanvasUIElement {
public:
    UltraCanvasNodeDiagram(const std::string& id, int x, int y, int width, int height);
    bool AcceptsFocus() const override { return true; }
    
    // =============================================================================
    // NODE MANAGEMENT - SIMPLE API (compatible with 1.1.0)
    // =============================================================================
    
    void AddNode(const std::string& id, const std::string& label, double x, double y);
    void AddNode(const std::string& id, const std::string& label, double x, double y, double size);
    void AddNode(const std::string& id, const std::string& label, double x, double y, double width, double height);
    
    // NEW in 2.0.0: Verbose API
    void AddNode(const NodeDiagramNode& node);
    
    void RemoveNode(const std::string& id);
    void UpdateNodePosition(const std::string& id, double x, double y);
    void UpdateNodeLabel(const std::string& id, const std::string& label);
    void SetNodeColor(const std::string& id, const Color& fillColor, const Color& borderColor);
    void SetNodeShape(const std::string& id, NodeShape shape);
    void PinNode(const std::string& id, bool pinned);
    
    NodeDiagramNode* GetNode(const std::string& id);
    const NodeDiagramNode* GetNode(const std::string& id) const;
    std::vector<std::string> GetAllNodeIds() const;
    
    // NEW in 2.0.0: Handle management
    void AddHandle(const std::string& nodeId, const NodeHandle& handle);
    void AddDefaultHandles(const std::string& nodeId);  // Convenience: left-target + right-source
    void RemoveHandle(const std::string& nodeId, const std::string& handleId);
    NodeHandle* GetHandle(const std::string& nodeId, const std::string& handleId);
    
    // =============================================================================
    // LINK MANAGEMENT - SIMPLE API (compatible with 1.1.0)
    // =============================================================================
    
    void AddLink(const std::string& id, const std::string& sourceId, const std::string& targetId);
    void AddLink(const std::string& id, const std::string& sourceId, const std::string& targetId,
                 const Color& lineColor);
    
    // NEW in 2.0.0: Verbose API
    void AddLink(const NodeDiagramLink& link);
    
    void RemoveLink(const std::string& id);
    void RemoveLink(const std::string& sourceId, const std::string& targetId);
    void SetLinkColor(const std::string& id, const Color& color);
    void SetLinkWidth(const std::string& id, double width);
    void SetLinkLabel(const std::string& id, const std::string& label);
    void SetLinkStyle(const std::string& id, LinkStyle style);  // NEW in 2.0.0
    
    NodeDiagramLink* GetLink(const std::string& id);
    std::vector<std::string> GetAllLinkIds() const;
    
    // NEW in 2.0.0: Default style for newly created links (from drag-to-connect)
    void SetDefaultLinkStyle(LinkStyle style) { defaultLinkStyle = style; }
    LinkStyle GetDefaultLinkStyle() const { return defaultLinkStyle; }
    
    // =============================================================================
    // LAYOUT
    // =============================================================================
    
    void SetLayout(NodeDiagramLayout layout);
    void RunLayout();
    void RunForceDirectedLayout(int iterations = 100);
    void ApplyCircularLayout();
    void ApplyGridLayout();
    void ApplyHierarchicalLayout(const std::string& rootId);

    // =============================================================================
    // DATA-DRIVEN NODE SIZING (NEW in 2.2.0)
    // =============================================================================

    // Switch the sizing mode. Sizes are recomputed immediately, and again
    // whenever the link set changes while a degree-driven mode is active.
    void SetNodeSizeMode(NodeSizeMode mode);
    NodeSizeMode GetNodeSizeMode() const { return sizing.mode; }

    void SetNodeSizing(const NodeDiagramSizing& newSizing);
    NodeDiagramSizing GetNodeSizing() const { return sizing; }

    // Convenience for the common case: mode + the scale range in one call.
    void SetNodeSizeRange(double baseSize, double minSize, double maxSize);

    // Magnitude behind a node, used by NodeSizeMode::ByValue.
    void SetNodeValue(const std::string& id, double value);
    double GetNodeValue(const std::string& id) const;

    // Connection count for a node under the current degree mode. Returns 0 for
    // unknown ids.
    int GetNodeDegree(const std::string& id) const;
    int GetNodeDegree(const std::string& id, NodeDegreeMode mode) const;

    // Force a sizing pass now. Normally unnecessary - the component recomputes
    // sizes when links or values change - but useful after bulk edits made
    // through GetNode() pointers.
    void ApplyNodeSizing();

    // =============================================================================
    // CLUSTER CONTAINERS / GROUPS (NEW in 2.2.0)
    // =============================================================================

    void AddGroup(const NodeDiagramGroup& group);
    void AddGroup(const std::string& id, const std::string& label,
                  const std::vector<std::string>& nodeIds,
                  const Color& borderColor,
                  const Color& fillColor = Color(0, 0, 0, 0));
    void RemoveGroup(const std::string& id);
    void ClearGroups();

    void AddNodeToGroup(const std::string& groupId, const std::string& nodeId);
    void RemoveNodeFromGroup(const std::string& groupId, const std::string& nodeId);

    NodeDiagramGroup* GetGroup(const std::string& id);
    const NodeDiagramGroup* GetGroup(const std::string& id) const;
    std::vector<std::string> GetAllGroupIds() const;

    // Id of the first group containing the node, or "" when it belongs to none.
    std::string GetNodeGroupId(const std::string& nodeId) const;

    // Current world-space box of a group, including its padding. Returns an
    // empty rect for unknown ids or groups with no (visible) members.
    Rect2Dd GetGroupBounds(const std::string& id) const;

    void SetGroupsVisible(bool visible);
    bool AreGroupsVisible() const { return groupsVisible; }

    // Strength of the per-group centroid attraction used by the force-directed
    // layout. 0 disables clustering.
    void SetGroupCohesion(double strength);
    double GetGroupCohesion() const { return style.groupCohesion; }

    // =============================================================================
    // COLOR LEGEND (NEW in 2.2.0)
    // =============================================================================

    void SetLegendVisible(bool visible);
    void SetLegendPosition(NodeDiagramPanelPosition pos);
    void SetLegendConfig(const NodeDiagramLegendConfig& cfg);
    NodeDiagramLegendConfig GetLegendConfig() const { return legend; }

    void AddLegendEntry(const std::string& label, const Color& color);
    void ClearLegendEntries();

    // Replaces the legend entries with one row per group, using each group's
    // border color as the swatch. Groups with an empty label are skipped.
    void BuildLegendFromGroups();

    // =============================================================================
    // STYLING & THEME
    // =============================================================================
    
    void SetTheme(NodeDiagramTheme theme);
    void SetBackgroundColor(const Color& color);
    void SetGridVisible(bool visible, double spacing = 25.0f);
    void SetFontFamily(const std::string& fontFamily);
    void SetFontSize(double size);
    void SetLinkDistance(double distance);
    
    // =============================================================================
    // SELECTION & INTERACTION
    // =============================================================================
    
    void SelectNode(const std::string& id, bool addToSelection = false);  // CHANGED in 2.0.0
    void SelectLink(const std::string& id, bool addToSelection = false);  // NEW in 2.0.0
    void SelectAll();                                                      // NEW in 2.0.0
    void DeselectAll();
    void DeleteSelected();                                                 // NEW in 2.0.0
    void Clear();
    
    std::string GetSelectedNodeId() const { return selectedNodeId; }
    std::vector<std::string> GetSelectedNodeIds() const;                   // NEW in 2.0.0
    std::vector<std::string> GetSelectedLinkIds() const;                   // NEW in 2.0.0
    bool IsNodeSelected(const std::string& id) const;                      // NEW in 2.0.0
    bool IsLinkSelected(const std::string& id) const;                      // NEW in 2.0.0
    
    // =============================================================================
    // VIEWPORT
    // =============================================================================
    
    void SetZoomLevel(double zoom);
    void SetPanOffset(double x, double y);
    double GetZoomLevel() const;
    Point2Dd GetPanOffset() const;
    
    void ZoomIn(double factor = 1.2f);                                      // NEW in 2.0.0
    void ZoomOut(double factor = 1.2f);                                     // NEW in 2.0.0
    void FitView(double padding = 40.0f);                                   // NEW in 2.0.0
    void CenterOn(double worldX, double worldY);                             // NEW in 2.0.0
    
    void SetMinZoom(double minZ);
    void SetMaxZoom(double maxZ);
    
    // =============================================================================
    // SNAP-TO-GRID (NEW in 2.0.0)
    // =============================================================================
    
    void SetSnapToGrid(bool enabled);
    void SetSnapGrid(double snapX, double snapY);
    bool IsSnapToGridEnabled() const;
    
    // =============================================================================
    // INTERACTION SETTINGS (NEW in 2.0.0)
    // =============================================================================
    
    void SetInteractive(bool interactive) { isInteractive = interactive; }
    bool IsInteractive() const { return isInteractive; }
    void SetNodesConnectable(bool connectable) { nodesConnectable = connectable; }
    void SetPanOnDrag(bool pan) { panOnDrag = pan; }
    void SetZoomOnScroll(bool zoom) { zoomOnScroll = zoom; }
    
    // =============================================================================
    // MINIMAP (NEW in 2.0.0)
    // =============================================================================
    
    void SetMinimapVisible(bool visible);
    void SetMinimapPosition(NodeDiagramPanelPosition pos);
    void SetMinimapConfig(const NodeDiagramMinimapConfig& cfg);
    NodeDiagramMinimapConfig GetMinimapConfig() const;
    
    // =============================================================================
    // CONTROLS OVERLAY (NEW in 2.0.0)
    // =============================================================================
    
    void SetControlsVisible(bool visible);
    void SetControlsPosition(NodeDiagramPanelPosition pos);
    void SetControlsConfig(const NodeDiagramControlsConfig& cfg);
    NodeDiagramControlsConfig GetControlsConfig() const;
    
    // =============================================================================
    // AUTO-FIT (NEW in 2.0.1)
    // =============================================================================
    
    // When true (default), RunLayout() automatically calls FitView() at the end
    // so the user never sees a stranded cluster in the corner.
    void SetAutoFitOnLayout(bool autoFit) { autoFitOnLayout = autoFit; }
    bool GetAutoFitOnLayout() const { return autoFitOnLayout; }
    
    // =============================================================================
    // LABEL MEASUREMENT (NEW in 2.0.4)
    // =============================================================================
    
    // Heuristic estimate of how wide/tall a label would render at the given font
    // size. Useful for callers that want to size nodes to fit their labels
    // without having direct access to an IRenderContext. Uses the conservative
    // average-glyph-width approximation (Arial: ~0.55 of font size) - real
    // measurement would need ctx.GetTextLineDimensions, which is only available
    // during Render().
    void MeasureLabel(const std::string& label, double fontSize,
                      int& outWidth, int& outHeight) const;
    
    // Convenience: returns the suggested node width/height for a given label
    // such that the label (in bold at the given font size) fits inside the
    // node with reasonable padding. Caller can use this directly when creating
    // a node so labels don't crop or overflow.
    void SuggestNodeSizeForLabel(const std::string& label, double fontSize,
                                  double minSize, double& outWidth,
                                  double& outHeight) const;
    
    // =============================================================================
    // SERIALIZATION (NEW in 2.0.0)
    // =============================================================================
    
    std::string ToJson() const;
    bool FromJson(const std::string& json);
    
    // =============================================================================
    // RENDERING & EVENTS
    // =============================================================================
    
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;
    
    // =============================================================================
    // CALLBACKS
    // =============================================================================
    
    std::function<void(const std::string&)> onNodeClick;
    std::function<void(const std::string&)> onNodeDoubleClick;
    std::function<void(const std::string&, double, double)> onNodeDrag;
    std::function<void(const std::string&)> onNodeHover;
    std::function<void(const std::string&)> onLinkClick;
    
    // NEW in 2.0.0
    std::function<void(const NodeDiagramLink&)> onLinkCreated;       // Drag-to-connect created a link
    std::function<void(const std::vector<std::string>&,
                       const std::vector<std::string>&)> onSelectionChange;  // (nodeIds, linkIds)
    std::function<void(double zoom, double panX, double panY)> onViewportChange;
    
    // NEW in 2.0.3
    // Fires when the user right-clicks on empty canvas (no node, no link, no
    // handle, no overlay). worldX/worldY are diagram-world coordinates so the
    // app can place a new node exactly where the click landed.
    std::function<void(double worldX, double worldY)> onCanvasRightClick;

private:
    // =============================================================================
    // HANDLE GEOMETRY HELPERS
    // =============================================================================
    
    Point2Dd GetHandleWorldPosition(const NodeDiagramNode& node, const NodeHandle& handle) const;
    
    // =============================================================================
    // HIT TESTING - PROPER PER-SHAPE
    // =============================================================================
    
    std::string FindNodeAt(const Point2Di& screenPos);
    std::string FindLinkAt(const Point2Di& screenPos);
    std::pair<std::string, std::string> FindHandleAt(const Point2Di& screenPos);  // (nodeId, handleId)
    int FindControlButtonAt(const Point2Di& screenPos);  // Returns button index or -1
    bool PointInNode(const NodeDiagramNode& node, const Point2Dd& worldPos) const;
    bool PointInMinimap(const Point2Di& screenPos) const;
    
    // =============================================================================
    // RENDERING HELPERS
    // =============================================================================
    
    void RenderGrid(IRenderContext* ctx);
    void RenderGroups(IRenderContext* ctx);          // NEW in 2.2.0 - boxes only
    void RenderGroupTitles(IRenderContext* ctx);     // NEW in 2.2.0 - drawn last
    void RenderGroupLabel(IRenderContext* ctx, const NodeDiagramGroup& group,
                          const Rect2Dd& box);        // NEW in 2.2.0
    void RenderLinks(IRenderContext* ctx);
    void RenderNodes(IRenderContext* ctx);
    void RenderNodeShape(IRenderContext* ctx, const NodeDiagramNode& node);
    void RenderNodeLabel(IRenderContext* ctx, const NodeDiagramNode& node);
    void RenderNodeHandles(IRenderContext* ctx, const NodeDiagramNode& node);  // NEW
    void RenderLink(IRenderContext* ctx, const NodeDiagramLink& link);          // NEW
    void RenderLinkLabel(IRenderContext* ctx, const NodeDiagramLink& link);
    void RenderArrowHead(IRenderContext* ctx, double tipX, double tipY,
                         double dirX, double dirY, const Color& color, double size);
    
    // NEW in 2.0.0: Overlays (rendered in screen coords, outside Push/Pop transform)
    void RenderConnectionLine(IRenderContext* ctx);  // In-progress drag-to-connect line
    void RenderSelectionBox(IRenderContext* ctx);
    void RenderMinimap(IRenderContext* ctx);
    void RenderControls(IRenderContext* ctx);
    void RenderLegend(IRenderContext* ctx);          // NEW in 2.2.0
    
    // =============================================================================
    // EVENT SUB-HANDLERS (NEW pattern in 2.0.0)
    // =============================================================================
    
    bool HandleMouseDown(const UCEvent& event);
    bool HandleMouseUp(const UCEvent& event);
    bool HandleMouseMove(const UCEvent& event);
    bool HandleMouseWheel(const UCEvent& event);
    bool HandleKeyDown(const UCEvent& event);
    
    // =============================================================================
    // UTILITY
    // =============================================================================
    
    Point2Dd ScreenToWorld(const Point2Di& screenPos) const;
    Point2Di WorldToScreen(const Point2Dd& worldPos) const;

    // NEW in 2.1.0: world-space extent of all nodes, handed to the shared
    // viewport for FitView and the minimap projection.
    DiagramContentBounds ComputeContentBounds() const;
    // Keeps the shared viewport's notion of the drawable area in sync with the
    // element's current size. Called at the top of Render() and event handling.
    void SyncViewportSize();
    double CalculateDistance(const Point2Dd& a, const Point2Dd& b) const;
    Point2Dd SnapPoint(const Point2Dd& p) const;
    void NotifySelectionChange();
    void NotifyViewportChange();
    void ClampZoom();
    void BuildLinkBezier(const Point2Dd& src, const Point2Dd& tgt,
                          HandlePosition srcSide, HandlePosition tgtSide,
                          std::vector<Point2Dd>& outPath) const;
    void BuildLinkStep(const Point2Dd& src, const Point2Dd& tgt,
                        HandlePosition srcSide, HandlePosition tgtSide,
                        bool smooth, std::vector<Point2Dd>& outPath) const;
    HandlePosition InferHandleSide(const NodeDiagramNode& fromNode,
                                    const NodeDiagramNode& toNode) const;

    // NEW in 2.2.0: degree cache + sizing + group geometry
    void RebuildDegreeCache() const;
    void InvalidateDegreeCache();
    // World-space box a group occupies, or a rect with width<=0 when the group
    // has no resolvable members.
    Rect2Dd ComputeGroupBounds(const NodeDiagramGroup& group) const;
    // Panel rectangle for the legend overlay, in element-local pixels.
    Rect2Dd ComputeLegendPanel(IRenderContext* ctx) const;

    // =============================================================================
    // THEME
    // =============================================================================
    
    void ApplyTheme(NodeDiagramTheme theme);
    void ApplyDefaultTheme();
    void ApplyProfessionalTheme();
    void ApplyColorfulTheme();
    
    // =============================================================================
    // DATA MEMBERS
    // =============================================================================
    
    std::map<std::string, NodeDiagramNode> nodes;
    std::vector<std::string> nodeOrder;        // For deterministic z-ordering
    std::vector<NodeDiagramLink> links;

    // NEW in 2.2.0: cluster containers, kept in a vector so draw order is the
    // order the app declared them in.
    std::vector<NodeDiagramGroup> groups;
    bool groupsVisible = true;

    // NEW in 2.2.0: data-driven sizing + degree cache. The cache is mutable so
    // const accessors (GetNodeDegree) can refresh it lazily.
    NodeDiagramSizing sizing;
    mutable std::map<std::string, int> degreeIn;
    mutable std::map<std::string, int> degreeOut;
    mutable bool degreeCacheDirty = true;

    // NEW in 2.2.0: legend overlay
    NodeDiagramLegendConfig legend;

    NodeDiagramStyle style;
    NodeDiagramTheme theme = NodeDiagramTheme::Default;
    NodeDiagramLayout layout = NodeDiagramLayout::Manual;
    LinkStyle defaultLinkStyle = LinkStyle::Bezier;
    
    // Selection (single-select fields kept for back-compat, multi-select in sets)
    std::string selectedNodeId;
    std::string selectedLinkId;
    std::set<std::string> selectedNodes;
    std::set<std::string> selectedLinks;
    std::string hoveredNodeId;
    std::string hoveredLinkId;
    std::string hoveredHandleNodeId;
    std::string hoveredHandleId;
    int hoveredControlButton = -1;
    
    // Mouse interaction state
    bool isDraggingNode = false;
    bool isDraggingViewport = false;
    bool isSelectingBox = false;
    bool isConnecting = false;       // NEW: drag-to-connect in progress
    bool isDraggingMinimap = false;  // NEW
    
    Point2Di dragStartPos;
    Point2Di lastMousePos;
    std::map<std::string, Point2Dd> dragStartPositions;  // For multi-node drag
    
    // Selection box (in world coordinates)
    Point2Dd selectionBoxStart;
    Point2Dd selectionBoxEnd;
    
    // In-progress connection state
    std::string connectionSourceNode;
    std::string connectionSourceHandle;
    HandleType connectionSourceType = HandleType::Source;
    Point2Dd connectionEndPoint;  // World coords, follows mouse
    
    // Viewport, snap grid, minimap and controls (shared component, 2.1.0)
    UltraCanvasDiagramViewport viewport;

    // Interaction toggles
    bool isInteractive = true;
    bool nodesConnectable = true;
    bool panOnDrag = true;
    bool zoomOnScroll = true;
    bool isMultiSelectKeyHeld = false;  // Tracked from key events

    // NEW in 2.0.1
    bool autoFitOnLayout = true;
};

// =============================================================================
// FACTORY FUNCTION
// =============================================================================

inline std::shared_ptr<UltraCanvasNodeDiagram> CreateNodeDiagram(
        const std::string& id, int x, int y, int w, int h) {
    return std::make_shared<UltraCanvasNodeDiagram>(id, x, y, w, h);
}

} // namespace UltraCanvas