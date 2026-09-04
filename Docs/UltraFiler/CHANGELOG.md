#### 2026-09-04 *1.19.0*
- **Settings > Display > File extensions.** Two switches on one page: whether
  the file display's names still end in their extension, and what the thumbnail
  tiles show about the file type instead — nothing, a bar across the foot of the
  icon with the extension at its right end, or that tag on its own in the
  corner. Hiding the extension only changes what is drawn: renaming, sorting,
  the Type column and every file operation keep using the real name, so a
  hidden extension can neither be lost nor duplicated by a rename, and a name
  whose tail is a version rather than a type (`UCDemo-Windows-0.3.27-x86_64`),
  a dot file and a folder with a dot in it are all left alone. Both settings are
  saved to `config.ini` (`display.extensions.in.names`,
  `display.extensions.badge`) and applied to every file display of the window;
  the display's own `Display > File extensions` submenu carries the same
  switches, and flipping one there is the same setting saved the same way.
- **A file display opened later starts configured.** The Display > Thumbnails
  and Display > Detail view switches were pushed into the file displays that
  existed when a setting changed, so a tab opened afterwards — and the History
  and Favorites lists — came up with everything switched on regardless of what
  was saved, until the next settings change swept them up. Every newly created
  display now takes the saved Display settings at creation.

#### 2026-09-03 *1.18.1*
- **The Fonts kind persists.** The framework grew a tenth preview kind — font
  files, thumbnailed as a line of their own glyphs — and both list-of-files
  pages show it and its twelve formats without any change here, because they
  are built from what the widget reports. Its kind switch was the one thing
  that did not survive a restart: the config file names the kinds it stores
  rather than storing a mask, so a kind with no name is simply not written and
  comes back on. `fonts` is now one of those names, and Settings > Display >
  Thumbnails > Fonts stays switched off across launches like every other kind.

#### 2026-09-03 *1.18.0*
- **Settings > Display > Thumbnails and Settings > Display > Detail view: two
  lists of files.** Each page shows the nine file kinds as one checkbox each
  and, under every kind, the individual formats belonging to it — so a single
  format (an EPS on a slow share, a PSD that takes a second to decode) can be
  excluded without losing the thumbnails of everything else in its kind. A
  format this build cannot show is listed but greyed, because seeing that
  `dxf` is unsupported here is what explains a missing thumbnail, which an
  omitted row would not. Both pages carry *Everything on* / *Everything off*.
  The file display's own `Display > Thumbnails` and `Display > Detail view`
  submenus keep the quick per-kind switches and end with *File formats…*,
  which opens the matching page. It is all one setting: a switch flipped in
  either place is saved to `config.ini` (`display.thumbnails.kinds.off`,
  `display.thumbnails.formats.off`, `display.detailview.kinds.off`,
  `display.detailview.formats.off`) and applied to every file display of the
  window — each tab, the folder preview, and the History and Favorites lists.
- **The lists hold every format this build can open.** They are built from the
  FileLoader's own inventory (plus the file display's format table), so what
  the application can open and what the lists can switch cannot drift apart —
  audio files, which had fallen through entirely, are in both lists now, and
  the *Audio* group of *Detail view* is what turns the player pane on and off.
  Both settings are persisted as what is switched **off**, so a format or a
  kind a later version adds arrives switched on rather than missing from an
  existing `config.ini`.
- **The detail pane follows those switches.** It used to open for whatever the
  media viewer could show, which is why switching a kind off left the pane
  showing the very files whose thumbnails had just been switched off. A
  double-click follows the same rule: a file the detail view is switched off
  for opens in its application instead of in the pane.
- **EPS files have thumbnails and a detail view; CorelDRAW and Xara files have
  a detail view.** See framework 0.3.94 — the filer widget and the media viewer
  are framework code, so the work is described there.

#### 2026-09-02 *1.17.1*
- **UltraFiler called itself 0.8.0.** The window title, the `--version` output
  and the Windows file properties all came from a literal in the build files
  that was last edited when the app was at 0.8.0 — so every release since
  reported a version thirteen entries out of date, and the number a bug report
  quoted said nothing about which build it came from. The version now comes
  from the first line of this changelog like every other application's
  (`cmake/UltraCanvasVersion.cmake` already read it and already exported
  `ULTRAFILER_VERSION`; nothing consumed it). `UltraFiler.rc` and
  `UltraFiler.manifest` carry the matching number: those two are compiled from
  disk by windres, so they stay literal — but `./set-version.sh` now writes
  them and a CMake configure on any platform warns when they fall behind
  (framework 0.3.92).
- Thumbnails and vector graphics: see framework 0.3.92. The filer widget is
  framework code, so the fixes are described there.

#### 2026-09-01 *1.17.0*
- **Drives connected while UltraFiler is running now appear in the tree.** The
  drive rows were enumerated exactly once, at start-up, and there was no path
  back to that code: the *Computer* node is marked as already scanned, so
  collapsing and re-expanding it does nothing, and the toolbar's *Refresh*
  re-lists only the file pane. A USB stick, a card, an optical disc, a network
  share or a disk image plugged in afterwards was therefore invisible until
  UltraFiler was restarted — while the path strip's *Computer* dropdown, which
  re-reads the volume list every time it opens, showed it. `RefreshDriveNodes()`
  brings the rows back in line with what is mounted, driven by the framework's
  new `UltraCanvasVolumeMonitor`, so the operating system says when (no
  background polling). *Refresh drives* in the tree's context menu runs the same
  pass by hand.
- **A volume that is removed no longer leaves a dead row and a stuck tab.** Its
  row went on sitting in the tree, painted as a drive and leading into a folder
  that was gone; a tab inside it just showed an empty listing, and could not be
  left by going up, because its parent had gone too. The row and everything the
  tree remembered about it are dropped, any tab inside the volume moves back to
  the home folder, and the status bar names the volume that disconnected.
  Plugging the same stick back in gives a freshly scanned tree rather than the
  one the window last saw.
- **`/run/media` and `/Volumes` were never scanned.** The tree looked only at
  `/media` and `/mnt`, so on Fedora, RHEL, Arch and openSUSE — where udisks2
  mounts under `/run/media` — a stick was missing from the tree even after a
  restart, and on macOS, where every removable volume lands in `/Volumes`, no
  removable volume ever appeared at all. Both are covered now, together with the
  per-user directory level (`/media/bob/USB STICK`) for each.
- **The Cloud Storage lookup is repeated when a volume appears**, so a Google
  Drive that mounts as its own drive letter reaches the section instead of
  needing a restart. It ran exactly once, at tree build.
- Requires framework 0.3.91 (`UltraCanvasVolumeMonitor`, the folder watcher's
  failure callback, and the `UltraCanvasTreeView::RemoveNode()` fix that
  removing a populated drive row depends on).

#### 2026-09-01 *1.16.0*
- **The sub-folder search runs in the background and shows its matches while
  it walks.** Pressing "Search in sub folders" started a
  `recursive_directory_iterator` walk on the **UI thread**: the window stopped
  answering for as long as the tree took, which on a large volume was long
  enough for the desktop to report the application as not responding — and the
  walk followed reparse points, so a Windows profile's compatibility junctions
  ("Documents and Settings", "All Users") could send it round in circles until
  it fell over. The scan is now a worker thread with an explicit folder stack:
  symlinks and junctions are never entered, there is a depth cap on top of
  that, every directory error is contained rather than thrown, and the walk
  stops at 20 000 matches. Matches reach the display in batches roughly five
  times a second through the new
  `UltraCanvasFilerWidget::AppendToFileList()`, which keeps the scroll
  position and the selection — so results can be opened while the rest is
  still being found. The status bar counts matches and scanned folders as they
  arrive.
- **A "Scan sub folder" button inside the search field.** It appears as soon
  as there is something to search for and starts the same scan as Enter; while
  the scan runs it reads **Stop** and ends it, keeping what was found. The
  folder display's centered escalation button now carries the same wording.
  Editing the query, navigating, switching or closing the tab and clearing the
  field all end a running scan.
- Fixed: pressing Enter in the search field emptied the field. Dropping the
  as-you-type filter made the widget report a refresh, and the handler for
  that copied the widget's (now empty) filter text back into the field,
  because the tab was not yet marked as showing a search.
#### 2026-09-01 *1.15.1*
- **Fix: "UltraFiler crashed: exception 0xC00000FF … in ntdll.dll" /
  "exception 0x20474343 … in KERNELBASE.dll" on opening a folder.** Both
  dialogs are the Windows crash reporter's view of one thing: a C++ exception
  thrown where nothing could catch it — inside the window procedure, where an
  event handler on x64 cannot unwind back to `main()`'s `try`/`catch`, or on a
  background thread. Framework 0.3.90 catches the first at the window
  procedure and guards the filer widget's workers; this version does the same
  for UltraFiler's own threads. The subfolder probe that decides which tree
  folders get an expand button, the cloud-storage discovery and the Windows
  program launcher each catch what their work throws, log it (with the folder
  it happened on) and carry on. The probe and the tree's subfolder listing
  also step their directory iterators with the error-code overload: the
  range-for form throws when a read fails part-way through, which a removable
  or network drive can do at any time. An error in an event handler is now
  reported once in a dialog and logged after that; set
  `ULTRACANVAS_DEBUG_LOG` to a file path to capture the operation, file and
  error text if it recurs.

#### 2026-08-31 *1.15.0*
- **UltraFiler keeps its own changelog from here.** Everything up to and
  including this version shipped as part of a framework release and is recorded
  in [`Docs/UltraCanvas/CHANGELOG.md`](../UltraCanvas/CHANGELOG.md) — nothing
  was rewritten or moved, so that history stays where it was published. From
  now on a change to the file manager (`Apps/UltraFiler`) is described here and
  carries this file's version, and UltraFiler no longer moves when the
  framework releases.
- A framework change UltraFiler needs still belongs in the framework changelog.
  Cross-reference it from here when a release depends on it; never describe one
  change in two files under two version numbers.

<!--
Version source of truth: the first line of this file, format
`#### YYYY-MM-DD *x.y.z*`, read by cmake/UltraCanvasVersion.cmake.
-->
