- **UltraCanvasFilerWidget: a file type filter for load and save dialogs, with
  the other files hidden or greyed out.** `SetFileTypeFilter(extensions, mode)`
  (or the `FileFilter` a dialog's dropdown picked) narrows the listing to the
  wanted extensions. `FilerTypeFilterMode::Hide` leaves the other files out, as
  a file dialog does; `FilerTypeFilterMode::ShowDimmed` keeps them in the
  listing drawn greyed out - name, columns, icon and thumbnail alike, in every
  view - so the user still sees what else the folder holds, while a
  double-click or Enter on one does nothing and `onFileActivated` never fires
  for it. Folders always pass, archives still open. The filter survives
  `SetPath()`, rescans and the name filter; `EntryPassesFileTypeFilter()` tells
  a dialog's OK button whether the selection is a valid pick,
  `GetTypeFilteredCount()` how many files were hidden or dimmed, and a Hide
  listing with every file filtered out says "No files of the chosen type"
  instead of "Folder is empty!". `SetDimmedEntryOpacity()` sets how faint a
  dimmed entry is (0.38 by default). The demo's Filer page gained a *Types* row
  with the switch.
