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
