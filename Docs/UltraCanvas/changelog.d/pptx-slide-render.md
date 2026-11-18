- **PowerPoint slides drawn as pictures** (`UltraCanvasPptxSlideRender.h`, new):
  `RenderPptxSlide(deck, index, widthPx)` draws a slide - its background,
  rectangles, rounded rectangles and ellipses in their fills and outlines,
  pictures, tables in the theme's accent, and text at its sizes, colours,
  alignment, bullets and anchor, shrunk to fit in a placeholder too small for
  it. UltraClipboard previews a copied deck's slides with it.
- **The PowerPoint reader resolves what a slide is drawn with**
  (`UltraCanvasPresentationPptx` 1.1.0): a placeholder without a place takes
  its layout's or master's (`placeInherited`), a run's size and colour and a
  paragraph's alignment come through the list styles, the master's text
  styles and the theme's colours (`schemeClr` with `lumMod` / `lumOff` /
  `tint` / `shade`), a shape keeps its fill, outline, geometry, anchor and
  autofit scale, a slide its background, a table its column widths and row
  heights, and a group's shapes are placed on the slide. `themeColors` on the
  deck; `withPictures` keeps the pictures' bytes. Shapes with a fill and no
  text are now in `shapes`.
