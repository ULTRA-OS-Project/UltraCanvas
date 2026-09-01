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
| Search field | `UltraCanvasTextInput` driving `UltraCanvasFilerWidget::SetNameFilter()` as-you-type and `ShowFileList()` for the recursive search |
| History view | `UltraCanvasTabbedContainer` (Files / Folders / Apps) hosting one small-thumbnail `UltraCanvasFilerWidget` per tab, fed with `ShowFileList()` from `UltraFilerHistory` |
| Favorites view | the same tabbed layout, fed with `ShowFileList()` from `UltraFilerFavorites` (the pinned paths) |
| Panes | `UltraCanvasSplitPane` with draggable splitters |

## Features

- **Tabs:** the tab strip is the topmost bar of the window — above the
  toolbars, browser style — and its tabs name the folder each one shows. The
  **"+" at the end of the tab list** opens an additional tab on the current
  folder. Every tab has its own folder view, Back / Forward history, sort and
  view settings; tabs can be reordered by dragging and closed (the last one
  stays open). The strip stays visible while the History or Favorites view
  replaces the folder display, so clicking a tab returns to browsing it.
- **Navigation:** Back / Forward history (per tab), Up, Refresh, clickable
  breadcrumb path (each segment's dropdown lists sibling folders), folder
  tree with lazy expansion, and the History toggle (see below).
- **Search:** typing in the field filters the shown folder **as-you-type**
  (case-insensitive name filter, no disk walk; the status bar notes the
  filter). When nothing in the folder matches, a centered **Search in sub
  folders** button appears in the folder display; it — like **Enter** in the
  field — searches the current folder recursively for names containing the
  text (case-insensitive, up to 1000 matches). The recursive matches are
  displayed in the tab's current view mode, with a *Path* column after the
  name in Details view, and the context menu's first entry, **Open path (in
  new tab)**, opens the selected match's folder in a new tab. Clearing the
  field (or navigating anywhere) returns to the normal folder display. Each
  tab keeps its own search.
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
  folder view. The lists survive restarts — they are stored next to the
  settings as `history.txt` — and *Settings ▸ Clear History* empties them.
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
  sort field + direction, view type selection, video preview mode, Preview
  toggle.
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
  folders) and *Settings > History & Favorites > Clear Folder views* forgets
  them.
- **Folder tree:** a **Pinned** section on top — above *Computer*, open, and
  shown only while something is pinned — then *Computer* with Home, **Cloud
  Storage** and the drives / volumes below it.
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
  Ctrl at the drop always copies and Shift always moves. A move takes the
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
  opens with it (it is the same setting as *Settings > Media Viewer >
  Transparent Images*).
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
- **Open files:** double-click / Enter shows a previewable file in the
  preview pane and launches every other file with the application the OS
  registers for it (`UltraCanvasFileAssociations`), like a double-click in
  Explorer. **Open with >** is the first entry of the context menu, and
  clicking it does the same as a double-click — opens the selection with the
  default application; its submenu lists all registered applications for the
  selection (default first, with icons) plus **Other application…**, a
  file-dialog picker. Launches are detached, so closing UltraFiler leaves the
  opened applications running. The application lists
  are prewarmed in the background while a folder is shown, so the menu opens
  without any lookup delay.
- **Status bar:** entry count of the folder, selection count and summed size.
- **Extras > Open prompt** (in the file context menu's Extras submenu):
  starts the operating system's command line program
  in the folder of the active tab, detached from UltraFiler (closing the file
  manager leaves the terminal running). Without configuration it uses the
  platform default — `%COMSPEC%` (cmd.exe) on Windows, Terminal.app on macOS,
  `$TERMINAL` or the first installed terminal emulator on Linux.

## Settings

The **Settings > Settings...** menu entry opens the settings window: a tree of
pages on the left, the selected page on the right. Every change applies to the
running application immediately and is saved to the config file
(`~/.config/UltraFiler/config.ini`, `%APPDATA%\UltraFiler\config.ini`,
`~/Library/Application Support/UltraFiler/config.ini`).

| Page | Setting |
|---|---|
| Display > Treeview | The folder tree's colours: the row background of the drive entries and the highlight of the selected folder, each picked with `UltraCanvasColorPicker` |
| Display > Home folder | What the Home folder shows, in the folder tree and the file display alike: **Show all content**, or **Show only predefined folders** (Desktop, Documents, Downloads, Music, Pictures, Videos, resolved through the platform). Defaults: curated on Windows — a profile there carries a dozen system folders — show all on Linux and macOS |
| Display > PDF Inventory | **PDF-Inventory thumbnails width** — how wide the page thumbnails beside a PDF shown in the preview are: a fixed width in pixels (a slider from 32 to 120 px, 56 px by default) or a share of the preview's own width (5–40 %, 25 % by default), so the inventory grows with the window. Moving either slider selects its mode |
| Media Viewer > Transparent Images | Backdrop shown behind transparent images in the preview: checkered pattern or a preset colour picked with `UltraCanvasColorPicker`. The colour strip under a transparent image in the preview writes to the same setting |
| Handling > Drag & Drop | **Drop on folder** — whether dragging files onto a folder of the file display moves them (the default) or copies them. Ctrl at the drop always copies, Shift always moves |
| History & Favorites | Clears the recently-used lists, the pinned entries, and the per-folder view settings |
| Extras > Open prompt | The command line application started by **Extras > Open prompt** |

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
