- **SVG export writes width profiles and brushes.** SVG has neither a
  variable-width stroke nor a brush, and the writer wrote a plain
  constant-width stroke instead: a tapered line came out uniform and a
  brushed line as a bare stroke. A shape with a `WidthProfile` or a `Brush`
  is now written as what the renderer draws: a group holding the shape with
  its fill and no stroke, then the band (`VariableWidthOutline`, filled
  even-odd in the stroke's paint) or the stamps (`<use>`s of the stamp,
  written once in `<defs>`), then the arrowheads. The group carries the
  stroke (`data-ultracanvas-stroke`, a style declaration list), the profile
  and the brush, and the SVG reader gives back the shape with its stroke
  and stamp, so it stays editable; any other reader draws the shapes. A
  tapered curve, a thick-thin curve, a profiled ellipse and a brush turning
  along a curve render the same through librsvg as through the renderer.
  - `BrushStampPlacements(points, stroke)` (`UltraCanvasVectorStorage.h`):
    where a brush stamps along a flattened subpath, as matrices from the
    stamp's own space. It was a private copy of the renderer's loop inside
    the XAR writer; the XAR and SVG writers now share it.
  - `Tests/SVGConverterTest.cpp` covers the export, the round trip and the
    librsvg rendering.
