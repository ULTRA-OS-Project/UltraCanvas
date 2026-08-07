# UltraCanvasParallelCoordinateChart Documentation

## Overview

`UltraCanvasParallelCoordinateChartElement` renders multi-dimensional records
as polylines across parallel axes: each dimension gets its own vertical axis,
one record becomes one line. It is the **first native client of the chart
engine** — it implements only phase 2 of the engine's three-phase render (the
polylines) plus small descriptor methods; axes, endpoint labels, the shared
value axis and its grid, the legend, the solved label plan and the plot-area
clipping all come from `UltraCanvasChartEngineElement`. Data lives in the
unit-tested `ParallelAxisModel`.

- Element: `include/Plugins/Charts/UltraCanvasParallelCoordinateChart.h`,
  `Plugins/Charts/UltraCanvasParallelCoordinateChart.cpp`
- Model (UI-free): `include/Plugins/Charts/UltraCanvasParallelAxisModel.h`,
  `Plugins/Charts/UltraCanvasParallelAxisModel.cpp` — tested in
  `Tests/ChartEngineTest.cpp`
- Engine driver: `include/Plugins/Charts/Engine/UltraCanvasChartEngineElement.h`
- Registry name: `"parallel-coordinate-chart"` (call
  `RegisterParallelCoordinateChartElement()` to register)

**Version:** 1.0.0
**Last Modified:** 2026-08-06
**Author:** UltraCanvas Framework
**Namespace:** `UltraCanvas`

## Reading the chart

Only *neighbouring* axes compare directly: lines running parallel between two
axes mean positive correlation, a tight X means negative correlation, a bundle
of lines sharing a corridor is a cluster, a line leaving the envelope is an
outlier. Axis order is therefore interactive state, not fixed configuration —
drag an axis header to reorder, double-click an axis to invert it.

## Basic usage

```cpp
#include "Plugins/Charts/UltraCanvasParallelCoordinateChart.h"

auto pcp = CreateParallelCoordinateChartElement("iris", 20, 20, 720, 420);

pcp->AddDimension("Petal Length", petalLength);   // column-oriented input
pcp->AddDimension("Petal Width",  petalWidth);
pcp->AddDimension("Sepal Length", sepalLength);
pcp->AddDimension("Sepal Width",  sepalWidth);
pcp->SetRecordGroups(species);                    // "setosa" / "versicolor" / ...

// The Iris look: z-score every column onto one shared value axis with grid.
pcp->SetNormalizationMode(PCPNormalization::CommonScale);
pcp->SetStandardize(PCPStandardize::ZScore);
pcp->SetShowVertexMarkers(true);
pcp->SetChartTitle("Parallel Coordinate Plot for the Iris Data");
// Groups get palette colours and a legend automatically; override with:
pcp->SetGroupColors({{"setosa", Color(68, 1, 84)},
                     {"versicolor", Color(33, 145, 140)},
                     {"virginica", Color(253, 231, 37)}});

window->AddElement(pcp.get());
```

Record-oriented input (`SetRecords`) takes `PCPRecord` — values, label, group,
weight, optional per-record colour. `NaN` values are missing: they break the
record's line and never poison an axis range.

## Normalisation

| Mode | Behaviour |
|---|---|
| `PerAxis` (default) | Each axis spans its own min..max — the classic look, with an integrated axis (tick labels along each rule) |
| `CommonScale` | All axes share one value band; a shared left value axis with gridlines appears, and each in-plot axis carries only its original-unit min/max endpoint labels |
| + `SetStandardize(ZScore)` | Columns are z-scored per column before the common scale, so different units compare (Iris) |

## Lines and colour

```cpp
pcp->SetLineMode(PCPLineMode::Curved);      // Catmull-Rom; straight is default
pcp->SetCurveTension(0.6f);
pcp->SetLineWidth(1.5f);
pcp->SetLineAlpha(0.0f);                    // 0 = automatic from record count

pcp->SetColorMode(PCPColorMode::ByValue);   // ByGroup | ByValue | PerRecord | Single
pcp->SetColorDimension("accuracy");
pcp->SetColormap(HeatmapColormap::Turbo);
```

Straight lines are the default deliberately — curves look softer but
misrepresent values between axes.

## Interaction

- **Hover** a line: it emphasises and a tooltip lists every dimension value.
  Emphasised lines are drawn full-strength inside a luminance-opposed casing
  (dark lines get a light halo, pale lines a dark one), so the highlight always
  contrasts with ordinary lines of the same colour around it.
- **Click** a line: pin it (bold, on top); click again to unpin.
- **Drag on an axis**: create a range brush. Records failing any brushed axis
  dim to the context colour (`SetContextStyle`) or vanish
  (`SetShowContext(false)`). Brushes on one axis OR together; across axes they
  AND. Click an axis to clear its brushes.
- **Drag an axis header** (just above the axis top): reorder the axes.
- **Double-click an axis**: invert it.

```cpp
pcp->AddBrush("learning_rate", 0.001, 0.01);       // programmatic brushes
pcp->onBrushChanged     = [](size_t passing) { /* live count */ };
pcp->onRecordClick      = [](int record) { /* ... */ };
pcp->onAxisOrderChanged = [](const std::vector<std::string>& order) { /* persist */ };
```

Brushing, pinning and hovering never re-run the label solver — the engine's
dirty model rebuilds the label plan only for data, geometry or style changes.

## Named properties

Created by name (`UltraCanvasElementRegistry::Create("parallel-coordinate-chart", ...)`),
the chart is configurable across any module boundary through
`IConfigurableElement`: `title`, `lineAlpha`, `lineWidth`, `curveTension`,
`lineMode` (`"straight"`/`"curved"`), `showEndpointLabels`.

## Best Practices

1. Feed columns, not pre-normalised values — the model normalises, and brushes
   are specified in data space.
2. Leave `SetLineAlpha(0)` for datasets above a few hundred records; the
   automatic alpha keeps dense plots readable.
3. Use `CommonScale` + `ZScore` only when the columns are comparable
   quantities; otherwise per-axis ranges tell the truer story.
4. `Model()` exposes the full `ParallelAxisModel` for programmatic reordering,
   visibility and brush inspection.
