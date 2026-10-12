- **`UltraCanvasAlbum` 1.9.0: a tile dragged out of the album.** The new
  `onItemDraggedOut(index, windowPos)` fires once when a tile is dragged past
  the album's edge in an edit mode: the album's own reorder ends and the app
  carries the item on with the window's mouse events - UltraVideo's media bin
  hands its file to the timeline's drop path this way, so a tile lands on the
  main track at a time, or on the overlay lane, with the element's insertion
  marker. Without the callback nothing changes.
