- **CI runs the file dialog test on a display.** `FileDialogTest` checks the
  framework file dialog and the save-settings dialog on real windows, and
  skips that half without a `DISPLAY`. The Linux row's plain `ctest` has no
  display, and no virtual-display step ran this test, so only the naming
  rule and the settings store were ever checked there. That is how the
  check *its rows scaled down with the type* failed on every display from
  0.9.541 (#1206) until it was fixed on 2026-11-15, and nothing said so. The
  test now has its own step under `xvfb-run`, like the Filer's folder-watch
  test and the window shutdown test, with
  `ULTRACANVAS_TEST_DISPLAY_REQUIRED=1`. The suite now honours that variable:
  when it is set, missing `DISPLAY`, an application that will not start or a
  dialog that cannot be made counts as a failure, not a skip. A skip also no
  longer hides a failure from the checks that had already run.
  `DisplayTest::DisplayRequired()` (`Tests/DisplayTestSupport.h`) reads the
  variable, so the next display test does not need its own copy.
