# UltraCanvasCalendarHeatmap

A calendar / contribution-graph style heatmap (the "GitHub contributions" look):
weeks are columns, weekdays are rows, and each cell's colour encodes a per-day
value. It's a thin date-aware layer on top of
[`UltraCanvasHeatmapChart`](UltraCanvasHeatmapChart.md).

- Element: `include/Plugins/Charts/UltraCanvasCalendarHeatmap.h` / `Plugins/Charts/UltraCanvasCalendarHeatmap.cpp`
- Date helpers (header-only): `include/Plugins/Charts/UltraCanvasCalendarDate.h`

## Quick start

```cpp
#include "Plugins/Charts/UltraCanvasCalendarHeatmap.h"

auto cal = UltraCanvas::CreateCalendarHeatmapElement("cal1", 20, 20, 900, 180);

// Option A: a range plus individual day values
cal->SetDateRange(2023, 1, 1, 2023, 12, 31);
cal->SetValue(2023, 6, 14, 7);   // 7 "contributions" on that day
cal->AddValue(2023, 6, 14, 1);   // accumulate -> 8

// Option B: consecutive daily values from a start date (sets the range)
std::vector<double> daily = loadDailyCounts();
cal->SetDailyValues(2023, 1, 1, daily);

cal->SetChartTitle("Contributions");
container->AddChild(cal);
```

## Defaults

Preconfigured to resemble a contribution graph:

- Colour map `Greens`, 5 discrete levels
- Rounded cells with a small gap
- Month labels along the top, Mon/Wed/Fri weekday labels on the left
- Out-of-range / empty days use a light "empty" colour (`SetNaNColor` to change)
- Colour bar off

All of the underlying heatmap options still apply — e.g. switch the palette with
`SetColormap(...)`, change the bucket count with `SetColorLevels(...)`, or adjust
cell styling with `SetCellGap()` / `SetCellShape()` / `SetCellCornerRadius()`.

## Configuration

```cpp
cal->SetWeekStart(UltraCanvas::CalendarWeekStart::Monday); // default Sunday
cal->SetMissingDayValue(0.0);   // value for in-range days without an entry
```

| Method | Description |
|---|---|
| `SetDateRange(sy,sm,sd, ey,em,ed)` | Set the displayed date span. |
| `SetValue(y,m,d, v)` / `AddValue(y,m,d, v)` | Set / accumulate a day's value. |
| `SetDailyValues(sy,sm,sd, values)` | Consecutive daily values; sets the range. |
| `SetWeekStart(Sunday\|Monday)` | First row / week alignment. |
| `SetMissingDayValue(v)` | Fill value for in-range days with no entry. |
| `ClearValues()` | Remove all values and the range. |

The matrix is rebuilt lazily on the next render after any change; call `Build()`
to force it. Hovering a cell shows its value via the heatmap tooltip.

## Notes / limitations

- Days outside the range (the padding needed to fill whole week columns) are
  drawn with the NaN colour.
- Date handling uses the proleptic Gregorian calendar (Hinnant's algorithms),
  verified for civil↔serial round-trips and weekday correctness across leap
  years.
