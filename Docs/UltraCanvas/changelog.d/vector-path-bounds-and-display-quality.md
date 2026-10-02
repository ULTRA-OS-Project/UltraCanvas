- **A path's bounding box is where the path is drawn.** `PathData::GetBounds`
  and `VectorPath::GetBoundingBox` read only absolute `M` / `L` / `C`
  commands: a relative curve (`c`) was measured as if its offsets were
  coordinates, and `H` / `V`, the smooth and quadratic forms and arcs were
  skipped. An SVG written with relative curves (`media/vector/SVG/astronaut.svg`)
  therefore got selection boxes near the page origin, the size of the
  offsets, and culling, snapping and alignment used the same wrong boxes. The
  bounds now come from the normalised path (every command kind, relative or
  absolute) and a curve is measured at its extrema, not its control points.
- **Vector display quality.** `VectorRenderOptions::DisplayQuality`
  (`Outline`, `Simple`, `Normal`) and `UltraCanvasVectorCanvas::SetDisplayQuality`:
  outlines only, fills and lines without antialiasing, or everything
  antialiased. `VectorRenderOptions::EnableAntialiasing`, which nothing read,
  now turns antialiasing off as well.
