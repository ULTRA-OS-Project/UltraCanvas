- **A dropdown takes the keyboard focus.** `UltraCanvasDropdown` returned
  false from `AcceptsFocus()`, so Tab never reached it and its own keys -
  Down or Space to open it - were never delivered: a dropdown could only be
  opened with the mouse. It takes the focus now as a button does, from Tab
  and from a click, and draws `focusBorderColor` while focused;
  `SetAcceptsFocus(false)` leaves one out of the Tab order again. Dropdowns
  therefore join the Tab order of every dialog that has one. The focus stays
  on the dropdown while its list is open: `UltraCanvasListView` (1.4.0) gains
  `SetAcceptsFocus`, and the popup list turns it off - a click on an entry
  used to move the focus to the list, which then closed. `DropdownKeyboardTest`
  covers Tab, a click, Down / Space / Return / Escape and the opt-out.
