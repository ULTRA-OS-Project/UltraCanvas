- **A slide's text could be edited only as plain lines, and tables not at
  all.**
  - `PptxParagraphsToRichDocument` / `PptxParagraphsFromRichDocument`
    (`UltraCanvasPresentationPptx.h`): a shape's text as a word-processing
    document and back, for `UltraCanvasRichTextEdit` laid over the shape -
    runs with their looks, line breaks, alignment, bullets and numbering
    (PowerPoint's schemes as the document's number formats, start numbers on
    a list's first item); what the document does not hold comes back from
    the text before the edit. `PptxBulletText` gives a bullet as drawn (a
    Wingdings square as a square).
  - `UCSlideDeckEditor::FormatSelectedText` / `AllSelectedText`: the text of
    the selected shapes formatted as one step.
  - Tables: `InsertTable` (PowerPoint's banded look), `TableCellBox`,
    `TableCellAt`, rows and columns inserted and deleted, each one step;
    `UltraCanvasSlideEdit::onEditTableCell`, `SetEditingShape`,
    `SetEditingCell` and `PptxSlideDrawOptions::hideTextOfCell...` for a
    host's in-place editor.

  Covered in `SlideDeckEditorTest`.
