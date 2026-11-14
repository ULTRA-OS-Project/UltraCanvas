- **TreeViewKeyboardTest said the tree view's focus ring stayed after the
  focus left; it was the label.** The check looked for a white pixel anywhere
  in the focused row, and the theme draws a selected row's label in white
  (`onAccent`) as well, so the row still "showed the ring" with none drawn -
  and the check that the ring is there passed on the label alone. The tree was
  right: `IsFocused()` goes false with the window's focus and the ring goes
  with it. The ring is now looked for along the row's edges, where it runs, on
  all four sides while the tree is focused and on none once it is not; a
  build that draws the ring regardless of focus fails the test.
- **CI runs the focus-ring half of TreeViewKeyboardTest.** A new Linux step
  runs it under `xvfb-run` with `ULTRACANVAS_TEST_DISPLAY_REQUIRED=1`, which
  turns the test's skips (no display, no window) into failures; "Run unit
  tests" has no display, so there that half skipped and the failure went
  unseen.
