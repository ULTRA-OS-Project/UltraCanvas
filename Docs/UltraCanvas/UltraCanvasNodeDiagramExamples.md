# UltraCanvasNodeDiagram Documentation

## Overview

**UltraCanvasNodeDiagram** is a hybrid graph visualization and interactive flow-editor component. It can display a graph with a one-line `AddNode` / `AddLink` API, or be driven through a verbose API with explicit per-node connection handles (input/output ports), drag-to-connect interactions, multi-select, snap-to-grid, JSON save/load, and four selectable per-link routing styles (Straight, Bezier, SmoothStep, Step). Optional minimap and zoom/fit/lock control overlays are built in.

**Namespace:** `UltraCanvas`
**Header:** `include/Plugins/Diagrams/UltraCanvasNodeDiagram.h`
**Base Class:** `UltraCanvasUIElement`
**Version:** 2.2.0

> **Since 2.2.0** the component covers organizational-network work: data-driven
> node sizing (`NodeSizeMode::ByDegree` / `ByValue`), cluster containers
> (`NodeDiagramGroup`) that auto-fit a boundary box around a set of member
> nodes, a group-cohesion force that keeps those clusters together during
> force-directed layout, and a color legend overlay. See
> [Cluster containers, sizing and legend](#cluster-containers-sizing-and-legend).

> **Since 2.1.0** the pan/zoom, snap-grid, minimap and controls machinery lives
> in the shared [`UltraCanvasDiagramViewport`](UltraCanvasDiagramViewport.md).
> The public API here is unchanged (the overlay config types are now aliases of
> the shared ones), and a coordinate bug is fixed: minimap/controls hit-testing
> and zoom-at-cursor were previously offset by the element's position whenever
> the diagram was not placed at `(0, 0)`.

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasNodeDiagram
```

## Header Include

```cpp
#include "Plugins/Diagrams/UltraCanvasNodeDiagram.h"
```

## Features

- **Simple API**: graph-vis style `AddNode(id, label, x, y)` and `AddLink(id, src, tgt)`
- **Verbose API**: build `NodeDiagramNode` / `NodeDiagramLink` with custom handles, shapes, colors, styles
- **Connection handles** (ports) with `HandleType::Source` and `HandleType::Target`
- **Drag-to-connect** from handles, producing links with the diagram's default style
- **Per-link routing**: Straight, Bezier, SmoothStep, Step
- **Multi-selection** via Shift+click and rubber-band selection box
- **Built-in layouts**: ForceDirected, Circular, Hierarchical, Grid
- **Snap-to-grid**, configurable per-axis
- **Minimap** and **controls overlay** (zoom in/out, fit view, lock)
- **JSON serialization**: `ToJson()` / `FromJson()`
- **Themes**: Default, Professional, Colorful, Minimal, Dark
- **Auto-fit on layout**, label measurement (`SuggestNodeSizeForLabel`)
- **Data-driven node sizing** (2.2.0): size by connection degree or by a per-node value
- **Cluster containers** (2.2.0): auto-fitted, titled boundary boxes around node sets
- **Group cohesion** (2.2.0): pulls cluster members together during force-directed layout
- **Color legend overlay** (2.2.0), placeable in any corner

## Data Structures

### Enumerations

```cpp
enum class NodeShape {
    Circle, Square, Rectangle, RoundedSquare, Diamond,
    Hexagon, Triangle, Star, Cloud
};

enum class NodeDiagramLayout {
    Manual, ForceDirected, Circular, Hierarchical, Grid
};

enum class NodeDiagramTheme {
    Default, Professional, Colorful, Minimal, Dark
};

enum class LinkStyle {
    Straight, Bezier, SmoothStep, Step
};

enum class HandlePosition { Top, Right, Bottom, Left };
enum class HandleType     { Source, Target };

// NEW in 2.2.0
enum class NodeSizeMode {
    Fixed,      // node.width / node.height used verbatim (default)
    ByDegree,   // size = baseSize * sqrt(degree), clamped to [minSize, maxSize]
    ByValue     // size = baseSize * sqrt(node.value), clamped the same way
};

enum class NodeDegreeMode {
    Total,      // incoming + outgoing (default)
    Incoming,
    Outgoing
};

// NOTE: no enumerator may be named "None" - X11's Xlib.h defines None as a macro.
enum class GroupLabelPosition {
    TopLeft, TopCenter, TopRight, BottomLeft, BottomCenter, NoLabel
};

// Since 2.1.0 an alias of the shared DiagramPanelPosition
// (see UltraCanvasDiagramViewport.md).
using NodeDiagramPanelPosition = DiagramPanelPosition;

enum class DiagramPanelPosition {
    TopLeft, TopRight, BottomLeft, BottomRight
};
```

### NodeHandle

```cpp
struct NodeHandle {
    std::string    id;
    HandleType     type     = HandleType::Source;
    HandlePosition position = HandlePosition::Right;
    double offsetX = 0.0, offsetY = 0.0;
    double radius  = 5.0;
    Color  color           = Color(85, 85, 85, 255);
    Color  hoverColor      = Color(0, 120, 215, 255);
    Color  connectedColor  = Color(0, 180, 100, 255);
    bool   connectable     = true;
    int    currentConnections = 0;
    bool   isHovered       = false;
};
```

### NodeDiagramNode

```cpp
struct NodeDiagramNode {
    std::string id;
    std::string label;
    NodeShape   shape = NodeShape::Circle;
    double x = 0.0, y = 0.0;
    double width = 40.0, height = 40.0;
    Color  fillColor   = Color(100, 150, 220, 255);
    Color  borderColor = Color( 50,  80, 140, 255);
    double borderWidth = 2.0;
    Color  textColor   = Color(255, 255, 255, 255);
    double fontSize    = 10.0;
    bool   isSelected = false;
    bool   isDragging = false;
    bool   isPinned   = false;
    double vx = 0.0, vy = 0.0;            // Force-directed velocity
    bool   draggable  = true;
    bool   selectable = true;
    bool   deletable  = true;
    std::vector<NodeHandle> handles;
    int    zIndex = 0;
};
```

### NodeDiagramLink

```cpp
struct NodeDiagramLink {
    std::string id;
    std::string sourceNodeId;
    std::string targetNodeId;
    std::string sourceHandleId;
    std::string targetHandleId;
    Color  lineColor = Color(120, 120, 120, 255);
    double lineWidth = 2.0;
    bool   directed  = true;
    std::string label;
    bool   isSelected = false;
    LinkStyle style    = LinkStyle::Straight;
    bool   selectable  = true;
    bool   deletable   = true;
};
```

### NodeDiagramSizing (2.2.0)

```cpp
struct NodeDiagramSizing {
    NodeSizeMode   mode       = NodeSizeMode::Fixed;
    NodeDegreeMode degreeMode = NodeDegreeMode::Total;

    double baseSize = 26.0;   // Size of a degree-1 (or value-1) node
    double minSize  = 18.0;   // Floor - keeps isolated nodes clickable
    double maxSize  = 96.0;   // Ceiling - keeps a mega-hub from eating the canvas

    bool keepAspect = true;   // Scale width and height together
};
```

Both scaling modes use a **square-root transfer**, so the node's *area* — not its
diameter — is proportional to the underlying quantity. That is the perceptually
correct mapping for a filled mark: scaling the diameter linearly would make a
degree-9 hub look nine times more important than a degree-1 leaf instead of three.

`NodeDiagramNode` gained a `value` field (default `1.0`) that drives
`NodeSizeMode::ByValue`, plus `baseWidth` / `baseHeight`, which capture the size a
node was authored with so switching back to `NodeSizeMode::Fixed` restores it exactly.

### NodeDiagramGroup (2.2.0)

A cluster container: a boundary box drawn around a set of member nodes, with an
optional title. Bounds are recomputed from the members every frame, so the box
tracks its contents through dragging and re-layout.

```cpp
struct NodeDiagramGroup {
    std::string id;
    std::string label;
    std::vector<std::string> nodeIds;

    Color  fillColor   = Color(0, 0, 0, 0);          // Transparent by default
    Color  borderColor = Color(170, 170, 170, 255);
    double borderWidth = 1.0;
    bool   dashed      = false;
    double dashLength  = 6.0;
    double dashGap     = 4.0;

    double padding      = 24.0;   // World-space gap between members and the box
    double cornerRadius = 0.0;    // 0 = sharp corners

    GroupLabelPosition labelPosition = GroupLabelPosition::TopLeft;
    Color  labelColor    = Color(70, 70, 70, 255);
    double labelFontSize = 12.0;
    double labelMargin   = 6.0;

    bool visible = true;
};
```

Boxes render **behind the links** so edges crossing between clusters stay readable;
titles render **after the nodes** so a node never covers one. Group titles and
box borders are scaled by the inverse of the zoom, so they hold a constant
on-screen size at any zoom level.

### NodeDiagramLegendConfig (2.2.0)

```cpp
struct NodeDiagramLegendEntry {
    std::string label;
    Color       color = Color(100, 150, 220, 255);
};

struct NodeDiagramLegendConfig {
    bool visible = false;
    std::vector<NodeDiagramLegendEntry> entries;

    DiagramPanelPosition position = DiagramPanelPosition::TopLeft;
    double padding      = 10.0;   // Distance from the element edge
    double innerPadding = 8.0;    // Panel border to content
    double swatchWidth  = 14.0;
    double swatchHeight = 14.0;
    double rowGap       = 5.0;
    double swatchGap    = 7.0;
    double fontSize     = 11.0;

    std::string title;            // Optional heading above the rows
    double titleFontSize = 12.0;
    double titleGap      = 6.0;

    Color textColor        = Color(50, 50, 50, 255);
    Color backgroundColor  = Color(255, 255, 255, 225);
    Color borderColor      = Color(200, 200, 200, 255);
    bool  showBackground   = true;
    bool  showSwatchBorder = true;
    Color swatchBorderColor = Color(120, 120, 120, 180);
};
```

The legend is drawn in **screen space**, like the minimap and controls overlays —
it does not pan or zoom with the diagram. Panel size is measured from the actual
text, so long category names are never clipped.


## Class Reference

### Constructor

```cpp
UltraCanvasNodeDiagram(const std::string& id,
                       int x, int y, int width, int height);
```

### Factory Function

```cpp
std::shared_ptr<UltraCanvasNodeDiagram> CreateNodeDiagram(
        const std::string& id, int x, int y, int w, int h);
```

### Node Management (Simple API)

```cpp
void AddNode(const std::string& id, const std::string& label,
             double x, double y);
void AddNode(const std::string& id, const std::string& label,
             double x, double y, double size);
void AddNode(const std::string& id, const std::string& label,
             double x, double y, double width, double height);
void AddNode(const NodeDiagramNode& node);    // Verbose API

void RemoveNode(const std::string& id);
void UpdateNodePosition(const std::string& id, double x, double y);
void UpdateNodeLabel(const std::string& id, const std::string& label);
void SetNodeColor(const std::string& id, const Color& fill, const Color& border);
void SetNodeShape(const std::string& id, NodeShape shape);
void PinNode(const std::string& id, bool pinned);

NodeDiagramNode*       GetNode(const std::string& id);
std::vector<std::string> GetAllNodeIds() const;
```

### Handle Management

```cpp
void        AddHandle(const std::string& nodeId, const NodeHandle& handle);
void        AddDefaultHandles(const std::string& nodeId);   // left=target, right=source
void        RemoveHandle(const std::string& nodeId, const std::string& handleId);
NodeHandle* GetHandle(const std::string& nodeId, const std::string& handleId);
```

### Link Management

```cpp
void AddLink(const std::string& id, const std::string& sourceId,
             const std::string& targetId);
void AddLink(const std::string& id, const std::string& sourceId,
             const std::string& targetId, const Color& lineColor);
void AddLink(const NodeDiagramLink& link);    // Verbose API

void RemoveLink(const std::string& id);
void RemoveLink(const std::string& sourceId, const std::string& targetId);
void SetLinkColor(const std::string& id, const Color& color);
void SetLinkWidth(const std::string& id, double width);
void SetLinkLabel(const std::string& id, const std::string& label);
void SetLinkStyle(const std::string& id, LinkStyle style);

void      SetDefaultLinkStyle(LinkStyle style);
LinkStyle GetDefaultLinkStyle() const;
```

### Layout

```cpp
void SetLayout(NodeDiagramLayout layout);
void RunLayout();
void RunForceDirectedLayout(int iterations = 100);
void ApplyCircularLayout();
void ApplyGridLayout();
void ApplyHierarchicalLayout(const std::string& rootId);

void SetAutoFitOnLayout(bool autoFit);
```

### Data-driven node sizing (2.2.0)

```cpp
void         SetNodeSizeMode(NodeSizeMode mode);
NodeSizeMode GetNodeSizeMode() const;

void              SetNodeSizing(const NodeDiagramSizing& sizing);
NodeDiagramSizing GetNodeSizing() const;

// Convenience: mode-independent scale range in one call.
void SetNodeSizeRange(double baseSize, double minSize, double maxSize);

// Magnitude behind a node, used by NodeSizeMode::ByValue.
void   SetNodeValue(const std::string& id, double value);
double GetNodeValue(const std::string& id) const;

// Connection count under the current (or an explicit) degree mode. 0 for unknown ids.
int GetNodeDegree(const std::string& id) const;
int GetNodeDegree(const std::string& id, NodeDegreeMode mode) const;

// Force a sizing pass now. Normally unnecessary - sizes are recomputed when
// links or values change - but useful after bulk edits made through GetNode().
void ApplyNodeSizing();
```

Degree counts are cached and invalidated by any node or link mutation, so
bulk-loading a graph costs one rebuild rather than one per `AddLink` call.
`RunLayout()` re-runs sizing before laying out, so the anti-overlap pass
separates nodes by their final sizes.

### Cluster containers (2.2.0)

```cpp
void AddGroup(const NodeDiagramGroup& group);
void AddGroup(const std::string& id, const std::string& label,
              const std::vector<std::string>& nodeIds,
              const Color& borderColor,
              const Color& fillColor = Color(0, 0, 0, 0));
void RemoveGroup(const std::string& id);
void ClearGroups();

void AddNodeToGroup(const std::string& groupId, const std::string& nodeId);
void RemoveNodeFromGroup(const std::string& groupId, const std::string& nodeId);

NodeDiagramGroup*        GetGroup(const std::string& id);
const NodeDiagramGroup*  GetGroup(const std::string& id) const;
std::vector<std::string> GetAllGroupIds() const;

// Id of the first group containing the node, or "" when it belongs to none.
std::string GetNodeGroupId(const std::string& nodeId) const;

// Current world-space box including padding. Empty rect for unknown ids.
Rect2Dd GetGroupBounds(const std::string& id) const;

void SetGroupsVisible(bool visible);
bool AreGroupsVisible() const;

// Per-group centroid attraction used by the force-directed layout.
// 0 disables clustering; 0.05-0.20 gives visually distinct clusters.
void   SetGroupCohesion(double strength);
double GetGroupCohesion() const;
```

`RemoveNode()` drops the node from every group it belonged to, so a box never
reserves space for a node that no longer exists. Group boxes are included in
`ComputeContentBounds()`, so `FitView()` and the minimap account for them.

**Group cohesion matters.** Without it, repulsion scatters a cluster's members
across the canvas and the boxes grow until they overlap into mush — the boxes
only read as clusters if the layout keeps their members together in the first
place.

### Color legend (2.2.0)

```cpp
void SetLegendVisible(bool visible);
void SetLegendPosition(NodeDiagramPanelPosition pos);
void SetLegendConfig(const NodeDiagramLegendConfig& cfg);
NodeDiagramLegendConfig GetLegendConfig() const;

void AddLegendEntry(const std::string& label, const Color& color);
void ClearLegendEntries();

// One row per group, using each group's border color as the swatch.
// Groups with an empty label are skipped.
void BuildLegendFromGroups();
```

### Styling & Theme

```cpp
void SetTheme(NodeDiagramTheme theme);
void SetBackgroundColor(const Color& color);
void SetGridVisible(bool visible, double spacing = 25.0);
void SetFontFamily(const std::string& fontFamily);
void SetFontSize(double size);
void SetLinkDistance(double distance);
```

### Selection

```cpp
void SelectNode(const std::string& id, bool addToSelection = false);
void SelectLink(const std::string& id, bool addToSelection = false);
void SelectAll();
void DeselectAll();
void DeleteSelected();
void Clear();

std::string              GetSelectedNodeId() const;
std::vector<std::string> GetSelectedNodeIds() const;
std::vector<std::string> GetSelectedLinkIds() const;
bool                     IsNodeSelected(const std::string& id) const;
bool                     IsLinkSelected(const std::string& id) const;
```

### Viewport

```cpp
void     SetZoomLevel(double zoom);
double   GetZoomLevel() const;
void     SetPanOffset(double x, double y);
Point2Dd GetPanOffset() const;

void ZoomIn(double factor = 1.2);
void ZoomOut(double factor = 1.2);
void FitView(double padding = 40.0);
void CenterOn(double worldX, double worldY);
```

### Snap-to-Grid & Interaction

```cpp
void SetSnapToGrid(bool enabled);
void SetSnapGrid(double snapX, double snapY);

void SetInteractive(bool interactive);
void SetNodesConnectable(bool connectable);
void SetPanOnDrag(bool pan);
void SetZoomOnScroll(bool zoom);
```

### Minimap & Controls Overlay

```cpp
void SetMinimapVisible(bool visible);
void SetMinimapPosition(NodeDiagramPanelPosition pos);
void SetMinimapConfig(const NodeDiagramMinimapConfig& cfg);

void SetControlsVisible(bool visible);
void SetControlsPosition(NodeDiagramPanelPosition pos);
void SetControlsConfig(const NodeDiagramControlsConfig& cfg);
```

### Label Measurement

```cpp
void MeasureLabel(const std::string& label, double fontSize,
                  int& outWidth, int& outHeight) const;
void SuggestNodeSizeForLabel(const std::string& label, double fontSize,
                              double minSize, double& outWidth,
                              double& outHeight) const;
```

### Serialization

```cpp
std::string ToJson() const;
bool        FromJson(const std::string& json);
```

### Callbacks

```cpp
std::function<void(const std::string&)> onNodeClick;
std::function<void(const std::string&)> onNodeDoubleClick;
std::function<void(const std::string&, double, double)> onNodeDrag;
std::function<void(const std::string&)> onNodeHover;
std::function<void(const std::string&)> onLinkClick;
std::function<void(const NodeDiagramLink&)> onLinkCreated;
std::function<void(const std::vector<std::string>&,
                   const std::vector<std::string>&)> onSelectionChange;
std::function<void(double zoom, double panX, double panY)> onViewportChange;
std::function<void(double worldX, double worldY)> onCanvasRightClick;
```

## Usage Examples

All examples are drawn from `Apps/DemoApp/UltraCanvasNodeDiagramExamples.cpp`.

### Simple API — Friends network with force-directed layout

```cpp
auto diagram = CreateNodeDiagram("nd_friends", x, y, w, h);

diagram->SetTheme(NodeDiagramTheme::Colorful);
diagram->SetGridVisible(true, 25.0);

diagram->AddNode("alice",   "Alice",   200, 200);
diagram->AddNode("bob",     "Bob",     400, 150);
diagram->AddNode("charlie", "Charlie", 600, 250);
diagram->AddNode("diana",   "Diana",   300, 400);
diagram->AddNode("eve",     "Eve",     500, 450);

diagram->AddLink("l1", "alice", "bob");
diagram->AddLink("l2", "alice", "diana");
diagram->AddLink("l3", "bob",   "charlie");
diagram->AddLink("l4", "charlie","eve");
diagram->AddLink("l5", "diana", "eve");
diagram->AddLink("l6", "bob",   "diana");

diagram->SetLayout(NodeDiagramLayout::ForceDirected);
diagram->RunLayout();   // Auto-fits viewport at the end
```

### Verbose API — Workflow editor with handles

```cpp
auto diagram = CreateNodeDiagram("nd_workflow", 0, 0, 800, 600);
diagram->SetTheme(NodeDiagramTheme::Professional);
diagram->SetGridVisible(true, 25.0);
diagram->SetSnapToGrid(true);
diagram->SetSnapGrid(25.0, 25.0);
diagram->SetDefaultLinkStyle(LinkStyle::Bezier);

NodeDiagramNode startNode("start", "Start");
startNode.shape = NodeShape::RoundedSquare;
startNode.x = 100; startNode.y = 250;
startNode.width = 80; startNode.height = 50;
startNode.fillColor   = Color(120, 200, 120, 255);
startNode.borderColor = Color( 60, 120,  60, 255);
diagram->AddNode(startNode);
diagram->AddDefaultHandles("start");  // left=target, right=source

NodeDiagramNode decisionNode("decision", "Valid?");
decisionNode.shape = NodeShape::Diamond;
decisionNode.x = 475; decisionNode.y = 240;
decisionNode.width = 80; decisionNode.height = 80;
decisionNode.fillColor   = Color(255, 200, 100, 255);
decisionNode.borderColor = Color(180, 120,  40, 255);
diagram->AddNode(decisionNode);
diagram->AddDefaultHandles("decision");

NodeDiagramLink l3("l_dec_end", "decision", "end");
l3.sourceHandleId = "source";
l3.targetHandleId = "target";
l3.style     = LinkStyle::Bezier;
l3.label     = "yes";
l3.lineColor = Color(60, 140, 60, 255);
diagram->AddLink(l3);

diagram->SetControlsVisible(true);
diagram->SetMinimapVisible(true);
diagram->SetMinimapPosition(NodeDiagramPanelPosition::BottomRight);

diagram->onLinkCreated = [](const NodeDiagramLink& link) {
    std::cout << "[Workflow] New connection: "
              << link.sourceNodeId << " -> " << link.targetNodeId << std::endl;
};
```

### Link styles showcase

Four pairs of nodes connected with each routing style side-by-side.

```cpp
struct StylePair {
    std::string label;
    LinkStyle   style;
    float       yOffset;
    Color       color;
};

std::vector<StylePair> pairs = {
    { "Straight",   LinkStyle::Straight,   100.0f, Color( 80,  80, 200, 255) },
    { "Bezier",     LinkStyle::Bezier,     220.0f, Color( 80, 200,  80, 255) },
    { "SmoothStep", LinkStyle::SmoothStep, 340.0f, Color(200, 120,  60, 255) },
    { "Step",       LinkStyle::Step,       460.0f, Color(200,  60, 200, 255) }
};

for (const auto& pair : pairs) {
    NodeDiagramNode src(srcId, pair.label + " src");
    src.shape = NodeShape::RoundedSquare;
    src.x = 100; src.y = pair.yOffset;
    src.width = 100; src.height = 50;
    src.fillColor = pair.color;
    diagram->AddNode(src);
    diagram->AddDefaultHandles(srcId);

    NodeDiagramLink link(linkId, srcId, tgtId);
    link.sourceHandleId = "source";
    link.targetHandleId = "target";
    link.style     = pair.style;
    link.lineColor = pair.color;
    link.lineWidth = 2.5;
    diagram->AddLink(link);
}
```

### Multi-select with keyboard

```cpp
diagram->onSelectionChange = [](const std::vector<std::string>& nodeIds,
                                const std::vector<std::string>& linkIds) {
    std::cout << "[MultiSelect] " << nodeIds.size() << " nodes, "
              << linkIds.size() << " links selected" << std::endl;
};
```

Shift+click toggles a node into the selection; dragging in empty space draws a
selection rubber-band; Delete removes the current selection; Ctrl+A selects
all; Escape cancels an in-progress connection.

### JSON round-trip

```cpp
diagram->AddNode("a", "Node A", 150, 150);
diagram->AddNode("b", "Node B", 350, 150);
diagram->SetNodeShape("b", NodeShape::Diamond);
diagram->AddLink("ab", "a", "b");
diagram->SetLinkLabel("ab", "1");

std::string json = diagram->ToJson();

std::ofstream out("/tmp/nodediagram_export.json");
if (out) { out << json; out.close(); }

bool ok = diagram->FromJson(json);
std::cout << "[JSON] Round trip: " << (ok ? "OK" : "FAILED") << std::endl;
```

### Right-click-to-create with auto-sized nodes

```cpp
auto sizeForLabel = [&diagram](const std::string& label) -> std::pair<float, float> {
    double w = 60.0, h = 60.0;
    diagram->SuggestNodeSizeForLabel(label, /*fontSize=*/11.0,
                                      /*minSize=*/60.0, w, h);
    float s = static_cast<float>(std::max(w, h));
    return { s, s };
};

diagram->onCanvasRightClick = [&](float worldX, float worldY) {
    std::string newId = "node_" + std::to_string(++counter);
    std::string defaultLabel = "Node " + std::to_string(counter);
    auto [w, h] = sizeForLabel(defaultLabel);

    NodeDiagramNode n(newId, defaultLabel);
    n.shape  = NodeShape::Circle;
    n.x      = worldX - w * 0.5f;
    n.y      = worldY - h * 0.5f;
    n.width  = w;
    n.height = h;
    diagram->AddNode(n);
};
```

### Hierarchical org chart layout

```cpp
diagram->AddNode("ceo",     "CEO",          0, 0, 100, 50);
diagram->AddNode("cto",     "CTO",          0, 0, 100, 50);
diagram->AddNode("eng_mgr", "Eng Manager",  0, 0, 100, 50);
diagram->AddNode("dev1",    "Developer 1",  0, 0, 100, 50);

diagram->AddLink("e1", "ceo", "cto");
diagram->AddLink("e3", "cto", "eng_mgr");
diagram->AddLink("e5", "eng_mgr", "dev1");

diagram->SetDefaultLinkStyle(LinkStyle::SmoothStep);
diagram->ApplyHierarchicalLayout("ceo");
```

### Cluster containers, sizing and legend

An organizational network: five departments, each wrapped in its own boundary
box, with hub nodes sized by how many people connect to them. This is the shape
a strict org chart cannot draw — the cross-department links are the point.

```cpp
auto diagram = CreateNodeDiagram("org", 0, 0, 900, 620);
diagram->SetTheme(NodeDiagramTheme::Minimal);
diagram->SetDefaultLinkStyle(LinkStyle::Straight);

struct Dept { const char* id; const char* label; Color color; const char* hub; };
const Dept departments[] = {
    {"grp_sales", "Sales",              Color(31,  78, 160, 255), "sales_hub"},
    {"grp_care",  "Customer Care",      Color(38, 125,  60, 255), "care_hub"},
    {"grp_biz",   "Business Solutions", Color(214,106,  20, 255), "biz_hub"},
    {"grp_cli",   "Client Services",    Color(196, 42,  96, 255), "cli_hub"},
};

int linkNo = 0;
for (const auto& dept : departments) {
    NodeDiagramNode hub(dept.hub, dept.label);
    hub.shape       = NodeShape::Square;
    hub.fillColor   = dept.color;
    hub.borderColor = Color(40, 40, 40, 255);
    hub.textColor   = Color(255, 255, 255, 255);
    hub.width = hub.height = 46.0;
    hub.value = 6.0;                     // Headcount - drives NodeSizeMode::ByValue
    diagram->AddNode(hub);

    std::vector<std::string> members{dept.hub};
    for (int i = 0; i < 6; ++i) {
        std::string id = std::string(dept.hub) + "_m" + std::to_string(i);
        NodeDiagramNode node(id, "Member " + std::to_string(i));
        node.shape       = NodeShape::Square;
        node.borderColor = dept.color;
        node.width = node.height = 34.0;
        diagram->AddNode(node);
        members.push_back(id);

        NodeDiagramLink link("l" + std::to_string(++linkNo), dept.hub, id);
        link.directed  = false;
        link.lineColor = Color(dept.color.r, dept.color.g, dept.color.b, 150);
        diagram->AddLink(link);
    }

    NodeDiagramGroup group(dept.id, dept.label);
    group.nodeIds       = members;
    group.borderColor   = dept.color;
    group.fillColor     = Color(dept.color.r, dept.color.g, dept.color.b, 18);
    group.labelColor    = dept.color;
    group.dashed        = true;
    group.cornerRadius  = 6.0;
    group.labelPosition = GroupLabelPosition::TopLeft;
    diagram->AddGroup(group);
}

// Cross-department ties: the reason this is a network, not a tree.
diagram->AddLink(NodeDiagramLink("x1", "sales_hub", "biz_hub"));
diagram->AddLink(NodeDiagramLink("x2", "biz_hub",   "cli_hub"));
diagram->AddLink(NodeDiagramLink("x3", "cli_hub",   "care_hub"));

// Hubs read as hubs: area proportional to connection count.
NodeDiagramSizing sizing;
sizing.mode     = NodeSizeMode::ByDegree;
sizing.baseSize = 20.0;
sizing.minSize  = 26.0;
sizing.maxSize  = 74.0;
diagram->SetNodeSizing(sizing);

// Keep each department's members together while the layout runs.
diagram->SetGroupCohesion(0.14);

diagram->BuildLegendFromGroups();
auto legend = diagram->GetLegendConfig();
legend.visible  = true;
legend.position = DiagramPanelPosition::TopRight;
legend.title    = "Departments";
diagram->SetLegendConfig(legend);

diagram->SetLayout(NodeDiagramLayout::ForceDirected);
diagram->RunLayout();
```

Switching `sizing.mode` to `NodeSizeMode::ByValue` sizes each node from its
`value` field instead — headcount, budget, revenue, whatever the graph is about.
`NodeSizeMode::Fixed` restores the authored sizes exactly.


## Keyboard Shortcuts

| Key       | Action                       |
|-----------|------------------------------|
| `Delete`  | Delete selected nodes/links  |
| `Ctrl+A`  | Select all                   |
| `Escape`  | Cancel in-progress connection |

## See Also

- [UltraCanvasBlockDiagramExamples](UltraCanvasBlockDiagramExamples.md) — flowchart-style block diagrams with isometric option
- [UltraCanvasGourceTreeExamples](UltraCanvasGourceTreeExamples.md) — radial filesystem tree
- [UltraCanvasAdjacencyDiagramExamples](UltraCanvasAdjacencyDiagramExamples.md) — architectural room adjacency
- [UltraCanvasArcDiagramExamples](UltraCanvasArcDiagramExamples.md) — nodes on a baseline with arc edges
- [UltraCanvasUIElement](UltraCanvasUIElement.md) — base element class
- [UltraCanvasTabbedContainer](UltraCanvasTabbedContainer.md) — tab control used by the demos
