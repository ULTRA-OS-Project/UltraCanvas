# UltraCanvasTableView

**Header:** `UltraCanvas/include/UltraCanvasTableView.h` (style model:
`UltraCanvasTableStyle.h`)
**Version:** 2.0.0
**Last Modified:** 2026-08-20

A styled table widget for presenting structured data in applications and
websites: typed cells (text, formatted numbers, check/cross glyphs, badge
chips, action buttons, custom renderers), section and total rows, a
highlighted column with a badge, sortable headers, row/cell selection,
virtual vertical scrolling — all driven by **region-based design presets**
shared through `TableStyleSheet`.

Use `UltraCanvasTableView` when you want to *display* tabular data
attractively (plan comparison matrices, data grids, financial statements,
dashboards). For *editing* tabular documents — formulas, multiple sheets,
file I/O — use `UltraCanvasSpreadsheet` instead. Background and rationale:
[`UltraCanvasTableViewProposal.md`](UltraCanvasTableViewProposal.md).

## Overview

The widget is presentation-first: the application owns the data and pushes
rows in; interaction arrives through callbacks (`onRowClick`,
`onCellAction`, `onColumnSort`, …). In-place cell editing is intentionally
not part of this component's first iteration.

Every visual decision comes from a `TableStyleSheet`, which layers
*region styles* in a fixed order — `WholeTable` → `BandedRows` →
`FirstColumn`/`LastColumn` → `HeaderRow`/`SectionRow`/`TotalRow` →
`HighlightColumn` → `HoverRow`/`SelectedRow` — each region overriding only
the fields it sets (the OOXML table-style model). Design presets fill the
sheet in; every field can be adjusted afterwards.

## Design presets

| `TableDesign` | Look | Typical use |
|---|---|---|
| `Comparison` | Hairline horizontal rules, grey section bands, centered columns, tinted highlight column with badge, CTA buttons | Pricing/feature matrices |
| `Professional` | Accent-colored header, zebra-striped rows, outer frame | Business data grids |
| `Minimal` | Booktabs/editorial: heavy top/bottom rules, thin header rule, no fills | Reports, documentation |
| `Financial` | Right-aligned numerics, red negatives, subtotal bands, double-rule total row | P&L, balance sheets, invoices |

Palettes are orthogonal to the design (`TablePaletteKind`): `Classic`
(blue), `Ocean`, `Olive`, `Slate`, `Warm` (the pricing-matrix red/cyan),
`Dark`, or a `Custom` palette you fill yourself:

```cpp
table->SetDesign(TableDesign::Professional,
                 TablePalette::BuiltIn(TablePaletteKind::Dark));

TablePalette brand = TablePalette::BuiltIn(TablePaletteKind::Custom);
brand.accent = Color(120, 40, 160);           // Corporate purple
table->SetDesign(TableDesign::Comparison, brand);
```

## Quick start — a comparison/pricing matrix

```cpp
#include "UltraCanvasTableView.h"
using namespace UltraCanvas;

auto table = CreateTableView("Plans", 20, 20, 470, 520);
table->SetDesign(TableDesign::Comparison);

table->SetColumns({
    TableViewColumn("", 0.0f, 2.0f),   // Feature labels; stretches (weight 2)
    TableViewColumn("S", 70.0f),       // Fixed 70 px plan columns
    TableViewColumn("M", 70.0f),
    TableViewColumn("L", 70.0f),
    TableViewColumn("XL", 70.0f),
});
table->SetHighlightedColumn(4, "Current");   // Tinted column + badge chip
table->SetSelectionMode(TableSelectionMode::NoSelection);

using C = TableViewCell;
table->AddSectionRow("Orders & Accounting");             // Grey band row
table->AddRow({C("Receipt archive"), C::MakeCheck(), C::MakeCheck(),
               C::MakeCheck(), C::MakeCheck()});
table->AddRow({C("Public API"), C::MakeCross(), C::MakeCross(),
               C::MakeCross(), C::MakeCheck()});
table->AddTotalRow({C(""), C::MakeButton("SELECT"), C::MakeButton("SELECT"),
                    C::MakeButton("SELECT"), C::MakeButton("SELECT")});

table->onCellAction = [table](int row, int col) {
    // The highlighted column's button renders filled; others outlined.
    printf("Plan chosen: %s\n", table->GetColumn(col).title.c_str());
};
window->AddChild(table);
```

## Cells

`TableViewCell` carries a `TableCellType` plus content:

| Factory | Renders as |
|---|---|
| `TableViewCell("text")` / `MakeText` | Plain text (implicit from a string) |
| `MakeNumber(value, decimals, thousands)` | Formatted number; red when negative and the sheet sets `negativeNumbersRed` |
| `MakeCheck()` | Check mark in `checkGlyphColor` |
| `MakeCross()` | Circled slash ("not included") in `crossGlyphColor` |
| `MakeMinus()` | Short dash ("not applicable") |
| `MakeBadge("ACTIVE")` | Rounded chip; `cell.background` tints it |
| `MakeButton("SELECT")` | Rounded action button → `onCellAction(row, col)` |
| type `Custom` + `customRenderer` | Your callback draws inside the cell rect |

Per-cell `textColor`, `background`, and `hAlign` overrides are available
but rarely needed — prefer the style sheet.

```cpp
TableViewCell gauge;
gauge.type = TableCellType::Custom;
gauge.number = 0.72;
gauge.customRenderer = [](IRenderContext* ctx, const Rect2Df& r,
                          const TableViewCell& cell) {
    Rect2Df track(r.x + 8, r.Center().y - 3, r.width - 16, 6);
    ctx->SetFillPaint(Color(230, 232, 235));
    ctx->FillRoundedRectangle(track, 3);
    track.width *= static_cast<float>(cell.number);
    ctx->SetFillPaint(Color(0, 150, 90));
    ctx->FillRoundedRectangle(track, 3);
};
```

## Rows

* `AddRow(cells)` / `AddRow(std::vector<std::string>)` — data rows.
* `AddSectionRow("caption")` — a full-width band styled by
  `TableRegion::SectionRow`; groups the data rows beneath it.
* `AddTotalRow(cells)` — a footer band styled by `TableRegion::TotalRow`
  (the `Financial` preset gives it the classic double rule).
* `SetCell`, `EditRow`, `ClearRows`, `GetRow(...).userData` for updates.

Rows differ in height by kind (`rowHeight`, `sectionRowHeight`,
`totalRowHeight` in the sheet). `GetContentHeight()` returns the full
content height so a table can be sized to fit without scrolling.

## Columns

`TableViewColumn{title, width, stretch}`: a positive `width` is a fixed
pixel width; `width == 0` makes the column share the leftover viewport
width proportionally to `stretch`. `minWidth` bounds both. Optional
`alignment` overrides the style regions; `sortable = true` enables
header-click sorting.

`SetHighlightedColumn(col, "badge")` tints the column with the
`HighlightColumn` region (a translucent wash composited over bands and
fills), renders the badge chip in a band above the header, and switches
that column's buttons to the filled `buttonHighlight` style.

## Sorting

`SortByColumn(col, ascending)` stable-sorts data rows — numerically when
both cells carry or parse to numbers, lexicographically otherwise. Rows
sort **within their section block**; section and total rows keep their
positions. Header clicks on sortable columns toggle direction and draw a
chevron; `onColumnSort(col, ascending)` fires after each sort.

## Selection and keyboard

`TableSelectionMode`: `NoSelection` (pure display), `RowSelection`
(default), `CellSelection`. Arrow keys move the selection and skip section
rows; PageUp/PageDown/Home/End navigate; Return/Space fires
`onRowActivate`. Hover and selection visuals come from the `HoverRow` /
`SelectedRow` regions.

## Callbacks

```cpp
std::function<void(int row)>            onRowClick, onRowDoubleClick, onRowActivate;
std::function<void(int row, int col)>   onCellClick;      // Data rows
std::function<void(int row, int col)>   onCellAction;     // Button cells
std::function<void(int col, bool asc)>  onColumnSort;
std::function<void(int row, int col)>   onSelectionChange; // (-1,-1) = cleared
```

## Styling beyond the presets

```cpp
TableStyleSheet& s = table->EditStyle();
s.rowHeight = 44;                                   // Comfortable density
s.EditRegion(TableRegion::HeaderRow).background = Color(20, 60, 120);
s.EditRegion(TableRegion::HeaderRow).textColor = Colors::White;
s.horizontalGrid = TableBorder(1.0f, Color(235, 236, 238));
table->StyleChanged();                              // Re-layout + redraw
```

A fully custom look starts from any preset:
`TableStyleSheet mine = TableStyles::Create(TableDesign::Minimal); …;
table->SetStyle(mine);`

## Demo

`Apps/DemoApp/UltraCanvasTableViewExamples.cpp` ("Table View" in the demo
app) shows the three designs side by side: the pricing matrix with
highlight column, badge and CTA buttons; a sortable Professional grid with
number and badge cells; and a Financial income statement with section
bands, red negatives and a double-rule total row.

## Notes and current limits

* Vertical scrolling is virtualized; horizontal scrolling is not yet
  implemented — size fixed columns to the viewport or use stretch columns.
* In-place editing, multi-level headers, `ToHTML()`/`ToCSV()` export and
  the spreadsheet's `ApplyTableStyle` consumer are the next phases of
  [`UltraCanvasTableViewProposal.md`](UltraCanvasTableViewProposal.md).
* `{"a", "b"}` brace lists of `std::string` variables are ambiguous
  between the two `AddRow` overloads; string *literals* resolve to the
  `std::vector<std::string>` overload, and mixed content uses
  `TableViewCell` initializers as in the examples above.
