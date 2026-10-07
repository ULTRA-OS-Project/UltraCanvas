- **Inline `<svg>` and `<math>` in a page keep their names' case.** The HTML
  parser lower-cased every tag and attribute name, so an SVG drawn inside a
  page came out as `lineargradient` with a `viewbox`, which no SVG consumer
  recognises. Inside `<svg>` and `<math>` the parser now applies the HTML
  standard's foreign-content tables: the vocabulary's own spelling
  (`linearGradient`, `viewBox`, `preserveAspectRatio`, `definitionURL`)
  whatever case the source used, HTML again inside `foreignObject`, `desc`,
  `title` and the MathML text elements, no HTML implicit closes or void
  elements in between. `Node::GetAttribute` finds a name exactly, then in any
  ASCII case, and a CSS type selector matches a foreign element's camelCase
  name, as in a browser. Needed before UltraWeb renders pages; harmless for
  mail and eBooks, whose SVG cover wrappers still show their raster image.
- **The tests compile the HTMLReader core once.** `Tests/CMakeLists.txt`
  listed the parser, CSS and resolver sources in three test targets; they are
  one object library, `HTMLReaderTestCore`, that the three link.
- **The unbuilt SVG plugin is gone.** `Plugins/SVG/UltraCanvasSVGPlugin`
  (a separate `UltraCanvasSVGElement` with its own SVG model, renderer and
  `style=""` parser, 1,850 lines) was commented out of the build and named
  only by the demo page, which has shown SVG files through
  `UltraCanvasImageElement` all along. An SVG is a picture: the image
  element and the media viewer show it (librsvg), `UltraCanvasVectorRaster`
  rasterizes it, the Vector plugin's `SVGConverter` reads and writes it.
  `Docs/UltraCanvas/UltraCanvasSVGExamples.md` now says so instead of
  documenting the removed element.
