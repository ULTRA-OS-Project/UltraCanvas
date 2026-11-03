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
- **Android: rotating the device in place resizes the window.** On a
  configuration change or window resize the backend read the window's size
  with `ANativeWindow_getWidth`, but once it has set a buffer size the window
  reports that size instead of its own - so the size never seemed to change,
  and the compositor squeezed the portrait frame into the landscape window.
  The backend now restores the window's own geometry before reading it, then
  rebuilds the surface at the new size. The emulator smoke test turns the
  display to landscape and requires a frame at the new size that fills the
  window; the backend logs `resized frame presented: ...` for each new size.
