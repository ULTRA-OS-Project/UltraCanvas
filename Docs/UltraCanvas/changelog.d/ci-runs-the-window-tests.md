- **The window tests that nobody ran now run in CI.** UltraMail's four
  display-backed suites - the account bar, the folder tree, the message pane,
  the format bar - were built by every row and run by none: the Linux row's
  `ctest` has no display and selected `^UltraMailEngine$` alone, and the
  suites skip themselves without one. Five assertions of the account bar's
  suite had been failing since the tiles learned to be dragged, and nothing
  said so until someone read it by hand. They now run on a virtual display,
  as `FilerVanishedFolderTest` and the tree view's focus-ring test do, and so
  does UltraDesktop's new Apps window test. A suite that would skip in that
  step fails instead (`ULTRACANVAS_TEST_DISPLAY_REQUIRED=1`), so a missing
  display can never again read as a pass.
