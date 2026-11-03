- **Android: frames are drawn at the window's exact pixel size at every
  density.** The backend built its surface from the logical size scaled back
  up, and at a phone's density that loses a pixel: at 420 dpi (scale 2.625)
  a 1080-pixel-wide window is 411 logical units, and 411 x 2.625 rounds to
  1079. The buffer was then a pixel short of the window, and the compositor
  stretched every frame to fit, softening the whole UI. The surface now
  takes the window's own size whenever it is the logical size up to
  rounding.
- **CI runs the Android demo at a phone's density too.** The emulator job
  now also runs on the `pixel_6` profile (1080x2400, 420 dpi), beside the
  default 320x640 at 160 dpi, and the smoke test checks that the first frame
  fills the window to the pixel at the scale the density gives (density /
  160). The backend's first-frame log line now names the surface size, the
  window size and the scale.
