- **`VectorEdit::SnapToPixels`: shapes onto the pixel grid of the size an
  icon is shown at.** An icon drawn for 24 px is sharp there only where its
  straight edges fall on whole pixels; a 2 px line half a pixel off covers
  three pixel rows, the outer two half grey. The new operation takes the
  width of one icon pixel in document units and the grid's origin, makes
  every line width a whole number of pixels (at least one), and moves the
  points of a shape whose line is an odd number of pixels wide onto pixel
  centres, those of a shape with an even width or no line onto pixel
  edges. Rectangles keep at least a pixel each way and whole-pixel corner
  radii; circles and ellipses snap by the edges of their bounds, so any
  diameter stays possible; paths are made absolute and their nodes snapped,
  each curve's control points moving with their node, and a curve that fits
  inside one pixel becomes straight. Groups are aligned through their
  children, following transforms that only scale and move; text, images
  and rotated or skewed shapes are left alone and counted. `SnapToPixel`
  is the same rule for one coordinate. ArtCreator's new icon mode uses
  both; `Tests/VectorPixelSnapTest.cpp` covers them
  (`Docs/UltraCanvas/UltraCanvasVectorCanvas.md`, *Pixel alignment*).
