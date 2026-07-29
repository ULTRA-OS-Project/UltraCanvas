# UltraCanvasDumbbellChart

A dumbbell (a.k.a. DNA, barbell or connected-dot) chart: one horizontal row per
category, two dots joined by a connector. It is the clearest way to show the
**gap** between two paired values — men vs women, before vs after, min vs max —
because the connector length *is* the difference.

- Element: `include/Plugins/Charts/UltraCanvasDumbbellChart.h` / `Plugins/Charts/UltraCanvasDumbbellChart.cpp`
- Base class: `UltraCanvasChartElementBase` (`include/Plugins/Charts/UltraCanvasChartElementBase.h`)
- Demo: `Apps/DemoApp/UltraCanvasDumbbellChartExamples.cpp` (Charts → Dumbbell Chart)

## Quick start

```cpp
#include "Plugins/Charts/UltraCanvasDumbbellChart.h"

auto chart = UltraCanvas::CreateDumbbellChart("usage", 20, 20, 800, 500);
chart->SetChartTitle("Social media usage by age and gender");
chart->SetLegendLabels("Men", "Women");
chart->SetAxisLabel("Percentage using the platform regularly (%)");
chart->SetValueFormat("%.0f%%");

chart->AddDataPoint("Instagram 16-20", 68, 83, "Men", "Women");
chart->AddDataPoint("Instagram 21-25", 66, 83, "Men", "Women");
chart->AddDataPoint("TikTok 16-20",    70, 88, "Men", "Women");

container->AddChild(chart);
```

## Data

`DumbbellDataPoint` derives from `ChartDataPoint`, so the chart also satisfies
the generic `IChartDataSource` contract used by the rest of the chart family.

```cpp
struct DumbbellDataPoint : ChartDataPoint {
    std::string categoryLabel;   // Row label
    double      value1, value2;  // The two paired values
    std::string label1, label2;  // Group names (used in tooltips)
    Color       color1, color2;  // Per-row dot colours (transparent = chart default)
    Color       lineColor;       // Per-row connector colour
    std::string tooltipText;     // Optional replacement tooltip

    double GetRange() const;     // |value2 - value1|
    double GetDelta() const;     // value2 - value1 (signed)
    double GetMinValue() const;
    double GetMaxValue() const;
};
```

Rows can be added one at a time, or through a shared `DumbbellDataSource`:

```cpp
auto data = UltraCanvas::CreateDumbbellDataSource();
data->AddDumbbellPoint("Category A", 45.5, 67.2, "Before", "After");
data->AddDumbbellPoint("Category B", 52.1, 71.8, "Before", "After");

auto chart = UltraCanvas::CreateDumbbellChartWithData("chart", 20, 20, 800, 400,
                                                      data, "Before / after");
```

`DumbbellDataSource::LoadFromCSV` reads `categoryLabel,value1,value2[,label1,label2]`;
rows whose value columns do not parse are skipped, so a header line needs no
special handling. `LoadFromArray` folds consecutive `ChartDataPoint` pairs into
one row each.

Per-row colours override the chart defaults:

```cpp
UltraCanvas::DumbbellDataPoint row("July", 9, 14, "Low", "High");
row.color1    = Color(90, 140, 210, 255);
row.color2    = Color(230, 140, 60, 255);
row.lineColor = Color(215, 215, 215, 255);
chart->AddDataPoint(row);
```

Note that the legend swatches always use the chart-level colours, so set
`SetDefaultColors()` to something representative when rows carry their own.

## Layout

Rows are laid out top to bottom inside the plot area. By default
(`SetAutoFitRows(true)`) the row pitch is the plot height divided by the row
count, so every row is always visible however many there are — the 39-row demo
tab relies on this. The dot radius is clamped to half the pitch so neighbouring
rows never touch, whatever `SetDotRadius` says.

Turn auto-fit off to pin the pitch to `rowHeight + rowSpacing` instead; combine
it with `GetPreferredHeight()` to size the element (or its scrolling parent) so
that nothing is clipped:

```cpp
chart->SetAutoFitRows(false);
chart->SetRowHeight(26.0f);
chart->SetRowSpacing(8.0f);
chart->SetSize(width, chart->GetPreferredHeight());
```

The category label column is `SetCategoryLabelWidth()` wide (default 170px);
labels are right-aligned against the plot area and truncated with an ellipsis
when they do not fit.

## Axis range

The value axis is horizontal and computed from the data by default: an 8%
margin, then snapped out to round tick values (`SetUseNiceAxisBounds`).

```cpp
chart->SetValueRange(0.0, 100.0);      // Pin the axis (percentages, scores, ...)
chart->ClearValueRange();              // Back to automatic
chart->SetIncludeZeroInRange(true);    // Keep zero visible (e.g. temperatures)
chart->SetAxisTickCount(5);
chart->SetShowAxisValues(false);
```

Tick labels use the same `SetValueFormat()` string as the value labels.

## Value labels

`SetShowValueLabels()` (inherited from the chart base) turns the numbers on the
dots on and off. `SetValueLabelPlacement()` decides where they go:

| Placement | Behaviour |
|-----------|-----------|
| `Auto` (default) | Inside the dots when the text fits **and** the dots are far enough apart; outside otherwise |
| `Inside` | Always centred on the dots, in white |
| `Outside` | Always beyond the outer edge of each dot, tinted with that dot's colour |

`Outside` places each label past its *own* dot's outer edge, so the two never
collide however narrow the gap gets.

## Sorting

`SetSortOrder()` reorders the drawn rows without touching the data source, so
indices reported by the callbacks always refer to the original rows:

```cpp
chart->SetSortOrder(UltraCanvas::DumbbellSortOrder::RangeDescending); // widest gap first
```

Available: `InsertionOrder`, `CategoryAscending/Descending`,
`Value1Ascending/Descending`, `Value2Ascending/Descending`,
`RangeAscending/Descending`.

## Interaction

Hovering a row highlights it; hovering a dot outlines it and shows a tooltip
with the category, both values and the difference. Clicks are reported through
two callbacks — `onDotClick` also fires `onRowClick`:

```cpp
chart->onRowClick = [](size_t rowIndex) { /* ... */ };
chart->onDotClick = [](size_t rowIndex, int dotIndex) { /* 0 = value1, 1 = value2 */ };
```

Tooltips are rendered as Pango markup. The generated text escapes `&`, `<` and
`>` for you; text you put in `DumbbellDataPoint::tooltipText` is passed through
verbatim so it can carry markup of its own.

## Configuration reference

```cpp
// Data
void AddDataPoint(category, value1, value2, label1, label2);
void AddDataPoint(const DumbbellDataPoint&);
void ClearData();
void SetDumbbellDataSource(std::shared_ptr<DumbbellDataSource>);
void SetSortOrder(DumbbellSortOrder);

// Geometry
void SetDotRadius(float);        // 2 - 24 px, clamped to half the row pitch
void SetLineWidth(float);        // 0.5 - 12 px
void SetRowHeight(float);        // Used when auto-fit is off
void SetRowSpacing(float);
void SetAutoFitRows(bool);
float GetPreferredHeight() const;

// Colours
void SetDefaultColors(color1, color2, lineColor);
void SetGroupColors(color1, color2);
void SetConnectorColor(lineColor);

// Labels
void SetShowCategoryLabels(bool);
void SetCategoryLabelWidth(float);
void SetCategoryLabelFontSize(float);
void SetCategoryLabelColor(const Color&);
void SetValueFormat(const std::string&);          // printf format, e.g. "%.1f%%"
void SetValueLabelPlacement(DumbbellValueLabelPlacement);

// Legend
void SetShowLegend(bool);
void SetLegendLabels(label1, label2);
void SetLegendPosition(DumbbellLegendPosition);   // TopRight (default), TopLeft, BottomRight, BottomLeft

// Axis
void SetShowAxisValues(bool);
void SetAxisTickCount(int);
void SetAxisLabel(const std::string&);
void SetIncludeZeroInRange(bool);
void SetUseNiceAxisBounds(bool);
void SetValueRange(double, double);
void ClearValueRange();

void SetShowRowHighlight(bool);
```

Inherited from the chart base: `SetChartTitle` / `SetTitle`, `SetShowGrid`,
`SetShowValueLabels`, `SetBackgroundColor`, `SetPlotAreaColor`, `SetGridColor`,
`SetEnableTooltips`.

## Notes

- Rendering is entirely in element-local coordinates through `IRenderContext`,
  so the chart carries no platform-specific code.
- With no data the base class draws its standard empty state.
- Complexity is O(n) for both rendering and hit testing; the auto-fit layout
  keeps 50+ rows legible, beyond which a scrolling parent with auto-fit off and
  `GetPreferredHeight()` reads better.
