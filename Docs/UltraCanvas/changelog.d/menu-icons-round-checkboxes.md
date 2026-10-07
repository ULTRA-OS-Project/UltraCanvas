- **Menus line their labels up, and a checkbox can be round.** A vertical
  `UltraCanvasMenu` now has an indicator column (when any item is a Checkbox
  or Radio) and an icon column (when any item has an icon), and every label
  starts after both - an `Action` with no icon used to start its label at the
  left edge while its neighbours' labels started one column further in. An
  item with both a tick and an icon (a checkbox entry with an `iconPath`)
  shows them side by side; it used to be measured for one column and drawn
  across two, so its label ran past the menu's edge. The icon of a disabled
  item is drawn faded with its label, as a disabled button's is.
  `MenuStyle::checkboxShape` draws a Checkbox's tick inside a circle
  (`MenuCheckboxShape::Round`) instead of a box, and
  `SetDefaultMenuCheckboxShape()` sets it for every menu an application shows,
  including the ones elements build for themselves. Square stays the default.
  See *Checkbox Indicator Shape* and *Icon and Indicator Columns* in
  `Docs/UltraCanvas/UltraCanvasMenuExamples.md`.
- **The file display's context menu has icons.** Every entry of
  `UltraCanvasFilerWidget`'s context menu and of its *Display* submenu now
  carries an icon - a new line-icon set in `media/icons/menu/` (Open with,
  Copy, Cut, Paste, Delete, Delete Permanently, Duplicate, Rename, New,
  Compress, Extract, Print, Extras, Display, Settings, Sort, Type, File
  extensions, File icons, Thumbnails, Detail view, Dataset, Icon-Menu, Folder
  previews, Info-Bar, Hidden files). *Display > Type* shows each layout with
  the glyph UltraFiler's view selector uses, with two new ones,
  `media/icons/view-force-tree.svg` and `media/icons/view-3d.svg`, for the
  force-directed tree and the 3D view. The *Display* submenu also lists
  *Folder previews* in `UltraCanvasFilerWidget.md` now; it was in the menu
  but missing from the documented layout.
