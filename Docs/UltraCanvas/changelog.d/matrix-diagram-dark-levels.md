- **Matrix diagram: near-black marks disappeared in the dark theme.** The
  "High" level of `MatrixScale::HighMediumLow()` is near-black, so on the
  dark grid the X-shaped sample's strongest marks were close to invisible.
  A level colour too close to the cell background is now drawn in the label
  colour, in the mark, the cell tint and the legend swatch alike, as a
  competitor's line in the House already was. Closeness is measured as RGB
  distance, so saturated colours keep their hue, and nothing changes in the
  light theme.
