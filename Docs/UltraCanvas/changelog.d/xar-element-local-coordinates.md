- **A Xara drawing is no longer shifted and cut off by its own position.**
  `UltraCanvasXARElement` (XAR plugin 2.3.1) placed the page by its position
  in the parent although the parent had already moved the drawing context
  there, so a drawing anywhere but its parent's top-left corner was drawn
  that far down and to the right and lost its bottom and right edge - in the
  demo's XAR tiles 12 px, enough to cut the last line of Backside.xar. It now
  draws at its own origin, as the CDR element does since this branch.
