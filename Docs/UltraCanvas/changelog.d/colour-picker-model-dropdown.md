- **The colour picker's HSV/HSL/RGB dropdown is a real dropdown.**
  In `ColorPickerModeSelector::Dropdown` mode the picker (1.6.1) painted its
  own button and three-row popup and hit-tested them itself. It is an
  `UltraCanvasDropdown` child now, placed and styled as the button was, so
  its list opens as a window popup above the rest of the window. Each entry
  still explains its model when the pointer rests on it.
  `ColorPickerModelDropdownTest` covers it.
- **A dropdown entry can have a tooltip.** `DropdownItem::tooltip` is shown
  when the pointer rests on the entry in the open list (the list's
  `ToolTipRole`).
