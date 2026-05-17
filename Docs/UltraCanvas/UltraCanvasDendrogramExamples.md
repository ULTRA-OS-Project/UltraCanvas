# UltraCanvasDendrogram Documentation

**Version:** 1.4.1
**Last Modified:** 2026-05-09
**Author:** UltraCanvas Framework

## Overview

`UltraCanvasDendrogram` is an interactive dendrogram / phylogenetic tree element. It renders hierarchical clustering data using a Reingold–Tilford layout (provided by `UltraCanvasDendrogramLayout`), supports multiple orientations (TopDown, BottomUp, LeftRight, RightLeft, Radial), multiple link styles (Rectangular, Curved, Diagonal), and rich styling of branches, leaf labels, internal-node clade names, confidence indicators, and group annotations. It supports zoom, pan, manual node repositioning, collapse/expand of internal subtrees, single and multi-selection, and emits callbacks for clicks, hovers, group highlighting, selection changes, and collapse state.

**Namespace:** `UltraCanvas`
**Header:** `include/Plugins/Diagrams/UltraCanvasDendrogram.h`
**Layout Header:** `include/Plugins/Diagrams/UltraCanvasDendrogramLayout.h`
**Implementation:** `Plugins/Diagrams/UltraCanvasDendrogram.cpp`
**Base Class:** `UltraCanvasUIElement`

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasDendrogram
```

## Features

- **Orientations:** Top-down, bottom-up, left-right, right-left, radial.
- **Scale modes:** `Proportional` (branch lengths reflect merge distance) or `Cladogram` (topology only, equidistant leaves).
- **Link styles:** `Rectangular`, `Curved`, `Diagonal`.
- **Branch coloring:** uniform, by group, by distance gradient, by per-node value gradient, or fully custom.
- **Confidence visualization:** map confidence to line thickness, dash pattern, node dots, or both.
- **Group annotations:** sidebar brackets, floating badges, arc text (radial), leader callouts; optional band/sector/convex-hull fills.
- **Interaction:** click to select leaves, shift/ctrl-click for multi-select, double-click internal nodes to collapse/expand, scroll-wheel zoom, middle/Alt+left drag to pan, ctrl+drag a node to reposition it manually.
- **Distance axis & grid:** optional axis with configurable tick count and font size.
- **Label background pills:** optional rounded rectangles behind labels for readability.

## Header Includes

```cpp
#include "Plugins/Diagrams/UltraCanvasDendrogram.h"
#include "Plugins/Diagrams/UltraCanvasDendrogramLayout.h"
```

## Class Reference

### Constructor

```cpp
UltraCanvasDendrogram(const std::string& id,
                      int x, int y, int width, int height);
```

### Data

```cpp
void SetDataSource(std::shared_ptr<IDendrogramDataSource> data);
std::shared_ptr<IDendrogramDataSource> GetDataSource() const;
```

### Layout Control

```cpp
void SetOrientation(DendrogramOrientation o);
DendrogramOrientation GetOrientation() const;

void SetScaleMode(DendrogramScaleMode m);
DendrogramScaleMode GetScaleMode() const;

void SetLeafSpacing(double px);
```

### Style

```cpp
void SetStyle(const DendrogramStyle& s);
const DendrogramStyle& GetStyle() const;

void SetBranchColorMode(BranchColorMode m);
void SetLinkStyle(DendrogramLinkStyle l);
void SetConfidenceMode(ConfidenceDisplayMode m);
```

### Selection

```cpp
void SelectNode(const std::string& nodeId, bool addToSelection = false);
void ClearSelection();
const std::vector<std::string>& GetSelectedLeafIds() const;
```

### Zoom, Pan, Custom Positions

```cpp
void  SetZoom(double z);
double GetZoom() const;
void  ResetView();

void ClearCustomPositions();
void ResetNodePositions();

void ToggleCollapse(const std::string& nodeId);
bool IsCollapsed(const std::string& nodeId) const;
void ExpandAll();
void CollapseAll();
```

### Callbacks

```cpp
std::function<void(const std::string&)>                  onLeafClicked;
std::function<void(const std::string&)>                  onInternalNodeClicked;
std::function<void(const std::string&)>                  onNodeHovered;
std::function<void(const std::string&)>                  onGroupClicked;
std::function<void(const std::string&, bool)>            onGroupHighlighted;
std::function<void(const std::vector<std::string>&)>     onSelectionChanged;
std::function<void(const std::string&, bool)>            onNodeCollapsed;
```

## Data Structures

### DendrogramNode (Input)

```cpp
struct DendrogramNode {
    std::string id;
    std::string label;
    double mergeDistance = 0.0;   // 0 for leaves
    std::string groupId;          // Assigns leaf to a DendrogramGroup

    float confidence       = 1.0f;   // 0-1; drives thickness/dash/dot
    float branchValue      = -1.0f;  // -1 = unset; 0-1 gradient input
    Color customBranchColor = Colors::Transparent; // BranchColorMode::Custom

    std::string nodeAnnotation;
    std::string tooltip;
    void* userData = nullptr;

    std::vector<std::string> childIds;
    bool IsLeaf() const;
};
```

### DendrogramGroup

```cpp
struct DendrogramGroup {
    std::string groupId;
    std::string label;
    std::vector<std::string> leafIds;

    Color branchColor = Color( 80,  80, 200, 255);
    Color labelColor  = Color( 60,  60, 180, 255);
    Color fillColor   = Color( 80,  80, 200,  40);

    GroupLabelPlacement labelPlacement = GroupLabelPlacement::SidebarBracket;
    GroupFillMode       fillMode       = GroupFillMode::BandFill;
    float fillOpacity = 0.15f;

    bool       showGroupLabel     = true;
    float      groupLabelFontSize = 12.0f;
    FontWeight groupLabelWeight   = FontWeight::Bold;
};
```

### DendrogramStyle (excerpt)

```cpp
struct DendrogramStyle {
    BranchColorMode branchColorMode = BranchColorMode::ByGroup;
    Color  defaultBranchColor       = Color(120, 120, 120, 255);
    double defaultBranchWidth        = 1.5f;
    DendrogramLinkStyle linkStyle   = DendrogramLinkStyle::Rectangular;

    ConfidenceDisplayMode confidenceMode = ConfidenceDisplayMode::None;
    double confidenceHighThreshold       = 0.80f;
    double confidenceLowThreshold        = 0.40f;
    double branchWidthMin = 0.5f, branchWidthMax = 3.5f;

    Color gradientLow  = Color( 24,  95, 165, 255);
    Color gradientHigh = Color(163,  45,  45, 255);

    double leafLabelFontSize  = 11.0f;
    bool   colorLeafLabels    = true;
    int    leafLabelPadding   = 5;
    Color  defaultLeafColor   = Color(40, 40, 40, 255);

    bool   showInternalNodeLabels    = false;
    double internalNodeLabelFontSize = 10.0f;
    Color  internalNodeLabelColor    = Color(60, 60, 70, 255);
    int    internalNodeLabelOffset   = 6;
    bool   showLabelsOnClick         = true;

    bool   showLabelBackground   = true;
    Color  labelBackgroundColor  = Color(255, 255, 255, 210);
    Color  labelBackgroundBorder = Color(200, 200, 210, 120);
    int    labelBackgroundPadding = 3;
    double labelBackgroundRadius  = 3.0f;

    double internalNodeRadius = 3.0f;
    double leafNodeRadius     = 2.0f;
    bool   showInternalNodes  = true;
    bool   showLeafNodes      = true;
    Color  internalNodeColor  = Color(100, 100, 100, 255);

    bool   scaleNodesByDepth      = false;
    double nodeRadiusDepthScale   = 1.2f;
    double nodeRadiusMax          = 10.0f;

    bool   showDistanceAxis = true;
    int    axisTickCount    = 5;
    double axisFontSize     = 10.0f;
    Color  axisColor        = Color(160, 160, 160, 255);
    Color  gridColor        = Color(225, 225, 225, 255);
    bool   showGrid         = true;

    bool   showGroupLabels     = true;
    int    sidebarBracketWidth = 5;
    int    sidebarBracketGap   = 6;
    int    floatingBadgePad    = 5;
    double arcLabelRadiusMul   = 1.10f;

    Color  backgroundColor = Color(255, 255, 255, 255);

    int marginTop = 20, marginBottom = 40, marginLeft = 20, marginRight = 20;
    double leafSpacing = 18.0f;
};
```

### Enumerations

```cpp
enum class DendrogramOrientation {
    TopDown, BottomUp, LeftRight, RightLeft, Radial
};

enum class DendrogramScaleMode { Proportional, Cladogram };

enum class DendrogramLinkStyle { Rectangular, Curved, Diagonal };

enum class BranchColorMode {
    Uniform, ByGroup, ByDistance, ByValue, Custom
};

enum class GroupLabelPlacement {
    None, SidebarBracket, FloatingBadge, ArcText, LeaderCallout
};

enum class GroupFillMode {
    None, BandFill, SectorFill, ConvexHull
};

enum class ConfidenceDisplayMode {
    None, LineThickness, LineDash, NodeDot, LineThicknessAndDot
};
```

### Data Source Interfaces

```cpp
class IDendrogramDataSource {
public:
    virtual ~IDendrogramDataSource() = default;
    virtual size_t GetNodeCount() const = 0;
    virtual const DendrogramNode* GetNode(size_t index) const = 0;
    virtual const DendrogramNode* GetNodeById(const std::string& id) const = 0;
    virtual std::string GetRootId() const = 0;

    virtual size_t GetGroupCount() const { return 0; }
    virtual const DendrogramGroup* GetGroup(size_t index) const { return nullptr; }
    virtual const DendrogramGroup* GetGroupById(const std::string& id) const { return nullptr; }
};

class DendrogramDataVector : public IDendrogramDataSource {
public:
    void AddNode(const DendrogramNode& node);
    void AddGroup(const DendrogramGroup& group);
    void SetRootId(const std::string& id);
    // ... + IDendrogramDataSource overrides
};
```

## Usage Examples

The following examples are drawn from `Apps/DemoApp/UltraCanvasDendrogramExamples.cpp`.

### Building a Tree from a Flat Spec List

The demo's `BuildTree` helper translates a flat description (id, label, parentId, ...) into a populated `DendrogramDataVector`:

```cpp
struct NodeSpec {
    std::string id, label, parentId, groupId;
    double mergeDistance = 0.0;
    float  confidence    = 1.0f;
    float  branchValue   = -1.0f;
};

static std::shared_ptr<DendrogramDataVector> BuildTree(
        const std::vector<NodeSpec>& specs,
        const std::vector<DendrogramGroup>& groups = {}) {
    std::unordered_map<std::string, std::vector<std::string>> childMap;
    std::string rootId;
    for (const auto& s : specs) {
        if (s.parentId.empty()) rootId = s.id;
        else childMap[s.parentId].push_back(s.id);
    }

    auto data = std::make_shared<DendrogramDataVector>();
    for (const auto& s : specs) {
        DendrogramNode n;
        n.id            = s.id;
        n.label         = s.label;
        n.mergeDistance = s.mergeDistance;
        n.groupId       = s.groupId;
        n.confidence    = s.confidence;
        n.branchValue   = s.branchValue;
        auto it = childMap.find(s.id);
        if (it != childMap.end()) n.childIds = it->second;
        data->AddNode(n);
    }
    data->SetRootId(rootId);
    for (const auto& g : groups) data->AddGroup(g);
    return data;
}
```

### Demo 1 — Top-Down Hierarchical Clustering

```cpp
std::vector<NodeSpec> specs = {
    {"root",  "All Species",     "",     "",      1.00},
    {"cA",    "Mammalia",        "root", "",      0.85},
    {"cA1",   "Primates",        "cA",   "grpA",  0.55},
    {"A1a",   "Homo sapiens",    "cA1",  "grpA",  0.0, 0.98f},
    {"A1b",   "Pan troglodytes", "cA1",  "grpA",  0.0, 0.96f},
    // ... etc.
};

std::vector<DendrogramGroup> groups = {
    {"grpA", "Primates", {"A1a","A1b","A1c","A1d"},
     Color(30,120,210,255), Color(20,100,190,255), Color(30,120,210,40),
     GroupLabelPlacement::SidebarBracket, GroupFillMode::BandFill, 0.12f},
    // ... etc.
};

auto data = BuildTree(specs, groups);
auto d    = std::make_shared<UltraCanvasDendrogram>("D1", 0, 36, w, h - 36);

d->SetDataSource(data);
d->SetOrientation(DendrogramOrientation::TopDown);
d->SetScaleMode(DendrogramScaleMode::Proportional);

DendrogramStyle s;
s.linkStyle          = DendrogramLinkStyle::Rectangular;
s.branchColorMode    = BranchColorMode::ByGroup;
s.defaultBranchColor = Color(80, 80, 90, 255);
s.defaultBranchWidth = 2.0f;
s.confidenceMode     = ConfidenceDisplayMode::LineThickness;
s.branchWidthMin     = 1.0f;
s.branchWidthMax     = 3.5f;
s.leafLabelFontSize  = 13.0f;
s.colorLeafLabels    = true;
s.showDistanceAxis   = true;
s.showGrid           = true;
s.showGroupLabels    = true;
s.sidebarBracketWidth = 6;
s.scaleNodesByDepth  = true;
s.nodeRadiusDepthScale = 1.2f;
s.showInternalNodeLabels = true;
s.showLabelsOnClick      = true;
s.marginBottom = 100; // Room for 45-degree leaf labels
s.marginLeft   =  60; // Room for distance axis
d->SetStyle(s);

d->onLeafClicked         = [](const std::string& id) { /* ... */ };
d->onInternalNodeClicked = [](const std::string& id) { /* ... */ };
```

### Demo 2 — Left-Right Curved Cladogram

```cpp
auto d = std::make_shared<UltraCanvasDendrogram>("D2", 0, 36, w, h - 36);
d->SetDataSource(data);
d->SetOrientation(DendrogramOrientation::LeftRight);
d->SetScaleMode(DendrogramScaleMode::Cladogram);

DendrogramStyle s;
s.linkStyle           = DendrogramLinkStyle::Curved;
s.branchColorMode     = BranchColorMode::Uniform;
s.defaultBranchColor  = Color(130, 130, 140, 220);
s.leafLabelFontSize   = 13.0f;
s.showInternalNodes   = true;
s.scaleNodesByDepth   = true;
s.nodeRadiusDepthScale = 2.0f;
s.nodeRadiusMax       = 12.0f;
s.showDistanceAxis    = false;
s.showGroupLabels     = false;
s.leafSpacing         = 35.0f;
d->SetStyle(s);
```

### Demo 3 — Radial Tree of Life with Sector Fills

```cpp
auto d = std::make_shared<UltraCanvasDendrogram>("D3", 0, 36, w, h - 36);
d->SetDataSource(data);
d->SetOrientation(DendrogramOrientation::Radial);
d->SetScaleMode(DendrogramScaleMode::Proportional);

std::vector<DendrogramGroup> groups = {
    {"bacteria",  "Bacteria",
     {"Staph","Bacil","Esch","Salm"},
     Color(180, 50, 120, 255), Color(160, 30, 100, 255),
     Color(180, 50, 120, 255),
     GroupLabelPlacement::ArcText, GroupFillMode::SectorFill, 0.22f},
    {"archaea",   "Archaea",
     {"Sulfo","Methan"},
     Color(180, 155, 30, 255), Color(160, 135, 10, 255),
     Color(200, 175, 30, 255),
     GroupLabelPlacement::ArcText, GroupFillMode::SectorFill, 0.22f},
    {"eukaryota", "Eukaryota",
     {"Sacc","HomoS","Arabi"},
     Color(30, 150, 130, 255), Color(20, 130, 110, 255),
     Color(30, 150, 130, 255),
     GroupLabelPlacement::ArcText, GroupFillMode::SectorFill, 0.22f},
};

DendrogramStyle s;
s.linkStyle             = DendrogramLinkStyle::Curved;
s.branchColorMode       = BranchColorMode::ByGroup;
s.colorLeafLabels       = true;
s.colorLeafNodesByGroup = true;
s.showInternalNodes     = true;
s.scaleNodesByDepth     = true;
s.showGroupLabels       = true;
s.arcLabelRadiusMul     = 1.14f;
s.marginTop = s.marginBottom = s.marginLeft = s.marginRight = 70;
d->SetStyle(s);
```

### Demo 6 — Branch Color By Value Gradient

```cpp
DendrogramStyle s;
s.linkStyle          = DendrogramLinkStyle::Rectangular;
s.branchColorMode    = BranchColorMode::ByValue;
s.gradientLow        = Color( 24,  95, 165, 255); // Blue
s.gradientHigh       = Color(163,  45,  45, 255); // Red
s.defaultBranchWidth = 2.2f;
s.leafLabelFontSize  = 13.0f;
s.colorLeafLabels    = true;
s.showInternalNodes  = true;
s.scaleNodesByDepth  = true;
s.showDistanceAxis   = true;
s.axisTickCount      = 5;
s.showGrid           = true;
d->SetStyle(s);
```

Per-node values are set via `DendrogramNode::branchValue` in the input data (0.0–1.0).

### Control Bar (Zoom, Reset, Collapse, Expand)

```cpp
mkBtn("rst",  110, "Reset View",   [d]{ d->ResetView(); });
mkBtn("zi",    36, "+",            [d]{ d->SetZoom(d->GetZoom() * 1.3f); });
mkBtn("zo",    36, "-",            [d]{ d->SetZoom(d->GetZoom() / 1.3f); });
mkBtn("exp",  120, "Expand All",   [d]{ d->ExpandAll(); });
mkBtn("col",  120, "Collapse All", [d]{ d->CollapseAll(); });
mkBtn("rpos", 120, "Reset Pos",    [d]{ d->ResetNodePositions(); });
```

## Mouse & Keyboard Cheat-Sheet

| Action                         | Behavior                              |
|--------------------------------|---------------------------------------|
| Scroll wheel                   | Zoom in/out toward cursor             |
| Middle-drag, Alt+Left-drag     | Pan the view                          |
| Left-click leaf                | Select the leaf                       |
| Shift/Ctrl + click leaf        | Multi-select                          |
| Double-click internal node     | Collapse / expand subtree             |
| Ctrl + drag node               | Move node manually                    |

## See Also

- [UltraCanvasSankeyExamples](UltraCanvasSankeyExamples.md) — flow diagrams
- [UltraCanvasVennDiagramExamples](UltraCanvasVennDiagramExamples.md) — set intersections
- [UltraCanvasTreeMapExamples](UltraCanvasTreeMapExamples.md) — hierarchical area visualization
- [UltraCanvasFlowChartExamples](UltraCanvasFlowChartExamples.md) — node/edge editor
- [UltraCanvasTreeView](UltraCanvasTreeViewExamples.md) — list-style hierarchical view
- [UltraCanvasLineChartElement](UltraCanvasLineChartElement.md) — line charts
- [UltraCanvasUIElement](UltraCanvasUIElement.md) — base element class
