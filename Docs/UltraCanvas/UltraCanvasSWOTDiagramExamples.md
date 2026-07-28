# UltraCanvasSWOTDiagram Documentation

**Version:** 1.0.0
**Last Modified:** 2026-07-28
**Author:** UltraCanvas Framework

## Overview

`UltraCanvasSWOTDiagram` renders the classic four-panel SWOT analysis
infographic: four **text item lists** — Strengths, Weaknesses, Opportunities,
Threats — presented in one of six design presets. Unlike
`UltraCanvasQuadrantChart` (which plots SWOT factors as scatter points on an
internal/external vs positive/negative plane), this element is the
presentation-style SWOT diagram used in slides and reports: items are short
texts, not coordinates.

All designs render from the same data model, so the presentation can be
switched at runtime without touching the data. Items support hover
highlighting with tooltips (showing the full text when clipped), click
selection and double-click callbacks.

**Namespace:** `UltraCanvas`
**Header:** `include/Plugins/Diagrams/UltraCanvasSWOTDiagram.h`
**Implementation:** `Plugins/Diagrams/UltraCanvasSWOTDiagram.cpp`
**Base Class:** `UltraCanvasChartElementBase`

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasChartElementBase
            └── UltraCanvasSWOTDiagram
```

## Design Presets

`SWOTDesign` selects the geometry (see
`Docs/UltraCanvas/SWOTDiagramDesignVariants.md` for the survey these presets
were chosen from):

| Design | Description |
|---|---|
| `SWOTDesign::CornerBadges` | Vivid filled panels, S/W/O/T letter badges at the outer corners, central "SWOT analysis" circle (default) |
| `SWOTDesign::Matrix` | Classic contiguous 2x2 grid; optional internal/external + helpful/harmful axis captions |
| `SWOTDesign::Cards` | Four separated rounded cards with colored header bars |
| `SWOTDesign::CenterDiamond` | Central rotated square of four letter triangles, clean text blocks at the corners |
| `SWOTDesign::Rows` | Four stacked horizontal bands with big letter blocks (suits longer texts) |
| `SWOTDesign::Columns` | Four side-by-side columns with header chips (portrait layouts) |

Decorations combine freely with every design: letter badges
(`SetShowBadges`), the central element where the design has one
(`SetShowCenterElement`), item bullet markers (`SetShowItemBullets`), axis
captions on the Matrix design (`SetShowAxisCaptions`) and a light/dark theme
(`SetDarkTheme`).

## Header Include

```cpp
#include "Plugins/Diagrams/UltraCanvasSWOTDiagram.h"
```

## Data Model

```cpp
enum class SWOTQuadrant { Strengths, Weaknesses, Opportunities, Threats };

struct SWOTItem {
    std::string text;
    Color bulletColor;   // Transparent (default) = use the quadrant accent
};

struct SWOTQuadrantConfig {
    std::string title;        // "Strengths"
    std::string badgeLetter;  // "S"
    Color accentColor;        // Panel tints/badges/headers derive from it
};
```

Each quadrant holds an ordered `std::vector<SWOTItem>`. Panel background
tints, header colors, badge fills and bullet colors are all derived from the
quadrant's single `accentColor`, so recoloring a quadrant is one call.

## Class Reference

### Constructor

```cpp
UltraCanvasSWOTDiagram(const std::string& id, int x, int y, int w, int h);
```

Selection and tooltips are enabled by default. The default design is
`CornerBadges` with the light theme and the conventional colors: Strengths
green, Weaknesses red, Opportunities blue, Threats orange.

### Design & Theme

```cpp
void SetDesign(SWOTDesign d);
SWOTDesign GetDesign() const;

void SetDarkTheme(bool dark);   // Also switches the element background
bool GetDarkTheme() const;
```

### Quadrant Configuration

```cpp
void SetQuadrantConfig(SWOTQuadrant q, const SWOTQuadrantConfig& config);
const SWOTQuadrantConfig& GetQuadrantConfig(SWOTQuadrant q) const;
void SetQuadrantTitle(SWOTQuadrant q, const std::string& title);
void SetQuadrantAccentColor(SWOTQuadrant q, const Color& color);
```

The diagram title comes from the base class: `SetTitle("...")`.
The central circle's text is set with `SetCenterText("SWOT\nanalysis")`.

### Data Management

```cpp
void AddItem(SWOTQuadrant q, const std::string& text);
void AddItem(SWOTQuadrant q, const SWOTItem& item);
void SetItems(SWOTQuadrant q, const std::vector<SWOTItem>& list);
const std::vector<SWOTItem>& GetItems(SWOTQuadrant q) const;
void RemoveItem(SWOTQuadrant q, size_t index);   // Keeps selection consistent
void ClearItems(SWOTQuadrant q);
void ClearAllItems();
size_t CountItems(SWOTQuadrant q) const;
size_t CountAllItems() const;

void LoadSampleData();   // Built-in business example (4 items per quadrant)
```

Item texts wrap automatically to the panel width. When a panel cannot fit
all items, the overflow is indicated with a "+N more..." line.

### Visual Toggles

```cpp
void SetShowBadges(bool show);         // S/W/O/T letter badges (default: on)
void SetShowCenterElement(bool show);  // Central circle / diamond (default: on)
void SetShowItemBullets(bool show);    // Bullet markers (default: on)
void SetShowAxisCaptions(bool show);   // Matrix design only (default: off)
void SetCenterText(const std::string& text);
```

### Selection & Interaction

```cpp
// Inherited from UltraCanvasChartElementBase:
void SetEnableSelection(bool enable);   // Default: on
void SetEnableTooltips(bool enable);    // Default: on

SWOTItemRef GetSelectedItem() const;    // {quadrant, item}; item < 0 = none
void ClearSelection();
```

Clicking an item toggles its selection; clicking empty space clears it.
Hovering an item highlights it and shows a tooltip with the quadrant title
and the full item text.

### Event Callbacks

```cpp
std::function<void(SWOTQuadrant, size_t, const SWOTItem&)> onItemSelect;
std::function<void(SWOTQuadrant, size_t, const SWOTItem&)> onItemHover;
std::function<void(SWOTQuadrant, size_t, const SWOTItem&)> onItemDoubleClick;
std::function<void()> onSelectionChange;   // Fires after any click-driven change
```

### Factory Functions

```cpp
std::shared_ptr<UltraCanvasSWOTDiagram> CreateSWOTDiagramElement(
        const std::string& id, int x, int y, int width, int height);

std::shared_ptr<UltraCanvasSWOTDiagram> CreateSWOTDiagramElement(
        const std::string& id, int x, int y, int width, int height,
        SWOTDesign design);
```

### Sample Data

```cpp
namespace SWOTDiagramSamples {
    std::array<std::vector<SWOTItem>, kSWOTQuadrantCount> BusinessExample();
}
```

## Usage Examples

### Basic SWOT Diagram (reference infographic style)

```cpp
#include "Plugins/Diagrams/UltraCanvasSWOTDiagram.h"

auto swot = CreateSWOTDiagramElement("swot", 20, 20, 620, 500);
swot->SetTitle("SWOT Analysis");
swot->LoadSampleData();               // Or add your own items:
swot->AddItem(SWOTQuadrant::Strengths, "Award-winning support team");
parentContainer->AddChild(swot);
```

### Classic Matrix with Axis Captions

```cpp
auto matrix = CreateSWOTDiagramElement("swotMatrix", 20, 20, 620, 500,
                                       SWOTDesign::Matrix);
matrix->SetShowAxisCaptions(true);    // HELPFUL/HARMFUL + INTERNAL/EXTERNAL
matrix->LoadSampleData();
```

### Custom Titles, Colors and Center Text

```cpp
auto diagram = CreateSWOTDiagramElement("swot", 20, 20, 620, 500);
diagram->SetQuadrantConfig(SWOTQuadrant::Strengths,
        SWOTQuadrantConfig("Advantages", "A", Color(26, 188, 156, 255)));
diagram->SetQuadrantAccentColor(SWOTQuadrant::Threats, Color(192, 57, 43, 255));
diagram->SetCenterText("ACME Corp\n2026");
diagram->SetDarkTheme(true);
```

### Selection Callbacks

```cpp
diagram->onItemSelect = [](SWOTQuadrant q, size_t index, const SWOTItem& item) {
    std::cout << "Selected item " << index << ": " << item.text << std::endl;
};
diagram->onSelectionChange = [ptr = diagram.get()]() {
    auto sel = ptr->GetSelectedItem();
    if (!sel.IsValid()) std::cout << "Selection cleared\n";
};
```

### Switching Designs at Runtime

```cpp
diagram->SetDesign(SWOTDesign::Rows);     // Same data, list presentation
diagram->SetShowBadges(false);
diagram->SetShowItemBullets(false);
```

## Demo Application

The demo page lives in `Apps/DemoApp/UltraCanvasSWOTDiagramExamples.cpp`
(`Info Graphics > SWOT Diagram`) and shows six tabs, one per design preset:

1. **Corner Badges** — the vivid infographic style with corner letter badges
   and a central SWOT circle.
2. **Matrix** — the textbook 2x2 grid with axis captions enabled.
3. **Cards** — separated rounded cards with colored header bars.
4. **Diamond** — central letter diamond with corner text blocks.
5. **Rows** — stacked horizontal bands with big letter blocks.
6. **Columns** — four columns with header chips.

The sidebar demonstrates runtime reconfiguration (light/dark theme, badges,
center element, bullets), dynamic data (add random item, remove selected,
reload sample data) and the statistics panel is driven by the selection
callbacks and per-quadrant counting utilities.

## Notes & Best Practices

- **One accent per quadrant:** every tint, badge and header color derives
  from `accentColor`, so themes stay consistent when you recolor.
- **Long texts:** items wrap to the panel width; panels that overflow show
  "+N more...". The `Rows` design gives items the most horizontal room.
- **Design switching** preserves data and selection; only geometry changes.
- **Tooltips** show the full item text, so clipped items remain readable;
  disable with `SetEnableTooltips(false)` if the host page provides its own
  hover UI.
- **Redraws** are requested automatically by every setter; no manual
  invalidation is needed.
