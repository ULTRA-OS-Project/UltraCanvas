# UltraCanvasMatrixDiagram

<!-- doc-check: int row; int col; void Log(const std::string& line); -->

A matrix diagram — one of the Seven Management and Planning Tools — makes the
relationships between two to four ordered lists visible and countable. Rows and
columns are things; each intersection carries a symbol from a small named scale,
and the weights behind those symbols roll up into per-row and per-column totals.

**Status:** Phases 1–3 — the `L`, `T`, `X`, `Y` and QFD `House` shapes, the
form header band, cell tint, hover bands that run across panels, a ranked bar
strip for the column totals, and spreadsheet import. Phase 4 (pan/zoom,
keyboard navigation, JSON, editing) is still open; see
[`UltraCanvasMatrixDiagramProposal.md`](UltraCanvasMatrixDiagramProposal.md).

## Which element do I want?

| You have | Use |
|---|---|
| Continuous numeric cell values, a colour map, a colour bar | `UltraCanvasHeatmapChartElement` |
| Items positioned at continuous (x, y) in a 2×2 space (BCG, Eisenhower, risk) | `UltraCanvasQuadrantChart` |
| Rooms and their required adjacencies | `UltraCanvasAdjacencyDiagram` — it owns that data and draws its own matrix view |
| Named relationship levels between two to four ordered lists, with totals | **this element** |

The distinction that matters is the cell datum. A heatmap cell holds a *number*
and encodes it as a fill; a matrix diagram cell holds a *named level* and draws
it as a glyph on an unfilled cell. Levels also carry weights, which is what lets
the diagram compute totals — something a heatmap has no concept of.

## Header

```cpp
#include "Plugins/Diagrams/UltraCanvasMatrixDiagram.h"   // the element
#include "Plugins/Diagrams/UltraCanvasMatrixModel.h"     // the data model alone
```

`UltraCanvasMatrixModel.h` is UI-free: it includes nothing from the widget or
render stack, so model code (and its arithmetic) can be built and tested without
a window. `Tests/MatrixModelTest.cpp` does exactly that.

## Class hierarchy

```
UltraCanvasUIElement
  └── UltraCanvasChartElementBase
        └── UltraCanvasMatrixDiagram
```

---

## The three primitives

**Item sets** are the ordered lists. **Panels** cross one set with another.
**Cell marks** are the symbols at the intersections, taken from the panel's
**relationship scale**.

```cpp
MatrixModel model;
model.title = "Application of improvement tools";
model.shape = MatrixShape::L;

model.AddSet("departments", "Departments", {"HR", "Finance", "Purchasing"});
model.AddSet("tools", "Improvement tools", {"SPC", "KPIs", "Kaizen"});
model.AddPanel("departments", "tools", MatrixScale::Strength531());

model.SetCell(0, "HR",      "KPIs",   "medium");
model.SetCell(0, "Finance", "SPC",    "strong");
model.SetCell(0, "Finance", "Kaizen", "weak");

auto diagram = CreateMatrixDiagramElement("matrix", 10, 10, 700, 420, model);
diagram->SetTotals(MatrixTotals::Both);
```

`AddSet` uses each label as its own item id, which is what you want for a
one-off diagram. Build `MatrixItemSet` directly when you need distinct ids or
per-row importance weights.

Cells are stored **sparsely** — set the marks you have; the rest stay empty.

## Shapes

| Shape | Sets | Panels | Meaning |
|---|---|---|---|
| `MatrixShape::L` | 2 | 1 | A × B — the basic form |
| `MatrixShape::T` | 3 | 2 | B × A and A × C, A being the shared row axis |
| `MatrixShape::X` | 4 | 4 | A × B, B × C, C × D, D × A — four panels round a hub |
| `MatrixShape::Y` | 3 | 3 | A × B, B × C, C × A — three rhombic panels between seams 120° apart |
| `MatrixShape::House` | 2 | 1–2 | the QFD House of Quality: an L plus the correlation roof |

An `X`, `Y` or `House` whose model the shape cannot place (see *Validation*)
falls back to the `L` layout of panel 0 rather than drawing nothing, so its
data is still visible.

A `T` shares its row axis between two wings. Panel 0 is the left wing, panel 1
the right, and the shared row labels are drawn once between them:

```cpp
model.shape = MatrixShape::T;
model.AddSet("employees", "Employees", {"Harvey", "Sami", "Emir"});
model.AddSet("projects",  "Projects",  {"Paper usage", "Energy saving"});
model.AddSet("skills",    "Skills",    {"SPC", "5S", "FMEA"});

// The two wings ask different questions, so they carry different scales.
model.AddPanel("employees", "projects", MatrixScale::Presence());
model.AddPanel("employees", "skills",   MatrixScale::HighMediumLow());
```

That is why the scale lives on the **panel** and not on the model.

### The X shape

An `X` puts its four sets on the arms of a cross, in model order: `sets[0]`
left, `sets[1]` top, `sets[2]` right, `sets[3]` bottom. The top and bottom arms
list their items as rows, the left and right arms as columns, and each panel
fills the quadrant between its two arms — so every panel must pair a left/right
set with a top/bottom one. Which of the two is the panel's row set is up to
you: a panel whose rows are a side arm's items is turned to fit.

```cpp
MatrixModel x;
x.shape = MatrixShape::X;
x.AddSet("suppliers", "Suppliers",        {"Supplier A", "Supplier B"});   // left
x.AddSet("storage",   "Storage areas",    {"W/H 1", "W/H 2"});             // top
x.AddSet("customers", "Customers",        {"P-1042", "P-1187"});           // right
x.AddSet("lines",     "Production lines", {"Line 1", "Line 2"});           // bottom

x.AddPanel("suppliers", "storage",   MatrixScale::HighMediumLow());   // top-left, turned
x.AddPanel("storage",   "customers", MatrixScale::HighMediumLow());   // top-right
x.AddPanel("lines",     "customers", MatrixScale::HighMediumLow());   // bottom-right
x.AddPanel("lines",     "suppliers", MatrixScale::HighMediumLow());   // bottom-left

auto xDiagram = CreateMatrixDiagramElement("supply", 10, 10, 760, 560, x);
xDiagram->SetHubMode(MatrixHubMode::Captions);
```

The middle of the cross is the **hub**. `MatrixHubMode::Blank` leaves it empty
and draws each set's title at the outer end of its arm; `MatrixHubMode::Captions`
puts the four titles in boxes in the hub with arrows out to their arms, and the
hub grows to hold them. One cell width serves both left quadrants and both right
ones, one cell height every row, so the quadrants always line up across the hub.

### The Y shape

A `Y` runs three seams out of a centre point, 120° apart — `sets[0]` straight
up, `sets[1]` down-right, `sets[2]` down-left. Each seam is a band listing its
set, and between two seams sits the panel crossing their sets, drawn as a
rhombus whose cells step along both seams. A row starts at its label on one
seam and runs parallel to the other.

```cpp
MatrixModel y;
y.shape = MatrixShape::Y;
y.AddSet("requirements", "Requirements", {"Quiet", "Compact"});
y.AddSet("functions",    "Functions",    {"Generate suction", "Store energy"});
y.AddSet("components",   "Components",   {"Motor", "Battery pack"});
y.AddPanel("requirements", "functions",    MatrixScale::QFD());
y.AddPanel("functions",    "components",   MatrixScale::QFD());
y.AddPanel("components",   "requirements", MatrixScale::QFD());
```

The cell size is the largest that fits the whole figure, labels and titles
included, between the style's `minCellSize` and `maxCellSize`.

`X` and `Y` draw no totals gutters — their outer edges are other panels' arms or
open space, and the totals are one call away on the model.

### The House of Quality

`MatrixShape::House` is the QFD composite: customer needs (`sets[0]`) against
technical requirements (`sets[1]`) in panel 0, and over the requirements'
header a **roof** — a second, *symmetric* panel that relates the requirements
to each other. Round the grid it adds an importance column (the rows'
`importance`), a direction-of-improvement row and a target row from the
requirements' own fields:

```cpp
MatrixModel house = MatrixSamples::QualityFunctionDeployment();   // needs x requirements
house.shape = MatrixShape::House;

MatrixItemSet* reqs = house.FindSet("requirements");
reqs->items[0].improvement = MatrixImprovement::Minimize;   // Maximize | Minimize | Target
reqs->items[0].target = "< 11 kg";                          // free text, read by people

MatrixPanel& roof = house.AddPanel("requirements", "requirements",
                                   MatrixScale::Correlation());
roof.symmetric = true;
house.SetCell(1, "Frame mass", "Gear range", "strongnegative");

auto houseDiagram = CreateMatrixDiagramElement("hoq", 10, 10, 900, 680, house);
houseDiagram->SetTotals(MatrixTotals::Both);
houseDiagram->SetRollUp(MatrixRollUp::Bars);
```

A **symmetric panel** crosses one set with itself and keeps one cell per pair,
above the diagonal: `SetCellAt(1, 4, 0, ...)` is stored as `(0, 4)`, `LevelAt`
and `FindCell` answer either way round, and the diagonal has no cell at all.
`MatrixScale::Correlation()` is the roof's usual scale — `++`, `+`, `−`, `−−`,
with signed weights that the roll-ups never use. The roof is drawn at 45°, one
diamond per pair, each standing over the boundary between its two columns; the
diagram trades cell width for row height so the roof cannot squeeze the rows
(`MatrixDiagramStyle::maxRoofShare`, `minRowAspect`). `SetShowImportance(false)`
hides the importance column. A House without a roof panel draws without one.

`MatrixSamples::HouseOfQuality()` is a complete example.

### The form header band

Many printed matrix diagrams carry a form header — PROJECT, AREA, TITLE, TEAM,
DATE. Put the fields on the model and the element draws them as a row of boxes
above the grid, in any shape:

```cpp
x.headerFields = {
        {"PROJECT", "Warehouse consolidation"},
        {"TEAM",    "Ops planning"},
        {"DATE",    "2026-10-13"},
};
xDiagram->SetShowHeaderBand(true);   // the default; false hides it
```

## Relationship scales

A scale is an ordered list of levels, strongest first. Each level carries an id,
a label, a mark shape, a colour and a **weight**.

```cpp
MatrixScale scale;
scale.title = "Relationship";
scale.levels.emplace_back("must",   "Must",   Color(214, 45, 45, 255), 5.0);
scale.levels.emplace_back("should", "Should", Color(35, 70, 180, 255), 3.0,
                          MatrixMarkShape::Ring);
```

Built-in presets:

| Preset | Levels and weights |
|---|---|
| `MatrixScale::QFD()` | strong 9 (disc), moderate 3 (ring), weak 1 (triangle) |
| `MatrixScale::Strength531()` | strong 5, medium 3, weak 1 — all discs |
| `MatrixScale::HighMediumLow()` | high 3, medium 2, low 1 (hollow) |
| `MatrixScale::Presence()` | a single level, weight 1 |

**Weights are data, not convention.** QFD's 9/3/1 is the best-known scale but
5/3/1 is at least as common, and the totals differ: the same eight marks total
18 on 5/3/1 and 22 on 9/3/1.

Mark shapes are `Disc`, `Ring`, `Triangle`, `Square`, `Diamond`, `Cross` and
`Text`. Prefer a scale with at least one `Ring`: a scale distinguished only by
hue disappears in greyscale and for colour-blind readers. `Text` draws
`MatrixLevel::glyph` instead of a shape, which is how a scale mixes coloured
dots with letter codes under one legend.

## Roll-ups

The point of the weights is that they sum.

```cpp
int col = 0, row = 1;                       // item indices in panel 0's column and row sets
double score = model.ColumnScore(0, col);   // Σ rowImportance × cellWeight
double total = model.RowScore(0, row);
std::vector<int> ranked = model.RankColumns(0);   // descending, stable ties
```

`MatrixItem::importance` defaults to `1.0`, so an unweighted matrix gets a plain
sum of cell weights out of the same formula. Set real importances for QFD:

```cpp
MatrixItemSet needs("needs", "Customer needs");
needs.items.emplace_back("light",   "Light to carry",         5.0);
needs.items.emplace_back("comfort", "Comfortable over 10 km", 4.0);
model.sets.push_back(needs);
```

Show the totals with `SetTotals`:

```cpp
diagram->SetTotals(MatrixTotals::Both);      // NoTotals | Rows | Columns | Both
```

Row totals appear in a gutter to the right of the grid, column totals in a
gutter underneath — the presentation the printed form uses. Because they are
computed from the marks they are consistent by construction, which a hand-built
version is not.

The column totals can instead be a **strip of bars** with each column's rank,
the way a QFD exercise presents its result; the top three ranks stand out:

```cpp
diagram->SetRollUp(MatrixRollUp::Bars);      // Numbers is the default
std::vector<double> shares = model.RelativeColumnWeights(0);   // percent of the total
```

A column's tooltip gives its total, rank, share and target.

## Presentation

```cpp
diagram->SetDarkTheme(true);
diagram->SetHeaderFit(MatrixHeaderFit::Auto);
diagram->SetShowAxisTitles(true);
diagram->SetShowLegend(true);
diagram->SetLegendPosition(ChartLegendPosition::BottomCenter);
diagram->SetShowCellValues(false);           // draw each cell's weight as text
```

`MatrixHeaderFit::Auto` measures the column labels once and picks the cheapest
form that fits — `Horizontal`, then `Wrapped`, then `Rotated45`, then
`Rotated90`. It never iterates: the cell width settles first, the fit follows
from the width, and the band height follows from the fit. A rotated header leans
right past its column, so when the fit is rotated the cells are then narrowed
just enough for the last label to end inside the element - which cannot change
the fit, because a rotated label does not depend on the cell width. The grid has first
claim on the vertical space, so a very long header is ellipsised rather than
squeezing the rows out.

Style is a plain struct of fields; read it, change what you need, set it back:

```cpp
MatrixDiagramStyle style = diagram->GetStyle();
style.alternateRowBands = true;
style.fillLabelBands = false;      // the solid label band behind row labels
style.markSize = 0.5;              // fraction of the cell's short side
style.tintCellsByLevel = true;     // fill each marked cell with its level's colour
style.cellTintAlpha = 56;          // ... this faintly, under the mark
diagram->SetStyle(style);
```

The tint gives a heatmap-like read of where the strong relationships cluster;
the mark stays the datum, and a level filtered out through the legend fades its
tint along with its mark.

`SetDarkTheme(true)` and `SetDarkTheme(false)` swap the palette and nothing
else: sizes, fonts and switches such as `alternateRowBands` or
`tintCellsByLevel` survive a round trip. Colours you set yourself are replaced,
so set them after choosing the theme. `MatrixDiagramStyle::ApplyDarkTheme()`
and `ApplyLightTheme()` do the same to a style you hold.

## Interaction

```cpp
diagram->onCellClick = [](const MatrixRef& ref) { /* ref.panel/row/col */ };
diagram->onRowClick    = [](int panel, int index, const MatrixItem& item) { };
diagram->onColumnClick = [](int panel, int index, const MatrixItem& item) { };
```

`MatrixRef` identifies a cell (`IsCell()`), or a whole row or column when the
click landed in a label band. `onCellClick` fires for empty cells too — check
`GetModel().LevelAt(...)` to tell them apart.

Hovering highlights the cell's row and column and shows a tooltip. The bands
follow the *items*, not the panel: a T's shared row lights up in both wings, and
an X's column lights up in both quadrants that list it. In an `X` or `Y`, a
click on an arm label reports a whole row or column of the first panel that
lists the item. Clicking a
legend entry filters the marks to that level; `SetLevelFilter("")` clears it.

## Interchange

```cpp
std::string csv = ExportMatrixCsv(model, 0);
bool ok = ImportMatrixCsv(csv, model, 0);
```

The CSV is a header row of column labels then one row per row item, with level
ids in the cells and empty fields for empty cells. `ExportMatrixCsv(model, 0,
true)` adds an `Importance` column after the row labels, and `ImportMatrixCsv`
reads it back. Import fills in marks; it does not invent items, so the sets and
the scale must already exist. It parses the whole file before touching the
model, so a malformed file leaves your data alone.

To build a model **from** a spreadsheet, use `ImportMatrixTable`. It creates the
items from the header row and the first column, and reads each mark by level
id, label (any case), glyph or weight — so a QFD sheet typed with `9`, `3`, `1`
imports as it is. An `Importance` (or `Weight`) second column sets the row
importances. Numbers are dot-decimal whatever the locale.

<!-- doc-check: std::string sheetCsv; -->

```cpp
MatrixModel imported;
std::string error;
if (!ImportMatrixTable(sheetCsv, MatrixScale::QFD(), imported,
                       "Customer needs", "Technical requirements", &error)) {
    Log(error);   // names the line and the mark it could not read
}
```

## Samples

`MatrixSamples::ImprovementTools()` (L), `MatrixSamples::EmployeeAllocation()`
(T), `MatrixSamples::QualityFunctionDeployment()` (L, weighted),
`MatrixSamples::SupplyChain()` (X, with a form header),
`MatrixSamples::DesignTraceability()` (Y) and `MatrixSamples::HouseOfQuality()`
(House) build the models the demo shows
(`Apps/DemoApp/UltraCanvasMatrixDiagramExamples.cpp`).

## Validation

<!-- doc-check: void Log(const std::string& message); -->

```cpp
MatrixValidation result = model.Validate();
if (!result.valid) { for (const auto& e : result.errors) Log(e); }   // Log: your app's logger
```

Checks the set count against the shape, panel set references, cell indices,
level ids and duplicate cells. For an `X` it also checks that every panel pairs
a left/right set with a top/bottom one and that no two panels claim the same
quadrant; for a `Y`, that every panel crosses two different sets among the first
three and that no pair appears twice; for a `House`, that panel 0 crosses two
different sets and any further panel is the symmetric roof over its columns.
A cell of a symmetric panel on or below the diagonal is an error. `warnings` collects things that are drawable but
suspicious, such as an empty item set.

The guarded mutators (`SetCell`, `SetCellAt`) already reject unknown panels,
items and level ids, so `Validate()` mainly matters when you have assembled a
`MatrixModel` by hand.

## See also

- [`UltraCanvasMatrixDiagramProposal.md`](UltraCanvasMatrixDiagramProposal.md) —
  the research behind the design, and the roadmap for the remaining shapes
- [`UltraCanvasHeatmapChart.md`](UltraCanvasHeatmapChart.md) — the numeric-cell sibling
- [`UltraCanvasAdjacencyDiagramExamples.md`](UltraCanvasAdjacencyDiagramExamples.md) —
  room adjacency, including its own matrix view
- [`UltraCanvasQuadrantChartExamples.md`](UltraCanvasQuadrantChartExamples.md) —
  the other component that calls itself a "matrix"
