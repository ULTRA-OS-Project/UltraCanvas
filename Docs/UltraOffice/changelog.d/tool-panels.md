- **The tools are in panels at the left of the document, by default.** Each
  tab's tools are a column of tool panels (`UltraCanvasToolPanelStack`)
  built from its kind's tool table (`UltraOfficePanels.h`): documents
  Clipboard, Style, Font, Paragraph, Lists, Insert, Page and Review, with
  Table and Picture while the caret is in a table or a picture is selected;
  spreadsheets Clipboard, Cell, Alignment, Number, Data, Insert and Page,
  with Chart and Pivot table for a selected chart and a pivot table's cell;
  presentations Clipboard, Slide, Insert, Transitions and Slide show, with
  Text, Arrange, Shape, Picture, Table and Chart for what is selected.
  Values are edited in place: line spacing, space before and after and the
  indents, a cell's font size, a shape's position, size, rotation and line
  width are spinners with their units, colours a row of swatches, the font
  the font picker across the panel with the size beside it, and every tool
  of an action group (the alignments, the lists) is a button of its own. A
  panel with a dialog (Paragraph, Styles, Page setup, Table properties,
  Picture, Chart properties, All comments) opens it with the "…" button in
  its header, whose help names it and the panel's keys. The panels follow
  the caret and the selection.
- **One toolbar at the top**: File, Undo, Redo and Save, and the view group
  last at its right end. The command tabs with their toolbar stay as a
  setting: Settings, General, *Tools: Panels at the left / Toolbar at the
  top*, which every open tab follows at once.
- **The panels are the user's to arrange.** A panel is dragged by its
  header to another place (or moved with its header's menu, Ctrl+Up and
  Ctrl+Down), opened and closed with a click; the order and the open panels
  are kept per kind in `settings.json` (`documentPanels`,
  `spreadsheetPanels`, `presentationPanels`) for the next start and the
  kind's other tabs, and the column's width - the splitter beside it - too
  (`documentPanelWidth` ...). Open at first: Font and Paragraph, Cell and
  Number, Slide and Text.
- **The icon rail**: in a window narrower than 1100 pixels the panels fold
  into a strip of icons, each opening its panel beside it; Settings,
  General, *Icon rail* chooses it at any width (`toolRail`).
- **The pivot table's field list is in the Pivot table panel** while the
  tools are panels (beside the grid with the toolbar).
- **F6** goes from the tab strip to the toolbar (File), the panels and the
  document, and Shift+F6 back; every shortcut works in both layouts.
- **The panels show the values in effect.** Space before and after, the
  indents and the line spacing of a paragraph that states none of its own
  show what it is laid out with - its paragraph style's (a heading's 12 pt
  before and 4 pt after), else the view's own spacing and single lines - and
  the table's Borders the grid line a table of no lines of its own is drawn
  with; "-" only for a selection of paragraphs that differ. A line spacing
  set as a height reads "exactly 14 pt" (or "at least").
- **The Page panel** has the page's orientation (Portrait, Landscape), its
  size (A3, A4, A5, Letter, Legal; Custom for one of its own), the margins
  (Narrow, Normal, Moderate, Wide; Custom opens Page setup) and the columns,
  beside Page setup as the header's "…".
- **The Lists panel** has the list style: None, bullets, dashes, check
  boxes, or numbers as 1. 2. 3., a. b. c., A. B. C., i. ii. iii. or I. II.
  III.
- **The Table panel** merges the selected cells (or a cell with the one at
  its right), merges a cell with the one below, splits a merged cell, and
  shades the cell or the selected cells (No shading takes it away).
- **The Slide panel** has the slide's background colour.
- **The Start tab says once where the tools went**: in the panels at the
  left, with Settings, where *Tools: Toolbar at the top* brings the toolbar
  back, and *Hide this note*, which `settings.json` remembers
  (`toolPanelsNoteDismissed`).
- **F6 on the tab strip shows a focus ring** round the open tab.
