- **UltraCanvasButton: the text on several lines.** `ButtonStyle::wrap` /
  `SetWrap(TextWrap::WrapWord)` breaks a button's text onto as many lines as
  its text area holds, from the top of the area, the last one shortened with
  "…" when more is left - an application's name under its icon on a tile.
  `WrapNone`, the default, keeps the text on one line as before. Doc:
  `Docs/UltraCanvas/UltraCanvasButtonExamples.md`.
