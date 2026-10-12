- **`UltraCanvasTextArea` 3.7.5: a console that appends shows whole lines.**
  Scrolling down to the caret - what `AppendText` does on every line of
  output - set the offset so the caret's bottom met the view's bottom, which
  left the tail of a line cut off at the top of the view. The offset now
  lands on a line top, the lowest one at or past the minimal offset while the
  caret's line still fits, so the first visible line is a whole one and the
  gap at the bottom is less than a line. The line ending exactly at the
  viewport's top edge was drawn too, its last pixel rows showing above the
  first line; it is skipped. UltraCanvasStart's Install and Build consoles
  showed both.
