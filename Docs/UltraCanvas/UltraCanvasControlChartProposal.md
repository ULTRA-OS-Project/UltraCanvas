# Control Charts on the Line Chart — Research & Feature Proposal

**Status:** **Phases 1 and 2 are implemented** - see
[`UltraCanvasControlLimits.md`](UltraCanvasControlLimits.md) for the
statistics and
[`UltraCanvasEngineCharts.md`](UltraCanvasEngineCharts.md#control-charts) for
the line chart features and `ShowControlChart`. Phase 1 delivered E1-E4 and
E6 on `UltraCanvasEngineLineChart`, the helper with K1, K2 and R1, P1, the
demo card at the end of **Charts > Line Chart** (P3, an X-bar-R and an I-MR
process, each with a late process change to switch on),
`Tests/ControlLimitsTest.cpp` and `Tests/EngineLineChartTest.cpp`. The rest
of §5 is still a sketch, and `check_doc_examples.py` records its findings in
`scripts/doc_examples_baseline.txt` until each part lands.

How Phase 1 departs from the plan below, and why:

* **The helper has only what Phase 1 computes.** `ControlChartKind` lists
  X-bar-R and I-MR; `ControlSubgroup` has no count, size, exclusion or note;
  `ControlOptions` has the four Western Electric switches and the run length.
  A field nothing reads would only be a promise in the API.
* **`FindRuleViolations` is public.** It runs the rules on one pane's series
  against whatever limits it carries, which is what limits frozen from a
  baseline (L5) need, and it lets the test check every rule on standard-unit
  data.
* **d2 and d3 are kept to four decimals.** From the three-decimal tables the
  derived factors missed their own published values (D4(2) came out 3.2686
  against 3.267).
* **A pane can name values its axis must reach** (`EngineLinePane::rangeIncludes`):
  a limit outside its pane's data range was otherwise drawn across the
  neighbouring pane. The engine now leaves a limiter outside its axis's band
  undrawn, and anchors a limiter's caption half its width in from the line's
  end, so the caption sits beside the line rather than across it.
* **The limit values stand in a column past the plot's right edge**, the
  usual SPC layout, through a new `ChartLimiter::captionPlacement`
  (`BeyondEnd`): placed by the label solver inside the plot, data lines kept
  crossing them or pushing them onto the axis. §7.6's in-plot stacking is not
  needed.
* **The engine gained `SetExtraGridAxes`** (a grid in every pane) and three
  read accessors, `GetLimiters`, `GetHighlights` and `HitRegionAt`, for
  tests and for hosts with their own read-out.
* **A clamped lower limit draws nothing**, not a caption reading "LCL = 0
  (none)": with no line there is nothing for a caption to sit beside.

Phase 2 (2026-11-06) delivered E5 (`AddSeriesBand`) and E7 (`onPointClick`),
K3-K5 (X-bar-S with unequal subgroups, p, np, c and u), R2 (the Nelson rules,
the run length as a setting, `rulesOnDispersion`), S1 (stages), S2 (excluded
points, `baselineCount`), C1 (spec limits, Cp, Cpk, Pp, Ppk), P2 (stepped
limits, stage dividers, hollow excluded points, notes, spec lines) and P4 (the
demo's p and c processes, a rule-set choice, the change as a stage, spec
limits with capability, and a click that excludes a point). How it departs:

* **No fixed centre and sigma in the options.** `baselineCount` freezes the
  limits from a stage's first points, and `FindRuleViolations` judges data
  against limits from anywhere; a third way would only be a promise.
* **`drawLine = false` on a series** makes an invisible band edge, so zones
  under stepped limits are bands between hidden series rather than a new
  kind of highlight.
* **Nelson rules 3 and 4 read the z-scores**, not the raw values: with limits
  that change from lot to lot, a run of rising proportions is not a trend if
  the lots shrink.
* **Capability is that of the last stage** (the process as it is now), from
  its values with the excluded points left out; specification limits on an
  attribute chart are refused with a warning.
* **`ControlC4` works for any n >= 2** (its closed form has no table limit),
  and `ControlA3`, `ControlSigmaLowerFactor` and `ControlSigmaUpperFactor`
  give A3, B3 and B4.
* **Stepped limits caption the last point's values** in the caption column,
  through zero-length limiters; the engine spreads that column so a control
  limit and a specification limit never overprint, and an `AboveEnd` caption
  on a vertical line (a stage divider) sits beside it, inside the plot.
* **The data line is not broken at a stage boundary**; the divider and the
  step in the limits mark it.

A control chart (Shewhart chart, statistical process control chart) plots a
measurement of a process in time order against a centre line and two control
limits computed from the data itself. It answers one question: **is the
variation in this process the usual kind, or has something changed?** A point
outside the limits, or a pattern of points inside them (a run, a trend, a
cluster near a limit), is a signal worth investigating; everything else is
noise that should be left alone. It is one of the seven basic quality tools,
next to the fishbone diagram (`UltraCanvasFishboneDiagram`) that is used to
find the cause once a control chart has flagged a signal.

**The approach: no new chart type.** A control chart is a line chart with
extra lines, a few marked points and sometimes a second pane. This proposal
adds those abilities to `UltraCanvasEngineLineChart` as general features,
each useful well beyond quality control, puts the statistics in a UI-free
helper next to `MovingAverage`, and shows control charts as a new example on
the **Charts → Line Chart** demo page.

---

## 1. Summary — the headline question

**Can the line chart draw a control chart today?** Partly. The engine line
chart and its base class already provide:

| Already there | Use in a control chart |
|---|---|
| `EngineLineSeries` in a categorical domain, markers, tooltips, legend | The plotted statistic, one point per subgroup |
| `ChartLimiter` (`AddLimiter`) with solved captions and an extent | Constant centre line and limits, "UCL = 10.032" at the edge; one limiter per stage via the extent |
| A limiter on the domain axis | The divider where a new stage starts |
| `ChartHighlightShape::ValueBand` (`AddHighlight`) | Constant zone shading (A, B, C) |
| `ChartLineShape::StepMiddle` | The shape of a limit that changes per point |

What is missing is small and general:

| Missing | General use | Control chart use |
|---|---|---|
| **E1** a line shape per series | A stepped tariff next to a smooth price | Stepped p and u limits next to a straight data line |
| **E2** per-series switches: markers, legend entry, tooltips | Reference and forecast lines that are not data | Limit lines that are not data points |
| **E3** per-point marker styles | Outliers, the latest value, a selected point | Points that break a rule (red diamond), excluded points (hollow) |
| **E4** per-point notes in the tooltip | Annotated events | The rule broken, the assignable cause |
| **E5** a filled band between two series | Confidence intervals, min/max envelopes | Zones and limits that change per point |
| **E6** stacked panes, each with its own value axis | An indicator under a price, two units in one chart | X̄ over R, I over MR |
| **E7** `onPointClick` | Drill-down from any point | Exclude / include a point, add a note |
| **E8** live append with a scrolling window | Monitors and dashboards | Monitoring a process against frozen limits |

The statistics go into **`UltraCanvasControlLimits.h`**, a UI-free helper
beside `UltraCanvasChartAnalysis.h`: the chart kinds (X̄–R, X̄–S, I–MR, p, np,
c, u, EWMA, CUSUM), the constants, the limits per point, the run rules,
stages, exclusions and capability. A small free function,
`ShowControlChart`, turns that result into series, limiters, bands and
point styles on a line chart, so an application needs two calls rather than
copying the demo.

Three phases (§10). Phase 1 shows a correct X̄–R and I–MR chart on the Line
Chart page.

---

## 2. What a control chart is

A process produces items one after another. Each item, or each small group of
items taken together (a **subgroup**, say five parts measured every hour), gives
a value. Plotted in time order, the values wander. Walter Shewhart's point was
that some wandering is built into the process (**common cause** variation) and
some comes from something that happened (**special** or **assignable cause**:
a worn tool, a new batch of material, a different operator). Reacting to
common-cause variation makes a process worse; missing a special cause lets a
defect run on.

The chart draws a **centre line** at the process mean and **control limits**
three standard deviations either side. For a stable process almost every point
falls inside (about 99.73% for normally distributed values), so a point outside
is unlikely to be noise. The band between the limits is usually split into
three **zones** each side: C (within 1σ), B (1σ to 2σ) and A (2σ to 3σ). The
run rules (§2.3) look at how points fall in those zones.

Control limits are **not** specification limits. Control limits describe what
the process does; specification limits (USL, LSL) describe what the customer
accepts. A process can be in control and still make parts out of
specification, which is what the capability indices measure.

### 2.1 The chart kinds

| Kind | Plots | Data | Typical use |
|---|---|---|---|
| **X̄–R** | Subgroup means (top pane) and ranges (bottom pane) | Measurements in subgroups of 2–10 | Machined dimensions sampled five at a time |
| **X̄–S** | Subgroup means and standard deviations | Subgroups larger than 10, or of varying size | Automated gauging of many parts per sample |
| **I–MR** | Individual values and the moving range of consecutive values | One value per time step | Daily yield, a lab assay, a monthly KPI |
| **p** | Proportion defective per lot | Defective count and lot size (sizes may vary) | Share of failed inspections per day |
| **np** | Number defective per lot | Defective count, constant lot size | Rejected units in batches of 200 |
| **c** | Defects per inspection unit | Defect count, constant opportunity | Paint blemishes per car body |
| **u** | Defects per unit | Defect count and number of units (may vary) | Bugs per thousand lines per release |
| **EWMA** | Exponentially weighted moving average | Individuals or subgroup means | Small sustained drifts the Shewhart chart misses |
| **CUSUM** | Upper and lower cumulative sums of deviations from target | Individuals or subgroup means | Detecting a shift of about one sigma quickly |

### 2.2 Where the limits come from

For the variables charts, sigma is estimated **within subgroups** (§7.1),
through the bias-correction constants d₂, d₃ and c₄ of the subgroup size n:

| Chart | Centre line | Limits |
|---|---|---|
| X̄ (with R) | X̿, the mean of subgroup means | X̿ ± A₂·R̄, where A₂ = 3 / (d₂·√n) |
| R | R̄ | D₃·R̄ and D₄·R̄, where D₃ = max(0, 1 − 3d₃/d₂), D₄ = 1 + 3d₃/d₂ |
| X̄ (with S) | X̿ | X̿ ± A₃·S̄, where A₃ = 3 / (c₄·√n) |
| S | S̄ | B₃·S̄ and B₄·S̄, where B₃ = max(0, 1 − 3√(1 − c₄²)/c₄), B₄ = 1 + 3√(1 − c₄²)/c₄ |
| I | X̄ | X̄ ± 3·MR̄ / d₂(2) = X̄ ± 2.66·MR̄ |
| MR | MR̄ | 0 and 3.267·MR̄ |
| p | p̄ = Σ defective / Σ inspected | p̄ ± 3·√(p̄(1 − p̄) / nᵢ), per point |
| np | n·p̄ | n·p̄ ± 3·√(n·p̄(1 − p̄)) |
| c | c̄ | c̄ ± 3·√c̄ |
| u | ū = Σ defects / Σ units | ū ± 3·√(ū / nᵢ), per point |

c₄ has a closed form, c₄(n) = √(2/(n − 1)) · Γ(n/2) / Γ((n − 1)/2); d₂ and d₃
do not (they are integrals over the range of a normal sample) and are
tabulated for n = 2..25. The other constants follow from those three, so the
helper computes A₂, A₃, B₃, B₄, D₃ and D₄ instead of storing a second table,
and the test checks the results against the published values:

| n | A₂ | D₃ | D₄ | d₂ | A₃ | B₃ | B₄ | c₄ |
|---|---|---|---|---|---|---|---|---|
| 2 | 1.880 | 0 | 3.267 | 1.128 | 2.659 | 0 | 3.267 | 0.7979 |
| 3 | 1.023 | 0 | 2.574 | 1.693 | 1.954 | 0 | 2.568 | 0.8862 |
| 4 | 0.729 | 0 | 2.282 | 2.059 | 1.628 | 0 | 2.266 | 0.9213 |
| 5 | 0.577 | 0 | 2.114 | 2.326 | 1.427 | 0 | 2.089 | 0.9400 |
| 6 | 0.483 | 0 | 2.004 | 2.534 | 1.287 | 0.030 | 1.970 | 0.9515 |
| 7 | 0.419 | 0.076 | 1.924 | 2.704 | 1.182 | 0.118 | 1.882 | 0.9594 |
| 8 | 0.373 | 0.136 | 1.864 | 2.847 | 1.099 | 0.185 | 1.815 | 0.9650 |
| 9 | 0.337 | 0.184 | 1.816 | 2.970 | 1.032 | 0.239 | 1.761 | 0.9693 |
| 10 | 0.308 | 0.223 | 1.777 | 3.078 | 0.975 | 0.284 | 1.716 | 0.9727 |

EWMA: zᵢ = λxᵢ + (1 − λ)zᵢ₋₁ with z₀ = the centre line; limits
μ ± Lσ·√(λ/(2 − λ) · (1 − (1 − λ)²ⁱ)), which widen over the first points and
settle. Typical λ = 0.2, L = 3. (The engine's `ExponentialMovingAverage` uses
smoothing 2/(window + 1) and a different seed, so the helper computes its own.)

CUSUM (tabular): Cᵢ⁺ = max(0, xᵢ − (μ + K) + Cᵢ₋₁⁺), Cᵢ⁻ = max(0, (μ − K) −
xᵢ + Cᵢ₋₁⁻), with K = kσ (k = 0.5) and a decision interval H = hσ (h = 4 or 5).
It is drawn as two series against ±H.

### 2.3 Run rules

A point beyond the limits is the first signal; the run rules add patterns
inside them that are just as unlikely for a stable process.

**Western Electric rules** (the default set):

1. One point beyond 3σ (outside the limits).
2. Two of three consecutive points beyond 2σ on the same side (in zone A or beyond).
3. Four of five consecutive points beyond 1σ on the same side (in zone B or beyond).
4. Eight consecutive points on the same side of the centre line.

**Nelson rules** (a superset, as an alternative):

1. One point beyond 3σ.
2. Nine points in a row on the same side of the centre line.
3. Six points in a row steadily increasing or decreasing.
4. Fourteen points in a row alternating up and down.
5. Two of three points beyond 2σ on the same side.
6. Four of five points beyond 1σ on the same side.
7. Fifteen points in a row within 1σ, either side (stratification: the limits
   are too wide, often because subgroups mix two streams).
8. Eight points in a row with none within 1σ, either side (mixture).

Each rule can be switched on or off, and the run lengths (8 or 9 for the
"same side" rule) are settings, since sources and industries differ.

---

## 3. Reference shapes

The X̄–R chart this proposal targets, with a shift at subgroup 18:

```
 X̄  ─────────────────────────────────────────────────────  UCL 10.032
    ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ zone A
          ●                 ●   ●  ●  ◆  ●   ◆
    ─ ─ ●─ ─ ●─ ─ ─ ●─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─  CL 10.004
      ●    ●   ●  ●   ●  ●
    ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
    ─────────────────────────────────────────────────────  LCL 9.976
 R  ─────────────────────────────────────────────────────  UCL 0.104
      ●  ●   ●    ●   ●   ●   ●  ●   ●   ●  ●   ●  ●
    ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─  CL 0.049
    ─────────────────────────────────────────────────────  LCL 0
      1    5     10     15    20     25    subgroup
```

`◆` marks a point that completes a rule (here rule 4, eight on one side, then
rule 2). The p chart's limits step with each lot's size:

```
      ┌─┐   ┌──┐ ┌┐        UCL (per lot)
    ──┘ └───┘  └─┘└──────
      ●   ●  ●   ●  ●      p
    ─ ─ ─ ─ ─ ─ ─ ─ ─ ─    p̄
    ──┐ ┌───┐  ┌─┐┌──────
      └─┘   └──┘ └┘        LCL (per lot, clamped at 0)
```

Mapped onto the line chart, every element of these pictures is one of:

| Element | Line chart feature |
|---|---|
| The data line and its markers | An `EngineLineSeries` (in pane 0 or 1) |
| A constant centre line or limit with its caption | `ChartLimiter` on that pane's value axis |
| A stepped limit | A series with `shape = StepMiddle`, no markers, no legend entry, no tooltips (E1, E2) |
| Constant zones | `ValueBand` highlights on that pane's value axis |
| Zones under stepped limits | Bands between two hidden series (E5) |
| A flagged or excluded point | A point style (E3) and a tooltip note (E4) |
| A stage divider | A `ChartLimiter` on the domain axis, caption = the stage name |
| X̄ over R | Two panes (E6) |

---

## 4. Where this belongs in the repository

### 4.1 Why the line chart, and not a new chart type

A control chart's drawing is a line chart's drawing: a time-ordered line with
markers, reference lines and shaded bands. Everything particular to SPC is
arithmetic (which limits, which points break which rule), and arithmetic
needs no element. A new chart type would duplicate the line chart's series,
markers, tooltips, legend and domain handling to add what amounts to a
handful of extra drawing options. Putting those options on the line chart
means:

* every line chart user gets per-point styles, bands, panes, notes and point
  clicks, which are common requests on their own;
* the control chart inherits the line chart's themes, label plan, polar mode
  and future improvements for free;
* the statistics stay testable without a window, and usable by an application
  that only wants to raise an alarm.

The trade-off is that the control chart is assembled from parts rather than
configured as one object. `ShowControlChart` (§5.3) closes that gap.

### 4.2 Two panes through axis sub-bands

The X̄–R, X̄–S and I–MR charts are read as a pair: the location chart on top,
the dispersion chart below, sharing the subgroup axis. The engine financial
chart already stacks price over volume as two value axes in disjoint
sub-bands of one plot (`ChartAxis::bandStart` / `bandEnd`), so both grids,
both edge axes and the shared domain come from the engine's layers. E6 gives
the line chart the same: each pane is one value axis in its own sub-band, and
each series names its pane. Limiters and highlights already name an axis
index, so they land in the right pane through `ValueAxisOfPane(p)`.

### 4.3 What "done" touches

1. `UltraCanvas/include/Plugins/Charts/Engine/Charts/UltraCanvasEngineLineChart.h`
   and its `.cpp`: E1–E8, each switched off by default so existing charts draw
   exactly as before.
2. `UltraCanvas/include/Plugins/Charts/Engine/UltraCanvasControlLimits.h` and
   `UltraCanvas/Plugins/Charts/Engine/UltraCanvasControlLimits.cpp` (UI-free),
   added to `UltraCanvas/CMakeLists.txt`; `ShowControlChart` in the line
   chart's files.
3. `Tests/ControlLimitsTest.cpp` (UI-free) and `Tests/EngineLineChartTest.cpp`
   (the new line chart features; there is no engine line chart test yet), both
   in `Tests/CMakeLists.txt`.
4. The demo: a control chart card on the **Line Chart** page
   (`Apps/DemoApp/UltraCanvasBasicChartsExamples.cpp`, or a new
   `UltraCanvasControlChartExamples.cpp` called from it if the card grows
   large), registered as a variant of `linecharts`.
5. Docs: a "Control charts" section and the new options in the engine line
   chart's part of `UltraCanvasEngineCharts.md`, a page for the helper
   (`UltraCanvasControlLimits.md`), and a `changelog.d/` entry. Every string a
   person reads goes through `Tr`; run `extract_ui_strings.py`,
   `check_locale_catalogues.py`, `generate_doc_toc.py`, `check_doc_examples.py`
   on the changed docs, and remove this proposal's baseline entries with
   `--update-baseline` as each API lands.

---

## 5. Data model sketch

Everything in this section is a sketch of an API that does not exist yet.

### 5.1 Line chart additions

```cpp
// E3: one point's look; alpha 0 / NaN / unset fields keep the series' own.
struct EngineLinePointStyle {
    Color color = Color(0, 0, 0, 0);
    std::optional<ChartMarkerShape> marker;
    bool hollow = false;                     // outline only: an excluded point
    float radiusScale = 1.0f;
    bool alwaysShown = false;                // drawn even when markers are off
};

struct EngineLineSeries {
    // ... the existing fields: name, values, color, marker, lineWidth, dashed ...
    std::optional<ChartLineShape> shape;     // E1: unset = the chart's shape
    std::optional<bool> showMarkers;         // E2: unset = the chart's setting
    bool inLegend = true;                    // E2
    bool interactive = true;                 // E2: hit regions and tooltips
    size_t pane = 0;                         // E6
    std::vector<EngineLinePointStyle> pointStyles;   // E3: empty = none; by index
    std::vector<std::string> pointNotes;     // E4: extra tooltip lines; by index
};

// E5: fill between two series, point by point, in the lower series' pane.
struct EngineLineBand {
    size_t lowerSeries = 0;
    size_t upperSeries = 0;
    Color color = Color(0, 0, 0, 0);         // alpha 0 = the upper series' colour
    float opacity = 0.15f;
    std::optional<ChartLineShape> shape;     // unset = the upper series' shape
};

// E6: a pane is one value axis in a sub-band of the plot.
struct EngineLinePane {
    double weight = 1.0;                     // share of the plot height
    std::string valueTitle;
    bool includeZero = false;
};

class UltraCanvasEngineLineChart : public UltraCanvasChartEngineElement {
public:
    // ... the existing API ...
    size_t AddSeriesBand(const EngineLineBand& band);              // E5
    void ClearSeriesBands();
    void SetPanes(const std::vector<EngineLinePane>& panes);       // E6; empty = one pane
    double PaneGap() const;
    void SetPaneGap(double pixels);
    size_t ValueAxisOfPane(size_t pane) const;                     // for limiters, highlights

    std::function<void(size_t series, size_t index)> onPointClick; // E7

    // E8: append one domain position to every series (NaN = a gap) and keep
    // the last `window` positions on screen (0 = all).
    void AppendPoint(const std::string& category, const std::vector<double>& values);
    void SetVisibleWindow(size_t window);
};
```

### 5.2 The statistics helper

```cpp
enum class ControlChartKind {
    XbarR, XbarS, IndividualsMR,      // variables
    P, NP, C, U,                      // attributes
    EWMA, CUSUM                       // time-weighted
};

enum class ControlRuleSet { WesternElectric, Nelson };

// One time step: a subgroup of measurements (variables charts), or a count
// and the size it was counted over (attributes charts).
struct ControlSubgroup {
    std::string label;                // "08:00", "Lot 1142"; empty = its number
    std::vector<double> values;       // measurements; one for I-MR
    double count = 0.0;               // defectives (p, np) or defects (c, u)
    double size = 0.0;                // lot size (p, np) or units (u)
    bool excluded = false;            // left out of the limit calculation
    std::string note;                 // an assignable cause
};

// The statistic one pane plots, with its limits per point.
struct ControlSeries {
    std::string name;                 // "X-bar", "Range", "p"
    std::vector<double> value;        // NaN = nothing plotted (MR's first point)
    std::vector<double> centre;
    std::vector<double> upper;
    std::vector<double> lower;        // clamped at 0 where the statistic cannot go below
    std::vector<double> sigma;        // the point's own sigma, for zones and rules
    bool constantLimits = true;       // false for p, u, X-bar-S with varying n, EWMA
};

struct ControlViolation {
    size_t point = 0;                 // the point that completes the pattern
    size_t firstPoint = 0;            // where the pattern starts
    int rule = 0;                     // 1-based number within the rule set
    size_t pane = 0;
};

struct ControlStage {
    size_t firstPoint = 0;
    std::string name;
};

struct ControlOptions {
    ControlChartKind kind = ControlChartKind::XbarR;
    std::vector<ControlStage> stages;             // empty = one stage
    ControlRuleSet ruleSet = ControlRuleSet::WesternElectric;
    std::vector<bool> rulesEnabled;               // per rule; empty = all
    int sameSideRun = 8;
    bool rulesOnDispersion = false;               // rule 1 only on R / S / MR when false
    size_t baselineCount = 0;                     // limits from the first N points; 0 = all
    double fixedCentre = std::nan(""), fixedSigma = std::nan("");
    double ewmaLambda = 0.2, ewmaL = 3.0;
    double cusumK = 0.5, cusumH = 5.0;
    double lowerSpec = std::nan(""), upperSpec = std::nan("");
};

struct ControlCapability {
    double mean = 0.0, sigmaWithin = 0.0, sigmaOverall = 0.0;
    double cp = 0.0, cpk = 0.0, pp = 0.0, ppk = 0.0;   // NaN when a spec side is missing
};

struct ControlResult {
    std::vector<std::string> labels;
    ControlSeries location;                       // X-bar, I, p, np, c, u, EWMA, CUSUM+
    std::optional<ControlSeries> dispersion;      // R, S, MR, CUSUM-
    std::vector<ControlViolation> violations;
    ControlCapability capability;
    std::vector<std::string> warnings;            // unequal n on X-bar-R, fewer than 20 subgroups
};

ControlResult ComputeControlChart(const std::vector<ControlSubgroup>& subgroups,
                                  const ControlOptions& options);
std::string ControlRuleDescription(ControlRuleSet set, int rule);

double ControlD2(int n);   // n = 2..25
double ControlD3(int n);
double ControlC4(int n);
```

### 5.3 Putting it on a line chart

```cpp
struct ControlChartStyle {
    bool showZones = false;                       // A / B / C shading
    bool showSigmaLines = false;                  // dotted 1 and 2 sigma
    bool showDispersion = true;                   // second pane when there is one
    double dispersionWeight = 0.55;               // pane height relative to the first
    Color flagColor = Color(200, 40, 40, 255);
};

// Replaces the chart's series, panes, limiters, highlights and bands with the
// result: the statistic per pane, constant limits as limiters, varying limits
// as stepped series, zones, stage dividers, flagged and excluded points with
// their notes in the tooltip.
void ShowControlChart(UltraCanvasEngineLineChart& chart, const ControlResult& result,
                      const ControlChartStyle& style = {});
```

An application's code is then:

```cpp
auto chart = CreateEngineLineChart("Spc", 0, 0, 720, 480);
ControlOptions options;
options.kind = ControlChartKind::XbarR;
ControlResult result = ComputeControlChart(subgroups, options);
ShowControlChart(*chart, result);
chart->onPointClick = [&](size_t, size_t index) {
    subgroups[index].excluded = !subgroups[index].excluded;
    ShowControlChart(*chart, ComputeControlChart(subgroups, options));
};
```

---

## 6. Feature list

### Line chart (E)

| ID | Feature | Phase |
|---|---|---|
| E1 | Line shape per series | 1 |
| E2 | Per-series markers, legend entry and tooltips switches | 1 |
| E3 | Per-point marker styles (colour, shape, hollow, size, always shown) | 1 |
| E4 | Per-point notes appended to the tooltip | 1 |
| E5 | A filled band between two series | 2 |
| E6 | Stacked panes with their own value axes; `ValueAxisOfPane` | 1 |
| E7 | `onPointClick` | 2 |
| E8 | `AppendPoint` and a scrolling window | 3 |

### Statistics (K, R, S, C)

| ID | Feature | Phase |
|---|---|---|
| K1 | X̄–R with subgroups of 2..25; constants from d₂, d₃ (tables) and c₄ (closed form) | 1 |
| K2 | I–MR | 1 |
| K3 | X̄–S, including subgroups of varying size | 2 |
| K4 | p and u with per-point limits | 2 |
| K5 | np and c | 2 |
| K6 | EWMA and tabular CUSUM | 3 |
| R1 | Western Electric rules 1–4, each switchable, with descriptions for the tooltip | 1 |
| R2 | Nelson rules 1–8; run lengths as settings; rules on the dispersion pane | 2 |
| S1 | Stages: limits per stage, no rule runs across a stage boundary | 2 |
| S2 | Excluded points and fixed limits from a baseline | 2 |
| C1 | Cp, Cpk, Pp, Ppk and specification limits | 2 |

### Presentation and demo (P)

| ID | Feature | Phase |
|---|---|---|
| P1 | `ShowControlChart` with limits, captions, zones, sigma lines, flagged points | 1 |
| P2 | Stage dividers, excluded points, notes, specification lines | 2 |
| P3 | Demo card on the Line Chart page: X̄–R and I–MR, switches for zones, sigma lines and the rule set | 1 |
| P4 | Demo: p and c examples, click to exclude a point, stages | 2 |
| P5 | Demo: a simulated live process with a "shift the process" button | 3 |

File import and export are left out: the data is a plain table that the
application owns, and `ControlSubgroup` is easy to fill from any reader
`UltraCanvasFileLoader` already offers.

---

## 7. The hard parts

### 7.1 Within-subgroup sigma, not the overall standard deviation

The most common error in home-made control charts is computing the limits from
the standard deviation of all the values. A shift in the process mean inflates
that standard deviation, which widens the limits, which hides the very shift the
chart is meant to show. The limits must use the within-subgroup estimate
(R̄/d₂, S̄/c₄, MR̄/d₂(2)), which a shift between subgroups does not inflate.
The overall standard deviation appears only in Pp and Ppk. `ControlLimitsTest`
includes a data set with a shift where the two estimates differ by a known
amount.

### 7.2 Limits that change from point to point

For p and u with unequal lot sizes, X̄–S with unequal subgroups and the first
EWMA points, each point has its own limits. `ShowControlChart` draws them as
series with `StepMiddle` (E1), so the step sits halfway between points; a line
joining the limit values would suggest limits between points. Zones under such
limits are bands between hidden series (E5). Constant limits stay
`ChartLimiter`s, which keeps the common case on the engine's own caption
placement.

### 7.3 Rules in standard units, and which point is flagged

Rules are evaluated on each point's z-score, (value − centre) / sigma, with that
point's own sigma, so the same code serves constant and varying limits. Where
the lower limit is clamped at zero (§7.5) the zones below the centre are
clamped too.

A pattern flags **the point that completes it** (the eighth of eight on one
side), as the common SPC software does; `firstPoint` keeps where it started for
the tooltip. A point can complete several rules and carries all of them.
Overlapping runs flag each completing point, so a long run flags every point
from the eighth on.

### 7.4 Stages, exclusions and fixed limits

Three things change which points the limits come from, and they compose in one
order: **stages** split the points; within each stage, **excluded** points are
dropped; with a **baseline** or fixed centre and sigma, only those count. Rules
never look across a stage boundary. The test covers each combination on a
small hand-checked data set.

### 7.5 Limits clamped at zero

R, S, MR, p, np, c and u cannot be negative, so a computed lower limit below
zero becomes zero (D₃ = 0 for n ≤ 6 says the same thing). `ShowControlChart`
then draws no lower limit line, and the caption reads "LCL = 0 (none)", so a
value of zero does not look out of control.

### 7.6 Panes in a chart that had one value axis

E6 is the largest change. Today the line chart describes one value axis and
every value-space call (limiters, highlights, bands, span annotations, value
labels) assumes it. With panes, each series maps through its pane's axis;
the existing single-pane case must stay byte-for-byte the same, which
`EngineLineChartTest` checks by rendering a chart before and after
`SetPanes({})`. The stacked area modes stay single-pane (every series is drawn in
pane 0 while stacking), and under Polar panes are ignored.

### 7.7 Labels at the right edge

Each pane has three or more captions at its right edge (UCL, CL, LCL, and
on the location pane USL and LSL). Limiter captions already go through the
label plan; with panes they must stay within their pane's band, so the
broker gets the pane's rectangle as the bounds of those captions. A stepped
limit's caption reads the last point's value.

### 7.8 Colour and meaning

Red is the conventional colour for a flagged point, and `ShowControlChart`
uses it for nothing else: limits are a neutral dark grey, the centre line a
lighter grey, specification limits a distinct colour and dash pattern. A
flagged point is also a different **shape** (a diamond), so the chart reads
without colour. All colours come from the chart theme, so dark mode works.

---

## 8. What already exists to build on

* **`UltraCanvasEngineLineChart`**: series, categorical domain, markers,
  tooltips through hit regions, legend toggling, `ChartLineShape`.
* **`ChartLimiter`** with captions and `extentStart` / `extentEnd`: constant
  limits per stage, and stage dividers on the domain axis.
* **`ChartHighlightShape::ValueBand`**: constant zone shading.
* **Axis sub-bands** (`ChartAxis::bandStart` / `bandEnd`) and their use in
  `UltraCanvasEngineFinancialChart`: the model for E6.
* **`DrawChartMarker` / `ChartMarkerShape`**: flagged (diamond) and excluded
  (hollow) points.
* **`UltraCanvasChartAnalysis.h`** (`MovingAverage`, `FitLinearTrend`): the
  place and style for a UI-free statistics helper.
* **`UltraCanvasFishboneDiagram`**: the next step after a flag; the demo text
  can point to it.

---

## 9. Open questions (with recommendations)

1. **A `ShowControlChart` function, or only the demo code?** → The function.
   Without it every application copies about a hundred lines of demo code,
   including the clamping and caption rules of §7.5 and §7.7, and gets them
   subtly wrong.
2. **Where does the second pane's height come from?** → A weight per pane
   (E6), default 1; `ShowControlChart` gives the dispersion pane 0.55, which
   reads well for X̄–R.
3. **Default rule set?** → Western Electric 1–4, all on; Nelson as the
   alternative; rule 1 only on the dispersion pane.
4. **Should point clicks exclude points automatically?** → No. E7 only reports
   the click; the demo shows exclusion, since what a click means is the
   application's business.
5. **A separate demo page?** → No, a card (and variant) on the Line Chart
   page, as the approach of this proposal is that a control chart is a line
   chart.
6. **Capability histogram?** → Out of scope here. The indices are in the
   result (C1); a histogram beside the chart would be a feature of its own.

---

## 10. Suggested phasing

**Phase 1 — an X̄–R and I–MR chart from line chart parts.** E1–E4 and E6 on
the line chart with `EngineLineChartTest`; the helper with K1, K2 and R1 and
`ControlLimitsTest` (constants against the table in §2.2, limits on
hand-checked data, every rule firing on its minimal pattern and not on a near
miss); `ShowControlChart` (P1); the demo card (P3); the docs.

**Phase 2 — attributes, stages and interaction.** E5 and E7; K3–K5, R2, S1,
S2, C1; P2 and P4.

**Phase 3 — drift and live data.** E8, K6 and P5.
