- **New element: `UltraCanvasColorPaletteManager`** - the colour palettes
  under a colour picker. Rows of swatches, one per palette: with one palette
  its row ends in a small **+**; with several, every row ends in a **−** that
  takes it away and the last row has the **+** under its **−**. **+** opens a
  gallery popup of ready-made schemes grouped by kind - basics, moods
  (pastel, earth, skin tones, sunset, ocean, forest, neon), design systems
  (Material, Nord, Solarized), retro machines (Game Boy, PICO-8, Commodore
  64, VGA 16) and indexed palettes up to 256 colours for a GIF (web-safe
  216, RGB 3-3-2, 256 greys) - each with a preview, its colour count and
  its own **+**. A left click on a swatch picks its colour; a right click
  opens a small foreground-only colour picker that sets that one entry,
  live, with *Revert* and *Done*. The element sets its own height to its
  rows; `SerializePalettes` / `ParsePalettes` keep them in one settings
  line. Doc: `Docs/UltraCanvas/UltraCanvasColorPaletteManager.md`; test:
  `ColorPaletteManagerTest`.
- **`UltraCanvasColorSwatchBar` 1.2.0: wrapping.** `SetWrap(true)` runs a
  large palette on as many lines as it takes to keep its swatches at least
  `minSwatchSize`, the colours shared out evenly over the lines, and
  `HeightForWidth` says how tall that is - a 256-colour palette is a block,
  not a hairline. `SetColorAt` edits one entry; `onColorContextClicked`
  reports a right click with the entry's index and the window position.
  `GetEffectiveSwatchSize` answers for a wrapping bar too.
- **`UltraCanvasColorPicker` 1.9.0: `SetShowBackgroundColor(false)`** - a
  foreground-only picker for editing one colour: the background swatch and
  the swap arrow are gone, and the right button and the eyedropper's right
  click edit the foreground as the left ones do.
