- **`UltraCanvasDialogTypes.h`: `UltraCanvasModalDialog.h` and
  `UltraCanvasFilerWidget.h` no longer include each other.** The dialog
  enums (`DialogType`, `DialogButton(s)`, `DialogButtonRole`, `DialogResult`,
  `DialogAnimation`, `DialogPosition`, `InputType`, `FileDialogType`) and
  `FileFilter` moved into the new header, which the dialog header includes, so
  every user sees the same names. The Filer header includes only that one, so
  it no longer pulls in the whole dialog system.
