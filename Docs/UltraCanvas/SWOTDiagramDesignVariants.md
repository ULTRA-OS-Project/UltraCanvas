# SWOT Diagram Design Variants — Investigation

**Status:** Design survey (pre-implementation research)
**Last Modified:** 2026-07-28
**Author:** UltraCanvas Framework
**Related:** `UltraCanvasQuadrantChart` (scatter-style SWOT already implemented),
future `UltraCanvasSWOTDiagram` (classic text-panel SWOT, not yet implemented)

## Purpose

A SWOT diagram presents four short texts (or bullet lists) — **S**trengths,
**W**eaknesses, **O**pportunities, **T**hreats — but the graphical presentation
of those four texts varies enormously across presentation tools (PowerPoint
template vendors, Visme, Venngage, Canva, Visual Paradigm InfoArt, SlideModel,
PresentationGO, Edraw). This document catalogs the distinct layout families
found in the wild, so a future `UltraCanvasSWOTDiagram` element can offer them
as design presets (the same approach used by `UltraCanvasPertChart` and
`UltraCanvasGanttChart` designs/palettes).

Survey result: **~24 distinct layout variants**, grouped into 6 structural
families, plus a set of cross-cutting decoration options that combine freely
with almost every layout.

## Cross-cutting decoration dimensions

These are orthogonal to the geometry and should be independent style options,
not separate designs:

- **Letter badges** — circled S/W/O/T letters; placed at outer corners, inside
  a central shape, or in panel headers.
- **Icons** — one pictogram per quadrant (conventions: muscle/thumb-up = S,
  broken chain/thumb-down = W, light bulb/rocket = O, warning triangle/
  lightning = T).
- **Color conventions** — S green/teal, W red/pink/orange, O yellow/gold/blue,
  T red/orange/dark. Flat fills vs. vertical gradients; full-fill panels vs.
  white panels with colored accents only.
- **Header placement** — inside the panel, as a colored strip above the panel,
  or outside the panel connected by dotted leader lines.
- **Text presentation** — bullet list, numbered list, or paragraph; with or
  without per-item markers.
- **Axis captions** — "Internal / External" and "Helpful / Harmful" written
  along the matrix edges (classic academic form).
- **Central element** — circle, diamond, crosshair, or company logo in the
  middle of the four panels.

## Family A — Rectangular / matrix layouts

**A1. Classic contiguous 2×2 matrix.** One square divided into four equal
colored cells; header text and body inside each cell. The canonical layout:
S top-left, W top-right, O bottom-left, T bottom-right.

```
+----------+----------+
| S ...    | W ...    |
+----------+----------+
| O ...    | T ...    |
+----------+----------+
```

**A2. Axis-labeled matrix.** A1 plus edge captions: "Internal | External"
across the top/side and "Helpful | Harmful" on the other axis (textbook /
academic form).

**A3. Card grid.** Four separated rounded-corner cards with gutters between
them, often with drop shadows; reads as four independent tiles rather than
one table.

**A4. Corner-badge cards.** Card grid where each card carries a circled
letter badge at its *outer* corner (badges at the four extremes of the
diagram); headers may sit outside the cards, linked by dotted lines.

**A5. Header-bar cards.** White/neutral body panels, each topped by a colored
header strip (or underlined heading) carrying the quadrant name and icon.

**A6. Worksheet / table form.** Plain bordered table, often monochrome — the
fill-in form used in reports and printouts.

## Family B — Center-anchored layouts

**B1. Central circle hub.** A circle in the middle split into four quarters
holding the S/W/O/T letters (or the words "SWOT analysis"); the four text
panels surround it in a 2×2 arrangement, usually notched so the circle
overlaps their inner corners.

**B2. Central diamond.** A square rotated 45° in the center, divided into
four triangles labeled S, W, O, T; the four texts sit at the surrounding
corners/quadrants, often with icons and thin leader lines.

**B3. Minimal crosshair.** Only two thin axis lines cross the canvas; texts
float in the four quadrants with small colored accents — the most understated
form.

**B4. Donut / ring segments.** A ring divided into four arcs (colored,
letter-labeled); texts sit outside the ring connected by callout lines, or in
panels aligned to each arc.

## Family C — Radial / organic layouts

**C1. Petal / flower.** Four petal shapes radiating from a central circle,
one per quadrant; text inside the petal or in an adjacent block.

**C2. Windmill / pinwheel.** Four rotated blade shapes suggesting motion;
letters at the hub, texts on the blades or beside them.

**C3. Full-circle four sectors.** A pie divided into four 90° sectors with
the texts inside the sectors (works only for very short texts) or as
callouts.

**C4. Mind map / spider.** A central "SWOT" node with four colored branches,
each branch fanning out into one sub-branch per bullet item. The only layout
that scales naturally to many items per quadrant.

## Family D — Linear layouts

**D1. Horizontal bands (list style).** Four full-width rows stacked
vertically; each row leads with a big letter block or vertical label,
followed by the bullet list and an icon. Bookmark/tab variants give each row
a protruding colored tab.

**D2. Four vertical columns.** Side-by-side columns with colored header
chips — good for portrait slides and posters.

**D3. Chevron / arrow strip.** Four chevrons or arrows in a row (or ascending
stair-step "growth" arrangement); text below/above each arrow. Conveys
progression rather than opposition.

## Family E — Shape-novelty layouts

**E1. Puzzle.** Four interlocking jigsaw pieces forming a square (or a
horizontal strip); conveys that the four parts form one whole.

**E2. Hexagons / honeycomb.** Four hexagonal tiles, either in a 2×2 cluster
around a central hexagon or in a row.

**E3. Speech bubbles / callouts.** Four bubble shapes, often pointing at a
central subject (product photo, logo).

**E4. 3D styles.** Isometric cubes or blocks (one face per letter), folded
ribbon/origami panels, stacked 3D slabs with dashed connectors to text
callouts.

**E5. Vendor-named geometrics.** Template libraries ship further one-off
geometries built from the same four-text idea: **bookmark**, **brick**,
**parallelogram**, **U-shape**, **bell**, plus hand-drawn/sketch renderings
of A1. (Visual Paradigm InfoArt alone offers ~30 named styles.)

## Family F — Analytic / extended layouts

**F1. Scatter quadrant chart.** Factors plotted as points with
x = internal↔external, y = helpful↔harmful. **Already implemented in
UltraCanvas** as `UltraCanvasQuadrantChart` with `QuadrantType::SWOT`.

**F2. TOWS 3×3 matrix.** The SWOT lists occupy the first row and column of a
3×3 grid; the four inner cells hold the derived S-O, W-O, S-T, W-T
strategies. A superset layout that reuses the four base texts.

## Observations for a future UltraCanvasSWOTDiagram element

1. **Geometry and decoration separate cleanly.** Nearly every commercial
   variant is one of ~10 geometries plus a combination of the cross-cutting
   options above. A `SWOTDesign` enum (geometry) + independent toggles
   (badges, icons, header placement, gradient) covers the bulk of the space —
   mirroring how `UltraCanvasPertChart` separates designs from palettes.
2. **Sensible preset shortlist** (highest real-world frequency, all visible
   in the reference images collected during this investigation):
   - `Matrix` (A1/A2, axis captions optional)
   - `Cards` (A3–A5 via header-placement/badge options)
   - `CenterCircle` (B1)
   - `CenterDiamond` (B2)
   - `Rows` (D1)
   - `Columns` (D2)
   - `Petals` (C1)
   - `Puzzle` (E1)
   - `MindMap` (C4) — the only one needing a distinct (branch) layout engine
3. **Data model is layout-independent:** four ordered lists of items
   (label + optional icon/color), plus per-quadrant title, color, icon and
   badge letter. Every family above renders from that same model.
4. **Text fitting is the hard part**, not the shapes: panels must wrap,
   ellipsize or auto-shrink bullet text; radial forms (petals, sectors) need
   the shared shape-label placement solver already used by the Venn diagram.
