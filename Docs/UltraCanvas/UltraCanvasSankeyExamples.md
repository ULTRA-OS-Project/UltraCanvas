# UltraCanvasSankeyDiagram Documentation

**Version:** 1.3.0
**Last Modified:** 2025-10-16
**Author:** UltraCanvas Framework

## Overview

`UltraCanvasSankeyDiagram` is an interactive Sankey diagram element within the UltraCanvas framework for visualizing flow/value distributions between entities. Nodes are arranged into columns by depth, and weighted links are rendered as curved ribbons whose thickness encodes the flow value. The component supports drag-to-reposition, hover tooltips, theme presets, CSV loading, SVG export, and manual node ordering.

**Namespace:** `UltraCanvas`
**Header:** `include/Plugins/Diagrams/UltraCanvasSankey.h`
**Implementation:** `Plugins/Diagrams/UltraCanvasSankey.cpp`
**Base Class:** `UltraCanvasUIElement`

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasSankeyDiagram
```

## Features

- **Node + link model:** Add named nodes and weighted links between them; layout is automatic.
- **Themes:** Preset color palettes for `Default`, `Energy`, `Finance`, `WebTraffic`, `Custom`.
- **Alignment modes:** `Left`, `Right`, `Center`, `Justify` distribute columns differently.
- **Curvature control:** `SetLinkCurvature(float)` adjusts ribbon bending (0 = straight, 1 = full S-curve).
- **Manual ordering:** Override automatic vertical ordering of nodes within a column.
- **Interaction:** Drag nodes to reposition, hover for tooltips, click for callbacks.
- **I/O:** Load flow data from CSV, save the rendered diagram to SVG.

## Header Include

```cpp
#include "Plugins/Diagrams/UltraCanvasSankey.h"
```

## Class Reference

### Constructor

```cpp
UltraCanvasSankeyDiagram(const std::string& id, long x, long y, long w, long h);
```

Creates a Sankey diagram element at the given position and size.

### Node Management

```cpp
void AddNode(const std::string& id,
             const std::string& label = "",
             const Color& tgtColor = Colors::Transparent);
void RemoveNode(const std::string& id);
```

`AddNode` registers a node (auto-created on first `AddLink` too); `label` overrides the displayed text and `tgtColor` overrides the theme color.

### Link Management

```cpp
void AddLink(const std::string& source,
             const std::string& target,
             float value,
             const Color& tgtColor = Colors::Transparent);
void RemoveLink(const std::string& source, const std::string& target);
void ClearAll();
```

### Data I/O

```cpp
bool LoadFromCSV(const std::string& filePath);
bool SaveToSVG(const std::string& filePath);
```

### Layout Configuration

```cpp
void SetAlignment(SankeyAlignment align);
void SetTheme(SankeyTheme t);
void SetNodeWidth(float width);
void SetNodePadding(float padding);
void SetLinkCurvature(float curvature);
void SetIterations(int iter);
void SetFontSize(float size);
void SetFontFamily(const std::string& family);
void SetMaxLabelWidth(float width);
float GetMaxLabelWidth() const;

void SetManualOrderMode(bool enabled);
bool GetManualOrderMode() const;
void SetNodeOrdering(const std::string& nodeId, int ordering);
int  GetNodeOrdering(const std::string& nodeId) const;

void PerformLayout();
```

When `SetManualOrderMode(true)` is active, nodes within a column are sorted by the integer value passed to `SetNodeOrdering` (ascending).

### Event Callbacks

```cpp
std::function<void(const std::string&)> onNodeClick;
std::function<void(const std::string&, const std::string&)> onLinkClick;
std::function<void(const std::string&)> onNodeHover;
std::function<void(const std::string&, const std::string&)> onLinkHover;
```

### Factory Function

```cpp
inline std::shared_ptr<UltraCanvasSankeyDiagram> CreateSankeyRenderer(
        const std::string& id, long x, long y, long w, long h);
```

## Data Structures

### SankeyNode

```cpp
struct SankeyNode {
    std::string id;
    std::string label;
    float x = 0.0f, y = 0.0f;
    float width = 0.0f, height = 0.0f;
    float value = 0.0f;
    int   depth = 0;
    int   ordering = 0;
    Color color = Colors::Blue;
    bool  isDragging = false;
    std::vector<std::string> sourceLinks;
    std::vector<std::string> targetLinks;
};
```

### SankeyLink

```cpp
struct SankeyLink {
    std::string source;
    std::string target;
    float value = 0.0f;
    float sourceY = 0.0f, targetY = 0.0f;
    float sourceWidth = 0.0f, targetWidth = 0.0f;
    Color color = Colors::LightBlue;
    float opacity = 0.7f;
};
```

### Enumerations

```cpp
enum class SankeyAlignment { Left, Right, Center, Justify };
enum class SankeyTheme     { Default, Energy, Finance, WebTraffic, Custom };
```

## Usage Examples

### Energy Flow Example

Drawn from `Apps/DemoApp/UltraCanvasSankeyExamples.cpp` (`GenerateEnergySankeyData`):

```cpp
void GenerateEnergySankeyData(UltraCanvasSankeyDiagram* renderer) {
    renderer->AddLink("Coal",        "Electricity", 35.0f);
    renderer->AddLink("Natural Gas", "Electricity", 35.0f);
    renderer->AddLink("Nuclear",     "Electricity", 10.0f);
    renderer->AddLink("Solar",       "Electricity", 3.0f);
    renderer->AddLink("Wind",        "Electricity", 2.0f);
    renderer->AddLink("Hydro",       "Electricity", 5.0f);

    renderer->AddLink("Electricity", "Residential",  40.0f);
    renderer->AddLink("Electricity", "Commercial",   35.0f);
    renderer->AddLink("Electricity", "Industrial",   45.0f);

    renderer->AddLink("Natural Gas", "Residential Heating", 15.0f);
    renderer->AddLink("Natural Gas", "Commercial Heating",  10.0f);

    renderer->SetTheme(SankeyTheme::Energy);
}
```

### Hosting the Diagram in a Container

```cpp
auto energySankey = std::make_shared<UltraCanvasSankeyDiagram>(
        "EnergySankey", 10, 110, 950, 520);

GenerateEnergySankeyData(energySankey.get());

energySankey->onNodeClick = [statusLabel](const std::string& nodeId) {
    statusLabel->SetText("Clicked node: " + nodeId);
};

energySankey->onNodeHover = [statusLabel](const std::string& nodeId) {
    statusLabel->SetText("Hovering over: " + nodeId + "\nDrag to reposition");
};

energyContainer->AddChild(energySankey);
```

### Live Configuration via Controls

```cpp
themeDropdown->onSelectionChanged = [energySankey](int index, const DropdownItem&) {
    energySankey->SetTheme(static_cast<SankeyTheme>(index));
};

alignDropdown->onSelectionChanged = [energySankey](int index, const DropdownItem&) {
    energySankey->SetAlignment(static_cast<SankeyAlignment>(index));
};

curveSlider->onValueChanged = [energySankey, curveValue](float value) {
    energySankey->SetLinkCurvature(value);
    std::stringstream ss; ss << std::fixed << value;
    curveValue->SetText(ss.str());
};
```

### Financial Flow Example

```cpp
void GenerateFinanceSankeyData(UltraCanvasSankeyDiagram* renderer) {
    renderer->AddLink("Revenue",       "Product Sales",  65.0f);
    renderer->AddLink("Revenue",       "Services",       35.0f);

    renderer->AddLink("Product Sales", "Profit",         20.0f);
    renderer->AddLink("Product Sales", "Manufacturing",  30.0f);
    renderer->AddLink("Product Sales", "Marketing",      15.0f);

    renderer->AddLink("Services",      "Profit",         15.0f);
    renderer->AddLink("Services",      "Operations",     10.0f);
    renderer->AddLink("Services",      "Support",        10.0f);

    renderer->AddLink("Profit",        "Dividends",      15.0f);
    renderer->AddLink("Profit",        "R&D",            10.0f);
    renderer->AddLink("Profit",        "Reserves",       10.0f);

    renderer->SetTheme(SankeyTheme::Finance);
}
```

### Web Traffic Flow Example

```cpp
void GenerateWebTrafficSankeyData(UltraCanvasSankeyDiagram* renderer) {
    renderer->ClearAll();

    renderer->AddLink("Search",       "Homepage", 30.0f);
    renderer->AddLink("Social Media", "Homepage", 30.0f);
    renderer->AddLink("Direct",       "Homepage", 25.0f);
    renderer->AddLink("Referral",     "Homepage", 25.0f);

    renderer->AddLink("Homepage",     "Product Page", 50.0f);
    renderer->AddLink("Homepage",     "About",        20.0f);
    renderer->AddLink("Homepage",     "Blog",         20.0f);
    renderer->AddLink("Homepage",     "Exit",         20.0f);

    renderer->AddLink("Product Page", "Checkout",     30.0f);
    renderer->AddLink("Product Page", "Exit",         20.0f);

    renderer->AddLink("Checkout",     "Purchase",     25.0f);
    renderer->AddLink("Checkout",     "Exit",          5.0f);

    renderer->SetTheme(SankeyTheme::WebTraffic);
}
```

### Manual Column Ordering and Custom Theme

The Oil Sales 2024 example uses `SankeyTheme::Custom`, justified alignment, and explicit per-node ordering so the regional column appears in a deliberate sequence:

```cpp
sankey->ClearAll();
sankey->SetTheme(SankeyTheme::Custom);
sankey->SetAlignment(SankeyAlignment::Justify);
sankey->SetNodeWidth(20.0f);
sankey->SetNodePadding(12.0f);
sankey->SetLinkCurvature(0.5f);
sankey->SetFontSize(13.0f);
sankey->SetMaxLabelWidth(250.0f);
sankey->SetManualOrderMode(true);

sankey->AddNode("World", "World\nOil Production\n(104.7 mb/d)",
                Color(147, 180, 220));

sankey->AddLink("World", "North America",   24.5f, Color(255, 182, 193));
sankey->AddLink("World", "Europe + CIS",    18.5f, Color(186, 225, 255));
sankey->AddLink("World", "Middle East",      8.7f, Color(255, 204, 153));
sankey->AddLink("World", "China",           16.4f, Color(255, 153, 153));
sankey->AddLink("World", "India",            5.55f, Color(204, 229, 255));
// ... more regions ...

sankey->SetNodeOrdering("North America", 10);
sankey->SetNodeOrdering("Europe + CIS",  30);
sankey->SetNodeOrdering("Middle East",   40);
sankey->SetNodeOrdering("China",         50);
sankey->SetNodeOrdering("India",         60);
```

### Custom Builder Tab (Add Link at Runtime)

```cpp
addLinkBtn->onClick = [customSankey, sourceInput, targetInput, valueInput, statusLabel]() {
    std::string source = sourceInput->GetText();
    std::string target = targetInput->GetText();
    std::string valueStr = valueInput->GetText();
    if (source.empty() || target.empty() || valueStr.empty()) return;
    try {
        float value = std::stof(valueStr);
        customSankey->AddLink(source, target, value);
        statusLabel->SetText("Added link: " + source + " -> " + target);
    } catch (...) {
        statusLabel->SetText("Invalid value entered!");
    }
};

clearBtn->onClick = [customSankey]() {
    customSankey->ClearAll();
};
```

### Performance Test (Random Diagram)

```cpp
generateBtn->onClick = [perfSankey, nodesSlider, linksSlider]() {
    perfSankey->ClearAll();

    int nodeCount = static_cast<int>(nodesSlider->GetValue());
    int linkCount = static_cast<int>(linksSlider->GetValue());

    std::vector<std::string> nodeNames;
    for (int i = 0; i < nodeCount; ++i)
        nodeNames.push_back("Node_" + std::to_string(i));

    std::random_device rd; std::mt19937 gen(rd());
    std::uniform_int_distribution<>  nodeDist(0, nodeCount - 1);
    std::uniform_real_distribution<> valueDist(10.0, 200.0);

    for (int i = 0; i < linkCount; ++i) {
        int s = nodeDist(gen), t = nodeDist(gen);
        if (s == t) continue;
        if (s > t) std::swap(s, t);
        perfSankey->AddLink(nodeNames[s], nodeNames[t], (float)valueDist(gen));
    }
};
```

## See Also

- [UltraCanvasVennDiagramExamples](UltraCanvasVennDiagramExamples.md) — set intersection visualization
- [UltraCanvasTreeMapExamples](UltraCanvasTreeMapExamples.md) — hierarchical area visualization
- [UltraCanvasFlowChartExamples](UltraCanvasFlowChartExamples.md) — node/edge flow chart editor
- [UltraCanvasLineChartElement](UltraCanvasLineChartElement.md) — line/curve charts
- [UltraCanvasUIElement](UltraCanvasUIElement.md) — base element class
- [UltraCanvasTabbedContainer](UltraCanvasTabbedContainer.md) — used by the demo to host examples
