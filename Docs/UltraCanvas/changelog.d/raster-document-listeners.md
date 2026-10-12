- **`UCRasterDocument` takes several observers.** Its four notification
  callbacks (`onPixelsChanged`, `onStructureChanged`, `onSelectionChanged`,
  `onStateChanged`) were single slots, so a second observer - the window's
  layer panel behind the paint surface, then the magnifying glass - had to
  save the slot it found and chain itself behind it, and the last one to
  install won. `AddListener(Listener)` / `RemoveListener(id)` give each
  observer a listener of its own; a notification fires the slot first and
  then every listener, on the list as it was when it began, so a callback
  may remove its own listener. `UltraCanvasPaintSurface` and UltraPaint's
  window use them; the slots stay for the one-observer case.
  `RasterEditingTest` covers it.
