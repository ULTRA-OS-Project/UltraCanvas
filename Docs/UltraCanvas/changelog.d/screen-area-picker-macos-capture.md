- **UltraCanvasScreenAreaPicker: choosing an area of the screen is part of
  the framework.** UltraSnip's selection - the screen frozen and dimmed, a
  rectangle dragged or a window clicked, Enter for the window under the
  pointer or the whole screen, Escape to give up - moved out of UltraSnip
  into `UltraCanvasScreenAreaPicker.h`, with the arithmetic beside it
  (`ScreenArea`: the rectangle of a drag, the crop of a capture into RGBA,
  the window under a point, the windows to offer). UltraSnip and ChatCube's
  new screenshot button use the same one.
  `Docs/UltraCanvas/UltraCanvasScreenAreaPicker.md`.
- **macOS captures the screen.** `CaptureScreen`, `CaptureScreenImage` and
  `CaptureScreenArea` said "Screenshots are not available on this platform
  yet" on a Mac. They now capture the main display at its own pixels
  through ScreenCaptureKit (macOS 14 and later). macOS asks once for the
  Screen Recording permission (Privacy & Security); until it is given the
  capture fails with that reason. Other displays are not captured yet.
- **`UltraCanvasDesktopShell::CanCaptureScreen()`** says whether a build
  can capture the screen at all - false on Android and WebAssembly - so a
  screenshot button can stay hidden where it would only fail.
- **`WindowType::Overlay` on macOS** is a borderless window above the menu
  bar and the Dock, on every Space, that takes the keyboard (a borderless
  window on macOS otherwise never does). The screen-area picker opens in
  one there, placed in points.
