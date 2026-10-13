- **The tree view answers the keyboard.** `UltraCanvasTreeView` now takes the
  keyboard focus (`AcceptsFocus()`), by a click on it or by Tab, and while it
  holds it the arrow keys move the selection: Up / Down to the previous / next
  row, Left collapses or goes to the parent, Right expands or goes to the first
  child, Page Up / Page Down move a page of rows, Home / End go to the first /
  last row, Enter toggles a parent and Space toggles the check flag. The
  handler existed but never ran: the tree did not accept the focus, so no key
  event reached it, and it compared `nativeKeyCode` against Windows
  virtual-key numbers that match nothing on Linux or macOS. It reads
  `UCKeys` now, like the list view. A tree with no row focused yet starts at
  the first row on Down / Home and at the last on Up / End; a hidden root is
  never a row (Up from the first top-level row stays there); and a handled key
  returns true from `OnEvent`, so a scrolling parent does not scroll on the
  same arrow. `GetFocusedNode()` returns the row the keys continue from.
  `Tests/TreeViewKeyboardTest.cpp` drives every key headless.
