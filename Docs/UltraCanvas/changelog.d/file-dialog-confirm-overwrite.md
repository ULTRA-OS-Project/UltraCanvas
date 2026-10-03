- **The file dialog asks before Save replaces a file.** `UltraCanvasFileDialog`
  in Save mode accepted a name that was already a file without a word, so
  every application saving through it overwrote silently. The platforms' own
  save dialogs (GTK, Windows, macOS) all ask, so a choice between native and
  framework dialogs also decided whether the user was asked. It now asks too:
  a **Replace File** question ("… already exists. Do you want to replace
  it?") on OK, on a typed name and on a double-click in the listing. No
  leaves the dialog open on that name. `FileDialogConfig::confirmOverwrite`
  (`FileDialogOptions::SetConfirmOverwrite` through `UltraCanvasFileLoader`)
  turns it off for a caller that asks itself. The new
  `Docs/UltraCanvas/UltraCanvasFileDialog.md` describes the dialog: its
  modes, the filter toggles, the overwrite question and what it remembers.
