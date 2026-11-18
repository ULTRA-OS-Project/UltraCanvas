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
