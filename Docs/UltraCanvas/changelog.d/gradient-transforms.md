- **Vector drawings draw their gradients' transforms and spread methods.**
  `LinearGradientData::Transform` / `RadialGradientData::Transform` (SVG's
  `gradientTransform`) and `SpreadMethod` were read and written by the
  converters but dropped by the renderer, so an elliptical, rotated or
  skewed gradient was drawn as a circle or an unrotated ramp, and a
  repeating or reflecting one was padded. `VectorRenderer` now builds the
  pattern in gradient space and places it with `IPaintPattern::SetMatrix`
  (user = bounding box x Transform x point, in SVG's order) and its extend
  with `SetExtend`. An `ObjectBoundingBox` radial gradient without a
  Transform keeps its earlier reading, the circle of the box's longer side.
  - SVG files with `gradientTransform` - and CorelDRAW files, which reach
    the model as SVG - draw as authored.
  - The ODG reader reads LibreOffice's ellipsoid gradient as the ellipse
    its outline is, turned by its angle (it was a circle), and the writer
    writes such a gradient back as an ellipsoid at that angle, which
    LibreOffice renders as its own. LibreOffice narrows an ellipsoid's
    middle to a line where a radial gradient's contours keep their shape;
    that difference is reported.
  - Checked by `VectorRasterTest` (an elliptical radial, a turned
    bounding-box ramp, repeat and reflect - each failing on the old
    renderer), `SVGConverterTest` and `ODGConverterTest` (LibreOffice's
    gradient styles, `media/vector/ODG/gradients.odg`).
