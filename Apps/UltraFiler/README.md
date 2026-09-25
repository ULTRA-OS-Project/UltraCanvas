# UltraFiler

A Windows Explorer style file manager built entirely from UltraCanvas
components:

This app versions itself: [`Docs/UltraFiler/CHANGELOG.md`](../../Docs/UltraFiler/CHANGELOG.md).

| Area | Component |
|---|---|
| Folder tree (left pane) | `UltraCanvasTreeView` — lazily populated filesystem tree (a curated Home, Cloud Storage, drives / mounted volumes) |
| Folder tabs (window top bar) | `UltraCanvasTabbedContainer` with its pages detached into the folder pane (`SetContentHost`), so the tab strip is the topmost bar of the window and its "+" ends the tab list |
| Folder content (center pane) | one `UltraCanvasFilerWidget` per tab, the active one shown in the tab strip's content host — details / list / thumbnail grids / size bars / treemap views, full file context menu, clipboard and drag & drop interop |
| Detail / preview (right pane) | `UltraCanvasMediaViewer` for a selected file — images, video, audio, PDFs, spreadsheets, 3D models and text files — and a second small-thumbnail `UltraCanvasFilerWidget` showing the content of a selected folder; the two share the pane |
| Path bar | `UltraCanvasBreadcrumb` via the shared `BuildFolderBreadcrumb` helper |
| Split view (two displays side by side) | a second `UltraCanvasFilerWidget` in a pane of the same `UltraCanvasSplitPane`, each display under a header row of a folder-tree `UltraCanvasButton` and its own `UltraCanvasBreadcrumb`; the one folder tree docks into whichever pane's button was pressed |
| Search field | a container holding an `UltraCanvasTextInput` — driving `UltraCanvasFilerWidget::SetNameFilter()` as-you-type — and the in-field **Scan sub folder** `UltraCanvasButton`, which starts the background sub-folder scan whose matches arrive through `ShowFileList()` / `AppendToFileList()` |
| History view | `UltraCanvasTabbedContainer` (Files / Folders / Apps) hosting one small-thumbnail `UltraCanvasFilerWidget` per tab, fed with `ShowFileList()` from `UltraFilerHistory` |
| Favorites view | the same tabbed layout, fed with `ShowFileList()` from `UltraFilerFavorites` (the pinned paths) |
| Panes | `UltraCanvasSplitPane` with draggable splitters |

## Features

- **Tabs:** the tab strip is the topmost bar of the window — above the
  toolbars, browser style — and its tabs name the folder each one shows. The
  **"+" at the end of the tab list** opens an additional tab — on the folder
  the active tab is showing, or on the Home folder, whichever *Settings >
  Handling > Tabs* is set to. Every tab has its own folder view, Back /
  Forward history, sort and view settings; tabs can be reordered by dragging
  and closed (the last one stays open). The strip stays visible while the
  History or Favorites view replaces the folder display, so clicking a tab
  returns to browsing it.
  home folder is the exception: its tab reads **Home**, under the home icon,
  rather than the account name the folder is named after — the same name and
  mark the folder tree's Home row and the Computer page's Home tile carry. A
  tab in one of the main user folders — Desktop, Documents, Downloads, Music,
  Pictures, Videos — carries that folder's icon, and keeps it *inside* the
  folder as well, so a tab deep in Downloads still says which of the user's
  places it is in; the nearest one wins where they are nested. The
  **"+" at the end of the tab list** opens an additional tab on the current
  folder. Every tab has its own folder view, Back / Forward history, sort and
  view settings; tabs can be reordered by dragging and closed (the last one
  stays open). The strip stays visible while the History or Favorites view
  replaces the folder display, so clicking a tab returns to browsing it.
- **Navigation:** Back / Forward history (per tab), Up, Refresh, the Split
  view toggle (see below), clickable breadcrumb path (each segment's dropdown
  lists sibling folders), folder tree with lazy expansion, and the History
  toggle (see below).
- **Split view:** the split-screen button in the navigation row (left of the
  clock) shows **two folder displays side by side** in place of the folder
  tree and the single display: the active tab's display on the left and a
  second display on the right, which has a Back / Forward history of its own
  and is in no tab. The two share the width the tree and the display had,
  with a draggable splitter between them. Each pane carries a **header row**
  of a folder-tree button and the pane's own breadcrumb, which navigates that
  pane. The **tree button** docks the folder tree down the left of that
  display, under its header, and takes it away again (also **Esc**); there is
  one tree, so pressing the other pane's button moves it over. A docked tree
  follows and navigates the display it sits beside. **The display clicked
  last is the active one** — its header is tinted — and it is what the
  navigation row, the command bar, the search field, the status bar and the
  preview pane act on, exactly as they act on the active tab; clicking a tab
  makes the left-hand pane active again, and the Computer page always opens
  in the left-hand pane. Files drag and drop between the two displays like
  between any two displays of the window. The right-hand display's folder
  and the switch itself are saved with the settings (`view.split`,
  `view.split.second.folder`), so the next start opens the pair as it was
  left. Pressing the button again brings the tree pane back as wide as it
  was.
- **Search:** typing in the field filters the shown folder **as-you-type**
  (case-insensitive name filter, no disk walk; the status bar notes the
  filter). A **Scan sub folder** button appears *inside* the search field as
  soon as there is something to search for; it — like **Enter** in the field,
  and like the centered **Scan sub folder** button the folder display shows
  when nothing in the folder matches — scans the current folder and everything
  under it for names containing the text (case-insensitive, up to 20 000
  matches).
  The scan runs on a worker thread and its matches appear **while it walks**,
  in batches, so the window stays usable and results can be opened before it
  finishes; the status bar counts matches and scanned folders as they come in.
  While a scan runs the in-field button reads **Stop** and ends it, keeping
  what was found. Symlinks and directory junctions are never entered (a
  reparse-point loop cannot make the scan run forever) and hidden entries are
  skipped, as in the folder tree. The matches are displayed in the tab's
  current view mode, with a *Path* column after the name in Details view, and
  the context menu's first entry, **Open path (in new tab)**, opens the
  selected match's folder in a new tab. Clearing the field, editing the query,
  navigating, switching tabs or closing the tab ends the scan and returns to
  the normal folder display. Each tab keeps its own search.
- **Type-ahead:** a letter typed anywhere outside a text field selects the
  first entry in the visible listing whose name starts with it; pressing the
  same letter again walks on to the next such entry (wrapping), Explorer
  style.
- **History:** the clock button in the navigation row replaces the folder tree
  and folder display with the History view — a tabbed container with **Files**,
  **Folders** and **Apps** tabs, each a filer widget in *small thumbnails* mode
  listing the recently used paths (most recent first) instead of a folder's
  content. Files and applications are remembered as they are opened
  (double-click / Enter). A folder is remembered only once **work has been
  done in it** — a file opened there, or something created, pasted, dropped in
  or out, renamed, duplicated, deleted, packed or extracted; browsing through
  a folder does not put it in the list. An entry whose file has meanwhile been
  deleted drops out of the list. Activating a tile leaves the History view
  and shows the entry in the folder display — a folder is opened, a file is
  selected inside the folder it lives in (so the preview picks it up when it is
  media); the context menu's **Open path (in new tab)** opens its folder in a
  new tab instead. Clicking the clock again, **Esc**, or
  any browsing action (navigation, search, a file command) returns to the
  folder view. The lists survive restarts — they are written to `history.txt`
  next to the settings whenever one of them changes, and read back at start-up,
  so nothing is lost if UltraFiler is killed rather than closed. Each list keeps
  the number of entries *Settings > Extras > History & Favorites > Limit of
  entries* is set to (10–1000, 300 by default), counted per section, with the
  oldest dropping off the end; *Settings ▸ Clear History* empties them.
- **Favorites (pinning):** the heart button next to the clock shows the
  Favorites view — the same **Files** / **Folders** / **Apps** layout as the
  History view, but listing what was pinned deliberately instead of what was
  used recently. The file context menu's **Extras** submenu does the pinning:
  its **Pin** and **Unpin** submenus each offer **To Favorites** — acting on
  the selection of the visible view (or, with nothing selected, the folder
  currently shown) — and **To Treeview** (folders only), which pins the
  folder into the folder tree's **Pinned** section, where it navigates like a
  bookmark on click. The entries are check items whose flag shows whether the
  selection is pinned there right now; Pin is enabled while something is
  still unpinned, Unpin while something is pinned. Entries keep the order
  they were pinned in and drop out when their path disappears from disk.
  Right-clicking the folder tree opens its context menu: **Copy** /
  **Delete** (with confirmation) / **Paste** act on the folder under the
  cursor — Paste only when a folder is under the cursor and the clipboard
  holds files, Delete never on the top-level roots — a **Pin** submenu whose
  **To Treeview** / **To Favorites** flags directly toggle where that folder
  is pinned, and **Unpin** (pinned entries only) removes the bookmark
  without touching the folder. The pins survive restarts — they are stored
  next to the settings as `favorites.txt` — and *Settings ▸ Clear Favorites*
  empties them (History and Favorites are separate stores).
- **Command bar:** the **New folder ▾** split button — its primary section
  creates a folder (also **Ctrl+F**, and *New > Folder* at the top of the
  file display's context menu), its arrow opens a menu with the same entries
  as the context menu's *New >* submenu (Folder, then Text / Doc /
  Spreadsheet / Bitmap / Vector / Audio / Video); inline rename starts
  automatically, and creating anything first ends the search (field, live
  filter and result display), so the fresh entry is visible — Cut / Copy /
  Paste (system clipboard interop), Rename, Delete (with confirmation),
  view type selection (every entry carries an icon of the layout it
  selects), sort field + direction, video preview mode, Preview toggle.
- **Live folder:** the file display rescans by itself when the folder changes
  behind it — another application saving a file into it, a download finishing,
  a script deleting one. The check runs on a background worker, and the refresh
  waits for any interaction it would interrupt (an open rename editor, a drag,
  a context menu, a file operation and its dialog).
- **Folder views:** the view type and sort order are remembered **per folder**,
  so a picture folder can stay on large thumbnails by date while a source folder
  stays on details by name. Entering a folder puts its own settings back; a
  folder that has none keeps whatever the previous one used. They are stored
  next to the settings as `folderviews.txt` (the 400 most recently entered
  folders) and *Settings > Extras > History & Favorites > Clear Folder views*
  forgets them.
- **Folder tree:** a **Pinned** section on top — above *Computer*, open, and
  shown only while something is pinned — then *Computer* with Home, **Cloud
  Storage** and the drives / volumes below it.
  - **Computer is a page of its own.** Clicking the entry (or *Up* from a
    drive root, or the breadcrumb's leading *Computer* node) replaces the
    folder display with the machine's places: **Folders** — Home and every
    Cloud Storage folder as folder tiles, which open on a double-click and
    carry the usual context menu — and **Drives** — one card per mounted
    volume with a **pie chart of used against free space** (green while there
    is room, amber past 75 %, red past 90 %, the percentage in the middle),
    the drive's name as the button that opens it, *232.9 GB free of 476.2 GB*
    and the mount point. The sizes are read off the UI thread, so a network
    share that stopped answering delays its own card and nothing else, and
    the cards follow mounts and unmounts like the tree's drive rows. The
    status bar sums the drives; Esc, any navigation or a tab switch returns
    to the folder display.
  - **Home** follows *Settings > Display > Home folder*. Curated (the Windows
    default), it lists the user's main folders — Desktop, Documents, Downloads,
    Music, Pictures, Videos — and stops there, so a profile does not spill
    *3D Objects*, *Saved Games*, *Links* and every working folder into the
    tree; *Show all content* (the Linux / macOS default) lists every subfolder,
    with the main folders keeping their icons. The paths come from the platform
    (`SHGetKnownFolderPath`, the macOS home layout, `xdg-user-dirs`), so a
    redirected or localized folder — *Bilder*, a Documents folder moved into
    OneDrive — is the one listed, under its own icon.
  - The **folder display follows the same setting**: curated, showing the home
    folder lists the main folders (wherever they physically live) plus the
    folder's files, and nothing else. **Display > Hidden files** in the context
    menu always reveals the full physical listing — that toggle means "show me
    everything", whatever the setting says.
  - **The home folder says what it is holding back.** While its display leaves
    anything out — the profile's hidden files (`NTUSER.DAT`, the
    profile junctions), the clutter *Display > Ignored files* drops
    (`Sti_Trace.log` and its like) and the subfolders the curation drops — a
    strip across the foot of the file display reads
    *"7 items are hidden here"* and carries a **Show hidden files** button that
    reveals them for that display. Another folder shows the strip only when an
    *ignored* name was dropped there — a setting's doing, which the user has no
    other way of noticing; a folder that merely leaves out its dot names stays
    quiet, as every file manager does. *Settings > Display > Files > Show
    hidden files* is the same choice made permanently, for every display.
  - **Cloud Storage** collects the sync folders this machine actually has —
    OneDrive (personal and every business tenant), Google Drive, Dropbox
    (personal and business) and iCloud Drive — instead of leaving them
    scattered through the profile, and instead of hiding a Google Drive that
    mounted as a virtual drive letter among the real drives. Each provider is
    asked where it put its folder (`UltraCanvas::GetCloudStorageFolders()`);
    the section is hidden entirely when there is nothing to show, and the
    lookup runs off the UI thread, so the window never waits for it. Like the
    drive roots, the cloud roots keep *Delete* disabled in the context menu —
    deleting one would sync the deletion to every other device. The lookup is
    repeated when a volume appears, because a cloud folder can arrive with one
    (that Google Drive mounted as its own drive letter).
  - **The drives follow the machine.** A USB stick, a card, an optical disc, a
    network share or a disk image connected while UltraFiler is running gets
    its row straight away, and loses it again when it is removed — the tree is
    not the start-up snapshot it used to be. The operating system reports the
    change (`UltraCanvasVolumeMonitor`: `WM_DEVICECHANGE` on Windows,
    `/proc/self/mountinfo` on Linux, `NSWorkspace` on macOS), so there is
    nothing polling in the background. A tab left inside a volume that went
    away is moved back to the home folder rather than showing a listing that
    no longer exists, and the status bar says which volume disconnected.
    *Refresh drives* in the tree's context menu runs the same pass by hand.
  - Volumes are looked for where each platform puts them: the drive letters on
    Windows, and `/media`, `/run/media` (both also one level down, for the
    per-user directory udisks creates), `/Volumes` and `/mnt` elsewhere.
    `/run/media` is the udisks2 location on Fedora, RHEL, Arch and openSUSE,
    and `/Volumes` is where every removable volume on macOS lands — neither
    used to be looked at, so on those systems a stick was missing from the
    tree even after a restart.
- **Archives:** packing and unpacking run in the background behind a progress
  window: a ring with the percentage, the file being handled and Cancel.
  Cancelling a pack removes the half-written archive; cancelling an unpack keeps
  what it already wrote.
- **File display:** everything `UltraCanvasFilerWidget` offers — sortable
  Details columns, thumbnail grids with async decoding, the size-bar and
  treemap views, hover icon menu, selection info bar, archive browsing
  (VirtualFS), compress / extract, drag & drop to and from other
  applications. Dropping dragged files onto a folder shown in the view moves
  them there; *Settings > Handling > Drag & Drop* switches that to copying.
  Ctrl at the drop always copies and Shift always moves. A move asks before it
  is carried out — a drag is easy to start by accident — which the same
  settings page turns off, or extends to copies. A move takes the
  dragged files out of the selection first, so the preview lets go of the file
  before it is renamed away.
- **Preview:** selecting a single previewable file shows it in the preview
  pane; double-click / Enter opens it there too. **Clicking a folder shows
  that folder's content in the same pane** — a small-thumbnail folder
  listing instead of a file preview, so a folder can be peeked into without
  leaving the one on display. The peek is live: double-clicking a subfolder
  in it descends further, a file activated in it opens with its OS default
  application, files can be dropped into it, and its context menu offers
  the usual file commands (only the hover icon menu stays off). While
  nothing previewable is selected the pane folds away, so the folder
  display always gets the whole width — the Preview toggle only enables /
  disables the feature, and **Esc** closes an open preview (turning the
  toggle off). The pane takes its width from the folder display only, so
  the folder tree and its splitter never move when the preview opens or
  closes; the width the pane is dragged to is restored on reopen, and the
  selected file is kept scrolled into view when the narrowed folder display
  would cut it off.
  The viewer provides zoom, rotation, color adjustments, slideshow, and
  per-kind views for documents, spreadsheets, models, audio and video.
  A file with real transparency - an alpha channel that is used, or a vector
  document (SVG) - shows a strip of backdrop colours right under the picture:
  greys and colours to click, and the checkered swatch to go back to the
  transparency pattern. What is picked there is saved, so the next preview
  opens with it; the strip is the only place this is set.
  While the preview is enabled, **deleting the previewed file selects its
  neighbour** (the next entry, or the previous one when it was the last), so
  the pane moves on to that file instead of folding away and snapping the
  folder display back to full width. The hover icon menu's Copy / Cut /
  Rename / Delete buttons act on the entry under the cursor without selecting
  it, so pressing one never re-targets or pops open the preview.
- **Video preview mode:** the command bar "Video" dropdown selects how a
  selected video plays in the preview: *Autoplay* (full playback with
  sound, the default), *5 s clip* (a five-second muted preview in the
  `UltraCanvasAlbum` hover style, then pause) or *Still image* (paused
  first frame).
- **Open files:** double-click / Enter either starts the program this system
  has registered for the file, exactly as a double-click in Explorer or the
  Finder does, or shows the file in the preview pane —
  *Settings > Handling > Opening files* chooses, and it ships set to the
  registered program on Windows and to the preview on Linux and macOS. A file
  type nothing is registered for is previewed whichever is set, so a
  double-click never comes to nothing; a file that cannot be previewed always
  goes to the system (`UltraCanvasFileAssociations`), and on Windows a type
  with no program behind it puts up the shell's own "How do you want to open
  this file?" chooser, again as Explorer does. **Open with >** is the first
  entry of the context menu, and
  clicking it does the same as a double-click — opens the selection with the
  default application; its submenu lists all registered applications for the
  selection (default first, with icons) plus **Other application…**, a
  file-dialog picker. Launches are detached, so closing UltraFiler leaves the
  opened applications running. The application lists
  are prewarmed in the background while a folder is shown, so the menu opens
  without any lookup delay.
- **Status bar:** entry count of the folder, selection count and summed size.
- **Folder icons:** the main user folders — Desktop, Documents, Downloads,
  Music, Pictures and Videos — are shown with icons of their own instead of
  the generic folder shape, in the file display and in the folder tree alike.
  **Extras > Set folder icon** gives *any* folder a picture of the user's
  choosing: it opens the file dialog filtered to the image formats the build
  can read, and converts the chosen file to a QOI icon kept in the config
  directory (`~/.config/UltraFiler/foldericons`), so the icon survives the
  original being moved or deleted. It is drawn everywhere the folder appears —
  every tab, the folder preview, the History and Favorites lists, the tree row
  and its Pinned bookmark — and beats the built-in icon of a main user folder.
  **Extras > Remove folder icon** takes it away again.
- **Extras > Find text…** (in the file context menu's Extras submenu): lists
  every file in the shown folder and its sub folders that contains a text.
  The dialog asks for the text, **In files named** — file-name patterns such
  as `*.cpp; *.h` (`*` any characters, `?` one; separated by `;`, `,` or
  blanks; empty for every file; the case of the name does not matter) — and
  **Match case**. The dialog remembers its last entries until UltraFiler
  closes. Results land in the same result display the search field's
  *Scan sub folder* fills, while the walk goes on; the status bar names the
  text, the patterns and the case option and counts the files found and
  read, and the field's *Stop* button ends it. Without *Match case* the
  comparison ignores the case of ASCII letters; other characters match
  exactly (UTF-8 bytes). Links, binary files (a NUL byte in the first 8 KB)
  and files over 64 MB are skipped, and so are hidden entries unless the
  display shows hidden files. Local folders only — the
  item is disabled on a remote drive and in the History, Favorites and
  Computer views.
- **Extras > Open prompt** (in the file context menu's Extras submenu):
  starts the operating system's command line program
  in the folder of the active tab, detached from UltraFiler (closing the file
  manager leaves the terminal running). Without configuration it uses the
  platform default — `%COMSPEC%` (cmd.exe) on Windows, Terminal.app on macOS,
  `$TERMINAL` or the first installed terminal emulator on Linux.

## Settings

The **Settings > Settings...** menu entry opens the settings window: a tree of
pages on the left, the selected page on the right. Its top level is the three
sections — *Display*, *Handling*, *Extras* — and the window opens with all
three closed, on a start page saying what each holds. Every page reads the same
way: a title, one line saying what the choice is about, the controls, and the
notes explaining the setting set apart in a tinted block at the foot of the
page. A page's *Restore default ...* button sits at the left end of the
window's bottom bar, opposite *Close*. Clicking a section opens it and moves on
to its first page, since a section has no page of its own. Every change applies
to the running application immediately and is saved to the config file
(`~/.config/UltraFiler/config.ini`, `%APPDATA%\UltraFiler\config.ini`,
`~/Library/Application Support/UltraFiler/config.ini`).

| Page | Setting |
|---|---|
| Display > Treeview | The folder tree's colours: the row background of the drive entries and the highlight of the selected folder, each picked with `UltraCanvasColorPicker` |
| Display > Home folder | What the Home folder shows, in the folder tree and the file display alike: **Show all content**, or **Show only predefined folders** (Desktop, Documents, Downloads, Music, Pictures, Videos, resolved through the platform). Defaults: curated on Windows — a profile there carries a dozen system folders — show all on Linux and macOS |
| Display > Files | **Show hidden files** — whether the file displays list what the platform calls hidden: a dot name everywhere, the hidden attribute on Windows (`NTUSER.DAT`, the profile junctions), the hidden flag on macOS. Off by default, as every file manager ships. It is what each folder display *starts* with: one display can still be switched on its own (its **Display > Hidden files** context-menu entry, or the Home folder's **Show hidden files** button) without changing the setting. Showing hidden files also shows the Home folder whole, whatever *Display > Home folder* says; the folder tree leaves hidden folders out either way |
| Display > Ignored files | **Hide known clutter files** — the built-in pattern list (`Sti_Trace.log`, `desktop.ini`, `Thumbs.db`, `ehthumbs.db`, `ntuser.dat*`, `ntuser.ini`, `.DS_Store`, `._*`, `.Trash-*`, `.directory`), each switchable on its own — plus **own patterns** typed into the field below it (globs: `*` any run, `?` one character, matched ignoring case), and whether they apply **only in the Home folder** (the default) or **in every folder**. This is what leaves out the clutter no hidden-file setting can reach, because the system gave it an ordinary unhidden name. Nothing is moved or deleted: an ignored file is only left out of the display, a search still finds it, and *Show hidden files* brings it back |
| Display > File icons | Whose icons the file display draws for a file that shows no picture of its own: **UltraFiler simple** — the drawn folder shape and the coloured sheet with the extension on it, the default and what every earlier release drew — or **Host OS icons**, what this desktop draws for the type (the shell's icons on Windows, Finder's on macOS, the installed icon theme's on Linux and BSD), so a folder listing looks like the rest of the desktop. Either way a file that shows a thumbnail of its own content keeps showing it, and a program or shortcut keeps the icon it carries inside itself. A type this system has no icon for keeps the simple one, and so does every icon until its lookup lands — the display never waits for the host. With host icons on, a folder is drawn with the system's folder icon, so the pictures inside it no longer peek out of it |
| Display > PDF Inventory | **PDF-Inventory thumbnails width** — how wide the page thumbnails beside a PDF shown in the preview are: a fixed width in pixels (a slider from 32 to 120 px, 56 px by default) or a share of the preview's own width (5–40 %, 25 % by default), so the inventory grows with the window. Moving either slider selects its mode |
| Handling > Drag & Drop | **Drop on folder** — whether dragging files onto a folder of the file display moves them (the default) or copies them. Ctrl at the drop always copies, Shift always moves. **Confirmation** — whether the drop asks before it is carried out: **Always**, **Only when files are moved** (the default) or **None**. The question names how many entries are about to be moved or copied and into which folder; files dragged in from another program are copies, so only *Always* asks about those |
| Handling > Opening files | **Double-click (or Enter) on a file** — **Start the registered program**, the way Explorer and the Finder do (the default on Windows), or **Show it in the preview**, keeping the file inside UltraFiler (the default on Linux and macOS). A file type this system has no program for is previewed either way, so the setting never turns a double-click into nothing happening; a file that cannot be previewed always goes to the system, and the context menu's *Open with* starts a program whichever is set |
| Handling > Tabs | **New tab** — what the **"+"** at the end of the tab strip opens: a **new view of the current folder** (the default) or the **Home folder**. Only the "+" follows this; a tab opened on a named folder — the containing folder of a search result, an entry of the History or Favorites view — still opens on that folder |
| Extras > Open prompt | The command line application started by **Extras > Open prompt** |
| Extras > History & Favorites | **Limit of entries** — how many entries each of the History view's three lists keeps: a slider from 10 to 1000, 300 by default. The limit counts *per section*, so Files, Folders and Apps each keep that many and a day of opening documents cannot push the remembered applications out. Lowering it takes effect at once — the oldest entries past the new limit are dropped and `history.txt` rewritten, not left on disk until some later restart. Also clears the recently-used lists, the pinned entries, and the per-folder view settings |

On the *Open prompt* page the folder button next to the path field opens the
file dialog filtered to applications (`*.exe`, `*.com`, `*.bat`, `*.cmd` on
Windows, `*.app` on macOS, all files on Linux); the chosen program lands in the
field and **Save app** stores it. **Use system default** clears the setting so
the platform's own command line program is used again, and **Test** starts the
program in the field right away to check the path.

## Usage

```bash
UltraFiler                  # opens the home folder
UltraFiler /path/to/folder  # opens a specific folder
```

## Building

Built by the top-level CMake project when `BUILD_ULTRAFILER_APP` is `ON`
(the default):

```bash
mkdir build && cd build && cmake .. && make UltraFiler
```
