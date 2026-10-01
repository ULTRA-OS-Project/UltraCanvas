- **The file dialog also remembers its Details column widths and the last
  folder.** `FileDialog.conf` (beside the view and window size it already
  kept) now holds the Size / Type / Modified widths the user dragged the
  columns to, and the folder the dialog last showed. That folder is where
  the next `UltraCanvasFileDialog` opens when the caller names no starting
  folder — UltraMail's *Attach file* — while a folder the caller does name
  still wins.
