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

**Positions are `{blockIndex, byteOffset}`** — the offset is into the block's
*concatenated run text*, never a `{run, offset}` pair. Applying a format splits
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

## Building the toolbar

The element draws **no chrome**. Build the toolbar from real elements (framework
rule — see [AGENTS.md](../../AGENTS.md)) and drive it from `GetFormatState()`,
which reports every attribute as on, off or *mixed* across the selection:

```cpp
auto toolbar = CreateToolbar("format-bar", 0, 0, 800, 32);
auto boldButton = CreateButton("bold", 1, 0, 0, 28, 28, "B");
boldButton->onClick = [editor]() { editor->ToggleBold(); };

auto styleBox = CreateDropdown("style", 0, 0, 140, 28);
styleBox->AddItem("Body text");
styleBox->AddItem("Heading 1");
styleBox->AddItem("Heading 2");
styleBox->onSelectionChanged = [editor](int index) {
    editor->SetHeadingLevel(index);          // 0 = body text
};

// Keep the toolbar in step with the caret.
editor->onSelectionChanged = [editor, boldButton, styleBox]() {
    RichCharFormatState state = editor->GetFormatState();
    boldButton->SetToggled(RichCharFormatState::IsOn(state.bold));
    styleBox->SetSelectedIndex(editor->GetCurrentHeadingLevel());
};
```

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

- **Tables render but are not edited in place.** A table block draws with its
  cells laid out; the caret treats it as one indivisible block. Editing inside
  cells is the next phase.
- **Images are not resized interactively** (insert and delete work).
- **Math runs (`RichTextRun::math`) render as their LaTeX source**, not as
  typeset formulas. `UltraCanvasInlineMath` already does the typesetting for the
  TextArea's Markdown mode and is the intended path.
- **No spell checking yet** — `UltraCanvasSpellChecker` integrates the same way
  it does in the TextArea.
- **No pre-edit (IME composition) display.** Committed text arrives correctly;
  an inline composition string needs an event the framework does not have yet
  (the same limit applies to every text widget today).
- **Cross-application rich paste** — see Clipboard above.

## Related

- [`WYSIWYGElementInvestigation.md`](WYSIWYGElementInvestigation.md) — why this
  element exists, what was measured, and the phased plan it follows
- [`ODT-DOCX-Support-Proposal.md`](ODT-DOCX-Support-Proposal.md) — the format
  layer that produces and consumes `UCRichDocument`
- [`UltraCanvasTextAreaExamples.md`](UltraCanvasTextAreaExamples.md) — the plain
  text and Markdown surface
- [`UltraCanvasUIElements.md`](UltraCanvasUIElements.md) — the element catalogue
