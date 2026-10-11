- **`UltraCanvasButton::onDoubleClick`.** A button counted the second click
  of a double-click as another press, so that rapid clicks are not lost: a
  toggle button toggled back, and a button could not give a double-click a
  meaning of its own - a list row that selects on a click and renames on a
  double-click had no way to tell. Set, `onDoubleClick` takes the left
  double-click instead of that second press (the first click has already
  pressed or toggled the button); unset, nothing changes. UltraPaint's layer
  names use it to rename in place. `MouseClickBubblingTest` double-clicks a
  toggle button: the first click toggles it, the double-click reaches
  `onDoubleClick` and neither toggles it back nor climbs to its parents, and
  a middle-button double-click is not taken for it.
