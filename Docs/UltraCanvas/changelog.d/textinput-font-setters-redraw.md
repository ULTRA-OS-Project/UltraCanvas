- **A text field showed a new font only when something else repainted it.**
  `UltraCanvasTextInput::SetFontSize` and `SetStyle` stored the new font and
  did nothing else: no redraw, no layout invalidation, and the horizontal
  scroll stayed clamped for the old character widths. Both now re-clamp the
  scroll, invalidate the layout and redraw, as the `UltraCanvasLabel` and
  `UltraCanvasButton` font setters always did. Test:
  `Tests/TextInputFontTest.cpp` (headless, through a probe subclass).
