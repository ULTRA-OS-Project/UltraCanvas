- **Windows: a window is never shown black.** `Show()` made the window
  visible and painted it at once - from a surface nothing had been drawn
  into yet, which a new Cairo image surface fills with zeros: black. The
  content came with the event loop's first frame, so a window stayed black
  for as long as anything held the loop on its way there (UltraMail's main
  window, for ten seconds and more on one machine).
  - `UltraCanvasWindowsWindow::Show()` draws the first frame before the
    window appears (`UltraCanvasWindowBase::RenderBeforeShow()`: the layout
    and paint of `UpdateAndRender()` for a window not yet shown), so it
    appears with its content.
  - A window's surface starts in its `backgroundColor`, not black - when it
    is created and when it is made again for another screen scale - so
    whatever is shown before a frame has been drawn is the window's colour.
  - Doc: `Docs/UltraCanvas/UltraCanvasWindowsDiagnostics.md`, "Slow to
    start, or a window that stays black".
