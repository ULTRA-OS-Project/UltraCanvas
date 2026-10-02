- **The file dialog also remembers its Details column widths and the last
  folder - one for all applications, or one per application.**
  `FileDialog.conf` (beside the view and window size it already kept) now
  holds the Size / Type / Modified widths the user dragged the columns to,
  and the last used folder. That folder is where the next
  `UltraCanvasFileDialog` opens when the caller names no starting folder -
  UltraMail's *Attach file* - while a folder the caller does name still wins.
  - Whether the folder is shared by all applications (Global) or kept per
    application (Individual, each application with its own Global /
    Individual choice) is set in the new ULTRA OS settings application,
    UOS-Settings. An application switched to its own folder starts from the
    common one until it has used a folder of its own.
  - **`UltraCanvasFileDialogSettings.h`** (`UltraCanvas::FileDialogSettings`)
    reads and writes the file for the dialog and for UOS-Settings alike.
    Every change goes through `Update()`, which re-reads the file first, so
    one application's write never discards another's; the file is written
    beside itself and renamed into place.
