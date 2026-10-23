# UltraCanvasPerformanceMatrix — Research & Feature Proposal

**Status:** Proposal — nothing is built yet. The demo app already lists a
*Performance Matrix* item (`performancematrix`, Info Graphics) with
`ImplementationStatus::Planned`. Its page is placeholder text only, and this
document is the design behind it.

**Author:** UltraCanvas Framework
**Last Modified:** 2026-10-23
**Related:** `UltraCanvasQuadrantChart` (implemented: a 2×2 plane with
continuous points, the nearest *chart* sibling), `UltraCanvasMatrixDiagram`
(implemented: relationship symbols between item lists, the nearest *grid*
sibling), `UltraCanvasHeatmapChart` (implemented: a scalar per cell),
`UltraCanvasSWOTDiagram` (implemented: four fixed panels of bullet lists in
several design presets, the template this proposal follows for *designs*),
[`UltraCanvasMatrixDiagramProposal.md`](UltraCanvasMatrixDiagramProposal.md)
(the template this document follows for its structure).

---

## 1. Summary — the headline question

**Do we need a new element, or is a performance matrix a preset of one we
already have?** We need a new element. The four candidates each miss something
central:

| Candidate | What it is | What a performance matrix needs that it lacks |
|---|---|---|
| `UltraCanvasQuadrantChart` | 2×2 plane split by two straight lines, with points plotted in axis units | 3×3 (and N×M) grids; text, codes, bullets and icons *in* the cells; ordinal axis levels with their own captions and icons; curved zones |
| `UltraCanvasMatrixDiagram` | Two to four item lists crossed, with a symbol from a scale at each intersection | The cells are not relationships between two lists. Each cell is a labelled category of a scored plane, and people or factors are placed into it |
| `UltraCanvasHeatmapChart` | A scalar value per cell, shown through a colour map | Cell titles, descriptions and bullet lists; populations; header tabs |
| `UltraCanvasSWOTDiagram` | Four fixed panels of bullet lists | Any grid other than four panels; axes; a population |

What the five reference images share is one idea. **Two ordinal axes (usually
performance and potential) make a grid of named categories, and a population
(people, products, service factors) falls into those categories.** That idea is
the model. How it is drawn (tabs, tiles, cards or curved zones) is a design
preset over that model, the same way `SWOTDesign` gives one SWOT dataset six
looks.

Recommendation:

* One UI-free model, `PerformanceMatrixModel`
  (`include/Plugins/Diagrams/UltraCanvasPerformanceMatrixModel.h`). It holds
  the two axes, the cells, the optional zones and the population, plus the
  arithmetic: placement from scores, counts, distribution against a target.
* One element, `UltraCanvasPerformanceMatrix`
  (`include/Plugins/Diagrams/UltraCanvasPerformanceMatrix.h`), deriving from
  `UltraCanvasChartElementBase` the way SWOT does.
* Five designs: `Grid`, `Tiles`, `HeaderTabs`, `Cards` and `Zones`. Every
  reference image is one of them plus a palette.
* Four phases (§10). Phase 1 alone retires the demo placeholder.

---

## 2. What a performance matrix is

A performance matrix rates every member of a population on two ordinal
dimensions and reads off a category from where the two ratings cross. The
category carries the meaning, such as "Future Star" or "Urgent Action", and
usually a recommended action. Four families are in common use, and the
reference images cover all of them:

| Family | Axes | Usual grid | Population |
|---|---|---|---|
| **9-box talent grid** (adapted from the GE–McKinsey business screen) | Performance (X) × Potential (Y) | 3×3 | Employees |
| **Performance management matrix** | Performance level (rows) × a facet of potential such as engagement, ability or aspiration (columns) | 3×3 with header tabs | Employees, or none: often a pure reference card |
| **Importance–performance matrix** (Martilla & James 1977; Slack 1994 for operations) | Importance to the customer (X) × performance against competitors (Y) | Continuous, split into *zones* by curved boundaries | Service or product factors, plotted as points |
| **Skill / will and similar 2×2s** | Skill × Will, Ability × Motivation | 2×2 | Employees |

Three properties set these apart from the charts we already have:

1. **The axes are ordinal, and each level has a name.** "Needs Development /
   Meets Expectations / Exceeds Expectations" and "Lots / Average / Little"
   are levels, not tick values. A level can have a caption under its name
   ("Move and Retain") and an icon.
2. **Every cell is content.** A cell can hold a code (`1A`), a number badge,
   a title, a description, a bullet list and an icon. In some uses a cell is
   only a coloured box.
3. **The population is optional, and when present it is the point.** A
   reference card (images 1 and 3) has no population. In a calibration
   meeting, which is the real use of the 9-box, the cells fill with names,
   and moving a person between boxes is the decision being recorded.

---

## 3. What the five reference images demand

### Image 1 — "Performance Management Matrix", coloured header tabs

A 3×3 grid of plain white cells holding short phrases ("Expert", "Consistent
Exceptional Delivery", "Star with Potential", …, "Needs Development"). The
column headers are tabs above the grid: *Potential Engagement* (red),
*Potential Ability* (green) and *Potential Aspiration* (orange). The row headers
are tabs to the left: *Performance Exceeding* (green), *Meeting* (orange) and
*Not Meeting* (red). Each tab has an icon, and the two header bands step out
from each other at the corner instead of meeting in a square.

Demands: **D3** `HeaderTabs` design; per-level accent colour and icon (**C1**);
a cell that is only a centred phrase (**C2**); `None` cell colouring (**S2**);
the stepped corner (**L3**).

### Image 2 — "Performance vs Potential", coded tiles

A 3×3 grid of separated rounded tiles. Each tile has a large code (`1A`–`3C`)
and a two-line description ("Outstanding Performance, High Potential"). The
tiles are coloured by how close they are to the best corner: blue at the top
right, then green, yellow and orange, down to red at the bottom left. The
column headers are text only (*Needs Development / Meets Expectations / Exceeds
Expectations*). The Y axis is a vertical double-headed arrow with a rotated
title, *Leadership Potential*, and level names *High / Moderate / Limited*.

Demands: **D2** `Tiles` design; cell codes (**C2**); `DiagonalBands` colouring
(**S2**); axis arrows with a rotated title (**L1**). Codes count *from* the best
corner, so the model has to know which corner is best (**C4**).

### Image 3 — Image 1 in a single hue

The same content and geometry as image 1, drawn in shades of one blue, inside
a rounded container panel. The header tabs carry icons over the label.

Demands: no new design. A **monochrome palette** (**S1**) derives the tab
shades from one colour. The rounded container (**S4**) is a style flag, not a
design. Image 3 is why a palette has to be separate from a design.

### Image 4 — "Importance Performance Matrix", curved zones

There is no grid. The plane is split by curved boundaries into four zones:
*Excess* (top left), *Appropriate* (most of the upper area), *Improve* (bottom
left) and *Urgent Action* (bottom right). A blue band labelled *Lower Bound of
Acceptability* runs diagonally between them. Each zone has an icon. Both axes
are ordinal with three level names (*Worse Than / Same As / Better Than* on Y
and *Less Important / Qualifying / Order Winning* on X), plus low and high end
captions (*Bad / Good*, *Low / High*) and a two-line title on each axis.

Demands: **D5** `Zones` design, with zones given as outlines in axis units
(**C5**); a labelled band drawn along a boundary (**C5**); level names placed
along a continuous axis (**L2**). In its usual use this chart plots service
factors as points, so **P1** must work with continuous scores too, and **P4**
must classify each point into the zone it lands in.

### Image 5 — "9 box matrix for talent management", bordered cards

A 3×3 grid of white cards. Each card has a coloured border (yellow for the top
row, orange for the middle and pink for the bottom), a numbered circular badge
(1–9) and a bullet list of three to four lines. The Y axis is *Potential*, and
each level has a name and an action caption: *Lots — Move and Retain*,
*Average — Develop*, *Little / None — Contribute and Utilize*. The X axis is
*Performance* with *Low / Med / High*. A coloured bar runs under the X axis.

Demands: **D4** `Cards` design; a number badge (**C2**); bullets (**C2**);
`ByRow` colouring (**S2**); level captions (**C1**); the colour bar along an
axis (**L4**).

### Cross-image constants

Every image has two axes, and every level is named. Every grid is 3×3, but
2×2 and 4×4 variants are common enough elsewhere that the model must not
assume three levels. Colour encodes rank or row, and never anything the text
does not also say, which matters for colour-blind readers (§7.4). None of the
five images shows a population, because they are slide templates. In real use
a population is what makes the chart worth having (§2, property 3), so it is
planned as a first-class part of the model and not left for later.

---

## 4. Where this belongs in the repository

### 4.1 Why not a `QuadrantType::NineBox` on the quadrant chart

This is the cheapest-looking option. `QuadrantChart` already has `QuadrantType`
presets (SWOT, BCG, Eisenhower, …), axis captions, coloured regions and points.
It still fails in three ways:

* `QuadrantDefinition` is four labels and four colours, and the whole layout
  (`QuadrantLayout`, the centre lines, `QuadrantPosition`) is built around two
  dividers. Changing it to N×M rewrites the element, not a preset.
* A quadrant label is one string drawn in a corner. Codes, descriptions,
  bullets, badges and icons in each cell are what four of the five images are
  made of.
* Images 1, 3 and 5 have no plotted points at all. Pushing reference cards
  through a scatter-plot element would mean switching off most of what it does.

What the quadrant chart *does* own is the continuous plane. Image 4 is the one
place where the two overlap, and §9 Q1 takes up that question.

### 4.2 Why not a shape of the matrix diagram

`MatrixModel` crosses *item lists*. A cell means "item r of set A relates to
item c of set B, with strength *level*", and the roll-ups add up those
strengths. In a performance matrix nothing is related to anything: rows and
columns are *levels of a rating*, and the cell is a category. Forcing it in
would fake a set of "levels" items and leave every roll-up meaningless. The
geometry is close, but the model is not.

### 4.3 The other neighbours

* **Heatmap**: a cell is one number. If a caller only wants counts of people
  per box, a heatmap does that today. This proposal's `ByCount` colouring
  (**S2**) is the one heatmap-like feature, and it reuses
  `UltraCanvasColormap` instead of copying it.
* **SWOT**: the closest in *spirit*. It has fixed content panels, design
  presets, bullets and hit-testable items. The new element follows its
  structure (a `…Design` enum, a layout cache, `DropLayoutCache()`), but four
  fixed quadrants with no axes cannot grow into an N×M rated grid.
* **Kanban board**: columns of cards you can drag between. That is
  superficially like drag-to-re-rate (**I4**), but it has no second axis and no
  cells.

### 4.4 Registration checklist (what "done" touches)

1. `UltraCanvas/include/Plugins/Diagrams/UltraCanvasPerformanceMatrixModel.h`:
   the UI-free model, presets, placement, counts and interchange.
2. `UltraCanvas/include/Plugins/Diagrams/UltraCanvasPerformanceMatrix.h`: the
   element.
3. `UltraCanvas/Plugins/Diagrams/UltraCanvasPerformanceMatrixModel.cpp` and
   `UltraCanvasPerformanceMatrix.cpp` (split rendering into
   `…MatrixRender.cpp` once it passes about 900 lines, as the matrix diagram
   does).
4. `UltraCanvas/CMakeLists.txt`: add the sources next to the other
   `Plugins/Diagrams/*.cpp` entries (near `UltraCanvasSWOTDiagram.cpp`).
5. `Apps/DemoApp/UltraCanvasPerformanceMatrixExamples.cpp`, declared in
   `Apps/DemoApp/UltraCanvasDemo.h` next to the SWOT diagram's examples page.
   The existing `infoBuilder.AddItem("performancematrix", …)` in
   `UltraCanvasDemo.cpp` gets the new page, a real status and one `AddVariant`
   per design. Keep the id so existing links still work. Add the source to the
   demo list in the root `CMakeLists.txt`.
6. `Docs/UltraCanvas/UltraCanvasPerformanceMatrix.md` (the guide, with a
   generated `## Contents`). Cross-reference it from
   `UltraCanvasQuadrantChartExamples.md` and `UltraCanvasMatrixDiagram.md`
   ("Which element do I want?"), so all three docs agree on where the
   boundaries are.
7. `Tests/PerformanceMatrixModelTest.cpp` and its entry in
   `Tests/CMakeLists.txt`, covering placement from scores, counts,
   distribution, zone classification and the JSON and CSV round trips, after
   `Tests/MatrixModelTest.cpp`.
8. A `Docs/UltraCanvas/changelog.d/` entry. Run
   `python3 scripts/generate_doc_toc.py`, `check_doc_examples.py` on the new
   guide, `check_ui_reuse.py` and `check_callback_cycles.py`. Then remove this
   proposal's entries from `scripts/doc_examples_baseline.txt`
   (`--all --update-baseline`) as each API lands.

`Docs/UltraCanvas/UltraCanvasUIElements.md` needs no row. The catalogue covers
interactive UI elements and hands charts and diagrams to the plugin docs, as
the matrix diagram proposal found. The element is a **self-rendered view**
("charts" in AGENTS.md), so painting its own cells, tabs and labels is within
that exception. Anything that *edits* text in a cell must use real child
elements (§9 Q4).

---

## 5. Data model sketch

Everything in this section is a sketch of an API that does not exist yet.
`check_doc_examples.py` records its findings in the baseline until it does.

```cpp
namespace UltraCanvas {

    // ===== AXES =====

    struct PerformanceLevel {
        std::string id;          // "exceeds"; stable key for JSON and CSV
        std::string label;       // "Exceeds Expectations"
        std::string caption;     // second line under the label: "Move and Retain"
        std::string iconPath;    // drawn in the header tab (HeaderTabs design)
        Color       accent = Color(0, 0, 0, 0);   // transparent = from the palette

        // Upper bound of this level on the axis' continuous score range. Used
        // to place an item given by score (P2). The last level's bound is the
        // axis maximum. Equal spacing when every bound is left at NaN.
        double      upperBound = std::numeric_limits<double>::quiet_NaN();
    };

    struct PerformanceAxis {
        std::string title;                     // "Performance", "Leadership Potential"
        std::string lowCaption  = "Low";       // at the arrow's low end
        std::string highCaption = "High";
        std::vector<PerformanceLevel> levels;  // always ordered low -> high
        double      minScore = 0.0;            // continuous range: scores, zones
        double      maxScore = 1.0;
        bool        reversed = false;          // draw high first (left / top)
    };

    // ===== CELLS =====

    struct PerformanceCell {
        int         row = 0;           // index into yAxis.levels (0 = low)
        int         col = 0;           // index into xAxis.levels (0 = low)
        std::string code;              // "1A"
        int         badge = 0;         // number in a circle; 0 = none
        std::string title;             // "Future Star"
        std::string description;       // free text, wrapped
        std::vector<std::string> bullets;
        std::string action;            // "Develop", "Move and retain"
        std::string iconPath;
        Color       fill = Color(0, 0, 0, 0);  // used by PerformanceCellColoring::Explicit
        std::string note;              // tooltip only
    };

    // ===== ZONES (Zones design only) =====

    struct PerformanceZone {
        std::string id;
        std::string title;                 // "Urgent Action"
        std::string iconPath;
        Color       fill;
        std::vector<Point2Dd> outline;     // closed polygon in axis units
        bool        smooth = true;         // draw the outline as a spline through its points
    };

    // A labelled band along a curve, e.g. Slack's "lower bound of acceptability".
    struct PerformanceBand {
        std::string label;
        Color       fill;
        std::vector<Point2Dd> centreLine;  // axis units
        double      width = 0.06;          // in axis units, measured on the Y range
    };

    // ===== POPULATION =====

    struct PerformanceMatrixItem {
        std::string id;
        std::string label;                 // "J. Smith", "Delivery speed"
        std::string group;                 // team, department: legend and filter
        std::string iconPath;              // avatar or symbol
        Color       color = Color(0, 0, 0, 0);
        std::string note;

        // Placement: either a cell...
        int         row = -1;
        int         col = -1;
        // ...or two scores, mapped through the axis level bounds. A cell set
        // explicitly wins over scores (a calibrated override).
        double      xScore = std::numeric_limits<double>::quiet_NaN();
        double      yScore = std::numeric_limits<double>::quiet_NaN();
    };

    struct PerformanceCellRef {
        int row = -1;
        int col = -1;
        bool IsValid() const { return row >= 0 && col >= 0; }
    };

    struct PerformanceMatrixValidation {
        bool ok = true;
        std::vector<std::string> problems;  // "cell (3, 0) outside a 3x3 grid", ...
    };

    struct PerformanceMatrixModel {
        std::string title;
        std::string subtitle;
        PerformanceAxis xAxis;             // usually performance
        PerformanceAxis yAxis;             // usually potential
        std::vector<PerformanceCell>       cells;    // at most one per (row, col)
        std::vector<PerformanceZone>       zones;
        std::vector<PerformanceBand>       bands;
        std::vector<PerformanceMatrixItem> items;

        // Optional target share per cell, row-major, summing to 1. Forced
        // distribution in a calibration: drawn as "12 % / target 10 %" (P3).
        std::vector<double> targetDistribution;

        int Rows() const    { return static_cast<int>(yAxis.levels.size()); }
        int Columns() const { return static_cast<int>(xAxis.levels.size()); }

        const PerformanceCell* FindCell(int row, int col) const;
        PerformanceCellRef     CellOf(const PerformanceMatrixItem& item) const;
        std::vector<int>       CountsPerCell() const;               // row-major
        int                    ZoneOf(const Point2Dd& scores) const; // index, -1 = none
        PerformanceMatrixValidation Validate() const;
    };

    // ===== INTERCHANGE (strings only; files go through UltraCanvasFileLoader) =====

    JSONValue   PerformanceMatrixToJson(const PerformanceMatrixModel& model);
    std::string ExportPerformanceMatrixJson(const PerformanceMatrixModel& model, bool pretty = true);
    bool        ImportPerformanceMatrixJson(const std::string& text, PerformanceMatrixModel& out,
                                            std::string* error = nullptr);

    // Rows of "id,label,group,x,y" or "id,label,group,row,column". Levels can
    // be given by id, label (any case) or index. Replaces model.items and
    // leaves the rest alone; on error returns false and leaves `model` untouched.
    bool        ImportPerformanceItemsCsv(const std::string& csv, PerformanceMatrixModel& model,
                                          std::string* error = nullptr);
    std::string ExportPerformanceItemsCsv(const PerformanceMatrixModel& model);

    namespace PerformanceMatrixSamples {
        PerformanceMatrixModel NineBox();                // classic labels, 1-9 badges
        PerformanceMatrixModel PerformanceManagement();  // reference image 1
        PerformanceMatrixModel PerformanceVsPotential(); // reference image 2, codes 1A-3C
        PerformanceMatrixModel TalentReview();           // reference image 5, bullets
        PerformanceMatrixModel ImportancePerformance();  // reference image 4, Slack zones
        PerformanceMatrixModel SkillWill();              // 2x2
    }

} // namespace UltraCanvas
```

The element:

```cpp
namespace UltraCanvas {

    enum class PerformanceMatrixDesign {
        Grid,        // contiguous grid, axis arrows and titles
        Tiles,       // separated rounded tiles: big code + description (image 2)
        HeaderTabs,  // coloured header tabs with icons, plain cells (images 1, 3)
        Cards,       // bordered cards, number badge, bullet list (image 5)
        Zones        // continuous plane cut by curved zones (image 4)
    };

    enum class PerformanceCellColoring {
        None,           // white cells; colour lives in the headers (images 1, 3)
        Explicit,       // each PerformanceCell::fill
        DiagonalBands,  // by distance from the best corner (image 2)
        ByRow,          // the row level's accent (image 5)
        ByColumn,
        ByCount         // population count through a colormap
    };

    enum class PerformancePopulationDisplay {
        Hidden,    // reference card
        Count,     // "7" in a corner chip
        Percent,   // "12 %", and "/ 10 %" when a target exists
        Dots,      // one dot per item, coloured by group
        Names      // item labels, "+12 more" when they do not fit
    };

    struct PerformanceMatrixPalette {
        std::vector<Color> rankColors;   // best -> worst, for DiagonalBands
        Color headerText;
        Color cellText;
        Color gridLine;
        Color panelBackground;

        static PerformanceMatrixPalette TrafficLight();        // image 2
        static PerformanceMatrixPalette Vivid();               // images 1, 4
        static PerformanceMatrixPalette Monochrome(const Color& hue);  // image 3
        static PerformanceMatrixPalette Pastel();              // image 5
    };

    class UltraCanvasPerformanceMatrix : public UltraCanvasChartElementBase {
    public:
        UltraCanvasPerformanceMatrix(const std::string& id, int x, int y, int w, int h);

        void SetModel(const PerformanceMatrixModel& model);
        const PerformanceMatrixModel& GetModel() const;

        void SetDesign(PerformanceMatrixDesign design);
        void SetCellColoring(PerformanceCellColoring coloring);
        void SetPalette(const PerformanceMatrixPalette& palette);
        void SetPopulationDisplay(PerformancePopulationDisplay display);
        void SetBestCorner(bool highXIsBest, bool highYIsBest);  // C4

        void SetShowAxisArrows(bool show);
        void SetShowContainerPanel(bool show);  // S4: rounded panel behind everything
        void SetShowLegend(bool show);          // groups, or the zone key
        void SetAnonymised(bool on);            // P5: counts only, never names

        void SetSelectedCell(const PerformanceCellRef& ref);
        PerformanceCellRef GetSelectedCell() const;
        void SetAllowItemDrag(bool allow);      // I4

        std::function<void(const PerformanceCellRef&)> onCellClick;
        std::function<void(const PerformanceCellRef&)> onCellHover;
        std::function<void(const PerformanceMatrixItem&)> onItemClick;
        // Return false to veto. The model is updated only after this returns true.
        std::function<bool(const PerformanceMatrixItem&, const PerformanceCellRef& from,
                           const PerformanceCellRef& to)> onItemMove;

        void InvalidateLayout() override { DropLayoutCache(); UltraCanvasChartElementBase::InvalidateLayout(); }

    protected:
        void RenderChart(IRenderContext* ctx) override;
        bool HandleChartMouseMove(const Point2Di& mousePos) override;
    };

} // namespace UltraCanvas
```

Notes on the shape of the model:

* **Levels are stored low → high, always.** Display order is a flag
  (`reversed`), so "row 0" never changes meaning between a 9-box drawn with
  high potential at the top and a slide drawn the other way up. Codes and
  badges are *content*, so a template that numbers from the top left (image 5)
  and one that codes from the best corner (image 2) are both just data.
* **A cell is sparse.** A 3×3 model with no cells is valid and draws an empty
  grid with headers. A cell with only `fill` is a coloured box.
* **An item has one placement.** Explicit `row`/`col` wins over scores. That
  is the calibration rule: the meeting overrides the formula, and the
  override is visible in the data, because the scores are still there.

---

## 6. Feature list

### Designs (D)

| Id | Feature | Images |
|---|---|---|
| D1 | `Grid`: contiguous cells, thin grid lines, axis arrows and titles | 2, 5 (skeleton) |
| D2 | `Tiles`: separated rounded tiles, large code over a wrapped description | 2 |
| D3 | `HeaderTabs`: coloured tabs with icon and label above and left of white cells | 1, 3 |
| D4 | `Cards`: white cards, coloured border, number badge, bullet list | 5 |
| D5 | `Zones`: continuous plane, curved filled zones with icon and title, optional bands | 4 |

### Content (C)

| Id | Feature |
|---|---|
| C1 | Level label, caption, icon and accent on both axes |
| C2 | Cell code, badge, title, description, bullets, action, icon. Each is optional and drawn when set |
| C3 | Sample models: `NineBox`, `PerformanceManagement`, `PerformanceVsPotential`, `TalentReview`, `ImportancePerformance`, `SkillWill` |
| C4 | Best corner (default: high X and high Y), used by `DiagonalBands` and by keyboard order |
| C5 | Zones as closed outlines in axis units, optionally smoothed; labelled bands along a centre line |
| C6 | 1 to 6 levels per axis; 2×2, 3×3, 4×4 and mixed (3×4) grids |

### Population (P)

| Id | Feature |
|---|---|
| P1 | Items placed by cell or by continuous scores |
| P2 | Score → level mapping through `PerformanceLevel::upperBound`, equal spacing by default |
| P3 | Counts and percentages per cell, row and column; deviation from `targetDistribution` |
| P4 | Zone classification of scored items (`ZoneOf`); in the `Zones` design items are drawn as points |
| P5 | Anonymised mode: names and tooltips off, counts only, for presenting a calibration to a wider room |
| P6 | Group colours and a legend; the legend filters the population (click a group to hide it) |

### Layout (L)

| Id | Feature |
|---|---|
| L1 | Axis arrows (single or double-headed), rotated Y title, low and high end captions |
| L2 | Level names placed along a continuous axis (`Zones`) at band centres |
| L3 | The stepped corner of header tabs (image 1) |
| L4 | An optional colour bar along an axis, from the level accents (image 5) |
| L5 | Title and subtitle band; the header band reserves its height before the grid is sized |
| L6 | Cells keep their aspect ratio within limits; text decides the minimum cell size (§7.1) |

### Style (S)

| Id | Feature |
|---|---|
| S1 | Palettes: `TrafficLight`, `Vivid`, `Monochrome(hue)`, `Pastel`; the design and the palette are independent |
| S2 | Cell colouring: `None`, `Explicit`, `DiagonalBands`, `ByRow`, `ByColumn`, `ByCount` |
| S3 | Text colour picked for contrast against each fill, never fixed |
| S4 | Rounded container panel (image 3) |
| S5 | Hover highlight of a cell and of its row and column headers |

### Interaction (I)

| Id | Feature |
|---|---|
| I1 | Tooltips: a cell's title, description and count; an item's label, group, scores and note |
| I2 | Click and select a cell; `onCellClick` and `onCellHover` |
| I3 | Click an item (Names / Dots); `onItemClick` |
| I4 | Drag an item to another cell; `onItemMove` can veto. Off by default |
| I5 | Keyboard: arrows move the cell selection, Enter activates; a selected item moves with Ctrl+arrows when I4 is on |
| I6 | An expanded view of an overflowing cell ("+12 more" opens a scrollable list) |

### Import / export (X)

| Id | Feature |
|---|---|
| X1 | JSON round trip of the whole model, `"format": "ultracanvas-performance-matrix"` |
| X2 | CSV import and export of the population only |
| X3 | Image and vector export through the chart base / `UltraCanvasFileLoader`, like every other chart |

---

## 7. The hard parts

### 7.1 Text in fixed cells

Every design but `Zones` puts a fixed grid of cells in a fixed rectangle. Then
text has to fit: image 5's four-bullet cards and image 2's two-line
descriptions at slide size, and a phone-sized dashboard tile. The plan, in
order:

1. Wrap with `TextWrapping::WrapGreedy` (the shared helper, not another copy
   beside SWOT's private `WrapText` and the matrix diagram's `WrapToLines`).
2. Shrink the cell font down to a floor (about 9 px).
3. Drop content in a fixed order: bullets past the ones that fit (with
   "…"), then the description, then the action. The code, badge and title are
   never dropped.
4. Show the full text in the tooltip (I1) whenever anything was dropped.

All cells share one font size, picked by the most crowded cell. Cells in one
grid at different sizes read as different importance.

### 7.2 Many items in one cell

A real 9-box for 200 people puts 80 of them in the middle box. `Names`
therefore has to degrade cleanly: lay names out in columns, end with "+N
more", and open the full list with I6. `Dots` packs on a grid inside the cell,
not at random positions, so the drawing is stable between redraws and dots
never overlap. `Count` and `Percent` always work and are the default (§9 Q3).

### 7.3 Zones from outlines

Zones are author-supplied closed outlines in axis units, smoothed with a
Catmull-Rom spline when `smooth` is set. They are not computed from boundary
functions. This keeps the model plain data (JSON-friendly) and lets a caller
draw any of the published IPA variants. Two things need care:

* **Drawing**: each zone is filled clipped to the plot rectangle, in model
  order, so a later zone covers an earlier one. Bands are drawn last, as a
  stroked path of width `width`, with the label laid along the path's longest
  near-straight run.
* **Classification**: `ZoneOf` must use the *same* curve as the drawing, or a
  point drawn inside *Improve* gets classified as *Urgent Action*. The model
  flattens the spline into a polygon once (the same flattening the renderer
  uses), and both drawing and classification read that polygon with an
  even-odd test.

### 7.4 Colour is never the only signal

Traffic-light colouring (red/green) is the worst case for the most common
colour-vision deficiency. Every preset that colours by rank also writes the
rank as content: a code, a badge or a title. `DiagonalBands` defaults to
`TrafficLight`, whose five colours are also spaced in lightness, so they stay
in order in greyscale. The demo must include a greyscale check of each preset.

### 7.5 Personal data

A populated talent matrix holds employees' names next to ratings of their
potential. The element stores nothing and sends nothing; it is a view.
However, `ExportPerformanceMatrixJson` writes the population, and the demo's
sample data must be invented names. P5 (anonymised) exists so that one model
can be shown on a projector without its names. The guide (§4.4 item 6) should
say this plainly.

---

## 8. What already exists to build on

| Need | Reuse |
|---|---|
| Base class, tooltips, background, redraw | `UltraCanvasChartElementBase` (as SWOT and the quadrant chart do) |
| Design-preset structure, layout cache, item hit rects | `UltraCanvasSWOTDiagram` (`SWOTDesign`, `SWOTLayout`, `DropLayoutCache`) |
| Legend with filtering and colour bar | `ChartLegend` (`ToggleEntryEnabled` for P6, `LegendColorBar` for `ByCount`) |
| Colour maps for `ByCount` | `UltraCanvasColormap.h` |
| Word wrap with truncation | `TextWrapping::WrapGreedy` (`UltraCanvasTextWrapping.h`) |
| Icons in cells and tabs | `IRenderContext::DrawImage(path, rect, ImageFitMode::Contain)`, the `iconPath` field pattern of `FishboneModel`, `PyramidChart`, `TreeMapElement` |
| JSON | `UltraCanvasJSON.h` (`JSONValue`, `Serialize`), following `MatrixModelToJson` / `ImportMatrixJson` |
| CSV conventions | `ImportMatrixTable`: dot-decimal numbers whatever the locale, levels by id or label, reject ragged rows and leave the output untouched. Numbers are read and written with `TryParseFloat` / `FormatFloatClassic` (AGENTS.md rule 2) |
| Loading and saving files | `UltraCanvasFileLoader`. The model functions take and return strings, and the demo's Open and Save buttons go through the loader (CLAUDE.md) |
| Model tests | `Tests/MatrixModelTest.cpp` as the template |

Nothing new is needed from the render context. `Zones` needs path filling
(`BezierCurveTo`, `ClosePath`) and clipping (`ClipRect`, `ClipPath`), which
the contour chart already uses.

---

## 9. Open questions (with recommendations)

**Q1. Should the importance–performance matrix (image 4) be here, or a
`QuadrantType` on the quadrant chart?** It plots points on a continuous plane,
which is the quadrant chart's job, but its regions are curved, it has more
than four of them, and its axes have named levels, none of which the quadrant
chart has. **Recommendation: here, as the `Zones` design.** It shares the
named-level axes, the population, classification and the legend with the grid
designs. Adding curves to the quadrant chart would only bring half of that.
The quadrant chart's doc gets a "for curved zones, see…" pointer.

**Q2. Is "performance" always X?** In the 9-box convention, yes: performance
on X and potential on Y, best at the top right. Image 1 puts performance on
the *rows*. **Recommendation:** the model does not care. Axes are just X and
Y, and the samples assign them as their image does. `NineBox()` follows the
convention.

**Q3. What should the population show by default?** **Recommendation:
`Count`.** It is honest at any population size and shows no names. `Names` is
opt-in, and `SetAnonymised(true)` overrides it.

**Q4. Should cell text be editable in place?** Changing a card's bullets by
typing on the chart would be convenient. It would also need real
`UltraCanvasTextInput` children (AGENTS.md: no hand-made editors), focus
handling and an undo story. **Recommendation: not in this proposal.** Editing
happens on the model, through whatever form the application builds. The
element redraws on `SetModel`. Revisit after Phase 3 if a user asks.

**Q5. Should drag-to-re-rate (I4) be in the first release?** It is the
feature that makes this a calibration tool and not a slide. It is also the
only one that changes data. **Recommendation: Phase 3, off by default, with
`onItemMove` able to veto.** The application decides whether a move is
allowed (permissions, a locked review cycle) and records it.

**Q6. Diagrams or Charts plugin directory?** The demo lists it under Info
Graphics, SWOT and the matrix diagram live in `Plugins/Diagrams`, and the
quadrant chart lives in `Plugins/Charts`. **Recommendation:
`Plugins/Diagrams`, next to SWOT**, whose structure it copies. Keep the demo
item where it is, with the same `performancematrix` id.

---

## 10. Suggested phasing

| Phase | Delivers | Retires |
|---|---|---|
| **1** | Model (axes, cells, `Validate`, `FindCell`), designs D1 `Grid`, D2 `Tiles` and D3 `HeaderTabs`, palettes S1, colouring S2 except `ByCount`, L1, L3, L5, L6, §7.1 text fitting, samples `NineBox`, `PerformanceManagement` and `PerformanceVsPotential`, demo page with one variant per design, the guide, model tests | The demo placeholder; reference images 1, 2 and 3 |
| **2** | D4 `Cards` with badges and bullets, L4, sample `TalentReview`; population P1–P3, P5, P6, `Count` / `Percent` / `Dots` / `Names` with §7.2 overflow, `ByCount`, tooltips I1, cell and item clicks I2 and I3, legend | Reference image 5; the 9-box becomes usable with real data |
| **3** | I4 drag with veto, I5 keyboard, I6 overflow list, X1 JSON, X2 CSV, demo Open and Save through `UltraCanvasFileLoader` | The calibration-meeting use case |
| **4** | D5 `Zones`, bands, L2, P4 classification (§7.3), sample `ImportancePerformance`, sample `SkillWill` | Reference image 4 |

Phases 1 and 2 are entirely axis-aligned rectangles and text. All the curve
work is in Phase 4, so if Q1 is decided the other way, Phase 4 moves to the
quadrant chart and nothing in Phases 1–3 changes.
