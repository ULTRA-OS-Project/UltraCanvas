- **One settings file, the framework's.** The look, the sites opened last
  and the editor window's size live in `<settings folder>/UltraPress/
  settings.json` (`ui/UltraPressSettings.h`, a typed view over the
  framework's `UltraCanvasAppSettings`), the place for the next
  preference; the `look` file of 0.8.0 to 0.8.2 is read once and goes when
  the settings are first written. The editor window opens at the size it
  had last. The program holds one settings object: the look and the window's
  size are written together as the editor closes, the recent list as the
  start window changes it - *Dark look* / *Light look* writes no file on
  every click, and nothing reads the file back before writing it.
- **A start-up and shut-down test ends the program by itself.** With
  `ULTRACANVAS_CLOSE_AFTER_MS=<ms>` the framework closes the window after
  that long, through the same path as the close button, so the window's
  size is saved and the settings written; no X client is needed under Xvfb.
- **The engine and render tests run on macOS and Windows too**, in a CI
  step of their own, on each platform's own path rules and settings folder.
