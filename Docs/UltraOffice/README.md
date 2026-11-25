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
| Presentation | [`UltraCanvasMediaViewer`](../UltraCanvas/UltraCanvasMediaViewer.md), shown only | .pptx, .odp and their template and show variants | .pptx, .odp (Save as) |

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
  wrap, number format, decimals. *Data*: sort, filter, fill down and right,
  SUM. *View*: freeze and unfreeze panes.
- **Presentations** - *Home*: present from the first slide or this one
  (F5, Shift+F5), the outline.

The **Start** tab opens a file, lists the recent ones and starts a new
document or spreadsheet.

## The File menu

| Item | Keys |
|---|---|
| New document | Ctrl+N |
| New spreadsheet | Ctrl+Shift+N |
| Open... | Ctrl+O |
| Open recent | |
| Save | Ctrl+S |
| Save as... | Ctrl+Shift+S |
| Export as PDF... | documents |
| Print... | Ctrl+P, documents |
| Close | Ctrl+W |
| New files are saved as | Office Open XML (.docx, .xlsx), the default, or OpenDocument (.odt, .ods) |
| Quit | Ctrl+Q |

Save writes a file where it came from in its own format; a file only read
(.doc, .xls, Markdown, LaTeX, text) and a presentation ask for a name. A file
opened with a password is saved protected with it again. Closing a tab or
the window with unsaved changes asks first.

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
| `Apps/UltraOffice/UltraOfficeWindow.h`, `.cpp` | the window: tabs, command row, File menu, Start tab, open, save, close |
| `Apps/UltraOffice/UltraOfficeHost.h`, `.cpp` | what every kind of tab provides (`OfficeHost`) and the shared toolbar helpers |
| `Apps/UltraOffice/UltraOfficeDocumentHost.cpp` | documents |
| `Apps/UltraOffice/UltraOfficeSpreadsheetHost.cpp` | spreadsheets |
| `Apps/UltraOffice/UltraOfficePresentationHost.cpp` | presentations |
| `Apps/UltraOffice/UltraOffice.desktop` | the freedesktop entry |
| `media/appicon/UltraOffice.svg`, `.png` | the icon |
| `media/icons/office/` | the kind icons and the tools the shared sets lack |
