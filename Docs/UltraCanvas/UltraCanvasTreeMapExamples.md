# UltraCanvasTreeMapElement Documentation

**Version:** 1.0.1
**Last Modified:** 2025-04-01
**Author:** UltraCanvas Framework

## Overview

`UltraCanvasTreeMapElement` is a hierarchical treemap component for visualizing nested datasets as nested rectangles whose areas are proportional to a numeric value. Each `TreeMapNode` carries a primary value (size) and a secondary value (for color shading), and the element supports several tiling algorithms, visual styles, color schemes, and interactive drill-down navigation through the hierarchy.

**Namespace:** `UltraCanvas`
**Header:** `include/Plugins/Diagrams/UltraCanvasTreeMapElement.h`
**Implementation:** `Plugins/Diagrams/UltraCanvasTreeMapElement.cpp`
**Base Class:** `UltraCanvasChartElementBase`

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasChartElementBase
            └── UltraCanvasTreeMapElement
```

## Features

- **Multiple layout algorithms:** `Squarified`, `SliceAndDice`, `Strip`, `Spiral`, `Binary`.
- **Visual styles:** `Flat`, `Raised`, `Sunken`, `Gradient`, `Textured`, `Minimal`.
- **Color schemes:** `Sequential`, `Diverging`, `Categorical`, `Random`, `Monochrome`, `Custom`.
- **Drill-down navigation:** click a node to descend into its subtree; navigate back to root.
- **Display toggles:** show/hide labels, values, percentages, and icons per rectangle.
- **Interaction:** hover tooltips, selection, mouse wheel zoom, panning, animation between states.
- **Rich callbacks:** drill-down/up, select/deselect, hover, double-click, right-click.

## Header Include

```cpp
#include "Plugins/Diagrams/UltraCanvasTreeMapElement.h"
```

## Class Reference

### Constructor

```cpp
UltraCanvasTreeMapElement(const std::string& id,
                          int x, int y, int width, int height);
```

The constructor enables tooltips, selection, zoom, and pan by default and creates an empty root node.

### Data Management

```cpp
void SetRootNode(std::shared_ptr<TreeMapNode> root);
std::shared_ptr<TreeMapNode> GetRootNode() const;
std::shared_ptr<TreeMapNode> GetCurrentNode() const;

// Populate from a flat (category, item, value, secondaryValue) tuple list.
void SetDataFromHierarchy(
        const std::vector<std::tuple<std::string, std::string,
                                     double, double>>& data);
```

### Layout & Style Configuration

```cpp
void SetLayoutAlgorithm(TreeMapAlgorithm algo);
void SetVisualStyle(TreeMapStyle style);
void SetColorScheme(TreeMapColorScheme scheme);
void SetColorPalette(const std::vector<Color>& palette);

void SetBorderProperties(double width, const Color& color);
void SetPadding(double pad);
void SetMinimumRectangleSize(double minSize);

void SetTextProperties(const std::string& font, double size,
                       double titleSize, bool autoScale);
void SetDisplayOptions(bool labels, bool values,
                       bool percentages, bool icons);

void SetBackgroundColor(const Color& color);
void SetAnimationEnabled(bool enabled);
```

### Navigation

```cpp
bool DrillDown(std::shared_ptr<TreeMapNode> node);
bool DrillUp();
void ResetToRoot();
std::vector<std::string> GetNavigationPath() const;

void SetInteractionOptions(bool drillDown, bool zoom, bool pan);
```

### Selection & Search

```cpp
void SelectNode(std::shared_ptr<TreeMapNode> node);
void ClearSelection();
std::shared_ptr<TreeMapNode> GetSelectedNode() const;

std::shared_ptr<TreeMapNode>
        FindNodeByName(const std::string& name) const;
std::vector<std::shared_ptr<TreeMapNode>>
        FindNodesByValue(double minValue, double maxValue) const;
void HighlightNode(std::shared_ptr<TreeMapNode> node);
```

### Event Callbacks

```cpp
std::function<void(const std::string&)>                     onNodeDrillDown;
std::function<void(const std::string&)>                     onNodeDrillUp;
std::function<void()>                                       onNodeReset;
std::function<void(const std::string&, double)>             onNodeSelect;
std::function<void()>                                       onNodeDeselect;
std::function<void(const std::string&, const Point2Di&)>    onNodeHover;
std::function<void(const std::string&, double, double)>     onNodeDoubleClick;
std::function<void(const std::string&)>                     onNodeRightClick;
```

### Factory Function

```cpp
inline std::shared_ptr<UltraCanvasTreeMapElement> CreateTreeMap(
        const std::string& id, int x, int y, int width, int height);
```

## Data Structures

### TreeMapNode

```cpp
struct TreeMapNode {
    std::string name;
    std::string description;
    double value;
    double secondaryValue;
    Color backgroundColor;
    Color textColor;
    std::string iconPath;
    std::string backgroundImagePath;
    bool hasBackgroundImage;
    bool isTransparent;

    std::vector<std::shared_ptr<TreeMapNode>> children;
    TreeMapNode* parent;

    std::vector<std::string> displayTexts;
    std::vector<std::string> iconPaths;
    Rect2Df bounds;
    bool isLeaf;
    int  depth;
    bool isVisible;

    void   AddChild(std::shared_ptr<TreeMapNode> child);
    double GetTotalValue() const; // Sum of leaves under this node
};
```

### Enumerations

```cpp
enum class TreeMapAlgorithm {
    Squarified, SliceAndDice, Strip, Spiral, Binary
};

enum class TreeMapStyle {
    Flat, Raised, Sunken, Gradient, Textured, Minimal
};

enum class TreeMapColorScheme {
    Sequential, Diverging, Categorical, Random, Monochrome, Custom
};
```

## Usage Examples

### Basic Hierarchical Treemap

Drawn from `Apps/DemoApp/UltraCanvasTreeMapExamples.cpp`:

```cpp
auto container = std::make_shared<UltraCanvasContainer>(
        "treeMapContainer", 0, 0, 800, 700);

auto treeMap = std::make_shared<UltraCanvasTreeMapElement>(
        "treeMap", 100, 120, 600, 400);

// (Category, Item, primary value, secondary value)
std::vector<std::tuple<std::string, std::string, double, double>> data = {
    {"Technology",  "Computers",   45000, 500},
    {"Technology",  "Phones",      32000, 400},
    {"Technology",  "Tablets",     18000, 200},
    {"Food",        "Fruits",      25000, 300},
    {"Food",        "Vegetables",  18000, 250},
    {"Food",        "Snacks",      12000, 150},
    {"Clothing",    "Men",         22000, 280},
    {"Clothing",    "Women",       28000, 350},
    {"Clothing",    "Kids",        12000, 150},
    {"Electronics", "TVs",         35000, 450},
    {"Electronics", "Audio",       22000, 280},
    {"Electronics", "Gaming",      28000, 360}
};

treeMap->SetDataFromHierarchy(data);
treeMap->SetColorScheme(TreeMapColorScheme::Categorical);

container->AddChild(treeMap);
```

### Cycling Layout Algorithms

```cpp
std::vector<TreeMapAlgorithm> algorithms = {
    TreeMapAlgorithm::Squarified,
    TreeMapAlgorithm::SliceAndDice,
    TreeMapAlgorithm::Strip,
    TreeMapAlgorithm::Binary,
    TreeMapAlgorithm::Spiral
};

static int currentAlgorithm = 0;

btnCycleAlgorithm->SetOnClick([treeMap, algorithms]() {
    currentAlgorithm = (currentAlgorithm + 1) % algorithms.size();
    treeMap->SetLayoutAlgorithm(algorithms[currentAlgorithm]);
});
```

### Cycling Visual Styles

```cpp
std::vector<TreeMapStyle> styles = {
    TreeMapStyle::Flat,
    TreeMapStyle::Raised,
    TreeMapStyle::Sunken,
    TreeMapStyle::Gradient,
    TreeMapStyle::Minimal
};

static int currentStyle = 0;

btnCycleStyle->SetOnClick([treeMap, styles]() {
    currentStyle = (currentStyle + 1) % styles.size();
    treeMap->SetVisualStyle(styles[currentStyle]);
});
```

### Cycling Color Schemes

```cpp
std::vector<TreeMapColorScheme> colorSchemes = {
    TreeMapColorScheme::Categorical,
    TreeMapColorScheme::Sequential,
    TreeMapColorScheme::Diverging,
    TreeMapColorScheme::Monochrome
};

static int currentColor = 0;

btnCycleColor->SetOnClick([treeMap, colorSchemes]() {
    currentColor = (currentColor + 1) % colorSchemes.size();
    treeMap->SetColorScheme(colorSchemes[currentColor]);
});
```

### Toggling Labels and Values

`SetDisplayOptions(labels, values, percentages, icons)` is the single switch for what's rendered inside each rectangle:

```cpp
static bool showValues = true;
static bool showLabels = true;

btnToggleValues->SetOnClick([treeMap]() {
    showValues = !showValues;
    treeMap->SetDisplayOptions(showLabels, showValues, false, true);
});

btnToggleLabels->SetOnClick([treeMap]() {
    showLabels = !showLabels;
    treeMap->SetDisplayOptions(showLabels, showValues, false, true);
});
```

### Reset to Default Configuration

```cpp
btnReset->SetOnClick([treeMap]() {
    std::vector<std::tuple<std::string, std::string, double, double>> data = {
        {"Technology",  "Computers", 45000, 500},
        {"Technology",  "Phones",    32000, 400},
        // ... (same dataset as above) ...
        {"Electronics", "Gaming",    28000, 360}
    };

    treeMap->SetDataFromHierarchy(data);
    treeMap->SetLayoutAlgorithm(TreeMapAlgorithm::Squarified);
    treeMap->SetVisualStyle(TreeMapStyle::Flat);
    treeMap->SetColorScheme(TreeMapColorScheme::Categorical);
    treeMap->SetDisplayOptions(true, true, false, true);
});
```

## Tiling Algorithms

| Algorithm     | Behavior                                                                 |
|---------------|--------------------------------------------------------------------------|
| `Squarified`  | Greedy algorithm minimizing aspect ratios — best for "balanced" tiles.   |
| `SliceAndDice`| Alternates horizontal and vertical strips by depth.                      |
| `Strip`       | Lays out items in horizontal strips of roughly even total value.         |
| `Spiral`      | Spirals outward from one corner.                                         |
| `Binary`      | Recursive binary subdivision balancing left/right halves' value.         |

## See Also

- [UltraCanvasSankeyExamples](UltraCanvasSankeyExamples.md) — flow diagrams
- [UltraCanvasVennDiagramExamples](UltraCanvasVennDiagramExamples.md) — set intersections
- [UltraCanvasDendrogramExamples](UltraCanvasDendrogramExamples.md) — hierarchical tree visualization
- [UltraCanvasFlowChartExamples](UltraCanvasFlowChartExamples.md) — interactive flow chart editor
- [UltraCanvasLineChartElement](UltraCanvasLineChartElement.md) — line charts
- [UltraCanvasTreeView](UltraCanvasTreeViewExamples.md) — alternative hierarchical view
- [UltraCanvasChartElementBase](UltraCanvasChartElementBase.md) — common chart base
