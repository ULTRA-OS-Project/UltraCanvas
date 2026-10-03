- **HTML `letter-spacing`.** Read in px or em and inherited as px, as CSS computes it
  (`ComputedStyle::letterSpacingPx`; `normal` is 0), and drawn through Pango's
  `letter_spacing`: a block's spacing wraps its whole text run, an inline element's
  own spacing is a span inside it. Measuring and wrapping take it into account - a
  Yahoo notice's `p { letter-spacing: 0.5px }` now wraps where a browser wraps it.
- Fixed: a label's natural width is one its text fits on its lines at. With letter
  spacing, Pango breaks a line on the spacing after its last letter, which its
  measured width leaves out, so a shrink-to-fit button ("FIND OUT WHO", 2px spacing)
  wrapped its last word.
