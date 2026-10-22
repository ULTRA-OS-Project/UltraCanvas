- **`UltraCanvasAlbum` 1.11.0: `onItemPressed` and `ReleaseDrag`.** An
  application that carries a tile's drag itself - UltraVideo's bin, whose
  tiles land on the timeline - hears the left button press on a tile in
  `onItemPressed(index)`, arms its own drag there as it does from a button's
  press, and calls `ReleaseDrag()` when that drag starts, so the album's
  reorder (edit modes) does not follow the same press. `onItemDraggedOut`
  stays for an application that lets the album decide when a tile has left.
