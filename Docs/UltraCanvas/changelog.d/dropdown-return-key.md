- **Return chooses the highlighted item of an open dropdown.** The arrow
  keys moved the highlight in an `UltraCanvasDropdown`'s list, but Return
  chose nothing and left the list open: the dropdown does not take the
  keyboard focus, so the window hands the keys to the open list, which
  reports Return through `onItemActivated` - and the dropdown did not listen
  to it. It does now, through the same path as a click. `DropdownKeyboardTest`
  covers it.
