# A WYSIWYG Editing Element — Investigation

Status: **Investigation — now partly implemented.** This was the "own design
round" that [`ODT-DOCX-Support-Proposal.md`](ODT-DOCX-Support-Proposal.md)
defers Phase 5 to ("the Phase-5 interactive styled-run editor … needs its own
design round"). The analysis below is preserved as written; what has since been
built against it is recorded in §11, and the element's own documentation is
[`UltraCanvasRichTextEdit.md`](UltraCanvasRichTextEdit.md).

The question asked was: *what would it take to implement a WYSIWYG UltraCanvas
element?* The answer has three parts:

1. **The framework has no WYSIWYG element, and the thing standing in for one
   has a hard ceiling.** `UltraCanvasTextArea`'s `MarkdownHybrid` mode renders
   formatting but edits *markup*: its document is `std::vector<std::string>`
   lines, so anything Markdown cannot spell (a font, a size, a colour, an
   alignment) cannot be typed, and the cursor line drops back to raw source by
   design. §2.
2. **Every hard part is already built.** The document model (`UCRichDocument`),
   the styled-text engine (`ITextLayout` + `TextAttributeFactory`, one
   implementation shared by every platform), the app-wide caret, spell
   checking, the format readers/writers and the file-loader front door all
   exist and line up almost 1:1 with what the element needs. §3.
3. **What is genuinely missing is small and nameable**: a block/offset position
   model, a per-block layout cache, command-based undo, viewport
   virtualization, a rich clipboard flavour and an HTML→`UCRichDocument`
   direction. §5–§6 size them; §7 phases them.

Everything below is measured against the tree at `0.8.47` (2026-09-14), not
assumed. The word "WYSIWYG" occurs exactly once elsewhere in the repository
(`CHANGELOG.md:4337`), about vector text matching Xara's own output — an
unrelated sense.

Author: UltraCanvas Framework
Last Modified: 2026-09-14

---

## 1. What "WYSIWYG" has to mean here

Three different things get called WYSIWYG, and the framework's position on each
is different:

| Reading | Status |
|---|---|
| **Render** a formatted document faithfully | **Done, twice.** `MarkdownHybrid` for Markdown; `HTMLReader` + `CSSLayout` for HTML/EPUB. |
| **Edit markup with a live preview** (Typora-style hybrid) | **Done.** `UltraCanvasTextArea::TextAreaEditingMode::MarkdownHybrid`. |
| **Edit the formatted result directly** — the caret sits in rendered text, bold is a state of a selection rather than two asterisks | **Missing.** This is the subject of this document. |

The third reading is the one a word processor means, and it is what Texter
needs to stop being "a text editor that opens `.docx`" (the framing of the ODT
proposal's Phase 5). It is also the only one of the three that changes the
*document model*: the first two can be driven from a string; the third cannot.

## 2. Where the current editing surface stops

`UltraCanvasTextArea` (1,374-line header, 3,759-line implementation) is the
most capable text surface in the tree, and the reasons it cannot be grown into
the answer are structural rather than cosmetic.

- **The document is plain text.** Content is `std::string textContent` plus
  `std::vector<std::string> lines`; the three modes
  (`UltraCanvasTextArea.h:285-289`) are `PlainText`, `MarkdownHybrid`, `Hex`,
  and all three are views of the same byte string. There is no place to put
  "this range is 14 pt Georgia, red" other than characters in that string.
- **Formatting is re-derived every frame from markup.** `ParseInlineMarkdownRuns`
  (`UltraCanvasTextArea.h:967`) walks a raw line, strips markers, and emits
  `InlineRun` spans (`:947-956`: Bold, Italic, Code, Strike, Sub/Superscript,
  Math, Link, Image, Footnote) in *visible* byte coordinates, plus a `cpMap`
  (`:329-368`) translating visible ↔ source positions. That machinery is
  excellent, and it is also the tell: every styling decision is a function of
  the source text, so styling that has no source spelling cannot exist.
- **The cursor line is deliberately not WYSIWYG.** In hybrid mode the line
  under the caret is rebuilt as a raw `PlainLine` and stashed
  (`UltraCanvasTextArea.h:1031`, `:1131-1136`). That is the right call for
  editing Markdown, and it is the opposite of the requirement here.
- **Positions are line/column.** `LineColumnIndex` (`:292`) is line index +
  codepoint index. A rich document's caret has to address a paragraph and an
  offset inside it; paragraphs wrap to many visual lines and a table cell is
  not a line at all.
- **Undo is a whole-document snapshot.** `TextState` holds a full copy of the
  text per step, capped at 100 (`:1151-1159`). Acceptable for text; not for a
  model that carries embedded media (`RichDocMedia::data`) — 100 keystrokes in
  a document with a 2 MB picture would hold 200 MB.
- **Layout is per logical line, for every line.** `lineLayouts`
  (`:1022`) is resized to `lines.size()` and any missing entry is built
  (`core/UltraCanvasTextArea.cpp:3315-3338`) — there is no viewport
  virtualization. A rich element inherits this problem and should not inherit
  this answer.

None of this is a defect in `UltraCanvasTextArea`. It is a correctly-built
plain-text editor with a Markdown preview, and adding a fourth mode with a
second document model, a second position type, a second undo strategy and a
second layout unit would roughly double the class while putting the
framework's most heavily used editing widget at risk. §5 treats that as a
rejected option rather than an open question.

## 3. What already exists and can be used unchanged

### 3.1 The document model — `UCRichDocument`

`UltraCanvas/include/Plugins/Documents/Word/UltraCanvasRichDocument.h` (153
lines) is exactly the model a WYSIWYG element needs, and it was written to be
UI-free ("only std types, no framework headers"):

- `RichTextRun` — text plus bold, italic, underline, strikethrough, code,
  subscript, superscript, math (LaTeX source), `linkTarget`, `fontFamily`,
  `fontSizePt`, `color`, `lineBreakBefore`; and — importantly for an editor —
  `HasSameFormatting()`, which is the run-coalescing predicate a formatting
  command needs after it splits runs at a selection boundary.
- `RichDocBlock` — Paragraph, Heading (1–6), ListItem (ordered + nesting
  level), CodeBlock, BlockQuote, Table (rows/cells with spans), Image
  (`mediaIndex`, alt text, size), HorizontalRule, PageBreak, MathBlock, each
  with `RichTextAlign`.
- Document level — metadata, a deduplicating media store (`AddMedia`), and
  serializers `ToMarkdown` / `FromMarkdown` / `ToHTML` / `ToPlainText`.

It is already the hub of a working format layer: ODT (1,274 lines), DOCX (966),
legacy `.doc` import (421), MathML/OMML→LaTeX (742), the LaTeX document reader,
and `UCWordDocumentIO` dispatch — all compiled into the library
unconditionally (`UltraCanvas/CMakeLists.txt:744-750`), with
`UltraCanvasFileLoader::LoadTextDocument` as the front door and
`Tests/WordFormatsTest.cpp` guarding round-trips.

**The model needs no new fields for a first editor.** The gaps that do exist
(background colour, per-paragraph spacing/indent, named styles, footnotes,
tracked changes) are fidelity items, not blockers, and each is additive.

### 3.2 The text engine — `ITextLayout` / `TextAttributeFactory`

This is the decisive finding. `UltraCanvasRenderContext.h:763-853` exposes a
Pango-class styled-text layout with everything caret arithmetic needs:

- `XYToIndex`, `IndexToPos`, `IndexToBaseline`, `IndexToLineX`,
  `GetCursorPos`, **`MoveCursorVisually`** (grapheme/bidi-correct arrow
  movement), `GetLineByteRanges`, `GetLayoutExtents`, `GetBaseline`.
- Wrapping, alignment, justification, indent, line spacing, tab stops.
- `InsertAttribute` / `ChangeAttribute` and
  **`UpdateAttributesAccordingToText(bytePos, added, removed)`** — attribute
  ranges follow an edit instead of being rebuilt.

`TextAttributeFactory` (`:655-726`) covers every `RichTextRun` field with a
direct mapping — this table *is* the block-layout builder:

| `RichTextRun` field | Attribute |
|---|---|
| `bold` | `CreateFontWeight` |
| `italic` | `CreateFontStyle(FontSlant)` |
| `underline` | `CreateUnderline` (+ `CreateUnderlineColor`) |
| `strikethrough` | `CreateStrikethrough` |
| `code` | `CreateFontFamily` (+ `CreateBackground`) |
| `subscript` / `superscript` | `CreateRise` + `CreateScale` |
| `fontFamily` / `fontSizePt` / `color` | `CreateFontFamily` / `CreateFontSize` / `CreateForeground` |
| `linkTarget` | `CreateForeground` + `CreateUnderline` + a hit rect |
| `math` | U+FFFC + `CreateShape(width, ascent, descent)` — the technique `MarkdownInlineMath` (`UltraCanvasTextArea.h:56-61`) already uses for `$…$` |

There is **one** implementation — `libspecific/Cairo/UCTextLayout.{h,cpp}`,
built on every platform (`UltraCanvas/CMakeLists.txt:1001-1003`) — so text
measurement, shaping, bidi and hit-testing behave identically on Linux,
Windows, macOS, WASM and ULTRA OS. A rich editor written against this
interface is cross-platform on day one, which is the single biggest reason the
cost estimate in §7 is as low as it is.

### 3.3 Services that transfer as-is

- **Caret** — `UltraCanvasCaret` is application-wide, composited over the
  window surface, and blinks without re-rendering any widget
  (`UltraCanvasCaret.h:20-66`). The new element implements the same three-call
  protocol (`Show` from `Render`, `Hide` on focus loss, `ResetBlink` on typing).
- **Spell check** — `ISpellCheckBackend` with per-OS backends (enchant,
  Windows `ISpellChecker`, `NSSpellChecker`, Hunspell fallback). The
  integration pattern and its geometry test already exist
  (`Tests/TextAreaSpellCheckTest.cpp`).
- **Scrolling** — `UltraCanvasScrollbar` + `UltraCanvasSmoothScroll`.
- **Layout/participation** — `UltraCanvasUIElement` with `Render`, `OnEvent`,
  `Arrange` under CSSLayout; the element is externally sized exactly as the
  TextArea is (`UltraCanvasTextArea.h:466-470`).
- **Images** — embedded media decodes through the existing libvips path; the
  element draws a decoded bitmap for an Image block just as
  `ImageLineLayout` does today.
- **Math** — `UltraCanvasInlineMath` typesets, measures and draws at a
  baseline; `RichTextRun::math` and `RichBlockType::MathBlock` already carry
  the LaTeX.
- **Tables** — `TableLineLayout::cellsLayouts` (`UltraCanvasTextArea.h:402-413`)
  is the precedent for one `ITextLayout` per cell plus a column-width
  negotiation pass.
- **Registration** — `UCElementDescriptor` (`UltraCanvasElementPlugins.h:37-58`)
  lets the element declare `fileExtensions` (`odt`, `docx`, `doc`, `rtf`) so
  `FileLoader` can open a document straight into it.

### 3.4 The read-only rich path (and one stale note)

`HTMLReader` (parser, CSS subset, style resolver, `HTMLElementBuilder`) and
`UltraCanvasEBookViewer` **are in the build**
(`UltraCanvas/CMakeLists.txt:507-524`). The ODT proposal's remark that the
read-only `ToHTML()` view is "blocked on the eBook/HTML subsystem not yet being
part of the build" is out of date; that view is now only a wiring job.
`HTMLElementBuilder` turns a DOM into containers + labels laid out by
CSSLayout — excellent for reading, and §5 explains why it is the wrong
substrate for editing.

## 4. The gap, stated precisely

Everything needed that does **not** exist today:

1. **A position model** over blocks and byte offsets, and a selection over it.
2. **A per-block layout cache** — `RichDocBlock` → `ITextLayout` + attributes,
   with the run→attribute mapping of §3.2, and cell layouts for tables.
3. **Editing operations on the model** — insert/delete across block
   boundaries, split/merge paragraphs, run splitting and coalescing, list
   renumbering, table row/column operations.
4. **Command-based undo** with typing coalescing (not document snapshots).
5. **Viewport virtualization** — build layouts for visible blocks only, with
   a prefix-sum of block heights for scrolling.
6. **A rich clipboard flavour.** `UltraCanvasClipboardBackend`
   (`UltraCanvasClipboard.h:89-126`) transports only text, images and file
   lists. `ClipboardDataType::RichText` exists in the enum (`:20`) with no
   transport behind it, while the macOS backend already speaks `text/html`
   internally (`OS/MacOS/UltraCanvasMacOSClipboard.mm:351-381`). A generic
   per-MIME get/set is needed.
7. **HTML → `UCRichDocument`.** Only the `ToHTML` direction exists. Pasting
   from a browser or another word processor needs the inverse, which
   `HTMLReader`'s DOM makes tractable (DOM walk → blocks/runs) but which
   nobody has written.
8. **A composition (pre-edit) event.** `UCEvent` carries committed text only
   (`UltraCanvasEvent.h:38`, `:329`); XIM is initialised per application and a
   per-window XIC exists (`OS/Linux/UltraCanvasLinuxApplication.cpp:183-216`,
   `UltraCanvasLinuxWindow.cpp:425`), but no pre-edit string reaches the
   widget. CJK input therefore commits without an inline composition display.
   This is today's behaviour for every text widget, so it is not a regression
   — but a word processor is where users notice it.
9. **A layering fix.** The element must live in `UltraCanvas/{include,core}`
   (framework rule), while `UCRichDocument` lives under
   `Plugins/Documents/Word/`. A core element including a plugin header inverts
   the dependency.

## 5. Architecture options

**A. A fourth `TextAreaEditingMode` — rejected.** §2 is the argument: a second
document model, position type, undo strategy and layout unit inside a class
that is already 5,100 lines across header and implementation, and every
existing feature (search, bookmarks, hex, spell ranges, syntax highlighting)
would have to answer "and in Rich mode?". The blast radius covers Texter,
the Filer, every dialog that embeds a TextArea.

**B. A new element over `UCRichDocument` — recommended.** The model, the text
engine and the services are all in place; the new element owns only the four
things that are genuinely new (position model, block layouts, editing
commands, formatting UI). It composes rather than replaces: `UltraCanvasTextArea`
keeps plain text and Markdown, the new element takes styled documents, and
`UCRichDocument` is the bridge between them (`ToMarkdown`/`FromMarkdown`
already convert in both directions, so a document can move between the two
surfaces).

**C. An editable DOM via `HTMLElementBuilder` + CSSLayout — rejected.** It
looks attractive because the rendering already works, but the editing model is
wrong at the root: content becomes a tree of containers and labels, so there
is no single caret or selection spanning nodes, arrow-key movement across a
`<b>` boundary becomes a tree walk, and CSSLayout is a box layout engine, not
an inline-flow engine for editable text. Per-paragraph `ITextLayout` solves
all of that already. Keep this path for read-only viewing, where it is the
right answer.

## 6. Recommended shape (option B)

**Naming and placement.** `UltraCanvasRichTextEdit` in
`UltraCanvas/include/UltraCanvasRichTextEdit.h` +
`UltraCanvas/core/UltraCanvasRichTextEdit.cpp`, documented as
`Docs/UltraCanvas/UltraCanvasRichTextEdit.md` with a row in the catalogue
(`UltraCanvasUIElements.md`) — "edit a formatted document (fonts, colours,
lists, tables, images)". Avoid "WYSIWYG" in the type name: it names an era,
not a capability.

**Fix the layering first.** Move `UCRichDocument` to
`include/UltraCanvasRichDocument.h` + `core/UltraCanvasRichDocument.cpp`,
leaving readers/writers where they are. It is already framework-free, it is
already compiled unconditionally, and both the element (core) and the format
plugins then depend downward. This is a mechanical move plus include updates
in the six format files, `UltraCanvasFileLoader`, the viewers and Texter.

**Position model.**

```cpp
struct RichDocPosition {          // caret / selection endpoint
    int blockIndex = 0;           // index into UCRichDocument::blocks
    int byteOffset = 0;           // into the block's concatenated run text
    int cellIndex  = -1;          // >= 0 inside a table cell (row-major)
};
```

Offsets address the *concatenated* text of a block, not `{run, offset}`:
applying bold splits and coalesces runs constantly, and a caret must not move
when the run structure changes underneath it. The same concatenated string is
what the block's `ITextLayout` holds, so `XYToIndex`/`IndexToPos`/
`MoveCursorVisually` map to it directly with no translation layer — the
`cpMap` indirection the Markdown path needs (because markup is stripped) does
not arise here.

**Block layout cache.**

```cpp
struct BlockLayout {
    std::unique_ptr<ITextLayout> layout;      // null for Image / HorizontalRule
    Rect2Df bounds;
    std::vector<std::pair<int,int>> runByteRanges;   // run index -> [start,end)
    std::vector<MarkdownHitRect> hitRects;           // links, images (reuse the struct)
    std::vector<MarkdownInlineMath> inlineMath;      // math placeholders
    std::vector<std::unique_ptr<BlockLayout>> cells; // Table
};
```

Built by concatenating run text, then one `InsertAttribute(...SetRange(...))`
per run per attribute using the §3.2 table. Invalidate one block on a text
edit; invalidate all on a width change (as the TextArea does), but only
*rebuild* what the viewport needs.

**Editing commands.** One command type per operation —
`InsertText`, `DeleteRange`, `ApplyCharFormat`, `SetBlockType`,
`SetAlignment`, `SplitBlock`, `MergeBlock`, `InsertImage`,
`TableInsert/RemoveRow/Column` — each with `Apply`/`Revert` and a
`TryCoalesce(next)` so a typed word is one undo step. `ApplyCharFormat` splits
runs at the selection boundaries, edits the covered runs, then merges
neighbours with `HasSameFormatting()`. Undo cost becomes proportional to the
edit, not to the document, which is the point of not copying §2's snapshot
approach.

**Rendering.** Same three-phase discipline as the TextArea: `Arrange` flags,
`Render` lazily (re)builds only the layouts intersecting the dirty rect plus a
margin, draws selection as a background rect per layout line
(`GetLineByteRanges`), then `UltraCanvasCaret::Show` with the rect from
`IndexToPos`. Keep a prefix sum of block heights so scrolling is O(log n) and
a document of 10,000 paragraphs does not pay for 10,000 layouts.

**Formatting UI belongs to the app, built from existing elements.** Per
AGENTS.md, the toolbar is `UltraCanvasToolbar` + `UltraCanvasDropdown` (font,
size, style) + `UltraCanvasButton` toggles + `UltraCanvasColorPicker` — not
painted by the element. The element exposes the state
(`GetFormatAtSelection()` returning a merged `RichTextRun` with per-field
"mixed" flags) and the commands; Texter draws the chrome.

**File I/O stays out of the element**, exactly as with the TextArea: apps use
`UltraCanvasFileLoader::LoadTextDocument` / `UCWordDocumentIO`, then
`SetDocument(std::shared_ptr<UCRichDocument>)`. This also keeps the
"formatting will be reduced" warning in the app layer where it already lives.

**Clipboard.** Add to `UltraCanvasClipboardBackend`:

```cpp
virtual bool GetClipboardData(const std::string& mimeType, std::vector<uint8_t>& out);
virtual bool SetClipboardData(const std::vector<std::pair<std::string, std::vector<uint8_t>>>& flavors);
```

with default implementations that map `text/plain` onto the existing text
calls, so no backend breaks. Copy publishes `text/html` (`ToHTML()`, which
already inlines images as data URIs) alongside `text/plain` (`ToPlainText()`);
paste prefers `text/html` through the new HTML→`UCRichDocument` importer and
falls back to plain text.

## 7. Phases and rough size

| Phase | Scope | New/changed code |
|---|---|---|
| **0. Foundations** | Move `UCRichDocument` to core; `RichDocPosition`; `BlockLayout` builder + run→attribute mapping; **read-only** rendering of a document (paragraphs, headings, lists, quotes, code, rules, images, math) | ~1,200 lines |
| **1. Caret & selection** | Hit-testing, click/drag selection, word/line/document motion via `MoveCursorVisually`, Home/End/PageUp/Down, selection painting, shared-caret protocol, focus | ~700 |
| **2. Text editing** | Insert/delete, split/merge blocks, run splitting + coalescing, command undo/redo with coalescing, `onTextChanged`/dirty flag | ~900 |
| **3. Formatting** | Character formats (bold … colour/font/size), paragraph formats (heading, alignment, list level/ordered, quote), merged-state query, Texter toolbar wiring | ~700 element + ~400 app |
| **4. Structure** | Tables (cell navigation, row/column insert/remove, width negotiation), image insert/resize/drag-drop, page breaks, math block editing | ~900 |
| **5. Interchange** | Clipboard MIME flavours across four backends; HTML→`UCRichDocument` importer over `HTMLReader` | ~700 |
| **6. Scale & polish** | Viewport virtualization + height prefix sums, spell check, find/replace, large-document tests, `UCElementDescriptor` registration, docs + catalogue row | ~600 |

Roughly 6,000–6,500 lines for a credible word-processing surface — comparable
to the TextArea itself, and less than the format layer that already feeds it.
Phases 0–2 are the risky ones; 3–6 are mostly mechanical once the position
model and command layer are right.

Optional follow-ups, deliberately out of the first pass: named styles, footnote
layout, page view with margins/headers/footers, tracked changes, an inline
pre-edit display (§4.8), RTF import.

## 8. Validation

- **Model-level tests** (no display needed), in the shape of
  `Tests/WordFormatsTest.cpp`: command apply/revert invariants
  (`Revert(Apply(d)) == d` for every command type), run coalescing after
  arbitrary format applications, position stability across edits, list
  renumbering.
- **Geometry/interaction tests under Xvfb**, in the shape of
  `Tests/TextAreaSpellCheckTest.cpp` (real window + render context, skip
  rather than fail when there is no display): click→caret position, arrow
  movement across run and block boundaries, selection rectangles, bold on a
  selection that starts and ends mid-run.
- **Round-trip fidelity**: load fixtures through the ODT/DOCX readers, edit
  programmatically, write back, reload, compare — the corpus and the
  LibreOffice/Word validation loop already exist from the ODT/DOCX work.
- **Scale**: a 10,000-block document must scroll without building every
  layout; assert the built-layout count stays bounded.

## 9. Two rules this work must not break

- **Build the chrome from elements.** The element itself is the legitimate
  exception (an editor owns its buffer and caret, `AGENTS.md`), but its
  toolbar, dialogs, colour and font pickers are not: use
  `UltraCanvasToolbar`, `UltraCanvasDropdown`, `UltraCanvasColorPicker`,
  `UltraCanvasModalDialog`. `scripts/check_ui_reuse.py` runs in CI and its
  baseline is empty; keep it that way.
- **Do not fork the model.** One `UCRichDocument` serves the readers, the
  writers, the Markdown surface, the HTML view and the new editor. A second
  "editor-only" model is how fidelity bugs start.

## 11. What has been implemented

Phases 0–3 of §7 plus the process-local half of Phase 5 have landed; the
recommendation in §6 was followed as written, including the layering fix.

| Piece | Where | State |
|---|---|---|
| `UCRichDocument` moved into core | `include/UltraCanvasRichDocument.h`, `core/UltraCanvasRichDocument.cpp` | Done — the format readers/writers stay in `Plugins/Documents/Word/` and now depend downward |
| Editing core (positions, commands, formatting, undo), UI-free | `include/UltraCanvasRichDocumentEditor.h`, `core/UltraCanvasRichDocumentEditor.cpp` | Done |
| The element (block layouts, rendering, input, caret, scrolling, clipboard) | `include/UltraCanvasRichTextEdit.h`, `core/UltraCanvasRichTextEdit.cpp` | Done |
| Model-level tests, no display needed | `Tests/RichTextEditorTest.cpp` | Done |
| Element tests against a real render context (Xvfb) | `Tests/RichTextEditElementTest.cpp` | Done |
| First application: UltraTexter opens `.odt`/`.docx`/`.doc` in the element | `Apps/Texter/UltraCanvasTextEditor.cpp` | Done — see below |

`RichDocPosition` is `{blockIndex, byteOffset}` as designed; undo is the
block-span step of §6 with typing coalescing; layouts are built only for blocks
near the viewport, with the rest carrying an estimated height until they scroll
in. Every `RichTextRun` field maps onto the attribute named in §3.2's table.

### UltraTexter as the first host

UltraTexter gained a third `DocumentKind` (`RichDocument`) beside `Text` and
`Pdf`. A word-processing file is handed to the element as a shared
`UCRichDocument` and saved straight back through `UCWordDocumentIO`, so the
Markdown detour — `ToMarkdown` on load, `FromMarkdown(GetText())` on save — is
gone from that path, and with it the lossy round trip §2 described.

The element replaces the text area *inside* the tab's `editorArea` rather than
replacing the whole tab content (which is what the PDF view does), so the
formatting toolbar and the search-bar slot above it stay in the tree. The
toolbar's buttons now go through one `ApplyFormatCommand` dispatcher: in a
Markdown tab a button inserts markup, in a word-processing tab it calls the
element's formatting API, and its pressed state comes from `GetFormatState()`
with `Mixed` shown as not-pressed.

What the host does not have yet follows the element's own limits: find/replace
and go-to-line (the element has no search surface), spell checking, and
autosave backups (a backup is a text file that recovery reopens as a text tab,
so there is nothing useful to write). Each is skipped explicitly rather than
silently operating on the detached, empty text area.

Still open from §4 and §7, and stated as limits in the element's documentation
rather than hidden: in-place table cell editing and interactive image resizing
(Phase 4), the clipboard MIME flavours and the HTML→`UCRichDocument` importer
that cross-application rich paste needs (Phase 5 proper — copy/paste *inside*
the application does keep formatting, through a process-local buffer), typeset
math runs, spell checking, and the pre-edit/composition event (§4.8), which no
text widget in the framework has yet.

## 10. References

- [`ODT-DOCX-Support-Proposal.md`](ODT-DOCX-Support-Proposal.md) — the format
  layer and the Phase-5 deferral this document answers
- `UltraCanvas/include/Plugins/Documents/Word/UltraCanvasRichDocument.h` — the model
- `UltraCanvas/include/UltraCanvasRenderContext.h:648-853` — `ITextAttribute`,
  `TextAttributeFactory`, `ITextLayout`
- `UltraCanvas/include/UltraCanvasTextArea.h` — the current editing surface
- `UltraCanvas/include/UltraCanvasCaret.h` — the caret protocol
- [`UltraCanvasRichTextEdit.md`](UltraCanvasRichTextEdit.md) — the element this
  investigation led to
- [`UltraCanvasUIElements.md`](UltraCanvasUIElements.md) — the element catalogue
- [`UltraCanvasLaTeXDocumentReader.md`](UltraCanvasLaTeXDocumentReader.md),
  [`UltraCanvasTextAreaExamples.md`](UltraCanvasTextAreaExamples.md)
