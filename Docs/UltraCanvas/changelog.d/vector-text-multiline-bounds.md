- **`VectorText::GetBoundingBox` counts every line of a text set over
  several.** A span with a `Position` of its own starts a line there (SVG's
  `<tspan x y>`, and ArtCreator's texts typed on the page), but the estimate
  stretched the first line by every line's characters and left the others
  out, so the selection frame and the hit test missed all but the first
  line. Each line is its own box now, and characters are counted, not UTF-8
  bytes (an accented or Thai text measured up to three times too wide).
