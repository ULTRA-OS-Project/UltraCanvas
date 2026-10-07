- **A button made without a label has none.** `UltraCanvasButton`'s
  constructors and `CreateButton` defaulted the label to "Button". An
  icon-only button that left the label out carried it and was laid out as
  icon + text, so its icon sat at the left padding instead of in the middle
  (the file dialog's Up arrow, until the fix in 0.9.176 passed `""`). The
  default is now empty. Of the 291 buttons in the repository made without a
  label, 288 are given one right after; the other three are UltraTexter's
  search-bar *First match*, *Previous* and *Search options* icon buttons,
  whose icons now sit in the middle like *Next*'s, which already passed
  `""`.
- **`FileDialogConfig::defaultExtension` is used.** It was declared and never
  read. A Save name that still has no extension after the chosen type's -
  under All files, or with no filters - now gets it ("photo" -> "photo.png"),
  before the Replace File question like the rest of the Save naming. Empty
  (the default) leaves such a name bare, as before. It belongs to the
  framework dialog; `FileDialogOptions` has no counterpart.
