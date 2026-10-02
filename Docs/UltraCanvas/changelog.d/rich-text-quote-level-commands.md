- **Quote levels can be changed from a toolbar.** New
  `UCRichDocumentEditor::IncreaseQuoteLevel` / `DecreaseQuoteLevel` and the
  matching `UltraCanvasRichTextEdit` methods move every block the selection
  touches one quote level in or out (the caret's block when nothing is
  selected).
  - The level stays between 0 and 8.
  - Each change is one undo step; a change that does nothing records none.
  - They also work with the caret in a table cell: the level belongs to the
    whole table and restyles none of its cells.
