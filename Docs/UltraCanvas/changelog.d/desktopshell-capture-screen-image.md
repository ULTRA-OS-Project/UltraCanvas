- **DesktopShell: the screen can be captured into memory.**
  `UltraCanvasDesktopShell::CaptureScreenImage(DesktopScreenImage&, &error)`
  returns the whole screen as BGRx pixels (cairo's RGB24 layout, the QR
  scanner's BGRA32) without touching the disk, for a caller that must not
  leave a file behind: UltraAuthenticator reads an enrolment QR code off the
  screen this way, and a PNG of that code in the Pictures folder would be the
  account's seed in the clear. `CaptureScreen(pngPath)` is unchanged and now
  writes the same buffer, so the two cannot disagree about what the screen
  looked like. The null backend fails both with the same reason.
