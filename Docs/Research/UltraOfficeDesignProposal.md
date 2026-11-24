# UltraOffice — one application for documents, spreadsheets and presentations (Investigation and Proposal)

**Date:** 2026-11-24
**Status:** Proposal — investigation only, no implementation yet
**Scope:** a new application, `Apps/UltraOffice`, that creates, opens, edits
and saves word-processing documents, spreadsheets and presentations in one
window. It is built on the WYSIWYG and document support UltraCanvas already
has, and adds the parts that are missing.

This document follows the other investigations in this directory
([`UltraCalendarDesignProposal.md`](UltraCalendarDesignProposal.md),
[`UltraFIBUDesignProposal.md`](UltraFIBUDesignProposal.md)), because like
them it asks *what belongs in the framework and what belongs in the app*.
Everything said about the repository was checked against `main` at
`12d1a744` (0.9.586). File references are to that tree. Line counts are
`wc -l` and are given only to show size. The sizes in §8 are estimates and
are labelled as such.

---

## 1. Recommendation

**Build `Apps/UltraOffice` as a thin shell: one window with document tabs, a
start page and one menu and toolbar set per document kind. Each tab hosts the
framework element for its kind:**

- **documents:** `UltraCanvasRichTextEdit`, as it is today;
- **spreadsheets:** `UltraCanvasSpreadsheet`, as it is today;
- **presentations:** a new framework element, `UltraCanvasSlideEdit`, over the
  deck model the PPTX and ODP readers already fill.

**Close the spreadsheet's file-fidelity and clipboard gaps in the framework,
not in the app. Ship in an order where the app is useful from its first
phase.**

| Decision | Reason |
|---|---|
| **One app, three document kinds, one tab strip** (§4) | This is what was asked for. The tree already has the pattern: UltraTexter's `DocumentTab` switches between `Text`, `Pdf` and `RichDocument` per tab, and swaps the element inside the tab (`Apps/Texter/UltraCanvasTextEditor.h:144-232`). UltraOffice generalises that switch to a small document-host interface. |
| **Documents need almost nothing new** (§5.1) | `UltraCanvasRichTextEdit` and the `UCRichDocument` model are about 18,400 lines. Readers and writers for ODT, DOCX and DOC add about 15,000 more. Together they cover page view, headers and footers, footnotes, tables, frames, shapes, charts, comments, tracked changes, styles, a table of contents, PDF export and printing. `WordProcessingFeatureCoverage.md` lists what is left, and none of it blocks an app. |
| **Spreadsheets need fidelity work, not a new element** (§5.2) | The grid, its model and a formula engine of about 150 functions exist (about 15,000 lines), with ODS and XLSX read and write, XLS and CSV read. The gaps are things a user notices on the first save or paste: freeze panes, hidden rows, names and filters are not saved; copy and paste stay inside the process; there are no charts and no print or PDF. |
| **Presentations need an editor** (§5.3) | Reading and writing are done: one deck model (`PptxPresentation`), PPTX and ODP readers and writers (about 5,900 lines), a slide renderer and a full-screen show in `UltraCanvasMediaViewer`. There is **no editing surface**, and the reader flattens masters and layouts into each shape. This is the one large new piece of work. |
| **The slide editor reuses the rich-text element for text in shapes** (§5.3.3) | UltraPaint's text layers already edit a `UCRichDocument` in place on a zoomable canvas with `UltraCanvasRichTextEdit` (`SetZoomRange`, `Docs/UltraCanvas/UltraCanvasPaintSurface.md:21`). A shape's text is the same problem. Spell check, input methods, undo and clipboard come with it. |
| **All file I/O goes through `UltraCanvasFileLoader`** (§4.3) | This is the house rule. FileLoader already has `LoadTextDocument`, `SaveTextDocument`, `SavePresentation`, the password prompts and the dialog types for documents. It gains the missing spreadsheet and presentation entries and one call that says which kind a file is. |
| **Phase 1 already opens all three kinds** (§8) | Decks open read-only through `UltraCanvasPresentationPages` + `UltraCanvasPDFView`, as UltraViewer does today, until the editor lands. The app is usable from Phase 1 rather than being finished all at once. |

**Do not:**
- **Fork the models.** `UCRichDocument`, the spreadsheet sheet model and
  `PptxPresentation` each serve readers, writers, viewers, previews and now an
  editor. A second editor-only model is how fidelity bugs start, as the
  WYSIWYG investigation already said (§9 there).
- **Put editing logic in the app.** Everything that takes input or presents a
  value is a framework element (`AGENTS.md`, *Build UI out of UltraCanvas
  elements*). UltraFiler, UltraViewer, UltraClipboard and UltraMail's
  attachment preview should get the improvements for free.

---

## 2. What was asked, restated

1. An office package of our own, **UltraOffice**.
2. Built on the WYSIWYG display element and the document support UltraCanvas
   has now.
3. It covers **documents, spreadsheets and presentations**.
4. **One application** manages all three kinds, rather than three programs.

What follows is (§3) what exists, (§4) the shape of the app, (§5) the work per
kind, (§6) what the kinds share, (§7) file formats, (§8) phases and size, and
(§9) the decisions that are the user's to make.

---

## 3. What exists today

### 3.1 Documents (word processing): mature

| Piece | Where | Size |
|---|---|---|
| Model `UCRichDocument` | `UltraCanvas/include/UltraCanvasRichDocument.h`, `core/UltraCanvasRichDocument.cpp` | 4,219 |
| Editing core (positions, commands, undo, find/replace), with no UI | `include/UltraCanvasRichDocumentEditor.h`, `core/UltraCanvasRichDocumentEditor.cpp` | 5,251 |
| WYSIWYG element | `include/UltraCanvasRichTextEdit.h`, `core/UltraCanvasRichTextEdit.cpp` | 8,925 |
| ODT / DOCX / DOC readers and writers (DOC is read-only), math, symbol fonts | `UltraCanvas/Plugins/Documents/Word/` | 14,963 |
| PDF export and printing | `UltraCanvasRichTextEdit::ExportToPdf`, `include/UltraCanvasRichTextPrint.h`, `include/UltraCanvasPdfSurface.h` | — |
| Host app today | UltraTexter (`DocumentKind::RichDocument`) | — |

Features covered, in detail in
[`WordProcessingFeatureCoverage.md`](../UltraCanvas/WordProcessingFeatureCoverage.md)
and [`UltraCanvasRichTextEdit.md`](../UltraCanvas/UltraCanvasRichTextEdit.md):

- character and paragraph formatting, named styles, lists with Word's and ODF's numbering;
- tables with borders, spans and widths;
- inline and floating pictures, text frames, shapes, gradients and patterns;
- charts drawn by the chart engine;
- page view with page size, margins, headers and footers, page numbers, footnotes and endnotes, sections and columns;
- table of contents, captions, bookmarks and cross references;
- comments and tracked changes;
- zoom, find/replace, spell check, input methods, right-to-left text, accessibility, password-protected `.docx`.

What is still open, from those two documents:
- frames inside table cells and inside other frames;
- editing a drawing shape by shape;
- one page setup per document;
- picture bullets and numbered headings;
- DOC reader gaps (merged cells, comments, revisions, sections);
- the input method's candidate window is placed by the input method;
- screen readers untested on Windows and macOS.

### 3.2 Spreadsheets: usable editor, fidelity gaps

| Piece | Where | Size |
|---|---|---|
| Element (grid, formula bar, sheet tabs, format menu) | `include/UltraCanvasSpreadsheet.h`, `core/UltraCanvasSpreadsheet.cpp`, `core/UltraCanvasSpreadsheetComponent.cpp`, `…FormatMenu.*` | about 5,700 |
| Model (sparse cells, up to 1,048,576 × 16,384) | `include/UltraCanvasSpreadsheetSheet.h`, `…Cell.h`, `…Types.h`, `core/UltraCanvasSpreadsheetSheet.cpp` | about 5,000 |
| Formula engine (about 150 functions, circular-reference detection, functions an app can add, Excel and ODF translation) | `include/UltraCanvasSpreadsheetFormula.h`, `core/UltraCanvasSpreadsheetFormula*.cpp`, `…ExcelFormula.h`, `…OdsFormula.h` | about 4,200 |
| ODS / XLSX read and write (with password), XLS (BIFF5/8, Excel 2003 XML, HTML table) read, CSV / TSV | `core/UltraCanvasSpreadsheetFileIO.cpp`, `…XlsxIO.cpp`, `…Xls.cpp`, `…XlsIO.cpp`, `include/UltraCanvasCSVImport.h` | about 5,900 |
| Hosts today | DemoApp, UltraViewer and UltraClipboard (through `UltraCanvasMediaViewer`), UltraFiler preview, UltraDBAdmin import/export | — |

What works:
- several sheets;
- editing in the cell and in the formula bar, and pointing at cells while typing a formula;
- fonts, colours, borders, alignment and number formats;
- merged cells and freeze panes;
- inserting, deleting, hiding and auto-fitting rows and columns;
- cut, copy and Paste Special;
- grouped undo;
- multi-key sort, fill handle and auto-filter;
- named ranges and zoom;
- only the visible range is drawn.

Gaps, each checked in the source:

- **Copy and paste never reach the system clipboard.** `Copy` fills
  `clipboardData_` and nothing else (`core/UltraCanvasSpreadsheet.cpp:2532-2556`).
  Pasting from or to another application, or between two UltraOffice windows
  in different processes, does not work.
- **What a save loses.** `UltraCanvasSpreadsheetXlsxIO.cpp` and
  `UltraCanvasSpreadsheetFileIO.cpp` save none of these:
  - freeze panes;
  - hidden rows and columns;
  - named ranges;
  - auto-filters;
  - the print area.

  A grep of the two files finds none of them. XLS import is the only path
  that reads hidden rows and columns, and names.
- **Data validation is checked but not enforced.** The rules are checked in
  the model (`CheckDataValidation`, `include/UltraCanvasSpreadsheetCell.h`).
  Until 2026-11, `SpreadsheetCell::ValidateValue` was a stub that accepted
  everything. But nothing in the grid calls the check: an entry that fails
  its rule is accepted, a list rule shows no dropdown, and neither ODS nor
  XLSX reads or writes rules.
- **Conditional formats** are stored on the sheet
  (`core/UltraCanvasSpreadsheetSheet.cpp:472-493`), but the renderer and the
  readers and writers never refer to them.
- **Comments and hyperlinks** exist as types only
  (`include/UltraCanvasSpreadsheetTypes.h:589`, `:732`).
- **No charts.** The chart engine and the DrawingML chart reader and writer
  (`include/UltraCanvasDrawingML.h`, already shared by DOCX and PPTX) are
  unused by the spreadsheet.
- **No print or PDF.** `PrintSettings` and `SetPrintArea` exist
  (`include/UltraCanvasSpreadsheet.h:615-618`), but no path draws pages.
- **No array or spill formulas.** Only PMT, FV, PV and NPV among the
  financial functions.
- **No XLS writer**, which is reasonable, since XLSX replaces it.
- **Tests:** a dozen spreadsheet tests exist (formula engine, ODS, XLS, sort,
  fill, number format …). There is no XLSX round trip, and nothing for undo,
  clipboard, merges, freeze panes or filters.

### 3.3 Presentations: read, write and show; no editor

| Piece | Where | Size |
|---|---|---|
| Deck model `PptxPresentation` → `PptxSlide` → `PptxShape` → `PptxParagraph` → `PptxRun` (EMU) | `include/UltraCanvasPresentationPptx.h` | 271 |
| PPTX reader and writer (pptm, ppsx, potx …, with password) | `core/UltraCanvasPresentationPptx.cpp`, `…PptxWriter.cpp` | 2,851 |
| ODP reader and writer (otp, fodp), into the same model | `include/UltraCanvasPresentationOdp.h`, `core/UltraCanvasPresentationOdp.cpp`, `…OdpWriter.cpp` | about 3,050 |
| Slide renderer (`RenderPptxSlide`, an internal `SlidePainter` on an `IRenderContext`) | `include/UltraCanvasPptxSlideRender.h`, `core/UltraCanvasPptxSlideRender.cpp` | 723 |
| Deck as pages for `UltraCanvasPDFView` (thumbnails, search, notes), read-only | `include/UltraCanvasPresentationPages.h`, `core/UltraCanvasPresentationPages.cpp` | about 330 |
| Full-screen show (F5 / Shift+F5, Escape) | `UltraCanvasMediaViewer::StartPresenting` (`include/UltraCanvasMediaViewer.h:556`) | — |
| Document → deck and deck → outline | `PptxPresentationFromRichDocument`, `PptxPresentationToRichDocument`; FileLoader `SavePresentation`, `LoadTextDocument` | — |
| Hosts today | UltraViewer (slides, show, notes), UltraFiler and UltraClipboard (thumbnails), UltraTexter (a deck as an outline) | — |

What the model keeps:
- slide size and the theme's colours and fonts;
- per slide: hidden flag, background and notes;
- per shape: kind (title, subtitle, body, text, table, picture, connector, chart), place, rotation and mirroring;
- preset or custom outline, solid or gradient fill, line dash and arrowheads;
- text insets, wrap and anchor;
- paragraphs with level, bullets and spacing;
- runs with font, size, colour, bold and italic, and hyperlinks;
- table cells with merges and styles;
- picture crop;
- charts as `RichChart`.

Gaps:

- **No editing surface at all.** Nothing selects, moves or resizes a shape,
  types into one, adds a slide or reorders slides.
  `UltraCanvasPresentationPages` answers `false` to every write
  (`include/UltraCanvasPresentationPages.h`).
- **Masters and layouts are flattened.** The reader resolves what each shape
  inherits from its layout and master and copies it into the shape
  (`include/UltraCanvasPresentationPptx.h:23-35`). That is right for drawing.
  It is wrong for editing, because "New slide → Title and Content", "change
  this deck's title font everywhere" and "apply another design" all need the
  layouts and the master as objects.
- **Not read:**
  - effects (shadow, glow, 3-D);
  - pattern and picture fills;
  - **animations and transitions**;
  - comments, embedded objects, audio and video (`:37-40`).
- **No PDF export or printing of a deck.** `SlidePainter` already draws on an
  `IRenderContext`, so this is mostly a matter of exposing it to
  `UltraCanvasPdfSurface`.
- **No presenter view** (notes, timer and next slide on a second screen).
- **No `.ppt` (binary PowerPoint 97–2003)** and no Keynote.

What a slide editor can reuse besides the model:

- **`UltraCanvasVectorCanvas`** (`include/UltraCanvasVectorCanvas.h`, 389 +
  1,122 lines): zoom, rulers, grid, guides, snapping, scale and rotate handles
  (`VectorHandle`, `VectorHandleMode`), and an undo history. Its model is
  `VectorStorage::VectorDocument`, though, not a deck, and its text is
  `VectorText` spans, not paragraphs.
- **`UltraCanvasSlideshow`** is a *photo* slideshow, not a presentation
  element. Its six transition styles (`SlideshowFadeStyle`,
  `include/UltraCanvasSlideshow.h:34`) are reusable for slide transitions.

### 3.4 Shared services that already exist

| Service | Where | Used by |
|---|---|---|
| File dialogs, format inventory, password prompts | `UltraCanvasFileLoader` (`OpenFileDialog`, `RequestDocumentPassword`, `RequestNewDocumentPassword`, `AddTextDocumentLoadTypes` …) | every app |
| Office encryption (Agile and Standard) and ODF encryption | `UltraCanvasOfficeEncryption.h`; `OfficeEncryptionTest`, `OdfEncryptionTest` | DOCX, XLSX, PPTX, ODS |
| Chart engine + DrawingML charts | `Plugins/Charts/Engine`, `include/UltraCanvasDrawingML.h`, `include/UltraCanvasRichDocumentChart.h` | documents, decks |
| PDF writer for any element | `include/UltraCanvasPdfSurface.h` | RichTextEdit |
| Printing (`PrintDocumentWithDialog`, `IPrintPageSource`) | `UltraCanvasRichTextPrint.h` and the print backends | Texter |
| Start page | `UltraCanvasStartPage` | UltraPress, UltraFIBU |
| Tabs, toolbars, menus, split panes, sidebar | `UltraCanvasTabbedContainer`, `UltraCanvasToolbar`, `UltraCanvasMenu`, `UltraCanvasSplitPane`, `UltraCanvasSidebarNavigation` | everywhere |
| Settings, single instance, file associations | `UltraCanvasAppSettings`, `UltraCanvasSingleInstance`, `UltraCanvasFileAssociations` | most apps |
| Localisation | `Tr` / `UltraLocalisation` | every app |
| Autosave and recovery (app code) | `Apps/Texter/UltraCanvasTextEditor.*` | Texter |

---

## 4. The shape of the application

### 4.1 One window, three document kinds

```
UltraOfficeWindow
├── menu bar           File  Edit  View  Insert  Format  [Table | Sheet | Slide]  Tools  Help
├── toolbar host       the active document kind's toolbar (swapped on tab switch)
├── UltraCanvasTabbedContainer
│     ├── tab: Report.docx      → DocumentHost     → UltraCanvasRichTextEdit
│     ├── tab: Budget.xlsx      → SpreadsheetHost  → UltraCanvasSpreadsheet
│     └── tab: Pitch.pptx       → PresentationHost → UltraCanvasSlideEdit
│                                                     (+ slide sorter, notes pane)
├── side panel         per kind: styles / navigator, sheet list, slide layouts
└── status bar         page n of m · words | Sum / Average of selection | slide n of m
```

With no document open, the window shows a **start page**
(`UltraCanvasStartPage`) with:
- three "new" buttons (document, spreadsheet, presentation) and a template row
  for each;
- the recent files of all three kinds, each with its kind's icon;
- "Open…", which offers every type the three kinds read under one filter.

### 4.2 The host interface (app code)

The app has one small interface. Each kind implements it once, and the window
talks only to it:

```cpp
// Apps/UltraOffice/UltraOfficeDocumentHost.h (sketch)
class IOfficeDocumentHost {
public:
    virtual ~IOfficeDocumentHost() = default;
    virtual OfficeDocumentKind Kind() const = 0;                 // Document / Spreadsheet / Presentation
    virtual std::shared_ptr<UltraCanvasUIElement> Content() = 0; // what the tab shows
    virtual std::shared_ptr<UltraCanvasToolbar> Toolbar() = 0;   // built from elements, one per kind
    virtual void FillMenus(OfficeMenus& menus) = 0;              // Insert/Format/kind menu entries

    virtual bool Load(const std::string& path, const std::string& password, std::string& error) = 0;
    virtual bool Save(const std::string& path, const std::string& password, std::string& error) = 0;
    virtual void AddSaveTypes(FileDialogOptions& opts) const = 0;   // through FileLoader
    virtual bool ExportPdf(std::vector<uint8_t>& out, std::string& error) = 0;
    virtual std::unique_ptr<IPrintPageSource> PrintPages() = 0;

    virtual bool IsModified() const = 0;
    virtual void Undo() = 0;   virtual void Redo() = 0;
    virtual void Cut() = 0;    virtual void Copy() = 0;   virtual void Paste() = 0;
    virtual void ShowFind(bool replace) = 0;
    virtual std::string StatusText() const = 0;
    std::function<void()> onModifiedChanged, onStatusChanged;
};
```

Everything in it delegates to the element. `DocumentHost` is mostly the code
UltraTexter already has for `DocumentKind::RichDocument`:
`ApplyFormatCommand`, the toolbar's pressed state from `GetFormatState()`, and
the `UCWordDocumentIO` save. Lift it from Texter rather than rewriting it. The
other two hosts are new but thin.

**Why an interface and not Texter's `if (kind == …)` chain:** Texter has three
kinds and its checks are scattered through about 11,300 lines (for example
`IsPdf()` and `IsRichDocument()` guards, and the "detached, empty text area"
workaround described in the WYSIWYG investigation §11). UltraOffice starts with
three kinds of *editable* document, so one dispatch point is cheaper from the
first day.

### 4.3 FileLoader is the front door

Following *Core conventions* in `AGENTS.md`, FileLoader gets what is missing,
and the app uses only that:

| New in `UltraCanvasFileLoader` | Why |
|---|---|
| `DetectOfficeDocumentKind(path)` → Document / Spreadsheet / Presentation / None, from the extension and, for an ambiguous or missing one, the package contents (`IsPptxPackage`, the ODF `mimetype` entry, a compound file's streams) | So that "Open…", a double-click and drag and drop all pick the right host. Other apps (Filer "Open with", the desktop) can ask the same question. |
| `AddOfficeDocumentLoadTypes(opts)`: one "All office documents" filter, then one per kind, then each type | The Open dialog of a three-in-one app. It reuses `AddTextDocumentLoadTypes` and the spreadsheet types from `core/UltraCanvasSupportedFormats.cpp:300-316`. |
| `AddSpreadsheetSaveTypes`, `AddPresentationSaveTypes` | Today only documents have save-type helpers. |
| `LoadPresentation(path, deck, error, password)` | Today a deck is read through `ReadPptxPresentation` / the ODP reader directly, or as an outline through `LoadTextDocument`. The editor needs the deck itself, through the front door. |

`UltraCanvasSpreadsheet::LoadFromFile` / `SaveToFile` stay the spreadsheet's
reader and writer. FileLoader's helpers list their types and call them, as
`FileLoaderText.cpp:175` already does for text extraction.

### 4.4 Relationship with UltraTexter and UltraViewer

- **UltraViewer** stays the fast read-only viewer for everything, office
  files included. It already shows all three kinds through
  `UltraCanvasMediaViewer`, and gains whatever the elements gain.
- **UltraTexter** stays the editor for text, code and Markdown. Its
  word-processing tab keeps working, because it is the same element. Once
  UltraOffice reaches document parity (Phase 1), the `.odt` / `.docx` / `.doc`
  file associations move to UltraOffice. Texter keeps "open as text" and
  keeps converting between Markdown and documents. Whether Texter *removes*
  its rich-document tab later is a decision for later (§9). Keeping it costs
  nothing while the element is shared.

### 4.5 Where the code lives

| Layer | Content | Rule it follows |
|---|---|---|
| `UltraCanvas/{include,core}` | the three elements, the models, FileLoader entries, PDF/print page sources, the new slide editor | input, pictures and values are elements; engines are headless and testable without a display |
| `UltraCanvas/Plugins/Documents/Word` | ODT/DOCX/DOC as today | format readers stay plugins and depend downward |
| `Apps/UltraOffice` | window, start page, the three hosts, menus and toolbars built from elements, recent files, templates, autosave and recovery, settings | an app is a thin shell over framework elements |
| `Docs/UltraOffice/README.md`, `CHANGELOG.md` | user and developer docs | as every app |

---

## 5. The work per document kind

### 5.1 Documents

There is almost no framework work before the app can ship. The app work is:

1. **`DocumentHost`** lifted from Texter: the formatting toolbar, format
   state, save through `UCWordDocumentIO` / `SaveTextDocument`, PDF export,
   print, find/replace, spell-check menu.
2. **Dialogs** the element's API already supports but no app exposes as a
   dialog yet, built from `UltraCanvasModalDialog` and `UltraCanvasFormLayout`:
   - page setup (`RichPageSetup`);
   - paragraph (indents, spacing, line spacing, tabs);
   - named styles;
   - table properties;
   - picture alt text and wrapping;
   - insert table of contents, caption, cross reference and footnote;
   - comments and tracked-changes review.

   Each is a form over a struct the model already has. Check Texter first:
   any dialog it already has moves, it is not written twice.
3. **The side panel:** a navigator (headings, tables, pictures) from the model.

Framework items worth doing in the same period, from the coverage document's
own roadmap:
- the DOC reader's remaining gaps;
- frames in table cells;
- several page setups per document (one per section).

None of them blocks the app.

### 5.2 Spreadsheets

All of this work is framework work in `UltraCanvasSpreadsheet*`, in order of
how quickly a user meets it:

1. **System clipboard.** On copy, publish TSV as `text/plain` and an HTML table
   as `text/html` on the system clipboard. On paste, prefer `text/html` through
   `HTMLReader` (the house rule: tables are read with `HTML::Parser`, not
   another tag stripper), then fall back to TSV/CSV through `UltraCanvasCSVImport`.
   Keep the in-process buffer for full-fidelity paste inside the app. The
   Linux backend already publishes `text/html` next to text
   (`OS/Linux/UltraCanvasLinuxClipboard.cpp:199`). Check each backend's
   MIME-flavour API before planning further.
2. **Save what the sheet holds.** In both ODS and XLSX, read and write:
   - freeze panes;
   - hidden rows and columns;
   - named ranges;
   - auto-filters;
   - the print area and print settings.

   Add the round-trip tests §3.2 found missing: XLSX, merges, freeze, hidden,
   names, filters.
3. **Print and PDF.** Add a page source that splits the used range (or the
   print area) into pages by `PrintSettings`, draws each through the grid's
   cell renderer on an `IRenderContext`, and repeats title rows and columns.
   It then feeds both `UltraCanvasPdfSurface` and `PrintDocumentWithDialog`,
   as `CreateRichDocumentPrintPages` does for documents.
4. **Conditional formatting:**
   - draw the rules already stored, starting with value comparisons, colour
     scales and data bars;
   - read and write them in ODS (`calcext:conditional-formats`) and XLSX
     (`conditionalFormatting`);
   - add a rules dialog.
5. **Data validation:**
   - refuse, or warn about, an entry that fails its rule when it is
     committed (`CommitEdit`), as the rule's `alertStyle` says;
   - show list validation as an `UltraCanvasDropdown` in the cell;
   - add read and write.
6. **Comments and hyperlinks:**
   - draw comments, read and write them, and show them in a tooltip or popover;
   - make hyperlinks clickable.
7. **Charts.** Add a chart object anchored to cells, holding a `RichChart`
   whose series are cell ranges and are recomputed on change. It is drawn by
   the same chart engine, read and written through `UltraCanvasDrawingML` for
   XLSX (the reader already exists for DOCX/PPTX) and through `chart:chart`
   for ODS (the ODT reader already parses that object). It is moved and
   resized with the same handles as the slide editor's shapes (§5.3).
8. **Engine breadth, later:**
   - array and spill formulas;
   - the remaining financial functions (RATE, NPER, IRR, XNPV …);
   - pivot tables (a later, larger item).

### 5.3 Presentations: the new element

#### 5.3.1 The model: keep `PptxPresentation`, stop flattening

Keep the one deck model that both readers and both writers already share, and
extend it rather than inventing a second one:

- **Masters and layouts as objects.**
  - `PptxPresentation::masters` and `layouts` (each with its placeholders,
    background and text styles);
  - `PptxSlide::layoutIndex`;
  - `PptxShape::placeholder`, which is already there.

  The reader keeps resolving inherited properties *for drawing*, but it also
  records which values were inherited (it already does this for the place,
  with `placeInherited`). The writer then writes a slide's own values only,
  and layouts and masters as parts.
- **Transitions and simple animations:**
  - `PptxSlide::transition` (kind, duration, advance on click or after a time);
  - per shape, an appear or fade order;
  - read and written as `p:transition` / `p:timing` and ODP
    `presentation:transition-*` / `anim:*`.

  Only the subset the show can play is kept. The rest is preserved as
  unknown XML, or dropped with a warning on save. Which of the two is a
  decision for §9.
- **A neutral name.** The model is already PowerPoint's *and* ODF's. A
  `using UCPresentation = PptxPresentation;` (and the same for slide and
  shape) lets new code use format-neutral names without touching the readers.
  Renaming the types is a later mechanical step.

#### 5.3.2 Why a new element and not `UltraCanvasVectorCanvas`

`VectorCanvas` edits `VectorStorage::VectorDocument`: one page of layers, in
points, with span-only text. To use it for slides, every edit would have to be
converted between that model and the deck's EMU shapes, paragraphs, tables,
charts and placeholders, and back, and the conversion would lose placeholders,
tables and charts. That is the "second model" this proposal refuses (§1).

A deck needs its own element. What it shares with VectorCanvas is behaviour:
handles, snapping and guides. Where that code can be lifted into a helper both
use (`VectorHandle`, `VectorHandleMode`), lift it. Where it cannot, copy the
behaviour, not the model.

#### 5.3.3 `UltraCanvasSlideEdit`

`UltraCanvas/include/UltraCanvasSlideEdit.h`, `core/UltraCanvasSlideEdit.cpp`,
documented as `Docs/UltraCanvas/UltraCanvasSlideEdit.md`, with a row in
`UltraCanvasUIElements.md`.

| Part | How |
|---|---|
| Drawing | Expose `SlidePainter` (today internal to `core/UltraCanvasPptxSlideRender.cpp:211`) as `RenderPptxSlide(ctx, deck, index, rect)` on any `IRenderContext`. The editor, the thumbnails, the show and PDF export then all draw with the same code. |
| Selection, move, resize, rotate | Hit-test shapes back to front; draw handles; Shift keeps proportions, Alt resizes from the centre; snap to the slide's edges and centre, to other shapes' edges and centres, and to guides; nudge with the arrow keys; group and ungroup; z-order commands. |
| **Text in shapes** | Double-click (or start typing on a selected text shape) opens the shape's paragraphs in an `UltraCanvasRichTextEdit` placed over the shape at the slide's zoom (`SetZoomRange`, as UltraPaint's text layers do). On commit the text goes back into `PptxParagraph`s. This needs a `PptxParagraph ⇄ UCRichDocument` conversion per shape, which is a small addition beside `PptxPresentationToRichDocument`. The text box gets caret, selection, formatting, spell check, IME, clipboard and undo from the element. |
| Tables | Phase 4b. Cell text is edited through the same in-place editor; rows and columns are added and removed through commands. |
| Pictures, charts | Insert through FileLoader's image dialog. Crop handles. A chart's data is edited in a small `UltraCanvasSpreadsheet` in a dialog, which is the natural link to §5.2's charts. |
| Undo | Command-based, one command per operation (insert, delete, move, resize, rotate, a text commit, formatting, slide insert, delete and move), as in `UltraCanvasRichDocumentEditor`. Do not snapshot the deck: it holds the pictures' bytes. |
| Slide sorter | A thumbnail strip at the side (single column) and a sorter view (grid) that both use `RenderPptxSlide`. Drag to reorder, context menu for new, duplicate, hide and delete. Look for an existing element first (`UltraCanvasAlbum`, `UltraCanvasListView`) before writing one. |
| Notes | An `UltraCanvasRichTextEdit` (continuous view) under the slide, over `PptxSlide::notes` through the same conversion. |
| Layouts | "New slide" and "Layout" menus from §5.3.1's layouts; placeholders show their prompt text ("Click to add title") when empty. |

#### 5.3.4 Show, presenter view, export

- **Show.** Move the full-screen show out of `UltraCanvasMediaViewer` into a
  framework element (`UltraCanvasSlideShowPlayer`). It plays the deck's
  transitions with `SlideshowFadeStyle` where they match, and the appear and
  fade order. UltraViewer and UltraOffice then share it.
- **Presenter view.** On a second screen, show the current slide, the next
  slide, the notes and a timer. This needs the platform layer's monitor list
  and a second window. Check what `UltraCanvasDesktopShell` and the window
  classes already offer before planning it.
- **PDF and print.** Draw one page per slide (hidden slides optional, notes
  pages optional) with the exposed `SlidePainter`, through
  `UltraCanvasPdfSurface` / `IPrintPageSource`.

---

## 6. What the three kinds share

| Concern | Plan |
|---|---|
| New from template | Templates are ordinary `.ott` / `.dotx`, `.ots` / `.xltx`, `.otp` / `.potx` files under `media/templates/office/`. The deck readers already read the template extensions; documents and spreadsheets do not yet (§7). "Save as template" uses the same writers. |
| Recent files, settings | `UltraCanvasAppSettings`; one recent list with a kind per entry. |
| Autosave and recovery | Lift Texter's autosave into a small shared helper (framework or app library): periodic save of a modified document to an internal backup in its own format, and offer to recover it at the next start. Texter's backup is a text file today, which is why it skips rich documents (WYSIWYG investigation §11). UltraOffice backs up in the document's own format. |
| Passwords | `RequestDocumentPassword` / `RequestNewDocumentPassword` already cover DOCX, XLSX and PPTX. ODT and ODP with a password are refused on save today (`include/UltraCanvasFileLoader.h:336-339`, `:345-348`), while `OdfEncryptionTest` exists. Find out whether the ODF writer side is missing for documents and decks or only not wired up. |
| Copy and paste between kinds | Cells to a document → a `UCRichDocument` table (the HTML flavour does this for free once §5.2.1 lands). Cells to a slide → a table shape. A document's selection to a slide → a text box. A chart from cells → a chart in a document or slide (embedding a *live* link is a later step). |
| Export to PDF | One menu item, three page sources (documents already have theirs). |
| Spell check, languages | Documents and slide text through `UltraCanvasRichTextEdit`, which already spell-checks. Cell text through the spell checker on edit, which is optional. |
| Localisation | All visible strings through `Tr`; run `extract_ui_strings.py` and `check_locale_catalogues.py` as for every app. |
| Accessibility | The documents element has an accessibility bridge. The spreadsheet grid and the slide editor need roles (table and cell, canvas and shape) through `UltraCanvasAccessibility`. Plan for it in each element, do not add it at the end. |

---

## 7. File formats

| Kind | Read | Write | Missing |
|---|---|---|---|
| Documents | ODT, DOCX, DOC, Markdown, LaTeX, plain text | ODT, DOCX, PDF (export), deck (as slides) | templates (OTT, DOTX) and flat ODT (FODT) as documents; RTF read and write (the WYSIWYG investigation lists RTF import as a follow-up); `.doc` write (not needed) |
| Spreadsheets | ODS, XLSX, XLS (BIFF5/8, 2003 XML), CSV / TSV | ODS, XLSX, CSV / TSV | PDF export (§5.2.3); templates (OTS, XLTX) and FODS; XLS write (not needed) |
| Presentations | PPTX (pptm, ppsx, ppsm, potx, potm), ODP (otp, fodp) | the same, plus a document as a deck | PDF export (§5.3.4); `.ppt` read (§9) |

The template extensions for documents and spreadsheets do not appear in
`core/UltraCanvasSupportedFormats.cpp` or `UltraCanvasWordDocumentIO.cpp`.
A template is the same package with another MIME type, so adding them is
small, but it is needed for §6's "new from template".

**Native format.** UltraOffice has none of its own. A new document is saved
as ODF or OOXML (§9). The UCD container (`Docs/UltraCanvas/UCD-FileFormat-v2.md`)
defines no presentation or spreadsheet content type and is not needed for this.

---

## 8. Phases and size

The sizes are estimates of new or changed code from comparable work in this
tree (the WYSIWYG investigation's estimates of about 6,000 lines came close to
what was built). They are not commitments.

| Phase | Scope | Framework | App |
|---|---|---|---|
| **1. Shell + documents + spreadsheets as they are** | Window, tabs, start page, the host interface, `DocumentHost` (lifted from Texter), `SpreadsheetHost`, decks shown read-only through `UltraCanvasPresentationPages` + `UltraCanvasPDFView` + the existing show; FileLoader `DetectOfficeDocumentKind`, `AddOfficeDocumentLoadTypes`, the save-type helpers, `LoadPresentation`; recent files, settings, file associations, PDF export and print for documents; Linux/Windows/macOS packaging; `Docs/UltraOffice/README.md` | ~600 | ~2,500 |
| **2. Spreadsheet fidelity** | §5.2 items 1–3: system clipboard, saving what the sheet holds, print/PDF, with round-trip tests | ~2,000 | ~200 |
| **3. Slide editor core** | §5.3.1 masters/layouts and transitions in the model, readers and writers; `SlidePainter` on `IRenderContext`; `UltraCanvasSlideEdit` with selection, move, resize, rotate, snapping, undo, slide sorter, notes, new slide from layout; `PresentationHost`; deck PDF/print | ~4,500 | ~800 |
| **4. Slide text, tables, show** | 4a: in-place text editing through `UltraCanvasRichTextEdit` and the paragraph conversion. 4b: tables. 4c: `UltraCanvasSlideShowPlayer` with transitions; presenter view | ~2,500 | ~400 |
| **5. Spreadsheet depth** | §5.2 items 4–7: conditional formatting, validation, comments, hyperlinks, charts | ~3,000 | ~400 |
| **6. Shared polish** | Templates, autosave and recovery, cross-kind paste, accessibility roles for grid and slides, dialogs of §5.1.2 not yet built | ~1,000 | ~1,500 |

**Total, roughly 20,000 lines**, about two thirds of it in the framework. That
is less than the document stack alone already is (about 33,000 lines). Phase 1
is a usable product: it opens, edits and saves documents and spreadsheets and
shows presentations. Phases 2 and 3 can run in parallel because they touch
different files. Phase 3 is the risky one, for the same reason the WYSIWYG
work's Phases 0–2 were: the command and undo layer and the master/layout
model must be right before anything is built on them.

**Validation:**
- **Model tests with no display:**
  - command apply and revert for every slide command;
  - spreadsheet round trips;
  - a slide's paragraphs to `UCRichDocument` and back.
- **Element tests on a real render context under Xvfb**, in the shape of
  `Tests/RichTextEditElementTest.cpp`: hit tests, handles, snapping.
- **Fixtures saved by LibreOffice** (`soffice --headless --convert-to`), as
  the Word tests do. Never a real person's file.
- **The demo app's layout audit** for every new dialog and panel.

---

## 9. Decisions for the user

1. **Default save format for a new file.** ODF (ODT/ODS/ODP: open standard,
   LibreOffice's) or OOXML (DOCX/XLSX/PPTX: what most recipients expect).
   Both are read and written equally well. *Suggested: OOXML by default, with
   a setting*, because files are mostly sent to people using Microsoft Office.
2. **What happens to animations and effects we cannot edit.** Keep their XML
   and write it back unchanged (more work, no loss), or drop them with a
   warning on save (simpler). *Suggested: drop with a clear warning in the
   first version*, and keep them once the model can carry them.
3. **Binary `.ppt` import.** Old decks are still common in companies. The
   compound-file reader (`UltraCanvasCompoundFile`) and the DOC reader's
   OfficeArt shapes are a head start, but this is a separate project of a
   size like the DOC reader (about 3,000 lines). *Suggested: not in the first
   release.*
4. **UltraTexter's word-processing tab** once UltraOffice exists: keep it (no
   cost while the element is shared) or remove it so there is one place to
   edit documents. *Suggested: keep it, and move the file associations.*
5. **Name and placement.** `Apps/UltraOffice`, executable `UltraOffice`,
   with three desktop entries ("UltraOffice Document", "Spreadsheet",
   "Presentation") that start the same program on the start page or with a
   new file of that kind.

---

## 10. References

- [`../UltraCanvas/WYSIWYGElementInvestigation.md`](../UltraCanvas/WYSIWYGElementInvestigation.md):
  the investigation behind `UltraCanvasRichTextEdit`, and the method this one follows
- [`../UltraCanvas/WordProcessingFeatureCoverage.md`](../UltraCanvas/WordProcessingFeatureCoverage.md):
  document feature coverage and roadmap
- [`../UltraCanvas/UltraCanvasRichTextEdit.md`](../UltraCanvas/UltraCanvasRichTextEdit.md):
  the document element
- [`../UltraCanvas/UltraCanvasSpreadsheetExamples.md`](../UltraCanvas/UltraCanvasSpreadsheetExamples.md),
  [`../UltraCanvas/UltraCanvasSpreadsheetXls.md`](../UltraCanvas/UltraCanvasSpreadsheetXls.md):
  the spreadsheet element and its formats
- [`../UltraCanvas/UltraCanvasDrawingML.md`](../UltraCanvas/UltraCanvasDrawingML.md):
  charts shared by DOCX, PPTX and (planned) XLSX
- [`../UltraCanvas/UltraCanvasVectorCanvas.md`](../UltraCanvas/UltraCanvasVectorCanvas.md),
  [`../UltraCanvas/UltraCanvasSlideshowExamples.md`](../UltraCanvas/UltraCanvasSlideshowExamples.md):
  the two elements the slide editor borrows behaviour from
- [`../UltraCanvas/UltraCanvasStartPage.md`](../UltraCanvas/UltraCanvasStartPage.md):
  the start page
- `UltraCanvas/include/UltraCanvasPresentationPptx.h`, `UltraCanvasPresentationOdp.h`,
  `UltraCanvasPptxSlideRender.h`, `UltraCanvasPresentationPages.h`: the deck model,
  readers, writers and renderer
- `Apps/Texter/UltraCanvasTextEditor.h`: the per-tab document-kind switch this
  app generalises
