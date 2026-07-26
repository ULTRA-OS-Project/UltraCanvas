# UltraCanvasGanttChart

A full project-schedule Gantt chart: a task table on the left, a scrollable,
zoomable timeline on the right. Supports task hierarchy with expand/collapse
and WBS numbering, milestones, four dependency types with arrow routing,
progress display, critical-path analysis, and a preset-based design and
palette system modeled on common real-world Gantt styles.

- Element: `include/Plugins/Charts/UltraCanvasGanttChart.h` / `Plugins/Charts/UltraCanvasGanttChart.cpp`
- Date helpers (header-only): `include/Plugins/Charts/UltraCanvasCalendarDate.h`
- Demo: `Apps/DemoApp/UltraCanvasGanttChartExamples.cpp`

## Quick start

```cpp
#include "Plugins/Charts/UltraCanvasGanttChart.h"
using namespace UltraCanvas;

auto data = std::make_shared<GanttDataSource>();

// Phases become summaries automatically once they have children.
int design = data->AddTask("Design",    GanttDate(2026, 2, 2),  GanttDate(2026, 2, 20));
int sketch = data->AddTask("Sketches",  GanttDate(2026, 2, 2),  GanttDate(2026, 2, 6), design);
int proto  = data->AddTask("Prototype", GanttDate(2026, 2, 9),  GanttDate(2026, 2, 18), design);
int ship   = data->AddMilestone("Design freeze", GanttDate(2026, 2, 20), design);

data->SetTaskProgress(sketch, 1.0f);
data->SetTaskProgress(proto, 0.4f);
data->SetTaskAssignee(proto, "Iris Chen");
data->AddDependency(sketch, proto);                    // finish-to-start
data->AddDependency(proto, ship);

auto gantt = CreateGanttChartWithData("gantt1", 10, 10, 900, 400,
                                      data, GanttDesign::Modern);
gantt->SetToday(GanttDate(2026, 2, 12));
container->AddChild(gantt);
```

## Dates

`GanttDate` stores a serial day count since 1970-01-01 (proleptic Gregorian);
construct with `GanttDate(year, month, day)`. Start and end dates are both
**inclusive**, so a one-day task has `start == end` and `DurationDays() == 1`.

## Data model

### Tasks

`GanttDataSource::AddTask(name, start, end, parentId = -1)` returns the new
task id (or -1 for invalid input). A task with children renders as a
**summary**: its dates and progress roll up from its subtree (duration-weighted)
and it gains an expand/collapse control. `AddMilestone(name, date, parent)`
adds a zero-duration point event. `GanttTask::shape` can force
`Bar` / `Summary` / `Milestone` rendering instead of the automatic choice.

Per-task setters: `SetTaskDates`, `SetTaskProgress` (0..1), `SetTaskColor`
(overrides the palette), `SetTaskAssignee`, `SetTaskPriority`
(`Low`..`Urgent`, drawn as a colored chip in the table), `SetTaskNotes`
(extra tooltip line), `SetTaskExpanded` / `SetAllExpanded`.

### Dependencies

```cpp
data->AddDependency(pred, succ);                                    // finish-to-start
data->AddDependency(a, b, GanttDependencyType::StartToStart, 3);    // 3-day lag
```

All four PDM link types are supported: `FinishToStart`, `StartToStart`,
`FinishToFinish`, `StartToFinish`, each with a signed lag in days.

### Critical path

`ComputeCriticalPath()` runs a standard forward/backward pass over the
dependency graph (leaf tasks only) and returns the ids of zero-slack tasks.
Enable `SetShowCriticalPath(true)` on the element to tint those bars and their
connecting arrows with `style.criticalColor`.

## Designs and palettes

`ApplyDesign(design)` replaces the whole style with a preset (data and the
today marker are kept):

| `GanttDesign` | Look |
|---|---|
| `Classic` | Flat single-color bars, month header, ID + Task table, print style |
| `Modern` | Rounded colorful phase bars, week + weekday-letter header, phase names inside summary bars |
| `Professional` | Project-tool look: bracket summaries, in-bar progress, names right of bars, week header |
| `Soft` | Pastel report style: thin line bars with square end caps, dashed dependency arrows |
| `Minimal` | Minimal-modern: wide info table (assignee, priority, %), light-track progress bars, day grid |
| `Dark` | Dark background variant of Modern |

`SetPalette(GanttPalette)` swaps only the bar color cycle:
`CorporateBlue`, `Vibrant`, `Pastel`, `Ocean`, `Sunset`, `Forest`, `Slate`,
`Mono` — or pass any color list to `SetCustomPalette`. How colors are assigned
is controlled by `style.colorMode`: `ByPhase` (top-level ancestor's color,
default), `ByRow`, `ByPriority`, or `Single`.

## Style

Every visual knob lives in `GanttChartStyle` (see the header for the full
list): row/bar metrics, table columns, header tiers and captions, grid and
weekend shading, bar/summary/milestone shapes, progress display
(`DarkerFill`, `LightTrack`, `InnerBar`), label placement, dependency line
style, selection/hover tints, fonts, and the date format. Two workflows:

```cpp
// Replace wholesale
GanttChartStyle s = GanttChartStyles::CreateProfessional();
s.rowHeight = 40;
gantt->SetStyle(s);

// Or tweak in place
gantt->EditStyle().shadeWeekends = false;
gantt->StyleChanged();
```

### Table columns

```cpp
gantt->SetColumns({
    {GanttColumnType::Name,      "Task",  220},
    {GanttColumnType::Assignee,  "Owner", 110},
    {GanttColumnType::Progress,  "%",      50},
});
```

Available columns: `Wbs` (1, 1.1, 1.2 …), `Name` (indented, with the
expander), `StartDate`, `EndDate`, `Duration`, `Progress`, `Assignee`,
`Priority`. `SetShowTable(false)` hides the table entirely.

### Time scale

`SetTimeScale()` chooses the minor header tier: `Days`, `Weeks`, `Months`,
`Quarters`, or `Auto` (picked from the zoom level). `SetDayWidth(px)` is the
zoom; `FitToRange()` picks a day width so the whole project fits.

## Interaction

- Mouse wheel scrolls vertically (horizontally when there is nothing to
  scroll vertically), Shift+wheel scrolls horizontally, **Ctrl+wheel zooms**
  around the cursor. Dragging the timeline pans.
- Hovering a row shows a tooltip (name, dates, duration, progress, assignee,
  predecessors, notes); clicking selects; the triangle in the Name column —
  or a double click — collapses/expands a phase.
- `ScrollToDate(date)` / `ScrollToTask(id)` / `SelectTask(id)` from code.

Callbacks:

```cpp
gantt->onTaskClick        = [](int id) { ... };
gantt->onTaskDoubleClick  = [](int id) { ... };
gantt->onTaskToggled      = [](int id, bool expanded) { ... };
gantt->onSelectionChanged = [](int id) { ... };   // -1 = cleared
```

## Element API summary

| Method | Description |
|---|---|
| `SetGanttDataSource(ds)` | Attach the task/dependency model. |
| `ApplyDesign(design)` | Load a design preset. |
| `SetPalette(p)` / `SetCustomPalette(colors)` | Swap the bar color cycle. |
| `SetStyle(s)` / `EditStyle()` + `StyleChanged()` | Full style control. |
| `SetTimeScale(scale)` / `SetDayWidth(px)` / `FitToRange()` | Timeline resolution & zoom. |
| `SetShowTable(bool)` / `SetColumns(cols)` | Task table configuration. |
| `SetShowDependencies(bool)` / `SetShowCriticalPath(bool)` | Link display. |
| `SetToday(date, showLine)` | Dashed today marker. |
| `ScrollToDate(d)` / `ScrollToTask(id)` | Programmatic navigation. |
| `SelectTask(id)` / `GetSelectedTaskId()` | Selection. |
