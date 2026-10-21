- **CSS layout: a wrapping flex row inside a column is as tall as all its
  lines.** A column measures its children at "at most" its width, and the
  flex layout wrapped a row only at an exact width - so a `FlexWrap::Wrap`
  row of cards measured one line high, the column gave it that height, and
  every line after the first was clipped (or the row reported all its cards
  side by side as its width and widened the page). An at-most width is a
  wrap budget now, as the code's own note already said it was.
  `CSSLayoutTests` covers it (`test_flex_wrap_row_height_in_column`).
