# UltraCanvas Circular Charts — Family Guide

## Overview

"Circular chart" is a family of chart types that all draw on a polar
(angle/radius) coordinate system but encode values differently. UltraCanvas
implements the family across several elements; this page is the map. Pick the
element by **what the angle and radius mean in your data**, then follow the
link to its component doc.

**Last Modified:** 2026-07-29
**Author:** UltraCanvas Framework

## Decision table

| You want | Encoding | Element | Doc |
|---|---|---|---|
| Composition of a whole (market share) | Slice angle ∝ share of total | `UltraCanvasPieChartElement` | [UltraCanvasPieChartExamples.md](UltraCanvasPieChartExamples.md) |
| Composition + a headline number in the hole | Donut mode + centre KPI | `UltraCanvasPieChartElement` (`SetDonutMode`, `SetCenterKPI`) | [UltraCanvasPieChartExamples.md](UltraCanvasPieChartExamples.md) |
| A handful of independent percentages ("activity rings") | Arc sweep ∝ each ring's own value | `UltraCanvasCircularProgressChart` | [UltraCanvasCircularProgressChart.md](UltraCanvasCircularProgressChart.md) |
| One completion metric (progress ring / progress pie) | Single arc or filled sector over a track | `UltraCanvasCircularProgressChart` (`SingleRing` / `ProgressPie`) | [UltraCanvasCircularProgressChart.md](UltraCanvasCircularProgressChart.md) |
| Many values as rays around a circle | Ray **length** ∝ value | `UltraCanvasRadialBarChart` | [UltraCanvasRadialBarChartExamples.md](UltraCanvasRadialBarChartExamples.md) |
| Cyclic categories (months, wind directions), incl. stacked | Sector **radius** ∝ value (rose / Nightingale) | `UltraCanvasPolarChart` (`CreatePolarRoseChart`) | [UltraCanvasPolarChart.md](UltraCanvasPolarChart.md) |
| True (angle, radius) observations | Polar scatter / line / spline / area | `UltraCanvasPolarChart` | [UltraCanvasPolarChart.md](UltraCanvasPolarChart.md) |
| Hierarchical composition with drill-down | Angle within concentric hierarchy rings | `UltraCanvasSunburstChart` | [UltraCanvasSunburstChartExamples.md](UltraCanvasSunburstChartExamples.md) |
| Multivariate profiles on fixed spokes | Radius on N axes | `UltraCanvasRadarChartElement` | [UltraCanvasRadarChartElement.md](UltraCanvasRadarChartElement.md) |
| Flows between entities | Arc segments + ribbons | `UltraCanvasChordChart` | [UltraCanvasChordChartExamples.md](UltraCanvasChordChartExamples.md) |
| Rings of styled text/image cells, cross-ring connections | Cell grid on concentric rings | `UltraCanvasCircularInfoGraphic` | [UltraCanvasCircularInfoGraphic.md](UltraCanvasCircularInfoGraphic.md) |
| A single instrument-style value (dial, ring, battery) | Needle angle or arc fill against a scale | `UltraCanvasGaugeDiagramElement` (Diagrams plugin) | [UltraCanvasGaugeExamples.md](UltraCanvasGaugeExamples.md) |

## The two "radial bar" charts

Two common chart-library names collide here — check the encoding:

- **Ray length** ∝ value (many thin bars radiating outward from a base ring)
  → `UltraCanvasRadialBarChart`.
- **Arc sweep** ∝ value (a few concentric rings, each partially swept — what
  some libraries also call a radial bar chart) →
  `UltraCanvasCircularProgressChart`.

## Independent values vs shares of a whole

A pie/donut answers "how does the whole split up?" — slice sweeps sum to
360°. The circular progress chart answers "how far along is each metric?" —
every ring can be 0–100 % on its own. If your percentages don't sum to 100,
you want progress rings, not a pie.

## Shared conventions across the family

- Start angle defaults to **−90° (12 o'clock)**; sweep is clockwise by
  default where a direction applies.
- Ring/slice colours fall back to a per-element default palette;
  `Colors::Transparent` on a datum means "inherit".
- All elements derive from `UltraCanvasChartElementBase`: chart title,
  tooltips, hover, and `RequestRedraw`-driven updates work the same way.
- Elements that own their data (radial bar, polar, sunburst, circular
  progress, infographic) do not use `SetDataSource`; the pie does.

## Roadmap

The research write-up and phased feature plan for the family — including the
gaps still open (animated value tweens, partial sweeps for progress rings,
angular value axis, percent-of-total rose labels) — is in
[UltraCanvasCircularChartProposal.md](UltraCanvasCircularChartProposal.md).
