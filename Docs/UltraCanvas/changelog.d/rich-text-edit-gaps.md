- **UltraCanvasRichTextEdit: border line styles.** `RichBorder` has a
  `style` (`RichBorderStyle::Solid`, `Dotted`, `Dashed`, `Double`). The ODT,
  DOCX and DOC readers keep a border's line type (Word's ~25 types and ODF's
  keywords map to the nearest of the four: thick-thin pairs to Double,
  dash-dot to Dashed), the ODT and DOCX writers write it back, the HTML
  serializer emits it as CSS, and the element draws it - a double border as
  two thin lines, dotted and dashed as dashed strokes. They used to become a
  solid line of the same width.
- **Check lists in UCRichDocument.** A list item can be a to-do item
  (`RichDocBlock::checkbox`, `checked`). The element draws a box in place of
  the bullet and ticks it on a click (undoable); `ToggleCheckList()` and
  `ToggleCheckedAtCaret()` on the element, `ToggleCheckList()` and
  `ToggleChecked(block)` on the editing core. Markdown reads and writes
  GitHub's `- [ ]` / `- [x]`, HTML writes a disabled check box, plain text
  `[ ]` / `[x]`. ODT and DOCX have no check list, so the writers put a ☐ or
  ☒ before the item's text and the readers turn a paragraph or list item
  opening with ☐ ☑ ☒ back into one - which is also how check boxes in
  documents written by Word arrive (`UCRichDocument::ReadCheckboxPrefixes`).
- **Math runs are typeset.** A `RichTextRun::math` run is drawn as a formula
  through `UltraCanvasInlineMath` (the LaTeX module), on the line's baseline,
  and never broken across lines; while the caret is inside it the element
  shows the LaTeX source, which is what is being edited. A `MathBlock` is
  typeset in display style, centred in the column, until the caret enters it.
  Without the LaTeX module both keep showing their source, as before.
- **`TextAttributeFactory::CreateAllowBreaks(bool)`**: keeps a range of a text
  layout on one line.
- **`UCRichDocumentEditor::SetMaxUndoSteps`** makes the 200-step undo limit a
  setting (0 = no limit).
- **Fixed: inline images in the rich text element were drawn again at every
  relayout.** The block layout never cleared its list of placed pictures, so
  each edit or caret move added another copy of every picture in the block.
- **Page number and page count fields in the body are numbered in page
  view**, each with the page its paragraph is on; they used to keep the
  number they were saved with. `InsertPageNumberField()` /
  `InsertPageCountField()` on the element (`InsertField` on the editing
  core) insert one. Text typed next to a field or an inline picture no longer
  inherits being a field or a picture - it takes only the neighbour's
  character formatting.
- **A selection can span table cells.** Dragging or Shift+arrowing from one
  cell into another selects a block of whole cells (grown to cover merged
  cells). Delete empties them, typing replaces them, formatting and alignment
  apply to all of them (`SetAlignment` in a table now sets the cells' own
  alignment rather than doing nothing), Copy copies them as a table, and
  `MergeSelectedCells()` merges them in one step. Pasting a table into a cell
  fills the grid from there; pasting paragraphs into a cell keeps them in it.
  A selection dragged out of a table stops at its edge, one dragged into a
  table from outside takes the whole table. Editing core: `HasCellSelection`,
  `GetCellSelectionRect`, `SelectedCells`, `SelectCellRange`,
  `MergeSelectedCells`.
- **Drag and drop in the rich text element.** Dragging the selection moves
  it (Ctrl at the drop copies it) with a drop caret showing where it lands,
  as one undo step, the moved text left selected; image files dropped from
  another application are inserted at the drop point (`onFilesDropped` lets a
  host take them). Editing core: `MoveRange(range, target, copy)`.
- **Autoformat as you type** (`RichAutoFormatOptions`, on by default in the
  element): smart quotes, em and en dashes from `--`, `…` from `...`, © ® ™ →
  ← ⇒, lists from `1. ` / `a) ` / `- ` / `[ ] `, headings from `#`, quotes
  from `> `, and a rule from `---` + Enter. Each correction is a separate undo
  step. Code and formulas are left alone, and so is pasted text.
- `UltraCanvasRichTextEdit::InsertImageFromFile` / `InsertInlineImageFromFile`
  opened the path with `std::ifstream(path)`, which on Windows reads a UTF-8
  name through the ANSI code page; they go through `PathFromUtf8` now.
- **Pictures in the rich text element can be selected, resized and
  described.** A click selects a picture (frame and eight handles); dragging a
  corner resizes it in proportion, a side stretches it, as one undo step.
  `HasSelectedImage`, `SelectImage`, `SetSelectedImageSize`,
  `Get/SetSelectedImageAltText` on the element; `IsImageAt`, `GetImageInfo`,
  `SetImageSize`, `SetImageAltText` on the editing core.
- **Floating pictures with text wrap.** `RichTextRun` gains `imageWrap`
  (`Square`, `TopAndBottom`, `BehindText`, `InFrontOfText`),
  `imageFloatAlign` and `imageOffsetXPt` / `imageOffsetYPt`. The DOCX reader
  used to flatten every `<wp:anchor>` picture into a separate image paragraph
  after its paragraph, losing the wrap; it now keeps it in the paragraph as a
  floating picture with its wrap and position, and the ODT reader does the
  same for paragraph- and character-anchored frames. Both writers write them
  back as anchored pictures (DOCX `wp:anchor`, ODT graphic styles); HTML
  output floats them. The element places a floating picture at its
  paragraph's top and wraps the text round it a paragraph at a time: beside a
  square one, above and below a top-and-bottom one, under or over the others.
- **Page view breaks paragraphs and tables across pages.** A paragraph that
  does not fit continues on the next page, broken between lines with widow
  and orphan control; a table continues between rows (never through a
  row-spanning cell), repeating its header rows on every page; a heading is
  kept with what follows it. They used to move to the next page whole, and a
  block taller than a page ran past the bottom margin. Caret, hit testing,
  selection, scrolling, spell marks and pictures follow the pieces.
- **`ITextLayout::GetLineExtents()`**: every line's bytes and vertical extent.
- **Zoom and horizontal scrolling in the rich text element.** `SetZoom`
  (0.25-5, Ctrl+wheel) scales everything it draws; outside page view the text
  rewraps to the zoomed width. A page wider than the view - landscape, or
  zoomed in - gets a horizontal scrollbar (Shift+wheel, the caret brings the
  view along) instead of being cut at the right.
- **PDF export.** `UltraCanvasRichTextEdit::ExportToPdf(path | bytes, error)`
  writes the document's pages - headers, footers, page numbers, pictures,
  formulas - as a vector PDF with real text, without selection, caret or
  editing guides, from an element that need never have been shown. It is built
  on the new **`UltraCanvasPdfSurface`**, which draws any element into a PDF
  through the ordinary render context (Cairo's PDF surface; UTF-8 paths on
  every platform), and **`PrintDocumentWithDialog`** / `PrintDocumentWithSettings`
  send such a PDF (or any payload a printer takes) through the print dialog.
  `RenderContextCairo::AttachSurface` lets a context draw onto a surface it
  did not create.
- **Headers and footers can be edited.** Double-click one (or the page's top
  or bottom margin, to create one), or call `EditHeader(page)` /
  `EditFooter(page)`: the body is shown pale, and typing, formatting,
  pictures, tables, fields and undo act on the header or footer, which is
  written into the document as it changes (the body moves down as it grows).
  Escape or a click in the body goes back. `IsEditingHeaderOrFooter`,
  `FinishHeaderFooterEditing`, `onHeaderFooterEditingChanged`.
- **Named styles.** `UCRichDocument::styles` (`RichStyle`: paragraph or
  character, `basedOn`, `nextStyle`, optional properties), `RichDocBlock::styleId`
  and `RichTextRun::characterStyleId`. The editing core applies, changes
  (propagating to the text that follows the style, sparing direct formatting),
  creates and deletes them in undoable steps; the element passes them through
  and adds `NewStyleFromCaret` / `UpdateStyleFromCaret`. DOCX and ODT read and
  write them (they were flattened into direct formatting on load and could not
  be saved), and the DOCX reader now gives runs their paragraph and character
  styles' bold, italics, underline and colour, which it used to drop. A
  heading's bold no longer appears as `**...**` in its Markdown.
- **Footnotes and endnotes.** `UCRichDocument::notes` (`RichNote`) with
  reference runs (`RichTextRun::noteIndex`) that number themselves (footnotes
  1, 2, 3, endnotes i, ii); `UCRichDocumentEditor::InsertNote`/`NoteAt`; and in
  `UltraCanvasRichTextEdit`, `InsertFootnote`, `InsertEndnote` and `EditNote`
  (double-click a note or its reference). Page view puts footnotes at the foot
  of their reference's page, making room for them, and endnotes after the body;
  PDF export includes them. DOCX and ODT read and write them - ODT footnotes
  used to be flattened into the text in parentheses, DOCX ones were dropped -
  and Markdown reads and writes `[^1]` footnotes.
