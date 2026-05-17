# UltraCanvasArcDiagram Documentation

## Overview

**UltraCanvasArcDiagram** lays nodes out on a single straight baseline (horizontal or vertical) and draws edges as cubic Bezier arcs curving above and/or below the baseline. Arc height encodes span; arc width and (optionally) opacity encode edge weight; node size can be fixed, scaled by value, or scaled by degree count. The component supports proportional, fixed, sqrt-scaled, and perfect-semicircle arc heights, parallel edge bundles, self-loops, mid-arc or end-of-arc arrowheads, vertical 90°-rotated labels, an axis arrow extending past the last node, and an optional color legend.

**Namespace:** `UltraCanvas`
**Header:** `include/Plugins/Diagrams/UltraCanvasArcDiagram.h`
**Base Class:** `UltraCanvasUIElement`
**Version:** 1.0.1

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasArcDiagram
```

## Header Include

```cpp
#include "Plugins/Diagrams/UltraCanvasArcDiagram.h"
```

## Features

- **Two baseline orientations**: Horizontal or Vertical
- **Four arc-height modes**: Proportional, Fixed, Sqrt, Semicircle (D-shape)
- **Three node sizing modes**: Fixed, ByValue, ByDegree
- **Arc width + optional opacity** encode edge weight
- **Arrowheads**: at target or at mid-arc apex
- **Self-loop arcs** for edges from a node to itself
- **Parallel edge bundles**: extra height-offset per parallel link to avoid overlap
- **Z-order by span**: shorter arcs drawn on top of longer ones
- **Label sides**: Below, Above, Alternating, or Vertical (90° rotated)
- **Axis arrow** extending the baseline past the last node
- **Color legend** with custom entries
- **Hover and click** callbacks for nodes and edges, with tooltips

## Data Structures

### Enumerations

```cpp
enum class ArcEdgeType {
    Above,     // Arc curves above the baseline (forward, default)
    Below,     // Arc curves below the baseline (back/secondary)
    Auto       // Above when source < target index, else below
};

enum class ArcLabelSide {
    Below,         // Default
    Above,
    Alternating,   // Alternates by node index
    Vertical       // Rotated 90° descending below baseline
};

enum class ArcHeightMode {
    Proportional,  // Height ∝ span between nodes
    Fixed,         // All arcs same fixed height
    Sqrt,          // Height = sqrt(span) × scale
    Semicircle     // Perfect D-shape: height = span/2
};

enum class ArcOrientation { Horizontal, Vertical };

enum class ArcArrowPosition { AtTarget, AtMidArc };

enum class ArcNodeSizeMode {
    Fixed,         // All nodes use ArcNode.size
    ByValue,       // size = baseSize * sqrt(node.value)
    ByDegree       // size = baseSize * sqrt(connectionCount)
};
```

### ArcLegendEntry

```cpp
struct ArcLegendEntry {
    std::string label;
    Color       color = Color(100, 149, 237, 200);
};
```

### ArcNode

```cpp
struct ArcNode {
    std::string id;
    std::string label;
    float       size         = 6.0f;
    Color       color        = Color(70, 130, 180, 255);
    float       value        = 1.0f;
    bool        scaleByValue = false;
};
```

### ArcEdge

```cpp
struct ArcEdge {
    std::string sourceId;
    std::string targetId;
    float       weight   = 1.0f;
    Color       color    = Color(100, 149, 237, 180);
    ArcEdgeType side     = ArcEdgeType::Auto;
    std::string label;
    bool        directed = false;
};
```

### ArcDiagramStyle (selected fields)

```cpp
struct ArcDiagramStyle {
    // Baseline
    Color baselineColor = Color(180, 180, 180, 255);
    float baselineWidth = 1.0f;
    bool  showBaseline  = true;

    // Nodes
    float nodeBaseSize    = 6.0f;
    float nodeStrokeWidth = 1.0f;
    Color nodeHoverColor    = Color(255, 200, 50, 255);
    Color nodeSelectedColor = Color(255, 120,  0, 255);

    // Labels
    float        labelFontSize = 11.0f;
    Color        labelColor    = Color(40, 40, 40, 255);
    ArcLabelSide labelSide     = ArcLabelSide::Below;

    // Arcs
    float          arcMinWidth     = 0.5f;
    float          arcMaxWidth     = 6.0f;
    float          arcHeightScale  = 0.5f;
    float          arcFixedHeight  = 60.0f;
    ArcHeightMode  arcHeightMode   = ArcHeightMode::Proportional;
    bool           showArrows      = true;
    float          arrowSize       = 8.0f;

    ArcNodeSizeMode  nodeSizeMode   = ArcNodeSizeMode::Fixed;

    bool             arcEncodeOpacity = false;
    uint8_t          arcMinOpacity    = 40;
    uint8_t          arcMaxOpacity    = 220;

    bool             showAxisArrow     = false;
    float            axisArrowOverhang = 20.0f;

    ArcArrowPosition arrowPosition     = ArcArrowPosition::AtTarget;

    float            bundleHeightStep = 8.0f;   // Per parallel edge
    bool             sortBySpan       = false;  // Short arcs drawn on top

    bool             showLegend       = false;
    std::vector<ArcLegendEntry> legendEntries;
    float            legendX = 20.0f, legendY = 20.0f;
    float            legendFontSize = 11.0f;

    float nodePadding = 40.0f, marginH = 40.0f, marginV = 40.0f;
    ArcOrientation orientation = ArcOrientation::Horizontal;
};
```

## Class Reference

### Constructor

```cpp
UltraCanvasArcDiagram(const std::string& id,
                      long x, long y, long w, long h);
```

### Factory Function

```cpp
std::shared_ptr<UltraCanvasArcDiagram> CreateArcDiagram(
        const std::string& id,
        long x, long y, long width, long height);
```

### Node API

```cpp
int  AddNode(const ArcNode& node);
int  AddNode(const std::string& id, const std::string& label);

bool RemoveNode(const std::string& id);
int  GetNodeCount() const;

const ArcNode* GetNode(int index) const;
const ArcNode* GetNodeById(const std::string& id) const;

void UpdateNode(const std::string& id, const ArcNode& updated);
```

### Edge API

```cpp
int  AddEdge(const ArcEdge& edge);
int  AddEdge(const std::string& sourceId,
             const std::string& targetId,
             float weight = 1.0f);

void RemoveEdge(const std::string& sourceId, const std::string& targetId);
void ClearEdges();
void Clear();
int  GetEdgeCount() const;
```

### Style & Selection

```cpp
void SetStyle(const ArcDiagramStyle& s);
const ArcDiagramStyle& GetStyle() const;

int  GetSelectedNodeIndex() const;
int  GetSelectedEdgeIndex() const;
void ClearSelection();
```

### Callbacks

```cpp
std::function<void(int, const ArcNode&)> onNodeClick;
std::function<void(int, const ArcEdge&)> onEdgeClick;
std::function<void(int, const ArcNode&)> onNodeHover;
std::function<void(int, const ArcEdge&)> onEdgeHover;
```

## Usage Examples

All examples are drawn from `Apps/DemoApp/UltraCanvasArcDiagramExamples.cpp`.

### Character co-occurrence — degree-sized nodes, vertical labels, semicircles

```cpp
auto diagram = std::make_shared<UltraCanvasArcDiagram>(
        "ArcFriends", 10, 34, 990, 654);

struct Char { const char* id; const char* label; Color color; };
Char chars[] = {
    {"monica",  "Monica",   Color(100, 160, 220, 255)},
    {"rachel",  "Rachel",   Color(100, 160, 220, 255)},
    {"chandler","Chandler", Color( 90, 120, 200, 255)},
    {"ross",    "Ross",     Color(120,  80, 200, 255)},
    {"joey",    "Joey",     Color(180, 120, 200, 255)},
    {"phoebe",  "Phoebe",   Color(220, 120, 200, 255)},
    // ...more characters
};

for (const auto& c : chars) {
    ArcNode n;
    n.id    = c.id;
    n.label = c.label;
    n.color = c.color;
    n.size  = 8.0f;
    diagram->AddNode(n);
}

struct Edge { const char* a; const char* b; float w; };
Edge edges[] = {
    {"monica","rachel",   8},{"monica","chandler", 9},{"monica","ross",   8},
    {"rachel","ross",     9},{"chandler","joey",   9},{"ross",  "joey",   7},
    {"joey",  "phoebe",   6},
    // ...
};

for (const auto& e : edges) {
    ArcEdge ae;
    ae.sourceId = e.a;
    ae.targetId = e.b;
    ae.weight   = e.w;
    ae.color    = Color(160, 160, 160, static_cast<uint8_t>(60 + e.w * 15));
    ae.side     = ArcEdgeType::Above;
    diagram->AddEdge(ae);
}

ArcDiagramStyle s;
s.nodeSizeMode     = ArcNodeSizeMode::ByDegree;   // Bigger node = more connections
s.nodeBaseSize     = 5.0f;
s.arcHeightMode    = ArcHeightMode::Semicircle;
s.arcEncodeOpacity = true;
s.arcMinOpacity    = 30;
s.arcMaxOpacity    = 180;
s.arcMinWidth      = 0.5f;
s.arcMaxWidth      = 3.5f;
s.labelSide        = ArcLabelSide::Vertical;       // 90° rotated labels
s.labelFontSize    = 10.0f;
s.nodePadding      = 26.0f;
s.marginH          = 60.0f;
s.marginV          = 120.0f;
s.sortBySpan       = true;                         // Short arcs drawn on top
diagram->SetStyle(s);
```

### Canonical reference — axis arrow + color legend

```cpp
const char* nodes[] = {"A","B","C","D","E","F","G","H"};
Color nodeCol(80, 120, 200, 240);
for (const char* n : nodes) {
    ArcNode an;
    an.id = n; an.label = n;
    an.color = nodeCol; an.size = 10.0f;
    diagram->AddNode(an);
}

Color red   (210,  50,  40, 200);
Color orange(220, 150,  50, 180);
Color blue  ( 60, 120, 200, 200);

struct CatEdge { const char* a; const char* b; float w; Color col; };
CatEdge catEdges[] = {
    {"A","H", 4.0f, red},
    {"B","G", 2.5f, red},
    {"A","D", 2.0f, orange},
    {"B","E", 1.5f, orange},
    {"C","D", 2.5f, blue},
    {"E","F", 2.0f, blue},
    {"F","H", 1.0f, orange},
    {"G","H", 1.5f, blue},
};

for (const auto& e : catEdges) {
    ArcEdge ae;
    ae.sourceId = e.a; ae.targetId = e.b;
    ae.weight   = e.w; ae.color    = e.col;
    ae.side     = ArcEdgeType::Above;
    diagram->AddEdge(ae);
}

ArcDiagramStyle s;
s.arcHeightMode = ArcHeightMode::Semicircle;
s.arcMinWidth   = 1.0f;
s.arcMaxWidth   = 6.0f;
s.labelSide     = ArcLabelSide::Below;
s.labelFontSize = 13.0f;
s.nodeBaseSize  = 10.0f;
s.nodePadding   = 90.0f;
s.marginH       = 80.0f;
s.marginV       = 60.0f;
s.showAxisArrow = true;
s.showLegend    = true;
s.legendEntries = {
    {"Category 1", red},
    {"Category 2", orange},
    {"Category 3", blue}
};
s.legendX = 20.0f;
s.legendY = 30.0f;
diagram->SetStyle(s);
```

### Directed flow — mid-arc arrowheads and self-loops

```cpp
struct FlowNode { const char* id; const char* label; float size; Color color; };
FlowNode fnodes[] = {
    {"home",    "Home Page",      20.0f, Color( 80, 140, 210, 230)},
    {"fleet",   "The Fleet",      28.0f, Color( 30,  30,  30, 240)},
    {"special", "Special Forces", 22.0f, Color(180,  50,  50, 230)},
    {"train",   "Training",        8.0f, Color(180,  80,  80, 200)},
    {"weNavy",  "We Are Navy",     8.0f, Color( 60,  60,  60, 200)},
    {"marco",   "San Marco Briga.",18.0f, Color( 40, 120,  40, 230)},
};
for (const auto& fn : fnodes) {
    ArcNode an;
    an.id = fn.id; an.label = fn.label;
    an.color = fn.color; an.size = fn.size;
    diagram->AddNode(an);
}

auto makeEdge = [](const char* s, const char* t, float w, Color c,
                   bool dir, ArcEdgeType side, const std::string& lbl = "") {
    ArcEdge e;
    e.sourceId = s; e.targetId = t;
    e.weight = w;   e.color   = c;
    e.directed = dir; e.side  = side;
    e.label = lbl;
    return e;
};

Color green (80, 180, 80, 200);
Color gray  (150, 150, 150, 180);
Color violet(160, 80, 200, 180);

diagram->AddEdge(makeEdge("home",  "fleet",   10.0f, green, true, ArcEdgeType::Above, "10"));
diagram->AddEdge(makeEdge("fleet", "special", 13.0f, green, true, ArcEdgeType::Above, "13"));
diagram->AddEdge(makeEdge("home",  "marco",   10.0f, green, true, ArcEdgeType::Above, "10"));
diagram->AddEdge(makeEdge("fleet", "train",    5.0f, gray,  true, ArcEdgeType::Above, "5"));
diagram->AddEdge(makeEdge("fleet", "weNavy",   5.0f, gray,  true, ArcEdgeType::Above, "5"));
diagram->AddEdge(makeEdge("fleet", "marco",    5.0f, gray,  true, ArcEdgeType::Above, "5"));

// Self-loop drawn below the baseline
ArcEdge selfLoop = makeEdge("special","special", 3.0f, violet, true,
                             ArcEdgeType::Below, "3");
diagram->AddEdge(selfLoop);

ArcDiagramStyle s;
s.arcHeightMode    = ArcHeightMode::Proportional;
s.arcHeightScale   = 0.38f;
s.arcMinWidth      = 1.5f;
s.arcMaxWidth      = 7.0f;
s.showArrows       = true;
s.arrowPosition    = ArcArrowPosition::AtMidArc;   // Arrow at apex, not at target
s.arrowSize        = 9.0f;
s.labelSide        = ArcLabelSide::Below;
s.labelFontSize    = 11.0f;
s.arcLabelFontSize = 11.0f;
s.nodeSizeMode     = ArcNodeSizeMode::Fixed;
s.nodePadding      = 130.0f;
s.marginH          = 80.0f;
s.marginV          = 60.0f;
diagram->SetStyle(s);
```

### Genomics — parallel bundles above and below

```cpp
// 20 genomic positions as nodes
for (int i = 1; i <= 20; ++i) {
    ArcNode n;
    char id[8]; snprintf(id, sizeof(id), "p%d", i);
    n.id    = id;
    n.label = std::to_string(i);
    n.color = Color(100, 100, 100, 220);
    n.size  = 5.0f;
    diagram->AddNode(n);
}

// P-value color bands
Color darkBlue ( 8,  48, 107, 220);
Color lightBlue(100, 160, 210, 180);
Color amber    (220, 160,  40, 180);
Color red      (200,  50,  40, 180);

struct Pair { int a; int b; Color c; };
Pair knownPairs[] = {
    {1,18, darkBlue},{2,17, darkBlue},{3,16, darkBlue},{4,15, lightBlue},
    {5,14, lightBlue},{6,13, amber},{7,12, amber},{8,11, red},{9,10, red},
};

for (const auto& p : knownPairs) {
    char idA[8], idB[8];
    snprintf(idA, sizeof(idA), "p%d", p.a);
    snprintf(idB, sizeof(idB), "p%d", p.b);
    ArcEdge e;
    e.sourceId = idA; e.targetId = idB;
    e.weight = 1.0f;  e.color    = p.c;
    e.side  = ArcEdgeType::Above;    // Known pairs above
    diagram->AddEdge(e);
}

Pair novelPairs[] = {
    {1,10, red},{2,11, red},{3,12, amber},{4,13, amber},
    {5,14, lightBlue},{6,15, lightBlue},{7,16, darkBlue},
};
for (const auto& p : novelPairs) {
    char idA[8], idB[8];
    snprintf(idA, sizeof(idA), "p%d", p.a);
    snprintf(idB, sizeof(idB), "p%d", p.b);
    ArcEdge e;
    e.sourceId = idA; e.targetId = idB;
    e.weight = 1.0f;  e.color    = p.c;
    e.side  = ArcEdgeType::Below;    // Novel pairs below
    diagram->AddEdge(e);
}

ArcDiagramStyle s;
s.arcHeightMode    = ArcHeightMode::Semicircle;
s.arcMinWidth      = 0.8f;
s.arcMaxWidth      = 2.0f;
s.bundleHeightStep = 6.0f;    // Stack parallel arcs by this delta
s.sortBySpan       = true;
s.labelSide        = ArcLabelSide::Below;
s.labelFontSize    = 10.0f;
s.nodeBaseSize     = 5.0f;
s.nodePadding      = 30.0f;
s.marginH          = 40.0f;
s.marginV          = 50.0f;
s.showLegend       = true;
s.legendEntries    = {
    {"Known: [0.1e-06]",       darkBlue},
    {"Known: (1e-06,1e-05]",   lightBlue},
    {"Known: (1e-05,0.0001]",  amber},
    {"Known: (0.0001,0.001]",  red},
};
s.legendX = 20.0f;
s.legendY = 10.0f;
diagram->SetStyle(s);
```

### Reporting node and edge selection

```cpp
diagram->onNodeClick = [statusLabel](int, const ArcNode& n) {
    statusLabel->SetText("Character: " + n.label);
};

diagram->onEdgeClick = [statusLabel](int, const ArcEdge& e) {
    std::ostringstream ss;
    ss << e.sourceId << " — " << e.targetId
       << "  scenes: " << static_cast<int>(e.weight);
    statusLabel->SetText(ss.str());
};
```

## Render Order

Each frame the arc diagram draws:

1. Baseline (+ axis arrow if enabled)
2. Edges (arcs), sorted by span when `sortBySpan` is true
3. Nodes (sized by mode)
4. Labels (side per `labelSide`)
5. Legend (if enabled)
6. Tooltip on hover

## Encoding Cheat Sheet

| Visual            | Controlled by                                                   |
|-------------------|------------------------------------------------------------------|
| Node radius       | `nodeSizeMode` + `nodeBaseSize` (and `ArcNode.value` / degree)   |
| Arc height        | `arcHeightMode` + `arcHeightScale` (or `arcFixedHeight`)         |
| Arc width         | `arcMinWidth` … `arcMaxWidth` interpolated by edge `weight`      |
| Arc opacity       | `arcEncodeOpacity` between `arcMinOpacity` … `arcMaxOpacity`     |
| Arrow position    | `arrowPosition` (AtTarget, AtMidArc)                              |
| Side of baseline  | `ArcEdge.side` (Above, Below, Auto)                              |
| Parallel stacking | `bundleHeightStep` adds height per parallel arc                  |
| Z-order           | `sortBySpan` — longer arcs first, shorter on top                 |

## See Also

- [UltraCanvasBlockDiagramExamples](UltraCanvasBlockDiagramExamples.md) — flowchart-style block diagrams
- [UltraCanvasNodeDiagramExamples](UltraCanvasNodeDiagramExamples.md) — interactive graph editor with handles
- [UltraCanvasGourceTreeExamples](UltraCanvasGourceTreeExamples.md) — radial filesystem tree
- [UltraCanvasAdjacencyDiagramExamples](UltraCanvasAdjacencyDiagramExamples.md) — architectural room adjacency
- [UltraCanvasTabbedContainer](UltraCanvasTabbedContainer.md) — tab control used by the demo
- [UltraCanvasUIElement](UltraCanvasUIElement.md) — base element class
