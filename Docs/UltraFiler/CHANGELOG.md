#### 2026-09-10 *1.28.0*
- **A dragged-and-dropped move asks first.** Dropping files onto a folder of
  the file display carried the move out the moment the button came up, and a
  drag is the one file operation that starts by accident - a press that
  wandered a few pixels on the way somewhere else - so the first sign of it was
  a folder that had quietly emptied itself into a neighbour. *Settings >
  Handling > Drag & Drop* now carries **Confirmation**: **Always**, **Only when
  files are moved** (the new default) or **None**, the silent drop earlier
  releases had. The question names how many entries are about to be moved or
  copied and into which folder, with that folder's full path underneath -
  a drop lands on whatever was under the cursor - and nothing is touched until
  it is answered. Files dragged in from another program are copies, so only
  *Always* asks about those. (Framework side:
  `Docs/UltraCanvas/CHANGELOG.md` 0.8.1.)
- **The settings window opens on its sections instead of inside one.** The tree
  led with a *Settings* row that held everything and stood for nothing, below
  it every section and every page was already open, and the window opened on
  *Display > Treeview* - one page of eleven, picked because it happened to be
  first. Now the three sections - **Display**, **Handling**, **Extras** - are
  the top level of the list, all three closed, and the page beside them says
  what each one holds. Opening a section still moves straight on to its first
  page, and a page opened from elsewhere in the application (the file display's
  *File formats...* entries) opens its section on the way.
- **History & Favorites moved into Extras.** It was a section of its own, one
  row deep, sitting beside the three that group everything else; it is now the
  second page of *Extras*, next to *Open prompt*. What it does is unchanged.

#### 2026-09-10 *1.27.0*
- **The "+" of the tab strip can open the Home folder instead of the folder
  in front of you.** Opening a tab has always meant "the same folder once
  more", which is what a second view of the folder being worked in wants, and
  exactly wrong when the new tab is meant to start somewhere else entirely -
  every one of those began with a walk back up out of the folder the tab
  inherited. *Settings > Handling > Tabs* now chooses between the two: **New
  view of the current folder** (the default, unchanged behaviour) or **Open
  the Home folder**. Only the "+" follows the setting - a tab opened on a
  folder that was named, such as the containing folder of a search result or
  an entry of the History or Favorites view, still opens on that folder - and
  the choice is saved as `handling.tabs.new.tab` in the config file.
#### 2026-09-10 *1.26.0*
- **The Home tab is called Home.** A folder tab is named after the folder it
  shows, and the home folder is named after the account that owns it - so the
  tab the file manager opens on carried a login name and said nothing about
  what was in it. It now reads **Home**, with the home icon beside it: the
  same mark the folder tree's Home row and the Computer page's Home tile
  carry, so the three places that lead to that folder finally look alike.
  Every other tab is unchanged - it keeps the folder's own name and no icon.
  The name follows the tab: navigating into the home folder renames the tab
  and puts the icon on, leaving it takes both away again. The Computer page's
  Home tile, which carried the account name under the same icon, now reads
  *Home* as well (framework side: `displayNameProvider`,
  `Docs/UltraCanvas/CHANGELOG.md` 0.8.0) - the folder it opens, its name and
  what can be done with it are unchanged, only what the tile is labelled.
- **A tab says which of the user's places it is in.** The main user folders -
  Desktop, Documents, Downloads, Music, Pictures and Videos - now put their
  icon on the tab, the same icons the folder tree and the file display already
  draw for them: the monitor for Desktop, the download arrow, the picture, the
  notes, the play mark, the document. A tab keeps the icon *inside* the folder
  too, so one three folders deep in Downloads still shows the download mark -
  which is the whole use of a mark on a tab, and what the folder name alone
  ("2024") does not tell. The nearest folder wins, so a Pictures folder kept
  inside Documents shows the picture icon rather than the document one, and
  the home folder is deliberately not inherited downwards: everything the user
  has is under it, and an icon on every tab marks nothing.
- **A home icon with the user in it.** The plain black house has been replaced
  by `home-user.svg` - a house with its occupant, in the flat two-tone shape
  of the folder icons - in the tree, on the Computer page's Home tile and on
  the new Home tab alike. (Framework side: `Docs/UltraCanvas/CHANGELOG.md`
  0.8.0.)
- **The New folder arrow is worth aiming at.** The split button's menu section
  was marked with a "▾" character, which the text renderer drew a few
  pixels across inside a section nearly thirty wide: it read as a speck rather
  than as the "there is more here" arrow it is. It now carries the framework's
  `dropdown.svg` at 14 px, in the button's own text color, centered in its
  section. Nothing about what the button does has changed - the primary
  section still creates a folder, the arrow still lists the other kinds.

#### 2026-09-09 *1.25.0*
- **Double-clicking a font opens it, full size, in its own window.** A font was
  the one previewable kind whose whole point is the part the preview pane
  cannot hold: the pane shows a two-letter specimen, and what you
  double-clicked for is every glyph in the file. It now opens in a viewer
  window (`UltraCanvasMediaViewerWindow`) over the file manager, with the
  range picker, the size control and the arrow keys walking the rest of the
  folder — unless this system has an application registered for the file, in
  which case that opens it, the way Explorer would. Single-click still shows
  the font in the preview pane, and every other kind of file behaves exactly
  as before. The window is reused, so double-clicking through a folder of
  fonts leaves one window open rather than one per font; Escape closes it.
#### 2026-09-09 *1.24.0*
- **The search field kept its button and lost its text when the window got
  narrow.** Reducing the window width squeezed the search field away entirely
  and left a clipped sliver of the blue "Scan sub folder" button where the
  whole search control used to be — nothing to type in, and a button too
  narrow to read. A flex row shrinks its shrinkable child first, so the text
  input gave up all its width while the fixed-width button kept its 118 px.
  The button is the one of the two that can be done without: Enter runs the
  same scan, and the file display still offers it in the middle of an empty
  result. So the search box now reports when it no longer has room for the
  button *and* a readable field, and the button goes instead of the field.
  Widening the window brings it straight back. The answer is only known once
  the layout engine has sized the box, but acting on it *inside* that pass
  leaves the text input zero-wide for good — a trap now written down in
  `Docs/CSSLayout.md` — so the box reports and the window applies it on the
  next turn of the event loop.
#### 2026-09-09 *1.23.0*
- **Double-clicking a program or a document now shows that it is starting.**
  The launch itself is instant, the application appearing is not, and until
  it did the window looked exactly as it does when a double-click was missed.
  A second after the double-click the pointer changes to the arrow-with-busy-
  sign shape, and it returns to normal by itself — so a program that opens
  at once never changes the pointer, and a slow one no longer invites a
  second double-click. Windows programs launched through UltraWin use it too,
  with the long wait a first launch needs while its Windows environment is
  prepared, and put the pointer back the moment the run actually starts.
  (Framework side: `Docs/UltraCanvas/CHANGELOG.md` 0.3.111.)

#### 2026-09-08 *1.22.0*
- **The settings window reads the same on every page.** The pages had grown
  one at a time and it showed: a title smaller than the choices under it,
  choice labels in a different size from the checkboxes, explanations mixed
  in among the controls at the same size as the choices, a radio label cut
  off at the page's edge, and a *Restore default ...* button sitting wherever
  the page happened to end. Every page is now built the same way: a bold
  title, one line saying what the choice is about, the controls, all in one
  text size, and the notes that explain the setting set apart in a tinted
  block with an accent bar at the foot of the page, a step smaller and
  greyed, so what is acted on and what is read about it are never
  confused. Texts wrap instead of being cut off.
  - The *Restore default colours* / *Restore default widths* button of a
    page now sits at the left end of the window's bottom bar, opposite
    *Close*, the same spot on every page that has one; it is hidden while
    the shown page has nothing to restore.
  - Clicking a heading in the tree (*Display*, *Handling*, *Extras*, or
    *Settings* itself) moves on to its first page: a heading has no page of
    its own, so it and its first entry used to show the same content with
    two different rows selected.
- **Media Viewer > Transparent Images is gone.** The backdrop behind a
  transparent image is chosen from the colour strip the preview shows right
  under the picture, which previews the choice in place; the page that
  duplicated it with a full colour picker had nothing left to do. The
  setting itself is unchanged and still saved from the strip.

#### 2026-09-06 *1.21.0*
- **The file display says when a file is in use.** A file another program is
  holding — a running program, a document something has open, an archive being
  read — now wears a small padlock in the corner of its icon, carries `X`
  among its attributes and is described in the info bar ("In use by another
  program (cannot be replaced)"). That is the answer to a copy, a rename or a
  delete that fails with *"the action can't be completed because the file is
  open in another program"*, given **before** the attempt instead of after it.
  A file that is merely open somewhere without blocking anything — the normal
  state on Linux and macOS, where an open file can still be replaced — is
  marked `O` and wears no padlock, because it is information, not an obstacle.
  Nothing is written by the check: the file is opened for the access an
  overwrite would need, with every sharing flag granted, and closed again.
- **Attributes names the program.** For a single file the Attributes dialog
  adds an "In use" row that says which program is holding it ("In use by
  Firefox (1234)") wherever the system can be asked — Windows through the
  Restart Manager, Linux through `/proc`. That is the one question a file
  manager could never answer and the user always has.
- **Settings > Display > Files in use** turns the marking off. It costs one
  extra open per shown file, which is worth avoiding on a slow network volume;
  the page also says so, and disables itself where the system cannot answer.
  Everything is probed in the background, so a folder never opens slower for
  it.
- **UltraFiler no longer holds its own folder open.** A program's working
  directory is an open handle on that folder, and on Windows that alone stops
  the folder being renamed, replaced or deleted — so starting UltraFiler by
  double-clicking it made its own folder impossible to overwrite with a newer
  version for as long as it ran, with an error blaming a folder whose files
  all looked free. UltraFiler now moves its working directory to the home
  folder at start-up. A folder given on the command line is resolved before
  the move, so a relative path still means what it did.
- **A closed preview lets go of the file it was showing.** Closing the preview
  pane stopped playback of a video or a track but left the decoder holding the
  file, which on Windows blocked renaming, replacing or deleting exactly the
  file the user had just been looking at. The preview now unloads it
  (framework 0.3.106).
- **Browsing into an archive no longer locks it.** The archive stayed open
  behind the scenes after being listed, so on Windows a `.zip` that had been
  looked into could not be overwritten or renamed for the rest of the session
  — and deleting a file *inside* one failed, because that rewrites the archive
  and renames the new file over the old (framework 0.3.106).
- **The tree's Computer entry opens a page of its own.** Clicking *Computer*
  used to do nothing — it was a header over Home, Cloud Storage and the
  drives. It now replaces the folder display with the machine's places:
  **Folders** — Home and every Cloud Storage folder as folder tiles, which
  open on a double-click and carry the usual context menu — and **Drives** —
  one card per mounted volume with a **pie chart of used against free
  space**: green while there is room, amber past 75 %, red past 90 %, the
  percentage in the middle, the drive's name as the button that opens it,
  *232.9 GB free of 476.2 GB* and the mount point under it. The sizes are
  read on a worker thread, so a network share that stopped answering delays
  its own card and never the window, and the cards follow mounts and
  unmounts like the tree's drive rows do. The status bar sums the drives and
  the window title says *Computer*.
- **Up from a drive root goes to the Computer page**, the way Explorer's Up
  from a drive lands on *This PC*; the Up button is no longer greyed out at
  a root. The breadcrumb's leading *Computer* node opens the page too
  (framework 0.3.106: `FolderBreadcrumbOptions::onComputerClick`), and its
  dropdown still lists the drives. Esc, any navigation, Back / Forward or a
  tab switch returns to the folder display, with the tree, the breadcrumb
  and the status bar describing the folder again.

#### 2026-09-05 *1.20.0*
- **Windows shortcuts show the icon of the program they start.** A Desktop or
  Start-Menu folder full of `.lnk` files was a wall of identical grey "LNK"
  sheets that said nothing about any of them. Each one is now read: the tile
  carries the icon the shortcut names — the application's own icon, or the
  `.ico` a browser wrote for a web shortcut — with a small arrow badge in the
  corner marking it as a shortcut, the type column says `Shortcut`, and the
  info column and info bar show what it points at. A shortcut also groups and
  colours as the thing it stands for, so one to a folder sits with the
  folders. This works wherever the files are: a mounted Windows disk, a
  folder synced from a Windows machine, or the `drive_c` of an UltraWin
  environment — the icons are read out of the files themselves rather than
  asked of a Windows shell (framework 0.3.101: `UltraCanvasShellLink.h` and
  `UltraCanvasIconResource.h`).
- **Double-clicking a shortcut starts what it points at.** A shortcut to a
  folder opens that folder; a shortcut to a program runs it through the
  UltraWin environment it belongs to, the same way a double-clicked `.exe`
  already did — through Wine's `start`, so the arguments and working
  directory the shortcut carries are kept. A shortcut to a *document* is not
  a Windows program: it opens with whatever this system opens that document
  with. A shortcut to a program also
  counts as an application in the History and Favorites lists, which is what
  a Start-Menu entry is.
- **Attributes shows what a shortcut is.** For a `.lnk` the dialog adds the
  target, the arguments, the folder it starts in and its comment, plus where
  that target lives on **this** system — or "not found on this system" when
  the shortcut points at a program this machine does not have.

#### 2026-09-04 *1.19.2*
- **The main user folders have icons of their own — in the file display, not
  only in the tree.** Desktop, Documents, Downloads, Music, Pictures and
  Videos are drawn from `media/icons/user-desktop.svg`,
  `folder-documents.svg`, `folder-download.svg`, `folder-music.svg`,
  `folder-images.svg` and `folder-videos.svg` instead of the generic folder
  shape, so the Home folder reads at a glance the way Explorer's and Finder's
  do. The tree and the display now take them from the same place, so they
  cannot drift apart, and a redirected or localized folder ("Bilder", a
  Documents folder moved into OneDrive) gets its icon like any other — the
  match is on the resolved path, not on the name.
- **Extras > Set folder icon: any folder can have any picture as its icon.**
  The entry opens the file dialog filtered to the image formats this build can
  actually read, and the chosen picture — SVG, PNG, JPEG, WebP, QOI, whatever
  the image pipeline loads — is converted once to a 256 px QOI file in the
  application's config directory (`~/.config/UltraFiler/foldericons`,
  `%APPDATA%\UltraFiler\foldericons`, `~/Library/Application
  Support/UltraFiler/foldericons`). It is that copy the filer draws, so the
  icon keeps working when the original is moved, renamed or deleted, and it
  costs no format plugin to decode. The icon shows everywhere the folder does:
  every tab, the folder preview, the History and Favorites lists, its row in
  the folder tree and its bookmark under the tree's Pinned section. With
  several folders selected they all get it; with nothing selected the shown
  folder does. **Extras > Remove folder icon** takes it away again (and
  deletes the converted file) — it is enabled only while one of the selected
  folders actually carries one. A folder the user set an icon on keeps it over
  the built-in icon of a main user folder, which is the point of setting one
  on Pictures.
- The mapping lives in `foldericons.txt` beside the settings, as one
  tab-separated line per folder, and an entry whose converted icon has gone
  missing is dropped on load rather than drawn as nothing.

#### 2026-09-04 *1.19.1*
- **The folder tree shows its connecting lines again.** The tree was built with
  `TreeLineStyle::NoLine`, so a deep folder opened alongside its neighbours left
  the rows below it indented against nothing: which folder a subfolder belonged
  to was a matter of counting indents. Dotted connectors are back, in a gray
  that carries the structure without competing with the folder icons.
- **Folders with and without subfolders now start at the same place.** A folder
  with a `+` had its icon and name pushed one button width to the right of a
  folder without one, so a plain folder looked like it sat a half-level to the
  left of the folder above it. The framework now reserves the expander slot on
  every row (see `Docs/UltraCanvas/CHANGELOG.md` 0.3.99), and the tree lines up
  down each level.

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

#### 2026-09-04 *1.18.2*
- **Application icons stay on screen.** In a folder holding both large previews
  and many executables — a program's install directory, say — the `.exe` and
  `.dll` icons could vanish partway through browsing and not come back, leaving
  a wall of generic EXE/DLL glyphs. The cause and the fix are entirely in the
  framework's thumbnail cache: see framework 0.3.98. Nothing changed in
  UltraFiler itself; this release is what carries the fix to it.

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
