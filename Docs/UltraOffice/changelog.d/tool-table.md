- **One table of tools per kind, the toolbars built from it.** Each kind of
  tab - documents, spreadsheets, presentations - describes its tools once
  (`UltraOfficeTools.h`): a stable command id, its icon, label and tooltip
  with the shortcut, its kind (button, toggle, action group, list, value,
  custom widget), the tool panel it belongs to, the command tab it is on
  today, when its panel applies (in a table, a chart selected, a shape
  selected, a pivot cell ...), and what it does and shows. The command tabs'
  bars and the view group are built from that table, with the same tools,
  order, icons, groups and widths as before, and `ToolsByPanel` gives the
  tools by panel in the default order (or a saved one) for the tool panels
  at the left (`Docs/Research/UltraOfficeToolPanelsProposal.md`, phase 1).
  The table also holds the values the panels will edit in place, each
  wired to its editor: a document's line spacing, space before and after,
  indents, text colour, highlight, superscript and subscript and its
  table's rows, columns, header row and borders; a cell's font, size, text
  and fill colour, borders, vertical alignment and merging; a slide's
  shapes' position, size, rotation, fill, outline colour and width, and a
  transition's duration. `UltraOfficeToolTableTest` checks every tool of
  the bars is in the table, each entry has a panel, the ids are unique and
  the values read back what was set.
