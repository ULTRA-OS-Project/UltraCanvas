# UltraCanvasKanbanBoard — Research & Feature Proposal

Status: **Proposal — not yet implemented.** This document is the research
write-up and feature list for a comprehensive Kanban board element (and its
analytics companion, the cumulative flow chart). It follows the same process
as [`UltraCanvasContourChartProposal.md`](UltraCanvasContourChartProposal.md).

Author: UltraCanvas Framework
Last Modified: 2026-07-30

---

## 1. What a Kanban board is

A Kanban board visualises **work items flowing through the stages of a
process**. Work items are *cards*; process stages are vertical *columns*; a
card moves left-to-right through the columns as the work progresses. The
method (from Toyota's manufacturing signboards, adapted to knowledge work by
David Anderson) adds rules on top of the picture that turn it from a to-do
list into a flow-management tool.

The canonical anatomy has **five components**:

| Component | Meaning |
|---|---|
| **Visual signals (cards)** | One card per work item: title, owner, due date, priority, description, tags — enough context to act on |
| **Columns** | Workflow stages. Minimal: *To Do / In Progress / Done*. Real boards add stages (*Backlog, Design, Develop, Test, Deploy*) and may **split a column** into "Doing / Done" sub-columns to make hand-offs explicit |
| **WIP limits** | A per-column (or per-lane) maximum card count. The backbone of the method: exceeding the limit is highlighted, exposing bottlenecks and preventing overload |
| **Commitment point** | The column boundary where the team commits to doing the work (typically Backlog → To Do) |
| **Delivery point** | The boundary where work counts as delivered (entering Done). Lead time is measured commitment → delivery |
| **Swimlanes** | Horizontal rows crossing all columns, separating work by class of service, priority (an *expedite* lane), team, or work type |

Flow metrics defined on top of the board:

* **Lead time** — commitment point → delivery point, per card.
* **Cycle time** — start of active work (In Progress) → delivery, per card.
* **WIP** — cards currently between commitment and delivery.
* **Throughput** — cards delivered per unit time.
* **Cumulative flow diagram (CFD)** — a stacked area chart of card count per
  stage over time; band thickness = WIP per stage, horizontal distance
  between band edges = approximate lead/cycle time (see §6).

Sources consulted:
[Atlassian — What is a kanban board?](https://www.atlassian.com/agile/kanban/boards)
(the guide linked in the task brief is a CDN mirror of this Atlassian corpus),
[Atlassian — Kanban metrics](https://www.atlassian.com/agile/project-management/kanban-metrics),
[Businessmap — Vital Kanban board features](https://businessmap.io/blog/best-kanban-board-features),
[Businessmap — Lead time vs cycle time](https://businessmap.io/kanban-resources/kanban-software/kanban-lead-cycle-time),
[Kanban Tool — Main components of the Kanban board](https://kanbantool.com/main-components-of-the-kanban-board),
[Kanban Zone — The anatomy of a Kanban board](https://kanbanzone.com/2019/kanban-board-anatomy/),
[Asana — Kanban explained](https://asana.com/resources/what-is-kanban),
[GeeksforGeeks — Cumulative flow diagrams in Kanban](https://www.geeksforgeeks.org/software-testing/cumulative-flow-diagrams-in-kanban/),
[Kanban Tool — Cumulative flow diagram](https://kanbantool.com/cumulative-flow-diagram).

---

## 2. What the five reference images demand

### Image 1 — Pastel sticky-note board (light)
White column tracks with slim colored header bars (*Backlog, To Do,
In Progress, Testing, Done*), pastel sticky-note cards **freely offset inside
their column** (not a strict vertical stack), short multi-line text, cards in
several accent colors independent of the column color.

> Requires: per-column header color, sticky-note card visual, free/staggered
> card placement inside a column as a layout option, per-card color override.

### Image 2 — "Kanban Board Template" poster (WordLayouts)
Board title centered on top; a **priority legend** (`Overdue / High / Low` as
colored dots) top-left; pill-shaped column headers each ending in a detached
circle; **full-height solid colored column panels**; white cards with a
folded "dog-ear" corner and drop shadow; card contents: colored title
(`TASK 01`), description text, a labelled field (`Due Date:` + value), and a
priority dot in the card corner; cards staggered left/right within a column.

> Requires: board title, legend element mapping priority → color, decorated
> column headers (pill + connector + circle), solid column panels, card with
> title/description/due-date/priority-dot layout, folded-corner + shadow card
> style, stagger layout.

### Image 3 — Minimal schematic board
Single light-gray rounded panel; column titles in a plain header row
separated from the body by a line; **dashed vertical separators** between
columns (*Pending, Design, Develop, Test, Deploy*); cards are plain colored
rounded rectangles (no text) with an optional small icon; strict grid
placement.

> Requires: a schematic/presentation design preset — headerless-color mode,
> dashed column separators, icon-only compact cards, uniform grid layout.

### Image 4 — "Kanban diagram to track cycle time" (SlideTeam)
**Not a board.** A stacked area chart over *Month 1 … Month 9* with bands
*To do / Doing / QA / Done*, a vertical legend, and three annotations drawn
onto the chart: a horizontal **Lead Time** arrow, a horizontal **Cycle Time**
arrow, and a vertical **WIP Limit** arrow. This is the classic **cumulative
flow diagram**, the standard Kanban analytics companion. §6 resolves whether
it belongs inside the board element.

> Requires: stacked-band time chart from per-stage counts, labelled
> horizontal/vertical span annotations, per-band colors, side legend.

### Image 5 — "Kanban timeline template" (portfolio matrix)
A matrix: **columns are statuses** (*Completed / In Progress / Soon /
Future*), **rows are swimlanes** (*New features, Stickiness, Integrations,
Infrastructure*) with dark row-header tabs on the left; cards are one-line
colored pills, colored **by column** (all Completed green, all In Progress
blue …); a dark footer strip maps columns to quarters (*Q1 … Q4*).

> Requires: swimlanes with row headers, card-color-by-column mode, compact
> single-line pill cards, a footer caption strip per column, timeline-flavored
> design preset.

---

## 3. How this fits the existing UltraCanvas code

The Gantt chart is the architectural template: it is the framework's other
project-management element and already solved the data-source / style /
design-preset / palette split. The Kanban element should **reuse, not
duplicate**:

| Existing piece | Reuse for |
|---|---|
| `UltraCanvasGanttChart` (`GanttDataSource` + element + `GanttChartStyle` + `GanttDesign` presets + `GanttPalette`) | The overall architecture pattern: `KanbanDataSource` + `UltraCanvasKanbanBoardElement` + `KanbanBoardStyle` + `KanbanDesign` presets + palettes |
| `include/Plugins/Charts/UltraCanvasCalendarDate.h` (`GanttDate`) | Card due dates, created/moved timestamps, overdue detection — no new date code |
| `UltraCanvasFlowChart` node dragging (`isDraggingNode`, drag offsets, `onNodeDragged`) | The card drag-and-drop interaction pattern |
| `UltraCanvasChartElementBase` | Tooltip infrastructure, empty-state, animation timing (the board itself derives from `UltraCanvasUIElement` like the Gantt does, but borrows these behaviours) |
| `UltraCanvasAreaChartElement` / `UltraCanvasChartElementBase` axes | The cumulative flow chart (§6): stacked bands, time axis, legend |
| `UltraCanvasLabelPlacement.h` | Avoiding label collisions in annotation placement on the CFD |
| Gantt priority chips, tooltip and scroll/zoom conventions | Priority dots/chips on cards; wheel = vertical scroll, Shift+wheel = horizontal, tooltips on hover |
| `UltraCanvasJSON` | Board save/load (see D12) |

---

## 4. Proposed architecture

```
include/Plugins/Charts/UltraCanvasKanbanBoard.h       # data source + element + style
Plugins/Charts/UltraCanvasKanbanBoard.cpp
include/Plugins/Charts/UltraCanvasCumulativeFlowChart.h  # §6 companion element
Plugins/Charts/UltraCanvasCumulativeFlowChart.cpp
Apps/DemoApp/UltraCanvasKanbanBoardExamples.cpp       # one demo per design preset
Docs/UltraCanvas/UltraCanvasKanbanBoard.md            # API doc (written with the code)
```

Placed under `Plugins/Charts/` beside the Gantt chart (both are
project-management elements sharing `UltraCanvasCalendarDate.h`).

### 4.1 Data model

```cpp
auto data = std::make_shared<KanbanDataSource>();

int backlog = data->AddColumn("Backlog");
int todo    = data->AddColumn("To Do");
int doing   = data->AddColumn("In Progress", /*wipLimit=*/3);
int testing = data->AddColumn("Testing",     /*wipLimit=*/2);
int done    = data->AddColumn("Done");

int features = data->AddLane("New features");      // optional swimlanes

int card = data->AddCard(todo, "Shopping cart is missing size");
data->SetCardDescription(card, "Steps to reproduce …");
data->SetCardPriority(card, KanbanPriority::High);
data->SetCardDueDate(card, GanttDate(2026, 8, 14));
data->SetCardAssignee(card, "Iris Chen");
data->AddCardTag(card, "bug");

data->MoveCard(card, doing);                        // records a history entry

auto board = CreateKanbanBoardWithData("board1", 10, 10, 1000, 600,
                                       data, KanbanDesign::StickyNotes);
```

Key decisions:

* **Column identity vs. position.** Columns have stable ids; display order is
  separate, so reordering columns does not orphan history.
* **`MoveCard` is the single mutation path for state changes** and appends a
  `(cardId, fromColumn, toColumn, date)` history entry. The history is what
  makes lead/cycle time and the CFD derivable *for free* — the board and the
  CFD chart share one `KanbanDataSource`.
* **Sub-columns** (split "Doing/Done") are modelled as child columns sharing
  the parent's header and WIP limit — same parent/child approach as Gantt
  summary tasks.
* **Swimlanes are optional**: with no lanes the board renders as images 1–3;
  with lanes it renders as image 5.

### 4.2 Rendering

One element, column layout computed from style metrics; each column lays out
its cards by a per-style `CardLayout` (`Stack` — strict vertical list;
`Stagger` — alternating offset, images 1–2; `Grid` — image 3). Vertical
scrolling per column when cards overflow; horizontal scrolling of the board
when columns overflow. All drawing through `IRenderContext` (rounded rects,
shadows, paths for the dog-ear fold, text measurement for card wrapping).

### 4.3 Style & designs

Everything visual lives in `KanbanBoardStyle`; `ApplyDesign()` swaps presets
mirroring the reference images:

| `KanbanDesign` | Look | Source |
|---|---|---|
| `StickyNotes` | White tracks, slim colored headers, pastel offset sticky notes | Image 1 |
| `Poster` | Solid colored panels, pill headers with circle, dog-ear cards, title + priority legend | Image 2 |
| `Schematic` | Gray panel, dashed separators, icon-only color-block cards | Image 3 |
| `Timeline` | Status-colored pill cards, swimlane row tabs, footer caption strip | Image 5 |
| `Professional` | Project-tool look: flat columns, full-field cards, WIP badges, avatars | tool convention |
| `Dark` | Dark variant of `Professional` | theme parity with Gantt |

---

## 5. Proposed feature list

Grouped, with a suggested delivery phase. **P1** = core board, ship first;
**P2** = completes the reference images + analytics; **P3** = polish /
advanced.

### 5.1 Data model
| # | Feature | Phase |
|---|---|---|
| D1 | Columns: add/remove/rename/reorder, stable ids, per-column color | P1 |
| D2 | Cards: title, description, priority, due date, assignee, tags, explicit color override | P1 |
| D3 | `MoveCard(card, column[, index])` + ordered cards within a column | P1 |
| D4 | Per-column **WIP limit** with over-limit state exposed to the renderer | P1 |
| D5 | State-change **history** recorded on every move (feeds metrics + CFD) | P1 |
| D6 | Swimlanes: add/remove/reorder, card ↔ lane assignment | P2 |
| D7 | Sub-columns (split *Doing / Done*), WIP limit shared with parent | P2 |
| D8 | Commitment / delivery point designation (which boundaries lead time measures) | P2 |
| D9 | Card checklist (n-of-m done), shown as a progress chip | P3 |
| D10 | Card cover image / icon (`UCPixmap`), icon-only mode for `Schematic` | P2 |
| D11 | Card blocked flag + blocked reason (rendered as a badge) | P2 |
| D12 | Board save/load as JSON via `UltraCanvasJSON` (columns, lanes, cards, history) | P2 |
| D13 | Change callbacks: `onCardMoved`, `onCardAdded`, `onCardRemoved`, `onWipExceeded` | P1 |

### 5.2 Board layout & chrome
| # | Feature | Phase |
|---|---|---|
| B1 | Column headers: text, card count, WIP badge (`3/3`), per-column color | P1 |
| B2 | Header shapes: flat bar (image 1), pill + connector circle (image 2), plain row over separator line (image 3) | P2 |
| B3 | Column body: solid panel (image 2), track on background (image 1), dashed separators only (image 3) | P2 |
| B4 | Card layouts per style: `Stack`, `Stagger`, `Grid` | P1 (`Stack`) / P2 (rest) |
| B5 | Per-column vertical scrolling; board horizontal scrolling; wheel conventions as in Gantt | P1 |
| B6 | Board title + optional legend (priority → color, as in image 2) | P2 |
| B7 | Swimlane rendering: row header tabs, lane separators, per-lane collapse | P2 |
| B8 | Column footer captions (the Q1–Q4 strip of image 5) | P2 |
| B9 | Collapsed columns (rotated title strip) for wide boards | P3 |
| B10 | `FitColumns()` — equal-width columns filling the element vs. fixed column width + scroll | P1 |

### 5.3 Cards
| # | Feature | Phase |
|---|---|---|
| C1 | Title + wrapped description with per-style max lines and ellipsis | P1 |
| C2 | Priority dot / chip, position settable (corner dot — image 2; chip row) | P1 |
| C3 | Due date field with label, **overdue highlighting** (needs today, `SetToday` as in Gantt) | P1 |
| C4 | Assignee display: initials avatar circle, or text | P2 |
| C5 | Tag chips with auto colors | P2 |
| C6 | Card visuals: rounded rect, sticky note, dog-ear fold + shadow, one-line pill | P1 (rect) / P2 (rest) |
| C7 | Card color source mode: `ByCard` (image 1), `Fixed` white (image 2), `ByColumn` (image 5), `ByLane`, `ByPriority` | P1 |
| C8 | Blocked badge, checklist chip, attachment/comment count chips | P3 |
| C9 | Compact ↔ full card detail level, switchable per style (and automatically when column is narrow) | P2 |

### 5.4 Style & designs
| # | Feature | Phase |
|---|---|---|
| S1 | `KanbanBoardStyle` with every metric/color/font knob; `SetStyle` / `EditStyle()+StyleChanged()` as in Gantt | P1 |
| S2 | Design presets: `Professional`, `StickyNotes` | P1 |
| S3 | Design presets: `Poster`, `Schematic`, `Timeline`, `Dark` | P2 |
| S4 | Column color palettes (reuse the Gantt palette concept: `CorporateBlue`, `Pastel`, `Vibrant`, … + custom list) | P1 |
| S5 | WIP-limit-exceeded styling: header tint, badge color, optional column outline | P1 |
| S6 | Priority color mapping settable (image 2's Overdue/High/Low legend) | P2 |

### 5.5 Interaction
| # | Feature | Phase |
|---|---|---|
| I1 | Hover tooltip with full card details (Gantt tooltip conventions) | P1 |
| I2 | Click to select; `onCardClick`, `onCardDoubleClick`, `onColumnClick` callbacks | P1 |
| I3 | **Drag & drop**: card between/within columns, ghost card + insertion indicator, auto-scroll at edges; reuses the FlowChart drag pattern; emits `MoveCard` so history stays correct | P1 |
| I4 | Drag rejection rule hook (`canDropCard(card, column)` → e.g. enforce WIP limit strictly) | P2 |
| I5 | Keyboard: arrows move selection, Enter activates, Ctrl+arrows move the selected card | P3 |
| I6 | Column drag-reorder with the mouse | P3 |
| I7 | In-place card title editing (double-click → text field overlay) | P3 |
| I8 | Programmatic API: `ScrollToCard`, `SelectCard`, `HighlightColumn` | P2 |

### 5.6 Metrics & analytics (from the recorded history)
| # | Feature | Phase |
|---|---|---|
| M1 | Per-card lead time & cycle time (commitment/delivery points from D8) | P2 |
| M2 | Per-column current WIP, average age of cards in column ("aging WIP" dot on cards) | P2 |
| M3 | Throughput per period | P2 |
| M4 | `GetStageCounts(date)` / per-day stage-count series — the CFD input (§6) | P2 |
| M5 | Board summary struct for host apps (counts, overdue count, WIP violations) | P2 |

---

## 6. The cumulative flow chart (image 4) — separate element, shared data

**Recommendation: a separate `UltraCanvasCumulativeFlowChart` element in the
same delivery, not a mode of the board.**

Reasoning:

* It is a **different chart type** — a stacked area chart over time with a
  value axis, time axis, legend and annotations. Nothing of the board's
  layout (columns, cards, drag & drop) applies; forcing it into the board
  element would couple two unrelated renderers.
* It is a **first-class chart** in its own right: users may want to feed it
  from external data (a Jira export) without instantiating a board. As a
  `UltraCanvasChartElementBase` subclass it inherits axes, grid, tooltips,
  zoom and the empty state, exactly like the other charts.
* The two stay **related through the data source**: `KanbanDataSource`
  records history (D5), and the CFD element accepts either a
  `KanbanDataSource` directly (it derives the per-stage daily counts via M4)
  or a raw `stage × date → count` series. One data source, two views — the
  same relationship Gantt has to its `GanttDataSource`.

Feature list for the companion element:

| # | Feature | Phase |
|---|---|---|
| F1 | Stacked bands from per-stage time series, ordered Done at bottom → To Do on top (the CFD convention) | P2 |
| F2 | Direct binding to a `KanbanDataSource` (auto-derive daily counts from history) | P2 |
| F3 | Manual series input (`AddStage(name, color, values)`) for external data | P2 |
| F4 | Band edge line + marker dots per period, as in image 4 | P2 |
| F5 | Labelled span annotations: horizontal arrow between two band edges (**Lead Time**, **Cycle Time**), vertical arrow across a band (**WIP Limit**) with boxed captions | P2 |
| F6 | Auto-computed annotation placement for the standard three, plus custom annotations | P3 |
| F7 | Side legend with stage swatches; theme/dark support | P2 |
| F8 | Hover read-out: date + per-stage counts + derived approximate cycle time | P3 |

Phase note: the CFD ships in P2 together with the metrics (M1–M4), because it
is meaningless before the board records history.

---

## 7. Suggested delivery phases

1. **P1 — the working board.** Data model (D1–D5, D13), stack layout, column
   headers with WIP badges, rounded-rect cards with title/description/
   priority/due date, drag & drop, tooltips, `Professional` + `StickyNotes`
   designs, palettes, demo + doc.
2. **P2 — the reference images + analytics.** Remaining designs (`Poster`,
   `Schematic`, `Timeline`, `Dark`), swimlanes, sub-columns, stagger/grid
   layouts, legend/title/footers, avatars/tags, JSON persistence, metrics
   M1–M5, and `UltraCanvasCumulativeFlowChart` (F1–F5, F7).
3. **P3 — polish.** Keyboard interaction, in-place editing, column reorder,
   collapsed columns, checklists/attachment chips, CFD hover read-out and
   custom annotations.

---

## 8. Open questions

1. **Where does editing stop?** The proposal includes in-place *title*
   editing (I7) but not a full card editor dialog — host applications will
   have their own forms. Is a built-in modal card editor wanted as a P3
   extra?
2. **Plugin directory**: `Plugins/Charts/` beside the Gantt (recommended, as
   both share `UltraCanvasCalendarDate.h` and the architecture), or
   `Plugins/Diagrams/`? The name says "diagram", the behaviour says
   "interactive project chart".
3. **Persistence format**: is board JSON (D12) enough, or should the
   FileLoader facade get a board file type registered so boards open via the
   universal load/save path?
