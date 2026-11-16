- **CI runs every window test on a display, and a skip there fails.** 33
  of the framework's window tests open a real window, read it back, and
  skip themselves without a `DISPLAY`. Among them are `FileDialogTest`, the
  caret's stacking order, the clipboard's large copies, popups and tooltips
  in windows of their own, dialogs closed by Escape, the colour picker, and
  the text area's spelling, search and autocomplete. The Linux row's plain
  `ctest` has no display, and no virtual-display step selected them, so they
  passed by checking nothing. That is how `FileDialogTest`'s check *its rows
  scaled down with the type* failed on every display from 0.9.541 (#1206)
  until 2026-11-15, and nothing said so. They now run under `xvfb-run` in
  two new steps: one for `FileDialogTest` and one, *Run the framework window
  tests*, for the rest. That step checks its list against what ctest has
  registered, so a renamed or unbuilt test fails it instead of dropping out.
  All of them pass there.
  - Every window test now fails rather than skips when
    `ULTRACANVAS_TEST_DISPLAY_REQUIRED=1`, which the virtual-display steps
    set. The thirteen suites that already did read the variable two ways,
    some only `1` and others anything but empty or `0`; they all ask
    `DisplayTest::DisplayRequired()` (`Tests/DisplayTestSupport.h`) now.
    `FileDialogTest`'s skip also no longer hides a failed check of the half
    that did run.
  - It stays that way: `scripts/check_window_tests.py`, run by the new
    `window-tests.yml` on every pull request that touches a test, a CMake
    file or a workflow, fails a window test that no virtual-display step
    runs, a step that runs one without the variable, a skip that never asks
    `DisplayRequired()`, and a name a step lists that nothing registers. A
    window test is a test source that reads `DISPLAY` or opens the X display
    itself. A source that is not one says so with
    `// window-test-exempt: <why>`, and one ctest entry of several with
    `// window-test-exempt(<name>): <why>`, as `DesktopShellTest`'s
    D-Bus-only `DesktopShellTrayTest` and `DesktopShellPortalTest` do.
  - CI installs `xclip` with `xvfb`, so `ClipboardIncrTest` also checks a
    large picture copied to and from another program. The comment on that
    install no longer says it is for DesktopShellTest alone.
