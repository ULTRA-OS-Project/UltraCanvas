#### 2026-10-10 *1.0.5*
- **`--help` and `--version` print again.** Both wrote to the framework's
  debug log, which a Release build keeps off, so `UltraViewer --help` printed
  nothing at all - on Linux and macOS as much as on Windows. They now write
  to stdout, and an unknown argument is reported on stderr. On Windows the
  text reaches the prompt as well: the framework attaches a GUI program to
  the prompt's console before `main()` now (framework changelog, pending
  entry `windows-gui-apps-print-help`).

#### 2026-10-06 *1.0.4*
- **Save image as saves an SVG.** Saving a drawing wrote nothing: the dialog
  offered `<name>.svg`, which the viewer cannot write, and the failure showed
  only in the info line at the bottom. It now offers `<name>.png` and says in
  a message when a save fails. The fix is in the framework's media viewer
  (`Docs/UltraCanvas/changelog.d/media-viewer-save-svg.md`); the viewer
  therefore shows message boxes now - a failed save and a replace prompt -
  which follow the framework-dialog setting like its file dialogs.
- **The file type chosen in Save image as is the format saved.** Picking JPEG
  and typing `photo` saved nothing - a name without an extension is no format
  the viewer knows - and the type list was ignored; it now saves `photo.jpg`.
  The dialog also starts on the type of the file shown (a JPEG opens on
  JPEG) and renames the file when another type is picked. Framework change:
  0.9.176 ("A Save dialog gives the name the chosen file type's
  extension").
- **A failed save leaves no empty file.** Saving as AVIF on a build without
  an AV1 encoder left a 0-byte `.avif` behind, and the same failure over an
  existing file emptied it. The image is now written to a temporary file and
  put in place only when complete. Framework change:
  `Docs/UltraCanvas/changelog.d/image-save-staged.md`.

#### 2026-10-01 *1.0.3*
- **Open and Save use the framework's new file dialog.** The toolbar's Open
  button showed the platform's picker, because `main.cpp` turned native
  dialogs on and `UltraCanvasFileLoader`'s file dialogs follow that setting.
  It is now off, so Open (several files at once, as before) and Save image as
  open `UltraCanvasFileDialog`: the folder tree, the filer-widget listing with
  its Details / list / icon views, and the same look on every platform. The
  viewer shows no message boxes, so nothing else changes.
- **Open picks file types with toggle buttons.** Below the file name the
  dialog has a row of buttons - Images, Audio, Video, Documents, Text and
  All files - instead of a dropdown of extension lists. Turn on as many as
  you want; the listing shows the files any of them matches. All except All
  files start on, so every file the viewer can open is listed from the
  start. Each button holds every extension of that kind the viewer opens,
  including what a plugin or codec adds: Images covers bitmaps, vector
  drawings and 3D models, Text covers plain text and every programming
  language the viewer highlights, and Documents holds PDFs, spreadsheets,
  e-books and fonts. Hover over the buttons for the extensions.

#### 2026-09-28 *1.0.2*
- **The version is in the window title** — `UltraViewer 1.0.2` — so a screenshot or a
  bug report says which build it came from. The number is this changelog's
  first line, as everywhere else (`cmake/UltraCanvasVersion.cmake`). The build also stops passing the framework's version as `ULTRAVIEWER_VERSION`: it is now UltraViewer's own, from this file.

#### 2026-09-19 *1.0.1*
- **UltraViewer has a new icon.** The four coloured arcs around a black play
  button (`media/appicon/UltraViewer.svg`) replace the purple picture-frame
  tile everywhere the viewer is drawn: the window and the taskbar entry that
  follows it (`SetDefaultWindowIcon` and the `UCAPP_ICON_PATH` fallback), the
  icon compiled into the Windows `.exe`, and the launcher in an application
  menu and in a filer. Only the SVG had been redrawn, and that is the
  *scalable* half of a pair - the window icon, the `.ico` the build generates
  and the `256x256` entry `make install` writes all read
  `media/appicon/UltraViewer.png`, which still held the old artwork. The PNG
  is now rendered from the SVG (padded to a square on a transparent
  background; the drawing is 131 x 128 units), so the scalable and the
  fixed-size icon agree at every size.
- **UltraViewer has a desktop entry.** `Apps/UltraViewer/UltraViewer.desktop`
  is installed to `share/applications`, with the PNG and the SVG installed to
  `share/icons/hicolor/256x256/apps` and `share/icons/hicolor/scalable/apps`.
  `Icon=UltraViewer` is an icon *name* resolved through the installed themes,
  and UltraFiler finds an application's icon by reading its desktop entry, so
  without one the viewer had no icon in the filer at all - the new artwork
  would have shown in the window and nowhere else. The entry also registers
  the viewer with the desktop for the media it displays (`Exec=UltraViewer
  %F`, matching the paths `main()` already accepts), so a file opened from a
  filer arrives the same way `UltraViewer photo.jpg` does.

#### 2026-08-31 *1.0.0*
- **UltraViewer keeps its own changelog from here.** Everything up to and
  including this version shipped as part of a framework release and is recorded
  in [`Docs/UltraCanvas/CHANGELOG.md`](../UltraCanvas/CHANGELOG.md) — nothing
  was rewritten or moved, so that history stays where it was published. From
  now on a change to the universal media viewer (`Apps/UltraViewer`) is
  described here and carries this file's version, and UltraViewer no longer
  moves when the framework releases.
- A framework change UltraViewer needs still belongs in the framework
  changelog. Cross-reference it from here when a release depends on it; never
  describe one change in two files under two version numbers.

<!--
Version source of truth: the first line of this file, format
`#### YYYY-MM-DD *x.y.z*`, read by cmake/UltraCanvasVersion.cmake.
-->
