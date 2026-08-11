# UltraCanvasMatrixDiagram

A matrix diagram — one of the Seven Management and Planning Tools — makes the
relationships between two or three ordered lists visible and countable. Rows and
columns are things; each intersection carries a symbol from a small named scale,
and the weights behind those symbols roll up into per-row and per-column totals.

**Status:** Phase 1 — the `L` and `T` shapes. `Y`, `X` and the QFD `House`
composite are planned; see
[`UltraCanvasMatrixDiagramProposal.md`](UltraCanvasMatrixDiagramProposal.md).

## Which element do I want?

| You have | Use |
|---|---|
| Continuous numeric cell values, a colour map, a colour bar | `UltraCanvasHeatmapChartElement` |
| Items positioned at continuous (x, y) in a 2×2 space (BCG, Eisenhower, risk) | `UltraCanvasQuadrantChart` |
| Rooms and their required adjacencies | `UltraCanvasAdjacencyDiagram` — it owns that data and draws its own matrix view |
| Named relationship levels between two or three ordered lists, with totals | **this element** |

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
| `MatrixShape::Y` | 3 | 3 | *planned* |
| `MatrixShape::X` | 4 | 4 | *planned* |
| `MatrixShape::House` | 2 | 2 | *planned* — QFD composite with the correlation roof |

Unimplemented shapes fall back to the `L` layout of panel 0 rather than drawing
nothing, so data set against them is still visible.

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
from the width, and the band height follows from the fit. The grid has first
claim on the vertical space, so a very long header is ellipsised rather than
squeezing the rows out.

Style is a plain struct of fields; read it, change what you need, set it back:

```cpp
MatrixDiagramStyle style = diagram->GetStyle();
style.alternateRowBands = true;
style.fillLabelBands = false;      // the solid label band behind row labels
style.markSize = 0.5;              // fraction of the cell's short side
diagram->SetStyle(style);
```

`SetDarkTheme(true)` applies the dark palette to the whole style; call it before
your own overrides, since it replaces the struct.

## Interaction

```cpp
diagram->onCellClick = [](const MatrixRef& ref) { /* ref.panel/row/col */ };
diagram->onRowClick    = [](int panel, int index, const MatrixItem& item) { };
diagram->onColumnClick = [](int panel, int index, const MatrixItem& item) { };
```

`MatrixRef` identifies a cell (`IsCell()`), or a whole row or column when the
click landed in a label band. `onCellClick` fires for empty cells too — check
`GetModel().LevelAt(...)` to tell them apart.

Hovering highlights the cell's row and column and shows a tooltip. Clicking a
legend entry filters the marks to that level; `SetLevelFilter("")` clears it.

## Interchange

```cpp
std::string csv = ExportMatrixCsv(model, 0);
bool ok = ImportMatrixCsv(csv, model, 0);
```

The CSV is a header row of column labels then one row per row item, with level
ids in the cells and empty fields for empty cells. Import fills in marks; it
does not invent items, so the sets and the scale must already exist. It parses
the whole file before touching the model, so a malformed file leaves your data
alone.

## Samples

`MatrixSamples::ImprovementTools()`, `MatrixSamples::EmployeeAllocation()` and
`MatrixSamples::QualityFunctionDeployment()` build the three models the demo
shows (`Apps/DemoApp/UltraCanvasMatrixDiagramExamples.cpp`).

## Validation

```cpp
MatrixValidation result = model.Validate();
if (!result.valid) { for (const auto& e : result.errors) Log(e); }
```

Checks the set count against the shape, panel set references, cell indices,
level ids and duplicate cells. `warnings` collects things that are drawable but
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
