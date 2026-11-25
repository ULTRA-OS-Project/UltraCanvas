# UltraOffice

## Overview

UltraOffice keeps text documents, spreadsheets and presentations in one
window. It ships as `Apps/UltraOffice` and builds the program `UltraOffice`
(`-DBUILD_ULTRAOFFICE=ON`, the default). Its version is the first line of
`Docs/UltraOffice/CHANGELOG.md` (`UltraOffice --version`).

It is built from the framework's elements, one per kind of document:

| Kind | Element | Opens | Saves |
|---|---|---|---|
| Document | [`UltraCanvasRichTextEdit`](../UltraCanvas/UltraCanvasRichTextEdit.md), in page view | .odt, .docx, .doc, LaTeX, Markdown, text | .docx, .odt |
| Spreadsheet | [`UltraCanvasSpreadsheet`](../UltraCanvas/UltraCanvasSpreadsheetExamples.md) | .ods, .xlsx, .xls, .csv, .tsv | .xlsx, .ods, .csv, .tsv |
| Presentation | [`UltraCanvasSlideEdit`](../UltraCanvas/UltraCanvasSlideEdit.md) | .pptx, .odp and their template and show variants | .pptx, .odp |

What a file is comes from `UltraCanvasFileLoader::DetectOfficeDocumentKind`:
the extension, else the package inside (`mimetype`, `word/document.xml`,
`xl/workbook.xml`, `ppt/presentation.xml`). The design, and the phases still
to come (the slide editor, charts, comments and track changes), are in
[`Docs/Research/UltraOfficeDesignProposal.md`](../Research/UltraOfficeDesignProposal.md).

## The window

| Part | What it does |
|---|---|
| The tabs | **Start**, then one tab per open file; the icon says its kind, a dot marks unsaved changes, **+** opens the File menu |
| The command row | **File** (the menu below), **Undo**, **Redo**, **Save**, then the command tabs of the file's kind |
| The toolbar | The tools of the command tab chosen; tools that do not fit go behind a chevron menu |
| The status line | Where the caret is (documents), the cell and the sum, average and count of the selection (spreadsheets), the number of slides (presentations) |

The command tabs:

- **Documents** - *Home*: cut, copy, paste, paragraph style, font, size,
  bold, italic, underline, strikethrough, bullets, numbering, indent,
  alignment. *Insert*: table, picture, link, page break, footnote, contents.
  *View*: pages on or off, page width, whole page, zoom.
- **Spreadsheets** - *Home*: cut, copy, paste, bold, italic, alignment,
  wrap, number format, decimals. *Insert*: *Chart* opens the chart gallery
  over the selected cells - every chart of the framework, 52 types in nine
  categories (column and bar, line and area, pie and circular, scatter and
  bubble, statistical, financial, flow and hierarchy, matrix and maps,
  other), each drawn from the selection, with what it shows and the
  columns it reads; *Chart type* changes the selected chart's type in the
  same gallery; the selected chart's properties (type, title, legend,
  series in rows) and deleting it. *Data*: sort, filter, fill
  down and right, SUM, conditional formatting (value and formula rules,
  text, blanks, errors, top values, averages, duplicates, colour scales,
  data bars) and data validation (numbers, dates, times, text length, a list
  typed or taken from cells, a custom formula; how a wrong entry is
  answered). *Review*: the active cell's comment (add, change, delete) and
  link (a web or mail address, or a cell of the workbook; open, remove).
  *Page*: set and clear the print area, landscape, fit to width, gridlines,
  headings. *View*: freeze and unfreeze panes.

  A chart stands over the cells, anchored to them: a click selects it, a
  drag moves it, its handles resize it, Delete removes it and a
  double-click opens its properties. It reads its cells again whenever they
  change, is saved in .xlsx and .ods, and prints where it stands
  ([Charts](../UltraCanvas/UltraCanvasSpreadsheetExamples.md#charts)). The
  gallery picks with a click or the arrow keys and Enter; a type the
  selection does not fit shows a sample and says why ("A bubble chart needs
  three columns of numbers: x, y and size."). A type Excel and LibreOffice
  have no form for - a Sankey diagram, a Gantt chart, a funnel, a heatmap -
  is saved as a column chart of its numbers, which they show, with the
  real type beside it for UltraOffice to read back
  ([Every chart type](../UltraCanvas/UltraCanvasSpreadsheetExamples.md#every-chart-type)).

  A comment is marked by a red corner and shown while the pointer rests on
  the cell; a link is blue and underlined, and Ctrl+click opens it.
  Conditional formats, validation rules, comments and links are kept in
  .xlsx and .ods, and printed and exported as PDF as shown
  ([Conditional formats](../UltraCanvas/UltraCanvasSpreadsheetExamples.md#conditional-formats)).
- **Presentations** - *Home*: new slide (Ctrl+M) or one from the deck's
  layouts, duplicate, delete and hide a slide, present. *Insert*: text box,
  drawn shape (rectangle, rounded rectangle, ellipse, triangle, arrow, star),
  picture. *Arrange*: bring to front, forward, backward, to back; duplicate
  and delete shapes. *Text*: bold, italic, underline, strikethrough, font,
  size, larger and smaller, colour, alignment, bullets, numbering, list
  level - on the text being edited, or all the text of the selected shapes.
  *Table*: insert a table, rows above and below, columns left and right,
  delete a row or a column. *Slide show*: from the beginning or from this
  slide.

  The slide strip shows every slide (drag one to move it); on the slide a
  click selects a shape, a band several, and they are moved, resized and
  turned with their handles, snapping to the slide and to each other;
  double-click a shape (or Enter) to edit its text in place, formatted, with
  bullets and numbering - a table's cell likewise - Ctrl+Enter or a click
  elsewhere to keep it, Escape to drop it. The show runs full screen
  with the slides' transitions and timings; on a second monitor the
  presenter view shows the next slide, the notes and the time. A click or
  the arrows move on, a number and Enter jump, B or W blank the screen,
  Escape ends it.

The **Start** tab opens a file, lists the recent ones and starts a new
document or spreadsheet.

## The File menu

| Item | Keys |
|---|---|
| New document | Ctrl+N |
| New spreadsheet | Ctrl+Shift+N |
| New presentation | |
| Open... | Ctrl+O |
| Open recent | |
| Save | Ctrl+S |
| Save as... | Ctrl+Shift+S |
| Export as PDF... | every kind |
| Print... | Ctrl+P, every kind |
| Close | Ctrl+W |
| New files are saved as | Office Open XML (.docx, .xlsx), the default, or OpenDocument (.odt, .ods) |
| Quit | Ctrl+Q |

Save writes a file where it came from in its own format; a file only read
(.doc, .xls, Markdown, LaTeX, text) asks for a name. A file
opened with a password is saved protected with it again. A .docx saved again
keeps the theme and font table it came with (`UCRichDocument::keptParts`). Closing a tab or
the window with unsaved changes asks first.

## Autosave and recovery

Every minute UltraOffice copies each document with unsaved changes into
`UltraOffice/recovery/` in the UltraCanvas settings folder: the copy in the
document's own type where it can be written back (else the type new files are saved as),
protected with the document's password when it has one, and a note of its
kind, file and name beside it. A copy is written under another name first
and then moved over the last one, so a crash while writing leaves the
earlier copy whole. Saving the document, closing its tab or quitting
UltraOffice removes its copy.

When UltraOffice ends without closing, the copies stay, and the next start
lists those documents on the **Start** tab, which shows their count, without
stopping anything else: **Restore** opens each in a tab under its own file
and name, with its changes still to save; **Discard the copies** deletes
them, after asking.

## Starting it

```bash
UltraOffice
```

```bash
UltraOffice report.docx budget.ods talk.pptx
```

One copy runs per user: a second start hands its files to the window
already open, and a file already open is brought forward rather than opened
twice.

## Settings

`UltraOffice/settings.json` in the UltraCanvas settings folder
(`UltraCanvasAppSettings`) keeps the recent files
(`recent`, twelve) and the format new files are saved as (`saveFormat`:
`ooxml` or `odf`).

## Files

| File | Role |
|---|---|
| `Apps/UltraOffice/main.cpp` | the command line, one copy per user |
| `Apps/UltraOffice/UltraOfficeWindow.h`, `.cpp` | the window: tabs, command row, File menu, Start tab, open, save, close, autosave and recovery |
| `Apps/UltraOffice/UltraOfficeHost.h`, `.cpp` | what every kind of tab provides (`OfficeHost`) and the shared toolbar helpers |
| `Apps/UltraOffice/UltraOfficeDocumentHost.cpp` | documents |
| `Apps/UltraOffice/UltraOfficeSpreadsheetHost.cpp` | spreadsheets |
| `Apps/UltraOffice/UltraOfficePresentationHost.cpp` | presentations |
| `Apps/UltraOffice/UltraOffice.desktop` | the freedesktop entry |
| `media/appicon/UltraOffice.svg`, `.png` | the icon |
| `media/icons/office/` | the kind icons and the tools the shared sets lack |

## Languages

UltraOffice's texts are in `media/locale/<language>/UltraOffice.json`, in
every language the framework has, as machine drafts until someone reviews
them (`@translated-by`); the shared ones come from `Standard.json`.
