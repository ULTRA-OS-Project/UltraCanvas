- **Width profiles follow straight segments.** `VariableWidthOutline`
  (`UltraCanvasVectorStorage.cpp`) built a width-profiled stroke's band from
  the flattened path's points only, so on a straight segment the width went
  linearly from one point to the next whatever the profile did in between:
  a straight two-point line with a mid-way peak (`{0: 0.2, 0.5: 2, 1: 0.2}`)
  came out as an even thin band. Curves were mostly spared, because
  flattening gives them many points. The band now gets a point wherever a
  profile sample falls inside a segment (the closing segment of a closed
  shape included). The profile is linear between samples, so the edges are
  exact straight lines between those points. The path's own points keep the
  tangent they had, so corners join as before. The renderer and the XAR
  writer use the function (the SVG writer too, once it writes width
  profiles), so the screen and the exports change together.
  `Tests/VectorModelTest.cpp` checks the band's width, the rendered peak, a
  thin end and a profiled closed shape's two rings.
