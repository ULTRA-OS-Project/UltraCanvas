# UltraCanvasVennDiagramElement Documentation

**Version:** 2.0.0
**Last Modified:** 2026-07-13
**Author:** UltraCanvas Framework

## Overview

`UltraCanvasVennDiagramElement` renders interactive Venn / Euler diagrams with two to five sets. Each set owns a collection of items, and items can additionally be assigned to specific intersections of sets. The element handles automatic layout for the common 2/3/4/5-set arrangements plus a **nested containment layout** (the LaTeX set-hierarchy style), draws sets either as **circles or rounded rectangles**, supports multiple visual styles (classic outlines, modern, minimal, filled), and exposes hover/click callbacks for both individual sets and computed regions.

Set labels are centred on their shape and automatically placed above shapes in the upper half of the diagram and below shapes in the lower half, so labels never pile up in the busy overlap region at the centre.

**Namespace:** `UltraCanvas`
**Header:** `include/Plugins/Diagrams/UltraCanvasVennDiagram.h`
**Implementation:** `Plugins/Diagrams/UltraCanvasVennDiagram.cpp`
**Base Class:** `UltraCanvasChartElementBase`

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasChartElementBase
            └── UltraCanvasVennDiagramElement
```

## Features

- **Preset layouts:** `TwoCircles`, `ThreeCircles`, `FourCircles`, `FiveCircles`, `Nested`, `Custom`.
- **Set shapes:** `Circle` (classic overlapping circles) or `RoundedRectangle` (LaTeX / set-theory boxes), selectable at runtime with `SetShape`.
- **Nested containment layout:** `VennLayout::Nested` draws each set fully inside the previous one — ideal for subset hierarchies such as *Group ⊃ Abelian Group ⊃ Ring ⊃ Field*. Reads best with the `RoundedRectangle` shape.
- **Styles:** `Classic`, `Modern`, `Minimal`, `Filled`, `Outlined`.
- **Item membership:** items per set plus items per intersection (any subset of set indices).
- **Region computation:** the element automatically derives the visible regions from the sets' geometry.
- **Interaction:** zoom, pan, drag-to-move sets, hover and click callbacks (hit-testing follows the active shape).
- **Configurable visuals:** show/hide set labels, item counts, region labels, font size, corner radius (box mode), background color, and a custom color palette.

## Header Include

```cpp
#include "Plugins/Diagrams/UltraCanvasVennDiagram.h"
```

## Class Reference

### Constructor

```cpp
UltraCanvasVennDiagramElement(const std::string& id,
                              int x, int y, int width, int height);
```

The constructor enables zoom, pan, selection, and tooltips by default, and applies the `ThreeCircles` layout.

### Circle Management

```cpp
void AddCircle(const std::string& label, const Color& color = Color());
void RemoveCircle(size_t index);
void ClearCircles();

VennCircle&       GetCircle(size_t index);
const VennCircle& GetCircle(size_t index) const;
size_t            GetCircleCount() const;
```

### Item Management

```cpp
void AddItemToCircle(size_t circleIndex, const std::string& item);
void AddItemToIntersection(const std::vector<size_t>& circleIndices,
                           const std::string& item);
```

`AddItemToIntersection` accepts any non-empty subset of circle indices. For three circles, `{0, 1}` is a pairwise overlap and `{0, 1, 2}` is the triple overlap.

### Layout Management

```cpp
void SetLayout(VennLayout newLayout);
VennLayout GetLayout() const;

void SetCirclePosition(size_t index, double x, double y);
void SetCircleRadius(size_t index, double radius);
void ApplyLayout();
```

### Style Management

```cpp
void SetStyle(VennStyle newStyle);
VennStyle GetStyle() const;

void SetShape(VennShape newShape);   // Circle or RoundedRectangle
VennShape GetShape() const;
void SetCornerRadius(double radius); // Corner radius used in RoundedRectangle mode

void SetShowLabels(bool show);
void SetShowItemCounts(bool show);
void SetShowRegionLabels(bool show);
void SetFontSize(double size);
void SetBackgroundColor(const Color& color);
void SetColorPalette(const std::vector<Color>& palette);
```

### Event Callbacks

```cpp
void SetOnCircleHover(std::function<void(size_t, const VennCircle&)> callback);
void SetOnRegionHover(std::function<void(size_t, const VennRegion&)> callback);
void SetOnCircleClick(std::function<void(size_t, const VennCircle&)> callback);
void SetOnRegionClick(std::function<void(size_t, const VennRegion&)> callback);
```

### Factory Function

```cpp
inline std::shared_ptr<UltraCanvasVennDiagramElement> CreateVennDiagram(
        const std::string& id, int x, int y, int width, int height);
```

## Data Structures

### VennCircle

```cpp
struct VennCircle {
    std::string label;
    Point2Dd center;
    float radius;
    float width  = 0.0f;   // Rectangle extents (RoundedRectangle mode);
    float height = 0.0f;   // 0 → derived from radius
    Color fillColor;
    Color borderColor;
    float borderWidth = 2.0f;
    float alpha = 0.6f;
    std::unordered_set<std::string> items;

    VennCircle(const std::string& circleLabel, float x, float y, float r,
               const Color& color);

    bool     Contains(const Point2Dd& point) const;      // circle hit-test
    bool     ContainsRect(const Point2Dd& point) const;  // rectangle hit-test
    Rect2Dd  GetRect() const;                            // centre-based box
    float    RectWidth() const;
    float    RectHeight() const;
    void     AddItem(const std::string& item);
    void     RemoveItem(const std::string& item);
};
```

### VennRegion

```cpp
struct VennRegion {
    std::vector<size_t> circleIndices; // Which circles this region belongs to
    std::unordered_set<std::string> items;
    Point2Df labelPosition;
    Color textColor = Colors::Black;
    bool isVisible = true;

    std::string GetRegionName(const std::vector<VennCircle>& circles) const;
};
```

### Enumerations

```cpp
enum class VennLayout {
    TwoCircles, ThreeCircles, FourCircles, FiveCircles, Nested, Custom
};

enum class VennStyle {
    Classic, Modern, Minimal, Filled, Outlined
};

enum class VennShape {
    Circle,             // Classic overlapping circles
    RoundedRectangle    // Rectangular (LaTeX / set-theory) style boxes
};
```

`VennCircle` additionally carries optional `width` / `height` fields used when the
diagram is drawn with `RoundedRectangle`; when left at `0` they are derived from
`radius`, so circle-only setups need no extra bookkeeping.

## Usage Examples

### Two-Circle Venn (Cats vs Dogs)

Drawn from `Apps/DemoApp/UltraCanvasVennDiagramExamples.cpp`:

```cpp
auto twoCirclesVenn = std::make_shared<UltraCanvasVennDiagramElement>(
        "twoCirclesVenn", 80, 100, 500, 400);

twoCirclesVenn->ClearCircles();
twoCirclesVenn->AddCircle("Cats", Color(255, 99, 132, 180));
twoCirclesVenn->AddCircle("Dogs", Color( 54, 162, 235, 180));

twoCirclesVenn->AddItemToCircle(0, "Independent");
twoCirclesVenn->AddItemToCircle(0, "Low maintenance");
twoCirclesVenn->AddItemToCircle(0, "Litter trained");
twoCirclesVenn->AddItemToCircle(1, "Loyal companion");
twoCirclesVenn->AddItemToCircle(1, "Needs walks");
twoCirclesVenn->AddItemToCircle(1, "Protective");

twoCirclesVenn->AddItemToIntersection({0, 1}, "Furry");
twoCirclesVenn->AddItemToIntersection({0, 1}, "Companionship");
twoCirclesVenn->AddItemToIntersection({0, 1}, "Playful");

twoCirclesVenn->SetLayout(VennLayout::TwoCircles);
twoCirclesVenn->SetStyle(VennStyle::Classic);
twoCirclesVenn->SetShowLabels(true);
twoCirclesVenn->SetShowItemCounts(true);
```

### Three-Circle Venn (Developer Skills)

```cpp
auto threeCirclesVenn = std::make_shared<UltraCanvasVennDiagramElement>(
        "threeCirclesVenn", 80, 100, 500, 400);

threeCirclesVenn->ClearCircles();
threeCirclesVenn->AddCircle("Frontend", Color( 54, 162, 235, 180));
threeCirclesVenn->AddCircle("Backend",  Color(255,  99, 132, 180));
threeCirclesVenn->AddCircle("DevOps",   Color(255, 205,  86, 180));

threeCirclesVenn->AddItemToCircle(0, "React");
threeCirclesVenn->AddItemToCircle(0, "Vue.js");
threeCirclesVenn->AddItemToCircle(0, "CSS/SCSS");

threeCirclesVenn->AddItemToCircle(1, "Node.js");
threeCirclesVenn->AddItemToCircle(1, "Python");
threeCirclesVenn->AddItemToCircle(1, "SQL");

threeCirclesVenn->AddItemToCircle(2, "Docker");
threeCirclesVenn->AddItemToCircle(2, "Kubernetes");
threeCirclesVenn->AddItemToCircle(2, "AWS");

threeCirclesVenn->AddItemToIntersection({0, 1},    "TypeScript");
threeCirclesVenn->AddItemToIntersection({0, 1},    "GraphQL");
threeCirclesVenn->AddItemToIntersection({1, 2},    "Monitoring");
threeCirclesVenn->AddItemToIntersection({1, 2},    "Logging");
threeCirclesVenn->AddItemToIntersection({0, 2},    "Performance");
threeCirclesVenn->AddItemToIntersection({0, 1, 2}, "Git");
threeCirclesVenn->AddItemToIntersection({0, 1, 2}, "Testing");

threeCirclesVenn->SetLayout(VennLayout::ThreeCircles);
threeCirclesVenn->SetStyle(VennStyle::Classic);
threeCirclesVenn->SetShowLabels(true);
threeCirclesVenn->SetShowItemCounts(true);
```

### Four-Circle Venn (SDLC)

```cpp
auto fourCirclesVenn = std::make_shared<UltraCanvasVennDiagramElement>(
        "fourCirclesVenn", 80, 100, 500, 400);

fourCirclesVenn->ClearCircles();
fourCirclesVenn->AddCircle("Planning",    Color( 75, 192, 192, 180));
fourCirclesVenn->AddCircle("Development", Color(255, 159,  64, 180));
fourCirclesVenn->AddCircle("Testing",     Color(153, 102, 255, 180));
fourCirclesVenn->AddCircle("Deployment",  Color(255,  99, 132, 180));

fourCirclesVenn->AddItemToCircle(0, "Requirements");
fourCirclesVenn->AddItemToCircle(0, "User Stories");
fourCirclesVenn->AddItemToCircle(1, "Coding");
fourCirclesVenn->AddItemToCircle(1, "Code Review");
fourCirclesVenn->AddItemToCircle(2, "Unit Tests");
fourCirclesVenn->AddItemToCircle(2, "Integration Tests");
fourCirclesVenn->AddItemToCircle(3, "Release Notes");
fourCirclesVenn->AddItemToCircle(3, "Production");

fourCirclesVenn->AddItemToIntersection({0, 1}, "Design Docs");
fourCirclesVenn->AddItemToIntersection({1, 2}, "TDD");
fourCirclesVenn->AddItemToIntersection({2, 3}, "Staging");

fourCirclesVenn->SetLayout(VennLayout::FourCircles);
fourCirclesVenn->SetStyle(VennStyle::Classic);
fourCirclesVenn->SetShowLabels(true);
fourCirclesVenn->SetShowItemCounts(true);
```

### Set Hierarchy (Rectangular / Nested, LaTeX style)

The `Nested` layout combined with the `RoundedRectangle` shape reproduces the
classic set-theory containment diagram, where each set is drawn fully inside its
superset and labelled at the top-left of its box:

```cpp
auto hierarchyVenn = std::make_shared<UltraCanvasVennDiagramElement>(
        "hierarchyVenn", 60, 90, 540, 420);

hierarchyVenn->ClearCircles();
hierarchyVenn->AddCircle("Group",         Color(210, 244, 180, 200));
hierarchyVenn->AddCircle("Abelian Group", Color(248, 200, 210, 210));
hierarchyVenn->AddCircle("Ring",          Color(190, 200, 255, 210));
hierarchyVenn->AddCircle("Field",         Color(255, 205, 205, 220));

hierarchyVenn->AddItemToCircle(0, "GL(n)");
hierarchyVenn->AddItemToCircle(0, "O(n)");
hierarchyVenn->AddItemToCircle(1, "(Z, +)");
hierarchyVenn->AddItemToCircle(2, "(Z, +, *)");
hierarchyVenn->AddItemToCircle(3, "(Q, +, *)");
hierarchyVenn->AddItemToCircle(3, "(R, +, *)");
hierarchyVenn->AddItemToCircle(3, "(C, +, *)");

hierarchyVenn->SetShape(VennShape::RoundedRectangle);
hierarchyVenn->SetLayout(VennLayout::Nested);
hierarchyVenn->SetStyle(VennStyle::Filled);
hierarchyVenn->SetShowLabels(true);
hierarchyVenn->SetShowItemCounts(true);
```

### Toggling Shape at Runtime

```cpp
shapeSelector->onSelectionChanged =
    [allVenns](int, const DropdownItem& item) {
        VennShape shape = (item.value == "rect") ? VennShape::RoundedRectangle
                                                 : VennShape::Circle;
        for (auto& v : allVenns) v->SetShape(shape);
    };
```

### Switching Style at Runtime

```cpp
styleSelector->onSelectionChanged =
    [twoCirclesVenn, threeCirclesVenn, fourCirclesVenn,
     cloudVenn, customVenn]
    (int index, const DropdownItem& item) {
        VennStyle style = VennStyle::Classic;
        if      (item.value == "modern")  style = VennStyle::Modern;
        else if (item.value == "minimal") style = VennStyle::Minimal;
        else if (item.value == "filled")  style = VennStyle::Filled;

        twoCirclesVenn  ->SetStyle(style);
        threeCirclesVenn->SetStyle(style);
        fourCirclesVenn ->SetStyle(style);
        cloudVenn       ->SetStyle(style);
        customVenn      ->SetStyle(style);
    };
```

### Hover Callback Wiring

```cpp
threeCirclesVenn->SetOnCircleHover(
    [statusBar](size_t idx, const VennCircle& circle) {
        statusBar->SetText("Hovering: " + circle.label +
                           " (" + std::to_string(circle.items.size()) + " items)");
    });
```

### Cloud Platform Comparison (Custom Item Distribution)

```cpp
auto cloudVenn = std::make_shared<UltraCanvasVennDiagramElement>(
        "cloudVenn", 80, 100, 500, 400);

cloudVenn->ClearCircles();
cloudVenn->AddCircle("AWS",   Color(255, 153,  51, 180));
cloudVenn->AddCircle("Azure", Color(  0, 120, 215, 180));
cloudVenn->AddCircle("GCP",   Color( 66, 133, 244, 180));

cloudVenn->AddItemToCircle(0, "EC2");
cloudVenn->AddItemToCircle(0, "S3");
cloudVenn->AddItemToCircle(0, "Lambda");
cloudVenn->AddItemToCircle(1, "VMs");
cloudVenn->AddItemToCircle(1, "Blob Storage");
cloudVenn->AddItemToCircle(1, "Functions");
cloudVenn->AddItemToCircle(2, "Compute Engine");
cloudVenn->AddItemToCircle(2, "Cloud Storage");
cloudVenn->AddItemToCircle(2, "Cloud Functions");

cloudVenn->AddItemToIntersection({0, 1},    "Container Registry");
cloudVenn->AddItemToIntersection({1, 2},    "Kubernetes");
cloudVenn->AddItemToIntersection({0, 2},    "Data Lake");
cloudVenn->AddItemToIntersection({0, 1, 2}, "Compute");
cloudVenn->AddItemToIntersection({0, 1, 2}, "Storage");

cloudVenn->SetLayout(VennLayout::ThreeCircles);
cloudVenn->SetStyle(VennStyle::Classic);
```

## See Also

- [UltraCanvasSankeyExamples](UltraCanvasSankeyExamples.md) — flow diagrams
- [UltraCanvasTreeMapExamples](UltraCanvasTreeMapExamples.md) — hierarchical rectangular charts
- [UltraCanvasFlowChartExamples](UltraCanvasFlowChartExamples.md) — node/edge editors
- [UltraCanvasDendrogramExamples](UltraCanvasDendrogramExamples.md) — hierarchical clustering trees
- [UltraCanvasLineChartElement](UltraCanvasLineChartElement.md) — line charts
- [UltraCanvasChartElementBase](UltraCanvasChartElementBase.md) — common chart base
