- **A third fitting zoom for the rich text editor: a share of the width.**
  `UltraCanvasRichTextEdit::ZoomToWidthShare(share)` (`ZoomFit::WidthShare`)
  makes the page itself `share` of the view's width (0.1 to 1, 0.6 unless
  chosen), centred on the desk, and keeps it so as the element is resized,
  like *Page width* and *Whole page* do; `GetZoomWidthShare` reads it back.
  A page on a wide screen keeps a reading width instead of filling it.
- **The slide show can play on the first monitor.** `StartSlideShow` takes a
  fourth argument, `showOnSecondScreen` (true unless given): with false and
  two monitors the show goes full screen on the first and the presenter view
  on the second.
