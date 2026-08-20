# UltraCanvasTableView & Predefined Table Designs — Investigation

**Status:** Design survey (pre-implementation research)

Author: UltraCanvas Framework
Last Modified: 2026-08-20

This document investigates how UltraCanvas should offer **predefined table
designs** to application programmers: should they be integrated into the
existing spreadsheet component, delivered as a dedicated table widget for
apps and websites, or both? It also catalogues the design presets worth
shipping, using a real-world feature/pricing comparison table (the
Lexware-style plan matrix) as the motivating example.

---

## 1. The motivating example

The reference screenshot is a classic **plan comparison matrix**: products
S / M / L / XL as columns, features as rows. Its anatomy is worth
dissecting because it contains almost every structural feature a styled
table needs — and almost none of them exist in the framework today:

| # | Feature in the screenshot | Structural requirement |
|---|---|---|
| A1 | "Aktuelle Version" flag above the XL column | Column badge/ribbon attached to a column, rendered above the header |
| A2 | Highlighted recommended column (tinted background, accent border) | Per-column emphasis style layered over the base design |
| A3 | Grey band rows "Auftrag & Buchhaltung", "Steuerberater Zugang", … | Section rows spanning all columns, grouping the rows beneath |
| A4 | ✓ / ⊘ marks | Boolean cell content type rendered as glyphs (check / not-included), not text |
| A5 | "AUSWÄHLEN" buttons in the last row | Action (button) cells; the highlighted column's button uses the filled accent variant |
| A6 | First column left-aligned text, feature columns centered | Per-column alignment defaults |
| A7 | Hairline row separators, no vertical grid | Border scheme is part of the design, not per-cell decoration |
| A8 | White page-style background, generous padding | Density/padding metrics belong to the design preset |

A programmer should get this table with roughly: *create table view, set
columns and rows, mark one column highlighted, apply
`TableDesign::Comparison`* — not by hand-painting hundreds of cell styles.

---

## 2. What exists in the framework today

Inventory (verified against the current tree):

1. **`UltraCanvasSpreadsheet`** (`include/UltraCanvasSpreadsheet*.h`,
   `core/UltraCanvasSpreadsheet*.cpp`, ~13,500 lines) — a full workbook
   component: multi-sheet, formula engine, undo/redo, clipboard,
   find/replace, sort/filter, freeze panes, merged cells, ODS/XLSX/CSV
   file I/O, print settings. Styling is **per-cell** via `CellStyle`
   (`UltraCanvasSpreadsheetTypes.h:493`) — font, fill, 13 border styles,
   number formats, alignment, rotation — plus `ConditionalFormatRule`
   types. There is **no table-style concept**: nothing generates styles
   by region (header row / banded rows / total row), and the XLSX I/O
   does not read or write table parts or `<tableStyles>`.
2. **`UltraCanvasTableView.h`** (`include/UltraCanvasTableView.h`,
   1,160 lines) — a header-only draft of an interactive table with
   sorting, filtering, selection, column resize and in-cell editing. It
   is listed in the UI element catalogue
   (`UltraCanvasUIElements.md`), but it is **dead code**: no `.cpp` in the
   repository includes it, and it cannot compile as written (e.g. the
   `ctx->PaintWidthColorheaderTextColor)` lines, `DrawHeader()` /
   `DrawRowCells(...)` signature mismatches, legacy `Rect2D`/`DrawFilledRect`
   free-function calls, and a `static` global inside `extern "C"`). The
   DemoApp shows a "Table View Component — Not Implemented" placeholder
   (`Apps/DemoApp/UltraCanvasDemoExamples.cpp:91`). Styling-wise it only
   has flat colors (header background, alternate row, grid line) — no
   designs, no section rows, no cell content types.
3. **`UltraCanvasListView`** — single-column list; no columns.
4. **Design-preset precedent** — presentation elements already follow a
   uniform pattern this proposal should reuse:
   * `KanbanBoardStyle` struct + `KanbanBoardStyles::Create(KanbanDesign)`
     factory with six named presets (`Plugins/Charts/UltraCanvasKanbanBoard.h:267`);
   * `PertNodeDesign` (geometry) kept orthogonal to
     `PertChartPaletteKind` / `PertChartPalette::BuiltIn(kind)` (colors);
   * survey docs `SWOTDiagramDesignVariants.md` and
     `CircleDiagramInfographicVariants.md` as the research-first workflow.
5. **Web/HTML side** — the framework targets WebAssembly, and
   `HTMLReader` already parses/builds HTML tables
   (`HTMLElementBuilder::BuildTable`, `HTMLStyleResolver` display types
   `Table/TableRow/TableCell`), so a table widget is usable on websites
   through the WASM build and could later back `<table>` rendering.
6. **`UltraCanvasGitGraph`** explicitly anticipates being paired with "an
   external UltraCanvasTableView" via its row-alignment API
   (`Plugins/Diagrams/UltraCanvasGitGraph.h:191`) — a second internal
   consumer waiting for the widget.

**Conclusion of the inventory:** the framework has a heavyweight editable
workbook and a broken, unshipped table draft — but no working component
for the by-far most common UI need: *display structured data
attractively*. Predefined designs currently have nowhere to live.

---

## 3. Spreadsheet integration vs. dedicated widget

### 3.1 What each option really means

**Option A — integrate into `UltraCanvasSpreadsheet`** ("Format as Table",
as in Excel/LibreOffice Calc): apply a named table style to a cell range;
the component expands it into header/banded/total `CellStyle`s.

**Option B — dedicated `UltraCanvasTableView` widget**: a presentation/
data-grid element (fixed column schema, rows of typed content) with design
presets, for application UI and websites.

### 3.2 Comparison

| Criterion | A: Spreadsheet | B: Table widget |
|---|---|---|
| Target use | Editing tabular *documents* (workbooks) | Displaying structured *data* in app UI (plan matrices, result grids, dashboards) |
| Weight carried | Formula engine, formula bar, sheet tabs, undo stack, row/column headers, unlimited grid | Only columns + rows + styles; suitable inside dialogs, panels, web pages |
| Interaction model | Cell cursor, range selection, in-cell editing | Row selection, sorting, hover, optional editing |
| Cell content | Text/number/formula with `NumberFormat` | Needs icons (✓/⊘), badges, buttons, progress, custom renderers — wrong things to put in spreadsheet cells |
| Structural styling | Range-based; table style = generated `CellStyle`s | Native regions: header, sections, bands, total row, highlighted column |
| The screenshot | Achievable but clumsy (merged band rows, no buttons, chrome must be hidden) | The design target |
| File formats | XLSX/ODS table styles round-trip is a real user expectation | HTML/CSS export is the natural counterpart |

Embedding a spreadsheet to show a pricing table is the wrong tool
(≈13.5k lines of workbook machinery for a static matrix); conversely, a
widget can never satisfy "my imported XLSX had a banded table". The two
options serve different callers and **do not compete** — what would be a
mistake is implementing two unrelated styling systems.

### 3.3 Recommendation

**Both, sharing one style definition — widget first.**

1. Define a reusable, region-based **`TableStyleSheet`** (§4) in a small
   standalone header, following the OOXML/ODF table-style region model.
   This is the single source of truth for "predefined table designs".
2. **Rebuild `UltraCanvasTableView`** as a real, compiling core element
   (the name, catalogue row and factory functions already exist; the dead
   header is replaced in place). It consumes `TableStyleSheet` natively
   and ships the preset gallery (§5). This is the deliverable programmers
   use in apps and websites.
3. Add **`UltraCanvasSpreadsheet::ApplyTableStyle(range, styleSheet)`** —
   "Format as Table" that expands the same `TableStyleSheet` into
   `CellStyle`s over a range (static expansion first; live table objects
   with structured references are explicitly out of scope). One gallery,
   two consumers — exactly how Excel shares its table-style gallery
   between tables and ranges.

This mirrors the repo's own precedent: geometry/design enum + style
struct + `Create()` factories (Kanban), palettes orthogonal to designs
(PERT).

---

## 4. The shared style model: `TableStyleSheet`

OOXML and ODF both describe table styles as **a set of region styles**
layered in a defined order, not per-cell formatting. The same model fits
both consumers here:

```cpp
// UltraCanvasTableStyle.h (new, no spreadsheet dependency)
namespace UltraCanvas {

enum class TableRegion {
    WholeTable,      // Base: font, background, outer border, cell padding
    HeaderRow,       // First (or multi-level) header
    TotalRow,        // Footer/total band
    FirstColumn,     // Row-label column emphasis
    LastColumn,
    BandedRows,      // Zebra stripe (second band)
    BandedColumns,
    SectionRow,      // Full-width group band (screenshot A3)
    HighlightColumn, // Recommended/current column (screenshot A2)
    HoverRow,        // Widget-only interaction states
    SelectedRow
};

struct TableRegionStyle {
    // Deliberately small; maps 1:1 onto spreadsheet CellStyle fields.
    std::optional<Color> background;
    std::optional<Color> textColor;
    std::optional<CellFont> font;          // reuse spreadsheet CellFont
    std::optional<CellBorders> borders;    // reuse spreadsheet CellBorders
    std::optional<HorizontalAlignment> hAlign;
    std::optional<float> paddingX, paddingY;
};

struct TableStyleSheet {
    std::string name;                       // "Comparison", "Financial", …
    std::map<TableRegion, TableRegionStyle> regions;
    int rowBandSize = 1;                    // Stripe thickness
    int colBandSize = 1;
    // Widget-only chrome (ignored by the spreadsheet consumer):
    float cornerRadius = 0.0f;
    bool showVerticalGrid = false;
    bool showHorizontalGrid = true;
    Color checkGlyphColor  = Color(0, 150, 90);
    Color crossGlyphColor  = Color(180, 180, 180);
};

enum class TableDesign { Comparison, Professional, Minimal, Material,
                         DarkDashboard, Financial, Report, Heatmap };

namespace TableStyles {
    TableStyleSheet Create(TableDesign design);
    TableStyleSheet Create(TableDesign design, const TablePalette& palette);
}
} // namespace UltraCanvas
```

Notes:

* `CellFont` / `CellBorders` / `HorizontalAlignment` already live in
  `UltraCanvasSpreadsheetTypes.h`, which is a plain types header. Either
  include it, or (cleaner) move those three types into a shared
  `UltraCanvasTableStyleTypes.h` that both headers include — decided at
  implementation time.
* `std::optional` fields make regions **layerable**: resolution order is
  WholeTable → Banded → First/Last column → Section → Header/Total →
  Highlight → Hover/Selected, later regions overriding only the fields
  they set (same as OOXML `dxf` layering).
* **`TablePalette`** (accent, neutral, band tint, header tint, glyph
  colors) is orthogonal to the design, exactly like
  `PertChartPalette`: `Create(TableDesign::Comparison, myBrandPalette)`
  restyles the screenshot table to a corporate brand without a new design.

---

## 5. Predefined design catalogue (design proposals)

Presets fill a `TableStyleSheet`; every field remains adjustable
afterwards (`EditStyle()` + `StyleChanged()`, as on the Kanban board).
Proposed gallery, ordered by expected real-world frequency:

**D1. Comparison / Pricing matrix** *(the screenshot)* — white base, no
vertical grid, hairline horizontal rules; grey full-width section bands
(bold, dark text); centered feature columns; boolean cells as ✓ (green) /
⊘ (grey outline); one `HighlightColumn` with tinted background + accent
top badge ("current version" ribbon); final action row with outline
buttons, filled accent button in the highlighted column. Use: plan
selection, feature matrices, product comparisons.

**D2. Professional / Banded grid** — the Excel "Medium" family look:
solid accent header row (white bold text), zebra `BandedRows` in a light
tint of the accent, thin outer border, optional `FirstColumn` bold. The
default for business data grids.

**D3. Minimal / Editorial (booktabs)** — publication style: heavy top and
bottom rules, thin rule under the header, **no** vertical rules, no fills,
generous padding. Use: reports, documentation, scientific output; the
best-practice typographic table.

**D4. Material / Web data-table** — 1px neutral row dividers, medium-grey
uppercase-ish header with sort chevrons, `HoverRow` tint, comfortable/
dense density switch (row height 52/36 px), pagination-friendly footer.
Use: admin panels, CRUD lists — the "website table" default.

**D5. Dark dashboard** — near-black panel background, subtle band
striping, high-contrast accent header underline, glyphs and numerics in
saturated accent colors; pairs with the existing Dark chart/Kanban
presets for dashboard composition.

**D6. Financial statement** — right-aligned numerics with thousands
separators (reuses `NumberFormat`), negatives red or parenthesised,
indent levels in the label column, `SectionRow` as subtotal bands with a
single top rule, `TotalRow` with the classic **double rule**. Use: P&L,
balance sheets, invoices.

**D7. Report / Print monochrome** — grayscale-safe: black rules, white
fills, banding via 5% grey; survives printing and PDF export. The
spreadsheet consumer's natural default.

**D8. Heatmap accent** — cell backgrounds driven by a value → color scale
(reuses the chart `Colormap` / conditional-formatting `ColorScale`
machinery) with automatic light/dark text contrast. Use: metric matrices,
calendar-style summaries.

Palettes (orthogonal, D×P combinations): **Classic** (blue accent),
**Ocean**, **Olive**, **Slate**, **Warm**, **Dark**, **Custom** (accent +
neutral supplied; everything else derived), matching the PERT palette
naming so galleries feel uniform across the framework.

---

## 6. Widget feature set required by the designs

Beyond what the dead draft already sketched (sorting, filtering, column
resize, selection, editing hooks — worth keeping), the designs above
require:

* **F1 — Section rows**: a row flagged `Section` spans all columns and
  takes the `SectionRow` region style (screenshot A3; financial
  subtotals).
* **F2 — Column emphasis**: `SetColumnHighlighted(col, badgeText)` →
  `HighlightColumn` region + optional badge/ribbon above the header (A1,
  A2).
* **F3 — Typed cell content**: `Text`, `Number(NumberFormat)`,
  `Check`/`Cross`/`Minus` glyphs (A4), `Badge` (chip with tinted
  background), `Button` (label + `onCellAction` callback; outline/filled
  variants) (A5), `Progress`, and an escape hatch
  `std::function<void(IRenderContext*, const Rect2Df&, const TableCell&)>`
  custom renderer.
* **F4 — Header model**: single header row first; multi-level grouped
  headers (merged header cells) as a follow-up.
* **F5 — Total/footer row** taking the `TotalRow` region.
* **F6 — Interaction states**: hover row, selected row/cell from the
  style sheet, not hard-coded colors.
* **F7 — Frozen header + optional frozen first column**, virtual
  scrolling for large row counts (the draft already scrolled; keep it).
* **F8 — Column sizing**: fixed px, min/max, and stretch weights;
  participates in CSS layout (`Docs/CSSLayout.md`) like other elements.
* **F9 — Keyboard navigation & accessibility**: arrow/home/end/page
  navigation (draft had it), focus ring from the style sheet.
* **F10 — Export**: `ToHTML()` emitting a self-contained `<table>` +
  scoped CSS matching the active style sheet (the "websites" story beyond
  WASM), and `ToCSV()` for data.

Placement: **core element** (`include/UltraCanvasTableView.h` +
`core/UltraCanvasTableView.cpp`), not a plugin — the catalogue already
lists it beside ListView/TreeView/Spreadsheet, and DemoApp has a slot
(`CreateTableViewExamples()`) waiting to be filled.

---

## 7. Spreadsheet consumer: "Format as Table"

* **S1 — `ApplyTableStyle(const CellRange&, const TableStyleSheet&, TableStyleOptions)`**
  with options `{hasHeaderRow, hasTotalRow, bandedRows, bandedColumns,
  emphasizeFirstColumn, emphasizeLastColumn}` (the six checkboxes Excel
  shows). Expansion is a pure function region → `CellStyle` per cell,
  recorded as one undo group.
* **S2 — Gallery reuse**: the same `TableStyles::Create(...)` presets are
  offered by the host application's UI (UltraTexter/Calc-style apps);
  no spreadsheet-specific style definitions.
* **S3 — Static first**: no live table object (auto-expansion, structured
  references, filter buttons bound to the table). If needed later, that
  is a separate proposal; the style sheet model doesn't change.
* **S4 — XLSX/ODS mapping (later)**: import can map document table styles
  onto the nearest preset or a `Custom` style sheet; export writes the
  expanded cell styles today (lossless visually), real `<tableStyles>`
  parts only if S3 ever lands.

---

## 8. Suggested phasing

* **P1 — Widget + gallery core**: `UltraCanvasTableStyle.h`
  (`TableStyleSheet`, regions, palettes), rewritten compiling
  `UltraCanvasTableView` with F1–F3, F5–F7 and designs **D1, D2, D3, D6**
  (the four that exercise every region), DemoApp examples incl. a
  faithful reproduction of the screenshot, doc pair
  (`UltraCanvasTableView.md` / examples), catalogue + changelog rows.
* **P2 — Spreadsheet consumer**: S1 + S2, presets D4, D5, D7 and the
  palette matrix, F8 stretch sizing, `ToCSV`.
* **P3 — Web story & extras**: F10 `ToHTML`, D8 heatmap (Colormap
  bridge), multi-level headers (F4), HTMLReader `<table>` rendering
  through the same style sheet, XLSX mapping (S4).

---

## 9. Open questions

1. **Type sharing**: include `UltraCanvasSpreadsheetTypes.h` from the new
   style header, or extract `CellFont`/`CellBorders`/alignment enums into
   a shared types header? (Recommendation: extract — keeps the widget
   free of spreadsheet includes; the spreadsheet header re-exports.)
2. **Old draft API**: keep the draft's `extern "C"` legacy interface?
   (Recommendation: drop it — no caller exists in the tree, and it holds
   a mutable global.)
3. **Editing scope**: is in-place cell editing (draft feature) needed for
   P1, or is the widget read-only-with-callbacks first? (Recommendation:
   read-only + `onCellAction`/selection callbacks in P1; editing via a
   real `UltraCanvasTextInput` child in P2, the same pattern the
   spreadsheet uses for its cell editor.)
4. **GitGraph pairing**: should P1 already include the row-alignment API
   `UltraCanvasGitGraph.h:191` expects, so the commit table can be its
   first internal consumer?
