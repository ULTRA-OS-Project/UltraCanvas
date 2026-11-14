- **The device tree no longer scrolls back to the top while you read it.** The
  scan timer rebuilds the tree row by row, so the device being read scrolled
  out from under the pointer every time the inventory came back - the more
  devices, the worse. The rebuild now holds the view (the framework's
  `TreeRebuildScope`): the scroll position and the selected row's place on
  screen survive it, and a device that has gone is still replaced by the
  computer row, scrolled into view.
