- **UltraCanvasDesktopShell: `CaptureWindowImage` and
  `DesktopWindowInfo::appUserModelId`.** `CaptureWindowImage(id, maxWidth,
  maxHeight, image)` gives a small picture of one window for a taskbar's
  previews - on Windows drawn by the window itself (`PrintWindow` with the
  full content), so a covered window shows its own content; on X11 the
  window's rectangle of the screen. On Windows `ListWindows` now reads each
  window's AppUserModelID (the window's own, else its process's): a
  taskbar groups an application's windows by it, `MatchApplication` finds
  a Store application's entry by it, and a Store application's window or a
  frame whose program is not known yet - Calculator, Settings, WhatsApp -
  takes the tile icon and the name the shell's Applications folder gives
  it instead of showing a letter.
- **UltraCanvasButton: a toggle that is on reports the hover.**
  `onHoverEnter` was skipped while the button was pressed, so a toggle
  button that is on - a taskbar's button of the active window - never said
  the pointer had come over it. A push button held down still does not.
