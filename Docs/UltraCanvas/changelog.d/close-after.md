- **A start-up and shut-down test can end the program:
  `ULTRACANVAS_CLOSE_AFTER_MS`.** With the variable set, `Run()` asks every
  window to close after that many milliseconds, the way the window
  manager's close button would (`UltraCanvasApplication::CloseAllWindows()`,
  which an application or a test calls itself too): the window's
  `onWindowClosing` may refuse, `onWindowClosed` runs, the settings are
  written, and `Run()` returns. `ULTRACANVAS_CLOSE_AFTER_MS=2000 xvfb-run
  ./ultrapress` opens, runs two seconds and ends, with no X client to send
  the close.
- **The start page has a headless test of its own: `StartPageTest`.** The
  parts laid out top to bottom, the preferred height growing by what each
  takes, the recent entries as buttons that hand back the whole entry, the
  action as wide as the column, a section at the margin and the rows put
  in it, the status line in the error colour. It and `AppSettingsTest`
  run on the macOS and Windows CI rows too (`ULTRACANVAS_BUILD_STARTPAGE_TESTS`,
  `Tests/StartPageTests.cmake`), with the UltraPress suites and the
  UltraFIBU start window test, on each platform's own path rules.
