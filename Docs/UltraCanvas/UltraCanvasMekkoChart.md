# UltraCanvasMekkoChart

Comprehensive Mekko (Marimekko / mosaic) chart element for UltraCanvas
(`include/Plugins/Charts/UltraCanvasMekkoChart.h`,
`Plugins/Charts/UltraCanvasMekkoChart.cpp`).

A Mekko chart is a two-dimensional stacked chart: each **column** is a
variable-width vertical bar whose width is proportional to the column's total,
and each column is subdivided into **series segments**. In the classic
Marimekko form every column is normalized to 100% height, so both axes carry
part-to-whole information at once — column widths show the mix across
categories, segment heights show the mix within each category.

| Mode | Enum | Description |
| --- | --- | --- |
| Marimekko | `MekkoMode::Marimekko` | 100% stacked columns, Y axis in percent; both dimensions show shares |
| Bar-Mekko | `MekkoMode::BarMekko` | Absolute stacked columns: height = column total, width still proportional to the total |

The demo (`Apps/DemoApp/UltraCanvasMekkoChartExamples.cpp`) shows a market
share by sector Marimekko, a product vs. region sales mix with percent labels
and a cumulative X axis, and a Bar-Mekko cloud-provider revenue example, plus
interactive controls for mode, labels, sorting and random data.

## Creating a chart

```cpp
#include "Plugins/Charts/UltraCanvasMekkoChart.h"

auto data = std::make_shared<MekkoChartDataVector>();
data->AddSeries("USA",    Color(25, 60, 130, 255));
data->AddSeries("China",  Color(235, 75, 105, 255));
data->AddSeries("Europe");                    // no color -> automatic palette

data->AddColumn("Technology", {88, 44, 30});  // one value per series, in order
data->AddColumn("Finance",    {95, 55, 48});
data->SetValue("Finance", "Europe", 51.0);    // cells addressable by name too

auto chart = CreateMekkoChartWithData("mekko1", 20, 20, 640, 420,
                                      data, "Market Share by Sector ($B)");
```

`CreateMekkoChartElement(id, x, y, w, h)` creates an empty element; attach data
later with `SetMekkoDataSource()`.

## Data model

`MekkoChartDataVector` implements `IChartDataSource` and stores:

* **Series** (`AddSeries(name, color)`) — the stacked segments / legend rows.
  A `Colors::Transparent` color means "use the chart's default palette".
* **Columns** (`AddColumn(label, values)`) — the variable-width bars. Values
  align with the series order; missing values default to 0 and negative values
  are ignored in totals.

Helpers: `SetValue(col, series, v)` (by index or by name), `GetValue`,
`GetColumnTotal`, `GetSeriesTotal`, `GetGrandTotal`, `SetColumnCategory`,
`FindSeries` / `FindColumn`, `ClearData`.

`LoadFromCSV(path)` reads a matrix CSV: header row
`Column,Series1,Series2,...`, then one row per column with its label and
values.

After mutating a data vector that is already attached to a chart, call
`chart->SetMekkoDataSource(data)` again (or resize/redraw) to force a layout
rebuild.

## Configuration

```cpp
chart->SetMekkoMode(UltraCanvasMekkoChartElement::MekkoMode::Marimekko);
chart->SetColumnSortMode(UltraCanvasMekkoChartElement::ColumnSortMode::TotalDescending);
chart->SetSegmentLabelMode(UltraCanvasMekkoChartElement::SegmentLabelMode::ValueAndPercent);
chart->SetColumnHeaderMode(UltraCanvasMekkoChartElement::ColumnHeaderMode::WidthPercent);
chart->SetLegendPosition(UltraCanvasMekkoChartElement::LegendPosition::Right);
chart->SetLegendWidth(120);
chart->SetColumnGap(2.0f);
chart->SetSegmentBorder(Colors::White, 1.0f);
chart->SetSegmentCornerRadius(0.0f);
chart->SetShowColumnLabels(true);       // category labels under the columns
chart->SetShowYAxisLabels(true);        // percent (Marimekko) or value (Bar-Mekko) scale
chart->SetShowCumulativeXAxis(true);    // cumulative % ticks at column boundaries
chart->SetLabelStyle(Color(33, 33, 33, 255), 11.0f);
chart->SetSegmentLabelStyle(10.0f, 14.0f /*min height*/, true /*auto contrast*/);
chart->SetValueUnits("$", "B");         // prefix/suffix for formatted values
chart->SetValueFormatter([](double v) { return MyMoneyFormat(v); });
```

* **`ColumnSortMode`**: `DataOrder`, `TotalDescending`, `TotalAscending`,
  `Alphabetical`.
* **`SegmentLabelMode`**: `NoLabels`, `Value`, `Percent`, `ValueAndPercent`,
  `SeriesName`, `SeriesAndValue`. Labels are skipped when the segment is
  shorter than the configured minimum height or narrower than the text; with
  auto-contrast enabled the text switches between dark and white based on the
  segment color's luminance.
* **`ColumnHeaderMode`**: `NoHeader`, `WidthPercent` (column share of the grand
  total), `Total`, `TotalAndPercent`. In Bar-Mekko mode headers sit just above
  each column's top.
* **`LegendPosition`**: `Hidden`, `Right`, `Bottom`, `Top`. Hovering a legend
  entry dims all other series.

## Interaction

* **Tooltips** — hovering a segment shows column / series, the value, percent
  of column, percent of grand total and the optional column category. Disable
  with `SetEnableTooltips(false)` (inherited from the chart base class).
* **Hover highlight** — the hovered segment gets an outline
  (`SetHighlightHoveredSegment(false)` to turn off); `GetHoveredSegment()`
  returns the current `(columnIndex, seriesIndex)`.
* **Click callback**:

```cpp
chart->SetOnSegmentClick([](size_t columnIndex, size_t seriesIndex) {
    // e.g. drill down into the clicked cell
});
```

## Notes

* Column widths always encode the column totals in both modes; the grand total
  must be positive for anything to render.
* The element inherits common behaviour from `UltraCanvasChartElementBase`:
  title rendering, background/plot-area styling, tooltip plumbing and redraw
  management. The Mekko element draws its own percent grid and axis labels, so
  the base grid/axes are disabled by default.
