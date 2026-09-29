# UltraCanvasRichTextEdit

The WYSIWYG editing element: the caret sits in rendered text, and **bold is a
state of the selection** rather than two asterisks in a buffer.

- Header: `UltraCanvas/include/UltraCanvasRichTextEdit.h`
- Editing core (UI-free): `UltraCanvas/include/UltraCanvasRichDocumentEditor.h`
- Document model: `UltraCanvas/include/UltraCanvasRichDocument.h`
- Design notes: [`WYSIWYGElementInvestigation.md`](WYSIWYGElementInvestigation.md)

## When to use it (and when not to)

| Editing… | Element |
|---|---|
| Source code, logs, configuration, plain text | `UltraCanvasTextArea` (`PlainText`) |
| Markdown, with a live preview and the caret line showing its source | `UltraCanvasTextArea` (`MarkdownHybrid`) |
| A word-processing document — fonts, sizes, colours, alignment, headings, lists, tables, images | **`UltraCanvasRichTextEdit`** |

The dividing line is the document model, not the look. The TextArea's document
is a list of text lines, so formatting is re-derived from markup every frame
and anything Markdown cannot spell cannot be typed. This element's document is
a `UCRichDocument` — the same block/run model the ODT, DOCX, legacy `.doc` and
LaTeX readers and writers already produce — so a 14 pt Georgia run in red
survives a round trip through `.odt` or `.docx`.

## Three layers

```
UltraCanvasRichTextEdit    element: layout, rendering, mouse/keyboard, caret, scrolling
        |
UCRichDocumentEditor       editing core: positions, commands, formatting, undo   (UI-free)
        |
UCRichDocument             document model: blocks, runs, media, serializers      (UI-free)
```

Both lower layers are plain C++ over std types, so every editing rule is
testable without a display (`Tests/RichTextEditorTest.cpp`).

**Positions are `{blockIndex, cellRow, cellColumn, byteOffset}`** — the offset
is into the *concatenated run text* of one **text container**, never a
`{run, offset}` pair. A container is either a block's own runs
(`cellRow == cellColumn == -1`, which is every position outside a table) or one
cell of a table block. Applying a format splits
and merges runs constantly; a caret must not move when the run structure
changes underneath it. That same string is what the element hands to
`ITextLayout`, so hit testing and caret geometry need no translation layer.

## Minimal use

```cpp
#include "UltraCanvasRichTextEdit.h"

auto editor = CreateRichTextEdit("editor", 0, 0, 800, 600);
window->AddElement(editor);

editor->SetMarkdown("# Report\n\nSome **bold** text and a [link](https://example.com).\n");
editor->onDocumentChanged = [editor]() {
    // Title bar dirty marker, autosave timer, ...
    (void)editor->IsModified();
};
```

## Opening and saving documents

The element performs **no file I/O**, exactly like `UltraCanvasTextArea`:
applications load and save through the framework's document front door and hand
the result over.

```cpp
#include "UltraCanvasFileLoader.h"
#include "Plugins/Documents/Word/UltraCanvasWordDocumentIO.h"

std::string error;
if (auto document = UltraCanvasFileLoader::LoadTextDocument(path, error)) {
    editor->SetDocument(document);          // .odt / .docx / .doc / .tex / .md
} else {
    ShowError(error);
}

// Saving: the element hands back the same document, edits included.
UCWordDocumentIO::Save(savePath, *editor->GetDocument(), error);
```

Because the element shares ownership of the document (`std::shared_ptr`), an
application can keep holding it — to save it, to inspect blocks the user never
touched, or to hand the same document to a read-only view.

UltraTexter does exactly this: a `.odt`/`.docx`/`.doc` tab holds the document
the reader produced, hands it to the element, and hands the same object back to
`UCWordDocumentIO::Save` — no conversion in either direction. See
`Apps/Texter/UltraCanvasTextEditor.cpp` (`LoadWordIntoDocument`,
`SaveRichDocumentAs`) for a worked integration, including how the element is
swapped into an existing tab layout and how a shared formatting toolbar drives
either this element or a Markdown text area.

## Building the toolbar

The element draws **no chrome**. Build the toolbar from real elements (framework
rule — see [AGENTS.md](../../AGENTS.md)) and drive it from `GetFormatState()`,
which reports every attribute as on, off or *mixed* across the selection:

```cpp
auto toolbar = std::make_shared<UltraCanvasToolbar>("format-bar", 0, 0, 800, 40);

// A toggle button carries its own on/off state: SetCanToggled(true) (which
// AddToggleButton does) makes it report through onToggle and hold its pressed
// look afterwards.
auto boldButton = toolbar->AddToggleButton("bold", "B", "",
    [editorRaw](bool) { editorRaw->ToggleBold(); SyncToolbar(); });
// Toolbar buttons must not take the focus, or pressing Bold and carrying on
// typing would type into the button.
boldButton->SetAcceptsFocus(false);

auto styleBox = toolbar->AddDropdownButton("style", "",
    {"Body text", "Heading 1", "Heading 2"}, nullptr);
styleBox->onSelectionChanged = [editorRaw](int index, const DropdownItem&) {
    editorRaw->SetHeadingLevel(index);       // 0 = body text
    SyncToolbar();
};

// Keep the toolbar in step with the caret.
void SyncToolbar() {
    RichCharFormatState state = editorRaw->GetFormatState();
    boldButton->SetPressed(RichCharFormatState::IsOn(state.bold));
    // `false` = do not re-notify, or writing the box back would re-apply the
    // style that was just read out of the document.
    styleBox->SetSelectedIndex(editorRaw->GetCurrentHeadingLevel(), false);
}
editor->onSelectionChanged = SyncToolbar;
editor->onDocumentChanged = SyncToolbar;
```

Two things that only show up once it is wired for real:

- **Call the sync after a toolbar action too.** Pressing Bold at a *collapsed*
  caret arms the format instead of editing anything, so it raises neither
  `onDocumentChanged` nor `onSelectionChanged` — the toolbar would show the old
  state until the caret next moved.
- **Point one direction of the wiring with raw pointers.** The element owns
  `onSelectionChanged`, and the container owns both the element and the
  toolbar; capturing the widgets' `shared_ptr`s in the element's callbacks
  *and* the element's in theirs closes an ownership cycle that never frees the
  page. `Apps/DemoApp/UltraCanvasWYSIWYGExamples.cpp` is the worked example.

`Tri::Mixed` is a real state: a selection spanning bold and plain text is
neither, and a toolbar should show that rather than lying in one direction.

## The editing surface

### Character formatting

```cpp
editor->ToggleBold();            // Ctrl+B
editor->ToggleItalic();          // Ctrl+I
editor->ToggleUnderline();       // Ctrl+U
editor->ToggleStrikethrough();
editor->ToggleInlineCode();
editor->ToggleSubscript();       // mutually exclusive with superscript
editor->ToggleSuperscript();
editor->SetFontFamily("Georgia");
editor->SetFontSize(14.0f);
editor->SetTextColor("#CC0000");
editor->SetLink("https://example.com");
editor->ClearFormatting();
```

With a selection these apply to it. With a collapsed caret they **arm** the
format for the next typed character — pressing Bold and carrying on typing does
what a word processor does.

### Paragraph formatting

```cpp
editor->SetHeadingLevel(2);                     // 0 = body text
editor->SetAlignment(RichTextAlign::Center);
editor->ToggleBulletList();
editor->ToggleNumberedList();
editor->IndentList();                           // Tab inside a list
editor->OutdentList();                          // Shift+Tab
editor->ToggleBlockQuote();
editor->ToggleCodeBlock("cpp");
```

Each applies to every block the selection touches. A selection that ends exactly
at the start of a block does not include it, which is what users expect when
they drag down to the next paragraph.

### Tables

The caret goes inside table cells: click into one, type, select, format, and
**Tab** / **Shift+Tab** walk the cells in reading order.

```cpp
RichDocPosition cell(tableBlock, /*row*/ 1, /*column*/ 0, /*byteOffset*/ 0);
editor->GetEditor().SetCaret(cell);
std::string text = editor->GetEditor().TextAt(cell);
```

Three rules make cell editing behave the way a word processor does rather than
the way a naive text model would:

- **Enter inside a cell adds a line to the cell**, it does not split the table's
  block in two.
- **Backspace at the start of a cell steps to the previous cell** and deletes
  nothing — cells cannot be merged by deleting the text between them, so there
  is nothing sensible to join.
- **A selection never spans two cells** (nor crosses into or out of one). The
  moving end is held at the edge of the anchor's container, because a range
  that spanned cells would describe an edit no table can honour.

**Merged cells are laid out on the grid.** A cell spanning columns is drawn that
many columns wide and the cells beside it shift past it; a cell spanning rows
stretches down over them and owns its column in every row it covers. A cell's
position stays `{row, index-within-row}` — the grid column is geometry only, so
spans never move a caret.

Search reaches into cells, so **find and replace now cover table content**.
`AllContainers()` enumerates every container in document order if you need to
walk the document yourself.

#### Editing a table's structure

Tables are created and reshaped from the caret, so a menu item says what the
user means — "insert a row below *this* one":

```cpp
editor->InsertTable(3, 4, /*headerRow=*/true);   // caret lands in the first cell

editor->InsertRowAbove();      editor->InsertRowBelow();
editor->InsertColumnLeft();    editor->InsertColumnRight();
editor->DeleteCurrentRow();    editor->DeleteCurrentColumn();
editor->MergeWithCellRight();  editor->MergeWithCellBelow();
editor->SplitCurrentCell();
```

Each returns `false` when it does not apply — the caret is not in a table,
there is no neighbour to merge with, the cell is not merged — which is also
what a menu should ask to decide whether to offer the item:

```cpp
if (editor->IsCaretInTable()) {
    int rows = 0, columns = 0, row = 0, column = 0;
    editor->CaretTableGeometry(rows, columns, row, column);   // for "Delete row 2 of 5"
    menu.SetEnabled("split", editor->CanSplitCurrentCell());
}
```

Each operation is **one undo step**, and each keeps the grid rectangular:

- A span reaching across an insertion point **grows** rather than being cut in
  two, because the text in it lives in one cell and cannot be in two places.
- A span reaching into a deleted row or column **shrinks**; where the span
  *started* in the deleted row, the cell moves down into the next one instead,
  so its text is not deleted along with the row.
- **Merging keeps the text of every cell it absorbs**, appended to the
  surviving cell. A merge is a layout decision, and dropping what somebody
  typed would be a silent deletion.
- A merge whose rectangle would cut an existing span in half is **refused**:
  the model cannot store half a cell, so approximating it would corrupt the
  grid.
- Deleting the last row or the last column deletes the table, because a table
  with no cells has nothing to type into and no way back.

Cells are stored sparsely — a merged cell is one `RichTableCell` with a span,
and the slots it covers hold nothing — so **a cell's index within its row is
not its column**. `BuildTableGrid()` (in `UltraCanvasRichDocument.h`) resolves
which cell occupies each slot, and is what both the layout and these operations
use; positions keep addressing cells as `{row, index-within-row}`.

#### Selecting a block of cells

Drag (or Shift+arrow) from one cell into another and the selection becomes a
block of whole cells - the smallest grid rectangle holding both, grown until no
merged cell sticks out of it - drawn filled, without a caret:

```cpp
if (editor->HasCellSelection()) editor->MergeSelectedCells();   // "merge these six"
editor->GetEditor().SelectCellRange(tableBlock, /*top*/ 0, /*left*/ 0, /*bottom*/ 1, /*right*/ 2);
```

On a cell selection Delete and Backspace empty the cells (the table keeps its
shape), typing replaces them from the top-left cell, character formatting and
`SetAlignment` apply to every cell, and Copy copies the cells as a table (plain
text: a tab between cells, a line per row). Pasting a copied table into a cell
fills the grid from that cell on, as a spreadsheet does; pasting paragraphs
into a cell keeps them in the cell, a line each. Extending a selection out of
a table stops at its edge; extending one into a table from outside takes the
whole table.

### Pictures: selecting, resizing, alt text

A click on a picture - a paragraph of its own or one in the line - selects it:
a frame with eight handles is drawn round it. Dragging a corner handle resizes
it in proportion, a side handle stretches it; release commits the size as one
undo step (`imageWidthPt` / `imageHeightPt`, never wider than the column).
Delete removes a selected picture. A right-click selects the picture under the
pointer first, so a host's context menu can offer picture items.

```cpp
if (editor->HasSelectedImage()) {
    editor->SetSelectedImageAltText("Quarterly revenue by region");
    editor->SetSelectedImageSize(/*widthPt*/ 240, /*heightPt*/ 160);
}
editor->SelectImage(RichDocPosition(imageBlock, 0));
// Editing core: pictures are addressed by where they sit.
edit.GetEditor().SetImageAltText(RichDocPosition(block, placeholderOffset), "Logo");
```

### Floating pictures

A picture run with `imageWrap` other than `Inline` floats: its placeholder stays
where it is anchored in the text (so it moves with its paragraph and a
selection or Backspace covers it), but it takes no room in the line. It is
placed at its paragraph's top plus `imageOffsetYPt`, at the column's left or
right edge, centred, or `imageOffsetXPt` from the left (`imageFloatAlign`
`Left` / `Right` / `Center` / `Default`), and text treats it as `imageWrap`
says:

| `imageWrap` | Text |
|---|---|
| `Square` | flows beside it; paragraphs level with it are narrowed |
| `TopAndBottom` (or a centred `Square`) | stops above it and resumes below |
| `BehindText` / `InFrontOfText` | ignores it; it is drawn under / over the text |

DOCX `<wp:anchor>` pictures (wrap, `wp:align` or `wp:posOffset`, vertical
offset from the paragraph) and ODT frames anchored to a paragraph or character
(`style:wrap`, `style:horizontal-pos`, `svg:x`/`svg:y`) read into this and
write back out; HTML output floats them with CSS. A floating picture is
selected, resized and described like any other. In a table cell or a header
it stays in the line.

The wrap works a paragraph at a time: a paragraph that starts beside a picture
is narrowed as a whole, so a long one beside a short picture stays narrow below
it too.

### Check lists

```cpp
editor->ToggleCheckList();        // the selected paragraphs become to-do items
editor->ToggleCheckedAtCaret();   // tick / untick (a click on the box does the same)
```

A check list item is an unordered `ListItem` with `RichDocBlock::checkbox`
set, ticked when `checked`. The element draws a box where the bullet would
be; clicking it toggles the tick as one undo step. Enter continues the list
with an unticked box. Markdown spells it `- [ ]` / `- [x]`; ODT and DOCX have
no check list, so they carry a ☐ or ☒ in front of the text, which
`UCRichDocument::ReadCheckboxPrefixes()` (run by every reader) turns back into
a check list item - Word's own check box content controls read the same way.

### Formulas

A run with `RichTextRun::math` holds LaTeX source and is drawn typeset
(through `UltraCanvasInlineMath`, on the line's baseline, never broken over
two lines). Put the caret inside it and the source appears in its place for
editing; move the caret away and it is typeset again. A `MathBlock` is typeset
in display style and centred until the caret enters it. Without the LaTeX
module, both show their source.

### Structure

```cpp
editor->InsertHorizontalRule();
editor->InsertPageBreak();
editor->InsertImageFromFile("/path/diagram.png", "Architecture diagram");
editor->InsertImageFromMemory("chart.png", "image/png", bytes, "Q3 revenue");

// ...or INSIDE the line at the caret, rather than as a paragraph of its own:
editor->InsertInlineImageFromFile("/path/logo.png", "Logo");
editor->InsertInlineImageFromMemory("icon.png", "image/png", bytes, "warning");
```

**A picture can sit in the text.** A run with `mediaIndex >= 0` *is* a picture -
a logo mid-sentence, an icon in a heading - and its `text` is a single U+FFFC
OBJECT REPLACEMENT CHARACTER. That placeholder gives the picture one character's
worth of the block's text, so the caret steps over it, a selection covers it and
Backspace deletes it, with no position needing to know it is not a letter. The
layout reserves a box for it (`TextAttributeFactory::CreateShape`), so the line
grows to hold it and the text after it flows along.

Readers decide inline-versus-block by what else the paragraph holds: a picture
alone on a line is a standalone `RichBlockType::Image`, a picture among words is
a run. Both formats anchor the two the same way in their markup, so the markup
alone cannot tell them apart.

Images are copied into the document's media store, so the document stays
self-contained and saves to `.odt`/`.docx` with the picture inside it.

## Keyboard

| Key | Action |
|---|---|
| Arrows | Character and visual-line motion (`+Ctrl`: word, `+Shift`: extend) |
| Home / End | Start / end of the paragraph (`+Ctrl`: document) |
| Page Up / Down | Scroll a screen and move the caret with it |
| Enter | New paragraph (a list continues the list; an empty list item leaves it; inside a code block, a new line) |
| Shift+Enter | Line break inside the paragraph |
| Tab / Shift+Tab | Indent / outdent, inside a list only |
| Backspace / Delete | Delete, joining paragraphs across a boundary |
| Ctrl+A / C / X / V | Select all, copy, cut, paste |
| Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z | Undo / redo |
| Ctrl+B / I / U | Bold, italic, underline |
| Ctrl+click on a link | `onLinkClicked` (a plain click just places the caret, so links stay editable) |

## Autoformat as you type

On by default, as in a word processor:

| Typed | Becomes |
|---|---|
| `"` `'` | “ ” ‘ ’ (opening after a space or bracket, closing - the apostrophe - after a letter) |
| `a--b`, `a -- b` | `a—b` (em dash), `a – b` (en dash) |
| `...` | … |
| `(c)` `(r)` `(tm)` `->` `<-` `=>` | © ® ™ → ← ⇒ |
| `1. ` `3) ` `a) ` opening a paragraph | a numbered list (starting at that number / letter) |
| `- ` `* ` `+ ` | a bullet list |
| `[ ] ` `[x] ` | a check list item |
| `# ` … `###### ` | a heading of that level |
| `> ` | a quote |
| `---` `***` `___` then Enter | a horizontal rule |

Each correction is its own undo step, so Ctrl+Z right after it takes back only
the correction and leaves what was typed. Code (inline or a code block) and
formulas are never corrected, and pasted text is never corrected.

```cpp
editor->SetAutoFormatEnabled(false);            // everything off
RichAutoFormatOptions options = editor->GetAutoFormatOptions();
options.smartQuotes = false;                    // or just one of them
editor->SetAutoFormatOptions(options);
```

The editing core has the same switches (off by default there, because a
programmatic `InsertText` must insert exactly what it is given); typing goes
through `UCRichDocumentEditor::TypeText` / `TypeEnter`.

## Drag and drop

Press inside the selection and drag: the text moves to where the drop caret
shows, and stays selected; hold Ctrl at the drop to copy it instead. One undo
step takes the move back. A press inside the selection that does not move is
an ordinary click. Image files dropped from another application are inserted
where they are dropped, as pictures in the line; `onFilesDropped` lets a host
take other files (or all of them). `enableDragAndDrop = false` turns dragging
the selection off. The editing core's `MoveRange(range, target, copy)` does the
move.

## Find and replace

```cpp
RichFindOptions options;
options.caseSensitive = false;
options.wholeWord = true;
editor->SetFindOptions(options);

editor->FindNext("Berlin");        // selects the match and scrolls to it
editor->FindPrevious("Berlin");
editor->ReplaceCurrent("Berlin", "Munich");   // only if the selection IS a match
int replaced = editor->ReplaceAll("Berlin", "Munich");
int total = editor->CountMatches("Berlin");   // for a "3 of 12" readout
```

Two things worth knowing:

- **`ReplaceAll` is one undo step**, not one per match, so Ctrl+Z takes the
  whole replace back.
- **Replaced text keeps the formatting of the text it replaced.** Replacing a
  word inside a bold heading leaves it bold — the format is sampled from inside
  the match before it is deleted, because deleting it would otherwise leave the
  caret in the preceding run and the insert would adopt *that* formatting.

Search lives in `UCRichDocumentEditor` (`Find`, `FindAll`, `ReplaceAll`), so it
is testable without a display. Matches never span a block boundary, which is
what makes each one independently replaceable. Case folding is ASCII, the same
as `UltraCanvasTextArea`'s search: `report` finds `Report`, but `strasse` does
not find `STRASSE`.

## Spell checking

```cpp
editor->SetSpellCheckEnabled(true);
editor->RunSpellCheck();                     // after changing dictionary
```

Checking runs on the shared `UltraCanvasSpellChecker` worker thread, over the
document as one string with blocks joined by `\n` — one job per document rather
than one per block, so a long document does not flood the queue. Results are
drained while rendering and their byte offsets map back onto
`{blockIndex, byteOffset}`; squiggles are drawn only for blocks the viewport has
laid out.

Right-click offers the suggestions. A host that has its own context menu takes
the click first and puts them inside it:

```cpp
editor->onContextMenu = [this](const UCEvent& event) {
    return ShowMyOwnMenu(event);   // true = consumed, no built-in popup
};
```

This is the same contract `UltraCanvasTextArea` offers, and UltraTexter uses it
so a right-click in a `.docx` tab gives one menu rather than two.

## Undo

Undo is command-based, not snapshot-based: a step records the blocks an edit
replaced and what it replaced them with, so its cost is the edit rather than the
document (and the media store is never copied). Consecutive keystrokes coalesce,
so a typed word undoes in one go; moving the caret ends the run.

```cpp
if (editor->CanUndo()) editor->Undo();
editor->GetEditor().BreakUndoCoalescing();   // force a new step
```

Undo goes back 200 steps by default; `GetEditor().SetMaxUndoSteps(n)` changes
that (0 = unlimited).

## Clipboard

Copy puts the selection's plain text on the system clipboard and keeps the
styled blocks in a process-local buffer. A paste whose clipboard text still
matches what was copied restores the formatting too, so copy/paste **inside the
application preserves formatting**; a paste from another application arrives as
plain text.

Full cross-application rich paste needs per-MIME clipboard transport
(`text/html`), which `UltraCanvasClipboardBackend` does not carry yet — see
[`WYSIWYGElementInvestigation.md`](WYSIWYGElementInvestigation.md) §4.

## Style

```cpp
RichTextEditStyle style = editor->GetStyle();
style.baseFont.fontFamily = "Georgia";
style.baseFont.fontSize = 13.0;
style.headingSizeMultipliers = {2.0f, 1.6f, 1.35f, 1.2f, 1.1f, 1.0f};
style.selectionColor = Color(180, 212, 253);
style.listIndent = 28.0f;
editor->SetStyle(style);
```

A run with no `fontFamily` or `fontSizePt` of its own inherits `baseFont` —
that is what "inherit" means in `UCRichDocument`, and it is why a document
authored elsewhere adopts the host application's typography until the user
overrides it.

## Paragraph layout from documents

A block read from `.odt`, `.docx` or `.doc` carries its paragraph geometry
(see `RichDocBlock` in `UltraCanvasRichDocument.h`), and the element lays it
out:

- **Indents:** `leftIndentPt` / `rightIndentPt` narrow the text column;
  `firstLineIndentPt` moves the first line, and when negative gives a hanging
  indent. List items keep `style.listIndent` instead.
- **Spacing:** when either neighbour states spacing, the gap between two blocks
  is `spaceAfterPt + spaceBeforePt`. Otherwise it is `style.blockSpacing`, so a
  document built from Markdown looks as before.
- **Line spacing:** `lineSpacing` (1.5 = one and a half lines).
- **Tab stops:** `tabStops` (left, centre, right, decimal) measured from the
  text column's edge, then every `UCRichDocument::defaultTabStopPt` (or
  `style.defaultTabStop` when the document states none).

- **List labels:** an ordered item draws `RichDocListLabel()` - its
  `numberFormat` (1, 01, a, A, i, I) inside its `numberTemplate` (`"%1.%2)"`
  gives "1.2)") - and an unordered one its `bulletText`, else
  `style.bulletCharacters`. A level's text starts after its widest label, so
  "(iii)" and "(iv)" line up. Enter keeps the label format; indenting or
  outdenting an item takes the format of that level in the same list.
- **Highlight, line heights, paragraph frames:** a run's `highlightColor` is
  drawn behind its text. `lineHeightPt` sets each line's height, exactly or
  as a minimum (`lineHeightAtLeast`). A paragraph's frame and fill are drawn
  3 pt outside its text, and that room is added to the space around it;
  consecutive paragraphs with the same frame form one box.
- **Table size and cells:** a document table takes its own width
  (`tableWidthPt`, or `tableWidthPercent` of the column, never more than the
  column) and place (`tableAlign`, `tableIndentPt`). A cell's text sits inside
  its `padding*Pt` (default 4 px at the sides, 2 px above) and at the cell's
  top, middle or bottom (`verticalAlign`); drawing, caret and clicks all use
  the same text origin.
- **Table frames:** a table read from a document
  (`RichDocBlock::tableBordersFromDocument`) is drawn with its cells' own
  borders (`RichTableCell::borderTop` … `borderRight`, width and colour) and
  `backgroundColor`, and its rows abut so the lines are continuous. A side with
  no border draws nothing when read-only, and a faint `style.tableGuideColor`
  guide when editable. Tables from Markdown or built in the editor keep the
  `style.tableBorderColor` grid. Rows and columns inserted into a framed table
  copy their neighbour's frame and fill.
- **Border line styles:** each `RichBorder` has a `style` - `Solid`,
  `Dotted`, `Dashed` or `Double` - read from and written to ODT and DOCX
  (read from DOC). A double border is drawn as two thin lines together as
  wide as the border.

Lengths are points and are drawn at 96/72 pixels per point - the scale run
font sizes get from Pango at the 96 DPI every context is pinned to - so
indents, tab stops, spacing and table widths stay in proportion to the text. Enter gives the
new paragraph the geometry of the one it was split from.

## Read-only rendering

```cpp
editor->SetReadOnly(true);       // no caret, no keys; still selectable and scrollable
```

This is the shortest path to a faithful `.odt`/`.docx` preview pane.

## Page view

```cpp
RichTextEditStyle style = editor->GetStyle();
style.padding = 0.0f;            // the pages bring their own margins
editor->SetStyle(style);
editor->SetPageView(true);       // like Writer's print layout
int pages = editor->GetPageCount();
```

Page view draws the document's pages (`UCRichDocument::page`: size,
margins, header and footer distances - A4 with 2 cm margins when the
document states none) on a desk (`style.deskColor`), centred, `style.pageGap`
apart. The text column is the page's, between its side margins, so a table
sized for the page's column fills it. Blocks go onto pages in order, and a
page break starts a new one. A paragraph that does not fit in what is left of
a page continues on the next, broken between lines with widow and orphan
control (at least two lines stay together at the foot of one page and the
top of the next; a paragraph of three lines or fewer moves whole). A table
continues between rows - never through a cell spanning rows - and its
leading header rows (`RichTableRow::header`: Word's *repeat as header row*,
ODF's `table:table-header-rows`, a Markdown table's first row) are repeated at
the top of each page it continues on. A heading is kept with the start of the
paragraph after it. A block taller than a whole page is still broken, between
any two lines. The space between two blocks is dropped at the top of a page.
Caret, clicks, selection, spell marks and pictures all follow a block's pieces
across the page gap.

Each page carries its header and footer (`UCRichDocument::FurnitureForPage`:
`firstPageFurniture` on page one when `firstPageDiffers`, `pageFurniture`
otherwise), with page number and page count fields
(`RichTextRun::field`) filled in for that page. The body starts below the
top margin, or below the header when the header is taller than the margin
leaves room for; the footer likewise.

**Editing a header or footer.** Double-click a page's header or footer - or
its top or bottom margin, to make one - or call `EditHeader(page)` /
`EditFooter(page)`. The body is then shown pale behind it, a dashed rule marks
the header's edge, and everything (typing, formatting, pictures, tables, page
fields, undo) acts on the header or footer; a header growing a line pushes the
body down as it is typed. Escape, a click in the body or
`FinishHeaderFooterEditing()` goes back. A document whose first page differs
edits the first page's own header on page 0. The changes go into
`UCRichDocument::pageFurniture` / `firstPageFurniture` as they are made, so
`GetDocument()` (which is the document, not the header, during editing) can be
saved at any moment; undo within the header works while it is edited, and the
edit as a whole is not an undo step of the body. `onHeaderFooterEditingChanged`
tells a host when to retarget its toolbar. Outside page view, the one header
and footer above and below the body are edited the same way.

Page fields work in the body too: `InsertPageNumberField()` and
`InsertPageCountField()` put one at the caret, and in page view each shows
the page its paragraph landed on (the value is kept in the run's text, so a
save or plain-text copy carries it; DOCX and ODT save it as a real field).
Updating them is not an edit - no undo step, and the document stays
unmodified.

An editable page view marks the corners of each page's text area
(`style.pageMarginGuideColor`) and shows page breaks as dashed rules; a
read-only one shows neither.

Outside page view the text fills the element, and a document's first-page
header and footer are drawn above and below the body.

## PDF and printing

```cpp
std::string error;
editor->ExportToPdf("/home/me/report.pdf", error);    // or ExportToPdf(bytes, error)
```

The document goes out as page view lays it out - whether or not the element is
in page view - with headers, footers, page numbers, floating pictures and
formulas, as vectors with real (selectable, searchable) text, and without
anything that belongs to editing: no selection, caret, margin corners, dashed
page breaks or borderless-cell guides. The element needs no window for it.
`UltraCanvasPdfSurface` (the PDF writer behind it) draws any element into a
PDF, and `PrintDocumentWithDialog(name, pdfBytes, "application/pdf", window)`
prints one - which is what UltraTexter's Print does for a word-processing tab,
and File > Export as PDF writes.

## Zoom and scrolling sideways

```cpp
editor->SetZoom(1.5f);            // 150%; 0.25 to 5, Ctrl+wheel too
editor->onZoomChanged = [](float zoom) { /* update a zoom box */ };
```

Everything is drawn scaled: text, pictures, pages. Outside page view the text
rewraps to the zoomed width; in page view the page is simply larger, and when
it (with the desk either side) is wider than the element a horizontal
scrollbar appears - drag it, Shift+wheel, or let the caret take the view
along. `GetHorizontalScrollOffset` / `SetHorizontalScrollOffset` read and set
it. UltraTexter's zoom box sets a word-processing tab's zoom.

## What is not implemented yet

Honest limits of this first version — none of them silently misbehave:

- **No pre-edit (IME composition) display.** Committed text arrives correctly;
  an inline composition string needs an event the framework does not have yet
  (the same limit applies to every text widget today).
- **Cross-application rich paste** — see Clipboard above.

## Related

- `Apps/DemoApp/UltraCanvasWYSIWYGExamples.cpp` — the demo application's
  **WYSIWYG Editor** page (Document support): three toolbars built from real
  elements, a sample document carrying formatting Markdown cannot spell, and
  open/save through `UCWordDocumentIO`
- [`WYSIWYGElementInvestigation.md`](WYSIWYGElementInvestigation.md) — why this
  element exists, what was measured, and the phased plan it follows
- [`ODT-DOCX-Support-Proposal.md`](ODT-DOCX-Support-Proposal.md) — the format
  layer that produces and consumes `UCRichDocument`
- [`UltraCanvasTextAreaExamples.md`](UltraCanvasTextAreaExamples.md) — the plain
  text and Markdown surface
- [`UltraCanvasUIElements.md`](UltraCanvasUIElements.md) — the element catalogue
