- **`FindProgram` / `LaunchProgram` find a sibling application on macOS.**
  Every macOS application is a bundle of its own, so "next to this
  executable" (`<App>.app/Contents/MacOS`) never held another application and
  `LaunchProgram("UltraViewer")` failed unless it was on `PATH`. Before `PATH`
  they now try `<name>.app/Contents/MacOS/<name>` beside this application's
  bundle, then in `~/Applications` and `/Applications`
  (`UltraCanvasDesktopShell`).
