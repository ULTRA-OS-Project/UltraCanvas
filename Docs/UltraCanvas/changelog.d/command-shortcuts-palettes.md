- **`UltraCanvasCommandRegistry::FindByShortcut`** finds the command a key
  press names ("Ctrl+Shift+S", however spelt), so a program's key handler runs
  the command whose shortcut its menu shows.
- **A tool palette from a toolbar layout.** `ToolbarLayout::separateGroups`
  (on by default) can be turned off for a palette of folded groups, and
  `UltraCanvasToolbar::RefreshCommandStates` presses a folded group of check
  commands on the checked one - the active tool - and releases the others.
