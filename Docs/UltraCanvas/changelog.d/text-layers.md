- **Text layers: a raster document keeps text as words until it is merged.**
  `UCRasterLayer::text` (`RasterTextContent`: a `UCRichDocument` in a box on
  the canvas, with a default font, size and colour) makes a layer a text
  layer, whose pixels are a rendering of its words. `UltraCanvasRasterText.h`
  draws one - `RenderTextLayer`, `RenderTextLayerLike` - through
  `UltraCanvasRichTextEdit` offscreen, and readies that element to edit the
  words on the canvas (`PrepareTextLayerEditor`, `TextLayerEditStyle`): it
  lays text out in its own document space and draws its zoom as a scale, so
  an editor at the canvas's zoom puts every glyph where the layer's pixels
  will be. `UCRasterDocument` keeps a text layer text through attribute
  edits, duplication, canvas resizes and crops (the box moves along), and
  makes it a bitmap at a pixel edit (undo makes it text again - the entry
  keeps the text, `RasterUndoEntry::textBefore`), Merge Down, Flatten,
  scaling, flipping and rotating; `RasterizeLayer` does it explicitly,
  `ReplaceLayer` is public (a text drawn again), `AddLayer` takes an undo
  label, and `SetInPlaceEditLayer` leaves a layer out of the views'
  composite while a host draws it itself. `.ucraster` projects keep text
  layers as text, the words as an OpenDocument text (`layers/<n>.odt`); the
  renderer states a document's paragraph spacing so it reads back laid out
  the same. Doc: `UltraCanvasPaintSurface.md`, *Text layers*.
- **`GetInstalledFontFamilies()`** (`UltraCanvasApplication.h`): every
  scalable font family the process can set text in - the system's installed
  fonts with the bundled and registered ones, as Pango sees them (fontconfig
  on Linux and Windows, CoreText on macOS) - sorted regardless of case. For
  font pickers.
- **`UltraCanvasRichTextEdit`:** `SetZoomRange` (follow a canvas's zoom past
  0.25-5) and `ApplyCharFormat` (set bold, italic, a font ... exactly, as a
  checkbox reads, where the toggles flip).
- **`UltraCanvasDropdown` type-ahead:** letters typed in the open list go to
  the first item that starts with them, regardless of case; letters within a
  second build one prefix (spaces too, once it has begun: "liberation s"),
  the same letter again steps to the next match, and
  Return chooses. A focused dropdown takes the letters itself; for one left
  out of the focus (`SetAcceptsFocus(false)`) the window hands the keys to
  its open list, so `UltraCanvasListView` gains `onKeyDown` (a key reaches
  the owner first) and `SetFocusedRow` (the arrow keys' move, from code).
