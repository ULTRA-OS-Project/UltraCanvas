- **List view: which cell a double-click hit, and where a cell is.**
  `onCellDoubleClicked(row, column, posInCell)` follows `onItemDoubleClicked`
  with the column, and `GetCellRect(row, column)` gives a cell's box as it is
  drawn now (element-local, scrolled), so an owner can lay a text field over a
  cell and edit it in place. The list view's documentation shows how;
  UltraDBAdmin's Browse grid is the first to do it.
- **FileLoader: `SaveFile(path, bytes, error)`**, the counterpart of
  `LoadFile`: bytes written to a local file as they are, for content no format
  writer stands for (a database blob, a download kept as it came). The file is
  written beside its name and moved there when complete, so a write that fails
  never leaves half a file in place of the one that was there.
