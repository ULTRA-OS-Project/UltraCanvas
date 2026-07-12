# UltraCanvasNodeDiagram Documentation

## Overview

**UltraCanvasNodeDiagram** is a hybrid graph visualization and interactive flow-editor component. It can display a graph with a one-line `AddNode` / `AddLink` API, or be driven through a verbose API with explicit per-node connection handles (input/output ports), drag-to-connect interactions, multi-select, snap-to-grid, JSON save/load, and four selectable per-link routing styles (Straight, Bezier, SmoothStep, Step). Optional minimap and zoom/fit/lock control overlays are built in.

**Namespace:** `UltraCanvas`
**Header:** `include/Plugins/Diagrams/UltraCanvasNodeDiagram.h`
**Base Class:** `UltraCanvasUIElement`
**Version:** 2.0.6

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

enum class NodeDiagramPanelPosition {
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
Point2Df GetPanOffset() const;

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
