- **`UltraCanvasToolPanelStack`: a column of tool panels beside a document**
  (`UltraCanvasToolPanelStack.h`), the framework half of
  `Docs/Research/UltraOfficeToolPanelsProposal.md`. One collapsible
  `UltraCanvasGroupBox` per topic in a scrolling column, 286 px wide on the
  ground, white panels with a header strip, 6 px corners and gaps.
  - **Context panels** (`ToolPanelOptions::context`, `SetPanelShown`) come in
    at the top, open, framed in the accent with an accent chip ("in a table"),
    and the panel under the pointer - or, scrolled, the first one in view -
    keeps its place on the screen.
  - **The order is the user's**: drag a header (a veil over the panel, an
    accent line where it goes, the column scrolling at its edges), *Move up* /
    *Move down* in the header's menu (right-click, Shift+F10, Menu), or
    Ctrl+Up / Ctrl+Down on a focused header; `onOrderChanged`. `GetOrder` /
    `SetOrder` (an unknown id skipped, an unnamed panel at its default place),
    `SaveState` / `RestoreState` with `ToolPanelStackState::ToString` /
    `FromString`: the order, the open panels and the rail as one line for the
    settings.
  - **Icon rail**: `SetRailMode`, or `SetAutoRailBelowWidth` for a narrow
    window - a 44 px strip of 32 px icons; a click opens the panel as a flyout
    on the window's popup stack beside it (Escape closes one level, a click
    outside closes it, another icon switches it).
  - **Keyboard**: `FocusFirst` for the host's F6, Up / Down / Home / End from
    header to header, Enter / Space open and close, Tab into an open panel,
    Ctrl+Up / Ctrl+Down. Each panel is a group named by its title, the stack
    a toolbar; `ToolPanelStackStyle::FromTheme` with the
    `tool-panel.header` / `tool-panel.context-header` roles.
- **Tool rows** (`UltraCanvasToolRows.h`), what a panel holds:
  `UltraCanvasToolButtonRow` (a wrapping row of 28 × 28 icon tools and
  toggles, "Bold (Ctrl+B)" tooltips) and `UltraCanvasToolValueRows`
  (spinner, dropdown and colour-swatch rows on a form grid, full-width list
  and font-picker rows). Every value is editable through a getter and a
  setter; `Sync()` - and `UltraCanvasToolPanelStack::Sync()` for every panel -
  shows the document's values again without calling a setter.
- **`UltraCanvasGroupBox` 1.7.0, a tool panel's header**: `SetMoreButton` /
  `onMoreClicked` (a "…" for the topic's dialog), `SetHeaderChip`,
  `SetHeaderFocusable` (Enter / Space fold the box, Shift+F10 / Menu and a
  right-click run `onHeaderContextMenu`, a focus ring), `SetHeaderDraggable`
  (`onHeaderDragStarted` / `Moved` / `Ended`; a click still folds, on the
  release); a collapsible box's accessible action opens and closes it. The
  `Header` style's strip is clipped to the frame's rounded corners - it poked
  out past them.
- **Tab skips what a hidden container holds** (`UltraCanvasWindowBase`):
  the focus went into the controls of a closed panel, which nobody could see.
- Doc `Docs/UltraCanvas/UltraCanvasToolPanelStack.md`, a demo page (*Tool
  Panel Stack*), and `ToolPanelStackElementTest` - a window test in the
  framework's virtual-display step.
