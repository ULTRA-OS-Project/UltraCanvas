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
- **Old PowerPoint decks open.** A PowerPoint 97-2003 presentation (.ppt,
  and the .pps show and .pot template) opens in the slide editor with its
  slides, text and bullets, shapes, pictures, tables, notes, hidden slides
  and transitions; it is shown, printed and exported to PDF like any deck.
  Saving one asks for a name and writes .pptx or .odp. A password-protected
  .ppt is refused with a reason.

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
