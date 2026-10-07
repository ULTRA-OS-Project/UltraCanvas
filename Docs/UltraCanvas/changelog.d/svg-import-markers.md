- **SVG import draws markers: arrowheads on connector lines.** The Vector
  plugin's SVG reader skipped `<marker>` ("`<marker>` is not supported,
  skipped") and ignored `marker-start` / `-mid` / `-end`, so a diagram's
  arrows imported as bare lines. The model has no marker-by-reference, so
  the reader now draws each marker's content as ordinary shapes at the
  vertices SVG 2 says (each subpath start and segment end; curves and arcs
  by their tangents): placed by `refX`/`refY`, mapped from its `viewBox`
  onto `markerWidth` x `markerHeight`, scaled by the stroke width unless
  `markerUnits="userSpaceOnUse"`, turned by `orient` (`auto`,
  `auto-start-reverse`, an angle) and clipped to its viewport when its
  content reaches past it. The shape and its markers become one group, so
  an arrow stays one object to select and move in ArtCreator, and the
  marker content takes `context-fill` / `context-stroke` from the shape.
  The properties work from attributes, `style=""` and `<style>` sheets
  alike, and inherit (`<g marker-end="…">`); a marker that uses itself is
  drawn once. Checked against librsvg on a diagram with single- and
  double-ended arrows. `Tests/SVGConverterTest.cpp` covers it.
