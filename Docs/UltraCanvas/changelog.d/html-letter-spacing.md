- **HTML `letter-spacing`.** Read in px or em and inherited as px, as CSS computes it
  (`ComputedStyle::letterSpacingPx`; `normal` is 0), and drawn through Pango's
  `letter_spacing`: a block's spacing wraps its whole text run, an inline element's
  own spacing is a span inside it. Measuring and wrapping take it into account - a
  Yahoo notice's `p { letter-spacing: 0.5px }` now wraps where a browser wraps it.
