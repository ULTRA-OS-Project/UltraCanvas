- **DropdownKeyboardTest runs in CI, and checks the dropdown's focus outline
  on real pixels.** The suite opens a real window, so in "Run unit tests" -
  which has no display - it skipped as a whole, its keyboard checks (Return
  choosing, Tab and the focus, type-ahead) included. It now runs in the
  virtual-display step next to the tree and list view focus tests, with
  `ULTRACANVAS_TEST_DISPLAY_REQUIRED=1` turning its skip into a failure. Its
  new section reads the dropdown button's edges back from the window
  (`DisplayTest::RingSides`): the outline in `focusBorderColor` on all four
  sides while the dropdown is focused and closed, none while its list is open
  or once the focus has gone, and the plain border back on all four sides.
