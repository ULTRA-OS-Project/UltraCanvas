# UltraCanvas Circular Charts — Research & Feature Proposal

Status: **Proposal** — research write-up, gap analysis and feature roadmap for
the circular chart family. Nothing in this document is implemented yet unless
explicitly marked as existing.

Author: UltraCanvas Framework
Last Modified: 2026-07-29

---

## 1. What a "circular chart" is

"Circular chart" is not a single chart type — it is a **family** of chart
types that all map data onto a polar (angle/radius) coordinate system but
encode values differently. The sub-types commonly grouped under the name:

| Sub-type | Value encoding | Typical use |
|---|---|---|
| **Pie chart** | Angle: each slice's sweep ∝ share of the total | Composition of a whole |
| **Donut chart** | Same as pie, with a centre hole (often holding a KPI) | Composition + a headline number |
| **Progress pie / progress ring** | One value 0–100% swept as a partial sector or arc over a faded "track" | Single completion metric |
| **Concentric progress rings** ("activity rings", "multi-level circular progress") | One value per ring; each ring's arc sweep ∝ its own % — rings are independent, not shares of one total | Comparing a handful of KPIs / goals |
| **Radial bar / ray chart** | Radius: bar *length* from an inner ring outward ∝ value | Many values in compact circular form |
| **Rose / Nightingale chart** | Radius of equal-angle sectors ∝ value (area-true with √ scale) | Cyclic categories (months, wind) |
| **Polar scatter/line/area** | True (angle, radius) coordinates | Directional / cyclic measurements |
| **Sunburst** | Angle within concentric hierarchy rings | Hierarchical composition |
| **Radar / spider** | Radius on N fixed spokes | Multivariate profiles |
| **Chord** | Arc segments + ribbons between them | Flows between entities |
| **Gauge / dial** | Needle angle or arc fill against a scale | Single instrument-style value |

The critical distinction that is easy to get wrong: **concentric progress
rings** (angle ∝ value, one ring per series — what ECharts et al. confusingly
also call a "radial bar chart") versus UltraCanvas'
`UltraCanvasRadialBarChart` (radius ∝ value, rays from an inner ring). They
look superficially related but are different encodings and need different
elements.

---

## 2. What the uploaded reference image demands

The reference is a classic "circular chart infographic": **four concentric
rings**, each an independent percentage. Concretely it requires:

* One arc per ring, all starting at a common angle, each sweeping
  proportionally to its own value (~48%, ~54%, ~64%, ~83% in the reference).
* A **percentage callout at each arc tip**, floating just outside/at the end
  of the arc.
* Rounded arc ends.
* The remainder of each ring visible as a **faded track** (a paler tint of
  the ring colour).
* A distinct colour per ring (yellow / magenta / green / purple / orange
  family).
* A filled **centre disc** (red in the reference) carrying a title text.
* A **numbered legend** at the side — chips `01 02 03 04`, each with an icon
  and colour-matched to its ring.
* Optional caption text lying *inside* the ring band ("Lorem ipsum" banners
  along the lower arcs).

> This is the *concentric progress rings* sub-type. No existing UltraCanvas
> element produces it cleanly today (see §4).

---

## 3. What UltraCanvas already implements

The circular family is already substantially covered. Inventory, from the
headers under `include/Plugins/Charts/` and `include/Plugins/Diagrams/`:

| Sub-type | Element | Status / capabilities |
|---|---|---|
| Pie + donut (+ 3D) | `UltraCanvasPieChartElement` | **Implemented.** Donut mode with inner-radius fraction, per-slice/global explosion, 3D extrusion with lighting and per-slice raise, inside/outside/edge/auto labels with leader lines, name/value/percentage content, per-slice gradients & patterns, tooltips, PNG export |
| Radial bar (rays) | `UltraCanvasRadialBarChart` | **Implemented.** Multi-series angular sectors, bar/line ray styles, butt/round/square/arrow caps, global/per-series normalisation, ring guides, centre text, series & peak labels, hover/click callbacks |
| Rose / polar | `UltraCanvasPolarChart` | **Implemented.** Line/spline/area/scatter/column series, categorical & numeric angle modes, `CreatePolarRoseChart` factory, stacking incl. percent, √ area-true radial scale, radial/angular bands, dual angle axes, legend with toggle, drag-rotate, coordinate helpers |
| Sunburst | `UltraCanvasSunburstChart` | **Implemented.** Hierarchical nodes, drill-down, depth shading, centre total, label orientations, tooltips |
| Ring/cell infographic | `UltraCanvasCircularInfoGraphic` | **Implemented.** Concentric rings of individually styled *cells*, circular/radial/star text, per-cell value visualisations (radial bar, angular arc, cross-ring line), decorative rings, cross-ring connections, CSV import. **No component doc exists yet.** |
| Radar / spider | `UltraCanvasRadarChartElement` | **Implemented.** Multi-axis, per-axis ranges, legend, animation |
| Chord | `UltraCanvasChordChart` | **Implemented.** |
| Gauge ring | `UltraCanvasGaugeDiagramElement` (`GaugeMode::CircularRing`, Diagrams plugin) | **Implemented** as a single-value dashboard widget: solid/segmented/dashed/spectrum ring styles, liquid fills, centre icon/label, value colour bands |

Shared infrastructure all of these build on: `UltraCanvasChartElementBase`
(title, tooltips, animation clock, plot-area cache, zoom/pan flags) and
`IRenderContext` (arc paths, gradients, rotated text).

---

## 4. Gap analysis

Matching §1/§2 against §3:

1. **Concentric progress rings — the reference image — have no clean home.**
   * `UltraCanvasRadialBarChart` encodes value as ray *length*, not arc
     sweep — wrong encoding.
   * `UltraCanvasGaugeDiagramElement::CircularRing` draws exactly one ring
     for one value; it is a UI widget, not a data-driven chart (no per-ring
     series, legend, tooltips or chart base).
   * `UltraCanvasCircularInfoGraphic` can approximate it via one cell per
     ring with `ValueVisualizationType::CircularBar`, but there is no
     arc-tip percentage callout, no per-ring track styling, no rounded caps,
     no numbered legend — and the cell-grid model is a heavyweight detour
     for "N values, N rings".
2. **Progress pie chart** (single value swept as a filled sector over a
   track) is absent. The pie element always renders a full-circle
   composition.
3. **Pie/donut element is missing family-standard controls** that every
   sibling already has: no `SetStartAngle` / direction / sweep (so no
   half-donut), no centre text for the donut hole (radial bar, sunburst and
   the infographic all have centre content), no legend, and no
   click/hover callbacks (`onSliceClick` — radial bar, polar and sunburst
   all expose callbacks).
4. **Discoverability.** The sub-types live in five elements with no umbrella
   documentation; a user searching for "circular chart", "donut KPI" or
   "activity rings" has no map. `UltraCanvasCircularInfoGraphic` has a demo
   (`Apps/DemoApp/UltraCanvasCircularInfoGraphicExamples.cpp`) but no doc
   page at all.
5. **Duplication (technical debt).** Annular-sector outlines, arc text and
   polar hit-testing are re-implemented privately in the pie, sunburst,
   radial bar, polar and infographic elements.

Rose chart needs **nothing new** — `CreatePolarRoseChart` plus the √ radial
scale already covers it.

---

## 5. Proposed architecture

**Do not build one monolithic "CircularChart" element.** The encodings are
different enough that a single class would become a mode-switch swamp, and
four of the sub-types are already shipped and documented. Instead:

1. **One new element** for the missing encoding:

   ```
   include/Plugins/Charts/UltraCanvasCircularProgressChart.h
   Plugins/Charts/UltraCanvasCircularProgressChart.cpp
   ```

   `UltraCanvasCircularProgressChart : UltraCanvasChartElementBase` — one
   value per ring, angle-encoded, covering *progress pie*, *single progress
   ring* and *concentric progress rings* (the reference image) as style
   options of the same data model (N rings; N = 1 + sector fill = progress
   pie).

2. **Complete the pie/donut element** with the missing family-standard
   controls (start angle, direction, sweep, centre content, legend,
   callbacks) — additive, no breaking changes.

3. **A family umbrella doc** `Docs/UltraCanvas/UltraCanvasCircularCharts.md`
   mapping every sub-type to its element with a thumbnail and a
   cross-link, plus the missing
   `Docs/UltraCanvas/UltraCanvasCircularInfoGraphic.md` component doc.

4. **Later**: extract the shared radial geometry (annular sector outline,
   arc-tip point, polar hit-test, circular text) into a dependency-free
   header `UltraCanvasRadialGeometry.h`, unit-tested, and migrate the five
   existing elements to it opportunistically.

The gauge stays what it is — an instrument widget. The new chart should not
absorb it, but may borrow its ring *styling* vocabulary (segmented/dashed
arcs) in a later phase.

---

## 6. Proposed feature list

**P1** = core, ships first (reproduces the reference image); **P2** =
completes the family; **P3** = polish / consolidation.

### 6.1 New element — `UltraCanvasCircularProgressChart`

#### Data model
| # | Feature | Phase |
|---|---|---|
| C1 | Ring model: `{label, value, min/max (default 0–100), color, icon path}`; `AddRing` / `SetRingValue` / `ClearRings` | P1 |
| C2 | Live value updates with animated tween between old and new sweep | P2 |
| C3 | Value & percentage formatter callbacks (shared convention with pie/sunburst) | P1 |
| C4 | Ring ordering control (first ring innermost vs outermost) | P2 |

#### Geometry
| # | Feature | Phase |
|---|---|---|
| C5 | Inner radius fraction, auto ring thickness from available radius, ring spacing in px | P1 |
| C6 | Fixed ring thickness override | P2 |
| C7 | Start angle (default −90° = 12 o'clock), clockwise/counter-clockwise direction | P1 |
| C8 | Sweep angle < 360° (semi-circular / gauge-style progress arcs) | P2 |
| C9 | Sub-style: `ConcentricRings` (default), `SingleRing`, `ProgressPie` (filled sector to the centre, one value) | P1 |

#### Arcs & tracks
| # | Feature | Phase |
|---|---|---|
| C10 | Per-ring background track: auto pale tint of the ring colour, or explicit colour/opacity, or hidden | P1 |
| C11 | Cap style: butt / round (reference image uses round) | P1 |
| C12 | Gradient along the arc and fade-toward-tip option | P2 |
| C13 | Value colour bands (arc colour derived from value: green→amber→red), matching the gauge convention | P2 |
| C14 | Target marker: a tick/dot on the ring at a target value | P2 |
| C15 | Over-100 % handling: clamp (default) or wrap-and-overlap with darkened second lap (activity-ring behaviour) | P3 |
| C16 | Segmented / dashed arc styles (vocabulary borrowed from `GaugeRingStyle`) | P3 |

#### Labels, centre & legend
| # | Feature | Phase |
|---|---|---|
| C17 | **Arc-tip callout**: percentage or value at the end of each arc, optional bubble background, auto-nudged to avoid the neighbouring ring | P1 |
| C18 | Ring name label: inside the track at the start of the arc (the reference's in-band captions), or outside with leader line | P1 / leader P3 |
| C19 | Centre disc: fill + border colours, image, title and subtitle text (the reference's red centre) | P1 |
| C20 | Legend: numbered chips (`01`, `02`, …) with colour swatch, optional icon and label; positions left/right/top/bottom | P1 |
| C21 | Legend interactivity: hover chip ↔ highlight ring | P2 |

#### Interaction & lifecycle
| # | Feature | Phase |
|---|---|---|
| C22 | Hover highlight + tooltips with custom formatter (base-class tooltip system) | P1 |
| C23 | `onRingClick` / `onRingHover` callbacks `(ringIndex)` | P1 |
| C24 | Sweep-in entry animation, optionally staggered ring by ring (base-class animation clock) | P2 |
| C25 | PNG export parity with the pie element (`QuickExport` / `ExportToFile`) | P2 |

### 6.2 Pie / donut completion (existing `UltraCanvasPieChartElement`)
| # | Feature | Phase |
|---|---|---|
| B1 | `SetStartAngle` + direction (every sibling element has it; pie does not) | P1 |
| B2 | Donut centre content: text / formatted total / KPI value + caption | P1 |
| B3 | `onSliceClick` / `onSliceHover` callbacks; optional explode-on-click selection | P1 |
| B4 | Sweep angle < 360° (half-pie / half-donut) | P2 |
| B5 | Legend with position + click-to-toggle, consistent with the polar chart's | P2 |
| B6 | Small-slice aggregation: threshold % below which slices merge into "Other" | P2 |
| B7 | Slice sorting (none / ascending / descending) | P2 |
| B8 | Sweep-in entry animation | P2 |
| B9 | Rounded slice corners (modern donut styling) | P3 |

### 6.3 Family alignment & documentation
| # | Feature | Phase |
|---|---|---|
| F1 | Umbrella doc `UltraCanvasCircularCharts.md`: sub-type → element decision table, cross-links, one screenshot each | P1 |
| F2 | Missing component doc `UltraCanvasCircularInfoGraphic.md` | P1 |
| F3 | Component doc + demo for the new element; demo scene reproducing the reference image | P1 |
| F4 | Factory aliases for discoverability: `CreateDonutChart`, `CreateProgressPieChart`, `CreateConcentricRingChart`, `CreateRoseChart` (thin wrappers over existing factories) | P2 |
| F5 | Shared `UltraCanvasRadialGeometry.h` (annular sector outline, arc-tip point, polar hit-test, circular text), unit-tested; migrate pie/sunburst/radial-bar/polar/infographic incrementally | P3 |
| F6 | Regenerate `llms.txt` / `llms-full.txt` with every doc change (house rule) | P1 |

---

## 7. Proposed API sketch

Reproducing the reference image:

```cpp
#include "Plugins/Charts/UltraCanvasCircularProgressChart.h"

auto rings = UltraCanvas::CreateCircularProgressChart("kpis", 20, 20, 520, 480);

// One ring per KPI, inner to outer. Values default to a 0..100 scale.
rings->AddRing("Research",  48.0, Color(255, 62, 133, 255));  // magenta
rings->AddRing("Marketing", 54.0, Color(255, 200, 40, 255));  // yellow
rings->AddRing("Sales",     64.0, Color(150, 200, 60, 255));  // green
rings->AddRing("Support",   83.0, Color(120, 90, 200, 255));  // purple

rings->SetStartAngle(-90.0f);                     // arcs start at 12 o'clock
rings->SetCapStyle(UltraCanvas::RingCapStyle::Round);
rings->SetTrackMode(UltraCanvas::RingTrackMode::AutoTint);
rings->SetTipLabelContent(UltraCanvas::RingTipLabel::Percentage);
rings->SetRingNameLabels(UltraCanvas::RingNamePosition::InsideStart);

rings->SetCenterDisc(Color(225, 30, 40, 255), Colors::White);
rings->SetCenterText("Lorem ipsum", "dolor sit amet");

rings->SetLegendStyle(UltraCanvas::CircularLegendStyle::NumberedChips,
                      UltraCanvas::CircularLegendPosition::Left);

rings->onRingClick = [](size_t ringIndex) { /* ... */ };
container->AddChild(rings);
```

Progress pie — same element, one ring, sector style:

```cpp
auto progress = UltraCanvas::CreateProgressPieChart("done", 20, 20, 200, 200);
progress->AddRing("Complete", 65.0, Color(40, 140, 230, 255));
progress->SetSubStyle(UltraCanvas::CircularProgressStyle::ProgressPie);
progress->SetTipLabelContent(UltraCanvas::RingTipLabel::None);
progress->SetCenterText("65%");
```

Donut KPI — existing pie element plus the B-phase additions:

```cpp
auto donut = UltraCanvas::CreatePieChartElement("share", 20, 20, 360, 300);
donut->SetDataSource(data);
donut->SetDonutMode(true);
donut->SetStartAngle(-90.0f);                       // B1
donut->SetCenterKPI("$1.2M", "Total revenue");      // B2
donut->onSliceClick = [](size_t i) { /* ... */ };   // B3
```

---

## 8. Suggested delivery order

1. **Phase 1 — the reference image + pie completion.**
   `UltraCanvasCircularProgressChart` with the P1 rows (ring model,
   concentric/single/sector styles, tracks, round caps, arc-tip callouts,
   in-band name labels, centre disc, numbered legend, tooltips, callbacks),
   the pie's B1–B3, the umbrella doc, the infographic doc, a DemoApp scene
   reproducing the reference image, `llms.txt` regeneration.
2. **Phase 2 — family completeness.** Animated value tweens, semi-circular
   sweeps, gradients/colour bands/targets, legend hover-link, export, pie
   legend + aggregation + sorting + half-pie, factory aliases.
3. **Phase 3 — polish & consolidation.** Over-100 % wrap, segmented arc
   styles, leader-line labels, rounded pie corners, and the shared
   `UltraCanvasRadialGeometry.h` extraction with element migration.

---

## 9. Open questions for review

1. **Naming** — `UltraCanvasCircularProgressChart` (recommended: says what
   it encodes) vs `UltraCanvasConcentricRingChart` vs
   `UltraCanvasActivityRingChart` (too Apple-specific).
2. **Progress-pie placement** — confirmed as a sub-style of the new element
   (recommended, §5) rather than a mode of `UltraCanvasPieChartElement`?
   The pie stays a pure composition chart either way.
3. **Gauge overlap** — keep `GaugeMode::CircularRing` untouched as the
   single-value instrument widget, and only borrow its segment styling
   vocabulary in P3 — or should the gauge eventually delegate its ring
   rendering to the shared radial geometry too?
4. **Legend widget** — the numbered-chip legend (C20) is the third
   element-private legend in the charts plugin (polar and radar have their
   own). Should P2 introduce a shared chart legend component instead? (Same
   question was raised in the contour proposal, §8.3.)
