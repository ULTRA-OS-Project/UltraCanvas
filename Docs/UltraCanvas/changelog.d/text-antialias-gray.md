- **Text is antialiased in grayscale, not subpixel, by default.** The Cairo
  backend rendered every glyph with LCD subpixel antialiasing, which colours
  the edge of each stem red on one side and blue on the other. That reads as
  sharper text only on an LCD with a horizontal RGB stripe at 1:1 scale; in
  a screenshot, on a scaled, rotated or OLED screen, and in a window
  composited through alpha - every UltraCanvas surface is ARGB - the colours
  showed as red and green ghosts beside the text, as the demo's screenshots
  did. `RenderContextCairo::SetTextAntialias(CAIRO_ANTIALIAS_SUBPIXEL)` is
  still there for a host that knows its panel; the demo's Text Rendering
  page switches it.
