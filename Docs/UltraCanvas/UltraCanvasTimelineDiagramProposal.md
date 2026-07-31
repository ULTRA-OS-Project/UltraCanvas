# UltraCanvasTimelineDiagram — Research & Feature Proposal

**Status:** **Phases 1-3 are implemented.** Both elements exist:
`UltraCanvasTimelineDiagram` (Family A — see
[`UltraCanvasTimelineDiagram.md`](UltraCanvasTimelineDiagram.md)) and
`UltraCanvasTimelineChart` (Family B — see
[`UltraCanvasTimelineChart.md`](UltraCanvasTimelineChart.md)), each with a demo
page. This document remains the research write-up and the roadmap for the rest.

Delivered: all Phase 1 items plus the `Serpentine`, `Hanging`, `Vertical`,
`Chevron` and `Steps` designs, circle-inscribed bubble text (A-T3), the
independent scale row (A-L10), the "current position" pending style (A-D7), the
proportional-placement bridge (A-L9, pulled forward from Phase 4) and the
geometry query API (A-X3). Cards, boxes and bubbles size themselves to their
text rather than filling the frame.

Two deviations from §5/§8 as written, both deliberate:
`TimelineItem` stores the date as a bare `long` serial (days since 1970-01-01,
identical to `GanttDate::serial`) instead of a `GanttDate`, so the Diagrams
element depends only on the header-only `UltraCanvasCalendarDate.h` and not on
the whole Gantt chart header; and the `None` enumerators became `Hidden` /
`Plain` because `None` is an X11 macro pulled in by the window headers.

Phase 3 delivered `UltraCanvasTimeAxis.h` (Q3: written fresh, Gantt not yet
migrated onto it), B-A1..B-A5, B-E1..B-E3, B-E5, B-E8, B-E6/B-E7 (open-ended and
uncertain dates), B-E9 (span progress), B-L1..B-L4, B-I1, B-I3 and B-D1/B-D2/B-D4.
Spans and milestone callouts share one shelf packer per side, so B-L2 became
real: when a side runs out of room, low-importance labels are dropped rather
than overprinted, and the marker is always kept.

Still open: the `Roadmap` design of the diagram element, item groups (A-D6),
per-item images (A-D4), CSV/JSON IO (A-X1/A-X2/B-D3), keyboard navigation
(A-I6), entrance animation (A-I7); time breaks (B-A6), relative time (B-A7),
swimlanes (B-E4), the overview minimap (B-I2), drag-to-reschedule (B-I4), the
crosshair read-out (B-I5); migrating the Gantt chart onto `UltraCanvasTimeAxis`;
and the Gantt additions G1/G2.

**Author:** UltraCanvas Framework
**Last Modified:** 2026-07-31
**Related:** `UltraCanvasGanttChart` (implemented), `UltraCanvasPertChart`
(implemented), `UltraCanvasStepper` (implemented), `UltraCanvasCircularInfoGraphic`
(implemented), orphaned `Plugins/Graphs/UltraCanvasTimeline.{h,cpp}` (see §9).

---

## 1. Summary — the headline question

The reference images do **not** show one diagram with several skins. They show
**two structurally different diagram families that share the word "timeline"**:

| | **Family A — Narrative timeline** | **Family B — Chronological timeline** |
|---|---|---|
| What it is | An ordered *story*: N events, each with a caption, a title and a paragraph | A *schedule* drawn on a real date axis |
| Position of an item | Evenly spaced along a decorative path (bar, snake, zigzag) — **time is not to scale** | Proportional to its date — **time is to scale** |
| Primary content | Rich per-item content: icon, number, heading, body text, image | Dates, durations, milestone names, lanes |
| Item count | 4–12 (a slide) | 10–1000 (a plan) |
| Typical question | "What happened, in what order?" | "When exactly, for how long, what overlaps?" |
| Gaps in time | Invisible — a 1-year gap and a 10-year gap look identical | Visible and meaningful |
| Sizing | Item boxes size to their *text* | Item bars size to their *duration* |
| Reference images | 1, 2, 3 | 4, 5 |

**Recommendation: yes, these must be two element types.** A single class would
carry two disjoint data models (ordinal items with rich content vs. date-anchored
spans and milestones), two layout engines (path-following vs. axis projection),
and two style vocabularies, with almost no shared code beyond the palette.
The detailed rationale, and the one rejected alternative, are in §4.

The proposed split:

- **`UltraCanvasTimelineDiagram`** — new element, `Plugins/Diagrams/`.
  Family A. Design-preset driven, like `UltraCanvasSWOTDiagram`.
- **`UltraCanvasTimelineChart`** — new element, `Plugins/Charts/`.
  Family B, milestone-and-span oriented, *without* the Gantt task table and
  dependency graph.
- **`UltraCanvasGanttChart`** — already implemented, needs **no** new work for
  these images. Reference image 4 is a Gantt chart, not a new element (§3.4).

A bridge feature keeps the split from becoming a wall: `TimelineDiagram` gains an
optional `TimelinePlacement::Proportional` mode (§6, L9), so a decorative
timeline can *also* be date-accurate when the data supports it.

---

## 2. What a timeline diagram is

A timeline is a visual arrangement of events along a time ordering. Two things
are always encoded — **sequence** (what came before what) and **identity** (what
each event is) — and one thing is optionally encoded: **metric time** (how far
apart events are). That optional third encoding is exactly what separates the
two families above.

Industry vocabulary, from the sources consulted:

- **Chronological timeline** — the simplest kind, events in order earliest to
  latest; used for history, company milestones, biographies.
- **Milestone timeline / milestone chart** — points only, no durations; answers
  "what gets delivered when" and deliberately omits "how we get there".
- **Roadmap timeline** — phases and themes over quarters, for strategy
  communication; usually to-scale but coarsely.
- **Serpentine / zigzag / alternating timeline** — a presentation layout: a path
  that wraps or a spine with content alternating on both sides, chosen because it
  fits many items into a 16:9 frame, not because it means anything.
- **Gantt chart** — one row per task, bars proportional to duration, plus
  dependencies, progress and critical path.

The Gantt-vs-timeline distinction is consistently drawn the same way in the
project-management literature: a timeline is *linear and simple*, focused on
milestones and the overall sequence for stakeholder communication; a Gantt chart
is *two-dimensional* — tasks down the vertical axis, schedule across the
horizontal — and adds dependencies, progress tracking and resource detail. Many
teams use both: the timeline for the executive update, the Gantt as the working
schedule behind it. That is precisely why UltraCanvas should ship both and not
force one to imitate the other.

Sources consulted:
[Creately — 15 Types of Timelines](https://creately.com/guides/types-of-timelines/),
[EdrawMind — Timeline Infographics: Types and Examples](https://edrawmind.wondershare.com/examples/timeline-infographics.html),
[Lucidchart — All About Timelines](https://www.lucidchart.com/pages/timelines),
[Indeed — 11 Types of Timelines](https://www.indeed.com/career-advice/career-development/types-of-timelines),
[Deckary — Timelines and Roadmaps](https://deckary.com/blog/pillar-timelines-roadmaps-guide),
[Smartsheet — Gantt Chart vs. Project Timeline](https://www.smartsheet.com/content/gantt-vs-timeline),
[GanttPRO — Gantt Chart vs. Timeline](https://blog.ganttpro.com/en/gantt-chart-vs-timeline/),
[ClickUp — Gantt Chart vs. Timeline](https://clickup.com/blog/gantt-vs-timeline/),
[PresentationGO — Timeline templates](https://www.presentationgo.com/presentation/category/timelines-planning/timelines/).

---

## 3. What the five reference images demand

Each image is read as a capability specification.

### Image 1 — "Timeline Infographic": segmented spine, alternating nodes (Family A)

A thick horizontal bar split into six **colored segments**, one per year. Each
segment carries a **circular node with an icon** sitting on the bar's edge —
nodes alternate **above and below** the bar. The year captions (`2015` … `2020`)
sit on the opposite side of the bar from their node, in the segment's color, at a
large type size. Body text sits beyond each node. A title block sits above the
whole diagram. Colors cycle through a 6-entry flat palette; the last segment is
a neutral slate.

> Requires: `Bar` design with per-item segment fill, alternating item side,
> circular icon nodes anchored on the spine, a large "caption" field distinct
> from the title/body, per-item palette color, diagram title + subtitle.

### Image 2 — Serpentine roadmap with 12 nodes (Family A)

A **snaking path** (three horizontal runs joined by 180° rounded turns) rendered
as a thick colored ribbon whose hue shifts gradually along its length. Twelve
**circular icon nodes** sit *on* the path with a year label inside or beside
each; text blocks sit alternately above and below each run. A decorative
"header" pill sits at the path's start. Item order runs left→right, then
right→left on the next run (boustrophedon).

> Requires: `Serpentine` design with configurable runs-per-row and turn radius,
> path-following node placement, gradient along the path, alternating text side
> per run, start/end cap decorations, correct reading order on reversed runs.

### Image 3 — Hanging drops from a month axis (Family A, near-proportional)

A thin horizontal axis with **twelve month labels** (`Jan`…`Dec`) above it, each
with a small dot. Colored **tick blocks** sit *on* the axis at eight of the
twelve positions. From each tick, a thin **stem of varying length** drops to a
**circle of varying diameter** containing an icon and a paragraph. Circles are
deliberately staggered vertically to avoid collision, each with a soft drop
shadow. Note that the axis is a *calendar* axis (months), and the labelled
positions are evenly spaced — so this is Family A layout over a Family-B-looking
axis, which is exactly the case the `Proportional` bridge mode (L9) covers.

> Requires: `Hanging` design (axis + stems + bubbles), per-item stem length and
> bubble radius, auto-stagger to prevent overlap, text wrapped **inside** a
> circle (non-rectangular text flow), drop shadows, an axis-scale label row that
> is independent of the item list, ticks drawn on the axis.

### Image 4 — "Project Timeline": steps × weeks grid (Family B — **already built**)

A left table of numbered steps (`STEP 1 … STEP 8` with names), a header row of
week columns (`WEEK 1 … WEEK 7`), and colored **bars spanning week ranges** on
each row, with a per-cell footnote row of small annotations underneath. Banded
row striping, a brand block bottom-left.

> This is a Gantt chart in every structural respect: row per task, columns as a
> time scale, bars spanning columns, a task table on the left.
> `UltraCanvasGanttChart` already provides all of it — `GanttTimeScale::Weeks`,
> the `Wbs`/`Name` table columns, `GanttDesign::Minimal`, row striping. The only
> gap is the per-bar footnote annotation row, which is a small additive feature
> on the Gantt element (§7, G2) — **not** a reason for a new element.

### Image 5 — "Development Timeline": milestone flags on a date spine (Family B)

A single horizontal **calendar axis** with a two-tier header (year band above,
month band below) covering one year. On and around that axis:
**duration bars** at several vertical offsets (`Partner Referral`,
`Legal Agreements Executed`, `Architecture Review`, `Handoff Calls`,
`Ongoing Partner Development`, `Partner Marketing`), each labelled inside the
bar and dated with a small date caption; **milestone markers** as small
triangles/flags on the axis with dates; three **major milestones** (`Milestone 1`
… `Milestone 3`) as large inverted triangles above the axis with a dated caption;
and **project start / project end** markers as filled triangles anchored to the
axis ends. Bars are stacked at whatever vertical slot keeps them from colliding —
there is **no task table and no dependency arrows**.

> Requires: the `UltraCanvasTimelineChart` element — real date axis with tiered
> headers, span bars with automatic vertical packing (not one row per item),
> point milestones in several marker styles above/below the axis, dated captions
> per item, start/end bookend markers, and a "today"/now line.

### Cross-image constants

Every image needs: a **color palette cycle**, **rounded/pill geometry**, **soft
shadows**, **icons inside markers**, **multi-field text per item** (caption /
title / body), **auto text fitting**, and a **light and dark** rendering.

---

## 4. Why two elements, not one

### The rejected alternative — one class, one mode flag

```cpp
enum class TimelineMode { Infographic, Chronological };   // rejected
```

Rejected because the two modes would share almost nothing:

| Concern | Family A | Family B |
|---|---|---|
| Item identity | ordinal index | date / date range |
| Item geometry | driven by text extent | driven by duration × pixels-per-day |
| Layout algorithm | walk a parametric path, distribute N nodes | project dates onto an axis, then pack rows to avoid collision |
| Overflow behaviour | shrink text, wrap to next run | scroll/zoom the axis |
| Hit testing | item boxes and nodes | axis positions and bars |
| Style struct | node shape, connector, side alternation, path curvature | tick tiers, scale, grid, weekend shading, marker styles |
| Data source | `std::vector<TimelineItem>` | date-indexed spans + milestones, sortable, streamable |

Roughly 80% of the members of the merged style struct would be inert in either
mode — the anti-pattern the framework already avoids by keeping
`UltraCanvasQuadrantChart` (scatter SWOT) separate from `UltraCanvasSWOTDiagram`
(text-panel SWOT), even though both are "a SWOT".

### What the two elements *should* share

1. **`GanttDate`** (`include/Plugins/Charts/UltraCanvasCalendarDate.h`, header-only)
   — already the framework's date type. Both elements use it; no new date type.
2. **A palette/design preset system** — the `SetPalette` / `ApplyDesign` pattern
   already used by Gantt, PERT and SWOT.
3. **`UltraCanvasChartElementBase`** — background, title, tooltips, hover state,
   zoom/pan plumbing.
4. **A time-axis renderer.** Gantt already contains a two-tier scale header
   internally. Proposal: extract it to
   `include/Plugins/Charts/UltraCanvasTimeAxis.h` (tier resolution, tick
   generation, date↔pixel projection, label formatting) so Gantt, TimelineChart
   and the `Proportional` mode of TimelineDiagram share one implementation.
   See §8 Q3 — this is a refactor of working code and is deliberately scheduled
   as its own step.
5. **`UltraCanvasLabelPlacement.h`** — already exists and is the right home for
   the collision-avoidance used by callout labels (B-L4) and hanging bubbles (A-L7).

### Naming and discoverability

Two elements with similar names is a documentation problem, so the docs must
route users explicitly. Proposed one-line selector, to appear at the top of every
related doc:

> Presentation/story with N events and rich text → `UltraCanvasTimelineDiagram`.
> Dates to scale, milestones and spans, no task table → `UltraCanvasTimelineChart`.
> Full project schedule with tasks, dependencies, progress → `UltraCanvasGanttChart`.
> Fixed process steps with a current position → `UltraCanvasStepper`.
> Sequence around a circle → `UltraCanvasCircularInfoGraphic`.

---

## 5. Data model sketch

### Family A — `UltraCanvasTimelineDiagram`

```cpp
namespace UltraCanvas {

    enum class TimelineNodeShape {
        Circle, RoundedSquare, Square, Hexagon, Diamond,
        Pin,        // map-pin / teardrop
        Flag,       // pennant on a short pole
        Chevron,    // arrowhead pointing along the path
        None
    };

    struct TimelineItem {
        std::string caption;      // "2015", "Q3", "Phase 1" — the big period label
        std::string title;        // short heading
        std::string body;         // paragraph, wrapped
        std::string iconGlyph;    // icon font glyph or short text inside the node
        std::string iconImage;    // optional image path inside the node
        Color accentColor = Color(0,0,0,0);   // transparent = take palette entry

        // Optional real date; only used by TimelinePlacement::Proportional (L9)
        GanttDate date;
        bool hasDate = false;

        // Per-item layout overrides (0 = auto)
        float nodeSize = 0.0f;    // node radius / half-extent
        float stemLength = 0.0f;  // Hanging design
        int   side = 0;           // -1 = above/left, 0 = auto-alternate, +1 = below/right

        std::string tooltip;      // overrides the generated tooltip
        bool highlighted = false; // "current"/"you are here" emphasis
    };
}
```

### Family B — `UltraCanvasTimelineChart`

```cpp
namespace UltraCanvas {

    enum class TimelineEntryKind { Milestone, Span, Marker, Era };

    struct TimelineEntry {
        TimelineEntryKind kind = TimelineEntryKind::Milestone;
        std::string name;
        std::string detail;             // second label line
        GanttDate start;                // milestones use start only
        GanttDate end;                  // inclusive, like GanttDate elsewhere
        bool openEnded = false;         // "…and ongoing"
        int  lane = -1;                 // -1 = auto-pack
        int  side = 0;                  // -1 above axis, +1 below, 0 = auto
        Color color = Color(0,0,0,0);   // transparent = palette
        std::string iconGlyph;
        float importance = 1.0f;        // scales marker size; drives label priority
        // Fuzzy/uncertain dates (B-D6)
        int startUncertaintyDays = 0;
        int endUncertaintyDays = 0;
    };

    class TimelineChartDataSource {          // mirrors GanttDataSource in shape
        int AddMilestone(const std::string& name, GanttDate on);
        int AddSpan(const std::string& name, GanttDate from, GanttDate to);
        int AddEra(const std::string& name, GanttDate from, GanttDate to);  // background band
        // + setters, removal, sorting, bounds query
    };
}
```

---

## 6. Feature list — `UltraCanvasTimelineDiagram` (Family A)

Tags: **D** data, **L** layout/design, **S** style, **I** interaction, **T** text,
**X** import/export. Phase in §10.

### Designs (L1 — the preset enum)

Surveyed across PresentationGO, Canva, Venngage, Visme, SmartArt and the
reference images; grouped into structural families, mirroring the SWOT survey
approach:

| `TimelineDesign` | Description | From |
|---|---|---|
| `Bar` | Horizontal spine split into colored segments; nodes alternate above/below | Image 1 |
| `Line` | Thin single-color horizontal axis, nodes on it, content on one or both sides | classic |
| `Alternating` | Center spine with content boxes zigzagging above/below (or left/right when vertical) | classic |
| `Serpentine` | Snaking ribbon wrapping over multiple runs | Image 2 |
| `Hanging` | Axis on top, stems dropping to bubbles of varying size | Image 3 |
| `Vertical` | Top-down spine, cards left/right, best for long lists and scrolling | classic |
| `Chevron` | Sequence of arrow/chevron blocks pointing forward | process style |
| `Steps` | Ascending staircase blocks, growth narrative | process style |
| `Roadmap` | Winding road with lane markings and milestone signs | roadmap style |
| `Cards` | Row of separated cards over a thin rule, no decorative path | minimal style |

`Circular` and `Spiral` layouts are deliberately **out of scope** — they are
already served by `UltraCanvasCircularInfoGraphic`; the docs must cross-link.

### Data & content

- **A-D1** Ordered `std::vector<TimelineItem>`; add/insert/remove/move, `Clear`.
- **A-D2** Four independent text fields per item (caption, title, body, icon).
- **A-D3** Per-item accent color, falling back to a palette cycle.
- **A-D4** Optional per-item image (loaded through the framework's image path,
  fitted into the node or a card thumbnail).
- **A-D5** Optional `GanttDate` per item, enabling `Proportional` placement (L9)
  and date-formatted captions.
- **A-D6** Item groups/phases: consecutive items may share a `groupName`,
  rendered as a bracket/band behind them with its own label.
- **A-D7** "Current position" index — everything before it renders as completed
  (filled), everything after as pending (outlined) — the roadmap/progress idiom.

### Layout

- **A-L1** Design preset switch (`ApplyDesign`), replacing the whole style while
  keeping data — same contract as `GanttChart::ApplyDesign`.
- **A-L2** Orientation: `Horizontal` / `Vertical`, valid for every design that
  can support it.
- **A-L3** Direction: forward/reverse (right-to-left, bottom-to-top) for RTL
  locales and "countdown" narratives.
- **A-L4** Side policy: `Alternate` / `AllAbove` / `AllBelow` / `PerItem`.
- **A-L5** Serpentine controls: items-per-run or runs count, turn radius, turn
  direction, correct boustrophedon reading order.
- **A-L6** Node placement on the path: on-axis, above-axis, offset by a fraction.
- **A-L7** Automatic overlap resolution: stagger stem lengths / box offsets so
  neighbouring content never collides (needed by `Hanging`, useful everywhere) —
  built on `UltraCanvasLabelPlacement.h`.
- **A-L8** Auto-fit: the whole diagram scales to the element rect; optional
  `FitPolicy::ShrinkText` / `Scroll` / `WrapToRuns` when items don't fit.
- **A-L9** **`TimelinePlacement::Even | Proportional`** — the bridge feature.
  With `Proportional` and dated items, positions along the path are computed from
  the dates instead of evenly spaced, while the decorative design is preserved.
  Includes a minimum-gap constraint so near-simultaneous items stay legible.
- **A-L10** Optional scale row: an independent axis label track (`Jan…Dec`,
  `2015…2020`) drawn along the spine, decoupled from the item list (Image 3).

### Style

- **A-S1** Palette presets (reuse the `GanttPalette` value set: `CorporateBlue`,
  `Vibrant`, `Pastel`, `Ocean`, `Sunset`, `Forest`, `Slate`, `Mono`) plus
  `SetCustomPalette`.
- **A-S2** Color modes: `PerItem` (cycle), `Single`, `GradientAlongPath`
  (Image 2), `ByGroup`.
- **A-S3** Node shapes per `TimelineNodeShape`, with per-item override.
- **A-S4** Node decorations: outer ring, contrast border, numbering (1,2,3 / I,II,III),
  icon glyph, image fill.
- **A-S5** Spine/path styling: thickness, solid or per-item segments, rounded
  caps, dashes, arrowhead at the end, gradient.
- **A-S6** Connector/stem styling: straight, elbow, curved, dashed; per-item
  length; leader dot at the content end.
- **A-S7** Content container: none, card (filled/outlined), speech bubble with a
  tail pointing at the node, circle (Image 3), chevron.
- **A-S8** Drop shadows (offset, blur, color) on nodes and cards.
- **A-S9** Light/dark theme switch that sets every color coherently, as
  `UltraCanvasSWOTDiagram::SetDarkTheme` does.
- **A-S10** Diagram title + subtitle block with its own placement and fonts.
- **A-S11** Per-role font sizes (caption / title / body / icon / axis) and an
  overall scale factor.

### Text

- **A-T1** Word wrap with max-lines and ellipsis for title and body.
- **A-T2** Auto-shrink to fit its container, with a floor font size.
- **A-T3** Text inside non-rectangular shapes — circle-inscribed wrapping for the
  `Hanging` bubbles (Image 3) — a real requirement that plain rect wrapping fails.
- **A-T4** Alignment and vertical anchoring per text role.
- **A-T5** Date-formatted captions when `hasDate` (reusing `GanttDateFormat`).

### Interaction

- **A-I1** Hover highlight of node + connector + content as one unit.
- **A-I2** Tooltip per item, with a per-item override and a custom generator.
- **A-I3** `onItemClick` / `onItemHover` callbacks with the item index.
- **A-I4** Selection state with a visual treatment.
- **A-I5** Optional expand/collapse of body text for dense diagrams.
- **A-I6** Keyboard navigation across items (accessibility + kiosk use).
- **A-I7** Optional entrance animation: items revealed in sequence along the path
  (`UltraCanvasChartElementBase` already carries animation state).

### Import/export

- **A-X1** CSV import, following the `UltraCanvasCircularInfoGraphic` convention:
  one line per item, `Caption;Title;Body;Icon;Color;Date`.
- **A-X2** JSON import/export through `UltraCanvasJSON` (never yyjson directly).
- **A-X3** Query API for computed geometry (node centers, content rects) so
  applications can overlay their own annotations.

---

## 7. Feature list — `UltraCanvasTimelineChart` (Family B)

### Axis & scale

- **B-A1** Two-tier header (major/minor) with `TimelineScale`:
  `Auto | Minutes | Hours | Days | Weeks | Months | Quarters | Years | Decades`.
  Superset of `GanttTimeScale` — sub-day and decade tiers are new and needed for
  logs and for historical timelines.
- **B-A2** Date↔pixel projection with pixels-per-day, `SetDateRange`,
  `FitToData` with padding.
- **B-A3** Grid lines per tier; weekend/non-working shading (reuse the Gantt
  style knobs).
- **B-A4** Today/now marker line with a label; `SetNow(GanttDate)`.
- **B-A5** Axis position: top, bottom, or center (center is what Image 5 needs,
  with content above and below).
- **B-A6** **Time breaks** — collapse an empty stretch into a zigzag gap so long
  quiet periods don't dominate. Common in historical timelines; absent from Gantt.
- **B-A7** Relative time mode: label as `Day 0 / +14d / +3mo` from an origin
  instead of calendar dates.

### Entries

- **B-E1** Milestones as points, marker styles: `Diamond`, `Circle`, `Square`,
  `TriangleUp`, `TriangleDown`, `Flag`, `Pin`, `Star`, `Image` — Image 5 uses
  triangles and flags, which Gantt's three shapes don't cover.
- **B-E2** Span bars with rounded/flat/pill styles, inside or outside labels,
  and a duration caption.
- **B-E3** **Automatic vertical packing** of entries into as few lanes as fit
  without overlap — the key difference from Gantt's fixed one-row-per-task.
  With `lane >= 0` an entry can be pinned to an explicit lane.
- **B-E4** Explicit swimlanes with headers (team, workstream, category) as an
  alternative grouping mode.
- **B-E5** Era/period background bands spanning a date range behind everything,
  with a label (`Phase 1`, `FY26`).
- **B-E6** Open-ended entries (no end date) rendered with a fade or arrow.
- **B-E7** Uncertainty/fuzzy dates rendered as a lighter extension on each end —
  needed by historical and research timelines.
- **B-E8** Bookend markers for project start/end (Image 5's filled triangles).
- **B-E9** Progress fraction on a span (light track / darker fill), reusing the
  Gantt progress styles — useful without pulling in the whole Gantt model.

### Labels

- **B-L1** Callout labels with leader lines when an entry is too narrow for an
  inside label.
- **B-L2** Automatic label collision avoidance with a priority order driven by
  `importance`, dropping or stacking overflow labels rather than overprinting.
- **B-L3** Dated captions under names (Image 5 shows a date under each marker),
  with `GanttDateFormat` control.
- **B-L4** Above/below-axis alternation for point milestones.

### Interaction

- **B-I1** Horizontal zoom (wheel/pinch) and pan (drag), with clamping —
  matching `UltraCanvasContourChart`'s zoom/pan contract, including
  double-click-to-reset.
- **B-I2** Overview/minimap band showing the full range with the current window,
  draggable.
- **B-I3** Hover highlight + tooltip per entry; `onEntryClick` / `onEntryHover`.
- **B-I4** Optional editing: drag a milestone to a new date, drag a span's edges
  to reschedule, with a `onEntryDateChanged` callback and snap-to-scale.
- **B-I5** Crosshair with a live date read-out.

### Data & IO

- **B-D1** `TimelineChartDataSource` mirroring `GanttDataSource`'s shape and
  ownership (`std::shared_ptr`), so the two are interchangeable in app code.
- **B-D2** Construction directly from a `GanttDataSource` (`FromGantt`) so a
  project plan can be presented as a stakeholder timeline without re-entry —
  this is the "same data, two audiences" workflow the PM literature describes.
- **B-D3** CSV and JSON import/export via `UltraCanvasJSON`.
- **B-D4** Designs and palettes as presets (`Classic`, `Modern`, `Minimal`,
  `Roadmap`, `Dark`, `Print`), same `ApplyDesign` contract.

### Additive features on the existing Gantt element (from Image 4)

- **G1** Optional per-row footnote/annotation strip under the bars.
- **G2** `GanttColumnType::StepNumber` — a `STEP n` styled column (cosmetic
  variant of `Wbs`).

Both are small and self-contained; neither justifies a new element.

---

## 8. Open questions (with recommendations)

**Q1 — Which plugin directory for the infographic timeline?**
Recommend `Plugins/Diagrams/` + `include/Plugins/Diagrams/`, alongside
`UltraCanvasSWOTDiagram`, since it is a presentation diagram, not a data plot.
`UltraCanvasTimelineChart` belongs in `Plugins/Charts/` next to the Gantt chart.

**Q2 — Base class for the infographic element?**
Recommend `UltraCanvasChartElementBase`, as `UltraCanvasSWOTDiagram` does — it
brings title, background, tooltip and hover plumbing for free even though there
is no data source. `dataSource` simply stays null.

**Q3 — Extract the shared time axis now or later?**
Recommend building `UltraCanvasTimelineChart` on a **new**
`UltraCanvasTimeAxis.h` written from the start, then migrating Gantt to it as a
separate, independently reviewable change. Doing the extraction first would
block new work behind a refactor of a large, working element.

**Q4 — Should `TimelineDiagram` support scrolling for long item lists?**
Recommend `FitPolicy::WrapToRuns` (serpentine-style) as the default overflow
behaviour and `Vertical` + scroll as the explicit long-list answer; a scrolling
horizontal infographic reads badly.

**Q5 — Icons.** Both elements want a glyph inside a marker. Recommend accepting
(a) a UTF-8 string drawn in the current font, and (b) an image path, with no new
icon-font dependency — consistent with the dependency policy.

**Q6 — Is `Roadmap` a design of `TimelineDiagram` or its own element?**
Recommend a design preset. A product roadmap differs from a timeline infographic
only in decoration; where it becomes date-accurate with swimlanes it is already
`UltraCanvasTimelineChart` with `B-E4`.

---

## 9. Fate of the existing `Plugins/Graphs/UltraCanvasTimeline.{h,cpp}`

There is an existing `UltraCanvasTimeline` class (205 + 828 lines, dated
2025-08-28) under `UltraCanvas/Plugins/Graphs/`. It is **orphaned**:

- not listed in `UltraCanvas/CMakeLists.txt` (so it is never compiled),
- not referenced by any header, app, test or doc,
- not derived from `UltraCanvasUIElement` — it has its own `Width/Height/PositionX/PositionY`
  and a `Render()` that does not take a `IRenderContext`,
- uses `std::chrono::steady_clock` time points, which are *monotonic process
  uptime*, not wall-clock dates — usable for profiling captures
  (`StartTimeCapture`/`EndTimeCapture`), not for a calendar timeline,
- has a global `TimelineManager` returning raw pointers, against the
  `std::shared_ptr` widget convention,
- `Plugins/Graphs/` contains nothing else and is not a registered plugin
  directory.

Recommendation: **retire it** as part of this work — delete the file pair and the
empty `Plugins/Graphs/` directory, and note the removal in `Docs/UltraCanvas/CHANGELOG.md`.
Nothing can break, since nothing compiles it. Its one genuinely distinct idea —
live capture of in-process event durations for profiling — is worth preserving as
a future `B-D5`: a `TimelineChart` streaming/append mode fed by a monotonic clock
with an origin mapped to wall time. It should not block this proposal.

---

## 10. Suggested phasing

**Phase 1 — `UltraCanvasTimelineDiagram` core**
A-D1..A-D3, A-L1 (designs `Bar`, `Line`, `Alternating`, `Cards`), A-L2, A-L4,
A-L8, A-S1..A-S5, A-S9..A-S11, A-T1, A-T2, A-T4, A-I1..A-I3.
Deliverable: images 1's structure fully reproducible, plus the classic layouts.
Docs: `UltraCanvasTimelineDiagram.md` + `Apps/DemoApp/UltraCanvasTimelineDiagramExamples.cpp`.

**Phase 2 — the distinctive designs**
`Serpentine` (A-L5), `Hanging` (A-L7 + A-T3 circle text), `Vertical`, `Chevron`,
`Steps`, `Roadmap`; A-S6..A-S8, A-L10, A-D6, A-D7.
Deliverable: images 2 and 3 reproducible.

**Phase 3 — `UltraCanvasTimelineChart`**
`UltraCanvasTimeAxis.h` (Q3), B-A1..B-A5, B-E1..B-E3, B-E5, B-E8, B-L1..B-L4,
B-I1, B-I3, B-D1, B-D4.
Deliverable: image 5 reproducible.

**Phase 4 — depth**
A-L9 (proportional bridge), A-X1..A-X3, A-I4..A-I7; B-A6, B-A7, B-E4, B-E6,
B-E7, B-E9, B-I2, B-I4, B-I5, B-D2, B-D3; Gantt additions G1, G2; retirement of
the orphaned element (§9).

Every phase follows the house rules: matching doc under `Docs/UltraCanvas/`
updated in the same change, demo example registered in the root `CMakeLists.txt`,
source registered in `UltraCanvas/CMakeLists.txt`, and
`python3 scripts/generate_llms_txt.py` re-run.
