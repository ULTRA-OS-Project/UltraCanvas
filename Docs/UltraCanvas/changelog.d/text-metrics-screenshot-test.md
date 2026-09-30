- **`TextMetricsScreenshotTest`: the text-metrics and crisp-border rules,
  checked on composited pixels.** A window with a checkbox, a radio, buttons,
  two text fields, a list view and the UltraMail filter menu is rendered under
  Xvfb and read back: a caps-only label and the indicator beside it share a
  centre line within a pixel, a checkbox border is one pixel wide, the caret
  spans the text's line box, the selection highlight and the caret are on
  screen, and the menu's radio outline is a circle. It skips itself without
  a display. With `ULTRACANVAS_SCREENSHOT_DIR=<dir>` it also writes the
  window as PPM screenshots for a person to look at, and with `GDK_SCALE=2`
  it renders at 2x (the PPM is read back at logical size; an X screenshot
  shows the 2x pixels). `AGENTS.md` now says how a test gets a focused window
  under Xvfb (there is no window manager to activate it, so the test
  dispatches the activation event itself) and why a text input shows no
  caret while a selection exists.
