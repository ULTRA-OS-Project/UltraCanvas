- **SVG `fill-rule` is read and written.** The SVG reader dropped it, so an
  icon, a ring or a letter converted to outlines with
  `fill-rule="evenodd"` came in with its holes filled solid. It now sets
  `VectorStyle::ShapeFillRule` (added for ODG), which the renderer draws:
  from the attribute, `style=""` or the stylesheet, and inherited from a
  group, as SVG inherits it - resolved per element on reading, since the
  model does not inherit the rule. The writer puts it on shapes only, never
  on a group whose children would then inherit what the model never gave
  them. The PDF writer (and so AI) fills such a shape with `f*` / `B*`, the
  EPS writer with `eofill`; the other writers follow in
  `vector-fill-rule-writers.md`.
  - Checked in `SVGConverterTest`: an even-odd ring reads, draws with its
    hole open and round-trips; the rule inherits from a group and applies
    from a stylesheet; it is written on the shape alone; PDF and EPS write
    the even-odd operators (8 of the 10 checks fail without the change).
