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
  action as wide as the column, a section at the margin at the height
  asked for, the status line in the error colour.
