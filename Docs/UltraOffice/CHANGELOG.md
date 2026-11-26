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
