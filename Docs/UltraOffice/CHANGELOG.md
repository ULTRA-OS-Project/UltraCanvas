#### 2026-11-27 *0.5.0*
- **Every installed font in the font lists, each drawn in itself.** The
  document *Home* tab's font list, the presentation *Text* tab's, the Settings
  window's font for new documents and the *Styles* dialog's font offered seven
  fixed names; they are the framework's `UltraCanvasFontPicker` now. The box
  keeps its 130 px on the bar and shows the font at the caret in itself; the
  list opens 300 px wide with *Default font*, the fonts the document (or the
  deck) uses - one this computer lacks marked *(not installed)* in grey
  italics -, the last ten fonts chosen (`recentFonts` in the settings, shared
  by every tab), and *All fonts* A to Z with an icon for each font's format
  (OpenType, TrueType, Type 1, bitmap). Symbol fonts show a few of their
  characters. A family with Light, SemiBold, Condensed or Black styles opens
  them beside the list, each drawn in itself: a style sets bold and italic as
  it is, and a weight or width they cannot say is kept by the face's own family
  name where the font has one ("Ubuntu Thin", as Word writes it). Typing jumps
  to a font by its first letters.
- **The navigator sits at the right of the document**, beside the page, where
  it no longer pushes the page away from the window's left edge.
- **The tab strip's + goes to the Start tab** - new documents of each kind,
  templates, the recent files and Open - instead of opening the File menu.
- **The view tools at the right of the toolbar, as one group, with icons.**
  Each kind's view controls stand at the toolbar's right end whichever
  command tab is chosen, and stay whole in a narrow window while the command
  tab's tools go behind their chevron: for documents the navigator, pages,
  page width, whole page, text width with its share, zoom out, the zoom level
  and zoom in; for spreadsheets freeze panes, unfreeze, gridlines and the
  zoom; for presentations normal, slide sorter, notes, fit the slide and the
  zoom. Every tool has an icon (new ones in `media/icons/office/`) and a
  tooltip with its key. The *View* command tabs, now empty, are gone.
  Ctrl++, Ctrl+- and Ctrl+0 zoom the tab that is open.
- **Text width: the page a share of the window's width.** A third fit beside
  *Page width* and *Whole page* makes the page 60 % of the window's width,
  centred, and keeps it so as the window is resized; the list beside it
  offers 40 to 90 % or a share typed in. The fit chosen last and the share
  are saved, and new tabs - the next run's too - open with them.
- **A Settings window** (File > Settings..., Ctrl+, , or the gear on the
  Start tab), on the framework's settings window: *General* - your name for
  comments and tracked changes, the language, the look (as the desktop,
  light, dark), how many recent files are kept and *Clear recent files*,
  reopening the files open at the last close; *Saving* - the format new
  files are saved as, autosave into the document's own file, recovery copies
  on or off, and how often (1 to 60 minutes; it was fixed at one); *Documents*
  - a new document's font, size and page (A4 or Letter), the navigator, the
  zoom and the text width's share; *Spreadsheets* - the gridlines;
  *Presentations* - a new deck's slide size (16:9 or 4:3), which monitor the
  show starts on and the presenter view. Each change is saved at once and
  applied at once where it can be; each page says which wait for a new
  document or the next start.
- **Every command tab fits beside the view group at 1280 px.** The style,
  font and size lists are narrower; the paragraph alignment and the lists
  (bullets, numbering, indent) of a document, and a slide's shape alignment
  and distribution, each share one slot that does the tool picked last and
  folds out the others; tools whose icon says them - Paragraph, Styles,
  table, picture, link, comments, accept, reject, sorting, filter, SUM, the
  pivot table's tools, group, ungroup - are icons, named in their tooltips,
  and an icon alone takes 34 px instead of 40.
- **Documents show a white page on a grey desk** in page view, as other word
  processors do, instead of a light grey page on white.

#### 2026-11-26 *0.4.0*
- **The navigator beside a document.** *View > Navigator*, or F5, shows a
  resizable side panel at the left of a document tab: the headings as a tree
  that follows the caret, the tables, pictures, charts, comments (with their
  authors), bookmarks, footnotes and endnotes and tracked changes, each a
  click away, and a search box that filters them and lists the places the
  document says it. Its buttons promote, demote and move a heading's chapter
  up or down, and a heading dragged onto another moves its chapter there,
  each one undo step. Whether it is shown and its width are kept in the
  settings (`documentNavigator`, `documentNavigatorWidth`). It is the
  framework's `UltraCanvasDocumentNavigator`.
- **Presentations: group, align and distribute, crop pictures, edit a
  chart's data, the slide sorter and the notes pane.** The *Arrange* tab
  groups and ungroups (Ctrl+G, Ctrl+Shift+G), aligns the left edges,
  centres, right edges, tops, middles or bottoms (to the slide when one
  shape is selected) and distributes across or down, each one undo step. A
  group is moved, resized and turned as one shape, and a double-click picks
  a shape inside it. A new *Format* tab crops the selected picture with its
  handles and resets the crop, and opens a chart's data - a chart from the
  file or one pasted from a spreadsheet - in a small spreadsheet in a
  dialog (a double-click on the chart does too); OK puts the data into the
  chart, its kind kept. A new *View* tab switches to the slide sorter, a
  grid of every slide to reorder by dragging, choose several with Ctrl and
  Shift and duplicate, hide or delete from a right-click, and shows or hides
  the notes pane under the slide, where the speaker notes are typed with
  their formatting and kept as one undo step when the pane or the slide is
  left. Groups and crops are kept in .pptx and .odp. A right-click on the
  slide offers the shape commands. Framework: `UltraCanvasSlideEdit`,
  `UCSlideDeckEditor` (the framework's changelog).
- **Pivot tables.** Insert > Pivot table summarises the selected cells: the
  field list opens in a dialog - drag nothing, pick a field and add it to
  the rows, columns, values or filters, set each value's summary (sum,
  count, average, min, max, product, count of numbers, standard deviation,
  variance) and whether it shows as a share, each field's order, items and
  grouping (dates by year, quarter or month, numbers by a step), the
  subtotals and grand totals - and puts the pivot on a new sheet or at a
  cell. Clicking into a pivot shows its field list beside the sheet, where
  every change applies at once; Data has Refresh, Change source, Field list
  and Delete pivot. A pivot's cells cannot be typed over, it follows its
  source's changes, and a double-click on a value opens a sheet of the rows
  behind it. Pivots are kept in .xlsx and .ods, and those Excel and
  LibreOffice made are read.

#### 2026-11-26 *0.3.0*
- **Every chart of the framework, in a chart gallery.** *Chart* opens a
  gallery of 52 types in nine categories - columns and bars, lines and
  areas, pies, radar and polar charts, scatter and bubbles, waterfall,
  histogram, box and whisker, stock, funnel, Sankey, chord, treemap,
  sunburst, Gantt, timeline, heatmaps, contour and surface, word cloud,
  parliament and more - each tile drawn from the selection, with what the
  type shows and the columns it reads; a type the selection does not fit
  shows a sample and says why. A click or the arrow keys pick one, Enter
  takes it; the new *Chart type* button and the properties dialog change a
  chart's type through the same gallery. A type Excel and LibreOffice have
  no form for is saved as a column chart of its numbers, which they show,
  with the real type beside it for UltraOffice to read back.
- **The document dialogs.** A document's *Home* tab opens *Paragraph*
  (indents, spacing, line spacing, alignment, tab stops) and *Styles*
  (apply, change, or make a style from the selection); *Insert* has
  *Footnote* (a footnote or an endnote, with its text), *Contents* (to a
  heading level; contents already there are rebuilt), *Caption* and
  *Cross-reference*. They are the framework's dialogs, shared with
  UltraTexter, and each OK is one undo step; Cancel changes nothing.
- **A Layout tab**: *Page setup* (paper, orientation, size, margins, header
  and footer distance), *Table* (width, alignment, header row, borders,
  padding) and *Picture* (alt text, wrapping, position, size).
- **A Review tab**: a new comment, the previous and next comment, all the
  comments in a list (go to, edit, resolve, delete), comments shown or
  hidden; track changes, the previous and next change, accept and reject
  one or all. Comments and changes are signed with the login name, and a
  double-click on a comment in the pane edits it.
- **New from template.** The Start tab shows a gallery of templates - a
  card each, with a picture of its first page, its sheet or its first slide -
  and the File menu lists them under *New from template...*, with *Other
  template...* for one anywhere else. A template opens as a new, untitled
  document ("Document 3"), whether it is picked there, opened with *Open...*,
  from the recent list or from the command line: Save asks for a name, and
  the template itself is never written over.
  - Shipped: a letter, a report with headings and a memo; a budget, an
    invoice and a simple list; a plain and a dark presentation
    (`media/templates/office/`, made by the framework's own writers through
    `Tests/OfficeTemplatesTest.cpp --write`).
  - The user's own are in `UltraOffice/templates/` in the settings folder.
- **Save as template...** (File) writes the open document as a template of
  its kind - .dotx / .ott, .xltx / .ots, .potx / .otp, in the format new files
  are saved as - into the user's templates folder. The document keeps its own
  file, name and unsaved changes, and the gallery shows the template at once.
- The Start tab scrolls when the window is shorter than it.
- A spreadsheet's *Save as* no longer offers the template types or .xls,
  which it cannot write; the templates are *Save as template*'s.
- **Copy and paste between the kinds.** Cells copied from a sheet paste
  into a document as a table and onto a slide as a table shape; a chart
  copied from a sheet (selected, then Copy) pastes into a document or onto a
  slide as a chart, and into other programs as a picture; a document's
  selection or any text pastes onto a slide as a text box, a picture as a
  picture. The presentation's Home bar has Cut, Copy and Paste. Through the
  framework's office clipboard (`UltraCanvasOfficeClipboard.h`).
- **A screen reader reads the sheet and the slides**: the grid as a table
  whose active cell is named by its address and value, the slide editor as
  the slide shown with its shapes and the slide strip.
- **Old PowerPoint decks open.** A PowerPoint 97-2003 presentation (.ppt,
  and the .pps show) opens in the slide editor with its slides, text and
  bullets, shapes, pictures, tables, notes, hidden slides and transitions;
  it is shown, printed and exported to PDF like any deck. Saving one asks
  for a name and writes .pptx or .odp. A .pot template opens as a new,
  untitled deck, like the other templates. A password-protected .ppt is
  refused with a reason.
- **Array formulas that spill.** A formula whose result is a block - FILTER,
  SORT, SORTBY, UNIQUE, SEQUENCE, RANDARRAY, TRANSPOSE, XLOOKUP returning
  rows, `{1,2;3,4}`, a range times a range - fills the cells below and to the
  right, shows #SPILL! when they are taken, and is referred to as `A1#`.
  LET, LAMBDA, MAP, REDUCE, SCAN, BYROW, BYCOL and MAKEARRAY; Ctrl+Shift+Enter
  enters a formula over the selection as a legacy array formula. Kept in
  .xlsx as dynamic arrays and in .ods as array formulas.
- **Financial functions**: NPER, RATE, IPMT, PPMT, CUMIPMT, CUMPRINC, IRR,
  XIRR, XNPV, MIRR, SLN, SYD, DB, DDB, VDB, EFFECT and NOMINAL.

#### 2026-11-25 *0.2.0*
- **Spreadsheet charts.** Charts of the selection - columns and bars (side
  by side or stacked), lines, areas, pies, doughnuts, scatter and radar
  charts - stand over the cells and follow them as they change. They are
  moved and resized with handles and deleted with Delete; a double-click
  changes their type, title and legend; every change can be undone. They
  are kept in .xlsx and .ods, and printed. A new *Insert* command tab holds
  Chart, Chart properties and Delete chart.
- **Conditional formatting and data validation** (*Data*): rules over the
  selection in the grid's own dialogs, colour scales and data bars, drawn
  on screen, on paper and in the PDF; every rule change can be undone.
- **Comments and links** (*Review*, a new command tab): a comment is marked
  by a red corner and shown on hover; Ctrl+click opens a link.
- Conditional formats, validation, comments and links are kept in .xlsx and
  .ods.
- Opening a spreadsheet from the command line no longer crashes.

#### 2026-11-25 *0.1.0*
- **UltraOffice: documents, spreadsheets and presentations in one window.**
  Every open file is a tab, marked with its kind's icon; the Start tab
  opens files, the recent ones and new documents and spreadsheets.
- **The toolbar follows the tab.** Each tab has a command row - File, Undo,
  Redo, Save and its kind's command tabs - and the toolbar of the command
  tab chosen:
  - documents: Home (styles, font, size, bold, italic, underline,
    strikethrough, lists, indent, alignment), Insert (table, picture, link,
    page break, footnote, contents), View (pages, page width, whole page,
    zoom);
  - spreadsheets: Home (clipboard, bold, italic, alignment, wrap, number
    format, decimals), Data (sort, filter, fill, SUM), Page (print area,
    landscape, fit to width, gridlines, headings), View (freeze panes);
  - presentations: Home (new slide, layouts, duplicate, delete, hide,
    present), Insert (text box, shape, picture), Text (formatting), Table,
    Arrange (order, duplicate, delete), Slide show;
- **Documents** are edited in the WYSIWYG element, in page view: .odt and
  .docx read and written, .doc, Markdown and LaTeX read. Export as PDF and
  Print.
- **Spreadsheets**: .ods and .xlsx read and written, .xls read, .csv and
  .tsv; the status line adds up the selection. Export as PDF and Print, by
  the sheet's print settings. Copy and paste reach other programs: cells go
  out as text and a table, and another program's table or text comes in.
- **Presentations** (.pptx, .odp and their kin) are edited in the slide
  editor: the slide strip, new slides from the deck's layouts, duplicate,
  move, hide and delete; text boxes, drawn shapes and pictures; shapes
  selected, moved, resized and turned with snapping, ordered, duplicated and
  deleted; a shape's text edited in place, formatted (a Text tab); tables
  inserted, their cells typed into, rows and columns added and deleted;
  every change undone. They are
  presented full screen with their transitions and timings (a presenter
  view with the next slide, the notes and the time on a second monitor),
  exported as PDF, printed and saved in place.
- New files are saved as Office Open XML (.docx, .xlsx) by default, or
  OpenDocument (*File > New files are saved as*). Protected files ask for
  their password and are saved protected again.
- **Autosave and crash recovery.** Every minute each document with
  unsaved changes is copied to the recovery folder (protected ones with
  their password); saving, closing or quitting removes the copy. When
  UltraOffice ends without closing - a crash, a power cut - the next start
  offers the documents back on the Start tab (the window does not wait on
  it), under their own files and names, with their changes still to save.
- A .docx saved again keeps its theme and font table.
- In every language UltraOffice's texts have (machine drafts, to be
  reviewed).
- One copy per user: opening a file again brings it to the window.
