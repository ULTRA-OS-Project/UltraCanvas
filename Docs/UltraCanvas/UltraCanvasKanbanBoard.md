# UltraCanvasKanbanBoard

A full Kanban board element: workflow columns with WIP limits, optional
swimlanes, rich cards (priority, due date, assignee, tags, blocked state),
drag & drop, a built-in interactive card editor, a move history that powers
flow metrics, loading from Mermaid `kanban` text definitions, JSON
persistence, and a preset-based design/palette system modeled on common
real-world board styles.

- Element: `include/Plugins/Charts/UltraCanvasKanbanBoard.h` / `Plugins/Charts/UltraCanvasKanbanBoard.cpp`
- Dates: reuses `GanttDate` (serial calendar date shared with the Gantt chart)
- Demo: `Apps/DemoApp/UltraCanvasKanbanBoardExamples.cpp`
- Research & roadmap: [`UltraCanvasKanbanBoardProposal.md`](UltraCanvasKanbanBoardProposal.md)

## Quick start

```cpp
#include "Plugins/Charts/UltraCanvasKanbanBoard.h"
using namespace UltraCanvas;

auto data = std::make_shared<KanbanDataSource>();
data->SetCurrentDate(GanttDate(2026, 7, 30));   // Stamps the move history

int todo  = data->AddColumn("To Do", /*wipLimit=*/4);
int doing = data->AddColumn("In Progress", 3);
int done  = data->AddColumn("Done");

int card = data->AddCard(todo, "Shopping cart is missing size");
data->SetCardDescription(card, "Cart summary drops the size attribute");
data->SetCardPriority(card, KanbanPriority::High);
data->SetCardDueDate(card, GanttDate(2026, 8, 4));
data->SetCardAssignee(card, "Iris Chen");
data->AddCardTag(card, "bug");

auto board = CreateKanbanBoardWithData("board1", 10, 10, 1000, 600,
                                       data, KanbanDesign::Professional);
board->SetToday(GanttDate(2026, 7, 30));   // Enables overdue highlighting
board->SetEditable(true);                  // Drag & drop + built-in editor
container->AddChild(board);
```

## Data model

### Columns

`AddColumn(title, wipLimit = 0, color = Transparent)` returns a stable column
id; display order is independent (`MoveColumn`). A `wipLimit` of 0 means
unlimited; when the card count exceeds the limit the header badge turns red
and `onWipExceeded` fires on drops. Column setters: `RenameColumn`,
`SetColumnWipLimit`, `SetColumnColor` (Transparent = palette color),
`SetColumnFooterCaption` (the caption strip of the Timeline design),
`RemoveColumn` (removes its cards too).

### Swimlanes

Lanes are optional. `AddLane(title)` returns a lane id; cards join a lane via
`AddCard(column, title, laneId)` or `SetCardLane`. With lanes present the
board renders horizontal lane bands with left header tabs (see the Timeline
design), cards group by (column, lane), and drops target the lane under the
cursor. `RemoveLane` moves its cards to the default lane (-1).

### Cards

`AddCard(columnId, title, laneId = -1)` appends and records a creation
history entry. Per-card setters: `SetCardTitle`, `SetCardDescription`,
`SetCardAssignee`, `SetCardPriority` (`NoPriority`..`Urgent`),
`SetCardDueDate` / `ClearCardDueDate`, `SetCardColor`, `SetCardBlocked`
(draws a red edge stripe; the reason joins the tooltip), `SetCardTags` /
`AddCardTag`.

`MoveCard(cardId, toColumnId, index = -1)` is the single mutation path for
workflow state: it reorders instantly, records a `KanbanMoveRecord` when the
column changes, and restamps `KanbanCard::enteredColumn`.

### History & flow metrics

Every add/move/remove appends to the history (dated with
`SetCurrentDate`). On top of it:

- `GetColumnCountsAt(date)` — cards per column at the end of `date`,
  replayed from history: the cumulative-flow-diagram input. The companion
  [`UltraCanvasCumulativeFlowChart`](UltraCanvasCumulativeFlowChart.md)
  element binds to the board through it via `LoadFromKanban`.
- `CardLeadTimeDays(id)` / `CardCycleTimeDays(id)` — days between passing
  the commitment/delivery boundaries (`SetCommitmentColumn` /
  `SetDeliveryColumn`; defaults are the second and last columns). Cycle time
  starts when the card first moves *beyond* the commitment column.

## Text definitions (Mermaid `kanban`)

`LoadFromText` replaces the whole board from the Mermaid kanban syntax;
`CreateKanbanBoardFromText` wraps it into a factory:

```
kanban
  %% comments are ignored
  todo[To Do]@{ wip: 3 }
    cart[Fix cart size bug]@{ ticket: 'UC-2038', assigned: 'Iris Chen', priority: 'Very High', due: '2026-08-04' }
    [Anonymous card text]
  done[Done]
```

Indentation decides the level: the first indent depth becomes the column
level, anything deeper is a card in the current column. Card metadata keys:
`assigned`/`assignee`, `priority` (`'Very High'`, `'High'`, `'Normal'`,
`'Low'`, `'Very Low'`), `due` (YYYY-MM-DD), `ticket`/`label`/`tag` (become
tags), `desc`/`description`, `color` (#rrggbb), `blocked`. Column metadata:
`wip`, `color`, `footer`/`caption`. Errors report the line number through
the optional `error` out-parameter.

## JSON persistence

`ToJSON(pretty)` / `LoadFromJSONText`, plus `SaveToJSONFile` /
`LoadFromJSONFile` (backed by `UltraCanvasJSON`). The document round-trips
columns (with per-column card order), lanes, cards, the move history and the
commitment/delivery configuration — so saved boards keep their metrics.
Schema marker: `"type": "kanban-board", "version": 1`.

## Designs and palettes

`ApplyDesign(design)` replaces the whole style (data untouched):

| `KanbanDesign` | Look |
|---|---|
| `Professional` | Project-tool look: neutral tracks, white cards with borders and shadows, WIP badges, priority chips, tag chips, avatars |
| `StickyNotes` | Light whiteboard: slim colored headers, pastel sticky notes staggered inside neutral tracks |
| `Poster` | Poster template: solid colored column panels, pill headers with connector dots, dog-ear cards with due dates, board title + priority legend |
| `Schematic` | Presentation schematic: gray rounded board panel, dashed column separators, compact color-block cards |
| `Timeline` | Portfolio kanban: one-line pill cards colored by column, swimlane header tabs, Q1..Q4 footer caption strips |
| `Dark` | Dark variant of Professional |

`SetPalette(KanbanPalette)` swaps only the column color cycle (`Vibrant`,
`Pastel`, `CorporateBlue`, `Ocean`, `Sunset`, `Slate`, `Mono`) — or pass any
color list to `SetCustomPalette`. Card face colors follow
`style.cardColorMode`: `ByCard` (custom color or the pastel card cycle),
`Fixed`, `ByColumn`, `ByPriority`, `ByLane`.

## Style

Every visual knob lives in `KanbanBoardStyle` (see the header): board/column/
card metrics, header shapes (`Bar`, `Pill`, `PillWithDot`, `TextRow`),
column backgrounds (`Panel`, `Track`, `Separators`, `Plain`), card shapes
(`Rounded`, `StickyNote`, `DogEar`, `Pill`), card layouts (`Stack`,
`Stagger`, `Grid`), content toggles (description lines, due date + label,
assignee avatar, tags, priority dot/chip), WIP badge colors, swimlane and
footer styling, editor colors, fonts and the date format. Two workflows:

```cpp
// Replace wholesale
KanbanBoardStyle s = KanbanBoardStyles::CreatePoster();
s.cardShadow = false;
board->SetStyle(s);

// Or tweak in place
board->EditStyle().showCardDescription = false;
board->StyleChanged();
```

The board title comes from the element's `SetChartTitle` and renders when
`style.showTitle` is set; the priority legend (`style.showPriorityLegend`)
lists Overdue plus every priority actually used on the board.

## Interaction

- **Hover** shows a tooltip with the full card details; clicking selects
  (`onCardClick`, `onSelectionChanged`).
- **Wheel** scrolls the hovered column (whole board when swimlanes are on),
  Shift+wheel scrolls the board horizontally.
- **Drag & drop** (editable boards): dragging a card ~4 px starts the drag;
  a ghost follows the cursor, the target column highlights and an insertion
  line shows the drop position. Esc cancels. Drops go through
  `KanbanDataSource::MoveCard`, so history and metrics stay correct;
  `canDropCard` can veto (e.g. strict WIP enforcement), `onCardMoved` and
  `onWipExceeded` report the result.
- **Editor** (editable boards): double-click a card for the editor overlay —
  title, description (multiline), assignee, due date, tags, priority chips,
  Save/Cancel/Delete; Tab cycles fields, Enter saves, Esc cancels. Each
  column shows a dashed "+ Add card" button; "+" past the last column adds a
  column; double-clicking a column header renames it inline; Del removes the
  selected card. Programmatic access: `OpenCardEditor(id)`,
  `CloseCardEditor(save)`, `SetEditable(bool)`.
- `ScrollToCard(id)` / `SelectCard(id)` from code.

Callbacks:

```cpp
board->onCardMoved = [](int id, int from, int to) { ... };
board->onCardAdded = [](int id) { ... };          // Via the editor UI
board->onCardRemoved = [](int id) { ... };
board->onCardClick = [](int id) { ... };
board->onCardDoubleClick = [](int id) { ... };
board->onSelectionChanged = [](int id) { ... };   // -1 = cleared
board->onWipExceeded = [](int columnId) { ... };
board->onBoardChanged = []() { ... };             // Any editor mutation
board->canDropCard = [](int cardId, int toColumnId) { return true; };
```

## Factories

```cpp
CreateKanbanBoardElement(id, x, y, w, h);
CreateKanbanBoardWithData(id, x, y, w, h, data, design, title = "");
CreateKanbanBoardFromText(id, x, y, w, h, mermaidText, design, &error);
```
