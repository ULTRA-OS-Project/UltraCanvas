- **Grids: columns as wide as their text.** The browse grid, the SQL results
  and the import preview sized their columns from a character count, so a
  column of narrow glyphs was wider than it needed to be and a wide one
  still cut its text. Each column is now measured (the list view's
  `SetColumnFitting`): its widest value and its title, at most 360 px. Drag
  a border and the widths are yours.
