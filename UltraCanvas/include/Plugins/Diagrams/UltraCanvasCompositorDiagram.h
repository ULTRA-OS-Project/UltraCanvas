// include/Plugins/Diagrams/UltraCanvasCompositorDiagram.h
// Compositor / shader-style node graph component for node-based editors
// where each node is a composite panel (header + typed sockets + embedded
// widgets + optional preview), as opposed to UltraCanvasNodeDiagram's
// opaque-shape model used for network/graph visualization.
//
// Version: 0.1.0 (API SKETCH - header only, no implementation yet)
// Last Modified: 2026-05-22
// Author: UltraCanvas Framework
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

    // Position (world coords, top-left of node bounds)
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;           // 0 = use template defaultWidth

    bool collapsed = false;
    bool isSelected = false;
    bool isDragging = false;
    int zIndex = 0;

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

    // In-progress connection
    Color connectionLineColor = Color(220, 220, 220, 220);
    double connectionLineWidth = 2.0f;
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

    // Right-click on empty canvas - typical use is to open a "create node"
    // palette at worldX/worldY.
    std::function<void(double worldX, double worldY)> onCanvasRightClick;

    // Drag-to-connect failed because sockets were incompatible. Lets the app
    // surface a tooltip explaining why.
    std::function<void(const CompositorSocketSpec& src,
                       const CompositorSocketSpec& dst)> onConnectionRejected;

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
    void NotifySelectionChange();
    void ClampZoom();
    bool TryConnect(const std::string& srcNodeId, const std::string& srcSocketId,
                    const std::string& dstNodeId, const std::string& dstSocketId,
                    std::string& outNewLinkId);

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
    Point2Di dragStartPos;
    Point2Di lastMousePos;
    std::map<std::string, Point2Df> dragStartPositions;

    // In-progress connection state
    std::string connectionSourceNode;
    std::string connectionSourceSocket;
    bool        connectionSourceIsOutput = true;
    Point2Df    connectionEndPoint;

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
};

// =============================================================================
// FACTORY FUNCTION
// =============================================================================

inline std::shared_ptr<UltraCanvasCompositorDiagram> CreateCompositorDiagram(
        const std::string& id, int x, int y, int w, int h) {
    return std::make_shared<UltraCanvasCompositorDiagram>(id, x, y, w, h);
}

} // namespace UltraCanvas
