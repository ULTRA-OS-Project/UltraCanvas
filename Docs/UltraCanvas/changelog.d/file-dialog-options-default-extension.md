- **`FileDialogOptions::SetDefaultExtension`: a default extension through
  `UltraCanvasFileLoader`, native dialogs included.** Only a caller that
  built the framework dialog itself could give one
  (`FileDialogConfig::defaultExtension`); everything that saves through
  `UltraCanvasFileLoader::SaveFileDialog` - most applications, and every one
  with native dialogs on - could not, so "photo" saved under All files came
  back bare. The option reaches every dialog now, each applying it before
  its own Replace File question:
  - **framework dialog**: handed on as `FileDialogConfig::defaultExtension`;
  - **GTK**: applied to the accepted name and when the type changes, with
    the question asked if the result already exists;
  - **Windows**: the dialog's own default extension when the chosen type
    names none or there are no filters, and applied to the result as on GTK;
  - **macOS**: with no type naming an extension, the panel is given it as
    its only allowed type, other extensions still allowed, so it adds it and
    asks itself;
  - **Android**: added to the name offered to the document picker, which
    also gives SAF the type.
  The rule is public as `ApplyDefaultExtension(name, extension)`
  (`UltraCanvasModalDialog.h`).
