- **The Filer's Display > Sort is greyed out for a list that keeps its own
  order.** With `SetFileListOrderPreserved(true)` on a file list, sorting
  does nothing, yet the context menu still offered every sort field and
  direction. The submenu entry is now disabled there.
- **A disabled submenu entry no longer opens its submenu.**
  `UltraCanvasMenu` drew a submenu entry with `enabled = false` greyed out
  but still opened it on hover, so its items stayed reachable. It now opens
  on neither hover, click nor keyboard.
