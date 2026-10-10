- **The stepper's completed steps show their check mark on Windows.**
  `UltraCanvasStepper` wrote the mark as the character "✓", which the
  bundled Ubuntu UI font does not have, so it was left to a fallback font -
  and on Windows it drew as a box of hex digits (UltraCanvasStart's step
  header). The check is now a stroked path in the marker's glyph colour, as
  `UltraCanvasCheckbox` draws its own, so it no longer depends on any font.
