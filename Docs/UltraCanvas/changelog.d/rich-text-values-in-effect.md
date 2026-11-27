- **A rich document's paragraph says the values it is laid out with.**
  `UltraCanvasRichTextEdit::GetEffectiveParagraphFormat` (over
  `UCRichDocumentEditor::GetEffectiveParagraphFormat`) gives the caret
  paragraph's layout as it applies - what the paragraph states, else what
  its paragraph style states (`UCRichDocument::ParagraphStyleOf`: Normal's
  for a paragraph naming none, its level's heading style for a heading),
  else the view's own: no space before, the view's block spacing after,
  single lines - and, in `RichParagraphFormatState`, which values the
  paragraphs of the selection disagree on. A tool panel shows a value, and
  "-" only for such a selection; `GetParagraphFormat` still says what the
  paragraph itself states. `GetEffectiveTableProperties` does the same for
  a table: the view's grid line (0.75 pt) and cell padding where the
  document states none.
- **A paragraph that states no spacing is spaced as its style says.** A
  heading made with the style list (`SetHeadingLevel`) states no spacing of
  its own, and was laid out with the view's 6 px whatever its style gave;
  the layout takes its style's space before and after now (Heading 1: 12 pt
  and 4 pt), as Word and Writer show the file it is saved to. A document
  read from a file states its spacing on every paragraph, so nothing
  changes for one; a document with no styles (Markdown) neither.
- **List styles and cell shading for a tool panel.** `GetListMarker` /
  `SetListMarker` (`RichListMarker`: a number format, a bullet or a check
  list's box) and `EndList` give the selection's paragraphs a list style or
  take it away; `GetCellShading` / `SetCellShading` fill the caret's cell or
  the selected cells. Each set is one undo step. A cell's fill is drawn on a
  table on the view's own grid too, and the DOCX and ODT writers keep it
  there (`w:shd`, `fo:background-color`); it was drawn and written only for
  a table with lines of the document's.
- **A tab strip that takes the keyboard when asked, with a focus ring.**
  `UltraCanvasTabbedContainer::SetKeyboardFocusOnRequest(true)` makes the
  strip take the keyboard only through `FocusTabStrip()` - an
  application's F6 - never from a click, so a click on a tab leaves the
  keyboard in the page; while it has it, Left and Right choose a tab and a
  ring in the theme's focus colour (`focusRingColor`) is drawn round the
  open tab. UltraOffice's window had a subclass of its own that took the
  focus and drew nothing; it uses this now.
  `UltraOfficePanelsWindowTest` checks the values in effect, each new panel
  entry both ways and the ring's colour on the strip after F6.
- **Splitting a merged cell gives every row its cells back.**
  `UCRichDocumentEditor::SplitTableCell` reset the span first and then put
  a fresh cell into each freed slot that was still free - but a row's cells
  fill the free slots in order, so the cells after the span had already
  slid left into them, and the row came out a cell short at its end (a
  three-column row merged across two cells and split again had two). The
  fresh cells go in while the span still covers the slots now.
  `RichTextEditorTest` checks a 2 x 2 merge in a 3 x 3 table splits back to
  three cells in every row.
- **A dropdown shows a choice set from outside at once.**
  `UltraCanvasDropdown::SetSelectedIndex` changed the choice without asking
  for a redraw, so a box a tool panel brought into step (the page size, the
  margins) went on showing the old one until something else redrew it.
- **A tool panel's long caption goes on two lines instead of cutting values
  off.** `UltraCanvasToolValueRows`' caption column is as wide as its widest
  caption, and a long one - Russian "Межстрочный интервал", Finnish
  "Sisennys vasemmalla", English "Horizontal position" - narrowed the
  spinners beside it until a shape's "29.63 cm" and Russian "1.00 строки"
  were cut off. A caption of more than 14 letters (`kCaptionBreakLength`) is
  set on two lines now, broken at the space nearest its middle
  (`TwoLineCaption`).
