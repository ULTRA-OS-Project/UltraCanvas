# UltraCanvasFishboneDiagram

A **fishbone (Ishikawa) cause-and-effect diagram** — the standard root-cause
analysis picture — with eight design presets. One **effect** at the head of a
spine, **cause categories** on the ribs, **causes** on the twigs, and optional
**sub-causes** for the 5-Whys tail.

**Pick the right element:**

| You want | Use |
|---|---|
| Root causes of one problem, grouped into categories | `UltraCanvasFishboneDiagram` (this element) |
| A free-form topic tree with any structure | [`UltraCanvasMindMap`](UltraCanvasMindMapExamples.md) |
| Four factor lists: strengths, weaknesses, opportunities, threats | [`UltraCanvasSWOTDiagram`](UltraCanvasSWOTDiagramExamples.md) |
| An ordered story along a decorative path | [`UltraCanvasTimelineDiagram`](UltraCanvasTimelineDiagram.md) |
| Process steps with branches and decisions | [`UltraCanvasFlowChart`](UltraCanvasFlowChartExamples.md) |

**Namespace:** `UltraCanvas`
**Headers:** `include/Plugins/Diagrams/UltraCanvasFishboneDiagram.h` (element),
`include/Plugins/Diagrams/UltraCanvasFishboneModel.h` (UI-free model + text interchange)
**Sources:** `Plugins/Diagrams/UltraCanvasFishboneDiagram.cpp`,
`Plugins/Diagrams/UltraCanvasFishboneModel.cpp`,
`Plugins/Diagrams/UltraCanvasFishboneText.cpp`
**Base class:** `UltraCanvasChartElementBase`
**Demo:** `Apps/DemoApp/UltraCanvasFishboneDiagramExamples.cpp` (Info Graphics > Fishbone Diagram)
**Tests:** `Tests/FishboneModelTest.cpp` (target `FishboneModelTest`)
**Research:** [`UltraCanvasFishboneDiagramProposal.md`](UltraCanvasFishboneDiagramProposal.md)
**Version:** 1.0.0
**Last Modified:** 2026-08-07
**Author:** UltraCanvas Framework

## Class hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasChartElementBase
            └── UltraCanvasFishboneDiagram
```

## Quick start

```cpp
#include "Plugins/Diagrams/UltraCanvasFishboneDiagram.h"
using namespace UltraCanvas;

auto fishbone = CreateFishboneDiagramElement("rca", 10, 10, 900, 520,
                                             FishboneDesign::SpineChips);
fishbone->SetEffect("Quality Control Issues");

const size_t man = fishbone->AddCategory("Man (Human Factors)");
fishbone->AddCause(man, "Inadequate training");
fishbone->AddCause(man, "Operator error");

const size_t machines = fishbone->AddCategory("Machines (Equipment)");
fishbone->AddCause(machines, "Calibration issues");
fishbone->AddCause(machines, "Outdated technology");

fishbone->SetPalette(FishbonePalette::Vibrant);
fishbone->onCauseSelect = [](const FishboneRef& ref, const FishboneCause& cause) {
    // ref identifies category / cause / sub-cause
};
```

Or start from a named checklist and fill it in:

```cpp
fishbone->SetEffect("Release Slipped Two Sprints");
fishbone->LoadCategoryPreset(FishboneCategoryPreset::Software);
fishbone->AddCause(0, "Scope added mid-sprint");
```

Or load the whole diagram from text (see [Text interchange](#text-interchange)):

```cpp
fishbone->LoadMermaid(R"(ishikawa-beta
    Blurry Photo
    Process
        Out of focus
    Equipment
        Dirty lens
)");
```

## Data model

The model lives in `UltraCanvasFishboneModel.h` and has no UI dependencies, so
it can be built, parsed and unit-tested without a window.

```cpp
struct FishboneCause {
    std::string text;
    std::vector<FishboneCause> subCauses;  // 5-Whys tail; usually empty
    Color       markerColor;               // transparent = category accent
    std::string tooltip;                   // overrides the generated tooltip
    bool        highlighted;               // extra emphasis (a suspected cause)
    bool        isRootCause;               // drawn with a ring around the marker
    double      weight;                    // optional score; 0 = unweighted
};

struct FishboneCategory {
    std::string title;
    std::string iconPath;    // image drawn inside the badge
    std::string iconGlyph;   // short text used when no image is set
    Color       accentColor; // transparent = take the next palette entry
    int         side;        // -1 above/left, +1 below/right, 0 = side policy
    bool        collapsed;   // draw the rib and label but not the causes
    std::vector<FishboneCause> causes;
};

struct FishboneDocument {
    std::string effect;
    std::vector<FishboneCategory> categories;
};
```

Structure is fixed by the diagram, not by the data: depth 0 is the effect,
depth 1 a category, depth 2 a cause, depth 3+ sub-causes. Categories never
nest; causes do.

> `FishboneDocument::AddCategory()` and `FishboneCategory::AddCause()` return a
> reference that the *next* add on the same container may invalidate — the
> `std::vector::emplace_back` rule. Finish with the reference, or index into the
> vector, before adding the next element.

Element-side accessors mirror the model: `SetDocument` / `GetDocument`,
`SetEffect`, `AddCategory`, `SetCategories`, `RemoveCategory`, `AddCause`,
`AddSubCause`, `RemoveCause`, `ClearCauses`, `CountCategories`, `CountCauses`,
`CountAllCauses(includeSubCauses)`, plus `SetCategoryTitle` / `SetCategoryAccent`
/ `SetCategoryIcon` / `SetCategoryGlyph` / `SetCategorySide` /
`SetCategoryCollapsed`.

## Designs

`SetDesign(FishboneDesign)` picks the geometry. Every design draws the same
document; only the spine, the ribs and the cause placement change.

| Design | Description |
|---|---|
| `Classic` | Textbook herringbone: thin spine, straight angled ribs, category chips at the rib tips, causes on the twigs |
| `SpineChips` | Solid tapered spine arrow; category pills sit **on** the spine and end in a circular icon badge; dot-terminated twigs |
| `CrossedRibs` | One stroke crosses the spine and serves two categories at once — one above, one below; badge at each crossing |
| `Bracket` | Parallelogram bones (slanted edge plus a horizontal shelf) with horizontal dot leaders, so cause text stays level |
| `ChevronSpine` | The spine is a chain of chevron blocks, one per category, each with a pictogram; hairline leaders reach floating pills |
| `Columns` | No ribs: each category is a tinted panel straddling the spine with an icon badge at the far end and its causes inside |
| `Vertical` | Spine top-to-bottom, ribs left and right — the portrait form |
| `Compact` | Every category on one side of a low spine; halves the height at the cost of length |

## Layout

The solver is described in full in the proposal (§7). Two behaviours are worth
knowing when authoring:

- **Ribs are trimmed to their content.** A rib is only as long as its causes
  need, so a category with two causes does not draw a rib sized for six.
- **Roots are placed after the rib length is known**, so the outermost category
  always has room for its labels against the frame edge.

Options:

| Call | Effect |
|---|---|
| `SetSidePolicy(FishboneSidePolicy)` | `Alternate` (default), `AllAbove`, `AllBelow`, `PerCategory` (uses `FishboneCategory::side`) |
| `SetRibAngle(degrees)` | Angle between a rib and the spine, clamped to 25–80. The solver may steepen it further when ribs would otherwise collide |
| `SetHeadShape(FishboneHeadShape)` | `Arrow`, `Triangle` (default), `Box`, `FishHead`, `Hidden` |
| `SetTailShape(FishboneTailShape)` | `Hidden`, `Chevron` (default), `Triangle`, `FishTail` |
| `SetEffectPlacement(FishboneEffectPlacement)` | `Title` (default — the surveyed infographics all put it there), `HeadBox`, `Hidden` |
| `SetCauseMarker(FishboneCauseMarker)` | `Dot` (default), `Bullet`, `Numbered`, `Hidden` |
| `SetLabelPlacement(FishboneLabelPlacement)` | `Auto` (default, per design), `RibTip`, `OnSpine`, `BesideSpine`, `PanelHeader` |
| `SetSpineThickness(px)` | Spine stroke width / solid-bar thickness |

When a category has more causes than its rib can hold, the overflow is reported
as a `+N` badge under the category chip rather than being silently dropped.

## Palettes and color

`SetPalette(FishbonePalette)` selects `CorporateBlue`, `Vibrant` (default),
`Pastel`, `Ocean`, `Sunset`, `Forest`, `Slate`, `Mono` or `Custom`.
`SetCustomPalette(std::vector<Color>)` supplies the custom entries and switches
to `Custom`.

Color is per **category**: the accent drives the chip, the badge, the rib, the
cause markers and the panel tint. A category with a non-transparent
`accentColor` keeps it; otherwise it takes the next palette entry.
`SetDarkTheme(bool)` switches the background, text and spine colors.

## Style toggles

All are orthogonal to the design:

`SetShowCategoryIcons` (badges/glyphs), `SetShowCauses`, `SetShowSubCauses`,
`SetShowSpineCaption` + `SetSpineCaption` (an inline caption on the spine near
the tail, e.g. "Potential Causes"), `SetShowWaypoints` (small chevrons at each
rib root), `SetShowWeights` (numeric badge on weighted causes),
`SetCauseFontSize`, `SetCategoryFontSize`.

Icons: set `iconPath` for an image (drawn `ImageFitMode::Contain` inside the
badge) or `iconGlyph` for a short text mark. With neither, the first character
of the title is used.

## Text handling

Cause labels wrap to the width the solver allocates and are edge-aligned to
their marker — right-aligned when the text runs back toward the tail,
left-aligned when it runs toward the head. Category chips size to their title
and ellipsize (`...`) when the available pitch cannot hold it.

## Interaction

Hover and selection address categories, causes and sub-causes through one
handle:

```cpp
struct FishboneRef {
    int category, cause, subCause;   // -1 where not applicable
    bool IsValid() const; bool IsCategory() const;
    bool IsCause() const; bool IsSubCause() const;
};
```

- Click a cause or a category chip to select it; click empty space to clear.
- Double-click a category chip to collapse/expand its causes.
- Hover shows a tooltip (category, cause text, root-cause flag, weight and
  sub-cause count), unless `SetEnableTooltips(false)`.

Callbacks: `onCategorySelect`, `onCauseSelect`, `onCauseHover`,
`onCauseDoubleClick`, `onSelectionChange`. Selection and tooltips are enabled
through the base class (`SetEnableSelection`, `SetEnableTooltips`).

Marking causes from code: `SetCauseHighlighted(ref, bool)`,
`SetCauseRootCause(ref, bool)`, `SetCauseWeight(ref, double)` and
`FindCause(ref)`.

## Geometry queries

Valid after the first render: `GetSpineLine(start, end)`,
`GetCategoryLabelRect(index, rect)`, `GetCategoryColor(index, color)`.

## Category presets

`LoadCategoryPreset(FishboneCategoryPreset)` fills in the titles and glyphs of a
named checklist and leaves the causes empty:

| Preset | Categories |
|---|---|
| `SixM` | Man, Machine, Material, Method, Measurement, Mother Nature |
| `FiveME` | Man, Machine, Material, Method, Environment |
| `EightP` | Product, Price, Place, Promotion, People, Process, Physical Evidence, Productivity |
| `FourS` | Surroundings, Suppliers, Systems, Skills |
| `FiveS` | Surroundings, Suppliers, Systems, Skills, Safety |
| `PEMPEM` | People, Equipment, Materials, Process, Environment, Management |
| `Software` | Requirements, Design, Code, Test, Process, Tooling, People, Environment |

`FishboneCategoryPresetName(preset)` returns a human-readable name.

## Text interchange

Two notations, both indentation-based, implemented in
`UltraCanvasFishboneText.cpp`:

**Mermaid `ishikawa-beta`** (Mermaid 11.13+) — `LoadMermaid(text, &error)` /
`ExportMermaid()`:

```
ishikawa-beta
    Blurry Photo
    Process
        Out of focus
        Shutter speed too slow
    Equipment
        LENS
            Damaged lens
            Dirty lens
```

**Indented outline** — `LoadOutline(text, &error)` / `ExportOutline()`: the same
shape without the keyword. The first non-blank line is the effect; the line
after it fixes the category level; anything deeper is a cause, nested by how
much deeper.

The parser is deliberately forgiving: tabs or spaces, any consistent indent
width, `-` / `*` / `+` bullets stripped, blank lines and `//`, `#`, `%%`
comments ignored, `---` front matter skipped, and text with no keyword read as a
plain outline. Both notations round-trip through the model — see
`Tests/FishboneModelTest.cpp`.

## Validation and queries

- `Validate()` returns human-readable problems (no effect, no categories, an
  empty category title, a category with no causes, blank cause text). It never
  blocks rendering.
- `FishboneDepth(document)` — 0 none, 1 categories, 2 causes, 3+ sub-causes.
- `CountFishboneCauses(categories, includeSubCauses)`.

## Sample data

`LoadSampleData()` loads the quality-control example. The full set lives in
`namespace FishboneSamples`:

| Sample | Content |
|---|---|
| `QualityControl()` | "Quality Control Issues", 4 categories from the 6M set, 12 causes |
| `CustomerChurn()` | "Customer Churn", the 4S set, with sub-causes and a marked root cause |
| `SoftwareDelivery()` | "Release Slipped Two Sprints", 5 categories, a highlighted weighted cause |

## Not yet implemented

Deferred from the proposal's later phases: pan/zoom through
`UltraCanvasDiagramViewport`, entrance animation, keyboard navigation, JSON and
CSV import/export, and the `MindMapStructure::Fishbone` layout case that draws
an existing mind map as ribs.
