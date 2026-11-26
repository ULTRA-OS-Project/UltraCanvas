# UltraOffice

## Overview

UltraOffice keeps text documents, spreadsheets and presentations in one
window. It ships as `Apps/UltraOffice` and builds the program `UltraOffice`
(`-DBUILD_ULTRAOFFICE=ON`, the default). Its version is the first line of
`Docs/UltraOffice/CHANGELOG.md` (`UltraOffice --version`).

It is built from the framework's elements, one per kind of document:

| Kind | Element | Opens | Saves |
|---|---|---|---|
| Document | [`UltraCanvasRichTextEdit`](../UltraCanvas/UltraCanvasRichTextEdit.md), in page view | .odt, .docx, .doc, LaTeX, Markdown, text; templates .ott, .dotx, .dotm | .docx, .odt; templates .dotx, .ott |
| Spreadsheet | [`UltraCanvasSpreadsheet`](../UltraCanvas/UltraCanvasSpreadsheetExamples.md) | .ods, .xlsx, .xls, .csv, .tsv; templates .ots, .xltx, .xltm | .xlsx, .ods, .csv, .tsv; templates .xltx, .ots |
| Presentation | [`UltraCanvasSlideEdit`](../UltraCanvas/UltraCanvasSlideEdit.md) | .pptx, .odp and their show variants; PowerPoint 97-2003 .ppt, .pps; templates .potx, .potm, .otp, .pot | .pptx, .odp; templates .potx, .otp |

What a file is comes from `UltraCanvasFileLoader::DetectOfficeDocumentKind`:
the extension, else the package inside (`mimetype`, `word/document.xml`,
`xl/workbook.xml`, `ppt/presentation.xml`; a compound file with a "PowerPoint
Document" stream is a presentation,
[UltraCanvasPresentationPpt](../UltraCanvas/UltraCanvasPresentationPpt.md)).
The design it was built to, in
six phases, is
[`Docs/Research/UltraOfficeDesignProposal.md`](../Research/UltraOfficeDesignProposal.md).

## The window

| Part | What it does |
|---|---|
| The tabs | **Start**, then one tab per open file; the icon says its kind, a dot marks unsaved changes, **+** opens the File menu |
| The command row | **File** (the menu below), **Undo**, **Redo**, **Save**, then the command tabs of the file's kind |
| The toolbar | The tools of the command tab chosen, and at its right end the kind's **view group** (below), which stays whichever command tab is chosen; in a narrow window the command tab's tools go behind a chevron menu first, and the view group stays whole |
| The status line | Where the caret is (documents), the cell and the sum, average and count of the selection (spreadsheets), the number of slides (presentations) |

The view group, at the right end of the toolbar, holds how the file is
looked at - one group per kind, each tool an icon whose tooltip names it and
its key:

| Kind | The view group, left to right |
|---|---|
| Document | navigator (F5) \| pages on or off \| page width \| whole page \| text width and its share (40 to 90 %, or *Other...* typed in) \| zoom out (Ctrl+-) \| the zoom level (25 to 400 %, or typed) \| zoom in (Ctrl++) |
| Spreadsheet | freeze panes \| unfreeze \| gridlines \| zoom out \| the zoom level \| zoom in |
| Presentation | normal \| slide sorter \| notes \| fit the slide \| zoom out \| the zoom level (100 % is the slide fitting) \| zoom in |

The three fits of a document keep fitting as the window is resized: *Page
width* (the page and the desk beside it fill the width), *Whole page*, and
*Text width*, which makes the page the share of the window's width chosen in
the list beside it - 60 % unless another is chosen - centred on the desk, a
reading width on a wide screen
([`ZoomToWidthShare`](../UltraCanvas/UltraCanvasRichTextEdit.md#zoom-and-scrolling-sideways)).
The fit chosen last and the share are saved, and every document tab opened
after - the next run's too - starts with them; a zoom of its own (the zoom
tools, Ctrl+wheel) is the tab's alone. Ctrl+0 is 100 % (the fitted slide in a
presentation). The icons are `media/icons/menu/` (navigator, zoom) and
`media/icons/office/` (the rest), masks in the toolbar's ink. The view tools
were each kind's *View* command tab before; everything that tab held is in
the group, so the tab is gone.

The command tabs:

- **Documents** - *Home*: cut, copy, paste, paragraph style, font, size,
  bold, italic, underline, strikethrough, bullets, numbering, indent,
  alignment, *Paragraph* (indents, space before and after, line spacing,
  alignment, tab stops) and *Styles* (apply a named style, change one, or
  make a new paragraph style from the selection). *Insert*: table, picture,
  link, page break, *Footnote* (a footnote or an endnote, with its text or
  opened for typing), *Contents* (headings to a level; a document that has
  contents rebuilds them), *Caption* (Figure, Table or Equation, numbered),
  *Cross-reference* (to a caption or a bookmark, its text or its page).
  *Layout*: *Page setup* (paper, orientation, size, margins, header and
  footer distance), *Table* (width, alignment, indent, header row, fitting
  the contents, borders and their colour, cell padding, the cells' vertical
  alignment) and *Picture* (alt text, wrapping, position, size). *Review*:
  a new comment, the previous and next comment, *All comments* (go to, edit,
  resolve or reopen, delete), show or hide comments (and their pane); track
  changes, the previous and next change, accept, reject, accept all, reject
  all.

  The dialogs are the framework's
  ([Document dialogs](../UltraCanvas/UltraCanvasRichTextEdit.md#document-dialogs)),
  shared with UltraTexter. What each OK changes is one undo step - the page
  setup too - and Cancel changes nothing. Lengths are in centimetres,
  spacing in points. Comments and changes are signed with the name in the
  Settings window, the login name until one is given; a double-click on a
  comment in the pane edits it.

  **The navigator** is a side panel at the left of the document, shown and
  hidden with the view group's navigator tool or **F5** (LibreOffice Writer's key; Word's
  Ctrl+F6 moves between windows, so it was not taken). Drag the bar between
  it and the page to make it wider; the choice to show it and its width are
  kept in the settings and apply to every document tab. It is the framework's
  [`UltraCanvasDocumentNavigator`](../UltraCanvas/UltraCanvasDocumentNavigator.md),
  shared with UltraTexter:
  - **Headings** as a tree by level. The heading the caret is under is the
    selected row and follows the caret; a click goes to a heading. The
    buttons above the tree promote and demote a heading with its
    subheadings, and move its chapter (the heading, its text and its
    subheadings) up or down past the heading of its level beside it;
    dragging a heading's row onto another heading moves the chapter before
    that one, onto the *Headings* row to the end. Each is one undo step.
  - **Tables, Pictures, Charts** (by caption, alt text or title), **Comments**
    (with their authors; a double-click opens *All comments*), **Bookmarks**,
    **Footnotes and endnotes** (a double-click opens the note) and **Tracked
    changes**; a click goes there and selects the picture, chart, comment or
    change.
  - **The search box** filters every list and lists under *Found in the text*
    each place the document says it; a click selects it.

  The lists follow the document as it is edited, read again once the typing
  pauses.
- **Spreadsheets** - *Home*: cut, copy, paste, bold, italic, alignment,
  wrap, number format, decimals. *Insert*: *Chart* opens the chart gallery
  over the selected cells - every chart of the framework, 52 types in nine
  categories (column and bar, line and area, pie and circular, scatter and
  bubble, statistical, financial, flow and hierarchy, matrix and maps,
  other), each drawn from the selection, with what it shows and the
  columns it reads; *Chart type* changes the selected chart's type in the
  same gallery; the selected chart's properties (type, title, legend,
  series in rows) and deleting it; *Pivot table* summarises the selected
  cells in a pivot table. *Data*: sort, filter, fill
  down and right, SUM, conditional formatting (value and formula rules,
  text, blanks, errors, top values, averages, duplicates, colour scales,
  data bars) and data validation (numbers, dates, times, text length, a list
  typed or taken from cells, a custom formula; how a wrong entry is
  answered); for the pivot table under the active cell *Refresh* (every
  pivot table when none is), *Change source*, *Field list* and *Delete
  pivot*. *Review*: the active cell's comment (add, change, delete) and
  link (a web or mail address, or a cell of the workbook; open, remove).
  *Page*: set and clear the print area, landscape, fit to width, gridlines,
  headings.

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

  A pivot table summarises a block of cells whose first row names its
  fields. *Insert > Pivot table* opens its field list in a dialog: the data
  (the selection, or a range typed in), where it goes (a new sheet, or a
  cell of this sheet), and the fields - pick one and add it to the rows,
  columns, values or filters (a double-click puts numbers into the values
  and anything else into the rows), remove it or move it up and down. A
  value field is summed, counted, averaged, taken as its min or max,
  multiplied, its numbers counted, its standard deviation or variance taken,
  and shown as it is or as a share of the grand total, its row or its
  column; a row, column or filter field has its order, the items it shows,
  its subtotals and a grouping - dates by year, quarter or month, numbers
  by a step. The grand totals at the bottom and the right can be turned
  off, and several values put one under the other. The pivot's cells are
  drawn with a header style and cannot be typed over; while one of them is
  active, the field list stands beside the sheet and each change in it is
  applied at once. When the source cells change the pivot follows, a
  double-click on a value opens a new sheet of the rows behind it, and
  pivots are kept in .xlsx and .ods - those Excel and LibreOffice made are
  read and refreshed too
  ([Pivot tables](../UltraCanvas/UltraCanvasSpreadsheetExamples.md#pivot-tables)).

  A comment is marked by a red corner and shown while the pointer rests on
  the cell; a link is blue and underlined, and Ctrl+click opens it.
  Conditional formats, validation rules, comments and links are kept in
  .xlsx and .ods, and printed and exported as PDF as shown
  ([Conditional formats](../UltraCanvas/UltraCanvasSpreadsheetExamples.md#conditional-formats)).
- **Presentations** - *Home*: new slide (Ctrl+M) or one from the deck's
  layouts, duplicate, delete and hide a slide, present. *Insert*: text box,
  drawn shape (rectangle, rounded rectangle, ellipse, triangle, arrow, star),
  picture. *Arrange*: group and ungroup (Ctrl+G, Ctrl+Shift+G); align the
  left edges, centres, right edges, tops, middles or bottoms (to each other,
  or to the slide when one shape is selected), distribute across or down;
  bring to front, forward, backward, to back; duplicate and delete shapes.
  *Format*: crop a picture with its handles, reset the crop; a chart's data.
  *Text*: bold, italic, underline, strikethrough, font,
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
  elsewhere to keep it, Escape to drop it. A group is selected, moved,
  resized and turned as one shape; a double-click selects a shape inside
  it, Escape the group again. Groups are kept in .pptx (`p:grpSp`) and .odp
  (`draw:g`), a crop as `a:srcRect` and `fo:clip`. A double-click on a chart
  (from the file, or pasted from a spreadsheet) opens its data in a small
  spreadsheet; OK puts it into the chart, its kind kept, as one undo step.
  Under the slide the notes pane holds the speaker notes, formatted, with a
  splitter to size it; what is typed goes into the slide (one undo step) on
  leaving the pane or the slide. The slide sorter shows every slide in a
  grid: Ctrl+click and Shift+click choose several, a drag moves them, a
  right-click offers a new slide, duplicate, hide and delete, and a
  double-click edits a slide. A right-click on the slide offers cut, copy,
  paste, group, the order, crop, the chart's data and delete. The show runs full screen
  with the slides' transitions and timings; on a second monitor the
  presenter view shows the next slide, the notes and the time. A click or
  the arrows move on, a number and Enter jump, B or W blank the screen,
  Escape ends it.

The **Start** tab opens a file, lists the recent ones, starts a new
document, spreadsheet or presentation, shows the templates (below), and has
a *Settings...* button with a gear.

## New from template

A template is an ordinary .ott / .dotx (documents), .ots / .xltx
(spreadsheets) or .otp / .potx (presentations) file: the same package as the
document, under the template's media type
(`UltraCanvasFileLoader::IsOfficeTemplate`). Opening one - from the gallery,
the File menu, the Open dialog, the recent list or the command line - starts
a **new, untitled document** with its content ("Document 3"), so Save asks
for a name and the template itself is never written over.

The **Start** tab's *New from template* gallery shows a card per template,
with a picture of its first page, its sheet or its first slide (drawn by the
framework's file previews a moment after the tab shows), and *Open a template
file* takes one from anywhere. The File menu has the same list under **New
from template...**, with **Other template...** at its end.

The templates come from two folders:

| Folder | What |
|---|---|
| `media/templates/office/` (in the resources, installed with the rest of `media/`) | the shipped set: a letter, a report with headings and a memo; a budget, an invoice and a simple list; a plain and a dark presentation |
| `UltraOffice/templates/` in the UltraCanvas settings folder | the user's own: **Save as template...** proposes this folder |

**Save as template...** writes the open document as a template of its kind,
in the format new files are saved as (.dotx / .xltx / .potx, or .ott / .ots
/ .otp; the dialog offers both). The document keeps its own file, name and
unsaved changes; the gallery shows the new template at once.

The shipped set is made by the framework's own writers:
`Tests/OfficeTemplatesTest.cpp` holds each template, and

```bash
build/bin/OfficeTemplatesTest --write media/templates/office
```

writes them again after one is changed there. Without `--write` the test
(a ctest entry) checks that each shipped file is a template of its kind,
loads, and still holds what the test makes of it.

## The File menu

| Item | Keys |
|---|---|
| New document | Ctrl+N |
| New spreadsheet | Ctrl+Shift+N |
| New presentation | |
| New from template... | the templates, then *Other template...* |
| Open... | Ctrl+O |
| Open recent | |
| Save | Ctrl+S |
| Save as... | Ctrl+Shift+S |
| Save as template... | into `UltraOffice/templates/` |
| Export as PDF... | every kind |
| Print... | Ctrl+P, every kind |
| Close | Ctrl+W |
| New files are saved as | Office Open XML (.docx, .xlsx), the default, or OpenDocument (.odt, .ods) |
| Settings... | Ctrl+, (below) |
| Quit | Ctrl+Q |

Save writes a file where it came from in its own format; a file only read
(.doc, .xls, .ppt / .pps / .pot, Markdown, LaTeX, text) asks for a name,
and is saved as the default type of its kind. A file
opened with a password is saved protected with it again. A .docx saved again
keeps the theme and font table it came with (`UCRichDocument::keptParts`). Closing a tab or
the window with unsaved changes asks first.

## Autosave and recovery

Every minute - or every 2 to 60, as the Settings window says - UltraOffice
copies each document with unsaved changes into
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

**Autosave**, off unless the Settings window turns it on, saves each document
that has a file of a type UltraOffice writes back into that file at the same
interval; a new, untitled one or a read-only type (.doc, Markdown ...) keeps
its recovery copy instead. With *Keep recovery copies* off, no copies are
written and those there are removed.

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

**File > Settings...** (Ctrl+,), or the gear on the Start tab, opens the
Settings window - the framework's settings window
([`UltraCanvasSettingsPageKit`](../UltraCanvas/UltraCanvasSettingsPageKit.md)),
as UltraFiler's and UltraSnip's are: the pages in a list at the left, the
page, *Restore defaults* for that page and *Close*. A change is saved at once,
and each page says when it applies.

| Page | Setting | What it does | Applies |
|---|---|---|---|
| General | Your name | signs comments and tracked changes; empty: the login name | at once, every open document |
| | Language | *System language* (the one UOS-Settings sets for every application) or one of the languages there are catalogues for, for UltraOffice alone | at the next start (`ULTRACANVAS_LANG` still wins) |
| | Look | as the desktop, light or dark (`ThemePolicy`) | at once |
| | Recent files kept | none, 4, 8 (the default), 12, 20 or 30; *Clear recent files* empties the list | at once |
| | Reopen the files open at the last close | off by default; a start with files of its own opens those | the next start |
| Saving | New files are saved as | Office Open XML or OpenDocument (as the File menu's choice, also *Save as template*'s type) | at once |
| | Autosave documents into their own files | each document with a file it can write saved into it | at once |
| | Keep recovery copies of unsaved changes | the copies of *Autosave and recovery* (on by default) | at once |
| | How often | every 1 (the default), 2, 5, 10, 15, 30 or 60 minutes, for both | at once |
| Documents | Font, Font size | a new document's text (the default font, 12 points); written as the document's *Normal* style, so other programs show it too | new documents |
| | Page | A4 (2 cm margins) or Letter (one-inch margins) | new documents |
| | Show the navigator | as F5 does | at once, every document tab |
| | Zoom | page width, whole page, text width or 100 %; the view group's last choice is the same setting | at once, every document tab |
| | Text width fills | 40 to 90 % of the window's width (60 %) | at once |
| Spreadsheets | Show the gridlines | on screen (printing them is the *Page* tab's) | at once, every spreadsheet; the view group's tool turns them on and off for one |
| Presentations | Slide size | 16:9 (13.33 x 7.5 in) or 4:3 (10 x 7.5 in) | new presentations |
| | Start the show on the second monitor | with two monitors, the show there and the presenter view on this one; off: the other way round (`StartSlideShow`'s `showOnSecondScreen`) | the next show |
| | Show the presenter view | the next slide, the notes and the time on the other monitor | the next show |

Not offered, because nothing in UltraOffice would follow it yet: a
spreadsheet's default font (a workbook has no default cell style of its own
to set in the framework) and measurement units (the document dialogs measure
in centimetres and points, fixed).

Everything is kept in `UltraOffice/settings.json` in the UltraCanvas settings
folder (`UltraCanvasAppSettings`), read and written through the model in
`UltraOfficeSettings.h`:

| Key | Value |
|---|---|
| `authorName`, `language`, `theme` | the name (empty: the login name); `system` or a tag; `system`, `light`, `dark` |
| `recent`, `recentCount` | the recent files, the last opened first; how many are kept |
| `reopenFiles`, `openFiles` | whether to reopen; the files open at the last close |
| `saveFormat` | `ooxml` or `odf` |
| `autosave`, `recoveryCopies`, `autosaveMinutes` | on or off; on or off; 1 to 60 |
| `documentFont`, `documentFontSize`, `pageSize` | a family (empty: the default); points; `a4` or `letter` |
| `documentNavigator`, `documentNavigatorWidth` | shown or not; its width in pixels |
| `documentFit`, `documentWidthShare` | `pageWidth`, `wholePage`, `widthShare` or `actual`; 0.1 to 1 |
| `sheetGridlines` | shown or not |
| `slideSize`, `showOnSecondScreen`, `presenterView` | `16:9` or `4:3`; on or off; on or off |

The user's templates are in `UltraOffice/templates/` beside it.
`Tests/UltraOfficeSettingsTest.cpp` holds the model to its defaults, ranges
and a save and load; `Tests/UltraOfficeSettingsWindowTest.cpp` opens the
window, the view groups and the fits in the real window.

## Files

| File | Role |
|---|---|
| `Apps/UltraOffice/main.cpp` | the command line, one copy per user |
| `Apps/UltraOffice/UltraOfficeWindow.h`, `.cpp` | the window: tabs, command row, File menu, Start tab, open, save, close, autosave and recovery |
| `Apps/UltraOffice/UltraOfficeSettings.h`, `.cpp` | the settings' model: every value, its default and range, by name |
| `Apps/UltraOffice/UltraOfficeSettingsDialog.h`, `.cpp` | the Settings window |
| `Apps/UltraOffice/UltraOfficeTemplates.h`, `.cpp` | the templates offered (shipped and the user's) and the Start tab's gallery |
| `Apps/UltraOffice/UltraOfficeHost.h`, `.cpp` | what every kind of tab provides (`OfficeHost`: its command tabs, its view group, the zoom keys) and the shared toolbar helpers (`UltraOfficeToolbar.h`) |
| `Apps/UltraOffice/UltraOfficeDocumentHost.cpp` | documents, and their navigator's side panel |
| `Apps/UltraOffice/UltraOfficeSpreadsheetHost.cpp` | spreadsheets |
| `Apps/UltraOffice/UltraOfficePresentationHost.cpp` | presentations |
| `Apps/UltraOffice/UltraOffice.desktop` | the freedesktop entry |
| `media/appicon/UltraOffice.svg`, `.png` | the icon |
| `media/icons/office/` | the kind icons and the tools the shared sets lack - among them the view group's: pages, page width, whole page, text width, freeze and unfreeze panes, gridlines, normal view, notes, fit the slide (24 x 24, a 2-pixel round stroke, drawn as masks) |
| `media/templates/office/` | the shipped templates, made by `Tests/OfficeTemplatesTest.cpp --write` |

## Languages

UltraOffice's texts are in `media/locale/<language>/UltraOffice.json`, in
every language the framework has, as machine drafts until someone reviews
them (`@translated-by`); the shared ones come from `Standard.json`.
