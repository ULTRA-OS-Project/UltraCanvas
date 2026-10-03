- **HTML images on a shared line: a real space, and `vertical-align`.**
  - The gap between two images a space apart is the width of a space in their font,
    measured once per font (`ElementBuilder` keeps a small offscreen context for it),
    no longer an estimate of 0.28 em.
  - `vertical-align: top` and `middle` place an image at the top or in the middle of
    the line's other images; `baseline` (the default) and `bottom` keep it on the
    line's bottom.
