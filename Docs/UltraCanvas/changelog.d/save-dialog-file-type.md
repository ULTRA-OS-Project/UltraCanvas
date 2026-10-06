- **"Files of type" in a save dialog now decides the extension.** Every save
  dialog - the framework's `UltraCanvasFileDialog` and the native ones on
  Windows and Linux - returned the name exactly as typed, whatever type was
  chosen: "photo" with JPEG picked came back as "photo", and each application
  then saved it as whatever it fell back to (UltraPaint and the media viewer a
  PNG). The dialogs now share one set of rules (`FileNameExtension`,
  `FileFilterIndexForName`, `FileNameForFileType`,
  `FileNameWithTypeExtension`, next to `FileFilter` in
  `UltraCanvasModalDialog.h`; documented in `UltraCanvasFileDialog.md`):
  - a save dialog starts on the type of the name it offers - `photo.jpg` on
    JPEG - unless the first type already describes it ("All files" does);
  - picking a type renames the file (`photo.png` → `photo.jpg`); an extension
    that is none of the dialog's types is kept and the type's added
    (`notes.v2` → `notes.v2.png`), and "All files" leaves the name alone;
  - OK gives a name without an extension, or with one none of the types
    offers, the chosen type's extension (`photo` → `photo.jpg`); an extension
    of one of the offered types was typed on purpose and is kept. The framework
    dialog does this before its Replace File question, and the GTK chooser asks
    again when the new name is a file that exists.
  - Windows: the `IFileSaveDialog` starts on the matching type and is given a
    default extension, so it follows the chosen type itself; a dialog that
    starts on "All files" applies the chosen type when it closes. macOS's save
    panel has no type list and already requires one of the extensions.
  - Linux: the GTK chooser's type-change renaming now uses the shared rule, so
    `photo.jpeg` stays a JPEG name and `notes.v2` is no longer cut to `notes`.
