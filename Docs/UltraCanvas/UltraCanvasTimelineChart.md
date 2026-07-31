# UltraCanvasTimelineChart

A **chronological timeline**: milestones and spans placed *to scale* on a real
date axis, with a two-tier header, automatic lane packing, callout labels, and
wheel zoom / drag pan. It deliberately has no task table and no dependency
graph — it is the stakeholder view, not the working schedule.

**Pick the right element:**

| You want | Use |
|---|---|
| Dates to scale, milestones and spans, no task table | `UltraCanvasTimelineChart` (this element) |
| A presentation/story with N events and rich text, evenly spaced | [`UltraCanvasTimelineDiagram`](UltraCanvasTimelineDiagram.md) |
| A full project schedule: tasks, dependencies, progress, critical path | [`UltraCanvasGanttChart`](UltraCanvasGanttChart.md) |
| Fixed process steps with a current position | [`UltraCanvasStepper`](UltraCanvasStepper.md) |

**Namespace:** `UltraCanvas`
**Header:** `include/Plugins/Charts/UltraCanvasTimelineChart.h`
**Source:** `Plugins/Charts/UltraCanvasTimelineChart.cpp`
**Date axis (header-only):** `include/Plugins/Charts/UltraCanvasTimeAxis.h`
**Base class:** `UltraCanvasChartElementBase`
**Demo:** `Apps/DemoApp/UltraCanvasTimelineChartExamples.cpp` (Info Graphics > Timeline Chart)
**Version:** 1.0.0
**Last Modified:** 2026-07-31
**Author:** UltraCanvas Framework

## Class hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasChartElementBase
            └── UltraCanvasTimelineChart
```

## Quick start

```cpp
#include "Plugins/Charts/UltraCanvasTimelineChart.h"
using namespace UltraCanvas;

auto data = std::make_shared<TimelineChartDataSource>();

int design = data->AddSpan("Design", 2026, 2, 2, 2026, 3, 20);
int build  = data->AddSpan("Build",  2026, 3, 10, 2026, 6, 30);
int freeze = data->AddMilestone("Feature freeze", 2026, 6, 1);
data->AddBookend("Project start", TimeAxis::Serial(2026, 2, 2));

data->SetEntryProgress(design, 1.0f);
data->SetEntryProgress(build, 0.4f);
data->SetEntryImportance(freeze, 1.6f);
data->SetEntryMarker(freeze, TimelineMarkerStyle::TriangleDown);

auto chart = CreateTimelineChartWithData("plan", 10, 10, 900, 420,
                                         data, TimelineChartDesign::Modern);
chart->SetTitle("Release plan");
chart->SetNow(2026, 5, 15);
container->AddChild(chart);
```

The view fits the data automatically on the first render; call `FitToData()`
after changing the data if you want it re-fitted.

## Dates

Everything is a **day serial**: days since 1970-01-01, identical to
`GanttDate::serial`, so the two interoperate directly. The type is `double`, so
the fractional part is the time of day and sub-day scales work.

```cpp
double when = TimeAxis::Serial(2026, 6, 1);          // civil date
double stamp = TimeAxis::Serial(2026, 6, 1, 14, 30);  // with a time
data->AddMilestone("Kickoff", ganttTask.start.serial);
std::string label = TimeAxis::FormatDate(when);       // "1 Jun 2026"
```

## Data model

```cpp
struct TimelineChartEntry {
    TimelineEntryKind kind;   // Milestone | Span | Era | Bookend
    std::string name;
    std::string detail;       // Second label line
    std::string iconGlyph;    // Drawn inside the marker

    double start, end;        // Day serials
    bool openEnded;           // Span with no known end: fades out
    double startUncertainty, endUncertainty;   // Days, drawn as lighter ends

    int lane;                 // -1 = pack automatically
    int side;                 // -1 above the axis, +1 below, 0 = automatic
    Color color;              // Transparent = palette
    TimelineMarkerStyle marker;
    float importance;         // Scales the marker; higher wins label space
    float progress;           // 0..1 on a span; < 0 = no progress display
    std::string tooltip;
};
```

The four kinds:

- **Milestone** — a point in time, drawn as a marker on the axis with a callout
  label.
- **Span** — a date range, drawn as a bar in a packed lane.
- **Era** — a date range drawn as a translucent background band behind
  everything, with a label at the top (`Phase 1`, `FY26`).
- **Bookend** — a point event for project start/end; identical to a milestone
  but semantically the ends of the plan.

### Building data

```cpp
auto data = std::make_shared<TimelineChartDataSource>();
int a = data->AddMilestone("Go live", 2026, 9, 10);
int b = data->AddSpan("Pilot", 2026, 6, 1, 2026, 8, 31);
int c = data->AddEra("Phase 2", TimeAxis::Serial(2026, 7, 1), TimeAxis::Serial(2027, 1, 1));
int d = data->AddBookend("Project end", TimeAxis::Serial(2026, 12, 15));

data->SetEntryColor(b, Color(41, 128, 185, 255));
data->SetEntrySide(a, -1);          // above the axis
data->SetEntryMarker(a, TimelineMarkerStyle::Flag);
data->SetEntryProgress(b, 0.65f);
data->RemoveEntry(c);
chart->DataChanged();               // after editing in place
```

`LoadTasks()` builds a stakeholder timeline from a list of
`{name, start, end, milestone}` rows — the "same data, two audiences" workflow
where a Gantt plan is re-presented as a timeline.

## Lane packing

Spans and milestone callouts share **one shelf packer per side of the axis**, so
a label can never be drawn on top of a bar. Rows are assigned from horizontal
extents (including the label's own pixels), then each row's height comes from
its tallest occupant.

When a side runs out of vertical room, labels are dropped rather than
overprinted — and point events are placed **most-important first**
(`TimelineChartEntry::importance`), so it is the minor labels that go. The
marker itself is always drawn, so nothing silently disappears from the axis.
Set `lane` explicitly to pin an entry to a row.

## Lane modes: packed or swimlanes

```cpp
chart->SetLaneMode(TimelineLaneMode::Swimlanes);
chart->SetSwimlanes({TimelineSwimlane("Delivery Management", "Programme office", Color(224, 106, 96, 255)),
                     TimelineSwimlane("Operations",          "Platform team",    Color(240, 173, 78, 255)),
                     TimelineSwimlane("Risk Management",     "Compliance",       Color(91, 168, 214, 255))});
data->SetEntrySwimlane(id, "Operations");
```

`Packed` (default) is the stakeholder view: rows are anonymous free space and
entries pack into as few of them as fit. `Swimlanes` gives rows identity — one
named band per workstream, with a name column on the left and a tinted
background per row. Nothing else changes: same axis, same entries, same
markers, same interaction.

This is *not* a Gantt chart in disguise. A Gantt row is one **task**; a swimlane
row is one **category** holding many entries, sub-packed inside its band.

- **Declared or derived.** `SetSwimlanes()` fixes the rows and their order.
  Leave it empty and the rows are derived from the distinct
  `TimelineChartEntry::swimlaneName` values, in first-appearance order. An entry
  whose name matches no row falls into the first one.
- **Markers move into the band.** In swimlane mode a milestone is drawn inside
  its row at its date with the label beside it, not on the axis with a leader.
- **The axis moves to the top.** Rows have identity, so a centered axis is
  meaningless; `axisPosition` is overridden in this mode.
- **Rows share one height budget.** Each band sub-packs independently, then the
  bands negotiate: rows are *compressed* (down to half height) before any
  sub-row is taken away, because losing a sub-row is what forces two bars onto
  one line. A busy row can hold three sub-rows while quiet rows hold one.

```cpp
Rect2Dd band;
chart->GetSwimlaneRect(0, band);                  // band rectangle, element-local
const auto& rows = chart->GetResolvedSwimlanes(); // declared or derived
int index = chart->FindSwimlane("Operations");    // -1 when absent
```

Style knobs: `swimlaneHeaderWidth`, `swimlaneMinHeight`, `swimlanePadding`,
`swimlaneGap`, `showSwimlaneHeaders`, `showSwimlaneBands`,
`showSwimlaneSeparators`, `swimlaneBandAlpha`, `swimlaneHeaderAlpha`,
`swimlaneTitleFontSize`. A row's color comes from `TimelineSwimlane::color`, or
the palette when that is transparent.

## Axis, scale and view

```cpp
chart->SetAxisPosition(TimelineAxisPosition::Center);  // Top | Bottom | Center
chart->SetScale(TimelineScale::Auto);                  // see below
chart->SetDateRange(2026, 1, 1, 2026, 12, 31);
chart->FitToData();
chart->ZoomBy(0.7);        // < 1 zooms in, about the view center
chart->PanDays(-30.0);
chart->SetNow(2026, 8, 20);
```

`TimelineScale`: `Auto`, `Minutes`, `Hours`, `Days`, `Weeks`, `Months`,
`Quarters`, `Years`, `Decades`. `Auto` picks the tier from the current
pixels-per-day, so the header follows the zoom — decades across a century, hours
inside a day. The coarser tier above it is chosen automatically
(`TimeAxis::MajorFor`). Header slots too narrow to hold a label are skipped
rather than drawn as hatching.

`Center` puts the axis in the middle with entries above and below (the classic
development-timeline poster); `Top` hangs everything below the axis; `Bottom`
stacks everything above it.

## Designs and palettes

```cpp
chart->ApplyDesign(TimelineChartDesign::Roadmap);   // replaces the whole style
chart->SetDesign(TimelineChartDesign::Classic);     // geometry only
chart->SetPalette(TimelineChartPalette::Ocean);
chart->SetCustomPalette({Color(200, 30, 60, 255), Color(30, 90, 200, 255)});
chart->SetDarkTheme(true);
```

| `TimelineChartDesign` | Look |
|---|---|
| `Modern` | Rounded colorful bars, tinted header bands (default) |
| `Classic` | Flat bars, thin axis, print friendly, no era bands |
| `Minimal` | Thin line bars, hairline grid, small markers |
| `Roadmap` | Big circular markers, pill bars, era bands |
| `Dark` | Dark-background variant of Modern |

Palettes: `CorporateBlue` (default), `Vibrant`, `Pastel`, `Ocean`, `Sunset`,
`Forest`, `Slate`, `Mono`, `Custom`. A per-entry `color` always wins.

Marker styles: `Diamond`, `Circle`, `Square`, `TriangleUp`, `TriangleDown`,
`Flag`, `Pin`, `Star`. Bar styles: `Flat`, `Rounded`, `Pill`, `Line`.

## Style

Every knob lives in `TimelineChartStyle`: axis position/thickness, tier heights
and visibility, grid and weekend shading, lane height and gaps, bar geometry and
progress display, marker size and border, label placement and leader length, era
band alpha, per-role fonts and all colors.

```cpp
TimelineChartStyle s = TimelineChartStyles::CreateForDesign(TimelineChartDesign::Modern);
s.laneHeight = 36.0;
s.shadeWeekends = false;
chart->SetStyle(s);

chart->EditStyle().showDateCaptions = false;
chart->StyleChanged();
```

`TimelineLabelPlacement`: `AutoPlace` (inside the bar when it fits, callout
otherwise), `InsideBar`, `Callout`, `Hidden`.

## Interaction

Enabled by default (`SetEnableZoom` / `SetEnablePan` / `SetEnableSelection` /
`SetEnableTooltips` from the base class):

- **Wheel** zooms about the cursor's date — the date under the pointer stays put.
- **Drag** pans the range.
- **Click** selects an entry, clicking empty space clears it.
- **Double-click** on empty space fits the view back to the data.

```cpp
chart->onEntrySelect = [](size_t index, const TimelineChartEntry& e) { /* ... */ };
chart->onEntryHover = [](size_t index, const TimelineChartEntry& e) { /* ... */ };
chart->onEntryDoubleClick = [](size_t index, const TimelineChartEntry& e) { /* ... */ };
chart->onViewChanged = []() { /* zoom/pan happened */ };
```

Tooltips are built from the name, detail, dates, duration and progress unless
`TimelineChartEntry::tooltip` overrides them.

## Geometry queries

```cpp
Rect2Dd rect;
if (chart->GetEntryRect(0, rect)) { /* bar or marker box, element-local */ }
double x = chart->DateToPixel(TimeAxis::Serial(2026, 6, 1));
double when = chart->PixelToDate(x);
```

## Using TimeAxis on its own

`UltraCanvasTimeAxis.h` is header-only and has no dependencies beyond the
calendar helper, so any element needing a calendar axis can use it:

```cpp
TimeAxis axis;
axis.SetRange(TimeAxis::Serial(2026, 1, 1), TimeAxis::Serial(2027, 1, 1));
axis.SetPixelRange(plot.x, plot.x + plot.width);

const TimelineScale minor = axis.Resolve(TimelineScale::Auto);
for (const TimeAxisTick& tick : axis.Ticks(minor, false)) {
    // tick.x, tick.width, tick.label, tick.isWeekend
}
axis.ZoomAbout(mouseX, 0.8);   // keeps the date under the cursor fixed
```

## Sample data

```cpp
TimelineChartSamples::DevelopmentTimeline(2026);  // phases + milestones + bookends
TimelineChartSamples::CompanyHistory();           // five decades with era bands
TimelineChartSamples::SwimlaneProgram(2026);      // 21 entries across four workstreams
TimelineChartSamples::ProgramSwimlanes();         // the four rows it expects
```

## Not yet implemented

Tracked in [`UltraCanvasTimelineDiagramProposal.md`](UltraCanvasTimelineDiagramProposal.md):
time breaks that collapse empty stretches (B-A6), relative-time labelling
(B-A7), the draggable overview minimap
(B-I2), interactive rescheduling by dragging entries (B-I4), the crosshair
read-out (B-I5) and CSV/JSON import-export (B-D3). The Gantt chart itself is
also unchanged; its two small additions (G1, G2) remain open.
