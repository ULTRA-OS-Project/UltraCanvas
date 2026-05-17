# UltraCanvasFlowChart Documentation

**Version:** 2.2.0
**Author:** UltraCanvas Framework

## Overview

`UltraCanvasFlowChart` is an interactive flow chart / node-diagram editor. It manages a collection of named, shaped `FlowChartNode` instances and `FlowChartConnection` arrows between them, supports multiple edit modes (`Select`, `CreateNode`, `CreateConnection`, `Pan`), grid + snap-to-grid, zoom and pan, themes, and a rich set of mouse/keyboard interactions. Orthogonal connections use obstacle-aware A* routing over the chart's grid. The framework also ships a companion palette element, `UltraCanvasFlowChartPalette`, that lets users pick shapes and click-place them on the diagram.

**Namespace:** `UltraCanvas`
**Header:** `include/Plugins/Diagrams/UltraCanvasFlowChart.h`
**Implementation:** `Plugins/Diagrams/UltraCanvasFlowChart.cpp`
**Companion header:** `include/Plugins/Diagrams/UltraCanvasFlowChartPalette.h`
**Base Class:** `UltraCanvasUIElement`

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasFlowChart
```

## Features

- **Rich shape vocabulary:** Rectangle, RoundedRectangle, Diamond, Oval, Circle, Parallelogram, Hexagon, Triangle, Star, Ellipse, Process, Decision, ManualInput, Document, Database, Cloud, StickyNote, Actor.
- **Connection styles:** `Straight`, `Orthogonal` (A* obstacle-aware), `Curved`.
- **Arrow heads:** `None`, `Arrow`, `ArrowFilled`, `Diamond`.
- **Line styles:** `Solid`, `Dashed`, `Dotted`, `DashDot`.
- **Themes:** `Default`, `Professional`, `Colorful`, `Minimal`, `Dark`.
- **Edit modes:** Select / move / connect / create / pan with built-in mode switching.
- **Grid + snap:** Visible grid with snap-to-grid on node placement and drag.
- **Zoom & pan:** Mouse-driven, programmable.
- **Shape palette:** `UltraCanvasFlowChartPalette` widget for click-to-place authoring.
- **Callbacks:** Node click, double-click, drag, selection, connection click & creation, edit-mode change.

## Header Includes

```cpp
#include "Plugins/Diagrams/UltraCanvasFlowChart.h"
#include "Plugins/Diagrams/UltraCanvasFlowChartPalette.h" // optional, for the shape picker
```

## Class Reference

### Constructor

```cpp
UltraCanvasFlowChart(const std::string& id, int x, int y, int width, int height);
```

### Node Management

```cpp
void AddNode(const std::string& id, FlowChartShape shape,
             const std::string& label, double x, double y);
void AddNode(const std::string& id, FlowChartShape shape,
             const std::string& label,
             double x, double y, double width, double height);
void RemoveNode(const std::string& id);
void UpdateNodePosition(const std::string& id, double x, double y);
void SetNodeSize(const std::string& id, double width, double height);
void UpdateNodeLabel(const std::string& id, const std::string& label);
void SetNodeColor(const std::string& id, const Color& fillColor,
                  const Color& borderColor);
void SetNodeTextColor(const std::string& id, const Color& color);
void SetNodeFontSize(const std::string& id, double size);
void SetNodeShape(const std::string& id, FlowChartShape shape);
void SetNodeBorderWidth(const std::string& id, double width);

FlowChartNode*       GetNode(const std::string& id);
const FlowChartNode* GetNode(const std::string& id) const;
std::vector<std::string> GetAllNodeIds() const;
```

### Connection Management

```cpp
void AddConnection(const std::string& id, const std::string& sourceId,
                   const std::string& targetId);
void AddConnection(const std::string& id, const std::string& sourceId,
                   const std::string& targetId,
                   FlowChartConnectionStyle style,
                   FlowChartArrowStyle arrowStyle = FlowChartArrowStyle::Arrow);
void RemoveConnection(const std::string& id);
void SetConnectionLabel(const std::string& id, const std::string& label);
void SetConnectionStyle(const std::string& id, FlowChartConnectionStyle style);
void SetConnectionLineStyle(const std::string& id, FlowChartLineStyle style);
void SetConnectionColor(const std::string& id, const Color& color);
void SetConnectionWidth(const std::string& id, double width);

FlowChartConnection* GetConnection(const std::string& id);
std::vector<std::string> GetAllConnectionIds() const;
```

### Styling & Theme

```cpp
void SetTheme(FlowChartTheme theme);
void SetBackgroundColor(const Color& color);
void SetGridVisible(bool visible, double spacing = 20.0f);
void SetGridColor(const Color& color);
void SetSnapToGrid(bool enable);
void SetFontFamily(const std::string& fontFamily);
void SetFontSize(double size);
```

### Interaction

```cpp
void SelectNode(const std::string& id);
void DeselectAll();
void Clear();
void DeleteSelected();

std::string GetSelectedNodeId() const;

void SetZoomLevel(double zoom);
void SetPanOffset(double x, double y);
double  GetZoomLevel() const;
Point2Df GetPanOffset() const;

void SetCreateNodeMode(bool enable);
void SetPendingNodeShape(FlowChartShape shape);
FlowChartShape GetPendingNodeShape() const;
void SetEditMode(EditMode mode);
EditMode GetEditMode() const;
```

### Edit Modes

```cpp
enum class EditMode { Select, CreateNode, CreateConnection, Pan };
```

### Callbacks

```cpp
std::function<void(const std::string&)>                      onNodeClick;
std::function<void(const std::string&)>                      onNodeDoubleClick;
std::function<void(const std::string&, double, double)>      onNodeDragged;
std::function<void(const std::string&)>                      onConnectionClick;
std::function<void(const std::string&)>                      onNodeCreated;
std::function<void(const std::string&)>                      onNodeSelected;
std::function<void(const std::string&, const std::string&)>  onConnectionCreated;
std::function<void(EditMode)>                                onEditModeChanged;
```

### Factory Functions

```cpp
inline std::shared_ptr<UltraCanvasFlowChart> CreateFlowChart(
        const std::string& id, int x, int y, int w, int h);

inline std::shared_ptr<UltraCanvasFlowChartPalette> CreateFlowChartPalette(
        const std::string& id, int x, int y, int width, int height);
```

### Palette Class

```cpp
class UltraCanvasFlowChartPalette : public UltraCanvasContainer {
public:
    UltraCanvasFlowChartPalette(const std::string& id,
                                int x, int y, int width, int height);

    void SetTargetDiagram(std::shared_ptr<UltraCanvasFlowChart> diagram);
    void BuildPalette();

    std::function<void(FlowChartShape)> onShapeSelected;
};
```

## Data Structures

### FlowChartNode

```cpp
struct FlowChartNode {
    std::string id;
    std::string label;
    FlowChartShape shape = FlowChartShape::Rectangle;

    double x = 0.0f, y = 0.0f;
    double width = 120.0f, height = 60.0f;

    Color  fillColor   = Color(240, 240, 245, 255);
    Color  borderColor = Color( 80,  80,  85, 255);
    double borderWidth = 1.5f;

    Color  textColor = Color(30, 30, 35, 255);
    double fontSize  = 12.0f;

    bool isSelected = false;
    bool isDragging = false;
    bool isStart    = false;
    bool isEnd      = false;
    bool isHovered  = false;
};
```

### FlowChartConnection

```cpp
struct FlowChartConnection {
    std::string id;
    std::string sourceNodeId;
    std::string targetNodeId;

    FlowChartConnectionStyle style      = FlowChartConnectionStyle::Orthogonal;
    FlowChartArrowStyle      arrowStyle = FlowChartArrowStyle::Arrow;
    FlowChartLineStyle       lineStyle  = FlowChartLineStyle::Solid;

    Color  lineColor = Color(60, 60, 65, 255);
    double lineWidth = 2.0f;

    std::string label;
    Color labelBackgroundColor = Color(255, 255, 255, 220);
    Color labelTextColor       = Color(  0,   0,   0, 255);

    bool isSelected = false;
};
```

### Enumerations

```cpp
enum class FlowChartShape {
    Rectangle, RoundedRectangle, Diamond, Oval, Circle, Parallelogram,
    Hexagon, Triangle, Star, Ellipse, Process, Decision, ManualInput,
    Document, Database, Cloud, StickyNote, Actor
};

enum class FlowChartConnectionStyle { Straight, Orthogonal, Curved };
enum class FlowChartArrowStyle      { None, Arrow, ArrowFilled, Diamond };
enum class FlowChartLineStyle       { Solid, Dashed, Dotted, DashDot };
enum class FlowChartTheme           { Default, Professional, Colorful, Minimal, Dark };
```

## Usage Examples

The following examples are drawn from `Apps/DemoApp/UltraCanvasFlowChartExamples.cpp`.

### Pre-built Order Processing Chart

```cpp
auto chart = CreateFlowChart("flowChart", 30, 95, 1130, 680);
chart->SetBackgroundColor(Color(255, 255, 255, 255));
chart->SetGridVisible(true, 20.0f);
chart->SetGridColor(Color(235, 235, 235, 255));
chart->SetSnapToGrid(true);

chart->AddNode("start",   FlowChartShape::RoundedRectangle, "Start",
               60,  80, 140, 60);
chart->SetNodeColor("start",
                    Color(200, 255, 200, 255),
                    Color( 50, 180,  50, 255));
chart->SetNodeTextColor("start", Color(0, 60, 0, 255));

chart->AddNode("process1", FlowChartShape::Rectangle, "Process Order",
               280,  80, 160, 60);
chart->AddNode("decision", FlowChartShape::Diamond,   "In Stock?",
               520,  60, 140, 100);
chart->AddNode("process3", FlowChartShape::Rectangle, "Backorder",
               740,  80, 140, 60);
chart->AddNode("process2", FlowChartShape::Rectangle, "Ship Item",
               520, 240, 140, 60);
chart->AddNode("end1",     FlowChartShape::RoundedRectangle, "Complete",
               520, 380, 140, 60);
chart->AddNode("note1",    FlowChartShape::StickyNote, "Check\ninventory\nfirst!",
               760, 240, 100, 80);

chart->AddConnection("c1", "start",    "process1",
                     FlowChartConnectionStyle::Orthogonal,
                     FlowChartArrowStyle::Arrow);

chart->AddConnection("c3", "decision", "process2",
                     FlowChartConnectionStyle::Orthogonal,
                     FlowChartArrowStyle::Arrow);
chart->SetConnectionLabel("c3", "Yes");
chart->SetConnectionColor("c3", Color(50, 180, 50, 255));
chart->SetConnectionWidth("c3", 2.5f);

chart->AddConnection("c5", "decision", "process3",
                     FlowChartConnectionStyle::Orthogonal,
                     FlowChartArrowStyle::Arrow);
chart->SetConnectionLabel("c5", "No");
chart->SetConnectionColor("c5", Color(220, 120, 80, 255));

chart->AddConnection("c6", "process3", "note1",
                     FlowChartConnectionStyle::Straight,
                     FlowChartArrowStyle::None);
chart->SetConnectionLineStyle("c6", FlowChartLineStyle::Dotted);
chart->SetConnectionColor("c6", Color(180, 180, 180, 255));
chart->SetConnectionWidth("c6", 1.5f);
```

### Toolbar — Switching Edit Mode and Zoom

```cpp
auto btnSelect = std::make_shared<UltraCanvasButton>("btnSelect", btnX, btnY, btnW, btnH);
btnSelect->SetText("Select");
btnSelect->SetOnClick([chart]() {
    chart->SetEditMode(UltraCanvasFlowChart::EditMode::Select);
});

auto btnConnect = std::make_shared<UltraCanvasButton>("btnConnect", btnX, btnY, btnW, btnH);
btnConnect->SetText("Connect");
btnConnect->SetOnClick([chart]() {
    chart->SetEditMode(UltraCanvasFlowChart::EditMode::CreateConnection);
});

auto btnZoomIn = std::make_shared<UltraCanvasButton>("btnZoomIn", btnX, btnY, 90, btnH);
btnZoomIn->SetText("Zoom +");
btnZoomIn->SetOnClick([chart]() {
    float z = chart->GetZoomLevel() * 1.2f;
    if (z <= 3.0f) chart->SetZoomLevel(z);
});

auto btnZoomOut = std::make_shared<UltraCanvasButton>("btnZoomOut", btnX, btnY, 90, btnH);
btnZoomOut->SetText("Zoom -");
btnZoomOut->SetOnClick([chart]() {
    float z = chart->GetZoomLevel() * 0.8f;
    if (z >= 0.3f) chart->SetZoomLevel(z);
});

auto btnReset = std::make_shared<UltraCanvasButton>("btnReset", btnX, btnY, 120, btnH);
btnReset->SetText("Reset View");
btnReset->SetOnClick([chart]() {
    chart->SetZoomLevel(1.0f);
    chart->SetPanOffset(0, 0);
});
```

### Grid + Snap-to-Grid Checkboxes

```cpp
auto chkGrid = std::make_shared<UltraCanvasCheckbox>("chkGrid", 30, 850, 130, 24);
chkGrid->SetText("Show Grid");
chkGrid->SetChecked(true);
chkGrid->onStateChanged = [chart](CheckedState, CheckedState newState) {
    chart->SetGridVisible(newState == CheckedState::Checked, 20.0f);
};

auto chkSnap = std::make_shared<UltraCanvasCheckbox>("chkSnap", 175, 850, 150, 24);
chkSnap->SetText("Snap to Grid");
chkSnap->SetChecked(true);
chkSnap->onStateChanged = [chart](CheckedState, CheckedState newState) {
    chart->SetSnapToGrid(newState == CheckedState::Checked);
};
```

### Shape Palette — Click to Place

```cpp
auto chart = CreateFlowChart("builderChart", 210, 75, 870, 640);
chart->SetGridVisible(true, 20.0f);
chart->SetSnapToGrid(true);

auto palette = CreateFlowChartPalette("builderPalette", 20, 75, 180, 640);
palette->SetTargetDiagram(chart);
palette->BuildPalette();

container->AddChild(chart);
container->AddChild(palette);
```

### Live Editing of the Selected Node

When the chart emits `onNodeSelected`, the demo populates a property editor and writes changes back to the chart:

```cpp
chart->onNodeSelected = [selectedNodeId, loadNodeIntoEditor]
                       (const std::string& id) {
    *selectedNodeId = id;
    loadNodeIntoEditor(id);
};

ddShape->onSelectionChanged = [chart, selectedNodeId]
                              (int idx, const DropdownItem&) {
    if (selectedNodeId->empty()) return;
    chart->SetNodeShape(*selectedNodeId, kShapeOptions[idx].shape);
};

inLabel->onTextChanged = [chart, selectedNodeId](const std::string& s) {
    if (selectedNodeId->empty()) return;
    chart->UpdateNodeLabel(*selectedNodeId, s);
};

auto applyNodeSize = [chart, selectedNodeId, inW, inH]() {
    if (selectedNodeId->empty()) return;
    int w = std::stoi(inW->GetText());
    int h = std::stoi(inH->GetText());
    chart->SetNodeSize(*selectedNodeId,
                       static_cast<float>(w),
                       static_cast<float>(h));
};
inW->onFocusLost = applyNodeSize;
inH->onFocusLost = applyNodeSize;
```

### Live Editing of the Selected Connection

```cpp
chart->onConnectionClick = [selectedConnId, loadConnIntoEditor]
                          (const std::string& id) {
    *selectedConnId = id;
    loadConnIntoEditor(id);
};

inCLabel->onTextChanged = [chart, selectedConnId](const std::string& s) {
    if (selectedConnId->empty()) return;
    chart->SetConnectionLabel(*selectedConnId, s);
};

ddStyle->onSelectionChanged = [chart, selectedConnId]
                              (int idx, const DropdownItem&) {
    if (selectedConnId->empty()) return;
    chart->SetConnectionStyle(*selectedConnId,
                              static_cast<FlowChartConnectionStyle>(idx));
};

inCWidth->onFocusLost = [chart, selectedConnId, inCWidth]() {
    if (selectedConnId->empty()) return;
    int w = std::stoi(inCWidth->GetText());
    chart->SetConnectionWidth(*selectedConnId, static_cast<float>(w));
};
```

### Adding a Sticky Note at Runtime

```cpp
btnAddNote->SetOnClick([chart]() {
    std::string id = "note_" + std::to_string(std::rand() % 100000);
    chart->AddNode(id, FlowChartShape::StickyNote, "Note",
                   380, 280, 120, 80);
    chart->SetNodeColor(id,
                        Color(255, 255, 200, 255),
                        Color(220, 220, 120, 255));
    chart->SetNodeTextColor(id, Color(80, 80, 0, 255));
    chart->SetNodeFontSize(id, 11.0f);
    chart->SelectNode(id);
});
```

### Delete Selected and Clear All

```cpp
btnDelete->SetOnClick([chart, showNoSelection]() {
    chart->DeleteSelected();
    showNoSelection();
});

btnClear->SetOnClick([chart, showNoSelection]() {
    chart->Clear();
    showNoSelection();
});
```

## Edit Mode Cheat-Sheet

| Mode               | Mouse Behavior                                                        |
|--------------------|-----------------------------------------------------------------------|
| `Select`           | Click to select, drag to move nodes.                                  |
| `CreateNode`       | Click on empty canvas to drop the current `pendingNodeShape`.         |
| `CreateConnection` | Click source node, then click target node to wire them up.            |
| `Pan`              | Drag to pan the canvas.                                               |

## See Also

- [UltraCanvasSankeyExamples](UltraCanvasSankeyExamples.md) — flow diagrams (value-weighted ribbons)
- [UltraCanvasVennDiagramExamples](UltraCanvasVennDiagramExamples.md) — set intersections
- [UltraCanvasTreeMapExamples](UltraCanvasTreeMapExamples.md) — hierarchical area visualization
- [UltraCanvasDendrogramExamples](UltraCanvasDendrogramExamples.md) — hierarchical clustering trees
- [UltraCanvasButtonExamples](UltraCanvasButtonExamples.md) — toolbar buttons used by the demo
- [UltraCanvasTabbedContainer](UltraCanvasTabbedContainer.md) — used to host the demo tabs
- [UltraCanvasUIElement](UltraCanvasUIElement.md) — base class
