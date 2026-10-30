- **Toolbars built from commands (`UltraCanvasToolbarLayout.h`).** A
  `ToolbarLayout` is a toolbar as data: groups of command ids, each group one
  function of the program, which the user can show, hide, reorder and fold,
  with single commands turned off. `MaterializeToolbar` builds the toolbar
  from a layout and the command registry menus are built from - a separator
  between two shown groups and nowhere else, a toggle for a check command, a
  widget for a widget command, one fold-out slot for a folded group, each
  item under its command id. `SaveToolbarLayout` keeps only what the user
  changed in the application's `settings.json`; `LoadToolbarLayout` merges it
  over the next release's defaults (new commands and groups appear at their
  place, dropped ones go); `ForgetToolbarLayout` is Reset. Phase 1 of the
  toolbar configuration investigation; the editor is phase 2. Doc:
  `Docs/UltraCanvas/UltraCanvasToolbarLayout.md`.
- **The command registry serves toolbars and live state.** `MenuCommand`
  gains `iconName` (resolved per size by `SetIconResolver` / `ResolveIcon`, as
  a line icon is sharp only at its own size), `tooltip`, `isEnabled`,
  `isChecked`, `dynamicLabel` and `makeToolbarWidget`. `Materialize` reads
  the state into the menu entry; `RefreshMenuStates(menu)` and the toolbar's
  `RefreshCommandStates()` read it again wherever the program's state
  changes, so it keeps no pointers to buttons the user may remove.
  `FindShortcutConflicts()` lists shortcuts given to two commands, however
  each spells them. `UltraCanvasToolbar::AddWidget` adds any widget as a
  named item.
