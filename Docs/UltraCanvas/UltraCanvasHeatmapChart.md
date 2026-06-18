# UltraCanvasHeatmapChart

A 2D heatmap / matrix chart element. Each grid cell's colour encodes a scalar
value via a configurable colour map. It is intentionally a generic building
block: feed it any 2D matrix — correlation matrices, confusion matrices,
density grids, or audio **spectrograms** produced by an STFT in front of it.

- Header: `include/Plugins/Charts/UltraCanvasHeatmapChart.h`
- Source: `Plugins/Charts/UltraCanvasHeatmapChart.cpp`
- Base class: `UltraCanvasChartElementBase`

## Quick start

```cpp
#include "Plugins/Charts/UltraCanvasHeatmapChart.h"

auto heatmap = UltraCanvas::CreateHeatmapChartElement("heatmap1", 20, 20, 600, 400);

// Row-major data: values[row * cols + col]
std::vector<double> data(cols * rows);
// ... fill data ...
heatmap->SetData(data, cols, rows);

heatmap->SetColormap(UltraCanvas::HeatmapColormap::Viridis);
heatmap->SetChartTitle("My Heatmap");
container->AddChild(heatmap);
```

You can also pass a 2D matrix directly:

```cpp
std::vector<std::vector<double>> matrix = {
    {1.0, 0.5, 0.2},
    {0.5, 1.0, 0.8},
    {0.2, 0.8, 1.0},
};
heatmap->SetData(matrix);
heatmap->SetRowLabels({"A", "B", "C"});
heatmap->SetColumnLabels({"A", "B", "C"});
heatmap->SetShowCellValues(true);
heatmap->SetShowCellBorders(true);
```

## Data model

The matrix is stored row-major (`values[row * cols + col]`).

| Method | Description |
|---|---|
| `SetData(flat, columns, rowsCount)` | Set from a flat row-major buffer. |
| `SetData(matrix)` | Set from a vector of rows. |
| `Resize(columns, rowsCount, fill)` | Allocate an empty grid. |
| `SetValue(col, row, v)` / `GetValue(col, row)` | Per-cell access. |
| `GetColumns()` / `GetRows()` | Current dimensions. |
| `ClearData()` | Drop all data. |

`NaN` cells are not drawn in the colour map; they are painted with the NaN
colour (default transparent — see `SetNaNColor`).

## Value range and scaling

By default the value range is computed automatically from the data
(ignoring NaNs). To pin it:

```cpp
heatmap->SetValueRange(0.0, 1.0);   // disables auto range
heatmap->SetScale(UltraCanvas::HeatmapScale::Logarithmic); // good for magnitudes
```

| Method | Description |
|---|---|
| `SetAutoValueRange(bool)` | Re-enable automatic min/max. |
| `SetValueRange(min, max)` | Fix the range (disables auto). |
| `SetScale(Linear \| Logarithmic)` | Mapping from value to colour position. |

## Colour maps

Built-in maps: `Grayscale`, `Viridis`, `Inferno`, `Magma`, `Plasma`, `Jet`,
`Hot`, `Cool`. `Inferno`/`Magma` give the classic spectrogram look.

```cpp
heatmap->SetColormap(UltraCanvas::HeatmapColormap::Inferno);
heatmap->SetReverseColormap(true);

// Or supply your own evenly spaced anchors (>= 2):
heatmap->SetCustomColormap({ Color(0,0,0), Color(0,128,255), Color(255,255,255) });
```

## Orientation and render strategy

```cpp
heatmap->SetRowOrder(UltraCanvas::HeatmapRowOrder::BottomUp); // row 0 at bottom
heatmap->SetRenderMode(UltraCanvas::HeatmapRenderMode::Auto);
```

- **Row order** — `TopDown` (default, natural matrix orientation) or
  `BottomUp` (e.g. lowest frequency at the bottom of a spectrogram).
- **Render mode**:
  - `Cells` — one filled rectangle per cell; supports cell borders and value
    text; crisp edges. Best for small/medium matrices.
  - `Image` — rasterize to a pixmap and blit it scaled; smooth and cheap for
    very large grids (spectrograms).
  - `Auto` — `Cells` for small grids, `Image` for large ones.

## Labels, titles, colour bar

```cpp
heatmap->SetColumnLabels({...});
heatmap->SetRowLabels({...});
heatmap->SetAxisTitles("Time", "Frequency");
heatmap->SetShowColorBar(true);
```

Labels are automatically thinned (stepped) when there are too many to fit.
Hovering a cell shows a tooltip with its row/column label and value.

## Using it for spectrograms

A spectrogram is a heatmap whose matrix is produced by a Short-Time Fourier
Transform (STFT) of an audio signal: columns = time frames, rows = frequency
bins, cell value = magnitude. The planned spectrogram layer computes that
matrix and feeds this element, typically with:

```cpp
heatmap->SetColormap(UltraCanvas::HeatmapColormap::Inferno);
heatmap->SetScale(UltraCanvas::HeatmapScale::Logarithmic);
heatmap->SetRowOrder(UltraCanvas::HeatmapRowOrder::BottomUp);
heatmap->SetRenderMode(UltraCanvas::HeatmapRenderMode::Image);
heatmap->SetAxisTitles("Time (s)", "Frequency (Hz)");
```
