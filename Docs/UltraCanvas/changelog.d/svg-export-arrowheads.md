- **SVG export writes arrowheads.** The SVG writer ignored the line
  gallery's arrowheads (`StrokeData::StartArrow` / `EndArrow`), so an arrow
  drawn in ArtCreator lost its heads when saved as SVG. Each is now written
  as a `<marker>` holding the outline the renderer draws
  (`ArrowheadOutline`), in the line's units and colour, with
  `orient="auto"` and a start head drawn pointing backwards so SVG 1.1
  readers (no `auto-start-reverse`) place it right. They go on lines,
  polylines and paths whose ends are open, as the renderer draws them, and
  the same arrowhead in the same colour is written once. The markers carry
  `data-ultracanvas-arrowhead` / `-scale` / `-end`, and the SVG reader turns
  them back into the arrowheads themselves, so saving and reopening in
  ArtCreator keeps them editable; any other reader simply draws them. All 14
  kinds at both ends of a curve render the same through librsvg as through
  the framework's renderer. `Tests/SVGConverterTest.cpp` covers it.
