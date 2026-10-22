- **`UltraCanvasAlbum` 1.11.0: `onItemPressed` and `ReleaseDrag`.** An
  application that carries a tile's drag itself - UltraVideo's bin, whose
  tiles land on the timeline - hears the left button press on a tile in
  `onItemPressed(index)`, arms its own drag there as it does from a button's
  press, and calls `ReleaseDrag()` when that drag starts, so the album's
  reorder (edit modes) does not follow the same press. `onItemDraggedOut`
  (1.9.0), which needed an edit mode and let the album decide when a tile
  had left, is gone: it had that one caller. UltraVideo's bin is a
  `Display` album now - the hover actions, a double-click and the drag.
