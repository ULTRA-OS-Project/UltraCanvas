# UltraCanvasCompositorDiagram Documentation

<!-- doc-check: std::shared_ptr<UltraCanvasCompositorDiagram> diagram; std::shared_ptr<UltraCanvasLabel> statusLabel; std::shared_ptr<UltraCanvasContainer> container; std::shared_ptr<UltraCanvasContainer> buttonBar; void SaveGraph(std::shared_ptr<UltraCanvasCompositorDiagram> diagram); void LoadGraph(std::shared_ptr<UltraCanvasCompositorDiagram> diagram); -->

**UltraCanvasCompositorDiagram** is a node-graph editor in which every node is
a composite panel: a coloured header, typed sockets down both sides, parameter
rows with small embedded widgets (dropdown, number field, checkbox, colour
swatch, text field, slider, file browser) and an optional preview slot. It is
the component for shader / material editors, layer compositors, VFX and audio
routing graphs, ETL pipelines and visual scripting. Nodes can be nested in
group nodes (subgraphs), and the editor has undo / redo, an internal
clipboard, JSON save / load, a minimap, a zoom / fit / lock controls panel,
snap-to-grid and alignment guides.

## Contents

- [Overview](#overview)
- [Header Include](#header-include)
- [Quick Start](#quick-start)
- [Concepts](#concepts)
  - [Templates](#templates)
  - [Nodes](#nodes)
  - [Sockets and pins](#sockets-and-pins)
  - [Parameter rows and embedded widgets](#parameter-rows-and-embedded-widgets)
  - [Links (edges)](#links-edges)
  - [Groups and subgraphs](#groups-and-subgraphs)
- [Data Structures](#data-structures)
  - [Enumerations](#enumerations)
  - [SocketTypeInfo](#sockettypeinfo)
  - [CompositorSocketSpec](#compositorsocketspec)
  - [CompositorParamSpec and ParamValue](#compositorparamspec-and-paramvalue)
  - [CompositorNodeTemplate](#compositornodetemplate)
  - [CompositorNode](#compositornode)
  - [CompositorLink](#compositorlink)
  - [CompositorDiagramStyle](#compositordiagramstyle)
- [API Reference](#api-reference)
  - [Construction](#construction)
  - [Socket type registry](#socket-type-registry)
  - [Templates](#templates-1)
  - [Nodes](#nodes-1)
  - [Parameter values](#parameter-values)
  - [Links](#links)
  - [Styling](#styling)
  - [Selection](#selection)
  - [Viewport](#viewport)
  - [Interaction settings](#interaction-settings)
  - [Subgraphs and groups](#subgraphs-and-groups)
  - [Node palette search](#node-palette-search)
- [Editing Commands and Undo](#editing-commands-and-undo)
  - [Undo and redo](#undo-and-redo)
  - [Clipboard](#clipboard)
  - [Keyboard shortcuts](#keyboard-shortcuts)
  - [Mouse interaction](#mouse-interaction)
- [Serialization (Save / Load)](#serialization-save--load)
  - [Saving and loading through FileLoader](#saving-and-loading-through-fileloader)
- [View Features](#view-features)
  - [Minimap](#minimap)
  - [Controls overlay](#controls-overlay)
  - [Snap to grid](#snap-to-grid)
  - [Alignment guides](#alignment-guides)
  - [Fit view and zoom](#fit-view-and-zoom)
- [Callbacks](#callbacks)
- [Usage Examples](#usage-examples)
  - [Recreate Images - Layer Compositor](#recreate-images---layer-compositor)
  - [Subgraph / Groups](#subgraph--groups)
  - [Visual Scripting](#visual-scripting)
  - [Feature Playground](#feature-playground)
- [Known Limitations](#known-limitations)
- [See Also](#see-also)

## Overview

**Namespace:** `UltraCanvas`
**Header:** `include/Plugins/Diagrams/UltraCanvasCompositorDiagram.h`
**Source:** `UltraCanvas/Plugins/Diagrams/UltraCanvasCompositorDiagram.cpp`
**Base class:** `UltraCanvasUIElement`
**Version:** 0.4.0
**Demo:** `Apps/DemoApp/UltraCanvasCompositorDiagramExamples.cpp`
(Diagrams → Compositor Diagram)

How it differs from [UltraCanvasNodeDiagram](UltraCanvasNodeDiagramExamples.md):
the node diagram draws each node as an opaque shape with handles on its
perimeter, for graph visualisation and simple flow editors. The compositor
diagram lays each node out in rows from a **template**, checks socket **types**
when a link is made, and stores **parameter values** per node. Both share
`LinkStyle`, `NodeShape`, the panel-position enum and the pan / zoom / minimap /
controls machinery of [UltraCanvasDiagramViewport](UltraCanvasDiagramViewport.md).

The component stores values but does **not** evaluate the graph. An
application listens to `onParamChange`, `onLinkCreated` and `onLinkRemoved`
and recomputes its own result (an image, a shader, a pipeline run).

Feature summary:

- Typed sockets (`SocketDataType`, 18 built-in kinds plus `Custom` with a tag),
  compatibility checked on connect, colour taken from the socket type registry
- Declarative node templates, registered once and instantiated per node
- Parameter rows with inline widgets; values live on the node instance
- Optional preview slot, drawn by the component or by a custom renderer
- Links with Bezier / Straight / SmoothStep / Step routing and optional
  midpoint labels
- Group nodes with nested children (React Flow model)
- Undo / redo (bounded history), copy / cut / paste / duplicate
- Rubber-band selection, keyboard shortcuts, right-click hooks
- Minimap, controls overlay, snap-to-grid, alignment guides, fit view
- Node palette search (`SearchTemplates`) with a Tab-key request hook
- `ToJson()` / `FromJson()`

## Header Include

```cpp
#include "Plugins/Diagrams/UltraCanvasCompositorDiagram.h"
```

The header pulls in `UltraCanvasNodeDiagram.h` (for `LinkStyle`, `NodeShape`
and `NodeDiagramPanelPosition`) and, through it, `UltraCanvasDiagramViewport.h`.

## Quick Start

Register a template, add two nodes, connect them:

```cpp
auto diagram = CreateCompositorDiagram("graph", 0, 0, 900, 560);

CompositorNodeTemplate blur;
blur.id = "blur";
blur.category = "Filter";
blur.title = "Blur";
blur.headerColor = Color(120, 80, 200, 255);
{
    CompositorSocketSpec in;
    in.id = "in"; in.label = "Image"; in.type = SocketDataType::Image;
    in.maxConnections = 1;                 // inputs usually take one link
    blur.inputs.push_back(in);

    CompositorSocketSpec out;
    out.id = "out"; out.label = "Image"; out.type = SocketDataType::Image;
    blur.outputs.push_back(out);           // 0 = unlimited fan-out

    CompositorParamSpec radius;
    radius.id = "radius"; radius.label = "Radius";
    radius.widget = ParamWidgetKind::Slider;
    radius.valueKind = ParamValueKind::Number;
    radius.numMin = 0; radius.numMax = 50; radius.numStep = 1;
    radius.defaultNumber = 8;
    blur.params.push_back(radius);
}
diagram->RegisterTemplate(blur);

diagram->AddNode("blur1", "blur", 40, 60);
diagram->AddNode("blur2", "blur", 300, 60);
diagram->AddLink("L1", "blur1", "out", "blur2", "in");

diagram->onParamChange = [](const std::string& nodeId, const std::string& paramId,
                            const ParamValue& v) {
    // Re-evaluate the downstream graph here.
};

diagram->FitView(40.0);
container->AddChild(diagram);
```

## Concepts

### Templates

A `CompositorNodeTemplate` declares a class of node: its id, palette category,
header (title, icon hint, colours), body colours, input and output socket
lists, parameter rows, optional preview slot and sizing. Templates are
registered with `RegisterTemplate` before nodes that use them are added. A node
refers to its template by id; the layout (header, rows, socket positions) is
computed from the template every frame, so editing a template is reflected on
every node that uses it.

### Nodes

A `CompositorNode` is an instance: id, template id, position, optional width,
collapsed state, optional title override, optional parent id, the parameter
values (`paramValues`, keyed by `CompositorParamSpec::id`) and an opaque
`previewHandle`. `AddNode(nodeId, templateId, x, y)` seeds the parameter values
from the template's defaults.

The body height is derived: header + one row per socket and parameter + the
preview slot. The width is `node.width`, or the template's `defaultWidth` when
it is 0.

### Sockets and pins

Sockets are declared on the template, not stored on the node:

- **Output sockets** - `CompositorNodeTemplate::outputs`, drawn on the right.
- **Input sockets** - `CompositorNodeTemplate::inputs`, drawn on the left.
- **Parameter-row sockets** - a `CompositorParamSpec` with
  `hasInputSocket = true` gets an input dot at the left of its row, so a value
  can come either from the row's widget or from a link. A link targets it by
  the parameter's `id` (the demo's Branch node takes its `cond` this way).

Every socket has a `SocketDataType`. The default compatibility rule is: the
types are equal, **or** either side is `Any`; two `Custom` sockets must also
have equal `customTag`s. Replace the rule with `SetSocketCompatibilityChecker`.
`maxConnections` limits links per socket (0 = unlimited). A refused
drag-to-connect fires `onConnectionRejected`.

The socket colour, border colour, dot radius and dot shape come from the
**socket type registry** (`SocketTypeInfo`). All built-in types are
pre-registered; call `RegisterSocketType` to restyle one, or
`RegisterCustomSocketType` for a `Custom` tag. `Trigger` sockets are the
"exec pins" of visual scripting.

### Parameter rows and embedded widgets

Each `CompositorParamSpec` is one row: label, optional input socket and a
`ParamWidgetKind`. The widgets are drawn inline by the diagram (they are not
child elements). On a click:

| Widget | What a click does |
|--------|-------------------|
| `Dropdown` | Steps to the next entry of `choices` (wraps), via `SetParamDropdownIndex` |
| `Checkbox` | Toggles the value, via `SetParamBool` |
| `Slider` | Sets the value from the click position within `numMin..numMax`, rounded to `numStep` |
| `NumberInput`, `ColorSwatch`, `TextInput`, `FileBrowser` | Fires `onParamChange` with the current value; the host opens its own editor and writes back with `SetParam*` |
| `NoWidget` | Label only |

Values are `ParamValue` structs (a tagged flat struct, not `std::variant`).

### Links (edges)

A `CompositorLink` joins an output socket (`sourceNodeId` / `sourceSocketId`)
to an input socket (`targetNodeId` / `targetSocketId`). `AddLink` validates
the type and `maxConnections` on both ends and returns `false` without adding
anything when the link is invalid. Links are Bezier by default; set `style` to
another `LinkStyle`. The line colour is the source socket type's colour unless
`lineColorOverride` has a non-zero alpha. A non-empty `label` is drawn as a
pill at the midpoint of the routed path.

Edge reconnection (on by default): pressing on a connected socket that allows a
single connection picks up the existing link by that end; drop it on another
compatible socket to reroute it, on empty canvas to remove it, or press Escape
to restore it.

### Groups and subgraphs

A group is an ordinary node whose template has `isGroup = true`. Other nodes
join it by having their `parentId` set to the group's id:

- A child's `x` / `y` are **relative** to its parent's world top-left.
  `GetNodeWorldPos` walks the parent chain to the world position.
- A group's bounds fit its direct children automatically (plus
  `groupPadding`, at least `groupMinWidth` × `groupMinHeight`).
- Groups draw only a header and a translucent body; children render after
  (on top of) their parent, and win hit-tests over the parent's body.
- `SetNodeParent` keeps the node's world position and rejects cycles.
- Dropping a node on a group does **not** reparent it automatically. Wire that
  in `onNodeDrag` if wanted, and call `SetNodeParent`.

`RegisterDefaultGroupTemplate()` registers a ready-made group template
(id `"group"`), `Group()` wraps a set of nodes in a new group node and
`Ungroup()` dissolves one. Links between children of different groups are
ordinary links.

## Data Structures

### Enumerations

```cpp
enum class SocketDataType {
    Any        = 0,
    Image      = 1,
    Color      = 2,
    Vector     = 3,
    Scalar     = 4,
    Boolean    = 5,
    String     = 6,
    Geometry   = 7,
    Shader     = 8,
    Audio      = 9,
    Data       = 10,
    Video      = 11,
    Time       = 12,
    Matrix     = 13,
    Trigger    = 14,
    List       = 15,
    Path       = 16,
    Custom     = 99
};

enum class ParamWidgetKind {
    NoWidget,
    Dropdown,
    NumberInput,
    Checkbox,
    ColorSwatch,
    TextInput,
    Slider,
    FileBrowser
};

enum class ParamValueKind {
    Unset,
    Number,
    Boolean,
    Text,
    Color,
    Vector2,
    Vector3,
    Vector4
};
```

The names avoid `Bool`, `None` and `NoValue`, which X11 headers define as
macros.

### SocketTypeInfo

```cpp
struct SocketTypeInfo {
    SocketDataType type = SocketDataType::Any;
    std::string customTag;                         // only when type == Custom
    std::string displayName;
    Color color = Color(180, 180, 180, 255);       // socket dot fill
    Color borderColor = Color(60, 60, 60, 255);    // socket dot border
    double radius = 5.0f;
    NodeShape dotShape = NodeShape::Circle;
};
```

### CompositorSocketSpec

```cpp
struct CompositorSocketSpec {
    std::string id;               // stable per template ("uv", "color_out")
    std::string label;            // shown next to the dot
    SocketDataType type = SocketDataType::Any;
    std::string customTag;        // for SocketDataType::Custom
    int maxConnections = 0;       // 0 = unlimited
    bool visibleWhenCollapsed = true;
};
```

### CompositorParamSpec and ParamValue

```cpp
struct CompositorParamSpec {
    std::string id;
    std::string label;

    bool hasInputSocket = false;
    SocketDataType inputType = SocketDataType::Any;
    std::string inputCustomTag;

    ParamWidgetKind widget = ParamWidgetKind::NoWidget;
    ParamValueKind valueKind = ParamValueKind::Unset;

    double numMin  = 0.0;         // NumberInput / Slider
    double numMax  = 1.0;
    double numStep = 0.01;
    std::vector<std::string> choices;   // Dropdown

    double  defaultNumber = 0.0;
    bool    defaultBool   = false;
    std::string defaultText;
    Color   defaultColor  = Color(255, 255, 255, 255);
    double  defaultVec[4] = {0, 0, 0, 0};
    int     defaultDropdownIndex = 0;
};

struct ParamValue {
    ParamValueKind kind = ParamValueKind::Unset;
    double  number = 0.0;
    bool    boolean = false;
    std::string text;
    Color   color = Color(255, 255, 255, 255);
    double  vec[4] = {0, 0, 0, 0};
    int     dropdownIndex = 0;
};
```

A dropdown's selection is `dropdownIndex`, independent of `kind` (the demo's
dropdown rows use `ParamValueKind::Unset`).

### CompositorNodeTemplate

```cpp
struct CompositorNodeTemplate {
    std::string id;
    std::string category;         // palette grouping

    std::string title;
    std::string iconName;         // optional icon hint
    Color headerColor = Color(60, 100, 60, 255);
    Color headerTextColor = Color(255, 255, 255, 255);

    Color bodyColor = Color(45, 45, 45, 235);
    Color borderColor = Color(20, 20, 20, 255);
    double borderWidth = 1.0f;
    double cornerRadius = 4.0f;

    std::vector<CompositorSocketSpec> inputs;
    std::vector<CompositorSocketSpec> outputs;
    std::vector<CompositorParamSpec> params;

    bool hasPreview = false;
    double previewHeight = 120.0f;

    PreviewRenderFn customPreviewRenderer;

    double defaultWidth = 180.0f;
    double minWidth = 120.0f;
    double maxWidth = 400.0f;

    bool   isGroup = false;
    double groupPadding   = 16.0;
    double groupMinWidth  = 200.0;
    double groupMinHeight = 120.0;
};
```

`PreviewRenderFn` is
`std::function<void(IRenderContext* ctx, const Rect2Df& bounds, const CompositorNode& node)>`.
When it is set and `hasPreview` is true, it draws the whole preview slot
(`bounds` is the slot's world-space rectangle) instead of the default
checkerboard / preview-handle drawing.

### CompositorNode

```cpp
struct CompositorNode {
    std::string id;
    std::string templateId;
    std::string titleOverride;    // empty = template title

    double x = 0.0;               // relative to the parent when parentId is set
    double y = 0.0;
    double width = 0.0;           // 0 = template defaultWidth

    bool collapsed = false;
    bool isSelected = false;
    bool isDragging = false;
    int zIndex = 0;

    std::string parentId;         // empty = top level
    std::map<std::string, ParamValue> paramValues;
    std::string previewHandle;    // app-defined; empty = no preview image
};
```

It also has the constructor
`CompositorNode(const std::string& nodeId, const std::string& tmplId, double posX, double posY)`.

### CompositorLink

```cpp
struct CompositorLink {
    std::string id;
    std::string sourceNodeId;
    std::string sourceSocketId;
    std::string targetNodeId;
    std::string targetSocketId;

    LinkStyle style = LinkStyle::Bezier;
    double lineWidth = 2.0f;
    Color lineColorOverride = Color(0, 0, 0, 0);   // alpha 0 = source socket colour

    std::string label;                              // midpoint pill; empty = none
    Color labelBgColor = Color(60, 200, 120, 220);
    Color labelTextColor = Color(255, 255, 255, 255);

    bool isSelected = false;
    bool selectable = true;
    bool deletable = true;
};
```

### CompositorDiagramStyle

```cpp
struct CompositorDiagramStyle {
    std::string fontFamily = "Arial";
    double headerFontSize = 11.0f;
    double rowFontSize = 10.0f;
    double socketLabelFontSize = 9.0f;

    Color backgroundColor = Color(38, 38, 38, 255);
    Color gridColor = Color(50, 50, 50, 255);
    bool showGrid = true;
    double gridSpacing = 25.0f;

    double headerHeight = 22.0f;
    double rowHeight = 22.0f;
    double rowPaddingX = 10.0f;
    double socketLabelGap = 8.0f;
    double widgetMinWidth = 80.0f;

    Color selectionColor = Color(255, 160, 30, 255);
    double selectionWidth = 2.0f;

    Color selectionBoxFill   = Color( 80, 130, 200,  40);
    Color selectionBoxStroke = Color( 80, 130, 200, 200);

    Color connectionLineColor = Color(220, 220, 220, 220);
    double connectionLineWidth = 2.0f;
};
```

The defaults are a dark theme. For a light canvas, change the copy returned by
`GetStyle()` and hand it back:

```cpp
CompositorDiagramStyle st = diagram->GetStyle();
st.backgroundColor = Color(243, 247, 252, 255);
st.gridColor       = Color(228, 232, 240, 255);
st.showGrid = false;
diagram->SetStyle(st);
```

The overlay and snap types are aliases of the shared viewport's:
`CompositorSnapGrid` = `DiagramSnapGrid`, `CompositorMinimapConfig` =
`DiagramMinimapConfig`, `CompositorControlsConfig` = `DiagramControlsConfig`.
`MakeCompositorMinimapDefaults()` and `MakeCompositorControlsDefaults()`
return the dark-theme colours the constructor applies, and
`kCompositorDefaultSnap` (20.0) is the default snap step.

## API Reference

### Construction

```cpp
UltraCanvasCompositorDiagram(const std::string& id, int x, int y, int width, int height);
```

```cpp
auto diagram = CreateCompositorDiagram("graph", 0, 0, 900, 560);   // std::shared_ptr
```

The element accepts keyboard focus (`AcceptsFocus()` returns true).

### Socket type registry

```cpp
void RegisterSocketType(const SocketTypeInfo& info);
void RegisterCustomSocketType(const std::string& customTag, const SocketTypeInfo& info);
const SocketTypeInfo* GetSocketTypeInfo(SocketDataType type,
                                        const std::string& customTag = "") const;

bool AreSocketsCompatible(const CompositorSocketSpec& src,
                          const CompositorSocketSpec& dst) const;
void SetSocketCompatibilityChecker(
    std::function<bool(const CompositorSocketSpec&,
                       const CompositorSocketSpec&)> checker);
```

```cpp
// A custom "Mesh" socket type, and a rule letting Scalar feed Vector inputs.
SocketTypeInfo mesh;
mesh.type = SocketDataType::Custom;
mesh.customTag = "mesh";
mesh.displayName = "Mesh";
mesh.color = Color(0, 200, 170, 255);
mesh.dotShape = NodeShape::Diamond;
diagram->RegisterCustomSocketType("mesh", mesh);

diagram->SetSocketCompatibilityChecker(
    [](const CompositorSocketSpec& src, const CompositorSocketSpec& dst) {
        if (src.type == SocketDataType::Any || dst.type == SocketDataType::Any) return true;
        if (src.type == SocketDataType::Scalar && dst.type == SocketDataType::Vector) return true;
        if (src.type != dst.type) return false;
        return src.type != SocketDataType::Custom || src.customTag == dst.customTag;
    });
```

A custom checker replaces the default rule entirely, so it has to repeat the
parts of it that should still hold.

### Templates

```cpp
void RegisterTemplate(const CompositorNodeTemplate& tmpl);
void RemoveTemplate(const std::string& templateId);
const CompositorNodeTemplate* GetTemplate(const std::string& templateId) const;
std::vector<std::string> GetAllTemplateIds() const;
std::vector<std::string> GetTemplateIdsByCategory(const std::string& category) const;
```

### Nodes

```cpp
bool AddNode(const std::string& nodeId, const std::string& templateId,
             double x, double y);            // false if templateId is unknown
void AddNode(const CompositorNode& node);

void RemoveNode(const std::string& nodeId);  // also removes its links
void UpdateNodePosition(const std::string& nodeId, double x, double y);
void SetNodeCollapsed(const std::string& nodeId, bool collapsed);
void SetNodeTitleOverride(const std::string& nodeId, const std::string& title);
void SetNodePreviewHandle(const std::string& nodeId, const std::string& handle);

CompositorNode* GetNode(const std::string& nodeId);
const CompositorNode* GetNode(const std::string& nodeId) const;
std::vector<std::string> GetAllNodeIds() const;
```

### Parameter values

```cpp
bool SetParamNumber(const std::string& nodeId, const std::string& paramId, double value);
bool SetParamBool(const std::string& nodeId, const std::string& paramId, bool value);
bool SetParamText(const std::string& nodeId, const std::string& paramId, const std::string& value);
bool SetParamColor(const std::string& nodeId, const std::string& paramId, const Color& value);
bool SetParamDropdownIndex(const std::string& nodeId, const std::string& paramId, int index);

const ParamValue* GetParamValue(const std::string& nodeId,
                                const std::string& paramId) const;
```

Every setter records an undo step and fires `onParamChange`.

```cpp
diagram->SetParamText("printT", "msg", "A is greater");
if (const ParamValue* v = diagram->GetParamValue("blur1", "radius")) {
    double radius = v->number;
}
```

### Links

```cpp
bool AddLink(const std::string& linkId,
             const std::string& sourceNodeId, const std::string& sourceSocketId,
             const std::string& targetNodeId, const std::string& targetSocketId);
bool AddLink(const CompositorLink& link);

void RemoveLink(const std::string& linkId);
void RemoveLinksOnSocket(const std::string& nodeId, const std::string& socketId);

CompositorLink* GetLink(const std::string& linkId);
std::vector<std::string> GetAllLinkIds() const;
std::vector<std::string> GetLinksForSocket(const std::string& nodeId,
                                           const std::string& socketId) const;
```

### Styling

```cpp
void SetStyle(const CompositorDiagramStyle& style);
const CompositorDiagramStyle& GetStyle() const;
void SetBackgroundColor(const Color& color);
void SetGridVisible(bool visible, double spacing = 25.0f);
```

### Selection

```cpp
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
```

`Clear()` removes all nodes and links but keeps the registered templates, the
socket type registry and the undo history; call `ClearHistory()` as well when
replacing the whole graph.

### Viewport

```cpp
void SetZoomLevel(double zoom);
void SetPanOffset(double x, double y);
double GetZoomLevel() const;
Point2Dd GetPanOffset() const;

void ZoomIn(double factor = 1.2f);
void ZoomOut(double factor = 1.2f);
void FitView(double padding = 40.0f);
void CenterOn(double worldX, double worldY);

void SetMinZoom(double minZ);
void SetMaxZoom(double maxZ);
```

### Interaction settings

```cpp
void SetInteractive(bool interactive);
bool IsInteractive() const;
void SetNodesConnectable(bool connectable);
void SetPanOnDrag(bool pan);
void SetZoomOnScroll(bool zoom);
void SetEdgeReconnectionEnabled(bool enabled);
bool IsEdgeReconnectionEnabled() const;
```

`SetInteractive(false)` locks the canvas against editing (middle-button pan and
the controls panel still work); the controls panel's lock button toggles it.

### Subgraphs and groups

```cpp
bool SetNodeParent(const std::string& nodeId, const std::string& newParentId);
std::string GetNodeParent(const std::string& nodeId) const;
std::vector<std::string> GetChildrenOf(const std::string& nodeId) const;
std::vector<std::string> GetRootNodeIds() const;
Point2Df GetNodeWorldPos(const std::string& nodeId) const;

std::string Group(const std::string& groupId,
                  const std::string& groupTemplateId,
                  const std::vector<std::string>& nodeIds);
void Ungroup(const std::string& groupId);
void RegisterDefaultGroupTemplate(const std::string& templateId = "group",
                                  const std::string& title = "Group");
```

- `SetNodeParent(id, "")` makes the node a root. It returns `false` for a
  change that would create a cycle, and fires `onNodeParentChanged`.
- `Group` creates a group node around the given nodes (header above them,
  `groupPadding` around) and reparents them, as **one** undo step. An empty
  `groupId` generates one. It returns the group's id, or `""` when the
  template is unknown, is not a group template, the list is empty or the id
  is taken.
- `Ungroup` promotes the group's children to the group's own parent (keeping
  their world positions and links) and removes the group node, as one undo
  step.
- `RegisterDefaultGroupTemplate` registers a group template in the
  "Layout" category with a grey header and a translucent body.

### Node palette search

```cpp
std::vector<std::string> SearchTemplates(const std::string& query) const;
```

Returns template ids, best match first: id starts with the query, then title
starts with it, title contains it, category contains it, id contains it
(case-insensitive). An empty query returns every template id, sorted. The
diagram does not draw a palette; pressing Tab over the canvas fires
`onPaletteRequested(worldX, worldY)` and the host shows its own popup,
filtering with `SearchTemplates` and adding the chosen node with
`AddNode(id, templateId, worldX, worldY)`.

## Editing Commands and Undo

### Undo and redo

```cpp
void Undo();
void Redo();
bool CanUndo() const;
bool CanRedo() const;
void ClearHistory();
void SetHistoryLimit(size_t limit);          // default 100 entries
void SetHistoryRecording(bool enabled);
size_t GetUndoStackSize() const;
size_t GetRedoStackSize() const;
```

The public mutators (`AddNode`, `RemoveNode`, `UpdateNodePosition`,
`AddLink`, `RemoveLink`, `SetParam*`, `SetNodeParent`, `Group`, `Ungroup`,
`Paste`, node drags, ...) record history automatically. Removing a node
records its links too, so an undo restores them. `onHistoryChange` fires after
every push, undo, redo and clear.

To build a graph in code without filling the history - the pattern every
scenario of the demo uses:

```cpp
diagram->Clear();
diagram->ClearHistory();
diagram->SetHistoryRecording(false);
// ... RegisterTemplate / AddNode / AddLink / SetParam* ...
diagram->SetHistoryRecording(true);
diagram->FitView(40.0);
```

`FromJson` does this itself.

### Clipboard

```cpp
void Copy();
void Cut();
void Paste(double offsetX = 20.0, double offsetY = 20.0);
void Duplicate(double offsetX = 20.0, double offsetY = 20.0);
bool HasClipboard() const;
```

`Copy` stores the selected nodes and the links **between** them in an internal
JSON buffer (not the OS clipboard). `Paste` gives the pasted nodes fresh ids,
offsets them, remaps parent ids inside the pasted set and makes a child whose
parent was not copied a root. `Duplicate` is copy + paste in one step.

### Keyboard shortcuts

Handled while the diagram has focus:

| Key | Action |
|-----|--------|
| `Ctrl+Z` | Undo |
| `Ctrl+Y`, `Ctrl+Shift+Z` | Redo |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Copy / Cut / Paste |
| `Ctrl+D` | Duplicate |
| `Ctrl+A` | Select all |
| `Delete`, `Backspace` | Delete the selection |
| `Escape` | Cancel a connect / reconnect drag (a reconnect restores the link) or a rubber-band selection; otherwise deselect |
| `Tab` | Fires `onPaletteRequested` at the pointer (when set) |

### Mouse interaction

| Gesture | Action |
|---------|--------|
| Wheel | Zoom at the pointer (when `SetZoomOnScroll(true)`, the default) |
| Drag on empty canvas | Pan (when `SetPanOnDrag(true)`, the default) |
| Middle-button drag | Pan, even when the canvas is locked |
| Shift+drag on empty canvas | Rubber-band selection (extends the selection) |
| Click / Shift+click a node or link | Select / add to the selection |
| Drag a node | Move the selection (snap and alignment guides apply) |
| Drag from a socket | Connect; drop on a compatible socket |
| Press on a connected single-connection socket | Reconnect that link |
| Click a row widget | See [Parameter rows and embedded widgets](#parameter-rows-and-embedded-widgets) |
| Right-click | `onNodeRightClick` / `onLinkRightClick` / `onSocketRightClick` / `onCanvasRightClick` |

Right-clicking a socket disconnects all its links unless `onSocketRightClick`
is set, in which case only the callback runs. The diagram shows no context
menus of its own.

## Serialization (Save / Load)

```cpp
std::string ToJson() const;
bool FromJson(const std::string& json);
```

`ToJson` writes the viewport (zoom, pan), the registered templates, the nodes
(position, width, collapsed state, title override, parent id, parameter
values, preview handle) and the links (endpoints, style, width, label).
Numbers are written with the classic locale, whatever the host's locale.

`FromJson` clears the diagram and reads the viewport, nodes and links back,
with history recording off, then clears the undo history. What it does **not**
restore (version 0.4.0):

- **Templates.** They are application behaviour (and `customPreviewRenderer`
  cannot be serialised). Register the templates before calling `FromJson`;
  nodes whose template is unknown are skipped.
- **The socket type registry** - register custom types again.
- **`parentId`** of nodes and the **`label`** / colours of links are written
  but not read back, so a saved subgraph loads with its children as root
  nodes (at their relative coordinates). `Copy` / `Paste` do keep parent ids.

### Saving and loading through FileLoader

The file dialogs and the read go through `UltraCanvasFileLoader`, and the
path is UTF-8 throughout. FileLoader has no writer for this JSON, so the text
is written with `OpenFileUtf8`:

```cpp
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasPathUtf8.h"

void SaveGraph(std::shared_ptr<UltraCanvasCompositorDiagram> diagram) {
    FileDialogOptions opts;
    opts.SetTitle("Save graph")
        .AddFilter("Compositor graph (JSON)", std::vector<std::string>{ "json" })
        .SetDefaultFileName("graph.json")
        .SetDefaultExtension("json");
    UltraCanvasFileLoader::SaveFileDialog(opts, [diagram](DialogResult r, const std::string& path) {
        if (r != DialogResult::OK || path.empty()) return;
        const std::string json = diagram->ToJson();
        if (std::FILE* f = OpenFileUtf8(path, "wb")) {
            std::fwrite(json.data(), 1, json.size(), f);
            std::fclose(f);
        }
    });
}

void LoadGraph(std::shared_ptr<UltraCanvasCompositorDiagram> diagram) {
    FileDialogOptions opts;
    opts.SetTitle("Open graph")
        .AddFilter("Compositor graph (JSON)", std::vector<std::string>{ "json" });
    UltraCanvasFileLoader::OpenFileDialog(opts, [diagram](DialogResult r, const std::string& path) {
        if (r != DialogResult::OK || path.empty()) return;
        auto file = UltraCanvasFileLoader::LoadFile(path);
        if (!file.success) return;
        // The templates must already be registered.
        diagram->FromJson(std::string(file.bytes.begin(), file.bytes.end()));
        diagram->FitView(40.0);
    });
}
```

## View Features

### Minimap

```cpp
void SetMinimapVisible(bool visible);
void SetMinimapPosition(NodeDiagramPanelPosition pos);
void SetMinimapConfig(const CompositorMinimapConfig& cfg);
const CompositorMinimapConfig& GetMinimapConfig() const;
```

A small overview of all nodes with the visible area marked; click or drag in it
to pan (when `pannable`). Off by default, bottom-right.

```cpp
diagram->SetMinimapVisible(true);
diagram->SetMinimapPosition(NodeDiagramPanelPosition::TopRight);

CompositorMinimapConfig mm = diagram->GetMinimapConfig();
mm.width = 220.0;
mm.height = 150.0;
diagram->SetMinimapConfig(mm);
```

There is no `IsMinimapVisible`; read `GetMinimapConfig().visible` (the demo's
Minimap toggle does).

### Controls overlay

```cpp
void SetControlsVisible(bool visible);
void SetControlsPosition(NodeDiagramPanelPosition pos);
void SetControlsConfig(const CompositorControlsConfig& cfg);
const CompositorControlsConfig& GetControlsConfig() const;
```

Zoom in, zoom out, fit view and a lock button (which toggles
`SetInteractive`). Off by default, bottom-left; `showZoom`, `showFit` and
`showLock` in the config choose the buttons. The panel stays clickable while
the canvas is locked.

### Snap to grid

```cpp
void SetSnapToGrid(bool enabled);
void SetSnapGrid(double snapX, double snapY);     // default 20 x 20
bool IsSnapToGridEnabled() const;
```

Applies while nodes are dragged, so a multi-node drag stays aligned.

### Alignment guides

```cpp
void SetAlignmentGuidesEnabled(bool enabled);     // default false
bool IsAlignmentGuidesEnabled() const;
void SetAlignmentGuideThreshold(double px);       // default 4.0
```

While a node is dragged, lines appear when its edges or centre line up with
another node's within the threshold, and the node snaps to that alignment
(after grid snapping).

### Fit view and zoom

`FitView(padding)` zooms and pans so every node is visible with `padding`
world units around them; the demo calls it after building each scene and from
its Fit View button. `CenterOn(worldX, worldY)` pans without zooming.
`SetMinZoom` / `SetMaxZoom` bound the zoom.

## Callbacks

```cpp
std::function<void(const std::string& nodeId)>                       onNodeClick;
std::function<void(const std::string& nodeId)>                       onNodeDoubleClick;
std::function<void(const std::string& nodeId, double x, double y)>   onNodeDrag;
std::function<void(const std::string& linkId)>                       onLinkClick;
std::function<void(const CompositorLink&)>                           onLinkCreated;
std::function<void(const std::string& linkId)>                       onLinkRemoved;
std::function<void(const std::string& nodeId,
                   const std::string& paramId,
                   const ParamValue& newValue)> onParamChange;
std::function<void(const std::vector<std::string>&,
                   const std::vector<std::string>&)> onSelectionChange;
std::function<void(double worldX, double worldY)> onCanvasRightClick;
std::function<void(const std::string& nodeId,
                   double worldX, double worldY)> onNodeRightClick;
std::function<void(const std::string& linkId,
                   double worldX, double worldY)> onLinkRightClick;
std::function<void(const std::string& nodeId, const std::string& socketId,
                   bool isOutput,
                   double worldX, double worldY)> onSocketRightClick;
std::function<void(const CompositorSocketSpec& src,
                   const CompositorSocketSpec& dst)> onConnectionRejected;
std::function<void()> onHistoryChange;
std::function<void(double worldX, double worldY)> onPaletteRequested;
std::function<void(const std::string& nodeId,
                   const std::string& oldParentId,
                   const std::string& newParentId)> onNodeParentChanged;
```

| Callback | Fires when |
|----------|-----------|
| `onNodeClick` | A node is clicked |
| `onNodeDoubleClick` | Declared, but not fired by version 0.4.0 |
| `onNodeDrag` | Per node, on every move during a drag (`x`, `y` as stored on the node) |
| `onLinkClick` | A link is clicked |
| `onLinkCreated` / `onLinkRemoved` | A link is added or removed, by code or by the user (also for links removed with their node) |
| `onParamChange` | A parameter value is committed (`SetParam*` or a widget click); also a click on a popup-style widget, so the host can open its editor |
| `onSelectionChange` | The selected nodes / links change: `(nodeIds, linkIds)` |
| `on...RightClick` | Right-click on the canvas, a node, a link or a socket, with the world position for a host popup |
| `onConnectionRejected` | A drag-to-connect ends on an incompatible socket |
| `onHistoryChange` | The undo / redo stacks change |
| `onPaletteRequested` | Tab is pressed over the canvas |
| `onNodeParentChanged` | A node enters, leaves or moves between groups (empty id = root) |

The demo reports selection, history, rejected connections and palette requests
in its status line:

```cpp
std::weak_ptr<UltraCanvasLabel> sl = statusLabel;
diagram->onHistoryChange = [sl, d = diagram.get()]() {
    if (auto s = sl.lock()) {
        s->SetText("History: undo " + std::to_string(d->GetUndoStackSize()) +
                   " | redo " + std::to_string(d->GetRedoStackSize()));
    }
};
diagram->onConnectionRejected = [sl](const CompositorSocketSpec& src,
                                     const CompositorSocketSpec& dst) {
    if (auto s = sl.lock()) {
        s->SetText("Connection rejected: socket types incompatible (" +
                   src.label + " -> " + dst.label + ")");
    }
};
diagram->onPaletteRequested = [sl, d = diagram.get()](double wx, double wy) {
    if (auto s = sl.lock()) {
        auto ids = d->SearchTemplates("");
        s->SetText(std::to_string(ids.size()) + " templates available");
    }
};
```

## Usage Examples

The demo page has four tabs, each with its own diagram, and a toolbar that acts
on the diagram of the current tab. The examples below follow them.

### Recreate Images - Layer Compositor

The first tab rebuilds seven reference images, chosen by a row of buttons:
Layer Compositor, Brush Editor, Music KPI, Portfolio Tree, ORM Modeler, Logic
Diagram and Marketing Funnel. The Layer Compositor is a card with six outputs,
parameter rows that also take links, plain inputs and a preview slot:

```cpp
CompositorNodeTemplate layer;
layer.id = "layer";
layer.category = "Compositor";
layer.title = "Layer";
layer.headerColor = Color(126, 86, 56, 255);
layer.bodyColor   = Color(45, 45, 50, 245);
layer.cornerRadius = 6.0;
layer.defaultWidth = 220.0;
layer.hasPreview = true;
layer.previewHeight = 140.0;

auto addOut = [&](const std::string& id, const std::string& label, SocketDataType t) {
    CompositorSocketSpec s; s.id = id; s.label = label; s.type = t;
    layer.outputs.push_back(s);
};
addOut("out",        "Output",          SocketDataType::Image);
addOut("out_raw",    "Raw",             SocketDataType::Image);
addOut("canvas_pos", "Canvas Position", SocketDataType::Vector);

auto addParam = [&](const std::string& id, const std::string& label,
                    SocketDataType inT, ParamWidgetKind w, ParamValueKind k) {
    CompositorParamSpec p; p.id = id; p.label = label;
    p.hasInputSocket = true; p.inputType = inT;
    p.widget = w; p.valueKind = k;
    layer.params.push_back(p);
};
addParam("opacity", "Opacity", SocketDataType::Scalar, ParamWidgetKind::NumberInput, ParamValueKind::Number);
layer.params.back().defaultNumber = 1.0;
addParam("visible", "Is visible", SocketDataType::Boolean, ParamWidgetKind::Checkbox, ParamValueKind::Boolean);
layer.params.back().defaultBool = true;
addParam("blend", "Blend mode", SocketDataType::Color, ParamWidgetKind::Dropdown, ParamValueKind::Unset);
layer.params.back().choices = {"Normal", "Multiply", "Screen", "Overlay"};

CompositorSocketSpec bg;
bg.id = "bg"; bg.label = "Background"; bg.type = SocketDataType::Image;
bg.maxConnections = 1;
layer.inputs.push_back(bg);
diagram->RegisterTemplate(layer);

CompositorNodeTemplate out;
out.id = "output";
out.title = "Output";
out.headerColor = Color(60, 140, 80, 255);
out.borderColor = Color(60, 200, 80, 255);
out.borderWidth = 2.0;
out.hasPreview = true;
out.inputs.push_back(bg);
diagram->RegisterTemplate(out);

diagram->AddNode("layer1", "layer", 40, 20);
diagram->AddNode("layer2", "layer", 320, 50);
diagram->AddNode("out1", "output", 600, 70);
diagram->AddLink("L1", "layer1", "out", "layer2", "bg");
diagram->AddLink("L2", "layer2", "out", "out1", "bg");
```

#### Custom preview renderer

The Marketing Funnel scenario draws a donut chart in each card's preview slot
from values kept on the node:

```cpp
CompositorNodeTemplate card;
card.id = "stage";
card.title = "Execution";
card.category = "Funnel";
card.headerColor = Color(76, 175, 80, 255);
card.bodyColor = Color(255, 255, 255, 255);
card.hasPreview = true;
card.previewHeight = 130.0;
{
    CompositorParamSpec pct;            // a value the preview reads
    pct.id = "pct"; pct.label = "Percent";
    pct.valueKind = ParamValueKind::Number;
    card.params.push_back(pct);
}
card.customPreviewRenderer = [](IRenderContext* ctx, const Rect2Df& pb,
                                const CompositorNode& node) {
    double pct = 0.0;
    auto it = node.paramValues.find("pct");
    if (it != node.paramValues.end()) pct = it->second.number;

    ctx->SetFillPaint(Color(255, 255, 255, 255));
    ctx->FillRectangle(pb);
    double cx = pb.x + pb.width * 0.5;
    double cy = pb.y + pb.height * 0.5;
    double r = std::min(pb.width, pb.height) * 0.34;
    const double pi = 3.14159265358979;
    ctx->SetStrokeWidth(8.0);
    ctx->SetStrokePaint(Color(225, 228, 232, 255));
    ctx->DrawArc(cx, cy, r, 0.0, 2.0 * pi);
    ctx->SetStrokePaint(Color(120, 100, 220, 255));
    ctx->DrawArc(cx, cy, r, -pi * 0.5, -pi * 0.5 + 2.0 * pi * (pct / 100.0));
};
diagram->RegisterTemplate(card);

diagram->AddNode("exec1", "stage", 40, 40);
diagram->SetParamNumber("exec1", "pct", 72);
```

#### Labelled edges

The Music KPI and Marketing Funnel scenarios put labels on links and override
their colour and routing:

```cpp
CompositorLink l;
l.id = "k1";
l.sourceNodeId = "camp";  l.sourceSocketId = "out";
l.targetNodeId = "trial"; l.targetSocketId = "in";
l.label = "+12%";
l.labelBgColor = Color(80, 180, 130, 230);
l.style = LinkStyle::Step;
l.lineColorOverride = Color(120, 130, 150, 200);
l.lineWidth = 1.5;
diagram->AddLink(l);
```

### Subgraph / Groups

Two groups built with `Group()`, links running between their children and to
a node outside both:

```cpp
diagram->RegisterDefaultGroupTemplate("group", "Group");

CompositorNodeTemplate process;
process.id = "process";
process.title = "Process";
process.headerColor = Color(60, 100, 160, 255);
process.defaultWidth = 160.0;
{
    CompositorSocketSpec in;
    in.id = "in"; in.label = "in"; in.type = SocketDataType::Image; in.maxConnections = 1;
    process.inputs.push_back(in);
    CompositorSocketSpec out;
    out.id = "out"; out.label = "out"; out.type = SocketDataType::Image;
    process.outputs.push_back(out);
}
diagram->RegisterTemplate(process);

diagram->AddNode("src",  "process", 100, 100);
diagram->AddNode("blur", "process", 320, 100);
diagram->AddNode("tone", "process", 540, 100);
diagram->AddNode("sink", "process", 1000, 300);
diagram->AddLink("g1", "src",  "out", "blur", "in");
diagram->AddLink("g2", "blur", "out", "tone", "in");

diagram->Group("preprocess", "group", {"src", "blur", "tone"});
diagram->SetNodeTitleOverride("preprocess", "Preprocess");

diagram->AddNode("scale",  "process", 100, 450);
diagram->AddNode("output", "process", 320, 450);
diagram->AddLink("g3", "scale", "out", "output", "in");
diagram->Group("postprocess", "group", {"scale", "output"});
diagram->SetNodeTitleOverride("postprocess", "Postprocess");

diagram->AddLink("g4", "tone",   "out", "scale", "in");   // group to group
diagram->AddLink("g5", "output", "out", "sink",  "in");   // group to root node

// Children report positions relative to the group:
Point2Df world = diagram->GetNodeWorldPos("blur");
std::vector<std::string> members = diagram->GetChildrenOf("preprocess");
```

Reparenting on drop is up to the host. A sketch that moves a dragged root node
into the group under it:

```cpp
diagram->onNodeDrag = [d = diagram.get()](const std::string& nodeId, double x, double y) {
    if (!d->GetNodeParent(nodeId).empty()) return;
    Point2Df p = d->GetNodeWorldPos(nodeId);
    for (const auto& id : d->GetRootNodeIds()) {
        const CompositorNode* g = d->GetNode(id);
        const CompositorNodeTemplate* t = g ? d->GetTemplate(g->templateId) : nullptr;
        if (!t || !t->isGroup || id == nodeId) continue;
        Point2Df gp = d->GetNodeWorldPos(id);
        if (p.x > gp.x && p.y > gp.y && p.x < gp.x + t->groupMinWidth && p.y < gp.y + t->groupMinHeight) {
            d->SetNodeParent(nodeId, id);
            break;
        }
    }
};
```

### Visual Scripting

`Trigger` sockets act as execution pins; a Branch node takes its condition
through a parameter-row socket and fans out on two exec outputs:

```cpp
auto exec = [](const std::string& id, const std::string& label, int maxConn) {
    CompositorSocketSpec s;
    s.id = id; s.label = label; s.type = SocketDataType::Trigger;
    s.maxConnections = maxConn;
    return s;
};

CompositorNodeTemplate onStart;
onStart.id = "on_start"; onStart.title = "OnStart"; onStart.category = "Scripting";
onStart.headerColor = Color(120, 50, 180, 255);
onStart.outputs.push_back(exec("exec", "exec", 0));
diagram->RegisterTemplate(onStart);

CompositorNodeTemplate branch;
branch.id = "branch"; branch.title = "Branch"; branch.category = "Scripting";
branch.headerColor = Color(160, 80, 60, 255);
branch.inputs.push_back(exec("exec_in", "exec", 1));
{
    CompositorParamSpec cond;
    cond.id = "cond"; cond.label = "Condition";
    cond.hasInputSocket = true; cond.inputType = SocketDataType::Boolean;
    cond.widget = ParamWidgetKind::Checkbox; cond.valueKind = ParamValueKind::Boolean;
    branch.params.push_back(cond);
}
branch.outputs.push_back(exec("true", "True", 0));
branch.outputs.push_back(exec("false", "False", 0));
diagram->RegisterTemplate(branch);

CompositorNodeTemplate compare;
compare.id = "compare"; compare.title = "Compare"; compare.category = "Scripting";
{
    CompositorSocketSpec a;
    a.id = "a"; a.label = "A"; a.type = SocketDataType::Scalar; a.maxConnections = 1;
    compare.inputs.push_back(a);
    a.id = "b"; a.label = "B";
    compare.inputs.push_back(a);

    CompositorParamSpec op;
    op.id = "op"; op.label = "Op";
    op.widget = ParamWidgetKind::Dropdown;
    op.choices = {">", ">=", "==", "<=", "<", "!="};
    compare.params.push_back(op);

    CompositorSocketSpec result;
    result.id = "result"; result.label = "result"; result.type = SocketDataType::Boolean;
    compare.outputs.push_back(result);
}
diagram->RegisterTemplate(compare);

diagram->AddNode("start",   "on_start", 60, 220);
diagram->AddNode("cmp",     "compare",  340, 220);
diagram->AddNode("branch1", "branch",   620, 220);
diagram->AddLink("v3", "cmp",   "result", "branch1", "cond");     // into a param-row socket
diagram->AddLink("v4", "start", "exec",   "branch1", "exec_in");

// An application evaluates the script itself, e.g. when values change:
diagram->onParamChange = [](const std::string& nodeId, const std::string& paramId,
                            const ParamValue& v) {
    if (paramId == "op") { int op = v.dropdownIndex; }
};
```

The demo adds Scalar source nodes (a `Slider` row) feeding A and B, and two
Print nodes on the True / False outputs whose `TextInput` message is set with
`SetParamText`.

### Feature Playground

The fourth tab starts with a small Source → Blur → Output chain, registers the
default group template and turns every view feature on:

```cpp
diagram->SetMinimapVisible(true);
diagram->SetControlsVisible(true);
diagram->SetSnapToGrid(true);
diagram->SetAlignmentGuidesEnabled(true);
```

#### The editing toolbar

The demo's button bar (under the tabs) calls the commands on the diagram of the
current tab. Its buttons map one-to-one onto the API:

| Button | Call |
|--------|------|
| Undo / Redo | `Undo()` / `Redo()` |
| Cut / Copy / Paste / Duplicate | `Cut()` / `Copy()` / `Paste()` / `Duplicate()` |
| Group | `Group("", "group", GetSelectedNodeIds())`, registering the default group template first if the tab has none |
| Ungroup | `Ungroup(id)` for each selected node |
| Delete | `DeleteSelected()` |
| Fit View | `FitView(40.0)` |
| Minimap / Controls | `SetMinimapVisible(!GetMinimapConfig().visible)` / `SetControlsVisible(!GetControlsConfig().visible)` |
| Snap | `SetSnapToGrid(!IsSnapToGridEnabled())` |
| Align Lines | `SetAlignmentGuidesEnabled(!IsAlignmentGuidesEnabled())` |

The current demo source has no Save / Load buttons; add them with the
functions of
[Saving and loading through FileLoader](#saving-and-loading-through-fileloader).
A trimmed version of the bar:

```cpp
auto addBtn = [&](const std::string& id, const std::string& text,
                  std::function<void()> click) {
    auto b = std::make_shared<UltraCanvasButton>(id, 0, 0, 80, 32);
    b->SetText(text);
    b->onClick = std::move(click);
    buttonBar->AddChild(b);
};
addBtn("bUndo", "Undo", [diagram]() { diagram->Undo(); });
addBtn("bRedo", "Redo", [diagram]() { diagram->Redo(); });
addBtn("bDup", "Duplicate", [diagram]() { diagram->Duplicate(); });
addBtn("bGroup", "Group", [diagram]() {
    auto sel = diagram->GetSelectedNodeIds();
    if (sel.empty()) return;
    if (!diagram->GetTemplate("group")) diagram->RegisterDefaultGroupTemplate();
    diagram->Group("", "group", sel);
});
addBtn("bUngroup", "Ungroup", [diagram]() {
    for (const auto& id : diagram->GetSelectedNodeIds()) {
        const CompositorNode* n = diagram->GetNode(id);
        const CompositorNodeTemplate* t = n ? diagram->GetTemplate(n->templateId) : nullptr;
        if (t && t->isGroup) diagram->Ungroup(id);
    }
});
addBtn("bFit", "Fit View", [diagram]() { diagram->FitView(40.0); });
addBtn("tSnap", "Snap", [diagram]() {
    diagram->SetSnapToGrid(!diagram->IsSnapToGridEnabled());
});
addBtn("bSave", "Save", [diagram]() { SaveGraph(diagram); });
addBtn("bLoad", "Load", [diagram]() { LoadGraph(diagram); });
```

The Ungroup handler above checks `isGroup` first: `Ungroup` itself does not,
and called on an ordinary node that has no children it removes that node.

## Known Limitations

As of version 0.4.0:

- `FromJson` does not restore templates, `parentId` or link labels / colours
  (see [Serialization (Save / Load)](#serialization-save--load)).
- `onNodeDoubleClick` is declared but never fired.
- `Ungroup(id)` on a node that is not a group and has no children removes the
  node.
- `NumberInput`, `ColorSwatch`, `TextInput` and `FileBrowser` rows have no
  in-place editor; the host edits them from `onParamChange`.
- The clipboard is internal to one diagram instance; it does not use the OS
  clipboard.
- The diagram never evaluates the graph.

## See Also

- [UltraCanvasNodeDiagramExamples](UltraCanvasNodeDiagramExamples.md) - the opaque-shape node graph that shares `LinkStyle` and the viewport
- [UltraCanvasDiagramViewport](UltraCanvasDiagramViewport.md) - shared pan / zoom / minimap / controls / snap grid
- [UltraCanvasUIElements](UltraCanvasUIElements.md) - the UI element catalogue
- [FileLoader](../Modules/FileLoader/README.md) - file dialogs and loading
