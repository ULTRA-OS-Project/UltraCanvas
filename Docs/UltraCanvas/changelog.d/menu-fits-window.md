- **A context menu no longer takes a scrollbar while the window has room for
  it.** A menu opened at a point went below the point or ended at it, and with
  room on neither side it was cut short with a scrollbar - one entry more
  (UltraFiler's "Open path" in search results) was enough. It now moves up as
  far as it must and shows whole; only a menu taller than the window scrolls.
  `UltraCanvasMenu::HasScrollbar()` tells which.
