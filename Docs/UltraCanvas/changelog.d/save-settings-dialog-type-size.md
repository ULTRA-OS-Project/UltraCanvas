- **The save-settings dialog fits the window it is opened from.**
  `UltraCanvasFileLoader::ShowSaveSettingsDialog` (the JPEG, PNG, WebP ...
  settings) was always drawn in the dialog's own 12-point type at a fixed
  420 wide, so opened from a 10-point settings window it towered over it,
  with a band of empty room right of the controls and a Defaults button
  fixed at 90 that a longer translation ("Standardwerte") filled edge to
  edge. The `FileSavePicture` overload now takes a `fontSize` last (0 keeps
  the dialog's own): the captions, controls and buttons are drawn at it and
  the rows, columns, slider and size fields scaled to it. At any size the
  window is as wide as its captions and controls need, and Defaults is as
  wide as its label, like OK and Cancel. `SettingsPageKit::
  ShowSaveSettingsDialog(extensionOrPath, parent)` opens it in a settings
  window's `kTextFontSize`. `FileDialogTest` checks the smaller dialog.
