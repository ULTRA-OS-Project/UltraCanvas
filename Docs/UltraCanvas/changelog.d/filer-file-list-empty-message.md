- **`UltraCanvasFilerWidget::SetFileListEmptyMessage()`**: what an empty file
  list says in the middle of the display, instead of "No entries". A search
  can now explain an empty result: what it looked through, and what it left
  out. `ShowFileList()` resets it, so History and Favorites keep their "No
  entries". The empty-display notice (`DrawEmptyState`) draws a message of
  several `\n`-separated lines, each centred; before, it drew one line,
  however long.
