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
