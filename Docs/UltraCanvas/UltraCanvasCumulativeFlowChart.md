# UltraCanvasCumulativeFlowChart

A cumulative flow diagram (CFD) element — the standard Kanban analytics
chart: stacked per-stage bands over time, where band thickness is the WIP of
a stage and the horizontal distance between band boundaries reads as
lead/cycle time. Binds directly to a `KanbanDataSource` (deriving stage
counts from its move history) or takes manual series, and draws the classic
**Lead Time**, **Cycle Time** and **WIP** span annotations.

- Element: `include/Plugins/Charts/UltraCanvasCumulativeFlowChart.h` / `Plugins/Charts/UltraCanvasCumulativeFlowChart.cpp`
- Companion of: [`UltraCanvasKanbanBoard.md`](UltraCanvasKanbanBoard.md) (one data source, two views)
- Demo: the "Cumulative Flow" tab of `Apps/DemoApp/UltraCanvasKanbanBoardExamples.cpp`

## Quick start — from a Kanban board

```cpp
#include "Plugins/Charts/UltraCanvasCumulativeFlowChart.h"
using namespace UltraCanvas;

// `board` is a KanbanDataSource whose mutations were dated with
// SetCurrentDate(...) — every AddCard/MoveCard/RemoveCard is in its history.
auto cfd = CreateCumulativeFlowChartFromKanban(
        "cfd1", 10, 10, 900, 420, board,
        GanttDate(2026, 5, 1), GanttDate(2026, 7, 29), /*stepDays=*/7,
        "Team flow (weekly)");
container->AddChild(cfd);
```

`LoadFromKanban(board, from, to, stepDays)` creates one stage per board
column (board order; the first column becomes the **top** band, the last —
Done — the **bottom** band, per CFD convention) and one period every
`stepDays`, replaying the history with `GetColumnCountsAt`. The data is
copied; call it again to refresh after further board changes. Custom column
colors carry over; unset ones take the style palette.

## Quick start — manual series

```cpp
auto cfd = CreateCumulativeFlowChartElement("cfd2", 10, 10, 900, 420);
cfd->SetStyle(CumulativeFlowStyles::CreateOlive());
cfd->AddStage("To do", {17, 15, 12, 12, 10, 5, 2, 1, 1});
cfd->AddStage("Doing", {3, 3, 3, 3, 3, 5, 5, 5, 4});
cfd->AddStage("QA",    {0, 2, 3, 2, 2, 3, 3, 3, 2});
cfd->AddStage("Done",  {0, 0, 2, 3, 5, 7, 10, 11, 13});
cfd->SetPeriodLabels({"Month 1", "Month 2", /*...*/ "Month 9"});
cfd->AddLeadTimeAnnotation();
cfd->AddCycleTimeAnnotation();
cfd->AddWipAnnotation();
```

Stage order follows the workflow: `stages[0]` is the first stage (top band),
`stages.back()` is Done (bottom band). `values[i]` is the item count in that
stage at period *i*; vectors of different lengths read missing entries as 0.

## Annotations

Band *boundaries* are indexed by stage: boundary *k* is the top edge of
stage *k* (the cumulative count of stages *k*..last). Boundary 0 is the
overall top (arrival) curve; the last boundary is the top of Done (the
departure curve).

- `AddLeadTimeAnnotation()` — horizontal double-headed arrow between the
  arrival curve and the Done curve, auto-placed at half the final total.
- `AddCycleTimeAnnotation()` — horizontal arrow between boundary 1 (start
  of active work) and Done, auto-placed slightly lower.
- `AddWipAnnotation()` — vertical arrow across the active-work thickness
  (boundary 1 to Done top), auto-placed at one third of the time range.
- `AddAnnotation(CFDAnnotation)` — custom spans: pick `kind`
  (`HorizontalSpan`/`VerticalSpan`), the two boundaries, and an explicit
  `value` (item count) or `position` (period, may be fractional) instead of
  the auto placement.

Each annotation renders as a double-headed arrow with a boxed caption;
spans too small to fit the arrowheads are skipped rather than drawn
illegibly.

## Style

Everything lives in `CumulativeFlowChartStyle`: paddings, band alpha, edge
lines (stroked in the band color darkened), per-period marker dots, grid and
axis colors, tick font sizes and an explicit `yTickStep` (0 = automatic
"nice" step), the right-hand legend panel (`legendPosition`, panel/border
colors, dot radius), annotation colors/fonts, `fontFamily` and the stage
color `palette`. Presets:

| Preset | Look |
|---|---|
| `CumulativeFlowStyles::CreateModern()` | Light background, vibrant blue/indigo/amber/teal bands |
| `CumulativeFlowStyles::CreateOlive()` | Olive/amber report style of the classic cycle-time slide |
| `CreateDark()` | Dark background variant |

`SetStyle` / `EditStyle()+StyleChanged()` work as on the other chart
elements; `SetChartTitle` draws a centered title above the plot.

## Interaction

Hovering snaps to the nearest period: a dashed guide line marks it, a
tooltip lists the per-stage counts and the total, and
`onHoveredPeriodChanged(periodIndex)` fires (-1 when the pointer leaves the
plot).
