- **The startup screens open in the middle of the app's window, not the
  middle of the monitor.** `UltraCanvasSplashScreen::Show` centred the splash
  on the parent window's screen, so with the main window anywhere but
  screen-centre the splash sat off to one side of it. It now centres over the
  parent with `CenterOnParent`, clamped to the parent's monitor, and still
  centres on the screen when no parent is given. UltraTexter's splash is the
  one caller.
  - DemoApp's startup information window gets the same treatment: it is
    created with the main window as its parent and centred over it after it is
    shown. Before, it was left wherever the window manager put it.
- **The root build's text editor target is now `Texter`.** It was
  `UltraCanvasTexter`, so the program was `UltraCanvasTexter`,
  `UltraCanvasTexter.exe` and `UltraCanvasTexter.app`; they are now `Texter`,
  `Texter.exe` and `Texter.app`. `cmake --build … --target Texter` builds it.
  `package-linux.sh` (the `Texter` launcher), `package-macos.sh`,
  `package-win.sh` (the signing step) and `.gitignore` follow, and the
  Windows version resource names `Texter.exe`. The macOS bundle identifier
  stays `com.cloverleaf.UltraCanvasTexter` so existing preferences and
  signing identity carry over. The standalone `Apps/Texter` build still
  produces `UltraTexter`.
