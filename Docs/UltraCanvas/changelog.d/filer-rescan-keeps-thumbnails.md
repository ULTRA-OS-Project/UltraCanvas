- **FilerWidget: a rescan keeps the thumbnails of unchanged files.** Every
  `Refresh()` - the one after a delete, a rename, a paste, the folder watch's -
  used to drop the whole thumbnail cache, so the folder was drawn with type
  glyphs until the thumbnails were decoded again. A rescan of the folder
  already shown now keeps the thumbnails, text pages, folder peeks and picture
  proportions of every file whose path, size and modification time are
  unchanged (`PruneThumbnailCache`). A changed file is decoded again in the
  background while its tile keeps the old picture (`ThumbSlot::stalePixmap`).
  Opening another folder or changing the view still drops everything.
  `Tests/FilerRescanKeepsThumbnailsTest.cpp` covers a delete, a change and a
  folder switch.
