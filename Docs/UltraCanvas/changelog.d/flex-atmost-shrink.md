- **CSS layout: a wrapped line wider than an "at most" row is measured shrunk
  to it.** A column measures its children at "at most" its width; a wrapping
  row measured that way broke its lines there but kept each item at its full
  natural width, so a growing text item alone on its line (a note that moves
  under the field beside it) was measured one wrapped line short, and the row,
  laid out at that height, cut the last line off when it was arranged at the
  real width. The line is shrunk to the width as it is when arranged
  (fit-content). `CSSLayoutTests` covers it.
