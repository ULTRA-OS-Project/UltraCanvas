- **The file dialog's Up arrow sits in the middle of its button.** The button
  was created without a label, and `UltraCanvasButton`'s constructor defaults
  the label to "Button", so it was laid out as an icon beside text: the arrow
  started at the left padding, 6px right of centre in the 28px button, and
  lost the tip of its right arm to the button's edge. It is created with an
  empty label now.
- **The file dialog's view buttons have their glyphs in the middle.**
  `UltraCanvasSegmentedControl` drew a segment's icon at the segment's left
  padding even when the segment had no text, so wherever the segments are
  wider than icon + padding - every equal-width control - the glyph sat to the
  right: 3px in the file dialog's six view buttons. An icon-only segment now
  centres its icon; a segment with text is laid out as before.
- **The file dialog's listing has no hover icon menu.** The Copy / Cut /
  Rename / Delete strip that `UltraCanvasFilerWidget` floats over the file
  under the pointer belongs to a file manager, not to a picker, and covered
  the names being chosen from. Every mode of the dialog (Open, Open multiple,
  Save, Select folder) now leaves it off; `FileDialogConfig::hoverIconMenu`
  (`FileDialogOptions::SetHoverIconMenu` through `UltraCanvasFileLoader`)
  brings it back for a caller that wants it.
- New display test `FileDialogLayoutTest` reads the dialog's pixels back and
  checks all three, skipping without a display like the other window tests.
