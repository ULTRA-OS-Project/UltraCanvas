# UltraCanvasTimelineDiagram

A **narrative timeline infographic**: an ordered list of events laid out along a
decorative path, with nine design presets. Items carry a period caption, a
title, a paragraph and an icon; they are evenly spaced by default, so this is
the presentation timeline rather than a date-accurate schedule.

**Pick the right element:**

| You want | Use |
|---|---|
| A presentation/story with N events and rich text | `UltraCanvasTimelineDiagram` (this element) |
| Dates to scale, milestones and spans, no task table | [`UltraCanvasTimelineChart`](UltraCanvasTimelineChart.md) |
| A full project schedule: tasks, dependencies, progress, critical path | [`UltraCanvasGanttChart`](UltraCanvasGanttChart.md) |
| Fixed process steps with a current position | [`UltraCanvasStepper`](UltraCanvasStepper.md) |
| A sequence arranged around a circle | [`UltraCanvasCircularInfoGraphic`](UltraCanvasCircularInfoGraphic.md) |

**Namespace:** `UltraCanvas`
**Header:** `include/Plugins/Diagrams/UltraCanvasTimelineDiagram.h`
**Source:** `Plugins/Diagrams/UltraCanvasTimelineDiagram.cpp`
**Base class:** `UltraCanvasChartElementBase`
**Demo:** `Apps/DemoApp/UltraCanvasTimelineDiagramExamples.cpp` (Info Graphics > Timeline Diagram)
**Version:** 1.0.0
**Last Modified:** 2026-07-30
**Author:** UltraCanvas Framework

## Class hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasChartElementBase
            └── UltraCanvasTimelineDiagram
```

## Quick start

```cpp
#include "Plugins/Diagrams/UltraCanvasTimelineDiagram.h"
using namespace UltraCanvas;

auto timeline = CreateTimelineDiagramElement("history", 10, 10, 900, 420,
                                             TimelineDesign::Bar);
timeline->SetTitle("Company History");
timeline->SetSubtitle("Six milestones from founding to one million users");

timeline->AddItem("2015", "Founded", "Three engineers start the company.");
timeline->AddItem("2016", "First product", "Ships to twelve pilot customers.");
timeline->AddItem("2017", "Series A", "The team grows to forty people.");

timeline->SetPalette(TimelinePalette::Vibrant);
container->AddChild(timeline);
```

`CreateTimelineDiagramWithData(id, x, y, w, h, items, design)` builds the same
element from a prepared `std::vector<TimelineItem>` in one call.

## Data model

```cpp
struct TimelineItem {
    std::string caption;      // Big period label: "2015", "Q3", "Phase 1"
    std::string title;        // Short heading
    std::string body;         // Paragraph; wrapped, "\n" starts a new paragraph
    std::string iconGlyph;    // Short text/glyph drawn inside the node

    Color accentColor;        // Transparent (default) = next palette entry

    long dateSerial;          // Optional real date (see below)
    bool hasDate;

    double nodeSize;          // 0 = automatic; node radius / bubble radius
    double stemLength;        // 0 = automatic; Hanging design stem
    int    side;              // -1 above/left, +1 below/right, 0 = policy

    std::string tooltip;      // Overrides the generated tooltip
    bool highlighted;         // Extra emphasis
};
```

Dates are **days since 1970-01-01**, the same representation as
`GanttDate::serial`, so the two interoperate directly:

```cpp
item.SetDate(2015, 6, 1);            // from a civil date
item.SetDate(ganttTask.start.serial); // straight from a Gantt task
```

### Managing items

```cpp
timeline->AddItem(item);                  // or AddItem(caption, title, body)
timeline->InsertItem(2, item);
timeline->SetItems(list);                 // replaces everything
timeline->RemoveItem(3);
timeline->MoveItem(1, 4);
timeline->ClearItems();
timeline->LoadSampleData();               // built-in company-history example

timeline->EditItem(0).body = "New text";  // in-place edit ...
timeline->ItemsChanged();                 // ... then invalidate the layout
```

## Designs

`ApplyDesign(design)` replaces the whole style with that design's preset and
switches to it; `SetDesign(design)` changes the geometry only, keeping the
current style. This is the same contract as `UltraCanvasGanttChart`.

| `TimelineDesign` | Look |
|---|---|
| `Bar` | Thick spine split into one colored segment per item; circular nodes alternate on the top and bottom edges, the period caption sits on the opposite side |
| `Line` | Thin axis with small nodes and a short stem to each text block; arrowhead at the end |
| `Alternating` | Classic zigzag: cards above and below a central spine, connected by stems |
| `Cards` | Row of separate cards with a numbered badge on the top edge; no spine |
| `Vertical` | Top-down spine with cards left and right — the choice for long lists and portrait layouts |
| `Serpentine` | Snaking ribbon wrapping over several runs, numbered nodes on the path |
| `Hanging` | Axis on top, stems dropping to bubbles of varying size, text wrapped to the circle |
| `Chevron` | Arrow blocks pointing forward — process/phase presentation |
| `Steps` | Ascending staircase blocks for growth narratives |

Cards, boxes and bubbles **size themselves to their text**; anything that still
does not fit is ellipsized (`style.maxBodyLines` caps the body).

## Layout options

```cpp
timeline->SetSidePolicy(TimelineSidePolicy::Alternate);  // AllAbove / AllBelow / PerItem
timeline->SetReverseOrder(true);                         // right-to-left / bottom-up
timeline->SetCurrentIndex(2);                            // items after #2 render as "pending"
timeline->SetScaleLabels({"Jan", "Feb", /* ... */});      // independent axis label track
timeline->EditStyle().showScaleRow = true;
timeline->StyleChanged();
```

`AllAbove` / `AllBelow` mean *left* / *right* for the `Vertical` design.
`SetCurrentIndex` drives the roadmap idiom: everything up to the index is drawn
filled, everything after it hollow and muted; `-1` disables the effect.

### Even vs. proportional placement

```cpp
timeline->SetPlacement(TimelinePlacement::Proportional);
```

`Even` (default) spaces items equally — time is *not* to scale, which is what
an infographic normally wants. `Proportional` positions each item by its
`dateSerial`, so a two-year gap really is twice a one-year gap, while keeping
the decorative design. It requires every item to have a date (otherwise it
silently falls back to `Even`) and enforces a minimum gap so near-simultaneous
items stay legible.

## Palettes and color

```cpp
timeline->SetPalette(TimelinePalette::Sunset);
timeline->SetCustomPalette({Color(200, 30, 60, 255), Color(30, 90, 200, 255)});
timeline->SetColorMode(TimelineColorMode::GradientAlongPath);
timeline->SetDarkTheme(true);
```

Palettes: `CorporateBlue` (default), `Vibrant`, `Pastel`, `Ocean`, `Sunset`,
`Forest`, `Slate`, `Mono`, `Custom`. Color modes: `PerItem` (cycle the
palette), `Single`, `GradientAlongPath` (the spine or serpentine ribbon becomes
a gradient across the item colors). A per-item `accentColor` always wins.

`SetDarkTheme(true)` recolors the background, text, cards, spine and connectors
coherently; it does not touch the palette.

## Style

Every visual knob lives in `TimelineDiagramStyle` — spine thickness and corner
radius, node shape/radius/border, connector style and length, content container
and padding, shadows, per-role font sizes, the auto-shrink floor, and the
design-specific `serpentineItemsPerRun`, `serpentineTurnRadius`,
`hangingTiers` and `stepRise`. Two workflows:

```cpp
// Replace wholesale
TimelineDiagramStyle s = TimelineDiagramStyles::CreateForDesign(TimelineDesign::Bar);
s.nodeRadius = 22.0;
s.showShadows = true;
timeline->SetStyle(s);

// Or tweak in place
timeline->EditStyle().showNodeNumbers = true;
timeline->StyleChanged();
```

Node shapes: `Circle`, `RoundedSquare`, `Square`, `Diamond`, `Hexagon`, `Pin`,
`Hidden`. Connector styles: `Straight`, `Elbow`, `Curved`, `Dashed`, `Hidden`.
Content containers: `Plain`, `Card`, `OutlinedCard`, `Bubble`.

> `Hidden` / `Plain` rather than `None`: `None` is a macro defined by X11,
> which the window headers pull in.

## Text handling

- Word wrap with a line cap per role and `...` ellipsis on overflow.
- Auto-shrink: fonts scale down with the available column width, never below
  `style.minFontSize`.
- The `Bubble` container wraps text **to the circle** — every line is measured
  against the chord width at its own height, so text fills the bubble instead
  of sitting in an inscribed rectangle.

## Interaction

Hover highlighting, tooltips, selection and callbacks are on by default
(`SetEnableTooltips` / `SetEnableSelection` from the base class control them):

```cpp
timeline->onItemSelect = [](size_t index, const TimelineItem& item) { /* ... */ };
timeline->onItemHover = [](size_t index, const TimelineItem& item) { /* ... */ };
timeline->onItemDoubleClick = [](size_t index, const TimelineItem& item) { /* ... */ };
timeline->onSelectionChange = []() { /* ... */ };

timeline->SetSelectedIndex(2);
int selected = timeline->GetSelectedIndex();   // -1 when nothing is selected
timeline->ClearSelection();
```

Clicking an item selects it, clicking it again or clicking empty space clears
the selection. The tooltip is built from caption/title/body unless
`TimelineItem::tooltip` overrides it.

## Geometry queries

Valid after the first render — useful for overlaying your own annotations:

```cpp
Point2Dd center;
Rect2Dd rect;
if (timeline->GetItemNodeCenter(0, center)) { /* node position, element-local */ }
if (timeline->GetItemContentRect(0, rect))  { /* text block of item 0 */ }
```

## Sample data

```cpp
TimelineDiagramSamples::CompanyHistory();   // 6 dated items, 2015-2020
TimelineDiagramSamples::ProjectYear(2026);  // 12 dated monthly items
```

## Not yet implemented

Tracked in [`UltraCanvasTimelineDiagramProposal.md`](UltraCanvasTimelineDiagramProposal.md):
the `Roadmap` design, item groups/phase brackets (A-D6), per-item images
(A-D4), CSV/JSON import-export (A-X1/A-X2), keyboard navigation (A-I6) and the
sequential entrance animation (A-I7).
