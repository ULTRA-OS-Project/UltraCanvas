# UltraCanvasPerformanceMatrix

<!-- doc-check: std::shared_ptr<UltraCanvasContainer> container; -->

A performance matrix rates something on two ordinal axes, usually people on
performance and potential, and names each crossing: the 9-box talent grid,
the performance management matrix, performance-vs-potential codes and
skill / will. Each cell is a category with a code, a badge, a title, a
description, bullets or an action. The element draws the grid in one of four
designs and one of four palettes.

- Header: `include/Plugins/Diagrams/UltraCanvasPerformanceMatrix.h`
- Model: `include/Plugins/Diagrams/UltraCanvasPerformanceMatrixModel.h` (UI-free)
- Base class: `UltraCanvasChartElementBase`
- Demo: **Info Graphics → Performance Matrix**
- Design: [`UltraCanvasPerformanceMatrixProposal.md`](UltraCanvasPerformanceMatrixProposal.md)

## Contents

- [Which element do I want?](#which-element-do-i-want)
- [Quick start](#quick-start)
- [The model](#the-model)
  - [Axes and levels](#axes-and-levels)
  - [Cells](#cells)
  - [The best corner](#the-best-corner)
- [Designs](#designs)
- [Cell colouring and palettes](#cell-colouring-and-palettes)
- [Text fitting](#text-fitting)
- [Interaction](#interaction)
- [Samples](#samples)
- [The population](#the-population)
  - [How a cell shows it](#how-a-cell-shows-it)
  - [The legend, totals and level bars](#the-legend-totals-and-level-bars)
  - [Showing it to a wider room](#showing-it-to-a-wider-room)
- [What is not built yet](#what-is-not-built-yet)

## Which element do I want?

| You want | Use |
|---|---|
| Named categories where two rated axes cross (9-box, performance vs potential, skill / will) | This element |
| Relationships between two to four item lists, with a symbol at each crossing (QFD, responsibility matrices) | `UltraCanvasMatrixDiagram` |
| Points on a continuous plane split into four quadrants (SWOT, BCG, Eisenhower) | `UltraCanvasQuadrantChart` |
| Factors plotted over curved importance–performance zones | `UltraCanvasImportancePerformanceChart` |
| One number per cell, shown as a colour | `UltraCanvasHeatmapChart` |

## Quick start

```cpp
#include "Plugins/Diagrams/UltraCanvasPerformanceMatrix.h"
using namespace UltraCanvas;

auto matrix = CreatePerformanceMatrix("ninebox", 0, 0, 840, 600);
matrix->SetModel(PerformanceMatrixSamples::NineBox());
matrix->SetDesign(PerformanceMatrixDesign::Grid);
matrix->SetCellColoring(PerformanceCellColoring::DiagonalBands);
matrix->SetPalette(PerformanceMatrixPalette::TrafficLight());
matrix->onCellClick = [](const PerformanceCellRef& ref) {
    // ref.row is the y level, ref.col the x level, both counted from the low end
};
container->AddChild(matrix);
```

## The model

`PerformanceMatrixModel` is plain data. `SetModel` takes a copy and uses
`model.title` as the element's title; set the model again after changing it.

### Axes and levels

`xAxis` and `yAxis` are `PerformanceAxis`: a `title` and a list of
`levels`, **always ordered low to high**. Row 0 is the lowest y level and
column 0 the lowest x level, whatever the picture looks like. That makes
`row` and `col` mean the same thing in every design. By default the x axis
runs low to high left to right, and the y axis low to high bottom to top.
`reversed = true` turns either axis round on screen without changing its
data.

A `PerformanceLevel` has a `label`, an optional `caption` (a second line, such
as "Move and Retain"), an optional `iconPath` (drawn in header tabs) and an
optional `accent` colour. Each axis takes 1 to 6 levels
(`PerformanceMatrixModel::kMaxLevels`), and the two axes need not have the
same number.

```cpp
PerformanceMatrixModel model;
model.title = "Sales team review";
model.xAxis.title = "Results";
model.xAxis.levels = {{"Below target"}, {"On target"}, {"Above target"}, {"Exceptional"}};
model.yAxis.title = "Potential";
model.yAxis.levels = {{"Limited", "Keep in role"}, {"Promotable", "Develop"}};
```

### Cells

Cells are sparse: a grid with no cells is valid and draws empty boxes under
its headers. `SetCell` adds a cell, or replaces the one already at its row and
column. Every field of a `PerformanceCell` is optional and is drawn when set,
in this order from the top: `iconPath`, `badge` (a number in a circle),
`code` (large, bold), `title` (bold), `description` (a `\n` starts a new
line), `bullets` and `action`. `fill` is used by
`PerformanceCellColoring::Explicit`. `note` appears only in the tooltip.

```cpp
PerformanceCell star;
star.row = 1;
star.col = 3;
star.badge = 8;
star.title = "Rising star";
star.bullets = {"Beats every target", "Ready for a team"};
star.action = "Promote within a year";
model.SetCell(star);
const PerformanceCell* found = model.FindCell(1, 3);
std::vector<std::string> problems = model.Validate();   // empty = valid
```

`Validate()` reports an axis with no levels or more than six, a level without
a label, a cell outside the grid, two cells in one place, and a negative
badge.

### The best corner

`highXIsBest` and `highYIsBest` (both `true` by default) say which corner is
best. `Rank(row, col)` counts steps from it: 0 for the best cell, up to
`MaxRank()` for the worst. The `DiagonalBands` colouring reads the rank, so
a matrix whose best corner is elsewhere is still coloured correctly.

## Designs

| Design | Looks like | Column labels |
|---|---|---|
| `Grid` | Contiguous cells, row and column labels, axis arrows pointing to each axis's high end, axis titles | Below |
| `Tiles` | Separated rounded tiles; a large code over a centred description | Above |
| `HeaderTabs` | Rounded tabs in each level's colour, with icon, label and caption, above and to the left of plain cells. No arrows or axis titles: the tabs carry the levels | Above (as tabs) |
| `Cards` | White rounded cards with a border in the cell's colour, the badge at the top left and the bullets below | Below |

`SetColumnLabels(PerformanceColumnLabels::Top / Bottom)` overrides where the
column labels go. `SetShowAxisArrows(false)` removes the arrows.
`SetShowContainerPanel(true)` draws a rounded panel behind the matrix in the
palette's panel colour.

## Cell colouring and palettes

`SetCellColoring` decides each cell's colour:

| Colouring | Colour |
|---|---|
| `Plain` | The palette's cell background (white); the colour lives in the headers |
| `Explicit` | Each cell's own `fill` |
| `DiagonalBands` | The palette's `rankColors`, sampled by the cell's rank from the best corner |
| `ByRow` / `ByColumn` | The row's or column's level colour, tinted |
| `ByCount` | How many people the cell holds, through `SetCountColormap` (Blues by default): the fullest cell is the darkest |

In the `Cards` design, the colour goes to the card's border and badge, and
the face stays white.

A level's colour is its own `accent` when it has one; otherwise it comes from
the palette's `levelColors`. A palette with `forceLevelColors` ignores the
levels' accents, which is how one hue recolours a model that brings its own
colours. Text on a fill is dark or white, whichever reads.

| Palette | For |
|---|---|
| `TrafficLight()` | Blue, green, yellow, orange, red from the best corner (performance vs potential tiles) |
| `Vivid()` | Saturated red, orange, green levels (header tabs) |
| `Monochrome(hue)` | Every colour a shade of one hue |
| `Pastel()` | Soft pink, orange, yellow levels (talent review cards) |

## Text fitting

All cells share one font size: the largest at which every cell's text fits,
counting down from `SetCellFontSize` (13 by default) to a floor of 9.
Different sizes in one grid would read as different importance. If a cell
still does not fit at the floor, it gives up content in this order: bullets
from the end (an ellipsis marks the cut), then the description, then the
action. The code, the badge and the title always stay.

Nothing is lost: the tooltip and `BuildCellTooltip(ref)` always hold the whole
cell. `GetFittedFontSize()` and `AnyCellTruncated()` report what the fitting
did.

## Interaction

Hovering a cell outlines it, colours its row and column labels (or outlines
its tabs), shows its tooltip and calls `onCellHover`. When the pointer leaves
the cells, `onCellHover` is called with an invalid ref. Clicking a cell
selects it (a second click clears the selection) and calls `onCellClick`.
`SetSelectedCell`, `GetSelectedCell` and `ClearSelection` drive the
selection from code. `CellAt(point)` and `GetCellRect(row, col)` map between
cells and the element's coordinates after the first render.

```cpp
matrix->onCellHover = [](const PerformanceCellRef& ref) {
    if (!ref.IsValid()) return;   // the pointer left the cells
};
matrix->SetSelectedCell(PerformanceCellRef(2, 2));
std::string text = matrix->BuildCellTooltip(PerformanceCellRef(2, 2));
```

## Samples

| Sample | Shape | Content |
|---|---|---|
| `PerformanceMatrixSamples::NineBox()` | 3×3 | A name, description and action per box; badges 1–9 counting up to the best box |
| `PerformanceManagement()` | 3×3 | Performance levels as rows and three facets of potential as columns, each with its own accent; a phrase per cell |
| `PerformanceVsPotential()` | 3×3 | Codes 1A–3C counted from the best corner, and a two-line description per tile |
| `TalentReview()` | 3×3 | Potential levels with action captions; badges and three bullets per card |
| `SkillWill()` | 2×2 | Guide, Delegate, Direct, Excite |
| `SampleTeam()` | — | 36 invented people in three departments for `NineBox()`: most placed by 1–5 review scores, six by a calibrated cell |
| `SampleTargetDistribution()` | — | A forced distribution for a 3×3 grid: 10 % top box, 30 % core box |

The demo page shows each one in the design it was made for, with the design,
colouring and palette switchable.

## The population

`model.items` holds the people (or products, or teams) being placed. Each
`PerformanceMatrixItem` has a `label`, a `group` (a team or department), an
optional `color` (by default the group's colour), a `note` for the tooltip,
and a placement:

- **An explicit cell** (`row`, `col`), or
- **two scores** (`xScore`, `yScore`) on the axes' score ranges
  (`PerformanceAxis::minScore` / `maxScore`). Each axis maps a score to a
  level through its levels' `upperBound`s, or splits the range evenly when
  every bound is left at NaN. A score on a bound belongs to the level above.

An explicit cell wins over the scores. That is the calibration rule: the
meeting overrides the formula, and the scores stay in the data beside the
override.

```cpp
PerformanceMatrixModel model = PerformanceMatrixSamples::NineBox();   // both axes score 1-5
model.items = {
    PerformanceMatrixItem("A. Rivera", "Sales", 4.6, 4.2),    // placed by scores
    PerformanceMatrixItem("B. Okoye", "Support", 1, 2),       // placed in row 1, column 2
};
model.xAxis.levels[0].upperBound = 2.5;                   // "Low" performance ends at 2.5
PerformanceCellRef where = model.CellOf(model.items[0]);
std::vector<int> perCell = model.CountsPerCell();         // row-major
std::vector<int> perRow = model.CountsPerRow();
std::vector<std::string> departments = model.Groups();    // in order of first appearance
```

### How a cell shows it

`SetPopulationDisplay` picks one form for every cell:

| Display | Shows |
|---|---|
| `Count` (default) | The number, in a chip in the cell's top corner. It is honest at any size and shows no names |
| `Percent` | The cell's share of the population, and `/ target` when the model has a `targetDistribution` |
| `Dots` | One dot per person in a fixed grid, in the group's colour; `+N` when they do not fit |
| `Names` | The labels in columns, each with its group's colour; `+N more` when they do not fit |
| `Hidden` | Nothing |

Dots and names take the lower part of the cell, and the cell's own text is
fitted into what is left. Hovering a dot or a name shows that person's
tooltip (name, group, box, levels, scores, note). A click calls
`onItemClick` and does not select the cell. `ItemAt(point)` and
`GetItemRect(index)` map between people and the element's coordinates.

`targetDistribution` is optional. It holds one share per cell, row-major from
row 0, and the shares sum to 1. `Validate()` checks its length and its sum.

### The legend, totals and level bars

When the people have named groups, a legend of them stands at the right,
with each group's size. A click on an entry hides that group: its people
leave the cells, the counts, the shares and `ByCount`, and a second click
brings them back. `SetGroupVisible` / `IsGroupVisible` do the same from
code, and `SetShowLegend(false)` removes the legend.

`SetShowTotals(true)` adds "n = 12" under each row and column label.
`SetShowLevelBars(true)` draws a strip in the level colours between the
grid and its labels on both axes.

```cpp
matrix->SetModel(model);
matrix->SetPopulationDisplay(PerformancePopulationDisplay::Dots);
matrix->SetShowTotals(true);
matrix->SetGroupVisible("Support", false);
matrix->onItemClick = [](const PerformanceMatrixItem& person) {
    // person.label, person.group ...
};
```

### Showing it to a wider room

A populated matrix puts people's names next to ratings of their potential.
The element stores nothing and sends nothing; it only draws. When the room
should not see names, call `SetAnonymised(true)`. `Names` then shows as
counts, and no person answers the pointer or a click, so no tooltip gives a
name away. The demo's sample team is invented.

## What is not built yet

The reference cards and the population are built. Still to come, as the
proposal's Phase 3:

- **Calibration:** dragging a person to another box, with a veto callback;
  keyboard navigation; a scrollable list behind a cell's "+N more".
- **Interchange:** JSON and CSV through `UltraCanvasFileLoader`.
