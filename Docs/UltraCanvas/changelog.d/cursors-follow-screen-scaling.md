- **Cursors drawn from a picture follow the screen's scaling.** The
  context-menu and magnifier cursors and the colour picker's eyedropper were
  drawn once, at the scaling of the screen they were first shown on, and kept
  that size after their window moved to a screen with other scaling - too
  small on a 200 % screen, too large back on a 100 % one. Windows and Linux
  now keep each such cursor's file and draw it once for every scaling it is
  asked for (`UCImageCursorCache`, `UltraCanvasImageCursorCache.h`); Windows
  picks the right one on the next pointer move, and Linux puts it up as soon
  as the window's scaling changes. `Tests/ImageCursorCacheTest.cpp` covers
  the cache.
- **macOS uses its own context-menu cursor.** `UCMouseCursor::ContextMenu`
  is `[NSCursor contextualMenuCursor]` on macOS: macOS draws it at the
  screen's resolution, where `context-menu.png` was scaled up and blurred on
  Retina, and there is no file to read - the PNG was read from disk again on
  every switch to that cursor. The magnifier cursor, still a picture on
  macOS, is now read once and kept.
- **The magnifier cursor on Windows.** `UCMouseCursor::LookingGlass` was a
  crosshair on Windows, because `looking-glass.png` could not be read there;
  it is now the same magnifier Linux and macOS show (the zoom tools of
  UltraPaint and ArtCreator). The crosshair remains the fallback when the
  picture is missing.
- **Window icons on Windows have clean edges.** `SetWindowIcon` copied
  cairo's premultiplied pixels straight into the icon, which Windows reads as
  straight alpha, so every soft edge of the taskbar and title-bar icon came
  out too dark. The icon and the image cursors now go through one conversion,
  `UltraCanvasWindowsApplication::IconFromPixmap`.
