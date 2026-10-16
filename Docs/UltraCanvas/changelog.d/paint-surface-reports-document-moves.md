- **`UltraCanvasPaintSurface` reports a view move caused by the document.**
  A document change (a layer added, the canvas resized) clamps the view, and
  since the view follows a resize, a change between a resize and the next
  frame moved the view there and then - without `onViewChanged`, so overlays
  placed in view coordinates (UltraPaint's editor over a text) stayed where
  they were until the next redraw. The listener now reports any move it
  makes; a change that moves nothing reports nothing.
