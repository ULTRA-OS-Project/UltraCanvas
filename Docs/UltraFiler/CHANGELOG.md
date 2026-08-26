#### 2026-08-26 *0.9.0*
- Everything that landed after the 0.8.0 pre-release, collected into the first
  version this changelog drives. UltraFiler had no changelog of its own until
  now, and its version was a literal repeated across `CMakeLists.txt`,
  `UltraFiler.rc` and `UltraFiler.manifest` that only moved when someone
  edited all three by hand — so sixteen commits of work shipped while the
  title bar kept saying 0.8.0.
- **Folder tree:** a *Pinned* section above *Computer*; drag & drop onto tree
  nodes (dropping on *Pinned* pins a folder, dropping on a folder moves the
  files into it); the drive rows colourable, with both tree colours exposed as
  settings; and a background probe that decides a node's expand button off the
  UI thread instead of freezing the window on a slow or network volume.
- **File display:** per-folder view modes remembered across sessions, live
  folder watching, a paste-conflict dialog (Keep both / Replace / Skip),
  double-click running applications on POSIX platforms, and clicking a folder
  showing its content in the detail pane.
- **File operations:** move-on-drop with a drop setting and a full error
  dialog, *New > Folder*, and archive packing / unpacking behind a cancellable
  progress window.
- **Menus and window:** *Open with* first and clickable, *Display* moved down
  next to *Settings*, and the current folder path appended to the window title.

#### 2026-08-21 *0.8.0*
- Pre-release baseline: the version the three hand-maintained literals carried
  before this changelog existed. The work of this release and everything
  before it was written against
  [`Docs/UltraCanvas/CHANGELOG.md`](../UltraCanvas/CHANGELOG.md), where it
  still is.

---

The **first line of this file is the single source of truth** for the
UltraFiler version. `cmake/UltraCanvasVersion.cmake` parses it at configure
time into `ULTRAFILER_VERSION` (plus the `_DOT4` / `_COMMA4` variants the
Windows resource files want), so the number in the window title, the About
dialog and the `--version` output always matches the build it came from.

To release, add an entry at the top — that is the whole bump — then run
`./set-version.sh`, which writes `UltraFiler.rc` and `UltraFiler.manifest`
(windres reads those from disk, so they cannot be generated). A CMake
configure on any platform warns when they have fallen behind.

Format: `#### YYYY-MM-DD *x.y.z*`. UltraFiler versions itself: it does not
move when the framework releases. Framework changes the app needs still belong
in [`Docs/UltraCanvas/CHANGELOG.md`](../UltraCanvas/CHANGELOG.md).
