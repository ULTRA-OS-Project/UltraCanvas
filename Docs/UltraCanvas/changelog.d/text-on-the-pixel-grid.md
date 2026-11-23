- **Text sits on the pixel grid: no more blurred first letters.** A file name
  in UltraFiler's icon grid starting with R, I, B or l often looked blurred
  and lighter than the rest of the word, on Windows and Linux alike. Glyphs
  are hinted only vertically and advance by fractional widths, so a label
  centred over an odd leftover width - or a letter whose side bearing is a
  fraction of a pixel - drew its first vertical stem as two grey columns
  instead of one dark one. `RenderContextCairo::DrawTextLayout` now moves the
  text by less than a pixel so the left edge of its ink and its baseline fall
  on whole device pixels. It does so only where text becomes pixels (not in a
  PDF, PostScript or SVG export) and only without rotation or skew, so every
  label, button and tab benefits, not just the Filer.
