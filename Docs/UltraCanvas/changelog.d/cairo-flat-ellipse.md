- **A zero-width or zero-height ellipse no longer kills a window's drawing.**
  `RenderContextCairo::DrawEllipse`, `FillEllipse` and `Ellipse` scaled the
  context by the radii with `cairo_scale`; a zero radius is an invalid
  matrix, which cairo answers by putting the whole context into a
  permanent error state that `cairo_restore` does not clear - everything
  drawn afterwards in that window silently did nothing. ArtCreator's
  ellipse preview hit it on the first step of every drag. A flat ellipse
  now adds the line it collapses to (`FillEllipse` adds nothing), a point
  adds nothing, non-finite input is ignored, and the arc starts its own
  sub-path instead of joining whatever path was current. `Scale()` and
  `SetTransform()` already refused degenerate matrices.
