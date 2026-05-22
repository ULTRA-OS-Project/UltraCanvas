// include/Plugins/Diagrams/UltraCanvasCompositorDiagram.h
// Compositor / shader-style node graph component for node-based editors
// where each node is a composite panel (header + typed sockets + embedded
// widgets + optional preview), as opposed to UltraCanvasNodeDiagram's
// opaque-shape model used for network/graph visualization.
//
// Version: 0.4.0
// Last Modified: 2026-05-22
// Author: UltraCanvas Framework
//
// CHANGELOG 0.4.0 (subgraphs / node groups - React Flow model):
//  - NEW: CompositorNode::parentId. Children's x/y are RELATIVE to their
//         parent's world top-left. Cycles are rejected.
//  - NEW: CompositorNodeTemplate::isGroup (+ groupPadding, groupMinWidth,
//         groupMinHeight). Group nodes auto-fit their bounds to enclose
//         direct children; renderer skips param/preview drawing and uses
//         a more transparent body.
//  - NEW: SetNodeParent / GetNodeParent / GetChildrenOf / GetRootNodeIds /
//         GetNodeWorldPos. SetNodeParent preserves world position.
//  - NEW: Group(groupId, templateId, nodeIds) / Ungroup(groupId) conveniences.
//  - NEW: RegisterDefaultGroupTemplate() ships a usable group template
//         out of the box.
//  - NEW: onNodeParentChanged(nodeId, oldParent, newParent) callback.
//  - Per React Flow convention, parent assignment on drop is NOT automatic;
//    host wires it via onNodeDrag and calls SetNodeParent.
//  - Render order is tree-traversal (parents-before-children) so children
//    appear nested visually; hit-test order is the reverse so children win
//    clicks over their parent's body.
//  - Serialization writes parentId; Paste remaps parent ids and orphans
//    children whose parent isn't part of the paste.
//
// CHANGELOG 0.3.0 (Tier 2 polish features):
//  - NEW: Minimap overlay (CompositorMinimapConfig) - small floating
//         viewport indicator with click-to-pan. Off by default; enable via
//         SetMinimapVisible(true).
//  - NEW: Controls overlay (CompositorControlsConfig) - zoom in/out/fit/lock
//         buttons in a corner. Off by default.
//  - NEW: Edge reconnection - clicking a connected single-connection socket
//         starts a reconnect drag instead of a fresh connection. Drop on a
//         new compatible socket reroutes; Esc restores. Toggle via
//         SetEdgeReconnectionEnabled(bool); default true.
//  - NEW: Alignment guides - dragging a node shows sticky lines when its
//         edges/center align with other nodes within threshold, and snaps to
//         the alignment. Off by default; enable via SetAlignmentGuidesEnabled.
//  - NEW: Node palette helpers - SearchTemplates(query) returns ranked
//         template ids; Tab key in canvas fires onPaletteRequested(worldX,
//         worldY). Host renders the popup using its own widget toolkit.
//
// CHANGELOG 0.2.0 (Tier 1 productivity features):
//  - NEW: 5 additional socket data types - Time, Matrix, Trigger, List, Path.
//  - NEW: Rubber-band selection box. Shift+drag in empty canvas selects all
//         enclosed nodes; non-shift drag still pans. Renders a translucent
//         selection rectangle styled via CompositorDiagramStyle::selectionBox*.
//  - NEW: Snap-to-grid. SetSnapToGrid / SetSnapGrid. Applies during node
//         drag so multi-node alignment is automatic.
//  - NEW: Right-click hooks for node / link / socket / canvas. Default socket
//         right-click disconnects all links on the socket; the host can
//         override this by setting onSocketRightClick.
//  - NEW: Undo / Redo with bounded history (default 100 entries). Public
//         mutators record automatically; FromJson suppresses recording during
//         load. Closures capture pre/post state, so node removal restores all
//         dependent links on undo.
//  - NEW: Copy / Cut / Paste / Duplicate over selected nodes + intra-selection
//         links. Internal clipboard only (JSON-encoded); OS clipboard binding
//         is a separate concern. Paste rewrites ids to avoid collisions and
//         offsets positions for visibility.
//  - NEW: Keyboard shortcuts wired in HandleKeyDown:
//             Ctrl+Z         Undo
//             Ctrl+Y         Redo
//             Ctrl+Shift+Z   Redo
//             Ctrl+C / X / V Copy / Cut / Paste
//             Ctrl+D         Duplicate
//             Ctrl+A         Select all
//             Delete / Backspace  Delete selected
//             Escape         Cancel drag-to-connect / deselect
//
// CHANGELOG 0.1.0 (initial API sketch):
//  - NEW: Companion to UltraCanvasNodeDiagram. NodeDiagram models nodes as
//         opaque shapes with perimeter handles, suitable for graph viz and
//         simple flow editors. CompositorDiagram models each node as a
//         composite panel with a header bar, typed sockets along the sides,
//         parameter rows with embedded widgets, and an optional preview
//         slot - suitable for shader/material editors, VFX compositors,
//         audio routing graphs, ETL pipelines, and visual scripting.
//
//  - NEW: SocketType registry. Connections are TYPED (image, color, vector,
//         scalar, bool, string, custom). The diagram enforces compatibility
//         on connect; visual color is derived from the registered type.
//
//  - NEW: CompositorNodeTemplate. Declarative node definition - header
//         (title, icon, color), input sockets list, output sockets list,
//         ordered parameter rows (each row = optional socket + label +
//         optional widget), and an optional preview slot. Templates are
//         registered once and instantiated per-node so the editor can offer
//         a "node palette" without duplicating layout/render code.
//
//  - NEW: ParamWidgetKind. Per-row widget hints (None, Dropdown, NumberInput,
//         Checkbox, ColorSwatch, TextInput, Slider, FileBrowser). The
//         renderer/event handler dispatches on this enum; values live on the
//         node instance, not the template.
//
//  - NEW: ParamValue. Tagged-union-style parameter value (numeric / boolean /
//         string / color / vector) carried per row per node. Avoids std::variant
//         to keep ABI flat, mirroring the existing NodeDiagram* struct style.
//
//  - NEW: CompositorNode. Node instance: position, template id, current
//         parameter values, optional preview image handle, collapse state.
//         Sockets are NOT stored on the node - they are resolved from the
//         template by index, so socket positions stay in sync if the template
//         changes.
//
//  - NEW: CompositorLink. Typed link with type-checked endpoints. Routing is
//         Bezier by default (compositor convention); per-link override
//         available via the existing LinkStyle enum (reused from
//         UltraCanvasNodeDiagram.h).
//
// DESIGN NOTES:
//  - Why a separate class? See architecture rationale in the project's
//    diagram-types comparison. Compositor nodes need row-based layout,
//    per-row hit-testing for embedded widgets, typed-socket compatibility,
//    and per-node parameter values. Folding these into UltraCanvasNodeDiagram
//    would force every node-graph user to pay for compositor complexity and
//    would muddy the "opaque-shape" model. The two classes can share
//    LinkStyle, HandlePosition, and the link routing helpers, but the node
//    representation is fundamentally different.
//
//  - Widget rendering: this header only declares ParamWidgetKind. Actual
//    drawing is done in-place by the diagram's render pass (small inline
//    controls, not full UltraCanvasUIElement children) so we avoid the cost
//    of N widget instances per node. For full-widget power, callers should
//    fall back to opening an external popup (the existing pattern in the
//    NodeDiagram demo).
//
//  - Evaluation: this header exposes onParamChange / onLinkCreated /
//    onLinkRemoved hooks so an application can drive its own evaluation
//    graph (compute the image, recompile the shader, re-run the pipeline).
//    The component itself stores values but does not evaluate.

#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasCommonTypes.h"
#include "Plugins/Diagrams/UltraCanvasNodeDiagram.h"  // Reuse LinkStyle, HandlePosition, NodeDiagramPanelPosition
#include <vector>
#include <map>
#include <set>
#include <string>
#include <memory>
#include <functional>
#include <cstdint>

namespace UltraCanvas {

// =============================================================================
// COMPOSITOR DIAGRAM ENUMERATIONS
// =============================================================================

// Built-in socket data types. Apps can extend with Custom + a string tag.
// The numeric values are stable for serialization.
// Note: enum entries avoid the names `Bool` and `None` because X11 headers
// (pulled in transitively via cairo/pango) #define them as macros.
enum class SocketDataType {
    Any        = 0,   // Wildcard - connects to anything
    Image      = 1,   // Raster image / texture
    Color      = 2,   // RGBA color
    Vector     = 3,   // 2D/3D/4D vector
    Scalar     = 4,   // Float / int
    Boolean    = 5,
    String     = 6,
    Geometry   = 7,   // Mesh / curve
    Shader     = 8,   // Compiled shader fragment
    Audio      = 9,
    Data       = 10,  // Generic struct / record (ETL pipelines)
    Video      = 11,  // Video stream (frame sequence + optional audio track)
    Time       = 12,  // Timestamp / duration (semantically distinct from Scalar)
    Matrix     = 13,  // 3x3 / 4x4 transform (semantically distinct from Vector)
    Trigger    = 14,  // Discrete signal / exec pin (visual scripting)
    List       = 15,  // Collection / array (use customTag for element-type tagging)
    Path       = 16,  // Filesystem path (binds naturally to FileBrowser widget)
    Custom     = 99   // Use SocketTypeInfo::customTag for sub-typing
};

// Per-row widget hint. The diagram renders a small inline control of the
// chosen kind inside the node body and routes mouse events to it.
enum class ParamWidgetKind {
    NoWidget,      // Just a label, no editable control
    Dropdown,      // Combo box (choices from ParamSpec::choices)
    NumberInput,   // Numeric text field (min/max/step from ParamSpec)
    Checkbox,      // Boolean toggle
    ColorSwatch,   // Click opens color picker
    TextInput,     // Single-line text
    Slider,        // Horizontal slider (min/max from ParamSpec)
    FileBrowser    // Path + browse button
};

// Tag for what a ParamValue currently holds. Set by the template's ParamSpec.
enum class ParamValueKind {
    NoValue,
    Number,
    Boolean,
    Text,
    Color,
    Vector2,
    Vector3,
    Vector4
};

// =============================================================================
// SOCKET TYPE REGISTRY
// =============================================================================

// Visual + behavioural metadata for a socket data type. The diagram owns a
// registry keyed by SocketDataType (and optionally customTag for Custom).
struct SocketTypeInfo {
    SocketDataType type = SocketDataType::Any;
    std::string customTag;        // Used only when type == Custom

    std::string displayName;      // "Image", "Vector", ...
    Color color = Color(180, 180, 180, 255);       // Socket dot fill
    Color borderColor = Color(60, 60, 60, 255);    // Socket dot border
    double radius = 5.0f;

    // Optional: shape hint (Circle is the default; some compositors use
    // diamonds for "evaluate-on-demand" sockets etc.). Renderer decides.
    NodeShape dotShape = NodeShape::Circle;
};

// =============================================================================
// PARAMETER SPEC & VALUE
// =============================================================================

// Spec for one row inside a node template. Declares socket presence, label,
// widget kind, and value constraints. Pure metadata - no runtime state.
struct CompositorParamSpec {
    std::string id;               // Stable per-template id ("opacity", "blend_mode")
    std::string label;            // "Opacity", "Blend mode"

    // Socket presence. Most rows have either an input socket (left) OR no
    // socket (label-only / widget-only). Output sockets normally live in
    // the template's outputs[] list, not in parameter rows.
    bool hasInputSocket = false;
    SocketDataType inputType = SocketDataType::Any;
    std::string inputCustomTag;

    // Widget on the right side of the row.
    ParamWidgetKind widget = ParamWidgetKind::NoWidget;
    ParamValueKind valueKind = ParamValueKind::NoValue;

    // Constraints (interpretation depends on widget):
    //   NumberInput / Slider: numMin/numMax/numStep
    //   Dropdown:             choices (vector of label strings)
    double numMin  = 0.0;
    double numMax  = 1.0;
    double numStep = 0.01;
    std::vector<std::string> choices;

    // Default value, used to seed the node instance on creation.
    double  defaultNumber = 0.0;
    bool    defaultBool   = false;
    std::string defaultText;
    Color   defaultColor  = Color(255, 255, 255, 255);
    double  defaultVec[4] = {0, 0, 0, 0};
    int     defaultDropdownIndex = 0;
};

// Runtime value for one row on one node. Tagged flat struct (matches the
// project's existing style; no std::variant).
struct ParamValue {
    ParamValueKind kind = ParamValueKind::NoValue;

    double  number = 0.0;
    bool    boolean = false;
    std::string text;
    Color   color = Color(255, 255, 255, 255);
    double  vec[4] = {0, 0, 0, 0};
    int     dropdownIndex = 0;
};

// =============================================================================
// SOCKET SPEC (template-side socket declaration)
// =============================================================================

// One socket on a node template. Sockets live in the template's input/output
// lists; their layout order maps to their visual order on the side of the node.
struct CompositorSocketSpec {
    std::string id;               // Stable per-template ("uv", "color_out")
    std::string label;            // Shown next to the socket dot

    SocketDataType type = SocketDataType::Any;
    std::string customTag;        // For SocketDataType::Custom

    // Multi-connection: outputs typically allow many; inputs usually one.
    // Set to 0 for "unlimited".
    int maxConnections = 0;

    // If true, this socket is also visible while the node is collapsed.
    bool visibleWhenCollapsed = true;
};

// =============================================================================
// NODE TEMPLATE (declarative node definition)
// =============================================================================

// A reusable template that defines how a class of nodes looks and behaves.
// Apps register templates once at startup (or load from JSON) and then
// instantiate CompositorNode instances referencing the template by id.
struct CompositorNodeTemplate {
    std::string id;               // "merge", "separate_color", "output"
    std::string category;         // "Color", "Vector", "Output" - palette grouping

    // Header bar
    std::string title;            // "Merge", "Separate Color"
    std::string iconName;         // Optional icon hint (renderer maps to glyph)
    Color headerColor = Color(60, 100, 60, 255);
    Color headerTextColor = Color(255, 255, 255, 255);

    // Body panel
    Color bodyColor = Color(45, 45, 45, 235);
    Color borderColor = Color(20, 20, 20, 255);
    double borderWidth = 1.0f;
    double cornerRadius = 4.0f;

    // Sockets, in visual order (top -> bottom on their respective sides).
    std::vector<CompositorSocketSpec> inputs;
    std::vector<CompositorSocketSpec> outputs;

    // Parameter rows, rendered between the socket bands.
    std::vector<CompositorParamSpec> params;

    // Optional preview slot at the bottom of the node body
    // (thumbnail / image preview, like in shader editors).
    bool hasPreview = false;
    double previewHeight = 120.0f;     // 0 = auto from width

    // Sizing
    double defaultWidth = 180.0f;
    double minWidth = 120.0f;
    double maxWidth = 400.0f;
    // Height is derived from row count + preview + header.

    // Subgraph / group support. When isGroup is true:
    //   - Bounds auto-fit to enclose all direct child nodes (per groupPadding).
    //   - The renderer skips param-row and preview drawing; only header + body
    //     are drawn, with a more transparent body so children show through.
    //   - Default sockets/inputs/outputs/params are still respected if declared,
    //     so the template author can opt into "interface sockets" on the group
    //     itself (advanced; React Flow doesn't do this but the framework
    //     doesn't preclude it).
    bool   isGroup = false;
    double groupPadding   = 16.0;   // Outer padding around enclosed children
    double groupMinWidth  = 200.0;  // Lower bound on the auto-fit width
    double groupMinHeight = 120.0;  // Lower bound on the auto-fit height
};

// =============================================================================
// NODE INSTANCE
// =============================================================================

struct CompositorNode {
    std::string id;               // Unique per diagram
    std::string templateId;       // Refers to a registered template

    // Optional per-instance title override (e.g. "Image.001" instead of
    // template title "Image"). Empty = use template title.
    std::string titleOverride;

    // Position. When parentId is empty this is world-space top-left.
    // When parentId is non-empty this is RELATIVE to the parent's world-space
    // top-left (matching React Flow's convention). World position is computed
    // by walking the parent chain via GetNodeWorldPos.
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;           // 0 = use template defaultWidth

    bool collapsed = false;
    bool isSelected = false;
    bool isDragging = false;
    int zIndex = 0;

    // Parent node id (subgraph membership). Empty = top-level (no parent).
    // Cycles are rejected by SetNodeParent.
    std::string parentId;

    // Per-row parameter values, keyed by ParamSpec::id. Missing entries
    // fall back to the template's default for that param.
    std::map<std::string, ParamValue> paramValues;

    // Opaque handle to a preview image (texture id, file path, base64
    // thumbnail key - the app decides). Empty = no preview rendered.
    std::string previewHandle;

    CompositorNode() = default;
    CompositorNode(const std::string& nodeId, const std::string& tmplId,
                   double posX, double posY)
        : id(nodeId), templateId(tmplId), x(posX), y(posY) {}
};

// =============================================================================
// LINK
// =============================================================================

struct CompositorLink {
    std::string id;
    std::string sourceNodeId;
    std::string sourceSocketId;   // Refers to a CompositorSocketSpec::id on the template's outputs
    std::string targetNodeId;
    std::string targetSocketId;   // Refers to a CompositorSocketSpec::id on the template's inputs

    // Visual
    LinkStyle style = LinkStyle::Bezier;   // Reused from UltraCanvasNodeDiagram.h
    double lineWidth = 2.0f;
    // Line color: if 0-alpha, the renderer uses the source socket type's color.
    Color lineColorOverride = Color(0, 0, 0, 0);

    bool isSelected = false;
    bool selectable = true;
    bool deletable = true;
};

// =============================================================================
// STYLE
// =============================================================================

struct CompositorDiagramStyle {
    std::string fontFamily = "Arial";
    double headerFontSize = 11.0f;
    double rowFontSize = 10.0f;
    double socketLabelFontSize = 9.0f;

    Color backgroundColor = Color(38, 38, 38, 255);
    Color gridColor = Color(50, 50, 50, 255);
    bool showGrid = true;
    double gridSpacing = 25.0f;

    // Row layout
    double headerHeight = 22.0f;
    double rowHeight = 22.0f;
    double rowPaddingX = 10.0f;
    double socketLabelGap = 8.0f;
    double widgetMinWidth = 80.0f;

    // Selection
    Color selectionColor = Color(255, 160, 30, 255);
    double selectionWidth = 2.0f;

    // Rubber-band selection box (shift+drag in empty canvas)
    Color selectionBoxFill   = Color( 80, 130, 200,  40);
    Color selectionBoxStroke = Color( 80, 130, 200, 200);

    // In-progress connection
    Color connectionLineColor = Color(220, 220, 220, 220);
    double connectionLineWidth = 2.0f;
};

// =============================================================================
// SNAP-TO-GRID
// =============================================================================

struct CompositorSnapGrid {
    bool   enabled = false;
    double snapX = 20.0;
    double snapY = 20.0;
};

// =============================================================================
// MINIMAP & CONTROLS OVERLAY CONFIGS
// =============================================================================
// Structurally identical to UltraCanvasNodeDiagram's overlay configs (the two
// diagrams are independent but share the visual idiom; copying the struct
// rather than reusing avoids cross-coupling). The position enum
// NodeDiagramPanelPosition IS reused.

struct CompositorMinimapConfig {
    bool visible = false;
    NodeDiagramPanelPosition position = NodeDiagramPanelPosition::BottomRight;
    double width = 180.0;
    double height = 130.0;
    double padding = 10.0;
    Color backgroundColor = Color(30, 30, 30, 235);
    Color borderColor     = Color(80, 80, 80, 255);
    Color nodeColor       = Color(140, 160, 200, 255);
    Color viewportFill    = Color(80, 130, 200,  40);
    Color viewportStroke  = Color(80, 130, 200, 200);
    bool pannable = true;
};

struct CompositorControlsConfig {
    bool visible = false;
    NodeDiagramPanelPosition position = NodeDiagramPanelPosition::BottomLeft;
    double buttonSize = 28.0;
    double padding = 10.0;
    double gap = 4.0;
    Color backgroundColor = Color(40, 40, 40, 235);
    Color borderColor     = Color(80, 80, 80, 255);
    Color iconColor       = Color(220, 220, 220, 255);
    Color hoverColor      = Color(70, 70, 70, 255);
    bool showZoom = true;
    bool showFit = true;
    bool showLock = true;
};

// =============================================================================
// ALIGNMENT GUIDES
// =============================================================================

struct CompositorAlignmentGuide {
    bool   vertical = true;     // true = vertical line (constant X), false = horizontal
    double position = 0.0;      // world coord on the constant axis
    double start = 0.0;         // world coord on the other axis (line endpoints)
    double end = 0.0;
};

// =============================================================================
// COMPOSITOR DIAGRAM COMPONENT CLASS
// =============================================================================

class UltraCanvasCompositorDiagram : public UltraCanvasUIElement {
public:
    UltraCanvasCompositorDiagram(const std::string& id, int x, int y, int width, int height);
    bool AcceptsFocus() const override { return true; }

    // =========================================================================
    // SOCKET TYPE REGISTRY
    // =========================================================================
    // Apps register all socket types they intend to use before adding templates.
    // Built-in types (Image, Color, Vector, ...) are pre-registered with
    // sensible defaults on construction; calling RegisterSocketType again with
    // the same key overrides those defaults.

    void RegisterSocketType(const SocketTypeInfo& info);
    void RegisterCustomSocketType(const std::string& customTag, const SocketTypeInfo& info);
    const SocketTypeInfo* GetSocketTypeInfo(SocketDataType type,
                                            const std::string& customTag = "") const;

    // Compatibility check used by the link validator and drag-to-connect.
    // Default rule: types match exactly, OR either side is SocketDataType::Any,
    // OR app overrides via SetSocketCompatibilityChecker().
    bool AreSocketsCompatible(const CompositorSocketSpec& src,
                              const CompositorSocketSpec& dst) const;
    void SetSocketCompatibilityChecker(
        std::function<bool(const CompositorSocketSpec&,
                           const CompositorSocketSpec&)> checker);

    // =========================================================================
    // TEMPLATE MANAGEMENT
    // =========================================================================

    void RegisterTemplate(const CompositorNodeTemplate& tmpl);
    void RemoveTemplate(const std::string& templateId);
    const CompositorNodeTemplate* GetTemplate(const std::string& templateId) const;
    std::vector<std::string> GetAllTemplateIds() const;
    std::vector<std::string> GetTemplateIdsByCategory(const std::string& category) const;

    // =========================================================================
    // NODE MANAGEMENT
    // =========================================================================

    // Instantiate from a registered template. Seeds paramValues from the
    // template's defaults. Returns false if templateId is unknown.
    bool AddNode(const std::string& nodeId, const std::string& templateId,
                 double x, double y);
    void AddNode(const CompositorNode& node);

    void RemoveNode(const std::string& nodeId);
    void UpdateNodePosition(const std::string& nodeId, double x, double y);
    void SetNodeCollapsed(const std::string& nodeId, bool collapsed);
    void SetNodeTitleOverride(const std::string& nodeId, const std::string& title);
    void SetNodePreviewHandle(const std::string& nodeId, const std::string& handle);

    CompositorNode* GetNode(const std::string& nodeId);
    const CompositorNode* GetNode(const std::string& nodeId) const;
    std::vector<std::string> GetAllNodeIds() const;

    // =========================================================================
    // PARAMETER VALUES
    // =========================================================================
    // Convenience accessors so apps don't have to walk paramValues by hand.
    // All setters fire onParamChange after the value is committed.

    bool SetParamNumber(const std::string& nodeId, const std::string& paramId, double value);
    bool SetParamBool   (const std::string& nodeId, const std::string& paramId, bool value);
    bool SetParamText   (const std::string& nodeId, const std::string& paramId, const std::string& value);
    bool SetParamColor  (const std::string& nodeId, const std::string& paramId, const Color& value);
    bool SetParamDropdownIndex(const std::string& nodeId, const std::string& paramId, int index);

    const ParamValue* GetParamValue(const std::string& nodeId,
                                    const std::string& paramId) const;

    // =========================================================================
    // LINK MANAGEMENT
    // =========================================================================
    // AddLink validates type compatibility via AreSocketsCompatible and respects
    // maxConnections on both sockets. Returns false (and does NOT add) if the
    // link would be invalid. Use TryConnect for explicit drag-to-connect flow.

    bool AddLink(const std::string& linkId,
                 const std::string& sourceNodeId, const std::string& sourceSocketId,
                 const std::string& targetNodeId, const std::string& targetSocketId);
    bool AddLink(const CompositorLink& link);

    void RemoveLink(const std::string& linkId);
    void RemoveLinksOnSocket(const std::string& nodeId, const std::string& socketId);

    CompositorLink* GetLink(const std::string& linkId);
    std::vector<std::string> GetAllLinkIds() const;

    // Query: enumerate links attached to a given socket on a given node.
    std::vector<std::string> GetLinksForSocket(const std::string& nodeId,
                                               const std::string& socketId) const;

    // =========================================================================
    // STYLING
    // =========================================================================

    void SetStyle(const CompositorDiagramStyle& style);
    const CompositorDiagramStyle& GetStyle() const { return style; }
    void SetBackgroundColor(const Color& color);
    void SetGridVisible(bool visible, double spacing = 25.0f);

    // =========================================================================
    // SELECTION
    // =========================================================================

    void SelectNode(const std::string& nodeId, bool addToSelection = false);
    void SelectLink(const std::string& linkId, bool addToSelection = false);
    void SelectAll();
    void DeselectAll();
    void DeleteSelected();
    void Clear();

    std::vector<std::string> GetSelectedNodeIds() const;
    std::vector<std::string> GetSelectedLinkIds() const;
    bool IsNodeSelected(const std::string& nodeId) const;
    bool IsLinkSelected(const std::string& linkId) const;

    // =========================================================================
    // VIEWPORT  (mirrors UltraCanvasNodeDiagram - same semantics)
    // =========================================================================

    void SetZoomLevel(double zoom);
    void SetPanOffset(double x, double y);
    double GetZoomLevel() const { return zoomLevel; }
    Point2Df GetPanOffset() const { return panOffset; }

    void ZoomIn(double factor = 1.2f);
    void ZoomOut(double factor = 1.2f);
    void FitView(double padding = 40.0f);
    void CenterOn(double worldX, double worldY);

    void SetMinZoom(double minZ) { minZoom = minZ; }
    void SetMaxZoom(double maxZ) { maxZoom = maxZ; }

    // =========================================================================
    // INTERACTION SETTINGS
    // =========================================================================

    void SetInteractive(bool interactive)        { isInteractive = interactive; }
    bool IsInteractive() const                   { return isInteractive; }
    void SetNodesConnectable(bool connectable)   { nodesConnectable = connectable; }
    void SetPanOnDrag(bool pan)                  { panOnDrag = pan; }
    void SetZoomOnScroll(bool zoom)              { zoomOnScroll = zoom; }

    // =========================================================================
    // SNAP-TO-GRID
    // =========================================================================

    void SetSnapToGrid(bool enabled);
    void SetSnapGrid(double snapX, double snapY);
    bool IsSnapToGridEnabled() const             { return snapGrid.enabled; }

    // =========================================================================
    // HISTORY (UNDO / REDO)
    // =========================================================================
    // Public mutators (AddNode/RemoveNode/UpdateNodePosition/AddLink/RemoveLink/
    // SetParam*/Paste etc.) push undo entries automatically. Set
    // SetHistoryRecording(false) to suppress recording during programmatic
    // batch loads; FromJson does this internally.

    void Undo();
    void Redo();
    bool CanUndo() const                         { return !undoStack.empty(); }
    bool CanRedo() const                         { return !redoStack.empty(); }
    void ClearHistory();
    void SetHistoryLimit(size_t limit)           { historyLimit = limit; }
    void SetHistoryRecording(bool enabled)       { historyRecording = enabled; }
    size_t GetUndoStackSize() const              { return undoStack.size(); }
    size_t GetRedoStackSize() const              { return redoStack.size(); }

    // =========================================================================
    // CLIPBOARD (COPY / PASTE / DUPLICATE)
    // =========================================================================
    // Copies selected nodes + intra-selection links to an internal clipboard
    // buffer (JSON-encoded). Does NOT touch the OS clipboard - cross-platform
    // OS clipboard binding is a separate concern.

    void Copy();
    void Cut();
    void Paste(double offsetX = 20.0, double offsetY = 20.0);
    void Duplicate(double offsetX = 20.0, double offsetY = 20.0);
    bool HasClipboard() const                    { return !clipboard.empty(); }

    // =========================================================================
    // MINIMAP & CONTROLS OVERLAYS
    // =========================================================================

    void SetMinimapVisible(bool visible);
    void SetMinimapPosition(NodeDiagramPanelPosition pos);
    void SetMinimapConfig(const CompositorMinimapConfig& cfg);
    const CompositorMinimapConfig& GetMinimapConfig() const { return minimapConfig; }

    void SetControlsVisible(bool visible);
    void SetControlsPosition(NodeDiagramPanelPosition pos);
    void SetControlsConfig(const CompositorControlsConfig& cfg);
    const CompositorControlsConfig& GetControlsConfig() const { return controlsConfig; }

    // =========================================================================
    // EDGE RECONNECTION
    // =========================================================================
    // When enabled (default true), clicking a connected single-connection
    // socket starts an edge-reconnect drag instead of a fresh connection.
    // Drop on a new compatible socket reroutes the edge; Esc restores it;
    // drop on empty canvas leaves it removed.

    void SetEdgeReconnectionEnabled(bool enabled) { edgeReconnectionEnabled = enabled; }
    bool IsEdgeReconnectionEnabled() const        { return edgeReconnectionEnabled; }

    // =========================================================================
    // ALIGNMENT GUIDES
    // =========================================================================
    // When enabled (default false), dragging a node shows alignment guides
    // when its edges/center match other nodes' edges/center within threshold,
    // and snaps to that alignment.

    void SetAlignmentGuidesEnabled(bool enabled)  { alignmentGuidesEnabled = enabled; }
    bool IsAlignmentGuidesEnabled() const         { return alignmentGuidesEnabled; }
    void SetAlignmentGuideThreshold(double px)    { alignmentGuideThreshold = px; }

    // =========================================================================
    // NODE PALETTE HELPERS
    // =========================================================================
    // The diagram does NOT render the palette popup itself - host renders it.
    // SearchTemplates returns matching template ids ranked by match quality
    // (prefix > title contains > category contains). onPaletteRequested fires
    // when the user presses Tab while hovering the canvas.

    std::vector<std::string> SearchTemplates(const std::string& query) const;

    // =========================================================================
    // SUBGRAPHS / NODE GROUPS  (React Flow model)
    // =========================================================================
    // A subgraph is a regular node whose template has isGroup=true. Other
    // nodes become its children by setting their parentId. Children are
    // positioned RELATIVE to the parent; the parent's bounds auto-fit to
    // enclose them.
    //
    // The framework does NOT automatically reparent on drop - matching React
    // Flow, the host wires that itself by listening to onNodeDrag /
    // onSelectionChange and calling SetNodeParent.

    // Set or clear the parent of a node. Empty newParentId = make it a root
    // node. Preserves the node's WORLD position by adjusting its relative
    // position. Rejects (returns false) if the change would create a cycle.
    bool SetNodeParent(const std::string& nodeId, const std::string& newParentId);

    // Lookup helpers.
    std::string GetNodeParent(const std::string& nodeId) const;
    std::vector<std::string> GetChildrenOf(const std::string& nodeId) const;
    std::vector<std::string> GetRootNodeIds() const;
    Point2Df GetNodeWorldPos(const std::string& nodeId) const;

    // Convenience: create a new group node and reparent the given nodes into
    // it. The group is positioned to enclose all the given nodes plus padding.
    // Returns the new group's id (the caller-supplied one, or generated if
    // empty). Returns "" if templateId isn't registered or doesn't have
    // isGroup=true.
    std::string Group(const std::string& groupId,
                      const std::string& groupTemplateId,
                      const std::vector<std::string>& nodeIds);

    // Convenience: remove the group node and promote its children to roots,
    // preserving their world positions. The links of the children survive
    // unchanged.
    void Ungroup(const std::string& groupId);

    // Convenience: registers a built-in "group" template (isGroup=true,
    // dark translucent body, no inputs/outputs/params). Use templateId="group"
    // when adding group nodes. Apps can also register their own templates with
    // isGroup=true directly via RegisterTemplate if they want custom styling.
    void RegisterDefaultGroupTemplate(const std::string& templateId = "group",
                                       const std::string& title = "Group");

    // =========================================================================
    // SERIALIZATION
    // =========================================================================
    // Serializes registered templates, nodes (with paramValues), and links.
    // Socket type registry is NOT serialized - apps re-register types on load.

    std::string ToJson() const;
    bool FromJson(const std::string& json);

    // =========================================================================
    // RENDERING & EVENTS
    // =========================================================================

    void Render(IRenderContext* ctx, const Rect2Di& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;

    // =========================================================================
    // CALLBACKS
    // =========================================================================

    std::function<void(const std::string& nodeId)>                       onNodeClick;
    std::function<void(const std::string& nodeId)>                       onNodeDoubleClick;
    std::function<void(const std::string& nodeId, double x, double y)>   onNodeDrag;
    std::function<void(const std::string& linkId)>                       onLinkClick;
    std::function<void(const CompositorLink&)>                           onLinkCreated;
    std::function<void(const std::string& linkId)>                       onLinkRemoved;

    // Fires whenever a parameter value is committed (SetParam* or in-place
    // edit through a row widget). App should re-evaluate the downstream graph.
    std::function<void(const std::string& nodeId,
                       const std::string& paramId,
                       const ParamValue& newValue)> onParamChange;

    // Selection change: (nodeIds, linkIds)
    std::function<void(const std::vector<std::string>&,
                       const std::vector<std::string>&)> onSelectionChange;

    // Right-click hooks - the diagram does NOT show a built-in menu; the host
    // app is expected to render a popup using the (worldX, worldY) anchor.
    // Setting onSocketRightClick suppresses the default "disconnect all on
    // socket" behaviour; if unset, right-clicking a socket disconnects it.
    std::function<void(double worldX, double worldY)> onCanvasRightClick;
    std::function<void(const std::string& nodeId,
                       double worldX, double worldY)> onNodeRightClick;
    std::function<void(const std::string& linkId,
                       double worldX, double worldY)> onLinkRightClick;
    std::function<void(const std::string& nodeId, const std::string& socketId,
                       bool isOutput,
                       double worldX, double worldY)> onSocketRightClick;

    // Drag-to-connect failed because sockets were incompatible. Lets the app
    // surface a tooltip explaining why.
    std::function<void(const CompositorSocketSpec& src,
                       const CompositorSocketSpec& dst)> onConnectionRejected;

    // History changed (push / undo / redo / clear). The bool indicates whether
    // the most recent operation can be undone (false right after ClearHistory).
    std::function<void()> onHistoryChange;

    // User pressed Tab while hovering the canvas. Host renders a searchable
    // popup at (worldX, worldY) using SearchTemplates(query) for filtering.
    std::function<void(double worldX, double worldY)> onPaletteRequested;

    // A node's parentId changed (entered, left, or moved between subgraphs).
    // oldParentId / newParentId are empty for the root level.
    std::function<void(const std::string& nodeId,
                       const std::string& oldParentId,
                       const std::string& newParentId)> onNodeParentChanged;

private:
    // =========================================================================
    // LAYOUT HELPERS (computed - not stored on instances)
    // =========================================================================
    // The renderer computes per-frame geometry from the template + node.x/y/width
    // so a template edit is reflected immediately without touching instances.

    struct NodeLayout {
        Rect2Df bounds;
        Rect2Df headerBounds;
        Rect2Df bodyBounds;
        Rect2Df previewBounds;          // Empty if template->hasPreview false
        std::vector<Rect2Df> inputSocketBounds;   // Hit-region of each socket dot
        std::vector<Rect2Df> outputSocketBounds;
        std::vector<Rect2Df> rowBounds;           // Per-param-row, body-relative
        std::vector<Rect2Df> rowWidgetBounds;     // Per-param-row widget hit-region
    };

    NodeLayout ComputeNodeLayout(const CompositorNode& node,
                                  const CompositorNodeTemplate& tmpl) const;
    Point2Df GetSocketWorldPosition(const CompositorNode& node,
                                     const CompositorNodeTemplate& tmpl,
                                     bool isOutput, int socketIndex) const;
    // Resolves a socket id (which may live on outputs[], inputs[], or as a
    // param row with hasInputSocket=true) to a world-space position. Returns
    // false if the id can't be found on the template.
    bool ResolveSocketWorldPosition(const CompositorNode& node,
                                     const CompositorNodeTemplate& tmpl,
                                     const std::string& socketId,
                                     bool& outIsOutput,
                                     Point2Df& outPos) const;

    // =========================================================================
    // HIT TESTING
    // =========================================================================

    std::string FindNodeAt(const Point2Di& screenPos);
    std::string FindLinkAt(const Point2Di& screenPos);
    // Returns (nodeId, socketId, isOutput) for a socket dot hit. socketId empty = miss.
    struct SocketHit { std::string nodeId; std::string socketId; bool isOutput = false; };
    SocketHit FindSocketAt(const Point2Di& screenPos);
    // Returns (nodeId, paramId) for a row widget hit. paramId empty = miss.
    struct WidgetHit { std::string nodeId; std::string paramId; };
    WidgetHit FindRowWidgetAt(const Point2Di& screenPos);

    // =========================================================================
    // RENDERING HELPERS
    // =========================================================================

    void RenderGrid(IRenderContext* ctx);
    void RenderLinks(IRenderContext* ctx);
    void RenderNodes(IRenderContext* ctx);
    void RenderNodePanel(IRenderContext* ctx, const CompositorNode& node,
                          const CompositorNodeTemplate& tmpl,
                          const NodeLayout& layout);
    void RenderRowWidget(IRenderContext* ctx, const CompositorNode& node,
                          const CompositorParamSpec& spec,
                          const ParamValue& value, const Rect2Df& widgetBounds);
    void RenderConnectionLine(IRenderContext* ctx);   // In-progress drag
    void RenderSelectionBox(IRenderContext* ctx);
    void RenderMinimap(IRenderContext* ctx);          // Screen space (after PopState)
    void RenderControls(IRenderContext* ctx);         // Screen space (after PopState)
    void RenderAlignmentGuides(IRenderContext* ctx);  // World space

    // Minimap & controls helpers (overlays live in screen space, so they hit-test
    // in element-local pixel coords).
    bool PointInMinimap(const Point2Di& screenPos) const;
    int  FindControlButtonAt(const Point2Di& screenPos);
    Rect2Df GetMinimapRectScreen() const;
    void HandleMinimapDrag(const Point2Di& screenPos);

    // Alignment guides: recomputes during node drag and stores the matched
    // lines for rendering. Returns the snap adjustment to apply to the
    // (already grid-snapped) drag delta.
    Point2Df ComputeAlignmentSnap(const std::string& leaderId,
                                   const Point2Df& proposedTopLeft);

    // =========================================================================
    // EVENT SUB-HANDLERS
    // =========================================================================

    bool HandleMouseDown(const UCEvent& event);
    bool HandleMouseUp(const UCEvent& event);
    bool HandleMouseMove(const UCEvent& event);
    bool HandleMouseWheel(const UCEvent& event);
    bool HandleKeyDown(const UCEvent& event);

    // =========================================================================
    // UTILITY
    // =========================================================================

    Point2Df ScreenToWorld(const Point2Di& screenPos) const;
    Point2Di WorldToScreen(const Point2Df& worldPos) const;
    Point2Df SnapWorldPoint(const Point2Df& p) const;
    void NotifySelectionChange();
    void NotifyHistoryChange();
    void ClampZoom();
    bool TryConnect(const std::string& srcNodeId, const std::string& srcSocketId,
                    const std::string& dstNodeId, const std::string& dstSocketId,
                    std::string& outNewLinkId);

    // Internal raw mutators - same effect as the public ones but bypass
    // history recording. Used by the Undo/Redo lambdas.
    void ApplyAddNode(const CompositorNode& node);
    void ApplyRemoveNode(const std::string& nodeId);
    void ApplyAddLink(const CompositorLink& link);
    void ApplyRemoveLink(const std::string& linkId);
    void ApplySetParamValue(const std::string& nodeId, const std::string& paramId,
                             const ParamValue& value);
    void ApplyMoveNodes(const std::map<std::string, Point2Df>& positions);
    // Raw reparent: sets parentId AND relative position without preserving
    // world position (callers compute world preservation themselves).
    void ApplySetNodeParent(const std::string& nodeId, const std::string& newParentId,
                             double relX, double relY);

    // Builds a render-order list: roots first in their nodeOrder, then each
    // root's descendants recursively. Used by both rendering (forward) and
    // hit-testing (reverse). Cheap enough to compute per frame.
    std::vector<std::string> BuildRenderOrder() const;

    // Writes a single node and a single link as JSON object literals into the
    // provided ostream - factored out so Copy() can reuse the same formatting
    // as ToJson(). The corresponding parser is ParseNodeFromObject.
    void WriteNodeJson(std::ostream& out, const CompositorNode& n) const;
    void WriteLinkJson(std::ostream& out, const CompositorLink& l) const;
    static bool ParseNodeFromObject(const std::string& obj, CompositorNode& out);
    static bool ParseLinkFromObject(const std::string& obj, CompositorLink& out);

    // Returns an id derived from `base` that does not collide with anything
    // currently in `nodes`. Used by Paste to avoid id collisions.
    std::string GenerateUniqueNodeId(const std::string& base) const;
    std::string GenerateUniqueLinkId() const;

    // PushHistory is a no-op when historyRecording is false.
    struct HistoryEntry {
        std::function<void()> undo;
        std::function<void()> redo;
        std::string description;
    };
    void PushHistory(HistoryEntry entry);

    // =========================================================================
    // DATA MEMBERS
    // =========================================================================

    std::map<std::string, CompositorNodeTemplate> templates;
    std::map<std::string, CompositorNode>         nodes;
    std::vector<std::string>                       nodeOrder;   // z-order
    std::vector<CompositorLink>                    links;

    // Socket type registry (built-ins seeded in the constructor)
    std::map<int, SocketTypeInfo>                  socketTypes;          // keyed by SocketDataType
    std::map<std::string, SocketTypeInfo>          customSocketTypes;    // keyed by customTag
    std::function<bool(const CompositorSocketSpec&,
                       const CompositorSocketSpec&)> compatibilityChecker;

    CompositorDiagramStyle style;

    // Selection
    std::set<std::string> selectedNodes;
    std::set<std::string> selectedLinks;
    std::string hoveredNodeId;
    std::string hoveredLinkId;
    std::string hoveredSocketNodeId;
    std::string hoveredSocketId;
    bool        hoveredSocketIsOutput = false;

    // Mouse interaction state
    bool isDraggingNode = false;
    bool isDraggingViewport = false;
    bool isConnecting = false;
    bool isSelectingBox = false;
    Point2Di dragStartPos;
    Point2Di lastMousePos;
    std::map<std::string, Point2Df> dragStartPositions;
    // Selection set at the start of a rubber-band drag, so we can extend
    // (shift+drag) rather than replace.
    std::set<std::string> selectionBoxStartNodes;
    std::set<std::string> selectionBoxStartLinks;

    // In-progress connection state
    std::string connectionSourceNode;
    std::string connectionSourceSocket;
    bool        connectionSourceIsOutput = true;
    Point2Df    connectionEndPoint;

    // Rubber-band selection box (world coords)
    Point2Df selectionBoxStart;
    Point2Df selectionBoxEnd;

    // Viewport
    double  zoomLevel = 1.0f;
    Point2Df panOffset;
    double  minZoom = 0.1f;
    double  maxZoom = 5.0f;

    // Interaction toggles
    bool isInteractive    = true;
    bool nodesConnectable = true;
    bool panOnDrag        = true;
    bool zoomOnScroll     = true;
    bool isMultiSelectKeyHeld = false;

    // Snap to grid
    CompositorSnapGrid snapGrid;

    // Minimap & controls overlays
    CompositorMinimapConfig minimapConfig;
    CompositorControlsConfig controlsConfig;
    bool isDraggingMinimap = false;
    int  hoveredControlButton = -1;

    // Edge reconnection state
    bool edgeReconnectionEnabled = true;
    bool isReconnectingEdge = false;
    CompositorLink reconnectSavedLink;   // Original edge captured at drag start

    // Alignment guides
    bool alignmentGuidesEnabled = false;
    double alignmentGuideThreshold = 4.0;
    std::vector<CompositorAlignmentGuide> currentAlignmentGuides;
    std::string dragLeaderId;  // Primary node for alignment snap during multi-drag

    // Undo / redo history
    std::vector<HistoryEntry> undoStack;
    std::vector<HistoryEntry> redoStack;
    size_t historyLimit = 100;
    bool   historyRecording = true;

    // Clipboard (in-process; not the OS clipboard)
    std::string clipboard;

    // Counter for generating unique link ids during paste/drag-to-connect
    mutable unsigned int linkIdCounter = 0;
};

// =============================================================================
// FACTORY FUNCTION
// =============================================================================

inline std::shared_ptr<UltraCanvasCompositorDiagram> CreateCompositorDiagram(
        const std::string& id, int x, int y, int w, int h) {
    return std::make_shared<UltraCanvasCompositorDiagram>(id, x, y, w, h);
}

} // namespace UltraCanvas
