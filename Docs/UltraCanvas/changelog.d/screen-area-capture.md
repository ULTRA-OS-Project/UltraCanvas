- **UltraCanvasDesktopShell: one rectangle of the screen.** `CaptureScreenArea(x, y,
  width, height, image)` captures that much of the screen as pixels in memory -
  `XGetImage` of the rectangle on X11, a `BitBlt` of it on Windows - clipped to
  the screen, instead of the whole screen and a crop: what UltraSnip's snips
  and its screen recording (twenty frames a second of one window) take.
  `DesktopScreenImage` carries `x` / `y`, where its pixels came from (the
  virtual screen's origin on Windows, which is negative with a monitor left
  of the primary). `DefaultRecordingPath()` names
  `<Videos>/Screen Recordings/Screen recording <date> <time>.mp4`, as
  `DefaultScreenshotPath()` names a screenshot.
