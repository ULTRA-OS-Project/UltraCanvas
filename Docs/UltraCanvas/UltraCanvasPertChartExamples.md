# UltraCanvasPertChart Documentation

**Version:** 1.1.0
**Author:** UltraCanvas Framework

## Overview

`UltraCanvasPertChart` is an interactive PERT (Program Evaluation and Review Technique) chart component for project scheduling. It manages `PertActivity` nodes connected by `PertDependency` arrows, runs the Critical Path Method (CPM) automatically — earliest/latest start and finish, slack and the critical path per activity — and computes classic PERT statistics from optional three-point estimates (expected duration, variance, probability of on-time completion). Activities lay themselves out in dependency layers from left to right, can be color-coded by team via groups with an on-canvas legend, and are rendered in one of four node designs with one of eight built-in color palettes (or a fully custom palette).

**Namespace:** `UltraCanvas`
**Header:** `include/Plugins/Diagrams/UltraCanvasPertChart.h`
**Implementation:** `Plugins/Diagrams/UltraCanvasPertChart.cpp`
**Base Class:** `UltraCanvasUIElement`

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasPertChart
```

## Features

- **CPM analysis:** forward/backward pass with earliest start/finish, latest start/finish, slack, critical-path flags and cycle detection; recomputed automatically whenever the network changes.
- **PERT statistics:** per-activity three-point estimates `TE = (O + 4M + P) / 6`, variance `((P - O) / 6)^2`, project variance/σ along the critical path and `GetCompletionProbability(target)` via the normal approximation.
- **Node designs (`PertNodeDesign`):** `Card` (header + code/duration + dates rows), `DetailedCard` (adds a responsible row), `Compact` (solid rounded box with name and duration), `Circle` (numbered circles, activity-on-arrow look). The Circle design has two label modes (`PertCircleLabel`): `Code` puts the activity number inside with the name outside (AOA), `Name` centers the activity name inside the circle as a single-cell node, auto-shrinking the font to fit.
- **Color palettes (`PertChartPaletteKind`):** `Classic`, `Ocean`, `Vibrant`, `Pastel`, `Mint`, `Midnight`, `Dark`, `Monochrome` built-ins plus `SetCustomPalette()` for full control over every color slot.
- **Groups & legend:** color-code activities per team; explicit colors or auto-assignment from the palette's group color cycle; optional legend overlay.
- **Connectors:** `Straight`, `Orthogonal`, `Curved`; dashed *dummy dependencies* (zero-duration logical links); connector labels; critical-path connectors drawn thicker in the palette's accent color.
- **Auto layout:** layered left-to-right placement with barycenter ordering; dragged / manually placed activities keep their position across relayouts; `FitToView()` frames the whole network.
- **Milestones:** zero-duration start/end markers with dedicated palette colors.
- **Zoom & pan:** mouse-driven (wheel zoom about the cursor, drag empty space to pan), programmable.
- **Callbacks:** activity click, double-click, selection, drag, dependency click, schedule recomputation.

## Header Includes

```cpp
#include "Plugins/Diagrams/UltraCanvasPertChart.h"
```

## Class Reference

### Construction

```cpp
UltraCanvasPertChart(const std::string& id, int x, int y, int width, int height);

// Factory helpers
auto chart = CreatePertChart("pert", 0, 0, 1200, 700);
auto styled = CreatePertChart("pert2", 0, 0, 1200, 700,
                              PertNodeDesign::Compact,
                              PertChartPaletteKind::Midnight);
```

### Activity Management

```cpp
void AddActivity(const std::string& id, const std::string& name);
void AddActivity(const std::string& id, const std::string& name, double duration);
void AddActivity(const std::string& id, const std::string& name, double duration,
                 const std::string& code);
void AddMilestone(const std::string& id, const std::string& name);
void RemoveActivity(const std::string& id);
void Clear();

void SetActivityDuration(const std::string& id, double duration);
void SetActivityThreePointEstimate(const std::string& id,
                                   double optimistic, double mostLikely,
                                   double pessimistic);
void ClearActivityThreePointEstimate(const std::string& id);
void SetActivityDates(const std::string& id, const std::string& startDate,
                      const std::string& endDate);      // display-only strings
void SetActivityResponsible(const std::string& id, const std::string& responsible);
void SetActivityCode(const std::string& id, const std::string& code);
void SetActivityGroup(const std::string& id, const std::string& groupId);
void SetActivityColors(const std::string& id, const Color& header,
                       const Color& fill, const Color& border, const Color& text);
void ClearActivityColors(const std::string& id);
void SetActivityPosition(const std::string& id, double x, double y); // pins the node
void SetActivitySize(const std::string& id, double width, double height);

PertActivity* GetActivity(const std::string& id);
std::vector<std::string> GetAllActivityIds() const;
```

`SetActivityPosition()` (and dragging a node) marks the activity as manually
placed, so subsequent `AutoLayout(false)` calls leave it where it is.
`AutoLayout(true)` resets all manual positions.

### Dependency Management

```cpp
void AddDependency(const std::string& id, const std::string& fromId,
                   const std::string& toId);
void AddDummyDependency(const std::string& id, const std::string& fromId,
                        const std::string& toId);   // dashed, zero-duration
void RemoveDependency(const std::string& id);
void SetDependencyLabel(const std::string& id, const std::string& label);
void SetDependencyColor(const std::string& id, const Color& color);
void ClearDependencyColor(const std::string& id);

PertDependency* GetDependency(const std::string& id);
std::vector<std::string> GetAllDependencyIds() const;
```

### Groups (team color-coding)

```cpp
void DefineGroup(const std::string& groupId, const std::string& name);
void DefineGroup(const std::string& groupId, const std::string& name, const Color& color);
void RemoveGroup(const std::string& groupId);
Color GetGroupColor(const std::string& groupId) const;
void SetLegendVisible(bool visible);
```

Groups without an explicit color receive one from `palette.groupColors`
(cycled by definition order), so switching palettes recolors the teams
consistently.

### Schedule Analysis

```cpp
bool ComputeSchedule();          // false when the graph has a cycle
bool HasCycle() const;

double GetProjectDuration() const;
std::vector<std::string> GetCriticalPath() const;  // ids in topological order
bool IsOnCriticalPath(const std::string& id) const;
double GetSlack(const std::string& id) const;

double GetExpectedDuration(const std::string& id) const; // TE or plain duration
double GetActivityVariance(const std::string& id) const;
double GetProjectVariance() const;
double GetProjectStandardDeviation() const;
double GetCompletionProbability(double targetDuration) const; // 0..1
```

The schedule is recomputed lazily before rendering whenever activities or
dependencies changed; `ComputeSchedule()` may also be called eagerly (e.g. to
read statistics before the first frame). `onScheduleComputed` fires after
every successful pass with the project duration.

### Design Options

```cpp
void SetNodeDesign(PertNodeDesign design);   // Card, DetailedCard, Compact, Circle
void SetCircleLabelMode(PertCircleLabel m);  // Circle only: Code (AOA) or Name inside
void SetConnectorStyle(PertConnectorStyle s); // Straight, Orthogonal, Curved
void SetCriticalPathHighlight(bool enable);
void SetShowDates(bool show);
void SetShowActivityCodes(bool show);
void SetShowDurations(bool show);
void SetShowSlack(bool show);                // "slack: N" under non-critical nodes
void SetDurationUnit(const std::string& unitSuffix); // default "days"
void SetGridVisible(bool visible, double spacing = 20.0);
void SetFontFamily(const std::string& fontFamily);
PertChartStyle& GetStyle();                  // spacing, fonts, widths, radii
```

### Palette Options

```cpp
void SetPalette(PertChartPaletteKind kind);          // one of the 8 built-ins
void SetCustomPalette(const PertChartPalette& p);    // any color scheme
const PertChartPalette& GetPalette() const;
static PertChartPalette PertChartPalette::BuiltIn(PertChartPaletteKind kind);
```

`PertChartPalette` exposes every color the renderer uses: background, grid,
node header/fill/border/text, milestone colors, connector and
critical-connector colors, dummy-connector color, selection, legend text and
the group color cycle. Start from a built-in and tweak:

```cpp
PertChartPalette p = PertChartPalette::BuiltIn(PertChartPaletteKind::Ocean);
p.criticalConnectorColor = Color(200, 30, 30, 255);
p.groupColors = { Color(90, 170, 235, 255), Color(240, 150, 70, 255) };
chart->SetCustomPalette(p);
```

### Layout & View

```cpp
void AutoLayout(bool resetManualPositions = false);
void FitToView();
void SetZoomLevel(double zoom);   // clamped to [0.1, 10]
void SetPanOffset(double x, double y);
```

### Interaction & Callbacks

```cpp
void SelectActivity(const std::string& id);
void DeselectAll();
std::string GetSelectedActivityId() const;

std::function<void(const std::string&)> onActivityClick;
std::function<void(const std::string&)> onActivityDoubleClick;
std::function<void(const std::string&)> onActivitySelected;
std::function<void(const std::string&, double, double)> onActivityDragged;
std::function<void(const std::string&)> onDependencyClick;
std::function<void(double)> onScheduleComputed; // project duration
```

Mouse: click selects, drag moves an activity (pinning it), dragging empty
space pans, the wheel zooms about the cursor, double-click fires
`onActivityDoubleClick`.

## Usage Examples

### Minimal network with critical path

```cpp
auto chart = CreatePertChart("pert", 30, 90, 1200, 640);

chart->AddMilestone("start", "Start");
chart->AddActivity("design", "Develop the design", 2, "001");
chart->AddActivity("wire", "Make the wireframe", 1, "002");
chart->AddActivity("home", "Develop the homepage", 3, "003");
chart->AddActivity("about", "Develop the About page", 1, "004");
chart->AddActivity("test", "Test the website", 1, "005");
chart->AddMilestone("end", "Launch");

chart->AddDependency("d1", "start", "design");
chart->AddDependency("d2", "design", "wire");
chart->AddDependency("d3", "wire", "home");
chart->AddDependency("d4", "wire", "about");
chart->AddDependency("d5", "home", "test");
chart->AddDependency("d6", "about", "test");
chart->AddDependency("d7", "test", "end");

chart->ComputeSchedule();
// chart->GetProjectDuration() == 8, critical path start>design>wire>home>test>end
```

### Teams, three-point estimates and a probability read-out

```cpp
chart->DefineGroup("design", "Design Team");
chart->DefineGroup("prog", "Programming Team", Color(30, 90, 200, 255));
chart->SetActivityGroup("home", "prog");

chart->SetActivityThreePointEstimate("home", 2, 3, 6); // TE = 3.33
chart->ComputeSchedule();
double p = chart->GetCompletionProbability(chart->GetProjectDuration() + 1.0);

chart->onScheduleComputed = [](double duration) {
    printf("Project duration: %.1f days\n", duration);
};
```

### Switching designs and palettes at runtime

```cpp
chart->SetNodeDesign(PertNodeDesign::Compact);
chart->SetPalette(PertChartPaletteKind::Midnight);   // dark navy boxes

chart->SetNodeDesign(PertNodeDesign::Circle);        // AOA-style circles
chart->SetPalette(PertChartPaletteKind::Mint);
chart->SetCircleLabelMode(PertCircleLabel::Name);    // name inside the circle
chart->SetCircleLabelMode(PertCircleLabel::Code);    // back to number inside

chart->SetNodeDesign(PertNodeDesign::DetailedCard);  // + responsible row
chart->SetPalette(PertChartPaletteKind::Dark);       // dark canvas
```

## Demo

`Apps/DemoApp/UltraCanvasPertChartExamples.cpp` builds a software-delivery
network (plan → parallel design tracks → programming → merge → test → UAT)
with team color-coding, a dummy dependency and three-point estimates, and a
control bar that switches node design, palette and connector routing live,
toggles critical-path highlighting, legend, dates and slack, and shows the
computed project duration, critical path and on-time probability.

## Notes & Limitations

- Dates are display-only strings; the component does not parse or validate
  them. Compute calendar dates externally if needed and pass them in.
- `GetCriticalPath()` returns all zero-slack activities in topological order;
  with parallel critical branches this is the union of those branches.
- A cyclic dependency graph disables scheduling (`ComputeSchedule()` returns
  `false`); the chart still renders using the last valid layout.
