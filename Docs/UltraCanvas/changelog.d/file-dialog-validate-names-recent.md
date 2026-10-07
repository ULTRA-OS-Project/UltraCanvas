- **The file dialog refuses a name the file system cannot hold.**
  `FileDialogConfig::validateNames` (on by default) was declared and never
  read, so a Save name like `a:b` on Windows, `CON.txt`, or one longer
  than 255 reached the caller's write and failed there, usually with a
  message about the write rather than the name. Save now refuses it with
  the reason - "\"a:b\" cannot be used as a file name: it contains ":"" -
  and stays open on the name. The rules are the new
  `InvalidFileNameReason(name, FileNameRules)` (`UltraCanvasModalDialog.h`):
  empty, "." and "..", control characters and names over 255 (bytes on
  POSIX, UTF-16 units on Windows) everywhere; on Windows also
  `< > : " / \ | ? *`, a trailing dot or space and the device names (CON,
  PRN, AUX, NUL, COM1-9, LPT1-9, with any extension). `validateNames =
  false` lets such a name through, as before.
- **The file dialog adds what it opens and saves to the recent files
  itself.** `FileDialogConfig::addToRecent` (on by default) was declared and
  never read: only `UltraCanvasFileLoader` registered recent files, after
  the dialog closed, so a dialog built with
  `UltraCanvasDialogManager::CreateFileDialog` added nothing. The dialog now
  calls `UltraCanvasFileLoader::NotifyRecentFile` for every file an Open,
  Open multiple or Save accepts (not for a folder picked). The loader hands
  its `FileDialogOptions::registerAsRecent` to the dialog and registers only
  after a native dialog, so a file is not added twice.
- `FileDialogTest` checks the name rules for both systems (always run), and
  under a display a refused name, `validateNames` off, and - on Linux, with
  GTK's recent-files store moved to the test's folder - `addToRecent` on
  and off.
