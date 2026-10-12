- **`UltraCanvasPaintSurface` has a loupe mode: a magnifying glass on the
  same document.** `SetLoupeMode(true)` makes a second surface the host
  alone drives - `SetLoupeView(zoom, imageX, imageY)` puts an image point at
  its centre at that zoom, and the view is never clamped or fitted, the
  pointer, wheel and keyboard are ignored, and the element takes neither
  focus nor hit-tests, so it can lie over the editing view without taking
  its events. A translucent diagonal crosshair (`SetLoupeCrosshair`) marks
  the centre and leaves the pixel there clear. UltraPaint's mouse pointer
  magnification glass is the first user.
