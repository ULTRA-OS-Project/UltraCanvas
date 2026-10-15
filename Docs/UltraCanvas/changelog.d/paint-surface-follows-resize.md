- **`UltraCanvasPaintSurface` follows a resize.** When the element was
  resized (the window, a split pane), the view kept its old origin until the
  next document change re-clamped it, so a grown window showed the image
  stuck in a corner of the new space, and only a later edit snapped it to
  the centre. The view now keeps the image point at its centre where it was,
  at the same zoom, and centres an image smaller than itself again, in the
  frame that shows the new size (or at the next view call, if one comes
  first); `onViewChanged` reports it. A loupe keeps the point `SetLoupeView`
  put at its centre. Doc: `UltraCanvasPaintSurface.md`, *The element*.
