- **A long URL ran out of its tooltip's box.** UltraMail shows a link's
  address as a tooltip, and a tracking link is mostly long runs of letters and
  digits with nowhere to break. The tooltip wrapped its text with Pango's
  default word-only wrap, which leaves such a run whole, so lines came out up
  to 991 px wide in a box sized for 430 and the text was drawn straight over
  the border. `UltraCanvasTooltipManager` now wraps at word boundaries first
  and between characters where a word alone does not fit, and sizes the box
  to the text it actually drew - for plain text, titles, bullets and table
  cells alike. A wrapped paragraph's box now also hugs its longest line
  instead of always taking the full `maxWidth`.
