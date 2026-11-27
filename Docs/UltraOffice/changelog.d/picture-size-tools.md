- **The Picture panel's width and height.** The document's tool table has
  the selected picture's width and height in cm (`doc-picture-width`,
  `doc-picture-height`), read from what the editor lays out
  (`GetImageDisplaySize`, so a picture the file gives no size of shows its
  own) and set through `SetSelectedImageSize`, one undo step; with *Keep
  proportions* (`doc-picture-proportions`, on by default) the other side
  follows in proportion. All three apply while a picture is selected.
  `UltraOfficeToolTableTest` sets them on a picture of no stated size.
