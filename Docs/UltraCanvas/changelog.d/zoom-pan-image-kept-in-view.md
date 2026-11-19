- **`UltraCanvasZoomPanImage` can keep the image in view and leave a small
  one at its own size.** `SetPanKeptInView(true)` holds the pan to the
  image's edges after every drag, wheel step and resize: an axis on which the
  image fits stays centred, so an image shown whole does not move and a
  zoomed one moves only as far as its edges, and the cursor is the four
  arrows only while there is room to drag. `SetFitEnlarges(false)` starts an
  image smaller than the view at 1:1, as `ImageFitMode::ScaleDown` shows it,
  instead of blowing it up to fill the view. `GetZoom()` reads the zoom. Both
  options are off by default, so the lightbox and the Album keep their
  behaviour. UltraSnip shows its picture with both on.
- **`UltraCanvasZoomPanImage` can show sharp pixels.**
  `SetSharpPixelsFrom(2.0)` draws every image pixel as a hard-edged square
  once it covers two screen pixels or more: the natural-size pixmap scaled by
  the context with smoothing off, since a pixmap made at the display size is
  already smoothed. Off (0) by default. While it is on, the cap that keeps a
  vector source's re-rasterising quick does not stop zooming past that
  point. UltraSnip turns it on at 200 %.
