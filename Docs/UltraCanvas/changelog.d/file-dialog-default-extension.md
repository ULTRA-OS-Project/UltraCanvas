- **A file dialog's default extension is honoured.** `FileDialogConfig`
  carried a `defaultExtension` that nothing read, and `FileDialogOptions`
  could not carry one at all, so a Save with "All files" chosen (or no
  filter) handed back exactly what was typed - "notes" with no extension.
  The framework dialog now adds it to a typed name that has none once the
  chosen type's rule has had its say (`WithDefaultExtension`): the type's
  extension always comes first, and a name that already has one, of any
  type, is left as typed, as the Windows save dialog treats its default
  extension. `FileDialogOptions::SetDefaultExtension` carries it through
  `UltraCanvasFileLoader`, and the Windows native save dialog takes it as
  its default when no file type names one. `Docs/UltraCanvas/UltraCanvasFileDialog.md`
  has the rule and the examples.
