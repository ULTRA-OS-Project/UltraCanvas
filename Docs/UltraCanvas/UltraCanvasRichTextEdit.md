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

### Structure

```cpp
editor->InsertHorizontalRule();
editor->InsertPageBreak();
editor->InsertImageFromFile("/path/diagram.png", "Architecture diagram");
editor->InsertImageFromMemory("chart.png", "image/png", bytes, "Q3 revenue");
```

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

## Read-only rendering

```cpp
editor->SetReadOnly(true);       // no caret, no keys; still selectable and scrollable
```

This is the shortest path to a faithful `.odt`/`.docx` preview pane.

## What is not implemented yet

Honest limits of this first version — none of them silently misbehave:

- **A table's own structure is not edited yet.** Merged cells load, save and
  lay out correctly, but *making* them does not: adding or removing rows and
  columns, merging and splitting cells, and selecting across several cells at
  once are not there — and neither is inserting a new table, which is why
  UltraTexter's Insert Table button stays disabled for these documents.
- **Images are not resized interactively** (insert and delete work).
- **Math runs (`RichTextRun::math`) render as their LaTeX source**, not as
  typeset formulas. `UltraCanvasInlineMath` already does the typesetting for the
  TextArea's Markdown mode and is the intended path.
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
