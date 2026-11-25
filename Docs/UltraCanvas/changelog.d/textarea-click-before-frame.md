- **`UltraCanvasTextArea`: a click on a line the caret was not on is hit-tested
  against that line.** A mouse press moves the caret; until the next frame
  lays the caret's line out again, `GetActualLineLayout` handed back the old
  caret line's layout for the new one, so a release hit-tested before a frame
  was drawn (a fast click, a synthetic one) landed on the wrong text and a
  link under it did not open. The caret's own layout is now used only for the
  line it was laid out for (`currentLineIndex`).
