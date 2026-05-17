# UltraCanvasBlockDiagram Documentation

## Overview

**UltraCanvasBlockDiagram** is an interactive block diagram component within the UltraCanvas framework, designed for building flowcharts, system diagrams, and technical schematics. Nodes can be drawn as a wide variety of shapes (rectangles, diamonds, hexagons, cylinders, actors, etc.) and connected by labeled, styled lines with optional arrowheads. The component supports an optional 3D isometric rendering mode for HVAC- and engineering-style visuals, an interactive editor mode (select / create-node / create-connection / pan), grid snapping, and pan/zoom.

**Namespace:** `UltraCanvas`
**Header:** `include/Plugins/Diagrams/UltraCanvasBlockDiagram.h`
**Base Class:** `UltraCanvasUIElement`
**Version:** 2.3.1

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasBlockDiagram
```

## Header Include

```cpp
#include "Plugins/Diagrams/UltraCanvasBlockDiagram.h"
```

## Features

### Core Capabilities
- **Many node shapes:** Rectangle, RoundedRectangle, Oval, Circle, Diamond, Process, Decision, Terminal, Data, Document, Hexagon, Octagon, Trapezoid, Parallelogram, Pentagon, Triangle, Star, Cloud, Cylinder, StickyNote, Actor
- **Four connection routings:** Straight, Orthogonal, Curved, Bezier
- **Multiple arrow styles:** NoneStyle, Forward, Backward, Bidirectional, Diamond, Circle
- **Line styles:** Solid, Dashed, Dotted, DashDot
- **Two render styles:** Flat 2D and Isometric3D (with configurable depth/angle)
- **Interactive editing:** Select / CreateNode / CreateConnection / Pan modes
- **View control:** Zoom, pan, grid with optional snap
- **Per-face anchor distribution:** Multiple connections meeting on the same node face are spread evenly along it (no overlap)

## Data Structures

### Enumerations

```cpp
enum class BlockShape {
    Rectangle, RoundedRectangle, Oval, Circle, Diamond,
    Process, Decision, Terminal, Data, Document,
    Hexagon, Octagon, Trapezoid, Parallelogram, Pentagon,
    Triangle, Star, Cloud, Cylinder, StickyNote, Actor
};

enum class ConnectionStyle {
    Straight,    // Direct line
    Orthogonal,  // Right-angle bends
    Curved,      // Smooth curve
    Bezier       // Cubic bezier curve
};

enum class ArrowStyle {
    NoneStyle, Forward, Backward, Bidirectional, Diamond, Circle
};

enum class LineStyle { Solid, Dashed, Dotted, DashDot };
```

### Nested Enumerations on UltraCanvasBlockDiagram

```cpp
enum class EditMode {
    Select,            // Select and move nodes
    CreateNode,        // Click to create node
    CreateConnection,  // Click nodes to connect
    Pan                // Pan the canvas
};

enum class RenderStyle {
    Flat,              // Flat 2D rendering
    Isometric3D        // 3D isometric blocks (HVAC/technical style)
};
```

### BlockNode

```cpp
struct BlockNode {
    std::string id;
    std::string label;
    BlockShape shape = BlockShape::Rectangle;

    float x = 0.0f, y = 0.0f;
    float width = 120.0f, height = 60.0f;

    Color fillColor   = Color(200, 200, 220, 255);
    Color borderColor = Color(80, 80, 100, 255);
    float borderWidth = 2.0f;

    Color textColor = Color(0, 0, 0, 255);
    float fontSize  = 12.0f;

    bool isSelected = false;
    bool isHovered  = false;
};
```

### BlockConnection

```cpp
struct BlockConnection {
    std::string id;
    std::string sourceId;
    std::string targetId;

    ConnectionStyle style     = ConnectionStyle::Straight;
    ArrowStyle      arrowStyle = ArrowStyle::Forward;
    LineStyle       lineStyle = LineStyle::Solid;

    Color color = Color(80, 80, 80, 255);
    float width = 2.0f;

    std::string label;
    Color labelBackgroundColor = Color(255, 255, 255, 220);
    Color labelTextColor       = Color(0, 0, 0, 255);

    bool isSelected = false;
};
```

## Class Reference

### Constructor

```cpp
UltraCanvasBlockDiagram(const std::string& id,
                        int x, int y, int width, int height);
```

### Factory Function

```cpp
std::shared_ptr<UltraCanvasBlockDiagram> CreateBlockDiagram(
        const std::string& id, int x, int y, int width, int height);
```

### Node Management

```cpp
void AddNode(const std::string& id, BlockShape shape,
             const std::string& label, float x, float y);
void AddNode(const std::string& id, BlockShape shape,
             const std::string& label, float x, float y,
             float width, float height);
void RemoveNode(const std::string& id);
BlockNode* GetNode(const std::string& id);
void UpdateNodePosition(const std::string& id, float x, float y);

// Per-node styling
void SetNodeColor(const std::string& id, const Color& fill, const Color& border);
void SetNodeTextColor(const std::string& id, const Color& color);
void SetNodeFontSize(const std::string& id, float size);
void SetNodeShape(const std::string& id, BlockShape shape);
void SetNodeBorderWidth(const std::string& id, float width);
```

### Connection Management

```cpp
void AddConnection(const std::string& id,
                   const std::string& sourceId,
                   const std::string& targetId);
void AddConnection(const std::string& id,
                   const std::string& sourceId,
                   const std::string& targetId,
                   ConnectionStyle style);
void AddConnection(const std::string& id,
                   const std::string& sourceId,
                   const std::string& targetId,
                   ConnectionStyle style,
                   ArrowStyle arrowStyle);
void RemoveConnection(const std::string& id);
BlockConnection* GetConnection(const std::string& id);

// Per-connection styling
void SetConnectionColor(const std::string& id, const Color& color);
void SetConnectionWidth(const std::string& id, float width);
void SetConnectionLabel(const std::string& id, const std::string& label);
void SetConnectionStyle(const std::string& id, ConnectionStyle style);
void SetConnectionLineStyle(const std::string& id, LineStyle lineStyle);
```

### View Control

```cpp
void   SetZoomLevel(float zoom);
double GetZoomLevel() const;
void   SetPanOffset(float x, float y);
```

### Grid & Background

```cpp
void SetBackgroundColor(const Color& color);
void SetGridVisible(bool visible, float spacing = 25.0f);
void SetGridColor(const Color& color);
void SetSnapToGrid(bool enable);
```

### 3D Isometric Rendering

```cpp
void        SetRenderStyle(RenderStyle style);
RenderStyle GetRenderStyle() const;
void        SetIsometricDepth(float depth);
double      GetIsometricDepth() const;
void        SetIsometricAngle(float angleDegrees);
```

### Edit Mode

```cpp
void       SetEditMode(EditMode mode);
EditMode   GetEditMode() const;
void       SetPendingNodeShape(BlockShape shape);
BlockShape GetPendingNodeShape() const;
```

### Selection & Operations

```cpp
void        SelectNode(const std::string& id);
void        DeselectAll();
std::string GetSelectedNodeId() const;

void        DeleteSelected();
void        Clear();
```

### Callbacks

```cpp
std::function<void(const std::string&)> onNodeCreated;
std::function<void(const std::string&)> onNodeSelected;
std::function<void(const std::string&)> onNodeDoubleClick;
std::function<void(const std::string&, const std::string&)> onConnectionCreated;
std::function<void(EditMode)> onEditModeChanged;
```

## Usage Examples

The following example is drawn directly from `Apps/DemoApp/UltraCanvasBlockDiagramExamples.cpp`, an HVAC system diagram built as a flat-2D, zoned layout.

### Creating the diagram canvas

```cpp
auto diagram = CreateBlockDiagram("mainDiagram", 50, 95, 800, 520);
diagram->SetBackgroundColor(Color(252, 252, 254, 255));
diagram->SetGridVisible(true, 20.0f);
diagram->SetGridColor(Color(238, 240, 244, 255));
diagram->SetSnapToGrid(true);

// Flat 2D so arrowheads land on the actual block edge instead of
// the front face of a 3D extrusion.
diagram->SetRenderStyle(UltraCanvasBlockDiagram::RenderStyle::Flat);
```

### Adding shaped, colored nodes

```cpp
Color coolFill   = Color(189, 215, 238, 255);
Color coolBorder = Color( 70, 120, 170, 255);
Color textColor  = Color( 25,  25,  35, 255);

diagram->AddNode("compressor", BlockShape::Rectangle, "Compressor",
                 200, 80, 120, 60);
diagram->SetNodeColor("compressor", coolFill, coolBorder);
diagram->SetNodeTextColor("compressor", textColor);

diagram->AddNode("condenser", BlockShape::Rectangle, "Condenser",
                 540, 80, 120, 60);
diagram->SetNodeColor("condenser", coolFill, coolBorder);
diagram->SetNodeTextColor("condenser", textColor);
```

### Connecting nodes with orthogonal routing

The refrigerant cycle is a closed clockwise loop. Every link uses orthogonal
routing so segments are axis-aligned and no arrow overlaps another.

```cpp
Color coolConn  = Color(70, 120, 170, 255);
float connWidth = 2.0f;

diagram->AddConnection("ref1", "compressor", "condenser",
                       ConnectionStyle::Orthogonal);
diagram->SetConnectionColor("ref1", coolConn);
diagram->SetConnectionWidth("ref1", connWidth);

diagram->AddConnection("ref2", "condenser", "expansion",
                       ConnectionStyle::Orthogonal);
diagram->SetConnectionColor("ref2", coolConn);
diagram->SetConnectionWidth("ref2", connWidth);

diagram->AddConnection("ref3", "expansion", "evaporator",
                       ConnectionStyle::Orthogonal);
diagram->SetConnectionColor("ref3", coolConn);
diagram->SetConnectionWidth("ref3", connWidth);

diagram->AddConnection("ref4", "evaporator", "compressor",
                       ConnectionStyle::Orthogonal);
diagram->SetConnectionColor("ref4", coolConn);
diagram->SetConnectionWidth("ref4", connWidth);
```

### Dashed control-signal lines

A dashed line distinguishes electrical control signals from the refrigerant
flow it modulates.

```cpp
diagram->AddConnection("ctrl_comp", "comp_control", "compressor",
                       ConnectionStyle::Orthogonal);
diagram->SetConnectionColor("ctrl_comp", ctrlConn);
diagram->SetConnectionWidth("ctrl_comp", connWidth);
diagram->SetConnectionLineStyle("ctrl_comp", LineStyle::Dashed);
```

### Toolbar wired to edit mode and view

```cpp
auto btnSelect = std::make_shared<UltraCanvasButton>("btnSelect", btnX, btnY, 90, 32);
btnSelect->SetText("Select");
btnSelect->SetOnClick([diagram]() {
    diagram->SetEditMode(UltraCanvasBlockDiagram::EditMode::Select);
});

auto btnConnect = std::make_shared<UltraCanvasButton>("btnConnect", btnX, btnY, 90, 32);
btnConnect->SetText("Connect");
btnConnect->SetOnClick([diagram]() {
    diagram->SetEditMode(UltraCanvasBlockDiagram::EditMode::CreateConnection);
});

auto btnZoomIn = std::make_shared<UltraCanvasButton>("btnZoomIn", btnX, btnY, 90, 32);
btnZoomIn->SetText("Zoom +");
btnZoomIn->SetOnClick([diagram]() {
    diagram->SetZoomLevel(diagram->GetZoomLevel() * 1.2f);
});

auto btnZoomOut = std::make_shared<UltraCanvasButton>("btnZoomOut", btnX, btnY, 90, 32);
btnZoomOut->SetText("Zoom -");
btnZoomOut->SetOnClick([diagram]() {
    diagram->SetZoomLevel(diagram->GetZoomLevel() * 0.8f);
});
```

### Resetting layout and view

Initial node positions are captured at construction so the Reset button can
restore them after the user drags nodes around.

```cpp
struct InitialNodePos { std::string id; float x; float y; };
std::vector<InitialNodePos> initialLayout = {
    {"compressor",   200,  80},
    {"condenser",    540,  80},
    {"expansion",    540, 200},
    {"evaporator",   200, 200},
    // ... etc.
};

auto btnReset = std::make_shared<UltraCanvasButton>("btnReset", btnX, btnY, 90, 32);
btnReset->SetText("Reset");
btnReset->SetOnClick([diagram, initialLayout]() {
    diagram->SetZoomLevel(1.0f);
    diagram->SetPanOffset(0, 0);
    for (const auto& p : initialLayout) {
        diagram->UpdateNodePosition(p.id, p.x, p.y);
    }
    diagram->DeselectAll();
});
```

### Toggling grid visibility from a checkbox

```cpp
auto chkGrid = std::make_shared<UltraCanvasCheckbox>("chkGrid", 50, 675, 100, 22);
chkGrid->SetText("Show Grid");
chkGrid->SetChecked(true);
chkGrid->onStateChanged = [diagram](CheckedState oldState, CheckedState newState) {
    bool visible = (newState == CheckedState::Checked);
    diagram->SetGridVisible(visible, 20.0f);
};
```

## Render Pipeline

The diagram renders in this order each frame:

1. Background fill
2. Grid (if visible)
3. Connections (with their arrows)
4. Nodes (shapes + text)
5. Connection preview (while drag-connecting)

## Mouse & Edit Behaviour

| Mode               | Mouse behavior                                                 |
|--------------------|----------------------------------------------------------------|
| `Select`           | Click to select; drag node to move; pan on empty area drag.    |
| `CreateNode`       | Click empty canvas to place a new node of `pendingNodeShape`.  |
| `CreateConnection` | Click source node, then click target node to wire a link.      |
| `Pan`              | Drag anywhere to pan the canvas.                               |

If `SetSnapToGrid(true)` is active, node positions snap to multiples of the
configured grid spacing during drag.

## See Also

- [UltraCanvasNodeDiagramExamples](UltraCanvasNodeDiagramExamples.md) — interactive graph editor with handles, multi-select, JSON I/O
- [UltraCanvasGourceTreeExamples](UltraCanvasGourceTreeExamples.md) — radial filesystem visualization
- [UltraCanvasAdjacencyDiagramExamples](UltraCanvasAdjacencyDiagramExamples.md) — architectural room adjacency
- [UltraCanvasArcDiagramExamples](UltraCanvasArcDiagramExamples.md) — nodes on a baseline with arc edges
- [UltraCanvasUIElement](UltraCanvasUIElement.md) — base element class
- [UltraCanvasRenderContext](UltraCanvasRenderContext.md) — rendering primitives used internally
